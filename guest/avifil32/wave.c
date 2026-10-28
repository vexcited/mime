/*
 * the wave file handler, AVIFile's class 00020003: a file of audio, WAVE or Sun AU, that is one file and one stream
 * of audio in the same object. It reads and writes the samples as they are, and keeps the chunks of the file it does
 * not know to write them back.
 */
#include "avifil32_private.h"

typedef struct WaveFile {
    const IAVIStreamVtbl *lpStream;
    const IAVIFileVtbl *lpFile;
    const IUnknownVtbl *lpUnknown;
    const IAVIPersistFileVtbl *lpPersist;
    IUnknown *outer;
    LONG refs;
    CRITICAL_SECTION lock;
    AviIo *io;
    UINT mode;
    BOOL dirty;
    WCHAR path[MAX_PATH];
    BYTE *format;
    DWORD format_size;
    DWORD data_offset;
    DWORD data_size;
    AVISTREAMINFOW info;
    AVIFILEINFOW file_info;
    AviExtra extra;
} WaveFile;

#define WAVE_FROM(p, field) ((WaveFile *)((BYTE *)(p) - offsetof(WaveFile, field)))

static BOOL wave_writable(const WaveFile *w)
{
    return (w->mode & 3) != 0;
}

static DWORD swap32(DWORD v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

/* ------------------------------------------------------------------ reading the files */

static void wave_describe(WaveFile *w)
{
    const WAVEFORMATEX *wf = (const WAVEFORMATEX *)w->format;
    w->info.fccType = FCC_auds;
    w->info.fccHandler = 0;
    w->info.dwScale = wf->nBlockAlign;
    w->info.dwRate = wf->nAvgBytesPerSec;
    w->info.dwStart = 0;
    w->info.dwLength = w->data_size / wf->nBlockAlign;
    w->info.dwSampleSize = wf->nBlockAlign;
    w->file_info.dwScale = 1;
    w->file_info.dwRate = wf->nSamplesPerSec;
    w->file_info.dwLength = (DWORD)MulDiv((int)w->data_size, (int)wf->nSamplesPerSec, (int)wf->nAvgBytesPerSec);
}

static HRESULT parse_au(WaveFile *w)
{
    DWORD head[7];
    if (!AviIoRead(w->io, 0, head, sizeof(head)) || head[0] != FOURCC_('.', 's', 'n', 'd'))
        return AVIERR_FILEREAD;
    DWORD offset = swap32(head[1]), size = swap32(head[2]), encoding = swap32(head[3]), rate = swap32(head[4]);
    DWORD channels = swap32(head[5]);
    WAVEFORMATEX *wf = AviAlloc(sizeof(*wf));
    if (!wf)
        return AVIERR_MEMORY;
    /* the encodings it plays are mu-law and 8 bit linear, as 8 bit samples. */
    if (encoding == 1) {
        wf->wFormatTag = WAVE_FORMAT_MULAW;
        if (rate >= 7981 && rate <= 8019)
            rate = 8000;
    } else if (encoding == 2) {
        wf->wFormatTag = WAVE_FORMAT_PCM;
    } else {
        AviFree(wf);
        return AVIERR_FILEREAD;
    }
    if (!channels || channels > 0xFFFF || !rate) {
        AviFree(wf);
        return AVIERR_FILEREAD;
    }
    wf->nChannels = (WORD)channels;
    wf->nSamplesPerSec = rate;
    wf->nBlockAlign = (WORD)channels;
    wf->nAvgBytesPerSec = rate * channels;
    wf->wBitsPerSample = 8;
    wf->cbSize = 0;
    w->format = (BYTE *)wf;
    w->format_size = sizeof(*wf);
    w->data_offset = offset;
    w->data_size = size;
    wave_describe(w);
    return S_OK;
}

static HRESULT parse_wave(WaveFile *w)
{
    DWORD riff[3];
    if (!AviIoRead(w->io, 0, riff, 12))
        return AVIERR_FILEREAD;
    if (riff[0] != FCC_RIFF || riff[2] != FCC_WAVE)
        return parse_au(w);
    UINT64 file_size = AviIoSize(w->io);
    UINT64 end = 8ull + riff[1];
    if (end > file_size || end < 12)
        end = file_size;
    UINT64 pos = 12;
    BOOL have_data = FALSE;
    while (pos + 8 <= end) {
        DWORD head[2];
        if (!AviIoRead(w->io, pos, head, 8))
            return AVIERR_FILEREAD;
        UINT64 data = pos + 8;
        UINT64 size = head[1];
        if (data + size > end)
            size = end - data;
        pos = data + size + (size & 1);
        if (head[0] == FCC_fmt && !w->format) {
            const WAVEFORMATEX *wf;
            if (size < 16)
                return AVIERR_FILEREAD;
            w->format = AviAlloc((SIZE_T)size);
            if (!w->format)
                return AVIERR_MEMORY;
            if (!AviIoRead(w->io, data, w->format, (DWORD)size))
                return AVIERR_FILEREAD;
            w->format_size = (DWORD)size;
            wf = (const WAVEFORMATEX *)w->format;
            if ((wf->wFormatTag != WAVE_FORMAT_PCM && (size < 18 || size < 18u + wf->cbSize)) || !wf->nBlockAlign)
                return AVIERR_FILEREAD;
        } else if (head[0] == FCC_data && w->format && !have_data) {
            w->data_offset = (DWORD)data;
            w->data_size = (DWORD)size;
            have_data = TRUE;
        } else if (head[0] != FCC_JUNK && head[0] != FCC_pad && head[0] != FCC_fmt && head[0] != FCC_data) {
            BYTE *copy = AviAlloc((SIZE_T)size);
            if (!copy)
                return AVIERR_MEMORY;
            HRESULT hr = AviIoRead(w->io, data, copy, (DWORD)size) ? AviExtraAdd(&w->extra, head[0], copy, (DWORD)size)
                                                                    : AVIERR_FILEREAD;
            AviFree(copy);
            if (FAILED(hr))
                return hr;
        }
    }
    if (!w->format || !have_data)
        return AVIERR_FILEREAD;
    wave_describe(w);
    return S_OK;
}

static HRESULT wave_open(WaveFile *w, LPCWSTR name, DWORD mode)
{
    if (lstrlenW(name) >= MAX_PATH)
        return E_INVALIDARG;
    AviCopyName(w->path, name, MAX_PATH);
    UINT old = SetErrorMode(0x8000);
    w->io = AviIoOpen(w->path, mode);
    if (!w->io && !(mode & 3)) {
        mode &= ~0x70u;
        w->io = AviIoOpen(w->path, mode);
    }
    SetErrorMode(old);
    w->mode = mode;
    if (!w->io)
        return AVIERR_FILEREAD;
    memset(&w->info, 0, sizeof(w->info));
    memset(&w->file_info, 0, sizeof(w->file_info));
    w->file_info.dwRate = 1000;
    w->file_info.dwScale = 1;
    w->file_info.dwStreams = 1;
    AviCopyName(w->file_info.szFileType, AVI_STRING_WAVE_FILE, 64);
    {
        WCHAR text[96];
        SIZE_T at;
        AviCopyName(text, L"Microsoft Waveform: ", 40);
        at = lstrlenW(text);
        AviCopyName(text + at, AviBaseName(w->path), 96 - at);
        AviCopyName(w->info.szName, text, 64);
    }
    if (mode & OF_CREATE) {
        w->info.fccType = FCC_auds;
        w->dirty = TRUE;
        return S_OK;
    }
    return parse_wave(w);
}

/* ------------------------------------------------------------------ the stream */

static HRESULT STDMETHODCALLTYPE wstream_qi(IAVIStream *This, REFIID riid, void **object)
{
    IUnknown *outer = WAVE_FROM(This, lpStream)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE wstream_addref(IAVIStream *This)
{
    IUnknown *outer = WAVE_FROM(This, lpStream)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE wstream_release(IAVIStream *This)
{
    IUnknown *outer = WAVE_FROM(This, lpStream)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE wstream_unsupported(IAVIStream *This, LPARAM a, LPARAM b)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE wstream_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    WaveFile *w = WAVE_FROM(This, lpStream);
    EnterCriticalSection(&w->lock);
    memcpy(info, &w->info, (ULONG)size < AVISTREAMINFOW_SIZE ? (SIZE_T)size : AVISTREAMINFOW_SIZE);
    LeaveCriticalSection(&w->lock);
    return S_OK;
}

static LONG STDMETHODCALLTYPE wstream_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    if (!(flags & FIND_FORMAT))
        return pos;
    if (flags & FIND_NEXT)
        return pos <= 0 ? 0 : -1;
    return 0;
}

static HRESULT STDMETHODCALLTYPE wstream_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    WaveFile *w = WAVE_FROM(This, lpStream);
    HRESULT hr = S_OK;
    if (!size)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&w->lock);
    if (format && *size) {
        LONG count = *size < (LONG)w->format_size ? *size : (LONG)w->format_size;
        if (count > 0)
            memcpy(format, w->format, (SIZE_T)count);
        if (*size < (LONG)w->format_size)
            hr = AVIERR_BUFFERTOOSMALL;
    }
    *size = (LONG)w->format_size;
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wstream_setformat(IAVIStream *This, LONG pos, void *format, LONG size)
{
    WaveFile *w = WAVE_FROM(This, lpStream);
    HRESULT hr = S_OK;
    if (!format || size < (LONG)sizeof(WAVEFORMATEX) - 2)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&w->lock);
    if (w->format) {
        if ((DWORD)size != w->format_size || memcmp(format, w->format, (SIZE_T)size))
            hr = AVIERR_UNSUPPORTED;
    } else if (!wave_writable(w)) {
        hr = AVIERR_READONLY;
    } else {
        const WAVEFORMATEX *wf = format;
        w->format = AviAlloc((SIZE_T)size);
        if (!w->format) {
            hr = AVIERR_MEMORY;
        } else if (!wf->nBlockAlign) {
            AviFree(w->format);
            w->format = nullptr;
            hr = AVIERR_BADFORMAT;
        } else {
            memcpy(w->format, format, (SIZE_T)size);
            w->format_size = (DWORD)size;
            w->data_size = 0;
            w->data_offset = 28 + (((DWORD)size + 1) & ~1u);
            w->info.dwScale = wf->nBlockAlign;
            w->info.dwRate = wf->nAvgBytesPerSec;
            w->info.dwLength = 0;
            w->info.dwSampleSize = wf->nBlockAlign;
            w->dirty = TRUE;
        }
    }
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wstream_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                              LONG *bytes, LONG *samples_read)
{
    WaveFile *w = WAVE_FROM(This, lpStream);
    EnterCriticalSection(&w->lock);
    LONG length = (LONG)w->info.dwLength, block = (LONG)w->info.dwSampleSize;
    if (start >= 0 && start <= length && block > 0) {
        LONG count = samples;
        if (start + count > length)
            count = length - start;
        if (!buffer) {
            if (size <= 0)
                count = count > 0 ? count : size / block;
            else if (count > 0)
                count = count < size / block ? count : size / block;
            else
                count = size / block;
        } else {
            count = count <= 0 ? size / block : (count < size / block ? count : size / block);
            if (size && size >= block) {
                BOOL ok = TRUE;
                if (count > 0)
                    ok = AviIoRead(w->io, (UINT64)w->data_offset + (UINT64)start * (DWORD)block, buffer,
                                   (DWORD)(count * block));
                if (!ok) {
                    LeaveCriticalSection(&w->lock);
                    goto fail;
                }
            } else if (size) {
                LeaveCriticalSection(&w->lock);
                goto fail;
            }
        }
        if (bytes)
            *bytes = count * block;
        if (samples_read)
            *samples_read = count;
        LeaveCriticalSection(&w->lock);
        return S_OK;
    }
    LeaveCriticalSection(&w->lock);
fail:
    if (bytes)
        *bytes = 0;
    if (samples_read)
        *samples_read = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wstream_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                               DWORD flags, LONG *samples_written, LONG *bytes_written)
{
    WaveFile *w = WAVE_FROM(This, lpStream);
    HRESULT hr = S_OK;
    EnterCriticalSection(&w->lock);
    if (!wave_writable(w)) {
        hr = AVIERR_READONLY;
    } else if (!w->format) {
        hr = E_UNEXPECTED;
    } else if (size < 0 || (size && !buffer)) {
        hr = AVIERR_BADPARAM;
    } else {
        if (start < 0)
            start = (LONG)w->info.dwLength;
        UINT64 offset = (UINT64)w->data_offset + (UINT64)(DWORD)start * w->info.dwSampleSize;
        w->dirty = TRUE;
        if (offset + (DWORD)size > 0xFFFFFFF0ull || !AviIoWrite(w->io, offset, buffer, (DWORD)size)) {
            hr = AVIERR_FILEWRITE;
        } else {
            if ((DWORD)(start + samples) > w->info.dwLength)
                w->info.dwLength = (DWORD)(start + samples);
            DWORD end = (DWORD)(offset - w->data_offset) + (DWORD)size;
            if (end > w->data_size)
                w->data_size = end;
            w->file_info.dwLength = (DWORD)MulDiv((int)(1000 * w->info.dwLength), (int)w->info.dwScale, (int)w->info.dwRate);
            if (samples_written)
                *samples_written = samples;
            if (bytes_written)
                *bytes_written = size;
        }
    }
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wstream_delete(IAVIStream *This, LONG start, LONG samples)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE wstream_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE wstream_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE wstream_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static const IAVIStreamVtbl g_wstream_vtbl = {
    wstream_qi,         wstream_addref,   wstream_release,  wstream_unsupported, wstream_info,
    wstream_findsample, wstream_readformat, wstream_setformat, wstream_read,       wstream_write,
    wstream_delete,     wstream_readdata, wstream_writedata, wstream_setinfo};

/* ------------------------------------------------------------------ the file */

static HRESULT STDMETHODCALLTYPE wfile_qi(IAVIFile *This, REFIID riid, void **object)
{
    IUnknown *outer = WAVE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE wfile_addref(IAVIFile *This)
{
    IUnknown *outer = WAVE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE wfile_release(IAVIFile *This)
{
    IUnknown *outer = WAVE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE wfile_info(IAVIFile *This, AVIFILEINFOW *info, LONG size)
{
    WaveFile *w = WAVE_FROM(This, lpFile);
    EnterCriticalSection(&w->lock);
    memcpy(info, &w->file_info, (ULONG)size < AVIFILEINFOW_SIZE ? (SIZE_T)size : AVIFILEINFOW_SIZE);
    LeaveCriticalSection(&w->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wfile_getstream(IAVIFile *This, PAVISTREAM *stream, DWORD type, LONG index)
{
    WaveFile *w = WAVE_FROM(This, lpFile);
    if (!w->format || index != 0 || (type && type != FCC_auds))
        return AVIERR_BADPARAM;
    *stream = (PAVISTREAM)&w->lpStream;
    wstream_addref(*stream);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wfile_createstream(IAVIFile *This, PAVISTREAM *stream, AVISTREAMINFOW *info)
{
    WaveFile *w = WAVE_FROM(This, lpFile);
    if (w->format || info->fccType != FCC_auds)
        return AVIERR_UNSUPPORTED;
    *stream = (PAVISTREAM)&w->lpStream;
    wstream_addref(*stream);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wfile_writedata(IAVIFile *This, DWORD ckid, void *data, LONG size)
{
    WaveFile *w = WAVE_FROM(This, lpFile);
    if (!data || size <= 0)
        return AVIERR_BADPARAM;
    if (!wave_writable(w))
        return AVIERR_READONLY;
    EnterCriticalSection(&w->lock);
    w->dirty = TRUE;
    HRESULT hr = AviExtraAdd(&w->extra, ckid, data, (DWORD)size);
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wfile_readdata(IAVIFile *This, DWORD ckid, void *data, LONG *size)
{
    WaveFile *w = WAVE_FROM(This, lpFile);
    if (!size)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&w->lock);
    HRESULT hr = AviExtraRead(&w->extra, ckid, data, size);
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wfile_endrecord(IAVIFile *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wfile_deletestream(IAVIFile *This, DWORD type, LONG index)
{
    return AVIERR_BADPARAM;
}

static const IAVIFileVtbl g_wfile_vtbl = {wfile_qi,        wfile_addref,      wfile_release,   wfile_info,
                                          wfile_getstream, wfile_createstream, wfile_writedata, wfile_readdata,
                                          wfile_endrecord, wfile_deletestream};

/* ------------------------------------------------------------------ IPersistFile */

static HRESULT STDMETHODCALLTYPE wpersist_qi(IAVIPersistFile *This, REFIID riid, void **object)
{
    IUnknown *outer = WAVE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE wpersist_addref(IAVIPersistFile *This)
{
    IUnknown *outer = WAVE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE wpersist_release(IAVIPersistFile *This)
{
    IUnknown *outer = WAVE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE wpersist_classid(IAVIPersistFile *This, CLSID *clsid)
{
    if (clsid)
        *clsid = CLSID_WAVFile_;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wpersist_isdirty(IAVIPersistFile *This)
{
    return WAVE_FROM(This, lpPersist)->dirty ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE wpersist_load(IAVIPersistFile *This, LPCOLESTR name, DWORD mode)
{
    WaveFile *w = WAVE_FROM(This, lpPersist);
    EnterCriticalSection(&w->lock);
    HRESULT hr = wave_open(w, name, mode & ~(DWORD)OF_PARSE);
    LeaveCriticalSection(&w->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE wpersist_save(IAVIPersistFile *This, LPCOLESTR name, BOOL remember)
{
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE wpersist_savecompleted(IAVIPersistFile *This, LPCOLESTR name)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wpersist_getcurfile(IAVIPersistFile *This, LPOLESTR *name)
{
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE wpersist_reserved(IAVIPersistFile *This)
{
    return S_OK;
}

static const IAVIPersistFileVtbl g_wpersist_vtbl = {
    wpersist_qi,       wpersist_addref,       wpersist_release,       wpersist_classid,    wpersist_isdirty,
    wpersist_load,     wpersist_save,         wpersist_savecompleted, wpersist_getcurfile, wpersist_reserved};

/* ------------------------------------------------------------------ the object */

static HRESULT STDMETHODCALLTYPE wunknown_qi(IUnknown *This, REFIID riid, void **object)
{
    WaveFile *w = WAVE_FROM(This, lpUnknown);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *object = &w->lpUnknown;
    else if (IsEqualGUID(riid, &IID_IAVIFile))
        *object = &w->lpFile;
    else if (IsEqualGUID(riid, &IID_IAVIStream))
        *object = &w->lpStream;
    else if (IsEqualGUID(riid, &IID_IPersistFile_))
        *object = &w->lpPersist;
    else
        return E_NOINTERFACE;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE wunknown_addref(IUnknown *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&WAVE_FROM(This, lpUnknown)->refs);
}

/* writes the header in front of the samples and the chunks that were kept behind them. */
static void wave_save(WaveFile *w)
{
    DWORD data_chunk = w->data_offset - 8;
    DWORD used = 12 + 8 + w->format_size + (w->format_size & 1);
    UINT64 end = (UINT64)w->data_offset + w->data_size + (w->data_size & 1);
    DWORD riff[3] = {FCC_RIFF, 0, FCC_WAVE};
    DWORD fmt_head[2] = {FCC_fmt, w->format_size};
    DWORD data_head[2] = {FCC_data, w->data_size};
    /* the header has to end where the samples start, with a JUNK chunk between when there is room. */
    if (data_chunk < used || (data_chunk > used && data_chunk - used < 8))
        return;
    if (w->data_size & 1)
        AviIoZero(w->io, (UINT64)w->data_offset + w->data_size, 1);
    AviIoWrite(w->io, 12, fmt_head, 8);
    AviIoWrite(w->io, 20, w->format, w->format_size);
    if (w->format_size & 1)
        AviIoZero(w->io, 20 + (UINT64)w->format_size, 1);
    if (data_chunk > used) {
        DWORD junk[2] = {FCC_JUNK, data_chunk - used - 8};
        AviIoWrite(w->io, used, junk, 8);
        AviIoZero(w->io, used + 8ull, junk[1]);
    }
    AviIoWrite(w->io, data_chunk, data_head, 8);
    if (w->extra.size)
        AviIoWrite(w->io, end, w->extra.data, w->extra.size);
    riff[1] = (DWORD)(end + w->extra.size - 8);
    AviIoWrite(w->io, 0, riff, 12);
    AviIoTruncate(w->io, end + w->extra.size);
}

static ULONG STDMETHODCALLTYPE wunknown_release(IUnknown *This)
{
    WaveFile *w = WAVE_FROM(This, lpUnknown);
    EnterCriticalSection(&w->lock);
    LONG refs = InterlockedDecrement(&w->refs);
    AviUseAdd(-1);
    if (!refs) {
        if (w->dirty && wave_writable(w) && w->io && w->format)
            wave_save(w);
        LeaveCriticalSection(&w->lock);
        AviIoClose(w->io);
        AviFree(w->format);
        AviExtraFree(&w->extra);
        DeleteCriticalSection(&w->lock);
        AviFree(w);
        return 0;
    }
    LeaveCriticalSection(&w->lock);
    return (ULONG)refs;
}

static const IUnknownVtbl g_wunknown_vtbl = {wunknown_qi, wunknown_addref, wunknown_release};

HRESULT AviWaveCreate(IUnknown *outer, REFIID riid, void **object)
{
    WaveFile *w = AviAlloc(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->lpStream = &g_wstream_vtbl;
    w->lpFile = &g_wfile_vtbl;
    w->lpUnknown = &g_wunknown_vtbl;
    w->lpPersist = &g_wpersist_vtbl;
    w->outer = outer ? outer : (IUnknown *)&w->lpUnknown;
    InitializeCriticalSection(&w->lock);
    HRESULT hr = wunknown_qi((IUnknown *)&w->lpUnknown, riid, object);
    if (FAILED(hr)) {
        DeleteCriticalSection(&w->lock);
        AviFree(w);
    }
    return hr;
}
