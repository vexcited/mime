/* running a compiled stylesheet over a source tree. */
#include <xmldom/xslt_priv.h>


static HRESULT run_list(XsRun *r, XsInst *list, XsFrame *f);

HRESULT xsr_error(XsRun *r, UINT id, const WCHAR *p1, const WCHAR *p2, const WCHAR *p3)
{
    xse_set(r->err, id, p1, p2, p3);
    return E_FAIL;
}

/* ------------------------------------------------------------------ documents made for the run */

XDoc *xsr_new_temp_doc(XsRun *r)
{
    XDoc *d = xd_doc_new(r->v6 ? XDF_V6 : 0);
    if (!d)
        return nullptr;
    if (r->ntemps == r->captemps) {
        UINT cap = r->captemps ? r->captemps * 2 : 16;
        XDoc **v = xd_realloc(r->temps, cap * sizeof(XDoc *));
        if (!v) {
            xn_release(&d->node);
            return nullptr;
        }
        r->temps = v;
        r->captemps = cap;
    }
    r->temps[r->ntemps++] = d;
    return d;
}

/* ------------------------------------------------------------------ the output tree */

static XNode *out_new(XsRun *r, UINT type, const WCHAR *qname, UINT qlen, const WCHAR *uri)
{
    return xd_node_new(r->out, type, qname, qlen, uri);
}

static HRESULT out_element(XsRun *r, const WCHAR *qname, UINT qlen, const WCHAR *uri, XNode **made)
{
    XNode *e = out_new(r, XN_ELEMENT, qname, qlen, uri);
    if (!e)
        return E_OUTOFMEMORY;
    xd_link(r->cur, e, nullptr);
    r->cur = e;
    if (made)
        *made = e;
    return S_OK;
}

static BOOL same_uri(const WCHAR *a, const WCHAR *b)
{
    return !wcscmp(a ? a : L"", b ? b : L"");
}

/* an attribute goes on the element being written while it has no children; later ones replace earlier ones. */
static void out_attribute(XsRun *r, const WCHAR *qname, UINT qlen, const WCHAR *uri, const WCHAR *value, UINT vlen)
{
    XNode *e = r->cur;
    if (e->type != XN_ELEMENT || e->first)
        return;
    UINT local_off = 0;
    for (UINT i = 1; i + 1 < qlen; i++)
        if (qname[i] == ':') {
            local_off = i + 1;
            break;
        }
    const WCHAR *local = qname + local_off;
    UINT llen = qlen - local_off;
    for (XNode *a = e->attrs; a; a = a->next) {
        if (xd_local_length(a) == llen && !memcmp(xd_local(a), local, (SIZE_T)llen * sizeof(WCHAR)) &&
            same_uri(a->uri, uri) && xd_is_xmlns_attr(a) == (qlen >= 5 && !memcmp(qname, L"xmlns", 10) &&
                                                             (qlen == 5 || qname[5] == ':'))) {
            xd_node_set_value(a, value, vlen);
            return;
        }
    }
    XNode *a = out_new(r, XN_ATTRIBUTE, qname, qlen, uri);
    if (!a)
        return;
    xd_node_set_value(a, value, vlen);
    xd_attr_link(e, a);
}

static void out_text(XsRun *r, const WCHAR *s, UINT len, BOOL doe)
{
    if (!len)
        return;
    XNode *last = r->cur->last;
    if (last && last->type == XN_TEXT && ((last->flags & XF_DOE) != 0) == doe) {
        WCHAR *v = xd_realloc(last->value, ((SIZE_T)last->length + len + 1) * sizeof(WCHAR));
        if (!v)
            return;
        memcpy(v + last->length, s, (SIZE_T)len * sizeof(WCHAR));
        last->length += len;
        v[last->length] = 0;
        last->value = v;
        return;
    }
    XNode *t = out_new(r, XN_TEXT, nullptr, 0, nullptr);
    if (!t)
        return;
    xd_node_set_value(t, s, len);
    if (doe)
        t->flags |= XF_DOE;
    xd_link(r->cur, t, nullptr);
}

static void out_leaf(XsRun *r, UINT type, const WCHAR *name, const WCHAR *value, UINT vlen)
{
    XNode *n = out_new(r, type, name, name ? xd_wlen(name) : 0, nullptr);
    if (!n)
        return;
    xd_node_set_value(n, value, vlen);
    xd_link(r->cur, n, nullptr);
}

/* ------------------------------------------------------------------ variables */

static HRESULT push_binding(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT llen, XpVal *val)
{
    if (r->sp == r->cap) {
        UINT cap = r->cap ? r->cap * 2 : 64;
        XsBinding *v = xd_realloc(r->stack, cap * sizeof(XsBinding));
        if (!v)
            return E_OUTOFMEMORY;
        r->stack = v;
        r->cap = cap;
    }
    XsBinding *b = &r->stack[r->sp++];
    b->uri = uri;
    b->local = local;
    b->llen = llen;
    b->val = *val;
    memset(val, 0, sizeof(*val));
    return S_OK;
}

static void pop_to(XsRun *r, UINT mark)
{
    while (r->sp > mark)
        xpv_free(&r->stack[--r->sp].val);
}

static BOOL same_name(const WCHAR *uri1, const WCHAR *l1, UINT n1, const WCHAR *uri2, const WCHAR *l2, UINT n2)
{
    return n1 == n2 && !memcmp(l1, l2, (SIZE_T)n1 * sizeof(WCHAR)) && same_uri(uri1, uri2);
}

static HRESULT eval_global(XsRun *r, UINT index);

static HRESULT host_rtf_error(void *user, int kind, const WCHAR *name, UINT name_len, UINT arg, const WCHAR *text,
                              UINT start, UINT end)
{
    XsRun *r = user;
    WCHAR n[120];
    UINT m = name_len < 118 ? name_len : 118;
    memcpy(n, name, m * sizeof(WCHAR));
    n[m] = 0;
    if (!r->v6) {
        xsr_error(r, XE_XS_VAR_NOT_SET, n, nullptr, nullptr);
        return E_FAIL;
    }
    WCHAR num[2] = {(WCHAR)('0' + arg), 0};
    if (kind == 1)
        xsr_error(r, XE_XS_ARG_NODESET, num, nullptr, nullptr);
    else
        xsr_error(r, XE_XS_NOT_NODESET, nullptr, nullptr, nullptr);
    r->err->suffix = xse_marked_text(text, xd_wlen(text), start, end);
    return E_FAIL;
}

static HRESULT host_variable(void *user, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *out)
{
    XsRun *r = user;
    for (UINT i = r->sp; i-- > r->base;) {
        XsBinding *b = &r->stack[i];
        if (same_name(b->uri, b->local, b->llen, uri, local, length))
            return xpv_copy(out, &b->val) ? S_OK : E_OUTOFMEMORY;
    }
    /* the global of the highest precedence. */
    int best = -1;
    for (UINT i = 0; i < r->sheet->nglobals; i++) {
        XsGlobal *g = &r->sheet->globals[i];
        if (same_name(g->name.uri, g->name.local, g->name.llen, uri, local, length) &&
            (best < 0 || g->precedence > r->sheet->globals[best].precedence))
            best = (int)i;
    }
    if (best < 0) {
        WCHAR name[120];
        UINT m = length < 118 ? length : 118;
        memcpy(name, local, m * sizeof(WCHAR));
        name[m] = 0;
        return xsr_error(r, XE_XS_VAR_UNRESOLVED, name, nullptr, nullptr);
    }
    HRESULT hr = eval_global(r, (UINT)best);
    if (FAILED(hr))
        return hr;
    return xpv_copy(out, &r->gval[best]) ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ evaluating */

static HRESULT eval_x(XsRun *r, XpExpr *x, const XsFrame *f, XpVal *out)
{
    XpCall call = {f->node, f->pos, f->size, f->node, &r->host};
    HRESULT hr = xp_eval(x, &call, out);
    if (FAILED(hr) && !r->err->id) {
        if (hr == (HRESULT)XE_XS_NOT_NODESET || hr == (HRESULT)XE_NOT_NODESET)
            xsr_error(r, XE_XS_NOT_NODESET, nullptr, nullptr, nullptr);
        else if (hr == (HRESULT)XE_NO_VARIABLES)
            xsr_error(r, XE_NO_VARIABLES, nullptr, nullptr, nullptr);
        else
            xsr_error(r, (UINT)hr, nullptr, nullptr, nullptr);
    }
    return hr;
}

static HRESULT eval_string(XsRun *r, XpExpr *x, const XsFrame *f, XBuf *out)
{
    XpVal v = {};
    HRESULT hr = eval_x(r, x, f, &v);
    if (SUCCEEDED(hr)) {
        if (v.t == XPV_SET && v.set.n) {
            xsr_node_string(v.set.v[0], out);
        } else {
            xpv_string(&v, out);
        }
    }
    xpv_free(&v);
    return hr;
}

static HRESULT eval_avt(XsRun *r, XsAvt *a, const XsFrame *f, XBuf *out)
{
    for (UINT i = 0; i < a->n; i++) {
        if (a->parts[i].expr) {
            HRESULT hr = eval_string(r, a->parts[i].expr, f, out);
            if (FAILED(hr))
                return hr;
        } else {
            xb_put(out, a->parts[i].lit, a->parts[i].len);
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ result fragments */

/* runs a list of instructions into a new fragment. */
static HRESULT run_to_fragment(XsRun *r, XsInst *list, const XsFrame *f, XNode **root)
{
    XDoc *d = xsr_new_temp_doc(r);
    if (!d)
        return E_OUTOFMEMORY;
    XDoc *saved_out = r->out;
    XNode *saved_cur = r->cur;
    r->out = d;
    r->cur = &d->node;
    XsFrame copy = *f;
    HRESULT hr = run_list(r, list, &copy);
    r->out = saved_out;
    r->cur = saved_cur;
    *root = &d->node;
    return hr;
}

/* the string of a body, such as the content of an attribute or a comment. */
static HRESULT run_to_string(XsRun *r, XsInst *list, const XsFrame *f, XBuf *out)
{
    XNode *root;
    HRESULT hr = run_to_fragment(r, list, f, &root);
    if (SUCCEEDED(hr))
        xd_text(root, out);
    return hr;
}

static HRESULT eval_binding(XsRun *r, XsInst *i, const XsFrame *f, XpVal *out)
{
    if (i->has_select)
        return eval_x(r, i->select, f, out);
    if (i->body) {
        XNode *root;
        HRESULT hr = run_to_fragment(r, i->body, f, &root);
        if (FAILED(hr))
            return hr;
        out->t = XPV_SET;
        out->rtf = TRUE;
        xs_add(&out->set, root);
        return S_OK;
    }
    xpv_set_string(out, L"", 0);
    return S_OK;
}

/* the value of an external parameter given to the processor, if there is one. */
static XsParam *find_param(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT llen)
{
    for (UINT i = r->req->nparams; i-- > 0;) {
        XsParam *p = &r->req->params[i];
        if (xd_wlen(p->name) == llen && !memcmp(p->name, local, (SIZE_T)llen * sizeof(WCHAR)) &&
            same_uri(p->uri, uri))
            return p;
    }
    return nullptr;
}

static HRESULT eval_global(XsRun *r, UINT index)
{
    XsGlobal *g = &r->sheet->globals[index];
    if (r->gstate[index] == 2)
        return S_OK;
    if (r->gstate[index] == 1)
        return xsr_error(r, XE_XS_VAR_CIRCULAR, g->name.qname, nullptr, nullptr);
    r->gstate[index] = 1;
    /* a global is evaluated outside every template: its own frame, the root of the source as context. */
    UINT saved_base = r->base, saved_sp = r->sp;
    r->base = r->sp;
    XsFrame f = {r->input_root, 1, 1};
    HRESULT hr = S_OK;
    XsParam *given = g->is_param ? find_param(r, g->name.uri, g->name.local, g->name.llen) : nullptr;
    if (given) {
        hr = xsr_variant_to_val(r, &given->value, &r->gval[index]);
    } else {
        hr = eval_binding(r, g->inst, &f, &r->gval[index]);
    }
    pop_to(r, saved_sp);
    r->base = saved_base;
    if (SUCCEEDED(hr))
        r->gstate[index] = 2;
    else
        r->gstate[index] = 0;
    return hr;
}

/* ------------------------------------------------------------------ nodes */

static BOOL skip_child(const XNode *n)
{
    return n->type == XN_DOCTYPE || (n->type == XN_PI && (n->flags & XF_XMLDECL));
}

static HRESULT children_of(XNode *n, XSet *set)
{
    for (XNode *c = n->first; c; c = c->next) {
        if (skip_child(c))
            continue;
        if (!xs_add(set, c))
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ sorting */

typedef struct SortKey {
    const WCHAR *s;
    UINT len;
    double num;
} SortKey;

typedef struct SortSpec {
    BOOL number;
    BOOL descending;
    BOOL upper_first;
    LCID lcid;
    SortKey *keys;
} SortSpec;

static int compare_keys(const SortSpec *sp, UINT a, UINT b)
{
    const SortKey *x = &sp->keys[a], *y = &sp->keys[b];
    int c;
    if (sp->number) {
        BOOL nx = xd_isnan(x->num), ny = xd_isnan(y->num);
        if (nx || ny)
            c = nx && ny ? 0 : nx ? -1 : 1;
        else
            c = x->num < y->num ? -1 : x->num > y->num ? 1 : 0;
    } else {
        int res = CompareStringW(sp->lcid, NORM_IGNORECASE, x->s, (int)x->len, y->s, (int)y->len);
        c = res == CSTR_LESS_THAN ? -1 : res == CSTR_GREATER_THAN ? 1 : 0;
        if (!c) {
            res = CompareStringW(sp->lcid, 0, x->s, (int)x->len, y->s, (int)y->len);
            c = res == CSTR_LESS_THAN ? -1 : res == CSTR_GREATER_THAN ? 1 : 0;
            if (sp->upper_first)
                c = -c;
        }
    }
    return sp->descending ? -c : c;
}

static HRESULT sort_nodes(XsRun *r, XsInst *sorts, XSet *set, const XsFrame *f)
{
    UINT nsorts = 0;
    for (XsInst *s = sorts; s; s = s->next)
        nsorts++;
    if (!nsorts || set->n < 2)
        return S_OK;
    UINT n = set->n;
    SortSpec *specs = xd_alloc(sizeof(SortSpec) * nsorts);
    WCHAR **pools = xd_alloc(sizeof(WCHAR *) * nsorts);
    UINT *index = xd_alloc(sizeof(UINT) * n * 2);
    if (!specs || !pools || !index) {
        xd_free(specs);
        xd_free(pools);
        xd_free(index);
        return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    UINT k = 0;
    for (XsInst *s = sorts; s && SUCCEEDED(hr); s = s->next, k++) {
        SortSpec *sp = &specs[k];
        sp->keys = xd_alloc(sizeof(SortKey) * n);
        if (!sp->keys) {
            hr = E_OUTOFMEMORY;
            break;
        }
        XBuf attr = {};
        XsFrame af = *f;
        sp->number = FALSE;
        sp->descending = FALSE;
        sp->upper_first = FALSE;
        sp->lcid = LOCALE_USER_DEFAULT;
        if (s->avt[1]) {
            hr = eval_avt(r, s->avt[1], &af, &attr);
            if (SUCCEEDED(hr))
                sp->number = attr.length == 6 && !memcmp(attr.s, L"number", 12);
            attr.length = 0;
        }
        if (SUCCEEDED(hr) && s->avt[2]) {
            hr = eval_avt(r, s->avt[2], &af, &attr);
            if (SUCCEEDED(hr))
                sp->descending = attr.length == 10 && !memcmp(attr.s, L"descending", 20);
            attr.length = 0;
        }
        if (SUCCEEDED(hr) && s->avt[3]) {
            hr = eval_avt(r, s->avt[3], &af, &attr);
            if (SUCCEEDED(hr))
                sp->upper_first = attr.length == 11 && !memcmp(attr.s, L"upper-first", 22);
            attr.length = 0;
        }
        if (SUCCEEDED(hr) && s->avt[0]) {
            hr = eval_avt(r, s->avt[0], &af, &attr);
            if (SUCCEEDED(hr) && attr.length) {
                xb_putc(&attr, 0);
                LCID lcid = LocaleNameToLCID(attr.s, 0);
                if (lcid)
                    sp->lcid = lcid;
            }
        }
        xb_free(&attr);
        /* all the keys in one pool. */
        XBuf pool = {};
        UINT *offsets = xd_alloc(sizeof(UINT) * n * 2);
        for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
            XsFrame nf = {set->v[i], i + 1, n};
            XpVal v = {};
            if (s->select) {
                hr = eval_x(r, s->select, &nf, &v);
            } else {
                XBuf t = {};
                xsr_node_string(nf.node, &t);
                xpv_set_string(&v, t.s ? t.s : L"", t.length);
                xb_free(&t);
            }
            if (FAILED(hr)) {
                xpv_free(&v);
                break;
            }
            XBuf str = {};
            if (v.t == XPV_SET && v.set.n)
                xsr_node_string(v.set.v[0], &str);
            else
                xpv_string(&v, &str);
            offsets[i * 2] = pool.length;
            offsets[i * 2 + 1] = str.length;
            if (str.length)
                xb_put(&pool, str.s, str.length);
            sp->keys[i].num = sp->number ? xd_parse_number(str.s ? str.s : L"", str.length) : 0;
            xb_free(&str);
            xpv_free(&v);
        }
        pools[k] = pool.s;
        if (SUCCEEDED(hr))
            for (UINT i = 0; i < n; i++) {
                sp->keys[i].s = pool.s ? pool.s + offsets[i * 2] : L"";
                sp->keys[i].len = offsets[i * 2 + 1];
            }
        xd_free(offsets);
    }
    if (SUCCEEDED(hr)) {
        /* a stable merge sort of the indexes. */
        UINT *a = index, *b = index + n;
        for (UINT i = 0; i < n; i++)
            a[i] = i;
        for (UINT width = 1; width < n; width *= 2) {
            for (UINT lo = 0; lo < n; lo += 2 * width) {
                UINT mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
                UINT i = lo, j = mid, o = lo;
                while (i < mid && j < hi) {
                    int c = 0;
                    for (UINT s = 0; s < nsorts && !c; s++)
                        c = compare_keys(&specs[s], a[j], a[i]);
                    /* c < 0 means the right one is smaller. */
                    if (c < 0)
                        b[o++] = a[j++];
                    else
                        b[o++] = a[i++];
                }
                while (i < mid)
                    b[o++] = a[i++];
                while (j < hi)
                    b[o++] = a[j++];
            }
            UINT *t = a;
            a = b;
            b = t;
        }
        XNode **sorted = xd_alloc(sizeof(XNode *) * n);
        if (sorted) {
            for (UINT i = 0; i < n; i++)
                sorted[i] = set->v[a[i]];
            memcpy(set->v, sorted, sizeof(XNode *) * n);
            xd_free(sorted);
        }
    }
    for (UINT s = 0; s < nsorts; s++) {
        xd_free(specs[s].keys);
        xd_free(pools[s]);
    }
    xd_free(specs);
    xd_free(pools);
    xd_free(index);
    return hr;
}

/* ------------------------------------------------------------------ templates */

typedef struct PassedParam {
    const WCHAR *uri;
    const WCHAR *local;
    UINT llen;
    XpVal val;
} PassedParam;

static void free_passed(PassedParam *p, UINT n)
{
    for (UINT i = 0; i < n; i++)
        xpv_free(&p[i].val);
    xd_free(p);
}

static HRESULT eval_with_params(XsRun *r, XsInst *list, const XsFrame *f, PassedParam **out, UINT *count)
{
    UINT n = 0;
    for (XsInst *p = list; p; p = p->next)
        n++;
    *out = nullptr;
    *count = n;
    if (!n)
        return S_OK;
    PassedParam *v = xd_alloc(sizeof(PassedParam) * n);
    if (!v)
        return E_OUTOFMEMORY;
    UINT k = 0;
    for (XsInst *p = list; p; p = p->next, k++) {
        v[k].uri = p->name.uri;
        v[k].local = p->name.local;
        v[k].llen = p->name.llen;
        HRESULT hr = eval_binding(r, p, f, &v[k].val);
        if (FAILED(hr)) {
            free_passed(v, n);
            return hr;
        }
    }
    *out = v;
    return S_OK;
}

static BOOL mode_matches(const XsName *tm, const WCHAR *uri, const WCHAR *local)
{
    UINT l = local ? (UINT)xd_wlen(local) : 0;
    UINT tl = tm->local ? tm->llen : 0;
    if (!tl && !l)
        return TRUE;
    return tl == l && !memcmp(tm->local, local, (SIZE_T)l * sizeof(WCHAR)) && same_uri(tm->uri, uri);
}

static HRESULT run_template(XsRun *r, XsTemplate *t, const XsFrame *f, PassedParam *passed, UINT npassed);

static HRESULT find_template(XsRun *r, XNode *n, UINT pos, UINT size, const WCHAR *mode_uri, const WCHAR *mode,
                             int max_prec, int min_prec, XsTemplate **found)
{
    *found = nullptr;
    XpCall call = {n, pos, size, n, &r->host};
    for (UINT i = 0; i < r->sheet->ntemplates; i++) {
        XsTemplate *t = r->sheet->templates[i];
        if (!t->has_match || !t->pattern)
            continue;
        if (t->precedence > max_prec || t->precedence < min_prec)
            continue;
        if (!mode_matches(&t->mode, mode_uri, mode))
            continue;
        /* a quick look at the kind and name of the node first. */
        switch (t->quick) {
        case XPQ_ELEMENT_NAME:
            if (n->type != XN_ELEMENT || xd_local_length(n) != t->quick_len ||
                memcmp(xd_local(n), t->quick_local, (SIZE_T)t->quick_len * sizeof(WCHAR)))
                continue;
            break;
        case XPQ_ELEMENT:
            if (n->type != XN_ELEMENT)
                continue;
            break;
        case XPQ_ATTRIBUTE_NAME:
            if (n->type != XN_ATTRIBUTE || xd_local_length(n) != t->quick_len ||
                memcmp(xd_local(n), t->quick_local, (SIZE_T)t->quick_len * sizeof(WCHAR)))
                continue;
            break;
        case XPQ_ATTRIBUTE:
            if (n->type != XN_ATTRIBUTE)
                continue;
            break;
        case XPQ_TEXT:
            if (n->type != XN_TEXT && n->type != XN_CDATA)
                continue;
            break;
        case XPQ_COMMENT:
            if (n->type != XN_COMMENT)
                continue;
            break;
        case XPQ_PI:
            if (n->type != XN_PI)
                continue;
            break;
        case XPQ_ROOT:
            if (n->type != XN_DOCUMENT)
                continue;
            break;
        }
        BOOL matches = FALSE;
        HRESULT hr = xp_match(t->pattern, n, &call, &matches);
        if (FAILED(hr)) {
            if (!r->err->id)
                xsr_error(r, (UINT)hr, nullptr, nullptr, nullptr);
            return hr;
        }
        if (matches) {
            *found = t;
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT apply_to(XsRun *r, XNode *n, UINT pos, UINT size, const WCHAR *mode_uri, const WCHAR *mode,
                        PassedParam *passed, UINT npassed);

static HRESULT builtin_rule(XsRun *r, XNode *n, UINT pos, UINT size, const WCHAR *mode_uri, const WCHAR *mode)
{
    switch (n->type) {
    case XN_ELEMENT:
    case XN_DOCUMENT:
    case XN_FRAGMENT: {
        XSet kids = {};
        HRESULT hr = children_of(n, &kids);
        for (UINT i = 0; SUCCEEDED(hr) && i < kids.n; i++)
            hr = apply_to(r, kids.v[i], i + 1, kids.n, mode_uri, mode, nullptr, 0);
        xs_free(&kids);
        return hr;
    }
    case XN_TEXT:
    case XN_CDATA:
    case XN_ATTRIBUTE:
        out_text(r, n->value ? n->value : L"", n->length, FALSE);
        return S_OK;
    }
    return S_OK;
}

static HRESULT enter_frame(XsRun *r)
{
    if (++r->frames > (r->v6 ? 9216u : 32768u))
        return xsr_error(r, XE_XS_STACK, nullptr, nullptr, nullptr);
    /* the host stack is the limit of how deep a stylesheet can go. */
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    if ((ULONG_PTR)__builtin_frame_address(0) < low + 192 * 1024)
        return xsr_error(r, XE_XS_STACK, nullptr, nullptr, nullptr);
    return S_OK;
}

static HRESULT apply_to(XsRun *r, XNode *n, UINT pos, UINT size, const WCHAR *mode_uri, const WCHAR *mode,
                        PassedParam *passed, UINT npassed)
{
    XsTemplate *t;
    HRESULT hr = find_template(r, n, pos, size, mode_uri, mode, 0x7FFFFFFF, 0, &t);
    if (FAILED(hr))
        return hr;
    XsFrame f = {n, pos, size};
    if (t) {
        const WCHAR *sm = r->tmpl_mode, *su = r->tmpl_mode_uri;
        XsTemplate *st = r->tmpl;
        XNode *sn = r->tmpl_node;
        r->tmpl_mode = mode;
        r->tmpl_mode_uri = mode_uri;
        r->tmpl_node = n;
        hr = run_template(r, t, &f, passed, npassed);
        r->tmpl = st;
        r->tmpl_mode = sm;
        r->tmpl_mode_uri = su;
        r->tmpl_node = sn;
        return hr;
    }
    hr = enter_frame(r);
    if (FAILED(hr))
        return hr;
    hr = builtin_rule(r, n, pos, size, mode_uri, mode);
    r->frames--;
    return hr;
}

static HRESULT run_template_here(XsRun *r, XsTemplate *t, const XsFrame *f, PassedParam *passed, UINT npassed)
{
    HRESULT hr = enter_frame(r);
    if (FAILED(hr))
        return hr;
    UINT saved_base = r->base, saved_sp = r->sp;
    XsTemplate *saved_tmpl = r->tmpl;
    r->tmpl = t;
    r->base = r->sp;
    XsFrame fr = *f;
    for (XsInst *p = t->params; p && SUCCEEDED(hr); p = p->next) {
        XpVal v = {};
        PassedParam *given = nullptr;
        for (UINT i = 0; i < npassed; i++)
            if (same_name(passed[i].uri, passed[i].local, passed[i].llen, p->name.uri, p->name.local, p->name.llen))
                given = &passed[i];
        if (given) {
            if (!xpv_copy(&v, &given->val))
                hr = E_OUTOFMEMORY;
        } else {
            hr = eval_binding(r, p, &fr, &v);
        }
        if (SUCCEEDED(hr))
            hr = push_binding(r, p->name.uri, p->name.local, p->name.llen, &v);
        xpv_free(&v);
    }
    if (SUCCEEDED(hr))
        hr = run_list(r, t->body, &fr);
    pop_to(r, saved_sp);
    r->base = saved_base;
    r->tmpl = saved_tmpl;
    r->frames--;
    return hr;
}

/* a deep recursion goes on in a thread with a large stack, the calling thread waits. */
typedef struct BigCall {
    XsRun *r;
    XsTemplate *t;
    const XsFrame *f;
    PassedParam *passed;
    UINT npassed;
    HRESULT hr;
} BigCall;

static DWORD WINAPI big_stack_thread(void *arg)
{
    BigCall *c = arg;
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    BOOL saved = xd_text_mode(TRUE);
    c->r->on_big_stack = TRUE;
    c->hr = run_template_here(c->r, c->t, c->f, c->passed, c->npassed);
    xd_text_mode(saved);
    if (SUCCEEDED(init))
        CoUninitialize();
    return 0;
}

static HRESULT run_template(XsRun *r, XsTemplate *t, const XsFrame *f, PassedParam *passed, UINT npassed)
{
    if (!r->on_big_stack) {
        ULONG_PTR low, high;
        GetCurrentThreadStackLimits(&low, &high);
        if ((ULONG_PTR)__builtin_frame_address(0) < low + 384 * 1024) {
            BigCall call = {r, t, f, passed, npassed, E_FAIL};
            HANDLE thread = CreateThread(nullptr, sizeof(void *) == 8 ? (256u << 20) : (48u << 20), big_stack_thread,
                                         &call, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
            if (thread) {
                WaitForSingleObject(thread, INFINITE);
                CloseHandle(thread);
                r->on_big_stack = FALSE;
                return call.hr;
            }
        }
    }
    return run_template_here(r, t, f, passed, npassed);
}

static XsTemplate *find_named(XsRun *r, const XsName *name)
{
    XsTemplate *best = nullptr;
    for (UINT i = 0; i < r->sheet->ntemplates; i++) {
        XsTemplate *t = r->sheet->templates[i];
        if (t->has_name && same_name(t->name.uri, t->name.local, t->name.llen, name->uri, name->local, name->llen) &&
            (!best || t->precedence > best->precedence))
            best = t;
    }
    return best;
}

/* ------------------------------------------------------------------ names of elements and attributes */

/* a name to create a node with, in the namespace its prefix or the namespace attribute gives. */
static HRESULT make_name(XsRun *r, XsInst *i, const XsFrame *f, BOOL attribute, XBuf *qname, const WCHAR **uri,
                         XBuf *uri_buf)
{
    XBuf name = {};
    HRESULT hr = eval_avt(r, i->avt[0], f, &name);
    if (FAILED(hr)) {
        xb_free(&name);
        return hr;
    }
    const WCHAR *s = name.s ? name.s : L"";
    UINT len = name.length;
    while (len && xd_is_space(*s)) {
        s++;
        len--;
    }
    while (len && xd_is_space(s[len - 1]))
        len--;
    int bad = len ? xd_check_name(s, len, TRUE) : 0;
    UINT colon = len;
    for (UINT k = 0; k < len; k++)
        if (s[k] == ':') {
            colon = k;
            break;
        }
    if (!len) {
        xb_free(&name);
        return xsr_error(r, 0xC00CE231u, nullptr, nullptr, nullptr);
    }
    if (bad >= 0 || colon == 0 || (colon < len && colon + 1 == len)) {
        UINT at = bad >= 0 ? (UINT)bad : (colon == 0 ? 0 : len - 1);
        xse_name_error(r->err, s, len, at);
        xb_free(&name);
        return E_FAIL;
    }
    *uri = nullptr;
    if (i->avt[1]) {
        hr = eval_avt(r, i->avt[1], f, uri_buf);
        if (FAILED(hr)) {
            xb_free(&name);
            return hr;
        }
        if (uri_buf->length) {
            xb_putc(uri_buf, 0);
            *uri = uri_buf->s;
        }
    } else if (colon < len) {
        BOOL found;
        const WCHAR *u = xs_scope_lookup(i->scope, s, colon, &found);
        if (!found) {
            WCHAR p[80];
            UINT m = colon < 78 ? colon : 78;
            memcpy(p, s, m * sizeof(WCHAR));
            p[m] = 0;
            xb_free(&name);
            return xsr_error(r, XE_UNDECLARED_PREFIX, p, nullptr, nullptr);
        }
        if (*u) {
            xb_put(uri_buf, u, xd_wlen(u));
            xb_putc(uri_buf, 0);
            *uri = uri_buf->s;
        }
    } else if (!attribute) {
        BOOL found;
        const WCHAR *u = xs_scope_lookup(i->scope, L"", 0, &found);
        if (found && *u) {
            xb_put(uri_buf, u, xd_wlen(u));
            xb_putc(uri_buf, 0);
            *uri = uri_buf->s;
        }
    }
    xb_put(qname, s, len);
    xb_free(&name);
    return S_OK;
}

static BOOL is_xmlns_name(const WCHAR *s, UINT n)
{
    return n >= 5 && !memcmp(s, L"xmlns", 10) && (n == 5 || s[5] == ':');
}

/* the attributes of the attribute sets named, in the order of the sets and of the document. */
static HRESULT apply_set(XsRun *r, const XsName *name, const XsFrame *f, UINT depth);

static HRESULT apply_sets(XsRun *r, XsName *sets, UINT n, const XsFrame *f)
{
    for (UINT i = 0; i < n; i++) {
        HRESULT hr = apply_set(r, &sets[i], f, 0);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT apply_set(XsRun *r, const XsName *name, const XsFrame *f, UINT depth)
{
    if (depth > 32)
        return xsr_error(r, XE_XS_ATTRSET_CIRCULAR, name->qname, nullptr, nullptr);
    /* the sets of one name apply in the order of their precedence. */
    for (int prec = 0; prec <= r->sheet->next_precedence; prec++) {
        for (UINT i = 0; i < r->sheet->nsets; i++) {
            XsAttrSet *s = &r->sheet->sets[i];
            if (s->precedence != prec || !same_name(s->name.uri, s->name.local, s->name.llen, name->uri, name->local,
                                                    name->llen))
                continue;
            for (UINT k = 0; k < s->nuse; k++) {
                HRESULT hr = apply_set(r, &s->use[k], f, depth + 1);
                if (FAILED(hr))
                    return hr;
            }
            XsFrame fr = *f;
            HRESULT hr = run_list(r, s->body, &fr);
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ copying */

static void copy_namespaces(XsRun *r, XNode *src, XNode *dst);

static HRESULT copy_node_to_output(XsRun *r, XNode *src)
{
    switch (src->type) {
    case XN_DOCUMENT:
    case XN_FRAGMENT:
        for (XNode *c = src->first; c; c = c->next) {
            if (skip_child(c))
                continue;
            HRESULT hr = copy_node_to_output(r, c);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    case XN_ATTRIBUTE:
        out_attribute(r, src->qname, xd_atom_len(src->qname), src->uri, src->value ? src->value : L"", src->length);
        return S_OK;
    case XN_TEXT:
    case XN_CDATA:
        out_text(r, src->value ? src->value : L"", src->length, FALSE);
        return S_OK;
    case XN_ELEMENT: {
        XNode *c = xd_clone(src, r->out, TRUE);
        if (!c)
            return E_OUTOFMEMORY;
        xd_link(r->cur, c, nullptr);
        copy_namespaces(r, src, c);
        return S_OK;
    }
    case XN_COMMENT:
    case XN_PI: {
        XNode *c = xd_clone(src, r->out, FALSE);
        if (!c)
            return E_OUTOFMEMORY;
        xd_link(r->cur, c, nullptr);
        return S_OK;
    }
    }
    return S_OK;
}

/* the namespace declarations in scope at a source element, each prefix once, the nearest wins. */
static void copy_namespaces(XsRun *r, XNode *src, XNode *dst)
{
    for (XNode *e = src; e; e = e->parent) {
        if (e->type != XN_ELEMENT)
            continue;
        for (XNode *a = e->attrs; a; a = a->next) {
            if (!xd_is_xmlns_attr(a))
                continue;
            BOOL have = FALSE;
            for (XNode *d = dst->attrs; d; d = d->next)
                if (d->qname == a->qname || (xd_atom_len(d->qname) == xd_atom_len(a->qname) &&
                                             !memcmp(d->qname, a->qname, xd_atom_len(a->qname) * sizeof(WCHAR))))
                    have = TRUE;
            if (have)
                continue;
            XNode *n = out_new(r, XN_ATTRIBUTE, a->qname, xd_atom_len(a->qname), nullptr);
            if (!n)
                return;
            xd_node_set_value(n, a->value ? a->value : L"", a->length);
            xd_attr_link(dst, n);
        }
    }
}

/* ------------------------------------------------------------------ the instructions */

/* whether the end tag of an element with this body is written even when nothing comes out of it. */
static BOOL body_forces_end(const XsInst *body, BOOL v6, BOOL copy)
{
    for (const XsInst *b = body; b; b = b->next) {
        switch (b->op) {
        case XO_APPLY:
        case XO_FOR_EACH:
            return TRUE;
        case XO_COPY_OF:
            if (!copy)
                return TRUE;
            break;
        case XO_VALUE_OF:
        case XO_TEXT:
            if (!v6)
                return TRUE;
            break;
        default:
            break;
        }
    }
    return FALSE;
}

static HRESULT run_inst(XsRun *r, XsInst *i, XsFrame *f);

static HRESULT run_list(XsRun *r, XsInst *list, XsFrame *f)
{
    UINT mark = r->sp;
    HRESULT hr = S_OK;
    for (XsInst *i = list; i && SUCCEEDED(hr); i = i->next) {
        /* the elements an instruction wrote end a line when the stylesheet had white space after it and the output is
         * indented, for the instructions that make or copy elements. */
        BOOL marks = r->indent && i->ws_after && r->cur &&
                     (i->op == XO_LRE || i->op == XO_ELEMENT || i->op == XO_COPY || i->op == XO_COPY_OF);
        XNode *last = marks ? r->cur->last : nullptr;
        hr = run_inst(r, i, f);
        if (SUCCEEDED(hr) && marks)
            for (XNode *n = last ? last->next : r->cur->first; n; n = n->next)
                if (n->type == XN_ELEMENT)
                    n->flags |= XF_WS_AFTER;
    }
    pop_to(r, mark);
    return hr;
}

static HRESULT run_body(XsRun *r, XsInst *list, XsFrame *f)
{
    HRESULT hr = enter_frame(r);
    if (FAILED(hr))
        return hr;
    hr = run_list(r, list, f);
    r->frames--;
    return hr;
}

static HRESULT select_nodes(XsRun *r, XsInst *i, XsFrame *f, XSet *set)
{
    if (!i->select) {
        return children_of(f->node, set);
    }
    XpVal v = {};
    HRESULT hr = eval_x(r, i->select, f, &v);
    if (SUCCEEDED(hr) && v.t != XPV_SET) {
        xpv_free(&v);
        return xsr_error(r, XE_XS_NOT_NODESET, nullptr, nullptr, nullptr);
    }
    if (SUCCEEDED(hr)) {
        if (v.rtf) {
            /* the children of a fragment, as xsl:copy-of sees them; a path on it is not allowed. */
        }
        *set = v.set;
        memset(&v.set, 0, sizeof(v.set));
    }
    xpv_free(&v);
    return hr;
}

/* the prefix of an attribute in a namespace: its own unless the element holds it for another namespace, else a made one. */
static HRESULT attribute_prefix(XsRun *r, XBuf *q, const WCHAR *uri)
{
    UINT colon = q->length;
    for (UINT k = 0; k < q->length; k++)
        if (q->s[k] == ':') {
            colon = k;
            break;
        }
    BOOL make = colon == q->length;
    XNode *e = r->cur;
    if (!make && e && e->type == XN_ELEMENT) {
        if (e->local && e->local - 1 == (UINT)colon && !memcmp(e->qname, q->s, colon * sizeof(WCHAR)) &&
            !same_uri(e->uri, uri))
            make = TRUE;
        for (XNode *a = e->attrs; a && !make; a = a->next) {
            if (xd_is_xmlns_attr(a)) {
                UINT al = xd_atom_len(a->qname);
                if (al == colon + 6 && !memcmp(a->qname + 6, q->s, colon * sizeof(WCHAR)) && wcscmp(a->value ? a->value : L"", uri))
                    make = TRUE;
            } else if (a->local && a->local - 1 == (UINT)colon && !memcmp(a->qname, q->s, colon * sizeof(WCHAR)) &&
                       !same_uri(a->uri, uri)) {
                make = TRUE;
            }
        }
    }
    if (!make)
        return S_OK;
    UINT n = 0;
    while (n < r->nautons && wcscmp(r->autons[n], uri))
        n++;
    if (n == r->nautons) {
        WCHAR **list = xd_realloc(r->autons, (r->nautons + 1) * sizeof(WCHAR *));
        if (!list)
            return E_OUTOFMEMORY;
        r->autons = list;
        r->autons[r->nautons++] = xd_wcsdup(uri, xd_wlen(uri));
    }
    XBuf name = {};
    xb_puta(&name, "auto-ns");
    UINT v = n + 1, digits = 0, t = v;
    while (t) {
        digits++;
        t /= 10;
    }
    WCHAR num[12];
    for (UINT k = digits; k--; v /= 10)
        num[k] = (WCHAR)('0' + v % 10);
    xb_put(&name, num, digits);
    xb_putc(&name, ':');
    xb_put(&name, q->s + (colon == q->length ? 0 : colon + 1), q->length - (colon == q->length ? 0 : colon + 1));
    xb_free(q);
    *q = name;
    return S_OK;
}

static HRESULT run_inst(XsRun *r, XsInst *i, XsFrame *f)
{
    switch (i->op) {
    case XO_TEXT:
        out_text(r, i->text, i->tlen, i->doe);
        return S_OK;
    case XO_VALUE_OF: {
        XBuf b = {};
        HRESULT hr = eval_string(r, i->select, f, &b);
        if (SUCCEEDED(hr)) {
            out_text(r, b.s, b.length, i->doe);
            /* an empty string is an empty text node, which is text next to the element before it when indenting. */
            if (!b.length && r->indent && (!r->cur->last || r->cur->last->type != XN_TEXT)) {
                XNode *t = out_new(r, XN_TEXT, nullptr, 0, nullptr);
                if (t) {
                    xd_node_set_value(t, L"", 0);
                    xd_link(r->cur, t, nullptr);
                }
            }
        }
        xb_free(&b);
        return hr;
    }
    case XO_COPY_OF: {
        XpVal v = {};
        HRESULT hr = eval_x(r, i->select, f, &v);
        if (FAILED(hr)) {
            xpv_free(&v);
            return hr;
        }
        if (v.t == XPV_SET) {
            XSet set = v.set;
            if (!v.rtf)
                xs_sort(&set);
            for (UINT k = 0; k < set.n && SUCCEEDED(hr); k++)
                hr = copy_node_to_output(r, set.v[k]);
        } else {
            XBuf b = {};
            xpv_string(&v, &b);
            out_text(r, b.s, b.length, FALSE);
            xb_free(&b);
        }
        xpv_free(&v);
        return hr;
    }
    case XO_IF: {
        XpVal v = {};
        HRESULT hr = eval_x(r, i->test, f, &v);
        BOOL yes = SUCCEEDED(hr) && xpv_bool(&v);
        xpv_free(&v);
        if (SUCCEEDED(hr) && yes)
            hr = run_body(r, i->body, f);
        return hr;
    }
    case XO_CHOOSE: {
        for (XsInst *w = i->body; w; w = w->next) {
            BOOL yes = TRUE;
            if (w->op == XO_WHEN) {
                XpVal v = {};
                HRESULT hr = eval_x(r, w->test, f, &v);
                yes = SUCCEEDED(hr) && xpv_bool(&v);
                xpv_free(&v);
                if (FAILED(hr))
                    return hr;
            }
            if (yes)
                return run_body(r, w->body, f);
        }
        return S_OK;
    }
    case XO_FOR_EACH: {
        XSet set = {};
        HRESULT hr = select_nodes(r, i, f, &set);
        if (SUCCEEDED(hr))
            hr = sort_nodes(r, i->sorts, &set, f);
        UINT n = set.n;
        for (UINT k = 0; k < n && SUCCEEDED(hr); k++) {
            XsFrame nf = {set.v[k], k + 1, n};
            hr = run_body(r, i->body, &nf);
        }
        xs_free(&set);
        return hr;
    }
    case XO_APPLY: {
        XSet set = {};
        HRESULT hr = select_nodes(r, i, f, &set);
        if (SUCCEEDED(hr))
            hr = sort_nodes(r, i->sorts, &set, f);
        PassedParam *passed = nullptr;
        UINT np = 0;
        if (SUCCEEDED(hr))
            hr = eval_with_params(r, i->params, f, &passed, &np);
        /* the sheet's copy of a qualified name ends the local name, so a mode is a plain string. */
        const WCHAR *mode = i->has_mode ? i->mode.local : nullptr, *mode_uri = i->has_mode ? i->mode.uri : nullptr;
        UINT n = set.n;
        for (UINT k = 0; k < n && SUCCEEDED(hr); k++)
            hr = apply_to(r, set.v[k], k + 1, n, mode_uri, mode, passed, np);
        free_passed(passed, np);
        xs_free(&set);
        return hr;
    }
    case XO_APPLY_IMPORTS: {
        if (!r->tmpl || !r->tmpl_node)
            return S_OK;
        XsTemplate *t;
        int max_prec = r->tmpl->module->precedence - 1;
        HRESULT hr = find_template(r, r->tmpl_node, f->pos, f->size, r->tmpl_mode_uri, r->tmpl_mode, max_prec,
                                   r->tmpl->module->import_min, &t);
        if (FAILED(hr))
            return hr;
        XsFrame nf = {r->tmpl_node, f->pos, f->size};
        if (t)
            return run_template(r, t, &nf, nullptr, 0);
        hr = enter_frame(r);
        if (SUCCEEDED(hr)) {
            hr = builtin_rule(r, r->tmpl_node, f->pos, f->size, r->tmpl_mode_uri, r->tmpl_mode);
            r->frames--;
        }
        return hr;
    }
    case XO_CALL: {
        XsTemplate *t = find_named(r, &i->name);
        if (!t)
            return xsr_error(r, XE_XS_TEMPLATE_MISSING, i->name.qname, nullptr, nullptr);
        PassedParam *passed = nullptr;
        UINT np = 0;
        HRESULT hr = enter_frame(r);
        if (SUCCEEDED(hr)) {
            hr = eval_with_params(r, i->params, f, &passed, &np);
            if (SUCCEEDED(hr))
                hr = run_template(r, t, f, passed, np);
            r->frames--;
        }
        free_passed(passed, np);
        return hr;
    }
    case XO_VARIABLE: {
        XpVal v = {};
        HRESULT hr = eval_binding(r, i, f, &v);
        if (SUCCEEDED(hr))
            hr = push_binding(r, i->name.uri, i->name.local, i->name.llen, &v);
        xpv_free(&v);
        return hr;
    }
    case XO_PARAM:
        return S_OK;
    case XO_LRE: {
        XNode *e;
        HRESULT hr = out_element(r, i->name.qname, i->name.qlen, i->name.uri, &e);
        if (FAILED(hr))
            return hr;
        if (i->doe || body_forces_end(i->body, r->v6, FALSE))
            e->flags |= XF_EXPLICIT;
        if (r->indent && i->ws_inside)
            e->flags |= XF_WS_INSIDE;
        for (UINT k = 0; k < i->nnsdecl; k++) {
            XBuf q = {};
            if (i->nsdecl[k].prefix[0]) {
                xb_puta(&q, "xmlns:");
                xb_put(&q, i->nsdecl[k].prefix, xd_wlen(i->nsdecl[k].prefix));
            } else {
                xb_puta(&q, "xmlns");
            }
            out_attribute(r, q.s, q.length, nullptr, i->nsdecl[k].uri, xd_wlen(i->nsdecl[k].uri));
            xb_free(&q);
        }
        if (i->nsets)
            hr = apply_sets(r, i->sets, i->nsets, f);
        for (UINT k = 0; k < i->nattrs && SUCCEEDED(hr); k++) {
            XBuf v = {};
            hr = eval_avt(r, i->attrs[k].value, f, &v);
            if (SUCCEEDED(hr))
                out_attribute(r, i->attrs[k].name.qname, i->attrs[k].name.qlen, i->attrs[k].name.uri, v.s, v.length);
            xb_free(&v);
        }
        if (SUCCEEDED(hr))
            hr = run_body(r, i->body, f);
        r->cur = e->parent;
        return hr;
    }
    case XO_ELEMENT: {
        XBuf q = {}, u = {};
        const WCHAR *uri;
        HRESULT hr = make_name(r, i, f, FALSE, &q, &uri, &u);
        if (FAILED(hr)) {
            xb_free(&q);
            xb_free(&u);
            return hr;
        }
        XNode *e;
        hr = out_element(r, q.s, q.length, uri, &e);
        xb_free(&q);
        xb_free(&u);
        if (FAILED(hr))
            return hr;
        if (body_forces_end(i->body, r->v6, FALSE))
            e->flags |= XF_EXPLICIT;
        if (i->nsets)
            hr = apply_sets(r, i->sets, i->nsets, f);
        if (SUCCEEDED(hr))
            hr = run_body(r, i->body, f);
        r->cur = e->parent;
        return hr;
    }
    case XO_ATTRIBUTE: {
        XBuf q = {}, u = {};
        const WCHAR *uri;
        HRESULT hr = make_name(r, i, f, TRUE, &q, &uri, &u);
        if (FAILED(hr)) {
            xb_free(&q);
            xb_free(&u);
            return hr;
        }
        if (is_xmlns_name(q.s, q.length)) {
            xb_putc(&q, 0);
            hr = xsr_error(r, XE_XS_ATTR_BAD, L"name", q.s, nullptr);
            xb_free(&q);
            xb_free(&u);
            return hr;
        }
        if (uri) {
            hr = attribute_prefix(r, &q, uri);
            if (FAILED(hr)) {
                xb_free(&q);
                xb_free(&u);
                return hr;
            }
        }
        XBuf v = {};
        hr = run_to_string(r, i->body, f, &v);
        if (SUCCEEDED(hr))
            out_attribute(r, q.s, q.length, uri, v.s, v.length);
        xb_free(&v);
        xb_free(&q);
        xb_free(&u);
        return hr;
    }
    case XO_COMMENT: {
        XBuf v = {};
        HRESULT hr = run_to_string(r, i->body, f, &v);
        if (SUCCEEDED(hr)) {
            XBuf c = {};
            for (UINT k = 0; k < v.length; k++) {
                if (v.s[k] == '-' && (k + 1 == v.length || v.s[k + 1] == '-')) {
                    xb_putc(&c, '-');
                    xb_putc(&c, ' ');
                } else {
                    xb_putc(&c, v.s[k]);
                }
            }
            out_leaf(r, XN_COMMENT, nullptr, c.s ? c.s : L"", c.length);
            xb_free(&c);
        }
        xb_free(&v);
        return hr;
    }
    case XO_PI: {
        XBuf name = {}, v = {};
        HRESULT hr = eval_avt(r, i->avt[0], f, &name);
        if (SUCCEEDED(hr)) {
            const WCHAR *s = name.s ? name.s : L"";
            UINT len = name.length;
            while (len && xd_is_space(*s)) {
                s++;
                len--;
            }
            while (len && xd_is_space(s[len - 1]))
                len--;
            int bad = len ? xd_check_name(s, len, FALSE) : 0;
            if (!len) {
                hr = xsr_error(r, 0xC00CE231u, nullptr, nullptr, nullptr);
            } else if (bad >= 0) {
                xse_name_error(r->err, s, len, (UINT)bad);
                hr = E_FAIL;
            } else if (len == 3 && (s[0] | 32) == 'x' && (s[1] | 32) == 'm' && (s[2] | 32) == 'l') {
                hr = xsr_error(r, 0xC00CE227u, L"PI", L"xml", nullptr);
            } else {
                hr = run_to_string(r, i->body, f, &v);
                if (SUCCEEDED(hr)) {
                    XBuf c = {};
                    for (UINT k = 0; k < v.length; k++) {
                        xb_putc(&c, v.s[k]);
                        if (v.s[k] == '?' && k + 1 < v.length && v.s[k + 1] == '>')
                            xb_putc(&c, ' ');
                    }
                    WCHAR *nm = xd_wcsdup(s, len);
                    out_leaf(r, XN_PI, nm, c.s ? c.s : L"", c.length);
                    xd_free(nm);
                    xb_free(&c);
                }
            }
        }
        xb_free(&name);
        xb_free(&v);
        return hr;
    }
    case XO_COPY: {
        XNode *n = f->node;
        switch (n->type) {
        case XN_ELEMENT: {
            XNode *e;
            HRESULT hr = out_element(r, n->qname, xd_atom_len(n->qname), n->uri, &e);
            if (FAILED(hr))
                return hr;
            copy_namespaces(r, n, e);
            if (body_forces_end(i->body, r->v6, TRUE))
                e->flags |= XF_EXPLICIT;
            if (i->nsets)
                hr = apply_sets(r, i->sets, i->nsets, f);
            if (SUCCEEDED(hr))
                hr = run_body(r, i->body, f);
            r->cur = e->parent;
            return hr;
        }
        case XN_ATTRIBUTE:
            out_attribute(r, n->qname, xd_atom_len(n->qname), n->uri, n->value ? n->value : L"", n->length);
            return S_OK;
        case XN_TEXT:
        case XN_CDATA:
            out_text(r, n->value ? n->value : L"", n->length, FALSE);
            return S_OK;
        case XN_COMMENT:
        case XN_PI:
            return copy_node_to_output(r, n);
        case XN_DOCUMENT:
        case XN_FRAGMENT:
            return run_body(r, i->body, f);
        }
        return S_OK;
    }
    case XO_NUMBER: {
        XBuf b = {};
        HRESULT hr = xsr_number(r, i, f, &b);
        if (SUCCEEDED(hr))
            out_text(r, b.s, b.length, FALSE);
        xb_free(&b);
        return hr;
    }
    case XO_MESSAGE: {
        XBuf b = {};
        HRESULT hr = run_to_string(r, i->body, f, &b);
        if (SUCCEEDED(hr) && i->level == 1) {
            xb_putc(&b, 0);
            xse_set_text(r->err, E_FAIL, b.s);
            hr = E_FAIL;
        } else if (SUCCEEDED(hr) && r->req->onmessage) {
            xb_putc(&b, 0);
            r->req->onmessage(r->req->user, b.s);
        }
        xb_free(&b);
        return hr;
    }
    case XO_FALLBACK:
        if (i->fallback_only)
            return xsr_error(r, XE_XS_KEYWORD_HERE, i->name.qname, nullptr, nullptr);
        return run_body(r, i->body, f);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the whole run */

/* a copy of the source tree with the white space only text of the elements to strip left out. */
static BOOL strip_matches(XsSheet *s, XNode *e)
{
    int best = -1;
    int best_prec = -1;
    double best_prio = -2;
    UINT best_order = 0;
    for (UINT i = 0; i < s->nspaces; i++) {
        XsSpace *sp = &s->spaces[i];
        double prio;
        if (sp->kind == 2) {
            prio = -0.5;
        } else if (sp->kind == 1) {
            if (!same_uri(e->uri, sp->uri))
                continue;
            prio = -0.25;
        } else {
            if (xd_local_length(e) != sp->llen || memcmp(xd_local(e), sp->local, (SIZE_T)sp->llen * sizeof(WCHAR)) ||
                !same_uri(e->uri, sp->uri))
                continue;
            prio = 0;
        }
        if (best < 0 || sp->precedence > best_prec ||
            (sp->precedence == best_prec && (prio > best_prio || (prio == best_prio && sp->order > best_order)))) {
            best = (int)i;
            best_prec = sp->precedence;
            best_prio = prio;
            best_order = sp->order;
        }
    }
    return best >= 0 && s->spaces[best].strip;
}

static BOOL ws_only(const XNode *t)
{
    for (UINT i = 0; i < t->length; i++)
        if (!xd_is_space(t->value[i]))
            return FALSE;
    return TRUE;
}

static BOOL has_strippable(XsRun *r, XNode *root)
{
    XNode *n = root;
    for (;;) {
        if ((n->type == XN_TEXT || n->type == XN_CDATA) && ws_only(n) && n->parent && n->parent->type == XN_ELEMENT &&
            strip_matches(r->sheet, n->parent)) {
            BOOL preserved = FALSE;
            for (XNode *p = n->parent; p && !preserved; p = p->parent) {
                XNode *a = p->type == XN_ELEMENT ? xd_attr_find(p, L"xml:space", 9) : nullptr;
                if (a && a->value && a->length == 8 && !memcmp(a->value, L"preserve", 16))
                    preserved = TRUE;
                else if (a && a->value && a->length == 7 && !memcmp(a->value, L"default", 14))
                    break;
            }
            if (!preserved)
                return TRUE;
        }
        if (n->first) {
            n = n->first;
            continue;
        }
        while (n != root && !n->next)
            n = n->parent;
        if (n == root)
            return FALSE;
        n = n->next;
    }
}

/* clones with the stripped text left out; map receives the clone of mark. */
static XNode *strip_clone(XsRun *r, XDoc *into, XNode *src, XNode *mark, XNode **mapped)
{
    XNode *c = xd_clone(src, into, FALSE);
    if (!c)
        return nullptr;
    if (src == mark)
        *mapped = c;
    for (XNode *k = src->first; k; k = k->next) {
        if ((k->type == XN_TEXT || k->type == XN_CDATA) && src->type == XN_ELEMENT && ws_only(k) &&
            strip_matches(r->sheet, src)) {
            BOOL preserved = FALSE;
            for (XNode *p = src; p && !preserved; p = p->parent) {
                XNode *a = p->type == XN_ELEMENT ? xd_attr_find(p, L"xml:space", 9) : nullptr;
                if (a && a->value && a->length == 8 && !memcmp(a->value, L"preserve", 16))
                    preserved = TRUE;
                else if (a && a->value && a->length == 7 && !memcmp(a->value, L"default", 14))
                    break;
            }
            if (!preserved)
                continue;
        }
        XNode *kc = strip_clone(r, into, k, mark, mapped);
        if (kc)
            xd_link(c, kc, nullptr);
    }
    if (src->attrs && src->attrs != c->attrs) {
        /* clone_one already copied the attributes. */
    }
    return c;
}

static HRESULT start_run(XsRun *r)
{
    XsRequest *req = r->req;
    XNode *input = req->input;
    if (r->sheet->has_strip) {
        XNode *root = input;
        while (root->parent)
            root = root->parent;
        if (has_strippable(r, root)) {
            XDoc *d = xsr_new_temp_doc(r);
            if (!d)
                return E_OUTOFMEMORY;
            XNode *mapped = nullptr;
            XNode *copy = strip_clone(r, d, root, input, &mapped);
            if (!copy)
                return E_OUTOFMEMORY;
            /* the clone of a document is a node of type document in the new document, whose own root is its parent. */
            if (root->type == XN_DOCUMENT) {
                for (XNode *k = copy->first; k;) {
                    XNode *next = k->next;
                    xd_unlink(k);
                    xd_link(&d->node, k, nullptr);
                    k = next;
                }
                if (input == root)
                    mapped = &d->node;
            } else {
                xd_link(&d->node, copy, nullptr);
            }
            input = mapped ? mapped : input;
        }
    }
    XNode *top = input;
    while (top->parent)
        top = top->parent;
    r->input_root = top;
    req->input = input;
    return S_OK;
}

void xsr_free_state(XsRun *r)
{
    pop_to(r, 0);
    xd_free(r->stack);
    for (UINT i = 0; i < r->sheet->nglobals; i++)
        xpv_free(&r->gval[i]);
    xd_free(r->gval);
    xd_free(r->gstate);
    for (UINT i = 0; i < r->nautons; i++)
        xd_free(r->autons[i]);
    xd_free(r->autons);
    for (UINT i = 0; i < r->ntemps; i++)
        xn_release(&r->temps[i]->node);
    xd_free(r->temps);
    for (UINT i = 0; i < r->nloaded; i++)
        xd_free(r->loaded[i].url);
    xd_free(r->loaded);
    for (UINT i = 0; i < r->nkeys; i++) {
        for (UINT k = 0; k < r->keys[i].n; k++)
            xd_free((WCHAR *)r->keys[i].entries[k].value);
        xd_free(r->keys[i].entries);
    }
    xd_free(r->keys);
    xd_free(r->seen);
}

HRESULT xs_run(XsRequest *req)
{
    XsSheet *sheet = req->sheet;
    XsRun run = {};
    run.req = req;
    run.sheet = sheet;
    run.v6 = req->v6;
    run.indent = req->sheet->output.indent == 1;
    run.err = &req->error;
    run.host.rtf_error = host_rtf_error;
    run.host.known = xsr_known_function;
    run.host.function = xsr_function;
    run.host.variable = host_variable;
    run.host.user = &run;
    run.gval = xd_alloc(sizeof(XpVal) * (sheet->nglobals + 1));
    run.gstate = xd_alloc(sheet->nglobals + 1);
    run.out = xd_doc_new(req->v6 ? XDF_V6 : 0);
    if (!run.gval || !run.gstate || !run.out) {
        xsr_free_state(&run);
        if (run.out)
            xn_release(&run.out->node);
        return E_OUTOFMEMORY;
    }
    run.cur = &run.out->node;
    HRESULT hr = S_OK;
    if (sheet->literal) {
        for (XNode *c = sheet->literal->first; c; c = c->next) {
            XNode *k = xd_clone(c, run.out, TRUE);
            if (k)
                xd_link(&run.out->node, k, nullptr);
        }
        req->result = run.out;
        xsr_free_state(&run);
        return S_OK;
    }
    BOOL saved_mode = xd_text_mode(TRUE);
    hr = start_run(&run);
    if (SUCCEEDED(hr))
        hr = xsr_start_scripts(&run);
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < sheet->nglobals && SUCCEEDED(hr); i++)
            hr = eval_global(&run, i);
    }
    if (SUCCEEDED(hr))
        hr = apply_to(&run, req->input, 1, 1, req->mode_uri, req->mode, nullptr, 0);
    xsr_script_free(&run);
    xsr_free_state(&run);
    xd_text_mode(saved_mode);
    if (FAILED(hr)) {
        if (!req->error.id)
            xse_set(&req->error, (UINT)hr, nullptr, nullptr, nullptr);
        xn_release(&run.out->node);
        return hr;
    }
    req->result = run.out;
    return S_OK;
}
