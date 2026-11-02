/* the functions of xslt that xpath does not have, and the msxsl functions. */
#include <xmldom/xslt_priv.h>

void xsr_node_string(XNode *n, XBuf *out)
{
    xd_text_xsl(n, out);
}

BOOL xsr_known_function(void *user, const WCHAR *uri, const WCHAR *local, UINT length, UINT args)
{
    return TRUE;
}

static BOOL lit_eq(const WCHAR *s, UINT n, const char *lit)
{
    UINT i = 0;
    for (; i < n; i++)
        if (!lit[i] || s[i] != (UCHAR)lit[i])
            return FALSE;
    return lit[i] == 0;
}

static BOOL same_uri(const WCHAR *a, const WCHAR *b)
{
    return !wcscmp(a ? a : L"", b ? b : L"");
}

/* the string value of an argument. */
static void arg_str(XpVal *v, XBuf *out)
{
    if (v->t == XPV_SET) {
        if (v->set.n)
            xsr_node_string(v->set.v[0], out);
    } else {
        xpv_string(v, out);
    }
}

static HRESULT ret_str(XpVal *out, const WCHAR *s, UINT n)
{
    xpv_set_string(out, s, n);
    return S_OK;
}

static HRESULT ret_set(XpVal *out, XNode *n)
{
    out->t = XPV_SET;
    if (n && !xs_add(&out->set, n))
        return E_OUTOFMEMORY;
    return S_OK;
}

/* a qualified name written in the stylesheet, split and resolved with the namespaces of the call. */
static BOOL resolve_call_name(const XpCall *call, const WCHAR *q, UINT n, const WCHAR **uri, const WCHAR **local,
                              UINT *llen, BOOL *prefix_missing)
{
    *uri = nullptr;
    *prefix_missing = FALSE;
    UINT colon = n;
    for (UINT i = 0; i < n; i++)
        if (q[i] == ':') {
            colon = i;
            break;
        }
    if (colon == n) {
        *local = q;
        *llen = n;
        return TRUE;
    }
    *local = q + colon + 1;
    *llen = n - colon - 1;
    for (UINT i = 0; i < call->nns; i++) {
        if (xd_wlen(call->ns[i].prefix) == colon && !memcmp(call->ns[i].prefix, q, colon * sizeof(WCHAR))) {
            *uri = call->ns[i].uri;
            return TRUE;
        }
    }
    if (colon == 3 && lit_eq(q, 3, "xml")) {
        *uri = XML_NS;
        return TRUE;
    }
    *prefix_missing = TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ generate-id */

static UINT doc_index(XsRun *r, XDoc *d)
{
    for (UINT i = 0; i < r->nseen; i++)
        if (r->seen[i] == d)
            return i;
    if (r->nseen == r->capseen) {
        UINT cap = r->capseen ? r->capseen * 2 : 8;
        XDoc **v = xd_realloc(r->seen, cap * sizeof(XDoc *));
        if (!v)
            return 0;
        r->seen = v;
        r->capseen = cap;
    }
    r->seen[r->nseen] = d;
    return r->nseen++;
}

const WCHAR *xsr_doc_serial_id(XsRun *r, XNode *n, XBuf *out)
{
    xd_number_order(n->doc);
    XNode *root = n;
    while (root->parent)
        root = root->parent;
    UINT order;
    UINT doc;
    if (root->type == XN_DOCUMENT) {
        order = n->order;
        doc = doc_index(r, n->doc);
    } else {
        /* a detached tree is numbered as itself; its identity is its root. */
        UINT count = 0;
        XNode *c = root;
        for (;;) {
            if (c == n)
                break;
            count++;
            for (XNode *a = c->attrs; a; a = a->next) {
                if (a == n)
                    break;
                count++;
            }
            if (c->first) {
                c = c->first;
                continue;
            }
            while (c != root && !c->next)
                c = c->parent;
            if (c == root)
                break;
            c = c->next;
        }
        order = count + 1;
        doc = doc_index(r, n->doc) * 7 + 3;
    }
    /* the id of windows is 54 characters of digits and capitals, here a mix of the document and the position. */
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    UINT64 seed = ((UINT64)doc << 32) | order;
    xb_puta(out, "ID");
    for (int k = 0; k < 52; k++) {
        seed += 0x9E3779B97F4A7C15ull;
        UINT64 z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        xb_putc(out, (WCHAR)digits[z % 36]);
    }
    return out->s;
}

/* ------------------------------------------------------------------ format-number */

static const XsDecimal *find_decimal(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT llen, BOOL named)
{
    const XsDecimal *found = nullptr;
    static const XsDecimal def = {
        .decimal = '.', .grouping = ',', .percent = '%', .permille = 0x2030, .zero = '0', .digit = '#',
        .pattern = ';', .minus = '-', .infinity = (WCHAR *)L"Infinity", .nan = (WCHAR *)L"NaN"};
    for (UINT i = 0; i < r->sheet->ndecimals; i++) {
        const XsDecimal *d = &r->sheet->decimals[i];
        if (named) {
            if (d->has_name && d->name.llen == llen && !memcmp(d->name.local, local, llen * sizeof(WCHAR)) &&
                same_uri(d->name.uri, uri))
                found = d;
        } else if (!d->has_name) {
            found = d;
        }
    }
    if (!found && !named)
        return &def;
    return found;
}

typedef struct Subpattern {
    UINT prefix_start, prefix_len;
    UINT int_start, int_len;
    UINT frac_start, frac_len;
    UINT suffix_start, suffix_len;
    BOOL has_decimal;
    int min_int, min_frac, max_frac;
    int group_size;
    BOOL percent, permille;
} Subpattern;

static BOOL parse_subpattern(const WCHAR *p, UINT n, const XsDecimal *d, Subpattern *s)
{
    memset(s, 0, sizeof(*s));
    UINT i = 0;
    s->prefix_start = 0;
    while (i < n && p[i] != d->digit && p[i] != d->zero && p[i] != d->decimal && p[i] != d->grouping) {
        if (p[i] == d->percent)
            s->percent = TRUE;
        if (p[i] == d->permille)
            s->permille = TRUE;
        i++;
    }
    s->prefix_len = i;
    s->int_start = i;
    int last_group = -1;
    int digits_after_group = 0;
    BOOL seen_zero = FALSE;
    while (i < n && (p[i] == d->digit || p[i] == d->zero || p[i] == d->grouping)) {
        if (p[i] == d->grouping) {
            last_group = (int)i;
            digits_after_group = 0;
        } else {
            digits_after_group++;
            if (p[i] == d->zero) {
                seen_zero = TRUE;
                s->min_int++;
            } else if (seen_zero) {
                return FALSE;
            }
        }
        i++;
    }
    s->int_len = i - s->int_start;
    s->group_size = last_group >= 0 ? digits_after_group : 0;
    if (i < n && p[i] == d->decimal) {
        s->has_decimal = TRUE;
        i++;
        s->frac_start = i;
        BOOL seen_digit = FALSE;
        while (i < n && (p[i] == d->digit || p[i] == d->zero)) {
            if (p[i] == d->zero) {
                if (seen_digit)
                    return FALSE;
                s->min_frac++;
            } else {
                seen_digit = TRUE;
            }
            s->max_frac++;
            i++;
        }
        s->frac_len = i - s->frac_start;
    }
    s->suffix_start = i;
    while (i < n) {
        if (p[i] == d->percent)
            s->percent = TRUE;
        if (p[i] == d->permille)
            s->permille = TRUE;
        i++;
    }
    s->suffix_len = i - s->suffix_start;
    return TRUE;
}

/* the digits of a non negative number rounded to frac places, as integer digits and fraction digits. */
static void round_digits(double v, int max_frac, XBuf *ip, XBuf *fp)
{
    XBuf t = {};
    xd_format_number(v, &t);
    /* the shortest form may have an exponent for big and small numbers. xd_format_number writes plain decimal. */
    UINT dot = t.length;
    for (UINT i = 0; i < t.length; i++)
        if (t.s[i] == '.') {
            dot = i;
            break;
        }
    XBuf all = {};
    xb_put(&all, t.s, dot);
    UINT int_len = all.length;
    if (dot < t.length)
        xb_put(&all, t.s + dot + 1, t.length - dot - 1);
    UINT total = all.length;
    UINT keep = int_len + (UINT)max_frac;
    if (keep < total) {
        BOOL up = all.s[keep] >= '5';
        all.length = keep;
        if (up) {
            int k = (int)keep - 1;
            while (k >= 0) {
                if (all.s[k] == '9') {
                    all.s[k] = '0';
                    k--;
                } else {
                    all.s[k]++;
                    break;
                }
            }
            if (k < 0) {
                xb_putc(&all, 0);
                memmove(all.s + 1, all.s, all.length * sizeof(WCHAR));
                all.s[0] = '1';
                all.length++;
                int_len++;
            }
        }
    }
    xb_put(ip, all.s, int_len);
    if (all.length > int_len)
        xb_put(fp, all.s + int_len, all.length - int_len);
    xb_free(&t);
    xb_free(&all);
}

static HRESULT format_with(XsRun *r, double value, const WCHAR *pattern, UINT plen, const XsDecimal *d, XBuf *out)
{
    if (xd_isnan(value)) {
        xb_put(out, d->nan, xd_wlen(d->nan));
        return S_OK;
    }
    UINT sep = plen;
    for (UINT i = 0; i < plen; i++)
        if (pattern[i] == d->pattern) {
            sep = i;
            break;
        }
    Subpattern pos, neg;
    if (!parse_subpattern(pattern, sep, d, &pos))
        return xsr_error(r, 0xC00CE307u, nullptr, nullptr, nullptr);
    BOOL negative = value < 0 || (value == 0 && 1 / value < 0);
    const WCHAR *use = pattern;
    Subpattern *sp = &pos;
    UINT use_len = sep;
    BOOL explicit_neg = FALSE;
    if (sep < plen) {
        if (!parse_subpattern(pattern + sep + 1, plen - sep - 1, d, &neg))
            return xsr_error(r, 0xC00CE307u, nullptr, nullptr, nullptr);
        explicit_neg = TRUE;
        if (negative) {
            use = pattern + sep + 1;
            use_len = plen - sep - 1;
            sp = &neg;
        }
    }
    double v = negative ? -value : value;
    if (negative && !explicit_neg) {
        xb_putc(out, d->minus);
    }
    xb_put(out, use + sp->prefix_start, sp->prefix_len);
    if (v > 1.7976931348623157e308) {
        xb_put(out, d->infinity, xd_wlen(d->infinity));
    } else {
        if (sp->percent)
            v *= 100;
        else if (sp->permille)
            v *= 1000;
        XBuf ip = {}, fp = {};
        round_digits(v, sp->max_frac, &ip, &fp);
        /* strip insignificant zeros. */
        while (ip.length > 1 && ip.s[0] == '0')
            memmove(ip.s, ip.s + 1, (--ip.length) * sizeof(WCHAR));
        if (ip.length == 1 && ip.s[0] == '0')
            ip.length = 0;
        while (fp.length > (UINT)sp->min_frac && fp.s[fp.length - 1] == '0')
            fp.length--;
        while ((int)ip.length < sp->min_int)
            xb_putc(&ip, 0), memmove(ip.s + 1, ip.s, (ip.length - 1) * sizeof(WCHAR)), ip.s[0] = '0';
        while ((int)fp.length < sp->min_frac)
            xb_putc(&fp, '0');
        BOOL any = ip.length || fp.length;
        if (!any && sp->min_int == 0 && sp->min_frac == 0 && sp->int_len + sp->frac_len)
            ;
        for (UINT i = 0; i < ip.length; i++) {
            UINT left = ip.length - i;
            if (i && sp->group_size && left % (UINT)sp->group_size == 0)
                xb_putc(out, d->grouping);
            xb_putc(out, (WCHAR)(ip.s[i] - '0' + d->zero));
        }
        if (fp.length || (sp->has_decimal && sp->min_frac == 0 && 0)) {
            xb_putc(out, d->decimal);
            for (UINT i = 0; i < fp.length; i++)
                xb_putc(out, (WCHAR)(fp.s[i] - '0' + d->zero));
        } else if (sp->has_decimal && !sp->int_len && 0) {
            xb_putc(out, d->decimal);
        }
        xb_free(&ip);
        xb_free(&fp);
    }
    xb_put(out, use + sp->suffix_start, sp->suffix_len);
    (void)use_len;
    return S_OK;
}

/* ------------------------------------------------------------------ key */

static void key_pool_free(XsKeyIndex *k)
{
    for (UINT i = 0; i < k->n; i++)
        xd_free((WCHAR *)k->entries[i].value);
    xd_free(k->entries);
    k->entries = nullptr;
    k->n = 0;
}

static int key_cmp(const XsKeyEntry *a, const WCHAR *s, UINT n)
{
    UINT m = a->length < n ? a->length : n;
    int c = memcmp(a->value, s, m * sizeof(WCHAR));
    if (c)
        return c;
    return a->length < n ? -1 : a->length > n ? 1 : 0;
}

static int entry_cmp(const XsKeyEntry *a, const XsKeyEntry *b)
{
    int c = key_cmp(a, b->value, b->length);
    if (c)
        return c;
    return a->node->order < b->node->order ? -1 : a->node->order > b->node->order ? 1 : 0;
}

static void sort_entries(XsKeyEntry *v, XsKeyEntry *tmp, UINT n)
{
    if (n < 2)
        return;
    UINT mid = n / 2;
    sort_entries(v, tmp, mid);
    sort_entries(v + mid, tmp, n - mid);
    UINT i = 0, j = mid, o = 0;
    while (i < mid && j < n)
        tmp[o++] = entry_cmp(&v[j], &v[i]) < 0 ? v[j++] : v[i++];
    while (i < mid)
        tmp[o++] = v[i++];
    while (j < n)
        tmp[o++] = v[j++];
    memcpy(v, tmp, n * sizeof(XsKeyEntry));
}

static HRESULT add_entry(XsKeyIndex *k, UINT *cap, XNode *node, const WCHAR *s, UINT n)
{
    if (k->n == *cap) {
        UINT c = *cap ? *cap * 2 : 32;
        XsKeyEntry *v = xd_realloc(k->entries, c * sizeof(XsKeyEntry));
        if (!v)
            return E_OUTOFMEMORY;
        k->entries = v;
        *cap = c;
    }
    WCHAR *copy = xd_wcsdup(s, n);
    if (!copy)
        return E_OUTOFMEMORY;
    k->entries[k->n].value = copy;
    k->entries[k->n].length = n;
    k->entries[k->n].node = node;
    k->n++;
    return S_OK;
}

static HRESULT build_key_index(XsRun *r, XsKeyIndex *k, XNode *root)
{
    UINT cap = 0;
    xd_number_order(root->doc);
    XNode *n = root;
    for (;;) {
        for (int pass = 0; pass < 2; pass++) {
            XNode *cand = pass ? n->attrs : n;
            for (; cand; cand = pass ? cand->next : nullptr) {
                for (UINT i = 0; i < r->sheet->nkeys; i++) {
                    XsKey *key = &r->sheet->keys[i];
                    if (key->name.llen != k->llen || memcmp(key->name.local, k->local, k->llen * sizeof(WCHAR)) ||
                        !same_uri(key->name.uri, k->uri))
                        continue;
                    XpCall call = {cand, 1, 1, cand, &r->host, nullptr, 0};
                    BOOL m = FALSE;
                    HRESULT hr = xp_match(key->match, cand, &call, &m);
                    if (FAILED(hr))
                        return hr;
                    if (!m)
                        continue;
                    XpVal v = {};
                    hr = xp_eval(key->use, &call, &v);
                    if (FAILED(hr)) {
                        xpv_free(&v);
                        return hr;
                    }
                    if (v.t == XPV_SET) {
                        for (UINT q = 0; q < v.set.n && SUCCEEDED(hr); q++) {
                            XBuf b = {};
                            xsr_node_string(v.set.v[q], &b);
                            hr = add_entry(k, &cap, cand, b.s ? b.s : L"", b.length);
                            xb_free(&b);
                        }
                    } else {
                        XBuf b = {};
                        xpv_string(&v, &b);
                        hr = add_entry(k, &cap, cand, b.s ? b.s : L"", b.length);
                        xb_free(&b);
                    }
                    xpv_free(&v);
                    if (FAILED(hr))
                        return hr;
                }
            }
        }
        if (n->first) {
            n = n->first;
            continue;
        }
        while (n != root && !n->next)
            n = n->parent;
        if (n == root)
            break;
        n = n->next;
    }
    XsKeyEntry *tmp = xd_alloc(sizeof(XsKeyEntry) * (k->n + 1));
    if (!tmp)
        return E_OUTOFMEMORY;
    sort_entries(k->entries, tmp, k->n);
    xd_free(tmp);
    return S_OK;
}

static HRESULT key_lookup(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT llen, XNode *ctx, XpVal *value,
                          XpVal *out)
{
    BOOL defined = FALSE;
    for (UINT i = 0; i < r->sheet->nkeys; i++)
        if (r->sheet->keys[i].name.llen == llen && !memcmp(r->sheet->keys[i].name.local, local, llen * sizeof(WCHAR)) &&
            same_uri(r->sheet->keys[i].name.uri, uri))
            defined = TRUE;
    if (!defined)
        return xsr_error(r, XE_XS_KEY_MISSING, local, nullptr, nullptr);
    XNode *root = ctx;
    while (root->parent)
        root = root->parent;
    XsKeyIndex *k = nullptr;
    for (UINT i = 0; i < r->nkeys; i++)
        if (r->keys[i].doc == root->doc && r->keys[i].llen == llen &&
            !memcmp(r->keys[i].local, local, llen * sizeof(WCHAR)) && same_uri(r->keys[i].uri, uri))
            k = &r->keys[i];
    if (!k) {
        if (r->nkeys == r->capkeys) {
            UINT cap = r->capkeys ? r->capkeys * 2 : 8;
            XsKeyIndex *v = xd_realloc(r->keys, cap * sizeof(XsKeyIndex));
            if (!v)
                return E_OUTOFMEMORY;
            r->keys = v;
            r->capkeys = cap;
        }
        k = &r->keys[r->nkeys++];
        memset(k, 0, sizeof(*k));
        k->uri = uri;
        k->local = local;
        k->llen = llen;
        k->doc = root->doc;
        HRESULT hr = build_key_index(r, k, root);
        if (FAILED(hr)) {
            key_pool_free(k);
            r->nkeys--;
            return hr;
        }
    }
    out->t = XPV_SET;
    UINT nvals = value->t == XPV_SET ? value->set.n : 1;
    for (UINT q = 0; q < nvals; q++) {
        XBuf b = {};
        if (value->t == XPV_SET)
            xsr_node_string(value->set.v[q], &b);
        else
            xpv_string(value, &b);
        const WCHAR *s = b.s ? b.s : L"";
        UINT lo = 0, hi = k->n;
        while (lo < hi) {
            UINT mid = (lo + hi) / 2;
            if (key_cmp(&k->entries[mid], s, b.length) < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (UINT i = lo; i < k->n && key_cmp(&k->entries[i], s, b.length) == 0; i++)
            xs_add(&out->set, k->entries[i].node);
        xb_free(&b);
    }
    xs_sort(&out->set);
    return S_OK;
}

/* ------------------------------------------------------------------ document */

XNode *xsr_load_document(XsRun *r, const WCHAR *url, UINT len, const WCHAR *base)
{
    WCHAR *full = xs_join_location(base, url, len);
    if (!full)
        return nullptr;
    /* the fragment is not part of the location. */
    for (WCHAR *p = full; *p; p++)
        if (*p == '#') {
            *p = 0;
            break;
        }
    for (UINT i = 0; i < r->nloaded; i++)
        if (!xd_wcsicmp(r->loaded[i].url, full)) {
            xd_free(full);
            return r->loaded[i].root;
        }
    XDoc *d = xsr_new_temp_doc(r);
    XNode *root = nullptr;
    if (d && xd_load_file(d, full))
        root = &d->node;
    if (r->nloaded == r->caploaded) {
        UINT cap = r->caploaded ? r->caploaded * 2 : 8;
        XsLoaded *v = xd_realloc(r->loaded, cap * sizeof(XsLoaded));
        if (!v) {
            xd_free(full);
            return root;
        }
        r->loaded = v;
        r->caploaded = cap;
    }
    r->loaded[r->nloaded].url = full;
    r->loaded[r->nloaded].root = root;
    r->nloaded++;
    return root;
}

static HRESULT fn_document(XsRun *r, XpVal *args, UINT count, XpCall *call, XpVal *out)
{
    out->t = XPV_SET;
    if (!r->req->allow_doc_fn) {
        xse_set_text(r->err, E_FAIL, L"Operation aborted: AllowDocumentFunction constraint violated.\r\n");
        return E_FAIL;
    }
    const WCHAR *base_default = r->sheet->url;
    const WCHAR *base2 = nullptr;
    if (count == 2) {
        if (args[1].t != XPV_SET)
            return xsr_error(r, 0xC00CE336u, L"2", nullptr, nullptr);
        if (!args[1].set.n)
            base2 = L"";
        else
            base2 = args[1].set.v[0]->doc->url;
    }
    UINT nargs = args[0].t == XPV_SET ? args[0].set.n : 1;
    for (UINT i = 0; i < nargs; i++) {
        XBuf b = {};
        const WCHAR *base = base2 ? base2 : base_default;
        if (args[0].t == XPV_SET) {
            xsr_node_string(args[0].set.v[i], &b);
            if (!base2)
                base = args[0].set.v[i]->doc->url ? args[0].set.v[i]->doc->url : base_default;
        } else {
            xpv_string(&args[0], &b);
        }
        XNode *root;
        if (!b.length && args[0].t != XPV_SET && r->sheet->source) {
            root = r->sheet->source;
        } else {
            root = xsr_load_document(r, b.s ? b.s : L"", b.length, base);
        }
        xb_free(&b);
        if (root)
            xs_add(&out->set, root);
    }
    xs_sort(&out->set);
    return S_OK;
}

/* ------------------------------------------------------------------ the dispatch */

static const char *const k_elements[] = {"apply-imports", "apply-templates", "attribute", "attribute-set", "call-template",
                                         "choose", "comment", "copy", "copy-of", "decimal-format", "element", "fallback",
                                         "for-each", "if", "import", "include", "key", "message", "namespace-alias",
                                         "number", "otherwise", "output", "param", "preserve-space",
                                         "processing-instruction", "sort", "strip-space", "stylesheet", "template",
                                         "text", "transform", "value-of", "variable", "when", "with-param", nullptr};
static const char *const k_functions[] = {"boolean", "ceiling", "concat", "contains", "count", "current", "document",
                                          "element-available", "false", "floor", "format-number", "function-available",
                                          "generate-id", "id", "key", "lang", "last", "local-name", "name",
                                          "namespace-uri", "normalize-space", "not", "number", "position", "round",
                                          "starts-with", "string", "string-length", "substring", "substring-after",
                                          "substring-before", "sum", "system-property", "translate", "true",
                                          "unparsed-entity-uri", nullptr};

HRESULT xsr_function(void *user, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *args, UINT count,
                     XpCall *call, XpVal *out)
{
    XsRun *r = user;
    if (!uri) {
        if (lit_eq(local, length, "current")) {
            return ret_set(out, call->current ? call->current : call->node);
        }
        if (lit_eq(local, length, "document"))
            return fn_document(r, args, count, call, out);
        if (lit_eq(local, length, "key")) {
            XBuf name = {};
            arg_str(&args[0], &name);
            const WCHAR *ku, *kl;
            UINT kn;
            BOOL missing;
            XNode *ctx = call->node;
            xb_putc(&name, 0);
            if (!resolve_call_name(call, name.s, name.length - 1, &ku, &kl, &kn, &missing)) {
                HRESULT hr = xsr_error(r, XE_UNDECLARED_PREFIX, nullptr, nullptr, nullptr);
                xb_free(&name);
                return hr;
            }
            HRESULT hr = key_lookup(r, ku, kl, kn, ctx, &args[1], out);
            xb_free(&name);
            return hr;
        }
        if (lit_eq(local, length, "format-number")) {
            double v = xpv_number(&args[0]);
            XBuf pat = {}, fname = {};
            arg_str(&args[1], &pat);
            const XsDecimal *d;
            if (count == 3) {
                arg_str(&args[2], &fname);
                xb_putc(&fname, 0);
                const WCHAR *fu, *fl;
                UINT fn;
                BOOL missing;
                if (!resolve_call_name(call, fname.s, fname.length - 1, &fu, &fl, &fn, &missing)) {
                    xb_free(&pat);
                    xb_free(&fname);
                    return xsr_error(r, XE_UNDECLARED_PREFIX, nullptr, nullptr, nullptr);
                }
                d = find_decimal(r, fu, fl, fn, TRUE);
                if (!d) {
                    HRESULT hr = xsr_error(r, XE_XS_DECIMAL_MISSING, fname.s, nullptr, nullptr);
                    xb_free(&pat);
                    xb_free(&fname);
                    return hr;
                }
            } else {
                d = find_decimal(r, nullptr, L"", 0, FALSE);
            }
            XBuf res = {};
            HRESULT hr = format_with(r, v, pat.s ? pat.s : L"", pat.length, d, &res);
            if (SUCCEEDED(hr))
                ret_str(out, res.s ? res.s : L"", res.length);
            xb_free(&res);
            xb_free(&pat);
            xb_free(&fname);
            return hr;
        }
        if (lit_eq(local, length, "generate-id")) {
            XNode *n = nullptr;
            if (count) {
                if (args[0].t != XPV_SET)
                    return xsr_error(r, 0xC00CE336u, L"1", nullptr, nullptr);
                n = args[0].set.n ? args[0].set.v[0] : nullptr;
            } else {
                n = call->node;
            }
            XBuf b = {};
            if (n)
                xsr_doc_serial_id(r, n, &b);
            ret_str(out, b.s ? b.s : L"", b.length);
            xb_free(&b);
            return S_OK;
        }
        if (lit_eq(local, length, "system-property")) {
            XBuf name = {};
            arg_str(&args[0], &name);
            xb_putc(&name, 0);
            const WCHAR *nu, *nl;
            UINT nn;
            BOOL missing;
            const WCHAR *res = L"";
            if (resolve_call_name(call, name.s, name.length - 1, &nu, &nl, &nn, &missing)) {
                if (nu && !wcscmp(nu, XSLT_NS)) {
                    if (lit_eq(nl, nn, "version"))
                        res = L"1";
                    else if (lit_eq(nl, nn, "vendor"))
                        res = L"Microsoft";
                    else if (lit_eq(nl, nn, "vendor-url"))
                        res = L"http://www.microsoft.com";
                } else if (nu && !wcscmp(nu, MSXSL_NS) && lit_eq(nl, nn, "version")) {
                    res = r->v6 ? L"6" : L"3";
                }
            }
            ret_str(out, res, (UINT)xd_wlen(res));
            xb_free(&name);
            return S_OK;
        }
        if (lit_eq(local, length, "element-available")) {
            XBuf name = {};
            arg_str(&args[0], &name);
            xb_putc(&name, 0);
            const WCHAR *nu, *nl;
            UINT nn;
            BOOL missing;
            BOOL yes = FALSE;
            if (resolve_call_name(call, name.s, name.length - 1, &nu, &nl, &nn, &missing)) {
                if (nu && !wcscmp(nu, XSLT_NS)) {
                    for (const char *const *e = k_elements; *e; e++)
                        if (lit_eq(nl, nn, *e))
                            yes = TRUE;
                } else if (nu && !wcscmp(nu, MSXSL_NS) && lit_eq(nl, nn, "script")) {
                    yes = r->req->allow_script;
                }
            }
            out->t = XPV_BOOL;
            out->b = yes;
            xb_free(&name);
            return S_OK;
        }
        if (lit_eq(local, length, "function-available")) {
            XBuf name = {};
            arg_str(&args[0], &name);
            xb_putc(&name, 0);
            const WCHAR *nu, *nl;
            UINT nn;
            BOOL missing;
            BOOL yes = FALSE;
            if (resolve_call_name(call, name.s, name.length - 1, &nu, &nl, &nn, &missing)) {
                if (!nu) {
                    for (const char *const *e = k_functions; *e; e++)
                        if (lit_eq(nl, nn, *e))
                            yes = TRUE;
                } else if (!wcscmp(nu, MSXSL_NS)) {
                    yes = lit_eq(nl, nn, "node-set") || lit_eq(nl, nn, "string-compare") || lit_eq(nl, nn, "utc") ||
                          lit_eq(nl, nn, "local-name") || lit_eq(nl, nn, "namespace-uri") ||
                          lit_eq(nl, nn, "format-date") || lit_eq(nl, nn, "format-time") || lit_eq(nl, nn, "number");
                } else {
                    yes = xsr_has_script_function(r, nu, nl, nn);
                }
            }
            out->t = XPV_BOOL;
            out->b = yes;
            xb_free(&name);
            return S_OK;
        }
        if (lit_eq(local, length, "unparsed-entity-uri")) {
            ret_str(out, L"", 0);
            return S_OK;
        }
        return xsr_error(r, XE_XS_BAD_FUNCTION, local, nullptr, nullptr);
    }
    if (!wcscmp(uri, MSXSL_NS)) {
        if (lit_eq(local, length, "node-set")) {
            if (args[0].t == XPV_SET) {
                out->t = XPV_SET;
                for (UINT i = 0; i < args[0].set.n; i++)
                    xs_add(&out->set, args[0].set.v[i]);
                return S_OK;
            }
            /* a string, number or boolean becomes the text of a fragment. */
            XDoc *d = xsr_new_temp_doc(r);
            if (!d)
                return E_OUTOFMEMORY;
            XBuf b = {};
            xpv_string(&args[0], &b);
            XNode *t = xd_node_new(d, XN_TEXT, nullptr, 0, nullptr);
            xd_node_set_value(t, b.s ? b.s : L"", b.length);
            xd_link(&d->node, t, nullptr);
            xb_free(&b);
            return ret_set(out, &d->node);
        }
        if (lit_eq(local, length, "number")) {
            out->t = XPV_NUM;
            if (args[0].t == XPV_SET) {
                XBuf b = {};
                if (args[0].set.n)
                    xsr_node_string(args[0].set.v[0], &b);
                out->n = xd_parse_number(b.s ? b.s : L"", b.length);
                xb_free(&b);
            } else {
                out->n = xpv_number(&args[0]);
            }
            return S_OK;
        }
        if (lit_eq(local, length, "local-name") || lit_eq(local, length, "namespace-uri")) {
            XNode *n = args[0].t == XPV_SET && args[0].set.n ? args[0].set.v[0] : nullptr;
            out->t = XPV_STR;
            if (n) {
                if (lit_eq(local, length, "local-name")) {
                    if (n->qname)
                        xb_put(&out->s, xd_local(n), xd_local_length(n));
                } else if (n->uri) {
                    xb_put(&out->s, n->uri, xd_wlen(n->uri));
                }
            }
            return S_OK;
        }
        if (lit_eq(local, length, "string-compare")) {
            XBuf a = {}, b = {}, opts = {};
            arg_str(&args[0], &a);
            arg_str(&args[1], &b);
            if (count > 3)
                arg_str(&args[3], &opts);
            DWORD flags = 0;
            if (opts.length && opts.s[0] == 'i')
                flags |= NORM_IGNORECASE;
            LCID lcid = LOCALE_USER_DEFAULT;
            if (count > 2) {
                XBuf lang = {};
                arg_str(&args[2], &lang);
                if (lang.length) {
                    xb_putc(&lang, 0);
                    LCID l = LocaleNameToLCID(lang.s, 0);
                    if (l)
                        lcid = l;
                }
                xb_free(&lang);
            }
            int c = CompareStringW(lcid, flags, a.s ? a.s : L"", (int)a.length, b.s ? b.s : L"", (int)b.length);
            out->t = XPV_NUM;
            out->n = c == CSTR_LESS_THAN ? -1 : c == CSTR_GREATER_THAN ? 1 : 0;
            xb_free(&a);
            xb_free(&b);
            xb_free(&opts);
            return S_OK;
        }
        return xsr_error(r, XE_XS_NS_NO_FUNCTION, uri, local, nullptr);
    }
    return xsr_call_extension(r, uri, local, length, args, count, call, out);
}
