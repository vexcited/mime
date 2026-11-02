/* writing the result tree of a transformation as xml, html or text. */
#include <xmldom/xslt.h>

static const char *g_void_elements[] = {"area", "base", "basefont", "br", "col", "frame", "hr", "img", "input",
                                        "isindex", "link", "meta", "param", nullptr};
static const char *g_bool_attrs[] = {"checked", "compact", "declare", "defer", "disabled", "ismap", "multiple",
                                     "nohref", "noresize", "noshade", "nowrap", "readonly", "selected", nullptr};

static BOOL ieq(const WCHAR *w, UINT n, const char *a)
{
    UINT i = 0;
    for (; i < n; i++) {
        WCHAR c = w[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (!a[i] || c != (UCHAR)a[i])
            return FALSE;
    }
    return a[i] == 0;
}

static BOOL in_list(const char **list, const WCHAR *w, UINT n)
{
    for (; *list; list++)
        if (ieq(w, n, *list))
            return TRUE;
    return FALSE;
}

typedef struct Ser {
    XBuf *out;
    int method;
    BOOL v6;
    UINT max;
    const XsOutput *cfg;
    const WCHAR *encoding;
    struct Bind {
        const WCHAR *prefix;
        const WCHAR *uri;
    } *binds;
    UINT nbinds, capbinds;
    BOOL indent;
    BOOL pending; /* a line break is due before the next thing written. */
} Ser;

static void flush_pending(Ser *w)
{
    if (!w->pending)
        return;
    w->pending = FALSE;
    if (w->out->length && w->out->s[w->out->length - 1] != '\n')
        xb_puta(w->out, "\r\n");
}

/* white space the stylesheet had after a child ends a line, unless text follows. */
static BOOL child_breaks(const XNode *c)
{
    return c->type != XN_TEXT && (c->flags & XF_WS_AFTER) && (!c->next || c->next->type != XN_TEXT);
}

static void put_ref(XBuf *b, UINT c, BOOL hex)
{
    WCHAR digits[12];
    UINT n = 0;
    xb_puta(b, hex ? "&#x" : "&#");
    UINT base = hex ? 16 : 10;
    do {
        UINT d = c % base;
        digits[n++] = (WCHAR)(d < 10 ? '0' + d : 'A' + d - 10);
        c /= base;
    } while (c);
    while (n)
        xb_putc(b, digits[--n]);
    xb_putc(b, ';');
}

/* a character the encoding cannot hold is written as a reference. i is advanced over a surrogate pair. */
static BOOL put_wide(Ser *w, const WCHAR *s, UINT length, UINT *i)
{
    WCHAR c = s[*i];
    UINT cp = c;
    if (c >= 0xD800 && c < 0xDC00 && *i + 1 < length && s[*i + 1] >= 0xDC00 && s[*i + 1] < 0xE000) {
        cp = 0x10000 + (((UINT)c - 0xD800) << 10) + (s[*i + 1] - 0xDC00);
        if (cp > w->max) {
            put_ref(w->out, cp, FALSE);
            (*i)++;
            return TRUE;
        }
        return FALSE;
    }
    if (cp > w->max) {
        put_ref(w->out, cp, FALSE);
        return TRUE;
    }
    return FALSE;
}

static void put_text(Ser *w, const WCHAR *s, UINT length, BOOL escape_lt, BOOL doe)
{
    XBuf *b = w->out;
    for (UINT i = 0; i < length; i++) {
        WCHAR c = s[i];
        if (doe) {
            xb_putc(b, c);
            continue;
        }
        switch (c) {
        case '&':
            xb_puta(b, "&amp;");
            break;
        case '<':
            xb_puta(b, escape_lt ? "&lt;" : "<");
            break;
        case '>':
            xb_puta(b, "&gt;");
            break;
        case '\r':
            if (i + 1 < length && s[i + 1] == '\n')
                i++;
            xb_puta(b, "\r\n");
            break;
        case '\n':
            xb_puta(b, "\r\n");
            break;
        default:
            if (!put_wide(w, s, length, &i))
                xb_putc(b, c);
        }
    }
}

static BOOL is_uri_attr(const WCHAR *element, UINT elen, const WCHAR *attr, UINT alen)
{
    static const struct {
        const char *e;
        const char *a;
    } t[] = {{"a", "href"},     {"applet", "codebase"}, {"area", "href"},   {"base", "href"},
             {"blockquote", "cite"}, {"body", "background"}, {"del", "cite"}, {"form", "action"}, {"frame", "longdesc"},
             {"frame", "src"}, {"head", "profile"}, {"iframe", "longdesc"}, {"iframe", "src"},   {"img", "longdesc"},
             {"img", "src"},   {"img", "usemap"},   {"input", "src"},     {"input", "usemap"},   {"ins", "cite"},
             {"link", "href"}, {"object", "classid"}, {"object", "codebase"}, {"object", "data"}, {"object", "usemap"},
             {"q", "cite"},    {"script", "src"}};
    for (UINT i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (ieq(element, elen, t[i].e) && ieq(attr, alen, t[i].a))
            return TRUE;
    return FALSE;
}

/* a non ascii character of a uri attribute is escaped as the bytes of its utf-8 form. */
static void put_uri(Ser *w, const WCHAR *s, UINT length)
{
    XBuf *b = w->out;
    for (UINT i = 0; i < length; i++) {
        UINT cp = s[i];
        if (cp < 0x80) {
            if (cp == '&')
                xb_puta(b, "&amp;");
            else if (cp == '"')
                xb_puta(b, "&quot;");
            else
                xb_putc(b, (WCHAR)cp);
            continue;
        }
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < length && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        UINT8 bytes[4];
        UINT n;
        if (cp < 0x800) {
            bytes[0] = (UINT8)(0xC0 | (cp >> 6));
            bytes[1] = (UINT8)(0x80 | (cp & 0x3F));
            n = 2;
        } else if (cp < 0x10000) {
            bytes[0] = (UINT8)(0xE0 | (cp >> 12));
            bytes[1] = (UINT8)(0x80 | ((cp >> 6) & 0x3F));
            bytes[2] = (UINT8)(0x80 | (cp & 0x3F));
            n = 3;
        } else {
            bytes[0] = (UINT8)(0xF0 | (cp >> 18));
            bytes[1] = (UINT8)(0x80 | ((cp >> 12) & 0x3F));
            bytes[2] = (UINT8)(0x80 | ((cp >> 6) & 0x3F));
            bytes[3] = (UINT8)(0x80 | (cp & 0x3F));
            n = 4;
        }
        for (UINT k = 0; k < n; k++) {
            static const char hex[] = "0123456789ABCDEF";
            xb_putc(b, '%');
            xb_putc(b, (WCHAR)hex[bytes[k] >> 4]);
            xb_putc(b, (WCHAR)hex[bytes[k] & 15]);
        }
    }
}

static void put_attr_value(Ser *w, const WCHAR *s, UINT length)
{
    XBuf *b = w->out;
    for (UINT i = 0; i < length; i++) {
        WCHAR c = s[i];
        switch (c) {
        case '&':
            xb_puta(b, "&amp;");
            break;
        case '<':
            xb_puta(b, w->method == 2 ? "<" : "&lt;");
            break;
        case '>':
            xb_puta(b, w->method == 2 ? ">" : "&gt;");
            break;
        case '"':
            xb_puta(b, "&quot;");
            break;
        case '\r':
            if (i + 1 < length && s[i + 1] == '\n')
                i++;
            xb_puta(b, w->method == 2 ? "\r\n" : "&#xA;");
            break;
        case '\n':
            xb_puta(b, w->method == 2 ? "\r\n" : "&#xA;");
            break;
        default:
            if (!put_wide(w, s, length, &i))
                xb_putc(b, c);
        }
    }
}

static const WCHAR *bound_uri(Ser *w, const WCHAR *prefix, UINT plen, UINT from)
{
    for (UINT i = w->nbinds; i-- > from;)
        if (xd_wlen(w->binds[i].prefix) == plen && !memcmp(w->binds[i].prefix, prefix, (SIZE_T)plen * sizeof(WCHAR)))
            return w->binds[i].uri;
    return nullptr;
}

static void push_bind(Ser *w, const WCHAR *prefix, const WCHAR *uri)
{
    if (w->nbinds == w->capbinds) {
        UINT cap = w->capbinds ? w->capbinds * 2 : 32;
        void *p = xd_realloc(w->binds, cap * sizeof(w->binds[0]));
        if (!p)
            return;
        w->binds = p;
        w->capbinds = cap;
    }
    w->binds[w->nbinds].prefix = prefix;
    w->binds[w->nbinds].uri = uri;
    w->nbinds++;
}

static BOOL in_cdata_list(Ser *w, XNode *n)
{
    const XsOutput *o = w->cfg;
    for (UINT i = 0; i < o->ncdata; i++) {
        const XsName *nm = &o->cdata_names[i];
        const WCHAR *uri = n->uri ? n->uri : L"";
        if (xd_local_length(n) == nm->llen && !memcmp(xd_local(n), nm->local, (SIZE_T)nm->llen * sizeof(WCHAR)) &&
            !wcscmp(uri, nm->uri ? nm->uri : L""))
            return TRUE;
    }
    return FALSE;
}

static void put_cdata(Ser *w, const WCHAR *s, UINT length)
{
    XBuf *b = w->out;
    xb_puta(b, "<![CDATA[");
    for (UINT i = 0; i < length; i++) {
        if (i + 2 < length + 0 && s[i] == ']' && s[i + 1] == ']' && s[i + 2] == '>') {
            /* the section ends before the angle bracket and a new one starts with it; msxml3 doubles the brackets. */
            xb_puta(b, w->v6 ? "]]" : "]]]]");
            xb_puta(b, "]]><![CDATA[>");
            i += 2;
            continue;
        }
        xb_putc(b, s[i]);
    }
    xb_puta(b, "]]>");
}

static void write_node(Ser *w, XNode *n, int level);

static BOOL has_explicit(XNode *n)
{
    return (n->flags & XF_EXPLICIT) != 0;
}

static void write_children(Ser *w, XNode *parent, int level)
{
    for (XNode *c = parent->first; c; c = c->next) {
        write_node(w, c, level);
        if (w->indent && child_breaks(c))
            w->pending = TRUE;
    }
}

static void write_element(Ser *w, XNode *n, int level)
{
    XBuf *b = w->out;
    flush_pending(w);
    UINT base = w->nbinds;
    const WCHAR *q = n->qname;
    UINT ql = xd_atom_len(q);
    BOOL html = w->method == 2;
    xb_putc(b, '<');
    xb_put(b, q, ql);
    /* the attributes, then the namespace declarations. */
    for (XNode *a = n->attrs; a; a = a->next) {
        if (xd_is_xmlns_attr(a))
            continue;
        const WCHAR *aq = a->qname;
        UINT al = xd_atom_len(aq);
        xb_putc(b, ' ');
        xb_put(b, aq, al);
        if (html && a->length == al - 0 && a->length && in_list(g_bool_attrs, aq, al) && ieq(a->value, a->length, "") == FALSE) {
            /* a boolean attribute written with its own name is minimized. */
            BOOL same = a->length == xd_local_length(a) && !xd_wcsicmp(a->value, xd_local(a));
            if (same)
                continue;
        }
        xb_puta(b, "=\"");
        if (html && is_uri_attr(q, ql, aq, al))
            put_uri(w, a->value ? a->value : L"", a->length);
        else
            put_attr_value(w, a->value ? a->value : L"", a->length);
        xb_putc(b, '"');
    }
    /* declarations: those the tree holds, then what the names need. */
    for (XNode *a = n->attrs; a; a = a->next) {
        if (!xd_is_xmlns_attr(a))
            continue;
        const WCHAR *aq = a->qname;
        UINT al = xd_atom_len(aq);
        const WCHAR *prefix = al > 6 ? aq + 6 : L"";
        UINT plen = al > 6 ? al - 6 : 0;
        const WCHAR *cur = bound_uri(w, prefix, plen, 0);
        const WCHAR *val = a->value ? a->value : L"";
        if (cur && !wcscmp(cur, val))
            continue;
        if (!cur && !*val && !plen)
            continue;
        xb_putc(b, ' ');
        xb_put(b, aq, al);
        xb_puta(b, "=\"");
        put_attr_value(w, val, a->length);
        xb_putc(b, '"');
        push_bind(w, xd_intern(n->doc, prefix, plen), xd_intern(n->doc, val, a->length));
    }
    {
        const WCHAR *prefix = n->local ? q : L"";
        UINT plen = n->local ? (UINT)(n->local - 1) : 0;
        const WCHAR *uri = n->uri ? n->uri : L"";
        const WCHAR *cur = bound_uri(w, prefix, plen, 0);
        if ((!cur && *uri) || (cur && wcscmp(cur, uri) && (*uri || !plen))) {
            if (!(html && !*uri)) {
                xb_puta(b, plen ? " xmlns:" : " xmlns");
                if (plen)
                    xb_put(b, prefix, plen);
                xb_puta(b, "=\"");
                put_attr_value(w, uri, xd_wlen(uri));
                xb_putc(b, '"');
                push_bind(w, xd_intern(n->doc, prefix, plen), xd_intern(n->doc, uri, xd_wlen(uri)));
            }
        }
    }
    for (XNode *a = n->attrs; a; a = a->next) {
        if (xd_is_xmlns_attr(a) || !a->local || !a->uri)
            continue;
        const WCHAR *prefix = a->qname;
        UINT plen = (UINT)(a->local - 1);
        if (plen == 3 && !memcmp(prefix, L"xml", 6))
            continue;
        const WCHAR *cur = bound_uri(w, prefix, plen, 0);
        if (cur && !wcscmp(cur, a->uri))
            continue;
        xb_puta(b, " xmlns:");
        xb_put(b, prefix, plen);
        xb_puta(b, "=\"");
        put_attr_value(w, a->uri, xd_wlen(a->uri));
        xb_putc(b, '"');
        push_bind(w, xd_intern(n->doc, prefix, plen), xd_intern(n->doc, a->uri, xd_wlen(a->uri)));
    }
    if (html) {
        /* an element of a namespace is written the way xml writes it. */
        if (((n->uri && *n->uri) || n->local) && !n->first) {
            xb_puta(b, "/>");
            w->nbinds = base;
            return;
        }
        xb_putc(b, '>');
        UINT ll = xd_local_length(n);
        if (!n->uri && in_list(g_void_elements, xd_local(n), ll)) {
            write_children(w, n, level + 1);
            w->nbinds = base;
            return;
        }
        if (w->indent && (n->flags & XF_WS_INSIDE) && !(n->first && n->first->type == XN_TEXT))
            w->pending = TRUE;
        if (!n->uri && ieq(xd_local(n), ll, "head") && w->cfg) {
            w->pending = FALSE;
            const WCHAR *enc = w->encoding ? w->encoding : L"UTF-16";
            xb_puta(b, "\r\n<META http-equiv=\"Content-Type\" content=\"text/html");
            if (!w->v6 || w->encoding) {
                xb_puta(b, "; charset=");
                xb_put(b, enc, xd_wlen(enc));
            }
            xb_puta(b, "\">\r\n");
        }
        BOOL raw = !n->uri && (ieq(xd_local(n), ll, "script") || ieq(xd_local(n), ll, "style"));
        for (XNode *c = n->first; c; c = c->next) {
            if (raw && (c->type == XN_TEXT || c->type == XN_CDATA)) {
                for (UINT i = 0; i < c->length; i++)
                    xb_putc(b, c->value[i]);
            } else {
                write_node(w, c, level + 1);
            }
            if (w->indent && child_breaks(c))
                w->pending = TRUE;
        }
        flush_pending(w);
        xb_puta(b, "</");
        xb_put(b, q, ql);
        xb_putc(b, '>');
        w->nbinds = base;
        return;
    }
    if (!n->first && !has_explicit(n)) {
        xb_puta(b, " />");
        w->nbinds = base;
        return;
    }
    xb_putc(b, '>');
    if (w->indent && (n->flags & XF_WS_INSIDE) && !(n->first && n->first->type == XN_TEXT))
        w->pending = TRUE;
    BOOL cdata = in_cdata_list(w, n);
    for (XNode *c = n->first; c; c = c->next) {
        if (cdata && (c->type == XN_TEXT || c->type == XN_CDATA) && !(c->flags & XF_EXPLICIT))
            put_cdata(w, c->value ? c->value : L"", c->length);
        else
            write_node(w, c, level + 1);
        if (w->indent && child_breaks(c))
            w->pending = TRUE;
    }
    flush_pending(w);
    xb_puta(b, "</");
    xb_put(b, q, ql);
    xb_putc(b, '>');
    w->nbinds = base;
}

static void write_node(Ser *w, XNode *n, int level)
{
    XBuf *b = w->out;
    switch (n->type) {
    case XN_ELEMENT:
        write_element(w, n, level);
        break;
    case XN_TEXT:
    case XN_CDATA:
        flush_pending(w);
        put_text(w, n->value ? n->value : L"", n->length, TRUE, (n->flags & XF_DOE) != 0);
        break;
    case XN_COMMENT:
        flush_pending(w);
        xb_puta(b, "<!--");
        xb_put(b, n->value ? n->value : L"", n->length);
        xb_puta(b, "-->");
        break;
    case XN_PI:
        flush_pending(w);
        xb_puta(b, "<?");
        xb_put(b, n->qname, xd_atom_len(n->qname));
        if (n->length) {
            xb_putc(b, ' ');
            xb_put(b, n->value, n->length);
        }
        xb_puta(b, w->method == 2 ? ">" : "?>");
        break;
    case XN_DOCUMENT:
    case XN_FRAGMENT:
        write_children(w, n, level);
        break;
    default:
        break;
    }
}

/* the text of a result tree for the text method: every text node, escapes off. */
static void write_text_only(XBuf *out, XNode *n)
{
    for (XNode *c = n->first; c; c = c->next) {
        if (c->type == XN_TEXT || c->type == XN_CDATA) {
            for (UINT i = 0; i < c->length; i++) {
                WCHAR ch = c->value[i];
                if (ch == '\r') {
                    if (i + 1 < c->length && c->value[i + 1] == '\n')
                        i++;
                    xb_puta(out, "\r\n");
                } else if (ch == '\n') {
                    xb_puta(out, "\r\n");
                } else {
                    xb_putc(out, ch);
                }
            }
        } else if (c->type == XN_ELEMENT) {
            write_text_only(out, c);
        }
    }
}

const WCHAR *xs_output_encoding(const XsOutput *out)
{
    return out && out->encoding ? out->encoding : L"UTF-16";
}

int xs_output_method(const XsOutput *out, XDoc *result)
{
    if (out && out->method)
        return out->method;
    for (XNode *c = result->node.first; c; c = c->next) {
        if (c->type == XN_ELEMENT) {
            if (!c->uri && ieq(xd_local(c), xd_local_length(c), "html") && !c->local)
                return 2;
            return 1;
        }
        if ((c->type == XN_TEXT || c->type == XN_CDATA)) {
            BOOL space = TRUE;
            for (UINT i = 0; i < c->length; i++)
                if (!xd_is_space(c->value[i]))
                    space = FALSE;
            if (!space)
                return 1;
        }
    }
    return 1;
}

void xs_serialize(const XsOutput *out, XDoc *result, BOOL v6, XBuf *text, const WCHAR *encoding)
{
    int method = xs_output_method(out, result);
    if (method == 3) {
        write_text_only(text, &result->node);
        return;
    }
    Ser w = {};
    w.out = text;
    w.method = method;
    w.v6 = v6;
    w.cfg = out;
    w.encoding = encoding;
    w.max = 0x10FFFF;
    if (encoding) {
        UINT cp = xd_code_page(encoding);
        if (cp == 20127)
            w.max = 0x7F;
        else if (cp == 28591)
            w.max = 0xFF;
        else if (cp && cp != 65001 && cp != 1200 && cp != 1201)
            w.max = 0xFF;
    }
    if (method == 1) {
        if (!(out && out->omit_decl == 1)) {
            xb_puta(text, "<?xml version=\"1.0\"");
            if (encoding || !v6) {
                xb_puta(text, " encoding=\"");
                const WCHAR *e = encoding ? encoding : L"UTF-16";
                xb_put(text, e, xd_wlen(e));
                xb_putc(text, '"');
            }
            if (out && out->standalone) {
                xb_puta(text, " standalone=\"");
                xb_put(text, out->standalone, xd_wlen(out->standalone));
                xb_putc(text, '"');
            }
            xb_puta(text, "?>");
            if (out && out->indent == 1)
                xb_puta(text, "\r\n");
        }
    }
    XNode *root = nullptr;
    for (XNode *c = result->node.first; c; c = c->next)
        if (c->type == XN_ELEMENT) {
            root = c;
            break;
        }
    if (out && (out->doctype_system || out->doctype_public) && root) {
        xb_puta(text, "<!DOCTYPE ");
        if (method == 2)
            xb_puta(text, "html");
        else
            xb_put(text, root->qname, xd_atom_len(root->qname));
        if (out->doctype_public) {
            xb_puta(text, " PUBLIC \"");
            xb_put(text, out->doctype_public, xd_wlen(out->doctype_public));
            xb_putc(text, '"');
            if (out->doctype_system) {
                xb_puta(text, " \"");
                xb_put(text, out->doctype_system, xd_wlen(out->doctype_system));
                xb_putc(text, '"');
            }
        } else {
            xb_puta(text, " SYSTEM \"");
            xb_put(text, out->doctype_system, xd_wlen(out->doctype_system));
            xb_putc(text, '"');
        }
        xb_putc(text, '>');
    }
    w.indent = out && out->indent == 1;
    write_node(&w, &result->node, 0);
    flush_pending(&w);
    xd_free(w.binds);
}
