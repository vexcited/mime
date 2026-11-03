/*
 * the length of an instruction of the amd64 processor and where its address operand is, as the patch compressor reads
 * code: the tables say for each opcode what follows it, and the rest is the prefixes, the register byte and the
 * displacement. an operand that is relative to the instruction pointer is told from the others by its kind.
 */
#include "internal.h"

/*
 * what follows the opcode: 0 nothing, 1 an opcode that does not exist, 2 a register byte, 3 an address as wide as
 * the mode, 4 a relative byte, 5 a relative word of four bytes.
 */
static const uint8_t g_class[256] = {
    2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1,
    2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 2, 1, 1, 1, 1, 0, 2, 0, 2, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1,
    3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 1, 0, 2, 2, 2, 2, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2,
    4, 4, 4, 4, 0, 0, 0, 0, 5, 5, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2,
};

static const uint8_t g_class_0f[256] = {
    2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 1, 0, 1, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 1, 1, 1, 1, 1, 2, 2,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 0, 2, 2, 2, 1, 1, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
};


/* the immediate operand: 0 none, 1 a byte, 2 two bytes, 3 three, 4 a word of the operand size up to eight bytes, 5 two or four bytes,
 * 6 a byte for the first two operations of the group of the register byte, 7 two or four bytes for them. */
static const uint8_t g_immediate[256] = {
    0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0,
    0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4,
    1, 1, 2, 0, 0, 0, 1, 5, 3, 0, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint8_t g_immediate_0f[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

MD_INSN MdAmd64Disassemble(const uint8_t *p, size_t remaining)
{
    MD_INSN r = {0, 0xFF, 2};
    unsigned limit = remaining < 15 ? (unsigned)remaining : 15;
    bool operand16 = false, address32 = false, wide = false;
    unsigned len = 0;
    uint8_t op;
#define NEED(n) \
    do { \
        if (limit - len < (unsigned)(n)) \
            goto invalid; \
    } while (0)
    for (;;) {
        NEED(1);
        op = p[len++];
        if (op == 0xF2 || op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0x64 || op == 0x65 || op == 0xF0 || op == 0xF3)
            continue;
        if (op == 0x66) {
            operand16 = true;
            continue;
        }
        if (op == 0x67) {
            address32 = true;
            continue;
        }
        break;
    }
    if ((op & 0xF0) == 0x40) {
        NEED(1);
        wide = (op & 8) != 0;
        unsigned after_rex = len;
        op = p[len++];
        if ((op & 0xF0) == 0x40) {
            len = after_rex;
            goto invalid;
        }
    }
    unsigned class, immediate;
    if (op == 0x0F) {
        NEED(1);
        uint8_t second = p[len++];
        class = g_class_0f[second];
        immediate = g_immediate_0f[second];
    } else {
        class = g_class[op];
        immediate = g_immediate[op];
    }
    bool group = false;
    if (class == 2) {
        NEED(1);
        uint8_t modrm = p[len++];
        unsigned mod = modrm >> 6, rm = modrm & 7, reg = modrm & 0x38;
        group = reg == 0 || reg == 8;
        if (mod != 3) {
            if (mod == 0 && rm == 5) {
                NEED(4);
                r.Kind = 5;
                r.Offset = (uint8_t)len;
                len += 4;
            } else if (rm == 4) {
                NEED(1);
                uint8_t sib = p[len++];
                if (mod == 1) {
                    NEED(1);
                    r.Offset = (uint8_t)len;
                    len += 1;
                    r.Kind = 7;
                } else if (mod == 2 || (mod == 0 && (sib & 7) == 5)) {
                    NEED(4);
                    r.Offset = (uint8_t)len;
                    len += 4;
                    r.Kind = 9;
                }
            } else if (mod == 1) {
                NEED(1);
                r.Kind = 7;
                r.Offset = (uint8_t)len;
                len += 1;
            } else if (mod == 2) {
                NEED(4);
                r.Kind = 9;
                r.Offset = (uint8_t)len;
                len += 4;
            }
        }
    } else if (class == 1) {
        goto invalid;
    } else if (class == 3) {
        unsigned width = address32 ? 4 : 8;
        NEED(width);
        r.Offset = (uint8_t)len;
        r.Kind = address32 ? 9 : 10;
        len += width;
    } else if (class == 4) {
        NEED(1);
        r.Kind = 3;
        r.Offset = (uint8_t)len;
        len += 1;
    } else if (class == 5) {
        NEED(4);
        r.Kind = 5;
        r.Offset = (uint8_t)len;
        len += 4;
    }
    switch (immediate) {
    case 1:
        NEED(1);
        len += 1;
        break;
    case 2:
        NEED(2);
        len += 2;
        break;
    case 3:
        NEED(3);
        len += 3;
        break;
    case 4: {
        unsigned width = wide ? 8 : operand16 ? 2 : 4;
        NEED(width);
        len += width;
        break;
    }
    case 5: {
        unsigned width = !wide && operand16 ? 2 : 4;
        NEED(width);
        len += width;
        break;
    }
    case 6:
        if (group) {
            NEED(1);
            len += 1;
        }
        break;
    case 7:
        if (group) {
            unsigned width = wide || !operand16 ? 4 : 2;
            NEED(width);
            len += width;
        }
        break;
    default:
        break;
    }
    r.Length = (uint8_t)len;
    return r;
invalid:
    r.Length = (uint8_t)len;
    r.Kind = 1;
    return r;
#undef NEED
}
