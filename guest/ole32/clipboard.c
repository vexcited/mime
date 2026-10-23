/*
 * ole initialization, the ole clipboard and storage mediums.
 *
 * OleSetClipboard makes the thread's hidden clipboard window the user32 clipboard owner and puts the data object's
 * formats on it without data. the window renders a format with the object's GetData when WM_RENDERFORMAT asks, for
 * a program in this process or another, or for the host. OleFlushClipboard renders them all and lets go of the
 * object. OleGetClipboard returns a data object over the clipboard that talks to the owner's object directly while
 * the owner is a window of this process.
 */
#include "ole32_private.h"

typedef BOOL(WINAPI *DELETE_GDI_HANDLE)(HANDLE handle);

#define RPC_E_WRONG_THREAD_HR ((HRESULT)0x8001010EL)

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

/* ------------------------------------------------------------------ the formats the clipboard window keeps */

static UINT g_cf_data_object, g_cf_private_data, g_cf_persist_on_flush;

/* ole's registered formats: the owner's window, and the list of formats and how they can be read. */
static void init_formats(void)
{
    if (g_cf_private_data)
        return;
    g_cf_data_object = RegisterClipboardFormatW(L"DataObject");
    g_cf_persist_on_flush = RegisterClipboardFormatW(L"OleClipboardPersistOnFlush");
    g_cf_private_data = RegisterClipboardFormatW(L"OlePrivateData");
}

/*
 * an owner's format list, as "OlePrivateData" holds it on the clipboard and the window keeps a copy. 48 bytes per
 * format, target devices after the list.
 */
typedef struct _CLIP_ENTRY {
    WORD Format;
    WORD Pad0;
    DWORD Pad1;
    ULONGLONG PtdOffset; /* from the start of the list, 0 for none. */
    DWORD Aspect;
    LONG Index;
    DWORD Tymed;
    DWORD Pad2;
    DWORD SaveOnFlush;   /* 1 for a format ole put on the clipboard without data, which a flush renders. */
    DWORD Pad3[3];
} CLIP_ENTRY;

typedef struct _CLIP_ARRAY {
    DWORD Signature;
    DWORD Size;
    DWORD Refs;
    DWORD Count;
    DWORD Flags;
    DWORD Is64;
    CLIP_ENTRY Entries[];
} CLIP_ARRAY;

static_assert(sizeof(CLIP_ENTRY) == 48 && offsetof(CLIP_ARRAY, Entries) == 24, "the layout of OlePrivateData");

#define ARRAY_PERSIST 4
#define CF_GDIOBJ_FIRST 0x0300
#define CF_GDIOBJ_LAST 0x03FF

static FORMATETC entry_format(const CLIP_ARRAY *array, const CLIP_ENTRY *e)
{
    FORMATETC f = {e->Format, nullptr, e->Aspect, e->Index, e->Tymed};
    if (e->PtdOffset)
        f.ptd = (DVTARGETDEVICE *)((BYTE *)array + e->PtdOffset);
    return f;
}

static const WCHAR kClipboardClass[] = L"CLIPBRDWNDCLASS";
static const WCHAR kPropObject[] = L"ClipboardDataObjectInterface";

/* the owner window's format list, or NULL. */
static CLIP_ARRAY *window_array(HWND hwnd)
{
    return (CLIP_ARRAY *)GetWindowLongPtrW(hwnd, 0);
}

static const CLIP_ENTRY *find_entry(const CLIP_ARRAY *array, UINT format)
{
    for (DWORD i = 0; array && i < array->Count; i++)
        if (array->Entries[i].Format == format)
            return &array->Entries[i];
    return nullptr;
}

/* ------------------------------------------------------------------ rendering */

static BOOL global_from_stream_end(IStream *stream, HGLOBAL *global)
{
    return SUCCEEDED(GetHGlobalFromStream(stream, global)) && *global;
}

/* copies a stream into a new memory stream, which holds it as an HGLOBAL. */
static HRESULT stream_to_global(IStream *source, HGLOBAL *global)
{
    IStream *memory;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, FALSE, &memory);
    if (FAILED(hr))
        return hr;
    ULARGE_INTEGER size = {};
    LARGE_INTEGER zero = {};
    hr = source->lpVtbl->Seek(source, zero, STREAM_SEEK_END, &size);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->Seek(source, zero, STREAM_SEEK_SET, nullptr);
    ULARGE_INTEGER written = {};
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->CopyTo(source, memory, size, nullptr, &written);
    if (SUCCEEDED(hr) && written.QuadPart != size.QuadPart)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && !global_from_stream_end(memory, global))
        hr = E_FAIL;
    memory->lpVtbl->Release(memory);
    return hr;
}

/*
 * the stream form of a format as an HGLOBAL. the object fills a memory stream with GetDataHere, else gives a stream
 * of its own that is copied.
 */
static HRESULT data_from_stream(IDataObject *object, FORMATETC *format, HGLOBAL *global)
{
    FORMATETC wanted = *format;
    wanted.tymed = TYMED_ISTREAM;
    IStream *memory;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, FALSE, &memory);
    if (FAILED(hr))
        return hr;
    STGMEDIUM here = {};
    here.tymed = TYMED_ISTREAM;
    here.pstm = memory;
    hr = object->lpVtbl->GetDataHere(object, &wanted, &here);
    if (hr == S_OK) {
        hr = GetHGlobalFromStream(memory, global);
    } else if (hr != E_OUTOFMEMORY) {
        STGMEDIUM app = {};
        hr = object->lpVtbl->GetData(object, &wanted, &app);
        if (hr == S_OK) {
            if (app.tymed != TYMED_ISTREAM || !app.pstm) {
                hr = app.tymed != TYMED_ISTREAM ? DV_E_TYMED : E_FAIL;
            } else {
                HGLOBAL copy;
                hr = stream_to_global(app.pstm, &copy);
                if (SUCCEEDED(hr)) {
                    *global = copy;
                }
            }
            ReleaseStgMedium(&app);
        }
    }
    memory->lpVtbl->Release(memory);
    if (SUCCEEDED(hr) && !*global)
        hr = E_FAIL;
    return hr;
}

/*
 * the storage form as an HGLOBAL holding a compound file. the object fills a storage over memory with GetDataHere,
 * else gives a storage of its own that is copied.
 */
static HRESULT data_from_storage(IDataObject *object, FORMATETC *format, HGLOBAL *global)
{
    FORMATETC wanted = *format;
    wanted.tymed = TYMED_ISTORAGE;
    ILockBytes *bytes;
    HRESULT hr = CreateILockBytesOnHGlobal(nullptr, FALSE, &bytes);
    if (FAILED(hr))
        return hr;
    IStorage *storage;
    hr = StgCreateDocfileOnILockBytes(bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage);
    if (SUCCEEDED(hr)) {
        STGMEDIUM here = {};
        here.tymed = TYMED_ISTORAGE;
        here.pstg = storage;
        hr = object->lpVtbl->GetDataHere(object, &wanted, &here);
        if (hr != S_OK) {
            STGMEDIUM app = {};
            hr = object->lpVtbl->GetData(object, &wanted, &app);
            if (hr == S_OK) {
                if (app.tymed != TYMED_ISTORAGE || !app.pstg)
                    hr = app.tymed != TYMED_ISTORAGE ? DV_E_TYMED : E_FAIL;
                else
                    hr = app.pstg->lpVtbl->CopyTo(app.pstg, 0, nullptr, nullptr, storage);
                ReleaseStgMedium(&app);
            }
        }
        if (SUCCEEDED(hr))
            hr = storage->lpVtbl->Commit(storage, STGC_DEFAULT);
        storage->lpVtbl->Release(storage);
    }
    if (SUCCEEDED(hr))
        hr = GetHGlobalFromILockBytes(bytes, global);
    else {
        HGLOBAL partial = nullptr;
        if (SUCCEEDED(GetHGlobalFromILockBytes(bytes, &partial)) && partial)
            GlobalFree(partial);
    }
    bytes->lpVtbl->Release(bytes);
    return hr;
}

/*
 * the handle form of a format. what the object gives is taken over when it is the clipboard's to keep, and copied
 * when the medium belongs to the object.
 */
static HRESULT data_from_handle(IDataObject *object, FORMATETC *format, STGMEDIUM *medium)
{
    STGMEDIUM temp = {};
    HRESULT hr = object->lpVtbl->GetData(object, format, &temp);
    if (hr != S_OK)
        return hr;
    if (temp.tymed != TYMED_HGLOBAL && temp.tymed != TYMED_GDI && temp.tymed != TYMED_MFPICT &&
        temp.tymed != TYMED_ENHMF) {
        ReleaseStgMedium(&temp);
        return DV_E_TYMED;
    }
    if (temp.pUnkForRelease) {
        HANDLE copy = OleDuplicateData(temp.hBitmap, format->cfFormat, GMEM_MOVEABLE | GMEM_DDESHARE);
        ReleaseStgMedium(&temp);
        if (!copy)
            return E_OUTOFMEMORY;
        medium->hBitmap = (HBITMAP)copy;
        medium->tymed = temp.tymed;
        return S_OK;
    }
    medium->hBitmap = temp.hBitmap;
    medium->tymed = temp.tymed;
    return S_OK;
}

/* the clipboard handle for a format of the owner. */
static HRESULT render_current(HWND hwnd, UINT cf, HANDLE *handle, IDataObject *object)
{
    const CLIP_ARRAY *array = window_array(hwnd);
    const CLIP_ENTRY *entry = find_entry(array, cf);
    if (!entry)
        return E_FAIL;
    FORMATETC format = entry_format(array, entry);
    if (format.tymed & TYMED_ISTORAGE) {
        HGLOBAL global = nullptr;
        HRESULT hr = data_from_storage(object, &format, &global);
        *handle = global;
        return hr;
    }
    if (format.tymed & TYMED_ISTREAM) {
        HGLOBAL global = nullptr;
        HRESULT hr = data_from_stream(object, &format, &global);
        *handle = global;
        return hr;
    }
    format.tymed &= ~(DWORD)TYMED_FILE;
    if (cf > 0xC000)
        format.tymed &= TYMED_HGLOBAL;
    if (!format.tymed)
        return E_FAIL;
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    HRESULT hr = data_from_handle(object, &format, &medium);
    *handle = medium.hBitmap;
    if (hr == S_OK && !(format.tymed & medium.tymed)) {
        /* the object gave a medium that is not one asked for: it is not something the clipboard holds. */
        medium.pUnkForRelease = nullptr;
        ReleaseStgMedium(&medium);
        *handle = nullptr;
        return DV_E_TYMED;
    }
    return hr;
}

/* WM_RENDERFORMAT: asks the object for the format and puts the answer on the clipboard. */
static void render_format(HWND hwnd, UINT cf)
{
    IDataObject *object = (IDataObject *)GetPropW(hwnd, kPropObject);
    if (!object)
        return;
    object->lpVtbl->AddRef(object);
    HANDLE handle = nullptr;
    HRESULT hr = render_current(hwnd, cf, &handle, object);
    if (hr == S_OK && !SetClipboardData(cf, handle)) {
        /* the medium goes back to its owner. */
        STGMEDIUM medium = {};
        const CLIP_ENTRY *entry = find_entry(window_array(hwnd), cf);
        medium.tymed = entry && (entry->Tymed & (TYMED_GDI | TYMED_MFPICT | TYMED_ENHMF)) ? entry->Tymed & (TYMED_GDI | TYMED_MFPICT | TYMED_ENHMF) : TYMED_HGLOBAL;
        medium.hBitmap = (HBITMAP)handle;
        ReleaseStgMedium(&medium);
    }
    object->lpVtbl->Release(object);
}

/* ------------------------------------------------------------------ the owner window */

/*
 * lets go of the window's object. flag 1 also resets the "DataObject" format that points to it, 2 means the system
 * emptied the clipboard, 4 keeps the references because the window is going away: an object is not released from a
 * destroyed window, since its apartment may be gone, so one that was not flushed or replaced stays referenced.
 */
static void remove_object(HWND hwnd, DWORD flags)
{
    IDataObject *object = (IDataObject *)GetPropW(hwnd, kPropObject);
    CLIP_ARRAY *array = (CLIP_ARRAY *)SetWindowLongPtrW(hwnd, 0, 0);
    if (array)
        OleFree(array);
    RemovePropW(hwnd, kPropObject);
    if (!(flags & 4) && object)
        object->lpVtbl->Release(object);
    if ((flags & 1) && g_cf_data_object && IsClipboardFormatAvailable(g_cf_data_object)) {
        HGLOBAL empty = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(HWND));
        if (empty && !SetClipboardData(g_cf_data_object, empty))
            GlobalFree(empty);
    }
}

static LRESULT CALLBACK clip_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_RENDERFORMAT:
        render_format(hwnd, (UINT)wparam);
        return 0;
    case WM_RENDERALLFORMATS:
        /* ole renders what it renders when the clipboard is flushed, not when the owner is destroyed. */
        return 0;
    case WM_DESTROYCLIPBOARD:
        remove_object(hwnd, 2);
        return 0;
    case WM_DESTROY:
        remove_object(hwnd, 6);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* the thread's window that owns the clipboard when ole does, created on request. */
static HWND clip_window(BOOL create)
{
    OLE_THREAD *t = OleThreadState(create);
    if (!t)
        return nullptr;
    if (t->ClipWindow && !IsWindow(t->ClipWindow))
        t->ClipWindow = nullptr;
    if (t->ClipWindow || !create)
        return t->ClipWindow;
    HINSTANCE module = GetModuleHandleW(L"ole32.dll");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = clip_proc;
    wc.cbWndExtra = sizeof(void *);
    wc.hInstance = module;
    wc.lpszClassName = kClipboardClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    t->ClipWindow = CreateWindowExW(0, kClipboardClass, nullptr, WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    CW_USEDEFAULT, HWND_MESSAGE, nullptr, module, nullptr);
    return t->ClipWindow;
}

/* this thread's window when it owns the clipboard. */
static HWND owner_window(void)
{
    HWND hwnd = clip_window(FALSE);
    return hwnd && hwnd == GetClipboardOwner() ? hwnd : nullptr;
}

/* opens the clipboard for a window, retrying once when another thread has it. */
static HRESULT lock_clipboard(HWND hwnd)
{
    if (OpenClipboard(hwnd))
        return S_OK;
    Sleep(0);
    return OpenClipboard(hwnd) ? S_OK : CLIPBRD_E_CANT_OPEN;
}

/* the data object of the process's window that owns the clipboard, referenced; null for any other owner. */
static IDataObject *owner_object(void)
{
    HWND owner = GetClipboardOwner();
    if (!owner)
        return nullptr;
    WCHAR name[32];
    DWORD pid = 0;
    if (!GetClassNameW(owner, name, RTL_NUMBER_OF(name)) || _wcsicmp(name, kClipboardClass))
        return nullptr;
    GetWindowThreadProcessId(owner, &pid);
    if (pid != GetCurrentProcessId())
        return nullptr;
    IDataObject *object = (IDataObject *)GetPropW(owner, kPropObject);
    if (object)
        object->lpVtbl->AddRef(object);
    return object;
}

/* puts a global of the given bytes on the open clipboard as a format. */
static BOOL set_format_global(UINT format, const void *data, SIZE_T size)
{
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, size);
    void *to = global ? GlobalLock(global) : nullptr;
    if (!to) {
        if (global)
            GlobalFree(global);
        return FALSE;
    }
    if (data)
        memcpy(to, data, size);
    GlobalUnlock(global);
    if (SetClipboardData(format, global))
        return TRUE;
    GlobalFree(global);
    return FALSE;
}

/*
 * puts the object's formats on the open clipboard without data, for the owner to render, plus the "OlePrivateData"
 * list of how each can be read. formats in the gdi object range are left out, a format already on the clipboard is
 * not set again, and an index of 0 becomes -1.
 */
static HRESULT set_clipboard_formats(HWND hwnd, IDataObject *object)
{
    IEnumFORMATETC *enumerator = nullptr;
    HRESULT hr = object->lpVtbl->EnumFormatEtc(object, DATADIR_GET, &enumerator);
    if (FAILED(hr))
        return hr;
    if (!enumerator)
        return E_FAIL;
    enumerator->lpVtbl->Reset(enumerator);
    ULONG capacity = 16, count = 0;
    FORMATETC *items = OleAlloc(capacity * sizeof(FORMATETC));
    if (!items) {
        enumerator->lpVtbl->Release(enumerator);
        return E_OUTOFMEMORY;
    }
    SIZE_T ptd_bytes = 0;
    FORMATETC format;
    ULONG fetched;
    DWORD flags = 0;
    while (enumerator->lpVtbl->Next(enumerator, 1, &format, &fetched) == S_OK && fetched == 1) {
        if (format.cfFormat >= CF_GDIOBJ_FIRST && format.cfFormat <= CF_GDIOBJ_LAST) {
            CoTaskMemFree(format.ptd);
            continue;
        }
        if (count == capacity) {
            FORMATETC *bigger = OleAlloc(capacity * 2 * sizeof(FORMATETC));
            if (!bigger) {
                hr = E_OUTOFMEMORY;
                CoTaskMemFree(format.ptd);
                break;
            }
            memcpy(bigger, items, count * sizeof(FORMATETC));
            OleFree(items);
            items = bigger;
            capacity *= 2;
        }
        if (format.ptd)
            ptd_bytes += (format.ptd->tdSize + 7) & ~(SIZE_T)7;
        if (format.cfFormat == g_cf_persist_on_flush)
            flags |= ARRAY_PERSIST;
        items[count++] = format;
    }
    enumerator->lpVtbl->Release(enumerator);
    SIZE_T size = sizeof(CLIP_ARRAY) + count * sizeof(CLIP_ENTRY) + ptd_bytes;
    CLIP_ARRAY *array = SUCCEEDED(hr) ? OleAllocZero(size) : nullptr;
    if (!array) {
        for (ULONG i = 0; i < count; i++)
            CoTaskMemFree(items[i].ptd);
        OleFree(items);
        return SUCCEEDED(hr) ? E_OUTOFMEMORY : hr;
    }
    array->Size = (DWORD)size;
    array->Refs = 1;
    array->Count = count;
    array->Flags = flags;
    array->Is64 = 1;
    SIZE_T at = sizeof(CLIP_ARRAY) + count * sizeof(CLIP_ENTRY);
    for (ULONG i = 0; i < count; i++) {
        CLIP_ENTRY *e = &array->Entries[i];
        e->Format = items[i].cfFormat;
        e->Aspect = items[i].dwAspect;
        e->Index = items[i].lindex ? items[i].lindex : -1;
        e->Tymed = items[i].tymed;
        if (items[i].ptd) {
            DWORD bytes = items[i].ptd->tdSize;
            memcpy((BYTE *)array + at, items[i].ptd, bytes);
            e->PtdOffset = at;
            at += (bytes + 7) & ~(SIZE_T)7;
            CoTaskMemFree(items[i].ptd);
        }
        if (!IsClipboardFormatAvailable(e->Format)) {
            e->SaveOnFlush = 1;
            SetClipboardData(e->Format, nullptr);
        }
    }
    OleFree(items);
    if (!set_format_global(g_cf_private_data, array, size)) {
        OleFree(array);
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    }
    SetWindowLongPtrW(hwnd, 0, (LONG_PTR)array);
    return S_OK;
}

/* ------------------------------------------------------------------ the data object over the clipboard */

typedef struct _CLIP_OBJECT {
    IDataObject Interface;
    LONG Refs;
    DWORD Thread;
    IDataObject *Local; /* the owner's object while it is a window of this process, referenced. */
    CLIP_ARRAY *Array;  /* the formats, made when first needed. */
} CLIP_OBJECT;

static const IDataObjectVtbl g_clip_vtbl;

typedef enum { MATCH_NOT_FOUND, MATCH_BAD, MATCH_GOOD } MATCH;

/* the formats of the clipboard with how they can be read: the owner's list, else made from the formats there are. */
static CLIP_ARRAY *clip_array(CLIP_OBJECT *c)
{
    if (c->Array)
        return c->Array;
    if (lock_clipboard(nullptr) != S_OK)
        return nullptr;
    CLIP_ARRAY *array = nullptr;
    if (g_cf_private_data && IsClipboardFormatAvailable(g_cf_private_data)) {
        HANDLE data = GetClipboardData(g_cf_private_data);
        const CLIP_ARRAY *source = data ? GlobalLock(data) : nullptr;
        if (source && GlobalSize(data) >= sizeof(CLIP_ARRAY) && source->Size <= GlobalSize(data) &&
            source->Count <= (source->Size - sizeof(CLIP_ARRAY)) / sizeof(CLIP_ENTRY)) {
            array = OleAlloc(source->Size);
            if (array)
                memcpy(array, source, source->Size);
        }
        if (source)
            GlobalUnlock(data);
    }
    if (!array) {
        int count = CountClipboardFormats();
        if (count < 0)
            count = 0;
        SIZE_T size = sizeof(CLIP_ARRAY) + (SIZE_T)count * sizeof(CLIP_ENTRY);
        array = OleAllocZero(size);
        if (array) {
            array->Size = (DWORD)size;
            array->Refs = 1;
            array->Is64 = 1;
            DWORD n = 0;
            for (UINT cf = EnumClipboardFormats(0); cf && n < (DWORD)count; cf = EnumClipboardFormats(cf)) {
                if (cf == g_cf_data_object || cf == g_cf_private_data)
                    continue;
                CLIP_ENTRY *e = &array->Entries[n++];
                e->Format = (WORD)cf;
                e->Aspect = DVASPECT_CONTENT;
                e->Index = -1;
                /* a bitmap and a palette are gdi objects, a metafile picture and an enhanced metafile have their
                   own mediums, the rest is memory or a stream. */
                e->Tymed = cf == CF_BITMAP || cf == CF_PALETTE ? TYMED_GDI
                         : cf == CF_METAFILEPICT              ? TYMED_MFPICT
                         : cf == CF_ENHMETAFILE               ? TYMED_ENHMF
                                                              : TYMED_HGLOBAL | TYMED_ISTREAM;
            }
            array->Count = n;
        }
    }
    CloseClipboard();
    c->Array = array;
    return array;
}

static MATCH match_format(CLIP_OBJECT *c, const FORMATETC *want, DWORD *tymed, BOOL native_only)
{
    MATCH match = MATCH_NOT_FOUND;
    CLIP_ARRAY *array = clip_array(c);
    LONG aspect = want->dwAspect ? (LONG)want->dwAspect : 1;
    if (tymed)
        *tymed = TYMED_NULL;
    for (DWORD i = 0; array && i < array->Count; i++) {
        const CLIP_ENTRY *e = &array->Entries[i];
        if (e->Format != want->cfFormat)
            continue;
        FORMATETC have = entry_format(array, e);
        if (((DWORD)aspect & e->Aspect) == (DWORD)aspect && e->Index == want->lindex &&
            (have.ptd == want->ptd || (have.ptd && want->ptd && have.ptd->tdSize == want->ptd->tdSize &&
                                      !memcmp(have.ptd, want->ptd, have.ptd->tdSize)))) {
            if (tymed)
                *tymed = e->Tymed;
            return MATCH_GOOD;
        }
        match = MATCH_BAD;
        if (native_only)
            break;
    }
    if (match == MATCH_NOT_FOUND && aspect != 1)
        match = MATCH_BAD;
    return match;
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
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    LONG refs = InterlockedDecrement(&c->Refs);
    if (!refs) {
        if (c->Local)
            c->Local->lpVtbl->Release(c->Local);
        OleFree(c->Array);
        OleFree(c);
    }
    return (ULONG)refs;
}

/* the handle of a format on the clipboard, copied for the caller. the clipboard is opened for it. */
static HRESULT clipboard_handle(UINT cf, HANDLE *copy)
{
    HRESULT hr = lock_clipboard(nullptr);
    if (hr != S_OK)
        return hr;
    HANDLE data = GetClipboardData(cf);
    if (!data)
        hr = DV_E_FORMATETC;
    else {
        *copy = OleDuplicateData(data, (CLIPFORMAT)cf, 0);
        if (!*copy)
            hr = E_OUTOFMEMORY;
    }
    if (!CloseClipboard() && hr == S_OK) {
        STGMEDIUM undo = {};
        undo.tymed = cf == CF_BITMAP || cf == CF_PALETTE ? TYMED_GDI : TYMED_HGLOBAL;
        undo.hBitmap = (HBITMAP)*copy;
        ReleaseStgMedium(&undo);
        hr = CLIPBRD_E_CANT_CLOSE;
    }
    return hr;
}

/* writes a clipboard format's bytes into a stream or memory the caller gave. */
static HRESULT clipboard_data_here(UINT cf, STGMEDIUM *medium)
{
    HANDLE copy = nullptr;
    HRESULT hr = clipboard_handle(cf, &copy);
    if (hr != S_OK)
        return hr;
    SIZE_T size = GlobalSize(copy);
    const void *bytes = GlobalLock(copy);
    if (!bytes) {
        GlobalFree(copy);
        return E_OUTOFMEMORY;
    }
    switch (medium->tymed) {
    case TYMED_ISTREAM: {
        ULONG written = 0;
        hr = medium->pstm->lpVtbl->Write(medium->pstm, bytes, (ULONG)size, &written);
        if (SUCCEEDED(hr) && written != size)
            hr = STG_E_MEDIUMFULL;
        if (SUCCEEDED(hr)) {
            LARGE_INTEGER zero = {};
            medium->pstm->lpVtbl->Seek(medium->pstm, zero, STREAM_SEEK_SET, nullptr);
        }
        break;
    }
    case TYMED_HGLOBAL: {
        SIZE_T room = GlobalSize(medium->hGlobal);
        void *to = room >= size ? GlobalLock(medium->hGlobal) : nullptr;
        if (!to) {
            hr = room >= size ? E_OUTOFMEMORY : STG_E_MEDIUMFULL;
        } else {
            memcpy(to, bytes, size);
            GlobalUnlock(medium->hGlobal);
        }
        break;
    }
    default:
        hr = DV_E_TYMED;
        break;
    }
    GlobalUnlock(copy);
    GlobalFree(copy);
    return hr;
}

static HRESULT STDMETHODCALLTYPE clip_get_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    if (c->Thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD_HR;
    if (!format || !medium)
        return E_INVALIDARG;
    memset(medium, 0, sizeof(*medium));
    if (c->Local && c->Local->lpVtbl->GetData(c->Local, format, medium) == S_OK)
        return S_OK;
    memset(medium, 0, sizeof(*medium));
    UINT cf = format->cfFormat;
    DWORD original = TYMED_NULL;
    if (cf >= CF_GDIOBJ_FIRST && cf <= CF_GDIOBJ_LAST)
        return DV_E_FORMATETC;
    if (match_format(c, format, &original, TRUE) == MATCH_BAD)
        return DV_E_FORMATETC;
    /* the medium: its own for a gdi object, else one of those asked for, memory first when the owner reads it so.
       the owner lists storage before memory before a stream. */
    DWORD tymed;
    if (cf == CF_BITMAP || cf == CF_PALETTE) {
        tymed = TYMED_GDI;
    } else if (cf == CF_METAFILEPICT) {
        tymed = TYMED_MFPICT;
    } else if (cf == CF_ENHMETAFILE) {
        tymed = TYMED_ENHMF;
    } else {
        DWORD both = original != TYMED_NULL ? format->tymed & original : 0;
        if (both & TYMED_ISTORAGE)
            tymed = TYMED_ISTORAGE;
        else if (both & TYMED_HGLOBAL)
            tymed = TYMED_HGLOBAL;
        else if (both & TYMED_ISTREAM)
            tymed = TYMED_ISTREAM;
        else if (format->tymed & TYMED_ISTORAGE)
            tymed = TYMED_ISTORAGE;
        else if (format->tymed & TYMED_HGLOBAL)
            tymed = TYMED_HGLOBAL;
        else if (format->tymed & TYMED_ISTREAM)
            tymed = TYMED_ISTREAM;
        else
            return DV_E_TYMED;
    }
    if ((cf == CF_BITMAP || cf == CF_PALETTE) ? !(format->tymed & TYMED_GDI)
        : cf == CF_METAFILEPICT              ? !(format->tymed & TYMED_MFPICT)
        : cf == CF_ENHMETAFILE               ? !(format->tymed & TYMED_ENHMF)
                                             : FALSE)
        return DV_E_TYMED;
    HRESULT hr;
    switch (tymed) {
    case TYMED_HGLOBAL:
    case TYMED_GDI:
    case TYMED_MFPICT:
    case TYMED_ENHMF: {
        HANDLE copy = nullptr;
        hr = clipboard_handle(cf, &copy);
        if (hr == S_OK) {
            medium->tymed = tymed;
            medium->hBitmap = (HBITMAP)copy;
        }
        return hr;
    }
    case TYMED_ISTREAM:
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &medium->pstm);
        if (hr == S_OK) {
            medium->tymed = TYMED_ISTREAM;
            hr = clipboard_data_here(cf, medium);
        }
        break;
    default: {
        /* a storage holds a compound file the owner wrote into memory. */
        HANDLE copy = nullptr;
        hr = clipboard_handle(cf, &copy);
        if (hr != S_OK)
            return hr;
        ILockBytes *bytes;
        hr = CreateILockBytesOnHGlobal(copy, TRUE, &bytes);
        if (hr == S_OK) {
            hr = StgOpenStorageOnILockBytes(bytes, nullptr, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, nullptr, 0,
                                            &medium->pstg);
            bytes->lpVtbl->Release(bytes);
            if (hr == S_OK)
                medium->tymed = TYMED_ISTORAGE;
        } else {
            GlobalFree(copy);
        }
        break;
    }
    }
    if (hr != S_OK)
        ReleaseStgMedium(medium);
    return hr;
}

static HRESULT STDMETHODCALLTYPE clip_get_data_here(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    if (c->Thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD_HR;
    if (!format || !medium)
        return E_INVALIDARG;
    if (c->Local && c->Local->lpVtbl->GetDataHere(c->Local, format, medium) == S_OK)
        return S_OK;
    if (format->cfFormat >= CF_GDIOBJ_FIRST && format->cfFormat <= CF_GDIOBJ_LAST)
        return DV_E_FORMATETC;
    if (match_format(c, format, nullptr, TRUE) == MATCH_BAD)
        return DV_E_FORMATETC;
    if (medium->tymed != format->tymed || !(medium->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)))
        return DV_E_TYMED;
    return clipboard_data_here(format->cfFormat, medium);
}

static HRESULT STDMETHODCALLTYPE clip_query_get_data(IDataObject *This, FORMATETC *format)
{
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    if (c->Thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD_HR;
    if (!format)
        return E_INVALIDARG;
    UINT cf = format->cfFormat;
    if (!IsClipboardFormatAvailable(cf))
        return (HRESULT)0x8004006AL; /* DV_E_CLIPFORMAT */
    MATCH match = match_format(c, format, nullptr, FALSE);
    if (match == MATCH_GOOD)
        return S_OK;
    if (match == MATCH_BAD)
        return DV_E_FORMATETC;
    /* a format the owner did not list is one the system makes from another: its medium is fixed. */
    if (cf == CF_BITMAP || cf == CF_PALETTE)
        return format->tymed == TYMED_GDI ? S_OK : DV_E_TYMED;
    if (cf == CF_METAFILEPICT)
        return format->tymed == TYMED_MFPICT ? S_OK : DV_E_TYMED;
    if (cf == CF_ENHMETAFILE)
        return format->tymed == TYMED_ENHMF ? S_OK : DV_E_TYMED;
    if (cf >= CF_GDIOBJ_FIRST && cf <= CF_GDIOBJ_LAST)
        return DV_E_FORMATETC;
    return format->tymed & ~(DWORD)(TYMED_GDI | TYMED_MFPICT) ? S_OK : DV_E_TYMED;
}

static HRESULT STDMETHODCALLTYPE clip_get_canonical_format_etc(IDataObject *This, FORMATETC *in, FORMATETC *out)
{
    if (!in || !out)
        return E_INVALIDARG;
    *out = *in;
    out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

/* whether a format says that a cut was pasted: the target reports it to the source, which then deletes. */
static BOOL says_moved(const FORMATETC *format, const STGMEDIUM *medium)
{
    static const WCHAR *names[] = {L"Performed DropEffect", L"Logical Performed DropEffect", L"Paste Succeeded"};
    if (format->cfFormat < 0xC000 || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal)
        return FALSE;
    WCHAR name[40];
    if (!GetClipboardFormatNameW(format->cfFormat, name, RTL_NUMBER_OF(name)))
        return FALSE;
    BOOL named = FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++)
        named = named || !_wcsicmp(name, names[i]);
    const DWORD *effect = named && GlobalSize(medium->hGlobal) >= sizeof(DWORD) ? GlobalLock(medium->hGlobal) : nullptr;
    BOOL moved = effect && (*effect & DROPEFFECT_MOVE);
    if (effect)
        GlobalUnlock(medium->hGlobal);
    return moved;
}

/*
 * SetData forwards what a program says about the clipboard, such as that a cut was pasted, to the owner's object, and
 * fails without one. a cut made by a host program has no owner here, and a file manager clears the clipboard once it
 * pasted one, so the same report from a guest program empties it and the host's selection goes with it. windows
 * fails the call instead.
 */
static HRESULT STDMETHODCALLTYPE clip_set_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    if (c->Thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD_HR;
    if (!format || !medium)
        return E_INVALIDARG;
    if (c->Local)
        return c->Local->lpVtbl->SetData(c->Local, format, medium, release);
    if (!GetClipboardOwner() && says_moved(format, medium) && OpenClipboard(nullptr)) {
        EmptyClipboard();
        CloseClipboard();
        if (release)
            ReleaseStgMedium(medium);
        return S_OK;
    }
    return E_FAIL;
}

/* an enumerator over a copy of the list of formats, with the target devices copied for each format it gives. */
typedef struct _ARRAY_ENUM {
    IEnumFORMATETC Interface;
    LONG Refs;
    DWORD Index;
    CLIP_ARRAY *Array;
} ARRAY_ENUM;

static const IEnumFORMATETCVtbl g_array_enum_vtbl;

static ARRAY_ENUM *array_enum_create(const CLIP_ARRAY *array, DWORD index)
{
    ARRAY_ENUM *e = OleAllocZero(sizeof(*e));
    if (!e)
        return nullptr;
    e->Array = OleAlloc(array->Size);
    if (!e->Array) {
        OleFree(e);
        return nullptr;
    }
    memcpy(e->Array, array, array->Size);
    e->Interface.lpVtbl = &g_array_enum_vtbl;
    e->Refs = 1;
    e->Index = index;
    return e;
}

static HRESULT STDMETHODCALLTYPE array_enum_query_interface(IEnumFORMATETC *This, REFIID riid, void **object)
{
    return format_enum_query_interface(This, riid, object);
}

static ULONG STDMETHODCALLTYPE array_enum_add_ref(IEnumFORMATETC *This)
{
    return (ULONG)InterlockedIncrement(&((ARRAY_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE array_enum_release(IEnumFORMATETC *This)
{
    ARRAY_ENUM *e = (ARRAY_ENUM *)This;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        OleFree(e->Array);
        OleFree(e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE array_enum_next(IEnumFORMATETC *This, ULONG count, FORMATETC *items, ULONG *fetched)
{
    ARRAY_ENUM *e = (ARRAY_ENUM *)This;
    if (!items || (count > 1 && !fetched))
        return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && e->Index < e->Array->Count) {
        FORMATETC f = entry_format(e->Array, &e->Array->Entries[e->Index]);
        if (f.ptd) {
            DWORD bytes = f.ptd->tdSize;
            DVTARGETDEVICE *copy = CoTaskMemAlloc(bytes);
            if (!copy)
                break;
            memcpy(copy, f.ptd, bytes);
            f.ptd = copy;
        }
        items[n++] = f;
        e->Index++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE array_enum_skip(IEnumFORMATETC *This, ULONG count)
{
    ARRAY_ENUM *e = (ARRAY_ENUM *)This;
    ULONG left = e->Array->Count - e->Index;
    e->Index += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE array_enum_reset(IEnumFORMATETC *This)
{
    ((ARRAY_ENUM *)This)->Index = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE array_enum_clone(IEnumFORMATETC *This, IEnumFORMATETC **clone)
{
    ARRAY_ENUM *e = (ARRAY_ENUM *)This;
    if (!clone)
        return E_INVALIDARG;
    ARRAY_ENUM *copy = array_enum_create(e->Array, e->Index);
    *clone = copy ? &copy->Interface : nullptr;
    return copy ? S_OK : E_OUTOFMEMORY;
}

static const IEnumFORMATETCVtbl g_array_enum_vtbl = {
    array_enum_query_interface, array_enum_add_ref, array_enum_release, array_enum_next,
    array_enum_skip,            array_enum_reset,   array_enum_clone,
};

static HRESULT STDMETHODCALLTYPE clip_enum_format_etc(IDataObject *This, DWORD direction, IEnumFORMATETC **enumerator)
{
    CLIP_OBJECT *c = (CLIP_OBJECT *)This;
    if (c->Thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD_HR;
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    CLIP_ARRAY *array = clip_array(c);
    ARRAY_ENUM *e = array ? array_enum_create(array, 0) : nullptr;
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

WINOLEAPI HRESULT WINAPI OleSetClipboard(IDataObject *data)
{
    if (!ole_initialized())
        return CO_E_NOTINITIALIZED;
    init_formats();
    HWND hwnd = clip_window(TRUE);
    if (!hwnd)
        return E_FAIL;
    HRESULT hr = lock_clipboard(hwnd);
    if (hr != S_OK)
        return hr;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return CLIPBRD_E_CANT_EMPTY;
    }
    /* emptying the clipboard tells its owner, this window too, that its contents went. */
    if (data) {
        if (!SetPropW(hwnd, kPropObject, data)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        } else {
            data->lpVtbl->AddRef(data);
            HWND self = hwnd;
            if (!set_format_global(g_cf_data_object, &self, sizeof(self)))
                hr = E_OUTOFMEMORY;
            else
                hr = set_clipboard_formats(hwnd, data);
            if (FAILED(hr))
                remove_object(hwnd, 0);
        }
    }
    if (!CloseClipboard() && SUCCEEDED(hr))
        hr = CLIPBRD_E_CANT_CLOSE;
    return hr;
}

WINOLEAPI HRESULT WINAPI OleGetClipboard(IDataObject **data)
{
    if (!data)
        return E_INVALIDARG;
    *data = nullptr;
    init_formats();
    CLIP_OBJECT *object = OleAllocZero(sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Interface.lpVtbl = &g_clip_vtbl;
    object->Refs = 1;
    object->Thread = GetCurrentThreadId();
    object->Local = owner_object();
    *data = &object->Interface;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI OleIsCurrentClipboard(IDataObject *data)
{
    if (!data)
        return S_FALSE;
    HWND owner = owner_window();
    if (!owner)
        return S_FALSE;
    return (IDataObject *)GetPropW(owner, kPropObject) == data ? S_OK : S_FALSE;
}

/*
 * renders every format ole put on the clipboard without data, and lets go of the object. "OleClipboardPersistOnFlush",
 * which saves the object itself with the clipboard so another process can bring it back, is not supported: its formats
 * are rendered like the others.
 */
WINOLEAPI HRESULT WINAPI OleFlushClipboard(void)
{
    init_formats();
    HWND hwnd = owner_window();
    if (!hwnd)
        return E_FAIL;
    HRESULT hr = lock_clipboard(hwnd);
    if (hr != S_OK)
        return hr;
    CLIP_ARRAY *array = window_array(hwnd);
    if (array) {
        DWORD kept = 0;
        for (DWORD i = 0; i < array->Count; i++) {
            const CLIP_ENTRY *e = &array->Entries[i];
            if (e->SaveOnFlush != 1)
                continue;
            GetClipboardData(e->Format);
            array->Entries[kept++] = *e;
        }
        array->Count = kept;
        set_format_global(g_cf_private_data, array, array->Size);
    }
    remove_object(hwnd, 1);
    if (!CloseClipboard())
        hr = CLIPBRD_E_CANT_CLOSE;
    return hr;
}

void OleClipboardUninitialize(void)
{
    OLE_THREAD *t = OleThreadState(FALSE);
    if (!t || !t->ClipWindow)
        return;
    HWND hwnd = t->ClipWindow;
    t->ClipWindow = nullptr;
    if (IsWindow(hwnd))
        DestroyWindow(hwnd);
}
