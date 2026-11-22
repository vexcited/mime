#include "dosvc.h"

#define AGENT L"Microsoft-Delivery-Optimization/10.1"
#define REQUEST_TIMEOUT 15000
#define MAX_FILE_SIZE (500ull << 30)
#define NO_PROGRESS_BACKGROUND 10800
#define NO_PROGRESS_FOREGROUND 3600
#define RETRY_MIN_MS 3000
#define RETRY_MAX_MS 240000

static SRWLOCK g_list_lock = SRWLOCK_INIT;
static DO_JOB *g_jobs;

/* the type each property takes, VT_EMPTY for those the download object handles itself. */
static const VARTYPE g_types[P_COUNT] = {
    VT_BSTR, VT_BSTR, VT_BSTR, VT_BSTR, VT_BSTR, VT_BSTR, VT_UI4, VT_UI4, VT_UI4, VT_UI4, VT_UI4,
    VT_BOOL, VT_BOOL, VT_EMPTY, VT_EMPTY, VT_EMPTY, VT_BOOL, VT_BSTR, VT_BSTR, VT_BSTR, VT_BOOL, VT_UI8,
    VT_BOOL, VT_BSTR, VT_BSTR, VT_BOOL, VT_BOOL, VT_BOOL, VT_BSTR, VT_BSTR, VT_BSTR, VT_UI4,
};

/* what a transferring job lets change; a job that is paused or transferred lets the uri change too. */
static BOOL settable_while_running(int id)
{
    switch (id) {
    case P_HEADERS:
    case P_COST:
    case P_SECURITY_FLAGS:
    case P_FREQ_PERCENT:
    case P_FREQ_SECONDS:
    case P_NO_PROGRESS:
    case P_FOREGROUND:
    case P_NETWORK_TOKEN:
    case P_NO_CELLULAR:
    case P_AUTH_HEADERS:
    case P_INSECURE_REDIRECT:
    case P_NON_VOLATILE:
        return TRUE;
    default:
        return FALSE;
    }
}

void JobListLock(BOOL exclusive)
{
    if (exclusive)
        AcquireSRWLockExclusive(&g_list_lock);
    else
        AcquireSRWLockShared(&g_list_lock);
}

void JobListUnlock(BOOL exclusive)
{
    if (exclusive)
        ReleaseSRWLockExclusive(&g_list_lock);
    else
        ReleaseSRWLockShared(&g_list_lock);
}

DO_JOB *JobList(void)
{
    return g_jobs;
}

BOOL JobsIdle(void)
{
    JobListLock(FALSE);
    BOOL idle = !g_jobs;
    JobListUnlock(FALSE);
    return idle;
}

static void unlink_job(DO_JOB *job)
{
    JobListLock(TRUE);
    for (DO_JOB **p = &g_jobs; *p; p = &(*p)->next) {
        if (*p == job) {
            *p = job->next;
            job->next = nullptr;
            JobListUnlock(TRUE);
            JobRelease(job);
            return;
        }
    }
    JobListUnlock(TRUE);
}

static WCHAR *dup(const WCHAR *text)
{
    if (!text)
        return nullptr;
    size_t size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = LocalAlloc(LMEM_FIXED, size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static void replace(WCHAR **slot, const WCHAR *text)
{
    if (*slot)
        LocalFree(*slot);
    *slot = dup(text);
}

static BOOL bool_prop(DO_JOB *job, int id, BOOL fallback)
{
    return V_VT(&job->props[id]) == VT_BOOL ? V_BOOL(&job->props[id]) == VARIANT_TRUE : fallback;
}

static ULONG u4_prop(DO_JOB *job, int id, ULONG fallback)
{
    return V_VT(&job->props[id]) == VT_UI4 ? V_UI4(&job->props[id]) : fallback;
}

static const WCHAR *str_prop(DO_JOB *job, int id)
{
    return V_VT(&job->props[id]) == VT_BSTR ? V_BSTR(&job->props[id]) : nullptr;
}

static ULONG no_progress_seconds(DO_JOB *job)
{
    return u4_prop(job, P_NO_PROGRESS, bool_prop(job, P_FOREGROUND, FALSE) ? NO_PROGRESS_FOREGROUND : NO_PROGRESS_BACKGROUND);
}

static void set_response(DO_JOB *job, const XFER_RESULT *r)
{
    if (r->status < 200 || r->status >= 300)
        return;
    VariantClear(&job->props[P_RESPONSE_HEADERS]);
    VariantClear(&job->props[P_SERVER_ADDRESS]);
    VariantClear(&job->props[P_STATUS_CODE]);
    if (r->headers) {
        V_VT(&job->props[P_RESPONSE_HEADERS]) = VT_BSTR;
        V_BSTR(&job->props[P_RESPONSE_HEADERS]) = SysAllocString(r->headers);
    }
    if (r->address[0]) {
        V_VT(&job->props[P_SERVER_ADDRESS]) = VT_BSTR;
        V_BSTR(&job->props[P_SERVER_ADDRESS]) = SysAllocString(r->address);
    }
    V_VT(&job->props[P_STATUS_CODE]) = VT_UI4;
    V_UI4(&job->props[P_STATUS_CODE]) = r->status;
    if (r->final_url && job->remote && wcscmp(r->final_url, job->remote)) {
        VariantClear(&job->props[P_REDIRECT_TARGET]);
        V_VT(&job->props[P_REDIRECT_TARGET]) = VT_BSTR;
        V_BSTR(&job->props[P_REDIRECT_TARGET]) = SysAllocString(r->final_url);
    }
}

/* ------------------------------------------------------------------ the correlation vector, MS-CV */

static void cv_init(DO_JOB *job)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    BYTE random[12];
    GUID g;
    CoCreateGuid(&g);
    memcpy(random, &g, sizeof(random));
    int n = 0;
    for (int i = 0; i < 12; i += 3) {
        DWORD v = (DWORD)random[i] << 16 | (DWORD)random[i + 1] << 8 | random[i + 2];
        for (int k = 3; k >= 0; k--)
            job->cv_base[n++] = alphabet[(v >> (6 * k)) & 63];
    }
    job->cv_base[n] = 0;
    job->cv_counter = 0;
}

/* a vector is base64 characters, then dot separated numbers; the last one is the counter that each use moves. */
static HRESULT cv_set(DO_JOB *job, const WCHAR *text)
{
    const WCHAR *dot = wcsrchr(text, '.');
    if (!dot || dot == text || !dot[1] || dot - text >= (ptrdiff_t)ARRAYSIZE(job->cv_base))
        return E_INVALIDARG;
    for (const WCHAR *p = text; p < dot; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '+' ||
              *p == '/' || *p == '.'))
            return E_INVALIDARG;
    ULONG counter = 0;
    for (const WCHAR *p = dot + 1; *p; p++) {
        if (*p < '0' || *p > '9')
            return E_INVALIDARG;
        counter = counter * 10 + (ULONG)(*p - '0');
    }
    memcpy(job->cv_base, text, (size_t)(dot - text) * sizeof(WCHAR));
    job->cv_base[dot - text] = 0;
    job->cv_counter = counter;
    return S_OK;
}

static void cv_next(DO_JOB *job, WCHAR *out, size_t count)
{
    _snwprintf(out, count, L"%s.%u", job->cv_base, ++job->cv_counter);
    out[count - 1] = 0;
}

/* ------------------------------------------------------------------ lifetime */

HRESULT JobCreate(DO_JOB **out)
{
    DO_JOB *job = LocalAlloc(LMEM_ZEROINIT, sizeof(*job));
    if (!job)
        return E_OUTOFMEMORY;
    job->refs = 2;
    InitializeSRWLock(&job->lock);
    CoCreateGuid(&job->id);
    for (int i = 0; i < P_COUNT; i++)
        VariantInit(&job->props[i]);
    job->owner_sid = XferCallerSid();
    job->blocking = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    GetSystemTimeAsFileTime(&job->created);
    cv_init(job);
    JobListLock(TRUE);
    job->next = g_jobs;
    g_jobs = job;
    JobListUnlock(TRUE);
    *out = job;
    return S_OK;
}

void JobAddRef(DO_JOB *job)
{
    InterlockedIncrement(&job->refs);
}

void JobRelease(DO_JOB *job)
{
    if (InterlockedDecrement(&job->refs))
        return;
    for (int i = 0; i < P_COUNT; i++)
        VariantClear(&job->props[i]);
    if (job->stream)
        job->stream->lpVtbl->Release(job->stream);
    if (job->client_cert)
        CertFreeCertificateContext(job->client_cert);
    if (job->owner_token)
        CloseHandle(job->owner_token);
    if (job->owner_sid)
        CoTaskMemFree(job->owner_sid);
    if (job->blocking)
        CloseHandle(job->blocking);
    if (job->cancel)
        CloseHandle(job->cancel);
    LocalFree(job->ranges);
    LocalFree(job->remote);
    LocalFree(job->local);
    LocalFree(job->temp);
    LocalFree(job);
}

BOOL JobVisible(DO_JOB *job)
{
    WCHAR *sid = XferCallerSid();
    BOOL visible = sid && job->owner_sid && !_wcsicmp(sid, job->owner_sid);
    if (sid)
        CoTaskMemFree(sid);
    return visible || XferCallerIsAdmin();
}

static void id_text(const GUID *g, WCHAR *out, size_t count)
{
    _snwprintf(out, count, L"%08x-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    out[count - 1] = 0;
}

BOOL JobMatches(DO_JOB *job, int id, const WCHAR *value)
{
    AcquireSRWLockShared(&job->lock);
    BOOL match = FALSE;
    if (id == P_ID) {
        WCHAR text[40];
        id_text(&job->id, text, ARRAYSIZE(text));
        match = !wcscmp(text, value);
    } else {
        const WCHAR *mine = str_prop(job, id);
        match = mine && !wcscmp(mine, value);
    }
    ReleaseSRWLockShared(&job->lock);
    return match;
}

/* ------------------------------------------------------------------ files, as the owner */

static BOOL as_owner(DO_JOB *job)
{
    return XferRunAs(job->owner_token);
}

static void back_to_self(void)
{
    XferRunAsSelf();
}

/* the temporary file, DOxxxx.tmp next to the destination and sparse, written until the download is finalized. */
static HRESULT create_file(DO_JOB *job)
{
    const WCHAR *target = job->local;
    WCHAR temp[MAX_PATH];
    BOOL impersonating = as_owner(job);
    HRESULT hr = S_OK;
    if (bool_prop(job, P_TEMP_FILE, !job->bits)) {
        WCHAR dir[MAX_PATH];
        size_t length = wcslen(target);
        if (length >= MAX_PATH) {
            hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            goto done;
        }
        wcscpy(dir, target);
        WCHAR *slash = wcsrchr(dir, '\\');
        if (slash)
            slash[1] = 0;
        else
            wcscpy(dir, L".");
        if (!GetTempFileNameW(dir, L"DO", 0, temp)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto done;
        }
        target = temp;
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 10; attempt++) {
        file = CreateFileW(target, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION)
            break;
        Sleep(500);
    }
    if (file == INVALID_HANDLE_VALUE) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }
    DWORD returned;
    DeviceIoControl(file, FSCTL_SET_SPARSE_, nullptr, 0, nullptr, 0, &returned, nullptr);
    CloseHandle(file);
    if (target == temp)
        replace(&job->temp, temp);
done:
    if (impersonating)
        back_to_self();
    return hr;
}

static void delete_file(DO_JOB *job)
{
    const WCHAR *path = job->temp ? job->temp : job->local;
    if (!path || !job->has_file)
        return;
    BOOL impersonating = as_owner(job);
    DeleteFileW(path);
    if (impersonating)
        back_to_self();
}

/* the caller's token, which files are made, renamed and deleted with. */
static void take_owner_token(DO_JOB *job)
{
    HANDLE token = XferCallerToken();
    if (!token)
        return;
    if (job->owner_token)
        CloseHandle(job->owner_token);
    job->owner_token = token;
}

/* ------------------------------------------------------------------ the transfer */

typedef struct SINK {
    DO_JOB *job;
    HANDLE file;
    UINT64 base;
    UINT64 segment_start;
} SINK;

static void progress(DO_JOB *job, UINT64 bytes)
{
    AcquireSRWLockExclusive(&job->lock);
    job->transferred += bytes;
    job->progress_tick = GetTickCount64();
    BOOL notify = job->notify_first;
    ULONG percent = u4_prop(job, P_FREQ_PERCENT, 0);
    if (!notify && percent && job->size_known && job->total)
        notify = (job->transferred - job->notify_bytes) * 100 / job->total >= percent;
    else if (!notify && !percent)
        notify = GetTickCount64() - job->notify_tick >= u4_prop(job, P_FREQ_SECONDS, 1) * 1000ull;
    if (notify) {
        job->notify_first = FALSE;
        job->notify_tick = GetTickCount64();
        job->notify_bytes = job->transferred;
    }
    ReleaseSRWLockExclusive(&job->lock);
    if (notify)
        ExtNotify(job);
}

static HRESULT sink_write(void *context, UINT64 offset, const void *data, DWORD size)
{
    SINK *s = context;
    DO_JOB *job = s->job;
    if (s->file != INVALID_HANDLE_VALUE) {
        OVERLAPPED at = {0};
        UINT64 where = s->base + (offset - s->segment_start);
        at.Offset = (DWORD)where;
        at.OffsetHigh = (DWORD)(where >> 32);
        DWORD written;
        if (!WriteFile(s->file, data, size, &written, &at) || written != size)
            return HRESULT_FROM_WIN32(GetLastError());
    } else {
        ISequentialStream *stream = job->stream;
        if (!stream)
            return DO_E_FILE_DOWNLOADSINK_UNSPECIFIED;
        ULONG written = 0;
        HRESULT hr = stream->lpVtbl->Write(stream, data, size, &written);
        if (FAILED(hr))
            return hr;
    }
    progress(job, size);
    return S_OK;
}

/* the size of the file is known once an answer says it, from its range or its length. */
static void headers_ready(void *context, const XFER_RESULT *r)
{
    SINK *s = context;
    DO_JOB *job = s->job;
    if (r->status != 200 && r->status != 206)
        return;
    AcquireSRWLockExclusive(&job->lock);
    if (!job->size_known && r->total != XFER_TO_END) {
        job->total = r->total;
        job->size_known = TRUE;
    }
    set_response(job, r);
    ReleaseSRWLockExclusive(&job->lock);
}

static HRESULT status_result(DWORD status)
{
    switch (status) {
    case 200:
    case 201:
    case 202:
    case 203:
    case 204:
    case 206:
        return S_OK;
    case 300:
    case 301:
    case 302:
    case 303:
    case 304:
    case 307:
    case 308:
        return (HRESULT)0x80190003L;
    case 305:
    case 504:
        return (HRESULT)0x801901F6L;
    case 400:
    case 405:
    case 409:
    case 411:
    case 412:
    case 414:
    case 415:
        return (HRESULT)0x80190190L;
    case 410:
        return (HRESULT)0x80190194L;
    case 505:
        return (HRESULT)0x801901F5L;
    default:
        if ((status >= 401 && status <= 416) || (status >= 500 && status <= 503))
            return (HRESULT)(0x80190000L | status);
        return (HRESULT)0x80190001L;
    }
}

/* a server that wants a certificate the job cannot show will not change its mind on a retry. */
static BOOL fatal_winhttp(HRESULT hr)
{
    return hr == HRESULT_FROM_WIN32(ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED) || hr == HRESULT_FROM_WIN32(12185) ||
           hr == HRESULT_FROM_WIN32(12186);
}

/* client errors are the server's last word on the url; timeouts and server errors are tried again. */
static BOOL fatal_status(DWORD status)
{
    return status >= 400 && status < 500 && status != 408 && status != 429;
}

typedef struct SEGMENT {
    UINT64 offset;
    UINT64 length;
} SEGMENT;

/* each request has a session of its own, as CHttpDownloader::_DoSendRequest makes a CHttpAgent for each. */
static HRESULT run_request(const XFER_REQUEST *q, XFER_RESULT *r)
{
    HINTERNET s = XferOpenSession(AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, REQUEST_TIMEOUT);
    if (!s)
        s = XferOpenSession(AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, REQUEST_TIMEOUT);
    if (s) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    HRESULT hr = XferRun(s, q, r);
    if (s)
        WinHttpCloseHandle(s);
    return hr;
}

static void request_for(DO_JOB *job, XFER_REQUEST *q, WCHAR *headers, size_t count, HANDLE cancel)
{
    memset(q, 0, sizeof(*q));
    q->url = job->remote;
    q->cancel = cancel;
    WCHAR cv[80];
    cv_next(job, cv, ARRAYSIZE(cv));
    const WCHAR *custom = str_prop(job, P_HEADERS);
    _snwprintf(headers, count, L"MS-CV: %s\r\n%s", cv, custom ? custom : L"");
    headers[count - 1] = 0;
    q->headers = headers;
    if (job->client_cert) {
        q->client_cert = job->client_cert;
        q->client_cert_size = sizeof(CERT_CONTEXT);
    }
    ULONG flags = u4_prop(job, P_SECURITY_FLAGS, 0);
    if (flags == 1)
        q->revocation = TRUE;
    else
        q->security_flags = flags;
    if (V_VT(&job->props[P_INSECURE_REDIRECT]) == VT_BOOL) {
        q->has_redirect_policy = TRUE;
        q->redirect_policy = bool_prop(job, P_INSECURE_REDIRECT, FALSE) ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
                                                                        : WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    }
}

/* the size of what the url names, from the headers of a HEAD: 0 when the server does not say. */
static HRESULT probe_size(DO_JOB *job, HANDLE cancel, UINT64 *size, XFER_RESULT *last)
{
    WCHAR headers[4096];
    XFER_REQUEST q;
    AcquireSRWLockExclusive(&job->lock);
    request_for(job, &q, headers, ARRAYSIZE(headers), cancel);
    ReleaseSRWLockExclusive(&job->lock);
    q.verb = L"HEAD";
    XFER_RESULT r;
    BOOL impersonating = job->client_cert && as_owner(job);
    HRESULT hr = run_request(&q, &r);
    if (impersonating)
        back_to_self();
    *size = 0;
    if (SUCCEEDED(hr)) {
        hr = status_result(r.status);
        if (SUCCEEDED(hr) && r.has_length)
            *size = r.length;
    }
    if (last) {
        XferFreeResult(last);
        *last = r;
    } else
        XferFreeResult(&r);
    return hr;
}

/* the part of the file a range download keeps, the requested ranges one after another; a whole download is one segment. */
static UINT segments(DO_JOB *job, SEGMENT *out, UINT max)
{
    if (job->range_count && !job->ranges_queued) {
        UINT n = 0;
        for (UINT i = 0; i < job->range_count && n < max; i++) {
            UINT64 length = job->ranges[i].Length;
            UINT64 size = job->size_known ? job->total : job->probed;
            if (length == DO_LENGTH_TO_EOF && size)
                length = size > job->ranges[i].Offset ? size - job->ranges[i].Offset : 0;
            out[n].offset = job->ranges[i].Offset;
            out[n++].length = length;
        }
        return n;
    }
    out[0].offset = 0;
    out[0].length = job->size_known ? job->total : job->probed ? job->probed : XFER_TO_END;
    return 1;
}

static DWORD backoff(int failures)
{
    DWORD a = 1, b = 1;
    for (int i = 0; i < failures && i < 15; i++) {
        DWORD c = a + b;
        a = b;
        b = c;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    DWORD wait = 1000 * (DWORD)(t.LowPart % (a + 1));
    return wait < RETRY_MIN_MS ? RETRY_MIN_MS : wait > RETRY_MAX_MS ? RETRY_MAX_MS : wait;
}

static void finish(DO_JOB *job, HANDLE cancel, int state, HRESULT error, HRESULT ext)
{
    AcquireSRWLockExclusive(&job->lock);
    BOOL current = job->cancel == cancel && WaitForSingleObject(cancel, 0) == WAIT_TIMEOUT;
    if (current) {
        job->state = state;
        job->error = error;
        job->ext_error = ext;
        SetEvent(job->blocking);
    }
    ReleaseSRWLockExclusive(&job->lock);
    if (current) {
        if (state != JOB_TRANSFERRED)
            ExtNotify(job);
        if (bool_prop(job, P_NON_VOLATILE, FALSE))
            StoreSaveJob(job);
    }
}

typedef struct RUN {
    DO_JOB *job;
    HANDLE cancel;
} RUN;

static DWORD WINAPI worker(void *arg)
{
    RUN *run = arg;
    DO_JOB *job = run->job;
    HANDLE cancel = run->cancel;
    LocalFree(run);
    HRESULT last = S_OK;
    int failures = 0;
    XFER_RESULT r = {0};
    for (;;) {
        if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0)
            break;
        AcquireSRWLockShared(&job->lock);
        ULONGLONG idle = GetTickCount64() - job->progress_tick;
        ULONG limit = no_progress_seconds(job);
        ReleaseSRWLockShared(&job->lock);
        if (idle >= limit * 1000ull) {
            finish(job, cancel, JOB_ERROR, DO_E_DOWNLOAD_NO_PROGRESS, last);
            break;
        }
        /* a size nobody gave yet comes with the answer to a plain GET of the whole file. */
        HRESULT hr = S_OK;
        SINK sink = {job, INVALID_HANDLE_VALUE};
        AcquireSRWLockShared(&job->lock);
        BOOL to_file = job->local != nullptr;
        const WCHAR *path = job->temp ? job->temp : job->local;
        ReleaseSRWLockShared(&job->lock);
        if (to_file) {
            BOOL impersonating = as_owner(job);
            sink.file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
            if (impersonating)
                back_to_self();
            if (sink.file == INVALID_HANDLE_VALUE) {
                finish(job, cancel, JOB_ERROR, DO_E_FATAL_FILE_, HRESULT_FROM_WIN32(GetLastError()));
                break;
            }
        }
        SEGMENT parts[64];
        AcquireSRWLockShared(&job->lock);
        UINT count = segments(job, parts, ARRAYSIZE(parts));
        UINT64 done = job->transferred;
        BOOL ranged = job->range_count != 0;
        ReleaseSRWLockShared(&job->lock);
        UINT64 base = 0;
        BOOL complete = TRUE;
        for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
            UINT64 length = parts[i].length;
            if (length != XFER_TO_END && done >= base + length) {
                base += length;
                continue;
            }
            UINT64 skip = done > base ? done - base : 0;
            WCHAR headers[4096];
            XFER_REQUEST q;
            AcquireSRWLockExclusive(&job->lock);
            request_for(job, &q, headers, ARRAYSIZE(headers), cancel);
            ReleaseSRWLockExclusive(&job->lock);
            q.range = ranged || length != XFER_TO_END || skip;
            q.offset = parts[i].offset + skip;
            q.length = length == XFER_TO_END ? XFER_TO_END : length - skip;
            q.write = sink_write;
            q.headers_ready = headers_ready;
            q.context = &sink;
            sink.base = base + skip;
            sink.segment_start = q.offset;
            XferFreeResult(&r);
            /* with a client certificate the requests run as the download's owner, whose key signs for it. */
            BOOL impersonating = job->client_cert && as_owner(job);
            hr = run_request(&q, &r);
            if (impersonating)
                back_to_self();
            if (r.status) {
                AcquireSRWLockExclusive(&job->lock);
                set_response(job, &r);
                ReleaseSRWLockExclusive(&job->lock);
            }
            if (FAILED(hr))
                break;
            hr = status_result(r.status);
            if (FAILED(hr))
                break;
            if (r.status == 204)
                hr = DO_E_UNEXPECTED_STATUS_;
            else if (q.range && r.status == 206 && r.has_range &&
                     (r.range_first != q.offset ||
                      (q.length != XFER_TO_END && r.range_last != q.offset + q.length - 1)))
                hr = DO_E_INVALID_RANGE_;
            else if (q.range && r.status != 206 && (ranged || q.offset))
                hr = DO_E_NO_RANGE_SUPPORT_;
            if (FAILED(hr))
                break;
            UINT64 got = r.received;
            if (length == XFER_TO_END) {
                AcquireSRWLockExclusive(&job->lock);
                if (!ranged) {
                    job->total = job->transferred;
                    job->size_known = TRUE;
                }
                ReleaseSRWLockExclusive(&job->lock);
            } else if (skip + got < length) {
                complete = FALSE;
                hr = DO_E_SIZE_MISMATCH_;
                break;
            }
            base += length == XFER_TO_END ? skip + got : length;
        }
        if (sink.file != INVALID_HANDLE_VALUE)
            CloseHandle(sink.file);
        if (hr == E_ABORT)
            break;
        if (SUCCEEDED(hr) && complete) {
            finish(job, cancel, JOB_TRANSFERRED, S_OK, S_OK);
            break;
        }
        if (r.status && fatal_status(r.status) && FAILED(status_result(r.status))) {
            finish(job, cancel, JOB_ERROR, hr, S_OK);
            break;
        }
        if (hr == DO_E_NO_RANGE_SUPPORT_ || hr == DO_E_INVALID_RANGE_ || hr == DO_E_UNEXPECTED_STATUS_ || fatal_winhttp(hr) ||
            (HRESULT_FACILITY(hr) == FACILITY_WIN32 &&
             (HRESULT_CODE(hr) == ERROR_DISK_FULL || HRESULT_CODE(hr) == ERROR_ACCESS_DENIED))) {
            finish(job, cancel, JOB_ERROR, hr, S_OK);
            break;
        }
        last = hr;
        if (SUCCEEDED(hr))
            failures = failures > 0 ? failures / 2 : 0;
        else
            failures++;
        DWORD wait = r.retry_after ? r.retry_after * 1000 : backoff(failures);
        if (WaitForSingleObject(cancel, wait) == WAIT_OBJECT_0)
            break;
    }
    XferFreeResult(&r);
    JobRelease(job);
    return 0;
}

/* a fresh cancel event for each run, so a stopped worker cannot touch the next one's state. */
static HRESULT start_worker(DO_JOB *job)
{
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    RUN *run = LocalAlloc(LMEM_FIXED, sizeof(*run));
    if (!cancel || !run) {
        if (cancel)
            CloseHandle(cancel);
        LocalFree(run);
        return E_OUTOFMEMORY;
    }
    if (job->cancel)
        CloseHandle(job->cancel);
    job->cancel = cancel;
    run->job = job;
    run->cancel = cancel;
    JobAddRef(job);
    HANDLE thread = CreateThread(nullptr, 0, worker, run, 0, nullptr);
    if (!thread) {
        JobRelease(job);
        LocalFree(run);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    job->worker = thread;
    return S_OK;
}

/* called with the lock held, which it drops while the worker ends, as the worker takes it to report progress. */
static void stop_worker(DO_JOB *job)
{
    HANDLE thread = job->worker;
    if (!thread)
        return;
    job->worker = nullptr;
    SetEvent(job->cancel);
    ReleaseSRWLockExclusive(&job->lock);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    AcquireSRWLockExclusive(&job->lock);
}

static BOOL closed(DO_JOB *job)
{
    return job->finalizing || job->state == JOB_FINALIZED || job->state == JOB_ABORTED;
}

static HRESULT resume(DO_JOB *job)
{
    stop_worker(job);
    take_owner_token(job);
    if (!job->has_file)
        return DO_E_NO_FILE_;
    job->error = S_OK;
    job->ext_error = S_OK;
    ResetEvent(job->blocking);
    BOOL complete = job->size_known && job->transferred >= job->total && !job->ranges_queued;
    if (job->range_count && !job->ranges_queued) {
        UINT64 want = 0;
        for (UINT i = 0; i < job->range_count; i++)
            want += job->ranges[i].Length;
        complete = job->transferred >= want;
    }
    job->state = complete && job->size_known ? JOB_TRANSFERRED : JOB_TRANSFERRING;
    job->ranges_queued = FALSE;
    job->notify_first = TRUE;
    job->progress_tick = GetTickCount64();
    if (job->state == JOB_TRANSFERRING) {
        HRESULT hr = start_worker(job);
        if (FAILED(hr))
            job->error = hr;
    } else
        SetEvent(job->blocking);
    return S_OK;
}

static const WCHAR *url_file_name(const WCHAR *url, WCHAR *out, size_t count)
{
    const WCHAR *end = url + wcslen(url);
    const WCHAR *q = wcschr(url, '?');
    if (q)
        end = q;
    const WCHAR *start = end;
    while (start > url && start[-1] != '/')
        start--;
    size_t n = (size_t)(end - start);
    if (n >= count)
        n = count - 1;
    memcpy(out, start, n * sizeof(WCHAR));
    out[n] = 0;
    return out;
}

HRESULT JobStart(DO_JOB *job)
{
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    BOOL notify = FALSE;
    if (closed(job)) {
        hr = DO_E_INVALID_STATE;
        goto done;
    }
    switch (job->state) {
    case JOB_CREATED: {
        if (job->has_file) {
            hr = DO_E_INVALID_STATE;
            goto done;
        }
        const WCHAR *uri = str_prop(job, P_URI), *local = str_prop(job, P_LOCAL_PATH);
        if (!uri && !str_prop(job, P_CONTENT_ID)) {
            hr = DO_E_DOWNLOAD_NO_URI;
            goto done;
        }
        if (!local && !job->stream) {
            hr = DO_E_FILE_DOWNLOADSINK_UNSPECIFIED;
            goto done;
        }
        if (!uri) {
            hr = DO_E_DOWNLOAD_NO_URI;
            goto done;
        }
        if (local) {
            WCHAR path[MAX_PATH + 1];
            size_t length = wcslen(local);
            if (length && local[length - 1] == '\\') {
                WCHAR name[MAX_PATH];
                url_file_name(uri, name, ARRAYSIZE(name));
                if (length + wcslen(name) > 259) {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
                    goto done;
                }
                wcscpy(path, local);
                wcscat(path, name);
            } else if (length > 259) {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
                goto done;
            } else
                wcscpy(path, local);
            replace(&job->local, path);
        }
        replace(&job->remote, uri);
        take_owner_token(job);
        if (job->local) {
            hr = create_file(job);
            if (FAILED(hr))
                goto done;
        }
        job->has_file = TRUE;
        hr = resume(job);
        notify = TRUE;
        break;
    }
    case JOB_TRANSFERRING:
        break;
    case JOB_TRANSFERRED:
        if (job->ranges_queued) {
            stop_worker(job);
            job->transferred = 0;
            hr = resume(job);
            notify = TRUE;
        }
        break;
    default:
        if (str_prop(job, P_URI))
            replace(&job->remote, str_prop(job, P_URI));
        hr = resume(job);
        notify = TRUE;
        break;
    }
done:;
    BOOL wait = SUCCEEDED(hr) && bool_prop(job, P_BLOCKING, FALSE);
    ReleaseSRWLockExclusive(&job->lock);
    if (notify)
        ExtNotify(job);
    if (bool_prop(job, P_NON_VOLATILE, FALSE))
        StoreSaveJob(job);
    if (wait) {
        WaitForSingleObject(job->blocking, INFINITE);
        AcquireSRWLockShared(&job->lock);
        hr = job->error;
        ReleaseSRWLockShared(&job->lock);
    }
    return hr;
}

HRESULT JobSetRanges(DO_JOB *job, const DO_DOWNLOAD_RANGE *ranges, UINT count)
{
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    if (closed(job))
        hr = DO_E_INVALID_STATE;
    else if (!count) {
        if (job->range_count && job->state != JOB_CREATED)
            hr = DO_E_RANGES_ALREADY_;
    } else if (job->state == JOB_TRANSFERRING)
        hr = DO_E_INVALID_STATE;
    else if (job->state != JOB_CREATED && !job->range_count)
        hr = DO_E_RANGES_NOT_NOW_;
    if (SUCCEEDED(hr) && count) {
        DO_DOWNLOAD_RANGE *copy = LocalAlloc(LMEM_FIXED, count * sizeof(*copy));
        if (!copy)
            hr = E_OUTOFMEMORY;
        else {
            memcpy(copy, ranges, count * sizeof(*copy));
            LocalFree(job->ranges);
            job->ranges = copy;
            job->range_count = count;
            job->ranges_queued = job->state != JOB_CREATED;
        }
    }
    ReleaseSRWLockExclusive(&job->lock);
    return hr;
}

HRESULT JobPause(DO_JOB *job)
{
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    if (closed(job))
        hr = DO_E_INVALID_STATE;
    else if (job->state != JOB_CREATED) {
        stop_worker(job);
        if (!closed(job)) {
            job->state = JOB_PAUSED;
            SetEvent(job->blocking);
        }
    }
    ReleaseSRWLockExclusive(&job->lock);
    if (SUCCEEDED(hr) && bool_prop(job, P_NON_VOLATILE, FALSE))
        StoreSaveJob(job);
    return hr;
}

static void close_job(DO_JOB *job, int state)
{
    job->state = state;
    if (job->stream) {
        job->stream->lpVtbl->Release(job->stream);
        job->stream = nullptr;
    }
    SetEvent(job->blocking);
}

HRESULT JobAbort(DO_JOB *job)
{
    AcquireSRWLockExclusive(&job->lock);
    if (closed(job)) {
        ReleaseSRWLockExclusive(&job->lock);
        return DO_E_INVALID_STATE;
    }
    job->finalizing = TRUE;
    stop_worker(job);
    delete_file(job);
    close_job(job, JOB_ABORTED);
    ReleaseSRWLockExclusive(&job->lock);
    StoreDeleteJob(job);
    unlink_job(job);
    return S_OK;
}

HRESULT JobFinalize(DO_JOB *job)
{
    AcquireSRWLockExclusive(&job->lock);
    /* a bits job completes in any live state; what it has not transferred is deleted, as bits does. */
    if (job->bits && !closed(job) && job->state != JOB_TRANSFERRED) {
        job->finalizing = TRUE;
        stop_worker(job);
        delete_file(job);
        close_job(job, JOB_FINALIZED);
        ReleaseSRWLockExclusive(&job->lock);
        StoreDeleteJob(job);
        unlink_job(job);
        return S_OK;
    }
    if (closed(job) || job->state != JOB_TRANSFERRED) {
        ReleaseSRWLockExclusive(&job->lock);
        return DO_E_INVALID_STATE;
    }
    job->finalizing = TRUE;
    stop_worker(job);
    HRESULT hr = S_OK;
    if (job->temp) {
        BOOL impersonating = as_owner(job);
        if (!MoveFileExW(job->temp, job->local, MOVEFILE_REPLACE_EXISTING))
            hr = HRESULT_FROM_WIN32(GetLastError());
        if (impersonating)
            back_to_self();
    }
    if (FAILED(hr)) {
        job->finalizing = FALSE;
        ReleaseSRWLockExclusive(&job->lock);
        return hr;
    }
    close_job(job, JOB_FINALIZED);
    ReleaseSRWLockExclusive(&job->lock);
    StoreDeleteJob(job);
    unlink_job(job);
    return S_OK;
}

void JobStatus(DO_JOB *job, DO_DOWNLOAD_STATUS *status)
{
    static const DODownloadState map[] = {
        DODownloadState_Created, DODownloadState_Transferring, DODownloadState_Transferred, DODownloadState_Finalized,
        DODownloadState_Aborted, DODownloadState_Paused, DODownloadState_Paused, DODownloadState_Paused,
    };
    memset(status, 0, sizeof(*status));
    AcquireSRWLockShared(&job->lock);
    /* the total counts once an answer gave the file's size, for a range download as for a whole one. */
    if (job->range_count && !job->ranges_queued && job->size_known) {
        for (UINT i = 0; i < job->range_count; i++)
            status->BytesTotal += job->ranges[i].Length == DO_LENGTH_TO_EOF && job->size_known
                                      ? job->total - job->ranges[i].Offset
                                      : job->ranges[i].Length;
    } else if (job->size_known)
        status->BytesTotal = job->total;
    status->BytesTransferred = job->transferred;
    status->State = job->state < (int)ARRAYSIZE(map) ? map[job->state] : DODownloadState_Aborted;
    /* the errors that wait for the network or its cost show as the extended error only. */
    HRESULT error = job->error;
    if (FAILED(error) && (error & 0x1FFF0000) == 0xD00000 && (error & 0xF800) == 0x3800) {
        status->Error = S_OK;
        status->ExtendedError = error;
    } else {
        status->Error = error;
        status->ExtendedError = job->ext_error;
    }
    ReleaseSRWLockShared(&job->lock);
}

/* ------------------------------------------------------------------ properties */

HRESULT JobGetProperty(DO_JOB *job, int id, VARIANT *value)
{
    if (id < 0 || id >= P_COUNT)
        return DO_E_UNKNOWN_PROPERTY_ID;
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    if (closed(job)) {
        hr = DO_E_INVALID_STATE;
        goto done;
    }
    switch (id) {
    case P_NETWORK_TOKEN:
    case P_DECRYPTION:
    case P_INTEGRITY:
    case P_SECURITY_CONTEXT:
        hr = DO_E_WRITE_ONLY_PROPERTY;
        goto done;
    case P_ID: {
        WCHAR text[40];
        id_text(&job->id, text, ARRAYSIZE(text));
        V_VT(value) = VT_BSTR;
        V_BSTR(value) = SysAllocString(text);
        goto done;
    }
    case P_CV: {
        WCHAR text[80];
        cv_next(job, text, ARRAYSIZE(text));
        V_VT(value) = VT_BSTR;
        V_BSTR(value) = SysAllocString(text);
        goto done;
    }
    case P_TOTAL_SIZE:
        if (job->size_known || job->probed) {
            V_VT(value) = VT_UI8;
            V_UI8(value) = job->size_known ? job->total : job->probed;
            goto done;
        }
        if (V_VT(&job->props[P_TOTAL_SIZE]) == VT_UI8)
            break;
        if (!job->has_file && str_prop(job, P_URI)) {
            /* a synchronous size request for a download that has no file yet, kept for the next ask. */
            replace(&job->remote, str_prop(job, P_URI));
            ReleaseSRWLockExclusive(&job->lock);
            UINT64 size = 0;
            HRESULT probe = probe_size(job, nullptr, &size, nullptr);
            AcquireSRWLockExclusive(&job->lock);
            if (FAILED(probe)) {
                hr = probe;
                goto done;
            }
            if (size) {
                job->probed = size;
                V_VT(value) = VT_UI8;
                V_UI8(value) = size;
                goto done;
            }
        }
        break;
    case P_COST:
        if (V_VT(&job->props[id]) == VT_EMPTY) {
            V_VT(value) = VT_UI4;
            V_UI4(value) = bool_prop(job, P_FOREGROUND, FALSE) ? DODownloadCostPolicy_Always : DODownloadCostPolicy_Standard;
            goto done;
        }
        break;
    case P_SECURITY_FLAGS:
    case P_FREQ_SECONDS:
    case P_NO_PROGRESS:
        if (V_VT(&job->props[id]) == VT_EMPTY) {
            V_VT(value) = VT_UI4;
            V_UI4(value) = id == P_SECURITY_FLAGS ? 0 : id == P_FREQ_SECONDS ? 1 : no_progress_seconds(job);
            goto done;
        }
        break;
    case P_FOREGROUND:
    case P_BLOCKING:
    case P_INTEGRITY_MANDATORY:
    case P_NO_CELLULAR:
    case P_NON_VOLATILE:
    case P_TEMP_FILE:
        if (V_VT(&job->props[id]) == VT_EMPTY) {
            V_VT(value) = VT_BOOL;
            V_BOOL(value) = id == P_TEMP_FILE && !job->bits ? VARIANT_TRUE : VARIANT_FALSE;
            goto done;
        }
        break;
    }
    if (V_VT(&job->props[id]) == VT_EMPTY)
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    else
        hr = VariantCopy(value, &job->props[id]);
done:
    ReleaseSRWLockExclusive(&job->lock);
    return hr;
}

static HRESULT check_value(DO_JOB *job, int id, const VARIANT *value)
{
    switch (id) {
    case P_URI:
        return XferCheckUrl(V_BSTR(value));
    case P_LOCAL_PATH: {
        const WCHAR *path = V_BSTR(value);
        size_t length = wcslen(path);
        if (path[length - 1] == '\\')
            return S_OK;
        HRESULT hr = XferImpersonateCaller();
        if (FAILED(hr))
            return hr;
        DWORD attributes = GetFileAttributesW(path);
        CoRevertToSelf();
        return attributes != INVALID_FILE_ATTRIBUTES ? HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) : S_OK;
    }
    case P_COST:
        return V_UI4(value) < DODownloadCostPolicy_NoCellular ? S_OK : E_INVALIDARG;
    case P_SECURITY_FLAGS:
        return V_UI4(value) == 1 || !(V_UI4(value) & ~0x3300u) ? S_OK : E_INVALIDARG;
    case P_FREQ_PERCENT:
        return V_UI4(value) >= 1 && V_UI4(value) <= 100 ? S_OK : E_INVALIDARG;
    case P_FREQ_SECONDS:
        return V_UI4(value) ? S_OK : E_INVALIDARG;
    case P_NO_PROGRESS:
        return V_UI4(value) >= 60 ? S_OK : E_INVALIDARG;
    case P_TOTAL_SIZE:
        if (!V_UI8(value))
            return E_INVALIDARG;
        return V_UI8(value) <= MAX_FILE_SIZE ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    case P_CV: {
        WCHAR base[48];
        ULONG counter = job->cv_counter;
        memcpy(base, job->cv_base, sizeof(base));
        HRESULT hr = cv_set(job, V_BSTR(value));
        memcpy(job->cv_base, base, sizeof(base));
        job->cv_counter = counter;
        return hr;
    }
    default:
        return S_OK;
    }
}

HRESULT JobSetProperty(DO_JOB *job, int id, const VARIANT *value)
{
    if (id < 0 || id >= P_COUNT)
        return DO_E_UNKNOWN_PROPERTY_ID;
    if (id == P_ID || id >= P_REDIRECT_TARGET)
        return DO_E_READ_ONLY_PROPERTY;
    VARTYPE type = g_types[id];
    if (type == VT_EMPTY)
        return E_UNEXPECTED;
    if (V_VT(value) != type)
        return E_INVALIDARG;
    if (type == VT_BSTR && (!V_BSTR(value) || !*V_BSTR(value)) && !(id == P_DISPLAY_NAME && job->bits))
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    if (closed(job))
        hr = DO_E_INVALID_STATE;
    else if (job->state == JOB_TRANSFERRING && !settable_while_running(id))
        hr = DO_E_INVALID_STATE;
    else if (job->state != JOB_CREATED && job->state != JOB_TRANSFERRING && !settable_while_running(id) && id != P_URI)
        hr = DO_E_INVALID_STATE;
    if (SUCCEEDED(hr))
        hr = check_value(job, id, value);
    if (SUCCEEDED(hr)) {
        if (id == P_CV)
            cv_set(job, V_BSTR(value));
        if (id == P_TOTAL_SIZE && !job->size_known) {
            job->total = V_UI8(value);
            job->size_known = TRUE;
        }
        if (id == P_BLOCKING && V_BOOL(value) != VARIANT_TRUE)
            SetEvent(job->blocking);
        hr = VariantCopy(&job->props[id], value);
    }
    ReleaseSRWLockExclusive(&job->lock);
    if (SUCCEEDED(hr) && id == P_NON_VOLATILE) {
        if (V_BOOL(value) == VARIANT_TRUE)
            StoreSaveJob(job);
        else
            StoreDeleteJob(job);
    }
    return hr;
}

/* the certificate the job's requests show a server that asks for one; none clears it. the job takes the reference. */
void JobSetClientCertificate(DO_JOB *job, PCCERT_CONTEXT cert)
{
    AcquireSRWLockExclusive(&job->lock);
    PCCERT_CONTEXT old = job->client_cert;
    job->client_cert = cert;
    ReleaseSRWLockExclusive(&job->lock);
    if (old)
        CertFreeCertificateContext(old);
}

HRESULT JobSetStream(DO_JOB *job, ISequentialStream *stream)
{
    AcquireSRWLockExclusive(&job->lock);
    HRESULT hr = S_OK;
    if (closed(job))
        hr = DO_E_INVALID_STATE;
    else if (stream && job->has_file && job->local)
        hr = DO_E_SINK_SET_;
    else {
        if (stream)
            stream->lpVtbl->AddRef(stream);
        if (job->stream)
            job->stream->lpVtbl->Release(job->stream);
        job->stream = stream;
    }
    ReleaseSRWLockExclusive(&job->lock);
    return hr;
}

/* the service stops: every worker ends and the jobs that persist are written as they are. */
void JobShutdown(void)
{
    for (;;) {
        JobListLock(TRUE);
        DO_JOB *job = g_jobs;
        if (job)
            g_jobs = job->next;
        JobListUnlock(TRUE);
        if (!job)
            break;
        AcquireSRWLockExclusive(&job->lock);
        stop_worker(job);
        ReleaseSRWLockExclusive(&job->lock);
        if (bool_prop(job, P_NON_VOLATILE, FALSE))
            StoreSaveJob(job);
        JobRelease(job);
    }
}
