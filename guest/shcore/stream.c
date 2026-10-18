/*
 * the streams of shcore: memory, files and registry values, and the IStream
 * helpers over any stream.
 */
#include "shcore_private.h"

#define STG_E_INVALIDFUNCTION ((HRESULT)0x80030001L)
#define STG_E_INVALIDPOINTER ((HRESULT)0x80030009L)
#define STG_E_SEEKERROR ((HRESULT)0x80030019L)
#define STREAM_SEEK_SET 0
#define STREAM_SEEK_CUR 1
#define STREAM_SEEK_END 2

/* ------------------------------------------------------------------ memory streams */

typedef struct {
    IStream iface;
    LONG refs;
    CRITICAL_SECTION lock;
    BYTE *data;
    SIZE_T size, capacity, position;
    /* a registry stream writes its bytes back as it goes away. */
    HKEY key;
    LPWSTR value;
} MEMSTREAM;

static MEMSTREAM *mem(IStream *iface)
{
    return (MEMSTREAM *)iface;
}

static HRESULT WINAPI mem_query(IStream *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IStream) ||
        IsEqualIID(riid, &IID_ISequentialStream)) {
        *object = iface;
        InterlockedIncrement(&mem(iface)->refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI mem_add_ref(IStream *iface)
{
    return (ULONG)InterlockedIncrement(&mem(iface)->refs);
}

static ULONG WINAPI mem_release(IStream *iface)
{
    MEMSTREAM *s = mem(iface);
    LONG refs = InterlockedDecrement(&s->refs);
    if (refs)
        return (ULONG)refs;
    if (s->key) {
        RegSetValueExW(s->key, s->value, 0, REG_BINARY, s->data, (DWORD)s->size);
        RegCloseKey(s->key);
    }
    CoTaskMemFree(s->value);
    LocalFree(s->data);
    DeleteCriticalSection(&s->lock);
    LocalFree(s);
    return 0;
}

static BOOL grow(MEMSTREAM *s, SIZE_T size)
{
    if (size <= s->capacity)
        return TRUE;
    SIZE_T capacity = s->capacity ? s->capacity : 256;
    while (capacity < size)
        capacity *= 2;
    BYTE *data = s->data ? LocalReAlloc(s->data, capacity, LMEM_MOVEABLE | LMEM_ZEROINIT)
                         : LocalAlloc(LMEM_ZEROINIT, capacity);
    if (!data)
        return FALSE;
    s->data = data;
    s->capacity = capacity;
    return TRUE;
}

static HRESULT WINAPI mem_read(IStream *iface, void *buffer, ULONG size, ULONG *read)
{
    MEMSTREAM *s = mem(iface);
    if (!buffer)
        return STG_E_INVALIDPOINTER;
    EnterCriticalSection(&s->lock);
    SIZE_T left = s->position < s->size ? s->size - s->position : 0;
    ULONG n = left < size ? (ULONG)left : size;
    if (n)
        memcpy(buffer, s->data + s->position, n);
    s->position += n;
    LeaveCriticalSection(&s->lock);
    if (read)
        *read = n;
    return S_OK;
}

static HRESULT WINAPI mem_write(IStream *iface, const void *buffer, ULONG size, ULONG *written)
{
    MEMSTREAM *s = mem(iface);
    if (written)
        *written = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    EnterCriticalSection(&s->lock);
    HRESULT hr = S_OK;
    if (!grow(s, s->position + size)) {
        hr = E_OUTOFMEMORY;
    } else {
        if (size)
            memcpy(s->data + s->position, buffer, size);
        s->position += size;
        if (s->position > s->size)
            s->size = s->position;
        if (written)
            *written = size;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT WINAPI mem_seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    MEMSTREAM *s = mem(iface);
    EnterCriticalSection(&s->lock);
    LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (LONGLONG)s->position
                                                                             : (LONGLONG)s->size;
    HRESULT hr = S_OK;
    if (origin > STREAM_SEEK_END)
        hr = STG_E_INVALIDFUNCTION;
    else if (base + move.QuadPart < 0)
        hr = STG_E_INVALIDFUNCTION;
    else
        s->position = (SIZE_T)(base + move.QuadPart);
    if (position)
        position->QuadPart = s->position;
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT WINAPI mem_set_size(IStream *iface, ULARGE_INTEGER size)
{
    MEMSTREAM *s = mem(iface);
    EnterCriticalSection(&s->lock);
    HRESULT hr = S_OK;
    if (size.QuadPart > 0x7FFFFFFF || !grow(s, (SIZE_T)size.QuadPart))
        hr = E_OUTOFMEMORY;
    else {
        if (size.QuadPart < s->size)
            memset(s->data + size.QuadPart, 0, s->size - (SIZE_T)size.QuadPart);
        s->size = (SIZE_T)size.QuadPart;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT WINAPI mem_copy_to(IStream *iface, IStream *to, ULARGE_INTEGER size, ULARGE_INTEGER *read,
                                  ULARGE_INTEGER *written)
{
    MEMSTREAM *s = mem(iface);
    if (!to)
        return STG_E_INVALIDPOINTER;
    EnterCriticalSection(&s->lock);
    SIZE_T left = s->position < s->size ? s->size - s->position : 0;
    ULONG n = left < size.QuadPart ? (ULONG)left : (ULONG)size.QuadPart;
    ULONG done = 0;
    HRESULT hr = to->lpVtbl->Write(to, s->data + s->position, n, &done);
    s->position += n;
    LeaveCriticalSection(&s->lock);
    if (read)
        read->QuadPart = n;
    if (written)
        written->QuadPart = done;
    return hr;
}

static HRESULT WINAPI mem_commit(IStream *iface, DWORD flags)
{
    return S_OK;
}

static HRESULT WINAPI mem_revert(IStream *iface)
{
    return S_OK;
}

static HRESULT WINAPI mem_lock(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI mem_stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    MEMSTREAM *s = mem(iface);
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = s->size;
    stat->grfMode = STGM_READWRITE;
    return S_OK;
}

static HRESULT create_memory(const BYTE *data, SIZE_T size, MEMSTREAM **out);

static HRESULT WINAPI mem_clone(IStream *iface, IStream **clone)
{
    MEMSTREAM *s = mem(iface), *copy;
    if (!clone)
        return STG_E_INVALIDPOINTER;
    *clone = nullptr;
    EnterCriticalSection(&s->lock);
    HRESULT hr = create_memory(s->data, s->size, &copy);
    if (SUCCEEDED(hr)) {
        copy->position = s->position;
        *clone = &copy->iface;
    }
    LeaveCriticalSection(&s->lock);
    return hr;
}

static const IStreamVtbl mem_vtbl = {
    mem_query, mem_add_ref, mem_release, mem_read, mem_write, mem_seek, mem_set_size, mem_copy_to, mem_commit,
    mem_revert, mem_lock, mem_lock, mem_stat, mem_clone,
};

static HRESULT create_memory(const BYTE *data, SIZE_T size, MEMSTREAM **out)
{
    MEMSTREAM *s = LocalAlloc(LMEM_ZEROINIT, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->iface.lpVtbl = &mem_vtbl;
    s->refs = 1;
    InitializeCriticalSection(&s->lock);
    if (!grow(s, size ? size : 1)) {
        mem_release(&s->iface);
        return E_OUTOFMEMORY;
    }
    if (data && size)
        memcpy(s->data, data, size);
    s->size = size;
    *out = s;
    return S_OK;
}

IStream *WINAPI SHCreateMemStream(const BYTE *data, UINT size)
{
    MEMSTREAM *s;
    return SUCCEEDED(create_memory(data, data ? size : 0, &s)) ? &s->iface : nullptr;
}

/* ------------------------------------------------------------------ registry streams */

IStream *WINAPI SHOpenRegStream2W(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD mode)
{
    HKEY key;
    LONG error;
    BOOL write = (mode & (STGM_WRITE | STGM_READWRITE)) != 0;
    if (write)
        error = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    else
        error = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (error)
        return nullptr;
    DWORD type, size = 0;
    BYTE *data = nullptr;
    if (!RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) && type == REG_BINARY && size) {
        data = LocalAlloc(LMEM_FIXED, size);
        if (data && RegQueryValueExW(key, value, nullptr, &type, data, &size)) {
            LocalFree(data);
            data = nullptr;
            size = 0;
        }
    } else {
        size = 0;
    }
    MEMSTREAM *s;
    HRESULT hr = create_memory(data, size, &s);
    LocalFree(data);
    if (FAILED(hr)) {
        RegCloseKey(key);
        return nullptr;
    }
    if (write && SUCCEEDED(ScAllocString(value ? value : L"", &s->value)))
        s->key = key;
    else
        RegCloseKey(key);
    return &s->iface;
}

IStream *WINAPI SHOpenRegStream2A(HKEY root, LPCSTR subkey, LPCSTR value, DWORD mode)
{
    WCHAR wsubkey[MAX_PATH], wvalue[MAX_PATH];
    if (subkey)
        MultiByteToWideChar(CP_ACP, 0, subkey, -1, wsubkey, MAX_PATH);
    if (value)
        MultiByteToWideChar(CP_ACP, 0, value, -1, wvalue, MAX_PATH);
    return SHOpenRegStream2W(root, subkey ? wsubkey : nullptr, value ? wvalue : nullptr, mode);
}

/* the old form never fails: a value it cannot open reads as an empty stream. */
IStream *WINAPI SHOpenRegStreamW(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD mode)
{
    IStream *stream = SHOpenRegStream2W(root, subkey, value, mode);
    return stream ? stream : SHCreateMemStream(nullptr, 0);
}

IStream *WINAPI SHOpenRegStreamA(HKEY root, LPCSTR subkey, LPCSTR value, DWORD mode)
{
    IStream *stream = SHOpenRegStream2A(root, subkey, value, mode);
    return stream ? stream : SHCreateMemStream(nullptr, 0);
}

/* ------------------------------------------------------------------ file streams */

typedef struct {
    IStream iface;
    LONG refs;
    HANDLE file;
    DWORD mode;
    LPWSTR path;
} FILESTREAM;

static FILESTREAM *fs(IStream *iface)
{
    return (FILESTREAM *)iface;
}

static HRESULT last_error(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static HRESULT WINAPI file_query(IStream *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IStream) ||
        IsEqualIID(riid, &IID_ISequentialStream)) {
        *object = iface;
        InterlockedIncrement(&fs(iface)->refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI file_add_ref(IStream *iface)
{
    return (ULONG)InterlockedIncrement(&fs(iface)->refs);
}

static ULONG WINAPI file_release(IStream *iface)
{
    FILESTREAM *s = fs(iface);
    LONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        CloseHandle(s->file);
        CoTaskMemFree(s->path);
        LocalFree(s);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI file_read(IStream *iface, void *buffer, ULONG size, ULONG *read)
{
    DWORD done = 0;
    if (!buffer)
        return STG_E_INVALIDPOINTER;
    BOOL ok = ReadFile(fs(iface)->file, buffer, size, &done, nullptr);
    if (read)
        *read = done;
    if (!ok)
        return last_error();
    return done < size ? S_FALSE : S_OK;
}

static HRESULT WINAPI file_write(IStream *iface, const void *buffer, ULONG size, ULONG *written)
{
    DWORD done = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    BOOL ok = WriteFile(fs(iface)->file, buffer, size, &done, nullptr);
    if (written)
        *written = done;
    return ok ? S_OK : last_error();
}

static HRESULT WINAPI file_seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    LARGE_INTEGER now;
    if (origin > STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    if (!SetFilePointerEx(fs(iface)->file, move, &now, origin))
        return last_error();
    if (position)
        position->QuadPart = (ULONGLONG)now.QuadPart;
    return S_OK;
}

static HRESULT WINAPI file_set_size(IStream *iface, ULARGE_INTEGER size)
{
    FILESTREAM *s = fs(iface);
    LARGE_INTEGER zero = {0}, here, target = {.QuadPart = (LONGLONG)size.QuadPart};
    if (!SetFilePointerEx(s->file, zero, &here, FILE_CURRENT) || !SetFilePointerEx(s->file, target, nullptr, 0) ||
        !SetEndOfFile(s->file))
        return last_error();
    SetFilePointerEx(s->file, here, nullptr, 0);
    return S_OK;
}

static HRESULT WINAPI file_copy_to(IStream *iface, IStream *to, ULARGE_INTEGER size, ULARGE_INTEGER *read,
                                   ULARGE_INTEGER *written)
{
    BYTE buffer[4096];
    ULONGLONG total_read = 0, total_written = 0;
    HRESULT hr = S_OK;
    if (!to)
        return STG_E_INVALIDPOINTER;
    while (total_read < size.QuadPart) {
        ULONG want = size.QuadPart - total_read < sizeof(buffer) ? (ULONG)(size.QuadPart - total_read) : sizeof(buffer);
        ULONG got = 0, put = 0;
        hr = file_read(iface, buffer, want, &got);
        if (FAILED(hr) || !got)
            break;
        total_read += got;
        hr = to->lpVtbl->Write(to, buffer, got, &put);
        total_written += put;
        if (FAILED(hr))
            break;
    }
    if (read)
        read->QuadPart = total_read;
    if (written)
        written->QuadPart = total_written;
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI file_commit(IStream *iface, DWORD flags)
{
    return FlushFileBuffers(fs(iface)->file) || GetLastError() == ERROR_ACCESS_DENIED ? S_OK : last_error();
}

static HRESULT WINAPI file_stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    FILESTREAM *s = fs(iface);
    BY_HANDLE_FILE_INFORMATION info;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    if (!GetFileInformationByHandle(s->file, &info))
        return last_error();
    if (!(flags & STATFLAG_NONAME)) {
        LPCWSTR name = s->path;
        for (LPCWSTR p = s->path; *p; p++)
            if (*p == '\\' || *p == '/')
                name = p + 1;
        HRESULT hr = ScAllocString(name, &stat->pwcsName);
        if (FAILED(hr))
            return hr;
    }
    stat->type = STGTY_STREAM;
    stat->cbSize.LowPart = info.nFileSizeLow;
    stat->cbSize.HighPart = info.nFileSizeHigh;
    stat->mtime = info.ftLastWriteTime;
    stat->ctime = info.ftCreationTime;
    stat->atime = info.ftLastAccessTime;
    stat->grfMode = s->mode;
    return S_OK;
}

static HRESULT WINAPI file_clone(IStream *iface, IStream **clone)
{
    if (clone)
        *clone = nullptr;
    return E_NOTIMPL;
}

static const IStreamVtbl file_vtbl = {
    file_query, file_add_ref, file_release, file_read, file_write, file_seek, file_set_size, file_copy_to,
    file_commit, mem_revert, mem_lock, mem_lock, file_stat, file_clone,
};

HRESULT WINAPI SHCreateStreamOnFileEx(LPCWSTR path, DWORD mode, DWORD attributes, BOOL create,
                                      IStream *template_stream, IStream **stream)
{
    if (!stream || !path)
        return E_INVALIDARG;
    *stream = nullptr;
    DWORD access = (mode & 3) == STGM_WRITE ? GENERIC_WRITE
                   : (mode & 3) == STGM_READWRITE ? GENERIC_READ | GENERIC_WRITE
                                                 : GENERIC_READ;
    DWORD share;
    switch (mode & 0x70) {
    case STGM_SHARE_EXCLUSIVE: share = 0; break;
    case STGM_SHARE_DENY_WRITE: share = FILE_SHARE_READ; break;
    case STGM_SHARE_DENY_READ: share = FILE_SHARE_WRITE; break;
    default: share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    }
    DWORD disposition = (mode & STGM_CREATE) ? CREATE_ALWAYS : create ? CREATE_NEW : OPEN_EXISTING;
    HANDLE file = CreateFileW(path, access, share | FILE_SHARE_DELETE, nullptr, disposition,
                              attributes ? attributes : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    FILESTREAM *s = LocalAlloc(LMEM_ZEROINIT, sizeof(*s));
    if (!s || FAILED(ScAllocString(path, &s->path))) {
        LocalFree(s);
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }
    s->iface.lpVtbl = &file_vtbl;
    s->refs = 1;
    s->file = file;
    s->mode = mode;
    *stream = &s->iface;
    return S_OK;
}

/* only the access, share and create bits of the mode are taken. */
HRESULT WINAPI SHCreateStreamOnFileW(LPCWSTR path, DWORD mode, IStream **stream)
{
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    if ((mode & ~(3u | 0x70u | STGM_CREATE)) || !path)
        return E_INVALIDARG;
    return SHCreateStreamOnFileEx(path, mode, 0, FALSE, nullptr, stream);
}

HRESULT WINAPI SHCreateStreamOnFileA(LPCSTR path, DWORD mode, IStream **stream)
{
    WCHAR wide[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path ? path : "", -1, wide, MAX_PATH);
    wide[MAX_PATH - 1] = 0;
    return SHCreateStreamOnFileW(wide, mode, stream);
}

/* ------------------------------------------------------------------ IStream helpers */

HRESULT WINAPI IStream_Read(IStream *stream, void *data, ULONG size)
{
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, size, &read);
    return SUCCEEDED(hr) && read != size ? E_FAIL : hr;
}

HRESULT WINAPI IStream_Write(IStream *stream, const void *data, ULONG size)
{
    ULONG written = 0;
    HRESULT hr = stream->lpVtbl->Write(stream, data, size, &written);
    return SUCCEEDED(hr) && written != size ? E_FAIL : hr;
}

HRESULT WINAPI IStream_Reset(IStream *stream)
{
    LARGE_INTEGER zero = {0};
    if (!stream)
        return E_INVALIDARG;
    return stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
}

HRESULT WINAPI IStream_Size(IStream *stream, ULARGE_INTEGER *size)
{
    STATSTG stat;
    memset(&stat, 0, sizeof(stat));
    HRESULT hr = stream->lpVtbl->Stat(stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr))
        *size = stat.cbSize;
    return hr;
}

HRESULT WINAPI IStream_Copy(IStream *from, IStream *to, DWORD size)
{
    ULARGE_INTEGER want = {.QuadPart = size}, read = {0};
    HRESULT hr = from->lpVtbl->CopyTo(from, to, want, nullptr, &read);
    return SUCCEEDED(hr) && read.QuadPart != size ? E_FAIL : hr;
}

/* a WORD of length, then the characters without their terminator. */
HRESULT WINAPI IStream_ReadStr(IStream *stream, LPWSTR *string)
{
    *string = nullptr;
    WORD length = 0;
    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, &length, 2, &read);
    if (FAILED(hr))
        return hr;
    if (read != 2)
        return E_FAIL;
    LPWSTR text = CoTaskMemAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    memset(text, 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
    hr = stream->lpVtbl->Read(stream, text, length * 2u, &read);
    if (SUCCEEDED(hr) && read != length * 2u)
        hr = E_FAIL;
    if (FAILED(hr)) {
        CoTaskMemFree(text);
        return hr;
    }
    text[length] = 0;
    *string = text;
    return hr;
}

HRESULT WINAPI IStream_WriteStr(IStream *stream, LPCWSTR string)
{
    WORD length = string ? (WORD)wcslen(string) : 0;
    HRESULT hr = stream->lpVtbl->Write(stream, &length, 2, nullptr);
    if (SUCCEEDED(hr))
        hr = stream->lpVtbl->Write(stream, string, length * 2u, nullptr);
    return hr;
}
