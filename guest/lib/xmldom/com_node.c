/* the node interfaces: IXMLDOMNode and the interfaces of each node type. */
#include <xmldom/xslt.h>

#define N(p) ((XNode *)(p))

/* ------------------------------------------------------------------ names */

static const WCHAR *node_name(XNode *n, UINT *length)
{
    const WCHAR *s;
    switch (n->type) {
    case XN_TEXT:
        s = L"#text";
        break;
    case XN_CDATA:
        s = L"#cdata-section";
        break;
    case XN_COMMENT:
        s = L"#comment";
        break;
    case XN_DOCUMENT:
        s = L"#document";
        break;
    case XN_FRAGMENT:
        s = L"#document-fragment";
        break;
    default:
        if (!n->qname) {
            *length = 0;
            return L"";
        }
        *length = xd_atom_len(n->qname);
        return n->qname;
    }
    *length = xd_wlen(s);
    return s;
}

static const WCHAR *const type_names[XN_TYPES] = {
    L"", L"element", L"attribute", L"text", L"cdatasection", L"entityreference", L"entity", L"processinginstruction",
    L"comment", L"document", L"documenttype", L"documentfragment", L"notation",
};

/* the xml declaration's data is its attributes written out. */
static void pi_data(XNode *n, XBuf *out)
{
    if (!(n->flags & XF_XMLDECL)) {
        if (n->value)
            xb_put(out, n->value, n->length);
        return;
    }
    for (XNode *a = n->attrs; a; a = a->next) {
        if (a != n->attrs)
            xb_putc(out, ' ');
        xb_put(out, a->qname, xd_atom_len(a->qname));
        xb_puta(out, "=\"");
        if (a->value)
            xb_put(out, a->value, a->length);
        xb_putc(out, '"');
    }
}

/* the declaration's data is parsed back into its three attributes. */
static HRESULT set_decl_data(XNode *n, const WCHAR *text, UINT length)
{
    while (n->attrs) {
        XNode *a = n->attrs;
        xd_attr_unlink(a);
        xd_free_if_orphan(a);
    }
    UINT i = 0;
    while (i < length) {
        while (i < length && xd_is_space(text[i]))
            i++;
        if (i >= length)
            break;
        UINT start = i;
        while (i < length && xd_is_name_char(text[i]))
            i++;
        UINT nlen = i - start;
        while (i < length && xd_is_space(text[i]))
            i++;
        if (!nlen || i >= length || text[i] != '=')
            return (HRESULT)0xC00CE209u;
        i++;
        while (i < length && xd_is_space(text[i]))
            i++;
        if (i >= length || (text[i] != '"' && text[i] != '\''))
            return (HRESULT)0xC00CE209u;
        WCHAR q = text[i++];
        UINT vs = i;
        while (i < length && text[i] != q)
            i++;
        if (i >= length)
            return (HRESULT)0xC00CE209u;
        UINT vlen = i - vs;
        i++;
        const WCHAR *name = text + start;
        if (!((nlen == 7 && !memcmp(name, L"version", 14)) || (nlen == 8 && !memcmp(name, L"encoding", 16)) ||
              (nlen == 10 && !memcmp(name, L"standalone", 20))))
            return (HRESULT)0xC00CE209u;
        XNode *a = xd_node_new(n->doc, XN_ATTRIBUTE, name, nlen, nullptr);
        if (!a)
            return E_OUTOFMEMORY;
        a->value = xd_wcsdup(text + vs, vlen);
        a->length = vlen;
        xd_attr_link(n, a);
    }
    return S_OK;
}

HRESULT xd_set_pi_data(XNode *n, const WCHAR *text, UINT length)
{
    if (n->flags & XF_XMLDECL)
        return set_decl_data(n, text, length);
    xd_node_set_value(n, text, length);
    return S_OK;
}

/* ------------------------------------------------------------------ IUnknown and IDispatch */

static HRESULT STDMETHODCALLTYPE n_qi(XNode *n, REFIID riid, void **out)
{
    return xn_query(n, riid, out);
}

static ULONG STDMETHODCALLTYPE n_addref(XNode *n)
{
    return xn_addref(n);
}

static ULONG STDMETHODCALLTYPE n_release(XNode *n)
{
    return xn_release(n);
}

static HRESULT STDMETHODCALLTYPE n_typeinfocount(XNode *n, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_typeinfo(XNode *n, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE n_ids(XNode *n, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(xd_dispset_for_node(n->type), riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE n_invoke(XNode *n, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                          VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(n, xd_dispset_for_node(n->type), id, riid, lcid, flags, params, result, excep, argerr);
}

/* ------------------------------------------------------------------ IXMLDOMNode */

static HRESULT STDMETHODCALLTYPE n_get_nodeName(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    UINT len;
    const WCHAR *s = node_name(n, &len);
    return xd_ret_string(p, s, len);
}

static BOOL has_value(XNode *n)
{
    return n->type == XN_ATTRIBUTE || n->type == XN_TEXT || n->type == XN_CDATA || n->type == XN_COMMENT ||
           n->type == XN_PI;
}

static HRESULT STDMETHODCALLTYPE n_get_nodeValue(XNode *n, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    if (!has_value(n)) {
        v->vt = VT_NULL;
        return S_FALSE;
    }
    XBuf b = {};
    if (n->type == XN_PI)
        pi_data(n, &b);
    else if (n->value)
        xb_put(&b, n->value, n->length);
    v->vt = VT_BSTR;
    HRESULT hr = xd_ret_buf(&v->bstrVal, &b);
    if (FAILED(hr))
        v->vt = VT_EMPTY;
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_put_nodeValue(XNode *n, VARIANT value)
{
    if (!has_value(n))
        return E_FAIL;
    XBuf b = {};
    if (!xv_to_string(&value, &b)) {
        xb_free(&b);
        return DISP_E_TYPEMISMATCH;
    }
    HRESULT hr = S_OK;
    if (n->type == XN_PI)
        hr = xd_set_pi_data(n, b.s ? b.s : L"", b.length);
    else
        xd_node_set_value(n, b.s ? b.s : L"", b.length);
    xb_free(&b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_get_nodeType(XNode *n, LONG *type)
{
    if (!type)
        return E_INVALIDARG;
    *type = n->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_get_parentNode(XNode *n, void **out)
{
    return xd_ret_node(n->type == XN_ATTRIBUTE ? nullptr : n->parent, out);
}

static HRESULT STDMETHODCALLTYPE n_get_childNodes(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    XList *l = xd_list_new(n, XL_CHILDREN, nullptr, FALSE);
    *out = l;
    return l ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE n_get_firstChild(XNode *n, void **out)
{
    return xd_ret_node(n->first, out);
}

static HRESULT STDMETHODCALLTYPE n_get_lastChild(XNode *n, void **out)
{
    return xd_ret_node(n->last, out);
}

static HRESULT STDMETHODCALLTYPE n_get_previousSibling(XNode *n, void **out)
{
    return xd_ret_node(n->type == XN_ATTRIBUTE ? nullptr : n->prev, out);
}

static HRESULT STDMETHODCALLTYPE n_get_nextSibling(XNode *n, void **out)
{
    return xd_ret_node(n->type == XN_ATTRIBUTE ? nullptr : n->next, out);
}

static HRESULT STDMETHODCALLTYPE n_get_attributes(XNode *n, void **out)
{
    return xd_get_attributes(n, out);
}

static HRESULT STDMETHODCALLTYPE n_insertBefore(XNode *n, XNode *node, VARIANT before, void **old)
{
    if (old)
        *old = nullptr;
    if (!node || !xd_node_is_ours(node))
        return E_INVALIDARG;
    XNode *ref = nullptr;
    if (!xv_is_missing(&before)) {
        ref = xv_to_node(&before);
        if (!ref || ref->doc != n->doc) {
            if (ref)
                xn_release(ref);
            return (HRESULT)XE_INSERT_NOT_FOUND;
        }
    }
    XNode *inserted = nullptr;
    HRESULT hr = xd_insert(n, node, ref, &inserted);
    if (ref)
        xn_release(ref);
    if (FAILED(hr))
        return hr;
    if (old)
        *old = xd_ref(inserted);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_replaceChild(XNode *n, XNode *node, XNode *old, void **out)
{
    if (out)
        *out = nullptr;
    if (!node || !old || !xd_node_is_ours(node) || !xd_node_is_ours(old))
        return E_INVALIDARG;
    if (old->parent != n || old->type == XN_ATTRIBUTE)
        return (HRESULT)XE_NOT_A_CHILD;
    if (node == old) {
        if (out)
            *out = xd_ref(old);
        return S_OK;
    }
    HRESULT hr = xd_check_insert(n, node, old);
    if (FAILED(hr))
        return hr;
    /* old must stay a child until the checks are done. */
    XNode *after = old->next;
    xn_addref(old);
    xd_unlink(old);
    if (after == node)
        after = node->next;
    hr = xd_insert(n, node, after, nullptr);
    if (FAILED(hr)) {
        xd_link(n, old, after);
        xn_release(old);
        return hr;
    }
    if (out)
        *out = old;
    else
        xn_release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_removeChild(XNode *n, XNode *node, void **out)
{
    if (out)
        *out = nullptr;
    if (!node || !xd_node_is_ours(node))
        return E_INVALIDARG;
    HRESULT hr = xd_remove(n, node);
    if (FAILED(hr))
        return hr;
    if (out)
        *out = xd_ref(node);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_appendChild(XNode *n, XNode *node, void **out)
{
    if (out)
        *out = nullptr;
    if (!node || !xd_node_is_ours(node))
        return E_INVALIDARG;
    XNode *inserted = nullptr;
    HRESULT hr = xd_insert(n, node, nullptr, &inserted);
    if (FAILED(hr))
        return hr;
    if (out)
        *out = xd_ref(inserted);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_hasChildNodes(XNode *n, VARIANT_BOOL *has)
{
    if (!has)
        return E_INVALIDARG;
    *has = n->first ? VARIANT_TRUE : VARIANT_FALSE;
    return n->first ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE n_get_ownerDocument(XNode *n, void **out)
{
    return xd_ret_node(n->type == XN_DOCUMENT ? nullptr : &n->doc->node, out);
}

static HRESULT STDMETHODCALLTYPE n_cloneNode(XNode *n, VARIANT_BOOL deep, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (n->type == XN_DOCUMENT) {
        XNode *c = xd_doc_clone((XDoc *)n, deep != 0);
        if (!c)
            return E_OUTOFMEMORY;
        *out = c; /* already referenced by its creation. */
        return S_OK;
    }
    XNode *c = xd_clone(n, n->doc, deep != 0);
    if (!c)
        return E_OUTOFMEMORY;
    return xd_ret_node(c, out);
}

static HRESULT STDMETHODCALLTYPE n_get_nodeTypeString(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    const WCHAR *s = type_names[n->type < XN_TYPES ? n->type : 0];
    return xd_ret_string(p, s, xd_wlen(s));
}

static HRESULT STDMETHODCALLTYPE n_get_text(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    XBuf b = {};
    if (n->type == XN_PI && (n->flags & XF_XMLDECL))
        pi_data(n, &b);
    else
        xd_text(n, &b);
    return xd_ret_buf(p, &b);
}

static HRESULT STDMETHODCALLTYPE n_put_text(XNode *n, BSTR p)
{
    UINT len = p ? SysStringLen(p) : 0;
    if (n->type == XN_PI && (n->flags & XF_XMLDECL))
        return set_decl_data(n, p ? p : L"", len);
    return xd_set_text(n, p ? p : L"", len) ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE n_get_specified(XNode *n, VARIANT_BOOL *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = VARIANT_TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_get_definition(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE n_get_nodeTypedValue(XNode *n, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    if (n->type == XN_DOCUMENT || n->type == XN_FRAGMENT || n->type == XN_DOCTYPE || n->type == XN_ENTITYREF) {
        v->vt = VT_NULL;
        return S_FALSE;
    }
    XBuf b = {};
    xd_text(n, &b);
    v->vt = VT_BSTR;
    HRESULT hr = xd_ret_buf(&v->bstrVal, &b);
    if (FAILED(hr))
        v->vt = VT_EMPTY;
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_put_nodeTypedValue(XNode *n, VARIANT value)
{
    XBuf b = {};
    if (!xv_to_string(&value, &b)) {
        xb_free(&b);
        return DISP_E_TYPEMISMATCH;
    }
    HRESULT hr = xd_set_text(n, b.s ? b.s : L"", b.length) ? S_OK : E_FAIL;
    xb_free(&b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_get_dataType(XNode *n, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    v->vt = VT_NULL;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE n_put_dataType(XNode *n, BSTR p)
{
    if (!p)
        return E_INVALIDARG;
    if (!xd_wcsicmp(p, L"string"))
        return S_OK;
    return (HRESULT)0xC00CE21Cu;
}

static HRESULT STDMETHODCALLTYPE n_get_xml(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    XBuf b = {};
    xd_write(n, &b, xd_indent(n->doc), 0x10FFFF, L"UTF-16");
    return xd_ret_buf(p, &b);
}

static HRESULT STDMETHODCALLTYPE n_transformNode(XNode *n, XNode *style, BSTR *p)
{
    if (p)
        *p = nullptr;
    if (!style || !p)
        return E_INVALIDARG;
    return xs_transform_node(n, style, nullptr, p);
}

static HRESULT select_list(XNode *n, BSTR query, XList **out)
{
    XSet set = {};
    HRESULT hr = xp_select(n, query, &set);
    if (FAILED(hr)) {
        xs_free(&set);
        return hr;
    }
    XList *l = xd_list_new(n, XL_NODES, query, TRUE);
    if (!l) {
        xs_free(&set);
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < set.n; i++)
        xd_list_add_node(l, set.v[i]);
    xs_free(&set);
    *out = l;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_selectNodes(XNode *n, BSTR query, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!query)
        return E_INVALIDARG;
    XList *l;
    HRESULT hr = select_list(n, query, &l);
    if (SUCCEEDED(hr))
        *out = l;
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_selectSingleNode(XNode *n, BSTR query, void **out)
{
    if (!out || !query)
        return E_INVALIDARG;
    *out = nullptr;
    XSet set = {};
    HRESULT hr = xp_select(n, query, &set);
    if (SUCCEEDED(hr)) {
        if (set.n)
            hr = xd_ret_node(set.v[0], out);
        else
            hr = S_FALSE;
    }
    xs_free(&set);
    return hr;
}

static HRESULT STDMETHODCALLTYPE n_get_parsed(XNode *n, VARIANT_BOOL *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = VARIANT_TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE n_get_namespaceURI(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if (n->type != XN_ELEMENT && n->type != XN_ATTRIBUTE)
        return S_FALSE;
    return xd_ret_string(p, n->uri, n->uri ? xd_wlen(n->uri) : 0);
}

static HRESULT STDMETHODCALLTYPE n_get_prefix(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if ((n->type != XN_ELEMENT && n->type != XN_ATTRIBUTE) || !n->local)
        return S_FALSE;
    return xd_ret_string(p, n->qname, (UINT)(n->local - 1));
}

static HRESULT STDMETHODCALLTYPE n_get_baseName(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if (n->type != XN_ELEMENT && n->type != XN_ATTRIBUTE && n->type != XN_PI && n->type != XN_ENTITYREF)
        return S_FALSE;
    return xd_ret_string(p, xd_local(n), xd_local_length(n));
}

static HRESULT STDMETHODCALLTYPE n_transformNodeToObject(XNode *n, XNode *style, VARIANT output)
{
    if (!style)
        return E_INVALIDARG;
    return xs_transform_node(n, style, &output, nullptr);
}

#define NODE_ENTRIES                                                                                                   \
    n_qi, n_addref, n_release, n_typeinfocount, n_typeinfo, n_ids, n_invoke, n_get_nodeName, n_get_nodeValue,          \
        n_put_nodeValue, n_get_nodeType, n_get_parentNode, n_get_childNodes, n_get_firstChild, n_get_lastChild,        \
        n_get_previousSibling, n_get_nextSibling, n_get_attributes, n_insertBefore, n_replaceChild, n_removeChild,     \
        n_appendChild, n_hasChildNodes, n_get_ownerDocument, n_cloneNode, n_get_nodeTypeString, n_get_text,            \
        n_put_text, n_get_specified, n_get_definition, n_get_nodeTypedValue, n_put_nodeTypedValue, n_get_dataType,     \
        n_put_dataType, n_get_xml, n_transformNode, n_selectNodes, n_selectSingleNode, n_get_parsed,                   \
        n_get_namespaceURI, n_get_prefix, n_get_baseName, n_transformNodeToObject

/* ------------------------------------------------------------------ IXMLDOMElement */

static HRESULT STDMETHODCALLTYPE e_get_tagName(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    return xd_ret_string(p, n->qname, xd_atom_len(n->qname));
}

static HRESULT STDMETHODCALLTYPE e_getAttribute(XNode *n, BSTR name, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    if (!name)
        return E_INVALIDARG;
    XNode *a = xd_attr_find(n, name, SysStringLen(name));
    if (!a) {
        v->vt = VT_NULL;
        return S_FALSE;
    }
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocStringLen(a->value ? a->value : L"", a->length);
    if (!v->bstrVal) {
        v->vt = VT_EMPTY;
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* a valid name with at most one colon. */
HRESULT xd_check_new_name(const WCHAR *name, UINT length)
{
    if (!name || !length)
        return (HRESULT)XE_NEED_NAME;
    if (xd_check_name(name, length, TRUE) >= 0)
        return (HRESULT)XE_NAME_HAS_CHAR;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE e_setAttribute(XNode *n, BSTR name, VARIANT value)
{
    UINT nlen = name ? SysStringLen(name) : 0;
    HRESULT hr = xd_check_new_name(name, nlen);
    if (FAILED(hr))
        return hr;
    XBuf b = {};
    if (!xv_to_string(&value, &b)) {
        xb_free(&b);
        return DISP_E_TYPEMISMATCH;
    }
    XNode *a = xd_attr_find(n, name, nlen);
    if (!a) {
        a = xd_node_new(n->doc, XN_ATTRIBUTE, name, nlen, nullptr);
        if (!a) {
            xb_free(&b);
            return E_OUTOFMEMORY;
        }
        /* xmlns attributes belong to the xmlns namespace. */
        if (xd_is_xmlns_attr(a))
            a->uri = xd_intern(n->doc, L"http://www.w3.org/2000/xmlns/", (UINT)-1);
        xd_attr_link(n, a);
    }
    xd_node_set_value(a, b.s ? b.s : L"", b.length);
    xb_free(&b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE e_removeAttribute(XNode *n, BSTR name)
{
    if (!name)
        return E_INVALIDARG;
    XNode *a = xd_attr_find(n, name, SysStringLen(name));
    if (!a)
        return S_FALSE;
    xd_attr_unlink(a);
    xd_free_if_orphan(a);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE e_getAttributeNode(XNode *n, BSTR name, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    return xd_ret_node(xd_attr_find(n, name, SysStringLen(name)), out);
}

static HRESULT STDMETHODCALLTYPE e_setAttributeNode(XNode *n, XNode *attr, void **old)
{
    if (old)
        *old = nullptr;
    if (!attr || !xd_node_is_ours(attr) || attr->type != XN_ATTRIBUTE)
        return E_INVALIDARG;
    if (attr->parent == n)
        return S_OK;
    if (attr->parent)
        return (HRESULT)XE_ATTRIBUTE_IN_USE;
    XNode *use = attr;
    if (attr->doc != n->doc) {
        use = xd_clone(attr, n->doc, FALSE);
        if (!use)
            return E_OUTOFMEMORY;
    }
    XNode *existing = xd_attr_find(n, use->qname, xd_atom_len(use->qname));
    if (existing) {
        xd_attr_unlink(existing);
        if (old)
            *old = xd_ref(existing);
        xd_free_if_orphan(existing);
    }
    xd_attr_link(n, use);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE e_removeAttributeNode(XNode *n, XNode *attr, void **old)
{
    if (old)
        *old = nullptr;
    if (!attr || !xd_node_is_ours(attr))
        return E_INVALIDARG;
    if (attr->parent != n || attr->type != XN_ATTRIBUTE)
        return E_INVALIDARG;
    xd_attr_unlink(attr);
    if (old)
        *old = xd_ref(attr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE e_getElementsByTagName(XNode *n, BSTR name, void **out)
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

static HRESULT STDMETHODCALLTYPE e_normalize(XNode *n)
{
    xd_normalize(n);
    return S_OK;
}

/* ------------------------------------------------------------------ IXMLDOMAttribute */

static HRESULT STDMETHODCALLTYPE a_get_name(XNode *n, BSTR *p)
{
    return e_get_tagName(n, p);
}

static HRESULT STDMETHODCALLTYPE a_get_value(XNode *n, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocStringLen(n->value ? n->value : L"", n->length);
    if (!v->bstrVal) {
        v->vt = VT_EMPTY;
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE a_put_value(XNode *n, VARIANT value)
{
    return n_put_nodeValue(n, value);
}

/* ------------------------------------------------------------------ character data */

static HRESULT STDMETHODCALLTYPE c_get_data(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    return xd_ret_string(p, n->value ? n->value : L"", n->length);
}

static HRESULT STDMETHODCALLTYPE c_put_data(XNode *n, BSTR p)
{
    xd_node_set_value(n, p ? p : L"", p ? SysStringLen(p) : 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_get_length(XNode *n, LONG *length)
{
    if (!length)
        return E_INVALIDARG;
    *length = (LONG)n->length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_substringData(XNode *n, LONG offset, LONG count, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if (offset < 0 || (UINT)offset > n->length || count < 0)
        return (HRESULT)XE_BAD_OFFSET;
    UINT take = (UINT)count;
    if (take > n->length - (UINT)offset)
        take = n->length - (UINT)offset;
    return xd_ret_string(p, (n->value ? n->value : L"") + offset, take);
}

static HRESULT splice(XNode *n, LONG offset, LONG count, const WCHAR *text, UINT tlen)
{
    if (offset < 0 || (UINT)offset > n->length || count < 0)
        return (HRESULT)XE_BAD_OFFSET;
    UINT cut = (UINT)count;
    if (cut > n->length - (UINT)offset)
        cut = n->length - (UINT)offset;
    UINT total = n->length - cut + tlen;
    WCHAR *s = xd_alloc_raw(((SIZE_T)total + 1) * sizeof(WCHAR));
    if (!s)
        return E_OUTOFMEMORY;
    if (offset)
        memcpy(s, n->value, (SIZE_T)offset * sizeof(WCHAR));
    if (tlen)
        memcpy(s + offset, text, (SIZE_T)tlen * sizeof(WCHAR));
    UINT rest = n->length - (UINT)offset - cut;
    if (rest)
        memcpy(s + offset + tlen, n->value + offset + cut, (SIZE_T)rest * sizeof(WCHAR));
    s[total] = 0;
    xd_node_set_value_owned(n, s, total);
    xd_touch(n->doc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE c_appendData(XNode *n, BSTR p)
{
    return splice(n, (LONG)n->length, 0, p ? p : L"", p ? SysStringLen(p) : 0);
}

static HRESULT STDMETHODCALLTYPE c_insertData(XNode *n, LONG offset, BSTR p)
{
    return splice(n, offset, 0, p ? p : L"", p ? SysStringLen(p) : 0);
}

static HRESULT STDMETHODCALLTYPE c_deleteData(XNode *n, LONG offset, LONG count)
{
    return splice(n, offset, count, L"", 0);
}

static HRESULT STDMETHODCALLTYPE c_replaceData(XNode *n, LONG offset, LONG count, BSTR p)
{
    return splice(n, offset, count, p ? p : L"", p ? SysStringLen(p) : 0);
}

static HRESULT STDMETHODCALLTYPE c_splitText(XNode *n, LONG offset, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (offset < 0 || (UINT)offset > n->length)
        return (HRESULT)XE_BAD_OFFSET;
    XNode *second = xd_node_new(n->doc, n->type, nullptr, 0, nullptr);
    if (!second)
        return E_OUTOFMEMORY;
    UINT rest = n->length - (UINT)offset;
    second->value = xd_wcsdup(n->value ? n->value + offset : L"", rest);
    second->length = rest;
    if (n->parent)
        xd_link(n->parent, second, n->next);
    splice(n, offset, (LONG)rest, L"", 0);
    return xd_ret_node(second, out);
}

#define CHAR_ENTRIES                                                                                                   \
    c_get_data, c_put_data, c_get_length, c_substringData, c_appendData, c_insertData, c_deleteData, c_replaceData

/* ------------------------------------------------------------------ processing instruction, document type */

static HRESULT STDMETHODCALLTYPE p_get_target(XNode *n, BSTR *p)
{
    return e_get_tagName(n, p);
}

static HRESULT STDMETHODCALLTYPE p_get_data(XNode *n, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    XBuf b = {};
    pi_data(n, &b);
    return xd_ret_buf(p, &b);
}

static HRESULT STDMETHODCALLTYPE p_put_data(XNode *n, BSTR p)
{
    return xd_set_pi_data(n, p ? p : L"", p ? SysStringLen(p) : 0);
}

static HRESULT STDMETHODCALLTYPE t_get_name(XNode *n, BSTR *p)
{
    return e_get_tagName(n, p);
}

static HRESULT STDMETHODCALLTYPE t_get_entities(XNode *n, void **out)
{
    if (out)
        *out = nullptr;
    return out ? S_FALSE : E_INVALIDARG;
}

/* ------------------------------------------------------------------ the tables */

const XNodeVtbl xd_vtbl_node = {NODE_ENTRIES};

const XElementVtbl xd_vtbl_element = {NODE_ENTRIES,      e_get_tagName,        e_getAttribute,
                                      e_setAttribute,    e_removeAttribute,    e_getAttributeNode,
                                      e_setAttributeNode, e_removeAttributeNode, e_getElementsByTagName,
                                      e_normalize};

const XAttributeVtbl xd_vtbl_attribute = {NODE_ENTRIES, a_get_name, a_get_value, a_put_value};

const XTextVtbl xd_vtbl_text = {NODE_ENTRIES, CHAR_ENTRIES, c_splitText};
const XTextVtbl xd_vtbl_cdata = {NODE_ENTRIES, CHAR_ENTRIES, c_splitText};
const XCommentVtbl xd_vtbl_comment = {NODE_ENTRIES, CHAR_ENTRIES};

const XPIVtbl xd_vtbl_pi = {NODE_ENTRIES, p_get_target, p_get_data, p_put_data};

const XDocTypeVtbl xd_vtbl_doctype = {NODE_ENTRIES, t_get_name, t_get_entities, t_get_entities};

const XNodeVtbl xd_vtbl_fragment = {NODE_ENTRIES};
const XNodeVtbl xd_vtbl_entityref = {NODE_ENTRIES};

/* ------------------------------------------------------------------ interface queries */

static BOOL dom_iid(REFIID riid)
{
    return IsEqualIID(riid, &IID_IXMLDOMNode) || IsEqualIID(riid, &IID_IXMLDOMDocument) ||
           IsEqualIID(riid, &IID_IXMLDOMDocument2) || IsEqualIID(riid, &IID_IXMLDOMDocument3) ||
           IsEqualIID(riid, &IID_IXMLDOMElement) || IsEqualIID(riid, &IID_IXMLDOMAttribute) ||
           IsEqualIID(riid, &IID_IXMLDOMText) || IsEqualIID(riid, &IID_IXMLDOMComment) ||
           IsEqualIID(riid, &IID_IXMLDOMCDATASection) || IsEqualIID(riid, &IID_IXMLDOMCharacterData) ||
           IsEqualIID(riid, &IID_IXMLDOMProcessingInstruction) || IsEqualIID(riid, &IID_IXMLDOMDocumentType) ||
           IsEqualIID(riid, &IID_IXMLDOMDocumentFragment) || IsEqualIID(riid, &IID_IXMLDOMEntityReference) ||
           IsEqualIID(riid, &IID_IXMLDOMNodeList) || IsEqualIID(riid, &IID_IXMLDOMNamedNodeMap) ||
           IsEqualIID(riid, &IID_IXMLDOMParseError) || IsEqualIID(riid, &IID_IXMLDOMSelection);
}

static HRESULT STDMETHODCALLTYPE aux_qi(XAux *a, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, a->safety ? &IID_IObjectSafety : &IID_ISupportErrorInfo)) {
        InterlockedIncrement(&a->refs);
        *out = a;
        return S_OK;
    }
    return xn_query(a->node, riid, out);
}

static ULONG STDMETHODCALLTYPE aux_addref(XAux *a)
{
    return (ULONG)InterlockedIncrement(&a->refs);
}

static ULONG STDMETHODCALLTYPE aux_release(XAux *a)
{
    LONG refs = InterlockedDecrement(&a->refs);
    if (refs == 0) {
        xn_release(a->node);
        xd_free(a);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE aux_supports_err(XAux *a, void *riid)
{
    return dom_iid((REFIID)riid) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE aux_get_safety(XAux *a, void *riid, ULONG *supported, ULONG *enabled)
{
    (void)riid;
    if (supported)
        *supported = 3;
    if (enabled)
        *enabled = 3;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE aux_set_safety(XAux *a, void *riid, ULONG mask, ULONG options)
{
    return S_OK;
}

typedef struct XAuxErrVtbl {
    MIME_IUNKNOWN_METHODS(XAux)
    HRESULT(STDMETHODCALLTYPE *InterfaceSupportsErrorInfo)(XAux *This, void *riid);
} XAuxErrVtbl;

typedef struct XAuxSafeVtbl {
    MIME_IUNKNOWN_METHODS(XAux)
    HRESULT(STDMETHODCALLTYPE *GetInterfaceSafetyOptions)(XAux *This, void *riid, ULONG *supported, ULONG *enabled);
    HRESULT(STDMETHODCALLTYPE *SetInterfaceSafetyOptions)(XAux *This, void *riid, ULONG mask, ULONG options);
} XAuxSafeVtbl;

static const XAuxErrVtbl aux_err_vtbl = {aux_qi, aux_addref, aux_release, aux_supports_err};
static const XAuxSafeVtbl aux_safe_vtbl = {aux_qi, aux_addref, aux_release, aux_get_safety, aux_set_safety};

HRESULT xn_query(XNode *n, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BOOL ok = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IXMLDOMNode);
    if (!ok) {
        switch (n->type) {
        case XN_ELEMENT:
            ok = IsEqualIID(riid, &IID_IXMLDOMElement);
            break;
        case XN_ATTRIBUTE:
            ok = IsEqualIID(riid, &IID_IXMLDOMAttribute);
            break;
        case XN_TEXT:
            ok = IsEqualIID(riid, &IID_IXMLDOMText) || IsEqualIID(riid, &IID_IXMLDOMCharacterData);
            break;
        case XN_CDATA:
            ok = IsEqualIID(riid, &IID_IXMLDOMCDATASection) || IsEqualIID(riid, &IID_IXMLDOMText) ||
                 IsEqualIID(riid, &IID_IXMLDOMCharacterData);
            break;
        case XN_COMMENT:
            ok = IsEqualIID(riid, &IID_IXMLDOMComment) || IsEqualIID(riid, &IID_IXMLDOMCharacterData);
            break;
        case XN_PI:
            ok = IsEqualIID(riid, &IID_IXMLDOMProcessingInstruction);
            break;
        case XN_DOCUMENT:
            ok = IsEqualIID(riid, &IID_IXMLDOMDocument) || IsEqualIID(riid, &IID_IXMLDOMDocument2) ||
                 IsEqualIID(riid, &IID_IXMLDOMDocument3);
            break;
        case XN_FRAGMENT:
            ok = IsEqualIID(riid, &IID_IXMLDOMDocumentFragment);
            break;
        case XN_DOCTYPE:
            ok = IsEqualIID(riid, &IID_IXMLDOMDocumentType);
            break;
        case XN_ENTITYREF:
            ok = IsEqualIID(riid, &IID_IXMLDOMEntityReference);
            break;
        }
    }
    if (ok) {
        xn_addref(n);
        *out = n;
        return S_OK;
    }
    BOOL err = IsEqualIID(riid, &IID_ISupportErrorInfo);
    if (err || IsEqualIID(riid, &IID_IObjectSafety)) {
        XAux *a = xd_alloc(sizeof(XAux));
        if (!a)
            return E_OUTOFMEMORY;
        a->vtbl = err ? (const void *)&aux_err_vtbl : (const void *)&aux_safe_vtbl;
        a->refs = 1;
        a->node = n;
        a->safety = !err;
        xn_addref(n);
        *out = a;
        return S_OK;
    }
    return E_NOINTERFACE;
}
