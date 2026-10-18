/*
 * application and assembly manifests: the xml subset real manifests use
 * and the model the activation context generator reads.
 *
 * the text is turned into utf-16 first, from utf-8 or utf-16 with or
 * without a byte order mark, or from a single byte code page the xml
 * declaration names. the parser keeps comments, processing instructions,
 * cdata, entities and namespaces, and builds a small tree that the
 * interpretation below walks by namespace and local name.
 */
#include "sxs.h"

#define NS_ASM_V1 L"urn:schemas-microsoft-com:asm.v1"
#define NS_ASM_V2 L"urn:schemas-microsoft-com:asm.v2"
#define NS_ASM_V3 L"urn:schemas-microsoft-com:asm.v3"
#define NS_COMPATIBILITY L"urn:schemas-microsoft-com:compatibility.v1"

/* ------------------------------------------------------------------ pool */

typedef struct POOL_BLOCK {
    struct POOL_BLOCK *Next;
    SIZE_T Used, Size;
    DECLSPEC_ALIGN(16) UCHAR Data[];
} POOL_BLOCK;

static PVOID pool_alloc(PVOID *pool, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    POOL_BLOCK *b = *pool;
    if (!b || b->Size - b->Used < size) {
        SIZE_T want = size > 16384 ? size : 16384;
        POOL_BLOCK *n = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*n) + want);
        if (!n)
            return nullptr;
        n->Next = b;
        n->Used = 0;
        n->Size = want;
        *pool = b = n;
    }
    PVOID p = b->Data + b->Used;
    b->Used += size;
    memset(p, 0, size);
    return p;
}

static void pool_free(PVOID pool)
{
    for (POOL_BLOCK *b = pool; b;) {
        POOL_BLOCK *next = b->Next;
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, b);
        b = next;
    }
}

static PWSTR pool_strndup(PVOID *pool, PCWSTR s, SIZE_T chars)
{
    PWSTR copy = pool_alloc(pool, (chars + 1) * sizeof(WCHAR));
    if (copy)
        memcpy(copy, s, chars * sizeof(WCHAR));
    return copy;
}

/* grows an array in the pool by one element; the old storage stays in the pool. */
static PVOID pool_append(PVOID *pool, PVOID array, ULONG *count, SIZE_T element)
{
    UCHAR *grown = pool_alloc(pool, (*count + 1) * element);
    if (!grown)
        return nullptr;
    if (*count)
        memcpy(grown, array, *count * element);
    (*count)++;
    return grown;
}

/* ------------------------------------------------------------------ decoding */

static BOOLEAN is_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* whether an <?xml ...?> declaration names an encoding other than utf-8 and utf-16. */
static BOOLEAN declared_single_byte(const UCHAR *bytes, SIZE_T size)
{
    if (size < 5 || memcmp(bytes, "<?xml", 5))
        return FALSE;
    SIZE_T end = 5;
    while (end + 1 < size && !(bytes[end] == '?' && bytes[end + 1] == '>'))
        end++;
    for (SIZE_T i = 5; i + 8 < end; i++) {
        if (memcmp(bytes + i, "encoding", 8))
            continue;
        SIZE_T j = i + 8;
        while (j < end && (bytes[j] == ' ' || bytes[j] == '=' || bytes[j] == '"' || bytes[j] == '\''))
            j++;
        char name[16];
        SIZE_T n = 0;
        while (j < end && n + 1 < sizeof(name) && bytes[j] != '"' && bytes[j] != '\'')
            name[n++] = (char)bytes[j++];
        name[n] = 0;
        return _stricmp(name, "utf-8") && _stricmp(name, "utf8") && _strnicmp(name, "utf-16", 6) &&
            _stricmp(name, "unicode");
    }
    return FALSE;
}

/* the text as nul terminated utf-16 from the process heap. */
static NTSTATUS decode(const void *data, SIZE_T size, PWSTR *out, SIZE_T *out_chars)
{
    const UCHAR *bytes = data;
    if (size > 64 * 1024 * 1024)
        return STATUS_SXS_CANT_GEN_ACTCTX;
    PWSTR text = nullptr;
    SIZE_T chars = 0;
    BOOLEAN big_endian = FALSE, wide = FALSE;
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        wide = TRUE;
        bytes += 2, size -= 2;
    } else if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        wide = big_endian = TRUE;
        bytes += 2, size -= 2;
    } else if (size >= 2 && bytes[0] == '<' && bytes[1] == 0) {
        wide = TRUE;
    } else if (size >= 2 && bytes[0] == 0 && bytes[1] == '<') {
        wide = big_endian = TRUE;
    }
    if (wide) {
        chars = size / 2;
        text = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, (chars + 1) * sizeof(WCHAR));
        if (!text)
            return STATUS_NO_MEMORY;
        for (SIZE_T i = 0; i < chars; i++)
            text[i] = big_endian ? (WCHAR)(bytes[2 * i] << 8 | bytes[2 * i + 1]) : (WCHAR)(bytes[2 * i] | bytes[2 * i + 1] << 8);
    } else {
        if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            bytes += 3, size -= 3;
        text = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, (size + 1) * sizeof(WCHAR));
        if (!text)
            return STATUS_NO_MEMORY;
        ULONG written = 0;
        if (declared_single_byte(bytes, size))
            RtlMultiByteToUnicodeN(text, (ULONG)(size * sizeof(WCHAR)), &written, (const CHAR *)bytes, (ULONG)size);
        else
            RtlUTF8ToUnicodeN(text, (ULONG)(size * sizeof(WCHAR)), &written, (PCCH)bytes, (ULONG)size);
        chars = written / sizeof(WCHAR);
    }
    text[chars] = 0;
    *out = text;
    *out_chars = chars;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ xml tree */

typedef struct XML_ATTR {
    PCWSTR Ns; /* NULL for unprefixed attributes */
    PCWSTR Name;
    PCWSTR Value;
} XML_ATTR;

typedef struct XML_NODE {
    PCWSTR Ns;
    PCWSTR Name;
    XML_ATTR *Attrs;
    ULONG AttrCount;
    struct XML_NODE *Parent, *First, *Last, *Next;
    PWSTR Text;
    SIZE_T TextChars;
} XML_NODE;

typedef struct XML_BINDING {
    PCWSTR Prefix; /* "" for the default namespace */
    SIZE_T PrefixChars;
    PCWSTR Uri;
    ULONG Depth;
} XML_BINDING;

typedef struct XML_PARSER {
    PCWSTR P, End;
    PVOID *Pool;
    XML_BINDING *Bindings;
    ULONG BindingCount, BindingCapacity;
    ULONG Depth;
} XML_PARSER;

static BOOLEAN starts(XML_PARSER *x, PCWSTR s)
{
    SIZE_T n = wcslen(s);
    return (SIZE_T)(x->End - x->P) >= n && !memcmp(x->P, s, n * sizeof(WCHAR));
}

static void skip_spaces(XML_PARSER *x)
{
    while (x->P < x->End && is_space(*x->P))
        x->P++;
}

/* skips to just past a terminator; FALSE at the end of the text. */
static BOOLEAN skip_past(XML_PARSER *x, PCWSTR terminator)
{
    while (x->P < x->End) {
        if (starts(x, terminator)) {
            x->P += wcslen(terminator);
            return TRUE;
        }
        x->P++;
    }
    return FALSE;
}

static BOOLEAN name_char(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
        c == '.' || c == ':' || c >= 0x80;
}

static BOOLEAN read_name(XML_PARSER *x, PCWSTR *name, SIZE_T *chars)
{
    PCWSTR start = x->P;
    if (start >= x->End || !name_char(*start) || *start == '-' || *start == '.' || (*start >= '0' && *start <= '9'))
        return FALSE;
    while (x->P < x->End && name_char(*x->P))
        x->P++;
    *name = start;
    *chars = (SIZE_T)(x->P - start);
    return TRUE;
}

static void append_char(WCHAR *buffer, SIZE_T *n, WCHAR c)
{
    buffer[(*n)++] = c;
}

/* decodes one entity reference starting after '&'; FALSE when unknown. */
static BOOLEAN read_entity(XML_PARSER *x, WCHAR *buffer, SIZE_T *n)
{
    static const struct {
        PCWSTR Name;
        WCHAR Char;
    } named[] = {{L"amp;", '&'}, {L"lt;", '<'}, {L"gt;", '>'}, {L"quot;", '"'}, {L"apos;", '\''}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(named); i++) {
        if (starts(x, named[i].Name)) {
            x->P += wcslen(named[i].Name);
            append_char(buffer, n, named[i].Char);
            return TRUE;
        }
    }
    if (x->P >= x->End || *x->P != '#')
        return FALSE;
    x->P++;
    ULONG base = 10, value = 0, digits = 0;
    if (x->P < x->End && (*x->P == 'x' || *x->P == 'X')) {
        base = 16;
        x->P++;
    }
    for (; x->P < x->End && *x->P != ';'; x->P++, digits++) {
        WCHAR c = *x->P;
        ULONG d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return FALSE;
        value = value * base + d;
        if (value > 0x10FFFF)
            return FALSE;
    }
    if (x->P >= x->End || !digits || !value)
        return FALSE;
    x->P++;
    if (value >= 0x10000) {
        value -= 0x10000;
        append_char(buffer, n, (WCHAR)(0xD800 + (value >> 10)));
        append_char(buffer, n, (WCHAR)(0xDC00 + (value & 0x3FF)));
    } else {
        append_char(buffer, n, (WCHAR)value);
    }
    return TRUE;
}

static PCWSTR lookup_namespace(XML_PARSER *x, PCWSTR prefix, SIZE_T chars)
{
    for (ULONG i = x->BindingCount; i-- > 0;) {
        XML_BINDING *b = &x->Bindings[i];
        if (b->PrefixChars == chars && !memcmp(b->Prefix, prefix, chars * sizeof(WCHAR)))
            return b->Uri;
    }
    return nullptr;
}

static BOOLEAN bind(XML_PARSER *x, PCWSTR prefix, SIZE_T chars, PCWSTR uri)
{
    if (x->BindingCount == x->BindingCapacity) {
        ULONG capacity = x->BindingCapacity ? x->BindingCapacity * 2 : 16;
        XML_BINDING *grown = pool_alloc(x->Pool, capacity * sizeof(*grown));
        if (!grown)
            return FALSE;
        if (x->BindingCount)
            memcpy(grown, x->Bindings, x->BindingCount * sizeof(*grown));
        x->Bindings = grown;
        x->BindingCapacity = capacity;
    }
    x->Bindings[x->BindingCount++] = (XML_BINDING){prefix, chars, uri, x->Depth};
    return TRUE;
}

typedef struct RAW_ATTR {
    PCWSTR Name;
    SIZE_T NameChars;
    PWSTR Value;
} RAW_ATTR;

/* reads a quoted attribute value with its entities resolved. */
static PWSTR read_value(XML_PARSER *x)
{
    if (x->P >= x->End || (*x->P != '"' && *x->P != '\''))
        return nullptr;
    WCHAR quote = *x->P++;
    PCWSTR start = x->P;
    while (x->P < x->End && *x->P != quote)
        x->P++;
    if (x->P >= x->End)
        return nullptr;
    SIZE_T span = (SIZE_T)(x->P - start);
    PWSTR value = pool_alloc(x->Pool, (span * 2 + 1) * sizeof(WCHAR));
    if (!value)
        return nullptr;
    SIZE_T n = 0;
    XML_PARSER inner = *x;
    inner.P = start;
    inner.End = x->P;
    while (inner.P < inner.End) {
        WCHAR c = *inner.P++;
        if (c == '<')
            return nullptr;
        if (c == '&') {
            if (!read_entity(&inner, value, &n))
                return nullptr;
        } else {
            /* attribute value normalization turns white space into spaces. */
            append_char(value, &n, is_space(c) ? ' ' : c);
        }
    }
    value[n] = 0;
    x->P++;
    return value;
}

static NTSTATUS parse_element(XML_PARSER *x, XML_NODE *parent, XML_NODE **out);

/* character data, comments, processing instructions and child elements up to the end tag. */
static NTSTATUS parse_content(XML_PARSER *x, XML_NODE *node, PCWSTR qname, SIZE_T qchars)
{
    for (;;) {
        if (x->P >= x->End)
            return STATUS_SXS_CANT_GEN_ACTCTX;
        if (*x->P != '<') {
            PCWSTR start = x->P;
            while (x->P < x->End && *x->P != '<')
                x->P++;
            SIZE_T span = (SIZE_T)(x->P - start);
            PWSTR text = pool_alloc(x->Pool, (node->TextChars + span * 2 + 1) * sizeof(WCHAR));
            if (!text)
                return STATUS_NO_MEMORY;
            SIZE_T n = node->TextChars;
            if (n)
                memcpy(text, node->Text, n * sizeof(WCHAR));
            XML_PARSER inner = *x;
            inner.P = start;
            inner.End = x->P;
            while (inner.P < inner.End) {
                WCHAR c = *inner.P++;
                if (c == '&') {
                    if (!read_entity(&inner, text, &n))
                        return STATUS_SXS_CANT_GEN_ACTCTX;
                } else {
                    append_char(text, &n, c);
                }
            }
            text[n] = 0;
            node->Text = text;
            node->TextChars = n;
            continue;
        }
        if (starts(x, L"<!--")) {
            if (!skip_past(x, L"-->"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            continue;
        }
        if (starts(x, L"<![CDATA[")) {
            x->P += 9;
            PCWSTR start = x->P;
            if (!skip_past(x, L"]]>"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            SIZE_T span = (SIZE_T)(x->P - 3 - start);
            PWSTR text = pool_alloc(x->Pool, (node->TextChars + span + 1) * sizeof(WCHAR));
            if (!text)
                return STATUS_NO_MEMORY;
            if (node->TextChars)
                memcpy(text, node->Text, node->TextChars * sizeof(WCHAR));
            memcpy(text + node->TextChars, start, span * sizeof(WCHAR));
            node->TextChars += span;
            text[node->TextChars] = 0;
            node->Text = text;
            continue;
        }
        if (starts(x, L"<?")) {
            if (!skip_past(x, L"?>"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            continue;
        }
        if (starts(x, L"</")) {
            x->P += 2;
            PCWSTR name;
            SIZE_T chars;
            if (!read_name(x, &name, &chars) || chars != qchars || memcmp(name, qname, chars * sizeof(WCHAR)))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            skip_spaces(x);
            if (x->P >= x->End || *x->P != '>')
                return STATUS_SXS_CANT_GEN_ACTCTX;
            x->P++;
            return STATUS_SUCCESS;
        }
        XML_NODE *child;
        NTSTATUS status = parse_element(x, node, &child);
        if (!NT_SUCCESS(status))
            return status;
    }
}

static NTSTATUS parse_element(XML_PARSER *x, XML_NODE *parent, XML_NODE **out)
{
    if (x->P >= x->End || *x->P != '<')
        return STATUS_SXS_CANT_GEN_ACTCTX;
    x->P++;
    PCWSTR qname;
    SIZE_T qchars;
    if (!read_name(x, &qname, &qchars))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    RAW_ATTR raw[64];
    ULONG raw_count = 0;
    x->Depth++;
    ULONG bindings_before = x->BindingCount;
    BOOLEAN empty = FALSE;
    for (;;) {
        BOOLEAN spaced = x->P < x->End && is_space(*x->P);
        skip_spaces(x);
        if (x->P >= x->End)
            return STATUS_SXS_CANT_GEN_ACTCTX;
        if (*x->P == '>') {
            x->P++;
            break;
        }
        if (starts(x, L"/>")) {
            x->P += 2;
            empty = TRUE;
            break;
        }
        if (!spaced || raw_count == RTL_NUMBER_OF(raw))
            return STATUS_SXS_CANT_GEN_ACTCTX;
        RAW_ATTR *a = &raw[raw_count];
        if (!read_name(x, &a->Name, &a->NameChars))
            return STATUS_SXS_CANT_GEN_ACTCTX;
        skip_spaces(x);
        if (x->P >= x->End || *x->P != '=')
            return STATUS_SXS_CANT_GEN_ACTCTX;
        x->P++;
        skip_spaces(x);
        if (!(a->Value = read_value(x)))
            return STATUS_SXS_CANT_GEN_ACTCTX;
        for (ULONG i = 0; i < raw_count; i++)
            if (raw[i].NameChars == a->NameChars && !memcmp(raw[i].Name, a->Name, a->NameChars * sizeof(WCHAR)))
                return STATUS_SXS_CANT_GEN_ACTCTX;
        if (a->NameChars == 5 && !memcmp(a->Name, L"xmlns", 5 * sizeof(WCHAR))) {
            if (!bind(x, L"", 0, a->Value))
                return STATUS_NO_MEMORY;
        } else if (a->NameChars > 6 && !memcmp(a->Name, L"xmlns:", 6 * sizeof(WCHAR))) {
            if (!*a->Value || !bind(x, a->Name + 6, a->NameChars - 6, a->Value))
                return STATUS_SXS_CANT_GEN_ACTCTX;
        }
        raw_count++;
    }
    XML_NODE *node = pool_alloc(x->Pool, sizeof(*node));
    if (!node)
        return STATUS_NO_MEMORY;
    node->Parent = parent;
    PCWSTR colon = nullptr;
    for (SIZE_T i = 0; i < qchars; i++)
        if (qname[i] == ':')
            colon = qname + i;
    if (colon) {
        node->Ns = lookup_namespace(x, qname, (SIZE_T)(colon - qname));
        if (!node->Ns)
            return STATUS_SXS_CANT_GEN_ACTCTX;
        node->Name = pool_strndup(x->Pool, colon + 1, qchars - (SIZE_T)(colon + 1 - qname));
    } else {
        node->Ns = lookup_namespace(x, L"", 0);
        if (node->Ns && !*node->Ns)
            node->Ns = nullptr;
        node->Name = pool_strndup(x->Pool, qname, qchars);
    }
    node->Attrs = pool_alloc(x->Pool, (raw_count ? raw_count : 1) * sizeof(XML_ATTR));
    if (!node->Name || !node->Attrs)
        return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < raw_count; i++) {
        RAW_ATTR *a = &raw[i];
        if ((a->NameChars == 5 && !memcmp(a->Name, L"xmlns", 10)) ||
            (a->NameChars > 6 && !memcmp(a->Name, L"xmlns:", 12)))
            continue;
        XML_ATTR *attr = &node->Attrs[node->AttrCount++];
        PCWSTR acolon = nullptr;
        for (SIZE_T j = 0; j < a->NameChars; j++)
            if (a->Name[j] == ':')
                acolon = a->Name + j;
        if (acolon) {
            attr->Ns = lookup_namespace(x, a->Name, (SIZE_T)(acolon - a->Name));
            if (!attr->Ns)
                return STATUS_SXS_CANT_GEN_ACTCTX;
            attr->Name = pool_strndup(x->Pool, acolon + 1, a->NameChars - (SIZE_T)(acolon + 1 - a->Name));
        } else {
            attr->Name = pool_strndup(x->Pool, a->Name, a->NameChars);
        }
        if (!attr->Name)
            return STATUS_NO_MEMORY;
        attr->Value = a->Value;
    }
    if (parent) {
        if (parent->Last)
            parent->Last->Next = node;
        else
            parent->First = node;
        parent->Last = node;
    }
    NTSTATUS status = empty ? STATUS_SUCCESS : parse_content(x, node, qname, qchars);
    x->BindingCount = bindings_before;
    x->Depth--;
    *out = node;
    return status;
}

static NTSTATUS parse_document(PCWSTR text, SIZE_T chars, PVOID *pool, XML_NODE **root)
{
    XML_PARSER x = {.P = text, .End = text + chars, .Pool = pool};
    /* the xml namespace is always bound. */
    if (!bind(&x, L"xml", 3, L"http://www.w3.org/XML/1998/namespace"))
        return STATUS_NO_MEMORY;
    *root = nullptr;
    for (;;) {
        skip_spaces(&x);
        if (x.P >= x.End)
            return *root ? STATUS_SUCCESS : STATUS_SXS_CANT_GEN_ACTCTX;
        if (starts(&x, L"<?")) {
            if (!skip_past(&x, L"?>"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
        } else if (starts(&x, L"<!--")) {
            if (!skip_past(&x, L"-->"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
        } else if (starts(&x, L"<!DOCTYPE")) {
            if (*root || !skip_past(&x, L">"))
                return STATUS_SXS_CANT_GEN_ACTCTX;
        } else if (*x.P == '<' && !*root) {
            NTSTATUS status = parse_element(&x, nullptr, root);
            if (!NT_SUCCESS(status))
                return status;
        } else if (*x.P == 0 && *root) {
            /* trailing nul padding of resources. */
            x.P++;
        } else {
            return STATUS_SXS_CANT_GEN_ACTCTX;
        }
    }
}

/* ------------------------------------------------------------------ tree helpers */

static BOOLEAN ns_is(PCWSTR ns, PCWSTR want)
{
    return ns && !wcscmp(ns, want);
}

static BOOLEAN is_asm_ns(PCWSTR ns)
{
    return ns_is(ns, NS_ASM_V1) || ns_is(ns, NS_ASM_V2) || ns_is(ns, NS_ASM_V3);
}

static BOOLEAN is(const XML_NODE *n, PCWSTR name)
{
    return !wcscmp(n->Name, name);
}

static PCWSTR attr(const XML_NODE *n, PCWSTR name)
{
    for (ULONG i = 0; i < n->AttrCount; i++)
        if (!n->Attrs[i].Ns && !wcscmp(n->Attrs[i].Name, name))
            return n->Attrs[i].Value;
    return nullptr;
}

static BOOLEAN yes(PCWSTR value)
{
    return value && (!_wcsicmp(value, L"yes") || !_wcsicmp(value, L"true"));
}

/* ------------------------------------------------------------------ values */

BOOLEAN SxspParseVersion(PCWSTR text, USHORT version[4])
{
    if (!text)
        return FALSE;
    for (ULONG part = 0; part < 4; part++) {
        ULONG value = 0, digits = 0;
        for (; *text >= '0' && *text <= '9'; text++, digits++) {
            value = value * 10 + (ULONG)(*text - '0');
            if (value > 0xFFFF)
                return FALSE;
        }
        if (!digits)
            return FALSE;
        version[part] = (USHORT)value;
        if (part < 3) {
            if (*text != '.')
                return FALSE;
            text++;
        }
    }
    return *text == 0;
}

static BOOLEAN hex_value(WCHAR c, ULONG *v)
{
    if (c >= '0' && c <= '9')
        *v = c - '0';
    else if (c >= 'a' && c <= 'f')
        *v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        *v = c - 'A' + 10;
    else
        return FALSE;
    return TRUE;
}

/* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", the braces optional. */
static BOOLEAN parse_guid(PCWSTR text, GUID *guid)
{
    if (!text)
        return FALSE;
    BOOLEAN braced = *text == '{';
    if (braced)
        text++;
    static const UCHAR groups[] = {8, 4, 4, 4, 12};
    UCHAR bytes[16];
    ULONG at = 0;
    for (ULONG g = 0; g < RTL_NUMBER_OF(groups); g++) {
        for (ULONG i = 0; i < groups[g]; i += 2) {
            ULONG hi, lo;
            if (!hex_value(text[0], &hi) || !hex_value(text[1], &lo))
                return FALSE;
            bytes[at++] = (UCHAR)(hi << 4 | lo);
            text += 2;
        }
        if (g < 4 && *text++ != '-')
            return FALSE;
    }
    if (braced && *text++ != '}')
        return FALSE;
    if (*text)
        return FALSE;
    guid->Data1 = (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
    guid->Data2 = (USHORT)(bytes[4] << 8 | bytes[5]);
    guid->Data3 = (USHORT)(bytes[6] << 8 | bytes[7]);
    memcpy(guid->Data4, bytes + 8, 8);
    return TRUE;
}

static ULONG parse_ulong(PCWSTR text, ULONG base)
{
    ULONG value = 0, digit;
    if (!text)
        return 0;
    if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    for (; hex_value(*text, &digit) && digit < base; text++)
        value = value * base + digit;
    return value;
}

static PWSTR dup(PVOID *pool, PCWSTR s)
{
    return s ? pool_strndup(pool, s, wcslen(s)) : nullptr;
}

/* the element's text without surrounding white space. */
static PWSTR trimmed_text(PVOID *pool, const XML_NODE *n)
{
    PCWSTR s = n->Text ? n->Text : L"";
    SIZE_T chars = n->TextChars;
    while (chars && is_space(*s))
        s++, chars--;
    while (chars && is_space(s[chars - 1]))
        chars--;
    return pool_strndup(pool, s, chars);
}

/* ------------------------------------------------------------------ interpretation */

static NTSTATUS read_identity(PVOID *pool, const XML_NODE *n, SXS_IDENTITY *id, BOOLEAN need_name)
{
    id->Name = dup(pool, attr(n, L"name"));
    id->Type = dup(pool, attr(n, L"type"));
    id->Arch = dup(pool, attr(n, L"processorArchitecture"));
    id->Token = dup(pool, attr(n, L"publicKeyToken"));
    id->Language = dup(pool, attr(n, L"language"));
    PCWSTR version = attr(n, L"version");
    if (version) {
        if (!SxspParseVersion(version, id->Version))
            return STATUS_SXS_CANT_GEN_ACTCTX;
        id->HasVersion = TRUE;
    }
    if (need_name && (!id->Name || !*id->Name))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    return STATUS_SUCCESS;
}

static ULONG threading_model(PCWSTR value)
{
    static const PCWSTR models[] = {L"Apartment", L"Free", L"Single", L"Both", L"Neutral"};
    if (!value)
        return 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(models); i++)
        if (!_wcsicmp(value, models[i]))
            return i + 1;
    return 0;
}

static NTSTATUS read_interface(PVOID *pool, const XML_NODE *n, SXS_INTERFACE *itf)
{
    if (!parse_guid(attr(n, L"iid"), &itf->Iid))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    itf->Name = dup(pool, attr(n, L"name"));
    parse_guid(attr(n, L"tlbid"), &itf->TypeLibrary);
    PCWSTR proxy = attr(n, L"proxyStubClsid32");
    if (proxy && !parse_guid(proxy, &itf->ProxyStubClsid))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    PCWSTR methods = attr(n, L"numMethods");
    if (methods) {
        itf->NumMethods = parse_ulong(methods, 10);
        itf->HasNumMethods = TRUE;
    }
    itf->HasBase = parse_guid(attr(n, L"baseInterface"), &itf->Base);
    return STATUS_SUCCESS;
}

/* the name of a file element must stay inside the assembly's directory, as sxs.dll checks. */
static BOOLEAN valid_file_name(PCWSTR name)
{
    if (!name || !*name || RtlDetermineDosPathNameType_U(name) != RtlPathTypeRelative)
        return FALSE;
    static const PCWSTR forbidden[] = {L"..\\", L"../", L"\\..", L"/.."};
    for (ULONG i = 0; i < RTL_NUMBER_OF(forbidden); i++)
        if (wcsstr(name, forbidden[i]))
            return FALSE;
    return TRUE;
}

static NTSTATUS read_file(PVOID *pool, const XML_NODE *n, SXS_MANIFEST *m)
{
    PCWSTR name = attr(n, L"name");
    if (!valid_file_name(name))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    SXS_FILE *files = pool_append(pool, m->Files, &m->FileCount, sizeof(SXS_FILE));
    if (!files)
        return STATUS_NO_MEMORY;
    m->Files = files;
    SXS_FILE *f = &files[m->FileCount - 1];
    f->Name = dup(pool, name);
    for (const XML_NODE *c = n->First; c; c = c->Next) {
        if (!is_asm_ns(c->Ns))
            continue;
        if (is(c, L"windowClass")) {
            SXS_WINDOW_CLASS *classes = pool_append(pool, f->Classes, &f->ClassCount, sizeof(*classes));
            if (!classes)
                return STATUS_NO_MEMORY;
            f->Classes = classes;
            SXS_WINDOW_CLASS *wc = &classes[f->ClassCount - 1];
            wc->Name = trimmed_text(pool, c);
            PCWSTR versioned = attr(c, L"versioned");
            wc->Versioned = !versioned || yes(versioned);
            if (!wc->Name || !*wc->Name)
                return STATUS_SXS_CANT_GEN_ACTCTX;
        } else if (is(c, L"comClass")) {
            SXS_COM_CLASS *classes = pool_append(pool, f->ComClasses, &f->ComClassCount, sizeof(*classes));
            if (!classes)
                return STATUS_NO_MEMORY;
            f->ComClasses = classes;
            SXS_COM_CLASS *cc = &classes[f->ComClassCount - 1];
            if (!parse_guid(attr(c, L"clsid"), &cc->Clsid))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            parse_guid(attr(c, L"tlbid"), &cc->TypeLibrary);
            cc->ProgId = dup(pool, attr(c, L"progid"));
            cc->ThreadingModel = threading_model(attr(c, L"threadingModel"));
            static const PCWSTR misc[] = {L"miscStatus", L"miscStatusContent", L"miscStatusThumbnail",
                                          L"miscStatusIcon", L"miscStatusDocPrint"};
            for (ULONG i = 0; i < RTL_NUMBER_OF(misc); i++)
                cc->MiscStatus[i] = parse_ulong(attr(c, misc[i]), 10);
            for (const XML_NODE *p = c->First; p; p = p->Next) {
                if (!is_asm_ns(p->Ns) || !is(p, L"progid"))
                    continue;
                PWSTR *ids = pool_append(pool, cc->ProgIds, &cc->ProgIdCount, sizeof(PWSTR));
                if (!ids)
                    return STATUS_NO_MEMORY;
                cc->ProgIds = ids;
                ids[cc->ProgIdCount - 1] = trimmed_text(pool, p);
            }
        } else if (is(c, L"typelib")) {
            SXS_TYPELIB *libs = pool_append(pool, f->TypeLibs, &f->TypeLibCount, sizeof(*libs));
            if (!libs)
                return STATUS_NO_MEMORY;
            f->TypeLibs = libs;
            SXS_TYPELIB *tl = &libs[f->TypeLibCount - 1];
            if (!parse_guid(attr(c, L"tlbid"), &tl->Id))
                return STATUS_SXS_CANT_GEN_ACTCTX;
            PCWSTR version = attr(c, L"version");
            if (version) {
                tl->Major = (USHORT)parse_ulong(version, 10);
                PCWSTR dot = wcschr(version, '.');
                tl->Minor = dot ? (USHORT)parse_ulong(dot + 1, 10) : 0;
            }
            tl->HelpDir = dup(pool, attr(c, L"helpdir"));
            PCWSTR flags = attr(c, L"flags");
            if (flags) {
                /* RESTRICTED, CONTROL, HIDDEN and HASDISKIMAGE, as TYPEFLAGS spells them. */
                static const PCWSTR names[] = {L"RESTRICTED", L"CONTROL", L"HIDDEN", L"HASDISKIMAGE"};
                for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++)
                    if (wcsstr(flags, names[i]))
                        tl->Flags |= 1u << i;
            }
        } else if (is(c, L"comInterfaceProxyStub")) {
            SXS_INTERFACE *itfs = pool_append(pool, f->Interfaces, &f->InterfaceCount, sizeof(*itfs));
            if (!itfs)
                return STATUS_NO_MEMORY;
            f->Interfaces = itfs;
            NTSTATUS status = read_interface(pool, c, &itfs[f->InterfaceCount - 1]);
            if (!NT_SUCCESS(status))
                return status;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS read_dependency(PVOID *pool, const XML_NODE *n, SXS_MANIFEST *m)
{
    BOOLEAN optional = yes(attr(n, L"optional"));
    for (const XML_NODE *d = n->First; d; d = d->Next) {
        if (!is_asm_ns(d->Ns) || !is(d, L"dependentAssembly"))
            continue;
        /* installation dependencies are for the servicing stack, not for activation. */
        PCWSTR type = attr(d, L"dependencyType");
        if (type && !_wcsicmp(type, L"install"))
            continue;
        for (const XML_NODE *i = d->First; i; i = i->Next) {
            if (!is_asm_ns(i->Ns) || !is(i, L"assemblyIdentity"))
                continue;
            SXS_DEPENDENCY *deps = pool_append(pool, m->Dependencies, &m->DependencyCount, sizeof(*deps));
            if (!deps)
                return STATUS_NO_MEMORY;
            m->Dependencies = deps;
            SXS_DEPENDENCY *dep = &deps[m->DependencyCount - 1];
            dep->Optional = optional;
            NTSTATUS status = read_identity(pool, i, &dep->Identity, TRUE);
            if (!NT_SUCCESS(status))
                return status;
            if (!dep->Identity.HasVersion)
                return STATUS_SXS_CANT_GEN_ACTCTX;
            break;
        }
    }
    return STATUS_SUCCESS;
}

static void read_trust(const XML_NODE *n, SXS_MANIFEST *m)
{
    for (const XML_NODE *s = n->First; s; s = s->Next) {
        if (!is_asm_ns(s->Ns))
            continue;
        if (is(s, L"security") || is(s, L"requestedPrivileges")) {
            read_trust(s, m);
        } else if (is(s, L"requestedExecutionLevel")) {
            PCWSTR level = attr(s, L"level");
            if (level && !wcscmp(level, L"asInvoker"))
                m->RunLevel = ACTCTX_RUN_LEVEL_AS_INVOKER;
            else if (level && !wcscmp(level, L"highestAvailable"))
                m->RunLevel = ACTCTX_RUN_LEVEL_HIGHEST_AVAILABLE;
            else if (level && !wcscmp(level, L"requireAdministrator"))
                m->RunLevel = ACTCTX_RUN_LEVEL_REQUIRE_ADMIN;
            m->UiAccess = yes(attr(s, L"uiAccess"));
        }
    }
}

static NTSTATUS read_settings(PVOID *pool, const XML_NODE *n, SXS_MANIFEST *m)
{
    for (const XML_NODE *w = n->First; w; w = w->Next) {
        /* manifests often put windowsSettings itself in a settings namespace: its name decides. */
        if (!is(w, L"windowsSettings"))
            continue;
        for (const XML_NODE *s = w->First; s; s = s->Next) {
            SXS_SETTING *settings = pool_append(pool, m->Settings, &m->SettingCount, sizeof(*settings));
            if (!settings)
                return STATUS_NO_MEMORY;
            m->Settings = settings;
            SXS_SETTING *setting = &settings[m->SettingCount - 1];
            setting->Namespace = dup(pool, s->Ns ? s->Ns : L"");
            setting->Name = dup(pool, s->Name);
            setting->Value = pool_strndup(pool, s->Text ? s->Text : L"", s->TextChars);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS add_compatibility(PVOID *pool, SXS_MANIFEST *m, const GUID *id, ULONG type, ULONGLONG version)
{
    SXS_COMPATIBILITY *items = pool_append(pool, m->Compatibility, &m->CompatibilityCount, sizeof(*items));
    if (!items)
        return STATUS_NO_MEMORY;
    m->Compatibility = items;
    items[m->CompatibilityCount - 1] = (SXS_COMPATIBILITY){*id, type, version};
    return STATUS_SUCCESS;
}

static NTSTATUS read_compatibility(PVOID *pool, const XML_NODE *n, SXS_MANIFEST *m)
{
    for (const XML_NODE *a = n->First; a; a = a->Next) {
        if (!ns_is(a->Ns, NS_COMPATIBILITY) || !is(a, L"application"))
            continue;
        for (const XML_NODE *e = a->First; e; e = e->Next) {
            if (!ns_is(e->Ns, NS_COMPATIBILITY))
                continue;
            GUID id = {};
            NTSTATUS status = STATUS_SUCCESS;
            if (is(e, L"supportedOS")) {
                if (!parse_guid(attr(e, L"Id"), &id))
                    return STATUS_SXS_CANT_GEN_ACTCTX;
                status = add_compatibility(pool, m, &id, ACTCTX_COMPATIBILITY_ELEMENT_TYPE_OS, 0);
            } else if (is(e, L"maxversiontested")) {
                USHORT v[4];
                if (!SxspParseVersion(attr(e, L"Id"), v))
                    return STATUS_SXS_CANT_GEN_ACTCTX;
                ULONGLONG packed = (ULONGLONG)v[0] << 48 | (ULONGLONG)v[1] << 32 | (ULONGLONG)v[2] << 16 | v[3];
                status = add_compatibility(pool, m, &id, ACTCTX_COMPATIBILITY_ELEMENT_TYPE_MAXVERSIONTESTED, packed);
            }
            if (!NT_SUCCESS(status))
                return status;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS interpret(PVOID *pool, const XML_NODE *root, SXS_MANIFEST *m)
{
    if (!is(root, L"assembly") || (root->Ns && !ns_is(root->Ns, NS_ASM_V1)))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    for (const XML_NODE *n = root->First; n; n = n->Next) {
        NTSTATUS status = STATUS_SUCCESS;
        if (ns_is(n->Ns, NS_COMPATIBILITY) && is(n, L"compatibility")) {
            status = read_compatibility(pool, n, m);
        } else if (!n->Ns || is_asm_ns(n->Ns)) {
            if (is(n, L"assemblyIdentity")) {
                if (m->HasIdentity)
                    return STATUS_SXS_CANT_GEN_ACTCTX;
                m->HasIdentity = TRUE;
                status = read_identity(pool, n, &m->Identity, FALSE);
            } else if (is(n, L"noInherit")) {
                m->NoInherit = TRUE;
            } else if (is(n, L"dependency")) {
                status = read_dependency(pool, n, m);
            } else if (is(n, L"file")) {
                status = read_file(pool, n, m);
            } else if (is(n, L"comInterfaceExternalProxyStub")) {
                SXS_INTERFACE *itfs =
                    pool_append(pool, m->ExternalProxies, &m->ExternalProxyCount, sizeof(SXS_INTERFACE));
                if (!itfs)
                    return STATUS_NO_MEMORY;
                m->ExternalProxies = itfs;
                status = read_interface(pool, n, &itfs[m->ExternalProxyCount - 1]);
            } else if (is(n, L"trustInfo")) {
                read_trust(n, m);
            } else if (is(n, L"application")) {
                status = read_settings(pool, n, m);
            }
        }
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS SxspParseManifest(const void *bytes, SIZE_T size, SXS_MANIFEST *manifest)
{
    memset(manifest, 0, sizeof(*manifest));
    PWSTR text;
    SIZE_T chars;
    NTSTATUS status = decode(bytes, size, &text, &chars);
    if (!NT_SUCCESS(status))
        return status;
    XML_NODE *root;
    status = parse_document(text, chars, &manifest->Pool, &root);
    if (NT_SUCCESS(status))
        status = interpret(&manifest->Pool, root, manifest);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, text);
    if (!NT_SUCCESS(status)) {
        DbgPrint("sxs: manifest rejected (%08x)\n", status);
        SxspFreeManifest(manifest);
    }
    return status;
}

void SxspFreeManifest(SXS_MANIFEST *manifest)
{
    pool_free(manifest->Pool);
    memset(manifest, 0, sizeof(*manifest));
}
