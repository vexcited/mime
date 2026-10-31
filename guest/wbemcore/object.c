/* objects: classes and instances with their properties, qualifiers and methods. */
#include "wbemcore.h"

/* ------------------------------------------------------------------ qualifier lists */

void wq_init(WQuals *quals)
{
    memset(quals, 0, sizeof(*quals));
}

void wq_free(WQuals *quals)
{
    for (UINT i = 0; i < quals->count; i++) {
        wb_free(quals->items[i].name);
        VariantClear(&quals->items[i].value);
    }
    wb_free(quals->items);
    wq_init(quals);
}

void wq_copy(WQuals *dst, const WQuals *src)
{
    wq_init(dst);
    if (!src->count)
        return;
    dst->items = wb_alloc(src->count * sizeof(WQual));
    dst->capacity = src->count;
    for (UINT i = 0; i < src->count; i++) {
        dst->items[i].name = wb_wcsdup(src->items[i].name);
        wv_copy(&dst->items[i].value, &src->items[i].value);
        dst->items[i].flavor = src->items[i].flavor;
    }
    dst->count = src->count;
}

WQual *wq_find(const WQuals *quals, const WCHAR *name)
{
    for (UINT i = 0; i < quals->count; i++)
        if (wb_eq(quals->items[i].name, name))
            return &quals->items[i];
    return nullptr;
}

void wq_set(WQuals *quals, const WCHAR *name, const VARIANT *value, LONG flavor)
{
    WQual *q = wq_find(quals, name);
    if (!q) {
        WB_GROW(quals->items, quals->count, quals->capacity);
        q = &quals->items[quals->count++];
        q->name = wb_wcsdup(name);
        wv_init(&q->value);
    }
    VariantClear(&q->value);
    wv_copy(&q->value, value);
    q->flavor = flavor;
}

BOOL wq_remove(WQuals *quals, const WCHAR *name)
{
    for (UINT i = 0; i < quals->count; i++) {
        if (wb_eq(quals->items[i].name, name)) {
            wb_free(quals->items[i].name);
            VariantClear(&quals->items[i].value);
            memmove(&quals->items[i], &quals->items[i + 1], (quals->count - i - 1) * sizeof(WQual));
            quals->count--;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL wq_true(const WQuals *quals, const WCHAR *name)
{
    WQual *q = wq_find(quals, name);
    return q && q->value.vt == VT_BOOL && q->value.boolVal;
}

/* ------------------------------------------------------------------ objects */

WObj *wo_new(BOOL is_class, const WCHAR *name, const WCHAR *super)
{
    WObj *o = wb_alloc(sizeof(WObj));
    o->is_class = is_class;
    o->name = wb_wcsdup(name);
    o->super = wb_wcsdup(super);
    return o;
}

static void prop_free(WProp *p)
{
    wb_free(p->name);
    wb_free(p->origin);
    VariantClear(&p->value);
    wq_free(&p->quals);
}

static void meth_free(WMeth *m)
{
    wb_free(m->name);
    wb_free(m->origin);
    wo_free(m->in);
    wo_free(m->out);
    wq_free(&m->quals);
}

void wo_free(WObj *o)
{
    if (!o)
        return;
    wb_free(o->name);
    wb_free(o->super);
    for (UINT i = 0; i < o->derivation_count; i++)
        wb_free(o->derivation[i]);
    wb_free(o->derivation);
    wb_free(o->server);
    wb_free(o->ns);
    wq_free(&o->quals);
    for (UINT i = 0; i < o->prop_count; i++)
        prop_free(&o->props[i]);
    wb_free(o->props);
    for (UINT i = 0; i < o->meth_count; i++)
        meth_free(&o->meths[i]);
    wb_free(o->meths);
    wb_free(o);
}

WObj *wo_clone(const WObj *src)
{
    if (!src)
        return nullptr;
    WObj *o = wb_alloc(sizeof(WObj));
    o->is_class = src->is_class;
    o->builtin = src->builtin;
    o->name = wb_wcsdup(src->name);
    o->super = wb_wcsdup(src->super);
    o->server = wb_wcsdup(src->server);
    o->ns = wb_wcsdup(src->ns);
    if (src->derivation_count) {
        o->derivation = wb_alloc(src->derivation_count * sizeof(WCHAR *));
        for (UINT i = 0; i < src->derivation_count; i++)
            o->derivation[i] = wb_wcsdup(src->derivation[i]);
        o->derivation_count = src->derivation_count;
    }
    wq_copy(&o->quals, &src->quals);
    if (src->prop_count) {
        o->props = wb_alloc(src->prop_count * sizeof(WProp));
        o->prop_capacity = src->prop_count;
        for (UINT i = 0; i < src->prop_count; i++) {
            const WProp *p = &src->props[i];
            WProp *d = &o->props[i];
            d->name = wb_wcsdup(p->name);
            d->type = p->type;
            wv_copy(&d->value, &p->value);
            wq_copy(&d->quals, &p->quals);
            d->origin = wb_wcsdup(p->origin);
            d->local = p->local;
        }
        o->prop_count = src->prop_count;
    }
    if (src->meth_count) {
        o->meths = wb_alloc(src->meth_count * sizeof(WMeth));
        o->meth_capacity = src->meth_count;
        for (UINT i = 0; i < src->meth_count; i++) {
            const WMeth *m = &src->meths[i];
            WMeth *d = &o->meths[i];
            d->name = wb_wcsdup(m->name);
            d->in = wo_clone(m->in);
            d->out = wo_clone(m->out);
            wq_copy(&d->quals, &m->quals);
            d->origin = wb_wcsdup(m->origin);
            d->local = m->local;
        }
        o->meth_count = src->meth_count;
    }
    return o;
}

WProp *wo_prop(const WObj *o, const WCHAR *name)
{
    for (UINT i = 0; i < o->prop_count; i++)
        if (wb_eq(o->props[i].name, name))
            return &o->props[i];
    return nullptr;
}

WMeth *wo_meth(const WObj *o, const WCHAR *name)
{
    for (UINT i = 0; i < o->meth_count; i++)
        if (wb_eq(o->meths[i].name, name))
            return &o->meths[i];
    return nullptr;
}

WProp *wo_add_prop(WObj *o, const WCHAR *name, CIMTYPE type, const VARIANT *value, const WCHAR *origin)
{
    WProp *p = wo_prop(o, name);
    if (!p) {
        WB_GROW(o->props, o->prop_count, o->prop_capacity);
        p = &o->props[o->prop_count++];
        p->name = wb_wcsdup(name);
        wv_init(&p->value);
    }
    p->type = type;
    VariantClear(&p->value);
    if (value)
        wv_copy(&p->value, value);
    else
        wv_init(&p->value);
    wb_free(p->origin);
    p->origin = wb_wcsdup(origin ? origin : o->name);
    p->local = TRUE;
    return p;
}

WMeth *wo_add_meth(WObj *o, const WCHAR *name, WObj *in, WObj *out, const WCHAR *origin)
{
    WMeth *m = wo_meth(o, name);
    if (!m) {
        WB_GROW(o->meths, o->meth_count, o->meth_capacity);
        m = &o->meths[o->meth_count++];
        m->name = wb_wcsdup(name);
    } else {
        wo_free(m->in);
        wo_free(m->out);
    }
    m->in = in;
    m->out = out;
    wb_free(m->origin);
    m->origin = wb_wcsdup(origin ? origin : o->name);
    m->local = TRUE;
    return m;
}

BOOL wo_remove_prop(WObj *o, const WCHAR *name)
{
    for (UINT i = 0; i < o->prop_count; i++) {
        if (wb_eq(o->props[i].name, name)) {
            prop_free(&o->props[i]);
            memmove(&o->props[i], &o->props[i + 1], (o->prop_count - i - 1) * sizeof(WProp));
            o->prop_count--;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL wo_remove_meth(WObj *o, const WCHAR *name)
{
    for (UINT i = 0; i < o->meth_count; i++) {
        if (wb_eq(o->meths[i].name, name)) {
            meth_free(&o->meths[i]);
            memmove(&o->meths[i], &o->meths[i + 1], (o->meth_count - i - 1) * sizeof(WMeth));
            o->meth_count--;
            return TRUE;
        }
    }
    return FALSE;
}

/* the qualifiers of a list that carry on to the next level, with the flavor they keep. */
static void propagate(WQuals *dst, const WQuals *src, LONG bit)
{
    wq_init(dst);
    for (UINT i = 0; i < src->count; i++) {
        const WQual *q = &src->items[i];
        if (!(q->flavor & bit))
            continue;
        wq_set(dst, q->name, &q->value, (q->flavor & ~WBEM_FLAVOR_MASK_ORIGIN) | WBEM_FLAVOR_ORIGIN_PROPAGATED);
    }
}

WObj *wo_spawn(const WObj *c)
{
    WObj *o = wo_new(FALSE, c->name, nullptr);
    o->server = wb_wcsdup(c->server);
    o->ns = wb_wcsdup(c->ns);
    o->super = wb_wcsdup(c->super);
    if (c->derivation_count) {
        o->derivation = wb_alloc(c->derivation_count * sizeof(WCHAR *));
        for (UINT i = 0; i < c->derivation_count; i++)
            o->derivation[i] = wb_wcsdup(c->derivation[i]);
        o->derivation_count = c->derivation_count;
    }
    propagate(&o->quals, &c->quals, WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE);
    for (UINT i = 0; i < c->prop_count; i++) {
        const WProp *p = &c->props[i];
        WProp *d;
        WB_GROW(o->props, o->prop_count, o->prop_capacity);
        d = &o->props[o->prop_count++];
        d->name = wb_wcsdup(p->name);
        d->type = p->type;
        wv_copy(&d->value, &p->value);
        propagate(&d->quals, &p->quals, WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE);
        d->origin = wb_wcsdup(p->origin);
        d->local = FALSE;
    }
    return o;
}

WObj *wo_derive(const WObj *parent, const WCHAR *name)
{
    WObj *o = wo_new(TRUE, name, parent ? parent->name : nullptr);
    if (!parent)
        return o;
    o->derivation = wb_alloc((parent->derivation_count + 1) * sizeof(WCHAR *));
    o->derivation[0] = wb_wcsdup(parent->name);
    for (UINT i = 0; i < parent->derivation_count; i++)
        o->derivation[i + 1] = wb_wcsdup(parent->derivation[i]);
    o->derivation_count = parent->derivation_count + 1;
    propagate(&o->quals, &parent->quals, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
    for (UINT i = 0; i < parent->prop_count; i++) {
        const WProp *p = &parent->props[i];
        WProp *d;
        WB_GROW(o->props, o->prop_count, o->prop_capacity);
        d = &o->props[o->prop_count++];
        d->name = wb_wcsdup(p->name);
        d->type = p->type;
        wv_copy(&d->value, &p->value);
        propagate(&d->quals, &p->quals, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
        d->origin = wb_wcsdup(p->origin);
        d->local = FALSE;
    }
    for (UINT i = 0; i < parent->meth_count; i++) {
        const WMeth *m = &parent->meths[i];
        WMeth *d;
        WB_GROW(o->meths, o->meth_count, o->meth_capacity);
        d = &o->meths[o->meth_count++];
        d->name = wb_wcsdup(m->name);
        d->in = wo_clone(m->in);
        d->out = wo_clone(m->out);
        propagate(&d->quals, &m->quals, WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
        d->origin = wb_wcsdup(m->origin);
        d->local = FALSE;
    }
    o->server = wb_wcsdup(parent->server);
    o->ns = wb_wcsdup(parent->ns);
    return o;
}

BOOL wo_inherits(const WObj *o, const WCHAR *ancestor)
{
    if (wb_eq(o->name, ancestor))
        return TRUE;
    for (UINT i = 0; i < o->derivation_count; i++)
        if (wb_eq(o->derivation[i], ancestor))
            return TRUE;
    return FALSE;
}

const WCHAR *wo_dynasty(const WObj *o)
{
    return o->derivation_count ? o->derivation[o->derivation_count - 1] : o->name;
}

/* ------------------------------------------------------------------ system properties */

static const WCHAR *const SYSTEM_NAMES[] = {L"__GENUS",   L"__CLASS",          L"__SUPERCLASS", L"__DYNASTY",  L"__RELPATH",
                                            L"__PROPERTY_COUNT", L"__DERIVATION", L"__SERVER",     L"__NAMESPACE", L"__PATH"};

UINT wo_system_count(void)
{
    return RTL_NUMBER_OF(SYSTEM_NAMES);
}

const WCHAR *wo_system_name(UINT index)
{
    return SYSTEM_NAMES[index];
}

static void set_string(VARIANT *v, const WCHAR *text)
{
    VariantClear(v);
    if (text) {
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(text);
    } else {
        v->vt = VT_NULL;
    }
}

static const WCHAR *local_server(void)
{
    static WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    if (!name[0]) {
        DWORD size = RTL_NUMBER_OF(name);
        if (!GetComputerNameW(name, &size))
            wcscpy(name, L"MIME");
    }
    return name;
}

BOOL wo_system_prop(const WObj *o, const WCHAR *name, VARIANT *value, CIMTYPE *type)
{
    CIMTYPE t = CIM_STRING;
    VariantClear(value);
    wv_init(value);
    if (wb_eq(name, L"__GENUS")) {
        t = CIM_SINT32;
        value->vt = VT_I4;
        value->lVal = o->is_class ? WBEM_GENUS_CLASS : WBEM_GENUS_INSTANCE;
    } else if (wb_eq(name, L"__CLASS")) {
        set_string(value, o->name);
    } else if (wb_eq(name, L"__SUPERCLASS")) {
        set_string(value, o->super);
    } else if (wb_eq(name, L"__DYNASTY")) {
        set_string(value, wo_dynasty(o));
    } else if (wb_eq(name, L"__RELPATH")) {
        WCHAR *text = wo_relpath(o);
        set_string(value, text);
        wb_free(text);
    } else if (wb_eq(name, L"__PROPERTY_COUNT")) {
        t = CIM_SINT32;
        value->vt = VT_I4;
        value->lVal = (LONG)o->prop_count;
    } else if (wb_eq(name, L"__DERIVATION")) {
        t = CIM_STRING | CIM_FLAG_ARRAY;
        SAFEARRAY *array = SafeArrayCreateVector(VT_BSTR, 0, o->derivation_count);
        for (UINT i = 0; array && i < o->derivation_count; i++) {
            LONG index = (LONG)i;
            BSTR s = SysAllocString(o->derivation[i]);
            SafeArrayPutElement(array, &index, s);
            SysFreeString(s);
        }
        if (array) {
            value->vt = VT_BSTR | VT_ARRAY;
            value->parray = array;
        }
    } else if (wb_eq(name, L"__SERVER")) {
        set_string(value, o->server ? o->server : local_server());
    } else if (wb_eq(name, L"__NAMESPACE")) {
        set_string(value, o->ns);
    } else if (wb_eq(name, L"__PATH")) {
        WCHAR *text = wo_path(o);
        set_string(value, text);
        wb_free(text);
    } else {
        return FALSE;
    }
    if (type)
        *type = t;
    return TRUE;
}

/* ------------------------------------------------------------------ paths of objects */

UINT wo_keys(const WObj *o, WProp **keys, UINT limit)
{
    UINT n = 0;
    for (UINT i = 0; i < o->prop_count; i++) {
        if (wq_true(&o->props[i].quals, L"key")) {
            if (n < limit)
                keys[n] = &o->props[i];
            n++;
        }
    }
    return n;
}

WCHAR *wo_relpath(const WObj *o)
{
    if (o->is_class)
        return wb_wcsdup(o->name);
    WProp *keys[64];
    UINT n = wo_keys(o, keys, RTL_NUMBER_OF(keys));
    if (n > RTL_NUMBER_OF(keys))
        n = RTL_NUMBER_OF(keys);
    if (!n) {
        if (wq_true(&o->quals, L"singleton"))
            return wb_format(L"%s=@", o->name);
        return nullptr;
    }
    for (UINT i = 1; i < n; i++) {
        WProp *k = keys[i];
        UINT j = i;
        while (j > 0 && _wcsicmp(keys[j - 1]->name, k->name) > 0) {
            keys[j] = keys[j - 1];
            j--;
        }
        keys[j] = k;
    }
    WBuf buf = {0};
    wb_buf_add(&buf, o->name);
    for (UINT i = 0; i < n; i++) {
        if (wv_is_null(&keys[i]->value)) {
            wb_free(buf.text);
            return nullptr;
        }
        WCHAR *value = wv_text(&keys[i]->value, keys[i]->type);
        wb_buf_addc(&buf, i ? ',' : '.');
        wb_buf_add(&buf, keys[i]->name);
        wb_buf_addc(&buf, '=');
        wb_buf_add(&buf, value);
        wb_free(value);
    }
    return wb_buf_take(&buf);
}

WCHAR *wo_path(const WObj *o)
{
    WCHAR *rel = wo_relpath(o);
    if (!rel)
        return nullptr;
    if (!o->ns) {
        return rel;
    }
    WCHAR *path = wb_format(L"\\\\%s\\%s:%s", o->server ? o->server : local_server(), o->ns, rel);
    wb_free(rel);
    return path;
}

/* ------------------------------------------------------------------ comparison */

static BOOL variant_equal(const VARIANT *a, const VARIANT *b, CIMTYPE type)
{
    if (wv_is_null(a) || wv_is_null(b))
        return wv_is_null(a) && wv_is_null(b);
    WCHAR *x = wv_text(a, type), *y = wv_text(b, type);
    BOOL same = !wcscmp(x, y);
    wb_free(x);
    wb_free(y);
    return same;
}

static BOOL quals_equal(const WQuals *a, const WQuals *b)
{
    if (a->count != b->count)
        return FALSE;
    for (UINT i = 0; i < a->count; i++) {
        WQual *q = wq_find(b, a->items[i].name);
        if (!q || !variant_equal(&a->items[i].value, &q->value, a->items[i].value.vt == VT_BOOL ? CIM_BOOLEAN : CIM_STRING))
            return FALSE;
    }
    return TRUE;
}

BOOL wo_equal(const WObj *a, const WObj *b, LONG flags)
{
    if (a->is_class != b->is_class)
        return FALSE;
    if (!(flags & WBEM_FLAG_IGNORE_CLASS) && !wb_eq(a->name, b->name))
        return FALSE;
    if (a->prop_count != b->prop_count)
        return FALSE;
    for (UINT i = 0; i < a->prop_count; i++) {
        const WProp *p = &a->props[i];
        const WProp *q = wo_prop(b, p->name);
        if (!q || q->type != p->type)
            return FALSE;
        if (!(flags & WBEM_FLAG_IGNORE_DEFAULT_VALUES) || !a->is_class)
            if (!variant_equal(&p->value, &q->value, p->type))
                return FALSE;
        if (!(flags & WBEM_FLAG_IGNORE_QUALIFIERS) && !quals_equal(&p->quals, &q->quals))
            return FALSE;
    }
    if (!(flags & WBEM_FLAG_IGNORE_QUALIFIERS) && !quals_equal(&a->quals, &b->quals))
        return FALSE;
    return a->meth_count == b->meth_count;
}
