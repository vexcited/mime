/*
 * the ole layer over com: running objects, menu descriptors, accelerators,
 * the class information of HKCR\CLSID and persistence helpers. creating and
 * loading embedded objects is not implemented.
 */
#include "ole32_private.h"
#include <mime/stub.h>

/* ------------------------------------------------------------------ running objects */

static IRunnableObject *runnable(IUnknown *object)
{
    IRunnableObject *result = NULL;
    if (object && FAILED(object->lpVtbl->QueryInterface(object, &IID_IRunnableObject, (void **)&result)))
        result = NULL;
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
    HRESULT hr = r->lpVtbl->Run(r, NULL);
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

static const WCHAR g_menu_property[] = L"PROP_OLEMenuDescriptor";

WINOLEAPI HOLEMENU WINAPI OleCreateMenuDescriptor(HMENU shared, LPOLEMENUGROUPWIDTHS widths)
{
    if (!shared || !widths)
        return NULL;
    HOLEMENU descriptor = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(MENU_DESCRIPTOR));
    MENU_DESCRIPTOR *d = descriptor ? GlobalLock(descriptor) : NULL;
    if (!d) {
        if (descriptor)
            GlobalFree(descriptor);
        return NULL;
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
                                    : NULL;
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
        return NULL;
    DWORD type, bytes = 0;
    LPOLESTR value = NULL;
    if (!RegQueryValueExW(sub, NULL, NULL, &type, NULL, &bytes) && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        value = CoTaskMemAlloc(bytes + sizeof(WCHAR));
        if (value && !RegQueryValueExW(sub, NULL, NULL, &type, (BYTE *)value, &bytes))
            value[bytes / sizeof(WCHAR)] = 0;
        else if (value) {
            CoTaskMemFree(value);
            value = NULL;
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

/* reads a REG_DWORD or decimal string default value. */
static BOOL read_number(HKEY key, LPCWSTR subkey, DWORD *out)
{
    HKEY sub = key;
    if (subkey && RegOpenKeyExW(key, subkey, 0, KEY_READ, &sub))
        return FALSE;
    WCHAR text[32];
    DWORD type, bytes = sizeof(text) - sizeof(WCHAR);
    BOOL ok = !RegQueryValueExW(sub, NULL, NULL, &type, (BYTE *)text, &bytes);
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
    *user_type = NULL;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(clsid, NULL, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    if (form != USERCLASSTYPE_FULL) {
        WCHAR subkey[32];
        _snwprintf(subkey, RTL_NUMBER_OF(subkey), L"AuxUserType\\%u", (unsigned)form);
        *user_type = read_string(key, subkey);
    }
    if (!*user_type)
        *user_type = read_string(key, NULL);
    RegCloseKey(key);
    return *user_type ? S_OK : REGDB_E_READREGDB;
}

WINOLEAPI HRESULT WINAPI OleRegGetMiscStatus(REFCLSID clsid, DWORD aspect, DWORD *status)
{
    if (!status)
        return E_INVALIDARG;
    *status = 0;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key;
    if (OleOpenClassKey(clsid, NULL, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    HKEY misc;
    LSTATUS result = RegOpenKeyExW(key, L"MiscStatus", 0, KEY_READ, &misc);
    RegCloseKey(key);
    if (result)
        return REGDB_E_READREGDB;
    read_number(misc, NULL, status);
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
        verbs[i].lpszVerbName = NULL;
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
    *result = NULL;
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
    *clone = NULL;
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
    *enumerator = NULL;
    if (!clsid)
        return E_INVALIDARG;
    HKEY key, verb_key;
    if (OleOpenClassKey(clsid, NULL, KEY_READ, &key))
        return REGDB_E_CLASSNOTREG;
    LSTATUS result = RegOpenKeyExW(key, L"Verb", 0, KEY_READ, &verb_key);
    RegCloseKey(key);
    if (result)
        return OLEOBJ_E_NOVERBS;
    OLEVERB *verbs = NULL;
    ULONG count = 0, capacity = 0;
    HRESULT hr = S_OK;
    for (DWORD index = 0;; index++) {
        WCHAR name[64];
        DWORD chars = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(verb_key, index, name, &chars, NULL, NULL, NULL, NULL))
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

WINOLEAPI HANDLE WINAPI OleDuplicateData(HANDLE source, CLIPFORMAT format, UINT flags)
{
    if (!source)
        return NULL;
    /* gdi handles and metafiles are not duplicated, only global memory formats. */
    if (format == CF_BITMAP || format == CF_PALETTE || format == CF_ENHMETAFILE || format == CF_METAFILEPICT)
        return NULL;
    SIZE_T size = GlobalSize(source);
    const void *data = GlobalLock(source);
    if (!data)
        return NULL;
    HGLOBAL copy = GlobalAlloc(flags ? flags : GMEM_MOVEABLE, size);
    void *dest = copy ? GlobalLock(copy) : NULL;
    if (dest) {
        memcpy(dest, data, size);
        GlobalUnlock(copy);
    } else if (copy) {
        GlobalFree(copy);
        copy = NULL;
    }
    GlobalUnlock(source);
    return copy;
}

/* ------------------------------------------------------------------ embedding, not implemented */

static HRESULT no_object(void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = NULL;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI OleCreate(REFCLSID clsid, REFIID riid, DWORD render, FORMATETC *format,
                                   IOleClientSite *site, IStorage *storage, void **object)
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

WINOLEAPI HRESULT WINAPI OleQueryLinkFromData(IDataObject *data)
{
    MIME_STUB("ole32");
    return S_FALSE;
}

WINOLEAPI HRESULT WINAPI OleQueryCreateFromData(IDataObject *data)
{
    MIME_STUB("ole32");
    return S_FALSE;
}

WINOLEAPI HGLOBAL WINAPI OleGetIconOfClass(REFCLSID clsid, LPOLESTR label, BOOL use_type_as_label)
{
    MIME_STUB("ole32");
    return NULL;
}
