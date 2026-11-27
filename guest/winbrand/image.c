#include "winbrand_private.h"
#include <mime/win32/objbase.h>
#include <mime/win32/wincodec.h>

#define LOGPIXELSX 88
#define RESOURCE_BITMAP 2

int _fltused = 0x9875;

typedef HRESULT(WINAPI *CO_CREATE)(const CLSID *, IUnknown *, DWORD, const IID *, void **);
typedef HRESULT(WINAPI *CO_INIT)(void *, DWORD);
typedef void(WINAPI *CO_UNINIT)(void);
typedef IStream *(WINAPI *MEM_STREAM)(const BYTE *, UINT);

static void release(void *object)
{
    IUnknown *unknown = object;
    if (unknown)
        unknown->lpVtbl->Release(unknown);
}

static HRESULT convert(IWICImagingFactory *factory, IWICBitmapSource *source, const GUID *format,
                       IWICBitmapSource **out)
{
    IWICFormatConverter *converter = nullptr;
    *out = nullptr;
    HRESULT hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr))
        hr = converter->lpVtbl->Initialize(converter, source, format, WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = converter->lpVtbl->QueryInterface(converter, &IID_IWICBitmapSource, (void **)out);
    release(converter);
    return hr;
}

/* the wider side keeps the size asked for, the other follows the picture's shape. */
static HRESULT scale(IWICImagingFactory *factory, IWICBitmapSource *source, int cx, int cy, IWICBitmapSource **out)
{
    IWICBitmapSource *premultiplied = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    UINT width = 0, height = 0;
    *out = nullptr;
    HRESULT hr = convert(factory, source, &GUID_WICPixelFormat32bppPBGRA, &premultiplied);
    if (SUCCEEDED(hr))
        hr = premultiplied->lpVtbl->GetSize(premultiplied, &width, &height);
    if (SUCCEEDED(hr)) {
        if (width <= height) {
            cx = MulDiv(cx, (int)width, (int)height);
            if (cx <= 1)
                cx = 1;
        } else {
            cy = MulDiv(cy, (int)height, (int)width);
            if (cy <= 1)
                cy = 1;
        }
        hr = factory->lpVtbl->CreateBitmapScaler(factory, &scaler);
    }
    if (SUCCEEDED(hr))
        hr = scaler->lpVtbl->Initialize(scaler, premultiplied, (UINT)cx, (UINT)cy, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr)) {
        *out = (IWICBitmapSource *)scaler;
        scaler = nullptr;
    }
    release(scaler);
    release(premultiplied);
    return hr;
}

static HRESULT to_bitmap(IWICImagingFactory *factory, IWICBitmapSource *source, HBITMAP *out)
{
    IWICBitmapSource *pixels = nullptr;
    UINT width = 0, height = 0;
    HRESULT hr = convert(factory, source, &GUID_WICPixelFormat32bppBGRA, &pixels);
    if (SUCCEEDED(hr))
        hr = pixels->lpVtbl->GetSize(pixels, &width, &height);
    if (SUCCEEDED(hr)) {
        BITMAPINFO info = {0};
        void *bits = nullptr;
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = (LONG)width;
        info.bmiHeader.biHeight = -(LONG)height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) {
            hr = E_OUTOFMEMORY;
        } else {
            WICRect all = {0, 0, (INT)width, (INT)height};
            hr = pixels->lpVtbl->CopyPixels(pixels, &all, 4 * width, 4 * width * height, bits);
            if (FAILED(hr))
                DeleteObject(bitmap);
            else
                *out = bitmap;
        }
    }
    release(pixels);
    return hr;
}

/* a picture of the brand is a file of type IMAGE that the imaging component decodes, made a 32-bit section. */
static HRESULT load_picture(HMODULE module, UINT id, int cx, int cy, HBITMAP *out)
{
    *out = nullptr;
    HMODULE ole = LoadLibraryExW(L"ole32.dll", nullptr, 0x800);
    HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, 0x800);
    CO_CREATE create = ole ? (CO_CREATE)GetProcAddress(ole, "CoCreateInstance") : nullptr;
    CO_INIT init = ole ? (CO_INIT)GetProcAddress(ole, "CoInitializeEx") : nullptr;
    CO_UNINIT uninit = ole ? (CO_UNINIT)GetProcAddress(ole, "CoUninitialize") : nullptr;
    MEM_STREAM stream_of = shcore ? (MEM_STREAM)GetProcAddress(shcore, "SHCreateMemStream") : nullptr;
    if (!create || !init || !uninit || !stream_of)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    HRESULT hr = init(nullptr, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE)
        hr = init(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return hr;
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapSource *source = nullptr, *scaled = nullptr;
    IStream *stream = nullptr;
    hr = create(&CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) {
        HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), L"IMAGE");
        HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
        const BYTE *data = loaded ? LockResource(loaded) : nullptr;
        DWORD size = data ? SizeofResource(module, found) : 0;
        if (!size)
            hr = HRESULT_FROM_WIN32(GetLastError());
        else if (!(stream = stream_of(data, size)))
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = factory->lpVtbl->CreateDecoderFromStream(factory, stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                                      &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr))
        hr = frame->lpVtbl->QueryInterface(frame, &IID_IWICBitmapSource, (void **)&source);
    if (SUCCEEDED(hr) && cx > 0 && cy > 0) {
        hr = scale(factory, source, cx, cy, &scaled);
        if (SUCCEEDED(hr)) {
            release(source);
            source = scaled;
        }
    }
    if (SUCCEEDED(hr))
        hr = to_bitmap(factory, source, out);
    release(source);
    release(frame);
    release(decoder);
    release(stream);
    release(factory);
    uninit();
    return hr;
}

/* a bitmap asked for by id is first the picture drawn for the density of the screen: 1000 more at 120, 2000 at 144. */
DECLSPEC_EXPORT HANDLE WINAPI BrandingLoadImage(const WCHAR *brand, ULONG_PTR id, UINT type, int cx, int cy, UINT load)
{
    ULONG_PTR resource_type = type == IMAGE_ICON ? 3 : type == IMAGE_CURSOR ? 1 : RESOURCE_BITMAP;
    DWORD edition = brand_edition();
    HMODULE module = brand_open(brand, resource_type, id);
    if (!module)
        return nullptr;
    UINT mapped = 0;
    HANDLE image = nullptr;
    if (FAILED(brand_remap(module, brand, (USHORT)id, edition, &mapped)))
        goto done;
    if (resource_type == RESOURCE_BITMAP && id < 0x10000) {
        HDC dc = GetDC(nullptr);
        if (dc) {
            int density = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(nullptr, dc);
            if (density >= 144)
                mapped += 2000;
            else if (density >= 120)
                mapped += 1000;
        }
        HBITMAP bitmap = nullptr;
        if (SUCCEEDED(load_picture(module, mapped, cx, cy, &bitmap)) && bitmap) {
            image = bitmap;
            goto done;
        }
    }
    image = LoadImageW(module, MAKEINTRESOURCEW((USHORT)mapped), type, cx, cy, load);
done:
    FreeLibrary(module);
    return image;
}

DECLSPEC_EXPORT HBITMAP WINAPI BrandingLoadBitmap(const WCHAR *brand, ULONG_PTR id)
{
    return BrandingLoadImage(brand, id, IMAGE_BITMAP, 0, 0, 0);
}

DECLSPEC_EXPORT HCURSOR WINAPI BrandingLoadCursor(const WCHAR *brand, ULONG_PTR id)
{
    return BrandingLoadImage(brand, id, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

DECLSPEC_EXPORT HICON WINAPI BrandingLoadIcon(const WCHAR *brand, ULONG_PTR id)
{
    return BrandingLoadImage(brand, id, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}
