/* samples: attributes, a time, a duration, flags and a list of buffers. */
#include "mfplat_private.h"

typedef struct {
    MfObject base;
    DWORD flags;
    /* which of time and duration were set, as bits 1 and 2. */
    DWORD has;
    LONGLONG time;
    LONGLONG duration;
    IMFMediaBuffer **buffers;
    DWORD count;
    DWORD capacity;
} Sample;

typedef struct {
    IMFAttributesVtbl attributes;
    HRESULT(STDMETHODCALLTYPE *GetSampleFlags)(IMFSample *This, DWORD *flags);
    HRESULT(STDMETHODCALLTYPE *SetSampleFlags)(IMFSample *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetSampleTime)(IMFSample *This, LONGLONG *time);
    HRESULT(STDMETHODCALLTYPE *SetSampleTime)(IMFSample *This, LONGLONG time);
    HRESULT(STDMETHODCALLTYPE *GetSampleDuration)(IMFSample *This, LONGLONG *duration);
    HRESULT(STDMETHODCALLTYPE *SetSampleDuration)(IMFSample *This, LONGLONG duration);
    HRESULT(STDMETHODCALLTYPE *GetBufferCount)(IMFSample *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetBufferByIndex)(IMFSample *This, DWORD index, IMFMediaBuffer **buffer);
    HRESULT(STDMETHODCALLTYPE *ConvertToContiguousBuffer)(IMFSample *This, IMFMediaBuffer **buffer);
    HRESULT(STDMETHODCALLTYPE *AddBuffer)(IMFSample *This, IMFMediaBuffer *buffer);
    HRESULT(STDMETHODCALLTYPE *RemoveBufferByIndex)(IMFSample *This, DWORD index);
    HRESULT(STDMETHODCALLTYPE *RemoveAllBuffers)(IMFSample *This);
    HRESULT(STDMETHODCALLTYPE *GetTotalLength)(IMFSample *This, DWORD *length);
    HRESULT(STDMETHODCALLTYPE *CopyToBuffer)(IMFSample *This, IMFMediaBuffer *buffer);
} SampleVtbl;

static Sample *sample_of(IMFSample *iface)
{
    return (Sample *)iface;
}

static void sample_destroy(MfObject *object)
{
    Sample *sample = (Sample *)object;
    for (DWORD i = 0; i < sample->count; i++)
        sample->buffers[i]->lpVtbl->Release(sample->buffers[i]);
    MfFree(sample->buffers);
}

static const GUID *const SAMPLE_IIDS[] = {&IID_IMFSample, nullptr};
static const MfClass SAMPLE_CLASS = {SAMPLE_IIDS, sample_destroy};

static HRESULT STDMETHODCALLTYPE sample_get_flags(IMFSample *iface, DWORD *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = sample_of(iface)->flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_flags(IMFSample *iface, DWORD flags)
{
    sample_of(iface)->flags = flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_time(IMFSample *iface, LONGLONG *time)
{
    Sample *sample = sample_of(iface);
    if (!time)
        return E_INVALIDARG;
    if (!(sample->has & 1))
        return MF_E_NO_SAMPLE_TIMESTAMP;
    *time = sample->time;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_time(IMFSample *iface, LONGLONG time)
{
    Sample *sample = sample_of(iface);
    sample->time = time;
    sample->has |= 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_duration(IMFSample *iface, LONGLONG *duration)
{
    Sample *sample = sample_of(iface);
    if (!duration)
        return E_INVALIDARG;
    if (!(sample->has & 2))
        return MF_E_NO_SAMPLE_DURATION;
    *duration = sample->duration;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_set_duration(IMFSample *iface, LONGLONG duration)
{
    Sample *sample = sample_of(iface);
    sample->duration = duration;
    sample->has |= 2;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_buffer_count(IMFSample *iface, DWORD *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = sample_of(iface)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_buffer_by_index(IMFSample *iface, DWORD index, IMFMediaBuffer **buffer)
{
    Sample *sample = sample_of(iface);
    if (index >= sample->count || !buffer)
        return E_INVALIDARG;
    *buffer = sample->buffers[index];
    (*buffer)->lpVtbl->AddRef(*buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_add_buffer(IMFSample *iface, IMFMediaBuffer *buffer)
{
    Sample *sample = sample_of(iface);
    if (!buffer)
        return E_INVALIDARG;
    if (sample->count == sample->capacity) {
        DWORD capacity = sample->count + 3;
        IMFMediaBuffer **buffers = MfAlloc(capacity * sizeof(*buffers));
        if (!buffers)
            return E_OUTOFMEMORY;
        if (sample->count)
            memcpy(buffers, sample->buffers, sample->count * sizeof(*buffers));
        MfFree(sample->buffers);
        sample->buffers = buffers;
        sample->capacity = capacity;
    }
    sample->buffers[sample->count++] = buffer;
    buffer->lpVtbl->AddRef(buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_remove_buffer_by_index(IMFSample *iface, DWORD index)
{
    Sample *sample = sample_of(iface);
    if (index >= sample->count)
        return E_INVALIDARG;
    IMFMediaBuffer *buffer = sample->buffers[index];
    memmove(&sample->buffers[index], &sample->buffers[index + 1], (sample->count - index - 1) * sizeof(*buffer));
    sample->count--;
    buffer->lpVtbl->Release(buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_remove_all_buffers(IMFSample *iface)
{
    Sample *sample = sample_of(iface);
    for (DWORD i = 0; i < sample->count; i++)
        sample->buffers[i]->lpVtbl->Release(sample->buffers[i]);
    sample->count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sample_get_total_length(IMFSample *iface, DWORD *length)
{
    Sample *sample = sample_of(iface);
    if (!length)
        return E_INVALIDARG;
    DWORD total = 0;
    for (DWORD i = 0; i < sample->count; i++) {
        DWORD current = 0;
        HRESULT hr = sample->buffers[i]->lpVtbl->GetCurrentLength(sample->buffers[i], &current);
        if (FAILED(hr))
            return hr;
        if (total + current < total)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        total += current;
    }
    *length = total;
    return S_OK;
}

/* the buffers one after the other, the destination's length becoming their total. */
static HRESULT STDMETHODCALLTYPE sample_copy_to_buffer(IMFSample *iface, IMFMediaBuffer *dest)
{
    Sample *sample = sample_of(iface);
    if (!dest)
        return E_INVALIDARG;
    BYTE *out = nullptr;
    DWORD max = 0, total = 0;
    HRESULT hr = dest->lpVtbl->Lock(dest, &out, &max, nullptr);
    if (FAILED(hr)) {
        dest->lpVtbl->SetCurrentLength(dest, 0);
        return hr;
    }
    hr = sample_get_total_length(iface, &total);
    if (SUCCEEDED(hr) && total > max)
        hr = MF_E_BUFFERTOOSMALL;
    for (DWORD i = 0; SUCCEEDED(hr) && i < sample->count; i++) {
        BYTE *data = nullptr;
        DWORD length = 0;
        hr = sample->buffers[i]->lpVtbl->Lock(sample->buffers[i], &data, nullptr, &length);
        if (FAILED(hr))
            break;
        memcpy(out, data, length);
        out += length;
        sample->buffers[i]->lpVtbl->Unlock(sample->buffers[i]);
    }
    if (SUCCEEDED(hr))
        hr = dest->lpVtbl->SetCurrentLength(dest, total);
    if (FAILED(hr))
        dest->lpVtbl->SetCurrentLength(dest, 0);
    dest->lpVtbl->Unlock(dest);
    return hr;
}

/* a single buffer as it is, several copied into a new one that replaces them. */
static HRESULT STDMETHODCALLTYPE sample_convert_to_contiguous_buffer(IMFSample *iface, IMFMediaBuffer **out)
{
    Sample *sample = sample_of(iface);
    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = S_OK;
    if (!sample->count)
        return E_UNEXPECTED;
    if (sample->count == 1) {
        buffer = sample->buffers[0];
        buffer->lpVtbl->AddRef(buffer);
    } else {
        DWORD total = 0;
        hr = sample_get_total_length(iface, &total);
        if (SUCCEEDED(hr))
            hr = MfCreateMemoryBuffer(total, 1, &buffer);
        if (SUCCEEDED(hr))
            hr = sample_copy_to_buffer(iface, buffer);
        if (SUCCEEDED(hr))
            hr = sample_remove_all_buffers(iface);
        if (SUCCEEDED(hr))
            hr = sample_add_buffer(iface, buffer);
    }
    if (SUCCEEDED(hr) && out) {
        *out = buffer;
        return hr;
    }
    if (buffer)
        buffer->lpVtbl->Release(buffer);
    return hr;
}

static const SampleVtbl SAMPLE_VTBL = {
    MF_ATTRIBUTES_VTBL,
    sample_get_flags,
    sample_set_flags,
    sample_get_time,
    sample_set_time,
    sample_get_duration,
    sample_set_duration,
    sample_get_buffer_count,
    sample_get_buffer_by_index,
    sample_convert_to_contiguous_buffer,
    sample_add_buffer,
    sample_remove_buffer_by_index,
    sample_remove_all_buffers,
    sample_get_total_length,
    sample_copy_to_buffer,
};

HRESULT WINAPI MFCreateSample(IMFSample **out)
{
    if (!out)
        return E_POINTER;
    Sample *sample = MfAllocZero(sizeof(*sample));
    if (!sample)
        return E_OUTOFMEMORY;
    MfObjectInit(&sample->base, &SAMPLE_VTBL, &SAMPLE_CLASS, 0);
    *out = (IMFSample *)sample;
    return S_OK;
}
