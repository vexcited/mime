/*
 * the structures of window messages that hold a pointer or a handle, in the layout of 32-bit programs and in the
 * 64-bit one win32k carries, and the conversions between them. the system call side converts what a 32-bit program
 * sends, the callback side what a 32-bit window procedure gets and what it writes into it.
 */
#ifndef MIME_WOW32_LAYOUT_H
#define MIME_WOW32_LAYOUT_H

#include <mime/ntuser.h>

/* a handle, sign extended as windows' wow64 does it, so that -1 stays -1. */
static inline HANDLE lay_handle(ULONG value)
{
    return (HANDLE)(LONG_PTR)(LONG)value;
}

static inline ULONG lay_narrow(const void *pointer)
{
    return (ULONG)(ULONG_PTR)pointer;
}

typedef struct DRAWITEM32 {
    ULONG CtlType, CtlID, itemID, itemAction, itemState, hwndItem, hDC;
    RECT rcItem;
    ULONG itemData;
} DRAWITEM32;
typedef struct MEASUREITEM32 {
    ULONG CtlType, CtlID, itemID, itemWidth, itemHeight, itemData;
} MEASUREITEM32;
typedef struct DELETEITEM32 {
    ULONG CtlType, CtlID, itemID, hwndItem, itemData;
} DELETEITEM32;
typedef struct COMPAREITEM32 {
    ULONG CtlType, CtlID, hwndItem, itemID1, itemData1, itemID2, itemData2, dwLocaleId;
} COMPAREITEM32;
typedef struct WINDOWPOS32_LAYOUT {
    ULONG hwnd, hwndInsertAfter;
    int x, y, cx, cy;
    UINT flags;
} WINDOWPOS32_LAYOUT;
typedef struct HELPINFO32 {
    UINT cbSize;
    int iContextType, iCtrlId;
    ULONG hItemHandle, dwContextId;
    POINT MousePos;
} HELPINFO32;
typedef struct MSG32_LAYOUT {
    ULONG hwnd, message, wParam, lParam, time;
    POINT pt;
} MSG32_LAYOUT;
typedef struct COMBOBOXINFO32 {
    DWORD cbSize;
    RECT rcItem, rcButton;
    DWORD stateButton;
    ULONG hwndCombo, hwndItem, hwndList;
} COMBOBOXINFO32;
typedef struct NEXTMENU32 {
    ULONG hmenuIn, hmenuNext, hwndNext;
} NEXTMENU32;
typedef struct MENUGETOBJECT32 {
    DWORD dwFlags;
    UINT uPos;
    ULONG hmenu, riid, pvObj;
} MENUGETOBJECT32;
typedef struct DROPSTRUCT32 {
    ULONG hwndSource, hwndSink, wFmt, dwData;
    POINT ptDrop;
    DWORD dwControlData;
} DROPSTRUCT32;

typedef struct MENUGETOBJECT64 {
    DWORD dwFlags;
    UINT uPos;
    HANDLE hmenu;
    ULONG_PTR riid, pvObj;
} MENUGETOBJECT64;
typedef struct DROPSTRUCT64 {
    HANDLE hwndSource, hwndSink;
    DWORD wFmt;
    ULONG_PTR dwData;
    POINT ptDrop;
    DWORD dwControlData;
} DROPSTRUCT64;
typedef struct NEXTMENU64 {
    HANDLE hmenuIn, hmenuNext, hwndNext;
} NEXTMENU64;
typedef struct COMBOBOXINFO64 {
    DWORD cbSize;
    RECT rcItem, rcButton;
    DWORD stateButton;
    HANDLE hwndCombo, hwndItem, hwndList;
} COMBOBOXINFO64;
typedef struct HELPINFO64 {
    UINT cbSize;
    int iContextType, iCtrlId;
    HANDLE hItemHandle;
    ULONG_PTR dwContextId;
    POINT MousePos;
} HELPINFO64;
typedef struct DRAWITEM64 {
    UINT CtlType, CtlID, itemID, itemAction, itemState;
    HANDLE hwndItem, hDC;
    RECT rcItem;
    ULONG_PTR itemData;
} DRAWITEM64;
typedef struct MEASUREITEM64 {
    UINT CtlType, CtlID, itemID, itemWidth, itemHeight;
    ULONG_PTR itemData;
} MEASUREITEM64;
typedef struct DELETEITEM64 {
    UINT CtlType, CtlID, itemID;
    HANDLE hwndItem;
    ULONG_PTR itemData;
} DELETEITEM64;
typedef struct COMPAREITEM64 {
    UINT CtlType, CtlID;
    HANDLE hwndItem;
    UINT itemID1;
    ULONG_PTR itemData1;
    UINT itemID2;
    ULONG_PTR itemData2;
    DWORD dwLocaleId;
} COMPAREITEM64;
typedef struct MSG64_LAYOUT {
    HANDLE hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
    DWORD lPrivate;
} MSG64_LAYOUT;
typedef struct WINDOWPOS64_LAYOUT {
    HANDLE hwnd, hwndInsertAfter;
    int x, y, cx, cy;
    UINT flags;
} WINDOWPOS64_LAYOUT;

/* the messages whose lparam is a fixed structure of one layout in the 32-bit program and another in win32k. */
static inline ULONG lay_size32(UINT msg)
{
    switch (msg) {
    case WM_DRAWITEM:
        return sizeof(DRAWITEM32);
    case WM_MEASUREITEM:
        return sizeof(MEASUREITEM32);
    case WM_DELETEITEM:
        return sizeof(DELETEITEM32);
    case WM_COMPAREITEM:
        return sizeof(COMPAREITEM32);
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        return sizeof(WINDOWPOS32_LAYOUT);
    case WM_HELP:
        return sizeof(HELPINFO32);
    case WM_GETDLGCODE:
        return sizeof(MSG32_LAYOUT);
    case CB_GETCOMBOBOXINFO:
        return sizeof(COMBOBOXINFO32);
    case WM_NEXTMENU:
        return sizeof(NEXTMENU32);
    case 0x0124:
        return sizeof(MENUGETOBJECT32);
    case 0x022A:
    case 0x022B:
    case 0x022D:
    case 0x022E:
    case 0x022F:
        return sizeof(DROPSTRUCT32);
    }
    return 0;
}

static inline ULONG lay_size64(UINT msg)
{
    switch (msg) {
    case WM_DRAWITEM:
        return sizeof(DRAWITEM64);
    case WM_MEASUREITEM:
        return sizeof(MEASUREITEM64);
    case WM_DELETEITEM:
        return sizeof(DELETEITEM64);
    case WM_COMPAREITEM:
        return sizeof(COMPAREITEM64);
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        return sizeof(WINDOWPOS64_LAYOUT);
    case WM_HELP:
        return sizeof(HELPINFO64);
    case WM_GETDLGCODE:
        return sizeof(MSG64_LAYOUT);
    case CB_GETCOMBOBOXINFO:
        return sizeof(COMBOBOXINFO64);
    case WM_NEXTMENU:
        return sizeof(NEXTMENU64);
    case 0x0124:
        return sizeof(MENUGETOBJECT64);
    case 0x022A:
    case 0x022B:
    case 0x022D:
    case 0x022E:
    case 0x022F:
        return sizeof(DROPSTRUCT64);
    }
    return 0;
}

/* whether the receiver may change the structure, so that what it wrote has to go back. */
static inline BOOLEAN lay_writes(UINT msg)
{
    return msg == WM_MEASUREITEM || msg == WM_WINDOWPOSCHANGING || msg == CB_GETCOMBOBOXINFO || msg == WM_NEXTMENU ||
           msg == 0x0124 || (msg >= 0x022A && msg <= 0x022F);
}

/* the structure of a 32-bit program as win32k wants it. */
static inline void lay_to64(UINT msg, const void *in, void *out)
{
    switch (msg) {
    case WM_DRAWITEM: {
        const DRAWITEM32 *a = in;
        *(DRAWITEM64 *)out = (DRAWITEM64){a->CtlType, a->CtlID, a->itemID, a->itemAction, a->itemState,
                                          lay_handle(a->hwndItem), lay_handle(a->hDC), a->rcItem, a->itemData};
        break;
    }
    case WM_MEASUREITEM: {
        const MEASUREITEM32 *a = in;
        *(MEASUREITEM64 *)out = (MEASUREITEM64){a->CtlType, a->CtlID, a->itemID, a->itemWidth, a->itemHeight, a->itemData};
        break;
    }
    case WM_DELETEITEM: {
        const DELETEITEM32 *a = in;
        *(DELETEITEM64 *)out = (DELETEITEM64){a->CtlType, a->CtlID, a->itemID, lay_handle(a->hwndItem), a->itemData};
        break;
    }
    case WM_COMPAREITEM: {
        const COMPAREITEM32 *a = in;
        *(COMPAREITEM64 *)out = (COMPAREITEM64){a->CtlType, a->CtlID, lay_handle(a->hwndItem), a->itemID1, a->itemData1,
                                                a->itemID2, a->itemData2, a->dwLocaleId};
        break;
    }
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED: {
        const WINDOWPOS32_LAYOUT *a = in;
        *(WINDOWPOS64_LAYOUT *)out = (WINDOWPOS64_LAYOUT){lay_handle(a->hwnd), lay_handle(a->hwndInsertAfter), a->x, a->y,
                                                          a->cx, a->cy, a->flags};
        break;
    }
    case WM_HELP: {
        const HELPINFO32 *a = in;
        /* a size that is not the 32-bit one stays, so the refusal of a wrong size is the same. */
        *(HELPINFO64 *)out = (HELPINFO64){a->cbSize == sizeof(HELPINFO32) ? sizeof(HELPINFO64) : a->cbSize,
                                          a->iContextType, a->iCtrlId, lay_handle(a->hItemHandle), a->dwContextId,
                                          a->MousePos};
        break;
    }
    case WM_GETDLGCODE: {
        const MSG32_LAYOUT *a = in;
        *(MSG64_LAYOUT *)out = (MSG64_LAYOUT){lay_handle(a->hwnd), a->message, a->wParam, (LONG_PTR)(LONG)a->lParam,
                                              a->time, a->pt, 0};
        break;
    }
    case CB_GETCOMBOBOXINFO: {
        const COMBOBOXINFO32 *a = in;
        *(COMBOBOXINFO64 *)out = (COMBOBOXINFO64){a->cbSize == sizeof(COMBOBOXINFO32) ? sizeof(COMBOBOXINFO64) : a->cbSize,
                                                  a->rcItem, a->rcButton, a->stateButton, lay_handle(a->hwndCombo),
                                                  lay_handle(a->hwndItem), lay_handle(a->hwndList)};
        break;
    }
    case WM_NEXTMENU: {
        const NEXTMENU32 *a = in;
        *(NEXTMENU64 *)out = (NEXTMENU64){lay_handle(a->hmenuIn), lay_handle(a->hmenuNext), lay_handle(a->hwndNext)};
        break;
    }
    case 0x0124: {
        const MENUGETOBJECT32 *a = in;
        *(MENUGETOBJECT64 *)out = (MENUGETOBJECT64){a->dwFlags, a->uPos, lay_handle(a->hmenu), a->riid, a->pvObj};
        break;
    }
    default: {
        const DROPSTRUCT32 *a = in;
        *(DROPSTRUCT64 *)out = (DROPSTRUCT64){lay_handle(a->hwndSource), lay_handle(a->hwndSink), a->wFmt, a->dwData,
                                              a->ptDrop, a->dwControlData};
        break;
    }
    }
}

/* the structure of win32k as a 32-bit program has it. */
static inline void lay_to32(UINT msg, const void *in, void *out)
{
    switch (msg) {
    case WM_DRAWITEM: {
        const DRAWITEM64 *a = in;
        *(DRAWITEM32 *)out = (DRAWITEM32){a->CtlType, a->CtlID, a->itemID, a->itemAction, a->itemState,
                                          lay_narrow(a->hwndItem), lay_narrow(a->hDC), a->rcItem, (ULONG)a->itemData};
        break;
    }
    case WM_MEASUREITEM: {
        const MEASUREITEM64 *a = in;
        *(MEASUREITEM32 *)out = (MEASUREITEM32){a->CtlType, a->CtlID, a->itemID, a->itemWidth, a->itemHeight,
                                                (ULONG)a->itemData};
        break;
    }
    case WM_DELETEITEM: {
        const DELETEITEM64 *a = in;
        *(DELETEITEM32 *)out = (DELETEITEM32){a->CtlType, a->CtlID, a->itemID, lay_narrow(a->hwndItem), (ULONG)a->itemData};
        break;
    }
    case WM_COMPAREITEM: {
        const COMPAREITEM64 *a = in;
        *(COMPAREITEM32 *)out = (COMPAREITEM32){a->CtlType, a->CtlID, lay_narrow(a->hwndItem), a->itemID1,
                                                (ULONG)a->itemData1, a->itemID2, (ULONG)a->itemData2, a->dwLocaleId};
        break;
    }
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED: {
        const WINDOWPOS64_LAYOUT *a = in;
        *(WINDOWPOS32_LAYOUT *)out = (WINDOWPOS32_LAYOUT){lay_narrow(a->hwnd), lay_narrow(a->hwndInsertAfter), a->x, a->y,
                                                          a->cx, a->cy, a->flags};
        break;
    }
    case WM_HELP: {
        const HELPINFO64 *a = in;
        *(HELPINFO32 *)out = (HELPINFO32){a->cbSize == sizeof(HELPINFO64) ? sizeof(HELPINFO32) : a->cbSize, a->iContextType,
                                          a->iCtrlId, lay_narrow(a->hItemHandle), (ULONG)a->dwContextId, a->MousePos};
        break;
    }
    case WM_GETDLGCODE: {
        const MSG64_LAYOUT *a = in;
        *(MSG32_LAYOUT *)out = (MSG32_LAYOUT){lay_narrow(a->hwnd), a->message, (ULONG)a->wParam, (ULONG)a->lParam, a->time,
                                              a->pt};
        break;
    }
    case CB_GETCOMBOBOXINFO: {
        const COMBOBOXINFO64 *a = in;
        *(COMBOBOXINFO32 *)out = (COMBOBOXINFO32){a->cbSize == sizeof(COMBOBOXINFO64) ? sizeof(COMBOBOXINFO32) : a->cbSize,
                                                  a->rcItem, a->rcButton, a->stateButton, lay_narrow(a->hwndCombo),
                                                  lay_narrow(a->hwndItem), lay_narrow(a->hwndList)};
        break;
    }
    case WM_NEXTMENU: {
        const NEXTMENU64 *a = in;
        *(NEXTMENU32 *)out = (NEXTMENU32){lay_narrow(a->hmenuIn), lay_narrow(a->hmenuNext), lay_narrow(a->hwndNext)};
        break;
    }
    case 0x0124: {
        const MENUGETOBJECT64 *a = in;
        *(MENUGETOBJECT32 *)out = (MENUGETOBJECT32){a->dwFlags, a->uPos, lay_narrow(a->hmenu), (ULONG)a->riid,
                                                    (ULONG)a->pvObj};
        break;
    }
    default: {
        const DROPSTRUCT64 *a = in;
        *(DROPSTRUCT32 *)out = (DROPSTRUCT32){lay_narrow(a->hwndSource), lay_narrow(a->hwndSink), a->wFmt,
                                              (ULONG)a->dwData, a->ptDrop, a->dwControlData};
        break;
    }
    }
}

/* what a 32-bit procedure changed, in the structure of win32k. only the fields that can change carry over. */
static inline void lay_back64(UINT msg, const void *in32, void *out64)
{
    UCHAR whole[sizeof(DRAWITEM64) + sizeof(COMBOBOXINFO64) + 32];
    lay_to64(msg, in32, whole);
    memcpy(out64, whole, lay_size64(msg));
}

#endif
