/*
 * the metadata query language: paths of blocks and items such as /app1/ifd/exif/{ushort=36867}, over the blocks of a
 * container or the items of one handler, with readers that follow them and writers that make what is missing.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"

typedef enum SegKind { SEG_NAME, SEG_ITEM } SegKind;

typedef struct Segment {
    SegKind kind;
    int index;           /* [n] before a name, -1 for none. */
    const WCHAR *name;   /* a name, not terminated. */
    SIZE_T name_length;
    const WCHAR *prefix; /* of a name with a namespace prefix: prefix:name. */
    SIZE_T prefix_length;
    PROPVARIANT id;      /* the id of a braced item. */
} Segment;

typedef struct WicQuery {
    WicBase base;
    WicSlot reader;
    WicSlot writer;
    CRITICAL_SECTION lock;
    GUID container;
    IWICMetadataBlockReader *blocks;
    IWICMetadataBlockWriter *bwriter;
    IWICMetadataReader *handler;
    WCHAR *location;
    BOOL writable;
} WicQuery;

static WicQuery *query_of(void *This) { return (WicQuery *)WicSlotBase(This); }

static void query_destroy(WicBase *base)
{
    WicQuery *q = (WicQuery *)base;
    if (q->blocks)
        q->blocks->lpVtbl->Release(q->blocks);
    if (q->bwriter)
        q->bwriter->lpVtbl->Release(q->bwriter);
    if (q->handler)
        q->handler->lpVtbl->Release(q->handler);
    WicFree(q->location);
    DeleteCriticalSection(&q->lock);
}

/* ------------------------------------------------------------------ parsing */

static BOOL ieq(const WCHAR *a, SIZE_T n, const WCHAR *b)
{
    if (wcslen(b) != n)
        return FALSE;
    for (SIZE_T i = 0; i < n; i++) {
        WCHAR x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return FALSE;
    }
    return TRUE;
}

static BOOL parse_number(const WCHAR *s, SIZE_T n, BOOL is_signed, unsigned long long *out, BOOL *negative)
{
    unsigned long long v = 0;
    SIZE_T i = 0;
    BOOL neg = FALSE;
    if (i < n && (s[i] == '-' || s[i] == '+')) {
        if (!is_signed && s[i] == '-')
            return FALSE;
        neg = s[i] == '-';
        i++;
    }
    unsigned base = 10;
    if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    if (i >= n)
        return FALSE;
    for (; i < n; i++) {
        unsigned digit;
        WCHAR c = s[i];
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return FALSE;
        if (v > (~0ull - digit) / base)
            return FALSE;
        v = v * base + digit;
    }
    *out = v;
    *negative = neg;
    return TRUE;
}

static BOOL parse_guid(const WCHAR *s, SIZE_T n, GUID *guid)
{
    WCHAR text[48];
    if (n >= 40)
        return FALSE;
    /* the braces are optional. */
    SIZE_T k = 0;
    if (n == 36)
        text[k++] = '{';
    memcpy(text + k, s, n * sizeof(WCHAR));
    k += n;
    if (n == 36)
        text[k++] = '}';
    text[k] = 0;
    return CLSIDFromString(text, guid) == S_OK;
}

/* the value in the braces: unescaped text copied into a new buffer. */
static WCHAR *unescape(const WCHAR *s, SIZE_T n, SIZE_T *length)
{
    WCHAR *out = CoTaskMemAlloc((n + 1) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n)
            i++;
        out[k++] = s[i];
    }
    out[k] = 0;
    *length = k;
    return out;
}

static HRESULT parse_item(const WCHAR *s, SIZE_T n, PROPVARIANT *id)
{
    /* type=value */
    SIZE_T eq = 0;
    while (eq < n && s[eq] != '=')
        eq++;
    if (eq == n)
        return WINCODEC_ERR_INVALIDQUERYREQUEST;
    const WCHAR *type = s;
    SIZE_T type_length = eq;
    const WCHAR *value = s + eq + 1;
    SIZE_T value_length = n - eq - 1;
    unsigned long long u;
    BOOL negative;
    WicPvInit(id);
    static const struct { const WCHAR *name; VARTYPE vt; BOOL is_signed; } numbers[] = {
        {L"uchar", VT_UI1, FALSE}, {L"char", VT_I1, TRUE},   {L"ushort", VT_UI2, FALSE}, {L"short", VT_I2, TRUE},
        {L"uint", VT_UI4, FALSE},  {L"int", VT_I4, TRUE},    {L"ulong", VT_UI8, FALSE},  {L"long", VT_I8, TRUE},
        {L"ulonglong", VT_UI8, FALSE}, {L"longlong", VT_I8, TRUE},
    };
    for (UINT i = 0; i < WIC_ARRAY_SIZE(numbers); i++) {
        if (!ieq(type, type_length, numbers[i].name))
            continue;
        if (!parse_number(value, value_length, numbers[i].is_signed, &u, &negative))
            return WINCODEC_ERR_INVALIDQUERYREQUEST;
        id->vt = numbers[i].vt;
        long long v = negative ? -(long long)u : (long long)u;
        switch (numbers[i].vt) {
        case VT_UI1:
        case VT_I1:
            id->bVal = (BYTE)v;
            break;
        case VT_UI2:
        case VT_I2:
            id->uiVal = (USHORT)v;
            break;
        case VT_UI4:
        case VT_I4:
            id->ulVal = (ULONG)v;
            break;
        default:
            id->uhVal.QuadPart = (ULONGLONG)v;
        }
        return S_OK;
    }
    if (ieq(type, type_length, L"str") || ieq(type, type_length, L"wstr")) {
        SIZE_T length;
        WCHAR *text = unescape(value, value_length, &length);
        if (!text)
            return E_OUTOFMEMORY;
        if (ieq(type, type_length, L"wstr")) {
            id->vt = VT_LPWSTR;
            id->pwszVal = text;
        } else {
            id->vt = VT_LPSTR;
            id->pszVal = CoTaskMemAlloc(length + 1);
            if (!id->pszVal) {
                CoTaskMemFree(text);
                WicPvInit(id);
                return E_OUTOFMEMORY;
            }
            for (SIZE_T i = 0; i <= length; i++)
                id->pszVal[i] = (char)text[i];
            CoTaskMemFree(text);
        }
        return S_OK;
    }
    if (ieq(type, type_length, L"guid")) {
        GUID guid;
        if (!parse_guid(value, value_length, &guid))
            return WINCODEC_ERR_INVALIDQUERYREQUEST;
        id->vt = VT_CLSID;
        id->puuid = CoTaskMemAlloc(sizeof(GUID));
        if (!id->puuid) {
            WicPvInit(id);
            return E_OUTOFMEMORY;
        }
        *id->puuid = guid;
        return S_OK;
    }
    if (ieq(type, type_length, L"bool")) {
        if (!parse_number(value, value_length, FALSE, &u, &negative)) {
            if (ieq(value, value_length, L"true"))
                u = 1;
            else if (ieq(value, value_length, L"false"))
                u = 0;
            else
                return WINCODEC_ERR_INVALIDQUERYREQUEST;
        }
        id->vt = VT_BOOL;
        id->boolVal = u ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    return WINCODEC_ERR_INVALIDQUERYREQUEST;
}

/* splits a path into segments. the path must start with a slash for an absolute one. */
typedef struct Path {
    Segment seg[24];
    UINT count;
    BOOL absolute;
} Path;

static void path_free(Path *p)
{
    for (UINT i = 0; i < p->count; i++)
        PropVariantClear(&p->seg[i].id);
    p->count = 0;
}

static HRESULT path_parse(const WCHAR *text, Path *p)
{
    memset(p, 0, sizeof(*p));
    const WCHAR *s = text;
    if (!*s)
        return WINCODEC_ERR_INVALIDQUERYREQUEST;
    p->absolute = *s == '/';
    if (p->absolute)
        s++;
    while (*s) {
        if (p->count == WIC_ARRAY_SIZE(p->seg))
            return WINCODEC_ERR_INVALIDQUERYREQUEST;
        Segment *seg = &p->seg[p->count];
        seg->index = -1;
        WicPvInit(&seg->id);
        if (*s == '{') {
            const WCHAR *start = ++s;
            while (*s && *s != '}') {
                if (*s == '\\' && s[1])
                    s++;
                s++;
            }
            if (*s != '}')
                return WINCODEC_ERR_INVALIDQUERYREQUEST;
            seg->kind = SEG_ITEM;
            HRESULT hr = parse_item(start, (SIZE_T)(s - start), &seg->id);
            if (FAILED(hr))
                return hr;
            s++;
            p->count++;
        } else {
            seg->kind = SEG_NAME;
            if (*s == '[') {
                const WCHAR *start = ++s;
                while (*s && *s != ']')
                    s++;
                if (*s != ']' || s == start)
                    return WINCODEC_ERR_INVALIDQUERYREQUEST;
                unsigned long long v;
                BOOL neg;
                if (!parse_number(start, (SIZE_T)(s - start), FALSE, &v, &neg) || v > 0x7fff)
                    return WINCODEC_ERR_INVALIDQUERYREQUEST;
                seg->index = (int)v;
                s++;
            }
            const WCHAR *start = s;
            while (*s && *s != '/') {
                if (*s == '{' || *s == '}' || *s == '[' || *s == ']')
                    return WINCODEC_ERR_INVALIDQUERYCHARACTER;
                if (*s == ':' && !seg->prefix) {
                    seg->prefix = start;
                    seg->prefix_length = (SIZE_T)(s - start);
                    start = s + 1;
                }
                s++;
            }
            seg->name = start;
            seg->name_length = (SIZE_T)(s - start);
            if (!seg->name_length)
                return WINCODEC_ERR_INVALIDQUERYREQUEST;
            p->count++;
        }
        if (*s == '/')
            s++;
        else if (*s)
            return WINCODEC_ERR_INVALIDQUERYREQUEST;
    }
    return p->count ? S_OK : WINCODEC_ERR_INVALIDQUERYREQUEST;
}

/* ------------------------------------------------------------------ walking */

/* the position of a walk: the block list or one handler. */
typedef struct Cursor {
    IWICMetadataBlockReader *blocks; /* when at the root. */
    IWICMetadataBlockWriter *bwriter;
    IWICMetadataReader *handler;     /* otherwise, referenced. */
} Cursor;

static const GUID *segment_format(const Segment *seg)
{
    WCHAR name[40];
    if (seg->name_length >= WIC_ARRAY_SIZE(name))
        return nullptr;
    memcpy(name, seg->name, seg->name_length * sizeof(WCHAR));
    name[seg->name_length] = 0;
    return WicFormatFromShortName(name);
}

/* the sub handler of an item. */
static IWICMetadataReader *item_block(const WicMetaItem *item)
{
    if (item->value.vt != VT_UNKNOWN || !item->value.punkVal)
        return nullptr;
    IWICMetadataReader *r = nullptr;
    if (FAILED(item->value.punkVal->lpVtbl->QueryInterface(item->value.punkVal, &IID_IWICMetadataReader, (void **)&r)))
        return nullptr;
    return r;
}

static BOOL item_matches(const WicMetaItem *item, const Segment *seg, const GUID *format)
{
    if (seg->kind == SEG_ITEM)
        return WicPvSame(&item->id, &seg->id);
    if (seg->prefix) {
        const WCHAR *uri = WicSchemaUri(format, seg->prefix, seg->prefix_length);
        if (!uri || item->schema.vt != VT_LPWSTR || wcscmp(item->schema.pwszVal, uri))
            return FALSE;
    }
    return item->id.vt == VT_LPWSTR && wcslen(item->id.pwszVal) == seg->name_length &&
           !wcsncmp(item->id.pwszVal, seg->name, seg->name_length);
}

typedef struct Found {
    IWICMetadataReader *block;  /* the sub block, referenced, or NULL. */
    UINT item;                  /* the item of the handler. */
    BOOL is_item;               /* an item that is not a block. */
    BOOL at_root;               /* found among the blocks of the container. */
    UINT block_index;
} Found;

/* looks for a segment under the cursor. */
static BOOL look(const Cursor *cur, const Segment *seg, Found *found)
{
    memset(found, 0, sizeof(*found));
    if (cur->blocks) {
        if (seg->kind != SEG_NAME)
            return FALSE;
        const GUID *format = segment_format(seg);
        if (!format)
            return FALSE;
        UINT count = 0;
        if (FAILED(cur->blocks->lpVtbl->GetCount(cur->blocks, &count)))
            return FALSE;
        int seen = 0;
        for (UINT i = 0; i < count; i++) {
            IWICMetadataReader *r = nullptr;
            GUID f;
            if (FAILED(cur->blocks->lpVtbl->GetReaderByIndex(cur->blocks, i, &r)) || !r)
                continue;
            if (SUCCEEDED(r->lpVtbl->GetMetadataFormat(r, &f)) && IsEqualGUID(&f, format)) {
                if (seg->index < 0 || seen == seg->index) {
                    found->block = r;
                    found->at_root = TRUE;
                    found->block_index = i;
                    return TRUE;
                }
                seen++;
            }
            r->lpVtbl->Release(r);
        }
        return FALSE;
    }
    IWICMetadataReader *h = cur->handler;
    GUID hformat;
    h->lpVtbl->GetMetadataFormat(h, &hformat);
    UINT count = WicMetaCount(h);
    const GUID *format = seg->kind == SEG_NAME && !seg->prefix ? segment_format(seg) : nullptr;
    int seen = 0;
    if (format) {
        for (UINT i = 0; i < count; i++) {
            const WicMetaItem *item = WicMetaItemAt(h, i);
            IWICMetadataReader *sub = item_block(item);
            if (!sub)
                continue;
            GUID f;
            if (SUCCEEDED(sub->lpVtbl->GetMetadataFormat(sub, &f)) && IsEqualGUID(&f, format)) {
                if (seg->index < 0 || seen == seg->index) {
                    found->block = sub;
                    found->item = i;
                    return TRUE;
                }
                seen++;
            }
            sub->lpVtbl->Release(sub);
        }
    }
    for (UINT i = 0; i < count; i++) {
        const WicMetaItem *item = WicMetaItemAt(h, i);
        if (item_matches(item, seg, &hformat)) {
            found->item = i;
            found->is_item = TRUE;
            IWICMetadataReader *sub = item_block(item);
            if (sub) {
                found->block = sub;
                found->is_item = FALSE;
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ query objects */

static HRESULT query_create(IWICMetadataBlockReader *blocks, IWICMetadataBlockWriter *bwriter,
                            IWICMetadataReader *handler, const WCHAR *location, const GUID *container, BOOL writable,
                            void **out);

static WCHAR *join_location(const WCHAR *parent, const Segment *seg)
{
    SIZE_T pl = wcslen(parent);
    SIZE_T need = pl + seg->name_length + 40;
    WCHAR *out = WicAlloc(need * sizeof(WCHAR));
    if (!out)
        return nullptr;
    wcscpy(out, parent);
    SIZE_T k = pl;
    if (k && out[k - 1] == '/')
        k--;
    out[k++] = '/';
    if (seg->index >= 0) {
        out[k++] = '[';
        WCHAR digits[12];
        int n = 0, v = seg->index;
        do {
            digits[n++] = (WCHAR)('0' + v % 10);
            v /= 10;
        } while (v);
        while (n)
            out[k++] = digits[--n];
        out[k++] = ']';
    }
    if (seg->kind == SEG_NAME) {
        if (seg->prefix) {
            memcpy(out + k, seg->prefix, seg->prefix_length * sizeof(WCHAR));
            k += seg->prefix_length;
            out[k++] = ':';
        }
        memcpy(out + k, seg->name, seg->name_length * sizeof(WCHAR));
        k += seg->name_length;
    }
    out[k] = 0;
    return out;
}

/* the value the query returns for what it found. */
static HRESULT found_value(WicQuery *q, const Cursor *cur, const Found *f, const Segment *seg, BOOL last,
                           PROPVARIANT *value)
{
    if (f->block) {
        WCHAR *location = join_location(q->location, seg);
        if (!location)
            return E_OUTOFMEMORY;
        void *object;
        IWICMetadataWriter *as_writer = nullptr;
        BOOL writable = q->writable && SUCCEEDED(f->block->lpVtbl->QueryInterface(f->block, &IID_IWICMetadataWriter,
                                                                                  (void **)&as_writer));
        if (as_writer)
            as_writer->lpVtbl->Release(as_writer);
        HRESULT hr = query_create(nullptr, nullptr, f->block, location, &q->container, writable, &object);
        WicFree(location);
        if (FAILED(hr))
            return hr;
        WicPvInit(value);
        value->vt = VT_UNKNOWN;
        value->punkVal = object;
        return S_OK;
    }
    (void)last;
    const WicMetaItem *item = WicMetaItemAt(cur->handler, f->item);
    return PropVariantCopy(value, &item->value);
}

static void cursor_set_handler(Cursor *cur, IWICMetadataReader *h)
{
    if (cur->handler)
        cur->handler->lpVtbl->Release(cur->handler);
    cur->handler = h;
    cur->blocks = nullptr;
    cur->bwriter = nullptr;
}

static HRESULT STDMETHODCALLTYPE q_get_container(void *This, GUID *container)
{
    if (!container)
        return E_INVALIDARG;
    *container = query_of(This)->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE q_get_location(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    WicQuery *q = query_of(This);
    UINT length = (UINT)wcslen(q->location) + 1;
    if (!actual)
        return E_INVALIDARG;
    *actual = length;
    if (!buffer)
        return size == 0 ? S_OK : E_INVALIDARG;
    if (size < length)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    memcpy(buffer, q->location, length * sizeof(WCHAR));
    return S_OK;
}

static void cursor_start(WicQuery *q, BOOL absolute, Cursor *cur)
{
    memset(cur, 0, sizeof(*cur));
    (void)absolute;
    if (q->handler) {
        cur->handler = q->handler;
        cur->handler->lpVtbl->AddRef(cur->handler);
    } else {
        cur->blocks = q->blocks;
        cur->bwriter = q->bwriter;
    }
}

static void cursor_done(Cursor *cur)
{
    if (cur->handler)
        cur->handler->lpVtbl->Release(cur->handler);
}

static HRESULT STDMETHODCALLTYPE q_get_by_name(void *This, LPCWSTR name, PROPVARIANT *value)
{
    WicQuery *q = query_of(This);
    if (!name || !value)
        return E_INVALIDARG;
    WicPvInit(value);
    Path path;
    HRESULT hr = path_parse(name, &path);
    if (FAILED(hr)) {
        path_free(&path);
        return hr;
    }
    Cursor cur;
    cursor_start(q, path.absolute, &cur);
    hr = S_OK;
    for (UINT i = 0; i < path.count && SUCCEEDED(hr); i++) {
        Found f;
        BOOL last = i + 1 == path.count;
        if (!look(&cur, &path.seg[i], &f)) {
            hr = WINCODEC_ERR_PROPERTYNOTFOUND;
            break;
        }
        if (last) {
            hr = found_value(q, &cur, &f, &path.seg[i], last, value);
            if (f.block)
                f.block->lpVtbl->Release(f.block);
            break;
        }
        if (!f.block) {
            hr = WINCODEC_ERR_PROPERTYNOTFOUND;
            break;
        }
        cursor_set_handler(&cur, f.block);
    }
    cursor_done(&cur);
    path_free(&path);
    return hr;
}

/* the names of what is under the cursor. */
/* appends at most n characters of text. */
static void append_n(WCHAR *dest, const WCHAR *text, SIZE_T n)
{
    SIZE_T k = wcslen(dest);
    for (SIZE_T i = 0; i < n && text[i]; i++)
        dest[k++] = text[i];
    dest[k] = 0;
}

/* "/{type=value}" with the value in decimal. */
static void format_id(WCHAR *dest, const WCHAR *type, unsigned long long v)
{
    WCHAR digits[24];
    int n = 0;
    do {
        digits[n++] = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    wcscpy(dest, L"/{");
    wcscat(dest, type);
    wcscat(dest, L"=");
    SIZE_T k = wcslen(dest);
    while (n)
        dest[k++] = digits[--n];
    dest[k++] = '}';
    dest[k] = 0;
}

static HRESULT STDMETHODCALLTYPE q_get_enumerator(void *This, IEnumString **out)
{
    WicQuery *q = query_of(This);
    if (!out)
        return E_INVALIDARG;
    UINT capacity = 64, count = 0;
    WCHAR **names = WicAllocZero((SIZE_T)capacity * sizeof(WCHAR *));
    if (!names)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (q->handler) {
        GUID hformat;
        q->handler->lpVtbl->GetMetadataFormat(q->handler, &hformat);
        UINT n = WicMetaCount(q->handler);
        for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
            const WicMetaItem *item = WicMetaItemAt(q->handler, i);
            WCHAR text[160];
            text[0] = 0;
            IWICMetadataReader *sub = item_block(item);
            if (sub) {
                GUID f;
                const WCHAR *short_name = nullptr;
                if (SUCCEEDED(sub->lpVtbl->GetMetadataFormat(sub, &f)))
                    short_name = WicFormatShortName(&f);
                sub->lpVtbl->Release(sub);
                if (!short_name)
                    continue;
                text[0] = '/';
                wcscpy(text + 1, short_name);
            } else if (item->id.vt == VT_LPWSTR && item->id.pwszVal) {
                if (item->schema.vt == VT_LPWSTR && item->schema.pwszVal) {
                    const WCHAR *prefix = WicSchemaPrefix(&hformat, item->schema.pwszVal);
                    text[0] = '/';
                    if (prefix) {
                        wcscpy(text + 1, prefix);
                        wcscat(text, L":");
                    }
                    append_n(text, item->id.pwszVal, 100);
                } else {
                    text[0] = '/';
                    text[1] = 0;
                    append_n(text, item->id.pwszVal, 140);
                }
            } else if (item->id.vt == VT_UI2) {
                format_id(text, L"ushort", item->id.uiVal);
            } else if (item->id.vt == VT_UI4) {
                format_id(text, L"uint", item->id.ulVal);
            } else if (item->id.vt == VT_UI1) {
                format_id(text, L"uchar", item->id.bVal);
            } else if (item->id.vt == VT_LPSTR && item->id.pszVal) {
                wcscpy(text, L"/{str=");
                SIZE_T k = wcslen(text);
                for (SIZE_T c = 0; item->id.pszVal[c] && k < 150; c++)
                    text[k++] = (BYTE)item->id.pszVal[c];
                text[k++] = '}';
                text[k] = 0;
            } else {
                continue;
            }
            if (count == capacity) {
                capacity *= 2;
                WCHAR **bigger = WicRealloc(names, (SIZE_T)capacity * sizeof(WCHAR *));
                if (!bigger) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                names = bigger;
            }
            names[count] = WicStrDup(text);
            if (!names[count])
                hr = E_OUTOFMEMORY;
            else
                count++;
        }
    } else if (q->blocks) {
        UINT n = 0;
        q->blocks->lpVtbl->GetCount(q->blocks, &n);
        for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
            IWICMetadataReader *r = nullptr;
            GUID f;
            if (FAILED(q->blocks->lpVtbl->GetReaderByIndex(q->blocks, i, &r)) || !r)
                continue;
            const WCHAR *short_name = SUCCEEDED(r->lpVtbl->GetMetadataFormat(r, &f)) ? WicFormatShortName(&f) : nullptr;
            r->lpVtbl->Release(r);
            if (!short_name)
                continue;
            if (count == capacity) {
                capacity *= 2;
                WCHAR **bigger = WicRealloc(names, (SIZE_T)capacity * sizeof(WCHAR *));
                if (!bigger) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                names = bigger;
            }
            WCHAR text[64];
            text[0] = '/';
            wcscpy(text + 1, short_name);
            names[count] = WicStrDup(text);
            if (!names[count])
                hr = E_OUTOFMEMORY;
            else
                count++;
        }
    }
    if (SUCCEEDED(hr))
        hr = WicEnumStringCreate(names, count, out);
    for (UINT i = 0; i < count; i++)
        WicFree(names[i]);
    WicFree(names);
    return hr;
}

/* ------------------------------------------------------------------ writing */

/* the tag of an ifd that points at a sub block of the format, or zero. */
static UINT16 ifd_pointer_tag(const GUID *parent, const GUID *child)
{
    BOOL ifd_parent = IsEqualGUID(parent, &GUID_MetadataFormatIfd) || IsEqualGUID(parent, &GUID_MetadataFormatSubIfd) ||
                      IsEqualGUID(parent, &GUID_MetadataFormatExif);
    if (!ifd_parent)
        return 0;
    if (IsEqualGUID(child, &GUID_MetadataFormatExif))
        return 34665;
    if (IsEqualGUID(child, &GUID_MetadataFormatGps))
        return 34853;
    if (IsEqualGUID(child, &GUID_MetadataFormatInterop))
        return 40965;
    if (IsEqualGUID(child, &GUID_MetadataFormatSubIfd))
        return 330;
    return 0;
}

/* makes the block a segment names, under the cursor. */
static HRESULT make_block(const Cursor *cur, const Segment *seg, IWICMetadataReader **out)
{
    const GUID *format = seg->kind == SEG_NAME && !seg->prefix ? segment_format(seg) : nullptr;
    if (!format)
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    IWICMetadataWriter *w;
    HRESULT hr = WicMetaHandlerCreate(format, &w);
    if (FAILED(hr))
        return hr;
    if (cur->blocks) {
        if (!cur->bwriter) {
            w->lpVtbl->Release(w);
            return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        }
        hr = cur->bwriter->lpVtbl->AddWriter(cur->bwriter, w);
    } else {
        IWICMetadataWriter *parent = nullptr;
        hr = cur->handler->lpVtbl->QueryInterface(cur->handler, &IID_IWICMetadataWriter, (void **)&parent);
        if (SUCCEEDED(hr)) {
            GUID pf;
            cur->handler->lpVtbl->GetMetadataFormat(cur->handler, &pf);
            PROPVARIANT id, value;
            WicPvInit(&id);
            WicPvInit(&value);
            UINT16 tag = ifd_pointer_tag(&pf, format);
            if (tag) {
                id.vt = VT_UI2;
                id.uiVal = tag;
            }
            value.vt = VT_UNKNOWN;
            value.punkVal = (IUnknown *)w;
            /* an item with no id cannot be found by one, so it is added next to any with the same value. */
            hr = WicMetaAdd((IWICMetadataReader *)parent, nullptr, &id, &value);
            parent->lpVtbl->Release(parent);
        }
    }
    if (FAILED(hr)) {
        w->lpVtbl->Release(w);
        return hr;
    }
    *out = (IWICMetadataReader *)w;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE q_set_by_name(void *This, LPCWSTR name, const PROPVARIANT *value)
{
    WicQuery *q = query_of(This);
    if (!name || !value)
        return E_INVALIDARG;
    if (!q->writable)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    Path path;
    HRESULT hr = path_parse(name, &path);
    if (FAILED(hr)) {
        path_free(&path);
        return hr;
    }
    Cursor cur;
    cursor_start(q, path.absolute, &cur);
    for (UINT i = 0; i + 1 < path.count && SUCCEEDED(hr); i++) {
        Found f;
        IWICMetadataReader *next = nullptr;
        if (look(&cur, &path.seg[i], &f) && f.block) {
            next = f.block;
        } else {
            hr = make_block(&cur, &path.seg[i], &next);
        }
        if (SUCCEEDED(hr))
            cursor_set_handler(&cur, next);
    }
    if (SUCCEEDED(hr)) {
        if (!cur.handler) {
            hr = WINCODEC_ERR_INVALIDQUERYREQUEST;
        } else {
            IWICMetadataWriter *w = nullptr;
            hr = cur.handler->lpVtbl->QueryInterface(cur.handler, &IID_IWICMetadataWriter, (void **)&w);
            if (SUCCEEDED(hr)) {
                const Segment *last = &path.seg[path.count - 1];
                PROPVARIANT id, schema;
                WicPvInit(&id);
                WicPvInit(&schema);
                if (last->kind == SEG_ITEM) {
                    hr = PropVariantCopy(&id, &last->id);
                } else {
                    const GUID *format = segment_format(last);
                    GUID hf;
                    cur.handler->lpVtbl->GetMetadataFormat(cur.handler, &hf);
                    if (format && value->vt == VT_UNKNOWN) {
                        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
                    } else {
                        SIZE_T n = last->name_length;
                        id.vt = VT_LPWSTR;
                        id.pwszVal = CoTaskMemAlloc((n + 1) * sizeof(WCHAR));
                        if (!id.pwszVal) {
                            hr = E_OUTOFMEMORY;
                        } else {
                            memcpy(id.pwszVal, last->name, n * sizeof(WCHAR));
                            id.pwszVal[n] = 0;
                        }
                        if (SUCCEEDED(hr) && last->prefix) {
                            const WCHAR *uri = WicSchemaUri(&hf, last->prefix, last->prefix_length);
                            if (!uri) {
                                hr = WINCODEC_ERR_PROPERTYNOTFOUND;
                            } else {
                                schema.vt = VT_LPWSTR;
                                schema.pwszVal = WicStrDup(uri);
                                if (!schema.pwszVal)
                                    hr = E_OUTOFMEMORY;
                                else {
                                    /* the string came from the heap of this library, so move it to the task heap. */
                                    SIZE_T len = wcslen(uri) + 1;
                                    WCHAR *task = CoTaskMemAlloc(len * sizeof(WCHAR));
                                    if (task) {
                                        memcpy(task, uri, len * sizeof(WCHAR));
                                        WicFree(schema.pwszVal);
                                        schema.pwszVal = task;
                                    } else {
                                        WicFree(schema.pwszVal);
                                        schema.vt = VT_EMPTY;
                                        hr = E_OUTOFMEMORY;
                                    }
                                }
                            }
                        }
                    }
                }
                if (SUCCEEDED(hr))
                    hr = w->lpVtbl->SetValue(w, &schema, &id, value);
                PropVariantClear(&id);
                PropVariantClear(&schema);
                w->lpVtbl->Release(w);
            }
        }
    }
    cursor_done(&cur);
    path_free(&path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE q_remove_by_name(void *This, LPCWSTR name)
{
    WicQuery *q = query_of(This);
    if (!name)
        return E_INVALIDARG;
    if (!q->writable)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    Path path;
    HRESULT hr = path_parse(name, &path);
    if (FAILED(hr)) {
        path_free(&path);
        return hr;
    }
    Cursor cur;
    cursor_start(q, path.absolute, &cur);
    for (UINT i = 0; i + 1 < path.count && SUCCEEDED(hr); i++) {
        Found f;
        if (look(&cur, &path.seg[i], &f) && f.block)
            cursor_set_handler(&cur, f.block);
        else
            hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    }
    if (SUCCEEDED(hr)) {
        Found f;
        const Segment *last = &path.seg[path.count - 1];
        if (!look(&cur, last, &f)) {
            hr = WINCODEC_ERR_PROPERTYNOTFOUND;
        } else if (f.at_root) {
            hr = cur.bwriter ? cur.bwriter->lpVtbl->RemoveWriterByIndex(cur.bwriter, f.block_index)
                             : WINCODEC_ERR_UNSUPPORTEDOPERATION;
            f.block->lpVtbl->Release(f.block);
        } else {
            if (f.block)
                f.block->lpVtbl->Release(f.block);
            IWICMetadataWriter *w = nullptr;
            hr = cur.handler->lpVtbl->QueryInterface(cur.handler, &IID_IWICMetadataWriter, (void **)&w);
            if (SUCCEEDED(hr)) {
                hr = w->lpVtbl->RemoveValueByIndex(w, f.item);
                w->lpVtbl->Release(w);
            }
        }
    }
    cursor_done(&cur);
    path_free(&path);
    return hr;
}

static const IWICMetadataQueryReaderVtbl g_query_reader_vtbl = {
    WIC_IUNKNOWN_FUNCS, VTBL_FN(q_get_container), VTBL_FN(q_get_location), VTBL_FN(q_get_by_name),
    VTBL_FN(q_get_enumerator),
};
static const IWICMetadataQueryWriterVtbl g_query_writer_vtbl = {
    WIC_IUNKNOWN_FUNCS, VTBL_FN(q_get_container), VTBL_FN(q_get_location), VTBL_FN(q_get_by_name),
    VTBL_FN(q_get_enumerator), VTBL_FN(q_set_by_name), VTBL_FN(q_remove_by_name),
};

static const WicIfaceEntry g_query_reader_ifaces[] = {WIC_ENTRY(WicQuery, IID_IWICMetadataQueryReader, reader)};
static const WicIfaceEntry g_query_writer_ifaces[] = {
    WIC_ENTRY(WicQuery, IID_IWICMetadataQueryWriter, writer),
    WIC_ENTRY(WicQuery, IID_IWICMetadataQueryReader, writer),
};
static const WicClass g_query_reader_class = {query_destroy, g_query_reader_ifaces, 1};
static const WicClass g_query_writer_class = {query_destroy, g_query_writer_ifaces, 2};

static HRESULT query_create(IWICMetadataBlockReader *blocks, IWICMetadataBlockWriter *bwriter,
                            IWICMetadataReader *handler, const WCHAR *location, const GUID *container, BOOL writable,
                            void **out)
{
    WicQuery *q = WicNew(sizeof(*q), writable ? &g_query_writer_class : &g_query_reader_class);
    if (!q)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&q->lock);
    WIC_SLOT(q, reader, &g_query_reader_vtbl);
    WIC_SLOT(q, writer, &g_query_writer_vtbl);
    q->container = *container;
    q->writable = writable;
    q->location = WicStrDup(location);
    if (!q->location) {
        WicRelease(&q->reader);
        return E_OUTOFMEMORY;
    }
    if (blocks) {
        blocks->lpVtbl->AddRef(blocks);
        q->blocks = blocks;
    }
    if (bwriter) {
        bwriter->lpVtbl->AddRef(bwriter);
        q->bwriter = bwriter;
    }
    if (handler) {
        handler->lpVtbl->AddRef(handler);
        q->handler = handler;
    }
    *out = writable ? (void *)&q->writer : (void *)&q->reader;
    return S_OK;
}

HRESULT WicQueryFromBlocks(IWICMetadataBlockReader *blocks, IWICMetadataQueryReader **out)
{
    GUID container;
    HRESULT hr = blocks->lpVtbl->GetContainerFormat(blocks, &container);
    if (FAILED(hr))
        return hr;
    return query_create(blocks, nullptr, nullptr, L"/", &container, FALSE, (void **)out);
}

HRESULT WicMetaQueryWriterFromBlocks(IWICMetadataBlockWriter *blocks, IWICMetadataQueryWriter **out)
{
    GUID container;
    IWICMetadataBlockReader *reader = nullptr;
    HRESULT hr = blocks->lpVtbl->QueryInterface(blocks, &IID_IWICMetadataBlockReader, (void **)&reader);
    if (FAILED(hr))
        return hr;
    hr = reader->lpVtbl->GetContainerFormat(reader, &container);
    if (SUCCEEDED(hr))
        hr = query_create(reader, blocks, nullptr, L"/", &container, TRUE, (void **)out);
    reader->lpVtbl->Release(reader);
    return hr;
}

HRESULT WicMetaQueryWriterCreate(const GUID *format, const GUID *vendor, IWICMetadataQueryWriter **out)
{
    IWICMetadataBlockWriter *blocks;
    HRESULT hr = WicBlockWriterCreate(format, &blocks);
    if (FAILED(hr))
        return hr;
    hr = WicMetaQueryWriterFromBlocks(blocks, out);
    blocks->lpVtbl->Release(blocks);
    return hr;
}

HRESULT WicMetaQueryWriterFromReader(IWICMetadataQueryReader *reader, const GUID *vendor, IWICMetadataQueryWriter **out)
{
    GUID container;
    HRESULT hr = reader->lpVtbl->GetContainerFormat(reader, &container);
    if (FAILED(hr))
        return hr;
    WicQuery *q = (WicQuery *)WicSlotBase(reader);
    if (q->base.cls != &g_query_reader_class && q->base.cls != &g_query_writer_class)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICMetadataBlockWriter *bw;
    hr = WicBlockWriterCreate(&container, &bw);
    if (FAILED(hr))
        return hr;
    if (q->blocks) {
        hr = bw->lpVtbl->InitializeFromBlockReader(bw, q->blocks);
    } else if (q->handler) {
        IWICMetadataWriter *copy;
        hr = WicMetaCopyToWriter(q->handler, &copy);
        if (SUCCEEDED(hr)) {
            hr = bw->lpVtbl->AddWriter(bw, copy);
            copy->lpVtbl->Release(copy);
        }
    }
    if (SUCCEEDED(hr))
        hr = WicMetaQueryWriterFromBlocks(bw, out);
    bw->lpVtbl->Release(bw);
    return hr;
}
