/*
 * Windows.Data.Xml.Dom, the object model toasts are written in: a small parser for elements, attributes, text,
 * sections, comments, instructions and the predefined and numeric entities, the node interfaces, and a serializer.
 * left out: namespaces beyond names as written, document types, entity declarations and storage stream buffers.
 *
 * a document's nodes live in a context. references are counted per node; a node's first reference holds the context,
 * and the last reference of the last node frees every node.
 */
#include "xmldom.h"

#define XML_E_INVALID_TOP ((HRESULT)0xC00CE556)
#define XML_E_UNEXPECTED_END ((HRESULT)0xC00CE558)
#define XML_E_SYNTAX ((HRESULT)0xC00CE508)
#define XML_E_DEPTH ((HRESULT)0xC00CE55D)
#define MAX_DEPTH 256

const WCHAR CbXmlDocumentClass[] = L"Windows.Data.Xml.Dom.XmlDocument";
static const WCHAR g_element_class[] = L"Windows.Data.Xml.Dom.XmlElement";
static const WCHAR g_attribute_class[] = L"Windows.Data.Xml.Dom.XmlAttribute";
static const WCHAR g_text_class[] = L"Windows.Data.Xml.Dom.XmlText";
static const WCHAR g_cdata_class[] = L"Windows.Data.Xml.Dom.XmlCDataSection";
static const WCHAR g_comment_class[] = L"Windows.Data.Xml.Dom.XmlComment";
static const WCHAR g_pi_class[] = L"Windows.Data.Xml.Dom.XmlProcessingInstruction";
static const WCHAR g_fragment_class[] = L"Windows.Data.Xml.Dom.XmlDocumentFragment";
static const WCHAR g_list_class[] = L"Windows.Data.Xml.Dom.XmlNodeList";
static const WCHAR g_map_class[] = L"Windows.Data.Xml.Dom.XmlNamedNodeMap";

struct XCTX {
    volatile LONG Refs;
    XNODE *Nodes;
    XNODE *Document;
};

static SRWLOCK g_lock = SRWLOCK_INIT;

static const WRT_CLASS document_class, element_class, attribute_class, text_class, cdata_class, comment_class,
    pi_class, fragment_class;

/* ------------------------------------------------------------------ strings */

static WCHAR *dup_n(const WCHAR *text, SIZE_T length)
{
    WCHAR *out = CbAlloc((length + 1) * sizeof(WCHAR));
    if (out) {
        if (length)
            memcpy(out, text, length * sizeof(WCHAR));
        out[length] = 0;
    }
    return out;
}


static WCHAR *dup_hstring(HSTRING string)
{
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(string, &length);
    return dup_n(raw, length);
}

typedef struct SB {
    WCHAR *Text;
    SIZE_T Length, Capacity;
    BOOL Failed;
} SB;

static void sb_put_n(SB *sb, const WCHAR *text, SIZE_T length)
{
    if (sb->Failed)
        return;
    if (sb->Length + length + 1 > sb->Capacity) {
        SIZE_T capacity = sb->Capacity ? sb->Capacity * 2 : 256;
        while (capacity < sb->Length + length + 1)
            capacity *= 2;
        WCHAR *grown = CbAlloc(capacity * sizeof(WCHAR));
        if (!grown) {
            sb->Failed = TRUE;
            return;
        }
        if (sb->Length)
            memcpy(grown, sb->Text, sb->Length * sizeof(WCHAR));
        CbFree(sb->Text);
        sb->Text = grown;
        sb->Capacity = capacity;
    }
    if (length)
        memcpy(sb->Text + sb->Length, text, length * sizeof(WCHAR));
    sb->Length += length;
    sb->Text[sb->Length] = 0;
}

static void sb_put(SB *sb, const WCHAR *text)
{
    sb_put_n(sb, text, wcslen(text));
}

static void sb_char(SB *sb, WCHAR c)
{
    sb_put_n(sb, &c, 1);
}

/* hands over the builder's text; an empty one is a real empty string, not NULL. */
static WCHAR *sb_take(SB *sb, SIZE_T *length)
{
    if (sb->Failed) {
        CbFree(sb->Text);
        return nullptr;
    }
    if (!sb->Text)
        sb->Text = dup_n(L"", 0);
    if (length)
        *length = sb->Length;
    return sb->Text;
}

static void sb_escape(SB *sb, const WCHAR *text, BOOL attribute)
{
    for (; *text; text++) {
        switch (*text) {
        case L'&':
            sb_put(sb, L"&amp;");
            break;
        case L'<':
            sb_put(sb, L"&lt;");
            break;
        case L'>':
            sb_put(sb, L"&gt;");
            break;
        case L'"':
            if (attribute)
                sb_put(sb, L"&quot;");
            else
                sb_char(sb, *text);
            break;
        default:
            sb_char(sb, *text);
        }
    }
}

static HRESULT make_hstring(const WCHAR *text, HSTRING *out)
{
    return WindowsCreateString(text, (UINT32)wcslen(text), out);
}

/* ------------------------------------------------------------------ contexts and nodes */

static void ctx_release(XCTX *ctx)
{
    if (InterlockedDecrement(&ctx->Refs))
        return;
    XNODE *n = ctx->Nodes;
    while (n) {
        XNODE *next = n->CtxNext;
        CbFree(n->Name);
        CbFree(n->Value);
        CbFree(n->Uri);
        CbFree(n);
        n = next;
    }
    CbFree(ctx);
}

ULONG CbXmlAddRef(XNODE *node)
{
    LONG refs = InterlockedIncrement(&node->Base.Refs);
    if (refs == 1)
        InterlockedIncrement(&node->Ctx->Refs);
    return (ULONG)refs;
}

ULONG CbXmlRelease(XNODE *node)
{
    XCTX *ctx = node->Ctx;
    LONG refs = InterlockedDecrement(&node->Base.Refs);
    if (!refs)
        ctx_release(ctx);
    return (ULONG)refs;
}

static const WRT_CLASS *class_of(NodeType type)
{
    switch (type) {
    case NodeType_DocumentNode:
        return &document_class;
    case NodeType_ElementNode:
        return &element_class;
    case NodeType_AttributeNode:
        return &attribute_class;
    case NodeType_TextNode:
        return &text_class;
    case NodeType_DataSectionNode:
        return &cdata_class;
    case NodeType_CommentNode:
        return &comment_class;
    case NodeType_ProcessingInstructionNode:
        return &pi_class;
    default:
        return &fragment_class;
    }
}

static const void *const *vtables_of(NodeType type);

/* a new node in the context, not referenced yet. */
static XNODE *node_new(XCTX *ctx, NodeType type, const WCHAR *name, SIZE_T name_length, const WCHAR *value,
                       SIZE_T value_length)
{
    XNODE *n = CbAlloc(sizeof(*n));
    if (!n)
        return nullptr;
    memset(n, 0, sizeof(*n));
    n->Ctx = ctx;
    n->Type = type;
    if (name) {
        n->Name = dup_n(name, name_length);
        if (!n->Name) {
            CbFree(n);
            return nullptr;
        }
    }
    if (value) {
        n->Value = dup_n(value, value_length);
        if (!n->Value) {
            CbFree(n->Name);
            CbFree(n);
            return nullptr;
        }
    }
    WrtInit(&n->Base, class_of(type), vtables_of(type));
    n->Base.Refs = 0;
    AcquireSRWLockExclusive(&g_lock);
    n->CtxNext = ctx->Nodes;
    ctx->Nodes = n;
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}

static void unlink_child(XNODE *n)
{
    XNODE *parent = n->Parent;
    if (!parent)
        return;
    if (n->Type == NodeType_AttributeNode) {
        for (XNODE **link = &parent->Attrs; *link; link = &(*link)->AttrNext)
            if (*link == n) {
                *link = n->AttrNext;
                break;
            }
        n->AttrNext = nullptr;
    } else {
        if (n->Prev)
            n->Prev->Next = n->Next;
        else
            parent->First = n->Next;
        if (n->Next)
            n->Next->Prev = n->Prev;
        else
            parent->Last = n->Prev;
        n->Prev = n->Next = nullptr;
    }
    n->Parent = nullptr;
}

/* inserts n before reference under parent, or at the end for a null reference. */
static void link_child(XNODE *parent, XNODE *n, XNODE *reference)
{
    n->Parent = parent;
    n->Next = reference;
    n->Prev = reference ? reference->Prev : parent->Last;
    if (n->Prev)
        n->Prev->Next = n;
    else
        parent->First = n;
    if (reference)
        reference->Prev = n;
    else
        parent->Last = n;
}

static void link_attribute(XNODE *element, XNODE *attribute)
{
    attribute->Parent = element;
    attribute->AttrNext = nullptr;
    XNODE **link = &element->Attrs;
    while (*link)
        link = &(*link)->AttrNext;
    *link = attribute;
}

/*
 * like Windows' model, an attribute's only child is a text node holding its value, which programs read it through. the
 * node is made on first access and tracks the value. an empty value has no child, which is a guess.
 */
static void attribute_text(XNODE *a)
{
    if (a->Type != NodeType_AttributeNode)
        return;
    const WCHAR *value = a->Value ? a->Value : L"";
    XNODE *t = a->First;
    if (!value[0]) {
        if (t)
            unlink_child(t);
        return;
    }
    if (!t) {
        XNODE *made = node_new(a->Ctx, NodeType_TextNode, nullptr, 0, value, wcslen(value));
        if (!made)
            return;
        AcquireSRWLockExclusive(&g_lock);
        if (!a->First)
            link_child(a, made, nullptr);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (t->Value && !wcscmp(t->Value, value))
        return;
    WCHAR *copy = dup_n(value, wcslen(value));
    if (!copy)
        return;
    CbFree(t->Value);
    t->Value = copy;
}

/* without adding a reference. */
static WRT_IFACE *find_iface(XNODE *n, REFIID riid)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IInspectable) || IsEqualIID(riid, &IID_IAgileObject))
        return &n->Base.Ifaces[0];
    for (ULONG i = 0; i < n->Base.Class->Count; i++)
        if (IsEqualIID(riid, n->Base.Class->Iids[i]))
            return &n->Base.Ifaces[i];
    return nullptr;
}

/* adds a reference for the program. */
static HRESULT node_interface(XNODE *n, REFIID riid, void **out)
{
    if (!n) {
        *out = nullptr;
        return S_OK;
    }
    WRT_IFACE *found = find_iface(n, riid);
    if (!found) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    CbXmlAddRef(n);
    *out = found;
    return S_OK;
}

static XNODE *node_of(void *iface)
{
    return (XNODE *)((WRT_IFACE *)iface)->Owner;
}

static const IXmlNodeVtbl node_vtbl;

XNODE *CbXmlFromInterface(IUnknown *object)
{
    if (!object)
        return nullptr;
    IXmlNode *node;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IXmlNode, (void **)&node)))
        return nullptr;
    /* only the nodes of this implementation share the vtable, and have a node behind the interface. */
    XNODE *n = node->lpVtbl == &node_vtbl ? node_of(node) : nullptr;
    node->lpVtbl->Release(node);
    return n;
}

/* ------------------------------------------------------------------ names and text */

static BOOL name_start(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c == L':' || c >= 0x80;
}

static BOOL name_char(WCHAR c)
{
    return name_start(c) || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.';
}

static BOOL valid_name(const WCHAR *name)
{
    if (!name_start(*name))
        return FALSE;
    for (; *name; name++)
        if (!name_char(*name))
            return FALSE;
    return TRUE;
}

static BOOL is_space(WCHAR c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

WCHAR *CbXmlInnerText(XNODE *node)
{
    SB sb = {};
    switch (node->Type) {
    case NodeType_TextNode:
    case NodeType_DataSectionNode:
    case NodeType_CommentNode:
    case NodeType_AttributeNode:
    case NodeType_ProcessingInstructionNode:
        if (node->Value)
            sb_put(&sb, node->Value);
        break;
    default: {
        /* every text under the node, in document order. */
        XNODE *n = node->First;
        while (n) {
            if (n->Type == NodeType_TextNode || n->Type == NodeType_DataSectionNode)
                sb_put(&sb, n->Value ? n->Value : L"");
            if (n->First) {
                n = n->First;
                continue;
            }
            while (n && n != node && !n->Next)
                n = n->Parent;
            if (!n || n == node)
                break;
            n = n->Next;
        }
    }
    }
    return sb_take(&sb, nullptr);
}

/* ------------------------------------------------------------------ serializer */

static void serialize(SB *sb, XNODE *n)
{
    switch (n->Type) {
    case NodeType_DocumentNode:
    case NodeType_DocumentFragmentNode:
        for (XNODE *c = n->First; c; c = c->Next)
            serialize(sb, c);
        break;
    case NodeType_ElementNode:
        sb_char(sb, L'<');
        sb_put(sb, n->Name);
        for (XNODE *a = n->Attrs; a; a = a->AttrNext) {
            sb_char(sb, L' ');
            sb_put(sb, a->Name);
            sb_put(sb, L"=\"");
            sb_escape(sb, a->Value ? a->Value : L"", TRUE);
            sb_char(sb, L'"');
        }
        if (!n->First) {
            sb_put(sb, L"/>");
        } else {
            sb_char(sb, L'>');
            for (XNODE *c = n->First; c; c = c->Next)
                serialize(sb, c);
            sb_put(sb, L"</");
            sb_put(sb, n->Name);
            sb_char(sb, L'>');
        }
        break;
    case NodeType_AttributeNode:
        sb_put(sb, n->Name);
        sb_put(sb, L"=\"");
        sb_escape(sb, n->Value ? n->Value : L"", TRUE);
        sb_char(sb, L'"');
        break;
    case NodeType_TextNode:
        sb_escape(sb, n->Value ? n->Value : L"", FALSE);
        break;
    case NodeType_DataSectionNode:
        sb_put(sb, L"<![CDATA[");
        sb_put(sb, n->Value ? n->Value : L"");
        sb_put(sb, L"]]>");
        break;
    case NodeType_CommentNode:
        sb_put(sb, L"<!--");
        sb_put(sb, n->Value ? n->Value : L"");
        sb_put(sb, L"-->");
        break;
    case NodeType_ProcessingInstructionNode:
        sb_put(sb, L"<?");
        sb_put(sb, n->Name);
        if (n->Value && n->Value[0]) {
            sb_char(sb, L' ');
            sb_put(sb, n->Value);
        }
        sb_put(sb, L"?>");
        break;
    default:
        break;
    }
}

WCHAR *CbXmlSerialize(XNODE *node, SIZE_T *length)
{
    SB sb = {};
    serialize(&sb, node);
    return sb_take(&sb, length);
}

/* ------------------------------------------------------------------ parser */

typedef struct PARSER {
    const WCHAR *At, *End;
    XCTX *Ctx;
    int Depth;
} PARSER;

static BOOL looking_at(PARSER *p, const WCHAR *text)
{
    SIZE_T n = wcslen(text);
    return (SIZE_T)(p->End - p->At) >= n && !wcsncmp(p->At, text, n);
}

static void skip_space(PARSER *p)
{
    while (p->At < p->End && is_space(*p->At))
        p->At++;
}

static BOOL parse_name(PARSER *p, const WCHAR **name, SIZE_T *length)
{
    const WCHAR *start = p->At;
    if (p->At >= p->End || !name_start(*p->At))
        return FALSE;
    while (p->At < p->End && name_char(*p->At))
        p->At++;
    *name = start;
    *length = (SIZE_T)(p->At - start);
    return TRUE;
}

static void put_code_point(SB *sb, ULONG code)
{
    if (code >= 0x10000) {
        code -= 0x10000;
        sb_char(sb, (WCHAR)(0xD800 + (code >> 10)));
        sb_char(sb, (WCHAR)(0xDC00 + (code & 0x3FF)));
    } else {
        sb_char(sb, (WCHAR)code);
    }
}

/* resolves the reference at p, which starts with &, into sb. */
static HRESULT parse_reference(PARSER *p, SB *sb)
{
    const WCHAR *at = p->At + 1;
    const WCHAR *semi = at;
    while (semi < p->End && *semi != L';' && semi - at < 12)
        semi++;
    if (semi >= p->End || *semi != L';')
        return XML_E_SYNTAX;
    SIZE_T n = (SIZE_T)(semi - at);
    if (n == 2 && !wcsncmp(at, L"lt", 2))
        sb_char(sb, L'<');
    else if (n == 2 && !wcsncmp(at, L"gt", 2))
        sb_char(sb, L'>');
    else if (n == 3 && !wcsncmp(at, L"amp", 3))
        sb_char(sb, L'&');
    else if (n == 4 && !wcsncmp(at, L"quot", 4))
        sb_char(sb, L'"');
    else if (n == 4 && !wcsncmp(at, L"apos", 4))
        sb_char(sb, L'\'');
    else if (n >= 2 && at[0] == L'#') {
        ULONG code = 0;
        BOOL hex = at[1] == L'x';
        const WCHAR *d = at + (hex ? 2 : 1);
        if (d == semi)
            return XML_E_SYNTAX;
        for (; d < semi; d++) {
            ULONG digit;
            if (*d >= L'0' && *d <= L'9')
                digit = (ULONG)(*d - L'0');
            else if (hex && *d >= L'a' && *d <= L'f')
                digit = (ULONG)(*d - L'a' + 10);
            else if (hex && *d >= L'A' && *d <= L'F')
                digit = (ULONG)(*d - L'A' + 10);
            else
                return XML_E_SYNTAX;
            code = code * (hex ? 16 : 10) + digit;
            if (code > 0x10FFFF)
                return XML_E_SYNTAX;
        }
        if (!code || (code >= 0xD800 && code < 0xE000))
            return XML_E_SYNTAX;
        put_code_point(sb, code);
    } else {
        return XML_E_SYNTAX;
    }
    p->At = semi + 1;
    return S_OK;
}

static HRESULT parse_attribute_value(PARSER *p, WCHAR **value)
{
    if (p->At >= p->End || (*p->At != L'"' && *p->At != L'\''))
        return XML_E_SYNTAX;
    WCHAR quote = *p->At++;
    SB sb = {};
    for (;;) {
        if (p->At >= p->End) {
            CbFree(sb.Text);
            return XML_E_UNEXPECTED_END;
        }
        WCHAR c = *p->At;
        if (c == quote) {
            p->At++;
            break;
        }
        if (c == L'<') {
            CbFree(sb.Text);
            return XML_E_SYNTAX;
        }
        if (c == L'&') {
            HRESULT hr = parse_reference(p, &sb);
            if (FAILED(hr)) {
                CbFree(sb.Text);
                return hr;
            }
            continue;
        }
        /* white space in a value becomes a space. */
        sb_char(&sb, is_space(c) ? L' ' : c);
        p->At++;
    }
    *value = sb_take(&sb, nullptr);
    return *value ? S_OK : E_OUTOFMEMORY;
}

/* text up to the next tag, with its references resolved. */
static HRESULT parse_text(PARSER *p, XNODE *parent)
{
    SB sb = {};
    while (p->At < p->End && *p->At != L'<') {
        if (*p->At == L'&') {
            HRESULT hr = parse_reference(p, &sb);
            if (FAILED(hr)) {
                CbFree(sb.Text);
                return hr;
            }
            continue;
        }
        if (looking_at(p, L"]]>")) {
            CbFree(sb.Text);
            return XML_E_SYNTAX;
        }
        /* a line break in text becomes one line feed. */
        if (*p->At == L'\r') {
            sb_char(&sb, L'\n');
            p->At++;
            if (p->At < p->End && *p->At == L'\n')
                p->At++;
            continue;
        }
        sb_char(&sb, *p->At++);
    }
    SIZE_T length;
    WCHAR *text = sb_take(&sb, &length);
    if (!text)
        return E_OUTOFMEMORY;
    XNODE *n = node_new(p->Ctx, NodeType_TextNode, nullptr, 0, text, length);
    CbFree(text);
    if (!n)
        return E_OUTOFMEMORY;
    link_child(parent, n, nullptr);
    return S_OK;
}

static HRESULT parse_markup_decl(PARSER *p, XNODE *parent)
{
    if (looking_at(p, L"<!--")) {
        const WCHAR *start = p->At + 4, *at = start;
        while (at + 2 < p->End && !(at[0] == L'-' && at[1] == L'-' && at[2] == L'>')) {
            if (at[0] == L'-' && at[1] == L'-')
                return XML_E_SYNTAX;
            at++;
        }
        if (at + 2 >= p->End)
            return XML_E_UNEXPECTED_END;
        XNODE *n = node_new(p->Ctx, NodeType_CommentNode, nullptr, 0, start, (SIZE_T)(at - start));
        if (!n)
            return E_OUTOFMEMORY;
        link_child(parent, n, nullptr);
        p->At = at + 3;
        return S_OK;
    }
    if (looking_at(p, L"<![CDATA[")) {
        const WCHAR *start = p->At + 9, *at = start;
        while (at + 2 < p->End && !(at[0] == L']' && at[1] == L']' && at[2] == L'>'))
            at++;
        if (at + 2 >= p->End)
            return XML_E_UNEXPECTED_END;
        XNODE *n = node_new(p->Ctx, NodeType_DataSectionNode, nullptr, 0, start, (SIZE_T)(at - start));
        if (!n)
            return E_OUTOFMEMORY;
        link_child(parent, n, nullptr);
        p->At = at + 3;
        return S_OK;
    }
    return XML_E_SYNTAX;
}

static HRESULT parse_instruction(PARSER *p, XNODE *parent)
{
    p->At += 2;
    const WCHAR *name;
    SIZE_T name_length;
    if (!parse_name(p, &name, &name_length))
        return XML_E_SYNTAX;
    skip_space(p);
    const WCHAR *start = p->At, *at = start;
    while (at + 1 < p->End && !(at[0] == L'?' && at[1] == L'>'))
        at++;
    if (at + 1 >= p->End)
        return XML_E_UNEXPECTED_END;
    XNODE *n = node_new(p->Ctx, NodeType_ProcessingInstructionNode, name, name_length, start, (SIZE_T)(at - start));
    if (!n)
        return E_OUTOFMEMORY;
    link_child(parent, n, nullptr);
    p->At = at + 2;
    return S_OK;
}

/* document types are skipped. */
static HRESULT skip_doctype(PARSER *p)
{
    int brackets = 0;
    p->At += 9;
    while (p->At < p->End) {
        WCHAR c = *p->At++;
        if (c == L'[')
            brackets++;
        else if (c == L']')
            brackets--;
        else if (c == L'>' && brackets <= 0)
            return S_OK;
    }
    return XML_E_UNEXPECTED_END;
}

static HRESULT parse_element(PARSER *p, XNODE *parent)
{
    /* iterative: the open elements are the parent chain. */
    XNODE *current = parent;
    int depth_at_entry = p->Depth;
    for (;;) {
        if (p->At >= p->End) {
            return XML_E_UNEXPECTED_END;
        }
        if (*p->At != L'<') {
            HRESULT hr = parse_text(p, current);
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (looking_at(p, L"</")) {
            p->At += 2;
            const WCHAR *name;
            SIZE_T length;
            if (!parse_name(p, &name, &length))
                return XML_E_SYNTAX;
            skip_space(p);
            if (p->At >= p->End)
                return XML_E_UNEXPECTED_END;
            if (*p->At != L'>')
                return XML_E_SYNTAX;
            p->At++;
            if (current == parent || wcslen(current->Name) != length || wcsncmp(current->Name, name, length))
                return XML_E_SYNTAX;
            current = current->Parent;
            p->Depth--;
            if (p->Depth == depth_at_entry)
                return S_OK;
            continue;
        }
        if (looking_at(p, L"<!")) {
            HRESULT hr = parse_markup_decl(p, current);
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (looking_at(p, L"<?")) {
            HRESULT hr = parse_instruction(p, current);
            if (FAILED(hr))
                return hr;
            continue;
        }
        /* a start tag. */
        p->At++;
        const WCHAR *name;
        SIZE_T length;
        if (!parse_name(p, &name, &length))
            return XML_E_SYNTAX;
        if (p->Depth - depth_at_entry >= MAX_DEPTH)
            return XML_E_DEPTH;
        XNODE *element = node_new(p->Ctx, NodeType_ElementNode, name, length, nullptr, 0);
        if (!element)
            return E_OUTOFMEMORY;
        link_child(current, element, nullptr);
        for (;;) {
            BOOL space = p->At < p->End && is_space(*p->At);
            skip_space(p);
            if (p->At >= p->End)
                return XML_E_UNEXPECTED_END;
            if (*p->At == L'>' || looking_at(p, L"/>"))
                break;
            if (!space)
                return XML_E_SYNTAX;
            const WCHAR *attr;
            SIZE_T attr_length;
            if (!parse_name(p, &attr, &attr_length))
                return XML_E_SYNTAX;
            skip_space(p);
            if (p->At >= p->End)
                return XML_E_UNEXPECTED_END;
            if (*p->At != L'=')
                return XML_E_SYNTAX;
            p->At++;
            skip_space(p);
            WCHAR *value;
            HRESULT hr = parse_attribute_value(p, &value);
            if (FAILED(hr))
                return hr;
            WCHAR *key = dup_n(attr, attr_length);
            BOOL duplicate = key && CbXmlAttribute(element, key);
            CbFree(key);
            if (duplicate) {
                CbFree(value);
                return XML_E_SYNTAX;
            }
            XNODE *a = node_new(p->Ctx, NodeType_AttributeNode, attr, attr_length, value, wcslen(value));
            CbFree(value);
            if (!a)
                return E_OUTOFMEMORY;
            link_attribute(element, a);
        }
        if (*p->At == L'/') {
            p->At += 2;
            if (p->Depth == depth_at_entry)
                return S_OK;
            continue;
        }
        p->At++;
        current = element;
        p->Depth++;
    }
}

static void clear_children(XNODE *node)
{
    while (node->First)
        unlink_child(node->First);
}

HRESULT CbXmlLoadText(XNODE *document, const WCHAR *text, SIZE_T length)
{
    PARSER p = {.At = text, .End = text + length, .Ctx = document->Ctx};
    if (p.At < p.End && *p.At == 0xFEFF)
        p.At++;
    /* the new tree is built beside the old one and replaces it when it is whole. */
    XNODE *scratch = node_new(document->Ctx, NodeType_DocumentNode, nullptr, 0, nullptr, 0);
    if (!scratch)
        return E_OUTOFMEMORY;
    BOOL root = FALSE;
    HRESULT hr = S_OK;
    while (SUCCEEDED(hr)) {
        skip_space(&p);
        if (p.At >= p.End)
            break;
        if (*p.At != L'<') {
            hr = XML_E_INVALID_TOP;
            break;
        }
        if (looking_at(&p, L"<?")) {
            hr = parse_instruction(&p, scratch);
        } else if (looking_at(&p, L"<!--")) {
            hr = parse_markup_decl(&p, scratch);
        } else if (looking_at(&p, L"<!DOCTYPE")) {
            hr = skip_doctype(&p);
        } else if (looking_at(&p, L"<!")) {
            hr = XML_E_INVALID_TOP;
        } else if (root) {
            hr = XML_E_INVALID_TOP;
        } else {
            root = TRUE;
            hr = parse_element(&p, scratch);
        }
    }
    if (SUCCEEDED(hr) && !root)
        hr = p.At >= p.End && scratch->First ? XML_E_INVALID_TOP : XML_E_UNEXPECTED_END;
    if (FAILED(hr)) {
        /* the scratch document stays unused in the context until the context goes. */
        return hr;
    }
    clear_children(document);
    while (scratch->First) {
        XNODE *n = scratch->First;
        unlink_child(n);
        link_child(document, n, nullptr);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ the tree */

XNODE *CbXmlRootElement(XNODE *document)
{
    for (XNODE *n = document->First; n; n = n->Next)
        if (n->Type == NodeType_ElementNode)
            return n;
    return nullptr;
}

XNODE *CbXmlAttribute(XNODE *element, const WCHAR *name)
{
    for (XNODE *a = element->Attrs; a; a = a->AttrNext)
        if (!wcscmp(a->Name, name))
            return a;
    return nullptr;
}

static BOOL is_ancestor(XNODE *maybe, XNODE *n)
{
    for (; n; n = n->Parent)
        if (n == maybe)
            return TRUE;
    return FALSE;
}

/* a detached copy in ctx. */
static XNODE *clone_into(XCTX *ctx, XNODE *src, BOOL deep)
{
    XNODE *n = node_new(ctx, src->Type, src->Name, src->Name ? wcslen(src->Name) : 0, src->Value,
                        src->Value ? wcslen(src->Value) : 0);
    if (!n)
        return nullptr;
    for (XNODE *a = src->Attrs; a; a = a->AttrNext) {
        XNODE *copy = clone_into(ctx, a, FALSE);
        if (!copy)
            return nullptr;
        link_attribute(n, copy);
    }
    if (deep)
        for (XNODE *c = src->First; c; c = c->Next) {
            XNODE *copy = clone_into(ctx, c, TRUE);
            if (!copy)
                return nullptr;
            link_child(n, copy, nullptr);
        }
    return n;
}

static BOOL may_hold(XNODE *parent, XNODE *child)
{
    switch (child->Type) {
    case NodeType_ElementNode:
        return parent->Type != NodeType_DocumentNode || !CbXmlRootElement(parent);
    case NodeType_TextNode:
    case NodeType_DataSectionNode:
        return parent->Type == NodeType_ElementNode || parent->Type == NodeType_DocumentFragmentNode;
    case NodeType_CommentNode:
    case NodeType_ProcessingInstructionNode:
        return parent->Type == NodeType_ElementNode || parent->Type == NodeType_DocumentNode ||
               parent->Type == NodeType_DocumentFragmentNode;
    case NodeType_DocumentFragmentNode:
        return TRUE;
    default:
        return FALSE;
    }
}

/* the node itself, a copy of another document's node, or null when it cannot be linked. */
static XNODE *adopt(XNODE *parent, XNODE *child)
{
    if (child->Ctx == parent->Ctx)
        return child;
    return clone_into(parent->Ctx, child, TRUE);
}

static HRESULT insert_child(XNODE *parent, XNODE *child, XNODE *reference, XNODE **inserted)
{
    if (!child)
        return E_INVALIDARG;
    if (parent->Type != NodeType_ElementNode && parent->Type != NodeType_DocumentNode &&
        parent->Type != NodeType_DocumentFragmentNode)
        return E_FAIL;
    XNODE *use = adopt(parent, child);
    if (!use)
        return E_OUTOFMEMORY;
    if (reference && reference->Parent != parent)
        return E_INVALIDARG;
    if (use == reference)
        return E_INVALIDARG;
    if (is_ancestor(use, parent))
        return E_INVALIDARG;
    if (use->Type == NodeType_DocumentFragmentNode) {
        while (use->First) {
            XNODE *n = use->First;
            if (!may_hold(parent, n))
                return E_INVALIDARG;
            unlink_child(n);
            link_child(parent, n, reference);
        }
    } else {
        if (!may_hold(parent, use) && !(use->Parent == parent && use->Type == NodeType_ElementNode))
            return E_INVALIDARG;
        unlink_child(use);
        link_child(parent, use, reference);
    }
    *inserted = use;
    return S_OK;
}

/* ------------------------------------------------------------------ interfaces: nodes */

#define X_THUNKS(T)                                                                                                    \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        if (!object)                                                                                                   \
            return E_POINTER;                                                                                          \
        return node_interface(node_of(This), riid, object);                                                            \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return CbXmlAddRef(node_of(This));                                                                             \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return CbXmlRelease(node_of(This));                                                                            \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetIids(T *This, ULONG *count, IID **iids)                                    \
    {                                                                                                                  \
        return WrtGetIids((WRT_IFACE *)This, count, iids);                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetRuntimeClassName(T *This, HSTRING *name)                                   \
    {                                                                                                                  \
        return WrtGetRuntimeClassName((WRT_IFACE *)This, name);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTrustLevel(T *This, TrustLevel *level)                                     \
    {                                                                                                                  \
        return WrtGetTrustLevel((WRT_IFACE *)This, level);                                                             \
    }

X_THUNKS(IXmlNode)
X_THUNKS(IXmlNodeSerializer)
X_THUNKS(IXmlNodeSelector)
X_THUNKS(IXmlDocument)
X_THUNKS(IXmlDocumentIO)
X_THUNKS(IXmlDocumentIO2)
X_THUNKS(IXmlElement)
X_THUNKS(IXmlAttribute)
X_THUNKS(IXmlText)
X_THUNKS(IXmlCharacterData)
X_THUNKS(IXmlComment)
X_THUNKS(IXmlCDataSection)
X_THUNKS(IXmlDocumentFragment)

#define NODE_ENTRIES(T) T##_QueryInterface, T##_AddRef, T##_Release, T##_GetIids, T##_GetRuntimeClassName, T##_GetTrustLevel

static HRESULT out_node(XNODE *n, IXmlNode **out)
{
    return node_interface(n, &IID_IXmlNode, (void **)out);
}

static HRESULT out_string(const WCHAR *text, HSTRING *out)
{
    return make_hstring(text ? text : L"", out);
}

static HRESULT STDMETHODCALLTYPE node_get_value(IXmlNode *This, IInspectable **value)
{
    XNODE *n = node_of(This);
    *value = nullptr;
    switch (n->Type) {
    case NodeType_TextNode:
    case NodeType_DataSectionNode:
    case NodeType_CommentNode:
    case NodeType_AttributeNode:
    case NodeType_ProcessingInstructionNode:
        return CbBoxString(n->Value ? n->Value : L"", value);
    default:
        return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE node_put_value(IXmlNode *This, IInspectable *value)
{
    XNODE *n = node_of(This);
    switch (n->Type) {
    case NodeType_TextNode:
    case NodeType_DataSectionNode:
    case NodeType_CommentNode:
    case NodeType_AttributeNode:
    case NodeType_ProcessingInstructionNode: {
        WCHAR *text;
        HRESULT hr = CbUnboxString(value, &text);
        if (FAILED(hr))
            return hr;
        CbFree(n->Value);
        n->Value = text;
        return S_OK;
    }
    default:
        return E_FAIL;
    }
}

static HRESULT STDMETHODCALLTYPE node_get_type(IXmlNode *This, NodeType *value)
{
    *value = node_of(This)->Type;
    return S_OK;
}

static const WCHAR *node_name(XNODE *n)
{
    switch (n->Type) {
    case NodeType_DocumentNode:
        return L"#document";
    case NodeType_TextNode:
        return L"#text";
    case NodeType_DataSectionNode:
        return L"#cdata-section";
    case NodeType_CommentNode:
        return L"#comment";
    case NodeType_DocumentFragmentNode:
        return L"#document-fragment";
    default:
        return n->Name;
    }
}

static HRESULT STDMETHODCALLTYPE node_get_name(IXmlNode *This, HSTRING *value)
{
    return out_string(node_name(node_of(This)), value);
}

static HRESULT STDMETHODCALLTYPE node_get_parent(IXmlNode *This, IXmlNode **value)
{
    XNODE *p = node_of(This)->Parent;
    return out_node(p, value);
}

typedef struct XLIST {
    WRT_OBJECT Base;
    XNODE *Owner; /* a live list of its children. */
    XNODE **Items; /* or a fixed set. */
    UINT32 Count;
} XLIST;

typedef struct XMAP {
    WRT_OBJECT Base;
    XNODE *Element;
} XMAP;

static const WRT_CLASS list_class, map_class;
static const void *const list_vtables[1];
static const void *const map_vtables[1];

static HRESULT new_list(XNODE *owner, XNODE **items, UINT32 count, IXmlNodeList **out)
{
    XLIST *l = CbAlloc(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    memset(l, 0, sizeof(*l));
    WrtInit(&l->Base, &list_class, list_vtables);
    l->Owner = owner;
    if (owner)
        CbXmlAddRef(owner);
    if (count) {
        l->Items = CbAlloc(count * sizeof(XNODE *));
        if (!l->Items) {
            if (owner)
                CbXmlRelease(owner);
            CbFree(l);
            return E_OUTOFMEMORY;
        }
        for (UINT32 i = 0; i < count; i++) {
            l->Items[i] = items[i];
            CbXmlAddRef(items[i]);
        }
        l->Count = count;
    }
    *out = (IXmlNodeList *)&l->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE node_get_children(IXmlNode *This, IXmlNodeList **value)
{
    attribute_text(node_of(This));
    return new_list(node_of(This), nullptr, 0, value);
}

static HRESULT STDMETHODCALLTYPE node_get_first(IXmlNode *This, IXmlNode **value)
{
    attribute_text(node_of(This));
    return out_node(node_of(This)->First, value);
}

static HRESULT STDMETHODCALLTYPE node_get_last(IXmlNode *This, IXmlNode **value)
{
    attribute_text(node_of(This));
    return out_node(node_of(This)->Last, value);
}

static HRESULT STDMETHODCALLTYPE node_get_previous(IXmlNode *This, IXmlNode **value)
{
    XNODE *n = node_of(This);
    return out_node(n->Type == NodeType_AttributeNode ? nullptr : n->Prev, value);
}

static HRESULT STDMETHODCALLTYPE node_get_next(IXmlNode *This, IXmlNode **value)
{
    XNODE *n = node_of(This);
    return out_node(n->Type == NodeType_AttributeNode ? nullptr : n->Next, value);
}

static HRESULT STDMETHODCALLTYPE node_get_attributes(IXmlNode *This, IXmlNamedNodeMap **value)
{
    XNODE *n = node_of(This);
    *value = nullptr;
    if (n->Type != NodeType_ElementNode)
        return S_OK;
    XMAP *m = CbAlloc(sizeof(*m));
    if (!m)
        return E_OUTOFMEMORY;
    memset(m, 0, sizeof(*m));
    WrtInit(&m->Base, &map_class, map_vtables);
    m->Element = n;
    CbXmlAddRef(n);
    *value = (IXmlNamedNodeMap *)&m->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE node_has_children(IXmlNode *This, BOOLEAN *value)
{
    attribute_text(node_of(This));
    *value = node_of(This)->First != nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE node_get_owner(IXmlNode *This, IXmlDocument **value)
{
    XNODE *n = node_of(This);
    if (n->Type == NodeType_DocumentNode) {
        *value = nullptr;
        return S_OK;
    }
    return node_interface(n->Ctx->Document, &IID_IXmlDocument, (void **)value);
}

static HRESULT STDMETHODCALLTYPE node_insert_before(IXmlNode *This, IXmlNode *child, IXmlNode *reference,
                                                    IXmlNode **inserted)
{
    XNODE *parent = node_of(This), *c = CbXmlFromInterface((IUnknown *)child);
    XNODE *r = reference ? CbXmlFromInterface((IUnknown *)reference) : nullptr;
    *inserted = nullptr;
    if (reference && !r)
        return E_INVALIDARG;
    XNODE *done;
    HRESULT hr = insert_child(parent, c, r, &done);
    return SUCCEEDED(hr) ? out_node(done, inserted) : hr;
}

static HRESULT STDMETHODCALLTYPE node_replace_child(IXmlNode *This, IXmlNode *child, IXmlNode *reference,
                                                    IXmlNode **previous)
{
    XNODE *parent = node_of(This), *c = CbXmlFromInterface((IUnknown *)child);
    XNODE *r = CbXmlFromInterface((IUnknown *)reference);
    *previous = nullptr;
    if (!r || r->Parent != parent)
        return E_INVALIDARG;
    XNODE *done;
    HRESULT hr = insert_child(parent, c, r, &done);
    if (FAILED(hr))
        return hr;
    unlink_child(r);
    return out_node(r, previous);
}

static HRESULT STDMETHODCALLTYPE node_remove_child(IXmlNode *This, IXmlNode *child, IXmlNode **removed)
{
    XNODE *parent = node_of(This), *c = CbXmlFromInterface((IUnknown *)child);
    *removed = nullptr;
    if (!c || c->Parent != parent || c->Type == NodeType_AttributeNode)
        return E_INVALIDARG;
    unlink_child(c);
    return out_node(c, removed);
}

static HRESULT STDMETHODCALLTYPE node_append_child(IXmlNode *This, IXmlNode *child, IXmlNode **appended)
{
    XNODE *parent = node_of(This), *c = CbXmlFromInterface((IUnknown *)child);
    *appended = nullptr;
    XNODE *done;
    HRESULT hr = insert_child(parent, c, nullptr, &done);
    return SUCCEEDED(hr) ? out_node(done, appended) : hr;
}

static HRESULT STDMETHODCALLTYPE node_clone(IXmlNode *This, BOOLEAN deep, IXmlNode **clone)
{
    XNODE *n = node_of(This);
    XNODE *copy = clone_into(n->Ctx, n, deep != 0);
    *clone = nullptr;
    return copy ? out_node(copy, clone) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE node_get_null(IXmlNode *This, IInspectable **value)
{
    *value = nullptr;
    return S_OK;
}

/* the part of a qualified name after the colon, or before it. */
static HRESULT name_part(XNODE *n, BOOL prefix, IInspectable **value)
{
    *value = nullptr;
    if (n->Type != NodeType_ElementNode && n->Type != NodeType_AttributeNode)
        return S_OK;
    const WCHAR *colon = wcschr(n->Name, L':');
    if (prefix) {
        if (!colon)
            return S_OK;
        WCHAR *text = dup_n(n->Name, (SIZE_T)(colon - n->Name));
        HRESULT hr = text ? CbBoxString(text, value) : E_OUTOFMEMORY;
        CbFree(text);
        return hr;
    }
    return CbBoxString(colon ? colon + 1 : n->Name, value);
}

static HRESULT STDMETHODCALLTYPE node_get_local(IXmlNode *This, IInspectable **value)
{
    return name_part(node_of(This), FALSE, value);
}

static HRESULT STDMETHODCALLTYPE node_get_prefix(IXmlNode *This, IInspectable **value)
{
    return name_part(node_of(This), TRUE, value);
}

static HRESULT STDMETHODCALLTYPE node_normalize(IXmlNode *This)
{
    XNODE *root = node_of(This);
    /* adjacent texts become one and empty ones go. */
    XNODE *n = root->First;
    while (n) {
        if (n->Type == NodeType_TextNode) {
            XNODE *next = n->Next;
            while (next && next->Type == NodeType_TextNode) {
                SIZE_T a = n->Value ? wcslen(n->Value) : 0, b = next->Value ? wcslen(next->Value) : 0;
                WCHAR *joined = CbAlloc((a + b + 1) * sizeof(WCHAR));
                if (!joined)
                    return E_OUTOFMEMORY;
                if (a)
                    memcpy(joined, n->Value, a * sizeof(WCHAR));
                if (b)
                    memcpy(joined + a, next->Value, b * sizeof(WCHAR));
                joined[a + b] = 0;
                CbFree(n->Value);
                n->Value = joined;
                XNODE *gone = next;
                next = next->Next;
                unlink_child(gone);
            }
            if (!n->Value || !n->Value[0]) {
                XNODE *gone = n;
                n = n->Next;
                unlink_child(gone);
                continue;
            }
        } else if (n->First) {
            node_normalize((IXmlNode *)&n->Base.Ifaces[0]);
        }
        n = n->Next;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE node_put_prefix(IXmlNode *This, IInspectable *value)
{
    XNODE *n = node_of(This);
    if (n->Type != NodeType_ElementNode && n->Type != NodeType_AttributeNode)
        return E_FAIL;
    WCHAR *prefix;
    HRESULT hr = CbUnboxString(value, &prefix);
    if (FAILED(hr))
        return hr;
    const WCHAR *colon = wcschr(n->Name, L':');
    const WCHAR *local = colon ? colon + 1 : n->Name;
    SIZE_T plen = wcslen(prefix);
    WCHAR *name = CbAlloc((plen + 1 + wcslen(local) + 1) * sizeof(WCHAR));
    if (!name) {
        CbFree(prefix);
        return E_OUTOFMEMORY;
    }
    name[0] = 0;
    if (plen) {
        wcscpy(name, prefix);
        wcscat(name, L":");
    }
    wcscat(name, local);
    CbFree(prefix);
    CbFree(n->Name);
    n->Name = name;
    return S_OK;
}

static const IXmlNodeVtbl node_vtbl = {NODE_ENTRIES(IXmlNode), node_get_value, node_put_value, node_get_type,
                                       node_get_name, node_get_parent, node_get_children, node_get_first,
                                       node_get_last, node_get_previous, node_get_next, node_get_attributes,
                                       node_has_children, node_get_owner, node_insert_before, node_replace_child,
                                       node_remove_child, node_append_child, node_clone, node_get_null,
                                       node_get_local, node_get_prefix, node_normalize, node_put_prefix};

static HRESULT STDMETHODCALLTYPE ser_get_xml(IXmlNodeSerializer *This, HSTRING *xml)
{
    WCHAR *text = CbXmlSerialize(node_of(This), nullptr);
    if (!text)
        return E_OUTOFMEMORY;
    HRESULT hr = make_hstring(text, xml);
    CbFree(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ser_get_inner(IXmlNodeSerializer *This, HSTRING *value)
{
    WCHAR *text = CbXmlInnerText(node_of(This));
    if (!text)
        return E_OUTOFMEMORY;
    HRESULT hr = make_hstring(text, value);
    CbFree(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ser_put_inner(IXmlNodeSerializer *This, HSTRING value)
{
    XNODE *n = node_of(This);
    WCHAR *text = dup_hstring(value);
    if (!text)
        return E_OUTOFMEMORY;
    switch (n->Type) {
    case NodeType_AttributeNode:
    case NodeType_TextNode:
    case NodeType_DataSectionNode:
    case NodeType_CommentNode:
    case NodeType_ProcessingInstructionNode:
        CbFree(n->Value);
        n->Value = text;
        return S_OK;
    default:
        break;
    }
    clear_children(n);
    if (text[0]) {
        XNODE *t = node_new(n->Ctx, NodeType_TextNode, nullptr, 0, text, wcslen(text));
        if (!t) {
            CbFree(text);
            return E_OUTOFMEMORY;
        }
        link_child(n, t, nullptr);
    }
    CbFree(text);
    return S_OK;
}

static const IXmlNodeSerializerVtbl ser_vtbl = {NODE_ENTRIES(IXmlNodeSerializer), ser_get_xml, ser_get_inner,
                                                ser_put_inner};

/* ------------------------------------------------------------------ the path language */

/*
 * the part of XPath programs use on a notification: absolute and relative paths of names, *, @name, ., .., text()
 * and node(), the // step, and the predicates [n], [@name], [@name='value'], [name] and [name='value'].
 */
typedef struct NODESET {
    XNODE **Items;
    SIZE_T Count, Capacity;
} NODESET;

static BOOL set_add(NODESET *s, XNODE *n)
{
    for (SIZE_T i = 0; i < s->Count; i++)
        if (s->Items[i] == n)
            return TRUE;
    if (s->Count == s->Capacity) {
        SIZE_T capacity = s->Capacity ? s->Capacity * 2 : 16;
        XNODE **grown = CbAlloc(capacity * sizeof(XNODE *));
        if (!grown)
            return FALSE;
        if (s->Count)
            memcpy(grown, s->Items, s->Count * sizeof(XNODE *));
        CbFree(s->Items);
        s->Items = grown;
        s->Capacity = capacity;
    }
    s->Items[s->Count++] = n;
    return TRUE;
}

static void descend(NODESET *out, XNODE *n)
{
    set_add(out, n);
    for (XNODE *c = n->First; c; c = c->Next)
        descend(out, c);
}

static BOOL step_matches(XNODE *n, const WCHAR *test, SIZE_T length)
{
    if (length == 1 && test[0] == L'*')
        return n->Type == NodeType_ElementNode;
    if (length == 6 && !wcsncmp(test, L"node()", 6))
        return TRUE;
    if (length == 6 && !wcsncmp(test, L"text()", 6))
        return n->Type == NodeType_TextNode || n->Type == NodeType_DataSectionNode;
    return n->Type == NodeType_ElementNode && wcslen(n->Name) == length && !wcsncmp(n->Name, test, length);
}

/* filters the nodes a step found. */
static void apply_predicate(NODESET *set, const WCHAR *pred, SIZE_T length)
{
    WCHAR *text = dup_n(pred, length);
    if (!text)
        return;
    SIZE_T kept = 0;
    BOOL number = text[0] >= L'0' && text[0] <= L'9';
    if (number) {
        SIZE_T index = 0;
        for (const WCHAR *d = text; *d >= L'0' && *d <= L'9'; d++)
            index = index * 10 + (SIZE_T)(*d - L'0');
        if (index >= 1 && index <= set->Count) {
            set->Items[0] = set->Items[index - 1];
            kept = 1;
        }
        set->Count = kept;
        CbFree(text);
        return;
    }
    /* [@name], [@name='v'], [name], [name='v'] */
    BOOL attribute = text[0] == L'@';
    WCHAR *name = text + (attribute ? 1 : 0);
    WCHAR *eq = wcschr(name, L'=');
    WCHAR *value = nullptr;
    if (eq) {
        *eq = 0;
        value = eq + 1;
        SIZE_T vl = wcslen(value);
        if (vl >= 2 && (value[0] == L'\'' || value[0] == L'"') && value[vl - 1] == value[0]) {
            value[vl - 1] = 0;
            value++;
        }
    }
    for (SIZE_T i = 0; i < set->Count; i++) {
        XNODE *n = set->Items[i];
        BOOL ok = FALSE;
        if (attribute) {
            XNODE *a = n->Type == NodeType_ElementNode ? CbXmlAttribute(n, name) : nullptr;
            ok = a && (!value || !wcscmp(a->Value ? a->Value : L"", value));
        } else {
            for (XNODE *c = n->First; c && !ok; c = c->Next) {
                if (c->Type != NodeType_ElementNode || wcscmp(c->Name, name))
                    continue;
                if (!value) {
                    ok = TRUE;
                } else {
                    WCHAR *inner = CbXmlInnerText(c);
                    ok = inner && !wcscmp(inner, value);
                    CbFree(inner);
                }
            }
        }
        if (ok)
            set->Items[kept++] = n;
    }
    set->Count = kept;
    CbFree(text);
}

static HRESULT select_nodes(XNODE *context, const WCHAR *path, NODESET *result)
{
    NODESET current = {};
    const WCHAR *p = path;
    BOOL absolute = *p == L'/';
    XNODE *start = context;
    if (absolute) {
        while (start->Parent)
            start = start->Parent;
    }
    if (!set_add(&current, start))
        return E_OUTOFMEMORY;
    BOOL descendants = FALSE;
    if (absolute) {
        p++;
        if (*p == L'/') {
            p++;
            descendants = TRUE;
        }
    }
    HRESULT hr = S_OK;
    while (*p || descendants) {
        const WCHAR *end = p;
        int brackets = 0;
        while (*end && (*end != L'/' || brackets)) {
            if (*end == L'[')
                brackets++;
            else if (*end == L']')
                brackets--;
            end++;
        }
        const WCHAR *test_end = p;
        while (test_end < end && *test_end != L'[')
            test_end++;
        SIZE_T test_length = (SIZE_T)(test_end - p);
        if (!test_length) {
            hr = E_INVALIDARG;
            break;
        }
        NODESET next = {};
        for (SIZE_T i = 0; i < current.Count && SUCCEEDED(hr); i++) {
            XNODE *from = current.Items[i];
            NODESET found = {};
            if (test_length == 1 && p[0] == L'.') {
                set_add(&found, from);
            } else if (test_length == 2 && p[0] == L'.' && p[1] == L'.') {
                if (from->Parent)
                    set_add(&found, from->Parent);
            } else if (p[0] == L'@') {
                if (from->Type == NodeType_ElementNode)
                    for (XNODE *a = from->Attrs; a; a = a->AttrNext)
                        if ((test_length == 2 && p[1] == L'*') ||
                            (wcslen(a->Name) == test_length - 1 && !wcsncmp(a->Name, p + 1, test_length - 1)))
                            set_add(&found, a);
            } else {
                NODESET pool = {};
                if (descendants) {
                    descend(&pool, from);
                } else {
                    set_add(&pool, from);
                }
                for (SIZE_T k = 0; k < pool.Count; k++)
                    for (XNODE *c = pool.Items[k]->First; c; c = c->Next)
                        if (step_matches(c, p, test_length))
                            set_add(&found, c);
                CbFree(pool.Items);
            }
            /* predicates apply to the nodes found from each node. */
            const WCHAR *q = test_end;
            while (q < end && *q == L'[') {
                const WCHAR *close = q;
                int depth = 0;
                while (close < end) {
                    if (*close == L'[')
                        depth++;
                    else if (*close == L']' && --depth == 0)
                        break;
                    close++;
                }
                if (close >= end) {
                    hr = E_INVALIDARG;
                    break;
                }
                apply_predicate(&found, q + 1, (SIZE_T)(close - q - 1));
                q = close + 1;
            }
            for (SIZE_T k = 0; k < found.Count; k++)
                if (!set_add(&next, found.Items[k]))
                    hr = E_OUTOFMEMORY;
            CbFree(found.Items);
        }
        CbFree(current.Items);
        current = next;
        descendants = FALSE;
        p = end;
        if (*p == L'/') {
            p++;
            if (*p == L'/') {
                p++;
                descendants = TRUE;
            }
        }
        if (FAILED(hr))
            break;
    }
    if (FAILED(hr)) {
        CbFree(current.Items);
        return hr;
    }
    *result = current;
    return S_OK;
}

static HRESULT select_common(XNODE *n, HSTRING xpath, BOOL single, IXmlNode **node, IXmlNodeList **list)
{
    if (node)
        *node = nullptr;
    if (list)
        *list = nullptr;
    WCHAR *path = dup_hstring(xpath);
    if (!path)
        return E_OUTOFMEMORY;
    NODESET set = {};
    HRESULT hr = path[0] ? select_nodes(n, path, &set) : E_INVALIDARG;
    CbFree(path);
    if (FAILED(hr))
        return hr;
    if (single) {
        if (set.Count)
            hr = out_node(set.Items[0], node);
    } else {
        hr = new_list(nullptr, set.Items, (UINT32)set.Count, list);
    }
    CbFree(set.Items);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sel_single(IXmlNodeSelector *This, HSTRING xpath, IXmlNode **node)
{
    return select_common(node_of(This), xpath, TRUE, node, nullptr);
}

static HRESULT STDMETHODCALLTYPE sel_nodes(IXmlNodeSelector *This, HSTRING xpath, IXmlNodeList **list)
{
    return select_common(node_of(This), xpath, FALSE, nullptr, list);
}

static HRESULT STDMETHODCALLTYPE sel_single_ns(IXmlNodeSelector *This, HSTRING xpath, IInspectable *ns, IXmlNode **node)
{
    return select_common(node_of(This), xpath, TRUE, node, nullptr);
}

static HRESULT STDMETHODCALLTYPE sel_nodes_ns(IXmlNodeSelector *This, HSTRING xpath, IInspectable *ns,
                                              IXmlNodeList **list)
{
    return select_common(node_of(This), xpath, FALSE, nullptr, list);
}

static const IXmlNodeSelectorVtbl sel_vtbl = {NODE_ENTRIES(IXmlNodeSelector), sel_single, sel_nodes, sel_single_ns,
                                              sel_nodes_ns};

/* ------------------------------------------------------------------ lists and maps */



/* lists and maps belong to no document: they count their own references and hold the nodes they show. */
#define L_THUNKS(T)                                                                                                 \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        return WrtQuery((WRT_IFACE *)This, riid, object);                                                              \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return WrtAddRef((WRT_IFACE *)This);                                                                           \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return WrtRelease((WRT_IFACE *)This);                                                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetIids(T *This, ULONG *count, IID **iids)                                    \
    {                                                                                                                  \
        return WrtGetIids((WRT_IFACE *)This, count, iids);                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetRuntimeClassName(T *This, HSTRING *name)                                   \
    {                                                                                                                  \
        return WrtGetRuntimeClassName((WRT_IFACE *)This, name);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTrustLevel(T *This, TrustLevel *level)                                     \
    {                                                                                                                  \
        return WrtGetTrustLevel((WRT_IFACE *)This, level);                                                             \
    }



L_THUNKS(IXmlNodeList)
L_THUNKS(IXmlNamedNodeMap)

static XLIST *list_of(IXmlNodeList *This)
{
    return (XLIST *)((WRT_IFACE *)This)->Owner;
}

static UINT32 list_count(XLIST *l)
{
    if (!l->Owner)
        return l->Count;
    UINT32 n = 0;
    for (XNODE *c = l->Owner->First; c; c = c->Next)
        n++;
    return n;
}

static HRESULT STDMETHODCALLTYPE list_get_length(IXmlNodeList *This, UINT32 *value)
{
    *value = list_count(list_of(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE list_item(IXmlNodeList *This, UINT32 index, IXmlNode **node)
{
    XLIST *l = list_of(This);
    *node = nullptr;
    if (l->Owner) {
        XNODE *c = l->Owner->First;
        for (UINT32 i = 0; c && i < index; i++)
            c = c->Next;
        return out_node(c, node);
    }
    return index < l->Count ? out_node(l->Items[index], node) : S_OK;
}

static const IXmlNodeListVtbl list_vtbl = {WRT_INSPECTABLE_ENTRIES(IXmlNodeList), list_get_length, list_item};
static const void *const list_vtables[1] = {&list_vtbl};

static void list_destroy(WRT_OBJECT *object)
{
    XLIST *l = (XLIST *)object;
    if (l->Owner)
        CbXmlRelease(l->Owner);
    for (UINT32 i = 0; i < l->Count; i++)
        CbXmlRelease(l->Items[i]);
    CbFree(l->Items);
    CbFree(l);
}

static const WRT_CLASS list_class = {g_list_class, 1, {&IID_IXmlNodeList}, 0, list_destroy};

static XMAP *map_of(IXmlNamedNodeMap *This)
{
    return (XMAP *)((WRT_IFACE *)This)->Owner;
}

static HRESULT STDMETHODCALLTYPE map_get_length(IXmlNamedNodeMap *This, UINT32 *value)
{
    UINT32 n = 0;
    for (XNODE *a = map_of(This)->Element->Attrs; a; a = a->AttrNext)
        n++;
    *value = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_item(IXmlNamedNodeMap *This, UINT32 index, IXmlNode **node)
{
    XNODE *a = map_of(This)->Element->Attrs;
    for (UINT32 i = 0; a && i < index; i++)
        a = a->AttrNext;
    return out_node(a, node);
}

static HRESULT STDMETHODCALLTYPE map_get_named(IXmlNamedNodeMap *This, HSTRING name, IXmlNode **node)
{
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    XNODE *a = CbXmlAttribute(map_of(This)->Element, key);
    CbFree(key);
    return out_node(a, node);
}

static HRESULT STDMETHODCALLTYPE map_set_named(IXmlNamedNodeMap *This, IXmlNode *node, IXmlNode **previous)
{
    XNODE *element = map_of(This)->Element, *a = CbXmlFromInterface((IUnknown *)node);
    *previous = nullptr;
    if (!a || a->Type != NodeType_AttributeNode)
        return E_INVALIDARG;
    if (a->Ctx != element->Ctx)
        a = clone_into(element->Ctx, a, FALSE);
    if (!a)
        return E_OUTOFMEMORY;
    XNODE *old = CbXmlAttribute(element, a->Name);
    if (old == a)
        return S_OK;
    if (a->Parent)
        unlink_child(a);
    if (old)
        unlink_child(old);
    link_attribute(element, a);
    return out_node(old, previous);
}

static HRESULT STDMETHODCALLTYPE map_remove_named(IXmlNamedNodeMap *This, HSTRING name, IXmlNode **previous)
{
    WCHAR *key = dup_hstring(name);
    *previous = nullptr;
    if (!key)
        return E_OUTOFMEMORY;
    XNODE *old = CbXmlAttribute(map_of(This)->Element, key);
    CbFree(key);
    if (old)
        unlink_child(old);
    return out_node(old, previous);
}

static HRESULT STDMETHODCALLTYPE map_get_ns(IXmlNamedNodeMap *This, IInspectable *ns, HSTRING name, IXmlNode **node)
{
    return map_get_named(This, name, node);
}

static HRESULT STDMETHODCALLTYPE map_remove_ns(IXmlNamedNodeMap *This, IInspectable *ns, HSTRING name,
                                               IXmlNode **previous)
{
    return map_remove_named(This, name, previous);
}

static HRESULT STDMETHODCALLTYPE map_set_ns(IXmlNamedNodeMap *This, IXmlNode *node, IXmlNode **previous)
{
    return map_set_named(This, node, previous);
}

static const IXmlNamedNodeMapVtbl map_vtbl = {WRT_INSPECTABLE_ENTRIES(IXmlNamedNodeMap), map_get_length, map_item,
                                              map_get_named, map_set_named, map_remove_named, map_get_ns,
                                              map_remove_ns, map_set_ns};
static const void *const map_vtables[1] = {&map_vtbl};

static void map_destroy(WRT_OBJECT *object)
{
    XMAP *m = (XMAP *)object;
    CbXmlRelease(m->Element);
    CbFree(m);
}

static const WRT_CLASS map_class = {g_map_class, 1, {&IID_IXmlNamedNodeMap}, 0, map_destroy};

/* ------------------------------------------------------------------ interfaces: elements and attributes */

static HRESULT collect_by_name(XNODE *root, const WCHAR *name, IXmlNodeList **elements)
{
    NODESET set = {};
    BOOL all = !wcscmp(name, L"*");
    for (XNODE *n = root->First; n;) {
        if (n->Type == NodeType_ElementNode && (all || !wcscmp(n->Name, name)))
            set_add(&set, n);
        if (n->First) {
            n = n->First;
            continue;
        }
        while (n && n != root && !n->Next)
            n = n->Parent;
        if (!n || n == root)
            break;
        n = n->Next;
    }
    HRESULT hr = new_list(nullptr, set.Items, (UINT32)set.Count, elements);
    CbFree(set.Items);
    return hr;
}

static HRESULT by_name(XNODE *root, HSTRING name, IXmlNodeList **elements)
{
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    HRESULT hr = collect_by_name(root, key, elements);
    CbFree(key);
    return hr;
}

static HRESULT STDMETHODCALLTYPE elem_get_tag(IXmlElement *This, HSTRING *value)
{
    return out_string(node_of(This)->Name, value);
}

static HRESULT STDMETHODCALLTYPE elem_get_attribute(IXmlElement *This, HSTRING name, HSTRING *value)
{
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    XNODE *a = CbXmlAttribute(node_of(This), key);
    CbFree(key);
    return out_string(a ? a->Value : nullptr, value);
}

static HRESULT STDMETHODCALLTYPE elem_set_attribute(IXmlElement *This, HSTRING name, HSTRING value)
{
    XNODE *e = node_of(This);
    WCHAR *key = dup_hstring(name), *text = dup_hstring(value);
    if (!key || !text) {
        CbFree(key);
        CbFree(text);
        return E_OUTOFMEMORY;
    }
    if (!valid_name(key)) {
        CbFree(key);
        CbFree(text);
        return E_INVALIDARG;
    }
    XNODE *a = CbXmlAttribute(e, key);
    if (a) {
        CbFree(a->Value);
        a->Value = text;
    } else {
        a = node_new(e->Ctx, NodeType_AttributeNode, key, wcslen(key), nullptr, 0);
        if (!a) {
            CbFree(key);
            CbFree(text);
            return E_OUTOFMEMORY;
        }
        a->Value = text;
        link_attribute(e, a);
    }
    CbFree(key);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE elem_remove_attribute(IXmlElement *This, HSTRING name)
{
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    XNODE *a = CbXmlAttribute(node_of(This), key);
    CbFree(key);
    if (a)
        unlink_child(a);
    return S_OK;
}

static HRESULT out_attribute(XNODE *a, IXmlAttribute **out)
{
    return node_interface(a, &IID_IXmlAttribute, (void **)out);
}

static HRESULT STDMETHODCALLTYPE elem_get_attribute_node(IXmlElement *This, HSTRING name, IXmlAttribute **attribute)
{
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    XNODE *a = CbXmlAttribute(node_of(This), key);
    CbFree(key);
    return out_attribute(a, attribute);
}

static HRESULT STDMETHODCALLTYPE elem_set_attribute_node(IXmlElement *This, IXmlAttribute *attribute,
                                                         IXmlAttribute **previous)
{
    XNODE *e = node_of(This), *a = CbXmlFromInterface((IUnknown *)attribute);
    *previous = nullptr;
    if (!a || a->Type != NodeType_AttributeNode)
        return E_INVALIDARG;
    if (a->Ctx != e->Ctx)
        a = clone_into(e->Ctx, a, FALSE);
    if (!a)
        return E_OUTOFMEMORY;
    XNODE *old = CbXmlAttribute(e, a->Name);
    if (old == a)
        return S_OK;
    if (a->Parent)
        unlink_child(a);
    if (old)
        unlink_child(old);
    link_attribute(e, a);
    return out_attribute(old, previous);
}

static HRESULT STDMETHODCALLTYPE elem_remove_attribute_node(IXmlElement *This, IXmlAttribute *attribute,
                                                            IXmlAttribute **removed)
{
    XNODE *e = node_of(This), *a = CbXmlFromInterface((IUnknown *)attribute);
    *removed = nullptr;
    if (!a || a->Parent != e || a->Type != NodeType_AttributeNode)
        return E_INVALIDARG;
    unlink_child(a);
    return out_attribute(a, removed);
}

static HRESULT STDMETHODCALLTYPE elem_get_by_tag(IXmlElement *This, HSTRING name, IXmlNodeList **elements)
{
    return by_name(node_of(This), name, elements);
}

static HRESULT STDMETHODCALLTYPE elem_set_attribute_ns(IXmlElement *This, IInspectable *ns, HSTRING name, HSTRING value)
{
    return elem_set_attribute(This, name, value);
}

static HRESULT STDMETHODCALLTYPE elem_get_attribute_ns(IXmlElement *This, IInspectable *ns, HSTRING name,
                                                       HSTRING *value)
{
    return elem_get_attribute(This, name, value);
}

static HRESULT STDMETHODCALLTYPE elem_remove_attribute_ns(IXmlElement *This, IInspectable *ns, HSTRING name)
{
    return elem_remove_attribute(This, name);
}

static HRESULT STDMETHODCALLTYPE elem_set_attribute_node_ns(IXmlElement *This, IXmlAttribute *attribute,
                                                            IXmlAttribute **previous)
{
    return elem_set_attribute_node(This, attribute, previous);
}

static HRESULT STDMETHODCALLTYPE elem_get_attribute_node_ns(IXmlElement *This, IInspectable *ns, HSTRING name,
                                                            IXmlAttribute **attribute)
{
    return elem_get_attribute_node(This, name, attribute);
}

static const IXmlElementVtbl elem_vtbl = {NODE_ENTRIES(IXmlElement), elem_get_tag, elem_get_attribute,
                                          elem_set_attribute, elem_remove_attribute, elem_get_attribute_node,
                                          elem_set_attribute_node, elem_remove_attribute_node, elem_get_by_tag,
                                          elem_set_attribute_ns, elem_get_attribute_ns, elem_remove_attribute_ns,
                                          elem_set_attribute_node_ns, elem_get_attribute_node_ns};

static HRESULT STDMETHODCALLTYPE attr_get_name(IXmlAttribute *This, HSTRING *value)
{
    return out_string(node_of(This)->Name, value);
}

static HRESULT STDMETHODCALLTYPE attr_get_specified(IXmlAttribute *This, BOOLEAN *value)
{
    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE attr_get_value(IXmlAttribute *This, HSTRING *value)
{
    return out_string(node_of(This)->Value, value);
}

static HRESULT STDMETHODCALLTYPE attr_put_value(IXmlAttribute *This, HSTRING value)
{
    XNODE *a = node_of(This);
    WCHAR *text = dup_hstring(value);
    if (!text)
        return E_OUTOFMEMORY;
    CbFree(a->Value);
    a->Value = text;
    return S_OK;
}

static const IXmlAttributeVtbl attr_vtbl = {NODE_ENTRIES(IXmlAttribute), attr_get_name, attr_get_specified,
                                            attr_get_value, attr_put_value};

/* ------------------------------------------------------------------ interfaces: character data */

static HRESULT STDMETHODCALLTYPE data_get_data(IXmlCharacterData *This, HSTRING *value)
{
    return out_string(node_of(This)->Value, value);
}

static HRESULT STDMETHODCALLTYPE data_put_data(IXmlCharacterData *This, HSTRING value)
{
    XNODE *n = node_of(This);
    WCHAR *text = dup_hstring(value);
    if (!text)
        return E_OUTOFMEMORY;
    CbFree(n->Value);
    n->Value = text;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_get_length(IXmlCharacterData *This, UINT32 *value)
{
    XNODE *n = node_of(This);
    *value = (UINT32)(n->Value ? wcslen(n->Value) : 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_substring(IXmlCharacterData *This, UINT32 offset, UINT32 count, HSTRING *data)
{
    XNODE *n = node_of(This);
    SIZE_T length = n->Value ? wcslen(n->Value) : 0;
    *data = nullptr;
    if (offset > length)
        return E_INVALIDARG;
    if (count > length - offset)
        count = (UINT32)(length - offset);
    return WindowsCreateString(n->Value ? n->Value + offset : L"", count, data);
}

/* replaces count characters at offset with text. */
static HRESULT splice(XNODE *n, UINT32 offset, UINT32 count, const WCHAR *text)
{
    SIZE_T length = n->Value ? wcslen(n->Value) : 0, add = wcslen(text);
    if (offset > length)
        return E_INVALIDARG;
    if (count > length - offset)
        count = (UINT32)(length - offset);
    WCHAR *out = CbAlloc((length - count + add + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    if (offset)
        memcpy(out, n->Value, offset * sizeof(WCHAR));
    memcpy(out + offset, text, add * sizeof(WCHAR));
    memcpy(out + offset + add, n->Value ? n->Value + offset + count : L"", (length - offset - count + 1) * sizeof(WCHAR));
    CbFree(n->Value);
    n->Value = out;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_append(IXmlCharacterData *This, HSTRING data)
{
    XNODE *n = node_of(This);
    return splice(n, n->Value ? (UINT32)wcslen(n->Value) : 0, 0, WindowsGetStringRawBuffer(data, nullptr));
}

static HRESULT STDMETHODCALLTYPE data_insert(IXmlCharacterData *This, UINT32 offset, HSTRING data)
{
    return splice(node_of(This), offset, 0, WindowsGetStringRawBuffer(data, nullptr));
}

static HRESULT STDMETHODCALLTYPE data_delete(IXmlCharacterData *This, UINT32 offset, UINT32 count)
{
    return splice(node_of(This), offset, count, L"");
}

static HRESULT STDMETHODCALLTYPE data_replace(IXmlCharacterData *This, UINT32 offset, UINT32 count, HSTRING data)
{
    return splice(node_of(This), offset, count, WindowsGetStringRawBuffer(data, nullptr));
}

static const IXmlCharacterDataVtbl data_vtbl = {NODE_ENTRIES(IXmlCharacterData), data_get_data, data_put_data,
                                                data_get_length, data_substring, data_append, data_insert,
                                                data_delete, data_replace};

static HRESULT STDMETHODCALLTYPE text_split(IXmlText *This, UINT32 offset, IXmlText **second)
{
    XNODE *n = node_of(This);
    SIZE_T length = n->Value ? wcslen(n->Value) : 0;
    *second = nullptr;
    if (offset > length)
        return E_INVALIDARG;
    XNODE *tail = node_new(n->Ctx, n->Type, nullptr, 0, n->Value ? n->Value + offset : L"", length - offset);
    if (!tail)
        return E_OUTOFMEMORY;
    n->Value[offset] = 0;
    if (n->Parent)
        link_child(n->Parent, tail, n->Next);
    return node_interface(tail, &IID_IXmlText, (void **)second);
}

static const IXmlTextVtbl text_vtbl = {NODE_ENTRIES(IXmlText), text_split};
static const IXmlCommentVtbl comment_vtbl = {NODE_ENTRIES(IXmlComment)};
static const IXmlCDataSectionVtbl cdata_vtbl = {NODE_ENTRIES(IXmlCDataSection)};
static const IXmlDocumentFragmentVtbl fragment_vtbl = {NODE_ENTRIES(IXmlDocumentFragment)};

/* ------------------------------------------------------------------ interfaces: documents */

static HRESULT STDMETHODCALLTYPE doc_get_doctype(IXmlDocument *This, IXmlDocumentType **value)
{
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE doc_get_implementation(IXmlDocument *This, IXmlDomImplementation **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE doc_get_root(IXmlDocument *This, IXmlElement **value)
{
    return node_interface(CbXmlRootElement(node_of(This)), &IID_IXmlElement, (void **)value);
}

static HRESULT new_named(XNODE *doc, NodeType type, HSTRING name, REFIID riid, void **out)
{
    *(void **)out = nullptr;
    WCHAR *key = dup_hstring(name);
    if (!key)
        return E_OUTOFMEMORY;
    if (!valid_name(key)) {
        CbFree(key);
        return E_INVALIDARG;
    }
    XNODE *n = node_new(doc->Ctx, type, key, wcslen(key), type == NodeType_AttributeNode ? L"" : nullptr, 0);
    CbFree(key);
    return n ? node_interface(n, riid, out) : E_OUTOFMEMORY;
}

static HRESULT new_valued(XNODE *doc, NodeType type, HSTRING data, REFIID riid, void **out)
{
    *(void **)out = nullptr;
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(data, &length);
    XNODE *n = node_new(doc->Ctx, type, nullptr, 0, raw, length);
    return n ? node_interface(n, riid, out) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE doc_create_element(IXmlDocument *This, HSTRING name, IXmlElement **element)
{
    return new_named(node_of(This), NodeType_ElementNode, name, &IID_IXmlElement, (void **)element);
}

static HRESULT STDMETHODCALLTYPE doc_create_fragment(IXmlDocument *This, IXmlDocumentFragment **fragment)
{
    XNODE *n = node_new(node_of(This)->Ctx, NodeType_DocumentFragmentNode, nullptr, 0, nullptr, 0);
    *fragment = nullptr;
    return n ? node_interface(n, &IID_IXmlDocumentFragment, (void **)fragment) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE doc_create_text(IXmlDocument *This, HSTRING data, IXmlText **text)
{
    return new_valued(node_of(This), NodeType_TextNode, data, &IID_IXmlText, (void **)text);
}

static HRESULT STDMETHODCALLTYPE doc_create_comment(IXmlDocument *This, HSTRING data, IXmlComment **comment)
{
    return new_valued(node_of(This), NodeType_CommentNode, data, &IID_IXmlComment, (void **)comment);
}

static HRESULT STDMETHODCALLTYPE doc_create_pi(IXmlDocument *This, HSTRING target, HSTRING data,
                                               IXmlProcessingInstruction **pi)
{
    XNODE *doc = node_of(This);
    WCHAR *key = dup_hstring(target);
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(data, &length);
    *pi = nullptr;
    if (!key)
        return E_OUTOFMEMORY;
    if (!valid_name(key)) {
        CbFree(key);
        return E_INVALIDARG;
    }
    XNODE *n = node_new(doc->Ctx, NodeType_ProcessingInstructionNode, key, wcslen(key), raw, length);
    CbFree(key);
    return n ? node_interface(n, &IID_IXmlNode, (void **)pi) : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE doc_create_attribute(IXmlDocument *This, HSTRING name, IXmlAttribute **attribute)
{
    return new_named(node_of(This), NodeType_AttributeNode, name, &IID_IXmlAttribute, (void **)attribute);
}

static HRESULT STDMETHODCALLTYPE doc_create_entity_reference(IXmlDocument *This, HSTRING name,
                                                             IXmlEntityReference **reference)
{
    *reference = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE doc_get_by_tag(IXmlDocument *This, HSTRING name, IXmlNodeList **elements)
{
    return by_name(node_of(This), name, elements);
}

static HRESULT STDMETHODCALLTYPE doc_create_cdata(IXmlDocument *This, HSTRING data, IXmlCDataSection **section)
{
    return new_valued(node_of(This), NodeType_DataSectionNode, data, &IID_IXmlCDataSection, (void **)section);
}

static HRESULT STDMETHODCALLTYPE doc_get_uri(IXmlDocument *This, HSTRING *value)
{
    return out_string(node_of(This)->Uri, value);
}

static HRESULT STDMETHODCALLTYPE doc_create_attribute_ns(IXmlDocument *This, IInspectable *ns, HSTRING name,
                                                         IXmlAttribute **attribute)
{
    return doc_create_attribute(This, name, attribute);
}

static HRESULT STDMETHODCALLTYPE doc_create_element_ns(IXmlDocument *This, IInspectable *ns, HSTRING name,
                                                       IXmlElement **element)
{
    return doc_create_element(This, name, element);
}

static HRESULT STDMETHODCALLTYPE doc_get_by_id(IXmlDocument *This, HSTRING id, IXmlElement **element)
{
    *element = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE doc_import(IXmlDocument *This, IXmlNode *node, BOOLEAN deep, IXmlNode **imported)
{
    XNODE *doc = node_of(This), *src = CbXmlFromInterface((IUnknown *)node);
    *imported = nullptr;
    if (!src || src->Type == NodeType_DocumentNode)
        return E_INVALIDARG;
    XNODE *copy = clone_into(doc->Ctx, src, deep != 0);
    return copy ? out_node(copy, imported) : E_OUTOFMEMORY;
}

static const IXmlDocumentVtbl doc_vtbl = {NODE_ENTRIES(IXmlDocument), doc_get_doctype, doc_get_implementation,
                                          doc_get_root, doc_create_element, doc_create_fragment, doc_create_text,
                                          doc_create_comment, doc_create_pi, doc_create_attribute,
                                          doc_create_entity_reference, doc_get_by_tag, doc_create_cdata, doc_get_uri,
                                          doc_create_attribute_ns, doc_create_element_ns, doc_get_by_id, doc_import};

static HRESULT STDMETHODCALLTYPE io_load_xml(IXmlDocumentIO *This, HSTRING xml)
{
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(xml, &length);
    return CbXmlLoadText(node_of(This), raw, length);
}

static HRESULT STDMETHODCALLTYPE io_load_xml_settings(IXmlDocumentIO *This, HSTRING xml, IInspectable *settings)
{
    return io_load_xml(This, xml);
}

static HRESULT STDMETHODCALLTYPE io_save(IXmlDocumentIO *This, IInspectable *file, IInspectable **operation)
{
    *operation = nullptr;
    return E_NOTIMPL;
}

static const IXmlDocumentIOVtbl io_vtbl = {NODE_ENTRIES(IXmlDocumentIO), io_load_xml, io_load_xml_settings, io_save};

static HRESULT STDMETHODCALLTYPE io2_load(IXmlDocumentIO2 *This, IInspectable *buffer)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE io2_load_settings(IXmlDocumentIO2 *This, IInspectable *buffer, IInspectable *settings)
{
    return E_NOTIMPL;
}

static const IXmlDocumentIO2Vtbl io2_vtbl = {NODE_ENTRIES(IXmlDocumentIO2), io2_load, io2_load_settings};

/* ------------------------------------------------------------------ the classes of the nodes */

static const void *const document_vtables[] = {&node_vtbl, &ser_vtbl, &sel_vtbl, &doc_vtbl, &io_vtbl, &io2_vtbl};
static const void *const element_vtables[] = {&node_vtbl, &ser_vtbl, &sel_vtbl, &elem_vtbl};
static const void *const attribute_vtables[] = {&node_vtbl, &ser_vtbl, &attr_vtbl};
static const void *const text_vtables[] = {&node_vtbl, &ser_vtbl, &text_vtbl, &data_vtbl};
static const void *const cdata_vtables[] = {&node_vtbl, &ser_vtbl, &cdata_vtbl, &text_vtbl, &data_vtbl};
static const void *const comment_vtables[] = {&node_vtbl, &ser_vtbl, &comment_vtbl, &data_vtbl};
static const void *const pi_vtables[] = {&node_vtbl, &ser_vtbl};
static const void *const fragment_vtables[] = {&node_vtbl, &ser_vtbl, &sel_vtbl, &fragment_vtbl};

static void node_destroy(WRT_OBJECT *object)
{
    /* nodes are freed with their context. */
    object->Refs = 0;
}

static const WRT_CLASS document_class = {CbXmlDocumentClass, 6,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlNodeSelector, &IID_IXmlDocument, &IID_IXmlDocumentIO,
     &IID_IXmlDocumentIO2},
    0, node_destroy};
static const WRT_CLASS element_class = {g_element_class, 4,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlNodeSelector, &IID_IXmlElement}, 0, node_destroy};
static const WRT_CLASS attribute_class = {g_attribute_class, 3,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlAttribute}, 0, node_destroy};
static const WRT_CLASS text_class = {g_text_class, 4,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlText, &IID_IXmlCharacterData}, 0, node_destroy};
static const WRT_CLASS cdata_class = {g_cdata_class, 5,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlCDataSection, &IID_IXmlText, &IID_IXmlCharacterData}, 0,
    node_destroy};
static const WRT_CLASS comment_class = {g_comment_class, 4,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlComment, &IID_IXmlCharacterData}, 0, node_destroy};
static const WRT_CLASS pi_class = {g_pi_class, 2, {&IID_IXmlNode, &IID_IXmlNodeSerializer}, 0, node_destroy};
static const WRT_CLASS fragment_class = {g_fragment_class, 4,
    {&IID_IXmlNode, &IID_IXmlNodeSerializer, &IID_IXmlNodeSelector, &IID_IXmlDocumentFragment}, 0, node_destroy};

static const void *const *vtables_of(NodeType type)
{
    switch (type) {
    case NodeType_DocumentNode:
        return document_vtables;
    case NodeType_ElementNode:
        return element_vtables;
    case NodeType_AttributeNode:
        return attribute_vtables;
    case NodeType_TextNode:
        return text_vtables;
    case NodeType_DataSectionNode:
        return cdata_vtables;
    case NodeType_CommentNode:
        return comment_vtables;
    case NodeType_ProcessingInstructionNode:
        return pi_vtables;
    default:
        return fragment_vtables;
    }
}

HRESULT CbXmlNewDocument(IXmlDocument **document)
{
    *document = nullptr;
    XCTX *ctx = CbAlloc(sizeof(*ctx));
    if (!ctx)
        return E_OUTOFMEMORY;
    memset(ctx, 0, sizeof(*ctx));
    /* the reference the program is given keeps the context until it goes. */
    ctx->Refs = 0;
    XNODE *doc = node_new(ctx, NodeType_DocumentNode, nullptr, 0, nullptr, 0);
    if (!doc) {
        CbFree(ctx);
        return E_OUTOFMEMORY;
    }
    ctx->Document = doc;
    return node_interface(doc, &IID_IXmlDocument, (void **)document);
}

/* ------------------------------------------------------------------ the factory of documents */

typedef struct IXmlDocumentFactory IXmlDocumentFactory;
#define IXmlDocumentFactory_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *ActivateInstance)(T * This, IInspectable * *instance);
MIME_COM_INTERFACE(IXmlDocumentFactory)

WRT_INSPECTABLE_THUNKS(IXmlDocumentFactory)

static HRESULT STDMETHODCALLTYPE factory_activate(IXmlDocumentFactory *This, IInspectable **instance)
{
    IXmlDocument *document;
    *instance = nullptr;
    HRESULT hr = CbXmlNewDocument(&document);
    if (FAILED(hr))
        return hr;
    *instance = (IInspectable *)document;
    return S_OK;
}

static const IXmlDocumentFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IXmlDocumentFactory), factory_activate};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS factory_class = {CbXmlDocumentClass, 1, {&IID_IActivationFactory}, 0, factory_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_factory_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factory(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[1] = {&factory_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbXmlDocumentFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factory_once, make_factory, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
