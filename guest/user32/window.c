/* window, class, coordinate and dpi services. */
#include "user32_private.h"
#include <mime/stub.h>

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

/* the menu a class names is loaded for each top level window created without a menu, so the name is kept here. */
typedef struct CLASS_MENU {
    struct CLASS_MENU *Next;
    ATOM Atom;
    HINSTANCE Instance;
    LPWSTR Name; /* a string, or a resource number. */
} CLASS_MENU;

static CLASS_MENU *g_class_menus;
static volatile LONG g_class_menu_lock;

static void class_menu_lock(void)
{
    while (InterlockedCompareExchange(&g_class_menu_lock, 1, 0))
        YieldProcessor();
}

static void class_menu_unlock(void)
{
    InterlockedExchange(&g_class_menu_lock, 0);
}

static void class_menu_remove(ATOM atom, HINSTANCE instance)
{
    class_menu_lock();
    for (CLASS_MENU **p = &g_class_menus; *p; p = &(*p)->Next)
        if ((*p)->Atom == atom && (*p)->Instance == instance) {
            CLASS_MENU *dead = *p;
            *p = dead->Next;
            if (!IS_INTRESOURCE(dead->Name))
                UserFree(dead->Name);
            UserFree(dead);
            break;
        }
    class_menu_unlock();
}

static void class_menu_add(ATOM atom, HINSTANCE instance, LPCWSTR name)
{
    if (!atom || !name)
        return;
    CLASS_MENU *entry = UserAlloc(sizeof(*entry));
    if (!entry)
        return;
    entry->Atom = atom;
    entry->Instance = instance;
    if (IS_INTRESOURCE(name)) {
        entry->Name = (LPWSTR)name;
    } else {
        SIZE_T n = wcslen(name) + 1;
        entry->Name = UserAlloc(n * sizeof(WCHAR));
        if (!entry->Name) {
            UserFree(entry);
            return;
        }
        memcpy(entry->Name, name, n * sizeof(WCHAR));
    }
    class_menu_remove(atom, instance);
    class_menu_lock();
    entry->Next = g_class_menus;
    g_class_menus = entry;
    class_menu_unlock();
}

/* the menu bar a new top level window gets from its class, or NULL. */
static HMENU class_menu_load(HINSTANCE instance, LPCWSTR class_name)
{
    if (!g_class_menus)
        return nullptr;
    WNDCLASSEXW wc;
    UNICODE_STRING name;
    init_ustring(&name, class_name);
    ATOM atom = (ATOM)NtUserGetClassInfoEx(instance, &name, &wc, nullptr, FALSE);
    if (!atom)
        return nullptr;
    LPWSTR menu_name = nullptr;
    class_menu_lock();
    for (CLASS_MENU *e = g_class_menus; e; e = e->Next)
        if (e->Atom == atom && e->Instance == wc.hInstance) {
            menu_name = e->Name;
            break;
        }
    class_menu_unlock();
    if (!menu_name)
        return nullptr;
    return LoadMenuW(wc.hInstance, menu_name);
}

static ATOM register_class(const WNDCLASSEXW *wc, LPCWSTR menu, BOOL ansi)
{
    if (!wc || wc->cbSize != sizeof(WNDCLASSEXW)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UNICODE_STRING name, menu_name;
    init_ustring(&name, wc->lpszClassName);
    init_ustring(&menu_name, menu);
    ATOM a = NtUserRegisterClassExWOW(wc, &name, menu && !IS_INTRESOURCE(menu) ? &menu_name : nullptr, 0,
                                      ansi ? MIME_CLASS_ANSI : 0);
    if (!a)
        SetLastError(ERROR_CLASS_ALREADY_EXISTS);
    else
        class_menu_add(a, wc->hInstance, menu);
    return a;
}

WINUSERAPI ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *wc)
{
    return register_class(wc, wc ? wc->lpszMenuName : nullptr, FALSE);
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
                      w->hIcon,     w->hCursor,       w->hbrBackground, w->lpszMenuName, w->lpszClassName, nullptr};
    return RegisterClassExW(&wc);
}

WINUSERAPI ATOM WINAPI RegisterClassA(const WNDCLASSA *a)
{
    WNDCLASSEXA wc = {sizeof(wc),   a->style,         a->lpfnWndProc,  a->cbClsExtra,    a->cbWndExtra, a->hInstance,
                      a->hIcon,     a->hCursor,       a->hbrBackground, a->lpszMenuName, a->lpszClassName, nullptr};
    return RegisterClassExA(&wc);
}

WINUSERAPI BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE instance)
{
    UNICODE_STRING s;
    init_ustring(&s, name);
    WNDCLASSEXW wc;
    ATOM atom = g_class_menus ? (ATOM)NtUserGetClassInfoEx(instance, &s, &wc, nullptr, FALSE) : 0;
    BOOL ok = NtUserUnregisterClass(&s, instance);
    if (ok && atom)
        class_menu_remove(atom, wc.hInstance);
    return ok;
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
    LPWSTR menu = nullptr;
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
    wc->style = ex.style;
    wc->lpfnWndProc = ex.lpfnWndProc;
    wc->cbClsExtra = ex.cbClsExtra;
    wc->cbWndExtra = ex.cbWndExtra;
    wc->hInstance = ex.hInstance;
    wc->hIcon = ex.hIcon;
    wc->hCursor = ex.hCursor;
    wc->hbrBackground = ex.hbrBackground;
    wc->lpszMenuName = ex.lpszMenuName;
    wc->lpszClassName = ex.lpszClassName;
    return TRUE;
}

WINUSERAPI int WINAPI GetClassNameW(HWND hwnd, LPWSTR buffer, int count)
{
    if (count <= 0)
        return 0;
    UNICODE_STRING s = {0, (USHORT)(count > 32767 ? 65534 : count * 2), buffer};
    return NtUserGetClassName(hwnd, FALSE, &s);
}

/* mime keeps no superclass link, so this is the real class name. */
WINUSERAPI UINT WINAPI RealGetWindowClassW(HWND hwnd, LPWSTR buffer, UINT count)
{
    if (!count || !buffer)
        return 0;
    UNICODE_STRING s = {0, (USHORT)(count > 32767 ? 65534 : count * 2), buffer};
    return (UINT)NtUserGetClassName(hwnd, TRUE, &s);
}

WINUSERAPI UINT WINAPI RealGetWindowClassA(HWND hwnd, LPSTR buffer, UINT count)
{
    WCHAR w[256];
    UINT n = RealGetWindowClassW(hwnd, w, 256);
    if (!n || !count)
        return 0;
    int a = WideCharToMultiByte(CP_ACP, 0, w, (int)n, buffer, (int)count - 1, nullptr, nullptr);
    buffer[a] = 0;
    return (UINT)a;
}

WINUSERAPI int WINAPI GetClassNameA(HWND hwnd, LPSTR buffer, int count)
{
    WCHAR w[256];
    int n = GetClassNameW(hwnd, w, 256);
    if (!n || count <= 0)
        return 0;
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, buffer, count - 1, nullptr, nullptr);
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

WINUSERAPI DWORD WINAPI GetClassLongA(HWND hwnd, int index)
{
    return (DWORD)GetClassLongPtrA(hwnd, index);
}

WINUSERAPI WORD WINAPI GetClassWord(HWND hwnd, int index)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    return (WORD)NtUserCallHwndParam(hwnd, (ULONG_PTR)(LONG_PTR)index, MIME_HWNDPARAM_GETCLASSWORD);
}

/* in a 32-bit program this is also the pointer sized form. */
WINUSERAPI DWORD WINAPI SetClassLongW(HWND hwnd, int index, LONG value)
{
#ifdef __i386__
    return (DWORD)SetClassLongPtrW(hwnd, index, (LONG_PTR)(ULONG)value);
#else
    return NtUserSetClassLong(hwnd, index, (ULONG)value, FALSE);
#endif
}

/* ------------------------------------------------------------------ creation and destruction */

HWND UserCreateWindowRaw(DWORD ex_style, LPCWSTR class_name, LPCWSTR name, DWORD style, int x, int y, int width, int height,
                         HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    UNICODE_STRING cls, title;
    init_ustring(&cls, class_name);
    init_ustring(&title, name ? name : L"");
    /* a top level window has a menu bar: the one it was given or the one its class names. */
    HMENU class_menu = nullptr;
    if (!(style & WS_CHILD)) {
        if (menu && !IsMenu(menu)) {
            SetLastError(ERROR_INVALID_MENU_HANDLE);
            return nullptr;
        }
        if (!menu && parent != HWND_MESSAGE)
            menu = class_menu = class_menu_load(instance, class_name);
    }
    HWND h = NtUserCreateWindowEx(ex_style, &cls, name ? &title : nullptr, style, x, y, width, height, parent, menu,
                                  instance, param, 0);
    if (!h) {
        if (class_menu)
            DestroyMenu(class_menu);
        SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
    } else if (!(style & WS_CHILD) && parent != HWND_MESSAGE) {
        /* windows tells every top level window of a change of the device tree, registered or not. */
        UserWatchDevices();
    }
    return h;
}

WINUSERAPI HWND WINAPI CreateWindowExW(DWORD ex_style, LPCWSTR class_name, LPCWSTR name, DWORD style, int x, int y,
                                       int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                                       LPVOID param)
{
    /* a window that asks to be a document of an MDI client is made by the client, which activates it. */
    HWND document;
    if ((ex_style & WS_EX_MDICHILD) &&
        UserMdiCreate(&document, ex_style, class_name, name, style, x, y, width, height, parent, menu, instance, param))
        return document;
    return UserCreateWindowRaw(ex_style, class_name, name, style, x, y, width, height, parent, menu, instance, param);
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
    UserCaretDestroyed(hwnd);
    return NtUserDestroyWindow(hwnd);
}

WINUSERAPI BOOL WINAPI ShowWindow(HWND hwnd, int cmd)
{
    return NtUserShowWindow(hwnd, cmd);
}

WINUSERAPI BOOL WINAPI ShowWindowAsync(HWND hwnd, int cmd)
{
    return NtUserShowWindowAsync(hwnd, cmd);
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
    return NtUserSetWindowPos(hwnd, nullptr, x, y, width, height,
                              SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

/* ------------------------------------------------------------------ deferred positions */

/* collected, then applied in order by EndDeferWindowPos. */
typedef struct DEFER_POS {
    DWORD Magic;
    UINT Count;
    UINT Capacity;
    WINDOWPOS *Items;
} DEFER_POS;

#define DEFER_MAGIC 0x50574444u /* DDWP */

static DEFER_POS *defer_pos(HDWP dwp)
{
    DEFER_POS *d = (DEFER_POS *)dwp;
    if (!d || d->Magic != DEFER_MAGIC) {
        SetLastError(ERROR_INVALID_DWP_HANDLE);
        return nullptr;
    }
    return d;
}

WINUSERAPI HDWP WINAPI BeginDeferWindowPos(int count)
{
    if (count < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    DEFER_POS *d = UserAlloc(sizeof(*d));
    if (!d)
        return nullptr;
    d->Magic = DEFER_MAGIC;
    d->Capacity = count ? (UINT)count : 8;
    d->Items = UserAlloc(d->Capacity * sizeof(WINDOWPOS));
    if (!d->Items) {
        UserFree(d);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    return (HDWP)d;
}

static void free_defer(DEFER_POS *d)
{
    d->Magic = 0;
    UserFree(d->Items);
    UserFree(d);
}

WINUSERAPI HDWP WINAPI DeferWindowPos(HDWP dwp, HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    DEFER_POS *d = defer_pos(dwp);
    if (!d)
        return nullptr;
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        free_defer(d);
        return nullptr;
    }
    /* a second call for the same window updates the pending position. */
    for (UINT i = 0; i < d->Count; i++) {
        WINDOWPOS *p = &d->Items[i];
        if (p->hwnd != hwnd)
            continue;
        if (!(flags & SWP_NOZORDER)) {
            p->hwndInsertAfter = after;
            p->flags &= ~SWP_NOZORDER;
        }
        if (!(flags & SWP_NOMOVE)) {
            p->x = x;
            p->y = y;
            p->flags &= ~SWP_NOMOVE;
        }
        if (!(flags & SWP_NOSIZE)) {
            p->cx = cx;
            p->cy = cy;
            p->flags &= ~SWP_NOSIZE;
        }
        p->flags = (p->flags & (SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE)) |
                   (flags & ~(SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE));
        return dwp;
    }
    if (d->Count == d->Capacity) {
        WINDOWPOS *items = UserAlloc(d->Capacity * 2 * sizeof(WINDOWPOS));
        if (!items) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            free_defer(d);
            return nullptr;
        }
        memcpy(items, d->Items, d->Count * sizeof(WINDOWPOS));
        UserFree(d->Items);
        d->Items = items;
        d->Capacity *= 2;
    }
    d->Items[d->Count++] = (WINDOWPOS){hwnd, after, x, y, cx, cy, flags};
    return dwp;
}

WINUSERAPI BOOL WINAPI EndDeferWindowPos(HDWP dwp)
{
    DEFER_POS *d = defer_pos(dwp);
    if (!d)
        return FALSE;
    BOOL ok = TRUE;
    for (UINT i = 0; i < d->Count; i++) {
        WINDOWPOS *p = &d->Items[i];
        if (!SetWindowPos(p->hwnd, p->hwndInsertAfter, p->x, p->y, p->cx, p->cy, p->flags))
            ok = FALSE;
    }
    free_defer(d);
    return ok;
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
    return NtUserSetWindowPlacement(hwnd, wp);
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

WINUSERAPI BOOL WINAPI OpenIcon(HWND hwnd)
{
    if (!IsIconic(hwnd))
        return FALSE;
    ShowWindow(hwnd, SW_RESTORE);
    return TRUE;
}

/* nothing here generates shell hook messages, so the window never gets any. */
WINUSERAPI BOOL WINAPI RegisterShellHookWindow(HWND hwnd)
{
    return IsWindow(hwnd);
}

WINUSERAPI BOOL WINAPI DeregisterShellHookWindow(HWND hwnd)
{
    return IsWindow(hwnd);
}

/* the shell extracts icons from file resources; no file has any here. */
WINUSERAPI UINT WINAPI PrivateExtractIconsW(LPCWSTR file, int index, int cx, int cy, HICON *icons, UINT *ids, UINT count,
                                           UINT flags)
{
    if (!file || !*file) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0xFFFFFFFFu;
    }
    SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
    return 0;
}

WINUSERAPI BOOL WINAPI IsIconic(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISICONIC);
}

/* despite the name this minimizes the window, nothing is destroyed. */
WINUSERAPI BOOL WINAPI CloseWindow(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    ShowWindow(hwnd, SW_MINIMIZE);
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsZoomed(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISZOOMED);
}

/* follows how the window or its class was registered. */
WINUSERAPI BOOL WINAPI IsWindowUnicode(HWND hwnd)
{
    return (BOOL)NtUserCallHwnd(hwnd, MIME_HWND_ISUNICODE);
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
    return nullptr;
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

WINUSERAPI BOOL WINAPI GetTitleBarInfo(HWND hwnd, PTITLEBARINFO info)
{
    return (BOOL)NtUserCallHwndParam(hwnd, (ULONG_PTR)info, MIME_HWNDPARAM_GETTITLEBARINFO);
}

WINUSERAPI int WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count)
{
    MIME_MAP_POINTS mp = {to, count, points};
    ULONG_PTR r = NtUserCallHwndParam(from, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS);
    return r == 0x80000000u ? 0 : (int)r;
}

WINUSERAPI BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT point)
{
    MIME_MAP_POINTS mp = {nullptr, 1, point};
    return NtUserCallHwndParam(hwnd, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS) != 0;
}

WINUSERAPI BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT point)
{
    MIME_MAP_POINTS mp = {hwnd, 1, point};
    return NtUserCallHwndParam(nullptr, (ULONG_PTR)&mp, MIME_HWNDPARAM_MAPPOINTS) != 0;
}

WINUSERAPI HWND WINAPI ChildWindowFromPoint(HWND parent, POINT pt)
{
    return (HWND)NtUserCallHwndParam(parent, (ULONG_PTR)(DWORD)MAKELONG(pt.x, pt.y), MIME_HWNDPARAM_CHILDFROMPOINT);
}

WINUSERAPI HWND WINAPI WindowFromPoint(POINT pt)
{
    return (HWND)NtUserCallTwoParam((ULONG_PTR)(LONG_PTR)pt.x, (ULONG_PTR)(LONG_PTR)pt.y,
                                    MIME_TWOPARAM_WINDOWFROMPOINT);
}

/* returns the parent itself when no child matches. */
WINUSERAPI HWND WINAPI ChildWindowFromPointEx(HWND parent, POINT pt, UINT flags)
{
    RECT client;
    if (!GetClientRect(parent, &client))
        return nullptr;
    if (!PtInRect(&client, pt))
        return nullptr;
    POINT screen = pt;
    ClientToScreen(parent, &screen);
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r;
        if (!GetWindowRect(c, &r) || !PtInRect(&r, screen))
            continue;
        LONG style = GetWindowLongW(c, GWL_STYLE), ex = GetWindowLongW(c, GWL_EXSTYLE);
        if ((flags & CWP_SKIPINVISIBLE) && !(style & WS_VISIBLE))
            continue;
        if ((flags & CWP_SKIPDISABLED) && (style & WS_DISABLED))
            continue;
        if ((flags & CWP_SKIPTRANSPARENT) && (ex & WS_EX_TRANSPARENT))
            continue;
        return c;
    }
    return parent;
}

/* skips invisible and transparent children; group boxes only win when nothing else does. */
WINUSERAPI HWND WINAPI RealChildWindowFromPoint(HWND parent, POINT pt)
{
    RECT client;
    if (!GetClientRect(parent, &client) || !PtInRect(&client, pt))
        return nullptr;
    POINT screen = pt;
    ClientToScreen(parent, &screen);
    HWND group = nullptr;
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r;
        LONG style = GetWindowLongW(c, GWL_STYLE);
        if (!(style & WS_VISIBLE) || (GetWindowLongW(c, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
            continue;
        if (!GetWindowRect(c, &r) || !PtInRect(&r, screen))
            continue;
        WCHAR cls[16];
        if ((style & BS_TYPEMASK) == BS_GROUPBOX && GetClassNameW(c, cls, 16) && !lstrcmpiW(cls, L"Button")) {
            if (!group)
                group = c;
            continue;
        }
        return c;
    }
    return group ? group : parent;
}

/* ------------------------------------------------------------------ owned popups */

/* win32k keeps no last active popup, so the active window stands in when hwnd owns it. */
WINUSERAPI HWND WINAPI GetLastActivePopup(HWND hwnd)
{
    if (!IsWindow(hwnd))
        return nullptr;
    HWND active = GetActiveWindow();
    if (!active)
        active = GetForegroundWindow();
    for (HWND w = active; w && w != hwnd; w = GetWindow(w, GW_OWNER))
        if (GetWindow(w, GW_OWNER) == hwnd)
            return IsWindowVisible(active) && IsWindowEnabled(active) ? active : hwnd;
    return hwnd;
}

typedef struct OWNED_POPUPS {
    HWND Owner;
    BOOL Show;
} OWNED_POPUPS;

static constexpr WCHAR g_hidden_popup[] = L"HiddenPopup";

static BOOL CALLBACK show_owned_popup(HWND hwnd, LPARAM param)
{
    const OWNED_POPUPS *op = (const OWNED_POPUPS *)param;
    if (GetWindow(hwnd, GW_OWNER) != op->Owner || !(GetWindowLongW(hwnd, GWL_STYLE) & WS_POPUP))
        return TRUE;
    if (!op->Show && IsWindowVisible(hwnd)) {
        SetPropW(hwnd, g_hidden_popup, (HANDLE)1);
        ShowWindow(hwnd, SW_HIDE);
    } else if (op->Show && RemovePropW(hwnd, g_hidden_popup)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return TRUE;
}

/* shows again only the popups it hid. */
WINUSERAPI BOOL WINAPI ShowOwnedPopups(HWND owner, BOOL show)
{
    if (!IsWindow(owner)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    OWNED_POPUPS op = {owner, show};
    EnumWindows(show_owned_popup, (LPARAM)&op);
    return TRUE;
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

/* a 32-bit pointer is a long, so 32-bit programs write four bytes. */
WINUSERAPI LONG_PTR WINAPI SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value)
{
#ifdef _WIN64
    return NtUserSetWindowLongPtr(hwnd, index, value, FALSE);
#else
    return NtUserSetWindowLong(hwnd, index, (LONG)value, FALSE);
#endif
}

WINUSERAPI LONG_PTR WINAPI SetWindowLongPtrA(HWND hwnd, int index, LONG_PTR value)
{
#ifdef _WIN64
    return NtUserSetWindowLongPtr(hwnd, index, value, TRUE);
#else
    return NtUserSetWindowLong(hwnd, index, (LONG)value, TRUE);
#endif
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

/*
 * a window of another process never gets WM_GETTEXT, since its owner may be too busy to answer. the stored title
 * is read instead, so a control like an edit shows only its title. windows of this process ask their procedure.
 */
static LRESULT window_text_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    DWORD pid;
    if (!GetWindowThreadProcessId(hwnd, &pid)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (pid == GetCurrentProcessId())
        return ansi ? SendMessageA(hwnd, msg, wparam, lparam) : SendMessageW(hwnd, msg, wparam, lparam);
    return ansi ? DefWindowProcA(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINUSERAPI int WINAPI GetWindowTextW(HWND hwnd, LPWSTR buffer, int count)
{
    if (!buffer || count == 0)
        return 0;
    buffer[0] = 0;
    return (int)window_text_message(hwnd, WM_GETTEXT, (WPARAM)(LONG_PTR)count, (LPARAM)buffer, FALSE);
}

WINUSERAPI int WINAPI GetWindowTextA(HWND hwnd, LPSTR buffer, int count)
{
    if (!buffer || count == 0)
        return 0;
    buffer[0] = 0;
    return (int)window_text_message(hwnd, WM_GETTEXT, (WPARAM)(LONG_PTR)count, (LPARAM)buffer, TRUE);
}

WINUSERAPI int WINAPI GetWindowTextLengthW(HWND hwnd)
{
    return (int)window_text_message(hwnd, WM_GETTEXTLENGTH, 0, 0, FALSE);
}

WINUSERAPI int WINAPI GetWindowTextLengthA(HWND hwnd)
{
    return (int)window_text_message(hwnd, WM_GETTEXTLENGTH, 0, 0, TRUE);
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
            return nullptr;
        NTSTATUS status = NtUserBuildHwndList(nullptr, parent, children, FALSE, thread, cap, list, count);
        if (NT_SUCCESS(status))
            return list;
        UserFree(list);
        if (status != STATUS_BUFFER_TOO_SMALL)
            return nullptr;
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
    return enum_list(nullptr, FALSE, 0, proc, param);
}

/*
 * mime keeps one window tree for all desktops of a window station, so every desktop gets the same windows.
 * a handle that is no desktop is refused.
 */
WINUSERAPI BOOL WINAPI EnumDesktopWindows(HDESK desktop, WNDENUMPROC proc, LPARAM param)
{
    WCHAR name[64];
    DWORD needed;
    if (desktop && !GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed))
        return FALSE;
    return enum_list(nullptr, FALSE, 0, proc, param);
}

WINUSERAPI BOOL WINAPI EnumChildWindows(HWND parent, WNDENUMPROC proc, LPARAM param)
{
    return enum_list(parent ? parent : GetDesktopWindow(), TRUE, 0, proc, param);
}

WINUSERAPI BOOL WINAPI EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM param)
{
    return enum_list(nullptr, FALSE, thread, proc, param);
}

WINUSERAPI HWND WINAPI FindWindowExW(HWND parent, HWND after, LPCWSTR class_name, LPCWSTR name)
{
    UNICODE_STRING cls, title;
    init_ustring(&cls, class_name);
    init_ustring(&title, name);
    return NtUserFindWindowEx(parent, after, class_name ? &cls : nullptr, name ? &title : nullptr, 0);
}

WINUSERAPI HWND WINAPI FindWindowExA(HWND parent, HWND after, LPCSTR class_name, LPCSTR name)
{
    LPWSTR c = UserAtoW(class_name), n = UserAtoW(name);
    HWND h = FindWindowExW(parent, after, c, n);
    UserFreeConverted(c, class_name);
    UserFreeConverted(n, name);
    return h;
}

WINUSERAPI HWND WINAPI FindWindowW(LPCWSTR class_name, LPCWSTR name)
{
    return FindWindowExW(nullptr, nullptr, class_name, name);
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
    return NtUserGetKeyboardState(state);
}

WINUSERAPI BOOL WINAPI SetKeyboardState(PBYTE state)
{
    return NtUserSetKeyboardState(state);
}

WINUSERAPI BOOL WINAPI GetCursorPos(LPPOINT point)
{
    return NtUserGetCursorPos(point);
}

/* the cursor in physical pixels, which are the logical ones while a process is not scaled by the system. */
WINUSERAPI BOOL WINAPI GetPhysicalCursorPos(LPPOINT point)
{
    return GetCursorPos(point);
}

WINUSERAPI BOOL WINAPI SetCursorPos(int x, int y)
{
    return (BOOL)NtUserCallTwoParam((ULONG_PTR)x, (ULONG_PTR)y, MIME_TWOPARAM_SETCURSORPOS);
}

WINUSERAPI BOOL WINAPI SetPhysicalCursorPos(int x, int y)
{
    return SetCursorPos(x, y);
}

WINUSERAPI int WINAPI ShowCursor(BOOL show)
{
    return (int)(LONG_PTR)NtUserCallOneParam((ULONG_PTR)show, MIME_ONEPARAM_SHOWCURSOR);
}

WINUSERAPI BOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
    return NtUserTrackMouseEvent(track);
}

/* ------------------------------------------------------------------ dpi, metrics and monitors */

WINUSERAPI UINT WINAPI GetDpiForWindow(HWND hwnd)
{
    UINT dpi = (UINT)NtUserCallHwnd(hwnd, MIME_HWND_GETDPI);
    if (!dpi)
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return dpi;
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
    WideCharToMultiByte(CP_ACP, 0, w->lfFaceName, -1, a->lfFaceName, LF_FACESIZE, nullptr, nullptr);
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

#define FIRST_MONITOR 0x00010001

static HMONITOR monitor_handle(ULONG index)
{
    return (HMONITOR)(ULONG_PTR)(FIRST_MONITOR + index);
}

/* primary first, returns the count. */
static ULONG get_monitors(MIME_MONITOR_INFO *list)
{
    ULONG count = (ULONG)NtUserCallTwoParam((ULONG_PTR)list, MIME_USER_MAX_MONITORS, MIME_TWOPARAM_GETMONITORS);
    return count > MIME_USER_MAX_MONITORS ? 0 : count;
}

/* the monitor holding most of the rectangle, else the nearest, the primary or none as flags ask. */
static HMONITOR monitor_for(const RECT *rect, DWORD flags)
{
    if (flags > MONITOR_DEFAULTTONEAREST) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = rect ? get_monitors(list) : 0;
    LONG best = -1;
    LONG64 best_area = 0;
    for (ULONG i = 0; i < count; i++) {
        RECT overlap;
        LONG64 area = 0;
        if (IntersectRect(&overlap, rect, &list[i].Rect))
            area = (LONG64)(overlap.right - overlap.left) * (overlap.bottom - overlap.top);
        /* a point is an empty rectangle that still counts as inside. */
        else if (rect->left == rect->right && rect->top == rect->bottom && rect->left >= list[i].Rect.left &&
                 rect->left < list[i].Rect.right && rect->top >= list[i].Rect.top && rect->top < list[i].Rect.bottom)
            area = 1;
        if (area > best_area) {
            best_area = area;
            best = (LONG)i;
        }
    }
    if (best >= 0)
        return monitor_handle((ULONG)best);
    if (flags == MONITOR_DEFAULTTONULL)
        return nullptr;
    if (flags == MONITOR_DEFAULTTONEAREST && rect) {
        LONG64 best_distance = -1;
        for (ULONG i = 0; i < count; i++) {
            const RECT *m = &list[i].Rect;
            LONG64 dx = rect->right <= m->left ? m->left - rect->right : rect->left >= m->right ? rect->left - m->right + 1 : 0;
            LONG64 dy = rect->bottom <= m->top ? m->top - rect->bottom : rect->top >= m->bottom ? rect->top - m->bottom + 1 : 0;
            if (best_distance < 0 || dx * dx + dy * dy < best_distance) {
                best_distance = dx * dx + dy * dy;
                best = (LONG)i;
            }
        }
        if (best >= 0)
            return monitor_handle((ULONG)best);
    }
    return monitor_handle(0);
}

WINUSERAPI HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD flags)
{
    if (flags > MONITOR_DEFAULTTONEAREST) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    RECT rect;
    if (!IsWindow(hwnd) || !GetWindowRect(hwnd, &rect))
        return flags == MONITOR_DEFAULTTONULL ? nullptr : monitor_handle(0);
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT wp = {sizeof(wp)};
        if (GetWindowPlacement(hwnd, &wp))
            rect = wp.rcNormalPosition;
    }
    return monitor_for(&rect, flags);
}

WINUSERAPI HMONITOR WINAPI MonitorFromPoint(POINT point, DWORD flags)
{
    RECT rect = {point.x, point.y, point.x, point.y};
    return monitor_for(&rect, flags);
}

WINUSERAPI HMONITOR WINAPI MonitorFromRect(LPCRECT rect, DWORD flags)
{
    return monitor_for(rect, flags);
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
    WideCharToMultiByte(CP_ACP, 0, w.szDevice, -1, (LPSTR)(info + 1), CCHDEVICENAME, nullptr, nullptr);
    return TRUE;
}

WINUSERAPI BOOL WINAPI EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC proc, LPARAM param)
{
    MIME_MONITOR_INFO list[MIME_USER_MAX_MONITORS];
    ULONG count = get_monitors(list);
    if (!proc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* the list is a snapshot taken before the callbacks run. */
    for (ULONG i = 0; i < count; i++) {
        RECT rect = list[i].Rect;
        if (clip && !IntersectRect(&rect, &rect, clip))
            continue;
        if (!proc(monitor_handle(i), dc, &rect, param))
            return FALSE;
    }
    return TRUE;
}
