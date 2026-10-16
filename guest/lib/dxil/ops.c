/*
 * the arithmetic of dx.op, conversions, derivatives and wave operations.
 * dxil passes scalars, or vectors with long vectors; each operation works
 * on whatever its operands are.
 */
#include "internal.h"

static uint32_t t_i32(Compiler *c) { return spv_type_int(&c->b, 32, 1); }

/* the operand of a dx.op, after the opcode. */
static uint32_t arg(Compiler *c, const DxInst *ins, uint32_t i) { return dxil_value(c, ins->ops[2 + i]); }

static uint32_t arg_type(Compiler *c, const DxInst *ins, uint32_t i)
{
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[2 + i]);
    return v ? v->type : ins->type;
}

/* a constant of the scalar type, splatted over the value type's vector. */
static uint32_t splat(Compiler *c, uint32_t dx_type, uint64_t bits)
{
    const DxType *t = &c->m.types[dx_type];
    uint32_t scalar_dx = dxil_component_type(c, dx_type);
    uint32_t scalar = dxil_type(c, scalar_dx);
    uint32_t k;
    if (scalar == c->t_u64 || scalar == c->t_f64)
        k = spv_const64(&c->b, scalar, bits);
    else
        k = spv_const(&c->b, scalar, (uint32_t)bits);
    if (t->kind != DT_VECTOR)
        return k;
    uint32_t parts[16];
    for (uint32_t i = 0; i < t->count && i < 16; i++)
        parts[i] = k;
    return spv_const_composite(&c->b, dxil_type(c, dx_type), parts, t->count < 16 ? t->count : 16);
}

/* the bits of 1.0 and 0.0 and the like in a float type's width. */
static uint64_t float_bits(Compiler *c, uint32_t dx_type, double v)
{
    uint32_t scalar = dxil_type(c, dxil_component_type(c, dx_type));
    if (scalar == c->t_f64) {
        union {
            double d;
            uint64_t u;
        } x = {v};
        return x.u;
    }
    union {
        float f;
        uint32_t u;
    } x = {(float)v};
    if (scalar == c->t_f16) {
        /* the few constants used here are exact halves. */
        uint32_t f = x.u, sign = (f >> 16) & 0x8000, exp = (f >> 23) & 0xFF;
        return exp ? sign | (exp - 112) << 10 | ((f >> 13) & 0x3FF) : sign;
    }
    return x.u;
}

static uint32_t glsl1(Compiler *c, const DxInst *ins, uint32_t instruction)
{
    uint32_t a = arg(c, ins, 0);
    return spv_glsl(&c->b, dxil_type(c, ins->type), instruction, &a, 1);
}

static uint32_t glsl2(Compiler *c, const DxInst *ins, uint32_t instruction)
{
    uint32_t a[2] = {arg(c, ins, 0), arg(c, ins, 1)};
    return spv_glsl(&c->b, dxil_type(c, ins->type), instruction, a, 2);
}

static uint32_t is_special(Compiler *c, const DxInst *ins, uint32_t op)
{
    uint32_t a = arg(c, ins, 0), at = arg_type(c, ins, 0);
    uint32_t type = dxil_type(c, ins->type);
    uint32_t nan = spv_op1(&c->b, SpvOpIsNan, type, a), inf = spv_op1(&c->b, SpvOpIsInf, type, a);
    switch (op) {
    case 8: return nan;
    case 9: return inf;
    case 10: return spv_op1(&c->b, SpvOpLogicalNot, type, spv_op2(&c->b, SpvOpLogicalOr, type, nan, inf));
    default: {
        /* normal: not zero, a denormal, an infinity or a nan. */
        uint32_t abs = spv_glsl(&c->b, dxil_type(c, at), GLSLstd450FAbs, &a, 1);
        uint32_t ge = spv_op2(&c->b, SpvOpFOrdGreaterThanEqual, type, abs,
                              splat(c, at, dxil_type(c, dxil_component_type(c, at)) == c->t_f64 ? 0x0010000000000000ull
                                           : dxil_type(c, dxil_component_type(c, at)) == c->t_f16 ? 0x0400
                                                                                                    : 0x00800000));
        return spv_op2(&c->b, SpvOpLogicalAnd, type, ge, spv_op1(&c->b, SpvOpLogicalNot, type, inf));
    }
    }
}

/* the low and high words of a 64-bit operand. */
static void split64(Compiler *c, uint32_t v, uint32_t *lo, uint32_t *hi)
{
    uint32_t pair = spv_op1(&c->b, SpvOpBitcast, c->t_uvec[2], v);
    *lo = dxil_extract(c, c->t_u32, pair, 0);
    *hi = dxil_extract(c, c->t_u32, pair, 1);
}

/* the bit a find of glsl gives for a 64-bit value, from those of its words: all ones when none. */
static uint32_t find64(Compiler *c, uint32_t v, uint32_t instruction)
{
    uint32_t lo, hi;
    split64(c, v, &lo, &hi);
    uint32_t l = spv_glsl(&c->b, c->t_u32, instruction == GLSLstd450FindILsb ? GLSLstd450FindILsb : GLSLstd450FindUMsb, &lo, 1);
    uint32_t h = spv_glsl(&c->b, c->t_u32, instruction, &hi, 1);
    uint32_t none = dxil_uconst(c, ~0u);
    uint32_t h32 = spv_op2(&c->b, SpvOpIAdd, c->t_u32, h, dxil_uconst(c, 32));
    if (instruction == GLSLstd450FindILsb)
        return spv_op3(&c->b, SpvOpSelect, c->t_u32, spv_op2(&c->b, SpvOpIEqual, c->t_bool, l, none),
                       spv_op3(&c->b, SpvOpSelect, c->t_u32, spv_op2(&c->b, SpvOpIEqual, c->t_bool, h, none), none, h32), l);
    /* the top word decides; a signed value's top word of all ones or zeros leaves it to the bottom one. */
    uint32_t top_word = spv_op2(&c->b, SpvOpINotEqual, c->t_bool, h, none);
    if (instruction == GLSLstd450FindSMsb) {
        uint32_t sign = spv_op2(&c->b, SpvOpShiftRightArithmetic, c->t_u32, hi, dxil_uconst(c, 31));
        uint32_t flipped = spv_op2(&c->b, SpvOpBitwiseXor, c->t_u32, lo, sign);
        l = spv_glsl(&c->b, c->t_u32, GLSLstd450FindUMsb, &flipped, 1);
    }
    return spv_op3(&c->b, SpvOpSelect, c->t_u32, top_word, h32, l);
}

/* firstbitHi and firstbitSHi count from the top bit, all ones when none is found. */
static uint32_t first_bit_high(Compiler *c, const DxInst *ins, int is_signed)
{
    uint32_t a = arg(c, ins, 0);
    uint32_t type = dxil_type(c, ins->type), at = dxil_type(c, arg_type(c, ins, 0));
    uint32_t instruction = is_signed ? GLSLstd450FindSMsb : GLSLstd450FindUMsb;
    uint32_t r, top = 31;
    if (at == c->t_u64) {
        r = find64(c, a, instruction);
        top = 63;
    } else {
        r = spv_glsl(&c->b, c->t_u32, instruction, &a, 1);
    }
    uint32_t none = spv_op2(&c->b, SpvOpIEqual, c->t_bool, r, dxil_uconst(c, ~0u));
    uint32_t flipped = spv_op2(&c->b, SpvOpISub, c->t_u32, dxil_uconst(c, top), r);
    return dxil_convert(c, spv_op3(&c->b, SpvOpSelect, c->t_u32, none, r, flipped), c->t_u32, type);
}

/* the width and offset of a bit field, masked as direct3d does and the width cut at the top bit. */
static void bit_field(Compiler *c, uint32_t *width, uint32_t *offset)
{
    uint32_t mask = dxil_uconst(c, 31);
    *width = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, *width, mask);
    *offset = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, *offset, mask);
    uint32_t args[2] = {*width, spv_op2(&c->b, SpvOpISub, c->t_u32, dxil_uconst(c, 32), *offset)};
    *width = spv_glsl(&c->b, c->t_u32, GLSLstd450UMin, args, 2);
}

/* masked sums of absolute differences: the bytes where the reference byte is not zero. */
static uint32_t msad(Compiler *c, const DxInst *ins)
{
    uint32_t ref = arg(c, ins, 0), src = arg(c, ins, 1), sum = arg(c, ins, 2);
    uint32_t u = c->t_u32, i = t_i32(c);
    for (uint32_t b = 0; b < 4; b++) {
        uint32_t at = dxil_uconst(c, 8 * b), eight = dxil_uconst(c, 8);
        uint32_t rb = spv_op3(&c->b, SpvOpBitFieldUExtract, u, ref, at, eight);
        uint32_t sb = spv_op3(&c->b, SpvOpBitFieldUExtract, u, src, at, eight);
        uint32_t d = spv_op2(&c->b, SpvOpISub, i, spv_op1(&c->b, SpvOpBitcast, i, rb), spv_op1(&c->b, SpvOpBitcast, i, sb));
        d = spv_op1(&c->b, SpvOpBitcast, u, spv_glsl(&c->b, i, GLSLstd450SAbs, &d, 1));
        d = spv_op3(&c->b, SpvOpSelect, u, spv_op2(&c->b, SpvOpINotEqual, c->t_bool, rb, dxil_uconst(c, 0)), d,
                    dxil_uconst(c, 0));
        sum = spv_op2(&c->b, SpvOpIAdd, u, sum, d);
    }
    return sum;
}

static uint32_t dot(Compiler *c, const DxInst *ins, uint32_t n)
{
    uint32_t a[4], b[4];
    for (uint32_t i = 0; i < n; i++) {
        a[i] = arg(c, ins, i);
        b[i] = arg(c, ins, n + i);
    }
    uint32_t type = dxil_type(c, ins->type);
    if (type == c->t_f32 || type == c->t_f16) {
        uint32_t va = dxil_vector(c, type, a, n), vb = dxil_vector(c, type, b, n);
        return spv_op2(&c->b, SpvOpDot, type, va, vb);
    }
    uint32_t sum = spv_op2(&c->b, SpvOpIMul, type, a[0], b[0]);
    for (uint32_t i = 1; i < n; i++)
        sum = spv_op2(&c->b, SpvOpIAdd, type, sum, spv_op2(&c->b, SpvOpIMul, type, a[i], b[i]));
    return sum;
}

/* dot4AddI8Packed and dot4AddU8Packed: the dot product of four bytes each, added to the accumulator. */
static uint32_t dot4_packed(Compiler *c, const DxInst *ins, int is_signed)
{
    uint32_t acc = arg(c, ins, 0), a = arg(c, ins, 1), b = arg(c, ins, 2);
    uint32_t u = c->t_u32;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t at = dxil_uconst(c, 8 * i), eight = dxil_uconst(c, 8);
        uint32_t op = is_signed ? SpvOpBitFieldSExtract : SpvOpBitFieldUExtract;
        uint32_t x = spv_op3(&c->b, op, u, a, at, eight), y = spv_op3(&c->b, op, u, b, at, eight);
        acc = spv_op2(&c->b, SpvOpIAdd, u, acc, spv_op2(&c->b, SpvOpIMul, u, x, y));
    }
    return acc;
}

/* dx.types.i32c and the like: a value and a carry or borrow. */
static void add_with_carry(Compiler *c, const DxInst *ins, int subtract)
{
    uint32_t a = arg(c, ins, 0), b = arg(c, ins, 1);
    uint32_t pair = spv_type_struct(&c->b, (uint32_t[]){c->t_u32, c->t_u32}, 2);
    uint32_t r = spv_op2(&c->b, subtract ? SpvOpISubBorrow : SpvOpIAddCarry, pair, a, b);
    uint32_t parts[2] = {dxil_extract(c, c->t_u32, r, 0),
                         spv_op2(&c->b, SpvOpINotEqual, c->t_bool, dxil_extract(c, c->t_u32, r, 1), dxil_uconst(c, 0))};
    dxil_set_parts(c, ins->value, parts, 2);
}

/* the 16-bit float of legacyF32ToF16, in the low half of a word, and back. */
static uint32_t f32_to_f16(Compiler *c, uint32_t v)
{
    uint32_t parts[2] = {v, dxil_fconst(c, 0.0f)};
    uint32_t packed = spv_glsl(&c->b, c->t_u32, GLSLstd450PackHalf2x16, (uint32_t[]){dxil_vector(c, c->t_f32, parts, 2)}, 1);
    return spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, packed, dxil_uconst(c, 0xFFFF));
}

static uint32_t f16_to_f32(Compiler *c, uint32_t v)
{
    uint32_t pair = spv_glsl(&c->b, c->t_vec[2], GLSLstd450UnpackHalf2x16, &v, 1);
    return dxil_extract(c, c->t_f32, pair, 0);
}

/* pack4x8: the low bytes of four values, truncated, clamped to 0 to 255, or clamped to -128 to 127 by mode. */
static uint32_t pack4x8(Compiler *c, const DxInst *ins)
{
    uint32_t mode = (uint32_t)dxil_constant(c, ins->ops[2], 0), r = dxil_uconst(c, 0), i32 = t_i32(c);
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t v = dxil_convert(c, arg(c, ins, 1 + i), dxil_type(c, arg_type(c, ins, 1 + i)), c->t_u32);
        if (mode) {
            uint32_t args[3] = {spv_op1(&c->b, SpvOpBitcast, i32, v), spv_const(&c->b, i32, mode == 1 ? 0 : (uint32_t)-128),
                                spv_const(&c->b, i32, mode == 1 ? 255 : 127)};
            v = spv_op1(&c->b, SpvOpBitcast, c->t_u32, spv_glsl(&c->b, i32, GLSLstd450SClamp, args, 3));
        }
        v = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, v, dxil_uconst(c, 0xFF));
        r = spv_op2(&c->b, SpvOpBitwiseOr, c->t_u32, r, spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, v, dxil_uconst(c, 8 * i)));
    }
    return r;
}

/* unpack4x8: four bytes, zero or sign extended by mode. */
static void unpack4x8(Compiler *c, const DxInst *ins)
{
    uint32_t op = dxil_constant(c, ins->ops[2], 0) ? SpvOpBitFieldSExtract : SpvOpBitFieldUExtract;
    uint32_t pk = arg(c, ins, 1), parts[4];
    const DxType *t = &c->m.types[ins->type];
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t v = spv_op3(&c->b, op, c->t_u32, pk, dxil_uconst(c, 8 * i), dxil_uconst(c, 8));
        uint32_t to = t->kind == DT_STRUCT && i < t->count ? dxil_type(c, t->members[i]) : c->t_u32;
        parts[i] = to == c->t_u32 ? v : spv_op1(&c->b, op == SpvOpBitFieldSExtract ? SpvOpSConvert : SpvOpUConvert, to, v);
    }
    dxil_set_parts(c, ins->value, parts, 4);
}

/* ------------------------------------------------------------------ waves */

static void subgroup(Compiler *c, uint32_t capability)
{
    spv_capability(&c->b, SpvCapabilityGroupNonUniform);
    if (capability)
        spv_capability(&c->b, capability);
}

static uint32_t subgroup_scope(Compiler *c) { return dxil_uconst(c, SpvScopeSubgroup); }

static uint32_t wave_reduce(Compiler *c, const DxInst *ins, int prefix)
{
    uint32_t value = arg(c, ins, 0), type = dxil_type(c, ins->type);
    uint32_t kind = (uint32_t)dxil_constant(c, ins->ops[3], 0), sop = (uint32_t)dxil_constant(c, ins->ops[4], 0);
    int is_float = dxil_type(c, dxil_component_type(c, ins->type)) == c->t_f32 ||
                   dxil_type(c, dxil_component_type(c, ins->type)) == c->t_f16 ||
                   dxil_type(c, dxil_component_type(c, ins->type)) == c->t_f64;
    uint32_t op;
    switch (kind) {
    case 0: op = is_float ? SpvOpGroupNonUniformFAdd : SpvOpGroupNonUniformIAdd; break;
    case 1: op = is_float ? SpvOpGroupNonUniformFMul : SpvOpGroupNonUniformIMul; break;
    case 2: op = is_float ? SpvOpGroupNonUniformFMin : sop ? SpvOpGroupNonUniformUMin : SpvOpGroupNonUniformSMin; break;
    default: op = is_float ? SpvOpGroupNonUniformFMax : sop ? SpvOpGroupNonUniformUMax : SpvOpGroupNonUniformSMax; break;
    }
    subgroup(c, SpvCapabilityGroupNonUniformArithmetic);
    uint32_t w[3] = {subgroup_scope(c), prefix ? SpvGroupOperationExclusiveScan : SpvGroupOperationReduce, value};
    return spv_op(&c->b, op, type, w, 3);
}

static uint32_t wave_bits(Compiler *c, const DxInst *ins)
{
    static const uint32_t OPS[3] = {SpvOpGroupNonUniformBitwiseAnd, SpvOpGroupNonUniformBitwiseOr,
                                    SpvOpGroupNonUniformBitwiseXor};
    uint32_t kind = (uint32_t)dxil_constant(c, ins->ops[3], 0);
    subgroup(c, SpvCapabilityGroupNonUniformArithmetic);
    uint32_t w[3] = {subgroup_scope(c), SpvGroupOperationReduce, arg(c, ins, 0)};
    return spv_op(&c->b, OPS[kind < 3 ? kind : 0], dxil_type(c, ins->type), w, 3);
}

static void ballot(Compiler *c, const DxInst *ins)
{
    subgroup(c, SpvCapabilityGroupNonUniformBallot);
    uint32_t v = spv_op2(&c->b, SpvOpGroupNonUniformBallot, c->t_uvec[4], subgroup_scope(c), arg(c, ins, 0));
    uint32_t parts[4];
    for (uint32_t i = 0; i < 4; i++)
        parts[i] = dxil_extract(c, c->t_u32, v, i);
    dxil_set_parts(c, ins->value, parts, 4);
}

static uint32_t bit_count(Compiler *c, const DxInst *ins, uint32_t operation)
{
    subgroup(c, SpvCapabilityGroupNonUniformBallot);
    uint32_t v = spv_op2(&c->b, SpvOpGroupNonUniformBallot, c->t_uvec[4], subgroup_scope(c), arg(c, ins, 0));
    uint32_t w[3] = {subgroup_scope(c), operation, v};
    return spv_op(&c->b, SpvOpGroupNonUniformBallotBitCount, c->t_u32, w, 3);
}

static uint32_t quad_swap(Compiler *c, uint32_t type, uint32_t value, uint32_t direction)
{
    subgroup(c, SpvCapabilityGroupNonUniformQuad);
    uint32_t w[3] = {subgroup_scope(c), value, dxil_uconst(c, direction)};
    return spv_op(&c->b, SpvOpGroupNonUniformQuadSwap, type, w, 3);
}

/* a value compared with another by its bits, as WaveMatch compares lanes' values. */
static uint32_t same_bits(Compiler *c, uint32_t type, uint32_t a, uint32_t b)
{
    if (type == c->t_bool)
        return spv_op2(&c->b, SpvOpLogicalEqual, c->t_bool, a, b);
    uint32_t as = type;
    if (type == c->t_f32)
        as = c->t_u32;
    else if (type == c->t_f16)
        as = c->t_u16;
    else if (type == c->t_f64)
        as = c->t_u64;
    if (as != type) {
        a = spv_op1(&c->b, SpvOpBitcast, as, a);
        b = spv_op1(&c->b, SpvOpBitcast, as, b);
    }
    return spv_op2(&c->b, SpvOpIEqual, c->t_bool, a, b);
}

/*
 * waveMatch: a loop taking the first active lane's value each time, the
 * lanes equal to it leaving it together with their ballot.
 */
static void wave_match(Compiler *c, const DxInst *ins)
{
    subgroup(c, SpvCapabilityGroupNonUniformBallot);
    uint32_t value = arg(c, ins, 0), type = dxil_type(c, arg_type(c, ins, 0));
    uint32_t header = spv_id(&c->b), body = spv_id(&c->b), cont = spv_id(&c->b), merge = spv_id(&c->b);
    spv_branch(&c->b, header);
    spv_label(&c->b, header);
    uint32_t loop[3] = {merge, cont, SpvLoopControlMaskNone};
    spv_op_void(&c->b, SpvOpLoopMerge, loop, 3);
    spv_branch(&c->b, body);
    spv_label(&c->b, body);
    uint32_t first = spv_op2(&c->b, SpvOpGroupNonUniformBroadcastFirst, type, subgroup_scope(c), value);
    uint32_t same = same_bits(c, type, value, first);
    uint32_t mask = spv_op2(&c->b, SpvOpGroupNonUniformBallot, c->t_uvec[4], subgroup_scope(c), same);
    spv_branch_conditional(&c->b, same, merge, cont);
    spv_label(&c->b, cont);
    spv_branch(&c->b, header);
    spv_label(&c->b, merge);
    uint32_t parts[4];
    for (uint32_t i = 0; i < 4; i++)
        parts[i] = dxil_extract(c, c->t_u32, mask, i);
    dxil_set_parts(c, ins->value, parts, 4);
}

/* the lanes of a multi prefix operation's mask below this one. */
static uint32_t lanes_before(Compiler *c, const DxInst *ins)
{
    uint32_t words[4];
    for (uint32_t i = 0; i < 4; i++)
        words[i] = arg(c, ins, 1 + i);
    uint32_t mask = dxil_vector(c, c->t_u32, words, 4);
    uint32_t below = spv_load(&c->b, c->t_uvec[4], dxil_builtin(c, SpvBuiltInSubgroupLtMask, SpvStorageClassInput,
                                                                  c->t_uvec[4]));
    return spv_op2(&c->b, SpvOpBitwiseAnd, c->t_uvec[4], mask, below);
}

static uint32_t multi_prefix_count(Compiler *c, const DxInst *ins)
{
    subgroup(c, SpvCapabilityGroupNonUniformBallot);
    uint32_t lanes = lanes_before(c, ins);
    uint32_t v = spv_op2(&c->b, SpvOpGroupNonUniformBallot, c->t_uvec[4], subgroup_scope(c), arg(c, ins, 0));
    uint32_t w[3] = {subgroup_scope(c), SpvGroupOperationReduce, spv_op2(&c->b, SpvOpBitwiseAnd, c->t_uvec[4], v, lanes)};
    return spv_op(&c->b, SpvOpGroupNonUniformBallotBitCount, c->t_u32, w, 3);
}

/*
 * waveMultiPrefixOp: the operation over the lanes of the mask below this
 * one, a loop over every lane of the wave, each shuffled to all, those of
 * the mask below this one taken. its operations, read from dxc's output:
 * 0 sum, 1 and, 2 or, 3 xor, 4 product.
 */
static uint32_t multi_prefix(Compiler *c, const DxInst *ins)
{
    subgroup(c, SpvCapabilityGroupNonUniformShuffle);
    subgroup(c, SpvCapabilityGroupNonUniformBallot);
    uint32_t value = arg(c, ins, 0), type = dxil_type(c, ins->type);
    uint32_t kind = (uint32_t)dxil_constant(c, ins->ops[7], 0);
    int is_float = type == c->t_f32 || type == c->t_f16 || type == c->t_f64;
    uint32_t op, identity;
    switch (kind) {
    case 0: op = is_float ? SpvOpFAdd : SpvOpIAdd, identity = splat(c, ins->type, 0); break;
    case 1: op = SpvOpBitwiseAnd, identity = splat(c, ins->type, ~0ull); break;
    case 2: op = SpvOpBitwiseOr, identity = splat(c, ins->type, 0); break;
    case 3: op = SpvOpBitwiseXor, identity = splat(c, ins->type, 0); break;
    default: {
        op = is_float ? SpvOpFMul : SpvOpIMul;
        uint64_t one = type == c->t_f32 ? 0x3F800000 : type == c->t_f16 ? 0x3C00 : type == c->t_f64 ? 0x3FF0000000000000ull : 1;
        identity = splat(c, ins->type, one);
        break;
    }
    }
    uint32_t lanes = lanes_before(c, ins);
    uint32_t size = spv_load(&c->b, c->t_u32, dxil_builtin(c, SpvBuiltInSubgroupSize, SpvStorageClassInput, c->t_u32));
    uint32_t before = c->b.block;
    uint32_t header = spv_id(&c->b), body = spv_id(&c->b), cont = spv_id(&c->b), merge = spv_id(&c->b);
    uint32_t next = spv_id(&c->b), taken = spv_id(&c->b), lane = spv_id(&c->b), acc = spv_id(&c->b);
    spv_branch(&c->b, header);
    spv_label(&c->b, header);
    uint32_t phi_lane[6] = {c->t_u32, lane, dxil_uconst(c, 0), before, next, cont};
    spv_emit(&c->b, SECTION_FUNCTIONS, SpvOpPhi, phi_lane, 6);
    uint32_t phi_acc[6] = {type, acc, identity, before, taken, cont};
    spv_emit(&c->b, SECTION_FUNCTIONS, SpvOpPhi, phi_acc, 6);
    uint32_t more = spv_op2(&c->b, SpvOpULessThan, c->t_bool, lane, size);
    uint32_t loop[3] = {merge, cont, SpvLoopControlMaskNone};
    spv_op_void(&c->b, SpvOpLoopMerge, loop, 3);
    spv_branch_conditional(&c->b, more, body, merge);
    spv_label(&c->b, body);
    uint32_t v = spv_op3(&c->b, SpvOpGroupNonUniformShuffle, type, subgroup_scope(c), value, lane);
    uint32_t word = spv_op2(&c->b, SpvOpVectorExtractDynamic, c->t_u32, lanes,
                            spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, lane, dxil_uconst(c, 5)));
    uint32_t bit = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32,
                           spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, word,
                                   spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, lane, dxil_uconst(c, 31))),
                           dxil_uconst(c, 1));
    uint32_t take = spv_op2(&c->b, SpvOpINotEqual, c->t_bool, bit, dxil_uconst(c, 0));
    uint32_t combined = spv_op2(&c->b, op, type, acc, v);
    uint32_t select[5] = {type, taken, take, combined, acc};
    spv_emit(&c->b, SECTION_FUNCTIONS, SpvOpSelect, select, 5);
    spv_branch(&c->b, cont);
    spv_label(&c->b, cont);
    uint32_t add[4] = {c->t_u32, next, lane, dxil_uconst(c, 1)};
    spv_emit(&c->b, SECTION_FUNCTIONS, SpvOpIAdd, add, 4);
    spv_branch(&c->b, header);
    spv_label(&c->b, merge);
    return acc;
}

/* quadVote: any or all of the quad's lanes. */
static uint32_t quad_vote(Compiler *c, const DxInst *ins)
{
    uint32_t v = arg(c, ins, 0), all = dxil_constant(c, ins->ops[3], 0) & 1;
    uint32_t op = all ? SpvOpLogicalAnd : SpvOpLogicalOr;
    uint32_t x = spv_op2(&c->b, op, c->t_bool, v, quad_swap(c, c->t_bool, v, 0));
    return spv_op2(&c->b, op, c->t_bool, x, quad_swap(c, c->t_bool, x, 1));
}

int dxil_math_op(Compiler *c, const DxInst *ins, uint32_t op)
{
    uint32_t type = dxil_type(c, ins->type), r;
    switch (op) {
    case 6: r = glsl1(c, ins, GLSLstd450FAbs); break;
    case 7: {
        uint32_t a[3] = {arg(c, ins, 0), splat(c, ins->type, 0), splat(c, ins->type, float_bits(c, ins->type, 1.0))};
        r = spv_glsl(&c->b, type, GLSLstd450NClamp, a, 3);
        break;
    }
    case 8:
    case 9:
    case 10:
    case 11: r = is_special(c, ins, op); break;
    case 12: r = glsl1(c, ins, GLSLstd450Cos); break;
    case 13: r = glsl1(c, ins, GLSLstd450Sin); break;
    case 14: r = glsl1(c, ins, GLSLstd450Tan); break;
    case 15: r = glsl1(c, ins, GLSLstd450Acos); break;
    case 16: r = glsl1(c, ins, GLSLstd450Asin); break;
    case 17: r = glsl1(c, ins, GLSLstd450Atan); break;
    case 18: r = glsl1(c, ins, GLSLstd450Cosh); break;
    case 19: r = glsl1(c, ins, GLSLstd450Sinh); break;
    case 20: r = glsl1(c, ins, GLSLstd450Tanh); break;
    case 21: r = glsl1(c, ins, GLSLstd450Exp2); break;
    case 22: r = glsl1(c, ins, GLSLstd450Fract); break;
    case 23: r = glsl1(c, ins, GLSLstd450Log2); break;
    case 24: r = glsl1(c, ins, GLSLstd450Sqrt); break;
    case 25: r = glsl1(c, ins, GLSLstd450InverseSqrt); break;
    case 26: r = glsl1(c, ins, GLSLstd450RoundEven); break;
    case 27: r = glsl1(c, ins, GLSLstd450Floor); break;
    case 28: r = glsl1(c, ins, GLSLstd450Ceil); break;
    case 29: r = glsl1(c, ins, GLSLstd450Trunc); break;
    case 30: r = spv_op1(&c->b, SpvOpBitReverse, type, arg(c, ins, 0)); break;
    case 31: {
        /* the count is an i32 whatever the operand's width. */
        uint32_t at = dxil_type(c, arg_type(c, ins, 0));
        if (at == c->t_u64) {
            uint32_t lo, hi;
            split64(c, arg(c, ins, 0), &lo, &hi);
            r = spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op1(&c->b, SpvOpBitCount, c->t_u32, lo),
                        spv_op1(&c->b, SpvOpBitCount, c->t_u32, hi));
            at = c->t_u32;
        } else {
            r = spv_op1(&c->b, SpvOpBitCount, at, arg(c, ins, 0));
        }
        r = dxil_convert(c, r, at, type);
        break;
    }
    case 32: {
        uint32_t a = arg(c, ins, 0), at = dxil_type(c, arg_type(c, ins, 0));
        r = at == c->t_u64 ? find64(c, a, GLSLstd450FindILsb) : spv_glsl(&c->b, at, GLSLstd450FindILsb, &a, 1);
        r = dxil_convert(c, r, at == c->t_u64 ? c->t_u32 : at, type);
        break;
    }
    case 33: r = first_bit_high(c, ins, 0); break;
    case 34: r = first_bit_high(c, ins, 1); break;
    case 35: r = glsl2(c, ins, GLSLstd450NMax); break;
    case 36: r = glsl2(c, ins, GLSLstd450NMin); break;
    case 37: r = glsl2(c, ins, GLSLstd450SMax); break;
    case 38: r = glsl2(c, ins, GLSLstd450SMin); break;
    case 39: r = glsl2(c, ins, GLSLstd450UMax); break;
    case 40: r = glsl2(c, ins, GLSLstd450UMin); break;
    case 41:
    case 42: {
        /* imul and umul: the high and low words of the full product. */
        uint32_t pair = spv_type_struct(&c->b, (uint32_t[]){c->t_u32, c->t_u32}, 2);
        uint32_t m = spv_op2(&c->b, op == 41 ? SpvOpSMulExtended : SpvOpUMulExtended, pair, arg(c, ins, 0), arg(c, ins, 1));
        uint32_t parts[2] = {dxil_extract(c, c->t_u32, m, 1), dxil_extract(c, c->t_u32, m, 0)};
        dxil_set_parts(c, ins->value, parts, 2);
        return 1;
    }
    case 43: {
        uint32_t a = arg(c, ins, 0), b = arg(c, ins, 1);
        uint32_t parts[2] = {spv_op2(&c->b, SpvOpUDiv, c->t_u32, a, b), spv_op2(&c->b, SpvOpUMod, c->t_u32, a, b)};
        dxil_set_parts(c, ins->value, parts, 2);
        return 1;
    }
    case 44:
    case 45: add_with_carry(c, ins, op == 45); return 1;
    case 46:
        r = spv_op2(&c->b, SpvOpFAdd, type, spv_op2(&c->b, SpvOpFMul, type, arg(c, ins, 0), arg(c, ins, 1)), arg(c, ins, 2));
        break;
    case 47: {
        uint32_t a[3] = {arg(c, ins, 0), arg(c, ins, 1), arg(c, ins, 2)};
        r = spv_glsl(&c->b, type, GLSLstd450Fma, a, 3);
        break;
    }
    case 48:
    case 49:
        r = spv_op2(&c->b, SpvOpIAdd, type, spv_op2(&c->b, SpvOpIMul, type, arg(c, ins, 0), arg(c, ins, 1)), arg(c, ins, 2));
        break;
    case 50: r = msad(c, ins); break;
    case 51:
    case 52: {
        uint32_t width = arg(c, ins, 0), offset = arg(c, ins, 1);
        bit_field(c, &width, &offset);
        r = spv_op3(&c->b, op == 51 ? SpvOpBitFieldSExtract : SpvOpBitFieldUExtract, type, arg(c, ins, 2), offset, width);
        break;
    }
    case 53: {
        uint32_t width = arg(c, ins, 0), offset = arg(c, ins, 1);
        bit_field(c, &width, &offset);
        uint32_t w[4] = {arg(c, ins, 3), arg(c, ins, 2), offset, width};
        r = spv_op(&c->b, SpvOpBitFieldInsert, type, w, 4);
        break;
    }
    case 54: r = dot(c, ins, 2); break;
    case 55: r = dot(c, ins, 3); break;
    case 56: r = dot(c, ins, 4); break;
    case 83:
    case 84:
    case 85:
    case 86: {
        static const uint32_t DERIVATIVES[4] = {SpvOpDPdxCoarse, SpvOpDPdyCoarse, SpvOpDPdxFine, SpvOpDPdyFine};
        if (!dxil_derivatives(c)) {
            r = spv_const_null(&c->b, type);
            break;
        }
        spv_capability(&c->b, SpvCapabilityDerivativeControl);
        r = spv_op1(&c->b, DERIVATIVES[op - 83], type, arg(c, ins, 0));
        break;
    }
    case 101: {
        uint32_t parts[2] = {arg(c, ins, 0), arg(c, ins, 1)};
        r = spv_op1(&c->b, SpvOpBitcast, type, dxil_vector(c, c->t_u32, parts, 2));
        break;
    }
    case 102: {
        uint32_t pair = spv_op1(&c->b, SpvOpBitcast, c->t_uvec[2], arg(c, ins, 0));
        uint32_t parts[2] = {dxil_extract(c, c->t_u32, pair, 0), dxil_extract(c, c->t_u32, pair, 1)};
        dxil_set_parts(c, ins->value, parts, 2);
        return 1;
    }
    case 109: {
        uint32_t parts[2] = {dxil_uconst(c, 0), dxil_uconst(c, 0)};
        dxil_set_parts(c, ins->value, parts, 2);
        return 1;
    }
    case 124:
    case 125:
    case 126:
    case 127:
    case 128:
    case 129: {
        uint32_t from = dxil_type(c, arg_type(c, ins, 0));
        r = from == type ? arg(c, ins, 0) : dxil_convert(c, arg(c, ins, 0), from, type);
        break;
    }
    case 130: r = dxil_convert(c, f32_to_f16(c, arg(c, ins, 0)), c->t_u32, type); break;
    case 131: r = dxil_convert(c, f16_to_f32(c, dxil_convert(c, arg(c, ins, 0), dxil_type(c, arg_type(c, ins, 0)), c->t_u32)), c->t_f32, type); break;
    case 132: r = spv_op1(&c->b, SpvOpFConvert, type, arg(c, ins, 0)); break;
    case 133: r = spv_op1(&c->b, SpvOpConvertFToS, type, arg(c, ins, 0)); break;
    case 134: r = spv_op1(&c->b, SpvOpConvertFToU, type, arg(c, ins, 0)); break;
    case 162: {
        /* dot2AddHalf: two halves each, in float, added to a float. */
        uint32_t a[2], b[2];
        for (uint32_t i = 0; i < 2; i++) {
            a[i] = dxil_convert(c, arg(c, ins, 1 + i), dxil_type(c, arg_type(c, ins, 1 + i)), c->t_f32);
            b[i] = dxil_convert(c, arg(c, ins, 3 + i), dxil_type(c, arg_type(c, ins, 3 + i)), c->t_f32);
        }
        uint32_t d = spv_op2(&c->b, SpvOpDot, c->t_f32, dxil_vector(c, c->t_f32, a, 2), dxil_vector(c, c->t_f32, b, 2));
        r = spv_op2(&c->b, SpvOpFAdd, c->t_f32, arg(c, ins, 0), d);
        break;
    }
    case 219: unpack4x8(c, ins); return 1;
    case 220: r = pack4x8(c, ins); break;
    case 163: r = dot4_packed(c, ins, 1); break;
    case 164: r = dot4_packed(c, ins, 0); break;
    /* waves. */
    case 110:
        subgroup(c, 0);
        r = spv_op1(&c->b, SpvOpGroupNonUniformElect, c->t_bool, subgroup_scope(c));
        break;
    case 111:
    case 112: {
        subgroup(c, 0);
        uint32_t which = op == 111 ? SpvBuiltInSubgroupLocalInvocationId : SpvBuiltInSubgroupSize;
        r = spv_load(&c->b, c->t_u32, dxil_builtin(c, which, SpvStorageClassInput, c->t_u32));
        break;
    }
    case 113:
    case 114:
    case 115:
        subgroup(c, SpvCapabilityGroupNonUniformVote);
        r = spv_op2(&c->b, op == 113 ? SpvOpGroupNonUniformAny : op == 114 ? SpvOpGroupNonUniformAll : SpvOpGroupNonUniformAllEqual,
                    c->t_bool, subgroup_scope(c), arg(c, ins, 0));
        break;
    case 116: ballot(c, ins); return 1;
    case 117: {
        subgroup(c, SpvCapabilityGroupNonUniformShuffle);
        r = spv_op3(&c->b, SpvOpGroupNonUniformShuffle, type, subgroup_scope(c), arg(c, ins, 0), arg(c, ins, 1));
        break;
    }
    case 118:
        subgroup(c, SpvCapabilityGroupNonUniformBallot);
        r = spv_op2(&c->b, SpvOpGroupNonUniformBroadcastFirst, type, subgroup_scope(c), arg(c, ins, 0));
        break;
    case 119: r = wave_reduce(c, ins, 0); break;
    case 120: r = wave_bits(c, ins); break;
    case 121: r = wave_reduce(c, ins, 1); break;
    case 122:
        subgroup(c, SpvCapabilityGroupNonUniformQuad);
        r = spv_op3(&c->b, SpvOpGroupNonUniformQuadBroadcast, type, subgroup_scope(c), arg(c, ins, 0), arg(c, ins, 1));
        break;
    case 123: r = quad_swap(c, type, arg(c, ins, 0), (uint32_t)dxil_constant(c, ins->ops[3], 0)); break;
    case 135: r = bit_count(c, ins, SpvGroupOperationReduce); break;
    case 136: r = bit_count(c, ins, SpvGroupOperationExclusiveScan); break;
    case 222: r = quad_vote(c, ins); break;
    case 165: wave_match(c, ins); return c->error ? -1 : 1;
    case 166: r = multi_prefix(c, ins); break;
    case 167: r = multi_prefix_count(c, ins); break;
    default: return 0;
    }
    if (c->error)
        return -1;
    dxil_set_plain(c, ins->value, r);
    return 1;
}
