/*
 * the messages whose parameters point to memory: which they are, what a send to another process copies of what
 * they point to, and what comes back. a thread and the thread of another process share nothing, so the data goes
 * into the kernel, the receiver gets its own copy through the window procedure callback, and what the procedure
 * wrote is copied to the sender after it returned. docs/win32k.md says how each kind of message is treated.
 */
#include "win32k.h"
#include <mime/msgtable.h>

/* the largest block copied for a message, in bytes. a program asking for more is refused. */
#define MSG_MAX_BLOCK (64u << 20)
/* the characters a text query is given room for, whatever its buffer size claims. */
#define MSG_MAX_TEXT (1u << 22)

#define RULE(class, layout, size) ((MSG_RULE){(class), (layout), (size)})

/* ------------------------------------------------------------------ which message means what */

/*
 * the messages whose parameters are more than numbers, in the order of their numbers. a message that is not here is
 * a plain value unless the window manager keeps it for itself. `Wide` marks a structure with fields as wide as a
 * pointer, which a 32-bit program holds in another layout.
 */
typedef struct MSG_ENTRY {
    USHORT Msg;
    UCHAR Class;
    UCHAR Wide;
    USHORT Size; /* bytes of a structure of fixed size, in the layout of a 64-bit program */
} MSG_ENTRY;

static const MSG_ENTRY g_classified[] = {
    {0x0001, MC_CREATE, 0, 0},         /* WM_CREATE */
    {0x000C, MC_STRING_IN, 0, 0},      /* WM_SETTEXT */
    {0x000D, MC_STRING_OUT, 0, 0},     /* WM_GETTEXT */
    {0x001A, MC_STRING_IN, 0, 0},      /* WM_SETTINGCHANGE, the name of the section */
    {0x001B, MC_STRING_IN, 0, 0},      /* WM_DEVMODECHANGE, the name of the device */
    {0x0024, MC_STRUCT_INOUT, 0, 40},  /* WM_GETMINMAXINFO, five points */
    {0x002B, MC_STRUCT_IN, 1, 64},     /* WM_DRAWITEM */
    {0x002C, MC_STRUCT_INOUT, 1, 32},  /* WM_MEASUREITEM, the owner fills in the size */
    {0x002D, MC_STRUCT_IN, 1, 32},     /* WM_DELETEITEM */
    {0x0038, MC_WINHELP, 0, 0},        /* the help request of the old help engine */
    {0x0039, MC_STRUCT_IN, 1, 56},     /* WM_COMPAREITEM */
    {0x0046, MC_STRUCT_INOUT, 1, 40},  /* WM_WINDOWPOSCHANGING, the receiver may change it */
    {0x0047, MC_STRUCT_IN, 1, 40},     /* WM_WINDOWPOSCHANGED */
    {0x0049, MC_COPYGLOBAL, 0, 0},     /* a copy of global memory for a window of another process */
    {0x004A, MC_COPYDATA, 1, 0},       /* WM_COPYDATA, a 32-bit program has a block with narrow fields */
    {0x0053, MC_HELP, 1, 40},          /* WM_HELP, says how large it is */
    {0x007C, MC_STRUCT_INOUT, 0, 8},   /* WM_STYLECHANGING */
    {0x007D, MC_STRUCT_INOUT, 0, 8},   /* WM_STYLECHANGED */
    {0x0081, MC_CREATE, 0, 0},         /* WM_NCCREATE */
    {0x0083, MC_NCCALCSIZE, 1, 0},     /* WM_NCCALCSIZE, a rectangle or the parameters with a position */
    {0x0087, MC_OPTMSG, 1, 48},        /* WM_GETDLGCODE, a message of the key being tested, or none */
    {0x008B, MC_STRUCT_INOUT, 0, 16},  /* a rectangle, undocumented */
    {0x00B0, MC_PTR2, 0, 0},           /* EM_GETSEL */
    {0x00B2, MC_STRUCT_OUT, 0, 16},    /* EM_GETRECT */
    {0x00B3, MC_STRUCT_INOUT, 0, 16},  /* EM_SETRECT */
    {0x00B4, MC_STRUCT_INOUT, 0, 16},  /* EM_SETRECTNP */
    {0x00C2, MC_STRING_IN, 0, 0},      /* EM_REPLACESEL */
    {0x00C4, MC_LINE_OUT, 0, 0},       /* EM_GETLINE, the room is the first word of the buffer */
    {0x00CB, MC_UINTS_IN, 0, 0},       /* EM_SETTABSTOPS */
    {0x00E3, MC_PTR2, 0, 0},           /* SBM_GETRANGE */
    {0x00E9, MC_STRUCT_INOUT, 0, 28},  /* SBM_SETSCROLLINFO */
    {0x00EA, MC_STRUCT_INOUT, 0, 28},  /* SBM_GETSCROLLINFO */
    {0x00EB, MC_STRUCT_INOUT, 0, 60},  /* SBM_GETSCROLLBARINFO */
    {0x0124, MC_STRUCT_INOUT, 1, 32},  /* WM_MENUGETOBJECT */
    {0x0140, MC_PTR2, 0, 0},           /* CB_GETEDITSEL */
    {0x0143, MC_ITEM_CB, 0, 0},        /* CB_ADDSTRING */
    {0x0145, MC_STRING_IN, 0, 0},      /* CB_DIR, the file pattern */
    {0x0148, MC_ITEM_OUT, 0, 0},       /* CB_GETLBTEXT */
    {0x014A, MC_ITEM_CB, 0, 0},        /* CB_INSERTSTRING */
    {0x014C, MC_ITEM_CB, 0, 0},        /* CB_FINDSTRING */
    {0x014D, MC_ITEM_CB, 0, 0},        /* CB_SELECTSTRING */
    {0x0152, MC_STRUCT_OUT, 0, 16},    /* CB_GETDROPPEDCONTROLRECT */
    {0x0158, MC_ITEM_CB, 0, 0},        /* CB_FINDSTRINGEXACT */
    {0x0164, MC_STRUCT_INOUT, 1, 64},  /* CB_GETCOMBOBOXINFO */
    {0x0180, MC_ITEM_LB, 0, 0},        /* LB_ADDSTRING */
    {0x0181, MC_ITEM_LB, 0, 0},        /* LB_INSERTSTRING */
    {0x0189, MC_ITEM_OUT, 0, 0},       /* LB_GETTEXT */
    {0x018C, MC_ITEM_LB, 0, 0},        /* LB_SELECTSTRING */
    {0x018D, MC_STRING_IN, 0, 0},      /* LB_DIR, the file pattern */
    {0x018F, MC_ITEM_LB, 0, 0},        /* LB_FINDSTRING */
    {0x0191, MC_UINTS_OUT, 0, 0},      /* LB_GETSELITEMS */
    {0x0192, MC_UINTS_IN, 0, 0},       /* LB_SETTABSTOPS */
    {0x0196, MC_STRING_IN, 0, 0},      /* LB_ADDFILE */
    {0x0198, MC_STRUCT_INOUT, 0, 16},  /* LB_GETITEMRECT, the index goes in with the rectangle */
    {0x01A2, MC_ITEM_LB, 0, 0},        /* LB_FINDSTRINGEXACT */
    {0x01AA, MC_ITEM_LB, 0, 0},        /* LB_INSERTSTRING_UPPER, undocumented */
    {0x01AB, MC_ITEM_LB, 0, 0},        /* LB_INSERTSTRING_LOWER, undocumented */
    {0x01AC, MC_ITEM_LB, 0, 0},        /* LB_ADDSTRING_UPPER, undocumented */
    {0x01AD, MC_ITEM_LB, 0, 0},        /* LB_ADDSTRING_LOWER, undocumented */
    {0x01EB, MC_WPTR, 0, 0},           /* undocumented, answers through wparam */
    {0x0213, MC_STRUCT_INOUT, 1, 24},  /* WM_NEXTMENU */
    {0x0214, MC_STRUCT_INOUT, 0, 16},  /* WM_SIZING */
    {0x0216, MC_STRUCT_INOUT, 0, 16},  /* WM_MOVING */
    {0x0218, MC_POWER, 0, 0},          /* WM_POWERBROADCAST */
    {0x0219, MC_DEVCHANGE, 0, 0},      /* WM_DEVICECHANGE */
    {0x0220, MC_MDICREATE, 1, 0},      /* WM_MDICREATE, with a class, a title and an owner */
    {0x0229, MC_PTR2, 0, 0},           /* WM_MDIGETACTIVE, both parameters may point at an answer */
    {0x022A, MC_STRUCT_INOUT, 1, 48},  /* undocumented, a structure of drops */
    {0x022B, MC_STRUCT_INOUT, 1, 48},  /* undocumented, a structure of drops */
    {0x022D, MC_STRUCT_INOUT, 1, 48},  /* undocumented, a structure of drops */
    {0x022E, MC_STRUCT_INOUT, 1, 48},  /* undocumented, a structure of drops */
    {0x022F, MC_STRUCT_INOUT, 1, 48},  /* undocumented, a structure of drops */
    {0x02E0, MC_STRUCT_INOUT, 0, 16},  /* WM_DPICHANGED, the suggested rectangle */
    {0x02E4, MC_STRUCT_INOUT, 0, 8},   /* WM_GETDPISCALEDSIZE, a size */
    {0x030C, MC_FORMAT_OUT, 0, 0},     /* WM_ASKCBFORMATNAME */
    {0x033F, MC_TITLEBAR, 0, 140},     /* WM_GETTITLEBARINFOEX, says how large it is */
};

MSG_RULE MsgRuleFor(UINT msg)
{
    if (msg >= 0x400)
        return RULE(MC_VALUE, 0, 0);
    ULONG low = 0, high = RTL_NUMBER_OF(g_classified);
    while (low < high) {
        ULONG middle = (low + high) / 2;
        if (g_classified[middle].Msg < msg)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < RTL_NUMBER_OF(g_classified) && g_classified[low].Msg == msg)
        return RULE(g_classified[low].Class, g_classified[low].Wide, g_classified[low].Size);
    return RULE(MimeMsgKernelOnly(msg) ? MC_KERNEL : MC_VALUE, 0, 0);
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
        /* wparam characters of room: more than the limit is refused with no error, and the format name needs room. */
        if (wparam > 0x3FFFFFFF) {
            W32SetLastError(0);
            return FALSE;
        }
        if (rule.Class == MC_FORMAT_OUT && wparam < 1) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (wparam && !lparam)
            return FALSE;
        if (!probe_buffer(lparam, (ULONG)wparam * sizeof(WCHAR)))
            return FALSE;
        ULONG room = wparam < MSG_MAX_TEXT ? (ULONG)wparam : MSG_MAX_TEXT;
        c->Wparam = room;
        c->Capacity = (ULONG)wparam;
        return capture_text_room(c, room);
    }
    case MC_LINE_OUT: {
        /* the first word of the buffer is the room, in characters, and the line is written over it. */
        USHORT room;
        if (!lparam || !user_read(&room, lparam, sizeof(room)) || room < 1 || !probe_buffer(lparam, room * 2u))
            return FALSE;
        if (!capture_text_room(c, room))
            return FALSE;
        *(USHORT *)c->Buffer = room;
        c->Capacity = room;
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
    case MC_TITLEBAR:
    case MC_HELP: {
        /* a structure that says how large it is, and has to say it right. */
        DWORD claimed;
        if (!lparam || !user_read(&claimed, lparam, sizeof(claimed)))
            return FALSE;
        if (claimed != rule.Size) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return capture_struct(c, lparam, rule.Size, rule.Class == MC_HELP ? MIME_LPARAM_IN : MIME_LPARAM_INOUT, TRUE);
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
        /* a change of the device that carries a structure: its first field is how many bytes it is. */
        if (!lparam) {
            if (wparam == 0x8000 || wparam == 0x8004 || (wparam >= 0x8007 && wparam <= 0x800A)) {
                W32SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            return TRUE;
        }
        if (!(wparam & 0x8000))
            return TRUE;
        DWORD size;
        if (!user_read(&size, lparam, sizeof(size)))
            return FALSE;
        if (size < 12 || size > MSG_MAX_BLOCK - 2) {
            W32SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        /* two zero bytes follow, so that a name in the structure always ends. */
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
        /* only the low half of the first field counts as the size, and the copy keeps it as its first word. */
        DWORD first;
        if (!lparam || !user_read(&first, lparam, sizeof(first)))
            return FALSE;
        USHORT size = (USHORT)first;
        if (size < 0x18)
            return FALSE;
        if (!capture_struct(c, lparam, size, MIME_LPARAM_IN, TRUE))
            return FALSE;
        *(USHORT *)c->Buffer = size;
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
        /* the text written is cut to what the result says and to the room the sender really has. */
        LRESULT n = *result;
        ULONG room = (ULONG)c->UserWparam;
        if (n <= 0) {
            if (room) {
                WCHAR end = 0;
                user_write(c->UserLparam, &end, sizeof(end));
            }
            if (n < 0)
                *result = 0;
            return;
        }
        if (!room)
            return;
        ULONG limit = (ULONG)((ULONG_PTR)n < c->Chars ? (ULONG_PTR)n : c->Chars);
        ULONG got = text_chars(c->Buffer, limit);
        user_write(c->UserLparam, c->Buffer, (SIZE_T)got * sizeof(WCHAR));
        if (c->Capacity > got) {
            WCHAR end = 0;
            user_write(c->UserLparam + (SIZE_T)got * sizeof(WCHAR), &end, sizeof(end));
        }
        *result = got;
        return;
    }
    case MC_LINE_OUT: {
        /* the characters of the line come back without an end, and a line of none leaves an empty string. */
        LRESULT n = *result;
        if (n <= 0) {
            WCHAR end = 0;
            user_write(c->UserLparam, &end, sizeof(end));
            return;
        }
        ULONG count = (ULONG)((ULONG_PTR)n < c->Capacity ? (ULONG_PTR)n : c->Capacity);
        user_write(c->UserLparam, c->Buffer, (SIZE_T)count * sizeof(WCHAR));
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
