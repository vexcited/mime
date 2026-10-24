/*
 * x86 and x64 instruction decoder. it only finds an instruction's fields and
 * length, the interpreter gives each opcode its meaning.
 */
#include "x86.h"



/* opcode attributes. */
#define M 0x001    /* modrm */
#define B 0x002    /* imm8 */
#define Z 0x004    /* imm16 or imm32 by operand size */
#define V 0x008    /* imm16, imm32 or imm64 by operand size */
#define W 0x010    /* imm16 */
#define E 0x020    /* imm16 then imm8, enter */
#define O 0x040    /* memory offset of the address size, mov moffs */
#define I 0x080    /* invalid in 64 bit mode */
#define D 0x100    /* 64 bit default operand size in 64 bit mode */
#define X 0x200    /* undefined */
#define P 0x400    /* far pointer, offset then selector */

static constexpr uint16_t g_one[256] = {
    /* 00 */ M, M, M, M, B, Z, I, I, M, M, M, M, B, Z, I, 0,
    /* 10 */ M, M, M, M, B, Z, I, I, M, M, M, M, B, Z, I, I,
    /* 20 */ M, M, M, M, B, Z, 0, I, M, M, M, M, B, Z, 0, I,
    /* 30 */ M, M, M, M, B, Z, 0, I, M, M, M, M, B, Z, 0, I,
    /* 40 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 50 */ D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
    /* 60 */ I, I, M | I, M, 0, 0, 0, 0, Z | D, M | Z, B | D, M | B, X, X, X, X,
    /* 70 */ B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D, B | D,
    /* 80 */ M | B, M | Z, M | B | I, M | B, M, M, M, M, M, M, M, M, M, M, M, M | D,
    /* 90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, P | I, 0, D, D, 0, 0,
    /* a0 */ O, O, O, O, 0, 0, 0, 0, B, Z, 0, 0, 0, 0, 0, 0,
    /* b0 */ B, B, B, B, B, B, B, B, V, V, V, V, V, V, V, V,
    /* c0 */ M | B, M | B, W | D, D, M | I, M | I, M | B, M | Z, E | D, D, W, 0, 0, B, I, 0,
    /* d0 */ M, M, M, M, B | I, B | I, I, 0, M, M, M, M, M, M, M, M,
    /* e0 */ B | D, B | D, B | D, B | D, X, X, X, X, Z | D, Z | D, P | I, B | D, X, X, X, X,
    /* f0 */ 0, 0, 0, 0, 0, 0, M, M, 0, 0, 0, 0, 0, 0, M, M,
};

static constexpr uint16_t g_two[256] = {
    /* 00 */ M, M, M, M, X, 0, X, X, X, X, X, 0, X, M, X, X,
    /* 10 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* 20 */ X, X, X, X, X, X, X, X, M, M, M, M, M, M, M, M,
    /* 30 */ 0, 0, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    /* 40 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* 50 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* 60 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* 70 */ M | B, M | B, M | B, M | B, M, M, M, 0, X, X, X, X, M, M, M, M,
    /* 80 */ Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D, Z | D,
    /* 90 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* a0 */ D, D, 0, M, M | B, M, X, X, D, D, X, M, M | B, M, M, M,
    /* b0 */ M, M, X, M, X, X, M, M, M, M, M | B, M, M, M, M, M,
    /* c0 */ M, M, M | B, M, M | B, M | B, M | B, M, 0, 0, 0, 0, 0, 0, 0, 0,
    /* d0 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* e0 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, M,
    /* f0 */ M, M, M, M, M, M, M, M, M, M, M, M, M, M, M, X,
};

static inline uint64_t read_le(const uint8_t *p, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static inline uint64_t sign_extend(uint64_t v, int bytes)
{
    int shift = 64 - 8 * bytes;
    return (uint64_t)((int64_t)(v << shift) >> shift);
}

int x86_decode(X86Insn *insn, const uint8_t *code, uint64_t address, int mode64)
{
    __builtin_memset(insn, 0, sizeof(*insn));
    insn->base = -1;
    insn->index = -1;
    const uint8_t *p = code;
    int rex = 0, opsize_prefix = 0, addr_prefix = 0;

    /* prefixes. a rex prefix only counts right before the opcode. */
    for (;;) {
        if (p - code >= 14)
            return 0;
        uint8_t c = *p;
        if (c == 0xF0) {
            insn->lock = 1;
        } else if (c == 0xF2 || c == 0xF3) {
            insn->rep = c;
        } else if (c == 0x66) {
            opsize_prefix = 1;
        } else if (c == 0x67) {
            addr_prefix = 1;
        } else if (c == 0x64) {
            insn->seg = 4;
        } else if (c == 0x65) {
            insn->seg = 5;
        } else if (c == 0x26 || c == 0x2E || c == 0x36 || c == 0x3E) {
            if (!mode64)
                insn->seg = 0;
        } else if (mode64 && (c & 0xF0) == 0x40) {
            rex = c;
            p++;
            if ((*p & 0xF0) == 0x40)
                continue; /* a later rex replaces this one. */
            uint8_t n = *p;
            if (n == 0xF0 || n == 0xF2 || n == 0xF3 || n == 0x66 || n == 0x67 || n == 0x64 || n == 0x65 ||
                n == 0x26 || n == 0x2E || n == 0x36 || n == 0x3E) {
                rex = 0; /* a prefix after rex cancels it. */
                continue;
            }
            break;
        } else {
            break;
        }
        p++;
    }
    insn->rex = (uint8_t)rex;
    insn->p66 = (uint8_t)opsize_prefix;
    insn->mandatory = insn->rep ? insn->rep : (opsize_prefix ? 0x66 : 0);

    uint16_t attr;
    uint8_t op = *p++;
    if (op == 0x0F) {
        op = *p++;
        if (op == 0x38) {
            insn->map = 2;
            op = *p++;
            attr = M;
        } else if (op == 0x3A) {
            insn->map = 3;
            op = *p++;
            attr = M | B;
        } else {
            insn->map = 1;
            attr = g_two[op];
        }
    } else {
        insn->map = 0;
        attr = g_one[op];
        /* vex encodings are avx, which the processor does not report. */
        if (mode64 && (op == 0xC4 || op == 0xC5))
            return 0;
    }
    insn->opcode = op;
    if (attr & X)
        return 0;
    if (mode64 && (attr & I))
        return 0;

    if (mode64) {
        if (rex & 8)
            insn->opsize = 8;
        else if (opsize_prefix)
            insn->opsize = 2;
        else
            insn->opsize = (attr & D) ? 8 : 4;
        if ((attr & D) && !opsize_prefix)
            insn->opsize = 8;
        insn->addrsize = addr_prefix ? 4 : 8;
    } else {
        insn->opsize = opsize_prefix ? 2 : 4;
        /* 16 bit addressing is not supported. */
        if (addr_prefix)
            return 0;
        insn->addrsize = 4;
    }

    if (attr & M) {
        uint8_t m = *p++;
        insn->has_modrm = 1;
        insn->modrm = m;
        insn->mod = m >> 6;
        insn->reg = (uint8_t)(((m >> 3) & 7) | ((rex & 4) << 1));
        uint8_t rm3 = m & 7;
        insn->rm = (uint8_t)(rm3 | ((rex & 1) << 3));
        if (insn->mod != 3) {
            if (rm3 == 4) {
                uint8_t sib = *p++;
                uint8_t base3 = sib & 7;
                int index = ((sib >> 3) & 7) | ((rex & 2) << 2);
                insn->scale = sib >> 6;
                insn->index = (int8_t)(index == 4 ? -1 : index);
                if (base3 == 5 && insn->mod == 0) {
                    insn->base = -1;
                    insn->disp = (int32_t)read_le(p, 4);
                    p += 4;
                } else {
                    insn->base = (int8_t)(base3 | ((rex & 1) << 3));
                }
            } else if (rm3 == 5 && insn->mod == 0) {
                insn->base = mode64 ? -2 : -1;
                insn->disp = (int32_t)read_le(p, 4);
                p += 4;
            } else {
                insn->base = (int8_t)insn->rm;
            }
            if (insn->mod == 1) {
                insn->disp = (int8_t)*p++;
            } else if (insn->mod == 2) {
                insn->disp = (int32_t)read_le(p, 4);
                p += 4;
            }
        }
    }

    /* group 3 test carries an immediate the opcode table cannot express. */
    if (insn->map == 0 && (op == 0xF6 || op == 0xF7) && ((insn->modrm >> 3) & 7) < 2)
        attr |= op == 0xF6 ? B : Z;

    if (attr & B) {
        insn->imm = sign_extend(*p, 1);
        p++;
    } else if (attr & Z) {
        int n = insn->opsize == 2 ? 2 : 4;
        insn->imm = sign_extend(read_le(p, n), n);
        p += n;
    } else if (attr & V) {
        int n = insn->opsize;
        insn->imm = read_le(p, n);
        p += n;
    } else if (attr & W) {
        insn->imm = read_le(p, 2);
        p += 2;
    } else if (attr & E) {
        insn->imm = read_le(p, 2);
        insn->imm2 = p[2];
        p += 3;
    } else if (attr & O) {
        insn->imm = read_le(p, insn->addrsize);
        p += insn->addrsize;
    } else if (attr & P) {
        int n = insn->opsize == 2 ? 2 : 4;
        insn->imm = read_le(p, n);
        insn->imm2 = read_le(p + n, 2);
        p += n + 2;
    }

    insn->len = (uint8_t)(p - code);
    if (insn->len > 15)
        return 0;
    insn->valid = 1;
    (void)address;
    return 1;
}
