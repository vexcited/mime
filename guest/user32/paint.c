/*
 * painting services and the user32 drawing helpers: rectangles, edges,
 * focus rectangles and DrawText.
 */
#include "user32_private.h"

WINUSERAPI HDC WINAPI BeginPaint(HWND hwnd, LPPAINTSTRUCT ps)
{
    UserCaretPaint(hwnd, TRUE);
    return NtUserBeginPaint(hwnd, ps);
}

WINUSERAPI BOOL WINAPI EndPaint(HWND hwnd, const PAINTSTRUCT *ps)
{
    BOOL r = NtUserEndPaint(hwnd, ps);
    UserCaretPaint(hwnd, FALSE);
    return r;
}

WINUSERAPI HDC WINAPI GetDC(HWND hwnd)
{
    return NtUserGetDC(hwnd);
}

WINUSERAPI HDC WINAPI GetWindowDC(HWND hwnd)
{
    return NtUserGetWindowDC(hwnd);
}

WINUSERAPI HDC WINAPI GetDCEx(HWND hwnd, HRGN clip, DWORD flags)
{
    return NtUserGetDCEx(hwnd, clip, flags);
}

WINUSERAPI int WINAPI ReleaseDC(HWND hwnd, HDC dc)
{
    return NtUserReleaseDC(hwnd, dc);
}

WINUSERAPI HWND WINAPI WindowFromDC(HDC dc)
{
    DWORD window = 0;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_WINDOW, &window))
        return nullptr;
    return (HWND)(ULONG_PTR)window;
}

WINUSERAPI BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *rect, BOOL erase)
{
    return NtUserInvalidateRect(hwnd, rect, erase);
}

WINUSERAPI BOOL WINAPI InvalidateRgn(HWND hwnd, HRGN region, BOOL erase)
{
    return NtUserRedrawWindow(hwnd, nullptr, region, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
}

WINUSERAPI BOOL WINAPI ValidateRect(HWND hwnd, const RECT *rect)
{
    return NtUserValidateRect(hwnd, rect);
}

WINUSERAPI BOOL WINAPI ValidateRgn(HWND hwnd, HRGN region)
{
    return NtUserRedrawWindow(hwnd, nullptr, region, RDW_VALIDATE | RDW_NOCHILDREN);
}

WINUSERAPI BOOL WINAPI RedrawWindow(HWND hwnd, const RECT *rect, HRGN region, UINT flags)
{
    return NtUserRedrawWindow(hwnd, rect, region, flags);
}

WINUSERAPI BOOL WINAPI GetUpdateRect(HWND hwnd, LPRECT rect, BOOL erase)
{
    return NtUserGetUpdateRect(hwnd, rect, erase);
}

WINUSERAPI int WINAPI GetUpdateRgn(HWND hwnd, HRGN region, BOOL erase)
{
    RECT r;
    if (!NtUserGetUpdateRect(hwnd, &r, erase)) {
        SetRectRgn(region, 0, 0, 0, 0);
        return NULLREGION;
    }
    SetRectRgn(region, r.left, r.top, r.right, r.bottom);
    return SIMPLEREGION;
}

WINUSERAPI BOOL WINAPI LockWindowUpdate(HWND hwnd)
{
    return TRUE;
}

/* ------------------------------------------------------------------ rectangles and edges */

static HBRUSH brush_of(HBRUSH brush)
{
    if ((ULONG_PTR)brush && (ULONG_PTR)brush <= COLOR_MENUBAR + 1)
        return GetSysColorBrush((int)(ULONG_PTR)brush - 1);
    return brush;
}

WINUSERAPI int WINAPI FillRect(HDC dc, const RECT *rect, HBRUSH brush)
{
    HGDIOBJ old = SelectObject(dc, brush_of(brush));
    PatBlt(dc, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, PATCOPY);
    SelectObject(dc, old);
    return 1;
}

WINUSERAPI int WINAPI FrameRect(HDC dc, const RECT *r, HBRUSH brush)
{
    if (r->right <= r->left || r->bottom <= r->top)
        return 0;
    HGDIOBJ old = SelectObject(dc, brush_of(brush));
    PatBlt(dc, r->left, r->top, r->right - r->left, 1, PATCOPY);
    PatBlt(dc, r->left, r->bottom - 1, r->right - r->left, 1, PATCOPY);
    PatBlt(dc, r->left, r->top, 1, r->bottom - r->top, PATCOPY);
    PatBlt(dc, r->right - 1, r->top, 1, r->bottom - r->top, PATCOPY);
    SelectObject(dc, old);
    return 1;
}

WINUSERAPI BOOL WINAPI InvertRect(HDC dc, const RECT *r)
{
    return PatBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, DSTINVERT);
}

/* the dotted focus rectangle: every other pixel inverted, like the classic look. */
void UserDrawFocusRect(HDC dc, const RECT *r)
{
    COLORREF old_text = SetTextColor(dc, RGB(0, 0, 0));
    for (LONG x = r->left; x < r->right; x += 2) {
        PatBlt(dc, x, r->top, 1, 1, DSTINVERT);
        PatBlt(dc, x, r->bottom - 1, 1, 1, DSTINVERT);
    }
    for (LONG y = r->top + 2; y < r->bottom - 1; y += 2) {
        PatBlt(dc, r->left, y, 1, 1, DSTINVERT);
        PatBlt(dc, r->right - 1, y, 1, 1, DSTINVERT);
    }
    SetTextColor(dc, old_text);
}

WINUSERAPI BOOL WINAPI DrawFocusRect(HDC dc, const RECT *r)
{
    UserDrawFocusRect(dc, r);
    return TRUE;
}

static void edge_line(HDC dc, int x, int y, int w, int h, int color)
{
    if (w <= 0 || h <= 0)
        return;
    HGDIOBJ old = SelectObject(dc, GetSysColorBrush(color));
    PatBlt(dc, x, y, w, h, PATCOPY);
    SelectObject(dc, old);
}

WINUSERAPI BOOL WINAPI DrawEdge(HDC dc, LPRECT rect, UINT edge, UINT flags)
{
    RECT r = *rect;
    /* outer then inner ring; raised edges are light at the top left. */
    for (int ring = 0; ring < 2; ring++) {
        UINT bits = ring == 0 ? (edge & (BDR_RAISEDOUTER | BDR_SUNKENOUTER)) : (edge & (BDR_RAISEDINNER | BDR_SUNKENINNER));
        if (!bits)
            continue;
        BOOL raised = ring == 0 ? (bits & BDR_RAISEDOUTER) != 0 : (bits & BDR_RAISEDINNER) != 0;
        int tl, br;
        if (flags & BF_FLAT) {
            tl = br = COLOR_3DSHADOW;
        } else if (ring == 0) {
            tl = raised ? COLOR_3DLIGHT : COLOR_3DSHADOW;
            br = raised ? COLOR_3DDKSHADOW : COLOR_3DHILIGHT;
        } else {
            tl = raised ? COLOR_3DHILIGHT : COLOR_3DDKSHADOW;
            br = raised ? COLOR_3DSHADOW : COLOR_3DLIGHT;
        }
        if (flags & BF_TOP)
            edge_line(dc, r.left, r.top, r.right - r.left, 1, tl);
        if (flags & BF_LEFT)
            edge_line(dc, r.left, r.top, 1, r.bottom - r.top, tl);
        if (flags & BF_BOTTOM)
            edge_line(dc, r.left, r.bottom - 1, r.right - r.left, 1, br);
        if (flags & BF_RIGHT)
            edge_line(dc, r.right - 1, r.top, 1, r.bottom - r.top, br);
        if (flags & BF_LEFT)
            r.left++;
        if (flags & BF_TOP)
            r.top++;
        if (flags & BF_RIGHT)
            r.right--;
        if (flags & BF_BOTTOM)
            r.bottom--;
    }
    if (flags & BF_MIDDLE)
        FillRect(dc, &r, GetSysColorBrush(COLOR_3DFACE));
    if (flags & BF_ADJUST)
        *rect = r;
    return TRUE;
}

/* ------------------------------------------------------------------ frame controls */

enum {
    GLYPH_UP,
    GLYPH_DOWN,
    GLYPH_LEFT,
    GLYPH_RIGHT,
    GLYPH_CHECK,
    GLYPH_BULLET,
    GLYPH_CLOSE,
    GLYPH_MIN,
    GLYPH_MAX,
    GLYPH_RESTORE,
    GLYPH_HELP,
};

static void line(HDC dc, int x0, int y0, int x1, int y1)
{
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
}

/* the glyph fills the square centered in r. */
static void draw_glyph(HDC dc, const RECT *r, int glyph, COLORREF color)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    int s = (w < h ? w : h) * (glyph <= GLYPH_RIGHT ? 50 : 60) / 100;
    if (s < 4)
        s = 4;
    RECT g = {r->left + (w - s) / 2, r->top + (h - s) / 2, 0, 0};
    g.right = g.left + s;
    g.bottom = g.top + s;
    int cx = (g.left + g.right) / 2, cy = (g.top + g.bottom) / 2;
    int t = s / 8 > 1 ? s / 8 : 1;
    HPEN pen = CreatePen(PS_SOLID, glyph == GLYPH_CHECK ? (s / 6 > 1 ? s / 6 : 1) : t, color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, brush);
    switch (glyph) {
    case GLYPH_UP:
    case GLYPH_DOWN: {
        int d = glyph == GLYPH_UP ? -1 : 1;
        POINT p[3] = {{cx, cy + d * s / 4}, {cx - s / 2, cy - d * s / 4}, {cx + s / 2, cy - d * s / 4}};
        Polygon(dc, p, 3);
        break;
    }
    case GLYPH_LEFT:
    case GLYPH_RIGHT: {
        int d = glyph == GLYPH_LEFT ? -1 : 1;
        POINT p[3] = {{cx + d * s / 4, cy}, {cx - d * s / 4, cy - s / 2}, {cx - d * s / 4, cy + s / 2}};
        Polygon(dc, p, 3);
        break;
    }
    case GLYPH_CHECK: {
        POINT p[3] = {{g.left, cy}, {g.left + s / 3, g.bottom - s / 6}, {g.right, g.top + s / 6}};
        Polyline(dc, p, 3);
        break;
    }
    case GLYPH_BULLET:
        Ellipse(dc, cx - s / 4, cy - s / 4, cx + s / 4 + 1, cy + s / 4 + 1);
        break;
    case GLYPH_CLOSE:
        line(dc, g.left, g.top, g.right, g.bottom);
        line(dc, g.right, g.top, g.left, g.bottom);
        break;
    case GLYPH_MIN:
        PatBlt(dc, g.left, g.bottom - t, s, t, PATCOPY);
        break;
    case GLYPH_MAX:
        PatBlt(dc, g.left, g.top, s, 2 * t, PATCOPY);
        PatBlt(dc, g.left, g.top, t, s, PATCOPY);
        PatBlt(dc, g.right - t, g.top, t, s, PATCOPY);
        PatBlt(dc, g.left, g.bottom - t, s, t, PATCOPY);
        break;
    case GLYPH_RESTORE: {
        int b = s * 3 / 4, fx = g.left, fy = g.bottom - b;
        /* the back window, then the front one over a cleared face. */
        PatBlt(dc, g.right - b, g.top, b, 2 * t, PATCOPY);
        PatBlt(dc, g.right - t, g.top, t, b, PATCOPY);
        HGDIOBJ glyph_brush = SelectObject(dc, GetSysColorBrush(COLOR_BTNFACE));
        PatBlt(dc, fx, fy, b, b, PATCOPY);
        SelectObject(dc, glyph_brush);
        PatBlt(dc, fx, fy, b, 2 * t, PATCOPY);
        PatBlt(dc, fx, fy, t, b, PATCOPY);
        PatBlt(dc, fx + b - t, fy, t, b, PATCOPY);
        PatBlt(dc, fx, fy + b - t, b, t, PATCOPY);
        break;
    }
    case GLYPH_HELP: {
        COLORREF old_color = SetTextColor(dc, color);
        int old_mode = SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, L"?", 1, &g, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SetBkMode(dc, old_mode);
        SetTextColor(dc, old_color);
        break;
    }
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

/* draws a raised or pushed face and shrinks r to the area inside the edge. */
static void draw_face(HDC dc, RECT *r, UINT state)
{
    UINT edge = (state & (DFCS_PUSHED | DFCS_CHECKED)) ? EDGE_SUNKEN : EDGE_RAISED;
    UINT flags = BF_RECT | BF_ADJUST | ((state & DFCS_FLAT) ? BF_FLAT : 0);
    if (!(state & DFCS_TRANSPARENT))
        flags |= BF_MIDDLE;
    DrawEdge(dc, r, edge, flags);
}

static RECT square_in(const RECT *r)
{
    int w = r->right - r->left, h = r->bottom - r->top, s = w < h ? w : h;
    RECT q = {r->left + (w - s) / 2, r->top + (h - s) / 2, 0, 0};
    q.right = q.left + s;
    q.bottom = q.top + s;
    return q;
}

static void draw_check_box(HDC dc, const RECT *r, UINT state)
{
    RECT box = square_in(r);
    BOOL face = (state & (DFCS_PUSHED | DFCS_INACTIVE)) != 0;
    FillRect(dc, &box, GetSysColorBrush(face ? COLOR_3DFACE : COLOR_WINDOW));
    DrawEdge(dc, &box, EDGE_SUNKEN, BF_RECT | BF_ADJUST | ((state & (DFCS_FLAT | DFCS_MONO)) ? BF_FLAT : 0));
    if (state & DFCS_CHECKED) {
        int color = (state & (DFCS_INACTIVE | DFCS_BUTTON3STATE)) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT;
        draw_glyph(dc, &box, GLYPH_CHECK, GetSysColor(color));
    }
}

static void draw_radio(HDC dc, const RECT *r, UINT state)
{
    RECT box = square_in(r);
    UINT kind = state & 0xFF;
    BOOL mask = (kind & DFCS_BUTTONRADIOMASK) != 0;
    if (mask)
        FillRect(dc, &box, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, mask ? RGB(0, 0, 0) : GetSysColor(COLOR_3DSHADOW));
    BOOL face = (state & (DFCS_PUSHED | DFCS_INACTIVE)) != 0;
    HBRUSH fill = mask ? (HBRUSH)GetStockObject(BLACK_BRUSH) : GetSysColorBrush(face ? COLOR_3DFACE : COLOR_WINDOW);
    HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, fill);
    Ellipse(dc, box.left, box.top, box.right, box.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    if (!mask && (state & DFCS_CHECKED)) {
        RECT dot = box;
        InflateRect(&dot, -(box.right - box.left) / 4, -(box.bottom - box.top) / 4);
        HBRUSH b = GetSysColorBrush((state & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN)), ob = SelectObject(dc, b);
        Ellipse(dc, dot.left, dot.top, dot.right + 1, dot.bottom + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
    }
}

static void draw_size_grip(HDC dc, const RECT *r, UINT state)
{
    if (!(state & DFCS_TRANSPARENT))
        FillRect(dc, r, GetSysColorBrush(COLOR_3DFACE));
    int w = r->right - r->left, h = r->bottom - r->top, s = w < h ? w : h;
    BOOL left = (state & 0xFF) == DFCS_SCROLLSIZEGRIPRIGHT;
    HPEN shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
    HPEN light = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
    HGDIOBJ old = SelectObject(dc, shadow);
    for (int i = 1; i <= 3; i++) {
        int off = i * s / 4;
        for (int k = 0; k < 3; k++) {
            SelectObject(dc, k == 2 ? light : shadow);
            int o = off - k;
            if (left)
                line(dc, r->left + o, r->bottom - 1, r->left - 1, r->bottom - o - 1);
            else
                line(dc, r->right - o, r->bottom - 1, r->right, r->bottom - o - 1);
        }
    }
    SelectObject(dc, old);
    DeleteObject(shadow);
    DeleteObject(light);
}

WINUSERAPI BOOL WINAPI DrawFrameControl(HDC dc, LPRECT rect, UINT type, UINT state)
{
    if (!dc || !rect)
        return FALSE;
    RECT r = *rect;
    UINT kind = state & 0xFF;
    COLORREF text = GetSysColor((state & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
    int glyph;
    switch (type) {
    case DFC_BUTTON:
        if (kind & DFCS_BUTTONPUSH) {
            draw_face(dc, &r, state);
            if (state & DFCS_ADJUSTRECT)
                *rect = r;
        } else if (kind & (DFCS_BUTTONRADIO | DFCS_BUTTONRADIOIMAGE | DFCS_BUTTONRADIOMASK)) {
            draw_radio(dc, &r, state);
        } else {
            draw_check_box(dc, &r, state);
        }
        return TRUE;
    case DFC_CAPTION:
        if (kind > DFCS_CAPTIONHELP)
            return FALSE;
        glyph = kind == DFCS_CAPTIONCLOSE     ? GLYPH_CLOSE
                : kind == DFCS_CAPTIONMIN     ? GLYPH_MIN
                : kind == DFCS_CAPTIONMAX     ? GLYPH_MAX
                : kind == DFCS_CAPTIONRESTORE ? GLYPH_RESTORE
                                              : GLYPH_HELP;
        break;
    case DFC_SCROLL:
        if (kind == DFCS_SCROLLSIZEGRIP || kind == DFCS_SCROLLSIZEGRIPRIGHT) {
            draw_size_grip(dc, &r, state);
            return TRUE;
        }
        if (kind == DFCS_SCROLLUP)
            glyph = GLYPH_UP;
        else if (kind == DFCS_SCROLLDOWN || kind == DFCS_SCROLLCOMBOBOX)
            glyph = GLYPH_DOWN;
        else if (kind == DFCS_SCROLLLEFT)
            glyph = GLYPH_LEFT;
        else if (kind == DFCS_SCROLLRIGHT)
            glyph = GLYPH_RIGHT;
        else
            return FALSE;
        break;
    case DFC_MENU:
    case DFC_POPUPMENU:
        if (kind == DFCS_MENUARROW)
            glyph = GLYPH_RIGHT;
        else if (kind == DFCS_MENUARROWRIGHT)
            glyph = GLYPH_LEFT;
        else if (kind == DFCS_MENUCHECK)
            glyph = GLYPH_CHECK;
        else if (kind == DFCS_MENUBULLET)
            glyph = GLYPH_BULLET;
        else
            return FALSE;
        /* DFC_MENU draws a black on white mask, DFC_POPUPMENU only the glyph. */
        if (type == DFC_MENU) {
            FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            draw_glyph(dc, &r, glyph, RGB(0, 0, 0));
        } else {
            draw_glyph(dc, &r, glyph, GetSysColor(COLOR_MENUTEXT));
        }
        return TRUE;
    default:
        return FALSE;
    }
    /* caption buttons and scroll arrows share the button face. */
    draw_face(dc, &r, state);
    if (state & DFCS_ADJUSTRECT)
        *rect = r;
    RECT g = r;
    if (state & DFCS_PUSHED)
        OffsetRect(&g, 1, 1);
    if (state & DFCS_INACTIVE) {
        RECT emboss = g;
        OffsetRect(&emboss, 1, 1);
        draw_glyph(dc, &emboss, glyph, GetSysColor(COLOR_3DHILIGHT));
        text = GetSysColor(COLOR_3DSHADOW);
    }
    draw_glyph(dc, &g, glyph, text);
    return TRUE;
}

/* ------------------------------------------------------------------ DrawState and GrayString */

static SIZE state_item_size(HDC dc, UINT type, LPARAM ldata, int len)
{
    SIZE size = {0, 0};
    switch (type) {
    case DST_TEXT:
    case DST_PREFIXTEXT: {
        RECT r = {0, 0, 0, 0};
        DrawTextW(dc, (LPCWSTR)ldata, len, &r, DT_CALCRECT | DT_NOCLIP | (type == DST_TEXT ? DT_NOPREFIX : 0));
        size.cx = r.right;
        size.cy = r.bottom;
        break;
    }
    case DST_ICON: {
        ICONINFO ii;
        if (GetIconInfo((HICON)ldata, &ii)) {
            BITMAP bm;
            if (GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm)) {
                size.cx = bm.bmWidth;
                size.cy = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
            }
            if (ii.hbmColor)
                DeleteObject(ii.hbmColor);
            if (ii.hbmMask)
                DeleteObject(ii.hbmMask);
        }
        break;
    }
    case DST_BITMAP: {
        BITMAP bm;
        if (GetObjectW((HBITMAP)ldata, sizeof(bm), &bm)) {
            size.cx = bm.bmWidth;
            size.cy = bm.bmHeight;
        }
        break;
    }
    }
    return size;
}

static BOOL draw_state_item(HDC dc, DRAWSTATEPROC proc, UINT type, LPARAM ldata, WPARAM wdata, int len, int x,
                            int y, int cx, int cy, UINT flags, COLORREF color)
{
    COLORREF old_color = color != CLR_INVALID ? SetTextColor(dc, color) : CLR_INVALID;
    BOOL ok = FALSE;
    if (proc && type != DST_ICON && type != DST_BITMAP) {
        /* the callback draws at the origin of the item. */
        POINT org;
        OffsetViewportOrgEx(dc, x, y, &org);
        ok = proc(dc, ldata, wdata, cx, cy);
        SetViewportOrgEx(dc, org.x, org.y, nullptr);
    } else if (type == DST_TEXT || type == DST_PREFIXTEXT) {
        RECT r = {x, y, x + cx, y + cy};
        UINT format = DT_NOCLIP | (type == DST_TEXT ? DT_NOPREFIX : 0) | ((flags & DSS_HIDEPREFIX) ? DT_HIDEPREFIX : 0) |
                      ((flags & DSS_PREFIXONLY) ? DT_PREFIXONLY : 0) | ((flags & DSS_RIGHT) ? DT_RIGHT : 0);
        int old_mode = SetBkMode(dc, TRANSPARENT);
        ok = DrawTextW(dc, (LPCWSTR)ldata, len, &r, format) != 0;
        SetBkMode(dc, old_mode);
    } else if (type == DST_ICON) {
        ok = DrawIconEx(dc, x, y, (HICON)ldata, cx, cy, 0, nullptr, DI_NORMAL);
    } else if (type == DST_BITMAP) {
        HDC mem = CreateCompatibleDC(dc);
        if (mem) {
            HGDIOBJ old = SelectObject(mem, (HBITMAP)ldata);
            ok = BitBlt(dc, x, y, cx, cy, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
    }
    if (old_color != CLR_INVALID)
        SetTextColor(dc, old_color);
    return ok;
}

/* disabled text and complex items get the classic embossed look, icons and bitmaps are drawn unchanged. */
WINUSERAPI BOOL WINAPI DrawStateW(HDC dc, HBRUSH brush, DRAWSTATEPROC proc, LPARAM ldata, WPARAM wdata, int x, int y,
                                  int cx, int cy, UINT flags)
{
    UINT type = flags & 0x000F;
    if (!dc || type > DST_BITMAP || (type == DST_COMPLEX && !proc))
        return FALSE;
    int len = (int)wdata;
    if ((type == DST_TEXT || type == DST_PREFIXTEXT) && !len)
        len = ldata ? lstrlenW((LPCWSTR)ldata) : 0;
    if (!cx || !cy) {
        SIZE size = state_item_size(dc, type, ldata, len);
        if (!cx)
            cx = size.cx;
        if (!cy)
            cy = size.cy;
    }
    BOOL text = type != DST_ICON && type != DST_BITMAP;
    if (text && (flags & DSS_DISABLED)) {
        draw_state_item(dc, proc, type, ldata, wdata, len, x + 1, y + 1, cx, cy, flags, GetSysColor(COLOR_3DHILIGHT));
        return draw_state_item(dc, proc, type, ldata, wdata, len, x, y, cx, cy, flags, GetSysColor(COLOR_3DSHADOW));
    }
    COLORREF color = CLR_INVALID;
    if (text && (flags & DSS_MONO)) {
        LOGBRUSH lb;
        color = brush && GetObjectW(brush, sizeof(lb), &lb) ? lb.lbColor : GetSysColor(COLOR_WINDOWTEXT);
    }
    return draw_state_item(dc, proc, type, ldata, wdata, len, x, y, cx, cy, flags, color);
}

/*
 * plain text is drawn in the gray text color. a callback draws black on white into a memory bitmap, whose black
 * pixels are then painted with the brush, the gray text brush by default.
 */
WINUSERAPI BOOL WINAPI GrayStringW(HDC dc, HBRUSH brush, GRAYSTRINGPROC proc, LPARAM data, int count, int x, int y,
                                   int cx, int cy)
{
    if (!dc)
        return FALSE;
    LPCWSTR text = (LPCWSTR)data;
    if (count <= 0 && text && !IS_INTRESOURCE(text))
        count = lstrlenW(text);
    if ((!cx || !cy) && text && !IS_INTRESOURCE(text)) {
        SIZE size = {0, 0};
        GetTextExtentPoint32W(dc, text, count, &size);
        if (!cx)
            cx = size.cx;
        if (!cy)
            cy = size.cy;
    }
    if (!proc) {
        if (!text)
            return FALSE;
        COLORREF old = SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        BOOL ok = TextOutW(dc, x, y, text, count);
        SetTextColor(dc, old);
        return ok;
    }
    if (cx <= 0 || cy <= 0)
        return FALSE;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = mem ? CreateCompatibleBitmap(dc, cx, cy) : nullptr;
    if (!bitmap) {
        if (mem)
            DeleteDC(mem);
        return FALSE;
    }
    HGDIOBJ old_bitmap = SelectObject(mem, bitmap);
    HGDIOBJ old_font = SelectObject(mem, GetCurrentObject(dc, OBJ_FONT));
    PatBlt(mem, 0, 0, cx, cy, WHITENESS);
    SetTextColor(mem, RGB(0, 0, 0));
    SetBkMode(mem, TRANSPARENT);
    BOOL ok = proc(mem, data, count);
    if (ok) {
        HGDIOBJ old_brush = SelectObject(dc, brush ? brush : GetSysColorBrush(COLOR_GRAYTEXT));
        /* PSDPxax: the brush where the source is black, the destination where it is white. */
        BitBlt(dc, x, y, cx, cy, mem, 0, 0, 0x00B8074A);
        SelectObject(dc, old_brush);
    }
    SelectObject(mem, old_font);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    return ok;
}

/* ------------------------------------------------------------------ DrawText */

typedef struct TEXT_LINE {
    int start;  /* index in the processed text. */
    int length;
    int width;
} TEXT_LINE;

static int text_width(HDC dc, LPCWSTR text, int count, UINT format, int tab_width)
{
    if (count <= 0)
        return 0;
    if (!(format & DT_EXPANDTABS)) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, count, &size);
        return size.cx;
    }
    int x = 0, start = 0;
    for (int i = 0; i <= count; i++) {
        if (i == count || text[i] == L'\t') {
            SIZE size = {0, 0};
            if (i > start)
                GetTextExtentPoint32W(dc, text + start, i - start, &size);
            x += size.cx;
            if (i < count && tab_width)
                x = (x / tab_width + 1) * tab_width;
            start = i + 1;
        }
    }
    return x;
}

/* removes mnemonic prefixes; returns the processed length and the position of the underlined character. */
static int strip_prefix(LPCWSTR in, int count, LPWSTR out, int *underline, UINT format)
{
    int n = 0;
    *underline = -1;
    for (int i = 0; i < count; i++) {
        if (in[i] == L'&' && !(format & DT_NOPREFIX)) {
            if (i + 1 < count && in[i + 1] == L'&') {
                out[n++] = L'&';
                i++;
                continue;
            }
            if (i + 1 < count && *underline < 0 && !(format & DT_HIDEPREFIX))
                *underline = n;
            continue;
        }
        out[n++] = in[i];
    }
    return n;
}

static void draw_underline(HDC dc, int x, int y, LPCWSTR line, int pos, const TEXTMETRICW *tm, UINT format,
                           int tab_width)
{
    int left = x + text_width(dc, line, pos, format, tab_width);
    SIZE ch;
    GetTextExtentPoint32W(dc, line + pos, 1, &ch);
    HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(dc));
    HGDIOBJ old = SelectObject(dc, pen);
    int uy = y + tm->tmAscent + 1;
    MoveToEx(dc, left, uy, nullptr);
    LineTo(dc, left + ch.cx, uy);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void draw_tabbed(HDC dc, int x, int y, LPCWSTR text, int count, UINT format, int tab_width, const RECT *clip)
{
    UINT opts = (format & DT_NOCLIP) ? 0 : ETO_CLIPPED;
    if (!(format & DT_EXPANDTABS)) {
        ExtTextOutW(dc, x, y, opts, clip, text, (UINT)count, nullptr);
        return;
    }
    int pos = x, start = 0;
    for (int i = 0; i <= count; i++) {
        if (i == count || text[i] == L'\t') {
            if (i > start) {
                ExtTextOutW(dc, pos, y, opts, clip, text + start, (UINT)(i - start), nullptr);
                pos += text_width(dc, text + start, i - start, 0, 0);
            }
            if (i < count && tab_width)
                pos = x + ((pos - x) / tab_width + 1) * tab_width;
            start = i + 1;
        }
    }
}

/* splits text into lines at newlines and, with DT_WORDBREAK, at word boundaries within width. */
static int break_lines(HDC dc, LPCWSTR text, int count, UINT format, int width, int tab_width, TEXT_LINE *lines,
                       int max_lines)
{
    int n = 0, i = 0;
    BOOL single = (format & DT_SINGLELINE) != 0;
    while (i <= count && n < max_lines) {
        int start = i, end = i;
        while (end < count && (single || (text[end] != L'\n' && text[end] != L'\r')))
            end++;
        /* the paragraph [start, end) is cut into lines that fit. */
        int p = start;
        do {
            int line_end = end;
            if ((format & DT_WORDBREAK) && !single && width > 0 && text_width(dc, text + p, end - p, format, tab_width) > width) {
                int last_break = -1;
                int k = p;
                for (; k < end; k++) {
                    if (text_width(dc, text + p, k - p + 1, format, tab_width) > width)
                        break;
                    if (text[k] == L' ' || text[k] == L'\t' || text[k] == L'-')
                        last_break = k;
                }
                if (last_break >= p)
                    line_end = text[last_break] == L'-' ? last_break + 1 : last_break;
                else if (format & DT_EDITCONTROL)
                    line_end = k > p ? k : p + 1;
                else {
                    /* a single word wider than the line: keep it whole, like windows without DT_EDITCONTROL. */
                    line_end = p;
                    while (line_end < end && text[line_end] != L' ')
                        line_end++;
                }
            }
            int len = line_end - p;
            /* trailing spaces of a wrapped line are dropped. */
            while (len > 0 && line_end < end && text[p + len - 1] == L' ')
                len--;
            lines[n].start = p;
            lines[n].length = len;
            lines[n].width = text_width(dc, text + p, len, format, tab_width);
            n++;
            p = line_end;
            while (p < end && text[p] == L' ' && line_end < end)
                p++;
        } while (p < end && n < max_lines);
        if (end >= count)
            break;
        i = end + 1;
        if (text[end] == L'\r' && i < count && text[i] == L'\n')
            i++;
        if (i > count)
            break;
        if (i == count) {
            /* a trailing newline starts an empty last line. */
            if (n < max_lines)
                lines[n++] = (TEXT_LINE){count, 0, 0};
            break;
        }
    }
    return n;
}

static void apply_end_ellipsis(HDC dc, LPWSTR text, int *length, int width, UINT format, int tab_width)
{
    if (text_width(dc, text, *length, format, tab_width) <= width)
        return;
    int dots = text_width(dc, L"...", 3, 0, 0);
    int n = *length;
    while (n > 0 && text_width(dc, text, n, format, tab_width) + dots > width)
        n--;
    text[n] = L'.';
    text[n + 1] = L'.';
    text[n + 2] = L'.';
    *length = n + 3;
}

WINUSERAPI int WINAPI DrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params)
{
    if (!text || !rect)
        return 0;
    if (count < 0)
        count = (int)wcslen(text);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int line_height = tm.tmHeight + ((format & DT_EXTERNALLEADING) ? tm.tmExternalLeading : 0);
    int tab_width = tm.tmAveCharWidth * ((format & DT_TABSTOP) ? (int)((format >> 8) & 0xFF) : 8);
    if (format & DT_TABSTOP)
        format &= ~0xFF00u;
    if (params && params->iTabLength)
        tab_width = tm.tmAveCharWidth * params->iTabLength;

    /* processed copy without prefixes, with room for an ellipsis. */
    LPWSTR buf = UserAlloc(((SIZE_T)count + 4) * sizeof(WCHAR));
    if (!buf)
        return 0;
    int underline;
    int length = strip_prefix(text, count, buf, &underline, format);
    if (format & DT_PREFIXONLY) {
        UserFree(buf);
        return 0;
    }

    int width = rect->right - rect->left;
    TEXT_LINE local[64];
    int max_lines = length / 2 + 2;
    TEXT_LINE *lines = max_lines <= 64 ? local : UserAlloc((SIZE_T)max_lines * sizeof(TEXT_LINE));
    if (!lines) {
        UserFree(buf);
        return 0;
    }
    if (format & DT_SINGLELINE) {
        for (int i = 0; i < length; i++)
            if (buf[i] == L'\r' || buf[i] == L'\n')
                buf[i] = L' ';
        if (format & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS)) {
            apply_end_ellipsis(dc, buf, &length, width, format, tab_width);
            if (underline >= length - 3)
                underline = -1;
        }
    }
    int nlines = break_lines(dc, buf, length, format, width, tab_width, lines, max_lines);
    int max_width = 0;
    for (int i = 0; i < nlines; i++)
        if (lines[i].width > max_width)
            max_width = lines[i].width;
    int total = nlines * line_height;

    if (format & DT_CALCRECT) {
        if (format & DT_SINGLELINE) {
            rect->right = rect->left + max_width;
            rect->bottom = rect->top + line_height;
        } else {
            if (max_width > width || !(format & DT_WORDBREAK))
                rect->right = rect->left + max_width;
            else if (format & DT_WORDBREAK)
                rect->right = rect->left + max_width;
            rect->bottom = rect->top + total;
        }
        if (lines != local)
            UserFree(lines);
        UserFree(buf);
        return total;
    }

    int y = rect->top;
    if (format & DT_SINGLELINE) {
        if (format & DT_VCENTER)
            y = rect->top + (rect->bottom - rect->top - line_height) / 2;
        else if (format & DT_BOTTOM)
            y = rect->bottom - line_height;
    }
    UINT old_align = SetTextAlign(dc, TA_LEFT | TA_TOP);
    for (int i = 0; i < nlines; i++) {
        int x = rect->left;
        if (format & DT_CENTER)
            x = rect->left + (width - lines[i].width) / 2;
        else if (format & DT_RIGHT)
            x = rect->right - lines[i].width;
        draw_tabbed(dc, x, y, buf + lines[i].start, lines[i].length, format, tab_width, rect);
        if (underline >= lines[i].start && underline < lines[i].start + lines[i].length)
            draw_underline(dc, x, y, buf + lines[i].start, underline - lines[i].start, &tm, format, tab_width);
        y += line_height;
    }
    SetTextAlign(dc, old_align);
    if (lines != local)
        UserFree(lines);
    UserFree(buf);
    if (format & DT_VCENTER && format & DT_SINGLELINE)
        return line_height + (rect->bottom - rect->top - line_height) / 2;
    return total;
}

WINUSERAPI int WINAPI DrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format)
{
    return DrawTextExW(dc, (LPWSTR)text, count, rect, format & ~DT_MODIFYSTRING, nullptr);
}

WINUSERAPI int WINAPI DrawTextA(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format)
{
    int n;
    LPWSTR w = UserAtoWn(text, count, &n);
    if (!w)
        return 0;
    int r = DrawTextExW(dc, w, n, rect, format & ~DT_MODIFYSTRING, nullptr);
    UserFree(w);
    return r;
}

WINUSERAPI LONG WINAPI TabbedTextOutW(HDC dc, int x, int y, LPCWSTR text, int count, int tabs, const INT *positions,
                                      int origin)
{
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int tab_width = tabs > 0 && positions ? positions[0] : tm.tmAveCharWidth * 8;
    draw_tabbed(dc, x, y, text, count, DT_EXPANDTABS | DT_NOCLIP, tab_width, nullptr);
    return MAKELONG(text_width(dc, text, count, DT_EXPANDTABS, tab_width), tm.tmHeight);
}

WINUSERAPI DWORD WINAPI GetTabbedTextExtentW(HDC dc, LPCWSTR text, int count, int tabs, const INT *positions)
{
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int tab_width = tabs > 0 && positions ? positions[0] : tm.tmAveCharWidth * 8;
    return MAKELONG(text_width(dc, text, count, DT_EXPANDTABS, tab_width), tm.tmHeight);
}

/* the message font of NONCLIENTMETRICS for a dpi, cached. */
HFONT UserGetMessageFont(UINT dpi)
{
    static HFONT fonts[4];
    static UINT dpis[4];
    for (int i = 0; i < 4; i++)
        if (fonts[i] && dpis[i] == dpi)
            return fonts[i];
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi))
        return GetStockObject(DEFAULT_GUI_FONT);
    HFONT f = CreateFontIndirectW(&ncm.lfMessageFont);
    for (int i = 0; i < 4; i++)
        if (!fonts[i]) {
            fonts[i] = f;
            dpis[i] = dpi;
            return f;
        }
    return f;
}
