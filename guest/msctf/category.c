/*
 * the category manager and the display attribute manager. a guid atom stands for a guid within the process, the
 * way a property carries it; the display attributes are the ones the host's input method uses, as there are no
 * text services registering their own.
 */
#include "msctf_private.h"

/* ------------------------------------------------------------------ guid atoms */

/*
 * the atoms of the three guids Windows knows in advance are 32, 64 and 96. any other guid falls in one of 31 groups
 * by its first word, and its atom is 32 times its place in the group plus the group number plus one, so the low
 * five bits of an ordinary atom are never zero.
 */
#define ATOM_GROUPS 31
#define ATOM_RESERVED 3

static const GUID g_reserved_guids[ATOM_RESERVED] = {
    {0x626761ad, 0x78d2, 0x44d2, {0xbe, 0x8b, 0x75, 0x2c, 0xf1, 0x22, 0xac, 0xec}},
    {0x78cb5b0e, 0x26ed, 0x4fcc, {0x85, 0x4c, 0x77, 0xe8, 0xf3, 0xd1, 0xaa, 0x80}},
    {0x5f4feb92, 0xd37d, 0x449e, {0x83, 0x93, 0x76, 0xb2, 0x6f, 0xfe, 0x60, 0x21}},
};

typedef struct ATOM_GROUP {
    GUID *Guids;
    ULONG Count, Capacity;
} ATOM_GROUP;

static ATOM_GROUP g_groups[ATOM_GROUPS];
static SRWLOCK g_groups_lock = SRWLOCK_INIT;

static BOOL group_add(ATOM_GROUP *group, REFGUID guid)
{
    if (group->Count == group->Capacity) {
        ULONG capacity = group->Capacity ? group->Capacity * 2 : 4;
        GUID *grown = CtfAlloc(capacity * sizeof(*grown));
        if (!grown)
            return FALSE;
        if (group->Count)
            memcpy(grown, group->Guids, group->Count * sizeof(*grown));
        CtfFree(group->Guids);
        group->Guids = grown;
        group->Capacity = capacity;
    }
    group->Guids[group->Count++] = *guid;
    return TRUE;
}

TfGuidAtom CtfGuidAtom(REFGUID guid)
{
    for (ULONG i = 0; i < ATOM_RESERVED; i++) {
        if (IsEqualGUID(guid, &g_reserved_guids[i]))
            return (i + 1) << 5;
    }

    ULONG group_number = guid->Data1 % ATOM_GROUPS;
    ATOM_GROUP *group = &g_groups[group_number];
    TfGuidAtom atom = 0;
    AcquireSRWLockExclusive(&g_groups_lock);
    for (ULONG n = 0; n < group->Count && !atom; n++) {
        if (IsEqualGUID(&group->Guids[n], guid))
            atom = n << 5 | (group_number + 1);
    }
    if (!atom && group_add(group, guid))
        atom = (group->Count - 1) << 5 | (group_number + 1);
    ReleaseSRWLockExclusive(&g_groups_lock);
    return atom;
}

BOOL CtfAtomGuid(TfGuidAtom atom, GUID *guid)
{
    ULONG low = atom & 31, high = atom >> 5;
    if (!low) {
        if (!high || high > ATOM_RESERVED)
            return FALSE;
        *guid = g_reserved_guids[high - 1];
        return TRUE;
    }

    ATOM_GROUP *group = &g_groups[low - 1];
    BOOL known;
    AcquireSRWLockShared(&g_groups_lock);
    known = high < group->Count;
    if (known)
        *guid = group->Guids[high];
    ReleaseSRWLockShared(&g_groups_lock);
    return known;
}


/* ------------------------------------------------------------------ display attributes */

typedef struct {
    GUID Guid;
    const WCHAR *Description;
    TF_DISPLAYATTRIBUTE Attribute;
} DISPLAY_DEFAULT;

#define NO_COLOR {TF_CT_NONE, {0}}

/* the host's input method marks what it composes by underlines: dotted while typed, thick for the part converted. */
static const DISPLAY_DEFAULT g_displays[] = {
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x01}}, L"Mime Input",
     {NO_COLOR, NO_COLOR, TF_LS_DOT, FALSE, NO_COLOR, TF_ATTR_INPUT}},
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x02}}, L"Mime Target Converted",
     {NO_COLOR, NO_COLOR, TF_LS_SOLID, TRUE, NO_COLOR, TF_ATTR_TARGET_CONVERTED}},
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x03}}, L"Mime Converted",
     {NO_COLOR, NO_COLOR, TF_LS_SOLID, FALSE, NO_COLOR, TF_ATTR_CONVERTED}},
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x04}}, L"Mime Target Not Converted",
     {NO_COLOR, NO_COLOR, TF_LS_DOT, TRUE, NO_COLOR, TF_ATTR_TARGET_NOTCONVERTED}},
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x05}}, L"Mime Input Error",
     {NO_COLOR, NO_COLOR, TF_LS_SQUIGGLE, FALSE, NO_COLOR, TF_ATTR_INPUT_ERROR}},
    {{0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x06}}, L"Mime Fixed Converted",
     {NO_COLOR, NO_COLOR, TF_LS_NONE, FALSE, NO_COLOR, TF_ATTR_FIXEDCONVERTED}},
};
#define DISPLAY_COUNT (sizeof(g_displays) / sizeof(g_displays[0]))

/* the clsid the host's text service goes by. */
const CLSID g_ctf_service_clsid = {0x7d5c3a41, 0x2f6e, 0x4b0a, {0x9e, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0xff}};

BOOL CtfDisplayAttributeGuid(TF_DA_ATTR_INFO kind, GUID *guid)
{
    for (ULONG i = 0; i < DISPLAY_COUNT; i++) {
        if (g_displays[i].Attribute.bAttr == kind) {
            *guid = g_displays[i].Guid;
            return TRUE;
        }
    }
    return FALSE;
}

typedef struct {
    ITfDisplayAttributeInfo Iface;
    LONG Refs;
    ULONG Index;
    TF_DISPLAYATTRIBUTE Attribute;
} DISPLAY_INFO;

static HRESULT STDMETHODCALLTYPE info_query(ITfDisplayAttributeInfo *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDisplayAttributeInfo)) {
        *object = This;
        InterlockedIncrement(&((DISPLAY_INFO *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE info_add_ref(ITfDisplayAttributeInfo *This)
{
    return InterlockedIncrement(&((DISPLAY_INFO *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE info_release(ITfDisplayAttributeInfo *This)
{
    DISPLAY_INFO *info = (DISPLAY_INFO *)This;
    LONG refs = InterlockedDecrement(&info->Refs);
    if (!refs) {
        CtfFree(info);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE info_get_guid(ITfDisplayAttributeInfo *This, GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    *guid = g_displays[((DISPLAY_INFO *)This)->Index].Guid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_description(ITfDisplayAttributeInfo *This, BSTR *description)
{
    if (!description)
        return E_INVALIDARG;
    *description = SysAllocString(g_displays[((DISPLAY_INFO *)This)->Index].Description);
    return *description ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE info_get_attribute(ITfDisplayAttributeInfo *This, TF_DISPLAYATTRIBUTE *attribute)
{
    if (!attribute)
        return E_INVALIDARG;
    *attribute = ((DISPLAY_INFO *)This)->Attribute;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_set_attribute(ITfDisplayAttributeInfo *This, const TF_DISPLAYATTRIBUTE *attribute)
{
    if (!attribute)
        return E_INVALIDARG;
    ((DISPLAY_INFO *)This)->Attribute = *attribute;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_reset(ITfDisplayAttributeInfo *This)
{
    DISPLAY_INFO *info = (DISPLAY_INFO *)This;
    info->Attribute = g_displays[info->Index].Attribute;
    return S_OK;
}

static const ITfDisplayAttributeInfoVtbl info_vtbl = {info_query, info_add_ref, info_release, info_get_guid,
                                                      info_get_description, info_get_attribute, info_set_attribute,
                                                      info_reset};

static HRESULT info_create(ULONG index, ITfDisplayAttributeInfo **out)
{
    DISPLAY_INFO *info = CtfAlloc(sizeof(*info));
    if (!info)
        return E_OUTOFMEMORY;
    info->Iface.lpVtbl = &info_vtbl;
    info->Refs = 1;
    info->Index = index;
    info->Attribute = g_displays[index].Attribute;
    CtfObjectCreated();
    *out = &info->Iface;
    return S_OK;
}

typedef struct {
    ITfDisplayAttributeMgr Iface;
    LONG Refs;
} DISPLAY_MGR;

static HRESULT STDMETHODCALLTYPE dmgr_query(ITfDisplayAttributeMgr *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDisplayAttributeMgr)) {
        *object = This;
        InterlockedIncrement(&((DISPLAY_MGR *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dmgr_add_ref(ITfDisplayAttributeMgr *This)
{
    return InterlockedIncrement(&((DISPLAY_MGR *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE dmgr_release(ITfDisplayAttributeMgr *This)
{
    DISPLAY_MGR *mgr = (DISPLAY_MGR *)This;
    LONG refs = InterlockedDecrement(&mgr->Refs);
    if (!refs) {
        CtfFree(mgr);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE dmgr_on_update(ITfDisplayAttributeMgr *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dmgr_enum(ITfDisplayAttributeMgr *This, IEnumTfDisplayAttributeInfo **out)
{
    IUnknown *items[DISPLAY_COUNT] = {};
    HRESULT hr = S_OK;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    for (ULONG i = 0; i < DISPLAY_COUNT && SUCCEEDED(hr); i++)
        hr = info_create(i, (ITfDisplayAttributeInfo **)&items[i]);
    if (SUCCEEDED(hr))
        hr = EnumCreate(&IID_IEnumTfDisplayAttributeInfo, items, DISPLAY_COUNT, (void **)out);
    for (ULONG i = 0; i < DISPLAY_COUNT; i++)
        if (items[i])
            items[i]->lpVtbl->Release(items[i]);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dmgr_get_info(ITfDisplayAttributeMgr *This, REFGUID guid, ITfDisplayAttributeInfo **info,
                                               CLSID *owner)
{
    if (!guid || !info)
        return E_INVALIDARG;
    *info = nullptr;
    if (owner)
        memset(owner, 0, sizeof(*owner));
    for (ULONG i = 0; i < DISPLAY_COUNT; i++) {
        if (IsEqualGUID(&g_displays[i].Guid, guid)) {
            if (owner)
                *owner = g_ctf_service_clsid;
            return info_create(i, info);
        }
    }
    return E_FAIL;
}

static const ITfDisplayAttributeMgrVtbl dmgr_vtbl = {dmgr_query, dmgr_add_ref, dmgr_release, dmgr_on_update, dmgr_enum,
                                                     dmgr_get_info};

HRESULT DisplayAttributeMgrCreate(REFIID riid, void **object)
{
    DISPLAY_MGR *mgr = CtfAlloc(sizeof(*mgr));
    if (!mgr)
        return E_OUTOFMEMORY;
    mgr->Iface.lpVtbl = &dmgr_vtbl;
    mgr->Refs = 1;
    CtfObjectCreated();
    HRESULT hr = dmgr_query(&mgr->Iface, riid, object);
    dmgr_release(&mgr->Iface);
    return hr;
}

/* ------------------------------------------------------------------ the category manager */

typedef struct {
    ITfCategoryMgr Iface;
    LONG Refs;
} CATEGORY_MGR;

static HRESULT STDMETHODCALLTYPE cat_query(ITfCategoryMgr *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCategoryMgr)) {
        *object = This;
        InterlockedIncrement(&((CATEGORY_MGR *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cat_add_ref(ITfCategoryMgr *This)
{
    return InterlockedIncrement(&((CATEGORY_MGR *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE cat_release(ITfCategoryMgr *This)
{
    CATEGORY_MGR *mgr = (CATEGORY_MGR *)This;
    LONG refs = InterlockedDecrement(&mgr->Refs);
    if (!refs) {
        CtfFree(mgr);
        CtfObjectDestroyed();
    }
    return refs;
}

/* registrations go to the registry of the text services installed, of which there are none. */
static HRESULT STDMETHODCALLTYPE cat_register(ITfCategoryMgr *This, REFCLSID clsid, REFGUID catid, REFGUID guid)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE cat_enum_categories(ITfCategoryMgr *This, REFGUID guid, IEnumGUID **out)
{
    if (!out)
        return E_INVALIDARG;
    return EnumGuidsCreate(nullptr, 0, out);
}

static HRESULT STDMETHODCALLTYPE cat_find_closest(ITfCategoryMgr *This, REFGUID guid, GUID *catid,
                                                  const GUID **list, ULONG count)
{
    if (!guid || !catid)
        return E_INVALIDARG;
    for (ULONG i = 0; list && i < count; i++) {
        if (IsEqualGUID(list[i], guid)) {
            *catid = *guid;
            return S_OK;
        }
    }
    memset(catid, 0, sizeof(*catid));
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE cat_register_description(ITfCategoryMgr *This, REFCLSID clsid, REFGUID guid,
                                                          const WCHAR *description, ULONG cch)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE cat_unregister_description(ITfCategoryMgr *This, REFCLSID clsid, REFGUID guid)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE cat_get_description(ITfCategoryMgr *This, REFGUID guid, BSTR *description)
{
    if (!guid || !description)
        return E_INVALIDARG;
    *description = nullptr;
    for (ULONG i = 0; i < DISPLAY_COUNT; i++) {
        if (IsEqualGUID(&g_displays[i].Guid, guid)) {
            *description = SysAllocString(g_displays[i].Description);
            return *description ? S_OK : E_OUTOFMEMORY;
        }
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE cat_register_dword(ITfCategoryMgr *This, REFCLSID clsid, REFGUID guid, DWORD value)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE cat_unregister_dword(ITfCategoryMgr *This, REFCLSID clsid, REFGUID guid)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE cat_get_dword(ITfCategoryMgr *This, REFGUID guid, DWORD *value)
{
    if (!guid || !value)
        return E_INVALIDARG;
    *value = 0;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE cat_register_guid(ITfCategoryMgr *This, REFGUID guid, TfGuidAtom *atom)
{
    if (!guid || !atom)
        return E_INVALIDARG;
    *atom = CtfGuidAtom(guid);
    return *atom ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE cat_get_guid(ITfCategoryMgr *This, TfGuidAtom atom, GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    if (!CtfAtomGuid(atom, guid)) {
        memset(guid, 0, sizeof(*guid));
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cat_is_equal(ITfCategoryMgr *This, TfGuidAtom atom, REFGUID guid, BOOL *equal)
{
    GUID other;
    if (!equal)
        return E_INVALIDARG;
    *equal = FALSE;
    if (!guid)
        return E_INVALIDARG;
    if (!CtfAtomGuid(atom, &other))
        return E_FAIL;
    *equal = IsEqualGUID(&other, guid);
    return S_OK;
}

static const ITfCategoryMgrVtbl cat_vtbl = {cat_query, cat_add_ref, cat_release, cat_register, cat_register,
                                            cat_enum_categories, cat_enum_categories, cat_find_closest,
                                            cat_register_description, cat_unregister_description, cat_get_description,
                                            cat_register_dword, cat_unregister_dword, cat_get_dword,
                                            cat_register_guid, cat_get_guid, cat_is_equal};

HRESULT CategoryMgrCreate(REFIID riid, void **object)
{
    CATEGORY_MGR *mgr = CtfAlloc(sizeof(*mgr));
    if (!mgr)
        return E_OUTOFMEMORY;
    mgr->Iface.lpVtbl = &cat_vtbl;
    mgr->Refs = 1;
    CtfObjectCreated();
    HRESULT hr = cat_query(&mgr->Iface, riid, object);
    cat_release(&mgr->Iface);
    return hr;
}
