/* the mof compiler: reads managed object format text and puts its classes and instances into the repository. */
#include "wbemcore.h"

void wr_write_begin(void);
void wr_write_end(void);

typedef enum TokKind { T_EOF, T_IDENT, T_STRING, T_INT, T_REAL, T_ALIAS, T_PUNCT, T_ERROR } TokKind;

typedef struct Token {
    TokKind kind;
    WCHAR *text; /* identifier, string or alias name. */
    ULONGLONG integer;
    BOOL negative;
    double real;
    WCHAR punct;
    int line;
} Token;

typedef struct Alias {
    WCHAR *name;
    WCHAR *path;
} Alias;

typedef struct Parser {
    const WCHAR *src;
    SIZE_T length, pos;
    int line;
    Token tok;
    HRESULT error;
    int error_line;
    WNs *ns;
    LONG options, class_flags, instance_flags;
    BOOL builtin;
    Alias *aliases;
    UINT alias_count, alias_capacity;
    LONG object_number;
    int first_line;
    /* the classes the file defines while it is only checked, kept as |name| so that instances of them are known. */
    WCHAR *defined;
} Parser;

/* ------------------------------------------------------------------ lexer */

static void tok_free(Token *t)
{
    wb_free(t->text);
    memset(t, 0, sizeof(*t));
}

static WCHAR peek_char(Parser *p, SIZE_T offset)
{
    return p->pos + offset < p->length ? p->src[p->pos + offset] : 0;
}

static void skip_blank(Parser *p)
{
    for (;;) {
        WCHAR c = peek_char(p, 0);
        if (c == '\n') {
            p->line++;
            p->pos++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == 0xFEFF || c == 0x0C) {
            p->pos++;
        } else if (c == '/' && peek_char(p, 1) == '/') {
            while (p->pos < p->length && peek_char(p, 0) != '\n')
                p->pos++;
        } else if (c == '/' && peek_char(p, 1) == '*') {
            p->pos += 2;
            while (p->pos < p->length && !(peek_char(p, 0) == '*' && peek_char(p, 1) == '/')) {
                if (peek_char(p, 0) == '\n')
                    p->line++;
                p->pos++;
            }
            p->pos += 2;
        } else {
            break;
        }
    }
}

static BOOL ident_start(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c > 0x7F;
}

static BOOL ident_char(WCHAR c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}

static int hex_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL lex_escape(Parser *p, WBuf *buf)
{
    WCHAR c = peek_char(p, 0);
    p->pos++;
    switch (c) {
    case 'n':
        wb_buf_addc(buf, '\n');
        break;
    case 'r':
        wb_buf_addc(buf, '\r');
        break;
    case 't':
        wb_buf_addc(buf, '\t');
        break;
    case 'b':
        wb_buf_addc(buf, '\b');
        break;
    case 'f':
        wb_buf_addc(buf, '\f');
        break;
    case 'x':
    case 'X': {
        UINT value = 0;
        int digits = 0, v;
        while (digits < 4 && (v = hex_value(peek_char(p, 0))) >= 0) {
            value = value * 16 + (UINT)v;
            p->pos++;
            digits++;
        }
        wb_buf_addc(buf, (WCHAR)value);
        break;
    }
    case 0:
        return FALSE;
    default:
        wb_buf_addc(buf, c);
        break;
    }
    return TRUE;
}

static void lex(Parser *p)
{
    tok_free(&p->tok);
    skip_blank(p);
    Token *t = &p->tok;
    t->line = p->line;
    if (p->pos >= p->length) {
        t->kind = T_EOF;
        return;
    }
    WCHAR c = peek_char(p, 0);
    if (ident_start(c)) {
        SIZE_T start = p->pos;
        while (ident_char(peek_char(p, 0)))
            p->pos++;
        t->kind = T_IDENT;
        t->text = wb_wcsndup(p->src + start, p->pos - start);
        return;
    }
    if (c == '$') {
        p->pos++;
        SIZE_T start = p->pos;
        while (ident_char(peek_char(p, 0)))
            p->pos++;
        t->kind = T_ALIAS;
        t->text = wb_wcsndup(p->src + start, p->pos - start);
        return;
    }
    if (c == '"') {
        WBuf buf = {0};
        p->pos++;
        for (;;) {
            WCHAR d = peek_char(p, 0);
            if (p->pos >= p->length || d == '\n') {
                wb_free(buf.text);
                t->kind = T_ERROR;
                return;
            }
            p->pos++;
            if (d == '"')
                break;
            if (d == '\\') {
                if (!lex_escape(p, &buf)) {
                    wb_free(buf.text);
                    t->kind = T_ERROR;
                    return;
                }
            } else {
                wb_buf_addc(&buf, d);
            }
        }
        t->kind = T_STRING;
        t->text = wb_buf_take(&buf);
        return;
    }
    if (c == '\'') {
        WBuf buf = {0};
        p->pos++;
        WCHAR d = peek_char(p, 0);
        p->pos++;
        if (d == '\\')
            lex_escape(p, &buf);
        else
            wb_buf_addc(&buf, d);
        if (peek_char(p, 0) == '\'')
            p->pos++;
        t->kind = T_INT;
        t->integer = buf.text ? buf.text[0] : 0;
        wb_free(buf.text);
        return;
    }
    if ((c >= '0' && c <= '9') || ((c == '-' || c == '+' || c == '.') && ((peek_char(p, 1) >= '0' && peek_char(p, 1) <= '9') ||
                                                                       (c != '.' && peek_char(p, 1) == '.')))) {
        SIZE_T start = p->pos;
        BOOL negative = FALSE;
        if (c == '-' || c == '+') {
            negative = c == '-';
            p->pos++;
        }
        BOOL real = FALSE;
        ULONGLONG value = 0;
        if (peek_char(p, 0) == '0' && (peek_char(p, 1) == 'x' || peek_char(p, 1) == 'X')) {
            p->pos += 2;
            int v;
            while ((v = hex_value(peek_char(p, 0))) >= 0) {
                value = value * 16 + (ULONGLONG)v;
                p->pos++;
            }
        } else {
            SIZE_T digits_start = p->pos;
            while ((peek_char(p, 0) >= '0' && peek_char(p, 0) <= '9') || peek_char(p, 0) == '.' ||
                   ((peek_char(p, 0) == 'e' || peek_char(p, 0) == 'E') &&
                    ((peek_char(p, 1) >= '0' && peek_char(p, 1) <= '9') || peek_char(p, 1) == '-' || peek_char(p, 1) == '+'))) {
                if (peek_char(p, 0) == '.' || peek_char(p, 0) == 'e' || peek_char(p, 0) == 'E') {
                    real = TRUE;
                    if (peek_char(p, 0) != '.')
                        p->pos++;
                }
                p->pos++;
            }
            WCHAR *text = wb_wcsndup(p->src + digits_start, p->pos - digits_start);
            if (real) {
                double d = 0;
                VarR8FromStr(text, 0x409, 0, &d);
                t->real = negative ? -d : d;
            } else {
                BOOL octal = text[0] == '0' && text[1];
                for (const WCHAR *d = text; *d; d++)
                    value = value * (octal ? 8 : 10) + (ULONGLONG)(*d - '0');
            }
            wb_free(text);
            /* a trailing b or B makes a binary number out of 0s and 1s. */
            if (!real && (peek_char(p, 0) == 'b' || peek_char(p, 0) == 'B') && !ident_char(peek_char(p, 1))) {
                ULONGLONG bits = 0;
                for (SIZE_T i = digits_start; i < p->pos; i++)
                    bits = bits * 2 + (ULONGLONG)(p->src[i] - '0');
                value = bits;
                p->pos++;
            }
        }
        if (real) {
            t->kind = T_REAL;
        } else {
            t->kind = T_INT;
            t->integer = value;
            t->negative = negative;
        }
        (void)start;
        return;
    }
    p->pos++;
    t->kind = T_PUNCT;
    t->punct = c;
}

static BOOL is_punct(Parser *p, WCHAR c)
{
    return p->tok.kind == T_PUNCT && p->tok.punct == c;
}

static BOOL is_word(Parser *p, const WCHAR *word)
{
    return p->tok.kind == T_IDENT && wb_eq(p->tok.text, word);
}

static void fail(Parser *p, HRESULT hr)
{
    if (SUCCEEDED(p->error)) {
        p->error = hr;
        p->error_line = p->tok.line;
    }
}

static BOOL expect_punct(Parser *p, WCHAR c)
{
    if (!is_punct(p, c)) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    lex(p);
    return TRUE;
}

/* ------------------------------------------------------------------ values */

static Alias *find_alias(Parser *p, const WCHAR *name)
{
    for (UINT i = 0; i < p->alias_count; i++)
        if (wb_eq(p->aliases[i].name, name))
            return &p->aliases[i];
    return nullptr;
}

static void set_alias(Parser *p, const WCHAR *name, const WCHAR *path)
{
    Alias *a = find_alias(p, name);
    if (!a) {
        WB_GROW(p->aliases, p->alias_count, p->alias_capacity);
        a = &p->aliases[p->alias_count++];
        a->name = wb_wcsdup(name);
    } else {
        wb_free(a->path);
    }
    a->path = wb_wcsdup(path);
}

/* one scalar literal into a variant: strings, whole numbers as 64 bit values, reals, booleans, null and aliases. */
static BOOL parse_scalar(Parser *p, VARIANT *v)
{
    VariantClear(v);
    wv_init(v);
    switch (p->tok.kind) {
    case T_STRING: {
        WBuf buf = {0};
        while (p->tok.kind == T_STRING) {
            wb_buf_add(&buf, p->tok.text);
            lex(p);
        }
        WCHAR *text = wb_buf_take(&buf);
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(text);
        wb_free(text);
        return TRUE;
    }
    case T_INT:
        if (p->tok.negative && p->tok.integer > 0x8000000000000000ULL) {
            fail(p, WBEM_E_VALUE_OUT_OF_RANGE);
            return FALSE;
        }
        if (p->tok.negative) {
            v->vt = VT_I8;
            v->llVal = -(LONGLONG)p->tok.integer;
        } else if (p->tok.integer > 0x7FFFFFFFFFFFFFFFULL) {
            v->vt = VT_UI8;
            v->ullVal = p->tok.integer;
        } else {
            v->vt = VT_I8;
            v->llVal = (LONGLONG)p->tok.integer;
        }
        lex(p);
        return TRUE;
    case T_REAL:
        v->vt = VT_R8;
        v->dblVal = p->tok.real;
        lex(p);
        return TRUE;
    case T_ALIAS: {
        Alias *a = find_alias(p, p->tok.text);
        if (!a) {
            fail(p, WBEM_E_FAILED);
            return FALSE;
        }
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(a->path);
        lex(p);
        return TRUE;
    }
    case T_IDENT:
        if (is_word(p, L"TRUE") || is_word(p, L"FALSE")) {
            v->vt = VT_BOOL;
            v->boolVal = is_word(p, L"TRUE") ? VARIANT_TRUE : VARIANT_FALSE;
            lex(p);
            return TRUE;
        }
        if (is_word(p, L"NULL")) {
            lex(p);
            return TRUE;
        }
        break;
    default:
        break;
    }
    fail(p, WBEM_E_INVALID_SYNTAX);
    return FALSE;
}

/* a value, which is a scalar or a brace list of them; a list is an array of variants. */
static BOOL parse_value(Parser *p, VARIANT *v)
{
    if (!is_punct(p, '{'))
        return parse_scalar(p, v);
    lex(p);
    VARIANT *items = nullptr;
    UINT count = 0, capacity = 0;
    BOOL ok = TRUE;
    while (ok && !is_punct(p, '}')) {
        WB_GROW(items, count, capacity);
        VariantInit(&items[count]);
        ok = parse_scalar(p, &items[count]);
        if (ok)
            count++;
        if (ok && is_punct(p, ','))
            lex(p);
        else if (ok && !is_punct(p, '}')) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            ok = FALSE;
        }
    }
    if (ok) {
        lex(p);
        SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, count);
        for (UINT i = 0; array && i < count; i++) {
            LONG index = (LONG)i;
            SafeArrayPutElement(array, &index, &items[i]);
        }
        VariantClear(v);
        v->vt = VT_ARRAY | VT_VARIANT;
        v->parray = array;
    }
    for (UINT i = 0; i < count; i++)
        VariantClear(&items[i]);
    wb_free(items);
    return ok;
}

/* the value of a qualifier: a string, number or boolean, or an array of one kind of them. */
static void simple_qualifier_value(VARIANT *raw, VARIANT *out)
{
    VariantClear(out);
    wv_init(out);
    if (!(raw->vt & VT_ARRAY)) {
        switch (raw->vt) {
        case VT_I8:
            out->vt = VT_I4;
            out->lVal = (LONG)raw->llVal;
            return;
        case VT_UI8:
            out->vt = VT_I4;
            out->lVal = (LONG)raw->ullVal;
            return;
        default:
            VariantCopy(out, raw);
            return;
        }
    }
    SAFEARRAY *in = raw->parray;
    LONG low, high;
    SafeArrayGetLBound(in, 1, &low);
    SafeArrayGetUBound(in, 1, &high);
    ULONG count = (ULONG)(high - low + 1);
    VARTYPE kind = VT_BSTR;
    if (count) {
        VARIANT first;
        VariantInit(&first);
        SafeArrayGetElement(in, &low, &first);
        kind = first.vt == VT_I8 || first.vt == VT_UI8 ? VT_I4 : first.vt == VT_BOOL ? VT_BOOL : first.vt == VT_R8 ? VT_R8 : VT_BSTR;
        VariantClear(&first);
    }
    SAFEARRAY *array = SafeArrayCreateVector(kind, 0, count);
    for (ULONG i = 0; array && i < count; i++) {
        LONG at = low + (LONG)i, to = (LONG)i;
        VARIANT item, converted;
        VariantInit(&item);
        VariantInit(&converted);
        SafeArrayGetElement(in, &at, &item);
        if (kind == VT_BSTR) {
            if (SUCCEEDED(VariantChangeType(&converted, &item, 0, VT_BSTR)))
                SafeArrayPutElement(array, &to, converted.bstrVal);
        } else if (kind == VT_I4) {
            LONG n = item.vt == VT_UI8 ? (LONG)item.ullVal : (LONG)item.llVal;
            SafeArrayPutElement(array, &to, &n);
        } else if (kind == VT_BOOL) {
            SafeArrayPutElement(array, &to, &item.boolVal);
        } else {
            SafeArrayPutElement(array, &to, &item.dblVal);
        }
        VariantClear(&item);
        VariantClear(&converted);
    }
    out->vt = (VARTYPE)(kind | VT_ARRAY);
    out->parray = array;
}

/* ------------------------------------------------------------------ qualifiers */

static LONG default_flavor(const WCHAR *name)
{
    if (wb_eq(name, L"key"))
        return WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS | WBEM_FLAVOR_NOT_OVERRIDABLE;
    if (wb_eq(name, L"abstract") || wb_eq(name, L"Implemented") || wb_eq(name, L"in") || wb_eq(name, L"out") ||
        wb_eq(name, L"ID"))
        return WBEM_FLAVOR_DONT_PROPAGATE;
    return WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS;
}

static void parse_flavors(Parser *p, LONG *flavor)
{
    BOOL touched = FALSE;
    LONG f = 0;
    while (p->tok.kind == T_IDENT) {
        if (is_word(p, L"ToInstance")) {
            f |= WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE;
        } else if (is_word(p, L"ToSubclass")) {
            f |= WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS;
        } else if (is_word(p, L"Restricted")) {
            f &= ~3;
        } else if (is_word(p, L"DisableOverride")) {
            f |= WBEM_FLAVOR_NOT_OVERRIDABLE;
        } else if (is_word(p, L"EnableOverride")) {
            f &= ~WBEM_FLAVOR_NOT_OVERRIDABLE;
        } else if (is_word(p, L"Amended")) {
            f |= WBEM_FLAVOR_AMENDED;
        } else if (is_word(p, L"Translatable")) {
        } else {
            break;
        }
        touched = TRUE;
        lex(p);
    }
    if (touched)
        *flavor = f;
}

/* [name(value) : flavor, name{...}, name] */
static BOOL parse_qualifiers(Parser *p, WQuals *quals)
{
    if (!is_punct(p, '['))
        return TRUE;
    lex(p);
    for (;;) {
        if (p->tok.kind != T_IDENT) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            return FALSE;
        }
        WCHAR *name = wb_wcsdup(p->tok.text);
        lex(p);
        VARIANT raw, value;
        VariantInit(&raw);
        VariantInit(&value);
        wv_init(&value);
        BOOL ok = TRUE;
        if (is_punct(p, '(')) {
            lex(p);
            ok = parse_scalar(p, &raw) && expect_punct(p, ')');
        } else if (is_punct(p, '{')) {
            ok = parse_value(p, &raw);
        } else {
            raw.vt = VT_BOOL;
            raw.boolVal = VARIANT_TRUE;
        }
        LONG flavor = default_flavor(name);
        if (ok && is_punct(p, ':')) {
            lex(p);
            parse_flavors(p, &flavor);
        }
        if (ok) {
            simple_qualifier_value(&raw, &value);
            wq_set(quals, name, &value, flavor);
        }
        VariantClear(&raw);
        VariantClear(&value);
        wb_free(name);
        if (!ok)
            return FALSE;
        if (is_punct(p, ',')) {
            lex(p);
            continue;
        }
        break;
    }
    return expect_punct(p, ']');
}

/* ------------------------------------------------------------------ classes */

typedef struct TypeSpec {
    CIMTYPE type;
    WCHAR *target; /* the class of a reference or an embedded object. */
} TypeSpec;

/* a type: a primitive name, "Class ref", or the name of a class for an embedded object. */
static BOOL parse_type(Parser *p, TypeSpec *spec)
{
    memset(spec, 0, sizeof(*spec));
    if (p->tok.kind != T_IDENT) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    WCHAR *word = wb_wcsdup(p->tok.text);
    lex(p);
    CIMTYPE type = wv_cim_from_name(word);
    if (type == CIM_REFERENCE) {
        /* the older form: ref Class. */
        if (p->tok.kind == T_IDENT && !is_word(p, L"ref")) {
            spec->target = wb_wcsdup(p->tok.text);
            lex(p);
        }
        spec->type = CIM_REFERENCE;
        wb_free(word);
        return TRUE;
    }
    if (type != CIM_ILLEGAL && !(type == CIM_OBJECT && is_word(p, L"ref"))) {
        spec->type = type;
        wb_free(word);
        return TRUE;
    }
    if (is_word(p, L"ref")) {
        lex(p);
        spec->type = CIM_REFERENCE;
        spec->target = word;
        return TRUE;
    }
    spec->type = CIM_OBJECT;
    spec->target = word;
    return TRUE;
}

static void apply_type_qualifier(WProp *prop, const TypeSpec *spec)
{
    if (spec->type == CIM_REFERENCE || (spec->type == CIM_OBJECT && spec->target)) {
        WCHAR *text = wb_format(spec->type == CIM_REFERENCE ? L"ref:%s" : L"object:%s", spec->target ? spec->target : L"");
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(text);
        wq_set(&prop->quals, L"CIMTYPE", &v, WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE | WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS);
        VariantClear(&v);
        wb_free(text);
    }
}

typedef struct MemberDecl {
    WQuals quals;
    TypeSpec type;
    WCHAR *name;
    BOOL is_array;
    BOOL is_method;
    VARIANT value;
    BOOL has_value;
    WObj *in, *out;
} MemberDecl;

static void member_free(MemberDecl *m)
{
    wq_free(&m->quals);
    wb_free(m->type.target);
    wb_free(m->name);
    VariantClear(&m->value);
    wo_free(m->in);
    wo_free(m->out);
    memset(m, 0, sizeof(*m));
}

/* the parameters of a method into its in and out signatures. */
static BOOL parse_parameters(Parser *p, MemberDecl *m)
{
    m->in = wo_new(TRUE, L"__PARAMETERS", nullptr);
    m->out = wo_new(TRUE, L"__PARAMETERS", nullptr);
    LONG id = 0;
    if (!is_punct(p, ')')) {
        for (;;) {
            WQuals quals;
            wq_init(&quals);
            TypeSpec type;
            if (!parse_qualifiers(p, &quals) || !parse_type(p, &type)) {
                wq_free(&quals);
                return FALSE;
            }
            if (p->tok.kind != T_IDENT) {
                fail(p, WBEM_E_INVALID_SYNTAX);
                wq_free(&quals);
                wb_free(type.target);
                return FALSE;
            }
            WCHAR *name = wb_wcsdup(p->tok.text);
            lex(p);
            CIMTYPE ctype = type.type;
            if (is_punct(p, '[')) {
                lex(p);
                while (p->tok.kind == T_INT)
                    lex(p);
                if (!expect_punct(p, ']')) {
                    wq_free(&quals);
                    wb_free(type.target);
                    wb_free(name);
                    return FALSE;
                }
                ctype |= CIM_FLAG_ARRAY;
            }
            BOOL is_in = wq_find(&quals, L"in") != nullptr, is_out = wq_find(&quals, L"out") != nullptr;
            if (!is_in && !is_out) {
                VARIANT t;
                VariantInit(&t);
                t.vt = VT_BOOL;
                t.boolVal = VARIANT_TRUE;
                wq_set(&quals, L"in", &t, WBEM_FLAVOR_DONT_PROPAGATE);
                is_in = TRUE;
            }
            VARIANT idv;
            VariantInit(&idv);
            idv.vt = VT_I4;
            idv.lVal = id++;
            wq_set(&quals, L"ID", &idv, WBEM_FLAVOR_DONT_PROPAGATE);
            for (int side = 0; side < 2; side++) {
                if (!(side ? is_out : is_in))
                    continue;
                WObj *target = side ? m->out : m->in;
                WProp *prop = wo_add_prop(target, name, ctype, nullptr, nullptr);
                wq_free(&prop->quals);
                wq_copy(&prop->quals, &quals);
                apply_type_qualifier(prop, &type);
            }
            wq_free(&quals);
            wb_free(type.target);
            wb_free(name);
            if (is_punct(p, ',')) {
                lex(p);
                continue;
            }
            break;
        }
    }
    return expect_punct(p, ')');
}

static BOOL parse_member(Parser *p, MemberDecl *m)
{
    memset(m, 0, sizeof(*m));
    wv_init(&m->value);
    if (!parse_qualifiers(p, &m->quals))
        return FALSE;
    BOOL is_void = is_word(p, L"void");
    if (is_void)
        lex(p);
    else if (!parse_type(p, &m->type))
        return FALSE;
    if (p->tok.kind != T_IDENT) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    m->name = wb_wcsdup(p->tok.text);
    lex(p);
    if (is_punct(p, '(')) {
        lex(p);
        m->is_method = TRUE;
        if (!parse_parameters(p, m))
            return FALSE;
        if (!is_void) {
            WProp *ret = wo_add_prop(m->out, L"ReturnValue", m->type.type, nullptr, nullptr);
            VARIANT t;
            VariantInit(&t);
            t.vt = VT_BOOL;
            t.boolVal = VARIANT_TRUE;
            wq_set(&ret->quals, L"out", &t, WBEM_FLAVOR_DONT_PROPAGATE);
            apply_type_qualifier(ret, &m->type);
        }
        return expect_punct(p, ';');
    }
    if (is_punct(p, '[')) {
        lex(p);
        while (p->tok.kind == T_INT)
            lex(p);
        if (!expect_punct(p, ']'))
            return FALSE;
        m->is_array = TRUE;
    }
    if (is_punct(p, '=')) {
        lex(p);
        if (!parse_value(p, &m->value))
            return FALSE;
        m->has_value = TRUE;
    }
    return expect_punct(p, ';');
}

static HRESULT merge_quals(WQuals *dst, const WQuals *src)
{
    for (UINT i = 0; i < src->count; i++) {
        WQual *old = wq_find(dst, src->items[i].name);
        if (old && (old->flavor & WBEM_FLAVOR_ORIGIN_PROPAGATED) && (old->flavor & WBEM_FLAVOR_NOT_OVERRIDABLE))
            continue;
        wq_set(dst, src->items[i].name, &src->items[i].value, src->items[i].flavor & ~WBEM_FLAVOR_MASK_ORIGIN);
    }
    return S_OK;
}

static BOOL parse_class(Parser *p, WQuals *class_quals)
{
    /* the current token is the word class. */
    lex(p);
    if (p->tok.kind != T_IDENT) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    WCHAR *name = wb_wcsdup(p->tok.text);
    WCHAR *super = nullptr;
    lex(p);
    if (is_punct(p, ':')) {
        lex(p);
        if (p->tok.kind != T_IDENT) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            wb_free(name);
            return FALSE;
        }
        super = wb_wcsdup(p->tok.text);
        lex(p);
    }
    WObj *parent = nullptr;
    if (super) {
        parent = wr_find_class(p->ns, super);
        if (!parent) {
            fail(p, WBEM_E_INVALID_SUPERCLASS);
            wb_free(name);
            wb_free(super);
            return FALSE;
        }
    }
    WObj *existing = wr_find_class(p->ns, name);
    WObj *c = wo_derive(parent, name);
    wb_free(c->ns);
    c->ns = wb_wcsdup(p->ns->path);
    c->builtin = p->builtin;
    merge_quals(&c->quals, class_quals);
    /* a class that exists keeps the order of its own properties when a mof updates it. */
    (void)existing;
    BOOL ok = expect_punct(p, '{');
    while (ok && !is_punct(p, '}')) {
        if (p->tok.kind == T_EOF) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            ok = FALSE;
            break;
        }
        MemberDecl m;
        if (!parse_member(p, &m)) {
            member_free(&m);
            ok = FALSE;
            break;
        }
        if (m.is_method) {
            WMeth *meth = wo_add_meth(c, m.name, m.in, m.out, c->name);
            m.in = m.out = nullptr;
            wq_free(&meth->quals);
            wq_copy(&meth->quals, &m.quals);
        } else {
            CIMTYPE type = m.type.type | (m.is_array ? CIM_FLAG_ARRAY : 0);
            VARIANT value;
            wv_init(&value);
            HRESULT hr = S_OK;
            if (m.has_value)
                hr = wv_coerce(&value, &m.value, type);
            if (FAILED(hr)) {
                fail(p, hr);
                member_free(&m);
                ok = FALSE;
                VariantClear(&value);
                break;
            }
            WProp *old = wo_prop(c, m.name);
            BOOL inherited = old != nullptr;
            WQuals inherited_quals;
            wq_init(&inherited_quals);
            if (inherited)
                wq_copy(&inherited_quals, &old->quals);
            VARIANT kept;
            wv_init(&kept);
            if (inherited && !m.has_value)
                wv_copy(&kept, &old->value);
            WProp *prop = wo_add_prop(c, m.name, type, m.has_value ? &value : (inherited ? &kept : nullptr), c->name);
            VariantClear(&kept);
            if (inherited) {
                /* an override keeps what the parent's property passed on. */
                wb_free(prop->origin);
                prop->origin = wb_wcsdup(parent && wo_prop(parent, m.name) ? wo_prop(parent, m.name)->origin : c->name);
                wq_free(&prop->quals);
                prop->quals = inherited_quals;
            }
            merge_quals(&prop->quals, &m.quals);
            apply_type_qualifier(prop, &m.type);
            VariantClear(&value);
        }
        member_free(&m);
    }
    if (ok)
        ok = expect_punct(p, '}');
    if (ok && is_punct(p, ';'))
        lex(p);
    if (ok) {
        LONG flags = p->class_flags;
        if (!(p->options & WBEM_FLAG_CHECK_ONLY)) {
            HRESULT hr = wr_put_class(p->ns, c, flags);
            if (FAILED(hr)) {
                fail(p, hr);
                ok = FALSE;
            }
        } else {
            WCHAR *grown = wb_format(L"%ls|%ls|", p->defined ? p->defined : L"", name);
            wb_free(p->defined);
            p->defined = grown;
        }
    }
    wo_free(c);
    wb_free(name);
    wb_free(super);
    return ok;
}

/* ------------------------------------------------------------------ instances */

static BOOL parse_instance(Parser *p, WQuals *quals)
{
    /* the current token is the word instance. */
    lex(p);
    if (!is_word(p, L"of")) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    lex(p);
    if (p->tok.kind != T_IDENT) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    WCHAR *name = wb_wcsdup(p->tok.text);
    lex(p);
    WCHAR *alias = nullptr;
    if (is_word(p, L"as")) {
        lex(p);
        if (p->tok.kind != T_ALIAS) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            wb_free(name);
            return FALSE;
        }
        alias = wb_wcsdup(p->tok.text);
        lex(p);
    }
    WObj *class_object = wr_find_class(p->ns, name);
    if (!class_object && (p->options & WBEM_FLAG_CHECK_ONLY) && p->defined) {
        /* a class of this file is not stored when the file is only checked, so its instances are read for their syntax. */
        WCHAR *key = wb_format(L"|%ls|", name);
        BOOL known = wcsstr(p->defined, key) != nullptr;
        wb_free(key);
        if (known) {
            BOOL ok = expect_punct(p, '{');
            int depth = ok ? 1 : 0;
            while (ok && depth && p->tok.kind != T_EOF) {
                if (is_punct(p, '{'))
                    depth++;
                else if (is_punct(p, '}'))
                    depth--;
                lex(p);
            }
            if (ok && depth)
                fail(p, WBEM_E_INVALID_SYNTAX);
            if (ok && is_punct(p, ';'))
                lex(p);
            wb_free(name);
            wb_free(alias);
            return ok && !depth;
        }
    }
    if (!class_object) {
        fail(p, WBEM_E_INVALID_CLASS);
        wb_free(name);
        wb_free(alias);
        return FALSE;
    }
    WObj *instance = wo_spawn(class_object);
    wb_free(instance->ns);
    instance->ns = wb_wcsdup(p->ns->path);
    instance->builtin = p->builtin;
    BOOL ok = expect_punct(p, '{');
    while (ok && !is_punct(p, '}')) {
        if (p->tok.kind != T_IDENT) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            ok = FALSE;
            break;
        }
        WCHAR *prop_name = wb_wcsdup(p->tok.text);
        lex(p);
        WProp *prop = wo_prop(instance, prop_name);
        if (!prop) {
            fail(p, WBEM_E_INVALID_PROPERTY);
            wb_free(prop_name);
            ok = FALSE;
            break;
        }
        VARIANT raw, value;
        VariantInit(&raw);
        wv_init(&value);
        wv_init(&raw);
        if (ok && is_punct(p, '='))
            lex(p);
        else if (ok) {
            fail(p, WBEM_E_INVALID_SYNTAX);
            ok = FALSE;
        }
        if (ok)
            ok = parse_value(p, &raw);
        if (ok) {
            HRESULT hr = wv_coerce(&value, &raw, prop->type);
            if (FAILED(hr)) {
                fail(p, hr);
                ok = FALSE;
            } else {
                VariantClear(&prop->value);
                prop->value = value;
                wv_init(&value);
            }
        }
        if (ok)
            ok = expect_punct(p, ';');
        VariantClear(&raw);
        VariantClear(&value);
        wb_free(prop_name);
    }
    if (ok)
        ok = expect_punct(p, '}');
    if (ok && is_punct(p, ';'))
        lex(p);
    if (ok && !(p->options & WBEM_FLAG_CHECK_ONLY)) {
        HRESULT hr = wr_put_instance(p->ns, instance, p->instance_flags);
        if (FAILED(hr)) {
            fail(p, hr);
            ok = FALSE;
        }
    }
    if (ok && alias) {
        WCHAR *rel = wo_relpath(instance);
        set_alias(p, alias, rel ? rel : L"");
        wb_free(rel);
    }
    wo_free(instance);
    wb_free(name);
    wb_free(alias);
    (void)quals;
    return ok;
}

/* ------------------------------------------------------------------ pragmas */

static BOOL parse_pragma(Parser *p)
{
    lex(p);
    if (!is_word(p, L"pragma")) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    lex(p);
    if (p->tok.kind != T_IDENT) {
        fail(p, WBEM_E_INVALID_SYNTAX);
        return FALSE;
    }
    WCHAR *name = wb_wcsdup(p->tok.text);
    lex(p);
    BOOL ok = TRUE;
    VARIANT args[3];
    UINT count = 0;
    for (UINT i = 0; i < 3; i++)
        VariantInit(&args[i]);
    if (is_punct(p, '(')) {
        lex(p);
        while (ok && !is_punct(p, ')')) {
            if (count >= 3) {
                ok = FALSE;
            } else if (p->tok.kind == T_IDENT) {
                /* a flag such as NOFAIL stands for itself. */
                args[count].vt = VT_BSTR;
                args[count].bstrVal = SysAllocString(p->tok.text);
                ok = args[count++].bstrVal != nullptr;
                lex(p);
            } else {
                ok = parse_scalar(p, &args[count++]);
            }
            if (ok && is_punct(p, ','))
                lex(p);
            else if (ok && !is_punct(p, ')'))
                ok = FALSE;
        }
        if (ok)
            lex(p);
        else
            fail(p, WBEM_E_INVALID_SYNTAX);
    }
    if (ok) {
        if (wb_eq(name, L"namespace")) {
            if (count != 1 || args[0].vt != VT_BSTR) {
                fail(p, WBEM_E_INVALID_SYNTAX);
            } else {
                WNs *ns = wr_find_ns(args[0].bstrVal);
                if (!ns && p->builtin)
                    ns = wr_create_ns(args[0].bstrVal);
                if (!ns)
                    fail(p, WBEM_E_INVALID_NAMESPACE);
                else
                    p->ns = ns;
            }
        } else if (wb_eq(name, L"mimesd")) {
            if (count == 1 && args[0].vt == VT_BSTR) {
                wb_free(p->ns->sddl);
                p->ns->sddl = wb_wcsdup(args[0].bstrVal);
            }
        } else if (wb_eq(name, L"classflags") || wb_eq(name, L"instanceflags")) {
            /* only the update modes are read; the names of the flags are those of mofcomp. */
            LONG f = 0;
            for (UINT i = 0; i < count; i++) {
                if (args[i].vt != VT_BSTR)
                    continue;
                if (wb_eq(args[i].bstrVal, L"updateonly"))
                    f = WBEM_FLAG_UPDATE_ONLY;
                else if (wb_eq(args[i].bstrVal, L"createonly"))
                    f = WBEM_FLAG_CREATE_ONLY;
                else if (wb_eq(args[i].bstrVal, L"forceupdate"))
                    f = WBEM_FLAG_UPDATE_FORCE_MODE;
                else if (wb_eq(args[i].bstrVal, L"safeupdate"))
                    f = WBEM_FLAG_UPDATE_SAFE_MODE;
            }
            if (wb_eq(name, L"classflags"))
                p->class_flags = f;
            else
                p->instance_flags = f;
        } else if (wb_eq(name, L"deleteclass")) {
            if (count >= 1 && args[0].vt == VT_BSTR && !(p->options & WBEM_FLAG_CHECK_ONLY))
                wr_delete_class(p->ns, args[0].bstrVal);
        } else if (wb_eq(name, L"deleteinstance")) {
            if (count >= 1 && args[0].vt == VT_BSTR && !(p->options & WBEM_FLAG_CHECK_ONLY)) {
                WPath path;
                wp_init(&path);
                if (SUCCEEDED(wp_parse(&path, args[0].bstrVal, WBEMPATH_CREATE_ACCEPT_ALL))) {
                    WCHAR *rel = wp_text(&path, WBEMPATH_GET_RELATIVE_ONLY);
                    wr_delete_instance(p->ns, rel);
                    wb_free(rel);
                }
                wp_free(&path);
            }
        }
        /* autorecover, include, locale, amendment and the others change nothing here. */
    }
    for (UINT i = 0; i < 3; i++)
        VariantClear(&args[i]);
    wb_free(name);
    return SUCCEEDED(p->error);
}

/* ------------------------------------------------------------------ the driver */

HRESULT wmof_compile(const WCHAR *text, SIZE_T length, const WCHAR *default_ns, LONG options, LONG class_flags,
                     LONG instance_flags, WMofResult *info, BOOL builtin)
{
    Parser p;
    memset(&p, 0, sizeof(p));
    p.src = text;
    p.length = length;
    p.line = 1;
    p.options = options;
    p.class_flags = class_flags;
    p.instance_flags = instance_flags;
    p.builtin = builtin;
    wr_lock();
    wr_write_begin();
    p.ns = wr_find_ns(default_ns ? default_ns : L"root\\default");
    if (!p.ns && builtin && default_ns)
        p.ns = wr_create_ns(default_ns);
    if (!p.ns) {
        p.error = WBEM_E_INVALID_NAMESPACE;
    } else {
        lex(&p);
        while (SUCCEEDED(p.error) && p.tok.kind != T_EOF) {
            p.first_line = p.tok.line;
            if (is_punct(&p, '#')) {
                parse_pragma(&p);
                continue;
            }
            WQuals quals;
            wq_init(&quals);
            if (!parse_qualifiers(&p, &quals)) {
                wq_free(&quals);
                break;
            }
            p.object_number++;
            if (is_word(&p, L"class"))
                parse_class(&p, &quals);
            else if (is_word(&p, L"instance"))
                parse_instance(&p, &quals);
            else
                fail(&p, WBEM_E_INVALID_SYNTAX);
            wq_free(&quals);
        }
    }
    wr_write_end();
    wr_unlock();
    if (info) {
        memset(info, 0, sizeof(*info));
        info->result = p.error;
        info->first_line = p.first_line;
        info->last_line = p.error_line ? p.error_line : p.tok.line;
        info->object_number = p.object_number;
        info->phase = FAILED(p.error) ? 1 : 0;
    }
    tok_free(&p.tok);
    for (UINT i = 0; i < p.alias_count; i++) {
        wb_free(p.aliases[i].name);
        wb_free(p.aliases[i].path);
    }
    wb_free(p.aliases);
    wb_free(p.defined);
    return p.error;
}
