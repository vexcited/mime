/* transformNode, transformNodeToObject, and the template and processor objects. */
#include <xmldom/xslt.h>

static CRITICAL_SECTION g_err_lock;
static BOOL g_err_ready;
static DWORD g_err_thread;
static HRESULT g_err_hr;
static BOOL g_err_v6;
static WCHAR *g_err_text;

/* the text of the last failure of a transformation, kept for the exception info of the late bound call. */
void xd_set_error_text(HRESULT hr, const WCHAR *text, BOOL v6)
{
    if (!g_err_ready) {
        InitializeCriticalSection(&g_err_lock);
        g_err_ready = TRUE;
    }
    EnterCriticalSection(&g_err_lock);
    xd_free(g_err_text);
    g_err_text = text ? xd_wcsdup(text, xd_wlen(text)) : nullptr;
    g_err_hr = hr;
    g_err_v6 = v6;
    g_err_thread = GetCurrentThreadId();
    LeaveCriticalSection(&g_err_lock);
}

WCHAR *xd_take_error_text(HRESULT hr, BOOL *v6)
{
    if (!g_err_ready)
        return nullptr;
    WCHAR *t = nullptr;
    EnterCriticalSection(&g_err_lock);
    if (g_err_text && g_err_thread == GetCurrentThreadId() && g_err_hr == hr) {
        t = g_err_text;
        *v6 = g_err_v6;
        g_err_text = nullptr;
    }
    LeaveCriticalSection(&g_err_lock);
    return t;
}

/* an error of the processor as a failure of the call, with its text for whoever asks. */
static HRESULT fail_with(XsError *e, BOOL v6)
{
    WCHAR *text = xse_text(e, v6);
    HRESULT hr = e->hr ? e->hr : E_FAIL;
    if (text) {
        xd_set_error_text(hr, text, v6);
        ICreateErrorInfo *create;
        if (SUCCEEDED(CreateErrorInfo(&create))) {
            create->lpVtbl->SetDescription(create, text);
            create->lpVtbl->SetSource(create, v6 ? L"msxml6.dll" : L"msxml3.dll");
            IErrorInfo *info;
            if (SUCCEEDED(create->lpVtbl->QueryInterface(create, &IID_IErrorInfo, (void **)&info))) {
                SetErrorInfo(0, info);
                info->lpVtbl->Release(info);
            }
            create->lpVtbl->Release(create);
        }
    }
    xd_free(text);
    return hr;
}

/* the parameters, objects and flags of a request come from the stylesheet document. */
static void request_flags(XNode *style, XsRequest *req)
{
    XDoc *sd = style->doc;
    req->v6 = (sd->flags & XDF_V6) != 0;
    req->allow_doc_fn = (sd->flags & XDF_ALLOW_DOCFN) != 0;
    req->allow_script = (sd->flags & XDF_ALLOW_SCRIPT) != 0;
}

XsSheet *xs_compile_for(XNode *style, XsError *err)
{
    UINT flags = 0;
    if (style->doc->flags & XDF_V6)
        flags |= XSF_V6;
    if (style->doc->flags & XDF_ALLOW_SCRIPT)
        flags |= XSF_ALLOW_SCRIPT;
    if (style->doc->flags & XDF_RESOLVE)
        flags |= XSF_RESOLVE;
    return xs_compile(style, flags, err);
}

/* the text of the result in the encoding the stylesheet asks for, or utf-16 text. */
void xs_result_text(XsSheet *sheet, XDoc *result, BOOL v6, XBuf *text, const WCHAR *encoding)
{
    xs_serialize(&sheet->output, result, v6, text, encoding);
}

HRESULT xs_transform_node(XNode *input, XNode *style, VARIANT *output, BSTR *text)
{
    XsError err = {};
    XsSheet *sheet = xs_compile_for(style, &err);
    BOOL v6 = (style->doc->flags & XDF_V6) != 0;
    if (!sheet) {
        HRESULT hr = fail_with(&err, v6);
        xse_clear(&err);
        return hr;
    }
    XsRequest req = {};
    req.sheet = sheet;
    req.input = input;
    request_flags(style, &req);
    HRESULT hr = xs_run(&req);
    if (FAILED(hr)) {
        hr = fail_with(&req.error, v6);
        xse_clear(&req.error);
        xs_sheet_release(sheet);
        return hr;
    }
    XBuf buf = {};
    if (output) {
        /* to an object. */
        const WCHAR *enc = xs_output_encoding(&sheet->output);
        const VARIANT *o = xv_deref(output);
        XNode *target = nullptr;
        IStream *stream = nullptr;
        if ((o->vt == VT_DISPATCH || o->vt == VT_UNKNOWN) && o->punkVal) {
            if (xd_node_is_ours(o->punkVal))
                target = (XNode *)o->punkVal;
            else
                o->punkVal->lpVtbl->QueryInterface(o->punkVal, &IID_IStream, (void **)&stream);
        }
        if (target) {
            xs_result_text(sheet, req.result, v6, &buf, L"UTF-16");
            XDoc *td = target->doc;
            if (target->type == XN_DOCUMENT) {
                xd_err_clear(td);
                xd_clear_children(&td->node);
                if (!xd_parse(td, buf.s ? buf.s : L"", buf.length, XP_STRING))
                    hr = S_OK;
            } else {
                hr = E_INVALIDARG;
            }
        } else if (stream) {
            xs_result_text(sheet, req.result, v6, &buf, enc);
            BYTE *bytes;
            UINT size;
            if (xd_encode(buf.s ? buf.s : L"", buf.length, enc, &bytes, &size)) {
                UINT done = 0;
                while (done < size && SUCCEEDED(hr)) {
                    ULONG wrote = 0;
                    hr = stream->lpVtbl->Write(stream, bytes + done, size - done, &wrote);
                    if (SUCCEEDED(hr) && !wrote)
                        hr = E_FAIL;
                    done += wrote;
                }
                xd_free(bytes);
            }
            stream->lpVtbl->Release(stream);
        } else {
            hr = E_INVALIDARG;
        }
    } else {
        xs_result_text(sheet, req.result, v6, &buf, nullptr);
        *text = xd_bstr(buf.s ? buf.s : L"", buf.length);
        if (!*text)
            hr = E_OUTOFMEMORY;
    }
    xb_free(&buf);
    xn_release(&req.result->node);
    xs_sheet_release(sheet);
    return hr;
}

/* ------------------------------------------------------------------ the template and the processor */

struct XTemplate {
    const void *vtbl;
    LONG refs;
    BOOL v6;
    XNode *style;
    XsSheet *sheet;
};

struct XProc {
    const void *vtbl;
    LONG refs;
    XTemplate *tmpl;
    XsSheet *sheet;
    XNode *style;
    BOOL v6;
    VARIANT input;
    VARIANT output;
    XsParam *params;
    UINT nparams;
    XsObject *objects;
    UINT nobjects;
    WCHAR *mode;
    WCHAR *mode_uri;
    LONG ready;
    BSTR text;
};

extern const XTplVtbl xd_vtbl_template;
extern const XProcVtbl xd_vtbl_processor;

static HRESULT fail_hr(HRESULT hr, const WCHAR *text, BOOL v6)
{
    XsError e = {};
    xse_set_text(&e, hr, text);
    hr = fail_with(&e, v6);
    xse_clear(&e);
    return hr;
}

static HRESULT fail_id(UINT id, BOOL v6, const WCHAR *p1)
{
    XsError e = {};
    xse_set(&e, id, p1, nullptr, nullptr);
    HRESULT hr = fail_with(&e, v6);
    xse_clear(&e);
    return hr;
}

static HRESULT STDMETHODCALLTYPE t_qi(XTemplate *t, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_IXSLTemplate))
        return E_NOINTERFACE;
    InterlockedIncrement(&t->refs);
    *out = t;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE t_addref(XTemplate *t)
{
    return (ULONG)InterlockedIncrement(&t->refs);
}

static ULONG STDMETHODCALLTYPE t_release(XTemplate *t)
{
    LONG refs = InterlockedDecrement(&t->refs);
    if (!refs) {
        if (t->sheet)
            xs_sheet_release(t->sheet);
        if (t->style)
            xn_release(t->style);
        xd_free(t);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE t_typeinfocount(XTemplate *o, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE t_typeinfo(XTemplate *o, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE p_typeinfocount(XProc *o, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_typeinfo(XProc *o, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE t_ids(XTemplate *t, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(&xd_dispset_template, riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE t_invoke(XTemplate *t, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                          DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(t, &xd_dispset_template, id, riid, lcid, flags, params, result, excep, argerr);
}

static HRESULT STDMETHODCALLTYPE t_putref_stylesheet(XTemplate *t, XNode *style)
{
    if (!style) {
        if (t->sheet)
            xs_sheet_release(t->sheet);
        if (t->style)
            xn_release(t->style);
        t->sheet = nullptr;
        t->style = nullptr;
        return S_OK;
    }
    if (!t->v6 && !(style->doc->flags & XDF_FREE))
        return fail_id(0xC00CE22Du, t->v6, nullptr);
    XsError err = {};
    XsSheet *sheet = xs_compile_for(style, &err);
    if (!sheet) {
        HRESULT hr = fail_with(&err, t->v6);
        xse_clear(&err);
        return hr;
    }
    if (t->sheet)
        xs_sheet_release(t->sheet);
    if (t->style)
        xn_release(t->style);
    t->sheet = sheet;
    t->style = style;
    xn_addref(style);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE t_get_stylesheet(XTemplate *t, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!t->style)
        return S_OK;
    return xd_ret_node(t->style, out);
}

static HRESULT STDMETHODCALLTYPE t_createProcessor(XTemplate *t, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!t->sheet)
        return fail_id(0xC00CE228u, t->v6, nullptr);
    XProc *p = xd_alloc(sizeof(XProc));
    if (!p)
        return E_OUTOFMEMORY;
    p->vtbl = &xd_vtbl_processor;
    p->refs = 1;
    p->tmpl = t;
    p->v6 = t->v6;
    p->sheet = t->sheet;
    xs_sheet_addref(p->sheet);
    p->style = t->style;
    xn_addref(p->style);
    InterlockedIncrement(&t->refs);
    InterlockedIncrement(&xd_objects);
    VariantInit(&p->input);
    VariantInit(&p->output);
    p->ready = 0;
    *out = p;
    return S_OK;
}

const XTplVtbl xd_vtbl_template = {t_qi,  t_addref, t_release,           t_typeinfocount,  t_typeinfo,
                                   t_ids, t_invoke, t_putref_stylesheet, t_get_stylesheet, t_createProcessor};

HRESULT xd_create_template(BOOL v6, REFIID riid, void **out)
{
    XTemplate *t = xd_alloc(sizeof(XTemplate));
    if (!t)
        return E_OUTOFMEMORY;
    t->vtbl = &xd_vtbl_template;
    t->refs = 1;
    t->v6 = v6;
    InterlockedIncrement(&xd_objects);
    HRESULT hr = t_qi(t, riid, out);
    t_release(t);
    return hr;
}

/* ------------------------------------------------------------------ the processor */

static void proc_clear_params(XProc *p)
{
    for (UINT i = 0; i < p->nparams; i++) {
        xd_free(p->params[i].name);
        xd_free(p->params[i].uri);
        VariantClear(&p->params[i].value);
    }
    xd_free(p->params);
    p->params = nullptr;
    p->nparams = 0;
    for (UINT i = 0; i < p->nobjects; i++) {
        xd_free(p->objects[i].uri);
        p->objects[i].object->lpVtbl->Release(p->objects[i].object);
    }
    xd_free(p->objects);
    p->objects = nullptr;
    p->nobjects = 0;
}

static HRESULT STDMETHODCALLTYPE p_qi(XProc *p, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_IXSLProcessor))
        return E_NOINTERFACE;
    InterlockedIncrement(&p->refs);
    *out = p;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE p_addref(XProc *p)
{
    return (ULONG)InterlockedIncrement(&p->refs);
}

static ULONG STDMETHODCALLTYPE p_release(XProc *p)
{
    LONG refs = InterlockedDecrement(&p->refs);
    if (!refs) {
        SysFreeString(p->text);
        proc_clear_params(p);
        VariantClear(&p->input);
        VariantClear(&p->output);
        xd_free(p->mode);
        xd_free(p->mode_uri);
        xs_sheet_release(p->sheet);
        xn_release(p->style);
        t_release(p->tmpl);
        xd_free(p);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE p_ids(XProc *p, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(&xd_dispset_processor, riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE p_invoke(XProc *p, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                          DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(p, &xd_dispset_processor, id, riid, lcid, flags, params, result, excep, argerr);
}

static HRESULT STDMETHODCALLTYPE p_put_input(XProc *p, VARIANT input)
{
    const VARIANT *v = xv_deref(&input);
    BOOL object = (v->vt & VT_TYPEMASK) == VT_DISPATCH || (v->vt & VT_TYPEMASK) == VT_UNKNOWN;
    if (object) {
        XNode *n = xv_to_node(&input);
        if (!n)
            return E_INVALIDARG;
        xn_release(n);
    }
    VariantClear(&p->input);
    HRESULT hr = VariantCopyInd(&p->input, &input);
    if (SUCCEEDED(hr) && p->ready < 2)
        p->ready = 2;
    return hr;
}

static HRESULT STDMETHODCALLTYPE p_get_input(XProc *p, VARIANT *out)
{
    if (!out)
        return E_INVALIDARG;
    VariantInit(out);
    if (!p->v6) {
        /* msxml3 answers this property with a variant that has no type a script can use. */
        out->vt = VT_VARIANT;
        return DISP_E_BADVARTYPE;
    }
    return VariantCopy(out, &p->input);
}

static HRESULT STDMETHODCALLTYPE p_get_ownerTemplate(XProc *p, void **out)
{
    if (!out)
        return E_INVALIDARG;
    InterlockedIncrement(&p->tmpl->refs);
    *out = p->tmpl;
    return S_OK;
}

static WCHAR *dup_bstr(BSTR s)
{
    return s ? xd_wcsdup(s, xd_slen(s)) : nullptr;
}

static HRESULT STDMETHODCALLTYPE p_setStartMode(XProc *p, BSTR mode, BSTR uri)
{
    if (mode && *mode) {
        int bad = xd_check_name(mode, xd_slen(mode), FALSE);
        if (bad >= 0) {
            XsError e = {};
            xse_name_error(&e, mode, xd_slen(mode), (UINT)bad);
            HRESULT hr = fail_with(&e, p->v6);
            xse_clear(&e);
            return hr;
        }
    }
    xd_free(p->mode);
    xd_free(p->mode_uri);
    p->mode = dup_bstr(mode);
    p->mode_uri = dup_bstr(uri);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_get_startMode(XProc *p, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = xd_bstr(p->mode ? p->mode : L"", p->mode ? xd_wlen(p->mode) : 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_get_startModeURI(XProc *p, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = xd_bstr(p->mode_uri ? p->mode_uri : L"", p->mode_uri ? xd_wlen(p->mode_uri) : 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_put_output(XProc *p, VARIANT output)
{
    VariantClear(&p->output);
    if (output.vt == VT_EMPTY || output.vt == VT_NULL)
        return S_OK;
    return VariantCopyInd(&p->output, &output);
}

static HRESULT STDMETHODCALLTYPE p_get_output(XProc *p, VARIANT *out)
{
    if (!out)
        return E_INVALIDARG;
    VariantInit(out);
    if (p->output.vt != VT_EMPTY)
        return VariantCopy(out, &p->output);
    out->vt = VT_BSTR;
    if (p->text) {
        out->bstrVal = p->text;
        p->text = nullptr;
    } else {
        out->bstrVal = SysAllocStringLen(L"", 0);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_transform(XProc *p, VARIANT_BOOL *done)
{
    if (!done)
        return E_INVALIDARG;
    *done = VARIANT_FALSE;
    XNode *input = nullptr;
    XDoc *loaded = nullptr;
    if (p->input.vt == VT_DISPATCH || p->input.vt == VT_UNKNOWN) {
        input = xv_to_node(&p->input);
    } else if (p->input.vt != VT_EMPTY && p->input.vt != VT_NULL) {
        /* anything else names a document to read. */
        VARIANT s;
        VariantInit(&s);
        if (SUCCEEDED(VariantChangeType(&s, &p->input, 0, VT_BSTR)) && s.bstrVal) {
            loaded = xd_doc_new(p->v6 ? XDF_V6 : 0);
            if (loaded && xd_load_file(loaded, s.bstrVal)) {
                input = &loaded->node;
            } else {
                BOOL bad_name = FALSE;
                for (BSTR q = s.bstrVal; *q; q++)
                    if (*q == '<' || *q == '>' || *q == '"' || *q == '|' || *q == '*' || *q == '?')
                        bad_name = TRUE;
                if (loaded)
                    xn_release(&loaded->node);
                VariantClear(&s);
                p->ready = 4;
                return fail_hr(bad_name ? 0x8007007Bu : 0x800C0006u,
                               bad_name ? L"The filename, directory name, or volume label syntax is incorrect.\r\n"
                                        : L"The system cannot locate the object specified.\r\n",
                               p->v6);
            }
        }
        VariantClear(&s);
    }
    if (!input)
        return fail_id(0xC00CE228u, p->v6, nullptr);
    XsRequest req = {};
    req.sheet = p->sheet;
    req.input = input;
    request_flags(p->style, &req);
    req.params = p->params;
    req.nparams = p->nparams;
    req.objects = p->objects;
    req.nobjects = p->nobjects;
    req.mode = p->mode && *p->mode ? p->mode : nullptr;
    req.mode_uri = p->mode_uri && *p->mode_uri ? p->mode_uri : nullptr;
    HRESULT hr = xs_run(&req);
    if (FAILED(hr)) {
        hr = fail_with(&req.error, p->v6);
        xse_clear(&req.error);
        xn_release(input);
        if (loaded)
            xn_release(&loaded->node);
        p->ready = 4;
        return hr;
    }
    XBuf buf = {};
    IStream *stream = nullptr;
    XNode *target = nullptr;
    const VARIANT *o = &p->output;
    if ((o->vt == VT_DISPATCH || o->vt == VT_UNKNOWN) && o->punkVal) {
        if (xd_node_is_ours(o->punkVal))
            target = (XNode *)o->punkVal;
        else
            o->punkVal->lpVtbl->QueryInterface(o->punkVal, &IID_IStream, (void **)&stream);
    }
    const WCHAR *enc = xs_output_encoding(&p->sheet->output);
    if (target && target->type == XN_DOCUMENT) {
        xs_result_text(p->sheet, req.result, p->v6, &buf, L"UTF-16");
        XDoc *td = target->doc;
        xd_err_clear(td);
        xd_clear_children(&td->node);
        xd_parse(td, buf.s ? buf.s : L"", buf.length, XP_STRING);
    } else if (stream) {
        xs_result_text(p->sheet, req.result, p->v6, &buf, enc);
        BYTE *bytes;
        UINT size;
        if (xd_encode(buf.s ? buf.s : L"", buf.length, enc, &bytes, &size)) {
            UINT done_bytes = 0;
            while (done_bytes < size && SUCCEEDED(hr)) {
                ULONG wrote = 0;
                hr = stream->lpVtbl->Write(stream, bytes + done_bytes, size - done_bytes, &wrote);
                if (SUCCEEDED(hr) && !wrote)
                    hr = E_FAIL;
                done_bytes += wrote;
            }
            xd_free(bytes);
        }
        stream->lpVtbl->Release(stream);
    } else {
        xs_result_text(p->sheet, req.result, p->v6, &buf, nullptr);
        SysFreeString(p->text);
        p->text = xd_bstr(buf.s ? buf.s : L"", buf.length);
    }
    xb_free(&buf);
    xn_release(&req.result->node);
    xn_release(input);
    if (loaded)
        xn_release(&loaded->node);
    p->ready = 4;
    if (SUCCEEDED(hr))
        *done = VARIANT_TRUE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE p_reset(XProc *p)
{
    SysFreeString(p->text);
    p->text = nullptr;
    p->ready = p->input.vt != VT_EMPTY ? 2 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_get_readyState(XProc *p, LONG *state)
{
    if (!state)
        return E_INVALIDARG;
    *state = p->ready;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_addParameter(XProc *p, BSTR name, VARIANT value, BSTR uri)
{
    if (!name || !*name)
        return E_INVALIDARG;
    {
        const VARIANT *v = xv_deref(&value);
        VARTYPE t = v->vt & VT_TYPEMASK;
        BOOL ok;
        switch (t) {
        case VT_BSTR: case VT_BOOL: case VT_I1: case VT_I2: case VT_I4: case VT_INT: case VT_UI1: case VT_UI2:
        case VT_UI4: case VT_UINT: case VT_I8: case VT_UI8: case VT_R4: case VT_R8: case VT_CY: case VT_DECIMAL:
            ok = !(v->vt & VT_ARRAY);
            break;
        case VT_DISPATCH:
        case VT_UNKNOWN: {
            IUnknown *u = (v->vt & VT_BYREF) ? (v->ppunkVal ? *v->ppunkVal : nullptr) : v->punkVal;
            ok = u && (xd_node_is_ours(u) || xd_list_is_ours(u));
            break;
        }
        case VT_NULL:
        case VT_EMPTY:
            if (p->v6)
                ok = TRUE;
            else
                return DISP_E_TYPEMISMATCH;
            break;
        default:
            ok = FALSE;
        }
        if (!ok)
            return p->v6 ? DISP_E_TYPEMISMATCH : E_INVALIDARG;
    }
    XsParam *list = xd_realloc(p->params, (p->nparams + 1) * sizeof(XsParam));
    if (!list)
        return E_OUTOFMEMORY;
    p->params = list;
    XsParam *a = &p->params[p->nparams];
    a->name = dup_bstr(name);
    a->uri = uri && *uri ? dup_bstr(uri) : nullptr;
    VariantInit(&a->value);
    HRESULT hr = VariantCopyInd(&a->value, &value);
    if (FAILED(hr)) {
        xd_free(a->name);
        xd_free(a->uri);
        return hr;
    }
    p->nparams++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_addObject(XProc *p, IDispatch *object, BSTR uri)
{
    if (!object || !uri)
        return E_INVALIDARG;
    XsObject *list = xd_realloc(p->objects, (p->nobjects + 1) * sizeof(XsObject));
    if (!list)
        return E_OUTOFMEMORY;
    p->objects = list;
    p->objects[p->nobjects].uri = dup_bstr(uri);
    p->objects[p->nobjects].object = object;
    object->lpVtbl->AddRef(object);
    p->nobjects++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_get_stylesheet(XProc *p, void **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_ret_node(p->style, out);
}

const XProcVtbl xd_vtbl_processor = {p_qi,       p_addref,   p_release,           p_typeinfocount, p_typeinfo,
                                     p_ids,      p_invoke,   p_put_input,         p_get_input,     p_get_ownerTemplate,
                                     p_setStartMode, p_get_startMode, p_get_startModeURI, p_put_output, p_get_output,
                                     p_transform, p_reset,   p_get_readyState,    p_addParameter,  p_addObject,
                                     p_get_stylesheet};
