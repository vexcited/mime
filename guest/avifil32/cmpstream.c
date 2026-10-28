/*
 * the compressed video stream, AVIFile's class 00020001. The compressors of video are the installable codecs of the
 * video compression manager, which mime has none of, so a stream is only made for the "DIB " handler, the frames as
 * device independent bitmaps. It reads the frames of a source stream through IGetFrame, which expands what the
 * source holds, and it writes bitmaps to a destination stream. Any other handler is a compressor that is not there.
 */
#include "avifil32_private.h"

typedef struct CmpStream {
    const IUnknownVtbl *lpUnknown;
    const IAVIStreamVtbl *lpStream;
    IUnknown *outer;
    LONG refs;
    PAVISTREAM source;
    PGETFRAME frame;
    AVISTREAMINFOW info;
    BYTE *format;
    DWORD format_size;
} CmpStream;

#define CMP_FROM_STREAM(p) ((CmpStream *)((BYTE *)(p) - offsetof(CmpStream, lpStream)))
#define CMP_FROM_UNKNOWN(p) ((CmpStream *)((BYTE *)(p) - offsetof(CmpStream, lpUnknown)))

static HRESULT STDMETHODCALLTYPE cmp_unknown_qi(IUnknown *This, REFIID riid, void **object)
{
    CmpStream *s = CMP_FROM_UNKNOWN(This);
    if (IsEqualGUID(riid, &IID_IUnknown))
        *object = &s->lpUnknown;
    else if (IsEqualGUID(riid, &IID_IAVIStream))
        *object = &s->lpStream;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cmp_unknown_addref(IUnknown *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&CMP_FROM_UNKNOWN(This)->refs);
}

static ULONG STDMETHODCALLTYPE cmp_unknown_release(IUnknown *This)
{
    CmpStream *s = CMP_FROM_UNKNOWN(This);
    LONG refs = InterlockedDecrement(&s->refs);
    AviUseAdd(-1);
    if (!refs) {
        if (s->frame)
            s->frame->lpVtbl->Release(s->frame);
        if (s->source)
            s->source->lpVtbl->Release(s->source);
        AviFree(s->format);
        AviFree(s);
    }
    return (ULONG)refs;
}

static const IUnknownVtbl g_cmp_unknown_vtbl = {cmp_unknown_qi, cmp_unknown_addref, cmp_unknown_release};

static HRESULT STDMETHODCALLTYPE cmp_qi(IAVIStream *This, REFIID riid, void **object)
{
    IUnknown *outer = CMP_FROM_STREAM(This)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE cmp_addref(IAVIStream *This)
{
    IUnknown *outer = CMP_FROM_STREAM(This)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE cmp_release(IAVIStream *This)
{
    IUnknown *outer = CMP_FROM_STREAM(This)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE cmp_create(IAVIStream *This, LPARAM source, LPARAM options_param)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    const AVICOMPRESSOPTIONS *options = (const AVICOMPRESSOPTIONS *)options_param;
    s->source = (PAVISTREAM)source;
    s->source->lpVtbl->AddRef(s->source);
    AVIStreamInfoW(s->source, &s->info, AVISTREAMINFOW_SIZE);
    if (options && options->fccHandler != FCC_DIB)
        return AVIERR_NOCOMPRESSOR;
    s->info.fccHandler = FCC_DIB;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmp_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    memcpy(info, &s->info, (ULONG)size < AVISTREAMINFOW_SIZE ? (SIZE_T)size : AVISTREAMINFOW_SIZE);
    return S_OK;
}

static LONG STDMETHODCALLTYPE cmp_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    if (flags & (FIND_KEY | FIND_ANY))
        return pos;
    if ((flags & FIND_FORMAT) && (flags & FIND_PREV))
        return 0;
    return -1;
}

/* the frames are available once the source has been asked for them. */
static HRESULT frames_ready(CmpStream *s)
{
    if (s->frame)
        return S_OK;
    s->frame = AVIStreamGetFrameOpen(s->source, nullptr);
    return s->frame ? S_OK : AVIERR_INTERNAL;
}

static HRESULT STDMETHODCALLTYPE cmp_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    HRESULT hr = frames_ready(s);
    if (FAILED(hr))
        return hr;
    BITMAPINFOHEADER *bih = s->frame->lpVtbl->GetFrame(s->frame, pos);
    if (!bih)
        return AVIERR_MEMORY;
    LONG needed = (LONG)(bih->biSize + 4 * bih->biClrUsed);
    if (format) {
        LONG count = *size < needed ? *size : needed;
        if (count > 0)
            memcpy(format, bih, (SIZE_T)count);
    }
    *size = needed;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmp_setformat(IAVIStream *This, LONG pos, void *format, LONG size)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    const BITMAPINFOHEADER *bih = format;
    if (s->frame || !bih || size < (LONG)sizeof(*bih) || bih->biCompression != BI_RGB)
        return AVIERR_UNSUPPORTED;
    if (s->format && s->format_size == (DWORD)size && !memcmp(s->format, format, (SIZE_T)size))
        return S_OK;
    if (pos < (LONG)(s->info.dwStart + s->info.dwLength))
        return AVIERR_UNSUPPORTED;
    if (!s->format) {
        s->format = AviAlloc((SIZE_T)size);
        if (!s->format)
            return AVIERR_MEMORY;
        memcpy(s->format, format, (SIZE_T)size);
        s->format_size = (DWORD)size;
    }
    return s->source->lpVtbl->SetFormat(s->source, pos, format, size);
}

static HRESULT STDMETHODCALLTYPE cmp_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                          LONG *bytes, LONG *samples_read)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    HRESULT hr = frames_ready(s);
    if (FAILED(hr))
        return hr;
    BITMAPINFOHEADER *bih = s->frame->lpVtbl->GetFrame(s->frame, start);
    if (!bih)
        return AVIERR_MEMORY;
    if (bytes)
        *bytes = (LONG)bih->biSizeImage;
    if (buffer) {
        if ((LONG)bih->biSizeImage > size)
            return AVIERR_BUFFERTOOSMALL;
        memcpy(buffer, (BYTE *)bih + bih->biSize + 4 * bih->biClrUsed, bih->biSizeImage);
    }
    if (samples_read)
        *samples_read = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmp_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                           DWORD flags, LONG *samples_written, LONG *bytes_written)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    if (s->frame || start < (LONG)(s->info.dwStart + s->info.dwLength) || samples > 1)
        return AVIERR_UNSUPPORTED;
    return AVIStreamWrite(s->source, start, samples, buffer, size, flags | AVIIF_KEYFRAME, samples_written,
                          bytes_written);
}

static HRESULT STDMETHODCALLTYPE cmp_delete(IAVIStream *This, LONG start, LONG samples)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE cmp_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    CmpStream *s = CMP_FROM_STREAM(This);
    if (fcc != FCC_strd)
        return s->source->lpVtbl->ReadData(s->source, fcc, data, size);
    /* a compressor of mime's has no data of its own to keep in the file. */
    *size = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmp_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE cmp_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static const IAVIStreamVtbl g_cmp_stream_vtbl = {cmp_qi,         cmp_addref,   cmp_release,   cmp_create,
                                                 cmp_info,       cmp_findsample, cmp_readformat, cmp_setformat,
                                                 cmp_read,       cmp_write,    cmp_delete,    cmp_readdata,
                                                 cmp_writedata,  cmp_setinfo};

HRESULT AviCmpStreamCreate(IUnknown *outer, REFIID riid, void **object)
{
    CmpStream *s = AviAlloc(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpUnknown = &g_cmp_unknown_vtbl;
    s->lpStream = &g_cmp_stream_vtbl;
    s->outer = outer ? outer : (IUnknown *)&s->lpUnknown;
    HRESULT hr = cmp_unknown_qi((IUnknown *)&s->lpUnknown, riid, object);
    if (FAILED(hr))
        AviFree(s);
    return hr;
}
