/*
 * the interpreter for the general purpose instruction set, the decoded
 * block cache and the run loop. sse lives in sse.c and the x87 in x87.c.
 */
#include "internal.h"

/* ------------------------------------------------------------ registers */

static inline uint64_t get_reg(X86Cpu *c, int reg, int size, int rex)
{
    switch (size) {
    case 1:
        if (!rex && reg >= 4 && reg < 8)
            return (c->r[reg - 4] >> 8) & 0xFF;
        return c->r[reg] & 0xFF;
    case 2:
        return c->r[reg] & 0xFFFF;
    case 4:
        return c->r[reg] & 0xFFFFFFFF;
    default:
        return c->r[reg];
    }
}

static inline void set_reg(X86Cpu *c, int reg, int size, int rex, uint64_t v)
{
    switch (size) {
    case 1:
        if (!rex && reg >= 4 && reg < 8)
            c->r[reg - 4] = (c->r[reg - 4] & ~0xFF00ull) | ((v & 0xFF) << 8);
        else
            c->r[reg] = (c->r[reg] & ~0xFFull) | (v & 0xFF);
        break;
    case 2:
        c->r[reg] = (c->r[reg] & ~0xFFFFull) | (v & 0xFFFF);
        break;
    case 4:
        c->r[reg] = (uint32_t)v;
        break;
    default:
        c->r[reg] = v;
        break;
    }
}

static inline uint64_t read_rm(X86Cpu *c, const X86Insn *i, int size)
{
    if (i->mod == 3)
        return get_reg(c, i->rm, size, i->rex);
    return rdn(x86_ea(c, i), size);
}

static inline void write_rm(X86Cpu *c, const X86Insn *i, int size, uint64_t v)
{
    if (i->mod == 3)
        set_reg(c, i->rm, size, i->rex, v);
    else
        wrn(x86_ea(c, i), size, v);
}

static inline uint64_t read_greg(X86Cpu *c, const X86Insn *i, int size)
{
    return get_reg(c, i->reg, size, i->rex);
}

static inline void write_greg(X86Cpu *c, const X86Insn *i, int size, uint64_t v)
{
    set_reg(c, i->reg, size, i->rex, v);
}

static inline int log2_size(int size)
{
    return __builtin_ctz((unsigned)size);
}

static inline int carry(X86Cpu *c)
{
    return (x86_arith_flags(c) & X86_CF) != 0;
}

/* ------------------------------------------------------------ faults */

__attribute__((noreturn)) void x86_fault(X86Cpu *c, uint32_t code, uint64_t p0, uint64_t p1)
{
    c->rip = c->insn_rip;
    c->env->fault(c, code, p0, p1);
    __builtin_unreachable();
}

__attribute__((noreturn)) void x86_undefined(X86Cpu *c)
{
    x86_fault(c, X86_STATUS_ILLEGAL_INSTRUCTION, 0, 0);
}

/* ------------------------------------------------------------ atomics */

static int cas(uint64_t addr, int size, uint64_t expected, uint64_t desired)
{
    switch (size) {
    case 1: {
        uint8_t e = (uint8_t)expected;
        return __atomic_compare_exchange_n((uint8_t *)(uintptr_t)addr, &e, (uint8_t)desired, 0, __ATOMIC_SEQ_CST,
                                           __ATOMIC_SEQ_CST);
    }
    case 2: {
        uint16_t e = (uint16_t)expected;
        return __atomic_compare_exchange_n((uint16_t *)(uintptr_t)addr, &e, (uint16_t)desired, 0, __ATOMIC_SEQ_CST,
                                           __ATOMIC_SEQ_CST);
    }
    case 4: {
        uint32_t e = (uint32_t)expected;
        return __atomic_compare_exchange_n((uint32_t *)(uintptr_t)addr, &e, (uint32_t)desired, 0, __ATOMIC_SEQ_CST,
                                           __ATOMIC_SEQ_CST);
    }
    default: {
        uint64_t e = expected;
        return __atomic_compare_exchange_n((uint64_t *)(uintptr_t)addr, &e, desired, 0, __ATOMIC_SEQ_CST,
                                           __ATOMIC_SEQ_CST);
    }
    }
}

static uint64_t xchg_mem(uint64_t addr, int size, uint64_t v)
{
    switch (size) {
    case 1: return __atomic_exchange_n((uint8_t *)(uintptr_t)addr, (uint8_t)v, __ATOMIC_SEQ_CST);
    case 2: return __atomic_exchange_n((uint16_t *)(uintptr_t)addr, (uint16_t)v, __ATOMIC_SEQ_CST);
    case 4: return __atomic_exchange_n((uint32_t *)(uintptr_t)addr, (uint32_t)v, __ATOMIC_SEQ_CST);
    default: return __atomic_exchange_n((uint64_t *)(uintptr_t)addr, v, __ATOMIC_SEQ_CST);
    }
}

/* ------------------------------------------------------------ alu */

enum { ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP };

static uint64_t alu(X86Cpu *c, int op, int size, uint64_t a, uint64_t b)
{
    int lg = log2_size(size);
    uint64_t mask = x86_mask(size), res;
    a &= mask;
    b &= mask;
    switch (op) {
    case ALU_ADD:
        res = a + b;
        c->lf_op = X86_LF(X86_LF_ADD, lg);
        break;
    case ALU_OR:
        res = a | b;
        c->lf_op = X86_LF(X86_LF_LOGIC, lg);
        break;
    case ALU_ADC: {
        uint64_t cf = (uint64_t)carry(c);
        res = a + b + cf;
        c->lf_carry = cf;
        c->lf_op = X86_LF(X86_LF_ADC, lg);
        break;
    }
    case ALU_SBB: {
        uint64_t cf = (uint64_t)carry(c);
        res = a - b - cf;
        c->lf_carry = cf;
        c->lf_op = X86_LF(X86_LF_SBB, lg);
        break;
    }
    case ALU_AND:
        res = a & b;
        c->lf_op = X86_LF(X86_LF_LOGIC, lg);
        break;
    case ALU_XOR:
        res = a ^ b;
        c->lf_op = X86_LF(X86_LF_LOGIC, lg);
        break;
    default: /* sub, cmp */
        res = a - b;
        c->lf_op = X86_LF(X86_LF_SUB, lg);
        break;
    }
    res &= mask;
    c->lf_a = a;
    c->lf_b = b;
    c->lf_res = res;
    return res;
}

/* applies an alu operation to the modrm destination, atomically under lock. */
static void alu_rm(X86Cpu *c, const X86Insn *i, int op, int size, uint64_t b)
{
    if (i->mod == 3) {
        uint64_t a = get_reg(c, i->rm, size, i->rex);
        uint64_t r = alu(c, op, size, a, b);
        if (op != ALU_CMP)
            set_reg(c, i->rm, size, i->rex, r);
        return;
    }
    uint64_t addr = x86_ea(c, i);
    if (i->lock && op != ALU_CMP) {
        for (;;) {
            uint64_t a = rdn(addr, size);
            uint64_t r = alu(c, op, size, a, b);
            if (cas(addr, size, a, r))
                return;
        }
    }
    uint64_t r = alu(c, op, size, rdn(addr, size), b);
    if (op != ALU_CMP)
        wrn(addr, size, r);
}

static uint64_t inc_dec(X86Cpu *c, int size, uint64_t a, int dec)
{
    uint64_t cf = (uint64_t)carry(c);
    uint64_t mask = x86_mask(size);
    uint64_t res = (dec ? a - 1 : a + 1) & mask;
    c->lf_carry = cf;
    c->lf_a = a & mask;
    c->lf_b = 1;
    c->lf_res = res;
    c->lf_op = X86_LF(dec ? X86_LF_DEC : X86_LF_INC, log2_size(size));
    return res;
}

static void inc_dec_rm(X86Cpu *c, const X86Insn *i, int size, int dec)
{
    if (i->mod == 3) {
        set_reg(c, i->rm, size, i->rex, inc_dec(c, size, get_reg(c, i->rm, size, i->rex), dec));
        return;
    }
    uint64_t addr = x86_ea(c, i);
    if (i->lock) {
        for (;;) {
            uint64_t a = rdn(addr, size);
            uint64_t r = inc_dec(c, size, a, dec);
            if (cas(addr, size, a, r))
                return;
        }
    }
    wrn(addr, size, inc_dec(c, size, rdn(addr, size), dec));
}

/* group 2: rol, ror, rcl, rcr, shl, shr, sal, sar. */
static uint64_t shift(X86Cpu *c, int op, int size, uint64_t v, unsigned count)
{
    unsigned bits = (unsigned)size * 8;
    uint64_t mask = x86_mask(size);
    uint64_t sign = 1ull << (bits - 1);
    count &= size == 8 ? 63 : 31;
    if (!count)
        return v & mask;
    v &= mask;
    uint32_t f = x86_arith_flags(c);
    uint64_t res;
    int cf, of;
    switch (op) {
    case 0: { /* rol */
        unsigned n = count % bits;
        res = n ? ((v << n) | (v >> (bits - n))) & mask : v;
        cf = (int)(res & 1);
        of = ((res & sign) != 0) ^ cf;
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | (f & ~(X86_CF | X86_OF)) | (cf ? X86_CF : 0) | (of ? X86_OF : 0);
        c->lf_op = X86_LF_NONE;
        return res;
    }
    case 1: { /* ror */
        unsigned n = count % bits;
        res = n ? ((v >> n) | (v << (bits - n))) & mask : v;
        cf = (res & sign) != 0;
        of = ((res & sign) != 0) ^ ((res & (sign >> 1)) != 0);
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | (f & ~(X86_CF | X86_OF)) | (cf ? X86_CF : 0) | (of ? X86_OF : 0);
        c->lf_op = X86_LF_NONE;
        return res;
    }
    case 2: { /* rcl */
        unsigned n = size < 4 ? count % (bits + 1) : count;
        cf = (f & X86_CF) != 0;
        res = v;
        for (unsigned k = 0; k < n; k++) {
            int out = (res & sign) != 0;
            res = ((res << 1) | (uint64_t)cf) & mask;
            cf = out;
        }
        of = ((res & sign) != 0) ^ cf;
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | (f & ~(X86_CF | X86_OF)) | (cf ? X86_CF : 0) | (of ? X86_OF : 0);
        c->lf_op = X86_LF_NONE;
        return res;
    }
    case 3: { /* rcr */
        unsigned n = size < 4 ? count % (bits + 1) : count;
        cf = (f & X86_CF) != 0;
        res = v;
        for (unsigned k = 0; k < n; k++) {
            int out = (int)(res & 1);
            res = (res >> 1) | (cf ? sign : 0);
            cf = out;
        }
        of = ((res & sign) != 0) ^ ((res & (sign >> 1)) != 0);
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | (f & ~(X86_CF | X86_OF)) | (cf ? X86_CF : 0) | (of ? X86_OF : 0);
        c->lf_op = X86_LF_NONE;
        return res;
    }
    case 4:
    case 6: /* shl, sal */
        res = count < bits ? (v << count) & mask : 0;
        cf = count <= bits ? (int)((v >> (bits - count)) & 1) : 0;
        of = ((res & sign) != 0) ^ cf;
        break;
    case 5: /* shr */
        res = count < bits ? v >> count : 0;
        cf = count <= bits ? (int)((v >> (count - 1)) & 1) : 0;
        of = (v & sign) != 0;
        break;
    default: { /* sar */
        int64_t sv = (int64_t)x86_sext(v, size);
        res = (uint64_t)(sv >> (count < bits ? count : bits - 1)) & mask;
        cf = (int)((sv >> (count - 1 < bits ? count - 1 : bits - 1)) & 1);
        of = 0;
        break;
    }
    }
    x86_set_szp(c, res, size, (cf ? X86_CF : 0) | (of ? X86_OF : 0));
    return res;
}

/* shld when left, shrd otherwise. */
static uint64_t double_shift(X86Cpu *c, int size, uint64_t dst, uint64_t src, unsigned count, int left)
{
    unsigned bits = (unsigned)size * 8;
    uint64_t mask = x86_mask(size);
    count &= size == 8 ? 63 : 31;
    if (!count)
        return dst & mask;
    dst &= mask;
    src &= mask;
    uint64_t res;
    int cf;
    if (size == 2 && count > 16) {
        /* undefined; behave like a 32 bit shift of src:dst. */
        uint64_t wide = left ? (dst << 16) | src : (src << 16) | dst;
        if (left) {
            res = ((wide << count) >> 16) & mask;
            cf = (int)((wide >> (32 - count)) & 1);
        } else {
            res = (wide >> count) & mask;
            cf = (int)((wide >> (count - 1)) & 1);
        }
    } else if (left) {
        res = count < bits ? ((dst << count) | (src >> (bits - count))) & mask : src;
        cf = (int)((dst >> (bits - count)) & 1);
    } else {
        res = count < bits ? ((dst >> count) | (src << (bits - count))) & mask : src;
        cf = (int)((dst >> (count - 1)) & 1);
    }
    uint64_t sign = 1ull << (bits - 1);
    int of = ((res ^ dst) & sign) != 0;
    x86_set_szp(c, res, size, (cf ? X86_CF : 0) | (of ? X86_OF : 0));
    return res;
}

/* ------------------------------------------------------------ stack */

static inline uint64_t stack_mask(X86Cpu *c)
{
    return c->mode64 ? ~0ull : 0xFFFFFFFFull;
}

static inline void push(X86Cpu *c, int size, uint64_t v)
{
    uint64_t sp = (c->r[X86_RSP] - (uint64_t)size) & stack_mask(c);
    wrn(sp, size, v);
    c->r[X86_RSP] = sp;
}

static inline uint64_t pop(X86Cpu *c, int size)
{
    uint64_t sp = c->r[X86_RSP];
    uint64_t v = rdn(sp, size);
    c->r[X86_RSP] = (sp + (uint64_t)size) & stack_mask(c);
    return v;
}

static inline uint64_t branch_target(X86Cpu *c, uint64_t target)
{
    return c->mode64 ? target : (uint32_t)target;
}

/* ------------------------------------------------------------ mul and div */

static void mul_one(X86Cpu *c, const X86Insn *i, int size, int is_signed)
{
    uint64_t src = read_rm(c, i, size);
    uint64_t lo, hi;
    int overflow;
    switch (size) {
    case 1: {
        uint16_t r;
        if (is_signed) {
            r = (uint16_t)((int16_t)(int8_t)c->r[X86_RAX] * (int16_t)(int8_t)src);
            overflow = (int16_t)r != (int8_t)r;
        } else {
            r = (uint16_t)((c->r[X86_RAX] & 0xFF) * (src & 0xFF));
            overflow = (r >> 8) != 0;
        }
        set_reg(c, X86_RAX, 2, 0, r);
        lo = r & 0xFF;
        break;
    }
    case 2: {
        uint32_t r;
        if (is_signed) {
            r = (uint32_t)((int32_t)(int16_t)c->r[X86_RAX] * (int32_t)(int16_t)src);
            overflow = (int32_t)r != (int16_t)r;
        } else {
            r = (uint32_t)(c->r[X86_RAX] & 0xFFFF) * (uint32_t)(src & 0xFFFF);
            overflow = (r >> 16) != 0;
        }
        set_reg(c, X86_RAX, 2, 0, r & 0xFFFF);
        set_reg(c, X86_RDX, 2, 0, r >> 16);
        lo = r & 0xFFFF;
        break;
    }
    case 4: {
        uint64_t r;
        if (is_signed) {
            r = (uint64_t)((int64_t)(int32_t)c->r[X86_RAX] * (int64_t)(int32_t)src);
            overflow = (int64_t)r != (int32_t)r;
        } else {
            r = (c->r[X86_RAX] & 0xFFFFFFFF) * (src & 0xFFFFFFFF);
            overflow = (r >> 32) != 0;
        }
        c->r[X86_RAX] = (uint32_t)r;
        c->r[X86_RDX] = r >> 32;
        lo = (uint32_t)r;
        break;
    }
    default: {
        if (is_signed) {
            __int128 r = (__int128)(int64_t)c->r[X86_RAX] * (__int128)(int64_t)src;
            lo = (uint64_t)r;
            hi = (uint64_t)(r >> 64);
            overflow = r != (__int128)(int64_t)lo;
        } else {
            unsigned __int128 r = (unsigned __int128)c->r[X86_RAX] * src;
            lo = (uint64_t)r;
            hi = (uint64_t)(r >> 64);
            overflow = hi != 0;
        }
        c->r[X86_RAX] = lo;
        c->r[X86_RDX] = hi;
        break;
    }
    }
    x86_set_szp(c, lo, size, overflow ? (X86_CF | X86_OF) : 0);
}

/* the truncated two and three operand imul. */
static uint64_t imul_trunc(X86Cpu *c, int size, uint64_t a, uint64_t b)
{
    int64_t sa = (int64_t)x86_sext(a, size), sb = (int64_t)x86_sext(b, size);
    uint64_t res;
    int overflow;
    if (size == 8) {
        __int128 r = (__int128)sa * sb;
        res = (uint64_t)r;
        overflow = r != (__int128)(int64_t)res;
    } else {
        int64_t r = sa * sb;
        res = (uint64_t)r & x86_mask(size);
        overflow = r != (int64_t)x86_sext(res, size);
    }
    x86_set_szp(c, res, size, overflow ? (X86_CF | X86_OF) : 0);
    return res;
}

static void div_one(X86Cpu *c, const X86Insn *i, int size, int is_signed)
{
    uint64_t src = read_rm(c, i, size) & x86_mask(size);
    if (!src)
        x86_fault(c, X86_STATUS_INTEGER_DIVIDE_BY_ZERO, 0, 0);
    switch (size) {
    case 1: {
        uint16_t n = (uint16_t)c->r[X86_RAX];
        if (is_signed) {
            int32_t q = (int16_t)n / (int8_t)src, r = (int16_t)n % (int8_t)src;
            if (q != (int8_t)q)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            set_reg(c, X86_RAX, 2, 0, ((uint16_t)(uint8_t)r << 8) | (uint8_t)q);
        } else {
            uint32_t q = n / (uint8_t)src, r = n % (uint8_t)src;
            if (q > 0xFF)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            set_reg(c, X86_RAX, 2, 0, (r << 8) | q);
        }
        break;
    }
    case 2: {
        uint32_t n = (uint32_t)((c->r[X86_RDX] & 0xFFFF) << 16) | (uint32_t)(c->r[X86_RAX] & 0xFFFF);
        if (is_signed) {
            int32_t q = (int32_t)n / (int16_t)src, r = (int32_t)n % (int16_t)src;
            if (q != (int16_t)q)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            set_reg(c, X86_RAX, 2, 0, (uint16_t)q);
            set_reg(c, X86_RDX, 2, 0, (uint16_t)r);
        } else {
            uint32_t q = n / (uint16_t)src, r = n % (uint16_t)src;
            if (q > 0xFFFF)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            set_reg(c, X86_RAX, 2, 0, q);
            set_reg(c, X86_RDX, 2, 0, r);
        }
        break;
    }
    case 4: {
        uint64_t n = ((c->r[X86_RDX] & 0xFFFFFFFF) << 32) | (c->r[X86_RAX] & 0xFFFFFFFF);
        if (is_signed) {
            int64_t sn = (int64_t)n, d = (int32_t)src;
            if (sn == INT64_MIN && d == -1)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            int64_t q = sn / d, r = sn % d;
            if (q != (int32_t)q)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            c->r[X86_RAX] = (uint32_t)q;
            c->r[X86_RDX] = (uint32_t)r;
        } else {
            uint64_t q = n / src, r = n % src;
            if (q > 0xFFFFFFFF)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            c->r[X86_RAX] = q;
            c->r[X86_RDX] = r;
        }
        break;
    }
    default: {
        unsigned __int128 n = ((unsigned __int128)c->r[X86_RDX] << 64) | c->r[X86_RAX];
        if (is_signed) {
            __int128 sn = (__int128)n, d = (int64_t)src;
            if (d == -1 && sn == (__int128)((unsigned __int128)1 << 127))
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            __int128 q = sn / d, r = sn % d;
            if (q != (__int128)(int64_t)q)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            c->r[X86_RAX] = (uint64_t)q;
            c->r[X86_RDX] = (uint64_t)r;
        } else {
            unsigned __int128 q = n / src, r = n % src;
            if (q >> 64)
                x86_fault(c, X86_STATUS_INTEGER_OVERFLOW, 0, 0);
            c->r[X86_RAX] = (uint64_t)q;
            c->r[X86_RDX] = (uint64_t)r;
        }
        break;
    }
    }
}

/* ------------------------------------------------------------ strings */

static void string_op(X86Cpu *c, const X86Insn *i)
{
    uint8_t op = i->opcode;
    int size = (op & 1) ? i->opsize : 1;
    uint64_t amask = i->addrsize == 4 ? 0xFFFFFFFFull : ~0ull;
    int64_t delta = (c->flags & X86_DF) ? -size : size;
    uint64_t src_base = i->seg == 4 ? c->fs_base : i->seg == 5 ? c->gs_base : 0;
    int rep = i->rep != 0;
    uint64_t count = rep ? c->r[X86_RCX] & amask : 1;

    /* fast paths for forward copies and fills. */
    if (rep && delta > 0 && count) {
        uint64_t si = c->r[X86_RSI] & amask, di = c->r[X86_RDI] & amask;
        uint64_t bytes = count * (uint64_t)size;
        if ((op & 0xFE) == 0xA4 && amask == ~0ull && !src_base && (di <= si || di >= si + bytes)) {
            __builtin_memmove((void *)(uintptr_t)di, (const void *)(uintptr_t)si, bytes);
            c->r[X86_RSI] = si + bytes;
            c->r[X86_RDI] = di + bytes;
            c->r[X86_RCX] = 0;
            return;
        }
        if (op == 0xAA && amask == ~0ull) {
            __builtin_memset((void *)(uintptr_t)di, (int)(c->r[X86_RAX] & 0xFF), bytes);
            c->r[X86_RDI] = di + bytes;
            c->r[X86_RCX] = 0;
            return;
        }
    }

    while (count) {
        uint64_t si = c->r[X86_RSI] & amask, di = c->r[X86_RDI] & amask;
        int stop = 0;
        switch (op & 0xFE) {
        case 0xA4: /* movs */
            wrn(di, size, rdn(si + src_base, size));
            c->r[X86_RSI] = (si + (uint64_t)delta) & amask;
            c->r[X86_RDI] = (di + (uint64_t)delta) & amask;
            break;
        case 0xA6: /* cmps */
            alu(c, ALU_CMP, size, rdn(si + src_base, size), rdn(di, size));
            c->r[X86_RSI] = (si + (uint64_t)delta) & amask;
            c->r[X86_RDI] = (di + (uint64_t)delta) & amask;
            stop = 1;
            break;
        case 0xAA: /* stos */
            wrn(di, size, c->r[X86_RAX]);
            c->r[X86_RDI] = (di + (uint64_t)delta) & amask;
            break;
        case 0xAC: /* lods */
            set_reg(c, X86_RAX, size, 1, rdn(si + src_base, size));
            c->r[X86_RSI] = (si + (uint64_t)delta) & amask;
            break;
        default: /* scas */
            alu(c, ALU_CMP, size, c->r[X86_RAX], rdn(di, size));
            c->r[X86_RDI] = (di + (uint64_t)delta) & amask;
            stop = 1;
            break;
        }
        if (!rep)
            break;
        count--;
        c->r[X86_RCX] = (c->r[X86_RCX] & ~amask) | count;
        if (amask != ~0ull)
            c->r[X86_RCX] = count;
        if (stop) {
            int zf = x86_cond(c, 4);
            if (i->rep == 0xF3 ? !zf : zf)
                break;
        }
    }
}

/* ------------------------------------------------------------ cpuid */

static void put_string(uint32_t *regs, const char *s)
{
    for (int k = 0; k < 4; k++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++)
            v |= (uint32_t)(uint8_t)s[k * 4 + b] << (8 * b);
        regs[k] = v;
    }
}

static void cpuid(X86Cpu *c)
{
    uint32_t leaf = (uint32_t)c->r[X86_RAX], sub = (uint32_t)c->r[X86_RCX];
    uint32_t r[4] = {0, 0, 0, 0}; /* eax, ebx, ecx, edx */
    static const char brand[] = "Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    switch (leaf) {
    case 0:
        r[0] = 0x16;
        r[1] = 0x756E6547; /* Genu */
        r[3] = 0x49656E69; /* ineI */
        r[2] = 0x6C65746E; /* ntel */
        break;
    case 1:
        r[0] = 0x000906EA; /* family 6, model 0x9e, stepping 10 */
        r[1] = (8u << 8) | (1u << 16);
        r[2] = (1u << 0) | (1u << 9) | (1u << 13) | (1u << 19) | (1u << 23); /* sse3 ssse3 cx16 sse4.1 popcnt */
        r[3] = 0x078BFBFFu & ~(1u << 28); /* fpu .. sse2, without htt */
        break;
    case 7:
        break;
    case 0x80000000:
        r[0] = 0x80000008;
        break;
    case 0x80000001:
        r[2] = (1u << 0) | (1u << 8); /* lahf in 64 bit mode, prefetchw */
        r[3] = (1u << 11) | (1u << 20) | (c->mode64 ? (1u << 29) : 0); /* syscall, nx, long mode */
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        put_string(r, brand + (leaf - 0x80000002) * 16);
        { uint32_t t = r[1]; r[1] = r[1]; (void)t; }
        break;
    case 0x80000006:
        r[2] = (256u << 16) | (8u << 12) | 64; /* 256K l2, 8 way, 64 byte lines */
        break;
    case 0x80000008:
        r[0] = 0x3027;
        break;
    case 4:
        if (sub < 4) {
            static const uint32_t a[4] = {0x121, 0x122, 0x143, 0x163};
            static const uint32_t b[4] = {0x01C0003F, 0x01C0003F, 0x00C0003F, 0x03C0003F};
            static const uint32_t cc[4] = {63, 63, 1023, 12287};
            r[0] = a[sub];
            r[1] = b[sub];
            r[2] = cc[sub];
        }
        break;
    default:
        break;
    }
    c->r[X86_RAX] = r[0];
    c->r[X86_RBX] = r[1];
    c->r[X86_RCX] = r[2];
    c->r[X86_RDX] = r[3];
    (void)sub;
}

/* ------------------------------------------------------------ bit tests */

static void bit_test(X86Cpu *c, const X86Insn *i, int op, int size, uint64_t offset, int from_reg)
{
    /* op: 0 bt, 1 bts, 2 btr, 3 btc. */
    uint32_t f = x86_arith_flags(c) & ~X86_CF;
    int bit;
    if (i->mod == 3) {
        unsigned n = (unsigned)(offset & (size * 8 - 1));
        uint64_t v = get_reg(c, i->rm, size, i->rex);
        bit = (int)((v >> n) & 1);
        if (op) {
            uint64_t m = 1ull << n;
            v = op == 1 ? v | m : op == 2 ? v & ~m : v ^ m;
            set_reg(c, i->rm, size, i->rex, v);
        }
    } else {
        uint64_t addr = x86_ea(c, i);
        int64_t off = from_reg ? (int64_t)x86_sext(offset, size) : (int64_t)(offset & (uint64_t)(size * 8 - 1));
        addr += (uint64_t)(off >> 3);
        unsigned n = (unsigned)(off & 7);
        uint8_t m = (uint8_t)(1u << n);
        if (op && i->lock) {
            uint8_t old;
            if (op == 1)
                old = __atomic_fetch_or((uint8_t *)(uintptr_t)addr, m, __ATOMIC_SEQ_CST);
            else if (op == 2)
                old = __atomic_fetch_and((uint8_t *)(uintptr_t)addr, (uint8_t)~m, __ATOMIC_SEQ_CST);
            else
                old = __atomic_fetch_xor((uint8_t *)(uintptr_t)addr, m, __ATOMIC_SEQ_CST);
            bit = (old >> n) & 1;
        } else {
            uint8_t v = rd8(addr);
            bit = (v >> n) & 1;
            if (op)
                wr8(addr, op == 1 ? v | m : op == 2 ? v & (uint8_t)~m : v ^ m);
        }
    }
    c->flags = (c->flags & ~X86_ARITH_FLAGS) | f | (bit ? X86_CF : 0);
    c->lf_op = X86_LF_NONE;
}

/* ------------------------------------------------------------ segments */

static uint16_t selector(X86Cpu *c, int seg)
{
    static const uint16_t sel64[6] = {0x2B, 0x33, 0x2B, 0x2B, 0x53, 0x2B};
    static const uint16_t sel32[6] = {0x2B, 0x23, 0x2B, 0x2B, 0x53, 0x2B};
    return seg < 6 ? (c->mode64 ? sel64[seg] : sel32[seg]) : 0;
}

/* ------------------------------------------------------------ interrupts */

static void interrupt(X86Cpu *c, uint8_t vector)
{
    switch (vector) {
    case 3:
        x86_fault(c, X86_STATUS_BREAKPOINT, 0, 0);
    case 0x29:
        x86_fault(c, X86_STATUS_STACK_BUFFER_OVERRUN, c->r[X86_RCX], 0);
    case 0x2C:
        x86_fault(c, 0xC0000420u, 0, 0); /* STATUS_ASSERTION_FAILURE */
    case 0x2D:
        /* debug service: without a debugger it reports a breakpoint after the int3 that follows. */
        x86_fault(c, X86_STATUS_BREAKPOINT, 0, 0);
    case 0x2E:
        c->env->syscall(c);
        return;
    default:
        x86_fault(c, X86_STATUS_ACCESS_VIOLATION, 0, ~0ull);
    }
}

/* ------------------------------------------------------------ 0f map */

static void exec_0f(X86Cpu *c, const X86Insn *i)
{
    uint8_t op = i->opcode;
    int size = i->opsize;
    switch (op) {
    case 0x05: /* syscall: rcx and r11 change first, a service like NtContinue may then load everything */
        c->r[X86_RCX] = c->rip;
        c->r[X86_R11] = x86_get_flags(c);
        c->env->syscall(c);
        return;
    case 0x0B: /* ud2 */
    case 0xB9: /* ud1 */
    case 0xFF: /* ud0 */
        x86_undefined(c);
    case 0x0D: /* prefetch */
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: /* hints, nop */
        return;
    case 0x00:
    case 0x01:
        if (op == 0x01 && i->modrm == 0xD0 && 0) /* xgetbv is reserved to processors reporting osxsave */
            return;
        x86_undefined(c);
    case 0x31: { /* rdtsc */
        uint64_t t = c->env->timestamp(c);
        c->r[X86_RAX] = (uint32_t)t;
        c->r[X86_RDX] = t >> 32;
        return;
    }
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: { /* cmovcc */
        uint64_t v = read_rm(c, i, size);
        if (x86_cond(c, op & 15))
            write_greg(c, i, size, v);
        else if (size == 4)
            write_greg(c, i, 4, read_greg(c, i, 4));
        return;
    }
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: /* jcc rel */
        if (x86_cond(c, op & 15))
            c->rip = branch_target(c, c->rip + i->imm);
        return;
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: /* setcc */
        write_rm(c, i, 1, (uint64_t)x86_cond(c, op & 15));
        return;
    case 0xA0: /* push fs */
    case 0xA8: /* push gs */
        push(c, size, selector(c, op == 0xA0 ? 4 : 5));
        return;
    case 0xA1: /* pop fs */
    case 0xA9: /* pop gs */
        pop(c, size);
        return;
    case 0xA2:
        cpuid(c);
        return;
    case 0xA3:
        bit_test(c, i, 0, size, read_greg(c, i, size), 1);
        return;
    case 0xAB:
        bit_test(c, i, 1, size, read_greg(c, i, size), 1);
        return;
    case 0xB3:
        bit_test(c, i, 2, size, read_greg(c, i, size), 1);
        return;
    case 0xBB:
        bit_test(c, i, 3, size, read_greg(c, i, size), 1);
        return;
    case 0xBA: {
        int sub = (i->modrm >> 3) & 7;
        if (sub < 4)
            x86_undefined(c);
        bit_test(c, i, sub - 4, size, i->imm & 0xFF, 0);
        return;
    }
    case 0xA4: /* shld imm */
    case 0xA5: /* shld cl */
    case 0xAC: /* shrd imm */
    case 0xAD: { /* shrd cl */
        unsigned count = (op & 1) ? (unsigned)(c->r[X86_RCX] & 0xFF) : (unsigned)(i->imm & 0xFF);
        int left = op < 0xA8;
        if (i->mod == 3) {
            uint64_t r = double_shift(c, size, get_reg(c, i->rm, size, i->rex), read_greg(c, i, size), count, left);
            set_reg(c, i->rm, size, i->rex, r);
        } else {
            uint64_t addr = x86_ea(c, i);
            wrn(addr, size, double_shift(c, size, rdn(addr, size), read_greg(c, i, size), count, left));
        }
        return;
    }
    case 0xAE: {
        int sub = (i->modrm >> 3) & 7;
        if (i->mod == 3) {
            if (sub >= 5 && !i->mandatory) { /* lfence, mfence, sfence */
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                return;
            }
            x86_undefined(c);
        }
        uint64_t addr = x86_ea(c, i);
        switch (sub) {
        case 0:
            x86_fxsave(c, (uint8_t *)(uintptr_t)addr);
            return;
        case 1:
            x86_fxrstor(c, (const uint8_t *)(uintptr_t)addr);
            return;
        case 2:
            x86_ldmxcsr(c, rd32(addr));
            return;
        case 3:
            wr32(addr, c->mxcsr);
            return;
        case 7: /* clflush */
            return;
        default:
            x86_undefined(c);
        }
    }
    case 0xAF:
        write_greg(c, i, size, imul_trunc(c, size, read_greg(c, i, size), read_rm(c, i, size)));
        return;
    case 0xB0:
    case 0xB1: { /* cmpxchg */
        int sz = op == 0xB0 ? 1 : size;
        uint64_t mask = x86_mask(sz);
        uint64_t acc = c->r[X86_RAX] & mask, src = read_greg(c, i, sz);
        if (i->mod == 3) {
            uint64_t dst = get_reg(c, i->rm, sz, i->rex);
            alu(c, ALU_CMP, sz, acc, dst);
            if (acc == dst) {
                set_reg(c, i->rm, sz, i->rex, src);
            } else {
                set_reg(c, i->rm, sz, i->rex, dst);
                set_reg(c, X86_RAX, sz, 1, dst);
            }
            return;
        }
        uint64_t addr = x86_ea(c, i);
        uint64_t old;
        switch (sz) {
        case 1: { uint8_t e = (uint8_t)acc; __atomic_compare_exchange_n((uint8_t *)(uintptr_t)addr, &e, (uint8_t)src, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); old = e; break; }
        case 2: { uint16_t e = (uint16_t)acc; __atomic_compare_exchange_n((uint16_t *)(uintptr_t)addr, &e, (uint16_t)src, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); old = e; break; }
        case 4: { uint32_t e = (uint32_t)acc; __atomic_compare_exchange_n((uint32_t *)(uintptr_t)addr, &e, (uint32_t)src, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); old = e; break; }
        default: { uint64_t e = acc; __atomic_compare_exchange_n((uint64_t *)(uintptr_t)addr, &e, src, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); old = e; break; }
        }
        alu(c, ALU_CMP, sz, acc, old);
        if (old != acc)
            set_reg(c, X86_RAX, sz, 1, old);
        return;
    }
    case 0xB6: /* movzx r, rm8 */
        write_greg(c, i, size, read_rm(c, i, 1));
        return;
    case 0xB7: /* movzx r, rm16 */
        write_greg(c, i, size, read_rm(c, i, 2));
        return;
    case 0xBE: /* movsx r, rm8 */
        write_greg(c, i, size, x86_sext(read_rm(c, i, 1), 1));
        return;
    case 0xBF: /* movsx r, rm16 */
        write_greg(c, i, size, x86_sext(read_rm(c, i, 2), 2));
        return;
    case 0xB8: { /* popcnt */
        if (i->rep != 0xF3)
            x86_undefined(c);
        uint64_t v = read_rm(c, i, size) & x86_mask(size);
        uint64_t n = (uint64_t)__builtin_popcountll(v);
        write_greg(c, i, size, n);
        c->flags &= ~X86_ARITH_FLAGS;
        if (!v)
            c->flags |= X86_ZF;
        c->lf_op = X86_LF_NONE;
        return;
    }
    case 0xBC: /* bsf, and tzcnt as bsf without bmi1 */
    case 0xBD: { /* bsr */
        uint64_t v = read_rm(c, i, size) & x86_mask(size);
        uint32_t f = x86_arith_flags(c) & ~X86_ZF;
        if (!v) {
            f |= X86_ZF;
        } else {
            write_greg(c, i, size, op == 0xBC ? (uint64_t)__builtin_ctzll(v) : (uint64_t)(63 - __builtin_clzll(v)));
        }
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        c->lf_op = X86_LF_NONE;
        return;
    }
    case 0xC0:
    case 0xC1: { /* xadd */
        int sz = op == 0xC0 ? 1 : size;
        uint64_t src = read_greg(c, i, sz);
        if (i->mod == 3) {
            uint64_t dst = get_reg(c, i->rm, sz, i->rex);
            uint64_t r = alu(c, ALU_ADD, sz, dst, src);
            write_greg(c, i, sz, dst);
            set_reg(c, i->rm, sz, i->rex, r);
            return;
        }
        uint64_t addr = x86_ea(c, i), old;
        switch (sz) {
        case 1: old = __atomic_fetch_add((uint8_t *)(uintptr_t)addr, (uint8_t)src, __ATOMIC_SEQ_CST); break;
        case 2: old = __atomic_fetch_add((uint16_t *)(uintptr_t)addr, (uint16_t)src, __ATOMIC_SEQ_CST); break;
        case 4: old = __atomic_fetch_add((uint32_t *)(uintptr_t)addr, (uint32_t)src, __ATOMIC_SEQ_CST); break;
        default: old = __atomic_fetch_add((uint64_t *)(uintptr_t)addr, src, __ATOMIC_SEQ_CST); break;
        }
        alu(c, ALU_ADD, sz, old, src);
        write_greg(c, i, sz, old);
        return;
    }
    case 0xC3: /* movnti */
        if (i->mod == 3)
            x86_undefined(c);
        wrn(x86_ea(c, i), size == 8 ? 8 : 4, read_greg(c, i, size == 8 ? 8 : 4));
        return;
    case 0xC7: {
        int sub = (i->modrm >> 3) & 7;
        if (sub != 1 || i->mod == 3)
            x86_undefined(c);
        uint64_t addr = x86_ea(c, i);
        if (i->rex & 8) { /* cmpxchg16b */
            if (addr & 15)
                x86_fault(c, X86_STATUS_ACCESS_VIOLATION, 0, ~0ull);
            unsigned __int128 expected = ((unsigned __int128)c->r[X86_RDX] << 64) | c->r[X86_RAX];
            unsigned __int128 desired = ((unsigned __int128)c->r[X86_RCX] << 64) | c->r[X86_RBX];
            int ok = __atomic_compare_exchange_n((unsigned __int128 *)(uintptr_t)addr, &expected, desired, 0,
                                                 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            uint32_t f = x86_arith_flags(c) & ~X86_ZF;
            if (ok) {
                f |= X86_ZF;
            } else {
                c->r[X86_RAX] = (uint64_t)expected;
                c->r[X86_RDX] = (uint64_t)(expected >> 64);
            }
            c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        } else { /* cmpxchg8b */
            uint64_t expected = ((c->r[X86_RDX] & 0xFFFFFFFF) << 32) | (c->r[X86_RAX] & 0xFFFFFFFF);
            uint64_t desired = ((c->r[X86_RCX] & 0xFFFFFFFF) << 32) | (c->r[X86_RBX] & 0xFFFFFFFF);
            int ok = __atomic_compare_exchange_n((uint64_t *)(uintptr_t)addr, &expected, desired, 0, __ATOMIC_SEQ_CST,
                                                 __ATOMIC_SEQ_CST);
            uint32_t f = x86_arith_flags(c) & ~X86_ZF;
            if (ok) {
                f |= X86_ZF;
            } else {
                c->r[X86_RAX] = (uint32_t)expected;
                c->r[X86_RDX] = expected >> 32;
            }
            c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        }
        c->lf_op = X86_LF_NONE;
        return;
    }
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: { /* bswap */
        int reg = (op & 7) | ((i->rex & 1) << 3);
        if (size == 8)
            c->r[reg] = __builtin_bswap64(c->r[reg]);
        else if (size == 4)
            c->r[reg] = __builtin_bswap32((uint32_t)c->r[reg]);
        else
            c->r[reg] &= ~0xFFFFull; /* undefined, zero like recent processors */
        return;
    }
    case 0x77: /* emms */
        c->mmx_mode = 0;
        c->ftag = 0;
        return;
    default:
        break;
    }
    if ((op >= 0x10 && op <= 0x17) || (op >= 0x28 && op <= 0x2F) || (op >= 0x50 && op <= 0x7F) || op == 0xC2 ||
        (op >= 0xC4 && op <= 0xC6) || op >= 0xD0) {
        x86_exec_sse(c, i);
        return;
    }
    x86_undefined(c);
}

/* ------------------------------------------------------------ one byte map */

static void exec(X86Cpu *c, const X86Insn *i)
{
    c->insn_rip = c->rip;
    c->rip += i->len;
    if (!c->mode64)
        c->rip = (uint32_t)c->rip;

    if (i->map != 0) {
        if (i->map == 1)
            exec_0f(c, i);
        else
            x86_exec_sse(c, i);
        return;
    }

    uint8_t op = i->opcode;
    int size = i->opsize;
    switch (op) {
    /* add, or, adc, sbb, and, sub, xor, cmp */
    case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
        alu_rm(c, i, op >> 3, 1, read_greg(c, i, 1));
        return;
    case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31: case 0x39:
        alu_rm(c, i, op >> 3, size, read_greg(c, i, size));
        return;
    case 0x02: case 0x0A: case 0x12: case 0x1A: case 0x22: case 0x2A: case 0x32: case 0x3A: {
        uint64_t r = alu(c, op >> 3, 1, read_greg(c, i, 1), read_rm(c, i, 1));
        if ((op >> 3) != ALU_CMP)
            write_greg(c, i, 1, r);
        return;
    }
    case 0x03: case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: case 0x33: case 0x3B: {
        uint64_t r = alu(c, op >> 3, size, read_greg(c, i, size), read_rm(c, i, size));
        if ((op >> 3) != ALU_CMP)
            write_greg(c, i, size, r);
        return;
    }
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
        uint64_t r = alu(c, op >> 3, 1, c->r[X86_RAX], i->imm);
        if ((op >> 3) != ALU_CMP)
            set_reg(c, X86_RAX, 1, 1, r);
        return;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
        uint64_t r = alu(c, op >> 3, size, c->r[X86_RAX], i->imm);
        if ((op >> 3) != ALU_CMP)
            set_reg(c, X86_RAX, size, 1, r);
        return;
    }
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: /* inc, 32 bit */
        set_reg(c, op & 7, size, 1, inc_dec(c, size, get_reg(c, op & 7, size, 1), 0));
        return;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: /* dec, 32 bit */
        set_reg(c, op & 7, size, 1, inc_dec(c, size, get_reg(c, op & 7, size, 1), 1));
        return;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        push(c, size, c->r[reg]);
        return;
    }
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        uint64_t v = pop(c, size);
        set_reg(c, reg, size, 1, v);
        return;
    }
    case 0x60: { /* pushad */
        uint64_t sp = c->r[X86_RSP];
        for (int r = 0; r < 8; r++)
            push(c, size, r == X86_RSP ? sp : c->r[r]);
        return;
    }
    case 0x61: /* popad */
        for (int r = 7; r >= 0; r--) {
            uint64_t v = pop(c, size);
            if (r != X86_RSP)
                set_reg(c, r, size, 1, v);
        }
        return;
    case 0x63: /* movsxd */
        if (!c->mode64)
            x86_undefined(c);
        write_greg(c, i, size, size == 8 ? x86_sext(read_rm(c, i, 4), 4) : read_rm(c, i, size));
        return;
    case 0x68:
    case 0x6A:
        push(c, size, i->imm);
        return;
    case 0x69:
    case 0x6B:
        write_greg(c, i, size, imul_trunc(c, size, read_rm(c, i, size), i->imm));
        return;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        if (x86_cond(c, op & 15))
            c->rip = branch_target(c, c->rip + i->imm);
        return;
    case 0x80:
    case 0x82:
        alu_rm(c, i, (i->modrm >> 3) & 7, 1, i->imm);
        return;
    case 0x81:
    case 0x83:
        alu_rm(c, i, (i->modrm >> 3) & 7, size, i->imm);
        return;
    case 0x84:
        alu(c, ALU_AND, 1, read_rm(c, i, 1), read_greg(c, i, 1));
        return;
    case 0x85:
        alu(c, ALU_AND, size, read_rm(c, i, size), read_greg(c, i, size));
        return;
    case 0x86:
    case 0x87: { /* xchg */
        int sz = op == 0x86 ? 1 : size;
        uint64_t v = read_greg(c, i, sz);
        if (i->mod == 3) {
            uint64_t o = get_reg(c, i->rm, sz, i->rex);
            set_reg(c, i->rm, sz, i->rex, v);
            write_greg(c, i, sz, o);
        } else {
            write_greg(c, i, sz, xchg_mem(x86_ea(c, i), sz, v));
        }
        return;
    }
    case 0x88:
        write_rm(c, i, 1, read_greg(c, i, 1));
        return;
    case 0x89:
        write_rm(c, i, size, read_greg(c, i, size));
        return;
    case 0x8A:
        write_greg(c, i, 1, read_rm(c, i, 1));
        return;
    case 0x8B:
        write_greg(c, i, size, read_rm(c, i, size));
        return;
    case 0x8C: { /* mov rm, sreg */
        uint16_t s = selector(c, (i->modrm >> 3) & 7);
        if (i->mod == 3)
            set_reg(c, i->rm, size == 2 ? 2 : size, i->rex, s);
        else
            wr16(x86_ea(c, i), s);
        return;
    }
    case 0x8D: { /* lea */
        if (i->mod == 3)
            x86_undefined(c);
        uint8_t seg = i->seg;
        ((X86Insn *)i)->seg = 0;
        uint64_t a = x86_ea(c, i);
        ((X86Insn *)i)->seg = seg;
        write_greg(c, i, size, a);
        return;
    }
    case 0x8E: /* mov sreg, rm: the flat segments stay as they are */
        (void)read_rm(c, i, 2);
        return;
    case 0x8F: { /* pop rm */
        uint64_t v = pop(c, size);
        if (i->mod == 3) {
            set_reg(c, i->rm, size, i->rex, v);
        } else {
            /* the address uses the stack pointer after the pop. */
            wrn(x86_ea(c, i), size, v);
        }
        return;
    }
    case 0x90:
        if (!(i->rex & 1))
            return; /* nop and pause */
        /* fallthrough: xchg r8, rax */
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        uint64_t a = get_reg(c, X86_RAX, size, 1), b = get_reg(c, reg, size, 1);
        set_reg(c, X86_RAX, size, 1, b);
        set_reg(c, reg, size, 1, a);
        return;
    }
    case 0x98: /* cbw, cwde, cdqe */
        set_reg(c, X86_RAX, size, 1, x86_sext(c->r[X86_RAX], size / 2));
        return;
    case 0x99: /* cwd, cdq, cqo */
        set_reg(c, X86_RDX, size, 1, (c->r[X86_RAX] >> (size * 8 - 1)) & 1 ? ~0ull : 0);
        return;
    case 0x9B: /* fwait */
        return;
    case 0x9C: /* pushf */
        push(c, size, x86_get_flags(c) & 0x00FCFFFFu);
        return;
    case 0x9D: { /* popf */
        uint32_t v = (uint32_t)pop(c, size);
        uint32_t keep = size == 2 ? 0xFFFF0000u : 0;
        uint32_t cur = x86_get_flags(c);
        x86_set_flags(c, ((cur & keep) | (v & ~keep)) | X86_IF);
        return;
    }
    case 0x9E: { /* sahf */
        uint32_t f = x86_get_flags(c);
        f = (f & ~0xD5u) | (uint32_t)((c->r[X86_RAX] >> 8) & 0xD5);
        x86_set_flags(c, f);
        return;
    }
    case 0x9F: /* lahf */
        set_reg(c, 4, 1, 0, (x86_get_flags(c) & 0xD5) | 2);
        return;
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3: { /* mov moffs */
        int sz = (op & 1) ? size : 1;
        uint64_t addr = i->imm;
        if (i->addrsize == 4)
            addr = (uint32_t)addr;
        if (i->seg == 4)
            addr += c->fs_base;
        else if (i->seg == 5)
            addr += c->gs_base;
        if (op < 0xA2)
            set_reg(c, X86_RAX, sz, 1, rdn(addr, sz));
        else
            wrn(addr, sz, c->r[X86_RAX]);
        return;
    }
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        string_op(c, i);
        return;
    case 0xA8:
        alu(c, ALU_AND, 1, c->r[X86_RAX], i->imm);
        return;
    case 0xA9:
        alu(c, ALU_AND, size, c->r[X86_RAX], i->imm);
        return;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_reg(c, (op & 7) | ((i->rex & 1) << 3), 1, i->rex, i->imm);
        return;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        set_reg(c, (op & 7) | ((i->rex & 1) << 3), size, 1, i->imm);
        return;
    case 0xC0:
    case 0xC1:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3: { /* group 2 */
        int sz = (op & 1) ? size : 1;
        unsigned count = op < 0xD0 ? (unsigned)(i->imm & 0xFF) : op < 0xD2 ? 1 : (unsigned)(c->r[X86_RCX] & 0xFF);
        int sub = (i->modrm >> 3) & 7;
        if (i->mod == 3) {
            set_reg(c, i->rm, sz, i->rex, shift(c, sub, sz, get_reg(c, i->rm, sz, i->rex), count));
        } else {
            uint64_t addr = x86_ea(c, i);
            wrn(addr, sz, shift(c, sub, sz, rdn(addr, sz), count));
        }
        return;
    }
    case 0xC2: /* ret imm16 */
        c->rip = branch_target(c, pop(c, size));
        c->r[X86_RSP] = (c->r[X86_RSP] + i->imm) & stack_mask(c);
        return;
    case 0xC3:
        c->rip = branch_target(c, pop(c, size));
        return;
    case 0xC6:
        if ((i->modrm >> 3) & 7)
            x86_undefined(c);
        write_rm(c, i, 1, i->imm);
        return;
    case 0xC7:
        if ((i->modrm >> 3) & 7)
            x86_undefined(c);
        write_rm(c, i, size, i->imm);
        return;
    case 0xC8: { /* enter */
        uint64_t alloc = i->imm & 0xFFFF;
        unsigned level = (unsigned)(i->imm2 & 31);
        push(c, size, c->r[X86_RBP]);
        uint64_t frame = c->r[X86_RSP];
        if (level) {
            uint64_t bp = c->r[X86_RBP];
            for (unsigned k = 1; k < level; k++) {
                bp -= (uint64_t)size;
                push(c, size, rdn(bp & stack_mask(c), size));
            }
            push(c, size, frame);
        }
        set_reg(c, X86_RBP, size, 1, frame);
        c->r[X86_RSP] = (c->r[X86_RSP] - alloc) & stack_mask(c);
        return;
    }
    case 0xC9: /* leave */
        c->r[X86_RSP] = c->r[X86_RBP] & stack_mask(c);
        set_reg(c, X86_RBP, size, 1, pop(c, size));
        return;
    case 0xCC:
        x86_fault(c, X86_STATUS_BREAKPOINT, 0, 0);
    case 0xCD:
        interrupt(c, (uint8_t)i->imm);
        return;
    case 0xD7: { /* xlat */
        uint64_t a = c->r[X86_RBX] + (c->r[X86_RAX] & 0xFF);
        if (i->addrsize == 4)
            a = (uint32_t)a;
        if (i->seg == 4)
            a += c->fs_base;
        else if (i->seg == 5)
            a += c->gs_base;
        set_reg(c, X86_RAX, 1, 1, rd8(a));
        return;
    }
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        x86_exec_x87(c, i);
        return;
    case 0xE0: /* loopne */
    case 0xE1: /* loope */
    case 0xE2: { /* loop */
        uint64_t amask = i->addrsize == 4 ? 0xFFFFFFFFull : ~0ull;
        uint64_t n = (c->r[X86_RCX] - 1) & amask;
        c->r[X86_RCX] = amask == ~0ull ? n : n;
        int take = n != 0;
        if (op == 0xE0)
            take = take && !x86_cond(c, 4);
        else if (op == 0xE1)
            take = take && x86_cond(c, 4);
        if (take)
            c->rip = branch_target(c, c->rip + i->imm);
        return;
    }
    case 0xE3: { /* jrcxz */
        uint64_t amask = i->addrsize == 4 ? 0xFFFFFFFFull : ~0ull;
        if (!(c->r[X86_RCX] & amask))
            c->rip = branch_target(c, c->rip + i->imm);
        return;
    }
    case 0xE8: /* call rel */
        push(c, c->mode64 ? 8 : size, c->rip);
        c->rip = branch_target(c, c->rip + i->imm);
        return;
    case 0xE9:
    case 0xEB:
        c->rip = branch_target(c, c->rip + i->imm);
        return;
    case 0xF4: /* hlt */
    case 0xFA: /* cli */
    case 0xFB: /* sti */
        x86_fault(c, X86_STATUS_PRIVILEGED_INSTRUCTION, 0, 0);
    case 0xF5: { /* cmc */
        uint32_t f = x86_arith_flags(c) ^ X86_CF;
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        c->lf_op = X86_LF_NONE;
        return;
    }
    case 0xF8:
    case 0xF9: { /* clc, stc */
        uint32_t f = (x86_arith_flags(c) & ~X86_CF) | (op == 0xF9 ? X86_CF : 0);
        c->flags = (c->flags & ~X86_ARITH_FLAGS) | f;
        c->lf_op = X86_LF_NONE;
        return;
    }
    case 0xFC:
        c->flags &= ~X86_DF;
        return;
    case 0xFD:
        c->flags |= X86_DF;
        return;
    case 0xF6:
    case 0xF7: { /* group 3 */
        int sz = op == 0xF6 ? 1 : size;
        int sub = (i->modrm >> 3) & 7;
        switch (sub) {
        case 0:
        case 1:
            alu(c, ALU_AND, sz, read_rm(c, i, sz), i->imm);
            return;
        case 2: /* not */
            if (i->mod != 3 && i->lock) {
                uint64_t addr = x86_ea(c, i);
                for (;;) {
                    uint64_t a = rdn(addr, sz);
                    if (cas(addr, sz, a, ~a))
                        return;
                }
            }
            write_rm(c, i, sz, ~read_rm(c, i, sz));
            return;
        case 3: { /* neg */
            if (i->mod != 3 && i->lock) {
                uint64_t addr = x86_ea(c, i);
                for (;;) {
                    uint64_t a = rdn(addr, sz);
                    uint64_t r = alu(c, ALU_SUB, sz, 0, a);
                    if (cas(addr, sz, a, r))
                        return;
                }
            }
            write_rm(c, i, sz, alu(c, ALU_SUB, sz, 0, read_rm(c, i, sz)));
            return;
        }
        case 4:
        case 5:
            mul_one(c, i, sz, sub == 5);
            return;
        default:
            div_one(c, i, sz, sub == 7);
            return;
        }
    }
    case 0xFE:
    case 0xFF: {
        int sub = (i->modrm >> 3) & 7;
        if (op == 0xFE && sub > 1)
            x86_undefined(c);
        int sz = op == 0xFE ? 1 : size;
        switch (sub) {
        case 0:
        case 1:
            inc_dec_rm(c, i, sz, sub);
            return;
        case 2: { /* call rm */
            int bsz = c->mode64 ? 8 : size;
            uint64_t target = read_rm(c, i, bsz);
            push(c, bsz, c->rip);
            c->rip = branch_target(c, target);
            return;
        }
        case 4: /* jmp rm */
            c->rip = branch_target(c, read_rm(c, i, c->mode64 ? 8 : size));
            return;
        case 6: { /* push rm */
            int psz = c->mode64 ? (i->p66 ? 2 : 8) : size;
            push(c, psz, read_rm(c, i, psz));
            return;
        }
        default:
            x86_undefined(c);
        }
    }
    default:
        x86_undefined(c);
    }
}

/* ------------------------------------------------------------ block cache */

#define BLOCK_MAX 32
#define BLOCK_BUCKETS 16384

typedef struct X86Block {
    struct X86Block *next;
    uint64_t rip;
    uint32_t count;
    X86Insn insns[];
} X86Block;

typedef struct X86BlockCache {
    X86Block *buckets[BLOCK_BUCKETS];
    uint8_t *arena;
    size_t arena_used;
    size_t arena_size;
    struct X86BlockArena *arenas;
} X86BlockCache;

typedef struct X86BlockArena {
    struct X86BlockArena *next;
    size_t size;
} X86BlockArena;

#define ARENA_SIZE (4u << 20)

static void *cache_alloc(X86BlockCache *bc, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (!bc->arena || bc->arena_used + size > bc->arena_size) {
        X86BlockArena *a = x86_alloc(ARENA_SIZE);
        a->next = bc->arenas;
        a->size = ARENA_SIZE;
        bc->arenas = a;
        bc->arena = (uint8_t *)a;
        bc->arena_used = 64;
        bc->arena_size = ARENA_SIZE;
    }
    void *p = bc->arena + bc->arena_used;
    bc->arena_used += size;
    return p;
}

static int ends_block(const X86Insn *i)
{
    if (i->map == 0) {
        uint8_t op = i->opcode;
        if ((op >= 0x70 && op <= 0x7F) || op == 0xC2 || op == 0xC3 || op == 0xCC || op == 0xCD || op == 0xE8 ||
            op == 0xE9 || op == 0xEB || (op >= 0xE0 && op <= 0xE3) || op == 0xCA || op == 0xCB || op == 0xCF)
            return 1;
        if (op == 0xFF) {
            int sub = (i->modrm >> 3) & 7;
            return sub >= 2 && sub <= 5;
        }
        return 0;
    }
    if (i->map == 1) {
        uint8_t op = i->opcode;
        return (op >= 0x80 && op <= 0x8F) || op == 0x05 || op == 0x34 || op == 0x0B || op == 0xA2;
    }
    return 0;
}

static X86Block *translate(X86Cpu *c, X86BlockCache *bc, uint64_t rip)
{
    X86Insn buf[BLOCK_MAX];
    uint32_t n = 0;
    uint64_t pc = rip;
    c->insn_rip = rip;
    while (n < BLOCK_MAX && (n == 0 || pc != c->exit_rip)) {
        c->insn_rip = pc;
        if (!x86_decode(&buf[n], (const uint8_t *)(uintptr_t)pc, pc, (int)c->mode64)) {
            if (n == 0) {
                c->rip = pc;
                x86_undefined(c);
            }
            break;
        }
        pc += buf[n].len;
        if (ends_block(&buf[n++]))
            break;
    }
    X86Block *b = cache_alloc(bc, sizeof(X86Block) + n * sizeof(X86Insn));
    b->rip = rip;
    b->count = n;
    __builtin_memcpy(b->insns, buf, n * sizeof(X86Insn));
    uint32_t h = (uint32_t)((rip ^ (rip >> 14)) & (BLOCK_BUCKETS - 1));
    b->next = bc->buckets[h];
    bc->buckets[h] = b;
    return b;
}

void *x86_new_blocks(void)
{
    X86BlockCache *bc = x86_alloc(sizeof(X86BlockCache));
    __builtin_memset(bc, 0, sizeof(*bc));
    return bc;
}

static X86BlockCache *block_cache(X86Cpu *c)
{
    if (!c->blocks)
        c->blocks = x86_new_blocks();
    return c->blocks;
}

void x86_free_blocks(X86Cpu *c)
{
    X86BlockCache *bc = c->blocks;
    if (!bc)
        return;
    X86BlockArena *a = bc->arenas;
    while (a) {
        X86BlockArena *next = a->next;
        x86_free(a, a->size);
        a = next;
    }
    x86_free(bc, sizeof(*bc));
    c->blocks = NULL;
}

void x86_flush_blocks(X86Cpu *c)
{
    X86BlockCache *bc = c->blocks;
    if (!bc)
        return;
    X86BlockArena *a = bc->arenas;
    while (a) {
        X86BlockArena *next = a->next;
        x86_free(a, a->size);
        a = next;
    }
    __builtin_memset(bc, 0, sizeof(*bc));
}

static inline X86Block *lookup(X86Cpu *c, uint64_t rip)
{
    X86BlockCache *bc = block_cache(c);
    uint32_t h = (uint32_t)((rip ^ (rip >> 14)) & (BLOCK_BUCKETS - 1));
    for (X86Block *b = bc->buckets[h]; b; b = b->next) {
        if (b->rip == rip)
            return b;
    }
    return translate(c, bc, rip);
}

void x86_run(X86Cpu *c)
{
    while (c->rip != c->exit_rip) {
        if (c->code_epoch && *c->code_epoch != c->blocks_epoch) {
            c->blocks_epoch = *c->code_epoch;
            x86_flush_blocks(c);
        }
        if (c->trace)
            c->trace[(*c->trace_pos)++ % X86_TRACE_SIZE] = c->rip;
        X86Block *b = lookup(c, c->rip);
        const X86Insn *i = b->insns, *end = i + b->count;
        uint64_t expected = c->rip;
        for (; i < end; i++) {
            if (c->rip != expected)
                break;
            expected += i->len;
            exec(c, i);
        }
        c->steps += (uint64_t)(i - b->insns);
    }
}

void x86_step(X86Cpu *c)
{
    X86Insn insn;
    c->insn_rip = c->rip;
    if (!x86_decode(&insn, (const uint8_t *)(uintptr_t)c->rip, c->rip, (int)c->mode64))
        x86_undefined(c);
    exec(c, &insn);
}

void x86_init_cpu(X86Cpu *c, const X86Env *env, int mode64)
{
    __builtin_memset(c, 0, sizeof(*c));
    c->env = env;
    c->mode64 = (uint32_t)mode64;
    c->flags = 0x202;
    c->mxcsr = 0x1F80;
    c->exit_rip = ~0ull;
    x86_fpu_init(c);
}
