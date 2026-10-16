/*
 * fixed function: direct3d 9's transform, lighting and texture stages as
 * shaders of its own bytecode, a vs_3_0 and a ps_2_x program for each key
 * of the state they depend on, which the translator makes dxbc of as it
 * does programs' shaders, fog and the alpha test included. their
 * constants are computed from the transforms, lights, material and
 * texture stage states when those change.
 *
 * the vertex program's constants: c0 to c3 world view projection, c4 to
 * c7 world view, c8 to c10 the inverse of world view's 3x3 for normals,
 * c11 to c15 the material (diffuse, ambient, specular, emissive, power),
 * c16 the global ambient, c17 fog (start, end, density, 1 / (end -
 * start)), c18 points (size, 0, 0, viewport height), c19 point scale (a,
 * b, c), c20 to c23 projection, c24 on 7 for each of 8 lights (diffuse,
 * specular, ambient, position, direction, attenuations 0 to 2 and range,
 * falloff and the cosines of theta and phi halves), c80 on 4 for each
 * stage's texture matrix, c112 on 4 for each blend matrix, c148 the
 * screen to clip transform of pretransformed vertices, c149 (0, 1, 0.5,
 * 2), c150 (4, 255, log2 e, 0). the pixel program's: c0 the texture factor, c1 to c8
 * the stages' constants, c9 to c16 their bump matrices, c17 to c24 their
 * luminance scale and offset, c25 (0, 1, 0.5, 4), c26 (2).
 */
#include "d3d9_private.h"

/* ------------------------------------------------------------------ bytecode */

typedef struct Asm {
    DWORD words[8192];
    UINT n;
    BOOL failed;
} Asm;

enum { T_TEMP = 0, T_INPUT = 1, T_CONST = 2, T_ADDR = 3, T_TEXTURE = 3, T_OUTPUT = 6, T_COLOROUT = 8, T_SAMPLER = 10 };

#define P(type, num) (0x80000000u | ((num)&0x7FF) | ((type)&7u) << 28 | ((type)&0x18u) << 8)
#define XYZW 0xE4
#define REP(c) ((c) | (c) << 2 | (c) << 4 | (c) << 6)

static DWORD dst(UINT type, UINT num, UINT mask) { return P(type, num) | mask << 16; }
static DWORD dst_sat(UINT type, UINT num, UINT mask) { return dst(type, num, mask) | 1u << 20; }
static DWORD src(UINT type, UINT num, UINT swizzle) { return P(type, num) | swizzle << 16; }
static DWORD neg(DWORD s) { return s | 1u << 24; }
static DWORD comp(DWORD s) { return (s & ~(0xFu << 24)) | 6u << 24; }
static DWORD r(UINT n) { return src(T_TEMP, n, XYZW); }
static DWORD rs(UINT n, UINT swizzle) { return src(T_TEMP, n, swizzle); }
static DWORD c(UINT n) { return src(T_CONST, n, XYZW); }
static DWORD cs(UINT n, UINT swizzle) { return src(T_CONST, n, swizzle); }
static DWORD w(UINT n, UINT mask) { return dst(T_TEMP, n, mask); }

static void emit(Asm *a, UINT opcode, UINT control, UINT n, const DWORD *params)
{
    if (a->n + n + 1 >= RTL_NUMBER_OF(a->words)) {
        a->failed = TRUE;
        return;
    }
    a->words[a->n++] = opcode | control << 16 | n << 24;
    for (UINT i = 0; i < n; i++)
        a->words[a->n++] = params[i];
}

#define OP(a, op, ...)                                                                                                 \
    do {                                                                                                               \
        const DWORD params_[] = {__VA_ARGS__};                                                                         \
        emit(a, op, 0, RTL_NUMBER_OF(params_), params_);                                                              \
    } while (0)

static void dcl(Asm *a, UINT usage, UINT index, DWORD reg)
{
    const DWORD params[2] = {0x80000000u | usage | index << 16, reg};
    emit(a, D9_OP_DCL, 0, 2, params);
}

static void dcl_sampler(Asm *a, UINT type, UINT unit)
{
    const DWORD params[2] = {0x80000000u | type << 27, dst(T_SAMPLER, unit, 0xF)};
    emit(a, D9_OP_DCL, 0, 2, params);
}

static void def(Asm *a, UINT reg, float x, float y, float z, float w_)
{
    DWORD params[5] = {dst(T_CONST, reg, 0xF)};
    memcpy(&params[1], &x, 4);
    memcpy(&params[2], &y, 4);
    memcpy(&params[3], &z, 4);
    memcpy(&params[4], &w_, 4);
    emit(a, D9_OP_DEF, 0, 5, params);
}

/* ------------------------------------------------------------------ the vertex program */

/* the inputs' registers. */
enum { V_POS, V_NORMAL, V_DIFFUSE, V_SPECULAR, V_PSIZE, V_WEIGHT, V_INDICES, V_TEX };
/* the outputs'. */
enum { O_POS, O_DIFFUSE, O_SPECULAR, O_FOG, O_PSIZE, O_TEX };
/* temps: the position in view space, the normal, the colors summed, scratch. */
enum { R_POS, R_NORMAL, R_DIFFUSE, R_SPECULAR, R_AMBIENT, R_EYE, R_L, R_T0, R_T1, R_T2, R_T3, R_T4, R_COLOR, R_MAT };

#define C_WVP 0
#define C_WV 4
#define C_NORMAL 8
#define C_MAT 11
#define C_AMBIENT 16
#define C_FOG 17
#define C_POINT 18
#define C_POINT_SCALE 19
#define C_PROJ 20
#define C_LIGHTS 24
#define C_TEXMAT 80
#define C_BLEND 112
#define C_SCREEN 148
#define C_K 149
#define C_K2 150

/* the material's color of a kind, from the vertex's colors as the material sources say. */
static DWORD material(const D9FFVSKey *k, UINT kind)
{
    UINT source = (k->material_sources >> (kind * 2)) & 3;
    if (source == D3DMCS_COLOR1 && (k->inputs & (1u << V_DIFFUSE)))
        return src(T_INPUT, V_DIFFUSE, XYZW);
    if (source == D3DMCS_COLOR2 && (k->inputs & (1u << V_SPECULAR)))
        return src(T_INPUT, V_SPECULAR, XYZW);
    static const BYTE REGS[4] = {C_MAT, C_MAT + 2, C_MAT + 1, C_MAT + 3};
    return c(REGS[kind]);
}

static void light(Asm *a, const D9FFVSKey *k, UINT i)
{
    UINT type = (k->lights >> (i * 2)) & 3;
    UINT base = C_LIGHTS + i * 7;
    /* L toward the light and the attenuation in R_T1.w. */
    if (type == D3DLIGHT_DIRECTIONAL) {
        OP(a, D9_OP_MOV, w(R_L, 7), neg(cs(base + 4, XYZW)));
        OP(a, D9_OP_MOV, w(R_T1, 8), cs(C_K, REP(1)));
    } else {
        OP(a, D9_OP_ADD, w(R_L, 7), c(base + 3), neg(r(R_POS)));
        /* d squared in w, 1 / d in z, then L normalized and d in y, 1 in x. */
        OP(a, D9_OP_DP3, w(R_T0, 8), r(R_L), r(R_L));
        OP(a, D9_OP_RSQ, w(R_T0, 4), rs(R_T0, REP(3)));
        OP(a, D9_OP_MUL, w(R_L, 7), r(R_L), rs(R_T0, REP(2)));
        OP(a, D9_OP_RCP, w(R_T0, 2), rs(R_T0, REP(2)));
        OP(a, D9_OP_MOV, w(R_T0, 1), cs(C_K, REP(1)));
        /* 1 / (a0 + a1 d + a2 d^2), zero beyond the range. */
        OP(a, D9_OP_DP3, w(R_T1, 8), rs(R_T0, 0xF4), c(base + 5));
        OP(a, D9_OP_RCP, w(R_T1, 8), rs(R_T1, REP(3)));
        OP(a, D9_OP_SGE, w(R_T2, 1), cs(base + 5, REP(3)), rs(R_T0, REP(1)));
        OP(a, D9_OP_MUL, w(R_T1, 8), rs(R_T1, REP(3)), rs(R_T2, REP(0)));
        if (type == D3DLIGHT_SPOT) {
            /* rho, the cosine of the light's direction and the light to the vertex, between phi's and theta's. */
            OP(a, D9_OP_DP3, w(R_T2, 2), neg(r(R_L)), c(base + 4));
            OP(a, D9_OP_ADD, w(R_T2, 4), rs(R_T2, REP(1)), neg(cs(base + 6, REP(2))));
            OP(a, D9_OP_ADD, w(R_T2, 8), cs(base + 6, REP(1)), neg(cs(base + 6, REP(2))));
            OP(a, D9_OP_RCP, w(R_T2, 8), rs(R_T2, REP(3)));
            OP(a, D9_OP_MUL, dst_sat(T_TEMP, R_T2, 4), rs(R_T2, REP(2)), rs(R_T2, REP(3)));
            OP(a, D9_OP_POW, w(R_T2, 4), rs(R_T2, REP(2)), cs(base + 6, REP(0)));
            OP(a, D9_OP_MUL, w(R_T1, 8), rs(R_T1, REP(3)), rs(R_T2, REP(2)));
        }
    }
    /* diffuse: N.L clamped, ambient, then specular of the halfway vector. */
    OP(a, D9_OP_DP3, w(R_T0, 1), r(R_NORMAL), r(R_L));
    OP(a, D9_OP_MAX, w(R_T0, 1), rs(R_T0, REP(0)), cs(C_K, REP(0)));
    OP(a, D9_OP_MUL, w(R_T0, 1), rs(R_T0, REP(0)), rs(R_T1, REP(3)));
    OP(a, D9_OP_MAD, w(R_DIFFUSE, 7), c(base), rs(R_T0, REP(0)), r(R_DIFFUSE));
    OP(a, D9_OP_MAD, w(R_AMBIENT, 7), c(base + 2), rs(R_T1, REP(3)), r(R_AMBIENT));
    if (k->specular) {
        OP(a, D9_OP_ADD, w(R_T2, 7), r(R_L), r(R_EYE));
        OP(a, D9_OP_NRM, w(R_T3, 7), r(R_T2));
        OP(a, D9_OP_DP3, w(R_T2, 1), r(R_NORMAL), r(R_T3));
        OP(a, D9_OP_MAX, w(R_T2, 1), rs(R_T2, REP(0)), cs(C_K, REP(0)));
        OP(a, D9_OP_POW, w(R_T2, 1), rs(R_T2, REP(0)), cs(C_MAT + 4, REP(0)));
        /* none where the surface faces away. */
        OP(a, D9_OP_SLT, w(R_T2, 2), cs(C_K, REP(0)), rs(R_T0, REP(0)));
        OP(a, D9_OP_MUL, w(R_T2, 1), rs(R_T2, REP(0)), rs(R_T2, REP(1)));
        OP(a, D9_OP_MUL, w(R_T2, 1), rs(R_T2, REP(0)), rs(R_T1, REP(3)));
        OP(a, D9_OP_MAD, w(R_SPECULAR, 7), c(base + 1), rs(R_T2, REP(0)), r(R_SPECULAR));
    }
}

/* the texture coordinates of a stage: a vertex's set or generated, through the stage's matrix. */
static void texcoord(Asm *a, const D9FFVSKey *k, UINT stage)
{
    UINT index = (k->tex_index >> (stage * 4)) & 7;
    UINT gen = (k->tex_gen >> (stage * 3)) & 7;
    UINT flags = (k->tex_flags >> (stage * 4)) & 0xF;
    UINT count = flags & 7;
    DWORD out = dst(T_OUTPUT, O_TEX + stage, 0xF);
    UINT size = (k->tex_sizes >> (index * 2)) & 3;
    switch (gen) {
    case 1:
        OP(a, D9_OP_MOV, w(R_T0, 7), r(R_NORMAL));
        break;
    case 2:
        OP(a, D9_OP_MOV, w(R_T0, 7), r(R_POS));
        break;
    case 3:
    case 4:
        /* the reflection of the eye's direction about the normal: 2 (E.N) N - E. */
        OP(a, D9_OP_DP3, w(R_T0, 8), neg(r(R_EYE)), r(R_NORMAL));
        OP(a, D9_OP_ADD, w(R_T0, 8), rs(R_T0, REP(3)), rs(R_T0, REP(3)));
        OP(a, D9_OP_MAD, w(R_T0, 7), r(R_NORMAL), rs(R_T0, REP(3)), r(R_EYE));
        if (gen == 4) {
            /* the sphere map's: R over 2 sqrt(x^2 + y^2 + (z + 1)^2), plus a half. */
            OP(a, D9_OP_ADD, w(R_T1, 7), r(R_T0), cs(C_K, 0x10));
            OP(a, D9_OP_DP3, w(R_T1, 8), r(R_T1), r(R_T1));
            OP(a, D9_OP_RSQ, w(R_T1, 8), rs(R_T1, REP(3)));
            OP(a, D9_OP_MUL, w(R_T1, 8), rs(R_T1, REP(3)), cs(C_K, REP(2)));
            OP(a, D9_OP_MAD, w(R_T0, 3), r(R_T0), rs(R_T1, REP(3)), cs(C_K, REP(2)));
            OP(a, D9_OP_MOV, w(R_T0, 0xC), cs(C_K, REP(0)));
        }
        break;
    default:
        if (k->inputs & (1u << (V_TEX + index))) {
            OP(a, D9_OP_MOV, w(R_T0, 0xF), src(T_INPUT, V_TEX + index, XYZW));
            /* missing components: the one after the last is 1, as direct3d 9 pads coordinates for matrices. */
            if (size < 3)
                OP(a, D9_OP_MOV, w(R_T0, 0xF & ~((2u << size) - 1)), cs(C_K, REP(0)));
            if (size < 3)
                OP(a, D9_OP_MOV, w(R_T0, 2u << size), cs(C_K, REP(1)));
        } else {
            OP(a, D9_OP_MOV, w(R_T0, 0xF), cs(C_K, 0x40));
        }
        break;
    }
    if (gen && gen != 4)
        OP(a, D9_OP_MOV, w(R_T0, 8), cs(C_K, REP(1)));
    if (count) {
        OP(a, D9_OP_M4x4, w(R_T1, 0xF), r(R_T0), c(C_TEXMAT + stage * 4));
        /* projected coordinates divide by their last component, which ps_2's texldp reads in w. */
        if ((flags & 8) && count < 4)
            OP(a, D9_OP_MOV, w(R_T1, 8), rs(R_T1, REP(count - 1)));
        OP(a, D9_OP_MOV, out, r(R_T1));
    } else {
        OP(a, D9_OP_MOV, out, r(R_T0));
    }
}

static HRESULT vertex_program(const D9FFVSKey *k, Asm *a)
{
    a->words[a->n++] = 0xFFFE0300;
    def(a, C_K, 0, 1, 0.5f, 2);
    def(a, C_K2, 4, 255, 1.442695f, 0);
    if (k->pretransformed) {
        dcl(a, D3DDECLUSAGE_POSITIONT, 0, dst(T_INPUT, V_POS, 0xF));
    } else {
        dcl(a, D3DDECLUSAGE_POSITION, 0, dst(T_INPUT, V_POS, 0xF));
        if (k->inputs & (1u << V_NORMAL))
            dcl(a, D3DDECLUSAGE_NORMAL, 0, dst(T_INPUT, V_NORMAL, 0xF));
        if (k->inputs & (1u << V_WEIGHT))
            dcl(a, D3DDECLUSAGE_BLENDWEIGHT, 0, dst(T_INPUT, V_WEIGHT, 0xF));
        if (k->inputs & (1u << V_INDICES))
            dcl(a, D3DDECLUSAGE_BLENDINDICES, 0, dst(T_INPUT, V_INDICES, 0xF));
    }
    if (k->inputs & (1u << V_DIFFUSE))
        dcl(a, D3DDECLUSAGE_COLOR, 0, dst(T_INPUT, V_DIFFUSE, 0xF));
    if (k->inputs & (1u << V_SPECULAR))
        dcl(a, D3DDECLUSAGE_COLOR, 1, dst(T_INPUT, V_SPECULAR, 0xF));
    if (k->inputs & (1u << V_PSIZE))
        dcl(a, D3DDECLUSAGE_PSIZE, 0, dst(T_INPUT, V_PSIZE, 0xF));
    for (UINT i = 0; i < 8; i++)
        if (k->inputs & (1u << (V_TEX + i)))
            dcl(a, D3DDECLUSAGE_TEXCOORD, i, dst(T_INPUT, V_TEX + i, 0xF));
    dcl(a, D3DDECLUSAGE_POSITION, 0, dst(T_OUTPUT, O_POS, 0xF));
    dcl(a, D3DDECLUSAGE_COLOR, 0, dst(T_OUTPUT, O_DIFFUSE, 0xF));
    dcl(a, D3DDECLUSAGE_COLOR, 1, dst(T_OUTPUT, O_SPECULAR, 0xF));
    if (k->fog)
        dcl(a, D3DDECLUSAGE_FOG, 0, dst(T_OUTPUT, O_FOG, 1));
    if (k->point_scale || (k->inputs & (1u << V_PSIZE)))
        dcl(a, D3DDECLUSAGE_PSIZE, 0, dst(T_OUTPUT, O_PSIZE, 1));
    for (UINT i = 0; i < k->stages; i++)
        dcl(a, D3DDECLUSAGE_TEXCOORD, i, dst(T_OUTPUT, O_TEX + i, 0xF));

    if (k->pretransformed) {
        /* screen coordinates to clip space, times w, which is 1 over rhw. */
        OP(a, D9_OP_MAD, w(R_T0, 3), src(T_INPUT, V_POS, XYZW), c(C_SCREEN), cs(C_SCREEN, 0xEE));
        OP(a, D9_OP_MOV, w(R_T0, 4), src(T_INPUT, V_POS, REP(2)));
        OP(a, D9_OP_MOV, w(R_T0, 8), cs(C_K, REP(1)));
        OP(a, D9_OP_RCP, w(R_T1, 1), src(T_INPUT, V_POS, REP(3)));
        OP(a, D9_OP_MUL, dst(T_OUTPUT, O_POS, 0xF), r(R_T0), rs(R_T1, REP(0)));
        OP(a, D9_OP_MOV, w(R_POS, 0xF), cs(C_K, 0x40));
        OP(a, D9_OP_MOV, w(R_NORMAL, 0xF), cs(C_K, 0x40));
        OP(a, D9_OP_MOV, w(R_EYE, 0xF), cs(C_K, 0x40));
    } else {
        UINT blend = k->blend;
        if (!blend) {
            OP(a, D9_OP_M4x4, w(R_POS, 0xF), src(T_INPUT, V_POS, XYZW), c(C_WV));
            if (k->inputs & (1u << V_NORMAL))
                OP(a, D9_OP_M3x3, w(R_NORMAL, 7), src(T_INPUT, V_NORMAL, XYZW), c(C_NORMAL));
        } else {
            /* vertex blending: the weights given, the last 1 less their sum, each matrix's. */
            OP(a, D9_OP_MOV, w(R_POS, 0xF), cs(C_K, 0x40));
            OP(a, D9_OP_MOV, w(R_NORMAL, 0xF), cs(C_K, 0x40));
            OP(a, D9_OP_MOV, w(R_T2, 1), cs(C_K, REP(1)));
            for (UINT i = 0; i <= blend; i++) {
                DWORD weight;
                if (i < blend) {
                    weight = src(T_INPUT, V_WEIGHT, REP(i));
                    OP(a, D9_OP_ADD, w(R_T2, 1), rs(R_T2, REP(0)), neg(weight));
                } else {
                    weight = rs(R_T2, REP(0));
                }
                UINT m = C_BLEND + i * 4;
                if (k->indexed) {
                    /* the matrix of the vertex's index, 4 registers each, through a0. */
                    OP(a, D9_OP_MUL, w(R_T3, 1), src(T_INPUT, V_INDICES, REP(i)), cs(C_K2, REP(0)));
                    OP(a, D9_OP_MOVA, dst(T_ADDR, 0, 1), rs(R_T3, REP(0)));
                    const DWORD params[4] = {w(R_T4, 0xF), src(T_INPUT, V_POS, XYZW),
                                             src(T_CONST, C_BLEND, XYZW) | 1u << 13, P(T_ADDR, 0) | REP(0) << 16};
                    emit(a, D9_OP_M4x4, 0, 4, params);
                } else {
                    OP(a, D9_OP_M4x4, w(R_T4, 0xF), src(T_INPUT, V_POS, XYZW), c(m));
                }
                OP(a, D9_OP_MAD, w(R_POS, 0xF), r(R_T4), weight, r(R_POS));
                if (k->inputs & (1u << V_NORMAL)) {
                    if (k->indexed) {
                        const DWORD params[4] = {w(R_T4, 7), src(T_INPUT, V_NORMAL, XYZW),
                                                 src(T_CONST, C_BLEND, XYZW) | 1u << 13, P(T_ADDR, 0) | REP(0) << 16};
                        emit(a, D9_OP_M3x3, 0, 4, params);
                    } else {
                        OP(a, D9_OP_M3x3, w(R_T4, 7), src(T_INPUT, V_NORMAL, XYZW), c(m));
                    }
                    OP(a, D9_OP_MAD, w(R_NORMAL, 7), r(R_T4), weight, r(R_NORMAL));
                }
            }
        }
        OP(a, D9_OP_M4x4, dst(T_OUTPUT, O_POS, 0xF), r(R_POS), c(C_PROJ));
        if (k->normalize && (k->inputs & (1u << V_NORMAL)))
            OP(a, D9_OP_NRM, w(R_NORMAL, 7), r(R_NORMAL));
        /* the direction toward the eye: the vertex's own with a local viewer, z otherwise. */
        if (k->local_viewer)
            OP(a, D9_OP_NRM, w(R_EYE, 7), neg(r(R_POS)));
        else
            OP(a, D9_OP_MOV, w(R_EYE, 7), neg(cs(C_K, 0x40 | 0x10)));
    }

    /* colors: lit, or the vertex's own, white and black without them. */
    if (k->lighting && !k->pretransformed) {
        OP(a, D9_OP_MOV, w(R_DIFFUSE, 0xF), cs(C_K, REP(0)));
        OP(a, D9_OP_MOV, w(R_SPECULAR, 0xF), cs(C_K, REP(0)));
        OP(a, D9_OP_MOV, w(R_AMBIENT, 0xF), cs(C_K, REP(0)));
        for (UINT i = 0; i < 8; i++)
            if ((k->lights >> (i * 2)) & 3)
                light(a, k, i);
        DWORD diffuse = material(k, 0), ambient = material(k, 2), emissive = material(k, 3), specular = material(k, 1);
        OP(a, D9_OP_MUL, w(R_COLOR, 7), r(R_DIFFUSE), diffuse);
        OP(a, D9_OP_ADD, w(R_AMBIENT, 7), r(R_AMBIENT), c(C_AMBIENT));
        OP(a, D9_OP_MAD, w(R_COLOR, 7), r(R_AMBIENT), ambient, r(R_COLOR));
        OP(a, D9_OP_ADD, w(R_COLOR, 7), r(R_COLOR), emissive);
        OP(a, D9_OP_MOV, w(R_COLOR, 8), diffuse);
        OP(a, D9_OP_MOV, dst_sat(T_OUTPUT, O_DIFFUSE, 0xF), r(R_COLOR));
        OP(a, D9_OP_MUL, w(R_SPECULAR, 7), r(R_SPECULAR), specular);
        OP(a, D9_OP_MOV, w(R_SPECULAR, 8), specular);
        OP(a, D9_OP_MOV, dst_sat(T_OUTPUT, O_SPECULAR, 0xF), r(R_SPECULAR));
    } else {
        if (k->inputs & (1u << V_DIFFUSE))
            OP(a, D9_OP_MOV, dst(T_OUTPUT, O_DIFFUSE, 0xF), src(T_INPUT, V_DIFFUSE, XYZW));
        else
            OP(a, D9_OP_MOV, dst(T_OUTPUT, O_DIFFUSE, 0xF), cs(C_K, REP(1)));
        if (k->inputs & (1u << V_SPECULAR))
            OP(a, D9_OP_MOV, dst(T_OUTPUT, O_SPECULAR, 0xF), src(T_INPUT, V_SPECULAR, XYZW));
        else
            OP(a, D9_OP_MOV, dst(T_OUTPUT, O_SPECULAR, 0xF), cs(C_K, REP(0)));
    }

    /* vertex fog: of the depth or the distance; pretransformed vertices carry theirs in specular alpha. */
    if (k->fog) {
        if (k->pretransformed) {
            if (k->inputs & (1u << V_SPECULAR))
                OP(a, D9_OP_MOV, dst(T_OUTPUT, O_FOG, 1), src(T_INPUT, V_SPECULAR, REP(3)));
            else
                OP(a, D9_OP_MOV, dst(T_OUTPUT, O_FOG, 1), cs(C_K, REP(1)));
        } else {
            if (k->range_fog) {
                OP(a, D9_OP_DP3, w(R_T0, 1), r(R_POS), r(R_POS));
                OP(a, D9_OP_RSQ, w(R_T0, 1), rs(R_T0, REP(0)));
                OP(a, D9_OP_RCP, w(R_T0, 1), rs(R_T0, REP(0)));
            } else {
                OP(a, D9_OP_MOV, w(R_T0, 1), rs(R_POS, REP(2)));
            }
            if (k->fog == D3DFOG_LINEAR) {
                OP(a, D9_OP_ADD, w(R_T0, 1), cs(C_FOG, REP(1)), neg(rs(R_T0, REP(0))));
                OP(a, D9_OP_MUL, dst(T_OUTPUT, O_FOG, 1), rs(R_T0, REP(0)), cs(C_FOG, REP(3)));
            } else {
                /* e to the -x as 2 to the -x log2 e. */
                OP(a, D9_OP_MUL, w(R_T0, 1), rs(R_T0, REP(0)), cs(C_FOG, REP(2)));
                if (k->fog == D3DFOG_EXP2)
                    OP(a, D9_OP_MUL, w(R_T0, 1), rs(R_T0, REP(0)), rs(R_T0, REP(0)));
                OP(a, D9_OP_MUL, w(R_T0, 1), rs(R_T0, REP(0)), neg(cs(C_K2, REP(2))));
                OP(a, D9_OP_EXP, dst(T_OUTPUT, O_FOG, 1), rs(R_T0, REP(0)));
            }
        }
    }

    /* point sizes: the vertex's or the state's, scaled by distance when asked. */
    if (k->point_scale || (k->inputs & (1u << V_PSIZE))) {
        DWORD size = k->inputs & (1u << V_PSIZE) ? src(T_INPUT, V_PSIZE, REP(0)) : cs(C_POINT, REP(0));
        if (k->point_scale) {
            OP(a, D9_OP_DP3, w(R_T0, 1), r(R_POS), r(R_POS));
            OP(a, D9_OP_RSQ, w(R_T0, 2), rs(R_T0, REP(0)));
            OP(a, D9_OP_RCP, w(R_T0, 2), rs(R_T0, REP(1)));
            OP(a, D9_OP_MAD, w(R_T1, 1), rs(R_T0, REP(1)), cs(C_POINT_SCALE, REP(1)), cs(C_POINT_SCALE, REP(0)));
            OP(a, D9_OP_MAD, w(R_T1, 1), rs(R_T0, REP(0)), cs(C_POINT_SCALE, REP(2)), rs(R_T1, REP(0)));
            OP(a, D9_OP_RSQ, w(R_T1, 1), rs(R_T1, REP(0)));
            OP(a, D9_OP_MUL, w(R_T1, 1), rs(R_T1, REP(0)), cs(C_POINT, REP(3)));
            OP(a, D9_OP_MUL, dst(T_OUTPUT, O_PSIZE, 1), rs(R_T1, REP(0)), size);
        } else {
            OP(a, D9_OP_MOV, dst(T_OUTPUT, O_PSIZE, 1), size);
        }
    }

    for (UINT i = 0; i < k->stages; i++)
        texcoord(a, k, i);
    a->words[a->n++] = 0x0000FFFF;
    return a->failed ? E_OUTOFMEMORY : D3D_OK;
}

/* ------------------------------------------------------------------ the pixel program */

enum { P_CURRENT = 0, P_TEMP = 1, P_TEX = 2, P_ARG = 3, P_T = 8 };
#define PC_TFACTOR 0
#define PC_CONSTANT 1
#define PC_BUMP 9
#define PC_LUMINANCE 17
#define PC_K 25
#define PC_K2 26

/* a stage's argument: its register and the modifiers asked. */
static DWORD argument(Asm *a, UINT stage, DWORD arg, BOOL alpha, UINT scratch)
{
    DWORD reg;
    switch (arg & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE:
        reg = src(T_INPUT, 0, XYZW);
        break;
    case D3DTA_CURRENT:
        reg = stage ? r(P_CURRENT) : src(T_INPUT, 0, XYZW);
        break;
    case D3DTA_TEXTURE:
        reg = r(P_TEX);
        break;
    case D3DTA_TFACTOR:
        reg = cs(0, XYZW);
        break;
    case D3DTA_SPECULAR:
        reg = src(T_INPUT, 1, XYZW);
        break;
    case D3DTA_TEMP:
        reg = r(P_TEMP);
        break;
    case D3DTA_CONSTANT:
        reg = cs(PC_CONSTANT + stage, XYZW);
        break;
    default:
        reg = cs(PC_K, REP(0));
        break;
    }
    if ((arg & D3DTA_ALPHAREPLICATE) || alpha)
        reg = (reg & ~(0xFFu << 16)) | REP(3) << 16;
    if (arg & D3DTA_COMPLEMENT) {
        OP(a, D9_OP_ADD, w(scratch, 0xF), cs(PC_K, REP(1)), neg(reg));
        reg = r(scratch);
    }
    return reg;
}

/* a stage's color or alpha operation on its arguments, into dest's components. */
static void operation(Asm *a, const D9FFPSStage *s, UINT stage, BOOL alpha, DWORD dest)
{
    UINT op = alpha ? s->alpha_op : s->color_op;
    const BYTE *args = alpha ? s->alpha_args : s->color_args;
    DWORD a1 = argument(a, stage, args[1], alpha, P_ARG);
    DWORD a2 = argument(a, stage, args[2], alpha, P_ARG + 1);
    DWORD a0 = op == D3DTOP_MULTIPLYADD || op == D3DTOP_LERP ? argument(a, stage, args[0], alpha, P_ARG + 2) : 0;
    DWORD sat = dest | 1u << 20;
    switch (op) {
    case D3DTOP_SELECTARG1:
        OP(a, D9_OP_MOV, sat, a1);
        break;
    case D3DTOP_SELECTARG2:
        OP(a, D9_OP_MOV, sat, a2);
        break;
    case D3DTOP_MODULATE:
        OP(a, D9_OP_MUL, sat, a1, a2);
        break;
    case D3DTOP_MODULATE2X:
    case D3DTOP_MODULATE4X:
        OP(a, D9_OP_MUL, w(P_ARG + 3, 0xF), a1, a2);
        OP(a, D9_OP_MUL, sat, r(P_ARG + 3), op == D3DTOP_MODULATE2X ? cs(PC_K2, REP(0)) : cs(PC_K, REP(3)));
        break;
    case D3DTOP_ADD:
        OP(a, D9_OP_ADD, sat, a1, a2);
        break;
    case D3DTOP_ADDSIGNED:
    case D3DTOP_ADDSIGNED2X:
        OP(a, D9_OP_ADD, w(P_ARG + 3, 0xF), a1, a2);
        if (op == D3DTOP_ADDSIGNED) {
            OP(a, D9_OP_ADD, sat, r(P_ARG + 3), neg(cs(PC_K, REP(2))));
        } else {
            OP(a, D9_OP_ADD, w(P_ARG + 3, 0xF), r(P_ARG + 3), neg(cs(PC_K, REP(2))));
            OP(a, D9_OP_ADD, sat, r(P_ARG + 3), r(P_ARG + 3));
        }
        break;
    case D3DTOP_SUBTRACT:
        OP(a, D9_OP_ADD, sat, a1, neg(a2));
        break;
    case D3DTOP_ADDSMOOTH:
        OP(a, D9_OP_MAD, sat, comp(a1), a2, a1);
        break;
    case D3DTOP_BLENDDIFFUSEALPHA:
    case D3DTOP_BLENDTEXTUREALPHA:
    case D3DTOP_BLENDFACTORALPHA:
    case D3DTOP_BLENDCURRENTALPHA: {
        DWORD f = op == D3DTOP_BLENDDIFFUSEALPHA   ? src(T_INPUT, 0, REP(3))
                  : op == D3DTOP_BLENDTEXTUREALPHA ? rs(P_TEX, REP(3))
                  : op == D3DTOP_BLENDFACTORALPHA  ? cs(0, REP(3))
                  : stage                          ? rs(P_CURRENT, REP(3))
                                                   : src(T_INPUT, 0, REP(3));
        OP(a, D9_OP_LRP, sat, f, a1, a2);
        break;
    }
    case D3DTOP_BLENDTEXTUREALPHAPM:
        OP(a, D9_OP_MAD, sat, a2, comp(rs(P_TEX, REP(3))), a1);
        break;
    case D3DTOP_MODULATEALPHA_ADDCOLOR:
        OP(a, D9_OP_MAD, sat, (a1 & ~(0xFFu << 16)) | REP(3) << 16, a2, a1);
        break;
    case D3DTOP_MODULATECOLOR_ADDALPHA:
        OP(a, D9_OP_MAD, sat, a1, a2, (a1 & ~(0xFFu << 16)) | REP(3) << 16);
        break;
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR:
        OP(a, D9_OP_MAD, sat, comp((a1 & ~(0xFFu << 16)) | REP(3) << 16), a2, a1);
        break;
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA:
        OP(a, D9_OP_MAD, sat, comp(a1), a2, (a1 & ~(0xFFu << 16)) | REP(3) << 16);
        break;
    case D3DTOP_DOTPRODUCT3:
        /* 4 (a1 - 0.5).(a2 - 0.5), in every component. */
        OP(a, D9_OP_ADD, w(P_ARG + 3, 0xF), a1, neg(cs(PC_K, REP(2))));
        OP(a, D9_OP_ADD, w(P_ARG + 4, 0xF), a2, neg(cs(PC_K, REP(2))));
        OP(a, D9_OP_DP3, w(P_ARG + 3, 0xF), r(P_ARG + 3), r(P_ARG + 4));
        OP(a, D9_OP_MUL, sat, r(P_ARG + 3), cs(PC_K, REP(3)));
        break;
    case D3DTOP_MULTIPLYADD:
        OP(a, D9_OP_MAD, sat, a1, a2, a0);
        break;
    case D3DTOP_LERP:
        OP(a, D9_OP_LRP, sat, a0, a1, a2);
        break;
    default:
        OP(a, D9_OP_MOV, sat, a1);
        break;
    }
}

static HRESULT pixel_program(const D9FFPSKey *k, Asm *a)
{
    a->words[a->n++] = 0xFFFF0201;
    def(a, PC_K, 0, 1, 0.5f, 4);
    def(a, PC_K2, 2, 0, 0, 0);
    dcl(a, 0, 0, dst(T_INPUT, 0, 0xF));
    dcl(a, 0, 0, dst(T_INPUT, 1, 0xF));
    for (UINT i = 0; i < k->stages; i++) {
        const D9FFPSStage *s = &k->stage[i];
        if (s->texture) {
            dcl(a, 0, 0, dst(T_TEXTURE, i, 0xF));
            dcl_sampler(a, 1 + s->texture, i);
        }
    }
    OP(a, D9_OP_MOV, w(P_CURRENT, 0xF), src(T_INPUT, 0, XYZW));
    OP(a, D9_OP_MOV, w(P_TEMP, 0xF), cs(PC_K, REP(0)));
    BOOL bumped = FALSE;
    for (UINT i = 0; i < k->stages; i++) {
        const D9FFPSStage *s = &k->stage[i];
        if (s->texture) {
            DWORD coords = src(T_TEXTURE, i, XYZW);
            if (bumped) {
                /* the coordinates moved by the bump map of the stage before. */
                OP(a, D9_OP_MOV, w(P_ARG, 0xF), coords);
                OP(a, D9_OP_MAD, w(P_ARG, 3), rs(P_ARG + 5, REP(0)), cs(PC_BUMP + i - 1, 0x44), r(P_ARG));
                OP(a, D9_OP_MAD, w(P_ARG, 3), rs(P_ARG + 5, REP(1)), cs(PC_BUMP + i - 1, 0xEE), r(P_ARG));
                coords = r(P_ARG);
            }
            const DWORD params[3] = {w(P_TEX + 0, 0xF), coords, src(T_SAMPLER, i, XYZW)};
            emit(a, D9_OP_TEX, s->projected ? 1 : 0, 3, params);
            if (bumped && k->stage[i - 1].color_op == D3DTOP_BUMPENVMAPLUMINANCE) {
                OP(a, D9_OP_MAD, w(P_ARG + 1, 1), rs(P_ARG + 5, REP(2)), cs(PC_LUMINANCE + i - 1, REP(0)),
                   cs(PC_LUMINANCE + i - 1, REP(1)));
                OP(a, D9_OP_MUL, w(P_TEX, 7), r(P_TEX), rs(P_ARG + 1, REP(0)));
            }
        }
        bumped = FALSE;
        if (s->color_op == D3DTOP_BUMPENVMAP || s->color_op == D3DTOP_BUMPENVMAPLUMINANCE) {
            OP(a, D9_OP_MOV, w(P_ARG + 5, 0xF), r(P_TEX));
            bumped = TRUE;
            continue;
        }
        UINT out = s->result_temp ? P_TEMP : P_CURRENT;
        operation(a, s, i, FALSE, w(out, 7));
        operation(a, s, i, TRUE, w(out, 8));
    }
    if (k->specular)
        OP(a, D9_OP_ADD, w(P_CURRENT, 7), r(P_CURRENT), src(T_INPUT, 1, XYZW));
    OP(a, D9_OP_MOV, dst(T_COLOROUT, 0, 0xF), r(P_CURRENT));
    a->words[a->n++] = 0x0000FFFF;
    return a->failed ? E_OUTOFMEMORY : D3D_OK;
}

/* ------------------------------------------------------------------ keys and programs */

static UINT decl_inputs(const D9VertexDecl *d, UINT *sizes, BOOL *pretransformed)
{
    UINT inputs = 0;
    *sizes = 0;
    *pretransformed = FALSE;
    for (UINT i = 0; i < d->count; i++) {
        const D3DVERTEXELEMENT9 *e = &d->elements[i];
        switch (e->Usage) {
        case D3DDECLUSAGE_POSITIONT:
            *pretransformed = TRUE;
            inputs |= 1u << V_POS;
            break;
        case D3DDECLUSAGE_POSITION:
            if (!e->UsageIndex)
                inputs |= 1u << V_POS;
            break;
        case D3DDECLUSAGE_NORMAL:
            if (!e->UsageIndex)
                inputs |= 1u << V_NORMAL;
            break;
        case D3DDECLUSAGE_COLOR:
            if (e->UsageIndex < 2)
                inputs |= 1u << (V_DIFFUSE + e->UsageIndex);
            break;
        case D3DDECLUSAGE_PSIZE:
            inputs |= 1u << V_PSIZE;
            break;
        case D3DDECLUSAGE_BLENDWEIGHT:
            inputs |= 1u << V_WEIGHT;
            break;
        case D3DDECLUSAGE_BLENDINDICES:
            inputs |= 1u << V_INDICES;
            break;
        case D3DDECLUSAGE_TEXCOORD:
            if (e->UsageIndex < 8) {
                inputs |= 1u << (V_TEX + e->UsageIndex);
                UINT size = e->Type <= D3DDECLTYPE_FLOAT4 ? e->Type : 1;
                *sizes |= size << (e->UsageIndex * 2);
            }
            break;
        }
    }
    return inputs;
}

/* the stages the pixel program runs: those before the first disabled. */
static UINT active_stages(const D9Device *d)
{
    UINT n = 0;
    while (n < D9_MAX_STAGES && d->stage_states[n][D3DTSS_COLOROP] != D3DTOP_DISABLE)
        n++;
    return n;
}

void D9FFVSKeyOf(const D9Device *d, D9FFVSKey *k)
{
    const DWORD *rs = d->render_states;
    memset(k, 0, sizeof(*k));
    BOOL pre;
    k->inputs = decl_inputs(d->decl, &k->tex_sizes, &pre);
    k->pretransformed = pre;
    k->lighting = rs[D3DRS_LIGHTING] && !pre;
    if (k->lighting) {
        for (UINT i = 0, n = 0; i < d->light_count && n < 8; i++)
            if (d->lights[i].enabled)
                k->lights |= (UINT)d->lights[i].light.Type << (n++ * 2);
        k->specular = !!rs[D3DRS_SPECULARENABLE];
        k->local_viewer = !!rs[D3DRS_LOCALVIEWER];
        if (rs[D3DRS_COLORVERTEX])
            k->material_sources = (rs[D3DRS_DIFFUSEMATERIALSOURCE] & 3) | (rs[D3DRS_SPECULARMATERIALSOURCE] & 3) << 2 |
                                  (rs[D3DRS_AMBIENTMATERIALSOURCE] & 3) << 4 |
                                  (rs[D3DRS_EMISSIVEMATERIALSOURCE] & 3) << 6;
    }
    k->normalize = !!rs[D3DRS_NORMALIZENORMALS];
    if (rs[D3DRS_FOGENABLE] && rs[D3DRS_FOGTABLEMODE] == D3DFOG_NONE)
        k->fog = pre ? 1 : (BYTE)rs[D3DRS_FOGVERTEXMODE];
    k->range_fog = !!rs[D3DRS_RANGEFOGENABLE];
    k->point_scale = !!rs[D3DRS_POINTSCALEENABLE] && !pre;
    if (!pre && rs[D3DRS_VERTEXBLEND] >= 1 && rs[D3DRS_VERTEXBLEND] <= 3) {
        k->blend = (BYTE)rs[D3DRS_VERTEXBLEND];
        k->indexed = rs[D3DRS_INDEXEDVERTEXBLENDENABLE] && (k->inputs & (1u << V_INDICES));
    }
    k->stages = (BYTE)D9_MAX(active_stages(d), 1);
    for (UINT i = 0; i < k->stages; i++) {
        DWORD index = d->stage_states[i][D3DTSS_TEXCOORDINDEX];
        k->tex_index |= (index & 7) << (i * 4);
        k->tex_gen |= ((index >> 16) & 7) << (i * 3);
        DWORD flags = d->stage_states[i][D3DTSS_TEXTURETRANSFORMFLAGS];
        k->tex_flags |= ((flags & 7) | (flags & D3DTTFF_PROJECTED ? 8 : 0)) << (i * 4);
    }
    if (pre)
        k->tex_gen = 0, k->tex_flags = 0;
}

void D9FFPSKeyOf(const D9Device *d, D9FFPSKey *k)
{
    memset(k, 0, sizeof(*k));
    k->stages = (BYTE)active_stages(d);
    k->specular = !!d->render_states[D3DRS_SPECULARENABLE];
    for (UINT i = 0; i < k->stages; i++) {
        const DWORD *ts = d->stage_states[i];
        D9FFPSStage *s = &k->stage[i];
        s->color_op = (BYTE)ts[D3DTSS_COLOROP];
        s->alpha_op = (BYTE)ts[D3DTSS_ALPHAOP];
        if (s->alpha_op == D3DTOP_DISABLE)
            s->alpha_op = D3DTOP_SELECTARG1, s->alpha_args[1] = D3DTA_CURRENT;
        else
            s->alpha_args[0] = (BYTE)ts[D3DTSS_ALPHAARG0], s->alpha_args[1] = (BYTE)ts[D3DTSS_ALPHAARG1],
            s->alpha_args[2] = (BYTE)ts[D3DTSS_ALPHAARG2];
        s->color_args[0] = (BYTE)ts[D3DTSS_COLORARG0];
        s->color_args[1] = (BYTE)ts[D3DTSS_COLORARG1];
        s->color_args[2] = (BYTE)ts[D3DTSS_COLORARG2];
        s->result_temp = ts[D3DTSS_RESULTARG] == D3DTA_TEMP;
        D9Texture *t = d->textures[i];
        if (t)
            s->texture = t->base.type == D3DRTYPE_CUBETEXTURE ? 2 : t->base.type == D3DRTYPE_VOLUMETEXTURE ? 3 : 1;
        s->projected = !!(ts[D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED);
    }
}

/* the program of a key, made once for the device's life. */
D9Shader *D9FFShader(D9Device *d, BOOL pixel, const void *key, UINT size)
{
    D9FFProgram **list = pixel ? &d->ff_ps : &d->ff_vs;
    for (D9FFProgram *p = *list; p; p = p->next)
        if (p->size == size && !memcmp(p->key, key, size))
            return p->shader;
    Asm *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    D9FFProgram *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) + size);
    if (!a || !p) {
        HeapFree(GetProcessHeap(), 0, a);
        HeapFree(GetProcessHeap(), 0, p);
        return NULL;
    }
    a->n = 0;
    a->failed = FALSE;
    HRESULT hr = pixel ? pixel_program(key, a) : vertex_program(key, a);
    D9Shader *s = NULL;
    if (SUCCEEDED(hr) && SUCCEEDED(D9CreateShader(d, a->words, pixel, &s))) {
        s->ff = TRUE;
        /* the device's own: bound, its reference let go, as programs' references would keep the device. */
        D9Bind(&s->base);
        D9ResourceRelease(&s->base);
    }
    HeapFree(GetProcessHeap(), 0, a);
    p->shader = s;
    p->size = size;
    memcpy(p->key, key, size);
    p->next = *list;
    *list = p;
    return s;
}

void D9FreeFFShaders(D9Device *d)
{
    for (UINT pixel = 0; pixel < 2; pixel++) {
        D9FFProgram **list = pixel ? &d->ff_ps : &d->ff_vs;
        for (D9FFProgram *p = *list, *next; p; p = next) {
            next = p->next;
            if (p->shader)
                D9Unbind(&p->shader->base);
            HeapFree(GetProcessHeap(), 0, p);
        }
        *list = NULL;
    }
}

/* ------------------------------------------------------------------ constants */

/* square roots and cosines without a math library: newton's method and a polynomial. */
static float root(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    float r = x > 1.0f ? x / 2.0f : 1.0f;
    for (UINT i = 0; i < 32; i++) {
        float next = 0.5f * (r + x / r);
        if (next == r)
            break;
        r = next;
    }
    return r;
}

static float cosine(float x)
{
    const float pi = 3.14159265f;
    /* into [-pi, pi], then the series to x^16, close enough to a float there. */
    long turns = (long)(x / (2.0f * pi));
    x -= (float)turns * 2.0f * pi;
    if (x > pi)
        x -= 2.0f * pi;
    if (x < -pi)
        x += 2.0f * pi;
    float x2 = x * x, term = 1.0f, sum = 1.0f;
    for (UINT n = 1; n <= 8; n++) {
        term *= -x2 / (float)((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

static void multiply(const D3DMATRIX *a, const D3DMATRIX *b, D3DMATRIX *out)
{
    D3DMATRIX m;
    for (UINT i = 0; i < 4; i++)
        for (UINT j = 0; j < 4; j++)
            m.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] +
                        a->m[i][3] * b->m[3][j];
    *out = m;
}

/* a matrix's columns as rows, as m4x4 reads them. */
static void columns(const D3DMATRIX *m, float (*out)[4], UINT rows)
{
    for (UINT i = 0; i < rows; i++)
        for (UINT j = 0; j < 4; j++)
            out[i][j] = m->m[j][i];
}

static BOOL invert(const D3DMATRIX *m, D3DMATRIX *out)
{
    const float *a = &m->m[0][0];
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0.0f)
        return FALSE;
    for (UINT i = 0; i < 16; i++)
        (&out->m[0][0])[i] = inv[i] / det;
    return TRUE;
}

static void color4(D3DCOLOR c, float out[4])
{
    out[0] = ((c >> 16) & 0xFF) / 255.0f;
    out[1] = ((c >> 8) & 0xFF) / 255.0f;
    out[2] = (c & 0xFF) / 255.0f;
    out[3] = (c >> 24) / 255.0f;
}

static float fstate(DWORD v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void D9UpdateFFConstants(D9Device *d)
{
    float(*c)[4] = d->ff_vs_consts;
    const DWORD *rs = d->render_states;
    memset(d->ff_vs_consts, 0, sizeof(d->ff_vs_consts));
    const D3DMATRIX *world = &d->transforms[256], *view = &d->transforms[D3DTS_VIEW];
    const D3DMATRIX *proj = &d->transforms[D3DTS_PROJECTION];
    D3DMATRIX wv, wvp, inv;
    multiply(world, view, &wv);
    multiply(&wv, proj, &wvp);
    columns(&wvp, &c[C_WVP], 4);
    columns(&wv, &c[C_WV], 4);
    if (invert(&wv, &inv))
        for (UINT i = 0; i < 3; i++)
            for (UINT j = 0; j < 3; j++)
                c[C_NORMAL + i][j] = inv.m[i][j];
    const D3DMATERIAL9 *m = &d->material;
    memcpy(c[C_MAT], &m->Diffuse, 16);
    memcpy(c[C_MAT + 1], &m->Ambient, 16);
    memcpy(c[C_MAT + 2], &m->Specular, 16);
    memcpy(c[C_MAT + 3], &m->Emissive, 16);
    c[C_MAT + 4][0] = m->Power;
    color4(rs[D3DRS_AMBIENT], c[C_AMBIENT]);
    float start = fstate(rs[D3DRS_FOGSTART]), end = fstate(rs[D3DRS_FOGEND]);
    c[C_FOG][0] = start, c[C_FOG][1] = end, c[C_FOG][2] = fstate(rs[D3DRS_FOGDENSITY]);
    c[C_FOG][3] = end != start ? 1.0f / (end - start) : 0.0f;
    c[C_POINT][0] = fstate(rs[D3DRS_POINTSIZE]);
    c[C_POINT][3] = (float)d->viewport.Height;
    c[C_POINT_SCALE][0] = fstate(rs[D3DRS_POINTSCALE_A]);
    c[C_POINT_SCALE][1] = fstate(rs[D3DRS_POINTSCALE_B]);
    c[C_POINT_SCALE][2] = fstate(rs[D3DRS_POINTSCALE_C]);
    columns(proj, &c[C_PROJ], 4);
    for (UINT i = 0, n = 0; i < d->light_count && n < 8; i++) {
        if (!d->lights[i].enabled)
            continue;
        const D3DLIGHT9 *l = &d->lights[i].light;
        float(*lc)[4] = &c[C_LIGHTS + n++ * 7];
        memcpy(lc[0], &l->Diffuse, 16);
        memcpy(lc[1], &l->Specular, 16);
        memcpy(lc[2], &l->Ambient, 16);
        /* the position and direction in view space. */
        const float p[4] = {l->Position.x, l->Position.y, l->Position.z, 1}, dr[4] = {l->Direction.x, l->Direction.y,
                                                                                    l->Direction.z, 0};
        float len = 0;
        for (UINT j = 0; j < 4; j++) {
            lc[3][j] = p[0] * view->m[0][j] + p[1] * view->m[1][j] + p[2] * view->m[2][j] + p[3] * view->m[3][j];
            lc[4][j] = dr[0] * view->m[0][j] + dr[1] * view->m[1][j] + dr[2] * view->m[2][j];
        }
        len = lc[4][0] * lc[4][0] + lc[4][1] * lc[4][1] + lc[4][2] * lc[4][2];
        if (len > 0) {
            float inv_len = 1.0f / root(len);
            for (UINT j = 0; j < 3; j++)
                lc[4][j] *= inv_len;
        }
        lc[5][0] = l->Attenuation0, lc[5][1] = l->Attenuation1, lc[5][2] = l->Attenuation2, lc[5][3] = l->Range;
        lc[6][0] = l->Falloff;
        lc[6][1] = cosine(l->Theta / 2), lc[6][2] = cosine(l->Phi / 2);
    }
    for (UINT i = 0; i < 8; i++) {
        D3DMATRIX tm = d->transforms[D3DTS_TEXTURE0 + i];
        columns(&tm, &c[C_TEXMAT + i * 4], 4);
    }
    for (UINT i = 0; i < 9 && C_BLEND + i * 4 + 4 <= C_SCREEN; i++) {
        D3DMATRIX bw;
        multiply(&d->transforms[256 + i], view, &bw);
        columns(&bw, &c[C_BLEND + i * 4], 4);
    }
    /* screen to clip space: x 2 / w - 1 - 2 x0 / w, y -2 / h + 1 + 2 y0 / h. */
    const D3DVIEWPORT9 *vp = &d->viewport;
    float sw = vp->Width ? 2.0f / vp->Width : 0, sh = vp->Height ? -2.0f / vp->Height : 0;
    c[C_SCREEN][0] = sw, c[C_SCREEN][1] = sh;
    c[C_SCREEN][2] = -1.0f - vp->X * sw, c[C_SCREEN][3] = 1.0f - vp->Y * sh;
    c[C_K][1] = 1, c[C_K][2] = 0.5f, c[C_K][3] = 2;
    c[C_K2][0] = 4, c[C_K2][1] = 255, c[C_K2][2] = 1.442695f;

    float(*p)[4] = d->ff_ps_consts;
    memset(d->ff_ps_consts, 0, sizeof(d->ff_ps_consts));
    color4(rs[D3DRS_TEXTUREFACTOR], p[PC_TFACTOR]);
    p[PC_K][1] = 1, p[PC_K][2] = 0.5f, p[PC_K][3] = 4;
    p[PC_K2][0] = 2;
    for (UINT i = 0; i < 8; i++) {
        const DWORD *ts = d->stage_states[i];
        color4(ts[D3DTSS_CONSTANT], p[PC_CONSTANT + i]);
        p[PC_BUMP + i][0] = fstate(ts[D3DTSS_BUMPENVMAT00]);
        p[PC_BUMP + i][1] = fstate(ts[D3DTSS_BUMPENVMAT01]);
        p[PC_BUMP + i][2] = fstate(ts[D3DTSS_BUMPENVMAT10]);
        p[PC_BUMP + i][3] = fstate(ts[D3DTSS_BUMPENVMAT11]);
        p[PC_LUMINANCE + i][0] = fstate(ts[D3DTSS_BUMPENVLSCALE]);
        p[PC_LUMINANCE + i][1] = fstate(ts[D3DTSS_BUMPENVLOFFSET]);
    }
}

/* fixed function's user clip planes are in world space: in clip space through the inverse of view projection. */
void D9FFClipPlanes(const D9Device *d, float out[6][4])
{
    D3DMATRIX vp, inv;
    multiply(&d->transforms[D3DTS_VIEW], &d->transforms[D3DTS_PROJECTION], &vp);
    if (!invert(&vp, &inv)) {
        memcpy(out, d->clip_planes, sizeof(d->clip_planes));
        return;
    }
    for (UINT i = 0; i < 6; i++)
        for (UINT j = 0; j < 4; j++)
            out[i][j] = d->clip_planes[i][0] * inv.m[j][0] + d->clip_planes[i][1] * inv.m[j][1] +
                        d->clip_planes[i][2] * inv.m[j][2] + d->clip_planes[i][3] * inv.m[j][3];
}
