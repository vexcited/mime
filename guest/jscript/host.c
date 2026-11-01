/*
 * the objects of the host: automation objects as values of the language, the conversions of variants, ActiveXObject,
 * Enumerator and VBArray, and the objects of the script that hosts see through IDispatchEx.
 */
#include "js.h"

#define ARG(i) ((i) < argc ? args[i] : UNDEF)

/* ------------------------------------------------------------------ names that hosts know as numbers */

DISPID disp_id_of(Engine *e, Str *name)
{
    name = str_intern_str(e, name);
    for (ULONG i = 0; i < e->DispCount; i++)
        if (e->DispNames[i] == name)
            return (DISPID)(i + 1);
    if (e->DispCount == e->DispCap) {
        e->DispCap = e->DispCap ? e->DispCap * 2 : 64;
        e->DispNames = js_realloc(e->DispNames, e->DispCap * sizeof(Str *));
    }
    e->DispNames[e->DispCount++] = name;
    return (DISPID)e->DispCount;
}

Str *disp_name_of(Engine *e, DISPID id)
{
    if (id < 1 || (ULONG)id > e->DispCount)
        return nullptr;
    return e->DispNames[id - 1];
}

/* ------------------------------------------------------------------ variants and values */

static VarBox *box_variant(Engine *e, const VARIANT *v)
{
    VarBox *b = gc_alloc(e, sizeof(VarBox), GC_VARBOX);
    VariantInit(&b->v);
    VariantCopy(&b->v, (VARIANT *)v);
    return b;
}

void varbox_free(Engine *e, VarBox *b)
{
    if (b->v.vt == VT_DISPATCH || b->v.vt == VT_UNKNOWN) {
        if (b->v.punkVal) {
            if (e->DeferredCount == e->DeferredCap) {
                e->DeferredCap = e->DeferredCap ? e->DeferredCap * 2 : 32;
                e->Deferred = js_realloc(e->Deferred, e->DeferredCap * sizeof(void *));
            }
            e->Deferred[e->DeferredCount++] = b->v.punkVal;
        }
        return;
    }
    VariantClear(&b->v);
}

BOOL host_same_object(Obj *a, Obj *b)
{
    if (a == b)
        return TRUE;
    if (a->cls != C_HOST || b->cls != C_HOST)
        return FALSE;
    IUnknown *x = nullptr, *y = nullptr;
    a->u.host.disp->lpVtbl->QueryInterface(a->u.host.disp, &IID_IUnknown, (void **)&x);
    b->u.host.disp->lpVtbl->QueryInterface(b->u.host.disp, &IID_IUnknown, (void **)&y);
    BOOL same = x && x == y;
    if (x)
        x->lpVtbl->Release(x);
    if (y)
        y->lpVtbl->Release(y);
    return same;
}

Obj *host_wrap(Engine *e, IDispatch *d)
{
    Obj *o = obj_new(e, C_HOST, nullptr);
    o->u.host.disp = d;
    d->lpVtbl->AddRef(d);
    return o;
}

void host_release_obj(Engine *e, Obj *o)
{
    switch (o->cls) {
    case C_HOST:
        if (o->u.host.disp) {
            if (e->DeferredCount == e->DeferredCap) {
                e->DeferredCap = e->DeferredCap ? e->DeferredCap * 2 : 32;
                e->Deferred = js_realloc(e->Deferred, e->DeferredCap * sizeof(void *));
            }
            e->Deferred[e->DeferredCount++] = o->u.host.disp;
            o->u.host.disp = nullptr;
        }
        break;
    case C_ENUMERATOR:
        if (o->u.en.en) {
            if (e->DeferredCount == e->DeferredCap) {
                e->DeferredCap = e->DeferredCap ? e->DeferredCap * 2 : 32;
                e->Deferred = js_realloc(e->Deferred, e->DeferredCap * sizeof(void *));
            }
            e->Deferred[e->DeferredCount++] = o->u.en.en;
            o->u.en.en = nullptr;
        }
        VariantClear(&o->u.en.cur);
        break;
    case C_VBARRAY:
        if (o->u.vba.sa)
            SafeArrayDestroy(o->u.vba.sa);
        break;
    case C_REGEXP:
        if (o->u.re.re)
            regexp_free(o->u.re.re);
        break;
    default:
        break;
    }
}

IDispatch *dispatch_for_obj(Engine *e, Obj *o);

int variant_to_val(Engine *e, const VARIANT *vin, Val *out)
{
    VARIANT tmp;
    const VARIANT *v = vin;
    VARTYPE vt = v->vt;
    VariantInit(&tmp);
    BOOL copied = FALSE;
    if (vt & VT_BYREF) {
        if (vt == (VT_VARIANT | VT_BYREF)) {
            if (!v->pvarVal) {
                *out = UNDEF;
                return RC_OK;
            }
            return variant_to_val(e, v->pvarVal, out);
        }
        if (!(vt & VT_ARRAY) && !v->byref) {
            *out = UNDEF;
            return RC_OK;
        }
        if (SUCCEEDED(VariantCopyInd(&tmp, (VARIANT *)v))) {
            v = &tmp;
            vt = tmp.vt;
            copied = TRUE;
        }
    }
    int rc = RC_OK;
    switch (vt) {
    case VT_EMPTY:
        *out = UNDEF;
        break;
    case VT_NULL:
        *out = v_null();
        break;
    case VT_I2: *out = v_num(v->iVal); break;
    case VT_I4: *out = v_num(v->lVal); break;
    case VT_INT: *out = v_num(v->intVal); break;
    case VT_UI1: *out = v_num(v->bVal); break;
    case VT_I1: *out = v_num(v->cVal); break;
    case VT_UI2: *out = v_num(v->uiVal); break;
    case VT_UI4: *out = v_num(v->ulVal); break;
    case VT_UINT: *out = v_num(v->uintVal); break;
    case VT_R4: *out = v_num(v->fltVal); break;
    case VT_R8: *out = v_num(v->dblVal); break;
    case VT_BOOL: *out = v_bool(v->boolVal != 0); break;
    case VT_BSTR:
        *out = v_str(str_new(e, v->bstrVal ? v->bstrVal : L"", v->bstrVal ? SysStringLen(v->bstrVal) : 0));
        break;
    case VT_CY:
    case VT_DECIMAL: {
        VARIANT r;
        VariantInit(&r);
        if (SUCCEEDED(VariantChangeType(&r, (VARIANT *)v, 0, VT_R8)))
            *out = v_num(r.dblVal);
        else
            *out = v_num(js_nan);
        break;
    }
    case VT_DISPATCH:
    case VT_UNKNOWN: {
        IDispatch *d = nullptr;
        if (!v->punkVal) {
            *out = v_null();
            break;
        }
        if (vt == VT_DISPATCH)
            d = v->pdispVal, d->lpVtbl->AddRef(d);
        else if (FAILED(v->punkVal->lpVtbl->QueryInterface(v->punkVal, &IID_IDispatch, (void **)&d)))
            d = nullptr;
        if (!d) {
            *out = v_obj(nullptr);
            out->t = T_VAR;
            out->u.v = box_variant(e, v);
            break;
        }
        Obj *own = script_object_of(e, d);
        if (own) {
            d->lpVtbl->Release(d);
            *out = v_obj(own);
        } else {
            *out = v_obj(host_wrap(e, d));
            d->lpVtbl->Release(d);
        }
        break;
    }
    default:
        out->t = T_VAR;
        out->u.v = box_variant(e, v);
        break;
    }
    if (copied)
        VariantClear(&tmp);
    return rc;
}

int val_to_variant(Engine *e, Val *v, VARIANT *out)
{
    VariantInit(out);
    switch (v->t) {
    case T_UNDEF:
    case T_HOLE:
        out->vt = VT_EMPTY;
        break;
    case T_NULL:
        out->vt = VT_NULL;
        break;
    case T_BOOL:
        out->vt = VT_BOOL;
        out->boolVal = v->u.b ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case T_NUM: {
        double d = v->u.n;
        if (!v->r8 && d == (double)(LONG)d && d >= -2147483648.0 && d <= 2147483647.0 && !(d == 0 && __builtin_signbit(d))) {
            out->vt = VT_I4;
            out->lVal = (LONG)d;
        } else {
            out->vt = VT_R8;
            out->dblVal = d;
        }
        break;
    }
    case T_STR:
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocStringLen(v->u.s->s, v->u.s->len);
        break;
    case T_VAR:
        VariantCopy(out, &v->u.v->v);
        break;
    case T_OBJ: {
        Obj *o = v->u.o;
        if (o->cls == C_HOST) {
            out->vt = VT_DISPATCH;
            out->pdispVal = o->u.host.disp;
            o->u.host.disp->lpVtbl->AddRef(o->u.host.disp);
        } else {
            out->vt = VT_DISPATCH;
            out->pdispVal = dispatch_for_obj(e, o);
        }
        break;
    }
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ errors from automation */

static int map_hresult(HRESULT hr)
{
    switch ((ULONG)hr) {
    case 0x80020005: return 13;
    case 0x8002000A: return 6;
    case 0x8002000E: return 450;
    case 0x80020003:
    case 0x80020006:
    case 0x80020001: return 438;
    case 0x80020004: return 448;
    case 0x80020007: return 446;
    case 0x80020008: return 458;
    case 0x8002000B: return 9;
    case 0x8002000C: return 447;
    case 0x8002000D: return 10;
    case 0x8002000F: return 449;
    case 0x80020011: return 451;
    case 0x80004001: return 445;
    case 0x80004002: return 430;
    case 0x80070005: return 70;
    case 0x80070057: return 5;
    case 0x8007000E: return 7;
    case 0x80040154:
    case 0x800401F3:
    case 0x80080005: return 429;
    case 0x800706BA: return 462;
    }
    return 0;
}

static int host_fail(Engine *e, HRESULT hr, EXCEPINFO *ei)
{
    if (hr == DISP_E_EXCEPTION) {
        HRESULT sc = E_FAIL;
        BSTR src = nullptr, desc = nullptr;
        if (ei) {
            if (ei->pfnDeferredFillIn)
                ei->pfnDeferredFillIn(ei);
            sc = ei->scode;
            if (!sc && ei->wCode)
                sc = (HRESULT)(0x800A0000u | ei->wCode);
            src = ei->bstrSource;
            desc = ei->bstrDescription;
        }
        if (sc >= 0) {
            /* a scode that is no failure is taken for E_FAIL, with nothing to say. */
            sc = E_FAIL;
            SysFreeString(desc);
            desc = nullptr;
        }
        int rc = throw_host_error(e, sc, src, desc);
        if (ei) {
            SysFreeString(ei->bstrSource);
            SysFreeString(ei->bstrDescription);
            SysFreeString(ei->bstrHelpFile);
        } else {
            SysFreeString(src);
            SysFreeString(desc);
        }
        return rc;
    }
    int code = map_hresult(hr);
    if (code)
        return throw_number(e, (HRESULT)(0x800A0000u | code));
    if (((ULONG)hr >> 16) == 0x800A)
        return throw_number(e, hr);
    return throw_host_error(e, hr, nullptr, nullptr);
}

/* ------------------------------------------------------------------ invoking */

static HRESULT host_dispid(Engine *e, Obj *o, Str *name, DISPID *id)
{
    Prop *p = obj_find_own(o, name);
    if (p) {
        *id = p->id;
        return *id == DISPID_UNKNOWN ? DISP_E_UNKNOWNNAME : S_OK;
    }
    IDispatch *d = o->u.host.disp;
    if (!d)
        return E_FAIL;
    WCHAR *names[1] = {name->s};
    DISPID did = DISPID_UNKNOWN;
    HRESULT hr = d->lpVtbl->GetIDsOfNames(d, &IID_NULL, names, 1, e->Lcid, &did);
    if (FAILED(hr))
        did = DISPID_UNKNOWN;
    Prop *np = obj_add_prop(e, o, name, UNDEF, PF_DONTENUM);
    np->id = did;
    *id = did;
    return did == DISPID_UNKNOWN ? (hr ? hr : DISP_E_UNKNOWNNAME) : S_OK;
}

static int invoke_host(Engine *e, Obj *o, DISPID id, WORD flags, Val *args, int argc, Val *ret, BOOL put)
{
    IDispatch *d = o->u.host.disp;
    VARIANT stack_vars[8], *vars = stack_vars;
    if (argc > 8)
        vars = js_alloc(argc * sizeof(VARIANT));
    for (int i = 0; i < argc; i++)
        val_to_variant(e, &args[argc - 1 - i], &vars[i]);
    DISPPARAMS dp = {vars, nullptr, (UINT)argc, 0};
    DISPID named = DISPID_PROPERTYPUT;
    if (put) {
        dp.rgdispidNamedArgs = &named;
        dp.cNamedArgs = 1;
    }
    VARIANT res;
    VariantInit(&res);
    EXCEPINFO ei = {};
    UINT arg_err = 0;
    IDispatch *keep = d;
    keep->lpVtbl->AddRef(keep);
    e->NoGc = TRUE;
    HRESULT hr = d->lpVtbl->Invoke(d, id, &IID_NULL, e->Lcid, flags, &dp, ret ? &res : nullptr, &ei, &arg_err);
    e->NoGc = FALSE;
    keep->lpVtbl->Release(keep);
    for (int i = 0; i < argc; i++)
        VariantClear(&vars[i]);
    if (vars != stack_vars)
        js_free(vars);
    if (FAILED(hr)) {
        VariantClear(&res);
        return host_fail(e, hr, &ei);
    }
    if (ret) {
        variant_to_val(e, &res, ret);
        VariantClear(&res);
    }
    return RC_OK;
}

int host_get(Engine *e, Obj *o, Str *key, Val *out, BOOL *found)
{
    *found = FALSE;
    DISPID id;
    if (host_dispid(e, o, key, &id) != S_OK)
        return RC_OK;
    *found = TRUE;
    *out = UNDEF;
    return invoke_host(e, o, id, DISPATCH_PROPERTYGET, nullptr, 0, out, FALSE);
}

int host_get_quiet(Engine *e, Obj *o, Str *key, Val *out, BOOL *found)
{
    *found = FALSE;
    DISPID id;
    if (host_dispid(e, o, key, &id) == S_OK)
        *found = TRUE;
    *out = UNDEF;
    return RC_OK;
}

int host_put(Engine *e, Obj *o, Str *key, Val *v)
{
    DISPID id;
    HRESULT hr = host_dispid(e, o, key, &id);
    if (hr != S_OK) {
        /* an object that does not know the name may take it as a new member. */
        IDispatchEx *dex = nullptr;
        if (o->u.host.disp && SUCCEEDED(o->u.host.disp->lpVtbl->QueryInterface(o->u.host.disp, &IID_IDispatchEx, (void **)&dex))) {
            BSTR name = SysAllocStringLen(key->s, key->len);
            DISPID nid;
            hr = dex->lpVtbl->GetDispID(dex, name, fdexNameEnsure | fdexNameCaseSensitive, &nid);
            SysFreeString(name);
            dex->lpVtbl->Release(dex);
            if (SUCCEEDED(hr)) {
                Prop *p = obj_find_own(o, key);
                if (p)
                    p->id = nid;
                id = nid;
                goto put;
            }
        }
        return throw_error(e, ERR_TYPE, 438, nullptr);
    }
put:;
    WORD flags = DISPATCH_PROPERTYPUT;
    if (v->t == T_OBJ)
        flags = DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF;
    return invoke_host(e, o, id, flags, v, 1, nullptr, TRUE);
}

BOOL host_has(Engine *e, Obj *o, Str *key)
{
    DISPID id;
    return host_dispid(e, o, key, &id) == S_OK;
}

int host_delete(Engine *e, Obj *o, Str *key, BOOL *result)
{
    IDispatchEx *dex = nullptr;
    *result = FALSE;
    if (o->u.host.disp && SUCCEEDED(o->u.host.disp->lpVtbl->QueryInterface(o->u.host.disp, &IID_IDispatchEx, (void **)&dex))) {
        BSTR name = SysAllocStringLen(key->s, key->len);
        HRESULT hr = dex->lpVtbl->DeleteMemberByName(dex, name, fdexNameCaseSensitive);
        SysFreeString(name);
        dex->lpVtbl->Release(dex);
        *result = SUCCEEDED(hr);
        return RC_OK;
    }
    return throw_error(e, ERR_TYPE, 445, nullptr);
}

int host_call_member(Engine *e, Obj *o, Str *key, Val *args, int argc, Val *ret, BOOL *found, int ctx)
{
    DISPID id;
    *found = FALSE;
    if (host_dispid(e, o, key, &id) != S_OK)
        return RC_OK;
    *found = TRUE;
    *ret = UNDEF;
    WORD flags = ctx == 1 ? DISPATCH_METHOD : (DISPATCH_METHOD | DISPATCH_PROPERTYGET);
    return invoke_host(e, o, id, flags, args, argc, ret, FALSE);
}

int host_call(Engine *e, Obj *o, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    *ret = UNDEF;
    if (construct)
        return throw_error(e, ERR_TYPE, 438, nullptr);
    WORD flags = e->CallCtx == 1 ? DISPATCH_METHOD : (DISPATCH_METHOD | DISPATCH_PROPERTYGET);
    return invoke_host(e, o, DISPID_VALUE, flags, args, argc, ret, FALSE);
}

int host_default_value(Engine *e, Obj *o, int hint, Val *out)
{
    IDispatch *d = o->u.host.disp;
    if (!d)
        return 2;
    VARIANT res;
    VariantInit(&res);
    DISPPARAMS dp = {};
    EXCEPINFO ei = {};
    HRESULT hr = d->lpVtbl->Invoke(d, DISPID_VALUE, &IID_NULL, e->Lcid, DISPATCH_PROPERTYGET, &dp, &res, &ei, nullptr);
    if (FAILED(hr)) {
        if (hr == DISP_E_EXCEPTION) {
            SysFreeString(ei.bstrSource);
            SysFreeString(ei.bstrDescription);
            SysFreeString(ei.bstrHelpFile);
        }
        return 2;
    }
    variant_to_val(e, &res, out);
    VariantClear(&res);
    if (out->t == T_OBJ)
        return 2;
    return RC_OK;
}

int host_enum_keys(Engine *e, Obj *o, Obj *array)
{
    IDispatchEx *dex = nullptr;
    IDispatch *d = o->u.host.disp;
    if (!d || FAILED(d->lpVtbl->QueryInterface(d, &IID_IDispatchEx, (void **)&dex)))
        return RC_OK;
    DISPID id = DISPID_STARTENUM;
    while (dex->lpVtbl->GetNextDispID(dex, 0, id, &id) == S_OK) {
        BSTR name = nullptr;
        if (SUCCEEDED(dex->lpVtbl->GetMemberName(dex, id, &name)) && name) {
            array_push(e, array, v_str(str_new(e, name, SysStringLen(name))));
            SysFreeString(name);
        }
    }
    dex->lpVtbl->Release(dex);
    return RC_OK;
}

/* ------------------------------------------------------------------ ActiveXObject and GetObject */

#define MK_E_SYNTAX ((HRESULT)0x800401E4L)
#define DISPID_THIS (-613)

typedef struct IPersistFile IPersistFile;
MIME_DEFINE_GUID(IID_IPersistFile, 0x0000010B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#define IPersistFile_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassID)(T * This, CLSID * id);                                                      \
    HRESULT(STDMETHODCALLTYPE *IsDirty)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, LPCOLESTR name, DWORD mode);                                            \
    HRESULT(STDMETHODCALLTYPE *Save)(T * This, LPCOLESTR name, BOOL remember);                                         \
    HRESULT(STDMETHODCALLTYPE *SaveCompleted)(T * This, LPCOLESTR name);                                               \
    HRESULT(STDMETHODCALLTYPE *GetCurFile)(T * This, LPOLESTR * name);
MIME_COM_INTERFACE(IPersistFile)


static int activex_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *prog;
    Val a = ARG(0);
    if (a.t != T_STR) {
        int rc = to_string(e, &a, &prog);
        if (rc)
            return rc;
    } else
        prog = a.u.s;
    if (argc < 1)
        return throw_error(e, ERR_ERROR, 5, nullptr);
    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(prog->s, &clsid);
    if (FAILED(hr)) {
        if (prog->len && prog->s[0] == '{')
            hr = CLSIDFromString(prog->s, &clsid);
    }
    if (FAILED(hr))
        return throw_error(e, ERR_ERROR, 429, nullptr);
    IUnknown *unk = nullptr;
    e->NoGc = TRUE;
    hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IUnknown, (void **)&unk);
    e->NoGc = FALSE;
    if (FAILED(hr))
        return throw_error(e, ERR_ERROR, 429, nullptr);
    IDispatch *d = nullptr;
    hr = unk->lpVtbl->QueryInterface(unk, &IID_IDispatch, (void **)&d);
    unk->lpVtbl->Release(unk);
    if (FAILED(hr))
        return throw_error(e, ERR_ERROR, 430, nullptr);
    Obj *o = host_wrap(e, d);
    d->lpVtbl->Release(d);
    *ret = v_obj(o);
    return RC_OK;
}

static int global_get_object(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val a0 = ARG(0), a1 = ARG(1);
    Str *path = nullptr, *prog = nullptr;
    int rc;
    if (a0.t != T_UNDEF && (rc = to_string(e, &a0, &path)))
        return rc;
    if (a1.t != T_UNDEF && (rc = to_string(e, &a1, &prog)))
        return rc;
    IUnknown *unk = nullptr;
    HRESULT hr;
    e->NoGc = TRUE;
    if (path && path->len) {
        if (prog) {
            /* a file and a class: the file is loaded into a new object of the class. */
            CLSID clsid;
            hr = CLSIDFromProgID(prog->s, &clsid);
            if (SUCCEEDED(hr)) {
                IPersistFile *pf = nullptr;
                hr = CoCreateInstance(&clsid, nullptr, CLSCTX_ALL, &IID_IPersistFile, (void **)&pf);
                if (SUCCEEDED(hr)) {
                    hr = pf->lpVtbl->Load(pf, path->s, 0);
                    if (SUCCEEDED(hr))
                        hr = pf->lpVtbl->QueryInterface(pf, &IID_IUnknown, (void **)&unk);
                    pf->lpVtbl->Release(pf);
                }
            }
        } else
            hr = CoGetObject(path->s, nullptr, &IID_IUnknown, (void **)&unk);
    } else if (prog) {
        CLSID clsid;
        hr = CLSIDFromProgID(prog->s, &clsid);
        if (SUCCEEDED(hr))
            hr = GetActiveObject(&clsid, nullptr, &unk);
    } else
        hr = E_INVALIDARG;
    e->NoGc = FALSE;
    if (FAILED(hr) || !unk)
        return throw_error(e, ERR_ERROR, hr == MK_E_SYNTAX || prog ? 429 : 432, nullptr);
    IDispatch *d = nullptr;
    hr = unk->lpVtbl->QueryInterface(unk, &IID_IDispatch, (void **)&d);
    unk->lpVtbl->Release(unk);
    if (FAILED(hr))
        return throw_error(e, ERR_ERROR, 430, nullptr);
    Obj *o = host_wrap(e, d);
    d->lpVtbl->Release(d);
    *ret = v_obj(o);
    return RC_OK;
}

/* ------------------------------------------------------------------ Enumerator */

static int enum_advance(Engine *e, Obj *o)
{
    VariantClear(&o->u.en.cur);
    ULONG got = 0;
    HRESULT hr = o->u.en.en->lpVtbl->Next(o->u.en.en, 1, &o->u.en.cur, &got);
    o->u.en.at_end = FAILED(hr) || got == 0;
    if (o->u.en.at_end)
        VariantClear(&o->u.en.cur);
    o->u.en.loaded = TRUE;
    return RC_OK;
}

int enumerator_new(Engine *e, Val *args, int argc, Val *ret)
{
    Val a = ARG(0);
    if (a.t != T_OBJ || a.u.o->cls != C_HOST) {
        if (a.t == T_VAR && a.u.v->v.vt == (VT_ARRAY | VT_VARIANT))
            ;
        return throw_error(e, ERR_TYPE, 5007, nullptr);
    }
    IDispatch *d = a.u.o->u.host.disp;
    VARIANT res;
    VariantInit(&res);
    DISPPARAMS dp = {};
    EXCEPINFO ei = {};
    HRESULT hr = d->lpVtbl->Invoke(d, DISPID_NEWENUM, &IID_NULL, e->Lcid, DISPATCH_METHOD | DISPATCH_PROPERTYGET, &dp, &res, &ei, nullptr);
    if (FAILED(hr))
        return host_fail(e, hr == DISP_E_MEMBERNOTFOUND ? DISP_E_MEMBERNOTFOUND : hr, &ei);
    IEnumVARIANT *en = nullptr;
    if ((res.vt == VT_UNKNOWN || res.vt == VT_DISPATCH) && res.punkVal)
        hr = res.punkVal->lpVtbl->QueryInterface(res.punkVal, &IID_IEnumVARIANT, (void **)&en);
    else
        hr = E_NOINTERFACE;
    VariantClear(&res);
    if (FAILED(hr) || !en)
        return throw_error(e, ERR_TYPE, 5007, nullptr);
    Obj *o = obj_new(e, C_ENUMERATOR, e->EnumeratorProto);
    o->u.en.en = en;
    VariantInit(&o->u.en.cur);
    enum_advance(e, o);
    *ret = v_obj(o);
    return RC_OK;
}

static int enumerator_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    return enumerator_new(e, args, argc, ret);
}

static int enumerator_this(Engine *e, Val *self, Obj **o)
{
    if (self->t != T_OBJ || self->u.o->cls != C_ENUMERATOR)
        return throw_error(e, ERR_TYPE, 5015, nullptr);
    *o = self->u.o;
    return RC_OK;
}

static int enumerator_method(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = enumerator_this(e, self, &o);
    if (rc)
        return rc;
    switch (e->Magic) {
    case 0:
        *ret = v_bool(o->u.en.at_end);
        break;
    case 1:
        if (o->u.en.at_end)
            *ret = UNDEF;
        else
            variant_to_val(e, &o->u.en.cur, ret);
        break;
    case 2:
        if (!o->u.en.at_end)
            enum_advance(e, o);
        *ret = UNDEF;
        break;
    default:
        o->u.en.en->lpVtbl->Reset(o->u.en.en);
        enum_advance(e, o);
        *ret = UNDEF;
        break;
    }
    return RC_OK;
}

/* ------------------------------------------------------------------ VBArray */

static int vbarray_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Val a = ARG(0);
    if (a.t != T_VAR || !(a.u.v->v.vt & VT_ARRAY))
        return throw_error(e, ERR_TYPE, 5013, nullptr);
    SAFEARRAY *copy = nullptr;
    SAFEARRAY *src = (a.u.v->v.vt & VT_BYREF) ? *a.u.v->v.pparray : a.u.v->v.parray;
    if (FAILED(SafeArrayCopy(src, &copy)))
        return throw_error(e, ERR_ERROR, 7, nullptr);
    Obj *o = obj_new(e, C_VBARRAY, e->VBArrayProto);
    o->u.vba.sa = copy;
    *ret = v_obj(o);
    return RC_OK;
}

static int vba_this(Engine *e, Val *self, Obj **o)
{
    if (self->t != T_OBJ || self->u.o->cls != C_VBARRAY)
        return throw_error(e, ERR_TYPE, 5013, nullptr);
    *o = self->u.o;
    return RC_OK;
}

static int vba_element(Engine *e, SAFEARRAY *sa, LONG *idx, Val *out)
{
    VARTYPE vt;
    SafeArrayGetVartype(sa, &vt);
    VARIANT v;
    VariantInit(&v);
    HRESULT hr;
    if (vt == VT_VARIANT)
        hr = SafeArrayGetElement(sa, idx, &v);
    else {
        v.vt = vt;
        hr = SafeArrayGetElement(sa, idx, &v.byref);
        if (SUCCEEDED(hr) && vt == VT_BSTR) {
            /* the element came as a pointer: take it as a string. */
        }
    }
    if (FAILED(hr))
        return throw_error(e, ERR_RANGE, 9, nullptr);
    variant_to_val(e, &v, out);
    if (vt == VT_VARIANT || vt == VT_BSTR || vt == VT_DISPATCH || vt == VT_UNKNOWN)
        VariantClear(&v);
    return RC_OK;
}

static int vbarray_method(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Obj *o;
    int rc = vba_this(e, self, &o);
    if (rc)
        return rc;
    SAFEARRAY *sa = o->u.vba.sa;
    UINT dims = SafeArrayGetDim(sa);
    switch (e->Magic) {
    case 0:
        *ret = v_num(dims);
        return RC_OK;
    case 1:
    case 2: {
        double d = 1;
        if (argc && (rc = to_number(e, &args[0], &d)))
            return rc;
        d = to_integer_d(d);
        if (d < 1 || d > dims)
            return throw_error(e, ERR_RANGE, 9, nullptr);
        LONG v;
        HRESULT hr = e->Magic == 1 ? SafeArrayGetLBound(sa, (UINT)d, &v) : SafeArrayGetUBound(sa, (UINT)d, &v);
        if (FAILED(hr))
            return throw_error(e, ERR_RANGE, 9, nullptr);
        *ret = v_num(v);
        return RC_OK;
    }
    case 3: {
        if ((UINT)argc < dims)
            return throw_error(e, ERR_RANGE, 9, nullptr);
        LONG idx[16];
        for (UINT i = 0; i < dims && i < 16; i++) {
            double d;
            if ((rc = to_number(e, &args[i], &d)))
                return rc;
            idx[i] = (LONG)d;
        }
        return vba_element(e, sa, idx, ret);
    }
    default: {
        Obj *a = array_new(e, 0);
        *ret = v_obj(a);
        LONG lb[16], ub[16], idx[16];
        if (dims > 16)
            return throw_error(e, ERR_RANGE, 9, nullptr);
        for (UINT i = 0; i < dims; i++) {
            SafeArrayGetLBound(sa, i + 1, &lb[i]);
            SafeArrayGetUBound(sa, i + 1, &ub[i]);
            idx[i] = lb[i];
            if (ub[i] < lb[i])
                return RC_OK;
        }
        for (;;) {
            Val v;
            if ((rc = vba_element(e, sa, idx, &v)))
                return rc;
            array_push(e, a, v);
            /* the first dimension varies fastest, as the elements lie in memory. */
            UINT k = 0;
            for (; k < dims; k++) {
                if (++idx[k] <= ub[k])
                    break;
                idx[k] = lb[k];
            }
            if (k == dims)
                break;
        }
        return RC_OK;
    }
    }
}

void init_host(Engine *e)
{
    Obj *g = e->Global;
    Obj *ax = obj_native(e, "ActiveXObject", activex_ctor, 1, 0);
    obj_define_z(e, g, "ActiveXObject", v_obj(ax), PF_DONTENUM);
    obj_method(e, g, "GetObject", global_get_object, 2, 0);

    Obj *eproto = obj_new(e, C_OBJECT, e->ObjectProto);
    e->EnumeratorProto = eproto;
    Obj *ector = obj_native(e, "Enumerator", enumerator_ctor, 1, 0);
    obj_define_z(e, ector, "prototype", v_obj(eproto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, eproto, "constructor", v_obj(ector), PF_DONTENUM);
    obj_method(e, eproto, "atEnd", enumerator_method, 0, 0);
    obj_method(e, eproto, "item", enumerator_method, 0, 1);
    obj_method(e, eproto, "moveNext", enumerator_method, 0, 2);
    obj_method(e, eproto, "moveFirst", enumerator_method, 0, 3);
    obj_define_z(e, g, "Enumerator", v_obj(ector), PF_DONTENUM);

    Obj *vproto = obj_new(e, C_OBJECT, e->ObjectProto);
    e->VBArrayProto = vproto;
    Obj *vctor = obj_native(e, "VBArray", vbarray_ctor, 1, 0);
    obj_define_z(e, vctor, "prototype", v_obj(vproto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, vproto, "constructor", v_obj(vctor), PF_DONTENUM);
    obj_method(e, vproto, "dimensions", vbarray_method, 0, 0);
    obj_method(e, vproto, "lbound", vbarray_method, 1, 1);
    obj_method(e, vproto, "ubound", vbarray_method, 1, 2);
    obj_method(e, vproto, "getItem", vbarray_method, 1, 3);
    obj_method(e, vproto, "toArray", vbarray_method, 0, 4);
    obj_define_z(e, g, "VBArray", v_obj(vctor), PF_DONTENUM);
}

/* ------------------------------------------------------------------ the objects of the script, as IDispatchEx */

typedef struct JsDisp {
    IDispatchEx Ex;
    LONG Refs;
    Engine *E;
    Obj *O;
} JsDisp;

extern LONG g_js_objects;

static HRESULT STDMETHODCALLTYPE jd_qi(IDispatchEx *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IDispatchEx)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE jd_addref(IDispatchEx *This)
{
    return InterlockedIncrement(&((JsDisp *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE jd_release(IDispatchEx *This)
{
    JsDisp *d = (JsDisp *)This;
    LONG refs = InterlockedDecrement(&d->Refs);
    if (!refs) {
        d->O->ext--;
        InterlockedDecrement(&g_js_objects);
        js_free(d);
    }
    return refs;
}

IDispatch *dispatch_for_obj(Engine *e, Obj *o)
{
    extern const IDispatchExVtbl g_jd_vtbl;
    JsDisp *d = js_alloc(sizeof(JsDisp));
    d->Ex.lpVtbl = &g_jd_vtbl;
    d->Refs = 1;
    d->E = e;
    d->O = o;
    o->ext++;
    InterlockedIncrement(&g_js_objects);
    return (IDispatch *)&d->Ex;
}

Obj *script_object_of(Engine *e, IDispatch *d)
{
    extern const IDispatchExVtbl g_jd_vtbl;
    if (d && ((IDispatchEx *)d)->lpVtbl == &g_jd_vtbl && ((JsDisp *)d)->E == e)
        return ((JsDisp *)d)->O;
    return nullptr;
}

IDispatch *script_dispatch_create(Engine *e)
{
    return dispatch_for_obj(e, e->Global);
}

static HRESULT STDMETHODCALLTYPE jd_count(IDispatchEx *This, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE jd_typeinfo(IDispatchEx *This, UINT i, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE jd_getdispid(IDispatchEx *This, BSTR name, DWORD flags, DISPID *id)
{
    JsDisp *d = (JsDisp *)This;
    Engine *e = d->E;
    if (!name || !id)
        return E_POINTER;
    Str *key = str_intern(e, name, SysStringLen(name));
    Obj *o = d->O;
    BOOL exists = obj_has(e, o, key);
    if (!exists && (flags & fdexNameCaseInsensitive)) {
        for (ULONG i = 0; i < o->pcount; i++)
            if (o->props[i].key && o->props[i].key->len == key->len && !_wcsicmp(o->props[i].key->s, key->s)) {
                key = o->props[i].key;
                exists = TRUE;
                break;
            }
    }
    if (!exists && o == e->Global) {
        Val v;
        BOOL f = FALSE;
        for (NamedItem *it = e->Items; it && !f; it = it->next)
            if (it->name->len == key->len && !_wcsicmp(it->name->s, key->s) && (it->flags & SCRIPTITEM_ISVISIBLE))
                f = TRUE;
        (void)v;
        exists = f;
    }
    if (!exists) {
        if (!(flags & fdexNameEnsure)) {
            *id = DISPID_UNKNOWN;
            return DISP_E_UNKNOWNNAME;
        }
        Val u = UNDEF;
        obj_put(e, o, key, &u);
    }
    *id = disp_id_of(e, key);
    return S_OK;
}

static int jd_run(JsDisp *d, DISPID id, WORD flags, DISPPARAMS *dp, Val *result)
{
    Engine *e = d->E;
    Obj *o = d->O;
    int rc;
    UINT argc = dp ? dp->cArgs : 0;
    UINT named = dp ? dp->cNamedArgs : 0;
    Val *args = gc_alloc(e, (argc ? argc : 1) * sizeof(Val), GC_VALS);
    Val thisv = UNDEF;
    UINT n = 0;
    BOOL has_put_arg = FALSE;
    for (UINT i = 0; i < argc; i++) {
        VARIANT *v = &dp->rgvarg[argc - 1 - i];
        BOOL is_named = FALSE;
        for (UINT k = 0; k < named; k++)
            if (dp->rgdispidNamedArgs[k] == DISPID_THIS && &dp->rgvarg[k] == v) {
                variant_to_val(e, v, &thisv);
                is_named = TRUE;
            } else if (dp->rgdispidNamedArgs[k] == DISPID_PROPERTYPUT && &dp->rgvarg[k] == v)
                has_put_arg = TRUE;
        if (is_named)
            continue;
        variant_to_val(e, v, &args[n++]);
    }
    (void)has_put_arg;
    *result = UNDEF;
    if (id == DISPID_VALUE && (flags & (DISPATCH_METHOD | DISPATCH_CONSTRUCT)) && (o->cls == C_FUNCTION || o->cls == C_HOST)) {
        Val fv = v_obj(o);
        if (flags & DISPATCH_CONSTRUCT)
            return construct_function(e, &fv, args, (int)n, result);
        return call_function(e, &fv, &thisv, args, (int)n, result);
    }
    if (id == DISPID_NEWENUM)
        return throw_error(e, ERR_TYPE, 438, nullptr);
    Str *key = id == DISPID_VALUE ? nullptr : disp_name_of(e, id);
    if (id != DISPID_VALUE && !key)
        return throw_error(e, ERR_TYPE, 438, nullptr);
    if (!key) {
        /* the default value of an object. */
        return obj_default_value(e, o, 0, result);
    }
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        if (n < 1)
            return throw_error(e, ERR_TYPE, 450, nullptr);
        return obj_put(e, o, key, &args[n - 1]);
    }
    Val member;
    if ((rc = obj_get(e, o, key, &member)))
        return rc;
    if (o == e->Global && member.t == T_UNDEF && !obj_has(e, o, key)) {
        BOOL f;
        if ((rc = lookup_item_by_name(e, key, &member, &f)))
            return rc;
    }
    if (flags & DISPATCH_CONSTRUCT)
        return construct_function(e, &member, args, (int)n, result);
    if (is_callable(&member) && (flags & DISPATCH_METHOD)) {
        Val self = thisv.t != T_UNDEF ? thisv : v_obj(o);
        return call_function(e, &member, &self, args, (int)n, result);
    }
    *result = member;
    if (n && (flags & DISPATCH_METHOD) && !(member.t == T_OBJ))
        return throw_error(e, ERR_TYPE, 5002, nullptr);
    return RC_OK;
}

static HRESULT STDMETHODCALLTYPE jd_invokeex(IDispatchEx *This, DISPID id, LCID lcid, WORD flags, DISPPARAMS *dp, VARIANT *res,
                                             EXCEPINFO *ei, IUnknown *caller)
{
    JsDisp *d = (JsDisp *)This;
    Engine *e = d->E;
    if (e->State == SCRIPTSTATE_CLOSED)
        return E_UNEXPECTED;
    This->lpVtbl->AddRef(This);
    engine_enter(e);
    Val result = UNDEF;
    Node *saved = e->CurStmt;
    int rc = jd_run(d, id, flags, dp, &result);
    e->CurStmt = saved;
    HRESULT hr = S_OK;
    if (rc == RC_THROW) {
        hr = DISP_E_EXCEPTION;
        if (ei)
            fill_excepinfo_from_exception(e, ei);
        fault_from_exception(e);
        if (e->NestedRun == 1 && e->Site) {
            engine_report(e);
            hr = SCRIPT_E_REPORTED;
            if (ei)
                memset(ei, 0, sizeof(*ei));
        } else
            fault_clear(e);
    } else if (res) {
        val_to_variant(e, &result, res);
    }
    engine_leave(e);
    This->lpVtbl->Release(This);
    return hr;
}

static HRESULT STDMETHODCALLTYPE jd_names(IDispatchEx *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    if (!count)
        return S_OK;
    HRESULT hr = This->lpVtbl->GetDispID(This, names[0], fdexNameCaseSensitive, &ids[0]);
    if (hr == S_OK && count > 1)
        return DISP_E_UNKNOWNNAME;
    return hr;
}

static HRESULT STDMETHODCALLTYPE jd_invoke(IDispatchEx *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *dp,
                                           VARIANT *res, EXCEPINFO *ei, UINT *arg_err)
{
    return This->lpVtbl->InvokeEx(This, id, lcid, flags, dp, res, ei, nullptr);
}

static HRESULT STDMETHODCALLTYPE jd_delete_name(IDispatchEx *This, BSTR name, DWORD flags)
{
    JsDisp *d = (JsDisp *)This;
    Str *key = str_intern(d->E, name, SysStringLen(name));
    BOOL r;
    obj_delete(d->E, d->O, key, &r);
    return r ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE jd_delete_id(IDispatchEx *This, DISPID id)
{
    JsDisp *d = (JsDisp *)This;
    Str *key = disp_name_of(d->E, id);
    if (!key)
        return S_OK;
    BOOL r;
    obj_delete(d->E, d->O, key, &r);
    return r ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE jd_props(IDispatchEx *This, DISPID id, DWORD fetch, DWORD *flags)
{
    *flags = fdexPropCanGet | fdexPropCanPut | fdexPropCannotPutRef | fdexPropCannotCall | fdexPropCannotConstruct |
             fdexPropCannotSourceEvents;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE jd_member_name(IDispatchEx *This, DISPID id, BSTR *name)
{
    JsDisp *d = (JsDisp *)This;
    Str *k = disp_name_of(d->E, id);
    if (!k)
        return DISP_E_UNKNOWNNAME;
    *name = SysAllocStringLen(k->s, k->len);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE jd_next(IDispatchEx *This, DWORD flags, DISPID id, DISPID *next)
{
    JsDisp *d = (JsDisp *)This;
    Engine *e = d->E;
    Obj *keys;
    if (obj_enum_keys(e, d->O, &keys, FALSE))
        return E_FAIL;
    ULONG start = 0;
    if (id != DISPID_STARTENUM) {
        Str *cur = disp_name_of(e, id);
        BOOL found = FALSE;
        for (ULONG i = 0; i < keys->u.arr.len; i++)
            if (keys->u.arr.items[i].u.s == cur || (cur && str_equal(keys->u.arr.items[i].u.s, cur))) {
                start = i + 1;
                found = TRUE;
                break;
            }
        if (!found)
            start = keys->u.arr.len;
    }
    if (start >= keys->u.arr.len) {
        *next = DISPID_UNKNOWN;
        return S_FALSE;
    }
    *next = disp_id_of(e, keys->u.arr.items[start].u.s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE jd_parent(IDispatchEx *This, IUnknown **parent)
{
    *parent = nullptr;
    return E_NOTIMPL;
}

const IDispatchExVtbl g_jd_vtbl = {jd_qi, jd_addref, jd_release, jd_count, jd_typeinfo, jd_names, jd_invoke, jd_getdispid,
                                   jd_invokeex, jd_delete_name, jd_delete_id, jd_props, jd_member_name, jd_next, jd_parent};
