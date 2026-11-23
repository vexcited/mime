#include "qmgr.h"
#include <mime/win32/dpapi.h>

#define PROGRAM_MAX 260
#define PARAMETERS_MAX 4000
#define HEADERS_MAX 16383
#define PROXY_MAX 32767
#define URL_MAX 2200
#define REPLY_DATA_MAX (1024 * 1024)
#define RPC_X_BAD_STUB_DATA_ ((HRESULT)0x800706F7L)

typedef struct JOB_EXT {
    IBackgroundCopyJob5 iface;
    IBackgroundCopyJobHttpOptions3 http;
    LONG refs;
    BJOB *job;
} JOB_EXT;

typedef struct TOKEN_EXT {
    IBitsTokenOptions iface;
    LONG refs;
    JOB_EXT *outer;
    DWORD granted;
} TOKEN_EXT;

static HRESULT token_create(JOB_EXT *j, void **out);

/* ------------------------------------------------------------------ the call pattern of CJobExternal */

typedef struct CALL {
    CALLER caller;
    BOOL exclusive;
    BOOL owner;
} CALL;

/* CheckServerInstance, the reader or writer lock of the manager, then CLockedJob*Pointer::ValidateAccess. */
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
    hr = JobCheckAccess(job, &call->caller, access, &call->owner);
    if (FAILED(hr)) {
        if (call->exclusive)
            Unlock();
        else
            UnlockShared();
        CallerClose(&call->caller);
        QmLeave();
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

static void modified(BJOB *job)
{
    JobTouch(job, 1);
    StoreSaveJob(job);
}

/* a change that the running transfer must see: it stops and the scheduler starts it again. */
static void restart(BJOB *job)
{
    if (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING) {
        ScheduleInterrupt(job);
        JobSetState(job, BG_JOB_STATE_QUEUED);
    }
    QmWake();
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

static HRESULT out_string(const WCHAR *text, LPWSTR *out)
{
    *out = nullptr;
    if (!text)
        return S_OK;
    *out = QmTaskDup(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static const WCHAR *default_method(BJOB *job)
{
    return job->type == BG_JOB_TYPE_DOWNLOAD ? L"GET" : L"BITS_POST";
}

/* ------------------------------------------------------------------ IUnknown */

#define JX(iface) CONTAINING_RECORD(iface, JOB_EXT, iface)
#define HX(iface) CONTAINING_RECORD(iface, JOB_EXT, http)

static HRESULT job_qi(JOB_EXT *j, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyJob) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyJob2) || IsEqualGUID(iid, &IID_IBackgroundCopyJob3) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyJob4) || IsEqualGUID(iid, &IID_IBackgroundCopyJob5))
        *out = &j->iface;
    else if (IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions) ||
             IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions2) ||
             IsEqualGUID(iid, &IID_IBackgroundCopyJobHttpOptions3))
        *out = &j->http;
    else if (IsEqualGUID(iid, &IID_IBitsTokenOptions))
        return token_create(j, out);
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&j->refs);
    return S_OK;
}

static ULONG job_release(JOB_EXT *j)
{
    LONG refs = InterlockedDecrement(&j->refs);
    if (refs)
        return (ULONG)refs;
    InterlockedCompareExchangePointer(&j->job->ext, nullptr, j);
    JobRelease(j->job);
    LocalFree(j);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE j_qi(IBackgroundCopyJob5 *iface, REFIID iid, void **out)
{
    return job_qi(JX(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE j_addref(IBackgroundCopyJob5 *iface)
{
    return (ULONG)InterlockedIncrement(&JX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE j_release(IBackgroundCopyJob5 *iface)
{
    return job_release(JX(iface));
}

/* ------------------------------------------------------------------ IBackgroundCopyJob */

#define JOB (JX(iface)->job)

static HRESULT STDMETHODCALLTYPE j_add_set(IBackgroundCopyJob5 *iface, ULONG count, BG_FILE_INFO *files)
{
    CALL call;
    if (count && !files)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobAddFileSet(JOB, &call.caller, count, files));
}

static HRESULT STDMETHODCALLTYPE j_add(IBackgroundCopyJob5 *iface, LPCWSTR remote, LPCWSTR local)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobAddFile(JOB, &call.caller, remote, local, (DWORD)-1, nullptr));
}

static HRESULT STDMETHODCALLTYPE j_enum_files(IBackgroundCopyJob5 *iface, IEnumBackgroundCopyFiles **out)
{
    CALL call;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, FileEnumCreate(JOB, out));
}

static HRESULT STDMETHODCALLTYPE j_suspend(IBackgroundCopyJob5 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobSuspend(JOB));
}

static HRESULT STDMETHODCALLTYPE j_resume(IBackgroundCopyJob5 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobResume(JOB));
}

static HRESULT STDMETHODCALLTYPE j_cancel(IBackgroundCopyJob5 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobCancel(JOB, &call.caller));
}

static HRESULT STDMETHODCALLTYPE j_complete(IBackgroundCopyJob5 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobComplete(JOB));
}

static HRESULT STDMETHODCALLTYPE j_id(IBackgroundCopyJob5 *iface, GUID *id)
{
    CALL call;
    if (!id)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *id = JOB->id;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_type(IBackgroundCopyJob5 *iface, BG_JOB_TYPE *type)
{
    CALL call;
    if (!type)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *type = JOB->type;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_progress(IBackgroundCopyJob5 *iface, BG_JOB_PROGRESS *progress)
{
    CALL call;
    if (!progress)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    JobProgress(JOB, progress);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_times(IBackgroundCopyJob5 *iface, BG_JOB_TIMES *times)
{
    CALL call;
    if (!times)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    times->CreationTime = JOB->created;
    times->ModificationTime = JOB->modified;
    times->TransferCompletionTime = JOB->completed;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_state(IBackgroundCopyJob5 *iface, BG_JOB_STATE *state)
{
    CALL call;
    if (!state)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *state = JOB->state;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_error(IBackgroundCopyJob5 *iface, IBackgroundCopyError **error)
{
    CALL call;
    if (!error)
        return E_INVALIDARG;
    *error = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, ErrorCreate(JOB, error));
}

static HRESULT STDMETHODCALLTYPE j_owner(IBackgroundCopyJob5 *iface, LPWSTR *owner)
{
    CALL call;
    if (!owner)
        return E_INVALIDARG;
    *owner = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *owner = QmTaskDup(JOB->owner_text);
    return end(&call, *owner ? S_OK : E_OUTOFMEMORY);
}

static HRESULT STDMETHODCALLTYPE j_set_name(IBackgroundCopyJob5 *iface, LPCWSTR name)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = set_limited(&JOB->name, name, 255);
    if (SUCCEEDED(hr))
        modified(JOB);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_name(IBackgroundCopyJob5 *iface, LPWSTR *name)
{
    CALL call;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *name = QmTaskDup(JOB->name ? JOB->name : L"");
    return end(&call, *name ? S_OK : E_OUTOFMEMORY);
}

static HRESULT STDMETHODCALLTYPE j_set_description(IBackgroundCopyJob5 *iface, LPCWSTR text)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = set_limited(&JOB->description, text, 1023);
    if (SUCCEEDED(hr))
        modified(JOB);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_description(IBackgroundCopyJob5 *iface, LPWSTR *text)
{
    CALL call;
    if (!text)
        return E_INVALIDARG;
    *text = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *text = QmTaskDup(JOB->description ? JOB->description : L"");
    return end(&call, *text ? S_OK : E_OUTOFMEMORY);
}

/* CJob::SetPriority: the cost flags follow the priority unless they were set. */
static HRESULT set_priority(BJOB *job, BG_JOB_PRIORITY priority)
{
    if (job->custom_method)
        return BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD;
    if ((DWORD)priority > BG_JOB_PRIORITY_LOW)
        return E_NOTIMPL;
    if (job->priority != priority) {
        job->priority = priority;
        if (!job->cost_explicit)
            JobDefaultCost(job);
        QmWake();
    }
    modified(job);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE j_set_priority(IBackgroundCopyJob5 *iface, BG_JOB_PRIORITY priority)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, set_priority(JOB, priority));
}

static HRESULT STDMETHODCALLTYPE j_priority(IBackgroundCopyJob5 *iface, BG_JOB_PRIORITY *priority)
{
    CALL call;
    if (!priority)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *priority = JOB->priority;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_set_flags(IBackgroundCopyJob5 *iface, ULONG flags)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (flags & ~0x3Fu)
        return end(&call, E_NOTIMPL);
    JOB->notify_flags = flags;
    modified(JOB);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_flags(IBackgroundCopyJob5 *iface, ULONG *flags)
{
    CALL call;
    if (!flags)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *flags = JOB->notify_flags;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_set_notify(IBackgroundCopyJob5 *iface, IUnknown *notify)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    hr = NotifySetInterface(JOB, notify);
    if (SUCCEEDED(hr))
        modified(JOB);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_notify(IBackgroundCopyJob5 *iface, IUnknown **notify)
{
    CALL call;
    if (!notify)
        return E_INVALIDARG;
    *notify = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, NotifyGetInterface(JOB, notify));
}

/* the retry timer that runs moves by as much as the delay changed. */
static HRESULT STDMETHODCALLTYPE j_set_retry(IBackgroundCopyJob5 *iface, ULONG seconds)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (seconds < 5)
        seconds = 5;
    if (job->retry_at)
        job->retry_at = job->retry_at - (UINT64)job->retry_delay * 10000000ull + (UINT64)seconds * 10000000ull;
    job->retry_delay = seconds;
    modified(job);
    QmWake();
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_retry(IBackgroundCopyJob5 *iface, ULONG *seconds)
{
    CALL call;
    if (!seconds)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *seconds = JOB->retry_delay;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_set_no_progress(IBackgroundCopyJob5 *iface, ULONG seconds)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (job->no_progress_at)
        job->no_progress_at = job->no_progress_at - (UINT64)job->no_progress * 10000000ull + (UINT64)seconds * 10000000ull;
    job->no_progress = seconds;
    modified(job);
    QmWake();
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_no_progress(IBackgroundCopyJob5 *iface, ULONG *seconds)
{
    CALL call;
    if (!seconds)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *seconds = JOB->no_progress;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_error_count(IBackgroundCopyJob5 *iface, ULONG *errors)
{
    CALL call;
    if (!errors)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *errors = JOB->error_count;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_set_proxy(IBackgroundCopyJob5 *iface, BG_JOB_PROXY_USAGE usage, LPCWSTR list,
                                             LPCWSTR bypass)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if ((DWORD)usage > BG_JOB_PROXY_USAGE_AUTODETECT)
        return end(&call, E_INVALIDARG);
    if (usage != BG_JOB_PROXY_USAGE_OVERRIDE && (list || bypass))
        return end(&call, E_INVALIDARG);
    if (usage == BG_JOB_PROXY_USAGE_OVERRIDE && !list)
        return end(&call, E_INVALIDARG);
    if (list && wcslen(list) > PROXY_MAX)
        return end(&call, BG_E_PROXY_LIST_TOO_LARGE);
    if (bypass && wcslen(bypass) > PROXY_MAX)
        return end(&call, BG_E_PROXY_BYPASS_LIST_TOO_LARGE);
    job->proxy_usage = usage;
    QmReplace(&job->proxy_list, list);
    QmReplace(&job->proxy_bypass, bypass);
    restart(job);
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_proxy(IBackgroundCopyJob5 *iface, BG_JOB_PROXY_USAGE *usage, LPWSTR *list,
                                         LPWSTR *bypass)
{
    CALL call;
    if (!usage || !list || !bypass)
        return E_INVALIDARG;
    *list = *bypass = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *usage = JOB->proxy_usage;
    hr = out_string(JOB->proxy_list, list);
    if (SUCCEEDED(hr))
        hr = out_string(JOB->proxy_bypass, bypass);
    if (FAILED(hr)) {
        CoTaskMemFree(*list);
        *list = nullptr;
    }
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_take(IBackgroundCopyJob5 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, JobTakeOwnership(JOB, &call.caller));
}

/* ------------------------------------------------------------------ IBackgroundCopyJob2 */

static HRESULT STDMETHODCALLTYPE j_set_cmdline(IBackgroundCopyJob5 *iface, LPCWSTR program, LPCWSTR parameters)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (!program && parameters)
        return end(&call, E_INVALIDARG);
    if ((program && wcslen(program) > PROGRAM_MAX) || (parameters && wcslen(parameters) > PARAMETERS_MAX))
        return end(&call, BG_E_STRING_TOO_LONG);
    QmReplace(&JOB->cmd_program, program);
    QmReplace(&JOB->cmd_params, parameters);
    modified(JOB);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_cmdline(IBackgroundCopyJob5 *iface, LPWSTR *program, LPWSTR *parameters)
{
    CALL call;
    if (!program || !parameters)
        return E_INVALIDARG;
    *program = *parameters = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    hr = out_string(JOB->cmd_program, program);
    if (SUCCEEDED(hr))
        hr = out_string(JOB->cmd_params, parameters);
    if (FAILED(hr)) {
        CoTaskMemFree(*program);
        *program = nullptr;
    }
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_reply_progress(IBackgroundCopyJob5 *iface, BG_JOB_REPLY_PROGRESS *progress)
{
    CALL call;
    if (!progress)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    if (JOB->type != BG_JOB_TYPE_UPLOAD_REPLY)
        return end(&call, E_NOTIMPL);
    progress->BytesTotal = JOB->reply_url ? JOB->reply_total : BG_SIZE_UNKNOWN;
    progress->BytesTransferred = JOB->reply_done;
    return end(&call, S_OK);
}

/* the reply as it is in the reply file, once it all arrived. */
static HRESULT STDMETHODCALLTYPE j_reply_data(IBackgroundCopyJob5 *iface, BYTE **buffer, UINT64 *length)
{
    CALL call;
    if (!buffer || !length)
        return E_INVALIDARG;
    *buffer = nullptr;
    *length = 0;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (job->type != BG_JOB_TYPE_UPLOAD_REPLY)
        return end(&call, E_NOTIMPL);
    if (!job->reply_name || !job->reply_url || job->reply_done < job->reply_total)
        return end(&call, S_OK);
    if (job->reply_total > REPLY_DATA_MAX)
        return end(&call, BG_E_TOO_LARGE);
    BOOL impersonating = JobAsOwner(job, TRUE);
    HANDLE h = CreateFileW(job->reply_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    JobBackToSelf(impersonating);
    if (h == INVALID_HANDLE_VALUE)
        return end(&call, HRESULT_FROM_WIN32(GetLastError()));
    DWORD size = (DWORD)job->reply_total, got = 0;
    BYTE *data = size ? CoTaskMemAlloc(size) : nullptr;
    if (size && !data)
        hr = E_OUTOFMEMORY;
    else if (size && (!ReadFile(h, data, size, &got, nullptr) || got != size))
        hr = HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(h);
    if (FAILED(hr)) {
        CoTaskMemFree(data);
        return end(&call, hr);
    }
    *buffer = data;
    *length = size;
    return end(&call, S_OK);
}

/* CUploadJob::SetReplyFileName: absolute, not \\?\, writable by the owner, and fixed once the reply started. */
static HRESULT STDMETHODCALLTYPE j_set_reply(IBackgroundCopyJob5 *iface, LPCWSTR name)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (job->type != BG_JOB_TYPE_UPLOAD_REPLY)
        return end(&call, E_NOTIMPL);
    if (job->reply_started)
        return end(&call, BG_E_INVALID_STATE);
    if (!name) {
        if (!job->reply_generated) {
            QmReplace(&job->reply_name, nullptr);
            UploadGenerateReply(job);
        }
        modified(job);
        return end(&call, S_OK);
    }
    BOOL drive = ((name[0] | 0x20) >= 'a' && (name[0] | 0x20) <= 'z') && name[1] == ':' && name[2] == '\\';
    BOOL unc = name[0] == '\\' && name[1] == '\\';
    if ((!drive && !unc) || !wcsncmp(name, L"\\\\?\\", 4) || wcslen(name) > MAX_PATH)
        return end(&call, E_INVALIDARG);
    BOOL impersonating = JobAsOwner(job, TRUE);
    HANDLE h = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    DWORD error = GetLastError();
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (job->reply_generated && job->reply_name)
        DeleteFileW(job->reply_name);
    JobBackToSelf(impersonating);
    if (h == INVALID_HANDLE_VALUE && error != ERROR_FILE_NOT_FOUND)
        return end(&call, HRESULT_FROM_WIN32(error));
    QmReplace(&job->reply_name, name);
    job->reply_generated = FALSE;
    modified(job);
    return end(&call, job->reply_name ? S_OK : E_OUTOFMEMORY);
}

static HRESULT STDMETHODCALLTYPE j_reply(IBackgroundCopyJob5 *iface, LPWSTR *name)
{
    CALL call;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    if (JOB->type != BG_JOB_TYPE_UPLOAD_REPLY)
        return end(&call, E_NOTIMPL);
    return end(&call, out_string(JOB->reply_name, name));
}

/* ValidateCredentials then CJob::SetCredentials: kept protected, one a target and scheme. */
static HRESULT STDMETHODCALLTYPE j_set_creds(IBackgroundCopyJob5 *iface, BG_AUTH_CREDENTIALS *creds)
{
    CALL call;
    if (!creds)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if ((DWORD)creds->Target - 1 > 1)
        return end(&call, BG_E_INVALID_AUTH_TARGET);
    if ((DWORD)creds->Scheme - 1 > 4)
        return end(&call, BG_E_INVALID_AUTH_SCHEME);
    const WCHAR *user = creds->Credentials.Basic.UserName, *password = creds->Credentials.Basic.Password;
    if (creds->Scheme <= BG_AUTH_SCHEME_DIGEST && (!user || !password || !*user))
        return end(&call, E_INVALIDARG);
    if (user && wcslen(user) > 300)
        return end(&call, BG_E_USERNAME_TOO_LARGE);
    if (password && wcslen(password) > 65536)
        return end(&call, BG_E_PASSWORD_TOO_LARGE);
    if (user && !password)
        return end(&call, E_INVALIDARG);
    BJOB *job = JOB;
    DWORD slot = 0;
    while (slot < job->cred_count &&
           (job->creds[slot].target != creds->Target || job->creds[slot].scheme != creds->Scheme))
        slot++;
    if (slot == MAX_CREDS)
        return end(&call, E_OUTOFMEMORY);
    BCRED fresh = {creds->Target, creds->Scheme};
    fresh.user = QmDup(user);
    if (user && !fresh.user)
        return end(&call, E_OUTOFMEMORY);
    if (password) {
        DATA_BLOB in = {(DWORD)((wcslen(password) + 1) * sizeof(WCHAR)), (BYTE *)password}, out = {0};
        if (!CryptProtectData(&in, L"BITS job credentials", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            LocalFree(fresh.user);
            return end(&call, hr);
        }
        fresh.secret = out.pbData;
        fresh.secret_size = out.cbData;
    }
    if (slot < job->cred_count) {
        LocalFree(job->creds[slot].user);
        LocalFree(job->creds[slot].secret);
    } else
        job->cred_count++;
    job->creds[slot] = fresh;
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_remove_creds(IBackgroundCopyJob5 *iface, BG_AUTH_TARGET target, BG_AUTH_SCHEME scheme)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if ((DWORD)target - 1 > 1)
        return end(&call, BG_E_INVALID_AUTH_TARGET);
    if ((DWORD)scheme - 1 > 4)
        return end(&call, BG_E_INVALID_AUTH_SCHEME);
    BJOB *job = JOB;
    for (DWORD i = 0; i < job->cred_count; i++) {
        if (job->creds[i].target != target || job->creds[i].scheme != scheme)
            continue;
        LocalFree(job->creds[i].user);
        if (job->creds[i].secret) {
            SecureZeroMemory(job->creds[i].secret, job->creds[i].secret_size);
            LocalFree(job->creds[i].secret);
        }
        job->creds[i] = job->creds[--job->cred_count];
        memset(&job->creds[job->cred_count], 0, sizeof(job->creds[0]));
        modified(job);
        return end(&call, S_OK);
    }
    return end(&call, S_FALSE);
}

/* ------------------------------------------------------------------ IBackgroundCopyJob3 */

/* CJob::ReplaceRemotePrefix: case sensitive, every new name a url of at most 2200 characters, all or nothing. */
static HRESULT STDMETHODCALLTYPE j_replace_prefix(IBackgroundCopyJob5 *iface, LPCWSTR old_prefix, LPCWSTR new_prefix)
{
    CALL call;
    if (!old_prefix || !*old_prefix || !new_prefix)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (job->type != BG_JOB_TYPE_DOWNLOAD)
        return end(&call, E_NOTIMPL);
    size_t old_length = wcslen(old_prefix), new_length = wcslen(new_prefix);
    WCHAR **names = LocalAlloc(LMEM_ZEROINIT, (job->file_count ? job->file_count : 1) * sizeof(*names));
    if (!names)
        return end(&call, E_OUTOFMEMORY);
    BOOL any = FALSE;
    for (DWORD i = 0; i < job->file_count && SUCCEEDED(hr); i++) {
        BFILE *file = job->files[i];
        if (wcsncmp(file->remote, old_prefix, old_length))
            continue;
        size_t length = new_length + wcslen(file->remote + old_length);
        if (length > URL_MAX) {
            hr = E_INVALIDARG;
            break;
        }
        names[i] = LocalAlloc(LMEM_FIXED, (length + 1) * sizeof(WCHAR));
        if (!names[i]) {
            hr = E_OUTOFMEMORY;
            break;
        }
        wcscpy(names[i], new_prefix);
        wcscat(names[i], file->remote + old_length);
        if (FAILED(FileVerifyRemote(job, names[i], file->range_count != 0)))
            hr = E_INVALIDARG;
        any = TRUE;
    }
    if (SUCCEEDED(hr) && any) {
        restart(job);
        for (DWORD i = 0; i < job->file_count; i++) {
            if (!names[i])
                continue;
            LocalFree(job->files[i]->remote);
            job->files[i]->remote = names[i];
            job->files[i]->headed = FALSE;
            names[i] = nullptr;
        }
        modified(job);
    }
    for (DWORD i = 0; i < job->file_count; i++)
        LocalFree(names[i]);
    LocalFree(names);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE j_add_ranges(IBackgroundCopyJob5 *iface, LPCWSTR remote, LPCWSTR local, DWORD count,
                                              BG_FILE_RANGE *ranges)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (count == (DWORD)-1)
        return end(&call, E_INVALIDARG);
    return end(&call, JobAddFile(JOB, &call.caller, remote, local, count, ranges));
}

static HRESULT STDMETHODCALLTYPE j_set_acl(IBackgroundCopyJob5 *iface, DWORD flags)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (flags & ~0xFu)
        return end(&call, E_INVALIDARG);
    JOB->acl_flags = flags;
    modified(JOB);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_acl(IBackgroundCopyJob5 *iface, DWORD *flags)
{
    CALL call;
    if (!flags)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *flags = JOB->acl_flags;
    return end(&call, S_OK);
}

/* ------------------------------------------------------------------ IBackgroundCopyJob4 */

static HRESULT STDMETHODCALLTYPE j_set_peer(IBackgroundCopyJob5 *iface, DWORD flags)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (flags & ~7u)
        return end(&call, E_INVALIDARG);
    if (JOB->type != BG_JOB_TYPE_DOWNLOAD && JOB->type != BG_JOB_TYPE_UPLOAD_REPLY)
        return end(&call, E_NOTIMPL);
    if (JOB->peer_flags != flags) {
        JOB->peer_flags = flags;
        modified(JOB);
    }
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_peer(IBackgroundCopyJob5 *iface, DWORD *flags)
{
    CALL call;
    if (!flags)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *flags = JOB->peer_flags;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_integrity(IBackgroundCopyJob5 *iface, ULONG *level)
{
    CALL call;
    if (!level)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *level = JOB->owner_il;
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_elevation(IBackgroundCopyJob5 *iface, BOOL *elevated)
{
    CALL call;
    if (!elevated)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *elevated = JOB->owner_elevated != 0;
    return end(&call, S_OK);
}

/* CJob::SetMaximumDownloadTime: a job that already ran longer fails at once. */
static HRESULT STDMETHODCALLTYPE j_set_max_time(IBackgroundCopyJob5 *iface, ULONG seconds)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    job->max_time = seconds;
    UINT64 elapsed = job->elapsed_ms + (job->run_tick ? GetTickCount64() - job->run_tick : 0);
    if (seconds && elapsed >= (UINT64)seconds * 1000 &&
        (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING)) {
        ScheduleInterrupt(job);
        JobFatalError(job, BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER, BG_E_MAXDOWNLOAD_TIMEOUT, SOURCE_HRESULT, -1);
    }
    modified(job);
    QmWake();
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE j_max_time(IBackgroundCopyJob5 *iface, ULONG *seconds)
{
    CALL call;
    if (!seconds)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *seconds = JOB->max_time;
    return end(&call, S_OK);
}

/* ------------------------------------------------------------------ IBackgroundCopyJob5 */

/* CJob::SetProperty. */
static HRESULT set_property(BJOB *job, BITS_JOB_PROPERTY_ID id, BITS_JOB_PROPERTY_VALUE value)
{
    BOOL download = job->type == BG_JOB_TYPE_DOWNLOAD;
    switch ((DWORD)id) {
    case BITS_JOB_PROPERTY_ID_COST_FLAGS:
        if (!value.Dword || (value.Dword & 0x7FFFFF00u))
            return S_OK;
        job->cost_flags = value.Dword;
        job->cost_explicit = TRUE;
        QmWake();
        break;
    case BITS_JOB_PROPERTY_NOTIFICATION_CLSID:
        job->notify_clsid = value.ClsID;
        break;
    case BITS_JOB_PROPERTY_DYNAMIC_CONTENT:
        if (!download)
            return BG_E_PROPERTY_SUPPORTED_FOR_DOWNLOAD_JOBS_ONLY;
        if (job->custom_method)
            return BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD;
        if ((DWORD)value.Enable > 1)
            return E_INVALIDARG;
        if (job->resumed)
            return BG_E_READ_ONLY_PROPERTY_AFTER_RESUME;
        job->dynamic = value.Enable;
        break;
    case BITS_JOB_PROPERTY_HIGH_PERFORMANCE:
        if (!download)
            return BG_E_PROPERTY_SUPPORTED_FOR_DOWNLOAD_JOBS_ONLY;
        if (job->file_count)
            return BG_E_READ_ONLY_PROPERTY_AFTER_ADDFILE;
        if ((DWORD)value.Enable > 1)
            return E_INVALIDARG;
        job->high_performance = value.Enable;
        /* a high performance job never goes to the store, and what was stored of it goes. */
        if (job->high_performance)
            StoreDeleteJob(job);
        break;
    case BITS_JOB_PROPERTY_MAX_DOWNLOAD_SIZE:
        if (!value.Uint64)
            return BG_E_MAX_DOWNLOAD_SIZE_INVALID_VALUE;
        if (!download)
            return BG_E_PROPERTY_SUPPORTED_FOR_DOWNLOAD_JOBS_ONLY;
        if (job->resumed)
            return BG_E_READ_ONLY_PROPERTY_AFTER_RESUME;
        job->max_size = value.Uint64;
        break;
    case 6:
        if (value.Dword & ~3u)
            return E_INVALIDARG;
        job->power_flags = value.Dword;
        QmWake();
        break;
    case BITS_JOB_PROPERTY_USE_STORED_CREDENTIALS:
        if (value.Target.Target != BG_AUTH_TARGET_PROXY)
            return BG_E_USE_STORED_CREDENTIALS_NOT_SUPPORTED;
        job->stored_creds = value.Target.Target;
        break;
    case 8:
        if (value.Dword & ~1u)
            return E_INVALIDARG;
        job->media_flags = value.Dword;
        break;
    case BITS_JOB_PROPERTY_MINIMUM_NOTIFICATION_INTERVAL_MS:
        job->notify_interval = value.Dword;
        break;
    case BITS_JOB_PROPERTY_ON_DEMAND_MODE:
        if (job->custom_method)
            return BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD;
        job->on_demand = value.Enable != 0;
        QmWake();
        break;
    default:
        return BG_E_UNKNOWN_PROPERTY_ID;
    }
    modified(job);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE j_set_property(IBackgroundCopyJob5 *iface, BITS_JOB_PROPERTY_ID id,
                                                BITS_JOB_PROPERTY_VALUE value)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, set_property(JOB, id, value));
}

static HRESULT STDMETHODCALLTYPE j_get_property(IBackgroundCopyJob5 *iface, BITS_JOB_PROPERTY_ID id,
                                                BITS_JOB_PROPERTY_VALUE *value)
{
    CALL call;
    if (!value)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    memset(value, 0, sizeof(*value));
    switch ((DWORD)id) {
    case BITS_JOB_PROPERTY_ID_COST_FLAGS:
        value->Dword = job->cost_flags;
        break;
    case BITS_JOB_PROPERTY_NOTIFICATION_CLSID:
        value->ClsID = job->notify_clsid;
        break;
    case BITS_JOB_PROPERTY_DYNAMIC_CONTENT:
        value->Enable = job->dynamic;
        break;
    case BITS_JOB_PROPERTY_HIGH_PERFORMANCE:
        value->Enable = job->high_performance;
        break;
    case BITS_JOB_PROPERTY_MAX_DOWNLOAD_SIZE:
        value->Uint64 = job->max_size;
        break;
    case 6:
        value->Dword = job->power_flags;
        break;
    case BITS_JOB_PROPERTY_USE_STORED_CREDENTIALS:
        value->Target.Target = (BG_AUTH_TARGET)job->stored_creds;
        break;
    case 8:
        value->Dword = job->media_flags;
        break;
    case BITS_JOB_PROPERTY_MINIMUM_NOTIFICATION_INTERVAL_MS:
        value->Dword = job->notify_interval;
        break;
    case BITS_JOB_PROPERTY_ON_DEMAND_MODE:
        value->Enable = job->on_demand;
        break;
    default:
        hr = BG_E_UNKNOWN_PROPERTY_ID;
    }
    return end(&call, hr);
}

static const IBackgroundCopyJob5Vtbl g_job_vtbl = {
    j_qi,           j_addref,          j_release,       j_add_set,        j_add,          j_enum_files,
    j_suspend,      j_resume,          j_cancel,        j_complete,       j_id,           j_type,
    j_progress,     j_times,           j_state,         j_error,          j_owner,        j_set_name,
    j_name,         j_set_description, j_description,   j_set_priority,   j_priority,     j_set_flags,
    j_flags,        j_set_notify,      j_notify,        j_set_retry,      j_retry,        j_set_no_progress,
    j_no_progress,  j_error_count,     j_set_proxy,     j_proxy,          j_take,         j_set_cmdline,
    j_cmdline,      j_reply_progress,  j_reply_data,    j_set_reply,      j_reply,        j_set_creds,
    j_remove_creds, j_replace_prefix,  j_add_ranges,    j_set_acl,        j_acl,          j_set_peer,
    j_peer,         j_integrity,       j_elevation,     j_set_max_time,   j_max_time,     j_set_property,
    j_get_property,
};

/* ------------------------------------------------------------------ IBackgroundCopyJobHttpOptions 1 to 3 */

#undef JOB
#define JOB (HX(iface)->job)

static HRESULT STDMETHODCALLTYPE h_qi(IBackgroundCopyJobHttpOptions3 *iface, REFIID iid, void **out)
{
    return job_qi(HX(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE h_addref(IBackgroundCopyJobHttpOptions3 *iface)
{
    return (ULONG)InterlockedIncrement(&HX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE h_release(IBackgroundCopyJobHttpOptions3 *iface)
{
    return job_release(HX(iface));
}

static HRESULT set_cert(BJOB *job, BG_CERT_STORE_LOCATION location, LPCWSTR store, const BYTE *hash, LPCWSTR subject)
{
    if ((DWORD)location > BG_CERT_STORE_LOCATION_LOCAL_MACHINE_ENTERPRISE || !store || (!hash && !subject))
        return E_INVALIDARG;
    WCHAR *store_copy = QmDup(store), *subject_copy = subject ? QmDup(subject) : nullptr;
    if (!store_copy || (subject && !subject_copy)) {
        LocalFree(store_copy);
        LocalFree(subject_copy);
        return E_OUTOFMEMORY;
    }
    LocalFree(job->cert_store);
    LocalFree(job->cert_subject);
    job->cert_store = store_copy;
    job->cert_subject = subject_copy;
    job->cert_location = location;
    job->cert_by_hash = hash != nullptr;
    if (hash)
        memcpy(job->cert_hash, hash, sizeof(job->cert_hash));
    job->has_cert = TRUE;
    restart(job);
    modified(job);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE h_cert_id(IBackgroundCopyJobHttpOptions3 *iface, BG_CERT_STORE_LOCATION location,
                                           LPCWSTR store, BYTE *hash)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, hash ? set_cert(JOB, location, store, hash, nullptr) : E_INVALIDARG);
}

static HRESULT STDMETHODCALLTYPE h_cert_name(IBackgroundCopyJobHttpOptions3 *iface, BG_CERT_STORE_LOCATION location,
                                             LPCWSTR store, LPCWSTR subject)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, subject ? set_cert(JOB, location, store, nullptr, subject) : E_INVALIDARG);
}

static HRESULT STDMETHODCALLTYPE h_cert_remove(IBackgroundCopyJobHttpOptions3 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (!job->has_cert)
        return end(&call, S_FALSE);
    job->has_cert = FALSE;
    QmReplace(&job->cert_store, nullptr);
    QmReplace(&job->cert_subject, nullptr);
    restart(job);
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE h_cert(IBackgroundCopyJobHttpOptions3 *iface, BG_CERT_STORE_LOCATION *location,
                                        LPWSTR *store, BYTE **hash, LPWSTR *subject)
{
    CALL call;
    if (!location || !store || !hash || !subject)
        return E_INVALIDARG;
    *store = *subject = nullptr;
    *hash = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (!job->has_cert)
        return end(&call, RPC_X_BAD_STUB_DATA_);
    *location = job->cert_location;
    hr = out_string(job->cert_store, store);
    if (SUCCEEDED(hr))
        hr = out_string(job->cert_subject, subject);
    if (SUCCEEDED(hr) && job->cert_by_hash) {
        *hash = CoTaskMemAlloc(sizeof(job->cert_hash));
        if (*hash)
            memcpy(*hash, job->cert_hash, sizeof(job->cert_hash));
        else
            hr = E_OUTOFMEMORY;
    }
    if (FAILED(hr)) {
        CoTaskMemFree(*store);
        CoTaskMemFree(*subject);
        *store = *subject = nullptr;
    }
    return end(&call, hr);
}

/* CJob::SetCustomHeaders: the owner's alone. */
static HRESULT STDMETHODCALLTYPE h_set_headers(IBackgroundCopyJobHttpOptions3 *iface, LPCWSTR headers)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if (!call.owner)
        return end(&call, E_ACCESSDENIED);
    if (headers && wcslen(headers) > HEADERS_MAX)
        return end(&call, BG_E_STRING_TOO_LONG);
    QmReplace(&JOB->headers, headers && *headers ? headers : nullptr);
    restart(JOB);
    modified(JOB);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE h_headers(IBackgroundCopyJobHttpOptions3 *iface, LPWSTR *headers)
{
    CALL call;
    if (!headers)
        return E_INVALIDARG;
    *headers = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    if (!call.owner || JOB->headers_write_only)
        return end(&call, E_ACCESSDENIED);
    if (!JOB->headers)
        return end(&call, S_FALSE);
    return end(&call, out_string(JOB->headers, headers));
}

static HRESULT STDMETHODCALLTYPE h_set_security(IBackgroundCopyJobHttpOptions3 *iface, ULONG flags)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    if ((flags & ~0xF1Fu) || (flags & 0x700) > BG_HTTP_REDIRECT_POLICY_DISALLOW)
        return end(&call, E_NOTIMPL);
    JOB->security_flags = flags;
    restart(JOB);
    modified(JOB);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE h_security(IBackgroundCopyJobHttpOptions3 *iface, ULONG *flags)
{
    CALL call;
    if (!flags)
        return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *flags = JOB->security_flags;
    return end(&call, S_OK);
}

static BOOL token_char(WCHAR c)
{
    return c > 0x20 && c < 0x7F && !wcschr(L"()<>@,;:\\\"/[]?={}", c);
}

/* CJob::SetHttpMethod: any method but the default one makes the job FOREGROUND for good. */
static HRESULT STDMETHODCALLTYPE h_set_method(IBackgroundCopyJobHttpOptions3 *iface, LPCWSTR method)
{
    CALL call;
    if (!method || !*method)
        return E_INVALIDARG;
    for (const WCHAR *p = method; *p; p++)
        if (!token_char(*p))
            return E_INVALIDARG;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = JOB;
    if (!wcscmp(method, job->method ? job->method : default_method(job)))
        return end(&call, S_OK);
    if (job->resumed)
        return end(&call, BG_E_INVALID_STATE);
    if (job->dynamic || job->on_demand || job->ranges_added)
        return end(&call, BG_E_UNSUPPORTED_JOB_CONFIGURATION);
    if (wcslen(method) > 255)
        return end(&call, BG_E_STRING_TOO_LONG);
    QmReplace(&job->method, method);
    if (!job->method)
        return end(&call, E_OUTOFMEMORY);
    job->custom_method = wcscmp(method, default_method(job)) != 0;
    if (job->custom_method) {
        job->priority = BG_JOB_PRIORITY_FOREGROUND;
        if (!job->cost_explicit)
            JobDefaultCost(job);
    }
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE h_method(IBackgroundCopyJobHttpOptions3 *iface, LPWSTR *method)
{
    CALL call;
    if (!method)
        return E_INVALIDARG;
    *method = nullptr;
    HRESULT hr = begin(JOB, BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, out_string(JOB->method ? JOB->method : default_method(JOB), method));
}

static HRESULT STDMETHODCALLTYPE h_set_validator(IBackgroundCopyJobHttpOptions3 *iface, IUnknown *callback)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BG_JOB_STATE state = JOB->state;
    if (state != BG_JOB_STATE_SUSPENDED && state != BG_JOB_STATE_ERROR && state != BG_JOB_STATE_TRANSFERRED)
        return end(&call, BG_E_READ_ONLY_WHEN_JOB_ACTIVE);
    hr = NotifySetValidator(JOB, callback);
    if (SUCCEEDED(hr))
        modified(JOB);
    return end(&call, hr);
}

static HRESULT STDMETHODCALLTYPE h_write_only(IBackgroundCopyJobHttpOptions3 *iface)
{
    CALL call;
    HRESULT hr = begin(JOB, BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    JOB->headers_write_only = TRUE;
    modified(JOB);
    return end(&call, S_OK);
}

static const IBackgroundCopyJobHttpOptions3Vtbl g_http_vtbl = {
    h_qi,          h_addref,  h_release,      h_cert_id,      h_cert_name,     h_cert_remove, h_cert,
    h_set_headers, h_headers, h_set_security, h_security,     h_set_method,    h_method,      h_set_validator,
    h_write_only,
};

/* ------------------------------------------------------------------ IBitsTokenOptions, TokenHelperExternal */

#define TX(iface) CONTAINING_RECORD(iface, TOKEN_EXT, iface)

static HRESULT STDMETHODCALLTYPE t_qi(IBitsTokenOptions *iface, REFIID iid, void **out)
{
    if (out && IsEqualGUID(iid, &IID_IBitsTokenOptions)) {
        *out = iface;
        InterlockedIncrement(&TX(iface)->refs);
        return S_OK;
    }
    return job_qi(TX(iface)->outer, iid, out);
}

static ULONG STDMETHODCALLTYPE t_addref(IBitsTokenOptions *iface)
{
    return (ULONG)InterlockedIncrement(&TX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE t_release(IBitsTokenOptions *iface)
{
    TOKEN_EXT *t = TX(iface);
    LONG refs = InterlockedDecrement(&t->refs);
    if (refs)
        return (ULONG)refs;
    job_release(t->outer);
    LocalFree(t);
    QmObjectRelease();
    return 0;
}

/* TokenHelperExternal::VerifyAccess: the rights the caller had at QueryInterface time. */
static HRESULT t_begin(TOKEN_EXT *t, DWORD access, CALL *call)
{
    if ((t->granted & access) != access)
        return E_ACCESSDENIED;
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
    if (call->exclusive && (t->outer->job->state == BG_JOB_STATE_ACKNOWLEDGED ||
                            t->outer->job->state == BG_JOB_STATE_CANCELLED))
        return end(call, BG_E_INVALID_STATE);
    return S_OK;
}

static BOOL owner_admin(BJOB *job)
{
    HANDLE token = job->owner_token;
    return token && XferTokenIsAdmin(token);
}

static HRESULT STDMETHODCALLTYPE t_set_flags(IBitsTokenOptions *iface, DWORD flags)
{
    CALL call;
    if (flags & ~3u)
        return E_NOTIMPL;
    HRESULT hr = t_begin(TX(iface), BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = TX(iface)->outer->job;
    if (job->helper_token && !owner_admin(job) && XferTokenIsAdmin(job->helper_token))
        return end(&call, E_ACCESSDENIED);
    job->helper_flags = flags;
    restart(job);
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE t_flags(IBitsTokenOptions *iface, DWORD *flags)
{
    CALL call;
    if (!flags)
        return E_INVALIDARG;
    HRESULT hr = t_begin(TX(iface), BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    *flags = TX(iface)->outer->job->helper_flags;
    return end(&call, S_OK);
}

/* CJob::SetHelperToken: the caller's own token at impersonation level; an unelevated owner may not lend an admin's. */
static HRESULT STDMETHODCALLTYPE t_set_token(IBitsTokenOptions *iface)
{
    CALL call;
    HRESULT hr = t_begin(TX(iface), BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = TX(iface)->outer->job;
    if (call.caller.level < SECURITY_IMPERSONATION_)
        return end(&call, CO_E_FAILEDTOIMPERSONATE_);
    if (call.caller.admin && !owner_admin(job))
        return end(&call, E_ACCESSDENIED);
    HANDLE token = nullptr;
    if (!DuplicateTokenEx(call.caller.token, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_IMPERSONATION_,
                          &token))
        return end(&call, HRESULT_FROM_WIN32(GetLastError()));
    WCHAR *sid = nullptr;
    if (!ConvertSidToStringSidW(call.caller.sid, &sid))
        sid = nullptr;
    if (job->helper_token)
        CloseHandle(job->helper_token);
    job->helper_token = token;
    LocalFree(job->helper_sid);
    job->helper_sid = sid;
    restart(job);
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE t_clear(IBitsTokenOptions *iface)
{
    CALL call;
    HRESULT hr = t_begin(TX(iface), BITS_WRITE, &call);
    if (FAILED(hr))
        return hr;
    BJOB *job = TX(iface)->outer->job;
    if (job->helper_token)
        CloseHandle(job->helper_token);
    job->helper_token = nullptr;
    LocalFree(job->helper_sid);
    job->helper_sid = nullptr;
    restart(job);
    modified(job);
    return end(&call, S_OK);
}

static HRESULT STDMETHODCALLTYPE t_sid(IBitsTokenOptions *iface, LPWSTR *sid)
{
    CALL call;
    if (!sid)
        return E_INVALIDARG;
    *sid = nullptr;
    HRESULT hr = t_begin(TX(iface), BITS_READ, &call);
    if (FAILED(hr))
        return hr;
    return end(&call, out_string(TX(iface)->outer->job->helper_sid, sid));
}

static const IBitsTokenOptionsVtbl g_token_vtbl = {t_qi, t_addref, t_release, t_set_flags, t_flags, t_set_token,
                                                   t_clear, t_sid};

/* the rights are computed once, with MAXIMUM_ALLOWED against the job's security descriptor. */
static HRESULT token_create(JOB_EXT *j, void **out)
{
    static const GENERIC_MAPPING mapping = {BITS_READ, BITS_WRITE, BITS_READ, BITS_ALL};
    CALLER c;
    HRESULT hr = CallerOpen(&c);
    if (FAILED(hr))
        return hr;
    BYTE privileges[256];
    DWORD length = sizeof(privileges), granted = 0;
    BOOL status = FALSE;
    LockShared();
    if (!AccessCheck(j->job->sd, c.token, MAXIMUM_ALLOWED_, (void *)&mapping, privileges, &length, &granted, &status))
        status = FALSE;
    UnlockShared();
    CallerClose(&c);
    TOKEN_EXT *t = LocalAlloc(LMEM_ZEROINIT, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->iface.lpVtbl = &g_token_vtbl;
    t->refs = 1;
    t->outer = j;
    t->granted = status ? granted & (BITS_READ | BITS_WRITE) : 0;
    InterlockedIncrement(&j->refs);
    QmObjectAdd();
    *out = &t->iface;
    return S_OK;
}

/* CJob::GetJobExternal: one object a job while a client holds it, made under the manager lock. */
HRESULT JobExtFor(BJOB *job, IBackgroundCopyJob **out)
{
    for (;;) {
        JOB_EXT *current = job->ext;
        if (current) {
            LONG refs = current->refs;
            if (refs && InterlockedCompareExchange(&current->refs, refs + 1, refs) == refs) {
                *out = (IBackgroundCopyJob *)&current->iface;
                return S_OK;
            }
            if (refs)
                continue;
        }
        JOB_EXT *j = LocalAlloc(LMEM_ZEROINIT, sizeof(*j));
        if (!j)
            return E_OUTOFMEMORY;
        j->iface.lpVtbl = &g_job_vtbl;
        j->http.lpVtbl = &g_http_vtbl;
        j->refs = 1;
        j->job = job;
        /* readers make these under the shared lock, so the first one in wins and the others take it. */
        if (InterlockedCompareExchangePointer(&job->ext, j, current) != current) {
            LocalFree(j);
            continue;
        }
        JobAddRef(job);
        QmObjectAdd();
        *out = (IBackgroundCopyJob *)&j->iface;
        return S_OK;
    }
}
