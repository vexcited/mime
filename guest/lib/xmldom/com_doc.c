/* IXMLDOMDocument, IXMLDOMDocument2 and IXMLDOMDocument3. */
#include <xmldom/xmldom.h>

#define D(n) ((XDoc *)(n))

/* ------------------------------------------------------------------ load state */

static void fire(IDispatch *handler)
{
    if (!handler)
        return;
    DISPPARAMS none = {};
    VARIANT result;
    VariantInit(&result);
    handler->lpVtbl->Invoke(handler, 0, &IID_NULL, 0x409, DISPATCH_METHOD, &none, &result, nullptr, nullptr);
    VariantClear(&result);
}

/* a load steps through loading, loaded, interactive and completed, firing the handler at each. */
static void run_states(XDoc *doc)
{
    for (LONG state = 1; state <= 4; state++) {
        doc->ready_state = state;
        fire(doc->on_readystatechange);
    }
}

HRESULT xd_read_stream(IUnknown *object, BYTE **data, UINT *size)
{
    IStream *stream = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IStream, (void **)&stream)) &&
        FAILED(object->lpVtbl->QueryInterface(object, &IID_ISequentialStream, (void **)&stream)))
        return E_INVALIDARG;
    BYTE *buffer = nullptr;
    UINT used = 0, cap = 0;
    HRESULT hr = S_OK;
    for (;;) {
        if (cap - used < 8192) {
            UINT ncap = cap ? cap * 2 : 16384;
            BYTE *nb = xd_realloc(buffer, ncap);
            if (!nb) {
                hr = E_OUTOFMEMORY;
                break;
            }
            buffer = nb;
            cap = ncap;
        }
        ULONG got = 0;
        hr = stream->lpVtbl->Read(stream, buffer + used, cap - used, &got);
        if (FAILED(hr) || !got) {
            if (SUCCEEDED(hr))
                hr = S_OK;
            break;
        }
        used += got;
    }
    stream->lpVtbl->Release(stream);
    if (FAILED(hr)) {
        xd_free(buffer);
        return hr;
    }
    *data = buffer;
    *size = used;
    return S_OK;
}

/* ------------------------------------------------------------------ IXMLDOMDocument */

static HRESULT STDMETHODCALLTYPE d_get_doctype(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    for (XNode *c = n->first; c; c = c->next)
        if (c->type == XN_DOCTYPE)
            return xd_ret_node(c, out);
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE d_get_implementation(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_create_implementation(out);
}

static HRESULT STDMETHODCALLTYPE d_get_documentElement(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_ret_node(xd_document_element(D(n)), out);
}

static HRESULT STDMETHODCALLTYPE d_putref_documentElement(XNode *n, XNode *element)
{
    XNode *cur = xd_document_element(D(n));
    if (!element) {
        if (cur) {
            xd_unlink(cur);
            xd_free_if_orphan(cur);
        }
        return S_OK;
    }
    if (!xd_node_is_ours(element) || element->type != XN_ELEMENT)
        return E_INVALIDARG;
    if (cur == element)
        return S_OK;
    if (!cur)
        return xd_insert(n, element, nullptr, nullptr);
    XNode *after = cur->next;
    xn_addref(cur);
    xd_unlink(cur);
    HRESULT hr = xd_insert(n, element, after == element ? element->next : after, nullptr);
    if (FAILED(hr))
        xd_link(n, cur, after);
    xn_release(cur);
    return hr;
}

static HRESULT new_named(XNode *n, UINT type, BSTR name, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    UINT len = name ? xd_slen(name) : 0;
    HRESULT hr = xd_check_new_name(name, len);
    if (FAILED(hr))
        return hr;
    XNode *e = xd_node_new(n->doc, type, name, len, nullptr);
    if (!e)
        return E_OUTOFMEMORY;
    if (type == XN_ATTRIBUTE && xd_is_xmlns_attr(e))
        e->uri = xd_intern(n->doc, L"http://www.w3.org/2000/xmlns/", (UINT)-1);
    return xd_ret_node(e, out);
}

static HRESULT STDMETHODCALLTYPE d_createElement(XNode *n, BSTR name, void **out)
{
    return new_named(n, XN_ELEMENT, name, out);
}

static HRESULT STDMETHODCALLTYPE d_createDocumentFragment(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    XNode *f = xd_node_new(n->doc, XN_FRAGMENT, nullptr, 0, nullptr);
    return f ? xd_ret_node(f, out) : E_OUTOFMEMORY;
}

static HRESULT new_data(XNode *n, UINT type, BSTR data, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    XNode *t = xd_node_new(n->doc, type, nullptr, 0, nullptr);
    if (!t)
        return E_OUTOFMEMORY;
    xd_node_set_value(t, data ? data : L"", data ? xd_slen(data) : 0);
    return xd_ret_node(t, out);
}

static HRESULT STDMETHODCALLTYPE d_createTextNode(XNode *n, BSTR data, void **out)
{
    return new_data(n, XN_TEXT, data, out);
}

static HRESULT STDMETHODCALLTYPE d_createComment(XNode *n, BSTR data, void **out)
{
    return new_data(n, XN_COMMENT, data, out);
}

static HRESULT STDMETHODCALLTYPE d_createCDATASection(XNode *n, BSTR data, void **out)
{
    return new_data(n, XN_CDATA, data, out);
}

static HRESULT new_pi(XNode *n, BSTR target, BSTR data, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    UINT tlen = target ? xd_slen(target) : 0;
    if (!tlen)
        return (HRESULT)XE_NEED_NAME;
    if (xd_check_name(target, tlen, FALSE) >= 0)
        return (HRESULT)XE_NAME_HAS_CHAR;
    XNode *p = xd_node_new(n->doc, XN_PI, target, tlen, nullptr);
    if (!p)
        return E_OUTOFMEMORY;
    if (tlen == 3 && !memcmp(target, L"xml", 6))
        p->flags |= XF_XMLDECL;
    HRESULT hr = xd_set_pi_data(p, data ? data : L"", data ? xd_slen(data) : 0);
    if (FAILED(hr)) {
        xd_free_if_orphan(p);
        return hr;
    }
    return xd_ret_node(p, out);
}

static HRESULT STDMETHODCALLTYPE d_createProcessingInstruction(XNode *n, BSTR target, BSTR data, void **out)
{
    return new_pi(n, target, data, out);
}

static HRESULT STDMETHODCALLTYPE d_createAttribute(XNode *n, BSTR name, void **out)
{
    return new_named(n, XN_ATTRIBUTE, name, out);
}

static HRESULT STDMETHODCALLTYPE d_createEntityReference(XNode *n, BSTR name, void **out)
{
    return new_named(n, XN_ENTITYREF, name, out);
}

static HRESULT STDMETHODCALLTYPE d_getElementsByTagName(XNode *n, BSTR name, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    XList *l = xd_list_new(n, XL_TAGNAME, name, FALSE);
    *out = l;
    return l ? S_OK : E_OUTOFMEMORY;
}

/* createNode takes the type as a number or a name. */
static int node_type_of(const VARIANT *v)
{
    LONG n;
    const VARIANT *d = xv_deref(v);
    if (d && (d->vt & ~VT_BYREF) != VT_BSTR && xv_to_long(d, &n))
        return n;
    XBuf b = {};
    int type = -1;
    if (xv_to_string(d, &b) && b.s) {
        static const struct {
            const WCHAR *name;
            int type;
        } names[] = {
            {L"element", XN_ELEMENT},
            {L"attribute", XN_ATTRIBUTE},
            {L"text", XN_TEXT},
            {L"cdatasection", XN_CDATA},
            {L"entityreference", XN_ENTITYREF},
            {L"processinginstruction", XN_PI},
            {L"comment", XN_COMMENT},
            {L"document", XN_DOCUMENT},
            {L"documentfragment", XN_FRAGMENT},
            {L"documenttype", XN_DOCTYPE},
        };
        for (UINT i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (!xd_wcsicmp(b.s, names[i].name))
                type = names[i].type;
        if (type < 0 && xd_parse_number(b.s, b.length) == xd_parse_number(b.s, b.length))
            type = (int)xd_parse_number(b.s, b.length);
    }
    xb_free(&b);
    return type;
}

static HRESULT STDMETHODCALLTYPE d_createNode(XNode *n, VARIANT type, BSTR name, BSTR uri, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    int t = node_type_of(&type);
    UINT len = name ? xd_slen(name) : 0;
    switch (t) {
    case XN_ELEMENT:
    case XN_ATTRIBUTE: {
        HRESULT hr = xd_check_new_name(name, len);
        if (FAILED(hr))
            return hr;
        XNode *e = xd_node_new(n->doc, (UINT)t, name, len, uri);
        if (!e)
            return E_OUTOFMEMORY;
        if (t == XN_ATTRIBUTE && xd_is_xmlns_attr(e))
            e->uri = xd_intern(n->doc, L"http://www.w3.org/2000/xmlns/", (UINT)-1);
        return xd_ret_node(e, out);
    }
    case XN_TEXT:
    case XN_CDATA:
    case XN_COMMENT:
        return new_data(n, (UINT)t, nullptr, out);
    case XN_ENTITYREF:
        return new_named(n, XN_ENTITYREF, name, out);
    case XN_PI:
        return new_pi(n, name, nullptr, out);
    case XN_FRAGMENT:
        return d_createDocumentFragment(n, out);
    }
    return (HRESULT)XE_BAD_NODE_TYPE;
}

static HRESULT STDMETHODCALLTYPE d_nodeFromID(XNode *n, BSTR id, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return S_FALSE;
}

static HRESULT load_result(XDoc *doc, BOOL ok, VARIANT_BOOL *flag)
{
    run_states(doc);
    *flag = ok ? VARIANT_TRUE : VARIANT_FALSE;
    return ok ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE d_load(XNode *n, VARIANT source, VARIANT_BOOL *flag)
{
    XDoc *doc = D(n);
    if (!flag)
        return E_INVALIDARG;
    *flag = VARIANT_FALSE;
    const VARIANT *v = xv_deref(&source);
    if (!v)
        return E_INVALIDARG;
    BYTE *data = nullptr;
    UINT size = 0;
    switch (v->vt & ~VT_BYREF) {
    case VT_BSTR: {
        BSTR s = (v->vt & VT_BYREF) ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (!s || !*s)
            return E_INVALIDARG;
        return load_result(doc, xd_load_file(doc, s), flag);
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
        IUnknown *obj = (v->vt & VT_BYREF) ? (v->ppunkVal ? *v->ppunkVal : nullptr) : v->punkVal;
        if (!obj)
            return E_INVALIDARG;
        HRESULT hr = xd_read_stream(obj, &data, &size);
        if (FAILED(hr))
            return hr;
        break;
    }
    case VT_ARRAY | VT_UI1: {
        SAFEARRAY *a = (v->vt & VT_BYREF) ? (v->pparray ? *v->pparray : nullptr) : v->parray;
        if (!a || SafeArrayGetDim(a) != 1)
            return E_INVALIDARG;
        LONG lo, hi;
        void *bytes;
        if (FAILED(SafeArrayGetLBound(a, 1, &lo)) || FAILED(SafeArrayGetUBound(a, 1, &hi)) ||
            FAILED(SafeArrayAccessData(a, &bytes)))
            return E_INVALIDARG;
        size = hi >= lo ? (UINT)(hi - lo + 1) : 0;
        data = xd_alloc_raw(size);
        if (data)
            memcpy(data, bytes, size);
        SafeArrayUnaccessData(a);
        if (!data)
            return E_OUTOFMEMORY;
        break;
    }
    default:
        return E_INVALIDARG;
    }
    xd_free(doc->url);
    doc->url = nullptr;
    BOOL ok = xd_parse_bytes(doc, data, size);
    xd_free(data);
    return load_result(doc, ok, flag);
}

static HRESULT STDMETHODCALLTYPE d_get_readyState(XNode *n, LONG *state)
{
    if (!state)
        return E_INVALIDARG;
    *state = D(n)->ready_state;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_parseError(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_create_error_object(&D(n)->err, out);
}

static HRESULT STDMETHODCALLTYPE d_get_url(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    XDoc *doc = D(n);
    return xd_ret_string(p, doc->url, doc->url ? xd_wlen(doc->url) : 0);
}

static HRESULT STDMETHODCALLTYPE d_get_async(XNode *n, VARIANT_BOOL *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = (D(n)->flags & XDF_ASYNC) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT set_flag(XNode *n, UINT flag, VARIANT_BOOL on)
{
    if (on)
        D(n)->flags |= flag;
    else
        D(n)->flags &= ~flag;
    return S_OK;
}

static HRESULT get_flag(XNode *n, UINT flag, VARIANT_BOOL *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = (D(n)->flags & flag) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_put_async(XNode *n, VARIANT_BOOL on)
{
    return set_flag(n, XDF_ASYNC, on);
}

static HRESULT STDMETHODCALLTYPE d_abort(XNode *n)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_loadXML(XNode *n, BSTR xml, VARIANT_BOOL *flag)
{
    XDoc *doc = D(n);
    if (!flag)
        return E_INVALIDARG;
    *flag = VARIANT_FALSE;
    xd_free(doc->url);
    doc->url = nullptr;
    BOOL ok = xd_parse(doc, xml ? xml : L"", xml ? xd_slen(xml) : 0, XP_STRING);
    return load_result(doc, ok, flag);
}

static HRESULT STDMETHODCALLTYPE d_save(XNode *n, VARIANT target)
{
    XDoc *doc = D(n);
    const VARIANT *v = xv_deref(&target);
    if (!v)
        return E_INVALIDARG;
    switch (v->vt & ~VT_BYREF) {
    case VT_BSTR: {
        BSTR s = (v->vt & VT_BYREF) ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (!s || !*s)
            return E_INVALIDARG;
        BOOL other;
        WCHAR *path = xd_location_to_path(s, &other);
        if (!path)
            return E_INVALIDARG;
        HRESULT hr = xd_save_file(doc, path);
        xd_free(path);
        return hr;
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
        IUnknown *obj = (v->vt & VT_BYREF) ? (v->ppunkVal ? *v->ppunkVal : nullptr) : v->punkVal;
        if (!obj || (void *)obj == (void *)n)
            return E_INVALIDARG;
        IStream *stream = nullptr;
        if (FAILED(obj->lpVtbl->QueryInterface(obj, &IID_IStream, (void **)&stream)) &&
            FAILED(obj->lpVtbl->QueryInterface(obj, &IID_ISequentialStream, (void **)&stream)))
            return E_INVALIDARG;
        HRESULT hr = xd_save_document(doc, stream, nullptr);
        stream->lpVtbl->Release(stream);
        return hr;
    }
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d_get_validateOnParse(XNode *n, VARIANT_BOOL *p)
{
    return get_flag(n, XDF_VALIDATE, p);
}

static HRESULT STDMETHODCALLTYPE d_put_validateOnParse(XNode *n, VARIANT_BOOL on)
{
    return set_flag(n, XDF_VALIDATE, on);
}

static HRESULT STDMETHODCALLTYPE d_get_resolveExternals(XNode *n, VARIANT_BOOL *p)
{
    return get_flag(n, XDF_RESOLVE, p);
}

static HRESULT STDMETHODCALLTYPE d_put_resolveExternals(XNode *n, VARIANT_BOOL on)
{
    return set_flag(n, XDF_RESOLVE, on);
}

static HRESULT STDMETHODCALLTYPE d_get_preserveWhiteSpace(XNode *n, VARIANT_BOOL *p)
{
    return get_flag(n, XDF_PRESERVE, p);
}

static HRESULT STDMETHODCALLTYPE d_put_preserveWhiteSpace(XNode *n, VARIANT_BOOL on)
{
    return set_flag(n, XDF_PRESERVE, on);
}

/* a handler is a callable object or nothing. */
static HRESULT set_handler(IDispatch **slot, VARIANT handler)
{
    const VARIANT *v = xv_deref(&handler);
    IDispatch *d = nullptr;
    if (v && (v->vt == VT_DISPATCH || v->vt == VT_UNKNOWN)) {
        if (v->punkVal && FAILED(v->punkVal->lpVtbl->QueryInterface(v->punkVal, &IID_IDispatch, (void **)&d)))
            return E_INVALIDARG;
    } else if (!xv_is_missing(v)) {
        return E_INVALIDARG;
    }
    if (*slot)
        (*slot)->lpVtbl->Release(*slot);
    *slot = d;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_put_onreadystatechange(XNode *n, VARIANT handler)
{
    return set_handler(&D(n)->on_readystatechange, handler);
}

static HRESULT STDMETHODCALLTYPE d_put_ondataavailable(XNode *n, VARIANT handler)
{
    return set_handler(&D(n)->on_dataavailable, handler);
}

static HRESULT STDMETHODCALLTYPE d_put_ontransformnode(XNode *n, VARIANT handler)
{
    return set_handler(&D(n)->on_transformnode, handler);
}

/* ------------------------------------------------------------------ IXMLDOMDocument2 and 3 */

static HRESULT STDMETHODCALLTYPE d_get_namespaces(XNode *n, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d_get_schemas(XNode *n, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    return VariantCopy(v, &D(n)->schemas);
}

static HRESULT STDMETHODCALLTYPE d_putref_schemas(XNode *n, VARIANT schemas)
{
    VariantClear(&D(n)->schemas);
    return VariantCopy(&D(n)->schemas, xv_deref(&schemas));
}

static HRESULT no_schema_error(void **out)
{
    if (!out)
        return E_INVALIDARG;
    XErr e = {};
    e.code = (LONG)XE_NO_SCHEMA;
    WCHAR *reason = xd_format(XE_NO_SCHEMA, nullptr, nullptr);
    e.reason = reason;
    HRESULT hr = xd_create_error_object(&e, out);
    xd_free(reason);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_validate(XNode *n, void **out)
{
    return no_schema_error(out);
}

static HRESULT STDMETHODCALLTYPE d_validateNode(XNode *n, XNode *node, void **out)
{
    if (!node || !xd_node_is_ours(node))
        return E_INVALIDARG;
    return no_schema_error(out);
}

static HRESULT STDMETHODCALLTYPE d_importNode(XNode *n, XNode *node, VARIANT_BOOL deep, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!node || !xd_node_is_ours(node) || node->type == XN_DOCUMENT)
        return E_INVALIDARG;
    XNode *c = xd_clone(node, n->doc, deep != 0);
    if (!c)
        return E_OUTOFMEMORY;
    return xd_ret_node(c, out);
}

enum { P_LANGUAGE, P_NAMESPACES, P_DEPTH, P_SIZE, P_FLAG };

static const struct {
    const WCHAR *name;
    int kind;
    UINT flag;
} properties[] = {
    {L"SelectionLanguage", P_LANGUAGE, 0},
    {L"SelectionNamespaces", P_NAMESPACES, 0},
    {L"MaxElementDepth", P_DEPTH, 0},
    {L"MaxXMLSize", P_SIZE, 0},
    {L"ServerHTTPRequest", P_FLAG, XDF_SERVER_HTTP},
    {L"ForcedResync", P_FLAG, XDF_FORCED_RESYNC},
    {L"AllowDocumentFunction", P_FLAG, XDF_ALLOW_DOCFN},
    {L"AllowXsltScript", P_FLAG, XDF_ALLOW_SCRIPT},
    {L"PopulateElementDefaultValues", P_FLAG, XDF_POPULATE},
    {L"ProhibitDTD", P_FLAG, XDF_PROHIBIT_DTD},
    {L"NewParser", P_FLAG, XDF_NEWPARSER},
    {L"MultipleErrorMessages", P_FLAG, XDF_MULTIPLE_ERRORS},
    {L"UseInlineSchema", P_FLAG, XDF_INLINE_SCHEMA},
    {L"ValidateOnParse", P_FLAG, XDF_VALIDATE},
    {L"ResolveExternals", P_FLAG, XDF_RESOLVE},
    {L"NormalizeAttributeValues", P_FLAG, XDF_NORMALIZE_ATTR},
};

/* the value is xmlns:p='uri' declarations separated by white space. */
static HRESULT set_selection_namespaces(XDoc *doc, const WCHAR *text, UINT length)
{
    XNs *list = nullptr;
    UINT count = 0, cap = 0;
    UINT i = 0;
    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        while (i < length && xd_is_space(text[i]))
            i++;
        if (i >= length)
            break;
        if (length - i < 5 || memcmp(text + i, L"xmlns", 10)) {
            hr = (HRESULT)XE_SELECTION_NAMESPACES;
            break;
        }
        i += 5;
        const WCHAR *prefix = L"";
        UINT plen = 0;
        if (i < length && text[i] == ':') {
            i++;
            prefix = text + i;
            while (i < length && xd_is_name_char(text[i]) && text[i] != ':')
                i++;
            plen = (UINT)(text + i - prefix);
            if (!plen) {
                hr = (HRESULT)XE_SELECTION_NAMESPACES;
                break;
            }
        }
        while (i < length && xd_is_space(text[i]))
            i++;
        if (i >= length || text[i] != '=') {
            hr = (HRESULT)XE_SELECTION_NAMESPACES;
            break;
        }
        i++;
        while (i < length && xd_is_space(text[i]))
            i++;
        if (i >= length || (text[i] != '"' && text[i] != '\'')) {
            hr = (HRESULT)XE_SELECTION_NAMESPACES;
            break;
        }
        WCHAR q = text[i++];
        const WCHAR *uri = text + i;
        while (i < length && text[i] != q)
            i++;
        if (i >= length) {
            hr = (HRESULT)XE_SELECTION_NAMESPACES;
            break;
        }
        UINT ulen = (UINT)(text + i - uri);
        i++;
        if (count == cap) {
            UINT ncap = cap ? cap * 2 : 8;
            XNs *nl = xd_realloc(list, (SIZE_T)ncap * sizeof(XNs));
            if (!nl) {
                hr = E_OUTOFMEMORY;
                break;
            }
            list = nl;
            cap = ncap;
        }
        list[count].prefix = xd_wcsdup(prefix, plen);
        list[count].uri = xd_wcsdup(uri, ulen);
        count++;
    }
    if (FAILED(hr)) {
        for (UINT k = 0; k < count; k++) {
            xd_free(list[k].prefix);
            xd_free(list[k].uri);
        }
        xd_free(list);
        return hr;
    }
    for (UINT k = 0; k < doc->sel_ns_count; k++) {
        xd_free(doc->sel_ns[k].prefix);
        xd_free(doc->sel_ns[k].uri);
    }
    xd_free(doc->sel_ns);
    doc->sel_ns = list;
    doc->sel_ns_count = count;
    xd_free(doc->sel_ns_text);
    doc->sel_ns_text = xd_wcsdup(text, length);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_setProperty(XNode *n, BSTR name, VARIANT value)
{
    XDoc *doc = D(n);
    if (!name)
        return E_INVALIDARG;
    int which = -1;
    for (UINT i = 0; i < sizeof(properties) / sizeof(properties[0]); i++)
        if (!xd_wcsicmp(name, properties[i].name))
            which = (int)i;
    if (which < 0)
        return (HRESULT)XE_BAD_PROPERTY_NAME;
    switch (properties[which].kind) {
    case P_LANGUAGE: {
        XBuf b = {};
        if (!xv_to_string(&value, &b) || !b.s) {
            xb_free(&b);
            return (HRESULT)XE_BAD_PROPERTY_VALUE;
        }
        HRESULT hr = S_OK;
        if (!wcscmp(b.s, L"XPath"))
            doc->flags |= XDF_XPATH;
        else if (!wcscmp(b.s, L"XSLPattern"))
            hr = (doc->flags & XDF_V6) ? (HRESULT)XE_XSLPATTERN_UNSUPPORTED : (doc->flags &= ~XDF_XPATH, S_OK);
        else
            hr = (HRESULT)XE_BAD_PROPERTY_VALUE;
        xb_free(&b);
        return hr;
    }
    case P_NAMESPACES: {
        XBuf b = {};
        if (!xv_to_string(&value, &b)) {
            xb_free(&b);
            return (HRESULT)XE_SELECTION_NAMESPACES;
        }
        HRESULT hr = set_selection_namespaces(doc, b.s ? b.s : L"", b.length);
        xb_free(&b);
        return hr;
    }
    case P_DEPTH:
    case P_SIZE: {
        LONG v;
        if (!xv_to_long(&value, &v))
            return DISP_E_TYPEMISMATCH;
        if (properties[which].kind == P_DEPTH)
            doc->max_depth = v;
        else
            doc->max_size = v;
        return S_OK;
    }
    }
    BOOL on;
    if (!xv_to_bool(&value, &on))
        return DISP_E_TYPEMISMATCH;
    if (on)
        doc->flags |= properties[which].flag;
    else
        doc->flags &= ~properties[which].flag;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_getProperty(XNode *n, BSTR name, VARIANT *v)
{
    XDoc *doc = D(n);
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    if (!name)
        return E_INVALIDARG;
    int which = -1;
    for (UINT i = 0; i < sizeof(properties) / sizeof(properties[0]); i++)
        if (!xd_wcsicmp(name, properties[i].name))
            which = (int)i;
    if (which < 0)
        return (HRESULT)XE_BAD_PROPERTY_NAME;
    switch (properties[which].kind) {
    case P_LANGUAGE:
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString((doc->flags & XDF_XPATH) ? L"XPath" : L"XSLPattern");
        return v->bstrVal ? S_OK : E_OUTOFMEMORY;
    case P_NAMESPACES:
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(doc->sel_ns_text ? doc->sel_ns_text : L"");
        return v->bstrVal ? S_OK : E_OUTOFMEMORY;
    case P_DEPTH:
        v->vt = VT_I4;
        v->lVal = doc->max_depth;
        return S_OK;
    case P_SIZE:
        v->vt = VT_I4;
        v->lVal = doc->max_size;
        return S_OK;
    }
    v->vt = VT_BOOL;
    v->boolVal = (doc->flags & properties[which].flag) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

/* ------------------------------------------------------------------ vtable and copies */

XDocumentVtbl xd_vtbl_document;

void xd_doc_vtbl_init(void)
{
    memcpy(&xd_vtbl_document, &xd_vtbl_node, sizeof(XNodeVtbl));
    XDocumentVtbl *t = &xd_vtbl_document;
    t->get_doctype = d_get_doctype;
    t->get_implementation = d_get_implementation;
    t->get_documentElement = d_get_documentElement;
    t->putref_documentElement = d_putref_documentElement;
    t->createElement = d_createElement;
    t->createDocumentFragment = d_createDocumentFragment;
    t->createTextNode = d_createTextNode;
    t->createComment = d_createComment;
    t->createCDATASection = d_createCDATASection;
    t->createProcessingInstruction = d_createProcessingInstruction;
    t->createAttribute = d_createAttribute;
    t->createEntityReference = d_createEntityReference;
    t->getElementsByTagName = d_getElementsByTagName;
    t->createNode = d_createNode;
    t->nodeFromID = d_nodeFromID;
    t->load = d_load;
    t->get_readyState = d_get_readyState;
    t->get_parseError = d_get_parseError;
    t->get_url = d_get_url;
    t->get_async = d_get_async;
    t->put_async = d_put_async;
    t->abort = d_abort;
    t->loadXML = d_loadXML;
    t->save = d_save;
    t->get_validateOnParse = d_get_validateOnParse;
    t->put_validateOnParse = d_put_validateOnParse;
    t->get_resolveExternals = d_get_resolveExternals;
    t->put_resolveExternals = d_put_resolveExternals;
    t->get_preserveWhiteSpace = d_get_preserveWhiteSpace;
    t->put_preserveWhiteSpace = d_put_preserveWhiteSpace;
    t->put_onreadystatechange = d_put_onreadystatechange;
    t->put_ondataavailable = d_put_ondataavailable;
    t->put_ontransformnode = d_put_ontransformnode;
    t->get_namespaces = d_get_namespaces;
    t->get_schemas = d_get_schemas;
    t->putref_schemas = d_putref_schemas;
    t->validate = d_validate;
    t->setProperty = d_setProperty;
    t->getProperty = d_getProperty;
    t->validateNode = d_validateNode;
    t->importNode = d_importNode;
}

XNode *xd_doc_clone(XDoc *src, BOOL deep)
{
    XDoc *copy = xd_doc_new(src->flags);
    if (!copy)
        return nullptr;
    copy->max_depth = src->max_depth;
    copy->max_size = src->max_size;
    if (src->sel_ns_text)
        set_selection_namespaces(copy, src->sel_ns_text, xd_wlen(src->sel_ns_text));
    if (deep) {
        for (XNode *c = src->node.first; c; c = c->next) {
            XNode *k = xd_clone(c, copy, TRUE);
            if (k)
                xd_link(&copy->node, k, nullptr);
        }
    }
    return &copy->node;
}
