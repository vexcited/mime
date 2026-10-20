/*
 * the standard picture object of OleCreatePictureIndirect and OleLoadPicture: bitmaps, metafiles, icons and enhanced
 * metafiles with their sizes in himetric, drawing, and the stream and file formats they are saved in.
 */
#include "picture.h"

typedef struct PICTURE {
    IPicture Pic;
    IPicture2 Pic2;
    IDispatch Disp;
    IPersistStream Persist;
    LONG Refs;
    PICSTATE S;
    HDC CurDc;
    HGDIOBJ CurOld;
    BOOL Own, OwnGdi, OwnPal, Keep;
    BYTE *Original;
    ULONG OriginalLength;
} PICTURE;

#define PIC_FROM_PIC2(p) CONTAINING_RECORD(p, PICTURE, Pic2)
#define PIC_FROM_DISP(p) CONTAINING_RECORD(p, PICTURE, Disp)
#define PIC_FROM_PERSIST(p) CONTAINING_RECORD(p, PICTURE, Persist)

typedef struct {
    UINT cbSize;
    DWORD dwFlags;
    LPWSTR lpszDefaultScheme;
} HIGH_CONTRAST;

/* ------------------------------------------------------------------ helpers */

/* a picture nothing was loaded into is an empty one. */
static void pic_ready(PICTURE *p)
{
    if (p->S.type == PIC_UNSET)
        p->S.type = PICTYPE_NONE;
}

static HANDLE pic_handle(const PICTURE *p)
{
    switch (p->S.type) {
    case PICTYPE_BITMAP: return p->S.bitmap;
    case PICTYPE_METAFILE: return p->S.wmf;
    case PICTYPE_ICON: return p->S.icon;
    case PICTYPE_ENHMETAFILE: return p->S.emf;
    default: return nullptr;
    }
}

/* the bitmap of the picture comes out of the context it was selected into, and what the context had comes back. */
static BOOL pic_take_out(PICTURE *p)
{
    if (!p->CurDc)
        return TRUE;
    HGDIOBJ found = SelectObject(p->CurDc, p->CurOld ? p->CurOld : (HGDIOBJ)PicStock());
    if (found != p->S.bitmap) {
        if (found)
            SelectObject(p->CurDc, found);
        return FALSE;
    }
    p->CurDc = nullptr;
    p->CurOld = nullptr;
    return TRUE;
}

static void pic_deselect(PICTURE *p)
{
    if (!pic_take_out(p)) {
        p->CurDc = nullptr;
        p->CurOld = nullptr;
    }
}

static void pic_release_state(PICTURE *p)
{
    pic_deselect(p);
    if (!p->OwnPal)
        p->S.palette = nullptr;
    PicStateFree(&p->S, p->OwnGdi, p->Own);
}

static BOOL pic_high_contrast(void)
{
    HIGH_CONTRAST hc = {sizeof(hc), 0, nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, 0, &hc, 0) && (hc.dwFlags & 1);
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE pic_query(IPicture *This, REFIID riid, void **object)
{
    PICTURE *p = (PICTURE *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!riid)
        return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPicture))
        *object = &p->Pic;
    else if (IsEqualIID(riid, &IID_IPicture2))
        *object = &p->Pic2;
    else if (IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IPictureDisp))
        *object = &p->Disp;
    else if (IsEqualIID(riid, &IID_IPersistStream))
        *object = &p->Persist;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&p->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE pic_addref(IPicture *This)
{
    return InterlockedIncrement(&((PICTURE *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE pic_release(IPicture *This)
{
    PICTURE *p = (PICTURE *)This;
    LONG refs = InterlockedDecrement(&p->Refs);
    if (refs)
        return refs;
    pic_release_state(p);
    AutFree(p->Original);
    AutFree(p);
    return 0;
}

/* ------------------------------------------------------------------ properties */

static HRESULT STDMETHODCALLTYPE pic2_get_handle(IPicture2 *This, HHANDLE *handle)
{
    PICTURE *p = PIC_FROM_PIC2(This);
    if (!handle)
        return E_POINTER;
    pic_ready(p);
    if (p->S.type > PICTYPE_ENHMETAFILE) {
        *handle = 0;
        return E_INVALIDARG;
    }
    *handle = (HHANDLE)pic_handle(p);
    return S_OK;
}

/* a handle in the 32 bits automation has room for, and false when it does not fit. */
static HRESULT pic_narrow(HHANDLE wide, OLE_HANDLE *narrow)
{
    *narrow = (OLE_HANDLE)wide;
    return (HHANDLE)(OLE_HANDLE)wide == wide || (HHANDLE)(LONG_PTR)(LONG)wide == wide ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE pic_get_handle(IPicture *This, OLE_HANDLE *handle)
{
    PICTURE *p = (PICTURE *)This;
    if (!handle)
        return E_POINTER;
    HHANDLE wide = 0;
    HRESULT hr = pic2_get_handle(&p->Pic2, &wide);
    if (FAILED(hr)) {
        *handle = 0;
        return hr;
    }
    return pic_narrow(wide, handle);
}

static HRESULT STDMETHODCALLTYPE pic2_get_hpal(IPicture2 *This, HHANDLE *palette)
{
    PICTURE *p = PIC_FROM_PIC2(This);
    if (!palette)
        return E_POINTER;
    pic_ready(p);
    if (p->S.type != PICTYPE_BITMAP)
        return E_FAIL;
    *palette = (HHANDLE)p->S.palette;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_hpal(IPicture *This, OLE_HANDLE *palette)
{
    PICTURE *p = (PICTURE *)This;
    if (!palette)
        return E_POINTER;
    HHANDLE wide = 0;
    HRESULT hr = pic2_get_hpal(&p->Pic2, &wide);
    if (FAILED(hr)) {
        *palette = 0;
        return hr;
    }
    return pic_narrow(wide, palette);
}

static HRESULT STDMETHODCALLTYPE pic2_set_hpal(IPicture2 *This, HHANDLE palette)
{
    PICTURE *p = PIC_FROM_PIC2(This);
    pic_ready(p);
    if (p->S.type != PICTYPE_BITMAP)
        return E_FAIL;
    HPALETTE next = (HPALETTE)palette;
    if (p->OwnGdi && p->Own) {
        /* the palette the picture owned until now goes with the one it gets. */
        if (p->OwnPal && p->S.palette && p->S.palette != next)
            DeleteObject(p->S.palette);
        p->OwnPal = TRUE;
    }
    p->S.palette = next;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_set_hpal(IPicture *This, OLE_HANDLE palette)
{
    PICTURE *p = (PICTURE *)This;
    return pic2_set_hpal(&p->Pic2, (HHANDLE)(LONG_PTR)(LONG)palette);
}

static HRESULT STDMETHODCALLTYPE pic_get_type(IPicture *This, SHORT *type)
{
    PICTURE *p = (PICTURE *)This;
    if (!type)
        return E_POINTER;
    pic_ready(p);
    *type = (SHORT)p->S.type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_width(IPicture *This, OLE_XSIZE_HIMETRIC *width)
{
    PICTURE *p = (PICTURE *)This;
    if (!width)
        return E_POINTER;
    pic_ready(p);
    *width = p->S.himetric.cx;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_height(IPicture *This, OLE_YSIZE_HIMETRIC *height)
{
    PICTURE *p = (PICTURE *)This;
    if (!height)
        return E_POINTER;
    pic_ready(p);
    *height = p->S.himetric.cy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_curdc(IPicture *This, HDC *hdc)
{
    PICTURE *p = (PICTURE *)This;
    if (!hdc)
        return E_POINTER;
    *hdc = nullptr;
    pic_ready(p);
    if (p->S.type != PICTYPE_BITMAP)
        return E_FAIL;
    *hdc = p->CurDc;
    return S_OK;
}

/*
 * puts the bitmap into a context. A bitmap that is in another one comes out of it first; that context and the bitmap
 * the new one had are given back.
 */
static HRESULT pic_select(PICTURE *p, HDC in, HDC *out, HHANDLE *previous)
{
    pic_ready(p);
    if (p->S.type != PICTYPE_BITMAP || !p->S.bitmap)
        return E_FAIL;
    HDC before = p->CurDc;
    if (!pic_take_out(p))
        return E_FAIL;
    if (in) {
        HGDIOBJ old = SelectObject(in, p->S.bitmap);
        if (previous)
            *previous = (HHANDLE)old;
        p->CurOld = old;
    }
    if (out)
        *out = before;
    p->CurDc = in;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic2_select_picture(IPicture2 *This, HDC in, HDC *out, HHANDLE *bitmap)
{
    return pic_select(PIC_FROM_PIC2(This), in, out, bitmap);
}

static HRESULT STDMETHODCALLTYPE pic_select_picture(IPicture *This, HDC in, HDC *out, OLE_HANDLE *bitmap)
{
    HHANDLE wide = 0;
    HRESULT hr = pic_select((PICTURE *)This, in, out, &wide);
    if (SUCCEEDED(hr) && bitmap)
        hr = pic_narrow(wide, bitmap);
    return hr;
}

static HRESULT STDMETHODCALLTYPE pic_get_keep(IPicture *This, BOOL *keep)
{
    if (!keep)
        return E_POINTER;
    *keep = ((PICTURE *)This)->Keep;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_put_keep(IPicture *This, BOOL keep)
{
    PICTURE *p = (PICTURE *)This;
    /* the original format cannot be asked for once a picture is made. */
    if (!p->Keep && keep)
        return E_FAIL;
    p->Keep = keep != 0;
    if (!keep) {
        AutFree(p->Original);
        p->Original = nullptr;
        p->OriginalLength = 0;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_changed(IPicture *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_attributes(IPicture *This, DWORD *attributes)
{
    PICTURE *p = (PICTURE *)This;
    if (!attributes)
        return E_POINTER;
    *attributes = 0;
    pic_ready(p);
    switch (p->S.type) {
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: *attributes = p->S.transparent ? PICTURE_TRANSPARENT : 0; return S_OK;
    case PICTYPE_METAFILE:
    case PICTYPE_ENHMETAFILE: *attributes = PICTURE_SCALABLE | PICTURE_TRANSPARENT; return S_OK;
    case PICTYPE_ICON: *attributes = PICTURE_TRANSPARENT; return S_OK;
    default: return E_FAIL;
    }
}

/* ------------------------------------------------------------------ drawing */

/*
 * maps the himetric of the picture, which counts up from its lower left corner, onto its pixels in the shared
 * context. The context's own settings come back from pic_unmap.
 */
typedef struct {
    int mode;
    POINT window_org, viewport_org;
    SIZE window_ext, viewport_ext;
} PIC_MAP;

static void pic_map(HDC dc, const SIZE *pixels, PIC_MAP *map)
{
    RECT himetric;
    PicRectToHimetric(pixels, &himetric);
    map->mode = SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowOrgEx(dc, himetric.left, himetric.bottom, &map->window_org);
    SetWindowExtEx(dc, himetric.right - himetric.left, himetric.top - himetric.bottom, &map->window_ext);
    SetViewportOrgEx(dc, 0, pixels->cy, &map->viewport_org);
    SetViewportExtEx(dc, pixels->cx, -pixels->cy, &map->viewport_ext);
}

static void pic_unmap(HDC dc, const PIC_MAP *map)
{
    SetMapMode(dc, map->mode);
    SetWindowOrgEx(dc, map->window_org.x, map->window_org.y, nullptr);
    SetWindowExtEx(dc, map->window_ext.cx, map->window_ext.cy, nullptr);
    SetViewportOrgEx(dc, map->viewport_org.x, map->viewport_org.y, nullptr);
    SetViewportExtEx(dc, map->viewport_ext.cx, map->viewport_ext.cy, nullptr);
}

/* the colors a one bit bitmap takes when it is blitted to a color context. */
typedef struct {
    COLORREF back, text;
} PIC_COLORS;

static void pic_set_colors(HDC dc, PIC_COLORS *saved)
{
    BOOL contrast = pic_high_contrast();
    saved->back = SetBkColor(dc, GetSysColor(contrast ? COLOR_WINDOWTEXT : COLOR_WINDOW));
    saved->text = SetTextColor(dc, GetSysColor(contrast ? COLOR_WINDOW : COLOR_WINDOWTEXT));
}

static void pic_restore_colors(HDC dc, const PIC_COLORS *saved)
{
    SetBkColor(dc, saved->back);
    SetTextColor(dc, saved->text);
}

/* a blit from the picture's context, through a device independent bitmap when the destination cannot take it. */
static BOOL pic_stretch(HDC dest, int x, int y, int cx, int cy, HDC src, int xs, int ys, int cxs, int cys, DWORD rop)
{
    if (GetObjectType(dest) != OBJ_ENHMETADC && StretchBlt(dest, x, y, cx, cy, src, xs, ys, cxs, cys, rop))
        return TRUE;
    HBITMAP stock = PicStock();
    HGDIOBJ bitmap = stock ? SelectObject(src, stock) : nullptr;
    if (!bitmap)
        return FALSE;
    BITMAP bm = {};
    BOOL ok = FALSE;
    if (GetObjectW(bitmap, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = bm.bmWidth;
        info.bmiHeader.biHeight = bm.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits = AutAlloc((SIZE_T)bm.bmWidth * bm.bmHeight * 4);
        if (bits) {
            if (GetDIBits(src, bitmap, 0, bm.bmHeight, bits, &info, DIB_RGB_COLORS)) {
                int dpi_x = PicDpiX(), dpi_y = PicDpiY();
                int px = MulDiv(xs, dpi_x, 2540), py = MulDiv(ys, dpi_y, 2540);
                int pcx = MulDiv(cxs, dpi_x, 2540), pcy = MulDiv(cys, dpi_y, 2540);
                if (pcy < 0)
                    pcy = -pcy;
                ok = StretchDIBits(dest, x, y, cx, cy, px, py - pcy, pcx, pcy, bits, &info, DIB_RGB_COLORS, rop) != 0;
            }
            AutFree(bits);
        }
    }
    SelectObject(src, bitmap);
    return ok;
}

static HRESULT pic_render_bitmap(PICTURE *p, HDC dc, int x, int y, int cx, int cy, int xs, int ys, int cxs, int cys)
{
    PicLock();
    HDC mem = PicDc();
    if (!mem) {
        PicUnlock();
        return E_OUTOFMEMORY;
    }
    PIC_MAP map;
    pic_map(mem, &p->S.pixels, &map);
    BOOL dib_section = FALSE;
    if (p->S.palette) {
        DIBSECTION ds;
        dib_section = GetObjectW(p->S.bitmap, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits;
    }
    HPALETTE old_palette = nullptr;
    if (p->S.palette && !dib_section) {
        old_palette = SelectPalette(mem, p->S.palette, TRUE);
        RealizePalette(mem);
    }
    int old_stretch = SetStretchBltMode(dc, COLORONCOLOR);
    PIC_COLORS colors;
    pic_set_colors(dc, &colors);
    HDC old_dc = nullptr;
    HRESULT selected = pic_select(p, mem, &old_dc, nullptr);
    if (!p->S.transparent || !p->S.mask) {
        pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCCOPY);
    } else {
        /* the transparent parts are those the mask holds ones for: the destination is XORed, ANDed and XORed again. */
        pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCINVERT);
        HGDIOBJ held = SelectObject(mem, p->S.mask);
        pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCAND);
        SelectObject(mem, held);
        pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCINVERT);
    }
    SetStretchBltMode(dc, old_stretch);
    /* the picture goes back where it was, and the shared context gets what it had. */
    if (SUCCEEDED(selected))
        pic_select(p, old_dc, nullptr, nullptr);
    pic_unmap(mem, &map);
    if (old_palette) {
        SelectPalette(mem, old_palette, FALSE);
        RealizePalette(mem);
    }
    PicUnlock();
    pic_restore_colors(dc, &colors);
    return S_OK;
}

static HRESULT pic_render_icon(PICTURE *p, HDC dc, int x, int y, int cx, int cy, int xs, int ys, int cxs, int cys)
{
    PicLock();
    HDC mem = PicDc();
    if (!mem) {
        PicUnlock();
        return E_OUTOFMEMORY;
    }
    PIC_MAP map;
    pic_map(mem, &p->S.pixels, &map);
    PIC_COLORS colors;
    pic_set_colors(dc, &colors);
    HGDIOBJ old = SelectObject(mem, p->S.and_bmp);
    pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCAND);
    SelectObject(mem, p->S.xor_bmp);
    pic_stretch(dc, x, y, cx, cy, mem, xs, ys, cxs, cys, SRCINVERT);
    pic_unmap(mem, &map);
    SelectObject(mem, old);
    PicUnlock();
    pic_restore_colors(dc, &colors);
    return S_OK;
}

static int CALLBACK pic_play_record(HDC dc, HANDLETABLE *table, METARECORD *record, int objects, LPARAM param)
{
    PlayMetaFileRecord(dc, table, record, objects);
    return 1;
}

/*
 * a metafile is scaled to the rectangle the source rectangle asks for, in the context's own units, then played with
 * the window of the metafile mapped onto it.
 */
static HRESULT pic_render_metafile(PICTURE *p, HDC dc, int x, int y, int cx, int cy, int xs, int ys, int cxs, int cys,
                                   const RECT *bounds)
{
    if (GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE)
        return bounds ? E_NOTIMPL : E_POINTER;
    int saved = SaveDC(dc);
    if (!saved)
        return E_OUTOFMEMORY;
    IntersectClipRect(dc, x, y, x + cx, y + cy);
    SetMapMode(dc, MM_ANISOTROPIC);
    POINT org = {};
    GetWindowOrgEx(dc, &org);
    int full_x = (int)AutRoundDiv64((LONGLONG)p->S.himetric.cx * cx, cxs);
    int full_y = (int)AutRoundDiv64((LONGLONG)p->S.himetric.cy * cy, cys);
    int left = x - org.x - (int)AutRoundDiv64((LONGLONG)xs * cx, cxs);
    int top = full_y + y - org.y - (int)AutRoundDiv64((LONGLONG)ys * cy, cys);
    RECT rect = {left, top, left + full_x, top - full_y};
    if (p->S.type == PICTYPE_METAFILE) {
        OffsetViewportOrgEx(dc, left, top, nullptr);
        SetViewportExtEx(dc, full_x, -full_y, nullptr);
        EnumMetaFile(dc, p->S.wmf, pic_play_record, (LPARAM)p);
    } else {
        PlayEnhMetaFile(dc, p->S.emf, &rect);
    }
    RestoreDC(dc, saved);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_render(IPicture *This, HDC hdc, LONG x, LONG y, LONG cx, LONG cy,
                                            OLE_XPOS_HIMETRIC xs, OLE_YPOS_HIMETRIC ys, OLE_XSIZE_HIMETRIC cxs,
                                            OLE_YSIZE_HIMETRIC cys, const RECT *bounds)
{
    PICTURE *p = (PICTURE *)This;
    if (!cx || !cy || !cxs || !cys)
        return CTL_E_INVALIDPROPERTYVALUE;
    pic_ready(p);
    DWORD type = GetObjectType(hdc);
    if (type > 13 || !((0x3618u >> type) & 1))
        return E_INVALIDARG;
    switch (p->S.type) {
    case PICTYPE_NONE: return S_OK;
    case PICTYPE_BITMAP: return pic_render_bitmap(p, hdc, x, y, cx, cy, xs, ys, cxs, cys);
    case PICTYPE_ICON: return pic_render_icon(p, hdc, x, y, cx, cy, xs, ys, cxs, cys);
    case PICTYPE_METAFILE:
    case PICTYPE_ENHMETAFILE: return pic_render_metafile(p, hdc, x, y, cx, cy, xs, ys, cxs, cys, bounds);
    default: return E_FAIL;
    }
}

/* ------------------------------------------------------------------ saving */

static void put16(BYTE *p, UINT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put32(BYTE *p, ULONG v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

/* the rows of a bitmap as 24 bit, or 8 bits or fewer with their table, bottom row first. */
typedef struct {
    int width, height, bpp, colors;
    RGBQUAD table[256];
    ULONG stride;
    BYTE *bits;
} PIC_DIB;

/* a bitmap read into a device independent form: from the bits of a section, or by asking the device. */
static HRESULT pic_read_bitmap(HBITMAP bitmap, PIC_DIB *dib, BOOL force_mono)
{
    DIBSECTION ds = {};
    memset(dib, 0, sizeof(*dib));
    int got = GetObjectW(bitmap, sizeof(ds), &ds);
    if (got < (int)sizeof(BITMAP))
        return E_FAIL;
    dib->width = ds.dsBm.bmWidth;
    dib->height = ds.dsBm.bmHeight;
    if (dib->width <= 0 || dib->height <= 0)
        return E_FAIL;
    int depth = ds.dsBm.bmBitsPixel * ds.dsBm.bmPlanes;
    BOOL section = got == (int)sizeof(DIBSECTION) && ds.dsBm.bmBits;
    if (section)
        depth = ds.dsBmih.biBitCount;
    dib->bpp = (depth <= 8 || force_mono) ? (force_mono ? 1 : depth) : 24;
    if (dib->bpp == 2 || dib->bpp == 3)
        dib->bpp = 4;
    if (dib->bpp > 4 && dib->bpp < 8)
        dib->bpp = 8;
    dib->stride = (ULONG)(((ULONGLONG)dib->width * dib->bpp + 31) / 32 * 4);
    dib->bits = AutAllocZero((SIZE_T)dib->stride * dib->height);
    if (!dib->bits)
        return E_OUTOFMEMORY;
    PicLock();
    HDC dc = PicDc();
    HRESULT hr = E_FAIL;
    if (dc) {
        HBITMAP stock = PicStock();
        HGDIOBJ old = SelectObject(dc, stock);
        if (dib->bpp <= 8 && section) {
            SelectObject(dc, bitmap);
            dib->colors = (int)GetDIBColorTable(dc, 0, 256, dib->table);
            if (dib->colors > (1 << dib->bpp))
                dib->colors = 1 << dib->bpp;
            SelectObject(dc, stock);
        }
        SelectObject(dc, old);
        hr = S_OK;
    }
    if (SUCCEEDED(hr) && section) {
        const BYTE *src = ds.dsBm.bmBits;
        BOOL bottom_up = ds.dsBmih.biHeight > 0;
        int src_bpp = depth;
        /* the width of a row is worked out, as the one the bitmap reports is negative for rows that run upwards. */
        SIZE_T src_stride = (SIZE_T)(((ULONGLONG)dib->width * (ULONG)depth + 31) / 32 * 4);
        for (int row = 0; row < dib->height; row++) {
            const BYTE *in = src + (SIZE_T)(bottom_up ? row : dib->height - 1 - row) * src_stride;
            BYTE *out = dib->bits + (SIZE_T)row * dib->stride;
            if (dib->bpp <= 8 || src_bpp == 24) {
                memcpy(out, in, (SIZE_T)((ULONGLONG)dib->width * dib->bpp + 7) / 8);
            } else if (src_bpp == 32) {
                for (int x = 0; x < dib->width; x++) {
                    out[3 * x] = in[4 * x];
                    out[3 * x + 1] = in[4 * x + 1];
                    out[3 * x + 2] = in[4 * x + 2];
                }
            } else if (src_bpp == 16) {
                for (int x = 0; x < dib->width; x++) {
                    UINT v = in[2 * x] | in[2 * x + 1] << 8;
                    out[3 * x] = (BYTE)((v & 31) * 255 / 31);
                    out[3 * x + 1] = (BYTE)(((v >> 5) & 31) * 255 / 31);
                    out[3 * x + 2] = (BYTE)(((v >> 10) & 31) * 255 / 31);
                }
            }
        }
    } else if (SUCCEEDED(hr)) {
        /* a device bitmap is converted by the device, once it is out of the shared context. */
        struct {
            BITMAPINFOHEADER header;
            RGBQUAD colors[256];
        } info;
        memset(&info, 0, sizeof(info));
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = dib->width;
        info.header.biHeight = dib->height;
        info.header.biPlanes = 1;
        info.header.biBitCount = (WORD)dib->bpp;
        HDC screen = GetDC(nullptr);
        int lines = screen ? GetDIBits(screen, bitmap, 0, dib->height, dib->bits, (BITMAPINFO *)&info, DIB_RGB_COLORS) : 0;
        if (screen)
            ReleaseDC(nullptr, screen);
        if (!lines)
            hr = E_OUTOFMEMORY;
        if (dib->bpp <= 8) {
            dib->colors = 1 << dib->bpp;
            memcpy(dib->table, info.colors, (SIZE_T)dib->colors * sizeof(RGBQUAD));
        }
    }
    PicUnlock();
    if (FAILED(hr)) {
        AutFree(dib->bits);
        dib->bits = nullptr;
    }
    return hr;
}

/* a bitmap file: the file header and a device independent bitmap of 24 bits, or of 8 or fewer with its table. */
static HRESULT pic_encode_bitmap(PICTURE *p, BYTE **data, ULONG *size)
{
    pic_deselect(p);
    PIC_DIB dib;
    HRESULT hr = pic_read_bitmap(p->S.bitmap, &dib, FALSE);
    if (FAILED(hr))
        return hr;
    ULONG table = dib.bpp <= 8 ? (ULONG)dib.colors * 4 : 0;
    ULONG image = dib.stride * dib.height;
    ULONG total = 14 + 40 + table + image;
    BYTE *out = AutAllocZero(total);
    if (!out) {
        AutFree(dib.bits);
        return E_OUTOFMEMORY;
    }
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, total);
    put32(out + 10, 14 + 40 + table);
    put32(out + 14, 40);
    put32(out + 18, dib.width);
    put32(out + 22, dib.height);
    put16(out + 26, 1);
    put16(out + 28, dib.bpp);
    put32(out + 34, image);
    put32(out + 46, dib.bpp <= 8 ? dib.colors : 0);
    memcpy(out + 54, dib.table, table);
    memcpy(out + 54 + table, dib.bits, image);
    AutFree(dib.bits);
    *data = out;
    *size = total;
    return S_OK;
}

/* an icon file with the one image: the colors as 24 bits and the mask as one. */
static HRESULT pic_encode_icon(PICTURE *p, BYTE **data, ULONG *size)
{
    if (!p->S.and_bmp || !p->S.xor_bmp)
        return E_UNEXPECTED;
    PIC_DIB color, mask;
    HRESULT hr = pic_read_bitmap(p->S.xor_bmp, &color, FALSE);
    if (FAILED(hr))
        return hr;
    hr = pic_read_bitmap(p->S.and_bmp, &mask, TRUE);
    if (FAILED(hr)) {
        AutFree(color.bits);
        return hr;
    }
    if (mask.width != color.width) {
        AutFree(color.bits);
        AutFree(mask.bits);
        return E_FAIL;
    }
    int height = color.height < mask.height ? color.height : mask.height;
    ULONG table = color.bpp <= 8 ? (ULONG)color.colors * 4 : 0;
    ULONG color_size = color.stride * height, mask_size = mask.stride * height;
    ULONG image = 40 + table + color_size + mask_size;
    ULONG total = 22 + image;
    BYTE *out = AutAllocZero(total);
    if (!out) {
        AutFree(color.bits);
        AutFree(mask.bits);
        return E_OUTOFMEMORY;
    }
    put16(out + 2, 1);
    put16(out + 4, 1);
    out[6] = (BYTE)color.width;
    out[7] = (BYTE)height;
    out[8] = (BYTE)(color.bpp <= 8 ? color.colors : 0);
    put16(out + 10, 1);
    put16(out + 12, color.bpp);
    put32(out + 14, image);
    put32(out + 18, 22);
    BYTE *bitmap = out + 22;
    put32(bitmap, 40);
    put32(bitmap + 4, color.width);
    put32(bitmap + 8, height * 2);
    put16(bitmap + 12, 1);
    put16(bitmap + 14, color.bpp);
    put32(bitmap + 20, color_size + mask_size);
    put32(bitmap + 32, color.bpp <= 8 ? color.colors : 0);
    memcpy(bitmap + 40, color.table, table);
    memcpy(bitmap + 40 + table, color.bits, color_size);
    memcpy(bitmap + 40 + table + color_size, mask.bits, mask_size);
    AutFree(color.bits);
    AutFree(mask.bits);
    *data = out;
    *size = total;
    return S_OK;
}

/* a placeable metafile: the signature, a header with the extent and 1440 units to the inch, and the metafile. */
static HRESULT pic_encode_metafile(PICTURE *p, BYTE **data, ULONG *size)
{
    UINT bits = GetMetaFileBitsEx(p->S.wmf, 0, nullptr);
    if (!bits)
        return E_OUTOFMEMORY;
    BYTE *out = AutAllocZero((SIZE_T)bits + 22);
    if (!out)
        return E_OUTOFMEMORY;
    put32(out, 0x9AC6CDD7);
    put16(out + 4 + 2 + 4, p->S.xext);
    put16(out + 4 + 2 + 6, p->S.yext);
    put16(out + 4 + 10, 1440);
    UINT sum = 0x5711;
    for (int i = 0; i < 9; i++)
        sum ^= out[4 + 2 * i] | out[4 + 2 * i + 1] << 8;
    put16(out + 4 + 16, sum);
    if (GetMetaFileBitsEx(p->S.wmf, bits, out + 22) != bits) {
        AutFree(out);
        return E_FAIL;
    }
    *data = out;
    *size = bits + 22;
    return S_OK;
}

static HRESULT pic_encode_enhanced(PICTURE *p, BYTE **data, ULONG *size)
{
    UINT bits = GetEnhMetaFileBits(p->S.emf, 0, nullptr);
    BYTE *out = AutAlloc(bits ? bits : 1);
    if (!out)
        return E_OUTOFMEMORY;
    if (GetEnhMetaFileBits(p->S.emf, bits, out) != bits) {
        AutFree(out);
        return E_FAIL;
    }
    *data = out;
    *size = bits;
    return S_OK;
}

/* the picture in the format of its type; an empty picture gives no bytes. */
static HRESULT pic_encode(PICTURE *p, BYTE **data, ULONG *size)
{
    *data = nullptr;
    *size = 0;
    pic_ready(p);
    switch (p->S.type) {
    case PICTYPE_BITMAP: return pic_encode_bitmap(p, data, size);
    case PICTYPE_METAFILE: return pic_encode_metafile(p, data, size);
    case PICTYPE_ICON: return pic_encode_icon(p, data, size);
    case PICTYPE_ENHMETAFILE: return pic_encode_enhanced(p, data, size);
    default: return S_OK;
    }
}

/* the picture as it was read, which is only there when it was kept. */
static HRESULT pic_original(PICTURE *p, const BYTE **data, ULONG *size)
{
    *data = nullptr;
    *size = 0;
    if (!p->Original)
        return p->S.type != PICTYPE_NONE && p->S.type != PIC_UNSET ? E_FAIL : S_OK;
    *data = p->Original;
    *size = p->OriginalLength;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_save_as_file(IPicture *This, IStream *stream, BOOL save_copy, LONG *written)
{
    PICTURE *p = (PICTURE *)This;
    if (!stream)
        return E_POINTER;
    const BYTE *data;
    ULONG size;
    BYTE *encoded = nullptr;
    HRESULT hr;
    if (save_copy)
        hr = pic_encode(p, &encoded, &size), data = encoded;
    else
        hr = pic_original(p, &data, &size);
    if (SUCCEEDED(hr) && size)
        hr = stream->lpVtbl->Write(stream, data, size, nullptr);
    AutFree(encoded);
    if (SUCCEEDED(hr) && written)
        *written = (LONG)size;
    return hr;
}

/* ------------------------------------------------------------------ IPersistStream */

static HRESULT STDMETHODCALLTYPE pic_persist_query(IPersistStream *This, REFIID riid, void **object)
{
    return pic_query(&PIC_FROM_PERSIST(This)->Pic, riid, object);
}

static ULONG STDMETHODCALLTYPE pic_persist_addref(IPersistStream *This)
{
    return InterlockedIncrement(&PIC_FROM_PERSIST(This)->Refs);
}

static ULONG STDMETHODCALLTYPE pic_persist_release(IPersistStream *This)
{
    return pic_release(&PIC_FROM_PERSIST(This)->Pic);
}

static HRESULT STDMETHODCALLTYPE pic_get_class_id(IPersistStream *This, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    *clsid = CLSID_StdPicture;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_is_dirty(IPersistStream *This)
{
    return S_OK;
}

/* a picture read into this object, which keeps what it held if the stream holds none. */
static HRESULT pic_load_stream(PICTURE *p, IStream *stream, ULONG length, BOOL run_mode, DWORD cx, DWORD cy, DWORD flags)
{
    PICSTATE loaded;
    BYTE *original;
    ULONG original_length;
    BOOL header;
    HRESULT hr = PicLoad(stream, length, run_mode, cx, cy, flags, &loaded, &original, &original_length, &header);
    if (FAILED(hr)) {
        /* a header around data that is no picture leaves the picture unfinished. */
        return header && p->S.type == PIC_UNSET ? E_UNEXPECTED : hr;
    }
    pic_release_state(p);
    p->S = loaded;
    p->Own = p->OwnGdi = p->OwnPal = TRUE;
    AutFree(p->Original);
    p->Original = original;
    p->OriginalLength = original_length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_load(IPersistStream *This, IStream *stream)
{
    PICTURE *p = PIC_FROM_PERSIST(This);
    return pic_load_stream(p, stream, 0, !p->Keep, 0, 0, 0);
}

/* the size of what Save writes after its header. */
static HRESULT pic_payload_size(PICTURE *p, ULONG *size)
{
    if (p->Original) {
        *size = p->OriginalLength;
        return S_OK;
    }
    BYTE *encoded;
    HRESULT hr = pic_encode(p, &encoded, size);
    AutFree(encoded);
    return hr;
}

static HRESULT STDMETHODCALLTYPE pic_save(IPersistStream *This, IStream *stream, BOOL clear_dirty)
{
    PICTURE *p = PIC_FROM_PERSIST(This);
    if (!stream)
        return E_INVALIDARG;
    const BYTE *data;
    ULONG size;
    BYTE *encoded = nullptr;
    HRESULT hr;
    if (p->Keep)
        hr = pic_original(p, &data, &size);
    else
        hr = pic_encode(p, &encoded, &size), data = encoded;
    if (FAILED(hr)) {
        AutFree(encoded);
        return hr;
    }
    BYTE header[8] = {'l', 't', 0, 0};
    put32(header + 4, size);
    hr = stream->lpVtbl->Write(stream, header, 8, nullptr);
    if (SUCCEEDED(hr) && size)
        hr = stream->lpVtbl->Write(stream, data, size, nullptr);
    AutFree(encoded);
    return hr;
}

static HRESULT STDMETHODCALLTYPE pic_get_size_max(IPersistStream *This, ULARGE_INTEGER *size)
{
    PICTURE *p = PIC_FROM_PERSIST(This);
    if (!size)
        return E_INVALIDARG;
    pic_ready(p);
    ULONG payload;
    HRESULT hr = pic_payload_size(p, &payload);
    if (FAILED(hr))
        return hr;
    size->QuadPart = (ULONGLONG)payload + 8;
    return S_OK;
}

/* ------------------------------------------------------------------ IPictureDisp */

static HRESULT STDMETHODCALLTYPE pic_disp_query(IDispatch *This, REFIID riid, void **object)
{
    return pic_query(&PIC_FROM_DISP(This)->Pic, riid, object);
}

static ULONG STDMETHODCALLTYPE pic_disp_addref(IDispatch *This)
{
    return InterlockedIncrement(&PIC_FROM_DISP(This)->Refs);
}

static ULONG STDMETHODCALLTYPE pic_disp_release(IDispatch *This)
{
    return pic_release(&PIC_FROM_DISP(This)->Pic);
}

static HRESULT STDMETHODCALLTYPE pic_get_type_info_count(IDispatch *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_get_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    return AutStdOleTypeInfo(index, &IID_IPictureDisp, info);
}

static HRESULT STDMETHODCALLTYPE pic_get_ids_of_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count,
                                                      LCID lcid, DISPID *ids)
{
    static const WCHAR *const table[] = {L"Handle", L"hPal", L"Type", L"Width", L"Height", L"Render"};
    static const DISPID dispids[] = {DISPID_PICT_HANDLE, DISPID_PICT_HPAL,   DISPID_PICT_TYPE,
                                     DISPID_PICT_WIDTH,  DISPID_PICT_HEIGHT, DISPID_PICT_RENDER};
    return AutNamesToIds(table, dispids, ARRAYSIZE(table), riid, names, count, ids);
}

static HRESULT pic_exception(HRESULT hr, EXCEPINFO *excepinfo)
{
    if (((ULONG)hr & 0x1FFF0000) == 0xA0000 && excepinfo) {
        memset(excepinfo, 0, sizeof(*excepinfo));
        excepinfo->scode = hr;
        return DISP_E_EXCEPTION;
    }
    return hr;
}

/* a whole number from the argument of a property put, whatever number type it came in. */
static HRESULT pic_argument(const VARIANT *source, LONGLONG *value, BOOL wide)
{
    switch (source->vt) {
    case VT_I1: *value = source->cVal; break;
    case VT_UI1: *value = source->bVal; break;
    case VT_I2:
    case VT_BOOL: *value = source->iVal; break;
    case VT_UI2: *value = source->uiVal; break;
    case VT_I4:
    case VT_INT: *value = source->lVal; break;
    case VT_UI4:
    case VT_UINT: *value = source->ulVal; break;
    case VT_I8: *value = source->llVal; break;
    case VT_EMPTY: *value = 0; break;
    default: return DISP_E_TYPEMISMATCH;
    }
    if (!wide && (*value > 0xFFFFFFFFll || *value < -0x80000000ll))
        return DISP_E_OVERFLOW;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                            DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo, UINT *arg_error)
{
    PICTURE *p = PIC_FROM_DISP(This);
    IPicture *pic = &p->Pic;
    IPicture2 *pic2 = &p->Pic2;
    HRESULT hr;
    if (!IsEqualIID(riid, &GUID_NULL))
        return DISP_E_UNKNOWNNAME;
    if (!(flags & DISPATCH_PROPERTYGET)) {
        if (flags & DISPATCH_PROPERTYPUT) {
            if (!params)
                return DISP_E_PARAMNOTOPTIONAL;
            if (params->cArgs != 1)
                return DISP_E_BADPARAMCOUNT;
            if (!params->rgvarg)
                return DISP_E_PARAMNOTOPTIONAL;
            if (id != DISPID_PICT_HPAL && id != DISPID_PICT_HPAL64)
                return DISP_E_MEMBERNOTFOUND;
            LONGLONG value;
            hr = pic_argument(params->rgvarg, &value, id == DISPID_PICT_HPAL64);
            if (FAILED(hr)) {
                if (hr == DISP_E_OVERFLOW)
                    hr = CTL_E_OVERFLOW;
                return pic_exception(hr, excepinfo);
            }
            hr = id == DISPID_PICT_HPAL ? pic->lpVtbl->set_hPal(pic, (OLE_HANDLE)value)
                                        : pic2->lpVtbl->set_hPal(pic2, (HHANDLE)value);
            return pic_exception(hr, excepinfo);
        }
        if (!(flags & DISPATCH_METHOD) || (id != DISPID_PICT_RENDER && id != DISPID_PICT_RENDER64))
            return DISP_E_MEMBERNOTFOUND;
        if (!params)
            return DISP_E_PARAMNOTOPTIONAL;
        if (params->cArgs != 10)
            return DISP_E_BADPARAMCOUNT;
        if (!params->rgvarg)
            return DISP_E_PARAMNOTOPTIONAL;
        const VARIANT *v = params->rgvarg;
        BOOL typed = (v[0].vt == VT_I2 || v[0].vt == VT_I4);
        for (int i = 1; i <= 8; i++)
            typed = typed && v[i].vt == VT_I4;
        if (!typed || (v[9].vt != VT_I4 && v[9].vt != VT_I8))
            return DISP_E_TYPEMISMATCH;
        HDC hdc = (HDC)(v[9].vt == VT_I8 ? (ULONG_PTR)v[9].llVal : (ULONG_PTR)(LONG_PTR)v[9].lVal);
        const RECT *bounds = (const RECT *)(ULONG_PTR)(v[0].vt == VT_I2 ? (LONG_PTR)v[0].iVal : (LONG_PTR)v[0].lVal);
        hr = pic->lpVtbl->Render(pic, hdc, v[8].lVal, v[7].lVal, v[6].lVal, v[5].lVal, v[4].lVal, v[3].lVal, v[2].lVal,
                                 v[1].lVal, bounds);
        return pic_exception(hr, excepinfo);
    }
    if (!result || !params)
        return DISP_E_PARAMNOTOPTIONAL;
    if (params->cArgs != 0)
        return DISP_E_BADPARAMCOUNT;
    switch (id) {
    case DISPID_PICT_HANDLE:
        result->vt = VT_I4;
        hr = pic->lpVtbl->get_Handle(pic, (OLE_HANDLE *)&result->lVal);
        break;
    case DISPID_PICT_HPAL:
        result->vt = VT_I4;
        hr = pic->lpVtbl->get_hPal(pic, (OLE_HANDLE *)&result->lVal);
        break;
    case DISPID_PICT_TYPE:
        result->vt = VT_I2;
        hr = pic->lpVtbl->get_Type(pic, &result->iVal);
        break;
    case DISPID_PICT_WIDTH:
        result->vt = VT_I4;
        hr = pic->lpVtbl->get_Width(pic, &result->lVal);
        break;
    case DISPID_PICT_HEIGHT:
        result->vt = VT_I4;
        hr = pic->lpVtbl->get_Height(pic, &result->lVal);
        break;
    case DISPID_PICT_HANDLE64:
        result->vt = VT_I8;
        hr = pic2->lpVtbl->get_Handle(pic2, (HHANDLE *)&result->llVal);
        break;
    case DISPID_PICT_HPAL64:
        result->vt = VT_I8;
        hr = pic2->lpVtbl->get_hPal(pic2, (HHANDLE *)&result->llVal);
        break;
    default:
        return DISP_E_MEMBERNOTFOUND;
    }
    if (FAILED(hr)) {
        result->vt = VT_EMPTY;
        return pic_exception(hr, excepinfo);
    }
    return hr;
}

/* ------------------------------------------------------------------ the tables */

static const IPictureVtbl pic_vtbl = {
    pic_query,           pic_addref,           pic_release,      pic_get_handle,  pic_get_hpal,
    pic_get_type,        pic_get_width,        pic_get_height,   pic_render,      pic_set_hpal,
    pic_get_curdc,       pic_select_picture,   pic_get_keep,     pic_put_keep,    pic_changed,
    pic_save_as_file,    pic_get_attributes,
};

static HRESULT STDMETHODCALLTYPE pic2_query(IPicture2 *This, REFIID riid, void **object)
{
    return pic_query(&PIC_FROM_PIC2(This)->Pic, riid, object);
}

static ULONG STDMETHODCALLTYPE pic2_addref(IPicture2 *This)
{
    return InterlockedIncrement(&PIC_FROM_PIC2(This)->Refs);
}

static ULONG STDMETHODCALLTYPE pic2_release(IPicture2 *This)
{
    return pic_release(&PIC_FROM_PIC2(This)->Pic);
}

static HRESULT STDMETHODCALLTYPE pic2_get_type(IPicture2 *This, SHORT *v)
{
    return pic_get_type(&PIC_FROM_PIC2(This)->Pic, v);
}

static HRESULT STDMETHODCALLTYPE pic2_get_width(IPicture2 *This, OLE_XSIZE_HIMETRIC *v)
{
    return pic_get_width(&PIC_FROM_PIC2(This)->Pic, v);
}

static HRESULT STDMETHODCALLTYPE pic2_get_height(IPicture2 *This, OLE_YSIZE_HIMETRIC *v)
{
    return pic_get_height(&PIC_FROM_PIC2(This)->Pic, v);
}

static HRESULT STDMETHODCALLTYPE pic2_render(IPicture2 *This, HDC hdc, LONG x, LONG y, LONG cx, LONG cy,
                                             OLE_XPOS_HIMETRIC xs, OLE_YPOS_HIMETRIC ys, OLE_XSIZE_HIMETRIC cxs,
                                             OLE_YSIZE_HIMETRIC cys, const RECT *bounds)
{
    return pic_render(&PIC_FROM_PIC2(This)->Pic, hdc, x, y, cx, cy, xs, ys, cxs, cys, bounds);
}

static HRESULT STDMETHODCALLTYPE pic2_get_curdc(IPicture2 *This, HDC *hdc)
{
    return pic_get_curdc(&PIC_FROM_PIC2(This)->Pic, hdc);
}

static HRESULT STDMETHODCALLTYPE pic2_get_keep(IPicture2 *This, BOOL *keep)
{
    return pic_get_keep(&PIC_FROM_PIC2(This)->Pic, keep);
}

static HRESULT STDMETHODCALLTYPE pic2_put_keep(IPicture2 *This, BOOL keep)
{
    return pic_put_keep(&PIC_FROM_PIC2(This)->Pic, keep);
}

static HRESULT STDMETHODCALLTYPE pic2_changed(IPicture2 *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pic2_save_as_file(IPicture2 *This, IStream *stream, BOOL copy, LONG *written)
{
    return pic_save_as_file(&PIC_FROM_PIC2(This)->Pic, stream, copy, written);
}

static HRESULT STDMETHODCALLTYPE pic2_get_attributes(IPicture2 *This, DWORD *attributes)
{
    return pic_get_attributes(&PIC_FROM_PIC2(This)->Pic, attributes);
}

static const IPicture2Vtbl pic2_vtbl = {
    pic2_query,         pic2_addref,         pic2_release,     pic2_get_handle, pic2_get_hpal,
    pic2_get_type,      pic2_get_width,      pic2_get_height,  pic2_render,     pic2_set_hpal,
    pic2_get_curdc,     pic2_select_picture, pic2_get_keep,    pic2_put_keep,   pic2_changed,
    pic2_save_as_file,  pic2_get_attributes,
};

static const IDispatchVtbl pic_disp_vtbl = {
    pic_disp_query,         pic_disp_addref,       pic_disp_release, pic_get_type_info_count,
    pic_get_type_info,      pic_get_ids_of_names,  pic_invoke,
};

static const IPersistStreamVtbl pic_persist_vtbl = {
    pic_persist_query, pic_persist_addref, pic_persist_release, pic_get_class_id,
    pic_is_dirty,      pic_load,           pic_save,            pic_get_size_max,
};

/* ------------------------------------------------------------------ creation */

static PICTURE *pic_new(BOOL keep)
{
    PICTURE *p = AutAllocZero(sizeof(PICTURE));
    if (!p)
        return nullptr;
    p->Pic.lpVtbl = &pic_vtbl;
    p->Pic2.lpVtbl = &pic2_vtbl;
    p->Disp.lpVtbl = &pic_disp_vtbl;
    p->Persist.lpVtbl = &pic_persist_vtbl;
    p->Refs = 1;
    p->Keep = keep;
    p->S.type = PIC_UNSET;
    return p;
}

WINOLEAUTAPI HRESULT WINAPI OleCreatePictureIndirect(LPPICTDESC desc, REFIID riid, BOOL own, LPVOID *object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    PICTURE *p = pic_new(FALSE);
    if (!p)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (desc) {
        p->S.type = desc->picType;
        switch (desc->picType) {
        case PICTYPE_BITMAP:
            p->S.bitmap = desc->bmp.hbitmap;
            p->S.palette = desc->bmp.hpal;
            break;
        case PICTYPE_METAFILE:
            p->S.wmf = desc->wmf.hmeta;
            p->S.xext = desc->wmf.xExt;
            p->S.yext = desc->wmf.yExt;
            break;
        case PICTYPE_ICON:
            p->S.icon = desc->icon.hicon;
            hr = PicIconBitmaps(desc->icon.hicon, &p->S.and_bmp, &p->S.xor_bmp);
            break;
        case PICTYPE_ENHMETAFILE:
            p->S.emf = desc->emf.hemf;
            break;
        default:
            break;
        }
        if (SUCCEEDED(hr)) {
            p->Own = own != 0;
            p->OwnGdi = own && desc->picType == PICTYPE_BITMAP;
            p->OwnPal = p->OwnGdi;
            hr = PicStateSize(&p->S);
        }
    }
    if (SUCCEEDED(hr))
        hr = pic_query(&p->Pic, riid, object);
    pic_release(&p->Pic);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI OleLoadPictureEx(LPSTREAM stream, LONG size, BOOL run_mode, REFIID riid, DWORD cx_desired,
                                             DWORD cy_desired, DWORD flags, LPVOID *object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!stream)
        return E_POINTER;
    PICTURE *p = pic_new(!run_mode);
    if (!p)
        return E_OUTOFMEMORY;
    HRESULT hr = pic_load_stream(p, stream, (ULONG)size, run_mode != 0, cx_desired, cy_desired, flags);
    if (SUCCEEDED(hr))
        hr = pic_query(&p->Pic, riid, object);
    pic_release(&p->Pic);
    return hr;
}
