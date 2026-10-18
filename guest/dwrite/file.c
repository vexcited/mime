/*
 * font files: the references programs and collections hold to them, the
 * loader of files on disk and its memory mapped streams.
 */
#include "dw_private.h"

/* ------------------------------------------------------------------ streams over mapped files */

typedef struct DwMappedStream {
    const IDWriteFontFileStreamVtbl *lpVtbl;
    LONG refs;
    HANDLE mapping;
    BYTE *view;
    UINT64 size;
    UINT64 write_time;
} DwMappedStream;

static HRESULT WINAPI stream_query(IDWriteFontFileStream *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFileStream)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_addref(IDWriteFontFileStream *This)
{
    return InterlockedIncrement(&((DwMappedStream *)This)->refs);
}

static ULONG WINAPI stream_release(IDWriteFontFileStream *This)
{
    DwMappedStream *s = (DwMappedStream *)This;
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        if (s->view)
            UnmapViewOfFile(s->view);
        if (s->mapping)
            CloseHandle(s->mapping);
        DwFree(s);
    }
    return refs;
}

static HRESULT WINAPI stream_read(IDWriteFontFileStream *This, const void **start, UINT64 offset, UINT64 size,
                                  void **context)
{
    DwMappedStream *s = (DwMappedStream *)This;
    if (!start || !context)
        return E_POINTER;
    *start = nullptr;
    *context = nullptr;
    if (offset > s->size || size > s->size - offset)
        return E_FAIL;
    *start = s->view ? s->view + offset : nullptr;
    return S_OK;
}

static void WINAPI stream_release_fragment(IDWriteFontFileStream *This, void *context) {}

static HRESULT WINAPI stream_size(IDWriteFontFileStream *This, UINT64 *size)
{
    if (!size)
        return E_POINTER;
    *size = ((DwMappedStream *)This)->size;
    return S_OK;
}

static HRESULT WINAPI stream_time(IDWriteFontFileStream *This, UINT64 *time)
{
    if (!time)
        return E_POINTER;
    *time = ((DwMappedStream *)This)->write_time;
    return S_OK;
}

static const IDWriteFontFileStreamVtbl g_stream_vtbl = {
    stream_query, stream_addref, stream_release, stream_read, stream_release_fragment, stream_size, stream_time,
};

/* ------------------------------------------------------------------ the local loader */

/* a key is the write time the reference was made with, then the path with its terminator. */
typedef struct DwLocalKey {
    UINT64 write_time;
    WCHAR path[1];
} DwLocalKey;

const WCHAR *DwLocalKeyPath(const void *key, UINT32 size)
{
    const DwLocalKey *k = key;
    if (!k || size < sizeof(UINT64) + sizeof(WCHAR) || (size - sizeof(UINT64)) % sizeof(WCHAR))
        return nullptr;
    UINT32 chars = (size - (UINT32)sizeof(UINT64)) / sizeof(WCHAR);
    if (k->path[chars - 1])
        return nullptr;
    return k->path;
}

static HRESULT WINAPI local_query(IDWriteLocalFontFileLoader *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFileLoader) ||
        IsEqualGUID(riid, &IID_IDWriteLocalFontFileLoader)) {
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI local_addref(IDWriteLocalFontFileLoader *This) { return 2; }
static ULONG WINAPI local_release(IDWriteLocalFontFileLoader *This) { return 1; }

static HRESULT WINAPI local_create_stream(IDWriteLocalFontFileLoader *This, const void *key, UINT32 size,
                                          IDWriteFontFileStream **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const WCHAR *path = DwLocalKeyPath(key, size);
    if (!path)
        return E_INVALIDARG;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER length;
    FILETIME time = {};
    if (!GetFileSizeEx(file, &length)) {
        DWORD error = GetLastError();
        CloseHandle(file);
        return HRESULT_FROM_WIN32(error);
    }
    GetFileTime(file, nullptr, nullptr, &time);
    DwMappedStream *s = DwAllocZero(sizeof(*s));
    if (!s) {
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }
    s->lpVtbl = &g_stream_vtbl;
    s->refs = 1;
    s->size = (UINT64)length.QuadPart;
    s->write_time = ((UINT64)time.dwHighDateTime << 32) | time.dwLowDateTime;
    if (s->size) {
        s->mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        s->view = s->mapping ? MapViewOfFile(s->mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (!s->view) {
            DWORD error = GetLastError();
            stream_release((IDWriteFontFileStream *)s);
            CloseHandle(file);
            return HRESULT_FROM_WIN32(error ? error : ERROR_NOT_ENOUGH_MEMORY);
        }
    }
    CloseHandle(file);
    *out = (IDWriteFontFileStream *)s;
    return S_OK;
}

static HRESULT WINAPI local_path_length(IDWriteLocalFontFileLoader *This, const void *key, UINT32 size, UINT32 *length)
{
    if (!length)
        return E_POINTER;
    const WCHAR *path = DwLocalKeyPath(key, size);
    if (!path) {
        *length = 0;
        return E_INVALIDARG;
    }
    *length = (UINT32)wcslen(path);
    return S_OK;
}

static HRESULT WINAPI local_path(IDWriteLocalFontFileLoader *This, const void *key, UINT32 size, WCHAR *buffer,
                                 UINT32 buffer_size)
{
    const WCHAR *path = DwLocalKeyPath(key, size);
    if (!path)
        return E_INVALIDARG;
    UINT32 length = (UINT32)wcslen(path);
    if (!buffer || buffer_size < length + 1)
        return E_INVALIDARG;
    memcpy(buffer, path, (length + 1) * sizeof(WCHAR));
    return S_OK;
}

static HRESULT WINAPI local_time(IDWriteLocalFontFileLoader *This, const void *key, UINT32 size, FILETIME *time)
{
    if (!time)
        return E_POINTER;
    if (!DwLocalKeyPath(key, size))
        return E_INVALIDARG;
    UINT64 t = ((const DwLocalKey *)key)->write_time;
    time->dwLowDateTime = (DWORD)t;
    time->dwHighDateTime = (DWORD)(t >> 32);
    return S_OK;
}

static const IDWriteLocalFontFileLoaderVtbl g_local_vtbl = {
    local_query, local_addref, local_release, local_create_stream, local_path_length, local_path, local_time,
};
static IDWriteLocalFontFileLoader g_local_loader = {&g_local_vtbl};

IDWriteLocalFontFileLoader *DwLocalLoader(void) { return &g_local_loader; }

/* ------------------------------------------------------------------ the file object */

static HRESULT WINAPI file_query(IDWriteFontFile *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFile)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI file_addref(IDWriteFontFile *This) { return InterlockedIncrement(&((DwFontFile *)This)->refs); }

static ULONG WINAPI file_release(IDWriteFontFile *This)
{
    DwFontFile *f = (DwFontFile *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        if (f->loader)
            f->loader->lpVtbl->Release(f->loader);
        DwFree(f->key);
        DwFree(f);
    }
    return refs;
}

static HRESULT WINAPI file_key(IDWriteFontFile *This, const void **key, UINT32 *size)
{
    DwFontFile *f = (DwFontFile *)This;
    if (!key || !size)
        return E_POINTER;
    *key = f->key;
    *size = f->key_size;
    return S_OK;
}

static HRESULT WINAPI file_loader(IDWriteFontFile *This, IDWriteFontFileLoader **loader)
{
    DwFontFile *f = (DwFontFile *)This;
    if (!loader)
        return E_POINTER;
    *loader = f->loader;
    f->loader->lpVtbl->AddRef(f->loader);
    return S_OK;
}

HRESULT DwFontFileOpen(IDWriteFontFile *file, IDWriteFontFileStream **stream)
{
    const void *key;
    UINT32 key_size;
    IDWriteFontFileLoader *loader;
    HRESULT hr = file->lpVtbl->GetReferenceKey(file, &key, &key_size);
    if (SUCCEEDED(hr))
        hr = file->lpVtbl->GetLoader(file, &loader);
    if (FAILED(hr))
        return hr;
    hr = loader->lpVtbl->CreateStreamFromKey(loader, key, key_size, stream);
    loader->lpVtbl->Release(loader);
    return hr;
}

BOOL DwSniff(const BYTE *bytes, UINT64 size, DWRITE_FONT_FILE_TYPE *file_type, DWRITE_FONT_FACE_TYPE *face_type,
             UINT32 *faces)
{
    *file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    *face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    *faces = 0;
    if (size < 12)
        return FALSE;
    UINT32 tag = DwBe32(bytes);
    if (tag == DW_TAG('t', 't', 'c', 'f')) {
        UINT32 count = DwBe32(bytes + 8);
        if (!count || count > 0xffff || size < 12 + 4ull * count)
            return FALSE;
        *file_type = DWRITE_FONT_FILE_TYPE_TRUETYPE_COLLECTION;
        *face_type = DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION;
        *faces = count;
        return TRUE;
    }
    if (tag != 0x00010000 && tag != DW_TAG('O', 'T', 'T', 'O') && tag != DW_TAG('t', 'r', 'u', 'e'))
        return FALSE;
    UINT32 tables = DwBe16(bytes + 4);
    if (!tables || size < 12 + 16ull * tables)
        return FALSE;
    if (tag == DW_TAG('O', 'T', 'T', 'O')) {
        *file_type = DWRITE_FONT_FILE_TYPE_CFF;
        *face_type = DWRITE_FONT_FACE_TYPE_CFF;
    } else {
        *file_type = DWRITE_FONT_FILE_TYPE_TRUETYPE;
        *face_type = DWRITE_FONT_FACE_TYPE_TRUETYPE;
    }
    *faces = 1;
    return TRUE;
}

static HRESULT WINAPI file_analyze(IDWriteFontFile *This, BOOL *supported, DWRITE_FONT_FILE_TYPE *file_type,
                                   DWRITE_FONT_FACE_TYPE *face_type, UINT32 *faces)
{
    if (!supported || !file_type || !face_type || !faces)
        return E_POINTER;
    *supported = FALSE;
    *file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    *face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    *faces = 0;
    IDWriteFontFileStream *stream;
    HRESULT hr = DwFontFileOpen(This, &stream);
    if (FAILED(hr))
        return hr;
    UINT64 size = 0;
    hr = stream->lpVtbl->GetFileSize(stream, &size);
    if (SUCCEEDED(hr)) {
        /* enough for a collection's header and a font's table directory. */
        UINT64 want = size < 0x10000 ? size : 0x10000;
        const void *start;
        void *context;
        hr = stream->lpVtbl->ReadFileFragment(stream, &start, 0, want, &context);
        if (SUCCEEDED(hr)) {
            *supported = DwSniff(start, want, file_type, face_type, faces);
            stream->lpVtbl->ReleaseFileFragment(stream, context);
            if (!*supported)
                *faces = 0;
        }
    }
    stream->lpVtbl->Release(stream);
    return hr;
}

static const IDWriteFontFileVtbl g_file_vtbl = {
    file_query, file_addref, file_release, file_key, file_loader, file_analyze,
};

HRESULT DwFontFileCreate(IDWriteFontFileLoader *loader, const void *key, UINT32 key_size, IDWriteFontFile **out)
{
    DwFontFile *f = DwAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->key = DwAlloc(key_size);
    if (!f->key) {
        DwFree(f);
        return E_OUTOFMEMORY;
    }
    memcpy(f->key, key, key_size);
    f->key_size = key_size;
    f->lpVtbl = &g_file_vtbl;
    f->refs = 1;
    f->loader = loader;
    loader->lpVtbl->AddRef(loader);
    *out = (IDWriteFontFile *)f;
    return S_OK;
}

HRESULT DwFontFileFromPath(const WCHAR *path, const FILETIME *write_time, IDWriteFontFile **out)
{
    FILETIME time = {};
    if (write_time) {
        time = *write_time;
    } else {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
            return HRESULT_FROM_WIN32(GetLastError());
        time = data.ftLastWriteTime;
    }
    UINT32 chars = (UINT32)wcslen(path) + 1;
    UINT32 size = (UINT32)sizeof(UINT64) + chars * (UINT32)sizeof(WCHAR);
    DwLocalKey *key = DwAlloc(size);
    if (!key)
        return E_OUTOFMEMORY;
    key->write_time = ((UINT64)time.dwHighDateTime << 32) | time.dwLowDateTime;
    memcpy(key->path, path, chars * sizeof(WCHAR));
    HRESULT hr = DwFontFileCreate((IDWriteFontFileLoader *)&g_local_loader, key, size, out);
    DwFree(key);
    return hr;
}
