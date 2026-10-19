/*
 * ansi and unicode. win32k carries unicode only. a message sent through an ansi function is turned into unicode
 * before it goes to the system, and what comes back is turned into ansi; a message that reaches a window procedure
 * expecting ansi is turned the other way. the messages that hold text or characters are the ones turned, those of
 * the standard controls, of the window itself, of creation and of device changes.
 */
#include "user32_private.h"
#include <mime/msgtable.h>

#ifndef WM_IME_CHAR
#define WM_IME_CHAR 0x0286
#endif

/* the ansi code page of windows 7 and later systems can be a double byte one. */
static BOOL dbcs_system(void)
{
    static int known = -1;
    if (known < 0) {
        CPINFO info;
        known = GetCPInfo(CP_ACP, &info) && info.MaxCharSize > 1;
    }
    return known;
}

/* ------------------------------------------------------------------ characters */

/* the unicode characters as many ansi bytes of the buffer make, as many as fit in room; whole characters only. */
static int ansi_to_wide_fit(const char *a, int bytes, WCHAR *out, int room)
{
    if (room <= 0 || bytes <= 0)
        return 0;
    if (!dbcs_system()) {
        int n = bytes < room ? bytes : room;
        return MultiByteToWideChar(CP_ACP, 0, a, n, out, room);
    }
    int chars = 0;
    for (int i = 0; i < bytes && chars < room;) {
        int length = IsDBCSLeadByte((BYTE)a[i]) && i + 1 < bytes ? 2 : 1;
        if (!MultiByteToWideChar(CP_ACP, 0, a + i, length, out + chars, 1))
            out[chars] = L'?';
        chars++;
        i += length;
    }
    return chars;
}

/* the ansi bytes of as many characters as fit in room bytes, whole characters only. */
static int wide_to_ansi_fit(const WCHAR *w, int chars, char *out, int room)
{
    if (room <= 0 || chars <= 0)
        return 0;
    if (!dbcs_system()) {
        int n = chars < room ? chars : room;
        return WideCharToMultiByte(CP_ACP, 0, w, n, out, room, nullptr, nullptr);
    }
    int bytes = 0;
    for (int i = 0; i < chars; i++) {
        char one[4];
        int length = WideCharToMultiByte(CP_ACP, 0, w + i, 1, one, sizeof(one), nullptr, nullptr);
        if (length <= 0 || bytes + length > room)
            break;
        memcpy(out + bytes, one, length);
        bytes += length;
    }
    return bytes;
}

/* the bytes the characters take in ansi. */
static int wide_ansi_size(const WCHAR *w, int chars)
{
    if (chars <= 0)
        return 0;
    if (!dbcs_system())
        return chars;
    return WideCharToMultiByte(CP_ACP, 0, w, chars, nullptr, 0, nullptr, nullptr);
}

static int ansi_wide_size(const char *a, int bytes)
{
    if (bytes <= 0)
        return 0;
    if (!dbcs_system())
        return bytes;
    return MultiByteToWideChar(CP_ACP, 0, a, bytes, nullptr, 0);
}

static WCHAR char_to_wide(UINT ch)
{
    if (ch < 0x80)
        return (WCHAR)ch;
    char two[2] = {(char)(ch >> 8), (char)ch};
    WCHAR wc = L'?';
    if (ch > 0xFF)
        MultiByteToWideChar(CP_ACP, 0, two, 2, &wc, 1);
    else
        MultiByteToWideChar(CP_ACP, 0, two + 1, 1, &wc, 1);
    return wc;
}

/* the ansi character in the low word, a double byte one with its lead byte first. */
static UINT wide_to_char(WCHAR wc)
{
    if (wc < 0x80)
        return wc;
    char buf[4];
    int n = WideCharToMultiByte(CP_ACP, 0, &wc, 1, buf, sizeof(buf), nullptr, nullptr);
    if (n == 2)
        return ((UCHAR)buf[0] << 8) | (UCHAR)buf[1];
    return n == 1 ? (UCHAR)buf[0] : '?';
}

/* ------------------------------------------------------------------ which messages */

enum {
    CV_NONE,
    CV_STRING,     /* lparam is a string */
    CV_ITEM,       /* lparam is a string, or the data of an item that has none */
    CV_TEXT_OUT,   /* wparam characters of room at lparam */
    CV_LINE_OUT,   /* room in the first word of lparam's buffer */
    CV_ITEM_OUT,   /* an item's text, the room being the item's length */
    CV_LENGTH,     /* a length of text, in characters of the other set */
    CV_CHAR,       /* wparam is a character */
    CV_CHAR_LOW,   /* the low word of wparam is a character */
    CV_MDI,        /* an MDICREATESTRUCT */
    CV_CREATE,     /* a CREATESTRUCT */
    CV_DEVICE,     /* a DEV_BROADCAST structure with a name */
    CV_PASSWORD_SET,
    CV_PASSWORD_GET,
};

static int kind_of(UINT msg)
{
    switch (msg) {
    case WM_SETTEXT:
    case WM_SETTINGCHANGE:
    case WM_DEVMODECHANGE:
    case EM_REPLACESEL:
    case CB_DIR:
    case LB_DIR:
    case LB_ADDFILE:
        return CV_STRING;
    case LB_ADDSTRING:
    case LB_INSERTSTRING:
    case LB_SELECTSTRING:
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
    case 0x1AA:
    case 0x1AB:
    case 0x1AC:
    case 0x1AD:
    case CB_ADDSTRING:
    case CB_INSERTSTRING:
    case CB_FINDSTRING:
    case CB_SELECTSTRING:
    case CB_FINDSTRINGEXACT:
        return CV_ITEM;
    case WM_GETTEXT:
    case 0x030C: /* WM_ASKCBFORMATNAME */
        return CV_TEXT_OUT;
    case EM_GETLINE:
        return CV_LINE_OUT;
    case LB_GETTEXT:
    case CB_GETLBTEXT:
        return CV_ITEM_OUT;
    case WM_GETTEXTLENGTH:
    case LB_GETTEXTLEN:
    case CB_GETLBTEXTLEN:
        return CV_LENGTH;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_IME_CHAR:
        return CV_CHAR;
    case WM_MENUCHAR:
    case WM_CHARTOITEM:
        return CV_CHAR_LOW;
    case WM_MDICREATE:
        return CV_MDI;
    case WM_CREATE:
    case WM_NCCREATE:
        return CV_CREATE;
    case WM_DEVICECHANGE:
        return CV_DEVICE;
    case EM_SETPASSWORDCHAR:
        return CV_PASSWORD_SET;
    case EM_GETPASSWORDCHAR:
        return CV_PASSWORD_GET;
    }
    return CV_NONE;
}

/* whether a command of a list or combo box carries a string: the control has strings, or the items have none to keep. */
static BOOL item_has_text(HWND hwnd, UINT msg)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    BOOL combo = msg >= 0x140 && msg < 0x170;
    return (style & (combo ? 0x200u : 0x40u)) || !(style & 0x30);
}

/* ------------------------------------------------------------------ ansi to unicode, for the sender */

/* a name inside a device broadcast structure, put after the fixed part of the unicode one. */
static void *device_to_wide(const void *in, void **block)
{
    const DWORD *h = in;
    DWORD size = h[0], type = h[1];
    DWORD fixed = type == 5 ? 28 : type == 3 ? 12 : 0;
    if (!fixed || size < fixed)
        return (void *)in;
    const char *name = (const char *)in + fixed;
    int bytes = 0;
    while (fixed + bytes < size && name[bytes])
        bytes++;
    int chars = ansi_wide_size(name, bytes);
    DWORD total = fixed + (chars + 1) * sizeof(WCHAR);
    DWORD *out = UserAlloc(total);
    if (!out)
        return (void *)in;
    memcpy(out, in, fixed);
    out[0] = total;
    WCHAR *w = (WCHAR *)((char *)out + fixed);
    if (bytes)
        MultiByteToWideChar(CP_ACP, 0, name, bytes, w, chars);
    w[chars] = 0;
    *block = out;
    return out;
}

BOOL UserConvBegin(CONV *c, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    memset(c, 0, sizeof(*c));
    c->hwnd = hwnd;
    c->msg = msg;
    c->wparam = c->user_wparam = wparam;
    c->lparam = c->user_lparam = lparam;
    c->kind = msg < 0x400 ? kind_of(msg) : CV_NONE;
    switch (c->kind) {
    case CV_ITEM:
        if (!item_has_text(hwnd, msg)) {
            c->kind = CV_NONE;
            return TRUE;
        }
        [[fallthrough]];
    case CV_STRING:
        if (lparam) {
            c->block = UserAtoW((LPCSTR)lparam);
            if (!c->block)
                return FALSE;
            c->lparam = (LPARAM)c->block;
        }
        return TRUE;
    case CV_TEXT_OUT: {
        if (!lparam || !wparam)
            return TRUE;
        SIZE_T room = wparam < 0x400000 ? wparam : 0x400000;
        c->block = UserAlloc((room + 1) * sizeof(WCHAR));
        if (!c->block)
            return FALSE;
        c->room = (int)wparam;
        c->wparam = room;
        c->lparam = (LPARAM)c->block;
        return TRUE;
    }
    case CV_LINE_OUT: {
        if (!lparam)
            return TRUE;
        WORD room = *(WORD *)lparam;
        c->block = UserAlloc(((SIZE_T)room + 2) * sizeof(WCHAR));
        if (!c->block)
            return FALSE;
        *(WORD *)c->block = room;
        c->room = room;
        c->lparam = (LPARAM)c->block;
        return TRUE;
    }
    case CV_ITEM_OUT: {
        if (!lparam || !item_has_text(hwnd, msg)) {
            c->kind = CV_NONE;
            return TRUE;
        }
        LRESULT n = SendMessageW(hwnd, msg == LB_GETTEXT ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, wparam, 0);
        if (n < 0) {
            c->refused = TRUE;
            c->refusal = n;
            return FALSE;
        }
        c->block = UserAlloc(((SIZE_T)n + 2) * sizeof(WCHAR));
        if (!c->block)
            return FALSE;
        c->room = (int)n + 1;
        c->lparam = (LPARAM)c->block;
        return TRUE;
    }
    case CV_CHAR:
        c->wparam = char_to_wide((UINT)wparam);
        return TRUE;
    case CV_CHAR_LOW:
        c->wparam = (wparam & ~(WPARAM)0xFFFF) | char_to_wide((UINT)(wparam & 0xFFFF));
        return TRUE;
    case CV_PASSWORD_SET:
        c->wparam = wparam ? char_to_wide((UINT)(wparam & 0xFF)) : 0;
        return TRUE;
    case CV_MDI: {
        if (!lparam)
            return TRUE;
        const MDICREATESTRUCTA *a = (const MDICREATESTRUCTA *)lparam;
        MDICREATESTRUCTW *w = UserAlloc(sizeof(*w));
        if (!w)
            return FALSE;
        memcpy(w, a, sizeof(*w));
        w->szClass = UserAtoW(a->szClass);
        w->szTitle = UserAtoW(a->szTitle);
        c->block = w;
        c->lparam = (LPARAM)w;
        return TRUE;
    }
    case CV_CREATE: {
        if (!lparam)
            return TRUE;
        const CREATESTRUCTA *a = (const CREATESTRUCTA *)lparam;
        CREATESTRUCTW *w = UserAlloc(sizeof(*w));
        if (!w)
            return FALSE;
        memcpy(w, a, sizeof(*w));
        w->lpszName = UserAtoW(a->lpszName);
        w->lpszClass = UserAtoW(a->lpszClass);
        c->block = w;
        c->lparam = (LPARAM)w;
        return TRUE;
    }
    case CV_DEVICE: {
        if (!(wparam & 0x8000) || !lparam)
            return TRUE;
        void *block = nullptr;
        c->lparam = (LPARAM)device_to_wide((const void *)lparam, &block);
        c->block = block;
        return TRUE;
    }
    }
    return TRUE;
}

/* the result of a call made on the unicode form, as the caller who used the ansi form gets it. */
LRESULT UserConvEnd(CONV *c, LRESULT result)
{
    switch (c->kind) {
    case CV_STRING:
    case CV_ITEM:
        UserFreeConverted(c->block, (void *)c->user_lparam);
        break;
    case CV_TEXT_OUT: {
        if (!c->block)
            break;
        char *out = (char *)c->user_lparam;
        int room = c->room;
        if (result <= 0) {
            out[0] = 0;
            UserFree(c->block);
            return result;
        }
        int chars = (int)result < room ? (int)result : room - 1;
        if (chars > (int)c->wparam)
            chars = (int)c->wparam;
        int bytes = wide_to_ansi_fit((WCHAR *)c->block, chars, out, room - 1);
        out[bytes] = 0;
        UserFree(c->block);
        return bytes;
    }
    case CV_LINE_OUT: {
        if (!c->block)
            break;
        char *out = (char *)c->user_lparam;
        if (result <= 0) {
            out[0] = 0;
            UserFree(c->block);
            return result;
        }
        int bytes = wide_to_ansi_fit((WCHAR *)c->block, (int)result, out, c->room);
        UserFree(c->block);
        return bytes;
    }
    case CV_ITEM_OUT: {
        if (!c->block)
            break;
        if (result < 0) {
            UserFree(c->block);
            return result;
        }
        char *out = (char *)c->user_lparam;
        int bytes = wide_to_ansi_fit((WCHAR *)c->block, (int)result, out, 1 << 30);
        out[bytes] = 0;
        UserFree(c->block);
        return bytes;
    }
    case CV_LENGTH:
        if (result > 0 && dbcs_system()) {
            /* the length the text has in ansi needs the text. */
            WCHAR *text = UserAlloc(((SIZE_T)result + 2) * sizeof(WCHAR));
            if (text) {
                LRESULT n = c->msg == WM_GETTEXTLENGTH
                                ? SendMessageW(c->hwnd, WM_GETTEXT, (WPARAM)result + 1, (LPARAM)text)
                                : SendMessageW(c->hwnd, c->msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT,
                                               c->user_wparam, (LPARAM)text);
                result = n > 0 ? wide_ansi_size(text, (int)n) : result;
                UserFree(text);
            }
        }
        break;
    case CV_PASSWORD_GET:
        result = result ? (LRESULT)wide_to_char((WCHAR)result) : 0;
        break;
    case CV_MDI: {
        MDICREATESTRUCTW *w = c->block;
        if (w) {
            const MDICREATESTRUCTA *a = (const MDICREATESTRUCTA *)c->user_lparam;
            UserFreeConverted(w->szClass, a->szClass);
            UserFreeConverted(w->szTitle, a->szTitle);
            UserFree(w);
        }
        break;
    }
    case CV_CREATE: {
        CREATESTRUCTW *w = c->block;
        if (w) {
            const CREATESTRUCTA *a = (const CREATESTRUCTA *)c->user_lparam;
            UserFreeConverted(w->lpszName, a->lpszName);
            UserFreeConverted(w->lpszClass, a->lpszClass);
            UserFree(w);
        }
        break;
    }
    case CV_DEVICE:
        UserFree(c->block);
        break;
    }
    return result;
}

/* ------------------------------------------------------------------ the entry points that send */

LRESULT UserMessageCall(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR info, DWORD type, BOOL ansi)
{
    if (!ansi)
        return NtUserMessageCall(hwnd, msg, wparam, lparam, info, type, FALSE);
    CONV c;
    if (!UserConvBegin(&c, hwnd, msg, wparam, lparam))
        return c.refused ? c.refusal : 0;
    LRESULT r = NtUserMessageCall(hwnd, msg, c.wparam, c.lparam, info, type, FALSE);
    if (type == MIME_MSGCALL_SENDTIMEOUT) {
        /* the answer of the procedure is in the structure, the call only says it came. */
        MIME_SEND_TIMEOUT *st = (MIME_SEND_TIMEOUT *)info;
        if (r)
            st->Result = (ULONG_PTR)UserConvEnd(&c, (LRESULT)st->Result);
        else
            UserConvEnd(&c, 0);
        return r;
    }
    if (type == MIME_MSGCALL_SEND)
        return UserConvEnd(&c, r);
    UserConvEnd(&c, 0);
    return r;
}

LRESULT UserSendMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi)
{
    return UserMessageCall(hwnd, msg, wparam, lparam, 0, MIME_MSGCALL_SEND, ansi);
}

/* ------------------------------------------------------------------ unicode to ansi, for the receiver */

static LRESULT call_ansi_proc(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (kind_of(msg)) {
    case CV_ITEM:
        if (!item_has_text(hwnd, msg))
            return proc(hwnd, msg, wparam, lparam);
        [[fallthrough]];
    case CV_STRING: {
        LPSTR a = UserWtoA((LPCWSTR)lparam);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)a);
        UserFreeConverted(a, (void *)lparam);
        return r;
    }
    case CV_TEXT_OUT: {
        if (!wparam || !lparam)
            return proc(hwnd, msg, wparam, lparam);
        SIZE_T cap = wparam * (dbcs_system() ? 2 : 1) + 1;
        LPSTR a = UserAlloc(cap);
        if (!a)
            return 0;
        LRESULT n = proc(hwnd, msg, (WPARAM)cap, (LPARAM)a);
        WCHAR *out = (WCHAR *)lparam;
        int w = n > 0 ? ansi_to_wide_fit(a, (int)n, out, (int)wparam - 1) : 0;
        out[w] = 0;
        UserFree(a);
        return w;
    }
    case CV_LINE_OUT: {
        if (!lparam)
            return proc(hwnd, msg, wparam, lparam);
        WORD room = *(WORD *)lparam;
        SIZE_T cap = (SIZE_T)room * (dbcs_system() ? 2 : 1);
        if (cap > 0xFFFF)
            cap = 0xFFFF;
        LPSTR a = UserAlloc(cap + 2);
        if (!a)
            return 0;
        *(WORD *)a = (WORD)cap;
        LRESULT n = proc(hwnd, msg, wparam, (LPARAM)a);
        int w = n > 0 ? ansi_to_wide_fit(a, (int)n, (WCHAR *)lparam, room) : 0;
        if (!w && room)
            *(WCHAR *)lparam = 0;
        UserFree(a);
        return w;
    }
    case CV_ITEM_OUT: {
        if (!lparam || !item_has_text(hwnd, msg))
            return proc(hwnd, msg, wparam, lparam);
        LRESULT len = proc(hwnd, msg == LB_GETTEXT ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, wparam, 0);
        if (len < 0)
            return len;
        LPSTR a = UserAlloc((SIZE_T)len + 2);
        if (!a)
            return -1;
        LRESULT n = proc(hwnd, msg, wparam, (LPARAM)a);
        WCHAR *out = (WCHAR *)lparam;
        int w = n >= 0 ? ansi_to_wide_fit(a, (int)n, out, (int)len + 1) : 0;
        out[w] = 0;
        UserFree(a);
        return n < 0 ? n : w;
    }
    case CV_LENGTH: {
        LRESULT n = proc(hwnd, msg, wparam, lparam);
        if (n <= 0 || !dbcs_system())
            return n;
        LPSTR a = UserAlloc((SIZE_T)n + 2);
        if (!a)
            return n;
        LRESULT got = msg == WM_GETTEXTLENGTH ? proc(hwnd, WM_GETTEXT, (WPARAM)n + 1, (LPARAM)a)
                                              : proc(hwnd, msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT, wparam, (LPARAM)a);
        n = got > 0 ? ansi_wide_size(a, (int)got) : n;
        UserFree(a);
        return n;
    }
    case CV_CHAR: {
        if (wparam <= 0x7F)
            return proc(hwnd, msg, wparam, lparam);
        UINT ch = wide_to_char((WCHAR)wparam);
        /* a double byte character is two messages, the lead byte and then the trail byte. */
        if (ch > 0xFF) {
            proc(hwnd, msg, ch >> 8, lparam);
            return proc(hwnd, msg, ch & 0xFF, lparam);
        }
        return proc(hwnd, msg, ch, lparam);
    }
    case CV_CHAR_LOW: {
        WPARAM low = wparam & 0xFFFF;
        if (low <= 0x7F)
            return proc(hwnd, msg, wparam, lparam);
        return proc(hwnd, msg, (wparam & ~(WPARAM)0xFFFF) | wide_to_char((WCHAR)low), lparam);
    }
    case CV_PASSWORD_SET:
        return proc(hwnd, msg, wparam ? wide_to_char((WCHAR)wparam) : 0, lparam);
    case CV_PASSWORD_GET: {
        LRESULT r = proc(hwnd, msg, wparam, lparam);
        return r ? (LRESULT)char_to_wide((UINT)(r & 0xFF)) : 0;
    }
    case CV_MDI: {
        if (!lparam)
            return proc(hwnd, msg, wparam, lparam);
        const MDICREATESTRUCTW *w = (const MDICREATESTRUCTW *)lparam;
        MDICREATESTRUCTA a;
        memcpy(&a, w, sizeof(a));
        a.szClass = UserWtoA(w->szClass);
        a.szTitle = UserWtoA(w->szTitle);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)&a);
        UserFreeConverted(a.szClass, w->szClass);
        UserFreeConverted(a.szTitle, w->szTitle);
        return r;
    }
    case CV_CREATE: {
        if (!lparam)
            return proc(hwnd, msg, wparam, lparam);
        const CREATESTRUCTW *csw = (const CREATESTRUCTW *)lparam;
        CREATESTRUCTA csa;
        memcpy(&csa, csw, sizeof(csa));
        csa.lpszName = UserWtoA(csw->lpszName);
        csa.lpszClass = UserWtoA(csw->lpszClass);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)&csa);
        UserFreeConverted(csa.lpszName, csw->lpszName);
        UserFreeConverted(csa.lpszClass, csw->lpszClass);
        return r;
    }
    case CV_DEVICE: {
        const DWORD *h = (const DWORD *)lparam;
        if (!(wparam & 0x8000) || !lparam || (h[1] != 5 && h[1] != 3))
            return proc(hwnd, msg, wparam, lparam);
        DWORD fixed = h[1] == 5 ? 28 : 12;
        if (h[0] < fixed)
            return proc(hwnd, msg, wparam, lparam);
        const WCHAR *name = (const WCHAR *)((const char *)h + fixed);
        int chars = 0;
        while (fixed + (chars + 1) * sizeof(WCHAR) <= h[0] && name[chars])
            chars++;
        int bytes = wide_ansi_size(name, chars);
        DWORD total = fixed + bytes + 1;
        DWORD *a = UserAlloc(total);
        if (!a)
            return 0;
        memcpy(a, h, fixed);
        a[0] = total;
        if (chars)
            WideCharToMultiByte(CP_ACP, 0, name, chars, (char *)a + fixed, bytes, nullptr, nullptr);
        LRESULT r = proc(hwnd, msg, wparam, (LPARAM)a);
        UserFree(a);
        return r;
    }
    }
    return proc(hwnd, msg, wparam, lparam);
}

LRESULT UserCallProc(WNDPROC proc, BOOL proc_ansi, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!proc)
        return 0;
    if (proc_ansi && msg < 0x400)
        return call_ansi_proc(proc, hwnd, msg, wparam, lparam);
    return proc(hwnd, msg, wparam, lparam);
}

/* the character in the wparam of a posted or retrieved message, between its ansi and unicode forms. */
WPARAM UserCharWparam(UINT msg, WPARAM wparam, BOOL to_wide)
{
    switch (msg) {
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_IME_CHAR:
        return to_wide ? char_to_wide((UINT)wparam) : (wparam > 0x7F ? wide_to_char((WCHAR)wparam) : wparam);
    case WM_MENUCHAR:
    case WM_CHARTOITEM:
        if (to_wide)
            return (wparam & ~(WPARAM)0xFFFF) | char_to_wide((UINT)(wparam & 0xFFFF));
        return (wparam & ~(WPARAM)0xFFFF) | ((wparam & 0xFFFF) > 0x7F ? wide_to_char((WCHAR)wparam) : (wparam & 0xFFFF));
    case EM_SETPASSWORDCHAR:
        return to_wide ? (wparam ? char_to_wide((UINT)(wparam & 0xFF)) : 0) : (wparam ? wide_to_char((WCHAR)wparam) : 0);
    }
    return wparam;
}
