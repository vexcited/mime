/*
 * font file references, the loader of files on disk and its memory mapped
 * streams.
 */
#include "dw_private.h"
#include <png/inflate.h>

/* ------------------------------------------------------------------ mapped file streams */

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

/* ------------------------------------------------------------------ local loader */

/* the write time the reference was made with, then the path and its terminator. */
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

/* ------------------------------------------------------------------ file object */

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

BOOL DwSameFile(IDWriteFontFile *a, IDWriteFontFile *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    const void *ka, *kb;
    UINT32 sa, sb;
    IDWriteFontFileLoader *la, *lb;
    if (FAILED(a->lpVtbl->GetReferenceKey(a, &ka, &sa)) || FAILED(b->lpVtbl->GetReferenceKey(b, &kb, &sb)) || sa != sb ||
        memcmp(ka, kb, sa))
        return FALSE;
    if (FAILED(a->lpVtbl->GetLoader(a, &la)))
        return FALSE;
    if (FAILED(b->lpVtbl->GetLoader(b, &lb))) {
        la->lpVtbl->Release(la);
        return FALSE;
    }
    BOOL same = la == lb;
    la->lpVtbl->Release(la);
    lb->lpVtbl->Release(lb);
    return same;
}

/* ------------------------------------------------------------------ memory */

/* bytes copied for a stream or a loader, alive as long as one of them holds them. */
typedef struct DwBlob {
    const IUnknownVtbl *lpVtbl;
    LONG refs;
    UINT64 size;
    BYTE bytes[];
} DwBlob;

static HRESULT WINAPI blob_query(IUnknown *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI blob_addref(IUnknown *This) { return InterlockedIncrement(&((DwBlob *)This)->refs); }

static ULONG WINAPI blob_release(IUnknown *This)
{
    DwBlob *b = (DwBlob *)This;
    ULONG refs = InterlockedDecrement(&b->refs);
    if (!refs)
        DwFree(b);
    return refs;
}

static const IUnknownVtbl g_blob_vtbl = {blob_query, blob_addref, blob_release};

static DwBlob *blob_new(const void *data, UINT64 size)
{
    if (size > 0x7fffffff)
        return nullptr;
    DwBlob *b = DwAlloc(sizeof(*b) + (SIZE_T)size);
    if (!b)
        return nullptr;
    b->lpVtbl = &g_blob_vtbl;
    b->refs = 1;
    b->size = size;
    if (size)
        memcpy(b->bytes, data, (SIZE_T)size);
    return b;
}

typedef struct DwMemoryStream {
    const IDWriteFontFileStreamVtbl *lpVtbl;
    LONG refs;
    const BYTE *data;
    UINT64 size;
    IUnknown *owner;
} DwMemoryStream;

static ULONG WINAPI memory_addref(IDWriteFontFileStream *This)
{
    return InterlockedIncrement(&((DwMemoryStream *)This)->refs);
}

static ULONG WINAPI memory_release(IDWriteFontFileStream *This)
{
    DwMemoryStream *s = (DwMemoryStream *)This;
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        if (s->owner)
            s->owner->lpVtbl->Release(s->owner);
        DwFree(s);
    }
    return refs;
}

static HRESULT WINAPI memory_read(IDWriteFontFileStream *This, const void **start, UINT64 offset, UINT64 size,
                                  void **context)
{
    DwMemoryStream *s = (DwMemoryStream *)This;
    if (!start || !context)
        return E_POINTER;
    *start = nullptr;
    *context = nullptr;
    if (offset > s->size || size > s->size - offset)
        return E_FAIL;
    *start = s->data + offset;
    return S_OK;
}

static HRESULT WINAPI memory_size(IDWriteFontFileStream *This, UINT64 *size)
{
    if (!size)
        return E_POINTER;
    *size = ((DwMemoryStream *)This)->size;
    return S_OK;
}

/* memory has no write time. */
static HRESULT WINAPI memory_time(IDWriteFontFileStream *This, UINT64 *time)
{
    if (!time)
        return E_POINTER;
    *time = 0;
    return E_NOTIMPL;
}

static const IDWriteFontFileStreamVtbl g_memory_vtbl = {
    stream_query, memory_addref, memory_release, memory_read, stream_release_fragment, memory_size, memory_time,
};

HRESULT DwMemoryStreamCreate(const void *data, UINT64 size, IUnknown *owner, BOOL copy, IDWriteFontFileStream **out)
{
    *out = nullptr;
    DwMemoryStream *s = DwAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpVtbl = &g_memory_vtbl;
    s->refs = 1;
    s->size = size;
    if (copy) {
        DwBlob *b = blob_new(data, size);
        if (!b) {
            DwFree(s);
            return E_OUTOFMEMORY;
        }
        s->owner = (IUnknown *)b;
        s->data = b->bytes;
    } else {
        s->data = data;
        s->owner = owner;
        if (owner)
            owner->lpVtbl->AddRef(owner);
    }
    *out = (IDWriteFontFileStream *)s;
    return S_OK;
}

/* ------------------------------------------------------------------ in memory loader */

typedef struct DwMemoryFile {
    const BYTE *data;
    UINT64 size;
    IUnknown *owner;
} DwMemoryFile;

typedef struct DwMemoryLoader {
    const IDWriteInMemoryFontFileLoaderVtbl *lpVtbl;
    LONG refs;
    SRWLOCK lock;
    DwMemoryFile *files;
    UINT32 count, capacity;
} DwMemoryLoader;

static HRESULT WINAPI memory_loader_query(IDWriteInMemoryFontFileLoader *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFileLoader) ||
        IsEqualGUID(riid, &IID_IDWriteInMemoryFontFileLoader)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI memory_loader_addref(IDWriteInMemoryFontFileLoader *This)
{
    return InterlockedIncrement(&((DwMemoryLoader *)This)->refs);
}

static ULONG WINAPI memory_loader_release(IDWriteInMemoryFontFileLoader *This)
{
    DwMemoryLoader *l = (DwMemoryLoader *)This;
    ULONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        for (UINT32 i = 0; i < l->count; i++) {
            if (l->files[i].owner)
                l->files[i].owner->lpVtbl->Release(l->files[i].owner);
        }
        DwFree(l->files);
        DwFree(l);
    }
    return refs;
}

static HRESULT WINAPI memory_loader_stream(IDWriteInMemoryFontFileLoader *This, const void *key, UINT32 size,
                                           IDWriteFontFileStream **stream)
{
    DwMemoryLoader *l = (DwMemoryLoader *)This;
    if (!stream)
        return E_POINTER;
    *stream = nullptr;
    if (!key || size != sizeof(UINT32))
        return E_INVALIDARG;
    UINT32 index = *(const UINT32 *)key;
    AcquireSRWLockShared(&l->lock);
    HRESULT hr = E_INVALIDARG;
    if (index < l->count)
        hr = DwMemoryStreamCreate(l->files[index].data, l->files[index].size, l->files[index].owner, FALSE, stream);
    ReleaseSRWLockShared(&l->lock);
    return hr;
}

static HRESULT WINAPI memory_loader_reference(IDWriteInMemoryFontFileLoader *This, IDWriteFactory *factory,
                                              const void *data, UINT32 size, IUnknown *owner, IDWriteFontFile **file)
{
    DwMemoryLoader *l = (DwMemoryLoader *)This;
    if (!file)
        return E_POINTER;
    *file = nullptr;
    if (!data && size)
        return E_INVALIDARG;
    DwMemoryFile entry = {data, size, owner};
    /* without an owner the loader keeps a copy. */
    if (owner) {
        owner->lpVtbl->AddRef(owner);
    } else {
        DwBlob *b = blob_new(data, size);
        if (!b)
            return E_OUTOFMEMORY;
        entry.data = b->bytes;
        entry.owner = (IUnknown *)b;
    }
    AcquireSRWLockExclusive(&l->lock);
    HRESULT hr = S_OK;
    if (l->count == l->capacity) {
        UINT32 capacity = l->capacity ? l->capacity * 2 : 8;
        DwMemoryFile *grown = DwAlloc(capacity * sizeof(*grown));
        if (grown) {
            if (l->count)
                memcpy(grown, l->files, l->count * sizeof(*grown));
            DwFree(l->files);
            l->files = grown;
            l->capacity = capacity;
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    UINT32 index = l->count;
    if (SUCCEEDED(hr))
        l->files[l->count++] = entry;
    ReleaseSRWLockExclusive(&l->lock);
    if (FAILED(hr)) {
        entry.owner->lpVtbl->Release(entry.owner);
        return hr;
    }
    return DwFontFileCreate((IDWriteFontFileLoader *)This, &index, sizeof(index), file);
}

static UINT32 WINAPI memory_loader_count(IDWriteInMemoryFontFileLoader *This)
{
    DwMemoryLoader *l = (DwMemoryLoader *)This;
    AcquireSRWLockShared(&l->lock);
    UINT32 count = l->count;
    ReleaseSRWLockShared(&l->lock);
    return count;
}

static const IDWriteInMemoryFontFileLoaderVtbl g_memory_loader_vtbl = {
    memory_loader_query,     memory_loader_addref, memory_loader_release,
    memory_loader_stream,    memory_loader_reference, memory_loader_count,
};

HRESULT DwInMemoryLoaderCreate(IDWriteInMemoryFontFileLoader **out)
{
    if (!out)
        return E_POINTER;
    DwMemoryLoader *l = DwAllocZero(sizeof(*l));
    if (!l) {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    l->lpVtbl = &g_memory_loader_vtbl;
    l->refs = 1;
    InitializeSRWLock(&l->lock);
    *out = (IDWriteInMemoryFontFileLoader *)l;
    return S_OK;
}

/* ------------------------------------------------------------------ woff */

HRESULT DwUnpackWoff(const void *data, UINT32 size, IDWriteFontFileStream **out)
{
    const BYTE *woff = data;
    *out = nullptr;
    if (size < 44 || DwBe32(woff) != DW_TAG('w', 'O', 'F', 'F'))
        return DWRITE_E_FILEFORMAT;
    UINT32 tables = DwBe16(woff + 12), total = DwBe32(woff + 16);
    if (!tables || size - 44 < 20ull * tables || total < 12 + 16ull * tables || total > 0x10000000)
        return DWRITE_E_FILEFORMAT;
    /* the directory's tables, each four byte aligned after the header and the records. */
    UINT64 need = 12 + 16ull * tables;
    for (UINT32 i = 0; i < tables; i++)
        need += (DwBe32(woff + 44 + 20 * i + 12) + 3ull) & ~3ull;
    if (need > 0x10000000)
        return DWRITE_E_FILEFORMAT;
    BYTE *sfnt = DwAllocZero((SIZE_T)need);
    if (!sfnt)
        return E_OUTOFMEMORY;
    UINT32 search = 1, selector = 0;
    while (search * 2 <= tables) {
        search *= 2;
        selector++;
    }
    memcpy(sfnt, woff + 4, 4);
    sfnt[4] = (BYTE)(tables >> 8);
    sfnt[5] = (BYTE)tables;
    sfnt[6] = (BYTE)((search * 16) >> 8);
    sfnt[7] = (BYTE)(search * 16);
    sfnt[8] = (BYTE)(selector >> 8);
    sfnt[9] = (BYTE)selector;
    sfnt[10] = (BYTE)((tables * 16 - search * 16) >> 8);
    sfnt[11] = (BYTE)(tables * 16 - search * 16);
    UINT64 offset = 12 + 16ull * tables;
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < tables && SUCCEEDED(hr); i++) {
        const BYTE *e = woff + 44 + 20 * i;
        UINT32 at = DwBe32(e + 4), compressed = DwBe32(e + 8), length = DwBe32(e + 12);
        if (at > size || compressed > size - at || compressed > length) {
            hr = DWRITE_E_FILEFORMAT;
            break;
        }
        BYTE *record = sfnt + 12 + 16 * i;
        memcpy(record, e, 4);
        memcpy(record + 4, e + 16, 4);
        for (int b = 0; b < 4; b++) {
            record[8 + b] = (BYTE)(offset >> (24 - 8 * b));
            record[12 + b] = (BYTE)(length >> (24 - 8 * b));
        }
        if (compressed == length) {
            memcpy(sfnt + offset, woff + at, length);
        } else {
            size_t written = 0;
            if (mime_inflate(woff + at, compressed, sfnt + offset, length, &written, MIME_INFLATE_ZLIB) !=
                    MIME_INFLATE_OK ||
                written != length)
                hr = DWRITE_E_FILEFORMAT;
        }
        offset += (length + 3ull) & ~3ull;
    }
    if (SUCCEEDED(hr))
        hr = DwMemoryStreamCreate(sfnt, need, nullptr, TRUE, out);
    DwFree(sfnt);
    return hr;
}
