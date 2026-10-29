/*
 * ITypeLib2 and the library's ITypeComp, loading libraries from files and program resources, and the registry:
 * RegisterTypeLib and lookup by library id.
 */
#include "typelib.h"
#include <mime/win32/winnls.h>
#include <mime/win32/shlwapi.h>

#define CLSID_PSOA L"{00020424-0000-0000-C000-000000000046}"
#define CLSID_PSDISPATCH L"{00020420-0000-0000-C000-000000000046}"

static void copy_wide(WCHAR *to, const WCHAR *from, size_t capacity)
{
    size_t n = wcslen(from);
    if (n >= capacity)
        n = capacity - 1;
    memcpy(to, from, n * sizeof(WCHAR));
    to[n] = 0;
}

/* ------------------------------------------------------------------ library cache */

typedef struct CACHE_ENTRY {
    struct CACHE_ENTRY *Next;
    WCHAR *Path;
    SYSKIND Syskind;
    TL_LIB *Lib;
} CACHE_ENTRY;

static CRITICAL_SECTION g_cache_lock;
static BOOL g_cache_init;
static CACHE_ENTRY *g_cache;
static INIT_ONCE g_cache_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK cache_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_cache_lock);
    g_cache_init = TRUE;
    return TRUE;
}

static void cache_lock(void)
{
    InitOnceExecuteOnce(&g_cache_once, cache_init, nullptr, nullptr);
    EnterCriticalSection(&g_cache_lock);
}

static BOOL try_addref(TL_LIB *lib)
{
    for (;;) {
        LONG refs = lib->Refs;
        if (refs <= 0)
            return FALSE;
        if (InterlockedCompareExchange(&lib->Refs, refs + 1, refs) == refs)
            return TRUE;
    }
}

static TL_LIB *cache_find(const WCHAR *path, SYSKIND syskind)
{
    TL_LIB *found = nullptr;
    cache_lock();
    for (CACHE_ENTRY *e = g_cache; e; e = e->Next)
        if (!_wcsicmp(e->Path, path) && (syskind == (SYSKIND)-1 || e->Syskind == syskind) && try_addref(e->Lib)) {
            found = e->Lib;
            break;
        }
    LeaveCriticalSection(&g_cache_lock);
    return found;
}

static void cache_add(TL_LIB *lib, const WCHAR *path, SYSKIND syskind)
{
    CACHE_ENTRY *e = AutAllocZero(sizeof(*e) + (wcslen(path) + 1) * sizeof(WCHAR));
    if (!e)
        return;
    e->Path = (WCHAR *)(e + 1);
    wcscpy(e->Path, path);
    e->Syskind = syskind;
    e->Lib = lib;
    cache_lock();
    e->Next = g_cache;
    g_cache = e;
    LeaveCriticalSection(&g_cache_lock);
}

static void cache_remove(TL_LIB *lib)
{
    if (!g_cache_init)
        return;
    cache_lock();
    for (CACHE_ENTRY **link = &g_cache; *link;) {
        if ((*link)->Lib == lib) {
            CACHE_ENTRY *gone = *link;
            *link = gone->Next;
            AutFree(gone);
        } else {
            link = &(*link)->Next;
        }
    }
    LeaveCriticalSection(&g_cache_lock);
}

/* ------------------------------------------------------------------ ITypeLib2 */

TL_LIB *TlLibCreate(void)
{
    TL_LIB *lib = AutAllocZero(sizeof(TL_LIB));
    if (!lib)
        return nullptr;
    lib->lpVtbl = &g_typelib_vtbl;
    lib->lpCompVtbl = &g_typelib_comp_vtbl;
    lib->lpCreateVtbl = &g_createlib_vtbl;
    lib->Refs = 1;
    lib->Syskind = sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32;
    lib->FileSyskind = lib->Syskind;
    lib->DispatchRef = 0xFFFFFFFF;
    lib->CodePage = CP_ACP;
    InitializeCriticalSection(&lib->Lock);
    return lib;
}

void TlLibDestroy(TL_LIB *lib)
{
    cache_remove(lib);
    for (ULONG i = 0; i < lib->ImportCount && lib->ImportedLibs; i++)
        AutRelease(lib->ImportedLibs[i]);
    if (lib->ReleaseImage)
        lib->ReleaseImage(lib->ImageContext, lib->Image);
    TlHeapFree(lib);
    DeleteCriticalSection(&lib->Lock);
    AutFree(lib);
}

void TlLibAddRef(TL_LIB *lib)
{
    InterlockedIncrement(&lib->Refs);
}

void TlLibRelease(TL_LIB *lib)
{
    if (InterlockedDecrement(&lib->Refs) == 0) {
        /* a cache lookup may race with the last release, so it only takes references on live libraries. */
        TlLibDestroy(lib);
    }
}

TL_TYPE *TlTypeAdd(TL_LIB *lib)
{
    if (lib->TypeCount == lib->TypeCapacity) {
        ULONG capacity = lib->TypeCapacity ? lib->TypeCapacity * 2 : 16;
        TL_TYPE **types = TlAlloc(lib, (SIZE_T)capacity * sizeof(TL_TYPE *));
        if (!types)
            return nullptr;
        if (lib->TypeCount)
            memcpy(types, lib->Types, lib->TypeCount * sizeof(TL_TYPE *));
        lib->Types = types;
        lib->TypeCapacity = capacity;
    }
    TL_TYPE *type = TlAlloc(lib, sizeof(TL_TYPE));
    if (!type)
        return nullptr;
    type->Lib = lib;
    type->Index = lib->TypeCount;
    type->Constructor = type->Destructor = MEMBERID_NIL;
    type->NameOff = type->DocOff = type->GuidOff = type->CustOff = 0xFFFFFFFF;
    type->Major = 0;
    type->Minor = 0;
    lib->Types[lib->TypeCount++] = type;
    return type;
}

HRESULT TlLibQuery(TL_LIB *lib, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_ITypeLib) || IsEqualIID(iid, &IID_ITypeLib2))
        *out = &lib->lpVtbl;
    else if (IsEqualIID(iid, &IID_ITypeComp))
        *out = &lib->lpCompVtbl;
    else if (lib->Writable && (IsEqualIID(iid, &IID_ICreateTypeLib) || IsEqualIID(iid, &IID_ICreateTypeLib2)))
        *out = &lib->lpCreateVtbl;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&lib->Refs);
    return S_OK;
}

static TL_LIB *lib_of(ITypeLib2 *self)
{
    return (TL_LIB *)self;
}

static TL_LIB *lib_of_comp(ITypeComp *self)
{
    return CONTAINING_RECORD(self, TL_LIB, lpCompVtbl);
}

static HRESULT WINAPI tl_query(ITypeLib2 *self, REFIID iid, void **out)
{
    return TlLibQuery(lib_of(self), iid, out);
}

static ULONG WINAPI tl_addref(ITypeLib2 *self)
{
    return InterlockedIncrement(&lib_of(self)->Refs);
}

static ULONG WINAPI tl_release(ITypeLib2 *self)
{
    TL_LIB *lib = lib_of(self);
    LONG refs = InterlockedDecrement(&lib->Refs);
    if (refs == 0)
        TlLibDestroy(lib);
    return refs;
}

static UINT WINAPI tl_get_type_info_count(ITypeLib2 *self)
{
    return lib_of(self)->TypeCount;
}

static HRESULT WINAPI tl_get_type_info(ITypeLib2 *self, UINT index, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    return TlGetTypeInfo(lib_of(self), index, FALSE, info);
}

static TYPEKIND dual_kind(const TL_TYPE *t)
{
    if ((t->Flags & TYPEFLAG_FDUAL) && (t->Kind == TKIND_DISPATCH || t->Kind == TKIND_INTERFACE))
        return TKIND_DISPATCH;
    return t->Kind;
}

static HRESULT WINAPI tl_get_type_info_type(ITypeLib2 *self, UINT index, TYPEKIND *kind)
{
    TL_LIB *lib = lib_of(self);
    if (!kind)
        return E_POINTER;
    if (index >= lib->TypeCount)
        return TYPE_E_ELEMENTNOTFOUND;
    *kind = dual_kind(lib->Types[index]);
    return S_OK;
}

static HRESULT WINAPI tl_get_type_info_of_guid(ITypeLib2 *self, REFGUID guid, ITypeInfo **info)
{
    TL_LIB *lib = lib_of(self);
    if (!guid || !info)
        return E_POINTER;
    *info = nullptr;
    for (ULONG i = 0; i < lib->TypeCount; i++)
        if (lib->Types[i]->HasGuid && IsEqualGUID(&lib->Types[i]->Guid, guid))
            return TlGetTypeInfo(lib, i, FALSE, info);
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI tl_get_lib_attr(ITypeLib2 *self, TLIBATTR **out)
{
    TL_LIB *lib = lib_of(self);
    if (!out)
        return E_POINTER;
    TLIBATTR *attr = AutAllocZero(sizeof(*attr));
    if (!attr) {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    if (lib->HasGuid)
        attr->guid = lib->Guid;
    attr->lcid = lib->Lcid;
    attr->syskind = lib->Syskind;
    attr->wMajorVerNum = lib->Major;
    attr->wMinorVerNum = lib->Minor;
    attr->wLibFlags = lib->Flags;
    *out = attr;
    return S_OK;
}

static HRESULT WINAPI tl_get_type_comp(ITypeLib2 *self, ITypeComp **out)
{
    TL_LIB *lib = lib_of(self);
    if (!out)
        return E_POINTER;
    *out = (ITypeComp *)&lib->lpCompVtbl;
    InterlockedIncrement(&lib->Refs);
    return S_OK;
}

static const WCHAR *lib_doc(TL_LIB *lib)
{
    return lib->Doc;
}

static HRESULT WINAPI tl_get_documentation(ITypeLib2 *self, INT index, BSTR *name, BSTR *doc, DWORD *context,
                                           BSTR *file)
{
    TL_LIB *lib = lib_of(self);
    if (index == -1) {
        if (name)
            *name = nullptr;
        if (doc)
            *doc = nullptr;
        if (context)
            *context = lib->HelpContext;
        if (file)
            *file = nullptr;
        if (name && lib->Name && !(*name = SysAllocString(lib->Name)))
            return E_OUTOFMEMORY;
        if (doc && lib_doc(lib) && !(*doc = SysAllocString(lib_doc(lib)))) {
            if (name) {
                SysFreeString(*name);
                *name = nullptr;
            }
            return E_OUTOFMEMORY;
        }
        if (file && lib->HelpFile) {
            *file = SysAllocString(lib->HelpFile);
            if (!*file) {
                if (name) {
                    SysFreeString(*name);
                    *name = nullptr;
                }
                if (doc) {
                    SysFreeString(*doc);
                    *doc = nullptr;
                }
                return E_OUTOFMEMORY;
            }
        }
        return S_OK;
    }
    if ((UINT)index >= lib->TypeCount)
        return TYPE_E_ELEMENTNOTFOUND;
    ITypeInfo *info;
    HRESULT hr = TlGetTypeInfo(lib, (UINT)index, FALSE, &info);
    if (FAILED(hr))
        return hr;
    hr = info->lpVtbl->GetDocumentation(info, MEMBERID_NIL, name, doc, context, file);
    info->lpVtbl->Release(info);
    return hr;
}

/* whether a type, member or parameter uses the name, and the library's own spelling of it. */
static BOOL find_name_in_type(TL_TYPE *t, LPCOLESTR name, MEMBERID *id, const WCHAR **spelling, BOOL params)
{
    const WCHAR *type_name = TlTypeName(t);
    if (type_name && !_wcsicmp(type_name, name)) {
        *id = MEMBERID_NIL;
        *spelling = type_name;
        return TRUE;
    }
    if (FAILED(TlLoadMembers(t)))
        return FALSE;
    for (UINT i = 0; i < t->FuncCount; i++) {
        TL_FUNC *f = &t->Funcs[i];
        if (f->Name && !_wcsicmp(f->Name, name)) {
            *id = f->Id;
            *spelling = f->Name;
            return TRUE;
        }
        if (params)
            for (USHORT j = 0; j < f->ParamCount; j++)
                if (f->Params[j].Name && !_wcsicmp(f->Params[j].Name, name)) {
                    *id = MEMBERID_NIL;
                    *spelling = f->Params[j].Name;
                    return TRUE;
                }
    }
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name)) {
            *id = t->Vars[i].Id;
            *spelling = t->Vars[i].Name;
            return TRUE;
        }
    return FALSE;
}

static HRESULT WINAPI tl_is_name(ITypeLib2 *self, LPOLESTR name, ULONG hash, BOOL *found)
{
    TL_LIB *lib = lib_of(self);
    if (!name || !found)
        return E_INVALIDARG;
    *found = FALSE;
    if (lib->Name && !_wcsicmp(lib->Name, name)) {
        wcscpy(name, lib->Name);
        *found = TRUE;
        return S_OK;
    }
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        MEMBERID id;
        const WCHAR *spelling;
        if (find_name_in_type(lib->Types[i], name, &id, &spelling, TRUE)) {
            wcscpy(name, spelling);
            *found = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI tl_find_name(ITypeLib2 *self, LPOLESTR name, ULONG hash, ITypeInfo **infos, MEMBERID *ids,
                                   USHORT *count)
{
    TL_LIB *lib = lib_of(self);
    if (!name || !infos || !ids || !count)
        return E_INVALIDARG;
    USHORT limit = *count, n = 0;
    for (ULONG i = 0; i < lib->TypeCount && n < limit; i++) {
        MEMBERID id;
        const WCHAR *spelling;
        if (!find_name_in_type(lib->Types[i], name, &id, &spelling, FALSE))
            continue;
        HRESULT hr = TlGetTypeInfo(lib, i, FALSE, &infos[n]);
        if (FAILED(hr)) {
            for (USHORT j = 0; j < n; j++)
                infos[j]->lpVtbl->Release(infos[j]);
            *count = 0;
            return hr;
        }
        ids[n++] = id;
    }
    *count = n;
    return S_OK;
}

static void WINAPI tl_release_tlib_attr(ITypeLib2 *self, TLIBATTR *attr)
{
    AutFree(attr);
}

static HRESULT WINAPI tl_get_cust_data(ITypeLib2 *self, REFGUID guid, VARIANT *value)
{
    TL_LIB *lib = lib_of(self);
    if (!value)
        return E_INVALIDARG;
    value->vt = VT_EMPTY;
    for (TL_CUST *c = lib->Cust; c; c = c->Next)
        if (IsEqualGUID(&c->Guid, guid)) {
            *value = c->Value;
            if (c->Value.vt == VT_BSTR && c->Value.bstrVal) {
                value->bstrVal = SysAllocStringLen(c->Value.bstrVal, SysStringLen(c->Value.bstrVal));
                if (!value->bstrVal)
                    return E_OUTOFMEMORY;
            }
            return S_OK;
        }
    return S_OK;
}

HRESULT TlCustAll(TL_CUST *list, CUSTDATA *data);

static HRESULT WINAPI tl_get_lib_statistics(ITypeLib2 *self, ULONG *unique, ULONG *changed)
{
    TL_LIB *lib = lib_of(self);
    if (unique)
        *unique = lib->TypeCount;
    if (changed)
        *changed = 0;
    return S_OK;
}

static HRESULT WINAPI tl_get_documentation2(ITypeLib2 *self, INT index, LCID lcid, BSTR *help, DWORD *context,
                                            BSTR *dll)
{
    TL_LIB *lib = lib_of(self);
    if (help)
        *help = nullptr;
    if (context)
        *context = 0;
    if (dll)
        *dll = nullptr;
    if (index != -1) {
        if ((UINT)index >= lib->TypeCount)
            return TYPE_E_ELEMENTNOTFOUND;
        ITypeInfo *info;
        HRESULT hr = TlGetTypeInfo(lib, (UINT)index, FALSE, &info);
        if (FAILED(hr))
            return hr;
        ITypeInfo2 *info2;
        hr = info->lpVtbl->QueryInterface(info, &IID_ITypeInfo2, (void **)&info2);
        info->lpVtbl->Release(info);
        if (FAILED(hr))
            return hr;
        hr = info2->lpVtbl->GetDocumentation2(info2, MEMBERID_NIL, lcid, help, context, dll);
        info2->lpVtbl->Release(info2);
        return hr;
    }
    if (help && lib->Doc && !(*help = SysAllocString(lib->Doc)))
        return E_OUTOFMEMORY;
    if (context)
        *context = lib->HelpStringContext;
    if (dll && lib->HelpStringDll)
        *dll = SysAllocString(lib->HelpStringDll);
    return S_OK;
}

static HRESULT WINAPI tl_get_all_cust_data(ITypeLib2 *self, CUSTDATA *data)
{
    return TlCustAll(lib_of(self)->Cust, data);
}

const ITypeLib2Vtbl g_typelib_vtbl = {
    tl_query, tl_addref, tl_release, tl_get_type_info_count, tl_get_type_info, tl_get_type_info_type,
    tl_get_type_info_of_guid, tl_get_lib_attr, tl_get_type_comp, tl_get_documentation, tl_is_name, tl_find_name,
    tl_release_tlib_attr, tl_get_cust_data, tl_get_lib_statistics, tl_get_documentation2, tl_get_all_cust_data};

/* ------------------------------------------------------------------ library ITypeComp */

static HRESULT WINAPI lc_query(ITypeComp *self, REFIID iid, void **out)
{
    return TlLibQuery(lib_of_comp(self), iid, out);
}
static ULONG WINAPI lc_addref(ITypeComp *self)
{
    return InterlockedIncrement(&lib_of_comp(self)->Refs);
}
static ULONG WINAPI lc_release(ITypeComp *self)
{
    return tl_release((ITypeLib2 *)lib_of_comp(self));
}

HRESULT TlBindName(TYPEINFO *info, LPCOLESTR name, WORD flags, ITypeInfo **found, DESCKIND *kind, BINDPTR *bind,
                   int depth);

/* binds names usable without qualifying them by type: members of modules and enumerations. */
static HRESULT WINAPI lc_bind(ITypeComp *self, LPOLESTR name, ULONG hash, WORD flags, ITypeInfo **found,
                              DESCKIND *kind, BINDPTR *bind)
{
    TL_LIB *lib = lib_of_comp(self);
    if (!name || !found || !kind || !bind)
        return E_INVALIDARG;
    *found = nullptr;
    *kind = DESCKIND_NONE;
    memset(bind, 0, sizeof(*bind));
    /* a type name binds as a scope. */
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        TL_TYPE *t = lib->Types[i];
        const WCHAR *n = TlTypeName(t);
        if (!n || _wcsicmp(n, name))
            continue;
        if (t->Kind == TKIND_ENUM || t->Kind == TKIND_MODULE || t->Kind == TKIND_COCLASS || t->Kind == TKIND_RECORD ||
            t->Kind == TKIND_UNION || t->Kind == TKIND_INTERFACE || t->Kind == TKIND_DISPATCH || t->Kind == TKIND_ALIAS) {
            ITypeInfo *info;
            HRESULT hr = TlGetTypeInfo(lib, i, FALSE, &info);
            if (FAILED(hr))
                return hr;
            ITypeComp *comp;
            hr = info->lpVtbl->GetTypeComp(info, &comp);
            if (FAILED(hr)) {
                info->lpVtbl->Release(info);
                return hr;
            }
            *found = info;
            bind->lptcomp = comp;
            *kind = DESCKIND_TYPECOMP;
            return S_OK;
        }
    }
    /* functions, variables and constants of modules and enumerations. */
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        TL_TYPE *t = lib->Types[i];
        if (t->Kind != TKIND_MODULE && t->Kind != TKIND_ENUM)
            continue;
        ITypeInfo *info;
        HRESULT hr = TlGetTypeInfo(lib, i, FALSE, &info);
        if (FAILED(hr))
            return hr;
        ITypeComp *comp;
        hr = info->lpVtbl->GetTypeComp(info, &comp);
        if (SUCCEEDED(hr)) {
            hr = comp->lpVtbl->Bind(comp, name, hash, flags, found, kind, bind);
            comp->lpVtbl->Release(comp);
        }
        info->lpVtbl->Release(info);
        if (FAILED(hr))
            return hr;
        if (*kind != DESCKIND_NONE)
            return S_OK;
    }
    return S_OK;
}

static HRESULT WINAPI lc_bind_type(ITypeComp *self, LPOLESTR name, ULONG hash, ITypeInfo **info, ITypeComp **comp)
{
    TL_LIB *lib = lib_of_comp(self);
    if (!name || !info || !comp)
        return E_INVALIDARG;
    *info = nullptr;
    *comp = nullptr;
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        const WCHAR *n = TlTypeName(lib->Types[i]);
        if (n && !_wcsicmp(n, name)) {
            HRESULT hr = TlGetTypeInfo(lib, i, FALSE, info);
            if (FAILED(hr))
                return hr;
            return (*info)->lpVtbl->GetTypeComp(*info, comp);
        }
    }
    return S_OK;
}

const ITypeCompVtbl g_typelib_comp_vtbl = {lc_query, lc_addref, lc_release, lc_bind, lc_bind_type};

/* ------------------------------------------------------------------ imported libraries */

static HRESULT import_lib(TL_LIB *lib, ULONG index, ITypeLib **out)
{
    EnterCriticalSection(&lib->Lock);
    if (!lib->ImportedLibs) {
        lib->ImportedLibs = TlAlloc(lib, (SIZE_T)lib->ImportCount * sizeof(ITypeLib *));
        if (!lib->ImportedLibs) {
            LeaveCriticalSection(&lib->Lock);
            return E_OUTOFMEMORY;
        }
    }
    ITypeLib *loaded = lib->ImportedLibs[index];
    LeaveCriticalSection(&lib->Lock);
    if (loaded) {
        loaded->lpVtbl->AddRef(loaded);
        *out = loaded;
        return S_OK;
    }
    TL_IMPORT *imp = &lib->Imports[index];
    HRESULT hr = LoadRegTypeLib(&imp->Lib, imp->Major, imp->Minor, imp->Lcid, &loaded);
    if (FAILED(hr) && imp->File) {
        /* try the importing library's directory first. */
        WCHAR path[MAX_PATH];
        path[0] = 0;
        if (lib->Path) {
            copy_wide(path, lib->Path, MAX_PATH);
            WCHAR *slash = wcsrchr(path, L'\\');
            if (slash)
                slash[1] = 0;
            else
                path[0] = 0;
        }
        if (wcslen(path) + wcslen(imp->File) < MAX_PATH) {
            wcscat(path, imp->File);
            hr = LoadTypeLibEx(path, REGKIND_NONE, &loaded);
        }
        if (FAILED(hr))
            hr = LoadTypeLibEx(imp->File, REGKIND_NONE, &loaded);
    }
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&lib->Lock);
    if (lib->ImportedLibs[index]) {
        loaded->lpVtbl->Release(loaded);
        loaded = lib->ImportedLibs[index];
    } else {
        lib->ImportedLibs[index] = loaded;
    }
    loaded->lpVtbl->AddRef(loaded);
    LeaveCriticalSection(&lib->Lock);
    *out = loaded;
    return S_OK;
}

ITypeInfo *TlInfoVtableHalf(ITypeInfo *info);

HRESULT TlResolveHref(TL_TYPE *from, HREFTYPE href, ITypeInfo **out)
{
    TL_LIB *lib = from->Lib;
    ULONG raw = href & 0xFFFFFF;
    *out = nullptr;
    if (raw & 1) {
        ULONG offset = raw & ~(ULONG)3;
        if (offset % 12 || offset / 12 >= lib->ImportCount)
            return TYPE_E_ELEMENTNOTFOUND;
        TL_IMPORT *imp = &lib->Imports[offset / 12];
        ITypeLib *other;
        HRESULT hr = import_lib(lib, offset / 12, &other);
        if (FAILED(hr))
            return hr;
        ITypeInfo *info;
        if (imp->ByGuid) {
            hr = other->lpVtbl->GetTypeInfoOfGuid(other, &imp->Type, &info);
        } else {
            hr = other->lpVtbl->GetTypeInfo(other, imp->Index, &info);
            if (SUCCEEDED(hr)) {
                TYPEKIND kind;
                if (FAILED(other->lpVtbl->GetTypeInfoType(other, imp->Index, &kind)) ||
                    (kind != imp->Kind && imp->Kind != TKIND_INTERFACE && imp->Kind != TKIND_DISPATCH)) {
                    info->lpVtbl->Release(info);
                    hr = TYPE_E_CANTLOADLIBRARY;
                }
            }
        }
        other->lpVtbl->Release(other);
        if (FAILED(hr))
            return hr;
        if (href & 2) {
            ITypeInfo *half = TlInfoVtableHalf(info);
            if (half) {
                info->lpVtbl->Release(info);
                info = half;
            }
        }
        *out = info;
        return S_OK;
    }
    ULONG index;
    if (!TlIndexOfHref(raw, &index) || (raw & 0x7C))
        return TYPE_E_ELEMENTNOTFOUND;
    return TlGetTypeInfo(lib, index, (href & 2) != 0, out);
}

/* ------------------------------------------------------------------ files and resources */

typedef struct {
    HANDLE Mapping;
    HMODULE Module;
    BYTE *Memory;
} IMAGE_HOLD;

static void release_image(void *context, const BYTE *image)
{
    IMAGE_HOLD *h = context;
    if (!h)
        return;
    if (h->Memory)
        UnmapViewOfFile(h->Memory);
    if (h->Mapping)
        CloseHandle(h->Mapping);
    if (h->Module)
        FreeLibrary(h->Module);
    AutFree(h);
}

/* a trailing \N names the type library resource N of a program. */
static BOOL split_resource_suffix(WCHAR *path, int *id)
{
    size_t length = wcslen(path);
    size_t i = length;
    while (i > 0 && path[i - 1] >= L'0' && path[i - 1] <= L'9')
        i--;
    if (i == length || i == 0 || path[i - 1] != L'\\')
        return FALSE;
    /* the prefix must be a file: "dir\123" with a directory 123 is not a resource. */
    int value = 0;
    for (size_t k = i; k < length; k++)
        value = value * 10 + (path[k] - L'0');
    WCHAR saved = path[i - 1];
    path[i - 1] = 0;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        path[i - 1] = saved;
        return FALSE;
    }
    *id = value;
    return TRUE;
}

static BOOL looks_like_path(const WCHAR *file)
{
    return wcschr(file, L'\\') || wcschr(file, L'/') || (file[0] && file[1] == L':');
}

/* the path as given, or found on the system search path. */
static HRESULT resolve_path(LPCOLESTR file, WCHAR *full, int *resource_id)
{
    WCHAR path[MAX_PATH];
    if (!*file)
        return STG_E_FILENOTFOUND;
    if (wcslen(file) >= MAX_PATH)
        return E_INVALIDARG;
    wcscpy(path, file);
    *resource_id = -1;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        int id;
        WCHAR trimmed[MAX_PATH];
        wcscpy(trimmed, path);
        if (split_resource_suffix(trimmed, &id)) {
            wcscpy(path, trimmed);
            *resource_id = id;
        }
    }
    if (looks_like_path(path)) {
        DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH)
            return TYPE_E_CANTLOADLIBRARY;
        if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
            return TYPE_E_CANTLOADLIBRARY;
        return S_OK;
    }
    DWORD n = SearchPathW(nullptr, path, nullptr, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH) {
        n = SearchPathW(nullptr, path, L".tlb", MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH)
            return TYPE_E_CANTLOADLIBRARY;
    }
    return S_OK;
}

static HRESULT map_file(const WCHAR *path, IMAGE_HOLD **hold, const BYTE **image, SIZE_T *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return TYPE_E_CANTLOADLIBRARY;
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 1 || length.QuadPart > 0x7FFFFFFF) {
        CloseHandle(file);
        return TYPE_E_CANTLOADLIBRARY;
    }
    IMAGE_HOLD *h = AutAllocZero(sizeof(*h));
    if (!h) {
        CloseHandle(file);
        return E_OUTOFMEMORY;
    }
    h->Mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (h->Mapping)
        h->Memory = MapViewOfFile(h->Mapping, FILE_MAP_READ, 0, 0, 0);
    if (!h->Memory) {
        release_image(h, nullptr);
        return TYPE_E_CANTLOADLIBRARY;
    }
    *hold = h;
    *image = h->Memory;
    *size = (SIZE_T)length.QuadPart;
    return S_OK;
}

/* the TYPELIB resource numbered id, or the first one. */
static HRESULT load_resource(const WCHAR *path, int id, IMAGE_HOLD **hold, const BYTE **image, SIZE_T *size)
{
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module)
        return TYPE_E_CANTLOADLIBRARY;
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id < 0 ? 1 : id), L"TYPELIB");
    if (!found && id < 0) {
        /* any number will do. */
        found = nullptr;
    }
    if (!found) {
        FreeLibrary(module);
        return TYPE_E_CANTLOADLIBRARY;
    }
    HGLOBAL data = LoadResource(module, found);
    DWORD length = SizeofResource(module, found);
    const BYTE *bytes = data ? LockResource(data) : nullptr;
    if (!bytes || !length) {
        FreeLibrary(module);
        return TYPE_E_CANTLOADLIBRARY;
    }
    IMAGE_HOLD *h = AutAllocZero(sizeof(*h));
    if (!h) {
        FreeLibrary(module);
        return E_OUTOFMEMORY;
    }
    h->Module = module;
    *hold = h;
    *image = bytes;
    *size = length;
    return S_OK;
}

HRESULT TlLoadFile(LPCOLESTR file, SYSKIND kind, TL_LIB **result)
{
    WCHAR full[MAX_PATH];
    int resource_id;
    *result = nullptr;
    HRESULT hr = resolve_path(file, full, &resource_id);
    if (FAILED(hr))
        return hr;
    /* 64-bit windows only ships stdole32.tlb's contents as stdole2.tlb. */
    const WCHAR *name = wcsrchr(full, L'\\');
    WCHAR stdole[MAX_PATH];
    if (name && !_wcsicmp(name + 1, L"stdole32.tlb") && sizeof(void *) == 8) {
        wcscpy(stdole, full);
        wcscpy(stdole + (name - full) + 1, L"stdole2.tlb");
        if (GetFileAttributesW(stdole) != INVALID_FILE_ATTRIBUTES)
            wcscpy(full, stdole);
    }
    SYSKIND key = kind;
    TL_LIB *cached = cache_find(full, key);
    if (cached) {
        *result = cached;
        return S_OK;
    }
    IMAGE_HOLD *hold = nullptr;
    const BYTE *image = nullptr;
    SIZE_T size = 0;
    hr = map_file(full, &hold, &image, &size);
    if (FAILED(hr))
        return hr;
    if (size >= 2 && image[0] == 'M' && image[1] == 'Z') {
        release_image(hold, image);
        hold = nullptr;
        hr = load_resource(full, resource_id, &hold, &image, &size);
        if (FAILED(hr))
            return hr;
    } else if (size < 4 || (memcmp(image, "MSFT", 4) && memcmp(image, "SLTG", 4))) {
        /* neither a PE image nor a type library. */
        release_image(hold, image);
        return TYPE_E_CANTLOADLIBRARY;
    }
    TL_LIB *lib;
    hr = TlReadImage(image, size, kind == (SYSKIND)-1 ? (SYSKIND)0 : kind, release_image, hold, &lib);
    if (FAILED(hr)) {
        release_image(hold, image);
        return hr;
    }
    lib->Path = TlStrDup(lib, full);
    cache_add(lib, full, key);
    *result = lib;
    return S_OK;
}

static SYSKIND syskind_of_flags(REGKIND kind)
{
    SYSKIND native = sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32;
    if (((int)kind & 0x60) == 0x60)
        return (SYSKIND)-1;
    if ((int)kind & 0x20)
        return SYS_WIN32;
    if ((int)kind & 0x40)
        return SYS_WIN64;
    return native;
}

WINOLEAUTAPI HRESULT WINAPI LoadTypeLibEx(LPCOLESTR file, REGKIND regkind, ITypeLib **out)
{
    if (!file || !out)
        return E_INVALIDARG;
    int flags = (int)regkind & 0x60;
    REGKIND mode = (REGKIND)((int)regkind & ~0x60);
    if ((int)mode > REGKIND_NONE)
        return E_INVALIDARG;
    *out = nullptr;
    if (!*file)
        return STG_E_FILENOTFOUND;
    TL_LIB *lib;
    HRESULT hr = TlLoadFile(file, syskind_of_flags((REGKIND)flags), &lib);
    if (FAILED(hr))
        return hr;
    if (mode == REGKIND_REGISTER || (mode == REGKIND_DEFAULT && !looks_like_path(file)))
        RegisterTypeLib((ITypeLib *)lib, lib->Path, nullptr);
    *out = (ITypeLib *)lib;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI LoadTypeLib(LPCOLESTR file, ITypeLib **out)
{
    return LoadTypeLibEx(file, REGKIND_REGISTER, out);
}

/* ------------------------------------------------------------------ registry */

static void format_version(WORD major, WORD minor, WCHAR *out)
{
    wsprintfW(out, L"%x.%x", major, minor);
}

static void format_lcid(LCID lcid, WCHAR *out)
{
    wsprintfW(out, L"%x", lcid);
}

static const WCHAR *syskind_name(SYSKIND kind)
{
    switch (kind) {
    case SYS_WIN16: return L"win16";
    case SYS_WIN32: return L"win32";
    case SYS_WIN64: return L"win64";
    default: return nullptr;
    }
}

static LSTATUS set_value(HKEY root, REGSAM view, const WCHAR *subkey, const WCHAR *name, const WCHAR *value)
{
    HKEY key;
    LSTATUS status = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (status)
        return status;
    status = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return status;
}

static LSTATUS set_default(HKEY root, const WCHAR *subkey, const WCHAR *name, const WCHAR *value)
{
    return set_value(root, 0, subkey, name, value);
}

static HRESULT register_lib(ITypeLib *lib, LPCOLESTR path, LPCOLESTR help_dir, HKEY root)
{
    if (!lib || !path)
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    if (wcslen(path) >= MAX_PATH)
        return E_INVALIDARG;
    wcscpy(full, path);
    TLIBATTR *attr;
    HRESULT hr = lib->lpVtbl->GetLibAttr(lib, &attr);
    if (FAILED(hr))
        return hr;
    const WCHAR *platform = syskind_name(attr->syskind);
    if (!platform) {
        lib->lpVtbl->ReleaseTLibAttr(lib, attr);
        return TYPE_E_UNSUPFORMAT;
    }
    WCHAR guid[40], version[16], lcid[16], key[260];
    StringFromGUID2(&attr->guid, guid, 40);
    format_version(attr->wMajorVerNum, attr->wMinorVerNum, version);
    format_lcid(attr->lcid, lcid);
    WORD flags = attr->wLibFlags & ~LIBFLAG_FHASDISKIMAGE;
    GUID libid = attr->guid;
    SYSKIND syskind = attr->syskind;
    lib->lpVtbl->ReleaseTLibAttr(lib, attr);

    BSTR doc = nullptr, name = nullptr;
    lib->lpVtbl->GetDocumentation(lib, -1, &name, &doc, nullptr, nullptr);
    const WCHAR *label = doc ? doc : name ? name : L"";
    wsprintfW(key, L"TypeLib\\%ls\\%ls", guid, version);
    hr = TYPE_E_REGISTRYACCESS;
    WCHAR sub[300];
    if (set_default(root, key, nullptr, label))
        goto done;
    wsprintfW(sub, L"%ls\\FLAGS", key);
    WCHAR hex[16];
    wsprintfW(hex, L"%x", flags);
    if (set_default(root, sub, nullptr, hex))
        goto done;
    wsprintfW(sub, L"%ls\\%ls\\%ls", key, lcid, platform);
    if (set_default(root, sub, nullptr, full))
        goto done;
    /* the help directory given, else the library's own unless one is already registered. */
    wsprintfW(sub, L"%ls\\HELPDIR", key);
    HKEY existing;
    BOOL has_help = !help_dir && !RegOpenKeyExW(root, sub, 0, KEY_READ, &existing);
    if (has_help)
        RegCloseKey(existing);
    if (!has_help) {
        WCHAR dir[MAX_PATH];
        if (help_dir) {
            copy_wide(dir, help_dir, MAX_PATH);
        } else {
            wcscpy(dir, full);
            WCHAR *slash = wcsrchr(dir, L'\\');
            if (slash)
                *slash = 0;
            else
                dir[0] = 0;
        }
        if (set_default(root, sub, nullptr, dir))
            goto done;
    }
    hr = S_OK;
    /* interfaces automation marshals through the type library. */
    UINT count = lib->lpVtbl->GetTypeInfoCount(lib);
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        TYPEKIND kind;
        if (FAILED(lib->lpVtbl->GetTypeInfoType(lib, i, &kind)) || (kind != TKIND_INTERFACE && kind != TKIND_DISPATCH))
            continue;
        ITypeInfo *info;
        if (FAILED(lib->lpVtbl->GetTypeInfo(lib, i, &info)))
            continue;
        TYPEATTR *ta;
        if (FAILED(info->lpVtbl->GetTypeAttr(info, &ta))) {
            info->lpVtbl->Release(info);
            continue;
        }
        GUID iid = ta->guid;
        WORD tflags = ta->wTypeFlags;
        info->lpVtbl->ReleaseTypeAttr(info, ta);
        info->lpVtbl->Release(info);
        const WCHAR *proxy;
        if (tflags & (TYPEFLAG_FOLEAUTOMATION | TYPEFLAG_FDUAL))
            proxy = CLSID_PSOA;
        else if (kind == TKIND_INTERFACE)
            continue;
        else
            proxy = CLSID_PSDISPATCH;
        if (tflags & TYPEFLAG_FPROXY)
            proxy = nullptr;
        BSTR type_name = nullptr;
        lib->lpVtbl->GetDocumentation(lib, i, &type_name, nullptr, nullptr, nullptr);
        WCHAR iid_text[40], ikey[100];
        StringFromGUID2(&iid, iid_text, 40);
        wsprintfW(ikey, L"Interface\\%ls", iid_text);
        /* in the view of the program, then in the 32-bit one, which a 32-bit program has as its own. */
        for (int pass = 0; pass < 2; pass++) {
            REGSAM view = pass ? KEY_WOW64_32KEY : 0;
            if (set_value(root, view, ikey, nullptr, type_name ? type_name : L"")) {
                hr = TYPE_E_REGISTRYACCESS;
                continue;
            }
            WCHAR sk[160];
            if (proxy) {
                wsprintfW(sk, L"%ls\\ProxyStubClsid", ikey);
                if (set_value(root, view, sk, nullptr, proxy))
                    hr = TYPE_E_REGISTRYACCESS;
                wsprintfW(sk, L"%ls\\ProxyStubClsid32", ikey);
                if (set_value(root, view, sk, nullptr, proxy))
                    hr = TYPE_E_REGISTRYACCESS;
            }
            wsprintfW(sk, L"%ls\\TypeLib", ikey);
            if (set_value(root, view, sk, nullptr, guid) || set_value(root, view, sk, L"Version", version))
                hr = TYPE_E_REGISTRYACCESS;
        }
        SysFreeString(type_name);
    }
done:
    SysFreeString(doc);
    SysFreeString(name);
    (void)libid;
    (void)syskind;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI RegisterTypeLib(ITypeLib *lib, LPCOLESTR path, LPCOLESTR help_dir)
{
    HRESULT hr = register_lib(lib, path, help_dir, HKEY_CLASSES_ROOT);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI RegisterTypeLibForUser(ITypeLib *lib, LPCOLESTR path, LPCOLESTR help_dir)
{
    HKEY classes;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                     &classes, nullptr);
    if (status)
        return TYPE_E_REGISTRYACCESS;
    HRESULT hr = register_lib(lib, path, help_dir, classes);
    RegCloseKey(classes);
    return hr;
}

static BOOL key_is_empty(HKEY key)
{
    DWORD subkeys = 0, values = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
                         nullptr, nullptr))
        return FALSE;
    return !subkeys && !values;
}

static HRESULT unregister_lib(REFGUID guid, WORD major, WORD minor, LCID lcid, SYSKIND syskind, HKEY root)
{
    const WCHAR *platform = syskind_name(syskind);
    if (!platform)
        return E_INVALIDARG;
    WCHAR g[40], version[16], l[16], path[260];
    StringFromGUID2(guid, g, 40);
    format_version(major, minor, version);
    format_lcid(lcid, l);
    wsprintfW(path, L"TypeLib\\%ls\\%ls\\%ls\\%ls", g, version, l, platform);
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key))
        return TYPE_E_REGISTRYACCESS;
    RegCloseKey(key);
    if (RegDeleteTreeW(root, path))
        return TYPE_E_REGISTRYACCESS;
    /* parent keys go once only FLAGS and HELPDIR remain under them. */
    wsprintfW(path, L"TypeLib\\%ls\\%ls\\%ls", g, version, l);
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &key)) {
        BOOL empty = key_is_empty(key);
        RegCloseKey(key);
        if (empty)
            RegDeleteKeyW(root, path);
    }
    wsprintfW(path, L"TypeLib\\%ls\\%ls", g, version);
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &key)) {
        DWORD subkeys = 0;
        WCHAR name[40];
        DWORD size;
        BOOL only_house = TRUE;
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr);
        for (DWORD i = 0; i < subkeys; i++) {
            size = 40;
            if (!RegEnumKeyExW(key, i, name, &size, nullptr, nullptr, nullptr, nullptr)) {
                if (_wcsicmp(name, L"FLAGS") && _wcsicmp(name, L"HELPDIR"))
                    only_house = FALSE;
            }
        }
        RegCloseKey(key);
        if (only_house)
            RegDeleteTreeW(root, path);
    }
    wsprintfW(path, L"TypeLib\\%ls", g);
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &key)) {
        BOOL empty = key_is_empty(key);
        RegCloseKey(key);
        if (empty)
            RegDeleteKeyW(root, path);
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI UnRegisterTypeLib(REFGUID guid, WORD major, WORD minor, LCID lcid, SYSKIND syskind)
{
    if (!guid)
        return E_INVALIDARG;
    return unregister_lib(guid, major, minor, lcid, syskind, HKEY_CLASSES_ROOT);
}

WINOLEAUTAPI HRESULT WINAPI UnRegisterTypeLibForUser(REFGUID guid, WORD major, WORD minor, LCID lcid,
                                                     SYSKIND syskind)
{
    HKEY classes;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_ALL_ACCESS, &classes))
        return TYPE_E_REGISTRYACCESS;
    HRESULT hr = unregister_lib(guid, major, minor, lcid, syskind, classes);
    RegCloseKey(classes);
    return hr;
}

/* same major version and the highest minor not below the one asked for. */
static BOOL find_version(HKEY libkey, WORD major, WORD minor, WORD *found_major, WORD *found_minor)
{
    WCHAR name[40];
    BOOL found = FALSE;
    WORD best_minor = 0;
    for (DWORD i = 0;; i++) {
        DWORD size = 40;
        if (RegEnumKeyExW(libkey, i, name, &size, nullptr, nullptr, nullptr, nullptr))
            break;
        WCHAR *dot = wcschr(name, L'.');
        if (!dot)
            continue;
        *dot = 0;
        WCHAR *end;
        ULONG vm = 0, vn = 0;
        for (end = name; *end; end++) {
            int d = *end >= L'0' && *end <= L'9' ? *end - L'0' : (*end | 0x20) >= L'a' && (*end | 0x20) <= L'f' ? (*end | 0x20) - L'a' + 10 : -1;
            if (d < 0)
                break;
            vm = vm * 16 + d;
        }
        if (*end || vm != major)
            continue;
        for (end = dot + 1; *end; end++) {
            int d = *end >= L'0' && *end <= L'9' ? *end - L'0' : (*end | 0x20) >= L'a' && (*end | 0x20) <= L'f' ? (*end | 0x20) - L'a' + 10 : -1;
            if (d < 0)
                break;
            vn = vn * 16 + d;
        }
        if (*end || vn < minor)
            continue;
        if (!found || vn > best_minor || vn == minor) {
            if (vn == minor) {
                *found_major = (WORD)vm;
                *found_minor = (WORD)vn;
                return TRUE;
            }
            found = TRUE;
            best_minor = (WORD)vn;
            *found_major = (WORD)vm;
            *found_minor = (WORD)vn;
        }
    }
    return found;
}

/* tries the exact language, then the primary language, then neutral. */
static HRESULT registered_path(HKEY classes, REFGUID guid, WORD major, WORD minor, LCID lcid, SYSKIND syskind,
                               BSTR *path)
{
    WCHAR g[40], key[200];
    StringFromGUID2(guid, g, 40);
    wsprintfW(key, L"TypeLib\\%ls", g);
    HKEY libkey;
    if (RegOpenKeyExW(classes, key, 0, KEY_READ, &libkey))
        return TYPE_E_LIBNOTREGISTERED;
    WORD vm, vn;
    if (!find_version(libkey, major, minor, &vm, &vn)) {
        RegCloseKey(libkey);
        return TYPE_E_LIBNOTREGISTERED;
    }
    RegCloseKey(libkey);
    WCHAR version[16];
    format_version(vm, vn, version);
    LCID tries[3] = {lcid, lcid & 0x3FF, 0};
    const WCHAR *platform = syskind_name(syskind);
    for (int i = 0; i < 3; i++) {
        if (i && tries[i] == tries[i - 1])
            continue;
        WCHAR l[16], sub[300], value[MAX_PATH];
        format_lcid(tries[i], l);
        DWORD size = sizeof(value);
        wsprintfW(sub, L"TypeLib\\%ls\\%ls\\%ls\\%ls", g, version, l, platform);
        if (!RegGetValueW(classes, sub, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) && value[0]) {
            *path = SysAllocString(value);
            return *path ? S_OK : E_OUTOFMEMORY;
        }
    }
    return TYPE_E_LIBNOTREGISTERED;
}

WINOLEAUTAPI HRESULT WINAPI QueryPathOfRegTypeLib(REFGUID guid, USHORT major, USHORT minor, LCID lcid, BSTR *path)
{
    if (!guid || !path)
        return E_INVALIDARG;
    *path = nullptr;
    SYSKIND native = sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32;
    HRESULT hr = registered_path(HKEY_CLASSES_ROOT, guid, major, minor, lcid, native, path);
    if (FAILED(hr) && native == SYS_WIN64)
        hr = registered_path(HKEY_CLASSES_ROOT, guid, major, minor, lcid, SYS_WIN32, path);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI LoadRegTypeLib(REFGUID guid, WORD major, WORD minor, LCID lcid, ITypeLib **lib)
{
    if (!guid || !lib)
        return E_INVALIDARG;
    *lib = nullptr;
    BSTR path;
    HRESULT hr = QueryPathOfRegTypeLib(guid, major, minor, lcid, &path);
    if (FAILED(hr))
        return hr;
    hr = LoadTypeLibEx(path, REGKIND_NONE, lib);
    SysFreeString(path);
    return hr;
}
