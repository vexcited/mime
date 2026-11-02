/* msxsl:script blocks and objects given to the processor: calls through the scripting engines. */
#include <xmldom/xslt_priv.h>
#include <mime/win32/activscp.h>

typedef struct Site {
    IActiveScriptSite iface;
    LONG refs;
    BSTR error_text;
    ULONG error_line;
    LONG error_col;
    HRESULT error_code;
} Site;

typedef struct ScriptSet {
    WCHAR *uri;
    IActiveScript *engine;
    IActiveScriptParse *parse;
    IDispatch *dispatch;
    Site *site;
    BOOL parsed_all;
} ScriptSet;

struct XsScriptHost {
    ScriptSet *sets;
    UINT n;
};

static Site *site_of(IActiveScriptSite *s)
{
    return (Site *)s;
}

static HRESULT STDMETHODCALLTYPE site_qi(IActiveScriptSite *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IActiveScriptSite)) {
        *out = This;
        InterlockedIncrement(&site_of(This)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE site_addref(IActiveScriptSite *This)
{
    return InterlockedIncrement(&site_of(This)->refs);
}

static ULONG STDMETHODCALLTYPE site_release(IActiveScriptSite *This)
{
    return InterlockedDecrement(&site_of(This)->refs);
}

static HRESULT STDMETHODCALLTYPE site_lcid(IActiveScriptSite *This, LCID *lcid)
{
    *lcid = LOCALE_USER_DEFAULT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_item(IActiveScriptSite *This, LPCOLESTR name, DWORD mask, IUnknown **item,
                                           ITypeInfo **info)
{
    if (item)
        *item = nullptr;
    if (info)
        *info = nullptr;
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE site_version(IActiveScriptSite *This, BSTR *version)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE site_terminate(IActiveScriptSite *This, const VARIANT *result, const EXCEPINFO *info)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_state(IActiveScriptSite *This, SCRIPTSTATE state)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_error(IActiveScriptSite *This, IActiveScriptError *error)
{
    Site *s = site_of(This);
    EXCEPINFO info = {};
    DWORD64 cookie = 0;
    ULONG line = 0;
    LONG col = 0;
    if (SUCCEEDED(error->lpVtbl->GetExceptionInfo(error, &info))) {
        if (info.pfnDeferredFillIn)
            info.pfnDeferredFillIn(&info);
        if (!s->error_text) {
            s->error_text = info.bstrDescription ? SysAllocString(info.bstrDescription) : SysAllocString(L"");
            s->error_code = info.scode ? info.scode : (HRESULT)info.wCode;
        }
    }
    error->lpVtbl->GetSourcePosition(error, (DWORD *)&cookie, &line, &col);
    s->error_line = line;
    s->error_col = col;
    SysFreeString(info.bstrSource);
    SysFreeString(info.bstrDescription);
    SysFreeString(info.bstrHelpFile);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_enter(IActiveScriptSite *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_leave(IActiveScriptSite *This)
{
    return S_OK;
}

static const IActiveScriptSiteVtbl g_site_vtbl = {site_qi,    site_addref, site_release,   site_lcid,
                                                  site_item,  site_version, site_terminate, site_state,
                                                  site_error, site_enter,  site_leave};

/* ------------------------------------------------------------------ conversions */

HRESULT xsr_val_to_variant(XsRun *r, XpVal *v, VARIANT *out)
{
    VariantInit(out);
    switch (v->t) {
    case XPV_STR:
        out->vt = VT_BSTR;
        out->bstrVal = xd_bstr(v->s.s ? v->s.s : L"", v->s.length);
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    case XPV_NUM:
        out->vt = VT_R8;
        out->dblVal = v->n;
        return S_OK;
    case XPV_BOOL:
        out->vt = VT_BOOL;
        out->boolVal = v->b ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    default: {
        if (!v->set.n) {
            /* an empty set is an empty selection with no base node: hand the document of the source. */
        }
        XNode *base = v->set.n ? v->set.v[0] : r->input_root;
        XList *l = xd_list_new(base, XL_NODES, nullptr, TRUE);
        if (!l)
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < v->set.n; i++)
            xd_list_add_node(l, v->set.v[i]);
        out->vt = VT_DISPATCH;
        out->pdispVal = (IDispatch *)l;
        return S_OK;
    }
    }
}

HRESULT xsr_variant_to_val(XsRun *r, const VARIANT *in, XpVal *out)
{
    const VARIANT *v = xv_deref(in);
    memset(out, 0, sizeof(*out));
    switch (v->vt & VT_TYPEMASK) {
    case VT_EMPTY:
    case VT_NULL:
        out->t = XPV_STR;
        return S_OK;
    case VT_BSTR:
        out->t = XPV_STR;
        if (v->bstrVal)
            xb_put(&out->s, v->bstrVal, SysStringLen(v->bstrVal));
        return S_OK;
    case VT_BOOL:
        out->t = XPV_BOOL;
        out->b = v->boolVal != VARIANT_FALSE;
        return S_OK;
    case VT_I1:
    case VT_I2:
    case VT_I4:
    case VT_INT:
    case VT_UI1:
    case VT_UI2:
    case VT_UI4:
    case VT_UINT:
    case VT_I8:
    case VT_UI8:
    case VT_R4:
    case VT_R8:
    case VT_CY:
    case VT_DECIMAL: {
        VARIANT d;
        VariantInit(&d);
        HRESULT hr = VariantChangeType(&d, (VARIANT *)v, 0, VT_R8);
        if (FAILED(hr))
            return hr;
        out->t = XPV_NUM;
        out->n = d.dblVal;
        return S_OK;
    }
    case VT_DATE: {
        VARIANT d;
        VariantInit(&d);
        HRESULT hr = VariantChangeType(&d, (VARIANT *)v, 0, VT_BSTR);
        if (FAILED(hr))
            return hr;
        out->t = XPV_STR;
        xb_put(&out->s, d.bstrVal, SysStringLen(d.bstrVal));
        VariantClear(&d);
        return S_OK;
    }
    case VT_DISPATCH:
    case VT_UNKNOWN: {
        IUnknown *u = v->punkVal;
        if (!u)
            return E_INVALIDARG;
        out->t = XPV_SET;
        if (xd_node_is_ours(u)) {
            xs_add(&out->set, (XNode *)u);
            return S_OK;
        }
        if (xd_list_is_ours(u)) {
            XList *l = (XList *)u;
            xd_list_refresh(l);
            for (UINT i = 0; i < l->n; i++)
                xs_add(&out->set, l->v[i]);
            return S_OK;
        }
        return E_INVALIDARG;
    }
    }
    return E_INVALIDARG;
}

/* ------------------------------------------------------------------ the engines */

static void host_free(XsScriptHost *h)
{
    for (UINT i = 0; i < h->n; i++) {
        ScriptSet *s = &h->sets[i];
        if (s->dispatch)
            s->dispatch->lpVtbl->Release(s->dispatch);
        if (s->parse)
            s->parse->lpVtbl->Release(s->parse);
        if (s->engine) {
            s->engine->lpVtbl->Close(s->engine);
            s->engine->lpVtbl->Release(s->engine);
        }
        if (s->site) {
            SysFreeString(s->site->error_text);
            xd_free(s->site);
        }
        xd_free(s->uri);
    }
    xd_free(h->sets);
    xd_free(h);
}

void xsr_script_free(XsRun *r)
{
    if (r->scripts) {
        host_free(r->scripts);
        r->scripts = nullptr;
    }
}

static void put_number(XBuf *b, UINT v)
{
    WCHAR d[12];
    UINT n = 0;
    do {
        d[n++] = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        xb_putc(b, d[--n]);
}

static HRESULT script_error(XsRun *r, ScriptSet *s, const WCHAR *what)
{
    (void)what;
    XBuf b = {};
    xb_put(&b, s->site->error_text ? s->site->error_text : L"", s->site->error_text ? SysStringLen(s->site->error_text) : 0);
    xb_puta(&b, "\r\n\r\nline = ");
    put_number(&b, s->site->error_line + 1);
    xb_puta(&b, ", col = ");
    put_number(&b, (UINT)(s->site->error_col + 1));
    xb_puta(&b, " (line is offset from the start of the script block).\r\n");
    xb_putc(&b, 0);
    HRESULT hr = xsr_error(r, 0xC00CE30Au, b.s, nullptr, nullptr);
    xb_free(&b);
    return hr;
}

HRESULT xsr_start_scripts(XsRun *r)
{
    XsSheet *sheet = r->sheet;
    if (!sheet->nscripts)
        return S_OK;
    XsScriptHost *h = xd_alloc(sizeof(XsScriptHost));
    if (!h)
        return E_OUTOFMEMORY;
    r->scripts = h;
    for (UINT i = 0; i < sheet->nscripts; i++) {
        BOOL seen = FALSE;
        for (UINT k = 0; k < i; k++)
            if (!wcscmp(sheet->scripts[k].uri, sheet->scripts[i].uri))
                seen = TRUE;
        if (seen)
            continue;
        ScriptSet *list = xd_realloc(h->sets, (h->n + 1) * sizeof(ScriptSet));
        if (!list)
            return E_OUTOFMEMORY;
        h->sets = list;
        ScriptSet *s = &h->sets[h->n++];
        memset(s, 0, sizeof(*s));
        s->uri = xd_wcsdup(sheet->scripts[i].uri, xd_wlen(sheet->scripts[i].uri));
        CLSID clsid;
        if (FAILED(CLSIDFromProgID(sheet->scripts[i].language, &clsid)))
            return xsr_error(r, XE_XS_NOT_LANGUAGE, sheet->scripts[i].language, nullptr, nullptr);
        HRESULT hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IActiveScript, (void **)&s->engine);
        if (FAILED(hr))
            return xsr_error(r, XE_XS_NOT_LANGUAGE, sheet->scripts[i].language, nullptr, nullptr);
        hr = s->engine->lpVtbl->QueryInterface(s->engine, sizeof(void *) == 8 ? &IID_IActiveScriptParse64 : &IID_IActiveScriptParse,
                                               (void **)&s->parse);
        if (FAILED(hr))
            return xsr_error(r, XE_XS_NOT_LANGUAGE, sheet->scripts[i].language, nullptr, nullptr);
        s->site = xd_alloc(sizeof(Site));
        if (!s->site)
            return E_OUTOFMEMORY;
        s->site->iface.lpVtbl = &g_site_vtbl;
        s->site->refs = 1;
        s->engine->lpVtbl->SetScriptSite(s->engine, &s->site->iface);
        s->parse->lpVtbl->InitNew(s->parse);
        XBuf text = {};
        for (UINT k = i; k < sheet->nscripts; k++)
            if (!wcscmp(sheet->scripts[k].uri, s->uri)) {
                xb_put(&text, sheet->scripts[k].text, xd_wlen(sheet->scripts[k].text));
                xb_puta(&text, "\n");
            }
        xb_putc(&text, 0);
        EXCEPINFO ex = {};
        hr = s->parse->lpVtbl->ParseScriptText(s->parse, text.s, nullptr, nullptr, nullptr, 0, 0, 0, nullptr, &ex);
        xb_free(&text);
        SysFreeString(ex.bstrSource);
        SysFreeString(ex.bstrDescription);
        SysFreeString(ex.bstrHelpFile);
        if (FAILED(hr))
            return script_error(r, s, nullptr);
        hr = s->engine->lpVtbl->SetScriptState(s->engine, SCRIPTSTATE_CONNECTED);
        if (FAILED(hr))
            return script_error(r, s, nullptr);
        hr = s->engine->lpVtbl->GetScriptDispatch(s->engine, nullptr, &s->dispatch);
        if (FAILED(hr))
            s->dispatch = nullptr;
    }
    return S_OK;
}

static IDispatch *find_dispatch(XsRun *r, const WCHAR *uri, ScriptSet **set)
{
    *set = nullptr;
    if (r->scripts)
        for (UINT i = 0; i < r->scripts->n; i++)
            if (!wcscmp(r->scripts->sets[i].uri, uri) && r->scripts->sets[i].dispatch) {
                *set = &r->scripts->sets[i];
                return r->scripts->sets[i].dispatch;
            }
    for (UINT i = 0; i < r->req->nobjects; i++)
        if (!wcscmp(r->req->objects[i].uri, uri))
            return r->req->objects[i].object;
    return nullptr;
}

BOOL xsr_has_script_function(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT length)
{
    ScriptSet *set;
    IDispatch *d = find_dispatch(r, uri, &set);
    if (!d)
        return FALSE;
    WCHAR *name = xd_wcsdup(local, length);
    DISPID id;
    HRESULT hr = d->lpVtbl->GetIDsOfNames(d, &IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);
    xd_free(name);
    return SUCCEEDED(hr);
}

HRESULT xsr_call_extension(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *args, UINT count,
                           XpCall *call, XpVal *out)
{
    ScriptSet *set;
    IDispatch *d = find_dispatch(r, uri, &set);
    WCHAR *name = xd_wcsdup(local, length);
    if (!d) {
        HRESULT hr = xsr_error(r, XE_XS_NS_NO_FUNCTIONS, uri, nullptr, nullptr);
        xd_free(name);
        return hr;
    }
    DISPID id;
    HRESULT hr = d->lpVtbl->GetIDsOfNames(d, &IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) {
        hr = xsr_error(r, XE_XS_NS_NO_FUNCTION, uri, name, nullptr);
        xd_free(name);
        return hr;
    }
    VARIANT stack[8];
    VARIANT *argv = count <= 8 ? stack : xd_alloc(sizeof(VARIANT) * count);
    if (!argv) {
        xd_free(name);
        return E_OUTOFMEMORY;
    }
    UINT made = 0;
    hr = S_OK;
    /* arguments are passed last first. */
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        hr = xsr_val_to_variant(r, &args[count - 1 - i], &argv[i]);
        if (SUCCEEDED(hr))
            made++;
    }
    VARIANT result;
    VariantInit(&result);
    EXCEPINFO ex = {};
    UINT argerr = 0;
    if (SUCCEEDED(hr)) {
        DISPPARAMS params = {argv, nullptr, count, 0};
        hr = d->lpVtbl->Invoke(d, id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &result, &ex, &argerr);
        if (hr == DISP_E_MEMBERNOTFOUND || hr == DISP_E_TYPEMISMATCH)
            hr = d->lpVtbl->Invoke(d, id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD | DISPATCH_PROPERTYGET, &params,
                                   &result, &ex, &argerr);
    }
    for (UINT i = 0; i < made; i++)
        VariantClear(&argv[i]);
    if (argv != stack)
        xd_free(argv);
    if (FAILED(hr)) {
        WCHAR *desc = nullptr;
        if (hr == DISP_E_EXCEPTION) {
            if (ex.pfnDeferredFillIn)
                ex.pfnDeferredFillIn(&ex);
            if (ex.bstrDescription)
                desc = xd_wcsdup(ex.bstrDescription, SysStringLen(ex.bstrDescription));
        }
        if (!desc && set && set->site && set->site->error_text)
            desc = xd_wcsdup(set->site->error_text, SysStringLen(set->site->error_text));
        if (!desc)
            desc = xd_wcsdup(L"Unspecified error", 17);
        SysFreeString(ex.bstrSource);
        SysFreeString(ex.bstrDescription);
        SysFreeString(ex.bstrHelpFile);
        xsr_error(r, XE_XS_CALL_ERROR, desc, name, nullptr);
        xd_free(desc);
        xd_free(name);
        return E_FAIL;
    }
    SysFreeString(ex.bstrSource);
    SysFreeString(ex.bstrDescription);
    SysFreeString(ex.bstrHelpFile);
    memset(out, 0, sizeof(*out));
    BOOL usable = result.vt != VT_EMPTY && result.vt != VT_NULL;
    if (usable)
        usable = SUCCEEDED(xsr_variant_to_val(r, &result, out));
    VariantClear(&result);
    if (!usable) {
        HRESULT e = xsr_error(r, XE_XS_NO_RETURN, name, nullptr, nullptr);
        xd_free(name);
        return e;
    }
    xd_free(name);
    return S_OK;
}
