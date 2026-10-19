/*
 * ole initialization, the ole clipboard and storage mediums.
 *
 * OleSetClipboard renders every HGLOBAL format of the data object onto the
 * user32 clipboard at once instead of delaying it, and keeps the object so
 * OleIsCurrentClipboard and OleGetClipboard can answer from it while it is
 * current. OleGetClipboard otherwise wraps the user32 clipboard.
 */
#include "ole32_private.h"

typedef DWORD(WINAPI *GET_SEQUENCE)(void);
typedef UINT(WINAPI *ENUM_FORMATS)(UINT format);
typedef BOOL(WINAPI *DELETE_GDI_HANDLE)(HANDLE handle);

static SRWLOCK g_clip_lock = SRWLOCK_INIT;
/* the data object of the last OleSetClipboard and the clipboard sequence number after it. */
static IDataObject *g_clip_object;
static DWORD g_clip_sequence;
static DWORD g_clip_thread;

static FARPROC user32_proc(const char *name)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? GetProcAddress(user32, name) : nullptr;
}

static DWORD clipboard_sequence(void)
{
    GET_SEQUENCE get = (GET_SEQUENCE)(void *)user32_proc("GetClipboardSequenceNumber");
    return get ? get() : 0;
}

static BOOL ole_initialized(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    return t && t->OleInits;
}

/* ------------------------------------------------------------------ OleInitialize */

WINOLEAPI HRESULT WINAPI OleInitialize(void *reserved)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return hr;
    OLE_THREAD *t = OleThreadState(FALSE);
    if (t->OleInits++)
        return S_FALSE;
    return hr;
}

WINOLEAPI void WINAPI OleUninitialize(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->OleInits)
        return;
    if (--t->OleInits == 0)
        OleClipboardUninitialize();
    CoUninitialize();
}

/* ------------------------------------------------------------------ storage mediums */

WINOLEAPI void WINAPI ReleaseStgMedium(STGMEDIUM *medium)
{
    if (!medium)
        return;
    IUnknown *owner = medium->pUnkForRelease;
    switch (medium->tymed) {
    case TYMED_HGLOBAL:
        if (!owner && medium->hGlobal)
            GlobalFree(medium->hGlobal);
        break;
    case TYMED_FILE:
        if (medium->lpszFileName) {
            if (!owner)
                DeleteFileW(medium->lpszFileName);
            CoTaskMemFree(medium->lpszFileName);
        }
        break;
    case TYMED_ISTREAM:
    case TYMED_ISTORAGE:
        if (medium->pstm)
            ((IUnknown *)medium->pstm)->lpVtbl->Release((IUnknown *)medium->pstm);
        break;
    case TYMED_GDI:
        if (!owner && medium->hBitmap)
            DeleteObject(medium->hBitmap);
        break;
    case TYMED_MFPICT:
        if (!owner && medium->hMetaFilePict) {
            METAFILEPICT *pict = GlobalLock(medium->hMetaFilePict);
            HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
            DELETE_GDI_HANDLE del = gdi32 ? (DELETE_GDI_HANDLE)(void *)GetProcAddress(gdi32, "DeleteMetaFile") : nullptr;
            if (pict && del)
                del(pict->hMF);
            if (pict)
                GlobalUnlock(medium->hMetaFilePict);
            GlobalFree(medium->hMetaFilePict);
        }
        break;
    case TYMED_ENHMF:
        if (!owner && medium->hEnhMetaFile) {
            HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
            DELETE_GDI_HANDLE del =
                gdi32 ? (DELETE_GDI_HANDLE)(void *)GetProcAddress(gdi32, "DeleteEnhMetaFile") : nullptr;
            if (del)
                del(medium->hEnhMetaFile);
        }
        break;
    }
    if (owner)
        owner->lpVtbl->Release(owner);
    memset(medium, 0, sizeof(*medium));
}

static HGLOBAL copy_global(HGLOBAL source)
{
    SIZE_T size = GlobalSize(source);
    void *from = GlobalLock(source);
    if (!from)
        return nullptr;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    void *to = copy ? GlobalLock(copy) : nullptr;
    if (to) {
        memcpy(to, from, size);
        GlobalUnlock(copy);
    } else if (copy) {
        GlobalFree(copy);
        copy = nullptr;
    }
    GlobalUnlock(source);
    return copy;
}

/* ------------------------------------------------------------------ a FORMATETC enumerator over an array */

typedef struct _FORMAT_ENUM {
    IEnumFORMATETC Interface;
    LONG Refs;
    ULONG Index;
    ULONG Count;
    FORMATETC Formats[];
} FORMAT_ENUM;

static const IEnumFORMATETCVtbl g_format_enum_vtbl;

static FORMAT_ENUM *format_enum_create(const FORMATETC *formats, ULONG count)
{
    FORMAT_ENUM *e = OleAllocZero(sizeof(*e) + count * sizeof(FORMATETC));
    if (!e)
        return nullptr;
    e->Interface.lpVtbl = &g_format_enum_vtbl;
    e->Refs = 1;
    e->Count = count;
    memcpy(e->Formats, formats, count * sizeof(FORMATETC));
    return e;
}

IEnumFORMATETC *OleFormatEnumCreate(const FORMATETC *formats, ULONG count)
{
    FORMAT_ENUM *e = format_enum_create(formats, count);
    return e ? &e->Interface : nullptr;
}

static HRESULT STDMETHODCALLTYPE format_enum_query_interface(IEnumFORMATETC *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumFORMATETC)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE format_enum_add_ref(IEnumFORMATETC *This)
{
    return (ULONG)InterlockedIncrement(&((FORMAT_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE format_enum_release(IEnumFORMATETC *This)
{
    LONG refs = InterlockedDecrement(&((FORMAT_ENUM *)This)->Refs);
    if (!refs)
        OleFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE format_enum_next(IEnumFORMATETC *This, ULONG count, FORMATETC *items, ULONG *fetched)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    if (!items || (count > 1 && !fetched))
        return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && e->Index < e->Count)
        items[n++] = e->Formats[e->Index++];
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE format_enum_skip(IEnumFORMATETC *This, ULONG count)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    ULONG left = e->Count - e->Index;
    e->Index += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE format_enum_reset(IEnumFORMATETC *This)
{
    ((FORMAT_ENUM *)This)->Index = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_enum_clone(IEnumFORMATETC *This, IEnumFORMATETC **clone)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    if (!clone)
        return E_INVALIDARG;
    FORMAT_ENUM *copy = format_enum_create(e->Formats, e->Count);
    *clone = copy ? &copy->Interface : nullptr;
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Index = e->Index;
    return S_OK;
}

static const IEnumFORMATETCVtbl g_format_enum_vtbl = {
    format_enum_query_interface, format_enum_add_ref, format_enum_release, format_enum_next,
    format_enum_skip,            format_enum_reset,   format_enum_clone,
};

/* ------------------------------------------------------------------ the clipboard data object */

typedef struct _CLIP_OBJECT {
    IDataObject Interface;
    LONG Refs;
} CLIP_OBJECT;

/* the data object set by this process while it still owns the clipboard, referenced. */
static IDataObject *current_object(void)
{
    AcquireSRWLockExclusive(&g_clip_lock);
    IDataObject *object = g_clip_object;
    if (object && clipboard_sequence() != g_clip_sequence)
        object = nullptr;
    if (object)
        object->lpVtbl->AddRef(object);
    ReleaseSRWLockExclusive(&g_clip_lock);
    return object;
}

static HRESULT check_format(const FORMATETC *format)
{
    if (!format)
        return E_INVALIDARG;
    if (format->lindex != -1)
        return DV_E_LINDEX;
    if (format->dwAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    if (!(format->tymed & TYMED_HGLOBAL))
        return DV_E_TYMED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clip_query_interface(IDataObject *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE clip_add_ref(IDataObject *This)
{
    return (ULONG)InterlockedIncrement(&((CLIP_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE clip_release(IDataObject *This)
{
    LONG refs = InterlockedDecrement(&((CLIP_OBJECT *)This)->Refs);
    if (!refs)
        OleFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE clip_get_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    if (!format || !medium)
        return E_INVALIDARG;
    memset(medium, 0, sizeof(*medium));
    IDataObject *source = current_object();
    if (source) {
        HRESULT hr = source->lpVtbl->GetData(source, format, medium);
        source->lpVtbl->Release(source);
        return hr;
    }
    HRESULT hr = check_format(format);
    if (FAILED(hr))
        return hr;
    if (!OpenClipboard(nullptr))
        return CLIPBRD_E_CANT_OPEN;
    HANDLE data = GetClipboardData(format->cfFormat);
    HGLOBAL copy = data ? copy_global(data) : nullptr;
    CloseClipboard();
    if (!copy)
        return data ? E_OUTOFMEMORY : DV_E_FORMATETC;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clip_get_data_here(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE clip_query_get_data(IDataObject *This, FORMATETC *format)
{
    IDataObject *source = current_object();
    if (source) {
        HRESULT hr = source->lpVtbl->QueryGetData(source, format);
        source->lpVtbl->Release(source);
        return hr;
    }
    HRESULT hr = check_format(format);
    if (FAILED(hr))
        return hr;
    return IsClipboardFormatAvailable(format->cfFormat) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE clip_get_canonical_format_etc(IDataObject *This, FORMATETC *in, FORMATETC *out)
{
    if (!in || !out)
        return E_INVALIDARG;
    *out = *in;
    out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT STDMETHODCALLTYPE clip_set_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium,
                                               BOOL release)
{
    return E_NOTIMPL;
}

/* the formats on the user32 clipboard, listed or probed. */
static HRESULT STDMETHODCALLTYPE clip_enum_format_etc(IDataObject *This, DWORD direction,
                                                      IEnumFORMATETC **enumerator)
{
    static const CLIPFORMAT probed[] = {CF_UNICODETEXT, CF_TEXT,         CF_OEMTEXT, CF_LOCALE,
                                        CF_DIBV5,       CF_DIB,          CF_HDROP,   CF_TIFF,
                                        CF_RIFF,        CF_WAVE,         CF_SYLK,    CF_DIF};
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    IDataObject *source = current_object();
    if (source) {
        HRESULT hr = source->lpVtbl->EnumFormatEtc(source, direction, enumerator);
        source->lpVtbl->Release(source);
        return hr;
    }
    FORMATETC formats[64];
    ULONG count = 0;
    ENUM_FORMATS next = (ENUM_FORMATS)(void *)user32_proc("EnumClipboardFormats");
    if (next && OpenClipboard(nullptr)) {
        for (UINT cf = next(0); cf && count < RTL_NUMBER_OF(formats); cf = next(cf))
            formats[count++] = (FORMATETC){(CLIPFORMAT)cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        CloseClipboard();
    } else {
        for (ULONG i = 0; i < RTL_NUMBER_OF(probed); i++)
            if (IsClipboardFormatAvailable(probed[i]))
                formats[count++] = (FORMATETC){probed[i], nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    }
    FORMAT_ENUM *e = format_enum_create(formats, count);
    if (!e)
        return E_OUTOFMEMORY;
    *enumerator = &e->Interface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE clip_d_advise(IDataObject *This, FORMATETC *format, DWORD flags,
                                               IAdviseSink *sink, DWORD *connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE clip_d_unadvise(IDataObject *This, DWORD connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE clip_enum_d_advise(IDataObject *This, IEnumSTATDATA **enumerator)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl g_clip_vtbl = {
    clip_query_interface,          clip_add_ref,  clip_release,         clip_get_data,
    clip_get_data_here,            clip_query_get_data,                  clip_get_canonical_format_etc,
    clip_set_data,                 clip_enum_format_etc, clip_d_advise, clip_d_unadvise,
    clip_enum_d_advise,
};

/* ------------------------------------------------------------------ the ole clipboard functions */

/* puts every HGLOBAL format of the object on the open clipboard. */
static void render_formats(IDataObject *object)
{
    IEnumFORMATETC *e;
    if (FAILED(object->lpVtbl->EnumFormatEtc(object, DATADIR_GET, &e)) || !e)
        return;
    FORMATETC format;
    ULONG fetched;
    while (e->lpVtbl->Next(e, 1, &format, &fetched) == S_OK && fetched == 1) {
        if (!(format.tymed & TYMED_HGLOBAL)) {
            CoTaskMemFree(format.ptd);
            continue;
        }
        format.tymed = TYMED_HGLOBAL;
        STGMEDIUM medium = {};
        if (SUCCEEDED(object->lpVtbl->GetData(object, &format, &medium)) && medium.tymed == TYMED_HGLOBAL) {
            HGLOBAL copy = copy_global(medium.hGlobal);
            if (copy && !SetClipboardData(format.cfFormat, copy))
                GlobalFree(copy);
            ReleaseStgMedium(&medium);
        } else if (medium.tymed != TYMED_NULL) {
            ReleaseStgMedium(&medium);
        }
        CoTaskMemFree(format.ptd);
    }
    e->lpVtbl->Release(e);
}

WINOLEAPI HRESULT WINAPI OleSetClipboard(IDataObject *data)
{
    if (!ole_initialized())
        return CO_E_NOTINITIALIZED;
    if (!OpenClipboard(nullptr))
        return CLIPBRD_E_CANT_OPEN;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return CLIPBRD_E_CANT_EMPTY;
    }
    if (data)
        render_formats(data);
    BOOL closed = CloseClipboard();
    AcquireSRWLockExclusive(&g_clip_lock);
    IDataObject *previous = g_clip_object;
    g_clip_object = data;
    if (data)
        data->lpVtbl->AddRef(data);
    g_clip_sequence = clipboard_sequence();
    g_clip_thread = GetCurrentThreadId();
    ReleaseSRWLockExclusive(&g_clip_lock);
    if (previous)
        previous->lpVtbl->Release(previous);
    return closed ? S_OK : CLIPBRD_E_CANT_CLOSE;
}

WINOLEAPI HRESULT WINAPI OleGetClipboard(IDataObject **data)
{
    if (!data)
        return E_INVALIDARG;
    *data = nullptr;
    if (!ole_initialized())
        return CO_E_NOTINITIALIZED;
    CLIP_OBJECT *object = OleAllocZero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Interface.lpVtbl = &g_clip_vtbl;
    object->Refs = 1;
    *data = &object->Interface;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI OleIsCurrentClipboard(IDataObject *data)
{
    if (!data)
        return S_FALSE;
    IDataObject *current = current_object();
    if (!current)
        return S_FALSE;
    current->lpVtbl->Release(current);
    return current == data ? S_OK : S_FALSE;
}

static void release_object(BOOL any_thread)
{
    AcquireSRWLockExclusive(&g_clip_lock);
    IDataObject *previous = nullptr;
    if (any_thread || g_clip_thread == GetCurrentThreadId()) {
        previous = g_clip_object;
        g_clip_object = nullptr;
    }
    ReleaseSRWLockExclusive(&g_clip_lock);
    if (previous)
        previous->lpVtbl->Release(previous);
}

/* the data is already on the clipboard, so flushing only lets go of the object. */
WINOLEAPI HRESULT WINAPI OleFlushClipboard(void)
{
    if (!ole_initialized())
        return CO_E_NOTINITIALIZED;
    release_object(TRUE);
    return S_OK;
}

void OleClipboardUninitialize(void)
{
    release_object(FALSE);
}
