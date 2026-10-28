/*
 * editable streams: a stream that is a list of pieces of other streams. Cutting, copying and pasting move pieces,
 * never data, so the streams that hold the data are only read. A video whose key frames are not where the pieces
 * start is read as bitmaps through IGetFrame, which then are the frames of the editable stream.
 */
#include "avifil32_private.h"

typedef struct Edit {
    PAVISTREAM stream;
    LONG start;
    LONG length;
} Edit;

typedef struct EditStream {
    const IAVIStreamVtbl *lpStream;
    const IAVIEditStreamVtbl *lpEdit;
    LONG refs;
    Edit *edits;
    int count, capacity;
    AVISTREAMINFOW info;
    BOOL frames;
    PGETFRAME reader;
    PAVISTREAM reader_stream;
} EditStream;

#define EDIT_FROM_STREAM(p) ((EditStream *)(p))
#define EDIT_FROM_EDIT(p) ((EditStream *)((BYTE *)(p) - offsetof(EditStream, lpEdit)))

static const IAVIStreamVtbl g_edit_stream_vtbl;

static EditStream *edit_of(PAVISTREAM stream)
{
    return stream && stream->lpVtbl == &g_edit_stream_vtbl ? EDIT_FROM_STREAM(stream) : nullptr;
}

/* ------------------------------------------------------------------ the list of pieces */

static HRESULT edits_room(EditStream *s, int extra)
{
    if (s->count + extra <= s->capacity)
        return S_OK;
    int wanted = s->capacity + (extra > 16 ? extra : 16);
    Edit *grown = AviRealloc(s->edits, sizeof(Edit) * (SIZE_T)wanted);
    if (!grown)
        return AVIERR_MEMORY;
    s->edits = grown;
    s->capacity = wanted;
    return S_OK;
}

static void edits_remove(EditStream *s, int at)
{
    if (s->edits[at].stream)
        s->edits[at].stream->lpVtbl->Release(s->edits[at].stream);
    memmove(s->edits + at, s->edits + at + 1, sizeof(Edit) * (SIZE_T)(s->count - at - 1));
    s->count--;
}

static void edits_prune(EditStream *s)
{
    for (int i = s->count - 1; i >= 0; i--)
        if (s->edits[i].length <= 0)
            edits_remove(s, i);
}

/* the piece that holds a position, and where in its stream the position is. */
static BOOL edits_locate(const EditStream *s, LONG pos, int *index, LONG *source_pos)
{
    LONG at = pos - (LONG)s->info.dwStart;
    if (at < 0)
        return FALSE;
    for (int i = 0; i < s->count; i++) {
        if (at < s->edits[i].length) {
            *index = i;
            *source_pos = s->edits[i].start + at;
            return TRUE;
        }
        at -= s->edits[i].length;
    }
    return FALSE;
}

/* makes a boundary of two pieces at a position, which has to be in the stream or at its end. */
static HRESULT edits_split(EditStream *s, LONG pos, int *boundary)
{
    LONG at = pos - (LONG)s->info.dwStart;
    for (int i = 0; i < s->count; i++) {
        if (at == 0) {
            *boundary = i;
            return S_OK;
        }
        if (at < s->edits[i].length) {
            HRESULT hr = edits_room(s, 1);
            if (FAILED(hr))
                return hr;
            memmove(s->edits + i + 1, s->edits + i, sizeof(Edit) * (SIZE_T)(s->count - i));
            s->count++;
            s->edits[i + 1].stream->lpVtbl->AddRef(s->edits[i + 1].stream);
            s->edits[i + 1].start += at;
            s->edits[i + 1].length -= at;
            s->edits[i].length = at;
            *boundary = i + 1;
            return S_OK;
        }
        at -= s->edits[i].length;
    }
    if (at != 0)
        return AVIERR_BADPARAM;
    *boundary = s->count;
    return S_OK;
}

static LONG edit_end(const EditStream *s)
{
    return (LONG)(s->info.dwStart + s->info.dwLength);
}

/* ------------------------------------------------------------------ frames, when the pieces are not for splicing */

static BITMAPINFOHEADER *edit_frame(EditStream *s, PAVISTREAM stream, LONG pos)
{
    if (s->reader_stream != stream) {
        PGETFRAME reader = AVIStreamGetFrameOpen(stream, nullptr);
        if (!reader)
            return nullptr;
        if (s->reader)
            s->reader->lpVtbl->Release(s->reader);
        s->reader = reader;
        s->reader_stream = stream;
    }
    return s->reader ? AVIStreamGetFrame(s->reader, pos) : nullptr;
}

/* the two streams hold the same kind of video, so that frames can follow one another in a list. */
static BOOL video_compatible(PAVISTREAM a, PAVISTREAM b)
{
    BITMAPINFOHEADER x = {0}, y = {0};
    LONG size_a = 0, size_b = 0;
    if (FAILED(a->lpVtbl->ReadFormat(a, AVIStreamStart(a), nullptr, &size_a)) ||
        FAILED(b->lpVtbl->ReadFormat(b, AVIStreamStart(b), nullptr, &size_b)) || size_a != size_b)
        return FALSE;
    size_a = size_b = sizeof(x);
    if (FAILED(a->lpVtbl->ReadFormat(a, AVIStreamStart(a), &x, &size_a)) ||
        FAILED(b->lpVtbl->ReadFormat(b, AVIStreamStart(b), &y, &size_b)))
        return FALSE;
    return x.biSize == y.biSize && x.biWidth == y.biWidth && x.biHeight == y.biHeight && x.biPlanes == y.biPlanes &&
           x.biBitCount == y.biBitCount && x.biCompression == y.biCompression;
}

static BOOL audio_compatible(PAVISTREAM a, PAVISTREAM b)
{
    LONG size_a = 0, size_b = 0;
    BOOL same = FALSE;
    if (FAILED(a->lpVtbl->ReadFormat(a, AVIStreamStart(a), nullptr, &size_a)) ||
        FAILED(b->lpVtbl->ReadFormat(b, AVIStreamStart(b), nullptr, &size_b)) || size_a != size_b)
        return FALSE;
    BYTE *formats = AviAlloc((SIZE_T)size_a * 2 + 2);
    if (!formats)
        return FALSE;
    if (SUCCEEDED(a->lpVtbl->ReadFormat(a, AVIStreamStart(a), formats, &size_a)) &&
        SUCCEEDED(b->lpVtbl->ReadFormat(b, AVIStreamStart(b), formats + size_a, &size_b)))
        same = !memcmp(formats, formats + size_a, (SIZE_T)size_a);
    AviFree(formats);
    return same;
}

/* from now on the video is read as bitmaps. */
static HRESULT edit_use_frames(EditStream *s)
{
    if (s->frames)
        return S_OK;
    for (int i = 0; i < s->count; i++) {
        PGETFRAME reader = AVIStreamGetFrameOpen(s->edits[i].stream, nullptr);
        if (!reader)
            return AVIERR_BADFORMAT;
        reader->lpVtbl->Release(reader);
    }
    s->frames = TRUE;
    s->info.fccHandler = 0;
    s->info.dwFlags |= AVISTREAMINFO_FORMATCHANGES;
    s->info.dwFormatChangeCount++;
    return S_OK;
}

static BOOL is_key(PAVISTREAM stream, LONG pos)
{
    return stream->lpVtbl->FindSample(stream, pos, FIND_KEY | FIND_PREV) == pos;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE edit_qi(IAVIStream *This, REFIID riid, void **object)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAVIStream))
        *object = &s->lpStream;
    else if (IsEqualGUID(riid, &IID_IAVIEditStream))
        *object = &s->lpEdit;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE edit_addref(IAVIStream *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&EDIT_FROM_STREAM(This)->refs);
}

static ULONG STDMETHODCALLTYPE edit_release(IAVIStream *This)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    LONG refs = InterlockedDecrement(&s->refs);
    AviUseAdd(-1);
    if (!refs) {
        if (s->reader)
            s->reader->lpVtbl->Release(s->reader);
        for (int i = 0; i < s->count; i++)
            if (s->edits[i].stream)
                s->edits[i].stream->lpVtbl->Release(s->edits[i].stream);
        AviFree(s->edits);
        AviFree(s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE editi_qi(IAVIEditStream *This, REFIID riid, void **object)
{
    return edit_qi((IAVIStream *)EDIT_FROM_EDIT(This), riid, object);
}

static ULONG STDMETHODCALLTYPE editi_addref(IAVIEditStream *This)
{
    return edit_addref((IAVIStream *)EDIT_FROM_EDIT(This));
}

static ULONG STDMETHODCALLTYPE editi_release(IAVIEditStream *This)
{
    return edit_release((IAVIStream *)EDIT_FROM_EDIT(This));
}

/* ------------------------------------------------------------------ IAVIStream */

static HRESULT STDMETHODCALLTYPE edit_create(IAVIStream *This, LPARAM source, LPARAM unused)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    PAVISTREAM stream = (PAVISTREAM)source;
    s->count = 0;
    HRESULT hr = edits_room(s, 16);
    if (FAILED(hr))
        return hr;
    if (stream) {
        stream->lpVtbl->AddRef(stream);
        s->edits[0].stream = stream;
        s->edits[0].start = AVIStreamStart(stream);
        s->edits[0].length = AVIStreamLength(stream);
        s->count = 1;
        AVIStreamInfoW(stream, &s->info, AVISTREAMINFOW_SIZE);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE edit_info(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    if (info)
        memcpy(info, &EDIT_FROM_STREAM(This)->info, (ULONG)size < AVISTREAMINFOW_SIZE ? (SIZE_T)size : AVISTREAMINFOW_SIZE);
    return S_OK;
}

static LONG STDMETHODCALLTYPE edit_findsample(IAVIStream *This, LONG pos, LONG flags)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    int index;
    LONG source_pos;
    if (pos || !(flags & FIND_FROM_START)) {
        if (pos < (LONG)s->info.dwStart || pos >= edit_end(s))
            return -1;
    } else {
        pos = (LONG)s->info.dwStart;
    }
    if (!edits_locate(s, pos, &index, &source_pos))
        return -1;
    if (flags & FIND_FORMAT)
        return (flags & FIND_PREV) ? 0 : -1;
    if (s->frames)
        return pos;
    LONG found = AVIStreamFindSample(s->edits[index].stream, source_pos, flags);
    if (found < 0)
        return -1;
    /* a key frame in front of the piece is no key frame of this stream. */
    if (found < s->edits[index].start && (flags & FIND_RET) == FIND_POS)
        return pos - (source_pos - s->edits[index].start);
    return pos - source_pos + found;
}

static HRESULT STDMETHODCALLTYPE edit_readformat(IAVIStream *This, LONG pos, void *format, LONG *size)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    int index;
    LONG source_pos;
    if (!size || pos < (LONG)s->info.dwStart || pos >= edit_end(s))
        return AVIERR_BADPARAM;
    if (!edits_locate(s, pos, &index, &source_pos))
        return AVIERR_BADPARAM;
    if (s->frames) {
        BITMAPINFOHEADER *bih = edit_frame(s, s->edits[index].stream, source_pos);
        if (!bih)
            return E_FAIL;
        LONG needed = (LONG)(bih->biSize + 4 * bih->biClrUsed);
        if (format) {
            LONG count = *size < needed ? *size : needed;
            if (count > 0)
                memcpy(format, bih, (SIZE_T)count);
        }
        *size = needed;
        return S_OK;
    }
    PAVISTREAM stream = s->edits[index].stream;
    return stream->lpVtbl->ReadFormat(stream, source_pos, format, size);
}

static HRESULT STDMETHODCALLTYPE edit_unsupported_format(IAVIStream *This, LONG pos, void *format, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE edit_read(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                           LONG *bytes, LONG *samples_read)
{
    EditStream *s = EDIT_FROM_STREAM(This);
    LONG total_bytes = 0, total_samples = 0;
    if (bytes)
        *bytes = 0;
    if (samples_read)
        *samples_read = 0;
    if (start < (LONG)s->info.dwStart || start >= edit_end(s))
        return AVIERR_BADPARAM;
    if (!samples)
        return S_OK;
    if (samples < 0)
        samples = 0x7FFFFFFF;
    for (;;) {
        int index;
        LONG source_pos;
        if (!edits_locate(s, start, &index, &source_pos))
            return E_FAIL;
        const Edit *e = &s->edits[index];
        LONG left = e->start + e->length - source_pos;
        LONG count = samples < 0 || samples > left ? left : samples;
        LONG got_bytes = 0, got_samples = 0;
        HRESULT hr;
        if (s->frames) {
            BITMAPINFOHEADER *bih = edit_frame(s, e->stream, source_pos);
            if (!bih)
                return E_FAIL;
            if (buffer && size < (LONG)bih->biSizeImage) {
                if (bytes)
                    *bytes = 0;
                if (samples_read)
                    *samples_read = 0;
                return AVIERR_BUFFERTOOSMALL;
            }
            if (buffer)
                memcpy(buffer, (BYTE *)bih + bih->biSize + 4 * bih->biClrUsed, bih->biSizeImage);
            if (bytes)
                *bytes = (LONG)bih->biSizeImage;
            if (samples_read)
                *samples_read = 1;
            return S_OK;
        }
        hr = e->stream->lpVtbl->Read(e->stream, source_pos, count, buffer, size, &got_bytes, &got_samples);
        if (hr != S_OK)
            return hr;
        total_bytes += got_bytes;
        total_samples += got_samples;
        if (bytes)
            *bytes = total_bytes;
        if (samples_read)
            *samples_read = total_samples;
        if (buffer) {
            buffer = (BYTE *)buffer + got_bytes;
            size -= got_bytes;
        }
        start += count;
        samples -= count;
        if (start >= edit_end(s) || !samples || !got_samples)
            return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE edit_write(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                            DWORD flags, LONG *a, LONG *b)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE edit_delete(IAVIStream *This, LONG start, LONG samples)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE edit_readdata(IAVIStream *This, DWORD fcc, void *data, LONG *size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE edit_writedata(IAVIStream *This, DWORD fcc, void *data, LONG size)
{
    return AVIERR_UNSUPPORTED;
}

static HRESULT edit_set_info(EditStream *s, AVISTREAMINFOW *info, LONG size)
{
    if ((ULONG)size < AVISTREAMINFOW_SIZE || !info)
        return AVIERR_BADPARAM;
    s->info.wPriority = info->wPriority;
    s->info.wLanguage = info->wLanguage;
    s->info.dwScale = info->dwScale;
    s->info.dwRate = info->dwRate;
    s->info.dwStart = info->dwStart;
    s->info.dwQuality = info->dwQuality;
    s->info.rcFrame = info->rcFrame;
    if (info->szName[0])
        memcpy(s->info.szName, info->szName, sizeof(s->info.szName));
    s->info.dwEditCount++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE edit_setinfo(IAVIStream *This, AVISTREAMINFOW *info, LONG size)
{
    return edit_set_info(EDIT_FROM_STREAM(This), info, size);
}

static const IAVIStreamVtbl g_edit_stream_vtbl = {
    edit_qi,         edit_addref,    edit_release,   edit_create,           edit_info,       edit_findsample,
    edit_readformat, edit_unsupported_format, edit_read, edit_write,        edit_delete,     edit_readdata,
    edit_writedata,  edit_setinfo};

/* ------------------------------------------------------------------ IAVIEditStream */

static EditStream *edit_new(PAVISTREAM source)
{
    EditStream *s = AviAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    s->lpStream = &g_edit_stream_vtbl;
    s->lpEdit = nullptr;
    s->refs = 1;
    AviUseAdd(1);
    if (FAILED(edit_create((IAVIStream *)s, (LPARAM)source, 0))) {
        edit_release((IAVIStream *)s);
        return nullptr;
    }
    return s;
}

static HRESULT STDMETHODCALLTYPE editi_clone(IAVIEditStream *This, PAVISTREAM *result);
static HRESULT STDMETHODCALLTYPE editi_cut(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result);
static HRESULT STDMETHODCALLTYPE editi_copy(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result);
static HRESULT STDMETHODCALLTYPE editi_paste(IAVIEditStream *This, LONG *pos, LONG *length, PAVISTREAM source,
                                             LONG src_start, LONG src_length);
static HRESULT STDMETHODCALLTYPE editi_setinfo(IAVIEditStream *This, AVISTREAMINFOW *info, LONG size)
{
    return edit_set_info(EDIT_FROM_EDIT(This), info, size);
}

static const IAVIEditStreamVtbl g_edit_vtbl = {editi_qi,   editi_addref, editi_release, editi_cut,
                                               editi_copy, editi_paste,  editi_clone,   editi_setinfo};

static EditStream *edit_make(PAVISTREAM source)
{
    EditStream *s = edit_new(source);
    if (s)
        s->lpEdit = &g_edit_vtbl;
    return s;
}

static HRESULT STDMETHODCALLTYPE editi_clone(IAVIEditStream *This, PAVISTREAM *result)
{
    EditStream *s = EDIT_FROM_EDIT(This);
    EditStream *copy = edit_make(nullptr);
    if (!copy) {
        *result = nullptr;
        return AVIERR_MEMORY;
    }
    HRESULT hr = edits_room(copy, s->count);
    if (FAILED(hr)) {
        edit_release((IAVIStream *)copy);
        *result = nullptr;
        return hr;
    }
    for (int i = 0; i < s->count; i++) {
        copy->edits[i] = s->edits[i];
        s->edits[i].stream->lpVtbl->AddRef(s->edits[i].stream);
    }
    copy->count = s->count;
    copy->info = s->info;
    copy->frames = s->frames;
    *result = (PAVISTREAM)&copy->lpStream;
    return S_OK;
}

/* the range of a copy or a cut, taken in from the arguments. */
static HRESULT edit_range(EditStream *s, LONG *start, LONG *length)
{
    LONG total = (LONG)s->info.dwLength;
    if (!start || !length)
        return AVIERR_BADPARAM;
    LONG begin = *start - (LONG)s->info.dwStart;
    if (begin < 0 || begin >= total + 1)
        return AVIERR_BADPARAM;
    if (*length < 0 || begin + *length > total) {
        if (begin >= total)
            return AVIERR_BADPARAM;
        *length = total - begin;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE editi_copy(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result)
{
    EditStream *s = EDIT_FROM_EDIT(This);
    if (result)
        *result = nullptr;
    if (!start || !length)
        return AVIERR_BADPARAM;
    LONG begin = *start - (LONG)s->info.dwStart;
    LONG total = (LONG)s->info.dwLength;
    if (begin < 0 || begin > total)
        return AVIERR_BADPARAM;
    if (*length < 0)
        *length = total - begin;
    if (begin + *length > total)
        *length = total - begin;
    if (!result)
        return S_OK;
    EditStream *copy = edit_make(nullptr);
    if (!copy)
        return AVIERR_MEMORY;
    copy->info = s->info;
    copy->frames = s->frames;
    copy->info.dwStart = 0;
    copy->info.dwLength = (DWORD)*length;
    LONG skip = begin, want = *length;
    for (int i = 0; i < s->count && want > 0; i++) {
        const Edit *e = &s->edits[i];
        if (skip >= e->length) {
            skip -= e->length;
            continue;
        }
        LONG take = e->length - skip < want ? e->length - skip : want;
        HRESULT hr = edits_room(copy, 1);
        if (FAILED(hr)) {
            edit_release((IAVIStream *)copy);
            return hr;
        }
        copy->edits[copy->count].stream = e->stream;
        copy->edits[copy->count].start = e->start + skip;
        copy->edits[copy->count].length = take;
        e->stream->lpVtbl->AddRef(e->stream);
        copy->count++;
        want -= take;
        skip = 0;
    }
    /* a piece of video that starts between key frames can only be read as bitmaps. */
    if (copy->info.fccType == FCC_vids && !copy->frames && copy->count &&
        !is_key(copy->edits[0].stream, copy->edits[0].start))
        edit_use_frames(copy);
    *result = (PAVISTREAM)&copy->lpStream;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE editi_cut(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result)
{
    EditStream *s = EDIT_FROM_EDIT(This);
    if (result)
        *result = nullptr;
    HRESULT hr = edit_range(s, start, length);
    if (FAILED(hr))
        return hr;
    if (result) {
        hr = editi_copy(This, start, length, result);
        if (FAILED(hr))
            return hr;
    }
    LONG end = *start + *length;
    /* what follows the cut has to be a place to start from, else the video becomes bitmaps. */
    if (s->info.fccType == FCC_vids && !s->frames && end < edit_end(s)) {
        int index;
        LONG source_pos;
        if (edits_locate(s, end, &index, &source_pos) && !is_key(s->edits[index].stream, source_pos)) {
            hr = edit_use_frames(s);
            if (FAILED(hr)) {
                if (result)
                    (*result)->lpVtbl->Release(*result);
                if (result)
                    *result = nullptr;
                return hr;
            }
        }
    }
    int first, last;
    hr = edits_split(s, *start, &first);
    if (SUCCEEDED(hr))
        hr = edits_split(s, end, &last);
    if (FAILED(hr)) {
        if (result) {
            (*result)->lpVtbl->Release(*result);
            *result = nullptr;
        }
        return hr;
    }
    /* the second split may have moved the boundary of the first. */
    for (int i = last - 1; i >= first; i--)
        edits_remove(s, i);
    s->info.dwLength -= (DWORD)*length;
    s->info.dwEditCount++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE editi_paste(IAVIEditStream *This, LONG *pos, LONG *length, PAVISTREAM source,
                                             LONG src_start, LONG src_length)
{
    EditStream *s = EDIT_FROM_EDIT(This);
    AVISTREAMINFOW info = {0};
    HRESULT hr = AVIStreamInfoW(source, &info, AVISTREAMINFOW_SIZE);
    if (FAILED(hr))
        return hr;
    if (!pos)
        return AVIERR_BADPARAM;
    if (!s->info.fccType) {
        hr = AVIStreamInfoW(source, &s->info, AVISTREAMINFOW_SIZE);
        if (FAILED(hr))
            return hr;
        s->info.dwLength = 0;
        s->info.dwStart = (DWORD)*pos;
    }
    if (*pos > edit_end(s) || *pos < (LONG)s->info.dwStart)
        return AVIERR_BADPARAM;
    if (info.fccType != s->info.fccType)
        return AVIERR_UNSUPPORTED;
    LONG source_end = (LONG)(info.dwStart + info.dwLength);
    if (src_length <= 0 || src_start + src_length > source_end) {
        if (src_start >= source_end || src_start < (LONG)info.dwStart)
            return AVIERR_BADPARAM;
        src_length = source_end - src_start;
    }
    if (*pos + (INT64)src_length > 0x7FFFFFFF)
        return AVIERR_MEMORY;
    EditStream *other = edit_of(source);
    if (s->count) {
        PAVISTREAM first = s->edits[0].stream;
        if (info.fccType == FCC_vids) {
            if (info.rcFrame.right - info.rcFrame.left != s->info.rcFrame.right - s->info.rcFrame.left ||
                info.rcFrame.bottom - info.rcFrame.top != s->info.rcFrame.bottom - s->info.rcFrame.top)
                return AVIERR_UNSUPPORTED;
        } else if (info.fccType == FCC_auds && !audio_compatible(first, source)) {
            return AVIERR_UNSUPPORTED;
        }
        if (info.fccType == FCC_vids) {
            int index;
            LONG source_pos;
            BOOL splice = !s->frames && video_compatible(first, source);
            /* the piece has to start at a key frame, and what comes after it too. */
            if (splice && (other ? FALSE : !is_key(source, src_start)))
                splice = FALSE;
            if (splice && other) {
                int i;
                LONG at;
                splice = edits_locate(other, src_start, &i, &at) && is_key(other->edits[i].stream, at);
            }
            if (splice && *pos < edit_end(s) && edits_locate(s, *pos, &index, &source_pos))
                splice = is_key(s->edits[index].stream, source_pos);
            if (!splice) {
                hr = edit_use_frames(s);
                if (FAILED(hr))
                    return hr;
                PGETFRAME reader = AVIStreamGetFrameOpen(source, nullptr);
                if (!reader)
                    return AVIERR_BADFORMAT;
                reader->lpVtbl->Release(reader);
            }
        }
    }
    if (length)
        *length = src_length;
    int at;
    hr = edits_split(s, *pos, &at);
    if (FAILED(hr))
        return hr;
    if (other) {
        /* the pieces of an editable stream are put in one by one. */
        LONG skip = src_start - (LONG)other->info.dwStart, want = src_length;
        int inserted = 0;
        for (int i = 0; i < other->count && want > 0; i++) {
            const Edit *e = &other->edits[i];
            if (skip >= e->length) {
                skip -= e->length;
                continue;
            }
            LONG take = e->length - skip < want ? e->length - skip : want;
            hr = edits_room(s, 1);
            if (FAILED(hr))
                return hr;
            memmove(s->edits + at + inserted + 1, s->edits + at + inserted,
                    sizeof(Edit) * (SIZE_T)(s->count - at - inserted));
            s->edits[at + inserted].stream = e->stream;
            s->edits[at + inserted].start = e->start + skip;
            s->edits[at + inserted].length = take;
            e->stream->lpVtbl->AddRef(e->stream);
            s->count++;
            inserted++;
            want -= take;
            skip = 0;
        }
    } else {
        hr = edits_room(s, 1);
        if (FAILED(hr))
            return hr;
        memmove(s->edits + at + 1, s->edits + at, sizeof(Edit) * (SIZE_T)(s->count - at));
        s->edits[at].stream = source;
        s->edits[at].start = src_start;
        s->edits[at].length = src_length;
        source->lpVtbl->AddRef(source);
        s->count++;
    }
    edits_prune(s);
    s->info.dwLength += (DWORD)src_length;
    s->info.dwEditCount++;
    return S_OK;
}

/* ------------------------------------------------------------------ the functions */

HRESULT AviEditStreamCreate(IUnknown *outer, REFIID riid, void **object)
{
    EditStream *s = edit_make(nullptr);
    *object = nullptr;
    if (!s)
        return E_OUTOFMEMORY;
    HRESULT hr = edit_qi((IAVIStream *)s, riid, object);
    edit_release((IAVIStream *)s);
    return hr;
}

static HRESULT edit_interface(PAVISTREAM stream, IAVIEditStream **edit)
{
    *edit = nullptr;
    stream->lpVtbl->QueryInterface(stream, &IID_IAVIEditStream, (void **)edit);
    return *edit ? S_OK : E_NOINTERFACE;
}

HRESULT WINAPI CreateEditableStream(PAVISTREAM *editable, PAVISTREAM source)
{
    IAVIEditStream *edit = nullptr;
    if (source) {
        source->lpVtbl->QueryInterface(source, &IID_IAVIEditStream, (void **)&edit);
        if (edit) {
            HRESULT hr = edit->lpVtbl->Clone(edit, editable);
            edit->lpVtbl->Release(edit);
            return hr;
        }
    }
    EditStream *s = edit_make(source);
    if (!s) {
        *editable = nullptr;
        return AVIERR_MEMORY;
    }
    *editable = (PAVISTREAM)&s->lpStream;
    return S_OK;
}

HRESULT WINAPI EditStreamClone(PAVISTREAM stream, PAVISTREAM *result)
{
    IAVIEditStream *edit;
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = edit->lpVtbl->Clone(edit, result);
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamCopy(PAVISTREAM stream, LONG *start, LONG *length, PAVISTREAM *result)
{
    IAVIEditStream *edit;
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = edit->lpVtbl->Copy(edit, start, length, result);
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamCut(PAVISTREAM stream, LONG *start, LONG *length, PAVISTREAM *result)
{
    IAVIEditStream *edit;
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = edit->lpVtbl->Cut(edit, start, length, result);
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamPaste(PAVISTREAM stream, LONG *pos, LONG *length, PAVISTREAM source, LONG start, LONG end)
{
    IAVIEditStream *edit;
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = edit->lpVtbl->Paste(edit, pos, length, source, start, end);
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamSetInfoW(PAVISTREAM stream, AVISTREAMINFOW *info, LONG size)
{
    IAVIEditStream *edit;
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = edit->lpVtbl->SetInfo(edit, info, size);
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamSetInfoA(PAVISTREAM stream, AVISTREAMINFOA *info, LONG size)
{
    AVISTREAMINFOW wide = {0};
    if ((ULONG)size < AVISTREAMINFOA_SIZE)
        return AVIERR_BADSIZE;
    memcpy(&wide, info, offsetof(AVISTREAMINFOW, szName));
    MultiByteToWideChar(CP_ACP, 0, info->szName, -1, wide.szName, 64);
    return EditStreamSetInfoW(stream, &wide, AVISTREAMINFOW_SIZE);
}

HRESULT WINAPI EditStreamSetNameW(PAVISTREAM stream, LPCWSTR name)
{
    IAVIEditStream *edit;
    AVISTREAMINFOW info = {0};
    if (FAILED(edit_interface(stream, &edit)))
        return E_NOINTERFACE;
    HRESULT hr = stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE);
    if (SUCCEEDED(hr)) {
        AviCopyName(info.szName, name, 64);
        hr = edit->lpVtbl->SetInfo(edit, &info, AVISTREAMINFOW_SIZE);
    }
    edit->lpVtbl->Release(edit);
    return hr;
}

HRESULT WINAPI EditStreamSetNameA(PAVISTREAM stream, LPCSTR name)
{
    WCHAR wide[64] = {0};
    MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 64);
    wide[63] = 0;
    return EditStreamSetNameW(stream, wide);
}
