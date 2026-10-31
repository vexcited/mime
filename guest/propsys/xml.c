/* a small xml reader for property descriptions: elements, attributes, text, entities and the usual prolog. */
#include "propsys_private.h"

typedef struct {
    const WCHAR *p, *end;
} CURSOR;

static BOOL is_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static BOOL name_char(WCHAR c)
{
    return c > 0x7f || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == ':';
}

static void skip_space(CURSOR *c)
{
    while (c->p < c->end && is_space(*c->p))
        c->p++;
}

static BOOL starts_with(CURSOR *c, LPCSTR text)
{
    SIZE_T i = 0;
    for (; text[i]; i++)
        if (c->p + i >= c->end || c->p[i] != (WCHAR)text[i])
            return FALSE;
    return TRUE;
}

/* moves past the first occurrence of text, false when it never comes. */
static BOOL skip_past(CURSOR *c, LPCSTR text)
{
    SIZE_T length = 0;
    while (text[length])
        length++;
    while (c->p < c->end) {
        if (starts_with(c, text)) {
            c->p += length;
            return TRUE;
        }
        c->p++;
    }
    return FALSE;
}

typedef struct {
    WCHAR *data;
    SIZE_T length, capacity;
} BUFFER;

static BOOL buffer_add(BUFFER *b, WCHAR c)
{
    if (b->length + 1 >= b->capacity) {
        SIZE_T capacity = b->capacity ? b->capacity * 2 : 64;
        WCHAR *grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR));
        if (!grown)
            return FALSE;
        if (b->data) {
            memcpy(grown, b->data, b->length * sizeof(WCHAR));
            HeapFree(GetProcessHeap(), 0, b->data);
        }
        b->data = grown;
        b->capacity = capacity;
    }
    b->data[b->length++] = c;
    b->data[b->length] = 0;
    return TRUE;
}

/* a character or entity reference at the cursor, appended to the buffer. */
static BOOL read_reference(CURSOR *c, BUFFER *b)
{
    c->p++;
    static const struct {
        LPCSTR name;
        WCHAR value;
    } named[] = {{"lt;", '<'}, {"gt;", '>'}, {"amp;", '&'}, {"apos;", '\''}, {"quot;", '"'}};
    for (UINT i = 0; i < 5; i++)
        if (starts_with(c, named[i].name)) {
            SIZE_T n = 0;
            while (named[i].name[n])
                n++;
            c->p += n;
            return buffer_add(b, named[i].value);
        }
    if (c->p < c->end && *c->p == '#') {
        c->p++;
        UINT base = 10;
        if (c->p < c->end && (*c->p == 'x' || *c->p == 'X')) {
            base = 16;
            c->p++;
        }
        ULONG value = 0;
        BOOL any = FALSE;
        while (c->p < c->end && *c->p != ';') {
            WCHAR d = *c->p;
            UINT digit = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10 :
                         d >= 'A' && d <= 'F' ? d - 'A' + 10 : 99;
            if (digit >= base)
                return FALSE;
            value = value * base + digit;
            any = TRUE;
            c->p++;
        }
        if (!any || c->p >= c->end || value > 0x10FFFF)
            return FALSE;
        c->p++;
        if (value >= 0x10000) {
            value -= 0x10000;
            return buffer_add(b, (WCHAR)(0xD800 + (value >> 10))) && buffer_add(b, (WCHAR)(0xDC00 + (value & 0x3FF)));
        }
        return buffer_add(b, (WCHAR)value);
    }
    return FALSE;
}

static WCHAR *take(BUFFER *b)
{
    if (!b->data)
        buffer_add(b, 0), b->length = 0;
    return b->data;
}

static WCHAR *read_name(CURSOR *c)
{
    const WCHAR *start = c->p;
    while (c->p < c->end && name_char(*c->p))
        c->p++;
    if (c->p == start)
        return nullptr;
    WCHAR *name = HeapAlloc(GetProcessHeap(), 0, (c->p - start + 1) * sizeof(WCHAR));
    if (name) {
        memcpy(name, start, (c->p - start) * sizeof(WCHAR));
        name[c->p - start] = 0;
    }
    return name;
}

static void free_attrs(XAttr *a)
{
    while (a) {
        XAttr *next = a->next;
        HeapFree(GetProcessHeap(), 0, a->name);
        HeapFree(GetProcessHeap(), 0, a->value);
        HeapFree(GetProcessHeap(), 0, a);
        a = next;
    }
}

void XmlFree(XElem *e)
{
    while (e) {
        XElem *next = e->next;
        XmlFree(e->child);
        free_attrs(e->attrs);
        HeapFree(GetProcessHeap(), 0, e->name);
        HeapFree(GetProcessHeap(), 0, e);
        e = next;
    }
}

LPCWSTR XmlAttr(const XElem *e, LPCWSTR name)
{
    for (const XAttr *a = e->attrs; a; a = a->next)
        if (!_wcsicmp(a->name, name))
            return a->value;
    return nullptr;
}

/* the name without a namespace prefix. */
static LPCWSTR local_name(LPCWSTR name)
{
    LPCWSTR colon = wcsrchr(name, ':');
    return colon ? colon + 1 : name;
}

BOOL XmlIs(const XElem *e, LPCWSTR name)
{
    return !_wcsicmp(local_name(e->name), name);
}

static HRESULT read_attributes(CURSOR *c, XElem *e, BOOL *empty)
{
    XAttr **tail = &e->attrs;
    *empty = FALSE;
    for (;;) {
        skip_space(c);
        if (c->p >= c->end)
            return E_FAIL;
        if (*c->p == '>') {
            c->p++;
            return S_OK;
        }
        if (starts_with(c, "/>")) {
            c->p += 2;
            *empty = TRUE;
            return S_OK;
        }
        WCHAR *name = read_name(c);
        if (!name)
            return E_FAIL;
        skip_space(c);
        if (c->p >= c->end || *c->p != '=') {
            HeapFree(GetProcessHeap(), 0, name);
            return E_FAIL;
        }
        c->p++;
        skip_space(c);
        if (c->p >= c->end || (*c->p != '"' && *c->p != '\'')) {
            HeapFree(GetProcessHeap(), 0, name);
            return E_FAIL;
        }
        WCHAR quote = *c->p++;
        BUFFER value = {};
        BOOL ok = TRUE;
        while (ok && c->p < c->end && *c->p != quote) {
            if (*c->p == '<')
                ok = FALSE;
            else if (*c->p == '&')
                ok = read_reference(c, &value);
            else
                ok = buffer_add(&value, is_space(*c->p) ? ' ' : *c->p), c->p++;
        }
        if (!ok || c->p >= c->end) {
            HeapFree(GetProcessHeap(), 0, name);
            HeapFree(GetProcessHeap(), 0, value.data);
            return E_FAIL;
        }
        c->p++;
        for (XAttr *a = e->attrs; a; a = a->next)
            if (!wcscmp(a->name, name)) {
                HeapFree(GetProcessHeap(), 0, name);
                HeapFree(GetProcessHeap(), 0, value.data);
                return E_FAIL;
            }
        XAttr *attr = PsAllocZero(sizeof(*attr));
        if (!attr) {
            HeapFree(GetProcessHeap(), 0, name);
            HeapFree(GetProcessHeap(), 0, value.data);
            return E_OUTOFMEMORY;
        }
        attr->name = name;
        attr->value = take(&value);
        *tail = attr;
        tail = &attr->next;
    }
}

/* the children and text up to the end tag of e. */
static HRESULT read_content(CURSOR *c, XElem *e, UINT depth)
{
    XElem **tail = &e->child;
    if (depth > 64)
        return E_FAIL;
    for (;;) {
        if (c->p >= c->end)
            return E_FAIL;
        if (*c->p != '<') {
            if (*c->p == '&') {
                BUFFER dummy = {};
                BOOL ok = read_reference(c, &dummy);
                HeapFree(GetProcessHeap(), 0, dummy.data);
                if (!ok)
                    return E_FAIL;
            } else
                c->p++;
            continue;
        }
        if (starts_with(c, "<!--")) {
            c->p += 4;
            if (!skip_past(c, "-->"))
                return E_FAIL;
        } else if (starts_with(c, "<![CDATA[")) {
            c->p += 9;
            if (!skip_past(c, "]]>"))
                return E_FAIL;
        } else if (starts_with(c, "<?")) {
            c->p += 2;
            if (!skip_past(c, "?>"))
                return E_FAIL;
        } else if (starts_with(c, "</")) {
            c->p += 2;
            WCHAR *name = read_name(c);
            BOOL match = name && !wcscmp(name, e->name);
            HeapFree(GetProcessHeap(), 0, name);
            skip_space(c);
            if (!match || c->p >= c->end || *c->p != '>')
                return E_FAIL;
            c->p++;
            return S_OK;
        } else {
            c->p++;
            XElem *child = PsAllocZero(sizeof(*child));
            if (!child)
                return E_OUTOFMEMORY;
            *tail = child;
            tail = &child->next;
            child->name = read_name(c);
            BOOL empty;
            if (!child->name)
                return E_FAIL;
            HRESULT hr = read_attributes(c, child, &empty);
            if (SUCCEEDED(hr) && !empty)
                hr = read_content(c, child, depth + 1);
            if (FAILED(hr))
                return hr;
        }
    }
}

static HRESULT decode(const BYTE *data, SIZE_T size, WCHAR **text, SIZE_T *length)
{
    *text = nullptr;
    *length = 0;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        SIZE_T n = (size - 2) / 2;
        WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
        if (!out)
            return E_OUTOFMEMORY;
        memcpy(out, data + 2, n * 2);
        out[n] = 0;
        *text = out;
        *length = n;
        return S_OK;
    }
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        SIZE_T n = (size - 2) / 2;
        WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
        if (!out)
            return E_OUTOFMEMORY;
        for (SIZE_T i = 0; i < n; i++)
            out[i] = (WCHAR)((data[2 + i * 2] << 8) | data[3 + i * 2]);
        out[n] = 0;
        *text = out;
        *length = n;
        return S_OK;
    }
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        size -= 3;
    }
    if (!size)
        return E_FAIL;
    int n = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)data, (int)size, nullptr, 0);
    if (n <= 0)
        return E_FAIL;
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)data, (int)size, out, n);
    out[n] = 0;
    *text = out;
    *length = (SIZE_T)n;
    return S_OK;
}

HRESULT XmlParse(const BYTE *data, SIZE_T size, XElem **root)
{
    *root = nullptr;
    WCHAR *text;
    SIZE_T length;
    HRESULT hr = decode(data, size, &text, &length);
    if (FAILED(hr))
        return hr;
    CURSOR c = {text, text + length};
    XElem holder = {};
    hr = E_FAIL;
    for (;;) {
        skip_space(&c);
        if (c.p >= c.end)
            break;
        if (*c.p != '<')
            goto done;
        if (starts_with(&c, "<?")) {
            c.p += 2;
            if (!skip_past(&c, "?>"))
                goto done;
        } else if (starts_with(&c, "<!--")) {
            c.p += 4;
            if (!skip_past(&c, "-->"))
                goto done;
        } else if (starts_with(&c, "<!")) {
            /* a doctype, which is skipped with its brackets. */
            UINT depth = 0;
            while (c.p < c.end && (*c.p != '>' || depth)) {
                if (*c.p == '[')
                    depth++;
                else if (*c.p == ']' && depth)
                    depth--;
                c.p++;
            }
            if (c.p >= c.end)
                goto done;
            c.p++;
        } else {
            if (holder.child)
                goto done;
            c.p++;
            XElem *e = PsAllocZero(sizeof(*e));
            if (!e) {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            holder.child = e;
            e->name = read_name(&c);
            BOOL empty;
            if (!e->name)
                goto done;
            hr = read_attributes(&c, e, &empty);
            if (SUCCEEDED(hr) && !empty)
                hr = read_content(&c, e, 0);
            if (FAILED(hr))
                goto done;
            hr = E_FAIL;
        }
    }
    if (holder.child) {
        *root = holder.child;
        holder.child = nullptr;
        hr = S_OK;
    }
done:
    XmlFree(holder.child);
    HeapFree(GetProcessHeap(), 0, text);
    return hr;
}
