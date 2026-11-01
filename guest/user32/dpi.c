/* the dpi awareness: the program's manifest and compatibility layers read when user32 starts, and the calls that set or ask it. */
#include "user32_private.h"

/* manifest dpi values, in windows' order of precedence. */
enum {
    DPI_MANIFEST_NONE,
    DPI_MANIFEST_INVALID,
    DPI_MANIFEST_UNAWARE,
    DPI_MANIFEST_SYSTEM,
    DPI_MANIFEST_PER_MONITOR,
    DPI_MANIFEST_PER_MONITOR_V2,
    DPI_MANIFEST_UNIFORM,
};

BOOL UserGdiScaling;

static BOOL is_blank(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* the 2016 dpiAwareness setting is a comma separated list where the first known value wins. */
static ULONG parse_dpi_awareness(void)
{
    static const struct {
        PCWSTR Name;
        ULONG Value;
    } values[] = {{L"unaware", DPI_MANIFEST_UNAWARE},
                  {L"system", DPI_MANIFEST_SYSTEM},
                  {L"permonitor", DPI_MANIFEST_PER_MONITOR},
                  {L"permonitorv2", DPI_MANIFEST_PER_MONITOR_V2},
                  {L"uniform", DPI_MANIFEST_UNIFORM}};
    SIZE_T needed = 0;
    QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"dpiAwareness",
                         nullptr, 0, &needed);
    if (!needed)
        return DPI_MANIFEST_NONE;
    PWSTR text = UserAlloc(needed * sizeof(WCHAR));
    if (!text)
        return DPI_MANIFEST_NONE;
    ULONG result = DPI_MANIFEST_NONE;
    if (QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"dpiAwareness",
                             text, needed, nullptr)) {
        result = DPI_MANIFEST_INVALID;
        for (PCWSTR p = text; *p && result == DPI_MANIFEST_INVALID;) {
            while (is_blank(*p))
                p++;
            PCWSTR start = p;
            while (*p && *p != ',')
                p++;
            PCWSTR end = p;
            while (end > start && is_blank(end[-1]))
                end--;
            for (ULONG i = 0; i < RTL_NUMBER_OF(values) && end > start; i++) {
                /* uniform is recognized but never taken. */
                if (CompareStringOrdinal(values[i].Name, -1, start, (int)(end - start), TRUE) == CSTR_EQUAL &&
                    values[i].Value != DPI_MANIFEST_UNIFORM) {
                    result = values[i].Value;
                    break;
                }
            }
            if (*p == ',')
                p++;
        }
    }
    UserFree(text);
    return result;
}

/* the 2005 dpiAware setting: true, true/pm or per monitor, anything else means unaware. */
static ULONG parse_dpi_aware(void)
{
    WCHAR value[12];
    if (!QueryActCtxSettingsW(0, nullptr, nullptr, L"dpiAware", value, RTL_NUMBER_OF(value), nullptr))
        return DPI_MANIFEST_NONE;
    if (CompareStringOrdinal(L"true", -1, value, -1, TRUE) == CSTR_EQUAL)
        return DPI_MANIFEST_SYSTEM;
    if (CompareStringOrdinal(L"true/pm", -1, value, -1, TRUE) == CSTR_EQUAL ||
        CompareStringOrdinal(L"per monitor", -1, value, -1, TRUE) == CSTR_EQUAL)
        return DPI_MANIFEST_PER_MONITOR;
    return DPI_MANIFEST_UNAWARE;
}

/* the layers of the program in the registry: HIGHDPIAWARE, DPIUNAWARE and GDIDPISCALING in a space separated list. */
static BOOL layer_has(PCWSTR layers, PCWSTR flag)
{
    SIZE_T length = lstrlenW(flag);
    for (PCWSTR p = layers; *p;) {
        while (is_blank(*p))
            p++;
        PCWSTR start = p;
        while (*p && !is_blank(*p))
            p++;
        if ((SIZE_T)(p - start) == length && CompareStringOrdinal(start, (int)length, flag, (int)length, TRUE) == CSTR_EQUAL)
            return TRUE;
    }
    return FALSE;
}

/* the awareness the layers force, or DPI_MANIFEST_NONE. */
static ULONG parse_layers(void)
{
    WCHAR image[MAX_PATH], layers[256];
    if (!GetModuleFileNameW(nullptr, image, RTL_NUMBER_OF(image)))
        return DPI_MANIFEST_NONE;
    ULONG result = DPI_MANIFEST_NONE;
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots) && result == DPI_MANIFEST_NONE; i++) {
        DWORD size = sizeof(layers);
        if (RegGetValueW(roots[i], L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", image,
                         RRF_RT_REG_SZ, nullptr, layers, &size))
            continue;
        if (layer_has(layers, L"HIGHDPIAWARE"))
            result = DPI_MANIFEST_SYSTEM;
        else if (layer_has(layers, L"DPIUNAWARE"))
            result = DPI_MANIFEST_UNAWARE;
        if (layer_has(layers, L"GDIDPISCALING"))
            UserGdiScaling = TRUE;
    }
    return result;
}

/* the awareness a dpi awareness value stands for, -1 when it is none. */
static LONG awareness_of(DPI_AWARENESS_CONTEXT context)
{
    LONG_PTR value = (LONG_PTR)context;
    if (value >= -5 && value <= -1)
        return (LONG)(-value - 1);
    /* the values behind the pseudo handles. */
    switch ((ULONG)value & 0x7FFFFFFF) {
    case 0x6010:
        return 0;
    case 0x40006010:
        return 4;
    case 0x12:
        return 2;
    case 0x22:
        return 3;
    }
    if (((ULONG)value & 0xFFFE00FF) == 0x11 && (((ULONG)value >> 8) & 0x1FF))
        return 1;
    return -1;
}

/* the value behind the pseudo handle of an awareness, as the get functions return it. */
static DPI_AWARENESS_CONTEXT context_of(ULONG awareness)
{
    switch (awareness) {
    case 0:
        return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x6010;
    case 1:
        return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)(((UINT)NtUserCallNoParam(MIME_NOPARAM_GETSYSTEMDPI) << 8) | 0x11);
    case 2:
        return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x12;
    case 3:
        return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x22;
    }
    return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x40006010;
}

void UserInitializeDpiAwareness(void)
{
    WCHAR scaling[6];
    UserGdiScaling = QueryActCtxSettingsW(0, nullptr, L"http://schemas.microsoft.com/SMI/2017/WindowsSettings",
                                          L"gdiScaling", scaling, RTL_NUMBER_OF(scaling), nullptr) &&
        !lstrcmpiW(scaling, L"true");
    ULONG manifest = parse_dpi_awareness();
    if (manifest == DPI_MANIFEST_NONE)
        manifest = parse_dpi_aware();
    /* a compatibility layer overrides what the manifest asks. */
    ULONG layers = parse_layers();
    if (layers != DPI_MANIFEST_NONE)
        manifest = layers;
    /* win32k's values: 0 unaware, 1 system aware, 2 per monitor, 3 per monitor v2. nothing asked means unaware. */
    ULONG awareness;
    switch (manifest) {
    case DPI_MANIFEST_UNAWARE:
        awareness = 0;
        break;
    case DPI_MANIFEST_SYSTEM:
        awareness = 1;
        break;
    case DPI_MANIFEST_PER_MONITOR:
        awareness = 2;
        break;
    case DPI_MANIFEST_PER_MONITOR_V2:
        awareness = 3;
        break;
    default:
        return;
    }
    /* forced: the awareness then counts as chosen. */
    NtUserCallOneParam(awareness | 0x100, MIME_ONEPARAM_SETPROCESSDPIAWARENESS);
}

/* ------------------------------------------------------------------ the calls */

WINUSERAPI BOOL WINAPI IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return awareness_of(context) >= 0;
}

WINUSERAPI BOOL WINAPI AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT a, DPI_AWARENESS_CONTEXT b)
{
    LONG x = awareness_of(a), y = awareness_of(b);
    if (x < 0 || y < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return x == y;
}

WINUSERAPI DPI_AWARENESS WINAPI GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    LONG awareness = awareness_of(context);
    if (awareness < 0)
        return DPI_AWARENESS_INVALID;
    /* per monitor v2 is a per monitor awareness, the gdi scaled one unaware. */
    return awareness == 4 ? DPI_AWARENESS_UNAWARE : awareness == 3 ? DPI_AWARENESS_PER_MONITOR_AWARE
                                                                    : (DPI_AWARENESS)awareness;
}

WINUSERAPI UINT WINAPI GetDpiFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    LONG awareness = awareness_of(context);
    if (awareness < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* the aware ones have the system dpi, what a monitor has is not part of a context. */
    return awareness == 0 || awareness == 4 ? 96 : (UINT)NtUserCallNoParam(MIME_NOPARAM_GETSYSTEMDPI);
}

WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(void)
{
    return context_of((ULONG)NtUserCallOneParam(0, MIME_ONEPARAM_GETTHREADDPICONTEXT));
}

/* returns the context in effect before, a call with a context that is not one fails. */
WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    LONG awareness = awareness_of(context);
    if (awareness < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return context_of((ULONG)NtUserCallOneParam((ULONG)awareness + 1, MIME_ONEPARAM_SETTHREADDPICONTEXT));
}

WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetWindowDpiAwarenessContext(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    return context_of((ULONG)NtUserCallHwnd(hwnd, MIME_HWND_GETDPIAWARENESS));
}

WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetDpiAwarenessContextForProcess(HANDLE process)
{
    if (process && process != GetCurrentProcess() && GetProcessId(process) != GetCurrentProcessId()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return context_of((ULONG)NtUserCallOneParam(0, MIME_ONEPARAM_GETPROCESSDPIAWARENESS));
}

/* a process can choose its awareness once, whether through its manifest or here. */
WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    LONG awareness = awareness_of(context);
    if (awareness < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!NtUserCallOneParam((ULONG)(awareness == 4 ? 0 : awareness), MIME_ONEPARAM_SETPROCESSDPIAWARENESS)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (awareness == 4)
        UserGdiScaling = TRUE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetProcessDPIAware(void)
{
    if (!NtUserCallOneParam(DPI_AWARENESS_SYSTEM_AWARE, MIME_ONEPARAM_SETPROCESSDPIAWARENESS)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsProcessDPIAware(void)
{
    return NtUserCallOneParam(0, MIME_ONEPARAM_GETPROCESSDPIAWARENESS) != 0;
}

/* a per monitor window scales its frame itself, a per monitor v2 one always does. */
WINUSERAPI BOOL WINAPI EnableNonClientDpiScaling(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (!NtUserCallHwndParam(hwnd, 0, MIME_HWNDPARAM_ENABLENCDPI))
        return FALSE;
    return TRUE;
}

/* the dpi the controls of a window work at, which is the dpi of the system for a per monitor window not told of it. */
WINUSERAPI UINT WINAPI GetWindowDPI(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (UINT)NtUserCallHwnd(hwnd, MIME_HWND_GETWINDOWDPI);
}

/* DPI_HOSTING_BEHAVIOR_DEFAULT or DPI_HOSTING_BEHAVIOR_MIXED, -1 when it is none. */
WINUSERAPI DPI_HOSTING_BEHAVIOR WINAPI SetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR behavior)
{
    if ((UINT)behavior > 1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return DPI_HOSTING_BEHAVIOR_INVALID;
    }
    return (DPI_HOSTING_BEHAVIOR)NtUserCallOneParam((ULONG)behavior, MIME_ONEPARAM_SETTHREADDPIHOSTING);
}

WINUSERAPI DPI_HOSTING_BEHAVIOR WINAPI GetThreadDpiHostingBehavior(void)
{
    return (DPI_HOSTING_BEHAVIOR)NtUserCallOneParam(0, MIME_ONEPARAM_GETTHREADDPIHOSTING);
}

/* the behavior its thread had when the window was created. */
WINUSERAPI DPI_HOSTING_BEHAVIOR WINAPI GetWindowDpiHostingBehavior(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return DPI_HOSTING_BEHAVIOR_INVALID;
    }
    return (DPI_HOSTING_BEHAVIOR)NtUserCallHwnd(hwnd, MIME_HWND_GETDPIHOSTING);
}

static BOOL convert_point(HWND hwnd, LPPOINT point, ULONG op, BOOL window_optional)
{
    if (!point || (hwnd && !IsWindow(hwnd)) || (!hwnd && !window_optional)) {
        SetLastError(hwnd || !point ? ERROR_INVALID_WINDOW_HANDLE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MIME_DPI_CONVERT convert = {*point, op};
    if (!NtUserCallHwndParam(hwnd, (ULONG_PTR)&convert, MIME_HWNDPARAM_DPICONVERT))
        return FALSE;
    *point = convert.Point;
    return TRUE;
}

/* the pixels of the window and the physical ones. */
WINUSERAPI BOOL WINAPI LogicalToPhysicalPoint(HWND hwnd, LPPOINT point)
{
    return convert_point(hwnd, point, MIME_DPI_WINDOW_TO_PHYSICAL, FALSE);
}

WINUSERAPI BOOL WINAPI PhysicalToLogicalPoint(HWND hwnd, LPPOINT point)
{
    return convert_point(hwnd, point, MIME_DPI_PHYSICAL_TO_WINDOW, FALSE);
}

/* the pixels of the calling thread, which a point of the window must be in. */
WINUSERAPI BOOL WINAPI LogicalToPhysicalPointForPerMonitorDPI(HWND hwnd, LPPOINT point)
{
    return convert_point(hwnd, point, MIME_DPI_THREAD_TO_PHYSICAL, TRUE);
}

WINUSERAPI BOOL WINAPI PhysicalToLogicalPointForPerMonitorDPI(HWND hwnd, LPPOINT point)
{
    return convert_point(hwnd, point, MIME_DPI_PHYSICAL_TO_THREAD, TRUE);
}
