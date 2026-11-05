#include "parse.h"

static const struct {
    const char *name;
    uint32_t id;
} intrinsics[] = {
    {"abs", I_ABS}, {"acos", I_ACOS}, {"all", I_ALL}, {"any", I_ANY}, {"asdouble", I_ASDOUBLE},
    {"asfloat", I_ASFLOAT}, {"asin", I_ASIN}, {"asint", I_ASINT}, {"asuint", I_ASUINT}, {"atan", I_ATAN},
    {"atan2", I_ATAN2}, {"ceil", I_CEIL}, {"clamp", I_CLAMP}, {"clip", I_CLIP}, {"cos", I_COS}, {"cosh", I_COSH},
    {"countbits", I_COUNTBITS}, {"cross", I_CROSS}, {"ddx", I_DDX}, {"ddx_coarse", I_DDX_COARSE},
    {"ddx_fine", I_DDX_FINE}, {"ddy", I_DDY}, {"ddy_coarse", I_DDY_COARSE}, {"ddy_fine", I_DDY_FINE},
    {"degrees", I_DEGREES}, {"determinant", I_DETERMINANT}, {"distance", I_DISTANCE}, {"dot", I_DOT},
    {"dst", I_DST}, {"EvaluateAttributeAtCentroid", I_EVAL_CENTROID}, {"EvaluateAttributeAtSample", I_EVAL_SAMPLE},
    {"EvaluateAttributeSnapped", I_EVAL_SNAPPED}, {"exp", I_EXP}, {"exp2", I_EXP2}, {"f16tof32", I_F16TOF32},
    {"f32tof16", I_F32TOF16}, {"faceforward", I_FACEFORWARD}, {"firstbithigh", I_FIRSTBITHIGH},
    {"firstbitlow", I_FIRSTBITLOW}, {"floor", I_FLOOR}, {"fma", I_FMA}, {"fmod", I_FMOD}, {"frac", I_FRAC},
    {"frexp", I_FREXP}, {"fwidth", I_FWIDTH}, {"GetRenderTargetSampleCount", I_RT_SAMPLE_COUNT},
    {"GetRenderTargetSamplePosition", I_RT_SAMPLE_POSITION}, {"GroupMemoryBarrier", I_GROUP_BARRIER},
    {"GroupMemoryBarrierWithGroupSync", I_GROUP_BARRIER_SYNC}, {"DeviceMemoryBarrier", I_DEVICE_BARRIER},
    {"DeviceMemoryBarrierWithGroupSync", I_DEVICE_BARRIER_SYNC}, {"AllMemoryBarrier", I_ALL_BARRIER},
    {"AllMemoryBarrierWithGroupSync", I_ALL_BARRIER_SYNC}, {"InterlockedAdd", I_INTERLOCKED_ADD},
    {"InterlockedAnd", I_INTERLOCKED_AND}, {"InterlockedOr", I_INTERLOCKED_OR}, {"InterlockedXor", I_INTERLOCKED_XOR},
    {"InterlockedMin", I_INTERLOCKED_MIN}, {"InterlockedMax", I_INTERLOCKED_MAX},
    {"InterlockedExchange", I_INTERLOCKED_EXCHANGE}, {"InterlockedCompareExchange", I_INTERLOCKED_COMPARE_EXCHANGE},
    {"InterlockedCompareStore", I_INTERLOCKED_COMPARE_STORE}, {"isfinite", I_ISFINITE}, {"isinf", I_ISINF},
    {"isnan", I_ISNAN}, {"ldexp", I_LDEXP}, {"length", I_LENGTH}, {"lerp", I_LERP}, {"lit", I_LIT}, {"log", I_LOG},
    {"log10", I_LOG10}, {"log2", I_LOG2}, {"mad", I_MAD}, {"max", I_MAX}, {"min", I_MIN}, {"modf", I_MODF},
    {"msad4", I_MSAD4}, {"mul", I_MUL}, {"noise", I_NOISE}, {"normalize", I_NORMALIZE}, {"pow", I_POW},
    {"radians", I_RADIANS}, {"rcp", I_RCP}, {"reflect", I_REFLECT}, {"refract", I_REFRACT},
    {"reversebits", I_REVERSEBITS}, {"round", I_ROUND}, {"rsqrt", I_RSQRT}, {"saturate", I_SATURATE},
    {"sign", I_SIGN}, {"sin", I_SIN}, {"sincos", I_SINCOS}, {"sinh", I_SINH}, {"smoothstep", I_SMOOTHSTEP},
    {"sqrt", I_SQRT}, {"step", I_STEP}, {"tan", I_TAN}, {"tanh", I_TANH}, {"transpose", I_TRANSPOSE},
    {"trunc", I_TRUNC}, {"tex1D", I_TEX1D}, {"tex1Dbias", I_TEX1DBIAS}, {"tex1Dgrad", I_TEX1DGRAD},
    {"tex1Dlod", I_TEX1DLOD}, {"tex1Dproj", I_TEX1DPROJ}, {"tex2D", I_TEX2D}, {"tex2Dbias", I_TEX2DBIAS},
    {"tex2Dgrad", I_TEX2DGRAD}, {"tex2Dlod", I_TEX2DLOD}, {"tex2Dproj", I_TEX2DPROJ}, {"tex3D", I_TEX3D},
    {"tex3Dbias", I_TEX3DBIAS}, {"tex3Dgrad", I_TEX3DGRAD}, {"tex3Dlod", I_TEX3DLOD}, {"tex3Dproj", I_TEX3DPROJ},
    {"texCUBE", I_TEXCUBE}, {"texCUBEbias", I_TEXCUBEBIAS}, {"texCUBEgrad", I_TEXCUBEGRAD},
    {"texCUBElod", I_TEXCUBELOD}, {"texCUBEproj", I_TEXCUBEPROJ}, {"abort", I_ABORT}, {"errorf", I_ERRORF},
    {"printf", I_PRINTF}, {"CheckAccessFullyMapped", I_CHECK_ACCESS}, {"D3DCOLORtoUBYTE4", I_D3DCOLORTOUBYTE4},
    {"ProcessTriTessFactorsMax", I_TESS_TRI_EDGE}, {"ProcessTriTessFactorsAvg", I_TESS_TRI_AVG},
    {"ProcessTriTessFactorsMin", I_TESS_TRI_SINGLE}, {"ProcessQuadTessFactorsMax", I_TESS_QUAD_EDGE},
    {"ProcessQuadTessFactorsAvg", I_TESS_QUAD_AVG}, {"ProcessQuadTessFactorsMin", I_TESS_QUAD_SINGLE},
    {"Process2DQuadTessFactorsMax", I_TESS_QUAD_2D_EDGE}, {"Process2DQuadTessFactorsAvg", I_TESS_QUAD_2D_AVG},
    {"Process2DQuadTessFactorsMin", I_TESS_QUAD_2D_SINGLE}, {"ProcessIsolineTessFactors", I_TESS_ISOLINE},
};

uint32_t intrinsic_lookup(Parser *p, const char *name)
{
    for (size_t i = 0; i < HL_ARRAY_SIZE(intrinsics); i++)
        if (hl_streq(intrinsics[i].name, name))
            return intrinsics[i].id;
    return I_NONE;
}

static HlExpr *intrinsic_expr(Parser *p, uint32_t id, HlType *type, HlExpr **args, uint32_t count, HlLoc loc)
{
    HlExpr *e = new_expr(p, E_INTRINSIC, type, loc);
    e->intrinsic = id;
    e->args = args;
    e->arg_count = count;
    return e;
}

static bool numeric(const HlExpr *e) { return hl_type_numeric_p(e->type); }

/* a float type of the shape: half and double and the minimum precisions stay. */
static HlType *float_of(Parser *p, const HlType *t)
{
    uint8_t base = t->base;
    if (!hl_base_float(base) || base == BT_LIT_FLOAT)
        base = BT_FLOAT;
    return hl_type_with_base(p->h, t, base);
}

/* the common type of numeric arguments, as binary operators combine them. */
static HlType *common(Parser *p, HlExpr **args, uint32_t count, HlLoc loc)
{
    HlType *t = args[0]->type;
    for (uint32_t i = 1; i < count; i++) {
        t = arith_type(p, t, args[i]->type, loc, true);
        if (t->kind == TY_ERROR)
            return t;
    }
    if (t->base == BT_LIT_INT)
        t = hl_type_with_base(p->h, t, BT_INT);
    else if (t->base == BT_LIT_FLOAT)
        t = hl_type_with_base(p->h, t, BT_FLOAT);
    return t;
}

static void convert_all(Parser *p, HlExpr **args, uint32_t count, HlType *t, HlLoc loc)
{
    for (uint32_t i = 0; i < count; i++)
        args[i] = convert(p, args[i], t, loc, false);
}

static HlExpr *no_match(Parser *p, const char *name, uint32_t count, HlLoc loc)
{
    hl_error(p->h, loc, 3013, "'%s': no matching %u parameter intrinsic function", name, count);
    return error_expr(p, loc);
}

static bool check_out(Parser *p, HlExpr *e)
{
    if (!e->lvalue) {
        hl_error(p->h, e->loc, 3025, "out parameters require l-value arguments");
        return false;
    }
    mark_assigned(p, e);
    return true;
}

/* the destination of an interlocked operation: a scalar int of groupshared memory or a read-write view. */
static bool interlocked_dest(Parser *p, HlExpr *e)
{
    if (!check_out(p, e))
        return false;
    if (!numeric(e) || e->type->kind != TY_SCALAR || !hl_base_int(e->type->base)) {
        hl_error(p->h, e->loc, 3013, "interlocked targets must be int or uint");
        return false;
    }
    return true;
}

static HlExpr *legacy_tex(Parser *p, uint32_t id, HlExpr **args, uint32_t count, HlLoc loc, const char *name)
{
    Hlsl *h = p->h;
    uint32_t base = id >= I_TEXCUBE ? I_TEXCUBE : id >= I_TEX3D ? I_TEX3D : id >= I_TEX2D ? I_TEX2D : I_TEX1D;
    uint32_t variant = id - base;
    uint8_t dims = base == I_TEX1D ? 1 : base == I_TEX2D ? 2 : 3;
    if (count < 2 || args[0]->type->kind != TY_OBJECT || !hl_object_is_sampler(args[0]->type->object))
        return no_match(p, name, count, loc);
    HlType *coord;
    if (variant == 0 && (count == 2 || count == 4)) {
        coord = dims == 1 ? hl_type_scalar(h, BT_FLOAT) : hl_type_vector(h, BT_FLOAT, dims);
        args[1] = convert(p, args[1], coord, loc, false);
        if (count == 4) {
            args[2] = convert(p, args[2], coord, loc, false);
            args[3] = convert(p, args[3], coord, loc, false);
        }
    } else if (variant == 2 && count == 4) {
        coord = dims == 1 ? hl_type_scalar(h, BT_FLOAT) : hl_type_vector(h, BT_FLOAT, dims);
        convert_all(p, args + 1, 3, coord, loc);
    } else if (count == 2) {
        args[1] = convert(p, args[1], hl_type_vector(h, BT_FLOAT, 4), loc, false);
    } else {
        return no_match(p, name, count, loc);
    }
    return intrinsic_expr(p, id, hl_type_vector(h, BT_FLOAT, 4), args, count, loc);
}

HlExpr *check_intrinsic(Parser *p, uint32_t id, HlExpr **args, uint32_t count, HlLoc loc, const char *name)
{
    Hlsl *h = p->h;
    for (uint32_t i = 0; i < count; i++)
        if (args[i]->kind == E_ERROR || args[i]->type->kind == TY_ERROR)
            return error_expr(p, loc);
    switch (id) {
    case I_ACOS: case I_ASIN: case I_ATAN: case I_CEIL: case I_COS: case I_COSH: case I_DDX: case I_DDX_COARSE:
    case I_DDX_FINE: case I_DDY: case I_DDY_COARSE: case I_DDY_FINE: case I_DEGREES: case I_EXP: case I_EXP2:
    case I_FLOOR: case I_FRAC: case I_FWIDTH: case I_LOG: case I_LOG10: case I_LOG2: case I_RADIANS: case I_ROUND:
    case I_RSQRT: case I_SIN: case I_SINH: case I_SQRT: case I_TAN: case I_TANH: case I_TRUNC: case I_SATURATE:
    case I_RCP: case I_EVAL_CENTROID: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, args[0]->type);
        if (id == I_RCP && args[0]->type->base == BT_DOUBLE)
            t = args[0]->type;
        args[0] = convert(p, args[0], t, loc, false);
        return fold(p, intrinsic_expr(p, id, t, args, count, loc));
    }
    case I_ABS: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 1, loc);
        if (t->base == BT_BOOL)
            t = hl_type_with_base(h, t, BT_INT);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_SIGN: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 1, loc);
        if (t->base == BT_BOOL)
            t = hl_type_with_base(h, t, BT_INT);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, hl_type_with_base(h, t, BT_INT), args, count, loc);
    }
    case I_ATAN2: case I_FMOD: case I_POW: case I_STEP: case I_LDEXP: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 2, loc);
        if (t->kind == TY_ERROR)
            return no_match(p, name, count, loc);
        t = float_of(p, t);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_MIN: case I_MAX: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 2, loc);
        if (t->kind == TY_ERROR)
            return no_match(p, name, count, loc);
        if (t->base == BT_BOOL)
            t = hl_type_with_base(h, t, BT_INT);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_CLAMP: case I_MAD: case I_LERP: case I_SMOOTHSTEP: case I_FMA: {
        if (count != 3 || !numeric(args[0]) || !numeric(args[1]) || !numeric(args[2]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 3, loc);
        if (t->kind == TY_ERROR)
            return no_match(p, name, count, loc);
        if (id == I_LERP || id == I_SMOOTHSTEP)
            t = float_of(p, t);
        if (id == I_FMA)
            t = hl_type_with_base(h, t, BT_DOUBLE);
        if (t->base == BT_BOOL)
            t = hl_type_with_base(h, t, BT_INT);
        convert_all(p, args, 3, t, loc);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_ALL: case I_ANY: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        return intrinsic_expr(p, id, hl_type_scalar(h, BT_BOOL), args, count, loc);
    }
    case I_ISFINITE: case I_ISINF: case I_ISNAN: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, args[0]->type);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, hl_type_with_base(h, t, BT_BOOL), args, count, loc);
    }
    case I_DOT: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]) || args[0]->type->kind == TY_MATRIX ||
            args[1]->type->kind == TY_MATRIX)
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 2, loc);
        if (t->kind == TY_ERROR)
            return no_match(p, name, count, loc);
        if (t->base == BT_BOOL)
            t = hl_type_with_base(h, t, BT_INT);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, hl_type_scalar(h, t->base), args, count, loc);
    }
    case I_CROSS: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *t = hl_type_vector(h, BT_FLOAT, 3);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_LENGTH: case I_NORMALIZE: {
        if (count != 1 || !numeric(args[0]) || args[0]->type->kind == TY_MATRIX)
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, args[0]->type);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, id == I_LENGTH ? hl_type_scalar(h, t->base) : t, args, count, loc);
    }
    case I_DISTANCE: case I_REFLECT: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 2, loc);
        if (t->kind == TY_ERROR || t->kind == TY_MATRIX)
            return no_match(p, name, count, loc);
        t = float_of(p, t);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, id == I_DISTANCE ? hl_type_scalar(h, t->base) : t, args, count, loc);
    }
    case I_REFRACT: {
        if (count != 3 || !numeric(args[0]) || !numeric(args[1]) || !numeric(args[2]))
            return no_match(p, name, count, loc);
        HlType *t = common(p, args, 2, loc);
        if (t->kind == TY_ERROR || t->kind == TY_MATRIX)
            return no_match(p, name, count, loc);
        t = float_of(p, t);
        convert_all(p, args, 2, t, loc);
        args[2] = convert(p, args[2], hl_type_scalar(h, t->base), loc, false);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_FACEFORWARD: {
        if (count != 3 || !numeric(args[0]) || !numeric(args[1]) || !numeric(args[2]))
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, common(p, args, 3, loc));
        if (t->kind == TY_ERROR)
            return no_match(p, name, count, loc);
        convert_all(p, args, 3, t, loc);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_MUL: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *a = args[0]->type, *b = args[1]->type;
        uint8_t base = common(p, args, 0 + 1, loc)->base;
        {
            HlType *both = arith_type(p, hl_type_scalar(h, a->base), hl_type_scalar(h, b->base), loc, false);
            base = both->base == BT_LIT_INT ? BT_INT : both->base == BT_LIT_FLOAT ? BT_FLOAT : both->base;
            if (base == BT_BOOL)
                base = BT_INT;
        }
        HlType *ta, *tb, *result;
        if (a->kind == TY_SCALAR || b->kind == TY_SCALAR) {
            result = hl_type_with_base(h, a->kind == TY_SCALAR ? b : a, base);
            ta = a->kind == TY_SCALAR ? hl_type_scalar(h, base) : result;
            tb = b->kind == TY_SCALAR ? hl_type_scalar(h, base) : result;
        } else if (a->kind == TY_VECTOR && b->kind == TY_VECTOR) {
            uint8_t n = a->cols < b->cols ? a->cols : b->cols;
            if (a->cols != b->cols)
                hl_warning(h, loc, 3206, "implicit truncation of vector type");
            ta = tb = hl_type_vector(h, base, n);
            result = hl_type_scalar(h, base);
        } else if (a->kind == TY_VECTOR) {
            /* a row vector times a matrix. */
            uint8_t n = b->rows;
            if (a->cols < n) {
                if (b->rows == 1 && 0)
                    ;
                hl_error(h, loc, 3020, "type mismatch: mul(%s, %s)", hl_type_name(h, a), hl_type_name(h, b));
                return error_expr(p, loc);
            }
            if (a->cols > n)
                hl_warning(h, loc, 3206, "implicit truncation of vector type");
            ta = hl_type_vector(h, base, n);
            tb = hl_type_with_base(h, b, base);
            result = b->cols == 1 ? hl_type_scalar(h, base) : hl_type_vector(h, base, b->cols);
        } else if (b->kind == TY_VECTOR) {
            uint8_t n = a->cols;
            if (b->cols < n) {
                hl_error(h, loc, 3020, "type mismatch: mul(%s, %s)", hl_type_name(h, a), hl_type_name(h, b));
                return error_expr(p, loc);
            }
            if (b->cols > n)
                hl_warning(h, loc, 3206, "implicit truncation of vector type");
            ta = hl_type_with_base(h, a, base);
            tb = hl_type_vector(h, base, n);
            result = a->rows == 1 ? hl_type_scalar(h, base) : hl_type_vector(h, base, a->rows);
        } else {
            if (a->cols != b->rows) {
                uint8_t n = a->cols < b->rows ? a->cols : b->rows;
                hl_warning(h, loc, 3206, "implicit truncation of vector type");
                ta = hl_type_matrix(h, base, a->rows, n);
                tb = hl_type_matrix(h, base, n, b->cols);
            } else {
                ta = hl_type_with_base(h, a, base);
                tb = hl_type_with_base(h, b, base);
            }
            result = hl_type_matrix(h, base, a->rows, b->cols);
        }
        args[0] = convert(p, args[0], ta, loc, false);
        args[1] = convert(p, args[1], tb, loc, false);
        return intrinsic_expr(p, id, result, args, count, loc);
    }
    case I_TRANSPOSE: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = args[0]->type;
        if (t->kind != TY_MATRIX)
            return intrinsic_expr(p, id, t, args, count, loc);
        return intrinsic_expr(p, id, hl_type_matrix(h, t->base, t->cols, t->rows), args, count, loc);
    }
    case I_DETERMINANT: {
        if (count != 1 || !numeric(args[0]) || args[0]->type->kind != TY_MATRIX ||
            args[0]->type->rows != args[0]->type->cols)
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, args[0]->type);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, hl_type_scalar(h, t->base), args, count, loc);
    }
    case I_ASFLOAT: case I_ASINT: case I_ASUINT: {
        if (id == I_ASUINT && count == 3) {
            if (!numeric(args[0]) || args[0]->type->base != BT_DOUBLE || !check_out(p, args[1]) ||
                !check_out(p, args[2]))
                return no_match(p, name, count, loc);
            return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
        }
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = args[0]->type;
        if (t->base == BT_BOOL || t->base == BT_LIT_INT)
            t = hl_type_with_base(h, t, BT_INT);
        else if (t->base == BT_LIT_FLOAT || t->base == BT_HALF || t->base == BT_MIN16FLOAT || t->base == BT_MIN10FLOAT)
            t = hl_type_with_base(h, t, BT_FLOAT);
        else if (t->base == BT_MIN16INT || t->base == BT_MIN12INT)
            t = hl_type_with_base(h, t, BT_INT);
        else if (t->base == BT_MIN16UINT)
            t = hl_type_with_base(h, t, BT_UINT);
        if (t->base == BT_DOUBLE)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], t, loc, false);
        uint8_t to = id == I_ASFLOAT ? BT_FLOAT : id == I_ASINT ? BT_INT : BT_UINT;
        return fold(p, intrinsic_expr(p, id, hl_type_with_base(h, t, to), args, count, loc));
    }
    case I_ASDOUBLE: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        HlType *t = hl_type_with_base(h, common(p, args, 2, loc), BT_UINT);
        convert_all(p, args, 2, t, loc);
        return intrinsic_expr(p, id, hl_type_with_base(h, t, BT_DOUBLE), args, count, loc);
    }
    case I_COUNTBITS: case I_REVERSEBITS: case I_FIRSTBITHIGH: case I_FIRSTBITLOW: {
        if (count != 1 || !numeric(args[0]) || hl_base_float(args[0]->type->base))
            return no_match(p, name, count, loc);
        uint8_t base = hl_base_unsigned(args[0]->type->base) ? BT_UINT : BT_INT;
        if (id == I_COUNTBITS || id == I_REVERSEBITS)
            base = BT_UINT;
        HlType *t = hl_type_with_base(h, args[0]->type, base == BT_INT && id == I_FIRSTBITHIGH ? BT_INT : BT_UINT);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, id == I_FIRSTBITHIGH && base == BT_INT ? hl_type_with_base(h, t, BT_INT) :
                                     hl_type_with_base(h, t, BT_UINT), args, count, loc);
    }
    case I_F16TOF32: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = hl_type_with_base(h, args[0]->type, BT_UINT);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, hl_type_with_base(h, t, BT_FLOAT), args, count, loc);
    }
    case I_F32TOF16: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        HlType *t = hl_type_with_base(h, args[0]->type, BT_FLOAT);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, hl_type_with_base(h, t, BT_UINT), args, count, loc);
    }
    case I_SINCOS: {
        if (count != 3 || !numeric(args[0]) || !check_out(p, args[1]) || !check_out(p, args[2]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], float_of(p, args[0]->type), loc, false);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    }
    case I_MODF: case I_FREXP: {
        if (count != 2 || !numeric(args[0]) || !check_out(p, args[1]))
            return no_match(p, name, count, loc);
        HlType *t = float_of(p, args[0]->type);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_CLIP: {
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], float_of(p, args[0]->type), loc, false);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    }
    case I_LIT: {
        if (count != 3)
            return no_match(p, name, count, loc);
        convert_all(p, args, 3, hl_type_scalar(h, BT_FLOAT), loc);
        return intrinsic_expr(p, id, hl_type_vector(h, BT_FLOAT, 4), args, count, loc);
    }
    case I_DST: {
        if (count != 2)
            return no_match(p, name, count, loc);
        convert_all(p, args, 2, hl_type_vector(h, BT_FLOAT, 4), loc);
        return intrinsic_expr(p, id, hl_type_vector(h, BT_FLOAT, 4), args, count, loc);
    }
    case I_GROUP_BARRIER: case I_GROUP_BARRIER_SYNC: case I_DEVICE_BARRIER: case I_DEVICE_BARRIER_SYNC:
    case I_ALL_BARRIER: case I_ALL_BARRIER_SYNC:
        if (count)
            return no_match(p, name, count, loc);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    case I_INTERLOCKED_ADD: case I_INTERLOCKED_AND: case I_INTERLOCKED_OR: case I_INTERLOCKED_XOR:
    case I_INTERLOCKED_MIN: case I_INTERLOCKED_MAX: case I_INTERLOCKED_EXCHANGE: {
        if ((count != 2 && count != 3) || !interlocked_dest(p, args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        if (id == I_INTERLOCKED_EXCHANGE && count != 3)
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], args[0]->type, loc, false);
        if (count == 3 && !check_out(p, args[2]))
            return no_match(p, name, count, loc);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    }
    case I_INTERLOCKED_COMPARE_EXCHANGE: case I_INTERLOCKED_COMPARE_STORE: {
        uint32_t want = id == I_INTERLOCKED_COMPARE_EXCHANGE ? 4 : 3;
        if (count != want || !interlocked_dest(p, args[0]) || !numeric(args[1]) || !numeric(args[2]))
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], args[0]->type, loc, false);
        args[2] = convert(p, args[2], args[0]->type, loc, false);
        if (count == 4 && !check_out(p, args[3]))
            return no_match(p, name, count, loc);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    }
    case I_RT_SAMPLE_COUNT:
        if (count)
            return no_match(p, name, count, loc);
        return intrinsic_expr(p, id, hl_type_scalar(h, BT_UINT), args, count, loc);
    case I_RT_SAMPLE_POSITION:
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_INT), loc, false);
        return intrinsic_expr(p, id, hl_type_vector(h, BT_FLOAT, 2), args, count, loc);
    case I_EVAL_SAMPLE: case I_EVAL_SNAPPED: {
        if (count != 2 || !numeric(args[0]) || !numeric(args[1]))
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], id == I_EVAL_SAMPLE ? hl_type_scalar(h, BT_UINT) : hl_type_vector(h, BT_INT, 2),
                          loc, false);
        HlType *t = float_of(p, args[0]->type);
        args[0] = convert(p, args[0], t, loc, false);
        return intrinsic_expr(p, id, t, args, count, loc);
    }
    case I_ABORT: case I_ERRORF: case I_PRINTF:
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    case I_CHECK_ACCESS:
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        return intrinsic_expr(p, id, hl_type_scalar(h, BT_BOOL), args, count, loc);
    case I_D3DCOLORTOUBYTE4:
        if (count != 1 || !numeric(args[0]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_vector(h, BT_FLOAT, 4), loc, false);
        return intrinsic_expr(p, id, hl_type_vector(h, BT_INT, 4), args, count, loc);
    case I_MSAD4:
        if (count != 3)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        args[1] = convert(p, args[1], hl_type_vector(h, BT_UINT, 2), loc, false);
        args[2] = convert(p, args[2], hl_type_vector(h, BT_UINT, 4), loc, false);
        return intrinsic_expr(p, id, hl_type_vector(h, BT_UINT, 4), args, count, loc);
    case I_TESS_TRI_EDGE: case I_TESS_TRI_AVG: case I_TESS_TRI_SINGLE: case I_TESS_QUAD_EDGE: case I_TESS_QUAD_AVG:
    case I_TESS_QUAD_SINGLE: case I_TESS_QUAD_2D_EDGE: case I_TESS_QUAD_2D_AVG: case I_TESS_QUAD_2D_SINGLE: {
        bool tri = id <= I_TESS_TRI_SINGLE;
        if (count != 5 || !check_out(p, args[2]) || !check_out(p, args[3]) || !check_out(p, args[4]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_vector(h, BT_FLOAT, tri ? 3 : 4), loc, false);
        args[1] = convert(p, args[1], hl_type_scalar(h, BT_FLOAT), loc, false);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    }
    case I_TESS_ISOLINE:
        if (count != 4 || !check_out(p, args[2]) || !check_out(p, args[3]))
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_FLOAT), loc, false);
        args[1] = convert(p, args[1], hl_type_scalar(h, BT_FLOAT), loc, false);
        return intrinsic_expr(p, id, hl_type_void(h), args, count, loc);
    case I_NOISE:
        hl_error(h, loc, 3013, "'noise': intrinsic function is only available for texture shaders");
        return error_expr(p, loc);
    default:
        if (id >= I_TEX1D && id <= I_TEXCUBEPROJ)
            return legacy_tex(p, id, args, count, loc, name);
        return no_match(p, name, count, loc);
    }
}

/* ------------------------------------------------------------------ methods */

static const struct {
    const char *name;
    uint32_t id;
} methods[] = {
    {"Sample", M_SAMPLE},
    {"SampleBias", M_SAMPLE_BIAS},
    {"SampleGrad", M_SAMPLE_GRAD},
    {"SampleLevel", M_SAMPLE_LEVEL},
    {"SampleCmp", M_SAMPLE_CMP},
    {"SampleCmpLevelZero", M_SAMPLE_CMP_LEVEL_ZERO},
    {"Load", M_LOAD},
    {"Gather", M_GATHER},
    {"GatherRed", M_GATHER_RED},
    {"GatherGreen", M_GATHER_GREEN},
    {"GatherBlue", M_GATHER_BLUE},
    {"GatherAlpha", M_GATHER_ALPHA},
    {"GatherCmp", M_GATHER_CMP},
    {"GatherCmpRed", M_GATHER_CMP_RED},
    {"GatherCmpGreen", M_GATHER_CMP_GREEN},
    {"GatherCmpBlue", M_GATHER_CMP_BLUE},
    {"GatherCmpAlpha", M_GATHER_CMP_ALPHA},
    {"GetDimensions", M_GET_DIMENSIONS},
    {"CalculateLevelOfDetail", M_CALCULATE_LOD},
    {"CalculateLevelOfDetailUnclamped", M_CALCULATE_LOD_UNCLAMPED},
    {"GetSamplePosition", M_GET_SAMPLE_POSITION},
    {"Load2", M_LOAD2},
    {"Load3", M_LOAD3},
    {"Load4", M_LOAD4},
    {"Store", M_STORE},
    {"Store2", M_STORE2},
    {"Store3", M_STORE3},
    {"Store4", M_STORE4},
    {"InterlockedAdd", M_INTERLOCKED_ADD},
    {"InterlockedAnd", M_INTERLOCKED_AND},
    {"InterlockedOr", M_INTERLOCKED_OR},
    {"InterlockedXor", M_INTERLOCKED_XOR},
    {"InterlockedMin", M_INTERLOCKED_MIN},
    {"InterlockedMax", M_INTERLOCKED_MAX},
    {"InterlockedExchange", M_INTERLOCKED_EXCHANGE},
    {"InterlockedCompareExchange", M_INTERLOCKED_COMPARE_EXCHANGE},
    {"InterlockedCompareStore", M_INTERLOCKED_COMPARE_STORE},
    {"Append", M_APPEND},
    {"Consume", M_CONSUME},
    {"IncrementCounter", M_INCREMENT_COUNTER},
    {"DecrementCounter", M_DECREMENT_COUNTER},
    {"RestartStrip", M_RESTART_STRIP},
};

static bool sampled_texture(uint8_t o) { return o >= OBJ_TEXTURE1D && o <= OBJ_TEXTURECUBEARRAY && o != OBJ_TEXTURE2DMS && o != OBJ_TEXTURE2DMSARRAY; }
static bool gather_texture(uint8_t o)
{
    return o == OBJ_TEXTURE2D || o == OBJ_TEXTURE2DARRAY || o == OBJ_TEXTURECUBE || o == OBJ_TEXTURECUBEARRAY;
}
static bool byte_address(uint8_t o) { return o == OBJ_BYTE_ADDRESS_BUFFER || o == OBJ_RWBYTE_ADDRESS_BUFFER; }

uint32_t method_lookup(Parser *p, const HlType *t, const char *name)
{
    if (t->kind != TY_OBJECT)
        return 0;
    uint32_t id = 0;
    for (size_t i = 0; i < HL_ARRAY_SIZE(methods); i++)
        if (hl_streq(methods[i].name, name))
            id = methods[i].id;
    uint8_t o = t->object;
    switch (id) {
    case M_SAMPLE: case M_SAMPLE_BIAS: case M_SAMPLE_GRAD: case M_SAMPLE_LEVEL: case M_SAMPLE_CMP:
    case M_SAMPLE_CMP_LEVEL_ZERO: case M_CALCULATE_LOD: case M_CALCULATE_LOD_UNCLAMPED:
        return sampled_texture(o) ? id : 0;
    case M_GATHER: case M_GATHER_RED: case M_GATHER_GREEN: case M_GATHER_BLUE: case M_GATHER_ALPHA:
    case M_GATHER_CMP: case M_GATHER_CMP_RED: case M_GATHER_CMP_GREEN: case M_GATHER_CMP_BLUE:
    case M_GATHER_CMP_ALPHA:
        return gather_texture(o) ? id : 0;
    case M_LOAD:
        return (hl_object_is_texture(o) && o != OBJ_TEXTURECUBE && o != OBJ_TEXTURECUBEARRAY && o != OBJ_TEXTURE) ||
                       (hl_object_is_uav(o) && o != OBJ_APPEND_STRUCTURED_BUFFER && o != OBJ_CONSUME_STRUCTURED_BUFFER)
                   ? id
                   : 0;
    case M_GET_DIMENSIONS:
        return hl_object_is_texture(o) || hl_object_is_uav(o) ? id : 0;
    case M_GET_SAMPLE_POSITION:
        return o == OBJ_TEXTURE2DMS || o == OBJ_TEXTURE2DMSARRAY ? id : 0;
    case M_LOAD2: case M_LOAD3: case M_LOAD4:
        return byte_address(o) ? id : 0;
    case M_STORE: case M_STORE2: case M_STORE3: case M_STORE4:
    case M_INTERLOCKED_ADD: case M_INTERLOCKED_AND: case M_INTERLOCKED_OR: case M_INTERLOCKED_XOR:
    case M_INTERLOCKED_MIN: case M_INTERLOCKED_MAX: case M_INTERLOCKED_EXCHANGE: case M_INTERLOCKED_COMPARE_EXCHANGE:
    case M_INTERLOCKED_COMPARE_STORE:
        return o == OBJ_RWBYTE_ADDRESS_BUFFER ? id : 0;
    case M_APPEND:
        return o == OBJ_APPEND_STRUCTURED_BUFFER || o == OBJ_POINT_STREAM || o == OBJ_LINE_STREAM ||
                       o == OBJ_TRIANGLE_STREAM
                   ? id
                   : 0;
    case M_RESTART_STRIP:
        return o == OBJ_POINT_STREAM || o == OBJ_LINE_STREAM || o == OBJ_TRIANGLE_STREAM ? id : 0;
    case M_CONSUME:
        return o == OBJ_CONSUME_STRUCTURED_BUFFER ? id : 0;
    case M_INCREMENT_COUNTER: case M_DECREMENT_COUNTER:
        return o == OBJ_RWSTRUCTURED_BUFFER ? id : 0;
    }
    return 0;
}

static HlExpr *method_expr(Parser *p, HlExpr *object, uint32_t id, HlType *type, HlExpr **args, uint32_t count,
                           HlLoc loc)
{
    HlExpr *e = new_expr(p, E_METHOD, type, loc);
    e->a = object;
    e->intrinsic = id;
    e->args = args;
    e->arg_count = count;
    return e;
}

static HlType *vec(Parser *p, uint8_t base, uint32_t n)
{
    return n == 1 ? hl_type_scalar(p->h, base) : hl_type_vector(p->h, base, (uint8_t)n);
}

static bool is_sampler(const HlExpr *e, bool comparison)
{
    if (e->type->kind != TY_OBJECT)
        return false;
    return comparison ? e->type->object == OBJ_SAMPLER_COMPARISON
                      : (e->type->object == OBJ_SAMPLER || e->type->object == OBJ_SAMPLER2D ||
                         e->type->object == OBJ_SAMPLER1D || e->type->object == OBJ_SAMPLER3D ||
                         e->type->object == OBJ_SAMPLERCUBE);
}

static HlType *texel_type(Parser *p, const HlType *t)
{
    return t->elem ? t->elem : hl_type_vector(p->h, BT_FLOAT, 4);
}

HlExpr *check_method(Parser *p, HlExpr *object, uint32_t id, HlExpr **args, uint32_t count, HlLoc loc,
                     const char *name)
{
    Hlsl *h = p->h;
    const HlType *t = object->type;
    uint8_t o = t->object;
    for (uint32_t i = 0; i < count; i++)
        if (args[i]->kind == E_ERROR || args[i]->type->kind == TY_ERROR)
            return error_expr(p, loc);
    uint32_t dims = hl_object_dims(o);
    uint32_t coords = dims + (hl_object_is_array(o) ? 1 : 0);
    /* offsets have the texture's dimensions; cubes take none. */
    uint32_t offset_dims = (o == OBJ_TEXTURECUBE || o == OBJ_TEXTURECUBEARRAY) ? 0 : dims;
    HlType *fcoord = vec(p, BT_FLOAT, coords);
    HlType *ioffset = offset_dims ? vec(p, BT_INT, offset_dims) : nullptr;
    switch (id) {
    case M_SAMPLE: case M_SAMPLE_BIAS: case M_SAMPLE_GRAD: case M_SAMPLE_LEVEL: case M_SAMPLE_CMP:
    case M_SAMPLE_CMP_LEVEL_ZERO: {
        bool cmp = id == M_SAMPLE_CMP || id == M_SAMPLE_CMP_LEVEL_ZERO;
        uint32_t fixed = id == M_SAMPLE ? 2 : id == M_SAMPLE_GRAD ? 4 : 3;
        if (id == M_SAMPLE_CMP_LEVEL_ZERO)
            fixed = 3;
        if (count < fixed || count > fixed + 3 || !is_sampler(args[0], cmp))
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], fcoord, loc, false);
        if (id == M_SAMPLE_GRAD) {
            HlType *g = vec(p, BT_FLOAT, dims);
            args[2] = convert(p, args[2], g, loc, false);
            args[3] = convert(p, args[3], g, loc, false);
        } else if (fixed == 3) {
            args[2] = convert(p, args[2], hl_type_scalar(h, BT_FLOAT), loc, false);
        }
        uint32_t i = fixed;
        if (i < count && ioffset)
            args[i] = convert(p, args[i], ioffset, loc, false), i++;
        if (i < count && id != M_SAMPLE_LEVEL && id != M_SAMPLE_CMP_LEVEL_ZERO)
            args[i] = convert(p, args[i], hl_type_scalar(h, BT_FLOAT), loc, false), i++;
        if (i < count) {
            if (!check_out(p, args[i]))
                return no_match(p, name, count, loc);
            i++;
        }
        if (i != count)
            return no_match(p, name, count, loc);
        HlType *r = cmp ? hl_type_scalar(h, BT_FLOAT) : texel_type(p, t);
        return method_expr(p, object, id, r, args, count, loc);
    }
    case M_CALCULATE_LOD: case M_CALCULATE_LOD_UNCLAMPED:
        if (count != 2 || !is_sampler(args[0], false))
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], vec(p, BT_FLOAT, dims), loc, false);
        return method_expr(p, object, id, hl_type_scalar(h, BT_FLOAT), args, count, loc);
    case M_GATHER: case M_GATHER_RED: case M_GATHER_GREEN: case M_GATHER_BLUE: case M_GATHER_ALPHA:
    case M_GATHER_CMP: case M_GATHER_CMP_RED: case M_GATHER_CMP_GREEN: case M_GATHER_CMP_BLUE:
    case M_GATHER_CMP_ALPHA: {
        bool cmp = id >= M_GATHER_CMP;
        uint32_t fixed = cmp ? 3 : 2;
        if (count < fixed || !is_sampler(args[0], cmp))
            return no_match(p, name, count, loc);
        args[1] = convert(p, args[1], fcoord, loc, false);
        if (cmp)
            args[2] = convert(p, args[2], hl_type_scalar(h, BT_FLOAT), loc, false);
        uint32_t extra = count - fixed;
        /* an offset, four offsets, then maybe the status. */
        uint32_t offsets = extra >= 4 ? 4 : extra >= 1 ? 1 : 0;
        if (offsets && !ioffset)
            return no_match(p, name, count, loc);
        for (uint32_t i = 0; i < offsets; i++)
            args[fixed + i] = convert(p, args[fixed + i], ioffset, loc, false);
        if (extra > offsets + 1 || (extra == offsets + 1 && !check_out(p, args[count - 1])))
            return no_match(p, name, count, loc);
        HlType *texel = texel_type(p, t);
        HlType *r = hl_type_vector(h, cmp ? BT_FLOAT : texel->base, 4);
        return method_expr(p, object, id, r, args, count, loc);
    }
    case M_LOAD: {
        if (o == OBJ_STRUCTURED_BUFFER || o == OBJ_RWSTRUCTURED_BUFFER) {
            if (count < 1 || count > 2)
                return no_match(p, name, count, loc);
            args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
            if (count == 2 && !check_out(p, args[1]))
                return no_match(p, name, count, loc);
            return method_expr(p, object, id, texel_type(p, t), args, count, loc);
        }
        if (byte_address(o)) {
            if (count < 1 || count > 2)
                return no_match(p, name, count, loc);
            args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
            if (count == 2 && !check_out(p, args[1]))
                return no_match(p, name, count, loc);
            return method_expr(p, object, id, hl_type_scalar(h, BT_UINT), args, count, loc);
        }
        bool ms = o == OBJ_TEXTURE2DMS || o == OBJ_TEXTURE2DMSARRAY;
        bool buffer = o == OBJ_BUFFER || o == OBJ_RWBUFFER;
        bool uav = hl_object_is_uav(o);
        uint32_t n = coords + (ms || buffer || uav ? 0 : 1);
        if (count < 1)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], vec(p, BT_INT, n), loc, false);
        uint32_t i = 1;
        if (ms) {
            if (count < 2)
                return no_match(p, name, count, loc);
            args[1] = convert(p, args[1], hl_type_scalar(h, BT_INT), loc, false);
            i = 2;
        }
        if (i < count && !buffer && !uav && ioffset)
            args[i] = convert(p, args[i], ioffset, loc, false), i++;
        if (i < count) {
            if (!check_out(p, args[i]))
                return no_match(p, name, count, loc);
            i++;
        }
        if (i != count)
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, texel_type(p, t), args, count, loc);
    }
    case M_LOAD2: case M_LOAD3: case M_LOAD4:
        if (count < 1 || count > 2)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        if (count == 2 && !check_out(p, args[1]))
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_vector(h, BT_UINT, (uint8_t)(id - M_LOAD2 + 2)), args, count, loc);
    case M_STORE: case M_STORE2: case M_STORE3: case M_STORE4:
        if (count != 2)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        args[1] = convert(p, args[1], vec(p, BT_UINT, id - M_STORE + 1), loc, false);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    case M_INTERLOCKED_ADD: case M_INTERLOCKED_AND: case M_INTERLOCKED_OR: case M_INTERLOCKED_XOR:
    case M_INTERLOCKED_MIN: case M_INTERLOCKED_MAX: case M_INTERLOCKED_EXCHANGE: {
        if (count != 2 && count != 3)
            return no_match(p, name, count, loc);
        if (id == M_INTERLOCKED_EXCHANGE && count != 3)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        uint8_t base = numeric(args[1]) && args[1]->type->base == BT_INT && (id == M_INTERLOCKED_MIN || id == M_INTERLOCKED_MAX) ? BT_INT : BT_UINT;
        args[1] = convert(p, args[1], hl_type_scalar(h, base), loc, false);
        if (count == 3 && !check_out(p, args[2]))
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    }
    case M_INTERLOCKED_COMPARE_EXCHANGE: case M_INTERLOCKED_COMPARE_STORE: {
        uint32_t want = id == M_INTERLOCKED_COMPARE_EXCHANGE ? 4 : 3;
        if (count != want)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
        args[1] = convert(p, args[1], hl_type_scalar(h, BT_UINT), loc, false);
        args[2] = convert(p, args[2], hl_type_scalar(h, BT_UINT), loc, false);
        if (count == 4 && !check_out(p, args[3]))
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    }
    case M_GET_DIMENSIONS: {
        for (uint32_t i = 0; i < count; i++) {
            if (!numeric(args[i]) || hl_type_components(args[i]->type) != 1)
                return no_match(p, name, count, loc);
        }
        /* the counts each object takes: outputs, and whether a mip level comes first. */
        uint32_t outs;
        bool mip_form = false;
        switch (o) {
        case OBJ_BUFFER: case OBJ_RWBUFFER: case OBJ_BYTE_ADDRESS_BUFFER: case OBJ_RWBYTE_ADDRESS_BUFFER:
        case OBJ_RWTEXTURE1D:
            outs = 1;
            break;
        case OBJ_STRUCTURED_BUFFER: case OBJ_RWSTRUCTURED_BUFFER: case OBJ_APPEND_STRUCTURED_BUFFER:
        case OBJ_CONSUME_STRUCTURED_BUFFER: case OBJ_RWTEXTURE1DARRAY: case OBJ_RWTEXTURE2D:
            outs = 2;
            break;
        case OBJ_RWTEXTURE2DARRAY: case OBJ_RWTEXTURE3D: case OBJ_TEXTURE2DMS:
            outs = 3;
            break;
        case OBJ_TEXTURE2DMSARRAY:
            outs = 4;
            break;
        case OBJ_TEXTURE1D:
            outs = 1, mip_form = true;
            break;
        case OBJ_TEXTURE1DARRAY: case OBJ_TEXTURE2D: case OBJ_TEXTURECUBE:
            outs = 2, mip_form = true;
            break;
        case OBJ_TEXTURE2DARRAY: case OBJ_TEXTURE3D: case OBJ_TEXTURECUBEARRAY:
            outs = 3, mip_form = true;
            break;
        default:
            return no_match(p, name, count, loc);
        }
        uint32_t first = 0;
        if (mip_form && count == outs + 2) {
            args[0] = convert(p, args[0], hl_type_scalar(h, BT_UINT), loc, false);
            first = 1;
        } else if (count != outs) {
            return no_match(p, name, count, loc);
        }
        for (uint32_t i = first; i < count; i++)
            if (!check_out(p, args[i]))
                return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    }
    case M_GET_SAMPLE_POSITION:
        if (count != 1)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], hl_type_scalar(h, BT_INT), loc, false);
        return method_expr(p, object, id, hl_type_vector(h, BT_FLOAT, 2), args, count, loc);
    case M_APPEND:
        if (count != 1)
            return no_match(p, name, count, loc);
        args[0] = convert(p, args[0], texel_type(p, t), loc, false);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    case M_RESTART_STRIP:
        if (count)
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_void(h), args, count, loc);
    case M_CONSUME:
        if (count)
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, texel_type(p, t), args, count, loc);
    case M_INCREMENT_COUNTER: case M_DECREMENT_COUNTER:
        if (count)
            return no_match(p, name, count, loc);
        return method_expr(p, object, id, hl_type_scalar(h, BT_UINT), args, count, loc);
    }
    return no_match(p, name, count, loc);
}
