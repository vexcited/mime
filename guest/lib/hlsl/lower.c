#include "lower.h"

/* ------------------------------------------------------------------ the ir */

uint32_t ir_new_vreg(IrShader *s) { return ++s->vregs; }

IrIns *ir_emit(IrShader *s, uint16_t op, uint32_t dst_count, uint32_t src_count)
{
    HL_GROW(s->h, s->ins, s->count, s->capacity);
    IrIns *ins = &s->ins[s->count++];
    __builtin_memset(ins, 0, sizeof(*ins));
    ins->op = op;
    ins->dst_count = (uint8_t)dst_count;
    ins->src_count = (uint8_t)src_count;
    if (src_count)
        ins->src = hl_alloc(s->h, src_count * sizeof(IrSrc));
    for (uint32_t i = 0; i < dst_count; i++)
        ins->dst[i] = ir_new_vreg(s);
    for (int i = 0; i < 4; i++)
        ins->res_swizzle[i] = (uint8_t)i;
    return ins;
}

void lw_error(Lower *l, HlLoc loc, uint32_t code, const char *format, ...)
{
    Hlsl *h = l->h;
    HlBuf b = {};
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, &b, format, ap);
    va_end(ap);
    hl_error(h, loc, code, "%s", b.data ? b.data : "");
    l->failed = true;
}

/* ------------------------------------------------------------------ folding */

static float bits_float(uint32_t u)
{
    union {
        uint32_t u;
        float f;
    } c = {.u = u};
    return c.f;
}

static uint32_t float_bits(float f)
{
    union {
        float f;
        uint32_t u;
    } c = {.f = f};
    return c.u;
}

/* the value of an immediate with its modifiers, as a float operation reads it. */
static float imm_float(const IrSrc *s)
{
    float f = bits_float(s->v);
    if (s->mod & MODIFIER_ABS)
        f = __builtin_fabsf(f);
    if (s->mod & MODIFIER_NEG)
        f = -f;
    return f;
}

static uint32_t imm_int(const IrSrc *s)
{
    uint32_t v = s->v;
    if (s->mod & MODIFIER_NEG)
        v = 0 - v;
    return v;
}

static int32_t ftoi(float f)
{
    if (f != f)
        return 0;
    if (f >= 2147483648.0f)
        return 0x7FFFFFFF;
    if (f <= -2147483648.0f)
        return (int32_t)0x80000000u;
    return (int32_t)f;
}

static uint32_t ftou(float f)
{
    if (f != f || f <= 0)
        return 0;
    if (f >= 4294967296.0f)
        return 0xFFFFFFFFu;
    return (uint32_t)f;
}

/* the libm functions the folding needs, as freestanding code has none. */
static float trunc_f(float f)
{
    if (!(__builtin_fabsf(f) < 8388608.0f))
        return f;
    float r = (float)(int32_t)f;
    return r == 0 && __builtin_signbit(f) ? -0.0f : r;
}

static float floor_f(float f)
{
    float t = trunc_f(f);
    return t > f ? t - 1.0f : t;
}

static float ceil_f(float f)
{
    float t = trunc_f(f);
    return t < f ? t + 1.0f : t;
}

static float round_ne(float f)
{
    if (!(__builtin_fabsf(f) < 8388608.0f))
        return f;
    float r = trunc_f(f);
    float d = f - r;
    bool odd = ((int32_t)r & 1) != 0;
    if (d > 0.5f || (d == 0.5f && odd))
        r += 1.0f;
    else if (d < -0.5f || (d == -0.5f && odd))
        r -= 1.0f;
    return r;
}

/* newton steps in double from the exponent halved: exact enough to round to float. */
static float sqrt_f(float f)
{
    if (f != f || f < 0)
        return __builtin_nanf("");
    if (f == 0 || f > 3.4e38f)
        return f;
    double x = f, r = 1;
    int e = 0;
    double m = x;
    while (m >= 4) {
        m /= 4;
        e++;
    }
    while (m < 1) {
        m *= 4;
        e--;
    }
    r = m < 2 ? 1.2 : 1.7;
    for (int i = 0; i < 6; i++)
        r = 0.5 * (r + m / r);
    for (; e > 0; e--)
        r *= 2;
    for (; e < 0; e++)
        r /= 2;
    return (float)r;
}

/* folds an operation of immediates; false when it cannot. */
static bool fold_op(uint16_t op, const IrSrc *a, const IrSrc *b, const IrSrc *c, uint32_t *out)
{
    float fa = a ? imm_float(a) : 0, fb = b ? imm_float(b) : 0, fc = c ? imm_float(c) : 0;
    uint32_t ia = a ? imm_int(a) : 0, ib = b ? imm_int(b) : 0, ic = c ? imm_int(c) : 0;
    switch (op) {
    case OP_MOV: *out = float_bits(fa); if (!a->mod) *out = a->v; return true;
    case OP_ADD: *out = float_bits(fa + fb); return true;
    case OP_MUL: *out = float_bits(fa * fb); return true;
    case OP_MAD: *out = float_bits(fa * fb + fc); return true;
    case OP_DIV: *out = float_bits(fa / fb); return true;
    case OP_MIN: *out = float_bits(fa < fb || fb != fb ? fa : fb); return true;
    case OP_MAX: *out = float_bits(fa > fb || fb != fb ? fa : fb); return true;
    case OP_FRC: *out = float_bits(fa - floor_f(fa)); return true;
    case OP_ROUND_NI: *out = float_bits(floor_f(fa)); return true;
    case OP_ROUND_PI: *out = float_bits(ceil_f(fa)); return true;
    case OP_ROUND_Z: *out = float_bits(trunc_f(fa)); return true;
    case OP_ROUND_NE: *out = float_bits(round_ne(fa)); return true;
    case OP_SQRT: *out = float_bits(sqrt_f(fa)); return true;
    case OP_RSQ: *out = float_bits(1.0f / sqrt_f(fa)); return true;
    case OP_RCP: *out = float_bits(1.0f / fa); return true;
    case OP_EQ: *out = fa == fb ? ~0u : 0; return true;
    case OP_NE: *out = fa != fb ? ~0u : 0; return true;
    case OP_LT: *out = fa < fb ? ~0u : 0; return true;
    case OP_GE: *out = fa >= fb ? ~0u : 0; return true;
    case OP_IADD: *out = ia + ib; return true;
    case OP_IMUL: *out = ia * ib; return true;
    case OP_IMAD: *out = ia * ib + ic; return true;
    case OP_UMAD: *out = ia * ib + ic; return true;
    case OP_INEG: *out = 0 - ia; return true;
    case OP_IEQ: *out = ia == ib ? ~0u : 0; return true;
    case OP_INE: *out = ia != ib ? ~0u : 0; return true;
    case OP_ILT: *out = (int32_t)ia < (int32_t)ib ? ~0u : 0; return true;
    case OP_IGE: *out = (int32_t)ia >= (int32_t)ib ? ~0u : 0; return true;
    case OP_ULT: *out = ia < ib ? ~0u : 0; return true;
    case OP_UGE: *out = ia >= ib ? ~0u : 0; return true;
    case OP_IMIN: *out = (int32_t)ia < (int32_t)ib ? ia : ib; return true;
    case OP_IMAX: *out = (int32_t)ia > (int32_t)ib ? ia : ib; return true;
    case OP_UMIN: *out = ia < ib ? ia : ib; return true;
    case OP_UMAX: *out = ia > ib ? ia : ib; return true;
    case OP_AND: *out = ia & ib; return true;
    case OP_OR: *out = ia | ib; return true;
    case OP_XOR: *out = ia ^ ib; return true;
    case OP_NOT: *out = ~ia; return true;
    case OP_ISHL: *out = ia << (ib & 31); return true;
    case OP_ISHR: *out = (uint32_t)((int32_t)ia >> (ib & 31)); return true;
    case OP_USHR: *out = ia >> (ib & 31); return true;
    case OP_UDIV: if (!ib) return false; *out = ia / ib; return true;
    case OP_ITOF: *out = float_bits((float)(int32_t)ia); return true;
    case OP_UTOF: *out = float_bits((float)ia); return true;
    case OP_FTOI: *out = (uint32_t)ftoi(fa); return true;
    case OP_FTOU: *out = ftou(fa); return true;
    case OP_MOVC: *out = ia ? (b->mod ? float_bits(fb) : b->v) : (c->mod ? float_bits(fc) : c->v); return true;
    case OP_COUNTBITS: *out = (uint32_t)__builtin_popcount(ia); return true;
    case OP_BFREV: {
        uint32_t r = 0;
        for (int i = 0; i < 32; i++)
            if (ia & (1u << i))
                r |= 1u << (31 - i);
        *out = r;
        return true;
    }
    default:
        return false;
    }
}

static bool float_modifiers(uint16_t op)
{
    switch (op) {
    case OP_ADD: case OP_MUL: case OP_MAD: case OP_DIV: case OP_MIN: case OP_MAX: case OP_FRC: case OP_ROUND_NE:
    case OP_ROUND_NI: case OP_ROUND_PI: case OP_ROUND_Z: case OP_SQRT: case OP_RSQ: case OP_RCP: case OP_EXP:
    case OP_LOG: case OP_EQ: case OP_NE: case OP_LT: case OP_GE: case OP_MOV: case OP_FTOI: case OP_FTOU:
    case OP_DP2: case OP_DP3: case OP_DP4: case OP_SINCOS: case OP_DERIV_RTX: case OP_DERIV_RTY:
    case OP_DERIV_RTX_COARSE: case OP_DERIV_RTX_FINE: case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE:
        return true;
    default:
        return false;
    }
}

/* a source with float modifiers fed to an instruction that has none gets a mov first. */
static IrSrc plain(Lower *l, uint16_t op, IrSrc a)
{
    if (!a.mod || float_modifiers(op))
        return a;
    if (op == OP_IADD || op == OP_IMUL || op == OP_IMAD) {
        /* integer negation is a modifier there; abs is not. */
        if (!(a.mod & MODIFIER_ABS))
            return a;
    }
    IrIns *ins = ir_emit(l->s, OP_MOV, 1, 1);
    ins->src[0] = a;
    if (a.kind == SRC_IMM) {
        uint32_t v;
        fold_op(OP_MOV, &a, nullptr, nullptr, &v);
        l->s->count--;
        return ir_imm(v);
    }
    return ir_reg(ins->dst[0]);
}

IrSrc lw_op1(Lower *l, uint16_t op, IrSrc a)
{
    if (op != OP_MOV)
        a = plain(l, op, a);
    uint32_t v;
    if (a.kind == SRC_IMM && fold_op(op, &a, nullptr, nullptr, &v))
        return ir_imm(v);
    IrIns *ins = ir_emit(l->s, op, 1, 1);
    ins->src[0] = a;
    return ir_reg(ins->dst[0]);
}

IrSrc lw_op2(Lower *l, uint16_t op, IrSrc a, IrSrc b)
{
    a = plain(l, op, a);
    b = plain(l, op, b);
    uint32_t v;
    if (a.kind == SRC_IMM && b.kind == SRC_IMM && fold_op(op, &a, &b, nullptr, &v))
        return ir_imm(v);
    /* identities of the common cases. */
    if (op == OP_IADD && ir_is_imm(&b) && !b.v)
        return a;
    if (op == OP_IADD && ir_is_imm(&a) && !a.v)
        return b;
    if (op == OP_IMUL && ir_is_imm(&b) && b.v == 1)
        return a;
    if (op == OP_IMUL && ir_is_imm(&a) && a.v == 1)
        return b;
    if (op == OP_ADD && ir_is_imm(&b) && !(b.v & 0x7FFFFFFF))
        return a;
    if (op == OP_ADD && ir_is_imm(&a) && !(a.v & 0x7FFFFFFF))
        return b;
    if (op == OP_MUL && ir_is_imm(&b) && b.v == 0x3F800000)
        return a;
    if (op == OP_MUL && ir_is_imm(&a) && a.v == 0x3F800000)
        return b;
    if ((op == OP_AND) && ir_is_imm(&b) && b.v == ~0u)
        return a;
    if ((op == OP_AND) && ir_is_imm(&a) && a.v == ~0u)
        return b;
    if ((op == OP_OR) && ir_is_imm(&b) && !b.v)
        return a;
    if ((op == OP_ISHL || op == OP_ISHR || op == OP_USHR) && ir_is_imm(&b) && !(b.v & 31))
        return a;
    if ((op == OP_AND) && ((ir_is_imm(&b) && !b.v) || (ir_is_imm(&a) && !a.v)))
        return ir_imm(0);
    IrIns *ins = ir_emit(l->s, op, 1, 2);
    ins->src[0] = a;
    ins->src[1] = b;
    return ir_reg(ins->dst[0]);
}

IrSrc lw_op3(Lower *l, uint16_t op, IrSrc a, IrSrc b, IrSrc c)
{
    if (op != OP_MOVC) {
        a = plain(l, op, a);
    } else {
        a = plain(l, OP_AND, a);
    }
    b = plain(l, op, b);
    c = plain(l, op, c);
    uint32_t v;
    if (a.kind == SRC_IMM && b.kind == SRC_IMM && c.kind == SRC_IMM && fold_op(op, &a, &b, &c, &v))
        return ir_imm(v);
    if (op == OP_MOVC && ir_is_imm(&a))
        return a.v ? b : c;
    if (op == OP_MOVC && b.kind == c.kind && b.v == c.v && b.mod == c.mod && b.comp == c.comp && b.index == c.index &&
        b.rel == c.rel && b.kind != SRC_X)
        return b;
    if (op == OP_MAD && ((ir_is_imm(&a) && !(a.v & 0x7FFFFFFF)) || (ir_is_imm(&b) && !(b.v & 0x7FFFFFFF))))
        return c;
    if (op == OP_MAD && ir_is_imm(&a) && ir_is_imm(&b)) {
        uint32_t m;
        fold_op(OP_MUL, &a, &b, nullptr, &m);
        return lw_op2(l, OP_ADD, ir_imm(m), c);
    }
    IrIns *ins = ir_emit(l->s, op, 1, 3);
    ins->src[0] = a;
    ins->src[1] = b;
    ins->src[2] = c;
    return ir_reg(ins->dst[0]);
}

IrSrc lw_mov(Lower *l, IrSrc a)
{
    if (a.kind == SRC_IMM && !a.mod)
        return a;
    IrIns *ins = ir_emit(l->s, OP_MOV, 1, 1);
    ins->src[0] = a;
    return ir_reg(ins->dst[0]);
}

IrSrc lw_movc(Lower *l, IrSrc c, IrSrc a, IrSrc b) { return lw_op3(l, OP_MOVC, c, a, b); }

IrSrc lw_neg(Lower *l, IrSrc a, uint8_t storage)
{
    if (storage == BT_FLOAT || storage == BT_DOUBLE) {
        a.mod ^= MODIFIER_NEG;
        if (a.kind == SRC_IMM) {
            uint32_t v;
            fold_op(OP_MOV, &a, nullptr, nullptr, &v);
            return ir_imm(v);
        }
        return a;
    }
    return lw_op1(l, OP_INEG, a);
}

IrSrc lw_abs(Lower *l, IrSrc a, uint8_t storage)
{
    if (storage == BT_FLOAT || storage == BT_DOUBLE) {
        a.mod |= MODIFIER_ABS;
        a.mod &= (uint8_t)~MODIFIER_NEG;
        if (a.kind == SRC_IMM) {
            uint32_t v;
            fold_op(OP_MOV, &a, nullptr, nullptr, &v);
            return ir_imm(v);
        }
        return a;
    }
    if (storage == BT_UINT || storage == BT_BOOL)
        return a;
    return lw_op2(l, OP_IMAX, a, lw_op1(l, OP_INEG, a));
}

IrSrc lw_to_bool(Lower *l, IrSrc a, uint8_t storage)
{
    switch (storage) {
    case BT_BOOL:
        return a;
    case BT_FLOAT:
    case BT_DOUBLE:
        return lw_op2(l, OP_NE, a, ir_imm(0));
    default:
        return lw_op2(l, OP_INE, a, ir_imm(0));
    }
}

IrSrc lw_convert_scalar(Lower *l, IrSrc a, uint8_t from, uint8_t to)
{
    from = hl_base_storage(from);
    to = hl_base_storage(to);
    if (from == BT_DOUBLE)
        from = BT_FLOAT;
    if (to == BT_DOUBLE)
        to = BT_FLOAT;
    if (from == to)
        return a;
    switch (to) {
    case BT_BOOL:
        return lw_to_bool(l, a, from);
    case BT_FLOAT:
        if (from == BT_BOOL)
            return lw_op2(l, OP_AND, a, ir_imm(0x3F800000));
        return lw_op1(l, from == BT_UINT ? OP_UTOF : OP_ITOF, a);
    case BT_INT:
    case BT_UINT:
        if (from == BT_BOOL)
            return lw_op2(l, OP_AND, a, ir_imm(1));
        if (from == BT_FLOAT)
            return lw_op1(l, to == BT_UINT ? OP_FTOU : OP_FTOI, a);
        return a;
    }
    return a;
}

/* ------------------------------------------------------------------ values */

Value lw_value(Lower *l, HlType *type, uint32_t count)
{
    Value v = {.type = type, .count = count};
    v.c = hl_alloc(l->h, (count ? count : 1) * sizeof(IrSrc));
    return v;
}

static Value zero_value(Lower *l, HlType *type)
{
    uint32_t n = hl_type_components(type);
    Value v = lw_value(l, type, n);
    for (uint32_t i = 0; i < n; i++)
        v.c[i] = ir_imm(0);
    return v;
}

static Value const_value(Lower *l, HlExpr *e)
{
    uint32_t n = hl_type_components(e->type);
    Value v = lw_value(l, e->type, n);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t base = hl_base_storage(hl_type_component_base(e->type, i));
        HlValue x = e->values[i];
        /* constants keep bools as 0 and 1; registers as 0 and ~0. */
        if (base == BT_BOOL)
            x.u = x.u ? ~0u : 0;
        v.c[i] = ir_imm(x.u);
    }
    return v;
}

Value lw_convert(Lower *l, Value v, HlType *to)
{
    Hlsl *h = l->h;
    HlType *from = v.type;
    if (!hl_type_numeric_p(to) && to->kind != TY_ARRAY && to->kind != TY_STRUCT)
        return v;
    uint32_t n = hl_type_components(to);
    Value r = lw_value(l, to, n);
    if (!v.count)
        return zero_value(l, to);
    if (from->kind == TY_MATRIX && to->kind == TY_MATRIX && (from->rows != to->rows || from->cols != to->cols)) {
        for (uint32_t row = 0; row < to->rows; row++)
            for (uint32_t col = 0; col < to->cols; col++)
                r.c[row * to->cols + col] =
                    lw_convert_scalar(l, v.c[row * from->cols + col], from->base, to->base);
        return r;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = v.count == 1 ? 0 : i;
        if (at >= v.count)
            at = v.count - 1;
        r.c[i] = lw_convert_scalar(l, v.c[at], hl_type_component_base(from, at), hl_type_component_base(to, i));
    }
    (void)h;
    return r;
}

/* ------------------------------------------------------------------ variables */

static void track_live(Lower *l, VarInfo *vi) { HL_PUSH(l->h, l->live, l->live_count, l->live_capacity, vi); }

static void set_known(Lower *l, VarInfo *vi, uint32_t comp, IrSrc v)
{
    UndoEntry u = {vi, comp, vi->known[comp]};
    HL_PUSH(l->h, l->undo, l->undo_count, l->undo_capacity, u);
    vi->known[comp] = v;
}

static void undo_to(Lower *l, uint32_t mark)
{
    while (l->undo_count > mark) {
        UndoEntry *u = &l->undo[--l->undo_count];
        u->vi->known[u->comp] = u->old;
    }
}

/* forgets every known value, as at loop heads. */
static void forget_all(Lower *l)
{
    for (uint32_t i = 0; i < l->live_count; i++) {
        VarInfo *vi = l->live[i];
        if (vi->kind != VK_REGS)
            continue;
        for (uint32_t c = 0; c < vi->count; c++)
            if (vi->known[c].kind != SRC_NONE)
                set_known(l, vi, c, (IrSrc){});
    }
}

typedef struct Touched {
    VarInfo **vi;
    uint32_t *comp;
    uint32_t count, capacity;
} Touched;

/* the components changed since the mark, then back to their values at it. */
static void collect_and_undo(Lower *l, uint32_t mark, Touched *t)
{
    for (uint32_t i = mark; i < l->undo_count; i++) {
        HL_GROW(l->h, t->vi, t->count, t->capacity);
        uint32_t capacity = t->count;
        (void)capacity;
        t->comp = hl_realloc(l->h, t->comp, t->count * sizeof(uint32_t), t->capacity * sizeof(uint32_t));
        t->vi[t->count] = l->undo[i].vi;
        t->comp[t->count] = l->undo[i].comp;
        t->count++;
    }
    undo_to(l, mark);
}

static void forget_touched(Lower *l, Touched *t)
{
    for (uint32_t i = 0; i < t->count; i++)
        if (t->vi[i]->known[t->comp[i]].kind != SRC_NONE)
            set_known(l, t->vi[i], t->comp[i], (IrSrc){});
}

/* homes of variables have several definitions; other virtual registers one. */
static bool is_home(Lower *l, uint32_t v) { return v < l->home_capacity && l->homes[v]; }

static void mark_home(Lower *l, uint32_t v)
{
    if (v >= l->home_capacity) {
        uint32_t capacity = l->home_capacity ? l->home_capacity : 1024;
        while (capacity <= v)
            capacity *= 2;
        l->homes = hl_realloc(l->h, l->homes, l->home_capacity, capacity);
        l->home_capacity = capacity;
    }
    l->homes[v] = 1;
}

/* immutable sources can stand for a variable; homes of other variables cannot. */
static bool immutable(Lower *l, const IrSrc *s)
{
    switch (s->kind) {
    case SRC_IMM:
    case SRC_IN:
    case SRC_SV:
    case SRC_CB:
    case SRC_ICB:
    case SRC_PATCH:
    case SRC_OUT_CP:
        return !s->rel || !is_home(l, s->rel);
    case SRC_REG:
        return !is_home(l, s->v);
    default:
        return false;
    }
}

static VarInfo *new_regs(Lower *l, HlVar *var, HlType *type)
{
    VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
    vi->kind = VK_REGS;
    vi->var = var;
    vi->type = type;
    vi->count = hl_type_components(type);
    vi->home = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    vi->known = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(IrSrc));
    for (uint32_t i = 0; i < vi->count; i++) {
        vi->home[i] = ir_new_vreg(l->s);
        mark_home(l, vi->home[i]);
    }
    track_live(l, vi);
    return vi;
}

/* registers an indexable temp: each component in its own register when vector parts are indexed. */
static uint32_t x_layout(const HlType *t, bool unpacked, uint32_t reg, uint32_t *xreg, uint8_t *xcomp, uint32_t *n)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
        for (uint32_t i = 0; i < t->cols; i++) {
            xreg[*n] = unpacked ? reg + i : reg;
            xcomp[*n] = unpacked ? 0 : (uint8_t)i;
            (*n)++;
        }
        return unpacked ? t->cols : 1;
    case TY_MATRIX:
        for (uint32_t r = 0; r < t->rows; r++)
            for (uint32_t c = 0; c < t->cols; c++) {
                xreg[*n] = unpacked ? reg + r * t->cols + c : reg + r;
                xcomp[*n] = unpacked ? 0 : (uint8_t)c;
                (*n)++;
            }
        return unpacked ? (uint32_t)t->rows * t->cols : t->rows;
    case TY_ARRAY: {
        uint32_t used = 0;
        for (uint32_t i = 0; i < t->count; i++)
            used += x_layout(t->elem, unpacked, reg + used, xreg, xcomp, n);
        return used;
    }
    case TY_STRUCT: {
        uint32_t used = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++)
            used += x_layout(t->st->fields[i].type, unpacked, reg + used, xreg, xcomp, n);
        return used;
    }
    default:
        return 0;
    }
}

/* registers per element of the type in an indexable temp. */
static uint32_t x_regs(const HlType *t, bool unpacked)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
        return unpacked ? t->cols : 1;
    case TY_MATRIX:
        return unpacked ? (uint32_t)t->rows * t->cols : t->rows;
    case TY_ARRAY:
        return t->count * x_regs(t->elem, unpacked);
    case TY_STRUCT: {
        uint32_t n = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++)
            n += x_regs(t->st->fields[i].type, unpacked);
        return n;
    }
    default:
        return 0;
    }
}

static bool vector_indexed(const HlType *t)
{
    /* whether a dynamic index may reach into vectors: matrices and vectors not inside arrays. */
    return t->kind == TY_VECTOR || t->kind == TY_MATRIX;
}

static VarInfo *new_x(Lower *l, HlVar *var, HlType *type)
{
    VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
    vi->kind = VK_X;
    vi->var = var;
    vi->type = type;
    vi->count = hl_type_components(type);
    vi->unpacked = vector_indexed(type) || (type->kind == TY_ARRAY && vector_indexed(type->elem) && var &&
                                            var->dynamic_index && false);
    vi->xreg = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    vi->xcomp = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint8_t));
    uint32_t n = 0;
    uint32_t regs = x_layout(type, vi->unpacked, 0, vi->xreg, vi->xcomp, &n);
    uint32_t comps = 1;
    for (uint32_t i = 0; i < vi->count; i++)
        if (vi->xcomp[i] + 1u > comps)
            comps = vi->xcomp[i] + 1u;
    IrIndexable x = {regs ? regs : 1, comps};
    HL_PUSH(l->h, l->s->indexables, l->s->indexable_count, l->s->indexable_capacity, x);
    vi->x = l->s->indexable_count - 1;
    return vi;
}

/* a local, parameter or static: registers, or an indexable temp when an index is not constant. */
static VarInfo *new_local(Lower *l, HlVar *var, HlType *type)
{
    if (var && var->dynamic_index && hl_type_components(type) > 1)
        return new_x(l, var, type);
    return new_regs(l, var, type);
}

/* ------------------------------------------------------------------ layouts */

static bool aggregate(const HlType *t) { return t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_MATRIX; }

/* byte offsets of each component of a type placed in a constant buffer at base. */
static void cb_offsets(const HlType *t, uint32_t base, bool row_major_default, uint32_t *out, uint32_t *n)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
        for (uint32_t i = 0; i < t->cols; i++)
            out[(*n)++] = base + i * 4;
        break;
    case TY_MATRIX: {
        bool rm = hl_type_row_major(t, row_major_default);
        for (uint32_t r = 0; r < t->rows; r++)
            for (uint32_t c = 0; c < t->cols; c++)
                out[(*n)++] = base + (rm ? r * 16 + c * 4 : c * 16 + r * 4);
        break;
    }
    case TY_ARRAY: {
        uint32_t stride = (hl_type_cb_size(t->elem, row_major_default) + 15) & ~15u;
        for (uint32_t i = 0; i < t->count; i++)
            cb_offsets(t->elem, base + i * stride, row_major_default, out, n);
        break;
    }
    case TY_STRUCT: {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++) {
            const HlType *f = t->st->fields[i].type;
            if (f->kind == TY_OBJECT)
                continue;
            uint32_t size = hl_type_cb_size(f, row_major_default);
            if (aggregate(f) || (offset & 15) + size > 16)
                offset = (offset + 15) & ~15u;
            cb_offsets(f, base + offset, row_major_default, out, n);
            offset += size;
        }
        break;
    }
    default:
        break;
    }
}

/* the bytes of a type laid out tightly, as structured buffers and groupshared memory hold it. */
uint32_t lw_tight_size(const HlType *t, bool row_major_default)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
    case TY_MATRIX:
        return hl_type_components(t) * 4;
    case TY_ARRAY:
        return t->count * lw_tight_size(t->elem, row_major_default);
    case TY_STRUCT: {
        uint32_t n = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++)
            n += lw_tight_size(t->st->fields[i].type, row_major_default);
        return n;
    }
    default:
        return 0;
    }
}

void lw_tight_offsets(const HlType *t, uint32_t base, bool row_major_default, uint32_t *out, uint32_t *n)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
        for (uint32_t i = 0; i < t->cols; i++)
            out[(*n)++] = base + i * 4;
        break;
    case TY_MATRIX: {
        bool rm = hl_type_row_major(t, row_major_default);
        for (uint32_t r = 0; r < t->rows; r++)
            for (uint32_t c = 0; c < t->cols; c++)
                out[(*n)++] = base + (rm ? (r * t->cols + c) * 4 : (c * t->rows + r) * 4);
        break;
    }
    case TY_ARRAY: {
        uint32_t stride = lw_tight_size(t->elem, row_major_default);
        for (uint32_t i = 0; i < t->count; i++)
            lw_tight_offsets(t->elem, base + i * stride, row_major_default, out, n);
        break;
    }
    case TY_STRUCT: {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++) {
            lw_tight_offsets(t->st->fields[i].type, base + offset, row_major_default, out, n);
            offset += lw_tight_size(t->st->fields[i].type, row_major_default);
        }
        break;
    }
    default:
        break;
    }
}

/* the start of each variable of a constant buffer, by packoffset or packing. */
static void layout_buffer(Lower *l, HlBuffer *b, uint32_t index)
{
    bool rm = l->s->row_major_default;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < b->var_count; i++) {
        HlVar *v = b->vars[i];
        uint32_t size = hl_type_cb_size(v->type, rm);
        uint32_t start;
        if (v->has_packoffset) {
            start = v->packoffset * 4;
        } else {
            if (aggregate(v->type) || (offset & 15) + size > 16)
                offset = (offset + 15) & ~15u;
            start = offset;
        }
        if (start + size > offset)
            offset = start + size;
        VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
        vi->kind = VK_CB;
        vi->var = v;
        vi->type = v->type;
        vi->count = hl_type_components(v->type);
        vi->buffer = index;
        vi->offsets = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
        uint32_t n = 0;
        cb_offsets(v->type, start, rm, vi->offsets, &n);
        v->lower = vi;
        v->packoffset = start / 4;
        v->has_packoffset = true;
    }
    b->size = (offset + 15) & ~15u;
}

/* ------------------------------------------------------------------ objects */

uint32_t lw_resource(Lower *l, const ObjRef *o)
{
    IrShader *s = l->s;
    for (uint32_t i = 0; i < s->resource_count; i++) {
        IrResource *r = &s->resources[i];
        if (r->var == o->var && r->element == o->element && r->dynamic == o->has_dynamic)
            return i;
    }
    IrResource r = {.var = o->var, .element = o->element, .dynamic = o->has_dynamic, .object = o->type->object};
    const HlType *t = o->var->type;
    uint32_t count = 1;
    while (t->kind == TY_ARRAY) {
        count *= t->count;
        t = t->elem;
    }
    r.object = t->object;
    r.count = o->has_dynamic ? count : 1;
    r.comparison = t->object == OBJ_SAMPLER_COMPARISON;
    if (hl_object_is_sampler(t->object))
        r.kind = SRC_SAMPLER;
    else if (hl_object_is_uav(t->object))
        r.kind = SRC_UAV;
    else
        r.kind = SRC_RESOURCE;
    r.id = s->resource_count;
    HL_PUSH(l->h, s->resources, s->resource_count, s->resource_capacity, r);
    return s->resource_count - 1;
}

/* the dynamic index of a resource array, in a register the emitter can name. */
IrSrc lw_resource_index(Lower *l, const ObjRef *o)
{
    return o->has_dynamic ? o->dynamic : (IrSrc){};
}

static uint32_t object_elements(const HlType *t)
{
    uint32_t n = 1;
    while (t->kind == TY_ARRAY) {
        n *= t->count;
        t = t->elem;
    }
    return n;
}

bool lw_obj(Lower *l, HlExpr *e, ObjRef *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    switch (e->kind) {
    case E_VAR: {
        HlVar *v = e->var;
        VarInfo *vi = v->lower;
        if (vi && vi->kind == VK_OBJECT && vi->object_count) {
            *out = vi->objects[0];
            out->type = e->type;
            return true;
        }
        if (vi && (vi->kind == VK_VERTICES || vi->kind == VK_CONTROL_POINTS)) {
            out->var = v;
            out->type = e->type;
            return true;
        }
        out->var = v;
        out->type = e->type;
        return true;
    }
    case E_INDEX: {
        ObjRef base;
        if (!lw_obj(l, e->a, &base))
            return false;
        VarInfo *vi = base.var ? base.var->lower : nullptr;
        uint32_t stride = object_elements(e->type);
        if (vi && vi->kind == VK_OBJECT && vi->object_count > 1 && e->b->kind == E_CONST) {
            uint32_t i = e->b->values[0].u * stride + base.element;
            if (i < vi->object_count) {
                *out = vi->objects[i];
                out->type = e->type;
                return true;
            }
        }
        *out = base;
        out->type = e->type;
        if (e->b->kind == E_CONST) {
            out->element = base.element + e->b->values[0].u * stride;
            return true;
        }
        if (!lw_sm51(l)) {
            lw_error(l, e->loc, 3512, "sampler array index must be a literal expression");
            return false;
        }
        IrSrc index = lw_scalar(l, e->b);
        if (stride != 1)
            index = lw_op2(l, OP_IMUL, index, ir_imm(stride));
        if (base.has_dynamic)
            index = lw_op2(l, OP_IADD, index, base.dynamic);
        if (base.element)
            index = lw_op2(l, OP_IADD, index, ir_imm(base.element));
        out->element = 0;
        out->dynamic = lw_mov(l, index);
        out->has_dynamic = true;
        return true;
    }
    case E_TERNARY:
        lw_error(l, e->loc, 3000, "objects cannot be selected by a condition");
        return false;
    default:
        lw_error(l, e->loc, 3000, "this expression does not name an object");
        return false;
    }
}

/* ------------------------------------------------------------------ indices into constant buffers */

/* a dynamic index as a temp, which a constant buffer operand can keep. */
static IrSrc index_temp(Lower *l, IrSrc i)
{
    if (i.kind == SRC_IMM)
        return i;
    if (i.kind == SRC_REG && !is_home(l, i.v) && !i.mod)
        return i;
    return lw_mov(l, i);
}

/* ------------------------------------------------------------------ expressions */

Value lw_expr(Lower *l, HlExpr *e);

IrSrc lw_scalar(Lower *l, HlExpr *e)
{
    Value v = lw_expr(l, e);
    return v.count ? v.c[0] : ir_imm(0);
}

static uint32_t flat_offset_of_field(const HlType *t, uint32_t field)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < field; i++)
        n += hl_type_components(t->st->fields[i].type);
    return n;
}

static Value binary_value(Lower *l, HlExpr *e, Value a, Value b);

/* a value the lvalue machinery reads, for subexpressions of a known variable. */
static bool simple_path(HlExpr *e)
{
    while (e->kind == E_FIELD || e->kind == E_SWIZZLE || (e->kind == E_INDEX && e->b->kind == E_CONST &&
                                                          e->a->type->kind != TY_OBJECT))
        e = e->a;
    return e->kind == E_VAR;
}

static Value lower_call(Lower *l, HlExpr *e);

Value lw_expr(Lower *l, HlExpr *e)
{
    Hlsl *h = l->h;
    if (l->subst == e)
        return l->subst_value;
    switch (e->kind) {
    case E_CONST:
        if (!hl_type_numeric_p(e->type) && e->type->kind != TY_ARRAY && e->type->kind != TY_STRUCT)
            return lw_value(l, e->type, 0);
        return const_value(l, e);
    case E_VAR:
    case E_FIELD:
    case E_SWIZZLE:
    case E_INDEX: {
        if (e->kind == E_SWIZZLE && !simple_path(e)) {
            Value a = lw_expr(l, e->a);
            uint32_t n = hl_type_components(e->type);
            Value r = lw_value(l, e->type, n);
            for (uint32_t i = 0; i < n; i++)
                r.c[i] = a.c[a.count == 1 ? 0 : e->swizzle[i]];
            return r;
        }
        if (e->kind == E_FIELD && !simple_path(e) && e->a->kind != E_INDEX) {
            Value a = lw_expr(l, e->a);
            uint32_t start = flat_offset_of_field(e->a->type, e->field), n = hl_type_components(e->type);
            Value r = lw_value(l, e->type, n);
            for (uint32_t i = 0; i < n; i++)
                r.c[i] = a.c[start + i];
            return r;
        }
        if (e->kind == E_INDEX && e->a->kind == E_CONST && e->b->kind != E_CONST) {
            /* a constant array read at a dynamic index: an immediate constant buffer. */
            IrShader *s = l->s;
            HlType *at = e->a->type;
            uint32_t elem = hl_type_components(e->type);
            uint32_t count = at->kind == TY_ARRAY ? at->count : at->kind == TY_MATRIX ? at->rows : at->cols;
            uint32_t regs = (elem + 3) / 4;
            uint32_t base = s->icb_count / 4;
            for (uint32_t i = 0; i < count; i++)
                for (uint32_t r = 0; r < regs * 4; r++) {
                    uint32_t v = r < elem ? e->a->values[i * elem + r].u : 0;
                    if (r < elem && hl_base_storage(hl_type_component_base(at, i * elem + r)) == BT_BOOL)
                        v = v ? ~0u : 0;
                    HL_PUSH(h, s->icb, s->icb_count, s->icb_capacity, v);
                }
            IrSrc index = lw_scalar(l, e->b);
            if (regs != 1)
                index = lw_op2(l, OP_IMUL, index, ir_imm(regs));
            index = index_temp(l, index);
            Value r = lw_value(l, e->type, elem);
            for (uint32_t i = 0; i < elem; i++) {
                IrSrc c = {.kind = SRC_ICB, .index = base + i / 4, .comp = (uint8_t)(i % 4)};
                if (index.kind == SRC_IMM)
                    c.index += index.v;
                else
                    c.rel = index.v;
                r.c[i] = c;
            }
            return r;
        }
        if (e->kind == E_INDEX && e->a->kind != E_VAR && e->a->type->kind != TY_OBJECT && e->b->kind == E_CONST &&
            !simple_path(e)) {
            Value a = lw_expr(l, e->a);
            uint32_t n = hl_type_components(e->type), i = e->b->values[0].u;
            Value r = lw_value(l, e->type, n);
            for (uint32_t k = 0; k < n; k++)
                r.c[k] = a.c[i * n + k < a.count ? i * n + k : 0];
            return r;
        }
        if (e->kind == E_VAR && e->type->kind == TY_OBJECT)
            return lw_value(l, e->type, 0);
        LValue lv;
        if (!lw_lvalue(l, e, &lv))
            return zero_value(l, e->type);
        return lw_load(l, &lv);
    }
    case E_CAST: {
        Value a = lw_expr(l, e->a);
        return lw_convert(l, a, e->type);
    }
    case E_UNARY: {
        HlType *t = e->type;
        uint8_t storage = hl_base_storage(t->base);
        if (e->op >= OPR_PREINC && e->op <= OPR_POSTDEC) {
            LValue lv;
            if (!lw_lvalue(l, e->a, &lv))
                return zero_value(l, t);
            Value old = lw_load(l, &lv);
            Value nv = lw_value(l, t, old.count);
            bool inc = e->op == OPR_PREINC || e->op == OPR_POSTINC;
            for (uint32_t i = 0; i < old.count; i++) {
                uint8_t st = hl_base_storage(hl_type_component_base(lv.type, i));
                if (st == BT_FLOAT || st == BT_DOUBLE)
                    nv.c[i] = lw_op2(l, OP_ADD, old.c[i], ir_immf(inc ? 1.0f : -1.0f));
                else
                    nv.c[i] = lw_op2(l, OP_IADD, old.c[i], ir_imm(inc ? 1u : ~0u));
            }
            lw_store(l, &lv, nv);
            return e->op == OPR_PREINC || e->op == OPR_PREDEC ? nv : old;
        }
        Value a = lw_expr(l, e->a);
        Value r = lw_value(l, t, a.count);
        for (uint32_t i = 0; i < a.count; i++) {
            switch (e->op) {
            case OPR_NEG:
                r.c[i] = lw_neg(l, a.c[i], storage);
                break;
            case OPR_NOT:
            case OPR_BITNOT:
                r.c[i] = lw_op1(l, OP_NOT, a.c[i]);
                break;
            default:
                r.c[i] = a.c[i];
                break;
            }
        }
        return r;
    }
    case E_BINARY: {
        Value a = lw_expr(l, e->a);
        Value b = lw_expr(l, e->b);
        return binary_value(l, e, a, b);
    }
    case E_ASSIGN: {
        LValue lv;
        if (!lw_lvalue(l, e->a, &lv)) {
            lw_expr(l, e->b);
            return zero_value(l, e->type);
        }
        Value v;
        if (e->op == OPR_NONE) {
            v = lw_expr(l, e->b);
        } else {
            Value old = lw_load(l, &lv);
            HlExpr *saved = l->subst;
            Value saved_value = l->subst_value;
            l->subst = e->a;
            l->subst_value = old;
            v = lw_convert(l, lw_expr(l, e->c), e->a->type);
            l->subst = saved;
            l->subst_value = saved_value;
        }
        v = lw_convert(l, v, e->a->type);
        lw_store(l, &lv, v);
        return v;
    }
    case E_TERNARY: {
        Value c = lw_expr(l, e->a);
        Value a = lw_expr(l, e->b);
        Value b = lw_expr(l, e->c);
        uint32_t n = hl_type_components(e->type);
        Value r = lw_value(l, e->type, n);
        for (uint32_t i = 0; i < n; i++)
            r.c[i] = lw_movc(l, c.c[c.count == 1 ? 0 : i], a.c[a.count == 1 ? 0 : i], b.c[b.count == 1 ? 0 : i]);
        return r;
    }
    case E_CONSTRUCT: {
        uint32_t n = hl_type_components(e->type);
        Value r = lw_value(l, e->type, n);
        uint32_t at = 0;
        for (uint32_t i = 0; i < e->arg_count && at < n; i++) {
            if (e->args[i]->type->kind == TY_OBJECT || e->args[i]->type->kind == TY_STRING) {
                continue;
            }
            Value a = lw_expr(l, e->args[i]);
            for (uint32_t j = 0; j < a.count && at < n; j++) {
                uint8_t from = hl_type_component_base(a.type, j), to = hl_type_component_base(e->type, at);
                r.c[at++] = lw_convert_scalar(l, a.c[j], from, to);
            }
        }
        for (; at < n; at++)
            r.c[at] = at ? r.c[0] : ir_imm(0);
        return r;
    }
    case E_COMMA:
        lw_expr(l, e->a);
        return lw_expr(l, e->b);
    case E_CALL:
        return lower_call(l, e);
    case E_INTRINSIC:
        return lw_intrinsic(l, e);
    case E_METHOD:
        return lw_method(l, e);
    case E_ERROR:
        l->failed = true;
        return lw_value(l, e->type, 0);
    default:
        lw_error(l, e->loc, 3000, "unsupported expression");
        return zero_value(l, e->type);
    }
}

static uint16_t compare_op(uint8_t op, uint8_t storage, bool *swap)
{
    *swap = op == OPR_GT || op == OPR_LE;
    bool f = storage == BT_FLOAT || storage == BT_DOUBLE, u = storage == BT_UINT;
    switch (op) {
    case OPR_LT:
    case OPR_GT:
        return f ? OP_LT : u ? OP_ULT : OP_ILT;
    case OPR_GE:
    case OPR_LE:
        return f ? OP_GE : u ? OP_UGE : OP_IGE;
    case OPR_EQ:
        return f ? OP_EQ : OP_IEQ;
    default:
        return f ? OP_NE : OP_INE;
    }
}

/* signed division and remainder of ints, through udiv of the magnitudes. */
static IrSrc idiv(Lower *l, IrSrc a, IrSrc b, bool remainder)
{
    if (a.kind == SRC_IMM && b.kind == SRC_IMM && b.v) {
        int32_t x = (int32_t)a.v, y = (int32_t)b.v;
        if (y == -1)
            return ir_imm(remainder ? 0 : 0 - a.v);
        return ir_imm((uint32_t)(remainder ? x % y : x / y));
    }
    IrSrc ma = lw_abs(l, a, BT_INT), mb = lw_abs(l, b, BT_INT);
    IrIns *ins = ir_emit(l->s, OP_UDIV, 2, 2);
    ins->src[0] = ma;
    ins->src[1] = mb;
    IrSrc q = ir_reg(ins->dst[remainder ? 1 : 0]);
    IrSrc negative = remainder ? lw_op2(l, OP_ILT, a, ir_imm(0)) : lw_op2(l, OP_ILT, lw_op2(l, OP_XOR, a, b), ir_imm(0));
    return lw_movc(l, negative, lw_op1(l, OP_INEG, q), q);
}

static IrSrc udiv(Lower *l, IrSrc a, IrSrc b, bool remainder)
{
    if (a.kind == SRC_IMM && b.kind == SRC_IMM && b.v)
        return ir_imm(remainder ? a.v % b.v : a.v / b.v);
    if (!remainder && ir_is_imm(&b) && b.v && !(b.v & (b.v - 1)))
        return lw_op2(l, OP_USHR, a, ir_imm((uint32_t)__builtin_ctz(b.v)));
    if (remainder && ir_is_imm(&b) && b.v && !(b.v & (b.v - 1)))
        return lw_op2(l, OP_AND, a, ir_imm(b.v - 1));
    IrIns *ins = ir_emit(l->s, OP_UDIV, 2, 2);
    ins->src[0] = a;
    ins->src[1] = b;
    return ir_reg(ins->dst[remainder ? 1 : 0]);
}

/* fmod as fxc computes x % y: the fraction of |x / y| with x / y's sign, times y. */
static IrSrc float_mod(Lower *l, IrSrc a, IrSrc b)
{
    IrSrc q = lw_op2(l, OP_DIV, a, b);
    IrSrc positive = lw_op2(l, OP_GE, q, lw_neg(l, q, BT_FLOAT));
    IrSrc f = lw_op1(l, OP_FRC, lw_abs(l, q, BT_FLOAT));
    IrSrc s = lw_movc(l, positive, f, lw_neg(l, f, BT_FLOAT));
    return lw_op2(l, OP_MUL, s, b);
}

IrSrc lw_binary_scalar(Lower *l, uint8_t op, uint8_t storage, IrSrc a, IrSrc b)
{
    bool f = storage == BT_FLOAT || storage == BT_DOUBLE, u = storage == BT_UINT;
    switch (op) {
    case OPR_ADD:
        return f ? lw_op2(l, OP_ADD, a, b) : lw_op2(l, OP_IADD, a, b);
    case OPR_SUB:
        return f ? lw_op2(l, OP_ADD, a, lw_neg(l, b, BT_FLOAT)) : lw_op2(l, OP_IADD, a, lw_neg(l, b, BT_INT));
    case OPR_MUL:
        return f ? lw_op2(l, OP_MUL, a, b) : lw_op2(l, OP_IMUL, a, b);
    case OPR_DIV:
        return f ? lw_op2(l, OP_DIV, a, b) : u ? udiv(l, a, b, false) : idiv(l, a, b, false);
    case OPR_MOD:
        return f ? float_mod(l, a, b) : u ? udiv(l, a, b, true) : idiv(l, a, b, true);
    case OPR_SHL:
        return lw_op2(l, OP_ISHL, a, b);
    case OPR_SHR:
        return lw_op2(l, u ? OP_USHR : OP_ISHR, a, b);
    case OPR_BITAND:
    case OPR_AND:
        return lw_op2(l, OP_AND, a, b);
    case OPR_BITOR:
    case OPR_OR:
        return lw_op2(l, OP_OR, a, b);
    case OPR_BITXOR:
        return lw_op2(l, OP_XOR, a, b);
    default: {
        bool swap;
        uint16_t cmp = compare_op(op, storage, &swap);
        return swap ? lw_op2(l, cmp, b, a) : lw_op2(l, cmp, a, b);
    }
    }
}

static Value binary_value(Lower *l, HlExpr *e, Value a, Value b)
{
    uint32_t n = hl_type_components(e->type);
    Value r = lw_value(l, e->type, n);
    uint8_t storage = hl_base_storage(e->a->type->base);
    if (e->op == OPR_SHL || e->op == OPR_SHR)
        storage = hl_base_storage(e->type->base);
    for (uint32_t i = 0; i < n; i++)
        r.c[i] = lw_binary_scalar(l, e->op, storage, a.c[a.count == 1 ? 0 : i], b.c[b.count == 1 ? 0 : i]);
    return r;
}

/* ------------------------------------------------------------------ lvalues */

static bool lvalue_path(Lower *l, HlExpr *e, LValue *lv, HlType **type);

/* adds a dynamic index times a stride to the lvalue's register or element offset. */
static void add_dynamic(Lower *l, LValue *lv, IrSrc index, uint32_t stride)
{
    if (index.kind == SRC_IMM) {
        /* a folded index adds to the static components through comps. */
        return;
    }
    IrSrc scaled = stride == 1 ? index : lw_op2(l, OP_IMUL, index, ir_imm(stride));
    if (lv->has_dynamic)
        scaled = lw_op2(l, OP_IADD, lv->dynamic, scaled);
    lv->dynamic = scaled;
    lv->has_dynamic = true;
}

/* the flat component range an access selects, built from the variable outwards. */
typedef struct Path {
    uint32_t start;
    uint32_t count;
    /* the components picked, for swizzles. */
    uint32_t *pick;
} Path;

bool lw_lvalue(Lower *l, HlExpr *e, LValue *lv)
{
    __builtin_memset(lv, 0, sizeof(*lv));
    HlType *type = e->type;
    if (!lvalue_path(l, e, lv, &type))
        return false;
    lv->type = e->type;
    return true;
}

static bool object_element_lvalue(Lower *l, HlExpr *e, LValue *lv);

/*
 * resolves e to the variable it names and the components it selects, in
 * lv->comps; dynamic array indices add to lv->dynamic in units the kind of
 * storage counts in.
 */
static bool lvalue_path(Lower *l, HlExpr *e, LValue *lv, HlType **type)
{
    Hlsl *h = l->h;
    switch (e->kind) {
    case E_VAR: {
        HlVar *var = e->var;
        VarInfo *vi = var->lower;
        if (!vi) {
            /* a global the shader reads before its lowering gave it storage. */
            lw_error(l, e->loc, 3004, "'%s' has no storage", var->name);
            return false;
        }
        lv->vi = vi;
        switch (vi->kind) {
        case VK_REGS: lv->kind = LV_REGS; break;
        case VK_X: lv->kind = LV_X; break;
        case VK_TGSM: lv->kind = LV_TGSM; break;
        case VK_CB: lv->kind = LV_REGS; break;
        case VK_VERTICES:
        case VK_CONTROL_POINTS: lv->kind = LV_REGS; break;
        default:
            lw_error(l, e->loc, 3000, "'%s' cannot be read as a value", var->name);
            return false;
        }
        lv->count = vi->count;
        lv->comps = hl_alloc(h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < vi->count; i++)
            lv->comps[i] = i;
        return true;
    }
    case E_FIELD: {
        if (!lvalue_path(l, e->a, lv, type))
            return false;
        if (lv->kind == LV_STRUCTURED) {
            uint32_t start = flat_offset_of_field(e->a->type, e->field), n = hl_type_components(e->type);
            uint32_t *c = hl_alloc(h, (n ? n : 1) * sizeof(uint32_t));
            for (uint32_t i = 0; i < n; i++)
                c[i] = lv->comps[start + i];
            lv->comps = c;
            lv->count = n;
            return true;
        }
        uint32_t start = flat_offset_of_field(e->a->type, e->field), n = hl_type_components(e->type);
        if (lv->kind == LV_REGS_DYNAMIC) {
            lw_error(l, e->loc, 3000, "unsupported dynamic access");
            return false;
        }
        lv->comps += start;
        lv->count = n;
        return true;
    }
    case E_SWIZZLE: {
        if (!lvalue_path(l, e->a, lv, type))
            return false;
        uint32_t n = hl_type_components(e->type);
        uint32_t *c = hl_alloc(h, (n ? n : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < n; i++)
            c[i] = lv->comps[lv->count == 1 ? 0 : e->swizzle[i]];
        lv->comps = c;
        lv->count = n;
        if (lv->kind == LV_TYPED) {
            lv->kind = LV_TYPED;
        }
        return true;
    }
    case E_INDEX: {
        HlType *at = e->a->type;
        if (at->kind == TY_OBJECT || e->a->kind == E_OBJECT_PROPERTY)
            return object_element_lvalue(l, e, lv);
        if (!lvalue_path(l, e->a, lv, type))
            return false;
        uint32_t n = hl_type_components(e->type);
        if (lv->kind == LV_TYPED) {
            lw_error(l, e->loc, 3000, "unsupported access to an element of a view");
            return false;
        }
        if (e->b->kind == E_CONST) {
            uint32_t i = e->b->values[0].u;
            if (at->kind == TY_VECTOR || at->kind == TY_MATRIX || at->kind == TY_ARRAY) {
                if ((i + 1) * n > lv->count) {
                    lw_error(l, e->loc, 3504, "array index out of bounds");
                    return false;
                }
                if (lv->kind == LV_STRUCTURED) {
                    uint32_t *c = hl_alloc(h, (n ? n : 1) * sizeof(uint32_t));
                    for (uint32_t k = 0; k < n; k++)
                        c[k] = lv->comps[i * n + k];
                    lv->comps = c;
                } else {
                    lv->comps += i * n;
                }
                lv->count = n;
                return true;
            }
        }
        IrSrc index = lw_scalar(l, e->b);
        if (index.kind == SRC_IMM) {
            uint32_t i = index.v;
            if ((i + 1) * n > lv->count) {
                lw_error(l, e->loc, 3504, "array index out of bounds");
                return false;
            }
            lv->comps += i * n;
            lv->count = n;
            return true;
        }
        switch (lv->kind) {
        case LV_X: {
            VarInfo *vi = lv->vi;
            /* registers per element at this level, from the layout. */
            uint32_t stride = x_regs(e->type, vi->unpacked);
            if (!stride || (!vi->unpacked && (at->kind == TY_VECTOR || (at->kind == TY_MATRIX && false)))) {
                lw_error(l, e->loc, 3000, "unsupported dynamic index");
                return false;
            }
            if (at->kind == TY_VECTOR && !vi->unpacked) {
                lw_error(l, e->loc, 3000, "unsupported dynamic vector index");
                return false;
            }
            add_dynamic(l, lv, index, stride);
            lv->count = n;
            return true;
        }
        case LV_TGSM: {
            /* the outermost array indexes elements; deeper ones add bytes. */
            uint32_t elem_bytes = n * 4;
            VarInfo *vi = lv->vi;
            bool outer = !lv->has_dynamic && !lv->has_byte_offset && lv->vi->type->kind == TY_ARRAY &&
                         at == lv->vi->type && lv->count == vi->count;
            if (outer) {
                lv->dynamic = index;
                lv->has_dynamic = true;
            } else {
                IrSrc bytes = lw_op2(l, OP_IMUL, index, ir_imm(elem_bytes));
                if (lv->has_byte_offset)
                    bytes = lw_op2(l, OP_IADD, lv->byte_offset, bytes);
                lv->byte_offset = bytes;
                lv->has_byte_offset = true;
            }
            lv->count = n;
            return true;
        }
        case LV_STRUCTURED: {
            uint32_t elem_bytes = n ? (lv->comps[n < lv->count ? n : 0] - lv->comps[0]) : 0;
            if (lv->count > n)
                elem_bytes = lv->comps[n] - lv->comps[0];
            IrSrc bytes = lw_op2(l, OP_IMUL, index, ir_imm(elem_bytes));
            if (lv->has_byte_offset)
                bytes = lw_op2(l, OP_IADD, lv->byte_offset, bytes);
            lv->byte_offset = bytes;
            lv->has_byte_offset = true;
            lv->count = n;
            return true;
        }
        case LV_REGS:
            if (lv->vi->kind == VK_CB) {
                /* registers per element of the array, as cbuffers lay arrays out. */
                uint32_t stride_bytes = lv->count > n ? lv->comps[0] : 0;
                VarInfo *vi = lv->vi;
                stride_bytes = (n < lv->count) ? vi->offsets[lv->comps[n]] - vi->offsets[lv->comps[0]] : 16;
                IrSrc scaled = lw_op2(l, OP_IMUL, index, ir_imm(stride_bytes / 16 ? stride_bytes / 16 : 1));
                if (lv->has_dynamic)
                    scaled = lw_op2(l, OP_IADD, lv->dynamic, scaled);
                lv->dynamic = scaled;
                lv->has_dynamic = true;
                lv->count = n;
                return true;
            }
            if (lv->vi->kind == VK_VERTICES || lv->vi->kind == VK_CONTROL_POINTS) {
                lv->dynamic = index;
                lv->has_dynamic = true;
                lv->count = n;
                return true;
            }
            if (at->kind == TY_VECTOR || at->kind == TY_MATRIX) {
                lv->kind = LV_REGS_DYNAMIC;
                lv->dynamic = index;
                lv->has_dynamic = true;
                /* comps keeps the candidates of every element; count is the element size. */
                lv->elements = at->kind == TY_VECTOR ? at->cols : at->rows;
                lv->count = n;
                return true;
            }
            lw_error(l, e->loc, 3000, "unsupported dynamic index of '%s'", lv->vi->var ? lv->vi->var->name : "");
            return false;
        default:
            lw_error(l, e->loc, 3000, "unsupported dynamic index");
            return false;
        }
    }
    default:
        break;
    }
    /* an rvalue: read it into registers. */
    Value v = lw_expr(l, e);
    VarInfo *vi = new_regs(l, nullptr, e->type);
    for (uint32_t i = 0; i < vi->count && i < v.count; i++)
        set_known(l, vi, i, v.c[i]);
    lv->kind = LV_REGS;
    lv->vi = vi;
    lv->count = vi->count;
    lv->comps = hl_alloc(h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < vi->count; i++)
        lv->comps[i] = i;
    return true;
}

static bool object_element_lvalue(Lower *l, HlExpr *e, LValue *lv)
{
    HlExpr *object = e->a;
    lv->obj.var = nullptr;
    if (object->kind == E_OBJECT_PROPERTY) {
        if (!lw_obj(l, object->a, &lv->obj))
            return false;
        lv->kind = LV_TYPED;
        Value pos = lw_expr(l, e->b);
        Value extra = lw_expr(l, object->b);
        /* mips: the level after the coordinates; sample: the index, which ld_ms takes on its own. */
        Value coord = lw_value(l, nullptr, pos.count + 1);
        for (uint32_t i = 0; i < pos.count; i++)
            coord.c[i] = pos.c[i];
        coord.c[pos.count] = extra.c[0];
        lv->coord = coord;
        lv->dynamic.kind = object->intrinsic == PROP_SAMPLE ? 1 : 0;
        lv->count = hl_type_components(e->type);
        lv->comps = hl_alloc(l->h, (lv->count ? lv->count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < lv->count; i++)
            lv->comps[i] = i;
        lv->has_byte_offset = object->intrinsic == PROP_SAMPLE;
        return true;
    }
    if (!lw_obj(l, object, &lv->obj))
        return false;
    uint8_t o = object->type->object;
    VarInfo *ovi = lv->obj.var ? lv->obj.var->lower : nullptr;
    if (ovi && (ovi->kind == VK_VERTICES || ovi->kind == VK_CONTROL_POINTS)) {
        /* patches and geometry shader inputs: an element at a vertex index. */
        LValue base = {.kind = LV_REGS, .vi = ovi};
        base.count = ovi->count;
        base.comps = hl_alloc(l->h, (ovi->count ? ovi->count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < ovi->count; i++)
            base.comps[i] = i;
        uint32_t n = hl_type_components(e->type);
        IrSrc index = lw_scalar(l, e->b);
        if (index.kind == SRC_IMM) {
            if ((index.v + 1) * n > base.count) {
                lw_error(l, e->loc, 3504, "array index out of bounds");
                return false;
            }
            base.comps += index.v * n;
        } else {
            base.dynamic = index_temp(l, index);
            base.has_dynamic = true;
        }
        base.count = n;
        *lv = base;
        return true;
    }
    Value coord = lw_expr(l, e->b);
    lv->coord = coord;
    uint32_t n = hl_type_components(e->type);
    if (o == OBJ_STRUCTURED_BUFFER || o == OBJ_RWSTRUCTURED_BUFFER) {
        lv->kind = LV_STRUCTURED;
        lv->count = n;
        lv->comps = hl_alloc(l->h, (n ? n : 1) * sizeof(uint32_t));
        uint32_t k = 0;
        lw_tight_offsets(e->type, 0, l->s->row_major_default, lv->comps, &k);
        return true;
    }
    lv->kind = LV_TYPED;
    lv->count = n;
    lv->comps = hl_alloc(l->h, (n ? n : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++)
        lv->comps[i] = i;
    return true;
}

static IrSrc bool_load(Lower *l, IrSrc s, uint8_t base)
{
    if (hl_base_storage(base) == BT_BOOL && s.kind != SRC_IMM)
        return lw_op2(l, OP_INE, s, ir_imm(0));
    return s;
}

Value lw_load(Lower *l, LValue *lv)
{
    Hlsl *h = l->h;
    Value r = lw_value(l, lv->type, lv->count);
    VarInfo *vi = lv->vi;
    switch (lv->kind) {
    case LV_REGS:
        if (vi->kind == VK_CB) {
            IrSrc rel = {};
            if (lv->has_dynamic)
                rel = index_temp(l, lv->dynamic);
            for (uint32_t i = 0; i < lv->count; i++) {
                uint32_t off = vi->offsets[lv->comps[i]];
                IrSrc c = {.kind = SRC_CB, .v = vi->buffer, .index = off / 16, .comp = (uint8_t)((off / 4) % 4)};
                if (lv->has_dynamic) {
                    if (rel.kind == SRC_IMM)
                        c.index += rel.v;
                    else
                        c.rel = rel.v;
                }
                r.c[i] = bool_load(l, c, hl_type_component_base(vi->type, lv->comps[i]));
            }
            return r;
        }
        if (vi->kind == VK_VERTICES || vi->kind == VK_CONTROL_POINTS) {
            IrSrc rel = {};
            if (lv->has_dynamic)
                rel = index_temp(l, lv->dynamic);
            for (uint32_t i = 0; i < lv->count; i++) {
                uint32_t comp = lv->comps[i];
                uint32_t vertex = comp / vi->vertex_components, within = comp % vi->vertex_components;
                IrSrc c = {.kind = vi->kind == VK_VERTICES ? SRC_IN : SRC_OUT_CP, .v = vi->in_reg[within],
                           .comp = vi->in_comp[within], .index = vertex};
                if (lv->has_dynamic) {
                    if (rel.kind == SRC_IMM)
                        c.index += rel.v;
                    else
                        c.rel = rel.v;
                }
                r.c[i] = c;
            }
            return r;
        }
        for (uint32_t i = 0; i < lv->count; i++) {
            uint32_t c = lv->comps[i];
            r.c[i] = vi->known[c].kind != SRC_NONE ? vi->known[c] : ir_reg(vi->home[c]);
        }
        return r;
    case LV_REGS_DYNAMIC: {
        /* the element at a dynamic index of a vector or matrix row: movc down the candidates. */
        uint32_t n = lv->count, elements = lv->elements;
        IrSrc index = lv->dynamic;
        for (uint32_t k = 0; k < n; k++) {
            uint32_t c0 = lv->comps[k];
            IrSrc acc = vi->known[c0].kind != SRC_NONE ? vi->known[c0] : ir_reg(vi->home[c0]);
            for (uint32_t e = 1; e < elements; e++) {
                uint32_t c = lv->comps[e * n + k];
                if (c >= vi->count)
                    break;
                IrSrc v = vi->known[c].kind != SRC_NONE ? vi->known[c] : ir_reg(vi->home[c]);
                IrSrc is = lw_op2(l, OP_IEQ, index, ir_imm(e));
                acc = lw_movc(l, is, v, acc);
            }
            r.c[k] = acc;
        }
        return r;
    }
    case LV_X: {
        IrSrc rel = {};
        if (lv->has_dynamic)
            rel = lw_mov(l, lv->dynamic);
        for (uint32_t i = 0; i < lv->count; i++) {
            uint32_t c = lv->comps[i];
            IrSrc src = {.kind = SRC_X, .v = vi->x, .index = vi->xreg[c], .comp = vi->xcomp[c]};
            if (lv->has_dynamic) {
                if (rel.kind == SRC_IMM)
                    src.index += rel.v;
                else
                    src.rel = rel.v;
            }
            IrIns *ins = ir_emit(l->s, OP_MOV, 1, 1);
            ins->src[0] = src;
            r.c[i] = ir_reg(ins->dst[0]);
        }
        return r;
    }
    case LV_TGSM: {
        IrSrc element = lv->has_dynamic ? lv->dynamic : ir_imm(0);
        for (uint32_t i = 0; i < lv->count; i++) {
            uint32_t comp = lv->comps[i];
            uint32_t off = (comp % (vi->stride / 4 ? vi->stride / 4 : 1)) * 4;
            uint32_t elem = vi->stride ? comp / (vi->stride / 4) : 0;
            IrSrc e = element.kind == SRC_IMM ? ir_imm(element.v + elem) : (elem ? lw_op2(l, OP_IADD, element, ir_imm(elem)) : element);
            IrSrc bytes = lv->has_byte_offset ? lw_op2(l, OP_IADD, lv->byte_offset, ir_imm(off)) : ir_imm(off);
            IrIns *ins = ir_emit(l->s, OP_LD_STRUCTURED, 1, 2);
            ins->src[0] = e;
            ins->src[1] = bytes;
            ins->res_kind = SRC_TGSM;
            ins->res = vi->tgsm;
            r.c[i] = bool_load(l, ir_reg(ins->dst[0]), hl_type_component_base(lv->type, i));
        }
        return r;
    }
    case LV_TYPED:
    case LV_STRUCTURED:
        return lw_object_load(l, lv);
    default:
        (void)h;
        return r;
    }
}

void lw_store(Lower *l, LValue *lv, Value v)
{
    VarInfo *vi = lv->vi;
    switch (lv->kind) {
    case LV_REGS:
        if (!vi || vi->kind != VK_REGS) {
            l->failed = true;
            return;
        }
        for (uint32_t i = 0; i < lv->count && i < v.count; i++) {
            uint32_t c = lv->comps[i];
            IrSrc value = v.c[i];
            IrIns *ins = ir_emit(l->s, OP_MOV, 0, 1);
            ins->dst_count = 1;
            ins->dst[0] = vi->home[c];
            ins->src[0] = value;
            set_known(l, vi, c, immutable(l, &value) ? value : (IrSrc){});
        }
        return;
    case LV_REGS_DYNAMIC: {
        uint32_t n = lv->count, elements = lv->elements;
        for (uint32_t e = 0; e < elements; e++) {
            IrSrc is = lw_op2(l, OP_IEQ, lv->dynamic, ir_imm(e));
            for (uint32_t k = 0; k < n && k < v.count; k++) {
                uint32_t c = lv->comps[e * n + k];
                if (c >= vi->count)
                    continue;
                IrSrc old = vi->known[c].kind != SRC_NONE ? vi->known[c] : ir_reg(vi->home[c]);
                IrSrc nv = lw_movc(l, is, v.c[k], old);
                IrIns *ins = ir_emit(l->s, OP_MOV, 0, 1);
                ins->dst_count = 1;
                ins->dst[0] = vi->home[c];
                ins->src[0] = nv;
                set_known(l, vi, c, immutable(l, &nv) ? nv : (IrSrc){});
            }
        }
        return;
    }
    case LV_X: {
        IrSrc rel = {};
        if (lv->has_dynamic)
            rel = lw_mov(l, lv->dynamic);
        for (uint32_t i = 0; i < lv->count && i < v.count; i++) {
            uint32_t c = lv->comps[i];
            IrIns *ins = ir_emit(l->s, IR_STORE, 0, 1);
            ins->store = (IrDst){.kind = DST_X, .v = vi->x, .index = vi->xreg[c], .comp = vi->xcomp[c]};
            if (lv->has_dynamic) {
                if (rel.kind == SRC_IMM)
                    ins->store.index += rel.v;
                else
                    ins->store.rel = rel.v;
            }
            ins->src[0] = v.c[i];
        }
        return;
    }
    case LV_TGSM: {
        IrSrc element = lv->has_dynamic ? lv->dynamic : ir_imm(0);
        for (uint32_t i = 0; i < lv->count && i < v.count; i++) {
            uint32_t comp = lv->comps[i];
            uint32_t per = vi->stride / 4 ? vi->stride / 4 : 1;
            uint32_t off = (comp % per) * 4, elem = comp / per;
            IrSrc e = element.kind == SRC_IMM ? ir_imm(element.v + elem) : (elem ? lw_op2(l, OP_IADD, element, ir_imm(elem)) : element);
            IrSrc bytes = lv->has_byte_offset ? lw_op2(l, OP_IADD, lv->byte_offset, ir_imm(off)) : ir_imm(off);
            IrIns *ins = ir_emit(l->s, OP_STORE_STRUCTURED, 0, 3);
            ins->src[0] = e;
            ins->src[1] = bytes;
            ins->src[2] = v.c[i];
            ins->res_kind = SRC_TGSM;
            ins->res = vi->tgsm;
        }
        return;
    }
    case LV_TYPED:
    case LV_STRUCTURED:
        lw_object_store(l, lv, v);
        return;
    default:
        l->failed = true;
        return;
    }
}

/* ------------------------------------------------------------------ declarations */

static void declare_local(Lower *l, HlVar *var)
{
    if (var->type->kind == TY_OBJECT || hl_type_has_object(var->type)) {
        VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
        vi->kind = VK_OBJECT;
        vi->var = var;
        vi->type = var->type;
        var->lower = vi;
        if (var->init) {
            uint32_t n = object_elements(var->type);
            vi->objects = hl_alloc(l->h, n * sizeof(ObjRef));
            vi->object_count = n;
            if (var->init->kind == E_CONSTRUCT) {
                for (uint32_t i = 0; i < var->init->arg_count && i < n; i++)
                    lw_obj(l, var->init->args[i], &vi->objects[i]);
            } else {
                lw_obj(l, var->init, &vi->objects[0]);
            }
        }
        return;
    }
    VarInfo *vi = new_local(l, var, var->type);
    var->lower = vi;
    /* indexable arrays start undefined, as fxc leaves them. */
    if (!var->init && vi->kind == VK_X)
        return;
    Value init;
    if (var->init)
        init = lw_convert(l, lw_expr(l, var->init), var->type);
    else
        init = zero_value(l, var->type);
    LValue lv = {.kind = vi->kind == VK_X ? LV_X : LV_REGS, .vi = vi, .type = var->type, .count = vi->count};
    lv.comps = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < vi->count; i++)
        lv.comps[i] = i;
    /* uninitialized locals start as zero, as fxc's code reads them. */
    lw_store(l, &lv, init);
}

/* ------------------------------------------------------------------ statements */

static void lower_stmt(Lower *l, HlStmt *s);

static bool stmt_returns(const HlStmt *s)
{
    if (!s)
        return false;
    switch (s->kind) {
    case S_RETURN:
        return true;
    case S_BLOCK:
        for (uint32_t i = 0; i < s->stmt_count; i++)
            if (stmt_returns(s->stmts[i]))
                return true;
        return false;
    case S_IF:
        return stmt_returns(s->body) || stmt_returns(s->other);
    case S_FOR:
    case S_WHILE:
    case S_DO:
    case S_SWITCH:
        return stmt_returns(s->body);
    default:
        return false;
    }
}

static bool stmt_jumps(const HlStmt *s, int depth, bool breaks)
{
    if (!s)
        return false;
    switch (s->kind) {
    case S_BREAK:
        return depth == 0 && breaks;
    case S_CONTINUE:
        return depth == 0;
    case S_BLOCK:
        for (uint32_t i = 0; i < s->stmt_count; i++)
            if (stmt_jumps(s->stmts[i], depth, breaks))
                return true;
        return false;
    case S_IF:
        return stmt_jumps(s->body, depth, breaks) || stmt_jumps(s->other, depth, breaks);
    case S_SWITCH:
        return stmt_jumps(s->body, depth, false);
    case S_FOR:
    case S_WHILE:
    case S_DO:
        return stmt_jumps(s->body, depth + 1, breaks);
    default:
        return false;
    }
}

static void emit_marker(Lower *l, uint16_t op) { ir_emit(l->s, op, 0, 0); }

static void emit_cond_marker(Lower *l, uint16_t op, IrSrc c, bool nz)
{
    IrIns *ins = ir_emit(l->s, op, 0, 1);
    ins->src[0] = c;
    if (nz)
        ins->flags |= IRF_NZ;
}

static VarInfo *flag_info(Lower *l) { return l->frame->flag; }

/* the statements of a block from i on, after returns guarded by the flag. */
static void lower_stmts(Lower *l, HlStmt **stmts, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        HlStmt *s = stmts[i];
        lower_stmt(l, s);
        if (s->kind == S_RETURN || s->kind == S_BREAK || s->kind == S_CONTINUE)
            return;
        Frame *f = l->frame;
        if (f && f->needs_flag && stmt_returns(s) && i + 1 < count) {
            VarInfo *flag = flag_info(l);
            IrSrc v = flag->known[0].kind != SRC_NONE ? flag->known[0] : ir_reg(flag->home[0]);
            if (v.kind == SRC_IMM) {
                if (v.v)
                    return;
                continue;
            }
            if (l->loops > f->loops) {
                emit_cond_marker(l, OP_BREAKC, v, true);
                continue;
            }
            emit_cond_marker(l, OP_IF, v, false);
            uint32_t mark = l->undo_count;
            lower_stmts(l, stmts + i + 1, count - i - 1);
            Touched t = {};
            collect_and_undo(l, mark, &t);
            forget_touched(l, &t);
            emit_marker(l, OP_ENDIF);
            return;
        }
    }
}

static const HlAttribute *attr(const HlStmt *s, const char *name)
{
    for (uint32_t i = 0; i < s->attr_count; i++)
        if (!hl_stricmp(s->attrs[i].name, name))
            return &s->attrs[i];
    return nullptr;
}

static void lower_if(Lower *l, HlStmt *s)
{
    IrSrc c = lw_scalar(l, s->cond);
    if (c.kind == SRC_IMM) {
        uint32_t mark = l->undo_count;
        (void)mark;
        if (c.v)
            lower_stmt(l, s->body);
        else if (s->other)
            lower_stmt(l, s->other);
        return;
    }
    if (c.kind != SRC_REG)
        c = lw_mov(l, c);
    emit_cond_marker(l, OP_IF, c, true);
    uint32_t mark = l->undo_count;
    Touched t = {};
    lower_stmt(l, s->body);
    collect_and_undo(l, mark, &t);
    if (s->other) {
        emit_marker(l, OP_ELSE);
        lower_stmt(l, s->other);
        collect_and_undo(l, mark, &t);
    }
    emit_marker(l, OP_ENDIF);
    forget_touched(l, &t);
}

/* loops: a dynamic loop, or the body repeated while the condition folds to true. */
static bool try_unroll(Lower *l, HlStmt *s, uint32_t limit)
{
    uint32_t ins_mark = l->s->count, undo_mark = l->undo_count, live_mark = l->live_count;
    uint32_t icb_mark = l->s->icb_count;
    for (uint32_t iteration = 0;; iteration++) {
        if (s->cond) {
            IrSrc c = lw_scalar(l, s->cond);
            if (c.kind != SRC_IMM)
                goto fail;
            if (!c.v)
                return true;
        }
        if (iteration >= limit || l->s->count - ins_mark > 65536)
            goto fail;
        lower_stmt(l, s->body);
        if (l->failed)
            return true;
        if (s->iter)
            lw_expr(l, s->iter);
    }
fail:
    l->s->count = ins_mark;
    undo_to(l, undo_mark);
    l->live_count = live_mark;
    l->s->icb_count = icb_mark;
    return false;
}

static void lower_loop(Lower *l, HlStmt *s)
{
    if (s->kind == S_FOR && s->init)
        lower_stmt(l, s->init);
    bool force_loop = attr(s, "loop") != nullptr;
    const HlAttribute *unroll = attr(s, "unroll");
    bool jumps = stmt_jumps(s->body, 0, true) || stmt_returns(s->body);
    if (!force_loop && !jumps && s->kind == S_FOR) {
        uint32_t limit = 32;
        if (unroll) {
            limit = 1024;
            if (unroll->arg_count && unroll->args[0]->kind == E_CONST)
                limit = unroll->args[0]->values[0].u;
        }
        if (try_unroll(l, s, limit))
            return;
        if (unroll && !(unroll->arg_count))
            hl_warning(l->h, s->loc, 3557, "loop doesn't seem to do anything, consider removing [unroll]");
    }
    forget_all(l);
    emit_marker(l, OP_LOOP);
    l->loops++;
    if (s->kind != S_DO && s->cond) {
        IrSrc c = lw_scalar(l, s->cond);
        if (c.kind == SRC_IMM) {
            if (!c.v)
                emit_marker(l, OP_BREAK);
        } else {
            emit_cond_marker(l, OP_BREAKC, c, false);
        }
    }
    /* continue runs the iterator first; the loop records it for continue statements. */
    HlStmt *outer_loop = l->loop_stmt;
    l->loop_stmt = s;
    lower_stmt(l, s->body);
    l->loop_stmt = outer_loop;
    if (s->kind == S_DO) {
        IrSrc c = lw_scalar(l, s->cond);
        if (c.kind == SRC_IMM) {
            if (!c.v)
                emit_marker(l, OP_BREAK);
        } else {
            emit_cond_marker(l, OP_BREAKC, c, false);
        }
    } else if (s->iter) {
        lw_expr(l, s->iter);
    }
    l->loops--;
    emit_marker(l, OP_ENDLOOP);
    forget_all(l);
}

static void lower_switch(Lower *l, HlStmt *s)
{
    IrSrc c = lw_scalar(l, s->cond);
    if (c.kind != SRC_REG)
        c = lw_mov(l, c);
    IrIns *sw = ir_emit(l->s, OP_SWITCH, 0, 1);
    sw->src[0] = c;
    uint32_t mark = l->undo_count;
    Touched t = {};
    HlStmt *body = s->body;
    HlStmt *outer = l->loop_stmt;
    uint32_t loops = l->loops;
    /* break in a switch leaves the switch, as in a loop. */
    l->loops++;
    HlStmt **stmts = body && body->kind == S_BLOCK ? body->stmts : &body;
    uint32_t count = body && body->kind == S_BLOCK ? body->stmt_count : (body ? 1 : 0);
    bool open = false;
    for (uint32_t i = 0; i < count; i++) {
        HlStmt *st = stmts[i];
        if (st->kind == S_CASE || st->kind == S_DEFAULT) {
            collect_and_undo(l, mark, &t);
            IrIns *ins = ir_emit(l->s, st->kind == S_CASE ? OP_CASE : OP_DEFAULT, 0, 0);
            ins->extra = st->value.u;
            open = true;
            continue;
        }
        lower_stmt(l, st);
    }
    (void)open;
    collect_and_undo(l, mark, &t);
    l->loops = loops;
    l->loop_stmt = outer;
    emit_marker(l, OP_ENDSWITCH);
    forget_touched(l, &t);
}

static void lower_return(Lower *l, HlStmt *s)
{
    Frame *f = l->frame;
    if (s->expr && f->ret) {
        Value v = lw_convert(l, lw_expr(l, s->expr), f->func->ret);
        LValue lv = {.kind = f->ret->kind == VK_X ? LV_X : LV_REGS, .vi = f->ret, .type = f->func->ret,
                     .count = f->ret->count};
        lv.comps = hl_alloc(l->h, (lv.count ? lv.count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < lv.count; i++)
            lv.comps[i] = i;
        lw_store(l, &lv, v);
    } else if (s->expr) {
        lw_expr(l, s->expr);
    }
    if (f->needs_flag) {
        VarInfo *flag = flag_info(l);
        LValue lv = {.kind = LV_REGS, .vi = flag, .count = 1};
        lv.comps = hl_alloc(l->h, sizeof(uint32_t));
        Value one = lw_value(l, nullptr, 1);
        one.c[0] = ir_imm(~0u);
        lw_store(l, &lv, one);
        if (l->loops > f->loops)
            emit_marker(l, OP_BREAK);
    }
}

static void lower_stmt(Lower *l, HlStmt *s)
{
    if (!s || l->failed)
        return;
    switch (s->kind) {
    case S_EMPTY:
    case S_CASE:
    case S_DEFAULT:
        return;
    case S_BLOCK:
        lower_stmts(l, s->stmts, s->stmt_count);
        return;
    case S_EXPR:
        if (s->expr)
            lw_expr(l, s->expr);
        return;
    case S_DECL:
        for (uint32_t i = 0; i < s->var_count; i++)
            declare_local(l, s->vars[i]);
        return;
    case S_IF:
        lower_if(l, s);
        return;
    case S_FOR:
    case S_WHILE:
    case S_DO:
        lower_loop(l, s);
        return;
    case S_SWITCH:
        lower_switch(l, s);
        return;
    case S_BREAK:
        emit_marker(l, OP_BREAK);
        return;
    case S_CONTINUE: {
        HlStmt *loop = l->loop_stmt;
        if (loop && loop->kind == S_FOR && loop->iter)
            lw_expr(l, loop->iter);
        if (loop && loop->kind == S_DO && loop->cond) {
            IrSrc c = lw_scalar(l, loop->cond);
            if (c.kind == SRC_IMM) {
                if (!c.v)
                    emit_marker(l, OP_BREAK);
            } else {
                emit_cond_marker(l, OP_BREAKC, c, false);
            }
        }
        emit_marker(l, OP_CONTINUE);
        return;
    }
    case S_RETURN:
        lower_return(l, s);
        return;
    case S_DISCARD: {
        IrIns *ins = ir_emit(l->s, OP_DISCARD, 0, 1);
        ins->src[0] = ir_imm(~0u);
        ins->flags |= IRF_NZ;
        l->s->uses_discard = true;
        return;
    }
    }
}

/* ------------------------------------------------------------------ calls */

static bool returns_only_at_end(const HlStmt *body)
{
    if (!body || body->kind != S_BLOCK)
        return !stmt_returns(body);
    for (uint32_t i = 0; i + 1 < body->stmt_count; i++)
        if (stmt_returns(body->stmts[i]))
            return false;
    if (!body->stmt_count)
        return true;
    const HlStmt *last = body->stmts[body->stmt_count - 1];
    return last->kind == S_RETURN || !stmt_returns(last);
}

Value lw_call_function(Lower *l, HlFunc *f, HlExpr **args, uint32_t count, HlLoc loc, HlExpr *object);

static Value lower_call(Lower *l, HlExpr *e)
{
    return lw_call_function(l, e->func, e->args, e->arg_count, e->loc, e->a);
}

Value lw_call_function(Lower *l, HlFunc *f, HlExpr **args, uint32_t count, HlLoc loc, HlExpr *object)
{
    Hlsl *h = l->h;
    HlFunc *def = f->body ? f : f->definition;
    if (!def && f->decl)
        def = f->decl->definition;
    if (!def) {
        lw_error(l, loc, 3501, "'%s': function is declared but not defined", f->name);
        return zero_value(l, f->ret);
    }
    if (def->active) {
        lw_error(l, loc, 3500, "'%s': recursive functions are not allowed", f->name);
        return zero_value(l, f->ret);
    }
    /* arguments are read before the parameters take them. */
    Value *values = hl_alloc(h, (count + 1) * sizeof(Value));
    LValue *outs = hl_alloc(h, (count + 1) * sizeof(LValue));
    bool *has_out = hl_alloc(h, (count + 1) * sizeof(bool));
    ObjRef *objs = hl_alloc(h, (count + 1) * sizeof(ObjRef));
    for (uint32_t i = 0; i < count && i < def->param_count; i++) {
        HlVar *param = def->params[i];
        if (param->type->kind == TY_OBJECT || hl_type_has_object(param->type)) {
            lw_obj(l, args[i], &objs[i]);
            continue;
        }
        if (param->storage & ST_OUT) {
            has_out[i] = lw_lvalue(l, args[i], &outs[i]);
            if (param->storage & ST_IN)
                values[i] = has_out[i] ? lw_convert(l, lw_load(l, &outs[i]), param->type) : zero_value(l, param->type);
            else
                values[i] = zero_value(l, param->type);
        } else {
            values[i] = lw_convert(l, lw_expr(l, args[i]), param->type);
        }
    }
    /* methods take their object as this, and give it back when they change it. */
    LValue self = {};
    bool self_out = false;
    VarInfo *self_info = nullptr;
    if (def->this_var && object) {
        Value v;
        if (object->lvalue && lw_lvalue(l, object, &self)) {
            v = lw_load(l, &self);
            self_out = def->this_var->assigned;
        } else {
            v = lw_expr(l, object);
        }
        self_info = new_local(l, def->this_var, def->this_var->type);
        LValue lv = {.kind = self_info->kind == VK_X ? LV_X : LV_REGS, .vi = self_info, .type = def->this_var->type,
                     .count = self_info->count};
        lv.comps = hl_alloc(h, (lv.count ? lv.count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < lv.count; i++)
            lv.comps[i] = i;
        lw_store(l, &lv, lw_convert(l, v, def->this_var->type));
    }
    def->active++;
    Frame frame = {.func = def, .outer = l->frame};
    frame.loops = l->loops;
    frame.needs_flag = !returns_only_at_end(def->body);
    if (def->ret->kind != TY_VOID)
        frame.ret = new_local(l, nullptr, def->ret);
    if (frame.needs_flag) {
        VarInfo *flag = new_regs(l, nullptr, hl_type_scalar(h, BT_BOOL));
        frame.flag = flag;
        LValue lv = {.kind = LV_REGS, .vi = flag, .count = 1};
        lv.comps = hl_alloc(h, sizeof(uint32_t));
        Value zero = lw_value(l, nullptr, 1);
        zero.c[0] = ir_imm(0);
        lw_store(l, &lv, zero);
    }
    if (frame.ret) {
        /* paths that miss a return leave the result zero. */
        LValue lv = {.kind = frame.ret->kind == VK_X ? LV_X : LV_REGS, .vi = frame.ret, .count = frame.ret->count};
        lv.comps = hl_alloc(h, (lv.count ? lv.count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < lv.count; i++)
            lv.comps[i] = i;
        if (frame.needs_flag)
            lw_store(l, &lv, zero_value(l, def->ret));
    }
    for (uint32_t i = 0; i < def->param_count; i++) {
        HlVar *param = def->params[i];
        if (param->type->kind == TY_OBJECT || hl_type_has_object(param->type)) {
            VarInfo *vi = hl_alloc(h, sizeof(VarInfo));
            vi->kind = VK_OBJECT;
            vi->var = param;
            vi->type = param->type;
            if (i < count) {
                ObjRef *o = &objs[i];
                VarInfo *src = o->var ? o->var->lower : nullptr;
                if (src && (src->kind == VK_VERTICES || src->kind == VK_CONTROL_POINTS)) {
                    param->lower = src;
                    continue;
                }
                vi->objects = hl_alloc(h, sizeof(ObjRef));
                vi->objects[0] = *o;
                vi->object_count = 1;
                if (src && src->kind == VK_OBJECT && src->object_count > 1 && !o->element) {
                    vi->objects = src->objects;
                    vi->object_count = src->object_count;
                }
            }
            param->lower = vi;
            continue;
        }
        VarInfo *vi = new_local(l, param, param->type);
        param->lower = vi;
        Value v = i < count ? values[i] : (param->init ? lw_convert(l, lw_expr(l, param->init), param->type)
                                                       : zero_value(l, param->type));
        LValue lv = {.kind = vi->kind == VK_X ? LV_X : LV_REGS, .vi = vi, .type = param->type, .count = vi->count};
        lv.comps = hl_alloc(h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
        for (uint32_t k = 0; k < vi->count; k++)
            lv.comps[k] = k;
        lw_store(l, &lv, v);
    }
    Frame *outer = l->frame;
    l->frame = &frame;
    HlStmt *outer_loop = l->loop_stmt;
    l->loop_stmt = nullptr;
    VarInfo *saved_self = def->this_var ? def->this_var->lower : nullptr;
    if (self_info)
        def->this_var->lower = self_info;
    lower_stmt(l, def->body);
    l->loop_stmt = outer_loop;
    l->frame = outer;
    def->active--;
    if (self_info && self_out) {
        LValue lv = {.kind = self_info->kind == VK_X ? LV_X : LV_REGS, .vi = self_info, .type = def->this_var->type,
                     .count = self_info->count};
        lv.comps = hl_alloc(h, (lv.count ? lv.count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < lv.count; i++)
            lv.comps[i] = i;
        lw_store(l, &self, lw_load(l, &lv));
    }
    if (def->this_var)
        def->this_var->lower = saved_self;
    /* out parameters copy back. */
    for (uint32_t i = 0; i < count && i < def->param_count; i++) {
        HlVar *param = def->params[i];
        if (!(param->storage & ST_OUT) || !has_out[i] || param->type->kind == TY_OBJECT)
            continue;
        VarInfo *vi = param->lower;
        LValue lv = {.kind = vi->kind == VK_X ? LV_X : LV_REGS, .vi = vi, .type = param->type, .count = vi->count};
        lv.comps = hl_alloc(h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
        for (uint32_t k = 0; k < vi->count; k++)
            lv.comps[k] = k;
        Value v = lw_load(l, &lv);
        lw_store(l, &outs[i], lw_convert(l, v, outs[i].type));
    }
    if (!frame.ret)
        return lw_value(l, def->ret, 0);
    LValue lv = {.kind = frame.ret->kind == VK_X ? LV_X : LV_REGS, .vi = frame.ret, .type = def->ret,
                 .count = frame.ret->count};
    lv.comps = hl_alloc(h, (lv.count ? lv.count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < lv.count; i++)
        lv.comps[i] = i;
    return lw_load(l, &lv);
}

VarInfo *lw_new_local(Lower *l, HlVar *var, HlType *type) { return new_local(l, var, type); }

Value lw_entry_body(Lower *l, HlFunc *f)
{
    Hlsl *h = l->h;
    Frame frame = {.func = f, .entry = true, .loops = l->loops};
    frame.needs_flag = !returns_only_at_end(f->body);
    if (f->ret->kind != TY_VOID)
        frame.ret = new_local(l, nullptr, f->ret);
    if (frame.needs_flag) {
        frame.flag = new_regs(l, nullptr, hl_type_scalar(h, BT_BOOL));
        LValue lv = {.kind = LV_REGS, .vi = frame.flag, .count = 1};
        lv.comps = hl_alloc(h, sizeof(uint32_t));
        Value zero = lw_value(l, nullptr, 1);
        zero.c[0] = ir_imm(0);
        lw_store(l, &lv, zero);
    }
    LValue rv = {};
    if (frame.ret) {
        rv = (LValue){.kind = frame.ret->kind == VK_X ? LV_X : LV_REGS, .vi = frame.ret, .type = f->ret,
                      .count = frame.ret->count};
        rv.comps = hl_alloc(h, (rv.count ? rv.count : 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < rv.count; i++)
            rv.comps[i] = i;
        lw_store(l, &rv, zero_value(l, f->ret));
    }
    f->active++;
    l->frame = &frame;
    lower_stmt(l, f->body);
    l->frame = nullptr;
    f->active--;
    if (!frame.ret)
        return lw_value(l, f->ret, 0);
    return lw_load(l, &rv);
}

/* ------------------------------------------------------------------ globals */

static VarInfo *object_info(Lower *l, HlVar *var)
{
    VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
    vi->kind = VK_OBJECT;
    vi->var = var;
    vi->type = var->type;
    return vi;
}

void lw_globals(Lower *l)
{
    HlProgram *prog = l->prog;
    for (uint32_t i = 0; i < prog->buffer_count; i++)
        layout_buffer(l, prog->buffers[i], i);
    for (uint32_t i = 0; i < prog->global_count; i++) {
        HlVar *v = prog->globals[i];
        if (v->buffer)
            continue;
        if (v->type->kind == TY_OBJECT || hl_type_has_object(v->type)) {
            v->lower = object_info(l, v);
            continue;
        }
        if (v->storage & ST_GROUPSHARED) {
            VarInfo *vi = hl_alloc(l->h, sizeof(VarInfo));
            vi->kind = VK_TGSM;
            vi->var = v;
            vi->type = v->type;
            vi->count = hl_type_components(v->type);
            const HlType *elem = v->type->kind == TY_ARRAY ? v->type->elem : v->type;
            uint32_t count = v->type->kind == TY_ARRAY ? v->type->count : 1;
            vi->stride = hl_type_components(elem) * 4;
            IrTgsm g = {.var = v, .stride = vi->stride ? vi->stride : 4, .count = count};
            HL_PUSH(l->h, l->s->tgsm, l->s->tgsm_count, l->s->tgsm_capacity, g);
            vi->tgsm = l->s->tgsm_count - 1;
            v->lower = vi;
            continue;
        }
        if (v->storage & ST_STATIC) {
            if (v->type->kind == TY_STRING)
                continue;
            /* statics live in registers and start with their initializers. */
            v->lower = nullptr;
            continue;
        }
    }
}

/* statics take their values at the start of the entry, in declaration order. */
void lw_statics(Lower *l)
{
    HlProgram *prog = l->prog;
    for (uint32_t i = 0; i < prog->global_count; i++) {
        HlVar *v = prog->globals[i];
        if (!(v->storage & ST_STATIC) || v->buffer || v->type->kind == TY_STRING || v->lower ||
            hl_type_has_object(v->type) || (v->storage & ST_GROUPSHARED))
            continue;
        if ((v->storage & ST_CONST) && v->init && v->init->kind == E_CONST && !v->dynamic_index && !v->assigned &&
            v->type->kind != TY_ARRAY) {
            /* constants fold where they are read. */
        }
        declare_local(l, v);
    }
}
