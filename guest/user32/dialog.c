/*
 * the dialog manager: templates, the #32770 class procedure, modal loops
 * and keyboard navigation.
 */
#include "user32_private.h"

/* private dialog state, two pointers after DWLP_USER in the window extra bytes. */
#define DWLP_MIME (DWLP_USER + 2 * (int)sizeof(LONG_PTR))

typedef struct DIALOG {
    HFONT Font;
    BOOL OwnsFont;
    LONG BaseX;     /* average character width. */
    LONG BaseY;     /* character height. */
    UINT DefId;
    HWND FocusSave;
    BOOL Ended;
    INT_PTR Result;
    BOOL Ansi;
    WORD PointSize;      /* of the font of the template, 0x7FFF for the font of message boxes. */
    BOOL Scaled;         /* the font comes from the template, so a change of dpi scales the dialog. */
    UINT FontDpi;        /* the dpi of the font in use, or of the one made ahead of a change. */
    HFONT NextFont;      /* made for a dpi the dialog is about to move to. */
    LONG NextBaseX, NextBaseY;
    UINT Behavior;       /* DDC_* */
} DIALOG;

static DIALOG *get_dialog(HWND hwnd)
{
    return (DIALOG *)GetWindowLongPtrW(hwnd, DWLP_MIME);
}

/* a window the dialog manager made, which a custom class of dialogs can be as well. */
static DIALOG *dialog_of(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || !NtUserCallHwnd(hwnd, MIME_HWND_ISDIALOG))
        return nullptr;
    return get_dialog(hwnd);
}

/* ------------------------------------------------------------------ templates */

typedef struct TEMPLATE_INFO {
    DWORD Style, ExStyle, HelpId;
    WORD Items;
    short X, Y, Cx, Cy;
    LPCWSTR Menu, Class, Caption;
    WORD PointSize, Weight;
    BOOL Italic;
    BYTE Charset;
    LPCWSTR Face;
    BOOL Extended;
    const WORD *FirstItem;
} TEMPLATE_INFO;

typedef struct ITEM_INFO {
    DWORD Style, ExStyle, HelpId;
    short X, Y, Cx, Cy;
    DWORD Id;
    LPCWSTR Class, Title;
    WORD DataSize;
    const void *Data;
} ITEM_INFO;

/* a sz_Or_Ord field: 0 for none, 0xFFFF and an ordinal, or a string. */
static const WORD *read_name(const WORD *p, LPCWSTR *out)
{
    if (*p == 0) {
        *out = nullptr;
        return p + 1;
    }
    if (*p == 0xFFFF) {
        *out = MAKEINTRESOURCEW(p[1]);
        return p + 2;
    }
    *out = (LPCWSTR)p;
    return p + wcslen((LPCWSTR)p) + 1;
}

static const WORD *align_dword(const WORD *p)
{
    return (const WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
}

static void parse_template(LPCDLGTEMPLATEW tmpl, TEMPLATE_INFO *t)
{
    const WORD *p = (const WORD *)tmpl;
    memset(t, 0, sizeof(*t));
    if (p[0] == 1 && p[1] == 0xFFFF) {
        t->Extended = TRUE;
        const DWORD *d = (const DWORD *)(p + 2);
        t->HelpId = d[0];
        t->ExStyle = d[1];
        t->Style = d[2];
        p = (const WORD *)(d + 3);
    } else {
        const DWORD *d = (const DWORD *)p;
        t->Style = d[0];
        t->ExStyle = d[1];
        p = (const WORD *)(d + 2);
    }
    t->Items = p[0];
    t->X = (short)p[1];
    t->Y = (short)p[2];
    t->Cx = (short)p[3];
    t->Cy = (short)p[4];
    p += 5;
    p = read_name(p, &t->Menu);
    p = read_name(p, &t->Class);
    t->Caption = (LPCWSTR)p;
    p += wcslen((LPCWSTR)p) + 1;
    if (t->Style & (DS_SETFONT | DS_SHELLFONT)) {
        t->PointSize = *p++;
        if (t->Extended) {
            t->Weight = *p++;
            t->Italic = LOBYTE(*p);
            t->Charset = HIBYTE(*p);
            p++;
        }
        t->Face = (LPCWSTR)p;
        p += wcslen((LPCWSTR)p) + 1;
    }
    t->FirstItem = align_dword(p);
}

static const WORD *parse_item(const WORD *p, BOOL extended, ITEM_INFO *it)
{
    memset(it, 0, sizeof(*it));
    p = align_dword(p);
    const DWORD *d = (const DWORD *)p;
    if (extended) {
        it->HelpId = d[0];
        it->ExStyle = d[1];
        it->Style = d[2];
        p = (const WORD *)(d + 3);
    } else {
        it->Style = d[0];
        it->ExStyle = d[1];
        p = (const WORD *)(d + 2);
    }
    it->X = (short)p[0];
    it->Y = (short)p[1];
    it->Cx = (short)p[2];
    it->Cy = (short)p[3];
    p += 4;
    if (extended) {
        it->Id = *(const DWORD *)p;
        p += 2;
    } else {
        it->Id = *p++;
    }
    p = read_name(p, &it->Class);
    p = read_name(p, &it->Title);
    it->DataSize = *p++;
    it->Data = it->DataSize ? p : nullptr;
    p = (const WORD *)((const UCHAR *)p + it->DataSize);
    return p;
}

static LPCWSTR builtin_class(LPCWSTR cls)
{
    if (!IS_INTRESOURCE(cls))
        return cls;
    switch ((ULONG_PTR)cls) {
    case 0x80:
        return L"Button";
    case 0x81:
        return L"Edit";
    case 0x82:
        return L"Static";
    case 0x83:
        return L"ListBox";
    case 0x84:
        return L"ScrollBar";
    case 0x85:
        return L"ComboBox";
    }
    return cls;
}

/* ------------------------------------------------------------------ base units */

static void compute_base_units(HFONT font, LONG *bx, LONG *by)
{
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, font);
    LONG height = 0;
    LONG width = GdiGetCharDimensions(dc, nullptr, &height);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    *bx = width ? width : 8;
    *by = height ? height : 16;
}

WINUSERAPI LONG WINAPI GetDialogBaseUnits(void)
{
    return (LONG)NtUserCallNoParam(MIME_NOPARAM_GETDIALOGBASEUNITS);
}

WINUSERAPI BOOL WINAPI MapDialogRect(HWND hwnd, LPRECT r)
{
    DIALOG *d = get_dialog(hwnd);
    LONG bx, by;
    if (d) {
        bx = d->BaseX;
        by = d->BaseY;
    } else {
        LONG units = GetDialogBaseUnits();
        bx = LOWORD(units);
        by = HIWORD(units);
    }
    r->left = UserMulDiv(r->left, bx, 4);
    r->right = UserMulDiv(r->right, bx, 4);
    r->top = UserMulDiv(r->top, by, 8);
    r->bottom = UserMulDiv(r->bottom, by, 8);
    return TRUE;
}

/* ------------------------------------------------------------------ focus and default buttons */

static UINT dlg_code(HWND hwnd)
{
    return hwnd ? (UINT)SendMessageW(hwnd, WM_GETDLGCODE, 0, 0) : 0;
}

static void set_default_button(HWND dialog, HWND button)
{
    /* the push button that has the focus is drawn as the default one. */
    for (HWND c = GetWindow(dialog, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        UINT code = dlg_code(c);
        if (c != button && (code & DLGC_DEFPUSHBUTTON))
            SendMessageW(c, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
    }
    if (button && (dlg_code(button) & DLGC_UNDEFPUSHBUTTON))
        SendMessageW(button, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
}

static void restore_default(HWND dialog)
{
    DIALOG *d = get_dialog(dialog);
    HWND def = d && d->DefId ? GetDlgItem(dialog, (int)d->DefId) : nullptr;
    set_default_button(dialog, def);
}

static void dialog_set_focus(HWND dialog, HWND control)
{
    if (!control)
        return;
    UINT code = dlg_code(control);
    if (code & (DLGC_UNDEFPUSHBUTTON | DLGC_DEFPUSHBUTTON))
        set_default_button(dialog, control);
    else
        restore_default(dialog);
    if (code & DLGC_HASSETSEL)
        SendMessageW(control, EM_SETSEL, 0, -1);
    SetFocus(control);
}

static BOOL is_tabstop(HWND hwnd)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
    return (style & WS_TABSTOP) && (style & WS_VISIBLE) && !(style & WS_DISABLED);
}

WINUSERAPI HWND WINAPI GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous)
{
    HWND first = GetWindow(dialog, GW_CHILD);
    if (!first)
        return nullptr;
    HWND c = control && IsChild(dialog, control) ? control : nullptr;
    HWND start = c;
    for (int guard = 0; guard < 4096; guard++) {
        if (!c)
            c = previous ? GetWindow(first, GW_HWNDLAST) : first;
        else {
            c = GetWindow(c, previous ? GW_HWNDPREV : GW_HWNDNEXT);
            if (!c)
                c = previous ? GetWindow(first, GW_HWNDLAST) : first;
        }
        if (is_tabstop(c))
            return c;
        if (c == start || (!start && guard > 0 && c == (previous ? GetWindow(first, GW_HWNDLAST) : first)))
            break;
    }
    return control;
}

WINUSERAPI HWND WINAPI GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous)
{
    if (!control || !IsChild(dialog, control))
        return GetNextDlgTabItem(dialog, nullptr, previous);
    /* the group runs from a WS_GROUP control to the next one. */
    HWND group_start = control;
    while (!(GetWindowLongW(group_start, GWL_STYLE) & WS_GROUP)) {
        HWND prev = GetWindow(group_start, GW_HWNDPREV);
        if (!prev)
            break;
        group_start = prev;
    }
    HWND c = control;
    for (int guard = 0; guard < 4096; guard++) {
        HWND next = previous ? GetWindow(c, GW_HWNDPREV) : GetWindow(c, GW_HWNDNEXT);
        if (!previous && (!next || (GetWindowLongW(next, GWL_STYLE) & WS_GROUP)))
            next = group_start;
        if (previous && (c == group_start || !next)) {
            /* wrap to the last control of the group. */
            next = group_start;
            for (HWND n = GetWindow(group_start, GW_HWNDNEXT); n && !(GetWindowLongW(n, GWL_STYLE) & WS_GROUP);
                 n = GetWindow(n, GW_HWNDNEXT))
                next = n;
        }
        c = next;
        if (c == control)
            break;
        DWORD style = (DWORD)GetWindowLongW(c, GWL_STYLE);
        if ((style & WS_VISIBLE) && !(style & WS_DISABLED))
            return c;
    }
    return control;
}

/* ------------------------------------------------------------------ dialogs and the dpi */

/* the size of a window of the style at a dpi, once its frame and bars are added to a client area. */
static void expand_by_frame(HWND hwnd, UINT dpi, RECT *r)
{
    DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(r, style, GetMenu(hwnd) != nullptr, ex, dpi);
    if (style & WS_HSCROLL)
        r->bottom += GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
    if (style & WS_VSCROLL)
        r->right += GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
}

static void to_dialog_units(RECT *r, LONG bx, LONG by)
{
    RECT in = *r;
    r->left = UserMulDiv(in.left, 4, bx);
    r->top = UserMulDiv(in.top, 8, by);
    r->right = r->left + UserMulDiv(in.right - in.left, 4, bx);
    r->bottom = r->top + UserMulDiv(in.bottom - in.top, 8, by);
}

static void from_dialog_units(RECT *r, LONG bx, LONG by)
{
    RECT in = *r;
    r->left = UserMulDiv(in.left, bx, 4);
    r->top = UserMulDiv(in.top, by, 8);
    r->right = r->left + UserMulDiv(in.right - in.left, bx, 4);
    r->bottom = r->top + UserMulDiv(in.bottom - in.top, by, 8);
}

/* the font a dialog gets at a dpi, kept until the dialog moves there. */
static void make_next_font(HWND hwnd, DIALOG *d, UINT dpi)
{
    if (d->NextFont) {
        DeleteObject(d->NextFont);
        d->NextFont = nullptr;
        d->NextBaseX = d->NextBaseY = 0;
        d->FontDpi = 0;
    }
    HFONT font = nullptr;
    if (d->PointSize == 0x7FFF) {
        font = UserGetMessageBoxFont(dpi);
    } else {
        LOGFONTW lf;
        if (GetObjectW(d->Font, sizeof(lf), &lf)) {
            lf.lfHeight = -(LONG)(((LONG)d->PointSize * (LONG)dpi + 36) / 72);
            font = CreateFontIndirectW(&lf);
        }
    }
    if (!font)
        return;
    d->NextFont = font;
    d->FontDpi = dpi;
    HDC dc = GetDC(hwnd);
    if (dc) {
        HGDIOBJ old = SelectObject(dc, font);
        d->NextBaseX = GdiGetCharDimensions(dc, nullptr, &d->NextBaseY);
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
    }
    if (!d->NextBaseX) {
        LONG units = GetDialogBaseUnits();
        d->NextBaseX = LOWORD(units);
        d->NextBaseY = HIWORD(units);
    }
}

/*
 * the size a dialog has at a dpi: its client area in dialog units of the font it has now, in pixels of the font it
 * would have there, and the frame of the dpi around it. false when the dialog does not scale.
 */
static BOOL dialog_scaled_size(HWND hwnd, DIALOG *d, UINT dpi, SIZE *size)
{
    if (!d->Scaled || !d->Font || (d->Behavior & DDC_DISABLE_ALL))
        return FALSE;
    RECT frame = {};
    expand_by_frame(hwnd, GetDpiForWindow(hwnd), &frame);
    RECT client = {0, 0, frame.left + size->cx - frame.right, frame.top + size->cy - frame.bottom};
    if (d->FontDpi != dpi)
        make_next_font(hwnd, d, dpi);
    if (d->NextFont) {
        to_dialog_units(&client, d->BaseX, d->BaseY);
        from_dialog_units(&client, d->NextBaseX, d->NextBaseY);
        expand_by_frame(hwnd, dpi, &client);
        size->cx = client.right - client.left;
        size->cy = client.bottom - client.top;
    }
    return TRUE;
}

static void set_font_of_children(HWND dialog, HFONT font)
{
    for (HWND c = GetWindow(dialog, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (!(NtUserCallHwnd(c, MIME_HWND_GETCONTROLDPIBEHAVIOR) & DCDC_DISABLE_FONT_UPDATE))
            SendMessageW(c, WM_SETFONT, (WPARAM)font, 1);
}

static void reposition_children(HDWP hdwp, HWND dialog, DIALOG *d, LONG old_x, LONG old_y)
{
    if (d->Behavior & DDC_DISABLE_CONTROL_RELAYOUT)
        return;
    for (HWND c = GetWindow(dialog, GW_CHILD); c && hdwp; c = GetWindow(c, GW_HWNDNEXT)) {
        if (NtUserCallHwnd(c, MIME_HWND_GETCONTROLDPIBEHAVIOR) & DCDC_DISABLE_RELAYOUT)
            continue;
        RECT r;
        GetWindowRect(c, &r);
        MapWindowPoints(nullptr, dialog, (POINT *)&r, 2);
        to_dialog_units(&r, old_x, old_y);
        from_dialog_units(&r, d->BaseX, d->BaseY);
        hdwp = DeferWindowPos(hdwp, c, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp)
        EndDeferWindowPos(hdwp);
}

/* the dialog follows a change of the dpi it is at: a new font, its controls moved to where the font puts them. */
static void dialog_dpi_changed(HWND hwnd, DIALOG *d, UINT dpi, const RECT *rect)
{
    if (!d->Scaled || !d->Font || (d->Behavior & DDC_DISABLE_ALL))
        return;
    RECT now;
    GetWindowRect(hwnd, &now);
    SIZE size = {now.right - now.left, now.bottom - now.top};
    if (d->FontDpi == dpi && rect) {
        size.cx = rect->right - rect->left;
        size.cy = rect->bottom - rect->top;
    } else {
        SIZE before = size;
        dialog_scaled_size(hwnd, d, dpi, &size);
        if (size.cx == before.cx && size.cy == before.cy)
            return;
    }
    if (!d->NextFont)
        return;
    LONG old_x = d->BaseX, old_y = d->BaseY;
    d->BaseX = d->NextBaseX;
    d->BaseY = d->NextBaseY;
    if (d->OwnsFont)
        DeleteObject(d->Font);
    d->Font = d->NextFont;
    d->OwnsFont = TRUE;
    d->NextFont = nullptr;
    d->NextBaseX = d->NextBaseY = 0;
    set_font_of_children(hwnd, d->Font);
    reposition_children(BeginDeferWindowPos(1), hwnd, d, old_x, old_y);
    if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD) && !(d->Behavior & DDC_DISABLE_RESIZE) && rect)
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, size.cx, size.cy,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(hwnd, nullptr, TRUE);
}

WINUSERAPI BOOL WINAPI SetDialogDpiChangeBehavior(HWND hwnd, DIALOG_DPI_CHANGE_BEHAVIORS mask,
                                                  DIALOG_DPI_CHANGE_BEHAVIORS values)
{
    if (!hwnd || !IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    DIALOG *d = dialog_of(hwnd);
    if (!d)
        return FALSE;
    UINT bits = (UINT)mask & 7;
    d->Behavior = ((UINT)values & bits) | (d->Behavior & ~bits);
    return TRUE;
}

WINUSERAPI DIALOG_DPI_CHANGE_BEHAVIORS WINAPI GetDialogDpiChangeBehavior(HWND hwnd)
{
    DIALOG *d = dialog_of(hwnd);
    return d ? (DIALOG_DPI_CHANGE_BEHAVIORS)d->Behavior : DDC_DEFAULT;
}

WINUSERAPI BOOL WINAPI SetDialogControlDpiChangeBehavior(HWND hwnd, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS mask,
                                                         DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS values)
{
    if (!hwnd || !IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return (BOOL)NtUserCallHwndParam(hwnd, ((ULONG_PTR)values << 8) | ((ULONG_PTR)mask & 0xFF),
                                     MIME_HWNDPARAM_CONTROLDPIBEHAVIOR);
}

WINUSERAPI DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS WINAPI GetDialogControlDpiChangeBehavior(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return DCDC_DEFAULT;
    return (DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS)NtUserCallHwnd(hwnd, MIME_HWND_GETCONTROLDPIBEHAVIOR);
}

/* ------------------------------------------------------------------ the dialog window procedure */

static BOOL returns_value_directly(UINT msg)
{
    switch (msg) {
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    case WM_COMPAREITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_QUERYDRAGICON:
    case WM_INITDIALOG:
        return TRUE;
    }
    return FALSE;
}

static LRESULT default_dialog(HWND hwnd, DIALOG *d, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        HBRUSH brush = (HBRUSH)SendMessageW(hwnd, WM_CTLCOLORDLG, wparam, (LPARAM)hwnd);
        if (!brush)
            brush = (HBRUSH)DefWindowProcW(hwnd, WM_CTLCOLORDLG, wparam, (LPARAM)hwnd);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wparam, &r, brush);
        return 1;
    }
    case WM_ACTIVATE:
        /* the default push button is only drawn as default while the dialog is active. */
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        if (LOWORD(wparam) == WA_INACTIVE) {
            HWND focus = GetFocus();
            if (d && focus && IsChild(hwnd, focus))
                d->FocusSave = focus;
        } else if (d && d->FocusSave && IsWindow(d->FocusSave)) {
            dialog_set_focus(hwnd, d->FocusSave);
        } else if (d) {
            dialog_set_focus(hwnd, GetNextDlgTabItem(hwnd, nullptr, FALSE));
        }
        return 0;
    case WM_SETFOCUS: {
        HWND target = d && d->FocusSave && IsWindow(d->FocusSave) ? d->FocusSave : GetNextDlgTabItem(hwnd, nullptr, FALSE);
        if (target)
            dialog_set_focus(hwnd, target);
        return 0;
    }
    case WM_CLOSE: {
        HWND cancel = GetDlgItem(hwnd, IDCANCEL);
        if (!cancel || IsWindowEnabled(cancel))
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
        return 0;
    }
    case WM_NCDESTROY:
        if (d) {
            if (d->OwnsFont && d->Font)
                DeleteObject(d->Font);
            SetWindowLongPtrW(hwnd, DWLP_MIME, 0);
            UserFree(d);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_GETFONT:
        return d ? (LRESULT)d->Font : 0;
    case WM_SETFONT:
        if (d) {
            /* a font of the program is the program's to scale. */
            if (d->Font != (HFONT)wparam)
                d->OwnsFont = d->Scaled = FALSE;
            d->Font = (HFONT)wparam;
        }
        return 0;
    case DM_GETDEFID:
        if (d && d->DefId)
            return MAKELONG(d->DefId, DC_HASDEFID);
        return 0;
    case DM_SETDEFID:
        if (d) {
            d->DefId = (UINT)wparam;
            restore_default(hwnd);
        }
        return TRUE;
    case WM_NEXTDLGCTL: {
        HWND target;
        if (lparam)
            target = (HWND)wparam;
        else
            target = GetNextDlgTabItem(hwnd, GetFocus(), wparam != 0);
        dialog_set_focus(hwnd, target);
        return 0;
    }
    case WM_SHOWWINDOW:
        if (wparam && d && !GetFocus())
            dialog_set_focus(hwnd, GetNextDlgTabItem(hwnd, nullptr, FALSE));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CHANGEUISTATE:
    case WM_UPDATEUISTATE:
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            SendMessageW(c, WM_UPDATEUISTATE, wparam, lparam);
        /* the dialog keeps the state too, for its own menu bar. */
        return DefWindowProcW(hwnd, WM_UPDATEUISTATE, wparam, lparam);
    case WM_GETDLGCODE:
        return 0;
    case WM_GETDPISCALEDSIZE:
        if (wparam < 96 || wparam > 96 + 0x180) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        if (!d || (d->Behavior & DDC_DISABLE_RESIZE))
            return 0;
        return dialog_scaled_size(hwnd, d, (UINT)wparam, (SIZE *)lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

WINUSERAPI LRESULT WINAPI DefDlgProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    DIALOG *d = get_dialog(hwnd);
    DLGPROC proc = (DLGPROC)GetWindowLongPtrW(hwnd, DWLP_DLGPROC);
    /* the dialog follows the dpi before its procedure hears of it. */
    if (d && (msg == WM_DPICHANGED || msg == WM_DPICHANGED_BEFOREPARENT))
        dialog_dpi_changed(hwnd, d, GetWindowDPI(hwnd), msg == WM_DPICHANGED ? (const RECT *)lparam : nullptr);
    if (proc) {
        SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, 0);
        INT_PTR r = d && d->Ansi ? (INT_PTR)UserCallProc((WNDPROC)proc, TRUE, hwnd, msg, wparam, lparam)
                                 : proc(hwnd, msg, wparam, lparam);
        if (r || !IsWindow(hwnd)) {
            if (returns_value_directly(msg))
                return r;
            return IsWindow(hwnd) ? GetWindowLongPtrW(hwnd, DWLP_MSGRESULT) : r;
        }
    }
    return default_dialog(hwnd, get_dialog(hwnd), msg, wparam, lparam);
}

WINUSERAPI LRESULT WINAPI DefDlgProcA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefDlgProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefDlgProcW(hwnd, msg, wparam, lparam);
}

/* ------------------------------------------------------------------ creation */

/*
 * lfMessageFont scaled to the session dpi. the signed division truncates toward zero, so -12 becomes -11 like on
 * windows. dialogs with point size 0x7FFF use this font.
 */
HFONT UserGetMessageBoxFont(UINT dpi)
{
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    UINT session = GetDpiForSystem();
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, session))
        return nullptr;
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = (LONG)(((LONG64)lf.lfHeight * (LONG64)dpi + (LONG64)(session / 2)) / (LONG64)session);
    lf.lfWidth = (LONG)(((LONG64)lf.lfWidth * (LONG64)dpi + (LONG64)(session / 2)) / (LONG64)session);
    return CreateFontIndirectW(&lf);
}

static HFONT create_dialog_font(const TEMPLATE_INFO *t, UINT dpi)
{
    if (t->PointSize == 0x7FFF)
        return UserGetMessageBoxFont(dpi);
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -UserMulDiv(t->PointSize, (int)dpi, 72);
    lf.lfWeight = t->Weight ? t->Weight : FW_NORMAL;
    lf.lfItalic = (BYTE)t->Italic;
    lf.lfCharSet = t->Charset ? t->Charset : DEFAULT_CHARSET;
    LPCWSTR face = t->Face;
    if ((t->Style & DS_SHELLFONT) == DS_SHELLFONT && !_wcsicmp(face, L"MS Shell Dlg"))
        face = L"MS Shell Dlg 2";
    for (int i = 0; i < LF_FACESIZE - 1 && face[i]; i++)
        lf.lfFaceName[i] = face[i];
    return CreateFontIndirectW(&lf);
}

HWND UserCreateDialog(HINSTANCE instance, LPCDLGTEMPLATEW tmpl, HWND owner, DLGPROC proc, LPARAM param, BOOL ansi,
                      BOOL modal)
{
    TEMPLATE_INFO t;
    parse_template(tmpl, &t);
    UINT dpi = GetDpiForSystem();

    DIALOG *d = UserAlloc(sizeof(DIALOG));
    if (!d)
        return nullptr;
    d->Ansi = ansi;
    if (t.Style & (DS_SETFONT | DS_SHELLFONT)) {
        d->Font = create_dialog_font(&t, dpi);
        d->OwnsFont = d->Font != nullptr;
        d->PointSize = t.PointSize;
        d->Scaled = d->OwnsFont;
        d->FontDpi = dpi;
    }
    if (d->Font) {
        compute_base_units(d->Font, &d->BaseX, &d->BaseY);
    } else {
        LONG units = GetDialogBaseUnits();
        d->BaseX = LOWORD(units);
        d->BaseY = HIWORD(units);
    }

    RECT r = {0, 0, UserMulDiv(t.Cx, d->BaseX, 4), UserMulDiv(t.Cy, d->BaseY, 8)};
    DWORD style = t.Style & ~WS_VISIBLE;
    DWORD ex = t.ExStyle;
    if (t.Style & DS_MODALFRAME)
        ex |= WS_EX_DLGMODALFRAME;
    if (t.Style & DS_SYSMODAL)
        ex |= WS_EX_TOPMOST;
    if (t.Style & DS_CONTROL) {
        style &= ~(WS_CAPTION | WS_SYSMENU);
        style |= WS_CHILD;
        ex |= WS_EX_CONTROLPARENT;
    }
    if (!(style & WS_CHILD) && (style & WS_CAPTION) == 0 && !(style & WS_POPUP))
        style |= WS_POPUP;
    /* the menu the template names is the dialog's menu bar. */
    HMENU menu = t.Menu && !(style & WS_CHILD) ? LoadMenuW(instance, t.Menu) : nullptr;
    AdjustWindowRectExForDpi(&r, style, menu != nullptr, ex, dpi);
    int width = r.right - r.left, height = r.bottom - r.top;
    int x = UserMulDiv(t.X, d->BaseX, 4), y = UserMulDiv(t.Y, d->BaseY, 8);
    if (t.Style & DS_CENTER) {
        RECT ref;
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &mi);
        ref = mi.rcWork;
        if (owner && IsWindowVisible(owner) && !IsIconic(owner) && !(t.Style & DS_CENTERMOUSE))
            GetWindowRect(owner, &ref);
        x = ref.left + (ref.right - ref.left - width) / 2;
        y = ref.top + (ref.bottom - ref.top - height) / 2;
        if (x + width > mi.rcWork.right)
            x = mi.rcWork.right - width;
        if (y + height > mi.rcWork.bottom)
            y = mi.rcWork.bottom - height;
        if (x < mi.rcWork.left)
            x = mi.rcWork.left;
        if (y < mi.rcWork.top)
            y = mi.rcWork.top;
    } else if (!(style & WS_CHILD) && owner && !(t.Style & DS_ABSALIGN)) {
        POINT pt = {x, y};
        ClientToScreen(owner, &pt);
        x = pt.x;
        y = pt.y;
    }

    if (!(style & WS_CHILD)) {
        /* keep the dialog inside the work area of its monitor. */
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &mi);
        if (y + height > mi.rcWork.bottom - 4)
            y = mi.rcWork.bottom - 4 - height;
        if (y < mi.rcWork.top)
            y = mi.rcWork.top;
        if (x + width > mi.rcWork.right)
            x = mi.rcWork.right - width;
        if (x < mi.rcWork.left)
            x = mi.rcWork.left;
    }

    LPCWSTR cls = t.Class ? t.Class : L"#32770";
    HWND hwnd = CreateWindowExW(ex, cls, t.Caption, style, x, y, width, height, owner, menu, instance, (LPVOID)param);
    if (!hwnd && menu)
        DestroyMenu(menu);
    if (!hwnd) {
        if (d->OwnsFont)
            DeleteObject(d->Font);
        UserFree(d);
        return nullptr;
    }
    SetWindowLongPtrW(hwnd, DWLP_MIME, (LONG_PTR)d);
    SetWindowLongPtrW(hwnd, DWLP_DLGPROC, (LONG_PTR)proc);
    NtUserCallHwndParam(hwnd, 1, MIME_HWNDPARAM_SETDIALOGINFO);
    if (d->Font)
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)d->Font, 0);

    /* controls, in template order: the first created is the top of the z order. */
    const WORD *p = t.FirstItem;
    HWND first_tab = nullptr;
    for (WORD i = 0; i < t.Items; i++) {
        ITEM_INFO it;
        p = parse_item(p, t.Extended, &it);
        LPCWSTR item_class = builtin_class(it.Class);
        HWND c = CreateWindowExW(it.ExStyle | WS_EX_NOPARENTNOTIFY, item_class, it.Title, it.Style | WS_CHILD,
                                 UserMulDiv(it.X, d->BaseX, 4), UserMulDiv(it.Y, d->BaseY, 8), UserMulDiv(it.Cx, d->BaseX, 4),
                                 UserMulDiv(it.Cy, d->BaseY, 8), hwnd, (HMENU)(UINT_PTR)it.Id, instance,
                                 (LPVOID)it.Data);
        if (!c) {
            if (t.Style & DS_NOFAILCREATE)
                continue;
            DestroyWindow(hwnd);
            return nullptr;
        }
        if (d->Font)
            SendMessageW(c, WM_SETFONT, (WPARAM)d->Font, 0);
        if (dlg_code(c) & DLGC_DEFPUSHBUTTON)
            d->DefId = it.Id;
        if (!first_tab && is_tabstop(c))
            first_tab = c;
    }
    if (!IsWindow(hwnd))
        return nullptr;
    HWND focus = first_tab ? first_tab : GetNextDlgTabItem(hwnd, nullptr, FALSE);
    INT_PTR init = SendMessageW(hwnd, WM_INITDIALOG, (WPARAM)focus, param);
    if (!IsWindow(hwnd))
        return nullptr;
    if (init && focus)
        dialog_set_focus(hwnd, focus);
    if (t.Style & DS_SETFOREGROUND)
        SetForegroundWindow(hwnd);
    if (t.Style & WS_VISIBLE)
        ShowWindow(hwnd, SW_SHOWNORMAL);
    (void)modal;
    return hwnd;
}

INT_PTR UserModalLoop(HWND dialog, HWND owner)
{
    BOOL owner_enabled = owner && IsWindowEnabled(owner);
    if (owner_enabled)
        EnableWindow(owner, FALSE);
    DIALOG *d = get_dialog(dialog);
    if (d && !d->Ended && !IsWindowVisible(dialog))
        ShowWindow(dialog, SW_SHOWNORMAL);
    MSG msg;
    BOOL quit = FALSE;
    INT_PTR result = -1;
    while (IsWindow(dialog) && (d = get_dialog(dialog)) && !d->Ended) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            quit = got == 0;
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    d = IsWindow(dialog) ? get_dialog(dialog) : nullptr;
    if (d)
        result = d->Result;
    if (owner_enabled)
        EnableWindow(owner, TRUE);
    if (IsWindow(dialog))
        DestroyWindow(dialog);
    if (owner && IsWindow(owner))
        SetActiveWindow(owner);
    if (quit)
        PostQuitMessage((int)msg.wParam);
    return result;
}

WINUSERAPI BOOL WINAPI EndDialog(HWND hwnd, INT_PTR result)
{
    DIALOG *d = get_dialog(hwnd);
    if (!d)
        return FALSE;
    d->Result = result;
    d->Ended = TRUE;
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner)
        EnableWindow(owner, TRUE);
    ShowWindow(hwnd, SW_HIDE);
    /* wake the modal loop. */
    PostMessageW(hwnd, WM_NULL, 0, 0);
    return TRUE;
}

static HWND find_owner(HWND parent)
{
    /* modal dialogs are owned by the top level window of their parent. */
    if (!parent)
        return nullptr;
    HWND root = GetAncestor(parent, GA_ROOT);
    return root ? root : parent;
}

WINUSERAPI HWND WINAPI CreateDialogIndirectParamW(HINSTANCE instance, LPCDLGTEMPLATEW tmpl, HWND parent, DLGPROC proc,
                                                  LPARAM param)
{
    return UserCreateDialog(instance, tmpl, parent, proc, param, FALSE, FALSE);
}

WINUSERAPI HWND WINAPI CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATEA tmpl, HWND parent, DLGPROC proc,
                                                  LPARAM param)
{
    return UserCreateDialog(instance, (LPCDLGTEMPLATEW)tmpl, parent, proc, param, TRUE, FALSE);
}

WINUSERAPI INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE instance, LPCDLGTEMPLATEW tmpl, HWND parent, DLGPROC proc,
                                                  LPARAM param)
{
    HWND owner = find_owner(parent);
    HWND dialog = UserCreateDialog(instance, tmpl, owner, proc, param, FALSE, TRUE);
    if (!dialog)
        return -1;
    return UserModalLoop(dialog, owner);
}

WINUSERAPI INT_PTR WINAPI DialogBoxIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATEA tmpl, HWND parent, DLGPROC proc,
                                                  LPARAM param)
{
    HWND owner = find_owner(parent);
    HWND dialog = UserCreateDialog(instance, (LPCDLGTEMPLATEW)tmpl, owner, proc, param, TRUE, TRUE);
    if (!dialog)
        return -1;
    return UserModalLoop(dialog, owner);
}

static LPCDLGTEMPLATEW load_template(HINSTANCE instance, LPCWSTR name)
{
    HRSRC res = FindResourceW(instance, name, RTW_DIALOG);
    return res ? LockResource(LoadResource(instance, res)) : nullptr;
}

WINUSERAPI INT_PTR WINAPI DialogBoxParamW(HINSTANCE instance, LPCWSTR name, HWND parent, DLGPROC proc, LPARAM param)
{
    LPCDLGTEMPLATEW t = load_template(instance, name);
    return t ? DialogBoxIndirectParamW(instance, t, parent, proc, param) : -1;
}

WINUSERAPI INT_PTR WINAPI DialogBoxParamA(HINSTANCE instance, LPCSTR name, HWND parent, DLGPROC proc, LPARAM param)
{
    LPWSTR w = UserAtoW(name);
    LPCDLGTEMPLATEW t = load_template(instance, w);
    UserFreeConverted(w, name);
    if (!t)
        return -1;
    HWND owner = find_owner(parent);
    HWND dialog = UserCreateDialog(instance, t, owner, proc, param, TRUE, TRUE);
    return dialog ? UserModalLoop(dialog, owner) : -1;
}

WINUSERAPI HWND WINAPI CreateDialogParamW(HINSTANCE instance, LPCWSTR name, HWND parent, DLGPROC proc, LPARAM param)
{
    LPCDLGTEMPLATEW t = load_template(instance, name);
    return t ? CreateDialogIndirectParamW(instance, t, parent, proc, param) : nullptr;
}

WINUSERAPI HWND WINAPI CreateDialogParamA(HINSTANCE instance, LPCSTR name, HWND parent, DLGPROC proc, LPARAM param)
{
    LPWSTR w = UserAtoW(name);
    LPCDLGTEMPLATEW t = load_template(instance, w);
    UserFreeConverted(w, name);
    return t ? UserCreateDialog(instance, t, parent, proc, param, TRUE, FALSE) : nullptr;
}

/* ------------------------------------------------------------------ keyboard navigation */

static void show_keyboard_cues(HWND dialog)
{
    SendMessageW(dialog, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
}

WINUSERAPI BOOL WINAPI IsDialogMessageW(HWND dialog, LPMSG msg)
{
    if (!dialog || (msg->hwnd != dialog && !IsChild(dialog, msg->hwnd)))
        return FALSE;
    HWND focus = GetFocus();
    if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_MENU)
        SendMessageW(dialog, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL), 0);
    if (msg->message != WM_KEYDOWN && msg->message != WM_CHAR) {
        if (msg->message == WM_SYSCHAR || msg->message == WM_SYSKEYDOWN || msg->message == WM_KEYUP)
            ;
        TranslateMessage(msg);
        DispatchMessageW(msg);
        return TRUE;
    }
    UINT code = dlg_code(msg->hwnd == dialog ? nullptr : msg->hwnd);
    if (code & DLGC_WANTMESSAGE) {
        DispatchMessageW(msg);
        return TRUE;
    }
    if (msg->message == WM_KEYDOWN) {
        switch (msg->wParam) {
        case VK_TAB:
            if (code & (DLGC_WANTTAB | DLGC_WANTALLKEYS))
                break;
            show_keyboard_cues(dialog);
            dialog_set_focus(dialog, GetNextDlgTabItem(dialog, focus, GetKeyState(VK_SHIFT) < 0));
            return TRUE;
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
            if (code & (DLGC_WANTARROWS | DLGC_WANTALLKEYS))
                break;
            show_keyboard_cues(dialog);
            {
                HWND next = GetNextDlgGroupItem(dialog, focus, msg->wParam == VK_LEFT || msg->wParam == VK_UP);
                if (next && next != focus) {
                    dialog_set_focus(dialog, next);
                    if (dlg_code(next) & DLGC_RADIOBUTTON)
                        SendMessageW(next, BM_CLICK, 0, 0);
                }
            }
            return TRUE;
        case VK_RETURN: {
            if (code & (DLGC_WANTALLKEYS))
                break;
            DWORD def = (DWORD)SendMessageW(dialog, DM_GETDEFID, 0, 0);
            UINT id = HIWORD(def) == DC_HASDEFID ? LOWORD(def) : IDOK;
            if (focus && (dlg_code(focus) & DLGC_DEFPUSHBUTTON))
                id = (UINT)GetDlgCtrlID(focus);
            HWND button = GetDlgItem(dialog, (int)id);
            if (!button || IsWindowEnabled(button))
                SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)button);
            return TRUE;
        }
        case VK_ESCAPE: {
            HWND cancel = GetDlgItem(dialog, IDCANCEL);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
            return TRUE;
        }
        }
    }
    TranslateMessage(msg);
    DispatchMessageW(msg);
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsDialogMessageA(HWND dialog, LPMSG msg)
{
    return IsDialogMessageW(dialog, msg);
}

/* ------------------------------------------------------------------ control helpers */

WINUSERAPI LRESULT WINAPI SendDlgItemMessageW(HWND dialog, int id, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HWND c = GetDlgItem(dialog, id);
    return c ? SendMessageW(c, msg, wparam, lparam) : 0;
}

WINUSERAPI LRESULT WINAPI SendDlgItemMessageA(HWND dialog, int id, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HWND c = GetDlgItem(dialog, id);
    return c ? SendMessageA(c, msg, wparam, lparam) : 0;
}

WINUSERAPI BOOL WINAPI SetDlgItemTextW(HWND dialog, int id, LPCWSTR text)
{
    return (BOOL)SendDlgItemMessageW(dialog, id, WM_SETTEXT, 0, (LPARAM)text);
}

WINUSERAPI BOOL WINAPI SetDlgItemTextA(HWND dialog, int id, LPCSTR text)
{
    return (BOOL)SendDlgItemMessageA(dialog, id, WM_SETTEXT, 0, (LPARAM)text);
}

WINUSERAPI UINT WINAPI GetDlgItemTextW(HWND dialog, int id, LPWSTR buffer, int count)
{
    if (buffer && count > 0)
        buffer[0] = 0;
    return (UINT)SendDlgItemMessageW(dialog, id, WM_GETTEXT, (WPARAM)count, (LPARAM)buffer);
}

WINUSERAPI UINT WINAPI GetDlgItemTextA(HWND dialog, int id, LPSTR buffer, int count)
{
    if (buffer && count > 0)
        buffer[0] = 0;
    return (UINT)SendDlgItemMessageA(dialog, id, WM_GETTEXT, (WPARAM)count, (LPARAM)buffer);
}

WINUSERAPI BOOL WINAPI SetDlgItemInt(HWND dialog, int id, UINT value, BOOL is_signed)
{
    WCHAR buf[16];
    wsprintfW(buf, is_signed ? L"%d" : L"%u", value);
    return SetDlgItemTextW(dialog, id, buf);
}

WINUSERAPI UINT WINAPI GetDlgItemInt(HWND dialog, int id, BOOL *ok, BOOL is_signed)
{
    WCHAR buf[32];
    GetDlgItemTextW(dialog, id, buf, 32);
    WCHAR *p = buf;
    while (*p == L' ')
        p++;
    BOOL neg = FALSE;
    if (is_signed && *p == L'-') {
        neg = TRUE;
        p++;
    }
    UINT v = 0;
    BOOL any = FALSE;
    for (; *p >= L'0' && *p <= L'9'; p++) {
        v = v * 10 + (UINT)(*p - L'0');
        any = TRUE;
    }
    if (ok)
        *ok = any;
    return neg ? (UINT)-(int)v : v;
}

WINUSERAPI BOOL WINAPI CheckDlgButton(HWND dialog, int id, UINT check)
{
    SendDlgItemMessageW(dialog, id, BM_SETCHECK, check, 0);
    return TRUE;
}

WINUSERAPI UINT WINAPI IsDlgButtonChecked(HWND dialog, int id)
{
    return (UINT)SendDlgItemMessageW(dialog, id, BM_GETCHECK, 0, 0);
}

WINUSERAPI BOOL WINAPI CheckRadioButton(HWND dialog, int first, int last, int check)
{
    for (int id = first; id <= last; id++)
        SendDlgItemMessageW(dialog, id, BM_SETCHECK, id == check ? BST_CHECKED : BST_UNCHECKED, 0);
    return TRUE;
}
