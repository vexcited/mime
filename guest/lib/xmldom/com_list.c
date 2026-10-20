/* node lists, selections, named node maps, the enumerator, the implementation object and parse error objects. */
#include <xmldom/xmldom.h>

#define L(p) ((XList *)(p))

/* ------------------------------------------------------------------ lists */

XList *xd_list_new(XNode *base, UINT kind, const WCHAR *name, BOOL selection)
{
    XList *l = xd_alloc(sizeof(XList));
    if (!l)
        return nullptr;
    l->vtbl = kind == XL_ATTRIBUTES ? (const void *)&xd_vtbl_map
                                    : selection ? (const void *)&xd_vtbl_selection : (const void *)&xd_vtbl_list;
    l->refs = 1;
    l->base = base;
    xn_addref(base);
    l->kind = kind;
    l->selection = selection;
    if (name)
        l->name = xd_wcsdup(name, (UINT)-1);
    InterlockedIncrement(&xd_objects);
    return l;
}

void xd_list_add_node(XList *l, XNode *n)
{
    if (l->n == l->cap) {
        UINT cap = l->cap ? l->cap * 2 : 16;
        XNode **v = xd_realloc(l->v, (SIZE_T)cap * sizeof(XNode *));
        if (!v)
            return;
        l->v = v;
        l->cap = cap;
    }
    xn_addref(n);
    l->v[l->n++] = n;
}

static void list_free(XList *l)
{
    if (l->kind == XL_NODES)
        for (UINT i = 0; i < l->n; i++)
            xn_release(l->v[i]);
    xd_free(l->v);
    xd_free(l->name);
    xn_release(l->base);
    xd_free(l);
    InterlockedDecrement(&xd_objects);
}

static BOOL tag_matches(XNode *n, const WCHAR *name, UINT length, BOOL any)
{
    if (n->type != XN_ELEMENT)
        return FALSE;
    return any || (xd_atom_len(n->qname) == length && !memcmp(n->qname, name, (SIZE_T)length * sizeof(WCHAR)));
}

/* brings the cached part of a list up to date with its document. */
BOOL xd_list_refresh(XList *l)
{
    XDoc *doc = l->base->doc;
    if (l->kind == XL_NODES || l->version == doc->version + 1)
        return TRUE;
    if (l->kind == XL_CHILDREN) {
        l->n = xd_child_count(l->base);
        l->cache_node = nullptr;
    } else if (l->kind == XL_TAGNAME) {
        l->n = 0;
        UINT length = xd_wlen(l->name);
        BOOL any = length == 1 && l->name[0] == '*';
        XNode *base = l->base, *n = base->first;
        while (n) {
            if (tag_matches(n, l->name, length, any)) {
                if (l->n == l->cap) {
                    UINT cap = l->cap ? l->cap * 2 : 16;
                    XNode **v = xd_realloc(l->v, (SIZE_T)cap * sizeof(XNode *));
                    if (!v)
                        return FALSE;
                    l->v = v;
                    l->cap = cap;
                }
                l->v[l->n++] = n;
            }
            if (n->first) {
                n = n->first;
                continue;
            }
            while (n != base && !n->next)
                n = n->parent;
            if (n == base)
                break;
            n = n->next;
        }
    }
    l->version = doc->version + 1;
    return TRUE;
}

UINT xd_list_count(XList *l)
{
    if (l->kind == XL_ATTRIBUTES) {
        UINT n = 0;
        for (XNode *a = l->base->attrs; a; a = a->next)
            n++;
        return n;
    }
    xd_list_refresh(l);
    return l->n;
}

XNode *xd_list_item(XList *l, UINT index)
{
    if (l->kind == XL_ATTRIBUTES) {
        XNode *a = l->base->attrs;
        while (a && index--)
            a = a->next;
        return a;
    }
    if (!xd_list_refresh(l) || index >= l->n)
        return nullptr;
    if (l->kind != XL_CHILDREN)
        return l->v[index];
    /* children are found from the last place looked, which suits walking a list in order. */
    XNode *n;
    UINT at;
    if (l->cache_node && index >= l->cache_index) {
        n = l->cache_node;
        at = l->cache_index;
    } else if (l->cache_node && index + index > l->cache_index) {
        n = l->cache_node;
        at = l->cache_index;
        while (at > index) {
            n = n->prev;
            at--;
        }
        l->cache_node = n;
        l->cache_index = at;
        return n;
    } else {
        n = l->base->first;
        at = 0;
    }
    while (at < index && n) {
        n = n->next;
        at++;
    }
    l->cache_node = n;
    l->cache_index = at;
    return n;
}

static ULONG STDMETHODCALLTYPE l_addref(XList *l)
{
    return (ULONG)InterlockedIncrement(&l->refs);
}

static ULONG STDMETHODCALLTYPE l_release(XList *l)
{
    LONG refs = InterlockedDecrement(&l->refs);
    if (refs == 0)
        list_free(l);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE l_qi(XList *l, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BOOL ok = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch);
    if (l->kind == XL_ATTRIBUTES)
        ok = ok || IsEqualIID(riid, &IID_IXMLDOMNamedNodeMap);
    else
        ok = ok || IsEqualIID(riid, &IID_IXMLDOMNodeList) || (l->selection && IsEqualIID(riid, &IID_IXMLDOMSelection));
    if (!ok)
        return E_NOINTERFACE;
    InterlockedIncrement(&l->refs);
    *out = l;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE l_typeinfocount(XList *l, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE l_typeinfo(XList *l, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static const XDispSet *list_dispset(XList *l)
{
    return l->kind == XL_ATTRIBUTES ? &xd_dispset_map : l->selection ? &xd_dispset_selection : &xd_dispset_list;
}

static HRESULT STDMETHODCALLTYPE l_ids(XList *l, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(list_dispset(l), riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE l_invoke(XList *l, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                          VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(l, list_dispset(l), id, riid, lcid, flags, params, result, excep, argerr);
}

static HRESULT STDMETHODCALLTYPE l_get_item(XList *l, LONG index, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (index < 0)
        return S_FALSE;
    return xd_ret_node(xd_list_item(l, (UINT)index), out);
}

static HRESULT STDMETHODCALLTYPE l_get_length(XList *l, LONG *length)
{
    if (!length)
        return E_INVALIDARG;
    *length = (LONG)xd_list_count(l);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE l_nextNode(XList *l, void **out)
{
    if (!out)
        return E_INVALIDARG;
    XNode *n = xd_list_item(l, l->cursor);
    if (n)
        l->cursor++;
    return xd_ret_node(n, out);
}

static HRESULT STDMETHODCALLTYPE l_reset(XList *l)
{
    l->cursor = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE l_newEnum(XList *l, IUnknown **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_create_enum((IUnknown *)l, (void **)out);
}

const XListVtbl xd_vtbl_list = {l_qi, l_addref, l_release, l_typeinfocount, l_typeinfo, l_ids, l_invoke,
                                l_get_item, l_get_length, l_nextNode, l_reset, l_newEnum};

/* ------------------------------------------------------------------ selections */

static HRESULT STDMETHODCALLTYPE s_get_expr(XList *l, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    return xd_ret_string(p, l->name, l->name ? xd_wlen(l->name) : 0);
}

static HRESULT STDMETHODCALLTYPE s_put_expr(XList *l, BSTR p)
{
    if (!p)
        return E_INVALIDARG;
    XSet set = {};
    HRESULT hr = xp_select(l->base, p, &set);
    if (FAILED(hr)) {
        xs_free(&set);
        return hr;
    }
    for (UINT i = 0; i < l->n; i++)
        xn_release(l->v[i]);
    l->n = 0;
    for (UINT i = 0; i < set.n; i++)
        xd_list_add_node(l, set.v[i]);
    xs_free(&set);
    xd_free(l->name);
    l->name = xd_wcsdup(p, (UINT)-1);
    l->cursor = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_get_context(XList *l, void **out)
{
    return xd_ret_node(l->base, out);
}

static HRESULT STDMETHODCALLTYPE s_putref_context(XList *l, XNode *node)
{
    if (!node || !xd_node_is_ours(node))
        return E_INVALIDARG;
    xn_addref(node);
    xn_release(l->base);
    l->base = node;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_peekNode(XList *l, void **out)
{
    if (!out)
        return E_INVALIDARG;
    return xd_ret_node(xd_list_item(l, l->cursor), out);
}

static HRESULT STDMETHODCALLTYPE s_matches(XList *l, XNode *node, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    for (UINT i = 0; i < l->n; i++)
        if (l->v[i] == node)
            return xd_ret_node(node, out);
    return S_FALSE;
}

static void detach(XNode *n)
{
    if (n->type == XN_ATTRIBUTE)
        xd_attr_unlink(n);
    else
        xd_unlink(n);
}

static HRESULT STDMETHODCALLTYPE s_removeNext(XList *l, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (l->cursor >= l->n)
        return S_FALSE;
    XNode *n = l->v[l->cursor];
    detach(n);
    *out = xd_ref(n);
    xn_release(n);
    memmove(l->v + l->cursor, l->v + l->cursor + 1, (SIZE_T)(l->n - l->cursor - 1) * sizeof(XNode *));
    l->n--;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_removeAll(XList *l)
{
    for (UINT i = 0; i < l->n; i++) {
        detach(l->v[i]);
        xn_release(l->v[i]);
    }
    l->n = 0;
    l->cursor = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_clone(XList *l, void **out)
{
    if (!out)
        return E_INVALIDARG;
    XList *copy = xd_list_new(l->base, XL_NODES, l->name, TRUE);
    if (!copy)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < l->n; i++)
        xd_list_add_node(copy, l->v[i]);
    *out = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_getProperty(XList *l, BSTR name, VARIANT *v)
{
    if (!v)
        return E_INVALIDARG;
    VariantInit(v);
    if (!name)
        return E_INVALIDARG;
    if (!xd_wcsicmp(name, L"SelectionLanguage")) {
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString((l->base->doc->flags & XDF_XPATH) ? L"XPath" : L"XSLPattern");
        return v->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (!xd_wcsicmp(name, L"SelectionNamespaces")) {
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(l->base->doc->sel_ns_text ? l->base->doc->sel_ns_text : L"");
        return v->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    return (HRESULT)XE_BAD_PROPERTY_NAME;
}

static HRESULT STDMETHODCALLTYPE s_setProperty(XList *l, BSTR name, VARIANT value)
{
    if (!name)
        return E_INVALIDARG;
    if (!xd_wcsicmp(name, L"SelectionLanguage") || !xd_wcsicmp(name, L"SelectionNamespaces"))
        return S_OK;
    return (HRESULT)XE_BAD_PROPERTY_NAME;
}

const XSelectionVtbl xd_vtbl_selection = {
    l_qi,        l_addref,    l_release,      l_typeinfocount, l_typeinfo,   l_ids,           l_invoke,
    l_get_item,  l_get_length, l_nextNode,    l_reset,         l_newEnum,    s_get_expr,      s_put_expr,
    s_get_context, s_putref_context, s_peekNode, s_matches,    s_removeNext, s_removeAll,     s_clone,
    s_getProperty, s_setProperty};

/* ------------------------------------------------------------------ named node maps */

HRESULT xd_get_attributes(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (n->type != XN_ELEMENT && !(n->type == XN_PI && (n->flags & XF_XMLDECL)))
        return S_FALSE;
    XList *l = xd_list_new(n, XL_ATTRIBUTES, nullptr, FALSE);
    *out = l;
    return l ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE m_getNamedItem(XList *l, BSTR name, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    return xd_ret_node(xd_attr_find(l->base, name, SysStringLen(name)), out);
}

static HRESULT STDMETHODCALLTYPE m_setNamedItem(XList *l, XNode *node, void **old)
{
    if (old)
        *old = nullptr;
    if (!node || !xd_node_is_ours(node) || node->type != XN_ATTRIBUTE)
        return E_INVALIDARG;
    XNode *el = l->base;
    if (node->parent == el)
        return S_FALSE;
    if (node->parent)
        return (HRESULT)XE_ATTRIBUTE_IN_USE;
    XNode *use = node;
    if (node->doc != el->doc) {
        use = xd_clone(node, el->doc, FALSE);
        if (!use)
            return E_OUTOFMEMORY;
    }
    XNode *existing = xd_attr_find(el, use->qname, xd_atom_len(use->qname));
    if (existing) {
        xd_attr_unlink(existing);
        if (old)
            *old = xd_ref(existing);
        xd_free_if_orphan(existing);
    }
    xd_attr_link(el, use);
    return existing ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE m_removeNamedItem(XList *l, BSTR name, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    XNode *a = xd_attr_find(l->base, name, SysStringLen(name));
    if (!a)
        return S_FALSE;
    xd_attr_unlink(a);
    *out = xd_ref(a);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE m_getQualifiedItem(XList *l, BSTR name, BSTR uri, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    return xd_ret_node(xd_attr_find_ns(l->base, name, SysStringLen(name), uri), out);
}

static HRESULT STDMETHODCALLTYPE m_removeQualifiedItem(XList *l, BSTR name, BSTR uri, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    XNode *a = xd_attr_find_ns(l->base, name, SysStringLen(name), uri);
    if (!a)
        return S_FALSE;
    xd_attr_unlink(a);
    *out = xd_ref(a);
    return S_OK;
}

const XMapVtbl xd_vtbl_map = {l_qi,           l_addref,        l_release,      l_typeinfocount,      l_typeinfo,
                              l_ids,          l_invoke,        m_getNamedItem, m_setNamedItem,       m_removeNamedItem,
                              l_get_item,     l_get_length,    m_getQualifiedItem, m_removeQualifiedItem,
                              l_nextNode,     l_reset,         l_newEnum};

/* ------------------------------------------------------------------ enumerators */

static HRESULT STDMETHODCALLTYPE en_qi(XEnum *e, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumVARIANT))
        return E_NOINTERFACE;
    InterlockedIncrement(&e->refs);
    *out = e;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE en_addref(XEnum *e)
{
    return (ULONG)InterlockedIncrement(&e->refs);
}

static ULONG STDMETHODCALLTYPE en_release(XEnum *e)
{
    LONG refs = InterlockedDecrement(&e->refs);
    if (refs == 0) {
        e->source->lpVtbl->Release(e->source);
        xd_free(e);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE en_next(XEnum *e, ULONG count, VARIANT *values, ULONG *fetched)
{
    ULONG got = 0;
    if (fetched)
        *fetched = 0;
    if (!values && count)
        return E_INVALIDARG;
    XList *l = (XList *)e->source;
    for (; got < count; got++) {
        XNode *n = xd_list_item(l, e->pos);
        if (!n)
            break;
        e->pos++;
        VariantInit(&values[got]);
        values[got].vt = VT_DISPATCH;
        values[got].pdispVal = (IDispatch *)xd_ref(n);
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE en_skip(XEnum *e, ULONG count)
{
    XList *l = (XList *)e->source;
    UINT total = xd_list_count(l);
    e->pos += count;
    if (e->pos > total) {
        e->pos = total;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_reset(XEnum *e)
{
    e->pos = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_clone(XEnum *e, void **out)
{
    if (!out)
        return E_POINTER;
    HRESULT hr = xd_create_enum(e->source, out);
    if (SUCCEEDED(hr))
        ((XEnum *)*out)->pos = e->pos;
    return hr;
}

const XEnumVtbl xd_vtbl_enum = {en_qi, en_addref, en_release, en_next, en_skip, en_reset, en_clone};

HRESULT xd_create_enum(IUnknown *source, void **out)
{
    XEnum *e = xd_alloc(sizeof(XEnum));
    if (!e)
        return E_OUTOFMEMORY;
    e->vtbl = &xd_vtbl_enum;
    e->refs = 1;
    e->source = source;
    source->lpVtbl->AddRef(source);
    InterlockedIncrement(&xd_objects);
    *out = e;
    return S_OK;
}

/* ------------------------------------------------------------------ the implementation object */

static HRESULT STDMETHODCALLTYPE im_qi(XImpl *o, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) &&
        !IsEqualIID(riid, &IID_IXMLDOMImplementation))
        return E_NOINTERFACE;
    InterlockedIncrement(&o->refs);
    *out = o;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE im_addref(XImpl *o)
{
    return (ULONG)InterlockedIncrement(&o->refs);
}

static ULONG STDMETHODCALLTYPE im_release(XImpl *o)
{
    LONG refs = InterlockedDecrement(&o->refs);
    if (refs == 0) {
        xd_free(o);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE im_typeinfocount(XImpl *o, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE im_typeinfo(XImpl *o, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE im_ids(XImpl *o, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(&xd_dispset_impl, riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE im_invoke(XImpl *o, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(o, &xd_dispset_impl, id, riid, lcid, flags, params, result, excep, argerr);
}

static HRESULT STDMETHODCALLTYPE im_hasFeature(XImpl *o, BSTR feature, BSTR version, VARIANT_BOOL *has)
{
    if (!has)
        return E_INVALIDARG;
    *has = VARIANT_FALSE;
    if (!feature)
        return S_OK;
    BOOL known = !xd_wcsicmp(feature, L"XML") || !xd_wcsicmp(feature, L"DOM") || !xd_wcsicmp(feature, L"MS-DOM");
    BOOL version_ok = !version || !*version || !wcscmp(version, L"1.0");
    if (known && version_ok)
        *has = VARIANT_TRUE;
    return S_OK;
}

const XImplVtbl xd_vtbl_impl = {im_qi, im_addref, im_release, im_typeinfocount, im_typeinfo, im_ids, im_invoke,
                                im_hasFeature};

HRESULT xd_create_implementation(void **out)
{
    XImpl *o = xd_alloc(sizeof(XImpl));
    if (!o)
        return E_OUTOFMEMORY;
    o->vtbl = &xd_vtbl_impl;
    o->refs = 1;
    InterlockedIncrement(&xd_objects);
    *out = o;
    return S_OK;
}

/* ------------------------------------------------------------------ parse errors */

static HRESULT STDMETHODCALLTYPE er_qi(XErrObj *o, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) &&
        !IsEqualIID(riid, &IID_IXMLDOMParseError) && !IsEqualIID(riid, &IID_IXMLDOMParseError2))
        return E_NOINTERFACE;
    InterlockedIncrement(&o->refs);
    *out = o;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE er_addref(XErrObj *o)
{
    return (ULONG)InterlockedIncrement(&o->refs);
}

static ULONG STDMETHODCALLTYPE er_release(XErrObj *o)
{
    LONG refs = InterlockedDecrement(&o->refs);
    if (refs == 0) {
        xd_err_free(&o->e);
        xd_free(o);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE er_typeinfocount(XErrObj *o, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE er_typeinfo(XErrObj *o, UINT index, LCID lcid, ITypeInfo **info)
{
    if (info)
        *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE er_ids(XErrObj *o, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    return xd_disp_names(&xd_dispset_error, riid, names, count, lcid, ids);
}

static HRESULT STDMETHODCALLTYPE er_invoke(XErrObj *o, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                           DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    return xd_disp_invoke(o, &xd_dispset_error, id, riid, lcid, flags, params, result, excep, argerr);
}

/* an error that did not happen answers false for all its parts. */
static HRESULT STDMETHODCALLTYPE er_get_errorCode(XErrObj *o, LONG *code)
{
    if (!code)
        return E_INVALIDARG;
    *code = o->e.code;
    return o->e.code ? S_OK : S_FALSE;
}

static HRESULT er_string(XErrObj *o, BSTR *p, const WCHAR *s)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if (!o->e.code || !s)
        return S_FALSE;
    return xd_ret_string(p, s, xd_wlen(s));
}

static HRESULT STDMETHODCALLTYPE er_get_url(XErrObj *o, BSTR *p)
{
    return er_string(o, p, o->e.url);
}

static HRESULT STDMETHODCALLTYPE er_get_reason(XErrObj *o, BSTR *p)
{
    return er_string(o, p, o->e.reason);
}

static HRESULT STDMETHODCALLTYPE er_get_srcText(XErrObj *o, BSTR *p)
{
    return er_string(o, p, o->e.src);
}

static HRESULT er_long(XErrObj *o, LONG *p, LONG value)
{
    if (!p)
        return E_INVALIDARG;
    *p = o->e.code ? value : 0;
    return o->e.code ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE er_get_line(XErrObj *o, LONG *p)
{
    return er_long(o, p, o->e.line);
}

static HRESULT STDMETHODCALLTYPE er_get_linepos(XErrObj *o, LONG *p)
{
    return er_long(o, p, o->e.linepos);
}

static HRESULT STDMETHODCALLTYPE er_get_filepos(XErrObj *o, LONG *p)
{
    return er_long(o, p, o->e.filepos);
}

static HRESULT STDMETHODCALLTYPE er_get_errorXPath(XErrObj *o, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE er_get_allErrors(XErrObj *o, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE er_errorParameters(XErrObj *o, LONG index, BSTR *p)
{
    if (!p)
        return E_INVALIDARG;
    *p = nullptr;
    if (index < 0 || (UINT)index >= o->e.params)
        return (HRESULT)0xC00CE226u;
    return xd_ret_string(p, o->e.param[index], xd_wlen(o->e.param[index]));
}

static HRESULT STDMETHODCALLTYPE er_get_errorParametersCount(XErrObj *o, LONG *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = (LONG)o->e.params;
    return S_OK;
}

const XErrVtbl xd_vtbl_error = {er_qi,         er_addref,       er_release,       er_typeinfocount, er_typeinfo,
                                er_ids,        er_invoke,       er_get_errorCode, er_get_url,       er_get_reason,
                                er_get_srcText, er_get_line,    er_get_linepos,   er_get_filepos,   er_get_errorXPath,
                                er_get_allErrors, er_errorParameters, er_get_errorParametersCount};

HRESULT xd_create_error_object(const XErr *e, void **out)
{
    XErrObj *o = xd_alloc(sizeof(XErrObj));
    if (!o)
        return E_OUTOFMEMORY;
    o->vtbl = &xd_vtbl_error;
    o->refs = 1;
    xd_err_copy(&o->e, e);
    InterlockedIncrement(&xd_objects);
    *out = o;
    return S_OK;
}
