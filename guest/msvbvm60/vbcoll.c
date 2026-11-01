/*
 * the Collection of Visual Basic: items kept in order, each with an optional key, and the enumerator For Each walks.
 */
#include "vb.h"

typedef struct VbItem {
    VARIANT value;
    BSTR key;
} VbItem;

typedef struct VbCollection VbCollection;
struct VbCollection {
    const struct CollectionVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(VbCollection *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(VbCollection *);
        ULONG(STDMETHODCALLTYPE *Release)(VbCollection *);
        HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(VbCollection *, UINT *);
        HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(VbCollection *, UINT, LCID, ITypeInfo **);
        HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(VbCollection *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
        HRESULT(STDMETHODCALLTYPE *Invoke)(VbCollection *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
        HRESULT(STDMETHODCALLTYPE *Item)(VbCollection *, VARIANT *, VARIANT *);
        HRESULT(STDMETHODCALLTYPE *Add)(VbCollection *, VARIANT *, VARIANT *, VARIANT *, VARIANT *);
        HRESULT(STDMETHODCALLTYPE *Count)(VbCollection *, LONG *);
        HRESULT(STDMETHODCALLTYPE *Remove)(VbCollection *, VARIANT *);
        HRESULT(STDMETHODCALLTYPE *NewEnum)(VbCollection *, IUnknown **);
    } *lpVtbl;
    LONG refs;
    VbItem *items;
    int count, capacity;
};

typedef struct VbEnum VbEnum;
struct VbEnum {
    const struct EnumVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(VbEnum *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(VbEnum *);
        ULONG(STDMETHODCALLTYPE *Release)(VbEnum *);
        HRESULT(STDMETHODCALLTYPE *Next)(VbEnum *, ULONG, VARIANT *, ULONG *);
        HRESULT(STDMETHODCALLTYPE *Skip)(VbEnum *, ULONG);
        HRESULT(STDMETHODCALLTYPE *Reset)(VbEnum *);
        HRESULT(STDMETHODCALLTYPE *Clone)(VbEnum *, VbEnum **);
    } *lpVtbl;
    LONG refs;
    VbCollection *collection;
    int position;
};

const GUID CLSID_VbCollection = {0xA4C4671C, 0x499F, 0x101B, {0xBB, 0x78, 0x00, 0xAA, 0x00, 0x38, 0x3C, 0xBB}};
static const GUID IID_VbCollection = {0xA4C46780, 0x499F, 0x101B, {0xBB, 0x78, 0x00, 0xAA, 0x00, 0x38, 0x3C, 0xBB}};
static const GUID IID_VbEnumVariant = {0x00020404, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static const struct CollectionVtbl g_collection_vtbl;
static const struct EnumVtbl g_enum_vtbl;

/* a missing argument is a pointer to an error variant that says so, or no pointer. */
static BOOL missing(const VARIANT *v)
{
    while (v && V_VT(v) == (VT_BYREF | VT_VARIANT) && V_VARIANTREF(v))
        v = V_VARIANTREF(v);
    return !v || (V_VT(v) == VT_ERROR && V_ERROR(v) == DISP_E_PARAMNOTFOUND);
}

/* the place of an item given by its number or its key, counted from zero, or -1 when it is not there. */
static int find_key(const VbCollection *c, const WCHAR *key)
{
    for (int i = 0; i < c->count; i++)
        if (c->items[i].key && !lstrcmpiW(c->items[i].key, key))
            return i;
    return -1;
}

/* an index that is neither a key nor a number is a type mismatch, one that is out of range subscript out of range, a missing key an invalid call. */
static int locate(const VbCollection *c, VARIANT *index)
{
    VARIANT v;
    VariantInit(&v);
    VbCheck(VbCopyInd(&v, index));
    int place;
    if (V_VT(&v) == VT_BSTR) {
        place = find_key(c, V_BSTR(&v) ? V_BSTR(&v) : L"");
        VariantClear(&v);
        if (place < 0)
            VbRaise(VBERR_INVALID_CALL);
        return place;
    }
    VARIANT number;
    VariantInit(&number);
    HRESULT hr = VariantChangeTypeEx(&number, &v, VbLocale(), 0, VT_I4);
    VariantClear(&v);
    if (FAILED(hr))
        VbRaise(VBERR_TYPE_MISMATCH);
    if (V_I4(&number) < 1 || V_I4(&number) > c->count)
        VbRaise(VBERR_SUBSCRIPT);
    return V_I4(&number) - 1;
}

static HRESULT STDMETHODCALLTYPE collection_query(VbCollection *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch) || IsEqualGUID(iid, &IID_VbCollection)) {
        InterlockedIncrement(&self->refs);
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE collection_addref(VbCollection *self) { return (ULONG)InterlockedIncrement(&self->refs); }

static ULONG STDMETHODCALLTYPE collection_release(VbCollection *self)
{
    LONG n = InterlockedDecrement(&self->refs);
    if (n == 0) {
        for (int i = 0; i < self->count; i++) {
            VariantClear(&self->items[i].value);
            SysFreeString(self->items[i].key);
        }
        HeapFree(GetProcessHeap(), 0, self->items);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE collection_typeinfocount(VbCollection *self, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_typeinfo(VbCollection *self, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

enum { DISPID_ITEM = 0, DISPID_ADD = 1, DISPID_COUNT = 2, DISPID_REMOVE = 3 };

static HRESULT STDMETHODCALLTYPE collection_names(VbCollection *self, REFIID iid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    static const struct {
        const WCHAR *name;
        DISPID id;
    } known[] = {{L"Item", DISPID_ITEM}, {L"Add", DISPID_ADD}, {L"Count", DISPID_COUNT}, {L"Remove", DISPID_REMOVE}, {L"_NewEnum", DISPID_NEWENUM}};
    HRESULT result = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i)
            continue;
        for (size_t k = 0; k < sizeof(known) / sizeof(known[0]); k++)
            if (!lstrcmpiW(names[0], known[k].name))
                ids[0] = known[k].id;
    }
    for (UINT i = 0; i < count; i++)
        if (ids[i] == DISPID_UNKNOWN)
            result = DISP_E_UNKNOWNNAME;
    return result;
}

static HRESULT STDMETHODCALLTYPE collection_item(VbCollection *self, VARIANT *index, VARIANT *out)
{
    int place = locate(self, index);
    VariantInit(out);
    return VariantCopy(out, &self->items[place].value);
}

static HRESULT STDMETHODCALLTYPE collection_count(VbCollection *self, LONG *out)
{
    *out = self->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_add(VbCollection *self, VARIANT *item, VARIANT *key, VARIANT *before, VARIANT *after)
{
    BSTR name = nullptr;
    if (!missing(key)) {
        VARIANT v, text;
        VariantInit(&v);
        VariantInit(&text);
        VbCheck(VbCopyInd(&v, key));
        HRESULT hr = V_VT(&v) == VT_BSTR ? VariantCopy(&text, &v) : DISP_E_TYPEMISMATCH;
        VariantClear(&v);
        if (FAILED(hr))
            VbRaise(VBERR_TYPE_MISMATCH);
        name = V_BSTR(&text);
        if (!name)
            name = SysAllocString(L"");
        if (find_key(self, name) >= 0) {
            SysFreeString(name);
            VbRaise(457);
        }
    }
    int at = self->count;
    if (!missing(before) && !missing(after)) {
        SysFreeString(name);
        VbRaise(VBERR_INVALID_CALL);
    }
    if (!missing(before) || !missing(after)) {
        VARIANT *where = missing(before) ? after : before;
        int place;
        VARIANT v;
        VariantInit(&v);
        VbCheck(VbCopyInd(&v, where));
        if (V_VT(&v) == VT_BSTR) {
            place = find_key(self, V_BSTR(&v) ? V_BSTR(&v) : L"");
            VariantClear(&v);
            if (place < 0) {
                SysFreeString(name);
                VbRaise(VBERR_INVALID_CALL);
            }
        } else {
            VARIANT number;
            VariantInit(&number);
            HRESULT hr = VariantChangeTypeEx(&number, &v, VbLocale(), 0, VT_I4);
            VariantClear(&v);
            if (FAILED(hr)) {
                SysFreeString(name);
                VbRaise(VBERR_TYPE_MISMATCH);
            }
            place = V_I4(&number) - 1;
            if (place < 0 || place >= self->count) {
                SysFreeString(name);
                VbRaise(VBERR_SUBSCRIPT);
            }
        }
        at = where == before ? place : place + 1;
    }
    if (self->count == self->capacity) {
        int capacity = self->capacity ? self->capacity * 2 : 8;
        VbItem *grown = self->items ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, self->items, capacity * sizeof(VbItem))
                                    : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(VbItem));
        if (!grown) {
            SysFreeString(name);
            VbRaise(VBERR_OUT_OF_MEMORY);
        }
        self->items = grown;
        self->capacity = capacity;
    }
    for (int i = self->count; i > at; i--)
        self->items[i] = self->items[i - 1];
    VariantInit(&self->items[at].value);
    HRESULT hr = VbCopyInd(&self->items[at].value, item);
    if (FAILED(hr)) {
        for (int i = at; i < self->count; i++)
            self->items[i] = self->items[i + 1];
        SysFreeString(name);
        VbRaiseHr(hr);
    }
    self->items[at].key = name;
    self->count++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_remove(VbCollection *self, VARIANT *index)
{
    int place = locate(self, index);
    VariantClear(&self->items[place].value);
    SysFreeString(self->items[place].key);
    for (int i = place; i < self->count - 1; i++)
        self->items[i] = self->items[i + 1];
    self->count--;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_newenum(VbCollection *self, IUnknown **out)
{
    VbEnum *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    e->lpVtbl = &g_enum_vtbl;
    e->refs = 1;
    e->collection = self;
    InterlockedIncrement(&self->refs);
    *out = (IUnknown *)e;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_invoke(VbCollection *self, DISPID id, REFIID iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result,
                                                   EXCEPINFO *info, UINT *bad)
{
    VARIANT dummy;
    if (!result)
        result = &dummy;
    VariantInit(result);
    UINT n = params->cArgs;
    VARIANT *args = params->rgvarg;
    /* the arguments are stored last first. */
#define ARG(i) ((i) < n ? &args[n - 1 - (i)] : nullptr)
    switch (id) {
    case DISPID_ITEM:
        if (!n)
            return DISP_E_BADPARAMCOUNT;
        return collection_item(self, ARG(0), result);
    case DISPID_ADD:
        if (!n)
            return DISP_E_BADPARAMCOUNT;
        return collection_add(self, ARG(0), ARG(1), ARG(2), ARG(3));
    case DISPID_COUNT: {
        LONG count;
        collection_count(self, &count);
        V_VT(result) = VT_I4;
        V_I4(result) = count;
        return S_OK;
    }
    case DISPID_REMOVE:
        if (!n)
            return DISP_E_BADPARAMCOUNT;
        return collection_remove(self, ARG(0));
    case DISPID_NEWENUM: {
        IUnknown *e;
        HRESULT hr = collection_newenum(self, &e);
        if (SUCCEEDED(hr)) {
            V_VT(result) = VT_UNKNOWN;
            V_UNKNOWN(result) = e;
        }
        return hr;
    }
    }
#undef ARG
    return DISP_E_MEMBERNOTFOUND;
}

static const struct CollectionVtbl g_collection_vtbl = {
    collection_query, collection_addref, collection_release, collection_typeinfocount, collection_typeinfo, collection_names,
    collection_invoke, collection_item, collection_add, collection_count, collection_remove, collection_newenum,
};

void *VbNewCollection(void)
{
    VbCollection *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return nullptr;
    c->lpVtbl = &g_collection_vtbl;
    c->refs = 1;
    return c;
}

/* ------------------------------------------------------------------ the enumerator */

static HRESULT STDMETHODCALLTYPE enum_query(VbEnum *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_VbEnumVariant)) {
        InterlockedIncrement(&self->refs);
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_addref(VbEnum *self) { return (ULONG)InterlockedIncrement(&self->refs); }

static ULONG STDMETHODCALLTYPE enum_release(VbEnum *self)
{
    LONG n = InterlockedDecrement(&self->refs);
    if (n == 0) {
        collection_release(self->collection);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE enum_next(VbEnum *self, ULONG want, VARIANT *out, ULONG *fetched)
{
    ULONG got = 0;
    while (got < want && self->position < self->collection->count) {
        VariantInit(&out[got]);
        HRESULT hr = VariantCopy(&out[got], &self->collection->items[self->position].value);
        if (FAILED(hr))
            break;
        self->position++;
        got++;
    }
    if (fetched)
        *fetched = got;
    return got == want ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(VbEnum *self, ULONG count)
{
    self->position += (int)count;
    if (self->position > self->collection->count) {
        self->position = self->collection->count;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(VbEnum *self)
{
    self->position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(VbEnum *self, VbEnum **out)
{
    IUnknown *copy;
    HRESULT hr = collection_newenum(self->collection, &copy);
    if (SUCCEEDED(hr))
        ((VbEnum *)copy)->position = self->position;
    *out = (VbEnum *)copy;
    return hr;
}

static const struct EnumVtbl g_enum_vtbl = {enum_query, enum_addref, enum_release, enum_next, enum_skip, enum_reset, enum_clone};
