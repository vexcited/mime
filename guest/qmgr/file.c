#include "qmgr.h"

#define FILE_PATH_MAX 260

typedef struct FILE_EXT {
    IBackgroundCopyFile6 iface;
    LONG refs;
    BJOB *job;
    BFILE *file;
} FILE_EXT;

/* ------------------------------------------------------------------ the file */

UINT64 FileRangeTotal(BFILE *file, UINT64 size)
{
    UINT64 total = 0;
    for (DWORD i = 0; i < file->range_count; i++) {
        UINT64 length = file->ranges[i].Length;
        if (length == BG_LENGTH_TO_EOF) {
            if (size == BG_SIZE_UNKNOWN)
                return BG_SIZE_UNKNOWN;
            length = size > file->ranges[i].InitialOffset ? size - file->ranges[i].InitialOffset : 0;
        }
        total += length;
    }
    return total;
}

BFILE *FileCreate(BJOB *job, const WCHAR *remote, const WCHAR *local, DWORD count, const BG_FILE_RANGE *ranges)
{
    BFILE *file = LocalAlloc(LMEM_ZEROINIT, sizeof(*file));
    if (!file)
        return nullptr;
    file->job = job;
    file->remote = QmDup(remote);
    file->local = QmDup(local);
    file->size = BG_SIZE_UNKNOWN;
    file->total = BG_SIZE_UNKNOWN;
    file->resumable = TRUE;
    file->attributes = FILE_ATTRIBUTE_NORMAL;
    if (count) {
        file->ranges = LocalAlloc(LMEM_FIXED, count * sizeof(*ranges));
        if (file->ranges) {
            memcpy(file->ranges, ranges, count * sizeof(*ranges));
            file->range_count = count;
            file->total = FileRangeTotal(file, BG_SIZE_UNKNOWN);
        }
    }
    if (!file->remote || !file->local || (count && !file->ranges)) {
        FileFree(file);
        return nullptr;
    }
    return file;
}

void FileFree(BFILE *file)
{
    if (!file)
        return;
    LocalFree(file->remote);
    LocalFree(file->local);
    LocalFree(file->temp);
    LocalFree(file->ranges);
    LocalFree(file->response);
    QmRangesFree(&file->filled);
    QmRangesFree(&file->wanted);
    QmRangesFree(&file->unreported);
    LocalFree(file);
}

/* the remote half of CFile::CFile: an upload takes http only, and ranges are not for the file protocol. */
HRESULT FileVerifyRemote(BJOB *job, const WCHAR *remote, BOOL ranged)
{
    int protocol = QmProtocol(remote);
    if (protocol == PROTOCOL_NONE)
        return E_INVALIDARG;
    if (protocol == PROTOCOL_FILE && (job->type != BG_JOB_TYPE_DOWNLOAD || ranged))
        return E_INVALIDARG;
    return S_OK;
}

const WCHAR *FileTarget(BFILE *file)
{
    return file->temp ? file->temp : file->local;
}

/*
 * BITSCreateTempFile: GetTempFileNameW makes BITxxxx.tmp beside the destination, which is opened again hidden and
 * written through, so the name is taken from the start.
 */
HRESULT FileCreateTemp(BFILE *file)
{
    WCHAR dir[MAX_PATH], temp[MAX_PATH];
    size_t length = wcslen(file->local);
    if (length >= MAX_PATH)
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    wcscpy(dir, file->local);
    WCHAR *slash = wcsrchr(dir, '\\');
    if (!slash)
        return E_INVALIDARG;
    slash[1] = 0;
    if (!GetTempFileNameW(dir, L"BITS", 0, temp))
        return HRESULT_FROM_WIN32(GetLastError());
    CREATEFILE2_EXTENDED_PARAMETERS params = {sizeof(params)};
    params.dwFileAttributes = FILE_ATTRIBUTE_HIDDEN;
    params.dwFileFlags = FILE_FLAG_WRITE_THROUGH;
    HANDLE h = CreateFile2(temp, GENERIC_WRITE, 0, CREATE_ALWAYS, &params);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        DeleteFileW(temp);
        return HRESULT_FROM_WIN32(error);
    }
    CloseHandle(h);
    QmReplace(&file->temp, temp);
    return file->temp ? S_OK : E_OUTOFMEMORY;
}

HRESULT FileDeleteTemp(BFILE *file)
{
    if (!file->temp)
        return S_OK;
    if (DeleteFileW(file->temp))
        return S_OK;
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return S_OK;
    return HRESULT_FROM_WIN32(error);
}

/* BITSCheckFileWritability: the destination may be missing, else it must be a disk file the owner can write. */
static HRESULT check_writable(const WCHAR *path)
{
    HANDLE h = CreateFile2(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(error);
    }
    DWORD type = GetFileType(h);
    CloseHandle(h);
    return type == FILE_TYPE_DISK ? S_OK : E_INVALIDARG;
}

/*
 * CFile::VerifyLocalName, as the owner: X:\ or \\server, not \\?\, at most MAX_PATH once full. A download makes its
 * temporary file now; an upload opens its file for reading and keeps its size and time.
 */
HRESULT FileVerifyLocal(BFILE *file, const WCHAR *local)
{
    BOOL drive = ((local[0] | 0x20) >= 'a' && (local[0] | 0x20) <= 'z') && local[1] == ':' && local[2] == '\\';
    BOOL unc = local[0] == '\\' && local[1] == '\\';
    if (!drive && !unc)
        return E_INVALIDARG;
    if (!wcsncmp(local, L"\\\\?\\", 4) || !wcsncmp(local, L"\\\\.\\", 4))
        return E_INVALIDARG;
    WCHAR full[FILE_PATH_MAX + 2];
    DWORD length = GetFullPathNameW(local, ARRAYSIZE(full), full, nullptr);
    if (!length || length > FILE_PATH_MAX)
        return E_INVALIDARG;
    QmReplace(&file->local, full);
    if (!file->local)
        return E_OUTOFMEMORY;
    BJOB *job = file->job;
    if (job->type != BG_JOB_TYPE_DOWNLOAD) {
        HANDLE h = CreateFile2(full, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());
        LARGE_INTEGER size;
        BY_HANDLE_FILE_INFORMATION info;
        HRESULT hr = S_OK;
        if (!GetFileSizeEx(h, &size) || !GetFileInformationByHandle(h, &info))
            hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(h);
        if (FAILED(hr))
            return hr;
        file->upload_size = (UINT64)size.QuadPart;
        file->upload_time = info.ftLastWriteTime;
        return S_OK;
    }
    if (!job->high_performance) {
        HRESULT hr = FileCreateTemp(file);
        if (FAILED(hr))
            return hr;
    }
    HRESULT hr = check_writable(full);
    if (FAILED(hr))
        FileDeleteTemp(file);
    return hr;
}

/*
 * CFile::MoveTempFile and CFile::ApplySecurity: the final attributes first, then MoveFileExW ten times, 50 ms doubling,
 * with an early end after four tries for anything but a sharing violation; the old attributes come back on failure.
 */
HRESULT FileCommit(BFILE *file)
{
    BJOB *job = file->job;
    if (file->temp && !file->moved) {
        DWORD old = GetFileAttributesW(file->temp);
        if (old == INVALID_FILE_ATTRIBUTES)
            Sleep(50);
        for (int i = 0; i < 10 && !SetFileAttributesW(file->temp, file->attributes); i++)
            ;
        DWORD error = 0, wait = 50;
        BOOL moved = FALSE;
        for (int i = 0; i < 10; i++) {
            if (MoveFileExW(file->temp, file->local, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                moved = TRUE;
                break;
            }
            error = GetLastError();
            if (i > 3 && error != ERROR_SHARING_VIOLATION)
                break;
            if (i < 9)
                Sleep(wait);
            wait *= 2;
        }
        if (!moved) {
            if (old != INVALID_FILE_ATTRIBUTES)
                SetFileAttributesW(file->temp, old);
            return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
        }
        file->moved = TRUE;
    }
    /* BG_COPY_FILE_OWNER, GROUP, DACL and SACL take the security of the source, which only the file protocol has. */
    DWORD what = job->acl_flags & 0xF;
    if (what && QmProtocol(file->remote) == PROTOCOL_FILE) {
        WCHAR *source = QmFilePath(file->remote);
        DWORD needed = 0;
        if (source && !GetFileSecurityW(source, what, nullptr, 0, &needed) && needed) {
            PSECURITY_DESCRIPTOR sd = LocalAlloc(LMEM_FIXED, needed);
            if (sd && GetFileSecurityW(source, what, sd, needed, &needed) && !SetFileSecurityW(file->local, what, sd)) {
                DWORD error = GetLastError();
                LocalFree(sd);
                LocalFree(source);
                return HRESULT_FROM_WIN32(error);
            }
            LocalFree(sd);
        }
        LocalFree(source);
    }
    file->completed = TRUE;
    return S_OK;
}

void FileProgress(BFILE *file, BG_FILE_PROGRESS *p)
{
    p->BytesTotal = file->total;
    p->BytesTransferred = file->transferred;
    p->Completed = file->completed;
}

/* the transfer of the file starts again from nothing. */
void FileReset(BFILE *file)
{
    file->transferred = 0;
    file->done = FALSE;
    file->told = FALSE;
    file->headed = FALSE;
    file->size = BG_SIZE_UNKNOWN;
    file->last_modified[0] = 0;
    /* an upload says its size again once the server took its first bytes. */
    file->total = file->range_count ? FileRangeTotal(file, BG_SIZE_UNKNOWN) : BG_SIZE_UNKNOWN;
    file->filled.count = 0;
    file->unreported.count = 0;
}

/* ------------------------------------------------------------------ IBackgroundCopyFile 1 to 6 */

#define FX(iface) CONTAINING_RECORD(iface, FILE_EXT, iface)

static HRESULT begin(FILE_EXT *f, DWORD access, CALLER *c, BOOL exclusive)
{
    HRESULT hr = QmEnter();
    if (FAILED(hr))
        return hr;
    hr = CallerOpen(c);
    if (FAILED(hr)) {
        QmLeave();
        return hr;
    }
    if (exclusive)
        Lock();
    else
        LockShared();
    hr = JobCheckAccess(f->job, c, access, nullptr);
    if (FAILED(hr)) {
        if (exclusive)
            Unlock();
        else
            UnlockShared();
        CallerClose(c);
        QmLeave();
    }
    return hr;
}

static HRESULT end(HRESULT hr, CALLER *c, BOOL exclusive)
{
    if (exclusive)
        Unlock();
    else
        UnlockShared();
    CallerClose(c);
    QmLeave();
    return hr;
}

static HRESULT STDMETHODCALLTYPE f_qi(IBackgroundCopyFile6 *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IBackgroundCopyFile) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyFile2) || IsEqualGUID(iid, &IID_IBackgroundCopyFile3) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyFile4) || IsEqualGUID(iid, &IID_IBackgroundCopyFile5) ||
        IsEqualGUID(iid, &IID_IBackgroundCopyFile6)) {
        *out = iface;
        InterlockedIncrement(&FX(iface)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE f_addref(IBackgroundCopyFile6 *iface)
{
    return (ULONG)InterlockedIncrement(&FX(iface)->refs);
}

static ULONG STDMETHODCALLTYPE f_release(IBackgroundCopyFile6 *iface)
{
    FILE_EXT *f = FX(iface);
    LONG refs = InterlockedDecrement(&f->refs);
    if (refs)
        return (ULONG)refs;
    InterlockedCompareExchangePointer(&f->file->ext, nullptr, f);
    JobRelease(f->job);
    LocalFree(f);
    QmObjectRelease();
    return 0;
}

static HRESULT STDMETHODCALLTYPE f_remote(IBackgroundCopyFile6 *iface, LPWSTR *name)
{
    CALLER c;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *name = QmTaskDup(FX(iface)->file->remote);
    return end(*name ? S_OK : E_OUTOFMEMORY, &c, FALSE);
}

static HRESULT STDMETHODCALLTYPE f_local(IBackgroundCopyFile6 *iface, LPWSTR *name)
{
    CALLER c;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *name = QmTaskDup(FX(iface)->file->local);
    return end(*name ? S_OK : E_OUTOFMEMORY, &c, FALSE);
}

static HRESULT STDMETHODCALLTYPE f_progress(IBackgroundCopyFile6 *iface, BG_FILE_PROGRESS *progress)
{
    CALLER c;
    if (!progress)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    FileProgress(FX(iface)->file, progress);
    return end(S_OK, &c, FALSE);
}

static HRESULT copy_ranges(const BG_FILE_RANGE *source, DWORD count, DWORD *out_count, BG_FILE_RANGE **out)
{
    *out_count = 0;
    *out = nullptr;
    if (!count)
        return S_OK;
    *out = CoTaskMemAlloc(count * sizeof(**out));
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, source, count * sizeof(**out));
    *out_count = count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE f_ranges(IBackgroundCopyFile6 *iface, DWORD *count, BG_FILE_RANGE **ranges)
{
    CALLER c;
    if (!count || !ranges)
        return E_INVALIDARG;
    *count = 0;
    *ranges = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    BFILE *file = FX(iface)->file;
    return end(copy_ranges(file->ranges, file->range_count, count, ranges), &c, FALSE);
}

/* CFile::SetRemoteName: the transfer stops and asks the new url for its size again. */
static HRESULT STDMETHODCALLTYPE f_set_remote(IBackgroundCopyFile6 *iface, LPCWSTR name)
{
    CALLER c;
    if (!name)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_WRITE, &c, TRUE);
    if (FAILED(hr))
        return hr;
    BJOB *job = FX(iface)->job;
    BFILE *file = FX(iface)->file;
    if (job->type != BG_JOB_TYPE_DOWNLOAD)
        return end(E_NOTIMPL, &c, TRUE);
    hr = FileVerifyRemote(job, name, file->range_count != 0);
    if (FAILED(hr))
        return end(hr, &c, TRUE);
    BOOL running = job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING;
    if (running) {
        ScheduleInterrupt(job);
        ScheduleWaitIdle(job);
    }
    WCHAR *copy = QmDup(name);
    if (!copy)
        return end(E_OUTOFMEMORY, &c, TRUE);
    LocalFree(file->remote);
    file->remote = copy;
    file->headed = FALSE;
    if (running && (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING))
        JobSetState(job, BG_JOB_STATE_QUEUED);
    JobTouch(job, 1);
    StoreSaveJob(job);
    QmWake();
    return end(S_OK, &c, TRUE);
}

static HRESULT STDMETHODCALLTYPE f_temp(IBackgroundCopyFile6 *iface, LPWSTR *name)
{
    CALLER c;
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *name = QmTaskDup(FileTarget(FX(iface)->file));
    return end(*name ? S_OK : E_OUTOFMEMORY, &c, FALSE);
}

/* CFile::SetValidationState: with no peers a file that fails validation is a fatal error of the local file. */
static HRESULT STDMETHODCALLTYPE f_set_valid(IBackgroundCopyFile6 *iface, BOOL state)
{
    CALLER c;
    if ((DWORD)state > 1)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_WRITE, &c, TRUE);
    if (FAILED(hr))
        return hr;
    BJOB *job = FX(iface)->job;
    BFILE *file = FX(iface)->file;
    if (job->type != BG_JOB_TYPE_DOWNLOAD)
        return end(E_NOTIMPL, &c, TRUE);
    if (!file->done)
        return end(E_PENDING_, &c, TRUE);
    file->valid = state;
    if (!state) {
        if (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING) {
            ScheduleInterrupt(job);
            ScheduleWaitIdle(job);
        }
        if (file->done && job->files_done)
            job->files_done--;
        FileReset(file);
        JobFatalError(job, BG_ERROR_CONTEXT_LOCAL_FILE, BG_E_VALIDATION_FAILED, SOURCE_HRESULT, (LONG)file->index);
    }
    JobTouch(job, 1);
    StoreSaveJob(job);
    return end(S_OK, &c, TRUE);
}

static HRESULT STDMETHODCALLTYPE f_valid(IBackgroundCopyFile6 *iface, BOOL *state)
{
    CALLER c;
    if (!state)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *state = FX(iface)->file->valid;
    return end(S_OK, &c, FALSE);
}

static HRESULT STDMETHODCALLTYPE f_peer(IBackgroundCopyFile6 *iface, BOOL *peer)
{
    CALLER c;
    if (!peer)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *peer = FALSE;
    return end(S_OK, &c, FALSE);
}

static HRESULT STDMETHODCALLTYPE f_peer_stats(IBackgroundCopyFile6 *iface, UINT64 *origin, UINT64 *peers)
{
    CALLER c;
    if (!origin || !peers)
        return E_INVALIDARG;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    *origin = FX(iface)->file->transferred;
    *peers = 0;
    return end(S_OK, &c, FALSE);
}

static HRESULT STDMETHODCALLTYPE f_set_property(IBackgroundCopyFile6 *iface, BITS_FILE_PROPERTY_ID id,
                                                BITS_FILE_PROPERTY_VALUE value)
{
    CALLER c;
    HRESULT hr = begin(FX(iface), BITS_WRITE, &c, TRUE);
    if (FAILED(hr))
        return hr;
    return end(id == BITS_FILE_PROPERTY_ID_HTTP_RESPONSE_HEADERS ? BG_E_READ_ONLY_PROPERTY : BG_E_UNKNOWN_PROPERTY_ID, &c,
               TRUE);
}

/* the headers of the last answer, the empty string before there was one. */
static HRESULT STDMETHODCALLTYPE f_get_property(IBackgroundCopyFile6 *iface, BITS_FILE_PROPERTY_ID id,
                                                BITS_FILE_PROPERTY_VALUE *value)
{
    CALLER c;
    if (!value)
        return E_FAIL;
    value->String = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    if (id != BITS_FILE_PROPERTY_ID_HTTP_RESPONSE_HEADERS)
        return end(BG_E_UNKNOWN_PROPERTY_ID, &c, FALSE);
    BFILE *file = FX(iface)->file;
    value->String = QmTaskDup(file->response ? file->response : L"");
    return end(value->String ? S_OK : E_OUTOFMEMORY, &c, FALSE);
}

/* the restriction of IBackgroundCopyFile6: whole http downloads only. */
static HRESULT random_access(BJOB *job, BFILE *file)
{
    if (job->custom_method)
        return BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD;
    int protocol = QmProtocol(file->remote);
    if (job->type != BG_JOB_TYPE_DOWNLOAD || file->range_count || job->dynamic ||
        (protocol != PROTOCOL_HTTP && protocol != PROTOCOL_HTTPS))
        return BG_E_RANDOM_ACCESS_NOT_SUPPORTED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE f_position(IBackgroundCopyFile6 *iface, UINT64 offset)
{
    CALLER c;
    HRESULT hr = begin(FX(iface), BITS_WRITE, &c, TRUE);
    if (FAILED(hr))
        return hr;
    BJOB *job = FX(iface)->job;
    BFILE *file = FX(iface)->file;
    hr = random_access(job, file);
    if (SUCCEEDED(hr)) {
        file->position = offset;
        QmWake();
    }
    return end(hr, &c, TRUE);
}

static HRESULT STDMETHODCALLTYPE f_request(IBackgroundCopyFile6 *iface, DWORD count, const BG_FILE_RANGE *ranges)
{
    CALLER c;
    HRESULT hr = begin(FX(iface), BITS_WRITE, &c, TRUE);
    if (FAILED(hr))
        return hr;
    BJOB *job = FX(iface)->job;
    BFILE *file = FX(iface)->file;
    hr = random_access(job, file);
    if (SUCCEEDED(hr) && count && !ranges)
        hr = E_INVALIDARG;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++)
        if (ranges[i].Length != BG_LENGTH_TO_EOF && ~ranges[i].InitialOffset < ranges[i].Length)
            hr = BG_E_INVALID_RANGE;
    for (DWORD i = 0; i < count && SUCCEEDED(hr); i++) {
        UINT64 length = ranges[i].Length;
        if (length == BG_LENGTH_TO_EOF)
            length = file->size != BG_SIZE_UNKNOWN && file->size > ranges[i].InitialOffset
                         ? file->size - ranges[i].InitialOffset
                         : ~ranges[i].InitialOffset;
        if (!QmRangesAdd(&file->wanted, ranges[i].InitialOffset, length))
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        QmWake();
    return end(hr, &c, TRUE);
}

static HRESULT STDMETHODCALLTYPE f_filled(IBackgroundCopyFile6 *iface, DWORD *count, BG_FILE_RANGE **ranges)
{
    CALLER c;
    if (!count || !ranges)
        return E_INVALIDARG;
    *count = 0;
    *ranges = nullptr;
    HRESULT hr = begin(FX(iface), BITS_READ, &c, FALSE);
    if (FAILED(hr))
        return hr;
    BFILE *file = FX(iface)->file;
    hr = random_access(FX(iface)->job, file);
    if (SUCCEEDED(hr))
        hr = copy_ranges(file->filled.items, file->filled.count, count, ranges);
    return end(hr, &c, FALSE);
}

static const IBackgroundCopyFile6Vtbl g_file_vtbl = {
    f_qi,         f_addref,       f_release,      f_remote,   f_local,   f_progress, f_ranges,
    f_set_remote, f_temp,         f_set_valid,    f_valid,    f_peer,    f_peer_stats,
    f_set_property, f_get_property, f_position,   f_request,  f_filled,
};

/* CFile::GetFileExternal: one object a file while a client holds it, made under the manager lock. */
HRESULT FileExtFor(BFILE *file, IBackgroundCopyFile **out)
{
    for (;;) {
        FILE_EXT *current = file->ext;
        if (current) {
            LONG refs = current->refs;
            if (refs && InterlockedCompareExchange(&current->refs, refs + 1, refs) == refs) {
                *out = (IBackgroundCopyFile *)&current->iface;
                return S_OK;
            }
            if (refs)
                continue;
        }
        FILE_EXT *f = LocalAlloc(LMEM_ZEROINIT, sizeof(*f));
        if (!f)
            return E_OUTOFMEMORY;
        f->iface.lpVtbl = &g_file_vtbl;
        f->refs = 1;
        f->job = file->job;
        f->file = file;
        if (InterlockedCompareExchangePointer(&file->ext, f, current) != current) {
            LocalFree(f);
            continue;
        }
        JobAddRef(file->job);
        QmObjectAdd();
        *out = (IBackgroundCopyFile *)&f->iface;
        return S_OK;
    }
}

HRESULT FileEnumCreate(BJOB *job, IEnumBackgroundCopyFiles **out)
{
    *out = nullptr;
    IUnknown **items = LocalAlloc(LMEM_FIXED, (job->file_count ? job->file_count : 1) * sizeof(*items));
    if (!items)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    DWORD n = 0;
    for (; n < job->file_count && SUCCEEDED(hr); n++)
        hr = FileExtFor(job->files[n], (IBackgroundCopyFile **)&items[n]);
    if (FAILED(hr))
        n--;
    else
        hr = EnumCreate(&IID_IEnumBackgroundCopyFiles, items, n, (void **)out);
    for (DWORD i = 0; i < n; i++)
        items[i]->lpVtbl->Release(items[i]);
    LocalFree(items);
    return hr;
}
