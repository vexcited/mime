#include "shcore_private.h"
#include <winrt/winrt.h>
#include <mime/win32/winrt_streams.h>

static const WCHAR g_stream_class[] = L"Windows.Storage.Streams.FileRandomAccessStream";

static const WRT_ASYNC_TYPE g_read_type = {
    L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Storage.Streams.IBuffer, UInt32>",
    &IID_IAsyncOperationWithProgressBuffer, WRT_RESULT_OBJECT, TRUE};
static const WRT_ASYNC_TYPE g_write_type = {L"Windows.Foundation.IAsyncOperationWithProgress`2<UInt32, UInt32>",
                                            &IID_IAsyncOperationWithProgressUInt32, WRT_RESULT_UINT32, TRUE};
static const WRT_ASYNC_TYPE g_flush_type = {L"Windows.Foundation.IAsyncOperation`1<Boolean>", &IID_IAsyncOperationBool,
                                            WRT_RESULT_BOOL, FALSE};

/* the file a stream and its clones share. */
typedef struct FILE_REF {
    volatile LONG Refs;
    SRWLOCK Lock;
    HANDLE File;
    BOOL Write;
} FILE_REF;

typedef struct STREAM {
    WRT_AGILE Object;
    SRWLOCK Lock;
    FILE_REF *File;
    UINT64 Position;
} STREAM;

static STREAM *stream_of(void *iface)
{
    return (STREAM *)((WRT_IFACE *)iface)->Owner;
}

static void file_release(FILE_REF *file)
{
    if (file && !InterlockedDecrement(&file->Refs)) {
        CloseHandle(file->File);
        HeapFree(GetProcessHeap(), 0, file);
    }
}

/* the file of an open stream with a reference, or nothing once it is closed. */
static FILE_REF *stream_file(STREAM *s)
{
    AcquireSRWLockShared(&s->Lock);
    FILE_REF *file = s->File;
    if (file)
        InterlockedIncrement(&file->Refs);
    ReleaseSRWLockShared(&s->Lock);
    return file;
}

static HRESULT last_error(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static HRESULT file_size(FILE_REF *file, UINT64 *size)
{
    LARGE_INTEGER value;
    if (!GetFileSizeEx(file->File, &value))
        return last_error();
    *size = (UINT64)value.QuadPart;
    return S_OK;
}

WRT_AGILE_THUNKS(IRandomAccessStream)
WRT_AGILE_THUNKS(IInputStream)
WRT_AGILE_THUNKS(IOutputStream)
WRT_AGILE_THUNKS(IClosable)

static HRESULT stream_create(FILE_REF *file, UINT64 position, STREAM **result);

static HRESULT STDMETHODCALLTYPE ras_get_size(IRandomAccessStream *This, UINT64 *value)
{
    if (!value)
        return E_POINTER;
    *value = 0;
    FILE_REF *file = stream_file(stream_of(This));
    if (!file)
        return RO_E_CLOSED_;
    HRESULT hr = file_size(file, value);
    file_release(file);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ras_put_size(IRandomAccessStream *This, UINT64 value)
{
    FILE_REF *file = stream_file(stream_of(This));
    if (!file)
        return RO_E_CLOSED_;
    HRESULT hr = file->Write ? S_OK : E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        AcquireSRWLockExclusive(&file->Lock);
        LARGE_INTEGER at = {.QuadPart = (LONGLONG)value};
        if (!SetFilePointerEx(file->File, at, nullptr, FILE_BEGIN) || !SetEndOfFile(file->File))
            hr = last_error();
        ReleaseSRWLockExclusive(&file->Lock);
    }
    file_release(file);
    return hr;
}

static HRESULT stream_at(STREAM *s, UINT64 position, BOOL write, REFIID riid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    FILE_REF *file = stream_file(s);
    if (!file)
        return RO_E_CLOSED_;
    STREAM *other = nullptr;
    HRESULT hr = write && !file->Write ? E_ACCESSDENIED : stream_create(file, position, &other);
    file_release(file);
    if (FAILED(hr))
        return hr;
    hr = WrtAgileQuery(&other->Object.Base.Ifaces[0], riid, result);
    WrtAgileRelease(&other->Object.Base.Ifaces[0]);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ras_get_input_stream_at(IRandomAccessStream *This, UINT64 position,
                                                         IInputStream **stream)
{
    return stream_at(stream_of(This), position, FALSE, &IID_IInputStream, (void **)stream);
}

static HRESULT STDMETHODCALLTYPE ras_get_output_stream_at(IRandomAccessStream *This, UINT64 position,
                                                          IOutputStream **stream)
{
    return stream_at(stream_of(This), position, TRUE, &IID_IOutputStream, (void **)stream);
}

static HRESULT STDMETHODCALLTYPE ras_get_position(IRandomAccessStream *This, UINT64 *value)
{
    STREAM *s = stream_of(This);
    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&s->Lock);
    HRESULT hr = s->File ? S_OK : RO_E_CLOSED_;
    *value = SUCCEEDED(hr) ? s->Position : 0;
    ReleaseSRWLockShared(&s->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ras_seek(IRandomAccessStream *This, UINT64 position)
{
    STREAM *s = stream_of(This);
    AcquireSRWLockExclusive(&s->Lock);
    HRESULT hr = s->File ? S_OK : RO_E_CLOSED_;
    if (SUCCEEDED(hr))
        s->Position = position;
    ReleaseSRWLockExclusive(&s->Lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ras_clone_stream(IRandomAccessStream *This, IRandomAccessStream **stream)
{
    return stream_at(stream_of(This), 0, FALSE, &IID_IRandomAccessStream, (void **)stream);
}

static HRESULT STDMETHODCALLTYPE ras_get_can_read(IRandomAccessStream *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    FILE_REF *file = stream_file(stream_of(This));
    *value = file != nullptr;
    file_release(file);
    return file ? S_OK : RO_E_CLOSED_;
}

static HRESULT STDMETHODCALLTYPE ras_get_can_write(IRandomAccessStream *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    FILE_REF *file = stream_file(stream_of(This));
    *value = file && file->Write;
    file_release(file);
    return file ? S_OK : RO_E_CLOSED_;
}

static HRESULT buffer_bytes(IBuffer *buffer, BYTE **bytes)
{
    IBufferByteAccess *access;
    HRESULT hr = buffer->lpVtbl->QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&access);
    if (FAILED(hr))
        return hr;
    hr = access->lpVtbl->Buffer(access, bytes);
    access->lpVtbl->Release(access);
    return hr;
}

/* the read is done at once, so every option reads what there is up to the count. */
static HRESULT STDMETHODCALLTYPE input_read_async(IInputStream *This, IBuffer *buffer, UINT32 count,
                                                  InputStreamOptions options, void **operation)
{
    STREAM *s = stream_of(This);
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    if (!buffer)
        return E_INVALIDARG;
    UINT32 capacity = 0;
    BYTE *bytes = nullptr;
    HRESULT hr = buffer->lpVtbl->get_Capacity(buffer, &capacity);
    if (SUCCEEDED(hr) && count > capacity)
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
        hr = buffer_bytes(buffer, &bytes);
    if (FAILED(hr))
        return hr;
    FILE_REF *file = stream_file(s);
    if (!file)
        return RO_E_CLOSED_;
    AcquireSRWLockExclusive(&s->Lock);
    UINT64 position = s->Position;
    DWORD done = 0;
    OVERLAPPED at = {.Offset = (DWORD)position, .OffsetHigh = (DWORD)(position >> 32)};
    AcquireSRWLockExclusive(&file->Lock);
    if (count && !ReadFile(file->File, bytes, count, &done, &at) && GetLastError() != ERROR_HANDLE_EOF)
        hr = last_error();
    ReleaseSRWLockExclusive(&file->Lock);
    if (SUCCEEDED(hr))
        s->Position = position + done;
    ReleaseSRWLockExclusive(&s->Lock);
    file_release(file);
    if (SUCCEEDED(hr))
        hr = buffer->lpVtbl->put_Length(buffer, done);
    if (SUCCEEDED(hr))
        buffer->lpVtbl->AddRef(buffer);
    return WrtAsyncCompleted(&g_read_type, hr, 0, SUCCEEDED(hr) ? (IUnknown *)buffer : nullptr, operation);
}

static HRESULT STDMETHODCALLTYPE output_write_async(IOutputStream *This, IBuffer *buffer, void **operation)
{
    STREAM *s = stream_of(This);
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    if (!buffer)
        return E_INVALIDARG;
    UINT32 length = 0;
    BYTE *bytes = nullptr;
    HRESULT hr = buffer->lpVtbl->get_Length(buffer, &length);
    if (SUCCEEDED(hr))
        hr = buffer_bytes(buffer, &bytes);
    if (FAILED(hr))
        return hr;
    FILE_REF *file = stream_file(s);
    if (!file)
        return RO_E_CLOSED_;
    if (!file->Write) {
        file_release(file);
        return E_ACCESSDENIED;
    }
    AcquireSRWLockExclusive(&s->Lock);
    UINT64 position = s->Position;
    DWORD done = 0;
    OVERLAPPED at = {.Offset = (DWORD)position, .OffsetHigh = (DWORD)(position >> 32)};
    AcquireSRWLockExclusive(&file->Lock);
    if (length && !WriteFile(file->File, bytes, length, &done, &at))
        hr = last_error();
    ReleaseSRWLockExclusive(&file->Lock);
    if (SUCCEEDED(hr))
        s->Position = position + done;
    ReleaseSRWLockExclusive(&s->Lock);
    file_release(file);
    return WrtAsyncCompleted(&g_write_type, hr, done, nullptr, operation);
}

static HRESULT STDMETHODCALLTYPE output_flush_async(IOutputStream *This, void **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = nullptr;
    FILE_REF *file = stream_file(stream_of(This));
    if (!file)
        return RO_E_CLOSED_;
    HRESULT hr = !file->Write || FlushFileBuffers(file->File) ? S_OK : last_error();
    file_release(file);
    return WrtAsyncCompleted(&g_flush_type, hr, SUCCEEDED(hr), nullptr, operation);
}

/* the stream lets go of its file; the clones keep it open. */
static HRESULT STDMETHODCALLTYPE closable_close(IClosable *This)
{
    STREAM *s = stream_of(This);
    AcquireSRWLockExclusive(&s->Lock);
    FILE_REF *file = s->File;
    s->File = nullptr;
    ReleaseSRWLockExclusive(&s->Lock);
    file_release(file);
    return S_OK;
}

static const IRandomAccessStreamVtbl ras_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IRandomAccessStream),
    ras_get_size,
    ras_put_size,
    ras_get_input_stream_at,
    ras_get_output_stream_at,
    ras_get_position,
    ras_seek,
    ras_clone_stream,
    ras_get_can_read,
    ras_get_can_write,
};
static const IInputStreamVtbl input_vtbl = {WRT_INSPECTABLE_ENTRIES(IInputStream), input_read_async};
static const IOutputStreamVtbl output_vtbl = {WRT_INSPECTABLE_ENTRIES(IOutputStream), output_write_async,
                                              output_flush_async};
static const IClosableVtbl closable_vtbl = {WRT_INSPECTABLE_ENTRIES(IClosable), closable_close};

static void stream_destroy(WRT_OBJECT *object)
{
    STREAM *s = (STREAM *)object;
    file_release(s->File);
    HeapFree(GetProcessHeap(), 0, s);
}

static const WRT_CLASS g_class = {g_stream_class,
                                  5,
                                  {&IID_IRandomAccessStream, &IID_IInputStream, &IID_IOutputStream, &IID_IClosable,
                                   &IID_IWeakReferenceSource},
                                  1u << 4,
                                  stream_destroy};

static HRESULT stream_create(FILE_REF *file, UINT64 position, STREAM **result)
{
    STREAM *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    const void *vtables[5] = {&ras_vtbl, &input_vtbl, &output_vtbl, &closable_vtbl, &WrtWeakSourceVtbl};
    WrtAgileInit(&s->Object, &g_class, vtables);
    InitializeSRWLock(&s->Lock);
    InterlockedIncrement(&file->Refs);
    s->File = file;
    s->Position = position;
    *result = s;
    return S_OK;
}

/* a read stream lets others read, write and delete the file; a read and write one only lets them read. */
HRESULT WINAPI CreateRandomAccessStreamOnFile(LPCWSTR path, DWORD access, REFIID riid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!path || (access != FileAccessMode_Read && access != FileAccessMode_ReadWrite))
        return E_INVALIDARG;
    BOOL write = access == FileAccessMode_ReadWrite;
    HANDLE handle = CreateFileW(path, write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                write ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, write ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return last_error();
    FILE_REF *file = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*file));
    if (!file) {
        CloseHandle(handle);
        return E_OUTOFMEMORY;
    }
    file->Refs = 1;
    file->File = handle;
    file->Write = write;
    InitializeSRWLock(&file->Lock);
    STREAM *s;
    HRESULT hr = stream_create(file, 0, &s);
    file_release(file);
    if (FAILED(hr))
        return hr;
    hr = WrtAgileQuery(&s->Object.Base.Ifaces[0], riid, result);
    WrtAgileRelease(&s->Object.Base.Ifaces[0]);
    return hr;
}
