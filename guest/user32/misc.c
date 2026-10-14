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

/* ------------------------------------------------------------------ accelerators */

/*
 * an accelerator table is a process heap block on a list, the handle is
 * its address and is only used after it is found on the list. tables
 * loaded from resources are shared per resource like on windows.
 */
typedef struct ACCEL_TABLE {
    struct ACCEL_TABLE *Next;
    const void *Resource; /* the RT_ACCELERATOR data of a loaded table, NULL otherwise. */
    int Count;
    ACCEL Entries[];
} ACCEL_TABLE;

/* RT_ACCELERATOR resource entry. */
typedef struct ACCEL_RESOURCE {
    WORD Flags;
    WORD Key;
    WORD Cmd;
    WORD Pad;
} ACCEL_RESOURCE;

#define ACCEL_LAST 0x80

static ACCEL_TABLE *g_accels;
static RTL_CRITICAL_SECTION g_accel_lock = RTL_CRITICAL_SECTION_INIT;

static ACCEL_TABLE *new_accel_table(int count, const void *resource)
{
    ACCEL_TABLE *t = UserAlloc(sizeof(*t) + (SIZE_T)count * sizeof(ACCEL));
    if (!t) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    t->Count = count;
    t->Resource = resource;
    return t;
}

static void publish_accel_table(ACCEL_TABLE *t)
{
    RtlEnterCriticalSection(&g_accel_lock);
    t->Next = g_accels;
    g_accels = t;
    RtlLeaveCriticalSection(&g_accel_lock);
}

/* copies up to max entries of a listed table, returns the table size or -1 for a bad handle. */
static int read_accel_table(HACCEL accel, ACCEL *out, int max)
{
    int count = -1;
    RtlEnterCriticalSection(&g_accel_lock);
    for (ACCEL_TABLE *t = g_accels; t; t = t->Next)
        if ((HACCEL)t == accel) {
            count = t->Count;
            if (out && max > 0)
                memcpy(out, t->Entries, (SIZE_T)(max < count ? max : count) * sizeof(ACCEL));
            break;
        }
    RtlLeaveCriticalSection(&g_accel_lock);
    if (count < 0)
        SetLastError(ERROR_INVALID_ACCEL_HANDLE);
    return count;
}

WINUSERAPI HACCEL WINAPI LoadAcceleratorsW(HINSTANCE instance, LPCWSTR name)
{
    HRSRC res = FindResourceW(instance, name, MAKEINTRESOURCEW(9));
    const ACCEL_RESOURCE *data = res ? LockResource(LoadResource(instance, res)) : NULL;
    if (!data)
        return NULL;
    RtlEnterCriticalSection(&g_accel_lock);
    for (ACCEL_TABLE *t = g_accels; t; t = t->Next)
        if (t->Resource == data) {
            RtlLeaveCriticalSection(&g_accel_lock);
            return (HACCEL)t;
        }
    RtlLeaveCriticalSection(&g_accel_lock);
    int max = (int)(SizeofResource(instance, res) / sizeof(ACCEL_RESOURCE)), count = 0;
    while (count < max && !(data[count++].Flags & ACCEL_LAST))
        ;
    if (!count) {
        SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
        return NULL;
    }
    ACCEL_TABLE *t = new_accel_table(count, data);
    if (!t)
        return NULL;
    for (int i = 0; i < count; i++)
        t->Entries[i] = (ACCEL){(BYTE)(data[i].Flags & 0x7F), data[i].Key, data[i].Cmd};
    publish_accel_table(t);
    return (HACCEL)t;
}

WINUSERAPI HACCEL WINAPI CreateAcceleratorTableW(LPACCEL entries, int count)
{
    if (!entries || count <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    ACCEL_TABLE *t = new_accel_table(count, NULL);
    if (!t)
        return NULL;
    for (int i = 0; i < count; i++) {
        t->Entries[i] = entries[i];
        t->Entries[i].fVirt &= 0x7F;
    }
    publish_accel_table(t);
    return (HACCEL)t;
}

WINUSERAPI int WINAPI CopyAcceleratorTableW(HACCEL accel, LPACCEL entries, int count)
{
    int size = read_accel_table(accel, entries, entries ? count : 0);
    if (size < 0)
        return 0;
    if (!entries)
        return size;
    return count < size ? (count > 0 ? count : 0) : size;
}

WINUSERAPI BOOL WINAPI DestroyAcceleratorTable(HACCEL accel)
{
    ACCEL_TABLE *found = NULL;
    RtlEnterCriticalSection(&g_accel_lock);
    for (ACCEL_TABLE **p = &g_accels; *p; p = &(*p)->Next)
        if ((HACCEL)*p == accel) {
            found = *p;
            *p = found->Next;
            break;
        }
    RtlLeaveCriticalSection(&g_accel_lock);
    if (!found) {
        SetLastError(ERROR_INVALID_ACCEL_HANDLE);
        return FALSE;
    }
    UserFree(found);
    return TRUE;
}

/* whether an entry matches a key or character message with the current modifier state. */
static BOOL accel_matches(const ACCEL *a, const MSG *msg)
{
    if (a->key != (WORD)msg->wParam)
        return FALSE;
    BOOL key = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
    if (a->fVirt & FVIRTKEY) {
        if (!key)
            return FALSE;
        BYTE mods = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            mods |= FSHIFT;
        if (GetKeyState(VK_CONTROL) & 0x8000)
            mods |= FCONTROL;
        if (GetKeyState(VK_MENU) & 0x8000)
            mods |= FALT;
        return mods == (a->fVirt & (FSHIFT | FCONTROL | FALT));
    }
    if (key)
        return FALSE;
    if (msg->message == WM_CHAR)
        return !(a->fVirt & FALT);
    return (a->fVirt & FALT) && (msg->lParam & 0x20000000);
}

WINUSERAPI int WINAPI TranslateAcceleratorW(HWND hwnd, HACCEL accel, LPMSG msg)
{
    if (!msg) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN && msg->message != WM_CHAR &&
        msg->message != WM_SYSCHAR)
        return 0;
    int count = read_accel_table(accel, NULL, 0);
    if (count <= 0)
        return 0;
    ACCEL stack[32], *entries = count <= 32 ? stack : UserAlloc((SIZE_T)count * sizeof(ACCEL));
    if (!entries)
        return 0;
    count = read_accel_table(accel, entries, count);
    const ACCEL *hit = NULL;
    for (int i = 0; i < count && !hit; i++)
        if (accel_matches(&entries[i], msg))
            hit = &entries[i];
    WORD cmd = hit ? hit->cmd : 0;
    if (entries != stack)
        UserFree(entries);
    if (!hit)
        return 0;
    /* a disabled item of the window menu swallows the key without a command. */
    HMENU menu = GetMenu(hwnd);
    UINT state = menu ? GetMenuState(menu, cmd, MF_BYCOMMAND) : (UINT)-1;
    if (state != (UINT)-1 && (state & (MF_GRAYED | MF_DISABLED)))
        return 1;
    if (cmd >= 0xF000)
        SendMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
    else
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 1), 0);
    return 1;
}

/* ------------------------------------------------------------------ dde parameters */

/* the packed lparam carries the low and high words. */
WINUSERAPI BOOL WINAPI UnpackDDElParam(UINT msg, LPARAM lparam, UINT_PTR *lo, UINT_PTR *hi)
{
    (void)msg;
    if (lo)
        *lo = LOWORD(lparam);
    if (hi)
        *hi = HIWORD(lparam);
    return TRUE;
}

WINUSERAPI LPARAM WINAPI ReuseDDElParam(LPARAM lparam, UINT msg_in, UINT msg_out, UINT_PTR lo, UINT_PTR hi)
{
    (void)lparam;
    (void)msg_in;
    (void)msg_out;
    return (LPARAM)(DWORD)MAKELONG(lo, hi);
}

/* ------------------------------------------------------------------ services with nothing to do yet */

/* there is no help engine, like windows without WinHlp32. */
WINUSERAPI BOOL WINAPI WinHelpW(HWND hwnd, LPCWSTR file, UINT command, ULONG_PTR data)
{
    (void)hwnd;
    (void)file;
    (void)command;
    (void)data;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* no accessibility clients listen for events. */
WINUSERAPI void WINAPI NotifyWinEvent(DWORD event, HWND hwnd, LONG object, LONG child)
{
    (void)event;
    (void)hwnd;
    (void)object;
    (void)child;
}

WINUSERAPI HHOOK WINAPI SetWindowsHookExW(int id, HOOKPROC proc, HINSTANCE instance, DWORD thread)
{
    return NtUserSetWindowsHookEx(instance, NULL, thread, id, proc, FALSE);
}

WINUSERAPI HHOOK WINAPI SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE instance, DWORD thread)
{
    return NtUserSetWindowsHookEx(instance, NULL, thread, id, proc, TRUE);
}

WINUSERAPI BOOL WINAPI UnhookWindowsHookEx(HHOOK hook)
{
    return NtUserUnhookWindowsHookEx(hook);
}

/* the kernel knows which hook of the chain the thread is running. */
WINUSERAPI LRESULT WINAPI CallNextHookEx(HHOOK hook, int code, WPARAM wparam, LPARAM lparam)
{
    return NtUserCallNextHookEx(code, wparam, lparam, FALSE);
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

/* ------------------------------------------------------------------ window properties */

/* string keys are global atoms; integer atoms pass through. */
static ATOM prop_atom(LPCWSTR name, BOOL add)
{
    if (IS_INTRESOURCE(name))
        return (ATOM)(ULONG_PTR)name;
    return add ? GlobalAddAtomW(name) : GlobalFindAtomW(name);
}

WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data)
{
    ATOM atom = prop_atom(name, TRUE);
    if (!atom)
        return FALSE;
    BOOL ok = NtUserSetProp(hwnd, atom, data);
    if (!ok && !IS_INTRESOURCE(name))
        GlobalDeleteAtom(atom);
    return ok;
}

WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name)
{
    ATOM atom = prop_atom(name, FALSE);
    return atom ? NtUserGetProp(hwnd, atom) : NULL;
}

WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name)
{
    ATOM atom = prop_atom(name, FALSE);
    if (!atom)
        return NULL;
    HANDLE data = NtUserRemoveProp(hwnd, atom);
    if (data && !IS_INTRESOURCE(name))
        GlobalDeleteAtom(atom);
    return data;
}

WINUSERAPI BOOL WINAPI SetPropA(HWND hwnd, LPCSTR name, HANDLE data)
{
    LPWSTR w = UserAtoW(name);
    BOOL ok = SetPropW(hwnd, w, data);
    UserFreeConverted(w, name);
    return ok;
}

WINUSERAPI HANDLE WINAPI GetPropA(HWND hwnd, LPCSTR name)
{
    LPWSTR w = UserAtoW(name);
    HANDLE data = GetPropW(hwnd, w);
    UserFreeConverted(w, name);
    return data;
}

WINUSERAPI HANDLE WINAPI RemovePropA(HWND hwnd, LPCSTR name)
{
    LPWSTR w = UserAtoW(name);
    HANDLE data = RemovePropW(hwnd, w);
    UserFreeConverted(w, name);
    return data;
}

typedef BOOL(CALLBACK *PROPENUMPROCEXW)(HWND hwnd, LPWSTR name, HANDLE data, ULONG_PTR param);
typedef BOOL(CALLBACK *PROPENUMPROCW)(HWND hwnd, LPCWSTR name, HANDLE data);

/* calls the procedure for each property, string keys with their name; returns the last result, -1 for none. */
static int enum_props(HWND hwnd, PROPENUMPROCEXW proc_ex, PROPENUMPROCW proc, ULONG_PTR param)
{
    ULONG count = 0;
    PROPLISTITEM local[32];
    PROPLISTITEM *list = local;
    NTSTATUS status = NtUserBuildPropList(hwnd, RTL_NUMBER_OF(local), list, &count);
    if (status == STATUS_BUFFER_TOO_SMALL) {
        list = UserAlloc(count * sizeof(PROPLISTITEM));
        if (!list)
            return -1;
        status = NtUserBuildPropList(hwnd, count, list, &count);
    }
    int result = -1;
    if (NT_SUCCESS(status)) {
        for (ULONG i = 0; i < count; i++) {
            WCHAR name[256];
            LPWSTR key = (LPWSTR)(ULONG_PTR)list[i].Atom;
            if (list[i].Atom >= 0xC000 && GlobalGetAtomNameW(list[i].Atom, name, RTL_NUMBER_OF(name)))
                key = name;
            result = proc_ex ? proc_ex(hwnd, key, list[i].Data, param) : proc(hwnd, key, list[i].Data);
            if (!result)
                break;
        }
    }
    if (list != local)
        UserFree(list);
    return result;
}

WINUSERAPI int WINAPI EnumPropsExW(HWND hwnd, PROPENUMPROCEXW proc, LPARAM param)
{
    return proc ? enum_props(hwnd, proc, NULL, (ULONG_PTR)param) : -1;
}

WINUSERAPI int WINAPI EnumPropsW(HWND hwnd, PROPENUMPROCW proc)
{
    return proc ? enum_props(hwnd, NULL, proc, 0) : -1;
}

/* ------------------------------------------------------------------ layered windows and regions */

WINUSERAPI BOOL WINAPI SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)
{
    return NtUserSetLayeredWindowAttributes(hwnd, key, alpha, flags);
}

WINUSERAPI BOOL WINAPI GetLayeredWindowAttributes(HWND hwnd, COLORREF *key, BYTE *alpha, DWORD *flags)
{
    return NtUserGetLayeredWindowAttributes(hwnd, key, alpha, flags);
}

WINUSERAPI BOOL WINAPI UpdateLayeredWindowIndirect(HWND hwnd, const UPDATELAYEREDWINDOWINFO *info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtUserUpdateLayeredWindow(hwnd, info->hdcDst, (POINT *)info->pptDst, (SIZE *)info->psize, info->hdcSrc,
                                     (POINT *)info->pptSrc, info->crKey, (BLENDFUNCTION *)info->pblend, info->dwFlags,
                                     (RECT *)info->prcDirty);
}

WINUSERAPI BOOL WINAPI UpdateLayeredWindow(HWND hwnd, HDC dst, POINT *dst_pos, SIZE *size, HDC src, POINT *src_pos,
                                           COLORREF key, BLENDFUNCTION *blend, DWORD flags)
{
    /* ULW_EX_NORESIZE belongs to UpdateLayeredWindowIndirect only. */
    if (flags & ULW_EX_NORESIZE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtUserUpdateLayeredWindow(hwnd, dst, dst_pos, size, src, src_pos, key, blend, flags, NULL);
}

WINUSERAPI int WINAPI SetWindowRgn(HWND hwnd, HRGN region, BOOL redraw)
{
    return NtUserSetWindowRgn(hwnd, region, redraw);
}

WINUSERAPI int WINAPI GetWindowRgn(HWND hwnd, HRGN region)
{
    return NtUserGetWindowRgnEx(hwnd, region, 0);
}

WINUSERAPI int WINAPI GetWindowRgnBox(HWND hwnd, LPRECT rect)
{
    HRGN r = CreateRectRgn(0, 0, 0, 0);
    int type = r ? GetWindowRgn(hwnd, r) : ERROR;
    if (type != ERROR && rect)
        GetRgnBox(r, rect);
    if (r)
        DeleteObject(r);
    return type;
}

/* ------------------------------------------------------------------ touch */

/* there is no touch digitizer: windows register normally and never receive WM_TOUCH. */
WINUSERAPI BOOL WINAPI RegisterTouchWindow(HWND hwnd, ULONG flags)
{
    if (flags & ~(TWF_FINETOUCH | TWF_WANTPALM)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return (BOOL)NtUserCallHwndParam(hwnd, MIME_TOUCH_REGISTERED | flags, MIME_HWNDPARAM_SETTOUCH);
}

WINUSERAPI BOOL WINAPI UnregisterTouchWindow(HWND hwnd)
{
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return (BOOL)NtUserCallHwndParam(hwnd, 0, MIME_HWNDPARAM_SETTOUCH);
}

WINUSERAPI BOOL WINAPI IsTouchWindow(HWND hwnd, PULONG flags)
{
    return NtUserIsTouchWindow(hwnd, flags);
}

/* no WM_TOUCH message is ever sent, so no touch input handle is valid. */
WINUSERAPI BOOL WINAPI GetTouchInputInfo(HTOUCHINPUT input, UINT count, PTOUCHINPUT inputs, int size)
{
    SetLastError(size != (int)sizeof(TOUCHINPUT) || !inputs || !count ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
}

WINUSERAPI BOOL WINAPI CloseTouchInputHandle(HTOUCHINPUT input)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* ------------------------------------------------------------------ device and power notifications */

/*
 * the registrations are kept so they can be unregistered, but no device
 * arrival or power setting event exists to deliver yet.
 */
typedef struct NOTIFY_REGISTRATION {
    struct NOTIFY_REGISTRATION *Next;
    HANDLE Recipient;
    DWORD Flags;
    BOOL Power;
} NOTIFY_REGISTRATION;

static NOTIFY_REGISTRATION *g_notifications;
static RTL_CRITICAL_SECTION g_notify_lock = RTL_CRITICAL_SECTION_INIT;

static PVOID register_notification(HANDLE recipient, DWORD flags, BOOL power)
{
    if (!recipient) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!(flags & (DEVICE_NOTIFY_SERVICE_HANDLE | DEVICE_NOTIFY_CALLBACK)) && !IsWindow((HWND)recipient)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    NOTIFY_REGISTRATION *r = UserAlloc(sizeof(*r));
    if (!r) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    r->Recipient = recipient;
    r->Flags = flags;
    r->Power = power;
    RtlEnterCriticalSection(&g_notify_lock);
    r->Next = g_notifications;
    g_notifications = r;
    RtlLeaveCriticalSection(&g_notify_lock);
    return r;
}

static BOOL unregister_notification(PVOID handle, BOOL power)
{
    BOOL found = FALSE;
    RtlEnterCriticalSection(&g_notify_lock);
    for (NOTIFY_REGISTRATION **p = &g_notifications; *p; p = &(*p)->Next) {
        if (*p == handle && (*p)->Power == power) {
            NOTIFY_REGISTRATION *r = *p;
            *p = r->Next;
            UserFree(r);
            found = TRUE;
            break;
        }
    }
    RtlLeaveCriticalSection(&g_notify_lock);
    if (!found)
        SetLastError(ERROR_INVALID_HANDLE);
    return found;
}

WINUSERAPI HDEVNOTIFY WINAPI RegisterDeviceNotificationW(HANDLE recipient, LPVOID filter, DWORD flags)
{
    if (!filter && !(flags & DEVICE_NOTIFY_ALL_INTERFACE_CLASSES)) {
        SetLastError(ERROR_INVALID_DATA);
        return NULL;
    }
    return register_notification(recipient, flags, FALSE);
}

WINUSERAPI HDEVNOTIFY WINAPI RegisterDeviceNotificationA(HANDLE recipient, LPVOID filter, DWORD flags)
{
    return RegisterDeviceNotificationW(recipient, filter, flags);
}

WINUSERAPI BOOL WINAPI UnregisterDeviceNotification(HDEVNOTIFY handle)
{
    return unregister_notification(handle, FALSE);
}

WINUSERAPI HPOWERNOTIFY WINAPI RegisterPowerSettingNotification(HANDLE recipient, const GUID *setting, DWORD flags)
{
    if (!setting) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return register_notification(recipient, flags, TRUE);
}

WINUSERAPI BOOL WINAPI UnregisterPowerSettingNotification(HPOWERNOTIFY handle)
{
    return unregister_notification(handle, TRUE);
}

/* ------------------------------------------------------------------ display devices */

/* one adapter with one monitor, named like the first display of windows. */
WINUSERAPI BOOL WINAPI EnumDisplayDevicesW(LPCWSTR device, DWORD index, PDISPLAY_DEVICEW info, DWORD flags)
{
    if (!info || info->cb < offsetof(DISPLAY_DEVICEW, DeviceID)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (index != 0)
        return FALSE;
    DWORD cb = info->cb > sizeof(*info) ? sizeof(*info) : info->cb;
    DISPLAY_DEVICEW d;
    memset(&d, 0, sizeof(d));
    d.cb = cb;
    if (!device) {
        wcscpy(d.DeviceName, L"\\\\.\\DISPLAY1");
        wcscpy(d.DeviceString, L"Mime Display Adapter");
        d.StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE;
        wcscpy(d.DeviceID, L"ROOT\\MIME\\0000");
        wcscpy(d.DeviceKey, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{00000000-0000-0000-0000-000000000000}\\0000");
    } else if (!_wcsicmp(device, L"\\\\.\\DISPLAY1")) {
        wcscpy(d.DeviceName, L"\\\\.\\DISPLAY1\\Monitor0");
        wcscpy(d.DeviceString, L"Generic PnP Monitor");
        d.StateFlags = DISPLAY_DEVICE_ACTIVE | DISPLAY_DEVICE_ATTACHED;
        if (flags & EDD_GET_DEVICE_INTERFACE_NAME)
            wcscpy(d.DeviceID, L"\\\\?\\DISPLAY#MIME0000#1&00000000&0&UID0#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}");
        else
            wcscpy(d.DeviceID, L"MONITOR\\MIME0000\\{4d36e96e-e325-11ce-bfc1-08002be10318}\\0000");
        wcscpy(d.DeviceKey, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\{4d36e96e-e325-11ce-bfc1-08002be10318}\\0000");
    } else {
        return FALSE;
    }
    memcpy(info, &d, cb);
    return TRUE;
}

WINUSERAPI BOOL WINAPI EnumDisplayDevicesA(LPCSTR device, DWORD index, PDISPLAY_DEVICEA info, DWORD flags)
{
    if (!info || info->cb < offsetof(DISPLAY_DEVICEA, DeviceID)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LPWSTR w = UserAtoW(device);
    DISPLAY_DEVICEW dw;
    dw.cb = sizeof(dw);
    BOOL ok = EnumDisplayDevicesW(w, index, &dw, flags);
    UserFreeConverted(w, device);
    if (!ok)
        return FALSE;
    DISPLAY_DEVICEA da;
    memset(&da, 0, sizeof(da));
    da.cb = info->cb;
    WideCharToMultiByte(CP_ACP, 0, dw.DeviceName, -1, da.DeviceName, sizeof(da.DeviceName), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, dw.DeviceString, -1, da.DeviceString, sizeof(da.DeviceString), NULL, NULL);
    da.StateFlags = dw.StateFlags;
    WideCharToMultiByte(CP_ACP, 0, dw.DeviceID, -1, da.DeviceID, sizeof(da.DeviceID), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, dw.DeviceKey, -1, da.DeviceKey, sizeof(da.DeviceKey), NULL, NULL);
    memcpy(info, &da, info->cb > sizeof(da) ? sizeof(da) : info->cb);
    return TRUE;
}

/* ------------------------------------------------------------------ threads and message filters */

WINUSERAPI BOOL WINAPI AttachThreadInput(DWORD attach, DWORD to, BOOL do_attach)
{
    return NtUserAttachThreadInput(attach, to, do_attach);
}

/* user interface privilege isolation does not exist here: every message is allowed. */
WINUSERAPI BOOL WINAPI ChangeWindowMessageFilterEx(HWND hwnd, UINT msg, DWORD action, PCHANGEFILTERSTRUCT info)
{
    if (action > MSGFLT_DISALLOW || (info && info->cbSize != sizeof(CHANGEFILTERSTRUCT))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (info)
        info->ExtStatus = action == MSGFLT_ALLOW ? MSGFLTINFO_ALREADYALLOWED_FORWND : MSGFLTINFO_NONE;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ChangeWindowMessageFilter(UINT msg, DWORD flag)
{
    if (flag != 1 /* MSGFLT_ADD */ && flag != 2 /* MSGFLT_REMOVE */) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ multibyte characters */

WINUSERAPI LPSTR WINAPI CharNextExA(WORD code_page, LPCSTR s, DWORD flags)
{
    if (!s || !*s)
        return (LPSTR)s;
    if (IsDBCSLeadByteEx(code_page, (BYTE)s[0]) && s[1])
        return (LPSTR)(s + 2);
    return (LPSTR)(s + 1);
}

WINUSERAPI LPSTR WINAPI CharPrevExA(WORD code_page, LPCSTR start, LPCSTR s, DWORD flags)
{
    if (!start || !s || s <= start)
        return (LPSTR)start;
    /* walk forward from the start, lead bytes are ambiguous backwards. */
    LPCSTR p = start, prev = start;
    while (p < s && *p) {
        prev = p;
        p = CharNextExA(code_page, p, flags);
    }
    return (LPSTR)prev;
}
