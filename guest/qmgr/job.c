#include "qmgr.h"

SRWLOCK g_lock = SRWLOCK_INIT;
BJOB g_online = {&g_online, &g_online};
BJOB g_offline = {&g_offline, &g_offline};
static UINT64 g_order;

#define TICKS_PER_SECOND 10000000ull

void Lock(void)
{
    AcquireSRWLockExclusive(&g_lock);
}

void Unlock(void)
{
    ReleaseSRWLockExclusive(&g_lock);
}

void LockShared(void)
{
    AcquireSRWLockShared(&g_lock);
}

void UnlockShared(void)
{
    ReleaseSRWLockShared(&g_lock);
}

/* ------------------------------------------------------------------ lifetime and the lists */

void JobAddRef(BJOB *job)
{
    InterlockedIncrement(&job->refs);
}

static void free_creds(BJOB *job)
{
    for (DWORD i = 0; i < job->cred_count; i++) {
        LocalFree(job->creds[i].user);
        if (job->creds[i].secret) {
            SecureZeroMemory(job->creds[i].secret, job->creds[i].secret_size);
            LocalFree(job->creds[i].secret);
        }
    }
    memset(job->creds, 0, sizeof(job->creds));
    job->cred_count = 0;
}

void JobRelease(BJOB *job)
{
    if (InterlockedDecrement(&job->refs))
        return;
    NotifyRevoke(job);
    for (DWORD i = 0; i < job->file_count; i++)
        FileFree(job->files[i]);
    LocalFree(job->files);
    free_creds(job);
    if (job->owner_token)
        CloseHandle(job->owner_token);
    if (job->helper_token)
        CloseHandle(job->helper_token);
    if (job->sd)
        LocalFree(job->sd);
    if (job->legacy_callback)
        job->legacy_callback->lpVtbl->Release(job->legacy_callback);
    LocalFree(job->trusted);
    WCHAR *strings[] = {job->name,       job->description, job->owner_text,   job->package,     job->cmd_program,
                        job->cmd_params, job->proxy_list,  job->proxy_bypass, job->headers,     job->method,
                        job->cert_store, job->cert_subject, job->helper_sid,  job->reply_name,  job->reply_url,
                        job->session_id};
    for (DWORD i = 0; i < ARRAYSIZE(strings); i++)
        LocalFree(strings[i]);
    LocalFree(job);
}

void JobLink(BJOB *job, BOOL online)
{
    BJOB *head = online ? &g_online : &g_offline;
    if (job->listed)
        JobUnlink(job);
    job->prev = head->prev;
    job->next = head;
    head->prev->next = job;
    head->prev = job;
    job->listed = TRUE;
    job->online = online;
    if (!job->order)
        job->order = ++g_order;
    else if (job->order > g_order)
        g_order = job->order;
}

void JobUnlink(BJOB *job)
{
    if (!job->listed)
        return;
    job->prev->next = job->next;
    job->next->prev = job->prev;
    job->prev = job->next = nullptr;
    job->listed = FALSE;
}

BJOB *JobFind(const GUID *id)
{
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            if (IsEqualGUID(&job->id, id))
                return job;
    }
    return nullptr;
}

/* CJob::RemoveFromManager: the job leaves the lists and the store, the objects clients hold still read it. */
void JobRemove(BJOB *job)
{
    if (job->removed)
        return;
    job->removed = TRUE;
    BOOL listed = job->listed;
    JobUnlink(job);
    StoreDeleteJob(job);
    if (listed)
        JobRelease(job);
    QmWake();
}

DWORD JobCountForOwner(PSID sid)
{
    DWORD count = 0;
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            if (EqualSid(job->owner, sid))
                count++;
    }
    return count;
}

DWORD JobCountAll(void)
{
    DWORD count = 0;
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            count++;
    }
    return count;
}

void JobDefaultCost(BJOB *job)
{
    job->cost_flags = job->priority <= BG_JOB_PRIORITY_LOW ? g_config.cost_default[job->priority] : 0x800000FF;
}

/* CJobManager::ValidateJobLimit: service accounts and administrators are never limited. */
static HRESULT validate_job_limit(const CALLER *c)
{
    if (c->service || c->admin || !g_config.limits)
        return S_OK;
    DWORD mine = JobCountForOwner(c->sid);
    if (mine && g_config.max_jobs_user <= mine)
        return BG_E_TOO_MANY_JOBS_PER_USER;
    if (g_config.max_jobs_machine <= JobCountAll())
        return BG_E_TOO_MANY_JOBS_PER_MACHINE;
    return S_OK;
}

/* CJob::CJob and CJobManager::CreateJob, with the manager lock held. */
HRESULT JobCreate(const CALLER *c, const WCHAR *name, BG_JOB_TYPE type, const GUID *id, BJOB **out)
{
    *out = nullptr;
    HRESULT hr = validate_job_limit(c);
    if (FAILED(hr))
        return hr;
    GUID guid;
    if (id)
        guid = *id;
    else if (UuidCreate(&guid))
        return E_FAIL;
    if (JobFind(&guid))
        return E_INVALIDARG;
    BJOB *job = LocalAlloc(LMEM_ZEROINIT, sizeof(*job));
    if (!job)
        return E_OUTOFMEMORY;
    job->refs = 1;
    job->id = guid;
    job->type = type;
    job->state = BG_JOB_STATE_SUSPENDED;
    job->priority = BG_JOB_PRIORITY_NORMAL;
    size_t length = wcslen(name);
    job->name = LocalAlloc(LMEM_FIXED, ((length > 255 ? 255 : length) + 1) * sizeof(WCHAR));
    job->description = QmDup(L"");
    if (!job->name || !job->description) {
        JobRelease(job);
        return E_OUTOFMEMORY;
    }
    memcpy(job->name, name, (length > 255 ? 255 : length) * sizeof(WCHAR));
    job->name[length > 255 ? 255 : length] = 0;
    CopySid(sizeof(job->owner_buffer), job->owner_buffer, c->sid);
    job->owner = job->owner_buffer;
    if (!ConvertSidToStringSidW(job->owner, &job->owner_text))
        job->owner_text = nullptr;
    if (c->package[0])
        job->package = QmDup(c->package);
    job->owner_elevated = c->elevated;
    job->owner_il = c->il;
    job->owner_token = CallerOwnerToken(c);
    hr = JobSecurityCreate(job->owner, c->il, &job->sd);
    if (FAILED(hr)) {
        JobRelease(job);
        return hr;
    }
    job->notify_flags = BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
    JobDefaultCost(job);
    job->retry_delay = g_config.retry_delay;
    job->no_progress = g_config.no_progress;
    job->max_time = g_config.max_download_time;
    job->proxy_usage = BG_JOB_PROXY_USAGE_PRECONFIG;
    job->max_size = 0;
    UINT64 now = QmNow();
    job->created = job->modified = QmFileTime(now);
    job->accessed = (LONG64)now;
    JobAddRef(job);
    JobLink(job, TRUE);
    StoreSaveJob(job);
    *out = job;
    return S_OK;
}

/* ------------------------------------------------------------------ state */

void JobAccessed(BJOB *job)
{
    InterlockedExchange64(&job->accessed, (LONG64)QmNow());
}

/* CJob::UpdateModificationTime: behaviours 1 and 2 also move the inactivity timer, which counts from the last access. */
void JobTouch(BJOB *job, int behaviour)
{
    UINT64 now = QmNow();
    job->modified = QmFileTime(now);
    if (behaviour)
        job->accessed = (LONG64)now;
    NotifyModification(job);
}

static BOOL active(BG_JOB_STATE state)
{
    return state == BG_JOB_STATE_QUEUED || state == BG_JOB_STATE_CONNECTING || state == BG_JOB_STATE_TRANSFERRING;
}

/* CJob::SetState: the stored error goes on 2, 3, 6, 7 and 8 and the retry timer on everything but 5. */
void JobSetState(BJOB *job, BG_JOB_STATE state)
{
    BG_JOB_STATE old = job->state;
    BOOL running_before = old == BG_JOB_STATE_CONNECTING || old == BG_JOB_STATE_TRANSFERRING;
    BOOL running_after = state == BG_JOB_STATE_CONNECTING || state == BG_JOB_STATE_TRANSFERRING;
    if (running_before && !running_after && job->run_tick) {
        job->elapsed_ms += GetTickCount64() - job->run_tick;
        job->run_tick = 0;
    } else if (!running_before && running_after)
        job->run_tick = GetTickCount64();
    job->state = state;
    switch (state) {
    case BG_JOB_STATE_TRANSFERRING:
    case BG_JOB_STATE_SUSPENDED:
    case BG_JOB_STATE_TRANSFERRED:
    case BG_JOB_STATE_ACKNOWLEDGED:
    case BG_JOB_STATE_CANCELLED:
        memset(&job->error, 0, sizeof(job->error));
        break;
    default:
        break;
    }
    if (state != BG_JOB_STATE_TRANSIENT_ERROR)
        job->retry_at = 0;
    if (state != BG_JOB_STATE_TRANSIENT_ERROR && !active(state))
        job->no_progress_at = 0;
    if (old != state)
        NotifyModification(job);
    QmWake();
}

BOOL JobRunnable(BJOB *job)
{
    return job->online && !job->removed && active(job->state);
}

BOOL JobAllDone(BJOB *job)
{
    for (DWORD i = 0; i < job->file_count; i++)
        if (!job->files[i]->done)
            return FALSE;
    return job->file_count != 0;
}

/* CJob::GetProgress: the totals add up while each file knows its size, and an upload counts as one file. */
void JobProgress(BJOB *job, BG_JOB_PROGRESS *p)
{
    memset(p, 0, sizeof(*p));
    for (DWORD i = 0; i < job->file_count; i++) {
        BG_FILE_PROGRESS f;
        FileProgress(job->files[i], &f);
        p->BytesTransferred += f.BytesTransferred;
        p->BytesTotal = f.BytesTotal == BG_SIZE_UNKNOWN || p->BytesTotal == BG_SIZE_UNKNOWN ? BG_SIZE_UNKNOWN
                                                                                           : p->BytesTotal + f.BytesTotal;
    }
    p->FilesTotal = job->file_count;
    p->FilesTransferred = job->type == BG_JOB_TYPE_DOWNLOAD ? job->files_done : job->files_done != 0;
}

void JobSetError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file)
{
    job->error.present = TRUE;
    job->error.context = context;
    job->error.code = code;
    job->error.source = source;
    job->error.file = file;
}

/* CJob::SetTransientError: the job waits MinimumRetryDelay, and the no progress timer runs from the first such error. */
void JobTransientError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file)
{
    UINT64 now = QmNow();
    BERROR before = job->error;
    JobSetError(job, context, code, source, file);
    job->error_count++;
    job->retry_count++;
    JobSetState(job, BG_JOB_STATE_TRANSIENT_ERROR);
    job->retry_at = now + (UINT64)job->retry_delay * TICKS_PER_SECOND;
    if (!job->no_progress_at)
        job->no_progress_at = now + (UINT64)job->no_progress * TICKS_PER_SECOND;
    if (memcmp(&before, &job->error, sizeof(before)))
        NotifyModification(job);
    StoreSaveJob(job);
    QmWake();
}

/* CJob::FileFatalError: ERROR, told once through JobError. */
void JobFatalError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file)
{
    JobSetError(job, context, code, source, file);
    job->error_count++;
    JobSetState(job, BG_JOB_STATE_ERROR);
    job->pending_reason = PENDING_ERROR;
    NotifySchedule(job, 0);
    StoreSaveJob(job);
}

/* CJob::JobTransferred: TRANSFERRED, told once through JobTransferred. */
void JobTransferred(BJOB *job)
{
    JobSetState(job, BG_JOB_STATE_TRANSFERRED);
    job->completed = QmFileTime(QmNow());
    job->pending_reason = PENDING_TRANSFERRED;
    NotifySchedule(job, 0);
    StoreSaveJob(job);
}

/* ------------------------------------------------------------------ files as the owner */

/* the helper token for local file work when BG_TOKEN_LOCAL_FILE asks for it, else the owner's token. */
BOOL JobAsOwner(BJOB *job, BOOL files)
{
    HANDLE token = files && (job->helper_flags & 1) && job->helper_token ? job->helper_token : job->owner_token;
    return token && SetThreadToken(nullptr, token);
}

void JobBackToSelf(BOOL impersonating)
{
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
}

/* CJob::RemoveTemporaryFiles: what is not committed goes; a file that stays makes BG_S_UNABLE_TO_DELETE_FILES. */
HRESULT JobDeleteTempFiles(BJOB *job)
{
    BOOL impersonating = JobAsOwner(job, TRUE);
    HRESULT hr = S_OK;
    for (DWORD i = 0; i < job->file_count; i++) {
        BFILE *file = job->files[i];
        if (file->completed)
            continue;
        if (FAILED(FileDeleteTemp(file)))
            hr = BG_S_UNABLE_TO_DELETE_FILES;
    }
    JobBackToSelf(impersonating);
    return hr;
}

/* ------------------------------------------------------------------ the operations of the interfaces */

static HRESULT check_ranges(DWORD count, const BG_FILE_RANGE *ranges)
{
    for (DWORD i = 0; i < count; i++)
        if (ranges[i].Length != BG_LENGTH_TO_EOF && ~ranges[i].InitialOffset < ranges[i].Length)
            return BG_E_INVALID_RANGE;
    if (count < 2)
        return S_OK;
    BG_FILE_RANGE *sorted = LocalAlloc(LMEM_FIXED, count * sizeof(*sorted));
    if (!sorted)
        return E_OUTOFMEMORY;
    memcpy(sorted, ranges, count * sizeof(*sorted));
    for (DWORD i = 1; i < count; i++) {
        BG_FILE_RANGE r = sorted[i];
        DWORD k = i;
        for (; k && sorted[k - 1].InitialOffset > r.InitialOffset; k--)
            sorted[k] = sorted[k - 1];
        sorted[k] = r;
    }
    HRESULT hr = S_OK;
    for (DWORD i = 0; i + 1 < count; i++)
        if (sorted[i].Length == BG_LENGTH_TO_EOF || sorted[i].InitialOffset + sorted[i].Length > sorted[i + 1].InitialOffset)
            hr = BG_E_OVERLAPPING_RANGES;
    LocalFree(sorted);
    return hr;
}

static BOOL limited(BJOB *job, const CALLER *c)
{
    return g_config.limits && !SidIsService(job->owner) && !c->admin;
}

/* CJob::HandleAddFile: a TRANSFERRED job is QUEUED again with its new file. */
static void handle_add_file(BJOB *job)
{
    if (job->state == BG_JOB_STATE_TRANSFERRED) {
        JobSetState(job, BG_JOB_STATE_QUEUED);
        job->completed = QmFileTime(0);
        NotifyCancel(job);
    }
    JobTouch(job, 1);
    StoreSaveJob(job);
    ScheduleNow();
}

/* CJob::AddFileHelper; AddFile passes a count of all ones, as it has no ranges. */
HRESULT JobAddFile(BJOB *job, const CALLER *c, const WCHAR *remote, const WCHAR *local, DWORD count,
                   const BG_FILE_RANGE *ranges)
{
    if (!count)
        return S_OK;
    BOOL ranged = count != (DWORD)-1;
    if (!ranged) {
        if (job->type != BG_JOB_TYPE_DOWNLOAD && job->file_count)
            return BG_E_TOO_MANY_FILES;
    } else {
        if (job->custom_method)
            return BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD;
        if (!ranges)
            return E_INVALIDARG;
        if (job->type != BG_JOB_TYPE_DOWNLOAD)
            return E_NOTIMPL;
    }
    if (!remote || !local)
        return E_INVALIDARG;
    if (limited(job, c) && g_config.max_files < job->file_count + 1)
        return BG_E_TOO_MANY_FILES_IN_JOB;
    if (ranged && limited(job, c) && g_config.max_ranges < count)
        return BG_E_TOO_MANY_RANGES_IN_FILE;
    if (job->state == BG_JOB_STATE_ACKNOWLEDGED || job->state == BG_JOB_STATE_CANCELLED)
        return BG_E_INVALID_STATE;
    if ((job->helper_flags & 1) && !job->helper_token)
        return BG_E_TOKEN_REQUIRED;
    if (ranged) {
        HRESULT hr = check_ranges(count, ranges);
        if (FAILED(hr))
            return hr;
    }
    if (job->file_count == job->file_capacity) {
        DWORD capacity = job->file_capacity ? job->file_capacity * 2 : 4;
        BFILE **files = LocalAlloc(LMEM_FIXED, capacity * sizeof(*files));
        if (!files)
            return E_OUTOFMEMORY;
        if (job->file_count)
            memcpy(files, job->files, job->file_count * sizeof(*files));
        LocalFree(job->files);
        job->files = files;
        job->file_capacity = capacity;
    }
    HRESULT hr = FileVerifyRemote(job, remote, ranged);
    if (FAILED(hr))
        return hr;
    BFILE *file = FileCreate(job, remote, local, ranged ? count : 0, ranges);
    if (!file)
        return E_OUTOFMEMORY;
    BOOL impersonating = JobAsOwner(job, TRUE);
    hr = FileVerifyLocal(file, local);
    JobBackToSelf(impersonating);
    if (FAILED(hr)) {
        FileFree(file);
        return hr;
    }
    if (ranged)
        job->ranges_added = TRUE;
    file->index = job->file_count;
    job->files[job->file_count++] = file;
    if (job->type == BG_JOB_TYPE_UPLOAD_REPLY)
        UploadGenerateReply(job);
    handle_add_file(job);
    return S_OK;
}

/* CJob::AddFileSet: every file or none. */
HRESULT JobAddFileSet(BJOB *job, const CALLER *c, ULONG count, const BG_FILE_INFO *files)
{
    DWORD before = job->file_count;
    BG_JOB_STATE state = job->state;
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < count && SUCCEEDED(hr); i++)
        hr = JobAddFile(job, c, files[i].RemoteName, files[i].LocalName, (DWORD)-1, nullptr);
    if (FAILED(hr)) {
        BOOL impersonating = JobAsOwner(job, TRUE);
        while (job->file_count > before) {
            BFILE *file = job->files[--job->file_count];
            FileDeleteTemp(file);
            FileFree(file);
        }
        JobBackToSelf(impersonating);
        if (state == BG_JOB_STATE_TRANSFERRED && job->state == BG_JOB_STATE_QUEUED)
            JobSetState(job, BG_JOB_STATE_TRANSFERRED);
        StoreSaveJob(job);
    }
    return hr;
}

/* CJob::Resume: the error stays until the transfer moves, the retry count and the download time start again. */
HRESULT JobResume(BJOB *job)
{
    if (!job->file_count)
        return BG_E_EMPTY;
    job->retry_count = 0;
    job->elapsed_ms = 0;
    if (job->run_tick)
        job->run_tick = GetTickCount64();
    switch (job->state) {
    case BG_JOB_STATE_QUEUED:
    case BG_JOB_STATE_CONNECTING:
    case BG_JOB_STATE_TRANSFERRING:
    case BG_JOB_STATE_TRANSFERRED:
        return S_OK;
    case BG_JOB_STATE_SUSPENDED:
        if (JobAllDone(job) && job->type == BG_JOB_TYPE_DOWNLOAD) {
            JobTransferred(job);
            return S_OK;
        }
        break;
    case BG_JOB_STATE_ERROR:
    case BG_JOB_STATE_TRANSIENT_ERROR:
        break;
    default:
        return BG_E_INVALID_STATE;
    }
    if (job->helper_flags && !job->helper_token) {
        JobFatalError(job, BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER, BG_E_TOKEN_REQUIRED, SOURCE_HRESULT, -1);
        return S_OK;
    }
    BG_JOB_STATE before = job->state;
    job->state = BG_JOB_STATE_QUEUED;
    job->retry_at = 0;
    job->no_progress_at = 0;
    if (before != job->state)
        NotifyModification(job);
    job->resumed = TRUE;
    JobTouch(job, 1);
    StoreSaveJob(job);
    ScheduleNow();
    return S_OK;
}

/* CJob::Suspend. */
HRESULT JobSuspend(BJOB *job)
{
    switch (job->state) {
    case BG_JOB_STATE_QUEUED:
    case BG_JOB_STATE_CONNECTING:
    case BG_JOB_STATE_TRANSFERRING:
        ScheduleInterrupt(job);
        NotifyCancel(job);
        break;
    case BG_JOB_STATE_SUSPENDED:
        return S_OK;
    case BG_JOB_STATE_ERROR:
    case BG_JOB_STATE_TRANSIENT_ERROR:
        break;
    case BG_JOB_STATE_TRANSFERRED:
        NotifyCancel(job);
        break;
    default:
        return BG_E_INVALID_STATE;
    }
    JobSetState(job, BG_JOB_STATE_SUSPENDED);
    JobTouch(job, 2);
    StoreSaveJob(job);
    QmWake();
    return S_OK;
}

/* CJob::Cancel: the temporary files go as the owner; only an error other than access denied keeps the job. */
HRESULT JobCancel(BJOB *job, const CALLER *c)
{
    switch (job->state) {
    case BG_JOB_STATE_QUEUED:
    case BG_JOB_STATE_CONNECTING:
    case BG_JOB_STATE_TRANSFERRING:
        ScheduleInterrupt(job);
        ScheduleWaitIdle(job);
        break;
    case BG_JOB_STATE_ACKNOWLEDGED:
    case BG_JOB_STATE_CANCELLED:
        return BG_E_INVALID_STATE;
    default:
        break;
    }
    if (job->run) {
        ScheduleInterrupt(job);
        ScheduleWaitIdle(job);
    }
    if (job->state == BG_JOB_STATE_ACKNOWLEDGED || job->state == BG_JOB_STATE_CANCELLED)
        return BG_E_INVALID_STATE;
    JobTouch(job, 0);
    if (job->type != BG_JOB_TYPE_DOWNLOAD && job->session_id)
        UploadCancelSession(job);
    HRESULT hr = JobDeleteTempFiles(job);
    if (job->type == BG_JOB_TYPE_UPLOAD_REPLY && job->reply_generated && job->reply_name) {
        BOOL impersonating = JobAsOwner(job, TRUE);
        DeleteFileW(job->reply_name);
        JobBackToSelf(impersonating);
    }
    if (SUCCEEDED(hr) || hr == E_ACCESSDENIED) {
        NotifyCancel(job);
        JobSetState(job, BG_JOB_STATE_CANCELLED);
        JobRemove(job);
        NotifyModification(job);
    }
    return hr;
}

/* CJob::CommitTemporaryFiles: each whole file is renamed, the others are deleted with BG_S_PARTIAL_COMPLETE. */
static HRESULT commit(BJOB *job)
{
    if ((job->helper_flags & 1) && !job->helper_token)
        return BG_E_TOKEN_REQUIRED;
    BOOL impersonating = JobAsOwner(job, TRUE);
    HRESULT hr = S_OK;
    BOOL partial = FALSE;
    for (DWORD i = 0; i < job->file_count; i++) {
        BFILE *file = job->files[i];
        if (file->completed)
            continue;
        if (file->total != BG_SIZE_UNKNOWN && file->total == file->transferred) {
            HRESULT one = FileCommit(file);
            if (FAILED(one) && SUCCEEDED(hr))
                hr = one;
        } else
            partial = TRUE;
    }
    JobBackToSelf(impersonating);
    if (SUCCEEDED(hr)) {
        hr = JobDeleteTempFiles(job);
        if (partial)
            hr = BG_S_PARTIAL_COMPLETE;
    } else
        JobTouch(job, 2);
    return hr;
}

/* CJob::Complete and CUploadJob::Complete; an upload completes only once TRANSFERRED. */
HRESULT JobComplete(BJOB *job)
{
    if (job->type != BG_JOB_TYPE_DOWNLOAD) {
        if (job->state != BG_JOB_STATE_TRANSFERRED)
            return BG_E_INVALID_STATE;
        if ((job->helper_flags & 1) && !job->helper_token)
            return BG_E_TOKEN_REQUIRED;
        HRESULT hr = UploadCommitReply(job);
        if (FAILED(hr))
            return hr;
        NotifyCancel(job);
        JobSetState(job, BG_JOB_STATE_ACKNOWLEDGED);
        JobRemove(job);
        return hr;
    }
    switch (job->state) {
    case BG_JOB_STATE_QUEUED:
    case BG_JOB_STATE_CONNECTING:
    case BG_JOB_STATE_TRANSFERRING:
        JobSuspend(job);
        ScheduleWaitIdle(job);
        break;
    case BG_JOB_STATE_CANCELLED:
        return BG_E_INVALID_STATE;
    default:
        break;
    }
    if (job->run) {
        ScheduleInterrupt(job);
        ScheduleWaitIdle(job);
    }
    if (job->state == BG_JOB_STATE_ACKNOWLEDGED || job->state == BG_JOB_STATE_CANCELLED)
        return BG_E_INVALID_STATE;
    HRESULT hr = S_OK;
    if (!job->legacy_job) {
        hr = commit(job);
        if (FAILED(hr))
            return hr;
    }
    NotifyCancel(job);
    JobSetState(job, BG_JOB_STATE_ACKNOWLEDGED);
    JobRemove(job);
    return hr;
}

/* CFile::ValidateAccessForUser: the new owner must be able to write the temporary file and the destination folder. */
static HRESULT new_owner_access(BJOB *job, HANDLE token)
{
    if (!token)
        return S_OK;
    if (!SetThreadToken(nullptr, token))
        return BG_E_NEW_OWNER_NO_FILE_ACCESS;
    HRESULT hr = S_OK;
    for (DWORD i = 0; i < job->file_count && SUCCEEDED(hr); i++) {
        BFILE *file = job->files[i];
        const WCHAR *path = job->type == BG_JOB_TYPE_DOWNLOAD ? FileTarget(file) : file->local;
        if (file->completed || !path)
            continue;
        HANDLE h = CreateFileW(path, job->type == BG_JOB_TYPE_DOWNLOAD ? GENERIC_WRITE : GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                hr = BG_E_NEW_OWNER_NO_FILE_ACCESS;
        } else
            CloseHandle(h);
    }
    SetThreadToken(nullptr, nullptr);
    return hr;
}

/* CJob::TakeOwnership: the owner's secrets do not pass to the next owner. */
HRESULT JobTakeOwnership(BJOB *job, const CALLER *c)
{
    if (JobIsOwner(job, c))
        return S_OK;
    HANDLE token = CallerOwnerToken(c);
    HRESULT hr = new_owner_access(job, token);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (SUCCEEDED(hr))
        hr = JobSecurityCreate(c->sid, c->il, &sd);
    if (FAILED(hr)) {
        if (token)
            CloseHandle(token);
        return hr;
    }
    LocalFree(job->sd);
    job->sd = sd;
    CopySid(sizeof(job->owner_buffer), job->owner_buffer, c->sid);
    LocalFree(job->owner_text);
    if (!ConvertSidToStringSidW(job->owner, &job->owner_text))
        job->owner_text = nullptr;
    QmReplace(&job->package, c->package[0] ? c->package : nullptr);
    job->owner_elevated = c->elevated;
    job->owner_il = c->il;
    if (job->owner_token)
        CloseHandle(job->owner_token);
    job->owner_token = token;
    free_creds(job);
    QmReplace(&job->cmd_program, nullptr);
    QmReplace(&job->cmd_params, nullptr);
    QmReplace(&job->headers, nullptr);
    job->headers_write_only = FALSE;
    job->has_cert = FALSE;
    QmReplace(&job->cert_store, nullptr);
    QmReplace(&job->cert_subject, nullptr);
    if (!job->online)
        JobLink(job, TRUE);
    JobTouch(job, 1);
    StoreSaveJob(job);
    QmWake();
    return S_OK;
}

/* ------------------------------------------------------------------ users that log on and off */

/* CJob::OnSessionLogon: the jobs of a user who logs on come online; a null sid looks at every owner again. */
void JobUserLogon(PSID sid)
{
    Lock();
    for (BJOB *job = g_offline.next, *next; job != &g_offline; job = next) {
        next = job->next;
        if (sid ? EqualSid(job->owner, sid) : OwnerLoggedOn(job->owner))
            JobLink(job, TRUE);
    }
    Unlock();
    QmWake();
}

/* CJob::OnSessionLogoff: a running job of the user stops and its jobs wait offline. */
void JobUserLogoff(PSID sid)
{
    Lock();
    for (BJOB *job = g_online.next, *next; job != &g_online; job = next) {
        next = job->next;
        if (!EqualSid(job->owner, sid) || SidIsService(sid))
            continue;
        if (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING) {
            ScheduleInterrupt(job);
            JobSetState(job, BG_JOB_STATE_QUEUED);
        }
        JobLink(job, FALSE);
    }
    Unlock();
    QmWake();
}

/* the service stops: the workers end, every job is written as it is and freed. */
void JobShutdown(void)
{
    Lock();
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            ScheduleInterrupt(job);
    }
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        for (BJOB *job = head->next; job != head; job = job->next)
            ScheduleWaitIdle(job);
    }
    for (int pass = 0; pass < 2; pass++) {
        BJOB *head = pass ? &g_offline : &g_online;
        while (head->next != head) {
            BJOB *job = head->next;
            StoreSaveJob(job);
            JobUnlink(job);
            JobRelease(job);
        }
    }
    Unlock();
}
