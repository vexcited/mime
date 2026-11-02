/*
 * automation: calling the members of objects from the script, and the objects the engine serves, which are the
 * instances of the script's classes, the Err object, procedure references and the global namespace.
 */
#include "vbs.h"

LONG g_vbs_objects;

/* ------------------------------------------------------------------ calling out */

static void free_excep(EXCEPINFO *x)
{
    SysFreeString(x->bstrSource);
    SysFreeString(x->bstrDescription);
    SysFreeString(x->bstrHelpFile);
}

BOOL is_script_instance(IDispatch *d)
{
    extern const IDispatchVtbl g_instance_vtbl;
    return d && d->lpVtbl == &g_instance_vtbl;
}

static int inst_invoke(Frame *f, Instance *inst, DISPID id, PCWSTR name, WORD flags, VARIANT *argv, UINT argc, VARIANT *result);
static int err_invoke(Frame *f, DISPID id, PCWSTR name, WORD flags, VARIANT *argv, UINT argc, VARIANT *result);
static DISPID err_id_of(PCWSTR name);

static IDispatch *dispatch_of(Frame *f, VARIANT *object)
{
    IDispatch *d = nullptr;
    if (object->vt == VT_DISPATCH)
        d = object->pdispVal;
    else if (object->vt == VT_UNKNOWN && object->punkVal) {
        object->punkVal->lpVtbl->QueryInterface(object->punkVal, &IID_IDispatch, (void **)&d);
        if (d) {
            d->lpVtbl->Release(d);
        }
    }
    return d;
}

static int call_com(Frame *f, IDispatch *d, DISPID id, WORD flags, VARIANT *argv, UINT argc, VARIANT *result)
{
    VARIANT stack[9];
    VARIANT *rev = argc <= 9 ? stack : vbs_alloc(argc * sizeof(VARIANT));
    for (UINT i = 0; i < argc; i++)
        rev[i] = argv[argc - 1 - i];
    DISPPARAMS dp = {rev, nullptr, argc, 0};
    DISPID put = DISPID_PROPERTYPUT;
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        dp.cNamedArgs = 1;
        dp.rgdispidNamedArgs = &put;
    }
    EXCEPINFO ex = {};
    UINT arg_err = 0;
    VARIANT local;
    VariantInit(&local);
    HRESULT hr = d->lpVtbl->Invoke(d, id, &IID_NULL, f->E->Lcid, flags, &dp, result ? &local : nullptr, &ex, &arg_err);
    if (rev != stack)
        vbs_free(rev);
    if (hr == DISP_E_EXCEPTION) {
        if (ex.pfnDeferredFillIn)
            ex.pfnDeferredFillIn(&ex);
        HRESULT scode = ex.scode ? ex.scode : (HRESULT)ex.wCode;
        if (scode >= 0)
            scode = E_FAIL;
        int rc = raise_hresult(f, scode, ex.bstrSource, ex.bstrDescription);
        free_excep(&ex);
        VariantClear(&local);
        return rc;
    }
    if (FAILED(hr)) {
        free_excep(&ex);
        VariantClear(&local);
        int rc = raise_hresult(f, hr, nullptr, nullptr);
        if (f->E->Err.Number == (LONG)hr || ((ULONG)hr >> 16) != 0x800A) {
            /* an hresult that has no vbscript error stands as it is, with the engine's own words for none. */
        }
        return rc;
    }
    free_excep(&ex);
    /* the integer types of C that the language has no name for are an automation type it does not support. */
    if (result && (local.vt == VT_INT || local.vt == VT_UINT)) {
        VariantClear(&local);
        return raise_error(f, E_VBS_BAD_VARTYPE, nullptr);
    }
    if (result) {
        if (local.vt == (VT_VARIANT | VT_BYREF)) {
            var_deref(result, &local);
            VariantClear(&local);
        } else
            *result = local;
    }
    return R_OK;
}

int obj_get(Frame *f, VARIANT *object, PCWSTR name, Args *unused, VARIANT *argv, UINT argc, WORD flags, VARIANT *result, DISPID *cache)
{
    IDispatch *d = dispatch_of(f, object);
    if (!d)
        return raise_error(f, E_VBS_OBJECT_REQUIRED, nullptr);
    VARIANT dummy;
    VariantInit(&dummy);
    if (!result)
        result = &dummy;
    if (d == f->E->ErrObject)
        return err_invoke(f, 0, name, flags, argv, argc, result);
    if (is_script_instance(d))
        return inst_invoke(f, (Instance *)d, DISPID_UNKNOWN, name, flags, argv, argc, result);
    DISPID id;
    LPOLESTR names[1] = {(LPOLESTR)name};
    HRESULT hr = d->lpVtbl->GetIDsOfNames(d, &IID_NULL, names, 1, f->E->Lcid, &id);
    if (FAILED(hr)) {
        return raise_hresult(f, hr == DISP_E_UNKNOWNNAME || hr == DISP_E_MEMBERNOTFOUND ? (HRESULT)DISP_E_MEMBERNOTFOUND : hr,
                             nullptr, nullptr);
    }
    return call_com(f, d, id, flags, argv, argc, result);
}

int obj_put(Frame *f, VARIANT *object, PCWSTR name, VARIANT *argv, UINT argc, VARIANT *value, BOOL by_ref)
{
    IDispatch *d = dispatch_of(f, object);
    if (!d)
        return raise_error(f, E_VBS_OBJECT_REQUIRED, nullptr);
    VARIANT args[10];
    UINT n = argc + 1;
    if (n > 10)
        return raise_error(f, E_VBS_WRONG_ARGS, nullptr);
    for (UINT i = 0; i < argc; i++)
        args[i] = argv[i];
    args[argc] = *value;
    WORD flags = by_ref ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT;
    if (d == f->E->ErrObject)
        return err_invoke(f, 0, name, flags, args, n, nullptr);
    if (is_script_instance(d))
        return inst_invoke(f, (Instance *)d, DISPID_UNKNOWN, name, flags, args, n, nullptr);
    DISPID id;
    LPOLESTR names[1] = {(LPOLESTR)name};
    HRESULT hr = d->lpVtbl->GetIDsOfNames(d, &IID_NULL, names, 1, f->E->Lcid, &id);
    if (FAILED(hr))
        return raise_hresult(f, hr == DISP_E_UNKNOWNNAME ? (HRESULT)DISP_E_MEMBERNOTFOUND : hr, nullptr, nullptr);
    return call_com(f, d, id, flags, args, n, nullptr);
}

int obj_default(Frame *f, VARIANT *object, WORD flags, VARIANT *argv, UINT argc, VARIANT *result)
{
    IDispatch *d = dispatch_of(f, object);
    if (!d)
        return raise_error(f, E_VBS_NOT_SET, nullptr);
    VARIANT dummy;
    VariantInit(&dummy);
    if (!result && !(flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)))
        result = &dummy;
    if (d == f->E->ErrObject)
        return err_invoke(f, 0, nullptr, flags, argv, argc, result);
    if (is_script_instance(d))
        return inst_invoke(f, (Instance *)d, DISPID_VALUE, nullptr, flags, argv, argc, result);
    return call_com(f, d, DISPID_VALUE, flags, argv, argc, result);
}

int obj_enum(Frame *f, VARIANT *collection, IEnumVARIANT **out)
{
    IDispatch *d = dispatch_of(f, collection);
    if (!d)
        return raise_error(f, E_VBS_NOT_SET, nullptr);
    VARIANT r;
    VariantInit(&r);
    if (is_script_instance(d) || d == f->E->ErrObject)
        return raise_error(f, E_VBS_NO_MEMBER, nullptr);
    int rc = call_com(f, d, DISPID_NEWENUM, DISPATCH_METHOD | DISPATCH_PROPERTYGET, nullptr, 0, &r);
    if (rc != R_OK)
        return rc;
    IUnknown *u = r.vt == VT_UNKNOWN || r.vt == VT_DISPATCH ? r.punkVal : nullptr;
    HRESULT hr = E_NOINTERFACE;
    *out = nullptr;
    if (u)
        hr = u->lpVtbl->QueryInterface(u, &IID_IEnumVARIANT, (void **)out);
    VariantClear(&r);
    if (FAILED(hr))
        return raise_error(f, E_VBS_NOT_COLLECTION, nullptr);
    return R_OK;
}

/* ------------------------------------------------------------------ the instances of classes */

static HRESULT STDMETHODCALLTYPE inst_qi(IDispatch *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE inst_addref(IDispatch *This)
{
    return InterlockedIncrement(&((Instance *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE inst_release(IDispatch *This)
{
    Instance *inst = (Instance *)This;
    LONG refs = InterlockedDecrement(&inst->Refs);
    if (refs == 0)
        instance_destroy(inst);
    return refs;
}

static HRESULT STDMETHODCALLTYPE inst_count(IDispatch *This, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE inst_typeinfo(IDispatch *This, UINT i, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return DISP_E_BADINDEX;
}

static HRESULT STDMETHODCALLTYPE inst_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    Instance *inst = (Instance *)This;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        Member *m = member_find(inst->Cls, names[i], name_hash(names[i]));
        if (m && (m->Public & 1)) {
            ids[i] = m->Id;
        } else {
            ids[i] = DISPID_UNKNOWN;
            hr = DISP_E_UNKNOWNNAME;
        }
        if (i)
            ids[i] = DISPID_UNKNOWN;
    }
    return hr;
}

static void exception_of(Engine *e, EXCEPINFO *ex)
{
    if (!ex)
        return;
    memset(ex, 0, sizeof(*ex));
    ex->scode = e->Fault.Scode;
    ex->bstrSource = e->Fault.Source ? SysAllocString(e->Fault.Source) : nullptr;
    ex->bstrDescription = e->Fault.Description ? SysAllocString(e->Fault.Description) : nullptr;
}

static HRESULT STDMETHODCALLTYPE inst_invoke_com(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                 DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *arg_err)
{
    Instance *inst = (Instance *)This;
    Engine *e = inst->E;
    Frame f = {};
    f.E = e;
    f.Unit = inst->Cls->Unit;
    UINT argc = params ? params->cArgs : 0;
    VARIANT args[10];
    if (argc > 10)
        return DISP_E_BADPARAMCOUNT;
    for (UINT i = 0; i < argc; i++)
        args[i] = params->rgvarg[argc - 1 - i];
    HRESULT enter = engine_enter(e);
    VARIANT local;
    VariantInit(&local);
    int rc = inst_invoke(&f, inst, id, nullptr, flags, args, argc, result ? &local : nullptr);
    HRESULT hr = S_OK;
    if (rc != R_OK) {
        if (e->NestedRun == 1) {
            engine_report(e, FALSE);
            hr = SCRIPT_E_REPORTED;
        } else if (e->Fault.Set) {
            exception_of(e, ex);
            hr = DISP_E_EXCEPTION;
        } else
            hr = DISP_E_MEMBERNOTFOUND;
    } else if (result)
        *result = local;
    engine_leave(e);
    (void)enter;
    return hr;
}

const IDispatchVtbl g_instance_vtbl = {inst_qi, inst_addref, inst_release, inst_count, inst_typeinfo, inst_names,
                                       inst_invoke_com};

static Member *member_by_id(Class *cls, DISPID id)
{
    if (id == DISPID_VALUE)
        return cls->DefaultMember;
    for (Member *m = cls->Members; m; m = m->Next)
        if (m->Id == id)
            return m;
    return nullptr;
}

static int inst_invoke(Frame *f, Instance *inst, DISPID id, PCWSTR name, WORD flags, VARIANT *argv, UINT argc, VARIANT *result)
{
    Member *m = name ? member_find(inst->Cls, name, name_hash(name)) : member_by_id(inst->Cls, id);
    BOOL inside = f->Me == inst;
    if (!m || (!(m->Public & 1) && !inside))
        return raise_error(f, E_VBS_NO_MEMBER, nullptr);
    Engine *e = f->E;
    if (m->Kind == M_FIELD) {
        VARIANT *cell = &inst->Fields[m->Slot];
        if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
            if (argc != 1)
                return raise_error(f, E_VBS_WRONG_ARGS, m->Name);
            VARIANT v;
            var_deref(&v, &argv[0]);
            if ((flags & DISPATCH_PROPERTYPUT) && (v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN)) {
                int rc = value_of(f, &v);
                if (rc != R_OK) {
                    VariantClear(&v);
                    return rc;
                }
            }
            VariantClear(cell);
            *cell = v;
            return R_OK;
        }
        if (argc) {
            VARIANT *c = cell;
            if (c->vt & VT_ARRAY) {
                SAFEARRAY *sa = c->parray;
                LONG idx[8];
                if (sa->cDims != argc || argc > 8)
                    return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
                for (UINT i = 0; i < argc; i++) {
                    VARIANT v;
                    var_deref(&v, &argv[i]);
                    LONG l;
                    int rc = to_long(f, &v, &l, nullptr);
                    VariantClear(&v);
                    if (rc != R_OK)
                        return rc;
                    idx[i] = l;
                }
                VARIANT *p;
                if (FAILED(SafeArrayPtrOfIndex(sa, idx, (void **)&p)))
                    return raise_error(f, E_VBS_SUBSCRIPT, nullptr);
                VariantCopyInd(result, p);
                return R_OK;
            }
            return raise_error(f, E_VBS_WRONG_ARGS, m->Name);
        }
        if (result)
            VariantCopyInd(result, cell);
        return R_OK;
    }
    if (m->Kind == M_METHOD) {
        if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF))
            return raise_error(f, E_VBS_ILLEGAL_ASSIGN, m->Name);
        VARIANT r;
        VariantInit(&r);
        int rc = call_proc(e, m->Method, inst, argv, argc, &r, f);
        if (rc != R_OK)
            return rc;
        if (result)
            *result = r;
        else
            VariantClear(&r);
        return R_OK;
    }
    /* a property. */
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        BOOL ref = (flags & DISPATCH_PROPERTYPUTREF) != 0;
        Proc *p = ref ? m->Set : m->Let;
        VARIANT *last = &argv[argc - 1];
        if (!p && !ref && m->Set && (last->vt == VT_DISPATCH || last->vt == VT_UNKNOWN))
            p = m->Set;
        if (!p && ref && m->Let)
            p = m->Let;
        if (!p)
            return raise_error(f, E_VBS_NO_MEMBER, nullptr);
        return call_proc(e, p, inst, argv, argc, nullptr, f);
    }
    if (!m->Get)
        return raise_error(f, E_VBS_NO_MEMBER, nullptr);
    VARIANT r;
    VariantInit(&r);
    int rc = call_proc(e, m->Get, inst, argv, argc, &r, f);
    if (rc != R_OK)
        return rc;
    if (result)
        *result = r;
    else
        VariantClear(&r);
    return R_OK;
}

/* ------------------------------------------------------------------ the Err object */

typedef struct ErrObj {
    IDispatch Dispatch;
    Engine *E;
} ErrObj;

enum { ERR_NUMBER = 0, ERR_DESCRIPTION = 1, ERR_SOURCE = 2, ERR_HELPFILE = 3, ERR_HELPCONTEXT = 4, ERR_LASTDLL = 5,
       ERR_CLEAR = 10, ERR_RAISE = 11 };

static DISPID err_id_of(PCWSTR name)
{
    static const struct {
        PCWSTR Name;
        DISPID Id;
    } t[] = {{L"Number", ERR_NUMBER}, {L"Description", ERR_DESCRIPTION}, {L"Source", ERR_SOURCE},
             {L"HelpFile", ERR_HELPFILE}, {L"HelpContext", ERR_HELPCONTEXT}, {L"LastDllError", ERR_LASTDLL},
             {L"Clear", ERR_CLEAR}, {L"Raise", ERR_RAISE}};
    for (UINT i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (name_equal(name, t[i].Name))
            return t[i].Id;
    return DISPID_UNKNOWN;
}

static int err_raise(Frame *f, VARIANT *argv, UINT argc)
{
    Engine *e = f->E;
    if (argc < 1 || argc > 5)
        return raise_error(f, E_VBS_WRONG_ARGS, nullptr);
    VARIANT a[5];
    for (UINT i = 0; i < 5; i++)
        VariantInit(&a[i]);
    int rc = R_OK;
    LONG number = 0;
    for (UINT i = 0; i < argc && rc == R_OK; i++) {
        if (argv[i].vt == VT_ERROR && argv[i].scode == DISP_E_PARAMNOTFOUND)
            continue;
        var_deref(&a[i], &argv[i]);
        if (a[i].vt == VT_DISPATCH || a[i].vt == VT_UNKNOWN)
            rc = value_of(f, &a[i]);
    }
    if (rc == R_OK)
        rc = to_long(f, &a[0], &number, L"Err.Raise");
    if (rc != R_OK) {
        for (UINT i = 0; i < 5; i++)
            VariantClear(&a[i]);
        return rc;
    }
    if (number == 0 || number > 0xFFFF) {
        for (UINT i = 0; i < 5; i++)
            VariantClear(&a[i]);
        return raise_error(f, E_VBS_INVALID_CALL, nullptr);
    }
    BSTR strs[4] = {};
    for (int i = 1; i < 5; i++) {
        if (a[i].vt == VT_EMPTY)
            continue;
        VARIANT t;
        VariantInit(&t);
        if (i == 4) {
            LONG ctx;
            rc = to_long(f, &a[i], &ctx, L"Err.Raise");
            if (rc != R_OK)
                break;
            e->Err.HelpContext = ctx;
        } else if (FAILED(VariantChangeTypeEx(&t, &a[i], e->Lcid, 0, VT_BSTR))) {
            rc = raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
            break;
        } else
            strs[i] = t.bstrVal;
    }
    if (rc == R_OK) {
        e->Err.Number = number;
        if (strs[1]) {
            SysFreeString(e->Err.Source);
            e->Err.Source = strs[1];
            strs[1] = nullptr;
        }
        if (strs[2]) {
            SysFreeString(e->Err.Description);
            e->Err.Description = strs[2];
            strs[2] = nullptr;
        }
        if (strs[3]) {
            SysFreeString(e->Err.HelpFile);
            e->Err.HelpFile = strs[3];
            strs[3] = nullptr;
        }
        if (!e->Err.Source || !*e->Err.Source) {
            SysFreeString(e->Err.Source);
            e->Err.Source = SysAllocString(L"Microsoft VBScript runtime error");
        }
        if (!e->Err.Description || !*e->Err.Description) {
            SysFreeString(e->Err.Description);
            PCWSTR text = error_text(number);
            e->Err.Description = SysAllocString(text ? text : L"Unknown runtime error");
        }
        if (!e->Err.HelpFile)
            e->Err.HelpFile = SysAllocString(L"");
        PCWSTR src = e->Err.Source, desc = e->Err.Description;
        fault_clear(e);
        e->Fault.Set = TRUE;
        e->Fault.Scode = error_hresult(number);
        e->Fault.Source = SysAllocString(src);
        e->Fault.Description = SysAllocString(desc);
        e->Fault.Line = f->Line;
        e->Fault.Col = f->Col;
        e->Fault.Cookie = f->Unit ? f->Unit->Cookie : 0;
        rc = R_ERR;
    }
    for (UINT i = 1; i < 4; i++)
        SysFreeString(strs[i]);
    for (UINT i = 0; i < 5; i++)
        VariantClear(&a[i]);
    return rc;
}

static int err_invoke(Frame *f, DISPID id, PCWSTR name, WORD flags, VARIANT *argv, UINT argc, VARIANT *result)
{
    Engine *e = f->E;
    if (name)
        id = err_id_of(name);
    if (id == DISPID_UNKNOWN)
        return raise_error(f, E_VBS_NO_MEMBER, nullptr);
    if (id == ERR_CLEAR) {
        err_clear(e);
        return R_OK;
    }
    if (id == ERR_RAISE)
        return err_raise(f, argv, argc);
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        VARIANT v;
        var_deref(&v, &argv[argc - 1]);
        if (v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN)
            value_of(f, &v);
        int rc = R_OK;
        switch (id) {
        case ERR_NUMBER: {
            LONG n;
            rc = to_long(f, &v, &n, nullptr);
            if (rc == R_OK)
                e->Err.Number = n;
            break;
        }
        case ERR_HELPCONTEXT: {
            LONG n;
            rc = to_long(f, &v, &n, nullptr);
            if (rc == R_OK)
                e->Err.HelpContext = n;
            break;
        }
        case ERR_DESCRIPTION:
        case ERR_SOURCE:
        case ERR_HELPFILE: {
            VARIANT s;
            VariantInit(&s);
            if (FAILED(VariantChangeTypeEx(&s, &v, e->Lcid, 0, VT_BSTR)))
                rc = raise_error(f, E_VBS_TYPE_MISMATCH, nullptr);
            else {
                BSTR *slot = id == ERR_DESCRIPTION ? &e->Err.Description : id == ERR_SOURCE ? &e->Err.Source : &e->Err.HelpFile;
                SysFreeString(*slot);
                *slot = s.bstrVal;
            }
            break;
        }
        default:
            rc = raise_error(f, E_VBS_ILLEGAL_ASSIGN, nullptr);
        }
        VariantClear(&v);
        return rc;
    }
    if (!result)
        return R_OK;
    VariantInit(result);
    switch (id) {
    case ERR_NUMBER:
        result->vt = VT_I4;
        result->lVal = e->Err.Number;
        break;
    case ERR_HELPCONTEXT:
        result->vt = VT_I4;
        result->lVal = e->Err.HelpContext;
        break;
    case ERR_LASTDLL:
        result->vt = VT_I4;
        result->lVal = 0;
        break;
    case ERR_DESCRIPTION:
    case ERR_SOURCE:
    case ERR_HELPFILE: {
        BSTR s = id == ERR_DESCRIPTION ? e->Err.Description : id == ERR_SOURCE ? e->Err.Source : e->Err.HelpFile;
        result->vt = VT_BSTR;
        result->bstrVal = SysAllocStringLen(s ? s : L"", s ? SysStringLen(s) : 0);
        break;
    }
    }
    return R_OK;
}

static HRESULT STDMETHODCALLTYPE errobj_qi(IDispatch *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE errobj_addref(IDispatch *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE errobj_release(IDispatch *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE errobj_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = i ? DISPID_UNKNOWN : err_id_of(names[i]);
        if (ids[i] == DISPID_UNKNOWN)
            hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE errobj_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                               DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *arg_err)
{
    Engine *e = ((ErrObj *)This)->E;
    Frame f = {};
    f.E = e;
    UINT argc = params ? params->cArgs : 0;
    VARIANT args[6];
    if (argc > 6)
        return DISP_E_BADPARAMCOUNT;
    for (UINT i = 0; i < argc; i++)
        args[i] = params->rgvarg[argc - 1 - i];
    int rc = err_invoke(&f, id, nullptr, flags, args, argc, result);
    if (rc != R_OK) {
        exception_of(e, ex);
        return DISP_E_EXCEPTION;
    }
    return S_OK;
}

static const IDispatchVtbl g_errobj_vtbl = {errobj_qi, errobj_addref, errobj_release, inst_count, inst_typeinfo,
                                            errobj_names, errobj_invoke};

IDispatch *err_object_create(Engine *e)
{
    ErrObj *o = vbs_alloc(sizeof(ErrObj));
    o->Dispatch.lpVtbl = &g_errobj_vtbl;
    o->E = e;
    return &o->Dispatch;
}

/* ------------------------------------------------------------------ references to procedures */

typedef struct ProcRef {
    IDispatch Dispatch;
    LONG Refs;
    Engine *E;
    Proc *P;
    Instance *Me;
} ProcRef;

static ULONG STDMETHODCALLTYPE ref_addref(IDispatch *This)
{
    return InterlockedIncrement(&((ProcRef *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE ref_release(IDispatch *This)
{
    ProcRef *r = (ProcRef *)This;
    LONG refs = InterlockedDecrement(&r->Refs);
    if (!refs) {
        extern void engine_release(Engine *e);
        engine_release(r->E);
        vbs_free(r);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE ref_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

static HRESULT STDMETHODCALLTYPE ref_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                            DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *arg_err)
{
    ProcRef *r = (ProcRef *)This;
    Engine *e = r->E;
    if (id != DISPID_VALUE)
        return DISP_E_MEMBERNOTFOUND;
    Frame f = {};
    f.E = e;
    f.Unit = r->P->Unit;
    UINT argc = params ? params->cArgs : 0;
    VARIANT args[16];
    if (argc > 16)
        return DISP_E_BADPARAMCOUNT;
    for (UINT i = 0; i < argc; i++)
        args[i] = params->rgvarg[argc - 1 - i];
    engine_enter(e);
    VARIANT local;
    VariantInit(&local);
    int rc = call_proc(e, r->P, r->Me, args, argc, result ? &local : nullptr, &f);
    HRESULT hr = S_OK;
    if (rc != R_OK) {
        if (e->NestedRun == 1) {
            engine_report(e, FALSE);
            hr = SCRIPT_E_REPORTED;
        } else {
            exception_of(e, ex);
            hr = DISP_E_EXCEPTION;
        }
    } else if (result)
        *result = local;
    engine_leave(e);
    return hr;
}

static const IDispatchVtbl g_ref_vtbl = {errobj_qi, ref_addref, ref_release, inst_count, inst_typeinfo, ref_names, ref_invoke};

IDispatch *proc_ref_create(Engine *e, Proc *proc, Instance *me)
{
    ProcRef *r = vbs_alloc(sizeof(ProcRef));
    r->Dispatch.lpVtbl = &g_ref_vtbl;
    r->Refs = 1;
    r->E = e;
    r->P = proc;
    r->Me = me;
    InterlockedIncrement(&e->Refs);
    return &r->Dispatch;
}

/* ------------------------------------------------------------------ the global namespace */

typedef struct ScriptDispatch {
    IDispatch Dispatch;
    LONG Refs;
    Engine *E;
    Sym **Ids;
    UINT Count, Capacity;
    BOOL NoMembers;
} ScriptDispatch;

static ULONG STDMETHODCALLTYPE sd_addref(IDispatch *This)
{
    return InterlockedIncrement(&((ScriptDispatch *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE sd_release(IDispatch *This)
{
    ScriptDispatch *s = (ScriptDispatch *)This;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs) {
        extern void engine_release(Engine *e);
        engine_release(s->E);
        vbs_free(s->Ids);
        vbs_free(s);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE sd_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    ScriptDispatch *s = (ScriptDispatch *)This;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        Sym *sym = i || s->NoMembers ? nullptr : sym_find(s->E, names[i], name_hash(names[i]));
        if (!sym || (sym->Kind != SYM_VAR && sym->Kind != SYM_PROC && sym->Kind != SYM_CONST && sym->Kind != SYM_CLASS)) {
            hr = DISP_E_UNKNOWNNAME;
            continue;
        }
        UINT k;
        for (k = 0; k < s->Count; k++)
            if (s->Ids[k] == sym)
                break;
        if (k == s->Count) {
            if (s->Count == s->Capacity) {
                s->Capacity = s->Capacity ? s->Capacity * 2 : 16;
                s->Ids = vbs_realloc(s->Ids, s->Capacity * sizeof(Sym *));
            }
            s->Ids[s->Count++] = sym;
        }
        ids[i] = 1 + (DISPID)k;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE sd_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *ex, UINT *arg_err)
{
    ScriptDispatch *s = (ScriptDispatch *)This;
    Engine *e = s->E;
    if (id < 1 || (UINT)id > s->Count)
        return DISP_E_MEMBERNOTFOUND;
    Sym *sym = s->Ids[id - 1];
    Frame f = {};
    f.E = e;
    UINT argc = params ? params->cArgs : 0;
    VARIANT args[16];
    if (argc > 16)
        return DISP_E_BADPARAMCOUNT;
    for (UINT i = 0; i < argc; i++)
        args[i] = params->rgvarg[argc - 1 - i];
    if (e->State == SCRIPTSTATE_CLOSED || e->State == SCRIPTSTATE_UNINITIALIZED)
        return E_UNEXPECTED;
    engine_enter(e);
    int rc = R_OK;
    VARIANT local;
    VariantInit(&local);
    if (sym->Kind == SYM_PROC) {
        f.Unit = sym->Procedure->Unit;
        rc = call_proc(e, sym->Procedure, nullptr, args, argc, result ? &local : nullptr, &f);
    } else if (sym->Kind == SYM_VAR || sym->Kind == SYM_CONST) {
        if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
            if (sym->Kind == SYM_CONST || !argc)
                rc = raise_error(&f, E_VBS_ILLEGAL_ASSIGN, sym->Name);
            else {
                VariantClear(&sym->Value);
                var_deref(&sym->Value, &args[argc - 1]);
            }
        } else if (result)
            VariantCopyInd(&local, &sym->Value);
    } else
        rc = raise_error(&f, E_VBS_TYPE_MISMATCH, sym->Name);
    HRESULT hr = S_OK;
    if (rc != R_OK) {
        if (e->NestedRun == 1) {
            engine_report(e, FALSE);
            hr = SCRIPT_E_REPORTED;
        } else {
            exception_of(e, ex);
            hr = DISP_E_EXCEPTION;
        }
    } else if (result)
        *result = local;
    engine_leave(e);
    return hr;
}

static const IDispatchVtbl g_sd_vtbl = {errobj_qi, sd_addref, sd_release, inst_count, inst_typeinfo, sd_names, sd_invoke};

IDispatch *script_dispatch_create(Engine *e)
{
    ScriptDispatch *s = vbs_alloc(sizeof(ScriptDispatch));
    s->Dispatch.lpVtbl = &g_sd_vtbl;
    s->Refs = 1;
    s->E = e;
    InterlockedIncrement(&e->Refs);
    return &s->Dispatch;
}

/* ------------------------------------------------------------------ creating objects */

int create_object(Frame *f, PCWSTR progid, VARIANT *out)
{
    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(progid, &clsid);
    if (FAILED(hr)) {
        HRESULT h2 = CLSIDFromString((LPOLESTR)progid, &clsid);
        if (FAILED(h2))
            return raise_error(f, E_VBS_CANNOT_CREATE, progid);
    }
    IDispatch *d = nullptr;
    hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IDispatch, (void **)&d);
    if (FAILED(hr) || !d)
        return raise_error(f, hr == REGDB_E_CLASSNOTREG || hr == E_NOINTERFACE ? E_VBS_CANNOT_CREATE : E_VBS_CANNOT_CREATE, progid);
    out->vt = VT_DISPATCH;
    out->pdispVal = d;
    return R_OK;
}
