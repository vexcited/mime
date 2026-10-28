/*
 * streams over memory, the files made of streams and the clipboard: AVIMakeStreamFromClipboard, AVIMakeFileFromStreams,
 * AVIPutFileOnClipboard, AVIGetFromClipboard and AVIClearClipboard.
 */
#include "avifil32_private.h"

/* ------------------------------------------------------------------ a stream over a bitmap or a wave file in memory */

typedef struct MemStream {
    const IAVIStreamVtbl *lpVtbl;
    LONG refs;
    BYTE *block;
    const BYTE *format;
    DWORD format_size;
    const BYTE *data;
    DWORD data_size;
    AVISTREAMINFOW info;
} MemStream;

static HRESULT STDMETHODCALLTYPE mem_qi(IAVIStream *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAVIStream)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mem_addref(IAVIStream *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&((MemStream *)This)->refs);
}

static ULONG STDMETHODCALLTYPE mem_release(IAVIStream *This)
{
    MemStream *m = (MemStream *)This;
    LONG refs = InterlockedDecrement(&m->refs);
    AviUseAdd(-1);
    if (!refs) {
        AviFree(m->block);
        AviFree(m);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE mem_unsupported(IAVIStream *This, LPARAM a, LPARAM b)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    memcpy(info, &((MemStream *)This)->info, (ULONG)size < AVISTREAMINFOW_SIZE ? (SIZE_T)size : AVISTREAMINFOW_SIZE);
    return S_OK;
}

static LONG STDMETHODCALLTYPE mem_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    if (!(flags & FIND_FORMAT))
        return pos;
    if (!(flags & FIND_PREV))
        return pos <= 0 ? 0 : -1;
    return 0;
}

static HRESULT STDMETHODCALLTYPE mem_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    MemStream *m = (MemStream *)This;
    if (format) {
        LONG count = *size < (LONG)m->format_size ? *size : (LONG)m->format_size;
        if (count > 0)
            memcpy(format, m->format, (SIZE_T)count);
    }
    *size = (LONG)m->format_size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mem_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                          LONG *bytes, LONG *samples_read)
{
    MemStream *m = (MemStream *)This;
    LONG length = (LONG)m->info.dwLength, block = (LONG)m->info.dwSampleSize;
    if (start < 0 || start >= length || samples == 0) {
        if (!block || start < 0 || start >= length) {
            if (bytes)
                *bytes = 0;
            if (samples_read)
                *samples_read = 0;
            if (start < 0 || start >= length)
                return S_OK;
        }
    }
    if (block) {
        LONG count = samples;
        if (start + count > length)
            count = length - start;
        LONG fit = size / block;
        count = count <= 0 ? fit : (count < fit ? count : fit);
        if (!buffer && !size && samples > 0)
            count = samples < length - start ? samples : length - start;
        if (bytes)
            *bytes = count * block;
        if (samples_read)
            *samples_read = count;
        if (buffer) {
            if (size < block)
                return AVIERR_BUFFERTOOSMALL;
            memcpy(buffer, m->data + (SIZE_T)start * (DWORD)block, (SIZE_T)count * (DWORD)block);
        }
        return S_OK;
    }
    if (bytes)
        *bytes = (LONG)m->data_size;
    if (samples_read)
        *samples_read = 1;
    if (!buffer)
        return S_OK;
    memcpy(buffer, m->data, size < (LONG)m->data_size ? (SIZE_T)size : m->data_size);
    return size < (LONG)m->data_size ? AVIERR_BUFFERTOOSMALL : S_OK;
}

static HRESULT STDMETHODCALLTYPE mem_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                           DWORD flags, LONG *a, LONG *b)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_setformat(IAVIStream *This, LONG pos, void *format, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_delete(IAVIStream *This, LONG start, LONG samples)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE mem_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static const IAVIStreamVtbl g_mem_vtbl = {mem_qi,        mem_addref,    mem_release,  mem_unsupported, mem_info,
                                          mem_findsample, mem_readformat, mem_setformat, mem_read,        mem_write,
                                          mem_delete,    mem_readdata,  mem_writedata, mem_setinfo};

/* the data of a bitmap follows its header and its colors, those of a wave file are the chunks of a RIFF. */
static HRESULT mem_describe(MemStream *m, UINT format, SIZE_T size)
{
    if (format == CF_DIB) {
        BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)m->block;
        if (size < sizeof(*bih) || !AviBitmapHeaderValid(bih, (DWORD)size))
            return AVIERR_BADFORMAT;
        if (!bih->biSizeImage)
            bih->biCompression = 0;
        DWORD header = bih->biSize + 4 * bih->biClrUsed;
        if (header > size)
            return AVIERR_BADFORMAT;
        m->format = m->block;
        m->format_size = header;
        m->data = m->block + header;
        m->data_size = bih->biSizeImage ? bih->biSizeImage : (DWORD)(size - header);
        if (m->data_size > size - header)
            m->data_size = (DWORD)(size - header);
        m->info.fccType = FCC_vids;
        m->info.dwScale = 1;
        m->info.dwRate = 15;
        m->info.dwLength = 1;
        SetRect(&m->info.rcFrame, 0, 0, bih->biWidth, bih->biHeight);
        return S_OK;
    }
    /* a wave file: the format and the samples. */
    SIZE_T at = 12;
    if (size < 12 || memcmp(m->block, "RIFF", 4) || memcmp(m->block + 8, "WAVE", 4))
        return AVIERR_BADFORMAT;
    while (at + 8 <= size) {
        DWORD id, length;
        memcpy(&id, m->block + at, 4);
        memcpy(&length, m->block + at + 4, 4);
        if (length > size - at - 8)
            length = (DWORD)(size - at - 8);
        if (id == FCC_fmt && !m->format && length >= 14) {
            m->format = m->block + at + 8;
            m->format_size = length;
        } else if (id == FCC_data && m->format && !m->data) {
            m->data = m->block + at + 8;
            m->data_size = length;
        }
        at += 8 + (SIZE_T)length + (length & 1);
    }
    if (!m->format || !m->data)
        return AVIERR_BADFORMAT;
    const WAVEFORMATEX *wf = (const WAVEFORMATEX *)m->format;
    if (!wf->nBlockAlign)
        return AVIERR_BADFORMAT;
    m->info.fccType = FCC_auds;
    m->info.dwScale = wf->nBlockAlign;
    m->info.dwRate = wf->nAvgBytesPerSec;
    m->info.dwLength = m->data_size / wf->nBlockAlign;
    m->info.dwSampleSize = wf->nBlockAlign;
    return S_OK;
}

HRESULT AviMakeStreamFromMemory(UINT format, const void *data, SIZE_T size, PAVISTREAM *stream)
{
    MemStream *m = AviAlloc(sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    m->block = AviAlloc(size);
    if (!m->block) {
        AviFree(m);
        return E_OUTOFMEMORY;
    }
    memcpy(m->block, data, size);
    m->lpVtbl = &g_mem_vtbl;
    HRESULT hr = mem_describe(m, format, size);
    if (FAILED(hr)) {
        AviFree(m->block);
        AviFree(m);
        return hr;
    }
    m->refs = 1;
    AviUseAdd(1);
    *stream = (PAVISTREAM)m;
    return S_OK;
}

HRESULT WINAPI AVIMakeStreamFromClipboard(UINT format, HANDLE global, PAVISTREAM *stream)
{
    if (format != CF_DIB && format != CF_WAVE)
        return AVIERR_UNSUPPORTED;
    SIZE_T size = GlobalSize(global);
    void *data = GlobalLock(global);
    if (!data)
        return E_OUTOFMEMORY;
    HRESULT hr = AviMakeStreamFromMemory(format, data, size, stream);
    GlobalUnlock(global);
    return hr;
}

/* ------------------------------------------------------------------ a file made of streams */

typedef struct FakeFile {
    const IAVIFileVtbl *lpVtbl;
    LONG refs;
    AVIFILEINFOW info;
    DWORD count;
    PAVISTREAM streams[64];
} FakeFile;

static HRESULT STDMETHODCALLTYPE fake_qi(IAVIFile *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAVIFile)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE fake_addref(IAVIFile *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&((FakeFile *)This)->refs);
}

static ULONG STDMETHODCALLTYPE fake_release(IAVIFile *This)
{
    FakeFile *f = (FakeFile *)This;
    LONG refs = InterlockedDecrement(&f->refs);
    AviUseAdd(-1);
    if (!refs) {
        for (DWORD i = 0; i < f->count; i++)
            f->streams[i]->lpVtbl->Release(f->streams[i]);
        AviFree(f);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE fake_info(IAVIFile *This, AVIFILEINFOW *info, LONG size)
{
    memcpy(info, &((FakeFile *)This)->info, (ULONG)size < AVIFILEINFOW_SIZE ? (SIZE_T)size : AVIFILEINFOW_SIZE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fake_getstream(IAVIFile *This, PAVISTREAM *stream, DWORD type, LONG index)
{
    FakeFile *f = (FakeFile *)This;
    PAVISTREAM found = nullptr;
    *stream = nullptr;
    if (!type) {
        if (index >= 0 && (DWORD)index < f->count)
            found = f->streams[index];
    } else {
        for (DWORD i = 0; i < f->count && !found; i++) {
            AVISTREAMINFOW info = {0};
            AVIStreamInfoW(f->streams[i], &info, AVISTREAMINFOW_SIZE);
            if (info.fccType == type && !index--)
                found = f->streams[i];
        }
    }
    if (!found)
        return AVIERR_UNSUPPORTED;
    found->lpVtbl->AddRef(found);
    *stream = found;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fake_createstream(IAVIFile *This, PAVISTREAM *stream, AVISTREAMINFOW *info)
{
    return AVIERR_READONLY;
}

static HRESULT STDMETHODCALLTYPE fake_writedata(IAVIFile *This, DWORD ckid, void *data, LONG size)
{
    return AVIERR_READONLY;
}

static HRESULT STDMETHODCALLTYPE fake_readdata(IAVIFile *This, DWORD ckid, void *data, LONG *size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE fake_endrecord(IAVIFile *This)
{
    return AVIERR_READONLY;
}

static HRESULT STDMETHODCALLTYPE fake_deletestream(IAVIFile *This, DWORD type, LONG index)
{
    return AVIERR_READONLY;
}

static const IAVIFileVtbl g_fake_vtbl = {fake_qi,        fake_addref,       fake_release,    fake_info,
                                         fake_getstream, fake_createstream, fake_writedata,  fake_readdata,
                                         fake_endrecord, fake_deletestream};

HRESULT WINAPI AVIMakeFileFromStreams(PAVIFILE *file, int count, PAVISTREAM *streams)
{
    if ((UINT)count > 64)
        return AVIERR_BADPARAM;
    FakeFile *f = AviAlloc(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &g_fake_vtbl;
    f->count = (DWORD)count;
    f->info.dwStreams = (DWORD)count;
    for (int i = 0; i < count; i++) {
        AVISTREAMINFOW info = {0};
        f->streams[i] = streams[i];
        streams[i]->lpVtbl->AddRef(streams[i]);
        streams[i]->lpVtbl->Info(streams[i], &info, AVISTREAMINFOW_SIZE);
        /* the rate of the file is the rate of its first stream, at the most a hundred a second. */
        if (!i) {
            f->info.dwScale = info.dwScale;
            f->info.dwRate = info.dwRate;
            if (!info.dwScale || !info.dwRate || MulDiv(1000000, (int)info.dwScale, (int)info.dwRate) < 10000) {
                f->info.dwScale = 1;
                f->info.dwRate = 100;
            }
        }
        LONG time = AVIStreamSampleToTime(streams[i], (LONG)info.dwLength);
        DWORD length = (DWORD)MulDiv(time, (int)f->info.dwRate, (int)(1000 * f->info.dwScale));
        if (length > f->info.dwLength)
            f->info.dwLength = length;
        if ((DWORD)info.rcFrame.right > f->info.dwWidth)
            f->info.dwWidth = (DWORD)info.rcFrame.right;
        if ((DWORD)info.rcFrame.bottom > f->info.dwHeight)
            f->info.dwHeight = (DWORD)info.rcFrame.bottom;
    }
    f->refs = 1;
    AviUseAdd(1);
    *file = (PAVIFILE)f;
    return S_OK;
}

/* ------------------------------------------------------------------ the clipboard */

typedef struct FormatList {
    const IEnumFORMATETCVtbl *lpVtbl;
    LONG refs;
    FORMATETC items[3];
    ULONG count;
    ULONG at;
} FormatList;

static HRESULT STDMETHODCALLTYPE enum_qi(IEnumFORMATETC *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IEnumFORMATETC)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_addref(IEnumFORMATETC *This)
{
    return (ULONG)InterlockedIncrement(&((FormatList *)This)->refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumFORMATETC *This)
{
    FormatList *e = (FormatList *)This;
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs)
        AviFree(e);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumFORMATETC *This, ULONG count, FORMATETC *items, ULONG *fetched)
{
    FormatList *e = (FormatList *)This;
    ULONG done = 0;
    while (done < count && e->at < e->count)
        items[done++] = e->items[e->at++];
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumFORMATETC *This, ULONG count)
{
    FormatList *e = (FormatList *)This;
    if (count > e->count - e->at) {
        e->at = e->count;
        return S_FALSE;
    }
    e->at += count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumFORMATETC *This)
{
    ((FormatList *)This)->at = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumFORMATETC *This, IEnumFORMATETC **clone)
{
    FormatList *e = (FormatList *)This;
    FormatList *copy;
    if (!clone)
        return E_INVALIDARG;
    copy = AviAlloc(sizeof(*copy));
    if (!copy) {
        *clone = nullptr;
        return E_OUTOFMEMORY;
    }
    *copy = *e;
    copy->refs = 1;
    *clone = (IEnumFORMATETC *)copy;
    return S_OK;
}

static const IEnumFORMATETCVtbl g_enum_vtbl = {enum_qi, enum_addref, enum_release, enum_next, enum_skip, enum_reset,
                                               enum_clone};

typedef struct ClipObject {
    const IDataObjectVtbl *lpVtbl;
    LONG refs;
    PAVIFILE file;
    FORMATETC formats[3];
    ULONG count;
} ClipObject;

static HRESULT STDMETHODCALLTYPE clip_qi(IDataObject *This, REFIID riid, void **object)
{
    ClipObject *c = (ClipObject *)This;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDataObject)) {
        InterlockedIncrement(&c->refs);
        *object = This;
        return S_OK;
    }
    if (c->file && IsEqualGUID(riid, &IID_IAVIFile)) {
        c->file->lpVtbl->AddRef(c->file);
        *object = c->file;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE clip_addref(IDataObject *This)
{
    return (ULONG)InterlockedIncrement(&((ClipObject *)This)->refs);
}

static ULONG STDMETHODCALLTYPE clip_release(IDataObject *This)
{
    ClipObject *c = (ClipObject *)This;
    LONG refs = InterlockedDecrement(&c->refs);
    if (!refs) {
        if (c->file)
            c->file->lpVtbl->Release(c->file);
        AviFree(c);
        AviExitOle();
    }
    return (ULONG)refs;
}

/* the first stream of a type in the file the object holds, or none. */
static PAVISTREAM clip_stream(ClipObject *c, DWORD type)
{
    PAVISTREAM stream = nullptr;
    c->file->lpVtbl->GetStream(c->file, &stream, type, 0);
    return stream;
}

static HRESULT clip_get_video(ClipObject *c, FORMATETC *format, STGMEDIUM *medium)
{
    PAVISTREAM stream = clip_stream(c, FCC_vids);
    HRESULT hr = E_FAIL;
    if (!stream)
        return E_FAIL;
    PGETFRAME frames = AVIStreamGetFrameOpen(stream, nullptr);
    if (!frames) {
        stream->lpVtbl->Release(stream);
        return E_FAIL;
    }
    BITMAPINFOHEADER *dib = AVIStreamGetFrame(frames, 0);
    if (!dib) {
        hr = E_OUTOFMEMORY;
    } else if (format->cfFormat == CF_DIB) {
        if (format->tymed & TYMED_HGLOBAL) {
            DWORD colors = 4 * dib->biClrUsed;
            UINT64 total = (UINT64)dib->biSize + colors + dib->biSizeImage;
            HGLOBAL global = total < 0x7FFFFFFF ? GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (SIZE_T)total) : nullptr;
            if (!global) {
                hr = E_OUTOFMEMORY;
            } else {
                memcpy(GlobalLock(global), dib, (SIZE_T)total);
                GlobalUnlock(global);
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                medium->pUnkForRelease = nullptr;
                hr = S_OK;
            }
        } else {
            hr = DV_E_FORMATETC;
        }
    } else if (format->tymed & TYMED_GDI) {
        HPALETTE palette = nullptr;
        DWORD colors = dib->biClrUsed;
        if (!colors && dib->biBitCount <= 8)
            colors = 1u << dib->biBitCount;
        if (colors && colors <= 256) {
            LOGPALETTE *logical = AviAlloc(sizeof(LOGPALETTE) + sizeof(PALETTEENTRY) * 256);
            if (logical) {
                const RGBQUAD *rgb = (const RGBQUAD *)((const BYTE *)dib + dib->biSize);
                logical->palVersion = 0x300;
                logical->palNumEntries = (WORD)colors;
                for (DWORD i = 0; i < colors; i++) {
                    logical->palPalEntry[i].peRed = rgb[i].rgbRed;
                    logical->palPalEntry[i].peGreen = rgb[i].rgbGreen;
                    logical->palPalEntry[i].peBlue = rgb[i].rgbBlue;
                    logical->palPalEntry[i].peFlags = 0;
                }
                palette = CreatePalette(logical);
                AviFree(logical);
            }
        } else {
            HDC dc = GetDC(nullptr);
            palette = CreateHalftonePalette(dc);
            ReleaseDC(nullptr, dc);
        }
        medium->tymed = TYMED_GDI;
        medium->hBitmap = (HBITMAP)palette;
        medium->pUnkForRelease = nullptr;
        hr = palette ? S_OK : E_OUTOFMEMORY;
    } else {
        hr = DV_E_FORMATETC;
    }
    frames->lpVtbl->Release(frames);
    stream->lpVtbl->Release(stream);
    return hr;
}

/* the samples of the first audio stream as the wave file they would make. */
static HRESULT clip_get_wave(ClipObject *c, STGMEDIUM *medium)
{
    PAVISTREAM stream = clip_stream(c, FCC_auds);
    AVISTREAMINFOW info = {0};
    LONG format_size = 0;
    HRESULT hr = E_FAIL;
    if (!stream)
        return E_FAIL;
    AVIStreamInfoW(stream, &info, AVISTREAMINFOW_SIZE);
    stream->lpVtbl->ReadFormat(stream, (LONG)info.dwStart, nullptr, &format_size);
    UINT64 data_size = (UINT64)info.dwSampleSize * info.dwLength;
    UINT64 total = data_size + (UINT64)format_size + 28;
    if (data_size > 0x7FFFFFFF || total > 0x7FFFFFFF) {
        hr = (HRESULT)0x80070216L;
    } else {
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (SIZE_T)total);
        if (!global) {
            hr = E_OUTOFMEMORY;
        } else {
            DWORD *at = GlobalLock(global);
            at[0] = FCC_RIFF;
            at[1] = (DWORD)total - 8;
            at[2] = FCC_WAVE;
            at[3] = FCC_fmt;
            at[4] = (DWORD)format_size;
            stream->lpVtbl->ReadFormat(stream, (LONG)info.dwStart, at + 5, &format_size);
            DWORD *tail = (DWORD *)((BYTE *)(at + 5) + format_size);
            tail[0] = FCC_data;
            tail[1] = (DWORD)data_size;
            stream->lpVtbl->Read(stream, (LONG)info.dwStart, (LONG)info.dwLength, tail + 2, (LONG)data_size, nullptr,
                                 nullptr);
            GlobalUnlock(global);
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = global;
            medium->pUnkForRelease = nullptr;
            hr = S_OK;
        }
    }
    stream->lpVtbl->Release(stream);
    return hr;
}

static HRESULT STDMETHODCALLTYPE clip_getdata(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    ClipObject *c = (ClipObject *)This;
    medium->pUnkForRelease = nullptr;
    medium->tymed = 0;
    if (format->cfFormat == CF_DIB || format->cfFormat == CF_PALETTE) {
        return clip_get_video(c, format, medium);
    }
    if (format->cfFormat == CF_WAVE)
        return clip_get_wave(c, medium);
    return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE clip_getdatahere(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE clip_querygetdata(IDataObject *This, FORMATETC *format)
{
    ClipObject *c = (ClipObject *)This;
    DWORD type = FCC_vids, mask = TYMED_HGLOBAL;
    if (format->cfFormat == CF_PALETTE) {
        mask = TYMED_GDI;
    } else if (format->cfFormat == CF_WAVE) {
        type = FCC_auds;
    } else if (format->cfFormat != CF_DIB) {
        return DV_E_FORMATETC;
    }
    PAVISTREAM stream = clip_stream(c, type);
    if (!stream)
        return DV_E_FORMATETC;
    stream->lpVtbl->Release(stream);
    return (mask & format->tymed) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE clip_canonical(IDataObject *This, FORMATETC *in, FORMATETC *out)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE clip_setdata(IDataObject *This, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE clip_enumformats(IDataObject *This, DWORD direction, IEnumFORMATETC **list)
{
    ClipObject *c = (ClipObject *)This;
    if (direction == DATADIR_GET) {
        FormatList *e = AviAlloc(sizeof(*e));
        if (!e) {
            *list = nullptr;
            return E_OUTOFMEMORY;
        }
        e->lpVtbl = &g_enum_vtbl;
        e->refs = 1;
        e->count = c->count;
        memcpy(e->items, c->formats, sizeof(e->items));
        *list = (IEnumFORMATETC *)e;
        return S_OK;
    }
    return direction == DATADIR_SET ? E_NOTIMPL : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE clip_dadvise(IDataObject *This, FORMATETC *format, DWORD flags, IAdviseSink *sink,
                                              DWORD *connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE clip_dunadvise(IDataObject *This, DWORD connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE clip_enumdadvise(IDataObject *This, IEnumSTATDATA **list)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl g_clip_vtbl = {clip_qi,           clip_addref,     clip_release,   clip_getdata,
                                            clip_getdatahere,  clip_querygetdata, clip_canonical, clip_setdata,
                                            clip_enumformats,  clip_dadvise,    clip_dunadvise, clip_enumdadvise};

/* the data object of a file: its picture as a bitmap and a palette, its sound as a wave file. */
static HRESULT make_data_object(PAVIFILE file, IDataObject **object)
{
    PAVISTREAM stream = nullptr;
    *object = nullptr;
    if (!file)
        return S_OK;
    file->lpVtbl->AddRef(file);
    ClipObject *c = AviAlloc(sizeof(*c));
    if (!c) {
        file->lpVtbl->Release(file);
        return AVIERR_MEMORY;
    }
    AviInitOle();
    c->lpVtbl = &g_clip_vtbl;
    c->refs = 1;
    c->file = file;
    c->formats[0] = (FORMATETC){CF_WAVE, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    c->formats[1] = (FORMATETC){CF_DIB, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    c->formats[2] = (FORMATETC){CF_PALETTE, nullptr, DVASPECT_CONTENT, -1, TYMED_GDI};
    c->count = 3;
    ULONG first = 0;
    if (file->lpVtbl->GetStream(file, &stream, FCC_vids, 0) != S_OK) {
        c->count = 1;
    } else {
        BITMAPINFOHEADER bih = {0};
        AVISTREAMINFOW info = {0};
        LONG size = sizeof(bih);
        AVIStreamInfoW(stream, &info, AVISTREAMINFOW_SIZE);
        stream->lpVtbl->ReadFormat(stream, (LONG)info.dwStart, &bih, &size);
        if (bih.biBitCount > 8)
            c->count--;
        stream->lpVtbl->Release(stream);
    }
    if (file->lpVtbl->GetStream(file, &stream, FCC_auds, 0) != S_OK) {
        c->count--;
        first = 1;
    } else {
        stream->lpVtbl->Release(stream);
    }
    if (first) {
        memmove(c->formats, c->formats + 1, 2 * sizeof(FORMATETC));
    }
    *object = (IDataObject *)c;
    return S_OK;
}

HRESULT WINAPI AVIPutFileOnClipboard(PAVIFILE file)
{
    IDataObject *object = nullptr;
    AviInitOle();
    make_data_object(file, &object);
    HRESULT hr = OleSetClipboard(object);
    if (object)
        object->lpVtbl->Release(object);
    AviExitOle();
    return hr;
}

HRESULT WINAPI AVIGetFromClipboard(PAVIFILE *file)
{
    IDataObject *data = nullptr;
    HRESULT hr = S_OK;
    if (!file)
        return E_POINTER;
    *file = nullptr;
    AviInitOle();
    OleGetClipboard(&data);
    if (data) {
        data->lpVtbl->QueryInterface(data, &IID_IAVIFile, (void **)file);
        if (!*file) {
            PAVISTREAM streams[2];
            int count = 0;
            FORMATETC format = {CF_DIB, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium = {0};
            if (data->lpVtbl->GetData(data, &format, &medium) == S_OK) {
                if (AVIMakeStreamFromClipboard(CF_DIB, medium.hGlobal, &streams[count]) == S_OK)
                    count++;
                ReleaseStgMedium(&medium);
            }
            format.cfFormat = CF_WAVE;
            memset(&medium, 0, sizeof(medium));
            if (data->lpVtbl->GetData(data, &format, &medium) == S_OK) {
                if (AVIMakeStreamFromClipboard(CF_WAVE, medium.hGlobal, &streams[count]) == S_OK)
                    count++;
                ReleaseStgMedium(&medium);
            }
            if (count) {
                hr = AVIMakeFileFromStreams(file, count, streams);
                while (count--)
                    streams[count]->lpVtbl->Release(streams[count]);
            } else {
                hr = AVIERR_NODATA;
            }
        }
        data->lpVtbl->Release(data);
    }
    AviExitOle();
    return hr;
}

HRESULT WINAPI AVIClearClipboard(void)
{
    AviInitOle();
    HRESULT hr = OleFlushClipboard();
    AviExitOle();
    return hr;
}
