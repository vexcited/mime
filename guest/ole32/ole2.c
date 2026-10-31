/*
 * the ole layer over COM: running objects, menu descriptors, accelerators, HKCR\CLSID class information and
 * persistence helpers. creating and loading embedded objects is not implemented.
 */
#include "ole32_private.h"
#include <mime/stub.h>

/* ------------------------------------------------------------------ running objects */

static IRunnableObject *runnable(IUnknown *object)
{
    IRunnableObject *result = nullptr;
    if (object && FAILED(object->lpVtbl->QueryInterface(object, &IID_IRunnableObject, (void **)&result)))
        result = nullptr;
    return result;
}

WINOLEAPI BOOL WINAPI OleIsRunning(IOleObject *object)
{
    if (!object)
        return FALSE;
    IRunnableObject *r = runnable((IUnknown *)object);
    if (!r)
        return TRUE;
    BOOL running = r->lpVtbl->IsRunning(r);
    r->lpVtbl->Release(r);
    return running;
}

WINOLEAPI HRESULT WINAPI OleRun(IUnknown *object)
{
    IRunnableObject *r = runnable(object);
    if (!r)
        return S_OK;
    HRESULT hr = r->lpVtbl->Run(r, nullptr);
    r->lpVtbl->Release(r);
    return hr;
}

WINOLEAPI HRESULT WINAPI OleLockRunning(IUnknown *object, BOOL lock, BOOL last_unlock_closes)
{
    IRunnableObject *r = runnable(object);
    if (!r)
        return S_OK;
    HRESULT hr = r->lpVtbl->LockRunning(r, lock, last_unlock_closes);
    r->lpVtbl->Release(r);
    return hr;
}

WINOLEAPI HRESULT WINAPI OleSetContainedObject(IUnknown *object, BOOL contained)
{
    IRunnableObject *r = runnable(object);
    if (!r)
        return S_OK;
    HRESULT hr = r->lpVtbl->SetContainedObject(r, contained);
    r->lpVtbl->Release(r);
    return hr;
}

/* ------------------------------------------------------------------ menu descriptors */

typedef struct {
    HMENU Menu;
    OLEMENUGROUPWIDTHS Widths;
} MENU_DESCRIPTOR;

static constexpr WCHAR g_menu_property[] = L"PROP_OLEMenuDescriptor";

WINOLEAPI HOLEMENU WINAPI OleCreateMenuDescriptor(HMENU shared, LPOLEMENUGROUPWIDTHS widths)
{
    if (!shared || !widths)
        return nullptr;
    HOLEMENU descriptor = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(MENU_DESCRIPTOR));
    MENU_DESCRIPTOR *d = descriptor ? GlobalLock(descriptor) : nullptr;
    if (!d) {
        if (descriptor)
            GlobalFree(descriptor);
        return nullptr;
    }
    d->Menu = shared;
    d->Widths = *widths;
    GlobalUnlock(descriptor);
    return descriptor;
}

WINOLEAPI HRESULT WINAPI OleDestroyMenuDescriptor(HOLEMENU descriptor)
{
    if (descriptor)
        GlobalFree(descriptor);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI OleSetMenuDescriptor(HOLEMENU descriptor, HWND frame, HWND active_object,
                                              IOleInPlaceFrame *frame_object, IOleInPlaceActiveObject *active)
{
    if (!frame || (descriptor && !active_object))
        return E_INVALIDARG;
    /* the descriptor is kept on the frame; menu commands are not routed to the object window. */
    if (descriptor)
        SetPropW(frame, g_menu_property, descriptor);
    else
        RemovePropW(frame, g_menu_property);
    return S_OK;
}

/* ------------------------------------------------------------------ accelerators */

/* the ACCEL entry of user32 accelerator tables. */
typedef struct {
    BYTE fVirt;
    WORD key;
    WORD cmd;
} ACCEL_ENTRY;

#define ACCEL_VIRTKEY 0x01
#define ACCEL_SHIFT 0x04
#define ACCEL_CONTROL 0x08
#define ACCEL_ALT 0x10

typedef int(WINAPI *COPY_ACCELERATORS)(HACCEL accel, ACCEL_ENTRY *entries, int count);

WINOLEAPI BOOL WINAPI IsAccelerator(HACCEL accel, int count, MSG *msg, WORD *command)
{
    if (!msg || !accel || count <= 0)
        return FALSE;
    UINT message = msg->message;
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN && message != WM_CHAR && message != WM_SYSCHAR)
        return FALSE;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    COPY_ACCELERATORS copy = user32 ? (COPY_ACCELERATORS)(void *)GetProcAddress(user32, "CopyAcceleratorTableW")
                                    : nullptr;
    if (!copy)
        return FALSE;
    ACCEL_ENTRY *table = OleAlloc(count * sizeof(*table));
    if (!table)
        return FALSE;
    int entries = copy(accel, table, count);
    BOOL found = FALSE;
    for (int i = 0; i < entries && !found; i++) {
        if (table[i].key != (WORD)msg->wParam)
            continue;
        BYTE flags = table[i].fVirt;
        if (flags & ACCEL_VIRTKEY) {
            if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
                continue;
            BYTE state = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000)
                state |= ACCEL_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000)
                state |= ACCEL_CONTROL;
            if ((msg->lParam & 0x20000000) || (GetKeyState(VK_MENU) & 0x8000))
                state |= ACCEL_ALT;
            found = state == (flags & (ACCEL_SHIFT | ACCEL_CONTROL | ACCEL_ALT));
        } else if (message == WM_CHAR) {
            found = !(flags & ACCEL_ALT);
        } else if (message == WM_SYSCHAR) {
            found = (flags & ACCEL_ALT) != 0;
        }
        if (found && command)
            *command = table[i].cmd;
    }
    OleFree(table);
    return found;
}

WINOLEAPI HRESULT WINAPI OleTranslateAccelerator(IOleInPlaceFrame *frame, OLEINPLACEFRAMEINFO *info, MSG *msg)
{
    if (!info || !msg)
        return E_INVALIDARG;
    WORD command;
    if (!IsAccelerator(info->haccel, (int)info->cAccelEntries, msg, &command))
        return S_FALSE;
    if (!frame)
        return E_INVALIDARG;
    return frame->lpVtbl->TranslateAccelerator(frame, msg, command);
}

/* ------------------------------------------------------------------ class information */

/* reads a string value into task memory. */
static LPOLESTR read_string(HKEY key, LPCWSTR subkey)
{
    HKEY sub = key;
    if (subkey && RegOpenKeyExW(key, subkey, 0, KEY_READ, &sub))
        return nullptr;
    DWORD type, bytes = 0;
    LPOLESTR value = nullptr;
    if (!RegQueryValueExW(sub, nullptr, nullptr, &type, nullptr, &bytes) && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        value = CoTaskMemAlloc(bytes + sizeof(WCHAR));
        if (value && !RegQueryValueExW(sub, nullptr, nullptr, &type, (BYTE *)value, &bytes))
            value[bytes / sizeof(WCHAR)] = 0;
        else if (value) {
            CoTaskMemFree(value);
            value = nullptr;
        }
    }
    if (sub != key)
        RegCloseKey(sub);
    return value;
}

/* parses an optionally signed decimal number, stopping at the first other character. */
static LONG parse_number(const WCHAR **s)
{
    const WCHAR *p = *s;
    while (*p == L' ')
        p++;
    BOOL negative = *p == L'-';
    if (negative)
        p++;
    ULONG n = 0;
    while (*p >= L'0' && *p <= L'9')
        n = n * 10 + (ULONG)(*p++ - L'0');
    *s = p;
    return (LONG)(negative ? 0u - n : n);
}

/* reads a decimal string or REG_DWORD default value. */
static BOOL read_number(HKEY key, LPCWSTR subkey, DWORD *out)
{
    HKEY sub = key;
    if (subkey && RegOpenKeyExW(key, subkey, 0, KEY_READ, &sub))
        return FALSE;
    WCHAR text[32];
    DWORD type, bytes = sizeof(text) - sizeof(WCHAR);
    BOOL ok = !RegQueryValueExW(sub, nullptr, nullptr, &type, (BYTE *)text, &bytes);
    if (ok && type == REG_DWORD && bytes == sizeof(DWORD)) {
        memcpy(out, text, sizeof(DWORD));
    } else if (ok && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        text[bytes / sizeof(WCHAR)] = 0;
        const WCHAR *p = text;
        *out = (DWORD)parse_number(&p);
    } else {
        ok = FALSE;
    }
    if (sub != key)
        RegCloseKey(sub);
    return ok;
}

WINOLEAPI HRESULT WINAPI OleRegGetUserType(REFCLSID clsid, DWORD form, LPOLESTR *user_type)
{
    if (!user_type)
        return E_INVALIDARG;
    *user_type = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(clsid, nullptr, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    if (form != USERCLASSTYPE_FULL) {
        WCHAR subkey[32];
        _snwprintf(subkey, RTL_NUMBER_OF(subkey), L"AuxUserType\\%u", (unsigned)form);
        *user_type = read_string(key, subkey);
    }
    if (!*user_type)
        *user_type = read_string(key, nullptr);
    RegCloseKey(key);
    return *user_type ? S_OK : REGDB_E_READREGDB;
}

/* the classes of ole 1 programs have ids of the form 0003xxxx or 0004xxxx and an Ole1Class key. */
WINOLEAPI BOOL WINAPI CoIsOle1Class(REFCLSID clsid)
{
    if (!clsid || (WORD)(clsid->Data1 >> 16) - 3u > 1u)
        return FALSE;
    HKEY class_key, ole1_key;
    if (OleOpenClassKey(clsid, nullptr, KEY_READ, &class_key))
        return FALSE;
    LSTATUS result = RegOpenKeyExW(class_key, L"Ole1Class", 0, KEY_READ, &ole1_key);
    RegCloseKey(class_key);
    if (result)
        return FALSE;
    WCHAR text[39];
    DWORD size = sizeof(text), type;
    result = RegGetValueW(ole1_key, nullptr, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_NONE | RRF_NOEXPAND | RRF_ZEROONFAILURE, &type,
                          text, &size);
    RegCloseKey(ole1_key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

WINOLEAPI HRESULT WINAPI OleRegGetMiscStatus(REFCLSID clsid, DWORD aspect, DWORD *status)
{
    if (!status)
        return E_INVALIDARG;
    *status = 0;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(clsid, nullptr, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    HKEY misc;
    LSTATUS result = RegOpenKeyExW(key, L"MiscStatus", 0, KEY_READ, &misc);
    RegCloseKey(key);
    if (result)
        return REGDB_E_READREGDB;
    read_number(misc, nullptr, status);
    WCHAR subkey[16];
    _snwprintf(subkey, RTL_NUMBER_OF(subkey), L"%u", (unsigned)aspect);
    read_number(misc, subkey, status);
    RegCloseKey(misc);
    return S_OK;
}

typedef struct {
    IEnumOLEVERB Interface;
    LONG Refs;
    OLEVERB *Verbs;
    ULONG Count;
    ULONG Position;
} VERB_ENUM;

static const IEnumOLEVERBVtbl verb_enum_vtable;

static void free_verbs(OLEVERB *verbs, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        CoTaskMemFree(verbs[i].lpszVerbName);
    OleFree(verbs);
}

static HRESULT copy_verbs(const OLEVERB *source, ULONG count, OLEVERB **result)
{
    OLEVERB *verbs = OleAllocZero((count ? count : 1) * sizeof(*verbs));
    if (!verbs)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        verbs[i] = source[i];
        verbs[i].lpszVerbName = nullptr;
        if (source[i].lpszVerbName) {
            SIZE_T bytes = (wcslen(source[i].lpszVerbName) + 1) * sizeof(WCHAR);
            verbs[i].lpszVerbName = CoTaskMemAlloc(bytes);
            if (!verbs[i].lpszVerbName) {
                free_verbs(verbs, i);
                return E_OUTOFMEMORY;
            }
            memcpy(verbs[i].lpszVerbName, source[i].lpszVerbName, bytes);
        }
    }
    *result = verbs;
    return S_OK;
}

/* takes ownership of verbs. */
static HRESULT create_verb_enum(OLEVERB *verbs, ULONG count, ULONG position, IEnumOLEVERB **result)
{
    VERB_ENUM *e = OleAllocZero(sizeof(*e));
    if (!e) {
        free_verbs(verbs, count);
        return E_OUTOFMEMORY;
    }
    e->Interface.lpVtbl = &verb_enum_vtable;
    e->Refs = 1;
    e->Verbs = verbs;
    e->Count = count;
    e->Position = position;
    *result = &e->Interface;
    return S_OK;
}

static HRESULT WINAPI verb_enum_query(IEnumOLEVERB *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IEnumOLEVERB))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI verb_enum_addref(IEnumOLEVERB *self)
{
    return InterlockedIncrement(&((VERB_ENUM *)self)->Refs);
}

static ULONG WINAPI verb_enum_release(IEnumOLEVERB *self)
{
    VERB_ENUM *e = (VERB_ENUM *)self;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        free_verbs(e->Verbs, e->Count);
        OleFree(e);
    }
    return refs;
}

static HRESULT WINAPI verb_enum_next(IEnumOLEVERB *self, ULONG count, OLEVERB *items, ULONG *fetched)
{
    VERB_ENUM *e = (VERB_ENUM *)self;
    if (!items || (count > 1 && !fetched))
        return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && e->Position < e->Count) {
        OLEVERB *copy;
        HRESULT hr = copy_verbs(&e->Verbs[e->Position], 1, &copy);
        if (FAILED(hr)) {
            if (fetched)
                *fetched = n;
            return hr;
        }
        items[n++] = copy[0];
        OleFree(copy);
        e->Position++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT WINAPI verb_enum_skip(IEnumOLEVERB *self, ULONG count)
{
    VERB_ENUM *e = (VERB_ENUM *)self;
    ULONG left = e->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT WINAPI verb_enum_reset(IEnumOLEVERB *self)
{
    ((VERB_ENUM *)self)->Position = 0;
    return S_OK;
}

static HRESULT WINAPI verb_enum_clone(IEnumOLEVERB *self, IEnumOLEVERB **clone)
{
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    VERB_ENUM *e = (VERB_ENUM *)self;
    OLEVERB *verbs;
    HRESULT hr = copy_verbs(e->Verbs, e->Count, &verbs);
    return FAILED(hr) ? hr : create_verb_enum(verbs, e->Count, e->Position, clone);
}

static const IEnumOLEVERBVtbl verb_enum_vtable = {
    verb_enum_query, verb_enum_addref, verb_enum_release, verb_enum_next,
    verb_enum_skip,  verb_enum_reset,  verb_enum_clone,
};

WINOLEAPI HRESULT WINAPI OleRegEnumVerbs(REFCLSID clsid, IEnumOLEVERB **enumerator)
{
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key, verb_key;
    if (OleOpenClassKey(clsid, nullptr, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    LSTATUS result = RegOpenKeyExW(key, L"Verb", 0, KEY_READ, &verb_key);
    RegCloseKey(key);
    if (result)
        return OLEOBJ_E_NOVERBS;
    OLEVERB *verbs = nullptr;
    ULONG count = 0, capacity = 0;
    HRESULT hr = S_OK;
    for (DWORD index = 0;; index++) {
        WCHAR name[64];
        DWORD chars = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(verb_key, index, name, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        LPOLESTR value = read_string(verb_key, name);
        if (!value)
            continue;
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            OLEVERB *grown = verbs ? HeapReAlloc(GetProcessHeap(), 0, verbs, capacity * sizeof(*grown))
                                   : OleAlloc(capacity * sizeof(*grown));
            if (!grown) {
                CoTaskMemFree(value);
                hr = E_OUTOFMEMORY;
                break;
            }
            verbs = grown;
        }
        /* the value reads "name,menu flags,verb attributes". */
        OLEVERB *verb = &verbs[count++];
        const WCHAR *p = name;
        verb->lVerb = parse_number(&p);
        verb->fuFlags = verb->grfAttribs = 0;
        WCHAR *comma = wcschr(value, L',');
        if (comma) {
            *comma = 0;
            p = comma + 1;
            verb->fuFlags = (DWORD)parse_number(&p);
            if (*p == L',') {
                p++;
                verb->grfAttribs = (DWORD)parse_number(&p);
            }
        }
        verb->lpszVerbName = value;
    }
    RegCloseKey(verb_key);
    if (FAILED(hr) || !count) {
        free_verbs(verbs, count);
        return FAILED(hr) ? hr : OLEOBJ_E_NOVERBS;
    }
    return create_verb_enum(verbs, count, 0, enumerator);
}

/* ------------------------------------------------------------------ persistence */

WINOLEAPI HRESULT WINAPI OleSave(IPersistStorage *object, IStorage *storage, BOOL same_as_load)
{
    if (!object || !storage)
        return E_INVALIDARG;
    CLSID clsid;
    if (SUCCEEDED(object->lpVtbl->GetClassID(object, &clsid)))
        WriteClassStg(storage, &clsid);
    HRESULT hr = object->lpVtbl->Save(object, storage, same_as_load);
    if (SUCCEEDED(hr))
        storage->lpVtbl->Commit(storage, STGC_DEFAULT);
    return hr;
}

WINOLEAPI HRESULT WINAPI OleSaveToStream(IPersistStream *object, IStream *stream)
{
    if (!stream)
        return E_INVALIDARG;
    if (!object)
        return WriteClassStm(stream, &CLSID_NULL);
    CLSID clsid;
    HRESULT hr = object->lpVtbl->GetClassID(object, &clsid);
    if (SUCCEEDED(hr))
        hr = WriteClassStm(stream, &clsid);
    if (SUCCEEDED(hr))
        hr = object->lpVtbl->Save(object, stream, TRUE);
    return hr;
}

/* the object behind the class the stream starts with, loaded from the rest of it. */
WINOLEAPI HRESULT WINAPI OleLoadFromStream(IStream *stream, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!stream)
        return E_INVALIDARG;
    CLSID clsid;
    HRESULT hr = ReadClassStm(stream, &clsid);
    if (hr != S_OK)
        return hr;
    IUnknown *instance;
    hr = CoCreateInstance(&clsid, nullptr, CLSCTX_SERVER | CLSCTX_NO_CODE_DOWNLOAD, riid, (void **)&instance);
    if (FAILED(hr))
        return hr;
    IPersistStream *persist;
    hr = instance->lpVtbl->QueryInterface(instance, &IID_IPersistStream, (void **)&persist);
    if (hr == S_OK) {
        hr = persist->lpVtbl->Load(persist, stream);
        persist->lpVtbl->Release(persist);
        if (hr == S_OK)
            hr = instance->lpVtbl->QueryInterface(instance, riid, object);
    }
    instance->lpVtbl->Release(instance);
    return hr;
}

WINOLEAPI HRESULT WINAPI OleDraw(IUnknown *object, DWORD aspect, HDC hdc, LPCRECT rect)
{
    if (!object)
        return E_INVALIDARG;
    IViewObject *view;
    if (object->lpVtbl->QueryInterface(object, &IID_IViewObject, (void **)&view) != S_OK)
        return DV_E_NOIVIEWOBJECT;
    HRESULT hr = view->lpVtbl->Draw(view, aspect, -1, nullptr, nullptr, nullptr, hdc, (const RECTL *)rect, nullptr,
                                    nullptr, 0);
    view->lpVtbl->Release(view);
    return hr;
}

static FARPROC gdi_proc(const char *name)
{
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    return gdi32 ? GetProcAddress(gdi32, name) : nullptr;
}

/* copies a bitmap through memory dcs in the screen's format. */
static HANDLE duplicate_bitmap(HANDLE source)
{
    typedef int(WINAPI * GET_OBJECT)(HANDLE, int, void *);
    typedef HDC(WINAPI * CREATE_DC)(HDC);
    typedef HBITMAP(WINAPI * CREATE_BITMAP)(HDC, int, int);
    typedef HGDIOBJ(WINAPI * SELECT)(HDC, HGDIOBJ);
    typedef BOOL(WINAPI * BLT)(HDC, int, int, int, int, HDC, int, int, DWORD);
    typedef BOOL(WINAPI * DELETE_DC)(HDC);
    typedef BOOL(WINAPI * DELETE_OBJECT)(HGDIOBJ);
    GET_OBJECT get = (GET_OBJECT)(void *)gdi_proc("GetObjectW");
    CREATE_DC create_dc = (CREATE_DC)(void *)gdi_proc("CreateCompatibleDC");
    CREATE_BITMAP create_bitmap = (CREATE_BITMAP)(void *)gdi_proc("CreateCompatibleBitmap");
    SELECT select = (SELECT)(void *)gdi_proc("SelectObject");
    BLT blt = (BLT)(void *)gdi_proc("BitBlt");
    DELETE_DC delete_dc = (DELETE_DC)(void *)gdi_proc("DeleteDC");
    DELETE_OBJECT delete_object = (DELETE_OBJECT)(void *)gdi_proc("DeleteObject");
    BITMAP info;
    if (!get || !create_dc || !create_bitmap || !select || !blt || !delete_dc || !delete_object ||
        !get(source, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return nullptr;
    HDC from = create_dc(nullptr), to = create_dc(nullptr);
    HBITMAP copy = nullptr;
    if (from && to) {
        HDC screen = GetDC(nullptr);
        copy = create_bitmap(screen, info.bmWidth, info.bmHeight);
        ReleaseDC(nullptr, screen);
        if (copy) {
            HGDIOBJ old_from = select(from, source), old_to = select(to, copy);
            if (!blt(to, 0, 0, info.bmWidth, info.bmHeight, from, 0, 0, SRCCOPY)) {
                select(to, old_to);
                delete_object(copy);
                copy = nullptr;
            } else {
                select(to, old_to);
            }
            select(from, old_from);
        }
    }
    if (from)
        delete_dc(from);
    if (to)
        delete_dc(to);
    return copy;
}

static HANDLE duplicate_palette(HANDLE source)
{
    typedef UINT(WINAPI * GET_ENTRIES)(HANDLE, UINT, UINT, PALETTEENTRY *);
    typedef HPALETTE(WINAPI * CREATE)(const LOGPALETTE *);
    GET_ENTRIES get = (GET_ENTRIES)(void *)gdi_proc("GetPaletteEntries");
    CREATE create = (CREATE)(void *)gdi_proc("CreatePalette");
    UINT count = get && create ? get(source, 0, 0, nullptr) : 0;
    if (!count)
        return nullptr;
    LOGPALETTE *logical = HeapAlloc(GetProcessHeap(), 0, sizeof(LOGPALETTE) + count * sizeof(PALETTEENTRY));
    HANDLE copy = nullptr;
    if (logical) {
        logical->palVersion = 0x300;
        logical->palNumEntries = (WORD)count;
        if (get(source, 0, count, logical->palPalEntry))
            copy = create(logical);
        HeapFree(GetProcessHeap(), 0, logical);
    }
    return copy;
}

WINOLEAPI HANDLE WINAPI OleDuplicateData(HANDLE source, CLIPFORMAT format, UINT flags)
{
    if (!source)
        return nullptr;
    /* gdi objects and metafiles have their own copy functions, everything else is global memory. */
    if (format == CF_BITMAP)
        return duplicate_bitmap(source);
    if (format == CF_PALETTE)
        return duplicate_palette(source);
    if (format == CF_ENHMETAFILE) {
        typedef HANDLE(WINAPI * COPY)(HANDLE, LPCWSTR);
        COPY copy = (COPY)(void *)gdi_proc("CopyEnhMetaFileW");
        return copy ? copy(source, nullptr) : nullptr;
    }
    SIZE_T size = GlobalSize(source);
    const void *data = GlobalLock(source);
    if (!data)
        return nullptr;
    HGLOBAL copy = GlobalAlloc(flags ? flags : GMEM_MOVEABLE, size);
    void *dest = copy ? GlobalLock(copy) : nullptr;
    if (dest) {
        memcpy(dest, data, size);
        GlobalUnlock(copy);
        if (format == CF_METAFILEPICT && size >= sizeof(METAFILEPICT)) {
            /* the picture holds its own metafile. */
            typedef HANDLE(WINAPI * COPY)(HANDLE, LPCWSTR);
            COPY copy_meta = (COPY)(void *)gdi_proc("CopyMetaFileW");
            METAFILEPICT *pict = GlobalLock(copy);
            if (pict && copy_meta)
                pict->hMF = copy_meta(pict->hMF, nullptr);
            if (pict)
                GlobalUnlock(copy);
        }
    } else if (copy) {
        GlobalFree(copy);
        copy = nullptr;
    }
    GlobalUnlock(source);
    return copy;
}

/* ------------------------------------------------------------------ embedding, not implemented */

static HRESULT no_object(void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI OleCreate(REFCLSID clsid, REFIID riid, DWORD render, FORMATETC *format,
                                   IOleClientSite *site, IStorage *storage, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI CreateDataCache(IUnknown *outer, REFCLSID clsid, REFIID riid, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleCreateFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                           IOleClientSite *site, IStorage *storage, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleCreateLinkFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                               IOleClientSite *site, IStorage *storage, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleCreateStaticFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                                 IOleClientSite *site, IStorage *storage, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleCreateLinkToFile(LPCOLESTR path, REFIID riid, DWORD render, FORMATETC *format,
                                             IOleClientSite *site, IStorage *storage, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleCreateFromFile(REFCLSID clsid, LPCOLESTR path, REFIID riid, DWORD render,
                                           FORMATETC *format, IOleClientSite *site, IStorage *storage,
                                           void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HRESULT WINAPI OleLoad(IStorage *storage, REFIID riid, IOleClientSite *site, void **object)
{
    MIME_STUB("ole32");
    return no_object(object);
}

WINOLEAPI HGLOBAL WINAPI OleGetIconOfClass(REFCLSID clsid, LPOLESTR label, BOOL use_type_as_label)
{
    MIME_STUB("ole32");
    return nullptr;
}

/*
 * an object of the first OLE is read from the stream the program supplies, in the format those objects were saved
 * in, and written as a storage. that format is not read here.
 */
WINOLEAPI HRESULT WINAPI OleConvertOLESTREAMToIStorage(void *stream, IStorage *storage, const void *device)
{
    if (!stream || !storage)
        return E_INVALIDARG;
    return E_NOTIMPL;
}
