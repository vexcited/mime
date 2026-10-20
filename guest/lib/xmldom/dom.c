/* nodes and documents: allocation, the tree, reference counts and what a subtree is worth. */
#include <xmldom/xmldom.h>

static CRITICAL_SECTION g_lock;
LONG xd_objects;

void xd_init(void)
{
    InitializeCriticalSection(&g_lock);
    xd_doc_vtbl_init();
}

const void *xd_vtbl_for(UINT type)
{
    switch (type) {
    case XN_ELEMENT:
        return &xd_vtbl_element;
    case XN_ATTRIBUTE:
        return &xd_vtbl_attribute;
    case XN_TEXT:
        return &xd_vtbl_text;
    case XN_CDATA:
        return &xd_vtbl_cdata;
    case XN_COMMENT:
        return &xd_vtbl_comment;
    case XN_PI:
        return &xd_vtbl_pi;
    case XN_DOCUMENT:
        return &xd_vtbl_document;
    case XN_DOCTYPE:
        return &xd_vtbl_doctype;
    case XN_FRAGMENT:
        return &xd_vtbl_fragment;
    case XN_ENTITYREF:
        return &xd_vtbl_entityref;
    }
    return &xd_vtbl_node;
}

/* ------------------------------------------------------------------ names and values */

static UINT16 local_offset(const WCHAR *qname, UINT length)
{
    for (UINT i = 1; i + 1 < length; i++)
        if (qname[i] == ':')
            return (UINT16)(i + 1);
    return 0;
}

void xd_node_set_name(XNode *n, const WCHAR *qname, UINT qlen, const WCHAR *uri)
{
    n->qname = nullptr;
    n->uri = nullptr;
    n->local = 0;
    if (qname) {
        if (qlen == (UINT)-1)
            qlen = xd_wlen(qname);
        n->qname = xd_intern(n->doc, qname, qlen);
        n->local = local_offset(qname, qlen);
    }
    if (uri && *uri)
        n->uri = xd_intern(n->doc, uri, (UINT)-1);
}

XNode *xd_node_new(XDoc *doc, UINT type, const WCHAR *qname, UINT qlen, const WCHAR *uri)
{
    XNode *n = xd_alloc(sizeof(XNode));
    if (!n)
        return nullptr;
    n->vtbl = xd_vtbl_for(type);
    n->type = (UINT8)type;
    n->doc = doc;
    if (qname || uri)
        xd_node_set_name(n, qname, qlen, uri);
    return n;
}

BOOL xd_node_set_value_owned(XNode *n, WCHAR *value, UINT length)
{
    xd_free(n->value);
    n->value = value;
    n->length = length;
    return TRUE;
}

void xd_node_set_value(XNode *n, const WCHAR *value, UINT length)
{
    WCHAR *copy = nullptr;
    if (value) {
        if (length == (UINT)-1)
            length = xd_wlen(value);
        copy = xd_wcsdup(value, length);
    } else {
        length = 0;
    }
    xd_node_set_value_owned(n, copy, length);
    xd_touch(n->doc);
}

const WCHAR *xd_local(const XNode *n)
{
    return n->qname ? n->qname + n->local : nullptr;
}

UINT xd_local_length(const XNode *n)
{
    return n->qname ? xd_atom_len(n->qname) - n->local : 0;
}

BOOL xd_is_xmlns_attr(const XNode *n)
{
    const WCHAR *q = n->qname;
    if (n->type != XN_ATTRIBUTE || !q || q[0] != 'x' || q[1] != 'm' || q[2] != 'l' || q[3] != 'n' || q[4] != 's')
        return FALSE;
    return q[5] == 0 || q[5] == ':';
}

void xd_touch(XDoc *doc)
{
    doc->version++;
}

BOOL xd_indent(const XDoc *doc)
{
    return !(doc->flags & XDF_V6) || !(doc->flags & XDF_PRESERVE);
}

/* ------------------------------------------------------------------ documents */

XDoc *xd_doc_new(UINT flags)
{
    XDoc *doc = xd_alloc(sizeof(XDoc));
    if (!doc)
        return nullptr;
    doc->node.vtbl = &xd_vtbl_document;
    doc->node.type = XN_DOCUMENT;
    doc->node.doc = doc;
    doc->node.refs = 1;
    doc->node.subs = 1;
    doc->live = 1;
    doc->flags = flags;
    doc->ready_state = 4;
    doc->max_depth = (flags & XDF_V6) ? 256 : 5000;
    doc->max_size = 0;
    doc->version = 1;
    VariantInit(&doc->schemas);
    InterlockedIncrement(&xd_objects);
    return doc;
}

/* frees what hangs below a node, one leaf at a time so a deep tree cannot overflow the stack. */
static void free_below(XNode *root)
{
    XNode *n = root;
    for (;;) {
        XNode *child = n->attrs ? n->attrs : n->first;
        if (child) {
            n = child;
            continue;
        }
        if (n == root)
            return;
        XNode *parent = n->parent;
        if (n->type == XN_ATTRIBUTE)
            parent->attrs = n->next;
        else
            parent->first = n->next;
        xd_free(n->value);
        xd_free(n);
        n = parent;
    }
}

static void free_tree(XNode *root)
{
    free_below(root);
    xd_free(root->value);
    xd_free(root);
}

static void doc_destroy(XDoc *doc)
{
    free_below(&doc->node);
    xd_free(doc->url);
    xd_free(doc->sel_ns_text);
    for (UINT i = 0; i < doc->sel_ns_count; i++) {
        xd_free(doc->sel_ns[i].prefix);
        xd_free(doc->sel_ns[i].uri);
    }
    xd_free(doc->sel_ns);
    for (UINT i = 0; i < doc->entity_count; i++)
        xd_free(doc->entities[i].value);
    xd_free(doc->entities);
    xd_err_free(&doc->err);
    xd_free(doc->encoding);
    if (doc->on_readystatechange)
        doc->on_readystatechange->lpVtbl->Release(doc->on_readystatechange);
    if (doc->on_dataavailable)
        doc->on_dataavailable->lpVtbl->Release(doc->on_dataavailable);
    if (doc->on_transformnode)
        doc->on_transformnode->lpVtbl->Release(doc->on_transformnode);
    VariantClear(&doc->schemas);
    xd_atoms_free(doc);
    xd_free(doc->node.value);
    xd_free(doc);
    InterlockedDecrement(&xd_objects);
}

/* ------------------------------------------------------------------ references */

ULONG xn_addref(XNode *n)
{
    EnterCriticalSection(&g_lock);
    LONG refs = ++n->refs;
    if (refs == 1) {
        for (XNode *a = n; a; a = a->parent)
            a->subs++;
        n->doc->live++;
    }
    LeaveCriticalSection(&g_lock);
    return (ULONG)refs;
}

ULONG xn_release(XNode *n)
{
    EnterCriticalSection(&g_lock);
    if (n->refs <= 0) {
        LeaveCriticalSection(&g_lock);
        return 0;
    }
    LONG refs = --n->refs;
    if (refs == 0) {
        XDoc *doc = n->doc;
        XNode *top = n;
        for (XNode *a = n; a; a = a->parent) {
            a->subs--;
            top = a;
        }
        doc->live--;
        if (doc->live == 0)
            doc_destroy(doc);
        else if (top != &doc->node && top->subs == 0)
            free_tree(top);
    }
    LeaveCriticalSection(&g_lock);
    return (ULONG)refs;
}

void *xd_ref(XNode *n)
{
    if (n)
        xn_addref(n);
    return n;
}

/* ------------------------------------------------------------------ linking */

static void add_subs(XNode *from, LONG amount)
{
    if (!amount)
        return;
    EnterCriticalSection(&g_lock);
    for (XNode *a = from; a; a = a->parent)
        a->subs += amount;
    LeaveCriticalSection(&g_lock);
}

void xd_link(XNode *parent, XNode *child, XNode *before)
{
    child->parent = parent;
    child->next = before;
    if (before) {
        child->prev = before->prev;
        before->prev = child;
    } else {
        child->prev = parent->last;
        parent->last = child;
    }
    if (child->prev)
        child->prev->next = child;
    else
        parent->first = child;
    add_subs(parent, child->subs);
    xd_touch(parent->doc);
}

void xd_unlink(XNode *child)
{
    XNode *parent = child->parent;
    if (!parent)
        return;
    if (child->prev)
        child->prev->next = child->next;
    else
        parent->first = child->next;
    if (child->next)
        child->next->prev = child->prev;
    else
        parent->last = child->prev;
    child->parent = child->prev = child->next = nullptr;
    add_subs(parent, -child->subs);
    xd_touch(parent->doc);
}

void xd_attr_link(XNode *element, XNode *attr)
{
    attr->parent = element;
    attr->next = nullptr;
    XNode *tail = element->attrs;
    if (!tail) {
        element->attrs = attr;
        attr->prev = nullptr;
    } else {
        while (tail->next)
            tail = tail->next;
        tail->next = attr;
        attr->prev = tail;
    }
    add_subs(element, attr->subs);
    xd_touch(element->doc);
}

void xd_attr_unlink(XNode *attr)
{
    XNode *element = attr->parent;
    if (!element)
        return;
    if (attr->prev)
        attr->prev->next = attr->next;
    else
        element->attrs = attr->next;
    if (attr->next)
        attr->next->prev = attr->prev;
    attr->parent = attr->prev = attr->next = nullptr;
    add_subs(element, -attr->subs);
    xd_touch(element->doc);
}

void xd_free_if_orphan(XNode *n)
{
    if (!n->parent && n->type != XN_DOCUMENT && n->subs == 0)
        free_tree(n);
}

void xd_clear_children(XNode *parent)
{
    XNode *c;
    while ((c = parent->first) != nullptr) {
        xd_unlink(c);
        xd_free_if_orphan(c);
    }
}

/* ------------------------------------------------------------------ lookups */

XNode *xd_attr_find(XNode *element, const WCHAR *qname, UINT length)
{
    for (XNode *a = element->attrs; a; a = a->next)
        if (xd_atom_len(a->qname) == length && !memcmp(a->qname, qname, (SIZE_T)length * sizeof(WCHAR)))
            return a;
    return nullptr;
}

static BOOL same_uri(const WCHAR *a, const WCHAR *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    return !wcscmp(a, b);
}

XNode *xd_attr_find_ns(XNode *element, const WCHAR *local, UINT length, const WCHAR *uri)
{
    if (uri && !*uri)
        uri = nullptr;
    for (XNode *a = element->attrs; a; a = a->next)
        if (xd_local_length(a) == length && !memcmp(xd_local(a), local, (SIZE_T)length * sizeof(WCHAR)) &&
            same_uri(a->uri, uri))
            return a;
    return nullptr;
}

XNode *xd_child_at(XNode *parent, UINT index)
{
    XNode *c = parent->first;
    while (c && index--)
        c = c->next;
    return c;
}

UINT xd_child_count(XNode *parent)
{
    UINT n = 0;
    for (XNode *c = parent->first; c; c = c->next)
        n++;
    return n;
}

XNode *xd_document_element(XDoc *doc)
{
    for (XNode *c = doc->node.first; c; c = c->next)
        if (c->type == XN_ELEMENT)
            return c;
    return nullptr;
}

BOOL xd_is_ancestor(XNode *maybe, XNode *of)
{
    for (XNode *a = of; a; a = a->parent)
        if (a == maybe)
            return TRUE;
    return FALSE;
}

static const WCHAR xml_uri[] = L"http://www.w3.org/XML/1998/namespace";

const WCHAR *xd_lookup_ns(XNode *at, const WCHAR *prefix, UINT length)
{
    if (length == 3 && !memcmp(prefix, L"xml", 6))
        return xml_uri;
    for (XNode *e = at; e; e = e->parent) {
        if (e->type != XN_ELEMENT)
            continue;
        for (XNode *a = e->attrs; a; a = a->next) {
            if (!xd_is_xmlns_attr(a))
                continue;
            UINT qlen = xd_atom_len(a->qname);
            if (length == 0 ? qlen == 5 : (qlen == 6 + length && !memcmp(a->qname + 6, prefix, (SIZE_T)length * sizeof(WCHAR))))
                return a->value && *a->value ? a->value : nullptr;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ text */

BOOL xd_text(XNode *n, XBuf *out)
{
    switch (n->type) {
    case XN_ATTRIBUTE:
    case XN_TEXT:
    case XN_CDATA:
    case XN_COMMENT:
    case XN_PI:
        if (n->value)
            xb_put(out, n->value, n->length);
        return !out->failed;
    case XN_ELEMENT:
    case XN_DOCUMENT:
    case XN_FRAGMENT:
    case XN_ENTITYREF:
        break;
    default:
        return TRUE;
    }
    XNode *c = n->first;
    while (c) {
        if ((c->type == XN_TEXT || c->type == XN_CDATA) && c->value)
            xb_put(out, c->value, c->length);
        if (c->first && (c->type == XN_ELEMENT || c->type == XN_ENTITYREF)) {
            c = c->first;
            continue;
        }
        while (c != n && !c->next)
            c = c->parent;
        if (c == n)
            break;
        c = c->next;
    }
    return !out->failed;
}

BOOL xd_set_text(XNode *n, const WCHAR *text, UINT length)
{
    switch (n->type) {
    case XN_ELEMENT:
    case XN_FRAGMENT:
    case XN_ENTITYREF: {
        xd_clear_children(n);
        if (length) {
            XNode *t = xd_node_new(n->doc, XN_TEXT, nullptr, 0, nullptr);
            if (!t)
                return FALSE;
            xd_node_set_value(t, text, length);
            xd_link(n, t, nullptr);
            n->flags &= ~XF_EXPLICIT;
        } else {
            n->flags |= XF_EXPLICIT;
        }
        return TRUE;
    }
    case XN_ATTRIBUTE:
    case XN_TEXT:
    case XN_CDATA:
    case XN_COMMENT:
    case XN_PI:
        xd_node_set_value(n, text, length);
        return TRUE;
    }
    return FALSE;
}

void xd_normalize(XNode *root)
{
    XNode *n = root;
    for (;;) {
        if (n->type == XN_ELEMENT || n->type == XN_DOCUMENT || n->type == XN_FRAGMENT) {
            XNode *c = n->first;
            while (c) {
                XNode *next = c->next;
                if (c->type == XN_TEXT) {
                    if (!c->length) {
                        xd_unlink(c);
                        xd_free_if_orphan(c);
                    } else if (next && next->type == XN_TEXT) {
                        UINT total = c->length + next->length;
                        WCHAR *merged = xd_alloc_raw(((SIZE_T)total + 1) * sizeof(WCHAR));
                        if (merged) {
                            memcpy(merged, c->value, (SIZE_T)c->length * sizeof(WCHAR));
                            memcpy(merged + c->length, next->value, ((SIZE_T)next->length + 1) * sizeof(WCHAR));
                            xd_node_set_value_owned(c, merged, total);
                            xd_unlink(next);
                            xd_free_if_orphan(next);
                            continue;
                        }
                    }
                }
                c = next;
            }
        }
        if (n->first && n->type != XN_ATTRIBUTE) {
            n = n->first;
            continue;
        }
        while (n != root && !n->next)
            n = n->parent;
        if (n == root)
            return;
        n = n->next;
    }
}

/* ------------------------------------------------------------------ document order */

void xd_number_order(XDoc *doc)
{
    if (doc->order_version == doc->version)
        return;
    UINT order = 0;
    XNode *n = &doc->node;
    for (;;) {
        n->order = ++order;
        for (XNode *a = n->attrs; a; a = a->next)
            a->order = ++order;
        if (n->first) {
            n = n->first;
            continue;
        }
        while (n != &doc->node && !n->next)
            n = n->parent;
        if (n == &doc->node)
            break;
        n = n->next;
    }
    doc->order_version = doc->version;
}

/* ------------------------------------------------------------------ cloning */

static XNode *clone_one(XNode *src, XDoc *into)
{
    XNode *n = xd_node_new(into, src->type, nullptr, 0, nullptr);
    if (!n)
        return nullptr;
    n->flags = src->flags;
    if (src->qname) {
        if (into == src->doc) {
            n->qname = src->qname;
            n->uri = src->uri;
            n->local = src->local;
        } else {
            xd_node_set_name(n, src->qname, xd_atom_len(src->qname), src->uri);
        }
    }
    if (src->value) {
        n->value = xd_wcsdup(src->value, src->length);
        n->length = src->length;
    }
    for (XNode *a = src->attrs; a; a = a->next) {
        XNode *copy = clone_one(a, into);
        if (!copy)
            break;
        xd_attr_link(n, copy);
    }
    return n;
}

XNode *xd_clone(XNode *src, XDoc *into, BOOL deep)
{
    XNode *root = clone_one(src, into);
    if (!root || !deep || !src->first)
        return root;
    XNode *s = src->first, *d = root;
    while (s) {
        XNode *c = clone_one(s, into);
        if (!c)
            break;
        xd_link(d, c, nullptr);
        if (s->first) {
            s = s->first;
            d = c;
            continue;
        }
        while (s != src && !s->next) {
            s = s->parent;
            d = d->parent;
        }
        if (s == src)
            break;
        s = s->next;
    }
    return root;
}

/* ------------------------------------------------------------------ inserting and removing */

static BOOL may_contain(UINT parent, UINT child)
{
    switch (parent) {
    case XN_DOCUMENT:
        return child == XN_ELEMENT || child == XN_PI || child == XN_COMMENT || child == XN_DOCTYPE;
    case XN_ELEMENT:
    case XN_FRAGMENT:
    case XN_ENTITYREF:
        return child == XN_ELEMENT || child == XN_TEXT || child == XN_CDATA || child == XN_COMMENT || child == XN_PI ||
               child == XN_ENTITYREF;
    }
    return FALSE;
}

HRESULT xd_check_insert(XNode *parent, XNode *child, XNode *before)
{
    if (before && before->parent != parent)
        return (HRESULT)XE_INSERT_NOT_CHILD;
    if (parent->type != XN_DOCUMENT && parent->type != XN_ELEMENT && parent->type != XN_FRAGMENT &&
        parent->type != XN_ENTITYREF)
        return (HRESULT)XE_BAD_NODE_TYPE;
    if (child->type == XN_FRAGMENT) {
        if (xd_is_ancestor(child, parent))
            return (HRESULT)XE_UNDER_ITSELF;
        for (XNode *c = child->first; c; c = c->next)
            if (!may_contain(parent->type, c->type))
                return (HRESULT)XE_BAD_NODE_TYPE;
        return S_OK;
    }
    if (xd_is_ancestor(child, parent))
        return (HRESULT)XE_UNDER_ITSELF;
    if (!may_contain(parent->type, child->type))
        return (HRESULT)XE_BAD_NODE_TYPE;
    if (parent->type == XN_DOCUMENT && (child->type == XN_ELEMENT || child->type == XN_DOCTYPE)) {
        for (XNode *c = parent->first; c; c = c->next)
            if (c->type == child->type && c != child)
                return (HRESULT)XE_BAD_NODE_TYPE;
    }
    return S_OK;
}

/* a node of another document is moved over as a copy, the original leaves its tree. */
static XNode *adopt(XNode *parent, XNode *child)
{
    if (child->doc == parent->doc)
        return child;
    XNode *copy = xd_clone(child, parent->doc, TRUE);
    if (child->parent) {
        if (child->type == XN_ATTRIBUTE)
            xd_attr_unlink(child);
        else
            xd_unlink(child);
    }
    return copy;
}

HRESULT xd_insert(XNode *parent, XNode *child, XNode *before, XNode **inserted)
{
    HRESULT hr = xd_check_insert(parent, child, before);
    if (FAILED(hr))
        return hr;
    if (child == before)
        before = child->next;
    if (child->type == XN_FRAGMENT) {
        XNode *c;
        while ((c = child->first) != nullptr) {
            xd_unlink(c);
            XNode *use = adopt(parent, c);
            if (!use)
                return E_OUTOFMEMORY;
            xd_link(parent, use, before);
        }
        if (inserted)
            *inserted = child;
        return S_OK;
    }
    XNode *use = adopt(parent, child);
    if (!use)
        return E_OUTOFMEMORY;
    if (use->parent)
        xd_unlink(use);
    xd_link(parent, use, before);
    if (inserted)
        *inserted = use;
    return S_OK;
}

HRESULT xd_remove(XNode *parent, XNode *child)
{
    if (child->parent != parent || child->type == XN_ATTRIBUTE)
        return (HRESULT)XE_NOT_A_CHILD;
    xd_unlink(child);
    return S_OK;
}
