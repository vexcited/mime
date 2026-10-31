/* the mof text of classes and instances. */
#include "wbemcore.h"

static void add_text_literal(WBuf *buf, const WCHAR *text)
{
    wb_buf_addc(buf, '"');
    for (; *text; text++) {
        switch (*text) {
        case '"':
            wb_buf_add(buf, L"\\\"");
            break;
        case '\\':
            wb_buf_add(buf, L"\\\\");
            break;
        case '\n':
            wb_buf_add(buf, L"\\n");
            break;
        case '\r':
            wb_buf_add(buf, L"\\r");
            break;
        case '\t':
            wb_buf_add(buf, L"\\t");
            break;
        default:
            if (*text < 0x20)
                wb_buf_addf(buf, L"\\x%04X", (UINT)*text);
            else
                wb_buf_addc(buf, *text);
            break;
        }
    }
    wb_buf_addc(buf, '"');
}

/* one scalar as mof source. */
static void add_scalar(WBuf *buf, const VARIANT *v, CIMTYPE type)
{
    switch (type & ~CIM_FLAG_ARRAY) {
    case CIM_STRING:
    case CIM_DATETIME:
    case CIM_REFERENCE:
        if (v->vt == VT_BSTR && v->bstrVal)
            add_text_literal(buf, v->bstrVal);
        else
            wb_buf_add(buf, L"NULL");
        break;
    case CIM_CHAR16:
        wb_buf_addf(buf, L"%d", (int)v->iVal);
        break;
    default: {
        WCHAR *text = wv_text(v, type & ~CIM_FLAG_ARRAY);
        wb_buf_add(buf, text);
        wb_free(text);
        break;
    }
    }
}

static void add_value(WBuf *buf, const VARIANT *v, CIMTYPE type)
{
    if (wv_is_null(v)) {
        wb_buf_add(buf, L"NULL");
        return;
    }
    if (!(v->vt & VT_ARRAY)) {
        add_scalar(buf, v, type);
        return;
    }
    wb_buf_addc(buf, '{');
    if (v->parray) {
        LONG low, high;
        SafeArrayGetLBound(v->parray, 1, &low);
        SafeArrayGetUBound(v->parray, 1, &high);
        VARTYPE vt = (VARTYPE)(v->vt & VT_TYPEMASK);
        for (LONG i = low; i <= high; i++) {
            VARIANT item;
            VariantInit(&item);
            item.vt = vt;
            if (vt == VT_BSTR || vt == VT_UNKNOWN)
                SafeArrayGetElement(v->parray, &i, &item.bstrVal);
            else if (vt == VT_R8 || vt == VT_I8 || vt == VT_UI8)
                SafeArrayGetElement(v->parray, &i, &item.llVal);
            else
                SafeArrayGetElement(v->parray, &i, &item.lVal);
            if (i > low)
                wb_buf_add(buf, L", ");
            add_scalar(buf, &item, type);
            if (vt == VT_BSTR)
                SysFreeString(item.bstrVal);
        }
    }
    wb_buf_addc(buf, '}');
}

/* the type of a qualifier value from its variant. */
static CIMTYPE qual_type(const VARIANT *v)
{
    switch (v->vt & VT_TYPEMASK) {
    case VT_BOOL:
        return CIM_BOOLEAN;
    case VT_I4:
        return CIM_SINT32;
    case VT_R8:
        return CIM_REAL64;
    default:
        return CIM_STRING;
    }
}

static void add_flavor(WBuf *buf, LONG flavor)
{
    LONG visible = flavor & (WBEM_FLAVOR_MASK_PROPAGATION | WBEM_FLAVOR_MASK_PERMISSIONS | WBEM_FLAVOR_MASK_AMENDED);
    if (visible == WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS)
        return;
    wb_buf_add(buf, L" :");
    if ((visible & 3) == 0)
        wb_buf_add(buf, L" Restricted");
    if (visible & WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE)
        wb_buf_add(buf, L" ToInstance");
    if (visible & WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS)
        wb_buf_add(buf, L" ToSubclass");
    if (visible & WBEM_FLAVOR_NOT_OVERRIDABLE)
        wb_buf_add(buf, L" DisableOverride");
    if (visible & WBEM_FLAVOR_AMENDED)
        wb_buf_add(buf, L" Amended");
}

/* [a, b("c")] for the qualifiers that are written, with a trailing separator. */
static void add_quals(WBuf *buf, const WQuals *quals, const WCHAR *separator, BOOL local_only)
{
    BOOL first = TRUE;
    for (UINT i = 0; i < quals->count; i++) {
        const WQual *q = &quals->items[i];
        if (wb_eq(q->name, L"CIMTYPE") || (local_only && (q->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED)))
            continue;
        wb_buf_add(buf, first ? L"[" : L", ");
        first = FALSE;
        wb_buf_add(buf, q->name);
        if (q->value.vt & VT_ARRAY) {
            add_value(buf, &q->value, CIM_STRING | CIM_FLAG_ARRAY);
        } else if (q->value.vt == VT_BOOL && q->value.boolVal) {
        } else {
            wb_buf_addc(buf, '(');
            add_value(buf, &q->value, qual_type(&q->value));
            wb_buf_addc(buf, ')');
        }
        add_flavor(buf, q->flavor);
    }
    if (!first) {
        wb_buf_addc(buf, ']');
        wb_buf_add(buf, separator);
    }
}

/* the declared type of a property: ref Class, object, or a scalar name. */
static void add_type(WBuf *buf, const WProp *p)
{
    WQual *cim = wq_find(&p->quals, L"CIMTYPE");
    CIMTYPE base = p->type & ~CIM_FLAG_ARRAY;
    if (base == CIM_REFERENCE) {
        const WCHAR *target = cim && cim->value.vt == VT_BSTR && cim->value.bstrVal ? cim->value.bstrVal : L"ref:";
        wb_buf_add(buf, L"ref");
        if (!wcsncmp(target, L"ref:", 4) && target[4]) {
            wb_buf_addc(buf, ' ');
            wb_buf_add(buf, target + 4);
        }
        return;
    }
    if (base == CIM_OBJECT && cim && cim->value.vt == VT_BSTR && cim->value.bstrVal &&
        !wcsncmp(cim->value.bstrVal, L"object:", 7) && cim->value.bstrVal[7]) {
        wb_buf_add(buf, cim->value.bstrVal + 7);
        wb_buf_add(buf, L" object");
        return;
    }
    wb_buf_add(buf, wv_cim_name(p->type));
}

static void add_parameter(WBuf *buf, const WProp *p)
{
    add_quals(buf, &p->quals, L" ", FALSE);
    add_type(buf, p);
    wb_buf_addc(buf, ' ');
    wb_buf_add(buf, p->name);
    if (p->type & CIM_FLAG_ARRAY)
        wb_buf_add(buf, L"[]");
}

static LONG param_id(const WProp *p)
{
    WQual *q = wq_find(&p->quals, L"ID");
    return q && q->value.vt == VT_I4 ? q->value.lVal : 0;
}

static void add_method(WBuf *buf, const WMeth *m)
{
    wb_buf_add(buf, L"\t");
    add_quals(buf, &m->quals, L" ", TRUE);
    const WProp *ret = m->out ? wo_prop(m->out, L"ReturnValue") : nullptr;
    if (ret)
        add_type(buf, ret);
    else
        wb_buf_add(buf, L"void");
    wb_buf_addc(buf, ' ');
    wb_buf_add(buf, m->name);
    wb_buf_addc(buf, '(');
    BOOL first = TRUE;
    UINT in_count = m->in ? m->in->prop_count : 0, out_count = m->out ? m->out->prop_count : 0;
    for (LONG id = 0; id < 256; id++) {
        for (UINT pass = 0; pass < 2; pass++) {
            const WObj *o = pass ? m->out : m->in;
            UINT count = pass ? out_count : in_count;
            for (UINT i = 0; i < count; i++) {
                const WProp *p = &o->props[i];
                if (wb_eq(p->name, L"ReturnValue") || param_id(p) != id)
                    continue;
                if (pass && m->in && wo_prop(m->in, p->name))
                    continue;
                if (!first)
                    wb_buf_add(buf, L", ");
                first = FALSE;
                add_parameter(buf, p);
            }
        }
    }
    wb_buf_add(buf, L");\n");
}

WCHAR *wo_text(const WObj *o, LONG flags)
{
    WBuf buf = {0};
    BOOL local_only = TRUE;
    (void)flags;
    wb_buf_addc(&buf, '\n');
    if (o->is_class) {
        add_quals(&buf, &o->quals, L"\n", local_only);
        wb_buf_add(&buf, L"class ");
        wb_buf_add(&buf, o->name);
        if (o->super) {
            wb_buf_add(&buf, L" : ");
            wb_buf_add(&buf, o->super);
        }
        wb_buf_add(&buf, L"\n{\n");
        for (UINT i = 0; i < o->prop_count; i++) {
            const WProp *p = &o->props[i];
            if (!p->local && o->super)
                continue;
            wb_buf_addc(&buf, '\t');
            add_quals(&buf, &p->quals, L" ", TRUE);
            add_type(&buf, p);
            wb_buf_addc(&buf, ' ');
            wb_buf_add(&buf, p->name);
            if (p->type & CIM_FLAG_ARRAY)
                wb_buf_add(&buf, L"[]");
            if (!wv_is_null(&p->value)) {
                wb_buf_add(&buf, L" = ");
                add_value(&buf, &p->value, p->type);
            }
            wb_buf_add(&buf, L";\n");
        }
        for (UINT i = 0; i < o->meth_count; i++)
            if (o->meths[i].local || !o->super)
                add_method(&buf, &o->meths[i]);
        wb_buf_add(&buf, L"};\n");
    } else {
        wb_buf_add(&buf, L"\ninstance of ");
        wb_buf_add(&buf, o->name);
        wb_buf_add(&buf, L"\n{\n");
        for (UINT i = 0; i < o->prop_count; i++) {
            const WProp *p = &o->props[i];
            if (wv_is_null(&p->value))
                continue;
            wb_buf_addc(&buf, '\t');
            wb_buf_add(&buf, p->name);
            wb_buf_add(&buf, L" = ");
            add_value(&buf, &p->value, p->type);
            wb_buf_add(&buf, L";\n");
        }
        wb_buf_add(&buf, L"};\n");
    }
    return wb_buf_take(&buf);
}
