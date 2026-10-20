/* writing nodes as xml, and the encodings of files and streams. */
#include <xmldom/xmldom.h>

typedef struct CodePage {
    const char *name;
    UINT page;
} CodePage;

static const CodePage pages[] = {
    {"utf-8", 65001},       {"utf8", 65001},        {"utf-16", 1200},       {"utf-16le", 1200},
    {"ucs-2", 1200},        {"unicode", 1200},      {"utf16", 1200},        {"utf-16be", 1201},
    {"unicodefffe", 1201},  {"iso-8859-1", 28591},  {"iso_8859-1", 28591},  {"latin1", 28591},
    {"l1", 28591},          {"us-ascii", 20127},    {"ascii", 20127},       {"windows-1252", 1252},
    {"cp1252", 1252},       {"windows-1250", 1250}, {"windows-1251", 1251}, {"windows-1253", 1253},
    {"windows-1254", 1254}, {"windows-1255", 1255}, {"windows-1256", 1256}, {"windows-1257", 1257},
    {"windows-1258", 1258}, {"windows-874", 874},   {"iso-8859-2", 28592},  {"iso-8859-3", 28593},
    {"iso-8859-4", 28594},  {"iso-8859-5", 28595},  {"iso-8859-6", 28596},  {"iso-8859-7", 28597},
    {"iso-8859-8", 28598},  {"iso-8859-9", 28599},  {"iso-8859-13", 28603}, {"iso-8859-15", 28605},
    {"koi8-r", 20866},      {"koi8-u", 21866},      {"shift_jis", 932},     {"shift-jis", 932},
    {"ms_kanji", 932},      {"gb2312", 936},        {"gbk", 936},           {"big5", 950},
    {"euc-kr", 949},        {"ks_c_5601-1987", 949}, {"euc-jp", 20932},     {"ibm437", 437},
    {"cp437", 437},         {"ibm850", 850},        {"cp850", 850},         {"iso-2022-jp", 50220},
};

static BOOL name_equals(const WCHAR *w, const char *a)
{
    for (;; w++, a++) {
        WCHAR c = *w;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != (UCHAR)*a)
            return FALSE;
        if (!c)
            return TRUE;
    }
}

UINT xd_code_page(const WCHAR *name)
{
    for (UINT i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
        if (name_equals(name, pages[i].name))
            return pages[i].page;
    return 0;
}

BOOL xd_valid_encoding(const WCHAR *name)
{
    return xd_code_page(name) != 0;
}

/* ------------------------------------------------------------------ writing nodes */

typedef struct Scope {
    const WCHAR *prefix; /* an atom, the empty string for the default namespace. */
    const WCHAR *uri;
} Scope;

typedef struct WFrame {
    XNode *node;
    UINT scope;
    BOOL block;
    BOOL level;
} WFrame;

typedef struct Writer {
    XBuf *out;
    BOOL indent;
    UINT max;
    const WCHAR *encoding;
    UINT level;
    BOOL pending;
    WFrame *frames;
    UINT depth;
    UINT frame_cap;
    Scope *scopes;
    UINT scope_count;
    UINT scope_cap;
} Writer;

static void flush_pending(Writer *w)
{
    if (!w->pending)
        return;
    w->pending = FALSE;
    xb_putc(w->out, '\r');
    xb_putc(w->out, '\n');
    for (UINT i = 0; i < w->level; i++)
        xb_putc(w->out, '\t');
}

static void put_ref(XBuf *b, UINT c)
{
    WCHAR digits[12];
    UINT n = 0;
    xb_puta(b, "&#");
    do {
        digits[n++] = (WCHAR)('0' + c % 10);
        c /= 10;
    } while (c);
    while (n)
        xb_putc(b, digits[--n]);
    xb_putc(b, ';');
}

/* text with the escapes of character data. */
static void put_text(Writer *w, const WCHAR *s, UINT length)
{
    XBuf *b = w->out;
    for (UINT i = 0; i < length; i++) {
        WCHAR c = s[i];
        switch (c) {
        case '&':
            xb_puta(b, "&amp;");
            break;
        case '<':
            xb_puta(b, "&lt;");
            break;
        case '>':
            xb_puta(b, "&gt;");
            break;
        case '\r':
            xb_putc(b, '\r');
            xb_putc(b, '\n');
            if (i + 1 < length && s[i + 1] == '\n')
                i++;
            break;
        case '\n':
            xb_putc(b, '\r');
            xb_putc(b, '\n');
            break;
        default:
            if (c > w->max) {
                UINT cp = c;
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                    cp = 0x10000 + ((c - 0xD800u) << 10) + (s[i + 1] - 0xDC00u);
                    i++;
                }
                put_ref(b, cp);
            } else {
                xb_putc(b, c);
            }
        }
    }
}

static void put_attr_value(Writer *w, const WCHAR *s, UINT length)
{
    XBuf *b = w->out;
    for (UINT i = 0; i < length; i++) {
        WCHAR c = s[i];
        switch (c) {
        case '&':
            xb_puta(b, "&amp;");
            break;
        case '<':
            xb_puta(b, "&lt;");
            break;
        case '>':
            xb_puta(b, "&gt;");
            break;
        case '"':
            xb_puta(b, "&quot;");
            break;
        case '\n':
            xb_puta(b, "&#xA;");
            break;
        case '\r':
            /* a line end is one character reference, a pair of them too. */
            xb_puta(b, "&#xA;");
            if (i + 1 < length && s[i + 1] == '\n')
                i++;
            break;
        default:
            if (c > w->max) {
                UINT cp = c;
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                    cp = 0x10000 + ((c - 0xD800u) << 10) + (s[i + 1] - 0xDC00u);
                    i++;
                }
                put_ref(b, cp);
            } else {
                xb_putc(b, c);
            }
        }
    }
}

static BOOL push_scope(Writer *w, const WCHAR *prefix, const WCHAR *uri)
{
    if (w->scope_count == w->scope_cap) {
        UINT cap = w->scope_cap ? w->scope_cap * 2 : 16;
        Scope *s = xd_realloc(w->scopes, (SIZE_T)cap * sizeof(Scope));
        if (!s)
            return FALSE;
        w->scopes = s;
        w->scope_cap = cap;
    }
    w->scopes[w->scope_count].prefix = prefix;
    w->scopes[w->scope_count].uri = uri;
    w->scope_count++;
    return TRUE;
}

/* the uri a prefix is bound to among the declarations written so far, null for none. */
static const WCHAR *scope_lookup(Writer *w, const WCHAR *prefix, UINT plen)
{
    for (UINT i = w->scope_count; i-- > 0;) {
        const WCHAR *p = w->scopes[i].prefix;
        if (xd_atom_len(p) == plen && !memcmp(p, prefix, (SIZE_T)plen * sizeof(WCHAR)))
            return w->scopes[i].uri;
    }
    return nullptr;
}

static BOOL has_text_child(XNode *n)
{
    for (XNode *c = n->first; c; c = c->next)
        if (c->type == XN_TEXT || c->type == XN_CDATA || c->type == XN_ENTITYREF)
            return TRUE;
    return FALSE;
}

static void put_name(Writer *w, const WCHAR *s, UINT n)
{
    xb_put(w->out, s, n);
}

static void write_xml_decl(Writer *w, XNode *n)
{
    XBuf *b = w->out;
    xb_puta(b, "<?xml");
    XNode *v = xd_attr_find(n, L"version", 7);
    xb_puta(b, " version=\"");
    if (v && v->value)
        xb_put(b, v->value, v->length);
    else
        xb_puta(b, "1.0");
    xb_putc(b, '"');
    XNode *e = xd_attr_find(n, L"encoding", 8);
    if (e && e->value && w->encoding && !xd_wcsicmp(e->value, w->encoding)) {
        xb_puta(b, " encoding=\"");
        xb_put(b, e->value, e->length);
        xb_putc(b, '"');
    }
    XNode *s = xd_attr_find(n, L"standalone", 10);
    if (s && s->value) {
        xb_puta(b, " standalone=\"");
        xb_put(b, s->value, s->length);
        xb_putc(b, '"');
    }
    xb_puta(b, "?>");
}

static void write_start(Writer *w, XNode *n, BOOL empty)
{
    XBuf *b = w->out;
    UINT qlen = xd_atom_len(n->qname);
    xb_putc(b, '<');
    put_name(w, n->qname, qlen);
    UINT mark = w->scope_count;
    /* the declarations the element carries are in force for its own name. */
    for (XNode *a = n->attrs; a; a = a->next) {
        if (!xd_is_xmlns_attr(a))
            continue;
        UINT alen = xd_atom_len(a->qname);
        const WCHAR *prefix = alen > 6 ? a->qname + 6 : L"";
        UINT plen = alen > 6 ? alen - 6 : 0;
        const WCHAR *atom = xd_intern(n->doc, prefix, plen);
        if (atom)
            push_scope(w, atom, a->value && *a->value ? xd_intern(n->doc, a->value, a->length) : nullptr);
    }
    /* a name whose namespace is not declared in scope gets its declaration. */
    UINT plen = n->local ? (UINT)(n->local - 1) : 0;
    const WCHAR *bound = scope_lookup(w, n->qname, plen);
    if (n->uri) {
        if (bound != n->uri) {
            const WCHAR *prefix = plen ? xd_intern(n->doc, n->qname, plen) : xd_intern(n->doc, L"", 0);
            xb_puta(b, plen ? " xmlns:" : " xmlns");
            if (plen)
                put_name(w, n->qname, plen);
            xb_puta(b, "=\"");
            put_attr_value(w, n->uri, xd_wlen(n->uri));
            xb_putc(b, '"');
            if (prefix)
                push_scope(w, prefix, n->uri);
        }
    } else if (!plen && bound) {
        const WCHAR *empty_atom = xd_intern(n->doc, L"", 0);
        xb_puta(b, " xmlns=\"\"");
        if (empty_atom)
            push_scope(w, empty_atom, nullptr);
    }
    for (XNode *a = n->attrs; a; a = a->next) {
        UINT alen = xd_atom_len(a->qname);
        if (a->uri && !xd_is_xmlns_attr(a) && a->local) {
            UINT ap = (UINT)(a->local - 1);
            if (scope_lookup(w, a->qname, ap) != a->uri) {
                const WCHAR *prefix = xd_intern(n->doc, a->qname, ap);
                xb_puta(b, " xmlns:");
                put_name(w, a->qname, ap);
                xb_puta(b, "=\"");
                put_attr_value(w, a->uri, xd_wlen(a->uri));
                xb_putc(b, '"');
                if (prefix)
                    push_scope(w, prefix, a->uri);
            }
        }
        xb_putc(b, ' ');
        put_name(w, a->qname, alen);
        xb_puta(b, "=\"");
        if (a->value)
            put_attr_value(w, a->value, a->length);
        xb_putc(b, '"');
    }
    xb_puta(b, empty ? "/>" : ">");
    if (empty)
        w->scope_count = mark;
}

static BOOL write_open(Writer *w, XNode *n)
{
    XBuf *b = w->out;
    flush_pending(w);
    switch (n->type) {
    case XN_ELEMENT: {
        UINT mark = w->scope_count;
        BOOL empty = !n->first && !(n->flags & XF_EXPLICIT);
        write_start(w, n, empty);
        if (empty)
            return FALSE;
        if (w->depth == w->frame_cap) {
            UINT cap = w->frame_cap ? w->frame_cap * 2 : 32;
            WFrame *f = xd_realloc(w->frames, (SIZE_T)cap * sizeof(WFrame));
            if (!f)
                return FALSE;
            w->frames = f;
            w->frame_cap = cap;
        }
        WFrame *f = &w->frames[w->depth++];
        f->node = n;
        f->scope = mark;
        f->block = w->indent && n->first && !(n->flags & XF_PRESERVE) && !has_text_child(n);
        f->level = f->block;
        if (f->block) {
            w->level++;
            w->pending = TRUE;
        }
        if (!n->first) {
            /* written with an end tag only: nothing to descend into. */
            return FALSE;
        }
        return TRUE;
    }
    case XN_DOCUMENT:
    case XN_FRAGMENT: {
        if (w->depth == w->frame_cap) {
            UINT cap = w->frame_cap ? w->frame_cap * 2 : 32;
            WFrame *f = xd_realloc(w->frames, (SIZE_T)cap * sizeof(WFrame));
            if (!f)
                return FALSE;
            w->frames = f;
            w->frame_cap = cap;
        }
        WFrame *f = &w->frames[w->depth++];
        f->node = n;
        f->scope = w->scope_count;
        f->block = n->type == XN_DOCUMENT && w->indent;
        f->level = FALSE;
        return n->first != nullptr;
    }
    case XN_TEXT:
        if (n->value)
            put_text(w, n->value, n->length);
        break;
    case XN_CDATA:
        xb_puta(b, "<![CDATA[");
        if (n->value) {
            for (UINT i = 0; i < n->length; i++) {
                if (n->value[i] == ']' && i + 2 < n->length && n->value[i + 1] == ']' && n->value[i + 2] == '>') {
                    xb_puta(b, "]]]]><![CDATA[>");
                    i += 2;
                } else {
                    xb_putc(b, n->value[i]);
                }
            }
        }
        xb_puta(b, "]]>");
        break;
    case XN_COMMENT:
        xb_puta(b, "<!--");
        if (n->value)
            xb_put(b, n->value, n->length);
        xb_puta(b, "-->");
        break;
    case XN_PI:
        if (n->flags & XF_XMLDECL) {
            write_xml_decl(w, n);
        } else {
            xb_puta(b, "<?");
            put_name(w, n->qname, xd_atom_len(n->qname));
            if (n->length) {
                xb_putc(b, ' ');
                xb_put(b, n->value, n->length);
            }
            xb_puta(b, "?>");
        }
        break;
    case XN_ATTRIBUTE:
        put_name(w, n->qname, xd_atom_len(n->qname));
        xb_puta(b, "=\"");
        if (n->value)
            put_attr_value(w, n->value, n->length);
        xb_putc(b, '"');
        break;
    case XN_DOCTYPE:
        xb_puta(b, "<!DOCTYPE ");
        put_name(w, n->qname, xd_atom_len(n->qname));
        if (n->value)
            xb_put(b, n->value, n->length);
        xb_putc(b, '>');
        break;
    case XN_ENTITYREF:
        xb_putc(b, '&');
        put_name(w, n->qname, xd_atom_len(n->qname));
        xb_putc(b, ';');
        break;
    }
    return FALSE;
}

static void write_close(Writer *w, XNode *n)
{
    WFrame *f = &w->frames[--w->depth];
    if (n->type == XN_ELEMENT) {
        if (f->level && w->level)
            w->level--;
        flush_pending(w);
        xb_puta(w->out, "</");
        xb_put(w->out, n->qname, xd_atom_len(n->qname));
        xb_putc(w->out, '>');
        w->scope_count = f->scope;
    }
}

void xd_write(XNode *root, XBuf *out, BOOL indent, UINT max, const WCHAR *encoding)
{
    Writer w = {};
    w.out = out;
    w.indent = indent;
    w.max = max;
    w.encoding = encoding;
    XNode *n = root;
    for (;;) {
        if (write_open(&w, n)) {
            n = n->first;
            continue;
        }
        /* an element or container with nothing below it is closed right away. */
        if (w.depth && w.frames[w.depth - 1].node == n)
            write_close(&w, n);
        for (;;) {
            if (n == root)
                goto done;
            XNode *parent = n->parent;
            if (w.depth && w.frames[w.depth - 1].node == parent && w.frames[w.depth - 1].block)
                w.pending = TRUE;
            if (n->next) {
                n = n->next;
                break;
            }
            n = parent;
            write_close(&w, n);
        }
    }
done:
    if (root->type == XN_DOCUMENT && w.pending) {
        w.level = 0;
        flush_pending(&w);
    }
    xd_free(w.frames);
    xd_free(w.scopes);
}

/* ------------------------------------------------------------------ encodings */

typedef struct XBuf8 {
    BYTE *p;
    UINT length;
    UINT cap;
    BOOL failed;
} XBuf8;

static void b8_put(XBuf8 *b, const BYTE *data, UINT n)
{
    if (b->failed)
        return;
    if (b->length + n > b->cap) {
        UINT cap = b->cap ? b->cap : 256;
        while (cap < b->length + n)
            cap += cap / 2 + 64;
        BYTE *p = xd_realloc(b->p, cap);
        if (!p) {
            b->failed = TRUE;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->length, data, n);
    b->length += n;
}

static void b8_putc(XBuf8 *b, BYTE c)
{
    b8_put(b, &c, 1);
}

static BOOL put_utf8(XBuf8 *b, UINT c)
{
    BYTE t[4];
    if (c < 0x80) {
        t[0] = (BYTE)c;
        b8_put(b, t, 1);
    } else if (c < 0x800) {
        t[0] = (BYTE)(0xC0 | (c >> 6));
        t[1] = (BYTE)(0x80 | (c & 0x3F));
        b8_put(b, t, 2);
    } else if (c < 0x10000) {
        t[0] = (BYTE)(0xE0 | (c >> 12));
        t[1] = (BYTE)(0x80 | ((c >> 6) & 0x3F));
        t[2] = (BYTE)(0x80 | (c & 0x3F));
        b8_put(b, t, 3);
    } else {
        t[0] = (BYTE)(0xF0 | (c >> 18));
        t[1] = (BYTE)(0x80 | ((c >> 12) & 0x3F));
        t[2] = (BYTE)(0x80 | ((c >> 6) & 0x3F));
        t[3] = (BYTE)(0x80 | (c & 0x3F));
        b8_put(b, t, 4);
    }
    return TRUE;
}

BOOL xd_encode(const WCHAR *text, UINT length, const WCHAR *encoding, BYTE **bytes, UINT *size)
{
    UINT page = encoding ? xd_code_page(encoding) : 65001;
    if (!page)
        return FALSE;
    XBuf8 b = {};
    if (page == 1200 || page == 1201) {
        BOOL big = page == 1201;
        /* byte order marks come with the encoding. */
        BYTE bom[2] = {big ? 0xFE : 0xFF, big ? 0xFF : 0xFE};
        b8_put(&b, bom, 2);
        for (UINT i = 0; i < length; i++) {
            BYTE two[2] = {big ? (BYTE)(text[i] >> 8) : (BYTE)text[i], big ? (BYTE)text[i] : (BYTE)(text[i] >> 8)};
            b8_put(&b, two, 2);
        }
    } else if (page == 65001) {
        for (UINT i = 0; i < length; i++) {
            UINT c = text[i];
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00u);
                i++;
            } else if (c >= 0xD800 && c <= 0xDFFF) {
                c = 0xFFFD;
            }
            put_utf8(&b, c);
        }
    } else if (page == 28591 || page == 20127 || page == 1252) {
        UINT limit = page == 20127 ? 0x7F : 0xFF;
        for (UINT i = 0; i < length; i++)
            b8_putc(&b, text[i] <= limit ? (BYTE)text[i] : '?');
    } else {
        int n = WideCharToMultiByte(page, 0, text, (int)length, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            BYTE *p = xd_alloc_raw((SIZE_T)n);
            if (p) {
                WideCharToMultiByte(page, 0, text, (int)length, (LPSTR)p, n, nullptr, nullptr);
                b8_put(&b, p, (UINT)n);
                xd_free(p);
            }
        }
    }
    if (b.failed) {
        xd_free(b.p);
        return FALSE;
    }
    *bytes = b.p;
    *size = b.length;
    return TRUE;
}

/* the highest character an encoding holds without references. */
static UINT encoding_max(const WCHAR *encoding)
{
    UINT page = xd_code_page(encoding);
    if (page == 20127)
        return 0x7F;
    if (page == 28591 || page == 1252)
        return 0xFF;
    return 0x10FFFF;
}

const WCHAR *xd_decl_attr(XDoc *doc, const char *name)
{
    for (XNode *c = doc->node.first; c; c = c->next) {
        if (c->type == XN_PI && (c->flags & XF_XMLDECL)) {
            WCHAR wide[16];
            UINT n = 0;
            while (name[n]) {
                wide[n] = (UCHAR)name[n];
                n++;
            }
            XNode *a = xd_attr_find(c, wide, n);
            return a ? a->value : nullptr;
        }
    }
    return nullptr;
}

static BOOL document_bytes(XDoc *doc, const WCHAR *encoding, BYTE **bytes, UINT *size, HRESULT *hr)
{
    const WCHAR *use = encoding ? encoding : xd_decl_attr(doc, "encoding");
    if (!use)
        use = L"UTF-8";
    if (!xd_valid_encoding(use)) {
        *hr = (HRESULT)XE_BAD_ENCODING;
        return FALSE;
    }
    XBuf text = {};
    xd_write(&doc->node, &text, xd_indent(doc), encoding_max(use), use);
    if (text.failed) {
        xb_free(&text);
        *hr = E_OUTOFMEMORY;
        return FALSE;
    }
    BOOL ok = xd_encode(text.s ? text.s : L"", text.length, use, bytes, size);
    xb_free(&text);
    if (!ok)
        *hr = E_OUTOFMEMORY;
    return ok;
}

HRESULT xd_save_document(XDoc *doc, IStream *stream, const WCHAR *encoding)
{
    BYTE *bytes;
    UINT size;
    HRESULT hr = S_OK;
    if (!document_bytes(doc, encoding, &bytes, &size, &hr))
        return hr;
    UINT done = 0;
    while (done < size && SUCCEEDED(hr)) {
        ULONG wrote = 0;
        hr = stream->lpVtbl->Write(stream, bytes + done, size - done, &wrote);
        if (SUCCEEDED(hr) && !wrote)
            hr = E_FAIL;
        done += wrote;
    }
    xd_free(bytes);
    return hr;
}

HRESULT xd_save_file(XDoc *doc, const WCHAR *path)
{
    BYTE *bytes;
    UINT size;
    HRESULT hr = S_OK;
    if (!document_bytes(doc, nullptr, &bytes, &size, &hr))
        return hr;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        xd_free(bytes);
        return err ? (HRESULT)(0x80070000 | (err & 0xFFFF)) : E_FAIL;
    }
    DWORD wrote = 0;
    if (!WriteFile(file, bytes, size, &wrote, nullptr) || wrote != size) {
        DWORD err = GetLastError();
        hr = err ? (HRESULT)(0x80070000 | (err & 0xFFFF)) : E_FAIL;
    }
    CloseHandle(file);
    xd_free(bytes);
    return hr;
}
