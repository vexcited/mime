#include "qmgr.h"

#define BITS_PROTOCOL L"{7df0354d-249b-430f-820d-3d2a9bef4931}"
#define FRAGMENT_MAX (1024 * 1024)
#define REPLY_MAX ((UINT64)1 << 32)

enum { STEP_OK, STEP_STOP, STEP_FAILED, STEP_RESTART };

/* the two ends of an upload request: the local file its body is read from, the reply file its answer goes to. */
typedef struct SOURCE {
    RUN *run;
    HANDLE handle;
    HANDLE reply;
} SOURCE;

static HRESULT read_source(void *context, UINT64 offset, void *data, DWORD size, DWORD *got)
{
    XREQ *x = context;
    SOURCE *s = x->sink;
    OVERLAPPED where = {0};
    where.Offset = (DWORD)offset;
    where.OffsetHigh = (DWORD)(offset >> 32);
    if (!ReadFile(s->handle, data, size, got, &where))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static int fail(RUN *run, BG_ERROR_CONTEXT context, HRESULT hr, DWORD status, BOOL fatal)
{
    BJOB *job = run->job;
    Lock();
    if (RunCurrent(run)) {
        HRESULT code = status ? TransferStatus(status) : hr;
        DWORD source = status ? SOURCE_HTTP : HRESULT_FACILITY(hr) == FACILITY_WIN32 ? SOURCE_WIN32 : SOURCE_HRESULT;
        if (fatal)
            JobFatalError(job, context, code, source, 0);
        else
            JobTransientError(job, context, code, source, 0);
    }
    Unlock();
    return STEP_FAILED;
}

static int fail_http(RUN *run, XREQ *x, HRESULT hr, DWORD status)
{
    if (FAILED(x->cert_error))
        return fail(run, BG_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK, x->cert_error, 0, TRUE);
    return fail(run, BG_ERROR_CONTEXT_REMOTE_FILE, hr, status, TransferFatal(status ? S_OK : hr, status));
}

static UINT64 number(const WCHAR *text, int base)
{
    UINT64 n = 0;
    if (!text)
        return 0;
    if (base == 16 && text[0] == '0' && (text[1] | 0x20) == 'x')
        text += 2;
    for (;; text++) {
        WCHAR c = *text;
        int digit = c >= '0' && c <= '9' ? c - '0' : base == 16 && (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
        if (digit < 0)
            break;
        n = n * (UINT64)base + (UINT64)digit;
    }
    return n;
}

/* the error a BITS server names in BITS-Error-Code and BITS-Error-Context, 0x5 the remote file, 0x7 its application. */
static int server_error(RUN *run, XREQ *x, const XFER_RESULT *r)
{
    WCHAR *code = XferHeader(r->headers, L"BITS-Error-Code");
    WCHAR *context = XferHeader(r->headers, L"BITS-Error-Context");
    int step;
    if (code) {
        HRESULT hr = (HRESULT)(DWORD)number(code, 16);
        DWORD where = context ? (DWORD)number(context, 16) : BG_ERROR_CONTEXT_REMOTE_FILE;
        if (hr == BG_E_SESSION_NOT_FOUND) {
            Lock();
            QmReplace(&run->job->session_id, nullptr);
            run->job->upload_acked = 0;
            Unlock();
        }
        step = fail(run, where == BG_ERROR_CONTEXT_REMOTE_APPLICATION ? BG_ERROR_CONTEXT_REMOTE_APPLICATION
                                                                       : BG_ERROR_CONTEXT_REMOTE_FILE,
                    hr, 0, hr != BG_E_SESSION_NOT_FOUND && TransferFatal(hr, 0));
    } else
        step = fail_http(run, x, S_OK, r->status);
    LocalFree(code);
    LocalFree(context);
    return step;
}

/* one packet of the BITS upload protocol, its body read from the local file. */
static HRESULT packet(RUN *run, BFILE *file, const WCHAR *url, const WCHAR *type, const WCHAR *session,
                      const WCHAR *range, SOURCE *source, UINT64 offset, UINT64 length, XREQ *x, XFER_RESULT *r)
{
    WCHAR headers[512];
    int n = _snwprintf(headers, ARRAYSIZE(headers), L"BITS-Packet-Type: %s\r\n", type);
    if (session)
        n += _snwprintf(headers + n, ARRAYSIZE(headers) - (size_t)n, L"BITS-Session-Id: %s\r\n", session);
    else if (!wcscmp(type, L"Create-Session"))
        n += _snwprintf(headers + n, ARRAYSIZE(headers) - (size_t)n, L"BITS-Supported-Protocols: %s\r\n", BITS_PROTOCOL);
    if (range)
        _snwprintf(headers + n, ARRAYSIZE(headers) - (size_t)n, L"Content-Range: %s\r\n", range);
    headers[ARRAYSIZE(headers) - 1] = 0;
    TransferRequest(run, file, url, headers, x);
    /* CHttpWriter asks for no encoding, unlike the reader. */
    x->q.identity = FALSE;
    x->q.verb = L"BITS_POST";
    x->sink = source;
    x->q.body_offset = offset;
    x->q.body_length = length;
    x->q.read = length ? read_source : nullptr;
    return TransferRun(x, r);
}

static int ack(RUN *run, XREQ *x, HRESULT hr, const XFER_RESULT *r)
{
    if (hr == E_ABORT && SUCCEEDED(x->cert_error))
        return STEP_STOP;
    if (FAILED(hr))
        return fail_http(run, x, hr, 0);
    if (r->status < 200 || r->status >= 300)
        return server_error(run, x, r);
    return STEP_OK;
}

static void uploaded(RUN *run, BFILE *file, UINT64 acked)
{
    BJOB *job = run->job;
    if (!RunCurrent(run))
        return;
    if (acked > file->transferred) {
        file->transferred = acked;
        job->no_progress_at = 0;
        job->modified = QmFileTime(QmNow());
        NotifyModification(job);
    }
    job->upload_acked = acked;
    /* the total shows with the first bytes the server took. */
    if (file->total == BG_SIZE_UNKNOWN)
        file->total = file->upload_size;
    if (job->state == BG_JOB_STATE_CONNECTING)
        JobSetState(job, BG_JOB_STATE_TRANSFERRING);
}

/* the reply url of BITS-Reply-Url, made absolute against the upload url when the server gave a path. */
static WCHAR *absolute(const WCHAR *base, const WCHAR *reply)
{
    if (QmProtocol(reply) == PROTOCOL_HTTP || QmProtocol(reply) == PROTOCOL_HTTPS)
        return QmDup(reply);
    const WCHAR *host = wcsstr(base, L"://");
    if (!host)
        return nullptr;
    const WCHAR *path = wcschr(host + 3, '/');
    size_t keep = path ? (size_t)(path - base) : wcslen(base);
    WCHAR *url = LocalAlloc(LMEM_FIXED, (keep + wcslen(reply) + 2) * sizeof(WCHAR));
    if (!url)
        return nullptr;
    memcpy(url, base, keep * sizeof(WCHAR));
    url[keep] = 0;
    if (reply[0] != '/')
        wcscat(url, L"/");
    wcscat(url, reply);
    return url;
}

static HRESULT reply_write(void *context, UINT64 offset, const void *data, DWORD size)
{
    XREQ *x = context;
    SOURCE *s = x->sink;
    if (s->run->stop)
        return E_ABORT;
    OVERLAPPED where = {0};
    where.Offset = (DWORD)offset;
    where.OffsetHigh = (DWORD)(offset >> 32);
    DWORD written = 0;
    if (!WriteFile(s->reply, data, size, &written, &where) || written != size)
        return HRESULT_FROM_WIN32(GetLastError());
    BJOB *job = s->run->job;
    Lock();
    if (job->run == s->run)
        job->reply_done += size;
    UINT64 done = job->reply_done;
    Unlock();
    return done > REPLY_MAX ? BG_E_TOO_LARGE : S_OK;
}

static HANDLE open_reply(RUN *run)
{
    BJOB *job = run->job;
    Lock();
    job->reply_started = TRUE;
    job->reply_done = 0;
    job->reply_total = BG_SIZE_UNKNOWN;
    WCHAR *name = QmDup(job->reply_name);
    BOOL impersonating = JobAsOwner(job, TRUE);
    Unlock();
    HANDLE h = name ? CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                    : INVALID_HANDLE_VALUE;
    DWORD error = GetLastError();
    JobBackToSelf(impersonating);
    LocalFree(name);
    SetLastError(error);
    return h;
}

/* the reply of an UPLOAD_REPLY job, downloaded from BITS-Reply-Url into the reply file. */
static int get_reply(RUN *run, BFILE *file, const WCHAR *url)
{
    BJOB *job = run->job;
    SOURCE io = {run, INVALID_HANDLE_VALUE, open_reply(run)};
    if (io.reply == INVALID_HANDLE_VALUE)
        return fail(run, BG_ERROR_CONTEXT_LOCAL_FILE, HRESULT_FROM_WIN32(GetLastError()), 0, TRUE);
    XREQ x;
    TransferRequest(run, file, url, nullptr, &x);
    x.sink = &io;
    x.q.write = reply_write;
    XFER_RESULT r;
    HRESULT hr = TransferRun(&x, &r);
    CloseHandle(io.reply);
    int step = ack(run, &x, hr, &r);
    if (step == STEP_OK && r.status != 200)
        step = fail_http(run, &x, S_OK, r.status);
    if (step == STEP_OK) {
        Lock();
        if (RunCurrent(run))
            job->reply_total = job->reply_done;
        Unlock();
    }
    XferFreeResult(&r);
    TransferFree(&x);
    return step;
}

/* CHttpRfcPostWriter: a custom method sends the whole file in one plain request, whose answer is the reply. */
static int upload_plain(RUN *run, BFILE *file, const WCHAR *url, const WCHAR *method, SOURCE *io, UINT64 size)
{
    BJOB *job = run->job;
    if (job->type == BG_JOB_TYPE_UPLOAD_REPLY) {
        io->reply = open_reply(run);
        if (io->reply == INVALID_HANDLE_VALUE)
            return fail(run, BG_ERROR_CONTEXT_LOCAL_FILE, HRESULT_FROM_WIN32(GetLastError()), 0, TRUE);
    }
    XREQ x;
    TransferRequest(run, file, url, nullptr, &x);
    x.q.verb = method;
    x.q.body_length = size;
    x.q.read = size ? read_source : nullptr;
    x.q.write = io->reply != INVALID_HANDLE_VALUE ? reply_write : nullptr;
    x.sink = io;
    XFER_RESULT r;
    HRESULT hr = TransferRun(&x, &r);
    if (io->reply != INVALID_HANDLE_VALUE) {
        CloseHandle(io->reply);
        io->reply = INVALID_HANDLE_VALUE;
    }
    int step = ack(run, &x, hr, &r);
    if (step == STEP_OK) {
        Lock();
        uploaded(run, file, size);
        if (job->type == BG_JOB_TYPE_UPLOAD_REPLY)
            job->reply_total = job->reply_done;
        Unlock();
    }
    XferFreeResult(&r);
    TransferFree(&x);
    return step;
}

/*
 * CHttpBitsPostWriter: Create-Session, then Fragment packets with Content-Range from what the server acknowledged in
 * BITS-Received-Content-Range, then Close-Session; an UPLOAD_REPLY job then fetches BITS-Reply-Url.
 */
static int upload_bits(RUN *run, BFILE *file, const WCHAR *url, SOURCE *source, UINT64 size)
{
    BJOB *job = run->job;
    XREQ x;
    XFER_RESULT r;
    Lock();
    WCHAR *session = QmDup(job->session_id);
    UINT64 at = job->upload_acked;
    Unlock();
    int step = STEP_OK;
    if (!session) {
        HRESULT hr = packet(run, file, url, L"Create-Session", nullptr, nullptr, source, 0, 0, &x, &r);
        step = ack(run, &x, hr, &r);
        if (step == STEP_OK) {
            WCHAR *protocol = XferHeader(r.headers, L"BITS-Protocol");
            session = XferHeader(r.headers, L"BITS-Session-Id");
            if (!protocol || _wcsicmp(protocol, BITS_PROTOCOL))
                step = fail(run, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_CLIENT_SERVER_PROTOCOL_MISMATCH, 0, TRUE);
            else if (!session)
                step = fail(run, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_INVALID_SERVER_RESPONSE, 0, TRUE);
            LocalFree(protocol);
        }
        Lock();
        if (step == STEP_OK && RunCurrent(run)) {
            QmReplace(&job->session_id, session);
            job->upload_acked = at = 0;
            StoreSaveJob(job);
        }
        Unlock();
        XferFreeResult(&r);
        TransferFree(&x);
    }
    WCHAR *reply = nullptr;
    while (step == STEP_OK) {
        if (run->stop) {
            step = STEP_STOP;
            break;
        }
        UINT64 length = size - at > FRAGMENT_MAX ? FRAGMENT_MAX : size - at;
        WCHAR range[96];
        if (size)
            _snwprintf(range, ARRAYSIZE(range), L"bytes %I64u-%I64u/%I64u", at, at + length - 1, size);
        else
            _snwprintf(range, ARRAYSIZE(range), L"bytes */%I64u", size);
        range[ARRAYSIZE(range) - 1] = 0;
        HRESULT hr = packet(run, file, url, L"Fragment", session, range, source, at, length, &x, &r);
        step = ack(run, &x, hr, &r);
        if (step == STEP_OK) {
            WCHAR *received = XferHeader(r.headers, L"BITS-Received-Content-Range");
            UINT64 next = received ? number(received, 10) : at + length;
            LocalFree(received);
            WCHAR *url_reply = XferHeader(r.headers, L"BITS-Reply-Url");
            if (url_reply) {
                LocalFree(reply);
                reply = url_reply;
            }
            if (next > size)
                step = fail(run, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_INVALID_SERVER_RESPONSE, 0, TRUE);
            else {
                Lock();
                uploaded(run, file, next);
                Unlock();
                at = next;
            }
        }
        XferFreeResult(&r);
        TransferFree(&x);
        if (step != STEP_OK || at >= size)
            break;
    }
    if (step == STEP_OK) {
        HRESULT hr = packet(run, file, url, L"Close-Session", session, nullptr, source, 0, 0, &x, &r);
        step = ack(run, &x, hr, &r);
        if (step == STEP_OK && !reply)
            reply = XferHeader(r.headers, L"BITS-Reply-Url");
        XferFreeResult(&r);
        TransferFree(&x);
    }
    if (step == STEP_OK && job->type == BG_JOB_TYPE_UPLOAD_REPLY && reply) {
        WCHAR *full = absolute(url, reply);
        Lock();
        QmReplace(&job->reply_url, full);
        Unlock();
        step = full ? get_reply(run, file, full) : fail(run, BG_ERROR_CONTEXT_REMOTE_FILE, BG_E_INVALID_SERVER_RESPONSE, 0, TRUE);
        LocalFree(full);
    }
    LocalFree(reply);
    LocalFree(session);
    return step;
}

DWORD WINAPI UploadWorker(void *argument)
{
    RUN *run = argument;
    BJOB *job = run->job;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int step = STEP_FAILED;
    Lock();
    BFILE *file = job->file_count ? job->files[0] : nullptr;
    WCHAR *url = file ? QmDup(file->remote) : nullptr;
    WCHAR *local = file ? QmDup(file->local) : nullptr;
    WCHAR *method = job->custom_method && job->method ? QmDup(job->method) : nullptr;
    UINT64 expected = file ? file->upload_size : 0;
    FILETIME stamp = file ? file->upload_time : (FILETIME){0};
    BOOL impersonating = file && JobAsOwner(job, TRUE);
    Unlock();
    SOURCE source = {run, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    if (local)
        source.handle = CreateFileW(local, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD error = GetLastError();
    JobBackToSelf(impersonating);
    if (!file || !url)
        step = fail(run, BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER, E_OUTOFMEMORY, 0, FALSE);
    else if (source.handle == INVALID_HANDLE_VALUE)
        step = fail(run, BG_ERROR_CONTEXT_LOCAL_FILE, HRESULT_FROM_WIN32(error), 0, TRUE);
    else {
        LARGE_INTEGER size;
        BY_HANDLE_FILE_INFORMATION info;
        /* the file must be the one AddFile saw: same size, same time of last write. */
        if (!GetFileSizeEx(source.handle, &size) || !GetFileInformationByHandle(source.handle, &info) ||
            (UINT64)size.QuadPart != expected || memcmp(&info.ftLastWriteTime, &stamp, sizeof(stamp)))
            step = fail(run, BG_ERROR_CONTEXT_LOCAL_FILE, BG_E_LOCAL_FILE_CHANGED, 0, TRUE);
        else
            step = method ? upload_plain(run, file, url, method, &source, expected)
                          : upload_bits(run, file, url, &source, expected);
    }
    if (source.handle != INVALID_HANDLE_VALUE)
        CloseHandle(source.handle);
    Lock();
    if (step == STEP_OK && RunCurrent(run)) {
        file->done = TRUE;
        job->files_done = 1;
        QmReplace(&job->session_id, nullptr);
        JobTransferred(job);
    }
    Unlock();
    LocalFree(url);
    LocalFree(local);
    LocalFree(method);
    RunFinish(run);
    CoUninitialize();
    return 0;
}

/* CUploadJob::GenerateReplyFile: with no SetReplyFileName the reply goes to a BITxxxx.tmp beside the upload file. */
HRESULT UploadGenerateReply(BJOB *job)
{
    if (job->reply_name || !job->file_count)
        return S_OK;
    WCHAR dir[MAX_PATH], name[MAX_PATH];
    const WCHAR *local = job->files[0]->local;
    if (wcslen(local) >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    wcscpy(dir, local);
    WCHAR *slash = wcsrchr(dir, '\\');
    if (!slash)
        return E_INVALIDARG;
    slash[1] = 0;
    BOOL impersonating = JobAsOwner(job, TRUE);
    UINT made = GetTempFileNameW(dir, L"BITS", 0, name);
    DWORD error = GetLastError();
    JobBackToSelf(impersonating);
    if (!made)
        return HRESULT_FROM_WIN32(error);
    QmReplace(&job->reply_name, name);
    job->reply_generated = TRUE;
    return job->reply_name ? S_OK : E_OUTOFMEMORY;
}

/* the reply was written to its file as it came, so Complete has nothing more to move. */
/*
 * CUploadJob::CommitReplyFile: a whole reply is kept. no reply, as for a plain upload, or a part of one is removed and
 * the job completes partially, BG_S_PARTIAL_COMPLETE.
 */
HRESULT UploadCommitReply(BJOB *job)
{
    if (job->type == BG_JOB_TYPE_UPLOAD_REPLY && job->reply_name && job->reply_started &&
        job->reply_total != BG_SIZE_UNKNOWN && job->reply_done == job->reply_total)
        return S_OK;
    if (job->reply_name && job->reply_started) {
        BOOL impersonating = JobAsOwner(job, TRUE);
        DeleteFileW(job->reply_name);
        JobBackToSelf(impersonating);
    }
    return BG_S_PARTIAL_COMPLETE;
}

typedef struct CANCEL_SESSION {
    WCHAR *url;
    WCHAR *session;
    HANDLE token;
} CANCEL_SESSION;

static DWORD WINAPI cancel_session(void *argument)
{
    CANCEL_SESSION *c = argument;
    WCHAR headers[256];
    _snwprintf(headers, ARRAYSIZE(headers), L"BITS-Packet-Type: Cancel-Session\r\nBITS-Session-Id: %s\r\n", c->session);
    headers[ARRAYSIZE(headers) - 1] = 0;
    XFER_REQUEST q = {0};
    q.url = c->url;
    q.verb = L"BITS_POST";
    q.headers = headers;
    XFER_RESULT r;
    BOOL impersonating = c->token && SetThreadToken(nullptr, c->token);
    HINTERNET session = TransferSession();
    if (session && SUCCEEDED(XferRun(session, &q, &r)))
        XferFreeResult(&r);
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
    if (c->token)
        CloseHandle(c->token);
    LocalFree(c->url);
    LocalFree(c->session);
    LocalFree(c);
    return 0;
}

/* a cancelled upload tells the server with Cancel-Session, on a thread of its own as the lock is held here. */
HRESULT UploadCancelSession(BJOB *job)
{
    if (!job->session_id || !job->file_count)
        return S_OK;
    CANCEL_SESSION *c = LocalAlloc(LMEM_ZEROINIT, sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->url = QmDup(job->files[0]->remote);
    c->session = QmDup(job->session_id);
    if (job->owner_token)
        DuplicateHandle(GetCurrentProcess(), job->owner_token, GetCurrentProcess(), &c->token, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
    if (!c->url || !c->session || FAILED(QmStartThread(cancel_session, c, nullptr))) {
        if (c->token)
            CloseHandle(c->token);
        LocalFree(c->url);
        LocalFree(c->session);
        LocalFree(c);
    }
    return S_OK;
}
