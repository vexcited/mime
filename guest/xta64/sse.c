/*
 * sse up to sse4.1, ssse3 and mmx. integer simd operations are written
 * once over 8 or 16 bytes and shared by the mmx and xmm forms.
 */
#include "internal.h"

/* ------------------------------------------------------------ operands */

static void load_xmm_src(X86Cpu *c, const X86Insn *i, X86Xmm *s, int bytes)
{
    if (i->mod == 3) {
        *s = c->xmm[i->rm];
        return;
    }
    uint64_t a = x86_ea(c, i);
    __builtin_memset(s, 0, sizeof(*s));
    __builtin_memcpy(s, (const void *)(uintptr_t)a, (size_t)bytes);
}

static void load_mm_src(X86Cpu *c, const X86Insn *i, X86Xmm *s)
{
    __builtin_memset(s, 0, sizeof(*s));
    if (i->mod == 3)
        s->q[0] = c->mm[i->rm & 7];
    else
        s->q[0] = rd64(x86_ea(c, i));
}

static inline void enter_mmx(X86Cpu *c)
{
    c->mmx_mode = 1;
    c->ftop = 0;
    c->ftag = 0xFF;
}

static inline uint64_t gpr_src(X86Cpu *c, const X86Insn *i, int size)
{
    if (i->mod == 3)
        return size == 8 ? c->r[i->rm] : (uint32_t)c->r[i->rm];
    return rdn(x86_ea(c, i), size);
}

/* ------------------------------------------------------------ floating point */

#define F32_QNAN 0xFFC00000u
#define F64_QNAN 0xFFF8000000000000ull

static inline uint32_t f2u(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float u2f(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
static inline uint64_t d2u(double d) { uint64_t u; __builtin_memcpy(&u, &d, 8); return u; }
static inline double u2d(uint64_t u) { double d; __builtin_memcpy(&d, &u, 8); return d; }

static inline int isnan_f(float f) { return f != f; }
static inline int isnan_d(double d) { return d != d; }

/* x86 nan rules: the first nan operand quieted, else the negative default nan. */
static inline float nan_f(float r, float a, float b)
{
    if (!isnan_f(r))
        return r;
    if (isnan_f(a))
        return u2f(f2u(a) | 0x00400000u);
    if (isnan_f(b))
        return u2f(f2u(b) | 0x00400000u);
    return u2f(F32_QNAN);
}

static inline double nan_d(double r, double a, double b)
{
    if (!isnan_d(r))
        return r;
    if (isnan_d(a))
        return u2d(d2u(a) | 0x0008000000000000ull);
    if (isnan_d(b))
        return u2d(d2u(b) | 0x0008000000000000ull);
    return u2d(F64_QNAN);
}

static float arith_f(int op, float a, float b)
{
    float r;
    switch (op) {
    case 0x58: r = a + b; break;
    case 0x59: r = a * b; break;
    case 0x5C: r = a - b; break;
    case 0x5E: r = a / b; break;
    case 0x5D: return (isnan_f(a) || isnan_f(b) || !(a < b)) ? b : a; /* min */
    case 0x5F: return (isnan_f(a) || isnan_f(b) || !(a > b)) ? b : a; /* max */
    case 0x51: r = __builtin_sqrtf(b); return isnan_f(r) ? (isnan_f(b) ? u2f(f2u(b) | 0x00400000u) : u2f(F32_QNAN)) : r;
    case 0x52: return 1.0f / __builtin_sqrtf(b); /* rsqrt, exact instead of approximate. */
    case 0x53: return 1.0f / b;                  /* rcp */
    default: return a;
    }
    return nan_f(r, a, b);
}

static double arith_d(int op, double a, double b)
{
    double r;
    switch (op) {
    case 0x58: r = a + b; break;
    case 0x59: r = a * b; break;
    case 0x5C: r = a - b; break;
    case 0x5E: r = a / b; break;
    case 0x5D: return (isnan_d(a) || isnan_d(b) || !(a < b)) ? b : a;
    case 0x5F: return (isnan_d(a) || isnan_d(b) || !(a > b)) ? b : a;
    case 0x51: r = __builtin_sqrt(b); return isnan_d(r) ? (isnan_d(b) ? u2d(d2u(b) | 0x0008000000000000ull) : u2d(F64_QNAN)) : r;
    default: return a;
    }
    return nan_d(r, a, b);
}

static int compare(int pred, double a, double b)
{
    int unordered = isnan_d(a) || isnan_d(b);
    switch (pred & 7) {
    case 0: return a == b;
    case 1: return a < b;
    case 2: return a <= b;
    case 3: return unordered;
    case 4: return !(a == b);
    case 5: return !(a < b);
    case 6: return !(a <= b);
    default: return !unordered;
    }
}

static inline double round_mode(double v, int mode)
{
    switch (mode & 3) {
    case 0: return __builtin_roundeven(v);
    case 1: return __builtin_floor(v);
    case 2: return __builtin_ceil(v);
    default: return __builtin_trunc(v);
    }
}

/* conversion to integer: the indefinite value and the invalid flag when out of range. */
static int64_t to_int(X86Cpu *c, double v, int size, int mode)
{
    double r = isnan_d(v) ? v : round_mode(v, mode);
    if (isnan_d(v) || (size == 8 ? r >= 9223372036854775808.0 || r < -9223372036854775808.0 :
                                   r >= 2147483648.0 || r < -2147483648.0)) {
        c->mxcsr |= X86_MXCSR_IE;
        return size == 8 ? INT64_MIN : INT32_MIN;
    }
    return size == 8 ? (int64_t)r : (int32_t)r;
}

static void set_compare_flags(X86Cpu *c, double a, double b)
{
    uint32_t f;
    if (isnan_d(a) || isnan_d(b))
        f = X86_ZF | X86_PF | X86_CF;
    else if (a < b)
        f = X86_CF;
    else if (a > b)
        f = 0;
    else
        f = X86_ZF;
    c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
    c->lf_op = X86_LF_NONE;
}

/*
 * the exception flags of mxcsr are the ones arm64 collects in fpsr, as the floating point of the interpreter and of
 * translated code runs on the host: invalid, divide by zero, overflow, underflow and inexact are the same bits in
 * another order. they are folded in when mxcsr is read and cleared with it when it is written.
 */
static const struct {
    uint32_t fpsr, mxcsr;
} g_host_flags[] = {{1u << 0, X86_MXCSR_IE}, {1u << 1, 1u << 2}, {1u << 2, 1u << 3}, {1u << 3, 1u << 4}, {1u << 4, 1u << 5}};

uint32_t x86_mxcsr(X86Cpu *c)
{
    uint64_t fpsr;
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    for (unsigned k = 0; k < sizeof(g_host_flags) / sizeof(g_host_flags[0]); k++)
        if (fpsr & g_host_flags[k].fpsr)
            c->mxcsr |= g_host_flags[k].mxcsr;
    return c->mxcsr;
}

/*
 * the rounding and flushing of mxcsr become fpcr's, as the host runs the program's floating point: x86 rounds down
 * and up where arm64 numbers them up and down. arm64 flushes inputs and results alike, which is mxcsr's flush to zero
 * with denormals are zero.
 */
uint32_t x86_jit_afp;

/* the bits read back as written only where the processor has them. */
void x86_probe_host(void)
{
    uint64_t old, now;
    __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    __asm__ volatile("msr fpcr, %0" ::"r"(old | 6));
    __asm__ volatile("mrs %0, fpcr" : "=r"(now));
    __asm__ volatile("msr fpcr, %0" ::"r"(old));
    x86_jit_afp = (now & 6) == 6;
}

void x86_set_mxcsr(X86Cpu *c, uint32_t v)
{
    static const uint64_t ROUNDING[4] = {0, 2, 1, 3};
    uint64_t fpsr, fpcr;
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    fpsr &= ~(uint64_t)0x9F;
    __asm__ volatile("msr fpsr, %0" ::"r"(fpsr));
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr &= ~((3ull << 22) | (1ull << 24) | (uint64_t)x86_jit_afp);
    fpcr |= ROUNDING[(v >> 13) & 3] << 22;
    /* with AH set, as translated code runs, FZ only flushes results: FIZ flushes the inputs too then. */
    if ((v & 0x8040) == 0x8040)
        fpcr |= 1ull << 24 | (uint64_t)x86_jit_afp;
    __asm__ volatile("msr fpcr, %0" ::"r"(fpcr));
    c->mxcsr = v;
}

void x86_ldmxcsr(X86Cpu *c, uint32_t v)
{
    if (v & 0xFFFF0000u)
        x86_fault(c, X86_STATUS_ACCESS_VIOLATION, 0, ~0ull);
    x86_set_mxcsr(c, v);
}

/* ------------------------------------------------------------ integer simd */

static inline int8_t sat8(int v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
static inline uint8_t satu8(int v) { return (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v); }
static inline int16_t sat16(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static inline uint16_t satu16(int v) { return (uint16_t)(v > 65535 ? 65535 : v < 0 ? 0 : v); }

static void shift_lanes(X86Xmm *d, int n, int esize, uint64_t count, int kind)
{
    /* kind: 0 logical right, 1 arithmetic right, 2 left. */
    int bits = esize * 8;
    int lanes = n / esize;
    for (int k = 0; k < lanes; k++) {
        switch (esize) {
        case 2:
            if (kind == 1)
                d->sw[k] = (int16_t)(d->sw[k] >> (count > 15 ? 15 : count));
            else if (count >= (uint64_t)bits)
                d->w[k] = 0;
            else
                d->w[k] = kind ? (uint16_t)(d->w[k] << count) : (uint16_t)(d->w[k] >> count);
            break;
        case 4:
            if (kind == 1)
                d->sd[k] = d->sd[k] >> (count > 31 ? 31 : count);
            else if (count >= (uint64_t)bits)
                d->d[k] = 0;
            else
                d->d[k] = kind ? d->d[k] << count : d->d[k] >> count;
            break;
        default:
            if (count >= (uint64_t)bits)
                d->q[k] = 0;
            else
                d->q[k] = kind ? d->q[k] << count : d->q[k] >> count;
            break;
        }
    }
}

static void byte_shift(X86Xmm *d, unsigned count, int left)
{
    X86Xmm t;
    __builtin_memset(&t, 0, sizeof(t));
    if (count < 16) {
        for (int k = 0; k < 16; k++) {
            int from = left ? k - (int)count : k + (int)count;
            if (from >= 0 && from < 16)
                t.b[k] = d->b[from];
        }
    }
    *d = t;
}

static void pack(X86Xmm *d, const X86Xmm *s, int n, int kind)
{
    /* kind: 0 packsswb, 1 packuswb, 2 packssdw, 3 packusdw. */
    X86Xmm t;
    __builtin_memset(&t, 0, sizeof(t));
    if (kind < 2) {
        int lanes = n / 2;
        for (int k = 0; k < lanes; k++) {
            int a = d->sw[k], b = s->sw[k];
            if (kind == 0) {
                t.sb[k] = sat8(a);
                t.sb[k + lanes] = sat8(b);
            } else {
                t.b[k] = satu8(a);
                t.b[k + lanes] = satu8(b);
            }
        }
    } else {
        int lanes = n / 4;
        for (int k = 0; k < lanes; k++) {
            int64_t a = d->sd[k], b = s->sd[k];
            if (kind == 2) {
                t.sw[k] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
                t.sw[k + lanes] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
            } else {
                t.w[k] = (uint16_t)(a > 65535 ? 65535 : a < 0 ? 0 : a);
                t.w[k + lanes] = (uint16_t)(b > 65535 ? 65535 : b < 0 ? 0 : b);
            }
        }
    }
    *d = t;
}

static void unpack(X86Xmm *d, const X86Xmm *s, int n, int esize, int high)
{
    X86Xmm t;
    __builtin_memset(&t, 0, sizeof(t));
    int half = n / 2;
    int lanes = half / esize;
    int base = high ? half : 0;
    for (int k = 0; k < lanes; k++) {
        __builtin_memcpy(&t.b[2 * k * esize], &d->b[base + k * esize], (size_t)esize);
        __builtin_memcpy(&t.b[(2 * k + 1) * esize], &s->b[base + k * esize], (size_t)esize);
    }
    *d = t;
}

/*
 * integer operations of the 0f map, shared by mmx with n = 8 and sse2 with
 * n = 16. returns 0 for other opcodes.
 */
static int simd_int(X86Cpu *c, uint8_t op, X86Xmm *d, X86Xmm *s, int n)
{
    int k;
    switch (op) {
    case 0x60: unpack(d, s, n, 1, 0); return 1;
    case 0x61: unpack(d, s, n, 2, 0); return 1;
    case 0x62: unpack(d, s, n, 4, 0); return 1;
    case 0x63: pack(d, s, n, 0); return 1;
    case 0x64: for (k = 0; k < n; k++) d->b[k] = d->sb[k] > s->sb[k] ? 0xFF : 0; return 1;
    case 0x65: for (k = 0; k < n / 2; k++) d->w[k] = d->sw[k] > s->sw[k] ? 0xFFFF : 0; return 1;
    case 0x66: for (k = 0; k < n / 4; k++) d->d[k] = d->sd[k] > s->sd[k] ? ~0u : 0; return 1;
    case 0x67: pack(d, s, n, 1); return 1;
    case 0x68: unpack(d, s, n, 1, 1); return 1;
    case 0x69: unpack(d, s, n, 2, 1); return 1;
    case 0x6A: unpack(d, s, n, 4, 1); return 1;
    case 0x6B: pack(d, s, n, 2); return 1;
    case 0x6C: unpack(d, s, n, 8, 0); return 1;
    case 0x6D: unpack(d, s, n, 8, 1); return 1;
    case 0x74: for (k = 0; k < n; k++) d->b[k] = d->b[k] == s->b[k] ? 0xFF : 0; return 1;
    case 0x75: for (k = 0; k < n / 2; k++) d->w[k] = d->w[k] == s->w[k] ? 0xFFFF : 0; return 1;
    case 0x76: for (k = 0; k < n / 4; k++) d->d[k] = d->d[k] == s->d[k] ? ~0u : 0; return 1;
    case 0xD1: shift_lanes(d, n, 2, s->q[0], 0); return 1;
    case 0xD2: shift_lanes(d, n, 4, s->q[0], 0); return 1;
    case 0xD3: shift_lanes(d, n, 8, s->q[0], 0); return 1;
    case 0xD4: for (k = 0; k < n / 8; k++) d->q[k] += s->q[k]; return 1;
    case 0xD5: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(d->sw[k] * s->sw[k]); return 1;
    case 0xD8: for (k = 0; k < n; k++) d->b[k] = satu8(d->b[k] - s->b[k]); return 1;
    case 0xD9: for (k = 0; k < n / 2; k++) d->w[k] = satu16(d->w[k] - s->w[k]); return 1;
    case 0xDA: for (k = 0; k < n; k++) d->b[k] = d->b[k] < s->b[k] ? d->b[k] : s->b[k]; return 1;
    case 0xDB: for (k = 0; k < n / 8; k++) d->q[k] &= s->q[k]; return 1;
    case 0xDC: for (k = 0; k < n; k++) d->b[k] = satu8(d->b[k] + s->b[k]); return 1;
    case 0xDD: for (k = 0; k < n / 2; k++) d->w[k] = satu16(d->w[k] + s->w[k]); return 1;
    case 0xDE: for (k = 0; k < n; k++) d->b[k] = d->b[k] > s->b[k] ? d->b[k] : s->b[k]; return 1;
    case 0xDF: for (k = 0; k < n / 8; k++) d->q[k] = ~d->q[k] & s->q[k]; return 1;
    case 0xE0: for (k = 0; k < n; k++) d->b[k] = (uint8_t)((d->b[k] + s->b[k] + 1) >> 1); return 1;
    case 0xE1: shift_lanes(d, n, 2, s->q[0], 1); return 1;
    case 0xE2: shift_lanes(d, n, 4, s->q[0], 1); return 1;
    case 0xE3: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)((d->w[k] + s->w[k] + 1) >> 1); return 1;
    case 0xE4: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(((uint32_t)d->w[k] * s->w[k]) >> 16); return 1;
    case 0xE5: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(((int32_t)d->sw[k] * s->sw[k]) >> 16); return 1;
    case 0xE8: for (k = 0; k < n; k++) d->sb[k] = sat8(d->sb[k] - s->sb[k]); return 1;
    case 0xE9: for (k = 0; k < n / 2; k++) d->sw[k] = sat16(d->sw[k] - s->sw[k]); return 1;
    case 0xEA: for (k = 0; k < n / 2; k++) d->sw[k] = d->sw[k] < s->sw[k] ? d->sw[k] : s->sw[k]; return 1;
    case 0xEB: for (k = 0; k < n / 8; k++) d->q[k] |= s->q[k]; return 1;
    case 0xEC: for (k = 0; k < n; k++) d->sb[k] = sat8(d->sb[k] + s->sb[k]); return 1;
    case 0xED: for (k = 0; k < n / 2; k++) d->sw[k] = sat16(d->sw[k] + s->sw[k]); return 1;
    case 0xEE: for (k = 0; k < n / 2; k++) d->sw[k] = d->sw[k] > s->sw[k] ? d->sw[k] : s->sw[k]; return 1;
    case 0xEF: for (k = 0; k < n / 8; k++) d->q[k] ^= s->q[k]; return 1;
    case 0xF1: shift_lanes(d, n, 2, s->q[0], 2); return 1;
    case 0xF2: shift_lanes(d, n, 4, s->q[0], 2); return 1;
    case 0xF3: shift_lanes(d, n, 8, s->q[0], 2); return 1;
    case 0xF4: for (k = 0; k < n / 8; k++) d->q[k] = (uint64_t)d->d[2 * k] * s->d[2 * k]; return 1;
    case 0xF5:
        for (k = 0; k < n / 4; k++)
            d->sd[k] = (int32_t)((int64_t)d->sw[2 * k] * s->sw[2 * k] + (int64_t)d->sw[2 * k + 1] * s->sw[2 * k + 1]);
        return 1;
    case 0xF6:
        for (k = 0; k < n / 8; k++) {
            uint32_t sum = 0;
            for (int b = 0; b < 8; b++) {
                int diff = d->b[k * 8 + b] - s->b[k * 8 + b];
                sum += (uint32_t)(diff < 0 ? -diff : diff);
            }
            d->q[k] = sum;
        }
        return 1;
    case 0xF8: for (k = 0; k < n; k++) d->b[k] = (uint8_t)(d->b[k] - s->b[k]); return 1;
    case 0xF9: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(d->w[k] - s->w[k]); return 1;
    case 0xFA: for (k = 0; k < n / 4; k++) d->d[k] -= s->d[k]; return 1;
    case 0xFB: for (k = 0; k < n / 8; k++) d->q[k] -= s->q[k]; return 1;
    case 0xFC: for (k = 0; k < n; k++) d->b[k] = (uint8_t)(d->b[k] + s->b[k]); return 1;
    case 0xFD: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(d->w[k] + s->w[k]); return 1;
    case 0xFE: for (k = 0; k < n / 4; k++) d->d[k] += s->d[k]; return 1;
    default:
        (void)c;
        return 0;
    }
}

/* the shift by immediate groups 71, 72 and 73. */
static void shift_imm(X86Cpu *c, uint8_t op, int sub, X86Xmm *d, int n, unsigned count)
{
    int esize = op == 0x71 ? 2 : op == 0x72 ? 4 : 8;
    switch (sub) {
    case 2: shift_lanes(d, n, esize, count, 0); return;
    case 4:
        if (esize == 8)
            break;
        shift_lanes(d, n, esize, count, 1);
        return;
    case 6: shift_lanes(d, n, esize, count, 2); return;
    case 3:
    case 7:
        if (esize == 8 && n == 16) {
            byte_shift(d, count, sub == 7);
            return;
        }
        break;
    default:
        break;
    }
    x86_undefined(c);
}

/* ssse3 and sse4.1 integer operations of the 0f 38 map. */
static int simd_0f38(X86Cpu *c, uint8_t op, X86Xmm *d, X86Xmm *s, int n)
{
    int k;
    X86Xmm t;
    switch (op) {
    case 0x00: /* pshufb */
        for (k = 0; k < n; k++)
            t.b[k] = (s->b[k] & 0x80) ? 0 : d->b[s->b[k] & (n - 1)];
        __builtin_memcpy(d, &t, (size_t)n);
        return 1;
    case 0x01: /* phaddw */
    case 0x03: /* phaddsw */
    case 0x05: /* phsubw */
    case 0x07: { /* phsubsw */
        int lanes = n / 4;
        for (k = 0; k < lanes; k++) {
            int a = op <= 3 ? d->sw[2 * k] + d->sw[2 * k + 1] : d->sw[2 * k] - d->sw[2 * k + 1];
            int b = op <= 3 ? s->sw[2 * k] + s->sw[2 * k + 1] : s->sw[2 * k] - s->sw[2 * k + 1];
            t.sw[k] = (op == 3 || op == 7) ? sat16(a) : (int16_t)a;
            t.sw[k + lanes] = (op == 3 || op == 7) ? sat16(b) : (int16_t)b;
        }
        __builtin_memcpy(d, &t, (size_t)n);
        return 1;
    }
    case 0x02: /* phaddd */
    case 0x06: { /* phsubd */
        int lanes = n / 8;
        for (k = 0; k < lanes; k++) {
            t.d[k] = op == 2 ? d->d[2 * k] + d->d[2 * k + 1] : d->d[2 * k] - d->d[2 * k + 1];
            t.d[k + lanes] = op == 2 ? s->d[2 * k] + s->d[2 * k + 1] : s->d[2 * k] - s->d[2 * k + 1];
        }
        __builtin_memcpy(d, &t, (size_t)n);
        return 1;
    }
    case 0x04: /* pmaddubsw */
        for (k = 0; k < n / 2; k++)
            d->sw[k] = sat16(d->b[2 * k] * s->sb[2 * k] + d->b[2 * k + 1] * s->sb[2 * k + 1]);
        return 1;
    case 0x08: for (k = 0; k < n; k++) d->sb[k] = s->sb[k] < 0 ? (int8_t)-d->sb[k] : s->sb[k] ? d->sb[k] : 0; return 1;
    case 0x09: for (k = 0; k < n / 2; k++) d->sw[k] = s->sw[k] < 0 ? (int16_t)-d->sw[k] : s->sw[k] ? d->sw[k] : 0; return 1;
    case 0x0A: for (k = 0; k < n / 4; k++) d->sd[k] = s->sd[k] < 0 ? (int32_t)(0u - d->d[k]) : s->sd[k] ? d->sd[k] : 0; return 1;
    case 0x0B: /* pmulhrsw */
        for (k = 0; k < n / 2; k++)
            d->sw[k] = (int16_t)((((int32_t)d->sw[k] * s->sw[k] >> 14) + 1) >> 1);
        return 1;
    case 0x1C: for (k = 0; k < n; k++) d->b[k] = (uint8_t)(s->sb[k] < 0 ? -s->sb[k] : s->sb[k]); return 1;
    case 0x1D: for (k = 0; k < n / 2; k++) d->w[k] = (uint16_t)(s->sw[k] < 0 ? -s->sw[k] : s->sw[k]); return 1;
    case 0x1E: for (k = 0; k < n / 4; k++) d->d[k] = s->sd[k] < 0 ? 0u - s->d[k] : s->d[k]; return 1;
    default:
        break;
    }
    if (n != 16)
        return 0;
    switch (op) {
    case 0x10: /* pblendvb, xmm0 mask */
        for (k = 0; k < 16; k++)
            if (c->xmm[0].b[k] & 0x80)
                d->b[k] = s->b[k];
        return 1;
    case 0x14: /* blendvps */
        for (k = 0; k < 4; k++)
            if (c->xmm[0].d[k] & 0x80000000u)
                d->d[k] = s->d[k];
        return 1;
    case 0x15: /* blendvpd */
        for (k = 0; k < 2; k++)
            if (c->xmm[0].q[k] >> 63)
                d->q[k] = s->q[k];
        return 1;
    case 0x17: { /* ptest */
        uint32_t f = 0;
        if (!((d->q[0] & s->q[0]) | (d->q[1] & s->q[1])))
            f |= X86_ZF;
        if (!((~d->q[0] & s->q[0]) | (~d->q[1] & s->q[1])))
            f |= X86_CF;
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        c->lf_op = X86_LF_NONE;
        return 2; /* no destination write. */
    }
    case 0x20: for (k = 0; k < 8; k++) t.sw[k] = s->sb[k]; *d = t; return 1;
    case 0x21: for (k = 0; k < 4; k++) t.sd[k] = s->sb[k]; *d = t; return 1;
    case 0x22: for (k = 0; k < 2; k++) t.sq[k] = s->sb[k]; *d = t; return 1;
    case 0x23: for (k = 0; k < 4; k++) t.sd[k] = s->sw[k]; *d = t; return 1;
    case 0x24: for (k = 0; k < 2; k++) t.sq[k] = s->sw[k]; *d = t; return 1;
    case 0x25: for (k = 0; k < 2; k++) t.sq[k] = s->sd[k]; *d = t; return 1;
    case 0x28: for (k = 0; k < 2; k++) d->sq[k] = (int64_t)d->sd[2 * k] * s->sd[2 * k]; return 1;
    case 0x29: for (k = 0; k < 2; k++) d->q[k] = d->q[k] == s->q[k] ? ~0ull : 0; return 1;
    case 0x2A: *d = *s; return 1; /* movntdqa */
    case 0x2B: pack(d, s, 16, 3); return 1;
    case 0x30: for (k = 0; k < 8; k++) t.w[k] = s->b[k]; *d = t; return 1;
    case 0x31: for (k = 0; k < 4; k++) t.d[k] = s->b[k]; *d = t; return 1;
    case 0x32: for (k = 0; k < 2; k++) t.q[k] = s->b[k]; *d = t; return 1;
    case 0x33: for (k = 0; k < 4; k++) t.d[k] = s->w[k]; *d = t; return 1;
    case 0x34: for (k = 0; k < 2; k++) t.q[k] = s->w[k]; *d = t; return 1;
    case 0x35: for (k = 0; k < 2; k++) t.q[k] = s->d[k]; *d = t; return 1;
    case 0x38: for (k = 0; k < 16; k++) d->sb[k] = d->sb[k] < s->sb[k] ? d->sb[k] : s->sb[k]; return 1;
    case 0x39: for (k = 0; k < 4; k++) d->sd[k] = d->sd[k] < s->sd[k] ? d->sd[k] : s->sd[k]; return 1;
    case 0x3A: for (k = 0; k < 8; k++) d->w[k] = d->w[k] < s->w[k] ? d->w[k] : s->w[k]; return 1;
    case 0x3B: for (k = 0; k < 4; k++) d->d[k] = d->d[k] < s->d[k] ? d->d[k] : s->d[k]; return 1;
    case 0x3C: for (k = 0; k < 16; k++) d->sb[k] = d->sb[k] > s->sb[k] ? d->sb[k] : s->sb[k]; return 1;
    case 0x3D: for (k = 0; k < 4; k++) d->sd[k] = d->sd[k] > s->sd[k] ? d->sd[k] : s->sd[k]; return 1;
    case 0x3E: for (k = 0; k < 8; k++) d->w[k] = d->w[k] > s->w[k] ? d->w[k] : s->w[k]; return 1;
    case 0x3F: for (k = 0; k < 4; k++) d->d[k] = d->d[k] > s->d[k] ? d->d[k] : s->d[k]; return 1;
    case 0x40: for (k = 0; k < 4; k++) d->d[k] = d->d[k] * s->d[k]; return 1;
    case 0x41: { /* phminposuw */
        int best = 0;
        for (k = 1; k < 8; k++)
            if (s->w[k] < s->w[best])
                best = k;
        __builtin_memset(&t, 0, sizeof(t));
        t.w[0] = s->w[best];
        t.w[1] = (uint16_t)best;
        *d = t;
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ sha */

static inline uint32_t rol32(uint32_t v, int n) { return v << n | v >> (32 - n); }
static inline uint32_t ror32(uint32_t v, int n) { return v >> n | v << (32 - n); }

/* sha1rnds4 and the 0f 38 sha instructions. lane 3 holds the first word, as the sdm writes them. */
static void sha(X86Cpu *c, const X86Insn *i)
{
    X86Xmm s, k0 = c->xmm[0], t = c->xmm[i->reg];
    uint32_t *d = t.d, *w = s.d;
    load_xmm_src(c, i, &s, 16);
    if (i->map == 3) { /* sha1rnds4 */
        static const uint32_t k[4] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};
        int f = (int)(i->imm & 3);
        uint32_t a = d[3], b = d[2], cc = d[1], dd = d[0], e = 0;
        for (int r = 0; r < 4; r++) {
            uint32_t fv = f == 0 ? (b & cc) ^ (~b & dd) : f == 2 ? (b & cc) ^ (b & dd) ^ (cc & dd) : b ^ cc ^ dd;
            uint32_t na = fv + rol32(a, 5) + w[3 - r] + e + k[f];
            e = dd;
            dd = cc;
            cc = rol32(b, 30);
            b = a;
            a = na;
        }
        d[3] = a, d[2] = b, d[1] = cc, d[0] = dd;
        c->xmm[i->reg] = t;
        return;
    }
    switch (i->opcode) {
    case 0xC8: /* sha1nexte */
        d[3] = w[3] + rol32(d[3], 30);
        d[2] = w[2], d[1] = w[1], d[0] = w[0];
        break;
    case 0xC9: /* sha1msg1 */
        d[3] ^= d[1], d[2] ^= d[0], d[1] ^= w[3], d[0] ^= w[2];
        break;
    case 0xCA: { /* sha1msg2 */
        uint32_t w16 = rol32(d[3] ^ w[2], 1), w17 = rol32(d[2] ^ w[1], 1), w18 = rol32(d[1] ^ w[0], 1);
        d[0] = rol32(d[0] ^ w16, 1), d[1] = w18, d[2] = w17, d[3] = w16;
        break;
    }
    case 0xCB: { /* sha256rnds2, with the words and keys of two rounds in the low half of xmm0 */
        uint32_t a = w[3], b = w[2], cc = d[3], dd = d[2], e = w[1], f = w[0], g = d[1], h = d[0];
        for (int r = 0; r < 2; r++) {
            uint32_t t1 = ((e & f) ^ (~e & g)) + (ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25)) + k0.d[r] + h;
            uint32_t t2 = ((a & b) ^ (a & cc) ^ (b & cc)) + (ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22));
            h = g, g = f, f = e, e = dd + t1, dd = cc, cc = b, b = a, a = t1 + t2;
        }
        d[3] = a, d[2] = b, d[1] = e, d[0] = f;
        break;
    }
    case 0xCC: /* sha256msg1 */
        for (int k = 0; k < 4; k++) {
            uint32_t v = k < 3 ? d[k + 1] : w[0];
            d[k] += ror32(v, 7) ^ ror32(v, 18) ^ (v >> 3);
        }
        break;
    default: /* sha256msg2 */
        for (int k = 0; k < 4; k++) {
            uint32_t v = k < 2 ? w[k + 2] : d[k - 2];
            d[k] += ror32(v, 17) ^ ror32(v, 19) ^ (v >> 10);
        }
        break;
    }
    c->xmm[i->reg] = t;
}

/* ------------------------------------------------------------ 0f 3a */

static void exec_0f3a(X86Cpu *c, const X86Insn *i)
{
    uint8_t op = i->opcode;
    uint8_t imm = (uint8_t)i->imm;
    int k;
    X86Xmm s, t;
    if (op == 0x0F) { /* palignr */
        if (i->mandatory == 0x66) {
            load_xmm_src(c, i, &s, 16);
            uint8_t cat[32];
            __builtin_memcpy(cat, s.b, 16);
            __builtin_memcpy(cat + 16, c->xmm[i->reg].b, 16);
            for (k = 0; k < 16; k++)
                c->xmm[i->reg].b[k] = imm + k < 32 ? cat[imm + k] : 0;
        } else if (!i->mandatory) {
            enter_mmx(c);
            load_mm_src(c, i, &s);
            uint8_t cat[16];
            __builtin_memcpy(cat, &s.q[0], 8);
            __builtin_memcpy(cat + 8, &c->mm[i->reg & 7], 8);
            uint8_t out[8];
            for (k = 0; k < 8; k++)
                out[k] = imm + k < 16 ? cat[imm + k] : 0;
            __builtin_memcpy(&c->mm[i->reg & 7], out, 8);
        } else {
            x86_undefined(c);
        }
        return;
    }
    if (i->mandatory != 0x66)
        x86_undefined(c);
    X86Xmm *d = &c->xmm[i->reg];
    switch (op) {
    case 0x08: /* roundps */
        load_xmm_src(c, i, &s, 16);
        for (k = 0; k < 4; k++)
            d->f[k] = (float)round_mode(s.f[k], (imm & 4) ? (int)((c->mxcsr >> 13) & 3) : imm & 3);
        return;
    case 0x09: /* roundpd */
        load_xmm_src(c, i, &s, 16);
        for (k = 0; k < 2; k++)
            d->fd[k] = round_mode(s.fd[k], (imm & 4) ? (int)((c->mxcsr >> 13) & 3) : imm & 3);
        return;
    case 0x0A: /* roundss */
        load_xmm_src(c, i, &s, 4);
        d->f[0] = (float)round_mode(s.f[0], (imm & 4) ? (int)((c->mxcsr >> 13) & 3) : imm & 3);
        return;
    case 0x0B: /* roundsd */
        load_xmm_src(c, i, &s, 8);
        d->fd[0] = round_mode(s.fd[0], (imm & 4) ? (int)((c->mxcsr >> 13) & 3) : imm & 3);
        return;
    case 0x0C: /* blendps */
        load_xmm_src(c, i, &s, 16);
        for (k = 0; k < 4; k++)
            if (imm & (1 << k))
                d->d[k] = s.d[k];
        return;
    case 0x0D: /* blendpd */
        load_xmm_src(c, i, &s, 16);
        for (k = 0; k < 2; k++)
            if (imm & (1 << k))
                d->q[k] = s.q[k];
        return;
    case 0x0E: /* pblendw */
        load_xmm_src(c, i, &s, 16);
        for (k = 0; k < 8; k++)
            if (imm & (1 << k))
                d->w[k] = s.w[k];
        return;
    case 0x14: /* pextrb */
    case 0x15: /* pextrw */
    case 0x16: /* pextrd, pextrq */
    case 0x17: { /* extractps */
        uint64_t v;
        int size;
        if (op == 0x14) { v = d->b[imm & 15]; size = 1; }
        else if (op == 0x15) { v = d->w[imm & 7]; size = 2; }
        else if (op == 0x16 && (i->rex & 8)) { v = d->q[imm & 1]; size = 8; }
        else { v = d->d[imm & 3]; size = 4; }
        if (i->mod == 3)
            c->r[i->rm] = size == 8 ? v : (uint32_t)v;
        else
            wrn(x86_ea(c, i), size, v);
        return;
    }
    case 0x20: /* pinsrb */
        d->b[imm & 15] = (uint8_t)gpr_src(c, i, 1);
        return;
    case 0x21: { /* insertps */
        uint32_t v;
        if (i->mod == 3)
            v = c->xmm[i->rm].d[(imm >> 6) & 3];
        else
            v = rd32(x86_ea(c, i));
        d->d[(imm >> 4) & 3] = v;
        for (k = 0; k < 4; k++)
            if (imm & (1 << k))
                d->d[k] = 0;
        return;
    }
    case 0x22: /* pinsrd, pinsrq */
        if (i->rex & 8)
            d->q[imm & 1] = gpr_src(c, i, 8);
        else
            d->d[imm & 3] = (uint32_t)gpr_src(c, i, 4);
        return;
    case 0x40: { /* dpps */
        load_xmm_src(c, i, &s, 16);
        float p[4];
        for (k = 0; k < 4; k++)
            p[k] = (imm & (0x10 << k)) ? arith_f(0x59, d->f[k], s.f[k]) : 0.0f;
        float sum = arith_f(0x58, arith_f(0x58, p[0], p[1]), arith_f(0x58, p[2], p[3]));
        for (k = 0; k < 4; k++)
            t.f[k] = (imm & (1 << k)) ? sum : 0.0f;
        *d = t;
        return;
    }
    case 0x41: { /* dppd */
        load_xmm_src(c, i, &s, 16);
        double sum = arith_d(0x58, (imm & 0x10) ? arith_d(0x59, d->fd[0], s.fd[0]) : 0.0,
                             (imm & 0x20) ? arith_d(0x59, d->fd[1], s.fd[1]) : 0.0);
        for (k = 0; k < 2; k++)
            t.fd[k] = (imm & (1 << k)) ? sum : 0.0;
        *d = t;
        return;
    }
    case 0x42: { /* mpsadbw */
        load_xmm_src(c, i, &s, 16);
        int so = (imm & 3) * 4, dof = ((imm >> 2) & 1) * 4;
        for (k = 0; k < 8; k++) {
            int sum = 0;
            for (int b = 0; b < 4; b++) {
                int diff = d->b[dof + k + b] - s.b[so + b];
                sum += diff < 0 ? -diff : diff;
            }
            t.w[k] = (uint16_t)sum;
        }
        *d = t;
        return;
    }
    default:
        x86_undefined(c);
    }
}

/* ------------------------------------------------------------ mmx */

static void exec_mmx(X86Cpu *c, const X86Insn *i)
{
    uint8_t op = i->opcode;
    X86Xmm d, s;
    enter_mmx(c);
    uint64_t *dst = &c->mm[i->reg & 7];
    switch (op) {
    case 0x6E: /* movd mm, rm */
        *dst = gpr_src(c, i, (i->rex & 8) ? 8 : 4);
        return;
    case 0x6F: /* movq mm, mm/m64 */
        load_mm_src(c, i, &s);
        *dst = s.q[0];
        return;
    case 0x7E: { /* movd rm, mm */
        int size = (i->rex & 8) ? 8 : 4;
        uint64_t v = size == 8 ? *dst : (uint32_t)*dst;
        if (i->mod == 3)
            c->r[i->rm] = v;
        else
            wrn(x86_ea(c, i), size, v);
        return;
    }
    case 0x7F: /* movq mm/m64, mm */
    case 0xE7: /* movntq */
        if (i->mod == 3)
            c->mm[i->rm & 7] = *dst;
        else
            wr64(x86_ea(c, i), *dst);
        return;
    case 0x70: { /* pshufw */
        load_mm_src(c, i, &s);
        uint8_t imm = (uint8_t)i->imm;
        __builtin_memset(&d, 0, sizeof(d));
        for (int k = 0; k < 4; k++)
            d.w[k] = s.w[(imm >> (2 * k)) & 3];
        *dst = d.q[0];
        return;
    }
    case 0x71:
    case 0x72:
    case 0x73:
        __builtin_memset(&d, 0, sizeof(d));
        d.q[0] = c->mm[i->rm & 7];
        shift_imm(c, op, (i->modrm >> 3) & 7, &d, 8, (unsigned)(i->imm & 0xFF));
        c->mm[i->rm & 7] = d.q[0];
        return;
    case 0xC4: /* pinsrw */
        __builtin_memset(&d, 0, sizeof(d));
        d.q[0] = *dst;
        d.w[i->imm & 3] = (uint16_t)gpr_src(c, i, 2);
        *dst = d.q[0];
        return;
    case 0xC5: /* pextrw */
        if (i->mod != 3)
            x86_undefined(c);
        c->r[i->reg] = (uint16_t)(c->mm[i->rm & 7] >> (16 * (i->imm & 3)));
        return;
    case 0xD7: { /* pmovmskb */
        uint64_t v = c->mm[i->rm & 7];
        uint32_t m = 0;
        for (int k = 0; k < 8; k++)
            m |= (uint32_t)((v >> (8 * k + 7)) & 1) << k;
        c->r[i->reg] = m;
        return;
    }
    case 0xF7: { /* maskmovq */
        uint64_t data = *dst, mask = c->mm[i->rm & 7];
        uint64_t a = c->r[X86_RDI];
        if (i->addrsize == 4)
            a = (uint32_t)a;
        for (int k = 0; k < 8; k++)
            if ((mask >> (8 * k + 7)) & 1)
                wr8(a + (uint64_t)k, (uint8_t)(data >> (8 * k)));
        return;
    }
    default:
        break;
    }
    __builtin_memset(&d, 0, sizeof(d));
    d.q[0] = *dst;
    load_mm_src(c, i, &s);
    if (i->map == 2) {
        if (!simd_0f38(c, op, &d, &s, 8))
            x86_undefined(c);
    } else if (!simd_int(c, op, &d, &s, 8)) {
        x86_undefined(c);
    }
    *dst = d.q[0];
}

void x86_exec_mmx(X86Cpu *c, const X86Insn *i)
{
    exec_mmx(c, i);
}

/* ------------------------------------------------------------ 0f map */

static int is_mmx_form(uint8_t op)
{
    return (op >= 0x60 && op <= 0x7F && op != 0x77 && op != 0x78 && op != 0x79 && op != 0x7C && op != 0x7D) ||
           op == 0xC4 || op == 0xC5 || (op >= 0xD1 && op != 0xD6 && op != 0xFF);
}

void x86_exec_sse(X86Cpu *c, const X86Insn *i)
{
    uint8_t op = i->opcode;
    uint8_t pfx = i->mandatory;
    int k;
    X86Xmm s, t;

    if (i->map == 3) {
        if (op == 0xCC && !pfx)
            sha(c, i);
        else
            exec_0f3a(c, i);
        return;
    }
    if (i->map == 2) {
        if (op >= 0xC8 && op <= 0xCD && !pfx) {
            sha(c, i);
            return;
        }
        if (op == 0xF0 || op == 0xF1)
            x86_undefined(c); /* movbe and crc32 are not reported. */
        if (pfx == 0) {
            exec_mmx(c, i);
            return;
        }
        if (pfx != 0x66)
            x86_undefined(c);
        load_xmm_src(c, i, &s, 16);
        if (op >= 0x20 && op <= 0x35 && op != 0x28 && op != 0x29 && op != 0x2A && op != 0x2B && i->mod != 3) {
            /* the sign and zero extensions read only what they extend. */
            static const uint8_t bytes[6] = {8, 4, 2, 8, 4, 8};
            __builtin_memset(&s, 0, sizeof(s));
            __builtin_memcpy(&s, (const void *)(uintptr_t)x86_ea(c, i), bytes[(op & 0xF) % 6]);
        }
        t = c->xmm[i->reg];
        int r = simd_0f38(c, op, &t, &s, 16);
        if (!r)
            x86_undefined(c);
        if (r == 1)
            c->xmm[i->reg] = t;
        return;
    }

    if (pfx == 0 && is_mmx_form(op)) {
        exec_mmx(c, i);
        return;
    }

    X86Xmm *d = &c->xmm[i->reg];
    switch (op) {
    case 0x10:
        if (pfx == 0xF3) { /* movss */
            if (i->mod == 3)
                d->d[0] = c->xmm[i->rm].d[0];
            else {
                __builtin_memset(d, 0, 16);
                d->d[0] = rd32(x86_ea(c, i));
            }
        } else if (pfx == 0xF2) { /* movsd */
            if (i->mod == 3)
                d->q[0] = c->xmm[i->rm].q[0];
            else {
                d->q[1] = 0;
                d->q[0] = rd64(x86_ea(c, i));
            }
        } else { /* movups, movupd */
            load_xmm_src(c, i, &s, 16);
            *d = s;
        }
        return;
    case 0x11:
        if (i->mod == 3) {
            X86Xmm *dst = &c->xmm[i->rm];
            if (pfx == 0xF3)
                dst->d[0] = d->d[0];
            else if (pfx == 0xF2)
                dst->q[0] = d->q[0];
            else
                *dst = *d;
        } else {
            uint64_t a = x86_ea(c, i);
            if (pfx == 0xF3)
                wr32(a, d->d[0]);
            else if (pfx == 0xF2)
                wr64(a, d->q[0]);
            else
                __builtin_memcpy((void *)(uintptr_t)a, d, 16);
        }
        return;
    case 0x12:
        if (pfx == 0xF3) { /* movsldup */
            load_xmm_src(c, i, &s, 16);
            t.d[0] = t.d[1] = s.d[0];
            t.d[2] = t.d[3] = s.d[2];
            *d = t;
        } else if (pfx == 0xF2) { /* movddup */
            load_xmm_src(c, i, &s, 8);
            d->q[0] = d->q[1] = s.q[0];
        } else if (i->mod == 3) { /* movhlps */
            if (pfx)
                x86_undefined(c);
            d->q[0] = c->xmm[i->rm].q[1];
        } else { /* movlps, movlpd */
            d->q[0] = rd64(x86_ea(c, i));
        }
        return;
    case 0x13: /* movlps, movlpd store */
        if (i->mod == 3)
            x86_undefined(c);
        wr64(x86_ea(c, i), d->q[0]);
        return;
    case 0x14: /* unpcklps, unpcklpd */
    case 0x15: /* unpckhps, unpckhpd */
        load_xmm_src(c, i, &s, 16);
        unpack(d, &s, 16, pfx == 0x66 ? 8 : 4, op == 0x15);
        return;
    case 0x16:
        if (pfx == 0xF3) { /* movshdup */
            load_xmm_src(c, i, &s, 16);
            t.d[0] = t.d[1] = s.d[1];
            t.d[2] = t.d[3] = s.d[3];
            *d = t;
        } else if (i->mod == 3) { /* movlhps */
            d->q[1] = c->xmm[i->rm].q[0];
        } else { /* movhps, movhpd */
            d->q[1] = rd64(x86_ea(c, i));
        }
        return;
    case 0x17: /* movhps, movhpd store */
        if (i->mod == 3)
            x86_undefined(c);
        wr64(x86_ea(c, i), d->q[1]);
        return;
    case 0x28: /* movaps, movapd */
        load_xmm_src(c, i, &s, 16);
        *d = s;
        return;
    case 0x29:
    case 0x2B: /* movaps store, movntps */
        if (i->mod == 3)
            c->xmm[i->rm] = *d;
        else
            __builtin_memcpy((void *)(uintptr_t)x86_ea(c, i), d, 16);
        return;
    case 0x2A:
        if (pfx == 0xF3 || pfx == 0xF2) { /* cvtsi2ss, cvtsi2sd */
            int64_t v = (i->rex & 8) ? (int64_t)gpr_src(c, i, 8) : (int32_t)gpr_src(c, i, 4);
            if (pfx == 0xF3)
                d->f[0] = (float)v;
            else
                d->fd[0] = (double)v;
        } else { /* cvtpi2ps, cvtpi2pd */
            load_mm_src(c, i, &s);
            if (pfx == 0x66) {
                d->fd[0] = s.sd[0];
                d->fd[1] = s.sd[1];
            } else {
                d->f[0] = (float)s.sd[0];
                d->f[1] = (float)s.sd[1];
            }
            enter_mmx(c);
        }
        return;
    case 0x2C:
    case 0x2D: {
        int mode = op == 0x2C ? 3 : (int)((c->mxcsr >> 13) & 3);
        if (pfx == 0xF3 || pfx == 0xF2) { /* cvt(t)ss2si, cvt(t)sd2si */
            load_xmm_src(c, i, &s, pfx == 0xF3 ? 4 : 8);
            double v = pfx == 0xF3 ? (double)s.f[0] : s.fd[0];
            int size = (i->rex & 8) ? 8 : 4;
            int64_t r = to_int(c, v, size, mode);
            c->r[i->reg] = size == 8 ? (uint64_t)r : (uint32_t)r;
        } else { /* cvt(t)ps2pi, cvt(t)pd2pi */
            load_xmm_src(c, i, &s, pfx == 0x66 ? 16 : 8);
            uint32_t lo, hi;
            if (pfx == 0x66) {
                lo = (uint32_t)to_int(c, s.fd[0], 4, mode);
                hi = (uint32_t)to_int(c, s.fd[1], 4, mode);
            } else {
                lo = (uint32_t)to_int(c, s.f[0], 4, mode);
                hi = (uint32_t)to_int(c, s.f[1], 4, mode);
            }
            enter_mmx(c);
            c->mm[i->reg & 7] = ((uint64_t)hi << 32) | lo;
        }
        return;
    }
    case 0x2E: /* ucomiss, ucomisd */
    case 0x2F: /* comiss, comisd */
        if (pfx == 0x66) {
            load_xmm_src(c, i, &s, 8);
            set_compare_flags(c, d->fd[0], s.fd[0]);
        } else {
            load_xmm_src(c, i, &s, 4);
            set_compare_flags(c, d->f[0], s.f[0]);
        }
        return;
    case 0x50: { /* movmskps, movmskpd */
        if (i->mod != 3)
            x86_undefined(c);
        X86Xmm *src = &c->xmm[i->rm];
        uint32_t m = 0;
        if (pfx == 0x66)
            m = (uint32_t)(src->q[0] >> 63) | (uint32_t)((src->q[1] >> 63) << 1);
        else
            for (k = 0; k < 4; k++)
                m |= (src->d[k] >> 31) << k;
        c->r[i->reg] = m;
        return;
    }
    case 0x51: case 0x52: case 0x53: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        if (pfx == 0xF3) {
            load_xmm_src(c, i, &s, 4);
            d->f[0] = arith_f(op, d->f[0], s.f[0]);
        } else if (pfx == 0xF2) {
            if (op == 0x52 || op == 0x53)
                x86_undefined(c);
            load_xmm_src(c, i, &s, 8);
            d->fd[0] = arith_d(op, d->fd[0], s.fd[0]);
        } else if (pfx == 0x66) {
            if (op == 0x52 || op == 0x53)
                x86_undefined(c);
            load_xmm_src(c, i, &s, 16);
            for (k = 0; k < 2; k++)
                d->fd[k] = arith_d(op, d->fd[k], s.fd[k]);
        } else {
            load_xmm_src(c, i, &s, 16);
            for (k = 0; k < 4; k++)
                d->f[k] = arith_f(op, d->f[k], s.f[k]);
        }
        return;
    case 0x54: /* andps */
        load_xmm_src(c, i, &s, 16);
        d->q[0] &= s.q[0];
        d->q[1] &= s.q[1];
        return;
    case 0x55: /* andnps */
        load_xmm_src(c, i, &s, 16);
        d->q[0] = ~d->q[0] & s.q[0];
        d->q[1] = ~d->q[1] & s.q[1];
        return;
    case 0x56: /* orps */
        load_xmm_src(c, i, &s, 16);
        d->q[0] |= s.q[0];
        d->q[1] |= s.q[1];
        return;
    case 0x57: /* xorps */
        load_xmm_src(c, i, &s, 16);
        d->q[0] ^= s.q[0];
        d->q[1] ^= s.q[1];
        return;
    case 0x5A:
        if (pfx == 0xF3) { /* cvtss2sd */
            load_xmm_src(c, i, &s, 4);
            d->fd[0] = isnan_f(s.f[0]) ? u2d(((uint64_t)(f2u(s.f[0]) >> 31) << 63) | 0x7FF8000000000000ull | ((uint64_t)(f2u(s.f[0]) & 0x3FFFFF) << 29)) : (double)s.f[0];
        } else if (pfx == 0xF2) { /* cvtsd2ss */
            load_xmm_src(c, i, &s, 8);
            d->f[0] = (float)s.fd[0];
        } else if (pfx == 0x66) { /* cvtpd2ps */
            load_xmm_src(c, i, &s, 16);
            d->f[0] = (float)s.fd[0];
            d->f[1] = (float)s.fd[1];
            d->q[1] = 0;
        } else { /* cvtps2pd */
            load_xmm_src(c, i, &s, 8);
            d->fd[0] = s.f[0];
            d->fd[1] = s.f[1];
        }
        return;
    case 0x5B:
        load_xmm_src(c, i, &s, 16);
        if (pfx == 0x66 || pfx == 0xF3) { /* cvtps2dq, cvttps2dq */
            int mode = pfx == 0xF3 ? 3 : (int)((c->mxcsr >> 13) & 3);
            for (k = 0; k < 4; k++)
                d->sd[k] = (int32_t)to_int(c, s.f[k], 4, mode);
        } else if (pfx == 0) { /* cvtdq2ps */
            for (k = 0; k < 4; k++)
                d->f[k] = (float)s.sd[k];
        } else {
            x86_undefined(c);
        }
        return;
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D:
    case 0x74: case 0x75: case 0x76:
        if (pfx != 0x66)
            x86_undefined(c);
        load_xmm_src(c, i, &s, 16);
        simd_int(c, op, d, &s, 16);
        return;
    case 0x6E: /* movd, movq xmm, rm */
        if (pfx != 0x66)
            x86_undefined(c);
        __builtin_memset(d, 0, 16);
        d->q[0] = gpr_src(c, i, (i->rex & 8) ? 8 : 4);
        return;
    case 0x6F: /* movdqa, movdqu */
        if (pfx != 0x66 && pfx != 0xF3)
            x86_undefined(c);
        load_xmm_src(c, i, &s, 16);
        *d = s;
        return;
    case 0x70: { /* pshufd, pshufhw, pshuflw */
        load_xmm_src(c, i, &s, 16);
        uint8_t imm = (uint8_t)i->imm;
        t = s;
        if (pfx == 0x66) {
            for (k = 0; k < 4; k++)
                t.d[k] = s.d[(imm >> (2 * k)) & 3];
        } else if (pfx == 0xF3) {
            for (k = 0; k < 4; k++)
                t.w[4 + k] = s.w[4 + ((imm >> (2 * k)) & 3)];
        } else if (pfx == 0xF2) {
            for (k = 0; k < 4; k++)
                t.w[k] = s.w[(imm >> (2 * k)) & 3];
        } else {
            x86_undefined(c);
        }
        *d = t;
        return;
    }
    case 0x71:
    case 0x72:
    case 0x73:
        if (pfx != 0x66 || i->mod != 3)
            x86_undefined(c);
        shift_imm(c, op, (i->modrm >> 3) & 7, &c->xmm[i->rm], 16, (unsigned)(i->imm & 0xFF));
        return;
    case 0x7C: /* haddpd, haddps */
    case 0x7D: /* hsubpd, hsubps */
        load_xmm_src(c, i, &s, 16);
        if (pfx == 0x66) {
            int a = op == 0x7C ? 0x58 : 0x5C;
            t.fd[0] = arith_d(a, d->fd[0], d->fd[1]);
            t.fd[1] = arith_d(a, s.fd[0], s.fd[1]);
        } else if (pfx == 0xF2) {
            int a = op == 0x7C ? 0x58 : 0x5C;
            t.f[0] = arith_f(a, d->f[0], d->f[1]);
            t.f[1] = arith_f(a, d->f[2], d->f[3]);
            t.f[2] = arith_f(a, s.f[0], s.f[1]);
            t.f[3] = arith_f(a, s.f[2], s.f[3]);
        } else {
            x86_undefined(c);
        }
        *d = t;
        return;
    case 0x7E:
        if (pfx == 0xF3) { /* movq xmm, xmm/m64 */
            load_xmm_src(c, i, &s, 8);
            d->q[0] = s.q[0];
            d->q[1] = 0;
        } else if (pfx == 0x66) { /* movd, movq rm, xmm */
            int size = (i->rex & 8) ? 8 : 4;
            uint64_t v = size == 8 ? d->q[0] : d->d[0];
            if (i->mod == 3)
                c->r[i->rm] = v;
            else
                wrn(x86_ea(c, i), size, v);
        } else {
            x86_undefined(c);
        }
        return;
    case 0x7F: /* movdqa, movdqu store */
        if (pfx != 0x66 && pfx != 0xF3)
            x86_undefined(c);
        if (i->mod == 3)
            c->xmm[i->rm] = *d;
        else
            __builtin_memcpy((void *)(uintptr_t)x86_ea(c, i), d, 16);
        return;
    case 0xC2: { /* cmpps, cmppd, cmpss, cmpsd */
        int pred = (int)(i->imm & 7);
        if (pfx == 0xF3) {
            load_xmm_src(c, i, &s, 4);
            d->d[0] = compare(pred, d->f[0], s.f[0]) ? ~0u : 0;
        } else if (pfx == 0xF2) {
            load_xmm_src(c, i, &s, 8);
            d->q[0] = compare(pred, d->fd[0], s.fd[0]) ? ~0ull : 0;
        } else if (pfx == 0x66) {
            load_xmm_src(c, i, &s, 16);
            for (k = 0; k < 2; k++)
                d->q[k] = compare(pred, d->fd[k], s.fd[k]) ? ~0ull : 0;
        } else {
            load_xmm_src(c, i, &s, 16);
            for (k = 0; k < 4; k++)
                d->d[k] = compare(pred, d->f[k], s.f[k]) ? ~0u : 0;
        }
        return;
    }
    case 0xC4: /* pinsrw */
        if (pfx != 0x66)
            x86_undefined(c);
        d->w[i->imm & 7] = (uint16_t)gpr_src(c, i, 2);
        return;
    case 0xC5: /* pextrw */
        if (pfx != 0x66 || i->mod != 3)
            x86_undefined(c);
        c->r[i->reg] = c->xmm[i->rm].w[i->imm & 7];
        return;
    case 0xC6: { /* shufps, shufpd */
        load_xmm_src(c, i, &s, 16);
        uint8_t imm = (uint8_t)i->imm;
        if (pfx == 0x66) {
            t.q[0] = d->q[imm & 1];
            t.q[1] = s.q[(imm >> 1) & 1];
        } else if (pfx == 0) {
            t.d[0] = d->d[imm & 3];
            t.d[1] = d->d[(imm >> 2) & 3];
            t.d[2] = s.d[(imm >> 4) & 3];
            t.d[3] = s.d[(imm >> 6) & 3];
        } else {
            x86_undefined(c);
        }
        *d = t;
        return;
    }
    case 0xD0: /* addsubpd, addsubps */
        load_xmm_src(c, i, &s, 16);
        if (pfx == 0x66) {
            d->fd[0] = arith_d(0x5C, d->fd[0], s.fd[0]);
            d->fd[1] = arith_d(0x58, d->fd[1], s.fd[1]);
        } else if (pfx == 0xF2) {
            for (k = 0; k < 4; k++)
                d->f[k] = arith_f((k & 1) ? 0x58 : 0x5C, d->f[k], s.f[k]);
        } else {
            x86_undefined(c);
        }
        return;
    case 0xD6:
        if (pfx == 0x66) { /* movq xmm/m64, xmm */
            if (i->mod == 3) {
                c->xmm[i->rm].q[0] = d->q[0];
                c->xmm[i->rm].q[1] = 0;
            } else {
                wr64(x86_ea(c, i), d->q[0]);
            }
        } else if (pfx == 0xF3 && i->mod == 3) { /* movq2dq */
            enter_mmx(c);
            d->q[0] = c->mm[i->rm & 7];
            d->q[1] = 0;
        } else if (pfx == 0xF2 && i->mod == 3) { /* movdq2q */
            enter_mmx(c);
            c->mm[i->reg & 7] = c->xmm[i->rm].q[0];
        } else {
            x86_undefined(c);
        }
        return;
    case 0xD7: { /* pmovmskb */
        if (pfx != 0x66 || i->mod != 3)
            x86_undefined(c);
        uint32_t m = 0;
        for (k = 0; k < 16; k++)
            m |= (uint32_t)(c->xmm[i->rm].b[k] >> 7) << k;
        c->r[i->reg] = m;
        return;
    }
    case 0xE6:
        load_xmm_src(c, i, &s, 16);
        if (pfx == 0xF3) { /* cvtdq2pd */
            t.fd[0] = s.sd[0];
            t.fd[1] = s.sd[1];
        } else if (pfx == 0x66 || pfx == 0xF2) { /* cvttpd2dq, cvtpd2dq */
            int mode = pfx == 0x66 ? 3 : (int)((c->mxcsr >> 13) & 3);
            t.sd[0] = (int32_t)to_int(c, s.fd[0], 4, mode);
            t.sd[1] = (int32_t)to_int(c, s.fd[1], 4, mode);
            t.q[1] = 0;
        } else {
            x86_undefined(c);
        }
        *d = t;
        return;
    case 0xE7: /* movntdq */
        if (pfx != 0x66 || i->mod == 3)
            x86_undefined(c);
        __builtin_memcpy((void *)(uintptr_t)x86_ea(c, i), d, 16);
        return;
    case 0xF0: /* lddqu */
        if (pfx != 0xF2 || i->mod == 3)
            x86_undefined(c);
        load_xmm_src(c, i, &s, 16);
        *d = s;
        return;
    case 0xF7: { /* maskmovdqu */
        if (pfx != 0x66 || i->mod != 3)
            x86_undefined(c);
        uint64_t a = c->r[X86_RDI];
        if (i->addrsize == 4)
            a = (uint32_t)a;
        if (i->seg == 4)
            a += c->fs_base;
        else if (i->seg == 5)
            a += c->gs_base;
        for (k = 0; k < 16; k++)
            if (c->xmm[i->rm].b[k] & 0x80)
                wr8(a + (uint64_t)k, d->b[k]);
        return;
    }
    default:
        if (op >= 0xD1 && pfx == 0x66) {
            load_xmm_src(c, i, &s, 16);
            if (simd_int(c, op, d, &s, 16))
                return;
        }
        x86_undefined(c);
    }
}
