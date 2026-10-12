/*
 * window, class, coordinate and dpi services.
 */
#include "user32_private.h"

static void init_ustring(UNICODE_STRING *s, LPCWSTR text)
{
    if (IS_INTRESOURCE(text)) {
        s->Length = s->MaximumLength = 0;
        s->Buffer = (PWSTR)text;
    } else {
        RtlInitUnicodeString(s, text);
    }
}

/* ------------------------------------------------------------------ classes */

static ATOM register_class(const WNDCLASSEXW *wc, LPCWSTR menu, BOOL ansi)
{
    if (!wc || wc->cbSize != sizeof(WNDCLASSEXW)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UNICODE_STRING name, menu_name;
    init_ustring(&name, wc->lpszClassName);
    init_ustring(&menu_name, menu);
    ATOM a = NtUserRegisterClassExWOW(wc, &name, menu && !IS_INTRESOURCE(menu) ? &menu_name : NULL, 0,
                                      ansi ? MIME_CLASS_ANSI : 0);
    if (!a)
        SetLastError(ERROR_CLASS_ALREADY_EXISTS);
    return a;
}

WINUSERAPI ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *wc)
{
    return register_class(wc, wc ? wc->lpszMenuName : NULL, FALSE);
}

WINUSERAPI ATOM WINAPI RegisterClassExA(const WNDCLASSEXA *wca)
{
    if (!wca || wca->cbSize != sizeof(WNDCLASSEXA)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WNDCLASSEXW wc;
    memcpy(&wc, wca, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpszClassName = UserAtoW(wca->lpszClassName);
    wc.lpszMenuName = UserAtoW(wca->lpszMenuName);
    ATOM a = register_class(&wc, wc.lpszMenuName, TRUE);
    UserFreeConverted(wc.lpszClassName, wca->lpszClassName);
    UserFreeConverted(wc.lpszMenuName, wca->lpszMenuName);
    return a;
}

WINUSERAPI ATOM WINAPI RegisterClassW(const WNDCLASSW *w)
{
    WNDCLASSEXW wc = {sizeof(wc),   w->style,         w->lpfnWndProc,  w->cbClsExtra,    w->cbWndExtra, w->hInstance,
                      w->hIcon,     w->hCursor,       w->hbrBackground, w->lpszMenuName, w->lpszClassName, NULL};
    return RegisterClassExW(&wc);
}

WINUSERAPI ATOM WINAPI RegisterClassA(const WNDCLASSA *a)
{
    WNDCLASSEXA wc = {sizeof(wc),   a->style,         a->lpfnWndProc,  a->cbClsExtra,    a->cbWndExtra, a->hInstance,
                      a->hIcon,     a->hCursor,       a->hbrBackground, a->lpszMenuName, a->lpszClassName, NULL};
    return RegisterClassExA(&wc);
}

WINUSERAPI BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE instance)
{
    UNICODE_STRING s;
    init_ustring(&s, name);
    return NtUserUnregisterClass(&s, instance);
}

WINUSERAPI BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE instance)
{
    LPWSTR w = UserAtoW(name);
    BOOL ok = UnregisterClassW(w, instance);
    UserFreeConverted(w, name);
    return ok;
}

WINUSERAPI BOOL WINAPI GetClassInfoExW(HINSTANCE instance, LPCWSTR name, LPWNDCLASSEXW wc)
{
    UNICODE_STRING s;
    init_ustring(&s, name);
    LPWSTR menu = NULL;
    ATOM a = (ATOM)NtUserGetClassInfoEx(instance, &s, wc, &menu, FALSE);
    if (a)
        wc->lpszClassName = name;
    return a;
}

WINUSERAPI BOOL WINAPI GetClassInfoW(HINSTANCE instance, LPCWSTR name, LPWNDCLASSW wc)
{
    WNDCLASSEXW ex;
    if (!GetClassInfoExW(instance, name, &ex))
        return FALSE;
    memcpy(wc, &ex.style, sizeof(*wc));
    return TRUE;
}

WINUSERAPI int WINAPI GetClassNameW(HWND hwnd, LPWSTR buffer, int count)
{
    if (count <= 0)
        return 0;
    UNICODE_STRING s = {0, (USHORT)(count > 32767 ? 65534 : count * 2), buffer};
    return NtUserGetClassName(hwnd, FALSE, &s);
}

WINUSERAPI int WINAPI GetClassNameA(HWND hwnd, LPSTR buffer, int count)
{
    WCHAR w[256];
    int n = GetClassNameW(hwnd, w, 256);
    if (!n || count <= 0)
        return 0;
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, buffer, count - 1, NULL, NULL);
    buffer[a] = 0;
    return a;
}

WINUSERAPI ULONG_PTR WINAPI GetClassLongPtrW(HWND hwnd, int index)
{
    return NtUserCallHwndParam(hwnd, (ULONG_PTR)(LONG_PTR)index, MIME_HWNDPARAM_GETCLASSLONG);
}

WINUSERAPI DWORD WINAPI GetClassLongW(HWND hwnd, int index)
{
    return (DWORD)GetClassLongPtrW(hwnd, index);
}

WINUSERAPI ULONG_PTR WINAPI GetClassLongPtrA(HWND hwnd, int index)
{
    return GetClassLongPtrW(hwnd, index);
}

/* ------------------------------------------------------------------ creation and destruction */

WINUSERAPI HWND WINAPI CreateWindowExW(DWORD ex_style, LPCWSTR class_name, LPCWSTR name, DWORD style, int x, int y,
                                       int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                                       LPVOID param)
{
    UNICODE_STRING cls, title;
    init_ustring(&cls, class_name);
    init_ustring(&title, name ? name : L"");
    HWND h = NtUserCreateWindowEx(ex_style, &cls, name ? &title : NULL, style, x, y, width, height, parent, menu,
                                  instance, param, 0);
    if (!h)
        SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
    return h;
}

WINUSERAPI HWND WINAPI CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR name, DWORD style, int x, int y,
                                       int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                                       LPVOID param)
{
    LPWSTR cls = UserAtoW(class_name);
    LPWSTR title = UserAtoW(name);
    HWND h = CreateWindowExW(ex_style, cls, title, style, x, y, width, height, parent, menu, instance, param);
    UserFreeConverted(cls, class_name);
    UserFreeConverted(title, name);
    return h;
}

WINUSERAPI BOOL WINAPI DestroyWindow(HWND hwnd)
{
    return NtUserDestroyWindow(hwnd);
}

WINUSERAPI BOOL WINAPI ShowWindow(HWND hwnd, int cmd)
{
    return NtUserShowWindow(hwnd, cmd);
}

WINUSERAPI BOOL WINAPI ShowWindowAsync(HWND hwnd, int cmd)
{
    return NtUserShowWindow(hwnd, cmd);
}

WINUSERAPI BOOL WINAPI UpdateWindow(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_UPDATEWINDOW);
}

WINUSERAPI BOOL WINAPI SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    return NtUserSetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

WINUSERAPI BOOL WINAPI MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint)
{
    return NtUserSetWindowPos(hwnd, NULL, x, y, width, height,
                              SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

WINUSERAPI BOOL WINAPI BringWindowToTop(HWND hwnd)
{
    return NtUserSetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

WINUSERAPI BOOL WINAPI EnableWindow(HWND hwnd, BOOL enable)
{
    return NtUserEnableWindow(hwnd, enable);
}

WINUSERAPI HWND WINAPI SetParent(HWND hwnd, HWND parent)
{
    return NtUserSetParent(hwnd, parent);
}

WINUSERAPI BOOL WINAPI GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp)
{
    return NtUserGetWindowPlacement(hwnd, wp);
}

WINUSERAPI BOOL WINAPI SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp)
{
    const RECT *r = &wp->rcNormalPosition;
    NtUserSetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    return NtUserShowWindow(hwnd, (int)wp->showCmd), TRUE;
}

/* ------------------------------------------------------------------ queries */

WINUSERAPI BOOL WINAPI IsWindow(HWND hwnd)
{
    return hwnd && NtUserCallHwnd(hwnd, MIME_HWND_ISWINDOW);
}

WINUSERAPI BOOL WINAPI IsWindowVisible(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISVISIBLE);
}

WINUSERAPI BOOL WINAPI IsWindowEnabled(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISENABLED);
}

WINUSERAPI BOOL WINAPI IsIconic(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISICONIC);
}

WINUSERAPI BOOL WINAPI IsZoomed(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISZOOMED);
}

WINUSERAPI BOOL WINAPI IsWindowUnicode(HWND hwnd)
{
    return TRUE;
}

WINUSERAPI HWND WINAPI GetParent(HWND hwnd)
{
    return (HWND)NtUserCallHwnd(hwnd, MIME_HWND_GETPARENT);
}

WINUSERAPI HWND WINAPI GetAncestor(HWND hwnd, UINT flags)
{
    return NtUserGetAncestor(hwnd, flags);
}

WINUSERAPI HWND WINAPI GetWindow(HWND hwnd, UINT cmd)
{
    return (HWND)NtUserCallHwndParam(hwnd, cmd, MIME_HWNDPARAM_GETWINDOW);
}

WINUSERAPI HWND WINAPI GetTopWindow(HWND hwnd)
{
    return GetWindow(hwnd ? hwnd : GetDesktopWindow(), GW_CHILD);
}

WINUSERAPI HWND WINAPI GetDesktopWindow(void)
{
    return (HWND)NtUserCallNoParam(MIME_NOPARAM_GETDESKTOPWINDOW);
}

WINUSERAPI HWND WINAPI GetShellWindow(void)
{
    return NULL;
}

WINUSERAPI BOOL WINAPI IsChild(HWND parent, HWND child)
{
    return (BOOL)NtUserCallHwndParam(child, (ULONG_PTR)parent, MIME_HWNDPARAM_ISCHILD);
}

WINUSERAPI HWND WINAPI GetDlgItem(HWND dialog, int id)
{
    return (HWND)NtUserCallHwndParam(dialog, (ULONG_PTR)(UINT)id, MIME_HWNDPARAM_GETDLGITEM);
}

WINUSERAPI int WINAPI GetDlgCtrlID(HWND hwnd)
{
    return (int)NtUserCallHwndParam(hwnd, (ULONG_PTR)(LONG_PTR)GWLP_ID, MIME_HWNDPARAM_GETWINDOWLONG);
}

WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD pid)
{
    return (DWORD)NtUserCallHwndParam(hwnd, (ULONG_PTR)pid, MIME_HWNDPARAM_GETTHREADPROCESSID);
}

WINUSERAPI BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT rect)
{
    return (BOOL)NtUserCallHwndParam(hwnd, (ULONG_PTR)rect, MIME_HWNDPARAM_GETWINDOWRECT);
}

WINUSERAPI BOOL WINAPI GetClientRect(HWND hwnd, LPRECT rect)
{
    return (BOOL)NtUserCallHwndParam(hwnd, (ULONG_PTR)rect, MIME_HWNDPARAM_GETCLIENTRECT);
}

WINUSERAPI BOOL WINAPI GetWindowInfo(HWND hwnd, PWINDOWINFO info)
{
    return (BOOL)NtUserCallHwndParam(hwnd, (ULONG_PTR)info, MIME_HWNDPARAM_GETWINDOWINFO);
}

WINUSERAPI int WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
    MIME_MAP_POINTS mp = {to, count, points};
    ULONG_PTR r = NtUserCallHwndParam(from, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS);
    return r == 0x80000000u ? 0 : (int)r;
}

WINUSERAPI BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT point)
{
    MIME_MAP_POINTS mp = {NULL, 1, point};
    return NtUserCallHwndParam(hwnd, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS) != 0;
}

WINUSERAPI BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT point)
{
    MIME_MAP_POINTS mp = {hwnd, 1, point};
    return NtUserCallHwndParam(NULL, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS) != 0;
}

WINUSERAPI HWND WINAPI ChildWindowFromPoint(HWND parent, POINT pt)
{
    return (HWND)NtUserCallHwndParam(parent, (ULONG_PTR)(DWORD)MAKELONG(pt.x, pt.y), MIME_HWNDPARAM_CHILDFROMPOINT);
}

WINUSERAPI HWND WINAPI WindowFromPoint(POINT pt)
{
    return NULL;
}

/* ------------------------------------------------------------------ window longs */

WINUSERAPI LONG_PTR WINAPI GetWindowLongPtrW(HWND hwnd, int index)
{
    return (LONG_PTR)NtUserCallHwndParam(hwnd, (ULONG_PTR)(LONG_PTR)index, MIME_HWNDPARAM_GETWINDOWLONG);
}

WINUSERAPI LONG_PTR WINAPI GetWindowLongPtrA(HWND hwnd, int index)
{
    return GetWindowLongPtrW(hwnd, index);
}

WINUSERAPI LONG WINAPI GetWindowLongW(HWND hwnd, int index)
{
    return (LONG)GetWindowLongPtrW(hwnd, index);
}

WINUSERAPI LONG WINAPI GetWindowLongA(HWND hwnd, int index)
{
    return (LONG)GetWindowLongPtrW(hwnd, index);
}

WINUSERAPI LONG_PTR WINAPI SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value)
{
    return NtUserSetWindowLongPtr(hwnd, index, value, FALSE);
}

WINUSERAPI LONG_PTR WINAPI SetWindowLongPtrA(HWND hwnd, int index, LONG_PTR value)
{
    return NtUserSetWindowLongPtr(hwnd, index, value, TRUE);
}

WINUSERAPI LONG WINAPI SetWindowLongW(HWND hwnd, int index, LONG value)
{
    return NtUserSetWindowLong(hwnd, index, value, FALSE);
}

WINUSERAPI LONG WINAPI SetWindowLongA(HWND hwnd, int index, LONG value)
{
    return NtUserSetWindowLong(hwnd, index, value, TRUE);
}

WINUSERAPI WORD WINAPI GetWindowWord(HWND hwnd, int index)
{
    return (WORD)GetWindowLongPtrW(hwnd, index);
}

/* ------------------------------------------------------------------ text */

WINUSERAPI BOOL WINAPI SetWindowTextW(HWND hwnd, LPCWSTR text)
{
    return (BOOL)SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)text);
}

WINUSERAPI BOOL WINAPI SetWindowTextA(HWND hwnd, LPCSTR text)
{
    return (BOOL)SendMessageA(hwnd, WM_SETTEXT, 0, (LPARAM)text);
}

WINUSERAPI int WINAPI GetWindowTextW(HWND hwnd, LPWSTR buffer, int count)
{
    if (!buffer || count <= 0)
        return 0;
    buffer[0] = 0;
    return (int)SendMessageW(hwnd, WM_GETTEXT, (WPARAM)count, (LPARAM)buffer);
}

WINUSERAPI int WINAPI GetWindowTextA(HWND hwnd, LPSTR buffer, int count)
{
    if (!buffer || count <= 0)
        return 0;
    buffer[0] = 0;
    return (int)SendMessageA(hwnd, WM_GETTEXT, (WPARAM)count, (LPARAM)buffer);
}

WINUSERAPI int WINAPI GetWindowTextLengthW(HWND hwnd)
{
    return (int)SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
}

WINUSERAPI int WINAPI GetWindowTextLengthA(HWND hwnd)
{
    return (int)SendMessageA(hwnd, WM_GETTEXTLENGTH, 0, 0);
}

WINUSERAPI int WINAPI InternalGetWindowText(HWND hwnd, LPWSTR buffer, int count)
{
    return NtUserInternalGetWindowText(hwnd, buffer, count);
}

/* ------------------------------------------------------------------ enumeration and search */

static HWND *build_list(HWND parent, BOOL children, DWORD thread, ULONG *count)
{
    ULONG cap = 64;
    for (;;) {
        HWND *list = UserAlloc(cap * sizeof(HWND));
        if (!list)
            return NULL;
        NTSTATUS status = NtUserBuildHwndList(NULL, parent, children, thread, cap, list, count);
        if (NT_SUCCESS(status))
            return list;
        UserFree(list);
        if (status != STATUS_BUFFER_TOO_SMALL)
            return NULL;
        cap = *count + 16;
    }
}

static BOOL enum_list(HWND parent, BOOL children, DWORD thread, WNDENUMPROC proc, LPARAM param)
{
    ULONG count = 0;
    HWND *list = build_list(parent, children, thread, &count);
    if (!list)
        return FALSE;
    BOOL ok = TRUE;
    for (ULONG i = 0; i < count && ok; i++)
        if (IsWindow(list[i]))
            ok = proc(list[i], param);
    UserFree(list);
    return TRUE;
}

WINUSERAPI BOOL WINAPI EnumWindows(WNDENUMPROC proc, LPARAM param)
{
    return enum_list(NULL, FALSE, 0, proc, param);
}

WINUSERAPI BOOL WINAPI EnumChildWindows(HWND parent, WNDENUMPROC proc, LPARAM param)
{
    return enum_list(parent ? parent : GetDesktopWindow(), TRUE, 0, proc, param);
}

WINUSERAPI BOOL WINAPI EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM param)
{
    return enum_list(NULL, FALSE, thread, proc, param);
}

WINUSERAPI HWND WINAPI FindWindowExW(HWND parent, HWND after, LPCWSTR class_name, LPCWSTR name)
{
    UNICODE_STRING cls, title;
    init_ustring(&cls, class_name);
    init_ustring(&title, name);
    return NtUserFindWindowEx(parent, after, class_name ? &cls : NULL, name ? &title : NULL, 0);
}

WINUSERAPI HWND WINAPI FindWindowW(LPCWSTR class_name, LPCWSTR name)
{
    return FindWindowExW(NULL, NULL, class_name, name);
}

WINUSERAPI HWND WINAPI FindWindowA(LPCSTR class_name, LPCSTR name)
{
    LPWSTR c = UserAtoW(class_name), n = UserAtoW(name);
    HWND h = FindWindowW(c, n);
    UserFreeConverted(c, class_name);
    UserFreeConverted(n, name);
    return h;
}

/* ------------------------------------------------------------------ focus and activation */

WINUSERAPI HWND WINAPI SetFocus(HWND hwnd)
{
    return NtUserSetFocus(hwnd);
}

WINUSERAPI HWND WINAPI GetFocus(void)
{
    return (HWND)NtUserCallNoParam(MIME_NOPARAM_GETFOCUS);
}

WINUSERAPI HWND WINAPI SetActiveWindow(HWND hwnd)
{
    return NtUserSetActiveWindow(hwnd);
}

WINUSERAPI HWND WINAPI GetActiveWindow(void)
{
    return (HWND)NtUserCallNoParam(MIME_NOPARAM_GETACTIVEWINDOW);
}

WINUSERAPI HWND WINAPI GetForegroundWindow(void)
{
    return NtUserGetForegroundWindow();
}

WINUSERAPI BOOL WINAPI SetForegroundWindow(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_SETFOREGROUND);
}

WINUSERAPI BOOL WINAPI AllowSetForegroundWindow(DWORD pid)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI LockSetForegroundWindow(UINT code)
{
    return TRUE;
}

WINUSERAPI HWND WINAPI SetCapture(HWND hwnd)
{
    return NtUserSetCapture(hwnd);
}

WINUSERAPI HWND WINAPI GetCapture(void)
{
    return (HWND)NtUserCallNoParam(MIME_NOPARAM_GETCAPTURE);
}

WINUSERAPI BOOL WINAPI ReleaseCapture(void)
{
    return (BOOL)NtUserCallNoParam(MIME_NOPARAM_RELEASECAPTURE);
}

WINUSERAPI SHORT WINAPI GetKeyState(int key)
{
    return NtUserGetKeyState(key);
}

WINUSERAPI SHORT WINAPI GetAsyncKeyState(int key)
{
    return NtUserGetAsyncKeyState(key);
}

WINUSERAPI BOOL WINAPI GetKeyboardState(PBYTE state)
{
    for (int i = 0; i < 256; i++) {
        SHORT s = NtUserGetKeyState(i);
        state[i] = (BYTE)(((s & 0x8000) ? 0x80 : 0) | (s & 1));
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetCursorPos(LPPOINT point)
{
    return NtUserGetCursorPos(point);
}

WINUSERAPI BOOL WINAPI SetCursorPos(int x, int y)
{
    return (BOOL)NtUserCallTwoParam((ULONG_PTR)x, (ULONG_PTR)y, MIME_TWOPARAM_SETCURSORPOS);
}

WINUSERAPI int WINAPI ShowCursor(BOOL show)
{
    return (int)(LONG_PTR)NtUserCallOneParam((ULONG_PTR)show, MIME_ONEPARAM_SHOWCURSOR);
}

WINUSERAPI BOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
    return NtUserTrackMouseEvent(track);
}

WINUSERAPI HKL WINAPI GetKeyboardLayout(DWORD thread)
{
    return (HKL)(ULONG_PTR)0x04090409;
}

/* ------------------------------------------------------------------ dpi, metrics and monitors */

WINUSERAPI UINT WINAPI GetDpiForWindow(HWND hwnd)
{
    return (UINT)NtUserCallHwnd(hwnd, MIME_HWND_GETDPI);
}

WINUSERAPI UINT WINAPI GetDpiForSystem(void)
{
    return (UINT)NtUserCallNoParam(MIME_NOPARAM_GETDPIFORSYSTEM);
}

UINT UserWindowDpi(HWND hwnd)
{
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : GetDpiForSystem();
}

WINUSERAPI BOOL WINAPI SetProcessDPIAware(void)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsProcessDPIAware(void)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return TRUE;
}

WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(void)
{
    return (DPI_AWARENESS_CONTEXT)(LONG_PTR)-2; /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
}

WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return (DPI_AWARENESS_CONTEXT)(LONG_PTR)-2;
}

WINUSERAPI int WINAPI GetSystemMetrics(int index)
{
    return (int)(LONG_PTR)NtUserCallTwoParam((ULONG_PTR)(LONG_PTR)index, 0, MIME_TWOPARAM_GETSYSTEMMETRICS);
}

WINUSERAPI int WINAPI GetSystemMetricsForDpi(int index, UINT dpi)
{
    return (int)(LONG_PTR)NtUserCallTwoParam((ULONG_PTR)(LONG_PTR)index, dpi, MIME_TWOPARAM_GETSYSTEMMETRICS);
}

WINUSERAPI BOOL WINAPI SystemParametersInfoW(UINT action, UINT param, PVOID data, UINT ini)
{
    return NtUserSystemParametersInfo(action, param, data, ini);
}

WINUSERAPI BOOL WINAPI SystemParametersInfoForDpi(UINT action, UINT param, PVOID data, UINT ini, UINT dpi)
{
    return NtUserSystemParametersInfoForDpi(action, param, data, ini, dpi);
}

static void logfont_w_to_a(const LOGFONTW *w, LOGFONTA *a)
{
    memcpy(a, w, offsetof(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_ACP, 0, w->lfFaceName, -1, a->lfFaceName, LF_FACESIZE, NULL, NULL);
}

WINUSERAPI BOOL WINAPI SystemParametersInfoA(UINT action, UINT param, PVOID data, UINT ini)
{
    switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
        NONCLIENTMETRICSA *a = data;
        NONCLIENTMETRICSW w;
        w.cbSize = sizeof(w);
        if (!a || !SystemParametersInfoW(action, 0, &w, ini))
            return FALSE;
        UINT size = a->cbSize;
        memcpy(&a->iBorderWidth, &w.iBorderWidth, 5 * sizeof(int));
        logfont_w_to_a(&w.lfCaptionFont, &a->lfCaptionFont);
        a->iSmCaptionWidth = w.iSmCaptionWidth;
        a->iSmCaptionHeight = w.iSmCaptionHeight;
        logfont_w_to_a(&w.lfSmCaptionFont, &a->lfSmCaptionFont);
        a->iMenuWidth = w.iMenuWidth;
        a->iMenuHeight = w.iMenuHeight;
        logfont_w_to_a(&w.lfMenuFont, &a->lfMenuFont);
        logfont_w_to_a(&w.lfStatusFont, &a->lfStatusFont);
        logfont_w_to_a(&w.lfMessageFont, &a->lfMessageFont);
        if (size >= sizeof(NONCLIENTMETRICSA))
            a->iPaddedBorderWidth = w.iPaddedBorderWidth;
        a->cbSize = size;
        return TRUE;
    }
    case SPI_GETICONTITLELOGFONT: {
        LOGFONTW w;
        if (!SystemParametersInfoW(action, sizeof(w), &w, ini))
            return FALSE;
        logfont_w_to_a(&w, data);
        return TRUE;
    }
    default:
        return SystemParametersInfoW(action, param, data, ini);
    }
}

WINUSERAPI DWORD WINAPI GetSysColor(int index)
{
    return (DWORD)NtUserCallOneParam((ULONG_PTR)(LONG_PTR)index, MIME_ONEPARAM_GETSYSCOLOR);
}

WINUSERAPI HBRUSH WINAPI GetSysColorBrush(int index)
{
    return (HBRUSH)NtUserCallOneParam((ULONG_PTR)(LONG_PTR)index, MIME_ONEPARAM_GETSYSCOLORBRUSH);
}

WINUSERAPI BOOL WINAPI SetSysColors(int count, const INT *elements, const COLORREF *colors)
{
    return TRUE;
}

static BOOL adjust(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style, UINT dpi)
{
    MIME_ADJUST_RECT ar = {*rect, style, ex_style, menu, dpi};
    if (!NtUserCallHwndParam(GetDesktopWindow(), (ULONG_PTR)&ar, MIME_HWNDPARAM_ADJUSTRECT))
        return FALSE;
    *rect = ar.Rect;
    return TRUE;
}

WINUSERAPI BOOL WINAPI AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style)
{
    return adjust(rect, style, menu, ex_style, GetDpiForSystem());
}

WINUSERAPI BOOL WINAPI AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu)
{
    return adjust(rect, style, menu, 0, GetDpiForSystem());
}

WINUSERAPI BOOL WINAPI AdjustWindowRectExForDpi(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style, UINT dpi)
{
    return adjust(rect, style, menu, ex_style, dpi);
}

#define PRIMARY_MONITOR ((HMONITOR)(ULONG_PTR)0x00010001)

WINUSERAPI HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD flags)
{
    return PRIMARY_MONITOR;
}

WINUSERAPI HMONITOR WINAPI MonitorFromPoint(POINT point, DWORD flags)
{
    return PRIMARY_MONITOR;
}

WINUSERAPI HMONITOR WINAPI MonitorFromRect(LPCRECT rect, DWORD flags)
{
    return PRIMARY_MONITOR;
}

WINUSERAPI BOOL WINAPI GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info)
{
    return (BOOL)NtUserCallTwoParam((ULONG_PTR)monitor, (ULONG_PTR)info, MIME_TWOPARAM_GETMONITORINFO);
}

WINUSERAPI BOOL WINAPI GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info)
{
    if (info->cbSize == sizeof(MONITORINFO))
        return GetMonitorInfoW(monitor, info);
    MONITORINFOEXW w;
    w.cbSize = sizeof(w);
    if (!GetMonitorInfoW(monitor, (LPMONITORINFO)&w))
        return FALSE;
    memcpy(info, &w, sizeof(MONITORINFO));
    info->cbSize = sizeof(MONITORINFO) + CCHDEVICENAME;
    WideCharToMultiByte(CP_ACP, 0, w.szDevice, -1, (LPSTR)(info + 1), CCHDEVICENAME, NULL, NULL);
    return TRUE;
}

typedef BOOL(CALLBACK *MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);

WINUSERAPI BOOL WINAPI EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC proc, LPARAM param)
{
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(PRIMARY_MONITOR, &mi))
        return FALSE;
    return proc(PRIMARY_MONITOR, dc, &mi.rcMonitor, param);
}
