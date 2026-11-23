#include "win32k.h"

/*
 * the strip of the system bitmaps, which user32 draws check boxes, radio buttons and scroll bar arrows from:
 * NtUserGetOemBitmapSize gives the size of one and NtUserBitBltSysBmp copies it. win32kfull 26300 has 93 of them,
 * drawn each with DrawFrameControl when the metrics change, see CreateGlobalDpiBitmapStrip. the arrows of the scroll
 * bars, 46 to 61, and the buttons, 71 to 86, are drawn here. the caption and menu ones keep their place in the strip
 * but have no size yet.
 */

#define OEM_BITMAPS 93
#define OEM_BUTTON_FIRST 71
#define OEM_BUTTON_LAST 86
/* up, down, right and left, each one normal, pushed flat, hot and inactive. */
#define OEM_ARROW_FIRST 46
#define OEM_ARROW_LAST 61

#define DFC_CAPTION 1
#define DFC_MENU 2
#define DFC_SCROLL 3
#define DFC_BUTTON 4
#define DFC_POPUPMENU 5
#define DFC_CAPTIONBAR 0xFFFF /* filled by CreateGlobalDpiBitmapStrip itself. */

#define DFCS_BUTTONRADIOIMAGE 0x0001
#define DFCS_BUTTONRADIOMASK 0x0002
#define DFCS_BUTTONRADIO 0x0004
#define DFCS_BUTTON3STATE 0x0008
#define DFCS_INACTIVE 0x0100
#define DFCS_PUSHED 0x0200
#define DFCS_CHECKED 0x0400
#define DFCS_HOT 0x1000

/* the type and state of each bitmap, the table of win32kfull 26300 at 0x140370920. */
static const USHORT g_table[OEM_BITMAPS][2] = {
    {1, 0x0000}, {1, 0x0200}, {1, 0x1000}, {1, 0x0100}, {1, 0x0001}, {1, 0x0201}, {1, 0x1001}, {1, 0x0101},
    {1, 0x0003}, {1, 0x0203}, {1, 0x1003}, {1, 0x0004}, {1, 0x0204}, {1, 0x1004}, {1, 0x0002}, {1, 0x0202},
    {1, 0x1002}, {1, 0x0102}, {1, 0x0040}, {1, 0x0240}, {1, 0x1040}, {1, 0x0140}, {1, 0x0041}, {1, 0x0241},
    {1, 0x1041}, {1, 0x0141}, {1, 0x0043}, {1, 0x0243}, {1, 0x1043}, {0xFFFF, 0x0000}, {0xFFFF, 0x0100},
    {0xFFFF, 0x0000}, {0xFFFF, 0x0100}, {0xFFFF, 0x0000}, {0xFFFF, 0x0100}, {0xFFFF, 0x0000}, {0xFFFF, 0x0100},
    {0xFFFF, 0x0000}, {0xFFFF, 0x0100}, {0xFFFF, 0x0001}, {0xFFFF, 0x0101}, {1, 0x0080}, {1, 0x0280}, {1, 0x1080},
    {1, 0x0180}, {3, 0x0008}, {3, 0x0000}, {3, 0x4200}, {3, 0x1000}, {3, 0x0100}, {3, 0x0001}, {3, 0x4201},
    {3, 0x1001}, {3, 0x0101}, {3, 0x0003}, {3, 0x4203}, {3, 0x1003}, {3, 0x0103}, {3, 0x0002}, {3, 0x4202},
    {3, 0x1002}, {3, 0x0102}, {2, 0x0000}, {2, 0x0001}, {2, 0x0002}, {2, 0x0008}, {2, 0x1008}, {2, 0x0108},
    {2, 0x0010}, {2, 0x1010}, {2, 0x0110}, {4, 0x0002}, {4, 0x0000}, {4, 0x0400}, {4, 0x0200}, {4, 0x0600},
    {4, 0x0500}, {4, 0x0001}, {4, 0x0401}, {4, 0x0201}, {4, 0x0601}, {4, 0x0501}, {4, 0x0008}, {4, 0x0408},
    {4, 0x0208}, {4, 0x0608}, {4, 0x0508}, {5, 0x0040}, {5, 0x0043}, {5, 0x0042}, {5, 0x0041}, {3, 0x0010},
    {2, 0x0004},
};

typedef struct OEM_STRIP {
    UINT Dpi;
    RECT Rects[OEM_BITMAPS]; /* where each one is in the surface, empty when it has no size. */
    SURFACE *Surface;
} OEM_STRIP;

static OEM_STRIP g_strips[4];

static void fill(DC *dc, const RECT *rect, COLORREF color)
{
    BRUSH *brush = GdiCreateBrush(BS_SOLID, color, 0);
    if (!brush)
        return;
    REGION clip;
    RgnInit(&clip);
    if (DcGetClip(dc, &clip))
        DrawBrushRect(dc, &clip, rect, brush, PATCOPY);
    RgnFree(&clip);
    GdiDeleteObject(&brush->Header);
}

/*
 * DrawBox of win32kfull: the glyphs of marlett one over the other, the inside, the four arcs or edges of the
 * border and the mark.
 */
static void draw_box(DC *dc, const RECT *rect, USHORT state)
{
    PCWSTR glyphs = L"nmlkji";
    COLORREF colors[5] = {};
    BOOLEAN dithered = (state & (DFCS_BUTTON3STATE | DFCS_PUSHED | DFCS_INACTIVE | DFCS_CHECKED)) ==
                       (DFCS_BUTTON3STATE | DFCS_CHECKED);
    if (state & DFCS_BUTTONRADIOMASK) {
        fill(dc, rect, RGB(255, 255, 255));
    } else {
        colors[1] = MetricsSysColor(COLOR_3DLIGHT);
        colors[2] = MetricsSysColor(COLOR_3DDKSHADOW);
        colors[3] = MetricsSysColor(COLOR_3DHILIGHT);
        colors[4] = MetricsSysColor(COLOR_3DSHADOW);
        colors[0] = MetricsSysColor(state & (DFCS_PUSHED | DFCS_INACTIVE) ? COLOR_3DFACE
                                    : dithered                             ? COLOR_3DHILIGHT
                                                                           : COLOR_WINDOW);
        if (state & DFCS_BUTTONRADIOIMAGE)
            fill(dc, rect, RGB(0, 0, 0));
        else if (!(state & DFCS_BUTTONRADIO))
            glyphs = L"gfedcb";
    }
    LONG width = rect->right - rect->left, height = rect->bottom - rect->top, side = W32Min(width, height);
    LONG x = rect->left + (width - side) / 2, y = rect->top + (height - side) / 2;
    if (dithered && (W32Display.Bits < 8 || MetricsSysColor(COLOR_3DHILIGHT) == RGB(255, 255, 255))) {
        /* hbrGray with the face for its clear bits and the highlight for the set ones, aligned on the strip. */
        BRUSH *gray = (BRUSH *)GdiLookup(MetricsGrayBrush(), GDI_TYPE_BRUSH);
        COLORREF text = dc->State.TextColor, back = dc->State.BkColor;
        dc->State.TextColor = MetricsSysColor(COLOR_3DFACE);
        dc->State.BkColor = MetricsSysColor(COLOR_3DHILIGHT);
        REGION clip;
        RgnInit(&clip);
        if (gray && DcGetClip(dc, &clip))
            DrawBrushRect(dc, &clip, &(RECT){x, y, x + width, y + height}, gray, PATCOPY);
        RgnFree(&clip);
        dc->State.TextColor = text;
        dc->State.BkColor = back;
    } else {
        dc->State.TextColor = colors[0];
        TextDraw(dc, x, y, 0, nullptr, glyphs, 1, nullptr);
    }
    for (ULONG i = 1; i < 5; i++) {
        dc->State.TextColor = colors[i];
        TextDraw(dc, x, y, 0, nullptr, glyphs + i, 1, nullptr);
    }
    if (state & DFCS_CHECKED) {
        dc->State.TextColor = MetricsSysColor(state & (DFCS_BUTTON3STATE | DFCS_INACTIVE) ? COLOR_3DSHADOW
                                              : state & DFCS_HOT                          ? COLOR_HOTLIGHT
                                                                                          : COLOR_WINDOWTEXT);
        TextDraw(dc, x, y, 0, nullptr, glyphs + 5, 1, nullptr);
    }
}

static void frame(DC *dc, RECT r, COLORREF top_left, COLORREF bottom_right)
{
    fill(dc, &(RECT){r.left, r.top, r.right - 1, r.top + 1}, top_left);
    fill(dc, &(RECT){r.left, r.top, r.left + 1, r.bottom - 1}, top_left);
    fill(dc, &(RECT){r.left, r.bottom - 1, r.right, r.bottom}, bottom_right);
    fill(dc, &(RECT){r.right - 1, r.top, r.right, r.bottom}, bottom_right);
}

/*
 * an arrow of a scroll bar as RealDrawFrameControl of user32 26300 draws DFC_SCROLL: DrawPushButton, raised or a
 * flat frame when pushed, which leaves the rectangle two pixels smaller on each side, then DrawScrollArrow, the glyph
 * of marlett for the direction in a square of the smaller side of what is left (DrawIt). it moves a pixel down and right when pushed or inactive, takes the color of hot text when hot, and
 * an inactive one is the shadow color over a highlight one pixel down and right.
 */
static void draw_arrow(DC *dc, const RECT *rect, ULONG direction, ULONG state)
{
    static const WCHAR glyphs[] = L"5643";
    RECT r = *rect;
    fill(dc, &r, MetricsSysColor(COLOR_3DFACE));
    if (state == 1) {
        frame(dc, r, MetricsSysColor(COLOR_3DSHADOW), MetricsSysColor(COLOR_3DSHADOW));
    } else {
        frame(dc, r, MetricsSysColor(COLOR_3DLIGHT), MetricsSysColor(COLOR_3DDKSHADOW));
        frame(dc, (RECT){r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, MetricsSysColor(COLOR_3DHILIGHT),
              MetricsSysColor(COLOR_3DSHADOW));
    }
    r = (RECT){r.left + 2, r.top + 2, r.right - 2, r.bottom - 2};
    LONG width = r.right - r.left, height = r.bottom - r.top, side = W32Min(width, height);
    LONG x = r.left + (width - side) / 2, y = r.top + (height - side) / 2;
    if (state == 1 || state == 3)
        x++, y++;
    dc->State.TextColor = MetricsSysColor(state == 2 ? COLOR_HOTLIGHT : state == 3 ? COLOR_3DHILIGHT : COLOR_BTNTEXT);
    TextDraw(dc, x, y, 0, nullptr, &glyphs[direction], 1, nullptr);
    if (state == 3) {
        dc->State.TextColor = MetricsSysColor(COLOR_3DSHADOW);
        TextDraw(dc, x - 1, y - 1, 0, nullptr, &glyphs[direction], 1, nullptr);
    }
}

/*
 * an arrow of a frame scroll bar in the classic look. xxxDrawSB2 and zzzDrawInvertScrollArea of win32kfull blit it
 * from the strip when the button has the size of one there and call DrawFrameControl otherwise, which draw the same.
 */
void OemDrawScrollArrow(SURFACE *s, const REGION *clip, const RECT *r, ULONG direction, ULONG state)
{
    LONG width = r->right - r->left, height = r->bottom - r->top;
    if (width <= 4 || height <= 4 || !FontFamilyInstalled(L"Marlett"))
        return;
    SURFACE *surface = SurfaceCreate(width, height, 32, 0);
    DC *dc = surface ? DcCreate(DC_MEMORY) : nullptr;
    LOGFONTW lf = {.lfHeight = W32Min(width, height) - 4, .lfWeight = FW_NORMAL, .lfCharSet = SYMBOL_CHARSET,
                   .lfFaceName = L"Marlett"};
    FONT *font = dc ? GdiCreateFont(&lf) : nullptr;
    if (font) {
        DcSetSurface(dc, surface);
        dc->Dpi = 96;
        DcSelectFont(dc, font);
        dc->State.BkMode = TRANSPARENT;
        draw_arrow(dc, &(RECT){0, 0, width, height}, direction, state);
        DcSelectFont(dc, (FONT *)GdiStockObject(SYSTEM_FONT));
        GdiDeleteObject(&font->Header);
        BltSurface(s, clip, r->left, r->top, width, height, surface, 0, 0, SRCCOPY, nullptr);
    }
    if (dc)
        DcFree(dc);
    SurfaceDestroy(surface);
}

/*
 * UpdateFrameControlMetricsSize: the size of each bitmap at a dpi, from the metrics of that dpi. a menu one with
 * the 8 or 16 state is the height of a menu item, the menu font with its leading and two more.
 */
static SIZE bitmap_size(ULONG index, UINT dpi)
{
    USHORT type = g_table[index][0], state = g_table[index][1];
    switch (type) {
    case DFC_CAPTION:
        if (state & 0x80)
            return (SIZE){MetricsGet(SM_CXSMSIZE, dpi) - 2, MetricsGet(SM_CYSMSIZE, dpi) - 4};
        if (state & 0x40)
            return (SIZE){MetricsGet(SM_CXMENUSIZE, dpi) - 2, MetricsGet(SM_CYMENUSIZE, dpi) - 4};
        return (SIZE){MetricsGet(SM_CXSIZE, dpi) - 2, MetricsGet(SM_CYSIZE, dpi) - 4};
    case DFC_MENU:
    case DFC_POPUPMENU: {
        LONG side = MetricsGet(SM_CYMENUCHECK, dpi);
        if (state & 0x18) {
            LOGFONTW lf = W32MessageFontForDpi(dpi);
            FONT_INSTANCE *font = FontRealize(&lf, dpi);
            side = font ? font->Metrics.tmHeight + font->Metrics.tmExternalLeading + 2 : 0;
        }
        return (SIZE){side, side};
    }
    case DFC_SCROLL:
        if (state & 8)
            return (SIZE){MetricsGet(SM_CXVSCROLL, dpi), MetricsGet(SM_CYHSCROLL, dpi)};
        if (state & 2)
            return (SIZE){MetricsGet(SM_CXHSCROLL, dpi), MetricsGet(SM_CYHSCROLL, dpi)};
        return (SIZE){MetricsGet(SM_CXVSCROLL, dpi), MetricsGet(SM_CYVSCROLL, dpi)};
    case DFC_BUTTON: {
        /* the three state box that is not checked has no bitmap. */
        LONG side = (state & (DFCS_BUTTON3STATE | DFCS_CHECKED)) == DFCS_BUTTON3STATE ? 0 : (LONG)(dpi >> 3) + 1;
        return (SIZE){side, side};
    }
    case DFC_CAPTIONBAR:
        if (state & 1)
            return (SIZE){4 * MetricsGet(SM_CXSIZE, dpi), MetricsGet(SM_CYSIZE, dpi)};
        return (SIZE){MetricsGet(SM_CYSIZE, dpi), MetricsGet(SM_CYSIZE, dpi)};
    }
    return (SIZE){0, 0};
}

/* ArrangeFrameControls: rows as wide as the limit, a bitmap that does not fit starts the next. */
static LONG arrange(const SIZE *sizes, LONG limit, RECT *rects)
{
    LONG x = 0, y = 0, row = 0;
    for (ULONG i = 0; i < OEM_BITMAPS; i++) {
        if (!sizes[i].cx)
            continue;
        if (x + sizes[i].cx > limit) {
            y += row;
            x = row = 0;
        }
        if (rects)
            rects[i] = (RECT){x, y, x + sizes[i].cx, y + sizes[i].cy};
        x += sizes[i].cx;
        row = W32Max(row, sizes[i].cy);
    }
    return y + row;
}

/*
 * EnsureOemBitmapInfoForDpiSlot: the strip is one to five times as wide as the widest bitmap, whichever gives the
 * smallest area, the narrowest of equal ones.
 */
static SIZE layout(UINT dpi, RECT *rects)
{
    SIZE sizes[OEM_BITMAPS];
    LONG widest = 0;
    for (ULONG i = 0; i < OEM_BITMAPS; i++) {
        sizes[i] = bitmap_size(i, dpi);
        widest = W32Max(widest, sizes[i].cx);
    }
    LONG best = 1, area = 0x7FFFFFFF;
    for (LONG k = 1; k <= 5; k++) {
        LONG height = arrange(sizes, widest * k, nullptr);
        if (k * height < area) {
            area = k * height;
            best = k;
        }
    }
    memset(rects, 0, sizeof(RECT) * OEM_BITMAPS);
    return (SIZE){widest * best, arrange(sizes, widest * best, rects)};
}

static BOOLEAN drawn(ULONG index)
{
    return (index >= OEM_ARROW_FIRST && index <= OEM_ARROW_LAST) ||
           (index >= OEM_BUTTON_FIRST && index <= OEM_BUTTON_LAST);
}

static BOOLEAN build(OEM_STRIP *strip, UINT dpi)
{
    SIZE extent = layout(dpi, strip->Rects);
    LONG box = (LONG)(dpi >> 3) + 1;
    RECT *up = &strip->Rects[OEM_ARROW_FIRST];
    SURFACE *surface = extent.cx > 0 && extent.cy > 0 ? SurfaceCreate(extent.cx, extent.cy, 32, 0) : nullptr;
    DC *dc = surface ? DcCreate(DC_MEMORY) : nullptr;
    LOGFONTW lf = {.lfHeight = box, .lfWeight = FW_NORMAL, .lfCharSet = SYMBOL_CHARSET, .lfFaceName = L"Marlett"};
    FONT *font = dc ? GdiCreateFont(&lf) : nullptr;
    lf.lfHeight = W32Min(up->right - up->left, up->bottom - up->top) - 4;
    FONT *arrow_font = font ? GdiCreateFont(&lf) : nullptr;
    if (!arrow_font) {
        if (font)
            GdiDeleteObject(&font->Header);
        if (dc)
            DcFree(dc);
        SurfaceDestroy(surface);
        return FALSE;
    }
    DcSetSurface(dc, surface);
    dc->Dpi = 96;
    dc->State.BkMode = TRANSPARENT;
    for (ULONG i = 0; i < OEM_BITMAPS; i++) {
        RECT *rect = &strip->Rects[i];
        if (!drawn(i) || RectEmpty(rect)) {
            *rect = (RECT){0, 0, 0, 0};
            continue;
        }
        if (g_table[i][0] == DFC_BUTTON) {
            DcSelectFont(dc, font);
            draw_box(dc, rect, g_table[i][1]);
        } else {
            DcSelectFont(dc, arrow_font);
            draw_arrow(dc, rect, (i - OEM_ARROW_FIRST) / 4, (i - OEM_ARROW_FIRST) % 4);
        }
    }
    DcSelectFont(dc, (FONT *)GdiStockObject(SYSTEM_FONT));
    GdiDeleteObject(&font->Header);
    GdiDeleteObject(&arrow_font->Header);
    DcFree(dc);
    strip->Dpi = dpi;
    strip->Surface = surface;
    return TRUE;
}

/* the strip of the dpi of the calling thread, drawn the first time it is asked for. */
static OEM_STRIP *current_strip(void)
{
    UINT dpi = DpiScaleOf(DpiCurrentAwareness());
    OEM_STRIP *free_slot = nullptr;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_strips); i++) {
        if (g_strips[i].Surface && g_strips[i].Dpi == dpi)
            return &g_strips[i];
        if (!g_strips[i].Surface && !free_slot)
            free_slot = &g_strips[i];
    }
    if (!free_slot) {
        free_slot = &g_strips[0];
        SurfaceDestroy(free_slot->Surface);
        free_slot->Surface = nullptr;
    }
    /* without marlett the strip would stay empty for good, so it waits for the font. */
    if (!FontFamilyInstalled(L"Marlett"))
        return nullptr;
    return build(free_slot, dpi) ? free_slot : nullptr;
}

/* the colours of the system changed: the strips are drawn again when they are next asked for. */
void OemBitmapsInvalidate(void)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_strips); i++) {
        if (g_strips[i].Surface)
            SurfaceDestroy(g_strips[i].Surface);
        g_strips[i].Surface = nullptr;
    }
}

BOOL NTAPI NtUserGetOemBitmapSize(UINT index, SIZE *size)
{
    if (index >= OEM_BITMAPS) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    SIZE answer = drawn(index) ? bitmap_size(index, DpiScaleOf(DpiCurrentAwareness())) : (SIZE){0, 0};
    UserLeave();
    if (!NT_SUCCESS(MimeCopyToUser(size, &answer, sizeof(answer))))
        return FALSE;
    return answer.cx != 0;
}

BOOL NTAPI NtUserBitBltSysBmp(HDC hdc, int x, int y, UINT index, DWORD rop)
{
    if (index >= OEM_BITMAPS) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    BOOL ok = FALSE;
    OEM_STRIP *strip = drawn(index) ? current_strip() : nullptr;
    const RECT *r = strip ? &strip->Rects[index] : nullptr;
    if (r && !RectEmpty(r))
        ok = GdiBltFromSurface(hdc, x, y, r->right - r->left, r->bottom - r->top, strip->Surface, r->left, r->top, rop);
    UserLeave();
    return ok;
}
