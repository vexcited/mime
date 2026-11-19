#include "win32k.h"

/*
 * the strip of the system bitmaps, which user32 draws check boxes and radio buttons from: NtUserGetOemBitmapSize
 * gives the size of one and NtUserBitBltSysBmp copies it. win32kfull 26300 has 93 of them and draws each with
 * DrawFrameControl when the metrics change, see CreateGlobalDpiBitmapStrip and its table of types and states. the
 * ones of the buttons, 71 to 86, are here. the caption, scroll bar and menu ones have no size yet.
 */

#define OEM_BITMAPS 93
#define OEM_BUTTON_FIRST 71
#define OEM_BUTTON_LAST 86

#define DFCS_BUTTONRADIOIMAGE 0x0001
#define DFCS_BUTTONRADIOMASK 0x0002
#define DFCS_BUTTONRADIO 0x0004
#define DFCS_BUTTON3STATE 0x0008
#define DFCS_INACTIVE 0x0100
#define DFCS_PUSHED 0x0200
#define DFCS_CHECKED 0x0400

/* the states of the table of win32kfull, from the mask of the radio button on. */
static const USHORT g_states[OEM_BUTTON_LAST - OEM_BUTTON_FIRST + 1] = {
    0x002, 0x000, 0x400, 0x200, 0x600, 0x500, 0x001, 0x401, 0x201, 0x601, 0x501, 0x008, 0x408, 0x208, 0x608, 0x508,
};

typedef struct OEM_STRIP {
    UINT Dpi;
    LONG Size;
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
    LONG x = rect->left, y = rect->top;
    if (dithered && MetricsSysColor(COLOR_3DHILIGHT) == RGB(255, 255, 255)) {
        /* the gray brush of windows, one pixel of the face and one of the highlight in turn. */
        for (LONG row = rect->top; row < rect->bottom; row++)
            for (LONG column = rect->left; column < rect->right; column++)
                fill(dc, &(RECT){column, row, column + 1, row + 1},
                     MetricsSysColor(((row - rect->top) ^ (column - rect->left)) & 1 ? COLOR_3DHILIGHT : COLOR_3DFACE));
    } else {
        dc->State.TextColor = colors[0];
        TextDraw(dc, x, y, 0, nullptr, glyphs, 1, nullptr);
    }
    for (ULONG i = 1; i < 5; i++) {
        dc->State.TextColor = colors[i];
        TextDraw(dc, x, y, 0, nullptr, glyphs + i, 1, nullptr);
    }
    if (state & DFCS_CHECKED) {
        dc->State.TextColor = state & DFCS_BUTTONRADIOMASK ? 0
                                                           : MetricsSysColor(state & (DFCS_BUTTON3STATE | DFCS_INACTIVE)
                                                                                 ? COLOR_3DSHADOW
                                                                                 : COLOR_WINDOWTEXT);
        TextDraw(dc, x, y, 0, nullptr, glyphs + 5, 1, nullptr);
    }
}

static BOOLEAN build(OEM_STRIP *strip, UINT dpi)
{
    /* UpdateFrameControlMetricsSize: a button bitmap is an eighth of the dpi and one more, 13 at 96. */
    LONG size = (LONG)(dpi >> 3) + 1;
    ULONG count = RTL_NUMBER_OF(g_states);
    SURFACE *surface = SurfaceCreate(size * (LONG)count, size, 32, 0);
    DC *dc = surface ? DcCreate(DC_MEMORY) : nullptr;
    LOGFONTW lf = {.lfHeight = size, .lfWeight = FW_NORMAL, .lfCharSet = SYMBOL_CHARSET, .lfFaceName = L"Marlett"};
    FONT *font = dc ? GdiCreateFont(&lf) : nullptr;
    if (!font) {
        if (dc)
            DcFree(dc);
        if (surface)
            SurfaceDestroy(surface);
        return FALSE;
    }
    DcSetSurface(dc, surface);
    dc->Dpi = 96;
    DcSelectFont(dc, font);
    dc->State.BkMode = TRANSPARENT;
    for (ULONG i = 0; i < count; i++) {
        RECT rect = {(LONG)i * size, 0, (LONG)(i + 1) * size, size};
        /* the bitmap of the three state box that is not checked has no size in windows either. */
        if ((g_states[i] & (DFCS_BUTTON3STATE | DFCS_CHECKED)) != DFCS_BUTTON3STATE)
            draw_box(dc, &rect, g_states[i]);
    }
    DcSelectFont(dc, (FONT *)GdiStockObject(SYSTEM_FONT));
    GdiDeleteObject(&font->Header);
    DcFree(dc);
    strip->Dpi = dpi;
    strip->Size = size;
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

static BOOLEAN has_size(UINT index)
{
    return index >= OEM_BUTTON_FIRST && index <= OEM_BUTTON_LAST &&
           (g_states[index - OEM_BUTTON_FIRST] & (DFCS_BUTTON3STATE | DFCS_CHECKED)) != DFCS_BUTTON3STATE;
}

BOOL NTAPI NtUserGetOemBitmapSize(UINT index, SIZE *size)
{
    if (index >= OEM_BITMAPS) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    LONG side = has_size(index) ? (LONG)(DpiScaleOf(DpiCurrentAwareness()) >> 3) + 1 : 0;
    UserLeave();
    SIZE answer = {side, side};
    if (!NT_SUCCESS(MimeCopyToUser(size, &answer, sizeof(answer))))
        return FALSE;
    return side != 0;
}

BOOL NTAPI NtUserBitBltSysBmp(HDC hdc, int x, int y, UINT index, DWORD rop)
{
    if (index >= OEM_BITMAPS) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    BOOL ok = FALSE;
    OEM_STRIP *strip = has_size(index) ? current_strip() : nullptr;
    if (strip)
        ok = GdiBltFromSurface(hdc, x, y, strip->Size, strip->Size, strip->Surface,
                               (int)(index - OEM_BUTTON_FIRST) * strip->Size, 0, rop);
    UserLeave();
    return ok;
}
