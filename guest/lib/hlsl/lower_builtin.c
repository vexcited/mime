#include "lower.h"

uint32_t lw_tight_size(const HlType *t, bool row_major_default);
void lw_tight_offsets(const HlType *t, uint32_t base, bool row_major_default, uint32_t *out, uint32_t *n);
IrSrc lw_binary_scalar(Lower *l, uint8_t op, uint8_t storage, IrSrc a, IrSrc b);
Value lw_call_function(Lower *l, HlFunc *f, HlExpr **args, uint32_t count, HlLoc loc, bool entry);

static uint8_t storage_of(const HlType *t) { return hl_base_storage(t->base); }

static Value arg(Lower *l, HlExpr *e, uint32_t i) { return lw_expr(l, e->args[i]); }

static IrSrc comp(const Value *v, uint32_t i) { return v->c[v->count == 1 ? 0 : i]; }

static Value result(Lower *l, HlExpr *e) { return lw_value(l, e->type, hl_type_components(e->type)); }

/* ------------------------------------------------------------------ math helpers */

static IrSrc fmul(Lower *l, IrSrc a, IrSrc b) { return lw_op2(l, OP_MUL, a, b); }
static IrSrc fadd(Lower *l, IrSrc a, IrSrc b) { return lw_op2(l, OP_ADD, a, b); }
static IrSrc fsub(Lower *l, IrSrc a, IrSrc b) { return lw_op2(l, OP_ADD, a, lw_neg(l, b, BT_FLOAT)); }
static IrSrc fmad(Lower *l, IrSrc a, IrSrc b, IrSrc c) { return lw_op3(l, OP_MAD, a, b, c); }
static IrSrc fimm(float f) { return ir_immf(f); }

static IrSrc sincos(Lower *l, IrSrc x, bool cosine)
{
    IrIns *ins = ir_emit(l->s, OP_SINCOS, 2, 1);
    ins->src[0] = x;
    return ir_reg(ins->dst[cosine ? 1 : 0]);
}

static IrSrc fexp2(Lower *l, IrSrc x) { return lw_op1(l, OP_EXP, x); }
static IrSrc flog2(Lower *l, IrSrc x) { return lw_op1(l, OP_LOG, x); }

static IrSrc frcp(Lower *l, IrSrc x)
{
    if (l->s->profile.major >= 5)
        return lw_op1(l, OP_RCP, x);
    return lw_op2(l, OP_DIV, fimm(1.0f), x);
}

/* acos as fxc approximates it, with a cubic of |x| and a square root. */
static IrSrc facos(Lower *l, IrSrc x)
{
    IrSrc ax = lw_abs(l, x, BT_FLOAT);
    IrSrc p = fmad(l, ax, fimm(-0.0187293f), fimm(0.0742610f));
    p = fmad(l, p, ax, fimm(-0.2121144f));
    p = fmad(l, p, ax, fimm(1.5707288f));
    IrSrc r = lw_op1(l, OP_SQRT, fadd(l, lw_neg(l, ax, BT_FLOAT), fimm(1.0f)));
    IrSrc pr = fmul(l, p, r);
    IrSrc neg = lw_op2(l, OP_LT, x, lw_neg(l, x, BT_FLOAT));
    IrSrc fix = lw_op2(l, OP_AND, neg, fmad(l, pr, fimm(-2.0f), fimm(3.14159265f)));
    return fadd(l, pr, fix);
}

static IrSrc fasin(Lower *l, IrSrc x)
{
    IrSrc ax = lw_abs(l, x, BT_FLOAT);
    IrSrc p = fmad(l, ax, fimm(-0.0187293f), fimm(0.0742610f));
    p = fmad(l, p, ax, fimm(-0.2121144f));
    p = fmad(l, p, ax, fimm(1.5707288f));
    IrSrc r = lw_op1(l, OP_SQRT, fadd(l, lw_neg(l, ax, BT_FLOAT), fimm(1.0f)));
    IrSrc a = fmad(l, lw_neg(l, p, BT_FLOAT), r, fimm(1.57079632f));
    IrSrc neg = lw_op2(l, OP_LT, x, lw_neg(l, x, BT_FLOAT));
    return lw_movc(l, neg, lw_neg(l, a, BT_FLOAT), a);
}

/* atan of y / x over all quadrants; x is 1 for atan. */
static IrSrc fatan2(Lower *l, IrSrc y, IrSrc x, bool two)
{
    IrSrc ax = lw_abs(l, x, BT_FLOAT), ay = lw_abs(l, y, BT_FLOAT);
    IrSrc mx = lw_op2(l, OP_MAX, ax, ay), mn = lw_op2(l, OP_MIN, ax, ay);
    IrSrc t = fmul(l, frcp(l, mx), mn);
    IrSrc s = fmul(l, t, t);
    IrSrc p = fmad(l, s, fimm(0.0208350997f), fimm(-0.0851330012f));
    p = fmad(l, s, p, fimm(0.180141002f));
    p = fmad(l, s, p, fimm(-0.330299497f));
    p = fmad(l, s, p, fimm(0.999866009f));
    IrSrc r = fmul(l, p, t);
    /* the octant: swap past 45 degrees, mirror for negative x, then the sign of y. */
    IrSrc swap = lw_op2(l, OP_LT, ax, ay);
    r = lw_movc(l, swap, fadd(l, lw_neg(l, r, BT_FLOAT), fimm(1.57079637f)), r);
    if (two) {
        IrSrc negx = lw_op2(l, OP_LT, x, fimm(0.0f));
        r = lw_movc(l, negx, fadd(l, lw_neg(l, r, BT_FLOAT), fimm(3.14159274f)), r);
        IrSrc negy = lw_op2(l, OP_LT, y, fimm(0.0f));
        r = lw_movc(l, negy, lw_neg(l, r, BT_FLOAT), r);
        /* atan2(0, 0) is 0. */
        IrSrc zero = lw_op2(l, OP_EQ, mx, fimm(0.0f));
        return lw_movc(l, zero, fimm(0.0f), r);
    }
    IrSrc negy = lw_op2(l, OP_LT, y, fimm(0.0f));
    return lw_movc(l, negy, lw_neg(l, r, BT_FLOAT), r);
}

static IrSrc fexp(Lower *l, IrSrc x) { return fexp2(l, fmul(l, x, fimm(1.44269502f))); }

static IrSrc dot(Lower *l, const Value *a, const Value *b, uint32_t n, uint8_t storage)
{
    if (storage == BT_FLOAT && n >= 2 && n <= 4) {
        IrIns *ins = ir_emit(l->s, n == 2 ? OP_DP2 : n == 3 ? OP_DP3 : OP_DP4, 1, 2 * n);
        for (uint32_t i = 0; i < n; i++) {
            ins->src[i] = comp(a, i);
            ins->src[n + i] = comp(b, i);
        }
        ins->groups[0] = (uint8_t)n;
        ins->groups[1] = (uint8_t)n;
        ins->group_count = 2;
        bool all_imm = true;
        for (uint32_t i = 0; i < 2 * n; i++)
            all_imm &= ins->src[i].kind == SRC_IMM;
        if (all_imm) {
            l->s->count--;
            IrSrc acc = fmul(l, comp(a, 0), comp(b, 0));
            for (uint32_t i = 1; i < n; i++)
                acc = fmad(l, comp(a, i), comp(b, i), acc);
            return acc;
        }
        return ir_reg(ins->dst[0]);
    }
    if (storage == BT_FLOAT || storage == BT_DOUBLE) {
        IrSrc acc = fmul(l, comp(a, 0), comp(b, 0));
        for (uint32_t i = 1; i < n; i++)
            acc = fmad(l, comp(a, i), comp(b, i), acc);
        return acc;
    }
    IrSrc acc = lw_op2(l, OP_IMUL, comp(a, 0), comp(b, 0));
    for (uint32_t i = 1; i < n; i++)
        acc = lw_op3(l, OP_IMAD, comp(a, i), comp(b, i), acc);
    return acc;
}

static IrSrc fsaturate(Lower *l, IrSrc x)
{
    if (x.kind == SRC_IMM) {
        float f = (x.mod ? 0 : 0);
        union { uint32_t u; float f; } c = {.u = x.v};
        f = c.f;
        if (x.mod & MODIFIER_ABS)
            f = __builtin_fabsf(f);
        if (x.mod & MODIFIER_NEG)
            f = -f;
        f = f > 1.0f ? 1.0f : f > 0.0f ? f : 0.0f;
        return fimm(f);
    }
    IrIns *ins = ir_emit(l->s, OP_MOV, 1, 1);
    ins->src[0] = x;
    ins->flags |= IRF_SAT;
    return ir_reg(ins->dst[0]);
}

static IrSrc firstbit_from_low(Lower *l, uint16_t op, IrSrc x)
{
    IrSrc r = lw_op1(l, op, x);
    IrSrc pos = lw_op2(l, OP_IADD, lw_op1(l, OP_INEG, r), ir_imm(31));
    IrSrc none = lw_op2(l, OP_IEQ, r, ir_imm(~0u));
    return lw_movc(l, none, ir_imm(~0u), pos);
}

static IrSrc determinant(Lower *l, const Value *m, uint32_t n, uint32_t stride, const uint32_t *rows,
                         const uint32_t *cols)
{
    if (n == 1)
        return m->c[rows[0] * stride + cols[0]];
    if (n == 2) {
        IrSrc a = m->c[rows[0] * stride + cols[0]], b = m->c[rows[0] * stride + cols[1]];
        IrSrc c = m->c[rows[1] * stride + cols[0]], d = m->c[rows[1] * stride + cols[1]];
        return fsub(l, fmul(l, a, d), fmul(l, b, c));
    }
    IrSrc acc = {};
    for (uint32_t j = 0; j < n; j++) {
        uint32_t sub_cols[4], k = 0;
        for (uint32_t c = 0; c < n; c++)
            if (c != j)
                sub_cols[k++] = cols[c];
        IrSrc minor = determinant(l, m, n - 1, stride, rows + 1, sub_cols);
        IrSrc term = fmul(l, m->c[rows[0] * stride + cols[j]], minor);
        if (!j)
            acc = term;
        else if (j & 1)
            acc = fsub(l, acc, term);
        else
            acc = fadd(l, acc, term);
    }
    return acc;
}

static IrSrc min_max(Lower *l, bool max, uint8_t storage, IrSrc a, IrSrc b)
{
    if (storage == BT_FLOAT || storage == BT_DOUBLE)
        return lw_op2(l, max ? OP_MAX : OP_MIN, a, b);
    if (storage == BT_UINT)
        return lw_op2(l, max ? OP_UMAX : OP_UMIN, a, b);
    return lw_op2(l, max ? OP_IMAX : OP_IMIN, a, b);
}

/* ------------------------------------------------------------------ resources */

static IrIns *resource_ins(Lower *l, uint16_t op, uint32_t dsts, uint32_t srcs, const ObjRef *o)
{
    IrIns *ins = ir_emit(l->s, op, dsts, srcs);
    uint32_t id = lw_resource(l, o);
    ins->res = id;
    ins->res_kind = l->s->resources[id].kind;
    if (o->has_dynamic)
        ins->res_rel = o->dynamic.v ? o->dynamic.v : 0;
    if (o->has_dynamic && o->dynamic.kind == SRC_IMM)
        ins->res_rel = 0;
    return ins;
}

static void set_sampler(Lower *l, IrIns *ins, HlExpr *e)
{
    ObjRef s;
    if (!lw_obj(l, e, &s))
        return;
    uint32_t id = lw_resource(l, &s);
    ins->sampler = id;
    if (s.has_dynamic && s.dynamic.kind == SRC_REG)
        ins->sampler_rel = s.dynamic.v;
}

/* immediate texel offsets, as aoffimmi takes them. */
static bool offsets(Lower *l, IrIns *ins, HlExpr *e, uint32_t dims)
{
    Value v = lw_expr(l, e);
    for (uint32_t i = 0; i < v.count && i < 3 && i < dims; i++) {
        if (v.c[i].kind != SRC_IMM) {
            lw_error(l, e->loc, 4014, "offsets must be literal expressions");
            return false;
        }
        int32_t o = (int32_t)v.c[i].v;
        if (o < -8 || o > 7)
            hl_warning(l->h, e->loc, 4121, "offsets must be in the range -8 to 7");
        ins->offset[i] = (int8_t)o;
    }
    return true;
}

static uint32_t texel_count(const HlType *t) { return t->elem ? hl_type_components(t->elem) : 4; }

static Value take(Lower *l, HlType *type, IrIns *ins, uint32_t n)
{
    Value r = lw_value(l, type, n);
    for (uint32_t i = 0; i < n; i++)
        r.c[i] = ir_reg(ins->dst[i]);
    return r;
}

/* the coordinates as a group of the instruction's sources, from start. */
static void put_group(IrIns *ins, uint32_t *at, const Value *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        ins->src[*at + i] = comp(v, i);
    *at += n;
    ins->groups[ins->group_count++] = (uint8_t)n;
}

static void put_scalar(IrIns *ins, uint32_t *at, IrSrc s)
{
    ins->src[(*at)++] = s;
    ins->groups[ins->group_count++] = 1;
}

static Value sample(Lower *l, HlExpr *e, const ObjRef *o)
{
    uint8_t object = o->type->object;
    uint32_t dims = hl_object_dims(object), coords = dims + (hl_object_is_array(object) ? 1 : 0);
    uint32_t offset_dims = (object == OBJ_TEXTURECUBE || object == OBJ_TEXTURECUBEARRAY) ? 0 : dims;
    uint32_t id = e->intrinsic;
    uint16_t op;
    uint32_t fixed;
    switch (id) {
    case M_SAMPLE: op = OP_SAMPLE; fixed = 2; break;
    case M_SAMPLE_BIAS: op = OP_SAMPLE_B; fixed = 3; break;
    case M_SAMPLE_GRAD: op = OP_SAMPLE_D; fixed = 4; break;
    case M_SAMPLE_LEVEL: op = OP_SAMPLE_L; fixed = 3; break;
    case M_SAMPLE_CMP: op = OP_SAMPLE_C; fixed = 3; break;
    default: op = OP_SAMPLE_C_LZ; fixed = 3; break;
    }
    Value coord = arg(l, e, 1);
    uint32_t srcs = coords;
    Value extra = {}, ddy = {};
    if (op == OP_SAMPLE_D) {
        extra = arg(l, e, 2);
        ddy = arg(l, e, 3);
        srcs += 2 * dims;
    } else if (fixed == 3) {
        extra = arg(l, e, 2);
        srcs += 1;
    }
    if (e->arg_count > fixed + (offset_dims ? 1 : 0)) {
        lw_error(l, e->loc, 3000, "clamps and status values of samples are not supported");
        return result(l, e);
    }
    uint32_t n = op == OP_SAMPLE_C || op == OP_SAMPLE_C_LZ ? 1 : texel_count(o->type);
    IrIns *ins = resource_ins(l, op, n, srcs, o);
    uint32_t at = 0;
    put_group(ins, &at, &coord, coords);
    if (op == OP_SAMPLE_D) {
        put_group(ins, &at, &extra, dims);
        put_group(ins, &at, &ddy, dims);
    } else if (fixed == 3) {
        put_scalar(ins, &at, comp(&extra, 0));
    }
    set_sampler(l, ins, e->args[0]);
    if (e->arg_count > fixed && offset_dims)
        offsets(l, ins, e->args[fixed], offset_dims);
    return take(l, e->type, ins, n);
}

static Value gather(Lower *l, HlExpr *e, const ObjRef *o)
{
    uint8_t object = o->type->object;
    uint32_t dims = hl_object_dims(object), coords = dims + (hl_object_is_array(object) ? 1 : 0);
    uint32_t id = e->intrinsic;
    bool cmp = id >= M_GATHER_CMP;
    uint32_t channel = cmp ? id - M_GATHER_CMP : id - M_GATHER;
    if (channel)
        channel--;
    uint32_t fixed = cmp ? 3 : 2;
    uint32_t extra = e->arg_count - fixed;
    if (extra >= 4) {
        lw_error(l, e->loc, 3000, "gathers with four offsets are not supported");
        return result(l, e);
    }
    Value coord = arg(l, e, 1);
    Value offset = {};
    bool programmable = false;
    if (extra >= 1) {
        offset = arg(l, e, fixed);
        for (uint32_t i = 0; i < offset.count; i++)
            if (offset.c[i].kind != SRC_IMM)
                programmable = true;
    }
    uint16_t op = cmp ? (programmable ? OP_GATHER4_PO_C : OP_GATHER4_C) : (programmable ? OP_GATHER4_PO : OP_GATHER4);
    if ((channel || programmable || cmp) && !lw_sm5(l)) {
        lw_error(l, e->loc, 3000, "this gather needs shader model 5");
        return result(l, e);
    }
    uint32_t srcs = coords + (programmable ? 2 : 0) + (cmp ? 1 : 0);
    IrIns *ins = resource_ins(l, op, 4, srcs, o);
    uint32_t at = 0;
    put_group(ins, &at, &coord, coords);
    if (programmable)
        put_group(ins, &at, &offset, 2);
    if (cmp) {
        Value ref = arg(l, e, 2);
        put_scalar(ins, &at, comp(&ref, 0));
    }
    set_sampler(l, ins, e->args[0]);
    ins->extra = channel;
    if (extra >= 1 && !programmable)
        offsets(l, ins, e->args[fixed], dims);
    return take(l, e->type, ins, 4);
}

static Value load_texel(Lower *l, const ObjRef *o, Value coord, IrSrc sample_index, bool ms, HlType *type,
                        HlExpr *offset_expr)
{
    uint8_t object = o->type->object;
    uint32_t n = texel_count(o->type);
    uint32_t dims = hl_object_dims(object), coords = dims + (hl_object_is_array(object) ? 1 : 0);
    if (hl_object_is_uav(object)) {
        IrIns *ins = resource_ins(l, OP_LD_UAV_TYPED, n, coords, o);
        uint32_t at = 0;
        put_group(ins, &at, &coord, coords);
        return take(l, type, ins, n);
    }
    if (object == OBJ_BUFFER) {
        IrIns *ins = resource_ins(l, OP_LD, n, 1, o);
        uint32_t at = 0;
        put_group(ins, &at, &coord, 1);
        return take(l, type, ins, n);
    }
    /* ld takes the mip level in the last component; ld_ms the sample index on its own. */
    uint32_t total = ms ? coords : coords + 1;
    IrIns *ins = resource_ins(l, ms ? OP_LD_MS : OP_LD, n, total + (ms ? 1 : 0), o);
    uint32_t at = 0;
    put_group(ins, &at, &coord, total);
    if (ms)
        put_scalar(ins, &at, sample_index);
    if (offset_expr)
        offsets(l, ins, offset_expr, dims);
    return take(l, type, ins, n);
}

/* reads n dwords of a structured or raw view: ld_structured gives up to four at once. */
static Value load_structured(Lower *l, const ObjRef *o, IrSrc index, IrSrc byte_offset, const uint32_t *offsets_,
                             uint32_t n, HlType *type, bool raw)
{
    Value r = lw_value(l, type, n);
    uint32_t i = 0;
    while (i < n) {
        uint32_t run = 1;
        while (i + run < n && run < 4 && offsets_[i + run] == offsets_[i] + run * 4)
            run++;
        IrSrc bytes = byte_offset.kind == SRC_NONE ? ir_imm(offsets_[i])
                                                   : lw_op2(l, OP_IADD, byte_offset, ir_imm(offsets_[i]));
        IrIns *ins;
        if (raw) {
            ins = resource_ins(l, OP_LD_RAW, run, 1, o);
            ins->src[0] = bytes;
        } else {
            ins = resource_ins(l, OP_LD_STRUCTURED, run, 2, o);
            ins->src[0] = index;
            ins->src[1] = bytes;
        }
        for (uint32_t k = 0; k < run; k++) {
            r.c[i + k] = ir_reg(ins->dst[k]);
            uint8_t base = type ? hl_type_component_base(type, i + k) : BT_UINT;
            if (hl_base_storage(base) == BT_BOOL)
                r.c[i + k] = lw_op2(l, OP_INE, r.c[i + k], ir_imm(0));
        }
        i += run;
    }
    return r;
}

static void store_structured(Lower *l, const ObjRef *o, IrSrc index, IrSrc byte_offset, const uint32_t *offsets_,
                             const Value *v, uint32_t n, bool raw)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t run = 1;
        while (i + run < n && run < 4 && offsets_[i + run] == offsets_[i] + run * 4)
            run++;
        IrSrc bytes = byte_offset.kind == SRC_NONE ? ir_imm(offsets_[i])
                                                   : lw_op2(l, OP_IADD, byte_offset, ir_imm(offsets_[i]));
        IrIns *ins = resource_ins(l, raw ? OP_STORE_RAW : OP_STORE_STRUCTURED, 0, (raw ? 1 : 2) + run, o);
        uint32_t at = 0;
        if (!raw)
            put_scalar(ins, &at, index);
        put_scalar(ins, &at, bytes);
        for (uint32_t k = 0; k < run; k++)
            ins->src[at + k] = comp(v, i + k);
        ins->groups[ins->group_count++] = (uint8_t)run;
        i += run;
    }
}

Value lw_object_load(Lower *l, LValue *lv)
{
    const ObjRef *o = &lv->obj;
    if (lv->kind == LV_STRUCTURED) {
        IrSrc index = comp(&lv->coord, 0);
        return load_structured(l, o, index, lv->has_byte_offset ? lv->byte_offset : (IrSrc){}, lv->comps, lv->count,
                               lv->type, false);
    }
    bool ms = lv->has_byte_offset;
    Value coord = lv->coord;
    IrSrc sample_index = {};
    uint8_t object = o->type->object;
    uint32_t dims = hl_object_dims(object), coords = dims + (hl_object_is_array(object) ? 1 : 0);
    if (ms) {
        sample_index = coord.c[coord.count - 1];
        coord.count--;
    } else if (!hl_object_is_uav(object) && object != OBJ_BUFFER && coord.count == coords) {
        /* tex[pos] reads mip 0. */
        Value c = lw_value(l, nullptr, coords + 1);
        for (uint32_t i = 0; i < coords; i++)
            c.c[i] = coord.c[i];
        c.c[coords] = ir_imm(0);
        coord = c;
    }
    uint32_t n = texel_count(o->type);
    HlType *texel = o->type->elem ? o->type->elem : hl_type_vector(l->h, BT_FLOAT, 4);
    Value all = load_texel(l, o, coord, sample_index, ms, texel, nullptr);
    Value r = lw_value(l, lv->type, lv->count);
    for (uint32_t i = 0; i < lv->count; i++)
        r.c[i] = all.c[lv->comps[i] < n ? lv->comps[i] : 0];
    return r;
}

void lw_object_store(Lower *l, LValue *lv, Value v)
{
    const ObjRef *o = &lv->obj;
    uint8_t object = o->type->object;
    if (!hl_object_is_uav(object)) {
        lw_error(l, (HlLoc){0, 1, 1}, 3025, "l-value specifies const object");
        return;
    }
    if (lv->kind == LV_STRUCTURED) {
        store_structured(l, o, comp(&lv->coord, 0), lv->has_byte_offset ? lv->byte_offset : (IrSrc){}, lv->comps,
                         &v, lv->count, false);
        return;
    }
    uint32_t n = texel_count(o->type);
    uint32_t dims = hl_object_dims(object), coords = dims + (hl_object_is_array(object) ? 1 : 0);
    if (object == OBJ_RWBUFFER)
        coords = 1;
    bool whole = lv->count == n;
    for (uint32_t i = 0; i < lv->count && whole; i++)
        whole = lv->comps[i] == i;
    if (!whole) {
        lw_error(l, (HlLoc){0, 1, 1}, 3700, "writes to typed views must write every component");
        return;
    }
    IrIns *ins = resource_ins(l, OP_STORE_UAV_TYPED, 0, coords + 4, o);
    uint32_t at = 0;
    put_group(ins, &at, &lv->coord, coords);
    /* typed stores take four components, the missing ones repeated. */
    for (uint32_t i = 0; i < 4; i++)
        ins->src[at + i] = comp(&v, i < v.count ? i : v.count - 1);
    ins->groups[ins->group_count++] = 4;
    ins->extra = n;
}

/* ------------------------------------------------------------------ atomics */

typedef struct Address {
    uint8_t kind;
    ObjRef obj;
    uint32_t tgsm;
    Value coord;
    uint32_t count;
} Address;

static bool atomic_address(Lower *l, HlExpr *dest, Address *a, HlLoc loc)
{
    LValue lv;
    if (!lw_lvalue(l, dest, &lv))
        return false;
    __builtin_memset(a, 0, sizeof(*a));
    switch (lv.kind) {
    case LV_TGSM: {
        VarInfo *vi = lv.vi;
        uint32_t per = vi->stride / 4 ? vi->stride / 4 : 1;
        uint32_t comp_ = lv.comps[0];
        IrSrc element = lv.has_dynamic ? lv.dynamic : ir_imm(0);
        if (comp_ / per)
            element = lw_op2(l, OP_IADD, element, ir_imm(comp_ / per));
        IrSrc bytes = ir_imm((comp_ % per) * 4);
        if (lv.has_byte_offset)
            bytes = lw_op2(l, OP_IADD, lv.byte_offset, bytes);
        a->kind = SRC_TGSM;
        a->tgsm = vi->tgsm;
        a->coord = lw_value(l, nullptr, 2);
        a->coord.c[0] = element;
        a->coord.c[1] = bytes;
        a->count = 2;
        return true;
    }
    case LV_STRUCTURED: {
        IrSrc bytes = ir_imm(lv.comps[0]);
        if (lv.has_byte_offset)
            bytes = lw_op2(l, OP_IADD, lv.byte_offset, bytes);
        a->kind = SRC_UAV;
        a->obj = lv.obj;
        a->coord = lw_value(l, nullptr, 2);
        a->coord.c[0] = comp(&lv.coord, 0);
        a->coord.c[1] = bytes;
        a->count = 2;
        return true;
    }
    case LV_TYPED: {
        uint8_t object = lv.obj.type->object;
        uint32_t dims = hl_object_dims(object) + (hl_object_is_array(object) ? 1 : 0);
        a->kind = SRC_UAV;
        a->obj = lv.obj;
        a->coord = lv.coord;
        a->count = object == OBJ_RWBUFFER ? 1 : dims;
        return true;
    }
    default:
        lw_error(l, loc, 3671, "interlocked targets must be groupshared or UAV elements");
        return false;
    }
}

static IrIns *atomic_ins(Lower *l, uint16_t op, bool returns, Address *a, uint32_t values)
{
    IrIns *ins;
    if (a->kind == SRC_TGSM) {
        ins = ir_emit(l->s, op, returns ? 1 : 0, a->count + values);
        ins->res_kind = SRC_TGSM;
        ins->res = a->tgsm;
    } else {
        ins = resource_ins(l, op, returns ? 1 : 0, a->count + values, &a->obj);
    }
    uint32_t at = 0;
    put_group(ins, &at, &a->coord, a->count);
    return ins;
}

static uint16_t atomic_op(uint32_t which, bool returns, bool is_signed)
{
    switch (which) {
    case 0: return returns ? OP_IMM_ATOMIC_IADD : OP_ATOMIC_IADD;
    case 1: return returns ? OP_IMM_ATOMIC_AND : OP_ATOMIC_AND;
    case 2: return returns ? OP_IMM_ATOMIC_OR : OP_ATOMIC_OR;
    case 3: return returns ? OP_IMM_ATOMIC_XOR : OP_ATOMIC_XOR;
    case 4: return is_signed ? (returns ? OP_IMM_ATOMIC_IMIN : OP_ATOMIC_IMIN) : (returns ? OP_IMM_ATOMIC_UMIN : OP_ATOMIC_UMIN);
    case 5: return is_signed ? (returns ? OP_IMM_ATOMIC_IMAX : OP_ATOMIC_IMAX) : (returns ? OP_IMM_ATOMIC_UMAX : OP_ATOMIC_UMAX);
    default: return OP_IMM_ATOMIC_EXCH;
    }
}

static void store_to(Lower *l, HlExpr *target, Value v)
{
    LValue lv;
    if (lw_lvalue(l, target, &lv))
        lw_store(l, &lv, lw_convert(l, v, target->type));
}

static Value interlocked(Lower *l, HlExpr *e)
{
    Address a;
    uint32_t id = e->intrinsic;
    Value none = lw_value(l, e->type, 0);
    Value value = arg(l, e, 1);
    Value value2 = {};
    if (id == I_INTERLOCKED_COMPARE_EXCHANGE || id == I_INTERLOCKED_COMPARE_STORE)
        value2 = arg(l, e, 2);
    if (!atomic_address(l, e->args[0], &a, e->loc))
        return none;
    bool is_signed = e->args[0]->type->base == BT_INT;
    if (id == I_INTERLOCKED_COMPARE_STORE) {
        IrIns *ins = atomic_ins(l, OP_ATOMIC_CMP_STORE, false, &a, 2);
        uint32_t at = a.count;
        put_scalar(ins, &at, comp(&value, 0));
        put_scalar(ins, &at, comp(&value2, 0));
        return none;
    }
    if (id == I_INTERLOCKED_COMPARE_EXCHANGE) {
        IrIns *ins = atomic_ins(l, OP_IMM_ATOMIC_CMP_EXCH, true, &a, 2);
        uint32_t at = a.count;
        put_scalar(ins, &at, comp(&value, 0));
        put_scalar(ins, &at, comp(&value2, 0));
        Value r = lw_value(l, e->args[0]->type, 1);
        r.c[0] = ir_reg(ins->dst[0]);
        store_to(l, e->args[3], r);
        return none;
    }
    bool returns = e->arg_count == 3;
    IrIns *ins = atomic_ins(l, atomic_op(id - I_INTERLOCKED_ADD, returns, is_signed), returns, &a, 1);
    uint32_t at = a.count;
    put_scalar(ins, &at, comp(&value, 0));
    if (returns) {
        Value r = lw_value(l, e->args[0]->type, 1);
        r.c[0] = ir_reg(ins->dst[0]);
        store_to(l, e->args[2], r);
    }
    return none;
}

/* ------------------------------------------------------------------ mul */

static Value mul(Lower *l, HlExpr *e)
{
    Value a = arg(l, e, 0), b = arg(l, e, 1);
    HlType *ta = e->args[0]->type, *tb = e->args[1]->type;
    uint8_t storage = storage_of(e->type);
    bool f = storage == BT_FLOAT || storage == BT_DOUBLE;
    Value r = result(l, e);
    if (ta->kind == TY_SCALAR || tb->kind == TY_SCALAR) {
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_binary_scalar(l, OPR_MUL, storage, comp(&a, i), comp(&b, i));
        return r;
    }
    if (ta->kind == TY_VECTOR && tb->kind == TY_VECTOR) {
        r.c[0] = dot(l, &a, &b, ta->cols, storage);
        return r;
    }
    /* each result is a dot product of a row of a and a column of b. */
    uint32_t ar = ta->kind == TY_VECTOR ? 1 : ta->rows, ac = ta->kind == TY_VECTOR ? ta->cols : ta->cols;
    uint32_t bc = tb->kind == TY_VECTOR ? 1 : tb->cols;
    for (uint32_t i = 0; i < ar; i++)
        for (uint32_t j = 0; j < bc; j++) {
            IrSrc acc = {};
            for (uint32_t k = 0; k < ac; k++) {
                IrSrc x = a.c[i * ac + k];
                IrSrc y = tb->kind == TY_VECTOR ? b.c[k] : b.c[k * bc + j];
                if (!k)
                    acc = f ? fmul(l, x, y) : lw_op2(l, OP_IMUL, x, y);
                else
                    acc = f ? fmad(l, x, y, acc) : lw_op3(l, OP_IMAD, x, y, acc);
            }
            r.c[i * bc + j] = acc;
        }
    return r;
}

/* ------------------------------------------------------------------ intrinsics */

static IrSrc legacy_unsupported(Lower *l, HlExpr *e)
{
    lw_error(l, e->loc, 3000, "this intrinsic is not supported for this target");
    return ir_imm(0);
}

Value lw_intrinsic(Lower *l, HlExpr *e)
{
    uint32_t id = e->intrinsic;
    Value r = result(l, e);
    uint8_t storage = r.type && hl_type_numeric_p(r.type) ? storage_of(r.type) : BT_FLOAT;
    switch (id) {
    case I_ABS: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_abs(l, comp(&a, i), storage);
        return r;
    }
    case I_SATURATE: case I_CEIL: case I_FLOOR: case I_ROUND: case I_TRUNC: case I_FRAC: case I_SQRT:
    case I_RSQRT: case I_EXP2: case I_LOG2: case I_DDX: case I_DDY: case I_DDX_COARSE: case I_DDX_FINE:
    case I_DDY_COARSE: case I_DDY_FINE: case I_RCP: {
        Value a = arg(l, e, 0);
        uint16_t op = 0;
        switch (id) {
        case I_CEIL: op = OP_ROUND_PI; break;
        case I_FLOOR: op = OP_ROUND_NI; break;
        case I_ROUND: op = OP_ROUND_NE; break;
        case I_TRUNC: op = OP_ROUND_Z; break;
        case I_FRAC: op = OP_FRC; break;
        case I_SQRT: op = OP_SQRT; break;
        case I_RSQRT: op = OP_RSQ; break;
        case I_EXP2: op = OP_EXP; break;
        case I_LOG2: op = OP_LOG; break;
        case I_DDX: op = OP_DERIV_RTX; break;
        case I_DDY: op = OP_DERIV_RTY; break;
        case I_DDX_COARSE: op = OP_DERIV_RTX_COARSE; break;
        case I_DDX_FINE: op = OP_DERIV_RTX_FINE; break;
        case I_DDY_COARSE: op = OP_DERIV_RTY_COARSE; break;
        case I_DDY_FINE: op = OP_DERIV_RTY_FINE; break;
        }
        for (uint32_t i = 0; i < r.count; i++) {
            if (id == I_SATURATE)
                r.c[i] = fsaturate(l, comp(&a, i));
            else if (id == I_RCP)
                r.c[i] = frcp(l, comp(&a, i));
            else
                r.c[i] = lw_op1(l, op, comp(&a, i));
        }
        return r;
    }
    case I_SIN: case I_COS: case I_TAN: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            if (id == I_TAN) {
                IrIns *ins = ir_emit(l->s, OP_SINCOS, 2, 1);
                ins->src[0] = comp(&a, i);
                r.c[i] = lw_op2(l, OP_DIV, ir_reg(ins->dst[0]), ir_reg(ins->dst[1]));
            } else {
                r.c[i] = sincos(l, comp(&a, i), id == I_COS);
            }
        }
        return r;
    }
    case I_SINCOS: {
        Value a = arg(l, e, 0);
        uint32_t n = a.count;
        Value s = lw_value(l, e->args[0]->type, n), c = lw_value(l, e->args[0]->type, n);
        for (uint32_t i = 0; i < n; i++) {
            IrIns *ins = ir_emit(l->s, OP_SINCOS, 2, 1);
            ins->src[0] = a.c[i];
            s.c[i] = ir_reg(ins->dst[0]);
            c.c[i] = ir_reg(ins->dst[1]);
        }
        store_to(l, e->args[1], s);
        store_to(l, e->args[2], c);
        return lw_value(l, e->type, 0);
    }
    case I_ASIN: case I_ACOS: case I_ATAN: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            r.c[i] = id == I_ASIN ? fasin(l, x) : id == I_ACOS ? facos(l, x) : fatan2(l, x, fimm(1.0f), false);
        }
        return r;
    }
    case I_ATAN2: {
        Value y = arg(l, e, 0), x = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = fatan2(l, comp(&y, i), comp(&x, i), true);
        return r;
    }
    case I_SINH: case I_COSH: case I_TANH: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            IrSrc ep = fexp(l, x), en = fexp(l, lw_neg(l, x, BT_FLOAT));
            if (id == I_SINH)
                r.c[i] = fmul(l, fsub(l, ep, en), fimm(0.5f));
            else if (id == I_COSH)
                r.c[i] = fmul(l, fadd(l, ep, en), fimm(0.5f));
            else
                r.c[i] = lw_op2(l, OP_DIV, fsub(l, ep, en), fadd(l, ep, en));
        }
        return r;
    }
    case I_EXP: case I_LOG: case I_LOG10: case I_DEGREES: case I_RADIANS: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            switch (id) {
            case I_EXP: r.c[i] = fexp(l, x); break;
            case I_LOG: r.c[i] = fmul(l, flog2(l, x), fimm(0.693147182f)); break;
            case I_LOG10: r.c[i] = fmul(l, flog2(l, x), fimm(0.301029995f)); break;
            case I_DEGREES: r.c[i] = fmul(l, x, fimm(57.2957795f)); break;
            default: r.c[i] = fmul(l, x, fimm(0.0174532925f)); break;
            }
        }
        return r;
    }
    case I_POW: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i), y = comp(&b, i);
            if (ir_is_imm(&y) && y.v == 0x3F800000)
                r.c[i] = x;
            else if (ir_is_imm(&y) && y.v == 0x40000000)
                r.c[i] = fmul(l, x, x);
            else
                r.c[i] = fexp2(l, fmul(l, flog2(l, x), y));
        }
        return r;
    }
    case I_FMOD: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_binary_scalar(l, OPR_MOD, BT_FLOAT, comp(&a, i), comp(&b, i));
        return r;
    }
    case I_STEP: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_op2(l, OP_AND, lw_op2(l, OP_GE, comp(&b, i), comp(&a, i)), fimm(1.0f));
        return r;
    }
    case I_LDEXP: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = fmul(l, comp(&a, i), fexp2(l, comp(&b, i)));
        return r;
    }
    case I_MIN: case I_MAX: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = min_max(l, id == I_MAX, storage, comp(&a, i), comp(&b, i));
        return r;
    }
    case I_CLAMP: {
        Value x = arg(l, e, 0), lo = arg(l, e, 1), hi = arg(l, e, 2);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = min_max(l, false, storage, min_max(l, true, storage, comp(&x, i), comp(&lo, i)), comp(&hi, i));
        return r;
    }
    case I_MAD: case I_FMA: {
        Value a = arg(l, e, 0), b = arg(l, e, 1), c = arg(l, e, 2);
        for (uint32_t i = 0; i < r.count; i++) {
            if (storage == BT_FLOAT || storage == BT_DOUBLE)
                r.c[i] = fmad(l, comp(&a, i), comp(&b, i), comp(&c, i));
            else
                r.c[i] = lw_op3(l, storage == BT_UINT ? OP_UMAD : OP_IMAD, comp(&a, i), comp(&b, i), comp(&c, i));
        }
        return r;
    }
    case I_LERP: {
        Value a = arg(l, e, 0), b = arg(l, e, 1), t = arg(l, e, 2);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = fmad(l, comp(&t, i), fsub(l, comp(&b, i), comp(&a, i)), comp(&a, i));
        return r;
    }
    case I_SMOOTHSTEP: {
        Value a = arg(l, e, 0), b = arg(l, e, 1), x = arg(l, e, 2);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc t = fsaturate(l, lw_op2(l, OP_DIV, fsub(l, comp(&x, i), comp(&a, i)), fsub(l, comp(&b, i), comp(&a, i))));
            IrSrc k = fmad(l, t, fimm(-2.0f), fimm(3.0f));
            r.c[i] = fmul(l, fmul(l, t, t), k);
        }
        return r;
    }
    case I_ALL: case I_ANY: {
        Value a = arg(l, e, 0);
        uint8_t st = storage_of(e->args[0]->type);
        IrSrc acc = lw_to_bool(l, a.c[0], st);
        for (uint32_t i = 1; i < a.count; i++)
            acc = lw_op2(l, id == I_ALL ? OP_AND : OP_OR, acc, lw_to_bool(l, a.c[i], st));
        r.c[0] = acc;
        return r;
    }
    case I_ISFINITE: case I_ISINF: case I_ISNAN: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            if (x.mod)
                x = lw_mov(l, x);
            if (id == I_ISFINITE)
                r.c[i] = lw_op2(l, OP_INE, lw_op2(l, OP_AND, x, ir_imm(0x7F800000)), ir_imm(0x7F800000));
            else if (id == I_ISINF)
                r.c[i] = lw_op2(l, OP_IEQ, lw_op2(l, OP_AND, x, ir_imm(0x7FFFFFFF)), ir_imm(0x7F800000));
            else
                r.c[i] = lw_op2(l, OP_ULT, ir_imm(0x7F800000), lw_op2(l, OP_AND, x, ir_imm(0x7FFFFFFF)));
        }
        return r;
    }
    case I_SIGN: {
        Value a = arg(l, e, 0);
        uint8_t st = storage_of(e->args[0]->type);
        bool f = st == BT_FLOAT || st == BT_DOUBLE;
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            IrSrc pos = lw_op2(l, f ? OP_LT : OP_ILT, f ? fimm(0.0f) : ir_imm(0), x);
            IrSrc neg = lw_op2(l, f ? OP_LT : OP_ILT, x, f ? fimm(0.0f) : ir_imm(0));
            r.c[i] = lw_op2(l, OP_IADD, lw_op1(l, OP_INEG, pos), neg);
        }
        return r;
    }
    case I_DOT: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        uint32_t n = hl_type_components(e->args[0]->type);
        r.c[0] = dot(l, &a, &b, n, storage_of(e->args[0]->type));
        return r;
    }
    case I_CROSS: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t j = (i + 1) % 3, k = (i + 2) % 3;
            r.c[i] = fmad(l, comp(&a, j), comp(&b, k), lw_neg(l, fmul(l, comp(&a, k), comp(&b, j)), BT_FLOAT));
        }
        return r;
    }
    case I_LENGTH: {
        Value a = arg(l, e, 0);
        uint32_t n = a.count;
        if (n == 1) {
            r.c[0] = lw_abs(l, a.c[0], BT_FLOAT);
            return r;
        }
        r.c[0] = lw_op1(l, OP_SQRT, dot(l, &a, &a, n, BT_FLOAT));
        return r;
    }
    case I_DISTANCE: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        uint32_t n = hl_type_components(e->args[0]->type);
        Value d = lw_value(l, e->args[0]->type, n);
        for (uint32_t i = 0; i < n; i++)
            d.c[i] = fsub(l, comp(&a, i), comp(&b, i));
        r.c[0] = n == 1 ? lw_abs(l, d.c[0], BT_FLOAT) : lw_op1(l, OP_SQRT, dot(l, &d, &d, n, BT_FLOAT));
        return r;
    }
    case I_NORMALIZE: {
        Value a = arg(l, e, 0);
        uint32_t n = a.count;
        IrSrc inv = lw_op1(l, OP_RSQ, n == 1 ? fmul(l, a.c[0], a.c[0]) : dot(l, &a, &a, n, BT_FLOAT));
        for (uint32_t i = 0; i < n; i++)
            r.c[i] = fmul(l, a.c[i], inv);
        return r;
    }
    case I_REFLECT: {
        Value i_ = arg(l, e, 0), n_ = arg(l, e, 1);
        uint32_t n = r.count;
        IrSrc d = dot(l, &n_, &i_, n, BT_FLOAT);
        IrSrc d2 = fadd(l, d, d);
        for (uint32_t k = 0; k < n; k++)
            r.c[k] = fmad(l, lw_neg(l, d2, BT_FLOAT), comp(&n_, k), comp(&i_, k));
        return r;
    }
    case I_REFRACT: {
        Value i_ = arg(l, e, 0), n_ = arg(l, e, 1), eta_ = arg(l, e, 2);
        uint32_t n = r.count;
        IrSrc eta = comp(&eta_, 0);
        IrSrc d = dot(l, &n_, &i_, n, BT_FLOAT);
        IrSrc k = fsub(l, fimm(1.0f), fmul(l, fmul(l, eta, eta), fsub(l, fimm(1.0f), fmul(l, d, d))));
        IrSrc ok = lw_op2(l, OP_GE, k, fimm(0.0f));
        IrSrc t = fmad(l, eta, d, lw_op1(l, OP_SQRT, k));
        for (uint32_t c = 0; c < n; c++) {
            IrSrc v = fsub(l, fmul(l, eta, comp(&i_, c)), fmul(l, t, comp(&n_, c)));
            r.c[c] = lw_op2(l, OP_AND, ok, v);
        }
        return r;
    }
    case I_FACEFORWARD: {
        Value n_ = arg(l, e, 0), i_ = arg(l, e, 1), ng = arg(l, e, 2);
        uint32_t n = r.count;
        IrSrc d = dot(l, &ng, &i_, n, BT_FLOAT);
        IrSrc neg = lw_op2(l, OP_LT, d, fimm(0.0f));
        for (uint32_t c = 0; c < n; c++)
            r.c[c] = lw_movc(l, neg, comp(&n_, c), lw_neg(l, comp(&n_, c), BT_FLOAT));
        return r;
    }
    case I_MUL:
        return mul(l, e);
    case I_TRANSPOSE: {
        Value a = arg(l, e, 0);
        HlType *t = e->args[0]->type;
        if (t->kind != TY_MATRIX) {
            for (uint32_t i = 0; i < r.count; i++)
                r.c[i] = comp(&a, i);
            return r;
        }
        for (uint32_t i = 0; i < t->rows; i++)
            for (uint32_t j = 0; j < t->cols; j++)
                r.c[j * t->rows + i] = a.c[i * t->cols + j];
        return r;
    }
    case I_DETERMINANT: {
        Value a = arg(l, e, 0);
        uint32_t n = e->args[0]->type->rows;
        uint32_t idx[4] = {0, 1, 2, 3};
        r.c[0] = determinant(l, &a, n, n, idx, idx);
        return r;
    }
    case I_ASFLOAT: case I_ASINT: case I_ASUINT: {
        if (e->arg_count == 3) {
            lw_error(l, e->loc, 3000, "doubles are not supported");
            return lw_value(l, e->type, 0);
        }
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            r.c[i] = x.mod ? lw_mov(l, x) : x;
        }
        return r;
    }
    case I_COUNTBITS: case I_REVERSEBITS: case I_FIRSTBITLOW: {
        Value a = arg(l, e, 0);
        uint16_t op = id == I_COUNTBITS ? OP_COUNTBITS : id == I_REVERSEBITS ? OP_BFREV : OP_FIRSTBIT_LO;
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_op1(l, op, comp(&a, i));
        return r;
    }
    case I_FIRSTBITHIGH: {
        Value a = arg(l, e, 0);
        bool is_signed = e->args[0]->type->base == BT_INT;
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = firstbit_from_low(l, is_signed ? OP_FIRSTBIT_SHI : OP_FIRSTBIT_HI, comp(&a, i));
        return r;
    }
    case I_F16TOF32: case I_F32TOF16: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++)
            r.c[i] = lw_op1(l, id == I_F16TOF32 ? OP_F16TOF32 : OP_F32TOF16, comp(&a, i));
        return r;
    }
    case I_MODF: {
        Value a = arg(l, e, 0);
        Value ip = lw_value(l, e->type, r.count);
        for (uint32_t i = 0; i < r.count; i++) {
            ip.c[i] = lw_op1(l, OP_ROUND_Z, comp(&a, i));
            r.c[i] = fsub(l, comp(&a, i), ip.c[i]);
        }
        store_to(l, e->args[1], ip);
        return r;
    }
    case I_FREXP: {
        Value a = arg(l, e, 0);
        Value ex = lw_value(l, e->type, r.count);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            if (x.mod)
                x = lw_mov(l, x);
            IrSrc nonzero = lw_op2(l, OP_NE, x, fimm(0.0f));
            IrSrc bits = lw_op2(l, OP_USHR, lw_op2(l, OP_AND, x, ir_imm(0x7F800000)), ir_imm(23));
            IrSrc e_ = lw_op1(l, OP_ITOF, lw_op2(l, OP_IADD, bits, ir_imm((uint32_t)-126)));
            ex.c[i] = lw_op2(l, OP_AND, nonzero, e_);
            IrSrc m = lw_op2(l, OP_OR, lw_op2(l, OP_AND, x, ir_imm(0x807FFFFF)), ir_imm(0x3F000000));
            r.c[i] = lw_op2(l, OP_AND, nonzero, m);
        }
        store_to(l, e->args[1], ex);
        return r;
    }
    case I_FWIDTH: {
        Value a = arg(l, e, 0);
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            r.c[i] = fadd(l, lw_abs(l, lw_op1(l, OP_DERIV_RTX, x), BT_FLOAT), lw_abs(l, lw_op1(l, OP_DERIV_RTY, x), BT_FLOAT));
        }
        return r;
    }
    case I_CLIP: {
        Value a = arg(l, e, 0);
        IrSrc any = {};
        for (uint32_t i = 0; i < a.count; i++) {
            IrSrc lt = lw_op2(l, OP_LT, a.c[i], fimm(0.0f));
            any = i ? lw_op2(l, OP_OR, any, lt) : lt;
        }
        if (any.kind == SRC_IMM && !any.v)
            return lw_value(l, e->type, 0);
        IrIns *ins = ir_emit(l->s, OP_DISCARD, 0, 1);
        ins->src[0] = any;
        ins->flags |= IRF_NZ;
        l->s->uses_discard = true;
        return lw_value(l, e->type, 0);
    }
    case I_LIT: {
        Value nl = arg(l, e, 0), nh = arg(l, e, 1), m = arg(l, e, 2);
        r.c[0] = fimm(1.0f);
        r.c[1] = lw_op2(l, OP_MAX, nl.c[0], fimm(0.0f));
        IrSrc off = lw_op2(l, OP_OR, lw_op2(l, OP_LT, nl.c[0], fimm(0.0f)), lw_op2(l, OP_LT, nh.c[0], fimm(0.0f)));
        IrSrc spec = fexp2(l, fmul(l, flog2(l, nh.c[0]), m.c[0]));
        r.c[2] = lw_movc(l, off, fimm(0.0f), spec);
        r.c[3] = fimm(1.0f);
        return r;
    }
    case I_DST: {
        Value a = arg(l, e, 0), b = arg(l, e, 1);
        r.c[0] = fimm(1.0f);
        r.c[1] = fmul(l, a.c[1], b.c[1]);
        r.c[2] = a.c[2];
        r.c[3] = b.c[3];
        return r;
    }
    case I_GROUP_BARRIER: case I_GROUP_BARRIER_SYNC: case I_DEVICE_BARRIER: case I_DEVICE_BARRIER_SYNC:
    case I_ALL_BARRIER: case I_ALL_BARRIER_SYNC: {
        uint32_t flags = 0;
        bool compute = l->s->profile.stage == STAGE_COMPUTE;
        switch (id) {
        case I_GROUP_BARRIER: flags = SYNC_GROUP_SHARED_MEMORY; break;
        case I_GROUP_BARRIER_SYNC: flags = SYNC_GROUP_SHARED_MEMORY | SYNC_THREADS_IN_GROUP; break;
        case I_DEVICE_BARRIER: flags = SYNC_UAV_GLOBAL; break;
        case I_DEVICE_BARRIER_SYNC: flags = SYNC_UAV_GLOBAL | SYNC_THREADS_IN_GROUP; break;
        case I_ALL_BARRIER: flags = SYNC_UAV_GLOBAL | SYNC_GROUP_SHARED_MEMORY; break;
        case I_ALL_BARRIER_SYNC: flags = SYNC_UAV_GLOBAL | SYNC_GROUP_SHARED_MEMORY | SYNC_THREADS_IN_GROUP; break;
        }
        if (!compute) {
            if (flags & (SYNC_THREADS_IN_GROUP | SYNC_GROUP_SHARED_MEMORY)) {
                lw_error(l, e->loc, 3000, "group barriers are only for compute shaders");
                return lw_value(l, e->type, 0);
            }
        }
        IrIns *ins = ir_emit(l->s, OP_SYNC, 0, 0);
        ins->extra = flags;
        return lw_value(l, e->type, 0);
    }
    case I_INTERLOCKED_ADD: case I_INTERLOCKED_AND: case I_INTERLOCKED_OR: case I_INTERLOCKED_XOR:
    case I_INTERLOCKED_MIN: case I_INTERLOCKED_MAX: case I_INTERLOCKED_EXCHANGE: case I_INTERLOCKED_COMPARE_EXCHANGE:
    case I_INTERLOCKED_COMPARE_STORE:
        return interlocked(l, e);
    case I_RT_SAMPLE_COUNT: {
        IrIns *ins = ir_emit(l->s, OP_SAMPLE_INFO, 1, 0);
        ins->res_kind = SRC_SV;
        ins->res = OPERAND_RASTERIZER;
        ins->uint_result = true;
        r.c[0] = ir_reg(ins->dst[0]);
        return r;
    }
    case I_RT_SAMPLE_POSITION: {
        Value a = arg(l, e, 0);
        IrIns *ins = ir_emit(l->s, OP_SAMPLE_POS, 2, 1);
        ins->src[0] = a.c[0];
        ins->res_kind = SRC_SV;
        ins->res = OPERAND_RASTERIZER;
        r.c[0] = ir_reg(ins->dst[0]);
        r.c[1] = ir_reg(ins->dst[1]);
        return r;
    }
    case I_EVAL_CENTROID: case I_EVAL_SAMPLE: case I_EVAL_SNAPPED: {
        Value a = arg(l, e, 0);
        Value b = e->arg_count > 1 ? arg(l, e, 1) : (Value){};
        for (uint32_t i = 0; i < r.count; i++) {
            IrSrc x = comp(&a, i);
            if (x.kind != SRC_IN) {
                lw_error(l, e->loc, 3000, "attribute evaluation needs a pixel shader input");
                return r;
            }
            uint16_t op = id == I_EVAL_CENTROID ? OP_EVAL_CENTROID : id == I_EVAL_SAMPLE ? OP_EVAL_SAMPLE_INDEX : OP_EVAL_SNAPPED;
            IrIns *ins = ir_emit(l->s, op, 1, id == I_EVAL_CENTROID ? 1 : id == I_EVAL_SAMPLE ? 2 : 3);
            ins->src[0] = x;
            if (id == I_EVAL_SAMPLE)
                ins->src[1] = b.c[0];
            if (id == I_EVAL_SNAPPED) {
                ins->src[1] = b.c[0];
                ins->src[2] = b.c[1];
                ins->groups[0] = 1;
                ins->groups[1] = 2;
                ins->group_count = 2;
            }
            r.c[i] = ir_reg(ins->dst[0]);
        }
        return r;
    }
    case I_D3DCOLORTOUBYTE4: {
        Value a = arg(l, e, 0);
        static const uint8_t order[4] = {2, 1, 0, 3};
        for (uint32_t i = 0; i < 4; i++)
            r.c[i] = lw_op1(l, OP_FTOI, fmul(l, a.c[order[i]], fimm(255.001953f)));
        return r;
    }
    case I_CHECK_ACCESS:
        r.c[0] = ir_imm(~0u);
        return r;
    case I_ABORT: case I_ERRORF: case I_PRINTF:
        return lw_value(l, e->type, 0);
    case I_ASDOUBLE: case I_MSAD4:
    default:
        if (id >= I_TEX1D && id <= I_TEXCUBEPROJ) {
            r.c[0] = legacy_unsupported(l, e);
            return r;
        }
        lw_error(l, e->loc, 3000, "intrinsic is not supported yet");
        return r;
    }
}

/* ------------------------------------------------------------------ methods */

static Value get_dimensions(Lower *l, HlExpr *e, const ObjRef *o)
{
    uint8_t object = o->type->object;
    Value none = lw_value(l, e->type, 0);
    uint32_t outs = e->arg_count;
    bool has_mip = false;
    IrSrc mip = ir_imm(0);
    switch (object) {
    case OBJ_TEXTURE1D: case OBJ_TEXTURE1DARRAY: case OBJ_TEXTURE2D: case OBJ_TEXTURE2DARRAY: case OBJ_TEXTURE3D:
    case OBJ_TEXTURECUBE: case OBJ_TEXTURECUBEARRAY: {
        uint32_t base = (object == OBJ_TEXTURE1D) ? 1 : (object == OBJ_TEXTURE1DARRAY || object == OBJ_TEXTURE2D || object == OBJ_TEXTURECUBE) ? 2 : 3;
        if (outs == base + 2) {
            Value m = arg(l, e, 0);
            mip = m.c[0];
            has_mip = true;
        }
        break;
    }
    default:
        break;
    }
    uint32_t first = has_mip ? 1 : 0;
    bool any_float = false;
    for (uint32_t i = first; i < outs; i++)
        any_float |= hl_base_float(e->args[i]->type->base);
    if (object == OBJ_BUFFER || object == OBJ_RWBUFFER || object == OBJ_BYTE_ADDRESS_BUFFER ||
        object == OBJ_RWBYTE_ADDRESS_BUFFER || object == OBJ_STRUCTURED_BUFFER || object == OBJ_RWSTRUCTURED_BUFFER ||
        object == OBJ_APPEND_STRUCTURED_BUFFER || object == OBJ_CONSUME_STRUCTURED_BUFFER) {
        IrIns *ins = resource_ins(l, OP_BUFINFO, 1, 0, o);
        Value v = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
        v.c[0] = ir_reg(ins->dst[0]);
        store_to(l, e->args[0], v);
        if (outs >= 2) {
            Value s = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
            uint32_t stride = o->type->elem ? lw_tight_size(o->type->elem, l->s->row_major_default) : 4;
            s.c[0] = ir_imm(stride);
            store_to(l, e->args[1], s);
        }
        return none;
    }
    if (object == OBJ_TEXTURE2DMS || object == OBJ_TEXTURE2DMSARRAY) {
        IrIns *ins = resource_ins(l, OP_RESINFO, 4, 1, o);
        ins->src[0] = ir_imm(0);
        ins->uint_result = true;
        Value v = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
        uint32_t i;
        for (i = 0; i + 1 < outs; i++) {
            v.c[0] = ir_reg(ins->dst[i]);
            store_to(l, e->args[i], v);
        }
        IrIns *si = resource_ins(l, OP_SAMPLE_INFO, 1, 0, o);
        si->uint_result = true;
        v.c[0] = ir_reg(si->dst[0]);
        store_to(l, e->args[outs - 1], v);
        return none;
    }
    /* resinfo gives width, height, depth or elements, then the levels. */
    IrIns *ins = resource_ins(l, OP_RESINFO, 4, 1, o);
    ins->src[0] = mip;
    ins->uint_result = true;
    uint32_t dims = (object == OBJ_TEXTURE1D || object == OBJ_RWTEXTURE1D) ? 1 :
                    (object == OBJ_TEXTURE1DARRAY || object == OBJ_RWTEXTURE1DARRAY || object == OBJ_TEXTURE2D ||
                     object == OBJ_RWTEXTURE2D || object == OBJ_TEXTURECUBE) ? 2 : 3;
    Value v = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
    for (uint32_t i = first; i < outs; i++) {
        uint32_t k = i - first;
        uint32_t c = k < dims ? k : 3;
        v.c[0] = ir_reg(ins->dst[c]);
        store_to(l, e->args[i], v);
    }
    (void)any_float;
    return none;
}

static void stream_append(Lower *l, HlExpr *e, const ObjRef *o);

Value lw_method(Lower *l, HlExpr *e)
{
    ObjRef o;
    if (!lw_obj(l, e->a, &o))
        return result(l, e);
    uint8_t object = o.type->object;
    uint32_t id = e->intrinsic;
    switch (id) {
    case M_SAMPLE: case M_SAMPLE_BIAS: case M_SAMPLE_GRAD: case M_SAMPLE_LEVEL: case M_SAMPLE_CMP:
    case M_SAMPLE_CMP_LEVEL_ZERO:
        return sample(l, e, &o);
    case M_GATHER: case M_GATHER_RED: case M_GATHER_GREEN: case M_GATHER_BLUE: case M_GATHER_ALPHA:
    case M_GATHER_CMP: case M_GATHER_CMP_RED: case M_GATHER_CMP_GREEN: case M_GATHER_CMP_BLUE:
    case M_GATHER_CMP_ALPHA:
        return gather(l, e, &o);
    case M_CALCULATE_LOD: case M_CALCULATE_LOD_UNCLAMPED: {
        Value coord = arg(l, e, 1);
        uint32_t dims = hl_object_dims(object);
        IrIns *ins = resource_ins(l, OP_LOD, 2, dims, &o);
        uint32_t at = 0;
        put_group(ins, &at, &coord, dims);
        set_sampler(l, ins, e->args[0]);
        Value r = result(l, e);
        r.c[0] = ir_reg(ins->dst[id == M_CALCULATE_LOD ? 0 : 1]);
        return r;
    }
    case M_LOAD: {
        if (object == OBJ_STRUCTURED_BUFFER || object == OBJ_RWSTRUCTURED_BUFFER) {
            Value index = arg(l, e, 0);
            uint32_t n = hl_type_components(e->type);
            uint32_t *offs = hl_alloc(l->h, (n ? n : 1) * sizeof(uint32_t));
            uint32_t k = 0;
            lw_tight_offsets(e->type, 0, l->s->row_major_default, offs, &k);
            return load_structured(l, &o, index.c[0], (IrSrc){}, offs, n, e->type, false);
        }
        if (object == OBJ_BYTE_ADDRESS_BUFFER || object == OBJ_RWBYTE_ADDRESS_BUFFER) {
            Value addr = arg(l, e, 0);
            uint32_t offs[1] = {0};
            return load_structured(l, &o, ir_imm(0), addr.c[0], offs, 1, e->type, true);
        }
        Value coord = arg(l, e, 0);
        bool ms = object == OBJ_TEXTURE2DMS || object == OBJ_TEXTURE2DMSARRAY;
        IrSrc sample_index = {};
        uint32_t next = 1;
        if (ms) {
            Value s = arg(l, e, 1);
            sample_index = s.c[0];
            next = 2;
        }
        HlExpr *offset = e->arg_count > next && !hl_object_is_uav(object) && object != OBJ_BUFFER ? e->args[next] : nullptr;
        if (offset && e->arg_count > next + 1) {
            lw_error(l, e->loc, 3000, "status values of loads are not supported");
            return result(l, e);
        }
        HlType *texel = o.type->elem ? o.type->elem : hl_type_vector(l->h, BT_FLOAT, 4);
        return load_texel(l, &o, coord, sample_index, ms, texel, offset);
    }
    case M_LOAD2: case M_LOAD3: case M_LOAD4: {
        Value addr = arg(l, e, 0);
        uint32_t n = id - M_LOAD2 + 2, offs[4] = {0, 4, 8, 12};
        return load_structured(l, &o, ir_imm(0), addr.c[0], offs, n, e->type, true);
    }
    case M_STORE: case M_STORE2: case M_STORE3: case M_STORE4: {
        Value addr = arg(l, e, 0), v = arg(l, e, 1);
        uint32_t n = id - M_STORE + 1, offs[4] = {0, 4, 8, 12};
        store_structured(l, &o, ir_imm(0), addr.c[0], offs, &v, n, true);
        return lw_value(l, e->type, 0);
    }
    case M_INTERLOCKED_ADD: case M_INTERLOCKED_AND: case M_INTERLOCKED_OR: case M_INTERLOCKED_XOR:
    case M_INTERLOCKED_MIN: case M_INTERLOCKED_MAX: case M_INTERLOCKED_EXCHANGE: case M_INTERLOCKED_COMPARE_EXCHANGE:
    case M_INTERLOCKED_COMPARE_STORE: {
        Address a = {.kind = SRC_UAV, .obj = o, .count = 1};
        a.coord = arg(l, e, 0);
        Value v = arg(l, e, 1);
        Value none = lw_value(l, e->type, 0);
        if (id == M_INTERLOCKED_COMPARE_STORE || id == M_INTERLOCKED_COMPARE_EXCHANGE) {
            Value v2 = arg(l, e, 2);
            bool exch = id == M_INTERLOCKED_COMPARE_EXCHANGE;
            IrIns *ins = atomic_ins(l, exch ? OP_IMM_ATOMIC_CMP_EXCH : OP_ATOMIC_CMP_STORE, exch, &a, 2);
            uint32_t at = 1;
            put_scalar(ins, &at, v.c[0]);
            put_scalar(ins, &at, v2.c[0]);
            if (exch) {
                Value r = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
                r.c[0] = ir_reg(ins->dst[0]);
                store_to(l, e->args[3], r);
            }
            return none;
        }
        bool returns = e->arg_count == 3;
        bool is_signed = e->args[1]->type->base == BT_INT;
        IrIns *ins = atomic_ins(l, atomic_op(id - M_INTERLOCKED_ADD, returns, is_signed), returns, &a, 1);
        uint32_t at = 1;
        put_scalar(ins, &at, v.c[0]);
        if (returns) {
            Value r = lw_value(l, hl_type_scalar(l->h, BT_UINT), 1);
            r.c[0] = ir_reg(ins->dst[0]);
            store_to(l, e->args[2], r);
        }
        return none;
    }
    case M_GET_DIMENSIONS:
        return get_dimensions(l, e, &o);
    case M_GET_SAMPLE_POSITION: {
        Value s = arg(l, e, 0);
        IrIns *ins = resource_ins(l, OP_SAMPLE_POS, 2, 1, &o);
        ins->src[0] = s.c[0];
        return take(l, e->type, ins, 2);
    }
    case M_INCREMENT_COUNTER: case M_DECREMENT_COUNTER: {
        IrIns *ins = resource_ins(l, id == M_INCREMENT_COUNTER ? OP_IMM_ATOMIC_ALLOC : OP_IMM_ATOMIC_CONSUME, 1, 0, &o);
        l->s->resources[ins->res].counter = true;
        return take(l, e->type, ins, 1);
    }
    case M_APPEND: {
        if (object == OBJ_POINT_STREAM || object == OBJ_LINE_STREAM || object == OBJ_TRIANGLE_STREAM) {
            stream_append(l, e, &o);
            return lw_value(l, e->type, 0);
        }
        Value v = arg(l, e, 0);
        IrIns *ins = resource_ins(l, OP_IMM_ATOMIC_ALLOC, 1, 0, &o);
        l->s->resources[ins->res].counter = true;
        IrSrc index = ir_reg(ins->dst[0]);
        uint32_t n = hl_type_components(e->args[0]->type);
        uint32_t *offs = hl_alloc(l->h, (n ? n : 1) * sizeof(uint32_t));
        uint32_t k = 0;
        lw_tight_offsets(e->args[0]->type, 0, l->s->row_major_default, offs, &k);
        store_structured(l, &o, index, (IrSrc){}, offs, &v, n, false);
        return lw_value(l, e->type, 0);
    }
    case M_CONSUME: {
        IrIns *ins = resource_ins(l, OP_IMM_ATOMIC_CONSUME, 1, 0, &o);
        l->s->resources[ins->res].counter = true;
        IrSrc index = ir_reg(ins->dst[0]);
        uint32_t n = hl_type_components(e->type);
        uint32_t *offs = hl_alloc(l->h, (n ? n : 1) * sizeof(uint32_t));
        uint32_t k = 0;
        lw_tight_offsets(e->type, 0, l->s->row_major_default, offs, &k);
        return load_structured(l, &o, index, (IrSrc){}, offs, n, e->type, false);
    }
    case M_RESTART_STRIP: {
        IrIns *ins = ir_emit(l->s, l->s->gs_stream_mask > 1 ? OP_CUT_STREAM : OP_CUT, 0, 0);
        ins->extra = 0;
        return lw_value(l, e->type, 0);
    }
    }
    lw_error(l, e->loc, 3000, "method is not supported yet");
    return result(l, e);
}

/* ------------------------------------------------------------------ geometry shader streams */

void lw_stream_store(Lower *l, Value v);

static void stream_append(Lower *l, HlExpr *e, const ObjRef *o)
{
    Value v = arg(l, e, 0);
    lw_stream_store(l, v);
    IrIns *ins = ir_emit(l->s, OP_EMIT, 0, 0);
    (void)ins;
    (void)o;
}
