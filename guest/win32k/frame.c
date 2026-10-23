/*
 * the window frame, drawn by win32k in the role of the desktop window manager: a windows 11 caption using the
 * caption button images of the DWMWindow atlas in aero.msstyles. the host compositor adds the 1 pixel border and
 * the rounded corners.
 *
 * resizable windows keep the invisible resize borders of windows 10 and 11: the window rectangle extends 7 pixels
 * at 96 dpi beyond the visible frame on the left, right and bottom.
 */
#include "win32k.h"
#include <msstyles/msstyles.h>
#include <png/png.h>

#define FRAME_TAG W32_TAG('W', 'f', 'r', 'm')

static LONG scale(LONG v, UINT dpi)
{
    return W32MulDiv(v, (LONG)dpi, 96);
}

/* ------------------------------------------------------------------ metrics */

BOOLEAN FrameHasCaption(DWORD style, DWORD ex_style)
{
    (void)ex_style;
    return (style & WS_CAPTION) == WS_CAPTION;
}

static BOOLEAN is_top_frame(DWORD style)
{
    return !(style & WS_CHILD);
}

void FrameGetMetrics(DWORD style, DWORD ex_style, UINT dpi, FRAME_METRICS *m)
{
    memset(m, 0, sizeof(*m));
    BOOLEAN caption = FrameHasCaption(style, ex_style);
    BOOLEAN thick = (style & WS_THICKFRAME) != 0;
    BOOLEAN dlg = (style & WS_DLGFRAME) || (ex_style & WS_EX_DLGMODALFRAME);
    if (is_top_frame(style) && (caption || thick || dlg || (style & WS_BORDER))) {
        /* any caption window gets the sizing border and the padded border, sizable or not. */
        if (caption)
            m->Border = MetricsGet(SM_CXFRAME, dpi) + MetricsGet(SM_CXPADDEDBORDER, dpi);
        else if (thick)
            m->Border = MetricsGet(SM_CXFRAME, dpi);
        else if (dlg)
            m->Border = MetricsGet(SM_CXDLGFRAME, dpi);
        else
            m->Border = 1;
        m->VisibleInset = m->Border > 1 ? m->Border - 1 : 0;
        m->Caption = caption ? MetricsGet((ex_style & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION, dpi) : 0;
        m->Top = m->Border + m->Caption;
        m->ButtonWidth = scale(46, dpi);
        m->CornerRadius = scale(8, dpi);
        return;
    }
    /* child windows keep the classic edges. */
    LONG b = 0;
    if (ex_style & WS_EX_CLIENTEDGE)
        b += 2;
    if (ex_style & WS_EX_STATICEDGE)
        b += 1;
    if (thick)
        b += MetricsGet(SM_CXFRAME, dpi);
    else if (dlg)
        b += MetricsGet(SM_CXDLGFRAME, dpi);
    else if (style & WS_BORDER)
        b += 1;
    m->Border = b;
    m->Caption = caption ? MetricsGet(SM_CYCAPTION, dpi) : 0;
    m->Top = b + m->Caption;
    m->ButtonWidth = scale(46, dpi);
}

void FrameAdjustRect(RECT *r, DWORD style, DWORD ex_style, UINT dpi)
{
    FRAME_METRICS m;
    FrameGetMetrics(style, ex_style, dpi, &m);
    r->left -= m.Border;
    r->right += m.Border;
    r->top -= m.Top;
    r->bottom += m.Border;
}

void FrameClientFromWindow(RECT *r, DWORD style, DWORD ex_style, UINT dpi)
{
    FRAME_METRICS m;
    FrameGetMetrics(style, ex_style, dpi, &m);
    r->left += m.Border;
    r->right -= m.Border;
    r->top += m.Top;
    r->bottom -= m.Border;
    if (style & WS_VSCROLL)
        r->right -= MetricsGet(SM_CXVSCROLL, dpi);
    if (style & WS_HSCROLL)
        r->bottom -= MetricsGet(SM_CYHSCROLL, dpi);
    if (r->right < r->left)
        r->right = r->left;
    if (r->bottom < r->top)
        r->bottom = r->top;
}

void FrameGetVisibleRect(WND *w, RECT *r)
{
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    *r = (RECT){0, 0, width, height};
    if (!is_top_frame(w->Style))
        return;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndGetDpi(w), &m);
    if (w->Style & WS_MAXIMIZE) {
        /* the frame of a maximized window lies outside the work area. */
        *r = (RECT){m.Border, m.Border, width - m.Border, height - m.Border};
        return;
    }
    r->left = m.VisibleInset;
    r->right = width - m.VisibleInset;
    r->bottom = height - m.VisibleInset;
}

static BOOLEAN has_minmax(WND *w)
{
    return (w->Style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) && !(w->ExStyle & WS_EX_TOOLWINDOW);
}

static BOOLEAN has_help(WND *w)
{
    return (w->ExStyle & WS_EX_CONTEXTHELP) && !has_minmax(w);
}

/* window relative, right to left. */
void FrameGetButtonRect(WND *w, int button, RECT *r)
{
    *r = (RECT){0, 0, 0, 0};
    if (!FrameHasCaption(w->Style, w->ExStyle) || !(w->Style & WS_SYSMENU))
        return;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndGetDpi(w), &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG top = vis.top;
    LONG bottom = (w->Style & WS_MAXIMIZE) ? m.Top : m.Top;
    LONG right = vis.right;
    int slot;
    switch (button) {
    case CAPTION_CLOSE:
        slot = 0;
        break;
    case CAPTION_MAX:
        if (!has_minmax(w))
            return;
        slot = 1;
        break;
    case CAPTION_MIN:
        if (!has_minmax(w))
            return;
        slot = 2;
        break;
    case CAPTION_HELP:
        if (!has_help(w))
            return;
        slot = 1;
        break;
    default:
        return;
    }
    LONG bw = m.ButtonWidth;
    *r = (RECT){right - bw * (slot + 1), top, right - bw * slot, bottom};
}

static int button_at(WND *w, POINT wp)
{
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (!RectEmpty(&r) && RectContains(&r, wp.x, wp.y))
            return buttons[i];
    }
    return CAPTION_NONE;
}

static LONG caption_buttons_left(WND *w)
{
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG left = vis.right;
    static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
    for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++) {
        RECT r;
        FrameGetButtonRect(w, buttons[i], &r);
        if (!RectEmpty(&r) && r.left < left)
            left = r.left;
    }
    return left;
}

ULONG FrameGetDragRects(WND *w, RECT *rects, ULONG max)
{
    if (!max || !FrameHasCaption(w->Style, w->ExStyle) || !is_top_frame(w->Style))
        return 0;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, WndGetDpi(w), &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    LONG top = vis.top;
    if ((w->Style & WS_THICKFRAME) && !(w->Style & WS_MAXIMIZE))
        top += scale(4, WndGetDpi(w)); /* leave the top resize strip to the window. */
    rects[0] = (RECT){vis.left, top, caption_buttons_left(w), m.Top};
    return RectEmpty(&rects[0]) ? 0 : 1;
}

/* ------------------------------------------------------------------ hit testing */

LRESULT FrameHitTest(WND *w, POINT pt)
{
    if (!RectContains(&w->WindowRect, pt.x, pt.y))
        return HTNOWHERE;
    if (RectContains(&w->ClientRect, pt.x, pt.y))
        return HTCLIENT;
    if (w->Style & WS_MINIMIZE)
        return HTCAPTION;
    UINT dpi = WndGetDpi(w);
    POINT wp = {pt.x - w->WindowRect.left, pt.y - w->WindowRect.top};
    LONG width = w->WindowRect.right - w->WindowRect.left;
    LONG height = w->WindowRect.bottom - w->WindowRect.top;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);

    /* scroll bars sit between the client area and the frame. */
    RECT inner = w->WindowRect;
    FrameClientFromWindow(&inner, w->Style & ~(WS_VSCROLL | WS_HSCROLL), w->ExStyle, dpi);
    if (RectContains(&inner, pt.x, pt.y)) {
        if ((w->Style & WS_VSCROLL) && pt.x >= w->ClientRect.right)
            return (w->Style & WS_HSCROLL) && pt.y >= w->ClientRect.bottom ? HTSIZE : HTVSCROLL;
        if ((w->Style & WS_HSCROLL) && pt.y >= w->ClientRect.bottom)
            return HTHSCROLL;
    }

    if ((w->Style & WS_THICKFRAME) && !(w->Style & WS_MAXIMIZE)) {
        LONG edge = m.Border;
        LONG corner = scale(16, dpi);
        LONG top_edge = scale(4, dpi);
        BOOLEAN left = wp.x < edge, right = wp.x >= width - edge;
        BOOLEAN top = wp.y < top_edge, bottom = wp.y >= height - edge;
        if (top || bottom || left || right) {
            BOOLEAN near_left = wp.x < corner, near_right = wp.x >= width - corner;
            BOOLEAN near_top = wp.y < corner, near_bottom = wp.y >= height - corner;
            if ((top && near_left) || (left && near_top))
                return HTTOPLEFT;
            if ((top && near_right) || (right && near_top))
                return HTTOPRIGHT;
            if ((bottom && near_left) || (left && near_bottom))
                return HTBOTTOMLEFT;
            if ((bottom && near_right) || (right && near_bottom))
                return HTBOTTOMRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
            return left ? HTLEFT : HTRIGHT;
        }
    }

    if (FrameHasCaption(w->Style, w->ExStyle) && wp.y < m.Top) {
        RECT vis;
        FrameGetVisibleRect(w, &vis);
        switch (button_at(w, wp)) {
        case CAPTION_CLOSE:
            return HTCLOSE;
        case CAPTION_MAX:
            return HTMAXBUTTON;
        case CAPTION_MIN:
            return HTMINBUTTON;
        case CAPTION_HELP:
            return HTHELP;
        }
        if ((w->Style & WS_SYSMENU) && w->IconSmall && !(w->ExStyle & (WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW)) &&
            wp.x < vis.left + scale(32, dpi) && wp.y >= vis.top)
            return HTSYSMENU;
        if (wp.y >= vis.top && wp.x >= vis.left && wp.x < vis.right)
            return HTCAPTION;
    }
    return HTBORDER;
}

/* ------------------------------------------------------------------ the theme atlas */

static void *frame_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, FRAME_TAG);
}

static void frame_free(void *ctx, void *p)
{
    (void)ctx;
    if (p)
        ExFreePool(p);
}

static const MimeAllocator g_frame_alloc = {frame_alloc, frame_free, nullptr};

static struct {
    BOOLEAN Tried;
    BOOLEAN Ready;
    PUCHAR File;
    PUCHAR Mui;
    MimeMsstyles Theme;
    int Class;
    SURFACE *Atlas;
} g_theme;

static PUCHAR read_whole_file(PCWSTR path, SIZE_T *size_out)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, nullptr, 0, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0)))
        return nullptr;
    FILE_STANDARD_INFORMATION info;
    PUCHAR data = nullptr;
    if (NT_SUCCESS(ZwQueryInformationFile(h, &iosb, &info, sizeof(info), FileStandardInformation)) &&
        info.EndOfFile.QuadPart > 0 && info.EndOfFile.QuadPart < 32 * 1024 * 1024) {
        SIZE_T size = (SIZE_T)info.EndOfFile.QuadPart;
        data = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FRAME_TAG);
        LARGE_INTEGER off = {};
        if (data && (!NT_SUCCESS(ZwReadFile(h, nullptr, nullptr, nullptr, &iosb, data, (ULONG)size, &off, nullptr)) ||
                     iosb.Information != size)) {
            ExFreePool(data);
            data = nullptr;
        }
        if (data)
            *size_out = size;
    }
    ZwClose(h);
    return data;
}

static BOOLEAN load_theme(void)
{
    if (g_theme.Tried)
        return g_theme.Ready;
    g_theme.Tried = TRUE;
    SIZE_T size = 0, mui_size = 0;
    g_theme.File = read_whole_file(L"\\SystemRoot\\Resources\\Themes\\aero\\aero.msstyles", &size);
    if (!g_theme.File) {
        W32Trace("win32k: aero.msstyles not found, using the built in frame\n");
        return FALSE;
    }
    g_theme.Mui = read_whole_file(L"\\SystemRoot\\Resources\\Themes\\aero\\en-US\\aero.msstyles.mui", &mui_size);
    if (!mime_msstyles_open(&g_theme.Theme, g_theme.File, size, g_theme.Mui, g_theme.Mui ? mui_size : 0,
                            &g_frame_alloc)) {
        W32Trace("win32k: cannot parse aero.msstyles\n");
        return FALSE;
    }
    g_theme.Class = mime_msstyles_find_class(&g_theme.Theme, nullptr, "DWMWindow");
    const uint8_t *png = nullptr;
    uint32_t png_size = 0;
    if (g_theme.Class < 0 ||
        !mime_msstyles_get_stream(&g_theme.Theme, g_theme.Class, 0, 0, MIME_TMT_DISKSTREAM, &png, &png_size)) {
        W32Trace("win32k: no DWMWindow atlas in the theme\n");
        return FALSE;
    }
    MimePngInfo info;
    if (!mime_png_info(png, png_size, &info))
        return FALSE;
    SURFACE *atlas = SurfaceCreate((LONG)info.width, (LONG)info.height, 32, 0);
    if (!atlas)
        return FALSE;
    if (mime_png_decode(png, png_size, &g_frame_alloc, MIME_PNG_PREMULTIPLIED, atlas->Bits, (size_t)atlas->Stride) !=
        MIME_PNG_OK) {
        SurfaceDestroy(atlas);
        return FALSE;
    }
    g_theme.Atlas = atlas;
    g_theme.Ready = TRUE;
    return TRUE;
}

/* the part rectangle is split vertically into IMAGECOUNT frames, each inset by 1 pixel. */
static BOOLEAN atlas_frame(int part, int frame, RECT *out)
{
    MimeMsstylesRect r;
    if (!mime_msstyles_get_rect(&g_theme.Theme, g_theme.Class, part, 0, MIME_TMT_ATLASRECT, &r))
        return FALSE;
    int32_t count = 1;
    mime_msstyles_get_int(&g_theme.Theme, g_theme.Class, part, 0, MIME_TMT_IMAGECOUNT, &count);
    if (count < 1)
        count = 1;
    if (frame >= count)
        frame = 0;
    LONG h = (r.bottom - r.top) / count;
    *out = (RECT){r.left + 1, r.top + h * frame + 1, r.right - 1, r.top + h * (frame + 1) - 1};
    return !RectEmpty(out) && out->right <= g_theme.Atlas->Width && out->bottom <= g_theme.Atlas->Height;
}

static int dpi_slot(UINT dpi)
{
    return dpi <= 96 ? 0 : dpi <= 120 ? 1 : dpi <= 144 ? 2 : 3;
}

/* ------------------------------------------------------------------ painting */

#define CAPTION_COLOR RGB(0xFF, 0xFF, 0xFF)
#define TEXT_ACTIVE RGB(0x00, 0x00, 0x00)
#define TEXT_INACTIVE RGB(0x99, 0x99, 0x99)
#define CLOSE_HOT RGB(0xC4, 0x2B, 0x1C)
#define CLOSE_PRESSED RGB(0xC8, 0x40, 0x31)
#define BUTTON_HOT RGB(0xE9, 0xE9, 0xE9)
#define BUTTON_PRESSED RGB(0xED, 0xED, 0xED)

static FONT_INSTANCE *caption_font(UINT dpi)
{
    static FONT_INSTANCE *font;
    static UINT font_dpi;
    if (!font || font_dpi != dpi) {
        LOGFONTW lf = W32CaptionFontForDpi(dpi);
        FONT_INSTANCE *f = FontRealize(&lf, dpi);
        if (f) {
            font = f;
            font_dpi = dpi;
        }
    }
    return font;
}

static void draw_text(SURFACE *s, const REGION *clip, FONT_INSTANCE *fi, LONG x, LONG baseline, PCWSTR text,
                      ULONG count, LONG max_right, ULONG pixel)
{
    for (ULONG i = 0; i < count; i++) {
        const GLYPH *g = FontGetGlyph(fi, text[i]);
        if (!g)
            continue;
        if (x + g->Advance > max_right)
            break;
        if (g->Coverage && g->Width && g->Height)
            DrawCoverage(s, clip, x + g->Left, baseline + g->Top, g->Coverage, g->Width, g->Height, g->Width, pixel);
        x += g->Advance;
    }
}

/* like the windows caption, a title that does not fit ends with "...". */
static ULONG fit_title(FONT_INSTANCE *fi, PCWSTR text, ULONG count, LONG width, PWSTR out, ULONG cap)
{
    if (FontTextWidth(fi, text, count, 0) <= width) {
        ULONG n = count < cap ? count : cap;
        memcpy(out, text, n * sizeof(WCHAR));
        return n;
    }
    LONG dots = FontTextWidth(fi, L"...", 3, 0);
    ULONG n = 0;
    LONG used = 0;
    while (n < count && n + 3 < cap) {
        const GLYPH *g = FontGetGlyph(fi, text[n]);
        LONG adv = g ? g->Advance : 0;
        if (used + adv + dots > width)
            break;
        used += adv;
        out[n] = text[n];
        n++;
    }
    out[n++] = L'.';
    out[n++] = L'.';
    out[n++] = L'.';
    return n;
}

static void draw_atlas(SURFACE *dst, const REGION *clip, const RECT *dst_rect, const RECT *src)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlendSurface(dst, clip, dst_rect, g_theme.Atlas, src, blend);
}

/* fallback glyphs when the theme is missing: 1 pixel strokes scaled to the dpi. */
static void draw_fallback_glyph(SURFACE *s, const REGION *clip, int button, BOOLEAN maximized, const RECT *br,
                                ULONG pixel, UINT dpi)
{
    LONG size = scale(10, dpi);
    LONG cx = (br->left + br->right) / 2, cy = (br->top + br->bottom) / 2;
    LONG l = cx - size / 2, t = cy - size / 2;
    LONG th = W32Max(1, scale(1, dpi));
    switch (button) {
    case CAPTION_MIN: {
        RECT r = {l, cy, l + size, cy + th};
        DrawFillRect(s, clip, &r, pixel);
        break;
    }
    case CAPTION_MAX: {
        RECT edges[4] = {{l, t, l + size, t + th}, {l, t + size - th, l + size, t + size},
                         {l, t, l + th, t + size}, {l + size - th, t, l + size, t + size}};
        if (maximized)
            for (int i = 0; i < 4; i++)
                RectOffset(&edges[i], 0, scale(2, dpi));
        for (int i = 0; i < 4; i++)
            DrawFillRect(s, clip, &edges[i], pixel);
        break;
    }
    case CAPTION_CLOSE:
    case CAPTION_HELP:
        for (LONG i = 0; i < size; i++) {
            RECT a = {l + i, t + i, l + i + th, t + i + th};
            RECT b = {l + size - 1 - i, t + i, l + size - 1 - i + th, t + i + th};
            DrawFillRect(s, clip, &a, pixel);
            DrawFillRect(s, clip, &b, pixel);
        }
        break;
    }
}

static void draw_button(WND *w, SURFACE *s, const REGION *clip, POINT origin, int button, UINT dpi, BOOLEAN active)
{
    RECT br;
    FrameGetButtonRect(w, button, &br);
    if (RectEmpty(&br))
        return;
    RectOffset(&br, origin.x, origin.y);
    BOOLEAN disabled = (button == CAPTION_MAX && !(w->Style & WS_MAXIMIZEBOX)) ||
                       (button == CAPTION_MIN && !(w->Style & WS_MINIMIZEBOX)) ||
                       (button == CAPTION_CLOSE && (w->Flags & WNDF_NOCLOSE)) || (w->Style & WS_DISABLED);
    BOOLEAN hot = !disabled && w->CaptionHot == button && (!w->CaptionTracking || w->CaptionTracking == button);
    BOOLEAN pressed = hot && w->CaptionPressed == button;
    int frame = disabled ? 3 : pressed ? 2 : hot ? 1 : 0;

    if (hot || pressed) {
        ULONG fill = ColorToPixel(button == CAPTION_CLOSE ? (pressed ? CLOSE_PRESSED : CLOSE_HOT)
                                                          : (pressed ? BUTTON_PRESSED : BUTTON_HOT));
        DrawFillRect(s, clip, &br, fill);
    }

    BOOLEAN maximized = (w->Style & WS_MAXIMIZE) != 0;
    int kind;
    switch (button) {
    case CAPTION_CLOSE:
        kind = 0;
        break;
    case CAPTION_HELP:
        kind = 1;
        break;
    case CAPTION_MAX:
        kind = maximized ? 4 : 2;
        break;
    default:
        kind = 3;
        break;
    }
    if (g_theme.Ready) {
        int part = (active ? 11 : 64) + kind * 4 + dpi_slot(dpi);
        RECT src;
        if (atlas_frame(part, frame, &src)) {
            LONG gw = src.right - src.left, gh = src.bottom - src.top;
            LONG x = br.left + (br.right - br.left - gw) / 2;
            LONG y = br.top + (br.bottom - br.top - gh) / 2;
            RECT dst = {x, y, x + gw, y + gh};
            draw_atlas(s, clip, &dst, &src);
            return;
        }
    }
    COLORREF c = disabled ? RGB(0xC0, 0xC0, 0xC0)
                 : (button == CAPTION_CLOSE && hot) ? RGB(0xFF, 0xFF, 0xFF)
                 : active ? TEXT_ACTIVE
                          : TEXT_INACTIVE;
    draw_fallback_glyph(s, clip, button, maximized, &br, ColorToPixel(c), dpi);
}

static void paint_classic_edges(WND *w, SURFACE *s, const REGION *clip, RECT r)
{
    if (w->ExStyle & WS_EX_CLIENTEDGE) {
        ULONG shadow = ColorToPixel(MetricsSysColor(COLOR_3DSHADOW));
        ULONG light = ColorToPixel(MetricsSysColor(COLOR_3DHILIGHT));
        ULONG dark = ColorToPixel(MetricsSysColor(COLOR_3DDKSHADOW));
        ULONG face = ColorToPixel(MetricsSysColor(COLOR_3DLIGHT));
        for (int i = 0; i < 2; i++) {
            ULONG tl = i == 0 ? shadow : dark, br = i == 0 ? light : face;
            RECT top = {r.left, r.top, r.right, r.top + 1}, left = {r.left, r.top, r.left + 1, r.bottom};
            RECT bottom = {r.left, r.bottom - 1, r.right, r.bottom}, right = {r.right - 1, r.top, r.right, r.bottom};
            DrawFillRect(s, clip, &top, tl);
            DrawFillRect(s, clip, &left, tl);
            DrawFillRect(s, clip, &bottom, br);
            DrawFillRect(s, clip, &right, br);
            r.left++, r.top++, r.right--, r.bottom--;
        }
    } else if ((w->Style & WS_BORDER) || (w->ExStyle & WS_EX_STATICEDGE)) {
        ULONG c = ColorToPixel(MetricsSysColor((w->ExStyle & WS_EX_STATICEDGE) ? COLOR_3DSHADOW : COLOR_WINDOWFRAME));
        RECT edges[4] = {{r.left, r.top, r.right, r.top + 1},
                         {r.left, r.bottom - 1, r.right, r.bottom},
                         {r.left, r.top, r.left + 1, r.bottom},
                         {r.right - 1, r.top, r.right, r.bottom}};
        for (int i = 0; i < 4; i++)
            DrawFillRect(s, clip, &edges[i], c);
    }
}

void FramePaint(WND *w)
{
    WND *top = WndTopLevel(w);
    if (!top || !top->Surface || !WndIsVisible(w))
        return;
    SURFACE *s = top->Surface;
    UINT dpi = WndGetDpi(w);
    POINT origin = {w->WindowRect.left - top->WindowRect.left, w->WindowRect.top - top->WindowRect.top};

    REGION clip;
    RgnInit(&clip);
    WndComputeVisRgn(w, FALSE, 0, &clip);
    RECT client = w->ClientRect;
    RectOffset(&client, -top->WindowRect.left, -top->WindowRect.top);
    RgnCombineRect(&clip, &clip, &client, RGN_DIFF);
    if (RgnIsEmpty(&clip)) {
        RgnFree(&clip);
        return;
    }

    RECT window = {origin.x, origin.y, origin.x + (w->WindowRect.right - w->WindowRect.left),
                   origin.y + (w->WindowRect.bottom - w->WindowRect.top)};
    if (!is_top_frame(w->Style)) {
        paint_classic_edges(w, s, &clip, window);
        RgnFree(&clip);
        SurfaceMarkDirty(s, &window);
        return;
    }

    load_theme();
    BOOLEAN active = (w->Flags & WNDF_ACTIVE_FRAME) != 0;
    FRAME_METRICS m;
    FrameGetMetrics(w->Style, w->ExStyle, dpi, &m);
    RECT vis;
    FrameGetVisibleRect(w, &vis);
    RectOffset(&vis, origin.x, origin.y);

    /* the whole non client area takes the caption color; the host draws the border line over it. */
    DrawFillRect(s, &clip, &window, ColorToPixel(CAPTION_COLOR));

    if (FrameHasCaption(w->Style, w->ExStyle)) {
        LONG caption_top = vis.top;
        LONG caption_bottom = origin.y + m.Top;
        LONG x = vis.left + scale(12, dpi);
        BOOLEAN icon = (w->Style & WS_SYSMENU) && w->IconSmall && !(w->ExStyle & (WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW));
        if (icon) {
            LONG size = MetricsGet(SM_CXSMICON, dpi);
            LONG ix = vis.left + scale(8, dpi);
            LONG iy = caption_top + (caption_bottom - caption_top - size) / 2;
            HDC hdc = PaintGetDC(w, nullptr, DCX_WINDOW);
            DC *dc = DcLookup(hdc);
            if (dc) {
                RgnCopy(&dc->VisRgn, &clip);
                CursorDrawIcon(dc, ix - origin.x, iy - origin.y, w->IconSmall, size, size, DI_NORMAL);
                PaintReleaseDC(dc);
            }
            x = ix + size + scale(8, dpi);
        }
        FONT_INSTANCE *fi = caption_font(dpi);
        if (fi && w->Text && w->TextLength) {
            LONG right = caption_buttons_left(w) + origin.x - scale(4, dpi);
            WCHAR title[260];
            ULONG n = fit_title(fi, w->Text, w->TextLength, right - x, title, RTL_NUMBER_OF(title));
            LONG text_h = fi->Metrics.tmAscent + fi->Metrics.tmDescent;
            LONG baseline = caption_top + (caption_bottom - caption_top - text_h) / 2 + fi->Metrics.tmAscent;
            draw_text(s, &clip, fi, x, baseline, title, n, right, ColorToPixel(active ? TEXT_ACTIVE : TEXT_INACTIVE));
        }
        static const int buttons[] = {CAPTION_CLOSE, CAPTION_MAX, CAPTION_MIN, CAPTION_HELP};
        for (ULONG i = 0; i < RTL_NUMBER_OF(buttons); i++)
            draw_button(w, s, &clip, origin, buttons[i], dpi, active);
    }
    RgnFree(&clip);
    /* drawn straight into the surface, so the present needs to know. */
    SurfaceMarkDirty(s, &window);
    if (top->Thread == W32GetThread() && (top->Flags & WNDF_PRESENTED))
        PaintPresent(top);
}

/* ------------------------------------------------------------------ mouse */

static void repaint_buttons(WND *w)
{
    if (WndIsVisible(w))
        FramePaint(w);
}

static UINT button_command(WND *w, int button)
{
    switch (button) {
    case CAPTION_CLOSE:
        return SC_CLOSE;
    case CAPTION_MAX:
        return (w->Style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE;
    case CAPTION_MIN:
        return SC_MINIMIZE;
    case CAPTION_HELP:
        return SC_CONTEXTHELP;
    }
    return 0;
}

static BOOLEAN button_enabled(WND *w, int button)
{
    if (w->Style & WS_DISABLED)
        return FALSE;
    if (button == CAPTION_MAX)
        return (w->Style & WS_MAXIMIZEBOX) != 0;
    if (button == CAPTION_MIN)
        return (w->Style & WS_MINIMIZEBOX) != 0;
    if (button == CAPTION_CLOSE)
        return !(w->Flags & WNDF_NOCLOSE);
    return button != CAPTION_NONE;
}

LRESULT FrameMouse(WND *w, UINT msg, LONG hit, POINT pt)
{
    POINT wp = {pt.x - w->WindowRect.left, pt.y - w->WindowRect.top};
    int over = button_at(w, wp);
    if (!button_enabled(w, over))
        over = CAPTION_NONE;
    W32THREAD *t = w->Thread;
    switch (msg) {
    case WM_NCMOUSEMOVE:
        if (w->CaptionHot != over) {
            w->CaptionHot = (UCHAR)over;
            repaint_buttons(w);
        }
        break;
    case WM_NCLBUTTONDOWN:
        if (over != CAPTION_NONE) {
            w->CaptionHot = w->CaptionPressed = w->CaptionTracking = (UCHAR)over;
            if (t)
                InputSetCapture(t, w, (ULONG)hit);
            repaint_buttons(w);
        }
        break;
    case WM_NCLBUTTONUP:
        if (w->CaptionTracking) {
            int tracked = w->CaptionTracking;
            w->CaptionTracking = w->CaptionPressed = 0;
            w->CaptionHot = (UCHAR)over;
            if (t && t->Capture == WndHandle(w))
                InputSetCapture(t, nullptr, 0);
            repaint_buttons(w);
            if (over == tracked && !w->Header.Destroyed) {
                UINT cmd = button_command(w, tracked);
                if (cmd)
                    MsgSend(w, WM_SYSCOMMAND, cmd, MAKELPARAM(pt.x, pt.y));
            }
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (hit == HTCAPTION && (w->Style & WS_MAXIMIZEBOX) && (w->Style & WS_THICKFRAME))
            MsgSend(w, WM_SYSCOMMAND, (w->Style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE, MAKELPARAM(pt.x, pt.y));
        else if (hit == HTSYSMENU)
            MsgSend(w, WM_SYSCOMMAND, SC_CLOSE, MAKELPARAM(pt.x, pt.y));
        else if (over != CAPTION_NONE)
            return FrameMouse(w, WM_NCLBUTTONDOWN, hit, pt);
        break;
    case WM_NCMOUSELEAVE:
        if (w->CaptionHot && !w->CaptionTracking) {
            w->CaptionHot = CAPTION_NONE;
            repaint_buttons(w);
        }
        break;
    }
    return 0;
}
