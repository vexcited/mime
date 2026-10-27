/*
 * the menu bar of a top level window: its layout under the caption, the drawing in the frame and the questions about
 * it that user32 asks. the menu loop that runs the bar is in user32.
 *
 * the layout is per window, so a menu several windows share is laid out for each. the rows are one pixel less than
 * SM_CYMENU high, an item is as wide as its text plus the width of two average characters, and the bar line below the rows makes
 * the space it takes SM_CYMENU at the common height.
 */
#include "menu.h"

WND *MenuBarCalcWindow;

/* with the visual style, the Menu class of aero.msstyles: a white bar, a blue selection in two opacities over it. */
#define BAR_COLOR RGB(0xFF, 0xFF, 0xFF)
#define BAR_LINE RGB(0xF2, 0xF2, 0xF2)
#define BAR_TEXT RGB(0x00, 0x00, 0x00)
#define BAR_TEXT_GRAYED RGB(0x6D, 0x6D, 0x6D)
#define SELECT_COLOR RGB(0x00, 0x8C, 0xFF)
#define BAR_COLOR_DARK RGB(0x20, 0x20, 0x20)
#define BAR_LINE_DARK RGB(0x2B, 0x2B, 0x2B)
#define BAR_TEXT_DARK RGB(0xFF, 0xFF, 0xFF)
#define BAR_TEXT_GRAYED_DARK RGB(0x8A, 0x8A, 0x8A)

static FONT_INSTANCE *menu_font(UINT dpi)
{
    static FONT_INSTANCE *font;
    static UINT font_dpi;
    if (!font || font_dpi != dpi) {
        LOGFONTW lf = W32MessageFontForDpi(dpi);
        FONT_INSTANCE *f = FontRealize(&lf, dpi);
        if (f) {
            font = f;
            font_dpi = dpi;
        }
    }
    return font;
}

static MENU *bar_menu(WND *w)
{
    if ((w->Style & WS_CHILD) || !w->Id)
        return nullptr;
    return MenuLookup((HMENU)w->Id);
}

void MenuBarFree(WND *w)
{
    MENUBAR *bar = w->MenuBar;
    if (!bar)
        return;
    W32Free(bar->Items);
    W32Free(bar);
    w->MenuBar = nullptr;
}

/* the label of an item up to its tab, without the ampersands. underline is where the mnemonic is, or -1. */
static ULONG item_label(const MENU_ITEM *it, PWSTR out, ULONG cap, LONG *underline)
{
    ULONG n = 0;
    *underline = -1;
    for (ULONG i = 0; it->Text && i < it->TextLength && it->Text[i] != L'\t' && n < cap; i++) {
        if (it->Text[i] == L'&') {
            if (i + 1 < it->TextLength && it->Text[i + 1] == L'&') {
                out[n++] = L'&';
                i++;
            } else if (i + 1 < it->TextLength && it->Text[i + 1] != L'\t' && *underline < 0) {
                *underline = (LONG)n;
            }
            continue;
        }
        out[n++] = it->Text[i];
    }
    return n;
}

/* the size a system bitmap of an item has, which are the glyphs of the MDI buttons and the system menu icon. */
static BOOLEAN system_bitmap(HBITMAP bitmap)
{
    ULONG_PTR v = (ULONG_PTR)bitmap;
    return v <= 11 && v >= 1;
}

static BOOLEAN callback_bitmap(HBITMAP bitmap)
{
    return (ULONG_PTR)bitmap == (ULONG_PTR)-1 || (ULONG_PTR)bitmap == 0xFFFFFFFFu;
}

static void bitmap_size(WND *w, const MENU_ITEM *it, UINT dpi, LONG *cx, LONG *cy)
{
    *cx = *cy = 0;
    if (callback_bitmap(it->Bitmap)) {
        MEASUREITEMSTRUCT mi = {ODT_MENU, 0, it->Id, MetricsGet(SM_CXMENUSIZE, dpi), MetricsGet(SM_CYMENUSIZE, dpi),
                                it->Data};
        MsgSendKernel(w, WM_MEASUREITEM, 0, (LPARAM)&mi, MIME_LPARAM_INOUT, sizeof(mi));
        *cx = (LONG)mi.itemWidth;
        *cy = (LONG)mi.itemHeight;
    } else if (system_bitmap(it->Bitmap)) {
        if ((ULONG_PTR)it->Bitmap == 1) {
            *cx = MetricsGet(SM_CXSMICON, dpi);
            *cy = MetricsGet(SM_CYSMICON, dpi);
        } else {
            *cx = MetricsGet(SM_CXMENUSIZE, dpi);
            *cy = MetricsGet(SM_CYMENUSIZE, dpi);
        }
    } else if (it->Bitmap) {
        SURFACE *s = SurfaceLookup(it->Bitmap);
        if (s) {
            *cx = s->Width;
            *cy = s->Height;
        } else {
            *cx = MetricsGet(SM_CXMENUCHECK, dpi);
            *cy = MetricsGet(SM_CYMENUCHECK, dpi);
        }
    }
}

static BOOLEAN item_owner_drawn(const MENU_ITEM *it)
{
    return (it->Type & MFT_OWNERDRAW) != 0;
}

/* the width and height of an item, before the layout puts it in a row. */
static void item_size(WND *w, const MENU_ITEM *it, UINT dpi, FONT_INSTANCE *fi, LONG char_width, LONG row, LONG *cx,
                      LONG *cy)
{
    *cx = 0;
    *cy = row;
    BOOLEAN pad = TRUE;
    if (item_owner_drawn(it)) {
        MEASUREITEMSTRUCT mi = {ODT_MENU, 0, it->Id, 0, 0, it->Data};
        MsgSendKernel(w, WM_MEASUREITEM, 0, (LPARAM)&mi, MIME_LPARAM_INOUT, sizeof(mi));
        *cx = (LONG)mi.itemWidth;
    } else {
        LONG bw = 0, bh = 0, tw = 0;
        WCHAR label[128];
        LONG underline;
        ULONG n = (it->Type & MFT_SEPARATOR) ? 0 : item_label(it, label, RTL_NUMBER_OF(label), &underline);
        if (it->Bitmap)
            bitmap_size(w, it, dpi, &bw, &bh);
        if (n && fi)
            tw = FontTextWidth(fi, label, n, 0);
        *cx = bw + (bw && tw ? 2 : 0) + tw;
        if (bh > *cy)
            *cy = bh;
        pad = !bw || tw;
    }
    if (pad)
        *cx += 2 * char_width;
}

static void layout(WND *w, MENU *m, LONG x, LONG y, LONG width)
{
    UINT dpi = WndGetDpi(w);
    MENUBAR *bar = w->MenuBar;
    if (!bar) {
        bar = W32Alloc(sizeof(MENUBAR));
        if (!bar)
            return;
        w->MenuBar = bar;
    }
    if (bar->Count != m->Count) {
        W32Free(bar->Items);
        bar->Items = W32Alloc((SIZE_T)(m->Count ? m->Count : 1) * sizeof(RECT));
        bar->Count = bar->Items ? m->Count : 0;
        if (!bar->Items) {
            bar->Rows = 0;
            return;
        }
    }
    FONT_INSTANCE *fi = menu_font(dpi);
    LONG char_width = fi ? fi->Metrics.tmAveCharWidth : 6;
    LONG row = MetricsGet(SM_CYMENU, dpi) - 1;
    LONG cx = 0, cy = 0, row_height = 0, first_justified = -1;
    for (ULONG i = 0; i < m->Count && i < bar->Count; i++) {
        const MENU_ITEM *it = &m->Items[i];
        LONG iw, ih;
        item_size(w, it, dpi, fi, char_width, row, &iw, &ih);
        BOOLEAN breaks = i && ((it->Type & (MFT_MENUBREAK | MFT_MENUBARBREAK)) || cx + iw + char_width > width);
        if (breaks) {
            cy += row_height;
            cx = 0;
            row_height = 0;
        }
        bar->Items[i] = (RECT){x + cx, y + cy, x + cx + iw, y + cy + ih};
        cx += iw;
        if (ih > row_height)
            row_height = ih;
        if (first_justified < 0 && (it->Type & MFT_RIGHTJUSTIFY))
            first_justified = (LONG)i;
    }
    if (first_justified >= 0) {
        /* the items from the first right justified one on are packed against the right edge, but never moved left. */
        LONG edge = x + width;
        for (LONG i = (LONG)bar->Count - 1; i >= first_justified; i--) {
            LONG iw = bar->Items[i].right - bar->Items[i].left;
            edge -= iw;
            if (bar->Items[i].left < edge) {
                bar->Items[i].left = edge;
                bar->Items[i].right = edge + iw;
            }
        }
    }
    bar->Menu = (HMENU)w->Id;
    bar->Serial = m->Serial;
    bar->Dpi = dpi;
    bar->X = x;
    bar->Y = y;
    bar->Width = width;
    bar->Rows = cy + row_height;
}

/* the space a bar takes from the top of the client area, its line included, for a window of the given inner area. */
LONG MenuBarCompute(WND *w, LONG x, LONG y, LONG width)
{
    MENU *m = bar_menu(w);
    if (!m || !m->Count || (w->Style & WS_MINIMIZE)) {
        MenuBarFree(w);
        return 0;
    }
    UserReference(&m->Header);
    layout(w, m, x, y, width);
    UserDereference(&m->Header);
    return w->MenuBar && w->MenuBar->Rows ? w->MenuBar->Rows + 1 : 0;
}

/* an edit made since the layout moves the items to where they would be laid out now. */
static MENUBAR *current_bar(WND *w, MENU **menu)
{
    MENU *m = bar_menu(w);
    MENUBAR *bar = w->MenuBar;
    if (!m || !bar || !w->MenuBarHeight || bar->Menu != (HMENU)w->Id)
        return nullptr;
    if (bar->Serial != m->Serial || bar->Dpi != WndGetDpi(w)) {
        LONG rows = bar->Rows;
        layout(w, m, bar->X, bar->Y, bar->Width);
        bar = w->MenuBar;
        if (bar)
            bar->Rows = rows ? rows : bar->Rows;
    }
    if (menu)
        *menu = m;
    return bar;
}

BOOLEAN MenuBarRect(WND *w, LONG item, RECT *screen)
{
    MENU *m;
    MENUBAR *bar = current_bar(w, &m);
    *screen = (RECT){0, 0, 0, 0};
    if (!bar)
        return FALSE;
    if (item == 0) {
        *screen = (RECT){bar->X, bar->Y, bar->X + bar->Width, bar->Y + bar->Rows};
    } else if ((ULONG)item <= bar->Count) {
        *screen = bar->Items[item - 1];
    } else {
        return FALSE;
    }
    RectOffset(screen, w->WindowRect.left, w->WindowRect.top);
    return TRUE;
}

/* the item at a screen point, or -1 for the bar's own area. only called for points of the bar. */
LONG MenuBarHit(WND *w, POINT pt)
{
    MENU *m;
    MENUBAR *bar = current_bar(w, &m);
    if (!bar)
        return -1;
    POINT local = {pt.x - w->WindowRect.left, pt.y - w->WindowRect.top};
    for (ULONG i = 0; i < bar->Count; i++)
        if (RectContains(&bar->Items[i], local.x, local.y))
            return (LONG)i;
    return -1;
}

/* the area of the bar and its line, in the window's coordinates, for the hit test. */
BOOLEAN MenuBarArea(WND *w, RECT *screen)
{
    MENUBAR *bar = w->MenuBar;
    if (!bar || !w->MenuBarHeight)
        return FALSE;
    *screen = (RECT){bar->X, bar->Y, bar->X + bar->Width, bar->Y + w->MenuBarHeight};
    RectOffset(screen, w->WindowRect.left, w->WindowRect.top);
    return TRUE;
}

/* ------------------------------------------------------------------ drawing */

static void blend_pixel(SURFACE *s, const REGION *clip, const RECT *r, COLORREF color, ULONG alpha)
{
    /* a translucent fill, row by row, in the pixels of the surface. */
    if (alpha >= 255) {
        DrawFillRect(s, clip, r, ColorToPixel(color));
        return;
    }
    ULONG src_r = color & 0xFF, src_g = (color >> 8) & 0xFF, src_b = (color >> 16) & 0xFF;
    for (ULONG i = 0; i < clip->Count; i++) {
        RECT part;
        if (!RectIntersect(&part, &clip->Rects[i], r))
            continue;
        for (LONG y = part.top; y < part.bottom; y++) {
            if (y < 0 || y >= s->Height)
                continue;
            PULONG row = (PULONG)(s->Bits + (LONG_PTR)y * s->Stride);
            for (LONG x = part.left; x < part.right && x < s->Width; x++) {
                if (x < 0)
                    continue;
                ULONG d = row[x];
                ULONG dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
                dr = (src_r * alpha + dr * (255 - alpha) + 127) / 255;
                dg = (src_g * alpha + dg * (255 - alpha) + 127) / 255;
                db = (src_b * alpha + db * (255 - alpha) + 127) / 255;
                row[x] = (d & 0xFF000000u) | (dr << 16) | (dg << 8) | db;
            }
        }
    }
}

/* the glyphs of the MDI buttons and the popup system bitmaps, strokes of a pixel scaled to the dpi. */
static void draw_system_glyph(WND *w, SURFACE *s, const REGION *clip, ULONG_PTR kind, const RECT *r, COLORREF color,
                              UINT dpi, POINT origin)
{
    LONG size = W32MulDiv(8, (LONG)dpi, 96);
    LONG th = W32Max(1, W32MulDiv(1, (LONG)dpi, 96));
    LONG cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2;
    LONG l = cx - size / 2, t = cy - size / 2;
    ULONG pixel = ColorToPixel(color);
    switch (kind) {
    case 1: {
        /* the icon of the window, in the size of a small icon. */
        LONG icon_size = MetricsGet(SM_CXSMICON, dpi);
        HICON icon = w->IconSmall ? w->IconSmall : w->Icon;
        if (!icon)
            return;
        HDC hdc = PaintGetDC(w, nullptr, DCX_WINDOW);
        DC *dc = DcLookup(hdc);
        if (dc) {
            RgnCopy(&dc->VisRgn, clip);
            CursorDrawIcon(dc, (r->left + r->right - icon_size) / 2 - origin.x, (r->top + r->bottom - icon_size) / 2 - origin.y,
                           icon, icon_size, icon_size, DI_NORMAL);
            PaintReleaseDC(dc);
        }
        return;
    }
    case 3:
    case 7:
    case 11: {
        RECT line = {l, cy + size / 4, l + size, cy + size / 4 + th};
        DrawFillRect(s, clip, &line, pixel);
        return;
    }
    case 2:
    case 9: {
        LONG back = W32MulDiv(2, (LONG)dpi, 96);
        RECT front[4] = {{l, t + back, l + size - back, t + back + th},
                         {l, t + size - th, l + size - back, t + size},
                         {l, t + back, l + th, t + size},
                         {l + size - back - th, t + back, l + size - back, t + size}};
        RECT rear[3] = {{l + back, t, l + size, t + th}, {l + size - th, t, l + size, t + size - back},
                        {l + size - back - th, t + size - back - th, l + size, t + size - back}};
        for (int i = 0; i < 4; i++)
            DrawFillRect(s, clip, &front[i], pixel);
        for (int i = 0; i < 3; i++)
            DrawFillRect(s, clip, &rear[i], pixel);
        return;
    }
    case 10: {
        RECT edges[4] = {{l, t, l + size, t + th}, {l, t + size - th, l + size, t + size},
                         {l, t, l + th, t + size}, {l + size - th, t, l + size, t + size}};
        for (int i = 0; i < 4; i++)
            DrawFillRect(s, clip, &edges[i], pixel);
        return;
    }
    case 5:
    case 6:
    case 8:
        for (LONG i = 0; i < size; i++) {
            RECT a = {l + i, t + i, l + i + th, t + i + th};
            RECT b = {l + size - 1 - i, t + i, l + size - 1 - i + th, t + i + th};
            DrawFillRect(s, clip, &a, pixel);
            DrawFillRect(s, clip, &b, pixel);
        }
        return;
    }
}

static void draw_bitmap(WND *w, SURFACE *s, const REGION *clip, const MENU_ITEM *it, const RECT *r, COLORREF color,
                        UINT dpi, POINT origin)
{
    if (system_bitmap(it->Bitmap)) {
        draw_system_glyph(w, s, clip, (ULONG_PTR)it->Bitmap, r, color, dpi, origin);
        return;
    }
    SURFACE *src = SurfaceLookup(it->Bitmap);
    if (!src)
        return;
    LONG x = r->left, y = r->top + (r->bottom - r->top - src->Height) / 2;
    BOOLEAN alpha = FALSE;
    if (src->Bpp == 32)
        for (LONG row = 0; row < src->Height && !alpha; row++) {
            PULONG p = (PULONG)(src->Bits + (LONG_PTR)row * src->Stride);
            for (LONG col = 0; col < src->Width; col++)
                if (p[col] >> 24) {
                    alpha = TRUE;
                    break;
                }
        }
    if (alpha) {
        RECT dst = {x, y, x + src->Width, y + src->Height}, from = {0, 0, src->Width, src->Height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlendSurface(s, clip, &dst, src, &from, blend);
    } else {
        BltSurface(s, clip, x, y, src->Width, src->Height, src, 0, 0, SRCCOPY, nullptr);
    }
}

/* an owner drawn item is drawn by the window, in a device context of the window. */
static void draw_owner(WND *w, SURFACE *s, const REGION *clip, const MENU_ITEM *it, const MENU *m, const RECT *item,
                       UINT state, POINT origin, ULONG action)
{
    (void)s;
    HDC hdc = PaintGetDC(w, nullptr, DCX_WINDOW);
    DC *dc = DcLookup(hdc);
    if (!dc)
        return;
    REGION item_clip;
    RgnInit(&item_clip);
    RgnCombineRect(&item_clip, clip, item, RGN_AND);
    RgnCopy(&dc->VisRgn, &item_clip);
    RgnFree(&item_clip);
    DRAWITEMSTRUCT di;
    memset(&di, 0, sizeof(di));
    di.CtlType = ODT_MENU;
    di.itemID = it->Id;
    di.itemAction = action;
    di.itemState = state;
    di.hwndItem = (HWND)m->Header.Handle;
    di.hDC = hdc;
    di.rcItem = *item;
    RectOffset(&di.rcItem, -origin.x, -origin.y);
    di.itemData = it->Data;
    UserReference(&w->Header);
    MsgSendKernel(w, WM_DRAWITEM, 0, (LPARAM)&di, MIME_LPARAM_IN, sizeof(di));
    UserDereference(&w->Header);
    if ((dc = DcLookup(hdc)) != nullptr)
        PaintReleaseDC(dc);
}

void MenuBarPaint(WND *w, SURFACE *s, const REGION *clip, POINT origin)
{
    MENU *m;
    MENUBAR *bar = current_bar(w, &m);
    if (!bar || !bar->Count)
        return;
    UserReference(&m->Header);
    UINT dpi = bar->Dpi;
    BOOLEAN dark = FrameIsDark(w), themed = FrameThemed();
    COLORREF back, line, text_color, gray_color;
    if (dark) {
        back = BAR_COLOR_DARK, line = BAR_LINE_DARK, text_color = BAR_TEXT_DARK, gray_color = BAR_TEXT_GRAYED_DARK;
    } else if (themed) {
        back = BAR_COLOR, line = BAR_LINE, text_color = BAR_TEXT, gray_color = BAR_TEXT_GRAYED;
    } else {
        /* flat menus: the bar color, a line in the face color of a window with edges and in the frame color without. */
        back = MetricsSysColor(COLOR_MENUBAR);
        line = MetricsSysColor((w->ExStyle & (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)) ? COLOR_3DFACE : COLOR_WINDOWFRAME);
        text_color = MetricsSysColor(COLOR_MENUTEXT);
        gray_color = MetricsSysColor(COLOR_GRAYTEXT);
    }
    BRUSH *custom = m->Background ? (BRUSH *)GdiLookup(m->Background, GDI_TYPE_BRUSH) : nullptr;
    if (custom && custom->Style == BS_SOLID)
        back = custom->Color;
    RECT area = {bar->X + origin.x, bar->Y + origin.y, bar->X + bar->Width + origin.x,
                 bar->Y + w->MenuBarHeight + origin.y};
    RECT body = area, line_rect = area;
    body.bottom = line_rect.top = area.bottom - 1;
    DrawFillRect(s, clip, &body, ColorToPixel(back));
    DrawFillRect(s, clip, &line_rect, ColorToPixel(line));

    FONT_INSTANCE *fi = menu_font(dpi);
    BOOLEAN cues = !(w->UiState & UISF_HIDEACCEL) || w->MenuCues;
    for (ULONG i = 0; i < bar->Count && i < m->Count; i++) {
        const MENU_ITEM *it = &m->Items[i];
        RECT r = bar->Items[i];
        RectOffset(&r, origin.x, origin.y);
        BOOLEAN selected = w->MenuHot == (LONG)i + 1 || w->MenuPushed == (LONG)i + 1 || (it->State & MFS_HILITE) != 0;
        BOOLEAN pushed = w->MenuPushed == (LONG)i + 1;
        BOOLEAN grayed = (it->State & (MFS_GRAYED | MFS_DISABLED)) != 0;
        UINT ods = (selected ? ODS_SELECTED : 0) | (grayed ? ODS_GRAYED | ODS_DISABLED : 0) |
                   ((it->State & MFS_CHECKED) ? ODS_CHECKED : 0) | ((it->State & MFS_DEFAULT) ? ODS_DEFAULT : 0) |
                   (cues ? 0 : ODS_NOACCEL) | (selected ? ODS_HOTLIGHT : 0) | ODS_NOFOCUSRECT;
        if (!(w->Flags & WNDF_ACTIVE_FRAME))
            ods |= 0x80; /* ODS_INACTIVE */
        if (item_owner_drawn(it)) {
            draw_owner(w, s, clip, it, m, &r, ods, origin, selected ? ODA_SELECT : ODA_DRAWENTIRE);
            continue;
        }
        COLORREF text = grayed ? gray_color : text_color;
        if (selected && themed) {
            /* the selection is a thin border and a lighter inside, the pushed one is darker. */
            COLORREF accent = dark ? RGB(0xFF, 0xFF, 0xFF) : SELECT_COLOR;
            blend_pixel(s, clip, &r, accent, pushed ? 102 : 51);
            RECT inner = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
            if (!RectEmpty(&inner)) {
                DrawFillRect(s, clip, &inner, ColorToPixel(back));
                blend_pixel(s, clip, &inner, accent, pushed ? 51 : 26);
            }
        } else if (selected) {
            DrawFillRect(s, clip, &r, ColorToPixel(MetricsSysColor(COLOR_MENUHILIGHT)));
            if (!grayed)
                text = MetricsSysColor(COLOR_HIGHLIGHTTEXT);
        }
        LONG x = r.left + (fi ? fi->Metrics.tmAveCharWidth : 6);
        if (it->Bitmap) {
            LONG bw, bh;
            bitmap_size(w, it, dpi, &bw, &bh);
            RECT br = {x, r.top, x + bw, r.bottom};
            if (callback_bitmap(it->Bitmap)) {
                draw_owner(w, s, clip, it, m, &br, ods, origin, ODA_DRAWENTIRE);
                /* the window may have changed the menu in its answers. */
                if (m->Header.Destroyed || i >= m->Count)
                    break;
                it = &m->Items[i];
            } else {
                if (!(it->Type & MFT_SEPARATOR) && !it->TextLength)
                    br = (RECT){r.left, r.top, r.right, r.bottom}; /* a bitmap alone fills the item. */
                draw_bitmap(w, s, clip, it, &br, text, dpi, origin);
            }
            x = br.right + 2;
            if (!it->TextLength)
                continue;
        }
        if (!fi || (it->Type & MFT_SEPARATOR))
            continue;
        WCHAR label[128];
        LONG underline;
        ULONG n = item_label(it, label, RTL_NUMBER_OF(label), &underline);
        if (!n)
            continue;
        LONG height = fi->Metrics.tmAscent + fi->Metrics.tmDescent;
        LONG baseline = r.top + (r.bottom - r.top - height) / 2 + fi->Metrics.tmAscent;
        FrameDrawText(s, clip, fi, x, baseline, label, n, r.right, ColorToPixel(text));
        if (cues && underline >= 0 && (ULONG)underline < n) {
            LONG left = x + FontTextWidth(fi, label, (ULONG)underline, 0);
            const GLYPH *g = FontGetGlyph(fi, label[underline]);
            LONG advance = g ? g->Advance : fi->Metrics.tmAveCharWidth;
            LONG thickness = W32Max(1, fi->UnderlineSize);
            RECT u = {left, baseline + fi->UnderlinePos, left + advance, baseline + fi->UnderlinePos + thickness};
            DrawFillRect(s, clip, &u, ColorToPixel(text));
        }
    }
    UserDereference(&m->Header);
}

/* the selection of the icon of the system menu, under the icon. */
void MenuBarPaintIcon(WND *w, SURFACE *s, const REGION *clip, const RECT *area)
{
    if (!w->MenuIcon)
        return;
    if (FrameThemed() || FrameIsDark(w)) {
        COLORREF accent = FrameIsDark(w) ? RGB(0xFF, 0xFF, 0xFF) : SELECT_COLOR;
        blend_pixel(s, clip, area, accent, 51);
    } else {
        DrawFillRect(s, clip, area, ColorToPixel(MetricsSysColor(COLOR_MENUHILIGHT)));
    }
}

/* repaint after the state of the loop changed. */
void MenuBarRedraw(WND *w)
{
    if (WndIsVisible(w) && !(w->Style & WS_MINIMIZE))
        FramePaint(w);
}

/* ------------------------------------------------------------------ what user32 asks */

ULONG_PTR MenuBarCall(WND *w, PVOID user_request)
{
    MIME_MENUBAR req;
    if (!NT_SUCCESS(MimeCopyFromUser(&req, user_request, sizeof(req))))
        return 0;
    ULONG_PTR result = 0;
    MENU *m = nullptr;
    MENUBAR *bar = current_bar(w, &m);
    req.Menu = bar && m ? (ULONG)(ULONG_PTR)m->Header.Handle : 0;
    req.Count = bar ? bar->Count : 0;
    switch (req.Op) {
    case MIME_MENUBAR_RECT: {
        RECT r;
        if (MenuBarRect(w, req.Item, &r)) {
            req.Rect = r;
            result = 1;
        }
        break;
    }
    case MIME_MENUBAR_HIT: {
        RECT area;
        req.Item = -1;
        if (MenuBarArea(w, &area) && RectContains(&area, req.Point.x, req.Point.y)) {
            req.Item = MenuBarHit(w, req.Point);
            result = 1;
        }
        break;
    }
    case MIME_MENUBAR_STATE: {
        /* item -2 is the icon of the system menu, which the frame draws as selected. */
        BOOLEAN icon = req.Item == -2;
        if (icon)
            req.Item = -1;
        if (icon != w->MenuIcon) {
            w->MenuIcon = icon;
            MenuBarRedraw(w);
        }
        LONG hot = req.Item + 1, pushed = req.Pushed + 1;
        if (hot < 0)
            hot = 0;
        if (pushed < 0)
            pushed = 0;
        if (hot != w->MenuHot || pushed != w->MenuPushed) {
            w->MenuHot = hot;
            w->MenuPushed = pushed;
            MenuBarRedraw(w);
        }
        result = 1;
        break;
    }
    case MIME_MENUBAR_CUES:
        if ((req.Item != 0) != w->MenuCues) {
            w->MenuCues = req.Item != 0;
            if (w->MenuBarHeight)
                MenuBarRedraw(w);
        }
        result = 1;
        break;
    case MIME_MENUBAR_AREA: {
        RECT area;
        if (MenuBarArea(w, &area)) {
            req.Rect = area;
            result = 1;
        }
        break;
    }
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_request, &req, sizeof(req))))
        return 0;
    return result;
}
