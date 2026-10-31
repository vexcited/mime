/* the property system object, the lookups of descriptions and the classes propsys serves. */
#include "propsys_private.h"

#define E_FAIL_ ((HRESULT)0x80004005L)

/* the key whose description is never looked up. */
static BOOL is_unlisted_key(REFPROPERTYKEY key)
{
    static const GUID unlisted = {0x9E5E05AC, 0x1936, 0x4A75, {0x94, 0xF7, 0x47, 0x04, 0xB8, 0xB0, 0x19, 0x23}};
    return IsEqualGUID(&key->fmtid, &unlisted);
}

HRESULT WINAPI PSGetPropertyDescription(REFPROPERTYKEY key, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (is_unlisted_key(key))
        return E_FAIL_;
    SCHEMA *schema;
    DWORD index;
    HRESULT hr = SchemaFindKey(key, &schema, &index);
    if (FAILED(hr))
        return hr;
    hr = DescCreate(schema, index, riid, object);
    SchemaRelease(schema);
    return hr;
}

HRESULT WINAPI PSGetPropertyDescriptionByName(LPCWSTR name, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!name)
        return E_POINTER;
    if (*name == '{')
        return DescCreateDummy(name, riid, object);
    SCHEMA *schema;
    DWORD index;
    HRESULT hr = SchemaFindName(name, &schema, &index);
    if (FAILED(hr))
        return hr;
    hr = DescCreate(schema, index, riid, object);
    SchemaRelease(schema);
    return hr;
}

HRESULT WINAPI PSEnumeratePropertyDescriptions(PROPDESC_ENUMFILTER filter, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    SCHEMA **schemas;
    DWORD *indices, count;
    HRESULT hr = SchemaEnumerate(filter, &schemas, &indices, &count);
    if (FAILED(hr))
        return hr;
    return DescCreateList(schemas, indices, count, riid, object);
}

/* ------------------------------------------------------------------ lists from a string */

typedef struct {
    SCHEMA **schemas;
    DWORD *indices, *view;
    DWORD count, capacity;
} LISTBUILD;

static BOOL list_add(LISTBUILD *b, SCHEMA *schema, DWORD index, DWORD mask, DWORD value, DWORD width)
{
    if (b->count == b->capacity) {
        DWORD grown = b->capacity ? b->capacity * 2 : 8;
        SCHEMA **schemas = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*schemas));
        DWORD *indices = HeapAlloc(GetProcessHeap(), 0, grown * sizeof(*indices));
        DWORD *view = HeapAlloc(GetProcessHeap(), 0, grown * 3 * sizeof(*view));
        if (!schemas || !indices || !view) {
            HeapFree(GetProcessHeap(), 0, schemas);
            HeapFree(GetProcessHeap(), 0, indices);
            HeapFree(GetProcessHeap(), 0, view);
            return FALSE;
        }
        if (b->count) {
            memcpy(schemas, b->schemas, b->count * sizeof(*schemas));
            memcpy(indices, b->indices, b->count * sizeof(*indices));
            memcpy(view, b->view, b->count * 3 * sizeof(*view));
        }
        HeapFree(GetProcessHeap(), 0, b->schemas);
        HeapFree(GetProcessHeap(), 0, b->indices);
        HeapFree(GetProcessHeap(), 0, b->view);
        b->schemas = schemas;
        b->indices = indices;
        b->view = view;
        b->capacity = grown;
    }
    b->schemas[b->count] = schema;
    b->indices[b->count] = index;
    b->view[b->count * 3] = mask;
    b->view[b->count * 3 + 1] = value;
    b->view[b->count * 3 + 2] = width;
    b->count++;
    return TRUE;
}

/*
 * "prop:" and then names separated by semicolons, each with characters before it that set how the column shows:
 * 0 to 3 for the lists, / | right and center, ^ a new group, ~ no label, # to fill, ! to wrap, * only if present,
 * + and - the sort direction and (n) the width.
 */
HRESULT WINAPI PSGetPropertyDescriptionListFromString(LPCWSTR list, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!list)
        return E_POINTER;
    LISTBUILD b = {};
    BOOL omitted = FALSE;
    LPCWSTR p = list;
    if (!_wcsnicmp(p, L"prop:", 5))
        p += 5;
    for (;;) {
        DWORD flags = 0, width = 0;
        BOOL present = FALSE, ascending = FALSE;
        BOOL named = FALSE;
        while (!named) {
            WCHAR c = *p;
            switch (c) {
            case '0': flags |= 0x1C0; break;
            case '1': flags |= 0x180; break;
            case '2': flags |= 0x100; break;
            case '3': flags |= 0x800; break;
            case '^': flags |= 4; break;
            case '|': flags |= 1; break;
            case '~': flags |= 0x200; break;
            case '/': flags |= 2; break;
            case '!': flags |= 0x1000; break;
            case '#': flags |= 8; break;
            case '*': present = TRUE; break;
            case '+': ascending = TRUE; break;
            case '-': flags |= 0x10; break;
            case '(': {
                LPCWSTR digits = p + 1;
                WCHAR *end;
                LONG n = 0;
                BOOL negative = *digits == '-';
                if (negative || *digits == '+')
                    digits++;
                end = (WCHAR *)digits;
                while (*end >= '0' && *end <= '9')
                    n = n * 10 + (*end++ - '0');
                if (end == digits || *end != ')')
                    goto done;
                width = (DWORD)(negative ? -n : n);
                p = end;
                break;
            }
            case 0:
                goto done;
            default:
                named = TRUE;
                continue;
            }
            p++;
        }
        {
            LPCWSTR end = wcschr(p, ';');
            SIZE_T length = end ? (SIZE_T)(end - p) : wcslen(p);
            if (length) {
                WCHAR name[128];
                if (length >= 128)
                    omitted = TRUE;
                else {
                    memcpy(name, p, length * sizeof(WCHAR));
                    name[length] = 0;
                    SCHEMA *schema;
                    DWORD index;
                    if (FAILED(*name == '{' ? E_FAIL : SchemaFindName(name, &schema, &index)))
                        omitted = TRUE;
                    else {
                        DWORD value = flags | (present ? 0x20 : 0);
                        DWORD mask = value | (ascending ? 0x10 : 0);
                        if (!list_add(&b, schema, index, mask, value, width)) {
                            SchemaRelease(schema);
                            for (DWORD i = 0; i < b.count; i++)
                                SchemaRelease(b.schemas[i]);
                            HeapFree(GetProcessHeap(), 0, b.schemas);
                            HeapFree(GetProcessHeap(), 0, b.indices);
                            HeapFree(GetProcessHeap(), 0, b.view);
                            return E_OUTOFMEMORY;
                        }
                    }
                }
            }
            if (!end)
                break;
            p = end + 1;
        }
    }
done:
    {
        HRESULT hr = DescCreateListWithView(b.schemas, b.indices, b.view, b.count, riid, object);
        if (SUCCEEDED(hr) && omitted)
            hr = INPLACE_S_TRUNCATED;
        return hr;
    }
}

/* ------------------------------------------------------------------ the object */

static HRESULT STDMETHODCALLTYPE ps_query_interface(IPropertySystem *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertySystem))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE ps_add_ref(IPropertySystem *This)
{
    return 2;
}
static ULONG STDMETHODCALLTYPE ps_release(IPropertySystem *This)
{
    return 1;
}
static HRESULT STDMETHODCALLTYPE ps_get_description(IPropertySystem *This, REFPROPERTYKEY key, REFIID riid,
                                                    void **object)
{
    *object = nullptr;
    return PSGetPropertyDescription(key, riid, object);
}
static HRESULT STDMETHODCALLTYPE ps_get_by_name(IPropertySystem *This, LPCWSTR name, REFIID riid, void **object)
{
    *object = nullptr;
    if (!name)
        return E_POINTER;
    return PSGetPropertyDescriptionByName(name, riid, object);
}
static HRESULT STDMETHODCALLTYPE ps_list_from_string(IPropertySystem *This, LPCWSTR list, REFIID riid, void **object)
{
    return PSGetPropertyDescriptionListFromString(list, riid, object);
}
static HRESULT STDMETHODCALLTYPE ps_enumerate(IPropertySystem *This, PROPDESC_ENUMFILTER filter, REFIID riid,
                                              void **object)
{
    return PSEnumeratePropertyDescriptions(filter, riid, object);
}
static HRESULT STDMETHODCALLTYPE ps_format(IPropertySystem *This, REFPROPERTYKEY key, REFPROPVARIANT value,
                                           PROPDESC_FORMAT_FLAGS flags, LPWSTR text, DWORD length)
{
    return PSFormatForDisplay(key, value, flags, text, length);
}
static HRESULT STDMETHODCALLTYPE ps_format_alloc(IPropertySystem *This, REFPROPERTYKEY key, REFPROPVARIANT value,
                                                 PROPDESC_FORMAT_FLAGS flags, LPWSTR *text)
{
    return PSFormatForDisplayAlloc(key, value, flags, text);
}
static HRESULT STDMETHODCALLTYPE ps_register(IPropertySystem *This, LPCWSTR path)
{
    return SchemaRegister(path);
}
static HRESULT STDMETHODCALLTYPE ps_unregister(IPropertySystem *This, LPCWSTR path)
{
    return SchemaUnregister(path);
}
static HRESULT STDMETHODCALLTYPE ps_refresh(IPropertySystem *This)
{
    return SchemaRefresh();
}

static const IPropertySystemVtbl property_system_vtbl = {
    ps_query_interface, ps_add_ref, ps_release, ps_get_description, ps_get_by_name, ps_list_from_string, ps_enumerate,
    ps_format, ps_format_alloc, ps_register, ps_unregister, ps_refresh,
};
static IPropertySystem g_property_system = {&property_system_vtbl};

HRESULT WINAPI PSGetPropertySystem(REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    return ps_query_interface(&g_property_system, riid, object);
}

HRESULT WINAPI PSRegisterPropertySchema(LPCWSTR path)
{
    return SchemaRegister(path);
}

HRESULT WINAPI PSUnregisterPropertySchema(LPCWSTR path)
{
    return SchemaUnregister(path);
}

HRESULT WINAPI PSRefreshPropertySchema(void)
{
    return SchemaRefresh();
}
