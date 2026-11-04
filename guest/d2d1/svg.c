/*
 * svg documents: an xml reader that keeps the element tree, the ID2D1SvgDocument and ID2D1SvgElement objects over it,
 * and a renderer for the shapes, groups, gradients and transforms of svg 1.1 that direct2d draws.
 */
#include "d2d_private.h"
#include <float.h>

#define HR_NOT_FOUND ((HRESULT)0x80070490)

/* ------------------------------------------------------------------ the tree */

typedef struct SvgAttr {
    char *name;
    char *value;
} SvgAttr;

typedef struct SvgDoc SvgDoc;
typedef struct SvgElem SvgElem;

struct SvgElem {
    D2dObj obj;
    SvgDoc *doc;
    SvgElem *parent;
    SvgElem *first, *last, *next, *prev;
    char *tag;
    BOOL text_content;
    char *text;
    D2dArray attrs;
};

struct SvgDoc {
    D2dObj obj;
    SvgElem *root;
    D2D1_SIZE_F viewport;
    D2dRt *rt;
};

static D2dClass g_doc_class, g_elem_class;

static const char *find_char(const char *s, int c, size_t n)
{
    for (; n; n--, s++) {
        if (*s == (char)c)
            return s;
    }
    return nullptr;
}

static char *dup_n(const char *s, size_t n)
{
    char *d = D2dAlloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}
static char *dup_s(const char *s) { return dup_n(s, strlen(s)); }

static char *to_utf8(const WCHAR *w)
{
    if (!w)
        return nullptr;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    char *s = D2dAlloc(n > 0 ? (SIZE_T)n : 1);
    if (!s)
        return nullptr;
    s[0] = 0;
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, nullptr, nullptr);
    return s;
}

/* copies a utf8 string into a caller buffer of wide characters. count includes the terminator. */
static HRESULT utf8_out(const char *s, WCHAR *buf, UINT32 count)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if ((UINT32)n > count)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, (int)count);
    return S_OK;
}
static UINT32 utf8_len(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    return n > 0 ? (UINT32)(n - 1) : 0;
}

static const char *attr_get(const SvgElem *e, const char *name)
{
    const SvgAttr *a = e->attrs.data;
    for (UINT32 i = 0; i < e->attrs.count; i++) {
        if (strcmp(a[i].name, name) == 0)
            return a[i].value;
    }
    return nullptr;
}

static BOOL attr_set(SvgElem *e, const char *name, const char *value)
{
    SvgAttr *a = e->attrs.data;
    for (UINT32 i = 0; i < e->attrs.count; i++) {
        if (strcmp(a[i].name, name) == 0) {
            char *v = dup_s(value);
            if (!v)
                return FALSE;
            D2dFree(a[i].value);
            a[i].value = v;
            return TRUE;
        }
    }
    if (!D2dArrayGrow(&e->attrs, sizeof(SvgAttr), 1))
        return FALSE;
    SvgAttr n = {dup_s(name), dup_s(value)};
    if (!n.name || !n.value)
        return FALSE;
    ((SvgAttr *)e->attrs.data)[e->attrs.count++] = n;
    return TRUE;
}

static void elem_destroy(D2dObj *o)
{
    SvgElem *e = (SvgElem *)o;
    SvgElem *c = e->first;
    while (c) {
        SvgElem *next = c->next;
        c->parent = nullptr;
        c->prev = c->next = nullptr;
        D2dRelease(c);
        c = next;
    }
    SvgAttr *a = e->attrs.data;
    for (UINT32 i = 0; i < e->attrs.count; i++) {
        D2dFree(a[i].name);
        D2dFree(a[i].value);
    }
    D2dArrayFree(&e->attrs);
    D2dFree(e->tag);
    D2dFree(e->text);
    if (e->doc)
        D2dRelease(e->doc);
}

extern const void *const d2d_svg_elem_vtbl[];

static SvgElem *elem_new(SvgDoc *doc, const char *tag, BOOL text)
{
    SvgElem *e = D2dNew(sizeof(*e), d2d_svg_elem_vtbl, &g_elem_class, doc->obj.factory);
    if (!e)
        return nullptr;
    e->doc = doc;
    D2dAddRef(doc);
    e->tag = dup_s(tag);
    e->text_content = text;
    return e;
}

static void elem_append(SvgElem *parent, SvgElem *child)
{
    child->parent = parent;
    child->prev = parent->last;
    child->next = nullptr;
    if (parent->last)
        parent->last->next = child;
    else
        parent->first = child;
    parent->last = child;
}

static void elem_unlink(SvgElem *c)
{
    SvgElem *p = c->parent;
    if (!p)
        return;
    if (c->prev)
        c->prev->next = c->next;
    else
        p->first = c->next;
    if (c->next)
        c->next->prev = c->prev;
    else
        p->last = c->prev;
    c->parent = nullptr;
    c->prev = c->next = nullptr;
}

/* ------------------------------------------------------------------ xml reader */

typedef struct Reader {
    const char *p, *end;
    SvgDoc *doc;
    BOOL failed;
} Reader;

static BOOL is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static BOOL is_name(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '-' || c == '.' || (unsigned char)c >= 0x80; }

static void skip_space(Reader *r)
{
    while (r->p < r->end && is_space(*r->p))
        r->p++;
}

static BOOL starts(Reader *r, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(r->end - r->p) >= n && memcmp(r->p, s, n) == 0;
}

static char *decode_entities(const char *s, size_t n)
{
    char *out = D2dAlloc(n + 1);
    if (!out)
        return nullptr;
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        if (s[i] != '&') {
            out[o++] = s[i++];
            continue;
        }
        const char *semi = find_char(s + i, ';', n - i);
        if (!semi || semi - (s + i) > 10) {
            out[o++] = s[i++];
            continue;
        }
        size_t len = (size_t)(semi - (s + i)) - 1;
        const char *name = s + i + 1;
        unsigned cp = 0;
        BOOL ok = TRUE;
        if (len == 2 && !memcmp(name, "lt", 2))
            cp = '<';
        else if (len == 2 && !memcmp(name, "gt", 2))
            cp = '>';
        else if (len == 3 && !memcmp(name, "amp", 3))
            cp = '&';
        else if (len == 4 && !memcmp(name, "quot", 4))
            cp = '"';
        else if (len == 4 && !memcmp(name, "apos", 4))
            cp = '\'';
        else if (len >= 2 && name[0] == '#') {
            int base = 10;
            size_t k = 1;
            if (name[1] == 'x' || name[1] == 'X') {
                base = 16;
                k = 2;
            }
            for (; k < len; k++) {
                char c = name[k];
                int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                if (d < 0 || d >= base) {
                    ok = FALSE;
                    break;
                }
                cp = cp * (unsigned)base + (unsigned)d;
            }
        } else {
            ok = FALSE;
        }
        if (!ok) {
            out[o++] = s[i++];
            continue;
        }
        /* utf-8 encoding of the code point. */
        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xc0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xe0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 63));
            out[o++] = (char)(0x80 | (cp & 63));
        } else {
            out[o++] = (char)(0xf0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 63));
            out[o++] = (char)(0x80 | ((cp >> 6) & 63));
            out[o++] = (char)(0x80 | (cp & 63));
        }
        i = (size_t)(semi - s) + 1;
    }
    out[o] = 0;
    return out;
}

/* strips a namespace prefix of svg, keeping xlink and xml prefixes as they are. */
static char *local_name(const char *s, size_t n)
{
    const char *colon = find_char(s, ':', n);
    if (colon && !(colon - s == 5 && !memcmp(s, "xlink", 5)) && !(colon - s == 3 && !memcmp(s, "xml", 3)) && !(colon - s == 5 && !memcmp(s, "xmlns", 5))) {
        n -= (size_t)(colon - s) + 1;
        s = colon + 1;
    }
    return dup_n(s, n);
}

static SvgElem *parse_element(Reader *r)
{
    /* r->p is just after the '<'. */
    const char *start = r->p;
    while (r->p < r->end && is_name(*r->p))
        r->p++;
    char *tag = local_name(start, (size_t)(r->p - start));
    if (!tag || r->p == start) {
        D2dFree(tag);
        r->failed = TRUE;
        return nullptr;
    }
    SvgElem *e = elem_new(r->doc, tag, FALSE);
    D2dFree(tag);
    if (!e) {
        r->failed = TRUE;
        return nullptr;
    }
    for (;;) {
        skip_space(r);
        if (r->p >= r->end) {
            r->failed = TRUE;
            goto fail;
        }
        if (*r->p == '/' || *r->p == '>')
            break;
        const char *n0 = r->p;
        while (r->p < r->end && is_name(*r->p))
            r->p++;
        if (r->p == n0) {
            r->failed = TRUE;
            goto fail;
        }
        char *aname = local_name(n0, (size_t)(r->p - n0));
        skip_space(r);
        if (r->p >= r->end || *r->p != '=') {
            D2dFree(aname);
            r->failed = TRUE;
            goto fail;
        }
        r->p++;
        skip_space(r);
        if (r->p >= r->end || (*r->p != '"' && *r->p != '\'')) {
            D2dFree(aname);
            r->failed = TRUE;
            goto fail;
        }
        char q = *r->p++;
        const char *v0 = r->p;
        while (r->p < r->end && *r->p != q)
            r->p++;
        if (r->p >= r->end) {
            D2dFree(aname);
            r->failed = TRUE;
            goto fail;
        }
        char *val = decode_entities(v0, (size_t)(r->p - v0));
        r->p++;
        /* namespace declarations are not attributes of the document. */
        if (aname && val && strncmp(aname, "xmlns", 5) != 0)
            attr_set(e, aname, val);
        D2dFree(aname);
        D2dFree(val);
    }
    if (*r->p == '/') {
        r->p++;
        if (r->p >= r->end || *r->p != '>') {
            r->failed = TRUE;
            goto fail;
        }
        r->p++;
        return e;
    }
    r->p++;
    /* the content. */
    for (;;) {
        if (r->p >= r->end) {
            r->failed = TRUE;
            goto fail;
        }
        if (starts(r, "</")) {
            r->p += 2;
            while (r->p < r->end && *r->p != '>')
                r->p++;
            if (r->p < r->end)
                r->p++;
            return e;
        }
        if (starts(r, "<!--")) {
            const char *close = r->p + 4;
            while (close + 3 <= r->end && memcmp(close, "-->", 3))
                close++;
            r->p = close + 3 <= r->end ? close + 3 : r->end;
            continue;
        }
        if (starts(r, "<![CDATA[")) {
            const char *s = r->p + 9, *close = s;
            while (close + 3 <= r->end && memcmp(close, "]]>", 3))
                close++;
            SvgElem *t = elem_new(r->doc, "", TRUE);
            if (t) {
                t->text = dup_n(s, (size_t)(close - s));
                elem_append(e, t);
            }
            r->p = close + 3 <= r->end ? close + 3 : r->end;
            continue;
        }
        if (starts(r, "<?")) {
            while (r->p + 2 <= r->end && memcmp(r->p, "?>", 2))
                r->p++;
            r->p = r->p + 2 <= r->end ? r->p + 2 : r->end;
            continue;
        }
        if (*r->p == '<') {
            r->p++;
            SvgElem *c = parse_element(r);
            if (!c)
                goto fail;
            elem_append(e, c);
            continue;
        }
        const char *t0 = r->p;
        while (r->p < r->end && *r->p != '<')
            r->p++;
        BOOL blank = TRUE;
        for (const char *q = t0; q < r->p; q++) {
            if (!is_space(*q)) {
                blank = FALSE;
                break;
            }
        }
        if (!blank) {
            SvgElem *t = elem_new(r->doc, "", TRUE);
            if (t) {
                t->text = decode_entities(t0, (size_t)(r->p - t0));
                elem_append(e, t);
            }
        }
    }
fail:
    D2dRelease(e);
    return nullptr;
}

static SvgElem *parse_document(SvgDoc *doc, const char *data, size_t size)
{
    Reader r = {data, data + size, doc, FALSE};
    for (;;) {
        skip_space(&r);
        if (r.p >= r.end)
            return nullptr;
        if (starts(&r, "<?")) {
            while (r.p + 2 <= r.end && memcmp(r.p, "?>", 2))
                r.p++;
            r.p = r.p + 2 <= r.end ? r.p + 2 : r.end;
        } else if (starts(&r, "<!--")) {
            while (r.p + 3 <= r.end && memcmp(r.p, "-->", 3))
                r.p++;
            r.p = r.p + 3 <= r.end ? r.p + 3 : r.end;
        } else if (starts(&r, "<!")) {
            /* a doctype, possibly with an internal subset in brackets. */
            int depth = 0;
            while (r.p < r.end) {
                if (*r.p == '[')
                    depth++;
                else if (*r.p == ']')
                    depth--;
                else if (*r.p == '>' && depth <= 0) {
                    r.p++;
                    break;
                }
                r.p++;
            }
        } else if (*r.p == '<') {
            r.p++;
            return parse_element(&r);
        } else {
            return nullptr;
        }
    }
}

/* ------------------------------------------------------------------ element interface */

#define EL SvgElem *e = (SvgElem *)This

static SvgElem *elem_of(void *p) { return D2dIsOfClass(p, &g_elem_class) ? (SvgElem *)p : nullptr; }

static void STDMETHODCALLTYPE el_get_document(ID2D1SvgElement *This, ID2D1SvgDocument **doc)
{
    EL;
    *doc = (ID2D1SvgDocument *)e->doc;
    D2D_ADDREF(*doc);
}
static HRESULT STDMETHODCALLTYPE el_get_tag(ID2D1SvgElement *This, WCHAR *name, UINT32 count)
{
    EL;
    if (!name)
        return E_POINTER;
    return utf8_out(e->tag, name, count);
}
static UINT32 STDMETHODCALLTYPE el_tag_length(ID2D1SvgElement *This) { EL; return utf8_len(e->tag); }
static BOOL STDMETHODCALLTYPE el_is_text(ID2D1SvgElement *This) { EL; return e->text_content; }
static void STDMETHODCALLTYPE el_parent(ID2D1SvgElement *This, ID2D1SvgElement **out)
{
    EL;
    *out = (ID2D1SvgElement *)e->parent;
    D2D_ADDREF(*out);
}
static BOOL STDMETHODCALLTYPE el_has_children(ID2D1SvgElement *This) { EL; return e->first != nullptr; }
static void STDMETHODCALLTYPE el_first(ID2D1SvgElement *This, ID2D1SvgElement **out)
{
    EL;
    *out = (ID2D1SvgElement *)e->first;
    D2D_ADDREF(*out);
}
static void STDMETHODCALLTYPE el_last(ID2D1SvgElement *This, ID2D1SvgElement **out)
{
    EL;
    *out = (ID2D1SvgElement *)e->last;
    D2D_ADDREF(*out);
}
static HRESULT STDMETHODCALLTYPE el_prev(ID2D1SvgElement *This, ID2D1SvgElement *ref, ID2D1SvgElement **out)
{
    EL;
    SvgElem *r = elem_of(ref);
    *out = nullptr;
    if (!r || r->parent != e)
        return E_INVALIDARG;
    *out = (ID2D1SvgElement *)r->prev;
    D2D_ADDREF(*out);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE el_next(ID2D1SvgElement *This, ID2D1SvgElement *ref, ID2D1SvgElement **out)
{
    EL;
    SvgElem *r = elem_of(ref);
    *out = nullptr;
    if (!r || r->parent != e)
        return E_INVALIDARG;
    *out = (ID2D1SvgElement *)r->next;
    D2D_ADDREF(*out);
    return S_OK;
}

static BOOL is_ancestor(SvgElem *a, SvgElem *e)
{
    for (; e; e = e->parent)
        if (e == a)
            return TRUE;
    return FALSE;
}

static HRESULT insert_before(SvgElem *parent, SvgElem *child, SvgElem *ref)
{
    if (!child || child->doc != parent->doc || is_ancestor(child, parent) || child->parent)
        return E_INVALIDARG;
    if (ref && ref->parent != parent)
        return E_INVALIDARG;
    D2dAddRef(child);
    child->parent = parent;
    if (!ref) {
        child->prev = parent->last;
        child->next = nullptr;
        if (parent->last)
            parent->last->next = child;
        else
            parent->first = child;
        parent->last = child;
    } else {
        child->next = ref;
        child->prev = ref->prev;
        if (ref->prev)
            ref->prev->next = child;
        else
            parent->first = child;
        ref->prev = child;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE el_insert_before(ID2D1SvgElement *This, ID2D1SvgElement *child, ID2D1SvgElement *ref)
{
    EL;
    SvgElem *c = elem_of(child);
    if (!c)
        return E_INVALIDARG;
    return insert_before(e, c, elem_of(ref));
}
static HRESULT STDMETHODCALLTYPE el_append(ID2D1SvgElement *This, ID2D1SvgElement *child)
{
    EL;
    SvgElem *c = elem_of(child);
    if (!c)
        return E_INVALIDARG;
    return insert_before(e, c, nullptr);
}
static HRESULT STDMETHODCALLTYPE el_replace(ID2D1SvgElement *This, ID2D1SvgElement *child, ID2D1SvgElement *ref)
{
    EL;
    SvgElem *c = elem_of(child), *r = elem_of(ref);
    if (!c || !r || r->parent != e)
        return E_INVALIDARG;
    SvgElem *next = r->next;
    elem_unlink(r);
    D2dRelease(r);
    return insert_before(e, c, next);
}
static HRESULT STDMETHODCALLTYPE el_remove(ID2D1SvgElement *This, ID2D1SvgElement *child)
{
    EL;
    SvgElem *c = elem_of(child);
    if (!c || c->parent != e)
        return E_INVALIDARG;
    elem_unlink(c);
    D2dRelease(c);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE el_create_child(ID2D1SvgElement *This, const WCHAR *tag, ID2D1SvgElement **out)
{
    EL;
    if (!tag || !out)
        return E_INVALIDARG;
    *out = nullptr;
    char *t = to_utf8(tag);
    SvgElem *c = t ? elem_new(e->doc, t, FALSE) : nullptr;
    D2dFree(t);
    if (!c)
        return E_OUTOFMEMORY;
    HRESULT hr = insert_before(e, c, nullptr);
    if (FAILED(hr)) {
        D2dRelease(c);
        return hr;
    }
    /* the tree holds one reference and the caller gets another. */
    D2dAddRef(c);
    D2dRelease(c);
    *out = (ID2D1SvgElement *)c;
    return S_OK;
}
static BOOL STDMETHODCALLTYPE el_is_specified(ID2D1SvgElement *This, const WCHAR *name, BOOL *inherited)
{
    EL;
    char *n = to_utf8(name);
    BOOL has = n && attr_get(e, n);
    D2dFree(n);
    if (inherited)
        *inherited = FALSE;
    return has;
}
static UINT32 STDMETHODCALLTYPE el_attr_count(ID2D1SvgElement *This) { EL; return e->attrs.count; }
static HRESULT STDMETHODCALLTYPE el_attr_name(ID2D1SvgElement *This, UINT32 index, WCHAR *name, UINT32 count, BOOL *inherited)
{
    EL;
    if (index >= e->attrs.count)
        return E_INVALIDARG;
    if (inherited)
        *inherited = FALSE;
    return utf8_out(((SvgAttr *)e->attrs.data)[index].name, name, count);
}
static HRESULT STDMETHODCALLTYPE el_attr_name_length(ID2D1SvgElement *This, UINT32 index, UINT32 *length, BOOL *inherited)
{
    EL;
    if (index >= e->attrs.count)
        return E_INVALIDARG;
    if (inherited)
        *inherited = FALSE;
    *length = utf8_len(((SvgAttr *)e->attrs.data)[index].name);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE el_remove_attr(ID2D1SvgElement *This, const WCHAR *name)
{
    EL;
    char *n = to_utf8(name);
    SvgAttr *a = e->attrs.data;
    for (UINT32 i = 0; n && i < e->attrs.count; i++) {
        if (strcmp(a[i].name, n) == 0) {
            D2dFree(a[i].name);
            D2dFree(a[i].value);
            memmove(&a[i], &a[i + 1], (e->attrs.count - i - 1) * sizeof(*a));
            e->attrs.count--;
            break;
        }
    }
    D2dFree(n);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE el_set_text(ID2D1SvgElement *This, const WCHAR *text, UINT32 count)
{
    EL;
    if (!e->text_content)
        return D2DERR_INVALID_CALL;
    WCHAR *tmp = D2dAlloc(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (!tmp)
        return E_OUTOFMEMORY;
    memcpy(tmp, text, count * sizeof(WCHAR));
    tmp[count] = 0;
    char *u = to_utf8(tmp);
    D2dFree(tmp);
    D2dFree(e->text);
    e->text = u;
    return u ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE el_get_text(ID2D1SvgElement *This, WCHAR *text, UINT32 count)
{
    EL;
    if (!e->text_content)
        return D2DERR_INVALID_CALL;
    return utf8_out(e->text ? e->text : "", text, count);
}
static UINT32 STDMETHODCALLTYPE el_text_length(ID2D1SvgElement *This) { EL; return e->text_content ? utf8_len(e->text ? e->text : "") : 0; }
static HRESULT STDMETHODCALLTYPE el_set_attr_obj(ID2D1SvgElement *This, const WCHAR *name, ID2D1SvgAttribute *value)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE el_get_attr_obj(ID2D1SvgElement *This, const WCHAR *name, REFIID riid, void **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

/* the number at the start of a string, or false. */
static BOOL parse_number(const char **sp, double *out)
{
    const char *s = *sp;
    while (is_space(*s) || *s == ',')
        s++;
    const char *start = s;
    int sign = 1;
    if (*s == '+' || *s == '-') {
        sign = *s == '-' ? -1 : 1;
        s++;
    }
    double v = 0;
    BOOL digits = FALSE;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        digits = TRUE;
    }
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') {
            v += (*s - '0') * scale;
            scale *= 0.1;
            s++;
            digits = TRUE;
        }
    }
    if (!digits) {
        (void)start;
        return FALSE;
    }
    if ((*s == 'e' || *s == 'E') && (s[1] == '-' || s[1] == '+' || (s[1] >= '0' && s[1] <= '9'))) {
        const char *t = s + 1;
        int es = 1;
        if (*t == '+' || *t == '-') {
            es = *t == '-' ? -1 : 1;
            t++;
        }
        if (*t >= '0' && *t <= '9') {
            int ex = 0;
            while (*t >= '0' && *t <= '9')
                ex = ex * 10 + (*t++ - '0');
            double f = 1;
            for (int i = 0; i < ex && i < 308; i++)
                f *= 10;
            v = es > 0 ? v * f : v / f;
            s = t;
        }
    }
    *out = sign * v;
    *sp = s;
    return TRUE;
}

static BOOL parse_color(const char *s, D2D1_COLOR_F *c);

static HRESULT STDMETHODCALLTYPE el_set_pod(ID2D1SvgElement *This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_POD_TYPE type, const void *value, UINT32 size)
{
    EL;
    char *n = to_utf8(name);
    char buf[160];
    HRESULT hr = S_OK;
    if (!n)
        return E_OUTOFMEMORY;
    if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT && size == 4) {
        float f = *(const float *)value;
        int whole = (int)f;
        int frac = (int)((f - whole) * 100000.0f);
        if (frac < 0)
            frac = -frac;
        wsprintfA(buf, "%d.%05d", whole, frac);
    } else if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR && size == 16) {
        const D2D1_COLOR_F *c = value;
        wsprintfA(buf, "#%02x%02x%02x", (int)(c->r * 255 + 0.5f), (int)(c->g * 255 + 0.5f), (int)(c->b * 255 + 0.5f));
    } else {
        hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr) && !attr_set(e, n, buf))
        hr = E_OUTOFMEMORY;
    D2dFree(n);
    return hr;
}

static HRESULT STDMETHODCALLTYPE el_get_pod(ID2D1SvgElement *This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_POD_TYPE type, void *value, UINT32 size)
{
    EL;
    char *n = to_utf8(name);
    const char *v = n ? attr_get(e, n) : nullptr;
    D2dFree(n);
    if (!v)
        return HR_NOT_FOUND;
    if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT && size == 4) {
        double d = 0;
        const char *s = v;
        if (!parse_number(&s, &d))
            return E_INVALIDARG;
        *(float *)value = (float)d;
        return S_OK;
    }
    if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR && size == 16) {
        D2D1_COLOR_F c;
        if (!parse_color(v, &c))
            return E_INVALIDARG;
        *(D2D1_COLOR_F *)value = c;
        return S_OK;
    }
    if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH && size == sizeof(D2D1_SVG_LENGTH)) {
        double d = 0;
        const char *s = v;
        if (!parse_number(&s, &d))
            return E_INVALIDARG;
        D2D1_SVG_LENGTH *l = value;
        l->value = (float)d;
        l->units = *s == '%' ? D2D1_SVG_LENGTH_UNITS_PERCENTAGE : D2D1_SVG_LENGTH_UNITS_NUMBER;
        return S_OK;
    }
    if (type == D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX && size == sizeof(D2D1_SVG_VIEWBOX)) {
        double d[4];
        const char *s = v;
        for (int i = 0; i < 4; i++)
            if (!parse_number(&s, &d[i]))
                return E_INVALIDARG;
        D2D1_SVG_VIEWBOX *b = value;
        b->x = (float)d[0];
        b->y = (float)d[1];
        b->width = (float)d[2];
        b->height = (float)d[3];
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE el_set_string(ID2D1SvgElement *This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type, const WCHAR *value)
{
    EL;
    if (!name || !value)
        return E_INVALIDARG;
    char *n = to_utf8(name), *v = to_utf8(value);
    HRESULT hr = n && v && attr_set(e, n, v) ? S_OK : E_OUTOFMEMORY;
    D2dFree(n);
    D2dFree(v);
    return hr;
}
static HRESULT STDMETHODCALLTYPE el_get_string(ID2D1SvgElement *This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type, WCHAR *value, UINT32 count)
{
    EL;
    char *n = to_utf8(name);
    const char *v = n ? attr_get(e, n) : nullptr;
    D2dFree(n);
    if (!v)
        return HR_NOT_FOUND;
    return utf8_out(v, value, count);
}
static HRESULT STDMETHODCALLTYPE el_attr_length(ID2D1SvgElement *This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type, UINT32 *len)
{
    EL;
    char *n = to_utf8(name);
    const char *v = n ? attr_get(e, n) : nullptr;
    D2dFree(n);
    if (!v)
        return HR_NOT_FOUND;
    *len = utf8_len(v);
    return S_OK;
}

const void *const d2d_svg_elem_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, el_get_document, el_get_tag,
    el_tag_length, el_is_text, el_parent, el_has_children, el_first, el_last, el_prev, el_next, el_insert_before, el_append,
    el_replace, el_remove, el_create_child, el_is_specified, el_attr_count, el_attr_name, el_attr_name_length, el_remove_attr,
    el_set_text, el_get_text, el_text_length, el_set_attr_obj, el_get_attr_obj, el_set_pod, el_get_pod, el_set_string,
    el_get_string, el_attr_length};
D2D_IIDS(g_elem_iids, &IID_ID2D1Resource, &IID_ID2D1SvgElement);
static D2dClass g_elem_class = {"svg element", g_elem_iids, elem_destroy, nullptr};

/* ------------------------------------------------------------------ document interface */

#define DC SvgDoc *d = (SvgDoc *)This

static void doc_destroy(D2dObj *o)
{
    SvgDoc *d = (SvgDoc *)o;
    if (d->root) {
        d->root->parent = nullptr;
        D2dRelease(d->root);
    }
}

static HRESULT STDMETHODCALLTYPE doc_set_viewport(ID2D1SvgDocument *This, D2D1_SIZE_F size)
{
    DC;
    if (!(size.width >= 0) || !(size.height >= 0))
        return E_INVALIDARG;
    d->viewport = size;
    return S_OK;
}
static D2D1_SIZE_F *STDMETHODCALLTYPE doc_get_viewport(ID2D1SvgDocument *This, D2D1_SIZE_F *ret) { DC; *ret = d->viewport; return ret; }
static HRESULT STDMETHODCALLTYPE doc_set_root(ID2D1SvgDocument *This, ID2D1SvgElement *root)
{
    DC;
    SvgElem *r = elem_of(root);
    if (root && (!r || r->doc != d || r->parent))
        return E_INVALIDARG;
    if (d->root) {
        D2dRelease(d->root);
    }
    d->root = r;
    D2D_ADDREF(r);
    return S_OK;
}
static void STDMETHODCALLTYPE doc_get_root(ID2D1SvgDocument *This, ID2D1SvgElement **root)
{
    DC;
    *root = (ID2D1SvgElement *)d->root;
    D2D_ADDREF(*root);
}

static SvgElem *find_id(SvgElem *e, const char *id)
{
    const char *v = attr_get(e, "id");
    if (v && strcmp(v, id) == 0)
        return e;
    for (SvgElem *c = e->first; c; c = c->next) {
        SvgElem *f = find_id(c, id);
        if (f)
            return f;
    }
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE doc_find(ID2D1SvgDocument *This, const WCHAR *id, ID2D1SvgElement **out)
{
    DC;
    if (!id || !out)
        return E_INVALIDARG;
    *out = nullptr;
    char *u = to_utf8(id);
    SvgElem *f = u && d->root ? find_id(d->root, u) : nullptr;
    D2dFree(u);
    if (!f)
        return S_OK;
    *out = (ID2D1SvgElement *)f;
    D2dAddRef(f);
    return S_OK;
}

static void write_escaped(IStream *s, const char *t)
{
    ULONG w;
    for (; *t; t++) {
        const char *rep = *t == '<' ? "&lt;" : (*t == '>' ? "&gt;" : (*t == '&' ? "&amp;" : (*t == '"' ? "&quot;" : nullptr)));
        if (rep)
            s->lpVtbl->Write(s, rep, (ULONG)strlen(rep), &w);
        else
            s->lpVtbl->Write(s, t, 1, &w);
    }
}

static void write_elem(IStream *s, const SvgElem *e, int depth)
{
    ULONG w;
    if (e->text_content) {
        if (e->text)
            write_escaped(s, e->text);
        return;
    }
    s->lpVtbl->Write(s, "<", 1, &w);
    s->lpVtbl->Write(s, e->tag, (ULONG)strlen(e->tag), &w);
    const SvgAttr *a = e->attrs.data;
    for (UINT32 i = 0; i < e->attrs.count; i++) {
        s->lpVtbl->Write(s, " ", 1, &w);
        s->lpVtbl->Write(s, a[i].name, (ULONG)strlen(a[i].name), &w);
        s->lpVtbl->Write(s, "=\"", 2, &w);
        write_escaped(s, a[i].value);
        s->lpVtbl->Write(s, "\"", 1, &w);
    }
    if (!e->first) {
        s->lpVtbl->Write(s, "/>", 2, &w);
        return;
    }
    s->lpVtbl->Write(s, ">", 1, &w);
    for (const SvgElem *c = e->first; c; c = c->next)
        write_elem(s, c, depth + 1);
    s->lpVtbl->Write(s, "</", 2, &w);
    s->lpVtbl->Write(s, e->tag, (ULONG)strlen(e->tag), &w);
    s->lpVtbl->Write(s, ">", 1, &w);
}

static HRESULT STDMETHODCALLTYPE doc_serialize(ID2D1SvgDocument *This, IStream *stream, ID2D1SvgElement *subtree)
{
    DC;
    if (!stream)
        return E_INVALIDARG;
    SvgElem *e = subtree ? elem_of(subtree) : d->root;
    if (!e)
        return subtree ? E_INVALIDARG : S_OK;
    write_elem(stream, e, 0);
    return S_OK;
}

static HRESULT read_all(IStream *stream, char **data, size_t *size)
{
    size_t cap = 4096, n = 0;
    char *buf = D2dAlloc(cap);
    if (!buf)
        return E_OUTOFMEMORY;
    for (;;) {
        if (n + 4096 > cap) {
            cap *= 2;
            char *nb = D2dRealloc(buf, cap);
            if (!nb) {
                D2dFree(buf);
                return E_OUTOFMEMORY;
            }
            buf = nb;
        }
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, buf + n, 4096, &got);
        if (FAILED(hr)) {
            D2dFree(buf);
            return hr;
        }
        n += got;
        if (got == 0 || hr == S_FALSE)
            break;
    }
    *data = buf;
    *size = n;
    return S_OK;
}

static HRESULT load_text(SvgDoc *d, IStream *stream, SvgElem **out)
{
    char *data;
    size_t size;
    HRESULT hr = read_all(stream, &data, &size);
    if (FAILED(hr))
        return hr;
    const char *text = data;
    char *converted = nullptr;
    if (size >= 2 && (unsigned char)data[0] == 0xff && (unsigned char)data[1] == 0xfe) {
        int n = WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)(data + 2), (int)((size - 2) / 2), nullptr, 0, nullptr, nullptr);
        converted = D2dAlloc((SIZE_T)n + 1);
        if (converted) {
            WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)(data + 2), (int)((size - 2) / 2), converted, n, nullptr, nullptr);
            text = converted;
            size = (size_t)n;
        }
    } else if (size >= 3 && (unsigned char)data[0] == 0xef && (unsigned char)data[1] == 0xbb && (unsigned char)data[2] == 0xbf) {
        text += 3;
        size -= 3;
    }
    *out = parse_document(d, text, size);
    D2dFree(converted);
    D2dFree(data);
    return *out ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE doc_deserialize(ID2D1SvgDocument *This, IStream *stream, ID2D1SvgElement **subtree)
{
    DC;
    if (!stream || !subtree)
        return E_INVALIDARG;
    *subtree = nullptr;
    SvgElem *e;
    HRESULT hr = load_text(d, stream, &e);
    if (SUCCEEDED(hr))
        *subtree = (ID2D1SvgElement *)e;
    return hr;
}

static HRESULT STDMETHODCALLTYPE doc_no_paint(ID2D1SvgDocument *This, D2D1_SVG_PAINT_TYPE t, const D2D1_COLOR_F *c, const WCHAR *id, ID2D1SvgPaint **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE doc_no_dash(ID2D1SvgDocument *This, const D2D1_SVG_LENGTH *d, UINT32 n, ID2D1SvgStrokeDashArray **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE doc_no_points(ID2D1SvgDocument *This, const D2D1_POINT_2F *p, UINT32 n, ID2D1SvgPointCollection **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE doc_no_path(ID2D1SvgDocument *This, const FLOAT *d, UINT32 dn, const D2D1_SVG_PATH_COMMAND *c, UINT32 cn, ID2D1SvgPathData **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static const void *const g_doc_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, D2dResourceGetFactory, doc_set_viewport, doc_get_viewport,
                                         doc_set_root, doc_get_root, doc_find, doc_serialize, doc_deserialize, doc_no_paint,
                                         doc_no_dash, doc_no_points, doc_no_path};
D2D_IIDS(g_doc_iids, &IID_ID2D1Resource, &IID_ID2D1SvgDocument);
static D2dClass g_doc_class = {"svg document", g_doc_iids, doc_destroy, nullptr};

HRESULT D2dSvgCreate(D2dRt *rt, IStream *stream, D2D1_SIZE_F viewport, ID2D1SvgDocument **out)
{
    if (!(viewport.width >= 0) || !(viewport.height >= 0))
        return E_INVALIDARG;
    SvgDoc *d = D2dNew(sizeof(*d), g_doc_vtbl, &g_doc_class, rt->obj.factory);
    if (!d)
        return E_OUTOFMEMORY;
    d->viewport = viewport;
    if (stream) {
        HRESULT hr = load_text(d, stream, &d->root);
        if (FAILED(hr)) {
            D2dRelease(d);
            return hr;
        }
        d->root->parent = nullptr;
    } else {
        d->root = elem_new(d, "svg", FALSE);
        if (!d->root) {
            D2dRelease(d);
            return E_OUTOFMEMORY;
        }
    }
    *out = (ID2D1SvgDocument *)d;
    return S_OK;
}

/* ------------------------------------------------------------------ colors */

static const char g_color_names[] =
    "aliceblue:f0f8ff,antiquewhite:faebd7,aqua:00ffff,aquamarine:7fffd4,azure:f0ffff,beige:f5f5dc,bisque:ffe4c4,black:000000,"
    "blanchedalmond:ffebcd,blue:0000ff,blueviolet:8a2be2,brown:a52a2a,burlywood:deb887,cadetblue:5f9ea0,chartreuse:7fff00,"
    "chocolate:d2691e,coral:ff7f50,cornflowerblue:6495ed,cornsilk:fff8dc,crimson:dc143c,cyan:00ffff,darkblue:00008b,"
    "darkcyan:008b8b,darkgoldenrod:b8860b,darkgray:a9a9a9,darkgreen:006400,darkgrey:a9a9a9,darkkhaki:bdb76b,darkmagenta:8b008b,"
    "darkolivegreen:556b2f,darkorange:ff8c00,darkorchid:9932cc,darkred:8b0000,darksalmon:e9967a,darkseagreen:8fbc8f,"
    "darkslateblue:483d8b,darkslategray:2f4f4f,darkslategrey:2f4f4f,darkturquoise:00ced1,darkviolet:9400d3,deeppink:ff1493,"
    "deepskyblue:00bfff,dimgray:696969,dimgrey:696969,dodgerblue:1e90ff,firebrick:b22222,floralwhite:fffaf0,forestgreen:228b22,"
    "fuchsia:ff00ff,gainsboro:dcdcdc,ghostwhite:f8f8ff,gold:ffd700,goldenrod:daa520,gray:808080,green:008000,greenyellow:adff2f,"
    "grey:808080,honeydew:f0fff0,hotpink:ff69b4,indianred:cd5c5c,indigo:4b0082,ivory:fffff0,khaki:f0e68c,lavender:e6e6fa,"
    "lavenderblush:fff0f5,lawngreen:7cfc00,lemonchiffon:fffacd,lightblue:add8e6,lightcoral:f08080,lightcyan:e0ffff,"
    "lightgoldenrodyellow:fafad2,lightgray:d3d3d3,lightgreen:90ee90,lightgrey:d3d3d3,lightpink:ffb6c1,lightsalmon:ffa07a,"
    "lightseagreen:20b2aa,lightskyblue:87cefa,lightslategray:778899,lightslategrey:778899,lightsteelblue:b0c4de,lightyellow:ffffe0,"
    "lime:00ff00,limegreen:32cd32,linen:faf0e6,magenta:ff00ff,maroon:800000,mediumaquamarine:66cdaa,mediumblue:0000cd,"
    "mediumorchid:ba55d3,mediumpurple:9370db,mediumseagreen:3cb371,mediumslateblue:7b68ee,mediumspringgreen:00fa9a,"
    "mediumturquoise:48d1cc,mediumvioletred:c71585,midnightblue:191970,mintcream:f5fffa,mistyrose:ffe4e1,moccasin:ffe4b5,"
    "navajowhite:ffdead,navy:000080,oldlace:fdf5e6,olive:808000,olivedrab:6b8e23,orange:ffa500,orangered:ff4500,orchid:da70d6,"
    "palegoldenrod:eee8aa,palegreen:98fb98,paleturquoise:afeeee,palevioletred:db7093,papayawhip:ffefd5,peachpuff:ffdab9,peru:cd853f,"
    "pink:ffc0cb,plum:dda0dd,powderblue:b0e0e6,purple:800080,red:ff0000,rosybrown:bc8f8f,royalblue:4169e1,saddlebrown:8b4513,"
    "salmon:fa8072,sandybrown:f4a460,seagreen:2e8b57,seashell:fff5ee,sienna:a0522d,silver:c0c0c0,skyblue:87ceeb,slateblue:6a5acd,"
    "slategray:708090,slategrey:708090,snow:fffafa,springgreen:00ff7f,steelblue:4682b4,tan:d2b48c,teal:008080,thistle:d8bfd8,"
    "tomato:ff6347,turquoise:40e0d0,violet:ee82ee,wheat:f5deb3,white:ffffff,whitesmoke:f5f5f5,yellow:ffff00,yellowgreen:9acd32,";

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }

static BOOL parse_color(const char *s, D2D1_COLOR_F *c)
{
    while (is_space(*s))
        s++;
    size_t len = strlen(s);
    while (len && is_space(s[len - 1]))
        len--;
    c->a = 1.0f;
    if (*s == '#') {
        int v[8], n = 0;
        for (size_t i = 1; i < len && n < 8; i++) {
            v[n] = hexval(s[i]);
            if (v[n] < 0)
                return FALSE;
            n++;
        }
        if (n == 3 || n == 4) {
            c->r = (v[0] * 17) / 255.0f;
            c->g = (v[1] * 17) / 255.0f;
            c->b = (v[2] * 17) / 255.0f;
            if (n == 4)
                c->a = (v[3] * 17) / 255.0f;
            return TRUE;
        }
        if (n == 6 || n == 8) {
            c->r = (v[0] * 16 + v[1]) / 255.0f;
            c->g = (v[2] * 16 + v[3]) / 255.0f;
            c->b = (v[4] * 16 + v[5]) / 255.0f;
            if (n == 8)
                c->a = (v[6] * 16 + v[7]) / 255.0f;
            return TRUE;
        }
        return FALSE;
    }
    if (len > 4 && (!strncmp(s, "rgb(", 4) || !strncmp(s, "rgba(", 5))) {
        const char *p = s + (s[3] == 'a' ? 5 : 4);
        double v[4] = {0, 0, 0, 1};
        int n = 0;
        BOOL pct[4] = {};
        while (n < 4 && parse_number(&p, &v[n])) {
            if (*p == '%') {
                pct[n] = TRUE;
                p++;
            }
            n++;
        }
        if (n < 3)
            return FALSE;
        c->r = D2dClampF((float)(pct[0] ? v[0] / 100.0 : v[0] / 255.0), 0, 1);
        c->g = D2dClampF((float)(pct[1] ? v[1] / 100.0 : v[1] / 255.0), 0, 1);
        c->b = D2dClampF((float)(pct[2] ? v[2] / 100.0 : v[2] / 255.0), 0, 1);
        c->a = n > 3 ? D2dClampF((float)v[3], 0, 1) : 1.0f;
        return TRUE;
    }
    /* named colors, case insensitive. */
    char name[32];
    if (len == 0 || len >= sizeof(name))
        return FALSE;
    for (size_t i = 0; i < len; i++)
        name[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    name[len] = 0;
    if (!strcmp(name, "transparent")) {
        c->r = c->g = c->b = 0;
        c->a = 0;
        return TRUE;
    }
    for (const char *p = g_color_names; *p;) {
        const char *colon = strchr(p, ':');
        const char *comma = strchr(colon, ',');
        if ((size_t)(colon - p) == len && !memcmp(p, name, len)) {
            int v[6];
            for (int i = 0; i < 6; i++)
                v[i] = hexval(colon[1 + i]);
            c->r = (v[0] * 16 + v[1]) / 255.0f;
            c->g = (v[2] * 16 + v[3]) / 255.0f;
            c->b = (v[4] * 16 + v[5]) / 255.0f;
            return TRUE;
        }
        p = comma + 1;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ styles */

typedef struct Paint {
    int type; /* 0 none, 1 color, 2 current color, 3 url */
    D2D1_COLOR_F color;
    char url[96];
} Paint;

typedef struct Style {
    Paint fill, stroke;
    float fill_opacity, stroke_opacity, stroke_width, miter_limit, dash_offset;
    BOOL evenodd;
    D2D1_CAP_STYLE cap;
    D2D1_LINE_JOIN join;
    float dashes[16];
    int ndash;
    D2D1_COLOR_F color;
    BOOL hidden;
} Style;

/* the value of a presentation property, from the style attribute or the attribute of the same name. */
static BOOL prop_get(const SvgElem *e, const char *name, char *out, size_t cap)
{
    const char *style = attr_get(e, "style");
    size_t nlen = strlen(name);
    while (style && *style) {
        while (is_space(*style) || *style == ';')
            style++;
        const char *colon = strchr(style, ':');
        const char *semi = strchr(style, ';');
        if (!colon)
            break;
        const char *end = semi ? semi : style + strlen(style);
        if (colon < end) {
            const char *k1 = colon;
            while (k1 > style && is_space(k1[-1]))
                k1--;
            if ((size_t)(k1 - style) == nlen && !memcmp(style, name, nlen)) {
                const char *v = colon + 1;
                while (v < end && is_space(*v))
                    v++;
                const char *ve = end;
                while (ve > v && is_space(ve[-1]))
                    ve--;
                size_t n = (size_t)(ve - v);
                if (n >= cap)
                    n = cap - 1;
                memcpy(out, v, n);
                out[n] = 0;
                return TRUE;
            }
        }
        style = end;
    }
    const char *a = attr_get(e, name);
    if (a) {
        size_t n = strlen(a);
        if (n >= cap)
            n = cap - 1;
        memcpy(out, a, n);
        out[n] = 0;
        return TRUE;
    }
    return FALSE;
}

static void parse_paint(const char *v, Paint *p)
{
    while (is_space(*v))
        v++;
    if (!strcmp(v, "none")) {
        p->type = 0;
    } else if (!strcmp(v, "currentColor")) {
        p->type = 2;
    } else if (!strncmp(v, "url(", 4)) {
        const char *s = v + 4;
        while (is_space(*s) || *s == '\'' || *s == '"')
            s++;
        if (*s == '#')
            s++;
        size_t n = 0;
        while (s[n] && s[n] != ')' && s[n] != '\'' && s[n] != '"' && n < sizeof(p->url) - 1)
            n++;
        memcpy(p->url, s, n);
        p->url[n] = 0;
        p->type = 3;
    } else if (parse_color(v, &p->color)) {
        p->type = 1;
    }
}

static float num_prop(const char *v, float def)
{
    double d;
    const char *s = v;
    if (!parse_number(&s, &d))
        return def;
    if (*s == '%')
        d /= 100.0;
    return (float)d;
}

static void apply_props(const SvgElem *e, Style *st)
{
    char v[256];
    if (prop_get(e, "color", v, sizeof(v)))
        parse_color(v, &st->color);
    if (prop_get(e, "fill", v, sizeof(v)))
        parse_paint(v, &st->fill);
    if (prop_get(e, "stroke", v, sizeof(v)))
        parse_paint(v, &st->stroke);
    if (prop_get(e, "fill-opacity", v, sizeof(v)))
        st->fill_opacity = D2dClampF(num_prop(v, 1), 0, 1);
    if (prop_get(e, "stroke-opacity", v, sizeof(v)))
        st->stroke_opacity = D2dClampF(num_prop(v, 1), 0, 1);
    if (prop_get(e, "stroke-width", v, sizeof(v)))
        st->stroke_width = num_prop(v, 1);
    if (prop_get(e, "stroke-miterlimit", v, sizeof(v)))
        st->miter_limit = num_prop(v, 4);
    if (prop_get(e, "stroke-dashoffset", v, sizeof(v)))
        st->dash_offset = num_prop(v, 0);
    if (prop_get(e, "fill-rule", v, sizeof(v)))
        st->evenodd = !strcmp(v, "evenodd");
    if (prop_get(e, "stroke-linecap", v, sizeof(v)))
        st->cap = !strcmp(v, "round") ? D2D1_CAP_STYLE_ROUND : (!strcmp(v, "square") ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_FLAT);
    if (prop_get(e, "stroke-linejoin", v, sizeof(v)))
        st->join = !strcmp(v, "round") ? D2D1_LINE_JOIN_ROUND : (!strcmp(v, "bevel") ? D2D1_LINE_JOIN_BEVEL : D2D1_LINE_JOIN_MITER_OR_BEVEL);
    if (prop_get(e, "visibility", v, sizeof(v)))
        st->hidden = !strcmp(v, "hidden") || !strcmp(v, "collapse");
    if (prop_get(e, "stroke-dasharray", v, sizeof(v))) {
        st->ndash = 0;
        if (strcmp(v, "none")) {
            const char *s = v;
            double d;
            while (st->ndash < 16 && parse_number(&s, &d)) {
                if (*s == '%')
                    s++;
                st->dashes[st->ndash++] = (float)d;
            }
        }
    }
}

/* ------------------------------------------------------------------ transforms and paths */

static void mat_mul(D2dMatrix *out, const D2dMatrix *a, const D2dMatrix *b) { D2dMatMul(out, a, b); }

static BOOL parse_transform(const char *s, D2dMatrix *m)
{
    *m = d2d_identity;
    while (*s) {
        while (is_space(*s) || *s == ',')
            s++;
        if (!*s)
            break;
        char name[16];
        int n = 0;
        while (((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) && n < 15)
            name[n++] = *s++;
        name[n] = 0;
        while (is_space(*s))
            s++;
        if (*s != '(')
            return FALSE;
        s++;
        double a[6] = {};
        int count = 0;
        while (count < 6 && parse_number(&s, &a[count]))
            count++;
        while (is_space(*s))
            s++;
        if (*s != ')')
            return FALSE;
        s++;
        D2dMatrix t = d2d_identity;
        if (!strcmp(name, "matrix") && count == 6) {
            t._11 = (float)a[0]; t._12 = (float)a[1]; t._21 = (float)a[2]; t._22 = (float)a[3]; t._31 = (float)a[4]; t._32 = (float)a[5];
        } else if (!strcmp(name, "translate") && count >= 1) {
            t._31 = (float)a[0];
            t._32 = count > 1 ? (float)a[1] : 0.0f;
        } else if (!strcmp(name, "scale") && count >= 1) {
            t._11 = (float)a[0];
            t._22 = count > 1 ? (float)a[1] : (float)a[0];
        } else if (!strcmp(name, "rotate") && count >= 1) {
            float sn, cs;
            D2dSinCosF((float)a[0] * 0.017453292f, &sn, &cs);
            t._11 = cs; t._12 = sn; t._21 = -sn; t._22 = cs;
            if (count == 3) {
                t._31 = (float)a[1] - cs * (float)a[1] + sn * (float)a[2];
                t._32 = (float)a[2] - sn * (float)a[1] - cs * (float)a[2];
            }
        } else if (!strcmp(name, "skewX") && count == 1) {
            t._21 = D2dTanF((float)a[0] * 0.017453292f);
        } else if (!strcmp(name, "skewY") && count == 1) {
            t._12 = D2dTanF((float)a[0] * 0.017453292f);
        } else {
            return FALSE;
        }
        /* a list applies right to left, so each new item is on the inside. */
        D2dMatrix r;
        mat_mul(&r, &t, m);
        *m = r;
    }
    return TRUE;
}

static BOOL parse_flag(const char **sp, double *out)
{
    const char *s = *sp;
    while (is_space(*s) || *s == ',')
        s++;
    if (*s != '0' && *s != '1')
        return FALSE;
    *out = *s - '0';
    *sp = s + 1;
    return TRUE;
}

static BOOL path_from_data(const char *d, D2dPath *path)
{
    D2D1_POINT_2F cur = {0, 0}, start = {0, 0}, last_ctrl = {0, 0};
    char cmd = 0, prev = 0;
    BOOL open = FALSE;
    path->fill_mode = D2D1_FILL_MODE_WINDING;
    while (*d) {
        while (is_space(*d) || *d == ',')
            d++;
        if (!*d)
            break;
        if ((*d >= 'a' && *d <= 'z') || (*d >= 'A' && *d <= 'Z')) {
            cmd = *d++;
        } else if (!cmd) {
            return FALSE;
        } else if (cmd == 'M') {
            cmd = 'L';
        } else if (cmd == 'm') {
            cmd = 'l';
        } else if (cmd == 'z' || cmd == 'Z') {
            return TRUE;
        }
        BOOL rel = cmd >= 'a';
        char up = rel ? (char)(cmd - 32) : cmd;
        double v[7];
        D2D1_POINT_2F p, c1, c2;
        if (up == 'Z') {
            if (open) {
                D2dPathEnd(path, D2D1_FIGURE_END_CLOSED);
                open = FALSE;
            }
            cur = start;
            prev = 'Z';
            continue;
        }
        int need = up == 'M' || up == 'L' || up == 'T' ? 2 : (up == 'H' || up == 'V' ? 1 : (up == 'C' ? 6 : (up == 'S' || up == 'Q' ? 4 : (up == 'A' ? 7 : 0))));
        if (!need)
            return FALSE;
        for (int i = 0; i < need; i++) {
            BOOL ok = up == 'A' && (i == 3 || i == 4) ? parse_flag(&d, &v[i]) : parse_number(&d, &v[i]);
            if (!ok)
                return FALSE;
        }
        double ox = rel ? cur.x : 0, oy = rel ? cur.y : 0;
        if (up != 'M' && !open) {
            D2dPathBegin(path, cur, D2D1_FIGURE_BEGIN_FILLED);
            open = TRUE;
        }
        switch (up) {
        case 'M':
            if (open)
                D2dPathEnd(path, D2D1_FIGURE_END_OPEN);
            p.x = (float)(v[0] + ox);
            p.y = (float)(v[1] + oy);
            D2dPathBegin(path, p, D2D1_FIGURE_BEGIN_FILLED);
            open = TRUE;
            cur = start = p;
            break;
        case 'L':
            p.x = (float)(v[0] + ox);
            p.y = (float)(v[1] + oy);
            D2dPathLine(path, p);
            cur = p;
            break;
        case 'H':
            p.x = (float)(v[0] + ox);
            p.y = cur.y;
            D2dPathLine(path, p);
            cur = p;
            break;
        case 'V':
            p.x = cur.x;
            p.y = (float)(v[0] + oy);
            D2dPathLine(path, p);
            cur = p;
            break;
        case 'C':
            c1.x = (float)(v[0] + ox); c1.y = (float)(v[1] + oy);
            c2.x = (float)(v[2] + ox); c2.y = (float)(v[3] + oy);
            p.x = (float)(v[4] + ox); p.y = (float)(v[5] + oy);
            D2dPathBezier(path, c1, c2, p);
            last_ctrl = c2;
            cur = p;
            break;
        case 'S':
            c1 = (prev == 'C' || prev == 'S') ? (D2D1_POINT_2F){2 * cur.x - last_ctrl.x, 2 * cur.y - last_ctrl.y} : cur;
            c2.x = (float)(v[0] + ox); c2.y = (float)(v[1] + oy);
            p.x = (float)(v[2] + ox); p.y = (float)(v[3] + oy);
            D2dPathBezier(path, c1, c2, p);
            last_ctrl = c2;
            cur = p;
            break;
        case 'Q':
        case 'T': {
            D2D1_POINT_2F q;
            if (up == 'Q') {
                q.x = (float)(v[0] + ox); q.y = (float)(v[1] + oy);
                p.x = (float)(v[2] + ox); p.y = (float)(v[3] + oy);
            } else {
                q = (prev == 'Q' || prev == 'T') ? (D2D1_POINT_2F){2 * cur.x - last_ctrl.x, 2 * cur.y - last_ctrl.y} : cur;
                p.x = (float)(v[0] + ox); p.y = (float)(v[1] + oy);
            }
            D2D1_POINT_2F a = {cur.x + 2.0f / 3.0f * (q.x - cur.x), cur.y + 2.0f / 3.0f * (q.y - cur.y)};
            D2D1_POINT_2F b = {p.x + 2.0f / 3.0f * (q.x - p.x), p.y + 2.0f / 3.0f * (q.y - p.y)};
            D2dPathBezier(path, a, b, p);
            last_ctrl = q;
            cur = p;
            break;
        }
        case 'A': {
            D2dSegment seg = {};
            seg.kind = D2D_SEG_ARC;
            seg.arc.size.width = (float)v[0];
            seg.arc.size.height = (float)v[1];
            seg.arc.rotationAngle = (float)v[2];
            seg.arc.arcSize = v[3] != 0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
            seg.arc.sweepDirection = v[4] != 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
            seg.arc.point.x = (float)(v[5] + ox);
            seg.arc.point.y = (float)(v[6] + oy);
            D2dPathAdd(path, &seg);
            cur = seg.arc.point;
            break;
        }
        }
        prev = up;
    }
    if (open)
        D2dPathEnd(path, D2D1_FIGURE_END_OPEN);
    return TRUE;
}

/* ------------------------------------------------------------------ rendering */

typedef struct Render {
    D2dRt *rt;
    SvgDoc *doc;
    int depth;
} Render;

static float len_attr(const SvgElem *e, const char *name, float def, float percent_of)
{
    const char *v = attr_get(e, name);
    if (!v)
        return def;
    double d;
    const char *s = v;
    if (!parse_number(&s, &d))
        return def;
    if (*s == '%')
        return (float)(d / 100.0) * percent_of;
    if (!strncmp(s, "pt", 2))
        return (float)(d * 4.0 / 3.0);
    if (!strncmp(s, "pc", 2))
        return (float)(d * 16.0);
    if (!strncmp(s, "mm", 2))
        return (float)(d * 96.0 / 25.4);
    if (!strncmp(s, "cm", 2))
        return (float)(d * 96.0 / 2.54);
    if (!strncmp(s, "in", 2))
        return (float)(d * 96.0);
    return (float)d;
}

static BOOL shape_path(const SvgElem *e, D2dPath *path, float vw, float vh)
{
    const char *tag = e->tag;
    if (!strcmp(tag, "path")) {
        const char *d = attr_get(e, "d");
        return d && path_from_data(d, path);
    }
    if (!strcmp(tag, "rect")) {
        float x = len_attr(e, "x", 0, vw), y = len_attr(e, "y", 0, vh), w = len_attr(e, "width", 0, vw), h = len_attr(e, "height", 0, vh);
        if (!(w > 0) || !(h > 0))
            return FALSE;
        float rx = len_attr(e, "rx", -1, vw), ry = len_attr(e, "ry", -1, vh);
        if (rx < 0 && ry < 0)
            rx = ry = 0;
        else if (rx < 0)
            rx = ry;
        else if (ry < 0)
            ry = rx;
        rx = D2dMinF(rx, w / 2);
        ry = D2dMinF(ry, h / 2);
        path->fill_mode = D2D1_FILL_MODE_WINDING;
        D2D1_POINT_2F s = {x + rx, y};
        D2dPathBegin(path, s, D2D1_FIGURE_BEGIN_FILLED);
        D2dSegment arc = {};
        arc.kind = D2D_SEG_ARC;
        arc.arc.size.width = rx;
        arc.arc.size.height = ry;
        arc.arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        D2D1_POINT_2F p = {x + w - rx, y};
        D2dPathLine(path, p);
        if (rx > 0 && ry > 0) {
            arc.arc.point.x = x + w; arc.arc.point.y = y + ry;
            D2dPathAdd(path, &arc);
        }
        p.x = x + w; p.y = y + h - ry;
        D2dPathLine(path, p);
        if (rx > 0 && ry > 0) {
            arc.arc.point.x = x + w - rx; arc.arc.point.y = y + h;
            D2dPathAdd(path, &arc);
        }
        p.x = x + rx; p.y = y + h;
        D2dPathLine(path, p);
        if (rx > 0 && ry > 0) {
            arc.arc.point.x = x; arc.arc.point.y = y + h - ry;
            D2dPathAdd(path, &arc);
        }
        p.x = x; p.y = y + ry;
        D2dPathLine(path, p);
        if (rx > 0 && ry > 0) {
            arc.arc.point.x = x + rx; arc.arc.point.y = y;
            D2dPathAdd(path, &arc);
        }
        D2dPathEnd(path, D2D1_FIGURE_END_CLOSED);
        return TRUE;
    }
    if (!strcmp(tag, "circle") || !strcmp(tag, "ellipse")) {
        float cx = len_attr(e, "cx", 0, vw), cy = len_attr(e, "cy", 0, vh), rx, ry;
        if (tag[0] == 'c') {
            rx = ry = len_attr(e, "r", 0, D2dSqrtF((vw * vw + vh * vh) / 2));
        } else {
            rx = len_attr(e, "rx", 0, vw);
            ry = len_attr(e, "ry", 0, vh);
        }
        if (!(rx > 0) || !(ry > 0))
            return FALSE;
        D2D1_POINT_2F s = {cx + rx, cy};
        D2dPathBegin(path, s, D2D1_FIGURE_BEGIN_FILLED);
        D2dSegment arc = {};
        arc.kind = D2D_SEG_ARC;
        arc.arc.size.width = rx;
        arc.arc.size.height = ry;
        arc.arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arc.arc.point.x = cx - rx; arc.arc.point.y = cy;
        D2dPathAdd(path, &arc);
        arc.arc.point.x = cx + rx; arc.arc.point.y = cy;
        D2dPathAdd(path, &arc);
        D2dPathEnd(path, D2D1_FIGURE_END_CLOSED);
        return TRUE;
    }
    if (!strcmp(tag, "line")) {
        D2D1_POINT_2F a = {len_attr(e, "x1", 0, vw), len_attr(e, "y1", 0, vh)}, b = {len_attr(e, "x2", 0, vw), len_attr(e, "y2", 0, vh)};
        D2dPathBegin(path, a, D2D1_FIGURE_BEGIN_FILLED);
        D2dPathLine(path, b);
        D2dPathEnd(path, D2D1_FIGURE_END_OPEN);
        return TRUE;
    }
    if (!strcmp(tag, "polyline") || !strcmp(tag, "polygon")) {
        const char *pts = attr_get(e, "points");
        if (!pts)
            return FALSE;
        double x, y;
        const char *s = pts;
        BOOL first = TRUE;
        while (parse_number(&s, &x) && parse_number(&s, &y)) {
            D2D1_POINT_2F p = {(float)x, (float)y};
            if (first)
                D2dPathBegin(path, p, D2D1_FIGURE_BEGIN_FILLED);
            else
                D2dPathLine(path, p);
            first = FALSE;
        }
        if (first)
            return FALSE;
        D2dPathEnd(path, tag[4] == 'g' ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        return TRUE;
    }
    return FALSE;
}

static SvgElem *resolve_ref(Render *r, const char *id) { return r->doc->root ? find_id(r->doc->root, id) : nullptr; }

static const char *href_of(const SvgElem *e)
{
    const char *h = attr_get(e, "xlink:href");
    if (!h)
        h = attr_get(e, "href");
    return h && *h == '#' ? h + 1 : nullptr;
}

/* a gradient's attribute, looked up along the href chain. */
static const char *grad_attr(Render *r, const SvgElem *g, const char *name)
{
    for (int i = 0; g && i < 8; i++) {
        const char *v = attr_get(g, name);
        if (v)
            return v;
        const char *h = href_of(g);
        g = h ? resolve_ref(r, h) : nullptr;
    }
    return nullptr;
}

static const SvgElem *grad_with_stops(Render *r, const SvgElem *g)
{
    for (int i = 0; g && i < 8; i++) {
        for (const SvgElem *c = g->first; c; c = c->next)
            if (!c->text_content && !strcmp(c->tag, "stop"))
                return g;
        const char *h = href_of(g);
        g = h ? resolve_ref(r, h) : nullptr;
    }
    return nullptr;
}

static float grad_len(Render *r, const SvgElem *g, const char *name, float def, BOOL bbox)
{
    const char *v = grad_attr(r, g, name);
    if (!v)
        return def;
    double d;
    const char *s = v;
    if (!parse_number(&s, &d))
        return def;
    if (*s == '%')
        return (float)(d / 100.0);
    return (float)d;
}

/* makes the brush of a paint for a shape with these bounds. nullptr when there is nothing to paint. */
static ID2D1Brush *make_brush(Render *r, const Paint *p, const Style *st, float opacity, const D2D1_RECT_F *bounds)
{
    D2dFactory *f = r->rt->obj.factory;
    if (p->type == 0)
        return nullptr;
    if (p->type == 1 || p->type == 2) {
        D2D1_COLOR_F c = p->type == 1 ? p->color : st->color;
        c.a *= opacity;
        ID2D1SolidColorBrush *b;
        return SUCCEEDED(D2dCreateSolidBrush(f, &c, nullptr, &b)) ? (ID2D1Brush *)b : nullptr;
    }
    SvgElem *g = resolve_ref(r, p->url);
    if (!g || (strcmp(g->tag, "linearGradient") && strcmp(g->tag, "radialGradient"))) {
        return nullptr;
    }
    const SvgElem *sg = grad_with_stops(r, g);
    if (!sg)
        return nullptr;
    D2D1_GRADIENT_STOP stops[32];
    UINT32 n = 0;
    float last = 0;
    for (const SvgElem *c = sg->first; c && n < 32; c = c->next) {
        if (c->text_content || strcmp(c->tag, "stop"))
            continue;
        char v[96];
        float off = 0;
        if (prop_get(c, "offset", v, sizeof(v)) || attr_get(c, "offset")) {
            const char *o = attr_get(c, "offset");
            double d = 0;
            const char *s = o ? o : "0";
            if (parse_number(&s, &d))
                off = *s == '%' ? (float)(d / 100.0) : (float)d;
        }
        off = D2dClampF(off, last, 1);
        last = off;
        D2D1_COLOR_F col = {0, 0, 0, 1};
        if (prop_get(c, "stop-color", v, sizeof(v))) {
            if (!strcmp(v, "currentColor"))
                col = st->color;
            else
                parse_color(v, &col);
        }
        if (prop_get(c, "stop-opacity", v, sizeof(v)))
            col.a = num_prop(v, 1);
        col.a *= opacity;
        stops[n].position = off;
        stops[n].color = col;
        n++;
    }
    D2dGradient *gc;
    const char *spread = grad_attr(r, g, "spreadMethod");
    D2D1_EXTEND_MODE ext = spread && !strcmp(spread, "reflect") ? D2D1_EXTEND_MODE_MIRROR : (spread && !strcmp(spread, "repeat") ? D2D1_EXTEND_MODE_WRAP : D2D1_EXTEND_MODE_CLAMP);
    if (!n || FAILED(D2dCreateGradient(f, stops, n, D2D1_GAMMA_2_2, ext, FALSE, &gc)))
        return nullptr;
    const char *units = grad_attr(r, g, "gradientUnits");
    BOOL bbox = !units || strcmp(units, "userSpaceOnUse");
    D2dMatrix gt = d2d_identity;
    const char *tr = grad_attr(r, g, "gradientTransform");
    if (tr)
        parse_transform(tr, &gt);
    D2dMatrix bm = d2d_identity;
    float w = bounds->right - bounds->left, h = bounds->bottom - bounds->top;
    if (bbox) {
        if (!(w > 0) || !(h > 0)) {
            D2dRelease(gc);
            return nullptr;
        }
        bm._11 = w;
        bm._22 = h;
        bm._31 = bounds->left;
        bm._32 = bounds->top;
    }
    D2D1_BRUSH_PROPERTIES bp = {1.0f, d2d_identity};
    D2dMatMul(&bp.transform, &gt, &bm);
    ID2D1Brush *out = nullptr;
    if (!strcmp(g->tag, "linearGradient")) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {{grad_len(r, g, "x1", 0, bbox), grad_len(r, g, "y1", 0, bbox)},
                                                    {grad_len(r, g, "x2", bbox ? 1.0f : 0.0f, bbox), grad_len(r, g, "y2", 0, bbox)}};
        ID2D1LinearGradientBrush *b;
        if (SUCCEEDED(D2dCreateLinearBrush(f, &lp, &bp, (ID2D1GradientStopCollection *)gc, &b)))
            out = (ID2D1Brush *)b;
    } else {
        float cx = grad_len(r, g, "cx", 0.5f, bbox), cy = grad_len(r, g, "cy", 0.5f, bbox), rad = grad_len(r, g, "r", 0.5f, bbox);
        float fx = grad_len(r, g, "fx", cx, bbox), fy = grad_len(r, g, "fy", cy, bbox);
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {{cx, cy}, {fx - cx, fy - cy}, rad, rad};
        ID2D1RadialGradientBrush *b;
        if (SUCCEEDED(D2dCreateRadialBrush(f, &rp, &bp, (ID2D1GradientStopCollection *)gc, &b)))
            out = (ID2D1Brush *)b;
    }
    D2dRelease(gc);
    return out;
}

static void render_elem(Render *r, const SvgElem *e, const Style *parent, float vw, float vh);

static void render_shape(Render *r, const SvgElem *e, const Style *st, float vw, float vh, const D2dMatrix *local)
{
    D2dPath path = {};
    if (!shape_path(e, &path, vw, vh)) {
        D2dPathFree(&path);
        return;
    }
    path.fill_mode = st->evenodd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING;
    D2dGeometry g;
    D2dInitTempGeometry(&g, D2D_GEO_PATH);
    g.path = path;
    g.obj.factory = r->rt->obj.factory;
    D2D1_RECT_F bounds = {};
    D2dGeometryBounds(&g, nullptr, 0.25f, &bounds);
    BOOL filled = strcmp(e->tag, "line") != 0;
    if (filled && st->fill.type != 0) {
        ID2D1Brush *b = make_brush(r, &st->fill, st, st->fill_opacity, &bounds);
        if (b) {
            D2dFillGeometry(r->rt, (ID2D1Geometry *)&g, b, nullptr);
            D2dRelease(b);
        }
    }
    if (st->stroke.type != 0 && st->stroke_width > 0) {
        ID2D1Brush *b = make_brush(r, &st->stroke, st, st->stroke_opacity, &bounds);
        if (b) {
            ID2D1StrokeStyle1 *ss = nullptr;
            D2D1_STROKE_STYLE_PROPERTIES1 sp = {st->cap, st->cap, st->cap, st->join, D2dMaxF(st->miter_limit, 1.0f), D2D1_DASH_STYLE_SOLID, 0, D2D1_STROKE_TRANSFORM_TYPE_NORMAL};
            float dashes[32];
            int nd = st->ndash;
            if (nd > 0) {
                BOOL any = FALSE;
                for (int i = 0; i < nd; i++)
                    any |= st->dashes[i] > 0;
                if (any) {
                    int m = 0;
                    for (int rep = 0; rep < ((nd & 1) ? 2 : 1); rep++)
                        for (int i = 0; i < nd; i++)
                            dashes[m++] = st->dashes[i] / st->stroke_width;
                    sp.dashStyle = D2D1_DASH_STYLE_CUSTOM;
                    sp.dashOffset = st->dash_offset / st->stroke_width;
                    D2dCreateStrokeStyle(r->rt->obj.factory, &sp, TRUE, dashes, (UINT32)m, &ss);
                }
            }
            if (!ss)
                D2dCreateStrokeStyle(r->rt->obj.factory, &sp, TRUE, nullptr, 0, &ss);
            D2dDrawGeometry(r->rt, (ID2D1Geometry *)&g, b, st->stroke_width, (ID2D1StrokeStyle *)ss);
            D2D_RELEASE(ss);
            D2dRelease(b);
        }
    }
    D2dPathFree(&g.path);
}

static void render_children(Render *r, const SvgElem *e, const Style *st, float vw, float vh)
{
    for (const SvgElem *c = e->first; c; c = c->next) {
        if (!c->text_content)
            render_elem(r, c, st, vw, vh);
    }
}

static void render_elem(Render *r, const SvgElem *e, const Style *parent, float vw, float vh)
{
    if (r->depth > 32)
        return;
    const char *tag = e->tag;
    BOOL group = !strcmp(tag, "g") || !strcmp(tag, "svg") || !strcmp(tag, "a") || !strcmp(tag, "switch");
    BOOL use = !strcmp(tag, "use");
    BOOL shape = !strcmp(tag, "path") || !strcmp(tag, "rect") || !strcmp(tag, "circle") || !strcmp(tag, "ellipse") ||
                 !strcmp(tag, "line") || !strcmp(tag, "polyline") || !strcmp(tag, "polygon");
    if (!group && !use && !shape)
        return;
    char v[128];
    if (prop_get(e, "display", v, sizeof(v)) && !strcmp(v, "none"))
        return;
    Style st = *parent;
    apply_props(e, &st);
    D2dRt *rt = r->rt;
    D2dState saved = rt->state;
    D2D_ADDREF(saved.params);
    const char *tr = attr_get(e, "transform");
    D2dMatrix local = d2d_identity;
    if (tr && parse_transform(tr, &local)) {
        D2dMatMul(&rt->state.transform, &local, &rt->state.transform);
    }
    float opacity = 1.0f;
    if (prop_get(e, "opacity", v, sizeof(v)))
        opacity = D2dClampF(num_prop(v, 1), 0, 1);
    BOOL layer = opacity < 1.0f && (group || use);
    if (layer) {
        D2D1_LAYER_PARAMETERS1 lp = {{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX}, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, d2d_identity, opacity, nullptr, D2D1_LAYER_OPTIONS1_NONE};
        D2dPushLayer(rt, &lp);
    } else if (shape && opacity < 1.0f) {
        st.fill_opacity *= opacity;
        st.stroke_opacity *= opacity;
    }
    r->depth++;
    if (shape) {
        if (!st.hidden)
            render_shape(r, e, &st, vw, vh, &local);
    } else if (use) {
        const char *h = href_of(e);
        SvgElem *target = h ? resolve_ref(r, h) : nullptr;
        if (target) {
            D2dMatrix t = d2d_identity;
            t._31 = len_attr(e, "x", 0, vw);
            t._32 = len_attr(e, "y", 0, vh);
            D2dMatMul(&rt->state.transform, &t, &rt->state.transform);
            render_elem(r, target, &st, vw, vh);
        }
    } else if (!strcmp(tag, "svg") && e->parent) {
        /* a nested svg element positions its content and clips to its viewport. */
        D2dMatrix t = d2d_identity;
        t._31 = len_attr(e, "x", 0, vw);
        t._32 = len_attr(e, "y", 0, vh);
        D2dMatMul(&rt->state.transform, &t, &rt->state.transform);
        render_children(r, e, &st, vw, vh);
    } else {
        render_children(r, e, &st, vw, vh);
    }
    r->depth--;
    if (layer)
        D2dPopLayer(rt);
    D2D_RELEASE(rt->state.params);
    rt->state = saved;
}

void D2dSvgDraw(D2dRt *rt, ID2D1SvgDocument *document)
{
    if (!D2dIsOfClass(document, &g_doc_class)) {
        rt->error = rt->error ? rt->error : E_INVALIDARG;
        return;
    }
    SvgDoc *d = (SvgDoc *)document;
    if (!d->root || strcmp(d->root->tag, "svg"))
        return;
    Render r = {rt, d, 0};
    const SvgElem *root = d->root;
    float vw = d->viewport.width, vh = d->viewport.height;
    /* the view box is fitted into the viewport with the aspect ratio kept and centered. */
    D2dMatrix fit = d2d_identity;
    float ux = vw, uy = vh;
    const char *vb = attr_get(root, "viewBox");
    if (vb) {
        double b[4];
        const char *s = vb;
        BOOL ok = TRUE;
        for (int i = 0; i < 4; i++)
            ok &= parse_number(&s, &b[i]);
        if (ok && b[2] > 0 && b[3] > 0 && vw > 0 && vh > 0) {
            const char *par = attr_get(root, "preserveAspectRatio");
            float sx = vw / (float)b[2], sy = vh / (float)b[3];
            BOOL none = par && strstr(par, "none");
            BOOL slice = par && strstr(par, "slice");
            float sc = slice ? D2dMaxF(sx, sy) : D2dMinF(sx, sy);
            if (none) {
                fit._11 = sx;
                fit._22 = sy;
                fit._31 = -(float)b[0] * sx;
                fit._32 = -(float)b[1] * sy;
            } else {
                float ax = 0.5f, ay = 0.5f;
                if (par) {
                    if (strstr(par, "xMin")) ax = 0;
                    else if (strstr(par, "xMax")) ax = 1;
                    if (strstr(par, "YMin")) ay = 0;
                    else if (strstr(par, "YMax")) ay = 1;
                }
                fit._11 = fit._22 = sc;
                fit._31 = -(float)b[0] * sc + (vw - (float)b[2] * sc) * ax;
                fit._32 = -(float)b[1] * sc + (vh - (float)b[3] * sc) * ay;
            }
            ux = (float)b[2];
            uy = (float)b[3];
        }
    }
    D2dState saved = rt->state;
    D2D_ADDREF(saved.params);
    D2dMatMul(&rt->state.transform, &fit, &rt->state.transform);
    Style st = {};
    st.fill.type = 1;
    st.fill.color = (D2D1_COLOR_F){0, 0, 0, 1};
    st.fill_opacity = st.stroke_opacity = 1.0f;
    st.stroke_width = 1.0f;
    st.miter_limit = 4.0f;
    st.cap = D2D1_CAP_STYLE_FLAT;
    st.join = D2D1_LINE_JOIN_MITER_OR_BEVEL;
    st.color = (D2D1_COLOR_F){0, 0, 0, 1};
    apply_props(root, &st);
    /* the outermost svg is drawn through its own style and then its children. */
    float opacity = 1.0f;
    char v[64];
    if (prop_get(root, "opacity", v, sizeof(v)))
        opacity = D2dClampF(num_prop(v, 1), 0, 1);
    if (opacity < 1.0f) {
        D2D1_LAYER_PARAMETERS1 lp = {{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX}, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, d2d_identity, opacity, nullptr, D2D1_LAYER_OPTIONS1_NONE};
        D2dPushLayer(rt, &lp);
    }
    render_children(&r, root, &st, ux, uy);
    if (opacity < 1.0f)
        D2dPopLayer(rt);
    D2D_RELEASE(rt->state.params);
    rt->state = saved;
}
