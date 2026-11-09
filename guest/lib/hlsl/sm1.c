#include "ir.h"
#include "../dxbc/dxbc.h"

uint32_t lw_legacy_regs(const HlType *t, bool rm);
bool ir_is_marker(uint16_t op);

/* register types of parameter tokens. */
enum {
    RT_TEMP = 0,
    RT_INPUT = 1,
    RT_CONST = 2,
    RT_TEXTURE = 3,
    RT_RASTOUT = 4,
    RT_ATTROUT = 5,
    RT_OUTPUT = 6,
    RT_CONSTINT = 7,
    RT_COLOROUT = 8,
    RT_DEPTHOUT = 9,
    RT_SAMPLER = 10,
    RT_LOOP = 15,
    RT_MISCTYPE = 17,
};

enum {
    D9_MOV = 1, D9_ADD = 2, D9_MAD = 4, D9_MUL = 5, D9_RCP = 6, D9_RSQ = 7, D9_DP3 = 8, D9_DP4 = 9, D9_MIN = 10,
    D9_MAX = 11, D9_SLT = 12, D9_SGE = 13, D9_EXP = 14, D9_LOG = 15, D9_LRP = 18, D9_FRC = 19, D9_DCL = 31,
    D9_ABS = 35, D9_SINCOS = 37, D9_REP = 38, D9_ENDREP = 39, D9_IFC = 41, D9_ELSE = 42, D9_ENDIF = 43,
    D9_BREAK = 44, D9_BREAKC = 45, D9_DEFI = 48, D9_TEXKILL = 65, D9_TEX = 66, D9_DEF = 81, D9_CMP = 88,
    D9_DSX = 91, D9_DSY = 92, D9_TEXLDD = 93, D9_TEXLDL = 95, D9_COMMENT = 0xFFFE, D9_END = 0xFFFF,
};

enum { CMP_GT = 1, CMP_EQ = 2, CMP_GE = 3, CMP_LT = 4, CMP_NE = 5, CMP_LE = 6 };

typedef struct Reg {
    uint8_t type;
    uint32_t num;
    uint8_t comp;
    uint8_t mod;
} Reg;

typedef struct S1 {
    IrShader *s;
    Hlsl *h;
    int32_t *slots;
    uint32_t temps, scratch, scratch_max;
    bool ps, sm3;
    uint32_t const_base;
    uint32_t *defs;
    uint32_t def_count, def_capacity;
    uint32_t *words;
    uint32_t count, capacity;
    uint8_t in_type[64], out_type[64];
    uint64_t shadowed;
    /* texld of shader model 2 reads its coordinates without a swizzle. */
    bool exact;
    uint32_t shadow[64];
    uint32_t in_num[64], out_num[64];
    bool failed;
} S1;

static void w(S1 *e, uint32_t v) { HL_PUSH(e->h, e->words, e->count, e->capacity, v); }

static void fail(S1 *e, const char *what)
{
    if (!e->failed)
        hl_error(e->h, (HlLoc){0, 1, 1}, 3000, "%s is not supported for %s", what, e->s->profile.name);
    e->failed = true;
}

static uint32_t param(uint8_t type, uint32_t num) { return 0x80000000u | (num & 0x7FF) | (uint32_t)(type & 7) << 28 | (uint32_t)(type & 0x18) << 8; }

static void dst_token(S1 *e, Reg r, uint32_t mask, bool sat) { w(e, param(r.type, r.num) | mask << 16 | (sat ? 1u << 20 : 0)); }

static void src_token(S1 *e, Reg r, uint32_t swizzle)
{
    uint32_t mod = 0;
    if (r.mod & MODIFIER_ABS)
        mod = r.mod & MODIFIER_NEG ? 0xC : 0xB;
    else if (r.mod & MODIFIER_NEG)
        mod = 1;
    w(e, param(r.type, r.num) | swizzle << 16 | mod << 24);
}

static uint32_t replicate(uint8_t c) { return c | c << 2 | c << 4 | (uint32_t)c << 6; }

/* an immediate in a register of def constants, shared by equal values. */
static Reg constant(S1 *e, uint32_t bits)
{
    for (uint32_t i = 0; i < e->def_count; i++)
        if (e->defs[i] == bits)
            return (Reg){RT_CONST, e->const_base + i / 4, (uint8_t)(i % 4), 0};
    HL_PUSH(e->h, e->defs, e->def_count, e->def_capacity, bits);
    uint32_t i = e->def_count - 1;
    return (Reg){RT_CONST, e->const_base + i / 4, (uint8_t)(i % 4), 0};
}

static Reg constf(S1 *e, float f)
{
    union { float f; uint32_t u; } c = {.f = f};
    return constant(e, c.u);
}

static Reg scratch(S1 *e)
{
    if (e->scratch >= 8)
        fail(e, "an instruction this complex");
    uint32_t r = e->temps + e->scratch++;
    if (e->scratch > e->scratch_max)
        e->scratch_max = e->scratch;
    return (Reg){RT_TEMP, r, 0, 0};
}

static void ins(S1 *e, uint32_t op, uint32_t length)
{
    w(e, op | (e->sm3 || e->s->profile.major >= 2 ? length << 24 : 0));
}

/* an instruction of one result component and scalar sources. */
static void op_n(S1 *e, uint32_t op, Reg d, bool sat, const Reg *srcs, uint32_t n)
{
    /* one constant register per instruction: the others through temps. */
    Reg moved[4];
    for (uint32_t i = 0; i < n && i < 4; i++) {
        moved[i] = srcs[i];
        if (srcs[i].type != RT_CONST)
            continue;
        for (uint32_t j = 0; j < i; j++)
            if (moved[j].type == RT_CONST && moved[j].num != srcs[i].num) {
                Reg t = scratch(e), plain = srcs[i];
                plain.mod = 0;
                ins(e, D9_MOV, 2);
                dst_token(e, t, 1u << srcs[i].comp, false);
                src_token(e, plain, replicate(plain.comp));
                t.comp = srcs[i].comp;
                t.mod = srcs[i].mod;
                moved[i] = t;
                break;
            }
    }
    srcs = moved;
    ins(e, op, 1 + n);
    dst_token(e, d, 1u << d.comp, sat);
    for (uint32_t i = 0; i < n; i++)
        src_token(e, srcs[i], replicate(srcs[i].comp));
}

static void op1(S1 *e, uint32_t op, Reg d, Reg a) { op_n(e, op, d, false, &a, 1); }
static void op2(S1 *e, uint32_t op, Reg d, Reg a, Reg b) { Reg s[2] = {a, b}; op_n(e, op, d, false, s, 2); }
static void op3(S1 *e, uint32_t op, Reg d, Reg a, Reg b, Reg c) { Reg s[3] = {a, b, c}; op_n(e, op, d, false, s, 3); }

static Reg neg(Reg r) { r.mod ^= MODIFIER_NEG; return r; }

/* shader model 2 has no abs modifier: abs into a temp. */
static Reg plain_abs(S1 *e, Reg r)
{
    if (!(r.mod & MODIFIER_ABS) || e->sm3)
        return r;
    bool negate = r.mod & MODIFIER_NEG;
    Reg t = scratch(e);
    r.mod = 0;
    op1(e, D9_ABS, t, r);
    t.mod = negate ? MODIFIER_NEG : 0;
    return t;
}

static Reg src_reg(S1 *e, const IrSrc *s)
{
    Reg r = {};
    switch (s->kind) {
    case SRC_REG: {
        int32_t slot = e->slots[s->v];
        r = (Reg){RT_TEMP, slot >= 0 ? (uint32_t)slot / 4 : 0, (uint8_t)(slot >= 0 ? slot % 4 : 0), 0};
        break;
    }
    case SRC_IMM:
        r = constant(e, s->v);
        break;
    case SRC_CB:
        if (s->rel)
            fail(e, "indexing constants by a value");
        r = (Reg){RT_CONST, s->index, s->comp, 0};
        break;
    case SRC_IN:
        if (s->v >= 64 || !e->in_type[s->v])
            fail(e, "this input");
        r = (Reg){e->in_type[s->v] == 0xFF ? RT_INPUT : e->in_type[s->v], e->in_num[s->v], s->comp, 0};
        break;
    default:
        fail(e, "this operand");
        break;
    }
    r.mod = s->mod;
    return plain_abs(e, r);
}

static Reg dst_reg(S1 *e, const IrIns *i, uint32_t k)
{
    if (i->to_output && !k) {
        const IrDst *d = &i->output;
        if (d->kind != DST_OUT || d->v >= 64)
            fail(e, "this output");
        return (Reg){e->out_type[d->v & 63], e->out_num[d->v & 63], d->comp, 0};
    }
    int32_t slot = e->slots[i->dst[k]];
    if (slot < 0)
        return scratch(e);
    return (Reg){RT_TEMP, (uint32_t)slot / 4, (uint8_t)(slot % 4), 0};
}

/* a vector operand in one register: the scalars moved into a temp unless they share one. */
static Reg gather(S1 *e, const IrSrc *srcs, uint32_t n, uint32_t *swizzle)
{
    Reg first = src_reg(e, &srcs[0]);
    bool same = true;
    uint8_t comps[4] = {first.comp, first.comp, first.comp, first.comp};
    for (uint32_t i = 1; i < n; i++) {
        Reg r = src_reg(e, &srcs[i]);
        same &= r.type == first.type && r.num == first.num && r.mod == first.mod;
        comps[i] = r.comp;
    }
    for (uint32_t i = n; i < 4; i++)
        comps[i] = comps[n - 1];
    bool identity = true;
    for (uint32_t i = 0; i < n; i++)
        identity &= comps[i] == i;
    if (same && (identity || !e->exact)) {
        *swizzle = identity ? 0xE4 : comps[0] | comps[1] << 2 | comps[2] << 4 | (uint32_t)comps[3] << 6;
        return first;
    }
    Reg t = scratch(e);
    for (uint32_t i = 0; i < n; i++) {
        Reg d = t;
        d.comp = (uint8_t)i;
        op1(e, D9_MOV, d, src_reg(e, &srcs[i]));
    }
    *swizzle = 0xE4;
    if (n < 4 && !e->exact)
        *swizzle = n == 1 ? 0 : n == 2 ? 0x54 : 0xA4;
    return t;
}

static void compare(S1 *e, uint16_t op, Reg d, Reg a, Reg b)
{
    Reg zero = constf(e, 0), one = constf(e, 1);
    if (!e->ps) {
        if (op == OP_LT || op == OP_GE) {
            op2(e, op == OP_LT ? D9_SLT : D9_SGE, d, a, b);
            return;
        }
        Reg t = scratch(e);
        op2(e, D9_ADD, t, a, neg(b));
        op2(e, D9_MUL, t, t, t);
        op2(e, op == OP_EQ ? D9_SGE : D9_SLT, d, neg(t), zero);
        return;
    }
    Reg t = scratch(e);
    op2(e, D9_ADD, t, a, neg(b));
    switch (op) {
    case OP_LT: op3(e, D9_CMP, d, t, zero, one); break;
    case OP_GE: op3(e, D9_CMP, d, t, one, zero); break;
    default: {
        Reg m = t;
        m.mod = MODIFIER_ABS | MODIFIER_NEG;
        m = plain_abs(e, m);
        op3(e, D9_CMP, d, m, op == OP_EQ ? one : zero, op == OP_EQ ? zero : one);
        break;
    }
    }
}

static void floor_into(S1 *e, Reg d, Reg a)
{
    Reg t = scratch(e);
    op1(e, D9_FRC, t, a);
    op2(e, D9_ADD, d, a, neg(t));
}

static void emit(S1 *e, const IrIns *i)
{
    e->scratch = 0;
    uint16_t op = i->op;
    if (op == IR_NOP)
        return;
    if (op == IR_STORE) {
        const IrDst *d = &i->store;
        if (d->kind != DST_OUT || d->v >= 64) {
            fail(e, "this store");
            return;
        }
        Reg out = {e->out_type[d->v], e->out_num[d->v], d->comp, 0};
        if (out.type == RT_COLOROUT || out.type == RT_DEPTHOUT) {
            /* pixel outputs take whole movs: the components gather in a temp of their own. */
            out = (Reg){RT_TEMP, e->shadow[d->v], d->comp, 0};
        }
        op1(e, D9_MOV, out, src_reg(e, &i->src[0]));
        return;
    }
    Reg zero = constf(e, 0);
    switch (op) {
    case OP_IF:
        if (!e->sm3)
            { fail(e, "flow control"); return; }
        ins(e, D9_IFC | (i->flags & IRF_NZ ? CMP_NE : CMP_EQ) << 16, 2);
        src_token(e, src_reg(e, &i->src[0]), replicate(src_reg(e, &i->src[0]).comp));
        src_token(e, zero, replicate(zero.comp));
        return;
    case OP_ELSE: ins(e, D9_ELSE, 0); return;
    case OP_ENDIF: ins(e, D9_ENDIF, 0); return;
    case OP_LOOP:
        if (!e->sm3)
            { fail(e, "flow control"); return; }
        ins(e, D9_REP, 1);
        src_token(e, (Reg){RT_CONSTINT, 0, 0, 0}, 0);
        return;
    case OP_ENDLOOP: ins(e, D9_ENDREP, 0); return;
    case OP_BREAK: ins(e, D9_BREAK, 0); return;
    case OP_BREAKC: {
        Reg c = src_reg(e, &i->src[0]);
        ins(e, D9_BREAKC | (i->flags & IRF_NZ ? CMP_NE : CMP_EQ) << 16, 2);
        src_token(e, c, replicate(c.comp));
        src_token(e, zero, replicate(zero.comp));
        return;
    }
    case OP_RET:
        for (uint32_t r = 0; r < 64; r++)
            if (e->shadowed & (1ull << r)) {
                ins(e, D9_MOV, 2);
                dst_token(e, (Reg){e->out_type[r], e->out_num[r], 0, 0}, e->out_type[r] == RT_DEPTHOUT ? 1 : 0xF, false);
                src_token(e, (Reg){RT_TEMP, e->shadow[r], 0, 0}, 0xE4);
            }
        return;
    case OP_DISCARD: {
        if (!e->ps)
            { fail(e, "discard"); return; }
        Reg c = src_reg(e, &i->src[0]);
        Reg t = scratch(e);
        if (c.type == RT_CONST && i->src[0].kind == SRC_IMM)
            c = constf(e, 1);
        ins(e, D9_MOV, 2);
        dst_token(e, t, 0xF, false);
        src_token(e, neg(c), replicate(c.comp));
        ins(e, D9_TEXKILL, 1);
        dst_token(e, t, 0xF, false);
        return;
    }
    case OP_SAMPLE: {
        if (!e->ps && !(e->sm3 && i->extra == 2))
            { fail(e, "sampling in vertex shaders"); return; }
        uint32_t n0 = i->groups[0], swz;
        e->exact = !e->sm3;
        Reg coord = gather(e, i->src, n0, &swz);
        e->exact = false;
        Reg t = scratch(e);
        const IrResource *r = &e->s->resources[i->res];
        Reg sampler = {RT_SAMPLER, r->slot, 0, 0};
        uint32_t opcode = D9_TEX;
        uint32_t length = 3;
        if (i->extra == 1)
            opcode |= 0x20000;
        else if (i->extra == 3)
            opcode |= 0x10000;
        else if (i->extra == 2)
            opcode = D9_TEXLDL;
        else if (i->extra == 4)
            opcode = D9_TEXLDD, length = 5;
        if ((opcode == D9_TEXLDL || opcode == D9_TEXLDD) && !e->sm3)
            { fail(e, "this sample"); return; }
        uint32_t gx = 0, gy = 0;
        Reg dx = {}, dy = {};
        if (i->extra == 4) {
            dx = gather(e, i->src + n0, i->groups[1], &gx);
            dy = gather(e, i->src + n0 + i->groups[1], i->groups[2], &gy);
        }
        ins(e, opcode, length);
        dst_token(e, t, 0xF, false);
        src_token(e, coord, swz);
        src_token(e, sampler, 0xE4);
        if (i->extra == 4) {
            src_token(e, dx, gx);
            src_token(e, dy, gy);
        }
        for (uint32_t k = 0; k < i->dst_count; k++) {
            if (e->slots[i->dst[k]] < 0)
                continue;
            Reg s = t;
            s.comp = (uint8_t)k;
            op1(e, D9_MOV, dst_reg(e, i, k), s);
        }
        return;
    }
    case OP_SINCOS: {
        /* the angle into [-pi, pi] first, as both models take no other. */
        Reg a = src_reg(e, &i->src[0]);
        Reg t = scratch(e), r = scratch(e);
        op3(e, D9_MAD, t, a, constf(e, 0.159154943f), constf(e, 0.5f));
        op1(e, D9_FRC, t, t);
        op3(e, D9_MAD, t, t, constf(e, 6.28318548f), constf(e, -3.14159274f));
        ins(e, D9_SINCOS, e->sm3 ? 2 : 4);
        dst_token(e, r, 0x3, false);
        src_token(e, t, replicate(t.comp));
        if (!e->sm3) {
            /* the two constant registers sincos of shader model 2 reads. */
            Reg c1, c2;
            uint32_t at = e->def_count;
            while (at % 4) {
                HL_PUSH(e->h, e->defs, e->def_count, e->def_capacity, 0);
                at++;
            }
            static const float k[8] = {-1.5500992e-006f, -2.1701389e-005f, 0.0026041667f, 0.00026041668f,
                                       -0.020833334f, -0.12500000f, 1.0f, 0.50000000f};
            for (int j = 0; j < 8; j++) {
                union { float f; uint32_t u; } c = {.f = k[j]};
                HL_PUSH(e->h, e->defs, e->def_count, e->def_capacity, c.u);
            }
            c1 = (Reg){RT_CONST, e->const_base + at / 4, 0, 0};
            c2 = (Reg){RT_CONST, e->const_base + at / 4 + 1, 0, 0};
            src_token(e, c1, 0xE4);
            src_token(e, c2, 0xE4);
        }
        Reg sn = r, cs = r;
        sn.comp = 1;
        cs.comp = 0;
        if (i->dst[0] && e->slots[i->dst[0]] >= 0)
            op1(e, D9_MOV, dst_reg(e, i, 0), sn);
        if (i->dst[1] && e->slots[i->dst[1]] >= 0)
            op1(e, D9_MOV, dst_reg(e, i, 1), cs);
        return;
    }
    case OP_DP2: case OP_DP3: case OP_DP4: {
        uint32_t n = op == OP_DP2 ? 2 : op == OP_DP3 ? 3 : 4;
        Reg d = dst_reg(e, i, 0);
        if (n == 2) {
            Reg t = scratch(e);
            op2(e, D9_MUL, t, src_reg(e, &i->src[0]), src_reg(e, &i->src[2]));
            op3(e, D9_MAD, d, src_reg(e, &i->src[1]), src_reg(e, &i->src[3]), t);
            return;
        }
        uint32_t sa, sb;
        Reg a = gather(e, i->src, n, &sa), b = gather(e, i->src + n, n, &sb);
        ins(e, n == 3 ? D9_DP3 : D9_DP4, 3);
        dst_token(e, d, 1u << d.comp, false);
        src_token(e, a, sa);
        src_token(e, b, sb);
        return;
    }
    default:
        break;
    }
    if (i->dst_count != 1) {
        fail(e, ir_op_name(op));
        return;
    }
    Reg d = dst_reg(e, i, 0);
    Reg a = i->src_count > 0 ? src_reg(e, &i->src[0]) : zero;
    Reg b = i->src_count > 1 ? src_reg(e, &i->src[1]) : zero;
    Reg c = i->src_count > 2 ? src_reg(e, &i->src[2]) : zero;
    bool sat = i->flags & IRF_SAT;
    if (sat && !e->ps && !e->sm3) {
        /* vertex shaders of model 2 have no saturation. */
        Reg t = scratch(e);
        op2(e, D9_MAX, t, a, zero);
        op2(e, D9_MIN, d, t, constf(e, 1));
        return;
    }
    switch (op) {
    case OP_MOV:
        if (!sat && !a.mod && a.type == d.type && a.num == d.num && a.comp == d.comp)
            return;
        op_n(e, D9_MOV, d, sat, &a, 1);
        return;
    case OP_ADD: { Reg s[2] = {a, b}; op_n(e, D9_ADD, d, sat, s, 2); return; }
    case OP_MUL: { Reg s[2] = {a, b}; op_n(e, D9_MUL, d, sat, s, 2); return; }
    case OP_MAD: { Reg s[3] = {a, b, c}; op_n(e, D9_MAD, d, sat, s, 3); return; }
    case OP_MIN: op2(e, D9_MIN, d, a, b); return;
    case OP_MAX: op2(e, D9_MAX, d, a, b); return;
    case OP_FRC: op1(e, D9_FRC, d, a); return;
    case OP_RCP: op1(e, D9_RCP, d, a); return;
    case OP_RSQ: op1(e, D9_RSQ, d, a); return;
    case OP_EXP: op1(e, D9_EXP, d, a); return;
    case OP_LOG: op1(e, D9_LOG, d, a); return;
    case OP_DIV: {
        Reg t = scratch(e);
        op1(e, D9_RCP, t, b);
        op2(e, D9_MUL, d, a, t);
        return;
    }
    case OP_SQRT: {
        Reg t = scratch(e);
        op1(e, D9_RSQ, t, a);
        op1(e, D9_RCP, d, t);
        return;
    }
    case OP_ROUND_NI: floor_into(e, d, a); return;
    case OP_ROUND_PI: {
        Reg t = scratch(e);
        op1(e, D9_FRC, t, neg(a));
        op2(e, D9_ADD, d, a, t);
        return;
    }
    case OP_ROUND_NE: {
        Reg t = scratch(e);
        op2(e, D9_ADD, t, a, constf(e, 0.5f));
        floor_into(e, d, t);
        return;
    }
    case OP_ROUND_Z: {
        Reg fl = scratch(e), ce = scratch(e), t = scratch(e);
        floor_into(e, fl, a);
        op1(e, D9_FRC, t, neg(a));
        op2(e, D9_ADD, ce, a, t);
        if (e->ps) {
            op3(e, D9_CMP, d, a, fl, ce);
        } else {
            op2(e, D9_SGE, t, a, zero);
            op3(e, D9_LRP, d, t, fl, ce);
        }
        return;
    }
    case OP_LT: case OP_GE: case OP_EQ: case OP_NE: compare(e, op, d, a, b); return;
    case OP_MOVC: op3(e, D9_LRP, d, a, b, c); return;
    case OP_DERIV_RTX: case OP_DERIV_RTX_COARSE: case OP_DERIV_RTX_FINE:
    case OP_DERIV_RTY: case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE:
        if (!e->ps || !e->sm3)
            { fail(e, "derivatives"); return; }
        op1(e, op == OP_DERIV_RTX || op == OP_DERIV_RTX_COARSE || op == OP_DERIV_RTX_FINE ? D9_DSX : D9_DSY, d, a);
        return;
    default:
        fail(e, ir_op_name(op));
        return;
    }
}

/* ------------------------------------------------------------------ registers of inputs and outputs */

static uint32_t usage_of(const char *name)
{
    static const char *const usages[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD",
                                         "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH",
                                         "SAMPLE"};
    if (!hl_stricmp(name, "SV_Position"))
        return 0;
    if (!hl_stricmp(name, "SV_Target"))
        return 10;
    if (!hl_stricmp(name, "SV_Depth"))
        return 12;
    for (uint32_t i = 0; i < HL_ARRAY_SIZE(usages); i++)
        if (!hl_stricmp(name, usages[i]))
            return i;
    return 5;
}

static void map_io(S1 *e)
{
    IrShader *s = e->s;
    uint32_t next = 0;
    for (uint32_t i = 0; i < s->inputs.count; i++) {
        const IrElement *el = &s->inputs.elements[i];
        if (el->reg >= 64)
            continue;
        uint32_t u = usage_of(el->name);
        if (!e->ps) {
            e->in_type[el->reg] = 0xFF;
            e->in_num[el->reg] = el->reg;
        } else if (e->sm3) {
            if (u == 0 || !hl_stricmp(el->name, "VPOS")) {
                e->in_type[el->reg] = RT_MISCTYPE, e->in_num[el->reg] = 0;
            } else if (!hl_stricmp(el->name, "VFACE") || !hl_stricmp(el->name, "SV_IsFrontFace")) {
                e->in_type[el->reg] = RT_MISCTYPE, e->in_num[el->reg] = 1;
            } else {
                e->in_type[el->reg] = 0xFF, e->in_num[el->reg] = next++;
            }
        } else if (u == 5) {
            e->in_type[el->reg] = RT_TEXTURE, e->in_num[el->reg] = el->index;
        } else if (u == 10) {
            e->in_type[el->reg] = 0xFF, e->in_num[el->reg] = el->index;
        } else {
            fail(e, "this input semantic");
        }
    }
    next = 0;
    for (uint32_t i = 0; i < s->outputs.count; i++) {
        const IrElement *el = &s->outputs.elements[i];
        uint32_t u = usage_of(el->name);
        uint32_t r = el->reg < 64 ? el->reg : 63 - i;
        if (el->operand) {
            /* oDepth, which signatures keep without a register. */
            for (r = 63; r > 0 && e->out_type[r]; r--)
                ;
            ((IrElement *)el)->reg = r;
        }
        if (e->ps) {
            e->out_type[r] = u == 12 ? RT_DEPTHOUT : RT_COLOROUT;
            e->out_num[r] = u == 12 ? 0 : el->index;
        } else if (e->sm3) {
            e->out_type[r] = RT_OUTPUT, e->out_num[r] = next++;
        } else if (u == 0) {
            e->out_type[r] = RT_RASTOUT, e->out_num[r] = 0;
        } else if (u == 11) {
            e->out_type[r] = RT_RASTOUT, e->out_num[r] = 1;
        } else if (u == 4) {
            e->out_type[r] = RT_RASTOUT, e->out_num[r] = 2;
        } else if (u == 10) {
            e->out_type[r] = RT_ATTROUT, e->out_num[r] = el->index;
        } else {
            e->out_type[r] = RT_OUTPUT, e->out_num[r] = el->index;
        }
    }
}

/* ------------------------------------------------------------------ the constant table */

typedef struct Ctab {
    Hlsl *h;
    HlBuf b;
    uint32_t *string_at;
    const char **strings;
    uint32_t string_count, string_capacity, at_capacity;
} Ctab;

static uint32_t c32(Ctab *c, uint32_t v)
{
    uint32_t at = (uint32_t)c->b.length;
    hl_buf_append(c->h, &c->b, (const char *)&v, 4);
    return at;
}

static void c16(Ctab *c, uint16_t v) { hl_buf_append(c->h, &c->b, (const char *)&v, 2); }

static void cstring(Ctab *c, uint32_t at, const char *s)
{
    HL_PUSH(c->h, c->strings, c->string_count, c->string_capacity, s);
    c->string_count--;
    HL_PUSH(c->h, c->string_at, c->string_count, c->at_capacity, at);
}

static void set_at(Ctab *c, uint32_t at, uint32_t v) { __builtin_memcpy(c->b.data + at, &v, 4); }

static void write_ctab(S1 *e, uint32_t version)
{
    IrShader *s = e->s;
    Ctab c = {.h = e->h};
    HlBuffer *globals = s->prog->globals_buffer;
    uint32_t constants = 0;
    for (uint32_t i = 0; globals && i < globals->var_count; i++)
        constants++;
    for (uint32_t i = 0; i < s->resource_count; i++)
        if (s->resources[i].used && s->resources[i].kind == SRC_SAMPLER)
            constants++;
    c32(&c, 28);
    uint32_t creator = c32(&c, 0);
    c32(&c, version);
    c32(&c, constants);
    uint32_t info = c32(&c, 0);
    c32(&c, 0);
    uint32_t target = c32(&c, 0);
    set_at(&c, info, (uint32_t)c.b.length);
    uint32_t *types = hl_alloc(e->h, (constants + 1) * sizeof(uint32_t));
    const HlType **type_of = hl_alloc(e->h, (constants + 1) * sizeof(HlType *));
    uint32_t k = 0;
    for (uint32_t i = 0; globals && i < globals->var_count; i++) {
        HlVar *v = globals->vars[i];
        cstring(&c, c32(&c, 0), v->name);
        c16(&c, 2);
        c16(&c, (uint16_t)v->packoffset / 4);
        c16(&c, (uint16_t)lw_legacy_regs(v->type, s->row_major_default));
        c16(&c, 0);
        type_of[k] = v->type;
        types[k++] = c32(&c, 0);
        c32(&c, 0);
    }
    for (uint32_t i = 0; i < s->resource_count; i++) {
        const IrResource *r = &s->resources[i];
        if (!r->used || r->kind != SRC_SAMPLER)
            continue;
        cstring(&c, c32(&c, 0), r->var->name);
        c16(&c, 3);
        c16(&c, (uint16_t)r->slot);
        c16(&c, 1);
        c16(&c, 0);
        type_of[k] = r->var->type;
        types[k++] = c32(&c, 0);
        c32(&c, 0);
    }
    for (uint32_t i = 0; i < k; i++) {
        set_at(&c, types[i], (uint32_t)c.b.length);
        const HlType *t = type_of[i];
        uint16_t elements = 1;
        while (t->kind == TY_ARRAY) {
            elements = (uint16_t)(elements * t->count);
            t = t->elem;
        }
        uint16_t cls = 0, type = 3, rows = 1, cols = 1;
        if (t->kind == TY_VECTOR)
            cls = 1, cols = t->cols;
        else if (t->kind == TY_MATRIX)
            cls = hl_type_row_major(t, s->row_major_default) ? 2 : 3, rows = t->rows, cols = t->cols;
        else if (t->kind == TY_STRUCT)
            cls = 5;
        else if (t->kind == TY_OBJECT) {
            cls = 4;
            type = t->object == OBJ_SAMPLER1D ? 11 : t->object == OBJ_SAMPLER3D ? 13 : t->object == OBJ_SAMPLERCUBE ? 14 : 12;
        }
        if (t->kind != TY_OBJECT && t->kind != TY_STRUCT)
            type = t->base == BT_BOOL ? 1 : hl_base_int(t->base) ? 2 : 3;
        c16(&c, cls);
        c16(&c, type);
        c16(&c, rows);
        c16(&c, cols);
        c16(&c, elements);
        c16(&c, 0);
        c32(&c, 0);
    }
    cstring(&c, creator, "Microsoft (R) HLSL Shader Compiler 10.1");
    cstring(&c, target, s->profile.name);
    for (uint32_t i = 0; i < c.string_count; i++) {
        set_at(&c, c.string_at[i], (uint32_t)c.b.length);
        hl_buf_append(e->h, &c.b, c.strings[i], __builtin_strlen(c.strings[i]) + 1);
    }
    while (c.b.length & 3)
        hl_buf_putc(e->h, &c.b, 0);
    uint32_t dwords = (uint32_t)c.b.length / 4 + 1;
    w(e, D9_COMMENT | dwords << 16);
    w(e, DXBC_FOURCC('C', 'T', 'A', 'B'));
    for (uint32_t i = 0; i + 1 < dwords; i++) {
        uint32_t v;
        __builtin_memcpy(&v, c.b.data + i * 4, 4);
        w(e, v);
    }
}

/* ------------------------------------------------------------------ the program */

int ir_emit_sm1(IrShader *s, HlBuf *out)
{
    Hlsl *h = s->h;
    ir_bind(s);
    S1 e_ = {.s = s, .h = h}, *e = &e_;
    e->ps = s->profile.stage == STAGE_PIXEL;
    e->sm3 = s->profile.major >= 3;
    e->slots = ir_allocate(s, &e->temps);
    for (uint32_t i = 0; i < s->cbuffer_count; i++)
        if (s->cbuffers[i].buffer->size / 16 > e->const_base)
            e->const_base = s->cbuffers[i].buffer->size / 16;
    if (s->indexable_count || s->icb_count || s->tgsm_count)
        fail(e, "indexed arrays");
    map_io(e);
    /* pixel outputs gather in temps after the allocated ones; scratch temps come after those. */
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *x = &s->ins[i];
        if (e->ps && x->op == IR_STORE && x->store.kind == DST_OUT && x->store.v < 64 &&
            !(e->shadowed & (1ull << x->store.v))) {
            e->shadow[x->store.v] = e->temps++;
            e->shadowed |= 1ull << x->store.v;
        }
    }
    /* the body first: it makes the def constants and scratch temps the head declares. */
    uint32_t *head_words;
    for (uint32_t i = 0; i < s->count; i++)
        emit(e, &s->ins[i]);
    w(e, D9_END);
    if (e->failed || h->errors)
        return -1;
    uint32_t temps = e->temps + e->scratch_max;
    uint32_t max_temps = e->sm3 ? 32 : 12;
    if (temps > max_temps) {
        hl_error(h, (HlLoc){0, 1, 1}, 4005, "the shader needs %u temporary registers, %s has %u", temps,
                 s->profile.name, max_temps);
        return -1;
    }
    uint32_t max_consts = e->ps ? (e->sm3 ? 224 : 32) : 256;
    if (e->const_base + (e->def_count + 3) / 4 > max_consts) {
        hl_error(h, (HlLoc){0, 1, 1}, 4013, "the shader needs too many constant registers for %s", s->profile.name);
        return -1;
    }
    uint32_t *body = e->words, body_count = e->count;
    e->words = nullptr;
    e->count = e->capacity = 0;
    uint32_t version = (e->ps ? 0xFFFF0000u : 0xFFFE0000u) | (uint32_t)s->profile.major << 8 | s->profile.minor;
    w(e, version);
    write_ctab(e, version);
    for (uint32_t i = 0; i < e->def_count; i += 4) {
        ins(e, D9_DEF, 5);
        w(e, param(RT_CONST, e->const_base + i / 4) | 0xF << 16);
        for (uint32_t k = 0; k < 4; k++)
            w(e, i + k < e->def_count ? e->defs[i + k] : 0);
    }
    bool loops = false;
    for (uint32_t i = 0; i < s->count; i++)
        loops |= s->ins[i].op == OP_LOOP;
    if (loops) {
        ins(e, D9_DEFI, 5);
        w(e, param(RT_CONSTINT, 0) | 0xF << 16);
        w(e, 255);
        w(e, 0);
        w(e, 1);
        w(e, 0);
    }
    /* inputs, outputs and samplers. */
    for (uint32_t i = 0; i < s->inputs.count; i++) {
        const IrElement *el = &s->inputs.elements[i];
        if (el->reg >= 64)
            continue;
        uint8_t type = e->in_type[el->reg] == 0xFF ? RT_INPUT : e->in_type[el->reg];
        if (e->ps && !e->sm3 && type != RT_TEXTURE && type != RT_INPUT)
            continue;
        ins(e, D9_DCL, 2);
        uint32_t usage = usage_of(el->name);
        if (type == RT_MISCTYPE)
            usage = 0;
        w(e, 0x80000000u | (e->ps && !e->sm3 ? 0 : usage | el->index << 16));
        w(e, param(type, e->in_num[el->reg]) | (uint32_t)(type == RT_MISCTYPE ? (e->in_num[el->reg] ? 1 : 3) : el->mask) << 16);
    }
    if (!e->ps && e->sm3)
        for (uint32_t i = 0; i < s->outputs.count; i++) {
            const IrElement *el = &s->outputs.elements[i];
            if (el->reg >= 64)
                continue;
            ins(e, D9_DCL, 2);
            w(e, 0x80000000u | usage_of(el->name) | el->index << 16);
            w(e, param(RT_OUTPUT, e->out_num[el->reg]) | (uint32_t)el->mask << 16);
        }
    for (uint32_t i = 0; i < s->resource_count; i++) {
        const IrResource *r = &s->resources[i];
        if (!r->used || r->kind != SRC_SAMPLER)
            continue;
        uint32_t texture = r->object == OBJ_SAMPLERCUBE ? 3 : r->object == OBJ_SAMPLER3D ? 4 : 2;
        ins(e, D9_DCL, 2);
        w(e, 0x80000000u | texture << 27);
        w(e, param(RT_SAMPLER, r->slot) | 0xF << 16);
    }
    head_words = e->words;
    hl_buf_append(h, out, (const char *)head_words, e->count * 4);
    hl_buf_append(h, out, (const char *)body, body_count * 4);
    return 0;
}
