/*
 * the messages whose parameters point to memory: which they are, what a send to another process copies of what
 * they point to, and what comes back. a thread and the thread of another process share nothing, so the data goes
 * into the kernel, the receiver gets its own copy through the window procedure callback, and what the procedure
 * wrote is copied to the sender after it returned. the rules follow the thunk tables of windows' win32k and user32.
 */
#include "win32k.h"
#include <mime/msgtable.h>

/* the largest block copied for a message, in bytes. a program asking for more is refused. */
#define MSG_MAX_BLOCK (64u << 20)
/* the characters a text query is given room for, whatever its buffer size claims. */
#define MSG_MAX_TEXT (1u << 22)

#define RULE(class, layout, size) ((MSG_RULE){(class), (layout), (size)})

MSG_RULE MsgRuleFor(UINT msg)
{
    if (msg >= 0x400)
        return RULE(MC_VALUE, 0, 0);
    switch (msg) {
    case WM_CREATE:
    case WM_NCCREATE:
        return RULE(MC_CREATE, 0, 0);
    case WM_SETTEXT:
    case WM_SETTINGCHANGE:
    case EM_REPLACESEL:
    case WM_DEVMODECHANGE:
    case CB_DIR:
    case LB_DIR:
    case LB_ADDFILE:
        return RULE(MC_STRING_IN, 0, 0);
    case LB_ADDSTRING:
    case LB_INSERTSTRING:
    case LB_SELECTSTRING:
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
    case 0x1AA:
    case 0x1AB:
    case 0x1AC:
    case 0x1AD:
        return RULE(MC_ITEM_LB, 0, 0);
    case CB_ADDSTRING:
    case CB_INSERTSTRING:
    case CB_FINDSTRING:
    case CB_SELECTSTRING:
    case CB_FINDSTRINGEXACT:
        return RULE(MC_ITEM_CB, 0, 0);
    case WM_GETTEXT:
        return RULE(MC_STRING_OUT, 0, 0);
    case EM_GETLINE:
        return RULE(MC_LINE_OUT, 0, 0);
    case LB_GETTEXT:
    case CB_GETLBTEXT:
        return RULE(MC_ITEM_OUT, 0, 0);
    case 0x030C: /* WM_ASKCBFORMATNAME */
        return RULE(MC_FORMAT_OUT, 0, 0);
    case WM_DRAWITEM:
        return RULE(MC_STRUCT_IN, 1, 64);
    case WM_MEASUREITEM:
        return RULE(MC_STRUCT_INOUT, 1, 32);
    case WM_DELETEITEM:
        return RULE(MC_STRUCT_IN, 1, 32);
    case WM_COMPAREITEM:
        return RULE(MC_STRUCT_IN, 1, 56);
    case WM_WINDOWPOSCHANGING:
        return RULE(MC_STRUCT_INOUT, 1, 40);
    case WM_WINDOWPOSCHANGED:
        return RULE(MC_STRUCT_IN, 1, 40);
    case WM_GETMINMAXINFO:
        return RULE(MC_STRUCT_INOUT, 0, 40);
    case WM_STYLECHANGING:
    case WM_STYLECHANGED:
        return RULE(MC_STRUCT_INOUT, 0, 8);
    case 0x008B:
    case EM_SETRECT:
    case EM_SETRECTNP:
    case LB_GETITEMRECT:
    case WM_SIZING:
    case WM_MOVING:
    case WM_DPICHANGED:
        return RULE(MC_STRUCT_INOUT, 0, 16);
    case EM_GETRECT:
    case CB_GETDROPPEDCONTROLRECT:
        return RULE(MC_STRUCT_OUT, 0, 16);
    case 0x02E4:
        return RULE(MC_STRUCT_INOUT, 0, 8);
    case SBM_SETSCROLLINFO:
    case SBM_GETSCROLLINFO:
        return RULE(MC_STRUCT_INOUT, 0, 28);
    case 0x00EB: /* SBM_GETSCROLLBARINFO */
        return RULE(MC_STRUCT_INOUT, 0, 60);
    case CB_GETCOMBOBOXINFO:
        return RULE(MC_STRUCT_INOUT, 1, 64);
    case WM_NEXTMENU:
        return RULE(MC_STRUCT_INOUT, 1, 24);
    case 0x0124: /* WM_MENUGETOBJECT */
        return RULE(MC_STRUCT_INOUT, 1, 32);
    case 0x022A: /* WM_DROPOBJECT and the other drag list messages */
    case 0x022B:
    case 0x022D:
    case 0x022E:
    case 0x022F:
        return RULE(MC_STRUCT_INOUT, 1, 48);
    case 0x033F: /* WM_GETTITLEBARINFOEX */
        return RULE(MC_TITLEBAR, 0, 140);
    case WM_HELP:
        return RULE(MC_HELP, 1, 40);
    case WM_GETDLGCODE:
        return RULE(MC_OPTMSG, 1, 48);
    case WM_COPYDATA:
        return RULE(MC_COPYDATA, 1, 0);
    case WM_NCCALCSIZE:
        return RULE(MC_NCCALCSIZE, 1, 0);
    case WM_MDICREATE:
        return RULE(MC_MDICREATE, 1, 0);
    case EM_GETSEL:
    case SBM_GETRANGE:
    case CB_GETEDITSEL:
    case 0x0229: /* WM_MDIGETACTIVE */
        return RULE(MC_PTR2, 0, 0);
    case 0x01EB:
        return RULE(MC_WPTR, 0, 0);
    case EM_SETTABSTOPS:
    case LB_SETTABSTOPS:
        return RULE(MC_UINTS_IN, 0, 0);
    case LB_GETSELITEMS:
        return RULE(MC_UINTS_OUT, 0, 0);
    case WM_DEVICECHANGE:
        return RULE(MC_DEVCHANGE, 0, 0);
    case WM_POWERBROADCAST:
        return RULE(MC_POWER, 0, 0);
    case 0x0038: /* WM_WINHELP */
        return RULE(MC_WINHELP, 0, 0);
    case 0x0049: /* WM_COPYGLOBALDATA */
        return RULE(MC_COPYGLOBAL, 0, 0);
    }
    if (MimeMsgKernelOnly(msg))
        return RULE(MC_KERNEL, 0, 0);
    return RULE(MC_VALUE, 0, 0);
}

/* ------------------------------------------------------------------ reading the sender's memory */

static BOOLEAN user_read(PVOID dst, LPARAM user, SIZE_T size)
{
    return NT_SUCCESS(MimeCopyFromUser(dst, (PVOID)user, size));
}

static BOOLEAN user_write(LPARAM user, const void *src, SIZE_T size)
{
    return NT_SUCCESS(MimeCopyToUser((PVOID)user, src, size));
}

/*
 * a string ends at its terminator, which may sit at the end of the last page there is, so it is read to the end of
 * each page in turn.
 */
static BOOLEAN read_wstring(LPARAM user, PWSTR *out, ULONG *chars)
{
    *out = nullptr;
    if (user & 1)
        return FALSE;
    SIZE_T cap = 128, n = 0;
    PWSTR buf = W32Alloc(cap * sizeof(WCHAR));
    if (!buf)
        return FALSE;
    for (;;) {
        SIZE_T at = (SIZE_T)user + n * sizeof(WCHAR);
        SIZE_T room = (0x1000 - (at & 0xFFF)) / sizeof(WCHAR);
        if (n + room + 1 > cap) {
            SIZE_T grown = cap;
            while (n + room + 1 > grown)
                grown *= 2;
            if (grown * sizeof(WCHAR) > MSG_MAX_BLOCK) {
                W32Free(buf);
                return FALSE;
            }
            PWSTR bigger = W32Alloc(grown * sizeof(WCHAR));
            if (!bigger) {
                W32Free(buf);
                return FALSE;
            }
            memcpy(bigger, buf, n * sizeof(WCHAR));
            W32Free(buf);
            buf = bigger;
            cap = grown;
        }
        if (!user_read(buf + n, (LPARAM)at, room * sizeof(WCHAR))) {
            W32Free(buf);
            return FALSE;
        }
        for (SIZE_T i = 0; i < room; i++) {
            if (!buf[n + i]) {
                *out = buf;
                *chars = (ULONG)(n + i);
                return TRUE;
            }
        }
        n += room;
    }
}

static PVOID block(ULONG size)
{
    return size <= MSG_MAX_BLOCK ? W32Alloc(size ? size : 1) : nullptr;
}

/* ------------------------------------------------------------------ copying in */

/* a block holding the structure of the sender, or nothing at all if it cannot be read. */
static BOOLEAN capture_struct(MSG_CAPTURE *c, LPARAM user, ULONG size, ULONG kind, BOOLEAN read)
{
    if (!user)
        return FALSE;
    c->Buffer = block(size);
    if (!c->Buffer)
        return FALSE;
    c->Size = size;
    c->Kind = kind;
    if (read && !user_read(c->Buffer, user, size))
        return FALSE;
    return TRUE;
}

/* a string with room for the characters a query for text asks for, zeroed. */
static BOOLEAN capture_text_room(MSG_CAPTURE *c, ULONG chars)
{
    c->Buffer = block(((SIZE_T)chars + 2) * sizeof(WCHAR));
    if (!c->Buffer)
        return FALSE;
    c->Size = (chars + 2) * sizeof(WCHAR);
    c->Kind = MIME_LPARAM_INOUT;
    c->Chars = chars;
    return TRUE;
}

/* the sender's buffer must be there at both ends, as a write into it will be tried. */
static BOOLEAN probe_buffer(LPARAM user, ULONG bytes)
{
    UCHAR byte;
    if (!bytes)
        return TRUE;
    return user && user_read(&byte, user, 1) && user_read(&byte, user + bytes - 1, 1);
}

static BOOLEAN item_has_text(UINT msg, DWORD style)
{
    BOOLEAN combo = msg >= 0x140 && msg < 0x170;
    return (style & (combo ? 0x200u : 0x40u)) || !(style & 0x30);
}

static BOOLEAN capture_string(MSG_CAPTURE *c, LPARAM user)
{
    PWSTR text;
    ULONG chars;
    if (!user)
        return TRUE;
    if (!read_wstring(user, &text, &chars))
        return FALSE;
    c->Buffer = text;
    c->Size = (chars + 1) * sizeof(WCHAR);
    c->Kind = MIME_LPARAM_IN;
    return TRUE;
}

/* a string held in a structure comes in after it, the structure keeping the place it was put at. */
static BOOLEAN append_string(MSG_CAPTURE *c, ULONG_PTR *field, ULONG min_pointer)
{
    ULONG_PTR user = *field;
    if (user < min_pointer)
        return TRUE;
    PWSTR text;
    ULONG chars;
    if (!read_wstring((LPARAM)user, &text, &chars))
        return FALSE;
    SIZE_T bytes = ((SIZE_T)chars + 1) * sizeof(WCHAR);
    SIZE_T at = (c->Size + 7) & ~(SIZE_T)7;
    PUCHAR bigger = block((ULONG)(at + bytes));
    if (!bigger || at + bytes > MSG_MAX_BLOCK) {
        W32Free(text);
        W32Free(bigger);
        return FALSE;
    }
    memcpy(bigger, c->Buffer, c->Size);
    memcpy(bigger + at, text, bytes);
    W32Free(text);
    W32Free(c->Buffer);
    c->Buffer = bigger;
    c->Size = (ULONG)(at + bytes);
    *field = 0x10000 + at;
    return TRUE;
}

BOOLEAN MsgCapture(MSG_CAPTURE *c, WND *target, UINT msg, WPARAM wparam, LPARAM lparam, MSG_RULE rule, LONG hint)
{
    memset(c, 0, sizeof(*c));
    c->Msg = msg;
    c->Wparam = wparam;
    c->Lparam = lparam;
    c->UserWparam = wparam;
    c->UserLparam = lparam;
    c->Class = rule.Class;
    c->Refused = 0;

    switch (rule.Class) {
    case MC_STRING_IN:
        return capture_string(c, lparam);
    case MC_ITEM_LB:
    case MC_ITEM_CB:
        if (!item_has_text(msg, target->Style))
            return TRUE;
        return capture_string(c, lparam);
    case MC_STRING_OUT:
    case MC_FORMAT_OUT: {
        if (wparam > 0x3FFFFFFF) {
            W32SetLastError(0);
            return FALSE;
        }
        if (rule.Class == MC_FORMAT_OUT && wparam < 1) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        ULONG chars = wparam > MSG_MAX_TEXT ? MSG_MAX_TEXT : (ULONG)wparam;
        if (wparam && !probe_buffer(lparam, (ULONG)(wparam > MSG_MAX_TEXT ? chars : wparam) * 2))
            return FALSE;
        if (!lparam && wparam)
            return FALSE;
        c->Wparam = chars;
        c->Capacity = (ULONG)wparam;
        return capture_text_room(c, chars);
    }
    case MC_LINE_OUT: {
        USHORT max;
        if (!lparam || !user_read(&max, lparam, sizeof(max)) || max < 1 || !probe_buffer(lparam, max * 2u))
            return FALSE;
        c->Capacity = max;
        if (!capture_text_room(c, max))
            return FALSE;
        *(USHORT *)c->Buffer = max;
        return TRUE;
    }
    case MC_ITEM_OUT: {
        /* the text is as long as the control says, and it needs a terminator. an item without text gives its data. */
        if (hint < 0) {
            c->Refused = (LRESULT)-1;
            return FALSE;
        }
        ULONG chars = (ULONG)hint + 1;
        if (chars < 4)
            chars = 4;
        if (chars > MSG_MAX_TEXT)
            return FALSE;
        if (!probe_buffer(lparam, chars * 2))
            return FALSE;
        c->Capacity = chars;
        c->ItemData = !item_has_text(msg == LB_GETTEXT ? LB_ADDSTRING : CB_ADDSTRING, target->Style);
        return capture_text_room(c, chars);
    }
    case MC_STRUCT_IN:
    case MC_STRUCT_INOUT:
    case MC_STRUCT_OUT:
        if (!capture_struct(c, lparam, rule.Size, rule.Class == MC_STRUCT_IN ? MIME_LPARAM_IN : MIME_LPARAM_INOUT,
                            rule.Class != MC_STRUCT_OUT))
            return FALSE;
        return TRUE;
    case MC_TITLEBAR: {
        DWORD size;
        if (!lparam || !user_read(&size, lparam, sizeof(size)))
            return FALSE;
        if (size != rule.Size) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return capture_struct(c, lparam, rule.Size, MIME_LPARAM_INOUT, TRUE);
    }
    case MC_HELP: {
        DWORD size;
        if (!lparam || !user_read(&size, lparam, sizeof(size)))
            return FALSE;
        if (size != rule.Size) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return capture_struct(c, lparam, rule.Size, MIME_LPARAM_IN, TRUE);
    }
    case MC_OPTMSG:
        if (!lparam)
            return TRUE;
        return capture_struct(c, lparam, rule.Size, MIME_LPARAM_IN, TRUE);
    case MC_COPYDATA: {
        if (!lparam)
            return FALSE;
        COPYDATASTRUCT cd;
        if (!user_read(&cd, lparam, sizeof(cd)))
            return FALSE;
        ULONG size = cd.lpData ? cd.cbData : 0;
        if (size > MSG_MAX_BLOCK - sizeof(cd))
            return FALSE;
        c->Buffer = block(sizeof(cd) + size);
        if (!c->Buffer)
            return FALSE;
        if (cd.lpData && size && !user_read((PUCHAR)c->Buffer + sizeof(cd), (LPARAM)cd.lpData, size))
            return FALSE;
        /* the padding after the size is cleared, and the data is where the structure says it is. */
        COPYDATASTRUCT *out = c->Buffer;
        out->dwData = cd.dwData;
        memset(&out->cbData, 0, 8);
        out->cbData = cd.cbData;
        out->lpData = cd.lpData ? (PVOID)(ULONG_PTR)sizeof(cd) : nullptr;
        c->Size = (ULONG)(sizeof(cd) + size);
        c->Kind = MIME_LPARAM_IN;
        return TRUE;
    }
    case MC_NCCALCSIZE: {
        if (!lparam)
            return FALSE;
        if (!wparam)
            return capture_struct(c, lparam, sizeof(RECT), MIME_LPARAM_INOUT, TRUE);
        NCCALCSIZE_PARAMS params;
        WINDOWPOS pos;
        if (!user_read(&params, lparam, sizeof(params)) || !params.lppos || !user_read(&pos, (LPARAM)params.lppos, sizeof(pos)))
            return FALSE;
        c->Buffer = block(sizeof(params) + sizeof(pos));
        if (!c->Buffer)
            return FALSE;
        memcpy(c->Buffer, &params, sizeof(params));
        memcpy((PUCHAR)c->Buffer + sizeof(params), &pos, sizeof(pos));
        c->Size = sizeof(params) + sizeof(pos);
        c->Kind = MIME_LPARAM_INOUT;
        c->UserPos = (LPARAM)params.lppos;
        return TRUE;
    }
    case MC_MDICREATE: {
        if (!lparam)
            return FALSE;
        MDICREATESTRUCTW mdi;
        if (!user_read(&mdi, lparam, sizeof(mdi)))
            return FALSE;
        c->Buffer = block(sizeof(mdi));
        if (!c->Buffer)
            return FALSE;
        memcpy(c->Buffer, &mdi, sizeof(mdi));
        c->Size = sizeof(mdi);
        c->Kind = MIME_LPARAM_IN;
        MDICREATESTRUCTW *copy = c->Buffer;
        /* the class may be an atom, a small number, which stays as it is. */
        ULONG_PTR cls = (ULONG_PTR)copy->szClass, title = (ULONG_PTR)copy->szTitle;
        if (!append_string(c, &cls, 0x10000))
            return FALSE;
        copy = c->Buffer;
        if (!append_string(c, &title, 1))
            return FALSE;
        copy = c->Buffer;
        copy->szClass = (LPCWSTR)cls;
        copy->szTitle = (LPCWSTR)title;
        return TRUE;
    }
    case MC_PTR2:
        c->Buffer = block(8);
        if (!c->Buffer)
            return FALSE;
        c->Size = 8;
        c->Kind = MIME_LPARAM_PTR2;
        return TRUE;
    case MC_WPTR:
        c->Buffer = block(16);
        if (!c->Buffer)
            return FALSE;
        *(LPARAM *)((PUCHAR)c->Buffer + 8) = lparam;
        c->Size = 16;
        c->Kind = MIME_LPARAM_WPTR;
        return TRUE;
    case MC_UINTS_IN: {
        if (!lparam)
            return TRUE;
        if (wparam > MSG_MAX_BLOCK / 4)
            return FALSE;
        return capture_struct(c, lparam, (ULONG)wparam * 4, MIME_LPARAM_IN, TRUE);
    }
    case MC_UINTS_OUT:
        if (wparam > MSG_MAX_BLOCK / 4 || !probe_buffer(lparam, (ULONG)wparam * 4))
            return FALSE;
        if (!wparam)
            return TRUE;
        return capture_struct(c, lparam, (ULONG)wparam * 4, MIME_LPARAM_INOUT, FALSE);
    case MC_DEVCHANGE: {
        BOOLEAN needs = wparam == 0x8000 || wparam == 0x8004 || (wparam >= 0x8007 && wparam <= 0x800A);
        if (needs && !lparam) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (!(wparam & 0x8000) || !lparam)
            return TRUE;
        DWORD size;
        if (!user_read(&size, lparam, sizeof(size)))
            return FALSE;
        if (size < 12 || size > MSG_MAX_BLOCK - 2) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        c->Buffer = block(size + 2);
        if (!c->Buffer || !user_read(c->Buffer, lparam, size))
            return FALSE;
        c->Size = size + 2;
        c->Kind = MIME_LPARAM_IN;
        return TRUE;
    }
    case MC_POWER: {
        if (!(wparam & 0x8000) || !lparam)
            return TRUE;
        DWORD length;
        if (!user_read(&length, lparam + 16, sizeof(length)) || length > MSG_MAX_BLOCK - 20)
            return FALSE;
        return capture_struct(c, lparam, 20 + length, MIME_LPARAM_IN, TRUE);
    }
    case MC_WINHELP: {
        DWORD head;
        if (!lparam || !user_read(&head, lparam, sizeof(head)) || (USHORT)head < 0x18)
            return FALSE;
        if (!capture_struct(c, lparam, (USHORT)head, MIME_LPARAM_IN, TRUE))
            return FALSE;
        *(USHORT *)c->Buffer = (USHORT)head;
        return TRUE;
    }
    case MC_COPYGLOBAL:
        if (!lparam || wparam > MSG_MAX_BLOCK)
            return FALSE;
        return capture_struct(c, lparam, (ULONG)wparam, MIME_LPARAM_IN, TRUE);
    default:
        return TRUE;
    }
}

void MsgCaptureFree(MSG_CAPTURE *c)
{
    W32Free(c->Buffer);
    c->Buffer = nullptr;
}

/* ------------------------------------------------------------------ copying out */

/* the characters of a text up to its terminator, at most the room there is. */
static ULONG text_chars(const WCHAR *text, ULONG room)
{
    ULONG n = 0;
    while (n < room && text[n])
        n++;
    return n;
}

/*
 * what the procedure wrote goes back into the sender's memory. nothing is copied when the send did not finish,
 * and a write that fails is dropped, as the sender may have unmapped what it passed.
 */
void MsgCaptureFinish(MSG_CAPTURE *c, LRESULT *result, BOOLEAN completed)
{
    if (!completed || !c->Buffer)
        return;
    switch (c->Class) {
    case MC_STRING_OUT:
    case MC_FORMAT_OUT: {
        ULONG room = c->Chars;
        PWSTR text = c->Buffer;
        LRESULT n = *result;
        if (n <= 0 || room == 0) {
            if (c->UserWparam)
                user_write(c->UserLparam, L"", sizeof(WCHAR));
            if (n < 0)
                *result = 0;
            return;
        }
        if ((ULONG_PTR)n > room)
            n = room;
        ULONG got = text_chars(text, (ULONG)n);
        /* the terminator goes along when the buffer has room for it. */
        ULONG copy = got < c->Capacity ? got + 1 : got;
        user_write(c->UserLparam, text, (SIZE_T)copy * sizeof(WCHAR));
        *result = got;
        return;
    }
    case MC_LINE_OUT: {
        LRESULT n = *result;
        if (n <= 0) {
            user_write(c->UserLparam, L"", sizeof(WCHAR));
            return;
        }
        if ((ULONG_PTR)n > c->Capacity)
            n = c->Capacity;
        user_write(c->UserLparam, c->Buffer, (SIZE_T)n * sizeof(WCHAR));
        return;
    }
    case MC_ITEM_OUT: {
        LRESULT n = *result;
        if (c->ItemData) {
            /* an item without text answers with its data, as many bytes as the control says it is. */
            if (n > 0)
                user_write(c->UserLparam, c->Buffer, (SIZE_T)n < sizeof(ULONG_PTR) ? (SIZE_T)n : sizeof(ULONG_PTR));
            return;
        }
        if (n < 0)
            return;
        /* the text and its end fit the room the sender was asked to have, whatever the receiver wrote. */
        ULONG got = text_chars(c->Buffer, c->Capacity - 1);
        user_write(c->UserLparam, c->Buffer, ((SIZE_T)got + 1) * sizeof(WCHAR));
        if ((ULONG_PTR)n > got)
            *result = got;
        return;
    }
    case MC_STRUCT_INOUT:
    case MC_STRUCT_OUT:
    case MC_TITLEBAR:
    case MC_UINTS_OUT:
        user_write(c->UserLparam, c->Buffer, c->Size);
        return;
    case MC_NCCALCSIZE:
        if (!c->UserWparam) {
            user_write(c->UserLparam, c->Buffer, c->Size);
            return;
        }
        /* the position goes where the sender keeps it, and the pointer to it stays the sender's. */
        user_write(c->UserLparam, c->Buffer, 48);
        user_write(c->UserPos, (PUCHAR)c->Buffer + sizeof(NCCALCSIZE_PARAMS), sizeof(WINDOWPOS));
        return;
    case MC_PTR2: {
        const DWORD *d = c->Buffer;
        if (c->UserWparam)
            user_write(c->UserWparam, &d[0], sizeof(DWORD));
        if (c->UserLparam)
            user_write(c->UserLparam, &d[1], sizeof(DWORD));
        return;
    }
    case MC_WPTR:
        user_write(c->UserWparam, c->Buffer, sizeof(DWORD));
        return;
    default:
        return;
    }
}
