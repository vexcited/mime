/*
 * the x87 fpu, computed in double precision like most emulators do:
 * values loaded from 80-bit memory lose their extra bits.
 */
#include "internal.h"
#include "xmath.h"

#define FSW_C0 0x0100
#define FSW_C1 0x0200
#define FSW_C2 0x0400
#define FSW_C3 0x4000
#define FSW_CC (FSW_C0 | FSW_C1 | FSW_C2 | FSW_C3)

void x86_fpu_init(X86Cpu *c)
{
    c->fcw = 0x027F; /* windows starts threads with 53-bit precision. */
    c->fsw = 0;
    c->ftop = 0;
    c->ftag = 0;
    c->fop = 0;
    c->fpu_ip = 0;
    c->fpu_dp = 0;
    c->mmx_mode = 0;
}

/* ------------------------------------------------------------ 80-bit format */

void x86_double_to_ext(double v, uint8_t out[10])
{
    uint64_t u;
    __builtin_memcpy(&u, &v, 8);
    uint16_t sign = (uint16_t)((u >> 63) << 15);
    int exp = (int)((u >> 52) & 0x7FF);
    uint64_t frac = u & 0xFFFFFFFFFFFFFull;
    uint64_t mant;
    uint16_t e;
    if (exp == 0x7FF) {
        e = 0x7FFF;
        mant = 0x8000000000000000ull | (frac << 11);
    } else if (exp == 0) {
        if (!frac) {
            e = 0;
            mant = 0;
        } else {
            /* a denormal double is a normal extended value: frac * 2^-1074. */
            int lz = __builtin_clzll(frac);
            mant = frac << lz;
            e = (uint16_t)(15372 - lz);
        }
    } else {
        e = (uint16_t)(exp - 1023 + 16383);
        mant = 0x8000000000000000ull | (frac << 11);
    }
    __builtin_memcpy(out, &mant, 8);
    uint16_t se = (uint16_t)(sign | e);
    __builtin_memcpy(out + 8, &se, 2);
}

double x86_ext_to_double(const uint8_t in[10])
{
    uint64_t mant;
    uint16_t se;
    __builtin_memcpy(&mant, in, 8);
    __builtin_memcpy(&se, in + 8, 2);
    uint64_t sign = (uint64_t)(se >> 15) << 63;
    int e = se & 0x7FFF;
    uint64_t u;
    if (e == 0x7FFF) {
        uint64_t frac = (mant << 1) >> 12;
        if ((mant << 1) && !frac)
            frac = 1ull << 51; /* keeps a nan a nan. */
        u = sign | (0x7FFull << 52) | frac;
    } else if (!mant) {
        u = sign;
    } else {
        /* normalize, then round to nearest even into 53 bits. */
        int shift = __builtin_clzll(mant);
        mant <<= shift;
        e -= shift;
        int exp = e - 16383 + 1023;
        if (exp >= 0x7FF) {
            u = sign | (0x7FFull << 52);
        } else if (exp <= 0) {
            /* denormal or zero result. */
            int rshift = 11 + 1 - exp;
            if (rshift > 64) {
                u = sign;
            } else {
                uint64_t m = rshift == 64 ? 0 : mant >> rshift;
                uint64_t rest = rshift == 64 ? mant : mant << (64 - rshift);
                if (rest > 0x8000000000000000ull || (rest == 0x8000000000000000ull && (m & 1)))
                    m++;
                u = sign | m;
            }
        } else {
            uint64_t m = mant >> 11;
            uint64_t rest = mant & 0x7FF;
            if (rest > 0x400 || (rest == 0x400 && (m & 1))) {
                m++;
                if (m >> 53) {
                    m >>= 1;
                    exp++;
                }
            }
            if (exp >= 0x7FF)
                u = sign | (0x7FFull << 52);
            else
                u = sign | ((uint64_t)exp << 52) | (m & 0xFFFFFFFFFFFFFull);
        }
    }
    double d;
    __builtin_memcpy(&d, &u, 8);
    return d;
}

/* ------------------------------------------------------------ stack */

static inline double *st(X86Cpu *c, int i)
{
    return &c->fpr[(c->ftop + i) & 7];
}

static inline void fpush(X86Cpu *c, double v)
{
    c->ftop = (uint8_t)((c->ftop - 1) & 7);
    c->fpr[c->ftop] = v;
    c->ftag |= (uint8_t)(1u << c->ftop);
    c->mmx_mode = 0;
}

static inline void fpop(X86Cpu *c)
{
    c->ftag &= (uint8_t)~(1u << c->ftop);
    c->ftop = (uint8_t)((c->ftop + 1) & 7);
}

static uint16_t full_fsw(X86Cpu *c)
{
    return (uint16_t)((c->fsw & ~0x3800u) | ((unsigned)(c->ftop & 7) << 11));
}

static void set_cc(X86Cpu *c, uint16_t cc)
{
    c->fsw = (uint16_t)((c->fsw & ~FSW_CC) | cc);
}

static int rounding(X86Cpu *c)
{
    switch ((c->fcw >> 10) & 3) {
    case 0: return 0; /* nearest */
    case 1: return 1; /* down */
    case 2: return 2; /* up */
    default: return 3; /* toward zero */
    }
}

static double fround(double v, int mode)
{
    switch (mode) {
    case 0: return __builtin_roundeven(v);
    case 1: return __builtin_floor(v);
    case 2: return __builtin_ceil(v);
    default: return __builtin_trunc(v);
    }
}

static int64_t to_int(double v, int size, int mode)
{
    int64_t indefinite = size == 2 ? INT16_MIN : size == 4 ? INT32_MIN : INT64_MIN;
    if (v != v)
        return indefinite;
    double r = fround(v, mode);
    double limit = size == 2 ? 32768.0 : size == 4 ? 2147483648.0 : 9223372036854775808.0;
    if (r >= limit || r < -limit)
        return indefinite;
    return (int64_t)r;
}

static double arith(int op, double a, double b)
{
    switch (op) {
    case 0: return a + b;
    case 1: return a * b;
    case 4: return a - b;
    case 5: return b - a;
    case 6: return a / b;
    default: return b / a;
    }
}

static void compare(X86Cpu *c, double a, double b)
{
    uint16_t cc;
    if (a != a || b != b)
        cc = FSW_C0 | FSW_C2 | FSW_C3;
    else if (a < b)
        cc = FSW_C0;
    else if (a > b)
        cc = 0;
    else
        cc = FSW_C3;
    set_cc(c, cc);
}

static void comi(X86Cpu *c, double a, double b)
{
    uint32_t f;
    if (a != a || b != b)
        f = X86_ZF | X86_PF | X86_CF;
    else if (a < b)
        f = X86_CF;
    else if (a > b)
        f = 0;
    else
        f = X86_ZF;
    c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
    c->lf_op = X86_LF_NONE;
    c->fsw &= (uint16_t)~FSW_C1;
}

static uint16_t tag_word(X86Cpu *c)
{
    /* the full tag word: 00 valid, 01 zero, 10 special, 11 empty. */
    uint16_t tw = 0;
    for (int r = 0; r < 8; r++) {
        uint16_t t;
        if (!(c->ftag & (1u << r))) {
            t = 3;
        } else {
            double v = c->fpr[r];
            if (v == 0)
                t = 1;
            else if (v != v || v - v != 0)
                t = 2;
            else
                t = 0;
        }
        tw |= (uint16_t)(t << (2 * r));
    }
    return tw;
}

/* fnstenv and fldenv in the 28-byte protected mode format, with the deprecated selectors stored 0. */
static void store_env(X86Cpu *c, uint64_t a)
{
    wr32(a, c->fcw);
    wr32(a + 4, full_fsw(c));
    wr32(a + 8, tag_word(c));
    wr32(a + 12, (uint32_t)c->fpu_ip);
    wr32(a + 16, (uint32_t)c->fop << 16);
    wr32(a + 20, (uint32_t)c->fpu_dp);
    wr32(a + 24, 0);
}

static void load_env(X86Cpu *c, uint64_t a)
{
    c->fcw = rd16(a);
    uint16_t sw = rd16(a + 4);
    c->fsw = sw & (uint16_t)~0x3800u;
    c->ftop = (uint8_t)((sw >> 11) & 7);
    uint16_t tw = rd16(a + 8);
    c->ftag = 0;
    for (int r = 0; r < 8; r++)
        if (((tw >> (2 * r)) & 3) != 3)
            c->ftag |= (uint8_t)(1u << r);
    c->fpu_ip = rd32(a + 12);
    c->fop = rd16(a + 18) & 0x7FF;
    c->fpu_dp = rd32(a + 20);
}

/* ------------------------------------------------------------ fxsave */

/* wide is the rex.w form, with 64 bit instruction and data pointers. the other one keeps 32 bits and a selector. */
void x86_fxsave(X86Cpu *c, uint8_t *area, int wide)
{
    __builtin_memset(area, 0, 160); /* bytes 416 to 511 are left alone, like the processor does. */
    uint16_t fsw = full_fsw(c);
    __builtin_memcpy(area + 0, &c->fcw, 2);
    __builtin_memcpy(area + 2, &fsw, 2);
    area[4] = c->ftag;
    __builtin_memcpy(area + 6, &c->fop, 2);
    uint64_t ip = wide ? c->fpu_ip : (uint32_t)c->fpu_ip, dp = wide ? c->fpu_dp : (uint32_t)c->fpu_dp;
    __builtin_memcpy(area + 8, &ip, 8);
    __builtin_memcpy(area + 16, &dp, 8);
    uint32_t mxcsr = x86_mxcsr(c);
    __builtin_memcpy(area + 24, &mxcsr, 4);
    uint32_t mask = 0xFFFF;
    __builtin_memcpy(area + 28, &mask, 4);
    for (int i = 0; i < 8; i++) {
        uint8_t *slot = area + 32 + 16 * i;
        if (c->mmx_mode) {
            __builtin_memcpy(slot, &c->mm[i], 8);
            slot[8] = slot[9] = 0xFF;
        } else {
            x86_double_to_ext(*st(c, i), slot);
        }
    }
    __builtin_memcpy(area + 160, c->xmm, c->mode64 ? 256 : 128);
}

void x86_fxrstor(X86Cpu *c, const uint8_t *area, int wide)
{
    uint16_t fsw;
    __builtin_memcpy(&c->fcw, area + 0, 2);
    __builtin_memcpy(&fsw, area + 2, 2);
    c->fsw = fsw & (uint16_t)~0x3800u;
    c->ftop = (uint8_t)((fsw >> 11) & 7);
    c->ftag = area[4];
    __builtin_memcpy(&c->fop, area + 6, 2);
    c->fop &= 0x7FF;
    __builtin_memcpy(&c->fpu_ip, area + 8, wide ? 8 : 4);
    __builtin_memcpy(&c->fpu_dp, area + 16, wide ? 8 : 4);
    if (!wide) {
        c->fpu_ip = (uint32_t)c->fpu_ip;
        c->fpu_dp = (uint32_t)c->fpu_dp;
    }
    uint32_t mxcsr;
    __builtin_memcpy(&mxcsr, area + 24, 4);
    x86_set_mxcsr(c, mxcsr & 0xFFFF);
    for (int i = 0; i < 8; i++) {
        const uint8_t *slot = area + 32 + 16 * i;
        __builtin_memcpy(&c->mm[i], slot, 8);
        *st(c, i) = x86_ext_to_double(slot);
    }
    __builtin_memcpy(c->xmm, area + 160, c->mode64 ? 256 : 128);
}

/* ------------------------------------------------------------ fxam */

static uint16_t classify(X86Cpu *c)
{
    double v = *st(c, 0);
    uint16_t cc = __builtin_signbit(v) ? FSW_C1 : 0;
    if (!(c->ftag & (1u << c->ftop)))
        return cc | FSW_C3 | FSW_C0;
    if (v != v)
        return cc | FSW_C0;
    if (v - v != 0)
        return cc | FSW_C2 | FSW_C0;
    if (v == 0)
        return cc | FSW_C3;
    if (__builtin_fabs(v) < 2.2250738585072014e-308)
        return cc | FSW_C3 | FSW_C2;
    return cc | FSW_C2;
}

/* ------------------------------------------------------------ bcd */

static double load_bcd(uint64_t a)
{
    int64_t v = 0;
    for (int k = 8; k >= 0; k--) {
        uint8_t b = rd8(a + (uint64_t)k);
        v = v * 100 + (b >> 4) * 10 + (b & 15);
    }
    return (rd8(a + 9) & 0x80) ? -(double)v : (double)v;
}

static void store_bcd(uint64_t a, double d, int mode)
{
    double r = fround(d, mode);
    uint64_t v = (uint64_t)__builtin_fabs(r);
    for (int k = 0; k < 9; k++) {
        uint8_t lo = (uint8_t)(v % 10);
        v /= 10;
        uint8_t hi = (uint8_t)(v % 10);
        v /= 10;
        wr8(a + (uint64_t)k, (uint8_t)((hi << 4) | lo));
    }
    wr8(a + 9, r < 0 ? 0x80 : 0);
}

/* ------------------------------------------------------------ execution */

static void exec_memory(X86Cpu *c, const X86Insn *i, uint64_t a)
{
    int sub = (i->modrm >> 3) & 7;
    switch (i->opcode) {
    case 0xD8:
    case 0xDA:
    case 0xDC:
    case 0xDE: {
        double v;
        if (i->opcode == 0xD8) {
            uint32_t u = rd32(a);
            float f;
            __builtin_memcpy(&f, &u, 4);
            v = f;
        } else if (i->opcode == 0xDC) {
            uint64_t u = rd64(a);
            __builtin_memcpy(&v, &u, 8);
        } else if (i->opcode == 0xDA) {
            v = (int32_t)rd32(a);
        } else {
            v = (int16_t)rd16(a);
        }
        if (sub == 2 || sub == 3) {
            compare(c, *st(c, 0), v);
            if (sub == 3)
                fpop(c);
        } else {
            *st(c, 0) = arith(sub, *st(c, 0), v);
        }
        return;
    }
    case 0xD9:
        switch (sub) {
        case 0: {
            uint32_t u = rd32(a);
            float f;
            __builtin_memcpy(&f, &u, 4);
            fpush(c, f);
            return;
        }
        case 2:
        case 3: {
            float f = (float)*st(c, 0);
            uint32_t u;
            __builtin_memcpy(&u, &f, 4);
            wr32(a, u);
            if (sub == 3)
                fpop(c);
            return;
        }
        case 4:
            load_env(c, a);
            return;
        case 5:
            c->fcw = rd16(a);
            return;
        case 6:
            store_env(c, a);
            c->fcw |= 0x3F; /* fnstenv masks every exception. */
            return;
        case 7:
            wr16(a, c->fcw);
            return;
        default:
            x86_undefined(c);
        }
    case 0xDB:
        switch (sub) {
        case 0:
            fpush(c, (int32_t)rd32(a));
            return;
        case 1:
        case 2:
        case 3:
            wr32(a, (uint32_t)to_int(*st(c, 0), 4, sub == 1 ? 3 : rounding(c)));
            if (sub != 2)
                fpop(c);
            return;
        case 5:
            fpush(c, x86_ext_to_double((const uint8_t *)(uintptr_t)a));
            return;
        case 7: {
            uint8_t ext[10];
            x86_double_to_ext(*st(c, 0), ext);
            __builtin_memcpy((void *)(uintptr_t)a, ext, 10);
            fpop(c);
            return;
        }
        default:
            x86_undefined(c);
        }
    case 0xDD:
        switch (sub) {
        case 0: {
            uint64_t u = rd64(a);
            double d;
            __builtin_memcpy(&d, &u, 8);
            fpush(c, d);
            return;
        }
        case 1:
            wr64(a, (uint64_t)to_int(*st(c, 0), 8, 3));
            fpop(c);
            return;
        case 2:
        case 3: {
            double d = *st(c, 0);
            uint64_t u;
            __builtin_memcpy(&u, &d, 8);
            wr64(a, u);
            if (sub == 3)
                fpop(c);
            return;
        }
        case 4: /* frstor */
            load_env(c, a);
            for (int r = 0; r < 8; r++)
                *st(c, r) = x86_ext_to_double((const uint8_t *)(uintptr_t)(a + 28 + 10 * (uint64_t)r));
            return;
        case 6: /* fnsave */
            store_env(c, a);
            for (int r = 0; r < 8; r++) {
                uint8_t ext[10];
                x86_double_to_ext(*st(c, r), ext);
                __builtin_memcpy((void *)(uintptr_t)(a + 28 + 10 * (uint64_t)r), ext, 10);
            }
            x86_fpu_init(c);
            c->fcw = 0x037F;
            return;
        case 7:
            wr16(a, full_fsw(c));
            return;
        default:
            x86_undefined(c);
        }
    default: /* 0xdf */
        switch (sub) {
        case 0:
            fpush(c, (int16_t)rd16(a));
            return;
        case 1:
        case 2:
        case 3:
            wr16(a, (uint16_t)to_int(*st(c, 0), 2, sub == 1 ? 3 : rounding(c)));
            if (sub != 2)
                fpop(c);
            return;
        case 4:
            fpush(c, load_bcd(a));
            return;
        case 5:
            fpush(c, (double)(int64_t)rd64(a));
            return;
        case 6:
            store_bcd(a, *st(c, 0), rounding(c));
            fpop(c);
            return;
        default:
            wr64(a, (uint64_t)to_int(*st(c, 0), 8, rounding(c)));
            fpop(c);
            return;
        }
    }
}

static void exec_d9_register(X86Cpu *c, uint8_t m)
{
    double *s0 = st(c, 0);
    switch (m) {
    case 0xD0: /* fnop */
        return;
    case 0xE0:
        *s0 = -*s0;
        return;
    case 0xE1:
        *s0 = __builtin_fabs(*s0);
        return;
    case 0xE4: /* ftst */
        compare(c, *s0, 0.0);
        return;
    case 0xE5:
        set_cc(c, classify(c));
        return;
    case 0xE8: fpush(c, 1.0); return;
    case 0xE9: fpush(c, 3.32192809488736234787); return;  /* log2(10) */
    case 0xEA: fpush(c, 1.44269504088896340736); return;  /* log2(e) */
    case 0xEB: fpush(c, 3.14159265358979323846); return;
    case 0xEC: fpush(c, 0.30102999566398119521); return;  /* log10(2) */
    case 0xED: fpush(c, 0.69314718055994530942); return;  /* ln(2) */
    case 0xEE: fpush(c, 0.0); return;
    case 0xF0: /* f2xm1 */
        *s0 = xm_exp2m1(*s0);
        return;
    case 0xF1: { /* fyl2x */
        double r = *st(c, 1) * xm_log2(*s0);
        fpop(c);
        *st(c, 0) = r;
        return;
    }
    case 0xF2: /* fptan */
        *s0 = xm_tan(*s0);
        set_cc(c, 0);
        fpush(c, 1.0);
        return;
    case 0xF3: { /* fpatan */
        double r = xm_atan2(*st(c, 1), *s0);
        fpop(c);
        *st(c, 0) = r;
        return;
    }
    case 0xF4: { /* fxtract */
        double v = *s0;
        int e;
        double m = xm_frexp(v, &e);
        *s0 = (double)(e - 1);
        fpush(c, m * 2);
        return;
    }
    case 0xF5: /* fprem1 */
    case 0xF8: { /* fprem */
        uint64_t q;
        double a = *s0, b = *st(c, 1);
        double r;
        if (m == 0xF5) {
            /* round the quotient to nearest. */
            r = xm_fmod(a, b, &q);
            double half = __builtin_fabs(b) / 2;
            if (__builtin_fabs(r) > half || (__builtin_fabs(r) == half && (q & 1))) {
                r = r - (r < 0 ? -__builtin_fabs(b) : __builtin_fabs(b));
                q++;
            }
        } else {
            r = xm_fmod(a, b, &q);
        }
        *s0 = r;
        uint16_t cc = 0;
        if (q & 1) cc |= FSW_C1;
        if (q & 2) cc |= FSW_C3;
        if (q & 4) cc |= FSW_C0;
        set_cc(c, cc);
        return;
    }
    case 0xF6:
        c->ftop = (uint8_t)((c->ftop - 1) & 7);
        return;
    case 0xF7:
        c->ftop = (uint8_t)((c->ftop + 1) & 7);
        return;
    case 0xF9: { /* fyl2xp1 */
        double r = *st(c, 1) * xm_log2p1(*s0);
        fpop(c);
        *st(c, 0) = r;
        return;
    }
    case 0xFA:
        *s0 = __builtin_sqrt(*s0);
        return;
    case 0xFB: { /* fsincos */
        double v = *s0;
        *s0 = xm_sin(v);
        set_cc(c, 0);
        fpush(c, xm_cos(v));
        return;
    }
    case 0xFC:
        *s0 = fround(*s0, rounding(c));
        return;
    case 0xFD: { /* fscale */
        double n = __builtin_trunc(*st(c, 1));
        if (n > 4096) n = 4096;
        if (n < -4096) n = -4096;
        *s0 = xm_scalbn(*s0, (int)n);
        return;
    }
    case 0xFE:
        *s0 = xm_sin(*s0);
        set_cc(c, 0);
        return;
    case 0xFF:
        *s0 = xm_cos(*s0);
        set_cc(c, 0);
        return;
    default:
        if (m >= 0xC0 && m <= 0xC7) { /* fld st(i) */
            double v = *st(c, m & 7);
            fpush(c, v);
            return;
        }
        if (m >= 0xC8 && m <= 0xCF) { /* fxch */
            double t = *s0;
            *s0 = *st(c, m & 7);
            *st(c, m & 7) = t;
            return;
        }
        x86_undefined(c);
    }
}

static void exec_register(X86Cpu *c, const X86Insn *i)
{
    uint8_t m = i->modrm;
    int sub = (m >> 3) & 7, r = m & 7;
    switch (i->opcode) {
    case 0xD8:
        if (sub == 2 || sub == 3) {
            compare(c, *st(c, 0), *st(c, r));
            if (sub == 3)
                fpop(c);
        } else {
            *st(c, 0) = arith(sub, *st(c, 0), *st(c, r));
        }
        return;
    case 0xD9:
        exec_d9_register(c, m);
        return;
    case 0xDA:
        if (m == 0xE9) { /* fucompp */
            compare(c, *st(c, 0), *st(c, 1));
            fpop(c);
            fpop(c);
            return;
        } else if (sub < 4) { /* fcmovb, fcmove, fcmovbe, fcmovu */
            static const int cc[4] = {2, 4, 6, 0xA};
            if (x86_cond(c, cc[sub]))
                *st(c, 0) = *st(c, r);
            return;
        }
        x86_undefined(c);
    case 0xDB:
        if (sub < 4) { /* fcmovnb, fcmovne, fcmovnbe, fcmovnu */
            static const int cc[4] = {3, 5, 7, 0xB};
            if (x86_cond(c, cc[sub]))
                *st(c, 0) = *st(c, r);
            return;
        }
        if (m == 0xE2) { /* fnclex */
            c->fsw &= 0x7F00;
            return;
        }
        if (m == 0xE3) { /* fninit */
            x86_fpu_init(c);
            c->fcw = 0x037F;
            return;
        }
        if (sub == 5 || sub == 6) { /* fucomi, fcomi */
            comi(c, *st(c, 0), *st(c, r));
            return;
        }
        if (m == 0xE0 || m == 0xE1 || m == 0xE4) /* feni, fdisi, fsetpm */
            return;
        x86_undefined(c);
    case 0xDC:
        if (sub == 2 || sub == 3) {
            compare(c, *st(c, 0), *st(c, r));
            if (sub == 3)
                fpop(c);
            return;
        }
        *st(c, r) = arith(sub >= 4 ? sub ^ 1 : sub, *st(c, r), *st(c, 0));
        return;
    case 0xDD:
        switch (sub) {
        case 0: /* ffree */
            c->ftag &= (uint8_t)~(1u << ((c->ftop + r) & 7));
            return;
        case 2:
            *st(c, r) = *st(c, 0);
            return;
        case 3:
            *st(c, r) = *st(c, 0);
            fpop(c);
            return;
        case 4:
        case 5:
            compare(c, *st(c, 0), *st(c, r));
            if (sub == 5)
                fpop(c);
            return;
        default:
            x86_undefined(c);
        }
    case 0xDE:
        if (sub == 3) {
            if (r != 1)
                x86_undefined(c);
            compare(c, *st(c, 0), *st(c, 1));
            fpop(c);
            fpop(c);
            return;
        }
        if (sub == 2) {
            compare(c, *st(c, 0), *st(c, r));
            fpop(c);
            return;
        }
        *st(c, r) = arith(sub >= 4 ? sub ^ 1 : sub, *st(c, r), *st(c, 0));
        fpop(c);
        return;
    default: /* 0xdf */
        if (m == 0xE0) { /* fnstsw ax */
            c->r[X86_RAX] = (c->r[X86_RAX] & ~0xFFFFull) | full_fsw(c);
            return;
        }
        if (sub == 5 || sub == 6) { /* fucomip, fcomip */
            comi(c, *st(c, 0), *st(c, r));
            fpop(c);
            return;
        }
        if (sub == 0) { /* ffreep */
            c->ftag &= (uint8_t)~(1u << ((c->ftop + r) & 7));
            fpop(c);
            return;
        }
        x86_undefined(c);
    }
}

/*
 * the instructions that save, load or clear the fpu's own state: fldenv, fldcw, fnstenv, fnstcw, frstor, fnsave,
 * fnstsw, fnclex, fninit and the 8087 leftovers. they leave the last instruction and data pointers and opcode alone.
 */
int x86_x87_control(const X86Insn *i)
{
    int sub = (i->modrm >> 3) & 7;
    if (i->mod != 3)
        return (i->opcode == 0xD9 || i->opcode == 0xDD) && sub >= 4;
    return (i->opcode == 0xDB && i->modrm >= 0xE0 && i->modrm <= 0xE4) || (i->opcode == 0xDF && i->modrm == 0xE0);
}

void x86_exec_x87(X86Cpu *c, const X86Insn *i)
{
    if (c->mmx_mode) {
        c->mmx_mode = 0;
        c->ftop = 0;
    }
    uint64_t a = i->mod == 3 ? 0 : x86_ea(c, i);
    if (!x86_x87_control(i)) {
        /* anti tamper code reads them back, as securom does. */
        c->fop = (uint16_t)(((i->opcode & 7) << 8) | i->modrm);
        c->fpu_ip = c->insn_rip;
        if (i->mod != 3)
            c->fpu_dp = a;
    }
    if (i->mod == 3)
        exec_register(c, i);
    else
        exec_memory(c, i, a);
}
