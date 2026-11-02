/* xsl:number. */
#include <xmldom/xslt_priv.h>

static void put_roman(XBuf *out, UINT n, BOOL upper)
{
    static const struct {
        UINT v;
        const char *s;
    } t[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
             {40, "xl"},  {10, "x"},   {9, "ix"},  {5, "v"},    {4, "iv"},  {1, "i"}};
    for (UINT i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        while (n >= t[i].v) {
            for (const char *p = t[i].s; *p; p++)
                xb_putc(out, (WCHAR)(upper ? *p - 32 : *p));
            n -= t[i].v;
        }
}

static void put_alpha(XBuf *out, UINT n, BOOL upper)
{
    WCHAR buf[16];
    UINT k = 0;
    while (n && k < 16) {
        n--;
        buf[k++] = (WCHAR)((upper ? 'A' : 'a') + n % 26);
        n /= 26;
    }
    while (k)
        xb_putc(out, buf[--k]);
}

static void put_decimal(XBuf *out, double v, UINT width, const WCHAR *gsep, UINT group)
{
    XBuf t = {};
    xd_format_number(v, &t);
    UINT len = t.length;
    UINT pad = width > len ? width - len : 0;
    UINT total = len + pad;
    for (UINT i = 0; i < total; i++) {
        if (i && group && gsep && (total - i) % group == 0)
            xb_put(out, gsep, xd_wlen(gsep));
        xb_putc(out, i < pad ? '0' : t.s[i - pad]);
    }
    xb_free(&t);
}

static BOOL alnum(WCHAR c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 0x7f;
}

void xsr_format_numbers(const WCHAR *format, UINT flen, const double *values, UINT n, const WCHAR *group_sep,
                        UINT group_size, const WCHAR *letter_value, XBuf *out)
{
    UINT i = 0;
    /* the prefix. */
    while (i < flen && !alnum(format[i]))
        xb_putc(out, format[i++]);
    if (i >= flen) {
        /* no token at all: the number as 1. */
        for (UINT k = 0; k < n; k++) {
            if (k)
                xb_putc(out, '.');
            put_decimal(out, values[k], 1, group_sep, group_size);
        }
        return;
    }
    const WCHAR *tok = format + i;
    UINT tlen = 0;
    const WCHAR *last_tok = tok;
    UINT last_tlen = 0;
    const WCHAR *sep = L".";
    UINT sep_len = 1;
    BOOL first = TRUE;
    for (UINT k = 0; k < n; k++) {
        if (i < flen && alnum(format[i])) {
            tok = format + i;
            tlen = 0;
            while (i < flen && alnum(format[i])) {
                i++;
                tlen++;
            }
            last_tok = tok;
            last_tlen = tlen;
            if (!first) {
                /* the separator before this token was taken below. */
            }
        } else if (k > 0 && i >= flen) {
            tok = last_tok;
            tlen = last_tlen;
        }
        if (k > 0) {
            xb_put(out, sep, sep_len);
        }
        first = FALSE;
        double v = values[k];
        WCHAR c = tlen ? tok[0] : '1';
        UINT64 iv = v < 0 ? 0 : (UINT64)(v + 0.5);
        if (tlen == 1 && c == 'a')
            put_alpha(out, (UINT)iv, FALSE);
        else if (tlen == 1 && c == 'A')
            put_alpha(out, (UINT)iv, TRUE);
        else if (tlen == 1 && c == 'i')
            put_roman(out, (UINT)iv, FALSE);
        else if (tlen == 1 && c == 'I')
            put_roman(out, (UINT)iv, TRUE);
        else {
            UINT width = 1;
            BOOL digits = tlen > 1;
            for (UINT q = 0; q < tlen; q++)
                if (tok[q] < '0' || tok[q] > '9')
                    digits = FALSE;
            if (digits && tok[tlen - 1] == '1')
                width = tlen;
            put_decimal(out, v, width, group_sep, group_size);
        }
        /* the separator that follows this token, up to the next one. */
        if (i < flen && !alnum(format[i])) {
            UINT s0 = i;
            while (i < flen && !alnum(format[i]))
                i++;
            if (i < flen) {
                sep = format + s0;
                sep_len = i - s0;
            } else {
                /* the suffix, written after the last number. */
                if (k + 1 == n)
                    xb_put(out, format + s0, i - s0);
                else {
                    sep = format + s0;
                    sep_len = i - s0;
                }
            }
        }
    }
    (void)letter_value;
}

static BOOL count_matches(XsRun *r, XsInst *inst, XNode *cur, XNode *n, const XsFrame *f)
{
    if (inst->count) {
        XpCall call = {n, 1, 1, n, &r->host, nullptr, 0};
        BOOL m = FALSE;
        xp_match(inst->count, n, &call, &m);
        return m;
    }
    if (n->type != cur->type)
        return FALSE;
    if (n->type == XN_ELEMENT || n->type == XN_ATTRIBUTE || n->type == XN_PI)
        return xd_local_length(n) == xd_local_length(cur) &&
               !memcmp(xd_local(n), xd_local(cur), (SIZE_T)xd_local_length(n) * sizeof(WCHAR)) &&
               !wcscmp(n->uri ? n->uri : L"", cur->uri ? cur->uri : L"");
    return TRUE;
}

static BOOL from_matches(XsRun *r, XsInst *inst, XNode *n)
{
    if (!inst->from)
        return FALSE;
    XpCall call = {n, 1, 1, n, &r->host, nullptr, 0};
    BOOL m = FALSE;
    xp_match(inst->from, n, &call, &m);
    return m;
}

static double sibling_count(XsRun *r, XsInst *inst, XNode *cur, XNode *n, const XsFrame *f)
{
    double c = 1;
    for (XNode *p = n->prev; p; p = p->prev)
        if (count_matches(r, inst, cur, p, f))
            c++;
    return c;
}

static XNode *prev_in_doc(XNode *n)
{
    if (n->prev) {
        XNode *p = n->prev;
        while (p->last)
            p = p->last;
        return p;
    }
    return n->parent;
}

HRESULT xsr_number(XsRun *r, XsInst *inst, XsFrame *f, XBuf *out)
{
    double values[256];
    UINT n = 0;
    XBuf attr = {};
    HRESULT hr = S_OK;
    if (inst->has_select) {
        XpCall call = {f->node, f->pos, f->size, f->node, &r->host, nullptr, 0};
        XpVal v = {};
        hr = xp_eval(inst->select, &call, &v);
        if (FAILED(hr)) {
            xpv_free(&v);
            return hr;
        }
        double d = xpv_number(&v);
        xpv_free(&v);
        if (xd_isnan(d)) {
            xb_puta(out, "-2147483648");
            return S_OK;
        }
        if (d < 0.5) {
            xd_format_number(d < 0 ? -(double)(UINT64)(-d + 0.5) : 0, out);
            return S_OK;
        }
        values[n++] = (double)(UINT64)(d + 0.5);
    } else {
        XNode *cur = f->node;
        if (inst->level == 0) {
            XNode *target = nullptr;
            for (XNode *a = cur; a; a = a->parent) {
                if (count_matches(r, inst, cur, a, f)) {
                    target = a;
                    break;
                }
                if (from_matches(r, inst, a))
                    break;
            }
            if (target) {
                if (inst->from) {
                    BOOL under = FALSE;
                    for (XNode *a = target->parent; a; a = a->parent)
                        if (from_matches(r, inst, a))
                            under = TRUE;
                    if (!under && !from_matches(r, inst, target)) {
                        target = nullptr;
                    }
                }
            }
            if (target)
                values[n++] = sibling_count(r, inst, cur, target, f);
        } else if (inst->level == 1) {
            XNode *chain[256];
            UINT k = 0;
            for (XNode *a = cur; a && k < 256; a = a->parent) {
                if (count_matches(r, inst, cur, a, f))
                    chain[k++] = a;
                if (from_matches(r, inst, a))
                    break;
            }
            while (k)
                values[n++] = sibling_count(r, inst, cur, chain[--k], f);
        } else {
            double c = 0;
            BOOL any_match = FALSE, found_from = !inst->from;
            for (XNode *a = cur; a; a = prev_in_doc(a)) {
                if (count_matches(r, inst, cur, a, f)) {
                    c++;
                    any_match = TRUE;
                }
                if (from_matches(r, inst, a)) {
                    found_from = TRUE;
                    break;
                }
            }
            if (!found_from) {
                if (!r->v6)
                    values[n++] = 0;
            } else if (any_match) {
                values[n++] = c;
            }
        }
    }
    if (!n)
        return S_OK;
    const WCHAR *format = L"1";
    UINT flen = 1;
    XBuf fb = {}, gs = {}, gz = {}, lv = {};
    XsFrame af = *f;
    (void)af;
    XpCall call = {f->node, f->pos, f->size, f->node, &r->host, nullptr, 0};
    (void)call;
    if (inst->avt[0]) {
        for (UINT i = 0; i < inst->avt[0]->n; i++) {
            XsAvtPart *p = &inst->avt[0]->parts[i];
            if (p->expr) {
                XpVal v = {};
                XpCall c2 = {f->node, f->pos, f->size, f->node, &r->host, nullptr, 0};
                hr = xp_eval(p->expr, &c2, &v);
                if (SUCCEEDED(hr))
                    xpv_string(&v, &fb);
                xpv_free(&v);
            } else {
                xb_put(&fb, p->lit, p->len);
            }
        }
        if (FAILED(hr))
            return hr;
        format = fb.s ? fb.s : L"";
        flen = fb.length;
    }
    const WCHAR *gsep = nullptr;
    UINT gsize = 0;
    if (inst->avt[3] && inst->avt[4]) {
        for (int w = 3; w <= 4; w++) {
            XBuf *dst = w == 3 ? &gs : &gz;
            for (UINT i = 0; i < inst->avt[w]->n; i++) {
                XsAvtPart *p = &inst->avt[w]->parts[i];
                if (p->expr) {
                    XpVal v = {};
                    XpCall c2 = {f->node, f->pos, f->size, f->node, &r->host, nullptr, 0};
                    hr = xp_eval(p->expr, &c2, &v);
                    if (SUCCEEDED(hr))
                        xpv_string(&v, dst);
                    xpv_free(&v);
                } else {
                    xb_put(dst, p->lit, p->len);
                }
            }
        }
        if (SUCCEEDED(hr) && gs.length && gz.length) {
            xb_putc(&gs, 0);
            gsep = gs.s;
            gsize = (UINT)xd_parse_number(gz.s, gz.length);
        }
    }
    xsr_format_numbers(format, flen, values, n, gsep, gsize, nullptr, out);
    xb_free(&fb);
    xb_free(&gs);
    xb_free(&gz);
    xb_free(&lv);
    xb_free(&attr);
    return hr;
}
