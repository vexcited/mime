#include "qmgr.h"

typedef struct QMGR_EXT {
    IBackgroundCopyQMgr iface;
    LONG refs;
} QMGR_EXT;

/* BITS 1.0 on top of the jobs of today: a group is a download job, its one IBackgroundCopyJob1 a view of it. */
typedef struct GROUP_EXT {
    IBackgroundCopyGroup iface;
    LONG refs;
    BJOB *job;
} GROUP_EXT;

typedef struct JOB1_EXT {
    IBackgroundCopyJob1 iface;
    LONG refs;
    BJOB *job;
} JOB1_EXT;

typedef struct CALL {
    CALLER caller;
    BOOL exclusive;
} CALL;

static HRESULT begin(BJOB *job, DWORD access, CALL *call)
{
    call->exclusive = access != BITS_READ;
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = CallerOpen(&call->caller);
    if (FAILED(hr)) {
        QmLeave();
        return hr;
    }
    if (call->exclusive)
        Lock();
    else
        LockShared();
    if (job) {
        hr = JobCheckAccess(job, &call->caller, access, nullptr);
        if (FAILED(hr)) {
            if (call->exclusive)
                Unlock();
            else
                UnlockShared();
            CallerClose(&call->caller);
            QmLeave();
        }
    }
    return hr;
}

static HRESULT end(CALL *call, HRESULT hr)
{
    if (call->exclusive)
        Unlock();
    else
        UnlockShared();
    CallerClose(&call->caller);
    QmLeave();
    return hr;
}

/* MapOldNotifyToNewNotify: QM_NOTIFY_GROUP_DONE and QM_NOTIFY_DISABLE_NOTIFY only, errors are always told. */
static HRESULT map_notify(DWORD old, ULONG *flags)
{
    if (old & ~(DWORD)(QM_NOTIFY_GROUP_DONE | QM_NOTIFY_DISABLE_NOTIFY))
        return E_NOTIMPL;
    *flags = BG_NOTIFY_JOB_ERROR | (old & QM_NOTIFY_GROUP_DONE ? BG_NOTIFY_JOB_TRANSFERRED : 0) |
             (old & QM_NOTIFY_DISABLE_NOTIFY ? BG_NOTIFY_DISABLE : 0);
    return S_OK;
}

/* the percentage of the bytes, or the bytes done, of COldGroupInterface and COldJobInterface::GetProgress. */
static HRESULT old_progress(BJOB *job, DWORD flags, DWORD *value)
{
    BG_JOB_PROGRESS p;
    JobProgress(job, &p);
    *value = 0;
    if (flags == QM_PROGRESS_PERCENT_DONE) {
        if (p.BytesTotal != BG_SIZE_UNKNOWN && p.BytesTransferred != BG_SIZE_UNKNOWN && p.BytesTotal)
            *value = (DWORD)(p.BytesTransferred * 100 / p.BytesTotal);
        return S_OK;
    }
    if (flags == QM_PROGRESS_SIZE_DONE) {
        *value = p.BytesTransferred == BG_SIZE_UNKNOWN ? 0 : (DWORD)p.BytesTransferred;
        return S_OK;
    }
    return E_NOTIMPL;
}

static HRESULT group_for(BJOB *job, GROUP_EXT **out);
static HRESULT job1_for(BJOB *job, JOB1_EXT **out);

/* ------------------------------------------------------------------ IBackgroundCopyJob1 */

#define J1(iface) CONTAINING_RECORD(iface, JOB1_EXT, iface)

static HRESULT STDMETHODCALLTYPE j1_qi(IBackgroundCopyJob1 *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyJob1)) {
        *out = iface;
        InterlockedIncrement(&J1(iface)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE j1_addref(IBackgroundCopyJob1 *iface)
{
    return (ULONG)InterlockedIncrement(&J1(iface)->refs);
}

static ULONG STDMETHODCALLTYPE j1_release(IBackgroundCopyJob1 *iface)
{
    JOB1_EXT *j = J1(iface);
    LONG refs = InterlockedDecrement(&j->refs);
    if (refs)
        return (ULONG)refs;
    InterlockedCompareExchangePointer(&j->job->job1_ext, nullptr, j);
    JobRelease(j->job);
    LocalFree(j);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE j1_cancel(IBackgroundCopyJob1 *iface)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    QmLeave();
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE j1_progress(IBackgroundCopyJob1 *iface, DWORD flags, DWORD *progress)
{
    CALL call;
    if (!progress)
        return E_INVALIDARG;
    *progress = 0;
    HRESULT hr = begin(J1(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, old_progress(J1(iface)->job, flags, progress));
}

/* COldJobInterface::GetStatus: JOB_INCOMPLETE while it runs or waits, JOB_ERROR with the old errors, JOB_COMPLETE after. */
static HRESULT STDMETHODCALLTYPE j1_status(IBackgroundCopyJob1 *iface, DWORD *status, DWORD *win32, DWORD *transport,
                                           DWORD *retries)
{
    CALL call;
    if (!status || !win32 || !transport || !retries)
        return E_INVALIDARG;
    *status = *win32 = *transport = *retries = 0;
    HRESULT hr = begin(J1(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = J1(iface)->job;
    *retries = job->error_count;
    if (job->priority == BG_JOB_PRIORITY_FOREGROUND)
        *status |= QM_STATUS_JOB_FOREGROUND;
    switch (job->state) {
    case BG_JOB_STATE_ERROR:
        *status |= QM_STATUS_JOB_ERROR;
        if (job->error.present) {
            if (job->error.source == SOURCE_LEGACY)
                *win32 = (DWORD)job->error.code;
            else if (job->error.source == SOURCE_WIN32 && ((DWORD)job->error.code & 0xFFFF0000) == 0x80070000)
                *win32 = (DWORD)job->error.code & 0xFFFF;
            if (job->error.context == BG_ERROR_CONTEXT_REMOTE_FILE)
                *transport = (DWORD)job->error.code;
        }
        break;
    case BG_JOB_STATE_TRANSFERRED:
    case BG_JOB_STATE_ACKNOWLEDGED:
        *status |= QM_STATUS_JOB_COMPLETE;
        break;
    case BG_JOB_STATE_CANCELLED:
        break;
    default:
        *status |= QM_STATUS_JOB_INCOMPLETE;
    }
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j1_add(IBackgroundCopyJob1 *iface, ULONG count, FILESETINFO **files)
{
    CALL call;
    if (count && !files)
        return E_INVALIDARG;
    HRESULT hr = begin(J1(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BG_FILE_INFO *list = LocalAlloc(LMEM_FIXED, (count ? count : 1) * sizeof(*list));
    if (!list)
        return end(&call, E_OUTOFMEMORY);
    for (ULONG i = 0; i < count && SUCCEEDED(hr); i++) {
        if (!files[i])
            hr = E_INVALIDARG;
        else {
            list[i].RemoteName = files[i]->bstrRemoteFile;
            list[i].LocalName = files[i]->bstrLocalFile;
        }
    }
    if (SUCCEEDED(hr))
        hr = JobAddFileSet(J1(iface)->job, &call.caller, count, list);
    LocalFree(list);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j1_file(IBackgroundCopyJob1 *iface, ULONG index, FILESETINFO *info)
{
    CALL call;
    if (!info)
        return E_INVALIDARG;
    memset(info, 0, sizeof(*info));
    HRESULT hr = begin(J1(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = J1(iface)->job;
    if (index >= job->file_count)
        return end(&call, QM_E_ITEM_NOT_FOUND);
    BFILE *file = job->files[index];
    info->bstrRemoteFile = SysAllocString(file->remote);
    info->bstrLocalFile = SysAllocString(file->local);
    if (!info->bstrRemoteFile || !info->bstrLocalFile) {
        SysFreeString(info->bstrRemoteFile);
        SysFreeString(info->bstrLocalFile);
        memset(info, 0, sizeof(*info));
        return end(&call, E_OUTOFMEMORY);
    }
    info->dwSizeHint = file->total == BG_SIZE_UNKNOWN ? 0 : (DWORD)file->total;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j1_count(IBackgroundCopyJob1 *iface, DWORD *count)
{
    CALL call;
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    HRESULT hr = begin(J1(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *count = J1(iface)->job->file_count;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j1_foreground(IBackgroundCopyJob1 *iface)
{
    CALL call;
    HRESULT hr = begin(J1(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, E_NOTIMPL);
}

static HRESULT STDMETHODCALLTYPE j1_id(IBackgroundCopyJob1 *iface, GUID *id)
{
    CALL call;
    if (!id)
        return E_INVALIDARG;
    memset(id, 0, sizeof(*id));
    HRESULT hr = begin(J1(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *id = J1(iface)->job->legacy_job_id;
    return end(&call, S_OK);
}

static const IBackgroundCopyJob1Vtbl g_job1_vtbl = {j1_qi,  j1_addref, j1_release, j1_cancel,     j1_progress, j1_status,
                                                    j1_add, j1_file,   j1_count,   j1_foreground, j1_id};

/* ------------------------------------------------------------------ IBackgroundCopyGroup */

#define GX(iface) CONTAINING_RECORD(iface, GROUP_EXT, iface)

static HRESULT STDMETHODCALLTYPE g_qi(IBackgroundCopyGroup *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyGroup)) {
        *out = iface;
        InterlockedIncrement(&GX(iface)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE g_addref(IBackgroundCopyGroup *iface)
{
    return (ULONG)InterlockedIncrement(&GX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE g_release(IBackgroundCopyGroup *iface)
{
    GROUP_EXT *g = GX(iface);
    LONG refs = InterlockedDecrement(&g->refs);
    if (refs)
        return (ULONG)refs;
    InterlockedCompareExchangePointer(&g->job->group_ext, nullptr, g);
    JobRelease(g->job);
    LocalFree(g);
    QmObjectRelease();
    return 0;
}

static HRESULT bstr_variant(VARIANT *value, const WCHAR *text)
{
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(text ? text : L"");
    if (V_BSTR(value))
        return S_OK;
    V_VT(value) = VT_EMPTY;
    return E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE g_get_prop(IBackgroundCopyGroup *iface, GROUPPROP id, VARIANT *value)
{
    CALL call;
    if (!value)
        return E_INVALIDARG;
    VariantInit(value);
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    switch (id) {
    case GROUPPROP_PRIORITY:
    case GROUPPROP_PROTOCOLFLAGS:
        V_VT(value) = VT_INT;
        V_INT(value) = 1;
        break;
    case GROUPPROP_NOTIFYFLAGS:
        V_VT(value) = VT_INT;
        V_INT(value) = (job->notify_flags & BG_NOTIFY_JOB_TRANSFERRED ? QM_NOTIFY_GROUP_DONE : 0) |
                       (job->notify_flags & BG_NOTIFY_DISABLE ? QM_NOTIFY_DISABLE_NOTIFY : 0);
        break;
    case GROUPPROP_NOTIFYCLSID: {
        WCHAR *text = nullptr;
        hr = StringFromIID(&job->legacy_clsid, &text);
        if (SUCCEEDED(hr))
            hr = bstr_variant(value, text);
        CoTaskMemFree(text);
        break;
    }
    case GROUPPROP_DISPLAYNAME:
        hr = bstr_variant(value, job->name);
        break;
    case GROUPPROP_DESCRIPTION:
        hr = bstr_variant(value, job->description);
        break;
    default:
        hr = E_NOTIMPL;
    }
    return end(&call, hr);
}

static HRESULT set_limited(WCHAR **slot, const WCHAR *text, size_t max)
{
    if (!text)
        return E_INVALIDARG;
    if (wcslen(text) > max)
        return BG_E_STRING_TOO_LONG;
    WCHAR *copy = QmDup(text);
    if (!copy)
        return E_OUTOFMEMORY;
    LocalFree(*slot);
    *slot = copy;
    return S_OK;
}

/* COldGroupInterface::SetPropInternal: the integer types and BSTR, a negative integer reads as 0xFFFFFFFF. */
static HRESULT STDMETHODCALLTYPE g_set_prop(IBackgroundCopyGroup *iface, GROUPPROP id, VARIANT *value)
{
    CALL call;
    if (!value)
        return E_INVALIDARG;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    DWORD number = (DWORD)-1;
    const WCHAR *text = nullptr;
    switch (V_VT(value)) {
    case VT_I2:
        if (V_I2(value) >= 0)
            number = (DWORD)V_I2(value);
        break;
    case VT_I4:
    case VT_INT:
        number = V_I4(value) < 0 ? (DWORD)-1 : (DWORD)V_I4(value);
        break;
    case VT_UI2:
        number = V_UI2(value);
        break;
    case VT_UI4:
    case VT_UINT:
        number = V_UI4(value);
        break;
    case VT_BSTR:
        text = V_BSTR(value);
        break;
    default:
        return end(&call, E_INVALIDARG);
    }
    switch (id) {
    case GROUPPROP_PRIORITY:
    case GROUPPROP_PROTOCOLFLAGS:
        if (number != 1)
            hr = E_NOTIMPL;
        break;
    case GROUPPROP_NOTIFYFLAGS: {
        ULONG flags;
        hr = map_notify(number, &flags);
        if (SUCCEEDED(hr))
            job->notify_flags = flags;
        break;
    }
    case GROUPPROP_NOTIFYCLSID: {
        GUID clsid;
        if (!text)
            hr = E_INVALIDARG;
        else if (SUCCEEDED(hr = IIDFromString(text, &clsid)))
            job->legacy_clsid = clsid;
        break;
    }
    case GROUPPROP_DISPLAYNAME:
        hr = set_limited(&job->name, text, 255);
        break;
    case GROUPPROP_DESCRIPTION:
        hr = set_limited(&job->description, text, 1023);
        break;
    default:
        hr = E_NOTIMPL;
    }
    if (SUCCEEDED(hr)) {
        JobTouch(job, 1);
        StoreSaveJob(job);
    }
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE g_progress(IBackgroundCopyGroup *iface, DWORD flags, DWORD *progress)
{
    CALL call;
    if (!progress)
        return E_INVALIDARG;
    *progress = 0;
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, old_progress(GX(iface)->job, flags, progress));
}

/* COldGroupInterface::GetStatus: a waiting group is SUSPENDED, a group in error ERROR and SUSPENDED. */
static HRESULT STDMETHODCALLTYPE g_status(IBackgroundCopyGroup *iface, DWORD *status, DWORD *index)
{
    CALL call;
    if (!status || !index)
        return E_INVALIDARG;
    *status = *index = 0;
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    if (job->priority == BG_JOB_PRIORITY_FOREGROUND)
        *status |= QM_STATUS_GROUP_FOREGROUND;
    switch (job->state) {
    case BG_JOB_STATE_QUEUED:
    case BG_JOB_STATE_CONNECTING:
    case BG_JOB_STATE_TRANSFERRING:
    case BG_JOB_STATE_TRANSIENT_ERROR:
        *status |= QM_STATUS_GROUP_INCOMPLETE;
        break;
    case BG_JOB_STATE_SUSPENDED:
        *status |= QM_STATUS_GROUP_SUSPENDED | QM_STATUS_GROUP_INCOMPLETE;
        break;
    case BG_JOB_STATE_ERROR:
        *status |= QM_STATUS_GROUP_ERROR | QM_STATUS_GROUP_SUSPENDED | QM_STATUS_GROUP_INCOMPLETE;
        break;
    case BG_JOB_STATE_TRANSFERRED:
    case BG_JOB_STATE_ACKNOWLEDGED:
        *status |= QM_STATUS_GROUP_COMPLETE | QM_STATUS_GROUP_SUSPENDED;
        break;
    default:
        break;
    }
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE g_get_job(IBackgroundCopyGroup *iface, GUID id, IBackgroundCopyJob1 **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    if (!job->legacy_job || !IsEqualGUID(&id, &job->legacy_job_id))
        return end(&call, QM_E_ITEM_NOT_FOUND);
    JOB1_EXT *j;
    hr = job1_for(job, &j);
    if (SUCCEEDED(hr))
        *out = &j->iface;
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE g_suspend(IBackgroundCopyGroup *iface)
{
    CALL call;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = JobSuspend(GX(iface)->job);
    return end(&call, FAILED(hr) ? hr : S_OK);
}

static HRESULT STDMETHODCALLTYPE g_resume(IBackgroundCopyGroup *iface)
{
    CALL call;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = JobResume(GX(iface)->job);
    return end(&call, FAILED(hr) ? hr : S_OK);
}

static HRESULT STDMETHODCALLTYPE g_cancel(IBackgroundCopyGroup *iface)
{
    CALL call;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobCancel(GX(iface)->job, &call.caller));
}

static HRESULT STDMETHODCALLTYPE g_size(IBackgroundCopyGroup *iface, DWORD *size)
{
    CALL call;
    if (!size)
        return E_INVALIDARG;
    *size = 0;
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BG_JOB_PROGRESS p;
    JobProgress(GX(iface)->job, &p);
    *size = p.BytesTotal == BG_SIZE_UNKNOWN ? 0 : (DWORD)p.BytesTotal;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE g_id(IBackgroundCopyGroup *iface, GUID *id)
{
    CALL call;
    if (!id)
        return E_INVALIDARG;
    memset(id, 0, sizeof(*id));
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *id = GX(iface)->job->id;
    return end(&call, S_OK);
}

/* COldGroupInterface::CreateJob: a group that is not running, and that has no job yet. */
static HRESULT STDMETHODCALLTYPE g_create_job(IBackgroundCopyGroup *iface, GUID id, IBackgroundCopyJob1 **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    if (job->state != BG_JOB_STATE_SUSPENDED && job->state != BG_JOB_STATE_ERROR)
        return end(&call, QM_E_INVALID_STATE);
    if (job->legacy_job)
        return end(&call, E_NOTIMPL);
    JOB1_EXT *j;
    hr = job1_for(job, &j);
    if (SUCCEEDED(hr)) {
        job->legacy_job = TRUE;
        job->legacy_job_id = id;
        StoreSaveJob(job);
        *out = &j->iface;
    }
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE g_enum(IBackgroundCopyGroup *iface, DWORD flags, IEnumBackgroundCopyJobs1 **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(GX(iface)->job, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    if (flags)
        return end(&call, E_NOTIMPL);
    BJOB *job = GX(iface)->job;
    return end(&call, EnumGuidCreate(&IID_IEnumBackgroundCopyJobs1, &job->legacy_job_id, job->legacy_job ? 1 : 0,
                                     (void **)out));
}

static HRESULT STDMETHODCALLTYPE g_foreground(IBackgroundCopyGroup *iface)
{
    CALL call;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = GX(iface)->job;
    if (job->custom_method)
        return end(&call, BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD);
    job->priority = BG_JOB_PRIORITY_FOREGROUND;
    if (!job->cost_explicit)
        JobDefaultCost(job);
    JobTouch(job, 1);
    StoreSaveJob(job);
    QmWake();
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE g_new_interface(IBackgroundCopyGroup *iface, REFIID iid, IUnknown **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!IsEqualGUID(iid, &IID_IBackgroundCopyJob))
        return E_NOTIMPL;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobExtFor(GX(iface)->job, (IBackgroundCopyJob **)out));
}

/* COldGroupInterface::SetNotificationPointer: IBackgroundCopyCallback1 only, held as is with an identify blanket. */
static HRESULT STDMETHODCALLTYPE g_notification(IBackgroundCopyGroup *iface, REFIID iid, IUnknown *callback)
{
    CALL call;
    if (!IsEqualGUID(iid, &IID_IBackgroundCopyCallback1))
        return E_NOTIMPL;
    HRESULT hr = begin(GX(iface)->job, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (!callback)
        return end(&call, S_OK);
    BJOB *job = GX(iface)->job;
    IUnknown *found = nullptr;
    NotifyBlanket(callback, nullptr);
    NotifyBlanket(callback, job->owner_token);
    hr = callback->lpVtbl->QueryInterface(callback, iid, (void **)&found);
    if (SUCCEEDED(hr))
        hr = NotifyBlanket(found, job->owner_token);
    if (FAILED(hr)) {
        if (found)
            found->lpVtbl->Release(found);
        return end(&call, hr);
    }
    if (job->legacy_callback != found) {
        if (job->legacy_callback)
            job->legacy_callback->lpVtbl->Release(job->legacy_callback);
        job->legacy_callback = found;
    } else
        found->lpVtbl->Release(found);
    return end(&call, S_OK);
}

static const IBackgroundCopyGroupVtbl g_group_vtbl = {
    g_qi,     g_addref, g_release,    g_get_prop, g_set_prop, g_progress,   g_status,        g_get_job,
    g_suspend, g_resume, g_cancel,    g_size,     g_id,       g_create_job, g_enum,          g_foreground,
    g_new_interface, g_notification,
};

/* ------------------------------------------------------------------ the objects of a job */

static HRESULT group_for(BJOB *job, GROUP_EXT **out)
{
    for (;;) {
        GROUP_EXT *current = job->group_ext;
        if (current) {
            LONG refs = current->refs;
            if (refs && InterlockedCompareExchange(&current->refs, refs + 1, refs) == refs) {
                *out = current;
                return S_OK;
            }
            if (refs)
                continue;
        }
        GROUP_EXT *g = LocalAlloc(LMEM_ZEROINIT, sizeof(*g));
        if (!g)
            return E_OUTOFMEMORY;
        g->iface.lpVtbl = &g_group_vtbl;
        g->refs = 1;
        g->job = job;
        if (InterlockedCompareExchangePointer(&job->group_ext, g, current) != current) {
            LocalFree(g);
            continue;
        }
        JobAddRef(job);
        QmObjectAdd();
        *out = g;
        return S_OK;
    }
}

static HRESULT job1_for(BJOB *job, JOB1_EXT **out)
{
    for (;;) {
        JOB1_EXT *current = job->job1_ext;
        if (current) {
            LONG refs = current->refs;
            if (refs && InterlockedCompareExchange(&current->refs, refs + 1, refs) == refs) {
                *out = current;
                return S_OK;
            }
            if (refs)
                continue;
        }
        JOB1_EXT *j = LocalAlloc(LMEM_ZEROINIT, sizeof(*j));
        if (!j)
            return E_OUTOFMEMORY;
        j->iface.lpVtbl = &g_job1_vtbl;
        j->refs = 1;
        j->job = job;
        if (InterlockedCompareExchangePointer(&job->job1_ext, j, current) != current) {
            LocalFree(j);
            continue;
        }
        JobAddRef(job);
        QmObjectAdd();
        *out = j;
        return S_OK;
    }
}

HRESULT LegacyGroupFor(BJOB *job, IUnknown **out)
{
    *out = nullptr;
    GROUP_EXT *g;
    HRESULT hr = group_for(job, &g);
    if (SUCCEEDED(hr))
        *out = (IUnknown *)&g->iface;
    return hr;
}

HRESULT LegacyJobFor(BJOB *job, IUnknown **out)
{
    *out = nullptr;
    JOB1_EXT *j;
    HRESULT hr = job1_for(job, &j);
    if (SUCCEEDED(hr))
        *out = (IUnknown *)&j->iface;
    return hr;
}

/* ------------------------------------------------------------------ IBackgroundCopyQMgr */

#define QX(iface) CONTAINING_RECORD(iface, QMGR_EXT, iface)

static HRESULT STDMETHODCALLTYPE q_qi(IBackgroundCopyQMgr *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyQMgr)) {
        *out = iface;
        InterlockedIncrement(&QX(iface)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE q_addref(IBackgroundCopyQMgr *iface)
{
    return (ULONG)InterlockedIncrement(&QX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE q_release(IBackgroundCopyQMgr *iface)
{
    QMGR_EXT *q = QX(iface);
    LONG refs = InterlockedDecrement(&q->refs);
    if (refs)
        return (ULONG)refs;
    LocalFree(q);
    QmObjectRelease();
    return 0;
}

/* COldQmgrInterface::CreateGroupInternal: a download job of that guid with no name, telling errors only. */
static HRESULT STDMETHODCALLTYPE q_create(IBackgroundCopyQMgr *iface, GUID id, IBackgroundCopyGroup **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(nullptr, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = CallerCheckManager(&call.caller);
    if (FAILED(hr))
        return end(&call, hr);
    BJOB *job;
    hr = JobCreate(&call.caller, L"", BG_JOB_TYPE_DOWNLOAD, &id, &job);
    if (FAILED(hr))
        return end(&call, hr);
    map_notify(0, &job->notify_flags);
    job->legacy = TRUE;
    GROUP_EXT *g;
    hr = group_for(job, &g);
    if (SUCCEEDED(hr)) {
        StoreSaveJob(job);
        *out = &g->iface;
    } else
        JobRemove(job);
    JobRelease(job);
    return end(&call, hr);
}

/* COldQmgrInterface::GetGroupInternal: only groups, and the caller takes the job over. */
static HRESULT STDMETHODCALLTYPE q_get(IBackgroundCopyQMgr *iface, GUID id, IBackgroundCopyGroup **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(nullptr, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = CallerCheckManager(&call.caller);
    if (FAILED(hr))
        return end(&call, hr);
    BJOB *job = JobFind(&id);
    if (!job || !job->legacy)
        return end(&call, QM_E_ITEM_NOT_FOUND);
    if (FAILED(JobCheckAccess(job, &call.caller, BITS_READ, nullptr)))
        return end(&call, E_ACCESSDENIED);
    hr = JobTakeOwnership(job, &call.caller);
    if (FAILED(hr))
        return end(&call, hr);
    GROUP_EXT *g;
    hr = group_for(job, &g);
    if (SUCCEEDED(hr))
        *out = &g->iface;
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE q_enum(IBackgroundCopyQMgr *iface, DWORD flags, IEnumBackgroundCopyGroups **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(nullptr, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = CallerCheckManager(&call.caller);
    if (FAILED(hr))
        return end(&call, hr);
    if (flags)
        return end(&call, E_NOTIMPL);
    DWORD count = JobCountAll(), n = 0;
    GUID *ids = LocalAlloc(LMEM_FIXED, (count ? count : 1) * sizeof(*ids));
    if (!ids)
        return end(&call, E_OUTOFMEMORY);
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            if (job->legacy && (JobIsOwner(job, &call.caller) || call.caller.admin))
                ids[n++] = job->id;
    }
    hr = EnumGuidCreate(&IID_IEnumBackgroundCopyGroups, ids, n, (void **)out);
    LocalFree(ids);
    return end(&call, hr);
}

static const IBackgroundCopyQMgrVtbl g_qmgr_vtbl = {q_qi, q_addref, q_release, q_create, q_get, q_enum};

HRESULT LegacyManagerCreate(REFIID iid, void **out)
{
    QMGR_EXT *q = LocalAlloc(LMEM_ZEROINIT, sizeof(*q));
    if (!q)
        return E_OUTOFMEMORY;
    q->iface.lpVtbl = &g_qmgr_vtbl;
    q->refs = 1;
    QmObjectAdd();
    HRESULT hr = q_qi(&q->iface, iid, out);
    q_release(&q->iface);
    return hr;
}
