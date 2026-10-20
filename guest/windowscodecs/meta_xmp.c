/*
 * xmp: a packet read into a block of properties with a schema and a name, whose values are text or blocks of their own for
 * bags, sequences, alternatives and structures, and written back as a packet.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"
#include "meta_codecs.h"

#define MAX_NS 32
#define MAX_DEPTH 8

typedef struct NsMap {
    WCHAR *prefix[MAX_NS];
    WCHAR *uri[MAX_NS];
    UINT count;
} NsMap;

typedef struct Xml {
    const char *p;
    const char *end;
} Xml;

/* ------------------------------------------------------------------ strings */

static WCHAR *dup_utf8(const char *s, SIZE_T n)
{
    WCHAR *w = CoTaskMemAlloc((n + 1) * sizeof(WCHAR));
    if (!w)
        return nullptr;
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < n;) {
        UINT c = (BYTE)s[i++];
        if (c >= 0xc0 && c < 0xe0 && i < n) {
            c = ((c & 0x1f) << 6) | ((BYTE)s[i++] & 0x3f);
        } else if (c >= 0xe0 && c < 0xf0 && i + 1 < n) {
            c = ((c & 0x0f) << 12) | (((BYTE)s[i] & 0x3f) << 6) | ((BYTE)s[i + 1] & 0x3f);
            i += 2;
        } else if (c >= 0xf0 && i + 2 < n) {
            c = ((c & 0x07) << 18) | (((BYTE)s[i] & 0x3f) << 12) | (((BYTE)s[i + 1] & 0x3f) << 6) | ((BYTE)s[i + 2] & 0x3f);
            i += 3;
            c -= 0x10000;
            w[k++] = (WCHAR)(0xd800 | (c >> 10));
            c = 0xdc00 | (c & 0x3ff);
        }
        w[k++] = (WCHAR)c;
    }
    w[k] = 0;
    return w;
}

/* decodes entities in place of a copy. */
static WCHAR *decode_text(const char *s, SIZE_T n)
{
    char *tmp = WicAlloc(n + 1);
    if (!tmp)
        return nullptr;
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < n;) {
        if (s[i] == '&') {
            SIZE_T j = i + 1;
            while (j < n && s[j] != ';' && j - i < 10)
                j++;
            if (j < n && s[j] == ';') {
                SIZE_T len = j - i - 1;
                const char *e = s + i + 1;
                if (len == 2 && !memcmp(e, "lt", 2)) {
                    tmp[k++] = '<';
                    i = j + 1;
                    continue;
                }
                if (len == 2 && !memcmp(e, "gt", 2)) {
                    tmp[k++] = '>';
                    i = j + 1;
                    continue;
                }
                if (len == 3 && !memcmp(e, "amp", 3)) {
                    tmp[k++] = '&';
                    i = j + 1;
                    continue;
                }
                if (len == 4 && !memcmp(e, "quot", 4)) {
                    tmp[k++] = '"';
                    i = j + 1;
                    continue;
                }
                if (len == 4 && !memcmp(e, "apos", 4)) {
                    tmp[k++] = '\'';
                    i = j + 1;
                    continue;
                }
                if (len >= 2 && e[0] == '#') {
                    UINT c = 0;
                    SIZE_T q = 1;
                    unsigned base = 10;
                    if (e[q] == 'x') {
                        base = 16;
                        q++;
                    }
                    for (; q < len; q++) {
                        UINT d = e[q] >= '0' && e[q] <= '9' ? e[q] - '0' : (e[q] | 32) >= 'a' && (e[q] | 32) <= 'f' ? (e[q] | 32) - 'a' + 10 : 99;
                        if (d >= base)
                            break;
                        c = c * base + d;
                    }
                    if (q == len && c) {
                        if (c < 0x80) {
                            tmp[k++] = (char)c;
                        } else if (c < 0x800) {
                            tmp[k++] = (char)(0xc0 | (c >> 6));
                            tmp[k++] = (char)(0x80 | (c & 0x3f));
                        } else if (c < 0x10000) {
                            tmp[k++] = (char)(0xe0 | (c >> 12));
                            tmp[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
                            tmp[k++] = (char)(0x80 | (c & 0x3f));
                        } else {
                            tmp[k++] = (char)(0xf0 | (c >> 18));
                            tmp[k++] = (char)(0x80 | ((c >> 12) & 0x3f));
                            tmp[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
                            tmp[k++] = (char)(0x80 | (c & 0x3f));
                        }
                        i = j + 1;
                        continue;
                    }
                }
            }
        }
        tmp[k++] = s[i++];
    }
    WCHAR *w = dup_utf8(tmp, k);
    WicFree(tmp);
    return w;
}

static const char *find_colon(const char *s, SIZE_T n)
{
    for (SIZE_T i = 0; i < n; i++) {
        if (s[i] == ':')
            return s + i;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ parsing */

typedef struct Attr {
    const char *name;
    SIZE_T name_length;
    const char *value;
    SIZE_T value_length;
} Attr;

typedef struct Tag {
    const char *name;
    SIZE_T name_length;
    Attr attrs[24];
    UINT attr_count;
    BOOL closing;
    BOOL empty;
    BOOL special; /* a comment, declaration or processing instruction. */
} Tag;

static BOOL is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* reads the tag at the cursor, which is at a '<'. */
static BOOL read_tag(Xml *x, Tag *t)
{
    memset(t, 0, sizeof(*t));
    const char *p = x->p + 1;
    if (p >= x->end)
        return FALSE;
    if (*p == '!' || *p == '?') {
        t->special = TRUE;
        if (p + 2 < x->end && p[0] == '!' && p[1] == '-' && p[2] == '-') {
            const char *q = p + 3;
            while (q + 2 < x->end && !(q[0] == '-' && q[1] == '-' && q[2] == '>'))
                q++;
            x->p = q + 3 <= x->end ? q + 3 : x->end;
            return TRUE;
        }
        const char *q = p;
        while (q < x->end && *q != '>')
            q++;
        x->p = q < x->end ? q + 1 : x->end;
        return TRUE;
    }
    if (*p == '/') {
        t->closing = TRUE;
        p++;
    }
    t->name = p;
    while (p < x->end && !is_space(*p) && *p != '>' && *p != '/')
        p++;
    t->name_length = (SIZE_T)(p - t->name);
    while (p < x->end && *p != '>') {
        while (p < x->end && is_space(*p))
            p++;
        if (p >= x->end)
            return FALSE;
        if (*p == '/') {
            t->empty = TRUE;
            p++;
            continue;
        }
        if (*p == '>')
            break;
        if (t->attr_count == WIC_ARRAY_SIZE(t->attrs)) {
            while (p < x->end && *p != '>')
                p++;
            break;
        }
        Attr *a = &t->attrs[t->attr_count];
        a->name = p;
        while (p < x->end && *p != '=' && !is_space(*p) && *p != '>')
            p++;
        a->name_length = (SIZE_T)(p - a->name);
        while (p < x->end && is_space(*p))
            p++;
        if (p >= x->end || *p != '=')
            continue;
        p++;
        while (p < x->end && is_space(*p))
            p++;
        if (p >= x->end || (*p != '"' && *p != '\''))
            return FALSE;
        char q = *p++;
        a->value = p;
        while (p < x->end && *p != q)
            p++;
        if (p >= x->end)
            return FALSE;
        a->value_length = (SIZE_T)(p - a->value);
        p++;
        t->attr_count++;
    }
    if (p >= x->end)
        return FALSE;
    x->p = p + 1;
    return TRUE;
}

static BOOL name_is(const Tag *t, const char *s) { return t->name_length == strlen(s) && !memcmp(t->name, s, t->name_length); }

static void ns_free(NsMap *ns)
{
    for (UINT i = 0; i < ns->count; i++) {
        CoTaskMemFree(ns->prefix[i]);
        CoTaskMemFree(ns->uri[i]);
    }
    ns->count = 0;
}

static void ns_declare(NsMap *ns, const Tag *t)
{
    for (UINT i = 0; i < t->attr_count; i++) {
        const Attr *a = &t->attrs[i];
        if (a->name_length > 6 && !memcmp(a->name, "xmlns:", 6) && ns->count < MAX_NS) {
            ns->prefix[ns->count] = dup_utf8(a->name + 6, a->name_length - 6);
            ns->uri[ns->count] = decode_text(a->value, a->value_length);
            if (ns->prefix[ns->count] && ns->uri[ns->count])
                ns->count++;
            else {
                CoTaskMemFree(ns->prefix[ns->count]);
                CoTaskMemFree(ns->uri[ns->count]);
            }
        }
    }
}

static const WCHAR *ns_uri(const NsMap *ns, const char *prefix, SIZE_T length)
{
    for (UINT i = ns->count; i > 0; i--) {
        if (wcslen(ns->prefix[i - 1]) == length) {
            BOOL same = TRUE;
            for (SIZE_T k = 0; k < length && same; k++)
                same = ns->prefix[i - 1][k] == (BYTE)prefix[k];
            if (same)
                return ns->uri[i - 1];
        }
    }
    return nullptr;
}

static HRESULT xmp_item(IWICMetadataReader *block, const WCHAR *schema, const WCHAR *name, PROPVARIANT *value)
{
    PROPVARIANT sc, id;
    WicPvInit(&sc);
    WicPvInit(&id);
    HRESULT hr = S_OK;
    if (schema) {
        sc.vt = VT_LPWSTR;
        sc.pwszVal = CoTaskMemAlloc((wcslen(schema) + 1) * sizeof(WCHAR));
        if (!sc.pwszVal)
            return E_OUTOFMEMORY;
        wcscpy(sc.pwszVal, schema);
    }
    if (name) {
        id.vt = VT_LPWSTR;
        id.pwszVal = CoTaskMemAlloc((wcslen(name) + 1) * sizeof(WCHAR));
        if (!id.pwszVal) {
            PropVariantClear(&sc);
            return E_OUTOFMEMORY;
        }
        wcscpy(id.pwszVal, name);
    }
    hr = WicMetaAdd(block, &sc, &id, value);
    PropVariantClear(&sc);
    PropVariantClear(&id);
    return hr;
}

static HRESULT xmp_text_item(IWICMetadataReader *block, const WCHAR *schema, const WCHAR *name, WCHAR *text)
{
    PROPVARIANT v;
    WicPvInit(&v);
    v.vt = VT_LPWSTR;
    v.pwszVal = text;
    HRESULT hr = xmp_item(block, schema, name, &v);
    PropVariantClear(&v);
    return hr;
}

static HRESULT xmp_index_item(IWICMetadataReader *block, UINT index, PROPVARIANT *value, const WCHAR *lang)
{
    PROPVARIANT id;
    WicPvInit(&id);
    if (lang) {
        id.vt = VT_LPWSTR;
        id.pwszVal = CoTaskMemAlloc((wcslen(lang) + 1) * sizeof(WCHAR));
        if (!id.pwszVal)
            return E_OUTOFMEMORY;
        wcscpy(id.pwszVal, lang);
    } else {
        id.vt = VT_UI8;
        id.uhVal.QuadPart = index;
    }
    HRESULT hr = WicMetaAdd(block, nullptr, &id, value);
    PropVariantClear(&id);
    return hr;
}

static HRESULT parse_property(Xml *x, const Tag *open, NsMap *ns, IWICMetadataReader *parent, UINT depth);

/* reads what is inside an element up to its closing tag: plain text, or a collection, or a structure. */
static HRESULT parse_value(Xml *x, const Tag *open, NsMap *ns, IWICMetadataReader *parent, const WCHAR *schema,
                           const WCHAR *name, UINT depth)
{
    HRESULT hr = S_OK;
    if (open->empty)
        return xmp_text_item(parent, schema, name, dup_utf8("", 0));
    const char *start = x->p;
    const char *p = start;
    while (p < x->end && *p != '<')
        p++;
    /* only text up to the closing tag. */
    if (p < x->end && p + 1 < x->end && p[1] == '/') {
        WCHAR *text = decode_text(start, (SIZE_T)(p - start));
        x->p = p;
        Tag close;
        if (!read_tag(x, &close))
            return WINCODEC_ERR_BADMETADATAHEADER;
        if (!text)
            return E_OUTOFMEMORY;
        return xmp_text_item(parent, schema, name, text);
    }
    x->p = p;
    GUID format = GUID_MetadataFormatXMPStruct;
    IWICMetadataReader *block = nullptr;
    Tag t;
    /* the first element decides: a bag, seq or alt, or a nested description. */
    while (x->p < x->end && SUCCEEDED(hr)) {
        if (*x->p != '<') {
            x->p++;
            continue;
        }
        if (!read_tag(x, &t))
            return WINCODEC_ERR_BADMETADATAHEADER;
        if (t.special)
            continue;
        if (t.closing) {
            if (block) {
                PROPVARIANT v;
                WicPvInit(&v);
                v.vt = VT_UNKNOWN;
                v.punkVal = (IUnknown *)block;
                hr = xmp_item(parent, schema, name, &v);
                block->lpVtbl->Release(block);
            }
            return hr;
        }
        if (name_is(&t, "rdf:Bag") || name_is(&t, "rdf:Seq") || name_is(&t, "rdf:Alt")) {
            format = name_is(&t, "rdf:Bag") ? GUID_MetadataFormatXMPBag
                     : name_is(&t, "rdf:Seq") ? GUID_MetadataFormatXMPSeq
                                              : GUID_MetadataFormatXMPAlt;
            hr = WicMetaReaderCreate(&format, &block);
            UINT index = 0;
            while (SUCCEEDED(hr) && x->p < x->end) {
                while (x->p < x->end && *x->p != '<')
                    x->p++;
                Tag li;
                if (x->p >= x->end || !read_tag(x, &li)) {
                    hr = WINCODEC_ERR_BADMETADATAHEADER;
                    break;
                }
                if (li.special)
                    continue;
                if (li.closing)
                    break;
                const WCHAR *lang = nullptr;
                WCHAR *lang_copy = nullptr;
                for (UINT a = 0; a < li.attr_count; a++) {
                    if (li.attrs[a].name_length == 8 && !memcmp(li.attrs[a].name, "xml:lang", 8)) {
                        lang_copy = dup_utf8(li.attrs[a].value, li.attrs[a].value_length);
                        lang = lang_copy;
                    }
                }
                if (name_is(&li, "rdf:li")) {
                    /* a text item, or a nested structure inside the item. */
                    const char *vs = x->p;
                    const char *vp = vs;
                    while (vp < x->end && *vp != '<')
                        vp++;
                    PROPVARIANT v;
                    WicPvInit(&v);
                    if (li.empty) {
                        v.vt = VT_LPWSTR;
                        v.pwszVal = dup_utf8("", 0);
                    } else if (vp < x->end && vp + 1 < x->end && vp[1] == '/') {
                        v.vt = VT_LPWSTR;
                        v.pwszVal = decode_text(vs, (SIZE_T)(vp - vs));
                        x->p = vp;
                        Tag close;
                        read_tag(x, &close);
                    } else {
                        /* a structure: the properties of the description inside, or of the item itself. */
                        IWICMetadataReader *st;
                        hr = WicMetaReaderCreate(&GUID_MetadataFormatXMPStruct, &st);
                        if (SUCCEEDED(hr) && depth < MAX_DEPTH) {
                            x->p = vp;
                            Tag d;
                            while (x->p < x->end && SUCCEEDED(hr)) {
                                while (x->p < x->end && *x->p != '<')
                                    x->p++;
                                if (x->p >= x->end || !read_tag(x, &d)) {
                                    hr = WINCODEC_ERR_BADMETADATAHEADER;
                                    break;
                                }
                                if (d.special)
                                    continue;
                                if (d.closing && name_is(&d, "rdf:li"))
                                    break;
                                if (d.closing)
                                    continue;
                                if (name_is(&d, "rdf:Description"))
                                    continue;
                                hr = parse_property(x, &d, ns, st, depth + 1);
                            }
                        }
                        if (SUCCEEDED(hr)) {
                            v.vt = VT_UNKNOWN;
                            v.punkVal = (IUnknown *)st;
                        } else if (st) {
                            st->lpVtbl->Release(st);
                        }
                    }
                    if (SUCCEEDED(hr) && v.vt != VT_EMPTY)
                        hr = xmp_index_item(block, index++, &v, format.Data1 == GUID_MetadataFormatXMPAlt.Data1 ? (lang ? lang : L"x-default") : nullptr);
                    PropVariantClear(&v);
                }
                CoTaskMemFree(lang_copy);
            }
            if (SUCCEEDED(hr)) {
                /* up to the closing tag of the element. */
                while (x->p < x->end && *x->p != '<')
                    x->p++;
                Tag close;
                if (x->p < x->end)
                    read_tag(x, &close);
            }
            if (SUCCEEDED(hr) && block) {
                PROPVARIANT v;
                WicPvInit(&v);
                v.vt = VT_UNKNOWN;
                v.punkVal = (IUnknown *)block;
                hr = xmp_item(parent, schema, name, &v);
            }
            if (block)
                block->lpVtbl->Release(block);
            return hr;
        }
        if (name_is(&t, "rdf:Description") && !t.empty) {
            hr = WicMetaReaderCreate(&format, &block);
            continue;
        }
        if (!block)
            hr = WicMetaReaderCreate(&format, &block);
        if (SUCCEEDED(hr) && depth < MAX_DEPTH)
            hr = parse_property(x, &t, ns, block, depth + 1);
    }
    if (block)
        block->lpVtbl->Release(block);
    return hr;
}

static HRESULT parse_property(Xml *x, const Tag *open, NsMap *ns, IWICMetadataReader *parent, UINT depth)
{
    const char *colon = find_colon(open->name, open->name_length);
    if (!colon)
        return S_OK;
    const WCHAR *uri = ns_uri(ns, open->name, (SIZE_T)(colon - open->name));
    WCHAR *local = dup_utf8(colon + 1, open->name_length - (SIZE_T)(colon - open->name) - 1);
    if (!local)
        return E_OUTOFMEMORY;
    HRESULT hr;
    if (!uri) {
        /* an element in a namespace nobody declared is skipped with its content. */
        UINT level = open->empty ? 0 : 1;
        Tag t;
        while (level && x->p < x->end) {
            while (x->p < x->end && *x->p != '<')
                x->p++;
            if (x->p >= x->end || !read_tag(x, &t))
                break;
            if (t.special)
                continue;
            if (t.closing)
                level--;
            else if (!t.empty)
                level++;
        }
        hr = S_OK;
    } else {
        hr = parse_value(x, open, ns, parent, uri, local, depth);
    }
    CoTaskMemFree(local);
    return hr;
}

HRESULT WicXmpFromPacket(const BYTE *data, SIZE_T size, IWICMetadataReader **out)
{
    Xml x = {(const char *)data, (const char *)data + size};
    /* a byte order mark is skipped, utf-16 packets are not read. */
    if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
        x.p += 3;
    NsMap ns;
    memset(&ns, 0, sizeof(ns));
    IWICMetadataReader *xmp;
    HRESULT hr = WicMetaReaderCreate(&GUID_MetadataFormatXMP, &xmp);
    if (FAILED(hr))
        return hr;
    Tag t;
    BOOL found = FALSE;
    while (SUCCEEDED(hr) && x.p < x.end) {
        while (x.p < x.end && *x.p != '<')
            x.p++;
        if (x.p >= x.end)
            break;
        if (!read_tag(&x, &t)) {
            hr = WINCODEC_ERR_BADMETADATAHEADER;
            break;
        }
        if (t.special || t.closing)
            continue;
        ns_declare(&ns, &t);
        if (!name_is(&t, "rdf:Description"))
            continue;
        found = TRUE;
        /* simple properties are written as attributes. */
        for (UINT i = 0; i < t.attr_count && SUCCEEDED(hr); i++) {
            const Attr *a = &t.attrs[i];
            const char *colon = find_colon(a->name, a->name_length);
            if (!colon || (a->name_length > 5 && !memcmp(a->name, "xmlns", 5)) ||
                (colon - a->name == 3 && !memcmp(a->name, "rdf", 3)) || (colon - a->name == 3 && !memcmp(a->name, "xml", 3)))
                continue;
            const WCHAR *uri = ns_uri(&ns, a->name, (SIZE_T)(colon - a->name));
            if (!uri)
                continue;
            WCHAR *local = dup_utf8(colon + 1, a->name_length - (SIZE_T)(colon - a->name) - 1);
            WCHAR *text = decode_text(a->value, a->value_length);
            if (local && text)
                hr = xmp_text_item(xmp, uri, local, text);
            else {
                CoTaskMemFree(text);
                hr = E_OUTOFMEMORY;
            }
            CoTaskMemFree(local);
        }
        if (t.empty)
            continue;
        /* the child elements until the end of the description. */
        while (SUCCEEDED(hr) && x.p < x.end) {
            while (x.p < x.end && *x.p != '<')
                x.p++;
            if (x.p >= x.end)
                break;
            Tag c;
            if (!read_tag(&x, &c)) {
                hr = WINCODEC_ERR_BADMETADATAHEADER;
                break;
            }
            if (c.special)
                continue;
            if (c.closing) {
                if (name_is(&c, "rdf:Description"))
                    break;
                continue;
            }
            ns_declare(&ns, &c);
            hr = parse_property(&x, &c, &ns, xmp, 0);
        }
    }
    ns_free(&ns);
    if (FAILED(hr) || !found) {
        xmp->lpVtbl->Release(xmp);
        return FAILED(hr) ? hr : WINCODEC_ERR_BADMETADATAHEADER;
    }
    *out = xmp;
    return S_OK;
}

/* ------------------------------------------------------------------ writing */

typedef struct Out {
    char *data;
    SIZE_T size;
    SIZE_T capacity;
    BOOL failed;
} Out;

static void put(Out *o, const char *s, SIZE_T n)
{
    if (o->failed)
        return;
    if (o->size + n + 1 > o->capacity) {
        SIZE_T capacity = (o->capacity ? o->capacity * 2 : 4096);
        while (capacity < o->size + n + 1)
            capacity *= 2;
        char *bigger = WicRealloc(o->data, capacity);
        if (!bigger) {
            o->failed = TRUE;
            return;
        }
        o->data = bigger;
        o->capacity = capacity;
    }
    memcpy(o->data + o->size, s, n);
    o->size += n;
    o->data[o->size] = 0;
}

static void puts_(Out *o, const char *s) { put(o, s, strlen(s)); }

static void put_escaped(Out *o, const WCHAR *w)
{
    char tmp[8];
    for (; *w; w++) {
        UINT c = *w;
        if (c >= 0xd800 && c < 0xdc00 && w[1] >= 0xdc00 && w[1] < 0xe000) {
            c = 0x10000 + ((c & 0x3ff) << 10) + (w[1] & 0x3ff);
            w++;
        }
        switch (c) {
        case '<':
            puts_(o, "&lt;");
            continue;
        case '>':
            puts_(o, "&gt;");
            continue;
        case '&':
            puts_(o, "&amp;");
            continue;
        case '"':
            puts_(o, "&quot;");
            continue;
        default:
            break;
        }
        UINT n = 0;
        if (c < 0x80) {
            tmp[n++] = (char)c;
        } else if (c < 0x800) {
            tmp[n++] = (char)(0xc0 | (c >> 6));
            tmp[n++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            tmp[n++] = (char)(0xe0 | (c >> 12));
            tmp[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
            tmp[n++] = (char)(0x80 | (c & 0x3f));
        } else {
            tmp[n++] = (char)(0xf0 | (c >> 18));
            tmp[n++] = (char)(0x80 | ((c >> 12) & 0x3f));
            tmp[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
            tmp[n++] = (char)(0x80 | (c & 0x3f));
        }
        put(o, tmp, n);
    }
}

typedef struct NsOut {
    const WCHAR *uri[MAX_NS];
    WCHAR prefix[MAX_NS][12];
    UINT count;
} NsOut;

static const WCHAR *prefix_for(NsOut *n, const GUID *format, const WCHAR *uri)
{
    for (UINT i = 0; i < n->count; i++) {
        if (!wcscmp(n->uri[i], uri))
            return n->prefix[i];
    }
    if (n->count == MAX_NS)
        return nullptr;
    const WCHAR *known = WicSchemaPrefix(format, uri);
    UINT i = n->count++;
    n->uri[i] = uri;
    if (known && wcslen(known) < 11) {
        wcscpy(n->prefix[i], known);
    } else {
        wcscpy(n->prefix[i], L"ns");
        n->prefix[i][2] = (WCHAR)('0' + i / 10);
        n->prefix[i][3] = (WCHAR)('0' + i % 10);
        n->prefix[i][4] = 0;
    }
    return n->prefix[i];
}

static void put_wide_ascii(Out *o, const WCHAR *w)
{
    for (; *w; w++) {
        char c = (char)*w;
        put(o, &c, 1);
    }
}

static const WCHAR *as_text(const PROPVARIANT *v, WCHAR *buffer)
{
    if (v->vt == VT_LPWSTR)
        return v->pwszVal;
    if (v->vt == VT_LPSTR) {
        SIZE_T i = 0;
        for (; v->pszVal && v->pszVal[i] && i < 255; i++)
            buffer[i] = (BYTE)v->pszVal[i];
        buffer[i] = 0;
        return buffer;
    }
    return nullptr;
}

static void write_value(Out *o, NsOut *ns, const PROPVARIANT *v, UINT depth);

static void write_block_children(Out *o, NsOut *ns, IWICMetadataReader *block, UINT depth)
{
    GUID f;
    block->lpVtbl->GetMetadataFormat(block, &f);
    UINT n = WicMetaCount(block);
    BOOL is_alt = IsEqualGUID(&f, &GUID_MetadataFormatXMPAlt);
    BOOL collection = IsEqualGUID(&f, &GUID_MetadataFormatXMPBag) || IsEqualGUID(&f, &GUID_MetadataFormatXMPSeq) || is_alt;
    if (collection) {
        puts_(o, IsEqualGUID(&f, &GUID_MetadataFormatXMPBag) ? "<rdf:Bag>" : IsEqualGUID(&f, &GUID_MetadataFormatXMPSeq) ? "<rdf:Seq>" : "<rdf:Alt>");
        for (UINT i = 0; i < n; i++) {
            const WicMetaItem *item = WicMetaItemAt(block, i);
            if (is_alt && item->id.vt == VT_LPWSTR) {
                puts_(o, "<rdf:li xml:lang=\"");
                put_escaped(o, item->id.pwszVal);
                puts_(o, "\">");
            } else {
                puts_(o, "<rdf:li>");
            }
            write_value(o, ns, &item->value, depth + 1);
            puts_(o, "</rdf:li>");
        }
        puts_(o, IsEqualGUID(&f, &GUID_MetadataFormatXMPBag) ? "</rdf:Bag>" : IsEqualGUID(&f, &GUID_MetadataFormatXMPSeq) ? "</rdf:Seq>" : "</rdf:Alt>");
        return;
    }
    puts_(o, "<rdf:Description>");
    for (UINT i = 0; i < n; i++) {
        const WicMetaItem *item = WicMetaItemAt(block, i);
        if (item->schema.vt != VT_LPWSTR || item->id.vt != VT_LPWSTR)
            continue;
        const WCHAR *prefix = prefix_for(ns, &GUID_MetadataFormatXMP, item->schema.pwszVal);
        if (!prefix)
            continue;
        puts_(o, "<");
        put_wide_ascii(o, prefix);
        puts_(o, ":");
        put_wide_ascii(o, item->id.pwszVal);
        puts_(o, ">");
        write_value(o, ns, &item->value, depth + 1);
        puts_(o, "</");
        put_wide_ascii(o, prefix);
        puts_(o, ":");
        put_wide_ascii(o, item->id.pwszVal);
        puts_(o, ">");
    }
    puts_(o, "</rdf:Description>");
}

static void write_value(Out *o, NsOut *ns, const PROPVARIANT *v, UINT depth)
{
    WCHAR tmp[256];
    if (v->vt == VT_UNKNOWN && v->punkVal && depth < MAX_DEPTH) {
        IWICMetadataReader *block = nullptr;
        if (SUCCEEDED(v->punkVal->lpVtbl->QueryInterface(v->punkVal, &IID_IWICMetadataReader, (void **)&block))) {
            write_block_children(o, ns, block, depth);
            block->lpVtbl->Release(block);
        }
        return;
    }
    const WCHAR *text = as_text(v, tmp);
    if (text) {
        put_escaped(o, text);
        return;
    }
    /* numbers are written as they print. */
    char digits[24];
    unsigned long long u = 0;
    BOOL neg = FALSE;
    switch (v->vt) {
    case VT_UI1:
        u = v->bVal;
        break;
    case VT_UI2:
        u = v->uiVal;
        break;
    case VT_UI4:
        u = v->ulVal;
        break;
    case VT_UI8:
        u = v->uhVal.QuadPart;
        break;
    case VT_I1:
        neg = (signed char)v->bVal < 0;
        u = neg ? (unsigned long long)(-(long long)(signed char)v->bVal) : v->bVal;
        break;
    case VT_I2:
        neg = (short)v->uiVal < 0;
        u = neg ? (unsigned long long)(-(long long)(short)v->uiVal) : v->uiVal;
        break;
    case VT_I4:
        neg = (int)v->ulVal < 0;
        u = neg ? (unsigned long long)(-(long long)(int)v->ulVal) : v->ulVal;
        break;
    case VT_BOOL:
        puts_(o, v->boolVal ? "True" : "False");
        return;
    default:
        return;
    }
    int n = 0;
    do {
        digits[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg)
        put(o, "-", 1);
    while (n)
        put(o, &digits[--n], 1);
}

HRESULT WicXmpToPacket(IWICMetadataReader *xmp, BOOL jpeg_header, BYTE **out, SIZE_T *size)
{
    Out o = {0};
    Out body = {0};
    NsOut ns;
    memset(&ns, 0, sizeof(ns));
    UINT n = WicMetaCount(xmp);
    /* the properties of the top level go into one description, grouped by nothing, each in its schema. */
    for (UINT i = 0; i < n; i++) {
        const WicMetaItem *item = WicMetaItemAt(xmp, i);
        if (item->schema.vt != VT_LPWSTR || item->id.vt != VT_LPWSTR)
            continue;
        const WCHAR *prefix = prefix_for(&ns, &GUID_MetadataFormatXMP, item->schema.pwszVal);
        if (!prefix)
            continue;
        puts_(&body, "<");
        put_wide_ascii(&body, prefix);
        puts_(&body, ":");
        put_wide_ascii(&body, item->id.pwszVal);
        puts_(&body, ">");
        write_value(&body, &ns, &item->value, 0);
        puts_(&body, "</");
        put_wide_ascii(&body, prefix);
        puts_(&body, ":");
        put_wide_ascii(&body, item->id.pwszVal);
        puts_(&body, ">");
    }
    if (jpeg_header)
        puts_(&o, "http://ns.adobe.com/xap/1.0/");
    if (jpeg_header)
        put(&o, "\0", 1);
    puts_(&o, "<?xpacket begin=\"\xef\xbb\xbf\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
              "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\"");
    for (UINT i = 0; i < ns.count; i++) {
        puts_(&o, " xmlns:");
        put_wide_ascii(&o, ns.prefix[i]);
        puts_(&o, "=\"");
        put_escaped(&o, ns.uri[i]);
        puts_(&o, "\"");
    }
    puts_(&o, ">");
    put(&o, body.data ? body.data : "", body.size);
    puts_(&o, "</rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>");
    WicFree(body.data);
    if (o.failed) {
        WicFree(o.data);
        return E_OUTOFMEMORY;
    }
    *out = (BYTE *)o.data;
    *size = o.size;
    return S_OK;
}
