#include "parse.h"

#define MAX_NESTING 200

HlExpr *parse_unary(Parser *p);
static HlExpr *parse_binary(Parser *p, int min_precedence);

HlExpr *new_expr(Parser *p, uint8_t kind, HlType *type, HlLoc loc)
{
    HlExpr *e = hl_alloc(p->h, sizeof(HlExpr));
    e->kind = kind;
    e->type = type;
    e->loc = loc;
    return e;
}

HlExpr *error_expr(Parser *p, HlLoc loc) { return new_expr(p, E_ERROR, hl_type_error(p->h), loc); }

static bool is_error(const HlExpr *e) { return e->kind == E_ERROR || e->type->kind == TY_ERROR; }

static HlExpr *const_expr(Parser *p, HlType *type, HlLoc loc)
{
    HlExpr *e = new_expr(p, E_CONST, type, loc);
    uint32_t n = hl_type_components(type);
    e->values = hl_alloc(p->h, (n ? n : 1) * sizeof(HlValue));
    return e;
}

HlExpr *const_int(Parser *p, uint8_t base, int32_t v, HlLoc loc)
{
    HlExpr *e = const_expr(p, hl_type_scalar(p->h, base), loc);
    e->values[0].i = v;
    return e;
}

HlExpr *const_float(Parser *p, float v, HlLoc loc)
{
    HlExpr *e = const_expr(p, hl_type_scalar(p->h, BT_FLOAT), loc);
    e->values[0].f = v;
    return e;
}

/* ------------------------------------------------------------------ values */

static float value_float(HlValue v, uint8_t base)
{
    switch (hl_base_storage(base)) {
    case BT_BOOL:
        return v.u ? 1.0f : 0.0f;
    case BT_INT:
        return (float)v.i;
    case BT_UINT:
        return (float)v.u;
    default:
        return v.f;
    }
}

/* ftoi and ftou clamp and turn NaN into 0, as the gpu does. */
static int32_t float_to_int(float f)
{
    if (f != f)
        return 0;
    if (f >= 2147483648.0f)
        return 0x7FFFFFFF;
    if (f <= -2147483648.0f)
        return (int32_t)0x80000000u;
    return (int32_t)f;
}

static uint32_t float_to_uint(float f)
{
    if (f != f || f <= 0)
        return 0;
    if (f >= 4294967296.0f)
        return 0xFFFFFFFFu;
    return (uint32_t)f;
}

static HlValue convert_value(HlValue v, uint8_t from, uint8_t to)
{
    uint8_t fs = hl_base_storage(from), ts = hl_base_storage(to);
    HlValue r = v;
    if (fs == BT_DOUBLE)
        fs = BT_FLOAT;
    if (ts == BT_DOUBLE)
        ts = BT_FLOAT;
    if (fs == ts)
        return r;
    switch (ts) {
    case BT_BOOL:
        r.u = fs == BT_FLOAT ? v.f != 0 : v.u != 0;
        break;
    case BT_INT:
        r.i = fs == BT_FLOAT ? float_to_int(v.f) : fs == BT_BOOL ? (v.u ? 1 : 0) : v.i;
        break;
    case BT_UINT:
        r.u = fs == BT_FLOAT ? float_to_uint(v.f) : fs == BT_BOOL ? (v.u ? 1u : 0u) : v.u;
        break;
    default:
        r.f = value_float(v, from);
        break;
    }
    return r;
}

/* ------------------------------------------------------------------ conversions */

static uint8_t promote_base(uint8_t a, uint8_t b)
{
    if (a == b)
        return a;
    bool la = a == BT_LIT_INT || a == BT_LIT_FLOAT, lb = b == BT_LIT_INT || b == BT_LIT_FLOAT;
    if (la && lb)
        return a == BT_LIT_FLOAT || b == BT_LIT_FLOAT ? BT_LIT_FLOAT : BT_LIT_INT;
    if (la || lb) {
        uint8_t lit = la ? a : b, other = la ? b : a;
        if (lit == BT_LIT_FLOAT && !hl_base_float(other))
            return BT_FLOAT;
        return other == BT_BOOL ? (lit == BT_LIT_FLOAT ? BT_FLOAT : BT_INT) : other;
    }
    return a > b ? a : b;
}

static uint32_t numeric_count(const HlType *t) { return hl_type_components(t); }

HlType *arith_type(Parser *p, HlType *a, HlType *b, HlLoc loc, bool warn)
{
    Hlsl *h = p->h;
    if (!hl_type_numeric_p(a) || !hl_type_numeric_p(b))
        return hl_type_error(h);
    uint8_t base = promote_base(a->base, b->base);
    if (a->kind == TY_SCALAR)
        return hl_type_with_base(h, b, base);
    if (b->kind == TY_SCALAR)
        return hl_type_with_base(h, a, base);
    if (a->kind == TY_VECTOR && b->kind == TY_VECTOR) {
        uint8_t n = a->cols < b->cols ? a->cols : b->cols;
        if (a->cols != b->cols && warn)
            hl_warning(h, loc, 3206, "implicit truncation of vector type");
        return hl_type_vector(h, base, n);
    }
    if (a->kind == TY_MATRIX && b->kind == TY_MATRIX) {
        uint8_t r = a->rows < b->rows ? a->rows : b->rows, c = a->cols < b->cols ? a->cols : b->cols;
        if ((a->rows != b->rows || a->cols != b->cols) && warn)
            hl_warning(h, loc, 3206, "implicit truncation of vector type");
        return hl_type_matrix(h, base, r, c);
    }
    /* a vector and a matrix of one row or column work as vectors. */
    const HlType *m = a->kind == TY_MATRIX ? a : b, *v = a->kind == TY_MATRIX ? b : a;
    if (m->rows == 1 || m->cols == 1) {
        uint8_t mc = (uint8_t)(m->rows * m->cols);
        uint8_t n = mc < v->cols ? mc : v->cols;
        if (mc != v->cols && warn)
            hl_warning(h, loc, 3206, "implicit truncation of vector type");
        return hl_type_vector(h, base, n);
    }
    if (numeric_count(m) == numeric_count(v))
        return hl_type_with_base(h, m, base);
    return hl_type_error(h);
}

bool can_convert(Parser *p, const HlType *from, const HlType *to, bool explicit_cast)
{
    if (from->kind == TY_ERROR || to->kind == TY_ERROR)
        return true;
    if (hl_type_equal(from, to))
        return true;
    if (to->kind == TY_VOID)
        return explicit_cast;
    if (hl_type_numeric_p(from) && hl_type_numeric_p(to)) {
        if (from->kind == TY_SCALAR)
            return true;
        if (to->kind == TY_SCALAR)
            return true;
        if (from->kind == TY_VECTOR && to->kind == TY_VECTOR)
            return to->cols <= from->cols;
        if (from->kind == TY_MATRIX && to->kind == TY_MATRIX)
            return to->rows <= from->rows && to->cols <= from->cols;
        uint32_t fc = numeric_count(from), tc = numeric_count(to);
        if (from->kind == TY_MATRIX && (from->rows == 1 || from->cols == 1))
            return tc <= fc;
        if (to->kind == TY_MATRIX && (to->rows == 1 || to->cols == 1))
            return tc <= fc;
        return fc == tc || (explicit_cast && tc <= fc);
    }
    bool from_aggregate = from->kind == TY_ARRAY || from->kind == TY_STRUCT;
    bool to_aggregate = to->kind == TY_ARRAY || to->kind == TY_STRUCT;
    if ((from_aggregate || to_aggregate) && (hl_type_numeric_p(from) || from_aggregate) &&
        (hl_type_numeric_p(to) || to_aggregate)) {
        if (hl_type_has_object(from) || hl_type_has_object(to))
            return false;
        uint32_t fc = hl_type_components(from), tc = hl_type_components(to);
        if (from->kind == TY_SCALAR)
            return explicit_cast || to->kind == TY_ARRAY;
        if (explicit_cast)
            return fc >= tc;
        /* arrays and structs of the same layout convert silently. */
        return fc == tc && from_aggregate && to_aggregate;
    }
    if (from->kind == TY_OBJECT && to->kind == TY_OBJECT)
        return from->object == to->object && (!from->elem || !to->elem || hl_type_equal(from->elem, to->elem));
    return false;
}

static bool truncates(const HlType *from, const HlType *to)
{
    if (!hl_type_numeric_p(from) || !hl_type_numeric_p(to) || from->kind == TY_SCALAR)
        return false;
    return numeric_count(to) < numeric_count(from);
}

HlExpr *convert(Parser *p, HlExpr *e, HlType *to, HlLoc loc, bool explicit_cast)
{
    Hlsl *h = p->h;
    if (is_error(e) || to->kind == TY_ERROR)
        return e;
    if (hl_type_equal(e->type, to) && e->type->kind != TY_MATRIX) {
        if (e->kind == E_CONST && e->type != to) {
            HlExpr *c = new_expr(p, E_CONST, to, e->loc);
            c->values = e->values;
            return c;
        }
        return e;
    }
    if (hl_type_equal(e->type, to) && e->type->modifiers == to->modifiers)
        return e;
    if (!can_convert(p, e->type, to, explicit_cast)) {
        hl_error(h, loc, 3017, "cannot %sconvert from '%s' to '%s'", explicit_cast ? "" : "implicitly ",
                 hl_type_name(h, e->type), hl_type_name(h, to));
        return error_expr(p, loc);
    }
    if (!explicit_cast && truncates(e->type, to))
        hl_warning(h, loc, 3206, "implicit truncation of vector type");
    if (!explicit_cast && e->type->base == BT_DOUBLE && hl_type_numeric_p(to) && to->base != BT_DOUBLE &&
        hl_type_numeric_p(e->type))
        hl_warning(h, loc, 3205, "conversion from larger type to smaller, possible loss of data");
    HlExpr *c = new_expr(p, E_CAST, to, loc);
    c->a = e;
    c->explicit_cast = explicit_cast;
    return fold(p, c);
}

/* ------------------------------------------------------------------ folding */

static bool all_const(HlExpr **args, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (args[i]->kind != E_CONST)
            return false;
    return true;
}

/* the value of flat component i of a constant, as the base of the target. */
static HlValue const_component(const HlExpr *e, uint32_t i, uint8_t to)
{
    uint32_t n = hl_type_components(e->type);
    uint32_t at = n ? (e->type->kind == TY_SCALAR ? 0 : i % n) : 0;
    return convert_value(e->values[at], hl_type_component_base(e->type, at), to);
}

static HlExpr *fold_cast(Parser *p, HlExpr *e)
{
    HlExpr *a = e->a;
    HlType *t = e->type;
    if (!hl_type_numeric_p(t) && t->kind != TY_ARRAY && t->kind != TY_STRUCT)
        return e;
    if (hl_type_has_object(t))
        return e;
    HlExpr *c = const_expr(p, t, e->loc);
    uint32_t n = hl_type_components(t);
    const HlType *from = a->type;
    if (from->kind == TY_MATRIX && t->kind == TY_MATRIX) {
        for (uint32_t r = 0; r < t->rows; r++)
            for (uint32_t col = 0; col < t->cols; col++)
                c->values[r * t->cols + col] =
                    convert_value(a->values[r * from->cols + col], from->base, t->base);
        return c;
    }
    for (uint32_t i = 0; i < n; i++)
        c->values[i] = const_component(a, i, hl_type_component_base(t, i));
    return c;
}

static HlValue fold_binary_value(Parser *p, uint8_t op, uint8_t base, HlValue a, HlValue b, HlLoc loc, bool *ok)
{
    HlValue r = {};
    uint8_t s = hl_base_storage(base);
    if (s == BT_DOUBLE)
        s = BT_FLOAT;
    if (s == BT_BOOL)
        s = BT_INT;
    switch (op) {
    case OPR_ADD:
        if (s == BT_FLOAT)
            r.f = a.f + b.f;
        else
            r.u = a.u + b.u;
        break;
    case OPR_SUB:
        if (s == BT_FLOAT)
            r.f = a.f - b.f;
        else
            r.u = a.u - b.u;
        break;
    case OPR_MUL:
        if (s == BT_FLOAT)
            r.f = a.f * b.f;
        else
            r.u = a.u * b.u;
        break;
    case OPR_DIV:
        if (s == BT_FLOAT) {
            r.f = a.f / b.f;
        } else if (!b.u) {
            hl_error(p->h, loc, 4008, "division by zero");
            *ok = false;
        } else if (s == BT_UINT) {
            r.u = a.u / b.u;
        } else {
            r.i = (b.i == -1) ? (int32_t)(0 - a.u) : a.i / b.i;
        }
        break;
    case OPR_MOD:
        if (s == BT_FLOAT) {
            float q = a.f / b.f;
            float f = q - (float)(int64_t)q;
            r.f = f * b.f;
        } else if (!b.u) {
            hl_error(p->h, loc, 4008, "modulo by zero");
            *ok = false;
        } else if (s == BT_UINT) {
            r.u = a.u % b.u;
        } else {
            r.i = b.i == -1 ? 0 : a.i % b.i;
        }
        break;
    case OPR_SHL:
        r.u = a.u << (b.u & 31);
        break;
    case OPR_SHR:
        if (s == BT_UINT)
            r.u = a.u >> (b.u & 31);
        else
            r.i = a.i >> (b.u & 31);
        break;
    case OPR_BITAND:
        r.u = a.u & b.u;
        break;
    case OPR_BITOR:
        r.u = a.u | b.u;
        break;
    case OPR_BITXOR:
        r.u = a.u ^ b.u;
        break;
    case OPR_LT:
        r.u = s == BT_FLOAT ? a.f < b.f : s == BT_UINT ? a.u < b.u : a.i < b.i;
        break;
    case OPR_GT:
        r.u = s == BT_FLOAT ? a.f > b.f : s == BT_UINT ? a.u > b.u : a.i > b.i;
        break;
    case OPR_LE:
        r.u = s == BT_FLOAT ? a.f <= b.f : s == BT_UINT ? a.u <= b.u : a.i <= b.i;
        break;
    case OPR_GE:
        r.u = s == BT_FLOAT ? a.f >= b.f : s == BT_UINT ? a.u >= b.u : a.i >= b.i;
        break;
    case OPR_EQ:
        r.u = s == BT_FLOAT ? a.f == b.f : a.u == b.u;
        break;
    case OPR_NE:
        r.u = s == BT_FLOAT ? a.f != b.f : a.u != b.u;
        break;
    case OPR_AND:
        r.u = a.u && b.u;
        break;
    case OPR_OR:
        r.u = a.u || b.u;
        break;
    default:
        *ok = false;
    }
    return r;
}

static bool comparison(uint8_t op) { return op >= OPR_LT && op <= OPR_NE; }

HlExpr *fold(Parser *p, HlExpr *e)
{
    switch (e->kind) {
    case E_CAST:
        if (e->a->kind == E_CONST)
            return fold_cast(p, e);
        return e;
    case E_UNARY: {
        if (e->a->kind != E_CONST || !hl_type_numeric_p(e->type))
            return e;
        HlExpr *c = const_expr(p, e->type, e->loc);
        uint32_t n = hl_type_components(e->type);
        uint8_t s = hl_base_storage(e->type->base);
        for (uint32_t i = 0; i < n; i++) {
            HlValue v = const_component(e->a, i, e->type->base);
            switch (e->op) {
            case OPR_NEG:
                if (s == BT_FLOAT || s == BT_DOUBLE)
                    v.f = -v.f;
                else
                    v.u = 0 - v.u;
                break;
            case OPR_NOT:
                v.u = !v.u;
                break;
            case OPR_BITNOT:
                v.u = ~v.u;
                break;
            default:
                break;
            }
            c->values[i] = v;
        }
        return c;
    }
    case E_BINARY: {
        if (e->a->kind != E_CONST || e->b->kind != E_CONST || !hl_type_numeric_p(e->type))
            return e;
        /* comparisons and logic compute in the operands' common type. */
        uint8_t base = e->type->base;
        if (comparison(e->op))
            base = promote_base(e->a->type->base, e->b->type->base);
        else if (e->op == OPR_AND || e->op == OPR_OR)
            base = BT_BOOL;
        HlExpr *c = const_expr(p, e->type, e->loc);
        uint32_t n = hl_type_components(e->type);
        bool ok = true;
        for (uint32_t i = 0; i < n && ok; i++) {
            HlValue a = const_component(e->a, i, base), b = const_component(e->b, i, base);
            c->values[i] = fold_binary_value(p, e->op, base, a, b, e->loc, &ok);
        }
        return ok ? c : e;
    }
    case E_TERNARY: {
        if (e->a->kind != E_CONST || e->b->kind != E_CONST || e->c->kind != E_CONST || !hl_type_numeric_p(e->type))
            return e;
        HlExpr *c = const_expr(p, e->type, e->loc);
        uint32_t n = hl_type_components(e->type);
        for (uint32_t i = 0; i < n; i++) {
            HlValue cond = const_component(e->a, i, BT_BOOL);
            c->values[i] = const_component(cond.u ? e->b : e->c, i, e->type->base);
        }
        return c;
    }
    case E_SWIZZLE: {
        if (e->a->kind != E_CONST)
            return e;
        HlExpr *c = const_expr(p, e->type, e->loc);
        uint32_t n = hl_type_components(e->type);
        for (uint32_t i = 0; i < n; i++)
            c->values[i] = e->a->values[e->a->type->kind == TY_SCALAR ? 0 : e->swizzle[i]];
        return c;
    }
    case E_CONSTRUCT: {
        if (!all_const(e->args, e->arg_count) || !hl_type_numeric_p(e->type))
            return e;
        HlExpr *c = const_expr(p, e->type, e->loc);
        uint32_t at = 0, n = hl_type_components(e->type);
        for (uint32_t i = 0; i < e->arg_count; i++) {
            uint32_t m = hl_type_components(e->args[i]->type);
            for (uint32_t j = 0; j < m && at < n; j++)
                c->values[at++] = const_component(e->args[i], j, e->type->base);
        }
        if (at == 1 && n > 1)
            for (; at < n; at++)
                c->values[at] = c->values[0];
        return c;
    }
    case E_INDEX: {
        if (e->a->kind != E_CONST || e->b->kind != E_CONST || !hl_type_numeric_p(e->type))
            return e;
        uint32_t index = const_component(e->b, 0, BT_UINT).u;
        uint32_t n = hl_type_components(e->type);
        if ((uint64_t)(index + 1) * n > hl_type_components(e->a->type))
            return e;
        HlExpr *c = const_expr(p, e->type, e->loc);
        for (uint32_t i = 0; i < n; i++)
            c->values[i] = e->a->values[index * n + i];
        return c;
    }
    default:
        return e;
    }
}

bool const_uint(Parser *p, HlExpr *e, uint32_t *out, const char *what)
{
    if (is_error(e))
        return false;
    if (e->kind != E_CONST || !hl_type_numeric_p(e->type) || hl_type_components(e->type) < 1) {
        hl_error(p->h, e->loc, 3058, "%s must be a literal scalar expression", what);
        return false;
    }
    HlValue v = const_component(e, 0, BT_INT);
    *out = v.u;
    return true;
}

/* ------------------------------------------------------------------ literals */

static double pow10_double(int e)
{
    double r = 1, b = 10;
    bool neg = e < 0;
    unsigned u = neg ? (unsigned)-e : (unsigned)e;
    while (u) {
        if (u & 1)
            r *= b;
        b *= b;
        u >>= 1;
    }
    return neg ? 1 / r : r;
}

static HlExpr *number(Parser *p, HlToken *t)
{
    Hlsl *h = p->h;
    const char *s = t->text, *end = t->text + t->length;
    bool is_float = false;
    for (const char *q = s; q < end; q++)
        if (*q == '.' || ((*q == 'e' || *q == 'E') && !(end - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))))
            is_float = true;
    if (!is_float) {
        uint64_t v = 0;
        unsigned base = 10;
        const char *q = s;
        if (end - q > 2 && q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
            base = 16, q += 2;
        else if (end - q > 1 && q[0] == '0')
            base = 8, q++;
        bool overflow = false;
        for (; q < end; q++) {
            unsigned d;
            char c = *q;
            if (c >= '0' && c <= '9')
                d = (unsigned)(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f')
                d = (unsigned)(c - 'a' + 10);
            else if (base == 16 && c >= 'A' && c <= 'F')
                d = (unsigned)(c - 'A' + 10);
            else
                break;
            if (d >= base)
                break;
            v = v * base + d;
            if (v > 0xFFFFFFFFull)
                overflow = true;
        }
        uint8_t type = BT_LIT_INT;
        bool suffix_ok = true;
        for (; q < end; q++) {
            if (*q == 'u' || *q == 'U')
                type = BT_UINT;
            else if (*q == 'l' || *q == 'L')
                ;
            else
                suffix_ok = false;
        }
        if (!suffix_ok) {
            hl_error(h, t->loc, 3000, "syntax error: invalid integer constant '%s'", hl_token_text(h, t));
            return error_expr(p, t->loc);
        }
        if (overflow)
            hl_warning(h, t->loc, 3051, "integer constant overflow, truncated");
        return const_int(p, type, (int32_t)(uint32_t)v, t->loc);
    }
    /* digits into a 19 digit mantissa and a decimal exponent, then to float through double. */
    uint64_t mantissa = 0;
    int exp10 = 0, digits = 0;
    const char *q = s;
    bool dot = false;
    for (; q < end; q++) {
        if (*q == '.') {
            if (dot)
                break;
            dot = true;
            continue;
        }
        if (*q < '0' || *q > '9')
            break;
        if (digits < 19) {
            if (mantissa || *q != '0')
                digits++;
            mantissa = mantissa * 10 + (uint64_t)(*q - '0');
            if (dot)
                exp10--;
        } else if (!dot) {
            exp10++;
        }
    }
    if (q < end && (*q == 'e' || *q == 'E')) {
        q++;
        bool neg = false;
        if (q < end && (*q == '+' || *q == '-'))
            neg = *q++ == '-';
        int e = 0;
        while (q < end && *q >= '0' && *q <= '9') {
            if (e < 10000)
                e = e * 10 + (*q - '0');
            q++;
        }
        exp10 += neg ? -e : e;
    }
    uint8_t type = BT_LIT_FLOAT;
    if (q < end && (*q == 'f' || *q == 'F'))
        type = BT_FLOAT, q++;
    else if (q < end && (*q == 'h' || *q == 'H'))
        type = BT_HALF, q++;
    else if (q < end && (*q == 'l' || *q == 'L'))
        type = BT_DOUBLE, q++;
    if (q != end) {
        hl_error(h, t->loc, 3000, "syntax error: invalid floating point constant '%s'", hl_token_text(h, t));
        return error_expr(p, t->loc);
    }
    double v = (double)mantissa;
    if (exp10 > 330)
        v = mantissa ? __builtin_inf() : 0;
    else if (exp10 < -360)
        v = 0;
    else if (exp10 < -300)
        v = v * pow10_double(-300) * pow10_double(exp10 + 300);
    else
        v = v * pow10_double(exp10);
    HlExpr *e = const_expr(p, hl_type_scalar(h, type), t->loc);
    e->values[0].f = (float)v;
    return e;
}

/* ------------------------------------------------------------------ names */

HlExpr *var_expr(Parser *p, HlVar *v, HlLoc loc)
{
    if (v->this_var) {
        HlExpr *self = var_expr(p, v->this_var, loc);
        HlExpr *e = new_expr(p, E_FIELD, v->type, loc);
        e->a = self;
        e->field = v->this_field;
        e->lvalue = self->lvalue;
        return e;
    }
    /* static constants with constant initializers are their values, arrays too. */
    if ((v->storage & ST_CONST) && (v->storage & ST_STATIC || !v->global) && v->init && v->init->kind == E_CONST &&
        !hl_type_has_object(v->type) && v->type->kind != TY_STRING) {
        HlExpr *c = new_expr(p, E_CONST, v->type, loc);
        c->values = v->init->values;
        v->used = true;
        return c;
    }
    HlExpr *e = new_expr(p, E_VAR, v->type, loc);
    e->var = v;
    e->lvalue = !(v->storage & ST_CONST) && !(v->global && !(v->storage & (ST_STATIC | ST_GROUPSHARED))) &&
                !(v->param && (v->storage & ST_UNIFORM));
    v->used = true;
    return e;
}

void mark_assigned(Parser *p, HlExpr *e)
{
    while (e) {
        switch (e->kind) {
        case E_VAR:
            e->var->assigned = true;
            return;
        case E_INDEX:
        case E_FIELD:
        case E_SWIZZLE:
            e = e->a;
            continue;
        default:
            return;
        }
    }
}

static bool check_lvalue(Parser *p, HlExpr *e, HlLoc loc)
{
    if (is_error(e))
        return false;
    if (!e->lvalue) {
        HlExpr *root = e;
        while (root->kind == E_INDEX || root->kind == E_FIELD || root->kind == E_SWIZZLE)
            root = root->a;
        if (root->kind == E_VAR && root->var->global && !(root->var->storage & (ST_STATIC | ST_GROUPSHARED)))
            hl_error(p->h, loc, 3025, "global variables are implicitly constant, enable compatibility mode to allow modification");
        else
            hl_error(p->h, loc, 3025, "l-value specifies const object");
        return false;
    }
    mark_assigned(p, e);
    return true;
}

/* ------------------------------------------------------------------ swizzles and members */

static int swizzle_char(char c, int set)
{
    static const char sets[2][5] = {"xyzw", "rgba"};
    for (int i = 0; i < 4; i++)
        if (sets[set][i] == c)
            return i;
    return -1;
}

static HlExpr *vector_swizzle(Parser *p, HlExpr *a, const char *name, HlLoc loc)
{
    Hlsl *h = p->h;
    size_t n = __builtin_strlen(name);
    uint32_t size = a->type->kind == TY_SCALAR ? 1 : a->type->cols;
    if (n < 1 || n > 4)
        goto invalid;
    int set = swizzle_char(name[0], 0) >= 0 ? 0 : 1;
    HlExpr *e = new_expr(p, E_SWIZZLE, nullptr, loc);
    bool repeated = false;
    for (size_t i = 0; i < n; i++) {
        int c = swizzle_char(name[i], set);
        if (c < 0 || (uint32_t)c >= size)
            goto invalid;
        for (size_t j = 0; j < i; j++)
            if (e->swizzle[j] == c)
                repeated = true;
        e->swizzle[i] = (uint8_t)c;
    }
    e->a = a;
    e->type = n == 1 ? hl_type_scalar(h, a->type->base) : hl_type_vector(h, a->type->base, (uint8_t)n);
    e->lvalue = a->lvalue && !repeated;
    return fold(p, e);
invalid:
    hl_error(h, loc, 3018, "invalid subscript '%s'", name);
    return error_expr(p, loc);
}

static HlExpr *matrix_swizzle(Parser *p, HlExpr *a, const char *name, HlLoc loc)
{
    Hlsl *h = p->h;
    HlExpr *e = new_expr(p, E_SWIZZLE, nullptr, loc);
    uint32_t n = 0;
    bool repeated = false;
    const char *s = name;
    while (*s) {
        if (s[0] != '_' || n >= 4)
            goto invalid;
        int r, c;
        if (s[1] == 'm' && s[2] >= '0' && s[2] <= '3' && s[3] >= '0' && s[3] <= '3') {
            r = s[2] - '0', c = s[3] - '0';
            s += 4;
        } else if (s[1] >= '1' && s[1] <= '4' && s[2] >= '1' && s[2] <= '4') {
            r = s[1] - '1', c = s[2] - '1';
            s += 3;
        } else {
            goto invalid;
        }
        if (r >= a->type->rows || c >= a->type->cols)
            goto invalid;
        uint8_t flat = (uint8_t)(r * a->type->cols + c);
        for (uint32_t j = 0; j < n; j++)
            if (e->swizzle[j] == flat)
                repeated = true;
        e->swizzle[n++] = flat;
    }
    e->a = a;
    e->type = n == 1 ? hl_type_scalar(h, a->type->base) : hl_type_vector(h, a->type->base, (uint8_t)n);
    e->lvalue = a->lvalue && !repeated;
    return fold(p, e);
invalid:
    hl_error(h, loc, 3018, "invalid subscript '%s'", name);
    return error_expr(p, loc);
}

static HlExpr *member(Parser *p, HlExpr *a, HlToken *name_token)
{
    Hlsl *h = p->h;
    const char *name = name_token->ident;
    HlLoc loc = name_token->loc;
    if (is_error(a))
        return a;
    HlType *t = a->type;
    if (t->kind == TY_STRUCT) {
        for (HlStruct *st = t->st; st; st = st->base)
            for (uint32_t i = 0; i < st->field_count; i++)
                if (st->fields[i].name == name) {
                    HlExpr *e = new_expr(p, E_FIELD, st->fields[i].type, loc);
                    e->a = a;
                    e->field = i;
                    e->lvalue = a->lvalue;
                    /* a field of a base class lives in the base. */
                    if (st != t->st) {
                        hl_error(h, loc, 3000, "members of base classes are not supported");
                        return error_expr(p, loc);
                    }
                    return e;
                }
        hl_error(h, loc, 3018, "invalid subscript '%s'", name);
        return error_expr(p, loc);
    }
    if (t->kind == TY_SCALAR || t->kind == TY_VECTOR)
        return vector_swizzle(p, a, name, loc);
    if (t->kind == TY_MATRIX)
        return matrix_swizzle(p, a, name, loc);
    if (t->kind == TY_OBJECT) {
        if (hl_streq(name, "mips") && (t->object >= OBJ_TEXTURE1D && t->object <= OBJ_TEXTURECUBEARRAY) &&
            t->object != OBJ_TEXTURE2DMS && t->object != OBJ_TEXTURE2DMSARRAY) {
            HlExpr *e = new_expr(p, E_OBJECT_PROPERTY, t, loc);
            e->a = a;
            e->intrinsic = PROP_MIPS;
            return e;
        }
        if (hl_streq(name, "sample") && (t->object == OBJ_TEXTURE2DMS || t->object == OBJ_TEXTURE2DMSARRAY)) {
            HlExpr *e = new_expr(p, E_OBJECT_PROPERTY, t, loc);
            e->a = a;
            e->intrinsic = PROP_SAMPLE;
            return e;
        }
    }
    hl_error(h, loc, 3018, "invalid subscript '%s'", name);
    return error_expr(p, loc);
}

static HlType *index_type(Parser *p, HlExpr *a, HlLoc loc, bool *lvalue)
{
    Hlsl *h = p->h;
    HlType *t = a->type;
    *lvalue = a->lvalue;
    switch (t->kind) {
    case TY_ARRAY:
        return t->elem;
    case TY_VECTOR:
        return hl_type_scalar(h, t->base);
    case TY_MATRIX: {
        HlType *row = hl_type_vector(h, t->base, t->cols);
        return t->cols == 1 ? hl_type_scalar(h, t->base) : row;
    }
    case TY_OBJECT:
        *lvalue = hl_object_is_uav(t->object) && t->object != OBJ_APPEND_STRUCTURED_BUFFER &&
                  t->object != OBJ_CONSUME_STRUCTURED_BUFFER && t->object != OBJ_RWBYTE_ADDRESS_BUFFER;
        switch (t->object) {
        case OBJ_BUFFER:
        case OBJ_RWBUFFER:
        case OBJ_TEXTURE1D:
        case OBJ_TEXTURE1DARRAY:
        case OBJ_TEXTURE2D:
        case OBJ_TEXTURE2DARRAY:
        case OBJ_TEXTURE2DMS:
        case OBJ_TEXTURE2DMSARRAY:
        case OBJ_TEXTURE3D:
        case OBJ_RWTEXTURE1D:
        case OBJ_RWTEXTURE1DARRAY:
        case OBJ_RWTEXTURE2D:
        case OBJ_RWTEXTURE2DARRAY:
        case OBJ_RWTEXTURE3D:
        case OBJ_STRUCTURED_BUFFER:
        case OBJ_RWSTRUCTURED_BUFFER:
        case OBJ_INPUT_PATCH:
        case OBJ_OUTPUT_PATCH:
            return t->elem ? t->elem : hl_type_vector(h, BT_FLOAT, 4);
        default:
            break;
        }
        break;
    default:
        break;
    }
    hl_error(h, loc, 3121, "array, matrix, vector, or indexable object type expected in index expression");
    return nullptr;
}

/* the integer vector an object index takes. */
static HlType *object_index_type(Parser *p, const HlExpr *a)
{
    Hlsl *h = p->h;
    const HlType *t = a->type;
    if (a->kind == E_OBJECT_PROPERTY)
        return hl_type_scalar(h, BT_UINT);
    uint32_t n = hl_object_dims(t->object) + (hl_object_is_array(t->object) ? 1 : 0);
    if (t->object == OBJ_TEXTURECUBE || t->object == OBJ_TEXTURECUBEARRAY)
        n = 0;
    return n == 1 ? hl_type_scalar(h, BT_UINT) : hl_type_vector(h, BT_UINT, (uint8_t)n);
}

static HlExpr *index_expr(Parser *p, HlExpr *a, HlExpr *index, HlLoc loc)
{
    Hlsl *h = p->h;
    if (is_error(a) || is_error(index))
        return error_expr(p, loc);
    if (a->kind == E_OBJECT_PROPERTY && !a->b) {
        /* tex.mips[level] and tex.sample[index], then their own [position]. */
        HlExpr *e = new_expr(p, E_OBJECT_PROPERTY, a->type, loc);
        e->a = a->a;
        e->intrinsic = a->intrinsic;
        e->b = convert(p, index, hl_type_scalar(h, BT_UINT), loc, false);
        return e;
    }
    if (a->kind == E_OBJECT_PROPERTY) {
        HlType *pos = object_index_type(p, a->a);
        HlExpr *e = new_expr(p, E_INDEX, a->type->elem ? a->type->elem : hl_type_vector(h, BT_FLOAT, 4), loc);
        e->a = a;
        e->b = convert(p, index, pos, loc, false);
        return e;
    }
    if (!hl_type_numeric_p(index->type) || hl_type_components(index->type) < 1) {
        hl_error(h, loc, 3120, "invalid type for index - index must be a scalar, or a vector with the correct number of dimensions");
        return error_expr(p, loc);
    }
    bool lvalue;
    HlType *t = index_type(p, a, loc, &lvalue);
    if (!t)
        return error_expr(p, loc);
    HlExpr *e = new_expr(p, E_INDEX, t, loc);
    e->a = a;
    if (a->type->kind == TY_OBJECT && a->type->object != OBJ_INPUT_PATCH && a->type->object != OBJ_OUTPUT_PATCH &&
        a->type->object != OBJ_STRUCTURED_BUFFER && a->type->object != OBJ_RWSTRUCTURED_BUFFER) {
        HlType *pos = object_index_type(p, a);
        if (hl_type_components(index->type) < hl_type_components(pos)) {
            hl_error(h, loc, 3120, "invalid type for index - index must be a scalar, or a vector with the correct number of dimensions");
            return error_expr(p, loc);
        }
        e->b = convert(p, index, pos, loc, false);
    } else {
        if (index->type->kind != TY_SCALAR)
            hl_warning(h, loc, 3206, "implicit truncation of vector type");
        e->b = convert(p, index, hl_type_scalar(h, hl_base_unsigned(index->type->base) ? BT_UINT : BT_INT), loc,
                       false);
    }
    e->lvalue = lvalue;
    if (e->b->kind == E_CONST && a->type->kind == TY_ARRAY && a->type->count) {
        uint32_t i = e->b->values[0].u;
        if (i >= a->type->count) {
            hl_error(h, loc, 3504, "array index out of bounds");
            return error_expr(p, loc);
        }
    }
    if (e->b->kind == E_CONST && (a->type->kind == TY_VECTOR || a->type->kind == TY_MATRIX)) {
        uint32_t i = e->b->values[0].u, n = a->type->kind == TY_VECTOR ? a->type->cols : a->type->rows;
        if (i >= n) {
            hl_error(h, loc, 3504, "array index out of bounds");
            return error_expr(p, loc);
        }
    }
    if (e->b->kind != E_CONST && a->type->kind == TY_ARRAY) {
        HlExpr *root = a;
        while (root->kind == E_INDEX || root->kind == E_FIELD)
            root = root->a;
        if (root->kind == E_VAR)
            root->var->dynamic_index = true;
    }
    if (e->b->kind != E_CONST && (a->type->kind == TY_VECTOR || a->type->kind == TY_MATRIX)) {
        HlExpr *root = a;
        while (root->kind == E_INDEX || root->kind == E_FIELD || root->kind == E_SWIZZLE)
            root = root->a;
        if (root->kind == E_VAR)
            root->var->dynamic_index = true;
    }
    return fold(p, e);
}

/* ------------------------------------------------------------------ operators */

static bool numeric_operand(Parser *p, HlExpr *e, HlLoc loc)
{
    if (is_error(e))
        return false;
    if (!hl_type_numeric_p(e->type)) {
        hl_error(p->h, loc, 3022, "scalar, vector, or matrix expected");
        return false;
    }
    return true;
}

static uint8_t int_base(uint8_t base)
{
    if (hl_base_unsigned(base))
        return BT_UINT;
    if (base == BT_LIT_INT)
        return BT_LIT_INT;
    return BT_INT;
}

static HlExpr *unary(Parser *p, uint8_t op, HlExpr *a, HlLoc loc)
{
    Hlsl *h = p->h;
    if (!numeric_operand(p, a, loc))
        return error_expr(p, loc);
    HlExpr *e = new_expr(p, E_UNARY, a->type, loc);
    e->op = op;
    switch (op) {
    case OPR_NOT:
        e->type = hl_type_with_base(h, a->type, BT_BOOL);
        a = convert(p, a, e->type, loc, false);
        break;
    case OPR_BITNOT:
        if (hl_base_float(a->type->base)) {
            hl_error(h, loc, 3082, "int or unsigned int type required");
            return error_expr(p, loc);
        }
        e->type = hl_type_with_base(h, a->type, int_base(a->type->base));
        a = convert(p, a, e->type, loc, false);
        break;
    case OPR_NEG:
    case OPR_PLUS:
        if (a->type->base == BT_BOOL) {
            e->type = hl_type_with_base(h, a->type, BT_INT);
            a = convert(p, a, e->type, loc, false);
        }
        if (op == OPR_PLUS)
            return a;
        break;
    case OPR_PREINC:
    case OPR_PREDEC:
    case OPR_POSTINC:
    case OPR_POSTDEC:
        if (!check_lvalue(p, a, loc))
            return error_expr(p, loc);
        if (a->type->base == BT_BOOL) {
            hl_error(h, loc, 3020, "type mismatch");
            return error_expr(p, loc);
        }
        e->lvalue = op == OPR_PREINC || op == OPR_PREDEC;
        e->lvalue = false;
        break;
    }
    e->a = a;
    return fold(p, e);
}

static HlExpr *binary(Parser *p, uint8_t op, HlExpr *a, HlExpr *b, HlLoc loc)
{
    Hlsl *h = p->h;
    if (!numeric_operand(p, a, loc) || !numeric_operand(p, b, loc))
        return error_expr(p, loc);
    HlType *t = arith_type(p, a->type, b->type, loc, true);
    if (t->kind == TY_ERROR) {
        hl_error(h, loc, 3020, "type mismatch");
        return error_expr(p, loc);
    }
    HlType *result = t, *operand = t;
    switch (op) {
    case OPR_SHL:
    case OPR_SHR:
    case OPR_BITAND:
    case OPR_BITOR:
    case OPR_BITXOR:
        if (hl_base_float(a->type->base) || hl_base_float(b->type->base)) {
            hl_error(h, loc, 3082, "int or unsigned int type required");
            return error_expr(p, loc);
        }
        if (op == OPR_SHL || op == OPR_SHR) {
            /* a shift has the type of its left side. */
            uint8_t base = int_base(a->type->base);
            if (base == BT_LIT_INT && b->type->base != BT_LIT_INT)
                base = int_base(b->type->base);
            result = operand = hl_type_with_base(h, t, base == BT_LIT_INT ? BT_LIT_INT : base);
            HlExpr *e = new_expr(p, E_BINARY, result, loc);
            e->op = op;
            e->a = convert(p, a, operand, loc, false);
            e->b = convert(p, b, hl_type_with_base(h, t, hl_base_unsigned(b->type->base) ? BT_UINT : BT_INT), loc,
                           false);
            return fold(p, e);
        }
        result = operand = hl_type_with_base(h, t, int_base(t->base == BT_BOOL ? BT_INT : t->base));
        break;
    case OPR_LT:
    case OPR_GT:
    case OPR_LE:
    case OPR_GE:
    case OPR_EQ:
    case OPR_NE:
        result = hl_type_with_base(h, t, BT_BOOL);
        break;
    case OPR_AND:
    case OPR_OR:
        result = operand = hl_type_with_base(h, t, BT_BOOL);
        break;
    default:
        if (t->base == BT_BOOL)
            result = operand = hl_type_with_base(h, t, BT_INT);
        break;
    }
    HlExpr *e = new_expr(p, E_BINARY, result, loc);
    e->op = op;
    e->a = convert(p, a, operand, loc, false);
    e->b = convert(p, b, operand, loc, false);
    if (op == OPR_DIV && hl_base_float(operand->base) && e->b->kind == E_CONST) {
        bool zero = false;
        for (uint32_t i = 0; i < hl_type_components(e->b->type); i++)
            zero |= e->b->values[i].f == 0;
        if (zero)
            hl_warning(h, loc, 4008, "floating point division by zero");
    }
    return fold(p, e);
}

static HlExpr *assign(Parser *p, uint8_t op, HlExpr *a, HlExpr *b, HlLoc loc)
{
    Hlsl *h = p->h;
    if (is_error(a) || is_error(b))
        return error_expr(p, loc);
    if (!check_lvalue(p, a, loc))
        return error_expr(p, loc);
    if (a->type->kind == TY_OBJECT && op == OPR_NONE) {
        hl_error(h, loc, 3020, "type mismatch, object assignment is not supported");
        return error_expr(p, loc);
    }
    HlExpr *e = new_expr(p, E_ASSIGN, a->type, loc);
    e->op = op;
    e->a = a;
    if (op == OPR_NONE) {
        e->b = convert(p, b, a->type, loc, false);
    } else {
        /* a op= b is a = a op b, with b in the type of the operation. */
        HlExpr *combined = binary(p, op, a, b, loc);
        if (is_error(combined))
            return combined;
        if (!can_convert(p, combined->type, a->type, false)) {
            hl_error(h, loc, 3020, "type mismatch");
            return error_expr(p, loc);
        }
        e->b = combined->kind == E_BINARY ? combined->b : convert(p, b, a->type, loc, false);
        e->c = combined;
    }
    return e;
}

static HlExpr *ternary(Parser *p, HlExpr *c, HlExpr *a, HlExpr *b, HlLoc loc)
{
    Hlsl *h = p->h;
    if (is_error(c) || is_error(a) || is_error(b))
        return error_expr(p, loc);
    if (!numeric_operand(p, c, loc))
        return error_expr(p, loc);
    HlType *t;
    if (hl_type_numeric_p(a->type) && hl_type_numeric_p(b->type)) {
        t = arith_type(p, a->type, b->type, loc, true);
        if (c->type->kind != TY_SCALAR) {
            HlType *shape = arith_type(p, c->type, t, loc, true);
            t = hl_type_with_base(h, shape, t->base);
        }
    } else if (hl_type_equal(a->type, b->type) && c->type->kind == TY_SCALAR) {
        t = a->type;
    } else {
        hl_error(h, loc, 3020, "type mismatch");
        return error_expr(p, loc);
    }
    if (t->kind == TY_ERROR) {
        hl_error(h, loc, 3020, "type mismatch");
        return error_expr(p, loc);
    }
    HlExpr *e = new_expr(p, E_TERNARY, t, loc);
    e->a = convert(p, c, c->type->kind == TY_SCALAR ? hl_type_scalar(h, BT_BOOL) : hl_type_with_base(h, t, BT_BOOL), loc, false);
    e->b = convert(p, a, t, loc, false);
    e->c = convert(p, b, t, loc, false);
    return fold(p, e);
}

/* ------------------------------------------------------------------ calls and constructors */

static uint32_t parse_args(Parser *p, HlExpr ***out)
{
    HlExpr **args = nullptr;
    uint32_t count = 0, capacity = 0;
    if (!at_punct(p, ')')) {
        for (;;) {
            HlExpr *a = parse_assignment(p);
            HL_PUSH(p->h, args, count, capacity, a);
            if (!accept_punct(p, ','))
                break;
        }
    }
    expect_punct(p, ')');
    *out = args;
    return count;
}

static HlExpr *construct(Parser *p, HlType *t, HlLoc loc)
{
    Hlsl *h = p->h;
    HlExpr **args;
    uint32_t count = parse_args(p, &args);
    for (uint32_t i = 0; i < count; i++)
        if (is_error(args[i]))
            return error_expr(p, loc);
    if (!hl_type_numeric_p(t)) {
        hl_error(h, loc, 3037, "constructors only defined for numeric base types");
        return error_expr(p, loc);
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!hl_type_numeric_p(args[i]->type) && args[i]->type->kind != TY_ARRAY && args[i]->type->kind != TY_STRUCT) {
            hl_error(h, args[i]->loc, 3037, "constructors only defined for numeric base types");
            return error_expr(p, loc);
        }
        total += hl_type_components(args[i]->type);
    }
    uint32_t need = hl_type_components(t);
    if (count == 1 && args[0]->type->kind == TY_SCALAR)
        return convert(p, args[0], t, loc, true);
    if (total != need) {
        hl_error(h, loc, 3014, "incorrect number of arguments to numeric-type constructor");
        return error_expr(p, loc);
    }
    if (count == 1 && hl_type_numeric_p(args[0]->type))
        return convert(p, args[0], t, loc, true);
    HlExpr *e = new_expr(p, E_CONSTRUCT, t, loc);
    e->args = args;
    e->arg_count = count;
    return fold(p, e);
}

static HlExpr *call(Parser *p, HlToken *name, HlExpr *object)
{
    Hlsl *h = p->h;
    HlLoc loc = name->loc;
    HlExpr **args;
    uint32_t count = parse_args(p, &args);
    for (uint32_t i = 0; i < count; i++)
        if (is_error(args[i]))
            return error_expr(p, loc);
    if (object) {
        if (is_error(object))
            return object;
        if (object->type->kind == TY_STRUCT) {
            for (HlStruct *st = object->type->st; st; st = st->base)
                for (uint32_t i = 0; i < st->method_count; i++)
                    if (st->methods[i]->name == name->ident) {
                        Symbol tmp = {.kind = SYM_FUNC};
                        for (uint32_t j = 0; j < st->method_count; j++)
                            if (st->methods[j]->name == name->ident)
                                HL_PUSH(h, tmp.funcs, tmp.func_count, tmp.func_capacity, st->methods[j]);
                        return resolve_call(p, &tmp, args, count, loc, object);
                    }
        }
        uint32_t id = method_lookup(p, object->type, name->ident);
        if (!id) {
            hl_error(h, loc, 3018, "invalid subscript '%s'", name->ident);
            return error_expr(p, loc);
        }
        return check_method(p, object, id, args, count, loc, name->ident);
    }
    Symbol *s = lookup(p, name->ident);
    if (s && s->kind == SYM_FUNC && s->func_count && s->funcs[0]->owner && p->func && p->func->this_var) {
        /* a method calling another of its struct runs on the same this. */
        return resolve_call(p, s, args, count, loc, var_expr(p, p->func->this_var, loc));
    }
    if (s && s->kind == SYM_FUNC) {
        uint32_t id = intrinsic_lookup(p, name->ident);
        if (!id)
            return resolve_call(p, s, args, count, loc, nullptr);
        /* user overloads of intrinsic names win when one fits. */
        for (uint32_t i = 0; i < s->func_count; i++)
            if (s->funcs[i]->param_count >= count) {
                bool fits = true;
                for (uint32_t j = 0; j < count && fits; j++)
                    fits = conversion_cost(p, args[j]->type, s->funcs[i]->params[j]->type) == 0;
                if (fits)
                    return resolve_call(p, s, args, count, loc, nullptr);
            }
        return check_intrinsic(p, id, args, count, loc, name->ident);
    }
    uint32_t id = intrinsic_lookup(p, name->ident);
    if (id)
        return check_intrinsic(p, id, args, count, loc, name->ident);
    hl_error(h, loc, 3004, "undeclared identifier '%s'", name->ident);
    return error_expr(p, loc);
}

/* ------------------------------------------------------------------ primary and postfix */

static HlExpr *primary(Parser *p)
{
    Hlsl *h = p->h;
    HlToken *t = peek(p);
    HlLoc loc = t->loc;
    if (t->kind == TK_NUMBER) {
        next(p);
        return number(p, t);
    }
    if (t->kind == TK_STRING) {
        HlBuf text = {};
        while (peek(p)->kind == TK_STRING) {
            HlToken *s = next(p);
            if (s->length >= 2)
                hl_buf_append(h, &text, s->text + 1, s->length - 2);
        }
        HlExpr *e = new_expr(p, E_CONST, hl_type_string(h), loc);
        e->text = text.data ? text.data : "";
        return e;
    }
    if (t->kind == TK_CHAR) {
        next(p);
        return const_int(p, BT_LIT_INT, t->length > 2 ? (uint8_t)t->text[1] : 0, loc);
    }
    if (accept_punct(p, '(')) {
        /* a cast, or an expression in parentheses. */
        uint32_t save = p->pos;
        if (at_type(p)) {
            HlType *type = parse_type(p, false);
            if (type) {
                while (accept_punct(p, '[')) {
                    HlExpr *n = parse_expr(p);
                    uint32_t count = 0;
                    if (const_uint(p, n, &count, "array dimension") && count)
                        type = hl_type_array(h, type, count);
                    expect_punct(p, ']');
                }
                if (accept_punct(p, ')')) {
                    HlExpr *operand = nullptr;
                    if (++p->nesting > MAX_NESTING) {
                        hl_error(h, loc, 3000, "expression is nested too deeply");
                        p->nesting--;
                        return error_expr(p, loc);
                    }
                    /* the operand of a cast is a unary expression. */
                    operand = parse_unary(p);
                    p->nesting--;
                    if (is_error(operand))
                        return operand;
                    if (operand->kind == E_INIT_LIST) {
                        HlType *tt = type;
                        return check_initializer(p, operand, &tt, loc);
                    }
                    return convert(p, operand, type, loc, true);
                }
            }
            p->pos = save;
        }
        HlExpr *e = parse_expr(p);
        expect_punct(p, ')');
        return e;
    }
    if (t->kind == TK_IDENT) {
        if (t->ident == p->k.k_true || t->ident == p->k.k_false) {
            next(p);
            return const_int(p, BT_BOOL, t->ident == p->k.k_true, loc);
        }
        if (at_type(p)) {
            HlType *type = parse_type(p, false);
            if (type && accept_punct(p, '('))
                return construct(p, type, loc);
            if (type && type->kind == TY_STRUCT && accept_punct(p, P_COLONCOLON)) {
                /* Struct::Method(...), a static method. */
                HlToken *method = next(p);
                if (method->kind == TK_IDENT && accept_punct(p, '(')) {
                    HlExpr **args;
                    uint32_t count = parse_args(p, &args);
                    Symbol tmp = {.kind = SYM_FUNC, .name = method->ident};
                    for (uint32_t i = 0; i < type->st->method_count; i++)
                        if (type->st->methods[i]->name == method->ident)
                            HL_PUSH(h, tmp.funcs, tmp.func_count, tmp.func_capacity, type->st->methods[i]);
                    if (tmp.func_count)
                        return resolve_call(p, &tmp, args, count, method->loc, nullptr);
                }
                hl_error(h, method->loc, 3004, "undeclared identifier '%s'", hl_token_text(h, method));
                return error_expr(p, loc);
            }
            hl_error(h, loc, 3000, "syntax error: unexpected token '%s'", hl_token_text(h, peek(p)));
            return error_expr(p, loc);
        }
        next(p);
        if (accept_punct(p, '('))
            return call(p, t, nullptr);
        Symbol *s = lookup(p, t->ident);
        if (s && s->kind == SYM_VAR)
            return var_expr(p, s->var, loc);
        if (s && s->kind == SYM_FUNC) {
            hl_error(h, loc, 3000, "syntax error: unexpected function name '%s'", t->ident);
            return error_expr(p, loc);
        }
        hl_error(h, loc, 3004, "undeclared identifier '%s'", t->ident);
        return error_expr(p, loc);
    }
    if (at_punct(p, '{'))
        return init_list(p, loc);
    syntax_error(p, t);
    if (t->kind != TK_EOF && !hl_is_punct(t, ';') && !hl_is_punct(t, '}'))
        next(p);
    return error_expr(p, loc);
}

static HlExpr *postfix(Parser *p)
{
    HlExpr *e = primary(p);
    for (;;) {
        HlToken *t = peek(p);
        if (accept_punct(p, '[')) {
            HlExpr *index = parse_expr(p);
            expect_punct(p, ']');
            e = index_expr(p, e, index, t->loc);
        } else if (accept_punct(p, '.')) {
            HlToken *name = next(p);
            if (name->kind != TK_IDENT) {
                syntax_error(p, name);
                return error_expr(p, name->loc);
            }
            if (accept_punct(p, '('))
                e = call(p, name, e);
            else
                e = member(p, e, name);
        } else if (accept_punct(p, P_INC)) {
            e = unary(p, OPR_POSTINC, e, t->loc);
        } else if (accept_punct(p, P_DEC)) {
            e = unary(p, OPR_POSTDEC, e, t->loc);
        } else {
            return e;
        }
    }
}

HlExpr *parse_unary(Parser *p)
{
    HlToken *t = peek(p);
    uint8_t op = 0;
    if (hl_is_punct(t, '-'))
        op = OPR_NEG;
    else if (hl_is_punct(t, '+'))
        op = OPR_PLUS;
    else if (hl_is_punct(t, '!'))
        op = OPR_NOT;
    else if (hl_is_punct(t, '~'))
        op = OPR_BITNOT;
    else if (hl_is_punct(t, P_INC))
        op = OPR_PREINC;
    else if (hl_is_punct(t, P_DEC))
        op = OPR_PREDEC;
    if (!op)
        return postfix(p);
    next(p);
    if (++p->nesting > MAX_NESTING) {
        hl_error(p->h, t->loc, 3000, "expression is nested too deeply");
        p->nesting--;
        return error_expr(p, t->loc);
    }
    HlExpr *a = parse_unary(p);
    p->nesting--;
    return unary(p, op, a, t->loc);
}

static int precedence(const HlToken *t, uint8_t *op)
{
    if (t->kind != TK_PUNCT)
        return 0;
    switch (t->punct) {
    case '*': *op = OPR_MUL; return 10;
    case '/': *op = OPR_DIV; return 10;
    case '%': *op = OPR_MOD; return 10;
    case '+': *op = OPR_ADD; return 9;
    case '-': *op = OPR_SUB; return 9;
    case P_SHL: *op = OPR_SHL; return 8;
    case P_SHR: *op = OPR_SHR; return 8;
    case '<': *op = OPR_LT; return 7;
    case '>': *op = OPR_GT; return 7;
    case P_LE: *op = OPR_LE; return 7;
    case P_GE: *op = OPR_GE; return 7;
    case P_EQ: *op = OPR_EQ; return 6;
    case P_NE: *op = OPR_NE; return 6;
    case '&': *op = OPR_BITAND; return 5;
    case '^': *op = OPR_BITXOR; return 4;
    case '|': *op = OPR_BITOR; return 3;
    case P_ANDAND: *op = OPR_AND; return 2;
    case P_OROR: *op = OPR_OR; return 1;
    default: return 0;
    }
}

static HlExpr *parse_binary(Parser *p, int min_precedence)
{
    HlExpr *lhs = parse_unary(p);
    for (;;) {
        uint8_t op = 0;
        HlToken *t = peek(p);
        int prec = precedence(t, &op);
        if (!prec || prec < min_precedence)
            return lhs;
        next(p);
        HlExpr *rhs = parse_binary(p, prec + 1);
        lhs = binary(p, op, lhs, rhs, t->loc);
    }
}

static HlExpr *parse_conditional(Parser *p)
{
    HlExpr *c = parse_binary(p, 1);
    HlToken *t = peek(p);
    if (!accept_punct(p, '?'))
        return c;
    HlExpr *a = parse_assignment(p);
    expect_punct(p, ':');
    HlExpr *b = parse_assignment(p);
    return ternary(p, c, a, b, t->loc);
}

HlExpr *parse_assignment(Parser *p)
{
    if (++p->nesting > MAX_NESTING) {
        hl_error(p->h, peek(p)->loc, 3000, "expression is nested too deeply");
        p->nesting--;
        while (peek(p)->kind != TK_EOF && !at_punct(p, ';'))
            next(p);
        return error_expr(p, peek(p)->loc);
    }
    HlExpr *lhs = parse_conditional(p);
    HlToken *t = peek(p);
    uint8_t op = 0xFF;
    if (t->kind == TK_PUNCT) {
        switch (t->punct) {
        case '=': op = OPR_NONE; break;
        case P_ADD_ASSIGN: op = OPR_ADD; break;
        case P_SUB_ASSIGN: op = OPR_SUB; break;
        case P_MUL_ASSIGN: op = OPR_MUL; break;
        case P_DIV_ASSIGN: op = OPR_DIV; break;
        case P_MOD_ASSIGN: op = OPR_MOD; break;
        case P_SHL_ASSIGN: op = OPR_SHL; break;
        case P_SHR_ASSIGN: op = OPR_SHR; break;
        case P_AND_ASSIGN: op = OPR_BITAND; break;
        case P_OR_ASSIGN: op = OPR_BITOR; break;
        case P_XOR_ASSIGN: op = OPR_BITXOR; break;
        }
    }
    if (op != 0xFF) {
        next(p);
        HlExpr *rhs = parse_assignment(p);
        lhs = assign(p, op, lhs, rhs, t->loc);
    }
    p->nesting--;
    return lhs;
}

HlExpr *parse_template_arg(Parser *p) { return parse_binary(p, 8); }

HlExpr *parse_expr(Parser *p)
{
    HlExpr *e = parse_assignment(p);
    while (at_punct(p, ',')) {
        HlToken *t = next(p);
        HlExpr *rhs = parse_assignment(p);
        HlExpr *c = new_expr(p, E_COMMA, rhs->type, t->loc);
        c->a = e;
        c->b = rhs;
        e = c;
    }
    return e;
}

/* ------------------------------------------------------------------ initializers */

HlExpr *init_list(Parser *p, HlLoc loc)
{
    HlExpr *e = new_expr(p, E_INIT_LIST, hl_type_void(p->h), loc);
    expect_punct(p, '{');
    uint32_t capacity = 0;
    while (!at_punct(p, '}') && peek(p)->kind != TK_EOF) {
        HlExpr *a = at_punct(p, '{') ? init_list(p, peek(p)->loc) : parse_assignment(p);
        HL_PUSH(p->h, e->args, e->arg_count, capacity, a);
        if (!accept_punct(p, ','))
            break;
    }
    expect_punct(p, '}');
    return e;
}

static void flatten_init(Parser *p, HlExpr *e, HlExpr ***out, uint32_t *count, uint32_t *capacity)
{
    if (e->kind == E_INIT_LIST) {
        for (uint32_t i = 0; i < e->arg_count; i++)
            flatten_init(p, e->args[i], out, count, capacity);
        return;
    }
    HL_PUSH(p->h, *out, *count, *capacity, e);
}

HlExpr *check_initializer(Parser *p, HlExpr *init, HlType **type, HlLoc loc)
{
    Hlsl *h = p->h;
    HlType *t = *type;
    if (is_error(init))
        return init;
    if (init->kind != E_INIT_LIST) {
        if (t->kind == TY_ARRAY && !t->count && init->type->kind == TY_ARRAY) {
            *type = hl_type_array(h, t->elem, init->type->count);
            t = *type;
        }
        if (t->kind == TY_OBJECT || init->type->kind == TY_OBJECT) {
            if (!hl_type_equal(t, init->type)) {
                hl_error(h, loc, 3017, "cannot implicitly convert from '%s' to '%s'", hl_type_name(h, init->type),
                         hl_type_name(h, t));
                return error_expr(p, loc);
            }
            return init;
        }
        if (init->type->kind == TY_SCALAR && (t->kind == TY_STRUCT || t->kind == TY_ARRAY))
            return convert(p, init, t, loc, true);
        return convert(p, init, t, loc, false);
    }
    HlExpr **items = nullptr;
    uint32_t count = 0, capacity = 0;
    flatten_init(p, init, &items, &count, &capacity);
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (is_error(items[i]))
            return error_expr(p, loc);
        if (items[i]->type->kind == TY_OBJECT) {
            if (count == 1 && t->kind == TY_OBJECT)
                return items[0];
            if (t->kind == TY_ARRAY && t->elem->kind == TY_OBJECT) {
                total++;
                continue;
            }
            hl_error(h, items[i]->loc, 3017, "cannot convert an object in an initializer list");
            return error_expr(p, loc);
        }
        if (items[i]->type->kind == TY_STRING) {
            total++;
            continue;
        }
        total += hl_type_components(items[i]->type);
    }
    if (t->kind == TY_ARRAY && !t->count) {
        uint32_t elem = hl_type_components(t->elem);
        if (t->elem->kind == TY_OBJECT)
            elem = 1;
        if (!elem || total % elem) {
            hl_error(h, loc, 3013, "incorrect number of elements in initializer for an unsized array");
            return error_expr(p, loc);
        }
        *type = t = hl_type_array(h, t->elem, total / elem);
    }
    uint32_t need = t->kind == TY_ARRAY && t->elem->kind == TY_OBJECT ? t->count : hl_type_components(t);
    if (t->kind == TY_STRING || (t->kind == TY_ARRAY && t->elem->kind == TY_STRING))
        need = total;
    if (total != need) {
        hl_error(h, loc, 3017, "initializer has %u elements where %u are needed", total, need);
        return error_expr(p, loc);
    }
    HlExpr *e = new_expr(p, E_CONSTRUCT, t, loc);
    e->args = items;
    e->arg_count = count;
    if (hl_type_numeric_p(t) || t->kind == TY_ARRAY || t->kind == TY_STRUCT) {
        bool constant = all_const(items, count) && !hl_type_has_object(t);
        if (constant) {
            HlExpr *c = new_expr(p, E_CONST, t, loc);
            c->values = hl_alloc(h, (need ? need : 1) * sizeof(HlValue));
            uint32_t at = 0;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t m = hl_type_components(items[i]->type);
                for (uint32_t j = 0; j < m && at < need; j++, at++)
                    c->values[at] = const_component(items[i], j, hl_type_component_base(t, at));
            }
            return c;
        }
    }
    return e;
}
