/*
 * the translator from x64 blocks to arm64 code.
 *
 * a translated block is a function taking the cpu, with the x64 state kept in
 * the cpu structure: x19 holds the cpu and every instruction loads and stores
 * the registers it uses. instructions the translator does not cover call the
 * interpreter for that one instruction, so any block can be translated. the
 * function returns the index of the first instruction left to interpret, with
 * rip at its address: the count of the block when it ran to its end, or the
 * index of a final instruction that may run nested emulation (a system call),
 * which must not return into code a nested emulation may free.
 *
 * flags stay lazy as in the interpreter: an arithmetic instruction records its
 * operands when a later instruction may read them. a liveness pass over the
 * block drops the records nothing reads, and a conditional branch right after
 * a compare tests the arm64 flags of the compare itself.
 */
#include "internal.h"
#include "jit.h"

#include <stddef.h>

/* registers: x19 the cpu, x9 addresses, x10 to x15, x5 and x6 values; x14 is free only between uses of ea. */
enum { CPU = 19, ADDR = 9, T0 = 10, T1 = 11, T2 = 12, T3 = 13, T4 = 14, T5 = 15, S0 = 5, S1 = 6, XZR = 31 };
enum { EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, VS = 6, VC = 7, HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13 };

typedef struct Emit {
    uint32_t *code, *end, *p;
    int overflow;
} Emit;

static void put(Emit *e, uint32_t word)
{
    if (e->p < e->end)
        *e->p++ = word;
    else
        e->overflow = 1;
}

static uint32_t OFF_R(int reg) { return (uint32_t)(offsetof(X86Cpu, r) + (uint32_t)reg * 8); }

/* ------------------------------------------------------------ arm64 encodings */

/* movz then movk for the nonzero halfwords, or movn then movk for the ones not all set. */
static void mov_imm(Emit *e, int rd, uint64_t v)
{
    int ones = 0;
    for (int hw = 0; hw < 4; hw++)
        ones += ((v >> (hw * 16)) & 0xFFFF) == 0xFFFF;
    int inverted = ones > 2 - (v == 0);
    uint64_t skip = inverted ? 0xFFFF : 0;
    int first = 1;
    for (int hw = 0; hw < 4; hw++) {
        uint32_t part = (uint32_t)(v >> (hw * 16)) & 0xFFFF;
        if (part == skip && !(first && hw == 3))
            continue;
        if (first)
            put(e, (inverted ? 0x92800000u : 0xD2800000u) | (uint32_t)hw << 21 | (inverted ? ~part & 0xFFFF : part) << 5 |
                       (uint32_t)rd);
        else
            put(e, 0xF2800000u | (uint32_t)hw << 21 | part << 5 | (uint32_t)rd);
        first = 0;
    }
}

/* register load and store at a scaled offset of base. */
static void ldr(Emit *e, int size, int rt, int rn, uint32_t off)
{
    static const uint32_t ops[] = {0, 0x39400000u, 0x79400000u, 0, 0xB9400000u, 0, 0, 0, 0xF9400000u};
    put(e, ops[size] | (off / (uint32_t)size) << 10 | (uint32_t)rn << 5 | (uint32_t)rt);
}

static void str(Emit *e, int size, int rt, int rn, uint32_t off)
{
    static const uint32_t ops[] = {0, 0x39000000u, 0x79000000u, 0, 0xB9000000u, 0, 0, 0, 0xF9000000u};
    put(e, ops[size] | (off / (uint32_t)size) << 10 | (uint32_t)rn << 5 | (uint32_t)rt);
}

static uint32_t sf(int size) { return size == 8 ? 0x80000000u : 0; }

/* the register form of an arithmetic or logic instruction; op is the 32 bit encoding. */
static void rrr(Emit *e, uint32_t op, int size, int rd, int rn, int rm)
{
    put(e, op | sf(size) | (uint32_t)rm << 16 | (uint32_t)rn << 5 | (uint32_t)rd);
}

#define OP_ADD 0x0B000000u
#define OP_ADDS 0x2B000000u
#define OP_SUB 0x4B000000u
#define OP_SUBS 0x6B000000u
#define OP_AND 0x0A000000u
#define OP_ANDS 0x6A000000u
#define OP_ORR 0x2A000000u
#define OP_EOR 0x4A000000u

static void mov_reg(Emit *e, int size, int rd, int rm) { rrr(e, OP_ORR, size, rd, XZR, rm); }

/* rd = rn + (rm << shift), 64 bit. */
static void add_shifted(Emit *e, int rd, int rn, int rm, int shift)
{
    put(e, 0x8B000000u | (uint32_t)rm << 16 | (uint32_t)shift << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
}

static void ubfm(Emit *e, int size, int rd, int rn, int immr, int imms)
{
    put(e, (size == 8 ? 0xD3400000u : 0x53000000u) | (uint32_t)immr << 16 | (uint32_t)imms << 10 | (uint32_t)rn << 5 |
               (uint32_t)rd);
}

static void sbfm(Emit *e, int size, int rd, int rn, int immr, int imms)
{
    put(e, (size == 8 ? 0x93400000u : 0x13000000u) | (uint32_t)immr << 16 | (uint32_t)imms << 10 | (uint32_t)rn << 5 |
               (uint32_t)rd);
}

static void blr(Emit *e, int rn) { put(e, 0xD63F0000u | (uint32_t)rn << 5); }

static void call(Emit *e, const void *fn)
{
    mov_imm(e, 16, (uint64_t)(uintptr_t)fn);
    blr(e, 16);
}

/* a branch to patch once its target is known. */
static uint32_t *b_cond(Emit *e, int cond)
{
    uint32_t *at = e->p;
    put(e, 0x54000000u | (uint32_t)cond);
    return at;
}

static uint32_t *cbnz_w(Emit *e, int rt)
{
    uint32_t *at = e->p;
    put(e, 0x35000000u | (uint32_t)rt);
    return at;
}

static uint32_t *b_always(Emit *e)
{
    uint32_t *at = e->p;
    put(e, 0x14000000u);
    return at;
}

static void patch26(Emit *e, uint32_t *at)
{
    if (e->overflow || !at)
        return;
    int64_t d = e->p - at;
    *at |= (uint32_t)d & 0x3FFFFFF;
}

static void patch19(Emit *e, uint32_t *at)
{
    if (e->overflow || !at)
        return;
    int64_t d = e->p - at;
    *at |= ((uint32_t)d & 0x7FFFF) << 5;
}

/* ------------------------------------------------------------ block frame */

static void prologue(Emit *e)
{
    put(e, 0xA9BF7BF3u); /* stp x19, x30, [sp, #-16]! */
    mov_reg(e, 8, CPU, 0);
}

/* sets rip and returns how many instructions ran. */
static void exit_to(Emit *e, int rip_reg, uint64_t rip, uint32_t index)
{
    if (rip_reg >= 0)
        str(e, 8, rip_reg, CPU, offsetof(X86Cpu, rip));
    else {
        mov_imm(e, T4, rip);
        str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    }
    mov_imm(e, 0, index);
    put(e, 0xA8C17BF3u); /* ldp x19, x30, [sp], #16 */
    put(e, 0xD65F03C0u); /* ret */
}


/* ------------------------------------------------------------ translation state */

typedef struct Ctx {
    Emit *e;
    X86Cpu *cpu;
    uint64_t addr, next;
    uint32_t index, count;
    int flags_live; /* a later instruction may read the flags this one sets */
    int nzcv_alu;   /* the instruction kind whose arm64 flags are current, -1 none */
    int keep;       /* set by an emitter that left the arm64 and x86 flags alone */
    int nzcv_set;   /* set by an emitter that left arm64 flags describing its x86 ones */
    const X86Insn *insn;
} Ctx;

static void emit_fallback(Ctx *x, const X86Insn *i);

static void set_nzcv(Ctx *x, int kind)
{
    x->nzcv_alu = kind;
    x->nzcv_set = 1;
}

/* ------------------------------------------------------------ operands */

/* the effective address of the memory operand into ADDR. */
static void ea(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    if (i->base == -2) {
        mov_imm(e, ADDR, x->next + (uint64_t)(int64_t)i->disp);
    } else {
        if (i->base >= 0)
            ldr(e, 8, ADDR, CPU, OFF_R(i->base));
        else
            mov_imm(e, ADDR, 0);
        if (i->disp) {
            mov_imm(e, T4, (uint64_t)(int64_t)i->disp);
            add_shifted(e, ADDR, ADDR, T4, 0);
        }
    }
    if (i->index >= 0) {
        ldr(e, 8, T4, CPU, OFF_R(i->index));
        add_shifted(e, ADDR, ADDR, T4, i->scale);
    }
    if (i->addrsize == 4)
        mov_reg(e, 4, ADDR, ADDR);
    if (i->seg == 4 || i->seg == 5) {
        ldr(e, 8, T4, CPU, i->seg == 4 ? offsetof(X86Cpu, fs_base) : offsetof(X86Cpu, gs_base));
        add_shifted(e, ADDR, ADDR, T4, 0);
    }
}

/* ah, ch, dh and bh: the byte registers 4 to 7 without a rex prefix. */
static int high_byte(int size, int reg, int rex) { return size == 1 && !rex && reg >= 4 && reg < 8; }

/* loads a register zero extended from its size. */
static void load_reg(Emit *e, int size, int rt, int reg, int rex)
{
    if (high_byte(size, reg, rex)) {
        ldr(e, 8, rt, CPU, OFF_R(reg - 4));
        ubfm(e, 8, rt, rt, 8, 15);
    } else {
        ldr(e, size, rt, CPU, OFF_R(reg));
    }
}

/* writes a register as x86 does: 32 bit results zero extend, which rt already is, narrower ones merge. */
static void store_reg(Emit *e, int size, int rt, int reg, int rex)
{
    if (high_byte(size, reg, rex)) {
        ldr(e, 8, T5, CPU, OFF_R(reg - 4));
        put(e, 0xB3780000u | 7u << 10 | (uint32_t)rt << 5 | T5); /* bfi t5, rt, #8, #8 */
        str(e, 8, T5, CPU, OFF_R(reg - 4));
    } else {
        str(e, size < 4 ? size : 8, rt, CPU, OFF_R(reg));
    }
}

/* loads the modrm operand; the memory form leaves its address in ADDR. */
static void load_rm(Ctx *x, const X86Insn *i, int size, int rt)
{
    if (i->mod == 3) {
        load_reg(x->e, size, rt, i->rm, i->rex);
    } else {
        ea(x, i);
        ldr(x->e, size, rt, ADDR, 0);
    }
}

/* stores to the modrm operand; with reuse, ADDR still holds the address of a previous load_rm. */
static void store_rm(Ctx *x, const X86Insn *i, int size, int rt, int reuse)
{
    if (i->mod == 3) {
        store_reg(x->e, size, rt, i->rm, i->rex);
    } else {
        if (!reuse)
            ea(x, i);
        str(x->e, size, rt, ADDR, 0);
    }
}

static uint64_t size_mask(int size) { return size == 8 ? ~0ull : (1ull << (size * 8)) - 1; }

static int log2_size(int size) { return size == 8 ? 3 : size == 4 ? 2 : size == 2 ? 1 : 0; }

/* zero extends the low size bytes of a register in place. */
static void narrow(Emit *e, int size, int r)
{
    if (size < 4)
        ubfm(e, 4, r, r, 0, size * 8 - 1);
}

/* ------------------------------------------------------------ instruction classes */

enum {
    ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP,
    ALU_TEST, ALU_UCOMI, ALU_INCDEC,
};

/* the alu operation of an instruction the translator emits, or -1. */
static int alu_op(const X86Insn *i)
{
    if (i->map != 0 || i->lock)
        return -1;
    uint8_t op = i->opcode;
    int kind = -1;
    if (op < 0x40 && (op & 7) <= 5)
        kind = op >> 3;
    else if (op == 0x80 || op == 0x81 || op == 0x83)
        kind = (i->modrm >> 3) & 7;
    else if (op == 0x84 || op == 0x85 || op == 0xA8 || op == 0xA9 ||
             ((op == 0xF6 || op == 0xF7) && ((i->modrm >> 3) & 7) < 2))
        kind = ALU_TEST;
    return kind == ALU_ADC || kind == ALU_SBB ? -1 : kind;
}

/* the operand size of an alu instruction: the even one byte opcodes work on bytes. */
static int alu_size(const X86Insn *i)
{
    uint8_t op = i->opcode;
    if (op < 0x40)
        return (op & 1) ? i->opsize : 1;
    return op == 0x80 || op == 0x84 || op == 0xA8 || op == 0xF6 ? 1 : i->opsize;
}

static int is_jcc(const X86Insn *i)
{
    return (i->map == 0 && i->opcode >= 0x70 && i->opcode <= 0x7F) ||
           (i->map == 1 && i->opcode >= 0x80 && i->opcode <= 0x8F);
}

/* instructions setting every arithmetic flag without reading any. */
static int writes_flags(const X86Insn *i)
{
    if (alu_op(i) >= 0)
        return 1;
    uint8_t op = i->opcode;
    if (i->map == 0)
        return op == 0x69 || op == 0x6B || ((op == 0xF6 || op == 0xF7) && ((i->modrm >> 3) & 7) == 3);
    if (i->map != 1)
        return 0;
    if (op == 0xAF)
        return !i->mandatory;
    return (op == 0x2E || op == 0x2F) && (!i->mandatory || i->mandatory == 0x66);
}

/* instructions known to leave the flags alone and not to read them. */
static int ignores_flags(const X86Insn *i)
{
    uint8_t op = i->opcode;
    if (i->map == 0)
        return (op >= 0x88 && op <= 0x8B) || op == 0x8D || op == 0xC6 || op == 0xC7 || (op >= 0xB0 && op <= 0xBF) ||
               (op >= 0x50 && op <= 0x5F) || op == 0x68 || op == 0x6A || op == 0xE8 || op == 0xE9 || op == 0xEB ||
               op == 0xC3 || op == 0xC2 || op == 0x90 || op == 0x63 || op == 0x98 || op == 0x99 ||
               (op >= 0x86 && op <= 0x87) || (op >= 0x91 && op <= 0x97) ||
               (op == 0xFF && ((i->modrm >> 3) & 7) >= 2) || ((op == 0xF6 || op == 0xF7) && ((i->modrm >> 3) & 7) == 2);
    if (i->map == 1)
        return op == 0x1F || op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF || (op >= 0x10 && op <= 0x17) ||
               (op >= 0x28 && op <= 0x2D) || (op >= 0x50 && op <= 0x7F) || op == 0xC2 || op == 0xC6 || op >= 0xD0;
    return 1; /* the 0f 38 and 0f 3a maps */
}

/* reads flags as a condition: jcc, setcc, cmovcc. */
static int reads_condition(const X86Insn *i)
{
    return is_jcc(i) || (i->map == 1 && ((i->opcode >= 0x90 && i->opcode <= 0x9F) || (i->opcode >= 0x40 && i->opcode <= 0x4F)));
}

/* the instructions after which the block must return before running them, see the top. */
static int nests(const X86Insn *i)
{
    return (i->map == 1 && (i->opcode == 0x05 || i->opcode == 0x34)) || (i->map == 0 && i->opcode == 0xCD);
}

/*
 * whether the code at addr sets the flags before anything reads them. only
 * the pages of the block are read, which are known to be mapped.
 */
static int flags_dead_at(X86Cpu *cpu, uint64_t addr, uint64_t lo, uint64_t hi)
{
    for (int n = 0; n < 8; n++) {
        /* the environment reads them where the emulation returns. */
        if (addr < lo || addr + 15 > hi || addr == cpu->exit_rip)
            return 0;
        X86Insn i;
        if (!x86_decode(&i, (const uint8_t *)(uintptr_t)addr, addr, (int)cpu->mode64))
            return 0;
        if (writes_flags(&i))
            return 1;
        if (!ignores_flags(&i) || x86_ends_block(&i))
            return 0;
        addr += i.len;
    }
    return 0;
}

/* ------------------------------------------------------------ flags */

/* records the lazy flags of an alu operation from T0, T1 and its result T2. */
static void record_flags(Ctx *x, int kind, int size)
{
    Emit *e = x->e;
    int lf = kind == ALU_ADD ? X86_LF_ADD : kind == ALU_SUB || kind == ALU_CMP ? X86_LF_SUB : X86_LF_LOGIC;
    mov_imm(e, T4, X86_LF(lf, log2_size(size)));
    str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
    str(e, 8, T0, CPU, offsetof(X86Cpu, lf_a));
    str(e, 8, T1, CPU, offsetof(X86Cpu, lf_b));
    str(e, 8, T2, CPU, offsetof(X86Cpu, lf_res));
}

/*
 * T2 = T0 op T1 at size, operands zero extended, with the arm64 flags of the
 * operation as x86 sets them: narrow operations compare the operands shifted
 * to the top of a 32 bit register.
 */
static void alu_core(Ctx *x, int kind, int size)
{
    Emit *e = x->e;
    static const uint32_t ops[] = {OP_ADD, OP_ORR, 0, 0, OP_AND, OP_SUB, OP_EOR, OP_SUB, OP_AND};
    static const uint32_t sops[] = {OP_ADDS, OP_ORR, 0, 0, OP_ANDS, OP_SUBS, OP_EOR, OP_SUBS, OP_ANDS};
    if (size >= 4) {
        rrr(e, sops[kind], size, T2, T0, T1);
        if (kind == ALU_OR || kind == ALU_XOR)
            rrr(e, OP_ANDS, size, XZR, T2, T2);
    } else {
        int sh = 32 - size * 8;
        rrr(e, ops[kind], 4, T2, T0, T1);
        narrow(e, size, T2);
        ubfm(e, 4, S0, T0, (32 - sh) & 31, 31 - sh); /* lsl s0, t0, #sh */
        uint32_t shifted = (uint32_t)sh << 10;
        if (kind == ALU_OR || kind == ALU_XOR) {
            ubfm(e, 4, S0, T2, (32 - sh) & 31, 31 - sh);
            rrr(e, OP_ANDS, 4, XZR, S0, S0);
        } else {
            put(e, sops[kind] | shifted | (uint32_t)T1 << 16 | (uint32_t)S0 << 5 | XZR);
        }
    }
    if (x->flags_live)
        record_flags(x, kind, size);
    set_nzcv(x, kind);
}

static int emit_alu(Ctx *x, const X86Insn *i, int kind)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = alu_size(i);
    uint64_t mask = size_mask(size);
    int dst; /* 1 the modrm operand, 0 the reg one, -1 the accumulator */
    if (op >= 0x80) {
        if (op == 0x84 || op == 0x85) {
            load_rm(x, i, size, T0);
            load_reg(e, size, T1, i->reg, i->rex);
        } else if (op == 0xA8 || op == 0xA9) {
            load_reg(e, size, T0, X86_RAX, 1);
            mov_imm(e, T1, i->imm & mask);
        } else {
            load_rm(x, i, size, T0);
            mov_imm(e, T1, i->imm & mask);
        }
        dst = 1;
    } else if ((op & 7) >= 4) {
        load_reg(e, size, T0, X86_RAX, 1);
        mov_imm(e, T1, i->imm & mask);
        dst = -1;
    } else if ((op & 7) <= 1) {
        load_rm(x, i, size, T0);
        load_reg(e, size, T1, i->reg, i->rex);
        dst = 1;
    } else {
        load_reg(e, size, T0, i->reg, i->rex);
        load_rm(x, i, size, T1);
        dst = 0;
    }
    alu_core(x, kind, size);
    if (kind != ALU_CMP && kind != ALU_TEST) {
        if (dst == 1)
            store_rm(x, i, size, T2, 1);
        else if (dst == 0)
            store_reg(e, size, T2, i->reg, i->rex);
        else
            store_reg(e, size, T2, X86_RAX, 1);
    }
    return 1;
}

/* the arm64 condition for x86 condition cc after an instruction kind; -1 none, -2 never, -3 always. */
static int arm_cond(int kind, int cc)
{
    static const int sub[16] = {VS, VC, LO, HS, EQ, NE, LS, HI, MI, PL, -1, -1, LT, GE, LE, GT};
    static const int add[16] = {VS, VC, HS, LO, EQ, NE, -1, -1, MI, PL, -1, -1, LT, GE, LE, GT};
    /* after ands: c and v are clear like cf and of. */
    static const int logic[16] = {-2, -3, -2, -3, EQ, NE, EQ, NE, MI, PL, -1, -1, LT, GE, LE, GT};
    /* after fcmp: unordered is c and v set, less n set, equal z and c set. */
    static const int ucomi[16] = {-2, -3, LT, GE, -1, -1, LE, GT, -2, -3, VS, VC, -2, -3, -1, -1};
    /* after inc and dec, computed as an add or sub of 1: cf is the earlier one. */
    static const int incdec[16] = {VS, VC, -1, -1, EQ, NE, -1, -1, MI, PL, -1, -1, LT, GE, LE, GT};
    switch (kind) {
    case ALU_SUB: case ALU_CMP: return sub[cc];
    case ALU_ADD: return add[cc];
    case ALU_OR: case ALU_AND: case ALU_XOR: case ALU_TEST: return logic[cc];
    case ALU_UCOMI: return ucomi[cc];
    case ALU_INCDEC: return incdec[cc];
    }
    return -1;
}

static void cset(Emit *e, int rd, int cond)
{
    put(e, 0x1A9F07E0u | (uint32_t)(cond ^ 1) << 12 | (uint32_t)rd);
}

/*
 * w0 = x86 condition cc, from the arm64 flags when they describe it or from
 * the lazy flags. returns whether the arm64 flags survived.
 */
static int condition_w0(Ctx *x, int cc)
{
    Emit *e = x->e;
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
    if (cond >= 0) {
        cset(e, 0, cond);
        return 1;
    }
    if (cond == -2 || cond == -3) {
        mov_imm(e, 0, cond == -3);
        return 1;
    }
    mov_reg(e, 8, 0, CPU);
    mov_imm(e, 1, (uint64_t)cc);
    call(e, (const void *)x86_cond);
    return 0;
}

static void emit_jcc(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int cc = i->opcode & 15;
    uint64_t target = x->next + i->imm;
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
    if (cond == -3) {
        exit_to(e, -1, target, x->count);
        return;
    }
    if (cond == -2) {
        exit_to(e, -1, x->next, x->count);
        return;
    }
    uint32_t *taken;
    if (cond >= 0) {
        taken = b_cond(e, cond);
    } else {
        condition_w0(x, cc);
        taken = cbnz_w(e, 0);
    }
    exit_to(e, -1, x->next, x->count);
    patch19(e, taken);
    exit_to(e, -1, target, x->count);
}

/* inc and dec keep the carry flag, read before their own flags replace the arm64 ones. */
static int emit_incdec(Ctx *x, const X86Insn *i, int size, int dec)
{
    Emit *e = x->e;
    if (x->flags_live) {
        int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, 2) : -1;
        if (cond >= 0)
            cset(e, T3, cond);
        else if (cond == -2 || cond == -3)
            mov_imm(e, T3, cond == -3);
        else {
            mov_reg(e, 8, 0, CPU);
            call(e, (const void *)x86_arith_flags);
            ubfm(e, 4, T3, 0, 0, 0); /* cf, bit 0 */
        }
    }
    load_rm(x, i, size, T0);
    mov_imm(e, T1, 1);
    if (size >= 4) {
        rrr(e, dec ? OP_SUBS : OP_ADDS, size, T2, T0, T1);
    } else {
        int sh = 32 - size * 8;
        rrr(e, dec ? OP_SUB : OP_ADD, 4, T2, T0, T1);
        narrow(e, size, T2);
        ubfm(e, 4, S0, T0, (32 - sh) & 31, 31 - sh);
        put(e, (dec ? OP_SUBS : OP_ADDS) | (uint32_t)sh << 10 | (uint32_t)T1 << 16 | (uint32_t)S0 << 5 | XZR);
    }
    if (x->flags_live) {
        mov_imm(e, T4, X86_LF(dec ? X86_LF_DEC : X86_LF_INC, log2_size(size)));
        str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
        str(e, 8, T0, CPU, offsetof(X86Cpu, lf_a));
        str(e, 8, T1, CPU, offsetof(X86Cpu, lf_b));
        str(e, 8, T2, CPU, offsetof(X86Cpu, lf_res));
        str(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
    }
    store_rm(x, i, size, T2, 1);
    set_nzcv(x, ALU_INCDEC);
    return 1;
}

#define OP_LSLV 0x1AC02000u
#define OP_LSRV 0x1AC02400u
#define OP_ASRV 0x1AC02800u
#define OP_RORV 0x1AC02C00u

/* shifts and rotates whose flags nothing reads. */
static int emit_shift(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = (op & 1) ? i->opsize : 1, sub = (i->modrm >> 3) & 7;
    if (x->flags_live || sub == 2 || sub == 3 || (sub < 2 && size < 4))
        return 0;
    int bits = size * 8;
    load_rm(x, i, size, T0);
    if (sub == 7 && size < 4)
        sbfm(e, 4, T0, T0, 0, bits - 1);
    int work = size == 8 ? 8 : 4;
    if (op <= 0xD1) {
        unsigned n = op <= 0xC1 ? (unsigned)(i->imm & 0xFF) : 1;
        n &= size == 8 ? 63 : 31;
        if (!n) {
            /* no flags, but a 32 bit register is still written: its upper half clears. */
            if (size == 4 && i->mod == 3)
                store_reg(e, 4, T0, i->rm, i->rex);
            return x->keep = 1, 1;
        }
        switch (sub) {
        case 4: case 6: ubfm(e, work, T2, T0, (int)((unsigned)(work * 8) - n) & (work * 8 - 1), work * 8 - 1 - (int)n); break;
        case 5: ubfm(e, work, T2, T0, (int)n, work * 8 - 1); break;
        case 7: sbfm(e, work, T2, T0, (int)(n < 32 || work == 8 ? n : 31), work * 8 - 1); break;
        case 1: /* ror */
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)T0 << 16 | n << 10 | (uint32_t)T0 << 5 | T2);
            break;
        default: /* rol */
            n %= (unsigned)bits;
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)T0 << 16 | (((unsigned)bits - n) % (unsigned)bits) << 10 |
                       (uint32_t)T0 << 5 | T2);
            break;
        }
    } else {
        load_reg(e, 1, T1, X86_RCX, 1);
        ubfm(e, 4, T1, T1, 0, size == 8 ? 5 : 4);
        if (sub == 0) { /* rol: ror by the complement */
            put(e, (work == 8 ? 0xCB000000u : 0x4B000000u) | (uint32_t)T1 << 16 | (uint32_t)XZR << 5 | T1); /* neg */
        }
        uint32_t ops[] = {OP_RORV, OP_RORV, 0, 0, OP_LSLV, OP_LSRV, OP_LSLV, OP_ASRV};
        rrr(e, ops[sub], work, T2, T0, T1);
    }
    store_rm(x, i, size, T2, 1);
    return 1;
}

static void push_value(Emit *e, int value)
{
    load_reg(e, 8, T3, X86_RSP, 1);
    put(e, 0xD1002000u | (uint32_t)T3 << 5 | (uint32_t)T3); /* sub t3, t3, #8 */
    str(e, 8, value, T3, 0);
    str(e, 8, T3, CPU, OFF_R(X86_RSP));
}

/* ------------------------------------------------------------ sse */

enum { V0 = 0, V1 = 1, V2 = 2, V3 = 3 };

static uint32_t OFF_X(int reg) { return (uint32_t)(offsetof(X86Cpu, xmm) + (uint32_t)reg * 16); }

/* vector loads and stores of 4, 8 or 16 bytes; narrower loads clear the rest of the register. */
static void vldr(Emit *e, int bytes, int vt, int rn, uint32_t off)
{
    uint32_t op = bytes == 16 ? 0x3DC00000u : bytes == 8 ? 0xFD400000u : 0xBD400000u;
    put(e, op | (off / (uint32_t)bytes) << 10 | (uint32_t)rn << 5 | (uint32_t)vt);
}

static void vstr(Emit *e, int bytes, int vt, int rn, uint32_t off)
{
    uint32_t op = bytes == 16 ? 0x3D800000u : bytes == 8 ? 0xFD000000u : 0xBD000000u;
    put(e, op | (off / (uint32_t)bytes) << 10 | (uint32_t)rn << 5 | (uint32_t)vt);
}

static void v3(Emit *e, uint32_t op, int vd, int vn, int vm)
{
    put(e, op | (uint32_t)vm << 16 | (uint32_t)vn << 5 | (uint32_t)vd);
}

/* ins vd.lane[dst], vn.lane[src] for lanes of 4 or 8 bytes. */
static void ins(Emit *e, int bytes, int vd, int dst, int vn, int src)
{
    uint32_t imm5 = bytes == 8 ? ((uint32_t)dst << 4 | 8) : ((uint32_t)dst << 3 | 4);
    uint32_t imm4 = bytes == 8 ? (uint32_t)src << 3 : (uint32_t)src << 2;
    put(e, 0x6E000400u | imm5 << 16 | imm4 << 11 | (uint32_t)vn << 5 | (uint32_t)vd);
}

#define V_AND 0x4E201C00u
#define V_BIC 0x4E601C00u
#define V_ORR 0x4EA01C00u
#define V_EOR 0x6E201C00u
#define V_BSL 0x6E601C00u

static void vmov(Emit *e, int vd, int vn) { v3(e, V_ORR, vd, vn, vn); }

/* the xmm or memory source operand into vt, bytes wide. */
static void load_xsrc(Ctx *x, const X86Insn *i, int bytes, int vt)
{
    if (i->mod == 3) {
        vldr(x->e, 16, vt, CPU, OFF_X(i->rm));
    } else {
        ea(x, i);
        vldr(x->e, bytes, vt, ADDR, 0);
    }
}

/* a nan result goes through the interpreter, which applies the x86 nan rules; cond holds when there is none. */
static uint32_t *nan_guard(Ctx *x, int cond)
{
    uint32_t *ok = b_cond(x->e, cond);
    emit_fallback(x, x->insn);
    return ok;
}

static void nan_join(Ctx *x, uint32_t *ok, uint32_t **done)
{
    *done = b_always(x->e);
    patch19(x->e, ok);
}

/* checks lane 0 of a scalar result: ok when ordered. */
static uint32_t *scalar_nan_check(Ctx *x, int dbl, int v)
{
    put(x->e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)v << 16 | (uint32_t)v << 5);
    return nan_guard(x, VC);
}

/* checks every lane of a packed result: ok when none is a nan. */
static uint32_t *packed_nan_check(Ctx *x, int dbl, int v)
{
    Emit *e = x->e;
    v3(e, dbl ? 0x4E60E400u : 0x4E20E400u, V3, v, v); /* fcmeq v3, v, v */
    put(e, (dbl ? 0x0EA12800u : 0x0E612800u) | (uint32_t)V3 << 5 | V3); /* xtn */
    put(e, 0x9E660000u | (uint32_t)V3 << 5 | 16);                       /* fmov x16, d3 */
    put(e, 0xB100061Fu);                                                /* cmn x16, #1 */
    return nan_guard(x, EQ);
}

static int emit_sse(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode, pfx = i->mandatory;
    int dbl = pfx == 0x66 || pfx == 0xF2;
    int scalar = pfx == 0xF2 || pfx == 0xF3;
    int lane = pfx == 0xF2 || pfx == 0x66 ? 8 : 4; /* element size */
    uint32_t sz = dbl ? 1u << 22 : 0;
    uint32_t *ok = NULL, *done = NULL;
    x->keep = 1; /* every case but the arithmetic, conversions to integers and compares */
    switch (op) {
    case 0x10: /* movups, movupd, movss, movsd */
        if (scalar && i->mod == 3) {
            vldr(e, 16, V0, CPU, OFF_X(i->reg));
            vldr(e, 16, V1, CPU, OFF_X(i->rm));
            ins(e, lane, V0, 0, V1, 0);
            vstr(e, 16, V0, CPU, OFF_X(i->reg));
        } else {
            load_xsrc(x, i, scalar ? lane : 16, V0);
            vstr(e, 16, V0, CPU, OFF_X(i->reg));
        }
        return 1;
    case 0x11:
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        if (i->mod == 3 && scalar) {
            vldr(e, 16, V1, CPU, OFF_X(i->rm));
            ins(e, lane, V1, 0, V0, 0);
            vstr(e, 16, V1, CPU, OFF_X(i->rm));
        } else if (i->mod == 3) {
            vstr(e, 16, V0, CPU, OFF_X(i->rm));
        } else {
            ea(x, i);
            vstr(e, scalar ? lane : 16, V0, ADDR, 0);
        }
        return 1;
    case 0x28: /* movaps, movapd */
    case 0x6F: /* movdqa, movdqu */
        if ((op == 0x28 && scalar) || (op == 0x6F && pfx != 0x66 && pfx != 0xF3))
            return 0;
        load_xsrc(x, i, 16, V0);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0x29:
    case 0x7F:
        if ((op == 0x29 && scalar) || (op == 0x7F && pfx != 0x66 && pfx != 0xF3))
            return 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        if (i->mod == 3)
            vstr(e, 16, V0, CPU, OFF_X(i->rm));
        else {
            ea(x, i);
            vstr(e, 16, V0, ADDR, 0);
        }
        return 1;
    case 0xD6: /* movq xmm/m64, xmm */
        if (pfx != 0x66)
            return 0;
        vldr(e, 8, V0, CPU, OFF_X(i->reg));
        if (i->mod == 3)
            vstr(e, 16, V0, CPU, OFF_X(i->rm));
        else {
            ea(x, i);
            vstr(e, 8, V0, ADDR, 0);
        }
        return 1;
    case 0x7E:
        if (pfx == 0xF3) { /* movq xmm, xmm/m64 */
            if (i->mod == 3)
                vldr(e, 8, V0, CPU, OFF_X(i->rm));
            else
                load_xsrc(x, i, 8, V0);
            vstr(e, 16, V0, CPU, OFF_X(i->reg));
            return 1;
        }
        if (pfx != 0x66)
            return 0;
        /* movd, movq rm, xmm */
        if (i->mod == 3) {
            if (i->rex & 8)
                ldr(e, 8, T0, CPU, OFF_X(i->reg));
            else
                ldr(e, 4, T0, CPU, OFF_X(i->reg));
            store_reg(e, 8, T0, i->rm, 1);
        } else {
            vldr(e, (i->rex & 8) ? 8 : 4, V0, CPU, OFF_X(i->reg));
            ea(x, i);
            vstr(e, (i->rex & 8) ? 8 : 4, V0, ADDR, 0);
        }
        return 1;
    case 0x6E: /* movd, movq xmm, rm */
        if (pfx != 0x66)
            return 0;
        if (i->mod == 3) {
            if (i->rex & 8)
                vldr(e, 8, V0, CPU, OFF_R(i->rm));
            else
                vldr(e, 4, V0, CPU, OFF_R(i->rm));
        } else {
            load_xsrc(x, i, (i->rex & 8) ? 8 : 4, V0);
        }
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0x14: /* unpcklps, unpcklpd */
    case 0x15:
        if (scalar)
            return 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, 16, V1);
        v3(e, (op == 0x14 ? 0x0E003800u : 0x0E007800u) | 0x40000000u | (dbl ? 3u : 2u) << 22, V0, V0, V1);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0x54: case 0x55: case 0x56: case 0x57: /* andps, andnps, orps, xorps and the pd forms */
    case 0xDB: case 0xDF: case 0xEB: case 0xEF: { /* pand, pandn, por, pxor */
        if (op >= 0xDB ? pfx != 0x66 : scalar)
            return 0;
        int k = op >= 0xDB ? (op == 0xDB ? 0 : op == 0xDF ? 1 : op == 0xEB ? 2 : 3) : op - 0x54;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, 16, V1);
        if (k == 1)
            v3(e, V_BIC, V0, V1, V0); /* ~dst & src */
        else
            v3(e, k == 0 ? V_AND : k == 2 ? V_ORR : V_EOR, V0, V0, V1);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    }
    case 0x58: case 0x59: case 0x5C: case 0x5E: case 0x51: { /* add, mul, sub, div, sqrt */
        x->keep = 0;
        static const uint32_t vec[] = {0x4E20D400u, 0x6E20DC00u, 0, 0, 0x4EA0D400u, 0, 0x6E20FC00u};
        static const uint32_t sca[] = {0x1E202800u, 0x1E200800u, 0, 0, 0x1E203800u, 0, 0x1E201800u};
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, scalar ? lane : 16, V1);
        if (scalar) {
            if (op == 0x51)
                put(e, 0x1E21C000u | sz | (uint32_t)V1 << 5 | V2);
            else
                v3(e, sca[op - 0x58] | sz, V2, V0, V1);
            ok = scalar_nan_check(x, dbl, V2);
            nan_join(x, ok, &done);
            ins(e, lane, V0, 0, V2, 0);
        } else {
            if (op == 0x51)
                put(e, 0x6EA1F800u | sz | (uint32_t)V1 << 5 | V2);
            else
                v3(e, vec[op - 0x58] | sz, V2, V0, V1);
            ok = packed_nan_check(x, dbl, V2);
            nan_join(x, ok, &done);
            vmov(e, V0, V2);
        }
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        patch26(e, done);
        return 1;
    }
    case 0x5D: /* min: a < b ? a : b, the second operand for nans and equal values */
    case 0x5F: /* max: a > b ? a : b */
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, scalar ? lane : 16, V1);
        if (op == 0x5D)
            v3(e, 0x6EA0E400u | sz, V2, V1, V0); /* fcmgt v2, b, a */
        else
            v3(e, 0x6EA0E400u | sz, V2, V0, V1); /* fcmgt v2, a, b */
        v3(e, V_BSL, V2, V0, V1);
        if (scalar)
            ins(e, lane, V0, 0, V2, 0);
        else
            vmov(e, V0, V2);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0xC2: { /* cmpps, cmppd, cmpss, cmpsd */
        int pred = (int)(i->imm & 7);
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, scalar ? lane : 16, V1);
        switch (pred & 3) {
        case 0: v3(e, 0x4E20E400u | sz, V2, V0, V1); break; /* eq: fcmeq */
        case 1: v3(e, 0x6EA0E400u | sz, V2, V1, V0); break; /* lt: fcmgt b, a */
        case 2: v3(e, 0x6E20E400u | sz, V2, V1, V0); break; /* le: fcmge b, a */
        default: /* unordered: not both ordered */
            v3(e, 0x4E20E400u | sz, V2, V0, V0);
            v3(e, 0x4E20E400u | sz, V3, V1, V1);
            v3(e, V_AND, V2, V2, V3);
            pred ^= 4;
            break;
        }
        if (pred & 4)
            put(e, 0x6E205800u | (uint32_t)V2 << 5 | V2); /* not */
        if (scalar)
            ins(e, lane, V0, 0, V2, 0);
        else
            vmov(e, V0, V2);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    }
    case 0x2E: /* ucomiss, ucomisd */
    case 0x2F: { /* comiss, comisd */
        if (scalar)
            return 0;
        x->keep = 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, lane, V1);
        put(e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)V1 << 16 | (uint32_t)V0 << 5);
        put(e, 0x1A9F07E0u | (uint32_t)(EQ ^ 1) << 12 | T0); /* cset t0, eq */
        put(e, 0x1A9F07E0u | (uint32_t)(VS ^ 1) << 12 | T1); /* cset t1, vs: unordered */
        put(e, 0x1A9F07E0u | (uint32_t)(LO ^ 1) << 12 | T2); /* cset t2, lo: less */
        rrr(e, OP_ORR, 4, T0, T0, T1);                       /* zf */
        rrr(e, OP_ORR, 4, T2, T2, T1);                       /* cf */
        put(e, OP_ORR | 6u << 10 | (uint32_t)T0 << 16 | (uint32_t)T2 << 5 | T2); /* | zf << 6 */
        put(e, OP_ORR | 2u << 10 | (uint32_t)T1 << 16 | (uint32_t)T2 << 5 | T2); /* | pf << 2 */
        ldr(e, 4, T3, CPU, offsetof(X86Cpu, flags));
        mov_imm(e, T4, X86_ARITH_FLAGS);
        put(e, 0x0A200000u | (uint32_t)T4 << 16 | (uint32_t)T3 << 5 | T3); /* bic */
        rrr(e, OP_ORR, 4, T3, T3, T2);
        str(e, 4, T3, CPU, offsetof(X86Cpu, flags));
        str(e, 4, XZR, CPU, offsetof(X86Cpu, lf_op));
        set_nzcv(x, ALU_UCOMI);
        return 1;
    }
    case 0x2A: { /* cvtsi2ss, cvtsi2sd */
        if (!scalar)
            return 0;
        int wide = (i->rex & 8) != 0;
        load_rm(x, i, wide ? 8 : 4, T0);
        put(e, (wide ? 0x80000000u : 0) | (dbl ? 0x1E620000u : 0x1E220000u) | (uint32_t)T0 << 5 | V2);
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        ins(e, lane, V0, 0, V2, 0);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    }
    case 0x2C: { /* cvttss2si, cvttsd2si: nans and out of range values take the interpreter's indefinite value */
        if (!scalar)
            return 0;
        x->keep = 0;
        int wide = (i->rex & 8) != 0;
        load_xsrc(x, i, lane, V1);
        put(e, (wide ? 0x80000000u : 0) | (dbl ? 0x1E780000u : 0x1E380000u) | (uint32_t)V1 << 5 | T0);
        put(e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)V1 << 16 | (uint32_t)V1 << 5); /* fcmp v1, v1 */
        uint32_t *nan = b_cond(e, VS);
        /* saturated: x - min is 0 or -1 (all ones) for the two limits. */
        mov_imm(e, T1, wide ? 0x8000000000000000ull : 0x80000000ull);
        rrr(e, OP_EOR, wide ? 8 : 4, T1, T0, T1);
        put(e, (wide ? 0xB1000400u : 0x31000400u) | (uint32_t)T1 << 5 | XZR); /* cmn t1, #1 */
        uint32_t *max = b_cond(e, EQ);
        rrr(e, OP_SUBS, wide ? 8 : 4, XZR, T1, XZR);
        ok = b_cond(e, NE);
        patch19(e, nan);
        patch19(e, max);
        emit_fallback(x, i);
        done = b_always(e);
        patch19(e, ok);
        store_reg(e, 8, T0, i->reg, 1);
        patch26(e, done);
        return 1;
    }
    case 0x5A: /* cvtss2sd, cvtsd2ss */
        if (!scalar)
            return 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, lane, V1);
        put(e, (dbl ? 0x1E624000u : 0x1E22C000u) | (uint32_t)V1 << 5 | V2);
        ins(e, dbl ? 4 : 8, V0, 0, V2, 0);
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0x70: { /* pshufd */
        if (pfx != 0x66)
            return 0;
        load_xsrc(x, i, 16, V1);
        for (int k = 0; k < 4; k++)
            ins(e, 4, V2, k, V1, (int)(i->imm >> (2 * k)) & 3);
        vstr(e, 16, V2, CPU, OFF_X(i->reg));
        return 1;
    }
    case 0x72: { /* psrld, psrad, pslld by an immediate */
        int sub = (i->modrm >> 3) & 7;
        if (pfx != 0x66 || i->mod != 3 || (sub != 2 && sub != 4 && sub != 6))
            return 0;
        unsigned n = (unsigned)(i->imm & 0xFF);
        vldr(e, 16, V0, CPU, OFF_X(i->rm));
        if (sub == 4) {
            if (n > 32)
                n = 32;
            if (n)
                put(e, 0x4F000400u | (64u - n) << 16 | (uint32_t)V0 << 5 | V0); /* sshr */
        } else if (n >= 32) {
            put(e, 0x6F00E400u | V0); /* movi v0.2d, #0 */
        } else if (n) {
            if (sub == 2)
                put(e, 0x6F000400u | (64u - n) << 16 | (uint32_t)V0 << 5 | V0); /* ushr */
            else
                put(e, 0x4F005400u | (32u + n) << 16 | (uint32_t)V0 << 5 | V0); /* shl */
        }
        vstr(e, 16, V0, CPU, OFF_X(i->rm));
        return 1;
    }
    case 0xC6: { /* shufps, shufpd */
        if (pfx != 0 && pfx != 0x66)
            return 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        load_xsrc(x, i, 16, V1);
        unsigned imm = (unsigned)i->imm;
        if (pfx == 0x66) {
            ins(e, 8, V2, 0, V0, (int)(imm & 1));
            ins(e, 8, V2, 1, V1, (int)((imm >> 1) & 1));
        } else {
            for (int k = 0; k < 4; k++)
                ins(e, 4, V2, k, k < 2 ? V0 : V1, (int)(imm >> (2 * k)) & 3);
        }
        vstr(e, 16, V2, CPU, OFF_X(i->reg));
        return 1;
    }
    case 0x12: /* movlps, movlpd, movhlps */
    case 0x16: /* movhps, movhpd, movlhps */
        if (scalar || (pfx == 0x66 && i->mod == 3))
            return 0;
        vldr(e, 16, V0, CPU, OFF_X(i->reg));
        if (i->mod == 3) {
            vldr(e, 16, V1, CPU, OFF_X(i->rm));
            ins(e, 8, V0, op == 0x16, V1, op == 0x12);
        } else {
            load_xsrc(x, i, 8, V1);
            ins(e, 8, V0, op == 0x16, V1, 0);
        }
        vstr(e, 16, V0, CPU, OFF_X(i->reg));
        return 1;
    case 0x13: /* movlps, movlpd store */
    case 0x17: /* movhps, movhpd store */
        if (scalar || i->mod == 3)
            return 0;
        ea(x, i);
        if (op == 0x13) {
            vldr(e, 8, V0, CPU, OFF_X(i->reg));
        } else {
            vldr(e, 16, V0, CPU, OFF_X(i->reg));
            ins(e, 8, V0, 0, V0, 1);
        }
        vstr(e, 8, V0, ADDR, 0);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ general instructions */

/* emits one instruction natively, or returns 0 for the interpreter. */
static int emit_one(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = i->opsize;
    int kind = alu_op(i);
    if (kind >= 0)
        return emit_alu(x, i, kind);
    if (i->lock)
        return 0;
    if (i->map == 1) {
        if (op == 0x1F)
            return x->keep = 1, 1;
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF) { /* movzx, movsx */
            int src = op & 1 ? 2 : 1;
            if (size == 2 && src == 2)
                return 0;
            load_rm(x, i, src, T0);
            if (op >= 0xBE)
                sbfm(e, size == 8 ? 8 : 4, T0, T0, 0, src * 8 - 1);
            narrow(e, size, T0);
            store_reg(e, size, T0, i->reg, i->rex);
            return x->keep = 1, 1;
        }
        if (op >= 0x90 && op <= 0x9F) { /* setcc */
            x->keep = condition_w0(x, op & 15);
            store_rm(x, i, 1, 0, 0);
            return 1;
        }
        if (op >= 0x40 && op <= 0x4F) { /* cmovcc */
            int cc = op & 15;
            int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
            x->keep = cond != -1;
            if (cond == -1)
                condition_w0(x, cc);
            load_rm(x, i, size, T1);
            load_reg(e, size, T0, i->reg, i->rex);
            if (cond == -2 || cond == -3) {
                if (cond == -3)
                    mov_reg(e, 8, T0, T1);
            } else {
                if (cond == -1) {
                    put(e, 0x7100001Fu); /* cmp w0, #0 */
                    cond = NE;
                }
                put(e, (size == 8 ? 0x9A800000u : 0x1A800000u) | (uint32_t)T0 << 16 | (uint32_t)cond << 12 |
                           (uint32_t)T1 << 5 | T0); /* csel t0, t1, t0, cond */
            }
            store_reg(e, size, T0, i->reg, i->rex);
            return 1;
        }
        if ((op == 0xAF) && !i->mandatory && !x->flags_live && size >= 4) { /* imul r, rm */
            load_rm(x, i, size, T1);
            load_reg(e, size, T0, i->reg, i->rex);
            put(e, (size == 8 ? 0x9B007C00u : 0x1B007C00u) | (uint32_t)T1 << 16 | (uint32_t)T0 << 5 | T2);
            store_reg(e, size, T2, i->reg, i->rex);
            return 1;
        }
        return emit_sse(x, i);
    }
    if (i->map != 0)
        return 0;
    switch (op) {
    case 0x88: /* mov rm8, r8 */
    case 0x89: /* mov rm, r */
        size = op == 0x88 ? 1 : size;
        load_reg(e, size, T0, i->reg, i->rex);
        store_rm(x, i, size, T0, 0);
        return x->keep = 1, 1;
    case 0x8A:
    case 0x8B: /* mov r, rm */
        size = op == 0x8A ? 1 : size;
        load_rm(x, i, size, T0);
        store_reg(e, size, T0, i->reg, i->rex);
        return x->keep = 1, 1;
    case 0xC6:
    case 0xC7: /* mov rm, imm */
        if ((i->modrm >> 3) & 7)
            return 0;
        size = op == 0xC6 ? 1 : size;
        mov_imm(e, T0, i->imm & size_mask(size));
        store_rm(x, i, size, T0, 0);
        return x->keep = 1, 1;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        mov_imm(e, T0, i->imm & 0xFF);
        store_reg(e, 1, T0, (op & 7) | ((i->rex & 1) << 3), i->rex);
        return x->keep = 1, 1;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        mov_imm(e, T0, i->imm & size_mask(size));
        store_reg(e, size, T0, (op & 7) | ((i->rex & 1) << 3), 1);
        return x->keep = 1, 1;
    case 0x8D: { /* lea */
        if (i->mod == 3)
            return 0;
        X86Insn flat = *i;
        flat.seg = 0;
        ea(x, &flat);
        if (size == 4)
            mov_reg(e, 4, ADDR, ADDR);
        store_reg(e, size, ADDR, i->reg, 1);
        return x->keep = 1, 1;
    }
    case 0x63: /* movsxd */
        if (size != 8)
            return 0;
        load_rm(x, i, 4, T0);
        sbfm(e, 8, T0, T0, 0, 31);
        store_reg(e, 8, T0, i->reg, 1);
        return x->keep = 1, 1;
    case 0x98: /* cwde, cdqe */
        if (size < 4)
            return 0;
        load_reg(e, size / 2, T0, X86_RAX, 1);
        sbfm(e, size == 8 ? 8 : 4, T0, T0, 0, size * 4 - 1);
        store_reg(e, size, T0, X86_RAX, 1);
        return x->keep = 1, 1;
    case 0x99: /* cdq, cqo */
        if (size < 4)
            return 0;
        load_reg(e, size, T0, X86_RAX, 1);
        sbfm(e, size, T0, T0, size * 8 - 1, size * 8 - 1);
        store_reg(e, size, T0, X86_RDX, 1);
        return x->keep = 1, 1;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        if (size != 8)
            return 0;
        load_reg(e, 8, T0, (op & 7) | ((i->rex & 1) << 3), 1);
        push_value(e, T0);
        return x->keep = 1, 1;
    case 0x6A: case 0x68: /* push imm */
        if (size != 8)
            return 0;
        mov_imm(e, T0, i->imm);
        push_value(e, T0);
        return x->keep = 1, 1;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        if (size != 8 || reg == X86_RSP)
            return 0;
        load_reg(e, 8, T3, X86_RSP, 1);
        ldr(e, 8, T0, T3, 0);
        put(e, 0x91002000u | (uint32_t)T3 << 5 | (uint32_t)T3); /* add t3, t3, #8 */
        str(e, 8, T3, CPU, OFF_R(X86_RSP));
        store_reg(e, 8, T0, reg, 1);
        return x->keep = 1, 1;
    }
    case 0x90:
        if (i->rex & 1)
            return 0;
        return x->keep = 1, 1;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        return emit_shift(x, i);
    case 0xFE:
    case 0xFF: {
        int sub = (i->modrm >> 3) & 7;
        if (sub < 2)
            return emit_incdec(x, i, op == 0xFE ? 1 : size, sub);
        if (sub == 6 && op == 0xFF && size == 8) { /* push rm */
            load_rm(x, i, 8, T0);
            push_value(e, T0);
            return x->keep = 1, 1;
        }
        return 0;
    }
    case 0xF6:
    case 0xF7: {
        int sub = (i->modrm >> 3) & 7, sz = op == 0xF6 ? 1 : size;
        if (sub == 2) { /* not */
            load_rm(x, i, sz, T0);
            put(e, (sz == 8 ? 0xAA2003E0u : 0x2A2003E0u) | (uint32_t)T0 << 16 | T2); /* mvn */
            narrow(e, sz, T2);
            store_rm(x, i, sz, T2, 1);
            return x->keep = 1, 1;
        }
        if (sub == 3) { /* neg: 0 - a */
            load_rm(x, i, sz, T1);
            mov_imm(e, T0, 0);
            alu_core(x, ALU_SUB, sz);
            store_rm(x, i, sz, T2, 1);
            return 1;
        }
        if ((sub == 4 || sub == 5) && sz >= 4 && !x->flags_live) { /* mul, imul: rdx:rax = rax * rm */
            load_rm(x, i, sz, T1);
            load_reg(e, sz, T0, X86_RAX, 1);
            if (sz == 8) {
                put(e, 0x9B007C00u | (uint32_t)T1 << 16 | (uint32_t)T0 << 5 | T2);
                put(e, (sub == 5 ? 0x9B407C00u : 0x9BC07C00u) | (uint32_t)T1 << 16 | (uint32_t)T0 << 5 | T3);
            } else {
                put(e, (sub == 5 ? 0x9B207C00u : 0x9BA07C00u) | (uint32_t)T1 << 16 | (uint32_t)T0 << 5 | T2);
                ubfm(e, 8, T3, T2, 32, 63);
                mov_reg(e, 4, T2, T2);
            }
            store_reg(e, 8, T2, X86_RAX, 1);
            store_reg(e, 8, T3, X86_RDX, 1);
            return 1;
        }
        return 0;
    }
    case 0x69:
    case 0x6B: /* imul r, rm, imm */
        if (x->flags_live || size < 4)
            return 0;
        load_rm(x, i, size, T0);
        mov_imm(e, T1, i->imm & size_mask(size));
        put(e, (size == 8 ? 0x9B007C00u : 0x1B007C00u) | (uint32_t)T1 << 16 | (uint32_t)T0 << 5 | T2);
        store_reg(e, size, T2, i->reg, i->rex);
        return 1;
    }
    return 0;
}

/* emits one instruction, tracking whether the arm64 flags still describe the x86 ones. */
static int emit_insn(Ctx *x, const X86Insn *i)
{
    int prev = x->nzcv_alu;
    x->keep = 0;
    x->nzcv_set = 0;
    if (!emit_one(x, i))
        return 0;
    if (x->keep)
        x->nzcv_alu = prev;
    else if (!x->nzcv_set)
        x->nzcv_alu = -1;
    return 1;
}

/* control transfers ending the block natively; returns 0 to leave it to the interpreter. */
static int emit_branch(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    if (is_jcc(i)) {
        emit_jcc(x, i);
        return 1;
    }
    if (i->map != 0)
        return 0;
    switch (op) {
    case 0xE9:
    case 0xEB:
        exit_to(e, -1, x->next + i->imm, x->count);
        return 1;
    case 0xE8: /* call rel */
        mov_imm(e, T0, x->next);
        push_value(e, T0);
        exit_to(e, -1, x->next + i->imm, x->count);
        return 1;
    case 0xC3: /* ret */
    case 0xC2: /* ret imm16 */
        if (i->opsize != 8)
            return 0;
        load_reg(e, 8, T3, X86_RSP, 1);
        ldr(e, 8, T0, T3, 0);
        mov_imm(e, T1, 8 + (op == 0xC2 ? (i->imm & 0xFFFF) : 0));
        add_shifted(e, T3, T3, T1, 0);
        str(e, 8, T3, CPU, OFF_R(X86_RSP));
        exit_to(e, T0, 0, x->count);
        return 1;
    case 0xFF: {
        int sub = (i->modrm >> 3) & 7;
        /* near calls and jumps take 64 bit targets in 64 bit code without rex.w. */
        if ((sub != 2 && sub != 4) || !x->cpu->mode64 || i->p66)
            return 0;
        load_rm(x, i, 8, T0);
        if (sub == 2) {
            mov_imm(e, T1, x->next);
            push_value(e, T1);
        }
        exit_to(e, T0, 0, x->count);
        return 1;
    }
    }
    return 0;
}

/* the interpreter runs one instruction, with rip at its start as it expects. */
static void emit_fallback(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    mov_imm(e, T4, x->addr);
    str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    mov_reg(e, 8, 0, CPU);
    mov_imm(e, 1, (uint64_t)(uintptr_t)i);
    call(e, (const void *)x86_exec_insn);
    x->nzcv_alu = -1;
}

/* whether the flags at the end of the block are dead in every place it continues at. */
static int flags_dead_after(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint64_t end)
{
    const X86Insn *last = &insns[count - 1];
    uint64_t lo = rip & ~0xFFFull, hi = ((end - 1) | 0xFFFull) + 1;
    uint64_t next = end;
    if (!x86_ends_block(last))
        return flags_dead_at(cpu, next, lo, hi);
    if (is_jcc(last))
        return 0; /* the branch itself reads them */
    if (last->map == 0 && (last->opcode == 0xE9 || last->opcode == 0xEB))
        return flags_dead_at(cpu, next + last->imm, lo, hi);
    if (last->map == 0 && last->opcode == 0xE8)
        return flags_dead_at(cpu, next + last->imm, lo, hi);
    return 0;
}

uint32_t x86_jit_translate(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint32_t *code,
                           uint32_t capacity, uint16_t *offsets)
{
    Emit e = {code, code + capacity, code, 0};
    uint8_t live[64];
    if (!count || count > 64)
        return 0;
    uint64_t end_addr = rip;
    for (uint32_t k = 0; k < count; k++)
        end_addr += insns[k].len;
    /* liveness of the flags each instruction sets. */
    int flags_live = !flags_dead_after(cpu, rip, insns, count, end_addr);
    for (uint32_t k = count; k-- > 0;) {
        live[k] = (uint8_t)flags_live;
        if (writes_flags(&insns[k]))
            flags_live = 0;
        else if (!ignores_flags(&insns[k]))
            flags_live = 1;
        if (reads_condition(&insns[k]))
            flags_live = 1;
    }
    Ctx x = {&e, cpu, rip, rip, 0, count, 0, -1, 0, 0, NULL};
    prologue(&e);
    uint32_t end = count;
    if (nests(&insns[count - 1]))
        end = count - 1;
    uint64_t addr = rip;
    for (uint32_t k = 0; k < end; k++) {
        const X86Insn *i = &insns[k];
        offsets[k] = (uint16_t)(e.p - code);
        x.addr = addr;
        x.next = addr + i->len;
        x.index = k;
        x.insn = i;
        x.flags_live = live[k];
        if (k == count - 1 && x86_ends_block(i)) {
            if (!emit_branch(&x, i)) {
                emit_fallback(&x, i);
                /* the interpreter set rip to wherever the instruction went. */
                mov_imm(&e, 0, count);
                put(&e, 0xA8C17BF3u);
                put(&e, 0xD65F03C0u);
            }
            offsets[count] = (uint16_t)(e.p - code);
            return e.overflow || e.p - code > 0xFFFF ? 0 : (uint32_t)(e.p - code);
        }
        if (!emit_insn(&x, i))
            emit_fallback(&x, i);
        addr = x.next;
    }
    offsets[end] = (uint16_t)(e.p - code);
    exit_to(&e, -1, addr, end);
    if (end < count)
        offsets[count] = (uint16_t)(e.p - code);
    return e.overflow || e.p - code > 0xFFFF ? 0 : (uint32_t)(e.p - code);
}

uint64_t x86_jit_fault_rip(uint64_t rip, const X86Insn *insns, uint32_t count, const uint16_t *offsets,
                           uint32_t code_offset)
{
    uint64_t a = rip;
    for (uint32_t k = 0; k < count; k++) {
        if (code_offset < offsets[k + 1])
            return a;
        a += insns[k].len;
    }
    return a;
}
