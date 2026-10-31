/* object paths: \\server\root\cimv2:Class.Key="value", and the IWbemPath objects that read and write them. */
#include "wbemcore.h"

void wp_init(WPath *p)
{
    memset(p, 0, sizeof(*p));
}

static void keys_free(WKey *keys, UINT count)
{
    for (UINT i = 0; i < count; i++) {
        wb_free(keys[i].name);
        VariantClear(&keys[i].value);
    }
    wb_free(keys);
}

void wp_free(WPath *p)
{
    wb_free(p->server);
    for (UINT i = 0; i < p->ns_count; i++)
        wb_free(p->ns[i]);
    wb_free(p->ns);
    wb_free(p->class_name);
    keys_free(p->keys, p->key_count);
    for (UINT i = 0; i < p->scope_count; i++) {
        wb_free(p->scopes[i].class_name);
        keys_free(p->scopes[i].keys, p->scopes[i].key_count);
    }
    wb_free(p->scopes);
    wb_free(p->original);
    wp_init(p);
}

static WKey *keys_copy(const WKey *keys, UINT count)
{
    if (!count)
        return nullptr;
    WKey *copy = wb_alloc(count * sizeof(WKey));
    for (UINT i = 0; i < count; i++) {
        copy[i].name = wb_wcsdup(keys[i].name);
        wv_copy(&copy[i].value, &keys[i].value);
        copy[i].type = keys[i].type;
    }
    return copy;
}

HRESULT wp_copy(WPath *dst, const WPath *src)
{
    wp_init(dst);
    *dst = *src;
    dst->server = wb_wcsdup(src->server);
    dst->original = wb_wcsdup(src->original);
    dst->class_name = wb_wcsdup(src->class_name);
    dst->ns = nullptr;
    if (src->ns_count) {
        dst->ns = wb_alloc(src->ns_count * sizeof(WCHAR *));
        for (UINT i = 0; i < src->ns_count; i++)
            dst->ns[i] = wb_wcsdup(src->ns[i]);
    }
    dst->keys = keys_copy(src->keys, src->key_count);
    dst->scopes = nullptr;
    dst->scope_count = 0;
    return S_OK;
}

/* ------------------------------------------------------------------ parsing */

static BOOL is_separator(WCHAR c)
{
    return c == '\\' || c == '/';
}

static BOOL is_name_char(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c > 0x7F;
}

static void add_ns(WPath *p, const WCHAR *start, SIZE_T length)
{
    p->ns = wb_realloc(p->ns, (p->ns_count + 1) * sizeof(WCHAR *));
    p->ns[p->ns_count++] = wb_wcsndup(start, length);
}

/* the position of the colon that ends the namespace, outside quotes, or null. */
static const WCHAR *find_colon(const WCHAR *text)
{
    BOOL quoted = FALSE;
    WCHAR quote = 0;
    for (const WCHAR *c = text; *c; c++) {
        if (quoted) {
            if (*c == '\\' && c[1])
                c++;
            else if (*c == quote)
                quoted = FALSE;
        } else if (*c == '"' || *c == '\'') {
            quoted = TRUE;
            quote = *c;
        } else if (*c == ':') {
            return c;
        } else if (*c == '=') {
            return nullptr;
        }
    }
    return nullptr;
}

static HRESULT parse_value(const WCHAR **cursor, WKey *key)
{
    const WCHAR *c = *cursor;
    VariantInit(&key->value);
    if (*c == '"' || *c == '\'') {
        WCHAR quote = *c++;
        WBuf buf = {0};
        for (; *c && *c != quote; c++) {
            if (*c == '\\' && c[1])
                c++;
            wb_buf_addc(&buf, *c);
        }
        if (*c != quote) {
            wb_free(buf.text);
            return WBEM_E_INVALID_OBJECT_PATH;
        }
        c++;
        WCHAR *text = wb_buf_take(&buf);
        key->value.vt = VT_BSTR;
        key->value.bstrVal = SysAllocString(text);
        wb_free(text);
        key->type = CIM_STRING;
    } else if (!_wcsnicmp(c, L"TRUE", 4) && !is_name_char(c[4])) {
        key->value.vt = VT_BOOL;
        key->value.boolVal = VARIANT_TRUE;
        key->type = CIM_BOOLEAN;
        c += 4;
    } else if (!_wcsnicmp(c, L"FALSE", 5) && !is_name_char(c[5])) {
        key->value.vt = VT_BOOL;
        key->value.boolVal = VARIANT_FALSE;
        key->type = CIM_BOOLEAN;
        c += 5;
    } else {
        const WCHAR *start = c;
        if (*c == '-' || *c == '+')
            c++;
        while ((*c >= '0' && *c <= '9') || *c == '.' || *c == 'x' || *c == 'X' || (*c >= 'a' && *c <= 'f') ||
               (*c >= 'A' && *c <= 'F'))
            c++;
        if (c == start)
            return WBEM_E_INVALID_OBJECT_PATH;
        WCHAR *text = wb_wcsndup(start, (SIZE_T)(c - start));
        BOOL fraction = wcschr(text, '.') != nullptr;
        if (fraction) {
            double d;
            if (FAILED(VarR8FromStr(text, 0x409, 0, &d))) {
                wb_free(text);
                return WBEM_E_INVALID_OBJECT_PATH;
            }
            key->value.vt = VT_R8;
            key->value.dblVal = d;
            key->type = CIM_REAL64;
        } else {
            VARIANT in;
            VariantInit(&in);
            in.vt = VT_BSTR;
            in.bstrVal = SysAllocString(text);
            VARIANT out;
            wv_init(&out);
            HRESULT hr = wv_coerce(&out, &in, CIM_SINT32);
            if (SUCCEEDED(hr)) {
                key->type = CIM_SINT32;
            } else {
                hr = wv_coerce(&out, &in, CIM_SINT64);
                key->type = CIM_SINT64;
            }
            VariantClear(&in);
            if (FAILED(hr)) {
                wb_free(text);
                return WBEM_E_INVALID_OBJECT_PATH;
            }
            key->value = out;
        }
        wb_free(text);
    }
    *cursor = c;
    return S_OK;
}

HRESULT wp_parse(WPath *p, const WCHAR *text, ULONG mode)
{
    wp_free(p);
    p->original = wb_wcsdup(text);
    p->parse_flags = mode;
    const WCHAR *c = text;
    while (*c == ' ')
        c++;
    if (!*c)
        return S_OK;
    if (is_separator(c[0]) && is_separator(c[1])) {
        p->has_server_prefix = TRUE;
        c += 2;
        const WCHAR *start = c;
        while (*c && !is_separator(*c))
            c++;
        if (c == start)
            return WBEM_E_INVALID_OBJECT_PATH;
        p->server = wb_wcsndup(start, (SIZE_T)(c - start));
        if (*c)
            c++;
    }
    const WCHAR *colon = find_colon(c);
    const WCHAR *object = c;
    BOOL namespace_only = FALSE;
    if (colon) {
        const WCHAR *start = c;
        for (const WCHAR *d = c; d <= colon; d++) {
            if (d == colon || is_separator(*d)) {
                if (d == start)
                    return WBEM_E_INVALID_OBJECT_PATH;
                add_ns(p, start, (SIZE_T)(d - start));
                start = d + 1;
            }
        }
        object = colon + 1;
    } else {
        /* no colon: a namespace when it has a server prefix or separators and no key. */
        BOOL has_separator = FALSE, has_equals = FALSE;
        for (const WCHAR *d = c; *d; d++) {
            has_separator |= is_separator(*d);
            has_equals |= *d == '=';
        }
        if ((p->has_server_prefix || has_separator || (mode & WBEMPATH_TREAT_SINGLE_IDENT_AS_NS)) && !has_equals)
            namespace_only = TRUE;
        if (namespace_only) {
            const WCHAR *start = c;
            for (const WCHAR *d = c;; d++) {
                if (!*d || is_separator(*d)) {
                    if (d == start) {
                        if (!*d)
                            break;
                        return WBEM_E_INVALID_OBJECT_PATH;
                    }
                    add_ns(p, start, (SIZE_T)(d - start));
                    start = d + 1;
                }
                if (!*d)
                    break;
            }
            return S_OK;
        }
    }
    /* the class and its keys. */
    const WCHAR *start = object;
    while (is_name_char(*object))
        object++;
    if (object == start) {
        if (!*object && colon)
            return S_OK;
        return WBEM_E_INVALID_OBJECT_PATH;
    }
    p->class_name = wb_wcsndup(start, (SIZE_T)(object - start));
    p->has_class = TRUE;
    if (!*object)
        return S_OK;
    if (*object == '=') {
        object++;
        if (*object == '@') {
            p->singleton = TRUE;
            object++;
        } else {
            WKey key = {0};
            HRESULT hr = parse_value(&object, &key);
            if (FAILED(hr))
                return hr;
            p->keys = wb_alloc(sizeof(WKey));
            p->keys[0] = key;
            p->key_count = 1;
        }
    } else if (*object == '.') {
        object++;
        for (;;) {
            const WCHAR *name_start = object;
            while (is_name_char(*object))
                object++;
            if (object == name_start || *object != '=')
                return WBEM_E_INVALID_OBJECT_PATH;
            WKey key = {0};
            key.name = wb_wcsndup(name_start, (SIZE_T)(object - name_start));
            object++;
            HRESULT hr = parse_value(&object, &key);
            if (FAILED(hr)) {
                wb_free(key.name);
                return hr;
            }
            p->keys = wb_realloc(p->keys, (p->key_count + 1) * sizeof(WKey));
            p->keys[p->key_count++] = key;
            while (*object == ' ')
                object++;
            if (*object == ',') {
                object++;
                while (*object == ' ')
                    object++;
                continue;
            }
            break;
        }
    }
    while (*object == ' ')
        object++;
    return *object ? WBEM_E_INVALID_OBJECT_PATH : S_OK;
}

/* ------------------------------------------------------------------ text */

WCHAR *wp_keys_text(const WKey *keys, UINT count, BOOL singleton, BOOL quoted_names)
{
    WBuf buf = {0};
    (void)quoted_names;
    if (singleton) {
        wb_buf_add(&buf, L"=@");
    } else if (count == 1 && !keys[0].name) {
        WCHAR *v = wv_text(&keys[0].value, keys[0].type);
        wb_buf_addc(&buf, '=');
        wb_buf_add(&buf, v);
        wb_free(v);
    } else {
        for (UINT i = 0; i < count; i++) {
            WCHAR *v = wv_text(&keys[i].value, keys[i].type);
            wb_buf_addc(&buf, i ? ',' : '.');
            wb_buf_add(&buf, keys[i].name ? keys[i].name : L"");
            wb_buf_addc(&buf, '=');
            wb_buf_add(&buf, v);
            wb_free(v);
        }
    }
    return wb_buf_take(&buf);
}

WCHAR *wp_text(const WPath *p, LONG flags)
{
    WBuf buf = {0};
    BOOL relative_only = (flags & WBEMPATH_GET_RELATIVE_ONLY) != 0;
    BOOL namespace_only = (flags & (WBEMPATH_GET_NAMESPACE_ONLY | WBEMPATH_GET_SERVER_AND_NAMESPACE_ONLY)) != 0;
    BOOL server = (flags & (WBEMPATH_GET_SERVER_TOO | WBEMPATH_GET_SERVER_AND_NAMESPACE_ONLY)) != 0;
    if ((flags & WBEMPATH_GET_ORIGINAL) && p->original)
        return wb_wcsdup(p->original);
    if (server && !relative_only) {
        wb_buf_add(&buf, L"\\\\");
        wb_buf_add(&buf, p->server ? p->server : L".");
        if (p->ns_count || (p->has_class && !namespace_only))
            wb_buf_addc(&buf, '\\');
    }
    if (!relative_only) {
        for (UINT i = 0; i < p->ns_count; i++) {
            if (i)
                wb_buf_addc(&buf, '\\');
            wb_buf_add(&buf, p->ns[i]);
        }
    }
    if (namespace_only)
        return wb_buf_take(&buf);
    if (p->has_class && p->class_name) {
        if (p->ns_count && !relative_only)
            wb_buf_addc(&buf, ':');
        wb_buf_add(&buf, p->class_name);
        if (p->key_count || p->singleton) {
            WCHAR *k = wp_keys_text(p->keys, p->key_count, p->singleton, FALSE);
            wb_buf_add(&buf, k);
            wb_free(k);
        }
    }
    return wb_buf_take(&buf);
}

ULONGLONG wp_info(const WPath *p)
{
    ULONGLONG info = WBEMPATH_INFO_V1_COMPLIANT | WBEMPATH_INFO_V2_COMPLIANT | WBEMPATH_INFO_CIM_COMPLIANT |
                     WBEMPATH_INFO_WMI_PATH;
    if (p->server) {
        info |= WBEMPATH_INFO_PATH_HAD_SERVER;
        if (!wcscmp(p->server, L"."))
            info |= WBEMPATH_INFO_ANON_LOCAL_MACHINE;
        else
            info |= WBEMPATH_INFO_HAS_MACHINE_NAME;
    }
    if (p->has_class) {
        if (p->key_count || p->singleton)
            info |= WBEMPATH_INFO_IS_INST_REF;
        else
            info |= WBEMPATH_INFO_IS_CLASS_REF;
        if (p->singleton)
            info |= WBEMPATH_INFO_CONTAINS_SINGLETON;
        if (p->key_count == 1 && !p->keys[0].name)
            info |= WBEMPATH_INFO_HAS_IMPLIED_KEY;
    } else if (p->server || p->ns_count) {
        info |= WBEMPATH_INFO_SERVER_NAMESPACE_ONLY;
    }
    if (p->scope_count)
        info |= WBEMPATH_INFO_HAS_SUBSCOPES;
    if (p->has_class && !p->ns_count && !p->server && !p->key_count && !p->singleton)
        info |= WBEMPATH_INFO_IS_SINGLE_TOKEN;
    return info;
}

/* ------------------------------------------------------------------ the com objects */

typedef struct ComPath ComPath;

typedef struct ComKeyList {
    const IWbemPathKeyListVtbl *lpVtbl;
    LONG refs;
    ComPath *path;
} ComKeyList;

struct ComPath {
    const IWbemPathVtbl *lpVtbl;
    LONG refs;
    WPath path;
};

static HRESULT copy_out(const WCHAR *text, ULONG *length, WCHAR *buffer)
{
    SIZE_T need = text ? wcslen(text) + 1 : 1;
    if (!length)
        return WBEM_E_INVALID_PARAMETER;
    ULONG capacity = *length;
    *length = (ULONG)need;
    if (!buffer)
        return S_OK;
    if (capacity < need)
        return WBEM_E_BUFFER_TOO_SMALL;
    memcpy(buffer, text ? text : L"", need * sizeof(WCHAR));
    return S_OK;
}

/* key list */

static HRESULT STDMETHODCALLTYPE kl_QueryInterface(IWbemPathKeyList *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemPathKeyList)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("kl", riid, out);
}

static ULONG STDMETHODCALLTYPE kl_AddRef(IWbemPathKeyList *This)
{
    return (ULONG)InterlockedIncrement(&((ComKeyList *)This)->refs);
}

static ULONG STDMETHODCALLTYPE kl_Release(IWbemPathKeyList *This)
{
    ComKeyList *k = (ComKeyList *)This;
    LONG n = InterlockedDecrement(&k->refs);
    if (!n) {
        ((IWbemPath *)k->path)->lpVtbl->Release((IWbemPath *)k->path);
        wb_free(k);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE kl_GetCount(IWbemPathKeyList *This, ULONG *count)
{
    *count = ((ComKeyList *)This)->path->path.key_count;
    return S_OK;
}

static HRESULT set_key(WPath *p, LPCWSTR name, const VARIANT *value, CIMTYPE type)
{
    if (name) {
        for (UINT i = 0; i < p->key_count; i++) {
            if (p->keys[i].name && wb_eq(p->keys[i].name, name)) {
                VariantClear(&p->keys[i].value);
                wv_copy(&p->keys[i].value, value);
                p->keys[i].type = type;
                return S_OK;
            }
        }
    }
    p->keys = wb_realloc(p->keys, (p->key_count + 1) * sizeof(WKey));
    WKey *k = &p->keys[p->key_count++];
    memset(k, 0, sizeof(*k));
    k->name = wb_wcsdup(name);
    wv_copy(&k->value, value);
    k->type = type;
    p->singleton = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_SetKey(IWbemPathKeyList *This, LPCWSTR name, ULONG flags, ULONG cimtype, LPVOID value)
{
    VARIANT v;
    VariantInit(&v);
    switch (cimtype) {
    case CIM_SINT32:
        v.vt = VT_I4;
        v.lVal = *(LONG *)value;
        break;
    case CIM_BOOLEAN:
        v.vt = VT_BOOL;
        v.boolVal = *(VARIANT_BOOL *)value ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case CIM_STRING:
    case CIM_REFERENCE:
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString((const WCHAR *)value);
        break;
    default:
        return WBEM_E_INVALID_PARAMETER;
    }
    HRESULT hr = set_key(&((ComKeyList *)This)->path->path, name, &v, (CIMTYPE)cimtype);
    VariantClear(&v);
    return hr;
}

static HRESULT STDMETHODCALLTYPE kl_SetKey2(IWbemPathKeyList *This, LPCWSTR name, ULONG flags, ULONG cimtype, VARIANT *value)
{
    if (!value)
        return WBEM_E_INVALID_PARAMETER;
    return set_key(&((ComKeyList *)This)->path->path, name, value, (CIMTYPE)cimtype);
}

static HRESULT STDMETHODCALLTYPE kl_GetKey(IWbemPathKeyList *This, ULONG index, ULONG flags, ULONG *name_size, LPWSTR name,
                                           ULONG *value_size, LPVOID value, ULONG *cimtype)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    if (index >= p->key_count)
        return WBEM_E_NOT_FOUND;
    WKey *k = &p->keys[index];
    HRESULT hr = copy_out(k->name, name_size, name);
    if (FAILED(hr))
        return hr;
    if (cimtype)
        *cimtype = (ULONG)k->type;
    if (value_size) {
        ULONG need;
        if (k->type == CIM_SINT32)
            need = sizeof(LONG);
        else if (k->type == CIM_BOOLEAN)
            need = sizeof(VARIANT_BOOL);
        else
            need = (ULONG)((k->value.bstrVal ? wcslen(k->value.bstrVal) + 1 : 1) * sizeof(WCHAR));
        ULONG capacity = *value_size;
        *value_size = need;
        if (value) {
            if (capacity < need)
                return WBEM_E_BUFFER_TOO_SMALL;
            if (k->type == CIM_SINT32)
                *(LONG *)value = k->value.lVal;
            else if (k->type == CIM_BOOLEAN)
                *(VARIANT_BOOL *)value = k->value.boolVal;
            else
                memcpy(value, k->value.bstrVal ? k->value.bstrVal : L"", need);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_GetKey2(IWbemPathKeyList *This, ULONG index, ULONG flags, ULONG *name_size, LPWSTR name,
                                            VARIANT *value, ULONG *cimtype)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    if (index >= p->key_count)
        return WBEM_E_NOT_FOUND;
    WKey *k = &p->keys[index];
    HRESULT hr = copy_out(k->name, name_size, name);
    if (FAILED(hr))
        return hr;
    if (cimtype)
        *cimtype = (ULONG)k->type;
    if (value) {
        VariantInit(value);
        VariantCopy(value, &k->value);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_RemoveKey(IWbemPathKeyList *This, LPCWSTR name, ULONG flags)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    for (UINT i = 0; i < p->key_count; i++) {
        if (p->keys[i].name && wb_eq(p->keys[i].name, name)) {
            wb_free(p->keys[i].name);
            VariantClear(&p->keys[i].value);
            memmove(&p->keys[i], &p->keys[i + 1], (p->key_count - i - 1) * sizeof(WKey));
            p->key_count--;
            return S_OK;
        }
    }
    return WBEM_E_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE kl_RemoveAllKeys(IWbemPathKeyList *This, ULONG flags)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    keys_free(p->keys, p->key_count);
    p->keys = nullptr;
    p->key_count = 0;
    p->singleton = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_MakeSingleton(IWbemPathKeyList *This, BOOLEAN value)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    if (value) {
        keys_free(p->keys, p->key_count);
        p->keys = nullptr;
        p->key_count = 0;
    }
    p->singleton = value != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_GetInfo(IWbemPathKeyList *This, ULONG request, ULONGLONG *response)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    ULONGLONG info = 0;
    if (p->singleton)
        info |= WBEMPATH_INFO_CONTAINS_SINGLETON;
    if (p->key_count == 1 && !p->keys[0].name)
        info |= WBEMPATH_INFO_HAS_IMPLIED_KEY;
    *response = info;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE kl_GetText(IWbemPathKeyList *This, LONG flags, ULONG *length, LPWSTR text)
{
    WPath *p = &((ComKeyList *)This)->path->path;
    WCHAR *t = wp_keys_text(p->keys, p->key_count, p->singleton, FALSE);
    HRESULT hr = copy_out(t, length, text);
    wb_free(t);
    return hr;
}

static const IWbemPathKeyListVtbl KEY_LIST_VTBL = {
    kl_QueryInterface, kl_AddRef,        kl_Release,    kl_GetCount, kl_SetKey,       kl_SetKey2,   kl_GetKey,
    kl_GetKey2,        kl_RemoveKey,     kl_RemoveAllKeys, kl_MakeSingleton, kl_GetInfo, kl_GetText,
};

/* path */

#define PATH(This) (&((ComPath *)(This))->path)

static HRESULT STDMETHODCALLTYPE cp_QueryInterface(IWbemPath *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemPath)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("cp", riid, out);
}

static ULONG STDMETHODCALLTYPE cp_AddRef(IWbemPath *This)
{
    return (ULONG)InterlockedIncrement(&((ComPath *)This)->refs);
}

static ULONG STDMETHODCALLTYPE cp_Release(IWbemPath *This)
{
    ComPath *c = (ComPath *)This;
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        wp_free(&c->path);
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE cp_SetText(IWbemPath *This, ULONG mode, LPCWSTR text)
{
    if (!text)
        return WBEM_E_INVALID_PARAMETER;
    if (!(mode & (WBEMPATH_CREATE_ACCEPT_RELATIVE | WBEMPATH_CREATE_ACCEPT_ABSOLUTE | WBEMPATH_CREATE_ACCEPT_ALL)))
        return WBEM_E_INVALID_PARAMETER;
    WPath fresh;
    wp_init(&fresh);
    HRESULT hr = wp_parse(&fresh, text, mode);
    if (FAILED(hr)) {
        wp_free(&fresh);
        return hr;
    }
    if (!(mode & WBEMPATH_CREATE_ACCEPT_ALL)) {
        BOOL absolute = fresh.has_server_prefix;
        if ((absolute && !(mode & WBEMPATH_CREATE_ACCEPT_ABSOLUTE)) || (!absolute && !(mode & WBEMPATH_CREATE_ACCEPT_RELATIVE))) {
            wp_free(&fresh);
            return WBEM_E_INVALID_PARAMETER;
        }
    }
    wp_free(PATH(This));
    *PATH(This) = fresh;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_GetText(IWbemPath *This, LONG flags, ULONG *length, LPWSTR text)
{
    WCHAR *t = wp_text(PATH(This), flags);
    HRESULT hr = copy_out(t, length, text);
    wb_free(t);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cp_GetInfo(IWbemPath *This, ULONG request, ULONGLONG *response)
{
    if (!response)
        return WBEM_E_INVALID_PARAMETER;
    *response = wp_info(PATH(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_SetServer(IWbemPath *This, LPCWSTR name)
{
    WPath *p = PATH(This);
    wb_free(p->server);
    p->server = wb_wcsdup(name);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_GetServer(IWbemPath *This, ULONG *length, LPWSTR name)
{
    WPath *p = PATH(This);
    if (!p->server)
        return WBEM_E_NOT_AVAILABLE;
    return copy_out(p->server, length, name);
}

static HRESULT STDMETHODCALLTYPE cp_GetNamespaceCount(IWbemPath *This, ULONG *count)
{
    *count = PATH(This)->ns_count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_SetNamespaceAt(IWbemPath *This, ULONG index, LPCWSTR name)
{
    WPath *p = PATH(This);
    if (index > p->ns_count)
        return WBEM_E_INVALID_PARAMETER;
    if (index == p->ns_count) {
        add_ns(p, name, wcslen(name));
    } else {
        wb_free(p->ns[index]);
        p->ns[index] = wb_wcsdup(name);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_GetNamespaceAt(IWbemPath *This, ULONG index, ULONG *length, LPWSTR name)
{
    WPath *p = PATH(This);
    if (index >= p->ns_count)
        return WBEM_E_INVALID_PARAMETER;
    return copy_out(p->ns[index], length, name);
}

static HRESULT STDMETHODCALLTYPE cp_RemoveNamespaceAt(IWbemPath *This, ULONG index)
{
    WPath *p = PATH(This);
    if (index >= p->ns_count)
        return WBEM_E_INVALID_PARAMETER;
    wb_free(p->ns[index]);
    memmove(&p->ns[index], &p->ns[index + 1], (p->ns_count - index - 1) * sizeof(WCHAR *));
    p->ns_count--;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_RemoveAllNamespaces(IWbemPath *This)
{
    WPath *p = PATH(This);
    for (UINT i = 0; i < p->ns_count; i++)
        wb_free(p->ns[i]);
    wb_free(p->ns);
    p->ns = nullptr;
    p->ns_count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_GetScopeCount(IWbemPath *This, ULONG *count)
{
    *count = PATH(This)->scope_count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_SetScope(IWbemPath *This, ULONG index, LPWSTR name)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE cp_SetScopeFromText(IWbemPath *This, ULONG index, LPWSTR text)
{
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT STDMETHODCALLTYPE cp_GetScope(IWbemPath *This, ULONG index, ULONG *length, LPWSTR name, IWbemPathKeyList **keys)
{
    return WBEM_E_INVALID_PARAMETER;
}

static HRESULT STDMETHODCALLTYPE cp_GetScopeAsText(IWbemPath *This, ULONG index, ULONG *length, LPWSTR text)
{
    return WBEM_E_INVALID_PARAMETER;
}

static HRESULT STDMETHODCALLTYPE cp_RemoveScope(IWbemPath *This, ULONG index)
{
    return WBEM_E_INVALID_PARAMETER;
}

static HRESULT STDMETHODCALLTYPE cp_RemoveAllScopes(IWbemPath *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_SetClassName(IWbemPath *This, LPCWSTR name)
{
    WPath *p = PATH(This);
    if (!name)
        return WBEM_E_INVALID_PARAMETER;
    wb_free(p->class_name);
    p->class_name = wb_wcsdup(name);
    p->has_class = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_GetClassName(IWbemPath *This, ULONG *length, LPWSTR name)
{
    WPath *p = PATH(This);
    if (!p->class_name)
        return WBEM_E_NOT_AVAILABLE;
    return copy_out(p->class_name, length, name);
}

static HRESULT STDMETHODCALLTYPE cp_GetKeyList(IWbemPath *This, IWbemPathKeyList **keys)
{
    ComPath *c = (ComPath *)This;
    if (!c->path.has_class)
        return WBEM_E_NOT_AVAILABLE;
    ComKeyList *k = wb_alloc(sizeof(ComKeyList));
    k->lpVtbl = &KEY_LIST_VTBL;
    k->refs = 1;
    k->path = c;
    This->lpVtbl->AddRef(This);
    *keys = (IWbemPathKeyList *)k;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_CreateClassPart(IWbemPath *This, LONG flags, LPCWSTR name)
{
    WPath *p = PATH(This);
    if (p->has_class)
        return WBEM_E_INVALID_OPERATION;
    wb_free(p->class_name);
    p->class_name = wb_wcsdup(name);
    p->has_class = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cp_DeleteClassPart(IWbemPath *This, LONG flags)
{
    WPath *p = PATH(This);
    wb_free(p->class_name);
    p->class_name = nullptr;
    p->has_class = FALSE;
    keys_free(p->keys, p->key_count);
    p->keys = nullptr;
    p->key_count = 0;
    p->singleton = FALSE;
    return S_OK;
}

static BOOL local_name(const WCHAR *machine)
{
    if (!machine || !*machine || !wcscmp(machine, L"."))
        return TRUE;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(name);
    return GetComputerNameW(name, &size) && wb_eq(machine, name);
}

static BOOL STDMETHODCALLTYPE cp_IsRelative(IWbemPath *This, LPWSTR machine, LPWSTR ns)
{
    WPath *p = PATH(This);
    if (p->server && !local_name(p->server) && (!machine || !wb_eq(p->server, machine)))
        return FALSE;
    if (p->server && machine && !wb_eq(p->server, machine) && !(local_name(p->server) && local_name(machine)))
        return FALSE;
    WCHAR *own = wp_text(p, WBEMPATH_GET_NAMESPACE_ONLY);
    BOOL same = !ns || !*ns || !p->ns_count || wb_eq(own, ns);
    wb_free(own);
    return same;
}

static BOOL STDMETHODCALLTYPE cp_IsRelativeOrChild(IWbemPath *This, LPWSTR machine, LPWSTR ns, LONG flags)
{
    WPath *p = PATH(This);
    if (p->server && machine && !wb_eq(p->server, machine) && !(local_name(p->server) && local_name(machine)))
        return FALSE;
    WCHAR *own = wp_text(p, WBEMPATH_GET_NAMESPACE_ONLY);
    SIZE_T n = ns ? wcslen(ns) : 0;
    BOOL ok = !n || !p->ns_count || (!_wcsnicmp(own, ns, n) && (!own[n] || own[n] == '\\'));
    wb_free(own);
    return ok;
}

static BOOL STDMETHODCALLTYPE cp_IsLocal(IWbemPath *This, LPCWSTR machine)
{
    WPath *p = PATH(This);
    return !p->server || local_name(p->server) || (machine && wb_eq(p->server, machine));
}

static BOOL STDMETHODCALLTYPE cp_IsSameClassName(IWbemPath *This, LPCWSTR name)
{
    WPath *p = PATH(This);
    return p->class_name && name && wb_eq(p->class_name, name);
}

static const IWbemPathVtbl PATH_VTBL = {
    cp_QueryInterface,   cp_AddRef,           cp_Release,           cp_SetText,           cp_GetText,
    cp_GetInfo,          cp_SetServer,        cp_GetServer,         cp_GetNamespaceCount, cp_SetNamespaceAt,
    cp_GetNamespaceAt,   cp_RemoveNamespaceAt, cp_RemoveAllNamespaces, cp_GetScopeCount,    cp_SetScope,
    cp_SetScopeFromText, cp_GetScope,         cp_GetScopeAsText,    cp_RemoveScope,       cp_RemoveAllScopes,
    cp_SetClassName,     cp_GetClassName,     cp_GetKeyList,        cp_CreateClassPart,   cp_DeleteClassPart,
    cp_IsRelative,       cp_IsRelativeOrChild, cp_IsLocal,          cp_IsSameClassName,
};

HRESULT wp_com_path_new(REFIID riid, void **out)
{
    ComPath *c = wb_alloc(sizeof(ComPath));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &PATH_VTBL;
    c->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = cp_QueryInterface((IWbemPath *)c, riid, out);
    cp_Release((IWbemPath *)c);
    return hr;
}
