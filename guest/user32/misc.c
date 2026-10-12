/*
 * small user32 services: rectangle arithmetic, wsprintf, character
 * classification and services that have nothing to do yet.
 */
#include "user32_private.h"

/* ------------------------------------------------------------------ rectangles */

WINUSERAPI BOOL WINAPI SetRect(LPRECT r, int left, int top, int right, int bottom)
{
    if (!r)
        return FALSE;
    *r = (RECT){left, top, right, bottom};
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetRectEmpty(LPRECT r)
{
    if (!r)
        return FALSE;
    *r = (RECT){0, 0, 0, 0};
    return TRUE;
}

WINUSERAPI BOOL WINAPI CopyRect(LPRECT dst, const RECT *src)
{
    if (!dst || !src)
        return FALSE;
    *dst = *src;
    return TRUE;
}

WINUSERAPI BOOL WINAPI InflateRect(LPRECT r, int dx, int dy)
{
    if (!r)
        return FALSE;
    r->left -= dx;
    r->right += dx;
    r->top -= dy;
    r->bottom += dy;
    return TRUE;
}

WINUSERAPI BOOL WINAPI OffsetRect(LPRECT r, int dx, int dy)
{
    if (!r)
        return FALSE;
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsRectEmpty(const RECT *r)
{
    return !r || r->right <= r->left || r->bottom <= r->top;
}

WINUSERAPI BOOL WINAPI IntersectRect(LPRECT out, const RECT *a, const RECT *b)
{
    if (!out || !a || !b)
        return FALSE;
    RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
              a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
    if (IsRectEmpty(&r)) {
        SetRectEmpty(out);
        return FALSE;
    }
    *out = r;
    return TRUE;
}

WINUSERAPI BOOL WINAPI UnionRect(LPRECT out, const RECT *a, const RECT *b)
{
    if (!out || !a || !b)
        return FALSE;
    if (IsRectEmpty(a)) {
        if (IsRectEmpty(b)) {
            SetRectEmpty(out);
            return FALSE;
        }
        *out = *b;
        return TRUE;
    }
    if (IsRectEmpty(b)) {
        *out = *a;
        return TRUE;
    }
    *out = (RECT){a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
                  a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom};
    return TRUE;
}

WINUSERAPI BOOL WINAPI SubtractRect(LPRECT out, const RECT *a, const RECT *b)
{
    if (!out || !a || !b)
        return FALSE;
    RECT r = *a, i;
    if (IntersectRect(&i, a, b)) {
        if (i.left == r.left && i.right == r.right) {
            if (i.top == r.top)
                r.top = i.bottom;
            else if (i.bottom == r.bottom)
                r.bottom = i.top;
        } else if (i.top == r.top && i.bottom == r.bottom) {
            if (i.left == r.left)
                r.left = i.right;
            else if (i.right == r.right)
                r.right = i.left;
        }
    }
    *out = r;
    return !IsRectEmpty(out);
}

WINUSERAPI BOOL WINAPI EqualRect(const RECT *a, const RECT *b)
{
    return a && b && a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

WINUSERAPI BOOL WINAPI PtInRect(const RECT *r, POINT pt)
{
    return r && pt.x >= r->left && pt.x < r->right && pt.y >= r->top && pt.y < r->bottom;
}

/* ------------------------------------------------------------------ wsprintf */

/* wsprintf supports a subset of printf without floating point; the rtl formatter covers it. */
WINUSERAPI int WINAPI wvsprintfW(LPWSTR buffer, LPCWSTR format, va_list args)
{
    int n = _vsnwprintf(buffer, 1024, format, args);
    if (n < 0) {
        buffer[1023] = 0;
        return 1023;
    }
    return n;
}

WINUSERAPI int WINAPIV wsprintfW(LPWSTR buffer, LPCWSTR format, ...)
{
    va_list args;
    va_start(args, format);
    int n = wvsprintfW(buffer, format, args);
    va_end(args);
    return n;
}

WINUSERAPI int WINAPI wvsprintfA(LPSTR buffer, LPCSTR format, va_list args)
{
    int n = _vsnprintf(buffer, 1024, format, args);
    if (n < 0) {
        buffer[1023] = 0;
        return 1023;
    }
    return n;
}

WINUSERAPI int WINAPIV wsprintfA(LPSTR buffer, LPCSTR format, ...)
{
    va_list args;
    va_start(args, format);
    int n = wvsprintfA(buffer, format, args);
    va_end(args);
    return n;
}

/* ------------------------------------------------------------------ characters */

static BOOL is_resource_char(const void *p)
{
    return IS_INTRESOURCE(p);
}

WINUSERAPI LPWSTR WINAPI CharUpperW(LPWSTR s)
{
    if (is_resource_char(s))
        return (LPWSTR)(ULONG_PTR)RtlUpcaseUnicodeChar((WCHAR)(ULONG_PTR)s);
    for (LPWSTR p = s; *p; p++)
        *p = RtlUpcaseUnicodeChar(*p);
    return s;
}

WINUSERAPI LPWSTR WINAPI CharLowerW(LPWSTR s)
{
    if (is_resource_char(s))
        return (LPWSTR)(ULONG_PTR)RtlDowncaseUnicodeChar((WCHAR)(ULONG_PTR)s);
    for (LPWSTR p = s; *p; p++)
        *p = RtlDowncaseUnicodeChar(*p);
    return s;
}

WINUSERAPI DWORD WINAPI CharUpperBuffW(LPWSTR s, DWORD n)
{
    for (DWORD i = 0; i < n; i++)
        s[i] = RtlUpcaseUnicodeChar(s[i]);
    return n;
}

WINUSERAPI DWORD WINAPI CharLowerBuffW(LPWSTR s, DWORD n)
{
    for (DWORD i = 0; i < n; i++)
        s[i] = RtlDowncaseUnicodeChar(s[i]);
    return n;
}

WINUSERAPI LPWSTR WINAPI CharNextW(LPCWSTR s)
{
    return (LPWSTR)(*s ? s + 1 : s);
}

WINUSERAPI LPWSTR WINAPI CharPrevW(LPCWSTR start, LPCWSTR s)
{
    return (LPWSTR)(s > start ? s - 1 : start);
}

WINUSERAPI LPSTR WINAPI CharNextA(LPCSTR s)
{
    return (LPSTR)(*s ? s + 1 : s);
}

WINUSERAPI LPSTR WINAPI CharUpperA(LPSTR s)
{
    if (IS_INTRESOURCE(s)) {
        char c = (char)(ULONG_PTR)s;
        return (LPSTR)(ULONG_PTR)(UCHAR)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    for (LPSTR p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
    return s;
}

WINUSERAPI LPSTR WINAPI CharLowerA(LPSTR s)
{
    if (IS_INTRESOURCE(s)) {
        char c = (char)(ULONG_PTR)s;
        return (LPSTR)(ULONG_PTR)(UCHAR)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    for (LPSTR p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    return s;
}

WINUSERAPI BOOL WINAPI IsCharAlphaW(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

WINUSERAPI BOOL WINAPI IsCharAlphaNumericW(WCHAR c)
{
    return IsCharAlphaW(c) || (c >= L'0' && c <= L'9');
}

WINUSERAPI BOOL WINAPI IsCharUpperW(WCHAR c)
{
    return RtlDowncaseUnicodeChar(c) != c;
}

WINUSERAPI BOOL WINAPI IsCharLowerW(WCHAR c)
{
    return RtlUpcaseUnicodeChar(c) != c;
}

/* ------------------------------------------------------------------ services with nothing to do yet */

/*
 * there are no menus yet. the system menu handle is the window handle, so
 * removing or greying SC_CLOSE can disable the caption close button.
 */
WINUSERAPI HMENU WINAPI GetSystemMenu(HWND hwnd, BOOL revert)
{
    if (revert) {
        NtUserCallHwndParam(hwnd, 1, MIME_HWNDPARAM_ENABLECLOSE);
        return NULL;
    }
    return IsWindow(hwnd) ? (HMENU)hwnd : NULL;
}

static BOOL is_system_menu(HMENU menu)
{
    return menu && IsWindow((HWND)menu);
}

WINUSERAPI BOOL WINAPI EnableMenuItem(HMENU menu, UINT id, UINT flags)
{
    if (!is_system_menu(menu) || id != SC_CLOSE)
        return -1;
    BOOL was = (BOOL)NtUserCallHwndParam((HWND)menu, !(flags & (MF_GRAYED | MF_DISABLED)), MIME_HWNDPARAM_ENABLECLOSE);
    return was ? MF_ENABLED : MF_GRAYED;
}

WINUSERAPI BOOL WINAPI DeleteMenu(HMENU menu, UINT position, UINT flags)
{
    if (is_system_menu(menu) && position == SC_CLOSE && !(flags & MF_BYPOSITION)) {
        NtUserCallHwndParam((HWND)menu, 0, MIME_HWNDPARAM_ENABLECLOSE);
        return TRUE;
    }
    return FALSE;
}

WINUSERAPI BOOL WINAPI RemoveMenu(HMENU menu, UINT position, UINT flags)
{
    return DeleteMenu(menu, position, flags);
}

WINUSERAPI HMENU WINAPI GetMenu(HWND hwnd)
{
    return NULL;
}

WINUSERAPI BOOL WINAPI SetMenu(HWND hwnd, HMENU menu)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI DrawMenuBar(HWND hwnd)
{
    return TRUE;
}

WINUSERAPI HMENU WINAPI CreateMenu(void)
{
    return NULL;
}

WINUSERAPI HMENU WINAPI CreatePopupMenu(void)
{
    return NULL;
}

WINUSERAPI BOOL WINAPI DestroyMenu(HMENU menu)
{
    return TRUE;
}

WINUSERAPI HACCEL WINAPI LoadAcceleratorsW(HINSTANCE instance, LPCWSTR name)
{
    return NULL;
}

WINUSERAPI int WINAPI TranslateAcceleratorW(HWND hwnd, HACCEL accel, LPMSG msg)
{
    return 0;
}

WINUSERAPI BOOL WINAPI OpenClipboard(HWND owner)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI CloseClipboard(void)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI EmptyClipboard(void)
{
    return TRUE;
}

WINUSERAPI HANDLE WINAPI SetClipboardData(UINT format, HANDLE data)
{
    return data;
}

WINUSERAPI HANDLE WINAPI GetClipboardData(UINT format)
{
    return NULL;
}

WINUSERAPI BOOL WINAPI IsClipboardFormatAvailable(UINT format)
{
    return FALSE;
}

WINUSERAPI HHOOK WINAPI SetWindowsHookExW(int id, HOOKPROC proc, HINSTANCE instance, DWORD thread)
{
    return (HHOOK)(ULONG_PTR)0x1234;
}

WINUSERAPI HHOOK WINAPI SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE instance, DWORD thread)
{
    return (HHOOK)(ULONG_PTR)0x1234;
}

WINUSERAPI BOOL WINAPI UnhookWindowsHookEx(HHOOK hook)
{
    return TRUE;
}

WINUSERAPI LRESULT WINAPI CallNextHookEx(HHOOK hook, int code, WPARAM wparam, LPARAM lparam)
{
    return 0;
}

WINUSERAPI BOOL WINAPI FlashWindow(HWND hwnd, BOOL invert)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI FlashWindowEx(PFLASHWINFO info)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetUserObjectInformationW(HANDLE object, int index, PVOID info, DWORD length, LPDWORD needed)
{
    return FALSE;
}

WINUSERAPI HWINSTA WINAPI GetProcessWindowStation(void)
{
    return (HWINSTA)(ULONG_PTR)0x10;
}

WINUSERAPI HDESK WINAPI GetThreadDesktop(DWORD thread)
{
    return (HDESK)(ULONG_PTR)0x14;
}

WINUSERAPI BOOL WINAPI RegisterHotKey(HWND hwnd, int id, UINT modifiers, UINT vk)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI UnregisterHotKey(HWND hwnd, int id)
{
    return TRUE;
}

WINUSERAPI UINT WINAPI GetDoubleClickTime(void)
{
    return 500;
}

WINUSERAPI UINT WINAPI GetCaretBlinkTime(void)
{
    return 530;
}

WINUSERAPI BOOL WINAPI CreateCaret(HWND hwnd, HBITMAP bitmap, int width, int height)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI DestroyCaret(void)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI ShowCaret(HWND hwnd)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI HideCaret(HWND hwnd)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetCaretPos(int x, int y)
{
    return TRUE;
}

WINUSERAPI BOOL WINAPI ExitWindowsEx(UINT flags, DWORD reason)
{
    return FALSE;
}

WINUSERAPI BOOL WINAPI IsHungAppWindow(HWND hwnd)
{
    return FALSE;
}

WINUSERAPI BOOL WINAPI SetWindowContextHelpId(HWND hwnd, DWORD id)
{
    return (BOOL)NtUserCallHwndParam(hwnd, id, MIME_HWNDPARAM_SETCONTEXTHELPID);
}

WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data)
{
    return FALSE;
}

WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name)
{
    return NULL;
}

WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name)
{
    return NULL;
}
