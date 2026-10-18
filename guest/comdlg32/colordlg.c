/*
 * ChooseColor. the dialog has a grid of 48 basic colors, one of 16 custom colors the program keeps, and
 * once opened a hue and saturation field, a luminance bar and edits for the hue, saturation, luminance and
 * the three primaries. the boxes are placeholders in the template that this file draws and handles with
 * their own window procedure, so a template the program supplies works as long as it has the same
 * control identifiers.
 */
#include "dlgbase.h"
#include "dlgids.h"

#define HLSMAX 240
#define RGBMAX 255
#define HUE_UNDEFINED (HLSMAX * 2 / 3)

#define BASIC_COLORS 48
#define BOX_COLORS 64
#define COLUMNS 8

/* the grid of basic colors, row by row. */
static const COLORREF g_basic[BASIC_COLORS] = {
    0x8080FF, 0x80FFFF, 0x80FF80, 0x80FF00, 0xFFFF80, 0xFF8000, 0xC080FF, 0xFF80FF,
    0x0000FF, 0x00FFFF, 0x00FF80, 0x40FF00, 0xFFFF00, 0xC08000, 0xC08080, 0xFF00FF,
    0x404080, 0x4080FF, 0x00FF00, 0x808000, 0x804000, 0xFF8080, 0x400080, 0x8000FF,
    0x000080, 0x0080FF, 0x008000, 0x408000, 0xFF0000, 0xA00000, 0x800080, 0xFF0080,
    0x000040, 0x004080, 0x004000, 0x404000, 0x800000, 0x400000, 0x400040, 0x800040,
    0x000000, 0x008080, 0x408080, 0x808080, 0x808040, 0xC0C0C0, 0x400040, 0xFFFFFF,
};

typedef struct _CCDLG {
    CHOOSECOLORW *Cc;
    BOOL Ansi;
    HWND Dialog;
    COLORREF Color;
    COLORREF Boxes[BOX_COLORS];
    /* the box that is selected, and the one with the focus in each of the grids. */
    int Selected, FocusBasic, FocusCustom;
    int Hue, Sat, Lum;
    BOOL Open;
    BOOL Capture;
    int Dragging;
    int ClosedWidth, OpenWidth;
    HBITMAP Rainbow;
    int RainbowW, RainbowH;
    void *RainbowBits;
    DLGPROC Hook;
    BOOL SettingEdits;
    DWORD Flags;
} CCDLG;

static UINT g_msg_ok, g_msg_setrgb, g_msg_help;
static DWORD g_hook_tls = TLS_OUT_OF_INDEXES;
static LONG g_hook_once;
static BOOL g_pressed_cancel;

/* ------------------------------------------------------------------ hue, luminance and saturation */

static void rgb_to_hls(COLORREF color, int *hue, int *lum, int *sat)
{
    int r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    int high = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int low = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int sum = high + low, diff = high - low;
    int l = (sum * HLSMAX + RGBMAX) / (2 * RGBMAX), s, h;
    if (!diff) {
        s = 0;
        h = HUE_UNDEFINED;
    } else {
        s = l <= HLSMAX / 2 ? (diff * HLSMAX + sum / 2) / sum
                            : (diff * HLSMAX + (2 * RGBMAX - sum) / 2) / (2 * RGBMAX - sum);
        int rd = ((high - r) * (HLSMAX / 6) + diff / 2) / diff;
        int gd = ((high - g) * (HLSMAX / 6) + diff / 2) / diff;
        int bd = ((high - b) * (HLSMAX / 6) + diff / 2) / diff;
        if (r == high)
            h = bd - gd;
        else if (g == high)
            h = HLSMAX / 3 + rd - bd;
        else
            h = 2 * HLSMAX / 3 + gd - rd;
        if (h < 0)
            h += HLSMAX;
        if (h >= HLSMAX)
            h -= HLSMAX;
    }
    *hue = h;
    *lum = l;
    *sat = s;
}

static int hue_to_channel(int n1, int n2, int hue)
{
    if (hue < 0)
        hue += HLSMAX;
    if (hue > HLSMAX)
        hue -= HLSMAX;
    if (hue < HLSMAX / 6)
        return n1 + ((n2 - n1) * hue + HLSMAX / 12) / (HLSMAX / 6);
    if (hue < HLSMAX / 2)
        return n2;
    if (hue < HLSMAX * 2 / 3)
        return n1 + ((n2 - n1) * (HLSMAX * 2 / 3 - hue) + HLSMAX / 12) / (HLSMAX / 6);
    return n1;
}

static COLORREF hls_to_rgb(int hue, int lum, int sat)
{
    int r, g, b;
    if (!sat) {
        r = g = b = lum * RGBMAX / HLSMAX;
    } else {
        int m2 = lum <= HLSMAX / 2 ? (lum * (HLSMAX + sat) + HLSMAX / 2) / HLSMAX : lum + sat - (lum * sat + HLSMAX / 2) / HLSMAX;
        int m1 = 2 * lum - m2;
        r = (hue_to_channel(m1, m2, hue + HLSMAX / 3) * RGBMAX + HLSMAX / 2) / HLSMAX;
        g = (hue_to_channel(m1, m2, hue) * RGBMAX + HLSMAX / 2) / HLSMAX;
        b = (hue_to_channel(m1, m2, hue - HLSMAX / 3) * RGBMAX + HLSMAX / 2) / HLSMAX;
    }
    if (r > RGBMAX)
        r = RGBMAX;
    if (g > RGBMAX)
        g = RGBMAX;
    if (b > RGBMAX)
        b = RGBMAX;
    return RGB(r, g, b);
}

/* ------------------------------------------------------------------ state */

static COLORREF map_color(const CCDLG *d, COLORREF color)
{
    if (d->Flags & CC_SOLIDCOLOR) {
        HDC dc = GetDC(nullptr);
        color = GetNearestColor(dc, color);
        ReleaseDC(nullptr, dc);
    }
    return color;
}

static HWND item(const CCDLG *d, int id)
{
    return GetDlgItem(d->Dialog, id);
}

static void redraw(const CCDLG *d, int id)
{
    HWND w = item(d, id);
    if (w)
        InvalidateRect(w, nullptr, FALSE);
}

static void set_numbers(CCDLG *d, int first, const int *values, int count)
{
    d->SettingEdits = TRUE;
    for (int i = 0; i < count; i++)
        SetDlgItemInt(d->Dialog, first + i, (UINT)values[i], FALSE);
    d->SettingEdits = FALSE;
}

static void show_hls(CCDLG *d)
{
    int v[3] = {d->Hue, d->Sat, d->Lum};
    set_numbers(d, COLOR_HUE, v, 3);
}

static void show_rgb(CCDLG *d)
{
    int v[3] = {GetRValue(d->Color), GetGValue(d->Color), GetBValue(d->Color)};
    set_numbers(d, COLOR_RED, v, 3);
}

/* the hue, saturation and luminance of the current color. */
static void take_hls(CCDLG *d)
{
    rgb_to_hls(d->Color, &d->Hue, &d->Lum, &d->Sat);
}

static void redraw_open(const CCDLG *d)
{
    redraw(d, COLOR_RAINBOW);
    redraw(d, COLOR_LUMSCROLL);
    redraw(d, COLOR_CURRENT);
}

/* a new color from the program or from a box: the edits and the drawings follow when the dialog is open. */
static void set_color(CCDLG *d, COLORREF color)
{
    d->Color = map_color(d, color);
    if (d->Open) {
        take_hls(d);
        show_hls(d);
        show_rgb(d);
        redraw_open(d);
    }
}

/* the first of the 64 boxes that holds the color, which becomes the selected one. */
static BOOL find_box(CCDLG *d, COLORREF color)
{
    for (int i = 0; i < BOX_COLORS; i++) {
        if (d->Boxes[i] == color) {
            d->Selected = i;
            return TRUE;
        }
    }
    return FALSE;
}

static void select_box(CCDLG *d, int index)
{
    int old = d->Selected;
    d->Selected = index;
    if (index >= BASIC_COLORS)
        d->FocusCustom = index;
    else
        d->FocusBasic = index;
    if ((old >= BASIC_COLORS) != (index >= BASIC_COLORS) || old != index) {
        redraw(d, COLOR_BOX1);
        redraw(d, COLOR_CUSTOM1);
    }
}

/* ------------------------------------------------------------------ the rainbow */

static void make_rainbow(CCDLG *d, int w, int h)
{
    if (d->Rainbow && d->RainbowW == w && d->RainbowH == h)
        return;
    if (d->Rainbow)
        DeleteObject(d->Rainbow);
    d->Rainbow = nullptr;
    d->RainbowBits = nullptr;
    if (w <= 0 || h <= 0)
        return;
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    d->Rainbow = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &d->RainbowBits, nullptr, 0);
    d->RainbowW = w;
    d->RainbowH = h;
    if (!d->Rainbow)
        return;
    DWORD *pixels = d->RainbowBits;
    for (int y = 0; y < h; y++) {
        int sat = h > 1 ? (h - 1 - y) * HLSMAX / (h - 1) : 0;
        for (int x = 0; x < w; x++) {
            int hue = w > 1 ? x * (HLSMAX - 1) / (w - 1) : 0;
            COLORREF c = hls_to_rgb(hue, HLSMAX / 2, sat);
            pixels[y * w + x] = (DWORD)(GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c));
        }
    }
}

static void client_size(HWND w, int *cx, int *cy)
{
    RECT r;
    GetClientRect(w, &r);
    *cx = r.right;
    *cy = r.bottom;
}

static POINT rainbow_point(const CCDLG *d, int w, int h)
{
    POINT p;
    p.x = w > 1 ? d->Hue * (w - 1) / (HLSMAX - 1) : 0;
    p.y = h > 1 ? (HLSMAX - d->Sat) * (h - 1) / HLSMAX : 0;
    return p;
}

static void paint_rainbow(CCDLG *d, HWND w, HDC dc)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    RECT all = {0, 0, cx, cy};
    if (!d->Open) {
        FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
        return;
    }
    RECT inner = {2, 2, cx - 2, cy - 2};
    DrawEdge(dc, &all, EDGE_SUNKEN, BF_RECT);
    int iw = inner.right - inner.left, ih = inner.bottom - inner.top;
    make_rainbow(d, iw, ih);
    if (d->Rainbow) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, d->Rainbow);
        BitBlt(dc, inner.left, inner.top, iw, ih, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    /* the cross hair, a black cross with a white one around it. */
    POINT p = rainbow_point(d, iw, ih);
    p.x += inner.left;
    p.y += inner.top;
    HPEN white = CreatePen(PS_SOLID, 3, RGB(255, 255, 255)), black = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    int clip = SaveDC(dc);
    IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);
    HGDIOBJ old = SelectObject(dc, white);
    for (int pass = 0; pass < 2; pass++) {
        MoveToEx(dc, p.x - 8, p.y, nullptr);
        LineTo(dc, p.x - 2, p.y);
        MoveToEx(dc, p.x + 3, p.y, nullptr);
        LineTo(dc, p.x + 9, p.y);
        MoveToEx(dc, p.x, p.y - 8, nullptr);
        LineTo(dc, p.x, p.y - 2);
        MoveToEx(dc, p.x, p.y + 3, nullptr);
        LineTo(dc, p.x, p.y + 9);
        SelectObject(dc, black);
    }
    SelectObject(dc, old);
    RestoreDC(dc, clip);
    DeleteObject(white);
    DeleteObject(black);
}

/* the bar is the left part of the window, the arrow that marks the luminance sits to the right of it. */
#define ARROW_WIDTH 8

static int bar_height(int cy)
{
    return cy - 4;
}

static void paint_lum(CCDLG *d, HWND w, HDC dc)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    RECT all = {0, 0, cx, cy};
    FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
    if (!d->Open)
        return;
    RECT frame = {0, 0, cx - ARROW_WIDTH, cy};
    DrawEdge(dc, &frame, EDGE_SUNKEN, BF_RECT);
    int h = bar_height(cy);
    for (int y = 0; y < h; y++) {
        int lum = h > 1 ? (h - 1 - y) * HLSMAX / (h - 1) : 0;
        RECT line = {2, 2 + y, cx - ARROW_WIDTH - 2, 3 + y};
        DlgFillRect(dc, &line, hls_to_rgb(d->Hue, lum, d->Sat));
    }
    int y = 2 + (h > 1 ? (HLSMAX - d->Lum) * (h - 1) / HLSMAX : 0);
    int x = cx - ARROW_WIDTH;
    POINT tri[3] = {{x + ARROW_WIDTH, y - 4}, {x + 1, y}, {x + ARROW_WIDTH, y + 4}};
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ old = SelectObject(dc, brush);
    HGDIOBJ pen = SelectObject(dc, GetStockObject(BLACK_PEN));
    Polygon(dc, tri, 3);
    SelectObject(dc, pen);
    SelectObject(dc, old);
    DeleteObject(brush);
}

static void paint_current(CCDLG *d, HWND w, HDC dc)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    RECT all = {0, 0, cx, cy};
    DrawEdge(dc, &all, EDGE_SUNKEN, BF_RECT);
    if (!(d->Open || (d->Flags & CC_FULLOPEN))) {
        return;
    }
    HDC screen = GetDC(nullptr);
    COLORREF solid = GetNearestColor(screen, d->Color);
    ReleaseDC(nullptr, screen);
    RECT left = {2, 2, cx / 2, cy - 2}, right = {cx / 2, 2, cx - 2, cy - 2};
    DlgFillRect(dc, &left, d->Color);
    DlgFillRect(dc, &right, solid);
}

/* ------------------------------------------------------------------ the grids of boxes */

static void paint_grid(CCDLG *d, HWND w, HDC dc, int first, int rows, int focus)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    RECT all = {0, 0, cx, cy};
    FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
    BOOL has_focus = GetFocus() == w;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < COLUMNS; col++) {
            int index = first + row * COLUMNS + col;
            RECT cell = {col * cx / COLUMNS, row * cy / rows, (col + 1) * cx / COLUMNS, (row + 1) * cy / rows};
            RECT box = {cell.left + 3, cell.top + 2, cell.right - 3, cell.bottom - 2};
            if (index == d->Selected) {
                RECT mark = {cell.left + 1, cell.top, cell.right - 1, cell.bottom};
                DlgFillRect(dc, &mark, RGB(0, 0, 0));
                RECT gap = {mark.left + 1, mark.top + 1, mark.right - 1, mark.bottom - 1};
                DlgFillRect(dc, &gap, GetSysColor(COLOR_BTNFACE));
            }
            RECT face = box;
            DrawEdge(dc, &face, EDGE_SUNKEN, BF_RECT);
            DlgFillRect(dc, &face, d->Boxes[index]);
            if (has_focus && index == focus) {
                RECT f = {cell.left + 1, cell.top + 1, cell.right - 1, cell.bottom - 1};
                DrawFocusRect(dc, &f);
            }
        }
    }
}

static int box_at(HWND w, int rows, int x, int y)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    if (x < 0 || y < 0 || x >= cx || y >= cy)
        return -1;
    int col = x * COLUMNS / cx, row = y * rows / cy;
    return row * COLUMNS + col;
}

/* the box the arrow keys reach from another, or the same when the edge is in the way. */
static int move_focus(int index, int base, int rows, WPARAM key)
{
    int rel = index - base, col = rel % COLUMNS, row = rel / COLUMNS, last = rows * COLUMNS - 1;
    switch (key) {
    case VK_LEFT:
        if (col)
            rel--;
        break;
    case VK_RIGHT:
        if (col < COLUMNS - 1)
            rel++;
        break;
    case VK_UP:
        if (row)
            rel -= COLUMNS;
        break;
    case VK_DOWN:
        if (row < rows - 1)
            rel += COLUMNS;
        break;
    case VK_HOME:
        rel = 0;
        break;
    case VK_END:
        rel = last;
        break;
    }
    return base + rel;
}

static CCDLG *dialog_of(HWND box)
{
    return (CCDLG *)GetWindowLongPtrW(box, GWLP_USERDATA);
}

static void choose_box(CCDLG *d, int index)
{
    select_box(d, index);
    set_color(d, d->Boxes[index]);
}

static LRESULT CALLBACK grid_proc(HWND w, UINT message, WPARAM wparam, LPARAM lparam)
{
    CCDLG *d = dialog_of(w);
    BOOL custom = GetDlgCtrlID(w) == COLOR_CUSTOM1;
    int first = custom ? BASIC_COLORS : 0, rows = custom ? 2 : 6;
    if (!d)
        return DefWindowProcW(w, message, wparam, lparam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        paint_grid(d, w, dc, first, rows, custom ? d->FocusCustom : d->FocusBasic);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int index = box_at(w, rows, (short)LOWORD(lparam), (short)HIWORD(lparam));
        SetFocus(w);
        if (index >= 0) {
            choose_box(d, first + index);
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wparam < VK_END || wparam > VK_DOWN)
            break;
        int *focus = custom ? &d->FocusCustom : &d->FocusBasic;
        int next = move_focus(*focus, first, rows, wparam);
        if (next != *focus) {
            *focus = next;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CHAR:
        if (wparam == L' ') {
            choose_box(d, custom ? d->FocusCustom : d->FocusBasic);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        break;
    }
    return DefWindowProcW(w, message, wparam, lparam);
}

/* ------------------------------------------------------------------ the field and the bar */

static void hls_changed(CCDLG *d)
{
    d->Color = map_color(d, hls_to_rgb(d->Hue, d->Lum, d->Sat));
    show_rgb(d);
    redraw_open(d);
}

static void drag(CCDLG *d, HWND w, int x, int y)
{
    int cx, cy;
    client_size(w, &cx, &cy);
    if (d->Dragging == COLOR_RAINBOW) {
        int iw = cx - 4, ih = cy - 4;
        x -= 2;
        y -= 2;
        x = x < 0 ? 0 : (x > iw - 1 ? iw - 1 : x);
        y = y < 0 ? 0 : (y > ih - 1 ? ih - 1 : y);
        d->Hue = iw > 1 ? x * (HLSMAX - 1) / (iw - 1) : 0;
        d->Sat = ih > 1 ? HLSMAX - y * HLSMAX / (ih - 1) : 0;
        if (d->Sat > HLSMAX)
            d->Sat = HLSMAX;
    } else {
        int h = bar_height(cy);
        y -= 2;
        y = y < 0 ? 0 : (y > h - 1 ? h - 1 : y);
        d->Lum = h > 1 ? HLSMAX - y * HLSMAX / (h - 1) : 0;
    }
    show_hls(d);
    hls_changed(d);
}

static LRESULT CALLBACK field_proc(HWND w, UINT message, WPARAM wparam, LPARAM lparam)
{
    CCDLG *d = dialog_of(w);
    int id = GetDlgCtrlID(w);
    if (!d)
        return DefWindowProcW(w, message, wparam, lparam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (id == COLOR_RAINBOW)
            paint_rainbow(d, w, dc);
        else if (id == COLOR_LUMSCROLL)
            paint_lum(d, w, dc);
        else
            paint_current(d, w, dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (id == COLOR_CURRENT || !d->Open)
            return 0;
        d->Dragging = id;
        SetCapture(w);
        drag(d, w, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_MOUSEMOVE:
        if (d->Dragging == id && GetCapture() == w)
            drag(d, w, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        if (d->Dragging == id) {
            d->Dragging = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (id == COLOR_CURRENT)
            SendMessageW(d->Dialog, WM_COMMAND, COLOR_SOLID, 0);
        return 0;
    case WM_CAPTURECHANGED:
        d->Dragging = 0;
        return 0;
    }
    return DefWindowProcW(w, message, wparam, lparam);
}

/* ------------------------------------------------------------------ the dialog */

static void enable_open(CCDLG *d, BOOL enable)
{
    for (int id = COLOR_HUE; id <= COLOR_BLUE; id++) {
        HWND w = item(d, id);
        if (w)
            EnableWindow(w, enable);
    }
    for (int id = COLOR_HUELABEL; id <= COLOR_BLUELABEL; id++) {
        HWND w = item(d, id);
        if (w)
            EnableWindow(w, enable);
    }
    for (int id = COLOR_ADD; id <= COLOR_SOLID; id++) {
        HWND w = item(d, id);
        if (w)
            EnableWindow(w, enable);
    }
    HWND solid = item(d, COLOR_SOLIDLABEL);
    if (solid)
        EnableWindow(solid, enable);
}

/* the dialog gets the width of its left half until the user asks for the rest. */
static void close_up(CCDLG *d)
{
    HWND grid = item(d, COLOR_BOX1), rainbow = item(d, COLOR_RAINBOW);
    RECT g, r, window, client;
    if (!grid || !rainbow)
        return;
    GetWindowRect(grid, &g);
    GetWindowRect(rainbow, &r);
    MapWindowPoints(nullptr, d->Dialog, (LPPOINT)&g, 2);
    MapWindowPoints(nullptr, d->Dialog, (LPPOINT)&r, 2);
    GetWindowRect(d->Dialog, &window);
    GetClientRect(d->Dialog, &client);
    int frame = (window.right - window.left) - client.right;
    d->OpenWidth = window.right - window.left;
    d->ClosedWidth = (g.right + r.left) / 2 + frame;
    SetWindowPos(d->Dialog, nullptr, 0, 0, d->ClosedWidth, window.bottom - window.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void open_up(CCDLG *d)
{
    if (d->Open)
        return;
    d->Open = TRUE;
    RECT window;
    GetWindowRect(d->Dialog, &window);
    if (d->OpenWidth > d->ClosedWidth)
        SetWindowPos(d->Dialog, nullptr, 0, 0, d->OpenWidth, window.bottom - window.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    enable_open(d, TRUE);
    EnableWindow(item(d, COLOR_MIX), FALSE);
    take_hls(d);
    show_hls(d);
    show_rgb(d);
    InvalidateRect(d->Dialog, nullptr, TRUE);
    SetFocus(item(d, COLOR_HUE));
}

/* the answer is the colors the dialog ended with, handed back also when cancelled. */
static void keep_custom(CCDLG *d)
{
    if (d->Cc->lpCustColors)
        memcpy(d->Cc->lpCustColors, &d->Boxes[BASIC_COLORS], 16 * sizeof(COLORREF));
}

static LRESULT call_hook(CCDLG *d, UINT message, WPARAM wparam, LPARAM lparam)
{
    return d->Hook ? (LRESULT)d->Hook(d->Dialog, message, wparam, lparam) : 0;
}

static void end_dialog(CCDLG *d, INT_PTR result)
{
    if (d->Dragging) {
        d->Dragging = 0;
        ReleaseCapture();
    }
    EndDialog(d->Dialog, result);
}

static BOOL edit_changed(CCDLG *d, int id, int code)
{
    if (d->SettingEdits)
        return TRUE;
    if (code == EN_CHANGE) {
        BOOL ok;
        UINT value = GetDlgItemInt(d->Dialog, id, &ok, FALSE);
        if (!ok) {
            /* an empty box is left alone, so a number can be typed afresh. */
            return TRUE;
        }
        UINT limit = id == COLOR_HUE ? HLSMAX - 1 : (id == COLOR_SAT || id == COLOR_LUM ? HLSMAX : RGBMAX);
        if (value > limit) {
            value = limit;
            d->SettingEdits = TRUE;
            SetDlgItemInt(d->Dialog, id, value, FALSE);
            d->SettingEdits = FALSE;
        }
        if (id >= COLOR_HUE && id <= COLOR_LUM) {
            int *field = id == COLOR_HUE ? &d->Hue : (id == COLOR_SAT ? &d->Sat : &d->Lum);
            if ((int)value == *field)
                return TRUE;
            *field = (int)value;
            hls_changed(d);
        } else {
            COLORREF c = d->Color;
            int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
            if (id == COLOR_RED)
                r = (int)value;
            else if (id == COLOR_GREEN)
                g = (int)value;
            else
                b = (int)value;
            d->Color = map_color(d, RGB(r, g, b));
            take_hls(d);
            show_hls(d);
            redraw_open(d);
        }
    } else if (code == EN_KILLFOCUS) {
        /* what is not a number goes back to the value it stands for. */
        BOOL ok;
        GetDlgItemInt(d->Dialog, id, &ok, FALSE);
        if (!ok) {
            if (id >= COLOR_HUE && id <= COLOR_LUM)
                show_hls(d);
            else
                show_rgb(d);
        }
    }
    return TRUE;
}

static BOOL command(CCDLG *d, WORD id, WORD code, LPARAM lparam)
{
    switch (id) {
    case IDOK:
    case IDCANCEL:
    case IDABORT:
        if (id == IDOK)
            d->Cc->rgbResult = d->Color;
        else if (id == IDCANCEL)
            g_pressed_cancel = TRUE;
        keep_custom(d);
        if (id == IDOK && d->Hook) {
            if (!d->Ansi || TRUE) {
                if (call_hook(d, DlgMessage(&g_msg_ok, COLOROKSTRINGW), 0, (LPARAM)d->Cc))
                    return TRUE;
            }
        }
        end_dialog(d, id == IDABORT ? (INT_PTR)lparam : (id == IDOK ? 1 : 0));
        return TRUE;
    case COLOR_MIX:
        open_up(d);
        return TRUE;
    case COLOR_ADD:
        d->Boxes[d->FocusCustom] = d->Color;
        redraw(d, COLOR_CUSTOM1);
        /* the next free place goes down a column and then on to the next one. */
        if (d->FocusCustom < BOX_COLORS - 1)
            d->FocusCustom = d->FocusCustom < BOX_COLORS - COLUMNS ? d->FocusCustom + COLUMNS : d->FocusCustom - COLUMNS + 1;
        else
            d->FocusCustom = BASIC_COLORS;
        return TRUE;
    case COLOR_SOLID: {
        HDC dc = GetDC(nullptr);
        COLORREF solid = GetNearestColor(dc, d->Color);
        ReleaseDC(nullptr, dc);
        set_color(d, solid);
        return TRUE;
    }
    case 1038:
        if (d->Cc->hwndOwner)
            SendMessageW(d->Cc->hwndOwner, DlgMessage(&g_msg_help, HELPMSGSTRINGW), (WPARAM)d->Dialog, (LPARAM)d->Cc);
        return TRUE;
    case COLOR_HUE:
    case COLOR_SAT:
    case COLOR_LUM:
    case COLOR_RED:
    case COLOR_GREEN:
    case COLOR_BLUE:
        return edit_changed(d, id, code);
    }
    return TRUE;
}

static void subclass(CCDLG *d, int id, WNDPROC proc)
{
    HWND w = item(d, id);
    if (!w)
        return;
    SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)d);
    SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)proc);
}

static INT_PTR init_dialog(HWND hwnd, CCDLG *d)
{
    d->Dialog = hwnd;
    SetPropW(hwnd, L"MimeColorDlg", d);
    memcpy(d->Boxes, g_basic, sizeof(g_basic));
    if (d->Cc->lpCustColors)
        memcpy(&d->Boxes[BASIC_COLORS], d->Cc->lpCustColors, 16 * sizeof(COLORREF));
    else
        for (int i = BASIC_COLORS; i < BOX_COLORS; i++)
            d->Boxes[i] = RGB(255, 255, 255);
    d->Color = (d->Flags & CC_RGBINIT) ? d->Cc->rgbResult : 0;
    d->Selected = 0;
    d->FocusBasic = 0;
    d->FocusCustom = BASIC_COLORS;
    subclass(d, COLOR_BOX1, grid_proc);
    subclass(d, COLOR_CUSTOM1, grid_proc);
    subclass(d, COLOR_RAINBOW, field_proc);
    subclass(d, COLOR_LUMSCROLL, field_proc);
    subclass(d, COLOR_CURRENT, field_proc);
    for (int id = COLOR_HUE; id <= COLOR_BLUE; id++) {
        HWND e = item(d, id);
        if (e)
            SendMessageW(e, EM_SETLIMITTEXT, 3, 0);
    }
    if (d->Flags & (CC_FULLOPEN | CC_PREVENTFULLOPEN))
        EnableWindow(item(d, COLOR_MIX), FALSE);
    if (d->Flags & CC_SOLIDCOLOR) {
        HWND w = item(d, COLOR_SOLIDLABEL);
        if (w)
            ShowWindow(w, SW_HIDE);
    }
    if (d->Flags & CC_FULLOPEN) {
        d->Open = TRUE;
        take_hls(d);
        show_hls(d);
        show_rgb(d);
    } else {
        enable_open(d, FALSE);
        close_up(d);
    }
    if (find_box(d, d->Color)) {
        if (d->Selected >= BASIC_COLORS)
            d->FocusCustom = d->Selected;
        else
            d->FocusBasic = d->Selected;
    }
    if (!(d->Flags & CC_SHOWHELP)) {
        HWND help = item(d, 1038);
        if (help) {
            EnableWindow(help, FALSE);
            ShowWindow(help, SW_HIDE);
        }
    }
    if (d->Hook)
        return (INT_PTR)d->Hook(hwnd, WM_INITDIALOG, 0, (LPARAM)d->Cc);
    return TRUE;
}

static INT_PTR CALLBACK color_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    CCDLG *d = (CCDLG *)GetPropW(hwnd, L"MimeColorDlg");
    if (!d) {
        if (message == WM_INITDIALOG)
            return init_dialog(hwnd, (CCDLG *)lparam);
        /* messages sent while the window is made reach the hook of this thread. */
        DLGPROC early = g_hook_tls != TLS_OUT_OF_INDEXES ? (DLGPROC)TlsGetValue(g_hook_tls) : nullptr;
        return early ? early(hwnd, message, wparam, lparam) : 0;
    }
    INT_PTR result = d->Hook ? d->Hook(hwnd, message, wparam, lparam) : 0;
    if (result)
        return result;
    if (message == DlgMessage(&g_msg_setrgb, SETRGBSTRINGW)) {
        if (find_box(d, (COLORREF)lparam)) {
            if (d->Selected >= BASIC_COLORS)
                d->FocusCustom = d->Selected;
            else
                d->FocusBasic = d->Selected;
            redraw(d, COLOR_BOX1);
            redraw(d, COLOR_CUSTOM1);
        }
        set_color(d, (COLORREF)lparam);
        return TRUE;
    }
    switch (message) {
    case WM_COMMAND:
        return command(d, LOWORD(wparam), HIWORD(wparam), lparam);
    case WM_DESTROY:
        if (d->Rainbow) {
            DeleteObject(d->Rainbow);
            d->Rainbow = nullptr;
        }
        RemovePropW(hwnd, L"MimeColorDlg");
        return FALSE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the entry points */

static LPDLGTEMPLATEW build_template(void)
{
    static const CTL controls[] = {
        {CTL_STATIC, L"&Basic colors:", 0xFFFF, WS_VISIBLE | SS_LEFT, 6, 6, 134, 9},
        {CTL_STATIC, L"", COLOR_BOX1, WS_VISIBLE | WS_TABSTOP | WS_GROUP | SS_NOTIFY, 6, 17, 134, 78},
        {CTL_STATIC, L"&Custom colors:", 0xFFFF, WS_VISIBLE | SS_LEFT, 6, 100, 134, 9},
        {CTL_STATIC, L"", COLOR_CUSTOM1, WS_VISIBLE | WS_TABSTOP | SS_NOTIFY, 6, 111, 134, 28},
        {CTL_BUTTON, L"&Define Custom Colors >>", COLOR_MIX, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 6, 146, 134, 14},
        {CTL_BUTTON, L"OK", IDOK, WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 6, 168, 42, 14},
        {CTL_BUTTON, L"Cancel", IDCANCEL, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 51, 168, 42, 14},
        {CTL_BUTTON, L"&Help", 1038, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 96, 168, 42, 14},
        {CTL_STATIC, L"", COLOR_RAINBOW, WS_VISIBLE | WS_TABSTOP | SS_NOTIFY, 150, 6, 120, 104},
        {CTL_STATIC, L"", COLOR_LUMSCROLL, WS_VISIBLE | SS_NOTIFY, 276, 6, 22, 104},
        {CTL_STATIC, L"", COLOR_CURRENT, WS_VISIBLE | SS_NOTIFY, 150, 118, 40, 26},
        {CTL_STATIC, L"Color", COLOR_COLORLABEL, WS_VISIBLE | SS_RIGHT, 150, 148, 20, 9},
        {CTL_STATIC, L"|S&olid", COLOR_SOLIDLABEL, WS_VISIBLE | SS_LEFT, 171, 148, 24, 9},
        {CTL_STATIC, L"Hu&e:", COLOR_HUELABEL, WS_VISIBLE | SS_RIGHT, 198, 120, 24, 9},
        {CTL_EDIT, L"", COLOR_HUE, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 224, 118, 20, 12},
        {CTL_STATIC, L"&Sat:", COLOR_SATLABEL, WS_VISIBLE | SS_RIGHT, 198, 134, 24, 9},
        {CTL_EDIT, L"", COLOR_SAT, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 224, 132, 20, 12},
        {CTL_STATIC, L"&Lum:", COLOR_LUMLABEL, WS_VISIBLE | SS_RIGHT, 198, 148, 24, 9},
        {CTL_EDIT, L"", COLOR_LUM, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 224, 146, 20, 12},
        {CTL_STATIC, L"&Red:", COLOR_REDLABEL, WS_VISIBLE | SS_RIGHT, 250, 120, 26, 9},
        {CTL_EDIT, L"", COLOR_RED, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 278, 118, 20, 12},
        {CTL_STATIC, L"&Green:", COLOR_GREENLABEL, WS_VISIBLE | SS_RIGHT, 250, 134, 26, 9},
        {CTL_EDIT, L"", COLOR_GREEN, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 278, 132, 20, 12},
        {CTL_STATIC, L"Bl&ue:", COLOR_BLUELABEL, WS_VISIBLE | SS_RIGHT, 250, 148, 26, 9},
        {CTL_EDIT, L"", COLOR_BLUE, WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 278, 146, 20, 12},
        {CTL_BUTTON, L"&Add to Custom Colors", COLOR_ADD, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 150, 168, 148, 14},
        {CTL_BUTTON, L"&o", COLOR_SOLID, WS_VISIBLE | BS_PUSHBUTTON, 302, 202, 4, 14},
    };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | WS_VISIBLE;
    return DlgBuild(L"Color", style, 306, 190, controls, sizeof(controls) / sizeof(controls[0]));
}

static BOOL choose_color(CHOOSECOLORW *cc, BOOL ansi)
{
    DlgSetError(0);
    g_pressed_cancel = FALSE;
    if (!cc) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (cc->lStructSize != sizeof(CHOOSECOLORW)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    DWORD error = 0;
    if (!DlgHookValid(cc->Flags, CC_ENABLEHOOK, cc->lpfnHook, &error)) {
        DlgSetError(error);
        return FALSE;
    }
    if (!(cc->Flags & CC_ENABLEHOOK))
        cc->lpfnHook = nullptr;
    DLGSOURCE source = {cc->Flags, CC_ENABLETEMPLATE, CC_ENABLETEMPLATEHANDLE, (HINSTANCE)cc->hInstance,
                        cc->lpTemplateName, ansi};
    const void *given = DlgFindTemplate(&source, &error);
    if (error) {
        DlgSetError(error);
        return FALSE;
    }
    LPDLGTEMPLATEW built = given ? nullptr : build_template();
    if (!given && !built) {
        DlgSetError(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    CCDLG *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        if (built)
            HeapFree(GetProcessHeap(), 0, built);
        DlgSetError(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    d->Cc = cc;
    d->Ansi = ansi;
    d->Flags = cc->Flags;
    d->Hook = (DLGPROC)((cc->Flags & CC_ENABLEHOOK) ? cc->lpfnHook : nullptr);
    if (g_hook_tls == TLS_OUT_OF_INDEXES && !g_hook_once) {
        g_hook_once = 1;
        g_hook_tls = TlsAlloc();
    }
    if (g_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_hook_tls, (void *)d->Hook);
    INT_PTR answer = DialogBoxIndirectParamW(g_comdlg32, given ? (LPCDLGTEMPLATEW)given : built, cc->hwndOwner,
                                             color_proc, (LPARAM)d);
    if (g_hook_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_hook_tls, nullptr);
    HeapFree(GetProcessHeap(), 0, d);
    if (built)
        HeapFree(GetProcessHeap(), 0, built);
    if (answer == -1 || (answer == 0 && !g_pressed_cancel && !CommDlgExtendedError()))
        DlgSetError(CDERR_DIALOGFAILURE);
    return answer == 1;
}

CDAPI BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc)
{
    return choose_color(cc, FALSE);
}

CDAPI BOOL WINAPI ChooseColorA(LPCHOOSECOLORA cc)
{
    /* the structure is the same but for the name of the template. */
    return choose_color((CHOOSECOLORW *)cc, TRUE);
}
