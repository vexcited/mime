#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include "wow32_callback.h"
#include "wow32_layout.h"

typedef struct WINDOWPROC32 {
    ULONG Proc, Hwnd, Msg, LparamKind, Wparam, Lparam, Ansi, DataSize;
    UCHAR Data[];
} WINDOWPROC32;

typedef struct CREATESTRUCT32 {
    ULONG Param, Instance, Menu, Parent;
    LONG Cy, Cx, Y, X, Style;
    ULONG Name, Class, ExStyle;
} CREATESTRUCT32;

typedef struct WINDOWPOS32 {
    ULONG Hwnd, After;
    int X, Y, Cx, Cy;
    UINT Flags;
} WINDOWPOS32;

typedef struct TIMERPROC32 {
    ULONG Proc, Hwnd, Msg, Id, Time;
} TIMERPROC32;

typedef struct HOOKPROC32 {
    ULONG Proc;
    int Id;
    int Code;
    ULONG Wparam, Lparam, Ansi, DataSize;
    UCHAR Data[];
} HOOKPROC32;

/* the x86 MSG has no private word, so it is 28 bytes. */
typedef struct MSG32 {
    ULONG Hwnd, Message, Wparam, Lparam, Time;
    POINT Pt;
} MSG32;

static ULONG low(ULONG_PTR v)
{
    return (ULONG)v;
}

/* converts hook data to the x86 layout and returns its size, or 0 for an unknown hook. */
/* the x86 CBT_CREATEWND and the CREATESTRUCT after it. */
typedef struct CBTCREATE32 {
    ULONG Lpcs, After;
    CREATESTRUCT32 Cs;
} CBTCREATE32;

static ULONG cbt_data32(int code, const UCHAR *in, ULONG size, UCHAR *out)
{
    switch (code) {
    case HCBT_CREATEWND: {
        const CBT_CREATEWNDW *cbt = (const void *)in;
        const CREATESTRUCTW *cs = (const void *)(cbt + 1);
        if (size < sizeof(*cbt) + sizeof(*cs))
            return 0;
        ULONG shift = sizeof(CREATESTRUCTW) - sizeof(CREATESTRUCT32);
        CBTCREATE32 *small = (void *)out;
        *small = (CBTCREATE32){0, low((ULONG_PTR)cbt->hwndInsertAfter),
                               {low((ULONG_PTR)cs->lpCreateParams), low((ULONG_PTR)cs->hInstance),
                                low((ULONG_PTR)cs->hMenu), low((ULONG_PTR)cs->hwndParent), cs->cy, cs->cx, cs->y, cs->x,
                                cs->style, cs->lpszName ? low((ULONG_PTR)cs->lpszName) - shift : 0,
                                cs->lpszClass ? low((ULONG_PTR)cs->lpszClass) - shift : 0, cs->dwExStyle}};
        ULONG strings = size - sizeof(*cbt) - sizeof(*cs);
        memcpy(small + 1, cs + 1, strings);
        return sizeof(*small) + strings;
    }
    case HCBT_ACTIVATE: {
        const CBTACTIVATESTRUCT *a = (const void *)in;
        if (size < sizeof(*a))
            return 0;
        ULONG v[2] = {(ULONG)a->fMouse, low((ULONG_PTR)a->hWndActive)};
        memcpy(out, v, sizeof(v));
        return sizeof(v);
    }
    case HCBT_MOVESIZE:
        if (size < sizeof(RECT))
            return 0;
        memcpy(out, in, sizeof(RECT));
        return sizeof(RECT);
    }
    return 0;
}

/*
 * the data a message's lparam points at, as a window procedure hook sees it after the message: in the x86 layout, and
 * none for the structures that have no conversion here.
 */
static ULONG cwp_payload32(UINT msg, ULONG_PTR wparam, const UCHAR *in, ULONG size, UCHAR *out)
{
    if (!size || msg == WM_COPYDATA || msg == WM_MDICREATE)
        return 0;
    ULONG fixed = lay_size32(msg);
    if (fixed) {
        if (size < lay_size64(msg))
            return 0;
        lay_to32(msg, in, out);
        return fixed;
    }
    if (msg == WM_NCCALCSIZE && wparam && size >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)) {
        memcpy(out, in, 3 * sizeof(RECT));
        PULONG pos = (void *)(out + 3 * sizeof(RECT));
        *pos = 0;
        lay_to32(WM_WINDOWPOSCHANGING, in + sizeof(NCCALCSIZE_PARAMS), pos + 1);
        return 3 * sizeof(RECT) + 4 + sizeof(WINDOWPOS32);
    }
    if (msg == WM_NCCALCSIZE)
        return 0;
    memcpy(out, in, size);
    return size;
}

static ULONG hook_data32(int id, int code, const UCHAR *in, ULONG size, UCHAR *out)
{
    switch (id) {
    case WH_CBT:
        return cbt_data32(code, in, size, out);
    case WH_KEYBOARD_LL: {
        const KBDLLHOOKSTRUCT *k = (const void *)in;
        if (size < sizeof(*k))
            return 0;
        ULONG v[5] = {k->vkCode, k->scanCode, k->flags, k->time, low(k->dwExtraInfo)};
        memcpy(out, v, sizeof(v));
        return sizeof(v);
    }
    case WH_MOUSE_LL: {
        const MSLLHOOKSTRUCT *m = (const void *)in;
        if (size < sizeof(*m))
            return 0;
        ULONG v[6] = {(ULONG)m->pt.x, (ULONG)m->pt.y, m->mouseData, m->flags, m->time, low(m->dwExtraInfo)};
        memcpy(out, v, sizeof(v));
        return sizeof(v);
    }
    case WH_CALLWNDPROC: {
        const CWPSTRUCT *c = (const void *)in;
        if (size < sizeof(*c))
            return 0;
        ULONG v[4] = {low(c->lParam), low(c->wParam), c->message, low((ULONG_PTR)c->hwnd)};
        memcpy(out, v, sizeof(v));
        return sizeof(v) + cwp_payload32(c->message, c->wParam, in + sizeof(*c), size - sizeof(*c), out + sizeof(v));
    }
    case WH_CALLWNDPROCRET: {
        const CWPRETSTRUCT *c = (const void *)in;
        if (size < sizeof(*c))
            return 0;
        ULONG v[5] = {low(c->lResult), low(c->lParam), low(c->wParam), c->message, low((ULONG_PTR)c->hwnd)};
        memcpy(out, v, sizeof(v));
        return sizeof(v) + cwp_payload32(c->message, c->wParam, in + sizeof(*c), size - sizeof(*c), out + sizeof(v));
    }
    case WH_GETMESSAGE: {
        const MSG *m = (const void *)in;
        if (size < sizeof(*m))
            return 0;
        MSG32 v = {low((ULONG_PTR)m->hwnd), m->message, low(m->wParam), low(m->lParam), m->time, m->pt};
        memcpy(out, &v, sizeof(v));
        return sizeof(v);
    }
    case WH_MOUSE: {
        const MOUSEHOOKSTRUCTEX *m = (const void *)in;
        if (size < sizeof(*m))
            return 0;
        ULONG v[6] = {(ULONG)m->pt.x, (ULONG)m->pt.y, low((ULONG_PTR)m->hwnd), m->wHitTestCode, low(m->dwExtraInfo),
                      m->mouseData};
        memcpy(out, v, sizeof(v));
        return sizeof(v);
    }
    default:
        return 0;
    }
}

/* copies what the hook changed back into the kernel's copy: a message, a window's place or a rectangle. */
static void hook_data64(int id, int code, const UCHAR *in, ULONG size, UCHAR *out, ULONG out_size)
{
    if (id == WH_CBT && code == HCBT_CREATEWND && size >= sizeof(CBTCREATE32) &&
        out_size >= sizeof(CBT_CREATEWNDW) + sizeof(CREATESTRUCTW)) {
        const CBTCREATE32 *small = (const void *)in;
        CBT_CREATEWNDW *cbt = (void *)out;
        CREATESTRUCTW *cs = (void *)(cbt + 1);
        cbt->hwndInsertAfter = lay_handle(small->After);
        cs->x = small->Cs.X, cs->y = small->Cs.Y, cs->cx = small->Cs.Cx, cs->cy = small->Cs.Cy;
        return;
    }
    if (id == WH_CBT && code == HCBT_MOVESIZE && size >= sizeof(RECT) && out_size >= sizeof(RECT)) {
        memcpy(out, in, sizeof(RECT));
        return;
    }
    if (id != WH_GETMESSAGE || size < sizeof(MSG32) || out_size < sizeof(MSG))
        return;
    const MSG32 *m = (const void *)in;
    *(MSG *)out = (MSG){(HWND)(ULONG_PTR)m->Hwnd, m->Message, m->Wparam, (LPARAM)(LONG)m->Lparam, m->Time, m->Pt,
                        0};
}

static BOOLEAN marshal_timer(ULONG *stack, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    if (length < sizeof(MIME_TIMERPROC_ARGS))
        return FALSE;
    const MIME_TIMERPROC_ARGS *in = arguments;
    *stack = (*stack - sizeof(TIMERPROC32) - 32) & ~15u;
    TIMERPROC32 *out = (void *)(ULONG_PTR)*stack;
    *out = (TIMERPROC32){low((ULONG_PTR)in->Proc), low((ULONG_PTR)in->Hwnd), in->Msg, low(in->Id), in->Time};
    callback->Guest = out;
    callback->Length = sizeof(*out);
    return TRUE;
}

typedef struct SENDASYNC32 {
    ULONG Proc, Hwnd, Msg, Data, Result;
} SENDASYNC32;

static BOOLEAN marshal_sendasync(ULONG *stack, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    if (length < sizeof(MIME_SENDASYNC_ARGS))
        return FALSE;
    const MIME_SENDASYNC_ARGS *in = arguments;
    *stack = (*stack - sizeof(SENDASYNC32) - 32) & ~15u;
    SENDASYNC32 *out = (void *)(ULONG_PTR)*stack;
    *out = (SENDASYNC32){low((ULONG_PTR)in->Proc), low((ULONG_PTR)in->Hwnd), in->Msg, low(in->Data), low((ULONG_PTR)in->Result)};
    callback->Guest = out;
    callback->Length = sizeof(*out);
    return TRUE;
}

static BOOLEAN marshal_hook(ULONG *stack, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    if (length < sizeof(MIME_HOOKPROC_ARGS))
        return FALSE;
    const MIME_HOOKPROC_ARGS *in = arguments;
    if (in->DataSize > length - sizeof(*in))
        return FALSE;
    /* the x86 data is never larger than the native one. */
    *stack = (*stack - sizeof(HOOKPROC32) - (in->DataSize > 64 ? in->DataSize : 64) - 32) & ~15u;
    HOOKPROC32 *out = (void *)(ULONG_PTR)*stack;
    *out = (HOOKPROC32){low((ULONG_PTR)in->Proc), in->Id, in->Code, low(in->Wparam), low(in->Lparam), in->Ansi, 0};
    if (in->DataSize) {
        out->DataSize = hook_data32(in->Id, in->Code, in->Data, in->DataSize, out->Data);
        /* an unknown structure would reach the hook in the wrong layout, so it gets none. */
        if (!out->DataSize)
            out->Lparam = 0;
    }
    callback->Guest = out;
    callback->Length = sizeof(*out) + out->DataSize;
    return TRUE;
}

/* memory below 4 GiB for data too large for the stack. */
static PVOID block_allocate(SIZE_T size)
{
    PVOID base = nullptr;
    return NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE)) &&
                   (ULONG_PTR)base + size <= 0xFFFFFFFFull
               ? base
               : nullptr;
}

static void block_release(PVOID base)
{
    SIZE_T size = 0;
    if (base)
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}

/* WM_COPYDATA larger than this goes in a separate block. its lpData becomes where the data starts. */
#define INLINE_MAX 0x8000

BOOLEAN Mx32MarshalCallback(ULONG *stack, ULONG api, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    memset(callback, 0, sizeof(*callback));
    callback->Api = api;
    callback->Native = arguments;
    if (api == MIME_CB_TIMERPROC)
        return marshal_timer(stack, arguments, length, callback);
    if (api == MIME_CB_HOOKPROC)
        return marshal_hook(stack, arguments, length, callback);
    if (api == MIME_CB_SENDASYNC)
        return marshal_sendasync(stack, arguments, length, callback);
    if (api != MIME_CB_WINDOWPROC || length < sizeof(MIME_WINDOWPROC_ARGS))
        return FALSE;
    MIME_WINDOWPROC_ARGS *in = arguments;
    ULONG kind = MIME_LPARAM_KIND(in->LparamKind);
    BOOLEAN external = (in->LparamKind & MIME_LPARAM_EXTERNAL) != 0;
    const UCHAR *source = external ? (const UCHAR *)in->Lparam : in->Data;
    if (!external && in->DataSize > length - sizeof(*in))
        return FALSE;
    ULONG bytes = in->DataSize;
    /* the 32-bit size of the structure, which differs from the one win32k sent. */
    ULONG fixed = kind == MIME_LPARAM_IN || kind == MIME_LPARAM_INOUT ? lay_size32(in->Msg) : 0;
    if (fixed && bytes < lay_size64(in->Msg))
        return FALSE;
    BOOLEAN copydata = in->Msg == WM_COPYDATA && kind == MIME_LPARAM_IN && bytes >= sizeof(COPYDATASTRUCT);
    BOOLEAN mdi = in->Msg == WM_MDICREATE && kind == MIME_LPARAM_IN && bytes >= sizeof(MDICREATESTRUCTW);
    ULONG size32 = bytes;
    if (fixed)
        size32 = fixed;
    else if (copydata)
        size32 = bytes - sizeof(COPYDATASTRUCT) + 12;
    else if (mdi)
        size32 = bytes - sizeof(MDICREATESTRUCTW) + 36;
    else if (in->Msg == WM_NCCALCSIZE && kind == MIME_LPARAM_INOUT && in->Wparam && bytes >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS))
        size32 = 3 * sizeof(RECT) + 4 + sizeof(WINDOWPOS32);
    /* a block too large for the stack stays where win32k put it, unless it needs converting. */
    BOOLEAN keep_external = external && !copydata && !fixed && !mdi;
    if (external && !keep_external && !copydata)
        return FALSE;
    PVOID outside = nullptr;
    if (copydata && size32 > INLINE_MAX) {
        outside = block_allocate(size32);
        if (!outside)
            return FALSE;
    }
    SIZE_T size = sizeof(WINDOWPROC32) + (outside || keep_external ? 0 : size32) + 32;
    *stack = (ULONG)((*stack - size) & ~15u);
    WINDOWPROC32 *out = (void *)(ULONG_PTR)*stack;
    UCHAR *target = outside ? outside : out->Data;
    *out = (WINDOWPROC32){(ULONG)(ULONG_PTR)in->Proc, (ULONG)(ULONG_PTR)in->Hwnd, in->Msg, in->LparamKind,
                          (ULONG)in->Wparam, (ULONG)in->Lparam, in->Ansi, size32};
    if (outside) {
        out->LparamKind = kind | MIME_LPARAM_EXTERNAL;
        out->Lparam = (ULONG)(ULONG_PTR)outside;
        callback->External = outside;
    } else if (keep_external) {
        out->Lparam = (ULONG)in->Lparam;
        out->DataSize = bytes;
    } else {
        out->LparamKind = kind;
    }
    if (kind == MIME_LPARAM_CREATESTRUCT && bytes >= sizeof(CREATESTRUCTW)) {
        memcpy(out->Data, source, bytes);
        const CREATESTRUCTW *cs = (const void *)source;
        CREATESTRUCT32 *small = (void *)out->Data;
        *small = (CREATESTRUCT32){(ULONG)(ULONG_PTR)cs->lpCreateParams, (ULONG)(ULONG_PTR)cs->hInstance,
                                 (ULONG)(ULONG_PTR)cs->hMenu, (ULONG)(ULONG_PTR)cs->hwndParent,
                                 cs->cy, cs->cx, cs->y, cs->x, cs->style,
                                 (ULONG)(ULONG_PTR)cs->lpszName, (ULONG)(ULONG_PTR)cs->lpszClass, cs->dwExStyle};
    } else if (fixed) {
        lay_to32(in->Msg, source, target);
    } else if (copydata) {
        const COPYDATASTRUCT *cd = (const void *)source;
        ULONG *small = (void *)target;
        small[0] = (ULONG)cd->dwData;
        small[1] = cd->cbData;
        small[2] = cd->lpData ? 12 : 0;
        memcpy(target + 12, source + sizeof(COPYDATASTRUCT), bytes - sizeof(COPYDATASTRUCT));
    } else if (mdi) {
        const MDICREATESTRUCTW *m = (const void *)source;
        ULONG *small = (void *)target;
        /* the strings follow the smaller 32-bit structure, so their offsets move up. */
        ULONG cls = (ULONG_PTR)m->szClass >= 0x10000 ? (ULONG)((ULONG_PTR)m->szClass - 20) : (ULONG)(ULONG_PTR)m->szClass;
        ULONG title = (ULONG_PTR)m->szTitle >= 0x10000 ? (ULONG)((ULONG_PTR)m->szTitle - 20) : (ULONG)(ULONG_PTR)m->szTitle;
        small[0] = cls;
        small[1] = title;
        small[2] = lay_narrow(m->hOwner);
        small[3] = m->x;
        small[4] = m->y;
        small[5] = m->cx;
        small[6] = m->cy;
        small[7] = m->style;
        small[8] = (ULONG)m->lParam;
        memcpy(target + 36, source + sizeof(MDICREATESTRUCTW), bytes - sizeof(MDICREATESTRUCTW));
    } else if (size32 != bytes && in->Msg == WM_NCCALCSIZE) {
        memcpy(out->Data, source, 3 * sizeof(RECT));
        PULONG pos = (void *)(out->Data + 3 * sizeof(RECT));
        *pos = (ULONG)(ULONG_PTR)(pos + 1);
        lay_to32(WM_WINDOWPOSCHANGING, source + sizeof(NCCALCSIZE_PARAMS), pos + 1);
    } else if (!keep_external && bytes) {
        memcpy(target, source, bytes);
    }
    callback->Guest = out;
    callback->Length = (ULONG)(sizeof(*out) + (outside || keep_external ? 0 : size32));
    return TRUE;
}

PVOID Mx32CallbackResult(const MX_CALLBACK32 *callback, PVOID result, ULONG *length)
{
    if (*length < 12)
        return nullptr;
    const ULONG *small = result;
    if (callback->Api == MIME_CB_TIMERPROC || callback->Api == MIME_CB_SENDASYNC) {
        /* the arguments are no longer needed, the result overwrites them. */
        MIME_CALLBACK_RESULT *out = callback->Native;
        *out = (MIME_CALLBACK_RESULT){(LONG)small[0], 0, 0};
        *length = sizeof(*out);
        return out;
    }
    if (callback->Api == MIME_CB_HOOKPROC) {
        MIME_HOOKPROC_ARGS *args = callback->Native;
        ULONG bytes = small[1];
        if (bytes > *length - 12)
            return nullptr;
        if (bytes)
            hook_data64(args->Id, args->Code, (const UCHAR *)(small + 3), bytes, args->Data, args->DataSize);
        /* the header overlaps the arguments it follows. */
        ULONG size = args->DataSize;
        MIME_CALLBACK_RESULT *out = (void *)(args->Data - sizeof(*out));
        out->Result = (LONG)small[0];
        out->DataSize = size;
        out->Pad = 0;
        *length = sizeof(*out) + size;
        return out;
    }
    MIME_WINDOWPROC_ARGS *args = callback->Native;
    ULONG kind = MIME_LPARAM_KIND(args->LparamKind);
    BOOLEAN external = (args->LparamKind & MIME_LPARAM_EXTERNAL) != 0;
    ULONG bytes = small[1];
    if (bytes > *length - 12)
        return nullptr;
    BOOLEAN writes = kind == MIME_LPARAM_INOUT || kind == MIME_LPARAM_PTR2 || kind == MIME_LPARAM_WPTR;
    if (bytes && writes && !external) {
        const UCHAR *data = (const void *)(small + 3);
        ULONG fixed = kind == MIME_LPARAM_INOUT ? lay_size32(args->Msg) : 0;
        if (fixed && lay_writes(args->Msg) && bytes >= fixed) {
            lay_to64(args->Msg, data, args->Data);
        } else if (args->Msg == WM_NCCALCSIZE && args->Wparam && bytes >= 3 * sizeof(RECT) + 4 + sizeof(WINDOWPOS32)) {
            memcpy(args->Data, data, 3 * sizeof(RECT));
            lay_to64(WM_WINDOWPOSCHANGING, data + 3 * sizeof(RECT) + 4, args->Data + sizeof(NCCALCSIZE_PARAMS));
        } else if (!fixed) {
            memcpy(args->Data, data, bytes < args->DataSize ? bytes : args->DataSize);
        }
        bytes = args->DataSize;
    } else {
        bytes = 0;
    }
    block_release(callback->External);
    MIME_CALLBACK_RESULT *out = (void *)(args->Data - sizeof(*out));
    out->Result = (LONG)small[0];
    out->DataSize = bytes;
    out->Pad = 0;
    *length = sizeof(*out) + bytes;
    return out;
}
