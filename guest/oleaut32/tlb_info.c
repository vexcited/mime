/*
 * the objects of a type of a library: ITypeInfo, ITypeInfo2 and ITypeComp, and ICreateTypeInfo2 on a library that is
 * being made. A type that is dual has two of them, the one that is seen as a dispatch interface (its functions without
 * the locale and the result parameters, as automation calls them) and the one of the virtual table.
 */
#include "typelib.h"

MIME_DEFINE_GUID(IID_MimeTypeInfo, 0x6d696d65, 0x7479, 0x7065, 0x69, 0x6e, 0x66, 0x6f, 0x00, 0x00, 0x00, 0x01);

struct TYPEINFO {
    const ITypeInfo2Vtbl *lpVtbl;
    const ITypeCompVtbl *lpCompVtbl;
    const ICreateTypeInfo2Vtbl *lpCreateVtbl;
    LONG Refs;
    TL_TYPE *Type;
    BOOL VtableView;
    TYPEINFO *Twin;
};

extern const ITypeInfo2Vtbl g_typeinfo_vtbl;
extern const ITypeCompVtbl g_typecomp_vtbl;
extern const ICreateTypeInfo2Vtbl g_createinfo_vtbl;

#define E_ELEMENTNOTFOUND TYPE_E_ELEMENTNOTFOUND

static TYPEINFO *from_info(ITypeInfo2 *i)
{
    return (TYPEINFO *)i;
}

static TYPEINFO *from_comp(ITypeComp *c)
{
    return CONTAINING_RECORD(c, TYPEINFO, lpCompVtbl);
}

TL_LIB *TlLibOfInfo(TYPEINFO *info)
{
    return info->Type->Lib;
}

TL_TYPE *TlTypeOfInfo(TYPEINFO *info)
{
    return info->Type;
}

static BOOL type_is_dual(const TL_TYPE *t)
{
    return (t->Flags & TYPEFLAG_FDUAL) && (t->Kind == TKIND_DISPATCH || t->Kind == TKIND_INTERFACE);
}

static TYPEKIND view_kind(const TYPEINFO *i)
{
    const TL_TYPE *t = i->Type;
    if (type_is_dual(t))
        return i->VtableView ? TKIND_INTERFACE : TKIND_DISPATCH;
    return t->Kind;
}

/* whether the functions come with the ones of the bases and as automation sees them. */
static BOOL dispatch_view(const TYPEINFO *i)
{
    return type_is_dual(i->Type) && !i->VtableView;
}

WCHAR *TlTypeName(TL_TYPE *t)
{
    if (!t->Name && t->NameOff != 0xFFFFFFFF) {
        EnterCriticalSection(&t->Lib->Lock);
        if (!t->Name)
            t->Name = TlNameAt(t->Lib, t->NameOff);
        LeaveCriticalSection(&t->Lib->Lock);
    }
    return t->Name;
}

static WCHAR *type_doc(TL_TYPE *t)
{
    if (!t->Doc && t->DocOff != 0xFFFFFFFF) {
        EnterCriticalSection(&t->Lib->Lock);
        if (!t->Doc)
            t->Doc = TlStringAt(t->Lib, t->DocOff);
        LeaveCriticalSection(&t->Lib->Lock);
    }
    return t->Doc;
}

static BSTR bstr_of(const WCHAR *text)
{
    return text ? SysAllocString(text) : nullptr;
}

/* ------------------------------------------------------------------ creating the objects */

HRESULT TlTypeInfoCreate(TL_TYPE *type, BOOL vtable_view, TYPEINFO **result)
{
    TYPEINFO *info = TlAlloc(type->Lib, sizeof(TYPEINFO));
    if (!info)
        return E_OUTOFMEMORY;
    info->lpVtbl = &g_typeinfo_vtbl;
    info->lpCompVtbl = &g_typecomp_vtbl;
    info->lpCreateVtbl = &g_createinfo_vtbl;
    info->Type = type;
    info->VtableView = vtable_view;
    *result = info;
    return S_OK;
}

HRESULT TlGetTypeInfo(TL_LIB *lib, ULONG index, BOOL vtable_view, ITypeInfo **out)
{
    if (index >= lib->TypeCount)
        return TYPE_E_ELEMENTNOTFOUND;
    TL_TYPE *type = lib->Types[index];
    EnterCriticalSection(&lib->Lock);
    if (!type->Info) {
        TYPEINFO *primary, *twin = nullptr;
        HRESULT hr = TlTypeInfoCreate(type, FALSE, &primary);
        if (SUCCEEDED(hr) && type_is_dual(type)) {
            hr = TlTypeInfoCreate(type, TRUE, &twin);
            if (SUCCEEDED(hr)) {
                primary->Twin = twin;
                twin->Twin = primary;
            }
        }
        if (FAILED(hr)) {
            LeaveCriticalSection(&lib->Lock);
            return hr;
        }
        type->Info = primary;
        type->Dual = twin;
    }
    LeaveCriticalSection(&lib->Lock);
    TYPEINFO *info = vtable_view && type->Dual ? type->Dual : type->Info;
    ITypeInfo *i = (ITypeInfo *)info;
    i->lpVtbl->AddRef(i);
    *out = i;
    return S_OK;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT WINAPI ti_query(ITypeInfo2 *self, REFIID iid, void **out)
{
    TYPEINFO *info = from_info(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_ITypeInfo) || IsEqualIID(iid, &IID_ITypeInfo2) ||
        IsEqualIID(iid, &IID_MimeTypeInfo))
        *out = info;
    else if (IsEqualIID(iid, &IID_ITypeComp))
        *out = &info->lpCompVtbl;
    else if (info->Type->Lib->Writable && (IsEqualIID(iid, &IID_ICreateTypeInfo) || IsEqualIID(iid, &IID_ICreateTypeInfo2)))
        *out = &info->lpCreateVtbl;
    else
        return E_NOINTERFACE;
    info->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI ti_addref(ITypeInfo2 *self)
{
    TYPEINFO *info = from_info(self);
    LONG refs = InterlockedIncrement(&info->Refs);
    if (refs == 1)
        TlLibAddRef(info->Type->Lib);
    return refs;
}

static ULONG WINAPI ti_release(ITypeInfo2 *self)
{
    TYPEINFO *info = from_info(self);
    TL_LIB *lib = info->Type->Lib;
    LONG refs = InterlockedDecrement(&info->Refs);
    if (refs == 0)
        TlLibRelease(lib);
    return refs;
}

static HRESULT WINAPI comp_query(ITypeComp *self, REFIID iid, void **out)
{
    TYPEINFO *info = from_comp(self);
    return ti_query((ITypeInfo2 *)info, iid, out);
}
static ULONG WINAPI comp_addref(ITypeComp *self)
{
    return ti_addref((ITypeInfo2 *)from_comp(self));
}
static ULONG WINAPI comp_release(ITypeComp *self)
{
    return ti_release((ITypeInfo2 *)from_comp(self));
}

/* ------------------------------------------------------------------ descriptions */

/* the number of bytes the nodes below a type description need. */
static SIZE_T td_extra(const TYPEDESC *td)
{
    switch (td->vt) {
    case VT_PTR:
    case VT_SAFEARRAY: return sizeof(TYPEDESC) + td_extra(td->lptdesc);
    case VT_CARRAY:
        return ((sizeof(ARRAYDESC) + (td->lpadesc->cDims - 1) * sizeof(SAFEARRAYBOUND)) + 7 & ~(SIZE_T)7) +
               td_extra(&td->lpadesc->tdescElem);
    default: return 0;
    }
}

/* copies a type description, its nodes taken from the blob; references get the depth of the base they came from. */
static void td_copy(BYTE **cursor, TYPEDESC *to, const TYPEDESC *from, USHORT tag)
{
    *to = *from;
    switch (from->vt) {
    case VT_PTR:
    case VT_SAFEARRAY: {
        TYPEDESC *inner = (TYPEDESC *)*cursor;
        *cursor += sizeof(TYPEDESC);
        to->lptdesc = inner;
        td_copy(cursor, inner, from->lptdesc, tag);
        break;
    }
    case VT_CARRAY: {
        SIZE_T size = sizeof(ARRAYDESC) + (from->lpadesc->cDims - 1) * sizeof(SAFEARRAYBOUND);
        ARRAYDESC *desc = (ARRAYDESC *)*cursor;
        *cursor += (size + 7) & ~(SIZE_T)7;
        memcpy(desc, from->lpadesc, size);
        to->lpadesc = desc;
        td_copy(cursor, &desc->tdescElem, &from->lpadesc->tdescElem, tag);
        break;
    }
    case VT_USERDEFINED:
        if (tag && !(from->hreftype & 0xFF000000))
            to->hreftype = from->hreftype | ((ULONG)tag << 24);
        break;
    }
}

static SHORT visible_params(const TL_FUNC *f, BOOL dispatch, const TL_PARAM **retval)
{
    SHORT n = 0;
    *retval = nullptr;
    for (USHORT i = 0; i < f->ParamCount; i++) {
        const TL_PARAM *p = &f->Params[i];
        if (dispatch) {
            if (p->Flags & PARAMFLAG_FLCID)
                continue;
            if ((p->Flags & PARAMFLAG_FRETVAL) && !*retval) {
                *retval = p;
                continue;
            }
        }
        n++;
    }
    return n;
}

HRESULT TlBuildFuncDesc(TL_TYPE *type, TL_FUNC *f, BOOL dispatch, USHORT tag, FUNCDESC **out)
{
    TL_LIB *lib = type->Lib;
    const TL_PARAM *retval;
    SHORT nparams = visible_params(f, dispatch, &retval);
    SIZE_T size = (sizeof(FUNCDESC) + 7) & ~(SIZE_T)7;
    size += (SIZE_T)nparams * sizeof(ELEMDESC);
    SIZE_T defaults = 0;
    TYPEDESC ret = f->Ret;
    SIZE_T extra = 0;
    if (retval && retval->Type.vt == VT_PTR) {
        ret = *retval->Type.lptdesc;
    }
    extra += td_extra(&ret);
    for (USHORT i = 0; i < f->ParamCount; i++) {
        const TL_PARAM *p = &f->Params[i];
        if (dispatch && ((p->Flags & PARAMFLAG_FLCID) || p == retval))
            continue;
        extra += td_extra(&p->Type);
        if (p->Flags & PARAMFLAG_FHASDEFAULT)
            defaults += (sizeof(PARAMDESCEX) + 7) & ~(SIZE_T)7;
    }
    size += defaults + extra;
    FUNCDESC *fd = AutAllocZero(size);
    if (!fd) {
        return E_OUTOFMEMORY;
    }
    BYTE *cursor = (BYTE *)fd + ((sizeof(FUNCDESC) + 7) & ~(SIZE_T)7);
    fd->lprgelemdescParam = nparams ? (ELEMDESC *)cursor : nullptr;
    cursor += (SIZE_T)nparams * sizeof(ELEMDESC);
    fd->memid = f->Id;
    fd->funckind = dispatch ? FUNC_DISPATCH : f->Kind;
    fd->invkind = f->Invoke;
    fd->callconv = f->Conv;
    fd->cParams = nparams;
    fd->cParamsOpt = f->OptCount;
    if (dispatch)
        fd->oVft = TlNativeVtOffset(lib, f->VtOffset);
    else
        fd->oVft = (f->Kind == FUNC_STATIC || f->Kind == FUNC_DISPATCH) ? 0 : TlNativeVtOffset(lib, f->VtOffset);
    fd->wFuncFlags = f->Flags;
    td_copy(&cursor, &fd->elemdescFunc.tdesc, &ret, tag);
    if (dispatch && fd->elemdescFunc.tdesc.vt == VT_HRESULT)
        fd->elemdescFunc.tdesc.vt = VT_VOID;
    SHORT k = 0;
    for (USHORT i = 0; i < f->ParamCount; i++) {
        const TL_PARAM *p = &f->Params[i];
        if (dispatch && ((p->Flags & PARAMFLAG_FLCID) || p == retval))
            continue;
        ELEMDESC *e = &fd->lprgelemdescParam[k++];
        td_copy(&cursor, &e->tdesc, &p->Type, tag);
        e->paramdesc.wParamFlags = p->Flags;
        if (p->Flags & PARAMFLAG_FHASDEFAULT) {
            PARAMDESCEX *ex = (PARAMDESCEX *)cursor;
            cursor += (sizeof(PARAMDESCEX) + 7) & ~(SIZE_T)7;
            ex->cBytes = sizeof(PARAMDESCEX);
            ex->varDefaultValue = p->Default;
            if (p->Default.vt == VT_BSTR && p->Default.bstrVal) {
                ex->varDefaultValue.bstrVal = SysAllocStringLen(p->Default.bstrVal, SysStringLen(p->Default.bstrVal));
                if (!ex->varDefaultValue.bstrVal) {
                    /* the strings so far belong to the blob, nothing else points at it yet. */
                    for (SHORT j = 0; j < k - 1; j++)
                        if ((fd->lprgelemdescParam[j].paramdesc.wParamFlags & PARAMFLAG_FHASDEFAULT) &&
                            fd->lprgelemdescParam[j].paramdesc.pparamdescex->varDefaultValue.vt == VT_BSTR)
                            SysFreeString(fd->lprgelemdescParam[j].paramdesc.pparamdescex->varDefaultValue.bstrVal);
                    AutFree(fd);
                    return E_OUTOFMEMORY;
                }
            }
            e->paramdesc.pparamdescex = ex;
        }
    }
    *out = fd;
    return S_OK;
}

static void free_funcdesc(FUNCDESC *fd)
{
    if (!fd)
        return;
    for (SHORT i = 0; i < fd->cParams; i++) {
        ELEMDESC *e = &fd->lprgelemdescParam[i];
        if ((e->paramdesc.wParamFlags & PARAMFLAG_FHASDEFAULT) && e->paramdesc.pparamdescex &&
            e->paramdesc.pparamdescex->varDefaultValue.vt == VT_BSTR)
            SysFreeString(e->paramdesc.pparamdescex->varDefaultValue.bstrVal);
    }
    AutFree(fd);
}

HRESULT TlBuildVarDesc(TL_TYPE *type, TL_VAR *v, VARDESC **out)
{
    SIZE_T size = ((sizeof(VARDESC) + 7) & ~(SIZE_T)7) + ((sizeof(VARIANT) + 7) & ~(SIZE_T)7) + td_extra(&v->Type);
    VARDESC *vd = AutAllocZero(size);
    if (!vd)
        return E_OUTOFMEMORY;
    BYTE *cursor = (BYTE *)vd + ((sizeof(VARDESC) + 7) & ~(SIZE_T)7);
    vd->memid = v->Id;
    vd->wVarFlags = v->Flags;
    vd->varkind = v->Kind;
    if (v->Kind == VAR_CONST) {
        vd->lpvarValue = (VARIANT *)cursor;
        *vd->lpvarValue = v->Value;
        if (v->Value.vt == VT_BSTR && v->Value.bstrVal) {
            vd->lpvarValue->bstrVal = SysAllocStringLen(v->Value.bstrVal, SysStringLen(v->Value.bstrVal));
            if (!vd->lpvarValue->bstrVal) {
                AutFree(vd);
                return E_OUTOFMEMORY;
            }
        }
    } else {
        vd->oInst = v->Offset;
    }
    cursor += (sizeof(VARIANT) + 7) & ~(SIZE_T)7;
    td_copy(&cursor, &vd->elemdescVar.tdesc, &v->Type, 0);
    *out = vd;
    return S_OK;
}

/* ------------------------------------------------------------------ the base of a type */

/* the type this one is derived from, referenced, or an error when it has none. */
static HRESULT base_info(TYPEINFO *info, ITypeInfo **base)
{
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    HREFTYPE ref;
    switch (view_kind(info)) {
    case TKIND_DISPATCH:
        ref = t->Lib->DispatchRef;
        if (t->ImplCount && t->Impls[0].Ref != 0xFFFFFFFF)
            ref = t->Impls[0].Ref;
        break;
    case TKIND_INTERFACE:
        if (!t->ImplCount)
            return TYPE_E_ELEMENTNOTFOUND;
        ref = t->Impls[0].Ref | 2;
        break;
    default:
        return TYPE_E_ELEMENTNOTFOUND;
    }
    if (ref == 0xFFFFFFFF)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlResolveHref(t, ref, base);
}

static TYPEINFO *ours(ITypeInfo *i)
{
    TYPEINFO *mine = nullptr;
    if (i && SUCCEEDED(i->lpVtbl->QueryInterface(i, &IID_MimeTypeInfo, (void **)&mine))) {
        /* the reference this took is given back: the caller holds the pointer it was given. */
        ((ITypeInfo *)mine)->lpVtbl->Release((ITypeInfo *)mine);
        return mine;
    }
    return nullptr;
}

/* the functions of the type itself, then those of the base. */
static HRESULT func_desc_flags(TYPEINFO *info, UINT index, FUNCDESC **out, BOOL inherited, BOOL dispatch)
{
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    USHORT tag = 0;
    if (inherited) {
        if (index < t->InheritedFuncs) {
            ITypeInfo *base;
            hr = base_info(info, &base);
            if (FAILED(hr))
                return hr;
            TYPEINFO *mine = ours(base);
            if (mine)
                hr = func_desc_flags(mine, index, out, TRUE, dispatch);
            else
                hr = base->lpVtbl->GetFuncDesc(base, index, out);
            base->lpVtbl->Release(base);
            return hr;
        }
        index -= t->InheritedFuncs;
        tag = (USHORT)(t->InheritedImpls + 1);
    }
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlBuildFuncDesc(t, &t->Funcs[index], dispatch, tag, out);
}

/* ------------------------------------------------------------------ ITypeInfo */

static HRESULT WINAPI ti_get_type_attr(ITypeInfo2 *self, TYPEATTR **out)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    TYPEKIND kind = view_kind(info);
    SIZE_T extra = kind == TKIND_ALIAS ? td_extra(&t->Alias) : 0;
    TYPEATTR *attr = AutAllocZero(((sizeof(TYPEATTR) + 7) & ~(SIZE_T)7) + extra);
    if (!attr)
        return E_OUTOFMEMORY;
    if (t->HasGuid)
        attr->guid = t->Guid;
    attr->lcid = t->Lib->Lcid;
    attr->memidConstructor = MEMBERID_NIL;
    attr->memidDestructor = MEMBERID_NIL;
    attr->cbSizeInstance = t->SizeInstance;
    attr->typekind = kind;
    attr->cVars = t->VarCount;
    attr->cImplTypes = t->ImplCount;
    attr->cbAlignment = t->Alignment;
    attr->wMajorVerNum = t->Major;
    attr->wMinorVerNum = t->Minor;
    if (kind == TKIND_DISPATCH) {
        attr->cFuncs = (WORD)(t->FuncCount + (dispatch_view(info) ? t->InheritedFuncs : 0));
        attr->cbSizeVft = (WORD)(7 * sizeof(void *));
        attr->wTypeFlags = t->Flags & 0x5AD0;
    } else {
        attr->cFuncs = t->FuncCount;
        attr->cbSizeVft = TlNativeVtSize(t->Lib, t->SizeVft);
        attr->wTypeFlags = t->Flags;
        if (kind == TKIND_ALIAS) {
            BYTE *cursor = (BYTE *)attr + ((sizeof(TYPEATTR) + 7) & ~(SIZE_T)7);
            td_copy(&cursor, &attr->tdescAlias, &t->Alias, 0);
        }
    }
    *out = attr;
    return S_OK;
}

static HRESULT WINAPI ti_get_type_comp(ITypeInfo2 *self, ITypeComp **out)
{
    TYPEINFO *info = from_info(self);
    if (!out)
        return E_POINTER;
    *out = (ITypeComp *)&info->lpCompVtbl;
    ti_addref(self);
    return S_OK;
}

static HRESULT WINAPI ti_get_func_desc(ITypeInfo2 *self, UINT index, FUNCDESC **out)
{
    TYPEINFO *info = from_info(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BOOL dispatch = dispatch_view(info) && view_kind(info) == TKIND_DISPATCH;
    return func_desc_flags(info, index, out, dispatch, dispatch);
}

static HRESULT WINAPI ti_get_var_desc(ITypeInfo2 *self, UINT index, VARDESC **out)
{
    TYPEINFO *info = from_info(self);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    if (index >= info->Type->VarCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlBuildVarDesc(info->Type, &info->Type->Vars[index], out);
}

/* the member a member id names in this type: a function, or a variable. */
typedef struct {
    TL_FUNC *Func;
    TL_VAR *Var;
    UINT Index;
} MEMBER;

static BOOL find_member(TL_TYPE *t, MEMBERID id, INVOKEKIND kind, BOOL any_kind, MEMBER *m)
{
    memset(m, 0, sizeof(*m));
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Id == id && (any_kind || t->Funcs[i].Invoke == kind)) {
            m->Func = &t->Funcs[i];
            m->Index = i;
            return TRUE;
        }
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Id == id && any_kind) {
            m->Var = &t->Vars[i];
            m->Index = i;
            return TRUE;
        }
    return FALSE;
}

static HRESULT WINAPI ti_get_names(ITypeInfo2 *self, MEMBERID id, BSTR *names, UINT max, UINT *count)
{
    TYPEINFO *info = from_info(self);
    if (!names || !count)
        return E_POINTER;
    *count = 0;
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (id == MEMBERID_NIL && t->Kind == TKIND_COCLASS)
        id = MEMBERID_NIL;
    MEMBER m;
    if (!find_member(t, id, INVOKE_FUNC, TRUE, &m)) {
        ITypeInfo *base;
        hr = base_info(info, &base);
        if (FAILED(hr))
            return hr == TYPE_E_ELEMENTNOTFOUND ? TYPE_E_ELEMENTNOTFOUND : hr;
        hr = base->lpVtbl->GetNames(base, id, names, max, count);
        base->lpVtbl->Release(base);
        return hr;
    }
    if (!max)
        return S_OK;
    if (m.Var) {
        names[0] = bstr_of(m.Var->Name);
        if (m.Var->Name && !names[0])
            return E_OUTOFMEMORY;
        *count = 1;
        return S_OK;
    }
    TL_FUNC *f = m.Func;
    names[0] = bstr_of(f->Name);
    if (f->Name && !names[0])
        return E_OUTOFMEMORY;
    UINT n = 1;
    BOOL dispatch = dispatch_view(info);
    for (USHORT i = 0; i < f->ParamCount && n < max; i++) {
        const TL_PARAM *p = &f->Params[i];
        if (dispatch && (p->Flags & (PARAMFLAG_FLCID | PARAMFLAG_FRETVAL)))
            continue;
        names[n] = bstr_of(p->Name);
        if (p->Name && !names[n]) {
            for (UINT j = 0; j < n; j++)
                SysFreeString(names[j]);
            return E_OUTOFMEMORY;
        }
        n++;
    }
    *count = n;
    return S_OK;
}

static HRESULT WINAPI ti_get_ref_type_of_impl_type(ITypeInfo2 *self, UINT index, HREFTYPE *ref)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!ref)
        return E_POINTER;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (index >= t->ImplCount) {
        if (index != 0xFFFFFFFF || !(t->Flags & TYPEFLAG_FDUAL))
            return TYPE_E_ELEMENTNOTFOUND;
        *ref = (HREFTYPE)-2;
        return S_OK;
    }
    switch (view_kind(info)) {
    case TKIND_INTERFACE: *ref = t->Impls[0].Ref | 2; break;
    case TKIND_DISPATCH: *ref = t->Lib->DispatchRef; break;
    case TKIND_COCLASS: *ref = t->Impls[index].Ref & ~(ULONG)2; break;
    default: *ref = 0; break;
    }
    if (view_kind(info) == TKIND_DISPATCH && t->Impls && t->Impls[0].Ref != 0xFFFFFFFF && t->Impls[0].Ref != t->Lib->DispatchRef)
        *ref = t->Impls[0].Ref;
    return S_OK;
}

static HRESULT WINAPI ti_get_impl_type_flags(ITypeInfo2 *self, UINT index, INT *flags)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!flags)
        return E_POINTER;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (index >= t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    *flags = view_kind(info) == TKIND_COCLASS ? t->Impls[index].Flags : 0;
    return S_OK;
}

/* the function of a name in a type or the types it is derived from: the member and the type it is in. */
static BOOL lookup_name(TYPEINFO *info, LPCOLESTR name, BOOL functions, MEMBER *m, TYPEINFO **owner, int depth)
{
    TL_TYPE *t = info->Type;
    if (FAILED(TlLoadMembers(t)))
        return FALSE;
    memset(m, 0, sizeof(*m));
    for (UINT i = 0; i < t->FuncCount; i++)
        if (t->Funcs[i].Name && !_wcsicmp(t->Funcs[i].Name, name)) {
            m->Func = &t->Funcs[i];
            m->Index = i;
            *owner = info;
            return TRUE;
        }
    for (UINT i = 0; i < t->VarCount; i++)
        if (t->Vars[i].Name && !_wcsicmp(t->Vars[i].Name, name)) {
            m->Var = &t->Vars[i];
            m->Index = i;
            *owner = info;
            return TRUE;
        }
    if (depth > 16)
        return FALSE;
    ITypeInfo *base;
    if (FAILED(base_info(info, &base)))
        return FALSE;
    TYPEINFO *mine = ours(base);
    BOOL found = mine && lookup_name(mine, name, functions, m, owner, depth + 1);
    base->lpVtbl->Release(base);
    return found;
}

static HRESULT WINAPI ti_get_ids_of_names(ITypeInfo2 *self, LPOLESTR *names, UINT count, MEMBERID *ids)
{
    TYPEINFO *info = from_info(self);
    if (!names || !ids || !count)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    MEMBER m;
    TYPEINFO *owner;
    if (!lookup_name(info, names[0], TRUE, &m, &owner, 0))
        return DISP_E_UNKNOWNNAME;
    ids[0] = m.Func ? m.Func->Id : m.Var->Id;
    if (count == 1)
        return S_OK;
    if (!m.Func)
        return DISP_E_UNKNOWNNAME;
    /* the arguments by name: their position among the ones automation passes. */
    HRESULT hr = S_OK;
    BOOL dispatch = dispatch_view(owner) || dispatch_view(info);
    for (UINT i = 1; i < count; i++) {
        SHORT position = 0;
        BOOL found = FALSE;
        for (USHORT j = 0; j < m.Func->ParamCount; j++) {
            const TL_PARAM *p = &m.Func->Params[j];
            if (dispatch && (p->Flags & (PARAMFLAG_FLCID | PARAMFLAG_FRETVAL)))
                continue;
            if (p->Name && !_wcsicmp(p->Name, names[i])) {
                ids[i] = position;
                found = TRUE;
                break;
            }
            position++;
        }
        if (!found)
            hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
}

static const WCHAR *help_file_of(TL_LIB *lib);

static HRESULT WINAPI ti_get_documentation(ITypeInfo2 *self, MEMBERID id, BSTR *name, BSTR *doc, DWORD *context,
                                           BSTR *file)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (name)
        *name = nullptr;
    if (doc)
        *doc = nullptr;
    if (context)
        *context = 0;
    if (file)
        *file = nullptr;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    const WCHAR *n = nullptr, *d = nullptr, *h = nullptr;
    DWORD ctx = 0;
    if (id == MEMBERID_NIL) {
        n = TlTypeName(t);
        d = type_doc(t);
        ctx = t->HelpContext;
    } else {
        MEMBER m;
        if (!find_member(t, id, INVOKE_FUNC, TRUE, &m)) {
            ITypeInfo *base;
            hr = base_info(info, &base);
            if (FAILED(hr))
                return TYPE_E_ELEMENTNOTFOUND;
            hr = base->lpVtbl->GetDocumentation(base, id, name, doc, context, file);
            base->lpVtbl->Release(base);
            return hr;
        }
        if (m.Func) {
            n = m.Func->Name;
            d = m.Func->Doc;
            ctx = m.Func->HelpContext;
            h = m.Func->HelpFile;
        } else {
            n = m.Var->Name;
            d = m.Var->Doc;
            ctx = m.Var->HelpContext;
        }
    }
    if (name && n && !(*name = SysAllocString(n)))
        goto fail;
    if (doc && d && !(*doc = SysAllocString(d)))
        goto fail;
    if (context)
        *context = ctx;
    if (file) {
        const WCHAR *f = h ? h : help_file_of(t->Lib);
        if (f && !(*file = SysAllocString(f)))
            goto fail;
    }
    return S_OK;
fail:
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

static HRESULT WINAPI ti_get_dll_entry(ITypeInfo2 *self, MEMBERID id, INVOKEKIND kind, BSTR *dll, BSTR *name,
                                       WORD *ordinal)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (dll)
        *dll = nullptr;
    if (name)
        *name = nullptr;
    if (ordinal)
        *ordinal = 0;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    MEMBER m;
    if (!find_member(t, id, kind, FALSE, &m) || !m.Func)
        return TYPE_E_ELEMENTNOTFOUND;
    const WCHAR *dll_name = m.Func->DllName ? m.Func->DllName : t->DllName;
    if (dll && dll_name && !(*dll = SysAllocString(dll_name)))
        return E_OUTOFMEMORY;
    if (m.Func->HasOrdinal) {
        if (ordinal)
            *ordinal = m.Func->Ordinal;
    } else if (name && m.Func->EntryName) {
        *name = SysAllocString(m.Func->EntryName);
        if (!*name) {
            if (dll) {
                SysFreeString(*dll);
                *dll = nullptr;
            }
            return E_OUTOFMEMORY;
        }
    } else if (name && m.Func->Name) {
        *name = SysAllocString(m.Func->Name);
    }
    return S_OK;
}

static HRESULT WINAPI ti_get_ref_type_info(ITypeInfo2 *self, HREFTYPE ref, ITypeInfo **out)
{
    TYPEINFO *info = from_info(self);
    if (!out || ref == 0xFFFFFFFF)
        return E_INVALIDARG;
    *out = nullptr;
    if (ref == (HREFTYPE)-2) {
        if (!info->Twin)
            return TYPE_E_ELEMENTNOTFOUND;
        ITypeInfo *twin = (ITypeInfo *)info->Twin;
        twin->lpVtbl->AddRef(twin);
        *out = twin;
        return S_OK;
    }
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    /* a reference from a base carries how deep that base is: it names a type of the base's own library. */
    ULONG depth = HREF_DEPTH(ref);
    if (depth && depth != (ULONG)t->InheritedImpls + 1) {
        ITypeInfo *base;
        hr = base_info(info, &base);
        if (FAILED(hr))
            return hr;
        hr = base->lpVtbl->GetRefTypeInfo(base, ref, out);
        base->lpVtbl->Release(base);
        return hr;
    }
    return TlResolveHref(t, ref & 0xFFFFFF, out);
}

static HRESULT WINAPI ti_address_of_member(ITypeInfo2 *self, MEMBERID id, INVOKEKIND kind, PVOID *address)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!address)
        return E_POINTER;
    *address = nullptr;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (t->Kind != TKIND_MODULE)
        return TYPE_E_BADMODULEKIND;
    MEMBER m;
    if (!find_member(t, id, kind, FALSE, &m) || !m.Func)
        return TYPE_E_ELEMENTNOTFOUND;
    const WCHAR *dll = m.Func->DllName ? m.Func->DllName : t->DllName;
    if (!dll)
        return TYPE_E_DLLFUNCTIONNOTFOUND;
    HMODULE module = LoadLibraryW(dll);
    if (!module)
        return TYPE_E_CANTLOADLIBRARY;
    FARPROC proc = nullptr;
    if (m.Func->HasOrdinal) {
        proc = GetProcAddress(module, (LPCSTR)(ULONG_PTR)m.Func->Ordinal);
    } else {
        const WCHAR *entry = m.Func->EntryName ? m.Func->EntryName : m.Func->Name;
        char narrow[260];
        int n = entry ? WideCharToMultiByte(CP_ACP, 0, entry, -1, narrow, sizeof(narrow), nullptr, nullptr) : 0;
        if (n)
            proc = GetProcAddress(module, narrow);
    }
    if (!proc) {
        FreeLibrary(module);
        return TYPE_E_DLLFUNCTIONNOTFOUND;
    }
    *address = (PVOID)proc;
    return S_OK;
}

static HRESULT WINAPI ti_create_instance(ITypeInfo2 *self, IUnknown *outer, REFIID riid, PVOID *object)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (t->Kind != TKIND_COCLASS)
        return TYPE_E_BADMODULEKIND;
    return CoCreateInstance(&t->Guid, outer, CLSCTX_INPROC_SERVER, riid, object);
}

static HRESULT WINAPI ti_get_mops(ITypeInfo2 *self, MEMBERID id, BSTR *mops)
{
    TYPEINFO *info = from_info(self);
    if (!mops)
        return E_POINTER;
    *mops = nullptr;
    if (id == MEMBERID_NIL)
        return S_OK;
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    MEMBER m;
    if (!find_member(info->Type, id, INVOKE_FUNC, TRUE, &m))
        return TYPE_E_ELEMENTNOTFOUND;
    return S_OK;
}

static HRESULT WINAPI ti_get_containing_type_lib(ITypeInfo2 *self, ITypeLib **lib, UINT *index)
{
    TYPEINFO *info = from_info(self);
    TL_LIB *l = info->Type->Lib;
    if (lib) {
        *lib = (ITypeLib *)l;
        TlLibAddRef(l);
    }
    if (index)
        *index = info->Type->Index;
    return S_OK;
}

static void WINAPI ti_release_type_attr(ITypeInfo2 *self, TYPEATTR *attr)
{
    AutFree(attr);
}

static void WINAPI ti_release_func_desc(ITypeInfo2 *self, FUNCDESC *fd)
{
    free_funcdesc(fd);
}

static void WINAPI ti_release_var_desc(ITypeInfo2 *self, VARDESC *vd)
{
    if (vd && vd->varkind == VAR_CONST && vd->lpvarValue && vd->lpvarValue->vt == VT_BSTR)
        SysFreeString(vd->lpvarValue->bstrVal);
    AutFree(vd);
}

static HRESULT WINAPI ti_invoke(ITypeInfo2 *self, PVOID instance, MEMBERID id, WORD flags, DISPPARAMS *params,
                                VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    TYPEINFO *info = from_info(self);
    return TlInvokeOnType((ITypeInfo *)self, info->Type, dispatch_view(info), instance, id, flags, params, result, excep, arg_err);
}

/* ------------------------------------------------------------------ ITypeInfo2 */

static HRESULT WINAPI ti_get_type_kind(ITypeInfo2 *self, TYPEKIND *kind)
{
    if (!kind)
        return E_POINTER;
    *kind = view_kind(from_info(self));
    return S_OK;
}

static HRESULT WINAPI ti_get_type_flags(ITypeInfo2 *self, ULONG *flags)
{
    TYPEINFO *info = from_info(self);
    if (!flags)
        return E_POINTER;
    *flags = view_kind(info) == TKIND_DISPATCH ? info->Type->Flags & 0x5AD0 : info->Type->Flags;
    return S_OK;
}

static HRESULT WINAPI ti_get_func_index_of_memid(ITypeInfo2 *self, MEMBERID id, INVOKEKIND kind, UINT *index)
{
    TYPEINFO *info = from_info(self);
    if (!index)
        return E_POINTER;
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < info->Type->FuncCount; i++)
        if (info->Type->Funcs[i].Id == id && info->Type->Funcs[i].Invoke == kind) {
            *index = i;
            return S_OK;
        }
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI ti_get_var_index_of_memid(ITypeInfo2 *self, MEMBERID id, UINT *index)
{
    TYPEINFO *info = from_info(self);
    if (!index)
        return E_POINTER;
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < info->Type->VarCount; i++)
        if (info->Type->Vars[i].Id == id) {
            *index = i;
            return S_OK;
        }
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT cust_get(TL_LIB *lib, TL_CUST *list, REFGUID guid, VARIANT *value)
{
    if (!value)
        return E_INVALIDARG;
    value->vt = VT_EMPTY;
    for (TL_CUST *c = list; c; c = c->Next)
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

HRESULT TlCustAll(TL_CUST *list, CUSTDATA *data)
{
    if (!data)
        return E_INVALIDARG;
    data->cCustData = 0;
    data->prgCustData = nullptr;
    DWORD n = 0;
    for (TL_CUST *c = list; c; c = c->Next)
        n++;
    if (!n)
        return S_OK;
    data->prgCustData = AutAllocZero(n * sizeof(CUSTDATAITEM));
    if (!data->prgCustData)
        return E_OUTOFMEMORY;
    DWORD i = 0;
    for (TL_CUST *c = list; c; c = c->Next, i++) {
        data->prgCustData[i].guid = c->Guid;
        data->prgCustData[i].varValue = c->Value;
        if (c->Value.vt == VT_BSTR && c->Value.bstrVal)
            data->prgCustData[i].varValue.bstrVal = SysAllocStringLen(c->Value.bstrVal, SysStringLen(c->Value.bstrVal));
    }
    data->cCustData = n;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI ClearCustData(CUSTDATA *data)
{
    if (!data)
        return E_INVALIDARG;
    for (DWORD i = 0; i < data->cCustData; i++)
        VariantClear(&data->prgCustData[i].varValue);
    AutFree(data->prgCustData);
    data->cCustData = 0;
    data->prgCustData = nullptr;
    return S_OK;
}

static HRESULT WINAPI ti_get_cust_data(ITypeInfo2 *self, REFGUID guid, VARIANT *value)
{
    TYPEINFO *info = from_info(self);
    return cust_get(info->Type->Lib, info->Type->Cust, guid, value);
}

static HRESULT member_cust(TYPEINFO *info, BOOL func, UINT index, INT param, TL_CUST **list)
{
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (func) {
        if (index >= t->FuncCount)
            return TYPE_E_ELEMENTNOTFOUND;
        if (param < 0) {
            *list = t->Funcs[index].Cust;
        } else {
            if ((UINT)param >= t->Funcs[index].ParamCount)
                return TYPE_E_ELEMENTNOTFOUND;
            *list = t->Funcs[index].Params[param].Cust;
        }
    } else {
        if (index >= t->VarCount)
            return TYPE_E_ELEMENTNOTFOUND;
        *list = t->Vars[index].Cust;
    }
    return S_OK;
}

static HRESULT WINAPI ti_get_func_cust_data(ITypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), TRUE, index, -1, &list);
    return FAILED(hr) ? hr : cust_get(nullptr, list, guid, value);
}

static HRESULT WINAPI ti_get_param_cust_data(ITypeInfo2 *self, UINT func, UINT param, REFGUID guid, VARIANT *value)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), TRUE, func, (INT)param, &list);
    return FAILED(hr) ? hr : cust_get(nullptr, list, guid, value);
}

static HRESULT WINAPI ti_get_var_cust_data(ITypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), FALSE, index, -1, &list);
    return FAILED(hr) ? hr : cust_get(nullptr, list, guid, value);
}

static HRESULT WINAPI ti_get_impl_type_cust_data(ITypeInfo2 *self, UINT index, REFGUID guid, VARIANT *value)
{
    TYPEINFO *info = from_info(self);
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    if (index >= info->Type->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return cust_get(nullptr, info->Type->Impls[index].Cust, guid, value);
}

static HRESULT WINAPI ti_get_documentation2(ITypeInfo2 *self, MEMBERID id, LCID lcid, BSTR *help, DWORD *context,
                                            BSTR *dll)
{
    TYPEINFO *info = from_info(self);
    if (help)
        *help = nullptr;
    if (context)
        *context = 0;
    if (dll)
        *dll = nullptr;
    HRESULT hr = ti_get_documentation(self, id, nullptr, help, context, nullptr);
    if (FAILED(hr))
        return hr;
    if (context) {
        MEMBER m;
        TL_TYPE *t = info->Type;
        if (id == MEMBERID_NIL)
            *context = t->HelpStringContext;
        else if (find_member(t, id, INVOKE_FUNC, TRUE, &m))
            *context = m.Func ? m.Func->HelpStringContext : m.Var->HelpStringContext;
    }
    if (dll && info->Type->Lib->HelpStringDll) {
        *dll = SysAllocString(info->Type->Lib->HelpStringDll);
        if (!*dll) {
            SysFreeString(help ? *help : nullptr);
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}

static HRESULT WINAPI ti_get_all_cust_data(ITypeInfo2 *self, CUSTDATA *data)
{
    return TlCustAll(from_info(self)->Type->Cust, data);
}

static HRESULT WINAPI ti_get_all_func_cust_data(ITypeInfo2 *self, UINT index, CUSTDATA *data)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), TRUE, index, -1, &list);
    return FAILED(hr) ? hr : TlCustAll(list, data);
}

static HRESULT WINAPI ti_get_all_param_cust_data(ITypeInfo2 *self, UINT func, UINT param, CUSTDATA *data)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), TRUE, func, (INT)param, &list);
    return FAILED(hr) ? hr : TlCustAll(list, data);
}

static HRESULT WINAPI ti_get_all_var_cust_data(ITypeInfo2 *self, UINT index, CUSTDATA *data)
{
    TL_CUST *list;
    HRESULT hr = member_cust(from_info(self), FALSE, index, -1, &list);
    return FAILED(hr) ? hr : TlCustAll(list, data);
}

static HRESULT WINAPI ti_get_all_impl_type_cust_data(ITypeInfo2 *self, UINT index, CUSTDATA *data)
{
    TYPEINFO *info = from_info(self);
    HRESULT hr = TlLoadMembers(info->Type);
    if (FAILED(hr))
        return hr;
    if (index >= info->Type->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlCustAll(info->Type->Impls[index].Cust, data);
}

const ITypeInfo2Vtbl g_typeinfo_vtbl = {
    ti_query, ti_addref, ti_release, ti_get_type_attr, ti_get_type_comp, ti_get_func_desc, ti_get_var_desc,
    ti_get_names, ti_get_ref_type_of_impl_type, ti_get_impl_type_flags, ti_get_ids_of_names, ti_invoke,
    ti_get_documentation, ti_get_dll_entry, ti_get_ref_type_info, ti_address_of_member, ti_create_instance, ti_get_mops,
    ti_get_containing_type_lib, ti_release_type_attr, ti_release_func_desc, ti_release_var_desc, ti_get_type_kind,
    ti_get_type_flags, ti_get_func_index_of_memid, ti_get_var_index_of_memid, ti_get_cust_data, ti_get_func_cust_data,
    ti_get_param_cust_data, ti_get_var_cust_data, ti_get_impl_type_cust_data, ti_get_documentation2,
    ti_get_all_cust_data, ti_get_all_func_cust_data, ti_get_all_param_cust_data, ti_get_all_var_cust_data,
    ti_get_all_impl_type_cust_data};

/* ------------------------------------------------------------------ ITypeComp */

static BOOL invoke_matches(INVOKEKIND kind, WORD flags)
{
    if (!flags)
        return TRUE;
    return (flags & kind) != 0;
}

/* a function of a name that a call of the given kind can reach, then a variable; the type that holds it. */
HRESULT TlBindName(TYPEINFO *info, LPCOLESTR name, WORD flags, ITypeInfo **found, DESCKIND *kind, BINDPTR *bind,
                   int depth)
{
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    BOOL dispatch = dispatch_view(info);
    for (UINT i = 0; i < t->FuncCount; i++) {
        TL_FUNC *f = &t->Funcs[i];
        if (!f->Name || _wcsicmp(f->Name, name) || !invoke_matches(f->Invoke, flags))
            continue;
        FUNCDESC *fd;
        hr = TlBuildFuncDesc(t, f, dispatch, 0, &fd);
        if (FAILED(hr))
            return hr;
        bind->lpfuncdesc = fd;
        *kind = DESCKIND_FUNCDESC;
        *found = (ITypeInfo *)info;
        ti_addref((ITypeInfo2 *)info);
        return S_OK;
    }
    for (UINT i = 0; i < t->VarCount; i++) {
        TL_VAR *v = &t->Vars[i];
        if (!v->Name || _wcsicmp(v->Name, name))
            continue;
        VARDESC *vd;
        hr = TlBuildVarDesc(t, v, &vd);
        if (FAILED(hr))
            return hr;
        bind->lpvardesc = vd;
        *kind = DESCKIND_VARDESC;
        *found = (ITypeInfo *)info;
        ti_addref((ITypeInfo2 *)info);
        return S_OK;
    }
    if (depth < 16 && (t->Kind == TKIND_INTERFACE || t->Kind == TKIND_DISPATCH)) {
        ITypeInfo *base;
        if (SUCCEEDED(base_info(info, &base))) {
            TYPEINFO *mine = ours(base);
            hr = mine ? TlBindName(mine, name, flags, found, kind, bind, depth + 1) : S_OK;
            base->lpVtbl->Release(base);
            return hr;
        }
    }
    *kind = DESCKIND_NONE;
    *found = nullptr;
    return S_OK;
}

static HRESULT WINAPI comp_bind(ITypeComp *self, LPOLESTR name, ULONG hash, WORD flags, ITypeInfo **found,
                                DESCKIND *kind, BINDPTR *bind)
{
    TYPEINFO *info = from_comp(self);
    if (!name || !found || !kind || !bind)
        return E_INVALIDARG;
    *found = nullptr;
    *kind = DESCKIND_NONE;
    memset(bind, 0, sizeof(*bind));
    return TlBindName(info, name, flags, found, kind, bind, 0);
}

static HRESULT WINAPI comp_bind_type(ITypeComp *self, LPOLESTR name, ULONG hash, ITypeInfo **info, ITypeComp **comp)
{
    if (!name || !info || !comp)
        return E_INVALIDARG;
    *info = nullptr;
    *comp = nullptr;
    return S_OK;
}

const ITypeCompVtbl g_typecomp_vtbl = {comp_query, comp_addref, comp_release, comp_bind, comp_bind_type};

/* ------------------------------------------------------------------ the help file */

static const WCHAR *help_file_of(TL_LIB *lib)
{
    return lib->HelpFile;
}

/* the other half of a dual interface, referenced, or null for a type that is not dual. */
ITypeInfo *TlInfoVtableHalf(ITypeInfo *info)
{
    TYPEINFO *mine = nullptr;
    if (FAILED(info->lpVtbl->QueryInterface(info, &IID_MimeTypeInfo, (void **)&mine)))
        return nullptr;
    ((ITypeInfo *)mine)->lpVtbl->Release((ITypeInfo *)mine);
    if (!mine->Twin || !mine->Type->Dual || mine == mine->Type->Dual)
        return nullptr;
    ITypeInfo *twin = (ITypeInfo *)mine->Type->Dual;
    twin->lpVtbl->AddRef(twin);
    return twin;
}
