/*
 * AVISave and AVISaveV: a file made from streams. The streams, compressed when the options ask for it, are copied
 * into a new file through its interfaces: one stream after the other, or when the options ask for it with the audio
 * interleaved with the video, in records of one frame or of several.
 */
#include "avifil32_private.h"

#define SAVE_MAX_STREAMS 64
#define SAVE_BUFFER 0x8000
#define OF_SAVE (OF_CREATE | OF_SHARE_EXCLUSIVE | OF_WRITE)

typedef BOOL(WINAPI *AVISAVECALLBACK)(int percent);

static BOOL WINAPI no_callback(int percent)
{
    return FALSE;
}

typedef struct SaveJob {
    PAVIFILE file;
    PAVISTREAM source[SAVE_MAX_STREAMS];
    PAVISTREAM out[SAVE_MAX_STREAMS];
    LONG position[SAVE_MAX_STREAMS];
    int count;
    AVISAVECALLBACK callback;
    BYTE *buffer;
    DWORD capacity;
} SaveJob;

static BOOL grow_buffer(SaveJob *job)
{
    DWORD wanted = job->capacity * 2;
    BYTE *grown = AviRealloc(job->buffer, wanted);
    if (!grown)
        return FALSE;
    job->buffer = grown;
    job->capacity = wanted;
    return TRUE;
}

/* the format of a stream at a position goes to the new stream, as the format that holds from there on. */
static HRESULT copy_format(SaveJob *job, int i, LONG pos)
{
    LONG size = (LONG)job->capacity;
    HRESULT hr = job->source[i]->lpVtbl->ReadFormat(job->source[i], pos, job->buffer, &size);
    if (hr == AVIERR_BUFFERTOOSMALL) {
        while ((LONG)job->capacity < size)
            if (!grow_buffer(job))
                return AVIERR_MEMORY;
        hr = job->source[i]->lpVtbl->ReadFormat(job->source[i], pos, job->buffer, &size);
    }
    if (FAILED(hr))
        return hr;
    return job->out[i]->lpVtbl->SetFormat(job->out[i], pos, job->buffer, size);
}

/* reads samples into the buffer, which grows when they do not fit. */
static HRESULT read_samples(SaveJob *job, int i, LONG pos, LONG samples, LONG *bytes, LONG *got)
{
    for (;;) {
        *got = 0;
        *bytes = (LONG)job->capacity;
        HRESULT hr = job->source[i]->lpVtbl->Read(job->source[i], pos, samples, job->buffer, (LONG)job->capacity,
                                                  bytes, got);
        if (hr != AVIERR_BUFFERTOOSMALL)
            return hr;
        if (!grow_buffer(job))
            return AVIERR_MEMORY;
    }
}

/* copies the samples of a stream from pos to end, the key frames marked. */
static HRESULT copy_range(SaveJob *job, int i, LONG pos, LONG end, BOOL one_by_one)
{
    while (pos < end) {
        LONG bytes, got;
        HRESULT hr = copy_format(job, i, pos);
        if (FAILED(hr))
            return hr;
        hr = read_samples(job, i, pos, one_by_one ? 1 : end - pos, &bytes, &got);
        if (FAILED(hr))
            return hr;
        if (!got)
            return AVIERR_FILEREAD;
        DWORD flags = 0;
        if (one_by_one && job->source[i]->lpVtbl->FindSample(job->source[i], pos, FIND_PREV | FIND_KEY) == pos)
            flags = AVIIF_KEYFRAME;
        hr = job->out[i]->lpVtbl->Write(job->out[i], -1, got, job->buffer, bytes, flags, nullptr, nullptr);
        if (FAILED(hr))
            return hr;
        pos += got;
        job->position[i] = pos;
    }
    return S_OK;
}

static HRESULT copy_in_turn(SaveJob *job)
{
    for (int i = 0; i < job->count; i++) {
        AVISTREAMINFOW info = {0};
        if (job->callback(MulDiv(i, 100, job->count)))
            return AVIERR_USERABORT;
        HRESULT hr = AVIStreamInfoW(job->source[i], &info, AVISTREAMINFOW_SIZE);
        if (FAILED(hr))
            return hr;
        LONG pos = (LONG)info.dwStart, end = (LONG)(info.dwStart + info.dwLength);
        while (pos < end) {
            LONG bytes, got;
            hr = copy_format(job, i, pos);
            if (FAILED(hr))
                return hr;
            LONG wanted = 1;
            if (info.dwSampleSize) {
                wanted = (LONG)(job->capacity / info.dwSampleSize);
                if (wanted > end - pos)
                    wanted = end - pos;
            }
            hr = read_samples(job, i, pos, wanted, &bytes, &got);
            if (FAILED(hr))
                return hr;
            if (!got)
                return AVIERR_FILEREAD;
            DWORD flags = 0;
            if (!info.dwSampleSize &&
                job->source[i]->lpVtbl->FindSample(job->source[i], pos, FIND_PREV | FIND_KEY) == pos)
                flags = AVIIF_KEYFRAME;
            hr = job->out[i]->lpVtbl->Write(job->out[i], -1, got, job->buffer, bytes, flags, nullptr, nullptr);
            if (FAILED(hr))
                return hr;
            pos += got;
            int length = (int)(info.dwLength ? info.dwLength : 1);
            if (job->callback(MulDiv(pos - (LONG)info.dwStart, 100, length * job->count) + MulDiv(i, 100, job->count)))
                return AVIERR_USERABORT;
        }
    }
    return S_OK;
}

/* the audio runs ahead of the video, and the streams are written in records of a step of the first stream. */
static HRESULT copy_interleaved(SaveJob *job, DWORD every, LONG step, LONG length, LONG ahead)
{
    HRESULT hr;
    if (every == 1) {
        hr = job->file->lpVtbl->EndRecord(job->file);
        if (FAILED(hr))
            return hr;
    }
    for (LONG at = -ahead; at < length; at += step) {
        for (int i = 0; i < job->count; i++) {
            AVISTREAMINFOW out_info = {0}, in_info = {0};
            hr = AVIStreamInfoW(job->out[i], &out_info, AVISTREAMINFOW_SIZE);
            if (FAILED(hr))
                return hr;
            if (at < -(LONG)out_info.dwInitialFrames)
                continue;
            hr = AVIStreamInfoW(job->source[i], &in_info, AVISTREAMINFOW_SIZE);
            if (FAILED(hr))
                return hr;
            LONG here = at + (LONG)out_info.dwInitialFrames;
            LONG target = here + step;
            LONG stream_end = (LONG)(in_info.dwStart + in_info.dwLength);
            if (at >= length - step) {
                target = stream_end;
            } else if (in_info.fccType == FCC_vids) {
                if (i)
                    target = AviTimeToSampleNoClip(job->source[i], AviSampleToTimeNoClip(job->source[0], target));
            } else if (i) {
                if (here % (LONG)every)
                    continue;
                target = AviTimeToSampleNoClip(job->source[i], AviSampleToTimeNoClip(job->source[0], here + (LONG)every));
            }
            if (target > stream_end)
                target = stream_end;
            if (target > job->position[i]) {
                hr = copy_range(job, i, job->position[i], target, in_info.dwSampleSize == 0);
                if (FAILED(hr))
                    return hr;
            }
        }
        if (every == 1) {
            hr = job->file->lpVtbl->EndRecord(job->file);
            if (FAILED(hr))
                return hr;
        }
        if (job->callback(MulDiv(ahead + at, 100, ahead + length)))
            return AVIERR_USERABORT;
    }
    return S_OK;
}

static HRESULT save_streams(LPCWSTR name, const CLSID *handler, AVISAVECALLBACK callback, int count,
                            PAVISTREAM *streams, LPAVICOMPRESSOPTIONS *options)
{
    SaveJob job = {0};
    AVIFILEINFOW file_info = {0};
    AVISTREAMINFOW info = {0};
    DWORD every = 0;
    int video = -1;
    LONG ahead = 0, length, step;
    HRESULT hr;
    if (count > SAVE_MAX_STREAMS)
        return AVIERR_INTERNAL;
    if (count <= 0 || !streams)
        return AVIERR_BADPARAM;
    job.count = count;
    job.callback = callback ? callback : no_callback;
    hr = AVIFileOpenW(&job.file, name, OF_SAVE, handler);
    if (FAILED(hr))
        return hr;
    AVIFileInfoW(job.file, &file_info, AVIFILEINFOW_SIZE);
    for (int i = 0; i < count; i++) {
        hr = AVIStreamInfoW(streams[i], &info, AVISTREAMINFOW_SIZE);
        if (FAILED(hr))
            goto done;
        if (info.fccType == FCC_vids) {
            if (video < 0)
                video = i;
        } else if (!every && options && options[i] && (options[i]->dwFlags & AVICOMPRESSF_INTERLEAVE)) {
            every = options[i]->dwInterleaveEvery;
        }
        if (options && options[i] && (options[i]->fccHandler || options[i]->lpFormat)) {
            DWORD key_every = options[i]->dwKeyFrameEvery;
            if (file_info.dwCaps & AVIFILECAPS_ALLKEYFRAMES)
                options[i]->dwKeyFrameEvery = 1;
            hr = AVIMakeCompressedStream(&job.source[i], streams[i], options[i], nullptr);
            options[i]->dwKeyFrameEvery = key_every;
            if (FAILED(hr)) {
                job.source[i] = nullptr;
                goto done;
            }
        } else {
            job.source[i] = streams[i];
            streams[i]->lpVtbl->AddRef(streams[i]);
        }
    }
    /* the video comes first. */
    if (video > 0) {
        PAVISTREAM swap = job.source[0];
        job.source[0] = job.source[video];
        job.source[video] = swap;
        video = 0;
    }
    job.capacity = SAVE_BUFFER;
    job.buffer = AviAlloc(job.capacity);
    if (!job.buffer) {
        hr = AVIERR_MEMORY;
        goto done;
    }
    hr = AVIStreamInfoW(job.source[0], &info, AVISTREAMINFOW_SIZE);
    if (FAILED(hr))
        goto done;
    length = (LONG)info.dwLength;
    if (video < 0) {
        step = AVIStreamTimeToSample(job.source[0], 500);
    } else {
        LONG size = (LONG)job.capacity;
        hr = job.source[0]->lpVtbl->ReadFormat(job.source[0], AVIStreamStart(job.source[0]), job.buffer, &size);
        if (size < (LONG)sizeof(BITMAPINFOHEADER)) {
            hr = AVIERR_INTERNAL;
            goto done;
        }
        if (FAILED(hr))
            goto done;
        step = 1;
    }
    if (step < 1)
        step = 1;
    {
        int made = 0;
        for (int i = 0; i < count; i++) {
            PAVISTREAM stream = job.source[i];
            hr = AVIStreamInfoW(stream, &info, AVISTREAMINFOW_SIZE);
            if (FAILED(hr))
                goto done;
            info.dwInitialFrames = 0;
            if (every && i > 0 && info.fccType != FCC_vids)
                info.dwInitialFrames = (DWORD)AVIStreamTimeToSample(made ? job.source[0] : stream, 750);
            LONG size = (LONG)job.capacity;
            hr = stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), job.buffer, &size);
            if (hr == AVIERR_BUFFERTOOSMALL) {
                while ((LONG)job.capacity < size)
                    if (!grow_buffer(&job)) {
                        hr = AVIERR_MEMORY;
                        goto done;
                    }
                hr = stream->lpVtbl->ReadFormat(stream, AVIStreamStart(stream), job.buffer, &size);
            }
            if (FAILED(hr))
                goto done;
            PAVISTREAM created = nullptr;
            hr = job.file->lpVtbl->CreateStream(job.file, &created, &info);
            if (hr != S_OK || !created) {
                /* a stream the file cannot hold is left out. */
                if (created)
                    created->lpVtbl->Release(created);
                stream->lpVtbl->Release(stream);
                job.source[i] = nullptr;
                continue;
            }
            job.source[i] = nullptr;
            job.source[made] = stream;
            job.out[made] = created;
            hr = created->lpVtbl->SetFormat(created, 0, job.buffer, size);
            if (FAILED(hr))
                goto done;
            size = (LONG)job.capacity;
            if (SUCCEEDED(stream->lpVtbl->ReadData(stream, FCC_strd, job.buffer, &size)) && size) {
                hr = created->lpVtbl->WriteData(created, FCC_strd, job.buffer, size);
                if (FAILED(hr))
                    goto done;
            }
            if (info.dwInitialFrames > (DWORD)ahead)
                ahead = (LONG)info.dwInitialFrames;
            LONG ends = AviTimeToSampleNoClip(made ? job.source[0] : stream, AviSampleToTimeNoClip(stream, AVIStreamLength(stream)));
            if (ends > length)
                length = ends;
            made++;
        }
        job.count = made;
    }
    if (job.count <= 0) {
        hr = AVIERR_ERROR;
        goto done;
    }
    /* the positions in the new streams start where the streams start. */
    for (int i = 0; i < job.count; i++)
        job.position[i] = AVIStreamStart(job.source[i]);
    if (every)
        hr = copy_interleaved(&job, every, step, length, ahead);
    else
        hr = copy_in_turn(&job);
done:
    /* the new streams go first, the file writes its header when the last of them is gone. */
    for (int i = 0; i < SAVE_MAX_STREAMS; i++) {
        if (job.out[i])
            job.out[i]->lpVtbl->Release(job.out[i]);
    }
    job.file->lpVtbl->Release(job.file);
    for (int i = 0; i < SAVE_MAX_STREAMS; i++) {
        if (job.source[i])
            job.source[i]->lpVtbl->Release(job.source[i]);
    }
    AviFree(job.buffer);
    return hr;
}

HRESULT WINAPI AVISaveVW(LPCWSTR name, const CLSID *handler, AVISAVECALLBACK callback, int count, PAVISTREAM *streams,
                         LPAVICOMPRESSOPTIONS *options)
{
    return save_streams(name, handler, callback, count, streams, options);
}

HRESULT WINAPI AVISaveVA(LPCSTR name, const CLSID *handler, AVISAVECALLBACK callback, int count, PAVISTREAM *streams,
                         LPAVICOMPRESSOPTIONS *options)
{
    int length = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    WCHAR *wide = AviAlloc((SIZE_T)(length > 0 ? length : 1) * sizeof(WCHAR));
    if (!wide)
        return AVIERR_MEMORY;
    if (length > 0)
        MultiByteToWideChar(CP_ACP, 0, name, -1, wide, length);
    HRESULT hr = AVISaveVW(wide, handler, callback, count, streams, options);
    AviFree(wide);
    return hr;
}

/* the streams and options of AVISave follow one another as arguments, the first pair in named parameters. */
static HRESULT save_arguments(LPCWSTR wide, LPCSTR ansi, const CLSID *handler, AVISAVECALLBACK callback, int count,
                              PAVISTREAM first, LPAVICOMPRESSOPTIONS first_options, __builtin_va_list more)
{
    if (count < 0 || (SIZE_T)count > 0x1FFFFFFF)
        return (HRESULT)0x80070216L;
    PAVISTREAM *streams = AviAlloc((SIZE_T)count * sizeof(*streams));
    LPAVICOMPRESSOPTIONS *options = AviAlloc((SIZE_T)count * sizeof(*options));
    HRESULT hr;
    if (!streams || !options) {
        AviFree(streams);
        AviFree(options);
        return AVIERR_MEMORY;
    }
    if (count) {
        streams[0] = first;
        options[0] = first_options;
    }
    for (int i = 1; i < count; i++) {
        streams[i] = __builtin_va_arg(more, PAVISTREAM);
        options[i] = __builtin_va_arg(more, LPAVICOMPRESSOPTIONS);
    }
    hr = wide ? AVISaveVW(wide, handler, callback, count, streams, options)
              : AVISaveVA(ansi, handler, callback, count, streams, options);
    AviFree(streams);
    AviFree(options);
    return hr;
}

HRESULT __cdecl AVISaveW(LPCWSTR name, const CLSID *handler, AVISAVECALLBACK callback, int count, PAVISTREAM stream,
                         LPAVICOMPRESSOPTIONS options, ...)
{
    __builtin_va_list more;
    __builtin_va_start(more, options);
    HRESULT hr = save_arguments(name, nullptr, handler, callback, count, stream, options, more);
    __builtin_va_end(more);
    return hr;
}

HRESULT __cdecl AVISaveA(LPCSTR name, const CLSID *handler, AVISAVECALLBACK callback, int count, PAVISTREAM stream,
                         LPAVICOMPRESSOPTIONS options, ...)
{
    __builtin_va_list more;
    __builtin_va_start(more, options);
    HRESULT hr = save_arguments(nullptr, name, handler, callback, count, stream, options, more);
    __builtin_va_end(more);
    return hr;
}
