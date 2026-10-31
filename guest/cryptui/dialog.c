/* the dialogs of cryptui, built in memory: a text that is read and a list that one entry is chosen from. */
#include "cryptui_private.h"

#ifndef WS_TABSTOP
#define WS_TABSTOP 0x00010000L
#endif
#define CTL_BUTTON 0x0080
#define CTL_EDIT 0x0081
#define CTL_STATIC 0x0082
#define CTL_LISTBOX 0x0083

#define ID_TEXT 100
#define ID_LIST 100
#define ID_PROMPT 101

typedef struct {
    WORD atom;
    LPCWSTR text;
    WORD id;
    DWORD style;
    short x, y, w, h;
} CONTROL;

static WORD *put_text(WORD *out, LPCWSTR text)
{
    while ((*out++ = *text++))
        ;
    return out;
}

static WORD *align(WORD *out, WORD *base)
{
    return (WORD *)(((ULONG_PTR)out - (ULONG_PTR)base + 3) / 4 * 4 + (ULONG_PTR)base);
}

/* a captioned dialog in the shell font, free it with LocalFree. */
static LPDLGTEMPLATEW build(LPCWSTR title, int width, int height, const CONTROL *controls, UINT count)
{
    SIZE_T room = 64 + (lstrlenW(title) + 16) * sizeof(WCHAR);
    for (UINT i = 0; i < count; i++)
        room += 32 + (lstrlenW(controls[i].text) + 2) * sizeof(WCHAR);
    WORD *base = LocalAlloc(0x40, room), *out = base;
    if (!base)
        return nullptr;
    *(DWORD *)out = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_VISIBLE;
    out += 2;
    *(DWORD *)out = 0;
    out += 2;
    *out++ = (WORD)count;
    *out++ = 0;
    *out++ = 0;
    *out++ = (WORD)width;
    *out++ = (WORD)height;
    *out++ = 0;
    *out++ = 0;
    out = put_text(out, title);
    *out++ = 8;
    out = put_text(out, L"MS Shell Dlg");
    for (UINT i = 0; i < count; i++) {
        out = align(out, base);
        *(DWORD *)out = controls[i].style | WS_CHILD | WS_VISIBLE;
        out += 2;
        *(DWORD *)out = 0;
        out += 2;
        *out++ = controls[i].x;
        *out++ = controls[i].y;
        *out++ = controls[i].w;
        *out++ = controls[i].h;
        *out++ = controls[i].id;
        *out++ = 0xFFFF;
        *out++ = controls[i].atom;
        out = put_text(out, controls[i].text);
        *out++ = 0;
    }
    return (LPDLGTEMPLATEW)base;
}

/* ------------------------------------------------------------------ the text */

static INT_PTR CALLBACK text_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dialog, ID_TEXT, (LPCWSTR)lparam);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void CuiShowText(HWND parent, const WCHAR *title, const WCHAR *text)
{
    static const CONTROL controls[] = {
        {CTL_EDIT, L"", ID_TEXT, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 7,
         246, 146},
        {CTL_BUTTON, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 203, 159, 50, 14},
    };
    LPDLGTEMPLATEW tmpl = build(title, 260, 180, controls, 2);
    if (!tmpl)
        return;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), tmpl, parent, text_proc, (LPARAM)text);
    LocalFree(tmpl);
}

/* ------------------------------------------------------------------ the list */

typedef struct {
    const WCHAR *prompt;
    WCHAR **items;
    UINT count;
    int chosen;
} LIST;

static INT_PTR CALLBACK list_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    LIST *list;
    switch (message) {
    case WM_INITDIALOG:
        list = (LIST *)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetDlgItemTextW(dialog, ID_PROMPT, list->prompt);
        for (UINT i = 0; i < list->count; i++)
            SendDlgItemMessageW(dialog, ID_LIST, LB_ADDSTRING, 0, (LPARAM)list->items[i]);
        if (list->count)
            SendDlgItemMessageW(dialog, ID_LIST, LB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(dialog, IDOK), list->count != 0);
        return TRUE;
    case WM_COMMAND:
        list = (LIST *)GetWindowLongPtrW(dialog, DWLP_USER);
        if (LOWORD(wparam) == IDOK || (LOWORD(wparam) == ID_LIST && HIWORD(wparam) == LBN_DBLCLK)) {
            int chosen = (int)SendDlgItemMessageW(dialog, ID_LIST, LB_GETCURSEL, 0, 0);
            if (chosen >= 0) {
                list->chosen = chosen;
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int CuiPickFromList(HWND parent, const WCHAR *title, const WCHAR *prompt, WCHAR **items, UINT count)
{
    static const CONTROL controls[] = {
        {CTL_STATIC, L"", ID_PROMPT, SS_LEFT, 7, 7, 246, 20},
        {CTL_LISTBOX, L"", ID_LIST, LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 30, 246,
         120},
        {CTL_BUTTON, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 148, 159, 50, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, WS_TABSTOP, 203, 159, 50, 14},
    };
    LIST list = {prompt, items, count, -1};
    LPDLGTEMPLATEW tmpl = build(title, 260, 180, controls, 4);
    if (!tmpl)
        return -1;
    INT_PTR answer = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), tmpl, parent,
                                             list_proc, (LPARAM)&list);
    LocalFree(tmpl);
    return answer == IDOK ? list.chosen : -1;
}
