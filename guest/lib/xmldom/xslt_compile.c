/* compiling a stylesheet document into instructions. */
#include <xmldom/xslt.h>

typedef struct Block {
    struct Block *next;
} Block;

void *xs_alloc(XsSheet *s, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    if (!s->cur || (SIZE_T)(s->end - s->cur) < size) {
        SIZE_T chunk = size > 16384 ? size : 16384;
        Block *b = xd_alloc_raw(sizeof(Block) + 16 + chunk);
        if (!b)
            return nullptr;
        b->next = s->arena;
        s->arena = b;
        s->cur = (UINT8 *)(((ULONG_PTR)(b + 1) + 15) & ~(ULONG_PTR)15);
        s->end = s->cur + chunk;
    }
    void *p = s->cur;
    s->cur += size;
    memset(p, 0, size);
    return p;
}

WCHAR *xs_dup(XsSheet *s, const WCHAR *text, UINT len)
{
    WCHAR *p = xs_alloc(s, ((SIZE_T)len + 1) * sizeof(WCHAR));
    if (p && len)
        memcpy(p, text, (SIZE_T)len * sizeof(WCHAR));
    return p;
}

/* a growing array in the arena: the old block stays, which is fine for the sizes here. */
void *xs_grow(XsSheet *s, void *old, UINT n, UINT cap, SIZE_T size)
{
    void *p = xs_alloc(s, (SIZE_T)cap * size);
    if (p && old && n)
        memcpy(p, old, (SIZE_T)n * size);
    return p;
}

#define XS_PUSH(sheet, arr, n, cap, T)                                                                                 \
    (((n) == (cap)) ? ((cap) = (cap) ? (cap) * 2 : 8, (arr) = xs_grow((sheet), (arr), (n), (cap), sizeof(T))) : (arr),  \
     &(arr)[(n)++])

/* ------------------------------------------------------------------ errors */

void xse_clear(XsError *e)
{
    for (int i = 0; i < 3; i++)
        xd_free(e->p[i]);
    xd_free(e->suffix);
    xd_free(e->text);
    memset(e, 0, sizeof(*e));
}

typedef struct XsMsg {
    UINT id;
    const WCHAR *m3;
    const WCHAR *m6;
} XsMsg;

static const XsMsg g_msgs[] = {
#include "xslt_messages.inc"
};

static const XsMsg *find_msg(UINT id)
{
    for (UINT i = 0; i < sizeof(g_msgs) / sizeof(g_msgs[0]); i++)
        if (g_msgs[i].id == id)
            return &g_msgs[i];
    return nullptr;
}

WCHAR *xse_text(const XsError *e, BOOL v6)
{
    if (e->text)
        return xd_wcsdup(e->text, xd_wlen(e->text));
    const XsMsg *m = find_msg(e->id);
    const WCHAR *text = m ? (v6 ? m->m6 : m->m3) : nullptr;
    if (m && !text)
        text = v6 ? m->m3 : m->m6;
    if (!text)
        text = xd_message(e->id);
    XBuf b = {};
    if (!text) {
        xb_puta(&b, "Unknown error.\r\n");
    } else {
        for (; *text; text++) {
            if (text[0] == '%' && text[1] >= '1' && text[1] <= '3') {
                const WCHAR *p = e->p[text[1] - '1'];
                if (p)
                    xb_put(&b, p, xd_wlen(p));
                text++;
            } else {
                xb_putc(&b, *text);
            }
        }
    }
    if (e->suffix) {
        xb_puta(&b, "\n");
        xb_put(&b, e->suffix, xd_wlen(e->suffix));
    }
    if (b.failed) {
        xb_free(&b);
        return nullptr;
    }
    return b.s ? b.s : xd_wcsdup(L"", 0);
}

/* the first error is the one that counts. */
HRESULT xse_set(XsError *e, UINT id, const WCHAR *p1, const WCHAR *p2, const WCHAR *p3)
{
    if (e->id)
        return E_FAIL;
    e->id = id;
    const WCHAR *p[3] = {p1, p2, p3};
    for (int i = 0; i < 3; i++)
        e->p[i] = p[i] ? xd_wcsdup(p[i], xd_wlen(p[i])) : nullptr;
    e->hr = E_FAIL;
    return E_FAIL;
}

/* an error that is only a text, such as the text of xsl:message. */
HRESULT xse_set_text(XsError *e, HRESULT hr, const WCHAR *text)
{
    if (e->id)
        return E_FAIL;
    e->id = 1;
    e->hr = hr;
    e->text = xd_wcsdup(text, xd_wlen(text));
    return hr;
}

/* a name that has a bad character at an offset: the name with the character marked. */
HRESULT xse_name_error(XsError *e, const WCHAR *s, UINT len, UINT bad)
{
    WCHAR before[200], after[200], ch[2];
    UINT b = bad < 198 ? bad : 198;
    memcpy(before, s, b * sizeof(WCHAR));
    before[b] = 0;
    UINT rest = len - bad - 1;
    UINT a = rest < 198 ? rest : 198;
    memcpy(after, s + bad + 1, a * sizeof(WCHAR));
    after[a] = 0;
    ch[0] = s[bad];
    ch[1] = 0;
    return xse_set(e, bad == 0 ? 0xC00CE230u : 0xC00CE22Fu, before, ch, after);
}

/* the expression with the part the message is about between arrows. */
WCHAR *xse_marked_text(const WCHAR *text, UINT length, UINT start, UINT end)
{
    XBuf b = {};
    if (start > length)
        start = length;
    if (end > length)
        end = length;
    if (end < start)
        end = start;
    xb_put(&b, text, start);
    if (start == end && end == length) {
        xb_puta(&b, "<--");
    } else {
        xb_puta(&b, "-->");
        xb_put(&b, text + start, end - start);
        xb_puta(&b, "<--");
        xb_put(&b, text + end, length - end);
    }
    if (b.failed) {
        xb_free(&b);
        return nullptr;
    }
    return b.s;
}

/* ------------------------------------------------------------------ the compiler state */

typedef struct Var {
    const WCHAR *uri;
    const WCHAR *local;
    UINT llen;
} Var;

typedef struct Ctx {
    XsSheet *s;
    XsScope *scope;
    XsModule *module;
    const WCHAR **excluded; /* uris of namespaces that are not copied to the result. */
    UINT nexcluded, capexcluded;
    const WCHAR **extension; /* uris of the extension namespaces. */
    UINT nextension, capextension;
    BOOL forward; /* version above 1.0 */
    XsTemplate *tmpl;
    BOOL in_for_each;
    BOOL sort_ok; /* an xsl:sort is being compiled where it may be. */
    UINT depth;
    Var *vars; /* the variables a body can see, innermost last. */
    UINT nvars, capvars;
    UINT base; /* the first variable of the template or global being compiled. */
    XpHost host;
    XNode *node;
    const WCHAR *base_url; /* of the module being compiled. */
    const WCHAR **stack;   /* the urls being compiled, for include loops. */
    UINT nstack, capstack;
    Var *pending; /* variables used but not yet seen, checked when everything is known. */
    UINT npending, cappending;
    XsInst **calls; /* call-template instructions, to check once all templates are known. */
    UINT ncalls, capcalls;
    XsName *used_sets; /* attribute sets used, checked at the end. */
    UINT nused_sets, capused_sets;
    UINT flags;
    BOOL script_blocked;
} Ctx;

static BOOL name_eq(const WCHAR *a, UINT al, const WCHAR *b, UINT bl)
{
    return al == bl && !memcmp(a, b, (SIZE_T)al * sizeof(WCHAR));
}

static BOOL is_lit(const WCHAR *s, UINT n, const char *lit)
{
    UINT i = 0;
    for (; i < n; i++)
        if (!lit[i] || s[i] != (UCHAR)lit[i])
            return FALSE;
    return lit[i] == 0;
}

static BOOL is_xsl(const XNode *n)
{
    return n->uri && !wcscmp(n->uri, XSLT_NS);
}

static BOOL is_msxsl(const XNode *n)
{
    return n->uri && !wcscmp(n->uri, MSXSL_NS);
}

static HRESULT cerr(Ctx *c, UINT id, const WCHAR *p1, const WCHAR *p2, const WCHAR *p3)
{
    return xse_set(&c->s->error, id, p1, p2, p3);
}

static BOOL failed(Ctx *c)
{
    return c->s->error.id != 0;
}

static BOOL local_is(const XNode *n, const char *lit)
{
    return is_lit(xd_local(n), xd_local_length(n), lit);
}

static const WCHAR *scope_lookup(const XsScope *sc, const WCHAR *prefix, UINT plen, BOOL *found);

/* ------------------------------------------------------------------ names and scopes */

static XsScope *make_scope(Ctx *c, XNode *element)
{
    UINT n = 0;
    for (XNode *a = element->attrs; a; a = a->next)
        if (xd_is_xmlns_attr(a))
            n++;
    if (!n)
        return c->scope;
    XsScope *sc = xs_alloc(c->s, sizeof(XsScope));
    sc->parent = c->scope;
    sc->decl = xs_alloc(c->s, n * sizeof(XNs));
    for (XNode *a = element->attrs; a; a = a->next) {
        if (!xd_is_xmlns_attr(a))
            continue;
        const WCHAR *q = a->qname;
        UINT ql = xd_atom_len(q);
        sc->decl[sc->n].prefix = ql > 6 ? xs_dup(c->s, q + 6, ql - 6) : xs_dup(c->s, L"", 0);
        sc->decl[sc->n].uri = xs_dup(c->s, a->value ? a->value : L"", a->length);
        sc->n++;
    }
    return sc;
}

const WCHAR *xs_scope_lookup(const XsScope *sc, const WCHAR *prefix, UINT plen, BOOL *found)
{
    for (; sc; sc = sc->parent)
        for (UINT i = sc->n; i-- > 0;)
            if (name_eq(sc->decl[i].prefix, xd_wlen(sc->decl[i].prefix), prefix, plen)) {
                *found = TRUE;
                return sc->decl[i].uri;
            }
    if (plen == 3 && is_lit(prefix, 3, "xml")) {
        *found = TRUE;
        return XML_NS;
    }
    *found = FALSE;
    return nullptr;
}

static const WCHAR *scope_lookup(const XsScope *sc, const WCHAR *prefix, UINT plen, BOOL *found)
{
    return xs_scope_lookup(sc, prefix, plen, found);
}

/* the namespaces in scope as one list in declaration order, outermost first, an inner declaration of a prefix
 * replacing the outer one in its place. */
static void flatten_scope(Ctx *c, XsScope *sc)
{
    if (!sc || sc->flat)
        return;
    UINT total = 0, depth = 0;
    for (XsScope *p = sc; p; p = p->parent) {
        total += p->n;
        depth++;
    }
    XsScope **chain = xs_alloc(c->s, depth * sizeof(XsScope *));
    UINT d = depth;
    for (XsScope *p = sc; p; p = p->parent)
        chain[--d] = p;
    XNs *v = xs_alloc(c->s, (total + 1) * sizeof(XNs));
    UINT n = 0;
    for (UINT k = 0; k < depth; k++) {
        XsScope *p = chain[k];
        for (UINT i = 0; i < p->n; i++) {
            UINT at = n;
            for (UINT q = 0; q < n; q++)
                if (!wcscmp(v[q].prefix, p->decl[i].prefix))
                    at = q;
            v[at] = p->decl[i];
            if (at == n)
                n++;
        }
    }
    sc->flat = v;
    sc->nflat = n;
}

/* splits a qualified name and resolves its prefix. a name without prefix uses the default namespace when asked. */
static BOOL resolve_qname(Ctx *c, const WCHAR *q, UINT len, BOOL use_default, XsName *out)
{
    memset(out, 0, sizeof(*out));
    UINT colon = len;
    for (UINT i = 0; i < len; i++)
        if (q[i] == ':') {
            colon = i;
            break;
        }
    out->qname = xs_dup(c->s, q, len);
    out->qlen = len;
    if (colon == len) {
        out->local = out->qname;
        out->llen = len;
        if (use_default) {
            BOOL found;
            const WCHAR *u = scope_lookup(c->scope, L"", 0, &found);
            if (found && *u)
                out->uri = u;
        }
        return TRUE;
    }
    if (!colon || colon + 1 >= len)
        return FALSE;
    BOOL found;
    const WCHAR *u = scope_lookup(c->scope, q, colon, &found);
    if (!found)
        return FALSE;
    out->prefix = out->qname;
    out->plen = colon;
    out->uri = u;
    out->local = out->qname + colon + 1;
    out->llen = len - colon - 1;
    return TRUE;
}

static void trim(const WCHAR **s, UINT *n)
{
    while (*n && xd_is_space(**s)) {
        (*s)++;
        (*n)--;
    }
    while (*n && xd_is_space((*s)[*n - 1]))
        (*n)--;
}

static BOOL name_same(const XsName *a, const XsName *b)
{
    return a->llen == b->llen && !memcmp(a->local, b->local, (SIZE_T)a->llen * sizeof(WCHAR)) &&
           !wcscmp(a->uri ? a->uri : L"", b->uri ? b->uri : L"");
}

/* ------------------------------------------------------------------ expressions */

static BOOL host_known(void *user, const WCHAR *uri, const WCHAR *local, UINT len, UINT args);
static BOOL host_declared(void *user, const WCHAR *uri, const WCHAR *local, UINT len);

/* the xslt functions and how many arguments each takes. */
static const struct {
    const char *name;
    int min, max;
} g_xslt_functions[] = {
    {"current", 0, 0},
    {"document", 1, 2},
    {"key", 2, 2},
    {"format-number", 2, 3},
    {"generate-id", 0, 1},
    {"system-property", 1, 1},
    {"element-available", 1, 1},
    {"function-available", 1, 1},
    {"unparsed-entity-uri", 1, 1},
};

static BOOL host_known(void *user, const WCHAR *uri, const WCHAR *local, UINT len, UINT args)
{
    if (!uri) {
        for (UINT i = 0; i < sizeof(g_xslt_functions) / sizeof(g_xslt_functions[0]); i++)
            if (is_lit(local, len, g_xslt_functions[i].name))
                return TRUE;
        return FALSE;
    }
    /* a function of a script block or of an object given to the processor is looked for when it is called. */
    return TRUE;
}

static BOOL host_declared(void *user, const WCHAR *uri, const WCHAR *local, UINT len)
{
    Ctx *c = user;
    for (UINT i = c->nvars; i-- > 0;)
        if (name_eq(c->vars[i].local, c->vars[i].llen, local, len) &&
            !wcscmp(c->vars[i].uri ? c->vars[i].uri : L"", uri ? uri : L""))
            return TRUE;
    for (UINT i = 0; i < c->s->nglobals; i++)
        if (name_eq(c->s->globals[i].name.local, c->s->globals[i].name.llen, local, len) &&
            !wcscmp(c->s->globals[i].name.uri ? c->s->globals[i].name.uri : L"", uri ? uri : L""))
            return TRUE;
    /* it may be a global declared further on. */
    Var *v = XS_PUSH(c->s, c->pending, c->npending, c->cappending, Var);
    v->uri = uri;
    v->local = xs_dup(c->s, local, len);
    v->llen = len;
    return TRUE;
}

static void track_expr(XsSheet *s, XpExpr *x)
{
    if (s->nexprs == s->capexprs) {
        UINT cap = s->capexprs ? s->capexprs * 2 : 64;
        XpExpr **v = xd_realloc(s->exprs, (SIZE_T)cap * sizeof(XpExpr *));
        if (!v)
            return;
        s->exprs = v;
        s->capexprs = cap;
    }
    s->exprs[s->nexprs++] = x;
}

/* an expression of the stylesheet. null with the error filled in. */
static const WCHAR *g_whole;
static UINT g_whole_len, g_whole_base;

static XpExpr *compile_expr(Ctx *c, const WCHAR *text, UINT len, UINT flags)
{
    flatten_scope(c, c->scope);
    XNs none[1];
    XpError err = {};
    XpExpr *x = xp_compile(text, len, c->scope ? c->scope->flat : none, c->scope ? c->scope->nflat : 0,
                           XPF_XPATH | flags | (c->s->v6 ? XPF_V6 : 0), &c->host, nullptr, &err);
    if (x) {
        track_expr(c->s, x);
        for (UINT i = 0; i + 8 <= len; i++)
            if (text[i] == 'd' && is_lit(text + i, 8, "document"))
                c->s->wants_source = TRUE;
        return x;
    }
    if (failed(c))
        return nullptr;
    UINT id = (UINT)err.hr;
    if (id == XE_UNDECLARED_PREFIX) {
        WCHAR prefix[80] = {};
        UINT i = err.start, k = 0;
        while (i < len && text[i] != ':' && k < 78 && (xd_is_name_char(text[i]) || text[i] == '$'))
            prefix[k++] = text[i++];
        UINT skip = k && prefix[0] == '$' ? 1 : 0;
        cerr(c, id, prefix + skip, nullptr, nullptr);
        return nullptr;
    }
    if (id == XE_BAD_FUNCTION)
        cerr(c, XE_XS_BAD_FUNCTION, err.p1, nullptr, nullptr);
    else
        cerr(c, id, err.p1[0] ? err.p1 : nullptr, err.p2[0] ? err.p2 : nullptr, nullptr);
    if (g_whole)
        c->s->error.suffix = xse_marked_text(g_whole, g_whole_len, g_whole_base + err.start, g_whole_base + err.end);
    else
        c->s->error.suffix = xse_marked_text(text, len, err.start, err.end);
    return nullptr;
}

static XpExpr *attr_expr(Ctx *c, XNode *a, UINT flags)
{
    return compile_expr(c, a->value ? a->value : L"", a->length, flags);
}

/* ------------------------------------------------------------------ attribute value templates */

static XsAvt *parse_avt(Ctx *c, const WCHAR *s, UINT len)
{
    XsAvt *avt = xs_alloc(c->s, sizeof(XsAvt));
    UINT cap = 0;
    XBuf lit = {};
    UINT i = 0;
    for (;;) {
        if (i >= len || s[i] == '{' || s[i] == '}') {
            if (i + 1 < len && s[i] == s[i + 1]) {
                xb_putc(&lit, s[i]);
                i += 2;
                continue;
            }
            if (i < len && s[i] == '}') {
                cerr(c, XE_XPATH_UNEXPECTED, L"'}'", nullptr, nullptr);
                c->s->error.suffix = xse_marked_text(s, len, i, i + 1);
                xb_free(&lit);
                return nullptr;
            }
            if (lit.length) {
                XsAvtPart *p = XS_PUSH(c->s, avt->parts, avt->n, cap, XsAvtPart);
                p->lit = xs_dup(c->s, lit.s, lit.length);
                p->len = lit.length;
                lit.length = 0;
            }
            if (i >= len)
                break;
            /* an expression up to the closing brace, not counting braces in string literals. */
            UINT start = ++i;
            WCHAR quote = 0;
            while (i < len && (quote || s[i] != '}')) {
                if (quote) {
                    if (s[i] == quote)
                        quote = 0;
                } else if (s[i] == '\'' || s[i] == '"') {
                    quote = s[i];
                }
                i++;
            }
            g_whole = s;
            g_whole_len = len;
            g_whole_base = start;
            XpExpr *x = compile_expr(c, s + start, i - start, 0);
            g_whole = nullptr;
            if (!x) {
                xb_free(&lit);
                return nullptr;
            }
            XsAvtPart *p = XS_PUSH(c->s, avt->parts, avt->n, cap, XsAvtPart);
            p->expr = x;
            if (i < len)
                i++;
            continue;
        }
        xb_putc(&lit, s[i++]);
    }
    xb_free(&lit);
    return avt;
}

/* ------------------------------------------------------------------ elements and their attributes */

typedef struct Spec {
    const char *name;
    const char *required;
    const char *optional;
} Spec;

static const Spec g_specs[] = {
    {"stylesheet", "version", "id extension-element-prefixes exclude-result-prefixes"},
    {"transform", "version", "id extension-element-prefixes exclude-result-prefixes"},
    {"import", "href", ""},
    {"include", "href", ""},
    {"strip-space", "elements", ""},
    {"preserve-space", "elements", ""},
    {"output", "",
     "method version encoding omit-xml-declaration standalone doctype-public doctype-system cdata-section-elements indent "
     "media-type"},
    {"key", "name match use", ""},
    {"decimal-format", "",
     "name decimal-separator grouping-separator infinity minus-sign NaN percent per-mille zero-digit digit "
     "pattern-separator"},
    {"namespace-alias", "stylesheet-prefix result-prefix", ""},
    {"attribute-set", "name", "use-attribute-sets"},
    {"variable", "name", "select"},
    {"param", "name", "select"},
    {"template", "", "match name priority mode"},
    {"apply-templates", "", "select mode"},
    {"apply-imports", "", ""},
    {"call-template", "name", ""},
    {"for-each", "select", ""},
    {"value-of", "select", "disable-output-escaping"},
    {"copy-of", "select", ""},
    {"if", "test", ""},
    {"choose", "", ""},
    {"when", "test", ""},
    {"otherwise", "", ""},
    {"with-param", "name", "select"},
    {"sort", "", "select lang data-type order case-order"},
    {"text", "", "disable-output-escaping"},
    {"element", "", "name namespace use-attribute-sets"},
    {"attribute", "", "name namespace"},
    {"comment", "", ""},
    {"processing-instruction", "name", ""},
    {"copy", "", "use-attribute-sets"},
    {"number", "", "level count from value format lang letter-value grouping-separator grouping-size"},
    {"message", "", "terminate"},
    {"fallback", "", ""},
};

static BOOL word_in(const char *list, const WCHAR *w, UINT n)
{
    while (*list) {
        const char *e = list;
        while (*e && *e != ' ')
            e++;
        if ((UINT)(e - list) == n) {
            UINT i = 0;
            while (i < n && (UCHAR)list[i] == w[i])
                i++;
            if (i == n)
                return TRUE;
        }
        list = *e ? e + 1 : e;
    }
    return FALSE;
}

/* the attributes of an xsl element: only the listed ones, and the required ones present. */
static BOOL check_attrs(Ctx *c, XNode *el)
{
    const WCHAR *l = xd_local(el);
    UINT ll = xd_local_length(el);
    const Spec *spec = nullptr;
    for (UINT i = 0; i < sizeof(g_specs) / sizeof(g_specs[0]); i++)
        if (is_lit(l, ll, g_specs[i].name)) {
            spec = &g_specs[i];
            break;
        }
    if (!spec)
        return TRUE;
    for (XNode *a = el->attrs; a; a = a->next) {
        if (xd_is_xmlns_attr(a) || a->uri)
            continue;
        if (!word_in(spec->required, a->qname, xd_atom_len(a->qname)) &&
            !word_in(spec->optional, a->qname, xd_atom_len(a->qname))) {
            cerr(c, XE_XS_ATTR_INVALID, a->qname, el->qname, nullptr);
            return FALSE;
        }
    }
    const char *r = spec->required;
    while (*r) {
        const char *e = r;
        while (*e && *e != ' ')
            e++;
        WCHAR w[40];
        UINT n = (UINT)(e - r);
        for (UINT i = 0; i < n; i++)
            w[i] = (UCHAR)r[i];
        w[n] = 0;
        if (!xd_attr_find(el, w, n)) {
            cerr(c, 0xC00CE020u, w, nullptr, nullptr);
            return FALSE;
        }
        r = *e ? e + 1 : e;
    }
    return TRUE;
}

static XNode *attr_node(XNode *el, const char *name)
{
    WCHAR w[40];
    UINT n = 0;
    while (name[n]) {
        w[n] = (UCHAR)name[n];
        n++;
    }
    return xd_attr_find(el, w, n);
}

static const WCHAR *attr_value(XNode *el, const char *name, UINT *len)
{
    XNode *a = attr_node(el, name);
    if (!a)
        return nullptr;
    *len = a->length;
    return a->value ? a->value : L"";
}

static XsAvt *attr_avt(Ctx *c, XNode *el, const char *name)
{
    XNode *a = attr_node(el, name);
    if (!a)
        return nullptr;
    return parse_avt(c, a->value ? a->value : L"", a->length);
}

/* an attribute that is yes or no. */
static BOOL yes_no(Ctx *c, XNode *el, const char *name, int *out)
{
    UINT n;
    const WCHAR *v = attr_value(el, name, &n);
    if (!v)
        return TRUE;
    trim(&v, &n);
    if (is_lit(v, n, "yes")) {
        *out = 1;
        return TRUE;
    }
    if (is_lit(v, n, "no")) {
        *out = 0;
        return TRUE;
    }
    WCHAR w[40];
    UINT k = 0;
    for (; name[k]; k++)
        w[k] = (UCHAR)name[k];
    w[k] = 0;
    WCHAR val[80];
    UINT m = n < 78 ? n : 78;
    memcpy(val, v, m * sizeof(WCHAR));
    val[m] = 0;
    if (c->s->v6)
        cerr(c, XE_XS_ATTR_BAD, w, val, nullptr);
    else
        cerr(c, XE_XS_ATTR_ONLY, w, L"yes", L"no");
    return FALSE;
}

static BOOL preserve_space(XNode *n)
{
    for (; n; n = n->parent) {
        if (n->type != XN_ELEMENT)
            continue;
        XNode *a = xd_attr_find(n, L"xml:space", 9);
        if (a && a->value) {
            if (is_lit(a->value, a->length, "preserve"))
                return TRUE;
            if (is_lit(a->value, a->length, "default"))
                return FALSE;
        }
    }
    return FALSE;
}

static BOOL all_space(const WCHAR *s, UINT n)
{
    for (UINT i = 0; i < n; i++)
        if (!xd_is_space(s[i]))
            return FALSE;
    return TRUE;
}

/* white space only text that the stylesheet does not keep. */
static BOOL stripped_text(XNode *k)
{
    return (k->type == XN_TEXT || k->type == XN_CDATA) && all_space(k->value ? k->value : L"", k->length) &&
           !preserve_space(k);
}

/* ------------------------------------------------------------------ instructions */

static HRESULT compile_children(Ctx *c, XNode *parent, XsInst **list);
static HRESULT compile_element(Ctx *c, XNode *el, XsInst ***tail);

static XsInst *new_inst(Ctx *c, int op)
{
    XsInst *i = xs_alloc(c->s, sizeof(XsInst));
    i->op = op;
    i->scope = c->scope;
    i->tmpl = c->tmpl;
    i->in_for_each = c->in_for_each;
    return i;
}

static void append(XsInst ***tail, XsInst *i)
{
    **tail = i;
    *tail = &i->next;
}

static void push_var(Ctx *c, const XsName *n)
{
    Var *v = XS_PUSH(c->s, c->vars, c->nvars, c->capvars, Var);
    v->uri = n->uri;
    v->local = n->local;
    v->llen = n->llen;
}

/* a variable or parameter may not be defined twice within a template, not even in a nested scope. */
static BOOL declare_local(Ctx *c, XsName *n)
{
    for (UINT i = c->base; i < c->nvars; i++) {
        XsName other = {};
        other.uri = c->vars[i].uri;
        other.local = c->vars[i].local;
        other.llen = c->vars[i].llen;
        if (name_same(&other, n)) {
            cerr(c, XE_XS_VAR_TWICE, n->qname, nullptr, nullptr);
            return FALSE;
        }
    }
    push_var(c, n);
    return TRUE;
}

static const WCHAR *child_label(XNode *n)
{
    return n->type == XN_ELEMENT ? n->qname : L"PCDATA nodes";
}

/* a name from an attribute, resolved: variables, templates, modes and keys use no default namespace. */
static BOOL attr_qname(Ctx *c, XNode *el, const char *name, XsName *out)
{
    UINT n;
    const WCHAR *v = attr_value(el, name, &n);
    if (!v)
        return FALSE;
    trim(&v, &n);
    if (n && xd_check_name(v, n, TRUE) < 0 && resolve_qname(c, v, n, FALSE, out))
        return TRUE;
    for (UINT i = 0; i < n; i++)
        if (v[i] == ':' && i) {
            BOOL f;
            scope_lookup(c->scope, v, i, &f);
            if (!f) {
                WCHAR p[80];
                UINT m = i < 78 ? i : 78;
                memcpy(p, v, m * sizeof(WCHAR));
                p[m] = 0;
                cerr(c, XE_UNDECLARED_PREFIX, p, nullptr, nullptr);
                return FALSE;
            }
        }
    WCHAR tmp[120];
    UINT k = n < 118 ? n : 118;
    memcpy(tmp, v, k * sizeof(WCHAR));
    tmp[k] = 0;
    cerr(c, XE_XS_BAD_PREFIX, tmp, nullptr, nullptr);
    return FALSE;
}

/* the check that a select is a node set, before anything runs. */
static BOOL select_node_set(Ctx *c, XNode *attr, XpExpr *x)
{
    if (x && xp_not_nodeset(x)) {
        cerr(c, XE_NOT_DOM_NODE, nullptr, nullptr, nullptr);
        c->s->error.suffix = xse_marked_text(attr->value ? attr->value : L"", attr->length, 0, attr->length);
        return FALSE;
    }
    return TRUE;
}

static HRESULT compile_sorts_and_params(Ctx *c, XNode *el, XsInst *inst, BOOL allow_sort, BOOL allow_params)
{
    XsInst **sorts = &inst->sorts, **params = &inst->params;
    for (XNode *k = el->first; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
            continue;
        if (k->type != XN_ELEMENT)
            return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, L"PCDATA nodes");
        if (is_xsl(k) && local_is(k, "sort") && allow_sort) {
            c->sort_ok = TRUE;
            HRESULT hr = compile_element(c, k, &sorts);
            c->sort_ok = FALSE;
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (is_xsl(k) && local_is(k, "with-param") && allow_params) {
            HRESULT hr = compile_element(c, k, &params);
            if (FAILED(hr))
                return hr;
            continue;
        }
        return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, k->qname);
    }
    if (c->s->v6) {
        for (XsInst *p = inst->params; p; p = p->next)
            for (XsInst *q = p->next; q; q = q->next)
                if (name_same(&p->name, &q->name))
                    return cerr(c, XE_XS_PARAM_TWICE, q->name.qname, nullptr, nullptr);
    }
    return S_OK;
}

/* a variable, a parameter or a with-param: name, then select or content. */
static HRESULT compile_variable(Ctx *c, XNode *el, XsInst ***tail, int op)
{
    XsInst *inst = new_inst(c, op);
    if (!attr_qname(c, el, "name", &inst->name))
        return E_FAIL;
    XNode *sel = attr_node(el, "select");
    BOOL has_content = FALSE;
    for (XNode *k = el->first; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
            continue;
        has_content = TRUE;
    }
    if (sel && has_content)
        return cerr(c, XE_XS_VAR_BOTH, inst->name.qname, nullptr, nullptr);
    if (sel) {
        inst->has_select = TRUE;
        inst->select = attr_expr(c, sel, 0);
        if (!inst->select)
            return E_FAIL;
    } else if (has_content) {
        UINT mark = c->nvars;
        HRESULT hr = compile_children(c, el, &inst->body);
        c->nvars = mark;
        if (FAILED(hr))
            return hr;
    }
    if (op != XO_WITH_PARAM && !declare_local(c, &inst->name))
        return E_FAIL;
    append(tail, inst);
    return S_OK;
}

static BOOL pattern_attr(Ctx *c, XNode *attr, XpExpr **out)
{
    XpExpr *x = compile_expr(c, attr->value ? attr->value : L"", attr->length, XPF_PATTERN);
    if (!x)
        return FALSE;
    XpExpr *alt[64];
    UINT n = xp_alternatives(x, alt, 64);
    for (UINT i = 0; i < n; i++) {
        UINT bad_start, bad_end;
        BOOL ok = xp_pattern_ok(alt[i], &bad_start, &bad_end);
        BOOL not_set = xp_not_nodeset(alt[i]);
        xp_free(alt[i]);
        if (!ok) {
            if (not_set)
                cerr(c, c->s->v6 ? 0xC00CE618u : XE_NOT_DOM_NODE, nullptr, nullptr, nullptr);
            else
                cerr(c, 0xC00CE60Eu, nullptr, nullptr, nullptr);
            if (bad_end > bad_start && c->s->v6)
                c->s->error.suffix = xse_marked_text(attr->value ? attr->value : L"", attr->length, bad_start, bad_end);
            else
                c->s->error.suffix = xse_marked_text(attr->value ? attr->value : L"", attr->length, 0, attr->length);
            return FALSE;
        }
    }
    *out = x;
    return TRUE;
}

static BOOL is_forward(Ctx *c, XNode *el)
{
    if (c->forward)
        return TRUE;
    for (XNode *n = el; n; n = n->parent) {
        if (n->type != XN_ELEMENT)
            continue;
        XNode *v = xd_attr_find_ns(n, L"version", 7, XSLT_NS);
        if (v && xd_parse_number(v->value, v->length) > 1.0)
            return TRUE;
    }
    return FALSE;
}

static HRESULT compile_lre(Ctx *c, XNode *el, XsInst ***tail);
static HRESULT compile_xsl(Ctx *c, XNode *el, XsInst ***tail);

/* the prefixes an attribute lists, as namespace uris. */
static void add_uri_list(Ctx *c, XNode *attr, const WCHAR ***list, UINT *n, UINT *cap, BOOL excluded)
{
    const WCHAR *v = attr->value ? attr->value : L"";
    UINT len = attr->length, i = 0;
    while (i < len) {
        while (i < len && xd_is_space(v[i]))
            i++;
        UINT start = i;
        while (i < len && !xd_is_space(v[i]))
            i++;
        if (i == start)
            break;
        BOOL found;
        const WCHAR *uri;
        if (i - start == 8 && is_lit(v + start, 8, "#default")) {
            uri = scope_lookup(c->scope, L"", 0, &found);
            if (!found)
                continue;
        } else {
            uri = scope_lookup(c->scope, v + start, i - start, &found);
        }
        if (!found) {
            WCHAR p[80];
            UINT m = i - start < 78 ? i - start : 78;
            memcpy(p, v + start, m * sizeof(WCHAR));
            p[m] = 0;
            cerr(c, XE_XS_BAD_PREFIX, p, nullptr, nullptr);
            return;
        }
        const WCHAR **slot = XS_PUSH(c->s, *list, *n, *cap, const WCHAR *);
        *slot = uri;
        (void)excluded;
    }
}

static BOOL uri_listed(const WCHAR **list, UINT n, const WCHAR *uri)
{
    for (UINT i = 0; i < n; i++)
        if (!wcscmp(list[i], uri))
            return TRUE;
    return FALSE;
}

static HRESULT compile_element_inner(Ctx *c, XNode *el, XsInst ***tail);

/* an instruction followed by white space the parser dropped remembers it, for the indent of the output. */
static HRESULT compile_element(Ctx *c, XNode *el, XsInst ***tail)
{
    XsInst **before = *tail;
    HRESULT hr = compile_element_inner(c, el, tail);
    if (SUCCEEDED(hr) && *tail != before && (el->flags & XF_WS_AFTER))
        ((XsInst *)((char *)*tail - offsetof(XsInst, next)))->ws_after = TRUE;
    return hr;
}

static HRESULT compile_element_inner(Ctx *c, XNode *el, XsInst ***tail)
{
    XsScope *saved_scope = c->scope;
    UINT saved_excluded = c->nexcluded, saved_extension = c->nextension;
    BOOL saved_forward = c->forward;
    c->scope = make_scope(c, el);
    c->node = el;
    /* the prefixes this element excludes or declares as extensions. */
    XNode *ex = is_xsl(el) ? attr_node(el, "exclude-result-prefixes")
                           : xd_attr_find_ns(el, L"exclude-result-prefixes", 22, XSLT_NS);
    if (ex)
        add_uri_list(c, ex, &c->excluded, &c->nexcluded, &c->capexcluded, TRUE);
    XNode *ext = is_xsl(el) ? attr_node(el, "extension-element-prefixes")
                            : xd_attr_find_ns(el, L"extension-element-prefixes", 26, XSLT_NS);
    if (ext)
        add_uri_list(c, ext, &c->extension, &c->nextension, &c->capextension, FALSE);
    if (failed(c))
        return E_FAIL;
    if (!is_xsl(el)) {
        XNode *v = xd_attr_find_ns(el, L"version", 7, XSLT_NS);
        if (v && xd_parse_number(v->value, v->length) > 1.0)
            c->forward = TRUE;
    }
    HRESULT hr;
    if (++c->depth > 500) {
        hr = cerr(c, XE_XS_STACK, nullptr, nullptr, nullptr);
    } else if (is_xsl(el)) {
        hr = compile_xsl(c, el, tail);
    } else if (el->uri && uri_listed(c->extension, c->nextension, el->uri)) {
        hr = cerr(c, XE_XS_NOT_EXTENSION, el->qname, nullptr, nullptr);
        /* an extension element that is not known stands for its xsl:fallback children. */
        for (XNode *k = el->first; k; k = k->next)
            if (k->type == XN_ELEMENT && is_xsl(k) && local_is(k, "fallback")) {
                c->s->error.id = 0;
                xse_clear(&c->s->error);
                XsInst *f = new_inst(c, XO_FALLBACK);
                hr = compile_children(c, k, &f->body);
                append(tail, f);
            }
    } else {
        hr = compile_lre(c, el, tail);
    }
    c->depth--;
    c->scope = saved_scope;
    c->nexcluded = saved_excluded;
    c->nextension = saved_extension;
    c->forward = saved_forward;
    return hr;
}

static HRESULT compile_children(Ctx *c, XNode *parent, XsInst **list)
{
    XsInst **tail = list;
    UINT mark = c->nvars;
    BOOL in_text = parent->type == XN_ELEMENT && is_xsl(parent) && local_is(parent, "text");
    for (XNode *k = parent->first; k; k = k->next) {
        if (k->type == XN_TEXT || k->type == XN_CDATA) {
            if (!in_text && stripped_text(k))
                continue;
            if (!k->length)
                continue;
            XsInst *t = new_inst(c, XO_TEXT);
            t->text = xs_dup(c->s, k->value, k->length);
            t->tlen = k->length;
            append(&tail, t);
        } else if (k->type == XN_ELEMENT) {
            HRESULT hr = compile_element(c, k, &tail);
            if (FAILED(hr))
                return hr;
        }
    }
    c->nvars = mark;
    return S_OK;
}

/* the namespace declarations of a literal element and the attribute sets it uses. */
static HRESULT use_attribute_sets(Ctx *c, XsInst *inst, XNode *attr)
{
    const WCHAR *v = attr->value ? attr->value : L"";
    UINT len = attr->length, i = 0, cap = 0;
    while (i < len) {
        while (i < len && xd_is_space(v[i]))
            i++;
        UINT start = i;
        while (i < len && !xd_is_space(v[i]))
            i++;
        if (i == start)
            break;
        XsName *n = XS_PUSH(c->s, inst->sets, inst->nsets, cap, XsName);
        if (!resolve_qname(c, v + start, i - start, FALSE, n))
            return cerr(c, XE_UNDECLARED_PREFIX, nullptr, nullptr, nullptr);
        XsName *u = XS_PUSH(c->s, c->used_sets, c->nused_sets, c->capused_sets, XsName);
        *u = *n;
    }
    return S_OK;
}

static const WCHAR *alias_result(Ctx *c, const WCHAR *uri, const WCHAR **prefix)
{
    for (UINT i = 0; i < c->s->naliases; i++)
        if (!wcscmp(c->s->aliases[i].uri, uri)) {
            *prefix = c->s->alias_target[i].prefix;
            return c->s->alias_target[i].uri;
        }
    return nullptr;
}

static HRESULT compile_lre(Ctx *c, XNode *el, XsInst ***tail)
{
    XsInst *inst = new_inst(c, XO_LRE);
    inst->ws_inside = (el->flags & XF_WS_INSIDE) != 0;
    inst->name.qname = xs_dup(c->s, el->qname, xd_atom_len(el->qname));
    inst->name.qlen = xd_atom_len(el->qname);
    inst->name.local = inst->name.qname + el->local;
    inst->name.llen = xd_local_length(el);
    if (el->local) {
        inst->name.prefix = inst->name.qname;
        inst->name.plen = el->local - 1;
    }
    inst->name.uri = el->uri ? xs_dup(c->s, el->uri, xd_wlen(el->uri)) : nullptr;
    /* a namespace alias changes the namespace of the element. */
    if (inst->name.uri) {
        const WCHAR *prefix = nullptr;
        const WCHAR *to = alias_result(c, inst->name.uri, &prefix);
        if (to) {
            XBuf q = {};
            if (prefix && *prefix) {
                xb_put(&q, prefix, xd_wlen(prefix));
                xb_putc(&q, ':');
            }
            xb_put(&q, inst->name.local, inst->name.llen);
            inst->name.qname = xs_dup(c->s, q.s, q.length);
            inst->name.qlen = q.length;
            xb_free(&q);
            inst->name.prefix = prefix && *prefix ? inst->name.qname : nullptr;
            inst->name.plen = prefix ? (UINT)xd_wlen(prefix) : 0;
            inst->name.local = inst->name.qname + (prefix && *prefix ? inst->name.plen + 1 : 0);
            inst->name.uri = *to ? to : nullptr;
        }
    }
    if (el->flags & XF_EXPLICIT)
        inst->doe = TRUE; /* written with an end tag even when empty */
    UINT acap = 0;
    for (XNode *a = el->attrs; a; a = a->next) {
        if (xd_is_xmlns_attr(a))
            continue;
        if (a->uri && !wcscmp(a->uri, XSLT_NS)) {
            if (local_is(a, "use-attribute-sets")) {
                HRESULT hr = use_attribute_sets(c, inst, a);
                if (FAILED(hr))
                    return hr;
            }
            continue;
        }
        XsAttr *out = XS_PUSH(c->s, inst->attrs, inst->nattrs, acap, XsAttr);
        out->name.qname = xs_dup(c->s, a->qname, xd_atom_len(a->qname));
        out->name.qlen = xd_atom_len(a->qname);
        out->name.local = out->name.qname + a->local;
        out->name.llen = xd_local_length(a);
        if (a->local) {
            out->name.prefix = out->name.qname;
            out->name.plen = a->local - 1;
        }
        out->name.uri = a->uri ? xs_dup(c->s, a->uri, xd_wlen(a->uri)) : nullptr;
        out->value = parse_avt(c, a->value ? a->value : L"", a->length);
        if (!out->value)
            return E_FAIL;
    }
    /* the namespaces in scope, except those that stay out of the result. */
    flatten_scope(c, c->scope);
    UINT ncap = 0;
    if (c->scope) {
        for (UINT i = 0; i < c->scope->nflat; i++) {
            const XNs *ns = &c->scope->flat[i];
            if (!wcscmp(ns->uri, XSLT_NS) || uri_listed(c->excluded, c->nexcluded, ns->uri) ||
                uri_listed(c->extension, c->nextension, ns->uri))
                continue;
            const WCHAR *prefix = ns->prefix;
            const WCHAR *uri = ns->uri;
            const WCHAR *to = alias_result(c, uri, &prefix);
            if (to)
                uri = to;
            XNs *d = XS_PUSH(c->s, inst->nsdecl, inst->nnsdecl, ncap, XNs);
            d->prefix = (WCHAR *)prefix;
            d->uri = (WCHAR *)uri;
        }
    }
    UINT mark = c->nvars;
    HRESULT hr = compile_children(c, el, &inst->body);
    c->nvars = mark;
    if (FAILED(hr))
        return hr;
    append(tail, inst);
    return S_OK;
}

/* the content of an element that has none. */
static HRESULT empty_content(Ctx *c, XNode *el)
{
    for (XNode *k = el->first; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
            continue;
        return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, child_label(k));
    }
    return S_OK;
}

static XsInst *simple_inst(Ctx *c, int op, XNode *el, XsInst ***tail)
{
    XsInst *inst = new_inst(c, op);
    append(tail, inst);
    return inst;
}

static BOOL sort_value(Ctx *c, XNode *el, const char *name, int index, XsInst *inst, const char *a, const char *b)
{
    XNode *n = attr_node(el, name);
    if (!n)
        return TRUE;
    inst->avt[index] = parse_avt(c, n->value ? n->value : L"", n->length);
    if (!inst->avt[index])
        return FALSE;
    /* a constant value is checked now. */
    if (inst->avt[index]->n == 1 && !inst->avt[index]->parts[0].expr) {
        const WCHAR *v = inst->avt[index]->parts[0].lit;
        UINT l = inst->avt[index]->parts[0].len;
        if (!is_lit(v, l, a) && !is_lit(v, l, b)) {
            WCHAR w[40];
            UINT k = 0;
            for (; name[k]; k++)
                w[k] = (UCHAR)name[k];
            w[k] = 0;
            WCHAR wa[40], wb[40];
            for (k = 0; a[k]; k++)
                wa[k] = (UCHAR)a[k];
            wa[k] = 0;
            for (k = 0; b[k]; k++)
                wb[k] = (UCHAR)b[k];
            wb[k] = 0;
            if (c->s->v6) {
                WCHAR val[80];
                UINT m = l < 78 ? l : 78;
                memcpy(val, v, m * sizeof(WCHAR));
                val[m] = 0;
                cerr(c, XE_XS_ATTR_BAD, w, val, nullptr);
            } else {
                cerr(c, XE_XS_ATTR_ONLY, w, wa, wb);
            }
            return FALSE;
        }
    }
    return TRUE;
}

static HRESULT compile_xsl(Ctx *c, XNode *el, XsInst ***tail)
{
    if (!check_attrs(c, el))
        return E_FAIL;
    const WCHAR *l = xd_local(el);
    UINT ll = xd_local_length(el);
#define IS(name) is_lit(l, ll, name)
    if (IS("apply-templates")) {
        XsInst *inst = new_inst(c, XO_APPLY);
        XNode *sel = attr_node(el, "select");
        if (sel) {
            inst->select = attr_expr(c, sel, 0);
            if (!inst->select || !select_node_set(c, sel, inst->select))
                return E_FAIL;
            inst->has_select = TRUE;
        }
        XNode *mode = attr_node(el, "mode");
        if (mode) {
            if (!attr_qname(c, el, "mode", &inst->mode))
                return E_FAIL;
            inst->has_mode = TRUE;
        }
        HRESULT hr = compile_sorts_and_params(c, el, inst, TRUE, TRUE);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("apply-imports")) {
        if (c->in_for_each || (c->s->v6 && (!c->tmpl || !c->tmpl->has_match)))
            return cerr(c, XE_XS_IMPORTS_FOREACH, nullptr, nullptr, nullptr);
        HRESULT hr = empty_content(c, el);
        if (FAILED(hr))
            return hr;
        simple_inst(c, XO_APPLY_IMPORTS, el, tail);
        return S_OK;
    }
    if (IS("call-template")) {
        XsInst *inst = new_inst(c, XO_CALL);
        if (!attr_qname(c, el, "name", &inst->name))
            return E_FAIL;
        HRESULT hr = compile_sorts_and_params(c, el, inst, FALSE, TRUE);
        if (FAILED(hr))
            return hr;
        XsInst **slot = XS_PUSH(c->s, c->calls, c->ncalls, c->capcalls, XsInst *);
        *slot = inst;
        append(tail, inst);
        return S_OK;
    }
    if (IS("for-each")) {
        XsInst *inst = new_inst(c, XO_FOR_EACH);
        XNode *sel = attr_node(el, "select");
        inst->select = attr_expr(c, sel, 0);
        if (!inst->select || !select_node_set(c, sel, inst->select))
            return E_FAIL;
        BOOL saved = c->in_for_each;
        c->in_for_each = TRUE;
        /* the sort keys come first. */
        XNode *k = el->first;
        XsInst **sorts = &inst->sorts;
        for (; k; k = k->next) {
            if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
                continue;
            if (k->type == XN_ELEMENT && is_xsl(k) && local_is(k, "sort")) {
                c->sort_ok = TRUE;
                HRESULT hr = compile_element(c, k, &sorts);
                c->sort_ok = FALSE;
                if (FAILED(hr)) {
                    c->in_for_each = saved;
                    return hr;
                }
                continue;
            }
            break;
        }
        XsInst **body = &inst->body;
        UINT mark = c->nvars;
        XsInst **bt = body;
        for (; k; k = k->next) {
            if (k->type == XN_TEXT || k->type == XN_CDATA) {
                if (stripped_text(k) || !k->length)
                    continue;
                XsInst *t = new_inst(c, XO_TEXT);
                t->text = xs_dup(c->s, k->value, k->length);
                t->tlen = k->length;
                append(&bt, t);
            } else if (k->type == XN_ELEMENT) {
                HRESULT hr = compile_element(c, k, &bt);
                if (FAILED(hr)) {
                    c->in_for_each = saved;
                    return hr;
                }
            }
        }
        c->nvars = mark;
        c->in_for_each = saved;
        append(tail, inst);
        return S_OK;
    }
    if (IS("value-of") || IS("copy-of")) {
        XsInst *inst = new_inst(c, IS("value-of") ? XO_VALUE_OF : XO_COPY_OF);
        inst->select = attr_expr(c, attr_node(el, "select"), 0);
        if (!inst->select)
            return E_FAIL;
        if (IS("value-of")) {
            int doe = 0;
            if (!yes_no(c, el, "disable-output-escaping", &doe))
                return E_FAIL;
            inst->doe = doe == 1;
        }
        HRESULT hr = empty_content(c, el);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("if")) {
        XsInst *inst = new_inst(c, XO_IF);
        inst->test = attr_expr(c, attr_node(el, "test"), 0);
        if (!inst->test)
            return E_FAIL;
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("choose")) {
        XsInst *inst = new_inst(c, XO_CHOOSE);
        XsInst **bt = &inst->body;
        BOOL seen_otherwise = FALSE, seen_when = FALSE;
        for (XNode *k = el->first; k; k = k->next) {
            if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
                continue;
            if (k->type != XN_ELEMENT)
                return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, L"PCDATA nodes");
            if (!is_xsl(k) || (!local_is(k, "when") && !local_is(k, "otherwise")))
                return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, k->qname);
            if (local_is(k, "when")) {
                if (seen_otherwise)
                    return cerr(c, XE_XS_KEYWORD_HERE, k->qname, nullptr, nullptr);
                seen_when = TRUE;
            } else {
                if (seen_otherwise)
                    return cerr(c, XE_XS_KEYWORD_HERE, k->qname, nullptr, nullptr);
                seen_otherwise = TRUE;
            }
            HRESULT hr = compile_element(c, k, &bt);
            if (FAILED(hr))
                return hr;
        }
        if (!seen_when)
            return cerr(c, XE_XS_CHOOSE_EMPTY, nullptr, nullptr, nullptr);
        append(tail, inst);
        return S_OK;
    }
    if (IS("when") || IS("otherwise")) {
        /* only reached through choose. */
        BOOL when = IS("when");
        if (!el->parent || !is_xsl(el->parent) || !local_is(el->parent, "choose"))
            return cerr(c, XE_XS_KEYWORD_HERE, el->qname, nullptr, nullptr);
        XsInst *inst = new_inst(c, when ? XO_WHEN : XO_OTHERWISE);
        if (when) {
            inst->test = attr_expr(c, attr_node(el, "test"), 0);
            if (!inst->test)
                return E_FAIL;
        }
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("variable")) {
        return compile_variable(c, el, tail, XO_VARIABLE);
    }
    if (IS("with-param")) {
        if (!el->parent || !is_xsl(el->parent) ||
            (!local_is(el->parent, "apply-templates") && !local_is(el->parent, "call-template")))
            return cerr(c, XE_XS_KEYWORD_HERE, el->qname, nullptr, nullptr);
        return compile_variable(c, el, tail, XO_WITH_PARAM);
    }
    if (IS("sort")) {
        if (!c->sort_ok || !el->parent || !is_xsl(el->parent) ||
            (!local_is(el->parent, "apply-templates") && !local_is(el->parent, "for-each")))
            return cerr(c, XE_XS_KEYWORD_HERE, el->qname, nullptr, nullptr);
        c->sort_ok = FALSE;
        XsInst *inst = new_inst(c, XO_SORT);
        XNode *sel = attr_node(el, "select");
        if (sel) {
            inst->select = attr_expr(c, sel, 0);
            if (!inst->select)
                return E_FAIL;
        }
        if (attr_node(el, "lang")) {
            inst->avt[0] = attr_avt(c, el, "lang");
            if (!inst->avt[0])
                return E_FAIL;
        }
        if (!sort_value(c, el, "data-type", 1, inst, "text", "number") && failed(c))
            return E_FAIL;
        if (!sort_value(c, el, "order", 2, inst, "ascending", "descending") && failed(c))
            return E_FAIL;
        if (!sort_value(c, el, "case-order", 3, inst, "upper-first", "lower-first") && failed(c))
            return E_FAIL;
        HRESULT hr = empty_content(c, el);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("text")) {
        XsInst *inst = new_inst(c, XO_TEXT);
        int doe = 0;
        if (!yes_no(c, el, "disable-output-escaping", &doe))
            return E_FAIL;
        inst->doe = doe == 1;
        XBuf b = {};
        for (XNode *k = el->first; k; k = k->next) {
            if (k->type == XN_TEXT || k->type == XN_CDATA)
                xb_put(&b, k->value ? k->value : L"", k->length);
            else if (k->type == XN_ELEMENT) {
                xb_free(&b);
                return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, k->qname);
            }
        }
        if (b.length) {
            inst->text = xs_dup(c->s, b.s, b.length);
            inst->tlen = b.length;
        } else if (el->flags & XF_WS_INSIDE) {
            /* the white space the document dropped is one space. */
            inst->text = L" ";
            inst->tlen = 1;
        } else {
            inst->text = L"";
        }
        xb_free(&b);
        append(tail, inst);
        return S_OK;
    }
    if (IS("element") || IS("attribute")) {
        BOOL elem = IS("element");
        XsInst *inst = new_inst(c, elem ? XO_ELEMENT : XO_ATTRIBUTE);
        if (!attr_node(el, "name"))
            return cerr(c, 0xC00CE20Eu, elem ? L"ELEMENT" : L"ATTRIBUTE", nullptr, nullptr);
        inst->avt[0] = attr_avt(c, el, "name");
        if (!inst->avt[0])
            return E_FAIL;
        if (attr_node(el, "namespace")) {
            inst->avt[1] = attr_avt(c, el, "namespace");
            if (!inst->avt[1])
                return E_FAIL;
        }
        XNode *sets = attr_node(el, "use-attribute-sets");
        if (sets && elem) {
            HRESULT hr = use_attribute_sets(c, inst, sets);
            if (FAILED(hr))
                return hr;
        }
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("comment")) {
        XsInst *inst = new_inst(c, XO_COMMENT);
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("processing-instruction")) {
        XsInst *inst = new_inst(c, XO_PI);
        inst->avt[0] = attr_avt(c, el, "name");
        if (!inst->avt[0])
            return E_FAIL;
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("copy")) {
        XsInst *inst = new_inst(c, XO_COPY);
        XNode *sets = attr_node(el, "use-attribute-sets");
        if (sets) {
            HRESULT hr = use_attribute_sets(c, inst, sets);
            if (FAILED(hr))
                return hr;
        }
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("number")) {
        XsInst *inst = new_inst(c, XO_NUMBER);
        UINT n;
        const WCHAR *lv = attr_value(el, "level", &n);
        inst->level = 0;
        if (lv) {
            trim(&lv, &n);
            if (is_lit(lv, n, "single"))
                inst->level = 0;
            else if (is_lit(lv, n, "multiple"))
                inst->level = 1;
            else if (is_lit(lv, n, "any"))
                inst->level = 2;
            else {
                WCHAR val[80];
                UINT m = n < 78 ? n : 78;
                memcpy(val, lv, m * sizeof(WCHAR));
                val[m] = 0;
                if (c->s->v6)
                    return cerr(c, XE_XS_ATTR_BAD, L"level", val, nullptr);
                return cerr(c, XE_XS_ATTR_ONLY, L"level", L"single", L"multiple");
            }
        }
        XNode *a;
        if ((a = attr_node(el, "count")) && !pattern_attr(c, a, &inst->count))
            return E_FAIL;
        if ((a = attr_node(el, "from")) && !pattern_attr(c, a, &inst->from))
            return E_FAIL;
        if ((a = attr_node(el, "value"))) {
            inst->select = attr_expr(c, a, 0);
            if (!inst->select)
                return E_FAIL;
            inst->has_select = TRUE;
        }
        static const char *names[] = {"format", "lang", "letter-value", "grouping-separator", "grouping-size"};
        for (int i = 0; i < 5; i++)
            if (attr_node(el, names[i])) {
                inst->avt[i] = attr_avt(c, el, names[i]);
                if (!inst->avt[i])
                    return E_FAIL;
            }
        HRESULT hr = empty_content(c, el);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("message")) {
        XsInst *inst = new_inst(c, XO_MESSAGE);
        int term = 0;
        if (!yes_no(c, el, "terminate", &term))
            return E_FAIL;
        inst->level = term;
        HRESULT hr = compile_children(c, el, &inst->body);
        if (FAILED(hr))
            return hr;
        append(tail, inst);
        return S_OK;
    }
    if (IS("fallback")) {
        /* a fallback outside an unknown instruction does nothing. */
        return S_OK;
    }
#undef IS
    /* an instruction of a later version stands for its fallback. */
    if (is_forward(c, el)) {
        XsInst *inst = new_inst(c, XO_FALLBACK);
        inst->fallback_only = TRUE;
        inst->name.qname = xs_dup(c->s, el->qname, xd_atom_len(el->qname));
        for (XNode *k = el->first; k; k = k->next)
            if (k->type == XN_ELEMENT && is_xsl(k) && local_is(k, "fallback")) {
                inst->fallback_only = FALSE;
                XsInst **bt = &inst->body;
                XsInst *sub = nullptr;
                HRESULT hr = compile_children(c, k, &sub);
                if (FAILED(hr))
                    return hr;
                *bt = sub;
            }
        append(tail, inst);
        return S_OK;
    }
    return cerr(c, XE_XS_KEYWORD_HERE, el->qname, nullptr, nullptr);
}

/* ------------------------------------------------------------------ locations */

static BOOL has_scheme(const WCHAR *s)
{
    for (const WCHAR *p = s; *p; p++) {
        if (*p == ':')
            return p - s > 1;
        if (*p == '/' || *p == '\\' || *p == '?' || *p == '#')
            return FALSE;
    }
    return FALSE;
}

/* a location relative to the one of the stylesheet that names it. */
WCHAR *xs_join_location(const WCHAR *base, const WCHAR *href, UINT hlen)
{
    WCHAR *rel = xd_wcsdup(href, hlen);
    if (!rel)
        return nullptr;
    BOOL drive = hlen > 1 && rel[1] == ':';
    if (has_scheme(rel) || drive || (hlen && (rel[0] == '\\' || rel[0] == '/')) || !base || !*base)
        return rel;
    XBuf b = {};
    UINT cut = xd_wlen(base);
    while (cut && base[cut - 1] != '/' && base[cut - 1] != '\\')
        cut--;
    xb_put(&b, base, cut);
    xb_put(&b, rel, hlen);
    xd_free(rel);
    if (b.failed) {
        xb_free(&b);
        return nullptr;
    }
    /* dot segments. */
    WCHAR *s = b.s;
    UINT n = b.length, start = 0;
    while (start < n && s[start] != ':')
        start++;
    start = start < n ? start + 1 : 0;
    while (start < n && s[start] == '/')
        start++;
    UINT out = start;
    for (UINT i = start; i < n;) {
        UINT j = i;
        while (j < n && s[j] != '/' && s[j] != '\\')
            j++;
        UINT seg = j - i;
        if (seg == 1 && s[i] == '.') {
        } else if (seg == 2 && s[i] == '.' && s[i + 1] == '.' && out > start) {
            UINT k = out;
            if (k > start && (s[k - 1] == '/' || s[k - 1] == '\\'))
                k--;
            while (k > start && s[k - 1] != '/' && s[k - 1] != '\\')
                k--;
            out = k;
        } else {
            memmove(s + out, s + i, (SIZE_T)(j < n ? seg + 1 : seg) * sizeof(WCHAR));
            out += j < n ? seg + 1 : seg;
        }
        i = j < n ? j + 1 : n;
    }
    s[out] = 0;
    return s;
}

/* ------------------------------------------------------------------ the top level */

static HRESULT compile_module_root(Ctx *c, XNode *root, XsModule *mod, BOOL main);

static HRESULT load_module(Ctx *c, XNode *el, XDoc **out)
{
    UINT n;
    const WCHAR *href = attr_value(el, "href", &n);
    WCHAR *location = xs_join_location(c->base_url, href, n);
    if (!location)
        return E_OUTOFMEMORY;
    XDoc *d = xd_doc_new(0);
    if (!d) {
        xd_free(location);
        return E_OUTOFMEMORY;
    }
    BOOL ok = xd_load_file(d, location);
    if (!ok) {
        if (d->err.code && d->err.reason) {
            UINT code = (UINT)d->err.code;
            if (code == XE_FILE_NOT_FOUND || (code & 0xFFFF0000u) == 0x80070000u) {
                UINT first = 0;
                while (d->err.reason[first] && !(d->err.reason[first] == '\n'))
                    first++;
                if (d->err.reason[first])
                    first++;
                WCHAR *line = xd_wcsdup(d->err.reason, first);
                xse_set_text(&c->s->error, (HRESULT)code, line);
                xd_free(line);
            } else {
                cerr(c, XE_XS_PARSE_ERROR, d->url ? d->url : location, d->err.reason, nullptr);
            }
        } else {
            cerr(c, XE_XS_BAD_URL, location, nullptr, nullptr);
        }
        xn_release(&d->node);
        xd_free(location);
        return E_FAIL;
    }
    xd_free(location);
    *out = d;
    return S_OK;
}

static HRESULT include_module(Ctx *c, XNode *el, XsModule *mod, BOOL is_import)
{
    /* msxml6 reads other stylesheets only when the document of the stylesheet may resolve externals. */
    if (c->s->v6 && !(c->s->resolve_externals))
        return S_OK;
    XDoc *d;
    HRESULT hr = load_module(c, el, &d);
    if (FAILED(hr))
        return hr;
    XNode *root = xd_document_element(d);
    if (!root) {
        xn_release(&d->node);
        return cerr(c, XE_XS_NO_DOCELEM, nullptr, nullptr, nullptr);
    }
    const WCHAR *url = d->url ? d->url : L"";
    for (UINT i = 0; i < c->nstack; i++)
        if (!xd_wcsicmp(c->stack[i], url)) {
            WCHAR *copy = xd_wcsdup(url, xd_wlen(url));
            cerr(c, XE_XS_INCLUDE_SELF, copy, nullptr, nullptr);
            xd_free(copy);
            xn_release(&d->node);
            return E_FAIL;
        }
    const WCHAR **slot = XS_PUSH(c->s, c->stack, c->nstack, c->capstack, const WCHAR *);
    *slot = xs_dup(c->s, url, xd_wlen(url));
    const WCHAR *saved_base = c->base_url;
    XsScope *saved_scope = c->scope;
    UINT saved_excluded = c->nexcluded, saved_extension = c->nextension;
    BOOL saved_forward = c->forward;
    c->base_url = *slot;
    c->scope = nullptr;
    if (is_import) {
        XsModule *sub = xs_alloc(c->s, sizeof(XsModule));
        sub->url = xs_dup(c->s, url, xd_wlen(url));
        hr = compile_module_root(c, root, sub, FALSE);
        if (SUCCEEDED(hr) && sub->import_min < mod->import_min)
            mod->import_min = sub->import_min;
        if (SUCCEEDED(hr) && sub->precedence < mod->import_min)
            mod->import_min = sub->precedence;
    } else {
        hr = compile_module_root(c, root, mod, FALSE);
    }
    c->base_url = saved_base;
    c->scope = saved_scope;
    c->nexcluded = saved_excluded;
    c->nextension = saved_extension;
    c->forward = saved_forward;
    c->nstack--;
    xn_release(&d->node);
    return hr;
}

static void add_space(Ctx *c, XNode *el, BOOL strip, XsModule *mod)
{
    UINT n;
    const WCHAR *v = attr_value(el, "elements", &n);
    UINT i = 0;
    while (i < n) {
        while (i < n && xd_is_space(v[i]))
            i++;
        UINT start = i;
        while (i < n && !xd_is_space(v[i]))
            i++;
        if (i == start)
            break;
        XsSpace *sp = XS_PUSH(c->s, c->s->spaces, c->s->nspaces, c->s->capspaces, XsSpace);
        sp->strip = strip;
        sp->precedence = mod->precedence;
        sp->order = c->s->order++;
        if (i - start == 1 && v[start] == '*') {
            sp->kind = 2;
        } else if (i - start > 2 && v[i - 1] == '*' && v[i - 2] == ':') {
            BOOL f;
            sp->uri = scope_lookup(c->scope, v + start, i - start - 2, &f);
            sp->kind = 1;
            if (!f) {
                cerr(c, XE_XS_BAD_PREFIX, nullptr, nullptr, nullptr);
                return;
            }
        } else {
            XsName nm;
            if (!resolve_qname(c, v + start, i - start, FALSE, &nm)) {
                cerr(c, XE_XS_BAD_PREFIX, nullptr, nullptr, nullptr);
                return;
            }
            sp->uri = nm.uri;
            sp->local = nm.local;
            sp->llen = nm.llen;
            sp->kind = 0;
        }
        if (strip)
            c->s->has_strip = TRUE;
    }
}

static HRESULT compile_output(Ctx *c, XNode *el)
{
    XsOutput *o = &c->s->output;
    UINT n;
    const WCHAR *v = attr_value(el, "method", &n);
    if (v) {
        trim(&v, &n);
        if (is_lit(v, n, "xml"))
            o->method = 1;
        else if (is_lit(v, n, "html"))
            o->method = 2;
        else if (is_lit(v, n, "text"))
            o->method = 3;
        else {
            BOOL qname = FALSE;
            for (UINT i = 0; i < n; i++)
                if (v[i] == ':')
                    qname = TRUE;
            if (!qname) {
                WCHAR val[80];
                UINT m = n < 78 ? n : 78;
                memcpy(val, v, m * sizeof(WCHAR));
                val[m] = 0;
                return cerr(c, XE_XS_ATTR_BAD, L"method", val, nullptr);
            }
            o->method = 1;
        }
    }
    if ((v = attr_value(el, "version", &n)))
        o->version = xs_dup(c->s, v, n);
    if ((v = attr_value(el, "encoding", &n)))
        o->encoding = xs_dup(c->s, v, n);
    if (!yes_no(c, el, "omit-xml-declaration", &o->omit_decl))
        return E_FAIL;
    if (!yes_no(c, el, "indent", &o->indent))
        return E_FAIL;
    if ((v = attr_value(el, "standalone", &n))) {
        int sa = 0;
        if (!yes_no(c, el, "standalone", &sa))
            return E_FAIL;
        o->standalone = xs_dup(c->s, sa ? L"yes" : L"no", sa ? 3 : 2);
    }
    if ((v = attr_value(el, "doctype-public", &n)))
        o->doctype_public = xs_dup(c->s, v, n);
    if ((v = attr_value(el, "doctype-system", &n)))
        o->doctype_system = xs_dup(c->s, v, n);
    if ((v = attr_value(el, "media-type", &n)))
        o->media_type = xs_dup(c->s, v, n);
    if ((v = attr_value(el, "cdata-section-elements", &n))) {
        UINT i = 0;
        while (i < n) {
            while (i < n && xd_is_space(v[i]))
                i++;
            UINT start = i;
            while (i < n && !xd_is_space(v[i]))
                i++;
            if (i == start)
                break;
            XsName *nm = XS_PUSH(c->s, o->cdata_names, o->ncdata, o->capcdata, XsName);
            if (!resolve_qname(c, v + start, i - start, FALSE, nm))
                return cerr(c, XE_XS_BAD_PREFIX, nullptr, nullptr, nullptr);
        }
    }
    return S_OK;
}

static HRESULT compile_decimal(Ctx *c, XNode *el)
{
    XsDecimal d = {};
    d.decimal = '.';
    d.grouping = ',';
    d.percent = '%';
    d.permille = 0x2030;
    d.zero = '0';
    d.digit = '#';
    d.pattern = ';';
    d.minus = '-';
    d.infinity = xs_dup(c->s, L"Infinity", 8);
    d.nan = xs_dup(c->s, L"NaN", 3);
    UINT n;
    const WCHAR *v;
    if (attr_node(el, "name")) {
        if (!attr_qname(c, el, "name", &d.name))
            return E_FAIL;
        d.has_name = TRUE;
    }
    struct {
        const char *name;
        WCHAR *field;
    } chars[] = {{"decimal-separator", &d.decimal}, {"grouping-separator", &d.grouping}, {"percent", &d.percent},
                 {"per-mille", &d.permille},        {"zero-digit", &d.zero},             {"digit", &d.digit},
                 {"pattern-separator", &d.pattern}, {"minus-sign", &d.minus}};
    UINT explicit_mask = 0;
    for (UINT i = 0; i < 8; i++) {
        if ((v = attr_value(el, chars[i].name, &n))) {
            if (n != 1) {
                WCHAR w[40];
                UINT k = 0;
                for (; chars[i].name[k]; k++)
                    w[k] = (UCHAR)chars[i].name[k];
                w[k] = 0;
                return cerr(c, XE_XS_SINGLE_CHAR, w, nullptr, nullptr);
            }
            *chars[i].field = v[0];
            explicit_mask |= 1u << i;
        }
    }
    if ((v = attr_value(el, "infinity", &n))) {
        d.infinity = xs_dup(c->s, v, n);
        explicit_mask |= 0x100;
    }
    if ((v = attr_value(el, "NaN", &n))) {
        d.nan = xs_dup(c->s, v, n);
        explicit_mask |= 0x200;
    }
    /* the same format given twice must agree on what it sets. */
    for (UINT i = 0; i < c->s->ndecimals; i++) {
        XsDecimal *o = &c->s->decimals[i];
        if (o->has_name != d.has_name || (d.has_name && !name_same(&o->name, &d.name)))
            continue;
        for (UINT k = 0; k < 8; k++)
            if ((explicit_mask & (1u << k)) && (o->explicit_mask & (1u << k)) && *chars[k].field != 0 &&
                o->chars[k] != *chars[k].field) {
                WCHAR val[2] = {*chars[k].field, 0};
                WCHAR w[40];
                UINT q = 0;
                for (; chars[k].name[q]; q++)
                    w[q] = (UCHAR)chars[k].name[q];
                w[q] = 0;
                return cerr(c, XE_XS_DECIMAL_REDEF, w, d.has_name ? d.name.qname : L"", val);
            }
    }
    d.explicit_mask = explicit_mask;
    for (UINT k = 0; k < 8; k++)
        d.chars[k] = *chars[k].field;
    XsDecimal *slot = XS_PUSH(c->s, c->s->decimals, c->s->ndecimals, c->s->capdecimals, XsDecimal);
    *slot = d;
    return S_OK;
}

static HRESULT compile_key(Ctx *c, XNode *el)
{
    XsKey *k = XS_PUSH(c->s, c->s->keys, c->s->nkeys, c->s->capkeys, XsKey);
    memset(k, 0, sizeof(*k));
    if (!attr_qname(c, el, "name", &k->name))
        return E_FAIL;
    if (!pattern_attr(c, attr_node(el, "match"), &k->match))
        return E_FAIL;
    k->use = attr_expr(c, attr_node(el, "use"), 0);
    if (!k->use)
        return E_FAIL;
    k->precedence = c->module->precedence;
    return S_OK;
}

static HRESULT compile_attribute_set(Ctx *c, XNode *el)
{
    XsAttrSet *as = XS_PUSH(c->s, c->s->sets, c->s->nsets, c->s->capsets, XsAttrSet);
    memset(as, 0, sizeof(*as));
    if (!attr_qname(c, el, "name", &as->name))
        return E_FAIL;
    as->precedence = c->module->precedence;
    XNode *u = attr_node(el, "use-attribute-sets");
    if (u) {
        const WCHAR *v = u->value ? u->value : L"";
        UINT n = u->length, i = 0, cap = 0;
        while (i < n) {
            while (i < n && xd_is_space(v[i]))
                i++;
            UINT start = i;
            while (i < n && !xd_is_space(v[i]))
                i++;
            if (i == start)
                break;
            XsName *nm = XS_PUSH(c->s, as->use, as->nuse, cap, XsName);
            if (!resolve_qname(c, v + start, i - start, FALSE, nm))
                return cerr(c, XE_XS_BAD_PREFIX, nullptr, nullptr, nullptr);
        }
    }
    XsInst **tail = &as->body;
    for (XNode *k = el->first; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
            continue;
        if (k->type != XN_ELEMENT)
            return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, L"PCDATA nodes");
        if (!is_xsl(k) || !local_is(k, "attribute"))
            return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, k->qname);
        HRESULT hr = compile_element(c, k, &tail);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT compile_global(Ctx *c, XNode *el, BOOL is_param)
{
    XsInst *list = nullptr;
    XsInst **tail = &list;
    c->base = 0;
    c->nvars = 0;
    c->tmpl = nullptr;
    HRESULT hr = compile_variable(c, el, &tail, is_param ? XO_PARAM : XO_VARIABLE);
    c->nvars = 0;
    if (FAILED(hr))
        return hr;
    XsGlobal *g = nullptr;
    for (UINT i = 0; i < c->s->nglobals; i++)
        if (name_same(&c->s->globals[i].name, &list->name) && c->s->globals[i].precedence == c->module->precedence)
            return cerr(c, XE_XS_GLOBAL_DUP, list->name.qname, nullptr, nullptr);
    g = XS_PUSH(c->s, c->s->globals, c->s->nglobals, c->s->capglobals, XsGlobal);
    memset(g, 0, sizeof(*g));
    g->name = list->name;
    g->is_param = is_param;
    g->inst = list;
    g->precedence = c->module->precedence;
    return S_OK;
}

static BOOL valid_priority(const WCHAR *v, UINT n)
{
    if (!n)
        return FALSE;
    UINT i = 0;
    if (v[i] == '-' || v[i] == '+')
        i++;
    BOOL digits = FALSE, dot = FALSE;
    for (; i < n; i++) {
        if (v[i] >= '0' && v[i] <= '9')
            digits = TRUE;
        else if (v[i] == '.' && !dot)
            dot = TRUE;
        else
            return FALSE;
    }
    return digits;
}

static HRESULT compile_template(Ctx *c, XNode *el)
{
    XNode *match = attr_node(el, "match");
    XNode *name = attr_node(el, "name");
    XNode *mode = attr_node(el, "mode");
    XNode *prio = attr_node(el, "priority");
    if (!match && !name)
        return cerr(c, 0xC00CE020u, L"match", nullptr, nullptr);
    if (mode && !match)
        return cerr(c, XE_XS_MODE_NOMATCH, nullptr, nullptr, nullptr);
    XsTemplate proto = {};
    proto.module = c->module;
    proto.precedence = c->module->precedence;
    proto.has_match = match != nullptr;
    if (name) {
        if (!attr_qname(c, el, "name", &proto.name))
            return E_FAIL;
        proto.has_name = TRUE;
        for (UINT i = 0; i < c->s->ntemplates; i++) {
            XsTemplate *o = c->s->templates[i];
            if (o->has_name && name_same(&o->name, &proto.name) && o->precedence == proto.precedence)
                return cerr(c, XE_XS_TEMPLATE_DUP, proto.name.qname, nullptr, nullptr);
        }
    }
    if (mode && !attr_qname(c, el, "mode", &proto.mode))
        return E_FAIL;
    double explicit_priority = 0;
    BOOL has_priority = FALSE;
    if (prio) {
        const WCHAR *v = prio->value ? prio->value : L"";
        UINT n = prio->length;
        trim(&v, &n);
        if (!valid_priority(v, n)) {
            WCHAR val[80];
            UINT m = n < 78 ? n : 78;
            memcpy(val, v, m * sizeof(WCHAR));
            val[m] = 0;
            return cerr(c, XE_XS_BAD_PRIORITY, val, nullptr, nullptr);
        }
        explicit_priority = xd_parse_number(v, n);
        has_priority = TRUE;
    }
    XpExpr *pattern = nullptr;
    if (match && !pattern_attr(c, match, &pattern))
        return E_FAIL;
    /* the body: parameters first, then the content. */
    XsTemplate *first = xs_alloc(c->s, sizeof(XsTemplate));
    *first = proto;
    c->tmpl = first;
    c->in_for_each = FALSE;
    c->nvars = 0;
    c->base = 0;
    XsInst **ptail = &first->params;
    XNode *k = el->first;
    for (; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI || stripped_text(k))
            continue;
        if (k->type == XN_ELEMENT && is_xsl(k) && local_is(k, "param")) {
            XsScope *saved = c->scope;
            c->scope = make_scope(c, k);
            HRESULT hr = check_attrs(c, k) ? compile_variable(c, k, &ptail, XO_PARAM) : E_FAIL;
            c->scope = saved;
            if (FAILED(hr))
                return hr;
            continue;
        }
        break;
    }
    XsInst **btail = &first->body;
    for (; k; k = k->next) {
        if (k->type == XN_TEXT || k->type == XN_CDATA) {
            if (stripped_text(k) || !k->length)
                continue;
            XsInst *t = new_inst(c, XO_TEXT);
            t->text = xs_dup(c->s, k->value, k->length);
            t->tlen = k->length;
            append(&btail, t);
        } else if (k->type == XN_ELEMENT) {
            HRESULT hr = compile_element(c, k, &btail);
            if (FAILED(hr))
                return hr;
        }
    }
    c->nvars = 0;
    c->tmpl = nullptr;
    if (!match) {
        XsTemplate **slot = XS_PUSH(c->s, c->s->templates, c->s->ntemplates, c->s->captemplates, XsTemplate *);
        *slot = first;
        first->order = c->s->order++;
        return S_OK;
    }
    XpExpr *alts[64];
    UINT n = xp_alternatives(pattern, alts, 64);
    for (UINT i = 0; i < n; i++) {
        XsTemplate *t = i ? xs_alloc(c->s, sizeof(XsTemplate)) : first;
        if (i)
            *t = *first;
        t->pattern = alts[i];
        t->match = pattern;
        t->priority = has_priority ? explicit_priority : xp_priority(alts[i]);
        t->order = c->s->order++;
        XsTemplate **slot = XS_PUSH(c->s, c->s->templates, c->s->ntemplates, c->s->captemplates, XsTemplate *);
        *slot = t;
    }
    return S_OK;
}

static HRESULT compile_script(Ctx *c, XNode *el)
{
    UINT n;
    const WCHAR *prefix = attr_value(el, "implements-prefix", &n);
    if (!prefix)
        return cerr(c, 0xC00CE020u, L"implements-prefix", nullptr, nullptr);
    BOOL found;
    const WCHAR *uri = scope_lookup(c->scope, prefix, n, &found);
    if (!found) {
        WCHAR p[80];
        UINT m = n < 78 ? n : 78;
        memcpy(p, prefix, m * sizeof(WCHAR));
        p[m] = 0;
        return cerr(c, 0xC00CE01Du, p, nullptr, nullptr);
    }
    for (XNode *k = el->first; k; k = k->next)
        if (k->type == XN_ELEMENT)
            return cerr(c, XE_XS_KEYWORD_CONTAIN, el->qname, nullptr, k->qname);
    /* a blocked script is reported once the whole stylesheet is compiled. */
    if (!c->s->allow_script)
        c->script_blocked = TRUE;
    XsScript *sc = XS_PUSH(c->s, c->s->scripts, c->s->nscripts, c->s->capscripts, XsScript);
    memset(sc, 0, sizeof(*sc));
    sc->uri = xs_dup(c->s, uri, xd_wlen(uri));
    sc->prefix = xs_dup(c->s, prefix, n);
    const WCHAR *lang = attr_value(el, "language", &n);
    sc->language = lang ? xs_dup(c->s, lang, n) : xs_dup(c->s, L"JScript", 7);
    XBuf b = {};
    for (XNode *k = el->first; k; k = k->next)
        if (k->type == XN_TEXT || k->type == XN_CDATA)
            xb_put(&b, k->value ? k->value : L"", k->length);
    sc->text = b.length ? xs_dup(c->s, b.s, b.length) : xs_dup(c->s, L"", 0);
    xb_free(&b);
    sc->base = c->base_url ? xs_dup(c->s, c->base_url, xd_wlen(c->base_url)) : nullptr;
    sc->scope = c->scope;
    return S_OK;
}

static HRESULT compile_alias(Ctx *c, XNode *el)
{
    UINT n1, n2;
    const WCHAR *sp = attr_value(el, "stylesheet-prefix", &n1);
    const WCHAR *rp = attr_value(el, "result-prefix", &n2);
    BOOL f1, f2;
    const WCHAR *su, *ru;
    su = is_lit(sp, n1, "#default") ? scope_lookup(c->scope, L"", 0, &f1) : scope_lookup(c->scope, sp, n1, &f1);
    ru = is_lit(rp, n2, "#default") ? scope_lookup(c->scope, L"", 0, &f2) : scope_lookup(c->scope, rp, n2, &f2);
    if (!f1 || !f2)
        return cerr(c, XE_XS_BAD_PREFIX, !f1 ? sp : rp, nullptr, nullptr);
    UINT cap = c->s->naliases;
    XNs *a = xs_grow(c->s, c->s->aliases, c->s->naliases, cap + 1, sizeof(XNs));
    XNs *t = xs_grow(c->s, c->s->alias_target, c->s->naliases, cap + 1, sizeof(XNs));
    c->s->aliases = a;
    c->s->alias_target = t;
    a[cap].prefix = xs_dup(c->s, sp, n1);
    a[cap].uri = xs_dup(c->s, su, xd_wlen(su));
    t[cap].prefix = is_lit(rp, n2, "#default") ? xs_dup(c->s, L"", 0) : xs_dup(c->s, rp, n2);
    t[cap].uri = xs_dup(c->s, ru, xd_wlen(ru));
    c->s->naliases++;
    return S_OK;
}

static HRESULT compile_toplevel(Ctx *c, XNode *root, XsModule *mod)
{
    BOOL imports_ok = TRUE;
    /* the aliases first, they change how the rest is compiled. */
    for (XNode *k = root->first; k; k = k->next)
        if (k->type == XN_ELEMENT && is_xsl(k) && local_is(k, "namespace-alias")) {
            XsScope *saved = c->scope;
            c->scope = make_scope(c, k);
            HRESULT hr = check_attrs(c, k) ? compile_alias(c, k) : E_FAIL;
            c->scope = saved;
            if (FAILED(hr))
                return hr;
        }
    for (XNode *k = root->first; k; k = k->next) {
        if (k->type == XN_COMMENT || k->type == XN_PI)
            continue;
        if (k->type == XN_TEXT || k->type == XN_CDATA) {
            if (stripped_text(k))
                continue;
            return cerr(c, XE_XS_KEYWORD_CONTAIN, root->qname, nullptr, L"PCDATA nodes");
        }
        if (k->type != XN_ELEMENT)
            continue;
        XsScope *saved_scope = c->scope;
        UINT saved_excluded = c->nexcluded, saved_extension = c->nextension;
        c->scope = make_scope(c, k);
        c->node = k;
        HRESULT hr = S_OK;
        if (!is_xsl(k)) {
            if (is_msxsl(k) && local_is(k, "script")) {
                hr = compile_script(c, k);
            } else if (!k->uri) {
                hr = cerr(c, XE_XS_KEYWORD_CONTAIN, root->qname, nullptr, k->qname);
            }
        } else if (local_is(k, "import")) {
            /* the imports were taken before the precedence of this module was chosen. */
            if (!imports_ok)
                hr = cerr(c, XE_XS_KEYWORD_HERE, k->qname, nullptr, nullptr);
        } else {
            imports_ok = FALSE;
            if (!check_attrs(c, k)) {
                hr = E_FAIL;
            } else if (local_is(k, "include")) {
                hr = include_module(c, k, mod, FALSE);
            } else if (local_is(k, "strip-space") || local_is(k, "preserve-space")) {
                add_space(c, k, local_is(k, "strip-space"), mod);
                hr = failed(c) ? E_FAIL : S_OK;
            } else if (local_is(k, "output")) {
                hr = compile_output(c, k);
            } else if (local_is(k, "key")) {
                hr = compile_key(c, k);
            } else if (local_is(k, "decimal-format")) {
                hr = compile_decimal(c, k);
            } else if (local_is(k, "namespace-alias")) {
                hr = S_OK;
            } else if (local_is(k, "attribute-set")) {
                hr = compile_attribute_set(c, k);
            } else if (local_is(k, "variable") || local_is(k, "param")) {
                hr = compile_global(c, k, local_is(k, "param"));
            } else if (local_is(k, "template")) {
                hr = compile_template(c, k);
            } else {
                hr = cerr(c, XE_XS_KEYWORD_CONTAIN, root->qname, nullptr, k->qname);
            }
        }
        c->scope = saved_scope;
        c->nexcluded = saved_excluded;
        c->nextension = saved_extension;
        if (FAILED(hr) || failed(c))
            return failed(c) ? E_FAIL : hr;
    }
    return S_OK;
}

static HRESULT compile_module_root(Ctx *c, XNode *root, XsModule *mod, BOOL main)
{
    if (!is_xsl(root) || (!local_is(root, "stylesheet") && !local_is(root, "transform")))
        return cerr(c, XE_XS_KEYWORD_HERE, root->qname, nullptr, nullptr);
    c->scope = make_scope(c, root);
    c->node = root;
    if (!check_attrs(c, root))
        return E_FAIL;
    XNode *ver = attr_node(root, "version");
    if (ver) {
        double v = xd_parse_number(ver->value, ver->length);
        if (main)
            c->s->version = v;
        c->forward = v > 1.0 || xd_isnan(v);
    }
    XNode *ex = attr_node(root, "exclude-result-prefixes");
    if (ex)
        add_uri_list(c, ex, &c->excluded, &c->nexcluded, &c->capexcluded, TRUE);
    XNode *ext = attr_node(root, "extension-element-prefixes");
    if (ext)
        add_uri_list(c, ext, &c->extension, &c->nextension, &c->capextension, FALSE);
    if (failed(c))
        return E_FAIL;
    mod->import_min = 0x7FFFFFFF;
    /* the imports go first and get the lower precedences. */
    BOOL seen_other = FALSE;
    for (XNode *k = root->first; k; k = k->next) {
        if (k->type != XN_ELEMENT)
            continue;
        if (is_xsl(k) && local_is(k, "import")) {
            if (seen_other)
                return cerr(c, XE_XS_KEYWORD_HERE, k->qname, nullptr, nullptr);
            XsScope *saved = c->scope;
            c->scope = make_scope(c, k);
            if (!check_attrs(c, k)) {
                c->scope = saved;
                return E_FAIL;
            }
            HRESULT hr = include_module(c, k, mod, TRUE);
            c->scope = saved;
            if (FAILED(hr))
                return hr;
        } else {
            seen_other = TRUE;
        }
    }
    mod->precedence = ++c->s->next_precedence;
    if (mod->import_min == 0x7FFFFFFF)
        mod->import_min = mod->precedence;
    XsModule **ms = XS_PUSH(c->s, c->s->modules, c->s->nmodules, c->s->capmodules, XsModule *);
    *ms = mod;
    XsModule *saved_module = c->module;
    c->module = mod;
    HRESULT hr = compile_toplevel(c, root, mod);
    c->module = saved_module;
    return hr;
}

/* ------------------------------------------------------------------ the whole stylesheet */

static BOOL set_exists(XsSheet *s, const XsName *n)
{
    for (UINT i = 0; i < s->nsets; i++)
        if (name_same(&s->sets[i].name, n))
            return TRUE;
    return FALSE;
}

/* attribute sets that use each other in a circle. */
static BOOL set_reaches(XsSheet *s, const XsName *from, const XsName *target, UINT depth)
{
    if (depth > 64)
        return TRUE;
    for (UINT i = 0; i < s->nsets; i++) {
        if (!name_same(&s->sets[i].name, from))
            continue;
        for (UINT k = 0; k < s->sets[i].nuse; k++) {
            if (name_same(&s->sets[i].use[k], target) || set_reaches(s, &s->sets[i].use[k], target, depth + 1))
                return TRUE;
        }
    }
    return FALSE;
}

static HRESULT finish(Ctx *c)
{
    XsSheet *s = c->s;
    for (UINT i = 0; i < c->npending; i++) {
        XsName n = {};
        n.uri = c->pending[i].uri;
        n.local = c->pending[i].local;
        n.llen = c->pending[i].llen;
        BOOL found = FALSE;
        for (UINT g = 0; g < s->nglobals; g++)
            if (name_same(&s->globals[g].name, &n))
                found = TRUE;
        if (!found) {
            WCHAR name[120];
            UINT m = n.llen < 118 ? n.llen : 118;
            memcpy(name, n.local, m * sizeof(WCHAR));
            name[m] = 0;
            return cerr(c, XE_XS_VAR_UNRESOLVED, name, nullptr, nullptr);
        }
    }
    for (UINT i = 0; i < c->ncalls; i++) {
        BOOL found = FALSE;
        for (UINT t = 0; t < s->ntemplates; t++)
            if (s->templates[t]->has_name && name_same(&s->templates[t]->name, &c->calls[i]->name))
                found = TRUE;
        if (!found)
            return cerr(c, XE_XS_TEMPLATE_MISSING, c->calls[i]->name.qname, nullptr, nullptr);
    }
    for (UINT i = 0; i < c->nused_sets; i++)
        if (!set_exists(s, &c->used_sets[i]))
            return cerr(c, XE_XS_ATTRSET_MISSING, c->used_sets[i].qname, nullptr, nullptr);
    for (UINT i = 0; i < s->nsets; i++) {
        for (UINT k = 0; k < s->sets[i].nuse; k++)
            if (!set_exists(s, &s->sets[i].use[k]))
                return cerr(c, XE_XS_ATTRSET_MISSING, s->sets[i].use[k].qname, nullptr, nullptr);
        if (set_reaches(s, &s->sets[i].name, &s->sets[i].name, 0))
            return cerr(c, XE_XS_ATTRSET_CIRCULAR, s->sets[i].name.qname, nullptr, nullptr);
    }
    for (UINT i = 0; i < s->nscripts; i++)
        for (UINT k = i + 1; k < s->nscripts; k++)
            if (!wcscmp(s->scripts[i].uri, s->scripts[k].uri) &&
                xd_wcsicmp(s->scripts[i].language, s->scripts[k].language))
                return cerr(c, XE_XS_SCRIPT_LANGS, s->scripts[i].uri, nullptr, nullptr);
    return S_OK;
}

/* higher precedence first, then higher priority, then the later one. */
static int template_rank(const void *a, const void *b)
{
    const XsTemplate *x = *(XsTemplate *const *)a, *y = *(XsTemplate *const *)b;
    if (x->precedence != y->precedence)
        return y->precedence - x->precedence;
    if (x->priority != y->priority)
        return x->priority < y->priority ? 1 : -1;
    return x->order < y->order ? 1 : x->order > y->order ? -1 : 0;
}

void xs_sheet_addref(XsSheet *s)
{
    InterlockedIncrement(&s->refs);
}

void xs_sheet_release(XsSheet *s)
{
    if (!s || InterlockedDecrement(&s->refs))
        return;
    for (UINT i = 0; i < s->ntemplates; i++)
        if (s->templates[i]->pattern && s->templates[i]->pattern != s->templates[i]->match)
            xp_free(s->templates[i]->pattern);
    for (UINT i = 0; i < s->nexprs; i++)
        xp_free(s->exprs[i]);
    xd_free(s->exprs);
    xse_clear(&s->error);
    if (s->literal)
        xn_release(s->literal);
    if (s->source)
        xn_release(s->source);
    Block *b = s->arena;
    while (b) {
        Block *next = b->next;
        xd_free(b);
        b = next;
    }
    xd_free(s);
}

XsSheet *xs_compile(XNode *style, UINT flags, XsError *error)
{
    XsSheet *s = xd_alloc(sizeof(XsSheet));
    if (!s)
        return nullptr;
    s->refs = 1;
    s->v6 = (flags & XSF_V6) != 0;
    s->resolve_externals = (flags & XSF_RESOLVE) != 0;
    s->allow_script = (flags & XSF_ALLOW_SCRIPT) != 0;
    Ctx c = {};
    c.s = s;
    c.flags = flags;
    c.host.declared = host_declared;
    c.host.known = host_known;
    c.host.user = &c;
    XNode *root = style->type == XN_DOCUMENT ? xd_document_element(style->doc) : style;
    if (style->doc->url)
        s->url = xs_dup(s, style->doc->url, xd_wlen(style->doc->url));
    if (style->type == XN_DOCUMENT && style->doc->url)
        c.base_url = xs_dup(s, style->doc->url, xd_wlen(style->doc->url));
    else if (style->doc->url)
        c.base_url = xs_dup(s, style->doc->url, xd_wlen(style->doc->url));
    HRESULT hr = S_OK;
    if (!root) {
        cerr(&c, XE_XS_NO_DOCELEM, nullptr, nullptr, nullptr);
        hr = E_FAIL;
    } else if (is_xsl(root) && (local_is(root, "stylesheet") || local_is(root, "transform"))) {
        XsModule *mod = xs_alloc(s, sizeof(XsModule));
        mod->url = (WCHAR *)c.base_url;
        hr = compile_module_root(&c, root, mod, TRUE);
        if (SUCCEEDED(hr))
            hr = finish(&c);
    } else if (xd_attr_find_ns(root, L"version", 7, XSLT_NS)) {
        /* a literal result element as the stylesheet. */
        c.scope = nullptr;
        XsModule *mod = xs_alloc(s, sizeof(XsModule));
        mod->precedence = ++s->next_precedence;
        mod->import_min = mod->precedence;
        c.module = mod;
        XsTemplate *t = xs_alloc(s, sizeof(XsTemplate));
        t->module = mod;
        t->precedence = mod->precedence;
        t->has_match = TRUE;
        c.tmpl = t;
        XpExpr *p = compile_expr(&c, L"/", 1, XPF_PATTERN);
        if (!p) {
            hr = E_FAIL;
        } else {
            t->pattern = p;
            t->match = p;
            t->priority = 0.5;
            XsInst **tail = &t->body;
            hr = compile_element(&c, root, &tail);
            XsTemplate **slot = XS_PUSH(s, s->templates, s->ntemplates, s->captemplates, XsTemplate *);
            *slot = t;
            if (SUCCEEDED(hr))
                hr = finish(&c);
        }
    } else {
        /* a document that is not a stylesheet is its own result. */
        s->literal = xd_doc_clone(root->doc, TRUE);
        s->output.omit_decl = 1;
    }
    if (SUCCEEDED(hr) && !failed(&c) && c.script_blocked) {
        cerr(&c, XE_XS_SCRIPT_BLOCKED, nullptr, nullptr, nullptr);
        hr = E_FAIL;
    }
    if (FAILED(hr) || failed(&c)) {
        if (error) {
            *error = s->error;
            memset(&s->error, 0, sizeof(s->error));
        }
        xs_sheet_release(s);
        return nullptr;
    }
    for (UINT i = 0; i < s->ntemplates; i++) {
        XsTemplate *t = s->templates[i];
        if (t->pattern)
            t->quick = xp_quick(t->pattern, &t->quick_local, &t->quick_len);
    }
    if (s->wants_source && root)
        s->source = xd_doc_clone(root->doc, TRUE);
    /* insertion sort, the order of equal templates is already the document order. */
    for (UINT i = 1; i < s->ntemplates; i++) {
        XsTemplate *t = s->templates[i];
        UINT k = i;
        while (k && template_rank(&s->templates[k - 1], &t) > 0) {
            s->templates[k] = s->templates[k - 1];
            k--;
        }
        s->templates[k] = t;
    }
    return s;
}
