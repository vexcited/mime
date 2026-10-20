/*
 * making type libraries: ICreateTypeLib2 and ICreateTypeInfo2 build the records of the model, and SaveAllChanges writes
 * them as an msft file, with the names and the guids hashed and the types, strings and constants in tables of their
 * own, as the reader above and windows' own read them. LHashValOfNameSys and CreateDispTypeInfo are here too.
 */
#include "typelib.h"

#define HRESULT_WRITEFAULT ((HRESULT)0x8003001DL)

/* ------------------------------------------------------------------ the hash of names */

/* the weights of the characters of english, which sort letters by their upper case and a few together. */
static const BYTE g_hash_weight[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x00,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x56, 0x58, 0x55, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x56, 0x58, 0x55, 0x5a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x7f, 0x7f, 0x82, 0x46, 0x84, 0x85, 0x86, 0x87, 0x7f, 0x89, 0x53, 0x8b, 0x8c, 0x7f, 0x7f, 0x7f,
    0x7f, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x96, 0x98, 0x99, 0x53, 0x9b, 0x8c, 0x7f, 0x7f, 0x55,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0x41, 0xab, 0xac, 0x96, 0xae, 0xaf,
    0xb0, 0xb1, 0x32, 0x33, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0x31, 0x4f, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x43, 0x45, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x49,
    0x44, 0x4e, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0xd7, 0x4f, 0x55, 0x55, 0x55, 0x55, 0x55, 0xde, 0xdf,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x43, 0x45, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x49,
    0x44, 0x4e, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0xf7, 0x4f, 0x55, 0x55, 0x55, 0x55, 0x55, 0xde, 0x55};

/* a guess for every language but english: its table is used, with the language bits of english. */
WINOLEAUTAPI ULONG WINAPI LHashValOfNameSysA(SYSKIND syskind, LCID lcid, LPCSTR name)
{
    if (!name)
        return 0;
    ULONG hash = 233495534;
    for (const BYTE *p = (const BYTE *)name; *p; p++)
        hash = g_hash_weight[*p] + 37 * hash;
    ULONG bits = 0x100000 | (syskind == SYS_MAC ? 0x10000 : 0);
    return bits | (hash % 0x1003F & 0xFFFF);
}

WINOLEAUTAPI ULONG WINAPI LHashValOfNameSys(SYSKIND syskind, LCID lcid, const OLECHAR *name)
{
    char narrow[256];
    if (!name)
        return 0;
    WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
    narrow[255] = 0;
    return LHashValOfNameSysA(syskind, lcid, narrow);
}

/* ------------------------------------------------------------------ the library being made */

static TL_LIB *lib_of_create(ICreateTypeLib2 *self)
{
    return CONTAINING_RECORD(self, TL_LIB, lpCreateVtbl);
}

static HRESULT WINAPI cl_query(ICreateTypeLib2 *self, REFIID iid, void **out)
{
    return TlLibQuery(lib_of_create(self), iid, out);
}
static ULONG WINAPI cl_addref(ICreateTypeLib2 *self)
{
    return InterlockedIncrement(&lib_of_create(self)->Refs);
}
static ULONG WINAPI cl_release(ICreateTypeLib2 *self)
{
    TL_LIB *lib = lib_of_create(self);
    LONG refs = InterlockedDecrement(&lib->Refs);
    if (!refs)
        TlLibDestroy(lib);
    return refs;
}

static BOOL valid_name(const WCHAR *name)
{
    return name && *name && wcslen(name) < 256;
}

static TL_TYPE *type_named(TL_LIB *lib, const WCHAR *name, const TL_TYPE *except)
{
    for (ULONG i = 0; i < lib->TypeCount; i++) {
        const WCHAR *n = lib->Types[i]->Name;
        if (lib->Types[i] != except && n && !_wcsicmp(n, name))
            return lib->Types[i];
    }
    return nullptr;
}

static HRESULT WINAPI cl_create_type_info(ICreateTypeLib2 *self, LPOLESTR name, TYPEKIND kind, ICreateTypeInfo **out)
{
    TL_LIB *lib = lib_of_create(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!valid_name(name) || (int)kind < TKIND_ENUM || kind >= TKIND_MAX)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    if (type_named(lib, name, nullptr)) {
        hr = TYPE_E_NAMECONFLICT;
    } else {
        TL_TYPE *t = TlTypeAdd(lib);
        if (!t) {
            hr = E_OUTOFMEMORY;
        } else {
            t->Kind = kind;
            t->Name = TlStrDup(lib, name);
            t->Loaded = TRUE;
            t->Alignment = 0;
            ITypeInfo *info;
            hr = TlGetTypeInfo(lib, t->Index, FALSE, &info);
            if (SUCCEEDED(hr)) {
                *out = (ICreateTypeInfo *)&t->Info->lpCreateVtbl;
                /* the reference TlGetTypeInfo gave goes to the interface returned: the same object. */
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI cl_set_name(ICreateTypeLib2 *self, LPOLESTR name)
{
    TL_LIB *lib = lib_of_create(self);
    if (!valid_name(name))
        return E_INVALIDARG;
    EnterCriticalSection(&lib->Lock);
    lib->Name = TlStrDup(lib, name);
    LeaveCriticalSection(&lib->Lock);
    return lib->Name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI cl_set_version(ICreateTypeLib2 *self, WORD major, WORD minor)
{
    TL_LIB *lib = lib_of_create(self);
    lib->Major = major;
    lib->Minor = minor;
    return S_OK;
}

static HRESULT WINAPI cl_set_guid(ICreateTypeLib2 *self, REFGUID guid)
{
    TL_LIB *lib = lib_of_create(self);
    if (!guid)
        return E_INVALIDARG;
    lib->Guid = *guid;
    lib->HasGuid = TRUE;
    return S_OK;
}

static HRESULT set_text(TL_LIB *lib, WCHAR **field, LPOLESTR text)
{
    EnterCriticalSection(&lib->Lock);
    *field = text ? TlStrDup(lib, text) : nullptr;
    LeaveCriticalSection(&lib->Lock);
    return text && !*field ? E_OUTOFMEMORY : S_OK;
}

static HRESULT WINAPI cl_set_doc_string(ICreateTypeLib2 *self, LPOLESTR doc)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->Doc, doc);
}

static HRESULT WINAPI cl_set_help_file_name(ICreateTypeLib2 *self, LPOLESTR file)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->HelpFile, file);
}

static HRESULT WINAPI cl_set_help_context(ICreateTypeLib2 *self, DWORD context)
{
    lib_of_create(self)->HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI cl_set_lcid(ICreateTypeLib2 *self, LCID lcid)
{
    TL_LIB *lib = lib_of_create(self);
    if (lcid != LOCALE_NEUTRAL && !GetLocaleInfoW(lcid, LOCALE_ILANGUAGE, nullptr, 0))
        return E_INVALIDARG;
    lib->Lcid = lcid;
    return S_OK;
}

static HRESULT WINAPI cl_set_lib_flags(ICreateTypeLib2 *self, UINT flags)
{
    if (flags & ~0xFFu)
        return E_INVALIDARG;
    lib_of_create(self)->Flags = (WORD)flags;
    return S_OK;
}

static HRESULT WINAPI cl_delete_type_info(ICreateTypeLib2 *self, LPOLESTR name)
{
    TL_LIB *lib = lib_of_create(self);
    if (!name)
        return E_INVALIDARG;
    HRESULT hr = TYPE_E_ELEMENTNOTFOUND;
    EnterCriticalSection(&lib->Lock);
    TL_TYPE *t = type_named(lib, name, nullptr);
    if (t) {
        /* the slot goes and the types after it move up; references by number follow. */
        for (ULONG i = t->Index; i + 1 < lib->TypeCount; i++) {
            lib->Types[i] = lib->Types[i + 1];
            lib->Types[i]->Index = i;
        }
        lib->TypeCount--;
        hr = S_OK;
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT cust_add(TL_LIB *lib, TL_CUST **list, REFGUID guid, VARIANT *value)
{
    if (!guid || !value)
        return E_INVALIDARG;
    switch (value->vt) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BSTR: case VT_ERROR:
    case VT_BOOL: case VT_I1: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT:
    case VT_UINT: case VT_DECIMAL:
        break;
    default:
        return DISP_E_BADVARTYPE;
    }
    EnterCriticalSection(&lib->Lock);
    TL_CUST *item = nullptr;
    for (TL_CUST *c = *list; c; c = c->Next)
        if (IsEqualGUID(&c->Guid, guid))
            item = c;
    HRESULT hr = S_OK;
    if (!item) {
        item = TlAlloc(lib, sizeof(*item));
        if (item) {
            item->Next = *list;
            *list = item;
        }
    }
    if (!item) {
        hr = E_OUTOFMEMORY;
    } else {
        item->Guid = *guid;
        hr = TlVariantCopy(lib, &item->Value, value);
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI cl_set_cust_data(ICreateTypeLib2 *self, REFGUID guid, VARIANT *value)
{
    TL_LIB *lib = lib_of_create(self);
    return cust_add(lib, &lib->Cust, guid, value);
}

static HRESULT WINAPI cl_set_help_string_context(ICreateTypeLib2 *self, ULONG context)
{
    lib_of_create(self)->HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI cl_set_help_string_dll(ICreateTypeLib2 *self, LPOLESTR dll)
{
    TL_LIB *lib = lib_of_create(self);
    return set_text(lib, &lib->HelpStringDll, dll);
}

/* ------------------------------------------------------------------ the type being made */

static TYPEINFO *info_of_create(ICreateTypeInfo2 *self)
{
    return CONTAINING_RECORD(self, TYPEINFO, lpCreateVtbl);
}

static HRESULT WINAPI ci_query(ICreateTypeInfo2 *self, REFIID iid, void **out)
{
    return ((ITypeInfo2 *)info_of_create(self))->lpVtbl->QueryInterface((ITypeInfo2 *)info_of_create(self), iid, out);
}
static ULONG WINAPI ci_addref(ICreateTypeInfo2 *self)
{
    ITypeInfo2 *i = (ITypeInfo2 *)info_of_create(self);
    return i->lpVtbl->AddRef(i);
}
static ULONG WINAPI ci_release(ICreateTypeInfo2 *self)
{
    ITypeInfo2 *i = (ITypeInfo2 *)info_of_create(self);
    return i->lpVtbl->Release(i);
}

#define TYPE_OF(self) (info_of_create(self)->Type)

/* a change of the type: it has to be laid out again. */
static void touch(TL_TYPE *t)
{
    t->Layouted = FALSE;
}

static HRESULT WINAPI ci_set_guid(ICreateTypeInfo2 *self, REFGUID guid)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!guid)
        return E_INVALIDARG;
    t->Guid = *guid;
    t->HasGuid = TRUE;
    return S_OK;
}

static HRESULT WINAPI ci_set_type_flags(ICreateTypeInfo2 *self, UINT flags)
{
    TL_TYPE *t = TYPE_OF(self);
    if (flags & ~0x7FFFu)
        return E_INVALIDARG;
    t->Flags = (USHORT)flags;
    touch(t);
    /* a dual interface has the other half too. */
    ITypeInfo *info;
    if (SUCCEEDED(TlGetTypeInfo(t->Lib, t->Index, FALSE, &info)))
        info->lpVtbl->Release(info);
    return S_OK;
}

static HRESULT WINAPI ci_set_doc_string(ICreateTypeInfo2 *self, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    t->DocOff = 0xFFFFFFFF;
    return set_text(t->Lib, &t->Doc, doc);
}

static HRESULT WINAPI ci_set_help_context(ICreateTypeInfo2 *self, DWORD context)
{
    TYPE_OF(self)->HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_version(ICreateTypeInfo2 *self, WORD major, WORD minor)
{
    TL_TYPE *t = TYPE_OF(self);
    t->Major = major;
    t->Minor = minor;
    return S_OK;
}

/* the reference to a type in this library, a number for one of its own and a record of imports for any other. */
static HRESULT WINAPI ci_add_ref_type_info(ICreateTypeInfo2 *self, ITypeInfo *info, HREFTYPE *ref)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!info || !ref)
        return E_INVALIDARG;
    ITypeLib *other;
    UINT index;
    HRESULT hr = info->lpVtbl->GetContainingTypeLib(info, &other, &index);
    if (FAILED(hr))
        return hr;
    if ((TL_LIB *)other == lib) {
        other->lpVtbl->Release(other);
        *ref = TlHrefOfIndex(index);
        return S_OK;
    }
    TLIBATTR *la;
    TYPEATTR *ta;
    hr = other->lpVtbl->GetLibAttr(other, &la);
    if (FAILED(hr)) {
        other->lpVtbl->Release(other);
        return hr;
    }
    hr = info->lpVtbl->GetTypeAttr(info, &ta);
    if (FAILED(hr)) {
        other->lpVtbl->ReleaseTLibAttr(other, la);
        other->lpVtbl->Release(other);
        return hr;
    }
    BOOL by_guid = !IsEqualGUID(&ta->guid, &GUID_NULL);
    EnterCriticalSection(&lib->Lock);
    ULONG found = lib->ImportCount;
    for (ULONG i = 0; i < lib->ImportCount; i++) {
        TL_IMPORT *imp = &lib->Imports[i];
        if (IsEqualGUID(&imp->Lib, &la->guid) && imp->Major == la->wMajorVerNum && imp->Minor == la->wMinorVerNum &&
            imp->ByGuid == by_guid && (by_guid ? IsEqualGUID(&imp->Type, &ta->guid) : imp->Index == index)) {
            found = i;
            break;
        }
    }
    if (found == lib->ImportCount) {
        TL_IMPORT *list = TlAlloc(lib, (SIZE_T)(lib->ImportCount + 1) * sizeof(TL_IMPORT));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            if (lib->ImportCount)
                memcpy(list, lib->Imports, lib->ImportCount * sizeof(TL_IMPORT));
            TL_IMPORT *imp = &list[lib->ImportCount];
            memset(imp, 0, sizeof(*imp));
            imp->Lib = la->guid;
            imp->Lcid = la->lcid;
            imp->Major = la->wMajorVerNum;
            imp->Minor = la->wMinorVerNum;
            imp->ByGuid = by_guid;
            imp->Type = ta->guid;
            imp->Index = index;
            imp->Kind = ta->typekind;
            BSTR path = nullptr;
            if (((TL_LIB *)other)->lpVtbl == lib->lpVtbl && ((TL_LIB *)other)->Path) {
                const WCHAR *p = ((TL_LIB *)other)->Path;
                const WCHAR *slash = wcsrchr(p, L'\\');
                imp->File = TlStrDup(lib, slash ? slash + 1 : p);
            } else if (SUCCEEDED(QueryPathOfRegTypeLib(&la->guid, la->wMajorVerNum, la->wMinorVerNum, la->lcid, &path))) {
                const WCHAR *slash = wcsrchr(path, L'\\');
                imp->File = TlStrDup(lib, slash ? slash + 1 : path);
            }
            SysFreeString(path);
            lib->Imports = list;
            found = lib->ImportCount++;
        }
    }
    LeaveCriticalSection(&lib->Lock);
    info->lpVtbl->ReleaseTypeAttr(info, ta);
    other->lpVtbl->ReleaseTLibAttr(other, la);
    other->lpVtbl->Release(other);
    if (FAILED(hr))
        return hr;
    *ref = (HREFTYPE)(found * 12) | 1;
    return S_OK;
}

static BOOL variant_type_ok(VARTYPE vt)
{
    return vt <= VT_DECIMAL || (vt >= VT_I1 && vt <= VT_UINT) || vt == VT_HRESULT || vt == VT_PTR || vt == VT_SAFEARRAY ||
           vt == VT_CARRAY || vt == VT_USERDEFINED || vt == VT_LPSTR || vt == VT_LPWSTR || vt == VT_VOID || vt == VT_INT_PTR ||
           vt == VT_UINT_PTR || vt == VT_RECORD;
}

static HRESULT copy_params(TL_LIB *lib, TL_FUNC *f, const FUNCDESC *fd)
{
    f->ParamCount = (USHORT)fd->cParams;
    f->Params = fd->cParams ? TlAlloc(lib, (SIZE_T)fd->cParams * sizeof(TL_PARAM)) : nullptr;
    if (fd->cParams && !f->Params)
        return E_OUTOFMEMORY;
    for (SHORT i = 0; i < fd->cParams; i++) {
        const ELEMDESC *e = &fd->lprgelemdescParam[i];
        if (!variant_type_ok(e->tdesc.vt))
            return E_INVALIDARG;
        TL_PARAM *p = &f->Params[i];
        TlCopyTypeDesc(lib, &p->Type, &e->tdesc);
        p->Flags = e->paramdesc.wParamFlags;
        if ((p->Flags & PARAMFLAG_FHASDEFAULT) && e->paramdesc.pparamdescex) {
            HRESULT hr = TlVariantCopy(lib, &p->Default, &e->paramdesc.pparamdescex->varDefaultValue);
            if (FAILED(hr))
                return hr;
        } else {
            p->Flags &= ~PARAMFLAG_FHASDEFAULT;
        }
    }
    return S_OK;
}

static HRESULT WINAPI ci_add_func_desc(ICreateTypeInfo2 *self, UINT index, FUNCDESC *fd)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!fd)
        return E_INVALIDARG;
    if (t->Kind != TKIND_INTERFACE && t->Kind != TKIND_DISPATCH && t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    if (index > t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (fd->cParams < 0 || (fd->cParams && !fd->lprgelemdescParam) || !variant_type_ok(fd->elemdescFunc.tdesc.vt))
        return E_INVALIDARG;
    if (fd->invkind != INVOKE_FUNC && fd->invkind != INVOKE_PROPERTYGET && fd->invkind != INVOKE_PROPERTYPUT &&
        fd->invkind != INVOKE_PROPERTYPUTREF)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Id == fd->memid && fd->memid != MEMBERID_NIL &&
            (t->Funcs[i].Invoke == fd->invkind || t->Funcs[i].Invoke == INVOKE_FUNC || fd->invkind == INVOKE_FUNC)) {
            hr = TYPE_E_DUPLICATEID;
            break;
        }
    for (UINT i = 0; i < t->VarCount && SUCCEEDED(hr); i++)
        if (t->Vars[i].Id == fd->memid && fd->memid != MEMBERID_NIL)
            hr = TYPE_E_DUPLICATEID;
    if (SUCCEEDED(hr)) {
        TL_FUNC *list = TlAlloc(lib, (SIZE_T)(t->FuncCount + 1) * sizeof(TL_FUNC));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            memcpy(list, t->Funcs, index * sizeof(TL_FUNC));
            memcpy(list + index + 1, t->Funcs + index, (t->FuncCount - index) * sizeof(TL_FUNC));
            TL_FUNC *f = &list[index];
            memset(f, 0, sizeof(*f));
            f->Id = fd->memid;
            f->Kind = fd->funckind;
            f->Invoke = fd->invkind;
            f->Conv = fd->callconv;
            f->Flags = fd->wFuncFlags;
            f->VtOffset = fd->oVft;
            f->VtOffsetSet = fd->oVft != 0;
            f->OptCount = fd->cParamsOpt;
            TlCopyTypeDesc(lib, &f->Ret, &fd->elemdescFunc.tdesc);
            hr = copy_params(lib, f, fd);
            if (SUCCEEDED(hr) && fd->cScodes > 0 && fd->lprgscode) {
                f->Scodes = TlAlloc(lib, (SIZE_T)fd->cScodes * sizeof(SCODE));
                if (f->Scodes) {
                    memcpy(f->Scodes, fd->lprgscode, fd->cScodes * sizeof(SCODE));
                    f->ScodeCount = fd->cScodes;
                }
            }
            if (SUCCEEDED(hr)) {
                t->Funcs = list;
                t->FuncCount++;
                touch(t);
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT WINAPI ci_add_impl_type(ICreateTypeInfo2 *self, UINT index, HREFTYPE ref)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (t->Kind != TKIND_INTERFACE && t->Kind != TKIND_DISPATCH && t->Kind != TKIND_COCLASS)
        return TYPE_E_WRONGTYPEKIND;
    if (ref == 0xFFFFFFFF || ref == (HREFTYPE)-2)
        return E_INVALIDARG;
    if (t->Kind != TKIND_COCLASS && (index != 0 || t->ImplCount))
        return t->ImplCount ? TYPE_E_ELEMENTNOTFOUND : TYPE_E_ELEMENTNOTFOUND;
    if (index > t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    ITypeInfo *target;
    HRESULT hr = TlResolveHref(t, ref & ~(HREFTYPE)2, &target);
    if (FAILED(hr))
        return hr;
    TYPEATTR *ta;
    if (SUCCEEDED(target->lpVtbl->GetTypeAttr(target, &ta))) {
        if (IsEqualGUID(&ta->guid, &IID_IDispatch) && lib->DispatchRef == 0xFFFFFFFF)
            lib->DispatchRef = ref & ~(HREFTYPE)2;
        target->lpVtbl->ReleaseTypeAttr(target, ta);
    }
    target->lpVtbl->Release(target);
    EnterCriticalSection(&lib->Lock);
    TL_IMPL *list = TlAlloc(lib, (SIZE_T)(t->ImplCount + 1) * sizeof(TL_IMPL));
    if (!list) {
        LeaveCriticalSection(&lib->Lock);
        return E_OUTOFMEMORY;
    }
    memcpy(list, t->Impls, index * sizeof(TL_IMPL));
    memcpy(list + index + 1, t->Impls + index, (t->ImplCount - index) * sizeof(TL_IMPL));
    memset(&list[index], 0, sizeof(TL_IMPL));
    list[index].Ref = ref & ~(HREFTYPE)2;
    t->Impls = list;
    t->ImplCount++;
    touch(t);
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_impl_type_flags(ICreateTypeInfo2 *self, UINT index, INT flags)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (flags & ~0xF)
        return E_INVALIDARG;
    t->Impls[index].Flags = flags;
    return S_OK;
}

static HRESULT WINAPI ci_set_alignment(ICreateTypeInfo2 *self, WORD alignment)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!alignment || alignment > 16 || (alignment & (alignment - 1)))
        return E_INVALIDARG;
    t->Pack = alignment;
    t->PackSet = TRUE;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_set_schema(ICreateTypeInfo2 *self, LPOLESTR schema)
{
    TL_TYPE *t = TYPE_OF(self);
    return set_text(t->Lib, &t->Schema, schema);
}

static BOOL variant_const_ok(VARTYPE vt)
{
    switch (vt) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BSTR: case VT_ERROR: case VT_BOOL:
    case VT_I1: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT: case VT_DECIMAL:
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI ci_add_var_desc(ICreateTypeInfo2 *self, UINT index, VARDESC *vd)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!vd)
        return E_INVALIDARG;
    if (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_COCLASS || t->Kind == TKIND_ALIAS)
        return TYPE_E_BADMODULEKIND;
    if (index > t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!variant_type_ok(vd->elemdescVar.tdesc.vt) || (int)vd->varkind > VAR_DISPATCH)
        return E_INVALIDARG;
    if (vd->varkind == VAR_CONST && (!vd->lpvarValue || !variant_const_ok(vd->lpvarValue->vt)))
        return vd->lpvarValue ? TYPE_E_TYPEMISMATCH : E_INVALIDARG;
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Id == vd->memid && vd->memid != MEMBERID_NIL && t->Kind != TKIND_RECORD && t->Kind != TKIND_UNION)
            hr = TYPE_E_DUPLICATEID;
    for (UINT i = 0; i < t->FuncCount && SUCCEEDED(hr); i++)
        if (t->Funcs[i].Id == vd->memid && vd->memid != MEMBERID_NIL)
            hr = TYPE_E_DUPLICATEID;
    if (SUCCEEDED(hr)) {
        TL_VAR *list = TlAlloc(lib, (SIZE_T)(t->VarCount + 1) * sizeof(TL_VAR));
        if (!list) {
            hr = E_OUTOFMEMORY;
        } else {
            memcpy(list, t->Vars, index * sizeof(TL_VAR));
            memcpy(list + index + 1, t->Vars + index, (t->VarCount - index) * sizeof(TL_VAR));
            TL_VAR *v = &list[index];
            memset(v, 0, sizeof(*v));
            v->Id = vd->memid;
            v->Kind = vd->varkind;
            v->Flags = vd->wVarFlags;
            TlCopyTypeDesc(lib, &v->Type, &vd->elemdescVar.tdesc);
            if (vd->varkind == VAR_CONST)
                hr = TlVariantCopy(lib, &v->Value, vd->lpvarValue);
            else
                v->Offset = vd->oInst;
            if (SUCCEEDED(hr)) {
                t->Vars = list;
                t->VarCount++;
                touch(t);
            }
        }
    }
    LeaveCriticalSection(&lib->Lock);
    return hr;
}

static HRESULT check_names_unique(TL_TYPE *t, UINT skip, const WCHAR *name, INVOKEKIND kind)
{
    for (UINT i = 0; i < t->FuncCount; i++) {
        if (i == skip || !t->Funcs[i].Name || _wcsicmp(t->Funcs[i].Name, name))
            continue;
        /* the accessors of a property share a name, a function does not share it with anything. */
        if (kind == INVOKE_FUNC || t->Funcs[i].Invoke == INVOKE_FUNC || t->Funcs[i].Invoke == kind)
            return TYPE_E_AMBIGUOUSNAME;
    }
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    return S_OK;
}

static HRESULT WINAPI ci_set_func_and_param_names(ICreateTypeInfo2 *self, UINT index, LPOLESTR *names, UINT count)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (!names || !count)
        return E_INVALIDARG;
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    TL_FUNC *f = &t->Funcs[index];
    if (count > (UINT)f->ParamCount + 1)
        return TYPE_E_ELEMENTNOTFOUND;
    for (UINT i = 0; i < count; i++)
        if (names[i] && !valid_name(names[i]))
            return E_INVALIDARG;
    HRESULT hr = S_OK;
    if (names[0])
        hr = check_names_unique(t, index, names[0], f->Invoke);
    for (UINT i = 1; i < count && SUCCEEDED(hr); i++)
        for (UINT j = 0; j < i; j++)
            if (names[i] && names[j] && j > 0 && !_wcsicmp(names[i], names[j]))
                hr = TYPE_E_AMBIGUOUSNAME;
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&lib->Lock);
    if (names[0])
        f->Name = TlStrDup(lib, names[0]);
    for (UINT i = 1; i < count; i++)
        f->Params[i - 1].Name = names[i] ? TlStrDup(lib, names[i]) : nullptr;
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_var_name(ICreateTypeInfo2 *self, UINT index, LPOLESTR name)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!valid_name(name))
        return E_INVALIDARG;
    for (UINT i = 0; i < t->VarCount; i++)
        if (i != index && t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Name && !_wcsicmp(t->Funcs[i].Name, name))
            return TYPE_E_AMBIGUOUSNAME;
    EnterCriticalSection(&t->Lib->Lock);
    t->Vars[index].Name = TlStrDup(t->Lib, name);
    LeaveCriticalSection(&t->Lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_type_desc_alias(ICreateTypeInfo2 *self, TYPEDESC *desc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (t->Kind != TKIND_ALIAS)
        return TYPE_E_WRONGTYPEKIND;
    if (!desc || !variant_type_ok(desc->vt))
        return E_INVALIDARG;
    EnterCriticalSection(&t->Lib->Lock);
    TlCopyTypeDesc(t->Lib, &t->Alias, desc);
    touch(t);
    LeaveCriticalSection(&t->Lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_define_func_as_dll_entry(ICreateTypeInfo2 *self, UINT index, LPOLESTR dll, LPOLESTR proc)
{
    TL_TYPE *t = TYPE_OF(self);
    TL_LIB *lib = t->Lib;
    if (t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    if (!dll || !proc)
        return E_INVALIDARG;
    TL_FUNC *f = &t->Funcs[index];
    EnterCriticalSection(&lib->Lock);
    f->DllName = TlStrDup(lib, dll);
    if (proc[0] == L'#') {
        f->HasOrdinal = TRUE;
        f->Ordinal = 0;
        for (const WCHAR *p = proc + 1; *p >= L'0' && *p <= L'9'; p++)
            f->Ordinal = (USHORT)(f->Ordinal * 10 + (*p - L'0'));
        f->EntryName = nullptr;
    } else {
        f->HasOrdinal = FALSE;
        f->EntryName = TlStrDup(lib, proc);
    }
    if (!t->DllName)
        t->DllName = f->DllName;
    LeaveCriticalSection(&lib->Lock);
    return S_OK;
}

static HRESULT WINAPI ci_set_func_doc_string(ICreateTypeInfo2 *self, UINT index, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return set_text(t->Lib, &t->Funcs[index].Doc, doc);
}

static HRESULT WINAPI ci_set_var_doc_string(ICreateTypeInfo2 *self, UINT index, LPOLESTR doc)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return set_text(t->Lib, &t->Vars[index].Doc, doc);
}

static HRESULT WINAPI ci_set_func_help_context(ICreateTypeInfo2 *self, UINT index, DWORD context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Funcs[index].HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_var_help_context(ICreateTypeInfo2 *self, UINT index, DWORD context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Vars[index].HelpContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_mops(ICreateTypeInfo2 *self, UINT index, BSTR mops)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount && index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return S_OK;
}

static HRESULT WINAPI ci_set_type_idldesc(ICreateTypeInfo2 *self, IDLDESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    TYPE_OF(self)->IdlFlags = desc->wIDLFlags;
    return S_OK;
}

static HRESULT WINAPI ci_lay_out(ICreateTypeInfo2 *self)
{
    TL_TYPE *t = TYPE_OF(self);
    t->Layouted = FALSE;
    return TlEnsureLayout(t);
}

static HRESULT WINAPI ci_delete_func_desc(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Funcs[index], &t->Funcs[index + 1], (t->FuncCount - index - 1) * sizeof(TL_FUNC));
    t->FuncCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_delete_func_desc_by_memid(ICreateTypeInfo2 *self, MEMBERID id, INVOKEKIND kind)
{
    TL_TYPE *t = TYPE_OF(self);
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Id == id && t->Funcs[i].Invoke == kind)
            return ci_delete_func_desc(self, i);
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI ci_delete_var_desc(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Vars[index], &t->Vars[index + 1], (t->VarCount - index - 1) * sizeof(TL_VAR));
    t->VarCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_delete_var_desc_by_memid(ICreateTypeInfo2 *self, MEMBERID id)
{
    TL_TYPE *t = TYPE_OF(self);
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Id == id)
            return ci_delete_var_desc(self, i);
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI ci_delete_impl_type(ICreateTypeInfo2 *self, UINT index)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    memmove(&t->Impls[index], &t->Impls[index + 1], (t->ImplCount - index - 1) * sizeof(TL_IMPL));
    t->ImplCount--;
    touch(t);
    return S_OK;
}

static HRESULT WINAPI ci_set_cust_data(ICreateTypeInfo2 *self, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    return cust_add(t->Lib, &t->Cust, guid, value);
}

static HRESULT WINAPI ci_set_func_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Funcs[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_param_cust_data(ICreateTypeInfo2 *self, UINT func, UINT param, REFGUID guid,
                                             VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (func >= t->FuncCount || param >= t->Funcs[func].ParamCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Funcs[func].Params[param].Cust, guid, value);
}

static HRESULT WINAPI ci_set_var_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Vars[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_impl_type_cust_data(ICreateTypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_add(t->Lib, &t->Impls[index].Cust, guid, value);
}

static HRESULT WINAPI ci_set_help_string_context(ICreateTypeInfo2 *self, ULONG context)
{
    TYPE_OF(self)->HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_func_help_string_context(ICreateTypeInfo2 *self, UINT index, ULONG context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Funcs[index].HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_set_var_help_string_context(ICreateTypeInfo2 *self, UINT index, ULONG context)
{
    TL_TYPE *t = TYPE_OF(self);
    if (index >= t->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    t->Vars[index].HelpStringContext = context;
    return S_OK;
}

static HRESULT WINAPI ci_invalidate(ICreateTypeInfo2 *self)
{
    touch(TYPE_OF(self));
    return S_OK;
}

static HRESULT WINAPI ci_set_name(ICreateTypeInfo2 *self, LPOLESTR name)
{
    TL_TYPE *t = TYPE_OF(self);
    if (!valid_name(name))
        return E_INVALIDARG;
    if (type_named(t->Lib, name, t))
        return TYPE_E_NAMECONFLICT;
    t->Name = TlStrDup(t->Lib, name);
    return t->Name ? S_OK : E_OUTOFMEMORY;
}

const ICreateTypeInfo2Vtbl g_createinfo_vtbl = {
    ci_query, ci_addref, ci_release, ci_set_guid, ci_set_type_flags, ci_set_doc_string, ci_set_help_context,
    ci_set_version, ci_add_ref_type_info, ci_add_func_desc, ci_add_impl_type, ci_set_impl_type_flags, ci_set_alignment,
    ci_set_schema, ci_add_var_desc, ci_set_func_and_param_names, ci_set_var_name, ci_set_type_desc_alias,
    ci_define_func_as_dll_entry, ci_set_func_doc_string, ci_set_var_doc_string, ci_set_func_help_context,
    ci_set_var_help_context, ci_set_mops, ci_set_type_idldesc, ci_lay_out, ci_delete_func_desc,
    ci_delete_func_desc_by_memid, ci_delete_var_desc, ci_delete_var_desc_by_memid, ci_delete_impl_type, ci_set_cust_data,
    ci_set_func_cust_data, ci_set_param_cust_data, ci_set_var_cust_data, ci_set_impl_type_cust_data,
    ci_set_help_string_context, ci_set_func_help_string_context, ci_set_var_help_string_context, ci_invalidate,
    ci_set_name};

/* ------------------------------------------------------------------ writing the file */

typedef struct {
    BYTE *Data;
    SIZE_T Length, Capacity;
    BOOL Failed;
} BUF;

static BYTE *buf_add(BUF *b, SIZE_T n)
{
    if (b->Failed)
        return nullptr;
    if (b->Length + n > b->Capacity) {
        SIZE_T capacity = b->Capacity ? b->Capacity * 2 : 4096;
        while (capacity < b->Length + n)
            capacity *= 2;
        BYTE *data = b->Data ? HeapReAlloc(GetProcessHeap(), 0, b->Data, capacity) : AutAlloc(capacity);
        if (!data) {
            b->Failed = TRUE;
            return nullptr;
        }
        b->Data = data;
        b->Capacity = capacity;
    }
    BYTE *p = b->Data + b->Length;
    memset(p, 0, n);
    b->Length += n;
    return p;
}

static void buf_u32(BUF *b, ULONG v)
{
    BYTE *p = buf_add(b, 4);
    if (p)
        memcpy(p, &v, 4);
}

static void buf_u16(BUF *b, USHORT v)
{
    BYTE *p = buf_add(b, 2);
    if (p)
        memcpy(p, &v, 2);
}

static void buf_pad(BUF *b, ULONG boundary, BYTE fill)
{
    while (b->Length % boundary) {
        BYTE *p = buf_add(b, 1);
        if (p)
            *p = fill;
    }
}

static void buf_free(BUF *b)
{
    AutFree(b->Data);
    memset(b, 0, sizeof(*b));
}

static void patch32(BUF *b, SIZE_T at, ULONG v)
{
    if (b->Data && at + 4 <= b->Length)
        memcpy(b->Data + at, &v, 4);
}

static ULONG get32(const BUF *b, SIZE_T at)
{
    ULONG v = 0;
    if (b->Data && at + 4 <= b->Length)
        memcpy(&v, b->Data + at, 4);
    return v;
}

typedef struct {
    ULONG Offset;
    WCHAR *Text;
    ULONG Next;
} NAMEREC;

typedef struct {
    TL_LIB *Lib;
    BUF Types, Members, Guids, Names, Strings, Typdesc, Arrays, CustData, CdGuid, Refs, ImpInfo, ImpFiles;
    ULONG GuidBucket[32];
    ULONG NameBucket[128];
    NAMEREC *NameList;
    ULONG NameCount, NameCapacity;
    ULONG NameChars;
    ULONG *NameHeads;
    HRESULT Status;
} WRITER;

static ULONG name_hash(WRITER *w, const WCHAR *text)
{
    return LHashValOfNameSys(w->Lib->Syskind, w->Lib->Lcid, text) & 0xFFFF;
}

static int to_ansi(WRITER *w, const WCHAR *text, char *out, int size)
{
    int n = WideCharToMultiByte(w->Lib->CodePage, 0, text, -1, out, size, nullptr, nullptr);
    return n > 0 ? n - 1 : 0;
}

static ULONG add_name(WRITER *w, const WCHAR *text, ULONG href)
{
    if (!text)
        return 0xFFFFFFFF;
    ULONG hash = name_hash(w, text);
    ULONG bucket = hash & 0x7F;
    for (ULONG at = w->NameBucket[bucket]; at != 0xFFFFFFFF; at = get32(&w->Names, at + 4)) {
        /* the names in a chain are told apart by their text, ignoring case. */
        for (ULONG i = 0; i < w->NameCount; i++)
            if (w->NameList[i].Offset == at) {
                if (!_wcsicmp(w->NameList[i].Text, text)) {
                    if (href != 0xFFFFFFFF && get32(&w->Names, at) == 0xFFFFFFFF)
                        patch32(&w->Names, at, href);
                    return at;
                }
                break;
            }
    }
    char ansi[1100];
    int length = to_ansi(w, text, ansi, sizeof(ansi));
    if (length > 0x3FF)
        length = 0x3FF;
    ULONG at = (ULONG)w->Names.Length;
    buf_u32(&w->Names, href);
    buf_u32(&w->Names, w->NameBucket[bucket]);
    buf_u32(&w->Names, (hash << 16) | 0x3800 | (ULONG)length);
    BYTE *p = buf_add(&w->Names, length);
    if (p)
        memcpy(p, ansi, length);
    buf_pad(&w->Names, 4, 0x57);
    w->NameBucket[bucket] = at;
    if (w->NameCount == w->NameCapacity) {
        ULONG capacity = w->NameCapacity ? w->NameCapacity * 2 : 64;
        NAMEREC *list = AutAlloc(capacity * sizeof(NAMEREC));
        if (!list) {
            w->Status = E_OUTOFMEMORY;
            return at;
        }
        if (w->NameList) {
            memcpy(list, w->NameList, w->NameCount * sizeof(NAMEREC));
            AutFree(w->NameList);
        }
        w->NameList = list;
        w->NameCapacity = capacity;
    }
    w->NameList[w->NameCount].Offset = at;
    w->NameList[w->NameCount].Text = (WCHAR *)text;
    w->NameCount++;
    w->NameChars += length;
    return at;
}

static ULONG add_string(WRITER *w, const WCHAR *text)
{
    if (!text)
        return 0xFFFFFFFF;
    char ansi[1100];
    int length = to_ansi(w, text, ansi, sizeof(ansi));
    if (length > 0xFFFF)
        length = 0xFFFF;
    /* the same string is kept once. */
    for (ULONG at = 0; at < w->Strings.Length;) {
        USHORT l;
        memcpy(&l, w->Strings.Data + at, 2);
        if (l == length && !memcmp(w->Strings.Data + at + 2, ansi, length))
            return at;
        at += (2 + l + 3) & ~3u;
    }
    ULONG at = (ULONG)w->Strings.Length;
    buf_u16(&w->Strings, (USHORT)length);
    BYTE *p = buf_add(&w->Strings, length);
    if (p)
        memcpy(p, ansi, length);
    buf_pad(&w->Strings, 4, 0x57);
    return at;
}

static ULONG guid_hash(const GUID *guid)
{
    const ULONG *d = (const ULONG *)guid;
    ULONG v = d[0] ^ d[1] ^ d[2] ^ d[3];
    return ((v & 0xFFFF) ^ (v >> 16)) % 32;
}

static ULONG add_guid(WRITER *w, const GUID *guid, ULONG href)
{
    ULONG bucket = guid_hash(guid);
    for (ULONG at = w->GuidBucket[bucket]; at != 0xFFFFFFFF; at = get32(&w->Guids, at + 20)) {
        if (!memcmp(w->Guids.Data + at, guid, 16)) {
            if (href != 0xFFFFFFFF && get32(&w->Guids, at + 16) == 0xFFFFFFFF)
                patch32(&w->Guids, at + 16, href);
            return at;
        }
    }
    ULONG at = (ULONG)w->Guids.Length;
    BYTE *p = buf_add(&w->Guids, 16);
    if (p)
        memcpy(p, guid, 16);
    buf_u32(&w->Guids, href);
    buf_u32(&w->Guids, w->GuidBucket[bucket]);
    w->GuidBucket[bucket] = at;
    return at;
}

static ULONG file_href(ULONG href)
{
    if (href == 0xFFFFFFFF)
        return href;
    if (href & 1)
        return href & ~2u;
    ULONG index;
    if (!TlIndexOfHref(href, &index))
        return 0xFFFFFFFF;
    return index * 100;
}

static BOOL simple_vt(VARTYPE vt)
{
    return vt != VT_PTR && vt != VT_SAFEARRAY && vt != VT_CARRAY && vt != VT_USERDEFINED;
}

/* a type description as the file holds it: in the reference itself when it is a plain type, else in the table. */
static ULONG enc_typedesc(WRITER *w, const TYPEDESC *td)
{
    if (simple_vt(td->vt))
        return 0x80000000u | ((ULONG)(0x8000 | td->vt) << 16) | td->vt;
    ULONG data, word = 0x7FFF;
    switch (td->vt) {
    case VT_PTR:
    case VT_SAFEARRAY:
        data = enc_typedesc(w, td->lptdesc);
        if (simple_vt(td->lptdesc->vt))
            word = (td->vt == VT_PTR ? 0x4000 : 0x2000) | td->lptdesc->vt;
        else
            word = td->vt == VT_PTR ? 0x7FFF : 0x7FFE;
        break;
    case VT_CARRAY: {
        const ARRAYDESC *a = td->lpadesc;
        ULONG elem = enc_typedesc(w, &a->tdescElem);
        data = (ULONG)w->Arrays.Length;
        buf_u32(&w->Arrays, elem);
        buf_u16(&w->Arrays, a->cDims);
        buf_u16(&w->Arrays, (USHORT)(a->cDims * 8));
        for (USHORT i = 0; i < a->cDims; i++) {
            buf_u32(&w->Arrays, a->rgbounds[i].cElements);
            buf_u32(&w->Arrays, (ULONG)a->rgbounds[i].lLbound);
        }
        word = 0x7FFE;
        break;
    }
    default:
        data = file_href(td->hreftype & 0xFFFFFF);
        break;
    }
    /* the same description is kept once. */
    for (ULONG at = 0; at + 8 <= w->Typdesc.Length; at += 8) {
        USHORT v, f;
        ULONG d;
        memcpy(&v, w->Typdesc.Data + at, 2);
        memcpy(&f, w->Typdesc.Data + at + 2, 2);
        memcpy(&d, w->Typdesc.Data + at + 4, 4);
        if (v == td->vt && f == word && d == data)
            return at;
    }
    ULONG at = (ULONG)w->Typdesc.Length;
    buf_u16(&w->Typdesc, td->vt);
    buf_u16(&w->Typdesc, (USHORT)word);
    buf_u32(&w->Typdesc, data);
    return at;
}

/* a constant: in the reference when it is small, else in the segment of custom data. */
static ULONG enc_const(WRITER *w, const VARIANT *v)
{
    switch (v->vt) {
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        return 0x80000000u | ((ULONG)v->vt << 26) | v->uiVal;
    case VT_I1:
    case VT_UI1:
        return 0x80000000u | ((ULONG)v->vt << 26) | v->bVal;
    case VT_I4:
    case VT_INT:
        if (v->lVal >= 0 && v->lVal < 0x4000000)
            return 0x80000000u | ((ULONG)v->vt << 26) | (ULONG)v->lVal;
        break;
    case VT_UI4:
    case VT_UINT:
    case VT_ERROR:
        if (v->ulVal < 0x4000000)
            return 0x80000000u | ((ULONG)v->vt << 26) | v->ulVal;
        break;
    default:
        break;
    }
    ULONG at = (ULONG)w->CustData.Length;
    switch (v->vt) {
    case VT_I1:
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: {
        /* these come through the reference only: a different type is stored widened. */
        break;
    }
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
        buf_u16(&w->CustData, v->vt);
        buf_u32(&w->CustData, v->ulVal);
        break;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8: {
        buf_u16(&w->CustData, v->vt);
        BYTE *p = buf_add(&w->CustData, 8);
        if (p)
            memcpy(p, &v->llVal, 8);
        break;
    }
    case VT_DECIMAL: {
        DECIMAL d = v->decVal;
        d.wReserved = VT_DECIMAL;
        BYTE *p = buf_add(&w->CustData, 16);
        if (p)
            memcpy(p, &d, 16);
        break;
    }
    case VT_BSTR: {
        buf_u16(&w->CustData, VT_BSTR);
        if (!v->bstrVal) {
            buf_u32(&w->CustData, 0xFFFFFFFF);
        } else {
            char ansi[2048];
            int n = WideCharToMultiByte(w->Lib->CodePage, 0, v->bstrVal, SysStringLen(v->bstrVal), ansi, sizeof(ansi),
                                        nullptr, nullptr);
            buf_u32(&w->CustData, n);
            BYTE *p = buf_add(&w->CustData, n);
            if (p)
                memcpy(p, ansi, n);
        }
        break;
    }
    default:
        break;
    }
    buf_pad(&w->CustData, 4, 0x57);
    return at;
}

static ULONG enc_cust(WRITER *w, TL_CUST *list)
{
    ULONG head = 0xFFFFFFFF;
    /* the chain is written from its end so that every item knows the next. */
    TL_CUST *items[64];
    int n = 0;
    for (TL_CUST *c = list; c && n < 64; c = c->Next)
        items[n++] = c;
    for (int i = n - 1; i >= 0; i--) {
        ULONG guid = add_guid(w, &items[i]->Guid, 0xFFFFFFFF);
        ULONG value = enc_const(w, &items[i]->Value);
        ULONG at = (ULONG)w->CdGuid.Length;
        buf_u32(&w->CdGuid, guid);
        buf_u32(&w->CdGuid, value);
        buf_u32(&w->CdGuid, head);
        head = at;
    }
    return head;
}

static ULONG funcdesc_size(WRITER *w, const TL_FUNC *f)
{
    SYSKIND k = w->Lib->Syskind;
    ULONG fd = k == SYS_WIN64 ? 88 : 52, ed = k == SYS_WIN64 ? 32 : 16, td = k == SYS_WIN64 ? 16 : 8;
    ULONG ex = k == SYS_WIN64 ? 32 : 24;
    ULONG size = fd + f->ParamCount * ed;
    TYPEDESC stack[1];
    (void)stack;
    for (USHORT i = 0; i < f->ParamCount; i++) {
        const TYPEDESC *t = &f->Params[i].Type;
        while (t->vt == VT_PTR || t->vt == VT_SAFEARRAY) {
            size += td;
            t = t->lptdesc;
        }
        if (f->Params[i].Flags & PARAMFLAG_FHASDEFAULT)
            size += ex;
    }
    const TYPEDESC *t = &f->Ret;
    while (t->vt == VT_PTR || t->vt == VT_SAFEARRAY) {
        size += td;
        t = t->lptdesc;
    }
    return size;
}

static void write_members(WRITER *w, TL_TYPE *t, ULONG *memoff)
{
    ULONG cf = t->FuncCount, cv = t->VarCount, total = cf + cv;
    *memoff = (ULONG)w->Members.Length;
    if (!total)
        return;
    BUF recs = {0};
    ULONG *offsets = AutAllocZero(total * 3 * sizeof(ULONG));
    if (!offsets) {
        w->Status = E_OUTOFMEMORY;
        return;
    }
    ULONG *memids = offsets, *names = offsets + total, *where = offsets + 2 * total;
    for (ULONG k = 0; k < total; k++) {
        where[k] = (ULONG)recs.Length;
        if (k < cf) {
            TL_FUNC *f = &t->Funcs[k];
            memids[k] = (ULONG)f->Id;
            names[k] = add_name(w, f->Name, 0xFFFFFFFF);
            BOOL defaults = FALSE, cust = f->Cust != nullptr;
            ULONG hidden = 0;
            for (USHORT j = 0; j < f->ParamCount; j++) {
                if (f->Params[j].Flags & PARAMFLAG_FHASDEFAULT)
                    defaults = TRUE;
                if (f->Params[j].Flags & (PARAMFLAG_FLCID | PARAMFLAG_FRETVAL))
                    hidden++;
                if (f->Params[j].Cust)
                    cust = TRUE;
            }
            ULONG nargs = f->ParamCount;
            ULONG size = 24 + 24 + (cust ? 4 * (nargs + 2) : 0) + nargs * (defaults ? 16 : 12);
            ULONG fk = (ULONG)f->Kind | ((ULONG)f->Invoke << 3) | (cust ? 0x80 : 0) | ((ULONG)f->Conv << 8) |
                       (defaults ? 0x1000 : 0) | (f->HasOrdinal ? 0x2000 : 0) | ((hidden > 3 ? 3 : hidden) << 14) | (k << 16);
            buf_u32(&recs, size | (k << 16));
            buf_u32(&recs, enc_typedesc(w, &f->Ret));
            buf_u32(&recs, f->Flags);
            buf_u32(&recs, (USHORT)f->VtOffset | (funcdesc_size(w, f) << 16));
            buf_u32(&recs, fk);
            buf_u32(&recs, nargs | ((ULONG)(USHORT)f->OptCount << 16));
            buf_u32(&recs, f->HelpContext);
            buf_u32(&recs, add_string(w, f->Doc));
            buf_u32(&recs, f->HasOrdinal ? f->Ordinal : add_string(w, f->EntryName));
            buf_u32(&recs, add_string(w, f->HelpFile));
            buf_u32(&recs, add_string(w, f->DllName));
            buf_u32(&recs, f->HelpStringContext);
            if (cust) {
                buf_u32(&recs, enc_cust(w, f->Cust));
                for (ULONG j = 0; j < nargs; j++)
                    buf_u32(&recs, enc_cust(w, f->Params[j].Cust));
                buf_u32(&recs, 0xFFFFFFFF);
            }
            if (defaults)
                for (ULONG j = 0; j < nargs; j++)
                    buf_u32(&recs, (f->Params[j].Flags & PARAMFLAG_FHASDEFAULT) ? enc_const(w, &f->Params[j].Default) : 0xFFFFFFFF);
            for (ULONG j = 0; j < nargs; j++) {
                buf_u32(&recs, enc_typedesc(w, &f->Params[j].Type));
                buf_u32(&recs, add_name(w, f->Params[j].Name, 0xFFFFFFFF));
                buf_u32(&recs, f->Params[j].Flags);
            }
        } else {
            TL_VAR *v = &t->Vars[k - cf];
            memids[k] = (ULONG)v->Id;
            names[k] = add_name(w, v->Name, 0xFFFFFFFF);
            SYSKIND sk = w->Lib->Syskind;
            ULONG vd = (sk == SYS_WIN64 ? 64 : 36) + (sk == SYS_WIN64 ? 24 : 16) * 0;
            buf_u32(&recs, 44 | ((k - cf) << 16));
            buf_u32(&recs, enc_typedesc(w, &v->Type));
            buf_u32(&recs, v->Flags);
            buf_u32(&recs, (USHORT)v->Kind | (vd << 16));
            buf_u32(&recs, v->Kind == VAR_CONST ? enc_const(w, &v->Value) : v->Offset);
            buf_u32(&recs, v->HelpContext);
            buf_u32(&recs, add_string(w, v->Doc));
            buf_u32(&recs, 0xFFFFFFFF);
            buf_u32(&recs, enc_cust(w, v->Cust));
            buf_u32(&recs, v->HelpStringContext);
            buf_u32(&recs, 0xFFFFFFFF);
        }
    }
    buf_u32(&w->Members, (ULONG)recs.Length);
    BYTE *p = buf_add(&w->Members, recs.Length);
    if (p && recs.Data)
        memcpy(p, recs.Data, recs.Length);
    p = buf_add(&w->Members, total * 12);
    if (p)
        memcpy(p, offsets, total * 12);
    buf_free(&recs);
    AutFree(offsets);
}

static ULONG impfile_for(WRITER *w, TL_LIB *lib, const TL_IMPORT *imp, BUF *files, ULONG *cache_keys, ULONG *cache_values,
                         ULONG *cache_count)
{
    for (ULONG i = 0; i < *cache_count; i++) {
        const TL_IMPORT *other = &lib->Imports[cache_keys[i]];
        if (IsEqualGUID(&other->Lib, &imp->Lib) && other->Major == imp->Major && other->Minor == imp->Minor &&
            other->Lcid == imp->Lcid)
            return cache_values[i];
    }
    ULONG at = (ULONG)files->Length;
    ULONG guid = add_guid(w, &imp->Lib, 0xFFFFFFFF);
    buf_u32(files, guid);
    buf_u32(files, imp->Lcid);
    buf_u32(files, imp->Major | ((ULONG)imp->Minor << 16));
    char ansi[300];
    int n = imp->File ? to_ansi(w, imp->File, ansi, sizeof(ansi)) : 0;
    buf_u16(files, (USHORT)((n << 2) | 3));
    BYTE *p = buf_add(files, n);
    if (p && n)
        memcpy(p, ansi, n);
    buf_pad(files, 4, 0x57);
    cache_keys[*cache_count] = (ULONG)(imp - lib->Imports);
    cache_values[*cache_count] = at;
    (*cache_count)++;
    return at;
}

HRESULT TlSaveMsft(TL_LIB *lib, LPCOLESTR path)
{
    WRITER w;
    memset(&w, 0, sizeof(w));
    w.Lib = lib;
    w.Status = S_OK;
    memset(w.GuidBucket, 0xFF, sizeof(w.GuidBucket));
    memset(w.NameBucket, 0xFF, sizeof(w.NameBucket));
    HRESULT hr = S_OK;
    EnterCriticalSection(&lib->Lock);
    for (ULONG i = 0; i < lib->TypeCount && SUCCEEDED(hr); i++) {
        hr = TlLoadMembers(lib->Types[i]);
        if (SUCCEEDED(hr))
            hr = TlEnsureLayout(lib->Types[i]);
    }
    if (FAILED(hr))
        goto done;
    {
        ULONG count = lib->TypeCount;
        ULONG lib_name = add_name(&w, lib->Name, 0xFFFFFFFF);
        ULONG lib_guid = lib->HasGuid ? add_guid(&w, &lib->Guid, 0xFFFFFFFF) : 0xFFFFFFFF;
        ULONG lib_doc = add_string(&w, lib->Doc);
        ULONG lib_help = add_string(&w, lib->HelpFile);
        ULONG lib_help_dll = add_string(&w, lib->HelpStringDll);
        /* the names of types are known before anything is written that refers to them. */
        for (ULONG i = 0; i < count; i++)
            add_name(&w, lib->Types[i]->Name, i * 100);
        ULONG *memoffs = AutAllocZero((count + 1) * sizeof(ULONG));
        if (!memoffs) {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        for (ULONG i = 0; i < count; i++) {
            TL_TYPE *t = lib->Types[i];
            ULONG memoff;
            write_members(&w, t, &memoff);
            memoffs[i] = memoff;
            ULONG guid = t->HasGuid ? add_guid(&w, &t->Guid, i * 100) : 0xFFFFFFFF;
            ULONG d1 = 0xFFFFFFFF, d2 = 0xFFFFFFFF;
            ULONG impl_count = t->ImplCount;
            if (t->Kind == TKIND_COCLASS) {
                ULONG chain = 0xFFFFFFFF;
                for (int j = (int)t->ImplCount - 1; j >= 0; j--) {
                    ULONG at = (ULONG)w.Refs.Length;
                    buf_u32(&w.Refs, file_href(t->Impls[j].Ref));
                    buf_u32(&w.Refs, t->Impls[j].Flags);
                    buf_u32(&w.Refs, enc_cust(&w, t->Impls[j].Cust));
                    buf_u32(&w.Refs, chain);
                    chain = at;
                }
                d1 = chain;
            } else if (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_DISPATCH) {
                if (t->ImplCount)
                    d1 = file_href(t->Impls[0].Ref);
                d2 = ((ULONG)t->InheritedFuncs << 16) | t->InheritedImpls;
            } else if (t->Kind == TKIND_ALIAS) {
                d1 = enc_typedesc(&w, &t->Alias);
            } else if (t->Kind == TKIND_MODULE) {
                d1 = add_string(&w, t->DllName);
            }
            ULONG kind_word = (i << 16) | ((ULONG)(t->Alignment & 0x1F) << 11) | 0x220 | (ULONG)t->Kind |
                              ((t->Flags & TYPEFLAG_FDUAL) ? 0x10 : 0);
            buf_u32(&w.Types, kind_word);
            buf_u32(&w.Types, memoff);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 3);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, (ULONG)t->FuncCount | ((ULONG)t->VarCount << 16));
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, guid);
            buf_u32(&w.Types, t->Flags);
            buf_u32(&w.Types, add_name(&w, t->Name, i * 100));
            buf_u32(&w.Types, t->Major | ((ULONG)t->Minor << 16));
            buf_u32(&w.Types, add_string(&w, t->Doc));
            buf_u32(&w.Types, t->HelpStringContext);
            buf_u32(&w.Types, t->HelpContext);
            buf_u32(&w.Types, enc_cust(&w, t->Cust));
            buf_u32(&w.Types, impl_count | ((ULONG)t->SizeVft << 16));
            buf_u32(&w.Types, t->SizeInstance);
            buf_u32(&w.Types, d1);
            buf_u32(&w.Types, d2);
            buf_u32(&w.Types, 0);
            buf_u32(&w.Types, 0xFFFFFFFF);
        }
        /* the imports. */
        ULONG *keys = AutAllocZero((lib->ImportCount + 1) * 2 * sizeof(ULONG) + sizeof(ULONG));
        ULONG *values = keys + lib->ImportCount + 1, cached = 0;
        for (ULONG i = 0; i < lib->ImportCount; i++) {
            TL_IMPORT *imp = &lib->Imports[i];
            ULONG file = keys ? impfile_for(&w, lib, imp, &w.ImpFiles, keys, values, &cached) : 0;
            buf_u32(&w.ImpInfo, (i & 0xFFFF) | ((ULONG)(imp->ByGuid ? 1 : 0) << 16) | ((ULONG)imp->Kind << 24));
            buf_u32(&w.ImpInfo, file);
            buf_u32(&w.ImpInfo, imp->ByGuid ? add_guid(&w, &imp->Type, 0xFFFFFFFF) : imp->Index);
        }
        AutFree(keys);
        ULONG custom = enc_cust(&w, lib->Cust);

        /* the file: the header, the table of types, the directory of segments, the segments, the members. */
        BUF out = {0};
        ULONG varflags = (ULONG)(lib->Syskind & 3) | 0x40 | (lib_help_dll != 0xFFFFFFFF ? 0x100 : 0);
        ULONG pos_header = 0x54 + (lib_help_dll != 0xFFFFFFFF ? 4 : 0) + count * 4 + 240;
        struct {
            BUF *Buf;
            int Seg;
        } order[] = {{&w.Types, SEG_TYPEINFO}, {&w.ImpInfo, SEG_IMPINFO}, {&w.ImpFiles, SEG_IMPFILES}, {&w.Refs, SEG_REFTAB},
                     {nullptr, SEG_GUIDHASH}, {&w.Guids, SEG_GUID}, {nullptr, SEG_NAMEHASH}, {&w.Names, SEG_NAME},
                     {&w.Strings, SEG_STRING}, {&w.Typdesc, SEG_TYPDESC}, {&w.Arrays, SEG_ARRAYDESC},
                     {&w.CustData, SEG_CUSTDATA}, {&w.CdGuid, SEG_CDGUID}};
        ULONG seg_offset[15], seg_length[15];
        ULONG at = pos_header;
        for (int i = 0; i < 15; i++) {
            seg_offset[i] = 0xFFFFFFFF;
            seg_length[i] = 0;
        }
        for (int i = 0; i < 13; i++) {
            ULONG length = order[i].Buf ? (ULONG)order[i].Buf->Length : order[i].Seg == SEG_GUIDHASH ? 128 : 512;
            if (!length)
                continue;
            seg_offset[order[i].Seg] = at;
            seg_length[order[i].Seg] = length;
            at += length;
        }
        ULONG members_base = at;
        buf_u32(&out, 0x5446534D);
        buf_u32(&out, 0x00010002);
        buf_u32(&out, lib_guid);
        buf_u32(&out, lib->Lcid);
        buf_u32(&out, lib->Lcid);
        buf_u32(&out, varflags);
        buf_u32(&out, lib->Major | ((ULONG)lib->Minor << 16));
        buf_u32(&out, lib->Flags);
        buf_u32(&out, count);
        buf_u32(&out, lib_doc);
        buf_u32(&out, lib->HelpStringContext);
        buf_u32(&out, lib->HelpContext);
        buf_u32(&out, w.NameCount);
        buf_u32(&out, w.NameChars);
        buf_u32(&out, lib_name);
        buf_u32(&out, lib_help);
        buf_u32(&out, custom);
        buf_u32(&out, 32);
        buf_u32(&out, 128);
        buf_u32(&out, lib->DispatchRef == 0xFFFFFFFF ? 0xFFFFFFFF : file_href(lib->DispatchRef));
        buf_u32(&out, lib->ImportCount);
        if (lib_help_dll != 0xFFFFFFFF)
            buf_u32(&out, lib_help_dll);
        for (ULONG i = 0; i < count; i++)
            buf_u32(&out, i * 100);
        for (int i = 0; i < 15; i++) {
            buf_u32(&out, seg_offset[i]);
            buf_u32(&out, seg_length[i]);
            buf_u32(&out, 0xFFFFFFFF);
            buf_u32(&out, 0xF);
        }
        for (int i = 0; i < 13; i++) {
            if (order[i].Buf) {
                if (i == 0) {
                    /* the members of the types start where the segments end. */
                    for (ULONG k = 0; k < count; k++)
                        patch32(&w.Types, k * 100 + 4, members_base + memoffs[k]);
                }
                BYTE *p = buf_add(&out, order[i].Buf->Length);
                if (p && order[i].Buf->Length)
                    memcpy(p, order[i].Buf->Data, order[i].Buf->Length);
            } else if (order[i].Seg == SEG_GUIDHASH) {
                BYTE *p = buf_add(&out, 128);
                if (p)
                    memcpy(p, w.GuidBucket, 128);
            } else {
                BYTE *p = buf_add(&out, 512);
                if (p)
                    memcpy(p, w.NameBucket, 512);
            }
        }
        BYTE *p = buf_add(&out, w.Members.Length);
        if (p && w.Members.Length)
            memcpy(p, w.Members.Data, w.Members.Length);
        AutFree(memoffs);
        if (out.Failed || w.Status != S_OK) {
            hr = FAILED(w.Status) ? w.Status : E_OUTOFMEMORY;
        } else {
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                hr = STG_E_ACCESSDENIED;
            } else {
                DWORD written;
                if (!WriteFile(file, out.Data, (DWORD)out.Length, &written, nullptr) || written != out.Length)
                    hr = HRESULT_WRITEFAULT;
                CloseHandle(file);
            }
        }
        buf_free(&out);
    }
done:
    LeaveCriticalSection(&lib->Lock);
    buf_free(&w.Types);
    buf_free(&w.Members);
    buf_free(&w.Guids);
    buf_free(&w.Names);
    buf_free(&w.Strings);
    buf_free(&w.Typdesc);
    buf_free(&w.Arrays);
    buf_free(&w.CustData);
    buf_free(&w.CdGuid);
    buf_free(&w.Refs);
    buf_free(&w.ImpInfo);
    buf_free(&w.ImpFiles);
    AutFree(w.NameList);
    return hr;
}

static HRESULT WINAPI cl_save_all_changes(ICreateTypeLib2 *self)
{
    TL_LIB *lib = lib_of_create(self);
    if (!lib->Path)
        return E_INVALIDARG;
    if (!lib->HasGuid)
        return E_UNEXPECTED;
    return TlSaveMsft(lib, lib->Path);
}

const ICreateTypeLib2Vtbl g_createlib_vtbl = {
    cl_query, cl_addref, cl_release, cl_create_type_info, cl_set_name, cl_set_version, cl_set_guid, cl_set_doc_string,
    cl_set_help_file_name, cl_set_help_context, cl_set_lcid, cl_set_lib_flags, cl_save_all_changes, cl_delete_type_info,
    cl_set_cust_data, cl_set_help_string_context, cl_set_help_string_dll};

WINOLEAUTAPI HRESULT WINAPI CreateTypeLib2(SYSKIND kind, LPCOLESTR file, ICreateTypeLib2 **out)
{
    if (!file || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (kind != SYS_WIN32 && kind != SYS_WIN64 && (int)kind != -1)
        return E_INVALIDARG;
    if ((int)kind == -1)
        kind = sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32;
    TL_LIB *lib = TlLibCreate();
    if (!lib)
        return E_OUTOFMEMORY;
    lib->Writable = TRUE;
    lib->Syskind = lib->FileSyskind = kind;
    lib->Lcid = GetUserDefaultLCID();
    if (!lib->Lcid)
        lib->Lcid = 0x409;
    lib->CodePage = CP_ACP;
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(file, MAX_PATH, full, nullptr);
    lib->Path = TlStrDup(lib, n && n < MAX_PATH ? full : file);
    if (!lib->Path) {
        TlLibDestroy(lib);
        return E_OUTOFMEMORY;
    }
    *out = (ICreateTypeLib2 *)&lib->lpCreateVtbl;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI OACreateTypeLib2(SYSKIND kind, LPCOLESTR file, ICreateTypeLib2 **out)
{
    return CreateTypeLib2(kind, file, out);
}

WINOLEAUTAPI HRESULT WINAPI CreateTypeLib(SYSKIND kind, LPCOLESTR file, ICreateTypeLib **out)
{
    return CreateTypeLib2(kind, file, (ICreateTypeLib2 **)out);
}
