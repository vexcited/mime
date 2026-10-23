/*
 * effect expressions: state assignments fxc could not fold, run by a small
 * virtual machine. a container holds the program in FXLC, its literals in CLI4
 * and the variables it reads in CTAB. each instruction applies an operation to
 * some components: inputs are literals, variables or temporaries, indexed
 * through literals or variables, and the output is a temporary or the state.
 */
#include "effect_private.h"
#include "../lib/dxbc/dxbc.h"

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }

/* operations, from the high bits of an instruction token. */
enum {
    OP_MOV = 0x100,
    OP_NEG = 0x101,
    OP_RCP = 0x103,
    OP_FRC = 0x104,
    OP_EXP = 0x105,
    OP_LOG = 0x106,
    OP_RSQ = 0x107,
    OP_SIN = 0x108,
    OP_COS = 0x109,
    OP_ASIN = 0x10A,
    OP_ACOS = 0x10B,
    OP_ATAN = 0x10C,
    OP_SQRT = 0x112,
    OP_INEG = 0x120,
    OP_NOT = 0x121,
    OP_ITOF = 0x130,
    OP_UTOF = 0x131,
    OP_FTOI = 0x132,
    OP_FTOU = 0x133,
    OP_BTOF = 0x134,
    OP_BTOI = 0x135,
    OP_ITOB = 0x136,
    OP_FTOB = 0x137,
    OP_FLOOR = 0x139,
    OP_CEIL = 0x13A,
    OP_MIN = 0x200,
    OP_MAX = 0x201,
    OP_LT = 0x202,
    OP_GE = 0x203,
    OP_ADD = 0x204,
    OP_MUL = 0x205,
    OP_ATAN2 = 0x206,
    OP_DIV = 0x208,
    OP_ILT = 0x210,
    OP_IGE = 0x211,
    OP_IEQ = 0x212,
    OP_INE = 0x213,
    OP_UGE = 0x214,
    OP_ULT = 0x215,
    OP_IADD = 0x216,
    OP_IDIV = 0x217,
    OP_IMUL = 0x219,
    OP_UDIV = 0x21A,
    OP_UMOD = 0x21B,
    OP_UMUL = 0x21C,
    OP_IMIN = 0x21D,
    OP_IMAX = 0x21E,
    OP_UMIN = 0x21F,
    OP_UMAX = 0x220,
    OP_AND = 0x230,
    OP_OR = 0x231,
    OP_XOR = 0x233,
    OP_SHL = 0x234,
    OP_ISHR = 0x235,
    OP_USHR = 0x236,
    OP_CMP = 0x300,
    OP_MOVC = 0x301,
    OP_DOT = 0x500,
    OP_DOTSWIZ = 0x70E,
};

/* operand register files. */
enum { R_LITERAL = 1, R_VARIABLE = 2, R_OUTPUT = 4, R_TEMP = 7 };

#define TEMPS 128

struct FxExpression {
    const UINT *code;
    UINT code_size;
    const UINT *literals;
    UINT literal_count;
    /* one pointer per register component, NULL past a variable's bytes. */
    UINT *const *table;
    UINT table_size;
};

/* 0 for an unknown operation, ~0 for any count. */
static UINT inputs_of(UINT op)
{
    switch (op) {
    case OP_MOV:
    case OP_NEG:
    case OP_RCP:
    case OP_FRC:
    case OP_EXP:
    case OP_LOG:
    case OP_RSQ:
    case OP_SIN:
    case OP_COS:
    case OP_ASIN:
    case OP_ACOS:
    case OP_ATAN:
    case OP_SQRT:
    case OP_INEG:
    case OP_NOT:
    case OP_ITOF:
    case OP_UTOF:
    case OP_FTOI:
    case OP_FTOU:
    case OP_BTOF:
    case OP_BTOI:
    case OP_ITOB:
    case OP_FTOB:
    case OP_FLOOR:
    case OP_CEIL:
        return 1;
    case OP_MIN:
    case OP_MAX:
    case OP_LT:
    case OP_GE:
    case OP_ADD:
    case OP_MUL:
    case OP_ATAN2:
    case OP_DIV:
    case OP_ILT:
    case OP_IGE:
    case OP_IEQ:
    case OP_INE:
    case OP_UGE:
    case OP_ULT:
    case OP_IADD:
    case OP_IDIV:
    case OP_IMUL:
    case OP_UDIV:
    case OP_UMOD:
    case OP_UMUL:
    case OP_IMIN:
    case OP_IMAX:
    case OP_UMIN:
    case OP_UMAX:
    case OP_AND:
    case OP_OR:
    case OP_XOR:
    case OP_SHL:
    case OP_ISHR:
    case OP_USHR:
    case OP_DOT:
        return 2;
    case OP_CMP:
    case OP_MOVC:
        return 3;
    case OP_DOTSWIZ:
        return ~0u;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ math */

/* math in doubles, done here since there is no c runtime or 32-bit msvcrt.dll to call. */
#define PI 3.14159265358979323846
#define PI_2_HI 1.57079632673412561417
#define PI_2_LO 6.07710050650619224932e-11

static double abs_d(double x) { return x < 0 ? -x : x; }

static float floor_of(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    float t = (float)(INT)x;
    return t > x ? t - 1.0f : t;
}

static float ceil_of(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    float t = (float)(INT)x;
    return t < x ? t + 1.0f : t;
}

static float abs_of(float x) { return x < 0 ? -x : x; }

/* sine and cosine of r in [-pi/4, pi/4]. */
static double sin_poly(double r)
{
    double r2 = r * r;
    return r *
           (1 + r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 + r2 * -1.0 / 39916800)))));
}

static double cos_poly(double r)
{
    double r2 = r * r;
    return 1 + r2 * (-0.5 +
                     r2 * (1.0 / 24 + r2 * (-1.0 / 720 + r2 * (1.0 / 40320 + r2 * (-1.0 / 3628800 + r2 / 479001600)))));
}

/* reduces x by multiples of pi/2 and gives back the quadrant. NaN past the integer range. */
static double reduce(double x, UINT *quadrant)
{
    double k = x * (2 / PI);
    if (!(abs_d(k) < 2147483647.0))
        return *quadrant = 0, x - x;
    INT n = (INT)(k < 0 ? k - 0.5 : k + 0.5);
    *quadrant = (UINT)n & 3;
    return (x - n * PI_2_HI) - n * PI_2_LO;
}

static double sin_d(double x)
{
    UINT q;
    double r = reduce(x, &q);
    double v = q & 1 ? cos_poly(r) : sin_poly(r);
    return q & 2 ? -v : v;
}

static double cos_d(double x)
{
    UINT q;
    double r = reduce(x, &q);
    double v = q & 1 ? sin_poly(r) : cos_poly(r);
    return (q + 1) & 2 ? -v : v;
}

/* the argument is halved twice, then the series runs. */
static double atan_d(double x)
{
    if (x != x)
        return x;
    BOOL invert = abs_d(x) > 1;
    double t = invert ? 1 / x : x;
    for (UINT i = 0; i < 2; i++)
        t = t / (1 + __builtin_elementwise_sqrt(1 + t * t));
    double t2 = t * t, sum = 0;
    for (INT k = 15; k >= 1; k -= 2)
        sum = 1.0 / k - t2 * sum;
    double a = 4 * t * sum;
    if (!invert)
        return a;
    return (x > 0 ? PI / 2 : -PI / 2) - a;
}

static double atan2_d(double y, double x)
{
    if (x != x || y != y)
        return x + y;
    if (x == 0)
        return y > 0 ? PI / 2 : y < 0 ? -PI / 2 : 0;
    double a = atan_d(y / x);
    if (x > 0)
        return a;
    return y < 0 ? a - PI : a + PI;
}

/* the integer part of x goes to the exponent, the fraction f through the series for e to the f ln 2. */
static double exp2_d(double x)
{
    if (x != x)
        return x;
    if (x > 1024)
        return 1.0 / 0.0;
    if (x < -1080)
        return 0;
    INT n = (INT)(x < 0 ? x - 0.5 : x + 0.5);
    double f = (x - n) * 0.69314718055994530942, term = 1, sum = 1;
    for (UINT k = 1; k < 14; k++) {
        term *= f / k;
        sum += term;
    }
    for (; n > 0; n--)
        sum *= 2;
    for (; n < 0; n++)
        sum *= 0.5;
    return sum;
}

/* x must be positive: its exponent plus an atanh series over the mantissa near 1. */
static double log2_d(double x)
{
    if (!(x > 0))
        return x == 0 ? -1.0 / 0.0 : (x - x) / 0.0;
    INT e = 0;
    while (x >= 1.41421356237309504880)
        x *= 0.5, e++;
    while (x < 0.70710678118654752440)
        x *= 2, e--;
    double s = (x - 1) / (x + 1), s2 = s * s, sum = 0;
    for (INT k = 21; k >= 1; k -= 2)
        sum = 1.0 / k + s2 * sum;
    return e + 2 * s * sum * 1.44269504088896340736;
}

/* ------------------------------------------------------------------ loading */

/* operand layout: an index count, then register file and index pairs, the last pair being the operand itself. */
static BOOL check_operand(const UINT *code, UINT size, UINT *pos, BOOL output, UINT literals, UINT table)
{
    if (*pos >= size)
        return FALSE;
    UINT n = code[(*pos)++];
    if (output && n)
        return FALSE;
    if (n > 4 || size - *pos < 2 * (n + 1))
        return FALSE;
    for (UINT i = 0; i <= n; i++) {
        UINT file = code[*pos + 2 * i], index = code[*pos + 2 * i + 1];
        BOOL last = i == n;
        if (output ? file != R_OUTPUT && file != R_TEMP
                   : file != R_LITERAL && file != R_VARIABLE && (file != R_TEMP || !last))
            return FALSE;
        UINT limit = file == R_LITERAL ? literals : file == R_VARIABLE ? table : file == R_OUTPUT ? 4 : TEMPS;
        if (index >= limit)
            return FALSE;
    }
    *pos += 2 * (n + 1);
    return TRUE;
}

/* variables are found by name, their components laid out as consecutive registers of four. */
HRESULT FxParseExpression(FxEffect *e, const BYTE *blob, UINT size, FxExpression **out)
{
    uint32_t code_size, literal_size, table_size;
    if (!D3d10ContainerValid(blob, size))
        return E_FAIL;
    const BYTE *code = dxbc_find_chunk(blob, size, DXBC_FOURCC('F', 'X', 'L', 'C'), &code_size);
    const BYTE *literals = dxbc_find_chunk(blob, size, DXBC_FOURCC('C', 'L', 'I', '4'), &literal_size);
    const BYTE *ctab = dxbc_find_chunk(blob, size, DXBC_FOURCC('C', 'T', 'A', 'B'), &table_size);
    if (!code || !literals || !ctab || code_size < 4 || literal_size < 4 || table_size < 28 || (code_size & 3))
        return E_FAIL;
    FxExpression *x = FxAlloc(e, sizeof(*x));
    if (!x)
        return E_OUTOFMEMORY;
    x->code = (const UINT *)code;
    x->code_size = code_size / 4;
    x->literal_count = read32(literals);
    if (x->literal_count > (literal_size - 4) / 4)
        return E_FAIL;
    x->literals = (const UINT *)(literals + 4);

    /* constant table entries: name, register set, index, count, type and default. */
    UINT constants = read32(ctab + 12), info = read32(ctab + 16);
    if (info > table_size || (UINT64)constants * 20 > table_size - info)
        return E_FAIL;
    UINT registers = 0;
    for (UINT i = 0; i < constants; i++) {
        const BYTE *c = ctab + info + 20 * i;
        UINT end = (c[6] | c[7] << 8) + (c[8] | c[9] << 8);
        registers = registers > end ? registers : end;
    }
    x->table_size = 4 * registers;
    UINT **table = x->table_size ? FxAlloc(e, x->table_size * sizeof(*table)) : nullptr;
    if (x->table_size && !table)
        return E_OUTOFMEMORY;
    x->table = table;
    for (UINT i = 0; i < constants; i++) {
        const BYTE *c = ctab + info + 20 * i;
        UINT name = read32(c), first = c[6] | c[7] << 8, count = c[8] | c[9] << 8;
        const char *s = (const char *)ctab + name;
        if (name >= table_size || strnlen(s, table_size - name) == table_size - name)
            return E_FAIL;
        FxVariable *v = FxFindVariable(e, s);
        if (!v || (v->type->kind != FX_NUMERIC && v->type->kind != FX_STRUCT))
            return E_FAIL;
        for (UINT k = 0; k < 4 * count; k++)
            table[4 * first + k] = k < v->type->size / 4 ? (UINT *)(v->data + 4 * k) : nullptr;
    }

    /* each instruction's operation, inputs and output must be in range. */
    const UINT *p = x->code;
    UINT pos = 1, instructions = p[0];
    for (UINT i = 0; i < instructions; i++) {
        if (x->code_size - pos < 2)
            return E_FAIL;
        UINT token = p[pos], op = (token >> 20) & 0x7FF, inputs = p[pos + 1], expected = inputs_of(op);
        if (!expected || (expected != ~0u && inputs != expected) || inputs > 8 || (token & 0xFFFFF) > 4 * TEMPS)
            return E_FAIL;
        pos += 2;
        for (UINT k = 0; k < inputs; k++)
            if (!check_operand(p, x->code_size, &pos, FALSE, x->literal_count, x->table_size))
                return E_FAIL;
        if (!check_operand(p, x->code_size, &pos, TRUE, x->literal_count, x->table_size))
            return E_FAIL;
    }
    *out = x;
    return S_OK;
}

/* ------------------------------------------------------------------ running */

static UINT zero[4 * TEMPS];

/* the indices add up, each scaled by four. */
static const UINT *input_at(const FxExpression *x, const UINT **pos, const UINT *temps)
{
    const UINT *p = *pos;
    UINT n = *p++, index = 0;
    for (UINT i = 0; i < n; i++, p += 2) {
        UINT at = p[1] + 4 * index;
        if (p[0] == R_LITERAL)
            index = at < x->literal_count ? x->literals[at] : 0;
        else if (p[0] == R_VARIABLE) {
            float f;
            memcpy(&f, at < x->table_size && x->table[at] ? x->table[at] : zero, 4);
            index = (UINT)(INT)f;
        } else {
            index = at < TEMPS ? temps[at] : 0;
        }
    }
    UINT file = p[0], at = p[1] + 4 * index;
    *pos = p + 2;
    if (file == R_LITERAL)
        return at < x->literal_count ? x->literals + at : zero;
    if (file == R_VARIABLE)
        return at < x->table_size && x->table[at] ? x->table[at] : zero;
    return at < TEMPS ? temps + at : zero;
}

static float f_of(UINT v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static UINT u_of(float f)
{
    UINT v;
    memcpy(&v, &f, 4);
    return v;
}

static UINT binary(UINT op, UINT a, UINT b)
{
    float fa = f_of(a), fb = f_of(b);
    INT ia = (INT)a, ib = (INT)b;
    switch (op) {
    case OP_MIN:
        return u_of(fa < fb || fb != fb ? fa : fb);
    case OP_MAX:
        return u_of(fa > fb || fb != fb ? fa : fb);
    case OP_LT:
        return u_of(fa < fb ? 1.0f : 0.0f);
    case OP_GE:
        return u_of(fa >= fb ? 1.0f : 0.0f);
    case OP_ADD:
        return u_of(fa + fb);
    case OP_MUL:
        return u_of(fa * fb);
    case OP_ATAN2:
        return u_of((float)atan2_d(fa, fb));
    case OP_DIV:
        return u_of(fa / fb);
    case OP_ILT:
        return ia < ib ? ~0u : 0;
    case OP_IGE:
        return ia >= ib ? ~0u : 0;
    case OP_IEQ:
        return a == b ? ~0u : 0;
    case OP_INE:
        return a != b ? ~0u : 0;
    case OP_UGE:
        return a >= b ? ~0u : 0;
    case OP_ULT:
        return a < b ? ~0u : 0;
    case OP_IADD:
        return a + b;
    case OP_IDIV:
        return b ? (UINT)(ib == -1 ? -(INT)a : ia / ib) : ~0u;
    case OP_IMUL:
    case OP_UMUL:
        return a * b;
    case OP_UDIV:
        return b ? a / b : ~0u;
    case OP_UMOD:
        return b ? a % b : ~0u;
    case OP_IMIN:
        return (UINT)(ia < ib ? ia : ib);
    case OP_IMAX:
        return (UINT)(ia > ib ? ia : ib);
    case OP_UMIN:
        return a < b ? a : b;
    case OP_UMAX:
        return a > b ? a : b;
    case OP_AND:
        return a & b;
    case OP_OR:
        return a | b;
    case OP_XOR:
        return a ^ b;
    case OP_SHL:
        return a << (b & 31);
    case OP_ISHR:
        return (UINT)(ia >> (b & 31));
    default:
        return a >> (b & 31);
    }
}

static UINT unary(UINT op, UINT a)
{
    float f = f_of(a);
    switch (op) {
    case OP_MOV:
        return a;
    case OP_NEG:
        return u_of(-f);
    case OP_RCP:
        return u_of(1.0f / f);
    case OP_FRC:
        return u_of(f - floor_of(f));
    case OP_EXP:
        return u_of((float)exp2_d(f));
    case OP_LOG:
        return u_of(f == 0.0f ? 0.0f : (float)log2_d(abs_of(f)));
    case OP_RSQ:
        return u_of(1.0f / __builtin_elementwise_sqrt(abs_of(f)));
    case OP_SIN:
        return u_of((float)sin_d(f));
    case OP_COS:
        return u_of((float)cos_d(f));
    case OP_ASIN:
        return u_of(abs_of(f) > 1 ? (f - f) / 0.0f : (float)atan2_d(f, __builtin_elementwise_sqrt(1 - (double)f * f)));
    case OP_ACOS:
        return u_of(abs_of(f) > 1 ? (f - f) / 0.0f : (float)atan2_d(__builtin_elementwise_sqrt(1 - (double)f * f), f));
    case OP_ATAN:
        return u_of((float)atan_d(f));
    case OP_SQRT:
        return u_of(__builtin_elementwise_sqrt(abs_of(f)));
    case OP_INEG:
        return (UINT) - (INT)a;
    case OP_NOT:
        return ~a;
    case OP_ITOF:
        return u_of((float)(INT)a);
    case OP_UTOF:
        return u_of((float)a);
    case OP_FTOI:
        return (UINT)(INT)f;
    case OP_FTOU:
        return f > 0.0f ? (UINT)f : 0;
    case OP_BTOF:
        return u_of(a ? 1.0f : 0.0f);
    case OP_BTOI:
        return a != 0;
    case OP_ITOB:
        return a ? ~0u : 0;
    case OP_FTOB:
        return f != 0.0f ? ~0u : 0;
    case OP_FLOOR:
        return u_of(floor_of(f));
    default:
        return u_of(ceil_of(f));
    }
}

/* out holds four components. */
void FxRunExpression(const FxExpression *x, void *out)
{
    UINT temps[TEMPS] = {};
    UINT *result = out;
    const UINT *p = x->code + 1;
    for (UINT i = 0; i < x->code[0]; i++) {
        UINT token = *p++, inputs = *p++, op = (token >> 20) & 0x7FF, n = token & 0xFFFFF;
        BOOL scalar = token >> 31;
        const UINT *in[8] = {};
        for (UINT k = 0; k < inputs; k++)
            in[k] = input_at(x, &p, temps);
        UINT file = p[1], index = p[2];
        p += 3;
        UINT *dst = file == R_OUTPUT ? result + index : temps + index;
        UINT room = file == R_OUTPUT ? 4 - index : TEMPS - index;
        UINT value[4 * TEMPS];
        n = n < room ? n : room;
        if (op == OP_DOT || op == OP_DOTSWIZ) {
            float sum = 0;
            if (op == OP_DOT)
                for (UINT c = 0; c < n; c++)
                    sum += f_of(in[0][scalar ? 0 : c]) * f_of(in[1][c]);
            else
                for (UINT c = 0; c < inputs / 2; c++)
                    sum += f_of(in[c][0]) * f_of(in[c + inputs / 2][0]);
            if (room)
                *dst = u_of(sum);
            continue;
        }
        for (UINT c = 0; c < n; c++) {
            UINT a = in[0][scalar ? 0 : c];
            if (op == OP_CMP)
                value[c] = f_of(a) >= 0.0f ? in[1][c] : in[2][c];
            else if (op == OP_MOVC)
                value[c] = a ? in[1][c] : in[2][c];
            else if (inputs == 2)
                value[c] = binary(op, a, in[1][c]);
            else
                value[c] = unary(op, a);
        }
        memcpy(dst, value, 4 * n);
    }
}
