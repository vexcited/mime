#include "qmgr.h"

#define BLOCK_ON_DEMAND (1024 * 1024)
#define SAVE_INTERVAL_MS 5000
#define MAX_ROUNDS 8
/* what a sink answers when the server did not send the part it was asked for. */
#define QM_E_NOT_PARTIAL ((HRESULT)0xA0200001L)
#define QM_E_LOCAL ((HRESULT)0xA0200002L)

enum { STEP_OK, STEP_STOP, STEP_FAILED, STEP_RESTART };

typedef struct SINK {
    RUN *run;
    BFILE *file;
    HANDLE handle;
    UINT64 origin;
    UINT64 base;
    UINT64 limit;
    BOOL partial;
    BOOL whole;
    HRESULT local_error;
    ULONGLONG rate_start;
    UINT64 rate_bytes;
} SINK;

/* ------------------------------------------------------------------ errors */

static BOOL transient_local(DWORD code)
{
    return code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL || code == ERROR_SHARING_VIOLATION ||
           code == ERROR_LOCK_VIOLATION;
}

/* the error of a transfer step, as the job's error when the worker still owns the job. */
static int fail(RUN *run, BFILE *file, HRESULT hr, DWORD status, BOOL local)
{
    BJOB *job = run->job;
    Lock();
    if (RunCurrent(run)) {
        LONG index = file ? (LONG)file->index : -1;
        if (local) {
            BOOL transient = HRESULT_FACILITY(hr) == FACILITY_WIN32 && transient_local(HRESULT_CODE(hr));
            DWORD source = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? SOURCE_WIN32 : SOURCE_HRESULT;
            if (transient)
                JobTransientError(job, BG_ERROR_CONTEXT_LOCAL_FILE, hr, source, index);
            else
                JobFatalError(job, BG_ERROR_CONTEXT_LOCAL_FILE, hr, source, index);
        } else if (hr == BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED) {
            JobFatalError(job, BG_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK, hr, SOURCE_HRESULT, index);
        } else {
            HRESULT code = status ? TransferStatus(status) : hr;
            DWORD source = status ? SOURCE_HTTP : HRESULT_FACILITY(hr) == FACILITY_WIN32 ? SOURCE_WIN32 : SOURCE_HRESULT;
            if (TransferFatal(status ? S_OK : hr, status))
                JobFatalError(job, BG_ERROR_CONTEXT_REMOTE_FILE, code, source, index);
            else
                JobTransientError(job, BG_ERROR_CONTEXT_REMOTE_FILE, code, source, index);
        }
    }
    Unlock();
    return STEP_FAILED;
}

/* a refusal of the server certificate callback ends the job in its own context. */
static int fail_request(RUN *run, BFILE *file, XREQ *x, HRESULT hr, DWORD status)
{
    if (FAILED(x->cert_error)) {
        BJOB *job = run->job;
        Lock();
        if (RunCurrent(run))
            JobFatalError(job, BG_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK, x->cert_error, SOURCE_HRESULT,
                          (LONG)file->index);
        Unlock();
        return STEP_FAILED;
    }
    return fail(run, file, hr, status, FALSE);
}

static BOOL stopped(RUN *run)
{
    LockShared();
    BOOL stop = !RunCurrent(run);
    UnlockShared();
    return stop;
}

/* CJob::UpdateRemoteNameIfRedirected: with ALLOW_REPORT the file takes the url it was redirected to. */
static void redirected(BJOB *job, BFILE *file, const WCHAR *asked, const XFER_RESULT *r)
{
    if ((job->security_flags & 0x700) != BG_HTTP_REDIRECT_POLICY_ALLOW_REPORT || !r->final_url || !wcscmp(asked, r->final_url))
        return;
    QmReplace(&file->remote, r->final_url);
    NotifyModification(job);
}

/* ------------------------------------------------------------------ progress */

/* with the lock held: bytes arrived, which is progress for the timers, the state and the notifications. */
static void progress(BJOB *job, BFILE *file, UINT64 offset, DWORD size, BOOL whole)
{
    file->transferred += size;
    if (whole)
        QmRangesAdd(&file->filled, offset, size);
    if (job->notify_flags & BG_NOTIFY_FILE_RANGES_TRANSFERRED) {
        QmRangesAdd(&file->unreported, offset, size);
        if (job->callback_version == 3 && NotifyEnabled(job, BG_NOTIFY_FILE_RANGES_TRANSFERRED))
            NotifySchedule(job, 0);
    }
    if (job->state == BG_JOB_STATE_CONNECTING)
        JobSetState(job, BG_JOB_STATE_TRANSFERRING);
    job->no_progress_at = 0;
    job->modified = QmFileTime(QmNow());
    NotifyModification(job);
    ULONGLONG tick = GetTickCount64();
    if (tick - job->save_tick >= SAVE_INTERVAL_MS) {
        job->save_tick = tick;
        StoreSaveJob(job);
    }
}

static UINT64 job_bytes(BJOB *job)
{
    UINT64 total = 0;
    for (DWORD i = 0; i < job->file_count; i++)
        total += job->files[i]->transferred;
    return total;
}

/* EnableBITSMaxBandwidth: the writes wait so that the average stays under the rate. */
static void throttle(SINK *s, DWORD size)
{
    DWORD rate = ConfigRateLimit();
    if (!rate)
        return;
    ULONGLONG now = GetTickCount64();
    if (!s->rate_start)
        s->rate_start = now;
    s->rate_bytes += size;
    ULONGLONG due = s->rate_start + s->rate_bytes * 1000 / rate;
    if (due > now)
        WaitForSingleObject(s->run->cancel, (DWORD)(due - now > 60000 ? 60000 : due - now));
}

static HRESULT write_at(HANDLE handle, UINT64 at, const void *data, DWORD size)
{
    OVERLAPPED where = {0};
    where.Offset = (DWORD)at;
    where.OffsetHigh = (DWORD)(at >> 32);
    DWORD written = 0;
    if (!WriteFile(handle, data, size, &written, &where) || written != size)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT sink_write(void *context, UINT64 offset, const void *data, DWORD size)
{
    XREQ *x = context;
    SINK *s = x->sink;
    if (s->run->stop)
        return E_ABORT;
    if (s->partial && x->status != 206)
        return QM_E_NOT_PARTIAL;
    if (offset < s->origin)
        return QM_E_NOT_PARTIAL;
    UINT64 local = s->base + (offset - s->origin);
    if (s->limit != (UINT64)-1 && local + size > s->limit) {
        if (local >= s->limit)
            return QM_E_NOT_PARTIAL;
        size = (DWORD)(s->limit - local);
    }
    HRESULT hr = write_at(s->handle, local, data, size);
    if (FAILED(hr)) {
        s->local_error = hr;
        return QM_E_LOCAL;
    }
    throttle(s, size);
    BJOB *job = s->run->job;
    Lock();
    if (job->run == s->run) {
        progress(job, s->file, offset, size, s->whole);
        if (job->max_size && job_bytes(job) > job->max_size)
            hr = BG_E_MAX_DOWNLOAD_SIZE_LIMIT_REACHED;
    }
    Unlock();
    return hr;
}

/* ------------------------------------------------------------------ sizes */

static BOOL ranges_fit(BFILE *file, UINT64 size)
{
    for (DWORD i = 0; i < file->range_count; i++) {
        UINT64 offset = file->ranges[i].InitialOffset, length = file->ranges[i].Length;
        if (offset > size || (length != BG_LENGTH_TO_EOF && offset + length > size))
            return FALSE;
    }
    return TRUE;
}

/* with the lock held: what a size request learnt. A size that changed starts the file again. */
static int learn_size(RUN *run, BFILE *file, UINT64 size, const WCHAR *last_modified)
{
    BJOB *job = run->job;
    if (file->size != BG_SIZE_UNKNOWN && file->size != size && file->transferred) {
        file->transferred = 0;
        file->filled.count = 0;
    }
    file->headed = TRUE;
    file->size = size;
    if (last_modified && wcslen(last_modified) < ARRAYSIZE(file->last_modified))
        wcscpy(file->last_modified, last_modified);
    if (file->range_count) {
        if (size == BG_SIZE_UNKNOWN && FileRangeTotal(file, size) == BG_SIZE_UNKNOWN) {
            JobFatalError(job, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_MISSING_FILE_SIZE, SOURCE_HRESULT, (LONG)file->index);
            return STEP_FAILED;
        }
        if (size != BG_SIZE_UNKNOWN && !ranges_fit(file, size)) {
            JobFatalError(job, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_INVALID_RANGE, SOURCE_HRESULT, (LONG)file->index);
            return STEP_FAILED;
        }
        file->total = FileRangeTotal(file, size);
    } else
        file->total = size;
    file->resumable = size != BG_SIZE_UNKNOWN && !job->dynamic;
    UINT64 known = 0;
    for (DWORD i = 0; i < job->file_count; i++)
        if (job->files[i]->total != BG_SIZE_UNKNOWN)
            known += job->files[i]->total;
    if (job->max_size && known > job->max_size) {
        JobFatalError(job, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_MAX_DOWNLOAD_SIZE_LIMIT_REACHED, SOURCE_HRESULT,
                      (LONG)file->index);
        return STEP_FAILED;
    }
    return STEP_OK;
}

/* the first request of a file is a HEAD for its size, its Last-Modified and whether it answers ranges. */
static int head(RUN *run, BFILE *file)
{
    BJOB *job = run->job;
    LockShared();
    WCHAR *url = QmDup(file->remote);
    UnlockShared();
    if (!url)
        return fail(run, file, E_OUTOFMEMORY, 0, FALSE);
    XREQ x;
    TransferRequest(run, file, url, nullptr, &x);
    x.q.verb = L"HEAD";
    XFER_RESULT r;
    HRESULT hr = TransferRun(&x, &r);
    int step = STEP_OK;
    if (hr == E_ABORT && SUCCEEDED(x.cert_error) && stopped(run))
        step = STEP_STOP;
    else if (FAILED(hr))
        step = fail_request(run, file, &x, hr, 0);
    else {
        WCHAR *modified = XferHeader(r.headers, L"Last-Modified");
        DWORD failed = 0;
        Lock();
        if (!RunCurrent(run))
            step = STEP_STOP;
        else {
            TransferKeepResponse(file, &r);
            redirected(job, file, url, &r);
            /* a server that does not answer HEAD is downloaded as it comes, from the start. */
            if (r.status == 405 || r.status == 501)
                step = learn_size(run, file, BG_SIZE_UNKNOWN, nullptr);
            else if (r.status < 200 || r.status >= 300)
                failed = r.status;
            else
                step = learn_size(run, file, r.has_length ? r.length : BG_SIZE_UNKNOWN, modified);
        }
        Unlock();
        if (failed)
            step = fail(run, file, S_OK, failed, FALSE);
        LocalFree(modified);
    }
    XferFreeResult(&r);
    TransferFree(&x);
    LocalFree(url);
    return step;
}

/* ------------------------------------------------------------------ http transfers */

static HANDLE open_target(BJOB *job, BFILE *file)
{
    Lock();
    const WCHAR *target = FileTarget(file);
    WCHAR *path = QmDup(target);
    BOOL direct = !file->temp;
    BOOL impersonating = JobAsOwner(job, TRUE);
    Unlock();
    HANDLE h = path ? CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, direct ? OPEN_ALWAYS : OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr)
                    : INVALID_HANDLE_VALUE;
    DWORD error = GetLastError();
    JobBackToSelf(impersonating);
    LocalFree(path);
    SetLastError(error);
    return h;
}

/* the first byte from at on, below size, that the set does not hold, or size when it holds them all. */
static UINT64 first_hole(const RANGES *filled, UINT64 at, UINT64 size)
{
    for (DWORD k = 0; k < filled->count; k++) {
        UINT64 fa = filled->items[k].InitialOffset, fb = fa + filled->items[k].Length;
        if (fa <= at && fb > at)
            at = fb;
    }
    return at < size ? at : size;
}

static UINT64 hole_end(const RANGES *filled, UINT64 at, UINT64 end)
{
    for (DWORD k = 0; k < filled->count; k++)
        if (filled->items[k].InitialOffset > at && filled->items[k].InitialOffset < end)
            end = filled->items[k].InitialOffset;
    return end;
}

/* the next part of a whole file to ask for: a requested range first, else the first gap from the position on. */
static BOOL next_gap(BFILE *file, UINT64 *start, UINT64 *length)
{
    UINT64 size = file->size;
    while (file->wanted.count) {
        UINT64 a = file->wanted.items[0].InitialOffset, b = a + file->wanted.items[0].Length;
        if (b > size || b < a)
            b = size;
        UINT64 at = a < b ? first_hole(&file->filled, a, b) : b;
        if (at < b) {
            *start = at;
            *length = hole_end(&file->filled, at, b) - at;
            return TRUE;
        }
        memmove(&file->wanted.items[0], &file->wanted.items[1], (file->wanted.count - 1) * sizeof(BG_FILE_RANGE));
        file->wanted.count--;
    }
    UINT64 from = file->position < size ? file->position : 0;
    UINT64 at = first_hole(&file->filled, from, size);
    if (at >= size)
        at = first_hole(&file->filled, 0, size);
    if (at >= size)
        return FALSE;
    *start = at;
    *length = hole_end(&file->filled, at, size) - at;
    return TRUE;
}

static void file_done(RUN *run, BFILE *file)
{
    BJOB *job = run->job;
    if (!RunCurrent(run) || file->done)
        return;
    file->done = TRUE;
    if (file->total == BG_SIZE_UNKNOWN)
        file->total = file->transferred;
    if (file->size == BG_SIZE_UNKNOWN && !file->range_count)
        file->size = file->transferred;
    job->files_done++;
    if (job->state == BG_JOB_STATE_CONNECTING)
        JobSetState(job, BG_JOB_STATE_TRANSFERRING);
    if (NotifyEnabled(job, BG_NOTIFY_FILE_TRANSFERRED))
        NotifySchedule(job, 0);
    StoreSaveJob(job);
}

/*
 * a whole file: Range requests for the gaps once the size is known, If-Unmodified-Since once Last-Modified is, a plain
 * GET from the start for dynamic content and for servers that ignore ranges, which starts again after any interruption.
 */
static int get_whole(RUN *run, BFILE *file)
{
    BJOB *job = run->job;
    HANDLE handle = open_target(job, file);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(run, file, HRESULT_FROM_WIN32(GetLastError()), 0, TRUE);
    int step = STEP_OK;
    for (;;) {
        Lock();
        if (!RunCurrent(run)) {
            Unlock();
            step = STEP_STOP;
            break;
        }
        if (file->done) {
            Unlock();
            break;
        }
        BOOL resumable = file->resumable && file->size != BG_SIZE_UNKNOWN && !job->dynamic;
        UINT64 start = 0, length = XFER_TO_END;
        if (resumable) {
            if (!next_gap(file, &start, &length)) {
                file_done(run, file);
                Unlock();
                break;
            }
            if (job->on_demand && length > BLOCK_ON_DEMAND)
                length = BLOCK_ON_DEMAND;
        } else {
            file->transferred = 0;
            file->filled.count = 0;
        }
        WCHAR *url = QmDup(file->remote);
        WCHAR condition[96] = {0};
        if (resumable && file->last_modified[0])
            _snwprintf(condition, ARRAYSIZE(condition) - 1, L"If-Unmodified-Since: %s\r\n", file->last_modified);
        Unlock();
        if (!url) {
            step = fail(run, file, E_OUTOFMEMORY, 0, FALSE);
            break;
        }
        if (!resumable) {
            SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
            SetEndOfFile(handle);
        }
        /* the whole file from its start is a plain GET; a Range goes out only to resume or for a block. */
        BOOL ranged = resumable && !(start == 0 && length == file->size);
        SINK sink = {run, file, handle, start, start, (UINT64)-1, ranged, TRUE};
        XREQ x;
        TransferRequest(run, file, url, condition[0] ? condition : nullptr, &x);
        x.sink = &sink;
        x.q.range = ranged;
        x.q.offset = start;
        x.q.length = length;
        x.q.write = sink_write;
        XFER_RESULT r;
        HRESULT hr = TransferRun(&x, &r);
        Lock();
        BOOL current = RunCurrent(run);
        if (current && r.status) {
            TransferKeepResponse(file, &r);
            redirected(job, file, url, &r);
        }
        Unlock();
        if (!current)
            step = STEP_STOP;
        else if (hr == QM_E_LOCAL)
            step = fail(run, file, sink.local_error, 0, TRUE);
        else if (hr == QM_E_NOT_PARTIAL || (SUCCEEDED(hr) && ranged && r.status == 200)) {
            /* the server sent the whole file for a range: from now on the file comes whole, from the start. */
            Lock();
            file->resumable = FALSE;
            file->transferred = 0;
            file->filled.count = 0;
            Unlock();
        } else if (hr == BG_E_MAX_DOWNLOAD_SIZE_LIMIT_REACHED)
            step = fail(run, file, hr, 0, FALSE);
        else if (FAILED(hr))
            step = fail_request(run, file, &x, hr, 0);
        else if (r.status == 412 && condition[0]) {
            Lock();
            FileReset(file);
            Unlock();
            step = fail(run, file, BG_E_REMOTE_FILE_CHANGED, 0, FALSE);
        } else if (r.status == 416) {
            Lock();
            FileReset(file);
            Unlock();
            step = STEP_RESTART;
        } else if (r.status != 200 && r.status != 206)
            step = fail(run, file, S_OK, r.status, FALSE);
        else if (r.status == 206 && r.has_range && r.total != XFER_TO_END && r.total != file->size) {
            Lock();
            FileReset(file);
            Unlock();
            step = STEP_RESTART;
        } else if (resumable && r.received < length)
            step = fail(run, file, BG_E_CONNECTION_CLOSED, 0, FALSE);
        else if (!resumable) {
            Lock();
            if (file->size != BG_SIZE_UNKNOWN && file->transferred != file->size) {
                Unlock();
                step = fail(run, file, BG_E_CONNECTION_CLOSED, 0, FALSE);
            } else {
                file->size = file->transferred;
                file->total = file->transferred;
                file_done(run, file);
                Unlock();
            }
        }
        XferFreeResult(&r);
        TransferFree(&x);
        LocalFree(url);
        if (step != STEP_OK)
            break;
    }
    CloseHandle(handle);
    return step;
}

/* a file with ranges: each range in turn, packed one after another in the local file. */
static int get_ranges(RUN *run, BFILE *file)
{
    BJOB *job = run->job;
    HANDLE handle = open_target(job, file);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(run, file, HRESULT_FROM_WIN32(GetLastError()), 0, TRUE);
    int step = STEP_OK;
    for (DWORD k = 0;; k++) {
        Lock();
        if (!RunCurrent(run)) {
            Unlock();
            step = STEP_STOP;
            break;
        }
        if (k >= file->range_count) {
            file_done(run, file);
            Unlock();
            break;
        }
        UINT64 base = 0;
        for (DWORD i = 0; i < k; i++) {
            UINT64 l = file->ranges[i].Length;
            base += l == BG_LENGTH_TO_EOF ? file->size - file->ranges[i].InitialOffset : l;
        }
        UINT64 offset = file->ranges[k].InitialOffset, length = file->ranges[k].Length;
        if (length == BG_LENGTH_TO_EOF)
            length = file->size - offset;
        UINT64 done = file->transferred > base ? file->transferred - base : 0;
        WCHAR *url = done < length ? QmDup(file->remote) : nullptr;
        Unlock();
        if (done >= length)
            continue;
        if (!url) {
            step = fail(run, file, E_OUTOFMEMORY, 0, FALSE);
            break;
        }
        SINK sink = {run, file, handle, offset + done, base + done, base + length, TRUE, FALSE};
        XREQ x;
        TransferRequest(run, file, url, nullptr, &x);
        x.sink = &sink;
        x.q.range = TRUE;
        x.q.offset = offset + done;
        x.q.length = length - done;
        x.q.write = sink_write;
        XFER_RESULT r;
        HRESULT hr = TransferRun(&x, &r);
        Lock();
        BOOL current = RunCurrent(run);
        if (current && r.status) {
            TransferKeepResponse(file, &r);
            redirected(job, file, url, &r);
        }
        Unlock();
        if (!current)
            step = STEP_STOP;
        else if (hr == QM_E_LOCAL)
            step = fail(run, file, sink.local_error, 0, TRUE);
        else if (hr == QM_E_NOT_PARTIAL || (SUCCEEDED(hr) && r.status == 200))
            step = fail(run, file, BG_E_INSUFFICIENT_RANGE_SUPPORT, 0, FALSE);
        else if (FAILED(hr))
            step = fail_request(run, file, &x, hr, 0);
        else if (r.status == 416) {
            Lock();
            FileReset(file);
            Unlock();
            step = STEP_RESTART;
        } else if (r.status != 206)
            step = fail(run, file, S_OK, r.status, FALSE);
        else if (r.received < length - done)
            step = fail(run, file, BG_E_CONNECTION_CLOSED, 0, FALSE);
        XferFreeResult(&r);
        TransferFree(&x);
        LocalFree(url);
        if (step != STEP_OK)
            break;
        k--;
    }
    CloseHandle(handle);
    return step;
}

/* ------------------------------------------------------------------ the file protocol */

static BOOL fatal_file_error(DWORD code)
{
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_ACCESS_DENIED:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_DIRECTORY:
        return TRUE;
    default:
        return FALSE;
    }
}

static int fail_remote_file(RUN *run, BFILE *file, DWORD code)
{
    BJOB *job = run->job;
    Lock();
    if (RunCurrent(run)) {
        if (fatal_file_error(code))
            JobFatalError(job, BG_ERROR_CONTEXT_REMOTE_FILE, HRESULT_FROM_WIN32(code), SOURCE_WIN32, (LONG)file->index);
        else
            JobTransientError(job, BG_ERROR_CONTEXT_REMOTE_FILE, HRESULT_FROM_WIN32(code), SOURCE_WIN32,
                              (LONG)file->index);
    }
    Unlock();
    return STEP_FAILED;
}

/* an unc or file url source, read as the owner or as the helper token with BG_TOKEN_NETWORK, and resumed where it was. */
static int copy_file(RUN *run, BFILE *file)
{
    BJOB *job = run->job;
    Lock();
    WCHAR *path = QmFilePath(file->remote);
    HANDLE token = (job->helper_flags & 2) && job->helper_token ? job->helper_token : job->owner_token;
    BOOL impersonating = token && SetThreadToken(nullptr, token);
    Unlock();
    if (!path) {
        JobBackToSelf(impersonating);
        return fail(run, file, E_INVALIDARG, 0, FALSE);
    }
    HANDLE source = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    DWORD error = GetLastError();
    JobBackToSelf(impersonating);
    LocalFree(path);
    if (source == INVALID_HANDLE_VALUE)
        return fail_remote_file(run, file, error);
    LARGE_INTEGER size;
    if (!GetFileSizeEx(source, &size)) {
        error = GetLastError();
        CloseHandle(source);
        return fail_remote_file(run, file, error);
    }
    Lock();
    int step = RunCurrent(run) ? learn_size(run, file, (UINT64)size.QuadPart, nullptr) : STEP_STOP;
    UINT64 at = file->transferred;
    Unlock();
    if (step != STEP_OK) {
        CloseHandle(source);
        return step;
    }
    HANDLE target = open_target(job, file);
    if (target == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        CloseHandle(source);
        return fail(run, file, HRESULT_FROM_WIN32(error), 0, TRUE);
    }
    BYTE *buffer = LocalAlloc(LMEM_FIXED, g_config.buffer_size);
    if (!buffer) {
        CloseHandle(source);
        CloseHandle(target);
        return fail(run, file, E_OUTOFMEMORY, 0, TRUE);
    }
    SINK sink = {run, file, target, 0, 0, (UINT64)-1, FALSE, TRUE};
    while (step == STEP_OK) {
        if (run->stop) {
            step = STEP_STOP;
            break;
        }
        LARGE_INTEGER where = {.QuadPart = (LONGLONG)at};
        DWORD got = 0;
        if (!SetFilePointerEx(source, where, nullptr, FILE_BEGIN) ||
            !ReadFile(source, buffer, g_config.buffer_size, &got, nullptr)) {
            step = fail_remote_file(run, file, GetLastError());
            break;
        }
        if (!got)
            break;
        XREQ x = {0};
        x.sink = &sink;
        x.status = 206;
        HRESULT hr = sink_write(&x, at, buffer, got);
        if (hr == QM_E_LOCAL)
            step = fail(run, file, sink.local_error, 0, TRUE);
        else if (FAILED(hr))
            step = hr == E_ABORT ? STEP_STOP : fail(run, file, hr, 0, FALSE);
        at += got;
    }
    LocalFree(buffer);
    CloseHandle(source);
    CloseHandle(target);
    if (step == STEP_OK) {
        Lock();
        if (file->transferred >= file->size)
            file_done(run, file);
        else
            step = STEP_RESTART;
        Unlock();
    }
    return step;
}

/* ------------------------------------------------------------------ the worker */

/*
 * CJob::Transfer: the sizes of every file first, as the job stays CONNECTING until it knows them, then each file in turn
 * from the first that is not done.
 */
static int transfer(RUN *run)
{
    BJOB *job = run->job;
    for (DWORD i = 0;; i++) {
        Lock();
        if (!RunCurrent(run)) {
            Unlock();
            return STEP_STOP;
        }
        if (i >= job->file_count) {
            Unlock();
            break;
        }
        BFILE *file = job->files[i];
        int protocol = QmProtocol(file->remote);
        BOOL need = !file->done && !file->headed && protocol != PROTOCOL_FILE;
        if (need && job->dynamic) {
            learn_size(run, file, BG_SIZE_UNKNOWN, nullptr);
            need = FALSE;
        }
        Unlock();
        if (need) {
            int step = head(run, file);
            if (step != STEP_OK)
                return step;
        }
    }
    for (DWORD i = 0;; i++) {
        Lock();
        if (!RunCurrent(run)) {
            Unlock();
            return STEP_STOP;
        }
        if (i >= job->file_count) {
            Unlock();
            break;
        }
        BFILE *file = job->files[i];
        BOOL done = file->done;
        BOOL ranged = file->range_count != 0;
        BOOL headed = file->headed;
        int protocol = QmProtocol(file->remote);
        Unlock();
        if (done)
            continue;
        int step;
        if (protocol == PROTOCOL_FILE)
            step = copy_file(run, file);
        else if (!headed)
            step = STEP_RESTART;
        else if (ranged)
            step = get_ranges(run, file);
        else
            step = get_whole(run, file);
        if (step != STEP_OK)
            return step;
    }
    return STEP_OK;
}

DWORD WINAPI DownloadWorker(void *argument)
{
    RUN *run = argument;
    BJOB *job = run->job;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Lock();
    BOOL lost = job->has_validator && !job->validator_cookie;
    /* CheckAndSetFatalErrorForServerCertCallback: the callback object did not survive a restart of the service. */
    if (lost && RunCurrent(run))
        JobFatalError(job, BG_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK, BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED,
                      SOURCE_HRESULT, -1);
    Unlock();
    int step = lost ? STEP_FAILED : STEP_RESTART;
    for (int round = 0; round < MAX_ROUNDS && step == STEP_RESTART; round++)
        step = transfer(run);
    Lock();
    if (step == STEP_OK && RunCurrent(run) && JobAllDone(job))
        JobTransferred(job);
    else if (step == STEP_RESTART && RunCurrent(run))
        JobTransientError(job, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_INVALID_SERVER_RESPONSE, SOURCE_HRESULT, -1);
    Unlock();
    RunFinish(run);
    CoUninitialize();
    return 0;
}
