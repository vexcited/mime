/* property descriptions, the lists of them and the enumerated types, over the schema records. */
#include "propsys_private.h"

/* ------------------------------------------------------------------ strings */

static HRESULT copy_string(LPCWSTR source, LPWSTR *out)
{
    *out = nullptr;
    SIZE_T bytes = (wcslen(source) + 1) * sizeof(WCHAR);
    LPWSTR copy = CoTaskMemAlloc(bytes);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, source, bytes);
    *out = copy;
    return S_OK;
}

HRESULT PsLoadIndirect(LPCWSTR source, LPWSTR *out)
{
    *out = nullptr;
    if (source[0] != '@')
        return copy_string(source, out);
    WCHAR buffer[1024];
    HRESULT hr = SHLoadIndirectString(source, buffer, 1024, nullptr);
    if (FAILED(hr))
        return hr;
    return copy_string(buffer, out);
}

/* the strings of the property system's own resources. */
LPCWSTR PsUiString(UINT id);

static HRESULT ui_string(UINT id, LPWSTR *out)
{
    LPCWSTR text = PsUiString(id);
    if (!text)
        return E_FAIL;
    return copy_string(text, out);
}

/* ------------------------------------------------------------------ one description */

typedef struct {
    IPropertyDescription2 description;
    IPropertyDescriptionAliasInfo alias;
    IPropertyDescriptionSearchInfo search;
    IPropertyDescriptionRelatedPropertyInfo related;
    LONG refs;
    SCHEMA *schema;
    DWORD index;
    /* what a list of descriptions from a string changed in the view of a column. */
    DWORD view_mask, view_value, width;
} DESC;

#define DESC_FROM(iface, member) ((DESC *)((BYTE *)(iface) - offsetof(DESC, member)))
#define RECORD(d) (&(d)->schema->descs[(d)->index])

static HRESULT desc_query(DESC *d, REFIID riid, void **object)
{
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyDescription) ||
        IsEqualIID(riid, &IID_IPropertyDescription2))
        *object = &d->description;
    else if (IsEqualIID(riid, &IID_IPropertyDescriptionAliasInfo))
        *object = &d->alias;
    else if (IsEqualIID(riid, &IID_IPropertyDescriptionSearchInfo))
        *object = &d->search;
    else if (IsEqualIID(riid, &IID_IPropertyDescriptionRelatedPropertyInfo))
        *object = &d->related;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&d->refs);
    return S_OK;
}

static ULONG desc_release(DESC *d)
{
    LONG refs = InterlockedDecrement(&d->refs);
    if (!refs) {
        SchemaRelease(d->schema);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE d_query_interface(IPropertyDescription2 *This, REFIID riid, void **object)
{
    return desc_query(DESC_FROM(This, description), riid, object);
}
static ULONG STDMETHODCALLTYPE d_add_ref(IPropertyDescription2 *This)
{
    return InterlockedIncrement(&DESC_FROM(This, description)->refs);
}
static ULONG STDMETHODCALLTYPE d_release(IPropertyDescription2 *This)
{
    return desc_release(DESC_FROM(This, description));
}

static HRESULT STDMETHODCALLTYPE d_get_key(IPropertyDescription2 *This, PROPERTYKEY *key)
{
    *key = RECORD(DESC_FROM(This, description))->key;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_name(IPropertyDescription2 *This, LPWSTR *name)
{
    DESC *d = DESC_FROM(This, description);
    return copy_string(OFS_STR(d->schema, RECORD(d)->name), name);
}

static HRESULT STDMETHODCALLTYPE d_get_type(IPropertyDescription2 *This, VARTYPE *type)
{
    *type = (VARTYPE)RECORD(DESC_FROM(This, description))->vt;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_display_name(IPropertyDescription2 *This, LPWSTR *name)
{
    DESC *d = DESC_FROM(This, description);
    LPCWSTR label = OFS_STR(d->schema, RECORD(d)->label);
    *name = nullptr;
    if (!label)
        return E_FAIL;
    return PsLoadIndirect(label, name);
}

static HRESULT STDMETHODCALLTYPE d_get_invitation(IPropertyDescription2 *This, LPWSTR *text)
{
    DESC *d = DESC_FROM(This, description);
    LPCWSTR invitation = OFS_STR(d->schema, RECORD(d)->invitation);
    *text = nullptr;
    if (!invitation) {
        HRESULT hr = ui_string(121, text);
        return SUCCEEDED(hr) ? S_FALSE : hr;
    }
    return PsLoadIndirect(invitation, text);
}

static HRESULT STDMETHODCALLTYPE d_get_type_flags(IPropertyDescription2 *This, PROPDESC_TYPE_FLAGS mask,
                                                  PROPDESC_TYPE_FLAGS *flags)
{
    *flags = (PROPDESC_TYPE_FLAGS)(mask & RECORD(DESC_FROM(This, description))->type_flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_view_flags(IPropertyDescription2 *This, PROPDESC_VIEW_FLAGS *flags)
{
    DESC *d = DESC_FROM(This, description);
    *flags = (PROPDESC_VIEW_FLAGS)((RECORD(d)->view_flags & ~d->view_mask) | d->view_value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_width(IPropertyDescription2 *This, UINT *width)
{
    DESC *d = DESC_FROM(This, description);
    *width = d->width ? d->width : RECORD(d)->col_width;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_display_type(IPropertyDescription2 *This, PROPDESC_DISPLAYTYPE *type)
{
    *type = (PROPDESC_DISPLAYTYPE)RECORD(DESC_FROM(This, description))->display_type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_column_state(IPropertyDescription2 *This, SHCOLSTATEF *state)
{
    *state = RECORD(DESC_FROM(This, description))->column_state;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_grouping(IPropertyDescription2 *This, PROPDESC_GROUPING_RANGE *range)
{
    *range = (PROPDESC_GROUPING_RANGE)RECORD(DESC_FROM(This, description))->grouping;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_relative_type(IPropertyDescription2 *This,
                                                     PROPDESC_RELATIVEDESCRIPTION_TYPE *type)
{
    *type = (PROPDESC_RELATIVEDESCRIPTION_TYPE)RECORD(DESC_FROM(This, description))->relative_type;
    return S_OK;
}

static BOOL date_before_now(const PROPVARIANT *value, const PROPVARIANT *now)
{
    return PropVariantCompareEx(value, now, PVCU_DEFAULT, 0) < 0;
}

/* the strings for a smaller, the same and a larger value, and which of them a pair of values takes. */
static HRESULT relative_strings(UINT less, UINT same, UINT greater, const PROPVARIANT *a, const PROPVARIANT *b,
                                LPWSTR *first, LPWSTR *second)
{
    if (a->vt != b->vt)
        return a->vt != VT_NULL && b->vt != VT_NULL ? (HRESULT)0x80030070 : S_OK;
    if (a->vt == VT_NULL)
        return S_OK;
    int order = PropVariantCompareEx(a, b, PVCU_DEFAULT, 0);
    UINT one = same, two = same;
    if (order < 0) {
        one = less;
        two = greater;
    } else if (order > 0) {
        one = greater;
        two = less;
    }
    HRESULT hr = ui_string(one, first);
    if (SUCCEEDED(hr))
        hr = ui_string(two, second);
    if (FAILED(hr) && *first) {
        CoTaskMemFree(*first);
        *first = nullptr;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_get_relative(IPropertyDescription2 *This, REFPROPVARIANT a, REFPROPVARIANT b,
                                                LPWSTR *first, LPWSTR *second)
{
    DESC *d = DESC_FROM(This, description);
    *first = *second = nullptr;
    if (a->vt != b->vt)
        return E_INVALIDARG;
    DWORD type = RECORD(d)->relative_type;
    if (type == PDRDT_DATE) {
        UINT less = 142, greater = 143;
        if ((a->vt == VT_DATE || a->vt == VT_FILETIME)) {
            PROPVARIANT now = {};
            now.vt = a->vt;
            if (a->vt == VT_FILETIME)
                GetSystemTimeAsFileTime(&now.filetime);
            else {
                SYSTEMTIME st;
                GetSystemTime(&st);
                SystemTimeToVariantTime(&st, &now.date);
            }
            if (!date_before_now(a, &now) && !date_before_now(b, &now)) {
                less = 146;
                greater = 147;
            } else if (date_before_now(a, &now) && date_before_now(b, &now)) {
                less = 144;
                greater = 145;
            }
            HRESULT hr = relative_strings(less, 140, greater, a, b, first, second);
            if (SUCCEEDED(hr))
                return hr;
        }
        return relative_strings(141, 140, 141, a, b, first, second);
    }
    static const UINT ids[11][2] = {{141, 141}, {0, 0}, {148, 149}, {150, 151}, {152, 153}, {154, 155},
                                    {156, 157}, {158, 159}, {160, 161}, {162, 163}, {164, 165}};
    if (type > PDRDT_PRIORITY)
        type = PDRDT_GENERAL;
    return relative_strings(ids[type][0], 140, ids[type][1], a, b, first, second);
}

static HRESULT STDMETHODCALLTYPE d_get_sort(IPropertyDescription2 *This, PROPDESC_SORTDESCRIPTION *sort)
{
    *sort = (PROPDESC_SORTDESCRIPTION)RECORD(DESC_FROM(This, description))->sort_desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_sort_label(IPropertyDescription2 *This, BOOL descending, LPWSTR *label)
{
    UINT id;
    switch (RECORD(DESC_FROM(This, description))->sort_desc) {
    case PDSD_A_Z: id = 130; break;
    case PDSD_LOWEST_HIGHEST: id = 136; break;
    case PDSD_SMALLEST_BIGGEST: id = 134; break;
    case PDSD_OLDEST_NEWEST: id = 132; break;
    default: id = 138; break;
    }
    *label = nullptr;
    return ui_string(id + (descending != 0), label);
}

static HRESULT STDMETHODCALLTYPE d_get_aggregation(IPropertyDescription2 *This, PROPDESC_AGGREGATION_TYPE *type)
{
    *type = (PROPDESC_AGGREGATION_TYPE)RECORD(DESC_FROM(This, description))->aggregation;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_get_condition(IPropertyDescription2 *This, PROPDESC_CONDITION_TYPE *type,
                                                 CONDITION_OPERATION *operation)
{
    const PDESC *r = RECORD(DESC_FROM(This, description));
    *type = (PROPDESC_CONDITION_TYPE)r->cond_type;
    *operation = (CONDITION_OPERATION)r->cond_op;
    return S_OK;
}

HRESULT EnumTypeListCreate(SCHEMA *schema, const PDESC *desc, REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d_get_enum_list(IPropertyDescription2 *This, REFIID riid, void **object)
{
    DESC *d = DESC_FROM(This, description);
    return EnumTypeListCreate(d->schema, RECORD(d), riid, object);
}

static HRESULT STDMETHODCALLTYPE d_coerce(IPropertyDescription2 *This, PROPVARIANT *value)
{
    DESC *d = DESC_FROM(This, description);
    return DescCoerce(d->schema, RECORD(d), value);
}

static HRESULT STDMETHODCALLTYPE d_format(IPropertyDescription2 *This, REFPROPVARIANT value,
                                          PROPDESC_FORMAT_FLAGS flags, LPWSTR *text)
{
    DESC *d = DESC_FROM(This, description);
    return DescFormat(d->schema, RECORD(d), value, flags, text);
}

static HRESULT STDMETHODCALLTYPE d_is_canonical(IPropertyDescription2 *This, REFPROPVARIANT value)
{
    DESC *d = DESC_FROM(This, description);
    PROPVARIANT copy = {};
    HRESULT hr = PropVariantCopy(&copy, value);
    if (FAILED(hr))
        return hr;
    hr = DescCoerce(d->schema, RECORD(d), &copy);
    if (SUCCEEDED(hr))
        hr = copy.vt == value->vt && !PropVariantCompareEx(&copy, value, PVCU_DEFAULT, PVCF_USESTRCMP) ? S_OK : S_FALSE;
    PropVariantClear(&copy);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d_get_image(IPropertyDescription2 *This, REFPROPVARIANT value, LPWSTR *image)
{
    DESC *d = DESC_FROM(This, description);
    return DescImageReference(d->schema, RECORD(d), value, image);
}

static const IPropertyDescription2Vtbl description_vtbl = {
    d_query_interface, d_add_ref, d_release, d_get_key, d_get_name, d_get_type, d_get_display_name, d_get_invitation,
    d_get_type_flags, d_get_view_flags, d_get_width, d_get_display_type, d_get_column_state, d_get_grouping,
    d_get_relative_type, d_get_relative, d_get_sort, d_get_sort_label, d_get_aggregation, d_get_condition,
    d_get_enum_list, d_coerce, d_format, d_is_canonical, d_get_image,
};

/* alias info. */

static HRESULT STDMETHODCALLTYPE a_query_interface(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object)
{
    return desc_query(DESC_FROM(This, alias), riid, object);
}
static ULONG STDMETHODCALLTYPE a_add_ref(IPropertyDescriptionAliasInfo *This)
{
    return InterlockedIncrement(&DESC_FROM(This, alias)->refs);
}
static ULONG STDMETHODCALLTYPE a_release(IPropertyDescriptionAliasInfo *This)
{
    return desc_release(DESC_FROM(This, alias));
}

static HRESULT alias_description(LPCWSTR name, REFIID riid, void **object)
{
    *object = nullptr;
    if (!name)
        return E_INVALIDARG;
    return PSGetPropertyDescriptionByName(name, riid, object);
}

static HRESULT STDMETHODCALLTYPE a_sort_by(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object)
{
    DESC *d = DESC_FROM(This, alias);
    LPCWSTR alias = OFS_STR(d->schema, RECORD(d)->sort_alias);
    if (!alias)
        return d->description.lpVtbl->QueryInterface(&d->description, riid, object);
    return alias_description(alias, riid, object);
}

static HRESULT STDMETHODCALLTYPE a_additional(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object)
{
    DESC *d = DESC_FROM(This, alias);
    LPCWSTR more = OFS_STR(d->schema, RECORD(d)->more_aliases);
    *object = nullptr;
    if (!more)
        return E_INVALIDARG;
    return PSGetPropertyDescriptionListFromString(more, riid, object);
}

static const IPropertyDescriptionAliasInfoVtbl alias_vtbl = {a_query_interface, a_add_ref, a_release, a_sort_by,
                                                             a_additional};

/* search info. */

static HRESULT STDMETHODCALLTYPE s_query_interface(IPropertyDescriptionSearchInfo *This, REFIID riid, void **object)
{
    return desc_query(DESC_FROM(This, search), riid, object);
}
static ULONG STDMETHODCALLTYPE s_add_ref(IPropertyDescriptionSearchInfo *This)
{
    return InterlockedIncrement(&DESC_FROM(This, search)->refs);
}
static ULONG STDMETHODCALLTYPE s_release(IPropertyDescriptionSearchInfo *This)
{
    return desc_release(DESC_FROM(This, search));
}
static HRESULT STDMETHODCALLTYPE s_flags(IPropertyDescriptionSearchInfo *This, PROPDESC_SEARCHINFO_FLAGS *flags)
{
    *flags = (PROPDESC_SEARCHINFO_FLAGS)RECORD(DESC_FROM(This, search))->search_flags;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_index_type(IPropertyDescriptionSearchInfo *This, PROPDESC_COLUMNINDEX_TYPE *type)
{
    *type = (PROPDESC_COLUMNINDEX_TYPE)RECORD(DESC_FROM(This, search))->col_index;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_projection(IPropertyDescriptionSearchInfo *This, LPWSTR *text)
{
    DESC *d = DESC_FROM(This, search);
    *text = nullptr;
    if (RECORD(d)->search_flags & (PDSIF_ININVERTEDINDEX | PDSIF_ISCOLUMN))
        return copy_string(OFS_STR(d->schema, RECORD(d)->name), text);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_max_size(IPropertyDescriptionSearchInfo *This, UINT *size)
{
    *size = RECORD(DESC_FROM(This, search))->max_size;
    return S_OK;
}

static const IPropertyDescriptionSearchInfoVtbl search_vtbl = {s_query_interface, s_add_ref, s_release, s_flags,
                                                               s_index_type, s_projection, s_max_size};

/* related property info. */

static HRESULT STDMETHODCALLTYPE r_query_interface(IPropertyDescriptionRelatedPropertyInfo *This, REFIID riid,
                                                   void **object)
{
    return desc_query(DESC_FROM(This, related), riid, object);
}
static ULONG STDMETHODCALLTYPE r_add_ref(IPropertyDescriptionRelatedPropertyInfo *This)
{
    return InterlockedIncrement(&DESC_FROM(This, related)->refs);
}
static ULONG STDMETHODCALLTYPE r_release(IPropertyDescriptionRelatedPropertyInfo *This)
{
    return desc_release(DESC_FROM(This, related));
}
static HRESULT STDMETHODCALLTYPE r_get(IPropertyDescriptionRelatedPropertyInfo *This, LPCWSTR relationship,
                                       REFIID riid, void **object)
{
    DESC *d = DESC_FROM(This, related);
    const PDESC *r = RECORD(d);
    *object = nullptr;
    if (!relationship)
        return E_INVALIDARG;
    const RELPROP *list = (const RELPROP *)(d->schema->base + r->rels);
    for (DWORD i = 0; r->rels && i < r->rel_count; i++)
        if (!_wcsicmp(OFS_STR(d->schema, list[i].relationship), relationship))
            return PSGetPropertyDescriptionByName(OFS_STR(d->schema, list[i].property), riid, object);
    return TYPE_E_ELEMENTNOTFOUND;
}

static const IPropertyDescriptionRelatedPropertyInfoVtbl related_vtbl = {r_query_interface, r_add_ref, r_release,
                                                                         r_get};

HRESULT DescCreateWithView(SCHEMA *schema, DWORD index, DWORD mask, DWORD value, DWORD width, REFIID riid,
                           void **object)
{
    DESC *d = PsAllocZero(sizeof(*d));
    *object = nullptr;
    if (!d)
        return E_OUTOFMEMORY;
    d->description.lpVtbl = &description_vtbl;
    d->alias.lpVtbl = &alias_vtbl;
    d->search.lpVtbl = &search_vtbl;
    d->related.lpVtbl = &related_vtbl;
    d->refs = 1;
    d->schema = schema;
    d->index = index;
    d->view_mask = mask;
    d->view_value = value;
    d->width = width;
    SchemaAddRef(schema);
    HRESULT hr = desc_query(d, riid, object);
    desc_release(d);
    return hr;
}

HRESULT DescCreate(SCHEMA *schema, DWORD index, REFIID riid, void **object)
{
    return DescCreateWithView(schema, index, 0, 0, 0, riid, object);
}

/* a description of a key written as a string, which has no schema behind it. */
HRESULT DescCreateDummy(LPCWSTR name, REFIID riid, void **object)
{
    *object = nullptr;
    PROPERTYKEY key;
    HRESULT hr = PSPropertyKeyFromString(name, &key);
    if (FAILED(hr))
        return hr;
    SIZE_T length = wcslen(name);
    if (length > 49)
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    DWORD desc_at = (sizeof(CACHE_HEADER) + 7) & ~7u;
    DWORD name_at = desc_at + sizeof(PDESC);
    DWORD size = name_at + (DWORD)(length + 1) * sizeof(WCHAR);
    BYTE *image = PsAllocZero(size);
    if (!image)
        return E_OUTOFMEMORY;
    CACHE_HEADER *header = (CACHE_HEADER *)image;
    header->magic = CACHE_MAGIC;
    header->version = CACHE_VERSION;
    header->size = size;
    header->count = 1;
    header->descs = desc_at;
    PDESC *desc = (PDESC *)(image + desc_at);
    desc->name = name_at;
    desc->key = key;
    desc->vt = VT_EMPTY;
    desc->column_state = 1;
    desc->col_width = 20;
    desc->max_size = 512;
    desc->display_type = PDDT_STRING;
    desc->cond_op = COP_EQUAL;
    memcpy(image + name_at, name, (length + 1) * sizeof(WCHAR));
    SCHEMA *schema = SchemaFromImage(image);
    if (!schema) {
        HeapFree(GetProcessHeap(), 0, image);
        return E_OUTOFMEMORY;
    }
    hr = DescCreate(schema, 0, riid, object);
    SchemaRelease(schema);
    return hr;
}

/* ------------------------------------------------------------------ the list of descriptions */

typedef struct {
    IPropertyDescriptionList list;
    LONG refs;
    SCHEMA **schemas;
    DWORD *indices;
    DWORD count;
    /* three numbers per description when the list came from a string. */
    DWORD *view;
} LIST;

static HRESULT STDMETHODCALLTYPE l_query_interface(IPropertyDescriptionList *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyDescriptionList))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE l_add_ref(IPropertyDescriptionList *This)
{
    return InterlockedIncrement(&((LIST *)This)->refs);
}
static ULONG STDMETHODCALLTYPE l_release(IPropertyDescriptionList *This)
{
    LIST *l = (LIST *)This;
    LONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        for (DWORD i = 0; i < l->count; i++)
            SchemaRelease(l->schemas[i]);
        HeapFree(GetProcessHeap(), 0, l->schemas);
        HeapFree(GetProcessHeap(), 0, l->indices);
        HeapFree(GetProcessHeap(), 0, l->view);
        HeapFree(GetProcessHeap(), 0, l);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE l_count(IPropertyDescriptionList *This, UINT *count)
{
    *count = ((LIST *)This)->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE l_at(IPropertyDescriptionList *This, UINT index, REFIID riid, void **object)
{
    LIST *l = (LIST *)This;
    *object = nullptr;
    if (index >= l->count)
        return E_INVALIDARG;
    if (l->view)
        return DescCreateWithView(l->schemas[index], l->indices[index], l->view[index * 3], l->view[index * 3 + 1],
                                  l->view[index * 3 + 2], riid, object);
    return DescCreate(l->schemas[index], l->indices[index], riid, object);
}

static const IPropertyDescriptionListVtbl list_vtbl = {l_query_interface, l_add_ref, l_release, l_count, l_at};

/* the list takes the references to the schemas and the arrays. */
HRESULT DescCreateListWithView(SCHEMA **schemas, DWORD *indices, DWORD *view, DWORD count, REFIID riid, void **object)
{
    LIST *l = PsAllocZero(sizeof(*l));
    *object = nullptr;
    if (!l) {
        for (DWORD i = 0; i < count; i++)
            SchemaRelease(schemas[i]);
        HeapFree(GetProcessHeap(), 0, schemas);
        HeapFree(GetProcessHeap(), 0, indices);
        HeapFree(GetProcessHeap(), 0, view);
        return E_OUTOFMEMORY;
    }
    l->view = view;
    l->list.lpVtbl = &list_vtbl;
    l->refs = 1;
    l->schemas = schemas;
    l->indices = indices;
    l->count = count;
    HRESULT hr = l->list.lpVtbl->QueryInterface(&l->list, riid, object);
    l->list.lpVtbl->Release(&l->list);
    return hr;
}

HRESULT DescCreateList(SCHEMA **schemas, DWORD *indices, DWORD count, REFIID riid, void **object)
{
    return DescCreateListWithView(schemas, indices, nullptr, count, riid, object);
}

/* ------------------------------------------------------------------ enumerated types */

typedef struct {
    IPropertyEnumType2 type;
    LONG refs;
    SCHEMA *schema;
    const ENUMINFO *info;
    VARTYPE vt;
} ENUMTYPE;

static HRESULT STDMETHODCALLTYPE e_query_interface(IPropertyEnumType2 *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyEnumType) &&
        !IsEqualIID(riid, &IID_IPropertyEnumType2))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE e_add_ref(IPropertyEnumType2 *This)
{
    return InterlockedIncrement(&((ENUMTYPE *)This)->refs);
}
static ULONG STDMETHODCALLTYPE e_release(IPropertyEnumType2 *This)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    LONG refs = InterlockedDecrement(&e->refs);
    if (!refs) {
        SchemaRelease(e->schema);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return refs;
}

static HRESULT enum_value(ENUMTYPE *e, DWORD offset, PROPVARIANT *value)
{
    LPCWSTR text = OFS_STR(e->schema, offset);
    memset(value, 0, sizeof(*value));
    if (!text || !*text)
        return S_OK;
    PROPVARIANT source = {};
    source.vt = VT_LPWSTR;
    source.pwszVal = (LPWSTR)text;
    VARTYPE vt = e->vt & 0xEFFF;
    if (vt == VT_LPWSTR || vt == VT_EMPTY || vt == VT_NULL)
        return PropVariantCopy(value, &source);
    return PropVariantChangeType(value, &source, 0, vt);
}

static HRESULT STDMETHODCALLTYPE e_get_type(IPropertyEnumType2 *This, PROPENUMTYPE *type)
{
    *type = (PROPENUMTYPE)((ENUMTYPE *)This)->info->type;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE e_get_value(IPropertyEnumType2 *This, PROPVARIANT *value)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    if (e->info->type == PET_RANGEDVALUE || e->info->type == PET_ENDRANGE)
        return enum_value(e, e->info->min, value);
    return enum_value(e, e->info->value, value);
}
static HRESULT STDMETHODCALLTYPE e_get_min(IPropertyEnumType2 *This, PROPVARIANT *value)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    return enum_value(e, e->info->min, value);
}
static HRESULT STDMETHODCALLTYPE e_get_set(IPropertyEnumType2 *This, PROPVARIANT *value)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    return enum_value(e, e->info->set, value);
}
static HRESULT STDMETHODCALLTYPE e_get_text(IPropertyEnumType2 *This, LPWSTR *text)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    LPCWSTR source = OFS_STR(e->schema, e->info->text);
    *text = nullptr;
    return PsLoadIndirect(source ? source : L"", text);
}
static HRESULT STDMETHODCALLTYPE e_get_image(IPropertyEnumType2 *This, LPWSTR *image)
{
    ENUMTYPE *e = (ENUMTYPE *)This;
    LPCWSTR source = OFS_STR(e->schema, e->info->image);
    *image = nullptr;
    return copy_string(source ? source : L"", image);
}

static const IPropertyEnumType2Vtbl enum_type_vtbl = {e_query_interface, e_add_ref, e_release, e_get_type, e_get_value,
                                                      e_get_min, e_get_set, e_get_text, e_get_image};

typedef struct {
    IPropertyEnumTypeList list;
    LONG refs;
    SCHEMA *schema;
    const PDESC *desc;
    const ENUMINFO *infos;
    DWORD count;
    DWORD default_text;
    BOOL owned;
} ENUMLIST;

static HRESULT STDMETHODCALLTYPE el_query_interface(IPropertyEnumTypeList *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IPropertyEnumTypeList))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE el_add_ref(IPropertyEnumTypeList *This)
{
    return InterlockedIncrement(&((ENUMLIST *)This)->refs);
}
static ULONG STDMETHODCALLTYPE el_release(IPropertyEnumTypeList *This)
{
    ENUMLIST *l = (ENUMLIST *)This;
    LONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        SchemaRelease(l->schema);
        HeapFree(GetProcessHeap(), 0, l);
    }
    return refs;
}
static HRESULT STDMETHODCALLTYPE el_count(IPropertyEnumTypeList *This, UINT *count)
{
    *count = ((ENUMLIST *)This)->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE el_at(IPropertyEnumTypeList *This, UINT index, REFIID riid, void **object)
{
    ENUMLIST *l = (ENUMLIST *)This;
    *object = nullptr;
    if (index >= l->count)
        return E_INVALIDARG;
    ENUMTYPE *e = PsAllocZero(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->type.lpVtbl = &enum_type_vtbl;
    e->refs = 1;
    e->schema = l->schema;
    SchemaAddRef(l->schema);
    e->info = &l->infos[index];
    e->vt = (VARTYPE)l->desc->vt;
    HRESULT hr = e->type.lpVtbl->QueryInterface(&e->type, riid, object);
    e->type.lpVtbl->Release(&e->type);
    return hr;
}
static HRESULT STDMETHODCALLTYPE el_condition_at(IPropertyEnumTypeList *This, UINT index, REFIID riid, void **object)
{
    *object = nullptr;
    return E_NOTIMPL;
}

/* the first entry a value matches, a range by its minimum up to the next entry. */
static HRESULT STDMETHODCALLTYPE el_find(IPropertyEnumTypeList *This, REFPROPVARIANT value, UINT *index)
{
    ENUMLIST *l = (ENUMLIST *)This;
    for (DWORD i = 0; i < l->count; i++) {
        ENUMTYPE e = {};
        e.schema = l->schema;
        e.info = &l->infos[i];
        e.vt = (VARTYPE)l->desc->vt;
        PROPVARIANT v = {};
        BOOL range = e.info->type == PET_RANGEDVALUE || e.info->type == PET_ENDRANGE;
        if (FAILED(enum_value(&e, range ? e.info->min : e.info->value, &v)))
            continue;
        BOOL match;
        if (!range)
            match = !PropVariantCompareEx(value, &v, PVCU_DEFAULT, 0);
        else {
            PROPVARIANT set = {};
            match = PropVariantCompareEx(value, &v, PVCU_DEFAULT, 0) >= 0;
            if (match && e.info->type == PET_RANGEDVALUE && SUCCEEDED(enum_value(&e, e.info->set, &set)) &&
                set.vt != VT_EMPTY)
                match = PropVariantCompareEx(value, &set, PVCU_DEFAULT, 0) < 0;
            PropVariantClear(&set);
        }
        PropVariantClear(&v);
        if (match) {
            *index = i;
            return S_OK;
        }
    }
    *index = (UINT)-1;
    return S_FALSE;
}

static const IPropertyEnumTypeListVtbl enum_list_vtbl = {el_query_interface, el_add_ref, el_release, el_count, el_at,
                                                         el_condition_at, el_find};

HRESULT EnumTypeListCreate(SCHEMA *schema, const PDESC *desc, REFIID riid, void **object)
{
    *object = nullptr;
    if (!desc->enum_count)
        return E_NOTIMPL;
    ENUMLIST *l = PsAllocZero(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->list.lpVtbl = &enum_list_vtbl;
    l->refs = 1;
    l->schema = schema;
    SchemaAddRef(schema);
    l->desc = desc;
    l->infos = (const ENUMINFO *)(schema->base + desc->enums);
    l->count = desc->enum_count;
    HRESULT hr = l->list.lpVtbl->QueryInterface(&l->list, riid, object);
    l->list.lpVtbl->Release(&l->list);
    return hr;
}
