#include <mime/ntrtl.h>
#include "wow32_callback.h"

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

static void windowpos32(WINDOWPOS32 *out, const WINDOWPOS *in)
{
    *out = (WINDOWPOS32){(ULONG)(ULONG_PTR)in->hwnd, (ULONG)(ULONG_PTR)in->hwndInsertAfter,
                        in->x, in->y, in->cx, in->cy, in->flags};
}

static void windowpos64(WINDOWPOS *out, const WINDOWPOS32 *in)
{
    *out = (WINDOWPOS){(HWND)(ULONG_PTR)in->Hwnd, (HWND)(LONG_PTR)(LONG)in->After,
                      in->X, in->Y, in->Cx, in->Cy, in->Flags};
}

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

typedef struct MSG32 {
    ULONG Hwnd, Message, Wparam, Lparam, Time;
    POINT Pt;
    ULONG Private;
} MSG32;

static ULONG low(ULONG_PTR v)
{
    return (ULONG)v;
}

/* the structure of a hook in its x86 layout, its size; 0 for one it does not know. */
static ULONG hook_data32(int id, const UCHAR *in, ULONG size, UCHAR *out)
{
    switch (id) {
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
        return sizeof(v);
    }
    case WH_CALLWNDPROCRET: {
        const CWPRETSTRUCT *c = (const void *)in;
        if (size < sizeof(*c))
            return 0;
        ULONG v[5] = {low(c->lResult), low(c->lParam), low(c->wParam), c->message, low((ULONG_PTR)c->hwnd)};
        memcpy(out, v, sizeof(v));
        return sizeof(v);
    }
    case WH_GETMESSAGE: {
        const MSG *m = (const void *)in;
        if (size < sizeof(*m))
            return 0;
        MSG32 v = {low((ULONG_PTR)m->hwnd), m->message, low(m->wParam), low(m->lParam), m->time, m->pt, m->lPrivate};
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

/* what a hook changed, back into the kernel's copy: a message alone can be. */
static void hook_data64(int id, const UCHAR *in, ULONG size, UCHAR *out, ULONG out_size)
{
    if (id != WH_GETMESSAGE || size < sizeof(MSG32) || out_size < sizeof(MSG))
        return;
    const MSG32 *m = (const void *)in;
    *(MSG *)out = (MSG){(HWND)(ULONG_PTR)m->Hwnd, m->Message, m->Wparam, (LPARAM)(LONG)m->Lparam, m->Time, m->Pt,
                        m->Private};
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

static BOOLEAN marshal_hook(ULONG *stack, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    if (length < sizeof(MIME_HOOKPROC_ARGS))
        return FALSE;
    const MIME_HOOKPROC_ARGS *in = arguments;
    if (in->DataSize > length - sizeof(*in))
        return FALSE;
    *stack = (*stack - sizeof(HOOKPROC32) - 64 - 32) & ~15u;
    HOOKPROC32 *out = (void *)(ULONG_PTR)*stack;
    *out = (HOOKPROC32){low((ULONG_PTR)in->Proc), in->Id, in->Code, low(in->Wparam), low(in->Lparam), in->Ansi, 0};
    if (in->DataSize) {
        out->DataSize = hook_data32(in->Id, in->Data, in->DataSize, out->Data);
        /* an unknown structure would reach the hook in the wrong layout: it gets none. */
        if (!out->DataSize)
            out->Lparam = 0;
    }
    callback->Guest = out;
    callback->Length = sizeof(*out) + out->DataSize;
    return TRUE;
}

BOOLEAN Mx32MarshalCallback(ULONG *stack, ULONG api, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    memset(callback, 0, sizeof(*callback));
    callback->Api = api;
    callback->Native = arguments;
    if (api == MIME_CB_TIMERPROC)
        return marshal_timer(stack, arguments, length, callback);
    if (api == MIME_CB_HOOKPROC)
        return marshal_hook(stack, arguments, length, callback);
    if (api != MIME_CB_WINDOWPROC || length < sizeof(MIME_WINDOWPROC_ARGS))
        return FALSE;
    MIME_WINDOWPROC_ARGS *in = arguments;
    if (in->DataSize > length - sizeof(*in))
        return FALSE;
    SIZE_T size = sizeof(WINDOWPROC32) + in->DataSize + 32;
    *stack = (ULONG)((*stack - size) & ~15u);
    WINDOWPROC32 *out = (void *)(ULONG_PTR)*stack;
    *out = (WINDOWPROC32){(ULONG)(ULONG_PTR)in->Proc, (ULONG)(ULONG_PTR)in->Hwnd,
                         in->Msg, in->LparamKind, (ULONG)in->Wparam, (ULONG)in->Lparam, in->Ansi, in->DataSize};
    memcpy(out->Data, in->Data, in->DataSize);
    if (in->LparamKind == MIME_LPARAM_CREATESTRUCT && in->DataSize >= sizeof(CREATESTRUCTW)) {
        const CREATESTRUCTW *cs = (const void *)in->Data;
        CREATESTRUCT32 *small = (void *)out->Data;
        *small = (CREATESTRUCT32){(ULONG)(ULONG_PTR)cs->lpCreateParams, (ULONG)(ULONG_PTR)cs->hInstance,
                                 (ULONG)(ULONG_PTR)cs->hMenu, (ULONG)(ULONG_PTR)cs->hwndParent,
                                 cs->cy, cs->cx, cs->y, cs->x, cs->style,
                                 (ULONG)(ULONG_PTR)cs->lpszName, (ULONG)(ULONG_PTR)cs->lpszClass, cs->dwExStyle};
    } else if ((in->Msg == WM_WINDOWPOSCHANGING || in->Msg == WM_WINDOWPOSCHANGED) && in->DataSize >= sizeof(WINDOWPOS)) {
        windowpos32((void *)out->Data, (const void *)in->Data);
        out->DataSize = sizeof(WINDOWPOS32);
    } else if (in->Msg == WM_NCCALCSIZE && in->Wparam && in->DataSize >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)) {
        PULONG pos = (void *)(out->Data + 3 * sizeof(RECT));
        *pos = (ULONG)(ULONG_PTR)(pos + 1);
        windowpos32((void *)(pos + 1), (const void *)(in->Data + sizeof(NCCALCSIZE_PARAMS)));
        out->DataSize = 3 * sizeof(RECT) + 4 + sizeof(WINDOWPOS32);
    }
    callback->Guest = out;
    callback->Length = sizeof(*out) + out->DataSize;
    return TRUE;
}

PVOID Mx32CallbackResult(const MX_CALLBACK32 *callback, PVOID result, ULONG *length)
{
    if (*length < 12)
        return nullptr;
    const ULONG *small = result;
    if (callback->Api == MIME_CB_TIMERPROC) {
        /* the arguments are done with; the result takes their place. */
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
            hook_data64(args->Id, (const UCHAR *)(small + 3), bytes, args->Data, args->DataSize);
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
    ULONG bytes = small[1];
    if (bytes > *length - 12)
        return nullptr;
    if (bytes && args->LparamKind == MIME_LPARAM_INOUT) {
        const UCHAR *data = (const void *)(small + 3);
        if ((args->Msg == WM_WINDOWPOSCHANGING || args->Msg == WM_WINDOWPOSCHANGED) && bytes >= sizeof(WINDOWPOS32)) {
            windowpos64((void *)args->Data, (const void *)data);
        } else if (args->Msg == WM_NCCALCSIZE && args->Wparam && bytes >= 3 * sizeof(RECT) + 4 + sizeof(WINDOWPOS32)) {
            memcpy(args->Data, data, 3 * sizeof(RECT));
            windowpos64((void *)(args->Data + sizeof(NCCALCSIZE_PARAMS)), (const void *)(data + 3 * sizeof(RECT) + 4));
        } else {
            memcpy(args->Data, data, bytes < args->DataSize ? bytes : args->DataSize);
        }
        bytes = args->DataSize;
    } else {
        bytes = 0;
    }
    MIME_CALLBACK_RESULT *out = (void *)(args->Data - sizeof(*out));
    out->Result = (LONG)small[0];
    out->DataSize = bytes;
    out->Pad = 0;
    *length = sizeof(*out) + bytes;
    return out;
}
