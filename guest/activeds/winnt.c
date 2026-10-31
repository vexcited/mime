/*
 * the WinNT provider: the local machine as a computer with its local groups and users. a group can be made, given a
 * description, filled with members and deleted, which goes to the account calls of netapi32. objects answer through
 * IDispatch by name too, which is how scripts and the managed directory classes call Add and IsMember.
 */
#include <rtl.h>
#include "activeds.h"

typedef DWORD NET_API_STATUS;

NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buffer);
NET_API_STATUS WINAPI NetLocalGroupAdd(LPCWSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter);
NET_API_STATUS WINAPI NetLocalGroupDel(LPCWSTR server, LPCWSTR group);
NET_API_STATUS WINAPI NetLocalGroupGetInfo(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE *buffer);
NET_API_STATUS WINAPI NetLocalGroupSetInfo(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE buffer, LPDWORD parameter);
NET_API_STATUS WINAPI NetLocalGroupEnum(LPCWSTR server, DWORD level, LPBYTE *buffer, DWORD preferred, LPDWORD entries,
                                        LPDWORD total, PDWORD_PTR resume);
NET_API_STATUS WINAPI NetLocalGroupGetMembers(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE *buffer, DWORD preferred,
                                              LPDWORD entries, LPDWORD total, PDWORD_PTR resume);
NET_API_STATUS WINAPI NetLocalGroupAddMembers(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE buffer, DWORD count);
NET_API_STATUS WINAPI NetLocalGroupDelMembers(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE buffer, DWORD count);
NET_API_STATUS WINAPI NetUserAdd(LPCWSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter);
NET_API_STATUS WINAPI NetUserDel(LPCWSTR server, LPCWSTR user);
NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buffer);
NET_API_STATUS WINAPI NetUserEnum(LPCWSTR server, DWORD level, DWORD filter, LPBYTE *buffer, DWORD preferred,
                                  LPDWORD entries, LPDWORD total, PDWORD_PTR resume);
BOOL WINAPI LookupAccountNameW(LPCWSTR system, LPCWSTR name, PVOID sid, LPDWORD sid_size, LPWSTR domain,
                               LPDWORD domain_size, PDWORD use);

#define WORKGROUP L"WORKGROUP"
#define SID_TYPE_USER 1
#define ERROR_NONE_MAPPED_ 1332

/* ------------------------------------------------------------------ objects */

typedef enum KIND { K_COMPUTER, K_GROUP, K_USER, K_MEMBERS } KIND;

typedef struct ITEM {
    KIND kind;
    WCHAR *domain;
    WCHAR *name;
} ITEM;

typedef struct OBJ OBJ;

typedef struct IFACE {
    const void *lpVtbl;
    OBJ *obj;
} IFACE;

struct OBJ {
    LONG ref;
    KIND kind;
    BOOL pending, dirty;
    WCHAR *name, *path, *parent;
    WCHAR *description;
    WCHAR **filter;
    DWORD filter_count;
    ITEM *items;
    DWORD item_count;
    IFACE face[3];
};

#define OBJ_OF(this) (((IFACE *)(this))->obj)

static void *mem(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void release_mem(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

static WCHAR *dup(const WCHAR *text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = mem(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

static WCHAR *dupn(const WCHAR *text, SIZE_T count)
{
    WCHAR *copy = mem((count + 1) * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, count * sizeof(WCHAR));
    return copy;
}

static WCHAR g_computer[MAX_COMPUTERNAME_LENGTH + 1];

static const WCHAR *computer_name(void)
{
    if (!g_computer[0]) {
        DWORD n = RTL_NUMBER_OF(g_computer);
        if (!GetComputerNameW(g_computer, &n))
            wcscpy(g_computer, L"MIME");
    }
    return g_computer;
}

static BOOL is_local(const WCHAR *machine)
{
    return !_wcsicmp(machine, L".") || !_wcsicmp(machine, L"localhost") || !_wcsicmp(machine, computer_name());
}

static void free_items(ITEM *items, DWORD count)
{
    for (DWORD i = 0; items && i < count; i++) {
        release_mem(items[i].domain);
        release_mem(items[i].name);
    }
    release_mem(items);
}

static void free_filter(OBJ *o)
{
    for (DWORD i = 0; i < o->filter_count; i++)
        release_mem(o->filter[i]);
    release_mem(o->filter);
    o->filter = nullptr;
    o->filter_count = 0;
}

static LONG obj_release(OBJ *o)
{
    LONG ref = InterlockedDecrement(&o->ref);
    if (!ref) {
        release_mem(o->name);
        release_mem(o->path);
        release_mem(o->parent);
        release_mem(o->description);
        free_filter(o);
        free_items(o->items, o->item_count);
        release_mem(o);
    }
    return ref;
}

static const WCHAR *class_text(KIND kind)
{
    static const WCHAR *const names[] = {L"Computer", L"Group", L"User", L"Members"};
    return names[kind];
}

/* ------------------------------------------------------------------ vtables */

#define DECLARE_UNKNOWN_AND_DISPATCH(T)                                                                                \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        return obj_query(OBJ_OF(This), riid, object);                                                                  \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return (ULONG)InterlockedIncrement(&OBJ_OF(This)->ref);                                                        \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return (ULONG)obj_release(OBJ_OF(This));                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTypeInfoCount(T *This, UINT *count)                                        \
    {                                                                                                                  \
        *count = 0;                                                                                                    \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTypeInfo(T *This, UINT index, LCID lcid, ITypeInfo **info)                 \
    {                                                                                                                  \
        *info = nullptr;                                                                                               \
        return DISP_E_BADINDEX;                                                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetIDsOfNames(T *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,  \
                                                       DISPID *ids)                                                    \
    {                                                                                                                  \
        return obj_ids(names, count, ids);                                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_Invoke(T *This, DISPID id, REFIID riid, LCID lcid, WORD flags,               \
                                                DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err) \
    {                                                                                                                  \
        return obj_invoke(OBJ_OF(This), id, flags, params, result);                                                    \
    }

#define INIT_UNKNOWN_AND_DISPATCH(T)                                                                                   \
    .QueryInterface = T##_QueryInterface, .AddRef = T##_AddRef, .Release = T##_Release,                                \
    .GetTypeInfoCount = T##_GetTypeInfoCount, .GetTypeInfo = T##_GetTypeInfo, .GetIDsOfNames = T##_GetIDsOfNames,      \
    .Invoke = T##_Invoke

static HRESULT obj_query(OBJ *o, REFIID riid, void **object);
static HRESULT obj_ids(LPOLESTR *names, UINT count, DISPID *ids);
static HRESULT obj_invoke(OBJ *o, DISPID id, WORD flags, DISPPARAMS *params, VARIANT *result);

/* the operations the interfaces and the dispatch table share. */
static HRESULT op_string(OBJ *o, int which, BSTR *value);
static HRESULT op_get_info(OBJ *o);
static HRESULT op_set_info(OBJ *o);
static HRESULT op_get(OBJ *o, const WCHAR *name, VARIANT *value);
static HRESULT op_put(OBJ *o, const WCHAR *name, const VARIANT *value);
static HRESULT op_get_ex(OBJ *o, const WCHAR *name, VARIANT *value);
static HRESULT op_put_ex(OBJ *o, LONG control, const WCHAR *name, const VARIANT *value);
static HRESULT op_count(OBJ *o, LONG *count);
static HRESULT op_get_filter(OBJ *o, VARIANT *filter);
static HRESULT op_put_filter(OBJ *o, const VARIANT *filter);
static HRESULT op_get_object(OBJ *o, const WCHAR *class_name, const WCHAR *name, IDispatch **object);
static HRESULT op_create(OBJ *o, const WCHAR *class_name, const WCHAR *name, IDispatch **object);
static HRESULT op_delete(OBJ *o, const WCHAR *class_name, const WCHAR *name);
static HRESULT op_new_enum(OBJ *o, IUnknown **enumerator);
static HRESULT op_get_description(OBJ *o, BSTR *value);
static HRESULT op_put_description(OBJ *o, const WCHAR *value);
static HRESULT op_members(OBJ *o, IDispatch **members);
static HRESULT op_is_member(OBJ *o, const WCHAR *member, VARIANT_BOOL *result);
static HRESULT op_change_member(OBJ *o, const WCHAR *member, BOOL add);

DECLARE_UNKNOWN_AND_DISPATCH(IADs)
DECLARE_UNKNOWN_AND_DISPATCH(IADsContainer)
DECLARE_UNKNOWN_AND_DISPATCH(IADsMembers)
DECLARE_UNKNOWN_AND_DISPATCH(IADsGroup)

#define IADS_METHODS_IMPL(T)                                                                                           \
    static HRESULT STDMETHODCALLTYPE T##_get_Name(T *This, BSTR *value) { return op_string(OBJ_OF(This), 0, value); }  \
    static HRESULT STDMETHODCALLTYPE T##_get_Class(T *This, BSTR *value) { return op_string(OBJ_OF(This), 1, value); } \
    static HRESULT STDMETHODCALLTYPE T##_get_GUID(T *This, BSTR *value) { return op_string(OBJ_OF(This), 2, value); }  \
    static HRESULT STDMETHODCALLTYPE T##_get_ADsPath(T *This, BSTR *value)                                             \
    {                                                                                                                  \
        return op_string(OBJ_OF(This), 3, value);                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_get_Parent(T *This, BSTR *value)                                              \
    {                                                                                                                  \
        return op_string(OBJ_OF(This), 4, value);                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_get_Schema(T *This, BSTR *value)                                              \
    {                                                                                                                  \
        return op_string(OBJ_OF(This), 5, value);                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetInfo(T *This) { return op_get_info(OBJ_OF(This)); }                        \
    static HRESULT STDMETHODCALLTYPE T##_SetInfo(T *This) { return op_set_info(OBJ_OF(This)); }                        \
    static HRESULT STDMETHODCALLTYPE T##_Get(T *This, BSTR name, VARIANT *value)                                       \
    {                                                                                                                  \
        return op_get(OBJ_OF(This), name, value);                                                                      \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_Put(T *This, BSTR name, VARIANT value)                                        \
    {                                                                                                                  \
        return op_put(OBJ_OF(This), name, &value);                                                                     \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetEx(T *This, BSTR name, VARIANT *value)                                     \
    {                                                                                                                  \
        return op_get_ex(OBJ_OF(This), name, value);                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_PutEx(T *This, LONG control, BSTR name, VARIANT value)                        \
    {                                                                                                                  \
        return op_put_ex(OBJ_OF(This), control, name, &value);                                                         \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetInfoEx(T *This, VARIANT names, LONG reserved)                              \
    {                                                                                                                  \
        return op_get_info(OBJ_OF(This));                                                                              \
    }

#define IADS_METHODS_INIT(T)                                                                                           \
    .get_Name = T##_get_Name, .get_Class = T##_get_Class, .get_GUID = T##_get_GUID, .get_ADsPath = T##_get_ADsPath,    \
    .get_Parent = T##_get_Parent, .get_Schema = T##_get_Schema, .GetInfo = T##_GetInfo, .SetInfo = T##_SetInfo,        \
    .Get = T##_Get, .Put = T##_Put, .GetEx = T##_GetEx, .PutEx = T##_PutEx, .GetInfoEx = T##_GetInfoEx

IADS_METHODS_IMPL(IADs)
IADS_METHODS_IMPL(IADsGroup)

static const IADsVtbl g_ads_vtbl = {INIT_UNKNOWN_AND_DISPATCH(IADs), IADS_METHODS_INIT(IADs)};

static HRESULT STDMETHODCALLTYPE IADsGroup_get_Description(IADsGroup *This, BSTR *value)
{
    return op_get_description(OBJ_OF(This), value);
}

static HRESULT STDMETHODCALLTYPE IADsGroup_put_Description(IADsGroup *This, BSTR value)
{
    return op_put_description(OBJ_OF(This), value);
}

static HRESULT STDMETHODCALLTYPE IADsGroup_Members(IADsGroup *This, IADsMembers **members)
{
    IDispatch *dispatch;
    HRESULT hr = op_members(OBJ_OF(This), &dispatch);
    if (FAILED(hr))
        return hr;
    hr = dispatch->lpVtbl->QueryInterface(dispatch, &IID_IADsMembers, (void **)members);
    dispatch->lpVtbl->Release(dispatch);
    return hr;
}

static HRESULT STDMETHODCALLTYPE IADsGroup_IsMember(IADsGroup *This, BSTR member, VARIANT_BOOL *result)
{
    return op_is_member(OBJ_OF(This), member, result);
}

static HRESULT STDMETHODCALLTYPE IADsGroup_Add(IADsGroup *This, BSTR member)
{
    return op_change_member(OBJ_OF(This), member, TRUE);
}

static HRESULT STDMETHODCALLTYPE IADsGroup_Remove(IADsGroup *This, BSTR member)
{
    return op_change_member(OBJ_OF(This), member, FALSE);
}

static const IADsGroupVtbl g_group_vtbl = {INIT_UNKNOWN_AND_DISPATCH(IADsGroup),
                                           IADS_METHODS_INIT(IADsGroup),
                                           .get_Description = IADsGroup_get_Description,
                                           .put_Description = IADsGroup_put_Description,
                                           .Members = IADsGroup_Members,
                                           .IsMember = IADsGroup_IsMember,
                                           .Add = IADsGroup_Add,
                                           .Remove = IADsGroup_Remove};

static HRESULT STDMETHODCALLTYPE IADsContainer_get_Count(IADsContainer *This, LONG *count)
{
    return op_count(OBJ_OF(This), count);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_get_Filter(IADsContainer *This, VARIANT *filter)
{
    return op_get_filter(OBJ_OF(This), filter);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_put_Filter(IADsContainer *This, VARIANT filter)
{
    return op_put_filter(OBJ_OF(This), &filter);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_get_Hints(IADsContainer *This, VARIANT *hints)
{
    VariantInit(hints);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE IADsContainer_put_Hints(IADsContainer *This, VARIANT hints)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE IADsContainer_GetObject(IADsContainer *This, BSTR class_name, BSTR name, IDispatch **object)
{
    return op_get_object(OBJ_OF(This), class_name, name, object);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_get__NewEnum(IADsContainer *This, IUnknown **enumerator)
{
    return op_new_enum(OBJ_OF(This), enumerator);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_Create(IADsContainer *This, BSTR class_name, BSTR name, IDispatch **object)
{
    return op_create(OBJ_OF(This), class_name, name, object);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_Delete(IADsContainer *This, BSTR class_name, BSTR name)
{
    return op_delete(OBJ_OF(This), class_name, name);
}

static HRESULT STDMETHODCALLTYPE IADsContainer_CopyHere(IADsContainer *This, BSTR source, BSTR name, IDispatch **object)
{
    *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE IADsContainer_MoveHere(IADsContainer *This, BSTR source, BSTR name, IDispatch **object)
{
    *object = nullptr;
    return E_NOTIMPL;
}

static const IADsContainerVtbl g_container_vtbl = {INIT_UNKNOWN_AND_DISPATCH(IADsContainer),
                                                   .get_Count = IADsContainer_get_Count,
                                                   .get_Filter = IADsContainer_get_Filter,
                                                   .put_Filter = IADsContainer_put_Filter,
                                                   .get_Hints = IADsContainer_get_Hints,
                                                   .put_Hints = IADsContainer_put_Hints,
                                                   .GetObject = IADsContainer_GetObject,
                                                   .get__NewEnum = IADsContainer_get__NewEnum,
                                                   .Create = IADsContainer_Create,
                                                   .Delete = IADsContainer_Delete,
                                                   .CopyHere = IADsContainer_CopyHere,
                                                   .MoveHere = IADsContainer_MoveHere};

static HRESULT STDMETHODCALLTYPE IADsMembers_get_Count(IADsMembers *This, LONG *count)
{
    return op_count(OBJ_OF(This), count);
}

static HRESULT STDMETHODCALLTYPE IADsMembers_get__NewEnum(IADsMembers *This, IUnknown **enumerator)
{
    return op_new_enum(OBJ_OF(This), enumerator);
}

static HRESULT STDMETHODCALLTYPE IADsMembers_get_Filter(IADsMembers *This, VARIANT *filter)
{
    return op_get_filter(OBJ_OF(This), filter);
}

static HRESULT STDMETHODCALLTYPE IADsMembers_put_Filter(IADsMembers *This, VARIANT filter)
{
    return op_put_filter(OBJ_OF(This), &filter);
}

static const IADsMembersVtbl g_members_vtbl = {INIT_UNKNOWN_AND_DISPATCH(IADsMembers),
                                               .get_Count = IADsMembers_get_Count,
                                               .get__NewEnum = IADsMembers_get__NewEnum,
                                               .get_Filter = IADsMembers_get_Filter,
                                               .put_Filter = IADsMembers_put_Filter};

static HRESULT obj_query(OBJ *o, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    IFACE *face = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch))
        face = &o->face[o->kind == K_MEMBERS ? 1 : 0];
    else if (o->kind == K_MEMBERS)
        face = IsEqualIID(riid, &IID_IADsMembers) ? &o->face[1] : nullptr;
    else if (IsEqualIID(riid, &IID_IADs))
        face = &o->face[0];
    else if (IsEqualIID(riid, &IID_IADsContainer))
        face = o->kind == K_COMPUTER ? &o->face[1] : nullptr;
    else if (IsEqualIID(riid, &IID_IADsGroup))
        face = o->kind == K_GROUP ? &o->face[2] : nullptr;
    if (!face)
        return E_NOINTERFACE;
    InterlockedIncrement(&o->ref);
    *object = face;
    return S_OK;
}

/* an object by its kind. a local one is below the computer in the path, the others are named by their domain. */
static OBJ *new_object(KIND kind, const WCHAR *domain, const WCHAR *name)
{
    OBJ *o = mem(sizeof(*o));
    if (!o)
        return nullptr;
    o->ref = 1;
    o->kind = kind;
    o->name = dup(name);
    if (kind == K_COMPUTER) {
        o->parent = dup(L"WinNT://" WORKGROUP);
        o->path = nullptr;
        WCHAR text[300];
        _snwprintf(text, RTL_NUMBER_OF(text), L"WinNT://%ls/%ls", WORKGROUP, name);
        o->path = dup(text);
    } else if (kind != K_MEMBERS) {
        WCHAR text[600], parent[300];
        if (is_local(domain)) {
            _snwprintf(parent, RTL_NUMBER_OF(parent), L"WinNT://%ls/%ls", WORKGROUP, computer_name());
            _snwprintf(text, RTL_NUMBER_OF(text), L"%ls/%ls", parent, name);
        } else {
            _snwprintf(parent, RTL_NUMBER_OF(parent), L"WinNT://%ls", domain);
            _snwprintf(text, RTL_NUMBER_OF(text), L"%ls/%ls", parent, name);
        }
        o->parent = dup(parent);
        o->path = dup(text);
    } else {
        o->path = dup(L"");
        o->parent = dup(L"");
    }
    if (!o->name || !o->path || !o->parent) {
        obj_release(o);
        return nullptr;
    }
    for (int i = 0; i < 3; i++)
        o->face[i].obj = o;
    o->face[0].lpVtbl = &g_ads_vtbl;
    o->face[1].lpVtbl = kind == K_MEMBERS ? (const void *)&g_members_vtbl : (const void *)&g_container_vtbl;
    o->face[2].lpVtbl = &g_group_vtbl;
    return o;
}

static HRESULT object_to_dispatch(OBJ *o, IDispatch **object)
{
    if (!o)
        return E_OUTOFMEMORY;
    HRESULT hr = obj_query(o, &IID_IDispatch, (void **)object);
    obj_release(o);
    return hr;
}

/* ------------------------------------------------------------------ the enumerator */

typedef struct ENUMV {
    const IEnumVARIANTVtbl *lpVtbl;
    LONG ref;
    ITEM *items;
    DWORD count, at;
} ENUMV;

static HRESULT open_item(const ITEM *item, IDispatch **object)
{
    return object_to_dispatch(new_object(item->kind, item->domain, item->name), object);
}

static HRESULT STDMETHODCALLTYPE enumv_query(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        InterlockedIncrement(&((ENUMV *)This)->ref);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enumv_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&((ENUMV *)This)->ref);
}

static ULONG STDMETHODCALLTYPE enumv_release(IEnumVARIANT *This)
{
    ENUMV *e = (ENUMV *)This;
    LONG ref = InterlockedDecrement(&e->ref);
    if (!ref) {
        free_items(e->items, e->count);
        release_mem(e);
    }
    return (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE enumv_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    ENUMV *e = (ENUMV *)This;
    ULONG done = 0;
    while (done < count && e->at < e->count) {
        IDispatch *object;
        HRESULT hr = open_item(&e->items[e->at], &object);
        if (FAILED(hr)) {
            for (ULONG i = 0; i < done; i++)
                VariantClear(&values[i]);
            if (fetched)
                *fetched = 0;
            return hr;
        }
        VariantInit(&values[done]);
        values[done].vt = VT_DISPATCH;
        values[done].pdispVal = object;
        e->at++;
        done++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enumv_skip(IEnumVARIANT *This, ULONG count)
{
    ENUMV *e = (ENUMV *)This;
    e->at = count > e->count - e->at ? e->count : e->at + count;
    return e->at == e->count && count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE enumv_reset(IEnumVARIANT *This)
{
    ((ENUMV *)This)->at = 0;
    return S_OK;
}

static ITEM *copy_items(const ITEM *items, DWORD count)
{
    ITEM *copy = mem((SIZE_T)count * sizeof(ITEM));
    for (DWORD i = 0; copy && i < count; i++) {
        copy[i].kind = items[i].kind;
        copy[i].domain = dup(items[i].domain);
        copy[i].name = dup(items[i].name);
    }
    return copy;
}

static IEnumVARIANT *make_enum(const ITEM *items, DWORD count, DWORD at);

static HRESULT STDMETHODCALLTYPE enumv_clone(IEnumVARIANT *This, IEnumVARIANT **clone)
{
    ENUMV *e = (ENUMV *)This;
    *clone = make_enum(e->items, e->count, e->at);
    return *clone ? S_OK : E_OUTOFMEMORY;
}

static const IEnumVARIANTVtbl g_enum_vtbl = {enumv_query, enumv_add_ref, enumv_release, enumv_next, enumv_skip, enumv_reset, enumv_clone};

static IEnumVARIANT *make_enum(const ITEM *items, DWORD count, DWORD at)
{
    ENUMV *e = mem(sizeof(*e));
    if (!e)
        return nullptr;
    e->lpVtbl = &g_enum_vtbl;
    e->ref = 1;
    e->count = count;
    e->at = at;
    e->items = copy_items(items, count);
    if (!e->items && count) {
        release_mem(e);
        return nullptr;
    }
    return (IEnumVARIANT *)e;
}

/* ------------------------------------------------------------------ the accounts */

static HRESULT win32_error(DWORD status)
{
    return status ? HRESULT_FROM_WIN32(status) : S_OK;
}

static BOOL group_exists(const WCHAR *name, WCHAR *canonical, SIZE_T chars)
{
    LPBYTE info;
    if (NetLocalGroupGetInfo(nullptr, name, 0, &info))
        return FALSE;
    if (canonical)
        _snwprintf(canonical, chars, L"%ls", *(WCHAR **)info);
    NetApiBufferFree(info);
    return TRUE;
}

static BOOL user_exists(const WCHAR *name, WCHAR *canonical, SIZE_T chars)
{
    LPBYTE info;
    if (NetUserGetInfo(nullptr, name, 0, &info))
        return FALSE;
    if (canonical)
        _snwprintf(canonical, chars, L"%ls", *(WCHAR **)info);
    NetApiBufferFree(info);
    return TRUE;
}

static BOOL class_is(const WCHAR *given, const WCHAR *wanted)
{
    return given && !_wcsicmp(given, wanted);
}

/* the local object of that name, of the class when one is given. */
static HRESULT open_local(const WCHAR *class_hint, const WCHAR *name, OBJ **out)
{
    WCHAR found[257];
    BOOL want_user = !class_hint || !*class_hint || class_is(class_hint, L"user");
    BOOL want_group = !class_hint || !*class_hint || class_is(class_hint, L"group");
    if (!want_user && !want_group)
        return E_ADS_UNKNOWN_OBJECT;
    KIND kind;
    if (want_user && user_exists(name, found, RTL_NUMBER_OF(found)))
        kind = K_USER;
    else if (want_group && group_exists(name, found, RTL_NUMBER_OF(found)))
        kind = K_GROUP;
    else
        return win32_error(want_group && !want_user ? 2220 : 2221);
    *out = new_object(kind, computer_name(), found);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ paths */

typedef struct SEGMENTS {
    WCHAR *text[3];
    DWORD count;
    WCHAR *class_hint;
} SEGMENTS;

static void free_segments(SEGMENTS *s)
{
    for (DWORD i = 0; i < s->count; i++)
        release_mem(s->text[i]);
    release_mem(s->class_hint);
    memset(s, 0, sizeof(*s));
}

/* the names between the slashes, with the class a comma puts after the last. */
static HRESULT split_path(const WCHAR *text, SEGMENTS *s)
{
    memset(s, 0, sizeof(*s));
    for (;;) {
        const WCHAR *end = text;
        while (*end && *end != L'/')
            end++;
        if (end == text || s->count == 3) {
            free_segments(s);
            return E_ADS_BAD_PATHNAME;
        }
        s->text[s->count] = dupn(text, (SIZE_T)(end - text));
        if (!s->text[s->count++]) {
            free_segments(s);
            return E_OUTOFMEMORY;
        }
        if (!*end)
            break;
        text = end + 1;
    }
    if (!s->count)
        return E_ADS_BAD_PATHNAME;
    WCHAR *last = s->text[s->count - 1], *comma = wcsrchr(last, L',');
    if (comma) {
        s->class_hint = dup(comma + 1);
        *comma = 0;
        if (!*last) {
            free_segments(s);
            return E_ADS_BAD_PATHNAME;
        }
    }
    return S_OK;
}

HRESULT WinNtOpen(LPCWSTR path, REFIID iid, void **object)
{
    if (_wcsnicmp(path, L"WinNT:", 6))
        return E_ADS_BAD_PATHNAME;
    const WCHAR *rest = path + 6;
    if (rest[0] != L'/' || rest[1] != L'/')
        return E_ADS_BAD_PATHNAME;
    SEGMENTS s;
    HRESULT hr = split_path(rest + 2, &s);
    if (FAILED(hr))
        return hr;
    OBJ *o = nullptr;
    const WCHAR *hint = s.class_hint;
    if ((hint && class_is(hint, L"computer")) || (s.count == 1 && !hint)) {
        /* a computer, alone or below its domain. */
        if (s.count > 2)
            hr = E_ADS_BAD_PATHNAME;
        else if (!is_local(s.text[s.count - 1]))
            hr = HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);
        else {
            o = new_object(K_COMPUTER, WORKGROUP, computer_name());
            hr = o ? S_OK : E_OUTOFMEMORY;
        }
    } else if (s.count == 1) {
        hr = E_ADS_UNKNOWN_OBJECT;
    } else {
        const WCHAR *domain = s.text[s.count - 2], *name = s.text[s.count - 1];
        if (s.count == 3 && !is_local(s.text[1]))
            hr = HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);
        else if (is_local(domain) || s.count == 3)
            hr = open_local(hint, name, &o);
        else {
            /* an account of another authority, which can be named but not changed. */
            WCHAR account[600], referenced[256];
            BYTE sid[68];
            DWORD sid_size = sizeof(sid), referenced_size = RTL_NUMBER_OF(referenced), use;
            _snwprintf(account, RTL_NUMBER_OF(account), L"%ls\\%ls", domain, name);
            if (!LookupAccountNameW(nullptr, account, sid, &sid_size, referenced, &referenced_size, &use))
                hr = HRESULT_FROM_WIN32(1355);
            else {
                o = new_object(use == SID_TYPE_USER ? K_USER : K_GROUP, domain, name);
                hr = o ? S_OK : E_OUTOFMEMORY;
            }
        }
    }
    free_segments(&s);
    if (FAILED(hr))
        return hr;
    hr = obj_query(o, iid, object);
    obj_release(o);
    return hr;
}

/* the account a path in a member call names, as domain and name. */
static HRESULT account_of(const WCHAR *text, WCHAR *account, SIZE_T chars)
{
    if (!_wcsnicmp(text, L"WinNT://", 8))
        text += 8;
    SEGMENTS s;
    HRESULT hr = split_path(text, &s);
    if (FAILED(hr))
        return hr;
    if (s.count == 1)
        _snwprintf(account, chars, L"%ls", s.text[0]);
    else
        _snwprintf(account, chars, L"%ls\\%ls", s.text[s.count - 2], s.text[s.count - 1]);
    free_segments(&s);
    return S_OK;
}

/* ------------------------------------------------------------------ operations */

static HRESULT op_string(OBJ *o, int which, BSTR *value)
{
    if (!value)
        return E_POINTER;
    WCHAR schema[400];
    const WCHAR *text = L"";
    switch (which) {
    case 0: text = o->name; break;
    case 1: text = class_text(o->kind); break;
    case 3: text = o->path; break;
    case 4: text = o->parent; break;
    case 5:
        _snwprintf(schema, RTL_NUMBER_OF(schema), L"WinNT://%ls/%ls/Schema/%ls", WORKGROUP, computer_name(), class_text(o->kind));
        text = schema;
        break;
    default: break;
    }
    *value = SysAllocString(text);
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT op_get_info(OBJ *o)
{
    /* what the caller changed stays, the rest is read again when asked. */
    if (!o->dirty && !o->pending) {
        release_mem(o->description);
        o->description = nullptr;
    }
    return S_OK;
}

static HRESULT op_set_info(OBJ *o)
{
    DWORD status = 0;
    if (o->kind == K_GROUP) {
        if (o->pending) {
            struct {
                WCHAR *name, *comment;
            } info = {o->name, o->description ? o->description : (WCHAR *)L""};
            status = NetLocalGroupAdd(nullptr, 1, (LPBYTE)&info, nullptr);
        } else if (o->dirty) {
            WCHAR *comment = o->description ? o->description : (WCHAR *)L"";
            status = NetLocalGroupSetInfo(nullptr, o->name, 1002, (LPBYTE)&comment, nullptr);
        }
    } else if (o->kind == K_USER && o->pending) {
        struct {
            WCHAR *name, *password;
            DWORD age, priv;
            WCHAR *home, *comment;
            DWORD flags;
            WCHAR *script;
        } info = {o->name, (WCHAR *)L"", 0, 1, nullptr, o->description, 0x201, nullptr};
        status = NetUserAdd(nullptr, 1, (LPBYTE)&info, nullptr);
    }
    if (!status) {
        o->pending = FALSE;
        o->dirty = FALSE;
    }
    return win32_error(status);
}

static HRESULT text_variant(const WCHAR *text, VARIANT *out)
{
    VariantInit(out);
    out->bstrVal = SysAllocString(text);
    if (!out->bstrVal)
        return E_OUTOFMEMORY;
    out->vt = VT_BSTR;
    return S_OK;
}

static const WCHAR *description_of(OBJ *o)
{
    if (!o->description && !o->pending && o->kind == K_GROUP) {
        LPBYTE info;
        if (!NetLocalGroupGetInfo(nullptr, o->name, 1002, &info)) {
            o->description = dup(*(WCHAR **)info ? *(WCHAR **)info : L"");
            NetApiBufferFree(info);
        }
    }
    return o->description ? o->description : L"";
}

static HRESULT op_get(OBJ *o, const WCHAR *name, VARIANT *value)
{
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if (o->kind == K_MEMBERS)
        return E_ADS_PROPERTY_NOT_FOUND;
    if (!_wcsicmp(name, L"Description") && o->kind != K_COMPUTER)
        return text_variant(description_of(o), value);
    if (!_wcsicmp(name, L"Name"))
        return text_variant(o->name, value);
    if (!_wcsicmp(name, L"groupType") && o->kind == K_GROUP) {
        value->vt = VT_I4;
        value->lVal = 4;
        return S_OK;
    }
    return E_ADS_PROPERTY_NOT_FOUND;
}

static HRESULT op_put(OBJ *o, const WCHAR *name, const VARIANT *value)
{
    if (!_wcsicmp(name, L"Description") && o->kind != K_COMPUTER && o->kind != K_MEMBERS) {
        VARIANT text;
        VariantInit(&text);
        HRESULT hr = VariantChangeType(&text, (VARIANT *)value, 0, VT_BSTR);
        if (FAILED(hr))
            return E_ADS_BAD_PARAMETER;
        hr = op_put_description(o, text.bstrVal);
        VariantClear(&text);
        return hr;
    }
    if (!_wcsicmp(name, L"groupType") && o->kind == K_GROUP) {
        /* the only kind a computer has is the local group, whether or not it is for security. */
        VARIANT number;
        VariantInit(&number);
        if (FAILED(VariantChangeType(&number, (VARIANT *)value, 0, VT_I4)))
            return E_ADS_BAD_PARAMETER;
        return (number.lVal & 0xF) == 4 ? S_OK : E_ADS_BAD_PARAMETER;
    }
    return E_ADS_PROPERTY_NOT_SUPPORTED;
}

static HRESULT op_get_ex(OBJ *o, const WCHAR *name, VARIANT *value)
{
    VARIANT single;
    HRESULT hr = op_get(o, name, &single);
    if (FAILED(hr)) {
        VariantInit(value);
        return hr;
    }
    SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, 1);
    if (!array) {
        VariantClear(&single);
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    hr = SafeArrayPutElement(array, &index, &single);
    VariantClear(&single);
    if (FAILED(hr)) {
        SafeArrayDestroy(array);
        return hr;
    }
    VariantInit(value);
    value->vt = VT_ARRAY | VT_VARIANT;
    value->parray = array;
    return S_OK;
}

#define ADS_PROPERTY_CLEAR 1
#define ADS_PROPERTY_UPDATE 2
#define ADS_PROPERTY_APPEND 3
#define ADS_PROPERTY_DELETE 4

static HRESULT op_put_ex(OBJ *o, LONG control, const WCHAR *name, const VARIANT *value)
{
    if (control == ADS_PROPERTY_CLEAR || control == ADS_PROPERTY_DELETE) {
        VARIANT empty;
        empty.vt = VT_BSTR;
        empty.bstrVal = nullptr;
        return op_put(o, name, &empty);
    }
    if (control != ADS_PROPERTY_UPDATE && control != ADS_PROPERTY_APPEND)
        return E_ADS_BAD_PARAMETER;
    if (!(value->vt & VT_ARRAY))
        return op_put(o, name, value);
    SAFEARRAY *array = value->parray;
    LONG low, high;
    if (!array || FAILED(SafeArrayGetLBound(array, 1, &low)) || FAILED(SafeArrayGetUBound(array, 1, &high)))
        return E_ADS_BAD_PARAMETER;
    if (high < low) {
        VARIANT empty;
        empty.vt = VT_BSTR;
        empty.bstrVal = nullptr;
        return op_put(o, name, &empty);
    }
    VARIANT first;
    VariantInit(&first);
    HRESULT hr;
    if ((value->vt & VT_TYPEMASK) == VT_VARIANT)
        hr = SafeArrayGetElement(array, &low, &first);
    else {
        void *data;
        hr = SafeArrayAccessData(array, &data);
        if (SUCCEEDED(hr)) {
            first.vt = (VARTYPE)(value->vt & VT_TYPEMASK);
            if (first.vt == VT_BSTR)
                first.bstrVal = SysAllocString(*(BSTR *)data);
            else
                memcpy(&first.lVal, data, SafeArrayGetElemsize(array) < 8 ? SafeArrayGetElemsize(array) : 8);
            SafeArrayUnaccessData(array);
        }
    }
    if (FAILED(hr))
        return hr;
    hr = op_put(o, name, &first);
    VariantClear(&first);
    return hr;
}

static HRESULT op_get_description(OBJ *o, BSTR *value)
{
    if (!value)
        return E_POINTER;
    *value = SysAllocString(description_of(o));
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT op_put_description(OBJ *o, const WCHAR *value)
{
    WCHAR *copy = dup(value ? value : L"");
    if (!copy)
        return E_OUTOFMEMORY;
    release_mem(o->description);
    o->description = copy;
    o->dirty = TRUE;
    return S_OK;
}

/* ------------------------------------------------------------------ children and members */

static BOOL wanted_class(OBJ *o, KIND kind)
{
    if (!o->filter_count)
        return TRUE;
    for (DWORD i = 0; i < o->filter_count; i++)
        if (!_wcsicmp(o->filter[i], class_text(kind)))
            return TRUE;
    return FALSE;
}

static BOOL add_item(ITEM **items, DWORD *count, DWORD *capacity, KIND kind, const WCHAR *domain, const WCHAR *name)
{
    if (*count == *capacity) {
        DWORD bigger = *capacity ? *capacity * 2 : 16;
        ITEM *grown = mem((SIZE_T)bigger * sizeof(ITEM));
        if (!grown)
            return FALSE;
        if (*items)
            memcpy(grown, *items, (SIZE_T)*count * sizeof(ITEM));
        release_mem(*items);
        *items = grown;
        *capacity = bigger;
    }
    ITEM *item = &(*items)[*count];
    item->kind = kind;
    item->domain = dup(domain);
    item->name = dup(name);
    if (!item->domain || !item->name) {
        release_mem(item->domain);
        release_mem(item->name);
        return FALSE;
    }
    (*count)++;
    return TRUE;
}

/* the children of the computer, the members of a group or what the collection has. */
static HRESULT list_items(OBJ *o, ITEM **items, DWORD *count)
{
    DWORD capacity = 0;
    *items = nullptr;
    *count = 0;
    if (o->kind == K_MEMBERS) {
        *items = copy_items(o->items, o->item_count);
        *count = o->item_count;
        return *items || !o->item_count ? S_OK : E_OUTOFMEMORY;
    }
    if (o->kind == K_COMPUTER) {
        LPBYTE buffer;
        DWORD entries, total;
        if (wanted_class(o, K_GROUP) && !NetLocalGroupEnum(nullptr, 0, &buffer, (DWORD)-1, &entries, &total, nullptr)) {
            for (DWORD i = 0; i < entries; i++)
                add_item(items, count, &capacity, K_GROUP, computer_name(), ((WCHAR **)buffer)[i]);
            NetApiBufferFree(buffer);
        }
        if (wanted_class(o, K_USER) && !NetUserEnum(nullptr, 0, 2, &buffer, (DWORD)-1, &entries, &total, nullptr)) {
            for (DWORD i = 0; i < entries; i++)
                add_item(items, count, &capacity, K_USER, computer_name(), ((WCHAR **)buffer)[i]);
            NetApiBufferFree(buffer);
        }
        return S_OK;
    }
    return E_NOTIMPL;
}

static HRESULT op_count(OBJ *o, LONG *count)
{
    ITEM *items;
    DWORD n;
    HRESULT hr = list_items(o, &items, &n);
    if (FAILED(hr))
        return hr;
    free_items(items, n);
    *count = (LONG)n;
    return S_OK;
}

static HRESULT op_new_enum(OBJ *o, IUnknown **enumerator)
{
    ITEM *items;
    DWORD n;
    *enumerator = nullptr;
    HRESULT hr = list_items(o, &items, &n);
    if (FAILED(hr))
        return hr;
    IEnumVARIANT *e = make_enum(items, n, 0);
    free_items(items, n);
    *enumerator = (IUnknown *)e;
    return e ? S_OK : E_OUTOFMEMORY;
}

static HRESULT op_get_filter(OBJ *o, VARIANT *filter)
{
    VariantInit(filter);
    if (!o->filter_count)
        return S_OK;
    SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, o->filter_count);
    if (!array)
        return E_OUTOFMEMORY;
    for (DWORD i = 0; i < o->filter_count; i++) {
        VARIANT item;
        LONG index = (LONG)i;
        text_variant(o->filter[i], &item);
        SafeArrayPutElement(array, &index, &item);
        VariantClear(&item);
    }
    filter->vt = VT_ARRAY | VT_VARIANT;
    filter->parray = array;
    return S_OK;
}

static HRESULT op_put_filter(OBJ *o, const VARIANT *filter)
{
    WCHAR **names = nullptr;
    DWORD count = 0;
    if (filter->vt & VT_ARRAY) {
        SAFEARRAY *array = filter->parray;
        LONG low, high;
        if (!array || (filter->vt & VT_TYPEMASK) != VT_VARIANT || FAILED(SafeArrayGetLBound(array, 1, &low)) ||
            FAILED(SafeArrayGetUBound(array, 1, &high)))
            return E_ADS_INVALID_FILTER;
        count = high >= low ? (DWORD)(high - low + 1) : 0;
        names = mem(count * sizeof(WCHAR *));
        for (DWORD i = 0; i < count; i++) {
            VARIANT item, text;
            LONG index = low + (LONG)i;
            VariantInit(&item);
            VariantInit(&text);
            if (SUCCEEDED(SafeArrayGetElement(array, &index, &item)) && SUCCEEDED(VariantChangeType(&text, &item, 0, VT_BSTR)))
                names[i] = dup(text.bstrVal ? text.bstrVal : L"");
            VariantClear(&item);
            VariantClear(&text);
        }
    } else if (filter->vt != VT_EMPTY && filter->vt != VT_NULL) {
        VARIANT text;
        VariantInit(&text);
        if (FAILED(VariantChangeType(&text, (VARIANT *)filter, 0, VT_BSTR)))
            return E_ADS_INVALID_FILTER;
        names = mem(sizeof(WCHAR *));
        names[0] = dup(text.bstrVal ? text.bstrVal : L"");
        count = 1;
        VariantClear(&text);
    }
    free_filter(o);
    o->filter = names;
    o->filter_count = count;
    return S_OK;
}

static HRESULT op_get_object(OBJ *o, const WCHAR *class_hint, const WCHAR *name, IDispatch **object)
{
    *object = nullptr;
    if (!name)
        return E_ADS_BAD_PARAMETER;
    OBJ *found;
    HRESULT hr = open_local(class_hint, name, &found);
    return FAILED(hr) ? hr : object_to_dispatch(found, object);
}

static HRESULT op_create(OBJ *o, const WCHAR *class_hint, const WCHAR *name, IDispatch **object)
{
    *object = nullptr;
    if (!class_hint || !name || !*name)
        return E_ADS_BAD_PARAMETER;
    KIND kind;
    if (class_is(class_hint, L"group"))
        kind = K_GROUP;
    else if (class_is(class_hint, L"user"))
        kind = K_USER;
    else
        return E_ADS_UNKNOWN_OBJECT;
    OBJ *made = new_object(kind, computer_name(), name);
    if (made)
        made->pending = TRUE;
    return object_to_dispatch(made, object);
}

static HRESULT op_delete(OBJ *o, const WCHAR *class_hint, const WCHAR *name)
{
    if (!class_hint || !name)
        return E_ADS_BAD_PARAMETER;
    if (class_is(class_hint, L"group"))
        return win32_error(NetLocalGroupDel(nullptr, name));
    if (class_is(class_hint, L"user"))
        return win32_error(NetUserDel(nullptr, name));
    return E_ADS_UNKNOWN_OBJECT;
}

typedef struct MEMBER1 {
    PVOID sid;
    DWORD usage;
    WCHAR *name;
} MEMBER1;

static HRESULT op_members(OBJ *o, IDispatch **members)
{
    *members = nullptr;
    LPBYTE buffer;
    DWORD entries, total;
    DWORD status = NetLocalGroupGetMembers(nullptr, o->name, 1, &buffer, (DWORD)-1, &entries, &total, nullptr);
    if (status)
        return win32_error(status);
    OBJ *collection = new_object(K_MEMBERS, L"", L"Members");
    DWORD capacity = 0;
    for (DWORD i = 0; collection && i < entries; i++) {
        const MEMBER1 *member = &((MEMBER1 *)buffer)[i];
        WCHAR *slash = wcsrchr(member->name, L'\\');
        WCHAR domain[256];
        if (slash && slash - member->name < (int)RTL_NUMBER_OF(domain)) {
            memcpy(domain, member->name, (SIZE_T)(slash - member->name) * sizeof(WCHAR));
            domain[slash - member->name] = 0;
        } else {
            wcscpy(domain, computer_name());
        }
        add_item(&collection->items, &collection->item_count, &capacity, member->usage == SID_TYPE_USER ? K_USER : K_GROUP,
                 domain, slash ? slash + 1 : member->name);
    }
    NetApiBufferFree(buffer);
    return object_to_dispatch(collection, members);
}

static HRESULT op_is_member(OBJ *o, const WCHAR *member, VARIANT_BOOL *result)
{
    *result = VARIANT_FALSE;
    WCHAR account[600], referenced[256];
    HRESULT hr = account_of(member, account, RTL_NUMBER_OF(account));
    if (FAILED(hr))
        return hr;
    BYTE sid[68];
    DWORD sid_size = sizeof(sid), referenced_size = RTL_NUMBER_OF(referenced), use;
    if (!LookupAccountNameW(nullptr, account, sid, &sid_size, referenced, &referenced_size, &use))
        return win32_error(GetLastError());
    LPBYTE buffer;
    DWORD entries, total;
    DWORD status = NetLocalGroupGetMembers(nullptr, o->name, 0, &buffer, (DWORD)-1, &entries, &total, nullptr);
    if (status)
        return win32_error(status);
    SIZE_T length = 8 + 4 * (SIZE_T)sid[1];
    for (DWORD i = 0; i < entries; i++) {
        const BYTE *other = ((BYTE **)buffer)[i];
        if (other[1] == sid[1] && !memcmp(other, sid, length))
            *result = VARIANT_TRUE;
    }
    NetApiBufferFree(buffer);
    return S_OK;
}

static HRESULT op_change_member(OBJ *o, const WCHAR *member, BOOL add)
{
    WCHAR account[600];
    HRESULT hr = account_of(member, account, RTL_NUMBER_OF(account));
    if (FAILED(hr))
        return hr;
    WCHAR *entry = account;
    DWORD status = add ? NetLocalGroupAddMembers(nullptr, o->name, 3, (LPBYTE)&entry, 1)
                       : NetLocalGroupDelMembers(nullptr, o->name, 3, (LPBYTE)&entry, 1);
    return win32_error(status);
}

/* ------------------------------------------------------------------ calls by name */

enum {
    D_NAME = 1, D_CLASS, D_GUID, D_ADSPATH, D_PARENT, D_SCHEMA, D_GETINFO, D_SETINFO, D_GET, D_PUT, D_GETEX, D_PUTEX,
    D_GETINFOEX, D_DESCRIPTION, D_MEMBERS, D_ISMEMBER, D_ADD, D_REMOVE, D_COUNT, D_FILTER, D_HINTS, D_GETOBJECT,
    D_CREATE, D_DELETE, D_COPYHERE, D_MOVEHERE, D_NEWENUM
};

static const struct {
    const WCHAR *name;
    DISPID id;
} g_names[] = {
    {L"Name", D_NAME}, {L"Class", D_CLASS}, {L"GUID", D_GUID}, {L"ADsPath", D_ADSPATH}, {L"Parent", D_PARENT},
    {L"Schema", D_SCHEMA}, {L"GetInfo", D_GETINFO}, {L"SetInfo", D_SETINFO}, {L"Get", D_GET}, {L"Put", D_PUT},
    {L"GetEx", D_GETEX}, {L"PutEx", D_PUTEX}, {L"GetInfoEx", D_GETINFOEX}, {L"Description", D_DESCRIPTION},
    {L"Members", D_MEMBERS}, {L"IsMember", D_ISMEMBER}, {L"Add", D_ADD}, {L"Remove", D_REMOVE}, {L"Count", D_COUNT},
    {L"Filter", D_FILTER}, {L"Hints", D_HINTS}, {L"GetObject", D_GETOBJECT}, {L"Create", D_CREATE},
    {L"Delete", D_DELETE}, {L"CopyHere", D_COPYHERE}, {L"MoveHere", D_MOVEHERE}, {L"_NewEnum", D_NEWENUM},
};

static HRESULT obj_ids(LPOLESTR *names, UINT count, DISPID *ids)
{
    if (!count)
        return S_OK;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i)
            hr = DISP_E_UNKNOWNNAME;
    }
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_names); i++)
        if (!_wcsicmp(names[0], g_names[i].name))
            ids[0] = g_names[i].id;
    return ids[0] == DISPID_UNKNOWN ? DISP_E_UNKNOWNNAME : hr;
}

/* an argument counted from the left, as text the caller frees. */
static HRESULT arg_text(const DISPPARAMS *p, UINT index, BSTR *text)
{
    *text = nullptr;
    if (index >= p->cArgs)
        return DISP_E_BADPARAMCOUNT;
    VARIANT *argument = &p->rgvarg[p->cArgs - 1 - index];
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, argument, 0, VT_BSTR);
    if (FAILED(hr))
        return DISP_E_TYPEMISMATCH;
    *text = converted.bstrVal ? converted.bstrVal : SysAllocString(L"");
    return *text ? S_OK : E_OUTOFMEMORY;
}

static HRESULT ret_bstr(VARIANT *result, HRESULT hr, BSTR text)
{
    if (SUCCEEDED(hr) && result) {
        result->vt = VT_BSTR;
        result->bstrVal = text;
    } else
        SysFreeString(text);
    return hr;
}

static HRESULT ret_object(VARIANT *result, HRESULT hr, IUnknown *object, VARTYPE vt)
{
    if (SUCCEEDED(hr) && result) {
        result->vt = vt;
        result->punkVal = object;
    } else if (SUCCEEDED(hr) && object)
        object->lpVtbl->Release(object);
    return hr;
}

static HRESULT obj_invoke(OBJ *o, DISPID id, WORD flags, DISPPARAMS *p, VARIANT *result)
{
    if (result)
        VariantInit(result);
    if (!p)
        return DISP_E_BADPARAMCOUNT;
    BOOL group = o->kind == K_GROUP, computer = o->kind == K_COMPUTER, members = o->kind == K_MEMBERS;
    BOOL set = flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF);
    BSTR a = nullptr, b = nullptr;
    HRESULT hr = DISP_E_MEMBERNOTFOUND;
    switch (id) {
    case D_NAME:
    case D_CLASS:
    case D_GUID:
    case D_ADSPATH:
    case D_PARENT:
    case D_SCHEMA: {
        BSTR text = nullptr;
        if (members)
            break;
        hr = op_string(o, id - D_NAME, &text);
        return ret_bstr(result, hr, text);
    }
    case D_GETINFO:
    case D_GETINFOEX:
        hr = members ? DISP_E_MEMBERNOTFOUND : op_get_info(o);
        break;
    case D_SETINFO:
        hr = members ? DISP_E_MEMBERNOTFOUND : op_set_info(o);
        break;
    case D_GET:
    case D_GETEX:
        if (members)
            break;
        hr = arg_text(p, 0, &a);
        if (SUCCEEDED(hr)) {
            VARIANT value;
            hr = id == D_GET ? op_get(o, a, &value) : op_get_ex(o, a, &value);
            if (SUCCEEDED(hr) && result)
                *result = value;
            else
                VariantClear(&value);
        }
        break;
    case D_PUT:
        if (members)
            break;
        hr = arg_text(p, 0, &a);
        if (SUCCEEDED(hr))
            hr = p->cArgs < 2 ? DISP_E_BADPARAMCOUNT : op_put(o, a, &p->rgvarg[p->cArgs - 2]);
        break;
    case D_PUTEX:
        if (members)
            break;
        hr = arg_text(p, 1, &a);
        if (SUCCEEDED(hr)) {
            VARIANT control;
            VariantInit(&control);
            hr = p->cArgs < 3 ? DISP_E_BADPARAMCOUNT : VariantChangeType(&control, &p->rgvarg[p->cArgs - 1], 0, VT_I4);
            if (SUCCEEDED(hr))
                hr = op_put_ex(o, control.lVal, a, &p->rgvarg[p->cArgs - 3]);
        }
        break;
    case D_DESCRIPTION:
        if (members || computer)
            break;
        if (set) {
            hr = arg_text(p, 0, &a);
            if (SUCCEEDED(hr))
                hr = op_put_description(o, a);
        } else {
            BSTR text = nullptr;
            hr = op_get_description(o, &text);
            return ret_bstr(result, hr, text);
        }
        break;
    case D_MEMBERS:
        if (group) {
            IDispatch *object = nullptr;
            hr = op_members(o, &object);
            return ret_object(result, hr, (IUnknown *)object, VT_DISPATCH);
        }
        break;
    case D_ISMEMBER:
        if (group) {
            VARIANT_BOOL is = VARIANT_FALSE;
            hr = arg_text(p, 0, &a);
            if (SUCCEEDED(hr))
                hr = op_is_member(o, a, &is);
            if (SUCCEEDED(hr) && result) {
                result->vt = VT_BOOL;
                result->boolVal = is;
            }
        }
        break;
    case D_ADD:
    case D_REMOVE:
        if (group) {
            hr = arg_text(p, 0, &a);
            if (SUCCEEDED(hr))
                hr = op_change_member(o, a, id == D_ADD);
        }
        break;
    case D_COUNT:
        if (computer || members) {
            LONG count;
            hr = op_count(o, &count);
            if (SUCCEEDED(hr) && result) {
                result->vt = VT_I4;
                result->lVal = count;
            }
        }
        break;
    case D_FILTER:
        if (computer || members) {
            if (set)
                hr = p->cArgs ? op_put_filter(o, &p->rgvarg[0]) : DISP_E_BADPARAMCOUNT;
            else
                hr = result ? op_get_filter(o, result) : S_OK;
        }
        break;
    case D_HINTS:
        if (computer)
            hr = S_OK;
        break;
    case D_GETOBJECT:
    case D_CREATE:
        if (computer) {
            IDispatch *object = nullptr;
            hr = arg_text(p, 0, &a);
            if (SUCCEEDED(hr))
                hr = arg_text(p, 1, &b);
            if (SUCCEEDED(hr))
                hr = id == D_CREATE ? op_create(o, a, b, &object) : op_get_object(o, a, b, &object);
            SysFreeString(a);
            SysFreeString(b);
            return ret_object(result, hr, (IUnknown *)object, VT_DISPATCH);
        }
        break;
    case D_DELETE:
        if (computer) {
            hr = arg_text(p, 0, &a);
            if (SUCCEEDED(hr))
                hr = arg_text(p, 1, &b);
            if (SUCCEEDED(hr))
                hr = op_delete(o, a, b);
        }
        break;
    case D_COPYHERE:
    case D_MOVEHERE:
        hr = computer ? E_NOTIMPL : DISP_E_MEMBERNOTFOUND;
        break;
    case D_NEWENUM:
    case DISPID_NEWENUM:
        if (computer || members) {
            IUnknown *enumerator;
            hr = op_new_enum(o, &enumerator);
            return ret_object(result, hr, enumerator, VT_UNKNOWN);
        }
        break;
    default:
        break;
    }
    SysFreeString(a);
    SysFreeString(b);
    return hr;
}
