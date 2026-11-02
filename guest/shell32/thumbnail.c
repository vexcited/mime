/*
 * IShellItemImageFactory::GetImage: a shell item's thumbnail or icon as a 32-bit bitmap.
 *
 * pictures are files a WIC codec decodes. the thumbnail is the first frame turned upright per its Exif orientation and
 * scaled to fit, or the thumbnail a jpeg or tiff carries when it is big enough. everything else shows its icon.
 */
#include "shell32_private.h"
#include <mime/win32/wincodec.h>
#include <mime/win32/propsys.h>

#ifndef STG_E_FILENOTFOUND
#define STG_E_FILENOTFOUND ((HRESULT)0x80030002L)
#endif

#ifndef LAYOUT_RTL
#define LAYOUT_RTL 1
#endif

/* the largest long side a request may ask for. */
#define LARGEST_REQUEST 8192
/* the Windows thumbnail cache sizes, which requests that do not shrink to fit round up to. */
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

/* enters a multithreaded apartment for the call when the thread has none. */
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

/* scales the long side to box, keeping proportions. */
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

/* every later step works in premultiplied 32-bit pixels. */
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

/* the transform that turns pixels stored in an Exif orientation upright. */
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

/* 1 when the frame has no Exif orientation. */
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

/* rounds up to the next Windows cache size, or keeps a request bigger than all of them. */
static int cache_size(int request)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_cache_sizes); i++)
        if (request <= g_cache_sizes[i])
            return g_cache_sizes[i];
    return request;
}

/* the extensions Windows takes embedded thumbnails from. */
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


/* a 32-bit top-down DIB of the source's pixels. */
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

static UINT long_side(IWICBitmapSource *source)
{
    UINT w = 0, h = 0;
    source->lpVtbl->GetSize(source, &w, &h);
    return w > h ? w : h;
}

/*
 * what a crop flag keeps, and the size the result is scaled to. a square crop takes the middle of a wide picture and
 * starts a third down a tall one. the wide shape is 19 by 13, cut the same way when the ratio is off by more than a
 * hundredth. the offsets are unsigned, so a half is a shift.
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

/* crops square or wide per the flags, then scales to the request or to the size the cache would give. */
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
    /* a carried thumbnail is used at its own size when that size suits the request. */
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

/* the short side must be within a pixel of what the picture's proportions give. a zero size never matches. */
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

/* only used when it is at least as big as the request and has the picture's proportions. */
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

/*
 * the thumbnail handler of a file type: the class that HKCR\<extension>\ShellEx\{E357FCCD-...} or the same key of the
 * program id the extension names has. It is made, given the file through the interface it takes (a stream, the item or
 * the path, in that order) and asked for the bitmap of the size.
 */
MIME_DEFINE_GUID(IID_PsThumbnailProvider, 0xE357FCCD, 0xA995, 0x4576, 0xB0, 0x1F, 0x23, 0x46, 0x30, 0x15, 0x4E, 0x96);
static const WCHAR THUMBNAIL_HANDLER[] = L"\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";

typedef struct PS_THUMBNAIL_PROVIDER PS_THUMBNAIL_PROVIDER;
typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(PS_THUMBNAIL_PROVIDER *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(PS_THUMBNAIL_PROVIDER *This);
    ULONG(STDMETHODCALLTYPE *Release)(PS_THUMBNAIL_PROVIDER *This);
    HRESULT(STDMETHODCALLTYPE *GetThumbnail)(PS_THUMBNAIL_PROVIDER *This, UINT size, HBITMAP *bitmap, DWORD *alpha);
} PS_THUMBNAIL_PROVIDER_VTBL;
struct PS_THUMBNAIL_PROVIDER {
    const PS_THUMBNAIL_PROVIDER_VTBL *lpVtbl;
};

static BOOL handler_clsid(LPCWSTR path, CLSID *clsid)
{
    LPCWSTR extension = wcsrchr(path, L'.');
    if (!extension || wcschr(extension, L'\\') || wcslen(extension) > 40)
        return FALSE;
    WCHAR key[160], text[64];
    wsprintfW(key, L"%s%s", extension, THUMBNAIL_HANDLER);
    DWORD size = sizeof(text);
    if (RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, text, &size) == ERROR_SUCCESS &&
        SUCCEEDED(CLSIDFromString(text, clsid)))
        return TRUE;
    WCHAR progid[100];
    size = sizeof(progid);
    if (RegGetValueW(HKEY_CLASSES_ROOT, extension, nullptr, RRF_RT_REG_SZ, nullptr, progid, &size) != ERROR_SUCCESS ||
        !progid[0])
        return FALSE;
    wsprintfW(key, L"%s%s", progid, THUMBNAIL_HANDLER);
    size = sizeof(text);
    return RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, text, &size) == ERROR_SUCCESS &&
           SUCCEEDED(CLSIDFromString(text, clsid));
}

static HRESULT initialize_handler(IUnknown *object, LPCWSTR path)
{
    IInitializeWithStream *with_stream;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IInitializeWithStream, (void **)&with_stream))) {
        typedef HRESULT(WINAPI * STREAM_ON_FILE)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **);
        HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
        STREAM_ON_FILE open_stream = shlwapi ? (STREAM_ON_FILE)GetProcAddress(shlwapi, "SHCreateStreamOnFileEx") : nullptr;
        IStream *stream = nullptr;
        HRESULT hr = open_stream ? open_stream(path, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)
                                 : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        if (SUCCEEDED(hr)) {
            hr = with_stream->lpVtbl->Initialize(with_stream, stream, STGM_READ);
            stream->lpVtbl->Release(stream);
        }
        with_stream->lpVtbl->Release(with_stream);
        return hr;
    }
    IInitializeWithFile *with_file;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IInitializeWithFile, (void **)&with_file))) {
        HRESULT hr = with_file->lpVtbl->Initialize(with_file, path, STGM_READ);
        with_file->lpVtbl->Release(with_file);
        return hr;
    }
    return S_OK;
}

static HRESULT handler_bitmap(IWICImagingFactory *factory, LPCWSTR path, int request, IWICBitmapSource **out)
{
    CLSID clsid;
    if (!handler_clsid(path, &clsid))
        return WTS_E_FASTEXTRACTIONNOTSUPPORTED;
    CO_CREATE create = (CO_CREATE)ole_proc("CoCreateInstance");
    if (!create)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    IUnknown *object = nullptr;
    HRESULT hr = create(&clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&object);
    if (FAILED(hr))
        return WTS_E_FASTEXTRACTIONNOTSUPPORTED;
    hr = initialize_handler(object, path);
    PS_THUMBNAIL_PROVIDER *provider = nullptr;
    if (SUCCEEDED(hr))
        hr = object->lpVtbl->QueryInterface(object, &IID_PsThumbnailProvider, (void **)&provider);
    HBITMAP bitmap = nullptr;
    DWORD alpha = 0;
    if (SUCCEEDED(hr))
        hr = provider->lpVtbl->GetThumbnail(provider, (UINT)request, &bitmap, &alpha);
    if (SUCCEEDED(hr) && bitmap) {
        /* an alpha type of 1 (WTSAT_RGB) has no alpha, 2 (WTSAT_ARGB) has. */
        IWICBitmap *wic;
        hr = factory->lpVtbl->CreateBitmapFromHBITMAP(factory, bitmap, nullptr,
                                                      alpha == 2 ? WICBitmapUseAlpha : WICBitmapIgnoreAlpha, &wic);
        if (SUCCEEDED(hr))
            *out = (IWICBitmapSource *)wic;
    } else if (SUCCEEDED(hr)) {
        hr = WTS_E_FAILEDEXTRACTION;
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (provider)
        provider->lpVtbl->Release(provider);
    object->lpVtbl->Release(object);
    return hr;
}

static HRESULT picture_bitmap(LPCWSTR path, int request, int flags, HBITMAP *out)
{
    WIC wic;
    HRESULT hr = wic_open(&wic);
    if (FAILED(hr))
        return hr;
    {
        /* a type with a thumbnail handler gets its own picture, the codecs of the imaging component are for the others. */
        IWICBitmapSource *handled = nullptr, *pbgra = nullptr, *shaped = nullptr;
        HRESULT handler = handler_bitmap(wic.Factory, path, request, &handled);
        if (SUCCEEDED(handler)) {
            hr = to_pbgra(wic.Factory, handled, &pbgra);
            if (SUCCEEDED(hr))
                hr = shape(wic.Factory, pbgra, request, flags, TRUE, &shaped);
            if (SUCCEEDED(hr))
                hr = bitmap_from_source(shaped, out);
            if (FAILED(hr))
                hr = WTS_E_FAILEDEXTRACTION;
            release_source(shaped);
            release_source(pbgra);
            release_source(handled);
            wic_close(&wic);
            return hr;
        }
        if (handler != WTS_E_FASTEXTRACTIONNOTSUPPORTED) {
            wic_close(&wic);
            return WTS_E_FAILEDEXTRACTION;
        }
    }
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
        /* embedded thumbnails only serve the small sizes of the Windows cache. */
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

/* the bare icon, square at the requested width. */
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
        /* stretch an icon of another size by nearest neighbor. */
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

/* the tile accent. Windows takes it from the app's tile or the user's color. */
#define TILE_RED 0
#define TILE_GREEN 120
#define TILE_BLUE 215


/* the tile frame: each channel lifted towards white. */
static DWORD frame_color(DWORD color)
{
    DWORD out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= (228 * ((color >> shift) & 255) / 255 + 27) << shift;
    return out;
}

/* blends a premultiplied icon pixel over an opaque one. */
static DWORD over_opaque(DWORD icon, DWORD under)
{
    DWORD inverse = 255 - (icon >> 24);
    DWORD out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= (((icon >> shift) & 255) + (((under >> shift) & 255) * inverse + 127) / 255) << shift;
    return out;
}

/* the icon centered on a colored tile with a lighter frame. */
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

/* ------------------------------------------------------------------ request */

/*
 * path is the item's file, or NULL for a virtual item: kind 1 is the desktop, 2 "This PC", 3 any other shell folder.
 *
 * icon only and memory only requests never make a thumbnail, so asking for only a thumbnail then fails with E_PENDING.
 * otherwise the thumbnail is tried first and the icon stands in, unless only the thumbnail was wanted, which returns
 * the extraction failure. a memory only request answered with an icon returns S_FALSE.
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
