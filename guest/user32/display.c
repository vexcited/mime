/* display settings for the single virtual monitor. */
#include "user32_private.h"
#include <mime/win32/winspool.h>

/* win32k owns and changes the display modes, see guest/win32k/display.c. */
WINUSERAPI BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR device, DWORD index, DEVMODEW *mode, DWORD flags)
{
    if (!mode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING name;
    if (device)
        RtlInitUnicodeString(&name, device);
    return NT_SUCCESS(NtUserEnumDisplaySettings(device ? &name : nullptr, index, mode, flags));
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsW(LPCWSTR device, DWORD index, DEVMODEW *mode)
{
    return EnumDisplaySettingsExW(device, index, mode, 0);
}

/* DEVMODEA has the same fields, with the two names as byte strings. */
static void mode_to_ansi(const DEVMODEW *w, DEVMODEA *a, WORD size)
{
    DEVMODEA m;
    memset(&m, 0, sizeof(m));
    WideCharToMultiByte(CP_ACP, 0, w->dmDeviceName, -1, (LPSTR)m.dmDeviceName, sizeof(m.dmDeviceName), nullptr,
                        nullptr);
    memcpy(&m.dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEW, dmFormName) - offsetof(DEVMODEW, dmSpecVersion));
    WideCharToMultiByte(CP_ACP, 0, w->dmFormName, -1, (LPSTR)m.dmFormName, sizeof(m.dmFormName), nullptr, nullptr);
    memcpy(&m.dmLogPixels, &w->dmLogPixels, sizeof(DEVMODEW) - offsetof(DEVMODEW, dmLogPixels));
    m.dmSize = size < sizeof(m) ? size : sizeof(m);
    m.dmDriverExtra = 0;
    memcpy(a, &m, m.dmSize);
}

static void mode_from_ansi(const DEVMODEA *a, DEVMODEW *w)
{
    DEVMODEA m;
    memset(&m, 0, sizeof(m));
    memcpy(&m, a, a->dmSize < sizeof(m) ? a->dmSize : sizeof(m));
    memset(w, 0, sizeof(*w));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)m.dmDeviceName, sizeof(m.dmDeviceName), w->dmDeviceName,
                        RTL_NUMBER_OF(w->dmDeviceName));
    w->dmDeviceName[RTL_NUMBER_OF(w->dmDeviceName) - 1] = 0;
    memcpy(&w->dmSpecVersion, &m.dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)m.dmFormName, sizeof(m.dmFormName), w->dmFormName,
                        RTL_NUMBER_OF(w->dmFormName));
    w->dmFormName[RTL_NUMBER_OF(w->dmFormName) - 1] = 0;
    memcpy(&w->dmLogPixels, &m.dmLogPixels, sizeof(DEVMODEA) - offsetof(DEVMODEA, dmLogPixels));
    /* dmSize counts byte names for ansi and wide names for wide. */
    w->dmSize = a->dmSize >= sizeof(DEVMODEA) ? sizeof(DEVMODEW) : a->dmSize + sizeof(DEVMODEW) - sizeof(DEVMODEA);
    w->dmDriverExtra = 0;
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsExA(LPCSTR device, DWORD index, DEVMODEA *mode, DWORD flags)
{
    if (!mode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LPWSTR name = UserAtoW(device);
    DEVMODEW w = {.dmSize = sizeof(w)};
    BOOL ok = EnumDisplaySettingsExW(name, index, &w, flags);
    UserFreeConverted(name, device);
    if (ok)
        mode_to_ansi(&w, mode, mode->dmSize);
    return ok;
}

WINUSERAPI BOOL WINAPI EnumDisplaySettingsA(LPCSTR device, DWORD index, DEVMODEA *mode)
{
    return EnumDisplaySettingsExA(device, index, mode, 0);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR device, DEVMODEW *mode, HWND window, DWORD flags,
                                                LPVOID param)
{
    UNICODE_STRING name;
    if (device)
        RtlInitUnicodeString(&name, device);
    return NtUserChangeDisplaySettings(device ? &name : nullptr, mode, flags, param);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsW(DEVMODEW *mode, DWORD flags)
{
    return ChangeDisplaySettingsExW(nullptr, mode, nullptr, flags, nullptr);
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsExA(LPCSTR device, DEVMODEA *mode, HWND window, DWORD flags,
                                                LPVOID param)
{
    DEVMODEW w;
    if (mode)
        mode_from_ansi(mode, &w);
    LPWSTR name = UserAtoW(device);
    LONG result = ChangeDisplaySettingsExW(name, mode ? &w : nullptr, window, flags, param);
    UserFreeConverted(name, device);
    return result;
}

WINUSERAPI LONG WINAPI ChangeDisplaySettingsA(DEVMODEA *mode, DWORD flags)
{
    return ChangeDisplaySettingsExA(nullptr, mode, nullptr, flags, nullptr);
}

/* ------------------------------------------------------------------ display configuration */

/* one path per host monitor: source and target carry its index, the target 0x100 above. */
#define CONFIG_TARGET 0x100
#define CONFIG_ADAPTER 0x0000D3D0 /* the luid dxgi gives the first adapter. */
#define CONFIG_DISPLAYPORT 10

#define QDC_ALL_PATHS 1
#define QDC_ONLY_ACTIVE_PATHS 2
#define QDC_DATABASE_CURRENT 4

typedef struct DISPLAYCONFIG_RATIONAL {
    UINT32 Numerator, Denominator;
} DISPLAYCONFIG_RATIONAL;

typedef struct DISPLAYCONFIG_PATH_INFO {
    struct {
        LUID adapterId;
        UINT32 id;
        UINT32 modeInfoIdx;
        UINT32 statusFlags;
    } sourceInfo;
    struct {
        LUID adapterId;
        UINT32 id;
        UINT32 modeInfoIdx;
        UINT32 outputTechnology;
        UINT32 rotation;
        UINT32 scaling;
        DISPLAYCONFIG_RATIONAL refreshRate;
        UINT32 scanLineOrdering;
        BOOL targetAvailable;
        UINT32 statusFlags;
    } targetInfo;
    UINT32 flags;
} DISPLAYCONFIG_PATH_INFO;

typedef struct DISPLAYCONFIG_MODE_INFO {
    UINT32 infoType;
    UINT32 id;
    LUID adapterId;
    union {
        struct {
            UINT64 pixelRate;
            DISPLAYCONFIG_RATIONAL hSyncFreq, vSyncFreq;
            SIZE activeSize, totalSize;
            UINT32 videoStandard;
            UINT32 scanLineOrdering;
        } target;
        struct {
            UINT32 width, height;
            UINT32 pixelFormat;
            POINTL position;
        } source;
    };
} DISPLAYCONFIG_MODE_INFO;

typedef struct DISPLAYCONFIG_DEVICE_INFO_HEADER {
    UINT32 type;
    UINT32 size;
    LUID adapterId;
    UINT32 id;
} DISPLAYCONFIG_DEVICE_INFO_HEADER;

typedef struct DISPLAYCONFIG_SOURCE_DEVICE_NAME {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    WCHAR viewGdiDeviceName[32];
} DISPLAYCONFIG_SOURCE_DEVICE_NAME;

typedef struct DISPLAYCONFIG_TARGET_DEVICE_NAME {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 flags;
    UINT32 outputTechnology;
    UINT16 edidManufactureId;
    UINT16 edidProductCodeId;
    UINT32 connectorInstance;
    WCHAR monitorFriendlyDeviceName[64];
    WCHAR monitorDevicePath[128];
} DISPLAYCONFIG_TARGET_DEVICE_NAME;

/* the base type, both advanced color answers and the white level: a header and up to four values. */
typedef struct DISPLAYCONFIG_VALUES {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value[4];
} DISPLAYCONFIG_VALUES;

static_assert(sizeof(DISPLAYCONFIG_PATH_INFO) == 72 && sizeof(DISPLAYCONFIG_MODE_INFO) == 64, "display paths");
static_assert(sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME) == 84 && sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME) == 420,
              "display names");

static ULONG config_monitors(MIME_MONITOR_INFO *list)
{
    ULONG count = (ULONG)NtUserCallTwoParam((ULONG_PTR)list, MIME_USER_MAX_MONITORS, MIME_TWOPARAM_GETMONITORS);
    return count > MIME_USER_MAX_MONITORS ? 0 : count;
}

static BOOL config_flags(UINT flags)
{
    UINT kind = flags & (QDC_ALL_PATHS | QDC_ONLY_ACTIVE_PATHS | QDC_DATABASE_CURRENT);
    return kind && !(kind & (kind - 1));
}

WINUSERAPI LONG WINAPI GetDisplayConfigBufferSizes(UINT flags, UINT *paths, UINT *modes)
{
    if (!paths || !modes)
        return ERROR_INVALID_PARAMETER;
    *paths = *modes = 0;
    if (!config_flags(flags))
        return ERROR_INVALID_PARAMETER;
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    *paths = config_monitors(list);
    *modes = *paths * 2;
    return ERROR_SUCCESS;
}

/* every path is active, so all three kinds of query answer the same. */
WINUSERAPI LONG WINAPI QueryDisplayConfig(UINT flags, UINT *paths, void *path_data, UINT *modes,
                                         void *mode_data, UINT *topology)
{
    if (!paths || !modes || !path_data || !mode_data || !config_flags(flags) ||
        !(flags & QDC_DATABASE_CURRENT) != !topology)
        return ERROR_INVALID_PARAMETER;
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = config_monitors(list);
    if (*paths < count || *modes < count * 2)
        return ERROR_INSUFFICIENT_BUFFER;
    DISPLAYCONFIG_PATH_INFO *path = path_data;
    DISPLAYCONFIG_MODE_INFO *mode = mode_data;
    memset(path, 0, count * sizeof(*path));
    memset(mode, 0, count * 2 * sizeof(*mode));
    for (ULONG i = 0; i < count; i++) {
        const MIME_MONITOR_INFO *m = &list[i];
        UINT32 width = m->Rect.right - m->Rect.left, height = m->Rect.bottom - m->Rect.top;
        UINT32 rate = m->RefreshHz ? m->RefreshHz : 60;
        DISPLAYCONFIG_PATH_INFO *p = &path[i];
        p->sourceInfo.adapterId.LowPart = p->targetInfo.adapterId.LowPart = CONFIG_ADAPTER;
        p->sourceInfo.id = i;
        p->sourceInfo.modeInfoIdx = i * 2;
        p->sourceInfo.statusFlags = 1; /* in use. */
        p->targetInfo.id = CONFIG_TARGET + i;
        p->targetInfo.modeInfoIdx = i * 2 + 1;
        p->targetInfo.outputTechnology = CONFIG_DISPLAYPORT;
        p->targetInfo.rotation = 1; /* identity, and so is the scaling. */
        p->targetInfo.scaling = 1;
        p->targetInfo.refreshRate = (DISPLAYCONFIG_RATIONAL){rate, 1};
        p->targetInfo.scanLineOrdering = 1; /* progressive. */
        p->targetInfo.targetAvailable = TRUE;
        p->targetInfo.statusFlags = 1;
        p->flags = 1; /* active. */
        DISPLAYCONFIG_MODE_INFO *source = &mode[i * 2], *target = source + 1;
        source->infoType = 1;
        source->id = i;
        source->adapterId.LowPart = target->adapterId.LowPart = CONFIG_ADAPTER;
        source->source.width = width;
        source->source.height = height;
        source->source.pixelFormat = 4; /* 32 bits per pixel. */
        source->source.position = (POINTL){m->Rect.left, m->Rect.top};
        target->infoType = 2;
        target->id = CONFIG_TARGET + i;
        target->target.pixelRate = (UINT64)width * height * rate;
        target->target.hSyncFreq = (DISPLAYCONFIG_RATIONAL){rate * height, 1};
        target->target.vSyncFreq = (DISPLAYCONFIG_RATIONAL){rate, 1};
        target->target.activeSize = target->target.totalSize = (SIZE){(LONG)width, (LONG)height};
        target->target.videoStandard = 255; /* other. */
        target->target.scanLineOrdering = 1;
    }
    *paths = count;
    *modes = count * 2;
    if (topology)
        *topology = count > 1 ? 4 /* extend */ : 1 /* internal */;
    return ERROR_SUCCESS;
}

WINUSERAPI LONG WINAPI DisplayConfigGetDeviceInfo(void *info)
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER *header = info;
    if (!header)
        return ERROR_INVALID_PARAMETER;
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = config_monitors(list);
    ULONG index = header->id & (CONFIG_TARGET - 1);
    if (index >= count)
        return ERROR_GEN_FAILURE;
    const MIME_MONITOR_INFO *m = &list[index];
    BOOL hdr = !!(m->Flags & MIME_MONITORINFOF_HDR);
    DISPLAYCONFIG_VALUES *values = info;
    UINT32 value[4] = {};
    UINT needed;
    switch (header->type) {
    case 1: { /* the source's gdi name. */
        DISPLAYCONFIG_SOURCE_DEVICE_NAME *name = info;
        if (header->size < sizeof(*name))
            return ERROR_INVALID_PARAMETER;
        wcscpy(name->viewGdiDeviceName, m->Device);
        return ERROR_SUCCESS;
    }
    case 2: { /* the target's name, with the device interface EnumDisplayDevicesW gives the monitor. */
        DISPLAYCONFIG_TARGET_DEVICE_NAME *name = info;
        if (header->size < sizeof(*name))
            return ERROR_INVALID_PARAMETER;
        memset(&name->flags, 0, sizeof(*name) - sizeof(*header));
        name->outputTechnology = CONFIG_DISPLAYPORT;
        name->connectorInstance = index;
        name->flags = m->Name[0] ? 1 : 0; /* the friendly name is the monitor's own. */
        wcscpy(name->monitorFriendlyDeviceName, m->Name);
        wsprintfW(name->monitorDevicePath,
                  L"\\\\?\\DISPLAY#MS_0025#5&2f3c1a9b&0&UID%04u#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}", index);
        return ERROR_SUCCESS;
    }
    case 6: /* the base output technology. */
        needed = 1;
        value[0] = CONFIG_DISPLAYPORT;
        break;
    case 9: /* advanced color: supported and enabled, rgb, bits per channel. */
        needed = 3;
        value[0] = hdr ? 3 : 0;
        value[1] = 0;
        value[2] = hdr ? 10 : 8;
        break;
    case 11: /* where sdr white shows, in thousandths of 80 nits. */
        needed = 1;
        value[0] = hdr ? (m->Color.WhiteNits ? m->Color.WhiteNits / 800 : 2537) : 1000;
        break;
    case 15: /* advanced color 2: supported, active, high dynamic range supported and enabled by the user. */
        needed = 4;
        value[0] = hdr ? 0x33 : 0;
        value[1] = 0;
        value[2] = hdr ? 10 : 8;
        value[3] = hdr ? 2 : 0;
        break;
    default:
        return ERROR_NOT_SUPPORTED;
    }
    if (header->size < sizeof(*header) + needed * sizeof(UINT32))
        return ERROR_INVALID_PARAMETER;
    memcpy(values->value, value, needed * sizeof(UINT32));
    return ERROR_SUCCESS;
}

/* the host owns the display configuration, so nothing in it can be changed. */
WINUSERAPI LONG WINAPI DisplayConfigSetDeviceInfo(void *info)
{
    return info ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

#define WDA_NONE 0x0
#define WDA_MONITOR 0x1
#define WDA_EXCLUDEFROMCAPTURE 0x11

/*
 * every screen capture honors the affinity: like windows 11, WDA_MONITOR shows as black and WDA_EXCLUDEFROMCAPTURE
 * is left out. only windows of the calling process may be changed.
 */
WINUSERAPI BOOL WINAPI SetWindowDisplayAffinity(HWND window, DWORD affinity)
{
    DWORD process = 0;
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (affinity != WDA_NONE && affinity != WDA_MONITOR && affinity != WDA_EXCLUDEFROMCAPTURE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    NtUserCallHwndParam(window, affinity, MIME_HWNDPARAM_SETAFFINITY);
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetWindowDisplayAffinity(HWND window, DWORD *affinity)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!affinity) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *affinity = (DWORD)NtUserCallHwndParam(window, 0, MIME_HWNDPARAM_GETAFFINITY);
    return TRUE;
}

/* there is no snap layout, so no window is ever arranged. */
WINUSERAPI BOOL WINAPI IsWindowArranged(HWND window)
{
    return FALSE;
}

#define PW_CLIENTONLY 0x1
#define PW_RENDERFULLCONTENT 0x2

/*
 * win32k captures the window's own pixels, whatever covers it: its redirection surface and, for
 * PW_RENDERFULLCONTENT, the image its process presented through the host, which covers windows filled by a Direct3D
 * or DirectComposition swap chain. the window is painted first, as UpdateWindow does. a missing device context or an
 * empty capture is not a failure. the desktop cannot be printed and fails without an error.
 */
WINUSERAPI BOOL WINAPI PrintWindow(HWND window, HDC dc, UINT flags)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (window == GetDesktopWindow())
        return FALSE;
    if (flags & ~(PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    MIME_CAPTURE_WINDOW info = {.Flags = flags};
    if (NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)window, (PUCHAR)&info, nullptr, 0) < 0)
        return FALSE;
    if (!dc || !info.Width || !info.Height)
        return TRUE;
    SIZE_T size = (SIZE_T)info.Width * info.Height * 4;
    void *bits = HeapAlloc(GetProcessHeap(), 0, size);
    if (!bits)
        return FALSE;
    BOOL ok = NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)window, (PUCHAR)&info, bits, (ULONG)size) >= 0;
    if (ok) {
        BITMAPINFO bmi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)info.Width, -(LONG)info.Height, 1, 32, BI_RGB}};
        SetDIBitsToDevice(dc, 0, 0, info.Width, info.Height, 0, 0, 0, info.Height, bits, &bmi, DIB_RGB_COLORS);
    }
    HeapFree(GetProcessHeap(), 0, bits);
    return ok;
}

/*
 * DefWindowProc's WM_PRINT: the device context holds the window from its top left corner. the frame comes from what the
 * window shows, the client area from WM_ERASEBKGND and WM_PRINTCLIENT at its origin, then the children from the bottom
 * of the z order up.
 */
void UserDefPrint(HWND hwnd, HDC dc, DWORD flags)
{
    if (!dc || ((flags & PRF_CHECKVISIBLE) && !IsWindowVisible(hwnd)))
        return;
    RECT window, client;
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    int cx = origin.x - window.left, cy = origin.y - window.top;
    if ((flags & PRF_NONCLIENT) && (cx || cy || client.right != window.right - window.left ||
                                     client.bottom != window.bottom - window.top)) {
        MIME_CAPTURE_WINDOW info = {0};
        if (NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)hwnd, (PUCHAR)&info, nullptr, 0) >= 0 &&
            info.Width && info.Height) {
            SIZE_T size = (SIZE_T)info.Width * info.Height * 4;
            void *bits = HeapAlloc(GetProcessHeap(), 0, size);
            if (bits && NtUserMimeCapture(MIME_CAPOP_WINDOW, (ULONG)(ULONG_PTR)hwnd, (PUCHAR)&info, bits, (ULONG)size) >= 0) {
                int saved = SaveDC(dc);
                ExcludeClipRect(dc, cx, cy, cx + client.right, cy + client.bottom);
                BITMAPINFO bmi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)info.Width, -(LONG)info.Height, 1, 32,
                                                BI_RGB}};
                SetDIBitsToDevice(dc, 0, 0, info.Width, info.Height, 0, 0, 0, info.Height, bits, &bmi, DIB_RGB_COLORS);
                RestoreDC(dc, saved);
            }
            HeapFree(GetProcessHeap(), 0, bits);
        }
    }
    if (flags & (PRF_ERASEBKGND | PRF_CLIENT)) {
        int saved = SaveDC(dc);
        OffsetViewportOrgEx(dc, cx, cy, nullptr);
        IntersectClipRect(dc, 0, 0, client.right, client.bottom);
        if (flags & PRF_ERASEBKGND)
            SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)dc, 0);
        if (flags & PRF_CLIENT)
            SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)dc, flags);
        RestoreDC(dc, saved);
    }
    if (!(flags & PRF_CHILDREN))
        return;
    for (HWND child = GetWindow(GetWindow(hwnd, GW_CHILD), GW_HWNDLAST); child; child = GetWindow(child, GW_HWNDPREV)) {
        if (!IsWindowVisible(child))
            continue;
        RECT r;
        GetWindowRect(child, &r);
        int saved = SaveDC(dc);
        OffsetViewportOrgEx(dc, r.left - window.left, r.top - window.top, nullptr);
        IntersectClipRect(dc, 0, 0, r.right - r.left, r.bottom - r.top);
        SendMessageW(child, WM_PRINT, (WPARAM)dc, flags & ~PRF_CHECKVISIBLE);
        RestoreDC(dc, saved);
    }
}

/* ------------------------------------------------------------------ pointers and power */

#define FEEDBACK_MAX 12
#define GWFS_INCLUDE_ANCESTORS 1

/* the setting of a window, kept as a property holding the value plus one so that zero means none. */
static void feedback_name(WCHAR name[24], UINT feedback)
{
    wsprintfW(name, L"FeedbackHost%u", feedback);
}

/* the visual feedback of touch and pen input. a window has none set until a program sets one. */
WINUSERAPI BOOL WINAPI SetWindowFeedbackSetting(HWND window, UINT feedback, DWORD flags, UINT32 size,
                                                const void *configuration)
{
    WCHAR name[24];
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!feedback || feedback > FEEDBACK_MAX || flags || (configuration ? size != sizeof(BOOL) : size != 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    feedback_name(name, feedback);
    if (!configuration) {
        RemovePropW(window, name);
        return TRUE;
    }
    return SetPropW(window, name, (HANDLE)(ULONG_PTR)(!!*(const BOOL *)configuration + 1));
}

WINUSERAPI BOOL WINAPI GetWindowFeedbackSetting(HWND window, UINT feedback, DWORD flags, UINT32 *size,
                                                void *configuration)
{
    WCHAR name[24];
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!feedback || feedback > FEEDBACK_MAX || (flags & ~GWFS_INCLUDE_ANCESTORS) || !size ||
        (configuration && *size < sizeof(BOOL))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    feedback_name(name, feedback);
    ULONG_PTR value = 0;
    for (HWND w = window; w && !value; w = flags & GWFS_INCLUDE_ANCESTORS ? GetAncestor(w, GA_PARENT) : nullptr)
        value = (ULONG_PTR)GetPropW(w, name);
    if (!value)
        return FALSE;
    *size = sizeof(BOOL);
    if (configuration)
        *(BOOL *)configuration = (BOOL)(value - 1);
    return TRUE;
}


#define PT_MOUSE 4
#define MOUSE_POINTER_ID 1

/* the machine has a mouse and no touch screen or pen. */
WINUSERAPI BOOL WINAPI GetPointerType(UINT32 id, int *type)
{
    if (!type || id != MOUSE_POINTER_ID) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *type = PT_MOUSE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetPointerDevices(UINT32 *count, void *devices)
{
    if (!count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 0;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetPointerDevice(HANDLE device, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerPenInfo(UINT32 id, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI GetPointerTouchInfo(UINT32 id, void *info)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

WINUSERAPI BOOL WINAPI RegisterPointerDeviceNotifications(HWND window, BOOL range)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return TRUE;
}

typedef struct {
    UINT32 pointerId;
    POINT point;
    RECT boundingBox;
    RECT nonOccludedBoundingBox;
    UINT32 orientation;
} TOUCH_HIT_TESTING_INPUT;

typedef struct {
    UINT16 score;
    POINT adjustedPoint;
} TOUCH_HIT_TESTING_PROXIMITY_EVALUATION;

#define TOUCH_HIT_TESTING_NONE 0x2
#define TOUCH_HIT_TESTING_PROXIMITY_FARTHEST 0xFFF

/* there is no touch screen, so no window is ever asked with WM_TOUCHHITTESTING. */
WINUSERAPI BOOL WINAPI RegisterTouchHitTestingWindow(HWND window, ULONG value)
{
    if (!IsWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (value > TOUCH_HIT_TESTING_NONE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* windows ranks a rectangle the contact misses by its shape, this takes the distance to its nearest point. */
WINUSERAPI BOOL WINAPI EvaluateProximityToRect(const RECT *box, const TOUCH_HIT_TESTING_INPUT *input,
                                               TOUCH_HIT_TESTING_PROXIMITY_EVALUATION *evaluation)
{
    if (!box || !input || !evaluation) {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }
    if (box->top == box->bottom || box->left == box->right) {
        evaluation->score = TOUCH_HIT_TESTING_PROXIMITY_FARTHEST;
        return TRUE;
    }
    POINT p = input->point;
    if (PtInRect(box, p)) {
        evaluation->adjustedPoint = p;
        evaluation->score = 0;
        return TRUE;
    }
    LONG x = p.x < box->left ? box->left : p.x >= box->right ? box->right - 1 : p.x;
    LONG y = p.y < box->top ? box->top : p.y >= box->bottom ? box->bottom - 1 : p.y;
    LONGLONG dx = x - p.x, dy = y - p.y, squared = dx * dx + dy * dy;
    ULONG distance = 0;
    while ((LONGLONG)(distance + 1) * (distance + 1) <= squared && distance < TOUCH_HIT_TESTING_PROXIMITY_FARTHEST)
        distance++;
    evaluation->score = (UINT16)distance;
    evaluation->adjustedPoint.x = x;
    evaluation->adjustedPoint.y = y;
    return TRUE;
}

/* the score in bits 20 to 31 and the adjustment in two signed 10 bit fields, or farthest when it does not fit. */
WINUSERAPI LRESULT WINAPI PackTouchHitTestingProximityEvaluation(const TOUCH_HIT_TESTING_INPUT *input,
                                                                 const TOUCH_HIT_TESTING_PROXIMITY_EVALUATION *evaluation)
{
    LONG dx = evaluation->adjustedPoint.x - input->point.x, dy = evaluation->adjustedPoint.y - input->point.y;
    if (evaluation->score > TOUCH_HIT_TESTING_PROXIMITY_FARTHEST || (dx < 0 ? -dx : dx) >= 511 ||
        (dy < 0 ? -dy : dy) >= 511)
        return 0xFFF00000;
    return (LRESULT)(((((ULONG)dx & 0x3FF) + ((ULONG)evaluation->score << 10)) << 10) + ((ULONG)dy & 0x3FF));
}

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 0x2
#endif

/* the machine never sleeps, so the registration stands but nothing is ever sent. */
WINUSERAPI HANDLE WINAPI RegisterSuspendResumeNotification(HANDLE recipient, DWORD flags)
{
    if (!recipient || (flags != DEVICE_NOTIFY_WINDOW_HANDLE && flags != DEVICE_NOTIFY_CALLBACK)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HANDLE *registration = HeapAlloc(GetProcessHeap(), 0, sizeof(HANDLE));
    if (registration)
        *registration = recipient;
    return registration;
}

WINUSERAPI BOOL WINAPI UnregisterSuspendResumeNotification(HANDLE registration)
{
    if (!registration) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, registration);
    return TRUE;
}
