/*
 * IShellItemImageFactory::GetImage: the thumbnail or the icon of a shell item as a 32 bit bitmap.
 *
 * a picture is a file one of the codecs of the imaging component knows. Its thumbnail is the first frame turned by
 * its Exif orientation, scaled to fit the size asked for; a jpeg or tiff file offers the thumbnail it carries when
 * that is big enough. Everything else is shown by its icon.
 */
#include "shell32_private.h"
#include <mime/win32/wincodec.h>

#ifndef STG_E_FILENOTFOUND
#define STG_E_FILENOTFOUND ((HRESULT)0x80030002L)
#endif

#ifndef LAYOUT_RTL
#define LAYOUT_RTL 1
#endif

/* the biggest bitmap a request may ask for, by its long side. */
#define LARGEST_REQUEST 8192
/* the sizes of the thumbnail cache of Windows, which a request is taken up to when it does not shrink to fit. */
static const int g_cache_sizes[] = {16, 32, 48, 96, 256, 768, 1280, 1920, 2560};

typedef HRESULT(WINAPI *CO_CREATE)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
typedef HRESULT(WINAPI *CO_INIT)(void *, DWORD);
typedef void(WINAPI *CO_UNINIT)(void);
typedef HRESULT(WINAPI *CLEAR_VARIANT)(PROPVARIANT *);

typedef struct {
    IWICImagingFactory *Factory;
    CO_UNINIT Uninitialize;
} WIC;

static void *ole_proc(const char *name)
{
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    return ole ? (void *)GetProcAddress(ole, name) : nullptr;
}

/* the imaging factory, with a multithreaded apartment made for the call when the thread has none. */
static HRESULT wic_open(WIC *wic)
{
    memset(wic, 0, sizeof(*wic));
    CO_CREATE create = (CO_CREATE)ole_proc("CoCreateInstance");
    if (!create)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    HRESULT hr = create(&CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                        (void **)&wic->Factory);
    if (hr == CO_E_NOTINITIALIZED) {
        CO_INIT init = (CO_INIT)ole_proc("CoInitializeEx");
        CO_UNINIT uninit = (CO_UNINIT)ole_proc("CoUninitialize");
        if (init && uninit && SUCCEEDED(init(nullptr, COINIT_MULTITHREADED))) {
            wic->Uninitialize = uninit;
            hr = create(&CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                        (void **)&wic->Factory);
        }
    }
    if (FAILED(hr) && wic->Uninitialize) {
        wic->Uninitialize();
        wic->Uninitialize = nullptr;
    }
    return hr;
}

static void wic_close(WIC *wic)
{
    if (wic->Factory)
        wic->Factory->lpVtbl->Release(wic->Factory);
    if (wic->Uninitialize)
        wic->Uninitialize();
}

static void release_source(IWICBitmapSource *source)
{
    if (source)
        source->lpVtbl->Release(source);
}

/* a source that fits the box by its long side, which keeps the proportions; the size it already has is kept. */
static HRESULT scale_fit(IWICImagingFactory *factory, IWICBitmapSource *source, int box, IWICBitmapSource **out)
{
    UINT w, h;
    HRESULT hr = source->lpVtbl->GetSize(source, &w, &h);
    if (FAILED(hr))
        return hr;
    UINT nw, nh;
    if (w > h) {
        nw = (UINT)box;
        nh = (UINT)MulDiv(box, (int)h, (int)w);
    } else {
        nh = (UINT)box;
        nw = (UINT)MulDiv(box, (int)w, (int)h);
    }
    if (!nw)
        nw = 1;
    if (!nh)
        nh = 1;
    if (nw == w && nh == h) {
        source->lpVtbl->AddRef(source);
        *out = source;
        return S_OK;
    }
    IWICBitmapScaler *scaler;
    hr = factory->lpVtbl->CreateBitmapScaler(factory, &scaler);
    if (FAILED(hr))
        return hr;
    hr = scaler->lpVtbl->Initialize(scaler, source, nw, nh, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr))
        *out = (IWICBitmapSource *)scaler;
    else
        scaler->lpVtbl->Release(scaler);
    return hr;
}

/* a source of exactly this size. */
static HRESULT scale_exact(IWICImagingFactory *factory, IWICBitmapSource *source, UINT w, UINT h,
                           IWICBitmapSource **out)
{
    UINT sw, sh;
    HRESULT hr = source->lpVtbl->GetSize(source, &sw, &sh);
    if (FAILED(hr))
        return hr;
    if (sw == w && sh == h) {
        source->lpVtbl->AddRef(source);
        *out = source;
        return S_OK;
    }
    IWICBitmapScaler *scaler;
    hr = factory->lpVtbl->CreateBitmapScaler(factory, &scaler);
    if (FAILED(hr))
        return hr;
    hr = scaler->lpVtbl->Initialize(scaler, source, w ? w : 1, h ? h : 1, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr))
        *out = (IWICBitmapSource *)scaler;
    else
        scaler->lpVtbl->Release(scaler);
    return hr;
}

static HRESULT clip(IWICImagingFactory *factory, IWICBitmapSource *source, const WICRect *rect,
                    IWICBitmapSource **out)
{
    IWICBitmapClipper *clipper;
    HRESULT hr = factory->lpVtbl->CreateBitmapClipper(factory, &clipper);
    if (FAILED(hr))
        return hr;
    hr = clipper->lpVtbl->Initialize(clipper, source, rect);
    if (SUCCEEDED(hr))
        *out = (IWICBitmapSource *)clipper;
    else
        clipper->lpVtbl->Release(clipper);
    return hr;
}

/* premultiplied 32 bit pixels, which every later step works in. */
static HRESULT to_pbgra(IWICImagingFactory *factory, IWICBitmapSource *source, IWICBitmapSource **out)
{
    WICPixelFormatGUID format;
    HRESULT hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (FAILED(hr))
        return hr;
    if (IsEqualGUID(&format, &GUID_WICPixelFormat32bppPBGRA)) {
        source->lpVtbl->AddRef(source);
        *out = source;
        return S_OK;
    }
    IWICFormatConverter *converter;
    hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (FAILED(hr))
        return hr;
    hr = converter->lpVtbl->Initialize(converter, source, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                       nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        *out = (IWICBitmapSource *)converter;
    else
        converter->lpVtbl->Release(converter);
    return hr;
}

/* the transform of an Exif orientation, which turns the stored pixels upright. */
static BOOL orientation_transform(int orientation, WICBitmapTransformOptions *transform)
{
    switch (orientation) {
    case 2:
        *transform = WICBitmapTransformFlipHorizontal;
        return TRUE;
    case 3:
        *transform = WICBitmapTransformRotate180;
        return TRUE;
    case 4:
        *transform = WICBitmapTransformFlipVertical;
        return TRUE;
    case 5:
        *transform = (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        return TRUE;
    case 6:
        *transform = WICBitmapTransformRotate90;
        return TRUE;
    case 7:
        *transform = (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        return TRUE;
    case 8:
        *transform = WICBitmapTransformRotate270;
        return TRUE;
    default:
        return FALSE;
    }
}

/* the Exif orientation of a frame, 1 when it has none. */
static int frame_orientation(IWICBitmapFrameDecode *frame)
{
    IWICMetadataQueryReader *reader;
    if (FAILED(frame->lpVtbl->GetMetadataQueryReader(frame, &reader)))
        return 1;
    CLEAR_VARIANT clear = (CLEAR_VARIANT)ole_proc("PropVariantClear");
    static const WCHAR *const names[] = {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"};
    int orientation = 1;
    for (int i = 0; i < 2 && clear; i++) {
        PROPVARIANT value;
        memset(&value, 0, sizeof(value));
        if (SUCCEEDED(reader->lpVtbl->GetMetadataByName(reader, names[i], &value)) && value.vt == VT_UI2) {
            orientation = value.uiVal;
            clear(&value);
            break;
        }
        clear(&value);
    }
    reader->lpVtbl->Release(reader);
    return orientation;
}

static HRESULT orient(IWICImagingFactory *factory, IWICBitmapSource *source, int orientation, IWICBitmapSource **out)
{
    WICBitmapTransformOptions transform;
    if (!orientation_transform(orientation, &transform)) {
        source->lpVtbl->AddRef(source);
        *out = source;
        return S_OK;
    }
    IWICBitmapFlipRotator *rotator;
    HRESULT hr = factory->lpVtbl->CreateBitmapFlipRotator(factory, &rotator);
    if (FAILED(hr))
        return hr;
    hr = rotator->lpVtbl->Initialize(rotator, source, transform);
    if (SUCCEEDED(hr))
        *out = (IWICBitmapSource *)rotator;
    else
        rotator->lpVtbl->Release(rotator);
    return hr;
}

/* the size a request is extracted at: the next size of the cache of Windows, or the request when it is bigger. */
static int cache_size(int request)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_cache_sizes); i++)
        if (request <= g_cache_sizes[i])
            return g_cache_sizes[i];
    return request;
}

/* whether the thumbnail of a file may be one it carries, as Windows takes them from these extensions. */
static BOOL carries_thumbnail(LPCWSTR path)
{
    static const WCHAR *const extensions[] = {L"jpg", L"jpeg", L"tif", L"tiff", L"wdp", L"crw", L"cr2", L"nef",
                                              L"dng", L"orf",  L"rw2", L"pef",  L"arw", L"sr2", L"srf"};
    LPCWSTR dot = wcsrchr(path, L'.');
    if (!dot || wcschr(dot, L'\\'))
        return FALSE;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(extensions); i++)
        if (!_wcsicmp(dot + 1, extensions[i]))
            return TRUE;
    return FALSE;
}


/* the 32 bit top down dib that holds the pixels of a source. */
static HRESULT bitmap_from_source(IWICBitmapSource *source, HBITMAP *out)
{
    UINT w, h;
    HRESULT hr = source->lpVtbl->GetSize(source, &w, &h);
    if (FAILED(hr))
        return hr;
    if (!w || !h || w > 0x7FFF || h > 0x7FFF)
        return E_INVALIDARG;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = (LONG)w;
    info.bmiHeader.biHeight = -(LONG)h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return E_OUTOFMEMORY;
    WICRect all = {0, 0, (INT)w, (INT)h};
    hr = source->lpVtbl->CopyPixels(source, &all, w * 4, w * h * 4, (BYTE *)bits);
    if (FAILED(hr)) {
        DeleteObject(bitmap);
        return hr;
    }
    *out = bitmap;
    return S_OK;
}

/* the long side of a source. */
static UINT long_side(IWICBitmapSource *source)
{
    UINT w = 0, h = 0;
    source->lpVtbl->GetSize(source, &w, &h);
    return w > h ? w : h;
}

/*
 * the part of the picture a crop flag keeps, and the size the result is scaled to. A square takes the middle of a wide
 * picture and the part starting a third down a tall one. The wide shape is 19 by 13 and is cut the same way when the
 * ratio is off by more than a hundredth. The offsets are unsigned, so a half is a shift.
 */
static void crop_area(UINT w, UINT h, int request, int flags, WICRect *area, UINT *out_w, UINT *out_h)
{
    area->X = area->Y = 0;
    area->Width = (INT)w;
    area->Height = (INT)h;
    if (flags & SIIGBF_CROPTOSQUARE) {
        *out_w = *out_h = (UINT)request;
        if (w < h) {
            area->Y = (INT)((h - w) / 3);
            area->Height = (INT)w;
        } else if (w > h) {
            area->X = (INT)((w - h) >> 1);
            area->Width = (INT)h;
        }
        return;
    }
    *out_w = (UINT)request;
    *out_h = (UINT)MulDiv(request, 13, 19);
    if (!h)
        return;
    const double shape = 19.0 / 13.0;
    double ratio = (double)w / (double)h;
    if (ratio - shape > 0.01) {
        UINT width = (UINT)MulDiv((int)h, 19, 13);
        if (width < w) {
            area->Width = (INT)width;
            area->X = (INT)((w - width) >> 1);
        }
    } else if (shape - ratio > 0.01) {
        UINT height = (UINT)MulDiv((int)w, 13, 19);
        if (height < h) {
            area->Height = (INT)height;
            area->Y = (INT)((h - height) / 3);
        }
    }
}

/* shapes the picture for the flags: a square or wide crop, the size asked for or the one the cache would give. */
static HRESULT shape(IWICImagingFactory *factory, IWICBitmapSource *picture, int request, int flags, BOOL natural,
                     IWICBitmapSource **out)
{
    UINT w, h;
    HRESULT hr = picture->lpVtbl->GetSize(picture, &w, &h);
    if (FAILED(hr))
        return hr;
    if (flags & (SIIGBF_CROPTOSQUARE | SIIGBF_WIDETHUMBNAILS)) {
        WICRect area;
        UINT box_w, box_h;
        crop_area(w, h, request, flags, &area, &box_w, &box_h);
        IWICBitmapSource *part = picture;
        part->lpVtbl->AddRef(part);
        if (area.X || area.Y || area.Width != (INT)w || area.Height != (INT)h) {
            IWICBitmapSource *clipped;
            hr = clip(factory, picture, &area, &clipped);
            release_source(part);
            if (FAILED(hr))
                return hr;
            part = clipped;
        }
        hr = scale_exact(factory, part, box_w, box_h, out);
        release_source(part);
        return hr;
    }
    /* the picture as it is is kept when the cache's size is asked for and it already has that long side. */
    int target = request;
    if (flags & SIIGBF_BIGGERSIZEOK) {
        if (natural)
            target = (int)long_side(picture);
        else
            target = cache_size(request);
    } else if (natural && (int)long_side(picture) <= request) {
        target = (int)long_side(picture);
    }
    hr = scale_fit(factory, picture, target, out);
    if (SUCCEEDED(hr) && (flags & SIIGBF_SCALEUP) && (int)long_side(*out) < request) {
        IWICBitmapSource *bigger;
        hr = scale_fit(factory, *out, request, &bigger);
        release_source(*out);
        *out = SUCCEEDED(hr) ? bigger : nullptr;
    }
    return hr;
}

/*
 * a carried thumbnail suits the picture when the side that is not its long one is within a pixel of what the picture's
 * proportions give for it. A size of 0 never suits.
 */
static BOOL thumbnail_matches(UINT tw, UINT th, UINT fw, UINT fh)
{
    if (!tw || !th || !fw || !fh)
        return FALSE;
    double gap;
    if (tw >= th)
        gap = (double)th - (double)fh * (double)tw / (double)fw;
    else
        gap = (double)tw - (double)fw * (double)th / (double)fh;
    return gap > -1.0 && gap < 1.0;
}

/* the thumbnail a jpeg or tiff file carries, upright, when it is as big as the request and fits the picture. */
static HRESULT carried_thumbnail(IWICImagingFactory *factory, IWICBitmapDecoder *decoder, IWICBitmapFrameDecode *frame,
                                 int orientation, UINT fw, UINT fh, int request, IWICBitmapSource **out)
{
    IWICBitmapSource *thumb;
    HRESULT hr = frame->lpVtbl->GetThumbnail(frame, &thumb);
    if (hr == WINCODEC_ERR_CODECNOTHUMBNAIL)
        hr = decoder->lpVtbl->GetPreview(decoder, &thumb);
    if (FAILED(hr))
        return WTS_E_FASTEXTRACTIONNOTSUPPORTED;
    IWICBitmapSource *upright;
    hr = orient(factory, thumb, orientation, &upright);
    release_source(thumb);
    if (FAILED(hr))
        return hr;
    UINT tw, th;
    hr = upright->lpVtbl->GetSize(upright, &tw, &th);
    if (FAILED(hr) || !thumbnail_matches(tw, th, fw, fh) || ((int)tw < request && (int)th < request)) {
        release_source(upright);
        return WTS_E_FASTEXTRACTIONNOTSUPPORTED;
    }
    *out = upright;
    return S_OK;
}

static BOOL file_error(HRESULT hr)
{
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
           hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) || hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
}

/* the thumbnail of a picture file. */
static HRESULT picture_bitmap(LPCWSTR path, int request, int flags, HBITMAP *out)
{
    WIC wic;
    HRESULT hr = wic_open(&wic);
    if (FAILED(hr))
        return hr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapSource *upright = nullptr, *picture = nullptr, *pbgra = nullptr, *shaped = nullptr;
    hr = wic.Factory->lpVtbl->CreateDecoderFromFilename(wic.Factory, path, nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        hr = file_error(hr) ? hr : WTS_E_FAILEDEXTRACTION;
        goto done;
    }
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) {
        hr = WTS_E_FAILEDEXTRACTION;
        goto done;
    }
    {
        int orientation = frame_orientation(frame);
        UINT fw, fh;
        hr = orient(wic.Factory, (IWICBitmapSource *)frame, orientation, &upright);
        if (FAILED(hr) || FAILED(upright->lpVtbl->GetSize(upright, &fw, &fh))) {
            hr = WTS_E_FAILEDEXTRACTION;
            goto done;
        }
        /* the thumbnail the file carries, for the sizes the cache of Windows holds. */
        if (request <= 256 && carries_thumbnail(path))
            carried_thumbnail(wic.Factory, decoder, frame, orientation, fw, fh, request, &picture);
        if (!picture) {
            picture = upright;
            picture->lpVtbl->AddRef(picture);
        }
    }
    hr = to_pbgra(wic.Factory, picture, &pbgra);
    if (SUCCEEDED(hr))
        hr = shape(wic.Factory, pbgra, request, flags, picture != upright, &shaped);
    if (SUCCEEDED(hr))
        hr = bitmap_from_source(shaped, out);
    if (FAILED(hr) && !file_error(hr))
        hr = WTS_E_FAILEDEXTRACTION;
done:
    release_source(shaped);
    release_source(pbgra);
    release_source(picture);
    release_source(upright);
    if (frame)
        frame->lpVtbl->Release(frame);
    if (decoder)
        decoder->lpVtbl->Release(decoder);
    wic_close(&wic);
    return hr;
}

/* ------------------------------------------------------------------ icons */

static HRESULT bitmap_from_pixels(const DWORD *pixels, int cx, int cy, HBITMAP *out)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = cx;
    info.bmiHeader.biHeight = -cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return E_OUTOFMEMORY;
    memcpy(bits, pixels, (SIZE_T)cx * (SIZE_T)cy * sizeof(DWORD));
    *out = bitmap;
    return S_OK;
}

/* the icon alone, square at the width asked for. */
static HRESULT icon_bitmap(const SH_ICON_SOURCE *source, int cx, HBITMAP *out)
{
    HICON icon = ShLoadIconSource(source, cx, cx);
    if (!icon)
        return E_OUTOFMEMORY;
    int w, h;
    DWORD *pixels = ShIconPixels(icon, &w, &h);
    DestroyIcon(icon);
    if (!pixels)
        return E_FAIL;
    HRESULT hr;
    if (w == cx && h == cx) {
        hr = bitmap_from_pixels(pixels, cx, cx, out);
    } else {
        /* an icon that came out another size is stretched by nearest pixels. */
        DWORD *square = ShAlloc((SIZE_T)cx * (SIZE_T)cx * sizeof(DWORD));
        hr = E_OUTOFMEMORY;
        if (square) {
            for (int y = 0; y < cx; y++)
                for (int x = 0; x < cx; x++)
                    square[y * cx + x] = pixels[(y * h / cx) * w + x * w / cx];
            hr = bitmap_from_pixels(square, cx, cx, out);
            ShFree(square);
        }
    }
    ShFree(pixels);
    return hr;
}

/* the accent the tile of an icon is filled with, which Windows takes from the app's tile or the user's color. */
#define TILE_RED 0
#define TILE_GREEN 120
#define TILE_BLUE 215


/* a channel of the frame of a tile, lifted towards white. */
static DWORD frame_color(DWORD color)
{
    DWORD out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= (228 * ((color >> shift) & 255) / 255 + 27) << shift;
    return out;
}

/* a pixel of the icon, premultiplied, over an opaque one. */
static DWORD over_opaque(DWORD icon, DWORD under)
{
    DWORD inverse = 255 - (icon >> 24);
    DWORD out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= (((icon >> shift) & 255) + (((under >> shift) & 255) * inverse + 127) / 255) << shift;
    return out;
}

/* the icon on a tile of a color with a lighter frame, centered with a margin of half the free space. */
static HRESULT tile_bitmap(const SH_ICON_SOURCE *source, int cx, int cy, BOOL wide, HBITMAP *out)
{
    if (wide)
        cy = MulDiv(cx, 13, 19);
    if (cx <= 0 || cy <= 0)
        return E_INVALIDARG;
    UINT side = (UINT)(wide ? MulDiv(cx, 48, 190) : MulDiv(cx, 32, 40));
    if (!side)
        side = 1;
    DWORD *tile = ShAlloc((SIZE_T)cx * (SIZE_T)cy * sizeof(DWORD));
    if (!tile)
        return E_OUTOFMEMORY;
    const DWORD fill = 0xff000000u | (TILE_RED << 16) | (TILE_GREEN << 8) | TILE_BLUE;
    const DWORD edge = frame_color(fill);
    for (int y = 0; y < cy; y++)
        for (int x = 0; x < cx; x++)
            tile[(SIZE_T)y * cx + x] = (x == 0 || y == 0 || x == cx - 1 || y == cy - 1) ? edge : fill;

    HRESULT hr = S_OK;
    HICON icon = ShLoadIconSource(source, (int)side, (int)side);
    int iw = 0, ih = 0;
    DWORD *art = icon ? ShIconPixels(icon, &iw, &ih) : nullptr;
    if (icon)
        DestroyIcon(icon);
    if (art && iw > 0 && ih > 0) {
        UINT free_x = (UINT)cx - side, free_y = (UINT)cy - side;
        UINT left = free_x >> 1, top = free_y >> 1;
        if (left > side || top > side)
            left = top = side >> 2;
        DWORD layout = 0;
        if (GetProcessDefaultLayout(&layout) && (layout & LAYOUT_RTL))
            left = free_x - left;
        for (UINT y = 0; y < side; y++) {
            for (UINT x = 0; x < side; x++) {
                int px = (int)(left + x), py = (int)(top + y);
                if (px >= cx || py >= cy)
                    continue;
                DWORD pixel = art[(SIZE_T)(y * (UINT)ih / side) * (UINT)iw + x * (UINT)iw / side];
                DWORD *under = &tile[(SIZE_T)py * cx + px];
                *under = over_opaque(pixel, *under);
            }
        }
    } else {
        hr = E_FAIL;
    }
    if (art)
        ShFree(art);
    if (SUCCEEDED(hr))
        hr = bitmap_from_pixels(tile, cx, cy, out);
    ShFree(tile);
    return hr;
}

/* ------------------------------------------------------------------ the request */

/*
 * the bitmap of an item. path is the file the item is, or NULL for the desktop, which is virtual kind 1, "This PC",
 * which is 2, and the other folders of the shell, which are 3.
 *
 * a request for icons or for what is in memory never makes a thumbnail, and one that wants only a thumbnail then
 * fails with E_PENDING. Otherwise the thumbnail is tried first and the icon stands in for it, unless only the
 * thumbnail was wanted, which gives the failure of the extraction. A request for what is in memory that was
 * answered with an icon says S_FALSE.
 */
HRESULT ShItemImage(LPCWSTR path, int virtual_kind, SIZE size, int flags, HBITMAP *bitmap)
{
    if (!bitmap)
        return E_POINTER;
    *bitmap = nullptr;
    flags &= 0x1FF;
    int request = size.cx > size.cy ? size.cx : size.cy;
    if (request <= 0 || request > LARGEST_REQUEST)
        return E_INVALIDARG;
    BOOL thumbnail_only = (flags & SIIGBF_THUMBNAILONLY) != 0, icon_only = (flags & SIIGBF_ICONONLY) != 0,
         memory_only = (flags & SIIGBF_MEMORYONLY) != 0;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if (path) {
        DWORD found = GetFileAttributesW(path);
        if (found != INVALID_FILE_ATTRIBUTES)
            attributes = found;
    }
    HRESULT thumbnail = E_PENDING;
    if (!memory_only && !icon_only) {
        if (flags & SIIGBF_INCACHEONLY)
            thumbnail = STG_E_FILENOTFOUND;
        else if (!path || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            thumbnail = WTS_E_FAILEDEXTRACTION;
        else
            thumbnail = picture_bitmap(path, request, flags, bitmap);
        if (SUCCEEDED(thumbnail))
            return thumbnail;
    }
    if (thumbnail_only)
        return thumbnail;
    SH_ICON_SOURCE source;
    if (virtual_kind)
        ShIconSourceStock(virtual_kind == 1 ? SH_STOCK_DESKTOP : virtual_kind == 2 ? SH_STOCK_COMPUTER : SIID_FOLDER,
                          &source);
    else
        ShIconSourceForPath(path, attributes, TRUE, FALSE, &source);
    if (size.cx <= 0)
        return E_INVALIDARG;
    HRESULT hr = (flags & SIIGBF_ICONBACKGROUND) ? tile_bitmap(&source, size.cx, size.cy, (flags & SIIGBF_WIDETHUMBNAILS) != 0, bitmap)
                                                  : icon_bitmap(&source, size.cx, bitmap);
    if (FAILED(hr))
        return hr;
    return memory_only && !icon_only ? S_FALSE : S_OK;
}
