#include "qmgr.h"

NTSYSAPI NTSTATUS NTAPI RtlAdjustPrivilege(ULONG privilege, BOOLEAN enable, BOOLEAN thread, PBOOLEAN was);

MIME_DEFINE_GUID(CLSID_StdGlobalInterfaceTable_, 0x00000323, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x46);
MIME_DEFINE_GUID(IID_IGlobalInterfaceTable_, 0x00000146, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

#define RPC_S_SERVER_UNAVAILABLE_HR ((HRESULT)0x800706BAL)
#define NORMAL_PRIORITY_CLASS_ 0x20
#define TICKS_PER_MS 10000ull

typedef BOOL(WINAPI *CREATE_ENVIRONMENT)(LPVOID *environment, HANDLE token, BOOL inherit);
typedef BOOL(WINAPI *DESTROY_ENVIRONMENT)(LPVOID environment);

static IGlobalInterfaceTable *g_git;
static HANDLE g_stop;

/* the event a delivery tells, in the order CJob::OnMakeCallback looks for them. */
enum { EVENT_NONE, EVENT_FILE, EVENT_TRANSFERRED, EVENT_ERROR, EVENT_RANGES, EVENT_MODIFICATION };

static const IID *callback_iid(int version)
{
    return version == 3 ? &IID_IBackgroundCopyCallback3
           : version == 2 ? &IID_IBackgroundCopyCallback2
                          : &IID_IBackgroundCopyCallback;
}

void NotifyStart(void)
{
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (FAILED(CoCreateInstance(&CLSID_StdGlobalInterfaceTable_, nullptr, CLSCTX_INPROC_SERVER,
                                &IID_IGlobalInterfaceTable_, (void **)&g_git)))
        g_git = nullptr;
}

void NotifyStop(void)
{
    if (g_stop)
        SetEvent(g_stop);
}

/* ConfigureProxyBlanket with identify level and static cloaking, as the owner, so the client sees the owner. */
HRESULT NotifyBlanket(IUnknown *proxy, HANDLE token)
{
    BOOL impersonating = token && SetThreadToken(nullptr, token);
    DWORD authn = 0, authz = 0, level = 0, imp = 0, caps = 0;
    OLECHAR *principal = nullptr;
    HRESULT hr = CoQueryProxyBlanket(proxy, &authn, &authz, &principal, &level, &imp, nullptr, &caps);
    if (hr == E_NOINTERFACE)
        hr = S_OK;
    else if (SUCCEEDED(hr))
        hr = CoSetProxyBlanket(proxy, authn, authz, principal, level, RPC_C_IMP_LEVEL_IDENTIFY, nullptr,
                               EOAC_STATIC_CLOAKING_);
    CoTaskMemFree(principal);
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
    return hr;
}

static void revoke(DWORD *cookie)
{
    if (*cookie && g_git)
        g_git->lpVtbl->RevokeInterfaceFromGlobal(g_git, *cookie);
    *cookie = 0;
}

void NotifyRevoke(BJOB *job)
{
    revoke(&job->git_cookie);
    revoke(&job->validator_cookie);
}

/* CJobExternal::SetNotifyInterface: the newest callback the object has, in the global interface table. */
HRESULT NotifySetInterface(BJOB *job, IUnknown *callback)
{
    if (!callback) {
        revoke(&job->git_cookie);
        job->callback_version = 0;
        return S_OK;
    }
    if (!g_git)
        return E_UNEXPECTED;
    static const int versions[] = {3, 2, 1};
    IUnknown *found = nullptr;
    int version = 0;
    for (int i = 0; i < 3 && !found; i++)
        if (SUCCEEDED(callback->lpVtbl->QueryInterface(callback, callback_iid(versions[i]), (void **)&found)))
            version = versions[i];
        else
            found = nullptr;
    if (!found)
        return E_NOINTERFACE;
    NotifyBlanket(found, job->owner_token);
    DWORD cookie = 0;
    HRESULT hr = g_git->lpVtbl->RegisterInterfaceInGlobal(g_git, found, callback_iid(version), &cookie);
    found->lpVtbl->Release(found);
    if (FAILED(hr))
        return hr;
    revoke(&job->git_cookie);
    job->git_cookie = cookie;
    job->callback_version = version;
    return S_OK;
}

HRESULT NotifyGetInterface(BJOB *job, IUnknown **callback)
{
    *callback = nullptr;
    if (!job->git_cookie || !g_git)
        return S_OK;
    return g_git->lpVtbl->GetInterfaceFromGlobal(g_git, job->git_cookie, callback_iid(job->callback_version),
                                                 (void **)callback);
}

HRESULT NotifySetValidator(BJOB *job, IUnknown *validator)
{
    if (!validator) {
        revoke(&job->validator_cookie);
        job->has_validator = FALSE;
        return S_OK;
    }
    if (!g_git)
        return E_UNEXPECTED;
    IUnknown *found = nullptr;
    HRESULT hr = validator->lpVtbl->QueryInterface(validator, &IID_IBackgroundCopyServerCertificateValidationCallback,
                                                   (void **)&found);
    if (FAILED(hr))
        return E_NOINTERFACE;
    NotifyBlanket(found, job->owner_token);
    DWORD cookie = 0;
    hr = g_git->lpVtbl->RegisterInterfaceInGlobal(g_git, found, &IID_IBackgroundCopyServerCertificateValidationCallback,
                                                  &cookie);
    found->lpVtbl->Release(found);
    if (FAILED(hr))
        return hr;
    revoke(&job->validator_cookie);
    job->validator_cookie = cookie;
    job->has_validator = TRUE;
    return S_OK;
}

HRESULT NotifyGetValidator(BJOB *job, IBackgroundCopyServerCertificateValidationCallback **out)
{
    *out = nullptr;
    if (!job->validator_cookie || !g_git)
        return BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED;
    return g_git->lpVtbl->GetInterfaceFromGlobal(g_git, job->validator_cookie,
                                                 &IID_IBackgroundCopyServerCertificateValidationCallback, (void **)out);
}

/* CJob::IsCallbackEnabled: modification and file events need an interface or a class, never the command line. */
BOOL NotifyEnabled(BJOB *job, ULONG flag)
{
    if (!(job->notify_flags & flag) || (job->notify_flags & BG_NOTIFY_DISABLE))
        return FALSE;
    if (job->legacy)
        return job->legacy_callback != nullptr && (flag == BG_NOTIFY_JOB_TRANSFERRED || flag == BG_NOTIFY_JOB_ERROR);
    if (job->git_cookie || !IsEqualGUID(&job->notify_clsid, &GUID_NULL))
        return TRUE;
    return (flag == BG_NOTIFY_JOB_TRANSFERRED || flag == BG_NOTIFY_JOB_ERROR) && job->cmd_program;
}

BOOL NotifyPending(BJOB *job)
{
    return job->callback_busy || job->callback_queued || job->modification_due ||
           (job->pending_reason == PENDING_TRANSFERRED && job->state == BG_JOB_STATE_TRANSFERRED &&
            NotifyEnabled(job, BG_NOTIFY_JOB_TRANSFERRED)) ||
           (job->pending_reason == PENDING_ERROR && job->state == BG_JOB_STATE_ERROR &&
            NotifyEnabled(job, BG_NOTIFY_JOB_ERROR));
}

/* CJob::CancelCallback: what waits to be told is not told any more. */
void NotifyCancel(BJOB *job)
{
    job->pending_reason = PENDING_NONE;
    job->callback_at = 0;
}

static BFILE *untold_file(BJOB *job)
{
    for (DWORD i = 0; i < job->file_count; i++)
        if (job->files[i]->done && !job->files[i]->told)
            return job->files[i];
    return nullptr;
}

static BFILE *unreported_file(BJOB *job)
{
    for (DWORD i = 0; i < job->file_count; i++)
        if (job->files[i]->unreported.count)
            return job->files[i];
    return nullptr;
}

static int choose(BJOB *job, UINT64 now, BFILE **file)
{
    *file = nullptr;
    if (!job->legacy && NotifyEnabled(job, BG_NOTIFY_FILE_TRANSFERRED) && (*file = untold_file(job)))
        return EVENT_FILE;
    if (NotifyEnabled(job, BG_NOTIFY_JOB_TRANSFERRED) && job->state == BG_JOB_STATE_TRANSFERRED &&
        job->pending_reason == PENDING_TRANSFERRED)
        return EVENT_TRANSFERRED;
    if (NotifyEnabled(job, BG_NOTIFY_JOB_ERROR) && job->state == BG_JOB_STATE_ERROR && job->pending_reason == PENDING_ERROR)
        return EVENT_ERROR;
    if (job->callback_version == 3 && NotifyEnabled(job, BG_NOTIFY_FILE_RANGES_TRANSFERRED) &&
        (*file = unreported_file(job)))
        return EVENT_RANGES;
    if (job->modification_due && NotifyEnabled(job, BG_NOTIFY_JOB_MODIFICATION)) {
        if (job->modification_at > now) {
            job->callback_at = job->callback_at && job->callback_at < job->modification_at ? job->callback_at
                                                                                          : job->modification_at;
            return EVENT_NONE;
        }
        return EVENT_MODIFICATION;
    }
    job->modification_due = FALSE;
    return EVENT_NONE;
}

typedef struct DELIVERY {
    int event;
    int version;
    DWORD cookie;
    GUID clsid;
    HANDLE token;
    IBackgroundCopyJob *job_ext;
    IBackgroundCopyFile *file_ext;
    IBackgroundCopyError *error;
    BFILE *file;
    BG_FILE_RANGE *ranges;
    DWORD range_count;
    WCHAR *program;
    WCHAR *parameters;
} DELIVERY;

static void delivery_free(DELIVERY *d)
{
    if (d->job_ext)
        QM_RELEASE(d->job_ext);
    if (d->file_ext)
        QM_RELEASE(d->file_ext);
    if (d->error)
        QM_RELEASE(d->error);
    if (d->token)
        CloseHandle(d->token);
    LocalFree(d->ranges);
    LocalFree(d->program);
    LocalFree(d->parameters);
}

/* the class of NOTIFICATION_CLSID, made as the owner, which then takes the place of the interface. */
static HRESULT activate(BJOB *job, DELIVERY *d, IBackgroundCopyCallback3 **out)
{
    *out = nullptr;
    if (IsEqualGUID(&d->clsid, &GUID_NULL))
        return E_NOINTERFACE;
    BOOL impersonating = d->token && SetThreadToken(nullptr, d->token);
    IUnknown *object = nullptr;
    HRESULT hr = CoCreateInstance(&d->clsid, nullptr, CLSCTX_LOCAL_SERVER, &IID_IBackgroundCopyCallback2, (void **)&object);
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
    if (FAILED(hr))
        return hr;
    int version = 2;
    IUnknown *better = nullptr;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IBackgroundCopyCallback3, (void **)&better))) {
        object->lpVtbl->Release(object);
        object = better;
        version = 3;
    }
    NotifyBlanket(object, d->token);
    DWORD cookie = 0;
    if (g_git && SUCCEEDED(g_git->lpVtbl->RegisterInterfaceInGlobal(g_git, object, callback_iid(version), &cookie))) {
        Lock();
        revoke(&job->git_cookie);
        job->git_cookie = cookie;
        job->callback_version = version;
        Unlock();
    }
    d->version = version;
    *out = (IBackgroundCopyCallback3 *)object;
    return S_OK;
}

static HRESULT call(DELIVERY *d, IBackgroundCopyCallback3 *callback)
{
    switch (d->event) {
    case EVENT_TRANSFERRED:
        return callback->lpVtbl->JobTransferred(callback, d->job_ext);
    case EVENT_ERROR:
        return callback->lpVtbl->JobError(callback, d->job_ext, d->error);
    case EVENT_MODIFICATION:
        return callback->lpVtbl->JobModification(callback, d->job_ext, 0);
    case EVENT_FILE:
        /* the first interface has no FileTransferred, the file then just counts as told. */
        if (d->version < 2)
            return S_OK;
        return callback->lpVtbl->FileTransferred(callback, d->job_ext, d->file_ext);
    case EVENT_RANGES:
        return callback->lpVtbl->FileRangesTransferred(callback, d->job_ext, d->file_ext, d->range_count, d->ranges);
    default:
        return S_OK;
    }
}

/* CJob::InterfaceCallback: the interface of the table, the class of NOTIFICATION_CLSID when the client is gone. */
static HRESULT interface_callback(BJOB *job, DELIVERY *d)
{
    IBackgroundCopyCallback3 *callback = nullptr;
    HRESULT hr = E_NOINTERFACE;
    if (d->cookie && g_git)
        hr = g_git->lpVtbl->GetInterfaceFromGlobal(g_git, d->cookie, callback_iid(d->version), (void **)&callback);
    if (FAILED(hr) || !callback) {
        callback = nullptr;
        hr = activate(job, d, &callback);
        if (FAILED(hr))
            return hr;
    }
    hr = call(d, callback);
    callback->lpVtbl->Release(callback);
    if (hr == RPC_S_SERVER_UNAVAILABLE_HR || hr == RPC_E_DISCONNECTED_) {
        if (FAILED(activate(job, d, &callback))) {
            Lock();
            if (job->git_cookie == d->cookie)
                revoke(&job->git_cookie);
            Unlock();
            return hr;
        }
        hr = call(d, callback);
        callback->lpVtbl->Release(callback);
    }
    return hr;
}

/* the token a notification program runs with: the owner's session, else the owner's own token made primary. */
static HANDLE program_token(BJOB *job, DWORD *session)
{
    HANDLE token = SessionTokenFor(job->owner), primary = nullptr;
    if (!token && job->owner_token)
        token = job->owner_token;
    if (token && !DuplicateTokenEx(token, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_PRIMARY_, &primary))
        primary = nullptr;
    if (token && token != job->owner_token)
        CloseHandle(token);
    DWORD size;
    *session = 0;
    if (primary)
        GetTokenInformation(primary, TOKEN_SESSION_ID_CLASS_, session, sizeof(*session), &size);
    return primary;
}

/*
 * CJob::CmdLineCallback and CUserSession::LaunchProcess: the program as the application, the parameters as the command
 * line, in the owner's session with its environment. Exit code 0 is success, as is any exit of a program that completed
 * or cancelled the job.
 */
static HRESULT cmdline_callback(BJOB *job, DELIVERY *d)
{
    if (!d->program)
        return E_FAIL;
    Lock();
    DWORD session = 0;
    HANDLE token = program_token(job, &session);
    Unlock();
    if (!token)
        return HRESULT_FROM_WIN32(ERROR_NOT_LOGGED_ON_);
    HMODULE userenv = LoadLibraryExW(L"userenv.dll", nullptr, 0x800);
    CREATE_ENVIRONMENT create = userenv ? (CREATE_ENVIRONMENT)GetProcAddress(userenv, "CreateEnvironmentBlock") : nullptr;
    DESTROY_ENVIRONMENT destroy =
        userenv ? (DESTROY_ENVIRONMENT)GetProcAddress(userenv, "DestroyEnvironmentBlock") : nullptr;
    void *environment = nullptr;
    if (create && !create(&environment, token, FALSE))
        environment = nullptr;
    STARTUPINFOW startup = {sizeof(startup)};
    startup.lpDesktop = session ? (LPWSTR)L"winsta0\\default" : nullptr;
    PROCESS_INFORMATION process = {0};
    WCHAR *line = d->parameters ? QmDup(d->parameters) : nullptr;
    HRESULT hr = S_OK;
    /* the service token holds both disabled, as on windows. */
    BOOLEAN was;
    RtlAdjustPrivilege(3 /* SeAssignPrimaryTokenPrivilege */, TRUE, FALSE, &was);
    RtlAdjustPrivilege(5 /* SeIncreaseQuotaPrivilege */, TRUE, FALSE, &was);
    if (!CreateProcessAsUserW(token, d->program, line, nullptr, nullptr, FALSE,
                              CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS_, environment, nullptr, &startup,
                              &process))
        hr = HRESULT_FROM_WIN32(GetLastError());
    LocalFree(line);
    if (environment && destroy)
        destroy(environment);
    if (userenv)
        FreeLibrary(userenv);
    CloseHandle(token);
    if (FAILED(hr))
        return hr;
    CloseHandle(process.hThread);
    HANDLE waits[2] = {process.hProcess, g_stop};
    DWORD code = 1;
    if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (!code)
        return S_OK;
    LockShared();
    BOOL done = job->state == BG_JOB_STATE_ACKNOWLEDGED || job->state == BG_JOB_STATE_CANCELLED;
    UnlockShared();
    return done ? S_OK : E_FAIL;
}

/* CJob::OldInterfaceCallback: IBackgroundCopyCallback1::OnStatus with the group, and the BITS 1.0 errors. */
static HRESULT legacy_callback(BJOB *job, DELIVERY *d)
{
    Lock();
    IBackgroundCopyCallback1 *callback = (IBackgroundCopyCallback1 *)job->legacy_callback;
    if (callback)
        QM_ADDREF(callback);
    IUnknown *group = nullptr, *job1 = nullptr;
    LegacyGroupFor(job, &group);
    if (d->event == EVENT_ERROR && job->legacy_job)
        LegacyJobFor(job, &job1);
    DWORD file = job->error.file >= 0 ? (DWORD)job->error.file : 0, win32 = 0, transport = 0;
    if (job->error.source == SOURCE_WIN32 && ((DWORD)job->error.code & 0xFFFF0000) == 0x80070000)
        win32 = (DWORD)job->error.code & 0xFFFF;
    else if (job->error.source == SOURCE_LEGACY)
        win32 = (DWORD)job->error.code;
    if (job->error.context == BG_ERROR_CONTEXT_REMOTE_FILE)
        transport = (DWORD)job->error.code;
    DWORD retries = job->error_count;
    Unlock();
    HRESULT hr = S_FALSE;
    if (callback) {
        if (d->event == EVENT_TRANSFERRED)
            hr = callback->lpVtbl->OnStatus(callback, (IBackgroundCopyGroup *)group, nullptr, (DWORD)-1,
                                            QM_STATUS_GROUP_COMPLETE, 0, 0, 0);
        else
            hr = callback->lpVtbl->OnStatus(callback, (IBackgroundCopyGroup *)group, (IBackgroundCopyJob1 *)job1, file,
                                            QM_STATUS_GROUP_ERROR | QM_STATUS_GROUP_SUSPENDED, retries, win32, transport);
        QM_RELEASE(callback);
        if (SUCCEEDED(hr) && d->event == EVENT_TRANSFERRED) {
            Lock();
            if (job->legacy_callback)
                QM_RELEASE(job->legacy_callback);
            job->legacy_callback = nullptr;
            Unlock();
        }
    }
    if (group)
        QM_RELEASE(group);
    if (job1)
        QM_RELEASE(job1);
    return hr;
}

/* a BITS 1.0 group that transferred is done: its files are in place and the group goes. */
static void legacy_done(BJOB *job)
{
    Lock();
    if (job->legacy && job->state == BG_JOB_STATE_TRANSFERRED) {
        BOOL impersonating = JobAsOwner(job, TRUE);
        for (DWORD i = 0; i < job->file_count; i++)
            if (!job->files[i]->completed)
                FileCommit(job->files[i]);
        JobBackToSelf(impersonating);
        JobSetState(job, BG_JOB_STATE_ACKNOWLEDGED);
        JobRemove(job);
    }
    Unlock();
}

/* the new ranges a file filled since the last FileRangesTransferred, in the offsets of the remote file. */
static void take_ranges(BFILE *file, DELIVERY *d)
{
    d->range_count = file->unreported.count;
    d->ranges = LocalAlloc(LMEM_FIXED, (d->range_count ? d->range_count : 1) * sizeof(*d->ranges));
    if (!d->ranges) {
        d->range_count = 0;
        return;
    }
    memcpy(d->ranges, file->unreported.items, d->range_count * sizeof(*d->ranges));
    file->unreported.count = 0;
}

static DWORD WINAPI deliver(void *argument)
{
    BJOB *job = argument;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        DELIVERY d = {0};
        UINT64 now = QmNow();
        Lock();
        job->callback_queued = FALSE;
        d.event = job->callback_hold ? EVENT_NONE : choose(job, now, &d.file);
        if (d.event == EVENT_NONE) {
            job->callback_busy = FALSE;
            Unlock();
            break;
        }
        job->callback_busy = TRUE;
        d.version = job->callback_version;
        d.cookie = job->git_cookie;
        d.clsid = job->notify_clsid;
        if (job->owner_token)
            DuplicateTokenEx(job->owner_token, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_IMPERSONATION_,
                             &d.token);
        if (!job->legacy)
            JobExtFor(job, &d.job_ext);
        if (d.file)
            FileExtFor(d.file, &d.file_ext);
        if (d.event == EVENT_ERROR)
            ErrorCreate(job, &d.error);
        if (d.event == EVENT_RANGES)
            take_ranges(d.file, &d);
        if (d.event == EVENT_MODIFICATION) {
            job->modification_due = FALSE;
            job->last_modification_tick = GetTickCount64();
        }
        DWORD reason = job->pending_reason;
        if (d.event == EVENT_TRANSFERRED || d.event == EVENT_ERROR) {
            job->pending_reason = PENDING_NONE;
            d.program = QmDup(job->cmd_program);
            d.parameters = QmDup(job->cmd_params);
        }
        BOOL legacy = job->legacy;
        Unlock();
        HRESULT hr;
        if (legacy)
            hr = legacy_callback(job, &d);
        else {
            hr = interface_callback(job, &d);
            if (FAILED(hr) && (d.event == EVENT_TRANSFERRED || d.event == EVENT_ERROR))
                hr = cmdline_callback(job, &d);
        }
        Lock();
        if (SUCCEEDED(hr) && d.event == EVENT_FILE)
            d.file->told = TRUE;
        if (FAILED(hr) && (d.event == EVENT_TRANSFERRED || d.event == EVENT_ERROR) && !job->pending_reason &&
            job->state == (d.event == EVENT_TRANSFERRED ? BG_JOB_STATE_TRANSFERRED : BG_JOB_STATE_ERROR))
            job->pending_reason = reason;
        /* a failed callback is tried again after the job's MinimumRetryDelay; the events that are not kept go. */
        BOOL again = FAILED(hr) && (d.event == EVENT_TRANSFERRED || d.event == EVENT_ERROR || d.event == EVENT_FILE);
        if (FAILED(hr) && d.event == EVENT_FILE && !d.file_ext)
            d.file->told = TRUE;
        if (again)
            job->callback_at = now + (UINT64)job->retry_delay * 10000000ull;
        if (d.event == EVENT_FILE && SUCCEEDED(hr))
            StoreSaveJob(job);
        if (again)
            job->callback_busy = FALSE;
        Unlock();
        delivery_free(&d);
        if (legacy && d.event == EVENT_TRANSFERRED && SUCCEEDED(hr))
            legacy_done(job);
        QmWake();
        if (again)
            break;
    }
    CoUninitialize();
    JobRelease(job);
    return 0;
}

/* CJob::MaybeScheduleCallback, with the lock held: one delivery at a time a job, now or at the time given. */
void NotifySchedule(BJOB *job, UINT64 when)
{
    UINT64 now = QmNow();
    if (when > now) {
        if (!job->callback_at || when < job->callback_at)
            job->callback_at = when;
        QmWake();
        return;
    }
    job->callback_at = 0;
    if (job->legacy && job->state == BG_JOB_STATE_TRANSFERRED && !NotifyEnabled(job, BG_NOTIFY_JOB_TRANSFERRED) &&
        job->pending_reason == PENDING_TRANSFERRED) {
        /* a group nobody listens to is done once it transferred. */
        job->pending_reason = PENDING_NONE;
        BOOL impersonating = JobAsOwner(job, TRUE);
        for (DWORD i = 0; i < job->file_count; i++)
            if (!job->files[i]->completed)
                FileCommit(job->files[i]);
        JobBackToSelf(impersonating);
        JobSetState(job, BG_JOB_STATE_ACKNOWLEDGED);
        JobRemove(job);
        return;
    }
    if (job->callback_busy || job->callback_queued)
        return;
    job->callback_queued = TRUE;
    JobAddRef(job);
    if (FAILED(QmStartThread(deliver, job, nullptr))) {
        job->callback_queued = FALSE;
        job->callback_at = now + (UINT64)job->retry_delay * 10000000ull;
        JobRelease(job);
    }
}

/* CJob::ScheduleModificationCallback: JobModification calls are at least MINIMUM_NOTIFICATION_INTERVAL_MS apart. */
void NotifyModification(BJOB *job)
{
    if (!NotifyEnabled(job, BG_NOTIFY_JOB_MODIFICATION))
        return;
    job->modification_due = TRUE;
    UINT64 now = QmNow();
    ULONGLONG since = GetTickCount64() - job->last_modification_tick;
    UINT64 when = job->last_modification_tick && since < job->notify_interval
                      ? now + (UINT64)(job->notify_interval - since) * TICKS_PER_MS
                      : now;
    job->modification_at = when;
    NotifySchedule(job, when);
}
