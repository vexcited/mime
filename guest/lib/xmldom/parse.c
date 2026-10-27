/* the xml parser: xml 1.0 with namespaces, reporting errors with the dom's codes and messages. */
#include <xmldom/xmldom.h>

typedef struct Frame {
    XNode *node;
    UINT ns_base;
    BOOL preserve;
    const WCHAR *name;
    UINT nlen;
} Frame;

typedef struct RawAttr {
    const WCHAR *name;
    UINT nlen;
    UINT colon; /* offset of the colon in the name, 0 for none. */
    UINT voff;
    UINT vlen;
    const WCHAR *at; /* where the attribute starts, for errors. */
} RawAttr;

typedef struct Bind {
    const WCHAR *prefix;
    const WCHAR *uri;
} Bind;

typedef struct Parser {
    XDoc *doc;
    const WCHAR *s;
    const WCHAR *p;
    const WCHAR *e;
    UINT flags;
    BOOL failed;
    Frame *frames;
    UINT depth;
    UINT frame_cap;
    Bind *binds;
    UINT bind_count;
    UINT bind_cap;
    RawAttr *attrs;
    UINT attr_cap;
    XBuf text;
    XBuf vals;
    const WCHAR *a_empty;
    const WCHAR *a_xml;
    const WCHAR *a_xmlns;
    const WCHAR *uri_xml;
    const WCHAR *uri_xmlns;
    BOOL strip; /* whitespace-only text is dropped. */
    BOOL text_ref; /* the pending text holds a reference, which keeps it as text. */
    UINT64 expanded; /* characters produced by entity expansion, which is capped. */
} Parser;

#define XML_URI L"http://www.w3.org/XML/1998/namespace"
#define XMLNS_URI L"http://www.w3.org/2000/xmlns/"

/* ------------------------------------------------------------------ errors */

void xd_line_info(const WCHAR *text, UINT length, UINT offset, LONG *line, LONG *linepos)
{
    LONG n = 1;
    UINT start = 0;
    if (offset > length)
        offset = length;
    for (UINT i = 0; i < offset; i++) {
        if (text[i] == '\n') {
            n++;
            start = i + 1;
        }
    }
    *line = n;
    *linepos = (LONG)(offset - start) + 1;
}

static BOOL fail_at(Parser *ps, UINT code, const WCHAR *at, const WCHAR *p1, const WCHAR *p2)
{
    if (ps->failed)
        return FALSE;
    ps->failed = TRUE;
    UINT length = (UINT)(ps->e - ps->s);
    UINT offset = (UINT)(at - ps->s);
    if (at < ps->s || at > ps->e)
        offset = length;
    LONG line, linepos;
    xd_line_info(ps->s, length, offset, &line, &linepos);
    UINT start = offset - (UINT)(linepos - 1), end = offset;
    while (end < length && ps->s[end] != '\n' && ps->s[end] != '\r')
        end++;
    xd_err_set(ps->doc, code, line, linepos, (LONG)offset, ps->s + start, end - start, p1, p2);
    return FALSE;
}

#define FAIL(code, at) fail_at(ps, (code), (at), nullptr, nullptr)

/* ------------------------------------------------------------------ small helpers */

static BOOL grow_frames(Parser *ps)
{
    if (ps->depth < ps->frame_cap)
        return TRUE;
    UINT cap = ps->frame_cap ? ps->frame_cap * 2 : 32;
    Frame *f = xd_realloc(ps->frames, (SIZE_T)cap * sizeof(Frame));
    if (!f)
        return FALSE;
    ps->frames = f;
    ps->frame_cap = cap;
    return TRUE;
}

static BOOL push_bind(Parser *ps, const WCHAR *prefix, const WCHAR *uri)
{
    if (ps->bind_count == ps->bind_cap) {
        UINT cap = ps->bind_cap ? ps->bind_cap * 2 : 32;
        Bind *b = xd_realloc(ps->binds, (SIZE_T)cap * sizeof(Bind));
        if (!b)
            return FALSE;
        ps->binds = b;
        ps->bind_cap = cap;
    }
    ps->binds[ps->bind_count].prefix = prefix;
    ps->binds[ps->bind_count].uri = uri;
    ps->bind_count++;
    return TRUE;
}

/* found says whether the prefix is declared at all. */
static const WCHAR *lookup_bind(Parser *ps, const WCHAR *prefix, BOOL *found)
{
    for (UINT i = ps->bind_count; i-- > 0;) {
        if (ps->binds[i].prefix == prefix) {
            *found = TRUE;
            return ps->binds[i].uri;
        }
    }
    *found = FALSE;
    return nullptr;
}

static BOOL starts_with(const WCHAR *p, const WCHAR *e, const WCHAR *lit, UINT n)
{
    return (UINT)(e - p) >= n && !memcmp(p, lit, (SIZE_T)n * sizeof(WCHAR));
}

#define STARTS(lit) starts_with(ps->p, ps->e, lit, (sizeof(lit) - sizeof(WCHAR)) / sizeof(WCHAR))

static const WCHAR *skip_space(const WCHAR *p, const WCHAR *e)
{
    while (p < e && xd_is_space(*p))
        p++;
    return p;
}

static UINT code_point_at(const WCHAR *p, const WCHAR *e, UINT *units)
{
    UINT c = *p;
    *units = 1;
    if (c >= 0xD800 && c <= 0xDBFF && p + 1 < e && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
        *units = 2;
        return 0x10000 + ((c - 0xD800) << 10) + (p[1] - 0xDC00u);
    }
    return c;
}

static void put_code_point(XBuf *b, UINT c)
{
    if (c >= 0x10000) {
        c -= 0x10000;
        xb_putc(b, (WCHAR)(0xD800 + (c >> 10)));
        xb_putc(b, (WCHAR)(0xDC00 + (c & 0x3FF)));
    } else {
        xb_putc(b, (WCHAR)c);
    }
}

/* a name with at most one colon. errors match the dom's: bad first character, bad later one, extra colon. */
static BOOL scan_qname(Parser *ps, const WCHAR **name, UINT *length, UINT *colon)
{
    const WCHAR *p = ps->p;
    if (p >= ps->e || !xd_is_name_start(*p) || *p == ':')
        return FAIL(XE_NAME_START, p);
    const WCHAR *start = p;
    UINT colons = 0, first = 0;
    while (p < ps->e) {
        UINT c = *p;
        if (c == ':') {
            if (++colons > 1)
                return FAIL(XE_MULTIPLE_COLONS, p);
            first = (UINT)(p - start);
        } else if (!xd_is_name_char(c)) {
            break;
        }
        p++;
    }
    if (colons && first + 1 == (UINT)(p - start))
        return FAIL(XE_NAME_CHAR, p - 1);
    if (colons && !xd_is_name_start(start[first + 1]))
        return FAIL(XE_NAME_START, start + first + 1);
    *name = start;
    *length = (UINT)(p - start);
    *colon = colons ? first : 0;
    ps->p = p;
    return TRUE;
}

/* ------------------------------------------------------------------ references */

static BOOL entity_value(Parser *ps, const WCHAR *name, UINT length, XEntity **out)
{
    XDoc *doc = ps->doc;
    const WCHAR *atom = xd_find_atom(doc, name, length);
    if (atom) {
        for (UINT i = 0; i < doc->entity_count; i++) {
            if (doc->entities[i].name == atom) {
                *out = &doc->entities[i];
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOL append_entity(Parser *ps, XBuf *out, const WCHAR *value, UINT length, UINT depth, const WCHAR *at);

/* ps->p is on the '&'. attribute values cannot reference an external entity. */
static BOOL parse_reference(Parser *ps, XBuf *out, BOOL attribute)
{
    const WCHAR *at = ps->p;
    const WCHAR *p = at + 1;
    const WCHAR *e = ps->e;
    if (p >= e)
        return FAIL(XE_EOF, p);
    if (*p == '#') {
        p++;
        UINT value = 0;
        BOOL hex = FALSE;
        if (p < e && *p == 'x') {
            hex = TRUE;
            p++;
        }
        const WCHAR *digits = p;
        for (; p < e; p++) {
            UINT c = *p, d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (hex && c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            if (value < 0x110000)
                value = value * (hex ? 16 : 10) + d;
        }
        if (p == digits)
            return FAIL(hex ? XE_HEX_DIGIT : XE_DECIMAL_DIGIT, p);
        if (p >= e || *p != ';')
            return FAIL(p >= e ? XE_EOF : XE_NEED_SEMICOLON, p);
        if (!xd_is_xml_char(value) || value >= 0x110000)
            return FAIL(XE_BAD_UNICODE, at);
        put_code_point(out, value);
        ps->p = p + 1;
        return TRUE;
    }
    if (!xd_is_name_start(*p) || *p == ':')
        return FAIL(XE_ENTITY_CHAR, p);
    const WCHAR *name = p;
    while (p < e && xd_is_name_char(*p) && *p != ':')
        p++;
    UINT length = (UINT)(p - name);
    if (p >= e)
        return FAIL(XE_EOF, p);
    if (*p != ';')
        return FAIL(xd_is_name_char(*p) ? XE_ENTITY_CHAR : XE_NEED_SEMICOLON, p);
    WCHAR c = 0;
    if (length == 2 && name[0] == 'l' && name[1] == 't')
        c = '<';
    else if (length == 2 && name[0] == 'g' && name[1] == 't')
        c = '>';
    else if (length == 3 && name[0] == 'a' && name[1] == 'm' && name[2] == 'p')
        c = '&';
    else if (length == 4 && name[0] == 'q' && name[1] == 'u' && name[2] == 'o' && name[3] == 't')
        c = '"';
    else if (length == 4 && name[0] == 'a' && name[1] == 'p' && name[2] == 'o' && name[3] == 's')
        c = '\'';
    if (c) {
        xb_putc(out, c);
        ps->p = p + 1;
        return TRUE;
    }
    XEntity *entity;
    if (!entity_value(ps, name, length, &entity)) {
        WCHAR *copy = xd_wcsdup(name, length);
        BOOL r = fail_at(ps, XE_UNDEFINED_ENTITY, at, copy, nullptr);
        xd_free(copy);
        return r;
    }
    UINT before = out->length;
    if (!append_entity(ps, out, entity->value, entity->length, 0, at))
        return FALSE;
    /* entity text in an attribute value has its white space normalized like the rest. */
    if (attribute)
        for (UINT i = before; i < out->length; i++)
            if (out->s[i] == '\t' || out->s[i] == '\n' || out->s[i] == '\r')
                out->s[i] = ' ';
    ps->p = p + 1;
    return TRUE;
}

/* replacement text may itself refer to entities. */
static BOOL append_entity(Parser *ps, XBuf *out, const WCHAR *value, UINT length, UINT depth, const WCHAR *at)
{
    if (depth > 16 || (ps->expanded += length) > 10000000)
        return FAIL(XE_ENTITY_LOOP, at);
    for (UINT i = 0; i < length; i++) {
        if (value[i] != '&') {
            xb_putc(out, value[i]);
            continue;
        }
        UINT j = i + 1;
        while (j < length && value[j] != ';')
            j++;
        if (j >= length) {
            xb_putc(out, '&');
            continue;
        }
        const WCHAR *name = value + i + 1;
        UINT n = j - i - 1;
        WCHAR c = 0;
        if (n == 2 && name[0] == 'l' && name[1] == 't')
            c = '<';
        else if (n == 2 && name[0] == 'g' && name[1] == 't')
            c = '>';
        else if (n == 3 && name[0] == 'a' && name[1] == 'm' && name[2] == 'p')
            c = '&';
        else if (n == 4 && name[0] == 'q' && name[1] == 'u' && name[2] == 'o' && name[3] == 't')
            c = '"';
        else if (n == 4 && name[0] == 'a' && name[1] == 'p' && name[2] == 'o' && name[3] == 's')
            c = '\'';
        if (c) {
            xb_putc(out, c);
        } else {
            XEntity *entity;
            if (!entity_value(ps, name, n, &entity)) {
                WCHAR *copy = xd_wcsdup(name, n);
                BOOL r = fail_at(ps, XE_UNDEFINED_ENTITY, at, copy, nullptr);
                xd_free(copy);
                return r;
            }
            if (!append_entity(ps, out, entity->value, entity->length, depth + 1, at))
                return FALSE;
        }
        i = j;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the pieces of the prolog and of content */

static XNode *new_node(Parser *ps, UINT type, const WCHAR *name, UINT nlen, const WCHAR *uri)
{
    XNode *n = xd_node_new(ps->doc, type, name, nlen, uri);
    if (!n)
        fail_at(ps, E_OUTOFMEMORY, ps->p, nullptr, nullptr);
    return n;
}

static XNode *current_parent(Parser *ps)
{
    return ps->depth ? ps->frames[ps->depth - 1].node : &ps->doc->node;
}

static void add_node(Parser *ps, XNode *n)
{
    xd_link(current_parent(ps), n, nullptr);
}

/* white space dropped after a node, or before the first child, is remembered for the writer. */
static void note_dropped_space(XNode *parent)
{
    if (parent->last)
        parent->last->flags |= XF_WS_AFTER;
    else if (parent->type == XN_ELEMENT)
        parent->flags |= XF_WS_INSIDE;
}

static void flush_text(Parser *ps)
{
    XBuf *t = &ps->text;
    if (!t->length)
        return;
    BOOL keep = TRUE;
    if (ps->strip && !ps->text_ref && !(ps->depth && ps->frames[ps->depth - 1].preserve)) {
        keep = FALSE;
        for (UINT i = 0; i < t->length; i++) {
            if (!xd_is_space(t->s[i])) {
                keep = TRUE;
                break;
            }
        }
    }
    if (keep) {
        XNode *n = new_node(ps, XN_TEXT, nullptr, 0, nullptr);
        if (n) {
            n->value = xd_wcsdup(t->s, t->length);
            n->length = t->length;
            add_node(ps, n);
        }
    } else {
        note_dropped_space(current_parent(ps));
    }
    ps->text_ref = FALSE;
    t->length = 0;
    if (t->s)
        t->s[0] = 0;
}

static BOOL parse_comment(Parser *ps)
{
    /* ps->p is on "<!--". */
    const WCHAR *start = ps->p + 4;
    const WCHAR *p = start;
    while (p + 1 < ps->e && !(p[0] == '-' && p[1] == '-'))
        p++;
    if (p + 1 >= ps->e)
        return FAIL(XE_COMMENT_NOT_CLOSED, ps->e);
    if (p + 2 >= ps->e || p[2] != '>')
        return FAIL(XE_BAD_COMMENT, p);
    for (const WCHAR *q = start; q < p; q++)
        if (!xd_is_xml_char(*q) && !(*q >= 0xD800 && *q <= 0xDFFF))
            return FAIL(XE_TEXT_CHAR, q);
    XNode *n = new_node(ps, XN_COMMENT, nullptr, 0, nullptr);
    if (!n)
        return FALSE;
    n->value = xd_wcsdup(start, (UINT)(p - start));
    n->length = (UINT)(p - start);
    add_node(ps, n);
    ps->p = p + 3;
    return TRUE;
}

static BOOL parse_cdata(Parser *ps)
{
    /* ps->p is on "<![CDATA[". */
    const WCHAR *start = ps->p + 9;
    const WCHAR *p = start;
    while (p + 2 < ps->e && !(p[0] == ']' && p[1] == ']' && p[2] == '>')) {
        if (!xd_is_xml_char(*p) && !(*p >= 0xD800 && *p <= 0xDFFF))
            return FAIL(XE_TEXT_CHAR, p);
        p++;
    }
    if (p + 2 >= ps->e)
        return FAIL(XE_CDATA_NOT_CLOSED, ps->e);
    flush_text(ps);
    XNode *n = new_node(ps, XN_CDATA, nullptr, 0, nullptr);
    if (!n)
        return FALSE;
    /* line ends are normalized in a CDATA section too. */
    XBuf b = {};
    for (const WCHAR *q = start; q < p; q++) {
        if (*q == '\r') {
            xb_putc(&b, '\n');
            if (q + 1 < p && q[1] == '\n')
                q++;
        } else {
            xb_putc(&b, *q);
        }
    }
    n->value = b.s ? b.s : xd_wcsdup(L"", 0);
    n->length = b.length;
    add_node(ps, n);
    ps->p = p + 3;
    return TRUE;
}

static BOOL parse_pi(Parser *ps)
{
    /* ps->p is on "<?". */
    const WCHAR *at = ps->p;
    ps->p += 2;
    const WCHAR *name;
    UINT nlen, colon;
    if (ps->p >= ps->e)
        return FAIL(XE_PI_NOT_CLOSED, ps->e);
    if (!xd_is_name_start(*ps->p))
        return FAIL(XE_NAME_START, ps->p);
    const WCHAR *p = ps->p;
    while (p < ps->e && xd_is_name_char(*p))
        p++;
    name = ps->p;
    nlen = (UINT)(p - name);
    colon = 0;
    (void)colon;
    if (nlen == 3 && (name[0] | 0x20) == 'x' && (name[1] | 0x20) == 'm' && (name[2] | 0x20) == 'l') {
        if (!(name[0] == 'x' && name[1] == 'm' && name[2] == 'l'))
            return FAIL(XE_BAD_XML_DECL, name);
        return FAIL(XE_BAD_XML_DECL, at);
    }
    const WCHAR *data;
    const WCHAR *end;
    if (p + 1 < ps->e && p[0] == '?' && p[1] == '>') {
        data = end = p;
    } else {
        if (p >= ps->e)
            return FAIL(XE_PI_NOT_CLOSED, ps->e);
        if (!xd_is_space(*p))
            return FAIL(XE_PI_SYNTAX, p);
        data = skip_space(p, ps->e);
        end = data;
        while (end + 1 < ps->e && !(end[0] == '?' && end[1] == '>'))
            end++;
        if (end + 1 >= ps->e)
            return FAIL(XE_PI_NOT_CLOSED, ps->e);
    }
    for (const WCHAR *q = data; q < end; q++)
        if (!xd_is_xml_char(*q) && !(*q >= 0xD800 && *q <= 0xDFFF))
            return FAIL(XE_TEXT_CHAR, q);
    flush_text(ps);
    XNode *n = new_node(ps, XN_PI, name, nlen, nullptr);
    if (!n)
        return FALSE;
    n->value = xd_wcsdup(data, (UINT)(end - data));
    n->length = (UINT)(end - data);
    add_node(ps, n);
    ps->p = end + 2;
    return TRUE;
}

/* ------------------------------------------------------------------ the xml declaration */

static XNode *decl_attr(Parser *ps, XNode *decl, const char *name, const WCHAR *value, UINT length)
{
    WCHAR wide[16];
    UINT n = 0;
    while (name[n]) {
        wide[n] = (UCHAR)name[n];
        n++;
    }
    XNode *a = new_node(ps, XN_ATTRIBUTE, wide, n, nullptr);
    if (!a)
        return nullptr;
    a->value = xd_wcsdup(value, length);
    a->length = length;
    xd_attr_link(decl, a);
    return a;
}

static BOOL parse_xml_decl(Parser *ps)
{
    /* ps->p is on "<?xml" followed by white space. */
    const WCHAR *at = ps->p;
    const WCHAR *p = ps->p + 5;
    const WCHAR *e = ps->e;
    XNode *decl = new_node(ps, XN_PI, L"xml", 3, nullptr);
    if (!decl)
        return FALSE;
    decl->flags |= XF_XMLDECL;
    UINT allowed = 0; /* the order must be version, encoding, standalone. */
    BOOL seen = FALSE;
    for (;;) {
        p = skip_space(p, e);
        if (p + 1 < e && p[0] == '?' && p[1] == '>')
            break;
        if (p >= e) {
            xd_free_if_orphan(decl);
            return FAIL(XE_PI_NOT_CLOSED, e);
        }
        const WCHAR *name = p;
        while (p < e && xd_is_name_char(*p))
            p++;
        UINT nlen = (UINT)(p - name);
        if (!nlen) {
            xd_free_if_orphan(decl);
            return FAIL(XE_XML_DECL_SYNTAX, p);
        }
        p = skip_space(p, e);
        if (p >= e || *p != '=') {
            xd_free_if_orphan(decl);
            return FAIL(XE_MISSING_EQUALS, p);
        }
        p = skip_space(p + 1, e);
        if (p >= e || (*p != '"' && *p != '\'')) {
            xd_free_if_orphan(decl);
            return FAIL(XE_NO_QUOTE, p);
        }
        WCHAR quote = *p++;
        const WCHAR *value = p;
        while (p < e && *p != quote)
            p++;
        if (p >= e) {
            xd_free_if_orphan(decl);
            return FAIL(XE_STRING_NOT_CLOSED, e);
        }
        UINT vlen = (UINT)(p - value);
        p++;
        const char *which = nullptr;
        UINT index = 3;
        if (nlen == 7 && !memcmp(name, L"version", 14)) {
            which = "version";
            index = 0;
        } else if (nlen == 8 && !memcmp(name, L"encoding", 16)) {
            which = "encoding";
            index = 1;
        } else if (nlen == 10 && !memcmp(name, L"standalone", 20)) {
            which = "standalone";
            index = 2;
        }
        if (!seen && index != 0) {
            xd_free_if_orphan(decl);
            return FAIL(XE_VERSION_REQUIRED, name);
        }
        if (!which || index < allowed) {
            xd_free_if_orphan(decl);
            return FAIL(XE_BAD_XML_DECL, name);
        }
        seen = TRUE;
        allowed = index + 1;
        if (index == 0) {
            if (vlen != 3 || value[0] != '1' || value[1] != '.' || value[2] < '0' || value[2] > '9') {
                xd_free_if_orphan(decl);
                return FAIL(XE_BAD_VERSION, value);
            }
        } else if (index == 1) {
            if (!vlen || !((value[0] | 0x20) >= 'a' && (value[0] | 0x20) <= 'z')) {
                xd_free_if_orphan(decl);
                return FAIL(XE_XML_DECL_SYNTAX, value);
            }
        } else {
            BOOL yes = vlen == 3 && !memcmp(value, L"yes", 6), no = vlen == 2 && !memcmp(value, L"no", 4);
            if (!yes && !no) {
                xd_free_if_orphan(decl);
                return FAIL(XE_STANDALONE, value);
            }
        }
        if (!decl_attr(ps, decl, which, value, vlen)) {
            xd_free_if_orphan(decl);
            return FALSE;
        }
        if (p < e && !xd_is_space(*p) && !(p + 1 < e && p[0] == '?' && p[1] == '>')) {
            xd_free_if_orphan(decl);
            return FAIL(XE_NEED_SPACE, p);
        }
    }
    if (!seen) {
        xd_free_if_orphan(decl);
        return FAIL(XE_VERSION_REQUIRED, at + 5);
    }
    XNode *enc = xd_attr_find(decl, L"encoding", 8);
    if (enc && enc->value) {
        xd_free(ps->doc->encoding);
        ps->doc->encoding = xd_wcsdup(enc->value, enc->length);
    }
    xd_link(&ps->doc->node, decl, nullptr);
    ps->p = p + 2;
    return TRUE;
}

/* ------------------------------------------------------------------ the document type */

static BOOL add_entity(Parser *ps, const WCHAR *name, UINT nlen, const WCHAR *value, UINT vlen)
{
    XDoc *doc = ps->doc;
    const WCHAR *atom = xd_intern(doc, name, nlen);
    if (!atom)
        return FALSE;
    for (UINT i = 0; i < doc->entity_count; i++)
        if (doc->entities[i].name == atom)
            return TRUE; /* the first declaration wins. */
    if (doc->entity_count == doc->entity_cap) {
        UINT cap = doc->entity_cap ? doc->entity_cap * 2 : 8;
        XEntity *e = xd_realloc(doc->entities, (SIZE_T)cap * sizeof(XEntity));
        if (!e)
            return FALSE;
        doc->entities = e;
        doc->entity_cap = cap;
    }
    XBuf b = {};
    /* character references are resolved where the entity is declared. */
    for (UINT i = 0; i < vlen; i++) {
        if (value[i] == '&' && i + 2 < vlen && value[i + 1] == '#') {
            UINT j = i + 2, v = 0;
            BOOL hex = value[j] == 'x';
            if (hex)
                j++;
            while (j < vlen && value[j] != ';') {
                UINT c = value[j], d = 0;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (hex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f')
                    d = (c | 0x20) - 'a' + 10;
                else
                    break;
                v = v * (hex ? 16 : 10) + d;
                j++;
            }
            if (j < vlen && value[j] == ';' && xd_is_xml_char(v)) {
                put_code_point(&b, v);
                i = j;
                continue;
            }
        }
        xb_putc(&b, value[i]);
    }
    doc->entities[doc->entity_count].name = atom;
    doc->entities[doc->entity_count].value = b.s ? b.s : xd_wcsdup(L"", 0);
    doc->entities[doc->entity_count].length = b.length;
    doc->entity_count++;
    return TRUE;
}

/* p is on the quote. returns the position after the closing one. */
static const WCHAR *skip_quoted(const WCHAR *p, const WCHAR *e)
{
    WCHAR q = *p++;
    while (p < e && *p != q)
        p++;
    return p < e ? p + 1 : e;
}

/* quoted strings in a declaration may hold a '>'. */
static const WCHAR *skip_declaration(const WCHAR *p, const WCHAR *e)
{
    while (p < e && *p != '>') {
        if (*p == '"' || *p == '\'')
            p = skip_quoted(p, e);
        else
            p++;
    }
    return p;
}

static BOOL parse_internal_subset(Parser *ps, const WCHAR *start, const WCHAR **end_out)
{
    /* start is just past the '['. the subset ends at the matching ']' outside quotes, comments and declarations. */
    const WCHAR *p = start, *e = ps->e;
    while (p < e) {
        if (*p == ']') {
            *end_out = p;
            return TRUE;
        }
        if (xd_is_space(*p)) {
            p++;
            continue;
        }
        if (*p == '%') {
            while (p < e && *p != ';')
                p++;
            if (p < e)
                p++;
            continue;
        }
        if (starts_with(p, e, L"<!--", 4)) {
            const WCHAR *q = p + 4;
            while (q + 2 < e && !(q[0] == '-' && q[1] == '-' && q[2] == '>'))
                q++;
            if (q + 2 >= e)
                return FAIL(XE_COMMENT_NOT_CLOSED, e);
            p = q + 3;
            continue;
        }
        if (starts_with(p, e, L"<?", 2)) {
            const WCHAR *q = p + 2;
            while (q + 1 < e && !(q[0] == '?' && q[1] == '>'))
                q++;
            if (q + 1 >= e)
                return FAIL(XE_PI_NOT_CLOSED, e);
            p = q + 2;
            continue;
        }
        if (!starts_with(p, e, L"<!", 2))
            return FAIL(XE_DTD_CHAR, p);
        const WCHAR *decl = p;
        if (starts_with(p, e, L"<!ENTITY", 8) && p + 8 < e && xd_is_space(p[8])) {
            p = skip_space(p + 8, e);
            BOOL parameter = FALSE;
            if (p < e && *p == '%') {
                parameter = TRUE;
                p = skip_space(p + 1, e);
            }
            const WCHAR *name = p;
            while (p < e && xd_is_name_char(*p))
                p++;
            UINT nlen = (UINT)(p - name);
            p = skip_space(p, e);
            if (nlen && !parameter && p < e && (*p == '"' || *p == '\'')) {
                const WCHAR *after = skip_quoted(p, e);
                UINT vlen = (UINT)(after - p) >= 2 ? (UINT)(after - p) - 2 : 0;
                if (!add_entity(ps, name, nlen, p + 1, vlen))
                    return fail_at(ps, E_OUTOFMEMORY, decl, nullptr, nullptr);
            }
        }
        p = skip_declaration(p, e);
        if (p >= e)
            return FAIL(XE_DECL_NOT_CLOSED, e);
        p++;
    }
    return FAIL(XE_DECL_NOT_CLOSED, e);
}

static BOOL parse_doctype(Parser *ps)
{
    /* ps->p is on "<!DOCTYPE". */
    const WCHAR *at = ps->p;
    if (ps->doc->flags & XDF_PROHIBIT_DTD)
        return FAIL(XE_DTD_PROHIBITED, at);
    const WCHAR *p = ps->p + 9, *e = ps->e;
    if (p >= e || !xd_is_space(*p))
        return FAIL(XE_NEED_SPACE, p);
    p = skip_space(p, e);
    const WCHAR *name = p;
    while (p < e && xd_is_name_char(*p))
        p++;
    if (p == name)
        return FAIL(XE_NAME_REQUIRED, p);
    UINT nlen = (UINT)(p - name);
    const WCHAR *rest = p;
    BOOL has_subset = FALSE;
    const WCHAR *subset_end = nullptr;
    for (;;) {
        p = skip_space(p, e);
        if (p >= e)
            return FAIL(XE_DECL_NOT_CLOSED, e);
        if (*p == '>')
            break;
        if (*p == '"' || *p == '\'') {
            p = skip_quoted(p, e);
            continue;
        }
        if (*p == '[') {
            has_subset = TRUE;
            if (!parse_internal_subset(ps, p + 1, &subset_end))
                return FALSE;
            p = subset_end + 1;
            continue;
        }
        if (xd_is_name_start(*p)) {
            const WCHAR *w = p;
            while (p < e && xd_is_name_char(*p))
                p++;
            UINT wl = (UINT)(p - w);
            if (!((wl == 6 && !memcmp(w, L"SYSTEM", 12)) || (wl == 6 && !memcmp(w, L"PUBLIC", 12))))
                return FAIL(XE_EXTERNAL_ID, w);
            continue;
        }
        return FAIL(XE_DTD_CHAR, p);
    }
    (void)has_subset;
    XNode *n = new_node(ps, XN_DOCTYPE, name, nlen, nullptr);
    if (!n)
        return FALSE;
    /* the rest is kept verbatim so it can be written back. */
    UINT rlen = (UINT)(p - rest);
    n->value = xd_wcsdup(rest, rlen);
    n->length = rlen;
    add_node(ps, n);
    ps->p = p + 1;
    return TRUE;
}

/* ------------------------------------------------------------------ elements */

static BOOL parse_attr_value(Parser *ps, RawAttr *a)
{
    /* ps->p is on the opening quote. */
    WCHAR quote = *ps->p++;
    a->voff = ps->vals.length;
    while (ps->p < ps->e && *ps->p != quote) {
        WCHAR c = *ps->p;
        if (c == '<')
            return FAIL(XE_LT_IN_ATTRIBUTE, ps->p);
        if (c == '&') {
            if (!parse_reference(ps, &ps->vals, TRUE))
                return FALSE;
            continue;
        }
        if (c == '\r') {
            xb_putc(&ps->vals, ' ');
            ps->p++;
            if (ps->p < ps->e && *ps->p == '\n')
                ps->p++;
            continue;
        }
        if (c == '\n' || c == '\t') {
            xb_putc(&ps->vals, ' ');
            ps->p++;
            continue;
        }
        UINT units;
        UINT cp = code_point_at(ps->p, ps->e, &units);
        if (!xd_is_xml_char(cp))
            return FAIL(XE_TEXT_CHAR, ps->p);
        xb_put(&ps->vals, ps->p, units);
        ps->p += units;
    }
    if (ps->p >= ps->e)
        return FAIL(XE_STRING_NOT_CLOSED, ps->e);
    a->vlen = ps->vals.length - a->voff;
    ps->p++;
    return TRUE;
}

static const WCHAR *prefix_atom(Parser *ps, const WCHAR *name, UINT colon)
{
    return xd_find_atom(ps->doc, name, colon);
}

static BOOL parse_start_tag(Parser *ps)
{
    /* ps->p is on '<' and a name follows. */
    const WCHAR *tag_at = ps->p;
    ps->p++;
    const WCHAR *name;
    UINT nlen, colon;
    if (!scan_qname(ps, &name, &nlen, &colon))
        return FALSE;
    UINT count = 0;
    ps->vals.length = 0;
    BOOL empty = FALSE;
    for (;;) {
        const WCHAR *before = ps->p;
        ps->p = skip_space(ps->p, ps->e);
        if (ps->p >= ps->e)
            return FAIL(XE_ELEMENT_NOT_CLOSED, ps->e);
        if (*ps->p == '>') {
            ps->p++;
            break;
        }
        if (*ps->p == '/') {
            if (ps->p + 1 >= ps->e)
                return FAIL(XE_ELEMENT_NOT_CLOSED, ps->e);
            if (ps->p[1] != '>')
                return FAIL(XE_NEED_GT, ps->p + 1);
            ps->p += 2;
            empty = TRUE;
            break;
        }
        if (ps->p == before)
            return FAIL(XE_NEED_SPACE, ps->p);
        if (count == ps->attr_cap) {
            UINT cap = ps->attr_cap ? ps->attr_cap * 2 : 16;
            RawAttr *a = xd_realloc(ps->attrs, (SIZE_T)cap * sizeof(RawAttr));
            if (!a)
                return fail_at(ps, E_OUTOFMEMORY, ps->p, nullptr, nullptr);
            ps->attrs = a;
            ps->attr_cap = cap;
        }
        RawAttr *a = &ps->attrs[count];
        a->at = ps->p;
        if (!scan_qname(ps, &a->name, &a->nlen, &a->colon))
            return FALSE;
        ps->p = skip_space(ps->p, ps->e);
        if (ps->p >= ps->e || *ps->p != '=')
            return FAIL(ps->p >= ps->e ? XE_ELEMENT_NOT_CLOSED : XE_MISSING_EQUALS, ps->p);
        ps->p = skip_space(ps->p + 1, ps->e);
        if (ps->p >= ps->e || (*ps->p != '"' && *ps->p != '\''))
            return FAIL(ps->p >= ps->e ? XE_ELEMENT_NOT_CLOSED : XE_NO_QUOTE, ps->p);
        if (!parse_attr_value(ps, a))
            return FALSE;
        for (UINT i = 0; i < count; i++) {
            if (ps->attrs[i].nlen == a->nlen && !memcmp(ps->attrs[i].name, a->name, (SIZE_T)a->nlen * sizeof(WCHAR)))
                return FAIL(XE_DUPLICATE_ATTRIBUTE, a->at);
        }
        count++;
    }

    flush_text(ps);
    XDoc *doc = ps->doc;
    UINT ns_base = ps->bind_count;

    /* declarations first: they apply to this element's own name and attributes. */
    for (UINT i = 0; i < count; i++) {
        RawAttr *a = &ps->attrs[i];
        BOOL is_default = a->nlen == 5 && !memcmp(a->name, L"xmlns", 10);
        BOOL is_prefixed = a->nlen > 6 && a->colon == 5 && !memcmp(a->name, L"xmlns", 10);
        if (!is_default && !is_prefixed)
            continue;
        const WCHAR *value = ps->vals.s + a->voff;
        const WCHAR *prefix = ps->a_empty;
        if (is_prefixed) {
            prefix = xd_intern(doc, a->name + 6, a->nlen - 6);
            if (!prefix)
                return fail_at(ps, E_OUTOFMEMORY, a->at, nullptr, nullptr);
            if (prefix == ps->a_xmlns)
                return FAIL(XE_RESERVED_PREFIX, a->at);
            if (prefix == ps->a_xml && !(a->vlen == 36 && !memcmp(value, ps->uri_xml, 72)))
                return FAIL(XE_RESERVED_PREFIX, a->at);
            if (!a->vlen)
                return FAIL(XE_EMPTY_URI, a->at);
        }
        if (a->vlen == 29 && !memcmp(value, ps->uri_xmlns, 58))
            return FAIL(XE_RESERVED_PREFIX, a->at);
        const WCHAR *uri = a->vlen ? xd_intern(doc, value, a->vlen) : nullptr;
        if (a->vlen && !uri)
            return fail_at(ps, E_OUTOFMEMORY, a->at, nullptr, nullptr);
        if (!push_bind(ps, prefix, uri))
            return fail_at(ps, E_OUTOFMEMORY, a->at, nullptr, nullptr);
    }

    const WCHAR *uri = nullptr;
    if (colon) {
        const WCHAR *prefix = prefix_atom(ps, name, colon);
        BOOL found = FALSE;
        if (prefix == ps->a_xmlns)
            return FAIL(XE_RESERVED_PREFIX, tag_at + 1);
        if (prefix == ps->a_xml) {
            uri = ps->uri_xml;
            found = TRUE;
        } else if (prefix) {
            uri = lookup_bind(ps, prefix, &found);
        }
        if (!found || !uri) {
            WCHAR *copy = xd_wcsdup(name, colon);
            BOOL r = fail_at(ps, XE_UNDECLARED_PREFIX, tag_at + 1, copy, nullptr);
            xd_free(copy);
            return r;
        }
    } else {
        BOOL found;
        uri = lookup_bind(ps, ps->a_empty, &found);
    }

    XNode *el = new_node(ps, XN_ELEMENT, name, nlen, nullptr);
    if (!el)
        return FALSE;
    el->uri = uri;
    BOOL preserve = ps->depth ? ps->frames[ps->depth - 1].preserve : FALSE;

    for (UINT i = 0; i < count; i++) {
        RawAttr *a = &ps->attrs[i];
        const WCHAR *auri = nullptr;
        BOOL is_decl = (a->nlen == 5 && !memcmp(a->name, L"xmlns", 10)) ||
                       (a->nlen > 6 && a->colon == 5 && !memcmp(a->name, L"xmlns", 10));
        if (is_decl) {
            auri = ps->uri_xmlns;
        } else if (a->colon) {
            const WCHAR *prefix = prefix_atom(ps, a->name, a->colon);
            BOOL found = FALSE;
            if (prefix == ps->a_xml) {
                auri = ps->uri_xml;
                found = TRUE;
            } else if (prefix) {
                auri = lookup_bind(ps, prefix, &found);
            }
            if (!found || !auri) {
                WCHAR *copy = xd_wcsdup(a->name, a->colon);
                BOOL r = fail_at(ps, XE_UNDECLARED_PREFIX, a->at, copy, nullptr);
                xd_free(copy);
                xd_free_if_orphan(el);
                return r;
            }
        }
        XNode *an = new_node(ps, XN_ATTRIBUTE, a->name, a->nlen, nullptr);
        if (!an) {
            xd_free_if_orphan(el);
            return FALSE;
        }
        an->uri = auri;
        an->value = xd_wcsdup(ps->vals.s + a->voff, a->vlen);
        an->length = a->vlen;
        /* attributes in one namespace with one local name are the same attribute. */
        if (auri && !is_decl) {
            for (XNode *o = el->attrs; o; o = o->next) {
                if (o->uri == auri && xd_local_length(o) == xd_local_length(an) &&
                    !memcmp(xd_local(o), xd_local(an), (SIZE_T)xd_local_length(an) * sizeof(WCHAR))) {
                    BOOL r = FAIL(XE_DUPLICATE_ATTRIBUTE, a->at);
                    xd_free(an->value);
                    xd_free(an);
                    xd_free_if_orphan(el);
                    return r;
                }
            }
        }
        xd_attr_link(el, an);
        if (a->colon == 3 && a->nlen == 9 && !memcmp(a->name, L"xml:space", 18)) {
            if (an->length == 8 && !memcmp(an->value, L"preserve", 16))
                preserve = TRUE;
            else if (an->length == 7 && !memcmp(an->value, L"default", 14))
                preserve = FALSE;
        }
    }
    if (doc->max_depth > 0 && ps->depth + 1 > (UINT)doc->max_depth) {
        xd_free_if_orphan(el);
        return FAIL(XE_MAX_DEPTH, tag_at + 1);
    }
    add_node(ps, el);
    if (!empty) {
            if (!grow_frames(ps))
            return fail_at(ps, E_OUTOFMEMORY, tag_at, nullptr, nullptr);
        Frame *f = &ps->frames[ps->depth++];
        f->node = el;
        f->ns_base = ns_base;
        f->preserve = preserve;
        f->name = name;
        f->nlen = nlen;
    } else {
        ps->bind_count = ns_base;
    }
    return TRUE;
}

static BOOL report_unclosed(Parser *ps)
{
    XBuf list = {};
    for (UINT i = 0; i < ps->depth; i++) {
        if (i)
            xb_puta(&list, ", ");
        xb_put(&list, ps->frames[i].name, ps->frames[i].nlen);
    }
    BOOL r = fail_at(ps, XE_TAGS_NOT_CLOSED, ps->e, list.s, nullptr);
    xb_free(&list);
    return r;
}

static BOOL parse_end_tag(Parser *ps)
{
    /* ps->p is on "</". */
    const WCHAR *at = ps->p;
    ps->p += 2;
    if (!ps->depth)
        return FAIL(XE_END_TAG_UNEXPECTED, at);
    const WCHAR *name;
    UINT nlen, colon;
    if (!scan_qname(ps, &name, &nlen, &colon))
        return FALSE;
    Frame *f = &ps->frames[ps->depth - 1];
    if (nlen != f->nlen || memcmp(name, f->name, (SIZE_T)nlen * sizeof(WCHAR))) {
        WCHAR *start = xd_wcsdup(f->name, f->nlen), *end = xd_wcsdup(name, nlen);
        BOOL r = fail_at(ps, XE_END_TAG_MISMATCH, ps->p, start, end);
        xd_free(start);
        xd_free(end);
        return r;
    }
    ps->p = skip_space(ps->p, ps->e);
    if (ps->p >= ps->e)
        return FAIL(XE_END_TAG_NO_GT, ps->e);
    if (*ps->p != '>')
        return FAIL(XE_END_TAG_NO_GT, ps->p);
    ps->p++;
    flush_text(ps);
    /* an element written with an end tag keeps it when it has no content. */
    if (!f->node->first)
        f->node->flags |= XF_EXPLICIT;
    ps->bind_count = f->ns_base;
    ps->depth--;
    return TRUE;
}

/* text up to the next markup or reference, with line ends normalized. */
static BOOL parse_text_run(Parser *ps)
{
    const WCHAR *p = ps->p, *e = ps->e;
    XBuf *t = &ps->text;
    while (p < e) {
        const WCHAR *q = p;
        while (q < e && ((*q >= 0x20 && *q < 0xD800 && *q != '<' && *q != '&' && *q != ']') || *q == '\t' || *q == '\n'))
            q++;
        if (q > p) {
            xb_put(t, p, (UINT)(q - p));
            p = q;
            if (p >= e)
                break;
        }
        WCHAR c = *p;
        if (c == '<' || c == '&')
            break;
        if (c == ']') {
            if (p + 2 < e && p[1] == ']' && p[2] == '>') {
                ps->p = p;
                return FAIL(XE_CDATA_END_IN_TEXT, p);
            }
            xb_putc(t, c);
            p++;
        } else if (c == '\r') {
            xb_putc(t, '\n');
            p++;
            if (p < e && *p == '\n')
                p++;
        } else {
            UINT units;
            UINT cp = code_point_at(p, e, &units);
            if (!xd_is_xml_char(cp)) {
                ps->p = p;
                return FAIL(XE_TEXT_CHAR, p);
            }
            xb_put(t, p, units);
            p += units;
        }
    }
    ps->p = p;
    return TRUE;
}

static BOOL parse_content(Parser *ps)
{
    /* the root element's start tag is at ps->p, this runs until it closes. */
    do {
        if (ps->p >= ps->e)
            return report_unclosed(ps);
        if (*ps->p == '<') {
            if (ps->p + 1 >= ps->e)
                return report_unclosed(ps);
            WCHAR c = ps->p[1];
            if (c == '/') {
                if (!parse_end_tag(ps))
                    return FALSE;
            } else if (c == '!') {
                if (STARTS(L"<!--")) {
                    flush_text(ps);
                    if (!parse_comment(ps))
                        return FALSE;
                } else if (STARTS(L"<![CDATA[")) {
                    if (!parse_cdata(ps))
                        return FALSE;
                } else if (STARTS(L"<!DOCTYPE")) {
                    return FAIL(XE_DOCTYPE_OUTSIDE_PROLOG, ps->p);
                } else {
                    return FAIL(XE_CDATA_SYNTAX, ps->p + 2);
                }
            } else if (c == '?') {
                if (!parse_pi(ps))
                    return FALSE;
            } else {
                if (!parse_start_tag(ps))
                    return FALSE;
            }
        } else if (*ps->p == '&') {
            if (!parse_reference(ps, &ps->text, FALSE))
                return FALSE;
            ps->text_ref = TRUE;
        } else {
            if (!parse_text_run(ps))
                return FALSE;
        }
    } while (ps->depth);
    return TRUE;
}

/* ------------------------------------------------------------------ the document */

static BOOL parse_document(Parser *ps)
{
    XDoc *doc = ps->doc;
    ps->a_empty = xd_intern(doc, L"", 0);
    ps->a_xml = xd_intern(doc, L"xml", 3);
    ps->a_xmlns = xd_intern(doc, L"xmlns", 5);
    ps->uri_xml = xd_intern(doc, XML_URI, (UINT)-1);
    ps->uri_xmlns = xd_intern(doc, XMLNS_URI, (UINT)-1);
    if (!ps->a_empty || !ps->a_xml || !ps->a_xmlns || !ps->uri_xml || !ps->uri_xmlns)
        return fail_at(ps, E_OUTOFMEMORY, ps->p, nullptr, nullptr);
    ps->strip = !(doc->flags & XDF_PRESERVE);

    if (ps->p < ps->e && STARTS(L"<?xml") && ps->p + 5 < ps->e && (xd_is_space(ps->p[5]) || ps->p[5] == '?')) {
        if (ps->p[5] == '?')
            return FAIL(XE_VERSION_REQUIRED, ps->p + 5);
        if (!parse_xml_decl(ps))
            return FALSE;
    }
    BOOL root = FALSE, doctype = FALSE, any = ps->e > ps->s;
    for (;;) {
        const WCHAR *before = ps->p;
        ps->p = skip_space(ps->p, ps->e);
        if (ps->p > before) {
            any = TRUE;
            if (doc->node.last)
                doc->node.last->flags |= XF_WS_AFTER;
        }
        if (ps->p >= ps->e)
            break;
        any = TRUE;
        if (*ps->p != '<')
            return FAIL(XE_TOP_LEVEL, ps->p);
        if (ps->p + 1 >= ps->e)
            return FAIL(XE_NAME_START, ps->e);
        WCHAR c = ps->p[1];
        if (c == '!') {
            if (STARTS(L"<!--")) {
                if (!parse_comment(ps))
                    return FALSE;
            } else if (STARTS(L"<!DOCTYPE")) {
                if (root)
                    return FAIL(XE_DOCTYPE_OUTSIDE_PROLOG, ps->p);
                if (doctype)
                    return FAIL(XE_TWO_DOCTYPES, ps->p);
                if (!parse_doctype(ps))
                    return FALSE;
                doctype = TRUE;
            } else {
                return FAIL(XE_TOP_LEVEL, ps->p);
            }
        } else if (c == '?') {
            if (!parse_pi(ps))
                return FALSE;
        } else if (c == '/') {
            return FAIL(XE_END_TAG_UNEXPECTED, ps->p);
        } else if (xd_is_name_start(c)) {
            if (root)
                return FAIL(XE_TWO_ROOTS, ps->p);
            root = TRUE;
            if (!parse_start_tag(ps))
                return FALSE;
            if (ps->depth && !parse_content(ps))
                return FALSE;
        } else {
            return FAIL(XE_NAME_START, ps->p + 1);
        }
    }
    if (!root) {
        if (!any) {
            ps->failed = TRUE;
            xd_err_set(doc, XE_NO_ROOT, 0, 0, 0, nullptr, 0, nullptr, nullptr);
            return FALSE;
        }
        return FAIL(XE_NO_ROOT, ps->e);
    }
    return TRUE;
}

BOOL xd_parse(XDoc *doc, const WCHAR *text, UINT length, UINT flags)
{
    Parser ps = {};
    ps.doc = doc;
    ps.flags = flags;
    if (length && text[0] == 0xFEFF) {
        text++;
        length--;
    }
    ps.s = ps.p = text;
    ps.e = text + length;

    xd_err_clear(doc);
    xd_clear_children(&doc->node);
    if (doc->max_size > 0 && length > (UINT)doc->max_size) {
        fail_at(&ps, XE_MAX_SIZE, text, nullptr, nullptr);
        xd_touch(doc);
        return FALSE;
    }
    for (UINT i = 0; i < doc->entity_count; i++)
        xd_free(doc->entities[i].value);
    doc->entity_count = 0;
    xd_free(doc->encoding);
    doc->encoding = nullptr;

    BOOL ok = parse_document(&ps);
    if (!ok) {
        xd_clear_children(&doc->node);
        if (!doc->err.code) {
            doc->err.code = E_FAIL;
        }
    }
    xd_free(ps.frames);
    xd_free(ps.binds);
    xd_free(ps.attrs);
    xb_free(&ps.text);
    xb_free(&ps.vals);
    xd_touch(doc);
    return ok;
}
