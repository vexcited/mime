#include "wg_private.h"
#include <mime/win32/shellscalingapi.h>
#include <mime/win32/winspool.h>
#include <rtl.h>

#define DMDO_DEFAULT 0
#define DMDO_90 1
#define DMDO_180 2
#define DMDO_270 3

static const WCHAR g_monitor_class[] = L"{4D36E96E-E325-11CE-BFC1-08002BE10318}";

HMONITOR WgMonitorOf(HWND window, HMONITOR monitor)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (window)
        monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &info))
        return monitor;
    return MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

/* raw pixels and rotation come from the mode of the display, which does not depend on the dpi awareness. */
void WgQueryMode(HMONITOR monitor, WG_STATE *state)
{
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    DWORD rotation = DMDO_DEFAULT;
    UINT32 width = 0, height = 0;
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info)) {
        width = (UINT32)(info.rcMonitor.right - info.rcMonitor.left);
        height = (UINT32)(info.rcMonitor.bottom - info.rcMonitor.top);
        if (EnumDisplaySettingsExW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0)) {
            if (mode.dmFields & DM_PELSWIDTH)
                width = mode.dmPelsWidth;
            if (mode.dmFields & DM_PELSHEIGHT)
                height = mode.dmPelsHeight;
            if (mode.dmFields & DM_DISPLAYORIENTATION)
                rotation = mode.dmDisplayOrientation;
        }
    }
    state->Width = width;
    state->Height = height;
    BOOL turned = rotation == DMDO_90 || rotation == DMDO_270;
    UINT32 native_width = turned ? height : width, native_height = turned ? width : height;
    BOOL landscape = native_width >= native_height;
    static const DisplayOrientations from_landscape[] = {DisplayOrientations_Landscape, DisplayOrientations_Portrait,
                                                         DisplayOrientations_LandscapeFlipped,
                                                         DisplayOrientations_PortraitFlipped};
    static const DisplayOrientations from_portrait[] = {DisplayOrientations_Portrait,
                                                        DisplayOrientations_LandscapeFlipped,
                                                        DisplayOrientations_PortraitFlipped,
                                                        DisplayOrientations_Landscape};
    state->Native = landscape ? DisplayOrientations_Landscape : DisplayOrientations_Portrait;
    state->Current = (landscape ? from_landscape : from_portrait)[rotation & 3];
}

/*
 * a standard dynamic range panel with the primaries of srgb, as dxgi reports the output in DXGI_OUTPUT_DESC1. its
 * sdr content shows at the 80 nits of the srgb reference white.
 */
void WgQueryColor(HMONITOR monitor, WG_COLOR *color)
{
    *color = (WG_COLOR){
        .Kind = AdvancedColorKind_StandardDynamicRange,
        .Available = 1u << AdvancedColorKind_StandardDynamicRange,
        .Red = {0.640f, 0.330f},
        .Green = {0.300f, 0.600f},
        .Blue = {0.150f, 0.060f},
        .White = {0.3127f, 0.3290f},
        .MinLuminance = 0.5f,
        .MaxLuminance = 270.0f,
        .MaxFullFrameLuminance = 270.0f,
        .SdrWhiteLevel = 80.0f,
    };
}

/* a scale below 100 percent reads as 100, as Windows rounds it. */
UINT WgQueryScale(HMONITOR monitor)
{
    DEVICE_SCALE_FACTOR scale = SCALE_100_PERCENT;
    if (FAILED(GetScaleFactorForMonitor(monitor, &scale)) || scale < SCALE_100_PERCENT)
        scale = SCALE_100_PERCENT;
    return (UINT)scale;
}

void WgQueryRawDpi(HMONITOR monitor, float *x, float *y)
{
    UINT dpi_x = 0, dpi_y = 0;
    if (FAILED(GetDpiForMonitor(monitor, MDT_RAW_DPI, &dpi_x, &dpi_y)))
        dpi_x = dpi_y = 0;
    *x = (float)dpi_x;
    *y = (float)dpi_y;
}

void WgQueryState(HMONITOR monitor, WG_STATE *state)
{
    memset(state, 0, sizeof(*state));
    state->Monitor = monitor;
    state->Scale = WgQueryScale(monitor);
    WgQueryRawDpi(monitor, &state->RawDpiX, &state->RawDpiY);
    WgQueryMode(monitor, state);
    if (FAILED(WgColorProfilePath(monitor, state->Profile, MAX_PATH)))
        state->Profile[0] = 0;
    WgQueryColor(monitor, &state->Color);
}

static BOOL color_directory(WCHAR *path, UINT32 capacity)
{
    static const WCHAR tail[] = L"\\spool\\drivers\\color\\";
    UINT n = GetSystemDirectoryW(path, capacity);
    if (!n || n + RTL_NUMBER_OF(tail) > capacity)
        return FALSE;
    memcpy(path + n, tail, sizeof(tail));
    return TRUE;
}

/* the last profile of the list whose file is in the color directory, as Windows picks the default one. */
static HRESULT profile_from_registry(HKEY root, LPCWSTR key, WCHAR *path, UINT32 capacity)
{
    DWORD size = 0;
    LSTATUS status = RegGetValueW(root, key, L"ICMProfile", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size);
    if (status)
        return HRESULT_FROM_WIN32(status);
    WCHAR *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 2 * sizeof(WCHAR));
    if (!list)
        return E_OUTOFMEMORY;
    status = RegGetValueW(root, key, L"ICMProfile", RRF_RT_REG_MULTI_SZ, nullptr, list, &size);
    HRESULT hr = status ? HRESULT_FROM_WIN32(status) : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    WCHAR directory[MAX_PATH];
    if (!status && !color_directory(directory, MAX_PATH))
        hr = E_FAIL;
    LPCWSTR names[64];
    UINT count = 0;
    for (WCHAR *p = list; !status && *p && count < RTL_NUMBER_OF(names); p += wcslen(p) + 1)
        names[count++] = p;
    while (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && count) {
        LPCWSTR name = names[--count], slash = wcsrchr(name, L'\\');
        name = slash ? slash + 1 : name;
        if (wcslen(directory) + wcslen(name) >= capacity)
            continue;
        wcscpy(path, directory);
        wcscat(path, name);
        DWORD attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            hr = S_OK;
    }
    HeapFree(GetProcessHeap(), 0, list);
    return hr;
}

/*
 * the profile associated with the monitor, per user when the user asked for it, else for the machine. a monitor
 * without one shows srgb, whose profile is the one the color directory has for it.
 */
HRESULT WgColorProfilePath(HMONITOR monitor, WCHAR *path, UINT32 capacity)
{
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    DISPLAY_DEVICEW device = {.cb = sizeof(device)};
    LPCWSTR instance = L"\\0000";
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) && EnumDisplayDevicesW(info.szDevice, 0, &device, 0)) {
        LPCWSTR slash = wcsrchr(device.DeviceID, L'\\');
        if (slash && wcslen(slash) <= 5)
            instance = slash;
    }
    WCHAR key[160];
    wcscpy(key, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\ICM\\ProfileAssociations\\Display\\");
    wcscat(wcscat(key, g_monitor_class), instance);
    DWORD per_user = 0, size = sizeof(per_user);
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!RegGetValueW(HKEY_CURRENT_USER, key, L"UsePerUserProfiles", RRF_RT_REG_DWORD, nullptr, &per_user, &size) &&
        per_user == 1)
        hr = profile_from_registry(HKEY_CURRENT_USER, key, path, capacity);
    if (FAILED(hr)) {
        wcscpy(key, L"SYSTEM\\CurrentControlSet\\Control\\Class\\");
        wcscat(wcscat(key, g_monitor_class), instance);
        hr = profile_from_registry(HKEY_LOCAL_MACHINE, key, path, capacity);
    }
    if (SUCCEEDED(hr))
        return hr;
    static const WCHAR srgb[] = L"sRGB Color Space Profile.icm";
    if (!color_directory(path, capacity) || wcslen(path) + RTL_NUMBER_OF(srgb) > capacity)
        return E_FAIL;
    wcscat(path, srgb);
    return S_OK;
}
