/*
 * bind contexts, the running object table of the process and the moniker
 * constructors. the table is process local: no other process sees it.
 */
#include "ole32_private.h"
#include <mime/stub.h>

/* ------------------------------------------------------------------ moniker enumerator */

typedef struct {
    IEnumMoniker Interface;
    LONG Refs;
    IMoniker **Items;
    ULONG Count;
    ULONG Position;
} MONIKER_ENUM;

static const IEnumMonikerVtbl moniker_enum_vtable;

static HRESULT create_moniker_enum(IMoniker **items, ULONG count, ULONG position, IEnumMoniker **result)
{
    MONIKER_ENUM *e = OleAllocZero(sizeof(*e));
    IMoniker **copy = OleAlloc((count ? count : 1) * sizeof(*copy));
    if (!e || !copy) {
        OleFree(e);
        OleFree(copy);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < count; i++) {
        copy[i] = items[i];
        copy[i]->lpVtbl->AddRef(copy[i]);
    }
    e->Interface.lpVtbl = &moniker_enum_vtable;
    e->Refs = 1;
    e->Items = copy;
    e->Count = count;
    e->Position = position;
    *result = &e->Interface;
    return S_OK;
}

static HRESULT WINAPI moniker_enum_query(IEnumMoniker *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IEnumMoniker))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI moniker_enum_addref(IEnumMoniker *self)
{
    return InterlockedIncrement(&((MONIKER_ENUM *)self)->Refs);
}

static ULONG WINAPI moniker_enum_release(IEnumMoniker *self)
{
    MONIKER_ENUM *e = (MONIKER_ENUM *)self;
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        for (ULONG i = 0; i < e->Count; i++)
            e->Items[i]->lpVtbl->Release(e->Items[i]);
        OleFree(e->Items);
        OleFree(e);
    }
    return refs;
}

static HRESULT WINAPI moniker_enum_next(IEnumMoniker *self, ULONG count, IMoniker **items, ULONG *fetched)
{
    MONIKER_ENUM *e = (MONIKER_ENUM *)self;
    if (!items || (count > 1 && !fetched))
        return E_INVALIDARG;
    ULONG n = 0;
    while (n < count && e->Position < e->Count) {
        items[n] = e->Items[e->Position++];
        items[n]->lpVtbl->AddRef(items[n]);
        n++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT WINAPI moniker_enum_skip(IEnumMoniker *self, ULONG count)
{
    MONIKER_ENUM *e = (MONIKER_ENUM *)self;
    ULONG left = e->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT WINAPI moniker_enum_reset(IEnumMoniker *self)
{
    ((MONIKER_ENUM *)self)->Position = 0;
    return S_OK;
}

static HRESULT WINAPI moniker_enum_clone(IEnumMoniker *self, IEnumMoniker **clone)
{
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    MONIKER_ENUM *e = (MONIKER_ENUM *)self;
    return create_moniker_enum(e->Items, e->Count, e->Position, clone);
}

static const IEnumMonikerVtbl moniker_enum_vtable = {
    moniker_enum_query, moniker_enum_addref, moniker_enum_release, moniker_enum_next,
    moniker_enum_skip,  moniker_enum_reset,  moniker_enum_clone,
};

/* ------------------------------------------------------------------ the running object table */

typedef struct {
    IUnknown *Object;
    IMoniker *Name;
    DWORD Cookie;
    FILETIME Time;
} ROT_ENTRY;

static SRWLOCK g_rot_lock = SRWLOCK_INIT;
static ROT_ENTRY *g_rot;
static ULONG g_rot_count, g_rot_capacity;
static DWORD g_rot_cookie;

/* the referenced names of every entry, so that IsEqual runs without the lock held. */
static IMoniker **rot_snapshot(ULONG *count)
{
    AcquireSRWLockShared(&g_rot_lock);
    IMoniker **names = OleAlloc((g_rot_count ? g_rot_count : 1) * sizeof(*names));
    *count = 0;
    if (names) {
        for (ULONG i = 0; i < g_rot_count; i++) {
            names[i] = g_rot[i].Name;
            names[i]->lpVtbl->AddRef(names[i]);
        }
        *count = g_rot_count;
    }
    ReleaseSRWLockShared(&g_rot_lock);
    return names;
}

static void release_names(IMoniker **names, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        names[i]->lpVtbl->Release(names[i]);
    OleFree(names);
}

/* finds the registered entry equal to name; returns its referenced object and time. */
static BOOL rot_find(IMoniker *name, IUnknown **object, FILETIME *time)
{
    ULONG count;
    IMoniker **names = rot_snapshot(&count);
    if (!names)
        return FALSE;
    IMoniker *match = nullptr;
    for (ULONG i = 0; i < count && !match; i++)
        if (names[i] == name || name->lpVtbl->IsEqual(name, names[i]) == S_OK)
            match = names[i];
    BOOL found = FALSE;
    if (match) {
        AcquireSRWLockShared(&g_rot_lock);
        for (ULONG i = 0; i < g_rot_count; i++) {
            if (g_rot[i].Name != match)
                continue;
            if (object) {
                *object = g_rot[i].Object;
                (*object)->lpVtbl->AddRef(*object);
            }
            if (time)
                *time = g_rot[i].Time;
            found = TRUE;
            break;
        }
        ReleaseSRWLockShared(&g_rot_lock);
    }
    release_names(names, count);
    return found;
}

static HRESULT WINAPI rot_query(IRunningObjectTable *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IRunningObjectTable))
        return E_NOINTERFACE;
    *result = self;
    return S_OK;
}

static ULONG WINAPI rot_addref(IRunningObjectTable *self)
{
    return 2;
}

static ULONG WINAPI rot_release(IRunningObjectTable *self)
{
    return 1;
}

static HRESULT WINAPI rot_register(IRunningObjectTable *self, DWORD flags, IUnknown *object, IMoniker *name,
                                   DWORD *cookie)
{
    if (!cookie)
        return E_INVALIDARG;
    *cookie = 0;
    if (!object || !name || (flags & ~(ROTFLAGS_REGISTRATIONKEEPSALIVE | ROTFLAGS_ALLOWANYCLIENT)))
        return E_INVALIDARG;
    HRESULT hr = rot_find(name, nullptr, nullptr) ? MK_S_MONIKERALREADYREGISTERED : S_OK;
    AcquireSRWLockExclusive(&g_rot_lock);
    if (g_rot_count == g_rot_capacity) {
        ULONG capacity = g_rot_capacity ? g_rot_capacity * 2 : 8;
        ROT_ENTRY *grown = g_rot ? HeapReAlloc(GetProcessHeap(), 0, g_rot, capacity * sizeof(*grown)) :
                                   OleAlloc(capacity * sizeof(*grown));
        if (!grown) {
            ReleaseSRWLockExclusive(&g_rot_lock);
            return E_OUTOFMEMORY;
        }
        g_rot = grown;
        g_rot_capacity = capacity;
    }
    ROT_ENTRY *entry = &g_rot[g_rot_count++];
    entry->Object = object;
    entry->Name = name;
    if (!++g_rot_cookie)
        ++g_rot_cookie;
    entry->Cookie = g_rot_cookie;
    GetSystemTimeAsFileTime(&entry->Time);
    object->lpVtbl->AddRef(object);
    name->lpVtbl->AddRef(name);
    *cookie = entry->Cookie;
    ReleaseSRWLockExclusive(&g_rot_lock);
    return hr;
}

static HRESULT WINAPI rot_revoke(IRunningObjectTable *self, DWORD cookie)
{
    ROT_ENTRY removed = {};
    AcquireSRWLockExclusive(&g_rot_lock);
    for (ULONG i = 0; i < g_rot_count; i++) {
        if (g_rot[i].Cookie == cookie) {
            removed = g_rot[i];
            g_rot[i] = g_rot[--g_rot_count];
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_rot_lock);
    if (!removed.Object)
        return E_INVALIDARG;
    removed.Object->lpVtbl->Release(removed.Object);
    removed.Name->lpVtbl->Release(removed.Name);
    return S_OK;
}

static HRESULT WINAPI rot_is_running(IRunningObjectTable *self, IMoniker *name)
{
    if (!name)
        return E_INVALIDARG;
    return rot_find(name, nullptr, nullptr) ? S_OK : S_FALSE;
}

static HRESULT WINAPI rot_get_object(IRunningObjectTable *self, IMoniker *name, IUnknown **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!name)
        return E_INVALIDARG;
    return rot_find(name, object, nullptr) ? S_OK : MK_E_UNAVAILABLE;
}

static HRESULT WINAPI rot_note_change_time(IRunningObjectTable *self, DWORD cookie, FILETIME *time)
{
    if (!time)
        return E_INVALIDARG;
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockExclusive(&g_rot_lock);
    for (ULONG i = 0; i < g_rot_count; i++) {
        if (g_rot[i].Cookie == cookie) {
            g_rot[i].Time = *time;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_rot_lock);
    return hr;
}

static HRESULT WINAPI rot_get_time(IRunningObjectTable *self, IMoniker *name, FILETIME *time)
{
    if (!name || !time)
        return E_INVALIDARG;
    return rot_find(name, nullptr, time) ? S_OK : MK_E_UNAVAILABLE;
}

static HRESULT WINAPI rot_enum_running(IRunningObjectTable *self, IEnumMoniker **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    ULONG count;
    IMoniker **names = rot_snapshot(&count);
    if (!names)
        return E_OUTOFMEMORY;
    HRESULT hr = create_moniker_enum(names, count, 0, enumerator);
    release_names(names, count);
    return hr;
}

static const IRunningObjectTableVtbl rot_vtable = {
    rot_query,       rot_addref,           rot_release,  rot_register,     rot_revoke,
    rot_is_running,  rot_get_object,       rot_note_change_time, rot_get_time, rot_enum_running,
};
static IRunningObjectTable g_rot_object = {&rot_vtable};

WINOLEAPI HRESULT WINAPI GetRunningObjectTable(DWORD reserved, IRunningObjectTable **table)
{
    if (!table)
        return E_INVALIDARG;
    *table = nullptr;
    if (reserved)
        return E_UNEXPECTED;
    HRESULT hr = OleCheckApartment();
    if (FAILED(hr))
        return hr;
    *table = &g_rot_object;
    return S_OK;
}

/* ------------------------------------------------------------------ bind contexts */

typedef struct {
    LPOLESTR Key;
    IUnknown *Object;
} BIND_PARAM;

typedef struct {
    IBindCtx Interface;
    LONG Refs;
    SRWLOCK Lock;
    IUnknown **Bound;
    ULONG BoundCount, BoundCapacity;
    BIND_PARAM *Params;
    ULONG ParamCount, ParamCapacity;
    BIND_OPTS3 Options;
} BIND_CONTEXT;

/* grows an array of items of the given size to hold one more. */
static BOOL grow(void **items, ULONG count, ULONG *capacity, SIZE_T size)
{
    if (count < *capacity)
        return TRUE;
    ULONG wanted = *capacity ? *capacity * 2 : 4;
    void *grown = *items ? HeapReAlloc(GetProcessHeap(), 0, *items, wanted * size) : OleAlloc(wanted * size);
    if (!grown)
        return FALSE;
    *items = grown;
    *capacity = wanted;
    return TRUE;
}

static HRESULT WINAPI bind_query(IBindCtx *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IBindCtx))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI bind_addref(IBindCtx *self)
{
    return InterlockedIncrement(&((BIND_CONTEXT *)self)->Refs);
}

static HRESULT WINAPI bind_release_bound(IBindCtx *self)
{
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    AcquireSRWLockExclusive(&bc->Lock);
    IUnknown **bound = bc->Bound;
    ULONG count = bc->BoundCount;
    bc->Bound = nullptr;
    bc->BoundCount = bc->BoundCapacity = 0;
    ReleaseSRWLockExclusive(&bc->Lock);
    for (ULONG i = 0; i < count; i++)
        bound[i]->lpVtbl->Release(bound[i]);
    OleFree(bound);
    return S_OK;
}

static ULONG WINAPI bind_release(IBindCtx *self)
{
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    LONG refs = InterlockedDecrement(&bc->Refs);
    if (!refs) {
        bind_release_bound(self);
        for (ULONG i = 0; i < bc->ParamCount; i++) {
            OleFree(bc->Params[i].Key);
            bc->Params[i].Object->lpVtbl->Release(bc->Params[i].Object);
        }
        OleFree(bc->Params);
        OleFree(bc);
    }
    return refs;
}

static HRESULT WINAPI bind_register_bound(IBindCtx *self, IUnknown *object)
{
    if (!object)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    AcquireSRWLockExclusive(&bc->Lock);
    BOOL ok = grow((void **)&bc->Bound, bc->BoundCount, &bc->BoundCapacity, sizeof(*bc->Bound));
    if (ok) {
        bc->Bound[bc->BoundCount++] = object;
        object->lpVtbl->AddRef(object);
    }
    ReleaseSRWLockExclusive(&bc->Lock);
    return ok ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI bind_revoke_bound(IBindCtx *self, IUnknown *object)
{
    if (!object)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&bc->Lock);
    for (ULONG i = bc->BoundCount; i-- > 0;) {
        if (bc->Bound[i] == object) {
            memmove(&bc->Bound[i], &bc->Bound[i + 1], (bc->BoundCount - i - 1) * sizeof(*bc->Bound));
            bc->BoundCount--;
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&bc->Lock);
    if (!found)
        return MK_E_NOTBOUND;
    object->lpVtbl->Release(object);
    return S_OK;
}

static HRESULT WINAPI bind_set_options(IBindCtx *self, BIND_OPTS *options)
{
    if (!options)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    DWORD size = options->cbStruct < sizeof(bc->Options) ? options->cbStruct : sizeof(bc->Options);
    if (size < sizeof(BIND_OPTS))
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&bc->Lock);
    memcpy((BYTE *)&bc->Options + sizeof(DWORD), (BYTE *)options + sizeof(DWORD), size - sizeof(DWORD));
    ReleaseSRWLockExclusive(&bc->Lock);
    return S_OK;
}

static HRESULT WINAPI bind_get_options(IBindCtx *self, BIND_OPTS *options)
{
    if (!options)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    DWORD size = options->cbStruct < sizeof(bc->Options) ? options->cbStruct : sizeof(bc->Options);
    if (size < sizeof(BIND_OPTS))
        return E_INVALIDARG;
    AcquireSRWLockShared(&bc->Lock);
    memcpy((BYTE *)options + sizeof(DWORD), (BYTE *)&bc->Options + sizeof(DWORD), size - sizeof(DWORD));
    ReleaseSRWLockShared(&bc->Lock);
    return S_OK;
}

static HRESULT WINAPI bind_get_rot(IBindCtx *self, IRunningObjectTable **table)
{
    return GetRunningObjectTable(0, table);
}

/* the index of the parameter named key, or -1; the lock is held. */
static LONG find_param(BIND_CONTEXT *bc, LPCOLESTR key)
{
    for (ULONG i = 0; i < bc->ParamCount; i++)
        if (!wcscmp(bc->Params[i].Key, key))
            return (LONG)i;
    return -1;
}

static HRESULT WINAPI bind_register_param(IBindCtx *self, LPOLESTR key, IUnknown *object)
{
    if (!key || !object)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    SIZE_T bytes = (wcslen(key) + 1) * sizeof(WCHAR);
    LPOLESTR copy = OleAlloc(bytes);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, key, bytes);
    object->lpVtbl->AddRef(object);
    IUnknown *old = nullptr;
    AcquireSRWLockExclusive(&bc->Lock);
    LONG index = find_param(bc, key);
    if (index >= 0) {
        old = bc->Params[index].Object;
        bc->Params[index].Object = object;
    } else if (grow((void **)&bc->Params, bc->ParamCount, &bc->ParamCapacity, sizeof(*bc->Params))) {
        bc->Params[bc->ParamCount].Key = copy;
        bc->Params[bc->ParamCount++].Object = object;
        copy = nullptr;
    } else {
        old = object;
        index = -2;
    }
    ReleaseSRWLockExclusive(&bc->Lock);
    OleFree(copy);
    if (old)
        old->lpVtbl->Release(old);
    return index == -2 ? E_OUTOFMEMORY : S_OK;
}

static HRESULT WINAPI bind_get_param(IBindCtx *self, LPOLESTR key, IUnknown **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!key)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    AcquireSRWLockShared(&bc->Lock);
    LONG index = find_param(bc, key);
    if (index >= 0) {
        *object = bc->Params[index].Object;
        (*object)->lpVtbl->AddRef(*object);
    }
    ReleaseSRWLockShared(&bc->Lock);
    return index >= 0 ? S_OK : E_FAIL;
}

static HRESULT WINAPI bind_enum_params(IBindCtx *self, IEnumString **enumerator)
{
    if (!enumerator)
        return E_POINTER;
    *enumerator = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI bind_revoke_param(IBindCtx *self, LPOLESTR key)
{
    if (!key)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = (BIND_CONTEXT *)self;
    BIND_PARAM removed = {};
    AcquireSRWLockExclusive(&bc->Lock);
    LONG index = find_param(bc, key);
    if (index >= 0) {
        removed = bc->Params[index];
        bc->Params[index] = bc->Params[--bc->ParamCount];
    }
    ReleaseSRWLockExclusive(&bc->Lock);
    if (index < 0)
        return S_FALSE;
    OleFree(removed.Key);
    removed.Object->lpVtbl->Release(removed.Object);
    return S_OK;
}

static const IBindCtxVtbl bind_vtable = {
    bind_query,          bind_addref,         bind_release,     bind_register_bound, bind_revoke_bound,
    bind_release_bound,  bind_set_options,    bind_get_options, bind_get_rot,        bind_register_param,
    bind_get_param,      bind_enum_params,    bind_revoke_param,
};

WINOLEAPI HRESULT WINAPI CreateBindCtx(DWORD reserved, IBindCtx **context)
{
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    if (reserved)
        return E_INVALIDARG;
    BIND_CONTEXT *bc = OleAllocZero(sizeof(*bc));
    if (!bc)
        return E_OUTOFMEMORY;
    bc->Interface.lpVtbl = &bind_vtable;
    bc->Refs = 1;
    bc->Options.cbStruct = sizeof(bc->Options);
    bc->Options.grfMode = STGM_READWRITE;
    bc->Options.dwClassContext = CLSCTX_SERVER;
    bc->Options.locale = GetThreadLocale();
    *context = &bc->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ monikers */

WINOLEAPI HRESULT WINAPI CreateFileMoniker(LPCOLESTR path, IMoniker **moniker)
{
    MIME_STUB("ole32");
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI CreateItemMoniker(LPCOLESTR delimiter, LPCOLESTR item, IMoniker **moniker)
{
    MIME_STUB("ole32");
    if (!moniker)
        return E_INVALIDARG;
    *moniker = nullptr;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI CreateGenericComposite(IMoniker *first, IMoniker *rest, IMoniker **composite)
{
    if (!composite)
        return E_POINTER;
    *composite = nullptr;
    /* a composite of one moniker is that moniker; true composites are not implemented. */
    if (first && rest)
        return E_NOTIMPL;
    *composite = first ? first : rest;
    if (*composite)
        (*composite)->lpVtbl->AddRef(*composite);
    return S_OK;
}
