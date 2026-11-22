#include "dosvc.h"
#include <mime/win32/bits.h>

#define DO_E_NO_ERROR_INFO_ ((HRESULT)0x80D02014L)

/*
 * the background copy interfaces of the service, for the callers that hand delivery optimization a bits job. the job is
 * a download of the job engine with no temporary file, and its file is added through IDeliveryOptimizationJob.
 */
typedef struct BITS_MANAGER {
    IBackgroundCopyManager iface;
    IUnknown inner;
    IUnknown *outer;
    LONG refs;
} BITS_MANAGER;

typedef struct BITS_JOB BITS_JOB;

typedef struct BITS_FILE {
    IBackgroundCopyFile5 iface;
    IDeliveryOptimizationFile file;
    IDeliveryOptimizationFile2 file2;
    LONG refs;
    DO_JOB *job;
} BITS_FILE;

struct BITS_JOB {
    IBackgroundCopyJob5 iface;
    IBackgroundCopyJobHttpOptions3 http;
    IBitsTokenOptions token;
    IDeliveryOptimizationJob dojob;
    IDeliveryOptimizationJob2 dojob2;
    LONG refs;
    DO_JOB *job;
    ULONG notify_flags;
    IUnknown *notify;
};

typedef struct BITS_ERROR {
    IBackgroundCopyError iface;
    LONG refs;
    HRESULT code;
} BITS_ERROR;

static HRESULT STDMETHODCALLTYPE not_implemented(void *self)
{
    return E_NOTIMPL;
}

#define NOTIMPL ((void *)not_implemented)

static WCHAR *task_copy(const WCHAR *text)
{
    if (!text)
        return nullptr;
    size_t size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = CoTaskMemAlloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static HRESULT job_ext_create(DO_JOB *job, BITS_JOB **out);

/* ------------------------------------------------------------------ the error */

static HRESULT STDMETHODCALLTYPE err_qi(IBackgroundCopyError *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyError)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE err_addref(IBackgroundCopyError *iface)
{
    return (ULONG)InterlockedIncrement(&((BITS_ERROR *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE err_release(IBackgroundCopyError *iface)
{
    LONG refs = InterlockedDecrement(&((BITS_ERROR *)iface)->refs);
    if (!refs)
        LocalFree(iface);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE err_get(IBackgroundCopyError *iface, BG_ERROR_CONTEXT *context, HRESULT *code)
{
    if (!context || !code)
        return E_POINTER;
    *context = BG_ERROR_CONTEXT_REMOTE_FILE;
    *code = ((BITS_ERROR *)iface)->code;
    return S_OK;
}

static const IBackgroundCopyErrorVtbl g_err_vtbl = {
    err_qi, err_addref, err_release, err_get, NOTIMPL, NOTIMPL, NOTIMPL, NOTIMPL,
};

/* ------------------------------------------------------------------ the file */

#define FILE_OF(iface, field) CONTAINING_RECORD(iface, BITS_FILE, field)

static HRESULT file_qi(BITS_FILE *f, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyFile) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyFile2) || IsEqualGUID(iid, &IID_IBackgroundCopyFile3) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyFile4) || IsEqualGUID(iid, &IID_IBackgroundCopyFile5))
        *out = &f->iface;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationFile))
        *out = &f->file;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationFile2))
        *out = &f->file2;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&f->refs);
    return S_OK;
}

static ULONG file_release(BITS_FILE *f)
{
    LONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        JobRelease(f->job);
        LocalFree(f);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE bf_qi(IBackgroundCopyFile5 *iface, REFIID iid, void **out)
{
    return file_qi(FILE_OF(iface, iface), iid, out);
}

static ULONG STDMETHODCALLTYPE bf_addref(IBackgroundCopyFile5 *iface)
{
    return (ULONG)InterlockedIncrement(&FILE_OF(iface, iface)->refs);
}

static ULONG STDMETHODCALLTYPE bf_release(IBackgroundCopyFile5 *iface)
{
    return file_release(FILE_OF(iface, iface));
}

static HRESULT STDMETHODCALLTYPE bf_remote(IBackgroundCopyFile5 *iface, LPWSTR *name)
{
    DO_JOB *job = FILE_OF(iface, iface)->job;
    if (!name)
        return E_POINTER;
    AcquireSRWLockShared(&job->lock);
    *name = task_copy(job->remote);
    ReleaseSRWLockShared(&job->lock);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE bf_local(IBackgroundCopyFile5 *iface, LPWSTR *name)
{
    DO_JOB *job = FILE_OF(iface, iface)->job;
    if (!name)
        return E_POINTER;
    AcquireSRWLockShared(&job->lock);
    *name = task_copy(job->local);
    ReleaseSRWLockShared(&job->lock);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE bf_progress(IBackgroundCopyFile5 *iface, BG_FILE_PROGRESS *progress)
{
    if (!progress)
        return E_POINTER;
    DO_DOWNLOAD_STATUS status;
    JobStatus(FILE_OF(iface, iface)->job, &status);
    progress->BytesTotal = status.BytesTotal ? status.BytesTotal : BG_SIZE_UNKNOWN;
    progress->BytesTransferred = status.BytesTransferred;
    progress->Completed = status.State == DODownloadState_Transferred || status.State == DODownloadState_Finalized;
    return S_OK;
}

static const IBackgroundCopyFile6Vtbl g_bf_vtbl = {
    bf_qi,   bf_addref, bf_release, bf_remote, bf_local, bf_progress, NOTIMPL, NOTIMPL,
    NOTIMPL, NOTIMPL,   NOTIMPL,    NOTIMPL,   NOTIMPL,  NOTIMPL,     NOTIMPL, NOTIMPL,
    NOTIMPL, NOTIMPL,
};

static HRESULT STDMETHODCALLTYPE df_qi(IDeliveryOptimizationFile *iface, REFIID iid, void **out)
{
    return file_qi(FILE_OF(iface, file), iid, out);
}

static ULONG STDMETHODCALLTYPE df_addref(IDeliveryOptimizationFile *iface)
{
    return (ULONG)InterlockedIncrement(&FILE_OF(iface, file)->refs);
}

static ULONG STDMETHODCALLTYPE df_release(IDeliveryOptimizationFile *iface)
{
    return file_release(FILE_OF(iface, file));
}

static HRESULT STDMETHODCALLTYPE df_id(IDeliveryOptimizationFile *iface, LPWSTR *id)
{
    if (!id)
        return E_POINTER;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = JobGetProperty(FILE_OF(iface, file)->job, P_CONTENT_ID, &v);
    *id = SUCCEEDED(hr) ? task_copy(V_BSTR(&v)) : nullptr;
    VariantClear(&v);
    return hr;
}

/* the statistics of a swarm: everything came from the origin server, nothing from peers. */
static HRESULT STDMETHODCALLTYPE df_stats(IDeliveryOptimizationFile *iface, DOSwarmStats *stats)
{
    if (!stats)
        return E_POINTER;
    memset(stats, 0, sizeof(*stats));
    DO_DOWNLOAD_STATUS status;
    JobStatus(FILE_OF(iface, file)->job, &status);
    stats->Bytes[0] = status.BytesTotal;
    stats->Bytes[1] = status.BytesTransferred;
    return S_OK;
}

static const IDeliveryOptimizationFileVtbl g_df_vtbl = {df_qi, df_addref, df_release, df_id, df_stats};

static HRESULT STDMETHODCALLTYPE df2_qi(IDeliveryOptimizationFile2 *iface, REFIID iid, void **out)
{
    return file_qi(FILE_OF(iface, file2), iid, out);
}

static ULONG STDMETHODCALLTYPE df2_addref(IDeliveryOptimizationFile2 *iface)
{
    return (ULONG)InterlockedIncrement(&FILE_OF(iface, file2)->refs);
}

static ULONG STDMETHODCALLTYPE df2_release(IDeliveryOptimizationFile2 *iface)
{
    return file_release(FILE_OF(iface, file2));
}

static HRESULT STDMETHODCALLTYPE df2_get(IDeliveryOptimizationFile2 *iface, DeliveryOptimizationFileProperty id,
                                         VARIANT *value)
{
    if (!value)
        return E_POINTER;
    VariantInit(value);
    return DO_E_UNKNOWN_PROPERTY_ID;
}

static HRESULT STDMETHODCALLTYPE df2_set(IDeliveryOptimizationFile2 *iface, DeliveryOptimizationFileProperty id,
                                         const VARIANT *value)
{
    return DO_E_UNKNOWN_PROPERTY_ID;
}

static const IDeliveryOptimizationFile2Vtbl g_df2_vtbl = {df2_qi, df2_addref, df2_release, df2_get, df2_set};

static HRESULT file_create(DO_JOB *job, BITS_FILE **out)
{
    BITS_FILE *f = LocalAlloc(LMEM_ZEROINIT, sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->iface.lpVtbl = &g_bf_vtbl;
    f->file.lpVtbl = &g_df_vtbl;
    f->file2.lpVtbl = &g_df2_vtbl;
    f->refs = 1;
    f->job = job;
    JobAddRef(job);
    *out = f;
    return S_OK;
}

/* ------------------------------------------------------------------ the job */

#define JOB_OF(iface, field) CONTAINING_RECORD(iface, BITS_JOB, field)

static HRESULT job_qi(BITS_JOB *j, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyJob) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyJob2) || IsEqualGUID(iid, &IID_IBackgroundCopyJob3) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyJob4) || IsEqualGUID(iid, &IID_IBackgroundCopyJob5))
        *out = &j->iface;
    else if (IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions) ||
             IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions2) ||
             IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions3))
        *out = &j->http;
    else if (IsEqualGUID(iid, &IID_IBitsTokenOptions))
        *out = &j->token;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationJob))
        *out = &j->dojob;
    else if (IsEqualGUID(iid, &IID_IDeliveryOptimizationJob2))
        *out = &j->dojob2;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&j->refs);
    return S_OK;
}

static ULONG job_release(BITS_JOB *j)
{
    LONG refs = InterlockedDecrement(&j->refs);
    if (!refs) {
        if (j->notify)
            j->notify->lpVtbl->Release(j->notify);
        JobRelease(j->job);
        LocalFree(j);
        DoLockService(FALSE);
    }
    return (ULONG)refs;
}

#define JOB_IUNKNOWN(prefix, type, field)                                                                              \
    static HRESULT STDMETHODCALLTYPE prefix##_qi(type *iface, REFIID iid, void **out)                                  \
    {                                                                                                                  \
        return job_qi(JOB_OF(iface, field), iid, out);                                                                 \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_addref(type *iface)                                                        \
    {                                                                                                                  \
        return (ULONG)InterlockedIncrement(&JOB_OF(iface, field)->refs);                                               \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_release(type *iface)                                                       \
    {                                                                                                                  \
        return job_release(JOB_OF(iface, field));                                                                     \
    }

JOB_IUNKNOWN(bj, IBackgroundCopyJob5, iface)

#define J(iface) (JOB_OF(iface, iface)->job)

static HRESULT STDMETHODCALLTYPE bj_enum_files(IBackgroundCopyJob5 *iface, IEnumBackgroundCopyFiles **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_suspend(IBackgroundCopyJob5 *iface)
{
    return JobPause(J(iface));
}

static HRESULT STDMETHODCALLTYPE bj_resume(IBackgroundCopyJob5 *iface)
{
    return JobStart(J(iface));
}

static HRESULT STDMETHODCALLTYPE bj_cancel(IBackgroundCopyJob5 *iface)
{
    return JobAbort(J(iface));
}

static HRESULT STDMETHODCALLTYPE bj_complete(IBackgroundCopyJob5 *iface)
{
    return JobFinalize(J(iface));
}

static HRESULT STDMETHODCALLTYPE bj_id(IBackgroundCopyJob5 *iface, GUID *id)
{
    if (!id)
        return E_POINTER;
    *id = J(iface)->id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_type(IBackgroundCopyJob5 *iface, BG_JOB_TYPE *type)
{
    if (!type)
        return E_POINTER;
    *type = BG_JOB_TYPE_DOWNLOAD;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_progress(IBackgroundCopyJob5 *iface, BG_JOB_PROGRESS *progress)
{
    if (!progress)
        return E_POINTER;
    DO_DOWNLOAD_STATUS status;
    JobStatus(J(iface), &status);
    AcquireSRWLockShared(&J(iface)->lock);
    BOOL file = J(iface)->has_file || J(iface)->remote;
    ReleaseSRWLockShared(&J(iface)->lock);
    progress->BytesTotal = status.BytesTotal ? status.BytesTotal : BG_SIZE_UNKNOWN;
    progress->BytesTransferred = status.BytesTransferred;
    progress->FilesTotal = file ? 1 : 0;
    progress->FilesTransferred = status.State == DODownloadState_Transferred || status.State == DODownloadState_Finalized;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_times(IBackgroundCopyJob5 *iface, BG_JOB_TIMES *times)
{
    if (!times)
        return E_POINTER;
    memset(times, 0, sizeof(*times));
    times->CreationTime = J(iface)->created;
    times->ModificationTime = J(iface)->created;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_state(IBackgroundCopyJob5 *iface, BG_JOB_STATE *state)
{
    static const BG_JOB_STATE map[] = {
        BG_JOB_STATE_QUEUED,    BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_TRANSFERRED, BG_JOB_STATE_ACKNOWLEDGED,
        BG_JOB_STATE_CANCELLED, BG_JOB_STATE_SUSPENDED,    BG_JOB_STATE_ERROR,       BG_JOB_STATE_TRANSIENT_ERROR,
    };
    if (!state)
        return E_POINTER;
    int s = J(iface)->state;
    *state = s >= 0 && s < (int)ARRAYSIZE(map) ? map[s] : BG_JOB_STATE_CANCELLED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_error(IBackgroundCopyJob5 *iface, IBackgroundCopyError **error)
{
    if (!error)
        return E_POINTER;
    *error = nullptr;
    DO_DOWNLOAD_STATUS status;
    JobStatus(J(iface), &status);
    HRESULT code = status.Error ? status.Error : status.ExtendedError;
    if (SUCCEEDED(code))
        return DO_E_NO_ERROR_INFO_;
    BITS_ERROR *e = LocalAlloc(LMEM_ZEROINIT, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &g_err_vtbl;
    e->refs = 1;
    e->code = code;
    *error = &e->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_owner(IBackgroundCopyJob5 *iface, LPWSTR *owner)
{
    if (owner)
        *owner = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_name(IBackgroundCopyJob5 *iface, LPWSTR *name)
{
    if (!name)
        return E_POINTER;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = JobGetProperty(J(iface), P_DISPLAY_NAME, &v);
    *name = task_copy(SUCCEEDED(hr) ? V_BSTR(&v) : L"");
    VariantClear(&v);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT set_u4(DO_JOB *job, int id, ULONG value)
{
    VARIANT v;
    V_VT(&v) = VT_UI4;
    V_UI4(&v) = value;
    return JobSetProperty(job, id, &v);
}

static HRESULT get_u4(DO_JOB *job, int id, ULONG *value)
{
    if (!value)
        return E_POINTER;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = JobGetProperty(job, id, &v);
    *value = SUCCEEDED(hr) && V_VT(&v) == VT_UI4 ? V_UI4(&v) : 0;
    return hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE bj_set_priority(IBackgroundCopyJob5 *iface, BG_JOB_PRIORITY priority)
{
    VARIANT v;
    V_VT(&v) = VT_BOOL;
    V_BOOL(&v) = priority == BG_JOB_PRIORITY_FOREGROUND ? VARIANT_TRUE : VARIANT_FALSE;
    return JobSetProperty(J(iface), P_FOREGROUND, &v);
}

static HRESULT STDMETHODCALLTYPE bj_priority(IBackgroundCopyJob5 *iface, BG_JOB_PRIORITY *priority)
{
    if (!priority)
        return E_POINTER;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = JobGetProperty(J(iface), P_FOREGROUND, &v);
    *priority = SUCCEEDED(hr) && V_BOOL(&v) == VARIANT_TRUE ? BG_JOB_PRIORITY_FOREGROUND : BG_JOB_PRIORITY_NORMAL;
    return hr;
}

static HRESULT STDMETHODCALLTYPE bj_set_flags(IBackgroundCopyJob5 *iface, ULONG flags)
{
    JOB_OF(iface, iface)->notify_flags = flags & 0xB;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_flags(IBackgroundCopyJob5 *iface, ULONG *flags)
{
    if (!flags)
        return E_POINTER;
    *flags = JOB_OF(iface, iface)->notify_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_set_notify(IBackgroundCopyJob5 *iface, IUnknown *notify)
{
    BITS_JOB *j = JOB_OF(iface, iface);
    if (notify)
        notify->lpVtbl->AddRef(notify);
    IUnknown *old = InterlockedExchangePointer((void **)&j->notify, notify);
    if (old)
        old->lpVtbl->Release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_notify(IBackgroundCopyJob5 *iface, IUnknown **notify)
{
    if (!notify)
        return E_POINTER;
    *notify = JOB_OF(iface, iface)->notify;
    if (*notify)
        (*notify)->lpVtbl->AddRef(*notify);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_set_retry(IBackgroundCopyJob5 *iface, ULONG seconds)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_set_no_progress(IBackgroundCopyJob5 *iface, ULONG seconds)
{
    return set_u4(J(iface), P_NO_PROGRESS, seconds);
}

static HRESULT STDMETHODCALLTYPE bj_no_progress(IBackgroundCopyJob5 *iface, ULONG *seconds)
{
    return get_u4(J(iface), P_NO_PROGRESS, seconds);
}

static HRESULT STDMETHODCALLTYPE bj_error_count(IBackgroundCopyJob5 *iface, ULONG *errors)
{
    if (!errors)
        return E_POINTER;
    *errors = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bj_proxy(IBackgroundCopyJob5 *iface, BG_JOB_PROXY_USAGE *usage, LPWSTR *list,
                                          LPWSTR *bypass)
{
    if (list)
        *list = nullptr;
    if (bypass)
        *bypass = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_out2(IBackgroundCopyJob5 *iface, LPWSTR *a, LPWSTR *b)
{
    if (a)
        *a = nullptr;
    if (b)
        *b = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_out1(IBackgroundCopyJob5 *iface, void **a)
{
    if (a)
        *a = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_reply_data(IBackgroundCopyJob5 *iface, BYTE **buffer, UINT64 *length)
{
    if (buffer)
        *buffer = nullptr;
    if (length)
        *length = 0;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE bj_elevation(IBackgroundCopyJob5 *iface, BOOL *elevated)
{
    return E_NOTIMPL;
}

static const IBackgroundCopyJob5Vtbl g_bj_vtbl = {
    bj_qi,           bj_addref,        bj_release,      NOTIMPL,         NOTIMPL,         bj_enum_files,
    bj_suspend,      bj_resume,        bj_cancel,       bj_complete,     bj_id,           bj_type,
    bj_progress,     bj_times,         bj_state,        bj_error,        bj_owner,        NOTIMPL,
    bj_name,         NOTIMPL,          (void *)bj_out1, bj_set_priority, bj_priority,     bj_set_flags,
    bj_flags,        bj_set_notify,    bj_notify,       bj_set_retry,    NOTIMPL,         bj_set_no_progress,
    bj_no_progress,  bj_error_count,   NOTIMPL,         bj_proxy,        NOTIMPL,         NOTIMPL,
    bj_out2,         NOTIMPL,          bj_reply_data,   NOTIMPL,         (void *)bj_out1, NOTIMPL,
    NOTIMPL,         NOTIMPL,          NOTIMPL,         NOTIMPL,         NOTIMPL,         NOTIMPL,
    NOTIMPL,         NOTIMPL,          bj_elevation,    NOTIMPL,         NOTIMPL,         NOTIMPL,
    NOTIMPL,
};

/* ------------------------------------------------------------------ http options and token options */

JOB_IUNKNOWN(ho, IBackgroundCopyJobHttpOptions3, http)

static HRESULT set_str(DO_JOB *job, int id, const WCHAR *text)
{
    VARIANT v;
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(text ? text : L"");
    HRESULT hr = V_BSTR(&v) ? JobSetProperty(job, id, &v) : E_OUTOFMEMORY;
    VariantClear(&v);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ho_set_headers(IBackgroundCopyJobHttpOptions3 *iface, LPCWSTR headers)
{
    return set_str(JOB_OF(iface, http)->job, P_HEADERS, headers);
}

static HRESULT STDMETHODCALLTYPE ho_headers(IBackgroundCopyJobHttpOptions3 *iface, LPWSTR *headers)
{
    if (!headers)
        return E_POINTER;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = JobGetProperty(JOB_OF(iface, http)->job, P_HEADERS, &v);
    *headers = SUCCEEDED(hr) ? task_copy(V_BSTR(&v)) : nullptr;
    VariantClear(&v);
    return hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? S_FALSE : hr;
}

static HRESULT STDMETHODCALLTYPE ho_set_security(IBackgroundCopyJobHttpOptions3 *iface, ULONG flags)
{
    return set_u4(JOB_OF(iface, http)->job, P_SECURITY_FLAGS, flags);
}

static HRESULT STDMETHODCALLTYPE ho_security(IBackgroundCopyJobHttpOptions3 *iface, ULONG *flags)
{
    return get_u4(JOB_OF(iface, http)->job, P_SECURITY_FLAGS, flags);
}

static HRESULT STDMETHODCALLTYPE ho_cert(IBackgroundCopyJobHttpOptions3 *iface, BG_CERT_STORE_LOCATION *location,
                                         LPWSTR *store, BYTE **hash, LPWSTR *subject)
{
    if (store)
        *store = nullptr;
    if (hash)
        *hash = nullptr;
    if (subject)
        *subject = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ho_method(IBackgroundCopyJobHttpOptions3 *iface, LPWSTR *method)
{
    if (method)
        *method = nullptr;
    return E_NOTIMPL;
}

static const IBackgroundCopyJobHttpOptions3Vtbl g_ho_vtbl = {
    ho_qi,          ho_addref,  ho_release,      NOTIMPL,     NOTIMPL, NOTIMPL,   ho_cert, ho_set_headers,
    ho_headers,     ho_set_security, ho_security, NOTIMPL,    ho_method, NOTIMPL, NOTIMPL,
};

JOB_IUNKNOWN(to, IBitsTokenOptions, token)

static HRESULT STDMETHODCALLTYPE to_set_flags(IBitsTokenOptions *iface, DWORD flags)
{
    return flags == 2 ? S_OK : E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE to_flags(IBitsTokenOptions *iface, DWORD *flags)
{
    if (!flags)
        return E_POINTER;
    *flags = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE to_set_token(IBitsTokenOptions *iface)
{
    VARIANT v;
    V_VT(&v) = VT_BOOL;
    V_BOOL(&v) = VARIANT_TRUE;
    return JobSetProperty(JOB_OF(iface, token)->job, P_NETWORK_TOKEN, &v);
}

static HRESULT STDMETHODCALLTYPE to_clear(IBitsTokenOptions *iface)
{
    VARIANT v;
    V_VT(&v) = VT_BOOL;
    V_BOOL(&v) = VARIANT_FALSE;
    return JobSetProperty(JOB_OF(iface, token)->job, P_NETWORK_TOKEN, &v);
}

static HRESULT STDMETHODCALLTYPE to_sid(IBitsTokenOptions *iface, LPWSTR *sid)
{
    if (!sid)
        return E_POINTER;
    *sid = nullptr;
    return S_OK;
}

static const IBitsTokenOptionsVtbl g_to_vtbl = {to_qi, to_addref, to_release, to_set_flags, to_flags, to_set_token,
                                                to_clear, to_sid};

/* ------------------------------------------------------------------ IDeliveryOptimizationJob and Job2 */

JOB_IUNKNOWN(dj, IDeliveryOptimizationJob, dojob)

/* the one file of the job: its url, where it goes and the ranges of it that are wanted. */
static HRESULT add_file(DO_JOB *job, const WCHAR *remote, const WCHAR *local, ULONG count, const BG_FILE_RANGE *ranges)
{
    if (!remote)
        return E_INVALIDARG;
    AcquireSRWLockShared(&job->lock);
    BOOL taken = V_VT(&job->props[P_URI]) != VT_EMPTY;
    ReleaseSRWLockShared(&job->lock);
    if (taken)
        return BG_E_TOO_MANY_FILES;
    HRESULT hr = set_str(job, P_URI, remote);
    if (SUCCEEDED(hr) && local)
        hr = set_str(job, P_LOCAL_PATH, local);
    if (SUCCEEDED(hr) && count) {
        if (!ranges)
            return E_INVALIDARG;
        DO_DOWNLOAD_RANGE *copy = LocalAlloc(LMEM_FIXED, count * sizeof(*copy));
        if (!copy)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < count; i++) {
            copy[i].Offset = ranges[i].InitialOffset;
            copy[i].Length = ranges[i].Length;
        }
        hr = JobSetRanges(job, copy, count);
        LocalFree(copy);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE dj_add(IDeliveryOptimizationJob *iface, const WCHAR *remote, const WCHAR *id,
                                        const WCHAR *local, ULONG count, BG_FILE_RANGE *ranges, UINT64 size)
{
    DO_JOB *job = JOB_OF(iface, dojob)->job;
    HRESULT hr = add_file(job, remote, local, count, ranges);
    if (SUCCEEDED(hr) && id && *id)
        hr = set_str(job, P_CONTENT_ID, id);
    if (SUCCEEDED(hr) && size && size != BG_SIZE_UNKNOWN) {
        VARIANT v;
        V_VT(&v) = VT_UI8;
        V_UI8(&v) = size;
        hr = JobSetProperty(job, P_TOTAL_SIZE, &v);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE dj_update(IDeliveryOptimizationJob *iface, const WCHAR *id)
{
    return set_str(JOB_OF(iface, dojob)->job, P_UPDATE_ID, id);
}

static HRESULT STDMETHODCALLTYPE dj_flags(IDeliveryOptimizationJob *iface, ULONG flags)
{
    return flags & ~1u ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE dj_cv(IDeliveryOptimizationJob *iface, const char *cv)
{
    if (!cv)
        return E_INVALIDARG;
    WCHAR text[128];
    int n = MultiByteToWideChar(CP_UTF8, 0, cv, -1, text, ARRAYSIZE(text));
    if (!n)
        return E_INVALIDARG;
    return set_str(JOB_OF(iface, dojob)->job, P_CV, text);
}

static const IDeliveryOptimizationJobVtbl g_dj_vtbl = {dj_qi, dj_addref, dj_release, dj_add, dj_update, NOTIMPL, dj_flags,
                                                       dj_cv};

JOB_IUNKNOWN(dj2, IDeliveryOptimizationJob2, dojob2)

static HRESULT STDMETHODCALLTYPE dj2_add(IDeliveryOptimizationJob2 *iface, const WCHAR *remote, const WCHAR *local,
                                         ULONG count, BG_FILE_RANGE *ranges, REFIID iid, void **file)
{
    if (!file)
        return E_POINTER;
    *file = nullptr;
    DO_JOB *job = JOB_OF(iface, dojob2)->job;
    HRESULT hr = add_file(job, remote, local, count, ranges);
    if (FAILED(hr))
        return hr;
    BITS_FILE *f;
    hr = file_create(job, &f);
    if (FAILED(hr))
        return hr;
    hr = file_qi(f, iid, file);
    file_release(f);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dj2_get(IDeliveryOptimizationJob2 *iface, DeliveryOptimizationJobProperty id,
                                         VARIANT *value)
{
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if (id)
        return DO_E_UNKNOWN_PROPERTY_ID;
    DO_DOWNLOAD_STATUS status;
    JobStatus(JOB_OF(iface, dojob2)->job, &status);
    V_VT(value) = VT_I4;
    V_I4(value) = status.ExtendedError;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dj2_set(IDeliveryOptimizationJob2 *iface, DeliveryOptimizationJobProperty id,
                                         const VARIANT *value)
{
    return id ? DO_E_UNKNOWN_PROPERTY_ID : DO_E_READ_ONLY_PROPERTY;
}

static const IDeliveryOptimizationJob2Vtbl g_dj2_vtbl = {dj2_qi, dj2_addref, dj2_release, dj2_add, dj2_get, dj2_set};

static HRESULT job_ext_create(DO_JOB *job, BITS_JOB **out)
{
    BITS_JOB *j = LocalAlloc(LMEM_ZEROINIT, sizeof(*j));
    if (!j)
        return E_OUTOFMEMORY;
    j->iface.lpVtbl = &g_bj_vtbl;
    j->http.lpVtbl = &g_ho_vtbl;
    j->token.lpVtbl = &g_to_vtbl;
    j->dojob.lpVtbl = &g_dj_vtbl;
    j->dojob2.lpVtbl = &g_dj2_vtbl;
    j->refs = 1;
    j->job = job;
    j->notify_flags = BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
    JobAddRef(job);
    DoLockService(TRUE);
    *out = j;
    return S_OK;
}

/* ------------------------------------------------------------------ the manager, aggregated in the one of IDOManager */

static HRESULT STDMETHODCALLTYPE inner_qi(IUnknown *iface, REFIID iid, void **out)
{
    BITS_MANAGER *m = CONTAINING_RECORD(iface, BITS_MANAGER, inner);
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = &m->inner;
    else if (IsEqualGUID(iid, &IID_IBackgroundCopyManager))
        *out = &m->iface;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE inner_addref(IUnknown *iface)
{
    return (ULONG)InterlockedIncrement(&CONTAINING_RECORD(iface, BITS_MANAGER, inner)->refs);
}

static ULONG STDMETHODCALLTYPE inner_release(IUnknown *iface)
{
    BITS_MANAGER *m = CONTAINING_RECORD(iface, BITS_MANAGER, inner);
    LONG refs = InterlockedDecrement(&m->refs);
    if (!refs)
        LocalFree(m);
    return (ULONG)refs;
}

static const IUnknownVtbl g_inner_vtbl = {inner_qi, inner_addref, inner_release};

static HRESULT STDMETHODCALLTYPE bm_qi(IBackgroundCopyManager *iface, REFIID iid, void **out)
{
    BITS_MANAGER *m = CONTAINING_RECORD(iface, BITS_MANAGER, iface);
    return m->outer->lpVtbl->QueryInterface(m->outer, iid, out);
}

static ULONG STDMETHODCALLTYPE bm_addref(IBackgroundCopyManager *iface)
{
    BITS_MANAGER *m = CONTAINING_RECORD(iface, BITS_MANAGER, iface);
    return m->outer->lpVtbl->AddRef(m->outer);
}

static ULONG STDMETHODCALLTYPE bm_release(IBackgroundCopyManager *iface)
{
    BITS_MANAGER *m = CONTAINING_RECORD(iface, BITS_MANAGER, iface);
    return m->outer->lpVtbl->Release(m->outer);
}

static HRESULT STDMETHODCALLTYPE bm_create(IBackgroundCopyManager *iface, LPCWSTR name, BG_JOB_TYPE type, GUID *id,
                                           IBackgroundCopyJob **out)
{
    if (!id || !out)
        return E_POINTER;
    *out = nullptr;
    if (type != BG_JOB_TYPE_DOWNLOAD)
        return E_NOTIMPL;
    DO_JOB *job;
    HRESULT hr = JobCreate(&job);
    if (FAILED(hr))
        return hr;
    job->bits = TRUE;
    if (name && *name)
        set_str(job, P_DISPLAY_NAME, name);
    BITS_JOB *j;
    hr = job_ext_create(job, &j);
    if (SUCCEEDED(hr)) {
        *id = job->id;
        *out = (IBackgroundCopyJob *)&j->iface;
    } else
        JobAbort(job);
    JobRelease(job);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bm_get(IBackgroundCopyManager *iface, REFGUID id, IBackgroundCopyJob **out)
{
    if (!id || !out)
        return E_POINTER;
    *out = nullptr;
    DO_JOB *found = nullptr;
    JobListLock(FALSE);
    for (DO_JOB *job = JobList(); job; job = job->next) {
        if (IsEqualGUID(&job->id, id)) {
            found = job;
            JobAddRef(job);
            break;
        }
    }
    JobListUnlock(FALSE);
    if (!found)
        return DO_E_JOB_NOT_FOUND;
    HRESULT hr = E_ACCESSDENIED;
    if (JobVisible(found)) {
        BITS_JOB *j;
        hr = job_ext_create(found, &j);
        if (SUCCEEDED(hr))
            *out = (IBackgroundCopyJob *)&j->iface;
    }
    JobRelease(found);
    return hr;
}

static HRESULT STDMETHODCALLTYPE bm_enum(IBackgroundCopyManager *iface, DWORD flags, IEnumBackgroundCopyJobs **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static const IBackgroundCopyManagerVtbl g_bm_vtbl = {bm_qi, bm_addref, bm_release, bm_create, bm_get, bm_enum, NOTIMPL};

HRESULT BitsManagerCreate(IUnknown *outer, IUnknown **out)
{
    BITS_MANAGER *m = LocalAlloc(LMEM_ZEROINIT, sizeof(*m));
    *out = nullptr;
    if (!m)
        return E_OUTOFMEMORY;
    m->iface.lpVtbl = &g_bm_vtbl;
    m->inner.lpVtbl = &g_inner_vtbl;
    m->outer = outer;
    m->refs = 1;
    *out = &m->inner;
    return S_OK;
}
