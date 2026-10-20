/*
 * IWICBitmap: pixels in memory, and the locks that give programs the memory to read or write.
 */
#include "wic_private.h"
#include "convert.h"
#include <mime/win32/wingdi.h>
#include <mime/win32/winuser.h>

typedef struct WicBitmap {
    WicBase base;
    WicSlot bitmap;
    CRITICAL_SECTION lock;
    UINT width;
    UINT height;
    GUID format;
    BYTE *pixels;
    UINT stride;
    UINT size;
    BYTE *owned;     /* the allocation, or NULL for memory that is not ours. */
    HANDLE view;     /* the mapped view of a section, unmapped at the end. */
    double dpi_x;
    double dpi_y;
    IWICPalette *palette;
    LONG readers;
    BOOL writer;
} WicBitmap;

typedef struct WicBitmapLock {
    WicBase base;
    WicSlot lock;
    WicBitmap *bitmap;
    BOOL write;
    UINT width;
    UINT height;
    UINT stride;
    UINT size;
    BYTE *data;
} WicBitmapLock;

static WicBitmap *bitmap_from(IWICBitmap *This) { return WIC_FROM_SLOT(WicBitmap, bitmap, This); }

/* ------------------------------------------------------------------ rectangles */

HRESULT WicCheckRect(const WICRect *rect, UINT width, UINT height, WICRect *out)
{
    if (!rect) {
        out->X = out->Y = 0;
        out->Width = (INT)width;
        out->Height = (INT)height;
        return S_OK;
    }
    if (rect->X < 0 || rect->Y < 0 || rect->Width < 0 || rect->Height < 0)
        return E_INVALIDARG;
    if ((UINT)rect->X > width || (UINT)rect->Y > height || (UINT)rect->Width > width - (UINT)rect->X ||
        (UINT)rect->Height > height - (UINT)rect->Y)
        return E_INVALIDARG;
    *out = *rect;
    return S_OK;
}

HRESULT WicCheckBuffer(const WICRect *rect, const GUID *format, UINT stride, UINT size, BYTE *buffer)
{
    UINT row;
    if (!buffer)
        return E_INVALIDARG;
    if (!rect->Width || !rect->Height)
        return S_OK;
    if (!WicRowBytes(format, (UINT)rect->Width, &row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (stride < row)
        return E_INVALIDARG;
    UINT64 need = (UINT64)stride * (UINT)(rect->Height - 1) + row;
    if (need > 0xFFFFFFFFull)
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (size < need)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    return S_OK;
}

/* copies a rectangle of bits, a start that is not on a byte is shifted into place. */
void WicCopyBits(BYTE *dest, UINT dest_stride, const BYTE *src, UINT src_stride, UINT x, UINT y, UINT width,
                 UINT height, UINT bpp)
{
    if (bpp >= 8) {
        UINT bytes = bpp / 8;
        for (UINT row = 0; row < height; row++)
            memcpy(dest + (SIZE_T)row * dest_stride, src + (SIZE_T)(y + row) * src_stride + (SIZE_T)x * bytes,
                   (SIZE_T)width * bytes);
        return;
    }
    UINT start_bit = x * bpp;
    UINT row_bits = width * bpp;
    UINT row_bytes = (row_bits + 7) / 8;
    for (UINT row = 0; row < height; row++) {
        const BYTE *s = src + (SIZE_T)(y + row) * src_stride + start_bit / 8;
        BYTE *d = dest + (SIZE_T)row * dest_stride;
        UINT shift = start_bit & 7;
        if (!shift) {
            memcpy(d, s, row_bytes);
        } else {
            UINT used = shift + row_bits;
            for (UINT i = 0; i < row_bytes; i++) {
                UINT hi = s[i];
                UINT next = (i + 1) * 8 < used ? s[i + 1] : 0;
                d[i] = (BYTE)((hi << shift) | (next >> (8 - shift)));
            }
        }
        /* clear the unused bits at the end of the row. */
        if (row_bits & 7)
            d[row_bytes - 1] &= (BYTE)(0xff << (8 - (row_bits & 7)));
    }
}

/* ------------------------------------------------------------------ locks */

static WicBitmapLock *lock_from(IWICBitmapLock *This) { return WIC_FROM_SLOT(WicBitmapLock, lock, This); }

static void lock_destroy(WicBase *base)
{
    WicBitmapLock *lock = (WicBitmapLock *)base;
    WicBitmap *bitmap = lock->bitmap;
    EnterCriticalSection(&bitmap->lock);
    if (lock->write)
        bitmap->writer = FALSE;
    else
        bitmap->readers--;
    LeaveCriticalSection(&bitmap->lock);
    WicRelease(&bitmap->bitmap);
}

static HRESULT STDMETHODCALLTYPE lock_get_size(IWICBitmapLock *This, UINT *width, UINT *height)
{
    WicBitmapLock *lock = lock_from(This);
    if (!width || !height)
        return E_INVALIDARG;
    *width = lock->width;
    *height = lock->height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lock_get_stride(IWICBitmapLock *This, UINT *stride)
{
    WicBitmapLock *lock = lock_from(This);
    if (!stride)
        return E_INVALIDARG;
    *stride = lock->stride;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lock_get_data(IWICBitmapLock *This, UINT *size, BYTE **data)
{
    WicBitmapLock *lock = lock_from(This);
    if (!size || !data)
        return E_INVALIDARG;
    *size = lock->size;
    *data = lock->data;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lock_get_format(IWICBitmapLock *This, WICPixelFormatGUID *format)
{
    WicBitmapLock *lock = lock_from(This);
    if (!format)
        return E_INVALIDARG;
    *format = lock->bitmap->format;
    return S_OK;
}

static const IWICBitmapLockVtbl g_lock_vtbl = {WIC_IUNKNOWN_FUNCS, lock_get_size, lock_get_stride, lock_get_data,
                                               lock_get_format};
static const WicIfaceEntry g_lock_ifaces[] = {WIC_ENTRY(WicBitmapLock, IID_IWICBitmapLock, lock)};
static const WicClass g_lock_class = {lock_destroy, g_lock_ifaces, 1};

/* ------------------------------------------------------------------ the bitmap */

static void bitmap_destroy(WicBase *base)
{
    WicBitmap *bitmap = (WicBitmap *)base;
    if (bitmap->palette)
        bitmap->palette->lpVtbl->Release(bitmap->palette);
    if (bitmap->view)
        UnmapViewOfFile(bitmap->view);
    WicFree(bitmap->owned);
    DeleteCriticalSection(&bitmap->lock);
}

static HRESULT STDMETHODCALLTYPE bitmap_get_size(IWICBitmap *This, UINT *width, UINT *height)
{
    WicBitmap *bitmap = bitmap_from(This);
    if (!width || !height)
        return E_INVALIDARG;
    *width = bitmap->width;
    *height = bitmap->height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_get_format(IWICBitmap *This, WICPixelFormatGUID *format)
{
    WicBitmap *bitmap = bitmap_from(This);
    if (!format)
        return E_INVALIDARG;
    *format = bitmap->format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_get_resolution(IWICBitmap *This, double *dpi_x, double *dpi_y)
{
    WicBitmap *bitmap = bitmap_from(This);
    if (!dpi_x || !dpi_y)
        return E_INVALIDARG;
    EnterCriticalSection(&bitmap->lock);
    *dpi_x = bitmap->dpi_x;
    *dpi_y = bitmap->dpi_y;
    LeaveCriticalSection(&bitmap->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_copy_palette(IWICBitmap *This, IWICPalette *palette)
{
    WicBitmap *bitmap = bitmap_from(This);
    if (!palette)
        return E_INVALIDARG;
    HRESULT hr;
    EnterCriticalSection(&bitmap->lock);
    if (!bitmap->palette)
        hr = WINCODEC_ERR_PALETTEUNAVAILABLE;
    else
        hr = palette->lpVtbl->InitializeFromPalette(palette, bitmap->palette);
    LeaveCriticalSection(&bitmap->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bitmap_copy_pixels(IWICBitmap *This, const WICRect *rect, UINT stride, UINT size,
                                                    BYTE *buffer)
{
    WicBitmap *bitmap = bitmap_from(This);
    WICRect r;
    if (!buffer)
        return E_INVALIDARG;
    HRESULT hr = WicCheckRect(rect, bitmap->width, bitmap->height, &r);
    if (SUCCEEDED(hr))
        hr = WicCheckBuffer(&r, &bitmap->format, stride, size, buffer);
    if (FAILED(hr))
        return hr;
    if (!r.Width || !r.Height)
        return S_OK;
    EnterCriticalSection(&bitmap->lock);
    if (bitmap->writer) {
        LeaveCriticalSection(&bitmap->lock);
        return WINCODEC_ERR_ALREADYLOCKED;
    }
    WicCopyBits(buffer, stride, bitmap->pixels, bitmap->stride, (UINT)r.X, (UINT)r.Y, (UINT)r.Width, (UINT)r.Height,
                WicFormatBits(&bitmap->format));
    LeaveCriticalSection(&bitmap->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_lock(IWICBitmap *This, const WICRect *rect, DWORD flags,
                                             IWICBitmapLock **out)
{
    WicBitmap *bitmap = bitmap_from(This);
    WICRect r;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!(flags & (WICBitmapLockRead | WICBitmapLockWrite)) || (flags & ~(DWORD)(WICBitmapLockRead | WICBitmapLockWrite)))
        return E_INVALIDARG;
    HRESULT hr = WicCheckRect(rect, bitmap->width, bitmap->height, &r);
    if (FAILED(hr))
        return hr;
    if (!r.Width || !r.Height)
        return E_INVALIDARG;
    UINT bpp = WicFormatBits(&bitmap->format);
    if ((UINT)r.X * bpp % 8)
        return E_INVALIDARG;
    UINT row;
    if (!WicRowBytes(&bitmap->format, (UINT)r.Width, &row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    BOOL write = (flags & WICBitmapLockWrite) != 0;
    EnterCriticalSection(&bitmap->lock);
    if (bitmap->writer || (write && bitmap->readers)) {
        LeaveCriticalSection(&bitmap->lock);
        return WINCODEC_ERR_ALREADYLOCKED;
    }
    WicBitmapLock *lock = WicNew(sizeof(*lock), &g_lock_class);
    if (!lock) {
        LeaveCriticalSection(&bitmap->lock);
        return E_OUTOFMEMORY;
    }
    if (write)
        bitmap->writer = TRUE;
    else
        bitmap->readers++;
    LeaveCriticalSection(&bitmap->lock);
    WIC_SLOT(lock, lock, &g_lock_vtbl);
    WicAddRef(&bitmap->bitmap);
    lock->bitmap = bitmap;
    lock->write = write;
    lock->width = (UINT)r.Width;
    lock->height = (UINT)r.Height;
    lock->stride = bitmap->stride;
    lock->size = (UINT)((UINT64)bitmap->stride * (UINT)(r.Height - 1) + row);
    lock->data = bitmap->pixels + (SIZE_T)r.Y * bitmap->stride + (SIZE_T)r.X * bpp / 8;
    *out = (IWICBitmapLock *)&lock->lock;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_set_palette(IWICBitmap *This, IWICPalette *palette)
{
    WicBitmap *bitmap = bitmap_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!WicFormatIsIndexed(&bitmap->format))
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICPalette *copy;
    HRESULT hr = WicPaletteCreate(&copy);
    if (FAILED(hr))
        return hr;
    hr = copy->lpVtbl->InitializeFromPalette(copy, palette);
    if (FAILED(hr)) {
        copy->lpVtbl->Release(copy);
        return hr;
    }
    EnterCriticalSection(&bitmap->lock);
    IWICPalette *old = bitmap->palette;
    bitmap->palette = copy;
    LeaveCriticalSection(&bitmap->lock);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bitmap_set_resolution(IWICBitmap *This, double dpi_x, double dpi_y)
{
    WicBitmap *bitmap = bitmap_from(This);
    EnterCriticalSection(&bitmap->lock);
    bitmap->dpi_x = dpi_x;
    bitmap->dpi_y = dpi_y;
    LeaveCriticalSection(&bitmap->lock);
    return S_OK;
}

static const IWICBitmapVtbl g_bitmap_vtbl = {
    WIC_IUNKNOWN_FUNCS, bitmap_get_size, bitmap_get_format, bitmap_get_resolution, bitmap_copy_palette,
    bitmap_copy_pixels, bitmap_lock,     bitmap_set_palette, bitmap_set_resolution,
};

static const WicIfaceEntry g_bitmap_ifaces[] = {
    WIC_ENTRY(WicBitmap, IID_IWICBitmap, bitmap),
    WIC_ENTRY(WicBitmap, IID_IWICBitmapSource, bitmap),
};
static const WicClass g_bitmap_class = {bitmap_destroy, g_bitmap_ifaces, 2};

static UINT aligned_stride(UINT row_bytes) { return (row_bytes + 3) & ~3u; }

/* a bitmap of the size, over the memory when given, copied into a new allocation unless it is a mapped view. */
HRESULT WicBitmapCreate(UINT width, UINT height, const GUID *format, BYTE *memory, UINT stride, UINT size,
                        BOOL copy, IWICBitmap **out)
{
    *out = nullptr;
    if (!width || !height)
        return E_INVALIDARG;
    if (!WicFindFormat(format) || IsEqualGUID(format, &GUID_WICPixelFormatDontCare))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    UINT row;
    if (!WicRowBytes(format, width, &row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    UINT own_stride = aligned_stride(row), total;
    if (own_stride < row || !WicMul32(own_stride, height, &total))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    WicBitmap *bitmap = WicNew(sizeof(*bitmap), &g_bitmap_class);
    if (!bitmap)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&bitmap->lock);
    WIC_SLOT(bitmap, bitmap, &g_bitmap_vtbl);
    bitmap->width = width;
    bitmap->height = height;
    bitmap->format = *format;
    bitmap->dpi_x = bitmap->dpi_y = 96.0;
    if (memory && !copy) {
        bitmap->pixels = memory;
        bitmap->stride = stride;
        bitmap->size = size;
    } else {
        bitmap->owned = WicAllocZero(total);
        if (!bitmap->owned) {
            WicRelease(&bitmap->bitmap);
            return E_OUTOFMEMORY;
        }
        bitmap->pixels = bitmap->owned;
        bitmap->stride = own_stride;
        bitmap->size = total;
        if (memory) {
            for (UINT y = 0; y < height; y++)
                memcpy(bitmap->owned + (SIZE_T)y * own_stride, memory + (SIZE_T)y * stride, row);
        }
    }
    *out = (IWICBitmap *)&bitmap->bitmap;
    return S_OK;
}

BYTE *WicBitmapPixels(IWICBitmap *bitmap, UINT *stride)
{
    WicBitmap *b = bitmap_from(bitmap);
    *stride = b->stride;
    return b->pixels;
}

HRESULT WicBitmapFromSource(IWICBitmapSource *source, const WICRect *rect, IWICBitmap **out)
{
    *out = nullptr;
    UINT width, height;
    GUID format;
    HRESULT hr = source->lpVtbl->GetSize(source, &width, &height);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetPixelFormat(source, &format);
    if (FAILED(hr))
        return hr;
    WICRect r;
    hr = WicCheckRect(rect, width, height, &r);
    if (FAILED(hr))
        return hr;
    IWICBitmap *bitmap;
    hr = WicBitmapCreate((UINT)r.Width, (UINT)r.Height, &format, nullptr, 0, 0, FALSE, &bitmap);
    if (FAILED(hr))
        return hr;
    WicBitmap *b = bitmap_from(bitmap);
    hr = source->lpVtbl->CopyPixels(source, &r, b->stride, b->size, b->pixels);
    if (FAILED(hr)) {
        bitmap->lpVtbl->Release(bitmap);
        return hr;
    }
    double dx = 96.0, dy = 96.0;
    if (SUCCEEDED(source->lpVtbl->GetResolution(source, &dx, &dy)))
        bitmap->lpVtbl->SetResolution(bitmap, dx, dy);
    if (WicFormatIsIndexed(&format)) {
        IWICPalette *palette;
        if (SUCCEEDED(WicPaletteCreate(&palette))) {
            if (SUCCEEDED(source->lpVtbl->CopyPalette(source, palette)))
                bitmap->lpVtbl->SetPalette(bitmap, palette);
            palette->lpVtbl->Release(palette);
        }
    }
    *out = bitmap;
    return S_OK;
}

/* ------------------------------------------------------------------ bitmaps of the system */

HRESULT WicBitmapFromSection(UINT width, UINT height, const GUID *format, HANDLE section, UINT stride, UINT offset,
                             DWORD access, IWICBitmap **out)
{
    *out = nullptr;
    if (!section || !width || !height)
        return E_INVALIDARG;
    UINT row;
    if (!WicFindFormat(format))
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!WicRowBytes(format, width, &row))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    if (!stride)
        stride = aligned_stride(row);
    UINT64 need = (UINT64)stride * (height - 1) + row;
    if (stride < row || need > 0xFFFFFFFFull)
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    UINT granularity = info.dwAllocationGranularity;
    UINT base = offset - offset % granularity;
    BYTE *view = MapViewOfFile(section, access == WICSectionAccessLevelRead ? FILE_MAP_READ : FILE_MAP_WRITE, 0, base,
                               (SIZE_T)(need + (offset - base)));
    if (!view)
        return HRESULT_FROM_WIN32(GetLastError());
    IWICBitmap *bitmap;
    HRESULT hr = WicBitmapCreate(width, height, format, view + (offset - base), stride, (UINT)need, FALSE, &bitmap);
    if (FAILED(hr)) {
        UnmapViewOfFile(view);
        return hr;
    }
    bitmap_from(bitmap)->view = view;
    *out = bitmap;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WICCreateBitmapFromSection(UINT width, UINT height, REFWICPixelFormatGUID format,
                                                          HANDLE section, UINT stride, UINT offset,
                                                          IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    return WicBitmapFromSection(width, height, format, section, stride, offset, WICSectionAccessLevelReadWrite, bitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI WICCreateBitmapFromSectionEx(UINT width, UINT height, REFWICPixelFormatGUID format,
                                                            HANDLE section, UINT stride, UINT offset,
                                                            WICSectionAccessLevel access, IWICBitmap **bitmap)
{
    if (!bitmap)
        return E_INVALIDARG;
    if (access != WICSectionAccessLevelRead && access != WICSectionAccessLevelReadWrite)
        return E_INVALIDARG;
    return WicBitmapFromSection(width, height, format, section, stride, offset, access, bitmap);
}

HRESULT WicBitmapFromHBITMAP(HBITMAP hbitmap, HPALETTE hpalette, WICBitmapAlphaChannelOption option,
                             IWICBitmap **out)
{
    *out = nullptr;
    BITMAP info;
    if (!hbitmap || !GetObjectW(hbitmap, sizeof(info), &info))
        return E_INVALIDARG;
    if (info.bmWidth <= 0 || info.bmHeight <= 0)
        return E_INVALIDARG;
    UINT width = (UINT)info.bmWidth, height = (UINT)info.bmHeight;
    const GUID *format = option == WICBitmapUseAlpha ? &GUID_WICPixelFormat32bppBGRA
                         : option == WICBitmapUsePremultipliedAlpha ? &GUID_WICPixelFormat32bppPBGRA
                                                                      : &GUID_WICPixelFormat32bppBGR;
    if (option > WICBitmapIgnoreAlpha)
        return E_INVALIDARG;
    IWICBitmap *bitmap;
    HRESULT hr = WicBitmapCreate(width, height, format, nullptr, 0, 0, FALSE, &bitmap);
    if (FAILED(hr))
        return hr;
    UINT stride;
    BYTE *pixels = WicBitmapPixels(bitmap, &stride);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = -(LONG)height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    HPALETTE old_palette = nullptr;
    if (!dc) {
        bitmap->lpVtbl->Release(bitmap);
        return E_FAIL;
    }
    if (hpalette)
        old_palette = SelectPalette(dc, hpalette, FALSE);
    BOOL ok = TRUE;
    /* the rows come in with the stride of the bitmap, so ask for them one at a time. */
    BYTE *packed = WicAlloc((SIZE_T)width * height * 4);
    if (!packed) {
        ok = FALSE;
    } else {
        ok = GetDIBits(dc, hbitmap, 0, height, packed, &bmi, DIB_RGB_COLORS) != 0;
        if (ok) {
            BOOL has_alpha = FALSE;
            if (info.bmBitsPixel == 32) {
                for (SIZE_T i = 0; i < (SIZE_T)width * height && !has_alpha; i++)
                    has_alpha = packed[i * 4 + 3] != 0;
            }
            for (UINT y = 0; y < height; y++) {
                BYTE *d = pixels + (SIZE_T)y * stride;
                const BYTE *s = packed + (SIZE_T)y * width * 4;
                for (UINT x = 0; x < width; x++) {
                    BYTE a = (option == WICBitmapIgnoreAlpha || !has_alpha) ? 255 : s[x * 4 + 3];
                    BYTE b = s[x * 4], g = s[x * 4 + 1], r = s[x * 4 + 2];
                    if (option == WICBitmapUseAlpha && info.bmBitsPixel == 32 && has_alpha && a != 255 && a) {
                        /* a 32 bit bitmap holds premultiplied color, which straight alpha wants undone. */
                        r = WicStraightChannel(r, a);
                        g = WicStraightChannel(g, a);
                        b = WicStraightChannel(b, a);
                    }
                    d[x * 4] = b;
                    d[x * 4 + 1] = g;
                    d[x * 4 + 2] = r;
                    d[x * 4 + 3] = option == WICBitmapIgnoreAlpha ? 255 : a;
                }
            }
        }
        WicFree(packed);
    }
    if (old_palette)
        SelectPalette(dc, old_palette, FALSE);
    DeleteDC(dc);
    if (!ok) {
        bitmap->lpVtbl->Release(bitmap);
        return E_FAIL;
    }
    *out = bitmap;
    return S_OK;
}

HRESULT WicBitmapFromHICON(HICON icon, IWICBitmap **out)
{
    *out = nullptr;
    ICONINFO info;
    if (!icon || !GetIconInfo(icon, &info))
        return E_INVALIDARG;
    BITMAP color, mask;
    HRESULT hr = E_FAIL;
    BOOL has_color = info.hbmColor != nullptr;
    if (!GetObjectW(has_color ? info.hbmColor : info.hbmMask, sizeof(color), &color))
        goto done;
    UINT width = (UINT)color.bmWidth, height = (UINT)color.bmHeight;
    if (!has_color)
        height /= 2;
    if (!width || !height)
        goto done;
    {
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)width;
        bmi.bmiHeader.biHeight = -(LONG)(has_color ? height : height * 2);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        BYTE *colors = WicAllocZero((SIZE_T)width * height * 8);
        BYTE *masks = WicAllocZero((SIZE_T)width * height * 4);
        HDC dc = CreateCompatibleDC(nullptr);
        if (colors && masks && dc) {
            BOOL ok;
            if (has_color) {
                ok = GetDIBits(dc, info.hbmColor, 0, height, colors, &bmi, DIB_RGB_COLORS) != 0;
                BITMAPINFO mi = bmi;
                mi.bmiHeader.biHeight = -(LONG)height;
                if (ok && info.hbmMask)
                    GetDIBits(dc, info.hbmMask, 0, height, masks, &mi, DIB_RGB_COLORS);
            } else {
                ok = GetDIBits(dc, info.hbmMask, 0, height * 2, colors, &bmi, DIB_RGB_COLORS) != 0;
            }
            if (ok) {
                BOOL alpha = FALSE;
                if (has_color) {
                    for (SIZE_T i = 0; i < (SIZE_T)width * height && !alpha; i++)
                        alpha = colors[i * 4 + 3] != 0;
                }
                IWICBitmap *bitmap;
                hr = WicBitmapCreate(width, height, &GUID_WICPixelFormat32bppPBGRA, nullptr, 0, 0, FALSE, &bitmap);
                if (SUCCEEDED(hr)) {
                    UINT stride;
                    BYTE *pixels = WicBitmapPixels(bitmap, &stride);
                    for (UINT y = 0; y < height; y++) {
                        BYTE *d = pixels + (SIZE_T)y * stride;
                        for (UINT x = 0; x < width; x++) {
                            SIZE_T i = (SIZE_T)y * width + x;
                            BYTE b, g, r, a;
                            if (has_color) {
                                b = colors[i * 4];
                                g = colors[i * 4 + 1];
                                r = colors[i * 4 + 2];
                                a = alpha ? colors[i * 4 + 3] : (masks[i * 4] ? 0 : 255);
                                if (!alpha && !a)
                                    b = g = r = 0;
                            } else {
                                /* a monochrome icon has the and mask above the xor mask. */
                                BYTE and_bit = colors[i * 4] ? 1 : 0;
                                BYTE xor_bit = colors[(i + (SIZE_T)width * height) * 4] ? 1 : 0;
                                a = (and_bit && !xor_bit) ? 0 : 255;
                                b = g = r = xor_bit ? 255 : 0;
                            }
                            if (alpha && a != 255) {
                                /* the color of an icon with alpha is already premultiplied by the system. */
                            }
                            d[x * 4] = b;
                            d[x * 4 + 1] = g;
                            d[x * 4 + 2] = r;
                            d[x * 4 + 3] = a;
                        }
                    }
                    *out = bitmap;
                }
            }
        } else {
            hr = E_OUTOFMEMORY;
        }
        if (dc)
            DeleteDC(dc);
        WicFree(colors);
        WicFree(masks);
    }
done:
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    (void)mask;
    return hr;
}
