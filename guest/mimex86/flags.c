/*
 * lazy arithmetic flags: computed from the last recorded operation when
 * an instruction reads them.
 */
#include "internal.h"

static inline uint32_t parity(uint64_t v)
{
    return (__builtin_popcount((uint32_t)(v & 0xFF)) & 1) ? 0 : X86_PF;
}

uint32_t x86_arith_flags(X86Cpu *c)
{
    uint32_t op = c->lf_op;
    if (op == X86_LF_NONE)
        return c->flags & X86_ARITH_FLAGS;
    int size = 1 << (op & 3);
    uint64_t mask = x86_mask(size);
    uint64_t sign = 1ull << (size * 8 - 1);
    uint64_t res = c->lf_res & mask, a = c->lf_a & mask, b = c->lf_b & mask;
    uint32_t f = parity(res);
    if (!res)
        f |= X86_ZF;
    if (res & sign)
        f |= X86_SF;
    switch (op >> 2) {
    case X86_LF_ADD:
        if (res < a)
            f |= X86_CF;
        if ((a ^ res) & (b ^ res) & sign)
            f |= X86_OF;
        f |= (a ^ b ^ res) & X86_AF;
        break;
    case X86_LF_SUB:
        if (a < b)
            f |= X86_CF;
        if ((a ^ b) & (a ^ res) & sign)
            f |= X86_OF;
        f |= (a ^ b ^ res) & X86_AF;
        break;
    case X86_LF_LOGIC:
        break;
    case X86_LF_INC:
        if (c->lf_carry)
            f |= X86_CF;
        if (res == sign)
            f |= X86_OF;
        if ((res & 0xF) == 0)
            f |= X86_AF;
        break;
    case X86_LF_DEC:
        if (c->lf_carry)
            f |= X86_CF;
        if (res == sign - 1)
            f |= X86_OF;
        if ((res & 0xF) == 0xF)
            f |= X86_AF;
        break;
    case X86_LF_ADC:
        if (c->lf_carry ? res <= a : res < a)
            f |= X86_CF;
        if ((a ^ res) & (b ^ res) & sign)
            f |= X86_OF;
        f |= (a ^ b ^ res) & X86_AF;
        break;
    case X86_LF_SBB:
        if (c->lf_carry ? a <= b : a < b)
            f |= X86_CF;
        if ((a ^ b) & (a ^ res) & sign)
            f |= X86_OF;
        f |= (a ^ b ^ res) & X86_AF;
        break;
    }
    return f;
}

uint32_t x86_get_flags(X86Cpu *c)
{
    return (c->flags & ~X86_ARITH_FLAGS) | x86_arith_flags(c) | 2;
}

void x86_set_flags(X86Cpu *c, uint32_t flags)
{
    c->flags = (flags & 0x00257FD5u) | 2;
    c->lf_op = X86_LF_NONE;
}

/* computes sf, zf and pf from res. others supplies the remaining arithmetic flags. */
void x86_set_szp(X86Cpu *c, uint64_t res, int size, uint32_t others)
{
    uint64_t mask = x86_mask(size);
    res &= mask;
    uint32_t f = others | parity(res);
    if (!res)
        f |= X86_ZF;
    if (res & (1ull << (size * 8 - 1)))
        f |= X86_SF;
    c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
    c->lf_op = X86_LF_NONE;
}

int x86_cond(X86Cpu *c, int cc)
{
    uint32_t op = c->lf_op;
    int r;
    /* handle the common compare and test cases without materializing. */
    if ((op >> 2) == X86_LF_SUB && (cc & 0xE) != 0 && (cc & 0xE) != 0xA && (cc & 0xE) != 8) {
        int size = 1 << (op & 3);
        uint64_t mask = x86_mask(size);
        uint64_t a = c->lf_a & mask, b = c->lf_b & mask;
        int64_t sa = (int64_t)x86_sext(a, size), sb = (int64_t)x86_sext(b, size);
        switch (cc & 0xE) {
        case 2: r = a < b; break;
        case 4: r = a == b; break;
        case 6: r = a <= b; break;
        case 0xC: r = sa < sb; break;
        default: r = sa <= sb; break;
        }
        return r ^ (cc & 1);
    }
    uint32_t f = x86_arith_flags(c);
    switch (cc & 0xE) {
    case 0x0: r = (f & X86_OF) != 0; break;
    case 0x2: r = (f & X86_CF) != 0; break;
    case 0x4: r = (f & X86_ZF) != 0; break;
    case 0x6: r = (f & (X86_CF | X86_ZF)) != 0; break;
    case 0x8: r = (f & X86_SF) != 0; break;
    case 0xA: r = (f & X86_PF) != 0; break;
    case 0xC: r = !(f & X86_SF) != !(f & X86_OF); break;
    default: r = (f & X86_ZF) || (!(f & X86_SF) != !(f & X86_OF)); break;
    }
    return r ^ (cc & 1);
}
