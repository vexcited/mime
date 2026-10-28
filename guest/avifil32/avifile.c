/*
 * the AVI file handler: IAVIFile and IAVIStream over RIFF AVI files. Reading follows the idx1 index, or the chunks
 * of the movi list when the file has none. Writing keeps the header in front of the data in the space it reserved,
 * a multiple of 2048 bytes, and writes the header, the idx1 index and the chunks kept as they are when the file is
 * released.
 */
#include "avifil32_private.h"

#define MAX_STREAMS 64
#define PAD_UNIT 2048

/* what the handler knows about a chunk of a stream. */
#define CK_KEY 0x0020
#define CK_NONKEY 0x0040
#define CK_ANY 0x0060
#define CK_PALETTE 0x0080

#define TYPE_PC 0x6370
#define TYPE_WB 0x6277
#define TYPE_DB 0x6264
#define TYPE_DC 0x6364
#define REC_STREAM 127

typedef struct Chunk {
    UINT32 offset;
    UINT32 size;
    INT32 first;
    INT32 count;
    UINT16 flags;
    UINT16 type;
} Chunk;

/* a palette change of a video stream: it belongs to the frames from pos on. */
typedef struct PaletteChange {
    UINT32 offset;
    UINT32 size;
    INT32 pos;
} PaletteChange;

/* the index of the movi list in the order of the file. */
typedef struct IndexRecord {
    UINT32 offset;
    UINT32 size;
    UINT16 flags;
    UINT16 type;
    UINT16 stream;
    UINT16 list;
} IndexRecord;

typedef struct AviFile AviFile;

typedef struct AviStream {
    const IAVIStreamVtbl *lpVtbl;
    const IAVIStreamingVtbl *lpStreaming;
    AviFile *file;
    LONG refs;
    int number;
    AVISTREAMINFOW info;
    BYTE *format;
    DWORD format_size;
    AviExtra extra;
    Chunk *chunks;
    UINT chunk_count, chunk_capacity;
    PaletteChange *changes;
    UINT change_count, change_capacity;
    INT32 first_pos;
    INT32 end_pos;
    UINT32 max_chunk;
    UINT32 empty_chunks;
    UINT32 key_chunks;
    BOOL plain_video;
    BOOL started;
    BOOL named;
} AviStream;

struct AviFile {
    const IUnknownVtbl *lpUnknown;
    const IAVIFileVtbl *lpFile;
    const IAVIPersistFileVtbl *lpPersist;
    IUnknown *outer;
    LONG refs;
    CRITICAL_SECTION lock;
    AviIo *io;
    WCHAR path[MAX_PATH];
    UINT mode;
    BOOL dirty;
    AviMainHeader header;
    DWORD stream_count;
    AviStream *streams[MAX_STREAMS];
    AviExtra extra;
    IndexRecord *index;
    UINT index_count, index_capacity;
    UINT32 movi_off;
    UINT32 end_off;
    UINT32 header_size;
    BOOL rec_open;
    UINT rec_index;
    UINT32 rec_start;
};

#define FILE_FROM(p, field) ((AviFile *)((BYTE *)(p) - offsetof(AviFile, field)))
#define STREAM_FROM(p, field) ((AviStream *)((BYTE *)(p) - offsetof(AviStream, field)))

static BOOL writable(const AviFile *f)
{
    return (f->mode & 3) != 0;
}

/* ------------------------------------------------------------------ chunk ids */

static int hex_value(DWORD c)
{
    c &= 0xFF;
    if (c >= '0' && c <= '9')
        return (int)(c - '0');
    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a' + 10);
    return -1;
}

static int ckid_stream(DWORD ckid)
{
    int high = hex_value(ckid), low = hex_value(ckid >> 8);
    return high < 0 || low < 0 ? -1 : high * 16 + low;
}

static DWORD make_ckid(int stream, UINT16 type)
{
    static const char digits[] = "0123456789ABCDEF";
    return (DWORD)(BYTE)digits[(stream >> 4) & 15] | ((DWORD)(BYTE)digits[stream & 15] << 8) | ((DWORD)type << 16);
}

/* ------------------------------------------------------------------ the arrays of a file */

static BOOL grow(void **array, UINT count, UINT *capacity, SIZE_T item)
{
    if (count < *capacity)
        return TRUE;
    UINT wanted = *capacity ? *capacity * 2 : 256;
    void *grown = AviRealloc(*array, (SIZE_T)wanted * item);
    if (!grown)
        return FALSE;
    *array = grown;
    *capacity = wanted;
    return TRUE;
}

static HRESULT index_add(AviFile *f, UINT32 offset, UINT32 size, UINT16 flags, UINT16 type, UINT16 stream, UINT16 list)
{
    if (!grow((void **)&f->index, f->index_count, &f->index_capacity, sizeof(IndexRecord)))
        return AVIERR_MEMORY;
    IndexRecord *r = &f->index[f->index_count++];
    r->offset = offset;
    r->size = size;
    r->flags = flags;
    r->type = type;
    r->stream = stream;
    r->list = list;
    return S_OK;
}

/* how many samples a chunk of the stream holds. */
static INT32 chunk_samples(const AviStream *s, UINT32 size)
{
    return s->info.dwSampleSize ? (INT32)(size / s->info.dwSampleSize) : 1;
}

static HRESULT stream_add_chunk(AviStream *s, UINT32 offset, UINT32 size, UINT16 flags, UINT16 type)
{
    /* the samples of a stream are counted from where its start less its initial frames puts the first one. */
    if (!s->started) {
        s->first_pos = s->end_pos = (INT32)(s->info.dwStart - s->info.dwInitialFrames);
        s->started = TRUE;
    }
    if (flags & CK_PALETTE) {
        if (!grow((void **)&s->changes, s->change_count, &s->change_capacity, sizeof(PaletteChange)))
            return AVIERR_MEMORY;
        PaletteChange *change = &s->changes[s->change_count++];
        change->offset = offset;
        change->size = size;
        change->pos = s->end_pos;
        s->info.dwFlags |= AVISTREAMINFO_FORMATCHANGES;
        return S_OK;
    }
    INT32 count = chunk_samples(s, size);
    if (count < 1 && s->info.dwSampleSize)
        return S_OK;
    if (!grow((void **)&s->chunks, s->chunk_count, &s->chunk_capacity, sizeof(Chunk)))
        return AVIERR_MEMORY;
    Chunk *c = &s->chunks[s->chunk_count++];
    /* the first chunk of a stream is always a place to start from. */
    if (s->end_pos == s->first_pos)
        flags |= CK_KEY;
    c->offset = offset;
    c->size = size;
    c->first = s->end_pos;
    c->count = count;
    c->flags = flags;
    c->type = type;
    s->end_pos += count;
    if (size > s->max_chunk)
        s->max_chunk = size;
    if (!size)
        s->empty_chunks++;
    if (flags & CK_KEY)
        s->key_chunks++;
    return S_OK;
}

/* the chunk holding a sample, or the index of the first chunk after it. */
static UINT find_chunk(const AviStream *s, INT32 pos, BOOL *inside)
{
    UINT low = 0, high = s->chunk_count;
    while (low < high) {
        UINT mid = low + (high - low) / 2;
        if (s->chunks[mid].first + s->chunks[mid].count <= pos)
            low = mid + 1;
        else
            high = mid;
    }
    *inside = low < s->chunk_count && s->chunks[low].first <= pos;
    return low;
}

/* ------------------------------------------------------------------ streams */

static HRESULT STDMETHODCALLTYPE stream_qi(IAVIStream *This, REFIID riid, void **object)
{
    AviStream *s = (AviStream *)This;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAVIStream))
        *object = &s->lpVtbl;
    else if (IsEqualGUID(riid, &IID_IAVIStreaming_))
        *object = &s->lpStreaming;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE stream_addref(IAVIStream *This)
{
    AviStream *s = (AviStream *)This;
    AviUseAdd(1);
    LONG refs = InterlockedIncrement(&s->refs);
    /* the first reference of a stream keeps its file alive. */
    if (refs == 1)
        s->file->outer->lpVtbl->AddRef(s->file->outer);
    return (ULONG)refs;
}

static ULONG STDMETHODCALLTYPE stream_release(IAVIStream *This)
{
    AviStream *s = (AviStream *)This;
    IUnknown *outer = s->file->outer;
    LONG refs = InterlockedDecrement(&s->refs);
    AviUseAdd(-1);
    if (!refs)
        outer->lpVtbl->Release(outer);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE streaming_qi(IAVIStreaming *This, REFIID riid, void **object)
{
    AviStream *s = STREAM_FROM(This, lpStreaming);
    return stream_qi((IAVIStream *)s, riid, object);
}

static ULONG STDMETHODCALLTYPE streaming_addref(IAVIStreaming *This)
{
    return stream_addref((IAVIStream *)STREAM_FROM(This, lpStreaming));
}

static ULONG STDMETHODCALLTYPE streaming_release(IAVIStreaming *This)
{
    return stream_release((IAVIStream *)STREAM_FROM(This, lpStreaming));
}

static HRESULT STDMETHODCALLTYPE streaming_begin(IAVIStreaming *This, LONG start, LONG end, LONG rate)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE streaming_end(IAVIStreaming *This)
{
    return S_OK;
}

static const IAVIStreamingVtbl g_streaming_vtbl = {streaming_qi, streaming_addref, streaming_release, streaming_begin,
                                                   streaming_end};

static HRESULT STDMETHODCALLTYPE stream_create(IAVIStream *This, LPARAM param1, LPARAM param2)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE stream_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    if (!info)
        return AVIERR_BADPARAM;
    if ((ULONG)size < AVISTREAMINFOW_SIZE)
        return AVIERR_BUFFERTOOSMALL;
    EnterCriticalSection(&f->lock);
    *info = s->info;
    LeaveCriticalSection(&f->lock);
    return S_OK;
}

/* the first chunk at or after a position the test accepts, going the way the flags say. */
static BOOL chunk_wanted(const Chunk *c, UINT mask)
{
    return (c->flags & mask) != 0;
}

static LONG stream_find(AviStream *s, LONG pos, LONG flags)
{
    UINT type = (UINT)(flags & FIND_TYPE);
    UINT ret = (UINT)(flags & FIND_RET);
    BOOL forward = (flags & FIND_NEXT) != 0;
    BOOL inside;
    if (!s->chunk_count)
        return -1;
    if (pos < s->first_pos || pos >= s->end_pos) {
        if (!(type == FIND_FORMAT && pos >= s->end_pos && !forward))
            return -1;
    }
    if (type == FIND_FORMAT) {
        /* without palette changes there is only the format of the start. */
        if (!s->change_count)
            return !forward || pos <= s->first_pos ? s->first_pos : -1;
        if (forward) {
            for (UINT i = 0; i < s->change_count; i++)
                if (s->changes[i].pos > pos)
                    return s->changes[i].pos;
            return -1;
        }
        LONG found = -1;
        for (UINT i = 0; i < s->change_count; i++)
            if (s->changes[i].pos <= pos)
                found = s->changes[i].pos;
        return found;
    }
    UINT at = find_chunk(s, pos, &inside);
    if (!inside)
        return -1;
    UINT mask = type == FIND_KEY ? CK_KEY : type == FIND_ANY ? CK_ANY : 0;
    if (mask && !chunk_wanted(&s->chunks[at], mask)) {
        if (forward) {
            do
                at++;
            while (at < s->chunk_count && !chunk_wanted(&s->chunks[at], mask));
            if (at >= s->chunk_count)
                return -1;
        } else {
            do {
                if (!at)
                    return -1;
                at--;
            } while (!chunk_wanted(&s->chunks[at], mask));
        }
        pos = s->chunks[at].first;
    } else if (s->info.dwSampleSize == 0 || ret != FIND_POS) {
        pos = s->chunks[at].first;
    }
    switch (ret) {
    case FIND_LENGTH:
        return (LONG)s->chunks[at].size;
    case FIND_OFFSET:
        return (LONG)(s->chunks[at].offset + 8);
    case FIND_SIZE:
        return s->chunks[at].count;
    case FIND_INDEX:
        return (LONG)at;
    default:
        return pos;
    }
}

static LONG STDMETHODCALLTYPE stream_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    EnterCriticalSection(&f->lock);
    LONG result = -1;
    LONG start = (LONG)s->info.dwStart;
    if (!pos && (flags & FIND_FROM_START))
        pos = start;
    if (pos >= start && pos < start + (LONG)s->info.dwLength)
        result = stream_find(s, pos, flags);
    LeaveCriticalSection(&f->lock);
    return result < 0 ? -1 : result;
}

/* the colors of a video stream in the frames from pos on. */
static void palette_at(const AviStream *s, LONG pos, RGBQUAD *palette)
{
    const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)s->format;
    DWORD colors = bih->biClrUsed;
    if (colors > 256)
        colors = 256;
    memcpy(palette, s->format + bih->biSize, colors * sizeof(RGBQUAD));
    for (UINT i = 0; i < s->change_count && s->changes[i].pos <= pos; i++) {
        BYTE data[4 + 4 * 256];
        DWORD size = s->changes[i].size < sizeof(data) ? s->changes[i].size : (DWORD)sizeof(data);
        if (size < 4 || !AviIoRead(s->file->io, (UINT64)s->changes[i].offset + 8, data, size))
            continue;
        UINT first = data[0], number = data[1] ? data[1] : 256;
        for (UINT k = 0; k < number && 4 + 4 * (k + 1) <= size; k++) {
            if (first + k >= colors)
                break;
            palette[first + k].rgbRed = data[4 + 4 * k];
            palette[first + k].rgbGreen = data[5 + 4 * k];
            palette[first + k].rgbBlue = data[6 + 4 * k];
            palette[first + k].rgbReserved = 0;
        }
    }
}

static HRESULT STDMETHODCALLTYPE stream_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    HRESULT hr = S_OK;
    EnterCriticalSection(&f->lock);
    if (!size) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_BADPARAM;
    }
    if (!format || !*size) {
        *size = (LONG)s->format_size;
        LeaveCriticalSection(&f->lock);
        return S_OK;
    }
    if (*size < 0) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_BADPARAM;
    }
    LONG room = *size;
    LONG count = room < (LONG)s->format_size ? room : (LONG)s->format_size;
    if (count)
        memcpy(format, s->format, (SIZE_T)count);
    if (s->format && s->info.fccType == FCC_vids && (s->info.dwFlags & AVISTREAMINFO_FORMATCHANGES)) {
        const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)s->format;
        DWORD colors = bih->biClrUsed > 256 ? 256 : bih->biClrUsed;
        if (bih->biSize < (DWORD)room && colors) {
            RGBQUAD palette[256];
            LONG bytes = (LONG)(colors * sizeof(RGBQUAD));
            palette_at(s, pos, palette);
            if (bytes > room - (LONG)bih->biSize)
                bytes = room - (LONG)bih->biSize;
            memcpy((BYTE *)format + bih->biSize, palette, (SIZE_T)bytes);
        }
    }
    if (room < (LONG)s->format_size)
        hr = AVIERR_BUFFERTOOSMALL;
    *size = (LONG)s->format_size;
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT stream_write_locked(AviStream *s, LONG start, LONG samples, void *buffer, LONG size, DWORD flags,
                                   LONG *samples_written, LONG *bytes_written);

static HRESULT STDMETHODCALLTYPE stream_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                              DWORD flags, LONG *samples_written, LONG *bytes_written)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    EnterCriticalSection(&f->lock);
    HRESULT hr = stream_write_locked(s, start, samples, buffer, size, flags, samples_written, bytes_written);
    LeaveCriticalSection(&f->lock);
    return hr;
}

static void note_header_size(AviFile *f, DWORD bytes)
{
    f->header_size += bytes;
}

/* the room the header may use is what lies in front of the movi list. */
static BOOL header_fits(const AviFile *f, DWORD bytes)
{
    return !f->end_off || (UINT64)f->header_size + bytes <= f->movi_off;
}

static HRESULT STDMETHODCALLTYPE stream_setformat(IAVIStream *This, LONG pos, void *format, LONG size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    HRESULT hr = S_OK;
    EnterCriticalSection(&f->lock);
    if (!writable(f)) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_READONLY;
    }
    if (!format || size <= 0) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_BADPARAM;
    }
    if (!s->format) {
        if (!header_fits(f, (DWORD)size + 8)) {
            LeaveCriticalSection(&f->lock);
            return AVIERR_UNSUPPORTED;
        }
        note_header_size(f, (DWORD)size + 8);
        s->format = AviAlloc((SIZE_T)size);
        if (!s->format) {
            LeaveCriticalSection(&f->lock);
            return AVIERR_MEMORY;
        }
        memcpy(s->format, format, (SIZE_T)size);
        s->format_size = (DWORD)size;
        if (s->info.fccType == FCC_vids && size >= (LONG)sizeof(BITMAPINFOHEADER)) {
            const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)format;
            RECT *rc = &s->info.rcFrame;
            if (IsRectEmpty(rc) || rc->right - rc->left > (LONG)f->header.dwWidth ||
                rc->bottom - rc->top > (LONG)f->header.dwHeight)
                SetRect(rc, 0, 0, bih->biWidth, bih->biHeight);
            if (rc->right > (LONG)f->header.dwWidth)
                f->header.dwWidth = (DWORD)rc->right;
            if (rc->bottom > (LONG)f->header.dwHeight)
                f->header.dwHeight = (DWORD)rc->bottom;
        }
        f->dirty = TRUE;
        LeaveCriticalSection(&f->lock);
        return S_OK;
    }
    if ((DWORD)size == s->format_size && !memcmp(s->format, format, (SIZE_T)size)) {
        LeaveCriticalSection(&f->lock);
        return S_OK;
    }
    /* the only change a format may have later is the palette of a video with 8 bits or less. */
    const BITMAPINFOHEADER *old = (const BITMAPINFOHEADER *)s->format;
    const BITMAPINFOHEADER *now = (const BITMAPINFOHEADER *)format;
    if (s->info.fccType != FCC_vids || pos < (LONG)(s->info.dwStart + s->info.dwLength) ||
        (DWORD)size != s->format_size || size < (LONG)sizeof(BITMAPINFOHEADER) || now->biBitCount > 8 ||
        !now->biClrUsed || now->biCompression != old->biCompression || now->biSize != old->biSize ||
        now->biWidth != old->biWidth || now->biHeight != old->biHeight || now->biBitCount != old->biBitCount) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_UNSUPPORTED;
    }
    DWORD colors = now->biClrUsed > 256 ? 256 : now->biClrUsed;
    if (now->biSize + colors * sizeof(RGBQUAD) > (DWORD)size) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_UNSUPPORTED;
    }
    const RGBQUAD *colors_now = (const RGBQUAD *)((const BYTE *)format + now->biSize);
    RGBQUAD current[256];
    palette_at(s, pos, current);
    if (!memcmp(current, colors_now, colors * sizeof(RGBQUAD))) {
        LeaveCriticalSection(&f->lock);
        return S_OK;
    }
    BYTE data[4 + 4 * 256] = {0};
    data[1] = (BYTE)colors;
    for (DWORD i = 0; i < colors; i++) {
        data[4 + 4 * i] = colors_now[i].rgbRed;
        data[5 + 4 * i] = colors_now[i].rgbGreen;
        data[6 + 4 * i] = colors_now[i].rgbBlue;
    }
    hr = stream_write_locked(s, pos, 0, data, (LONG)(4 + 4 * colors), AVIIF_NOTIME, nullptr, nullptr);
    LeaveCriticalSection(&f->lock);
    return hr;
}

/* reads the bytes of the samples from start on, following the chunks. A negative result is a failure. */
static LONG chunk_read(AviStream *s, LONG start, LONG samples, void *buffer, LONG size)
{
    AviFile *f = s->file;
    BOOL inside;
    DWORD sample_size = s->info.dwSampleSize;
    if (start < s->first_pos || start >= s->end_pos)
        return -1;
    UINT at = find_chunk(s, start, &inside);
    if (!inside)
        return -1;
    const Chunk *c = &s->chunks[at];
    LONG limit = size;
    if (!buffer && !size && samples)
        limit = 0x7FFFFFFF;
    LONG bytes;
    if (sample_size) {
        LONG available = (LONG)c->size - (start - c->first) * (LONG)sample_size;
        LONG count;
        if (samples == -1 && limit >= available) {
            limit = available;
            count = available / (LONG)sample_size;
        } else {
            count = limit / (LONG)sample_size;
            if (samples > 0 && samples < count)
                count = samples;
        }
        bytes = count * (LONG)sample_size;
    } else {
        bytes = (LONG)c->size;
    }
    if (!buffer)
        return bytes;
    if (limit < bytes)
        return -1;
    LONG total = 0;
    BYTE *out = buffer;
    LONG remaining = bytes;
    LONG position = start;
    while (remaining > 0) {
        LONG skip = sample_size ? (position - c->first) * (LONG)sample_size : 0;
        LONG take = (LONG)c->size - skip;
        if (take > remaining)
            take = remaining;
        if (take <= 0 || !AviIoRead(f->io, (UINT64)c->offset + 8 + (UINT64)skip, out, (DWORD)take))
            return -1;
        out += take;
        total += take;
        remaining -= take;
        if (!sample_size || remaining <= 0)
            break;
        position += take / (LONG)sample_size;
        at = find_chunk(s, position, &inside);
        if (!inside)
            break;
        c = &s->chunks[at];
    }
    return total;
}

static HRESULT STDMETHODCALLTYPE stream_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                             LONG *bytes, LONG *samples_read)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    HRESULT hr = S_OK;
    EnterCriticalSection(&f->lock);
    if (start < (LONG)s->info.dwStart) {
        LeaveCriticalSection(&f->lock);
        return AVIERR_BADPARAM;
    }
    LONG read = chunk_read(s, start, samples, buffer, size);
    if (read >= 0) {
        if (bytes)
            *bytes = read;
        if (samples_read)
            *samples_read = s->info.dwSampleSize ? read / (LONG)s->info.dwSampleSize : 1;
    } else {
        if (bytes)
            *bytes = 0;
        if (samples_read)
            *samples_read = 0;
        if (!size) {
            hr = AVIERR_ERROR;
        } else if (size < (LONG)s->info.dwSampleSize) {
            hr = AVIERR_BUFFERTOOSMALL;
        } else {
            LONG needed = stream_find(s, start, FIND_PREV | FIND_LENGTH);
            if (size < needed) {
                if (bytes)
                    *bytes = needed;
                hr = AVIERR_BUFFERTOOSMALL;
            } else {
                hr = AVIERR_FILEREAD;
            }
        }
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_delete(IAVIStream *This, LONG start, LONG samples)
{
    AviStream *s = (AviStream *)This;
    return writable(s->file) ? AVIERR_UNSUPPORTED : AVIERR_READONLY;
}

static HRESULT STDMETHODCALLTYPE stream_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    if (!size)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&f->lock);
    HRESULT hr = AviExtraRead(&s->extra, fcc, data, size);
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    HRESULT hr;
    EnterCriticalSection(&f->lock);
    if (!writable(f))
        hr = AVIERR_READONLY;
    else if (size < 0 || (size && !data))
        hr = AVIERR_BADPARAM;
    else if (!header_fits(f, (DWORD)size + 8))
        hr = AVIERR_UNSUPPORTED;
    else {
        note_header_size(f, (DWORD)size + 12);
        f->dirty = TRUE;
        hr = AviExtraAdd(&s->extra, fcc, data, (DWORD)size);
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    AviStream *s = (AviStream *)This;
    AviFile *f = s->file;
    HRESULT hr = S_OK;
    EnterCriticalSection(&f->lock);
    if (!writable(f))
        hr = AVIERR_READONLY;
    else if (size < AVISTREAMINFOW_SIZE || !info)
        hr = AVIERR_BADPARAM;
    else {
        s->info.wPriority = info->wPriority;
        s->info.wLanguage = info->wLanguage;
        s->info.dwScale = info->dwScale;
        s->info.dwRate = info->dwRate;
        s->info.dwStart = info->dwStart;
        s->info.dwQuality = info->dwQuality;
        s->info.rcFrame = info->rcFrame;
        if (info->szName[0])
            memcpy(s->info.szName, info->szName, sizeof(s->info.szName));
        f->dirty = TRUE;
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static const IAVIStreamVtbl g_stream_vtbl = {stream_qi,       stream_addref,    stream_release,   stream_create,
                                             stream_info,     stream_findsample, stream_readformat, stream_setformat,
                                             stream_read,     stream_write,     stream_delete,    stream_readdata,
                                             stream_writedata, stream_setinfo};

static AviStream *stream_new(AviFile *f, int number)
{
    AviStream *s = AviAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    s->lpVtbl = &g_stream_vtbl;
    s->lpStreaming = &g_streaming_vtbl;
    s->file = f;
    s->number = number;
    return s;
}

static void stream_free(AviStream *s)
{
    AviFree(s->format);
    AviFree(s->chunks);
    AviFree(s->changes);
    AviExtraFree(&s->extra);
    AviFree(s);
}

/* ------------------------------------------------------------------ writing */

/* the chunk id of a block of a stream: key frames of video are written as DIB blocks, the others as compressed. */
static UINT16 write_type(const AviStream *s, DWORD flags, LONG size)
{
    if (s->info.fccType == FCC_auds)
        return TYPE_WB;
    if (s->info.fccType != FCC_vids)
        return TYPE_DC;
    if (flags & AVIIF_NOTIME)
        return TYPE_PC;
    const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)s->format;
    if (s->format_size < sizeof(*bih))
        return TYPE_DC;
    if ((flags & AVIIF_KEYFRAME) || (bih->biCompression <= 1 && (DWORD)size == bih->biSizeImage))
        return TYPE_DB;
    return TYPE_DC;
}

/* where the data starts: the first multiple of 2048 after the header, less the headers of the lists. */
static void fix_layout(AviFile *f)
{
    if (!f->end_off) {
        f->end_off = (f->header_size + 3071) & ~(PAD_UNIT - 1u);
        f->movi_off = f->end_off - 12;
    }
}

static HRESULT emit_chunk(AviFile *f, DWORD ckid, const void *data, DWORD size, UINT32 *offset)
{
    UINT64 end = (UINT64)f->end_off + 8 + size + (size & 1);
    DWORD head[2] = {ckid, size};
    if (end > 0xFFFFFFF0ull)
        return AVIERR_FILEWRITE;
    if (!AviIoWrite(f->io, f->end_off, head, 8) || (size && !AviIoWrite(f->io, f->end_off + 8ull, data, size)))
        return AVIERR_FILEWRITE;
    if ((size & 1) && !AviIoZero(f->io, f->end_off + 8ull + size, 1))
        return AVIERR_FILEWRITE;
    *offset = f->end_off;
    f->end_off = (UINT32)end;
    return S_OK;
}

static HRESULT stream_write_locked(AviStream *s, LONG start, LONG samples, void *buffer, LONG size, DWORD flags,
                                   LONG *samples_written, LONG *bytes_written)
{
    AviFile *f = s->file;
    if (!writable(f))
        return AVIERR_READONLY;
    if (!s->format)
        return E_UNEXPECTED;
    if (size < 0 || (size && !buffer))
        return AVIERR_BADPARAM;
    UINT16 type = write_type(s, flags, size);
    LONG end = (LONG)(s->info.dwStart + s->info.dwLength);
    if (start < 0)
        start = end;
    if (start > end) {
        /* a stream of video may skip frames, the gap is filled with empty ones. */
        if (s->info.dwSampleSize)
            return AVIERR_BADPARAM;
        while (end < start) {
            HRESULT hr = stream_write_locked(s, end, 1, nullptr, 0, 0, nullptr, nullptr);
            if (FAILED(hr))
                return hr;
            end = (LONG)(s->info.dwStart + s->info.dwLength);
        }
    }
    if (start < end)
        return AVIERR_UNSUPPORTED;
    f->dirty = TRUE;
    fix_layout(f);
    UINT32 offset;
    HRESULT hr = emit_chunk(f, make_ckid(s->number, type), buffer, (DWORD)size, &offset);
    if (FAILED(hr))
        return hr;
    UINT16 kind = type == TYPE_PC ? CK_PALETTE
                  : type == TYPE_WB ? CK_ANY
                  : !size           ? 0
                  : type == TYPE_DB ? CK_KEY
                                    : CK_NONKEY;
    hr = index_add(f, offset, (DWORD)size, kind, type, (UINT16)s->number, 0);
    if (FAILED(hr))
        return hr;
    hr = stream_add_chunk(s, offset, (DWORD)size, kind, type);
    if (FAILED(hr))
        return hr;
    if (type != TYPE_PC)
        s->info.dwLength += (DWORD)samples;
    if ((DWORD)size > s->info.dwSuggestedBufferSize)
        s->info.dwSuggestedBufferSize = (DWORD)size;
    if ((DWORD)size > f->header.dwSuggestedBufferSize)
        f->header.dwSuggestedBufferSize = (DWORD)size;
    if (s->info.dwRate && f->header.dwMicroSecPerFrame) {
        int millis = MulDiv((int)s->info.dwLength, (int)(1000 * s->info.dwScale), (int)s->info.dwRate);
        DWORD frames = (DWORD)MulDiv(millis, 1000, (int)f->header.dwMicroSecPerFrame);
        if (frames > f->header.dwTotalFrames)
            f->header.dwTotalFrames = frames;
    }
    if (samples_written)
        *samples_written = samples;
    if (bytes_written)
        *bytes_written = size;
    return S_OK;
}

/* ------------------------------------------------------------------ the header of a file */

typedef struct Buffer {
    BYTE *data;
    DWORD size;
    DWORD capacity;
    BOOL failed;
} Buffer;

static void buffer_put(Buffer *b, const void *data, DWORD size)
{
    if (b->failed)
        return;
    if (b->size + size > b->capacity) {
        DWORD wanted = b->capacity ? b->capacity * 2 : 4096;
        while (wanted < b->size + size)
            wanted *= 2;
        BYTE *grown = AviRealloc(b->data, wanted);
        if (!grown) {
            b->failed = TRUE;
            return;
        }
        b->data = grown;
        b->capacity = wanted;
    }
    if (size)
        memcpy(b->data + b->size, data, size);
    b->size += size;
}

static void buffer_dword(Buffer *b, DWORD value)
{
    buffer_put(b, &value, 4);
}

/* opens a chunk or a list, whose size is known when it is closed. */
static DWORD buffer_open(Buffer *b, DWORD ckid, DWORD form)
{
    DWORD at = b->size;
    buffer_dword(b, ckid);
    buffer_dword(b, 0);
    if (form)
        buffer_dword(b, form);
    return at;
}

static void buffer_close(Buffer *b, DWORD at)
{
    if (b->failed)
        return;
    DWORD size = b->size - at - 8;
    memcpy(b->data + at + 4, &size, 4);
    if (b->size & 1)
        buffer_put(b, "", 1);
}

static void buffer_chunk(Buffer *b, DWORD ckid, const void *data, DWORD size)
{
    DWORD at = buffer_open(b, ckid, 0);
    buffer_put(b, data, size);
    buffer_close(b, at);
}

static DWORD ansi_length(const WCHAR *name)
{
    int length = WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr) - 1;
    return length > 0 && length <= 259 ? (DWORD)length : 0;
}

/* the number of bytes the stream data between two samples takes. */
static UINT64 bytes_between(const AviStream *s, LONG from, LONG to)
{
    UINT64 total = 0;
    BOOL inside;
    if (from < s->first_pos)
        from = s->first_pos;
    if (to > s->end_pos)
        to = s->end_pos;
    if (to <= from)
        return 0;
    if (s->info.dwSampleSize)
        return (UINT64)(to - from) * s->info.dwSampleSize;
    for (UINT i = find_chunk(s, from, &inside); i < s->chunk_count && s->chunks[i].first < to; i++)
        total += s->chunks[i].size;
    return total;
}

/* the highest rate of data in any window of one second, measured every 250 milliseconds. */
static DWORD data_rate(AviFile *f)
{
    LONG longest = 0;
    UINT64 best = 0;
    for (DWORD i = 0; i < f->stream_count; i++) {
        PAVISTREAM stream = (PAVISTREAM)&f->streams[i]->lpVtbl;
        LONG time = AVIStreamSampleToTime(stream, AVIStreamStart(stream) + AVIStreamLength(stream));
        if (time > longest)
            longest = time;
    }
    LONG at = 0;
    do {
        UINT64 sum = 0;
        at += 250;
        for (DWORD i = 0; i < f->stream_count; i++) {
            PAVISTREAM stream = (PAVISTREAM)&f->streams[i]->lpVtbl;
            sum += bytes_between(f->streams[i], AVIStreamTimeToSample(stream, at),
                                 AVIStreamTimeToSample(stream, at + 1000));
        }
        if (sum > best)
            best = sum;
    } while (at < longest);
    return best > 0xFFFFFFFFull ? 0xFFFFFFFFu : (DWORD)best;
}

/* the idx1 chunk: what the index of the file says in the way an AVI file keeps it. */
static void write_index(AviFile *f, Buffer *b)
{
    DWORD at = buffer_open(b, FCC_idx1, 0);
    UINT32 base = f->movi_off + 8;
    for (UINT i = 0; i < f->index_count; i++) {
        const IndexRecord *r = &f->index[i];
        AviIndexEntry entry;
        if (r->list) {
            entry.ckid = FCC_rec;
            entry.dwFlags = AVIIF_LIST;
        } else {
            entry.ckid = make_ckid(r->stream, r->type);
            entry.dwFlags = (r->flags & CK_KEY ? AVIIF_KEYFRAME : 0) | (r->flags & CK_PALETTE ? AVIIF_NOTIME : 0);
            if (r->type == TYPE_WB)
                entry.dwFlags |= AVIIF_KEYFRAME;
        }
        entry.dwChunkOffset = r->offset - base;
        entry.dwChunkLength = r->size;
        buffer_put(b, &entry, sizeof(entry));
    }
    buffer_close(b, at);
}

/* the header lists in front of the data. */
static void write_header(AviFile *f, Buffer *b)
{
    DWORD riff = buffer_open(b, FCC_RIFF, FCC_AVI);
    DWORD hdrl = buffer_open(b, FCC_LIST, FCC_hdrl);
    f->header.dwStreams = f->stream_count;
    buffer_chunk(b, FCC_avih, &f->header, sizeof(f->header));
    for (DWORD i = 0; i < f->stream_count; i++) {
        const AviStream *s = f->streams[i];
        AviStreamHeader strh = {0};
        DWORD strl = buffer_open(b, FCC_LIST, FCC_strl);
        strh.fccType = s->info.fccType;
        strh.fccHandler = s->info.fccHandler;
        strh.dwFlags = s->info.dwFlags;
        strh.wPriority = s->info.wPriority;
        strh.wLanguage = s->info.wLanguage;
        strh.dwInitialFrames = s->info.dwInitialFrames;
        strh.dwScale = s->info.dwScale;
        strh.dwRate = s->info.dwRate;
        strh.dwStart = s->info.dwStart;
        strh.dwLength = s->info.dwLength;
        strh.dwSuggestedBufferSize = s->info.dwSuggestedBufferSize;
        strh.dwQuality = s->info.dwQuality;
        strh.dwSampleSize = s->info.dwSampleSize;
        strh.rcLeft = (SHORT)s->info.rcFrame.left;
        strh.rcTop = (SHORT)s->info.rcFrame.top;
        strh.rcRight = (SHORT)s->info.rcFrame.right;
        strh.rcBottom = (SHORT)s->info.rcFrame.bottom;
        buffer_chunk(b, FCC_strh, &strh, sizeof(strh));
        buffer_chunk(b, FCC_strf, s->format, s->format_size);
        DWORD length = ansi_length(s->info.szName);
        if (length) {
            char name[260];
            WideCharToMultiByte(CP_ACP, 0, s->info.szName, -1, name, sizeof(name), nullptr, nullptr);
            buffer_chunk(b, FCC_strn, name, length + 1);
        }
        buffer_put(b, s->extra.data, s->extra.size);
        buffer_close(b, strl);
    }
    buffer_close(b, hdrl);
    (void)riff;
}

static HRESULT end_record(AviFile *f);

static HRESULT save_changes(AviFile *f)
{
    Buffer head = {0}, tail = {0};
    HRESULT hr = S_OK;
    if (f->rec_open) {
        /* the record that is open is closed and the one after it, which has nothing in it, is dropped. */
        if (f->index_count > f->rec_index + 1) {
            hr = end_record(f);
            if (FAILED(hr))
                goto done;
        }
        f->index_count = f->rec_index;
        f->end_off = f->rec_start;
        f->rec_open = FALSE;
    }
    fix_layout(f);
    for (DWORD i = 0; i < f->stream_count; i++)
        if (!f->streams[i]->format) {
            hr = AVIERR_FILEWRITE;
            goto done;
        }
    f->header.dwMaxBytesPerSec = data_rate(f);
    write_header(f, &head);
    if (head.failed || head.size > f->movi_off) {
        hr = AVIERR_FILEWRITE;
        goto done;
    }
    /* the room left in front of the data is a JUNK chunk. */
    if (head.size + 8 <= f->movi_off) {
        DWORD junk = f->movi_off - head.size - 8;
        buffer_dword(&head, FCC_JUNK);
        buffer_dword(&head, junk);
        if (head.failed || !AviIoWrite(f->io, 0, head.data, head.size) || !AviIoZero(f->io, head.size, junk))
            hr = AVIERR_FILEWRITE;
    } else if (!AviIoWrite(f->io, 0, head.data, head.size)) {
        hr = AVIERR_FILEWRITE;
    }
    if (hr != S_OK)
        goto done;
    DWORD list[3] = {FCC_LIST, f->end_off - f->movi_off - 8, FCC_movi};
    if (!AviIoWrite(f->io, f->movi_off, list, 12)) {
        hr = AVIERR_FILEWRITE;
        goto done;
    }
    write_index(f, &tail);
    buffer_put(&tail, f->extra.data, f->extra.size);
    if (tail.failed || !AviIoWrite(f->io, f->end_off, tail.data, tail.size)) {
        hr = AVIERR_FILEWRITE;
        goto done;
    }
    {
        UINT64 end = (UINT64)f->end_off + tail.size;
        DWORD riff_size = (DWORD)(end - 8);
        if (end > 0xFFFFFFF0ull || !AviIoWrite(f->io, 4, &riff_size, 4) || !AviIoTruncate(f->io, end))
            hr = AVIERR_FILEWRITE;
    }
done:
    if (hr == S_OK && !AviIoFlush(f->io))
        hr = AVIERR_FILEWRITE;
    AviFree(head.data);
    AviFree(tail.data);
    return hr;
}

/* ------------------------------------------------------------------ opening a file */

static void default_name(const AviFile *f, const AVISTREAMINFOW *info, int ordinal, WCHAR *out)
{
    WCHAR type[16];
    WCHAR text[96];
    char number[12];
    int count = 0;
    SIZE_T at;
    if (info->fccType == FCC_vids)
        AviCopyName(type, AVI_STRING_VIDEO, 16);
    else if (info->fccType == FCC_auds)
        AviCopyName(type, AVI_STRING_AUDIO, 16);
    else {
        type[0] = '\'';
        for (int i = 0; i < 4; i++)
            type[1 + i] = (BYTE)(info->fccType >> (8 * i));
        type[5] = '\'';
        type[6] = 0;
    }
    AviCopyName(text, AviBaseName(f->path), 60);
    at = lstrlenW(text);
    text[at++] = ' ';
    AviCopyName(text + at, type, 16);
    at = lstrlenW(text);
    text[at++] = ' ';
    text[at++] = '#';
    for (int n = ordinal; n > 0 || !count; n /= 10)
        number[count++] = (char)('0' + n % 10);
    while (count)
        text[at++] = (WCHAR)number[--count];
    text[at] = 0;
    AviCopyName(out, text, 64);
}

static HRESULT parse_strl(AviFile *f, AviStream *s, UINT64 pos, UINT64 end, int ordinal)
{
    AviIo *io = f->io;
    while (pos + 8 <= end) {
        DWORD head[2];
        if (!AviIoRead(io, pos, head, 8))
            return AVIERR_FILEREAD;
        UINT64 data = pos + 8;
        UINT64 size = head[1];
        if (data + size > end)
            size = end - data;
        pos = data + size + (size & 1);
        switch (head[0]) {
        case FCC_strh: {
            AviStreamHeader strh = {0};
            DWORD count = size < sizeof(strh) ? (DWORD)size : (DWORD)sizeof(strh);
            if (!AviIoRead(io, data, &strh, count))
                return AVIERR_FILEREAD;
            AVISTREAMINFOW *info = &s->info;
            info->fccType = strh.fccType;
            info->fccHandler = strh.fccHandler;
            info->dwFlags = strh.dwFlags;
            info->dwCaps = 0;
            info->wPriority = strh.wPriority;
            info->wLanguage = strh.wLanguage;
            info->dwScale = strh.dwScale;
            info->dwRate = strh.dwRate;
            info->dwStart = strh.dwStart;
            info->dwLength = strh.dwLength;
            info->dwInitialFrames = strh.dwInitialFrames;
            info->dwSuggestedBufferSize = strh.dwSuggestedBufferSize;
            info->dwQuality = strh.dwQuality;
            info->dwSampleSize = strh.dwSampleSize;
            info->rcFrame.left = strh.rcLeft;
            info->rcFrame.top = strh.rcTop;
            info->rcFrame.right = strh.rcRight;
            info->rcFrame.bottom = strh.rcBottom;
            info->dwEditCount = 0;
            info->dwFormatChangeCount = 0;
            if (!s->named)
                default_name(f, info, ordinal, info->szName);
            break;
        }
        case FCC_strf: {
            if (s->format)
                break;
            if (size > 0x7FFFFFFF || !(s->format = AviAlloc((SIZE_T)size)))
                return AVIERR_MEMORY;
            if (!AviIoRead(io, data, s->format, (DWORD)size))
                return AVIERR_FILEREAD;
            s->format_size = (DWORD)size;
            break;
        }
        case FCC_strn: {
            char name[260] = {0};
            DWORD count = size < 259 ? (DWORD)size : 259;
            if (!AviIoRead(io, data, name, count))
                return AVIERR_FILEREAD;
            MultiByteToWideChar(CP_ACP, 0, name, -1, s->info.szName, 64);
            s->info.szName[63] = 0;
            s->named = TRUE;
            break;
        }
        case FCC_JUNK:
        case FCC_pad:
            break;
        default: {
            BYTE *copy = AviAlloc((SIZE_T)size);
            if (!copy)
                return AVIERR_MEMORY;
            HRESULT hr = AviIoRead(io, data, copy, (DWORD)size) ? AviExtraAdd(&s->extra, head[0], copy, (DWORD)size)
                                                                   : AVIERR_FILEREAD;
            AviFree(copy);
            if (FAILED(hr))
                return hr;
            break;
        }
        }
    }
    return S_OK;
}

BOOL AviBitmapHeaderValid(const BITMAPINFOHEADER *bih, DWORD size)
{
    if (size < sizeof(BITMAPINFOHEADER) || bih->biSize < sizeof(BITMAPINFOHEADER) || bih->biSize > 40 + 0xFD8)
        return FALSE;
    if (!bih->biWidth || !bih->biHeight)
        return FALSE;
    DWORD bits = bih->biBitCount ? bih->biBitCount : 32;
    if (bih->biBitCount > 200)
        return FALSE;
    if ((UINT64)(DWORD)bih->biWidth * bits > 0xFFFFFFFFull)
        return FALSE;
    UINT64 row = ((((UINT64)(DWORD)bih->biWidth * bits) >> 3) + 3) & ~3ull;
    UINT64 height = bih->biHeight < 0 ? (UINT64)-(INT64)bih->biHeight : (UINT64)bih->biHeight;
    if (row * height > 0x40000000ull || bih->biSizeImage > 0x40000000ull || bih->biClrUsed > 256)
        return FALSE;
    DWORD colors = bih->biClrUsed;
    if (!colors && bih->biBitCount >= 1 && bih->biBitCount <= 8)
        colors = 1u << bih->biBitCount;
    if (bih->biCompression == 3 && bih->biBitCount != 16 && bih->biBitCount != 32)
        return FALSE;
    DWORD masks = bih->biCompression == 3 ? 12 : 0;
    if (size < (UINT64)bih->biSize + masks + 4ull * colors)
        return FALSE;
    if ((bih->biCompression == 0 || bih->biCompression == 3) && bih->biSizeImage) {
        UINT64 stride = (((UINT64)(DWORD)bih->biWidth * bih->biBitCount + 31) >> 3) & ~3ull;
        if (stride * height > bih->biSizeImage)
            return FALSE;
    }
    return TRUE;
}

/* the format of a stream as the handler keeps it, checked and with the defaults a reader may expect. */
static HRESULT settle_stream(AviFile *f, AviStream *s)
{
    AVISTREAMINFOW *info = &s->info;
    if (!info->fccType || !s->format)
        return AVIERR_BADFORMAT;
    if (info->fccType == FCC_vids) {
        BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)s->format;
        if (!AviBitmapHeaderValid(bih, s->format_size))
            return AVIERR_FILEREAD;
        if (!bih->biClrUsed && bih->biBitCount <= 8)
            bih->biClrUsed = 1u << bih->biBitCount;
        RECT *rc = &info->rcFrame;
        if (IsRectEmpty(rc) || rc->right - rc->left > (LONG)f->header.dwWidth ||
            rc->bottom - rc->top > (LONG)f->header.dwHeight)
            SetRect(rc, 0, 0, bih->biWidth, bih->biHeight);
        if (!bih->biCompression && bih->biBitCount == 8 &&
            (info->fccHandler == FCC_RLE8 || info->fccHandler == FCC_RLE))
            bih->biCompression = BI_RLE8;
        if ((info->fccHandler == FCC_NONE || !info->fccHandler) && !bih->biCompression)
            info->fccHandler = FCC_DIB;
        s->plain_video = bih->biCompression <= BI_RLE8;
        info->dwSampleSize = 0;
    } else if (info->fccType == FCC_auds) {
        if (s->format_size < 14)
            return AVIERR_BADFORMAT;
        info->dwSampleSize = ((const WAVEFORMATEX *)s->format)->nBlockAlign;
    }
    s->first_pos = s->end_pos = (INT32)(info->dwStart - info->dwInitialFrames);
    s->started = TRUE;
    return S_OK;
}

static void add_to_streams(AviFile *f, UINT32 offset, UINT32 size, DWORD ckid, DWORD idx_flags)
{
    int number = ckid_stream(ckid);
    UINT16 type = (UINT16)(ckid >> 16);
    if (number < 0 || (DWORD)number >= f->stream_count)
        return;
    AviStream *s = f->streams[number];
    UINT16 flags;
    if (type == TYPE_PC)
        flags = CK_PALETTE;
    else if (type == TYPE_WB)
        flags = CK_ANY;
    else {
        flags = size ? ((idx_flags & AVIIF_KEYFRAME) ? CK_KEY : CK_NONKEY) : 0;
        if (s->plain_video && size && type == TYPE_DB)
            flags |= CK_KEY;
    }
    stream_add_chunk(s, offset, size, flags, type);
    if (writable(f))
        index_add(f, offset, size, flags, type, (UINT16)number, 0);
}

/* the index of a file without idx1: its chunks, in the order they are in the movi list. */
static HRESULT scan_movi(AviFile *f, UINT64 pos, UINT64 end)
{
    while (pos + 8 <= end) {
        DWORD head[2];
        if (!AviIoRead(f->io, pos, head, 8))
            break;
        UINT64 data = pos + 8;
        if (head[0] == FCC_LIST) {
            DWORD form;
            if (data + 4 > end || !AviIoRead(f->io, data, &form, 4))
                break;
            if (form == FCC_rec) {
                if (writable(f))
                    index_add(f, (UINT32)pos, head[1], 0, 0, REC_STREAM, 1);
                pos = data + 4;
                continue;
            }
        } else if (head[0] != FCC_JUNK && head[0] != FCC_pad) {
            if (data + head[1] > end)
                head[1] = (DWORD)(end - data);
            add_to_streams(f, (UINT32)pos, head[1], head[0], AVIIF_KEYFRAME);
        }
        pos = data + head[1] + (head[1] & 1);
    }
    return S_OK;
}

static HRESULT read_index(AviFile *f, UINT64 chunk, DWORD size, UINT64 movi_data, UINT64 file_size)
{
    UINT64 base = movi_data;
    BOOL first = TRUE;
    DWORD count = size / sizeof(AviIndexEntry);
    AviIndexEntry entries[512];
    for (DWORD done = 0; done < count;) {
        DWORD batch = count - done < 512 ? count - done : 512;
        if (!AviIoRead(f->io, chunk + (UINT64)done * sizeof(AviIndexEntry), entries, batch * sizeof(AviIndexEntry)))
            return AVIERR_FILEREAD;
        for (DWORD i = 0; i < batch; i++) {
            /* the first entry is the first chunk of the list, whichever way the offsets count. */
            if (first && !(f->header.dwFlags & AVIF_MUSTUSEINDEX))
                base = movi_data + 4 - (UINT64)entries[i].dwChunkOffset;
            first = FALSE;
            UINT64 at = base + entries[i].dwChunkOffset;
            if (at + 8 > file_size || at > 0xFFFFFFF0ull)
                return S_OK;
            DWORD length = entries[i].dwChunkLength;
            if (length >= 0x40000000)
                length = 0x3FFFFFFF;
            if (entries[i].ckid == FCC_rec && (entries[i].dwFlags & AVIIF_LIST)) {
                if (writable(f))
                    index_add(f, (UINT32)at, entries[i].dwChunkLength, 0, 0, REC_STREAM, 1);
                continue;
            }
            add_to_streams(f, (UINT32)at, length, entries[i].ckid, entries[i].dwFlags);
        }
        done += batch;
    }
    return S_OK;
}

static HRESULT keep_chunk(AviFile *f, DWORD ckid, UINT64 data, UINT64 size)
{
    BYTE *copy = AviAlloc((SIZE_T)size);
    if (!copy)
        return AVIERR_MEMORY;
    HRESULT hr = AviIoRead(f->io, data, copy, (DWORD)size) ? AviExtraAdd(&f->extra, ckid, copy, (DWORD)size)
                                                            : AVIERR_FILEREAD;
    AviFree(copy);
    return hr;
}

static HRESULT parse_hdrl(AviFile *f, UINT64 at, UINT64 end, BOOL *have_avih)
{
    AviIo *io = f->io;
    int number = 0;
    while (at + 8 <= end) {
        DWORD inner[2];
        if (!AviIoRead(io, at, inner, 8))
            return AVIERR_FILEREAD;
        UINT64 inner_data = at + 8;
        UINT64 inner_size = inner[1];
        if (inner_data + inner_size > end)
            inner_size = end - inner_data;
        if (inner[0] == FCC_avih) {
            DWORD count = inner_size < sizeof(f->header) ? (DWORD)inner_size : (DWORD)sizeof(f->header);
            memset(&f->header, 0, sizeof(f->header));
            if (!AviIoRead(io, inner_data, &f->header, count))
                return AVIERR_FILEREAD;
            *have_avih = TRUE;
        } else if (inner[0] == FCC_LIST) {
            DWORD inner_form = 0;
            if (inner_size >= 4 && !AviIoRead(io, inner_data, &inner_form, 4))
                return AVIERR_FILEREAD;
            if (inner_form == FCC_strl) {
                if (number >= MAX_STREAMS)
                    return AVIERR_ERROR;
                AviStream *s = stream_new(f, number);
                if (!s)
                    return AVIERR_MEMORY;
                f->streams[f->stream_count++] = s;
                HRESULT hr = parse_strl(f, s, inner_data + 4, inner_data + inner_size, 1);
                if (FAILED(hr))
                    return hr;
                /* the name of a stream counts the streams of its type before it. */
                int same = 1;
                for (int i = 0; i < number; i++)
                    if (f->streams[i]->info.fccType == s->info.fccType)
                        same++;
                if (!s->named)
                    default_name(f, &s->info, same, s->info.szName);
                number++;
            }
        }
        at = inner_data + inner_size + (inner_size & 1);
    }
    return S_OK;
}

static HRESULT open_internal(AviFile *f, BOOL parse_only)
{
    AviIo *io = f->io;
    if (!io)
        return AVIERR_FILEOPEN;
    if (f->mode & OF_CREATE) {
        f->header_size = 100;
        f->header.dwFlags = AVIF_HASINDEX | AVIF_TRUSTCKTYPE;
        return S_OK;
    }
    UINT64 file_size = AviIoSize(io);
    DWORD riff[3];
    if (!AviIoRead(io, 0, riff, 12))
        return AVIERR_FILEREAD;
    if (riff[0] != FCC_RIFF || riff[2] != FCC_AVI)
        return AVIERR_BADFORMAT;
    UINT64 riff_end = 8ull + riff[1];
    if (riff_end > file_size || riff_end < 12)
        riff_end = file_size;
    UINT64 pos = 12, movi_data = 0, movi_end = 0, idx_data = 0;
    DWORD idx_size = 0;
    BOOL have_header_list = FALSE, have_avih = FALSE;
    while (pos + 8 <= riff_end) {
        DWORD head[2];
        HRESULT hr = S_OK;
        if (!AviIoRead(io, pos, head, 8))
            return AVIERR_FILEREAD;
        UINT64 data = pos + 8;
        UINT64 size = head[1];
        if (data + size > riff_end)
            size = riff_end - data;
        if (head[0] == FCC_LIST) {
            DWORD form = 0;
            if (size < 4 || !AviIoRead(io, data, &form, 4))
                return AVIERR_FILEREAD;
            if (form == FCC_hdrl && !have_header_list) {
                have_header_list = TRUE;
                f->header_size = (DWORD)size + 32;
                hr = parse_hdrl(f, data + 4, data + size, &have_avih);
            } else if (form == FCC_movi && !movi_data) {
                f->movi_off = (UINT32)pos;
                movi_data = data;
                movi_end = data + size;
            } else {
                hr = keep_chunk(f, FCC_LIST, data, size);
            }
        } else if (head[0] == FCC_idx1) {
            if (!idx_data) {
                idx_data = data;
                idx_size = (DWORD)size;
            }
        } else if (head[0] != FCC_JUNK && head[0] != FCC_pad) {
            hr = keep_chunk(f, head[0], data, size);
        }
        if (FAILED(hr))
            return hr;
        pos = data + size + (size & 1);
    }
    if (!have_header_list || !have_avih || !movi_data)
        return AVIERR_ERROR;
    for (DWORD i = 0; i < f->stream_count; i++) {
        HRESULT hr = settle_stream(f, f->streams[i]);
        if (FAILED(hr))
            return hr;
    }
    f->header.dwStreams = f->stream_count;
    f->end_off = (UINT32)movi_end;
    if (idx_data && idx_size) {
        HRESULT hr = read_index(f, idx_data, idx_size, movi_data, file_size);
        if (FAILED(hr))
            return hr;
    } else {
        if (parse_only)
            return AVIERR_ERROR;
        HRESULT hr = scan_movi(f, movi_data + 4, movi_end);
        if (FAILED(hr))
            return hr;
    }
    for (DWORD i = 0; i < f->stream_count; i++) {
        AviStream *s = f->streams[i];
        s->info.dwSuggestedBufferSize = s->max_chunk;
        if (s->change_count)
            s->info.dwFlags |= AVISTREAMINFO_FORMATCHANGES;
        else
            s->info.dwFlags &= ~(DWORD)AVISTREAMINFO_FORMATCHANGES;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ interleaved records */

/* closes the list of the record that is open and opens the next one. */
static HRESULT end_record(AviFile *f)
{
    if (!f->end_off) {
        f->end_off = ((f->header_size + 3071) & ~(PAD_UNIT - 1u)) - 12;
        f->movi_off = f->end_off - 12;
    }
    if (f->rec_open) {
        /* the next list starts 12 bytes before a multiple of 2048. */
        UINT32 phase = (f->end_off + 12) & (PAD_UNIT - 1);
        if (phase) {
            DWORD fill = phase <= PAD_UNIT - 8 ? PAD_UNIT - 8 - phase : 2 * PAD_UNIT - 8 - phase;
            DWORD head[2] = {FCC_JUNK, fill};
            if (!AviIoWrite(f->io, f->end_off, head, 8) || !AviIoZero(f->io, f->end_off + 8ull, fill))
                return AVIERR_FILEWRITE;
            f->end_off += 8 + fill;
        }
        DWORD size = f->end_off - f->rec_start - 8;
        if (!AviIoWrite(f->io, (UINT64)f->rec_start + 4, &size, 4))
            return AVIERR_FILEWRITE;
        f->index[f->rec_index].size = size;
        if (size + 12 > f->header.dwSuggestedBufferSize)
            f->header.dwSuggestedBufferSize = size + 12;
    }
    DWORD list[3] = {FCC_LIST, 0, FCC_rec};
    if (!AviIoWrite(f->io, f->end_off, list, 12))
        return AVIERR_FILEWRITE;
    f->rec_open = TRUE;
    f->rec_start = f->end_off;
    f->rec_index = f->index_count;
    f->end_off += 12;
    return index_add(f, f->rec_start, 0, 0, 0, REC_STREAM, 1);
}

static HRESULT file_end_record(AviFile *f)
{
    if (!writable(f))
        return AVIERR_READONLY;
    f->header.dwFlags |= AVIF_ISINTERLEAVED;
    f->dirty = TRUE;
    return end_record(f);
}

/* ------------------------------------------------------------------ the file object */

static HRESULT STDMETHODCALLTYPE unknown_qi(IUnknown *This, REFIID riid, void **object)
{
    AviFile *f = FILE_FROM(This, lpUnknown);
    if (IsEqualGUID(riid, &IID_IUnknown))
        *object = &f->lpUnknown;
    else if (IsEqualGUID(riid, &IID_IAVIFile))
        *object = &f->lpFile;
    else if (IsEqualGUID(riid, &IID_IAVIPersistFile_) || IsEqualGUID(riid, &IID_IPersistFile_))
        *object = &f->lpPersist;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE unknown_addref(IUnknown *This)
{
    AviFile *f = FILE_FROM(This, lpUnknown);
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&f->refs);
}

static void file_destroy(AviFile *f)
{
    for (DWORD i = 0; i < f->stream_count; i++)
        stream_free(f->streams[i]);
    AviIoClose(f->io);
    AviFree(f->index);
    AviExtraFree(&f->extra);
    DeleteCriticalSection(&f->lock);
    AviFree(f);
}

static ULONG STDMETHODCALLTYPE unknown_release(IUnknown *This)
{
    AviFile *f = FILE_FROM(This, lpUnknown);
    EnterCriticalSection(&f->lock);
    LONG refs = InterlockedDecrement(&f->refs);
    AviUseAdd(-1);
    if (!refs) {
        /* the header is written while the object still has a reference of its own. */
        if (f->dirty && writable(f) && f->io) {
            f->refs = 1;
            save_changes(f);
            f->refs = 0;
        }
        LeaveCriticalSection(&f->lock);
        file_destroy(f);
        return 0;
    }
    LeaveCriticalSection(&f->lock);
    return (ULONG)refs;
}

static const IUnknownVtbl g_unknown_vtbl = {unknown_qi, unknown_addref, unknown_release};

static HRESULT STDMETHODCALLTYPE file_qi(IAVIFile *This, REFIID riid, void **object)
{
    IUnknown *outer = FILE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE file_addref(IAVIFile *This)
{
    IUnknown *outer = FILE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE file_release(IAVIFile *This)
{
    IUnknown *outer = FILE_FROM(This, lpFile)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE file_info(IAVIFile *This, AVIFILEINFOW *info, LONG size)
{
    AviFile *f = FILE_FROM(This, lpFile);
    if (!info)
        return AVIERR_BADPARAM;
    if ((ULONG)size < AVIFILEINFOW_SIZE)
        return AVIERR_BUFFERTOOSMALL;
    EnterCriticalSection(&f->lock);
    info->dwMaxBytesPerSec = f->header.dwMaxBytesPerSec;
    info->dwCaps = AVIFILECAPS_CANREAD | AVIFILECAPS_CANWRITE;
    info->dwFlags = f->header.dwFlags & AVIF_ISINTERLEAVED;
    info->dwStreams = f->stream_count;
    info->dwSuggestedBufferSize = f->header.dwSuggestedBufferSize;
    info->dwWidth = f->header.dwWidth;
    info->dwHeight = f->header.dwHeight;
    info->dwScale = f->header.dwMicroSecPerFrame;
    info->dwRate = 1000000;
    info->dwLength = f->header.dwTotalFrames;
    AviCopyName(info->szFileType, AVI_STRING_AVI_FILE, 64);
    LeaveCriticalSection(&f->lock);
    return S_OK;
}

static HRESULT find_stream(AviFile *f, DWORD type, LONG index, AviStream **found)
{
    if (index < 0 || index >= (LONG)f->stream_count)
        return AVIERR_NODATA;
    for (DWORD i = 0; i < f->stream_count; i++) {
        if (type && f->streams[i]->info.fccType != type)
            continue;
        if (!index--) {
            *found = f->streams[i];
            return S_OK;
        }
    }
    return AVIERR_NODATA;
}

static HRESULT STDMETHODCALLTYPE file_getstream(IAVIFile *This, PAVISTREAM *stream, DWORD type, LONG index)
{
    AviFile *f = FILE_FROM(This, lpFile);
    AviStream *s;
    *stream = nullptr;
    EnterCriticalSection(&f->lock);
    HRESULT hr = find_stream(f, type, index, &s);
    if (SUCCEEDED(hr)) {
        *stream = (PAVISTREAM)&s->lpVtbl;
        stream_addref(*stream);
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_createstream(IAVIFile *This, PAVISTREAM *stream, AVISTREAMINFOW *info)
{
    AviFile *f = FILE_FROM(This, lpFile);
    if (!stream || !info)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&f->lock);
    HRESULT hr = S_OK;
    DWORD name = ansi_length(info->szName);
    if (!writable(f)) {
        hr = AVIERR_READONLY;
    } else if ((f->end_off && (UINT64)name + f->header_size + 96 > f->movi_off) || f->stream_count >= MAX_STREAMS) {
        hr = AVIERR_UNSUPPORTED;
    } else {
        AviStream *s = stream_new(f, (int)f->stream_count);
        if (!s) {
            hr = AVIERR_MEMORY;
        } else {
            f->header_size += name + 96;
            s->info = *info;
            s->info.dwLength = 0;
            s->info.dwSuggestedBufferSize = 0;
            if (s->info.fccType == FCC_vids)
                SetRectEmpty(&s->info.rcFrame);
            f->streams[f->stream_count++] = s;
            if (s->number == 0) {
                int micro = MulDiv(1000000, (int)s->info.dwScale, (int)s->info.dwRate);
                f->header.dwMicroSecPerFrame = micro >= 1000 ? (DWORD)micro : 1000;
            }
            if (s->info.rcFrame.right > (LONG)f->header.dwWidth)
                f->header.dwWidth = (DWORD)s->info.rcFrame.right;
            if (s->info.rcFrame.bottom > (LONG)f->header.dwHeight)
                f->header.dwHeight = (DWORD)s->info.rcFrame.bottom;
            if (s->info.dwInitialFrames > f->header.dwInitialFrames)
                f->header.dwInitialFrames = s->info.dwInitialFrames;
            f->header.dwStreams = f->stream_count;
            f->dirty = TRUE;
            *stream = (PAVISTREAM)&s->lpVtbl;
            stream_addref(*stream);
        }
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_writedata(IAVIFile *This, DWORD ckid, void *data, LONG size)
{
    AviFile *f = FILE_FROM(This, lpFile);
    HRESULT hr;
    if (!data || !size)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&f->lock);
    if (!writable(f))
        hr = AVIERR_READONLY;
    else if (size < 0)
        hr = AVIERR_BADPARAM;
    else if (!header_fits(f, (DWORD)size + 12))
        hr = AVIERR_UNSUPPORTED;
    else {
        f->dirty = TRUE;
        hr = AviExtraAdd(&f->extra, ckid, data, (DWORD)size);
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_readdata(IAVIFile *This, DWORD ckid, void *data, LONG *size)
{
    AviFile *f = FILE_FROM(This, lpFile);
    if (!size)
        return AVIERR_BADPARAM;
    EnterCriticalSection(&f->lock);
    HRESULT hr = AviExtraRead(&f->extra, ckid, data, size);
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_endrecord(IAVIFile *This)
{
    AviFile *f = FILE_FROM(This, lpFile);
    EnterCriticalSection(&f->lock);
    HRESULT hr = file_end_record(f);
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_deletestream(IAVIFile *This, DWORD type, LONG index)
{
    AviFile *f = FILE_FROM(This, lpFile);
    AviStream *s;
    EnterCriticalSection(&f->lock);
    HRESULT hr = find_stream(f, type, index, &s);
    LeaveCriticalSection(&f->lock);
    return FAILED(hr) ? hr : AVIERR_UNSUPPORTED;
}

static const IAVIFileVtbl g_file_vtbl = {file_qi,         file_addref,   file_release,  file_info,
                                         file_getstream,  file_createstream, file_writedata, file_readdata,
                                         file_endrecord,  file_deletestream};

/* ------------------------------------------------------------------ IPersistFile */

static HRESULT STDMETHODCALLTYPE persist_qi(IAVIPersistFile *This, REFIID riid, void **object)
{
    IUnknown *outer = FILE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->QueryInterface(outer, riid, object);
}

static ULONG STDMETHODCALLTYPE persist_addref(IAVIPersistFile *This)
{
    IUnknown *outer = FILE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE persist_release(IAVIPersistFile *This)
{
    IUnknown *outer = FILE_FROM(This, lpPersist)->outer;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE persist_classid(IAVIPersistFile *This, CLSID *clsid)
{
    if (clsid)
        *clsid = CLSID_AVIFile_;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_isdirty(IAVIPersistFile *This)
{
    return FILE_FROM(This, lpPersist)->dirty ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE persist_load(IAVIPersistFile *This, LPCOLESTR name, DWORD mode)
{
    AviFile *f = FILE_FROM(This, lpPersist);
    HRESULT hr;
    EnterCriticalSection(&f->lock);
    if (f->path[0]) {
        LeaveCriticalSection(&f->lock);
        return (HRESULT)-1;
    }
    BOOL parse = (mode & OF_PARSE) != 0;
    f->mode = mode & ~(DWORD)OF_PARSE;
    if (lstrlenW(name) >= MAX_PATH) {
        LeaveCriticalSection(&f->lock);
        return E_INVALIDARG;
    }
    AviCopyName(f->path, name, MAX_PATH);
    UINT old = SetErrorMode(0x8000);
    f->io = AviIoOpen(f->path, f->mode);
    if (!f->io && !(f->mode & 3)) {
        f->mode &= ~0x70u;
        f->io = AviIoOpen(f->path, f->mode);
    }
    SetErrorMode(old);
    hr = open_internal(f, parse);
    LeaveCriticalSection(&f->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE persist_save(IAVIPersistFile *This, LPCOLESTR name, BOOL remember)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_savecompleted(IAVIPersistFile *This, LPCOLESTR name)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_getcurfile(IAVIPersistFile *This, LPOLESTR *name)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE persist_reserved(IAVIPersistFile *This)
{
    return S_OK;
}

static const IAVIPersistFileVtbl g_persist_vtbl = {persist_qi,        persist_addref,       persist_release,
                                                   persist_classid,   persist_isdirty,      persist_load,
                                                   persist_save,      persist_savecompleted, persist_getcurfile,
                                                   persist_reserved};

HRESULT AviFileCreate(IUnknown *outer, REFIID riid, void **object)
{
    AviFile *f = AviAlloc(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpUnknown = &g_unknown_vtbl;
    f->lpFile = &g_file_vtbl;
    f->lpPersist = &g_persist_vtbl;
    f->outer = outer ? outer : (IUnknown *)&f->lpUnknown;
    InitializeCriticalSection(&f->lock);
    HRESULT hr = unknown_qi((IUnknown *)&f->lpUnknown, riid, object);
    if (FAILED(hr)) {
        DeleteCriticalSection(&f->lock);
        AviFree(f);
    }
    return hr;
}
