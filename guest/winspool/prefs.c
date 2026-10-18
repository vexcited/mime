/*
 * the printing preferences: the paper, the orientation, the quality and the color of a printer, in a
 * dialog made from a template in memory.
 */
#include "winspool_private.h"

#define ID_PAPER 101
#define ID_PORTRAIT 102
#define ID_LANDSCAPE 103
#define ID_QUALITY 104
#define ID_COLOR 105
#define ID_ORIENTATION 106
#define ID_SIZE_LABEL 107
#define ID_QUALITY_LABEL 108
#define ID_COLOR_LABEL 109

typedef struct _PREFS {
    int Printer;
    DEVMODEW *Devmode;
} PREFS;

typedef struct _ITEM {
    WORD Class;
    LPCWSTR Text;
    WORD Id;
    DWORD Style;
    short X, Y, W, H;
} ITEM;

static BYTE *put_word(BYTE *p, WORD v)
{
    *(WORD *)p = v;
    return p + 2;
}

static BYTE *put_dword(BYTE *p, DWORD v)
{
    *(DWORD *)p = v;
    return p + 4;
}

static BYTE *put_string(BYTE *p, LPCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    memcpy(p, text, bytes);
    return p + bytes;
}

static LPDLGTEMPLATEW build(LPCWSTR title, int width, int height, const ITEM *items, UINT count)
{
    SIZE_T size = 128 + (wcslen(title) + 1) * sizeof(WCHAR);
    for (UINT i = 0; i < count; i++)
        size += 40 + (wcslen(items[i].Text) + 1) * sizeof(WCHAR);
    BYTE *start = SpoolAlloc(size), *p = start;
    if (!start)
        return nullptr;
    p = put_dword(p, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT);
    p = put_dword(p, 0);
    p = put_word(p, (WORD)count);
    p = put_word(p, 0);
    p = put_word(p, 0);
    p = put_word(p, (WORD)width);
    p = put_word(p, (WORD)height);
    p = put_word(p, 0);
    p = put_word(p, 0);
    p = put_string(p, title);
    p = put_word(p, 9);
    p = put_string(p, L"Segoe UI");
    for (UINT i = 0; i < count; i++) {
        p = start + (((SIZE_T)(p - start) + 3) & ~(SIZE_T)3);
        p = put_dword(p, items[i].Style | WS_CHILD | WS_VISIBLE);
        p = put_dword(p, 0);
        p = put_word(p, (WORD)items[i].X);
        p = put_word(p, (WORD)items[i].Y);
        p = put_word(p, (WORD)items[i].W);
        p = put_word(p, (WORD)items[i].H);
        p = put_word(p, items[i].Id);
        p = put_word(p, 0xFFFF);
        p = put_word(p, items[i].Class);
        p = put_string(p, items[i].Text);
        p = put_word(p, 0);
    }
    return (LPDLGTEMPLATEW)start;
}


static void init_values(HWND hwnd, PREFS *prefs)
{
    DEVMODEW *dm = prefs->Devmode;
    for (int i = 0; i < SpoolPaperCount(); i++) {
        const SpoolPaper *paper = SpoolPaperAt(i);
        SendDlgItemMessageW(hwnd, ID_PAPER, CB_ADDSTRING, 0, (LPARAM)paper->Name);
        if (paper->Id == (WORD)dm->dmPaperSize)
            SendDlgItemMessageW(hwnd, ID_PAPER, CB_SETCURSEL, (WPARAM)i, 0);
    }
    CheckRadioButton(hwnd, ID_PORTRAIT, ID_LANDSCAPE, dm->dmOrientation == DMORIENT_LANDSCAPE ? ID_LANDSCAPE : ID_PORTRAIT);
    for (int i = 0; i < SpoolResolutionCount(); i++) {
        WCHAR text[24];
        _snwprintf(text, ARRAYSIZE(text), L"%d dpi", SpoolResolutionAt(i));
        SendDlgItemMessageW(hwnd, ID_QUALITY, CB_ADDSTRING, 0, (LPARAM)text);
        if (SpoolResolutionAt(i) == dm->dmPrintQuality)
            SendDlgItemMessageW(hwnd, ID_QUALITY, CB_SETCURSEL, (WPARAM)i, 0);
    }
    SendDlgItemMessageW(hwnd, ID_COLOR, CB_ADDSTRING, 0, (LPARAM)L"Color");
    SendDlgItemMessageW(hwnd, ID_COLOR, CB_ADDSTRING, 0, (LPARAM)L"Black and white");
    SendDlgItemMessageW(hwnd, ID_COLOR, CB_SETCURSEL, dm->dmColor == DMCOLOR_MONOCHROME ? 1 : 0, 0);
}

static void take_values(HWND hwnd, PREFS *prefs)
{
    DEVMODEW *dm = prefs->Devmode;
    LRESULT paper = SendDlgItemMessageW(hwnd, ID_PAPER, CB_GETCURSEL, 0, 0);
    if (paper >= 0 && paper < SpoolPaperCount()) {
        dm->dmPaperSize = (short)SpoolPaperAt((int)paper)->Id;
        wcscpy(dm->dmFormName, SpoolPaperAt((int)paper)->Name);
    }
    dm->dmOrientation = IsDlgButtonChecked(hwnd, ID_LANDSCAPE) == BST_CHECKED ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    LRESULT quality = SendDlgItemMessageW(hwnd, ID_QUALITY, CB_GETCURSEL, 0, 0);
    if (quality >= 0 && quality < SpoolResolutionCount()) {
        dm->dmPrintQuality = (short)SpoolResolutionAt((int)quality);
        dm->dmYResolution = dm->dmPrintQuality;
    }
    dm->dmColor = SendDlgItemMessageW(hwnd, ID_COLOR, CB_GETCURSEL, 0, 0) == 1 ? DMCOLOR_MONOCHROME : DMCOLOR_COLOR;
}

static INT_PTR CALLBACK prefs_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
        init_values(hwnd, (PREFS *)lparam);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK) {
            take_values(hwnd, (PREFS *)GetWindowLongPtrW(hwnd, DWLP_USER));
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR SpoolPreferences(HWND owner, int printer, DEVMODEW *devmode)
{
    static const ITEM items[] = {
        {0x0082, L"Paper &size:", ID_SIZE_LABEL, SS_LEFT, 8, 10, 60, 9},
        {0x0085, L"", ID_PAPER, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 72, 8, 100, 120},
        {0x0080, L"Orientation", ID_ORIENTATION, BS_GROUPBOX, 8, 28, 164, 34},
        {0x0080, L"&Portrait", ID_PORTRAIT, WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 16, 42, 70, 12},
        {0x0080, L"&Landscape", ID_LANDSCAPE, BS_AUTORADIOBUTTON, 92, 42, 72, 12},
        {0x0082, L"Print &quality:", ID_QUALITY_LABEL, SS_LEFT, 8, 72, 60, 9},
        {0x0085, L"", ID_QUALITY, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 72, 70, 100, 60},
        {0x0082, L"&Color:", ID_COLOR_LABEL, SS_LEFT, 8, 90, 60, 9},
        {0x0085, L"", ID_COLOR, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 72, 88, 100, 60},
        {0x0080, L"OK", IDOK, WS_TABSTOP | BS_DEFPUSHBUTTON, 70, 112, 48, 14},
        {0x0080, L"Cancel", IDCANCEL, WS_TABSTOP | BS_PUSHBUTTON, 124, 112, 48, 14},
    };
    WCHAR title[128];
    _snwprintf(title, ARRAYSIZE(title), L"%ls Printing Preferences", SpoolPrinterAt(printer)->Name);
    LPDLGTEMPLATEW tmpl = build(title, 180, 134, items, ARRAYSIZE(items));
    if (!tmpl)
        return IDCANCEL;
    PREFS prefs = {printer, devmode};
    INT_PTR answer = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), tmpl, owner, prefs_proc, (LPARAM)&prefs);
    SpoolFree(tmpl);
    return answer == IDOK ? IDOK : IDCANCEL;
}
