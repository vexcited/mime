#include "dosvc.h"

struct DO_EXT {
    IDODownload download;
    IDODownloadInternal internal;
    LONG refs;
    DO_JOB *job;
    IUnknown *callback_object;
    IDODownloadStatusCallback *callback;
    IUnknown *stream_object;
};

/* the ids of IDODownloadInternal, in the ids of the job. */
static const int g_ex_ids[] = {P_UPDATE_ID, P_CV, P_DECRYPTION, P_INTEGRITY, P_INTEGRITY_MANDATORY, P_TOTAL_SIZE, P_TEMP_FILE};

/* the public ids are those of the job up to 23; the four http answers and the two flags after them sit two further. */
static int map_id(DODownloadProperty id)
{
    if ((unsigned)id >= 30)
        return -1;
    return id < 24 ? (int)id : (int)id + 2;
}

static DO_EXT *from_download(IDODownload *iface)
{
    return CONTAINING_RECORD(iface, DO_EXT, download);
}

static DO_EXT *from_internal(IDODownloadInternal *iface)
{
    return CONTAINING_RECORD(iface, DO_EXT, internal);
}

static HRESULT ext_qi(DO_EXT *ext, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDODownload))
        *out = &ext->download;
    else if (IsEqualGUID(iid, &IID_IDODownloadInternal))
        *out = &ext->internal;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&ext->refs);
    return S_OK;
}

static ULONG ext_release(DO_EXT *ext)
{
    LONG refs = InterlockedDecrement(&ext->refs);
    if (refs)
        return (ULONG)refs;
    DO_JOB *job = ext->job;
    AcquireSRWLockExclusive(&job->lock);
    if (job->ext == ext)
        job->ext = nullptr;
    ReleaseSRWLockExclusive(&job->lock);
    if (ext->callback)
        ext->callback->lpVtbl->Release(ext->callback);
    if (ext->callback_object)
        ext->callback_object->lpVtbl->Release(ext->callback_object);
    if (ext->stream_object)
        ext->stream_object->lpVtbl->Release(ext->stream_object);
    JobRelease(job);
    LocalFree(ext);
    DoLockService(FALSE);
    return 0;
}

/* a reference to an object whose count may be dropping to zero on another thread: none once it reached it. */
static BOOL ext_try_addref(DO_EXT *ext)
{
    for (;;) {
        LONG refs = ext->refs;
        if (!refs)
            return FALSE;
        if (InterlockedCompareExchange(&ext->refs, refs + 1, refs) == refs)
            return TRUE;
    }
}

static HRESULT check_variant(const VARIANT *value)
{
    switch (V_VT(value)) {
    case VT_EMPTY:
    case VT_NULL:
    case VT_BSTR:
    case VT_BOOL:
    case VT_UI4:
    case VT_UI8:
        return S_OK;
    default:
        return DISP_E_BADVARTYPE;
    }
}

static HRESULT set_callback(DO_EXT *ext, const VARIANT *value)
{
    IUnknown *object = nullptr;
    IDODownloadStatusCallback *callback = nullptr;
    if (V_VT(value) == VT_UNKNOWN && V_UNKNOWN(value)) {
        object = V_UNKNOWN(value);
        HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_IDODownloadStatusCallback, (void **)&callback);
        if (FAILED(hr))
            return hr;
        object->lpVtbl->AddRef(object);
    } else if (V_VT(value) != VT_EMPTY && V_VT(value) != VT_NULL && V_VT(value) != VT_UNKNOWN)
        return E_INVALIDARG;
    DO_JOB *job = ext->job;
    AcquireSRWLockExclusive(&job->lock);
    IDODownloadStatusCallback *old = ext->callback;
    IUnknown *old_object = ext->callback_object;
    ext->callback = callback;
    ext->callback_object = object;
    ReleaseSRWLockExclusive(&job->lock);
    if (old)
        old->lpVtbl->Release(old);
    if (old_object)
        old_object->lpVtbl->Release(old_object);
    return S_OK;
}

static HRESULT set_stream(DO_EXT *ext, const VARIANT *value)
{
    IUnknown *object = nullptr;
    ISequentialStream *stream = nullptr;
    if (V_VT(value) == VT_UNKNOWN && V_UNKNOWN(value)) {
        object = V_UNKNOWN(value);
        HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_ISequentialStream, (void **)&stream);
        if (hr == E_NOINTERFACE)
            hr = object->lpVtbl->QueryInterface(object, &IID_IStream, (void **)&stream);
        if (FAILED(hr))
            return hr;
    } else if (V_VT(value) != VT_EMPTY && V_VT(value) != VT_NULL && V_VT(value) != VT_UNKNOWN)
        return E_INVALIDARG;
    HRESULT hr = JobSetStream(ext->job, stream);
    if (stream)
        stream->lpVtbl->Release(stream);
    if (FAILED(hr))
        return hr;
    if (object)
        object->lpVtbl->AddRef(object);
    IUnknown *old = InterlockedExchangePointer((void **)&ext->stream_object, object);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

/* the client certificate, a serialized element of a store as CertSerializeCertificateStoreElement makes it. */
static HRESULT set_security_context(DO_EXT *ext, const VARIANT *value)
{
    if (V_VT(value) != VT_EMPTY && V_VT(value) != (VT_ARRAY | VT_UI1))
        return E_INVALIDARG;
    if (V_VT(value) == VT_EMPTY || !V_ARRAY(value)) {
        JobSetClientCertificate(ext->job, nullptr);
        return S_OK;
    }
    SAFEARRAY *array = V_ARRAY(value);
    void *data = nullptr;
    HRESULT hr = SafeArrayAccessData(array, &data);
    if (FAILED(hr))
        return hr;
    LONG low = 0, high = 0;
    hr = SafeArrayGetLBound(array, 1, &low);
    if (SUCCEEDED(hr))
        hr = SafeArrayGetUBound(array, 1, &high);
    if (SUCCEEDED(hr) && high - low + 1 <= 0)
        hr = E_INVALIDARG;
    PCCERT_CONTEXT cert = nullptr;
    /* CERT_STORE_ADD_NEW, CERT_STORE_CERTIFICATE_CONTEXT_FLAG */
    if (SUCCEEDED(hr) && !CertAddSerializedElementToStore(nullptr, data, (DWORD)(high - low + 1), 1, 0, 2, nullptr,
                                                          (const void **)&cert))
        hr = HRESULT_FROM_WIN32(GetLastError());
    SafeArrayUnaccessData(array);
    if (SUCCEEDED(hr))
        JobSetClientCertificate(ext->job, cert);
    return hr;
}

/* ------------------------------------------------------------------ IDODownload */

static HRESULT STDMETHODCALLTYPE download_qi(IDODownload *iface, REFIID iid, void **out)
{
    return ext_qi(from_download(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE download_addref(IDODownload *iface)
{
    return (ULONG)InterlockedIncrement(&from_download(iface)->refs);
}

static ULONG STDMETHODCALLTYPE download_release(IDODownload *iface)
{
    return ext_release(from_download(iface));
}

/* ranges come sorted or not; each must have a length that fits, and none may reach into the next. */
static HRESULT check_ranges(const DO_DOWNLOAD_RANGE *ranges, UINT count, DO_DOWNLOAD_RANGE **sorted)
{
    DO_DOWNLOAD_RANGE *copy = LocalAlloc(LMEM_FIXED, count * sizeof(*copy));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, ranges, count * sizeof(*copy));
    for (UINT i = 0; i < count; i++) {
        if (!copy[i].Length || (copy[i].Length != DO_LENGTH_TO_EOF && copy[i].Offset + copy[i].Length < copy[i].Offset)) {
            LocalFree(copy);
            return DO_E_INVALID_RANGE_;
        }
    }
    for (UINT i = 1; i < count; i++) {
        DO_DOWNLOAD_RANGE r = copy[i];
        UINT k = i;
        for (; k && copy[k - 1].Offset > r.Offset; k--)
            copy[k] = copy[k - 1];
        copy[k] = r;
    }
    for (UINT i = 1; i < count; i++) {
        if (copy[i - 1].Length == DO_LENGTH_TO_EOF || copy[i - 1].Offset + copy[i - 1].Length > copy[i].Offset) {
            LocalFree(copy);
            return DO_E_OVERLAPPING_RANGES_;
        }
    }
    *sorted = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE download_start(IDODownload *iface, const DO_DOWNLOAD_RANGES_INFO *ranges)
{
    DO_EXT *ext = from_download(iface);
    if (ranges) {
        HRESULT hr;
        if (ranges->RangeCount) {
            DO_DOWNLOAD_RANGE *sorted;
            hr = check_ranges(ranges->Ranges, ranges->RangeCount, &sorted);
            if (FAILED(hr))
                return hr;
            hr = JobSetRanges(ext->job, sorted, ranges->RangeCount);
            LocalFree(sorted);
        } else
            hr = JobSetRanges(ext->job, nullptr, 0);
        if (FAILED(hr))
            return hr;
    }
    return JobStart(ext->job);
}

static HRESULT STDMETHODCALLTYPE download_pause(IDODownload *iface)
{
    return JobPause(from_download(iface)->job);
}

static HRESULT STDMETHODCALLTYPE download_abort(IDODownload *iface)
{
    return JobAbort(from_download(iface)->job);
}

static HRESULT STDMETHODCALLTYPE download_finalize(IDODownload *iface)
{
    return JobFinalize(from_download(iface)->job);
}

static HRESULT STDMETHODCALLTYPE download_status(IDODownload *iface, DO_DOWNLOAD_STATUS *status)
{
    if (!status)
        return E_POINTER;
    JobStatus(from_download(iface)->job, status);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE download_get(IDODownload *iface, DODownloadProperty id, VARIANT *value)
{
    DO_EXT *ext = from_download(iface);
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if (id == DODownloadProperty_CallbackInterface || id == DODownloadProperty_StreamInterface) {
        AcquireSRWLockShared(&ext->job->lock);
        IUnknown *object = id == DODownloadProperty_CallbackInterface ? ext->callback_object : ext->stream_object;
        if (object)
            object->lpVtbl->AddRef(object);
        ReleaseSRWLockShared(&ext->job->lock);
        if (!object)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        V_VT(value) = VT_UNKNOWN;
        V_UNKNOWN(value) = object;
        return S_OK;
    }
    if (id == DODownloadProperty_SecurityContext)
        return DO_E_WRITE_ONLY_PROPERTY;
    int mapped = map_id(id);
    if (mapped < 0)
        return DO_E_UNKNOWN_PROPERTY_ID;
    return JobGetProperty(ext->job, mapped, value);
}

static HRESULT STDMETHODCALLTYPE download_set(IDODownload *iface, DODownloadProperty id, const VARIANT *value)
{
    DO_EXT *ext = from_download(iface);
    if (!value)
        return E_POINTER;
    if ((unsigned)id >= 30)
        return DO_E_UNKNOWN_PROPERTY_ID;
    if (id == DODownloadProperty_CallbackInterface)
        return set_callback(ext, value);
    if (id == DODownloadProperty_StreamInterface)
        return set_stream(ext, value);
    if (id == DODownloadProperty_SecurityContext)
        return set_security_context(ext, value);
    HRESULT hr = check_variant(value);
    if (FAILED(hr))
        return hr;
    if (id == DODownloadProperty_LocalPath && V_VT(value) == VT_BSTR && V_BSTR(value)) {
        VARIANT path;
        V_VT(&path) = VT_BSTR;
        V_BSTR(&path) = SysAllocString(V_BSTR(value));
        if (!V_BSTR(&path))
            return E_OUTOFMEMORY;
        for (WCHAR *p = V_BSTR(&path); *p; p++)
            if (*p == '/')
                *p = '\\';
        hr = JobSetProperty(ext->job, P_LOCAL_PATH, &path);
        VariantClear(&path);
        return hr;
    }
    return JobSetProperty(ext->job, map_id(id), value);
}

static const IDODownloadVtbl g_download_vtbl = {
    download_qi,     download_addref, download_release, download_start, download_pause,
    download_abort,  download_finalize, download_status, download_get,   download_set,
};

/* ------------------------------------------------------------------ IDODownloadInternal */

static HRESULT STDMETHODCALLTYPE internal_qi(IDODownloadInternal *iface, REFIID iid, void **out)
{
    return ext_qi(from_internal(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE internal_addref(IDODownloadInternal *iface)
{
    return (ULONG)InterlockedIncrement(&from_internal(iface)->refs);
}

static ULONG STDMETHODCALLTYPE internal_release(IDODownloadInternal *iface)
{
    return ext_release(from_internal(iface));
}

static HRESULT STDMETHODCALLTYPE internal_get(IDODownloadInternal *iface, DODownloadPropertyEx id, VARIANT *value)
{
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if ((unsigned)id >= ARRAYSIZE(g_ex_ids))
        return DO_E_UNKNOWN_PROPERTY_ID;
    return JobGetProperty(from_internal(iface)->job, g_ex_ids[id], value);
}

static HRESULT STDMETHODCALLTYPE internal_set(IDODownloadInternal *iface, DODownloadPropertyEx id, const VARIANT *value)
{
    if (!value)
        return E_POINTER;
    if ((unsigned)id >= ARRAYSIZE(g_ex_ids))
        return DO_E_UNKNOWN_PROPERTY_ID;
    HRESULT hr = check_variant(value);
    if (FAILED(hr))
        return hr;
    return JobSetProperty(from_internal(iface)->job, g_ex_ids[id], value);
}

static const IDODownloadInternalVtbl g_internal_vtbl = {
    internal_qi, internal_addref, internal_release, internal_get, internal_set,
};

/* the one download object of a job: a job enumerated again gives the object its caller already holds. */
HRESULT ExtFor(DO_JOB *job, IDODownload **out)
{
    AcquireSRWLockExclusive(&job->lock);
    DO_EXT *ext = job->ext;
    if (ext && ext_try_addref(ext)) {
        ReleaseSRWLockExclusive(&job->lock);
        *out = &ext->download;
        return S_OK;
    }
    ext = LocalAlloc(LMEM_ZEROINIT, sizeof(*ext));
    if (!ext) {
        ReleaseSRWLockExclusive(&job->lock);
        return E_OUTOFMEMORY;
    }
    ext->download.lpVtbl = &g_download_vtbl;
    ext->internal.lpVtbl = &g_internal_vtbl;
    ext->refs = 1;
    ext->job = job;
    JobAddRef(job);
    job->ext = ext;
    ReleaseSRWLockExclusive(&job->lock);
    DoLockService(TRUE);
    *out = &ext->download;
    return S_OK;
}

/* ------------------------------------------------------------------ status callbacks */

/* one delivery at a time per job, and the asks that come during one fold into the next, which reads the status then. */
static DWORD WINAPI deliver(void *arg)
{
    DO_JOB *job = arg;
    BOOL com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    LONG asked;
    do {
        asked = job->notify_pending;
        AcquireSRWLockShared(&job->lock);
        DO_EXT *ext = job->ext;
        IDODownloadStatusCallback *callback = nullptr;
        if (ext && ext_try_addref(ext)) {
            callback = ext->callback;
            if (callback)
                callback->lpVtbl->AddRef(callback);
        } else
            ext = nullptr;
        ReleaseSRWLockShared(&job->lock);
        if (callback) {
            AcquireSRWLockShared(&job->lock);
            DO_DOWNLOAD_STATUS status = job->notify_status;
            ReleaseSRWLockShared(&job->lock);
            callback->lpVtbl->OnStatusChange(callback, &ext->download, &status);
            callback->lpVtbl->Release(callback);
        }
        if (ext)
            ext_release(ext);
    } while (InterlockedExchangeAdd(&job->notify_pending, -asked) != asked);
    if (com)
        CoUninitialize();
    JobRelease(job);
    return 0;
}

/* the status is taken when the change happens, so a delivery that runs late reports the change it was made for. */
void ExtNotify(DO_JOB *job)
{
    DO_DOWNLOAD_STATUS status;
    JobStatus(job, &status);
    AcquireSRWLockExclusive(&job->lock);
    job->notify_status = status;
    ReleaseSRWLockExclusive(&job->lock);
    if (InterlockedIncrement(&job->notify_pending) != 1)
        return;
    JobAddRef(job);
    if (!QueueUserWorkItem(deliver, job, WT_EXECUTELONGFUNCTION)) {
        job->notify_pending = 0;
        JobRelease(job);
    }
}
