/*
 * the reader of xmllite.dll: it takes the bytes of a stream whole, turns them into text by the encoding that a signature,
 * the declaration or the program names, and walks the text one node at a time. entities are the five predefined ones and
 * character references, a document type is refused unless the program asks for it, and namespaces are resolved as the
 * elements and attributes come.
 */
#include "xmllite.h"

#define WC_XML_NAMESPACE L"http://www.w3.org/XML/1998/namespace"
#define WC_XMLNS_NAMESPACE L"http://www.w3.org/2000/xmlns/"

typedef struct ATTRIBUTE {
    WCHAR *qname, *prefix, *local, *uri, *value;
    ULONG qlen, plen, llen, ulen, vlen;
} ATTRIBUTE;

typedef struct ELEMENT {
    WCHAR *qname;
    ULONG qlen;
    ULONG bindings;
} ELEMENT;

typedef struct BINDING {
    WCHAR *prefix, *uri;
    ULONG plen;
} BINDING;

typedef struct READER {
    const IXmlReaderVtbl *lpVtbl;
    LONG references;
    ISequentialStream *source;
    UINT code_page;
    BOOL code_page_forced;
    WCHAR *base_uri;
    WCHAR *text;
    SIZE_T length, position;
    BOOLEAN loaded, finished, root_seen, root_closed, declaration_seen, detected_document;
    HRESULT error;
    ULONG read_state;
    LONG conformance, dtd_processing, max_depth;
    LONG_PTR multi_language, random_access, resolver;

    XmlNodeType type, owner_type;
    WCHAR *qname, *local, *prefix, *uri, *value;
    ULONG qlen, llen, plen, ulen, vlen;
    BOOLEAN empty, pending_pop;
    ULONG depth;
    UINT line, column;
    SIZE_T counted;
    UINT counted_line, counted_column;
    SIZE_T chunk;

    ATTRIBUTE *attributes;
    ULONG attribute_count, attribute_capacity;
    LONG current_attribute;

    ELEMENT *stack;
    ULONG stack_count, stack_capacity;
    BINDING *bindings;
    ULONG binding_count, binding_capacity;

    WCHAR **blocks;
    ULONG block_count, block_capacity;
} READER;

static const WCHAR g_empty[] = L"";

/* ------------------------------------------------------------------ memory of a node */

static WCHAR *node_string(READER *r, const WCHAR *text, SIZE_T length)
{
    if (r->block_count == r->block_capacity) {
        ULONG capacity = r->block_capacity ? r->block_capacity * 2 : 32;
        WCHAR **blocks = XmlRealloc(r->blocks, capacity * sizeof(*blocks));
        if (!blocks)
            return nullptr;
        r->blocks = blocks;
        r->block_capacity = capacity;
    }
    WCHAR *copy = XmlDuplicate(text, length);
    if (copy)
        r->blocks[r->block_count++] = copy;
    return copy;
}

static void forget_node(READER *r)
{
    for (ULONG i = 0; i < r->block_count; i++)
        XmlFree(r->blocks[i]);
    r->block_count = 0;
    r->attribute_count = 0;
    r->current_attribute = -1;
    r->qname = r->local = r->prefix = r->uri = r->value = (WCHAR *)g_empty;
    r->qlen = r->llen = r->plen = r->ulen = r->vlen = 0;
    r->empty = FALSE;
    r->chunk = 0;
}

static void release_input(READER *r)
{
    if (r->source)
        r->source->lpVtbl->Release(r->source);
    r->source = nullptr;
    XmlFree(r->text);
    r->text = nullptr;
    XmlFree(r->base_uri);
    r->base_uri = nullptr;
    r->length = r->position = 0;
}

static void pop_element(READER *r)
{
    ELEMENT *element = &r->stack[--r->stack_count];
    while (r->binding_count > element->bindings) {
        BINDING *b = &r->bindings[--r->binding_count];
        XmlFree(b->prefix);
        XmlFree(b->uri);
    }
    XmlFree(element->qname);
    if (!r->stack_count)
        r->root_closed = TRUE;
}

static void reset_reader(READER *r)
{
    forget_node(r);
    while (r->stack_count)
        pop_element(r);
    release_input(r);
    r->loaded = r->finished = r->root_seen = r->root_closed = r->declaration_seen = r->detected_document = FALSE;
    r->pending_pop = FALSE;
    r->error = S_OK;
    r->read_state = XmlReadState_Initial;
    r->type = r->owner_type = XmlNodeType_None;
    r->depth = 0;
    r->line = r->column = 0;
    r->counted = 0;
    r->counted_line = r->counted_column = 1;
    r->code_page = 0;
    r->code_page_forced = FALSE;
}

/* ------------------------------------------------------------------ decoding of the bytes */

static HRESULT read_all(ISequentialStream *source, BYTE **data, SIZE_T *size)
{
    SIZE_T capacity = 4096, used = 0;
    BYTE *bytes = XmlAlloc(capacity);
    if (!bytes)
        return E_OUTOFMEMORY;
    for (;;) {
        if (used == capacity) {
            BYTE *larger = XmlRealloc(bytes, capacity * 2);
            if (!larger) {
                XmlFree(bytes);
                return E_OUTOFMEMORY;
            }
            bytes = larger;
            capacity *= 2;
        }
        ULONG got = 0;
        HRESULT hr = source->lpVtbl->Read(source, bytes + used, (ULONG)(capacity - used), &got);
        used += got;
        if (FAILED(hr)) {
            XmlFree(bytes);
            return hr;
        }
        if (!got || hr == S_FALSE)
            break;
    }
    *data = bytes;
    *size = used;
    return S_OK;
}

/* the encoding named in the declaration, which is ascii whatever the 8 bit encoding is. */
static UINT declared_code_page(const BYTE *bytes, SIZE_T size)
{
    if (size < 6 || memcmp(bytes, "<?xml", 5) != 0)
        return 0;
    SIZE_T end = 0;
    while (end + 1 < size && !(bytes[end] == '?' && bytes[end + 1] == '>') && end < 200)
        end++;
    for (SIZE_T i = 5; i + 8 < end; i++) {
        if (memcmp(bytes + i, "encoding", 8) != 0)
            continue;
        SIZE_T k = i + 8;
        while (k < end && (bytes[k] == ' ' || bytes[k] == '='))
            k++;
        if (k >= end || (bytes[k] != '"' && bytes[k] != '\''))
            return 0;
        BYTE quote = bytes[k++];
        WCHAR name[40];
        SIZE_T n = 0;
        while (k < end && bytes[k] != quote && n < ARRAYSIZE(name) - 1)
            name[n++] = bytes[k++];
        return XmlCodePageOfName(name, n);
    }
    return 0;
}

static HRESULT decode_utf8(const BYTE *in, SIZE_T size, WCHAR **text, SIZE_T *length)
{
    WCHAR *out = XmlAlloc((size + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    SIZE_T n = 0;
    for (SIZE_T i = 0; i < size;) {
        UCHAR c = in[i];
        ULONG code, extra;
        if (c < 0x80) {
            code = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            code = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            code = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            code = c & 0x07;
            extra = 3;
        } else {
            XmlFree(out);
            return MX_E_ENCODING;
        }
        if (i + extra >= size + (extra ? 0 : 1) && extra) {
            XmlFree(out);
            return MX_E_ENCODING;
        }
        for (ULONG k = 1; k <= extra; k++) {
            if (i + k >= size || (in[i + k] & 0xC0) != 0x80) {
                XmlFree(out);
                return MX_E_ENCODING;
            }
            code = (code << 6) | (in[i + k] & 0x3F);
        }
        i += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            out[n++] = (WCHAR)(0xD800 + (code >> 10));
            out[n++] = (WCHAR)(0xDC00 + (code & 0x3FF));
        } else {
            out[n++] = (WCHAR)code;
        }
    }
    *text = out;
    *length = n;
    return S_OK;
}

static HRESULT decode_utf16(const BYTE *in, SIZE_T size, BOOLEAN big_endian, WCHAR **text, SIZE_T *length)
{
    SIZE_T count = size / 2;
    WCHAR *out = XmlAlloc((count + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    for (SIZE_T i = 0; i < count; i++)
        out[i] = big_endian ? (WCHAR)((in[2 * i] << 8) | in[2 * i + 1]) : (WCHAR)(in[2 * i] | (in[2 * i + 1] << 8));
    *text = out;
    *length = count;
    return S_OK;
}

static HRESULT decode_input(READER *r, BYTE *bytes, SIZE_T size)
{
    UINT page = r->code_page;
    BOOLEAN signature = FALSE;
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        bytes += 3;
        size -= 3;
        signature = TRUE;
        if (!page || !r->code_page_forced)
            page = 65001;
    } else if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        bytes += 2;
        size -= 2;
        signature = TRUE;
        page = 1200;
    } else if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        bytes += 2;
        size -= 2;
        signature = TRUE;
        page = 1201;
    }
    if (!signature && (!page || !r->code_page_forced)) {
        if (size >= 4 && bytes[0] == '<' && bytes[1] == 0 && bytes[2] == '?' && bytes[3] == 0)
            page = 1200;
        else if (size >= 4 && bytes[0] == 0 && bytes[1] == '<' && bytes[2] == 0 && bytes[3] == '?')
            page = 1201;
        else {
            UINT declared = declared_code_page(bytes, size);
            if (declared == 1200 || declared == 1201)
                return MX_E_ENCODINGSWITCH;
            page = declared ? declared : (page ? page : 65001);
        }
    }
    WCHAR *text = nullptr;
    SIZE_T length = 0;
    HRESULT hr;
    if (page == 65001)
        hr = decode_utf8(bytes, size, &text, &length);
    else if (page == 1200 || page == 1201)
        hr = decode_utf16(bytes, size, page == 1201, &text, &length);
    else {
        text = XmlAlloc((size + 1) * sizeof(WCHAR));
        if (!text)
            return E_OUTOFMEMORY;
        int n = size ? MultiByteToWideChar(page, 0, (const char *)bytes, (int)size, text, (int)size) : 0;
        if (size && n <= 0) {
            XmlFree(text);
            return MX_E_ENCODING;
        }
        length = (SIZE_T)n;
        hr = S_OK;
    }
    if (FAILED(hr))
        return hr;
    r->text = text;
    r->length = length;
    r->position = 0;
    return S_OK;
}

static HRESULT load_input(READER *r)
{
    if (r->loaded)
        return S_OK;
    if (!r->source)
        return E_PENDING;
    BYTE *bytes;
    SIZE_T size;
    HRESULT hr = read_all(r->source, &bytes, &size);
    if (FAILED(hr))
        return hr;
    hr = decode_input(r, bytes, size);
    XmlFree(bytes);
    if (SUCCEEDED(hr))
        r->loaded = TRUE;
    return hr;
}

/* ------------------------------------------------------------------ text scanning */

static BOOLEAN is_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static BOOLEAN is_name_start(WCHAR c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || c >= 0xC0;
}

static BOOLEAN is_name_char(WCHAR c)
{
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == 0xB7;
}

static BOOLEAN is_xml_char(WCHAR c)
{
    return c >= 0x20 || c == '\t' || c == '\n' || c == '\r';
}

static BOOLEAN is_document(const READER *r)
{
    return r->conformance == XmlConformanceLevel_Document || (r->conformance == XmlConformanceLevel_Auto && r->detected_document);
}

/* the target names that are kept for the declaration, whatever the case. */
static BOOLEAN is_reserved_name(const WCHAR *name)
{
    return (name[0] | 0x20) == 'x' && (name[1] | 0x20) == 'm' && (name[2] | 0x20) == 'l';
}

static BOOLEAN looking_at(const READER *r, const WCHAR *what)
{
    SIZE_T n = wcslen(what);
    return r->position + n <= r->length && memcmp(r->text + r->position, what, n * sizeof(WCHAR)) == 0;
}

static void skip_spaces(READER *r)
{
    while (r->position < r->length && is_space(r->text[r->position]))
        r->position++;
}

/* the line and column of the position, counted from where the last node was counted. */
static void count_to(READER *r, SIZE_T target)
{
    while (r->counted < target) {
        if (r->text[r->counted] == '\n') {
            r->counted_line++;
            r->counted_column = 1;
        } else {
            r->counted_column++;
        }
        r->counted++;
    }
}

static HRESULT scan_name(READER *r, SIZE_T *start, SIZE_T *length)
{
    if (r->position >= r->length || !is_name_start(r->text[r->position]))
        return r->position >= r->length ? MX_E_INPUTEND : WC_E_NAME;
    *start = r->position;
    while (r->position < r->length && is_name_char(r->text[r->position]))
        r->position++;
    *length = r->position - *start;
    return S_OK;
}

/* the text between the positions with the entities expanded and the line ends or the white space normalized. */
static HRESULT expand_text(READER *r, SIZE_T from, SIZE_T to, BOOLEAN attribute, WCHAR **result, ULONG *result_length)
{
    WCHAR *out = XmlAlloc((to - from + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    SIZE_T n = 0;
    for (SIZE_T i = from; i < to; i++) {
        WCHAR c = r->text[i];
        if (!is_xml_char(c)) {
            XmlFree(out);
            return WC_E_XMLCHARACTER;
        }
        if (c == '\r') {
            if (i + 1 < to && r->text[i + 1] == '\n')
                i++;
            out[n++] = attribute ? ' ' : '\n';
        } else if (attribute && (c == '\t' || c == '\n')) {
            out[n++] = ' ';
        } else if (c == '&') {
            SIZE_T end = i + 1;
            while (end < to && r->text[end] != ';')
                end++;
            if (end >= to) {
                XmlFree(out);
                return WC_E_SEMICOLON;
            }
            const WCHAR *name = r->text + i + 1;
            SIZE_T length = end - i - 1;
            if (length >= 2 && name[0] == '#') {
                ULONG code = 0;
                BOOLEAN hex = name[1] == 'x';
                if (hex && length < 3) {
                    XmlFree(out);
                    return WC_E_HEXDIGIT;
                }
                for (SIZE_T k = hex ? 2 : 1; k < length; k++) {
                    WCHAR d = name[k];
                    ULONG digit;
                    if (d >= '0' && d <= '9')
                        digit = d - '0';
                    else if (hex && d >= 'a' && d <= 'f')
                        digit = d - 'a' + 10;
                    else if (hex && d >= 'A' && d <= 'F')
                        digit = d - 'A' + 10;
                    else {
                        XmlFree(out);
                        return hex ? WC_E_HEXDIGIT : WC_E_DIGIT;
                    }
                    code = code * (hex ? 16 : 10) + digit;
                    if (code > 0x10FFFF) {
                        XmlFree(out);
                        return WC_E_XMLCHARACTER;
                    }
                }
                if (!code || (code >= 0xD800 && code < 0xE000) || code == 0xFFFE || code == 0xFFFF ||
                    (code < 0x20 && code != '\t' && code != '\n' && code != '\r')) {
                    XmlFree(out);
                    return WC_E_XMLCHARACTER;
                }
                if (code >= 0x10000) {
                    code -= 0x10000;
                    out[n++] = (WCHAR)(0xD800 + (code >> 10));
                    out[n++] = (WCHAR)(0xDC00 + (code & 0x3FF));
                } else {
                    out[n++] = (WCHAR)code;
                }
            } else if (length == 2 && name[0] == 'l' && name[1] == 't') {
                out[n++] = '<';
            } else if (length == 2 && name[0] == 'g' && name[1] == 't') {
                out[n++] = '>';
            } else if (length == 3 && name[0] == 'a' && name[1] == 'm' && name[2] == 'p') {
                out[n++] = '&';
            } else if (length == 4 && name[0] == 'a' && name[1] == 'p' && name[2] == 'o' && name[3] == 's') {
                out[n++] = '\'';
            } else if (length == 4 && name[0] == 'q' && name[1] == 'u' && name[2] == 'o' && name[3] == 't') {
                out[n++] = '"';
            } else {
                XmlFree(out);
                return length ? WC_E_UNDECLAREDENTITY : WC_E_NAME;
            }
            i = end;
        } else {
            out[n++] = c;
        }
    }
    *result = out;
    *result_length = (ULONG)n;
    return S_OK;
}

/* a string of the node that has the characters between the positions after the expansion. */
static HRESULT node_text(READER *r, SIZE_T from, SIZE_T to, BOOLEAN attribute, WCHAR **result, ULONG *length)
{
    WCHAR *expanded;
    HRESULT hr = expand_text(r, from, to, attribute, &expanded, length);
    if (FAILED(hr))
        return hr;
    *result = node_string(r, expanded, *length);
    XmlFree(expanded);
    return *result ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ namespaces */

static const WCHAR *find_namespace(const READER *r, const WCHAR *prefix, ULONG length)
{
    if (length == 3 && memcmp(prefix, L"xml", 3 * sizeof(WCHAR)) == 0)
        return WC_XML_NAMESPACE;
    if (length == 5 && memcmp(prefix, L"xmlns", 5 * sizeof(WCHAR)) == 0)
        return WC_XMLNS_NAMESPACE;
    for (ULONG i = r->binding_count; i > 0; i--) {
        const BINDING *b = &r->bindings[i - 1];
        if (b->plen == length && memcmp(b->prefix, prefix, length * sizeof(WCHAR)) == 0)
            return b->uri;
    }
    return nullptr;
}

static HRESULT push_binding(READER *r, const WCHAR *prefix, ULONG plen, const WCHAR *uri, ULONG ulen)
{
    if (r->binding_count == r->binding_capacity) {
        ULONG capacity = r->binding_capacity ? r->binding_capacity * 2 : 16;
        BINDING *larger = XmlRealloc(r->bindings, capacity * sizeof(*larger));
        if (!larger)
            return E_OUTOFMEMORY;
        r->bindings = larger;
        r->binding_capacity = capacity;
    }
    BINDING *b = &r->bindings[r->binding_count];
    b->prefix = XmlDuplicate(prefix, plen);
    b->uri = XmlDuplicate(uri, ulen);
    b->plen = plen;
    if (!b->prefix || !b->uri) {
        XmlFree(b->prefix);
        XmlFree(b->uri);
        return E_OUTOFMEMORY;
    }
    r->binding_count++;
    return S_OK;
}

/* the position of the colon of a qualified name, which may be only one. */
static HRESULT split_name(const WCHAR *name, ULONG length, ULONG *colon)
{
    *colon = (ULONG)-1;
    for (ULONG i = 0; i < length; i++) {
        if (name[i] != ':')
            continue;
        if (*colon != (ULONG)-1)
            return NC_E_QNAMECOLON;
        *colon = i;
    }
    if (*colon != (ULONG)-1 && (*colon == 0 || *colon + 1 == length))
        return NC_E_NAMECOLON;
    return S_OK;
}

/* ------------------------------------------------------------------ nodes */

static HRESULT add_attribute(READER *r, const WCHAR *qname, ULONG qlen, WCHAR *value, ULONG vlen)
{
    if (r->attribute_count == r->attribute_capacity) {
        ULONG capacity = r->attribute_capacity ? r->attribute_capacity * 2 : 8;
        ATTRIBUTE *larger = XmlRealloc(r->attributes, capacity * sizeof(*larger));
        if (!larger)
            return E_OUTOFMEMORY;
        r->attributes = larger;
        r->attribute_capacity = capacity;
    }
    ATTRIBUTE *a = &r->attributes[r->attribute_count];
    memset(a, 0, sizeof(*a));
    a->qname = node_string(r, qname, qlen);
    a->value = value;
    a->qlen = qlen;
    a->vlen = vlen;
    a->prefix = a->local = a->uri = (WCHAR *)g_empty;
    if (!a->qname)
        return E_OUTOFMEMORY;
    r->attribute_count++;
    return S_OK;
}

static HRESULT set_name(READER *r, const WCHAR *qname, ULONG qlen, ULONG colon, const WCHAR *uri)
{
    r->qname = node_string(r, qname, qlen);
    if (!r->qname)
        return E_OUTOFMEMORY;
    r->qlen = qlen;
    if (colon == (ULONG)-1) {
        r->local = r->qname;
        r->llen = qlen;
        r->prefix = (WCHAR *)g_empty;
        r->plen = 0;
    } else {
        r->prefix = r->qname;
        r->plen = colon;
        r->local = r->qname + colon + 1;
        r->llen = qlen - colon - 1;
        WCHAR *prefix = node_string(r, qname, colon);
        if (!prefix)
            return E_OUTOFMEMORY;
        r->prefix = prefix;
    }
    r->uri = (WCHAR *)(uri ? uri : g_empty);
    r->ulen = (ULONG)wcslen(r->uri);
    return S_OK;
}

/* the characters between the positions as they are, with the line ends normalized. */
static HRESULT raw_text(READER *r, SIZE_T from, SIZE_T to, WCHAR **result, ULONG *length)
{
    WCHAR *copy = XmlAlloc((to - from + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    SIZE_T n = 0;
    for (SIZE_T i = from; i < to; i++) {
        if (!is_xml_char(r->text[i])) {
            XmlFree(copy);
            return WC_E_XMLCHARACTER;
        }
        if (r->text[i] == '\r') {
            if (i + 1 < to && r->text[i + 1] == '\n')
                i++;
            copy[n++] = '\n';
        } else {
            copy[n++] = r->text[i];
        }
    }
    *result = node_string(r, copy, n);
    XmlFree(copy);
    *length = (ULONG)n;
    return *result ? S_OK : E_OUTOFMEMORY;
}

static HRESULT parse_comment(READER *r)
{
    r->position += 4;
    SIZE_T start = r->position;
    while (r->position + 3 <= r->length) {
        if (r->text[r->position] == '-' && r->text[r->position + 1] == '-') {
            if (r->text[r->position + 2] != '>')
                return WC_E_COMMENT;
            ULONG length;
            HRESULT hr = raw_text(r, start, r->position, &r->value, &length);
            if (FAILED(hr))
                return hr;
            r->vlen = length;
            r->position += 3;
            r->type = XmlNodeType_Comment;
            return S_OK;
        }
        r->position++;
    }
    return MX_E_INPUTEND;
}

static HRESULT parse_cdata(READER *r)
{
    r->position += 9;
    SIZE_T start = r->position;
    while (r->position + 3 <= r->length) {
        if (r->text[r->position] == ']' && r->text[r->position + 1] == ']' && r->text[r->position + 2] == '>') {
            HRESULT hr = raw_text(r, start, r->position, &r->value, &r->vlen);
            if (FAILED(hr))
                return hr;
            r->position += 3;
            r->type = XmlNodeType_CDATA;
            return S_OK;
        }
        r->position++;
    }
    return MX_E_INPUTEND;
}

static HRESULT parse_declaration_attributes(READER *r, SIZE_T end)
{
    for (;;) {
        SIZE_T before = r->position;
        skip_spaces(r);
        if (r->position >= end)
            return S_OK;
        if (r->position == before)
            return WC_E_WHITESPACE;
        SIZE_T name, length;
        HRESULT hr = scan_name(r, &name, &length);
        if (FAILED(hr))
            return WC_E_XMLDECL;
        skip_spaces(r);
        if (r->position >= end || r->text[r->position] != '=')
            return WC_E_EQUAL;
        r->position++;
        skip_spaces(r);
        if (r->position >= end || (r->text[r->position] != '"' && r->text[r->position] != '\''))
            return WC_E_QUOTE;
        WCHAR quote = r->text[r->position++];
        SIZE_T value = r->position;
        while (r->position < end && r->text[r->position] != quote)
            r->position++;
        if (r->position >= end)
            return WC_E_QUOTE;
        WCHAR *text = node_string(r, r->text + value, r->position - value);
        if (!text)
            return E_OUTOFMEMORY;
        r->position++;
        hr = add_attribute(r, r->text + name, (ULONG)length, text, (ULONG)(r->position - 1 - value));
        if (FAILED(hr))
            return hr;
        ATTRIBUTE *a = &r->attributes[r->attribute_count - 1];
        a->local = a->qname;
        a->llen = a->qlen;
    }
}

static HRESULT parse_instruction(READER *r)
{
    SIZE_T from = r->position;
    r->position += 2;
    SIZE_T name, length;
    HRESULT hr = scan_name(r, &name, &length);
    if (FAILED(hr))
        return hr == MX_E_INPUTEND ? hr : WC_E_PI;
    BOOLEAN is_xml = length == 3 && memcmp(r->text + name, L"xml", 3 * sizeof(WCHAR)) == 0;
    SIZE_T end = r->position;
    while (end + 2 <= r->length && !(r->text[end] == '?' && r->text[end + 1] == '>'))
        end++;
    if (end + 2 > r->length)
        return MX_E_INPUTEND;
    if (is_xml) {
        if (from != 0 || r->declaration_seen)
            return WC_E_LEADINGXML;
        if (r->position < end && !is_space(r->text[r->position]))
            return WC_E_XMLDECL;
        r->declaration_seen = TRUE;
        SIZE_T content = r->position;
        while (content < end && is_space(r->text[content]))
            content++;
        r->value = node_string(r, r->text + content, end - content);
        if (!r->value)
            return E_OUTOFMEMORY;
        r->vlen = (ULONG)(end - content);
        hr = parse_declaration_attributes(r, end);
        if (FAILED(hr))
            return hr;
        r->position = end + 2;
        r->type = XmlNodeType_XmlDeclaration;
        r->qname = r->local = node_string(r, L"xml", 3);
        r->qlen = r->llen = 3;
        if (!r->local)
            return E_OUTOFMEMORY;
        r->detected_document = TRUE;
        return S_OK;
    }
    if (length == 3 && is_reserved_name(r->text + name))
        return WC_E_LEADINGXML;
    r->local = r->qname = node_string(r, r->text + name, length);
    r->qlen = r->llen = (ULONG)length;
    SIZE_T content = r->position;
    if (content < end && !is_space(r->text[content]))
        return WC_E_PI;
    while (content < end && is_space(r->text[content]))
        content++;
    r->value = node_string(r, r->text + content, end - content);
    if (!r->local || !r->value)
        return E_OUTOFMEMORY;
    r->vlen = (ULONG)(end - content);
    r->position = end + 2;
    r->type = XmlNodeType_ProcessingInstruction;
    return S_OK;
}

static HRESULT parse_doctype(READER *r)
{
    if (r->dtd_processing == DtdProcessing_Prohibit)
        return WC_E_DTDPROHIBITED;
    if (r->root_seen || r->stack_count)
        return WC_E_DECLDOCTYPE;
    r->position += 9;
    skip_spaces(r);
    SIZE_T name, length;
    HRESULT hr = scan_name(r, &name, &length);
    if (FAILED(hr))
        return hr;
    r->local = r->qname = node_string(r, r->text + name, length);
    r->qlen = r->llen = (ULONG)length;
    if (!r->local)
        return E_OUTOFMEMORY;
    SIZE_T subset = 0, subset_end = 0;
    WCHAR quote = 0;
    LONG brackets = 0;
    while (r->position < r->length) {
        WCHAR c = r->text[r->position];
        if (quote) {
            if (c == quote)
                quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[') {
            if (!brackets++)
                subset = r->position + 1;
        } else if (c == ']') {
            if (--brackets == 0)
                subset_end = r->position;
        } else if (c == '>' && !brackets) {
            r->value = node_string(r, subset_end > subset ? r->text + subset : g_empty, subset_end > subset ? subset_end - subset : 0);
            if (!r->value)
                return E_OUTOFMEMORY;
            r->vlen = (ULONG)(subset_end > subset ? subset_end - subset : 0);
            r->position++;
            r->type = XmlNodeType_DocumentType;
            r->detected_document = TRUE;
            return S_OK;
        }
        r->position++;
    }
    return MX_E_INPUTEND;
}

static HRESULT parse_end_tag(READER *r)
{
    r->position += 2;
    SIZE_T name, length;
    HRESULT hr = scan_name(r, &name, &length);
    if (FAILED(hr))
        return hr;
    skip_spaces(r);
    if (r->position >= r->length)
        return MX_E_INPUTEND;
    if (r->text[r->position] != '>')
        return WC_E_GREATERTHAN;
    r->position++;
    if (!r->stack_count)
        return WC_E_ELEMENTMATCH;
    ELEMENT *top = &r->stack[r->stack_count - 1];
    if (top->qlen != length || memcmp(top->qname, r->text + name, length * sizeof(WCHAR)) != 0)
        return WC_E_ELEMENTMATCH;
    ULONG colon;
    hr = split_name(top->qname, top->qlen, &colon);
    if (FAILED(hr))
        return hr;
    const WCHAR *uri = find_namespace(r, colon == (ULONG)-1 ? g_empty : top->qname, colon == (ULONG)-1 ? 0 : colon);
    hr = set_name(r, top->qname, top->qlen, colon, uri);
    if (FAILED(hr))
        return hr;
    pop_element(r);
    r->depth = r->stack_count;
    r->type = XmlNodeType_EndElement;
    return S_OK;
}

static HRESULT parse_start_tag(READER *r)
{
    if (r->root_closed && is_document(r))
        return WC_E_ROOTELEMENT;
    if (r->stack_count >= (ULONG)r->max_depth)
        return SC_E_MAXELEMENTDEPTH;
    r->position++;
    SIZE_T name, length;
    HRESULT hr = scan_name(r, &name, &length);
    if (FAILED(hr))
        return hr;
    for (;;) {
        SIZE_T before = r->position;
        skip_spaces(r);
        if (r->position >= r->length)
            return MX_E_INPUTEND;
        WCHAR c = r->text[r->position];
        if (c == '>' || c == '/')
            break;
        if (r->position == before)
            return WC_E_WHITESPACE;
        SIZE_T attribute, attribute_length;
        hr = scan_name(r, &attribute, &attribute_length);
        if (FAILED(hr))
            return hr;
        skip_spaces(r);
        if (r->position >= r->length)
            return MX_E_INPUTEND;
        if (r->text[r->position] != '=')
            return WC_E_EQUAL;
        r->position++;
        skip_spaces(r);
        if (r->position >= r->length)
            return MX_E_INPUTEND;
        WCHAR quote = r->text[r->position];
        if (quote != '"' && quote != '\'')
            return WC_E_QUOTE;
        SIZE_T value = ++r->position;
        while (r->position < r->length && r->text[r->position] != quote) {
            if (r->text[r->position] == '<')
                return WC_E_LESSTHAN;
            r->position++;
        }
        if (r->position >= r->length)
            return MX_E_INPUTEND;
        WCHAR *text;
        ULONG text_length;
        hr = node_text(r, value, r->position, TRUE, &text, &text_length);
        if (FAILED(hr))
            return hr;
        r->position++;
        for (ULONG i = 0; i < r->attribute_count; i++)
            if (r->attributes[i].qlen == attribute_length &&
                memcmp(r->attributes[i].qname, r->text + attribute, attribute_length * sizeof(WCHAR)) == 0)
                return WC_E_UNIQUEATTRIBUTE;
        hr = add_attribute(r, r->text + attribute, (ULONG)attribute_length, text, text_length);
        if (FAILED(hr))
            return hr;
    }
    BOOLEAN empty = FALSE;
    if (r->text[r->position] == '/') {
        if (r->position + 1 >= r->length)
            return MX_E_INPUTEND;
        if (r->text[r->position + 1] != '>')
            return WC_E_GREATERTHAN;
        empty = TRUE;
        r->position++;
    }
    r->position++;

    /* the names that declare a namespace go into scope before any prefix is looked up. */
    ULONG bindings_base = r->binding_count;
    for (ULONG i = 0; i < r->attribute_count; i++) {
        ATTRIBUTE *a = &r->attributes[i];
        ULONG colon;
        hr = split_name(a->qname, a->qlen, &colon);
        if (FAILED(hr))
            goto fail;
        if (a->qlen == 5 && memcmp(a->qname, L"xmlns", 5 * sizeof(WCHAR)) == 0) {
            hr = push_binding(r, g_empty, 0, a->value, a->vlen);
        } else if (colon == 5 && memcmp(a->qname, L"xmlns", 5 * sizeof(WCHAR)) == 0) {
            if (!a->vlen) {
                hr = NC_E_EMPTYURI;
                goto fail;
            }
            hr = push_binding(r, a->qname + 6, a->qlen - 6, a->value, a->vlen);
        }
        if (FAILED(hr))
            goto fail;
    }
    ULONG colon;
    hr = split_name(r->text + name, (ULONG)length, &colon);
    if (FAILED(hr))
        goto fail;
    const WCHAR *uri = find_namespace(r, colon == (ULONG)-1 ? g_empty : r->text + name, colon == (ULONG)-1 ? 0 : colon);
    if (!uri && colon != (ULONG)-1) {
        hr = NC_E_UNDECLAREDPREFIX;
        goto fail;
    }
    hr = set_name(r, r->text + name, (ULONG)length, colon, uri);
    if (FAILED(hr))
        goto fail;
    for (ULONG i = 0; i < r->attribute_count; i++) {
        ATTRIBUTE *a = &r->attributes[i];
        ULONG attribute_colon;
        hr = split_name(a->qname, a->qlen, &attribute_colon);
        if (FAILED(hr))
            goto fail;
        if (attribute_colon == (ULONG)-1) {
            a->local = a->qname;
            a->llen = a->qlen;
            if (a->qlen == 5 && memcmp(a->qname, L"xmlns", 5 * sizeof(WCHAR)) == 0) {
                a->uri = (WCHAR *)WC_XMLNS_NAMESPACE;
                a->ulen = (ULONG)wcslen(a->uri);
            }
            continue;
        }
        a->prefix = node_string(r, a->qname, attribute_colon);
        a->plen = attribute_colon;
        a->local = a->qname + attribute_colon + 1;
        a->llen = a->qlen - attribute_colon - 1;
        if (!a->prefix) {
            hr = E_OUTOFMEMORY;
            goto fail;
        }
        const WCHAR *attribute_uri = find_namespace(r, a->qname, attribute_colon);
        if (!attribute_uri) {
            hr = NC_E_UNDECLAREDPREFIX;
            goto fail;
        }
        a->uri = (WCHAR *)attribute_uri;
        a->ulen = (ULONG)wcslen(attribute_uri);
    }
    for (ULONG i = 0; i < r->attribute_count; i++)
        for (ULONG k = i + 1; k < r->attribute_count; k++)
            if (r->attributes[i].llen == r->attributes[k].llen && r->attributes[i].ulen == r->attributes[k].ulen &&
                r->attributes[i].ulen &&
                memcmp(r->attributes[i].local, r->attributes[k].local, r->attributes[i].llen * sizeof(WCHAR)) == 0 &&
                memcmp(r->attributes[i].uri, r->attributes[k].uri, r->attributes[i].ulen * sizeof(WCHAR)) == 0) {
                hr = WC_E_UNIQUEATTRIBUTE;
                goto fail;
            }

    if (r->stack_count == r->stack_capacity) {
        ULONG capacity = r->stack_capacity ? r->stack_capacity * 2 : 16;
        ELEMENT *larger = XmlRealloc(r->stack, capacity * sizeof(*larger));
        if (!larger) {
            hr = E_OUTOFMEMORY;
            goto fail;
        }
        r->stack = larger;
        r->stack_capacity = capacity;
    }
    ELEMENT *element = &r->stack[r->stack_count];
    element->qname = XmlDuplicate(r->text + name, length);
    element->qlen = (ULONG)length;
    element->bindings = bindings_base;
    if (!element->qname) {
        hr = E_OUTOFMEMORY;
        goto fail;
    }
    r->stack_count++;
    r->root_seen = TRUE;
    r->depth = r->stack_count - 1;
    r->type = XmlNodeType_Element;
    r->empty = empty;
    r->pending_pop = empty;
    return S_OK;

fail:
    while (r->binding_count > bindings_base) {
        BINDING *b = &r->bindings[--r->binding_count];
        XmlFree(b->prefix);
        XmlFree(b->uri);
    }
    return hr;
}

static HRESULT parse_text(READER *r)
{
    SIZE_T start = r->position;
    BOOLEAN space = TRUE;
    while (r->position < r->length && r->text[r->position] != '<') {
        if (!is_space(r->text[r->position]))
            space = FALSE;
        if (r->text[r->position] == ']' && r->position + 2 < r->length && r->text[r->position + 1] == ']' &&
            r->text[r->position + 2] == '>')
            return WC_E_CDSECTEND;
        r->position++;
    }
    if (!space && !r->stack_count && is_document(r))
        return WC_E_SYNTAX;
    HRESULT hr = node_text(r, start, r->position, FALSE, &r->value, &r->vlen);
    if (FAILED(hr))
        return hr;
    r->depth = r->stack_count;
    r->type = space ? XmlNodeType_Whitespace : XmlNodeType_Text;
    return S_OK;
}

static HRESULT read_node(READER *r)
{
    if (r->pending_pop) {
        pop_element(r);
        r->pending_pop = FALSE;
    }
    forget_node(r);
    r->owner_type = XmlNodeType_None;
    if (r->position >= r->length) {
        if (r->stack_count || (is_document(r) && !r->root_seen))
            return MX_E_INPUTEND;
        r->finished = TRUE;
        r->type = XmlNodeType_None;
        r->read_state = XmlReadState_EndOfFile;
        return S_FALSE;
    }
    count_to(r, r->position);
    r->line = r->counted_line;
    r->column = r->counted_column;
    r->depth = r->stack_count;
    HRESULT hr;
    if (r->text[r->position] != '<')
        hr = parse_text(r);
    else if (looking_at(r, L"<?"))
        hr = parse_instruction(r);
    else if (looking_at(r, L"<!--"))
        hr = parse_comment(r);
    else if (looking_at(r, L"<![CDATA[")) {
        hr = r->stack_count ? parse_cdata(r) : WC_E_SYNTAX;
    } else if (looking_at(r, L"<!DOCTYPE"))
        hr = parse_doctype(r);
    else if (looking_at(r, L"</"))
        hr = parse_end_tag(r);
    else if (r->position + 1 < r->length && is_name_start(r->text[r->position + 1]))
        hr = parse_start_tag(r);
    else
        hr = r->position + 1 >= r->length ? MX_E_INPUTEND : WC_E_SYNTAX;
    return hr;
}

/* ------------------------------------------------------------------ the interface */

static HRESULT STDMETHODCALLTYPE reader_query(IXmlReader *This, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IXmlReader))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE reader_add_ref(IXmlReader *This)
{
    return InterlockedIncrement(&((READER *)This)->references);
}

static ULONG STDMETHODCALLTYPE reader_release(IXmlReader *This)
{
    READER *r = (READER *)This;
    LONG left = InterlockedDecrement(&r->references);
    if (!left) {
        reset_reader(r);
        XmlFree(r->attributes);
        XmlFree(r->stack);
        XmlFree(r->bindings);
        XmlFree(r->blocks);
        XmlFree(r);
    }
    return left;
}

static HRESULT STDMETHODCALLTYPE reader_set_input(IXmlReader *This, IUnknown *input)
{
    READER *r = (READER *)This;
    reset_reader(r);
    if (!input)
        return S_OK;
    IUnknown *stream = input;
    if (input->lpVtbl == &XmlInputVtbl) {
        XML_STREAM_OBJECT *object = (XML_STREAM_OBJECT *)input;
        stream = object->stream;
        r->code_page = object->code_page;
        r->code_page_forced = object->code_page && !object->hint;
        if (object->base_uri)
            r->base_uri = XmlDuplicate(object->base_uri, wcslen(object->base_uri));
    }
    HRESULT hr = stream->lpVtbl->QueryInterface(stream, &IID_ISequentialStream, (void **)&r->source);
    if (FAILED(hr)) {
        r->source = nullptr;
        return hr == E_NOINTERFACE ? E_INVALIDARG : hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_get_property(IXmlReader *This, UINT property, LONG_PTR *value)
{
    READER *r = (READER *)This;
    if (!value)
        return E_INVALIDARG;
    switch (property) {
    case XmlReaderProperty_MultiLanguage:
        *value = r->multi_language;
        return S_OK;
    case XmlReaderProperty_ConformanceLevel:
        *value = r->conformance;
        return S_OK;
    case XmlReaderProperty_RandomAccess:
        *value = r->random_access;
        return S_OK;
    case XmlReaderProperty_XmlResolver:
        *value = r->resolver;
        return S_OK;
    case XmlReaderProperty_DtdProcessing:
        *value = r->dtd_processing;
        return S_OK;
    case XmlReaderProperty_ReadState:
        *value = r->read_state;
        return S_OK;
    case XmlReaderProperty_MaxElementDepth:
        *value = r->max_depth;
        return S_OK;
    case XmlReaderProperty_MaxEntityExpansion:
        *value = 4096;
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE reader_set_property(IXmlReader *This, UINT property, LONG_PTR value)
{
    READER *r = (READER *)This;
    switch (property) {
    case XmlReaderProperty_MultiLanguage:
        r->multi_language = value != 0;
        return S_OK;
    case XmlReaderProperty_ConformanceLevel:
        if (value < XmlConformanceLevel_Auto || value > XmlConformanceLevel_Document)
            return E_INVALIDARG;
        r->conformance = (LONG)value;
        return S_OK;
    case XmlReaderProperty_RandomAccess:
        r->random_access = value != 0;
        return S_OK;
    case XmlReaderProperty_XmlResolver:
        r->resolver = value;
        return S_OK;
    case XmlReaderProperty_DtdProcessing:
        if (value != DtdProcessing_Prohibit && value != DtdProcessing_Parse)
            return E_INVALIDARG;
        r->dtd_processing = (LONG)value;
        return S_OK;
    case XmlReaderProperty_MaxElementDepth:
        if (value < 0)
            return E_INVALIDARG;
        r->max_depth = value ? (LONG)value : 0x7FFFFFFF;
        return S_OK;
    case XmlReaderProperty_MaxEntityExpansion:
        return value >= 0 ? S_OK : E_INVALIDARG;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE reader_read(IXmlReader *This, XmlNodeType *type)
{
    READER *r = (READER *)This;
    if (type)
        *type = XmlNodeType_None;
    if (r->error != S_OK)
        return r->error;
    if (r->finished)
        return S_FALSE;
    HRESULT hr = load_input(r);
    if (SUCCEEDED(hr)) {
        r->read_state = XmlReadState_Interactive;
        hr = read_node(r);
    }
    if (FAILED(hr)) {
        if (hr != E_PENDING) {
            r->error = hr;
            r->read_state = XmlReadState_Error;
        }
        r->type = XmlNodeType_None;
        return hr;
    }
    if (type)
        *type = r->type;
    return hr;
}

static HRESULT STDMETHODCALLTYPE reader_get_node_type(IXmlReader *This, XmlNodeType *type)
{
    READER *r = (READER *)This;
    if (!type)
        return E_INVALIDARG;
    *type = r->current_attribute >= 0 ? XmlNodeType_Attribute : r->type;
    return r->type == XmlNodeType_None ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_move_first(IXmlReader *This)
{
    READER *r = (READER *)This;
    if ((r->type != XmlNodeType_Element && r->type != XmlNodeType_XmlDeclaration) || !r->attribute_count)
        return S_FALSE;
    r->current_attribute = 0;
    r->chunk = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_move_next(IXmlReader *This)
{
    READER *r = (READER *)This;
    if ((r->type != XmlNodeType_Element && r->type != XmlNodeType_XmlDeclaration) || !r->attribute_count)
        return S_FALSE;
    if (r->current_attribute + 1 >= (LONG)r->attribute_count)
        return S_FALSE;
    r->current_attribute++;
    r->chunk = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_move_name(IXmlReader *This, LPCWSTR local, LPCWSTR uri)
{
    READER *r = (READER *)This;
    if (!local)
        return E_INVALIDARG;
    SIZE_T length = wcslen(local);
    for (ULONG i = 0; i < r->attribute_count; i++) {
        ATTRIBUTE *a = &r->attributes[i];
        if (a->llen != length || memcmp(a->local, local, length * sizeof(WCHAR)) != 0)
            continue;
        if (uri && (a->ulen != wcslen(uri) || memcmp(a->uri, uri, a->ulen * sizeof(WCHAR)) != 0))
            continue;
        r->current_attribute = (LONG)i;
        r->chunk = 0;
        return S_OK;
    }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE reader_move_element(IXmlReader *This)
{
    READER *r = (READER *)This;
    if (r->current_attribute < 0)
        return S_FALSE;
    r->current_attribute = -1;
    r->chunk = 0;
    return S_OK;
}

/* the string of the current node, or of the current attribute while one is entered. */
static HRESULT string_result(const WCHAR *text, ULONG length, LPCWSTR *result, UINT *result_length)
{
    if (!result)
        return E_INVALIDARG;
    *result = text;
    if (result_length)
        *result_length = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_get_qualified_name(IXmlReader *This, LPCWSTR *name, UINT *length)
{
    READER *r = (READER *)This;
    if (r->current_attribute >= 0) {
        ATTRIBUTE *a = &r->attributes[r->current_attribute];
        return string_result(a->qname, a->qlen, name, length);
    }
    return string_result(r->qname, r->qlen, name, length);
}

static HRESULT STDMETHODCALLTYPE reader_get_namespace(IXmlReader *This, LPCWSTR *uri, UINT *length)
{
    READER *r = (READER *)This;
    if (r->current_attribute >= 0) {
        ATTRIBUTE *a = &r->attributes[r->current_attribute];
        return string_result(a->uri, a->ulen, uri, length);
    }
    return string_result(r->uri, r->ulen, uri, length);
}

static HRESULT STDMETHODCALLTYPE reader_get_local_name(IXmlReader *This, LPCWSTR *name, UINT *length)
{
    READER *r = (READER *)This;
    if (r->current_attribute >= 0) {
        ATTRIBUTE *a = &r->attributes[r->current_attribute];
        return string_result(a->local, a->llen, name, length);
    }
    return string_result(r->local, r->llen, name, length);
}

static HRESULT STDMETHODCALLTYPE reader_get_prefix(IXmlReader *This, LPCWSTR *prefix, UINT *length)
{
    READER *r = (READER *)This;
    if (r->current_attribute >= 0) {
        ATTRIBUTE *a = &r->attributes[r->current_attribute];
        return string_result(a->prefix, a->plen, prefix, length);
    }
    return string_result(r->prefix, r->plen, prefix, length);
}

static HRESULT STDMETHODCALLTYPE reader_get_value(IXmlReader *This, LPCWSTR *value, UINT *length)
{
    READER *r = (READER *)This;
    if (r->current_attribute >= 0) {
        ATTRIBUTE *a = &r->attributes[r->current_attribute];
        return string_result(a->value, a->vlen, value, length);
    }
    return string_result(r->value, r->vlen, value, length);
}

static HRESULT STDMETHODCALLTYPE reader_read_chunk(IXmlReader *This, WCHAR *buffer, UINT size, UINT *read)
{
    READER *r = (READER *)This;
    const WCHAR *text = r->value;
    ULONG length = r->vlen;
    if (r->current_attribute >= 0) {
        text = r->attributes[r->current_attribute].value;
        length = r->attributes[r->current_attribute].vlen;
    }
    if (!buffer || !read)
        return E_INVALIDARG;
    SIZE_T left = length - r->chunk;
    if (left > size)
        left = size;
    memcpy(buffer, text + r->chunk, left * sizeof(WCHAR));
    r->chunk += left;
    *read = (UINT)left;
    return left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE reader_get_base_uri(IXmlReader *This, LPCWSTR *uri, UINT *length)
{
    READER *r = (READER *)This;
    const WCHAR *text = r->base_uri ? r->base_uri : g_empty;
    return string_result(text, (ULONG)wcslen(text), uri, length);
}

static BOOL STDMETHODCALLTYPE reader_is_default(IXmlReader *This)
{
    return FALSE;
}

static BOOL STDMETHODCALLTYPE reader_is_empty(IXmlReader *This)
{
    READER *r = (READER *)This;
    return r->current_attribute < 0 && r->type == XmlNodeType_Element && r->empty;
}

static HRESULT STDMETHODCALLTYPE reader_get_line(IXmlReader *This, UINT *line)
{
    READER *r = (READER *)This;
    if (!line)
        return E_INVALIDARG;
    *line = r->line;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_get_position(IXmlReader *This, UINT *position)
{
    READER *r = (READER *)This;
    if (!position)
        return E_INVALIDARG;
    *position = r->column;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_get_attribute_count(IXmlReader *This, UINT *count)
{
    READER *r = (READER *)This;
    if (!count)
        return E_INVALIDARG;
    *count = r->type == XmlNodeType_Element || r->type == XmlNodeType_XmlDeclaration ? r->attribute_count : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reader_get_depth(IXmlReader *This, UINT *depth)
{
    READER *r = (READER *)This;
    if (!depth)
        return E_INVALIDARG;
    *depth = r->depth + (r->current_attribute >= 0 ? 1 : 0);
    return S_OK;
}

static BOOL STDMETHODCALLTYPE reader_is_eof(IXmlReader *This)
{
    return ((READER *)This)->finished;
}

static const IXmlReaderVtbl g_reader_vtbl = {
    reader_query,         reader_add_ref,        reader_release,        reader_set_input,     reader_get_property,
    reader_set_property,  reader_read,           reader_get_node_type,  reader_move_first,    reader_move_next,
    reader_move_name,     reader_move_element,   reader_get_qualified_name, reader_get_namespace, reader_get_local_name,
    reader_get_prefix,    reader_get_value,      reader_read_chunk,     reader_get_base_uri,  reader_is_default,
    reader_is_empty,      reader_get_line,       reader_get_position,   reader_get_attribute_count, reader_get_depth,
    reader_is_eof,
};

HRESULT XmlCreateReader(REFIID iid, void **object)
{
    if (!IsEqualGUID(iid, &IID_IXmlReader) && !IsEqualGUID(iid, &IID_IUnknown))
        return E_NOINTERFACE;
    READER *r = XmlAlloc(sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->lpVtbl = &g_reader_vtbl;
    r->references = 1;
    r->conformance = XmlConformanceLevel_Auto;
    r->dtd_processing = DtdProcessing_Prohibit;
    r->max_depth = 256;
    reset_reader(r);
    *object = r;
    return S_OK;
}
