/*
 * the compressed audio stream, AVIFile's class 0002000F: the samples of an audio stream converted to another wave
 * format by the audio compression manager. Without a format to convert to, or when the manager has no converter
 * that is asked for by a format of PCM, the samples are handed on as they are.
 */
#include "avifil32_private.h"
#include <mime/win32/msacm.h>

typedef struct AcmStream {
    const IUnknownVtbl *lpUnknown;
    const IAVIStreamVtbl *lpStream;
    IUnknown *outer;
    LONG refs;
    PAVISTREAM source;
    AVISTREAMINFOW info;
    BOOL ready;
    BYTE *source_format;
    DWORD source_format_size;
    BYTE *target_format;
    DWORD target_format_size;
    HACMSTREAM acm;
    BYTE *source_buffer;
    DWORD source_buffer_size;
    BYTE *target_buffer;
    DWORD target_buffer_size;
    ACMSTREAMHEADER header;
    LONG source_pos;
    LONG target_pos;
    LONG target_count;
    LONG trailing;
} AcmStream;

#define ACM_FROM_STREAM(p) ((AcmStream *)((BYTE *)(p) - offsetof(AcmStream, lpStream)))
#define ACM_FROM_UNKNOWN(p) ((AcmStream *)((BYTE *)(p) - offsetof(AcmStream, lpUnknown)))

static HRESULT STDMETHODCALLTYPE acm_unknown_qi(IUnknown *This, REFIID riid, void **object)
{
    AcmStream *s = ACM_FROM_UNKNOWN(This);
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

static ULONG STDMETHODCALLTYPE acm_unknown_addref(IUnknown *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&ACM_FROM_UNKNOWN(This)->refs);
}

static ULONG STDMETHODCALLTYPE acm_unknown_release(IUnknown *This)
{
    AcmStream *s = ACM_FROM_UNKNOWN(This);
    LONG refs = InterlockedDecrement(&s->refs);
    AviUseAdd(-1);
    if (!refs) {
        if (s->acm) {
            s->header.cbSrcLength = s->source_buffer_size;
            acmStreamUnprepareHeader(s->acm, &s->header, 0);
            acmStreamClose(s->acm, 0);
        }
        AviFree(s->source_buffer);
        AviFree(s->target_buffer);
        if (s->source)
            s->source->lpVtbl->Release(s->source);
        if (s->target_format != s->source_format)
            AviFree(s->target_format);
        AviFree(s->source_format);
        AviFree(s);
    }
    return (ULONG)refs;
}

static const IUnknownVtbl g_acm_unknown_vtbl = {acm_unknown_qi, acm_unknown_addref, acm_unknown_release};

static HRESULT STDMETHODCALLTYPE acm_qi(IAVIStream *This, REFIID riid, void **object)
{
    IUnknown *outer = ACM_FROM_STREAM(This)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE acm_addref(IAVIStream *This)
{
    IUnknown *outer = ACM_FROM_STREAM(This)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE acm_release(IAVIStream *This)
{
    IUnknown *outer = ACM_FROM_STREAM(This)->outer;
    return outer->lpVtbl->Release(outer);
}

static DWORD wave_format_size(const WAVEFORMATEX *wf)
{
    return wf->wFormatTag == WAVE_FORMAT_PCM ? 16 : 18u + wf->cbSize;
}

static HRESULT STDMETHODCALLTYPE acm_create(IAVIStream *This, LPARAM source, LPARAM options_param)
{
    AcmStream *s = ACM_FROM_STREAM(This);
    const AVICOMPRESSOPTIONS *options = (const AVICOMPRESSOPTIONS *)options_param;
    PAVISTREAM stream = (PAVISTREAM)source;
    stream->lpVtbl->Info(stream, &s->info, AVISTREAMINFOW_SIZE);
    s->info.fccHandler = 0;
    if (s->info.fccType != FCC_auds || acmGetVersion() < 0x02000000)
        return AVIERR_INTERNAL;
    if (options && options->lpFormat) {
        const WAVEFORMATEX *wf = options->lpFormat;
        DWORD size = wave_format_size(wf);
        if (options->cbFormat < size)
            return AVIERR_INTERNAL;
        s->target_format = AviAlloc(size);
        if (!s->target_format)
            return AVIERR_MEMORY;
        memcpy(s->target_format, wf, size);
        s->target_format_size = size;
    }
    stream->lpVtbl->AddRef(stream);
    s->source = stream;
    return S_OK;
}

/* finds the format to convert to and opens the converter, once. */
static HRESULT acm_setup(AcmStream *s)
{
    PAVISTREAM source = s->source;
    LONG size = 0;
    HRESULT hr = source->lpVtbl->ReadFormat(source, AVIStreamStart(source), nullptr, &size);
    if (FAILED(hr))
        return hr;
    s->source_format = AviAlloc((SIZE_T)size);
    if (!s->source_format)
        return AVIERR_MEMORY;
    hr = source->lpVtbl->ReadFormat(source, AVIStreamStart(source), s->source_format, &size);
    if (FAILED(hr))
        return hr;
    s->source_format_size = (DWORD)size;
    const WAVEFORMATEX *src = (const WAVEFORMATEX *)s->source_format;
    if (size < 14 || (src->wFormatTag != WAVE_FORMAT_PCM && (size < 18 || (DWORD)size < 18u + src->cbSize)))
        return AVIERR_BADFORMAT;
    BOOL passthrough = FALSE;
    if (s->target_format) {
        passthrough = (DWORD)size == s->target_format_size && !memcmp(s->source_format, s->target_format, (SIZE_T)size);
    } else if (src->wFormatTag == WAVE_FORMAT_PCM) {
        passthrough = TRUE;
    } else {
        DWORD largest = 0;
        acmMetrics(nullptr, ACM_METRIC_MAX_SIZE_FORMAT, &largest);
        s->target_format = AviAlloc(largest > 16 ? largest : 16);
        if (!s->target_format)
            return AVIERR_MEMORY;
        s->target_format_size = largest;
        ((WAVEFORMATEX *)s->target_format)->wFormatTag = WAVE_FORMAT_PCM;
        passthrough = acmFormatSuggest(nullptr, (WAVEFORMATEX *)s->source_format, (WAVEFORMATEX *)s->target_format,
                                       largest, 0) != 0;
    }
    if (passthrough) {
        if (s->target_format != s->source_format)
            AviFree(s->target_format);
        s->target_format = s->source_format;
        s->target_format_size = s->source_format_size;
        s->ready = TRUE;
        return S_OK;
    }
    const WAVEFORMATEX *dst = (const WAVEFORMATEX *)s->target_format;
    MMRESULT mm = acmStreamOpen(&s->acm, nullptr, (WAVEFORMATEX *)s->source_format, (WAVEFORMATEX *)s->target_format,
                                nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME);
    if (mm || !s->acm)
        return AVIERR_ERROR;
    s->info.dwSampleSize = dst->nBlockAlign;
    s->info.dwScale = dst->nBlockAlign;
    s->info.dwRate = dst->nAvgBytesPerSec;
    DWORD bytes = 0;
    acmStreamSize(s->acm, src->nBlockAlign * (DWORD)AVIStreamLength(source), &bytes, 0);
    s->info.dwLength = bytes / dst->nBlockAlign;
    s->info.dwQuality = 0;
    s->source_buffer_size = 4096 - 4096 % src->nBlockAlign;
    acmStreamSize(s->acm, s->source_buffer_size, &s->target_buffer_size, 0);
    s->source_buffer = AviAlloc(s->source_buffer_size);
    s->target_buffer = AviAlloc(s->target_buffer_size);
    if (!s->source_buffer || !s->target_buffer)
        return AVIERR_MEMORY;
    s->header.cbStruct = sizeof(s->header);
    s->header.pbSrc = s->source_buffer;
    s->header.cbSrcLength = s->source_buffer_size;
    s->header.pbDst = s->target_buffer;
    s->header.cbDstLength = s->target_buffer_size;
    if (acmStreamPrepareHeader(s->acm, &s->header, 0))
        return AVIERR_INTERNAL;
    s->source_pos = AVIStreamStart(source);
    s->target_pos = 0;
    s->target_count = 0;
    s->ready = TRUE;
    return S_OK;
}

static BOOL acm_passthrough(const AcmStream *s)
{
    return !s->acm;
}

static HRESULT STDMETHODCALLTYPE acm_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    AcmStream *s = ACM_FROM_STREAM(This);
    if (!s->ready) {
        HRESULT hr = acm_setup(s);
        if (FAILED(hr))
            return hr;
    }
    memcpy(info, &s->info, (ULONG)size < AVISTREAMINFOW_SIZE ? (SIZE_T)size : AVISTREAMINFOW_SIZE);
    return S_OK;
}

static LONG STDMETHODCALLTYPE acm_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    if (!(flags & FIND_FORMAT))
        return pos;
    if (!(flags & FIND_PREV))
        return pos <= 0 ? 0 : -1;
    return 0;
}

static HRESULT STDMETHODCALLTYPE acm_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    AcmStream *s = ACM_FROM_STREAM(This);
    if (!s->ready) {
        HRESULT hr = acm_setup(s);
        if (FAILED(hr))
            return hr;
    }
    if (format) {
        LONG count = *size < (LONG)s->target_format_size ? *size : (LONG)s->target_format_size;
        if (count > 0)
            memcpy(format, s->target_format, (SIZE_T)count);
    }
    *size = (LONG)s->target_format_size;
    return S_OK;
}

/* the converted samples, a block of the source at a time. */
static HRESULT STDMETHODCALLTYPE acm_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                          LONG *bytes, LONG *samples_read)
{
    AcmStream *s = ACM_FROM_STREAM(This);
    const WAVEFORMATEX *src = (const WAVEFORMATEX *)s->source_format;
    const WAVEFORMATEX *dst = (const WAVEFORMATEX *)s->target_format;
    if (bytes)
        *bytes = 0;
    if (samples_read)
        *samples_read = 0;
    if (!s->ready) {
        HRESULT hr = acm_setup(s);
        if (FAILED(hr))
            return hr;
    }
    if (acm_passthrough(s))
        return s->source->lpVtbl->Read(s->source, start, samples, buffer, size, bytes, samples_read);
    LONG end = (LONG)(s->info.dwStart + s->info.dwLength);
    if (start < 0 || start > end)
        return AVIERR_BADPARAM;
    if (samples == -1)
        samples = (size ? size : 0x8000) / dst->nBlockAlign;
    if (start + samples > end)
        samples = end - start;
    if (samples <= 0)
        return AVIERR_BADPARAM;
    if (!buffer) {
        if (bytes)
            *bytes = samples * dst->nBlockAlign;
        if (samples_read)
            *samples_read = samples;
        return S_OK;
    }
    if (size < 0)
        return AVIERR_BADPARAM;
    if (size < samples * dst->nBlockAlign) {
        if (bytes)
            *bytes = samples * dst->nBlockAlign;
        return AVIERR_BUFFERTOOSMALL;
    }
    if (start < s->target_pos) {
        s->target_pos = 0;
        s->source_pos = AVIStreamStart(s->source);
        s->target_count = 0;
    }
    BYTE *out = buffer;
    for (;;) {
        if (start >= s->target_pos + s->target_count) {
            LONG got_bytes = 0, got_samples = 0;
            s->target_pos += s->target_count;
            HRESULT hr = s->source->lpVtbl->Read(s->source, s->source_pos, (LONG)(s->source_buffer_size / src->nBlockAlign),
                                                 s->source_buffer, (LONG)s->source_buffer_size, &got_bytes, &got_samples);
            DWORD used = got_bytes > 0 ? (DWORD)got_bytes : 0;
            if (used < s->source_buffer_size) {
                /* silence follows the end of the source. */
                BYTE quiet = src->wFormatTag == WAVE_FORMAT_PCM && src->wBitsPerSample == 8 ? 0x80 : 0;
                memset(s->source_buffer + used, quiet, s->source_buffer_size - used);
                if (!used && s->source_pos >= AVIStreamLength(s->source))
                    used = s->source_buffer_size;
            }
            if (FAILED(hr))
                return hr;
            s->header.cbSrcLength = used;
            if (acmStreamConvert(s->acm, &s->header, ACM_STREAMCONVERTF_BLOCKALIGN) ||
                (!s->header.cbSrcLengthUsed && acmStreamConvert(s->acm, &s->header, 0)))
                return AVIERR_COMPRESSOR;
            s->source_pos += (LONG)(s->header.cbSrcLengthUsed / src->nBlockAlign);
            s->target_count = (LONG)((s->header.cbDstLengthUsed + dst->nBlockAlign - 1) / dst->nBlockAlign);
            if (!s->target_count)
                s->target_count = 1;
            s->trailing = s->target_count * dst->nBlockAlign - (LONG)s->header.cbDstLengthUsed;
        }
        if (start >= s->target_pos) {
            LONG take = s->target_pos + s->target_count - start;
            if (take > samples)
                take = samples;
            LONG count = take * dst->nBlockAlign;
            if (s->trailing && start - s->target_pos + take == s->target_count)
                count -= s->trailing;
            memcpy(out, s->target_buffer + (SIZE_T)dst->nBlockAlign * (DWORD)(start - s->target_pos), (SIZE_T)count);
            if (bytes)
                *bytes += count;
            if (samples_read)
                *samples_read += take;
            samples -= take;
            out += take * dst->nBlockAlign;
            start += take;
        }
        if (samples <= 0)
            return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE acm_unsupported(IAVIStream *This, LONG pos, void *format, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE acm_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                           DWORD flags, LONG *a, LONG *b)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE acm_delete(IAVIStream *This, LONG start, LONG samples)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE acm_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    AcmStream *s = ACM_FROM_STREAM(This);
    return s->source->lpVtbl->ReadData(s->source, fcc, data, size);
}

static HRESULT STDMETHODCALLTYPE acm_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE acm_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static const IAVIStreamVtbl g_acm_stream_vtbl = {
    acm_qi,         acm_addref,      acm_release,   acm_create,  acm_info,      acm_findsample,
    acm_readformat, acm_unsupported, acm_read,      acm_write,   acm_delete,    acm_readdata,
    acm_writedata,  acm_setinfo};

HRESULT AviAcmStreamCreate(IUnknown *outer, REFIID riid, void **object)
{
    AcmStream *s = AviAlloc(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpUnknown = &g_acm_unknown_vtbl;
    s->lpStream = &g_acm_stream_vtbl;
    s->outer = outer ? outer : (IUnknown *)&s->lpUnknown;
    HRESULT hr = acm_unknown_qi((IUnknown *)&s->lpUnknown, riid, object);
    if (FAILED(hr))
        AviFree(s);
    return hr;
}
