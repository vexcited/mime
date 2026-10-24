/*
 * PrintDlg and PrintDlgEx share one window. the answer comes back in two global handles, a DEVMODE and a
 * DEVNAMES, plus a printer device context when asked.
 */
#include "printcommon.h"
#include <mime/win32/shobjidl.h>

MIME_DEFINE_GUID(IID_IPrintDialogCallback, 0x5852A2C3, 0x6530, 0x11D1, 0xB6, 0xA3, 0x00, 0x00, 0xF8, 0x75, 0x7B, 0xF9);
MIME_DEFINE_GUID(IID_IPrintDialogServices, 0x509AAEDA, 0x5639, 0x11D1, 0xB6, 0xA3, 0x00, 0x00, 0xF8, 0x75, 0x7B, 0xF9);

/* the PRINTDLG flags these dialogs do not handle, plus those of PRINTDLGEX. */
#define PDEX_INVALID_FLAGS 0xEA22B840u

typedef struct _PRINTSTATE PRINTSTATE;

typedef struct _SERVICES {
    IPrintDialogServices Interface;
    PRINTSTATE *State;
} SERVICES;

struct _PRINTSTATE {
    BOOL Ex, Setup, Ansi;
    HWND Owner, Dialog;
    DWORD Flags;
    PRN_LIST List;
    int Current;
    DEVMODEW *Devmode;
    BOOL ToFile;
    int Range;
    DWORD From, To, Min, Max;
    DWORD Copies;
    BOOL Collate;
    /* PrintDlgEx page ranges. */
    PRINTPAGERANGE *Ranges;
    DWORD RangeCount, MaxRanges;
    BOOL CopiesFixed, CollateFixed;
    DLGPROC Hook;
    LPARAM HookParam;
    IPrintDialogCallback *Callback;
    SERVICES Services;
    int Result;
    BOOL Closed;
};

static UINT g_msg_help;
static DWORD g_hook_tls = TLS_OUT_OF_INDEXES;
static LONG g_hook_once;
static BOOL g_cancelled;

/* ------------------------------------------------------------------ the services of a callback */

static HRESULT STDMETHODCALLTYPE svc_query_interface(IPrintDialogServices *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPrintDialogServices)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE svc_add_ref(IPrintDialogServices *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE svc_release(IPrintDialogServices *This)
{
    return 1;
}

static PRINTSTATE *state_of(IPrintDialogServices *This)
{
    return ((SERVICES *)This)->State;
}

static HRESULT STDMETHODCALLTYPE svc_get_devmode(IPrintDialogServices *This, DEVMODEW *devmode, UINT *size)
{
    PRINTSTATE *s = state_of(This);
    if (!size)
        return E_INVALIDARG;
    if (!s->Devmode)
        return E_FAIL;
    UINT needed = (UINT)(s->Devmode->dmSize + s->Devmode->dmDriverExtra);
    if (!devmode || *size < needed) {
        *size = needed;
        return devmode ? E_OUTOFMEMORY : S_OK;
    }
    memcpy(devmode, s->Devmode, needed);
    *size = needed;
    return S_OK;
}

static HRESULT copy_text(LPCWSTR text, LPWSTR out, UINT *size)
{
    UINT needed = (UINT)lstrlenW(text) + 1;
    if (!size)
        return E_INVALIDARG;
    if (!out || *size < needed) {
        *size = needed;
        return out ? E_OUTOFMEMORY : S_OK;
    }
    memcpy(out, text, needed * sizeof(WCHAR));
    *size = needed;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE svc_get_name(IPrintDialogServices *This, LPWSTR name, UINT *size)
{
    PRINTSTATE *s = state_of(This);
    if (s->Current < 0)
        return E_FAIL;
    return copy_text(s->List.Items[s->Current].Name, name, size);
}

static HRESULT STDMETHODCALLTYPE svc_get_port(IPrintDialogServices *This, LPWSTR name, UINT *size)
{
    PRINTSTATE *s = state_of(This);
    if (s->Current < 0)
        return E_FAIL;
    return copy_text(s->List.Items[s->Current].Port, name, size);
}

static const IPrintDialogServicesVtbl g_services_vtbl = {
    svc_query_interface, svc_add_ref, svc_release, svc_get_devmode, svc_get_name, svc_get_port,
};

/* ------------------------------------------------------------------ the template */

static LPDLGTEMPLATEW build_template(const PRINTSTATE *s)
{
    static const DWORD tab = WS_VISIBLE | WS_TABSTOP;
    CTL c[40];
    UINT n = 0;
#define ADD(...) c[n++] = (CTL){__VA_ARGS__}
    ADD(CTL_BUTTON, L"Printer", PRN_GROUP_PRINTER, WS_VISIBLE | BS_GROUPBOX, 8, 4, 284, 88);
    ADD(CTL_STATIC, L"&Name:", PRN_NAME_LABEL, WS_VISIBLE | SS_LEFT, 16, 20, 34, 9);
    ADD(CTL_COMBOBOX, L"", PRN_NAME, tab | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 52, 18, 154, 100);
    ADD(CTL_BUTTON, L"&Properties...", PRN_PROPERTIES, tab | BS_PUSHBUTTON, 212, 17, 72, 14);
    ADD(CTL_STATIC, L"Status:", PRN_STATUS_LABEL, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 16, 36, 34, 10);
    ADD(CTL_STATIC, L"", PRN_STATUS, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 52, 36, 232, 10);
    ADD(CTL_STATIC, L"Type:", PRN_TYPE_LABEL, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 16, 48, 34, 10);
    ADD(CTL_STATIC, L"", PRN_TYPE, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 52, 48, 232, 10);
    ADD(CTL_STATIC, L"Where:", PRN_WHERE_LABEL, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 16, 60, 34, 10);
    ADD(CTL_STATIC, L"", PRN_WHERE, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 52, 60, 232, 10);
    ADD(CTL_STATIC, L"Comment:", PRN_COMMENT_LABEL, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 16, 72, 34, 10);
    ADD(CTL_STATIC, L"", PRN_COMMENT, WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 52, 72, 150, 10);
    ADD(CTL_BUTTON, L"Print to fi&le", PRN_TOFILE, tab | BS_AUTOCHECKBOX, 212, 72, 72, 12);
    if (!s->Setup) {
        ADD(CTL_BUTTON, s->Ex ? L"Page Range" : L"Print range", PRN_GROUP_RANGE, WS_VISIBLE | BS_GROUPBOX, 8, 96,
            s->Ex ? 150 : 144, s->Ex ? 86 : 72);
        ADD(CTL_BUTTON, L"&All", PRN_ALL, tab | BS_AUTORADIOBUTTON | WS_GROUP, 16, 110, 64, 12);
        if (s->Ex) {
            ADD(CTL_BUTTON, L"Selec&tion", PRN_SELECTION, WS_VISIBLE | BS_AUTORADIOBUTTON, 84, 110, 64, 12);
            ADD(CTL_BUTTON, L"C&urrent Page", PRN_EX_CURRENT, WS_VISIBLE | BS_AUTORADIOBUTTON, 84, 124, 64, 12);
            ADD(CTL_BUTTON, L"Pa&ges:", PRN_EX_PAGES, WS_VISIBLE | BS_AUTORADIOBUTTON, 16, 124, 36, 12);
            ADD(CTL_EDIT, L"", PRN_FROM, tab | WS_BORDER | ES_AUTOHSCROLL, 16, 140, 132, 12);
            ADD(CTL_STATIC, L"Enter page numbers and/or page ranges separated by commas. For example, 1,3,5-12", PRN_RANGES_HINT,
                WS_VISIBLE | SS_LEFT, 16, 155, 134, 24);
        } else {
            ADD(CTL_BUTTON, L"Pa&ges", PRN_PAGES, WS_VISIBLE | BS_AUTORADIOBUTTON, 16, 126, 36, 12);
            ADD(CTL_STATIC, L"&from:", PRN_FROM_LABEL, WS_VISIBLE | SS_RIGHT, 54, 128, 22, 9);
            ADD(CTL_EDIT, L"", PRN_FROM, tab | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 78, 126, 26, 12);
            ADD(CTL_STATIC, L"&to:", PRN_TO_LABEL, WS_VISIBLE | SS_RIGHT, 106, 128, 16, 9);
            ADD(CTL_EDIT, L"", PRN_TO, tab | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 124, 126, 26, 12);
            ADD(CTL_BUTTON, L"&Selection", PRN_SELECTION, WS_VISIBLE | BS_AUTORADIOBUTTON, 16, 142, 64, 12);
        }
        ADD(CTL_BUTTON, L"Copies", PRN_GROUP_COPIES, WS_VISIBLE | BS_GROUPBOX, s->Ex ? 166 : 160, 96, s->Ex ? 126 : 132,
            s->Ex ? 86 : 72);
        ADD(CTL_STATIC, L"Number of &copies:", PRN_COPIES_LABEL, WS_VISIBLE | SS_LEFT, s->Ex ? 174 : 168, 112, 68, 9);
        ADD(CTL_EDIT, L"", PRN_COPIES, tab | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, s->Ex ? 246 : 242, 110, 34, 12);
        ADD(CTL_STATIC, L"", PRN_COLLATE_ICON, WS_VISIBLE | SS_OWNERDRAW, s->Ex ? 174 : 168, 130, 40, 30);
        ADD(CTL_BUTTON, L"C&ollate", PRN_COLLATE, tab | BS_AUTOCHECKBOX, s->Ex ? 222 : 216, 138, 60, 12);
    }
    int y = s->Setup ? 100 : (s->Ex ? 190 : 174);
    ADD(CTL_BUTTON, s->Ex ? L"Print" : L"OK", IDOK, tab | BS_DEFPUSHBUTTON, 184, (short)y, 48, 14);
    ADD(CTL_BUTTON, L"Cancel", IDCANCEL, tab | BS_PUSHBUTTON, 236, (short)y, 48, 14);
    if (s->Ex)
        ADD(CTL_BUTTON, L"&Apply", PRN_APPLY, tab | BS_PUSHBUTTON, 132, (short)y, 48, 14);
    ADD(CTL_BUTTON, L"&Help", pshHelp, tab | BS_PUSHBUTTON, 8, (short)y, 48, 14);
#undef ADD
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | WS_VISIBLE;
    return DlgBuild(s->Setup ? L"Print Setup" : L"Print", style, 300, s->Setup ? 120 : (s->Ex ? 210 : 194), c, n);
}

/* ------------------------------------------------------------------ the controls */

static HWND item(const PRINTSTATE *s, int id)
{
    return GetDlgItem(s->Dialog, id);
}

static void enable(const PRINTSTATE *s, int id, BOOL on)
{
    HWND w = item(s, id);
    if (w)
        EnableWindow(w, on);
}

static void set_text(const PRINTSTATE *s, int id, LPCWSTR text)
{
    HWND w = item(s, id);
    if (w)
        SetWindowTextW(w, text);
}

static void show_printer(PRINTSTATE *s)
{
    if (s->Current < 0)
        return;
    const PRN_ITEM *p = &s->List.Items[s->Current];
    WCHAR text[96];
    if (p->Jobs)
        wsprintfW(text, p->Jobs == 1 ? L"%lu document waiting" : L"%lu documents waiting", (unsigned long)p->Jobs);
    else
        lstrcpyW(text, L"Ready");
    set_text(s, PRN_STATUS, text);
    set_text(s, PRN_TYPE, p->Driver);
    set_text(s, PRN_WHERE, p->Port);
    set_text(s, PRN_COMMENT, p->Comment);
}

/* whether the printer's device mode can carry copies and collation. */
static void update_copies(PRINTSTATE *s)
{
    BOOL fixed = FALSE, collate_fixed = FALSE;
    if (s->Current >= 0 && (s->Flags & PD_USEDEVMODECOPIESANDCOLLATE)) {
        const PRN_ITEM *p = &s->List.Items[s->Current];
        fixed = PrnCapability(p, DC_COPIES) <= 1;
        collate_fixed = PrnCapability(p, DC_COLLATE) <= 0;
    }
    s->CopiesFixed = fixed;
    s->CollateFixed = collate_fixed;
    enable(s, PRN_COPIES, !fixed);
    enable(s, PRN_COPIES_LABEL, !fixed);
    enable(s, PRN_COLLATE, !collate_fixed && s->Copies != 1);
}

static void show_copies(PRINTSTATE *s)
{
    HWND w = item(s, PRN_COPIES);
    if (w)
        SetDlgItemInt(s->Dialog, PRN_COPIES, s->Copies, FALSE);
    CheckDlgButton(s->Dialog, PRN_COLLATE, s->Collate ? BST_CHECKED : BST_UNCHECKED);
}

static void pages_enable(PRINTSTATE *s)
{
    BOOL pages = IsDlgButtonChecked(s->Dialog, s->Ex ? PRN_EX_PAGES : PRN_PAGES) == BST_CHECKED;
    enable(s, PRN_FROM, pages && !(s->Flags & PD_NOPAGENUMS));
    enable(s, PRN_FROM_LABEL, pages && !(s->Flags & PD_NOPAGENUMS));
    enable(s, PRN_TO, pages && !(s->Flags & PD_NOPAGENUMS));
    enable(s, PRN_TO_LABEL, pages && !(s->Flags & PD_NOPAGENUMS));
}

static void show_range(PRINTSTATE *s)
{
    if (s->Setup)
        return;
    int radio = PRN_ALL;
    if (s->Range == 1)
        radio = PRN_SELECTION;
    else if (s->Range == 2)
        radio = s->Ex ? PRN_EX_PAGES : PRN_PAGES;
    else if (s->Range == 3)
        radio = PRN_EX_CURRENT;
    CheckRadioButton(s->Dialog, PRN_ALL, s->Ex ? PRN_EX_PAGES : PRN_PAGES, radio);
    if (s->Ex) {
        WCHAR text[256];
        text[0] = 0;
        int at = 0;
        for (DWORD i = 0; i < s->RangeCount && at < 230; i++) {
            if (s->Ranges[i].nFromPage == s->Ranges[i].nToPage)
                at += wsprintfW(text + at, L"%s%lu", i ? L"," : L"", (unsigned long)s->Ranges[i].nFromPage);
            else
                at += wsprintfW(text + at, L"%s%lu-%lu", i ? L"," : L"", (unsigned long)s->Ranges[i].nFromPage,
                                (unsigned long)s->Ranges[i].nToPage);
        }
        SetDlgItemTextW(s->Dialog, PRN_FROM, text);
    } else {
        if (s->From)
            SetDlgItemInt(s->Dialog, PRN_FROM, s->From, FALSE);
        if (s->To)
            SetDlgItemInt(s->Dialog, PRN_TO, s->To, FALSE);
    }
    if (s->Flags & PD_NOSELECTION)
        enable(s, PRN_SELECTION, FALSE);
    if (s->Flags & PD_NOPAGENUMS) {
        enable(s, s->Ex ? PRN_EX_PAGES : PRN_PAGES, FALSE);
    }
    if (s->Ex && (s->Flags & PD_NOCURRENTPAGE))
        enable(s, PRN_EX_CURRENT, FALSE);
    pages_enable(s);
}

static void fill_printers(PRINTSTATE *s)
{
    HWND combo = item(s, PRN_NAME);
    if (!combo)
        return;
    for (int i = 0; i < s->List.Count; i++)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)s->List.Items[i].Name);
    if (s->Current >= 0)
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)s->Current, 0);
}

/* a new printer starts with its own defaults. */
static void printer_changed(PRINTSTATE *s, int index)
{
    if (index < 0 || index >= s->List.Count)
        return;
    s->Current = index;
    if (s->Devmode)
        HeapFree(GetProcessHeap(), 0, s->Devmode);
    s->Devmode = PrnDevmode(s->Dialog, &s->List.Items[index], nullptr);
    show_printer(s);
    update_copies(s);
    if (s->Callback)
        s->Callback->lpVtbl->SelectionChange(s->Callback);
}

/* ------------------------------------------------------------------ reading the controls */

static void message(PRINTSTATE *s, LPCWSTR text, int focus)
{
    WCHAR title[64];
    GetWindowTextW(s->Dialog, title, 64);
    MessageBoxW(s->Dialog, text, title, MB_OK | MB_ICONINFORMATION);
    HWND w = item(s, focus);
    if (w)
        SetFocus(w);
}

/* on error the caller shows the message. */
static BOOL page_number(PRINTSTATE *s, int id, DWORD *value, LPCWSTR empty_text)
{
    BOOL ok;
    UINT v = GetDlgItemInt(s->Dialog, id, &ok, FALSE);
    if (!ok || !v) {
        message(s, empty_text, id);
        return FALSE;
    }
    if (v < s->Min || v > s->Max) {
        WCHAR text[128];
        wsprintfW(text, L"This value is not within the page range.\nEnter a number between %u and %u.", s->Min, s->Max);
        message(s, text, id);
        return FALSE;
    }
    *value = v;
    return TRUE;
}

/* comma separated pages and ranges, each within the page count. */
static BOOL parse_ranges(PRINTSTATE *s, LPCWSTR text)
{
    DWORD count = 0;
    const WCHAR *p = text;
    while (*p) {
        while (*p == L' ' || *p == L',')
            p++;
        if (!*p)
            break;
        if (*p < L'0' || *p > L'9')
            goto syntax;
        DWORD from = 0, to;
        while (*p >= L'0' && *p <= L'9')
            from = from * 10 + (DWORD)(*p++ - L'0');
        to = from;
        while (*p == L' ')
            p++;
        if (*p == L'-') {
            p++;
            while (*p == L' ')
                p++;
            if (*p < L'0' || *p > L'9')
                goto syntax;
            to = 0;
            while (*p >= L'0' && *p <= L'9')
                to = to * 10 + (DWORD)(*p++ - L'0');
        }
        while (*p == L' ')
            p++;
        if (*p && *p != L',')
            goto syntax;
        if (from > to) {
            DWORD t = from;
            from = to;
            to = t;
        }
        if (from < s->Min || to > s->Max) {
            WCHAR msg[128];
            wsprintfW(msg, L"The page range is invalid.\nEnter numbers between %u and %u.", s->Min, s->Max);
            message(s, msg, PRN_FROM);
            return FALSE;
        }
        if (count >= s->MaxRanges) {
            WCHAR msg[128];
            wsprintfW(msg, L"You have entered too many page ranges. You may enter a total of %u page ranges.",
                      s->MaxRanges);
            message(s, msg, PRN_FROM);
            return FALSE;
        }
        s->Ranges[count].nFromPage = from;
        s->Ranges[count].nToPage = to;
        count++;
    }
    if (!count)
        goto syntax;
    s->RangeCount = count;
    return TRUE;
syntax:
    message(s, L"The syntax for the page range is invalid.\nEnter page numbers and/or page ranges separated by commas.  For example, 1,3,5-12.",
            PRN_FROM);
    return FALSE;
}

static BOOL take_values(PRINTSTATE *s)
{
    if (s->Current < 0) {
        message(s, L"Before you can print, you need to select a printer.", PRN_NAME);
        return FALSE;
    }
    if (!s->Setup) {
        if (!s->CopiesFixed) {
            BOOL ok;
            UINT copies = GetDlgItemInt(s->Dialog, PRN_COPIES, &ok, FALSE);
            if (!ok || !copies) {
                message(s, L"The 'Copies' value cannot be empty and must be a positive value.", PRN_COPIES);
                return FALSE;
            }
            s->Copies = copies > 9999 ? 9999 : copies;
        }
        if (IsDlgButtonChecked(s->Dialog, PRN_ALL) == BST_CHECKED)
            s->Range = 0;
        else if (IsDlgButtonChecked(s->Dialog, PRN_SELECTION) == BST_CHECKED)
            s->Range = 1;
        else if (s->Ex && IsDlgButtonChecked(s->Dialog, PRN_EX_CURRENT) == BST_CHECKED)
            s->Range = 3;
        else
            s->Range = 2;
        if (s->Range == 2) {
            if (s->Ex) {
                WCHAR text[256];
                GetDlgItemTextW(s->Dialog, PRN_FROM, text, 256);
                if (!parse_ranges(s, text))
                    return FALSE;
            } else {
                DWORD from, to;
                if (!page_number(s, PRN_FROM, &from, L"The 'From' value cannot be empty and must be a positive value.") ||
                    !page_number(s, PRN_TO, &to, L"The 'To' value cannot be empty and must be a positive value."))
                    return FALSE;
                if (from > to) {
                    message(s, L"The 'From' value cannot be greater than the 'To' value.", PRN_FROM);
                    return FALSE;
                }
                s->From = from;
                s->To = to;
            }
        }
        s->Collate = IsDlgButtonChecked(s->Dialog, PRN_COLLATE) == BST_CHECKED && s->Copies > 1;
    }
    s->ToFile = IsDlgButtonChecked(s->Dialog, PRN_TOFILE) == BST_CHECKED;
    return TRUE;
}

/* ------------------------------------------------------------------ the dialog procedure */

static void properties(PRINTSTATE *s)
{
    if (s->Current < 0 || !s->Devmode)
        return;
    const SPOOLFN *spool = PrnSpool();
    const PRN_ITEM *p = &s->List.Items[s->Current];
    HANDLE printer;
    if (!spool || !spool->OpenPrinter((LPWSTR)p->Name, &printer, nullptr))
        return;
    DEVMODEW *out = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)s->Devmode->dmSize + s->Devmode->dmDriverExtra);
    if (out) {
        LONG answer = spool->DocumentProperties(s->Dialog, printer, (LPWSTR)p->Name, out, s->Devmode,
                                                DM_IN_BUFFER | DM_IN_PROMPT | DM_OUT_BUFFER);
        if (answer == IDOK) {
            HeapFree(GetProcessHeap(), 0, s->Devmode);
            s->Devmode = out;
            out = nullptr;
        }
        if (out)
            HeapFree(GetProcessHeap(), 0, out);
    }
    spool->ClosePrinter(printer);
}

static BOOL call_callback(PRINTSTATE *s, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    if (!s->Callback)
        return FALSE;
    LRESULT r = 0;
    HRESULT hr = s->Callback->lpVtbl->HandleMessage(s->Callback, s->Dialog, message, wparam, lparam, &r);
    if (hr == S_OK) {
        *result = r;
        return TRUE;
    }
    return FALSE;
}

static void end_dialog(PRINTSTATE *s, int result)
{
    s->Result = result;
    s->Closed = TRUE;
    EndDialog(s->Dialog, result ? 1 : 0);
}

static BOOL command(PRINTSTATE *s, WORD id, WORD code)
{
    switch (id) {
    case IDOK:
    case PRN_APPLY:
        if (!take_values(s))
            return TRUE;
        end_dialog(s, id == IDOK ? 1 : 2);
        return TRUE;
    case IDCANCEL:
        g_cancelled = TRUE;
        end_dialog(s, 0);
        return TRUE;
    case PRN_NAME:
        if (code == CBN_SELCHANGE)
            printer_changed(s, (int)SendDlgItemMessageW(s->Dialog, PRN_NAME, CB_GETCURSEL, 0, 0));
        return TRUE;
    case PRN_PROPERTIES:
        properties(s);
        return TRUE;
    case PRN_ALL:
    case PRN_SELECTION:
    case PRN_PAGES:
    case PRN_EX_PAGES:
        pages_enable(s);
        return TRUE;
    case PRN_COPIES:
        if (code == EN_CHANGE && !s->CollateFixed) {
            BOOL ok;
            UINT copies = GetDlgItemInt(s->Dialog, PRN_COPIES, &ok, FALSE);
            enable(s, PRN_COLLATE, ok && copies > 1);
        }
        return TRUE;
    case pshHelp:
        if (s->Owner)
            SendMessageW(s->Owner, DlgMessage(&g_msg_help, HELPMSGSTRINGW), (WPARAM)s->Dialog, s->HookParam);
        return TRUE;
    }
    return FALSE;
}

/* the collate icon: sheets in order, or in stacks. */
static void draw_collate(const PRINTSTATE *s, const DRAWITEMSTRUCT *di)
{
    HDC dc = di->hDC;
    BOOL collated = IsDlgButtonChecked(s->Dialog, PRN_COLLATE) == BST_CHECKED;
    DlgFillRect(dc, &di->rcItem, GetSysColor(COLOR_BTNFACE));
    HBRUSH white = GetSysColorBrush(COLOR_WINDOW);
    for (int sheet = 0; sheet < 3; sheet++) {
        int base = di->rcItem.left + 2 + (collated ? sheet * 12 : (sheet % 2) * 18);
        for (int copy = 0; copy < (collated ? 1 : 2); copy++) {
            int x = base + copy * 4, y = di->rcItem.top + 2 + (collated ? copy : sheet) * 4;
            RECT r = {x, y, x + 12, y + 18};
            FillRect(dc, &r, white);
            FrameRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
    }
}

static INT_PTR init_dialog(HWND hwnd, PRINTSTATE *s)
{
    s->Dialog = hwnd;
    SetPropW(hwnd, L"PrintDlg", s);
    s->Services.Interface.lpVtbl = &g_services_vtbl;
    s->Services.State = s;
    fill_printers(s);
    show_printer(s);
    if (!s->Setup) {
        show_copies(s);
        show_range(s);
        enable(s, PRN_COPIES, TRUE);
    }
    update_copies(s);
    if (s->Flags & PD_SHOWHELP) {
        /* the Help button stays. */
    } else {
        HWND help = item(s, pshHelp);
        if (help) {
            EnableWindow(help, FALSE);
            ShowWindow(help, SW_HIDE);
        }
    }
    if (s->Flags & PD_HIDEPRINTTOFILE) {
        enable(s, PRN_TOFILE, FALSE);
        ShowWindow(item(s, PRN_TOFILE), SW_HIDE);
    } else {
        CheckDlgButton(hwnd, PRN_TOFILE, s->ToFile ? BST_CHECKED : BST_UNCHECKED);
        if (s->Flags & PD_DISABLEPRINTTOFILE)
            enable(s, PRN_TOFILE, FALSE);
    }
    if (s->Callback) {
        IObjectWithSite *site = nullptr;
        if (SUCCEEDED(s->Callback->lpVtbl->QueryInterface(s->Callback, &IID_IObjectWithSite, (void **)&site)) && site) {
            site->lpVtbl->SetSite(site, (IUnknown *)&s->Services.Interface);
            site->lpVtbl->Release(site);
        }
        s->Callback->lpVtbl->InitDone(s->Callback);
    }
    if (s->Hook)
        return (INT_PTR)s->Hook(hwnd, WM_INITDIALOG, 0, s->HookParam);
    return TRUE;
}

static INT_PTR CALLBACK print_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    PRINTSTATE *s = (PRINTSTATE *)GetPropW(hwnd, L"PrintDlg");
    if (!s) {
        if (message == WM_INITDIALOG)
            return init_dialog(hwnd, (PRINTSTATE *)lparam);
        DLGPROC early = g_hook_tls != TLS_OUT_OF_INDEXES ? (DLGPROC)TlsGetValue(g_hook_tls) : nullptr;
        return early ? early(hwnd, message, wparam, lparam) : 0;
    }
    INT_PTR result = s->Hook ? s->Hook(hwnd, message, wparam, lparam) : 0;
    if (result) {
        if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)
            g_cancelled = TRUE;
        return result;
    }
    LRESULT handled;
    if (call_callback(s, message, wparam, lparam, &handled)) {
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, (LONG_PTR)handled);
        return TRUE;
    }
    switch (message) {
    case WM_COMMAND:
        return command(s, LOWORD(wparam), HIWORD(wparam));
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lparam)->CtlID == PRN_COLLATE_ICON) {
            draw_collate(s, (DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        return FALSE;
    case WM_DESTROY:
        RemovePropW(hwnd, L"PrintDlg");
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ running it */

/* returns 0 for cancel, 1 for print, 2 for apply and -1 on failure. */
static int run_dialog(PRINTSTATE *s, const DLGSOURCE *source, DWORD *error)
{
    const void *given = DlgFindTemplate(source, error);
    if (*error)
        return -1;
    LPDLGTEMPLATEW built = given ? nullptr : build_template(s);
    if (!given && !built) {
        *error = CDERR_MEMALLOCFAILURE;
        return -1;
    }
    if (g_hook_tls == TLS_OUT_OF_INDEXES && !g_hook_once) {
        g_hook_once = 1;
        g_hook_tls = TlsAlloc();
    }
    if (g_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_hook_tls, (void *)s->Hook);
    g_cancelled = FALSE;
    INT_PTR answer = DialogBoxIndirectParamW(g_comdlg32, given ? (LPCDLGTEMPLATEW)given : built, s->Owner, print_proc,
                                             (LPARAM)s);
    if (g_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_hook_tls, nullptr);
    if (built)
        HeapFree(GetProcessHeap(), 0, built);
    if (answer == -1 || (answer == 0 && !g_cancelled && !s->Closed)) {
        *error = CDERR_DIALOGFAILURE;
        return -1;
    }
    return s->Result;
}

/* the first printer named by the structure's handles that exists, else the default one. */
static void choose_printer(PRINTSTATE *s, HGLOBAL devmode, HGLOBAL devnames)
{
    s->Current = -1;
    BOOL to_file = FALSE;
    int index;
    if (PrnDevnamesPrinter(devnames, &s->List, &index, &to_file)) {
        s->Current = index;
        s->ToFile = to_file;
    }
    if (s->Current < 0 && devmode) {
        DEVMODEW *dm = PrnCopyDevmode(devmode);
        if (dm) {
            s->Current = PrnFind(&s->List, dm->dmDeviceName);
            HeapFree(GetProcessHeap(), 0, dm);
        }
    }
    if (s->Current < 0)
        s->Current = s->List.Default;
}

/* the program's device mode when it is for this printer, else the defaults. */
static void initial_devmode(PRINTSTATE *s, HGLOBAL handle)
{
    if (s->Current < 0)
        return;
    DEVMODEW *given = PrnCopyDevmode(handle);
    if (given && lstrcmpiW(given->dmDeviceName, s->List.Items[s->Current].Name)) {
        HeapFree(GetProcessHeap(), 0, given);
        given = nullptr;
    }
    s->Devmode = PrnDevmode(s->Owner, &s->List.Items[s->Current], given);
    if (given)
        HeapFree(GetProcessHeap(), 0, given);
    if (s->Devmode && (s->Devmode->dmFields & DM_COPIES) && ((s->Flags & PD_USEDEVMODECOPIESANDCOLLATE) || handle))
        s->Copies = s->Devmode->dmCopies;
    if (s->Devmode && (s->Devmode->dmFields & DM_COLLATE) && ((s->Flags & PD_USEDEVMODECOPIESANDCOLLATE) || handle))
        s->Collate = s->Devmode->dmCollate != 0;
}

/* new DEVMODE and DEVNAMES handles, and the device context when asked. */
static BOOL make_results(PRINTSTATE *s, HGLOBAL *devmode_out, HGLOBAL *devnames_out, HDC *dc_out, DWORD flags)
{
    const PRN_ITEM *p = &s->List.Items[s->Current];
    DEVMODEW *dm = s->Devmode;
    if (dm) {
        /* without the flag the program does copies and collation, and the device mode has one copy. */
        if (!(flags & PD_USEDEVMODECOPIESANDCOLLATE)) {
            if (dm->dmFields & DM_COPIES)
                dm->dmCopies = 1;
            if (dm->dmFields & DM_COLLATE)
                dm->dmCollate = 0;
        } else {
            if (dm->dmFields & DM_COPIES)
                dm->dmCopies = (short)s->Copies;
            if (dm->dmFields & DM_COLLATE)
                dm->dmCollate = s->Collate ? 1 : 0;
        }
    }
    HGLOBAL names = PrnNewDevnamesHandle(p, s->Current == s->List.Default, s->ToFile);
    HGLOBAL mode = dm ? PrnNewDevmodeHandle(dm) : nullptr;
    if (!names || (dm && !mode)) {
        if (names)
            GlobalFree(names);
        if (mode)
            GlobalFree(mode);
        return FALSE;
    }
    *devmode_out = mode;
    *devnames_out = names;
    if (dc_out) {
        *dc_out = nullptr;
        if (flags & (PD_RETURNDC | PD_RETURNIC))
            *dc_out = PrnCreateDC(p, dm, (flags & PD_RETURNDC) == 0);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ PrintDlg */

static void free_state(PRINTSTATE *s)
{
    PrnFreeList(&s->List);
    if (s->Devmode)
        HeapFree(GetProcessHeap(), 0, s->Devmode);
    if (s->Ranges)
        HeapFree(GetProcessHeap(), 0, s->Ranges);
}

/* the default printer's answer, without a dialog. */
static BOOL return_default(PRINTSTATE *s, HGLOBAL *devmode, HGLOBAL *devnames, HDC *dc, DWORD flags)
{
    if (s->List.Default < 0) {
        DlgSetError(PDERR_NODEFAULTPRN);
        return FALSE;
    }
    s->Current = s->List.Default;
    s->Devmode = PrnDevmode(s->Owner, &s->List.Items[s->Current], nullptr);
    if (!make_results(s, devmode, devnames, dc, flags)) {
        DlgSetError(PDERR_NODEFAULTPRN);
        return FALSE;
    }
    return TRUE;
}

/*
 * the A structure has the wide layout; its DEVMODE and DEVNAMES handles are converted in and out.
 */
static BOOL print_dlg(PRINTDLGW *pd, BOOL ansi)
{
    DlgSetError(0);
    if (!pd) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (pd->lStructSize != sizeof(*pd)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if (pd->hwndOwner && !IsWindow(pd->hwndOwner)) {
        DlgSetError(CDERR_DIALOGFAILURE);
        return FALSE;
    }
    DWORD flags = pd->Flags;
    pd->hDC = nullptr;
    PRINTSTATE s;
    memset(&s, 0, sizeof(s));
    s.Ansi = ansi;
    s.Owner = pd->hwndOwner;
    HGLOBAL mode_in = pd->hDevMode, names_in = pd->hDevNames;
    if (flags & PD_RETURNDEFAULT) {
        if (mode_in || names_in) {
            DlgSetError(PDERR_RETDEFFAILURE);
            return FALSE;
        }
        PrnEnumerate(&s.List);
        HGLOBAL mode = nullptr, names = nullptr;
        BOOL ok = return_default(&s, &mode, &names, &pd->hDC, flags);
        free_state(&s);
        if (!ok)
            return FALSE;
        if (ansi) {
            HGLOBAL am = PrnDevmodeToAnsi(mode), an = PrnDevnamesToAnsi(names);
            GlobalFree(mode);
            GlobalFree(names);
            mode = am;
            names = an;
        }
        pd->hDevMode = mode;
        pd->hDevNames = names;
        return TRUE;
    }
    DWORD error = 0;
    if (!DlgHookValid(flags, PD_ENABLEPRINTHOOK, pd->lpfnPrintHook, &error) ||
        (!(flags & PD_PRINTSETUP) ? FALSE : !DlgHookValid(flags, PD_ENABLESETUPHOOK, pd->lpfnSetupHook, &error))) {
        DlgSetError(error);
        return FALSE;
    }
    if (!(flags & PD_ENABLEPRINTHOOK))
        pd->lpfnPrintHook = nullptr;
    if (!(flags & PD_ENABLESETUPHOOK))
        pd->lpfnSetupHook = nullptr;
    HGLOBAL wide_mode = ansi ? PrnDevmodeFromAnsi(mode_in) : mode_in;
    HGLOBAL wide_names = ansi ? PrnDevnamesFromAnsi(names_in) : names_in;
    s.Flags = flags;
    s.Setup = (flags & PD_PRINTSETUP) != 0;
    s.Min = pd->nMinPage;
    s.Max = pd->nMaxPage;
    if (!s.Min && !s.Max) {
        s.Min = 1;
        s.Max = 0xFFFF;
    }
    s.From = pd->nFromPage;
    s.To = pd->nToPage;
    s.Copies = pd->nCopies ? pd->nCopies : 1;
    s.Collate = (flags & PD_COLLATE) != 0;
    s.Range = (flags & PD_PAGENUMS) ? 2 : ((flags & PD_SELECTION) ? 1 : 0);
    s.ToFile = (flags & PD_PRINTTOFILE) != 0;
    s.Hook = (DLGPROC)((s.Setup && (flags & PD_ENABLESETUPHOOK)) ? pd->lpfnSetupHook
                                                                  : ((flags & PD_ENABLEPRINTHOOK) ? pd->lpfnPrintHook : nullptr));
    s.HookParam = (LPARAM)pd;
    if (!PrnEnumerate(&s.List) || !s.List.Count) {
        DlgSetError(PDERR_NODEVICES);
        free_state(&s);
        if (ansi) {
            if (wide_mode)
                GlobalFree(wide_mode);
            if (wide_names)
                GlobalFree(wide_names);
        }
        return FALSE;
    }
    choose_printer(&s, wide_mode, wide_names);
    initial_devmode(&s, wide_mode);
    DLGSOURCE source = {flags, s.Setup ? PD_ENABLESETUPTEMPLATE : PD_ENABLEPRINTTEMPLATE,
                        s.Setup ? PD_ENABLESETUPTEMPLATEHANDLE : PD_ENABLEPRINTTEMPLATEHANDLE,
                        pd->hInstance, s.Setup ? pd->lpSetupTemplateName : pd->lpPrintTemplateName, ansi};
    if (flags & PD_ENABLEPRINTTEMPLATEHANDLE)
        source.Instance = (HINSTANCE)pd->hPrintTemplate;
    if (flags & PD_ENABLESETUPTEMPLATEHANDLE)
        source.Instance = (HINSTANCE)pd->hSetupTemplate;
    int result = run_dialog(&s, &source, &error);
    BOOL ok = FALSE;
    if (result < 0) {
        DlgSetError(error);
    } else if (result > 0) {
        HGLOBAL mode = nullptr, names = nullptr;
        if (make_results(&s, &mode, &names, &pd->hDC, flags)) {
            if (ansi) {
                HGLOBAL am = PrnDevmodeToAnsi(mode), an = PrnDevnamesToAnsi(names);
                GlobalFree(mode);
                GlobalFree(names);
                mode = am;
                names = an;
            }
            /* like windows, the program's handles are replaced. */
            if (mode_in)
                GlobalFree(mode_in);
            if (names_in)
                GlobalFree(names_in);
            pd->hDevMode = mode;
            pd->hDevNames = names;
            DWORD out = flags & ~(DWORD)(PD_PAGENUMS | PD_SELECTION | PD_COLLATE | PD_PRINTTOFILE);
            if (s.Range == 2)
                out |= PD_PAGENUMS;
            else if (s.Range == 1)
                out |= PD_SELECTION;
            if (s.Collate && !(flags & PD_USEDEVMODECOPIESANDCOLLATE))
                out |= PD_COLLATE;
            if (s.ToFile)
                out |= PD_PRINTTOFILE;
            pd->Flags = out;
            if (s.Range == 2) {
                pd->nFromPage = (WORD)s.From;
                pd->nToPage = (WORD)s.To;
            } else {
                pd->nFromPage = pd->nToPage = 0xFFFF;
            }
            pd->nCopies = (flags & PD_USEDEVMODECOPIESANDCOLLATE) ? (WORD)s.Copies : (WORD)s.Copies;
            ok = TRUE;
        } else {
            DlgSetError(CDERR_MEMALLOCFAILURE);
        }
    }
    if (ansi) {
        if (wide_mode)
            GlobalFree(wide_mode);
        if (wide_names)
            GlobalFree(wide_names);
    }
    free_state(&s);
    return ok;
}

CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd)
{
    return print_dlg(pd, FALSE);
}

CDAPI BOOL WINAPI PrintDlgA(LPPRINTDLGA pd)
{
    /* the ANSI structure has the wide layout except for the template names. */
    return print_dlg((PRINTDLGW *)pd, TRUE);
}

/* ------------------------------------------------------------------ PrintDlgEx */

static HRESULT print_dlg_ex(PRINTDLGEXW *pd, BOOL ansi)
{
    DlgSetError(0);
    if (!pd || (pd->Flags & 0x80000000u)) {
        DlgSetError(CDERR_INITIALIZATION);
        return E_INVALIDARG;
    }
    if (pd->lStructSize != sizeof(*pd)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return E_INVALIDARG;
    }
    if (!pd->hwndOwner || !IsWindow(pd->hwndOwner)) {
        DlgSetError(CDERR_DIALOGFAILURE);
        return E_HANDLE;
    }
    if ((pd->Flags & PDEX_INVALID_FLAGS) || pd->Flags2 || (pd->ExclusionFlags & 0xFFFF7EFFu) || pd->dwResultAction) {
        DlgSetError(PDERR_INITFAILURE);
        return E_INVALIDARG;
    }
    DWORD flags = pd->Flags;
    if (flags & PD_ENABLEPRINTTEMPLATEHANDLE) {
        if (!pd->hInstance) {
            DlgSetError(CDERR_NOHINSTANCE);
            return E_HANDLE;
        }
    } else if (flags & PD_ENABLEPRINTTEMPLATE) {
        if (!pd->lpPrintTemplateName) {
            DlgSetError(CDERR_NOTEMPLATE);
            return E_POINTER;
        }
        if (!pd->hInstance) {
            DlgSetError(CDERR_NOHINSTANCE);
            return E_HANDLE;
        }
    } else if (pd->lpPrintTemplateName || pd->hInstance) {
        DlgSetError(PDERR_INITFAILURE);
        return E_INVALIDARG;
    }
    if ((pd->nPropertyPages && !pd->lphPropertyPages) ||
        (pd->nStartPage != START_PAGE_GENERAL && pd->nStartPage >= pd->nPropertyPages)) {
        DlgSetError(PDERR_INITFAILURE);
        return E_INVALIDARG;
    }
    if (!(flags & PD_NOPAGENUMS)) {
        BOOL bad = pd->nMinPage > pd->nMaxPage || pd->nPageRanges > pd->nMaxPageRanges || !pd->nMaxPageRanges ||
                   !pd->lpPageRanges;
        for (DWORD i = 0; !bad && i < pd->nPageRanges; i++) {
            const PRINTPAGERANGE *r = &pd->lpPageRanges[i];
            bad = r->nFromPage < pd->nMinPage || r->nFromPage > pd->nMaxPage || r->nToPage < pd->nMinPage ||
                  r->nToPage > pd->nMaxPage;
        }
        if (bad) {
            DlgSetError(PDERR_INITFAILURE);
            return E_INVALIDARG;
        }
    }
    pd->hDC = nullptr;
    PRINTSTATE s;
    memset(&s, 0, sizeof(s));
    s.Ex = TRUE;
    s.Ansi = ansi;
    s.Owner = pd->hwndOwner;
    s.Flags = flags;
    HGLOBAL mode_in = pd->hDevMode, names_in = pd->hDevNames;
    if (flags & PD_RETURNDEFAULT) {
        PrnEnumerate(&s.List);
        HGLOBAL mode = nullptr, names = nullptr;
        BOOL ok = return_default(&s, &mode, &names, &pd->hDC, flags);
        free_state(&s);
        if (!ok)
            return E_FAIL;
        pd->hDevMode = mode;
        pd->hDevNames = names;
        pd->dwResultAction = PD_RESULT_PRINT;
        return S_OK;
    }
    HGLOBAL wide_mode = ansi ? PrnDevmodeFromAnsi(mode_in) : mode_in;
    HGLOBAL wide_names = ansi ? PrnDevnamesFromAnsi(names_in) : names_in;
    s.Min = pd->nMinPage;
    s.Max = pd->nMaxPage;
    s.MaxRanges = pd->nMaxPageRanges;
    s.Copies = pd->nCopies ? pd->nCopies : 1;
    s.Collate = (flags & PD_COLLATE) != 0;
    s.Range = (flags & PD_PAGENUMS) ? 2 : ((flags & PD_SELECTION) ? 1 : ((flags & PD_CURRENTPAGE) ? 3 : 0));
    s.ToFile = (flags & PD_PRINTTOFILE) != 0;
    s.HookParam = (LPARAM)pd;
    if (pd->lpCallback)
        pd->lpCallback->lpVtbl->QueryInterface(pd->lpCallback, &IID_IPrintDialogCallback, (void **)&s.Callback);
    if (!(flags & PD_NOPAGENUMS)) {
        s.Ranges = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)pd->nMaxPageRanges * sizeof(PRINTPAGERANGE));
        if (s.Ranges) {
            for (DWORD i = 0; i < pd->nPageRanges; i++)
                s.Ranges[i] = pd->lpPageRanges[i];
            s.RangeCount = pd->nPageRanges;
        }
    }
    HRESULT hr = S_OK;
    if (!PrnEnumerate(&s.List) || !s.List.Count) {
        DlgSetError(PDERR_NODEVICES);
        hr = E_FAIL;
    } else {
        choose_printer(&s, wide_mode, wide_names);
        initial_devmode(&s, wide_mode);
        DLGSOURCE source = {flags, PD_ENABLEPRINTTEMPLATE, PD_ENABLEPRINTTEMPLATEHANDLE, pd->hInstance,
                            pd->lpPrintTemplateName, ansi};
        DWORD error = 0;
        int result = run_dialog(&s, &source, &error);
        if (result < 0) {
            DlgSetError(error);
            hr = E_FAIL;
        } else if (result == 0) {
            pd->dwResultAction = PD_RESULT_CANCEL;
        } else {
            HGLOBAL mode = nullptr, names = nullptr;
            if (make_results(&s, &mode, &names, &pd->hDC, flags)) {
                if (ansi) {
                    HGLOBAL am = PrnDevmodeToAnsi(mode), an = PrnDevnamesToAnsi(names);
                    GlobalFree(mode);
                    GlobalFree(names);
                    mode = am;
                    names = an;
                }
                if (mode_in)
                    GlobalFree(mode_in);
                if (names_in)
                    GlobalFree(names_in);
                pd->hDevMode = mode;
                pd->hDevNames = names;
                DWORD out = flags & ~(DWORD)(PD_PAGENUMS | PD_SELECTION | PD_COLLATE | PD_PRINTTOFILE | PD_CURRENTPAGE);
                if (s.Range == 2) {
                    out |= PD_PAGENUMS;
                    for (DWORD i = 0; i < s.RangeCount && i < pd->nMaxPageRanges; i++)
                        pd->lpPageRanges[i] = s.Ranges[i];
                    pd->nPageRanges = s.RangeCount;
                } else if (s.Range == 1) {
                    out |= PD_SELECTION;
                } else if (s.Range == 3) {
                    out |= PD_CURRENTPAGE;
                }
                if (s.Collate && !(flags & PD_USEDEVMODECOPIESANDCOLLATE))
                    out |= PD_COLLATE;
                if (s.ToFile)
                    out |= PD_PRINTTOFILE;
                pd->Flags = out;
                pd->nCopies = s.Copies;
                pd->dwResultAction = (DWORD)result == 2 ? PD_RESULT_APPLY : PD_RESULT_PRINT;
            } else {
                DlgSetError(CDERR_MEMALLOCFAILURE);
                hr = E_OUTOFMEMORY;
            }
        }
    }
    if (s.Callback)
        s.Callback->lpVtbl->Release(s.Callback);
    if (ansi) {
        if (wide_mode)
            GlobalFree(wide_mode);
        if (wide_names)
            GlobalFree(wide_names);
    }
    free_state(&s);
    return hr;
}

CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd)
{
    return print_dlg_ex(pd, FALSE);
}

CDAPI HRESULT WINAPI PrintDlgExA(LPPRINTDLGEXA pd)
{
    return print_dlg_ex((PRINTDLGEXW *)pd, TRUE);
}
