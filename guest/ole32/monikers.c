/*
 * the item, file and generic composite monikers programs make with CreateItemMoniker, CreateFileMoniker and
 * CreateGenericComposite, with the display names they ask for. anti, pointer and class monikers are missing.
 */
#include "ole32_private.h"
#include <mime/stub.h>

#define MKSYS_GENERICCOMPOSITE 1
#define MKSYS_FILEMONIKER 2
#define MKSYS_ITEMMONIKER 4
#define MK_E_NEEDGENERIC_ ((HRESULT)0x800401E2)
#define MK_E_SYNTAX_ ((HRESULT)0x800401E4)
#define MK_E_NOTBINDABLE_ ((HRESULT)0x800401E8)
#define MK_E_NOINVERSE_ ((HRESULT)0x800401EC)
#define MK_E_NOPREFIX_ ((HRESULT)0x800401EE)
#define MK_S_REDUCED_TO_SELF_ ((HRESULT)0x000401E2)
#define MK_S_US_ ((HRESULT)0x000401E6)

MIME_DEFINE_GUID(CLSID_FileMoniker_, 0x00000303, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(CLSID_ItemMoniker_, 0x00000304, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(CLSID_CompositeMoniker_, 0x00000309, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

/* what an item moniker binds through when it has a moniker to its left. */
typedef struct IOleItemContainer IOleItemContainer;
typedef struct IOleItemContainerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleItemContainer *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IOleItemContainer *);
    ULONG(STDMETHODCALLTYPE *Release)(IOleItemContainer *);
    HRESULT(STDMETHODCALLTYPE *ParseDisplayName)(IOleItemContainer *, IBindCtx *, LPOLESTR, ULONG *, IMoniker **);
    HRESULT(STDMETHODCALLTYPE *EnumObjects)(IOleItemContainer *, DWORD, IEnumUnknown **);
    HRESULT(STDMETHODCALLTYPE *LockContainer)(IOleItemContainer *, BOOL);
    HRESULT(STDMETHODCALLTYPE *GetObject)(IOleItemContainer *, LPOLESTR, DWORD, IBindCtx *, REFIID, void **);
    HRESULT(STDMETHODCALLTYPE *GetObjectStorage)(IOleItemContainer *, LPOLESTR, IBindCtx *, REFIID, void **);
    HRESULT(STDMETHODCALLTYPE *IsRunning)(IOleItemContainer *, LPOLESTR);
} IOleItemContainerVtbl;
struct IOleItemContainer {
    const IOleItemContainerVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IOleItemContainer_, 0x0000011c, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

typedef enum { KIND_ITEM, KIND_FILE, KIND_COMPOSITE } MK_KIND;

typedef struct MK {
    IMoniker Interface;
    LONG Refs;
    MK_KIND Kind;
    WCHAR *Delimiter; /* item */
    WCHAR *Name;      /* item, file */
    IMoniker *Left, *Right;
} MK;

static const IMonikerVtbl mk_vtable;

static MK *mk_of(IMoniker *moniker)
{
    return moniker && moniker->lpVtbl == &mk_vtable ? (MK *)moniker : nullptr;
}

static WCHAR *duplicate(const WCHAR *text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = OleAlloc(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

static const GUID *mk_class(const MK *mk)
{
    return mk->Kind == KIND_ITEM ? &CLSID_ItemMoniker_ : mk->Kind == KIND_FILE ? &CLSID_FileMoniker_ :
                                                                                  &CLSID_CompositeMoniker_;
}

/* ------------------------------------------------------------------ IUnknown, IPersist */

static HRESULT STDMETHODCALLTYPE mk_query(IMoniker *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IPersist) && !IsEqualIID(iid, &IID_IPersistStream) &&
        !IsEqualIID(iid, &IID_IMoniker))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE mk_addref(IMoniker *self)
{
    return InterlockedIncrement(&((MK *)self)->Refs);
}

static ULONG STDMETHODCALLTYPE mk_release(IMoniker *self)
{
    MK *mk = (MK *)self;
    LONG refs = InterlockedDecrement(&mk->Refs);
    if (!refs) {
        OleFree(mk->Delimiter);
        OleFree(mk->Name);
        if (mk->Left)
            mk->Left->lpVtbl->Release(mk->Left);
        if (mk->Right)
            mk->Right->lpVtbl->Release(mk->Right);
        OleFree(mk);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE mk_get_class_id(IMoniker *self, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = *mk_class((MK *)self);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mk_is_dirty(IMoniker *self)
{
    return S_FALSE;
}

/* ------------------------------------------------------------------ creation and persistence */

static MK *mk_new(MK_KIND kind)
{
    MK *mk = OleAllocZero(sizeof(*mk));
    if (!mk)
        return nullptr;
    mk->Interface.lpVtbl = &mk_vtable;
    mk->Refs = 1;
    mk->Kind = kind;
    return mk;
}

WINOLEAPI HRESULT WINAPI CreateItemMoniker(LPCOLESTR delimiter, LPCOLESTR item, IMoniker **moniker)
{
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    if (!delimiter || !item)
        return E_INVALIDARG;
    MK *mk = mk_new(KIND_ITEM);
    if (!mk)
        return E_OUTOFMEMORY;
    mk->Delimiter = duplicate(delimiter);
    mk->Name = duplicate(item);
    if (!mk->Delimiter || !mk->Name) {
        mk_release(&mk->Interface);
        return E_OUTOFMEMORY;
    }
    *moniker = &mk->Interface;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CreateFileMoniker(LPCOLESTR path, IMoniker **moniker)
{
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    if (!path)
        return E_INVALIDARG;
    MK *mk = mk_new(KIND_FILE);
    if (!mk)
        return E_OUTOFMEMORY;
    mk->Name = duplicate(path);
    if (!mk->Name) {
        mk_release(&mk->Interface);
        return E_OUTOFMEMORY;
    }
    *moniker = &mk->Interface;
    return S_OK;
}

/* flattens composites into their components in order, taking references. */
static HRESULT flatten(IMoniker *moniker, IMoniker **list, ULONG *count, ULONG capacity)
{
    MK *mk = mk_of(moniker);
    if (mk && mk->Kind == KIND_COMPOSITE) {
        HRESULT hr = flatten(mk->Left, list, count, capacity);
        return SUCCEEDED(hr) ? flatten(mk->Right, list, count, capacity) : hr;
    }
    if (*count >= capacity)
        return E_FAIL;
    list[(*count)++] = moniker;
    moniker->lpVtbl->AddRef(moniker);
    return S_OK;
}

WINOLEAPI HRESULT WINAPI CreateGenericComposite(IMoniker *first, IMoniker *rest, IMoniker **composite)
{
    if (!composite)
        return E_POINTER;
    *composite = nullptr;
    if (!first || !rest) {
        *composite = first ? first : rest;
        if (*composite)
            (*composite)->lpVtbl->AddRef(*composite);
        return S_OK;
    }
    MK *mk = mk_new(KIND_COMPOSITE);
    if (!mk)
        return E_OUTOFMEMORY;
    mk->Left = first;
    mk->Right = rest;
    first->lpVtbl->AddRef(first);
    rest->lpVtbl->AddRef(rest);
    *composite = &mk->Interface;
    return S_OK;
}

/* the stream holds the length including the terminator, then the bytes in the ansi code page. */
static HRESULT write_text(IStream *stream, const WCHAR *text)
{
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    char *ansi = OleAlloc((SIZE_T)bytes);
    if (!ansi)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, bytes, nullptr, nullptr);
    ULONG length = (ULONG)bytes, written;
    HRESULT hr = stream->lpVtbl->Write(stream, &length, sizeof(length), &written);
    if (SUCCEEDED(hr))
        hr = stream->lpVtbl->Write(stream, ansi, length, &written);
    OleFree(ansi);
    return hr;
}

static HRESULT read_text(IStream *stream, WCHAR **text)
{
    ULONG length, read;
    *text = nullptr;
    HRESULT hr = stream->lpVtbl->Read(stream, &length, sizeof(length), &read);
    if (FAILED(hr) || read != sizeof(length) || !length || length > 32768)
        return FAILED(hr) ? hr : STG_E_READFAULT;
    char *ansi = OleAlloc(length);
    WCHAR *wide = OleAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!ansi || !wide) {
        OleFree(ansi);
        OleFree(wide);
        return E_OUTOFMEMORY;
    }
    hr = stream->lpVtbl->Read(stream, ansi, length, &read);
    if (FAILED(hr) || read != length) {
        OleFree(ansi);
        OleFree(wide);
        return FAILED(hr) ? hr : STG_E_READFAULT;
    }
    MultiByteToWideChar(CP_ACP, 0, ansi, (int)length, wide, (int)length);
    wide[length - 1] = 0;
    OleFree(ansi);
    *text = wide;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mk_load(IMoniker *self, IStream *stream)
{
    MK *mk = (MK *)self;
    if (!stream)
        return E_POINTER;
    if (mk->Kind == KIND_ITEM) {
        WCHAR *delimiter, *item;
        HRESULT hr = read_text(stream, &delimiter);
        if (FAILED(hr))
            return hr;
        hr = read_text(stream, &item);
        if (FAILED(hr)) {
            OleFree(delimiter);
            return hr;
        }
        OleFree(mk->Delimiter);
        OleFree(mk->Name);
        mk->Delimiter = delimiter;
        mk->Name = item;
        return S_OK;
    }
    if (mk->Kind == KIND_FILE) {
        WCHAR *path;
        HRESULT hr = read_text(stream, &path);
        if (FAILED(hr))
            return hr;
        OleFree(mk->Name);
        mk->Name = path;
        return S_OK;
    }
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mk_save(IMoniker *self, IStream *stream, BOOL clear_dirty)
{
    MK *mk = (MK *)self;
    if (!stream)
        return E_POINTER;
    if (mk->Kind == KIND_ITEM) {
        HRESULT hr = write_text(stream, mk->Delimiter);
        return SUCCEEDED(hr) ? write_text(stream, mk->Name) : hr;
    }
    if (mk->Kind == KIND_FILE)
        return write_text(stream, mk->Name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mk_get_size_max(IMoniker *self, ULARGE_INTEGER *size)
{
    MK *mk = (MK *)self;
    if (!size)
        return E_POINTER;
    size->QuadPart = 8 + (mk->Delimiter ? wcslen(mk->Delimiter) * 2 : 0) + (mk->Name ? wcslen(mk->Name) * 2 : 0) + 8;
    return S_OK;
}

/* ------------------------------------------------------------------ names */

static HRESULT STDMETHODCALLTYPE mk_get_display_name(IMoniker *self, IBindCtx *context, IMoniker *left, LPOLESTR *name)
{
    MK *mk = (MK *)self;
    if (!name)
        return E_POINTER;
    *name = nullptr;
    WCHAR *text;
    if (mk->Kind == KIND_COMPOSITE) {
        WCHAR *a = nullptr, *b = nullptr;
        HRESULT hr = mk->Left->lpVtbl->GetDisplayName(mk->Left, context, nullptr, &a);
        if (SUCCEEDED(hr))
            hr = mk->Right->lpVtbl->GetDisplayName(mk->Right, context, nullptr, &b);
        if (FAILED(hr)) {
            CoTaskMemFree(a);
            return hr;
        }
        text = CoTaskMemAlloc((wcslen(a) + wcslen(b) + 1) * sizeof(WCHAR));
        if (text) {
            wcscpy(text, a);
            wcscat(text, b);
        }
        CoTaskMemFree(a);
        CoTaskMemFree(b);
    } else {
        SIZE_T length = (mk->Kind == KIND_ITEM ? wcslen(mk->Delimiter) : 0) + wcslen(mk->Name) + 1;
        text = CoTaskMemAlloc(length * sizeof(WCHAR));
        if (text) {
            text[0] = 0;
            if (mk->Kind == KIND_ITEM)
                wcscpy(text, mk->Delimiter);
            wcscat(text, mk->Name);
        }
    }
    if (!text)
        return E_OUTOFMEMORY;
    *name = text;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mk_parse_display_name(IMoniker *self, IBindCtx *context, IMoniker *left, LPOLESTR name,
                                                       ULONG *eaten, IMoniker **out)
{
    if (out)
        *out = nullptr;
    return MK_E_SYNTAX_;
}

static HRESULT STDMETHODCALLTYPE mk_is_system(IMoniker *self, DWORD *kind)
{
    MK *mk = (MK *)self;
    if (!kind)
        return E_POINTER;
    *kind = mk->Kind == KIND_ITEM ? MKSYS_ITEMMONIKER : mk->Kind == KIND_FILE ? MKSYS_FILEMONIKER :
                                                                                 MKSYS_GENERICCOMPOSITE;
    return S_OK;
}

static BOOL equal_text(const WCHAR *a, const WCHAR *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static HRESULT STDMETHODCALLTYPE mk_is_equal(IMoniker *self, IMoniker *other)
{
    MK *mk = (MK *)self;
    if (!other)
        return E_INVALIDARG;
    MK *peer = mk_of(other);
    if (!peer || peer->Kind != mk->Kind)
        return S_FALSE;
    if (mk->Kind == KIND_COMPOSITE) {
        IMoniker *a[16], *b[16];
        ULONG na = 0, nb = 0;
        HRESULT hr = flatten(self, a, &na, 16);
        if (SUCCEEDED(hr))
            hr = flatten(other, b, &nb, 16);
        HRESULT result = hr;
        if (SUCCEEDED(hr)) {
            result = na == nb ? S_OK : S_FALSE;
            for (ULONG i = 0; result == S_OK && i < na; i++)
                result = a[i]->lpVtbl->IsEqual(a[i], b[i]);
        }
        for (ULONG i = 0; i < na; i++)
            a[i]->lpVtbl->Release(a[i]);
        for (ULONG i = 0; i < nb; i++)
            b[i]->lpVtbl->Release(b[i]);
        return result;
    }
    if (mk->Kind == KIND_ITEM && !equal_text(mk->Delimiter, peer->Delimiter))
        return S_FALSE;
    return equal_text(mk->Name, peer->Name) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE mk_hash(IMoniker *self, DWORD *hash)
{
    MK *mk = (MK *)self;
    if (!hash)
        return E_POINTER;
    DWORD value = mk->Kind * 0x9E3779B1u;
    if (mk->Kind == KIND_COMPOSITE) {
        DWORD a, b;
        mk->Left->lpVtbl->Hash(mk->Left, &a);
        mk->Right->lpVtbl->Hash(mk->Right, &b);
        value ^= a * 31 + b;
    } else {
        for (const WCHAR *c = mk->Name; *c; c++)
            value = value * 31 + (DWORD)towlower(*c);
        if (mk->Kind == KIND_ITEM)
            for (const WCHAR *c = mk->Delimiter; *c; c++)
                value = value * 31 + (DWORD)towlower(*c);
    }
    *hash = value;
    return S_OK;
}

/* ------------------------------------------------------------------ binding and composing */

static HRESULT STDMETHODCALLTYPE mk_bind_to_object(IMoniker *self, IBindCtx *context, IMoniker *left, REFIID iid,
                                                   void **object)
{
    MK *mk = (MK *)self;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!context)
        return E_INVALIDARG;
    if (mk->Kind == KIND_COMPOSITE) {
        /* the last component binds with the rest to its left. */
        IMoniker *whole = left ? nullptr : mk->Left;
        if (left)
            CreateGenericComposite(left, mk->Left, &whole);
        HRESULT hr = whole ? mk->Right->lpVtbl->BindToObject(mk->Right, context, whole, iid, object) : E_OUTOFMEMORY;
        if (left && whole)
            whole->lpVtbl->Release(whole);
        return hr;
    }
    if (mk->Kind == KIND_ITEM) {
        if (!left)
            return MK_E_SYNTAX_;
        IOleItemContainer *container;
        HRESULT hr = left->lpVtbl->BindToObject(left, context, nullptr, &IID_IOleItemContainer_, (void **)&container);
        if (FAILED(hr))
            return hr;
        hr = container->lpVtbl->GetObject(container, mk->Name, 0, context, iid, object);
        container->lpVtbl->Release(container);
        return hr;
    }
    /* binding a file moniker needs the file's class to make its object. */
    return MK_E_NOTBINDABLE_;
}

static HRESULT STDMETHODCALLTYPE mk_bind_to_storage(IMoniker *self, IBindCtx *context, IMoniker *left, REFIID iid,
                                                    void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    return MK_E_NOTBINDABLE_;
}

static HRESULT STDMETHODCALLTYPE mk_reduce(IMoniker *self, IBindCtx *context, DWORD how_far, IMoniker **left,
                                           IMoniker **reduced)
{
    if (!reduced)
        return E_POINTER;
    if (left)
        *left = nullptr;
    *reduced = self;
    self->lpVtbl->AddRef(self);
    return MK_S_REDUCED_TO_SELF_;
}

static HRESULT STDMETHODCALLTYPE mk_compose_with(IMoniker *self, IMoniker *right, BOOL only_if_not_generic,
                                                 IMoniker **composite)
{
    if (!composite)
        return E_POINTER;
    *composite = nullptr;
    if (!right)
        return E_INVALIDARG;
    if (only_if_not_generic)
        return MK_E_NEEDGENERIC_;
    return CreateGenericComposite(self, right, composite);
}

/* a simple moniker has no components to enumerate. */
static HRESULT STDMETHODCALLTYPE mk_enum(IMoniker *self, BOOL forward, IEnumMoniker **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mk_is_running(IMoniker *self, IBindCtx *context, IMoniker *left, IMoniker *newly)
{
    if (newly)
        return self->lpVtbl->IsEqual(self, newly) == S_OK ? S_OK : S_FALSE;
    IRunningObjectTable *table;
    HRESULT hr = GetRunningObjectTable(0, &table);
    if (FAILED(hr))
        return hr;
    hr = table->lpVtbl->IsRunning(table, self);
    table->lpVtbl->Release(table);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mk_get_time(IMoniker *self, IBindCtx *context, IMoniker *left, FILETIME *time)
{
    IRunningObjectTable *table;
    HRESULT hr = GetRunningObjectTable(0, &table);
    if (FAILED(hr))
        return hr;
    hr = table->lpVtbl->GetTimeOfLastChange(table, self, time);
    table->lpVtbl->Release(table);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mk_inverse(IMoniker *self, IMoniker **inverse)
{
    if (!inverse)
        return E_POINTER;
    *inverse = nullptr;
    return MK_E_NOINVERSE_;
}

static HRESULT STDMETHODCALLTYPE mk_common_prefix(IMoniker *self, IMoniker *other, IMoniker **prefix)
{
    if (!prefix)
        return E_POINTER;
    *prefix = nullptr;
    if (!other)
        return E_INVALIDARG;
    if (self->lpVtbl->IsEqual(self, other) == S_OK) {
        *prefix = self;
        self->lpVtbl->AddRef(self);
        return MK_S_US_;
    }
    return MK_E_NOPREFIX_;
}

static HRESULT STDMETHODCALLTYPE mk_relative_path(IMoniker *self, IMoniker *other, IMoniker **relative)
{
    if (!relative)
        return E_POINTER;
    *relative = nullptr;
    return E_NOTIMPL;
}

static const IMonikerVtbl mk_vtable = {
    mk_query,        mk_addref,        mk_release,        mk_get_class_id,      mk_is_dirty,      mk_load,
    mk_save,         mk_get_size_max,  mk_bind_to_object, mk_bind_to_storage,   mk_reduce,        mk_compose_with,
    mk_enum,         mk_is_equal,      mk_hash,           mk_is_running,        mk_get_time,      mk_inverse,
    mk_common_prefix, mk_relative_path, mk_get_display_name, mk_parse_display_name, mk_is_system,
};

/* the key in the machine wide table: the moniker's class and its lower case display name. */
BOOL MonikerRotKey(IMoniker *moniker, WCHAR *out, ULONG chars)
{
    CLSID clsid = {};
    moniker->lpVtbl->GetClassID(moniker, &clsid);
    IBindCtx *context = nullptr;
    LPOLESTR name = nullptr;
    CreateBindCtx(0, &context);
    HRESULT hr = moniker->lpVtbl->GetDisplayName(moniker, context, nullptr, &name);
    if (context)
        context->lpVtbl->Release(context);
    if (FAILED(hr) || !name)
        return FALSE;
    BOOL ok = StringFromGUID2(&clsid, out, (int)chars) > 0 && wcslen(out) + 1 + wcslen(name) < chars;
    if (ok) {
        SIZE_T at = wcslen(out);
        out[at++] = L'|';
        for (const WCHAR *c = name; *c; c++)
            out[at++] = (WCHAR)towlower(*c);
        out[at] = 0;
    }
    CoTaskMemFree(name);
    return ok;
}
