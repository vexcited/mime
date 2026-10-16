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

BOOLEAN Mx32MarshalCallback(ULONG *stack, ULONG api, PVOID arguments, ULONG length, MX_CALLBACK32 *callback)
{
    memset(callback, 0, sizeof(*callback));
    callback->Api = api;
    callback->Native = arguments;
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
