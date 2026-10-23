/*
 * ITypeInfo2 and ITypeComp for library types, plus ICreateTypeInfo2 on a library being built. A dual type has two
 * objects: the dispatch view, whose functions lack the lcid and retval parameters as automation calls them, and the
 * vtable view.
 */
#include "typelib.h"

MIME_DEFINE_GUID(IID_MimeTypeInfo, 0x6d696d65, 0x7479, 0x7065, 0x69, 0x6e, 0x66, 0x6f, 0x00, 0x00, 0x00, 0x01);


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

/* the dispatch view of a dual type lists base functions too, in automation form. */
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

/* ------------------------------------------------------------------ creation */

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

static SIZE_T array_node_size(const ARRAYDESC *array)
{
    SIZE_T dims = array->cDims ? array->cDims : 1;
    return (sizeof(ARRAYDESC) + (dims - 1) * sizeof(SAFEARRAYBOUND) + 7) & ~(SIZE_T)7;
}

/*
 * copies a type description, taking child nodes from the cursor in the room td_extra counted. A type reference with no
 * level in its top byte gets the level of the type being described, which tells a base type how to resolve the handle
 * when it comes back.
 */
static void td_copy(BYTE **cursor, TYPEDESC *to, const TYPEDESC *from, USHORT level)
{
    *to = *from;
    switch (from->vt) {
    case VT_PTR:
    case VT_SAFEARRAY: {
        TYPEDESC *node = (TYPEDESC *)*cursor;
        *cursor += sizeof(TYPEDESC);
        to->lptdesc = node;
        td_copy(cursor, node, from->lptdesc, level);
        break;
    }
    case VT_CARRAY: {
        ARRAYDESC *node = (ARRAYDESC *)*cursor;
        *cursor += array_node_size(from->lpadesc);
        SIZE_T dims = from->lpadesc->cDims ? from->lpadesc->cDims : 1;
        memcpy(node, from->lpadesc, sizeof(ARRAYDESC) + (dims - 1) * sizeof(SAFEARRAYBOUND));
        to->lpadesc = node;
        td_copy(cursor, &node->tdescElem, &from->lpadesc->tdescElem, level);
        break;
    }
    case VT_USERDEFINED:
        if (level && !HREF_DEPTH(from->hreftype))
            to->hreftype = from->hreftype | (HREFTYPE)level << 24;
        break;
    default: break;
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
                    /* nothing else points into the blob yet, so free the strings copied so far. */
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

/* ------------------------------------------------------------------ base types */

/* the base type, referenced, or an error when there is none. */
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
        /* drop the QueryInterface reference, the caller already holds one. */
        ((ITypeInfo *)mine)->lpVtbl->Release((ITypeInfo *)mine);
        return mine;
    }
    return nullptr;
}

/*
 * with list_bases, base functions come first, as the dispatch side counts them, and bases are asked the same way in
 * automation form. Their handles carry the level of the type they belong to, so whoever gets one back knows which
 * type it is relative to.
 */
static HRESULT listed_func_desc(TYPEINFO *info, UINT index, BOOL list_bases, BOOL automation_form, FUNCDESC **out)
{
    TL_TYPE *t = info->Type;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (list_bases && index < t->InheritedFuncs) {
        ITypeInfo *base;
        hr = base_info(info, &base);
        if (FAILED(hr))
            return hr;
        TYPEINFO *mine = ours(base);
        if (mine)
            hr = listed_func_desc(mine, index, TRUE, automation_form, out);
        else
            hr = base->lpVtbl->GetFuncDesc(base, index, out);
        base->lpVtbl->Release(base);
        return hr;
    }
    if (list_bases)
        index -= t->InheritedFuncs;
    if (index >= t->FuncCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlBuildFuncDesc(t, &t->Funcs[index], automation_form, list_bases ? t->InheritedImpls + 1 : 0, out);
}


/* ------------------------------------------------------------------ ITypeInfo */

/* a dispatch interface never reports class, control or licensed flags, whatever the library says. */
#define DISPATCH_REPORTED_FLAGS 0x5AD0

static HRESULT WINAPI ti_get_type_attr(ITypeInfo2 *self, TYPEATTR **out)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT hr = TlEnsureLayout(t);
    if (FAILED(hr))
        return hr;
    TYPEKIND kind = view_kind(info);
    BOOL dispatch = kind == TKIND_DISPATCH;
    SIZE_T size = (sizeof(TYPEATTR) + 7) & ~(SIZE_T)7;
    if (kind == TKIND_ALIAS)
        size += td_extra(&t->Alias);
    TYPEATTR *attr = AutAllocZero(size);
    if (!attr)
        return E_OUTOFMEMORY;
    if (t->HasGuid)
        attr->guid = t->Guid;
    attr->lcid = t->Lib->Lcid;
    attr->memidConstructor = t->Constructor;
    attr->memidDestructor = t->Destructor;
    attr->lpstrSchema = t->Schema;
    attr->cbSizeInstance = t->SizeInstance;
    attr->typekind = kind;
    attr->cFuncs = (WORD)(t->FuncCount + (dispatch ? t->InheritedFuncs : 0));
    attr->cVars = t->VarCount;
    attr->cImplTypes = t->ImplCount;
    attr->cbSizeVft = dispatch ? 7 * sizeof(void *) : TlNativeVtSize(t->Lib, t->SizeVft);
    attr->cbAlignment = t->Alignment;
    attr->wTypeFlags = dispatch ? t->Flags & DISPATCH_REPORTED_FLAGS : t->Flags;
    attr->wMajorVerNum = t->Major;
    attr->wMinorVerNum = t->Minor;
    attr->idldescType.wIDLFlags = t->IdlFlags;
    if (kind == TKIND_ALIAS) {
        BYTE *cursor = (BYTE *)attr + ((sizeof(TYPEATTR) + 7) & ~(SIZE_T)7);
        td_copy(&cursor, &attr->tdescAlias, &t->Alias, 0);
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
    BOOL automation = dispatch_view(info);
    return listed_func_desc(info, index, automation, automation, out);
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

/* a function or a variable found by member id. */
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


/* refers to the other half of a dual interface. */
#define HREF_OTHER_HALF 0xFFFFFFFEu
#define HREF_INVALID 0xFFFFFFFFu
#define HREF_VTABLE_HALF 2u

static HRESULT WINAPI ti_get_ref_type_of_impl_type(ITypeInfo2 *self, UINT index, HREFTYPE *ref)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!ref)
        return E_INVALIDARG;
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    if (index >= t->ImplCount) {
        if (index == 0xFFFFFFFFu && (t->Flags & TYPEFLAG_FDUAL)) {
            *ref = HREF_OTHER_HALF;
            return S_OK;
        }
        return TYPE_E_ELEMENTNOTFOUND;
    }
    switch (view_kind(info)) {
    case TKIND_INTERFACE: *ref = t->Impls[index].Ref | HREF_VTABLE_HALF; break;
    case TKIND_DISPATCH: *ref = t->Lib->DispatchRef; break;
    case TKIND_COCLASS: *ref = t->Impls[index].Ref & ~HREF_VTABLE_HALF; break;
    default: *ref = 0; break;
    }
    return S_OK;
}

/* a type from another library via the import table, its vtable half when the handle asks and one exists. */
static HRESULT imported_type(TL_TYPE *from, HREFTYPE handle, ITypeInfo **out)
{
    TL_LIB *lib = from->Lib;
    ULONG offset = handle & ~3u;
    if (offset % 12 || offset / 12 >= lib->ImportCount)
        return E_FAIL;
    ITypeInfo *type;
    HRESULT hr = TlResolveHref(from, offset | 1, &type);
    if (FAILED(hr) || !(handle & HREF_VTABLE_HALF)) {
        *out = FAILED(hr) ? nullptr : type;
        return hr;
    }
    /* ask the imported type for its other half; without one it is its own. */
    HREFTYPE other;
    hr = type->lpVtbl->GetRefTypeOfImplType(type, 0xFFFFFFFFu, &other);
    if (hr == TYPE_E_ELEMENTNOTFOUND) {
        *out = type;
        return S_OK;
    }
    if (SUCCEEDED(hr)) {
        ITypeInfo *half;
        hr = type->lpVtbl->GetRefTypeInfo(type, other, &half);
        type->lpVtbl->Release(type);
        *out = SUCCEEDED(hr) ? half : nullptr;
        return hr;
    }
    type->lpVtbl->Release(type);
    return hr;
}

/* where foreign handles are forwarded: an interface's base, or a coclass's first interface. */
static HRESULT handle_base(TYPEINFO *info, ITypeInfo **base)
{
    TL_TYPE *t = info->Type;
    if (view_kind(info) != TKIND_COCLASS)
        return base_info(info, base);
    if (!t->ImplCount)
        return TYPE_E_ELEMENTNOTFOUND;
    return TlResolveHref(t, t->Impls[0].Ref, base);
}

static HRESULT WINAPI ti_get_ref_type_info(ITypeInfo2 *self, HREFTYPE handle, ITypeInfo **out)
{
    TYPEINFO *info = from_info(self);
    TL_TYPE *t = info->Type;
    if (!out || handle == HREF_INVALID)
        return E_INVALIDARG;
    *out = nullptr;
    if (handle == HREF_OTHER_HALF) {
        if (!info->Twin)
            return TYPE_E_ELEMENTNOTFOUND;
        *out = (ITypeInfo *)info->Twin;
        ti_addref((ITypeInfo2 *)info->Twin);
        return S_OK;
    }
    HRESULT hr = TlLoadMembers(t);
    if (FAILED(hr))
        return hr;
    /* a handle carrying another level was made by a base type. */
    ULONG level = HREF_DEPTH(handle);
    if (level) {
        TYPEKIND kind = view_kind(info);
        if (kind != TKIND_INTERFACE && kind != TKIND_DISPATCH && kind != TKIND_COCLASS)
            return E_UNEXPECTED;
        if (level != (ULONG)t->InheritedImpls + 1) {
            ITypeInfo *base;
            hr = handle_base(info, &base);
            if (FAILED(hr))
                return E_INVALIDARG;
            hr = base->lpVtbl->GetRefTypeInfo(base, handle, out);
            base->lpVtbl->Release(base);
            return hr;
        }
    }
    handle &= 0xFFFFFF;
    if (handle & 1)
        return imported_type(t, handle, out);
    ULONG index;
    if ((handle & 0x7C) || !TlIndexOfHref(handle, &index) || index >= t->Lib->TypeCount)
        return E_FAIL;
    if (t->Lib->Types[index]->Kind == TKIND_MAX)
        return TYPE_E_UNDEFINEDTYPE;
    return TlGetTypeInfo(t->Lib, index, (handle & HREF_VTABLE_HALF) != 0, out);
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

/* searches the type, then its bases, and returns the owning type too. */
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
    /* argument names map to their position among the parameters automation passes. */
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

/* the library's own flags, some of which a dispatch interface's TYPEATTR hides. */
static HRESULT WINAPI ti_get_type_flags(ITypeInfo2 *self, ULONG *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = from_info(self)->Type->Flags;
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

/* a function reachable by this kind of call is preferred over a variable; also returns the owning type. */
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

/* the other half of a dual interface, referenced, or NULL when not dual. */
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
