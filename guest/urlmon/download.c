/*
 * URLDownloadToFile, URLDownloadToCacheFile, URLOpenBlockingStream and URLOpenStream bind a url through wininet or
 * the file system on the caller's thread. like windows' urlmon, the caller's IBindStatusCallback gets OnStartBinding
 * with an IBinding that aborts, OnProgress with BINDSTATUS codes, OnDataAvailable for pushed streams, then
 * OnStopBinding.
 */
#include "urlmon_private.h"

#define URL_CHUNK 65536

/* ------------------------------------------------------------------ data stream */

typedef struct {
    IStream Interface;
    LONG volatile Refs;
    SRWLOCK Lock;
    BYTE *Data;
    ULONGLONG Size;
    ULONGLONG Capacity;
    ULONGLONG Position;
} DATA_STREAM;

static DATA_STREAM *stream_of(IStream *iface)
{
    return (DATA_STREAM *)iface;
}

static HRESULT STDMETHODCALLTYPE ds_QueryInterface(IStream *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IStream) ||
        IsEqualIID(riid, &IID_ISequentialStream)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ds_AddRef(IStream *iface)
{
    return (ULONG)InterlockedIncrement(&stream_of(iface)->Refs);
}

static ULONG STDMETHODCALLTYPE ds_Release(IStream *iface)
{
    DATA_STREAM *stream = stream_of(iface);
    LONG refs = InterlockedDecrement(&stream->Refs);
    if (!refs) {
        UrlFree(stream->Data);
        UrlFree(stream);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ds_Read(IStream *iface, void *buffer, ULONG size, ULONG *read)
{
    DATA_STREAM *stream = stream_of(iface);
    AcquireSRWLockExclusive(&stream->Lock);
    ULONGLONG left = stream->Position < stream->Size ? stream->Size - stream->Position : 0;
    ULONG count = left < size ? (ULONG)left : size;
    memcpy(buffer, stream->Data + stream->Position, count);
    stream->Position += count;
    ReleaseSRWLockExclusive(&stream->Lock);
    if (read)
        *read = count;
    return count < size ? S_FALSE : S_OK;
}

static HRESULT Grow(DATA_STREAM *stream, ULONGLONG size)
{
    if (size <= stream->Capacity)
        return S_OK;
    ULONGLONG capacity = stream->Capacity ? stream->Capacity : 65536;
    while (capacity < size)
        capacity *= 2;
    if (capacity > 0x7FFFFFFF)
        return E_OUTOFMEMORY;
    BYTE *data = UrlRealloc(stream->Data, (SIZE_T)capacity);
    if (!data)
        return E_OUTOFMEMORY;
    stream->Data = data;
    stream->Capacity = capacity;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ds_Write(IStream *iface, const void *buffer, ULONG size, ULONG *written)
{
    DATA_STREAM *stream = stream_of(iface);
    AcquireSRWLockExclusive(&stream->Lock);
    HRESULT hr = Grow(stream, stream->Position + size);
    if (SUCCEEDED(hr)) {
        memcpy(stream->Data + stream->Position, buffer, size);
        stream->Position += size;
        if (stream->Position > stream->Size)
            stream->Size = stream->Position;
    }
    ReleaseSRWLockExclusive(&stream->Lock);
    if (written)
        *written = SUCCEEDED(hr) ? size : 0;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_Seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    DATA_STREAM *stream = stream_of(iface);
    AcquireSRWLockExclusive(&stream->Lock);
    LONGLONG base = origin == STREAM_SEEK_SET ? 0
                    : origin == STREAM_SEEK_CUR ? (LONGLONG)stream->Position
                    : origin == STREAM_SEEK_END ? (LONGLONG)stream->Size
                                                : -1;
    HRESULT hr = S_OK;
    if (base < 0)
        hr = STG_E_INVALIDFUNCTION;
    else if (base + move.QuadPart < 0)
        hr = STG_E_INVALIDFUNCTION;
    else
        stream->Position = (ULONGLONG)(base + move.QuadPart);
    if (position)
        position->QuadPart = stream->Position;
    ReleaseSRWLockExclusive(&stream->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_SetSize(IStream *iface, ULARGE_INTEGER size)
{
    DATA_STREAM *stream = stream_of(iface);
    AcquireSRWLockExclusive(&stream->Lock);
    HRESULT hr = Grow(stream, size.QuadPart);
    if (SUCCEEDED(hr)) {
        if (size.QuadPart > stream->Size)
            memset(stream->Data + stream->Size, 0, (SIZE_T)(size.QuadPart - stream->Size));
        stream->Size = size.QuadPart;
    }
    ReleaseSRWLockExclusive(&stream->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_CopyTo(IStream *iface, IStream *target, ULARGE_INTEGER size, ULARGE_INTEGER *read,
                                           ULARGE_INTEGER *written)
{
    BYTE buffer[4096];
    ULONGLONG total_read = 0, total_written = 0;
    HRESULT hr = S_OK;
    while (total_read < size.QuadPart) {
        ULONG want = size.QuadPart - total_read < sizeof(buffer) ? (ULONG)(size.QuadPart - total_read)
                                                                   : (ULONG)sizeof(buffer);
        ULONG got = 0, put = 0;
        ds_Read(iface, buffer, want, &got);
        if (!got)
            break;
        total_read += got;
        hr = target->lpVtbl->Write(target, buffer, got, &put);
        total_written += put;
        if (FAILED(hr))
            break;
    }
    if (read)
        read->QuadPart = total_read;
    if (written)
        written->QuadPart = total_written;
    return hr;
}

static HRESULT STDMETHODCALLTYPE ds_Commit(IStream *iface, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ds_Revert(IStream *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ds_LockRegion(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE ds_Stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    DATA_STREAM *stream = stream_of(iface);
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = stream->Size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ds_Clone(IStream *iface, IStream **clone)
{
    if (clone)
        *clone = nullptr;
    return E_NOTIMPL;
}

static const IStreamVtbl DataStreamVtbl = {
    ds_QueryInterface, ds_AddRef, ds_Release,    ds_Read,
    ds_Write,          ds_Seek,   ds_SetSize,    ds_CopyTo,
    ds_Commit,         ds_Revert, ds_LockRegion, ds_LockRegion,
    ds_Stat,           ds_Clone,
};

IStream *UrlCreateDataStream(void)
{
    DATA_STREAM *stream = UrlAllocZero(sizeof(*stream));
    if (!stream)
        return nullptr;
    stream->Interface.lpVtbl = &DataStreamVtbl;
    stream->Refs = 1;
    return &stream->Interface;
}

/* appends without moving the read position. */
HRESULT UrlStreamAppend(IStream *iface, const void *data, DWORD size)
{
    DATA_STREAM *stream = stream_of(iface);
    AcquireSRWLockExclusive(&stream->Lock);
    HRESULT hr = Grow(stream, stream->Size + size);
    if (SUCCEEDED(hr)) {
        memcpy(stream->Data + stream->Size, data, size);
        stream->Size += size;
    }
    ReleaseSRWLockExclusive(&stream->Lock);
    return hr;
}

/* ------------------------------------------------------------------ binding */

typedef struct {
    IBinding Interface;
    LONG volatile Refs;
    LONG volatile Aborted;
    LONG Priority;
    IBindStatusCallback *Callback;
    LPCWSTR Url;
    DWORD Kind;
    DWORD Bindf;
    ULONG Progress;
    ULONG ProgressMax;
    BOOL Began;
    HANDLE File;
    IStream *Stream;
    HINTERNET Request;
} DOWNLOAD;

static DOWNLOAD *download_of(IBinding *iface)
{
    return (DOWNLOAD *)iface;
}

static HRESULT STDMETHODCALLTYPE b_QueryInterface(IBinding *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IBinding)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE b_AddRef(IBinding *iface)
{
    return (ULONG)InterlockedIncrement(&download_of(iface)->Refs);
}

static ULONG STDMETHODCALLTYPE b_Release(IBinding *iface)
{
    DOWNLOAD *download = download_of(iface);
    LONG refs = InterlockedDecrement(&download->Refs);
    if (!refs)
        UrlFree(download);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE b_Abort(IBinding *iface)
{
    DOWNLOAD *download = download_of(iface);
    if (InterlockedExchange(&download->Aborted, 1))
        return E_FAIL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b_Suspend(IBinding *iface)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE b_SetPriority(IBinding *iface, LONG priority)
{
    download_of(iface)->Priority = priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b_GetPriority(IBinding *iface, LONG *priority)
{
    if (!priority)
        return E_INVALIDARG;
    *priority = download_of(iface)->Priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b_GetBindResult(IBinding *iface, CLSID *protocol, DWORD *result, LPOLESTR *text,
                                                 DWORD *reserved)
{
    if (!protocol || !result || !text)
        return E_INVALIDARG;
    memset(protocol, 0, sizeof(*protocol));
    *result = 0;
    *text = nullptr;
    return S_OK;
}

static const IBindingVtbl BindingVtbl = {b_QueryInterface, b_AddRef,        b_Release,      b_Abort,        b_Suspend,
                                         b_Suspend,        b_SetPriority,   b_GetPriority,  b_GetBindResult};

/* ------------------------------------------------------------------ callback */

/*
 * E_NOTIMPL from the caller counts as success, E_ABORT aborts the binding. like windows, the maximum never falls
 * below the progress.
 */
static HRESULT Progress(DOWNLOAD *download, ULONG status, LPCWSTR text)
{
    if (download->Aborted)
        return E_ABORT;
    if (!download->Callback)
        return S_OK;
    ULONG max = download->ProgressMax;
    if (max < download->Progress)
        max = download->Progress;
    HRESULT hr = download->Callback->lpVtbl->OnProgress(download->Callback, download->Progress, max, status, text);
    if (hr == E_ABORT) {
        download->Aborted = TRUE;
        return E_ABORT;
    }
    return download->Aborted ? E_ABORT : S_OK;
}

static HRESULT Start(DOWNLOAD *download)
{
    IBindStatusCallback *callback = download->Callback;
    if (!callback)
        return S_OK;
    HRESULT hr = callback->lpVtbl->OnStartBinding(callback, 0, &download->Interface);
    if (hr == E_ABORT || download->Aborted)
        return E_ABORT;
    if (FAILED(hr) && hr != E_NOTIMPL)
        return hr;
    BINDINFO info = {sizeof(info)};
    DWORD bindf = 0;
    hr = callback->lpVtbl->GetBindInfo(callback, &bindf, &info);
    if (hr == E_ABORT)
        return E_ABORT;
    if (SUCCEEDED(hr)) {
        download->Bindf |= bindf;
        CoTaskMemFree(info.szExtraInfo);
        CoTaskMemFree(info.szCustomVerb);
        if (info.pUnk)
            info.pUnk->lpVtbl->Release(info.pUnk);
    }
    return S_OK;
}

/* writes to the file or stream sink, and for pushed streams tells the caller too. */
static HRESULT Deliver(DOWNLOAD *download, const BYTE *data, DWORD size, DWORD bscf)
{
    if (size && download->File) {
        DWORD written;
        if (!WriteFile(download->File, data, size, &written, nullptr) || written != size)
            return INET_E_DOWNLOAD_FAILURE;
    }
    if (size && download->Stream && FAILED(UrlStreamAppend(download->Stream, data, size)))
        return E_OUTOFMEMORY;
    download->Progress += size;
    HRESULT hr = Progress(download, download->Began ? BINDSTATUS_DOWNLOADINGDATA : BINDSTATUS_BEGINDOWNLOADDATA,
                          download->Url);
    download->Began = TRUE;
    if (FAILED(hr))
        return hr;
    if (download->Kind == URL_SINK_PUSH && download->Callback) {
        FORMATETC format = {0, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
        STGMEDIUM medium = {TYMED_ISTREAM};
        medium.pstm = download->Stream;
        hr = download->Callback->lpVtbl->OnDataAvailable(download->Callback, bscf, download->Progress, &format,
                                                         &medium);
        if (hr == E_ABORT || download->Aborted)
            return E_ABORT;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ http through wininet */

static INIT_ONCE SessionOnce = INIT_ONCE_STATIC_INIT;
static HINTERNET Session;
static const char *SessionAgent;

/* maps wininet progress to BINDSTATUS codes, through the request context. */
static void CALLBACK OnInternetStatus(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID info, DWORD length)
{
    DOWNLOAD *download = (DOWNLOAD *)context;
    if (!download || download->Aborted)
        return;
    WCHAR text[INTERNET_MAX_URL_LENGTH];
    switch (status) {
    case INTERNET_STATUS_RESOLVING_NAME:
        Progress(download, BINDSTATUS_FINDINGRESOURCE, info);
        break;
    case INTERNET_STATUS_CONNECTING_TO_SERVER:
        MultiByteToWideChar(CP_ACP, 0, info, -1, text, ARRAYSIZE(text));
        Progress(download, BINDSTATUS_CONNECTING, text);
        break;
    case INTERNET_STATUS_SENDING_REQUEST:
        Progress(download, BINDSTATUS_SENDINGREQUEST, nullptr);
        break;
    case INTERNET_STATUS_REDIRECT:
        Progress(download, BINDSTATUS_REDIRECTING, info);
        break;
    }
    /* an abort from a progress callback stops the request where it waits. */
    if (download->Aborted && download->Request)
        InternetCloseHandle(download->Request);
}

static BOOL CALLBACK OpenSession(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    SessionAgent = UrlUserAgent();
    PWSTR agent = UrlWiden(SessionAgent);
    Session = InternetOpenW(agent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    UrlFree(agent);
    if (Session) {
        BOOL decode = TRUE;
        InternetSetOptionW(Session, INTERNET_OPTION_HTTP_DECODING, &decode, sizeof(decode));
        InternetSetStatusCallbackW(Session, OnInternetStatus);
    }
    return TRUE;
}

/* maps a wininet error to INET_E_* following urlmon's table. */
static HRESULT InetError(DWORD error)
{
    switch (error) {
    case ERROR_INTERNET_TIMEOUT:
        return INET_E_CONNECTION_TIMEOUT;
    case ERROR_INTERNET_INVALID_URL:
        return INET_E_INVALID_URL;
    case ERROR_INTERNET_UNRECOGNIZED_SCHEME:
    case ERROR_INTERNET_PROTOCOL_NOT_FOUND:
        return INET_E_UNKNOWN_PROTOCOL;
    case ERROR_INTERNET_NAME_NOT_RESOLVED:
    case ERROR_INTERNET_CANNOT_CONNECT:
        return INET_E_RESOURCE_NOT_FOUND;
    case ERROR_INTERNET_ITEM_NOT_FOUND:
        return INET_E_OBJECT_NOT_FOUND;
    case ERROR_HTTP_REDIRECT_FAILED:
        return INET_E_REDIRECT_FAILED;
    case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
    case ERROR_INTERNET_SEC_CERT_CN_INVALID:
    case ERROR_INTERNET_MIXED_SECURITY:
    case ERROR_INTERNET_CHG_POST_IS_NON_SECURE:
    case ERROR_INTERNET_POST_IS_NON_SECURE:
    case ERROR_INTERNET_CLIENT_AUTH_CERT_NEEDED:
    case ERROR_INTERNET_INVALID_CA:
    case ERROR_INTERNET_CLIENT_AUTH_NOT_SETUP:
    case ERROR_INTERNET_SEC_CERT_REV_FAILED:
    case ERROR_INTERNET_SEC_CERT_REVOKED:
    case ERROR_INTERNET_SEC_CERT_ERRORS:
    case ERROR_INTERNET_SEC_INVALID_CERT:
    case ERROR_INTERNET_SECURITY_CHANNEL_ERROR:
        return INET_E_SECURITY_PROBLEM;
    case ERROR_INTERNET_OPERATION_CANCELLED:
        return E_ABORT;
    }
    return INET_E_DOWNLOAD_FAILURE;
}

/* maps a status code to a result the way urlmon does. */
static HRESULT StatusResult(DWORD code)
{
    switch (code) {
    case 200:
    case 304:
    case 449:
        return S_OK;
    case 404:
        return INET_E_OBJECT_NOT_FOUND;
    case 406:
        return INET_E_NO_VALID_MEDIA;
    case 408:
    case 504:
        return INET_E_CONNECTION_TIMEOUT;
    case 503:
        return INET_E_INVALID_REQUEST;
    }
    return INET_E_DOWNLOAD_FAILURE;
}

static HRESULT HttpBind(DOWNLOAD *download)
{
    InitOnceExecuteOnce(&SessionOnce, OpenSession, nullptr, nullptr);
    if (!Session)
        return INET_E_NO_SESSION;
    const char *agent = UrlUserAgent();
    if (agent != SessionAgent)
        InternetSetOptionA(Session, INTERNET_OPTION_USER_AGENT, (void *)agent, (DWORD)strlen(agent));
    DWORD flags = INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
    if (download->Bindf & (BINDF_GETNEWESTVERSION | BINDF_PRAGMA_NO_CACHE | BINDF_RESYNCHRONIZE))
        flags |= INTERNET_FLAG_RELOAD;
    if (download->Bindf & BINDF_IGNORESECURITYPROBLEM)
        flags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    HINTERNET request = InternetOpenUrlW(Session, download->Url, L"Accept: */*\r\n", (DWORD)-1, flags,
                                         (DWORD_PTR)download);
    if (!request)
        return download->Aborted ? E_ABORT : InetError(GetLastError());
    download->Request = request;
    DWORD code = 0, length = sizeof(code);
    HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &length, nullptr);
    HRESULT hr = StatusResult(code);
    if (SUCCEEDED(hr)) {
        WCHAR type[256];
        length = sizeof(type);
        if (HttpQueryInfoW(request, HTTP_QUERY_CONTENT_TYPE, type, &length, nullptr)) {
            for (WCHAR *p = type; *p; p++)
                if (*p == ';' || *p == ' ') {
                    *p = 0;
                    break;
                }
            hr = Progress(download, BINDSTATUS_MIMETYPEAVAILABLE, type);
        }
        DWORD size = 0;
        length = sizeof(size);
        if (HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &size, &length, nullptr))
            download->ProgressMax = size;
    }
    BYTE *buffer = SUCCEEDED(hr) ? UrlAlloc(URL_CHUNK) : nullptr;
    if (SUCCEEDED(hr) && !buffer)
        hr = E_OUTOFMEMORY;
    DWORD bscf = BSCF_FIRSTDATANOTIFICATION;
    while (SUCCEEDED(hr)) {
        DWORD read = 0;
        if (!InternetReadFile(request, buffer, URL_CHUNK, &read)) {
            hr = download->Aborted ? E_ABORT : InetError(GetLastError());
            break;
        }
        if (!read)
            break;
        hr = Deliver(download, buffer, read, bscf);
        bscf = BSCF_INTERMEDIATEDATANOTIFICATION;
    }
    UrlFree(buffer);
    download->Request = nullptr;
    InternetCloseHandle(request);
    return hr;
}

/* ------------------------------------------------------------------ files */

/* handles file urls and plain paths passed as urls. */
static BOOL LocalPath(LPCWSTR url, PWSTR path, DWORD capacity)
{
    if (!_wcsnicmp(url, L"file:", 5)) {
        DWORD length = capacity;
        return SUCCEEDED(PathCreateFromUrlW(url, path, &length, 0));
    }
    BOOL drive = ((url[0] | 0x20) >= 'a' && (url[0] | 0x20) <= 'z') && url[1] == ':' &&
                 (url[2] == '\\' || url[2] == '/');
    BOOL unc = url[0] == '\\' && url[1] == '\\';
    if (!drive && !unc)
        return FALSE;
    lstrcpynW(path, url, (int)capacity);
    return TRUE;
}

static HRESULT FileBind(DOWNLOAD *download, LPCWSTR path)
{
    HRESULT hr = Progress(download, BINDSTATUS_SENDINGREQUEST, path);
    if (FAILED(hr))
        return hr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return INET_E_RESOURCE_NOT_FOUND;
    LARGE_INTEGER size = {0};
    GetFileSizeEx(file, &size);
    download->ProgressMax = size.QuadPart > 0xFFFFFFFF ? 0xFFFFFFFF : (ULONG)size.QuadPart;
    LPCWSTR extension = PathFindExtensionW(path);
    WCHAR type[128];
    DWORD type_size = sizeof(type), value_type;
    if (*extension && !RegGetValueW(HKEY_CLASSES_ROOT, extension, L"Content Type", RRF_RT_REG_SZ, &value_type, type,
                                    &type_size))
        hr = Progress(download, BINDSTATUS_MIMETYPEAVAILABLE, type);
    if (SUCCEEDED(hr) && download->Kind == URL_SINK_CACHE)
        hr = Progress(download, BINDSTATUS_CACHEFILENAMEAVAILABLE, path);
    BYTE *buffer = SUCCEEDED(hr) ? UrlAlloc(URL_CHUNK) : nullptr;
    if (SUCCEEDED(hr) && !buffer)
        hr = E_OUTOFMEMORY;
    DWORD bscf = BSCF_FIRSTDATANOTIFICATION;
    while (SUCCEEDED(hr)) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, URL_CHUNK, &read, nullptr)) {
            hr = INET_E_DOWNLOAD_FAILURE;
            break;
        }
        if (!read)
            break;
        hr = Deliver(download, buffer, read, bscf);
        bscf = BSCF_INTERMEDIATEDATANOTIFICATION;
    }
    UrlFree(buffer);
    CloseHandle(file);
    return hr;
}

/* ------------------------------------------------------------------ the cache */

/* under the temporary internet files, named after the url's last segment. */
static BOOL CacheFileFor(LPCWSTR url, PWSTR path, DWORD capacity)
{
    WCHAR directory[MAX_PATH];
    DWORD length = GetTempPathW(ARRAYSIZE(directory), directory);
    if (!length || length + 32 >= ARRAYSIZE(directory))
        return FALSE;
    lstrcatW(directory, L"INetCache");
    CreateDirectoryW(directory, nullptr);
    lstrcatW(directory, L"\\IE");
    CreateDirectoryW(directory, nullptr);
    WCHAR name[MAX_PATH] = L"";
    LPCWSTR end = url;
    while (*end && *end != '?' && *end != '#')
        end++;
    LPCWSTR start = end;
    while (start > url && start[-1] != '/' && start[-1] != '\\')
        start--;
    DWORD name_length = (DWORD)(end - start);
    if (!name_length || name_length >= 100 || (start > url + 1 && start[-2] == '/' && start[-1] == '/'))
        lstrcpyW(name, L"index.html");
    else
        lstrcpynW(name, start, (int)name_length + 1);
    for (WCHAR *p = name; *p; p++)
        if (wcschr(L"<>:\"|*%", *p))
            *p = '_';
    WCHAR stem[MAX_PATH], *dot = wcsrchr(name, '.');
    lstrcpyW(stem, name);
    if (dot)
        stem[dot - name] = 0;
    for (DWORD i = 1; i < 1000; i++) {
        if ((DWORD)lstrlenW(directory) + lstrlenW(name) + 16 >= capacity)
            return FALSE;
        wsprintfW(path, L"%s\\%s[%u]%s", directory, stem, i, dot ? dot : L"");
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ operations */

HRESULT UrlDownload(LPCWSTR url, DWORD kind, DWORD bindf, IBindStatusCallback *callback, LPCWSTR file,
                    IStream **stream, PWSTR cache_file, DWORD cache_length)
{
    if (stream)
        *stream = nullptr;
    if (!url || !*url)
        return E_INVALIDARG;
    DOWNLOAD *download = UrlAllocZero(sizeof(*download));
    if (!download)
        return E_OUTOFMEMORY;
    download->Interface.lpVtbl = &BindingVtbl;
    download->Refs = 1;
    download->Callback = callback;
    download->Url = url;
    download->Kind = kind;
    download->Bindf = bindf;
    if (callback)
        callback->lpVtbl->AddRef(callback);
    HRESULT hr = Start(download);
    BOOL started = SUCCEEDED(hr) || hr == E_ABORT;
    WCHAR path[MAX_PATH * 2];
    BOOL local = LocalPath(url, path, ARRAYSIZE(path));
    WCHAR target[MAX_PATH * 2] = L"";
    if (SUCCEEDED(hr) && kind != URL_SINK_FILE && kind != URL_SINK_CACHE) {
        download->Stream = UrlCreateDataStream();
        if (!download->Stream)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && (kind == URL_SINK_FILE || (kind == URL_SINK_CACHE && !local))) {
        if (kind == URL_SINK_FILE)
            lstrcpynW(target, file, ARRAYSIZE(target));
        else if (!CacheFileFor(url, target, ARRAYSIZE(target)))
            hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            download->File = CreateFileW(target, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
            if (download->File == INVALID_HANDLE_VALUE) {
                download->File = nullptr;
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
    }
    if (SUCCEEDED(hr) && kind == URL_SINK_CACHE && !local)
        hr = Progress(download, BINDSTATUS_CACHEFILENAMEAVAILABLE, target);
    if (SUCCEEDED(hr)) {
        if (local)
            hr = FileBind(download, path);
        else if (!_wcsnicmp(url, L"http:", 5) || !_wcsnicmp(url, L"https:", 6))
            hr = HttpBind(download);
        else
            hr = !wcschr(url, ':') ? MK_E_SYNTAX : INET_E_UNKNOWN_PROTOCOL;
    }
    if (SUCCEEDED(hr) && !download->Began)
        hr = Progress(download, BINDSTATUS_BEGINDOWNLOADDATA, url);
    if (SUCCEEDED(hr))
        hr = Progress(download, BINDSTATUS_ENDDOWNLOADDATA, url);
    if (SUCCEEDED(hr) && kind == URL_SINK_PUSH && callback) {
        FORMATETC format = {0, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
        STGMEDIUM medium = {TYMED_ISTREAM};
        medium.pstm = download->Stream;
        callback->lpVtbl->OnDataAvailable(callback, BSCF_LASTDATANOTIFICATION | BSCF_DATAFULLYAVAILABLE,
                                          download->Progress, &format, &medium);
    }
    if (download->Aborted && SUCCEEDED(hr))
        hr = E_ABORT;
    if (download->File) {
        CloseHandle(download->File);
        if (FAILED(hr))
            DeleteFileW(target);
    }
    if (started && callback)
        callback->lpVtbl->OnStopBinding(callback, hr, nullptr);
    if (SUCCEEDED(hr) && kind == URL_SINK_CACHE && cache_file) {
        LPCWSTR name = local ? path : target;
        if ((DWORD)lstrlenW(name) >= cache_length) {
            cache_file[0] = 0;
            hr = E_OUTOFMEMORY;
        } else {
            lstrcpyW(cache_file, name);
        }
    }
    if (download->Stream) {
        if (SUCCEEDED(hr) && stream) {
            LARGE_INTEGER zero = {0};
            download->Stream->lpVtbl->Seek(download->Stream, zero, STREAM_SEEK_SET, nullptr);
            *stream = download->Stream;
        } else {
            download->Stream->lpVtbl->Release(download->Stream);
        }
    }
    if (callback)
        callback->lpVtbl->Release(callback);
    download->Interface.lpVtbl->Release(&download->Interface);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI URLDownloadToFileW(LPUNKNOWN caller, LPCWSTR url, LPCWSTR file, DWORD reserved,
                                                 LPBINDSTATUSCALLBACK callback)
{
    if (!url || !file)
        return E_INVALIDARG;
    return UrlDownload(url, URL_SINK_FILE, reserved, callback, file, nullptr, nullptr, 0);
}

DECLSPEC_EXPORT HRESULT WINAPI URLDownloadToFileA(LPUNKNOWN caller, LPCSTR url, LPCSTR file, DWORD reserved,
                                                 LPBINDSTATUSCALLBACK callback)
{
    if (!url || !file)
        return E_INVALIDARG;
    PWSTR wide_url = UrlWiden(url), wide_file = UrlWiden(file);
    HRESULT hr = wide_url && wide_file ? URLDownloadToFileW(caller, wide_url, wide_file, reserved, callback)
                                       : E_OUTOFMEMORY;
    UrlFree(wide_url);
    UrlFree(wide_file);
    return hr;
}

/* like windows, a length under 4 characters means MAX_PATH. */
DECLSPEC_EXPORT HRESULT WINAPI URLDownloadToCacheFileW(LPUNKNOWN caller, LPCWSTR url, LPWSTR file, DWORD length,
                                                      DWORD reserved, LPBINDSTATUSCALLBACK callback)
{
    if (!length)
        return E_OUTOFMEMORY;
    if (!file)
        return E_INVALIDARG;
    file[0] = 0;
    if (length < 4)
        length = MAX_PATH;
    return UrlDownload(url, URL_SINK_CACHE, reserved, callback, nullptr, nullptr, file, length);
}

DECLSPEC_EXPORT HRESULT WINAPI URLDownloadToCacheFileA(LPUNKNOWN caller, LPCSTR url, LPSTR file, DWORD length,
                                                      DWORD reserved, LPBINDSTATUSCALLBACK callback)
{
    if (!length)
        return E_OUTOFMEMORY;
    if (!file)
        return E_INVALIDARG;
    file[0] = 0;
    PWSTR wide_url = UrlWiden(url);
    WCHAR wide[MAX_PATH * 2];
    HRESULT hr = wide_url || !url ? URLDownloadToCacheFileW(caller, wide_url, wide, ARRAYSIZE(wide), reserved, callback)
                                  : E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && !WideCharToMultiByte(CP_ACP, 0, wide, -1, file, length < 4 ? MAX_PATH : (int)length, nullptr,
                                              nullptr)) {
        file[0] = 0;
        hr = E_OUTOFMEMORY;
    }
    UrlFree(wide_url);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI URLOpenBlockingStreamW(LPUNKNOWN caller, LPCWSTR url, LPSTREAM *stream, DWORD reserved,
                                                     LPBINDSTATUSCALLBACK callback)
{
    if (!stream)
        return E_INVALIDARG;
    return UrlDownload(url, URL_SINK_STREAM, reserved, callback, nullptr, stream, nullptr, 0);
}

DECLSPEC_EXPORT HRESULT WINAPI URLOpenBlockingStreamA(LPUNKNOWN caller, LPCSTR url, LPSTREAM *stream, DWORD reserved,
                                                     LPBINDSTATUSCALLBACK callback)
{
    if (!stream)
        return E_INVALIDARG;
    PWSTR wide = UrlWiden(url);
    HRESULT hr = URLOpenBlockingStreamW(caller, wide, stream, reserved, callback);
    UrlFree(wide);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI URLOpenStreamW(LPUNKNOWN caller, LPCWSTR url, DWORD reserved,
                                             LPBINDSTATUSCALLBACK callback)
{
    return UrlDownload(url, URL_SINK_PUSH, reserved, callback, nullptr, nullptr, nullptr, 0);
}

DECLSPEC_EXPORT HRESULT WINAPI URLOpenStreamA(LPUNKNOWN caller, LPCSTR url, DWORD reserved,
                                             LPBINDSTATUSCALLBACK callback)
{
    PWSTR wide = UrlWiden(url);
    HRESULT hr = URLOpenStreamW(caller, wide, reserved, callback);
    UrlFree(wide);
    return hr;
}
