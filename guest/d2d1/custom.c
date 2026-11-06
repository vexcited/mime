#include "effects.h"
#include <float.h>
#include <stddef.h>

/* ------------------------------------------------------------------ strings and numbers */

static WCHAR *wdup(const WCHAR *s)
{
    if (!s)
        return nullptr;
    SIZE_T n = wcslen(s) + 1;
    WCHAR *d = D2dAlloc(n * sizeof(WCHAR));
    if (d)
        memcpy(d, s, n * sizeof(WCHAR));
    return d;
}

static WCHAR *wdupn(const WCHAR *s, SIZE_T n)
{
    WCHAR *d = D2dAlloc((n + 1) * sizeof(WCHAR));
    if (d) {
        memcpy(d, s, n * sizeof(WCHAR));
        d[n] = 0;
    }
    return d;
}

static WCHAR wlower(WCHAR c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static BOOL wsame(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b)
        return FALSE;
    for (; *a && *b; a++, b++) {
        if (wlower(*a) != wlower(*b))
            return FALSE;
    }
    return *a == *b;
}

static BOOL wspace(WCHAR c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static const WCHAR *skip_space(const WCHAR *p)
{
    while (wspace(*p))
        p++;
    return p;
}

static BOOL parse_float(const WCHAR **pp, float *out)
{
    const WCHAR *p = skip_space(*pp);
    double sign = 1.0, v = 0.0;
    if (*p == '-' || *p == '+')
        sign = *p++ == '-' ? -1.0 : 1.0;
    if (wlower(p[0]) == 'i' && wlower(p[1]) == 'n' && wlower(p[2]) == 'f') {
        p += 3;
        *out = (float)(sign * (double)FLT_MAX * 10.0);
        *pp = p;
        return TRUE;
    }
    BOOL digits = FALSE;
    while (*p >= '0' && *p <= '9') {
        v = v * 10.0 + (*p++ - '0');
        digits = TRUE;
    }
    if (*p == '.') {
        double scale = 0.1;
        p++;
        while (*p >= '0' && *p <= '9') {
            v += (*p++ - '0') * scale;
            scale *= 0.1;
            digits = TRUE;
        }
    }
    if (!digits)
        return FALSE;
    if (wlower(*p) == 'e') {
        const WCHAR *q = p + 1;
        int es = 1, e = 0;
        if (*q == '-' || *q == '+')
            es = *q++ == '-' ? -1 : 1;
        if (*q >= '0' && *q <= '9') {
            while (*q >= '0' && *q <= '9')
                e = e * 10 + (*q++ - '0');
            p = q;
            for (; e > 0; e--)
                v = es > 0 ? v * 10.0 : v / 10.0;
        }
    }
    if (wlower(*p) == 'f')
        p++;
    *out = (float)(sign * v);
    *pp = p;
    return TRUE;
}

static BOOL parse_uint(const WCHAR *p, UINT32 *out, BOOL is_signed)
{
    p = skip_space(p);
    BOOL neg = FALSE;
    if (is_signed && (*p == '-' || *p == '+'))
        neg = *p++ == '-';
    UINT64 v = 0;
    BOOL digits = FALSE;
    if (p[0] == '0' && wlower(p[1]) == 'x') {
        p += 2;
        for (;; p++) {
            WCHAR c = wlower(*p);
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0)
                break;
            v = v * 16 + (UINT64)d;
            digits = TRUE;
        }
    } else {
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (UINT64)(*p++ - '0');
            digits = TRUE;
        }
    }
    if (!digits || *skip_space(p))
        return FALSE;
    *out = neg ? (UINT32)-(INT64)v : (UINT32)v;
    return TRUE;
}

static int hexval(WCHAR c)
{
    c = wlower(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static BOOL parse_guid(const WCHAR *p, GUID *g)
{
    p = skip_space(p);
    if (*p == '{')
        p++;
    static const int groups[5] = {8, 4, 4, 4, 12};
    BYTE b[16];
    int n = 0;
    for (int gi = 0; gi < 5; gi++) {
        for (int i = 0; i < groups[gi]; i += 2) {
            int hi = hexval(p[0]), lo = hi < 0 ? -1 : hexval(p[1]);
            if (lo < 0)
                return FALSE;
            b[n++] = (BYTE)(hi << 4 | lo);
            p += 2;
        }
        if (gi < 4 && *p++ != '-')
            return FALSE;
    }
    g->Data1 = (UINT32)b[0] << 24 | (UINT32)b[1] << 16 | (UINT32)b[2] << 8 | b[3];
    g->Data2 = (USHORT)(b[4] << 8 | b[5]);
    g->Data3 = (USHORT)(b[6] << 8 | b[7]);
    memcpy(g->Data4, b + 8, 8);
    return TRUE;
}

/* ------------------------------------------------------------------ xml */

typedef struct XNode {
    WCHAR *name;
    UINT32 nattr;
    WCHAR **anames, **avals;
    struct XNode **kids;
    UINT32 nkids;
} XNode;

static void xfree(XNode *n)
{
    if (!n)
        return;
    D2dFree(n->name);
    for (UINT32 i = 0; i < n->nattr; i++) {
        D2dFree(n->anames[i]);
        D2dFree(n->avals[i]);
    }
    D2dFree(n->anames);
    D2dFree(n->avals);
    for (UINT32 i = 0; i < n->nkids; i++)
        xfree(n->kids[i]);
    D2dFree(n->kids);
    D2dFree(n);
}

static BOOL xname_char(WCHAR c) { return c && !wspace(c) && c != '=' && c != '>' && c != '/' && c != '<' && c != '?' && c != '\'' && c != '"'; }

/* an attribute value with its entities decoded. */
static WCHAR *xdecode(const WCHAR *s, SIZE_T n)
{
    WCHAR *d = D2dAlloc((n + 1) * sizeof(WCHAR));
    if (!d)
        return nullptr;
    SIZE_T o = 0;
    for (SIZE_T i = 0; i < n;) {
        if (s[i] != '&') {
            d[o++] = s[i++];
            continue;
        }
        SIZE_T e = i + 1;
        while (e < n && s[e] != ';')
            e++;
        const WCHAR *ent = s + i + 1;
        SIZE_T len = e - i - 1;
        WCHAR c = 0;
        if (len == 2 && ent[0] == 'l' && ent[1] == 't')
            c = '<';
        else if (len == 2 && ent[0] == 'g' && ent[1] == 't')
            c = '>';
        else if (len == 3 && ent[0] == 'a' && ent[1] == 'm' && ent[2] == 'p')
            c = '&';
        else if (len == 4 && ent[0] == 'q')
            c = '"';
        else if (len == 4 && ent[0] == 'a')
            c = '\'';
        else if (len > 1 && ent[0] == '#') {
            UINT32 v = 0;
            for (SIZE_T k = 1; k < len; k++) {
                if (ent[1] == 'x' && k == 1)
                    continue;
                int h = ent[1] == 'x' ? hexval(ent[k]) : (ent[k] >= '0' && ent[k] <= '9' ? ent[k] - '0' : -1);
                if (h < 0)
                    break;
                v = v * (ent[1] == 'x' ? 16 : 10) + (UINT32)h;
            }
            c = (WCHAR)v;
        }
        if (c && e < n) {
            d[o++] = c;
            i = e + 1;
        } else {
            d[o++] = s[i++];
        }
    }
    d[o] = 0;
    return d;
}

static const WCHAR *xskip_misc(const WCHAR *p)
{
    for (;;) {
        p = skip_space(p);
        if (p[0] == '<' && p[1] == '?') {
            const WCHAR *e = p + 2;
            while (*e && !(e[0] == '?' && e[1] == '>'))
                e++;
            p = *e ? e + 2 : e;
        } else if (p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const WCHAR *e = p + 4;
            while (*e && !(e[0] == '-' && e[1] == '-' && e[2] == '>'))
                e++;
            p = *e ? e + 3 : e;
        } else if (p[0] == '<' && p[1] == '!') {
            const WCHAR *e = p + 2;
            while (*e && *e != '>')
                e++;
            p = *e ? e + 1 : e;
        } else if (*p && *p != '<') {
            /* text between elements carries nothing here. */
            while (*p && *p != '<')
                p++;
        } else {
            return p;
        }
    }
}

static BOOL xadd(void ***array, UINT32 *count, void *item)
{
    void **grown = D2dRealloc(*array, (*count + 1) * sizeof(void *));
    if (!grown)
        return FALSE;
    grown[(*count)++] = item;
    *array = grown;
    return TRUE;
}

static XNode *xparse_element(const WCHAR **pp, int depth)
{
    const WCHAR *p = *pp;
    if (*p != '<' || depth > 32)
        return nullptr;
    p++;
    const WCHAR *start = p;
    while (xname_char(*p))
        p++;
    if (p == start)
        return nullptr;
    XNode *n = D2dAllocZero(sizeof(*n));
    if (!n || !(n->name = wdupn(start, (SIZE_T)(p - start)))) {
        D2dFree(n);
        return nullptr;
    }
    for (;;) {
        p = skip_space(p);
        if (p[0] == '/' && p[1] == '>') {
            *pp = p + 2;
            return n;
        }
        if (*p == '>') {
            p++;
            break;
        }
        start = p;
        while (xname_char(*p))
            p++;
        if (p == start)
            goto fail;
        WCHAR *aname = wdupn(start, (SIZE_T)(p - start));
        p = skip_space(p);
        if (*p++ != '=') {
            D2dFree(aname);
            goto fail;
        }
        p = skip_space(p);
        WCHAR quote = *p;
        if (quote != '\'' && quote != '"') {
            D2dFree(aname);
            goto fail;
        }
        start = ++p;
        while (*p && *p != quote)
            p++;
        if (!*p) {
            D2dFree(aname);
            goto fail;
        }
        WCHAR *aval = xdecode(start, (SIZE_T)(p - start));
        p++;
        UINT32 count = n->nattr;
        if (!aname || !aval || !xadd((void ***)&n->anames, &count, aname) || !xadd((void ***)&n->avals, &n->nattr, aval)) {
            D2dFree(aname);
            D2dFree(aval);
            goto fail;
        }
    }
    for (;;) {
        p = xskip_misc(p);
        if (p[0] == '<' && p[1] == '/') {
            while (*p && *p != '>')
                p++;
            if (!*p)
                goto fail;
            *pp = p + 1;
            return n;
        }
        if (*p != '<')
            goto fail;
        XNode *kid = xparse_element(&p, depth + 1);
        if (!kid || !xadd((void ***)&n->kids, &n->nkids, kid)) {
            xfree(kid);
            goto fail;
        }
    }
fail:
    xfree(n);
    return nullptr;
}

static XNode *xparse(const WCHAR *text)
{
    const WCHAR *p = text;
    if (*p == 0xfeff)
        p++;
    p = xskip_misc(p);
    return xparse_element(&p, 0);
}

static const WCHAR *xattr(const XNode *n, const WCHAR *name)
{
    for (UINT32 i = 0; i < n->nattr; i++) {
        if (wsame(n->anames[i], name))
            return n->avals[i];
    }
    return nullptr;
}

/* ------------------------------------------------------------------ property values */

static UINT32 type_size(D2D1_PROPERTY_TYPE t)
{
    switch (t) {
    case D2D1_PROPERTY_TYPE_BOOL:
    case D2D1_PROPERTY_TYPE_UINT32:
    case D2D1_PROPERTY_TYPE_INT32:
    case D2D1_PROPERTY_TYPE_FLOAT:
    case D2D1_PROPERTY_TYPE_ENUM:
    case D2D1_PROPERTY_TYPE_ARRAY:
        return 4;
    case D2D1_PROPERTY_TYPE_VECTOR2:
        return 8;
    case D2D1_PROPERTY_TYPE_VECTOR3:
        return 12;
    case D2D1_PROPERTY_TYPE_VECTOR4:
    case D2D1_PROPERTY_TYPE_CLSID:
        return 16;
    case D2D1_PROPERTY_TYPE_MATRIX_3X2:
        return 24;
    case D2D1_PROPERTY_TYPE_MATRIX_4X3:
        return 48;
    case D2D1_PROPERTY_TYPE_MATRIX_4X4:
        return 64;
    case D2D1_PROPERTY_TYPE_MATRIX_5X4:
        return 80;
    case D2D1_PROPERTY_TYPE_IUNKNOWN:
    case D2D1_PROPERTY_TYPE_COLOR_CONTEXT:
        return sizeof(void *);
    default:
        return 0;
    }
}

static BOOL is_interface(D2D1_PROPERTY_TYPE t) { return t == D2D1_PROPERTY_TYPE_IUNKNOWN || t == D2D1_PROPERTY_TYPE_COLOR_CONTEXT; }

static const struct {
    const WCHAR *name;
    D2D1_PROPERTY_TYPE type;
} g_type_names[] = {
    {L"string", D2D1_PROPERTY_TYPE_STRING},       {L"bool", D2D1_PROPERTY_TYPE_BOOL},
    {L"uint32", D2D1_PROPERTY_TYPE_UINT32},       {L"int32", D2D1_PROPERTY_TYPE_INT32},
    {L"float", D2D1_PROPERTY_TYPE_FLOAT},         {L"vector2", D2D1_PROPERTY_TYPE_VECTOR2},
    {L"vector3", D2D1_PROPERTY_TYPE_VECTOR3},     {L"vector4", D2D1_PROPERTY_TYPE_VECTOR4},
    {L"blob", D2D1_PROPERTY_TYPE_BLOB},           {L"iunknown", D2D1_PROPERTY_TYPE_IUNKNOWN},
    {L"enum", D2D1_PROPERTY_TYPE_ENUM},           {L"array", D2D1_PROPERTY_TYPE_ARRAY},
    {L"clsid", D2D1_PROPERTY_TYPE_CLSID},         {L"matrix3x2", D2D1_PROPERTY_TYPE_MATRIX_3X2},
    {L"matrix4x3", D2D1_PROPERTY_TYPE_MATRIX_4X3}, {L"matrix4x4", D2D1_PROPERTY_TYPE_MATRIX_4X4},
    {L"matrix5x4", D2D1_PROPERTY_TYPE_MATRIX_5X4}, {L"colorcontext", D2D1_PROPERTY_TYPE_COLOR_CONTEXT},
};

static D2D1_PROPERTY_TYPE type_of_name(const WCHAR *name)
{
    for (size_t i = 0; i < sizeof(g_type_names) / sizeof(g_type_names[0]); i++) {
        if (wsame(name, g_type_names[i].name))
            return g_type_names[i].type;
    }
    return D2D1_PROPERTY_TYPE_UNKNOWN;
}

/* the binary form of a value written in the xml. */
static HRESULT parse_value(D2D1_PROPERTY_TYPE type, const WCHAR *text, BYTE **out, UINT32 *size)
{
    *out = nullptr;
    *size = 0;
    if (type == D2D1_PROPERTY_TYPE_STRING) {
        *size = (UINT32)(wcslen(text) + 1) * sizeof(WCHAR);
        *out = (BYTE *)wdup(text);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    UINT32 n = type_size(type);
    if (!n || is_interface(type))
        return D2DERR_INVALID_PROPERTY;
    BYTE *v = D2dAllocZero(n);
    if (!v)
        return E_OUTOFMEMORY;
    BOOL ok = TRUE;
    switch (type) {
    case D2D1_PROPERTY_TYPE_BOOL:
        if (wsame(skip_space(text), L"true") || wsame(skip_space(text), L"1"))
            *(BOOL *)v = TRUE;
        else
            ok = wsame(skip_space(text), L"false") || wsame(skip_space(text), L"0");
        break;
    case D2D1_PROPERTY_TYPE_UINT32:
    case D2D1_PROPERTY_TYPE_ENUM:
    case D2D1_PROPERTY_TYPE_ARRAY:
        ok = parse_uint(text, (UINT32 *)v, FALSE);
        break;
    case D2D1_PROPERTY_TYPE_INT32:
        ok = parse_uint(text, (UINT32 *)v, TRUE);
        break;
    case D2D1_PROPERTY_TYPE_CLSID:
        ok = parse_guid(text, (GUID *)v);
        break;
    default: {
        /* floats, as one number or a list in parentheses. */
        const WCHAR *p = skip_space(text);
        if (*p == '(')
            p++;
        for (UINT32 i = 0; i < n / 4 && ok; i++) {
            p = skip_space(p);
            if (i && *p == ',')
                p++;
            ok = parse_float(&p, (float *)v + i);
        }
        break;
    }
    }
    if (!ok) {
        D2dFree(v);
        return D2DERR_INVALID_PROPERTY;
    }
    *out = v;
    *size = n;
    return S_OK;
}

/* ------------------------------------------------------------------ registrations */

typedef struct CField {
    WCHAR *name, *display;
    UINT32 index;
} CField;

typedef struct CProp {
    WCHAR *name, *display;
    D2D1_PROPERTY_TYPE type;
    BOOL readonly;
    BYTE *def, *min, *max;
    UINT32 def_size, min_size, max_size;
    CField *fields;
    UINT32 nfields;
    PD2D1_PROPERTY_SET_FUNCTION set;
    PD2D1_PROPERTY_GET_FUNCTION get;
} CProp;

typedef struct CReg {
    LONG refs;
    UINT32 registrations;
    CLSID clsid;
    PD2D1_EFFECT_FACTORY factory;
    WCHAR *sys[4];
    UINT32 min_inputs, max_inputs, ninputs;
    WCHAR **input_names;
    CProp *props;
    UINT32 nprops;
} CReg;

static void reg_free(CReg *r)
{
    for (int i = 0; i < 4; i++)
        D2dFree(r->sys[i]);
    for (UINT32 i = 0; i < r->ninputs; i++)
        D2dFree(r->input_names[i]);
    D2dFree(r->input_names);
    for (UINT32 i = 0; i < r->nprops; i++) {
        CProp *p = &r->props[i];
        D2dFree(p->name);
        D2dFree(p->display);
        D2dFree(p->def);
        D2dFree(p->min);
        D2dFree(p->max);
        for (UINT32 k = 0; k < p->nfields; k++) {
            D2dFree(p->fields[k].name);
            D2dFree(p->fields[k].display);
        }
        D2dFree(p->fields);
    }
    D2dFree(r->props);
    D2dFree(r);
}

static void reg_release(CReg *r)
{
    if (r && InterlockedDecrement(&r->refs) == 0)
        reg_free(r);
}

static const WCHAR *const g_sys_names[] = {L"Clsid", L"DisplayName", L"Author", L"Category", L"Description",
                                           L"Inputs", L"Cached", L"Precision", L"MinInputs", L"MaxInputs"};
#define SYS_COUNT 10
#define SYS_FIRST 0x80000000u

static HRESULT parse_property(CReg *r, const XNode *n)
{
    const WCHAR *name = xattr(n, L"name"), *type_name = xattr(n, L"type"), *value = xattr(n, L"value");
    if (!name || !type_name)
        return D2DERR_INVALID_PROPERTY;
    D2D1_PROPERTY_TYPE type = type_of_name(type_name);
    if (type == D2D1_PROPERTY_TYPE_UNKNOWN)
        return D2DERR_INVALID_PROPERTY;
    for (int i = 1; i <= 4; i++) {
        if (wsame(name, g_sys_names[i])) {
            if (type != D2D1_PROPERTY_TYPE_STRING)
                return D2DERR_INVALID_PROPERTY;
            D2dFree(r->sys[i - 1]);
            r->sys[i - 1] = wdup(value ? value : L"");
            return r->sys[i - 1] ? S_OK : E_OUTOFMEMORY;
        }
    }
    for (UINT32 i = 0; i < r->nprops; i++) {
        if (wsame(r->props[i].name, name))
            return D2DERR_INVALID_PROPERTY;
    }
    CProp *grown = D2dRealloc(r->props, (r->nprops + 1) * sizeof(CProp));
    if (!grown)
        return E_OUTOFMEMORY;
    r->props = grown;
    CProp *p = &r->props[r->nprops++];
    memset(p, 0, sizeof(*p));
    p->type = type;
    if (!(p->name = wdup(name)))
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (value)
        hr = parse_value(type, value, &p->def, &p->def_size);
    for (UINT32 k = 0; k < n->nkids && SUCCEEDED(hr); k++) {
        const XNode *c = n->kids[k];
        if (wsame(c->name, L"Fields")) {
            for (UINT32 f = 0; f < c->nkids; f++) {
                const XNode *fn = c->kids[f];
                const WCHAR *fname = xattr(fn, L"name"), *fdisplay = xattr(fn, L"displayname"), *findex = xattr(fn, L"index");
                if (!wsame(fn->name, L"Field") || !fname)
                    continue;
                CField *fields = D2dRealloc(p->fields, (p->nfields + 1) * sizeof(CField));
                if (!fields)
                    return E_OUTOFMEMORY;
                p->fields = fields;
                CField *field = &p->fields[p->nfields++];
                field->name = wdup(fname);
                field->display = wdup(fdisplay ? fdisplay : fname);
                field->index = p->nfields - 1;
                if (findex)
                    parse_uint(findex, &field->index, FALSE);
            }
            continue;
        }
        if (!wsame(c->name, L"Property"))
            continue;
        const WCHAR *sname = xattr(c, L"name"), *svalue = xattr(c, L"value");
        if (!sname || !svalue)
            continue;
        if (wsame(sname, L"DisplayName")) {
            D2dFree(p->display);
            p->display = wdup(svalue);
        } else if (wsame(sname, L"IsReadOnly")) {
            p->readonly = wsame(skip_space(svalue), L"true");
        } else if (wsame(sname, L"Default")) {
            D2dFree(p->def);
            hr = parse_value(type, svalue, &p->def, &p->def_size);
        } else if (wsame(sname, L"Min")) {
            D2dFree(p->min);
            hr = parse_value(type, svalue, &p->min, &p->min_size);
        } else if (wsame(sname, L"Max")) {
            D2dFree(p->max);
            hr = parse_value(type, svalue, &p->max, &p->max_size);
        }
    }
    return hr;
}

static HRESULT parse_registration(CReg *r, const WCHAR *xml)
{
    XNode *root = xparse(xml);
    if (!root)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    HRESULT hr = wsame(root->name, L"Effect") ? S_OK : D2DERR_INVALID_PROPERTY;
    BOOL have_inputs = FALSE;
    const WCHAR *min = nullptr, *max = nullptr;
    for (UINT32 i = 0; i < root->nkids && SUCCEEDED(hr); i++) {
        const XNode *n = root->kids[i];
        if (wsame(n->name, L"Property")) {
            hr = parse_property(r, n);
        } else if (wsame(n->name, L"Inputs")) {
            have_inputs = TRUE;
            min = xattr(n, L"minimum");
            max = xattr(n, L"maximum");
            for (UINT32 k = 0; k < n->nkids && SUCCEEDED(hr); k++) {
                const WCHAR *iname = xattr(n->kids[k], L"name");
                if (!wsame(n->kids[k]->name, L"Input"))
                    continue;
                WCHAR *copy = wdup(iname ? iname : L"");
                if (!copy || !xadd((void ***)&r->input_names, &r->ninputs, copy)) {
                    D2dFree(copy);
                    hr = E_OUTOFMEMORY;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && !have_inputs)
        hr = D2DERR_INVALID_PROPERTY;
    r->min_inputs = r->max_inputs = r->ninputs;
    if (SUCCEEDED(hr) && min && !parse_uint(min, &r->min_inputs, FALSE))
        hr = D2DERR_INVALID_PROPERTY;
    if (SUCCEEDED(hr) && max && !parse_uint(max, &r->max_inputs, FALSE))
        hr = D2DERR_INVALID_PROPERTY;
    if (SUCCEEDED(hr) && (r->min_inputs > r->max_inputs || r->ninputs < r->min_inputs || r->ninputs > r->max_inputs))
        hr = D2DERR_INVALID_PROPERTY;
    xfree(root);
    return hr;
}

static CReg **reg_list(D2dFactory *f) { return (CReg **)f->effects.data; }

static CReg *reg_find(D2dFactory *f, REFCLSID clsid, UINT32 *at)
{
    for (UINT32 i = 0; i < f->effects.count; i++) {
        if (IsEqualGUID(&reg_list(f)[i]->clsid, clsid)) {
            if (at)
                *at = i;
            return reg_list(f)[i];
        }
    }
    return nullptr;
}

HRESULT D2dCustomRegister(D2dFactory *f, REFCLSID clsid, const WCHAR *xml, const void *bindings, UINT32 count, void *factory)
{
    D2dFactoryEnter(f);
    CReg *old = reg_find(f, clsid, nullptr);
    if (old) {
        /* registering again only counts, the first description stays. */
        old->registrations++;
        D2dFactoryLeave(f);
        return S_OK;
    }
    D2dFactoryLeave(f);
    CReg *r = D2dAllocZero(sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->refs = 1;
    r->registrations = 1;
    r->clsid = *clsid;
    r->factory = (PD2D1_EFFECT_FACTORY)factory;
    HRESULT hr = parse_registration(r, xml);
    const D2D1_PROPERTY_BINDING *b = bindings;
    for (UINT32 i = 0; i < count && SUCCEEDED(hr); i++) {
        CProp *p = nullptr;
        for (UINT32 k = 0; k < r->nprops; k++) {
            if (b[i].propertyName && wcscmp(r->props[k].name, b[i].propertyName) == 0)
                p = &r->props[k];
        }
        if (!p) {
            hr = D2DERR_INVALID_PROPERTY;
            break;
        }
        p->set = b[i].setFunction;
        p->get = b[i].getFunction;
        if (!p->get)
            hr = E_INVALIDARG;
    }
    if (FAILED(hr)) {
        reg_free(r);
        return hr;
    }
    D2dFactoryEnter(f);
    if (D2dArrayGrow(&f->effects, sizeof(CReg *), 1)) {
        reg_list(f)[f->effects.count++] = r;
        f->registered_count++;
    } else {
        hr = E_OUTOFMEMORY;
    }
    D2dFactoryLeave(f);
    if (FAILED(hr))
        reg_free(r);
    return hr;
}

HRESULT D2dCustomUnregister(D2dFactory *f, REFCLSID clsid)
{
    UINT32 at;
    D2dFactoryEnter(f);
    CReg *r = reg_find(f, clsid, &at);
    if (!r) {
        D2dFactoryLeave(f);
        return D2DERR_EFFECT_IS_NOT_REGISTERED;
    }
    if (--r->registrations == 0) {
        memmove(reg_list(f) + at, reg_list(f) + at + 1, (f->effects.count - at - 1) * sizeof(CReg *));
        f->effects.count--;
        f->registered_count--;
        reg_release(r);
    }
    D2dFactoryLeave(f);
    return S_OK;
}

UINT32 D2dCustomList(D2dFactory *f, CLSID *list, UINT32 max)
{
    D2dFactoryEnter(f);
    UINT32 n = f->effects.count;
    for (UINT32 i = 0; list && i < n && i < max; i++)
        list[i] = reg_list(f)[i]->clsid;
    D2dFactoryLeave(f);
    return n;
}

void D2dCustomFreeRegistry(D2dFactory *f)
{
    for (UINT32 i = 0; i < f->effects.count; i++)
        reg_release(reg_list(f)[i]);
    f->effects.count = 0;
}

/* ------------------------------------------------------------------ fixed property sets */

typedef struct PsEntry {
    UINT32 index;
    WCHAR *name;
    D2D1_PROPERTY_TYPE type;
    BYTE *data;
    UINT32 size;
    ID2D1Properties *sub;
} PsEntry;

typedef struct PropSet {
    D2dObj obj;
    PsEntry *e;
    UINT32 n, custom;
} PropSet;

static D2dClass g_ps_class;
#define PS PropSet *s = (PropSet *)This

static PsEntry *ps_entry(PropSet *s, UINT32 index)
{
    for (UINT32 i = 0; i < s->n; i++) {
        if (s->e[i].index == index)
            return &s->e[i];
    }
    return nullptr;
}

static UINT32 STDMETHODCALLTYPE ps_count(ID2D1Properties *This) { PS; return s->custom; }
static HRESULT STDMETHODCALLTYPE ps_name(ID2D1Properties *This, UINT32 index, WCHAR *name, UINT32 count)
{
    PS;
    PsEntry *e = ps_entry(s, index);
    if (!e)
        return D2DERR_INVALID_PROPERTY;
    UINT32 len = (UINT32)wcslen(e->name);
    if (!name || count < len + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(name, e->name, (len + 1) * sizeof(WCHAR));
    return S_OK;
}
static UINT32 STDMETHODCALLTYPE ps_name_length(ID2D1Properties *This, UINT32 index)
{
    PS;
    PsEntry *e = ps_entry(s, index);
    return e ? (UINT32)wcslen(e->name) : 0;
}
static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE ps_type(ID2D1Properties *This, UINT32 index)
{
    PS;
    PsEntry *e = ps_entry(s, index);
    return e ? e->type : D2D1_PROPERTY_TYPE_UNKNOWN;
}
static UINT32 STDMETHODCALLTYPE ps_index(ID2D1Properties *This, const WCHAR *name)
{
    PS;
    for (UINT32 i = 0; name && i < s->n; i++) {
        if (wcscmp(s->e[i].name, name) == 0)
            return s->e[i].index;
    }
    return 0xffffffffu;
}
static HRESULT STDMETHODCALLTYPE ps_set_by_name(ID2D1Properties *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    return ps_index(This, name) == 0xffffffffu ? D2DERR_INVALID_PROPERTY : D2DERR_INVALID_CALL;
}
static HRESULT STDMETHODCALLTYPE ps_set(ID2D1Properties *This, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    PS;
    return ps_entry(s, index) ? D2DERR_INVALID_CALL : D2DERR_INVALID_PROPERTY;
}
static HRESULT STDMETHODCALLTYPE ps_get(ID2D1Properties *This, UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    PS;
    PsEntry *e = ps_entry(s, index);
    if (!e)
        return D2DERR_INVALID_PROPERTY;
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && type != e->type)
        return E_INVALIDARG;
    if (!data || size < e->size)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (e->size)
        memcpy(data, e->data, e->size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_get_by_name(ID2D1Properties *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    UINT32 i = ps_index(This, name);
    return i == 0xffffffffu ? D2DERR_INVALID_PROPERTY : ps_get(This, i, type, data, size);
}
static UINT32 STDMETHODCALLTYPE ps_value_size(ID2D1Properties *This, UINT32 index)
{
    PS;
    PsEntry *e = ps_entry(s, index);
    return e ? e->size : 0;
}
static HRESULT STDMETHODCALLTYPE ps_subprops(ID2D1Properties *This, UINT32 index, ID2D1Properties **out)
{
    PS;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    PsEntry *e = ps_entry(s, index);
    if (!e)
        return D2DERR_INVALID_PROPERTY;
    if (!e->sub)
        return D2DERR_NO_SUBPROPERTIES;
    *out = e->sub;
    D2D_ADDREF(*out);
    return S_OK;
}
static void ps_destroy(D2dObj *o)
{
    PropSet *s = (PropSet *)o;
    for (UINT32 i = 0; i < s->n; i++) {
        D2dFree(s->e[i].name);
        D2dFree(s->e[i].data);
        D2D_RELEASE(s->e[i].sub);
    }
    D2dFree(s->e);
}
static const void *const g_ps_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ps_count, ps_name, ps_name_length, ps_type,
                                        ps_index, ps_set_by_name, ps_set, ps_get_by_name, ps_get, ps_value_size, ps_subprops};
D2D_IIDS(g_ps_iids, &IID_ID2D1Properties);
static D2dClass g_ps_class = {"properties", g_ps_iids, ps_destroy, nullptr};

static PropSet *ps_new(D2dFactory *f) { return D2dNew(sizeof(PropSet), g_ps_vtbl, &g_ps_class, f); }

/* adds an entry, taking the sub properties; the value is copied. */
static BOOL ps_add(PropSet *s, UINT32 index, const WCHAR *name, D2D1_PROPERTY_TYPE type, const void *data, UINT32 size, PropSet *sub)
{
    PsEntry *e = D2dRealloc(s->e, (s->n + 1) * sizeof(PsEntry));
    if (!e) {
        D2dReleaseObj(sub);
        return FALSE;
    }
    s->e = e;
    PsEntry *n = &s->e[s->n];
    n->index = index;
    n->name = wdup(name);
    n->type = type;
    n->size = size;
    n->data = size ? D2dAlloc(size) : nullptr;
    n->sub = (ID2D1Properties *)sub;
    if (!n->name || (size && !n->data)) {
        D2dFree(n->name);
        D2dFree(n->data);
        D2dReleaseObj(sub);
        return FALSE;
    }
    if (size)
        memcpy(n->data, data, size);
    s->n++;
    if (index < SYS_FIRST)
        s->custom++;
    return TRUE;
}

static BOOL ps_add_string(PropSet *s, UINT32 index, const WCHAR *name, const WCHAR *value, PropSet *sub)
{
    value = value ? value : L"";
    return ps_add(s, index, name, D2D1_PROPERTY_TYPE_STRING, value, (UINT32)(wcslen(value) + 1) * sizeof(WCHAR), sub);
}

/* the sub properties windows gives each property of a custom effect. */
static PropSet *prop_subprops(D2dFactory *f, const CProp *p)
{
    PropSet *s = ps_new(f);
    if (!s)
        return nullptr;
    BOOL ok = ps_add_string(s, D2D1_SUBPROPERTY_DISPLAYNAME, L"DisplayName", p->display ? p->display : p->name, nullptr);
    BOOL ro = p->readonly || (!p->set && p->get);
    ok = ok && ps_add(s, D2D1_SUBPROPERTY_ISREADONLY, L"IsReadOnly", D2D1_PROPERTY_TYPE_BOOL, &ro, 4, nullptr);
    if (ok && p->min)
        ok = ps_add(s, D2D1_SUBPROPERTY_MIN, L"Min", p->type, p->min, p->min_size, nullptr);
    if (ok && p->max)
        ok = ps_add(s, D2D1_SUBPROPERTY_MAX, L"Max", p->type, p->max, p->max_size, nullptr);
    if (ok && p->def)
        ok = ps_add(s, D2D1_SUBPROPERTY_DEFAULT, L"Default", p->type, p->def, p->def_size, nullptr);
    if (ok && p->type == D2D1_PROPERTY_TYPE_ENUM) {
        PropSet *fields = ps_new(f);
        ok = fields != nullptr;
        for (UINT32 i = 0; ok && i < p->nfields; i++) {
            PropSet *fs = ps_new(f);
            ok = fs && ps_add_string(fs, D2D1_SUBPROPERTY_DISPLAYNAME, L"DisplayName", p->fields[i].display, nullptr) &&
                 ps_add(fs, D2D1_SUBPROPERTY_INDEX, L"Index", D2D1_PROPERTY_TYPE_UINT32, &p->fields[i].index, 4, nullptr);
            ok = ok && ps_add_string(fields, i, p->fields[i].name, p->fields[i].display, fs);
            if (!ok)
                D2dReleaseObj(fs);
        }
        UINT32 nf = p->nfields;
        ok = ok && ps_add(s, D2D1_SUBPROPERTY_FIELDS, L"Fields", D2D1_PROPERTY_TYPE_ARRAY, &nf, 4, fields);
    }
    if (!ok) {
        D2dRelease(s);
        return nullptr;
    }
    return s;
}

static PropSet *inputs_subprops(D2dFactory *f, const CReg *r, UINT32 count)
{
    PropSet *s = ps_new(f);
    for (UINT32 i = 0; s && i < count; i++) {
        WCHAR name[12];
        UINT32 v = i, n = 0;
        WCHAR digits[12];
        do {
            digits[n++] = (WCHAR)('0' + v % 10);
            v /= 10;
        } while (v);
        for (UINT32 k = 0; k < n; k++)
            name[k] = digits[n - 1 - k];
        name[n] = 0;
        if (!ps_add_string(s, i, name, i < r->ninputs ? r->input_names[i] : L"", nullptr)) {
            D2dRelease(s);
            return nullptr;
        }
    }
    return s;
}

HRESULT D2dCustomProperties(D2dFactory *f, REFCLSID clsid, ID2D1Properties **out)
{
    D2dFactoryEnter(f);
    CReg *r = reg_find(f, clsid, nullptr);
    if (r)
        InterlockedIncrement(&r->refs);
    D2dFactoryLeave(f);
    if (!r)
        return D2DERR_EFFECT_IS_NOT_REGISTERED;
    PropSet *s = ps_new(f);
    BOOL ok = s != nullptr;
    for (UINT32 i = 0; ok && i < r->nprops; i++) {
        const CProp *p = &r->props[i];
        PropSet *sub = prop_subprops(f, p);
        ok = sub && ps_add(s, i, p->name, p->type, p->def, p->def ? p->def_size : 0, sub);
    }
    ok = ok && ps_add(s, D2D1_PROPERTY_CLSID, L"Clsid", D2D1_PROPERTY_TYPE_CLSID, &r->clsid, 16, nullptr);
    for (int i = 0; ok && i < 4; i++)
        ok = ps_add_string(s, D2D1_PROPERTY_DISPLAYNAME + i, g_sys_names[1 + i], r->sys[i], nullptr);
    UINT32 cached = 0, precision = D2D1_BUFFER_PRECISION_UNKNOWN;
    ok = ok && ps_add(s, D2D1_PROPERTY_INPUTS, L"Inputs", D2D1_PROPERTY_TYPE_ARRAY, &r->ninputs, 4, inputs_subprops(f, r, r->ninputs));
    ok = ok && ps_add(s, D2D1_PROPERTY_CACHED, L"Cached", D2D1_PROPERTY_TYPE_BOOL, &cached, 4, nullptr);
    ok = ok && ps_add(s, D2D1_PROPERTY_PRECISION, L"Precision", D2D1_PROPERTY_TYPE_ENUM, &precision, 4, nullptr);
    ok = ok && ps_add(s, D2D1_PROPERTY_MIN_INPUTS, L"MinInputs", D2D1_PROPERTY_TYPE_UINT32, &r->min_inputs, 4, nullptr);
    ok = ok && ps_add(s, D2D1_PROPERTY_MAX_INPUTS, L"MaxInputs", D2D1_PROPERTY_TYPE_UINT32, &r->max_inputs, 4, nullptr);
    reg_release(r);
    if (!ok) {
        D2dReleaseObj(s);
        return E_OUTOFMEMORY;
    }
    *out = (ID2D1Properties *)s;
    return S_OK;
}

/* ------------------------------------------------------------------ resource textures and vertex buffers */

typedef struct CResTex {
    D2dObj obj;
    D2dGpuTexture *tex;
} CResTex;

typedef struct CVertexBuffer {
    D2dObj obj;
    BYTE *data;
    UINT32 size;
    BOOL mapped;
} CVertexBuffer;

static D2dClass g_restex_class, g_vb_class;

static HRESULT STDMETHODCALLTYPE rt_update(ID2D1ResourceTexture *This, const UINT32 *lo, const UINT32 *hi, const UINT32 *strides, UINT32 dims,
                                           const BYTE *data, UINT32 count)
{
    return D2dGpuTextureUpdate(((CResTex *)This)->tex, lo, hi, strides, dims, data, count);
}
static void restex_destroy(D2dObj *o) { D2dGpuTextureFree(((CResTex *)o)->tex); }
static const void *const g_restex_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, rt_update};
D2D_IIDS(g_restex_iids, &IID_ID2D1ResourceTexture);
static D2dClass g_restex_class = {"resource texture", g_restex_iids, restex_destroy, nullptr};

static HRESULT STDMETHODCALLTYPE vb_map(ID2D1VertexBuffer *This, BYTE **data, UINT32 size)
{
    CVertexBuffer *b = (CVertexBuffer *)This;
    if (!data)
        return E_INVALIDARG;
    *data = nullptr;
    if (b->mapped || size > b->size)
        return D2DERR_WRONG_STATE;
    b->mapped = TRUE;
    *data = b->data;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE vb_unmap(ID2D1VertexBuffer *This)
{
    CVertexBuffer *b = (CVertexBuffer *)This;
    if (!b->mapped)
        return D2DERR_WRONG_STATE;
    b->mapped = FALSE;
    return S_OK;
}
static void vb_destroy(D2dObj *o) { D2dFree(((CVertexBuffer *)o)->data); }
static const void *const g_vb_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, vb_map, vb_unmap};
D2D_IIDS(g_vb_iids, &IID_ID2D1VertexBuffer);
static D2dClass g_vb_class = {"vertex buffer", g_vb_iids, vb_destroy, nullptr};

/* resources made with an id are kept for the life of the process, as windows keeps them for its device. */
typedef struct NamedResource {
    GUID id;
    IUnknown *object;
} NamedResource;
static D2dArray g_named;
static CRITICAL_SECTION g_named_lock;
static LONG g_named_ready;

static void named_enter(void)
{
    if (InterlockedCompareExchange(&g_named_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_named_lock);
        InterlockedExchange(&g_named_ready, 2);
    }
    while (g_named_ready != 2)
        Sleep(0);
    EnterCriticalSection(&g_named_lock);
}

static IUnknown *named_find(const GUID *id, const D2dClass *cls)
{
    for (UINT32 i = 0; i < g_named.count; i++) {
        NamedResource *n = &((NamedResource *)g_named.data)[i];
        if (IsEqualGUID(&n->id, id) && D2dIsOfClass(n->object, cls)) {
            n->object->lpVtbl->AddRef(n->object);
            return n->object;
        }
    }
    return nullptr;
}

static void named_add(const GUID *id, IUnknown *object)
{
    if (D2dArrayGrow(&g_named, sizeof(NamedResource), 1)) {
        object->lpVtbl->AddRef(object);
        ((NamedResource *)g_named.data)[g_named.count++] = (NamedResource){*id, object};
    }
}

/* ------------------------------------------------------------------ render, draw and compute infos */

enum { INFO_RENDER, INFO_DRAW, INFO_COMPUTE };
#define MAX_SLOTS 32

typedef struct CInfo {
    D2dObj obj;
    int kind;
    UINT32 inputs;
    GUID shader;
    BOOL has_shader;
    BYTE *cb;
    UINT32 cb_size;
    BYTE *vcb;
    UINT32 vcb_size;
    ID2D1ResourceTexture *tex[MAX_SLOTS];
    UINT32 filter[MAX_SLOTS];
    UINT32 lod[MAX_SLOTS];
    D2D1_BUFFER_PRECISION precision;
    D2D1_CHANNEL_DEPTH depth;
    BOOL cached;
    UINT32 hint;
    D2D1_PIXEL_OPTIONS options;
    ID2D1VertexBuffer *vb;
    GUID vertex_shader;
    BOOL has_vertex_shader;
} CInfo;

static D2dClass g_draw_info_class, g_compute_info_class, g_render_info_class;
#define INFO CInfo *i = (CInfo *)This

static HRESULT STDMETHODCALLTYPE ri_input(ID2D1RenderInfo *This, UINT32 index, D2D1_INPUT_DESCRIPTION d)
{
    INFO;
    if (index >= i->inputs || index >= MAX_SLOTS)
        return E_INVALIDARG;
    i->filter[index] = d.filter;
    i->lod[index] = d.levelOfDetailCount;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ri_output(ID2D1RenderInfo *This, D2D1_BUFFER_PRECISION p, D2D1_CHANNEL_DEPTH d)
{
    INFO;
    if (p > D2D1_BUFFER_PRECISION_32BPC_FLOAT || (d != D2D1_CHANNEL_DEPTH_DEFAULT && d != D2D1_CHANNEL_DEPTH_1 && d != D2D1_CHANNEL_DEPTH_4))
        return E_INVALIDARG;
    i->precision = p;
    i->depth = d;
    return S_OK;
}
static void STDMETHODCALLTYPE ri_cached(ID2D1RenderInfo *This, BOOL cached) { INFO; i->cached = cached; }
static void STDMETHODCALLTYPE ri_hint(ID2D1RenderInfo *This, UINT32 count) { INFO; i->hint = count; }

static HRESULT set_buffer(BYTE **dst, UINT32 *dst_size, const BYTE *data, UINT32 size)
{
    if (size && !data)
        return E_INVALIDARG;
    BYTE *copy = size ? D2dAlloc(size) : nullptr;
    if (size && !copy)
        return E_OUTOFMEMORY;
    if (size)
        memcpy(copy, data, size);
    D2dFree(*dst);
    *dst = copy;
    *dst_size = size;
    return S_OK;
}
static HRESULT set_texture(CInfo *i, UINT32 index, ID2D1ResourceTexture *t)
{
    if (index >= MAX_SLOTS || (t && !D2dIsOfClass(t, &g_restex_class)))
        return E_INVALIDARG;
    D2D_ADDREF(t);
    D2D_RELEASE(i->tex[index]);
    i->tex[index] = t;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE di_ps_cb(ID2D1DrawInfo *This, const BYTE *data, UINT32 size) { INFO; return set_buffer(&i->cb, &i->cb_size, data, size); }
static HRESULT STDMETHODCALLTYPE di_texture(ID2D1DrawInfo *This, UINT32 index, ID2D1ResourceTexture *t) { INFO; return set_texture(i, index, t); }
static HRESULT STDMETHODCALLTYPE di_vs_cb(ID2D1DrawInfo *This, const BYTE *data, UINT32 size) { INFO; return set_buffer(&i->vcb, &i->vcb_size, data, size); }
static HRESULT STDMETHODCALLTYPE di_ps(ID2D1DrawInfo *This, REFGUID id, D2D1_PIXEL_OPTIONS options)
{
    INFO;
    if (!id)
        return E_INVALIDARG;
    i->shader = *id;
    i->has_shader = TRUE;
    i->options = options;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di_vertex(ID2D1DrawInfo *This, ID2D1VertexBuffer *vb, D2D1_VERTEX_OPTIONS options, const D2D1_BLEND_DESCRIPTION *blend,
                                          const D2D1_VERTEX_RANGE *range, const GUID *shader)
{
    INFO;
    if (vb && !D2dIsOfClass(vb, &g_vb_class))
        return E_INVALIDARG;
    D2D_ADDREF(vb);
    D2D_RELEASE(i->vb);
    i->vb = vb;
    i->has_vertex_shader = shader != nullptr;
    if (shader)
        i->vertex_shader = *shader;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ci_cb(ID2D1ComputeInfo *This, const BYTE *data, UINT32 size) { INFO; return set_buffer(&i->cb, &i->cb_size, data, size); }
static HRESULT STDMETHODCALLTYPE ci_cs(ID2D1ComputeInfo *This, REFGUID id)
{
    INFO;
    if (!id)
        return E_INVALIDARG;
    i->shader = *id;
    i->has_shader = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ci_texture(ID2D1ComputeInfo *This, UINT32 index, ID2D1ResourceTexture *t) { INFO; return set_texture(i, index, t); }

static void info_destroy(D2dObj *o)
{
    CInfo *i = (CInfo *)o;
    D2dFree(i->cb);
    D2dFree(i->vcb);
    for (UINT32 k = 0; k < MAX_SLOTS; k++)
        D2D_RELEASE(i->tex[k]);
    D2D_RELEASE(i->vb);
}

static const void *const g_render_info_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ri_input, ri_output, ri_cached, ri_hint};
static const void *const g_draw_info_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ri_input, ri_output, ri_cached, ri_hint,
                                               di_ps_cb, di_texture, di_vs_cb, di_ps, di_vertex};
static const void *const g_compute_info_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ri_input, ri_output, ri_cached, ri_hint,
                                                  ci_cb, ci_cs, ci_texture};
D2D_IIDS(g_render_info_iids, &IID_ID2D1RenderInfo);
D2D_IIDS(g_draw_info_iids, &IID_ID2D1RenderInfo, &IID_ID2D1DrawInfo);
D2D_IIDS(g_compute_info_iids, &IID_ID2D1RenderInfo, &IID_ID2D1ComputeInfo);
static D2dClass g_render_info_class = {"render info", g_render_info_iids, info_destroy, nullptr};
static D2dClass g_draw_info_class = {"draw info", g_draw_info_iids, info_destroy, nullptr};
static D2dClass g_compute_info_class = {"compute info", g_compute_info_iids, info_destroy, nullptr};

static CInfo *info_new(D2dFactory *f, int kind, UINT32 inputs)
{
    const void *vtbl = kind == INFO_DRAW ? (const void *)g_draw_info_vtbl : kind == INFO_COMPUTE ? (const void *)g_compute_info_vtbl : (const void *)g_render_info_vtbl;
    const D2dClass *cls = kind == INFO_DRAW ? &g_draw_info_class : kind == INFO_COMPUTE ? &g_compute_info_class : &g_render_info_class;
    CInfo *i = D2dNew(sizeof(CInfo), vtbl, cls, f);
    if (!i)
        return nullptr;
    i->kind = kind;
    i->inputs = inputs;
    for (UINT32 k = 0; k < MAX_SLOTS; k++)
        i->filter[k] = D2D1_FILTER_MIN_MAG_MIP_LINEAR;
    return i;
}

/* ------------------------------------------------------------------ the transforms this library makes */

typedef struct CBlend {
    D2dObj obj;
    UINT32 inputs;
    D2D1_BLEND_DESCRIPTION desc;
} CBlend;

typedef struct CBorder {
    D2dObj obj;
    D2D1_EXTEND_MODE x, y;
} CBorder;

typedef struct COffset {
    D2dObj obj;
    D2D1_POINT_2L offset;
} COffset;

typedef struct CBounds {
    D2dObj obj;
    D2D1_RECT_L bounds;
} CBounds;

typedef struct CEffectNode {
    D2dObj obj;
    ID2D1Effect *effect;
} CEffectNode;

static D2dClass g_blend_class, g_border_class, g_offset_class, g_bounds_class, g_effect_node_class;

static UINT32 STDMETHODCALLTYPE one_input(void *This) { return 1; }
static HRESULT STDMETHODCALLTYPE concrete_output(void *This, D2D1_BUFFER_PRECISION p, D2D1_CHANNEL_DEPTH d)
{
    return p > D2D1_BUFFER_PRECISION_32BPC_FLOAT ? E_INVALIDARG : S_OK;
}
static void STDMETHODCALLTYPE concrete_cached(void *This, BOOL cached) {}

static UINT32 STDMETHODCALLTYPE bl_inputs(ID2D1BlendTransform *This) { return ((CBlend *)This)->inputs; }
static void STDMETHODCALLTYPE bl_set(ID2D1BlendTransform *This, const D2D1_BLEND_DESCRIPTION *d)
{
    if (d)
        ((CBlend *)This)->desc = *d;
}
static void STDMETHODCALLTYPE bl_get(ID2D1BlendTransform *This, D2D1_BLEND_DESCRIPTION *d)
{
    if (d)
        *d = ((CBlend *)This)->desc;
}
static const void *const g_blend_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, bl_inputs, concrete_output, concrete_cached, bl_set, bl_get};
D2D_IIDS(g_blend_iids, &IID_ID2D1TransformNode, &IID_ID2D1ConcreteTransform, &IID_ID2D1BlendTransform);
static D2dClass g_blend_class = {"blend transform", g_blend_iids, nullptr, nullptr};

static void STDMETHODCALLTYPE bo_set_x(ID2D1BorderTransform *This, D2D1_EXTEND_MODE m) { ((CBorder *)This)->x = m; }
static void STDMETHODCALLTYPE bo_set_y(ID2D1BorderTransform *This, D2D1_EXTEND_MODE m) { ((CBorder *)This)->y = m; }
static D2D1_EXTEND_MODE STDMETHODCALLTYPE bo_get_x(ID2D1BorderTransform *This) { return ((CBorder *)This)->x; }
static D2D1_EXTEND_MODE STDMETHODCALLTYPE bo_get_y(ID2D1BorderTransform *This) { return ((CBorder *)This)->y; }
static const void *const g_border_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, one_input, concrete_output, concrete_cached,
                                            bo_set_x, bo_set_y, bo_get_x, bo_get_y};
D2D_IIDS(g_border_iids, &IID_ID2D1TransformNode, &IID_ID2D1ConcreteTransform, &IID_ID2D1BorderTransform);
static D2dClass g_border_class = {"border transform", g_border_iids, nullptr, nullptr};

static void STDMETHODCALLTYPE of_set(ID2D1OffsetTransform *This, D2D1_POINT_2L p) { ((COffset *)This)->offset = p; }
static D2D1_POINT_2L *STDMETHODCALLTYPE of_get(ID2D1OffsetTransform *This, D2D1_POINT_2L *ret)
{
    *ret = ((COffset *)This)->offset;
    return ret;
}
static const void *const g_offset_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, one_input, of_set, of_get};
D2D_IIDS(g_offset_iids, &IID_ID2D1TransformNode, &IID_ID2D1OffsetTransform);
static D2dClass g_offset_class = {"offset transform", g_offset_iids, nullptr, nullptr};

static void STDMETHODCALLTYPE ba_set(ID2D1BoundsAdjustmentTransform *This, const D2D1_RECT_L *r)
{
    if (r)
        ((CBounds *)This)->bounds = *r;
}
static void STDMETHODCALLTYPE ba_get(ID2D1BoundsAdjustmentTransform *This, D2D1_RECT_L *r)
{
    if (r)
        *r = ((CBounds *)This)->bounds;
}
static const void *const g_bounds_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, one_input, ba_set, ba_get};
D2D_IIDS(g_bounds_iids, &IID_ID2D1TransformNode, &IID_ID2D1BoundsAdjustmentTransform);
static D2dClass g_bounds_class = {"bounds adjustment transform", g_bounds_iids, nullptr, nullptr};

static UINT32 STDMETHODCALLTYPE en_inputs(ID2D1TransformNode *This)
{
    ID2D1Effect *e = ((CEffectNode *)This)->effect;
    return e->lpVtbl->GetInputCount(e);
}
static void effect_node_destroy(D2dObj *o) { D2D_RELEASE(((CEffectNode *)o)->effect); }
static const void *const g_effect_node_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, en_inputs};
D2D_IIDS(g_effect_node_iids, &IID_ID2D1TransformNode);
static D2dClass g_effect_node_class = {"effect transform node", g_effect_node_iids, effect_node_destroy, nullptr};

/* ------------------------------------------------------------------ transform graphs */

enum { NODE_DRAW, NODE_COMPUTE, NODE_SOURCE, NODE_BLEND, NODE_BORDER, NODE_OFFSET, NODE_BOUNDS, NODE_EFFECT, NODE_OTHER };
enum { CONN_NONE, CONN_EFFECT, CONN_NODE };

typedef struct Conn {
    int kind;
    UINT32 index;
} Conn;

typedef struct GNode {
    ID2D1TransformNode *node;
    int kind;
    /* the transform interface of nodes the effect wrote, for the mapping of rectangles. */
    ID2D1Transform *transform;
    /* the transform hands the results of its pass to this instead of making an image. */
    ID2D1AnalysisTransform *analysis;
    CInfo *info;
    UINT32 ninputs;
    Conn *in;
} GNode;

typedef struct CEffect CEffect;

typedef struct CGraph {
    D2dObj obj;
    CEffect *effect;
    GNode *nodes;
    UINT32 n;
    int output;
    int passthrough;
    UINT32 inputs;
} CGraph;

static D2dClass g_graph_class;
#define GRAPH CGraph *g = (CGraph *)This

static int graph_find(CGraph *g, ID2D1TransformNode *node)
{
    for (UINT32 i = 0; i < g->n; i++) {
        if (g->nodes[i].node == node)
            return (int)i;
    }
    return -1;
}

static void gnode_free(GNode *n)
{
    D2D_RELEASE(n->transform);
    D2D_RELEASE(n->analysis);
    D2dReleaseObj(n->info);
    D2D_RELEASE(n->node);
    D2dFree(n->in);
}

static UINT32 STDMETHODCALLTYPE gr_inputs(ID2D1TransformGraph *This) { GRAPH; return g->inputs; }

static HRESULT STDMETHODCALLTYPE gr_add(ID2D1TransformGraph *This, ID2D1TransformNode *node)
{
    GRAPH;
    if (!node)
        return E_INVALIDARG;
    if (graph_find(g, node) >= 0)
        return E_INVALIDARG;
    GNode n = {node, NODE_OTHER, nullptr, nullptr, nullptr, node->lpVtbl->GetInputCount(node), nullptr};
    if (D2dIsOfClass(node, &g_blend_class))
        n.kind = NODE_BLEND;
    else if (D2dIsOfClass(node, &g_border_class))
        n.kind = NODE_BORDER;
    else if (D2dIsOfClass(node, &g_offset_class))
        n.kind = NODE_OFFSET;
    else if (D2dIsOfClass(node, &g_bounds_class))
        n.kind = NODE_BOUNDS;
    else if (D2dIsOfClass(node, &g_effect_node_class))
        n.kind = NODE_EFFECT;
    HRESULT hr = S_OK;
    if (n.kind == NODE_OTHER) {
        void *iface = nullptr;
        D2dFactory *f = g->obj.factory;
        if (SUCCEEDED(node->lpVtbl->QueryInterface(node, &IID_ID2D1DrawTransform, &iface))) {
            n.kind = NODE_DRAW;
            n.info = info_new(f, INFO_DRAW, n.ninputs);
            hr = n.info ? ((ID2D1DrawTransform *)iface)->lpVtbl->SetDrawInfo(iface, (ID2D1DrawInfo *)n.info) : E_OUTOFMEMORY;
        } else if (SUCCEEDED(node->lpVtbl->QueryInterface(node, &IID_ID2D1ComputeTransform, &iface))) {
            n.kind = NODE_COMPUTE;
            n.info = info_new(f, INFO_COMPUTE, n.ninputs);
            hr = n.info ? ((ID2D1ComputeTransform *)iface)->lpVtbl->SetComputeInfo(iface, (ID2D1ComputeInfo *)n.info) : E_OUTOFMEMORY;
        } else if (SUCCEEDED(node->lpVtbl->QueryInterface(node, &IID_ID2D1SourceTransform, &iface))) {
            n.kind = NODE_SOURCE;
            n.info = info_new(f, INFO_RENDER, n.ninputs);
            hr = n.info ? ((ID2D1SourceTransform *)iface)->lpVtbl->SetRenderInfo(iface, (ID2D1RenderInfo *)n.info) : E_OUTOFMEMORY;
        } else {
            return E_INVALIDARG;
        }
        n.transform = iface;
        if (n.kind != NODE_SOURCE)
            node->lpVtbl->QueryInterface(node, &IID_ID2D1AnalysisTransform, (void **)&n.analysis);
    }
    if (SUCCEEDED(hr) && n.ninputs) {
        n.in = D2dAllocZero(n.ninputs * sizeof(Conn));
        hr = n.in ? S_OK : E_OUTOFMEMORY;
    }
    GNode *grown = SUCCEEDED(hr) ? D2dRealloc(g->nodes, (g->n + 1) * sizeof(GNode)) : nullptr;
    if (SUCCEEDED(hr) && !grown)
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        n.node = nullptr;
        gnode_free(&n);
        return hr;
    }
    node->lpVtbl->AddRef(node);
    g->nodes = grown;
    g->nodes[g->n++] = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gr_remove(ID2D1TransformGraph *This, ID2D1TransformNode *node)
{
    GRAPH;
    int at = graph_find(g, node);
    if (at < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    gnode_free(&g->nodes[at]);
    memmove(g->nodes + at, g->nodes + at + 1, (g->n - (UINT32)at - 1) * sizeof(GNode));
    g->n--;
    for (UINT32 i = 0; i < g->n; i++) {
        for (UINT32 k = 0; k < g->nodes[i].ninputs; k++) {
            Conn *c = &g->nodes[i].in[k];
            if (c->kind == CONN_NODE && c->index == (UINT32)at)
                c->kind = CONN_NONE;
            else if (c->kind == CONN_NODE && c->index > (UINT32)at)
                c->index--;
        }
    }
    if (g->output == at)
        g->output = -1;
    else if (g->output > at)
        g->output--;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gr_output(ID2D1TransformGraph *This, ID2D1TransformNode *node)
{
    GRAPH;
    int at = graph_find(g, node);
    if (at < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    g->output = at;
    g->passthrough = -1;
    return S_OK;
}

/* the node of an effect has the inputs the effect has when it is connected, which may have changed since it was added. */
static HRESULT gnode_sync_inputs(GNode *n)
{
    if (n->kind != NODE_EFFECT)
        return S_OK;
    UINT32 count = n->node->lpVtbl->GetInputCount(n->node);
    if (count <= n->ninputs)
        return S_OK;
    Conn *in = D2dRealloc(n->in, count * sizeof(Conn));
    if (!in)
        return E_OUTOFMEMORY;
    memset(in + n->ninputs, 0, (count - n->ninputs) * sizeof(Conn));
    n->in = in;
    n->ninputs = count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gr_connect(ID2D1TransformGraph *This, ID2D1TransformNode *from, ID2D1TransformNode *to, UINT32 input)
{
    GRAPH;
    int a = graph_find(g, from), b = graph_find(g, to);
    if (a < 0 || b < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    HRESULT hr = gnode_sync_inputs(&g->nodes[b]);
    if (FAILED(hr))
        return hr;
    if (input >= g->nodes[b].ninputs)
        return E_INVALIDARG;
    g->nodes[b].in[input] = (Conn){CONN_NODE, (UINT32)a};
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gr_connect_input(ID2D1TransformGraph *This, UINT32 effect_input, ID2D1TransformNode *node, UINT32 input)
{
    GRAPH;
    int b = graph_find(g, node);
    if (b < 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    HRESULT hr = gnode_sync_inputs(&g->nodes[b]);
    if (FAILED(hr))
        return hr;
    if (input >= g->nodes[b].ninputs || effect_input >= g->inputs)
        return E_INVALIDARG;
    g->nodes[b].in[input] = (Conn){CONN_EFFECT, effect_input};
    return S_OK;
}

static void STDMETHODCALLTYPE gr_clear(ID2D1TransformGraph *This)
{
    GRAPH;
    for (UINT32 i = 0; i < g->n; i++)
        gnode_free(&g->nodes[i]);
    D2dFree(g->nodes);
    g->nodes = nullptr;
    g->n = 0;
    g->output = -1;
    g->passthrough = -1;
}

static HRESULT STDMETHODCALLTYPE gr_single(ID2D1TransformGraph *This, ID2D1TransformNode *node)
{
    GRAPH;
    if (!node)
        return E_INVALIDARG;
    gr_clear(This);
    HRESULT hr = gr_add(This, node);
    if (FAILED(hr))
        return hr;
    GNode *n = &g->nodes[0];
    for (UINT32 i = 0; i < n->ninputs && i < g->inputs; i++)
        n->in[i] = (Conn){CONN_EFFECT, i};
    g->output = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gr_passthrough(ID2D1TransformGraph *This, UINT32 input)
{
    GRAPH;
    if (input >= g->inputs)
        return E_INVALIDARG;
    gr_clear(This);
    g->passthrough = (int)input;
    return S_OK;
}

static void graph_destroy(D2dObj *o) { gr_clear((ID2D1TransformGraph *)o); }
static const void *const g_graph_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, gr_inputs, gr_single, gr_add, gr_remove,
                                           gr_output, gr_connect, gr_connect_input, gr_clear, gr_passthrough};
D2D_IIDS(g_graph_iids, &IID_ID2D1TransformGraph);
static D2dClass g_graph_class = {"transform graph", g_graph_iids, graph_destroy, nullptr};

/* ------------------------------------------------------------------ effect contexts */

typedef struct CContext {
    D2dObj obj;
    float dpi_x, dpi_y;
} CContext;

static D2dClass g_context_class;
#define CTX CContext *c = (CContext *)This

static void STDMETHODCALLTYPE cx_dpi(ID2D1EffectContext *This, FLOAT *x, FLOAT *y)
{
    CTX;
    if (x)
        *x = c->dpi_x;
    if (y)
        *y = c->dpi_y;
}
static HRESULT STDMETHODCALLTYPE cx_effect(ID2D1EffectContext *This, REFCLSID clsid, ID2D1Effect **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!clsid)
        return E_INVALIDARG;
    return D2dCreateEffectF(c->obj.factory, c->dpi_x, clsid, out);
}
static HRESULT STDMETHODCALLTYPE cx_level(ID2D1EffectContext *This, const D3D_FEATURE_LEVEL *levels, UINT32 count, D3D_FEATURE_LEVEL *level)
{
    if (!levels || !level)
        return E_INVALIDARG;
    D3D_FEATURE_LEVEL best = (D3D_FEATURE_LEVEL)0;
    for (UINT32 i = 0; i < count; i++) {
        if (levels[i] <= D3D_FEATURE_LEVEL_11_1 && levels[i] > best)
            best = levels[i];
    }
    if (!best)
        return D2DERR_INSUFFICIENT_DEVICE_CAPABILITIES;
    *level = best;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_node(ID2D1EffectContext *This, ID2D1Effect *effect, ID2D1TransformNode **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!D2dIsEffect(effect))
        return E_INVALIDARG;
    CEffectNode *n = D2dNew(sizeof(*n), g_effect_node_vtbl, &g_effect_node_class, c->obj.factory);
    if (!n)
        return E_OUTOFMEMORY;
    n->effect = effect;
    effect->lpVtbl->AddRef(effect);
    *out = (ID2D1TransformNode *)n;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_blend(ID2D1EffectContext *This, UINT32 inputs, const D2D1_BLEND_DESCRIPTION *d, ID2D1BlendTransform **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!d || !inputs)
        return E_INVALIDARG;
    CBlend *b = D2dNew(sizeof(*b), g_blend_vtbl, &g_blend_class, c->obj.factory);
    if (!b)
        return E_OUTOFMEMORY;
    b->inputs = inputs;
    b->desc = *d;
    *out = (ID2D1BlendTransform *)b;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_border(ID2D1EffectContext *This, D2D1_EXTEND_MODE x, D2D1_EXTEND_MODE y, ID2D1BorderTransform **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (x > D2D1_EXTEND_MODE_MIRROR || y > D2D1_EXTEND_MODE_MIRROR)
        return E_INVALIDARG;
    CBorder *b = D2dNew(sizeof(*b), g_border_vtbl, &g_border_class, c->obj.factory);
    if (!b)
        return E_OUTOFMEMORY;
    b->x = x;
    b->y = y;
    *out = (ID2D1BorderTransform *)b;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_offset(ID2D1EffectContext *This, D2D1_POINT_2L offset, ID2D1OffsetTransform **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    COffset *o = D2dNew(sizeof(*o), g_offset_vtbl, &g_offset_class, c->obj.factory);
    *out = (ID2D1OffsetTransform *)o;
    if (!o)
        return E_OUTOFMEMORY;
    o->offset = offset;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_bounds(ID2D1EffectContext *This, const D2D1_RECT_L *r, ID2D1BoundsAdjustmentTransform **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!r)
        return E_INVALIDARG;
    CBounds *b = D2dNew(sizeof(*b), g_bounds_vtbl, &g_bounds_class, c->obj.factory);
    if (!b)
        return E_OUTOFMEMORY;
    b->bounds = *r;
    *out = (ID2D1BoundsAdjustmentTransform *)b;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_load_ps(ID2D1EffectContext *This, REFGUID id, const BYTE *code, UINT32 size)
{
    return id ? D2dGpuLoadShader(id, D2D_SHADER_PIXEL, code, size) : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE cx_load_vs(ID2D1EffectContext *This, REFGUID id, const BYTE *code, UINT32 size)
{
    return id ? D2dGpuLoadShader(id, D2D_SHADER_VERTEX, code, size) : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE cx_load_cs(ID2D1EffectContext *This, REFGUID id, const BYTE *code, UINT32 size)
{
    return id ? D2dGpuLoadShader(id, D2D_SHADER_COMPUTE, code, size) : E_INVALIDARG;
}
static BOOL STDMETHODCALLTYPE cx_loaded(ID2D1EffectContext *This, REFGUID id) { return id && D2dGpuShaderLoaded(id); }
static HRESULT STDMETHODCALLTYPE cx_texture(ID2D1EffectContext *This, const GUID *id, const D2D1_RESOURCE_TEXTURE_PROPERTIES *p, const BYTE *data,
                                            const UINT32 *strides, UINT32 size, ID2D1ResourceTexture **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (id) {
        named_enter();
        IUnknown *found = named_find(id, &g_restex_class);
        LeaveCriticalSection(&g_named_lock);
        if (found) {
            *out = (ID2D1ResourceTexture *)found;
            return S_OK;
        }
    }
    CResTex *t = D2dNew(sizeof(*t), g_restex_vtbl, &g_restex_class, c->obj.factory);
    if (!t)
        return E_OUTOFMEMORY;
    HRESULT hr = D2dGpuTextureCreate(p, data, strides, size, &t->tex);
    if (FAILED(hr)) {
        D2dRelease(t);
        return hr;
    }
    if (id) {
        named_enter();
        named_add(id, (IUnknown *)t);
        LeaveCriticalSection(&g_named_lock);
    }
    *out = (ID2D1ResourceTexture *)t;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_find_texture(ID2D1EffectContext *This, const GUID *id, ID2D1ResourceTexture **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!id)
        return E_INVALIDARG;
    named_enter();
    *out = (ID2D1ResourceTexture *)named_find(id, &g_restex_class);
    LeaveCriticalSection(&g_named_lock);
    return *out ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
static HRESULT STDMETHODCALLTYPE cx_vb(ID2D1EffectContext *This, const D2D1_VERTEX_BUFFER_PROPERTIES *p, const GUID *id,
                                       const D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES *custom, ID2D1VertexBuffer **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!p || !p->byteWidth)
        return E_INVALIDARG;
    if (id) {
        named_enter();
        IUnknown *found = named_find(id, &g_vb_class);
        LeaveCriticalSection(&g_named_lock);
        if (found) {
            *out = (ID2D1VertexBuffer *)found;
            return S_OK;
        }
    }
    CVertexBuffer *b = D2dNew(sizeof(*b), g_vb_vtbl, &g_vb_class, c->obj.factory);
    if (!b)
        return E_OUTOFMEMORY;
    b->size = p->byteWidth;
    b->data = D2dAllocZero(b->size);
    if (!b->data) {
        D2dRelease(b);
        return E_OUTOFMEMORY;
    }
    if (p->data)
        memcpy(b->data, p->data, b->size);
    if (id) {
        named_enter();
        named_add(id, (IUnknown *)b);
        LeaveCriticalSection(&g_named_lock);
    }
    *out = (ID2D1VertexBuffer *)b;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cx_find_vb(ID2D1EffectContext *This, const GUID *id, ID2D1VertexBuffer **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!id)
        return E_INVALIDARG;
    named_enter();
    *out = (ID2D1VertexBuffer *)named_find(id, &g_vb_class);
    LeaveCriticalSection(&g_named_lock);
    return *out ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
static HRESULT STDMETHODCALLTYPE cx_color(ID2D1EffectContext *This, D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 size, ID2D1ColorContext **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return D2dCreateColorContext(c->obj.factory, space, profile, size, out);
}
static HRESULT STDMETHODCALLTYPE cx_color_file(ID2D1EffectContext *This, const WCHAR *path, ID2D1ColorContext **out)
{
    CTX;
    return D2dColorContextFromFile(c->obj.factory, path, out);
}
static HRESULT STDMETHODCALLTYPE cx_color_wic(ID2D1EffectContext *This, IWICColorContext *wic, ID2D1ColorContext **out)
{
    CTX;
    return D2dColorContextFromWic(c->obj.factory, wic, out);
}
static HRESULT STDMETHODCALLTYPE cx_feature(ID2D1EffectContext *This, D2D1_FEATURE feature, void *data, UINT32 size)
{
    if (!data || size != sizeof(BOOL))
        return E_INVALIDARG;
    switch (feature) {
    case D2D1_FEATURE_DOUBLES:
        *(BOOL *)data = TRUE;
        return S_OK;
    case D2D1_FEATURE_D3D10_X_HARDWARE_OPTIONS:
        *(BOOL *)data = TRUE;
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}
static BOOL STDMETHODCALLTYPE cx_precision(ID2D1EffectContext *This, D2D1_BUFFER_PRECISION p)
{
    return p >= D2D1_BUFFER_PRECISION_8BPC_UNORM && p <= D2D1_BUFFER_PRECISION_32BPC_FLOAT;
}
static HRESULT STDMETHODCALLTYPE cx_lut(ID2D1EffectContext1 *This, D2D1_BUFFER_PRECISION p, const UINT32 *extents, const BYTE *data, UINT32 count,
                                        const UINT32 *strides, ID2D1LookupTable3D **out)
{
    CTX;
    return D2dLut3DCreate(c->obj.factory, p, extents, data, count, strides, out);
}
static HRESULT STDMETHODCALLTYPE cx_color_dxgi(ID2D1EffectContext2 *This, DXGI_COLOR_SPACE_TYPE space, ID2D1ColorContext1 **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return D2dCreateColorContextDxgi(c->obj.factory, space, (ID2D1ColorContext **)out);
}
static HRESULT STDMETHODCALLTYPE cx_color_simple(ID2D1EffectContext2 *This, const void *profile, ID2D1ColorContext1 **out)
{
    CTX;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!profile)
        return E_INVALIDARG;
    return D2dCreateColorContextSimple(c->obj.factory, profile, (ID2D1ColorContext **)out);
}

static const void *const g_context_vtbl[] = {
    D2dQueryInterface, D2dAddRef,     D2dRelease,     cx_dpi,      cx_effect,   cx_level,      cx_node,       cx_blend,
    cx_border,         cx_offset,     cx_bounds,      cx_load_ps,  cx_load_vs,  cx_load_cs,    cx_loaded,     cx_texture,
    cx_find_texture,   cx_vb,         cx_find_vb,     cx_color,    cx_color_file, cx_color_wic, cx_feature,   cx_precision,
    cx_lut,            cx_color_dxgi, cx_color_simple};
D2D_IIDS(g_context_iids, &IID_ID2D1EffectContext, &IID_ID2D1EffectContext1, &IID_ID2D1EffectContext2);
static D2dClass g_context_class = {"effect context", g_context_iids, nullptr, nullptr};

/* ------------------------------------------------------------------ custom effects */

struct CEffect {
    D2dObj obj;
    const void *image_vtbl;
    CReg *reg;
    IUnknown *impl_unknown;
    ID2D1EffectImpl *impl;
    CContext *context;
    CGraph *graph;
    /* values of the properties without a binding. */
    BYTE **values;
    UINT32 *sizes;
    ID2D1Image **inputs;
    UINT32 ninputs;
    BOOL cached;
    D2D1_BUFFER_PRECISION precision;
    BOOL prepare;
    D2D1_CHANGE_TYPE change;
    LONG busy;
};

static D2dClass g_custom_class;
#define CE CEffect *e = (CEffect *)This

void *D2dCustomOf(const void *image)
{
    if (D2dIsOfClass(image, &g_custom_class))
        return (void *)image;
    void *e = D2dEffectFromImage(image);
    return D2dIsOfClass(e, &g_custom_class) ? e : nullptr;
}

static void mark_changed(CEffect *e, D2D1_CHANGE_TYPE change)
{
    e->prepare = TRUE;
    if (change > e->change)
        e->change = change;
}

static UINT32 STDMETHODCALLTYPE ce_count(ID2D1Effect *This) { CE; return e->reg->nprops; }

static const WCHAR *prop_name(CEffect *e, UINT32 index)
{
    if (index < e->reg->nprops)
        return e->reg->props[index].name;
    if (index >= SYS_FIRST && index - SYS_FIRST < SYS_COUNT)
        return g_sys_names[index - SYS_FIRST];
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE ce_name(ID2D1Effect *This, UINT32 index, WCHAR *name, UINT32 count)
{
    CE;
    const WCHAR *n = prop_name(e, index);
    if (!n)
        return D2DERR_INVALID_PROPERTY;
    UINT32 len = (UINT32)wcslen(n);
    if (!name || count < len + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(name, n, (len + 1) * sizeof(WCHAR));
    return S_OK;
}
static UINT32 STDMETHODCALLTYPE ce_name_length(ID2D1Effect *This, UINT32 index)
{
    CE;
    const WCHAR *n = prop_name(e, index);
    return n ? (UINT32)wcslen(n) : 0;
}

static D2D1_PROPERTY_TYPE sys_type(UINT32 i)
{
    switch (i) {
    case D2D1_PROPERTY_CLSID: return D2D1_PROPERTY_TYPE_CLSID;
    case D2D1_PROPERTY_DISPLAYNAME:
    case D2D1_PROPERTY_AUTHOR:
    case D2D1_PROPERTY_CATEGORY:
    case D2D1_PROPERTY_DESCRIPTION: return D2D1_PROPERTY_TYPE_STRING;
    case D2D1_PROPERTY_INPUTS: return D2D1_PROPERTY_TYPE_ARRAY;
    case D2D1_PROPERTY_CACHED: return D2D1_PROPERTY_TYPE_BOOL;
    case D2D1_PROPERTY_PRECISION: return D2D1_PROPERTY_TYPE_ENUM;
    case D2D1_PROPERTY_MIN_INPUTS:
    case D2D1_PROPERTY_MAX_INPUTS: return D2D1_PROPERTY_TYPE_UINT32;
    default: return D2D1_PROPERTY_TYPE_UNKNOWN;
    }
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE ce_type(ID2D1Effect *This, UINT32 index)
{
    CE;
    if (index >= SYS_FIRST)
        return sys_type(index);
    return index < e->reg->nprops ? e->reg->props[index].type : D2D1_PROPERTY_TYPE_UNKNOWN;
}

static UINT32 STDMETHODCALLTYPE ce_index(ID2D1Effect *This, const WCHAR *name)
{
    CE;
    if (!name)
        return 0xffffffffu;
    for (UINT32 i = 0; i < e->reg->nprops; i++) {
        if (wcscmp(e->reg->props[i].name, name) == 0)
            return i;
    }
    for (UINT32 i = 0; i < SYS_COUNT; i++) {
        if (wcscmp(g_sys_names[i], name) == 0)
            return SYS_FIRST + i;
    }
    return 0xffffffffu;
}

/* a system value: what it is and how many bytes it takes. */
static void sys_value(CEffect *e, UINT32 index, const void **data, UINT32 *size, UINT32 *scratch)
{
    *data = nullptr;
    *size = 0;
    switch (index) {
    case D2D1_PROPERTY_CLSID:
        *data = &e->reg->clsid;
        *size = 16;
        return;
    case D2D1_PROPERTY_DISPLAYNAME:
    case D2D1_PROPERTY_AUTHOR:
    case D2D1_PROPERTY_CATEGORY:
    case D2D1_PROPERTY_DESCRIPTION: {
        const WCHAR *s = e->reg->sys[index - D2D1_PROPERTY_DISPLAYNAME];
        s = s ? s : L"";
        *data = s;
        *size = (UINT32)(wcslen(s) + 1) * sizeof(WCHAR);
        return;
    }
    case D2D1_PROPERTY_INPUTS:
        *scratch = e->ninputs;
        break;
    case D2D1_PROPERTY_CACHED:
        *scratch = (UINT32)e->cached;
        break;
    case D2D1_PROPERTY_PRECISION:
        *scratch = e->precision;
        break;
    case D2D1_PROPERTY_MIN_INPUTS:
        *scratch = e->reg->min_inputs;
        break;
    case D2D1_PROPERTY_MAX_INPUTS:
        *scratch = e->reg->max_inputs;
        break;
    default:
        return;
    }
    *data = scratch;
    *size = 4;
}

static BOOL type_matches(D2D1_PROPERTY_TYPE want, D2D1_PROPERTY_TYPE have)
{
    return want == D2D1_PROPERTY_TYPE_UNKNOWN || want == have ||
           (have == D2D1_PROPERTY_TYPE_ENUM && want == D2D1_PROPERTY_TYPE_UINT32);
}

static HRESULT set_value(CEffect *e, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size, BOOL from_default)
{
    if (index >= SYS_FIRST) {
        if (!data || size != 4)
            return E_INVALIDARG;
        if (index == (UINT32)D2D1_PROPERTY_CACHED && type_matches(type, D2D1_PROPERTY_TYPE_BOOL)) {
            e->cached = *(const BOOL *)data != 0;
            return S_OK;
        }
        if (index == (UINT32)D2D1_PROPERTY_PRECISION && type_matches(type, D2D1_PROPERTY_TYPE_ENUM)) {
            UINT32 p = *(const UINT32 *)data;
            if (p > D2D1_BUFFER_PRECISION_32BPC_FLOAT)
                return E_INVALIDARG;
            e->precision = p;
            mark_changed(e, D2D1_CHANGE_TYPE_PROPERTIES);
            return S_OK;
        }
        return sys_type(index) ? D2DERR_INVALID_CALL : D2DERR_INVALID_PROPERTY;
    }
    if (index >= e->reg->nprops)
        return D2DERR_INVALID_PROPERTY;
    const CProp *p = &e->reg->props[index];
    if (!type_matches(type, p->type))
        return E_INVALIDARG;
    UINT32 fixed = type_size(p->type);
    if ((fixed && size != fixed) || (!data && size))
        return E_INVALIDARG;
    if (!from_default && p->readonly)
        return D2DERR_INVALID_CALL;
    HRESULT hr;
    if (p->get) {
        if (!p->set)
            return D2DERR_INVALID_CALL;
        hr = p->set(e->impl_unknown, data, size);
    } else {
        BYTE *copy = size ? D2dAlloc(size) : nullptr;
        if (size && !copy)
            return E_OUTOFMEMORY;
        if (size)
            memcpy(copy, data, size);
        if (is_interface(p->type) && size == sizeof(void *)) {
            IUnknown *now = *(IUnknown **)copy;
            D2D_ADDREF(now);
            if (e->values[index] && e->sizes[index] == sizeof(void *)) {
                IUnknown *old = *(IUnknown **)e->values[index];
                D2D_RELEASE(old);
            }
        }
        D2dFree(e->values[index]);
        e->values[index] = copy;
        e->sizes[index] = size;
        hr = S_OK;
    }
    if (SUCCEEDED(hr))
        mark_changed(e, D2D1_CHANGE_TYPE_PROPERTIES);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ce_set(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    CE;
    return set_value(e, index, type, data, size, FALSE);
}
static HRESULT STDMETHODCALLTYPE ce_set_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *data, UINT32 size)
{
    UINT32 i = ce_index(This, name);
    return i == 0xffffffffu ? D2DERR_INVALID_PROPERTY : ce_set(This, i, type, data, size);
}

/* the size of a value now, for GetValueSize and the checks of GetValue. */
static UINT32 value_size(CEffect *e, UINT32 index)
{
    if (index >= SYS_FIRST) {
        const void *data;
        UINT32 size, scratch;
        sys_value(e, index, &data, &size, &scratch);
        return size;
    }
    if (index >= e->reg->nprops)
        return 0;
    const CProp *p = &e->reg->props[index];
    UINT32 fixed = type_size(p->type);
    if (fixed)
        return fixed;
    if (p->get) {
        UINT32 actual = 0;
        p->get(e->impl_unknown, nullptr, 0, &actual);
        return actual;
    }
    return e->sizes[index];
}

static HRESULT STDMETHODCALLTYPE ce_get(ID2D1Effect *This, UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    CE;
    if (!data && size)
        return E_INVALIDARG;
    if (index >= SYS_FIRST) {
        const void *v;
        UINT32 n, scratch;
        sys_value(e, index, &v, &n, &scratch);
        if (!v)
            return D2DERR_INVALID_PROPERTY;
        if (!type_matches(type, sys_type(index)))
            return E_INVALIDARG;
        if (size < n)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        memcpy(data, v, n);
        return S_OK;
    }
    if (index >= e->reg->nprops)
        return D2DERR_INVALID_PROPERTY;
    const CProp *p = &e->reg->props[index];
    if (!type_matches(type, p->type))
        return E_INVALIDARG;
    UINT32 fixed = type_size(p->type);
    if (fixed && size < fixed)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (p->get) {
        UINT32 actual = 0;
        return p->get(e->impl_unknown, data, fixed ? fixed : size, &actual);
    }
    UINT32 n = e->sizes[index];
    if (!fixed && size < n)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (fixed && !e->values[index]) {
        memset(data, 0, fixed);
        return S_OK;
    }
    if (n)
        memcpy(data, e->values[index], n);
    if (is_interface(p->type) && n == sizeof(void *)) {
        IUnknown *u = *(IUnknown **)data;
        D2D_ADDREF(u);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ce_get_by_name(ID2D1Effect *This, const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size)
{
    UINT32 i = ce_index(This, name);
    return i == 0xffffffffu ? D2DERR_INVALID_PROPERTY : ce_get(This, i, type, data, size);
}
static UINT32 STDMETHODCALLTYPE ce_value_size(ID2D1Effect *This, UINT32 index) { CE; return value_size(e, index); }

static HRESULT STDMETHODCALLTYPE ce_subprops(ID2D1Effect *This, UINT32 index, ID2D1Properties **out)
{
    CE;
    if (!out)
        return E_POINTER;
    *out = nullptr;
    PropSet *s = nullptr;
    if (index < e->reg->nprops)
        s = prop_subprops(e->obj.factory, &e->reg->props[index]);
    else if (index == (UINT32)D2D1_PROPERTY_INPUTS)
        s = inputs_subprops(e->obj.factory, e->reg, e->ninputs);
    else if (index >= SYS_FIRST && index - SYS_FIRST < SYS_COUNT)
        return D2DERR_NO_SUBPROPERTIES;
    else
        return D2DERR_INVALID_PROPERTY;
    if (!s)
        return E_OUTOFMEMORY;
    *out = (ID2D1Properties *)s;
    return S_OK;
}

static void STDMETHODCALLTYPE ce_set_input(ID2D1Effect *This, UINT32 index, ID2D1Image *input, BOOL invalidate)
{
    CE;
    if (index >= e->ninputs)
        return;
    D2D_ADDREF(input);
    D2D_RELEASE(e->inputs[index]);
    e->inputs[index] = input;
}

static HRESULT STDMETHODCALLTYPE ce_set_input_count(ID2D1Effect *This, UINT32 count)
{
    CE;
    if (count < e->reg->min_inputs || count > e->reg->max_inputs)
        return E_INVALIDARG;
    if (count == e->ninputs)
        return S_OK;
    ID2D1Image **inputs = D2dAllocZero((count ? count : 1) * sizeof(ID2D1Image *));
    if (!inputs)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < e->ninputs; i++) {
        if (i < count)
            inputs[i] = e->inputs[i];
        else
            D2D_RELEASE(e->inputs[i]);
    }
    D2dFree(e->inputs);
    e->inputs = inputs;
    UINT32 old = e->ninputs;
    e->ninputs = count;
    e->graph->inputs = count;
    HRESULT hr = e->impl->lpVtbl->SetGraph(e->impl, (ID2D1TransformGraph *)e->graph);
    if (FAILED(hr)) {
        /* the effect keeps its inputs when it cannot take the new count. */
        e->graph->inputs = old;
        e->ninputs = old < count ? old : count;
        return hr;
    }
    mark_changed(e, D2D1_CHANGE_TYPE_GRAPH);
    return S_OK;
}

static void STDMETHODCALLTYPE ce_get_input(ID2D1Effect *This, UINT32 index, ID2D1Image **out)
{
    CE;
    if (!out)
        return;
    *out = index < e->ninputs ? e->inputs[index] : nullptr;
    D2D_ADDREF(*out);
}
static UINT32 STDMETHODCALLTYPE ce_get_input_count(ID2D1Effect *This) { CE; return e->ninputs; }

static void STDMETHODCALLTYPE ce_get_output(ID2D1Effect *This, ID2D1Image **out)
{
    CE;
    if (out)
        D2dEffectQueryImage(&e->obj, &IID_ID2D1Image, (void **)out);
}

static void custom_destroy(D2dObj *o)
{
    CEffect *e = (CEffect *)o;
    if (e->graph) {
        gr_clear((ID2D1TransformGraph *)e->graph);
        e->graph->effect = nullptr;
    }
    D2D_RELEASE(e->impl);
    D2D_RELEASE(e->impl_unknown);
    D2dReleaseObj(e->graph);
    D2dReleaseObj(e->context);
    for (UINT32 i = 0; i < e->ninputs; i++)
        D2D_RELEASE(e->inputs[i]);
    D2dFree(e->inputs);
    for (UINT32 i = 0; e->values && i < e->reg->nprops; i++) {
        if (is_interface(e->reg->props[i].type) && e->values[i] && e->sizes[i] == sizeof(void *)) {
            IUnknown *u = *(IUnknown **)e->values[i];
            D2D_RELEASE(u);
        }
        D2dFree(e->values[i]);
    }
    D2dFree(e->values);
    D2dFree(e->sizes);
    reg_release(e->reg);
}

static const void *const g_custom_vtbl[] = {D2dQueryInterface, D2dAddRef, D2dRelease, ce_count, ce_name, ce_name_length,
                                            ce_type, ce_index, ce_set_by_name, ce_set, ce_get_by_name, ce_get, ce_value_size,
                                            ce_subprops, ce_set_input, ce_set_input_count, ce_get_input, ce_get_input_count, ce_get_output};
D2D_IIDS(g_custom_iids, &IID_ID2D1Properties, &IID_ID2D1Effect);
static D2dClass g_custom_class = {"custom effect", g_custom_iids, custom_destroy, D2dEffectQueryImage};

HRESULT D2dCustomCreate(D2dFactory *f, float dpi, REFCLSID clsid, ID2D1Effect **out)
{
    D2dFactoryEnter(f);
    CReg *r = reg_find(f, clsid, nullptr);
    if (r)
        InterlockedIncrement(&r->refs);
    D2dFactoryLeave(f);
    if (!r)
        return D2DERR_EFFECT_IS_NOT_REGISTERED;
    CEffect *e = D2dNew(sizeof(*e), g_custom_vtbl, &g_custom_class, f);
    if (!e) {
        reg_release(r);
        return E_OUTOFMEMORY;
    }
    e->image_vtbl = d2d_effect_image_vtbl;
    e->reg = r;
    e->ninputs = r->ninputs;
    e->inputs = D2dAllocZero((r->ninputs ? r->ninputs : 1) * sizeof(ID2D1Image *));
    e->values = D2dAllocZero((r->nprops ? r->nprops : 1) * sizeof(BYTE *));
    e->sizes = D2dAllocZero((r->nprops ? r->nprops : 1) * sizeof(UINT32));
    e->context = D2dNew(sizeof(CContext), g_context_vtbl, &g_context_class, f);
    e->graph = D2dNew(sizeof(CGraph), g_graph_vtbl, &g_graph_class, f);
    HRESULT hr = e->inputs && e->values && e->sizes && e->context && e->graph ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        e->context->dpi_x = e->context->dpi_y = dpi > 0.0f ? dpi : 96.0f;
        e->graph->effect = e;
        e->graph->inputs = e->ninputs;
        e->graph->output = -1;
        e->graph->passthrough = -1;
        hr = r->factory(&e->impl_unknown);
    }
    if (SUCCEEDED(hr) && !e->impl_unknown)
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = e->impl_unknown->lpVtbl->QueryInterface(e->impl_unknown, &IID_ID2D1EffectImpl, (void **)&e->impl);
    /*
     * the defaults are the first values of the properties direct2d keeps. windows calls no setter of a bound property
     * when it makes an effect: Paint.NET's setters fail before Initialize, and its device feature probe keeps what it
     * sets there.
     */
    for (UINT32 i = 0; SUCCEEDED(hr) && i < r->nprops; i++) {
        const CProp *p = &r->props[i];
        if (p->def && !p->get)
            hr = set_value(e, i, p->type, p->def, p->def_size, TRUE);
    }
    if (SUCCEEDED(hr))
        hr = e->impl->lpVtbl->Initialize(e->impl, (ID2D1EffectContext *)e->context, (ID2D1TransformGraph *)e->graph);
    if (FAILED(hr)) {
        D2dRelease(e);
        return hr;
    }
    e->prepare = TRUE;
    e->change = D2D1_CHANGE_TYPE_NONE;
    *out = (ID2D1Effect *)e;
    return S_OK;
}

/* ------------------------------------------------------------------ rendering */

static LONG to_l(int v) { return v <= -D2D_INF_EXTENT ? (LONG)0x80000000 : v >= D2D_INF_EXTENT ? 0x7fffffff : v; }
static int from_l(LONG v) { return v <= -D2D_INF_EXTENT ? -D2D_INF_EXTENT : v >= D2D_INF_EXTENT ? D2D_INF_EXTENT : (int)v; }
static D2D1_RECT_L rect_l(D2dIRect r) { return (D2D1_RECT_L){to_l(r.x0), to_l(r.y0), to_l(r.x1), to_l(r.y1)}; }
static D2dIRect rect_i(D2D1_RECT_L r) { return (D2dIRect){from_l(r.left), from_l(r.top), from_l(r.right), from_l(r.bottom)}; }
static BOOL rect_empty(D2dIRect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
static D2dIRect rect_and(D2dIRect a, D2dIRect b)
{
    D2dIRect r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1};
    if (rect_empty(r))
        r.x1 = r.x0, r.y1 = r.y0;
    return r;
}
static D2dIRect rect_or(D2dIRect a, D2dIRect b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    return (D2dIRect){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}
static const D2dIRect g_infinite = {-D2D_INF_EXTENT, -D2D_INF_EXTENT, D2D_INF_EXTENT, D2D_INF_EXTENT};

static HRESULT empty_data(D2dIRect r, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    out->x = r.x0;
    out->y = r.y0;
    return S_OK;
}

/* the pixels of src over r, transparent where src has none. */
static HRESULT crop(const D2dImageData *src, D2dIRect r, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    out->x = r.x0;
    out->y = r.y0;
    if (rect_empty(r))
        return S_OK;
    if (!D2dSurfaceInit(&out->surface, r.x1 - r.x0, r.y1 - r.y0))
        return E_OUTOFMEMORY;
    if (src->fpx && !(out->fpx = D2dAllocZero((SIZE_T)out->surface.w * out->surface.h * 16))) {
        D2dImageDataFree(out);
        return E_OUTOFMEMORY;
    }
    for (int y = r.y0; y < r.y1; y++) {
        int sy = y - src->y;
        if (sy < 0 || sy >= src->surface.h)
            continue;
        int x0 = r.x0 > src->x ? r.x0 : src->x, x1 = r.x1 < src->x + src->surface.w ? r.x1 : src->x + src->surface.w;
        if (x1 <= x0)
            continue;
        memcpy(D2dSurfaceRow(&out->surface, y - r.y0) + (x0 - r.x0), D2dSurfaceRow(&src->surface, sy) + (x0 - src->x), (SIZE_T)(x1 - x0) * 4);
        if (src->fpx)
            memcpy(out->fpx + ((SIZE_T)(y - r.y0) * out->surface.w + (x0 - r.x0)) * 4,
                   src->fpx + ((SIZE_T)sy * src->surface.w + (x0 - src->x)) * 4, (SIZE_T)(x1 - x0) * 16);
    }
    return S_OK;
}

static float *pixel_f(const D2dImageData *d, SIZE_T i, float *tmp)
{
    if (d->fpx)
        return d->fpx + i * 4;
    uint32_t p = d->surface.px[i];
    tmp[0] = (p >> 16 & 255) / 255.0f;
    tmp[1] = (p >> 8 & 255) / 255.0f;
    tmp[2] = (p & 255) / 255.0f;
    tmp[3] = (p >> 24) / 255.0f;
    return tmp;
}

static uint32_t unorm8(float v) { return !(v > 0.0f) ? 0 : v >= 1.0f ? 255 : (uint32_t)(v * 255.0f + 0.5f); }

/* gives 8 bit image data its floats, for a node that mixes inputs of both kinds. */
static HRESULT widen(D2dImageData *d)
{
    if (d->fpx || d->surface.w <= 0 || d->surface.h <= 0)
        return S_OK;
    SIZE_T n = (SIZE_T)d->surface.w * d->surface.h;
    float *f = D2dAlloc(n * 16);
    if (!f)
        return E_OUTOFMEMORY;
    for (SIZE_T i = 0; i < n; i++) {
        float tmp[4];
        memcpy(f + i * 4, pixel_f(d, i, tmp), 16);
    }
    d->fpx = f;
    return S_OK;
}

typedef struct Eval {
    D2dRt *rt;
    CEffect *e;
    CGraph *g;
    int depth;
    D2dIRect *bounds;
    BYTE *state;
    /* the last pixels of each node that feeds more than one other, for the next one that reads them. */
    UINT32 *users;
    D2dImageData *kept;
} Eval;

static HRESULT conn_bounds(Eval *ev, Conn c, D2dIRect *b);
static HRESULT conn_get(Eval *ev, Conn c, D2dIRect want, D2dImageData *out);

typedef struct Proxy {
    Eval *ev;
    Conn conn;
} Proxy;

static HRESULT proxy_bounds(void *ctx, D2dIRect *b, int depth)
{
    Proxy *p = ctx;
    return conn_bounds(p->ev, p->conn, b);
}
static HRESULT proxy_get(void *ctx, D2dIRect want, D2dImageData *out, int depth)
{
    Proxy *p = ctx;
    return conn_get(p->ev, p->conn, want, out);
}
static const D2dProxyOps g_proxy_ops = {proxy_bounds, proxy_get};

/* runs fn on the effect of a node with its inputs replaced by the node's connections. */
static HRESULT with_proxies(Eval *ev, GNode *n, HRESULT (*fn)(Eval *ev, ID2D1Effect *effect, void *arg), void *arg)
{
    ID2D1Effect *effect = ((CEffectNode *)n->node)->effect;
    UINT32 count = effect->lpVtbl->GetInputCount(effect);
    ID2D1Image **saved = D2dAllocZero((count ? count : 1) * sizeof(ID2D1Image *));
    Proxy *proxies = D2dAllocZero((count ? count : 1) * sizeof(Proxy));
    if (!saved || !proxies) {
        D2dFree(saved);
        D2dFree(proxies);
        return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < count; i++) {
        effect->lpVtbl->GetInput(effect, i, &saved[i]);
        ID2D1Image *proxy = nullptr;
        proxies[i] = (Proxy){ev, i < n->ninputs ? n->in[i] : (Conn){CONN_NONE, 0}};
        if (proxies[i].conn.kind != CONN_NONE && FAILED(hr = D2dProxyCreate(ev->e->obj.factory, &g_proxy_ops, &proxies[i], &proxy)))
            break;
        effect->lpVtbl->SetInput(effect, i, proxy, FALSE);
        D2D_RELEASE(proxy);
    }
    if (SUCCEEDED(hr))
        hr = fn(ev, effect, arg);
    for (UINT32 i = 0; i < count; i++) {
        effect->lpVtbl->SetInput(effect, i, saved[i], FALSE);
        D2D_RELEASE(saved[i]);
    }
    D2dFree(saved);
    D2dFree(proxies);
    return hr;
}

static HRESULT effect_bounds_fn(Eval *ev, ID2D1Effect *effect, void *arg)
{
    return D2dImageBoundsPx(ev->rt, (ID2D1Image *)effect, arg, ev->depth + 1);
}

typedef struct EffectGet {
    D2dIRect want;
    D2dImageData *out;
} EffectGet;

static HRESULT effect_get_fn(Eval *ev, ID2D1Effect *effect, void *arg)
{
    EffectGet *g = arg;
    return D2dImageGet(ev->rt, (ID2D1Image *)effect, g->want, g->out, ev->depth + 1);
}

static HRESULT node_bounds(Eval *ev, UINT32 index, D2dIRect *out)
{
    if (ev->state[index] == 2) {
        *out = ev->bounds[index];
        return S_OK;
    }
    if (ev->state[index] == 1)
        return D2DERR_CYCLIC_GRAPH;
    ev->state[index] = 1;
    GNode *n = &ev->g->nodes[index];
    D2dIRect in[MAX_SLOTS], r = {0, 0, 0, 0};
    UINT32 count = n->ninputs < MAX_SLOTS ? n->ninputs : MAX_SLOTS;
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < count && SUCCEEDED(hr); i++)
        hr = conn_bounds(ev, n->in[i], &in[i]);
    if (FAILED(hr))
        return hr;
    switch (n->kind) {
    case NODE_DRAW:
    case NODE_COMPUTE:
    case NODE_SOURCE: {
        D2D1_RECT_L rl[MAX_SLOTS], opaque[MAX_SLOTS] = {}, o = {}, oo = {};
        for (UINT32 i = 0; i < count; i++)
            rl[i] = rect_l(in[i]);
        hr = n->transform->lpVtbl->MapInputRectsToOutputRect(n->transform, rl, opaque, count, &o, &oo);
        r = rect_i(o);
        break;
    }
    case NODE_BLEND:
        for (UINT32 i = 0; i < count; i++)
            r = rect_or(r, in[i]);
        break;
    case NODE_BORDER:
        r = g_infinite;
        break;
    case NODE_OFFSET: {
        D2D1_POINT_2L off = ((COffset *)n->node)->offset;
        r = count ? in[0] : r;
        if (r.x0 > -D2D_INF_EXTENT)
            r.x0 += off.x;
        if (r.x1 < D2D_INF_EXTENT)
            r.x1 += off.x;
        if (r.y0 > -D2D_INF_EXTENT)
            r.y0 += off.y;
        if (r.y1 < D2D_INF_EXTENT)
            r.y1 += off.y;
        break;
    }
    case NODE_BOUNDS:
        r = rect_i(((CBounds *)n->node)->bounds);
        break;
    case NODE_EFFECT:
        hr = with_proxies(ev, n, effect_bounds_fn, &r);
        break;
    default:
        hr = D2DERR_INVALID_GRAPH_CONFIGURATION;
        break;
    }
    if (FAILED(hr))
        return hr;
    ev->bounds[index] = r;
    ev->state[index] = 2;
    *out = r;
    return S_OK;
}

static HRESULT conn_bounds(Eval *ev, Conn c, D2dIRect *b)
{
    *b = (D2dIRect){0, 0, 0, 0};
    if (c.kind == CONN_NODE)
        return node_bounds(ev, c.index, b);
    if (c.kind == CONN_EFFECT && c.index < ev->e->ninputs && ev->e->inputs[c.index])
        return D2dImageBoundsPx(ev->rt, ev->e->inputs[c.index], b, ev->depth + 1);
    return S_OK;
}

/* no pass asks for more than this around its output, so that an input without edges stays finite. */
#define INPUT_MARGIN 4096

static HRESULT node_get(Eval *ev, UINT32 index, D2dIRect want, D2dImageData *out);
static HRESULT node_make(Eval *ev, GNode *n, D2dIRect r, D2dImageData *out);

static HRESULT conn_get(Eval *ev, Conn c, D2dIRect want, D2dImageData *out)
{
    if (rect_empty(want) || c.kind == CONN_NONE)
        return empty_data(want, out);
    if (c.kind == CONN_NODE)
        return node_get(ev, c.index, want, out);
    if (c.index >= ev->e->ninputs || !ev->e->inputs[c.index])
        return empty_data(want, out);
    D2dImageData raw = {};
    HRESULT hr = D2dImageGet(ev->rt, ev->e->inputs[c.index], want, &raw, ev->depth + 1);
    if (FAILED(hr))
        return hr;
    hr = crop(&raw, want, out);
    D2dImageDataFree(&raw);
    return hr;
}

static float blend_factor(D2D1_BLEND b, const float *s, const float *d, const float *k, int c)
{
    switch (b) {
    case D2D1_BLEND_ZERO: return 0.0f;
    case D2D1_BLEND_ONE: return 1.0f;
    case D2D1_BLEND_SRC_COLOR: return s[c];
    case D2D1_BLEND_INV_SRC_COLOR: return 1.0f - s[c];
    case D2D1_BLEND_SRC_ALPHA: return s[3];
    case D2D1_BLEND_INV_SRC_ALPHA: return 1.0f - s[3];
    case D2D1_BLEND_DEST_ALPHA: return d[3];
    case D2D1_BLEND_INV_DEST_ALPHA: return 1.0f - d[3];
    case D2D1_BLEND_DEST_COLOR: return d[c];
    case D2D1_BLEND_INV_DEST_COLOR: return 1.0f - d[c];
    case D2D1_BLEND_SRC_ALPHA_SAT: return c == 3 ? 1.0f : D2dMinF(s[3], 1.0f - d[3]);
    case D2D1_BLEND_BLEND_FACTOR: return k[c];
    case D2D1_BLEND_INV_BLEND_FACTOR: return 1.0f - k[c];
    default: return 0.0f;
    }
}

static float blend_op(D2D1_BLEND_OPERATION op, float s, float d)
{
    switch (op) {
    case D2D1_BLEND_OPERATION_SUBTRACT: return s - d;
    case D2D1_BLEND_OPERATION_REV_SUBTRACT: return d - s;
    case D2D1_BLEND_OPERATION_MIN: return D2dMinF(s, d);
    case D2D1_BLEND_OPERATION_MAX: return D2dMaxF(s, d);
    default: return s + d;
    }
}

/* each input blended in order onto transparent black, the way a blend transform draws them. */
static void blend_into(D2dImageData *dst, const D2dImageData *src, const D2D1_BLEND_DESCRIPTION *bd)
{
    SIZE_T n = (SIZE_T)dst->surface.w * dst->surface.h;
    for (SIZE_T i = 0; i < n; i++) {
        float st[4], dt[4], o[4];
        const float *sp = pixel_f(src, i, st), *dp = pixel_f(dst, i, dt);
        for (int c = 0; c < 3; c++)
            o[c] = blend_op(bd->blendOperation, sp[c] * blend_factor(bd->sourceBlend, sp, dp, bd->blendFactor, c),
                            dp[c] * blend_factor(bd->destinationBlend, sp, dp, bd->blendFactor, c));
        o[3] = blend_op(bd->blendOperationAlpha, sp[3] * blend_factor(bd->sourceBlendAlpha, sp, dp, bd->blendFactor, 3),
                        dp[3] * blend_factor(bd->destinationBlendAlpha, sp, dp, bd->blendFactor, 3));
        if (dst->fpx) {
            memcpy(dst->fpx + i * 4, o, 16);
        } else {
            for (int c = 0; c < 4; c++)
                o[c] = D2dClampF(o[c], 0.0f, 1.0f);
        }
        dst->surface.px[i] = unorm8(o[3]) << 24 | unorm8(o[0]) << 16 | unorm8(o[1]) << 8 | unorm8(o[2]);
    }
}

static int extend(int v, int lo, int hi, D2D1_EXTEND_MODE m)
{
    int w = hi - lo;
    if (w <= 0)
        return lo;
    int t = v - lo;
    if (m == D2D1_EXTEND_MODE_WRAP) {
        t %= w;
        if (t < 0)
            t += w;
    } else if (m == D2D1_EXTEND_MODE_MIRROR) {
        int p = 2 * w;
        t %= p;
        if (t < 0)
            t += p;
        if (t >= w)
            t = p - 1 - t;
    } else {
        t = t < 0 ? 0 : t >= w ? w - 1 : t;
    }
    return lo + t;
}

/* the target of a source transform is in the buffer it asked for, as it writes its pixels in that format. windows keeps one channel in red. */
static DXGI_FORMAT source_format(const CInfo *info, D2D1_BUFFER_PRECISION fallback)
{
    D2D1_BUFFER_PRECISION p = info && info->precision != D2D1_BUFFER_PRECISION_UNKNOWN ? info->precision : fallback;
    BOOL one = info && info->depth == D2D1_CHANNEL_DEPTH_1;
    switch (p) {
    case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case D2D1_BUFFER_PRECISION_16BPC_UNORM:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    case D2D1_BUFFER_PRECISION_16BPC_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case D2D1_BUFFER_PRECISION_32BPC_FLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default:
        return one ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}

static BOOL is_wide(D2D1_BUFFER_PRECISION p)
{
    return p == D2D1_BUFFER_PRECISION_16BPC_UNORM || p == D2D1_BUFFER_PRECISION_16BPC_FLOAT || p == D2D1_BUFFER_PRECISION_32BPC_FLOAT;
}

/* the buffer a pass draws into: what the transform asked for, else the most precise of its inputs, else the effect's. */
static BOOL pass_wide(Eval *ev, const CInfo *info, const D2dImageData *const *inputs, UINT32 count)
{
    if (info && info->precision != D2D1_BUFFER_PRECISION_UNKNOWN)
        return is_wide(info->precision);
    for (UINT32 i = 0; i < count; i++) {
        if (inputs[i]->fpx)
            return TRUE;
    }
    D2D1_BUFFER_PRECISION p = ev->e->precision;
    if (p == D2D1_BUFFER_PRECISION_UNKNOWN && ev->rt)
        p = ev->rt->controls.bufferPrecision;
    return is_wide(p);
}

static HRESULT run_pass(Eval *ev, GNode *n, D2dIRect r, D2dImageData *out)
{
    UINT32 count = n->ninputs < MAX_SLOTS ? n->ninputs : MAX_SLOTS;
    D2D1_RECT_L want_l = rect_l(r), in_l[MAX_SLOTS] = {};
    HRESULT hr = S_OK;
    if (n->kind != NODE_SOURCE && count)
        hr = n->transform->lpVtbl->MapOutputRectToInputRects(n->transform, &want_l, in_l, count);
    if (FAILED(hr))
        return hr;
    D2dImageData data[MAX_SLOTS] = {};
    const D2dImageData *inputs[MAX_SLOTS];
    D2dIRect limit = {r.x0 - INPUT_MARGIN, r.y0 - INPUT_MARGIN, r.x1 + INPUT_MARGIN, r.y1 + INPUT_MARGIN};
    for (UINT32 i = 0; i < count && SUCCEEDED(hr); i++) {
        D2dIRect b;
        hr = conn_bounds(ev, n->in[i], &b);
        if (SUCCEEDED(hr))
            hr = conn_get(ev, n->in[i], rect_and(rect_and(rect_i(in_l[i]), b), limit), &data[i]);
        inputs[i] = &data[i];
    }
    CInfo *info = n->info;
    if (SUCCEEDED(hr) && n->kind == NODE_SOURCE) {
        D2dBitmap *b;
        hr = D2dBitmapCreate(ev->e->obj.factory, (UINT32)(r.x1 - r.x0), (UINT32)(r.y1 - r.y0), source_format(info, ev->e->precision),
                             D2D1_ALPHA_MODE_PREMULTIPLIED, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, TRUE, &b);
        if (SUCCEEDED(hr)) {
            b->writable = TRUE;
            /* a buffer of an effect holds its values without the transfer curve of a float bitmap. */
            b->srgb = TRUE;
            ID2D1SourceTransform *t = (ID2D1SourceTransform *)n->transform;
            hr = t->lpVtbl->Draw(t, (ID2D1Bitmap1 *)b, &want_l, (D2D1_POINT_2U){0, 0});
            const D2dSurface *v = SUCCEEDED(hr) ? D2dBitmapView(b) : nullptr;
            if (SUCCEEDED(hr) && !v)
                hr = E_OUTOFMEMORY;
            if (SUCCEEDED(hr)) {
                D2dImageData whole = {*v, r.x0, r.y0, D2dBitmapFloats(b)};
                hr = crop(&whole, r, out);
                D2dFree(whole.fpx);
            }
            D2dRelease(b);
        }
    } else if (SUCCEEDED(hr) && info && info->has_shader) {
        UINT32 groups[3] = {1, 1, 1};
        /* the transform may set its constants while it counts its thread groups, so they are read after. */
        if (n->kind == NODE_COMPUTE) {
            ID2D1ComputeTransform *t = (ID2D1ComputeTransform *)n->transform;
            hr = t->lpVtbl->CalculateThreadgroups(t, &want_l, &groups[0], &groups[1], &groups[2]);
        }
        D2dGpuTexture *res[MAX_SLOTS];
        UINT32 slots[MAX_SLOTS], nres = 0;
        for (UINT32 k = 0; k < MAX_SLOTS; k++) {
            if (info->tex[k]) {
                res[nres] = ((CResTex *)info->tex[k])->tex;
                slots[nres++] = k;
            }
        }
        BYTE *results = nullptr;
        UINT32 results_size = 0;
        D2dGpuPass pass = {&info->shader, info->cb, info->cb_size, res, slots, nres, inputs, info->filter, count, r,
                           {groups[0], groups[1], groups[2]}, pass_wide(ev, info, inputs, count)};
        if (n->analysis && n->kind == NODE_COMPUTE) {
            pass.analysis = &results;
            pass.analysis_size = &results_size;
        }
        if (SUCCEEDED(hr))
            hr = n->kind == NODE_COMPUTE ? D2dGpuCompute(&pass, out) : D2dGpuDraw(&pass, out);
        if (SUCCEEDED(hr) && results) {
            hr = n->analysis->lpVtbl->ProcessAnalysisResults(n->analysis, results, results_size);
            D2dImageDataFree(out);
            empty_data(r, out);
        }
        D2dFree(results);
    } else if (SUCCEEDED(hr) && n->kind == NODE_DRAW && count) {
        /* a draw transform that set no pixel shader passes its first input through. */
        hr = crop(&data[0], r, out);
    } else if (SUCCEEDED(hr)) {
        hr = empty_data(r, out);
    }
    for (UINT32 i = 0; i < count; i++)
        D2dImageDataFree(&data[i]);
    return hr;
}

static HRESULT node_get(Eval *ev, UINT32 index, D2dIRect want, D2dImageData *out)
{
    memset(out, 0, sizeof(*out));
    D2dIRect b;
    HRESULT hr = node_bounds(ev, index, &b);
    if (FAILED(hr))
        return hr;
    GNode *n = &ev->g->nodes[index];
    /* an analysis looks at all of its input however little of the output is drawn, as windows runs it whole. */
    D2dIRect r = n->analysis && !rect_empty(b) && b.x0 > -D2D_INF_EXTENT && b.y0 > -D2D_INF_EXTENT && b.x1 < D2D_INF_EXTENT &&
                         b.y1 < D2D_INF_EXTENT
                     ? b
                     : rect_and(want, b);
    if (rect_empty(r))
        return empty_data(want, out);
    D2dImageData *kept = &ev->kept[index];
    if (kept->surface.px && r.x0 >= kept->x && r.y0 >= kept->y && r.x1 <= kept->x + kept->surface.w && r.y1 <= kept->y + kept->surface.h)
        return crop(kept, r, out);
    hr = node_make(ev, n, r, out);
    if (SUCCEEDED(hr) && ev->users[index] > 1 && out->surface.px) {
        D2dImageDataFree(kept);
        hr = crop(out, (D2dIRect){out->x, out->y, out->x + out->surface.w, out->y + out->surface.h}, kept);
    }
    return hr;
}

static HRESULT node_make(Eval *ev, GNode *n, D2dIRect r, D2dImageData *out)
{
    HRESULT hr = S_OK;
    switch (n->kind) {
    case NODE_DRAW:
    case NODE_COMPUTE:
    case NODE_SOURCE:
        return run_pass(ev, n, r, out);
    case NODE_BLEND: {
        if (!D2dSurfaceInit(&out->surface, r.x1 - r.x0, r.y1 - r.y0))
            return E_OUTOFMEMORY;
        out->x = r.x0;
        out->y = r.y0;
        D2dImageData *cut = D2dAllocZero((n->ninputs ? n->ninputs : 1) * sizeof(D2dImageData));
        if (!cut)
            return E_OUTOFMEMORY;
        BOOL wide = FALSE;
        for (UINT32 i = 0; i < n->ninputs && SUCCEEDED(hr); i++) {
            D2dImageData in;
            hr = conn_get(ev, n->in[i], r, &in);
            if (SUCCEEDED(hr)) {
                hr = crop(&in, r, &cut[i]);
                D2dImageDataFree(&in);
                wide |= cut[i].fpx != nullptr;
            }
        }
        if (SUCCEEDED(hr) && wide)
            hr = widen(out);
        for (UINT32 i = 0; i < n->ninputs; i++) {
            if (SUCCEEDED(hr))
                blend_into(out, &cut[i], &((CBlend *)n->node)->desc);
            D2dImageDataFree(&cut[i]);
        }
        D2dFree(cut);
        return hr;
    }
    case NODE_BORDER: {
        D2dIRect ib;
        hr = conn_bounds(ev, n->in[0], &ib);
        if (FAILED(hr))
            return hr;
        if (rect_empty(ib) || ib.x0 <= -D2D_INF_EXTENT || ib.y0 <= -D2D_INF_EXTENT || ib.x1 >= D2D_INF_EXTENT || ib.y1 >= D2D_INF_EXTENT)
            return conn_get(ev, n->in[0], r, out);
        /* only the part of the input that the output rectangle maps to. */
        CBorder *bt = (CBorder *)n->node;
        D2dIRect need = {D2D_INF_EXTENT, D2D_INF_EXTENT, -D2D_INF_EXTENT, -D2D_INF_EXTENT};
        for (int x = r.x0; x < r.x1; x++) {
            int sx = extend(x, ib.x0, ib.x1, bt->x);
            need.x0 = sx < need.x0 ? sx : need.x0;
            need.x1 = sx + 1 > need.x1 ? sx + 1 : need.x1;
        }
        for (int y = r.y0; y < r.y1; y++) {
            int sy = extend(y, ib.y0, ib.y1, bt->y);
            need.y0 = sy < need.y0 ? sy : need.y0;
            need.y1 = sy + 1 > need.y1 ? sy + 1 : need.y1;
        }
        D2dImageData in;
        hr = conn_get(ev, n->in[0], need, &in);
        D2dImageData full;
        if (SUCCEEDED(hr)) {
            hr = crop(&in, need, &full);
            D2dImageDataFree(&in);
        }
        if (FAILED(hr))
            return hr;
        if (!D2dSurfaceInit(&out->surface, r.x1 - r.x0, r.y1 - r.y0)) {
            D2dImageDataFree(&full);
            return E_OUTOFMEMORY;
        }
        out->x = r.x0;
        out->y = r.y0;
        if (full.fpx && !(out->fpx = D2dAlloc((SIZE_T)out->surface.w * out->surface.h * 16))) {
            D2dImageDataFree(&full);
            D2dImageDataFree(out);
            return E_OUTOFMEMORY;
        }
        for (int y = r.y0; y < r.y1; y++) {
            int sy = extend(y, ib.y0, ib.y1, bt->y) - need.y0;
            for (int x = r.x0; x < r.x1; x++) {
                int sx = extend(x, ib.x0, ib.x1, bt->x) - need.x0;
                D2dSurfaceRow(&out->surface, y - r.y0)[x - r.x0] = D2dSurfaceRow(&full.surface, sy)[sx];
                if (full.fpx)
                    memcpy(out->fpx + ((SIZE_T)(y - r.y0) * out->surface.w + (x - r.x0)) * 4, full.fpx + ((SIZE_T)sy * full.surface.w + sx) * 4, 16);
            }
        }
        D2dImageDataFree(&full);
        return S_OK;
    }
    case NODE_OFFSET: {
        D2D1_POINT_2L off = ((COffset *)n->node)->offset;
        D2dIRect src = {r.x0 - off.x, r.y0 - off.y, r.x1 - off.x, r.y1 - off.y};
        hr = conn_get(ev, n->in[0], src, out);
        out->x += off.x;
        out->y += off.y;
        return hr;
    }
    case NODE_BOUNDS:
        return conn_get(ev, n->in[0], r, out);
    case NODE_EFFECT: {
        EffectGet g = {r, out};
        return with_proxies(ev, n, effect_get_fn, &g);
    }
    default:
        return D2DERR_INVALID_GRAPH_CONFIGURATION;
    }
}

static HRESULT custom_prepare(D2dRt *rt, CEffect *e)
{
    if (rt && (e->context->dpi_x != rt->dpi_x || e->context->dpi_y != rt->dpi_y)) {
        e->context->dpi_x = rt->dpi_x;
        e->context->dpi_y = rt->dpi_y;
        mark_changed(e, D2D1_CHANGE_TYPE_CONTEXT);
    }
    if (!e->prepare)
        return S_OK;
    HRESULT hr = e->impl->lpVtbl->PrepareForRender(e->impl, e->change);
    if (SUCCEEDED(hr)) {
        e->prepare = FALSE;
        e->change = D2D1_CHANGE_TYPE_NONE;
    }
    return hr;
}

static void eval_end(Eval *ev);

static HRESULT eval_begin(D2dRt *rt, CEffect *e, int depth, Eval *ev)
{
    if (depth > D2D_MAX_DEPTH)
        return D2DERR_CYCLIC_GRAPH;
    HRESULT hr = custom_prepare(rt, e);
    if (FAILED(hr))
        return hr;
    *ev = (Eval){rt, e, e->graph, depth, nullptr, nullptr, nullptr, nullptr};
    UINT32 n = e->graph->n ? e->graph->n : 1;
    ev->bounds = D2dAllocZero(n * sizeof(D2dIRect));
    ev->state = D2dAllocZero(n);
    ev->users = D2dAllocZero(n * sizeof(UINT32));
    ev->kept = D2dAllocZero(n * sizeof(D2dImageData));
    if (!ev->bounds || !ev->state || !ev->users || !ev->kept) {
        eval_end(ev);
        return E_OUTOFMEMORY;
    }
    for (UINT32 i = 0; i < e->graph->n; i++) {
        const GNode *g = &e->graph->nodes[i];
        for (UINT32 k = 0; k < g->ninputs; k++) {
            if (g->in[k].kind == CONN_NODE && g->in[k].index < e->graph->n)
                ev->users[g->in[k].index]++;
        }
    }
    return S_OK;
}

static void eval_end(Eval *ev)
{
    for (UINT32 i = 0; ev->kept && i < (ev->g->n ? ev->g->n : 1); i++)
        D2dImageDataFree(&ev->kept[i]);
    D2dFree(ev->kept);
    D2dFree(ev->users);
    D2dFree(ev->bounds);
    D2dFree(ev->state);
}

HRESULT D2dCustomBounds(D2dRt *rt, ID2D1Image *image, D2dIRect *b, int depth)
{
    CEffect *e = D2dCustomOf(image);
    if (InterlockedIncrement(&e->busy) > 1) {
        InterlockedDecrement(&e->busy);
        return D2DERR_CYCLIC_GRAPH;
    }
    Eval ev;
    HRESULT hr = eval_begin(rt, e, depth, &ev);
    if (SUCCEEDED(hr)) {
        if (e->graph->passthrough >= 0)
            hr = conn_bounds(&ev, (Conn){CONN_EFFECT, (UINT32)e->graph->passthrough}, b);
        else if (e->graph->output >= 0)
            hr = node_bounds(&ev, (UINT32)e->graph->output, b);
        else
            hr = D2DERR_INVALID_GRAPH_CONFIGURATION;
        eval_end(&ev);
    }
    InterlockedDecrement(&e->busy);
    return hr;
}

HRESULT D2dCustomRender(D2dRt *rt, ID2D1Image *image, D2dIRect want, D2dImageData *out, int depth)
{
    memset(out, 0, sizeof(*out));
    CEffect *e = D2dCustomOf(image);
    if (InterlockedIncrement(&e->busy) > 1) {
        InterlockedDecrement(&e->busy);
        return D2DERR_CYCLIC_GRAPH;
    }
    Eval ev;
    HRESULT hr = eval_begin(rt, e, depth, &ev);
    if (SUCCEEDED(hr)) {
        if (e->graph->passthrough >= 0) {
            hr = conn_get(&ev, (Conn){CONN_EFFECT, (UINT32)e->graph->passthrough}, want, out);
        } else if (e->graph->output >= 0) {
            /* only the transforms the output reaches learn their rectangles, as nodes left unconnected are never run. */
            hr = node_get(&ev, (UINT32)e->graph->output, want, out);
        } else {
            hr = D2DERR_INVALID_GRAPH_CONFIGURATION;
        }
        eval_end(&ev);
    }
    InterlockedDecrement(&e->busy);
    return hr;
}
