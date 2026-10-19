/*
 * the scaling api. user32 keeps the process awareness on windows and shcore
 * asks it; mime's win32k keeps the value. a monitor has the dpi of its display,
 * a program aware of that sees it and the others see the system's.
 */
#include "shcore_private.h"

#define ERROR_BAD_ARGUMENTS 160L

/* set once per process, as windows allows it; an awareness from the manifest does not count yet. */
static LONG g_awareness_set;

HRESULT WINAPI SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value)
{
    if (value != PROCESS_DPI_UNAWARE && value != PROCESS_SYSTEM_DPI_AWARE && value != PROCESS_PER_MONITOR_DPI_AWARE)
        return E_INVALIDARG;
    if (InterlockedCompareExchange(&g_awareness_set, 1, 0))
        return E_ACCESSDENIED;
    NtUserCallOneParam((ULONG_PTR)value, MIME_ONEPARAM_SETPROCESSDPIAWARENESS);
    return S_OK;
}

HRESULT WINAPI GetProcessDpiAwareness(HANDLE process, PROCESS_DPI_AWARENESS *value)
{
    if (!value)
        return E_INVALIDARG;
    if (process && GetProcessId(process) != GetCurrentProcessId()) {
        *value = (PROCESS_DPI_AWARENESS)-1;
        return E_INVALIDARG;
    }
    ULONG_PTR awareness = NtUserCallOneParam(0, MIME_ONEPARAM_GETPROCESSDPIAWARENESS);
    /* the per monitor v2 awareness is the per monitor one to this api. */
    *value = awareness <= PROCESS_PER_MONITOR_DPI_AWARE ? (PROCESS_DPI_AWARENESS)awareness
             : awareness == 3                           ? PROCESS_PER_MONITOR_DPI_AWARE
                                                        : PROCESS_SYSTEM_DPI_AWARE;
    return S_OK;
}

/* the dpi of the display of a monitor, 0 for a handle that is none. */
static UINT monitor_dpi(HMONITOR monitor)
{
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = (ULONG)NtUserCallTwoParam((ULONG_PTR)list, MIME_USER_MAX_MONITORS, MIME_TWOPARAM_GETMONITORS);
    ULONG_PTR index = (ULONG_PTR)monitor - 0x00010001;
    if (!monitor || count > MIME_USER_MAX_MONITORS || (ULONG_PTR)monitor < 0x00010001 || index >= count)
        return 0;
    return list[index].Dpi ? list[index].Dpi : 96;
}

/* an unaware process sees 96 everywhere, a system aware one the system's, one aware of monitors the monitor's. */
static UINT process_dpi(UINT display)
{
    PROCESS_DPI_AWARENESS awareness;
    if (SUCCEEDED(GetProcessDpiAwareness(nullptr, &awareness))) {
        if (awareness == PROCESS_DPI_UNAWARE)
            return 96;
        if (awareness == PROCESS_PER_MONITOR_DPI_AWARE)
            return display;
    }
    return GetDpiForSystem();
}

HRESULT WINAPI GetDpiForMonitor(HMONITOR monitor, MONITOR_DPI_TYPE type, UINT *x, UINT *y)
{
    if (type != MDT_EFFECTIVE_DPI && type != MDT_ANGULAR_DPI && type != MDT_RAW_DPI)
        return HRESULT_FROM_WIN32(ERROR_BAD_ARGUMENTS);
    UINT dpi = x && y ? monitor_dpi(monitor) : 0;
    if (!dpi)
        return E_INVALIDARG;
    *x = *y = type == MDT_EFFECTIVE_DPI ? process_dpi(dpi) : dpi;
    return S_OK;
}

/* the scale factors windows names, the dpi's nearest. */
static DEVICE_SCALE_FACTOR scale_for_dpi(UINT dpi)
{
    static const DEVICE_SCALE_FACTOR factors[] = {
        SCALE_100_PERCENT, SCALE_120_PERCENT, SCALE_125_PERCENT, SCALE_140_PERCENT, SCALE_150_PERCENT,
        SCALE_160_PERCENT, SCALE_175_PERCENT, SCALE_180_PERCENT, SCALE_200_PERCENT, SCALE_225_PERCENT,
        SCALE_250_PERCENT, SCALE_300_PERCENT, SCALE_350_PERCENT, SCALE_400_PERCENT, SCALE_450_PERCENT,
        SCALE_500_PERCENT,
    };
    UINT percent = (dpi * 100 + 48) / 96;
    DEVICE_SCALE_FACTOR best = SCALE_100_PERCENT;
    for (UINT i = 0; i < sizeof(factors) / sizeof(factors[0]); i++)
        if ((UINT)factors[i] <= percent)
            best = factors[i];
    return best;
}

HRESULT WINAPI GetScaleFactorForMonitor(HMONITOR monitor, DEVICE_SCALE_FACTOR *scale)
{
    *scale = SCALE_100_PERCENT;
    UINT dpi = monitor_dpi(monitor);
    if (!dpi)
        return E_INVALIDARG;
    *scale = scale_for_dpi(dpi);
    return S_OK;
}

DEVICE_SCALE_FACTOR WINAPI GetScaleFactorForDevice(DISPLAY_DEVICE_TYPE type)
{
    return scale_for_dpi(GetDpiForSystem());
}

UINT WINAPI GetDpiForShellUIComponent(SHELL_UI_COMPONENT component)
{
    return GetDpiForSystem();
}

/* the scale never changes in mime, so a registration only hands out its cookie. */
static LONG g_next_cookie;

HRESULT WINAPI RegisterScaleChangeNotifications(DISPLAY_DEVICE_TYPE type, HWND window, UINT message, DWORD *cookie)
{
    *cookie = 0;
    if (!IsWindow(window))
        return E_INVALIDARG;
    *cookie = (DWORD)InterlockedIncrement(&g_next_cookie);
    return S_OK;
}

HRESULT WINAPI RevokeScaleChangeNotifications(DISPLAY_DEVICE_TYPE type, DWORD cookie)
{
    return S_OK;
}

HRESULT WINAPI RegisterScaleChangeEvent(HANDLE event, DWORD_PTR *cookie)
{
    if (!event || !cookie)
        return E_INVALIDARG;
    *cookie = (DWORD_PTR)InterlockedIncrement(&g_next_cookie);
    return S_OK;
}

HRESULT WINAPI UnregisterScaleChangeEvent(DWORD_PTR cookie)
{
    return S_OK;
}
