#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/shellapi.h>
#include <rtl.h>

#define MAX_ICONS 256
#define TRAY_SIGNATURE 0x34753423u
#define TRAY_REQUEST 1
#define WM_QUERY_NOTIFICATION_STATE (WM_USER + 0x3EF)
#define QUNS_ACCEPTS_NOTIFICATIONS 5

/*
 * what Shell_NotifyIconW of shell32 26300 sends the taskbar in a WM_COPYDATA of dwData 1. the icon data is the
 * NOTIFYICONDATAW of a 32-bit program whatever the program is, so its handles are 32 bits wide, and the flags are
 * cut to those the size the program gave allows. the path of the program follows, which is not read here.
 */
typedef struct TRAY_DATA {
    ULONG Signature;
    ULONG Message;
    ULONG Size;
    ULONG Window;
    ULONG Id;
    ULONG Flags;
    ULONG Callback;
    ULONG Icon;
    WCHAR Tip[128];
    ULONG State;
    ULONG StateMask;
    WCHAR Info[256];
    ULONG Version; /* uTimeout of a balloon, uVersion of NIM_SETVERSION. */
    WCHAR InfoTitle[64];
    ULONG InfoFlags;
    GUID Guid;
    ULONG BalloonIcon;
} TRAY_DATA;

typedef struct ICON {
    BOOL Used;
    HWND Window;
    DWORD Thread;
    UINT Id;
    GUID Guid;
    BOOL HasGuid;
    UINT Callback;
    ULONG Icon;
    ULONG State;
    UINT Version;
} ICON;

static ICON g_icons[MAX_ICONS];

/* the window of an icon is gone, or its handle went to a window of another thread. */
static BOOL stale(const ICON *icon)
{
    return !IsWindow(icon->Window) || GetWindowThreadProcessId(icon->Window, nullptr) != icon->Thread;
}

static ICON *find_icon(HWND window, UINT id, const GUID *guid)
{
    for (int i = 0; i < MAX_ICONS; i++) {
        ICON *icon = &g_icons[i];
        if (!icon->Used)
            continue;
        if (guid ? icon->HasGuid && !memcmp(&icon->Guid, guid, sizeof(GUID))
                 : !icon->HasGuid && icon->Window == window && icon->Id == id)
            return icon;
    }
    return nullptr;
}

static void apply(ICON *icon, const TRAY_DATA *data)
{
    if (data->Flags & NIF_MESSAGE)
        icon->Callback = data->Callback;
    if (data->Flags & NIF_ICON)
        icon->Icon = data->Icon;
    if (data->Flags & NIF_STATE)
        icon->State = (icon->State & ~data->StateMask) | (data->State & data->StateMask);
}

static void copy_text(WCHAR *out, SIZE_T room, const WCHAR *text, SIZE_T chars)
{
    SIZE_T n = 0;
    for (; n < chars && n + 1 < room && text[n]; n++)
        out[n] = text[n];
    out[n] = 0;
}

/* win32k keeps the icon for the host under the number it has here. */
static void mirror(ULONG op, const ICON *icon, const TRAY_DATA *data)
{
    static MIME_TRAY_REQUEST r;
    memset(&r, 0, sizeof(r));
    r.Window = (ULONG)(ULONG_PTR)icon->Window;
    r.Key = (ULONG)(icon - g_icons) + 1;
    r.Id = icon->Id;
    r.Callback = icon->Callback;
    r.Icon = icon->Icon;
    r.State = icon->State;
    r.Version = icon->Version;
    if (data) {
        r.Flags = data->Flags;
        if (data->Flags & NIF_TIP)
            copy_text(r.Tip, RTL_NUMBER_OF(r.Tip), data->Tip, RTL_NUMBER_OF(data->Tip));
        if (data->Flags & NIF_INFO) {
            copy_text(r.Info, RTL_NUMBER_OF(r.Info), data->Info, RTL_NUMBER_OF(data->Info));
            copy_text(r.InfoTitle, RTL_NUMBER_OF(r.InfoTitle), data->InfoTitle, RTL_NUMBER_OF(data->InfoTitle));
            r.InfoFlags = data->InfoFlags;
            r.Timeout = data->Version;
            r.BalloonIcon = data->BalloonIcon;
        }
    }
    NtUserMimeTray(op, (PUCHAR)&r, sizeof(r));
}

static LRESULT tray_request(const COPYDATASTRUCT *copy)
{
    const TRAY_DATA *data = copy->lpData;
    if (copy->cbData < sizeof(*data) || data->Signature != TRAY_SIGNATURE)
        return FALSE;
    HWND window = (HWND)(ULONG_PTR)data->Window;
    const GUID *guid = data->Flags & NIF_GUID ? &data->Guid : nullptr;
    ICON *icon = find_icon(window, data->Id, guid);
    switch (data->Message) {
    case NIM_ADD:
        /*
         * the taskbar of Windows keeps the icon of a window that is gone until it looks at it, so a request for it
         * still succeeds. a new icon takes its place.
         */
        if (!IsWindow(window) || (icon && !stale(icon)))
            return FALSE;
        for (int i = 0; i < MAX_ICONS && !icon; i++)
            if (!g_icons[i].Used || stale(&g_icons[i]))
                icon = &g_icons[i];
        if (!icon)
            return FALSE;
        memset(icon, 0, sizeof(*icon));
        icon->Used = TRUE;
        icon->Window = window;
        icon->Thread = GetWindowThreadProcessId(window, nullptr);
        icon->Id = data->Id;
        if (guid) {
            icon->Guid = *guid;
            icon->HasGuid = TRUE;
        }
        apply(icon, data);
        mirror(MIME_TRAYOP_ADD, icon, data);
        return TRUE;
    case NIM_MODIFY:
        if (!icon)
            return FALSE;
        apply(icon, data);
        mirror(MIME_TRAYOP_MODIFY, icon, data);
        return TRUE;
    case NIM_DELETE:
        if (!icon)
            return FALSE;
        mirror(MIME_TRAYOP_DELETE, icon, nullptr);
        memset(icon, 0, sizeof(*icon));
        return TRUE;
    case NIM_SETFOCUS:
        return icon != nullptr;
    case NIM_SETVERSION:
        if (!icon || data->Version > NOTIFYICON_VERSION_4)
            return FALSE;
        icon->Version = data->Version;
        mirror(MIME_TRAYOP_MODIFY, icon, nullptr);
        return TRUE;
    }
    return FALSE;
}

/* the taskbar is the strip the host keeps outside the work area of the first monitor, or its bottom edge. */
static void place(HWND window)
{
    POINT origin = {0, 0};
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info))
        return;
    RECT m = info.rcMonitor, w = info.rcWork, r = m;
    if (w.bottom < m.bottom)
        r.top = w.bottom;
    else if (w.top > m.top)
        r.bottom = w.top;
    else if (w.left > m.left)
        r.right = w.left;
    else if (w.right < m.right)
        r.left = w.right;
    else
        r.top = m.bottom;
    SetWindowPos(window, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK taskbar_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_COPYDATA: {
        const COPYDATASTRUCT *copy = (const COPYDATASTRUCT *)lparam;
        return copy && copy->dwData == TRAY_REQUEST && copy->lpData ? tray_request(copy) : FALSE;
    }
    case WM_QUERY_NOTIFICATION_STATE:
        return QUNS_ACCEPTS_NOTIFICATIONS;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        place(window);
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* the explorer.exe that starts with the session is its shell and owns the taskbar, which the machine does not show. */
void ShellRun(void)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW class = {.cbSize = sizeof(class), .lpfnWndProc = taskbar_proc, .hInstance = instance,
                         .lpszClassName = L"Shell_TrayWnd"};
    /* the shell is among the last programs a shutdown ends, as Windows' is. */
    SetProcessShutdownParameters(2, 0);
    if (!RegisterClassExW(&class))
        ExitProcess(1);
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, class.lpszClassName, nullptr,
                                  WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, nullptr, nullptr, instance,
                                  nullptr);
    if (!window)
        ExitProcess(1);
    place(window);
    /* programs put their icons back when a taskbar appears. */
    SendNotifyMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    ExitProcess(0);
}
