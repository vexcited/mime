/*
 * drawing pictures. Bitmaps and icons are blitted from the shared context, which is set up so that the himetric
 * rectangle of the picture maps onto its pixels with the y axis up. Metafiles are played into the destination.
 */
#include "picture.h"

/* the device contexts a picture may be drawn into: DC, metafile DC, memory DC and enhanced metafile DC. */
#define DC_KINDS_ALLOWED ((1u << OBJ_DC) | (1u << OBJ_METADC) | (1u << OBJ_METAFILE) | (1u << OBJ_MEMDC) | (1u << OBJ_ENHMETADC))
#define OBJECT_KIND_LAST 13

typedef struct HC_PARAMS {
    UINT cbSize;
    DWORD dwFlags;
    LPWSTR lpszDefaultScheme;
} HC_PARAMS;
#define HC_ON 0x1

/* ------------------------------------------------------------------ colors and mapping */

typedef struct DEST_STATE {
    COLORREF Background, Text;
    int Stretch;
} DEST_STATE;

static BOOL high_contrast(void)
{
    HC_PARAMS params = {sizeof(params)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(params), &params, 0) && (params.dwFlags & HC_ON);
}

/* a one bit source turns into the colors of the window, or their swap on a display for people who need contrast. */
static void colors_set(HDC dc, DEST_STATE *state)
{
    COLORREF background = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT);
    if (high_contrast()) {
        COLORREF swap = background;
        background = text;
        text = swap;
    }
    state->Background = SetBkColor(dc, background);
    state->Text = SetTextColor(dc, text);
}

static void colors_restore(HDC dc, const DEST_STATE *state)
{
    SetBkColor(dc, state->Background);
    SetTextColor(dc, state->Text);
}

typedef struct MAPPING {
    int Mode;
    POINT WindowOrigin, ViewportOrigin;
    SIZE WindowExtent, ViewportExtent;
} MAPPING;

/* himetric units with y up on the rectangle of the picture, onto its pixels. */
static void map_set(HDC dc, const PIC_IMAGE *image, MAPPING *saved)
{
    saved->Mode = SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowOrgEx(dc, 0, 0, &saved->WindowOrigin);
    SetWindowExtEx(dc, image->Himetric.cx, image->Himetric.cy, &saved->WindowExtent);
    SetViewportOrgEx(dc, 0, image->Pixels.cy, &saved->ViewportOrigin);
    SetViewportExtEx(dc, image->Pixels.cx, -image->Pixels.cy, &saved->ViewportExtent);
}

static void map_restore(HDC dc, const MAPPING *saved)
{
    SetViewportExtEx(dc, saved->ViewportExtent.cx, saved->ViewportExtent.cy, nullptr);
    SetViewportOrgEx(dc, saved->ViewportOrigin.x, saved->ViewportOrigin.y, nullptr);
    SetWindowExtEx(dc, saved->WindowExtent.cx, saved->WindowExtent.cy, nullptr);
    SetWindowOrgEx(dc, saved->WindowOrigin.x, saved->WindowOrigin.y, nullptr);
    SetMapMode(dc, saved->Mode);
}

/* ------------------------------------------------------------------ stretching */

typedef struct BLIT {
    HDC Dest;
    LONG X, Y, Cx, Cy;
    LONG Xs, Ys, Cxs, Cys;
    SIZE Pixels;
} BLIT;

/* the same blit through the pixels of the bitmap, for destinations that cannot take a bitmap out of a context. */
static BOOL blit_by_pixels(const BLIT *b, HDC source, HBITMAP bitmap, DWORD rop)
{
    int dpi_x = PicDpiX(), dpi_y = PicDpiY();
    if (!dpi_x || !dpi_y || b->Pixels.cx <= 0 || b->Pixels.cy <= 0)
        return FALSE;
    SelectObject(source, PicBlank());
    ULONG stride = (ULONG)(((ULONGLONG)b->Pixels.cx * 24 + 31) / 32 * 4);
    BYTE *bits = AutAlloc((SIZE_T)stride * b->Pixels.cy);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = b->Pixels.cx;
    info.bmiHeader.biHeight = b->Pixels.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    BOOL ok = FALSE;
    if (bits && GetDIBits(source, bitmap, 0, b->Pixels.cy, bits, &info, DIB_RGB_COLORS)) {
        LONG x0 = b->Cxs < 0 ? b->Xs + b->Cxs : b->Xs;
        LONG y0 = b->Cys < 0 ? b->Ys + b->Cys : b->Ys;
        LONG wide = b->Cxs < 0 ? -b->Cxs : b->Cxs, high = b->Cys < 0 ? -b->Cys : b->Cys;
        LONG x = b->X, y = b->Y, cx = b->Cx, cy = b->Cy;
        /* the picture is upright when the source runs downward, and mirrored in the other directions. */
        if (b->Cys > 0) {
            y += cy;
            cy = -cy;
        }
        if (b->Cxs < 0) {
            x += cx;
            cx = -cx;
        }
        ok = StretchDIBits(b->Dest, x, y, cx, cy, MulDiv(x0, dpi_x, HIMETRIC_PER_INCH), MulDiv(y0, dpi_y, HIMETRIC_PER_INCH),
                           MulDiv(wide, dpi_x, HIMETRIC_PER_INCH), MulDiv(high, dpi_y, HIMETRIC_PER_INCH), bits, &info,
                           DIB_RGB_COLORS, rop) != 0;
    }
    AutFree(bits);
    SelectObject(source, bitmap);
    return ok;
}

static BOOL blit(const BLIT *b, HDC source, HBITMAP bitmap, DWORD rop)
{
    if (GetObjectType(b->Dest) != OBJ_ENHMETADC &&
        StretchBlt(b->Dest, b->X, b->Y, b->Cx, b->Cy, source, b->Xs, b->Ys, b->Cxs, b->Cys, rop))
        return TRUE;
    return blit_by_pixels(b, source, bitmap, rop);
}

/* ------------------------------------------------------------------ the bitmap in a context of the program */

/* the picture lets go of the context it was put in by SelectPicture. */
HRESULT PicTakeOut(PIC_IMAGE *image)
{
    if (!image->Home)
        return S_OK;
    HGDIOBJ found = SelectObject(image->Home, image->Replaced ? image->Replaced : PicBlank());
    if (found == image->Bitmap)
        return S_OK;
    if (found)
        SelectObject(image->Home, found);
    return E_FAIL;
}

HRESULT PicSelect(PIC_IMAGE *image, HDC in, HDC *out, HBITMAP *displaced)
{
    if (image->Kind != PICTYPE_BITMAP || !image->Bitmap)
        return E_FAIL;
    PicLock();
    HRESULT hr = PicTakeOut(image);
    if (FAILED(hr)) {
        PicUnlock();
        return E_FAIL;
    }
    HDC previous = image->Home;
    HBITMAP replaced = nullptr;
    if (in) {
        replaced = SelectObject(in, image->Bitmap);
        if (!replaced) {
            if (previous)
                SelectObject(previous, image->Bitmap);
            PicUnlock();
            return E_FAIL;
        }
    }
    image->Home = in;
    image->Replaced = replaced;
    PicUnlock();
    if (out)
        *out = previous;
    if (displaced)
        *displaced = replaced;
    return S_OK;
}

/* ------------------------------------------------------------------ the kinds */

typedef struct VIEW {
    HDC Dest;
    LONG X, Y, Cx, Cy;
    LONG Xs, Ys, Cxs, Cys;
} VIEW;

static BLIT blit_of(const VIEW *v, const PIC_IMAGE *image)
{
    BLIT b = {v->Dest, v->X, v->Y, v->Cx, v->Cy, v->Xs, v->Ys, v->Cxs, v->Cys, image->Pixels};
    return b;
}

static BOOL is_section(HBITMAP bitmap)
{
    DIBSECTION section;
    return GetObjectW(bitmap, sizeof(section), &section) == sizeof(section);
}

static HRESULT draw_bitmap(PIC_IMAGE *image, const VIEW *view)
{
    PicLock();
    HDC shared = PicDc();
    if (!shared) {
        PicUnlock();
        return E_OUTOFMEMORY;
    }
    BOOL withdrawn = image->Home && SUCCEEDED(PicTakeOut(image));
    MAPPING map;
    map_set(shared, image, &map);
    HPALETTE old_palette = nullptr;
    if (image->Palette && !is_section(image->Bitmap)) {
        old_palette = SelectPalette(shared, image->Palette, FALSE);
        RealizePalette(shared);
    }
    DEST_STATE dest;
    dest.Stretch = SetStretchBltMode(view->Dest, COLORONCOLOR);
    colors_set(view->Dest, &dest);

    SelectObject(shared, image->Bitmap);
    BLIT b = blit_of(view, image);
    if (!image->Mask) {
        blit(&b, shared, image->Bitmap, SRCCOPY);
    } else {
        blit(&b, shared, image->Bitmap, SRCINVERT);
        SelectObject(shared, image->Mask);
        blit(&b, shared, image->Mask, SRCAND);
        SelectObject(shared, image->Bitmap);
        blit(&b, shared, image->Bitmap, SRCINVERT);
    }

    SelectObject(shared, PicBlank());
    if (withdrawn)
        SelectObject(image->Home, image->Bitmap);
    if (old_palette)
        SelectPalette(shared, old_palette, FALSE);
    map_restore(shared, &map);
    PicUnlock();
    colors_restore(view->Dest, &dest);
    SetStretchBltMode(view->Dest, dest.Stretch);
    return S_OK;
}

static HRESULT draw_icon(PIC_IMAGE *image, const VIEW *view)
{
    PicLock();
    HDC shared = PicDc();
    if (!shared) {
        PicUnlock();
        return E_OUTOFMEMORY;
    }
    MAPPING map;
    map_set(shared, image, &map);
    DEST_STATE dest;
    colors_set(view->Dest, &dest);
    BLIT b = blit_of(view, image);
    SelectObject(shared, image->IconAnd);
    blit(&b, shared, image->IconAnd, SRCAND);
    SelectObject(shared, image->IconXor);
    blit(&b, shared, image->IconXor, SRCINVERT);
    map_restore(shared, &map);
    SelectObject(shared, PicBlank());
    PicUnlock();
    colors_restore(view->Dest, &dest);
    return S_OK;
}

static int CALLBACK records_play(HDC dc, HANDLETABLE *table, METARECORD *record, int objects, LPARAM param)
{
    PlayMetaFileRecord(dc, table, record, objects);
    return 1;
}

static HRESULT draw_metafile(PIC_IMAGE *image, const VIEW *view, const RECT *bounds)
{
    HDC dc = view->Dest;
    if (GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE && !bounds)
        return E_POINTER;
    int saved = SaveDC(dc);
    if (!saved)
        return E_OUTOFMEMORY;
    IntersectClipRect(dc, view->X, view->Y, view->X + view->Cx, view->Y + view->Cy);
    SetMapMode(dc, MM_ANISOTROPIC);
    POINT origin = {};
    GetWindowOrgEx(dc, &origin);
    LONG full_x = (LONG)OleStdRoundDiv((LONGLONG)image->Himetric.cx * view->Cx, view->Cxs);
    LONG full_y = (LONG)OleStdRoundDiv((LONGLONG)image->Himetric.cy * view->Cy, view->Cys);
    LONG left = view->X - origin.x - (LONG)OleStdRoundDiv((LONGLONG)view->Xs * view->Cx, view->Cxs);
    LONG top = full_y + view->Y - origin.y - (LONG)OleStdRoundDiv((LONGLONG)view->Ys * view->Cy, view->Cys);
    if (image->Kind == PICTYPE_METAFILE) {
        OffsetViewportOrgEx(dc, left, top, nullptr);
        SetViewportExtEx(dc, full_x, -full_y, nullptr);
        EnumMetaFile(dc, image->Wmf, records_play, 0);
    } else {
        RECT target = {left, top, left + full_x, top - full_y};
        PlayEnhMetaFile(dc, image->Emf, &target);
    }
    RestoreDC(dc, saved);
    return S_OK;
}

HRESULT PicRender(PIC_IMAGE *image, HDC dc, LONG x, LONG y, LONG cx, LONG cy, LONG xs, LONG ys, LONG cxs, LONG cys,
                  const RECT *bounds)
{
    if (!cx || !cy || !cxs || !cys)
        return CTL_E_INVALIDPROPERTYVALUE;
    UINT kind = GetObjectType(dc);
    if (kind > OBJECT_KIND_LAST || !((DC_KINDS_ALLOWED >> kind) & 1))
        return E_INVALIDARG;
    VIEW view = {dc, x, y, cx, cy, xs, ys, cxs, cys};
    switch (image->Kind) {
    case PIC_UNSET:
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: return draw_bitmap(image, &view);
    case PICTYPE_ICON: return draw_icon(image, &view);
    case PICTYPE_METAFILE:
    case PICTYPE_ENHMETAFILE: return draw_metafile(image, &view, bounds);
    default: return E_FAIL;
    }
}
