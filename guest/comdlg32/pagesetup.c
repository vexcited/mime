/*
 * PageSetupDlg: the paper, the orientation and the margins of the page, with a sample of the page. the sizes
 * are in thousandths of an inch or hundredths of a millimeter, as the flags of the structure say and, when
 * they do not, as the measurement system of the locale does.
 */
#include "printcommon.h"

#define PSD_VALID_MASK_OUT 0xFBD15040u

typedef struct _PAGEDLG {
    PAGESETUPDLGW *Psd;
    BOOL Ansi;
    HWND Dialog;
    PRN_LIST List;
    int Current;
    DEVMODEW *Devmode;
    BOOL Metric;
    /* the paper of the printer. */
    int PaperCount;
    WORD Papers[64];
    POINT Sizes[64];
    WCHAR Names[64][64];
    int Width, Height;
    RECT Margin, MinMargin;
    BOOL Closed;
    DLGPROC Hook;
    LPPAGEPAINTHOOK PaintHook;
} PAGEDLG;

static UINT g_msg_help;
static BOOL g_cancelled;
static DWORD g_hook_tls = TLS_OUT_OF_INDEXES;
static LONG g_hook_once;

/* ------------------------------------------------------------------ the numbers */

/* a length in the dialog's unit as text: inches with up to three decimals, millimeters with one. */
static void length_text(const PAGEDLG *d, int value, WCHAR *text)
{
    int whole, fraction, digits;
    if (d->Metric) {
        whole = value / 100;
        fraction = value % 100;
        digits = 2;
    } else {
        whole = value / 1000;
        fraction = value % 1000;
        digits = 3;
    }
    if (!fraction) {
        wsprintfW(text, L"%d", whole);
        return;
    }
    WCHAR part[8];
    wsprintfW(part, digits == 2 ? L"%02d" : L"%03d", fraction);
    int n = lstrlenW(part);
    while (n > 1 && part[n - 1] == L'0')
        part[--n] = 0;
    wsprintfW(text, L"%d.%s", whole, part);
}

static BOOL parse_length(const PAGEDLG *d, LPCWSTR text, int *value)
{
    int whole = 0, fraction = 0, scale = d->Metric ? 100 : 1000;
    BOOL any = FALSE;
    const WCHAR *p = text;
    while (*p == L' ')
        p++;
    while (*p >= L'0' && *p <= L'9') {
        whole = whole * 10 + (*p++ - L'0');
        any = TRUE;
        if (whole > 100000)
            return FALSE;
    }
    if (*p == L'.' || *p == L',') {
        p++;
        int place = scale / 10;
        while (*p >= L'0' && *p <= L'9') {
            if (place > 0) {
                fraction += (*p - L'0') * place;
                place /= 10;
            }
            p++;
            any = TRUE;
        }
    }
    while (*p == L' ')
        p++;
    /* an optional unit after the number is ignored: in, ", mm. */
    while (*p == L'"' || *p == L'i' || *p == L'n' || *p == L'm')
        p++;
    if (!any || *p)
        return FALSE;
    *value = whole * scale + fraction;
    return TRUE;
}

/* the paper in the dialog's unit, turned for the orientation. */
static void paper_size(PAGEDLG *d, int *w, int *h)
{
    int sw = 2100, sh = 2970;
    if (d->Devmode) {
        for (int i = 0; i < d->PaperCount; i++) {
            if (d->Papers[i] == (WORD)d->Devmode->dmPaperSize) {
                sw = d->Sizes[i].x;
                sh = d->Sizes[i].y;
            }
        }
        if ((d->Devmode->dmFields & (DM_PAPERWIDTH | DM_PAPERLENGTH)) == (DM_PAPERWIDTH | DM_PAPERLENGTH) &&
            d->Devmode->dmPaperSize == DMPAPER_USER) {
            sw = d->Devmode->dmPaperWidth;
            sh = d->Devmode->dmPaperLength;
        }
        if (d->Devmode->dmOrientation == DMORIENT_LANDSCAPE) {
            int t = sw;
            sw = sh;
            sh = t;
        }
    }
    /* tenths of a millimeter to thousandths of an inch or hundredths of a millimeter. */
    if (d->Metric) {
        *w = sw * 10;
        *h = sh * 10;
    } else {
        *w = MulDiv(sw, 1000, 254);
        *h = MulDiv(sh, 1000, 254);
    }
}

static void load_papers(PAGEDLG *d)
{
    d->PaperCount = 0;
    if (d->Current < 0)
        return;
    const PRN_ITEM *p = &d->List.Items[d->Current];
    const SPOOLFN *spool = PrnSpool();
    int n = PrnCapability(p, DC_PAPERS);
    if (!spool || n <= 0)
        return;
    if (n > 64)
        n = 64;
    static WCHAR names[64 * 64];
    if (spool->DeviceCapabilities(p->Name, p->Port, DC_PAPERS, (LPWSTR)d->Papers, nullptr) != n ||
        spool->DeviceCapabilities(p->Name, p->Port, DC_PAPERSIZE, (LPWSTR)d->Sizes, nullptr) != n ||
        spool->DeviceCapabilities(p->Name, p->Port, DC_PAPERNAMES, names, nullptr) != n)
        return;
    for (int i = 0; i < n; i++)
        lstrcpynW(d->Names[i], names + 64 * i, 64);
    d->PaperCount = n;
}

/* ------------------------------------------------------------------ the controls */

static HWND item(const PAGEDLG *d, int id)
{
    return GetDlgItem(d->Dialog, id);
}

static void show_margins(PAGEDLG *d)
{
    WCHAR text[24];
    int ids[4] = {PSD_LEFT, PSD_TOP, PSD_RIGHT, PSD_BOTTOM};
    int values[4] = {d->Margin.left, d->Margin.top, d->Margin.right, d->Margin.bottom};
    for (int i = 0; i < 4; i++) {
        length_text(d, values[i], text);
        SetDlgItemTextW(d->Dialog, ids[i], text);
    }
}

static void fill_paper(PAGEDLG *d)
{
    HWND combo = item(d, PSD_SIZE);
    if (!combo)
        return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < d->PaperCount; i++) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)d->Names[i]);
        if (d->Devmode && d->Papers[i] == (WORD)d->Devmode->dmPaperSize)
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0);
    }
    HWND source = item(d, PSD_SOURCE);
    if (source) {
        SendMessageW(source, CB_RESETCONTENT, 0, 0);
        SendMessageW(source, CB_ADDSTRING, 0, (LPARAM)L"Automatically Select");
        SendMessageW(source, CB_SETCURSEL, 0, 0);
    }
}

static void refresh_preview(PAGEDLG *d)
{
    paper_size(d, &d->Width, &d->Height);
    HWND page = item(d, PSD_PAGE);
    if (page)
        InvalidateRect(page, nullptr, TRUE);
}

/* the page, as small as the box allows, with the margin as dotted lines and a few lines of text. */
static void draw_page(PAGEDLG *d, const DRAWITEMSTRUCT *di)
{
    HDC dc = di->hDC;
    RECT box = di->rcItem;
    DlgFillRect(dc, &box, GetSysColor(COLOR_BTNFACE));
    int pw = d->Width > 0 ? d->Width : 1, ph = d->Height > 0 ? d->Height : 1;
    int bw = box.right - box.left - 10, bh = box.bottom - box.top - 10;
    int w = bw, h = MulDiv(bw, ph, pw);
    if (h > bh) {
        h = bh;
        w = MulDiv(bh, pw, ph);
    }
    RECT page = {box.left + (box.right - box.left - w) / 2, box.top + (box.bottom - box.top - h) / 2, 0, 0};
    page.right = page.left + w;
    page.bottom = page.top + h;
    RECT shadow = {page.left + 3, page.top + 3, page.right + 3, page.bottom + 3};
    DlgFillRect(dc, &shadow, RGB(96, 96, 96));
    DlgFillRect(dc, &page, RGB(255, 255, 255));
    FrameRect(dc, &page, (HBRUSH)GetStockObject(BLACK_BRUSH));
    RECT m = {page.left + MulDiv(d->Margin.left, w, pw), page.top + MulDiv(d->Margin.top, h, ph),
              page.right - MulDiv(d->Margin.right, w, pw), page.bottom - MulDiv(d->Margin.bottom, h, ph)};
    if (m.right > m.left && m.bottom > m.top) {
        HPEN pen = CreatePen(PS_DOT, 1, RGB(128, 128, 128));
        HGDIOBJ old = SelectObject(dc, pen);
        HGDIOBJ brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, m.left, m.top, m.right, m.bottom);
        SelectObject(dc, brush);
        SelectObject(dc, old);
        DeleteObject(pen);
        /* lines of text that stand for the page, a hook may paint over them. */
        BOOL handled = FALSE;
        if (d->PaintHook) {
            RECT r = page;
            handled = d->PaintHook(d->Dialog, WM_PSD_FULLPAGERECT, (WPARAM)dc, (LPARAM)&r) != 0;
            r = m;
            if (!handled)
                handled = d->PaintHook(d->Dialog, WM_PSD_MARGINRECT, (WPARAM)dc, (LPARAM)&r) != 0;
            if (!handled)
                handled = d->PaintHook(d->Dialog, WM_PSD_GREEKTEXTRECT, (WPARAM)dc, (LPARAM)&r) != 0;
        }
        if (!handled) {
            HPEN text = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
            old = SelectObject(dc, text);
            for (int y = m.top + 3; y + 2 < m.bottom; y += 4) {
                MoveToEx(dc, m.left + 1, y, nullptr);
                LineTo(dc, m.right - (((y - m.top) / 4) % 5 == 4 ? (m.right - m.left) / 3 : 1), y);
            }
            SelectObject(dc, old);
            DeleteObject(text);
        }
    }
}

/* ------------------------------------------------------------------ taking values */

static void message(PAGEDLG *d, LPCWSTR text, int focus)
{
    WCHAR title[64];
    GetWindowTextW(d->Dialog, title, 64);
    MessageBoxW(d->Dialog, text, title, MB_OK | MB_ICONINFORMATION);
    HWND w = item(d, focus);
    if (w)
        SetFocus(w);
}

static BOOL take_margins(PAGEDLG *d)
{
    int ids[4] = {PSD_LEFT, PSD_TOP, PSD_RIGHT, PSD_BOTTOM};
    int values[4];
    for (int i = 0; i < 4; i++) {
        WCHAR text[32];
        GetDlgItemTextW(d->Dialog, ids[i], text, 32);
        if (!parse_length(d, text, &values[i])) {
            message(d, L"The margins overlap or they are off the paper.\nEnter a different margin size.", ids[i]);
            return FALSE;
        }
    }
    RECT m = {values[0], values[1], values[2], values[3]};
    if (m.left + m.right >= d->Width || m.top + m.bottom >= d->Height || m.left < d->MinMargin.left ||
        m.top < d->MinMargin.top || m.right < d->MinMargin.right || m.bottom < d->MinMargin.bottom) {
        message(d, L"The margins overlap or they are off the paper.\nEnter a different margin size.", PSD_LEFT);
        return FALSE;
    }
    d->Margin = m;
    return TRUE;
}

static void paper_changed(PAGEDLG *d)
{
    LRESULT i = SendDlgItemMessageW(d->Dialog, PSD_SIZE, CB_GETCURSEL, 0, 0);
    if (i < 0 || i >= d->PaperCount || !d->Devmode)
        return;
    d->Devmode->dmPaperSize = (short)d->Papers[i];
    d->Devmode->dmFields |= DM_PAPERSIZE;
    lstrcpynW(d->Devmode->dmFormName, d->Names[i], CCHFORMNAME);
    refresh_preview(d);
}

static void printer_properties(PAGEDLG *d)
{
    if (d->Current < 0 || !d->Devmode)
        return;
    const SPOOLFN *spool = PrnSpool();
    const PRN_ITEM *p = &d->List.Items[d->Current];
    HANDLE printer;
    if (!spool || !spool->OpenPrinter((LPWSTR)p->Name, &printer, nullptr))
        return;
    DEVMODEW *out = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)d->Devmode->dmSize + d->Devmode->dmDriverExtra);
    if (out) {
        if (spool->DocumentProperties(d->Dialog, printer, (LPWSTR)p->Name, out, d->Devmode,
                                      DM_IN_BUFFER | DM_IN_PROMPT | DM_OUT_BUFFER) == IDOK) {
            HeapFree(GetProcessHeap(), 0, d->Devmode);
            d->Devmode = out;
            out = nullptr;
            fill_paper(d);
            CheckRadioButton(d->Dialog, PSD_PORTRAIT, PSD_LANDSCAPE,
                             d->Devmode->dmOrientation == DMORIENT_LANDSCAPE ? PSD_LANDSCAPE : PSD_PORTRAIT);
            refresh_preview(d);
        }
        if (out)
            HeapFree(GetProcessHeap(), 0, out);
    }
    spool->ClosePrinter(printer);
}

static BOOL command(PAGEDLG *d, WORD id, WORD code)
{
    switch (id) {
    case IDOK:
        if (!take_margins(d))
            return TRUE;
        d->Closed = TRUE;
        EndDialog(d->Dialog, 1);
        return TRUE;
    case IDCANCEL:
        g_cancelled = TRUE;
        d->Closed = TRUE;
        EndDialog(d->Dialog, 0);
        return TRUE;
    case PSD_SIZE:
        if (code == CBN_SELCHANGE)
            paper_changed(d);
        return TRUE;
    case PSD_PORTRAIT:
    case PSD_LANDSCAPE:
        if (d->Devmode) {
            d->Devmode->dmOrientation = id == PSD_LANDSCAPE ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            d->Devmode->dmFields |= DM_ORIENTATION;
            /* the margins turn with the page. */
            refresh_preview(d);
        }
        return TRUE;
    case PSD_LEFT:
    case PSD_RIGHT:
    case PSD_TOP:
    case PSD_BOTTOM:
        if (code == EN_CHANGE) {
            int ids[4] = {PSD_LEFT, PSD_TOP, PSD_RIGHT, PSD_BOTTOM};
            int *fields[4] = {(int *)&d->Margin.left, (int *)&d->Margin.top, (int *)&d->Margin.right,
                              (int *)&d->Margin.bottom};
            for (int i = 0; i < 4; i++) {
                if (ids[i] == id) {
                    WCHAR text[32];
                    int v;
                    GetDlgItemTextW(d->Dialog, id, text, 32);
                    if (parse_length(d, text, &v)) {
                        *fields[i] = v;
                        refresh_preview(d);
                    }
                }
            }
        }
        return TRUE;
    case PSD_PRINTER:
        printer_properties(d);
        return TRUE;
    case pshHelp:
        if (d->Psd->hwndOwner)
            SendMessageW(d->Psd->hwndOwner, DlgMessage(&g_msg_help, HELPMSGSTRINGW), (WPARAM)d->Dialog,
                         (LPARAM)d->Psd);
        return TRUE;
    }
    return FALSE;
}

static INT_PTR init_dialog(HWND hwnd, PAGEDLG *d)
{
    d->Dialog = hwnd;
    SetPropW(hwnd, L"MimePageDlg", d);
    fill_paper(d);
    refresh_preview(d);
    CheckRadioButton(hwnd, PSD_PORTRAIT, PSD_LANDSCAPE,
                     d->Devmode && d->Devmode->dmOrientation == DMORIENT_LANDSCAPE ? PSD_LANDSCAPE : PSD_PORTRAIT);
    show_margins(d);
    DWORD f = d->Psd->Flags;
    if (f & PSD_DISABLEMARGINS) {
        for (int id = PSD_LEFT; id <= PSD_BOTTOM; id++)
            EnableWindow(item(d, id), FALSE);
    }
    if (f & PSD_DISABLEORIENTATION) {
        EnableWindow(item(d, PSD_PORTRAIT), FALSE);
        EnableWindow(item(d, PSD_LANDSCAPE), FALSE);
    }
    if (f & PSD_DISABLEPAPER) {
        EnableWindow(item(d, PSD_SIZE), FALSE);
        EnableWindow(item(d, PSD_SOURCE), FALSE);
    }
    if (f & PSD_DISABLEPRINTER)
        EnableWindow(item(d, PSD_PRINTER), FALSE);
    if (!(f & PSD_SHOWHELP)) {
        HWND help = item(d, pshHelp);
        if (help) {
            EnableWindow(help, FALSE);
            ShowWindow(help, SW_HIDE);
        }
    }
    if (d->Hook)
        return (INT_PTR)d->Hook(hwnd, WM_INITDIALOG, 0, (LPARAM)d->Psd);
    return TRUE;
}

static INT_PTR CALLBACK page_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAGEDLG *d = (PAGEDLG *)GetPropW(hwnd, L"MimePageDlg");
    if (!d) {
        if (message == WM_INITDIALOG)
            return init_dialog(hwnd, (PAGEDLG *)lparam);
        DLGPROC early = g_hook_tls != TLS_OUT_OF_INDEXES ? (DLGPROC)TlsGetValue(g_hook_tls) : nullptr;
        return early ? early(hwnd, message, wparam, lparam) : 0;
    }
    INT_PTR result = d->Hook ? d->Hook(hwnd, message, wparam, lparam) : 0;
    if (result) {
        if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)
            g_cancelled = TRUE;
        return result;
    }
    switch (message) {
    case WM_COMMAND:
        return command(d, LOWORD(wparam), HIWORD(wparam));
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lparam)->CtlID == PSD_PAGE) {
            draw_page(d, (DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        return FALSE;
    case WM_DESTROY:
        RemovePropW(hwnd, L"MimePageDlg");
        return FALSE;
    }
    return FALSE;
}

static LPDLGTEMPLATEW build_template(void)
{
    static const DWORD tab = WS_VISIBLE | WS_TABSTOP;
    static const CTL controls[] = {
        {CTL_STATIC, L"", PSD_PAGE, WS_VISIBLE | SS_OWNERDRAW, 70, 8, 100, 90},
        {CTL_BUTTON, L"Paper", PSD_GROUP_PAPER, WS_VISIBLE | BS_GROUPBOX, 8, 102, 224, 56},
        {CTL_STATIC, L"Si&ze:", PSD_SIZE_LABEL, WS_VISIBLE | SS_LEFT, 16, 118, 36, 9},
        {CTL_COMBOBOX, L"", PSD_SIZE, tab | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 64, 116, 160, 100},
        {CTL_STATIC, L"&Source:", PSD_SOURCE_LABEL, WS_VISIBLE | SS_LEFT, 16, 138, 36, 9},
        {CTL_COMBOBOX, L"", PSD_SOURCE, tab | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 64, 136, 160, 60},
        {CTL_BUTTON, L"Orientation", PSD_GROUP_ORIENTATION, WS_VISIBLE | BS_GROUPBOX, 8, 164, 64, 56},
        {CTL_BUTTON, L"P&ortrait", PSD_PORTRAIT, tab | BS_AUTORADIOBUTTON | WS_GROUP, 16, 178, 52, 12},
        {CTL_BUTTON, L"L&andscape", PSD_LANDSCAPE, WS_VISIBLE | BS_AUTORADIOBUTTON, 16, 198, 52, 12},
        {CTL_BUTTON, L"Margins", PSD_GROUP_MARGINS, WS_VISIBLE | BS_GROUPBOX, 80, 164, 152, 56},
        {CTL_STATIC, L"&Left:", PSD_LEFT_LABEL, WS_VISIBLE | SS_LEFT, 88, 180, 30, 9},
        {CTL_EDIT, L"", PSD_LEFT, tab | WS_BORDER | ES_AUTOHSCROLL, 120, 178, 32, 12},
        {CTL_STATIC, L"&Right:", PSD_RIGHT_LABEL, WS_VISIBLE | SS_LEFT, 164, 180, 30, 9},
        {CTL_EDIT, L"", PSD_RIGHT, tab | WS_BORDER | ES_AUTOHSCROLL, 196, 178, 32, 12},
        {CTL_STATIC, L"&Top:", PSD_TOP_LABEL, WS_VISIBLE | SS_LEFT, 88, 200, 30, 9},
        {CTL_EDIT, L"", PSD_TOP, tab | WS_BORDER | ES_AUTOHSCROLL, 120, 198, 32, 12},
        {CTL_STATIC, L"&Bottom:", PSD_BOTTOM_LABEL, WS_VISIBLE | SS_LEFT, 164, 200, 30, 9},
        {CTL_EDIT, L"", PSD_BOTTOM, tab | WS_BORDER | ES_AUTOHSCROLL, 196, 198, 32, 12},
        {CTL_BUTTON, L"OK", IDOK, tab | BS_DEFPUSHBUTTON, 132, 228, 48, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, tab | BS_PUSHBUTTON, 184, 228, 48, 14},
        {CTL_BUTTON, L"Pr&inter...", PSD_PRINTER, tab | BS_PUSHBUTTON, 76, 228, 52, 14},
        {CTL_BUTTON, L"&Help", pshHelp, tab | BS_PUSHBUTTON, 8, 228, 48, 14},
    };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | WS_VISIBLE;
    return DlgBuild(L"Page Setup", style, 240, 250, controls, sizeof(controls) / sizeof(controls[0]));
}

/* ------------------------------------------------------------------ the function */

static BOOL page_setup(PAGESETUPDLGW *psd, BOOL ansi)
{
    DlgSetError(0);
    if (!psd) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (psd->lStructSize != sizeof(*psd)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    DWORD flags = psd->Flags;
    if ((flags & PSD_RETURNDEFAULT) && (psd->hDevNames || psd->hDevMode)) {
        DlgSetError(PDERR_RETDEFFAILURE);
        return FALSE;
    }
    if ((flags & PSD_VALID_MASK_OUT) || (flags & (PSD_INTHOUSANDTHSOFINCHES | PSD_INHUNDREDTHSOFMILLIMETERS)) ==
                                            (PSD_INTHOUSANDTHSOFINCHES | PSD_INHUNDREDTHSOFMILLIMETERS) ||
        ((flags & (PSD_MINMARGINS | PSD_MARGINS)) == (PSD_MINMARGINS | PSD_MARGINS) &&
         (psd->rtMargin.left < psd->rtMinMargin.left || psd->rtMargin.top < psd->rtMinMargin.top ||
          psd->rtMargin.right < psd->rtMinMargin.right || psd->rtMargin.bottom < psd->rtMinMargin.bottom))) {
        DlgSetError(PDERR_INITFAILURE);
        return FALSE;
    }
    DWORD error = 0;
    if (!DlgHookValid(flags, PSD_ENABLEPAGESETUPHOOK, psd->lpfnPageSetupHook, &error) ||
        !DlgHookValid(flags, PSD_ENABLEPAGEPAINTHOOK, psd->lpfnPagePaintHook, &error)) {
        DlgSetError(error);
        return FALSE;
    }
    if (!(flags & PSD_ENABLEPAGESETUPHOOK))
        psd->lpfnPageSetupHook = nullptr;
    if (!(flags & PSD_ENABLEPAGEPAINTHOOK))
        psd->lpfnPagePaintHook = nullptr;

    /* the unit, from the locale when the flags do not say, and the margins' defaults. */
    DWORD saved_flags = flags;
    POINT saved_paper = psd->ptPaperSize;
    RECT saved_min = psd->rtMinMargin, saved_margin = psd->rtMargin;
    if (!(flags & (PSD_INTHOUSANDTHSOFINCHES | PSD_INHUNDREDTHSOFMILLIMETERS))) {
        WCHAR measure[2] = {L'1', 0};
        GetLocaleInfoW(0x0400, LOCALE_IMEASURE, measure, 2);
        flags |= measure[0] == L'1' ? PSD_INTHOUSANDTHSOFINCHES : PSD_INHUNDREDTHSOFMILLIMETERS;
        psd->Flags = flags;
    }
    if (!(flags & PSD_MINMARGINS))
        memset(&psd->rtMinMargin, 0, sizeof(RECT));
    if (!(flags & PSD_MARGINS)) {
        int m = (flags & PSD_INTHOUSANDTHSOFINCHES) ? 1000 : 2500;
        psd->rtMargin.left = psd->rtMargin.top = psd->rtMargin.right = psd->rtMargin.bottom = m;
    }

    PAGEDLG d;
    memset(&d, 0, sizeof(d));
    d.Psd = psd;
    d.Ansi = ansi;
    d.Metric = (flags & PSD_INHUNDREDTHSOFMILLIMETERS) != 0;
    d.Margin = psd->rtMargin;
    d.MinMargin = psd->rtMinMargin;
    d.Hook = (DLGPROC)psd->lpfnPageSetupHook;
    d.PaintHook = psd->lpfnPagePaintHook;
    BOOL ok = FALSE;
    HGLOBAL mode_in = psd->hDevMode, names_in = psd->hDevNames;
    HGLOBAL wide_mode = ansi ? PrnDevmodeFromAnsi(mode_in) : mode_in;
    HGLOBAL wide_names = ansi ? PrnDevnamesFromAnsi(names_in) : names_in;
    if (!PrnEnumerate(&d.List) || !d.List.Count) {
        DlgSetError(PDERR_NODEVICES);
    } else {
        BOOL file;
        int index;
        d.Current = PrnDevnamesPrinter(wide_names, &d.List, &index, &file) ? index : -1;
        if (d.Current < 0 && wide_mode) {
            DEVMODEW *dm = PrnCopyDevmode(wide_mode);
            if (dm) {
                d.Current = PrnFind(&d.List, dm->dmDeviceName);
                HeapFree(GetProcessHeap(), 0, dm);
            }
        }
        if (d.Current < 0)
            d.Current = d.List.Default;
        DEVMODEW *given = PrnCopyDevmode(wide_mode);
        if (given && lstrcmpiW(given->dmDeviceName, d.List.Items[d.Current].Name)) {
            HeapFree(GetProcessHeap(), 0, given);
            given = nullptr;
        }
        d.Devmode = PrnDevmode(psd->hwndOwner, &d.List.Items[d.Current], given);
        if (given)
            HeapFree(GetProcessHeap(), 0, given);
        load_papers(&d);
        paper_size(&d, &d.Width, &d.Height);
        int answer = 1;
        if (!(flags & PSD_RETURNDEFAULT)) {
            DLGSOURCE source = {flags, PSD_ENABLEPAGESETUPTEMPLATE, PSD_ENABLEPAGESETUPTEMPLATEHANDLE, psd->hInstance,
                                psd->lpPageSetupTemplateName, ansi};
            if (flags & PSD_ENABLEPAGESETUPTEMPLATEHANDLE)
                source.Instance = (HINSTANCE)psd->hPageSetupTemplate;
            const void *given_tmpl = DlgFindTemplate(&source, &error);
            LPDLGTEMPLATEW built = error || given_tmpl ? nullptr : build_template();
            if (error) {
                DlgSetError(error);
                answer = -1;
            } else if (!given_tmpl && !built) {
                DlgSetError(CDERR_MEMALLOCFAILURE);
                answer = -1;
            } else {
                if (g_hook_tls == TLS_OUT_OF_INDEXES && !g_hook_once) {
                    g_hook_once = 1;
                    g_hook_tls = TlsAlloc();
                }
                if (g_hook_tls != TLS_OUT_OF_INDEXES)
                    TlsSetValue(g_hook_tls, (void *)d.Hook);
                g_cancelled = FALSE;
                INT_PTR r = DialogBoxIndirectParamW(g_comdlg32, given_tmpl ? (LPCDLGTEMPLATEW)given_tmpl : built,
                                                    psd->hwndOwner, page_proc, (LPARAM)&d);
                if (g_hook_tls != TLS_OUT_OF_INDEXES)
                    TlsSetValue(g_hook_tls, nullptr);
                if (built)
                    HeapFree(GetProcessHeap(), 0, built);
                if (r == -1 || (r == 0 && !g_cancelled && !d.Closed)) {
                    DlgSetError(CDERR_DIALOGFAILURE);
                    answer = -1;
                } else {
                    answer = r == 1;
                }
            }
        }
        if (answer > 0) {
            HGLOBAL mode = d.Devmode ? PrnNewDevmodeHandle(d.Devmode) : nullptr;
            HGLOBAL names = PrnNewDevnamesHandle(&d.List.Items[d.Current], d.Current == d.List.Default, FALSE);
            if (names && (mode || !d.Devmode)) {
                if (ansi) {
                    HGLOBAL am = PrnDevmodeToAnsi(mode), an = PrnDevnamesToAnsi(names);
                    if (mode)
                        GlobalFree(mode);
                    GlobalFree(names);
                    mode = am;
                    names = an;
                }
                if (mode_in)
                    GlobalFree(mode_in);
                if (names_in)
                    GlobalFree(names_in);
                psd->hDevMode = mode;
                psd->hDevNames = names;
                int w, h;
                paper_size(&d, &w, &h);
                psd->ptPaperSize.x = w;
                psd->ptPaperSize.y = h;
                psd->rtMargin = d.Margin;
                ok = TRUE;
            } else {
                DlgSetError(CDERR_MEMALLOCFAILURE);
            }
        }
    }
    if (ansi) {
        if (wide_mode)
            GlobalFree(wide_mode);
        if (wide_names)
            GlobalFree(wide_names);
    }
    if (d.Devmode)
        HeapFree(GetProcessHeap(), 0, d.Devmode);
    PrnFreeList(&d.List);
    if (!ok) {
        psd->Flags = saved_flags;
        psd->ptPaperSize = saved_paper;
        psd->rtMinMargin = saved_min;
        psd->rtMargin = saved_margin;
    }
    return ok;
}

CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd)
{
    return page_setup(psd, FALSE);
}

CDAPI BOOL WINAPI PageSetupDlgA(LPPAGESETUPDLGA psd)
{
    /* the structure has the layout of the wide one but for the template name. */
    return page_setup((PAGESETUPDLGW *)psd, TRUE);
}
