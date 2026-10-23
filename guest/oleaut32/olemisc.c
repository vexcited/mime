/*
 * picture helpers: loading from files and paths, saving, colors, cursors and the property frame, which has no
 * window to show.
 */
#include "picture.h"

/* ------------------------------------------------------------------ files as streams */

typedef struct {
    const IStreamVtbl *Vtbl;
    LONG Refs;
    HANDLE File;
} FILE_STREAM;

static HRESULT STDMETHODCALLTYPE fs_query(IStream *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IStream) &&
        !IsEqualIID(riid, &IID_ISequentialStream))
        return E_NOINTERFACE;
    InterlockedIncrement(&((FILE_STREAM *)This)->Refs);
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE fs_addref(IStream *This)
{
    return InterlockedIncrement(&((FILE_STREAM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE fs_release(IStream *This)
{
    FILE_STREAM *stream = (FILE_STREAM *)This;
    LONG refs = InterlockedDecrement(&stream->Refs);
    if (!refs) {
        CloseHandle(stream->File);
        AutFree(stream);
    }
    return refs;
}

/* maps a win32 file error to the Visual Basic runtime error. */
static HRESULT file_error(DWORD error)
{
    static const DWORD table[][2] = {
        {2, 0x800A0035}, {3, 0x800A004C}, {4, 0x800A0043}, {5, 0x800A004B}, {14, 0x800A0044}, {15, 0x800A0044},
        {18, 0x800A0043}, {19, 0x800A0046}, {21, 0x800A0047}, {25, 0x800A0039}, {29, 0x800A0039}, {30, 0x800A0039},
        {31, 0x800A0039}, {32, 0x800A0046}, {33, 0x800A0046}, {39, 0x800A003D}, {53, 0x800A004C}, {65, 0x800A004B},
        {82, 0x800A0043}, {83, 0x800A0047}, {112, 0x800A003D},
    };
    if (error > 0xFF)
        return (HRESULT)0x800A004B;
    for (int i = 0; i < (int)ARRAYSIZE(table); i++) {
        if (table[i][0] == error)
            return (HRESULT)table[i][1];
    }
    return (HRESULT)0x800A004B;
}

static HRESULT STDMETHODCALLTYPE fs_read(IStream *This, void *buffer, ULONG size, ULONG *read)
{
    DWORD done = 0;
    BOOL ok = ReadFile(((FILE_STREAM *)This)->File, buffer, size, &done, nullptr);
    if (read)
        *read = done;
    return ok ? S_OK : file_error(GetLastError());
}

static HRESULT STDMETHODCALLTYPE fs_write(IStream *This, const void *buffer, ULONG size, ULONG *written)
{
    DWORD done = 0;
    BOOL ok = WriteFile(((FILE_STREAM *)This)->File, buffer, size, &done, nullptr);
    if (written)
        *written = done;
    return ok ? S_OK : file_error(GetLastError());
}

static HRESULT STDMETHODCALLTYPE fs_seek(IStream *This, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    LARGE_INTEGER now = {};
    if (origin > STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    if (!SetFilePointerEx(((FILE_STREAM *)This)->File, move, &now, origin))
        return file_error(GetLastError());
    if (position)
        position->QuadPart = (ULONGLONG)now.QuadPart;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_set_size(IStream *This, ULARGE_INTEGER size)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fs_copy_to(IStream *This, IStream *target, ULARGE_INTEGER size, ULARGE_INTEGER *read,
                                            ULARGE_INTEGER *written)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fs_commit(IStream *This, DWORD flags)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fs_revert(IStream *This)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fs_lock(IStream *This, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE fs_stat(IStream *This, STATSTG *stat, DWORD flags)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fs_clone(IStream *This, IStream **clone)
{
    return E_NOTIMPL;
}

static const IStreamVtbl file_stream_vtbl = {
    fs_query, fs_addref, fs_release, fs_read,   fs_write,  fs_seek, fs_set_size,
    fs_copy_to, fs_commit, fs_revert, fs_lock, fs_lock, fs_stat, fs_clone,
};

/* opens the file for reading or overwriting, failing with Visual Basic runtime errors. */
static HRESULT file_stream(const WCHAR *path, BOOL write, IStream **stream)
{
    HANDLE file = write ? CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr)
                        : CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return file_error(GetLastError());
    FILE_STREAM *object = AutAllocZero(sizeof(FILE_STREAM));
    if (!object) {
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }
    object->Vtbl = &file_stream_vtbl;
    object->Refs = 1;
    object->File = file;
    *stream = (IStream *)object;
    return S_OK;
}

/* ------------------------------------------------------------------ the load functions */

WINOLEAUTAPI HRESULT WINAPI OleLoadPicture(LPSTREAM stream, LONG size, BOOL run_mode, REFIID riid, LPVOID *object)
{
    return OleLoadPictureEx(stream, size, run_mode, riid, 0, 0, 0, object);
}

static BOOL path_is_url(const WCHAR *path)
{
    int i = 0;
    while ((path[i] >= 'a' && path[i] <= 'z') || (path[i] >= 'A' && path[i] <= 'Z') || (path[i] >= '0' && path[i] <= '9') ||
           path[i] == '+' || path[i] == '-' || path[i] == '.')
        i++;
    return i >= 2 && path[i] == ':';
}

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* a host means a UNC share, percent escapes are UTF-8 and slashes become backslashes. */
static BOOL url_to_path(const WCHAR *url, WCHAR *path, int capacity)
{
    const WCHAR *p = url + 5;
    BOOL unc = FALSE;
    const WCHAR *host = nullptr;
    int host_length = 0;
    if (p[0] == '/' && p[1] == '/') {
        p += 2;
        host = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        host_length = (int)(p - host);
        if (host_length && !(host_length == 9 && !_wcsnicmp(host, L"localhost", 9)))
            unc = TRUE;
    }
    if (!unc && (*p == '/' || *p == '\\') && p[1] && p[2] && (p[2] == ':' || p[2] == '|'))
        p++;
    char bytes[1040];
    int count = 0;
    for (; *p && *p != '?' && *p != '#' && count < (int)sizeof(bytes) - 4; p++) {
        if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0) {
            bytes[count++] = (char)(hex_digit(p[1]) << 4 | hex_digit(p[2]));
            p += 2;
        } else if (*p < 0x80) {
            bytes[count++] = *p == '/' ? '\\' : (char)*p;
        } else {
            /* encode a raw non-ASCII character as UTF-8. */
            if (*p < 0x800) {
                bytes[count++] = (char)(0xC0 | *p >> 6);
                bytes[count++] = (char)(0x80 | (*p & 0x3F));
            } else {
                bytes[count++] = (char)(0xE0 | *p >> 12);
                bytes[count++] = (char)(0x80 | ((*p >> 6) & 0x3F));
                bytes[count++] = (char)(0x80 | (*p & 0x3F));
            }
        }
    }
    bytes[count] = 0;
    WCHAR wide[1040];
    int length = MultiByteToWideChar(CP_UTF8, 0, bytes, count + 1, wide, ARRAYSIZE(wide));
    if (!length)
        return FALSE;
    int out = 0;
    if (unc) {
        if (host_length + 2 >= capacity)
            return FALSE;
        path[out++] = '\\';
        path[out++] = '\\';
        for (int i = 0; i < host_length; i++)
            path[out++] = host[i];
    }
    for (int i = 0; wide[i] && out < capacity - 1; i++)
        path[out++] = (i == 1 && wide[i] == '|') ? ':' : wide[i];
    path[out] = 0;
    return TRUE;
}

typedef HRESULT(WINAPI *URL_OPEN_BLOCKING_STREAM)(LPUNKNOWN, LPCWSTR, LPSTREAM *, DWORD, void *);

WINOLEAUTAPI HRESULT WINAPI OleLoadPicturePath(LPOLESTR path, LPUNKNOWN caller, DWORD reserved, OLE_COLOR color,
                                               REFIID riid, LPVOID *object)
{
    if (!path || !object)
        return E_INVALIDARG;
    *object = nullptr;
    IStream *stream = nullptr;
    HRESULT hr;
    WCHAR local[MAX_PATH];
    if (path_is_url(path) && _wcsnicmp(path, L"file:", 5)) {
        /* urlmon is loaded only when a url needs fetching. */
        HMODULE urlmon = LoadLibraryW(L"urlmon.dll");
        URL_OPEN_BLOCKING_STREAM open =
            urlmon ? (URL_OPEN_BLOCKING_STREAM)GetProcAddress(urlmon, "URLOpenBlockingStreamW") : nullptr;
        if (!open) {
            if (urlmon)
                FreeLibrary(urlmon);
            return E_FAIL;
        }
        hr = open(nullptr, path, &stream, 0, nullptr);
        if (FAILED(hr) || !stream)
            return FAILED(hr) ? hr : E_FAIL;
    } else {
        if (!_wcsnicmp(path, L"file:", 5)) {
            if (!url_to_path(path, local, ARRAYSIZE(local)))
                return INET_E_RESOURCE_NOT_FOUND;
        } else {
            lstrcpynW(local, path, ARRAYSIZE(local));
        }
        hr = file_stream(local, FALSE, &stream);
        if (FAILED(hr))
            return INET_E_RESOURCE_NOT_FOUND;
    }
    /* bit 1 of reserved means the original format is not kept. */
    hr = OleLoadPictureEx(stream, 0, (reserved & 2) != 0, riid, 0, 0, 0, object);
    stream->lpVtbl->Release(stream);
    if (FAILED(hr)) {
        *object = nullptr;
        return hr == E_NOINTERFACE ? hr : E_FAIL;
    }
    return hr;
}

/* the file name may be missing or a string, possibly by reference. */
static HRESULT file_name(const VARIANT *file, BSTR *name, BOOL *missing)
{
    *name = nullptr;
    *missing = FALSE;
    const VARIANT *v = file;
    if (v->vt == (VT_VARIANT | VT_BYREF) && v->pvarVal)
        v = v->pvarVal;
    if (v->vt == VT_ERROR && v->scode == DISP_E_PARAMNOTFOUND) {
        *missing = TRUE;
        return S_OK;
    }
    switch (v->vt) {
    case VT_EMPTY: return S_OK;
    case VT_BSTR: *name = v->bstrVal; return S_OK;
    case VT_BSTR | VT_BYREF: *name = v->pbstrVal ? *v->pbstrVal : nullptr; return S_OK;
    default: return DISP_E_TYPEMISMATCH;
    }
}

WINOLEAUTAPI HRESULT WINAPI OleLoadPictureFileEx(VARIANT file, DWORD cx_desired, DWORD cy_desired, DWORD flags,
                                                 LPDISPATCH *picture)
{
    BSTR name;
    BOOL missing;
    HRESULT hr = file_name(&file, &name, &missing);
    if (FAILED(hr))
        return hr;
    if (missing || !name || !*name) {
        /* no file name gives an empty picture. */
        PICTDESC desc = {sizeof(PICTDESC), PICTYPE_NONE};
        return OleCreatePictureIndirect(&desc, &IID_IDispatch, FALSE, (void **)picture);
    }
    IStream *stream;
    hr = file_stream(name, FALSE, &stream);
    if (FAILED(hr))
        return hr;
    hr = OleLoadPictureEx(stream, 0, TRUE, &IID_IDispatch, cx_desired, cy_desired, flags, (void **)picture);
    stream->lpVtbl->Release(stream);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI OleLoadPictureFile(VARIANT file, LPDISPATCH *picture)
{
    return OleLoadPictureFileEx(file, 0, 0, 0, picture);
}

WINOLEAUTAPI HRESULT WINAPI OleSavePictureFile(LPDISPATCH picture, BSTR file)
{
    IPicture *pic = nullptr;
    if (!picture || FAILED(picture->lpVtbl->QueryInterface(picture, &IID_IPicture, (void **)&pic)))
        return CTL_E_INVALIDPROPERTYVALUE;
    SHORT type = 0;
    HRESULT hr = pic->lpVtbl->get_Type(pic, &type);
    if (hr == S_OK) {
        if (type == PICTYPE_NONE) {
            hr = CTL_E_INVALIDPROPERTYVALUE;
        } else if (!file || !*file) {
            hr = CTL_E_FILENOTFOUND;
        } else {
            IStream *stream;
            hr = file_stream(file, TRUE, &stream);
            if (SUCCEEDED(hr)) {
                hr = pic->lpVtbl->SaveAsFile(pic, stream, TRUE, nullptr);
                stream->lpVtbl->Release(stream);
            }
        }
    }
    pic->lpVtbl->Release(pic);
    return hr;
}

/* ------------------------------------------------------------------ colors, cursors, property frames */

/*
 * an OLE_COLOR is a plain rgb value, a palette index, a palette relative color or a system color. With a palette, a
 * plain value becomes palette relative.
 */
WINOLEAUTAPI HRESULT WINAPI OleTranslateColor(OLE_COLOR color, HPALETTE palette, COLORREF *result)
{
    COLORREF value = color;
    switch (color & 0xFF000000) {
    case 0:
        if (palette)
            value |= 0x02000000;
        break;
    case 0x01000000: {
        PALETTEENTRY entry;
        if ((color & 0xFFFF0000) != 0x01000000)
            return E_INVALIDARG;
        if (palette && !GetPaletteEntries(palette, color & 0xFFFF, 1, &entry))
            return E_INVALIDARG;
        break;
    }
    case 0x02000000:
        break;
    case 0x80000000:
        if ((color & 0xFFFF) > 24)
            return E_INVALIDARG;
        if (result)
            value = GetSysColor((int)(color & 0xFFFF));
        break;
    default:
        return E_INVALIDARG;
    }
    if (result)
        *result = value;
    return S_OK;
}

WINOLEAUTAPI HCURSOR WINAPI OleIconToCursor(HINSTANCE instance, HICON icon)
{
    return (HCURSOR)CopyImage(icon, IMAGE_CURSOR, GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR), 0);
}

WINOLEAUTAPI HRESULT WINAPI OleCreatePropertyFrameIndirect(LPOCPFIPARAMS params)
{
    if (!params)
        return E_POINTER;
    if ((params->cObjects && !params->lplpUnk) || (params->cPages && !params->lpPages))
        return E_INVALIDARG;
    /* there is no property sheet to show. */
    return E_NOTIMPL;
}

WINOLEAUTAPI HRESULT WINAPI OleCreatePropertyFrame(HWND owner, UINT x, UINT y, LPCOLESTR caption, ULONG count,
                                                   LPUNKNOWN *objects, ULONG pages, LPCLSID page_ids, LCID lcid,
                                                   DWORD reserved1, LPVOID reserved2)
{
    OCPFIPARAMS params = {sizeof(params), owner, (int)x, (int)y, caption, count, objects, pages, page_ids, lcid, -1};
    return OleCreatePropertyFrameIndirect(&params);
}
