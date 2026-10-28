/*
 * the default IGetFrame: the frames of a video stream as device independent bitmaps. A stream of uncompressed
 * frames is read as it is; run length encoded frames are expanded here. Any other compression needs a decompressor
 * that mime does not have, and then the frames cannot be opened.
 */
#include "avifil32_private.h"

#define NO_FRAME (-4224)

typedef struct FrameDef {
    const IGetFrameVtbl *lpVtbl;
    LONG refs;
    PAVISTREAM stream;
    BOOL streaming;
    BOOL changes;
    BYTE *dib;
    DWORD header_size;
    DWORD image_size;
    BYTE *raw;
    DWORD raw_capacity;
    DWORD native_compression;
    DWORD nominal_size;
    LONG current;
} FrameDef;

DWORD AviDibStride(DWORD width, DWORD bits)
{
    return (width * bits + 31) / 32 * 4;
}

static void frame_release_buffers(FrameDef *g)
{
    AviFree(g->dib);
    AviFree(g->raw);
    g->dib = g->raw = nullptr;
    g->current = NO_FRAME;
}

/* expands a run length encoded frame of 8 bits into the bitmap, which keeps what the frame skips. */
static void decode_rle8(const BYTE *code, DWORD size, BYTE *bits, LONG width, LONG height, DWORD stride)
{
    LONG x = 0, y = 0;
    DWORD i = 0;
    while (i + 1 < size) {
        DWORD count = code[i], value = code[i + 1];
        i += 2;
        if (count) {
            for (DWORD k = 0; k < count; k++, x++)
                if (x < width && y < height)
                    bits[(SIZE_T)y * stride + (SIZE_T)x] = (BYTE)value;
            continue;
        }
        if (value == 0) {
            x = 0;
            y++;
        } else if (value == 1) {
            return;
        } else if (value == 2) {
            if (i + 2 > size)
                return;
            x += code[i];
            y += code[i + 1];
            i += 2;
        } else {
            if (i + value > size)
                value = size - i;
            for (DWORD k = 0; k < value; k++, x++)
                if (x < width && y < height)
                    bits[(SIZE_T)y * stride + (SIZE_T)x] = code[i + k];
            i += value + (value & 1);
        }
    }
}

static HRESULT STDMETHODCALLTYPE frame_qi(IGetFrame *This, REFIID riid, void **object)
{
    FrameDef *g = (FrameDef *)This;
    if (IsEqualGUID(riid, &IID_IGetFrame) || IsEqualGUID(riid, &IID_IUnknown)) {
        *object = This;
        InterlockedIncrement(&g->refs);
        return S_OK;
    }
    if (g->stream)
        return g->stream->lpVtbl->QueryInterface(g->stream, riid, object);
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE frame_addref(IGetFrame *This)
{
    return (ULONG)InterlockedIncrement(&((FrameDef *)This)->refs);
}

static ULONG STDMETHODCALLTYPE frame_release(IGetFrame *This)
{
    FrameDef *g = (FrameDef *)This;
    LONG refs = InterlockedDecrement(&g->refs);
    if (!refs) {
        frame_release_buffers(g);
        if (g->stream)
            g->stream->lpVtbl->Release(g->stream);
        AviUseAdd(-1);
        AviFree(g);
    }
    return (ULONG)refs;
}

/* the frame at a position is the one that last changed the picture, the key frame and the frames after it. */
static void *STDMETHODCALLTYPE frame_get(IGetFrame *This, LONG pos)
{
    FrameDef *g = (FrameDef *)This;
    if (!g->stream || !g->dib)
        return nullptr;
    if (g->current == pos)
        return g->dib;
    LONG key = g->stream->lpVtbl->FindSample(g->stream, pos, FIND_PREV | FIND_KEY);
    if (key == -1)
        return nullptr;
    LONG at = key;
    if (g->current < pos && g->current >= key)
        at = g->current + 1;
    BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)g->dib;
    BYTE *bits = g->dib + g->header_size;
    for (; at <= pos; at++) {
        LONG bytes = 0, samples = 0;
        if (g->changes) {
            LONG size = (LONG)g->header_size;
            DWORD stored_size = bih->biSizeImage, stored_compression = bih->biCompression;
            g->stream->lpVtbl->ReadFormat(g->stream, at, g->dib, &size);
            bih->biSizeImage = stored_size;
            bih->biCompression = stored_compression;
        }
        BYTE *into = g->native_compression == BI_RLE8 ? g->raw : bits;
        DWORD capacity = g->native_compression == BI_RLE8 ? g->raw_capacity : g->image_size;
        HRESULT hr = g->stream->lpVtbl->Read(g->stream, at, 1, into, (LONG)capacity, &bytes, &samples);
        if (hr == AVIERR_BUFFERTOOSMALL && bytes > (LONG)capacity) {
            if (g->native_compression == BI_RLE8) {
                BYTE *grown = AviRealloc(g->raw, (SIZE_T)bytes);
                if (!grown)
                    return nullptr;
                g->raw = grown;
                g->raw_capacity = (DWORD)bytes;
                hr = g->stream->lpVtbl->Read(g->stream, at, 1, g->raw, bytes, &bytes, &samples);
            } else {
                BYTE *grown = AviRealloc(g->dib, (SIZE_T)g->header_size + (SIZE_T)bytes);
                if (!grown)
                    return nullptr;
                g->dib = grown;
                bih = (BITMAPINFOHEADER *)grown;
                bits = grown + g->header_size;
                g->image_size = (DWORD)bytes;
                hr = g->stream->lpVtbl->Read(g->stream, at, 1, bits, bytes, &bytes, &samples);
            }
        }
        if (FAILED(hr) || samples != 1)
            return nullptr;
        if (!bytes)
            continue;
        if (g->native_compression == BI_RLE8) {
            decode_rle8(g->raw, (DWORD)bytes, bits, bih->biWidth, bih->biHeight < 0 ? -bih->biHeight : bih->biHeight,
                        AviDibStride((DWORD)bih->biWidth, bih->biBitCount));
        } else {
            /* a frame shorter than the picture is no frame. */
            if (g->nominal_size > (DWORD)bytes)
                return nullptr;
            bih->biSizeImage = (DWORD)bytes;
        }
    }
    g->current = pos;
    return g->dib;
}

static HRESULT STDMETHODCALLTYPE frame_begin(IGetFrame *This, LONG start, LONG end, LONG rate)
{
    FrameDef *g = (FrameDef *)This;
    g->streaming = TRUE;
    frame_get(This, start);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_end(IGetFrame *This)
{
    ((FrameDef *)This)->streaming = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_setformat(IGetFrame *This, BITMAPINFOHEADER *wanted, void *bits, int x, int y,
                                                 int dx, int dy)
{
    FrameDef *g = (FrameDef *)This;
    AVISTREAMINFOW info = {0};
    LONG size = 0;
    if (wanted == (BITMAPINFOHEADER *)1)
        wanted = nullptr;
    g->stream->lpVtbl->Info(g->stream, &info, AVISTREAMINFOW_SIZE);
    if (info.fccType != FCC_vids)
        return E_FAIL;
    g->streaming = FALSE;
    g->changes = (info.dwFlags & AVISTREAMINFO_FORMATCHANGES) != 0;
    frame_release_buffers(g);
    if (FAILED(g->stream->lpVtbl->ReadFormat(g->stream, (LONG)info.dwStart, nullptr, &size)) ||
        size < (LONG)sizeof(BITMAPINFOHEADER))
        return E_FAIL;
    BYTE *format = AviAlloc((SIZE_T)size);
    if (!format)
        return E_FAIL;
    if (FAILED(g->stream->lpVtbl->ReadFormat(g->stream, (LONG)info.dwStart, format, &size)) ||
        !AviBitmapHeaderValid((const BITMAPINFOHEADER *)format, (DWORD)size)) {
        AviFree(format);
        return E_FAIL;
    }
    BITMAPINFOHEADER *native = (BITMAPINFOHEADER *)format;
    DWORD compression = native->biCompression;
    LONG height = native->biHeight < 0 ? -native->biHeight : native->biHeight;
    BOOL ok = compression == BI_RGB || compression == 3 || compression == BI_RLE8;
    /* only the native picture is available, without another format, a stretch or a place of its own. */
    if (wanted) {
        if ((wanted->biWidth && wanted->biWidth != native->biWidth) ||
            (wanted->biHeight && wanted->biHeight != native->biHeight) ||
            (wanted->biBitCount && wanted->biBitCount != native->biBitCount) || wanted->biCompression != BI_RGB)
            ok = FALSE;
    }
    if (bits || x || y || (dx != -1 && dx != native->biWidth) || (dy != -1 && dy != height))
        ok = FALSE;
    if (!ok) {
        AviFree(format);
        return E_FAIL;
    }
    DWORD stride = AviDibStride((DWORD)native->biWidth, native->biBitCount);
    g->header_size = (DWORD)size;
    g->native_compression = compression;
    if (compression == BI_RLE8) {
        native->biCompression = BI_RGB;
        native->biSizeImage = stride * (DWORD)height;
        g->image_size = native->biSizeImage;
        g->raw_capacity = info.dwSuggestedBufferSize ? info.dwSuggestedBufferSize : 1024;
        g->raw = AviAlloc(g->raw_capacity);
        if (!g->raw) {
            AviFree(format);
            return E_FAIL;
        }
    } else {
        g->image_size = info.dwSuggestedBufferSize ? info.dwSuggestedBufferSize : 1024;
        if (native->biSizeImage > g->image_size)
            g->image_size = native->biSizeImage;
        if (!native->biSizeImage)
            native->biSizeImage = stride * (DWORD)height;
    }
    g->nominal_size = compression == BI_RLE8 ? 0 : native->biSizeImage;
    if (compression == 3 && stride * (DWORD)height > g->image_size)
        g->image_size = stride * (DWORD)height;
    g->dib = AviAlloc((SIZE_T)g->header_size + g->image_size);
    if (!g->dib) {
        AviFree(format);
        frame_release_buffers(g);
        return E_FAIL;
    }
    memcpy(g->dib, format, (SIZE_T)size);
    AviFree(format);
    g->current = NO_FRAME;
    return S_OK;
}

static const IGetFrameVtbl g_frame_vtbl = {frame_qi,    frame_addref, frame_release,
                                           frame_get,   frame_begin,  frame_end,
                                           frame_setformat};

HRESULT AviDefaultFrameCreate(PAVISTREAM stream, IGetFrame **frame)
{
    FrameDef *g = AviAlloc(sizeof(*g));
    if (!g)
        return E_OUTOFMEMORY;
    g->lpVtbl = &g_frame_vtbl;
    g->refs = 1;
    g->current = NO_FRAME;
    g->stream = stream;
    if (stream)
        stream->lpVtbl->AddRef(stream);
    AviUseAdd(1);
    *frame = (IGetFrame *)g;
    return S_OK;
}
