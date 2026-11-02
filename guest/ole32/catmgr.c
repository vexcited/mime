/*
 * the component categories manager of comcat.dll: which categories a class implements and requires, read from the
 * Implemented Categories and Required Categories keys of its registration. msxml asks it whether a class is a script engine.
 */
#include "ole32_private.h"

#ifndef CAT_E_CATIDNOEXIST
#define CAT_E_CATIDNOEXIST ((HRESULT)0x80040160L)
#endif

MIME_DEFINE_GUID(CLSID_StdComponentCategoriesMgr_, 0x0002E005, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ICatRegister_, 0x0002E012, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ICatInformation_, 0x0002E013, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IEnumGUID_, 0x0002E000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef GUID CATID;

typedef struct CATEGORYINFO_ {
    CATID catid;
    LCID lcid;
    OLECHAR description[128];
} CATEGORYINFO_;

typedef struct CatMgr CatMgr;
typedef struct ICatInformationVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(CatMgr *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(CatMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(CatMgr *This);
    HRESULT(STDMETHODCALLTYPE *EnumCategories)(CatMgr *This, LCID lcid, void **enumerator);
    HRESULT(STDMETHODCALLTYPE *GetCategoryDesc)(CatMgr *This, const CATID *catid, LCID lcid, OLECHAR **description);
    HRESULT(STDMETHODCALLTYPE *EnumClassesOfCategories)(CatMgr *This, ULONG impl_count, const CATID *impl, ULONG req_count,
                                                        const CATID *req, void **enumerator);
    HRESULT(STDMETHODCALLTYPE *IsClassOfCategories)(CatMgr *This, REFCLSID clsid, ULONG impl_count, const CATID *impl,
                                                    ULONG req_count, const CATID *req);
    HRESULT(STDMETHODCALLTYPE *EnumImplCategoriesOfClass)(CatMgr *This, REFCLSID clsid, void **enumerator);
    HRESULT(STDMETHODCALLTYPE *EnumReqCategoriesOfClass)(CatMgr *This, REFCLSID clsid, void **enumerator);
} ICatInformationVtbl;

typedef struct ICatRegisterVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(CatMgr *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(CatMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(CatMgr *This);
    HRESULT(STDMETHODCALLTYPE *RegisterCategories)(CatMgr *This, ULONG count, CATEGORYINFO_ *info);
    HRESULT(STDMETHODCALLTYPE *UnRegisterCategories)(CatMgr *This, ULONG count, CATID *catids);
    HRESULT(STDMETHODCALLTYPE *RegisterClassImplCategories)(CatMgr *This, REFCLSID clsid, ULONG count, CATID *catids);
    HRESULT(STDMETHODCALLTYPE *UnRegisterClassImplCategories)(CatMgr *This, REFCLSID clsid, ULONG count, CATID *catids);
    HRESULT(STDMETHODCALLTYPE *RegisterClassReqCategories)(CatMgr *This, REFCLSID clsid, ULONG count, CATID *catids);
    HRESULT(STDMETHODCALLTYPE *UnRegisterClassReqCategories)(CatMgr *This, REFCLSID clsid, ULONG count, CATID *catids);
} ICatRegisterVtbl;

struct CatMgr {
    const ICatInformationVtbl *Info;
    const ICatRegisterVtbl *Register;
    LONG Refs;
};

static void guid_text(const GUID *g, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    UINT n = 0;
    out[n++] = '{';
    for (int i = 7; i >= 0; i--)
        out[n++] = hex[(g->Data1 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 3; i >= 0; i--)
        out[n++] = hex[(g->Data2 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 3; i >= 0; i--)
        out[n++] = hex[(g->Data3 >> (i * 4)) & 15];
    out[n++] = '-';
    for (int i = 0; i < 2; i++) {
        out[n++] = hex[g->Data4[i] >> 4];
        out[n++] = hex[g->Data4[i] & 15];
    }
    out[n++] = '-';
    for (int i = 2; i < 8; i++) {
        out[n++] = hex[g->Data4[i] >> 4];
        out[n++] = hex[g->Data4[i] & 15];
    }
    out[n++] = '}';
    out[n] = 0;
}

static BOOL parse_hex(const WCHAR *s, int count, UINT64 *out)
{
    UINT64 v = 0;
    for (int i = 0; i < count; i++) {
        WCHAR c = s[i];
        UINT d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return FALSE;
        v = v * 16 + d;
    }
    *out = v;
    return TRUE;
}

static BOOL text_guid(const WCHAR *s, GUID *g)
{
    UINT64 a, b, c, d1, d2;
    if (lstrlenW(s) != 38 || s[0] != '{' || s[9] != '-' || s[14] != '-' || s[19] != '-' || s[24] != '-' || s[37] != '}')
        return FALSE;
    if (!parse_hex(s + 1, 8, &a) || !parse_hex(s + 10, 4, &b) || !parse_hex(s + 15, 4, &c) || !parse_hex(s + 20, 4, &d1) ||
        !parse_hex(s + 25, 12, &d2))
        return FALSE;
    g->Data1 = (DWORD)a;
    g->Data2 = (WORD)b;
    g->Data3 = (WORD)c;
    g->Data4[0] = (BYTE)(d1 >> 8);
    g->Data4[1] = (BYTE)d1;
    for (int i = 0; i < 6; i++)
        g->Data4[2 + i] = (BYTE)(d2 >> ((5 - i) * 8));
    return TRUE;
}

/* the key of a class's category list. */
static HKEY open_list(REFCLSID clsid, const WCHAR *list, REGSAM access, BOOL create)
{
    WCHAR id[40], path[160];
    guid_text(clsid, id);
    wsprintfW(path, L"CLSID\\%s\\%s", id, list);
    HKEY key = nullptr;
    if (create)
        RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, access, nullptr, &key, nullptr);
    else if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, access, &key))
        key = nullptr;
    return key;
}

static BOOL class_has(REFCLSID clsid, const WCHAR *list, const CATID *catid)
{
    HKEY key = open_list(clsid, list, KEY_READ, FALSE);
    if (!key)
        return FALSE;
    WCHAR id[40];
    guid_text(catid, id);
    HKEY sub;
    BOOL has = RegOpenKeyExW(key, id, 0, KEY_READ, &sub) == 0;
    if (has)
        RegCloseKey(sub);
    RegCloseKey(key);
    return has;
}

static BOOL class_has_any(REFCLSID clsid, const WCHAR *list)
{
    HKEY key = open_list(clsid, list, KEY_READ, FALSE);
    if (!key)
        return FALSE;
    WCHAR name[64];
    DWORD n = 64;
    BOOL any = RegEnumKeyExW(key, 0, name, &n, nullptr, nullptr, nullptr, nullptr) == 0;
    RegCloseKey(key);
    return any;
}

/* ------------------------------------------------------------------ enumerators of guids */

typedef struct GuidEnum GuidEnum;
typedef struct IEnumGUIDVtbl_ {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(GuidEnum *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(GuidEnum *This);
    ULONG(STDMETHODCALLTYPE *Release)(GuidEnum *This);
    HRESULT(STDMETHODCALLTYPE *Next)(GuidEnum *This, ULONG count, GUID *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(GuidEnum *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(GuidEnum *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(GuidEnum *This, GuidEnum **out);
} IEnumGUIDVtbl_;

struct GuidEnum {
    const IEnumGUIDVtbl_ *Vtbl;
    LONG Refs;
    GUID *Items;
    ULONG Count, Index;
};

static HRESULT STDMETHODCALLTYPE ge_query(GuidEnum *e, REFIID riid, void **out)
{
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IEnumGUID_)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&e->Refs);
    *out = e;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ge_addref(GuidEnum *e)
{
    return (ULONG)InterlockedIncrement(&e->Refs);
}

static ULONG STDMETHODCALLTYPE ge_release(GuidEnum *e)
{
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        HeapFree(GetProcessHeap(), 0, e->Items);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ge_next(GuidEnum *e, ULONG count, GUID *items, ULONG *fetched)
{
    ULONG got = 0;
    while (got < count && e->Index < e->Count)
        items[got++] = e->Items[e->Index++];
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE ge_skip(GuidEnum *e, ULONG count)
{
    e->Index += count;
    if (e->Index > e->Count) {
        e->Index = e->Count;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ge_reset(GuidEnum *e)
{
    e->Index = 0;
    return S_OK;
}

static HRESULT new_guid_enum(const GUID *items, ULONG count, ULONG index, GuidEnum **out);

static HRESULT STDMETHODCALLTYPE ge_clone(GuidEnum *e, GuidEnum **out)
{
    return new_guid_enum(e->Items, e->Count, e->Index, out);
}

static const IEnumGUIDVtbl_ g_enum_vtbl = {ge_query, ge_addref, ge_release, ge_next, ge_skip, ge_reset, ge_clone};

static HRESULT new_guid_enum(const GUID *items, ULONG count, ULONG index, GuidEnum **out)
{
    GuidEnum *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GuidEnum));
    if (!e)
        return E_OUTOFMEMORY;
    e->Vtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Items = HeapAlloc(GetProcessHeap(), 0, (count ? count : 1) * sizeof(GUID));
    if (!e->Items) {
        HeapFree(GetProcessHeap(), 0, e);
        return E_OUTOFMEMORY;
    }
    if (count)
        memcpy(e->Items, items, count * sizeof(GUID));
    e->Count = count;
    e->Index = index;
    *out = e;
    return S_OK;
}

/* the categories one list of a class holds. */
static HRESULT enum_categories(REFCLSID clsid, const WCHAR *list, void **out)
{
    GUID found[64];
    ULONG n = 0;
    HKEY key = open_list(clsid, list, KEY_READ, FALSE);
    if (key) {
        for (DWORD i = 0; n < 64; i++) {
            WCHAR name[64];
            DWORD len = 64;
            if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr))
                break;
            if (text_guid(name, &found[n]))
                n++;
        }
        RegCloseKey(key);
    }
    return new_guid_enum(found, n, 0, (GuidEnum **)out);
}

/* ------------------------------------------------------------------ the manager */

static CatMgr *from_info(CatMgr *p)
{
    return p;
}

static HRESULT STDMETHODCALLTYPE cm_query(CatMgr *m, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ICatInformation_)) {
        *out = &m->Info;
    } else if (IsEqualGUID(riid, &IID_ICatRegister_)) {
        *out = &m->Register;
    } else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&m->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cm_addref(CatMgr *m)
{
    return (ULONG)InterlockedIncrement(&m->Refs);
}

static ULONG STDMETHODCALLTYPE cm_release(CatMgr *m)
{
    LONG refs = InterlockedDecrement(&m->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, m);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ci_enum_categories(CatMgr *m, LCID lcid, void **out)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ci_desc(CatMgr *m, const CATID *catid, LCID lcid, OLECHAR **description)
{
    return CAT_E_CATIDNOEXIST;
}

static HRESULT STDMETHODCALLTYPE ci_enum_classes(CatMgr *m, ULONG impl_count, const CATID *impl, ULONG req_count,
                                                 const CATID *req, void **out)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ci_is_class(CatMgr *m, REFCLSID clsid, ULONG impl_count, const CATID *impl, ULONG req_count,
                                             const CATID *req)
{
    if (impl_count != (ULONG)-1) {
        BOOL ok = impl_count == 0 && !class_has_any(clsid, L"Implemented Categories");
        for (ULONG i = 0; i < impl_count && !ok; i++)
            ok = class_has(clsid, L"Implemented Categories", &impl[i]);
        if (!ok)
            return S_FALSE;
    }
    if (req_count != (ULONG)-1) {
        if (req_count == 0) {
            if (class_has_any(clsid, L"Required Categories"))
                return S_FALSE;
        } else {
            for (ULONG i = 0; i < req_count; i++)
                if (!class_has(clsid, L"Required Categories", &req[i]))
                    return S_FALSE;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ci_enum_impl(CatMgr *m, REFCLSID clsid, void **out)
{
    return enum_categories(clsid, L"Implemented Categories", out);
}

static HRESULT STDMETHODCALLTYPE ci_enum_req(CatMgr *m, REFCLSID clsid, void **out)
{
    return enum_categories(clsid, L"Required Categories", out);
}

/* the interface pointers of the second vtable point at the second member. */
#define MGR_FROM_REGISTER(p) ((CatMgr *)((BYTE *)(p) - offsetof(CatMgr, Register)))

static HRESULT STDMETHODCALLTYPE cr_query(CatMgr *p, REFIID riid, void **out)
{
    return cm_query(MGR_FROM_REGISTER(p), riid, out);
}

static ULONG STDMETHODCALLTYPE cr_addref(CatMgr *p)
{
    return cm_addref(MGR_FROM_REGISTER(p));
}

static ULONG STDMETHODCALLTYPE cr_release(CatMgr *p)
{
    return cm_release(MGR_FROM_REGISTER(p));
}

static HRESULT STDMETHODCALLTYPE cr_register_categories(CatMgr *p, ULONG count, CATEGORYINFO_ *info)
{
    for (ULONG i = 0; i < count; i++) {
        WCHAR id[40], path[160];
        guid_text(&info[i].catid, id);
        wsprintfW(path, L"Component Categories\\%s", id);
        HKEY key;
        if (RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr))
            return E_ACCESSDENIED;
        WCHAR name[16];
        wsprintfW(name, L"%X", (unsigned)info[i].lcid);
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)info[i].description,
                       ((DWORD)lstrlenW(info[i].description) + 1) * sizeof(WCHAR));
        RegCloseKey(key);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cr_unregister_categories(CatMgr *p, ULONG count, CATID *catids)
{
    for (ULONG i = 0; i < count; i++) {
        WCHAR id[40], path[160];
        guid_text(&catids[i], id);
        wsprintfW(path, L"Component Categories\\%s", id);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    }
    return S_OK;
}

static HRESULT change_class(REFCLSID clsid, const WCHAR *list, ULONG count, CATID *catids, BOOL add)
{
    for (ULONG i = 0; i < count; i++) {
        HKEY key = open_list(clsid, list, KEY_WRITE | KEY_READ, add);
        if (!key)
            continue;
        WCHAR id[40];
        guid_text(&catids[i], id);
        if (add) {
            HKEY sub;
            if (!RegCreateKeyExW(key, id, 0, nullptr, 0, KEY_WRITE, nullptr, &sub, nullptr))
                RegCloseKey(sub);
        } else {
            RegDeleteKeyW(key, id);
        }
        RegCloseKey(key);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cr_reg_impl(CatMgr *p, REFCLSID clsid, ULONG count, CATID *catids)
{
    return change_class(clsid, L"Implemented Categories", count, catids, TRUE);
}

static HRESULT STDMETHODCALLTYPE cr_unreg_impl(CatMgr *p, REFCLSID clsid, ULONG count, CATID *catids)
{
    return change_class(clsid, L"Implemented Categories", count, catids, FALSE);
}

static HRESULT STDMETHODCALLTYPE cr_reg_req(CatMgr *p, REFCLSID clsid, ULONG count, CATID *catids)
{
    return change_class(clsid, L"Required Categories", count, catids, TRUE);
}

static HRESULT STDMETHODCALLTYPE cr_unreg_req(CatMgr *p, REFCLSID clsid, ULONG count, CATID *catids)
{
    return change_class(clsid, L"Required Categories", count, catids, FALSE);
}

static const ICatInformationVtbl g_info_vtbl = {cm_query,        cm_addref,       cm_release,   ci_enum_categories,
                                                ci_desc,         ci_enum_classes, ci_is_class,  ci_enum_impl,
                                                ci_enum_req};
static const ICatRegisterVtbl g_register_vtbl = {cr_query,   cr_addref,   cr_release,   cr_register_categories,
                                                 cr_unregister_categories, cr_reg_impl, cr_unreg_impl, cr_reg_req,
                                                 cr_unreg_req};

static HRESULT STDMETHODCALLTYPE mf_query(IClassFactory *f, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *out = f;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mf_addref(IClassFactory *f)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE mf_release(IClassFactory *f)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE mf_create(IClassFactory *f, IUnknown *outer, REFIID riid, void **out)
{
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    CatMgr *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CatMgr));
    if (!m)
        return E_OUTOFMEMORY;
    m->Info = &g_info_vtbl;
    m->Register = &g_register_vtbl;
    m->Refs = 1;
    HRESULT hr = cm_query(from_info(m), riid, out);
    cm_release(m);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mf_lock(IClassFactory *f, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {mf_query, mf_addref, mf_release, mf_create, mf_lock};
static IClassFactory g_factory = {&g_factory_vtbl};

BOOL CatMgrClassObject(REFCLSID clsid, REFIID riid, void **object, HRESULT *hr)
{
    if (!IsEqualGUID(clsid, &CLSID_StdComponentCategoriesMgr_))
        return FALSE;
    *hr = mf_query(&g_factory, riid, object);
    return TRUE;
}
