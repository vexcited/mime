#include "qmgr.h"

#define STORE_DIR L"%ProgramData%\\Microsoft\\Network\\Downloader"
#define STORE_FILE L"qmgrdb.dat"
#define STORE_VERSION 1
#define STORE_SDDL L"D:P(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)"

static SRWLOCK g_store_lock = SRWLOCK_INIT;
static HKEY g_hive;
static HKEY g_jobs;
static WCHAR g_dir[MAX_PATH];

/* ------------------------------------------------------------------ values */

static void put_dword(HKEY key, const WCHAR *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void put_qword(HKEY key, const WCHAR *name, UINT64 value)
{
    RegSetValueExW(key, name, 0, REG_QWORD, (const BYTE *)&value, sizeof(value));
}

static void put_text(HKEY key, const WCHAR *name, const WCHAR *text)
{
    if (text)
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text, (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));
    else
        RegDeleteValueW(key, name);
}

static void put_binary(HKEY key, const WCHAR *name, const void *data, DWORD size)
{
    if (data && size)
        RegSetValueExW(key, name, 0, REG_BINARY, data, size);
    else
        RegDeleteValueW(key, name);
}

static DWORD get_dword(HKEY key, const WCHAR *name, DWORD fallback)
{
    DWORD value, size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ? fallback : value;
}

static UINT64 get_qword(HKEY key, const WCHAR *name, UINT64 fallback)
{
    UINT64 value;
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_QWORD, nullptr, &value, &size) ? fallback : value;
}

static WCHAR *get_text(HKEY key, const WCHAR *name)
{
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) || !size)
        return nullptr;
    WCHAR *text = LocalAlloc(LMEM_FIXED, size);
    if (text && RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text, &size)) {
        LocalFree(text);
        return nullptr;
    }
    return text;
}

static void *get_binary(HKEY key, const WCHAR *name, DWORD *size)
{
    *size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_BINARY, nullptr, nullptr, size) || !*size)
        return nullptr;
    void *data = LocalAlloc(LMEM_FIXED, *size);
    if (data && RegGetValueW(key, nullptr, name, RRF_RT_REG_BINARY, nullptr, data, size)) {
        LocalFree(data);
        *size = 0;
        return nullptr;
    }
    return data;
}

static BOOL get_fixed(HKEY key, const WCHAR *name, void *out, DWORD expected)
{
    DWORD size = expected;
    return !RegGetValueW(key, nullptr, name, RRF_RT_REG_BINARY, nullptr, out, &size) && size == expected;
}

/* ------------------------------------------------------------------ the store */

static void delete_all_files(const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH + 4], path[MAX_PATH + 260];
    _snwprintf(pattern, ARRAYSIZE(pattern), L"%s\\*", dir);
    pattern[ARRAYSIZE(pattern) - 1] = 0;
    WIN32_FIND_DATAW found;
    HANDLE h = FindFirstFileW(pattern, &found);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        _snwprintf(path, ARRAYSIZE(path), L"%s\\%s", dir, found.cFileName);
        path[ARRAYSIZE(path) - 1] = 0;
        DeleteFileW(path);
    } while (FindNextFileW(h, &found));
    FindClose(h);
}

static void make_dir(const WCHAR *dir)
{
    WCHAR path[MAX_PATH];
    wcscpy(path, dir);
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(STORE_SDDL, 1, &sd, nullptr);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};
    size_t length = wcslen(path);
    for (size_t i = 3; i < length; i++) {
        if (path[i] != '\\')
            continue;
        path[i] = 0;
        CreateDirectoryW(path, nullptr);
        path[i] = '\\';
    }
    CreateDirectoryW(path, sd ? &sa : nullptr);
    if (sd)
        LocalFree(sd);
}

static BOOL load_hive(const WCHAR *file)
{
    LSTATUS status = RegLoadAppKeyW(file, &g_hive, KEY_ALL_ACCESS, 0, 0);
    if (status == ERROR_SUCCESS)
        return TRUE;
    g_hive = nullptr;
    return FALSE;
}

/*
 * GlobalInfo::Init and HandleDatabaseCorruption: the folder of Administrators and SYSTEM, emptied once on the start after
 * DatabaseCorrupt was set. The legacy qmgr0.dat and qmgr1.dat cannot be read by this store and go.
 */
void StoreOpen(void)
{
    AcquireSRWLockExclusive(&g_store_lock);
    if (!ExpandEnvironmentStringsW(STORE_DIR, g_dir, ARRAYSIZE(g_dir)) || g_dir[0] == '%')
        ExpandEnvironmentStringsW(L"%SystemDrive%\\ProgramData\\Microsoft\\Network\\Downloader", g_dir, ARRAYSIZE(g_dir));
    make_dir(g_dir);
    if (ConfigGetDword(L"DatabaseCorrupt", 0)) {
        delete_all_files(g_dir);
        ConfigSetDword(L"DatabaseCorrupt", 0);
    }
    WCHAR file[MAX_PATH + 32];
    for (int i = 0; i < 2; i++) {
        _snwprintf(file, ARRAYSIZE(file), L"%s\\qmgr%u.dat", g_dir, i);
        file[ARRAYSIZE(file) - 1] = 0;
        DeleteFileW(file);
        _snwprintf(file, ARRAYSIZE(file), L"%s\\qmgr%u.bak", g_dir, i);
        file[ARRAYSIZE(file) - 1] = 0;
        DeleteFileW(file);
    }
    _snwprintf(file, ARRAYSIZE(file), L"%s\\%s", g_dir, STORE_FILE);
    file[ARRAYSIZE(file) - 1] = 0;
    if (!load_hive(file)) {
        /* a store that does not load is corrupt: it starts again empty. */
        DeleteFileW(file);
        load_hive(file);
    }
    if (g_hive && RegCreateKeyExW(g_hive, L"Jobs", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &g_jobs, nullptr))
        g_jobs = nullptr;
    ConfigSetDword(L"DatabaseVersion", STORE_VERSION);
    ReleaseSRWLockExclusive(&g_store_lock);
}

void StoreClose(void)
{
    AcquireSRWLockExclusive(&g_store_lock);
    if (g_jobs)
        RegCloseKey(g_jobs);
    if (g_hive)
        RegCloseKey(g_hive);
    g_jobs = g_hive = nullptr;
    ReleaseSRWLockExclusive(&g_store_lock);
}

static void job_name(const GUID *id, WCHAR *out)
{
    StringFromGUID2(id, out, 40);
}

/* the job store records a state that runs as QUEUED, as a worker does not outlive the service. */
static void save_file(HKEY files, DWORD index, BFILE *f)
{
    WCHAR name[16];
    _snwprintf(name, ARRAYSIZE(name), L"%u", index);
    name[ARRAYSIZE(name) - 1] = 0;
    HKEY key;
    if (RegCreateKeyExW(files, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr))
        return;
    put_text(key, L"Remote", f->remote);
    put_text(key, L"Local", f->local);
    put_text(key, L"Temp", f->temp);
    put_qword(key, L"Total", f->total);
    put_qword(key, L"Transferred", f->transferred);
    put_qword(key, L"Size", f->size);
    put_qword(key, L"Position", f->position);
    DWORD flags = (f->completed ? 1 : 0) | (f->moved ? 2 : 0) | (f->headed ? 4 : 0) | (f->resumable ? 8 : 0) |
                  (f->done ? 16 : 0) | (f->told ? 32 : 0) | (f->valid ? 64 : 0);
    put_dword(key, L"Flags", flags);
    put_binary(key, L"Ranges", f->ranges, f->range_count * (DWORD)sizeof(BG_FILE_RANGE));
    put_binary(key, L"Filled", f->filled.items, f->filled.count * (DWORD)sizeof(BG_FILE_RANGE));
    put_text(key, L"LastModified", f->last_modified[0] ? f->last_modified : nullptr);
    put_text(key, L"Response", f->response);
    put_qword(key, L"UploadSize", f->upload_size);
    put_qword(key, L"UploadTime", QmTime(&f->upload_time));
    put_dword(key, L"Attributes", f->attributes);
    RegCloseKey(key);
}

/* CJob::TriggerSerialization: every change of a job is written; a high performance job never is. */
void StoreSaveJob(BJOB *job)
{
    if (job->high_performance || job->removed)
        return;
    AcquireSRWLockExclusive(&g_store_lock);
    WCHAR name[40];
    job_name(&job->id, name);
    HKEY key;
    if (!g_jobs || RegCreateKeyExW(g_jobs, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr)) {
        ReleaseSRWLockExclusive(&g_store_lock);
        return;
    }
    BG_JOB_STATE state = job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING
                             ? BG_JOB_STATE_QUEUED
                             : job->state;
    put_qword(key, L"Order", job->order);
    put_dword(key, L"Type", job->type);
    put_dword(key, L"State", state);
    put_dword(key, L"Priority", job->priority);
    put_text(key, L"Name", job->name);
    put_text(key, L"Description", job->description);
    put_binary(key, L"Owner", job->owner, GetLengthSid(job->owner));
    put_text(key, L"Package", job->package);
    put_dword(key, L"OwnerElevated", job->owner_elevated);
    put_dword(key, L"OwnerIntegrity", job->owner_il);
    put_binary(key, L"Security", job->sd, job->sd ? RtlLengthSecurityDescriptor(job->sd) : 0);
    put_dword(key, L"NotifyFlags", job->notify_flags);
    put_binary(key, L"NotifyClsid", &job->notify_clsid, sizeof(GUID));
    put_text(key, L"NotifyProgram", job->cmd_program);
    put_text(key, L"NotifyParameters", job->cmd_params);
    put_dword(key, L"RetryDelay", job->retry_delay);
    put_dword(key, L"NoProgressTimeout", job->no_progress);
    put_dword(key, L"MaxDownloadTime", job->max_time);
    put_dword(key, L"ProxyUsage", job->proxy_usage);
    put_text(key, L"ProxyList", job->proxy_list);
    put_text(key, L"ProxyBypass", job->proxy_bypass);
    put_dword(key, L"ErrorCount", job->error_count);
    put_dword(key, L"RetryCount", job->retry_count);
    put_binary(key, L"Error", &job->error, sizeof(job->error));
    put_qword(key, L"Created", QmTime(&job->created));
    put_qword(key, L"Modified", QmTime(&job->modified));
    put_qword(key, L"Completed", QmTime(&job->completed));
    put_qword(key, L"Accessed", (UINT64)job->accessed);
    put_qword(key, L"RetryAt", job->retry_at);
    put_qword(key, L"NoProgressAt", job->no_progress_at);
    put_qword(key, L"Elapsed", job->elapsed_ms + (job->run_tick ? GetTickCount64() - job->run_tick : 0));
    put_dword(key, L"FilesDone", job->files_done);
    put_dword(key, L"AclFlags", job->acl_flags);
    put_dword(key, L"PeerFlags", job->peer_flags);
    put_dword(key, L"SecurityFlags", job->security_flags);
    put_text(key, L"Headers", job->headers);
    put_text(key, L"Method", job->method);
    put_dword(key, L"CostFlags", job->cost_flags);
    put_dword(key, L"NotifyInterval", job->notify_interval);
    put_qword(key, L"MaxSize", job->max_size);
    put_dword(key, L"PowerFlags", job->power_flags);
    put_dword(key, L"StoredCredentials", job->stored_creds);
    put_dword(key, L"MediaFlags", job->media_flags);
    put_dword(key, L"HelperFlags", job->helper_flags);
    DWORD flags = (job->headers_write_only ? 1 : 0) | (job->custom_method ? 2 : 0) | (job->cost_explicit ? 4 : 0) |
                  (job->dynamic ? 8 : 0) | (job->on_demand ? 16 : 0) | (job->resumed ? 32 : 0) |
                  (job->ranges_added ? 64 : 0) | (job->has_validator ? 128 : 0) | (job->has_cert ? 256 : 0) |
                  (job->cert_by_hash ? 512 : 0) | (job->reply_generated ? 1024 : 0) | (job->reply_started ? 2048 : 0) |
                  (job->legacy ? 4096 : 0) | (job->legacy_job ? 8192 : 0);
    put_dword(key, L"Flags", flags);
    put_dword(key, L"CertLocation", job->cert_location);
    put_text(key, L"CertStore", job->cert_store);
    put_binary(key, L"CertHash", job->cert_by_hash ? job->cert_hash : nullptr, sizeof(job->cert_hash));
    put_text(key, L"CertSubject", job->cert_subject);
    put_text(key, L"ReplyName", job->reply_name);
    put_qword(key, L"ReplyTotal", job->reply_total);
    put_qword(key, L"ReplyDone", job->reply_done);
    put_text(key, L"ReplyUrl", job->reply_url);
    put_text(key, L"SessionId", job->session_id);
    put_qword(key, L"UploadAcked", job->upload_acked);
    put_binary(key, L"LegacyJobId", &job->legacy_job_id, sizeof(GUID));
    put_binary(key, L"LegacyClsid", &job->legacy_clsid, sizeof(GUID));
    put_dword(key, L"PendingReason", job->pending_reason);
    RegDeleteTreeW(key, L"Credentials");
    HKEY creds;
    if (job->cred_count && !RegCreateKeyExW(key, L"Credentials", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &creds, nullptr)) {
        for (DWORD i = 0; i < job->cred_count; i++) {
            WCHAR index[16];
            HKEY one;
            _snwprintf(index, ARRAYSIZE(index), L"%u", i);
            index[ARRAYSIZE(index) - 1] = 0;
            if (RegCreateKeyExW(creds, index, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &one, nullptr))
                continue;
            put_dword(one, L"Target", job->creds[i].target);
            put_dword(one, L"Scheme", job->creds[i].scheme);
            put_text(one, L"User", job->creds[i].user);
            put_binary(one, L"Secret", job->creds[i].secret, job->creds[i].secret_size);
            RegCloseKey(one);
        }
        RegCloseKey(creds);
    }
    RegDeleteTreeW(key, L"Files");
    HKEY files;
    if (job->file_count && !RegCreateKeyExW(key, L"Files", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &files, nullptr)) {
        for (DWORD i = 0; i < job->file_count; i++)
            save_file(files, i, job->files[i]);
        RegCloseKey(files);
    }
    RegCloseKey(key);
    ReleaseSRWLockExclusive(&g_store_lock);
}

void StoreDeleteJob(BJOB *job)
{
    AcquireSRWLockExclusive(&g_store_lock);
    WCHAR name[40];
    job_name(&job->id, name);
    if (g_jobs)
        RegDeleteTreeW(g_jobs, name);
    ReleaseSRWLockExclusive(&g_store_lock);
}

/* ------------------------------------------------------------------ loading */

static void load_ranges(HKEY key, const WCHAR *name, BG_FILE_RANGE **items, DWORD *count)
{
    DWORD size;
    *items = get_binary(key, name, &size);
    *count = *items ? size / (DWORD)sizeof(BG_FILE_RANGE) : 0;
}

static BFILE *load_file(BJOB *job, HKEY key)
{
    WCHAR *remote = get_text(key, L"Remote"), *local = get_text(key, L"Local");
    BFILE *f = remote && local ? FileCreate(job, remote, local, 0, nullptr) : nullptr;
    LocalFree(remote);
    LocalFree(local);
    if (!f)
        return nullptr;
    f->temp = get_text(key, L"Temp");
    f->total = get_qword(key, L"Total", BG_SIZE_UNKNOWN);
    f->transferred = get_qword(key, L"Transferred", 0);
    f->size = get_qword(key, L"Size", BG_SIZE_UNKNOWN);
    f->position = get_qword(key, L"Position", 0);
    DWORD flags = get_dword(key, L"Flags", 0);
    f->completed = (flags & 1) != 0;
    f->moved = (flags & 2) != 0;
    f->headed = (flags & 4) != 0;
    f->resumable = (flags & 8) != 0;
    f->done = (flags & 16) != 0;
    f->told = (flags & 32) != 0;
    f->valid = (flags & 64) != 0;
    load_ranges(key, L"Ranges", &f->ranges, &f->range_count);
    load_ranges(key, L"Filled", &f->filled.items, &f->filled.count);
    f->filled.capacity = f->filled.count;
    WCHAR *modified = get_text(key, L"LastModified");
    if (modified && wcslen(modified) < ARRAYSIZE(f->last_modified))
        wcscpy(f->last_modified, modified);
    LocalFree(modified);
    f->response = get_text(key, L"Response");
    f->upload_size = get_qword(key, L"UploadSize", 0);
    f->upload_time = QmFileTime(get_qword(key, L"UploadTime", 0));
    f->attributes = get_dword(key, L"Attributes", FILE_ATTRIBUTE_NORMAL);
    return f;
}

static void load_creds(BJOB *job, HKEY key)
{
    HKEY creds;
    if (RegOpenKeyExW(key, L"Credentials", 0, KEY_READ, &creds))
        return;
    for (DWORD i = 0; job->cred_count < MAX_CREDS; i++) {
        WCHAR index[16];
        DWORD length = ARRAYSIZE(index);
        if (RegEnumKeyExW(creds, i, index, &length, nullptr, nullptr, nullptr, nullptr))
            break;
        HKEY one;
        if (RegOpenKeyExW(creds, index, 0, KEY_READ, &one))
            continue;
        BCRED *c = &job->creds[job->cred_count++];
        c->target = (BG_AUTH_TARGET)get_dword(one, L"Target", BG_AUTH_TARGET_SERVER);
        c->scheme = (BG_AUTH_SCHEME)get_dword(one, L"Scheme", BG_AUTH_SCHEME_BASIC);
        c->user = get_text(one, L"User");
        c->secret = get_binary(one, L"Secret", &c->secret_size);
        RegCloseKey(one);
    }
    RegCloseKey(creds);
}

static void load_files(BJOB *job, HKEY key)
{
    HKEY files;
    if (RegOpenKeyExW(key, L"Files", 0, KEY_READ, &files))
        return;
    DWORD count = 0;
    RegQueryInfoKeyW(files, nullptr, nullptr, nullptr, &count, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr);
    job->files = LocalAlloc(LMEM_ZEROINIT, (count ? count : 1) * sizeof(*job->files));
    job->file_capacity = job->files ? (count ? count : 1) : 0;
    for (DWORD i = 0; job->files && i < count; i++) {
        WCHAR index[16];
        HKEY one;
        _snwprintf(index, ARRAYSIZE(index), L"%u", i);
        index[ARRAYSIZE(index) - 1] = 0;
        if (RegOpenKeyExW(files, index, 0, KEY_READ, &one))
            break;
        BFILE *f = load_file(job, one);
        RegCloseKey(one);
        if (!f)
            break;
        f->index = job->file_count;
        job->files[job->file_count++] = f;
    }
    RegCloseKey(files);
}

static BJOB *load_job(HKEY key, const GUID *id)
{
    BJOB *job = LocalAlloc(LMEM_ZEROINIT, sizeof(*job));
    if (!job)
        return nullptr;
    job->refs = 1;
    job->id = *id;
    DWORD size;
    void *owner = get_binary(key, L"Owner", &size);
    if (!owner || size > sizeof(job->owner_buffer) || !IsValidSid(owner)) {
        LocalFree(owner);
        LocalFree(job);
        return nullptr;
    }
    memcpy(job->owner_buffer, owner, size);
    LocalFree(owner);
    job->owner = job->owner_buffer;
    if (!ConvertSidToStringSidW(job->owner, &job->owner_text))
        job->owner_text = nullptr;
    job->sd = get_binary(key, L"Security", &size);
    job->order = get_qword(key, L"Order", 0);
    job->type = (BG_JOB_TYPE)get_dword(key, L"Type", BG_JOB_TYPE_DOWNLOAD);
    job->state = (BG_JOB_STATE)get_dword(key, L"State", BG_JOB_STATE_SUSPENDED);
    if (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING)
        job->state = BG_JOB_STATE_QUEUED;
    job->priority = (BG_JOB_PRIORITY)get_dword(key, L"Priority", BG_JOB_PRIORITY_NORMAL);
    job->name = get_text(key, L"Name");
    job->description = get_text(key, L"Description");
    job->package = get_text(key, L"Package");
    job->owner_elevated = get_dword(key, L"OwnerElevated", 0);
    job->owner_il = get_dword(key, L"OwnerIntegrity", 0x2000);
    job->notify_flags = get_dword(key, L"NotifyFlags", BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR);
    get_fixed(key, L"NotifyClsid", &job->notify_clsid, sizeof(GUID));
    job->cmd_program = get_text(key, L"NotifyProgram");
    job->cmd_params = get_text(key, L"NotifyParameters");
    job->retry_delay = get_dword(key, L"RetryDelay", g_config.retry_delay);
    job->no_progress = get_dword(key, L"NoProgressTimeout", g_config.no_progress);
    job->max_time = get_dword(key, L"MaxDownloadTime", g_config.max_download_time);
    job->proxy_usage = (BG_JOB_PROXY_USAGE)get_dword(key, L"ProxyUsage", 0);
    job->proxy_list = get_text(key, L"ProxyList");
    job->proxy_bypass = get_text(key, L"ProxyBypass");
    job->error_count = get_dword(key, L"ErrorCount", 0);
    job->retry_count = get_dword(key, L"RetryCount", 0);
    get_fixed(key, L"Error", &job->error, sizeof(job->error));
    job->created = QmFileTime(get_qword(key, L"Created", 0));
    job->modified = QmFileTime(get_qword(key, L"Modified", 0));
    job->completed = QmFileTime(get_qword(key, L"Completed", 0));
    job->accessed = (LONG64)get_qword(key, L"Accessed", QmNow());
    job->retry_at = get_qword(key, L"RetryAt", 0);
    job->no_progress_at = get_qword(key, L"NoProgressAt", 0);
    job->elapsed_ms = get_qword(key, L"Elapsed", 0);
    job->files_done = get_dword(key, L"FilesDone", 0);
    job->acl_flags = get_dword(key, L"AclFlags", 0);
    job->peer_flags = get_dword(key, L"PeerFlags", 0);
    job->security_flags = get_dword(key, L"SecurityFlags", 0);
    job->headers = get_text(key, L"Headers");
    job->method = get_text(key, L"Method");
    job->cost_flags = get_dword(key, L"CostFlags", 0x800000FF);
    job->notify_interval = get_dword(key, L"NotifyInterval", 0);
    job->max_size = get_qword(key, L"MaxSize", 0);
    job->power_flags = get_dword(key, L"PowerFlags", 0);
    job->stored_creds = get_dword(key, L"StoredCredentials", 0);
    job->media_flags = get_dword(key, L"MediaFlags", 0);
    job->helper_flags = get_dword(key, L"HelperFlags", 0);
    DWORD flags = get_dword(key, L"Flags", 0);
    job->headers_write_only = (flags & 1) != 0;
    job->custom_method = (flags & 2) != 0;
    job->cost_explicit = (flags & 4) != 0;
    job->dynamic = (flags & 8) != 0;
    job->on_demand = (flags & 16) != 0;
    job->resumed = (flags & 32) != 0;
    job->ranges_added = (flags & 64) != 0;
    job->has_validator = (flags & 128) != 0;
    job->has_cert = (flags & 256) != 0;
    job->cert_by_hash = (flags & 512) != 0;
    job->reply_generated = (flags & 1024) != 0;
    job->reply_started = (flags & 2048) != 0;
    job->legacy = (flags & 4096) != 0;
    job->legacy_job = (flags & 8192) != 0;
    job->cert_location = (BG_CERT_STORE_LOCATION)get_dword(key, L"CertLocation", 0);
    job->cert_store = get_text(key, L"CertStore");
    get_fixed(key, L"CertHash", job->cert_hash, sizeof(job->cert_hash));
    job->cert_subject = get_text(key, L"CertSubject");
    job->reply_name = get_text(key, L"ReplyName");
    job->reply_total = get_qword(key, L"ReplyTotal", 0);
    job->reply_done = get_qword(key, L"ReplyDone", 0);
    job->reply_url = get_text(key, L"ReplyUrl");
    job->session_id = get_text(key, L"SessionId");
    job->upload_acked = get_qword(key, L"UploadAcked", 0);
    get_fixed(key, L"LegacyJobId", &job->legacy_job_id, sizeof(GUID));
    get_fixed(key, L"LegacyClsid", &job->legacy_clsid, sizeof(GUID));
    job->pending_reason = get_dword(key, L"PendingReason", 0);
    load_creds(job, key);
    load_files(job, key);
    if (!job->name)
        job->name = QmDup(L"");
    if (!job->description)
        job->description = QmDup(L"");
    if (!job->sd && FAILED(JobSecurityCreate(job->owner, job->owner_il, &job->sd))) {
        JobRelease(job);
        return nullptr;
    }
    HANDLE session = SessionTokenFor(job->owner);
    if (session) {
        if (!DuplicateTokenEx(session, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_IMPERSONATION_,
                              &job->owner_token))
            job->owner_token = nullptr;
        CloseHandle(session);
    }
    return job;
}

/*
 * CJobManager::UnserializeDatabaseState and Startup: every job comes back offline, in its order, and those of service
 * accounts and of users who are logged on go online; the timers come back from their stored times.
 */
void StoreLoadJobs(void)
{
    AcquireSRWLockShared(&g_store_lock);
    HKEY jobs = g_jobs;
    BJOB **loaded = nullptr;
    DWORD count = 0, capacity = 0;
    for (DWORD i = 0; jobs; i++) {
        WCHAR name[64];
        DWORD length = ARRAYSIZE(name);
        if (RegEnumKeyExW(jobs, i, name, &length, nullptr, nullptr, nullptr, nullptr))
            break;
        GUID id;
        HKEY key;
        if (FAILED(CLSIDFromString(name, &id)) || RegOpenKeyExW(jobs, name, 0, KEY_READ, &key))
            continue;
        BJOB *job = load_job(key, &id);
        RegCloseKey(key);
        if (!job)
            continue;
        if (count == capacity) {
            DWORD more = capacity ? capacity * 2 : 16;
            BJOB **list = LocalAlloc(LMEM_FIXED, more * sizeof(*list));
            if (!list) {
                JobRelease(job);
                break;
            }
            if (count)
                memcpy(list, loaded, count * sizeof(*list));
            LocalFree(loaded);
            loaded = list;
            capacity = more;
        }
        loaded[count++] = job;
    }
    ReleaseSRWLockShared(&g_store_lock);
    for (DWORD i = 1; i < count; i++) {
        BJOB *job = loaded[i];
        DWORD k = i;
        for (; k && loaded[k - 1]->order > job->order; k--)
            loaded[k] = loaded[k - 1];
        loaded[k] = job;
    }
    Lock();
    for (DWORD i = 0; i < count; i++) {
        BJOB *job = loaded[i];
        JobLink(job, OwnerLoggedOn(job->owner));
    }
    Unlock();
    LocalFree(loaded);
}
