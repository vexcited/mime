/*
 * backgrounds: theme images decoded once into premultiplied dib sections and
 * drawn the way uxtheme draws them, nine grid stretching or tiling with the
 * sizing margins, true size images aligned in the rectangle, glyphs over the
 * content area, border fill parts drawn with gdi.
 */
#include "uxtheme_private.h"
#include "../lib/png/png.h"

static struct {
    RTL_RUN_ONCE Once;
    RTL_CRITICAL_SECTION Lock;
    UX_BITMAP *List;
} g_bitmaps;

static ULONG NTAPI init_bitmaps(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_bitmaps.Lock);
    return TRUE;
}

static HBITMAP create_dib(int width, int height, DWORD **bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *p = NULL;
    HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &p, NULL, 0);
    *bits = p;
    return bm;
}

/* 24 and 32 bpp bmp files, the only depths themes use. 32 bpp ones carry straight alpha. */
static BOOL decode_bmp(const uint8_t *d, size_t n, DWORD *out, int width, int height)
{
    if (n < 54)
        return FALSE;
    uint32_t off = d[10] | d[11] << 8 | d[12] << 16 | (uint32_t)d[13] << 24;
    int32_t h = (int32_t)(d[22] | d[23] << 8 | d[24] << 16 | (uint32_t)d[25] << 24);
    int bpp = d[28] | d[29] << 8;
    uint32_t compression = d[30] | d[31] << 8 | d[32] << 16 | (uint32_t)d[33] << 24;
    if (compression != 0 && compression != 3)
        return FALSE;
    SIZE_T stride = (((SIZE_T)width * bpp + 31) / 32) * 4;
    if (off > n || stride * (SIZE_T)height > n - off)
        return FALSE;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = d + off + stride * (SIZE_T)(h > 0 ? height - 1 - y : y);
        DWORD *dst = out + (SIZE_T)y * width;
        for (int x = 0; x < width; x++) {
            uint32_t b, g, r, a;
            if (bpp == 32) {
                b = row[x * 4];
                g = row[x * 4 + 1];
                r = row[x * 4 + 2];
                a = row[x * 4 + 3];
                b = b * a / 255;
                g = g * a / 255;
                r = r * a / 255;
            } else {
                b = row[x * 3];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
                a = 255;
            }
            dst[x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    return TRUE;
}

UX_BITMAP *UxGetBitmap(const MimeMsstylesImage *image)
{
    RtlRunOnceExecuteOnce(&g_bitmaps.Once, init_bitmaps, NULL, NULL);
    COLORREF key = image->transparent && image->has_transparent_color ? image->transparent_color : CLR_INVALID;
    RtlEnterCriticalSection(&g_bitmaps.Lock);
    UX_BITMAP *b = g_bitmaps.List;
    while (b && (b->Data != image->data || b->Key != key))
        b = b->Next;
    RtlLeaveCriticalSection(&g_bitmaps.Lock);
    if (b)
        return b;

    DWORD *bits;
    HBITMAP bm = create_dib(image->width, image->height, &bits);
    if (!bm)
        return NULL;
    BOOL ok = FALSE, alpha = TRUE;
    if (image->format == MIME_MSSTYLES_FORMAT_PNG) {
        MimePngInfo info;
        ok = mime_png_info(image->data, image->size, &info) && (int)info.width == image->width &&
             (int)info.height == image->height &&
             mime_png_decode(image->data, image->size, &UxAllocator, MIME_PNG_PREMULTIPLIED, (uint8_t *)bits,
                             (size_t)image->width * 4) == MIME_PNG_OK;
        alpha = info.has_alpha;
    } else if (image->format == MIME_MSSTYLES_FORMAT_BMP) {
        ok = decode_bmp(image->data, image->size, bits, image->width, image->height);
        alpha = image->data[28] == 32;
    }
    if (!ok) {
        DeleteObject(bm);
        return NULL;
    }
    /* TRANSPARENTCOLOR keys out opaque images, images with alpha keep theirs. */
    if (key != CLR_INVALID && !alpha) {
        DWORD k = GetRValue(key) << 16 | GetGValue(key) << 8 | GetBValue(key);
        SIZE_T count = (SIZE_T)image->width * image->height;
        for (SIZE_T i = 0; i < count; i++)
            if ((bits[i] & 0xFFFFFF) == k)
                bits[i] = 0;
    }
    b = UxAlloc(sizeof(*b));
    if (!b) {
        DeleteObject(bm);
        return NULL;
    }
    b->Data = image->data;
    b->Key = key;
    b->Bitmap = bm;
    b->Bits = bits;
    b->Width = image->width;
    b->Height = image->height;
    RtlEnterCriticalSection(&g_bitmaps.Lock);
    UX_BITMAP *other = g_bitmaps.List;
    while (other && (other->Data != image->data || other->Key != key))
        other = other->Next;
    if (other) {
        RtlLeaveCriticalSection(&g_bitmaps.Lock);
        DeleteObject(bm);
        UxFree(b);
        return other;
    }
    b->Next = g_bitmaps.List;
    g_bitmaps.List = b;
    RtlLeaveCriticalSection(&g_bitmaps.Lock);
    return b;
}

int UxBackgroundType(UX_THEME *theme, int part, int state)
{
    int32_t v;
    return mime_msstyles_get_enum(&UxStyles, theme->Class, part, state, TMT_BGTYPE, &v) ? v : BT_BORDERFILL;
}

BOOL UxPartImage(UX_THEME *theme, int part, int state, const RECT *dest, MimeMsstylesImage *image)
{
    if (UxBackgroundType(theme, part, state) != BT_IMAGEFILE)
        return FALSE;
    int w = dest ? dest->right - dest->left : 0, h = dest ? dest->bottom - dest->top : 0;
    return mime_msstyles_get_image(&UxStyles, theme->Class, part, state, theme->Dpi, w > 0 ? w : 0, h > 0 ? h : 0,
                                   0, image);
}

/* ------------------------------------------------------------------ blitting */

typedef struct DRAW_SRC {
    HDC Dc;
    UX_BITMAP *Bitmap;
} DRAW_SRC;

static void blend(HDC dst, int x, int y, int w, int h, const DRAW_SRC *src, int sx, int sy, int sw, int sh)
{
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dst, x, y, w, h, src->Dc, sx, sy, sw, sh, bf);
}

/* one cell of the nine grid, stretched or repeated at the source size. */
static void cell(HDC dst, int x, int y, int w, int h, const DRAW_SRC *src, int sx, int sy, int sw, int sh, BOOL tile,
                 int scale)
{
    if (!tile || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) {
        blend(dst, x, y, w, h, src, sx, sy, sw, sh);
        return;
    }
    int tw = sw * scale, th = sh * scale;
    for (int ty = 0; ty < h; ty += th) {
        int ch = h - ty < th ? h - ty : th;
        for (int tx = 0; tx < w; tx += tw) {
            int cw = w - tx < tw ? w - tx : tw;
            blend(dst, x + tx, y + ty, cw, ch, src, sx, sy, (cw + scale - 1) / scale, (ch + scale - 1) / scale);
        }
    }
}

#define OMIT_BORDER 0x1u
#define OMIT_CONTENT 0x2u

static void draw_frame(HDC dc, const MimeMsstylesImage *img, const MimeMsstylesRect *frame, const DRAW_SRC *src,
                       const RECT *rect, unsigned omit)
{
    int sx = frame->left, sy = frame->top;
    int sw = frame->right - frame->left, sh = frame->bottom - frame->top;
    int x = rect->left, y = rect->top, w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0)
        return;

    if (img->sizing_type == ST_TRUESIZE) {
        int dw = img->draw_width ? img->draw_width : sw, dh = img->draw_height ? img->draw_height : sh;
        /* an image larger than the destination is shrunk to fit, keeping its ratio. guessed from screenshots. */
        if (dw > w || dh > h) {
            if ((LONG64)dw * h > (LONG64)dh * w) {
                dh = (int)((LONG64)dh * w / dw);
                dw = w;
            } else {
                dw = (int)((LONG64)dw * h / dh);
                dh = h;
            }
        }
        int dx = x, dy = y;
        if (img->halign == HA_CENTER)
            dx = x + (w - dw) / 2;
        else if (img->halign == HA_RIGHT)
            dx = x + w - dw;
        if (img->valign == VA_CENTER)
            dy = y + (h - dh) / 2;
        else if (img->valign == VA_BOTTOM)
            dy = y + h - dh;
        if (!(omit & OMIT_CONTENT))
            blend(dc, dx, dy, dw, dh, src, sx, sy, sw, sh);
        return;
    }

    int scale = img->margin_scale > 0 ? img->margin_scale : 1;
    int sl = img->sizing_margins.left, sr = img->sizing_margins.right;
    int st = img->sizing_margins.top, sb = img->sizing_margins.bottom;
    if (sl < 0 || sr < 0 || st < 0 || sb < 0 || sl + sr > sw || st + sb > sh)
        sl = sr = st = sb = 0;
    int l = sl * scale, r = sr * scale, t = st * scale, bo = sb * scale;
    if (l + r > w) {
        l = w * l / (l + r);
        r = w - l;
    }
    if (t + bo > h) {
        t = h * t / (t + bo);
        bo = h - t;
    }
    BOOL tile = img->sizing_type == ST_TILE;
    int cw = w - l - r, ch = h - t - bo, scw = sw - sl - sr, sch = sh - st - sb;
    if (!(omit & OMIT_BORDER)) {
        blend(dc, x, y, l, t, src, sx, sy, sl, st);
        cell(dc, x + l, y, cw, t, src, sx + sl, sy, scw, st, tile, scale);
        blend(dc, x + w - r, y, r, t, src, sx + sw - sr, sy, sr, st);
        cell(dc, x, y + t, l, ch, src, sx, sy + st, sl, sch, tile, scale);
        cell(dc, x + w - r, y + t, r, ch, src, sx + sw - sr, sy + st, sr, sch, tile, scale);
        blend(dc, x, y + h - bo, l, bo, src, sx, sy + sh - sb, sl, sb);
        cell(dc, x + l, y + h - bo, cw, bo, src, sx + sl, sy + sh - sb, scw, sb, tile, scale);
        blend(dc, x + w - r, y + h - bo, r, bo, src, sx + sw - sr, sy + sh - sb, sr, sb);
    }
    if (!(omit & OMIT_CONTENT) && !img->border_only)
        cell(dc, x + l, y + t, cw, ch, src, sx + sl, sy + st, scw, sch, tile, scale);
}

static BOOL draw_image(HDC dc, const MimeMsstylesImage *img, const RECT *rect, unsigned omit)
{
    UX_BITMAP *b = UxGetBitmap(img);
    if (!b)
        return FALSE;
    DRAW_SRC src = {CreateCompatibleDC(dc), b};
    if (!src.Dc)
        return FALSE;
    HGDIOBJ old = SelectObject(src.Dc, b->Bitmap);
    if (img->bg_fill && !(omit & OMIT_CONTENT)) {
        HBRUSH brush = CreateSolidBrush(img->fill_color);
        FillRect(dc, rect, brush);
        DeleteObject(brush);
    }
    draw_frame(dc, img, &img->src, &src, rect, omit);
    if (img->composed)
        draw_frame(dc, img, &img->overlay_src, &src, rect, omit);
    SelectObject(src.Dc, old);
    DeleteDC(src.Dc);
    return TRUE;
}

/* aerolite color images: border color around a fill pixel, stretched with the sizing margins. */
static BOOL draw_color_image(HDC dc, UX_THEME *theme, int part, int state, const RECT *rect, unsigned omit)
{
    MimeMsstylesColorImage ci;
    if (!mime_msstyles_get_color_image(&UxStyles, theme->Class, part, state, &ci) || ci.width <= 0 ||
        ci.height <= 0 || ci.width > 4096 || ci.height > 4096)
        return FALSE;
    DWORD *bits;
    HBITMAP bm = create_dib(ci.width, ci.height, &bits);
    if (!bm)
        return FALSE;
    for (int y = 0; y < ci.height; y++) {
        for (int x = 0; x < ci.width; x++) {
            DWORD v = 0;
            if (x >= ci.inset.left && x < ci.width - ci.inset.right && y >= ci.inset.top &&
                y < ci.height - ci.inset.bottom) {
                BOOL fill = x == ci.inset.left + ci.sizing.left && y == ci.inset.top + ci.sizing.top;
                v = fill ? ci.fill : ci.border;
                DWORD a = v >> 24;
                v = a << 24 | ((v >> 16 & 0xFF) * a / 255) << 16 | ((v >> 8 & 0xFF) * a / 255) << 8 |
                    ((v & 0xFF) * a / 255);
            }
            bits[y * ci.width + x] = v;
        }
    }
    MimeMsstylesImage img;
    memset(&img, 0, sizeof(img));
    img.sizing_type = ST_STRETCH;
    img.margin_scale = 1;
    img.sizing_margins.left = ci.sizing.left + ci.inset.left;
    img.sizing_margins.right = ci.sizing.right + ci.inset.right;
    img.sizing_margins.top = ci.sizing.top + ci.inset.top;
    img.sizing_margins.bottom = ci.sizing.bottom + ci.inset.bottom;
    MimeMsstylesRect frame = {0, 0, ci.width, ci.height};
    UX_BITMAP fake = {NULL, NULL, CLR_INVALID, bm, bits, ci.width, ci.height};
    DRAW_SRC src = {CreateCompatibleDC(dc), &fake};
    HGDIOBJ old = SelectObject(src.Dc, bm);
    draw_frame(dc, &img, &frame, &src, rect, omit);
    SelectObject(src.Dc, old);
    DeleteDC(src.Dc);
    DeleteObject(bm);
    return TRUE;
}

/* ------------------------------------------------------------------ border fill */

typedef struct BORDER_FILL {
    int Type, Fill, Size;
    COLORREF BorderColor, FillColor;
    BOOL BorderOnly, NoDraw;
    int RoundWidth, RoundHeight;
} BORDER_FILL;

static int prop_or(UX_THEME *theme, int part, int state, int prop, int def)
{
    int32_t v;
    return mime_msstyles_get_int(&UxStyles, theme->Class, part, state, prop, &v) ? v : def;
}

static void get_border_fill(UX_THEME *theme, int part, int state, BORDER_FILL *bf)
{
    uint32_t c;
    bf->NoDraw = UxBackgroundType(theme, part, state) == BT_NONE;
    bf->Type = prop_or(theme, part, state, TMT_BORDERTYPE, BT_RECT);
    bf->Fill = prop_or(theme, part, state, TMT_FILLTYPE, FT_SOLID);
    bf->Size = prop_or(theme, part, state, TMT_BORDERSIZE, 1);
    bf->BorderOnly = prop_or(theme, part, state, TMT_BORDERONLY, 0) != 0;
    bf->RoundWidth = prop_or(theme, part, state, TMT_ROUNDCORNERWIDTH, 0);
    bf->RoundHeight = prop_or(theme, part, state, TMT_ROUNDCORNERHEIGHT, 0);
    bf->BorderColor = mime_msstyles_get_color(&UxStyles, theme->Class, part, state, TMT_BORDERCOLOR, &c) ? c : 0;
    bf->FillColor =
        mime_msstyles_get_color(&UxStyles, theme->Class, part, state, TMT_FILLCOLOR, &c) ? c : RGB(255, 255, 255);
    if (bf->Size < 0)
        bf->Size = 0;
}

static COLORREF lerp_color(COLORREF a, COLORREF b, int num, int den)
{
    if (den <= 0)
        return a;
    int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * num / den;
    int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * num / den;
    int bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * num / den;
    return RGB(r, g, bl);
}

static void fill_solid(HDC dc, int l, int t, int r, int b, COLORREF color)
{
    if (r <= l || b <= t)
        return;
    RECT rc = {l, t, r, b};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

/* vertical and horizontal gradients through GRADIENTCOLOR1..5 weighted by GRADIENTRATIO1..5. */
static void fill_gradient(HDC dc, UX_THEME *theme, int part, int state, const RECT *rc, BOOL vertical,
                          COLORREF fallback)
{
    COLORREF colors[5];
    int ratios[5], n = 0, total = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t c;
        if (!mime_msstyles_get_color(&UxStyles, theme->Class, part, state, TMT_GRADIENTCOLOR1 + i, &c))
            break;
        colors[n] = c;
        ratios[n] = prop_or(theme, part, state, TMT_GRADIENTRATIO1 + i, 0);
        total += ratios[n];
        n++;
    }
    int len = vertical ? rc->bottom - rc->top : rc->right - rc->left;
    if (n < 2 || len <= 0) {
        fill_solid(dc, rc->left, rc->top, rc->right, rc->bottom, n ? colors[0] : fallback);
        return;
    }
    if (total <= 0) {
        for (int i = 0; i < n; i++)
            ratios[i] = 1;
        total = n;
    }
    /* each color owns a band of its ratio, blending toward the next one. */
    int pos = 0;
    for (int i = 0; i < n && pos < len; i++) {
        int band = i == n - 1 ? len - pos : len * ratios[i] / total;
        COLORREF next = i + 1 < n ? colors[i + 1] : colors[i];
        for (int k = 0; k < band && pos < len; k++, pos++) {
            COLORREF c = lerp_color(colors[i], next, k, band);
            if (vertical)
                fill_solid(dc, rc->left, rc->top + pos, rc->right, rc->top + pos + 1, c);
            else
                fill_solid(dc, rc->left + pos, rc->top, rc->left + pos + 1, rc->bottom, c);
        }
    }
}

static void draw_border_fill(HDC dc, UX_THEME *theme, int part, int state, const RECT *rect, unsigned omit)
{
    BORDER_FILL bf;
    get_border_fill(theme, part, state, &bf);
    if (bf.NoDraw)
        return;
    int size = UxScale(theme, bf.Size);
    if (bf.Type != BT_RECT) {
        BOOL border = size && !(omit & OMIT_BORDER), fill = !bf.BorderOnly && !(omit & OMIT_CONTENT);
        HPEN pen = border ? CreatePen(PS_SOLID, size, bf.BorderColor) : NULL;
        HBRUSH brush = fill ? CreateSolidBrush(bf.FillColor) : NULL;
        HGDIOBJ old_pen = SelectObject(dc, pen ? (HGDIOBJ)pen : GetStockObject(NULL_PEN));
        HGDIOBJ old_brush = SelectObject(dc, brush ? (HGDIOBJ)brush : GetStockObject(NULL_BRUSH));
        if (bf.Type == BT_ELLIPSE)
            Ellipse(dc, rect->left, rect->top, rect->right, rect->bottom);
        else
            RoundRect(dc, rect->left, rect->top, rect->right, rect->bottom, UxScale(theme, bf.RoundWidth),
                      UxScale(theme, bf.RoundHeight));
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        if (pen)
            DeleteObject(pen);
        if (brush)
            DeleteObject(brush);
        return;
    }
    RECT inner = {rect->left + size, rect->top + size, rect->right - size, rect->bottom - size};
    if (!bf.BorderOnly && !(omit & OMIT_CONTENT)) {
        if (bf.Fill == FT_VERTGRADIENT || bf.Fill == FT_HORZGRADIENT)
            fill_gradient(dc, theme, part, state, &inner, bf.Fill == FT_VERTGRADIENT, bf.FillColor);
        else
            fill_solid(dc, inner.left, inner.top, inner.right, inner.bottom, bf.FillColor);
    }
    if (size && !(omit & OMIT_BORDER)) {
        fill_solid(dc, rect->left, rect->top, rect->right, inner.top, bf.BorderColor);
        fill_solid(dc, rect->left, inner.bottom, rect->right, rect->bottom, bf.BorderColor);
        fill_solid(dc, rect->left, inner.top, inner.left, inner.bottom, bf.BorderColor);
        fill_solid(dc, inner.right, inner.top, rect->right, inner.bottom, bf.BorderColor);
    }
}

/* ------------------------------------------------------------------ content rectangles */

static void deflate(RECT *r, int l, int t, int ri, int b)
{
    r->left += l;
    r->top += t;
    r->right -= ri;
    r->bottom -= b;
}

static void content_margins(UX_THEME *theme, int part, int state, const RECT *bounds, MARGINS *m)
{
    MimeMsstylesImage img;
    MimeMsstylesMargins mm;
    memset(m, 0, sizeof(*m));
    if (UxPartImage(theme, part, state, bounds, &img)) {
        int s = img.margin_scale > 0 ? img.margin_scale : 1;
        m->cxLeftWidth = img.content_margins.left * s;
        m->cxRightWidth = img.content_margins.right * s;
        m->cyTopHeight = img.content_margins.top * s;
        m->cyBottomHeight = img.content_margins.bottom * s;
    } else if (mime_msstyles_get_margins(&UxStyles, theme->Class, part, state, TMT_CONTENTMARGINS, &mm)) {
        m->cxLeftWidth = UxScale(theme, mm.left);
        m->cxRightWidth = UxScale(theme, mm.right);
        m->cyTopHeight = UxScale(theme, mm.top);
        m->cyBottomHeight = UxScale(theme, mm.bottom);
    } else if (UxBackgroundType(theme, part, state) == BT_BORDERFILL) {
        BORDER_FILL bf;
        get_border_fill(theme, part, state, &bf);
        int s = UxScale(theme, bf.Size);
        m->cxLeftWidth = m->cxRightWidth = m->cyTopHeight = m->cyBottomHeight = s;
    }
}

THEMEAPI GetThemeBackgroundContentRect(HTHEME handle, HDC dc, int part, int state, LPCRECT bounds, LPRECT content)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!bounds || !content) {
        UxRelease(theme);
        return E_POINTER;
    }
    MARGINS m;
    content_margins(theme, part, state, bounds, &m);
    RECT r = *bounds;
    deflate(&r, m.cxLeftWidth, m.cyTopHeight, m.cxRightWidth, m.cyBottomHeight);
    *content = r;
    UxRelease(theme);
    return S_OK;
}

THEMEAPI GetThemeBackgroundExtent(HTHEME handle, HDC dc, int part, int state, LPCRECT content, LPRECT extent)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!content || !extent) {
        UxRelease(theme);
        return E_POINTER;
    }
    MARGINS m;
    content_margins(theme, part, state, content, &m);
    RECT r = *content;
    deflate(&r, -m.cxLeftWidth, -m.cyTopHeight, -m.cxRightWidth, -m.cyBottomHeight);
    *extent = r;
    UxRelease(theme);
    return S_OK;
}

/* ------------------------------------------------------------------ glyphs */

static void draw_glyph(HDC dc, UX_THEME *theme, int part, int state, const RECT *rect)
{
    int32_t type;
    if (!mime_msstyles_get_enum(&UxStyles, theme->Class, part, state, TMT_GLYPHTYPE, &type) || type == GT_NONE)
        return;
    MARGINS m;
    content_margins(theme, part, state, rect, &m);
    RECT content = *rect;
    deflate(&content, m.cxLeftWidth, m.cyTopHeight, m.cxRightWidth, m.cyBottomHeight);
    if (type == GT_IMAGEGLYPH) {
        MimeMsstylesImage glyph;
        if (!mime_msstyles_get_image(&UxStyles, theme->Class, part, state, theme->Dpi, content.right - content.left,
                                     content.bottom - content.top, MIME_MSSTYLES_GLYPH, &glyph))
            return;
        glyph.sizing_type = ST_TRUESIZE;
        glyph.bg_fill = false;
        draw_image(dc, &glyph, &content, 0);
        return;
    }
    if (type == GT_FONTGLYPH) {
        MimeMsstylesFont f;
        int32_t index;
        uint32_t color;
        if (!mime_msstyles_get_font(&UxStyles, theme->Class, part, state, TMT_GLYPHFONT, theme->Dpi, &f) ||
            !mime_msstyles_get_int(&UxStyles, theme->Class, part, state, TMT_GLYPHINDEX, &index))
            return;
        if (!mime_msstyles_get_color(&UxStyles, theme->Class, part, state, TMT_GLYPHTEXTCOLOR, &color))
            color = 0;
        LOGFONTW lf;
        memcpy(&lf, &f, sizeof(lf));
        HFONT font = CreateFontIndirectW(&lf);
        HGDIOBJ old = SelectObject(dc, font);
        COLORREF old_color = SetTextColor(dc, color);
        int old_mode = SetBkMode(dc, TRANSPARENT);
        WCHAR ch = (WCHAR)index;
        DrawTextW(dc, &ch, 1, &content, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetBkMode(dc, old_mode);
        SetTextColor(dc, old_color);
        SelectObject(dc, old);
        DeleteObject(font);
    }
}

/* ------------------------------------------------------------------ drawing entry points */

static HRESULT draw_background(UX_THEME *theme, HDC dc, int part, int state, const RECT *rect, DWORD flags,
                               const RECT *clip)
{
    if (rect->right <= rect->left || rect->bottom <= rect->top)
        return S_OK;
    int saved = 0;
    if (clip) {
        saved = SaveDC(dc);
        IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
    }
    unsigned omit = 0;
    if (flags & DTBG_OMITBORDER)
        omit |= OMIT_BORDER;
    if (flags & DTBG_OMITCONTENT)
        omit |= OMIT_CONTENT;
    BOOL glyph_only = prop_or(theme, part, state, TMT_GLYPHONLY, 0) != 0;
    int type = UxBackgroundType(theme, part, state);
    if (!glyph_only) {
        MimeMsstylesImage img;
        if (type == BT_IMAGEFILE) {
            if (!(UxPartImage(theme, part, state, rect, &img) && draw_image(dc, &img, rect, omit)))
                draw_color_image(dc, theme, part, state, rect, omit);
        } else {
            draw_border_fill(dc, theme, part, state, rect, omit);
        }
    }
    if (!(omit & OMIT_CONTENT))
        draw_glyph(dc, theme, part, state, rect);
    if (saved)
        RestoreDC(dc, saved);
    return S_OK;
}

THEMEAPI DrawThemeBackgroundEx(HTHEME handle, HDC dc, int part, int state, LPCRECT rect, const DTBGOPTS *options)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!dc)
        hr = E_HANDLE;
    else if (!rect)
        hr = E_POINTER;
    else if (options && options->dwSize != sizeof(DTBGOPTS))
        hr = E_FAIL;
    else
        hr = draw_background(theme, dc, part, state, rect, options ? options->dwFlags : 0,
                             options && (options->dwFlags & DTBG_CLIPRECT) ? &options->rcClip : NULL);
    UxRelease(theme);
    return hr;
}

THEMEAPI DrawThemeBackground(HTHEME handle, HDC dc, int part, int state, LPCRECT rect, LPCRECT clip)
{
    DTBGOPTS opts = {sizeof(opts), clip ? DTBG_CLIPRECT : 0, {0, 0, 0, 0}};
    if (clip)
        opts.rcClip = *clip;
    return DrawThemeBackgroundEx(handle, dc, part, state, rect, &opts);
}

THEMEAPI GetThemePartSize(HTHEME handle, HDC dc, int part, int state, LPCRECT rect, enum THEMESIZE size_type,
                          SIZE *size)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!size) {
        UxRelease(theme);
        return E_POINTER;
    }
    MimeMsstylesImage img;
    if (UxPartImage(theme, part, state, rect, &img)) {
        if (size_type == TS_MIN) {
            int s = img.margin_scale > 0 ? img.margin_scale : 1;
            int cx = (img.content_margins.left + img.content_margins.right) * s;
            int cy = (img.content_margins.top + img.content_margins.bottom) * s;
            size->cx = cx < 1 ? 1 : cx;
            size->cy = cy < 1 ? 1 : cy;
        } else if (size_type == TS_TRUE) {
            size->cx = img.frame_width;
            size->cy = img.frame_height;
        } else if (size_type == TS_DRAW) {
            if (img.sizing_type != ST_TRUESIZE) {
                size->cx = rect ? rect->right - rect->left : 0;
                size->cy = rect ? rect->bottom - rect->top : 0;
            } else {
                size->cx = img.draw_width;
                size->cy = img.draw_height;
            }
        } else {
            hr = E_INVALIDARG;
        }
    } else {
        BORDER_FILL bf;
        get_border_fill(theme, part, state, &bf);
        int s = UxScale(theme, bf.Size);
        if (size_type == TS_MIN) {
            size->cx = size->cy = 2 * s < 1 ? 1 : 2 * s;
        } else if (size_type == TS_TRUE) {
            size->cx = size->cy = 2 * s + 1;
        } else {
            hr = E_INVALIDARG;
        }
    }
    UxRelease(theme);
    return hr;
}

static BOOL frame_has_alpha(UX_BITMAP *b, const MimeMsstylesRect *src)
{
    for (int y = src->top; y < src->bottom && y < b->Height; y++)
        for (int x = src->left; x < src->right && x < b->Width; x++)
            if ((b->Bits[(SIZE_T)y * b->Width + x] >> 24) != 0xFF)
                return TRUE;
    return FALSE;
}

static BOOL partially_transparent(UX_THEME *theme, int part, int state)
{
    if (prop_or(theme, part, state, TMT_GLYPHONLY, 0))
        return TRUE;
    MimeMsstylesImage img;
    if (UxPartImage(theme, part, state, NULL, &img)) {
        if (img.transparent || img.border_only)
            return TRUE;
        UX_BITMAP *b = UxGetBitmap(&img);
        return b && frame_has_alpha(b, &img.src);
    }
    if (UxBackgroundType(theme, part, state) == BT_IMAGEFILE)
        return TRUE;
    BORDER_FILL bf;
    get_border_fill(theme, part, state, &bf);
    return bf.NoDraw || bf.Type != BT_RECT;
}

THEMEAPI_(BOOL) IsThemeBackgroundPartiallyTransparent(HTHEME handle, int part, int state)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme) {
        SetLastError((DWORD)hr);
        return FALSE;
    }
    BOOL result = partially_transparent(theme, part, state);
    SetLastError(0);
    UxRelease(theme);
    return result;
}

/* the region of the opaque pixels, drawn into a scratch dib and read back. */
static HRGN alpha_region(UX_THEME *theme, int part, int state, const RECT *rect)
{
    int w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return CreateRectRgn(rect->left, rect->top, rect->right, rect->bottom);
    DWORD *bits;
    HBITMAP bm = create_dib(w, h, &bits);
    if (!bm)
        return NULL;
    HDC mem = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(mem, bm);
    memset(bits, 0, (SIZE_T)w * h * 4);
    RECT local = {0, 0, w, h};
    draw_background(theme, mem, part, state, &local, 0, NULL);
    GdiFlush();
    int threshold = prop_or(theme, part, state, TMT_ALPHATHRESHOLD, 255);
    SIZE_T cap = 64, count = 0;
    RGNDATA *data = UxAlloc(sizeof(RGNDATAHEADER) + cap * sizeof(RECT));
    for (int y = 0; data && y < h; y++) {
        for (int x = 0; x < w;) {
            while (x < w && (int)(bits[(SIZE_T)y * w + x] >> 24) < threshold)
                x++;
            int start = x;
            while (x < w && (int)(bits[(SIZE_T)y * w + x] >> 24) >= threshold)
                x++;
            if (x == start)
                continue;
            if (count == cap) {
                RGNDATA *bigger = UxAlloc(sizeof(RGNDATAHEADER) + cap * 2 * sizeof(RECT));
                if (bigger)
                    memcpy(bigger, data, sizeof(RGNDATAHEADER) + cap * sizeof(RECT));
                UxFree(data);
                data = bigger;
                cap *= 2;
                if (!data)
                    break;
            }
            RECT *r = (RECT *)data->Buffer + count++;
            SetRect(r, rect->left + start, rect->top + y, rect->left + x, rect->top + y + 1);
        }
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bm);
    if (!data)
        return NULL;
    data->rdh.dwSize = sizeof(RGNDATAHEADER);
    data->rdh.iType = 1; /* RDH_RECTANGLES */
    data->rdh.nCount = (DWORD)count;
    data->rdh.nRgnSize = (DWORD)(count * sizeof(RECT));
    data->rdh.rcBound = *rect;
    HRGN rgn = ExtCreateRegion(NULL, (DWORD)(sizeof(RGNDATAHEADER) + count * sizeof(RECT)), data);
    UxFree(data);
    return rgn;
}

THEMEAPI GetThemeBackgroundRegion(HTHEME handle, HDC dc, int part, int state, LPCRECT rect, HRGN *region)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!rect || !region) {
        UxRelease(theme);
        return E_POINTER;
    }
    HRGN rgn;
    if (partially_transparent(theme, part, state))
        rgn = alpha_region(theme, part, state, rect);
    else
        rgn = CreateRectRgn(rect->left, rect->top, rect->right, rect->bottom);
    if (rgn)
        *region = rgn;
    else
        hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY);
    UxRelease(theme);
    return hr;
}

THEMEAPI HitTestThemeBackground(HTHEME handle, HDC dc, int part, int state, DWORD options, LPCRECT rect,
                                HRGN region, POINT test, WORD *code)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!rect || !code) {
        UxRelease(theme);
        return E_POINTER;
    }
    *code = HTNOWHERE;
    if (!PtInRect(rect, test) || (region && !PtInRegion(region, test.x, test.y))) {
        UxRelease(theme);
        return S_OK;
    }
    *code = HTCLIENT;
    if (options & (HTTB_RESIZINGBORDER | HTTB_FIXEDBORDER | HTTB_CAPTION)) {
        MARGINS m;
        MimeMsstylesMargins sm;
        memset(&m, 0, sizeof(m));
        if (options & HTTB_SYSTEMSIZINGMARGINS) {
            int f = GetSystemMetrics(32 /* SM_CXFRAME */);
            m.cxLeftWidth = m.cxRightWidth = m.cyTopHeight = m.cyBottomHeight = f;
        } else if (mime_msstyles_get_margins(&UxStyles, theme->Class, part, state, TMT_SIZINGMARGINS, &sm)) {
            m.cxLeftWidth = sm.left;
            m.cxRightWidth = sm.right;
            m.cyTopHeight = sm.top;
            m.cyBottomHeight = sm.bottom;
        }
        BOOL left = test.x < rect->left + m.cxLeftWidth, right = test.x >= rect->right - m.cxRightWidth;
        BOOL top = test.y < rect->top + m.cyTopHeight, bottom = test.y >= rect->bottom - m.cyBottomHeight;
        if (options & HTTB_RESIZINGBORDER) {
            BOOL l = left && (options & HTTB_RESIZINGBORDER_LEFT), r = right && (options & HTTB_RESIZINGBORDER_RIGHT);
            BOOL t = top && (options & HTTB_RESIZINGBORDER_TOP), b = bottom && (options & HTTB_RESIZINGBORDER_BOTTOM);
            if (t && l)
                *code = HTTOPLEFT;
            else if (t && r)
                *code = HTTOPRIGHT;
            else if (b && l)
                *code = HTBOTTOMLEFT;
            else if (b && r)
                *code = HTBOTTOMRIGHT;
            else if (t)
                *code = (options & HTTB_CAPTION) ? HTCAPTION : HTTOP;
            else if (b)
                *code = HTBOTTOM;
            else if (l)
                *code = HTLEFT;
            else if (r)
                *code = HTRIGHT;
        } else if ((options & HTTB_FIXEDBORDER) && (left || right || top || bottom)) {
            *code = (options & HTTB_CAPTION) && top ? HTCAPTION : HTBORDER;
        } else if ((options & HTTB_CAPTION) && top) {
            *code = HTCAPTION;
        }
    }
    UxRelease(theme);
    return S_OK;
}

/* asks the parent to paint its background into dc, under the child rectangle. */
THEMEAPI DrawThemeParentBackgroundEx(HWND hwnd, HDC dc, DWORD flags, const RECT *rect)
{
    HWND parent = GetParent(hwnd);
    if (!parent)
        parent = hwnd;
    if (!dc)
        return E_HANDLE;
    RECT rc;
    if (rect)
        rc = *rect;
    else
        GetClientRect(hwnd, &rc);
    POINT org = {0, 0};
    MapWindowPoints(hwnd, parent, &org, 1);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    POINT old;
    OffsetViewportOrgEx(dc, -org.x, -org.y, &old);
    if (flags & DTPB_USECTLCOLORSTATIC) {
        HBRUSH brush = (HBRUSH)SendMessageW(parent, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)hwnd);
        if (brush) {
            RECT prc = rc;
            OffsetRect(&prc, org.x, org.y);
            FillRect(dc, &prc, brush);
        }
    } else {
        SendMessageW(parent, WM_ERASEBKGND, (WPARAM)dc, 0);
        if (!(flags & DTPB_USEERASEBKGND))
            SendMessageW(parent, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    }
    RestoreDC(dc, saved);
    return S_OK;
}

THEMEAPI DrawThemeParentBackground(HWND hwnd, HDC dc, const RECT *rect)
{
    return DrawThemeParentBackgroundEx(hwnd, dc, 0, rect);
}

THEMEAPI DrawThemeEdge(HTHEME handle, HDC dc, int part, int state, LPCRECT dest, UINT edge, UINT flags,
                       LPRECT content)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!dest) {
        UxRelease(theme);
        return E_POINTER;
    }
    RECT rc = *dest;
    DrawEdge(dc, &rc, edge, flags);
    if (content)
        *content = rc;
    UxRelease(theme);
    return S_OK;
}

typedef BOOL(WINAPI *IMAGELIST_DRAW)(HANDLE list, int index, HDC dc, int x, int y, UINT style);

THEMEAPI DrawThemeIcon(HTHEME handle, HDC dc, int part, int state, LPCRECT rect, HANDLE image_list, int index)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    UxRelease(theme);
    if (!rect)
        return E_POINTER;
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    IMAGELIST_DRAW draw = comctl ? (IMAGELIST_DRAW)(void *)GetProcAddress(comctl, "ImageList_Draw") : NULL;
    if (!draw)
        return E_FAIL;
    return draw(image_list, index, dc, rect->left, rect->top, 0) ? S_OK : E_FAIL;
}
