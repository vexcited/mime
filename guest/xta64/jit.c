/*
 * translates x64 and x86 blocks to arm64 code.
 *
 * a translated block is a function taking the cpu. the x86 state stays in the
 * cpu structure: x19 holds the cpu and each instruction loads and stores the
 * registers it uses. instructions the translator does not cover call the
 * interpreter for that one instruction, so any block can be translated. the
 * function returns the index of the first instruction left to interpret, with
 * rip at its address: the block's count when it ran to the end, or the index
 * of a final instruction that may run nested emulation, such as a system call.
 * the block must not return into code that nested emulation may free. an exit
 * to a constant address jumps straight into the block translated there once the
 * dispatcher linked it, see X86ChainSlot.
 *
 * flags stay lazy as in the interpreter: an arithmetic instruction records its
 * operands when a later instruction may read them. a liveness pass drops the
 * records nothing reads, and a conditional branch right after a compare tests
 * the arm64 flags of the compare itself.
 */
#include "internal.h"
#include "jit.h"

#include <stddef.h>

/* x19 holds the cpu, x9 addresses, x10 to x15 and x8 values. x14 is only free between uses of ea. */
enum { CPU = 19, ADDR = 9, T0 = 10, T1 = 11, T2 = 12, T3 = 13, T4 = 14, T5 = 15, S0 = 8, XZR = 31 };
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

/* the register form of an arithmetic or logic instruction. op is the 32 bit encoding. */
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

/*
 * every x86 register lives in a host one while translated code runs: rax to rdi in x20 to x27, which calls keep, and
 * r8 to r15 in x0 to x7, which a call to c code saves and reloads around it. loaded by the prologue, stored by every
 * exit and around calls into the interpreter, and kept through chained exits.
 */
enum { HOST_REGS = 20 };
static int host_reg(int reg) { return reg < 0 ? -1 : reg < 8 ? HOST_REGS + reg : reg - 8; }

/* stp or ldp of two registers at a scaled offset of base. */
static void pair(Emit *e, int store, int rt, int rt2, int rn, uint32_t off)
{
    put(e, (store ? 0xA9000000u : 0xA9400000u) | (off / 8) << 15 | (uint32_t)rt2 << 10 | (uint32_t)rn << 5 | (uint32_t)rt);
}

/* stores or loads r8 to r15, the x86 registers a call may clobber, to or from the cpu. */
static void sync_high(Emit *e, int store)
{
    for (int k = 0; k < 4; k++)
        pair(e, store, 2 * k, 2 * k + 1, CPU, OFF_R(8 + 2 * k));
}

/* stores or loads every host register's x86 value to or from the cpu. */
static void sync_regs(Emit *e, int store)
{
    for (int k = 0; k < 4; k++)
        pair(e, store, HOST_REGS + 2 * k, HOST_REGS + 2 * k + 1, CPU, OFF_R(2 * k));
    sync_high(e, store);
}

static void prologue(Emit *e)
{
    put(e, 0xA9BB7BF3u); /* stp x19, x30, [sp, #-80]! */
    for (int k = 0; k < 4; k++)
        pair(e, 1, HOST_REGS + 2 * k, HOST_REGS + 2 * k + 1, 31, 16 + 16 * (uint32_t)k);
    mov_reg(e, 8, CPU, 0);
    sync_regs(e, 0);
}

/* stores the registers back and returns index to the dispatcher. */
static void epilogue(Emit *e, uint32_t index)
{
    sync_regs(e, 1);
    for (int k = 0; k < 4; k++)
        pair(e, 0, HOST_REGS + 2 * k, HOST_REGS + 2 * k + 1, 31, 16 + 16 * (uint32_t)k);
    put(e, 0xA8C57BF3u); /* ldp x19, x30, [sp], #80 */
    mov_imm(e, 0, index);
    put(e, 0xD65F03C0u); /* ret */
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
    epilogue(e, index);
}


/* ------------------------------------------------------------ translation state */

typedef struct Ctx {
    Emit *e;
    X86Cpu *cpu;
    uint64_t addr, next;
    uint32_t index, count;
    int flags_live; /* the LIVE_ flags a later instruction may read after this one. */
    int flags_lazy; /* the ones read from the lazy flags rather than straight from the arm64 flags. */
    int nzcv_alu;   /* the instruction kind whose arm64 flags are current, -1 for none. */
    int keep;       /* set by an emitter that left the arm64 and x86 flags alone. */
    int nzcv_set;   /* set by an emitter that left arm64 flags describing its x86 ones. */
    const X86Insn *insn;
    uint64_t start; /* the block's address. */
    uint16_t xloaded; /* the xmm registers held in v16 to v31, see the xmm cache. */
    uint16_t xdirty;  /* the ones changed there and not yet stored. */
} Ctx;

static uint32_t OFF_X(int reg) { return (uint32_t)(offsetof(X86Cpu, xmm) + (uint32_t)reg * 16); }

/*
 * the xmm cache: within a block, xmm n lives in v16 + n from its first use on, and the block's exits store the ones it
 * changed. c code called meanwhile may change v16 to v31 and reads the cpu's xmm, so the changed ones are stored
 * before such a call and the held ones loaded again after it. a fault restores the held ones from the faulting
 * context, see x86_jit_fault.
 */
enum { XCACHE = 16 };

static void vldr(Emit *e, int bytes, int vt, int rn, uint32_t off);
static void vstr(Emit *e, int bytes, int vt, int rn, uint32_t off);
static void vmov(Emit *e, int vd, int vn);

static int xget(Ctx *x, int n)
{
    if (!(x->xloaded & 1u << n)) {
        vldr(x->e, 16, XCACHE + n, CPU, OFF_X(n));
        x->xloaded |= (uint16_t)(1u << n);
    }
    return XCACHE + n;
}

/* vt = the low bytes of xmm n, the rest of vt cleared. */
static void xload(Ctx *x, int bytes, int vt, int n)
{
    int r = xget(x, n);
    if (bytes == 16)
        vmov(x->e, vt, r);
    else
        put(x->e, (bytes == 8 ? 0x1E604000u : 0x1E204000u) | (uint32_t)r << 5 | (uint32_t)vt); /* fmov d or s */
}

static void xstore(Ctx *x, int vt, int n)
{
    vmov(x->e, XCACHE + n, vt);
    x->xloaded |= (uint16_t)(1u << n);
    x->xdirty |= (uint16_t)(1u << n);
}

/* stores the changed registers. a path that may not run keeps them marked: storing again later does no harm. */
static void xstore_dirty(Ctx *x)
{
    for (int n = 0; n < 16; n++)
        if (x->xdirty & 1u << n)
            vstr(x->e, 16, XCACHE + n, CPU, OFF_X(n));
}

static void xreload(Ctx *x)
{
    for (int n = 0; n < 16; n++)
        if (x->xloaded & 1u << n)
            vldr(x->e, 16, XCACHE + n, CPU, OFF_X(n));
}

/* around a call to c code, which keeps none of x0 to x7 and v16 to v31. */
static void call_begin(Ctx *x)
{
    sync_high(x->e, 1);
    xstore_dirty(x);
}

static void call_end(Ctx *x)
{
    sync_high(x->e, 0);
    xreload(x);
}

static void emit_fallback(Ctx *x, const X86Insn *i);
static void add_imm(Emit *e, int size, int rd, int rn, int64_t imm);

static uint32_t *cbz_x(Emit *e, int rt)
{
    uint32_t *at = e->p;
    put(e, 0xB4000000u | (uint32_t)rt);
    return at;
}

/* the conditions to run on into another block: no suspension pending and code unchanged, else the branches out. */
static void chain_checks(Ctx *x, uint32_t **suspended, uint32_t **stale)
{
    Emit *e = x->e;
    ldr(e, 8, 17, CPU, offsetof(X86Cpu, suspend_pending));
    uint32_t *no_suspend = cbz_x(e, 17);
    ldr(e, 4, 17, 17, 0);
    *suspended = cbnz_w(e, 17);
    patch19(e, no_suspend);
    ldr(e, 8, 17, CPU, offsetof(X86Cpu, code_epoch));
    uint32_t *no_epoch = cbz_x(e, 17);
    ldr(e, 4, 17, 17, 0);
    ldr(e, 4, T5, CPU, offsetof(X86Cpu, blocks_epoch));
    put(e, 0x6B00001Fu | (uint32_t)T5 << 16 | 17u << 5); /* cmp w17, w15 */
    *stale = b_cond(e, NE);
    patch19(e, no_epoch);
}

/*
 * leaves for an address in rip_reg, or in the cpu's rip when rip_reg is -1: straight into the block translated there
 * when the block table has one, otherwise back to the dispatcher. x16, x17 and t1 to t5 are free at an exit.
 */
static void exit_computed(Ctx *x, int rip_reg)
{
    Emit *e = x->e;
    int stored = rip_reg < 0;
    if (stored) {
        ldr(e, 8, T0, CPU, offsetof(X86Cpu, rip));
        rip_reg = T0;
    }
    ldr(e, 8, 17, CPU, offsetof(X86Cpu, jit_slots));
    ldr(e, 8, T5, CPU, offsetof(X86Cpu, jit_mask));
    uint32_t *no_table = cbz_x(e, 17);
    put(e, 0xCA400000u | (uint32_t)rip_reg << 16 | 4u << 10 | (uint32_t)rip_reg << 5 | T1);  /* eor t1, rip, rip, lsr #4 */
    put(e, 0xCA400000u | (uint32_t)rip_reg << 16 | 15u << 10 | (uint32_t)T1 << 5 | T1);      /* eor t1, t1, rip, lsr #15 */
    rrr(e, OP_AND, 8, T1, T1, T5);
    uint32_t *probe = e->p;
    add_shifted(e, T2, 17, T1, 5);
    put(e, 0xA9400000u | 16u << 10 | (uint32_t)T2 << 5 | T3); /* ldp t3, x16, [t2]: the slot's rip and code */
    put(e, 0xEB00001Fu | (uint32_t)rip_reg << 16 | (uint32_t)T3 << 5); /* cmp t3, rip */
    uint32_t *other = b_cond(e, NE);
    uint32_t *untranslated = cbz_x(e, 16);
    uint32_t *suspended, *stale;
    chain_checks(x, &suspended, &stale);
    ldr(e, 8, T4, T2, offsetof(X86Slot, block));
    str(e, 8, T4, CPU, offsetof(X86Cpu, jit_block));
    put(e, 0xD61F0200u); /* br x16 */
    /* another address in the slot: the next one, unless this one is free. */
    patch19(e, other);
    ldr(e, 8, T4, T2, offsetof(X86Slot, block));
    uint32_t *free_slot = cbz_x(e, T4);
    add_imm(e, 8, T1, T1, 1);
    rrr(e, OP_AND, 8, T1, T1, T5);
    put(e, 0x14000000u | ((uint32_t)(probe - e->p) & 0x3FFFFFFu)); /* b probe */
    patch19(e, no_table);
    patch19(e, free_slot);
    patch19(e, untranslated);
    patch19(e, suspended);
    patch19(e, stale);
    if (!stored)
        str(e, 8, rip_reg, CPU, offsetof(X86Cpu, rip));
    epilogue(e, x->count);
}

/*
 * leaves for a constant address: straight into the translated block there once the dispatcher linked the slot,
 * otherwise back to the dispatcher with the slot noted for linking. x16 and x17 are free at an exit. every loop of
 * chained blocks has an exit to a block at or before its own, so only those check for suspension and stale code.
 */
static void exit_const(Ctx *x, uint64_t rip, uint32_t index)
{
    Emit *e = x->e;
    if (index != x->count) {
        exit_to(e, -1, rip, index);
        return;
    }
    uint32_t *load_code = e->p;
    put(e, 0x58000000u | 16); /* ldr x16, slot.code */
    uint32_t *unlinked = cbz_x(e, 16);
    uint32_t *suspended = nullptr, *stale = nullptr;
    if (rip <= x->start)
        chain_checks(x, &suspended, &stale);
    uint32_t *load_block = e->p;
    put(e, 0x58000000u | 17); /* ldr x17, slot.block */
    str(e, 8, 17, CPU, offsetof(X86Cpu, jit_block));
    put(e, 0xD61F0200u); /* br x16 */
    patch19(e, unlinked);
    patch19(e, suspended);
    patch19(e, stale);
    mov_imm(e, T4, rip);
    str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    uint32_t *address_slot = e->p;
    put(e, 0x10000000u | 17); /* adr x17, slot */
    str(e, 8, 17, CPU, offsetof(X86Cpu, exit_slot));
    epilogue(e, index);
    if ((e->p - e->code) & 1)
        put(e, 0xD503201Fu); /* nop: the slot's words are 8 byte aligned. */
    patch19(e, load_code);
    patch19(e, address_slot);
    put(e, 0);
    put(e, 0);
    patch19(e, load_block);
    put(e, 0);
    put(e, 0);
    put(e, (uint32_t)rip);
    put(e, (uint32_t)(rip >> 32));
}

/* the stack word: 8 bytes in 64-bit code, 4 in 32-bit code, whose stack and branch targets wrap at 4 GiB. */
static int stack_word(const Ctx *x) { return x->cpu->mode64 ? 8 : 4; }

static uint64_t branch_target(const Ctx *x, uint64_t target) { return x->cpu->mode64 ? target : (uint32_t)target; }

static void set_nzcv(Ctx *x, int kind)
{
    x->nzcv_alu = kind;
    x->nzcv_set = 1;
}

/* ------------------------------------------------------------ operands */

static uint64_t size_mask(int size) { return size == 8 ? ~0ull : (1ull << (size * 8)) - 1; }

/* rd = rn + imm at size, with t4 for immediates past 24 bits. rd and rn are never 31, the stack pointer here. */
static void add_imm(Emit *e, int size, int rd, int rn, int64_t imm)
{
    uint64_t m = imm < 0 ? -(uint64_t)imm : (uint64_t)imm;
    uint32_t op = (imm < 0 ? 0x51000000u : 0x11000000u) | sf(size);
    if (m >= 1u << 24) {
        mov_imm(e, T4, (uint64_t)imm);
        rrr(e, OP_ADD, size, rd, rn, T4);
    } else if (m >= 4096) {
        put(e, op | 1u << 22 | (uint32_t)(m >> 12) << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
        if (m & 0xFFF)
            put(e, op | (uint32_t)(m & 0xFFF) << 10 | (uint32_t)rd << 5 | (uint32_t)rd);
    } else {
        put(e, op | (uint32_t)m << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
    }
}

/*
 * computes the memory operand's address into ADDR. 32-bit addresses are
 * computed with 32-bit operations, which wrap and zero extend like x86.
 */
static void ea(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int size = i->addrsize == 4 ? 4 : 8;
    int64_t disp = i->disp;
    int base = -1, index = -1;
    if (i->base == -2) {
        mov_imm(e, ADDR, (x->next + (uint64_t)disp) & size_mask(size));
        base = ADDR;
        disp = 0;
    } else if (i->base >= 0) {
        base = host_reg(i->base);
        if (base < 0) {
            ldr(e, 8, ADDR, CPU, OFF_R(i->base));
            base = ADDR;
        }
    }
    if (i->index >= 0) {
        index = host_reg(i->index);
        if (index < 0) {
            ldr(e, 8, T4, CPU, OFF_R(i->index));
            index = T4;
        }
    }
    if (base < 0 && index < 0) {
        mov_imm(e, ADDR, (uint64_t)disp & size_mask(size));
    } else if (index >= 0) {
        rrr(e, OP_ADD | (uint32_t)i->scale << 10, size, ADDR, base < 0 ? XZR : base, index); /* add rd, rn, rm, lsl #scale */
        if (disp)
            add_imm(e, size, ADDR, ADDR, disp);
    } else if (disp) {
        add_imm(e, size, ADDR, base, disp);
    } else if (base != ADDR || size == 4) {
        mov_reg(e, size, ADDR, base);
    }
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
        ubfm(e, 8, rt, host_reg(reg - 4), 8, 15);
    } else if (host_reg(reg) >= 0) {
        int h = host_reg(reg);
        if (size >= 4)
            mov_reg(e, size, rt, h);
        else
            ubfm(e, 8, rt, h, 0, size * 8 - 1);
    } else {
        ldr(e, size, rt, CPU, OFF_R(reg));
    }
}

/* like x86: 32 bit results zero extend, as rt already is, narrower ones merge. */
static void store_reg(Emit *e, int size, int rt, int reg, int rex)
{
    if (high_byte(size, reg, rex)) {
        put(e, 0xB3780000u | 7u << 10 | (uint32_t)rt << 5 | (uint32_t)host_reg(reg - 4)); /* bfi h, rt, #8, #8 */
    } else if (host_reg(reg) >= 0) {
        int h = host_reg(reg);
        if (size >= 4)
            mov_reg(e, size, h, rt);
        else
            put(e, 0xB3400000u | (uint32_t)(size * 8 - 1) << 10 | (uint32_t)rt << 5 | (uint32_t)h); /* bfxil h, rt, #0 */
    } else {
        str(e, size < 4 ? size : 8, rt, CPU, OFF_R(reg));
    }
}

/* the memory form leaves the address in ADDR. */
static void load_rm(Ctx *x, const X86Insn *i, int size, int rt)
{
    if (i->mod == 3) {
        load_reg(x->e, size, rt, i->rm, i->rex);
    } else {
        ea(x, i);
        ldr(x->e, size, rt, ADDR, 0);
    }
}

/* with reuse, ADDR still holds the address from a previous load_rm. */
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
    ALU_TEST, ALU_UCOMI, ALU_INCDEC, ALU_SHIFT,
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

/* adc or sbb the translator emits, or -1. */
static int carry_op(const X86Insn *i)
{
    if (i->map != 0 || i->lock)
        return -1;
    uint8_t op = i->opcode;
    int kind = -1;
    if (op < 0x40 && (op & 7) <= 5)
        kind = op >> 3;
    else if (op == 0x80 || op == 0x81 || op == 0x83)
        kind = (i->modrm >> 3) & 7;
    return kind == ALU_ADC || kind == ALU_SBB ? kind : -1;
}

/* the even one byte opcodes work on bytes. */
static int alu_size(const X86Insn *i)
{
    uint8_t op = i->opcode;
    if (op < 0x40)
        return (op & 1) ? i->opsize : 1;
    return op == 0x80 || op == 0x84 || op == 0xA8 || op == 0xF6 ? 1 : i->opsize;
}

static int is_jcc(const X86Insn *i) { return x86_is_jcc(i); }

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
               (op == 0xFF && ((i->modrm >> 3) & 7) >= 2) || ((op == 0xF6 || op == 0xF7) && ((i->modrm >> 3) & 7) == 2) ||
               (op >= 0xA0 && op <= 0xA3) || op == 0xC9;
    if (i->map == 1)
        return op == 0x1F || op == 0x18 || op == 0x0D || op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF || (op >= 0x10 && op <= 0x17) ||
               (op >= 0x28 && op <= 0x2D) || (op >= 0x50 && op <= 0x7F) || op == 0xC2 || op == 0xC6 || op >= 0xC8;
    return 1; /* the 0f 38 and 0f 3a maps. */
}

/* reads flags as a condition: jcc, setcc, cmovcc. */
static int reads_condition(const X86Insn *i)
{
    return is_jcc(i) || (i->map == 1 && ((i->opcode >= 0x90 && i->opcode <= 0x9F) || (i->opcode >= 0x40 && i->opcode <= 0x4F)));
}

/* instructions the block must return before running, see the top of the file. */
static int nests(const X86Insn *i)
{
    return (i->map == 1 && (i->opcode == 0x05 || i->opcode == 0x34)) || (i->map == 0 && i->opcode == 0xCD);
}

/* flags liveness tracks cf apart from the rest: inc and dec keep cf, and rotates set only cf and of. */
enum { LIVE_CF = 1, LIVE_REST = 2, LIVE_ALL = 3 };

/* the flags live before an instruction, from the ones live after it. */
static int live_before(const X86Insn *i, int live)
{
    if (reads_condition(i)) {
        int cc = i->opcode & 14;
        return live | (cc == 2 ? LIVE_CF : cc == 6 ? LIVE_ALL : LIVE_REST);
    }
    if (writes_flags(i))
        return 0;
    if (carry_op(i) >= 0)
        return LIVE_CF;
    uint8_t op = i->opcode;
    int sub = (i->modrm >> 3) & 7;
    if (i->map == 0 && ((op >= 0x40 && op <= 0x4F) || ((op == 0xFE || op == 0xFF) && sub < 2)))
        return live & LIVE_CF;
    if (i->map == 0 && op >= 0xC0 && op <= 0xD1 && (op <= 0xC1 || op >= 0xD0)) {
        unsigned n = (op <= 0xC1 ? (unsigned)(i->imm & 0xFF) : 1) & ((op & 1) && i->opsize == 8 ? 63 : 31);
        if (n && sub < 2)
            return live & LIVE_REST;
        if (n && sub >= 4)
            return 0;
    }
    return ignores_flags(i) ? live : LIVE_ALL;
}

/*
 * the flags the code at addr may read before setting them. only the pages
 * of the block are read, which are known to be mapped.
 */
static int flags_read_from(X86Cpu *cpu, uint64_t addr, uint64_t lo, uint64_t hi, int depth)
{
    int ours = LIVE_ALL, read = 0;
    for (int n = 0; n < 8 && ours; n++) {
        /* the environment reads them where the emulation returns. */
        if (addr < lo || addr + 15 > hi || addr == cpu->exit_rip)
            break;
        X86Insn i;
        if (!x86_decode(&i, (const uint8_t *)(uintptr_t)addr, addr, (int)cpu->mode64))
            break;
        read |= live_before(&i, 0) & ours;
        ours &= live_before(&i, ours);
        if (x86_ends_block(&i)) {
            /* a branch or a jump goes on where it leads, a few steps deep: a compare there ends the flags' life. */
            uint64_t next = addr + i.len, target = cpu->mode64 ? next + i.imm : (uint32_t)(next + i.imm);
            if (depth > 0 && ours && is_jcc(&i))
                return read | (ours & (flags_read_from(cpu, next, lo, hi, depth - 1) |
                                       flags_read_from(cpu, target, lo, hi, depth - 1)));
            if (depth > 0 && ours && i.map == 0 && (i.opcode == 0xE9 || i.opcode == 0xEB))
                return read | (ours & flags_read_from(cpu, target, lo, hi, depth - 1));
            break;
        }
        addr += i.len;
    }
    return read | ours;
}

static int flags_read_at(X86Cpu *cpu, uint64_t addr, uint64_t lo, uint64_t hi)
{
    return flags_read_from(cpu, addr, lo, hi, 2);
}

/* ------------------------------------------------------------ flags */

/* records the lazy flags of an alu operation from operands ra and rb, its result rr and for adc and sbb the carry in T3. */
static void record_flags(Ctx *x, int kind, int size, int ra, int rb, int rr)
{
    Emit *e = x->e;
    int lf = kind == ALU_ADD ? X86_LF_ADD : kind == ALU_SUB || kind == ALU_CMP ? X86_LF_SUB : X86_LF_LOGIC;
    if (kind == ALU_ADC || kind == ALU_SBB)
        lf = kind == ALU_ADC ? X86_LF_ADC : X86_LF_SBB;
    mov_imm(e, T4, X86_LF(lf, log2_size(size)));
    str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
    /* the flags of logic operations only need the result. */
    if (lf != X86_LF_LOGIC) {
        str(e, 8, ra, CPU, offsetof(X86Cpu, lf_a));
        str(e, 8, rb, CPU, offsetof(X86Cpu, lf_b));
    }
    str(e, 8, rr, CPU, offsetof(X86Cpu, lf_res));
    if (kind == ALU_ADC || kind == ALU_SBB)
        str(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
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
    if (x->flags_lazy)
        record_flags(x, kind, size, T0, T1, T2);
    set_nzcv(x, kind);
}

static void carry_t3(Ctx *x);

/* a register operand: its host register, or tmp loaded from the cpu. */
static int reg_operand(Emit *e, int size, int reg, int tmp)
{
    if (host_reg(reg) >= 0)
        return host_reg(reg);
    ldr(e, size, tmp, CPU, OFF_R(reg));
    return tmp;
}

/* alu operations on 4 and 8 bytes, straight on the host registers. */
static int emit_alu_wide(Ctx *x, const X86Insn *i, int kind, int size)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int carry = kind == ALU_ADC || kind == ALU_SBB, compare = kind == ALU_CMP || kind == ALU_TEST;
    int record = x->flags_lazy != 0;
    if (carry)
        carry_t3(x);
    /* a is the operand written, or the first compared. b is the other: a register, or the immediate. */
    int a_rm, b_rm = 0, b_reg = -1, has_imm = 0;
    if (op >= 0x80) {
        a_rm = !(op == 0xA8 || op == 0xA9);
        if (op == 0x84 || op == 0x85)
            b_reg = i->reg;
        else
            has_imm = 1;
    } else if ((op & 7) >= 4) {
        a_rm = 0;
        has_imm = 1;
    } else if ((op & 7) <= 1) {
        a_rm = 1;
        b_reg = i->reg;
    } else {
        a_rm = 0;
        b_rm = 1;
    }
    int a_reg = a_rm ? (i->mod == 3 ? i->rm : -1) : ((op & 7) >= 4 && op < 0x80) || op == 0xA8 || op == 0xA9 ? X86_RAX : i->reg;
    int ra, rb;
    if (a_reg >= 0) {
        ra = reg_operand(e, size, a_reg, T0);
    } else {
        ea(x, i);
        ldr(e, size, T0, ADDR, 0);
        ra = T0;
    }
    uint64_t imm = i->imm & size_mask(size);
    /*
     * a small negative immediate is the other operation, whose arm64 c flag is then the x86 carry of this one: an add of
     * -k carries when the subtraction of k does not borrow. so the flags still read as this operation's.
     */
    int flag_kind = kind;
    int imm12 = -1; /* the add or sub immediate form, when the flags allow it. */
    if (has_imm && !record && !carry && (kind == ALU_ADD || kind == ALU_SUB || kind == ALU_CMP)) {
        if (imm < 4096)
            imm12 = (int)imm;
        else if (kind != ALU_CMP && size_mask(size) - imm < 4095) {
            imm12 = (int)(size_mask(size) - imm + 1);
            kind = kind == ALU_ADD ? ALU_SUB : ALU_ADD;
        }
    } else if (has_imm && record && (kind == ALU_ADD || kind == ALU_SUB || kind == ALU_CMP) && imm < 4096) {
        imm12 = (int)imm;
    }
    if (imm12 >= 0) {
        rb = -1;
        if (record) {
            mov_imm(e, T1, imm);
            rb = T1;
        }
    } else if (has_imm) {
        mov_imm(e, T1, imm);
        rb = T1;
    } else if (b_rm && i->mod != 3) {
        ea(x, i);
        ldr(e, size, T1, ADDR, 0);
        rb = T1;
    } else {
        rb = reg_operand(e, size, b_rm ? i->rm : b_reg, T1);
    }
    /* the result goes straight to a host register, unless the lazy flags need the operands after it. */
    int rd = a_reg >= 0 && host_reg(a_reg) >= 0 && !record ? host_reg(a_reg) : T2;
    if (compare && !record)
        rd = XZR;
    int set = x->flags_live != 0;
    if (compare && !set)
        return 1;
    if (carry) {
        /* the arm64 c flag carries in for adcs and borrows in, inverted, for sbcs. */
        if (kind == ALU_ADC)
            put(e, 0x71000400u | (uint32_t)T3 << 5 | XZR); /* cmp w3, #1 */
        else
            put(e, 0x6B000000u | (uint32_t)T3 << 16 | (uint32_t)XZR << 5 | XZR); /* cmp wzr, w3 */
        rrr(e, kind == ALU_ADC ? 0x3A000000u : 0x7A000000u, size, rd, ra, rb);
    } else if (imm12 >= 0) {
        uint32_t base = kind == ALU_ADD ? (set ? 0x31000000u : 0x11000000u) : (set ? 0x71000000u : 0x51000000u);
        put(e, base | sf(size) | (uint32_t)imm12 << 10 | (uint32_t)ra << 5 | (uint32_t)rd);
    } else {
        static const uint32_t ops[] = {OP_ADD, OP_ORR, 0, 0, OP_AND, OP_SUB, OP_EOR, OP_SUB, OP_AND};
        static const uint32_t sops[] = {OP_ADDS, OP_ORR, 0, 0, OP_ANDS, OP_SUBS, OP_EOR, OP_SUBS, OP_ANDS};
        rrr(e, set ? sops[kind] : ops[kind], size, rd, ra, rb);
        if (set && (kind == ALU_OR || kind == ALU_XOR))
            rrr(e, OP_ANDS, size, XZR, rd, rd);
    }
    if (record)
        record_flags(x, kind, size, ra, rb, T2);
    if (set)
        set_nzcv(x, flag_kind == ALU_ADC ? ALU_ADD : flag_kind == ALU_SBB ? ALU_SUB : flag_kind);
    if (compare)
        return 1;
    if (a_reg < 0)
        str(e, size, rd, ADDR, 0);
    else if (rd != host_reg(a_reg))
        store_reg(e, size, rd, a_reg, i->rex);
    return 1;
}

static int emit_alu(Ctx *x, const X86Insn *i, int kind)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = alu_size(i);
    uint64_t mask = size_mask(size);
    int dst; /* 1 for the modrm operand, 0 for the reg one, -1 for the accumulator. */
    if (size >= 4)
        return emit_alu_wide(x, i, kind, size);
    if (kind == ALU_ADC || kind == ALU_SBB)
        return 0;
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

/* the arm64 condition for x86 condition cc after an instruction kind. -1 none, -2 never, -3 always. */
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
    /* after a shift, the arm64 flags only hold zf and sf. */
    static const int shift[16] = {-1, -1, -1, -1, EQ, NE, -1, -1, MI, PL, -1, -1, -1, -1, -1, -1};
    switch (kind) {
    case ALU_SUB: case ALU_CMP: return sub[cc];
    case ALU_ADD: return add[cc];
    case ALU_OR: case ALU_AND: case ALU_XOR: case ALU_TEST: return logic[cc];
    case ALU_UCOMI: return ucomi[cc];
    case ALU_INCDEC: return incdec[cc];
    case ALU_SHIFT: return shift[cc];
    }
    return -1;
}

static void cset(Emit *e, int rd, int cond)
{
    put(e, 0x1A9F07E0u | (uint32_t)(cond ^ 1) << 12 | (uint32_t)rd);
}

/* t3 = cf, from the arm64 flags when they describe it or from the lazy flags. */
static void carry_t3(Ctx *x)
{
    Emit *e = x->e;
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, 2) : -1;
    if (cond >= 0) {
        cset(e, T3, cond);
    } else if (cond == -2 || cond == -3) {
        mov_imm(e, T3, cond == -3);
    } else if (x->nzcv_alu == ALU_INCDEC) {
        /* the inc or dec before kept cf, so it recorded it. */
        ldr(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
    } else {
        /* the lazy flags of an inc or dec hold cf as it is. */
        ldr(e, 4, T3, CPU, offsetof(X86Cpu, lf_op));
        put(e, 0x51000000u | X86_LF(X86_LF_INC, 0) << 10 | (uint32_t)T3 << 5 | T3); /* sub w13, w13, #inc */
        put(e, 0x71000000u | 8u << 10 | (uint32_t)T3 << 5 | XZR);                    /* cmp w13, #8 */
        uint32_t *other = b_cond(e, HS);
        ldr(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
        uint32_t *done = b_always(e);
        patch19(e, other);
        call_begin(x);
        mov_reg(e, 8, 0, CPU);
        call(e, (const void *)x86_arith_flags);
        ubfm(e, 4, T3, 0, 0, 0); /* cf, bit 0 */
        call_end(x);
        patch26(e, done);
    }
}

/*
 * t3 = x86 condition cc, from the arm64 flags when they describe it or from
 * the lazy flags. returns whether the arm64 flags survived.
 */
static int condition_t3(Ctx *x, int cc)
{
    Emit *e = x->e;
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
    if (cond >= 0) {
        cset(e, T3, cond);
        return 1;
    }
    if (cond == -2 || cond == -3) {
        mov_imm(e, T3, cond == -3);
        return 1;
    }
    call_begin(x);
    mov_reg(e, 8, 0, CPU);
    mov_imm(e, 1, (uint64_t)cc);
    call(e, (const void *)x86_cond);
    mov_reg(e, 4, T3, 0);
    call_end(x);
    return 0;
}

static void emit_jcc(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int cc = i->opcode & 15;
    uint64_t target = branch_target(x, x->next + i->imm);
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
    if (cond == -3) {
        exit_const(x, target, x->count);
        return;
    }
    if (cond == -2) {
        exit_const(x, x->next, x->count);
        return;
    }
    uint32_t *taken;
    if (cond >= 0) {
        taken = b_cond(e, cond);
    } else {
        condition_t3(x, cc);
        taken = cbnz_w(e, T3);
    }
    exit_const(x, x->next, x->count);
    patch19(e, taken);
    exit_const(x, target, x->count);
}

/* a conditional branch before the block's end: the branch taken leaves the block, the code after it runs on. */
static int emit_side_exit(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int cc = i->opcode & 15;
    uint64_t target = branch_target(x, x->next + i->imm);
    int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
    x->keep = 1;
    if (cond == -2)
        return 1;
    uint32_t *stay = nullptr;
    if (cond >= 0)
        stay = b_cond(e, cond ^ 1);
    else if (cond == -1) {
        x->keep = condition_t3(x, cc);
        stay = cbz_x(e, T3);
    }
    /* the arm64 flags still describe the branch's condition on the way out, which the stores leave alone. */
    xstore_dirty(x);
    exit_const(x, target, x->count);
    patch19(e, stay);
    return 1;
}

/* inc and dec keep the carry flag, read before their own flags replace the arm64 ones. */
static int emit_incdec(Ctx *x, const X86Insn *i, int size, int dec)
{
    Emit *e = x->e;
    int lf = X86_LF(dec ? X86_LF_DEC : X86_LF_INC, log2_size(size));
    if (x->flags_lazy & LIVE_CF)
        carry_t3(x);
    if (size >= 4 && i->mod == 3 && host_reg(i->rm) >= 0) {
        int h = host_reg(i->rm);
        uint32_t op = dec ? (x->flags_live ? 0x71000400u : 0x51000400u) : (x->flags_live ? 0x31000400u : 0x11000400u);
        put(e, op | sf(size) | (uint32_t)h << 5 | (uint32_t)h); /* adds or subs h, h, #1 */
        if (x->flags_lazy) {
            /* the flags of inc and dec only need the result and the carry kept. */
            mov_imm(e, T4, lf);
            str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
            str(e, 8, h, CPU, offsetof(X86Cpu, lf_res));
            if (x->flags_lazy & LIVE_CF)
                str(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
        }
        if (x->flags_live)
            set_nzcv(x, ALU_INCDEC);
        return 1;
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
    if (x->flags_lazy) {
        mov_imm(e, T4, lf);
        str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
        str(e, 8, T2, CPU, offsetof(X86Cpu, lf_res));
        if (x->flags_lazy & LIVE_CF)
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

/* shifts and rotates whose flags nothing reads, and shifts by a constant with lazy flags. */
static int emit_shift(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = (op & 1) ? i->opsize : 1, sub = (i->modrm >> 3) & 7;
    if ((x->flags_live && (sub < 4 || op >= 0xD2)) || sub == 2 || sub == 3 || (sub < 2 && size < 4)) {
        /* the interpreter's shift, without its decoding: flags read later stay exact. */
        load_rm(x, i, size, T0);
        if (op <= 0xC1)
            mov_imm(e, T1, i->imm & 0xFF);
        else if (op <= 0xD1)
            mov_imm(e, T1, 1);
        else
            load_reg(e, 1, T1, X86_RCX, 1);
        call_begin(x);
        mov_reg(e, 8, 2, T0);
        mov_reg(e, 8, 3, T1);
        mov_reg(e, 8, 0, CPU);
        mov_imm(e, 1, (uint64_t)sub << 8 | (uint64_t)size);
        call(e, (const void *)x86_jit_shift);
        mov_reg(e, 8, T2, 0);
        call_end(x);
        store_rm(x, i, size, T2, 0);
        return 1;
    }
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
        if (x->flags_live) {
            /* only shifts get here with live flags. */
            static const int lf[] = {X86_LF_SHL, X86_LF_SHR, X86_LF_SHL, X86_LF_SAR};
            if (x->flags_lazy) {
                mov_imm(e, T4, X86_LF(lf[sub - 4], log2_size(size)));
                str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
                str(e, 8, T0, CPU, offsetof(X86Cpu, lf_a));
                mov_imm(e, T1, n);
                str(e, 8, T1, CPU, offsetof(X86Cpu, lf_b));
                str(e, 8, T2, CPU, offsetof(X86Cpu, lf_res));
            }
            if (size >= 4) {
                rrr(e, OP_ANDS, size, XZR, T2, T2);
            } else {
                ubfm(e, 4, S0, T2, size * 8, size * 8 - 1); /* lsl s0, t2, #32 - bits */
                rrr(e, OP_ANDS, 4, XZR, S0, S0);
            }
            store_rm(x, i, size, T2, 1);
            set_nzcv(x, ALU_SHIFT);
            return 1;
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

/* sub or add of the stack word to t3, in the stack pointer's width. */
static void move_stack(Ctx *x, int add)
{
    int w = stack_word(x);
    put(x->e, (add ? (w == 8 ? 0x91000000u : 0x11000000u) : (w == 8 ? 0xD1000000u : 0x51000000u)) |
                  (uint32_t)w << 10 | (uint32_t)T3 << 5 | (uint32_t)T3);
}

/* the stack pointer moves once the store did not fault. */
static void push_value(Ctx *x, int value)
{
    Emit *e = x->e;
    int w = stack_word(x);
    add_imm(e, w, T3, host_reg(X86_RSP), -w);
    str(e, w, value, T3, 0);
    mov_reg(e, 8, host_reg(X86_RSP), T3);
}

/* ------------------------------------------------------------ sse */

enum { V0 = 0, V1 = 1, V2 = 2, V3 = 3 };


/* vector loads and stores of 4, 8 or 16 bytes. narrower loads clear the rest of the register. */
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

/* ins vd.lane[dst], vn.lane[src] for lanes of 2, 4 or 8 bytes. */
static void ins(Emit *e, int bytes, int vd, int dst, int vn, int src)
{
    uint32_t imm5 = bytes == 8 ? ((uint32_t)dst << 4 | 8) : bytes == 4 ? ((uint32_t)dst << 3 | 4) : ((uint32_t)dst << 2 | 2);
    uint32_t imm4 = bytes == 8 ? (uint32_t)src << 3 : bytes == 4 ? (uint32_t)src << 2 : (uint32_t)src << 1;
    put(e, 0x6E000400u | imm5 << 16 | imm4 << 11 | (uint32_t)vn << 5 | (uint32_t)vd);
}

#define V_AND 0x4E201C00u
#define V_BIC 0x4E601C00u
#define V_ORR 0x4EA01C00u
#define V_EOR 0x6E201C00u
#define V_BSL 0x6E601C00u

static void vmov(Emit *e, int vd, int vn) { v3(e, V_ORR, vd, vn, vn); }

/* loads the xmm or memory source operand into vt. */
static void load_xsrc(Ctx *x, const X86Insn *i, int bytes, int vt)
{
    if (i->mod == 3) {
        xload(x, 16, vt, i->rm);
    } else {
        ea(x, i);
        vldr(x->e, bytes, vt, ADDR, 0);
    }
}

/* a nan result goes through the interpreter, which applies the x86 nan rules. cond holds when there is none. */
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

/*
 * the integer operations of sse2 on xmm registers, each one or a few neon instructions on the whole register.
 * returns -1 for the ones it does not cover.
 */
#define N_ADD 0x4E208400u
#define N_SUB 0x6E208400u
#define N_SQADD 0x4E200C00u
#define N_UQADD 0x6E200C00u
#define N_SQSUB 0x4E202C00u
#define N_UQSUB 0x6E202C00u
#define N_CMGT 0x4E203400u
#define N_CMEQ 0x6E208C00u
#define N_SMAX 0x4E206400u
#define N_SMIN 0x4E206C00u
#define N_UMAX 0x6E206400u
#define N_UMIN 0x6E206C00u
#define N_URHADD 0x6E201400u
#define N_MUL 0x4E209C00u
#define N_ZIP1 0x4E003800u
#define N_ZIP2 0x4E007800u
#define N_UZP2 0x4E005800u
#define N_ADDP 0x4E20BC00u
#define N_USHL 0x6E204400u
#define N_SSHL 0x4E204400u
#define N_SMULL 0x0E20C000u
#define N_SMULL2 0x4E20C000u
#define N_UMULL 0x2E20C000u
#define N_UMULL2 0x6E20C000u

static void vzero(Emit *e, int vd) { put(e, 0x6F00E400u | (uint32_t)vd); } /* movi vd.2d, #0 */

/* vd = op(vn, vm) on lanes of 1 << size bytes. */
static void vop(Emit *e, uint32_t op, int size, int vd, int vn, int vm) { v3(e, op | (uint32_t)size << 22, vd, vn, vm); }

/* lanes of 2, 4 or 8 bytes shifted by a constant: kind 0 logical right, 1 arithmetic right, 2 left. */
static void shift_const(Emit *e, int v, int bytes, int kind, unsigned n)
{
    unsigned bits = (unsigned)bytes * 8;
    if (kind == 1 && n >= bits)
        n = bits;
    if (!n)
        return;
    if (kind != 1 && n >= bits) {
        vzero(e, v);
        return;
    }
    if (kind == 2)
        put(e, 0x4F005400u | (bits + n) << 16 | (uint32_t)v << 5 | (uint32_t)v); /* shl */
    else
        put(e, (kind ? 0x4F000400u : 0x6F000400u) | (2 * bits - n) << 16 | (uint32_t)v << 5 | (uint32_t)v); /* sshr, ushr */
}

static int emit_simd_int(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    static const struct {
        uint8_t op;
        uint8_t size;
        uint32_t neon;
    } same[] = {
        {0xFC, 0, N_ADD}, {0xFD, 1, N_ADD}, {0xFE, 2, N_ADD}, {0xD4, 3, N_ADD},
        {0xF8, 0, N_SUB}, {0xF9, 1, N_SUB}, {0xFA, 2, N_SUB}, {0xFB, 3, N_SUB},
        {0xEC, 0, N_SQADD}, {0xED, 1, N_SQADD}, {0xDC, 0, N_UQADD}, {0xDD, 1, N_UQADD},
        {0xE8, 0, N_SQSUB}, {0xE9, 1, N_SQSUB}, {0xD8, 0, N_UQSUB}, {0xD9, 1, N_UQSUB},
        {0x64, 0, N_CMGT}, {0x65, 1, N_CMGT}, {0x66, 2, N_CMGT},
        {0x74, 0, N_CMEQ}, {0x75, 1, N_CMEQ}, {0x76, 2, N_CMEQ},
        {0xDA, 0, N_UMIN}, {0xDE, 0, N_UMAX}, {0xEA, 1, N_SMIN}, {0xEE, 1, N_SMAX},
        {0xE0, 0, N_URHADD}, {0xE3, 1, N_URHADD}, {0xD5, 1, N_MUL},
        {0x60, 0, N_ZIP1}, {0x61, 1, N_ZIP1}, {0x62, 2, N_ZIP1}, {0x6C, 3, N_ZIP1},
        {0x68, 0, N_ZIP2}, {0x69, 1, N_ZIP2}, {0x6A, 2, N_ZIP2}, {0x6D, 3, N_ZIP2},
    };
    for (uint32_t k = 0; k < sizeof(same) / sizeof(same[0]); k++) {
        if (same[k].op != op)
            continue;
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        vop(e, same[k].neon, same[k].size, V0, V0, V1);
        xstore(x, V0, i->reg);
        return 1;
    }
    switch (op) {
    case 0x63: case 0x67: case 0x6B: { /* packsswb, packuswb, packssdw: the destination's lanes, then the source's. */
        int size = op == 0x6B ? 1 : 0;
        uint32_t lo = op == 0x67 ? 0x2E212800u : 0x0E214800u; /* sqxtun, sqxtn */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        put(e, lo | (uint32_t)size << 22 | (uint32_t)V0 << 5 | V2);
        put(e, lo | 0x40000000u | (uint32_t)size << 22 | (uint32_t)V1 << 5 | V2); /* the 2 forms fill the upper half */
        xstore(x, V2, i->reg);
        return 1;
    }
    case 0xE4: case 0xE5: /* pmulhuw, pmulhw: the upper halves of the products. */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        vop(e, op == 0xE5 ? N_SMULL : N_UMULL, 1, V2, V0, V1);
        vop(e, op == 0xE5 ? N_SMULL2 : N_UMULL2, 1, V3, V0, V1);
        vop(e, N_UZP2, 1, V0, V2, V3);
        xstore(x, V0, i->reg);
        return 1;
    case 0xF5: /* pmaddwd */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        vop(e, N_SMULL, 1, V2, V0, V1);
        vop(e, N_SMULL2, 1, V3, V0, V1);
        vop(e, N_ADDP, 2, V0, V2, V3);
        xstore(x, V0, i->reg);
        return 1;
    case 0xF4: /* pmuludq: the even dwords */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        put(e, 0x0EA12800u | (uint32_t)V0 << 5 | V2); /* xtn v2.2s, v0.2d */
        put(e, 0x0EA12800u | (uint32_t)V1 << 5 | V3);
        vop(e, N_UMULL, 2, V0, V2, V3);
        xstore(x, V0, i->reg);
        return 1;
    case 0xF6: /* psadbw: the sum of each half's absolute differences */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        vop(e, 0x6E207400u, 0, V0, V0, V1); /* uabd */
        for (int size = 0; size < 3; size++)
            put(e, 0x6E202800u | (uint32_t)size << 22 | (uint32_t)V0 << 5 | V0); /* uaddlp */
        xstore(x, V0, i->reg);
        return 1;
    case 0xD1: case 0xD2: case 0xD3: case 0xE1: case 0xE2: case 0xF1: case 0xF2: case 0xF3: {
        /* shifts by the low quadword of the source: counts past the lane width clear it or fill it with the sign. */
        int size = (op & 3) == 1 ? 1 : (op & 3) == 2 ? 2 : 3, bits = 8 << size;
        int right = op < 0xF0, arith = (op & 0xF0) == 0xE0;
        x->keep = 0; /* the clamp's compare replaces the arm64 flags. */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        put(e, 0x9E660000u | (uint32_t)V1 << 5 | 16); /* fmov x16, d1 */
        mov_imm(e, 17, (uint64_t)bits);
        put(e, 0xEB00001Fu | 17u << 16 | 16u << 5); /* cmp x16, x17 */
        put(e, 0x9A800000u | 17u << 16 | (uint32_t)LS << 12 | 16u << 5 | 16); /* csel x16, x16, x17, ls */
        if (right)
            put(e, 0xCB1003F0u); /* neg x16, x16 */
        put(e, 0x4E000C00u | (1u << size) << 16 | 16u << 5 | V1); /* dup v1, x16 */
        vop(e, arith ? N_SSHL : N_USHL, size, V0, V0, V1);
        xstore(x, V0, i->reg);
        return 1;
    }
    case 0x71: case 0x72: case 0x73: { /* shifts by an immediate */
        int sub = (i->modrm >> 3) & 7;
        unsigned n = (unsigned)(i->imm & 0xFF);
        if (i->mod != 3)
            return 0;
        xload(x, 16, V0, i->rm);
        if (op == 0x73 && (sub == 3 || sub == 7)) { /* psrldq, pslldq: whole bytes */
            if (n >= 16) {
                vzero(e, V0);
            } else if (n) {
                vzero(e, V3);
                if (sub == 3)
                    put(e, 0x6E000000u | (uint32_t)V3 << 16 | n << 11 | (uint32_t)V0 << 5 | V0); /* ext v0, v0, v3, #n */
                else
                    put(e, 0x6E000000u | (uint32_t)V0 << 16 | (16 - n) << 11 | (uint32_t)V3 << 5 | V0); /* ext v0, v3, v0 */
            }
        } else if (sub == 2 || sub == 6 || (sub == 4 && op != 0x73)) {
            shift_const(e, V0, op == 0x71 ? 2 : op == 0x72 ? 4 : 8, sub == 2 ? 0 : sub == 4 ? 1 : 2, n);
        } else {
            return 0;
        }
        xstore(x, V0, i->rm);
        return 1;
    }
    case 0xD7: /* pmovmskb: the top bit of every byte */
        if (i->mod != 3)
            return 0;
        xload(x, 16, V0, i->rm);
        put(e, 0x4E20A800u | (uint32_t)V0 << 5 | V0); /* cmlt v0.16b, v0.16b, #0 */
        mov_imm(e, 16, 0x8040201008040201ull);
        put(e, 0x4E080C00u | 16u << 5 | V1); /* dup v1.2d, x16 */
        v3(e, V_AND, V0, V0, V1);
        for (int k = 0; k < 3; k++)
            vop(e, N_ADDP, 0, V0, V0, V0);
        put(e, 0x0E023C00u | (uint32_t)V0 << 5 | T0); /* umov w10, v0.h[0] */
        store_reg(e, 8, T0, i->reg, 1);
        return 1;
    case 0x70: /* pshuflw, pshufhw */
        if (i->mandatory != 0xF2 && i->mandatory != 0xF3)
            return -1;
        load_xsrc(x, i, 16, V1);
        vmov(e, V2, V1);
        for (int k = 0; k < 4; k++) {
            int base = i->mandatory == 0xF3 ? 4 : 0;
            ins(e, 2, V2, base + k, V1, base + ((int)(i->imm >> (2 * k)) & 3));
        }
        xstore(x, V2, i->reg);
        return 1;
    }
    return -1;
}

static int emit_sse(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode, pfx = i->mandatory;
    int dbl = pfx == 0x66 || pfx == 0xF2;
    int scalar = pfx == 0xF2 || pfx == 0xF3;
    int lane = pfx == 0xF2 || pfx == 0x66 ? 8 : 4; /* element size */
    uint32_t sz = dbl ? 1u << 22 : 0;
    uint32_t *ok = nullptr, *done = nullptr;
    x->keep = 1; /* every case but the arithmetic, conversions to integers and compares. */
    if (pfx == 0x66 || op == 0x70) {
        int r = emit_simd_int(x, i);
        if (r >= 0)
            return r;
    }
    switch (op) {
    case 0x10: /* movups, movupd, movss, movsd */
        if (scalar && i->mod == 3) {
            xload(x, 16, V0, i->reg);
            xload(x, 16, V1, i->rm);
            ins(e, lane, V0, 0, V1, 0);
            xstore(x, V0, i->reg);
        } else {
            load_xsrc(x, i, scalar ? lane : 16, V0);
            xstore(x, V0, i->reg);
        }
        return 1;
    case 0x11:
        xload(x, 16, V0, i->reg);
        if (i->mod == 3 && scalar) {
            xload(x, 16, V1, i->rm);
            ins(e, lane, V1, 0, V0, 0);
            xstore(x, V1, i->rm);
        } else if (i->mod == 3) {
            xstore(x, V0, i->rm);
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
        xstore(x, V0, i->reg);
        return 1;
    case 0x29:
    case 0x7F:
        if ((op == 0x29 && scalar) || (op == 0x7F && pfx != 0x66 && pfx != 0xF3))
            return 0;
        xload(x, 16, V0, i->reg);
        if (i->mod == 3)
            xstore(x, V0, i->rm);
        else {
            ea(x, i);
            vstr(e, 16, V0, ADDR, 0);
        }
        return 1;
    case 0xD6: /* movq xmm/m64, xmm */
        if (pfx != 0x66)
            return 0;
        xload(x, 8, V0, i->reg);
        if (i->mod == 3)
            xstore(x, V0, i->rm);
        else {
            ea(x, i);
            vstr(e, 8, V0, ADDR, 0);
        }
        return 1;
    case 0x7E:
        if (pfx == 0xF3) { /* movq xmm, xmm/m64 */
            if (i->mod == 3)
                xload(x, 8, V0, i->rm);
            else
                load_xsrc(x, i, 8, V0);
            xstore(x, V0, i->reg);
            return 1;
        }
        if (pfx != 0x66)
            return 0;
        /* movd, movq rm, xmm */
        if (i->mod == 3) {
            if (i->rex & 8)
                put(e, 0x9E660000u | (uint32_t)xget(x, i->reg) << 5 | T0); /* fmov x10, d */
            else
                put(e, 0x1E260000u | (uint32_t)xget(x, i->reg) << 5 | T0); /* fmov w10, s */
            store_reg(e, 8, T0, i->rm, 1);
        } else {
            xload(x, (i->rex & 8) ? 8 : 4, V0, i->reg);
            ea(x, i);
            vstr(e, (i->rex & 8) ? 8 : 4, V0, ADDR, 0);
        }
        return 1;
    case 0x6E: /* movd, movq xmm, rm */
        if (pfx != 0x66)
            return 0;
        if (i->mod == 3 && host_reg(i->rm) >= 0) {
            put(e, ((i->rex & 8) ? 0x9E670000u : 0x1E270000u) | (uint32_t)host_reg(i->rm) << 5 | V0); /* fmov */
        } else if (i->mod == 3) {
            if (i->rex & 8)
                vldr(e, 8, V0, CPU, OFF_R(i->rm));
            else
                vldr(e, 4, V0, CPU, OFF_R(i->rm));
        } else {
            load_xsrc(x, i, (i->rex & 8) ? 8 : 4, V0);
        }
        xstore(x, V0, i->reg);
        return 1;
    case 0x14: /* unpcklps, unpcklpd */
    case 0x15:
        if (scalar)
            return 0;
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        v3(e, (op == 0x14 ? 0x0E003800u : 0x0E007800u) | 0x40000000u | (dbl ? 3u : 2u) << 22, V0, V0, V1);
        xstore(x, V0, i->reg);
        return 1;
    case 0x54: case 0x55: case 0x56: case 0x57: /* andps, andnps, orps, xorps and the pd forms */
    case 0xDB: case 0xDF: case 0xEB: case 0xEF: { /* pand, pandn, por, pxor */
        if (op >= 0xDB ? pfx != 0x66 : scalar)
            return 0;
        int k = op >= 0xDB ? (op == 0xDB ? 0 : op == 0xDF ? 1 : op == 0xEB ? 2 : 3) : op - 0x54;
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        if (k == 1)
            v3(e, V_BIC, V0, V1, V0); /* ~dst & src */
        else
            v3(e, k == 0 ? V_AND : k == 2 ? V_ORR : V_EOR, V0, V0, V1);
        xstore(x, V0, i->reg);
        return 1;
    }
    case 0x58: case 0x59: case 0x5C: case 0x5E: case 0x51: { /* add, mul, sub, div, sqrt */
        x->keep = 0;
        static const uint32_t vec[] = {0x4E20D400u, 0x6E20DC00u, 0, 0, 0x4EA0D400u, 0, 0x6E20FC00u};
        static const uint32_t sca[] = {0x1E202800u, 0x1E200800u, 0, 0, 0x1E203800u, 0, 0x1E201800u};
        xload(x, 16, V0, i->reg);
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
        xstore(x, V0, i->reg);
        patch26(e, done);
        return 1;
    }
    case 0x5D: /* min: a < b ? a : b, the second operand for nans and equal values. */
    case 0x5F: /* max: a > b ? a : b */
        xload(x, 16, V0, i->reg);
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
        xstore(x, V0, i->reg);
        return 1;
    case 0xC2: { /* cmpps, cmppd, cmpss, cmpsd */
        int pred = (int)(i->imm & 7);
        xload(x, 16, V0, i->reg);
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
        xstore(x, V0, i->reg);
        return 1;
    }
    case 0x2E: /* ucomiss, ucomisd */
    case 0x2F: { /* comiss, comisd */
        if (scalar)
            return 0;
        x->keep = 0;
        xload(x, 16, V0, i->reg);
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
        xload(x, 16, V0, i->reg);
        ins(e, lane, V0, 0, V2, 0);
        xstore(x, V0, i->reg);
        return 1;
    }
    case 0x2C: { /* cvttss2si, cvttsd2si: nans and out of range values take the interpreter's indefinite value. */
        if (!scalar)
            return 0;
        x->keep = 0;
        int wide = (i->rex & 8) != 0;
        load_xsrc(x, i, lane, V1);
        put(e, (wide ? 0x80000000u : 0) | (dbl ? 0x1E780000u : 0x1E380000u) | (uint32_t)V1 << 5 | T0);
        put(e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)V1 << 16 | (uint32_t)V1 << 5); /* fcmp v1, v1 */
        uint32_t *nan = b_cond(e, VS);
        /* saturated: x - min is 0 or -1, all ones, for the two limits. */
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
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, lane, V1);
        put(e, (dbl ? 0x1E624000u : 0x1E22C000u) | (uint32_t)V1 << 5 | V2);
        ins(e, dbl ? 4 : 8, V0, 0, V2, 0);
        xstore(x, V0, i->reg);
        return 1;
    case 0x5B: /* cvtdq2ps, cvtps2dq, cvttps2dq */
        if (pfx == 0xF2)
            return 0;
        load_xsrc(x, i, 16, V1);
        if (!pfx) {
            put(e, 0x4E21D800u | (uint32_t)V1 << 5 | V3); /* scvtf, in fpcr's rounding as mxcsr's */
        } else {
            if (pfx == 0x66)
                put(e, 0x6EA19800u | (uint32_t)V1 << 5 | V1); /* frinti */
            put(e, 0x4EA1B800u | (uint32_t)V1 << 5 | V3);     /* fcvtzs */
            /* nans and values from 2^31 up saturate elsewhere than at the indefinite value. */
            put(e, 0x4F0265E0u | V0);                         /* movi 0x4f000000, 2^31 */
            v3(e, 0x6EA0E400u, V0, V0, V1);                   /* fcmgt: in range */
            put(e, 0x4F046400u | V2);                         /* movi 0x80000000 */
            v3(e, 0x6EE01C00u, V3, V2, V0);                   /* bif */
        }
        xstore(x, V3, i->reg);
        return 1;
    case 0x70: { /* pshufd */
        if (pfx != 0x66)
            return 0;
        load_xsrc(x, i, 16, V1);
        for (int k = 0; k < 4; k++)
            ins(e, 4, V2, k, V1, (int)(i->imm >> (2 * k)) & 3);
        xstore(x, V2, i->reg);
        return 1;
    }
    case 0x72: { /* psrld, psrad, pslld by an immediate */
        int sub = (i->modrm >> 3) & 7;
        if (pfx != 0x66 || i->mod != 3 || (sub != 2 && sub != 4 && sub != 6))
            return 0;
        unsigned n = (unsigned)(i->imm & 0xFF);
        xload(x, 16, V0, i->rm);
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
        xstore(x, V0, i->rm);
        return 1;
    }
    case 0xC6: { /* shufps, shufpd */
        if (pfx != 0 && pfx != 0x66)
            return 0;
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        unsigned imm = (unsigned)i->imm;
        if (pfx == 0x66) {
            ins(e, 8, V2, 0, V0, (int)(imm & 1));
            ins(e, 8, V2, 1, V1, (int)((imm >> 1) & 1));
        } else {
            for (int k = 0; k < 4; k++)
                ins(e, 4, V2, k, k < 2 ? V0 : V1, (int)(imm >> (2 * k)) & 3);
        }
        xstore(x, V2, i->reg);
        return 1;
    }
    case 0x12: /* movlps, movlpd, movhlps */
    case 0x16: /* movhps, movhpd, movlhps */
        if (scalar || (pfx == 0x66 && i->mod == 3))
            return 0;
        xload(x, 16, V0, i->reg);
        if (i->mod == 3) {
            xload(x, 16, V1, i->rm);
            ins(e, 8, V0, op == 0x16, V1, op == 0x12);
        } else {
            load_xsrc(x, i, 8, V1);
            ins(e, 8, V0, op == 0x16, V1, 0);
        }
        xstore(x, V0, i->reg);
        return 1;
    case 0x13: /* movlps, movlpd store */
    case 0x17: /* movhps, movhpd store */
        if (scalar || i->mod == 3)
            return 0;
        ea(x, i);
        if (op == 0x13) {
            xload(x, 8, V0, i->reg);
        } else {
            xload(x, 16, V0, i->reg);
            ins(e, 8, V0, 0, V0, 1);
        }
        vstr(e, 8, V0, ADDR, 0);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ atomics */

enum { LSE_ADD = X86_ATOMIC_ADD, LSE_CLR = X86_ATOMIC_CLR, LSE_EOR = X86_ATOMIC_EOR, LSE_SET = X86_ATOMIC_SET,
       LSE_SWP = X86_ATOMIC_SWP };

/* ldaddal and the others: rt = [rn], [rn] = [rn] op rs. */
static void lse(Emit *e, int op, int size, int rs, int rn, int rt)
{
    put(e, (uint32_t)log2_size(size) << 30 | 0x38E00000u | (uint32_t)rs << 16 | (uint32_t)op << 12 |
               (uint32_t)rn << 5 | (uint32_t)rt);
}

/* a misaligned address, which arm64 atomics fault on: the branch to atomic_call. */
static uint32_t *misaligned(Emit *e, int size)
{
    if (size == 1)
        return nullptr;
    int k = log2_size(size);
    ubfm(e, 8, T4, ADDR, k, k - 1); /* lsl t4, addr, #(64 - k) */
    uint32_t *at = e->p;
    put(e, 0xB5000000u | T4); /* cbnz */
    return at;
}

/*
 * the misaligned case through the interpreter's atomics: fn(addr, a1, a2, a3) with the address in ADDR, its result to
 * rd. the registers go to the cpu around it like for the interpreter, which a fault in it reads. t0 to t5 are lost.
 */
static void atomic_call(Ctx *x, const void *fn, uint64_t a1, int a2, int a3, int rd)
{
    Emit *e = x->e;
    mov_imm(e, T4, x->addr);
    str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    str(e, 8, T4, CPU, offsetof(X86Cpu, insn_rip));
    sync_regs(e, 1);
    xstore_dirty(x);
    if (a3 >= 0)
        mov_reg(e, 8, 3, a3);
    mov_reg(e, 8, 2, a2);
    mov_reg(e, 8, 0, ADDR);
    mov_imm(e, 1, a1);
    call(e, fn);
    mov_reg(e, 8, rd, 0);
    sync_regs(e, 0);
    xreload(x);
}

/*
 * the locked read modify write instructions, and xadd, cmpxchg and xchg on memory, which lock by themselves. the old
 * value comes to t0 from one arm64 atomic, the flags from it after.
 */
static int emit_atomic(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int sub = (i->modrm >> 3) & 7;
    if (i->map == 0 && (op == 0x86 || op == 0x87) && i->mod == 3 && !i->lock) {
        int size = op == 0x86 ? 1 : i->opsize;
        load_reg(e, size, T0, i->rm, i->rex);
        load_reg(e, size, T1, i->reg, i->rex);
        store_reg(e, size, T1, i->rm, i->rex);
        store_reg(e, size, T0, i->reg, i->rex);
        return x->keep = 1, 1;
    }
    if (i->mod == 3)
        return 0;
    if (i->map == 0 && (op == 0x86 || op == 0x87)) {
        int size = op == 0x86 ? 1 : i->opsize;
        ea(x, i);
        load_reg(e, size, T1, i->reg, i->rex);
        uint32_t *slow = misaligned(e, size);
        lse(e, LSE_SWP, size, T1, ADDR, T0);
        uint32_t *done = b_always(e);
        patch19(e, slow);
        atomic_call(x, (const void *)x86_jit_atomic, (uint64_t)LSE_SWP << 8 | (uint64_t)size, T1, -1, T0);
        patch26(e, done);
        store_reg(e, size, T0, i->reg, i->rex);
        return x->keep = 1, 1;
    }
    if (i->map == 1 && (op == 0xB0 || op == 0xB1)) { /* cmpxchg */
        int size = op == 0xB0 ? 1 : i->opsize;
        ea(x, i);
        load_reg(e, size, T0, X86_RAX, 1);
        load_reg(e, size, T1, i->reg, i->rex);
        mov_reg(e, 8, T3, T0);
        uint32_t *slow = misaligned(e, size);
        put(e, (uint32_t)log2_size(size) << 30 | 0x08E0FC00u | (uint32_t)T3 << 16 | (uint32_t)ADDR << 5 | T1); /* casal */
        uint32_t *done = b_always(e);
        patch19(e, slow);
        atomic_call(x, (const void *)x86_jit_cas, (uint64_t)size, T3, T1, T3);
        load_reg(e, size, T0, X86_RAX, 1);
        patch26(e, done);
        /* the flags of cmp rax, old. a failure loads the old value into the accumulator. */
        mov_reg(e, 8, T1, T3);
        alu_core(x, ALU_SUB, size);
        uint32_t *same = b_cond(e, EQ);
        store_reg(e, size, T1, X86_RAX, 1);
        patch19(e, same);
        return 1;
    }
    if (i->map == 1 && (op == 0xC0 || op == 0xC1)) { /* xadd */
        int size = op == 0xC0 ? 1 : i->opsize;
        ea(x, i);
        load_reg(e, size, T1, i->reg, i->rex);
        uint32_t *slow = misaligned(e, size);
        lse(e, LSE_ADD, size, T1, ADDR, T0);
        uint32_t *done = b_always(e);
        patch19(e, slow);
        atomic_call(x, (const void *)x86_jit_atomic, (uint64_t)LSE_ADD << 8 | (uint64_t)size, T1, -1, T0);
        load_reg(e, size, T1, i->reg, i->rex);
        patch26(e, done);
        alu_core(x, ALU_ADD, size);
        store_reg(e, size, T0, i->reg, i->rex);
        return 1;
    }
    if (!i->lock || i->map != 0)
        return 0;
    if ((op == 0xFE || op == 0xFF) && sub < 2) { /* inc, dec */
        int size = op == 0xFE ? 1 : i->opsize, dec = sub;
        if (x->flags_lazy & LIVE_CF)
            carry_t3(x);
        ea(x, i);
        mov_imm(e, T1, 1);
        mov_imm(e, T5, dec ? ~0ull : 1);
        uint32_t *slow = misaligned(e, size);
        lse(e, LSE_ADD, size, T5, ADDR, T0);
        uint32_t *done = b_always(e);
        patch19(e, slow);
        put(e, 0xF81F0FE0u | T3); /* str t3, [sp, #-16]!: the carry outlives the call */
        atomic_call(x, (const void *)x86_jit_atomic, (uint64_t)LSE_ADD << 8 | (uint64_t)size, T5, -1, T0);
        put(e, 0xF84107E0u | T3); /* ldr t3, [sp], #16 */
        mov_imm(e, T1, 1);
        patch26(e, done);
        if (size >= 4) {
            rrr(e, dec ? OP_SUBS : OP_ADDS, size, T2, T0, T1);
        } else {
            int sh = 32 - size * 8;
            rrr(e, dec ? OP_SUB : OP_ADD, 4, T2, T0, T1);
            narrow(e, size, T2);
            ubfm(e, 4, S0, T0, (32 - sh) & 31, 31 - sh);
            put(e, (dec ? OP_SUBS : OP_ADDS) | (uint32_t)sh << 10 | (uint32_t)T1 << 16 | (uint32_t)S0 << 5 | XZR);
        }
        if (x->flags_lazy) {
            mov_imm(e, T4, X86_LF(dec ? X86_LF_DEC : X86_LF_INC, log2_size(size)));
            str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
            str(e, 8, T2, CPU, offsetof(X86Cpu, lf_res));
            if (x->flags_lazy & LIVE_CF)
                str(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
        }
        set_nzcv(x, ALU_INCDEC);
        return 1;
    }
    /* add, or, and, sub and xor into memory. */
    int kind, size;
    if (op < 0x40 && (op & 7) <= 1)
        kind = op >> 3, size = op & 1 ? i->opsize : 1;
    else if (op == 0x80 || op == 0x81 || op == 0x83)
        kind = sub, size = op == 0x80 ? 1 : i->opsize;
    else
        return 0;
    if (kind != ALU_ADD && kind != ALU_OR && kind != ALU_AND && kind != ALU_SUB && kind != ALU_XOR)
        return 0;
    ea(x, i);
    if (op < 0x40)
        load_reg(e, size, T1, i->reg, i->rex);
    else
        mov_imm(e, T1, i->imm & size_mask(size));
    int lop = LSE_ADD, rs = T1;
    if (kind == ALU_SUB)
        rrr(e, OP_SUB, 8, rs = T5, XZR, T1);
    else if (kind == ALU_AND)
        put(e, 0xAA2003E0u | (uint32_t)T1 << 16 | (rs = T5)), lop = LSE_CLR; /* mvn */
    else if (kind == ALU_OR)
        lop = LSE_SET;
    else if (kind == ALU_XOR)
        lop = LSE_EOR;
    uint32_t *slow = misaligned(e, size);
    lse(e, lop, size, rs, ADDR, T0);
    uint32_t *done = b_always(e);
    patch19(e, slow);
    atomic_call(x, (const void *)x86_jit_atomic, (uint64_t)lop << 8 | (uint64_t)size, rs, -1, T0);
    if (op < 0x40)
        load_reg(e, size, T1, i->reg, i->rex);
    else
        mov_imm(e, T1, i->imm & size_mask(size));
    patch26(e, done);
    alu_core(x, kind, size);
    return 1;
}

/* ------------------------------------------------------------ general instructions */

/* emits one instruction natively, or returns 0 for the interpreter. */
static int emit_one(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = i->opsize;
    if (is_jcc(i))
        return emit_side_exit(x, i);
    int kind = alu_op(i);
    if (kind < 0)
        kind = carry_op(i);
    if (kind >= 0)
        return emit_alu(x, i, kind);
    if (i->lock || (i->map == 1 && (op == 0xB0 || op == 0xB1 || op == 0xC0 || op == 0xC1)) ||
        (i->map == 0 && (op == 0x86 || op == 0x87)))
        return emit_atomic(x, i);
    if (i->map == 1) {
        if (op == 0x1F || op == 0x18 || op == 0x0D) /* nop, prefetch */
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
        if (op >= 0xC8 && op <= 0xCF && size >= 4) { /* bswap */
            int reg = (op & 7) | ((i->rex & 1) << 3);
            load_reg(e, size, T0, reg, 1);
            put(e, (size == 8 ? 0xDAC00C00u : 0x5AC00800u) | (uint32_t)T0 << 5 | T0); /* rev */
            store_reg(e, size, T0, reg, 1);
            return x->keep = 1, 1;
        }
        if (op >= 0x90 && op <= 0x9F) { /* setcc */
            x->keep = condition_t3(x, op & 15);
            store_rm(x, i, 1, T3, 0);
            return 1;
        }
        if (op >= 0x40 && op <= 0x4F) { /* cmovcc */
            int cc = op & 15;
            int cond = x->nzcv_alu >= 0 ? arm_cond(x->nzcv_alu, cc) : -1;
            x->keep = cond != -1;
            if (cond == -1)
                condition_t3(x, cc);
            load_rm(x, i, size, T1);
            load_reg(e, size, T0, i->reg, i->rex);
            if (cond == -2 || cond == -3) {
                if (cond == -3)
                    mov_reg(e, 8, T0, T1);
            } else {
                if (cond == -1) {
                    put(e, 0x7100001Fu | (uint32_t)T3 << 5); /* cmp w13, #0 */
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
    if (i->map == 2 && op == 0x00 && i->mandatory == 0x66) { /* pshufb: indexes with the top bit set give zero, like tbl's past the table */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        put(e, 0x4F00E400u | 4u << 16 | 0xFu << 5 | V3); /* movi v3.16b, #0x8f */
        v3(e, V_AND, V1, V1, V3);
        put(e, 0x4E000000u | (uint32_t)V1 << 16 | (uint32_t)V0 << 5 | V0); /* tbl v0.16b, {v0.16b}, v1.16b */
        xstore(x, V0, i->reg);
        return x->keep = 1, 1;
    }
    if (i->map == 2 && op == 0x04 && i->mandatory == 0x66) { /* pmaddubsw: unsigned bytes by signed ones, pairs added with saturation */
        xload(x, 16, V0, i->reg);
        load_xsrc(x, i, 16, V1);
        put(e, 0x2F08A400u | (uint32_t)V0 << 5 | V2); /* uxtl v2.8h, v0.8b */
        put(e, 0x6F08A400u | (uint32_t)V0 << 5 | V3); /* uxtl2 v3.8h, v0.16b */
        put(e, 0x0F08A400u | (uint32_t)V1 << 5 | 4);  /* sxtl v4.8h, v1.8b */
        put(e, 0x4F08A400u | (uint32_t)V1 << 5 | 5);  /* sxtl2 v5.8h, v1.16b */
        vop(e, N_MUL, 1, V2, V2, 4);
        vop(e, N_MUL, 1, V3, V3, 5);
        vop(e, 0x4E001800u, 1, 4, V2, V3); /* uzp1: the even products */
        vop(e, N_UZP2, 1, 5, V2, V3);      /* the odd ones */
        vop(e, N_SQADD, 1, V0, 4, 5);
        xstore(x, V0, i->reg);
        return x->keep = 1, 1;
    }
    if (i->map != 0)
        return 0;
    switch (op) {
    case 0x88: /* mov rm8, r8 */
    case 0x89: /* mov rm, r */
        size = op == 0x88 ? 1 : size;
        if (size >= 4 && host_reg(i->reg) >= 0 && (i->mod != 3 || host_reg(i->rm) >= 0)) {
            if (i->mod == 3)
                mov_reg(e, size, host_reg(i->rm), host_reg(i->reg));
            else
                store_rm(x, i, size, host_reg(i->reg), 0);
            return x->keep = 1, 1;
        }
        load_reg(e, size, T0, i->reg, i->rex);
        store_rm(x, i, size, T0, 0);
        return x->keep = 1, 1;
    case 0x8A:
    case 0x8B: /* mov r, rm */
        size = op == 0x8A ? 1 : size;
        if (size >= 4 && host_reg(i->reg) >= 0) {
            load_rm(x, i, size, host_reg(i->reg));
            return x->keep = 1, 1;
        }
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
        if (size >= 4 && host_reg((op & 7) | ((i->rex & 1) << 3)) >= 0) {
            mov_imm(e, host_reg((op & 7) | ((i->rex & 1) << 3)), i->imm & size_mask(size));
            return x->keep = 1, 1;
        }
        mov_imm(e, T0, i->imm & size_mask(size));
        store_reg(e, size, T0, (op & 7) | ((i->rex & 1) << 3), 1);
        return x->keep = 1, 1;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
        /* inc and dec of a register, the rex prefixes of 64-bit code. */
        if (x->cpu->mode64)
            return 0;
        X86Insn r = *i;
        r.has_modrm = 1;
        r.mod = 3;
        r.rm = op & 7;
        return emit_incdec(x, &r, size, op >= 0x48);
    }
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: { /* mov between the accumulator and an absolute address */
        if (i->addrsize != 4)
            return 0;
        X86Insn m = *i;
        m.has_modrm = 1;
        m.mod = 0;
        m.base = -1;
        m.index = -1;
        m.disp = (int32_t)(uint32_t)i->imm;
        int sz = op & 1 ? size : 1;
        if (op <= 0xA1) {
            load_rm(x, &m, sz, T0);
            store_reg(e, sz, T0, X86_RAX, 1);
        } else {
            load_reg(e, sz, T0, X86_RAX, 1);
            store_rm(x, &m, sz, T0, 0);
        }
        return x->keep = 1, 1;
    }
    case 0xC9: { /* leave */
        int w = stack_word(x);
        if (size != w)
            return 0;
        /* the stack pointer moves once the load did not fault. */
        mov_reg(e, w, T3, host_reg(X86_RBP));
        ldr(e, w, host_reg(X86_RBP), T3, 0);
        add_imm(e, w, host_reg(X86_RSP), T3, w);
        return x->keep = 1, 1;
    }
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
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        if (size != stack_word(x))
            return 0;
        if (host_reg(reg) < 0)
            load_reg(e, size, T0, reg, 1);
        push_value(x, host_reg(reg) >= 0 ? host_reg(reg) : T0);
        return x->keep = 1, 1;
    }
    case 0x6A: case 0x68: /* push imm */
        if (size != stack_word(x))
            return 0;
        mov_imm(e, T0, i->imm & size_mask(size));
        push_value(x, T0);
        return x->keep = 1, 1;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        int reg = (op & 7) | ((i->rex & 1) << 3);
        if (size != stack_word(x) || reg == X86_RSP)
            return 0;
        load_reg(e, size, T3, X86_RSP, 1);
        if (host_reg(reg) >= 0) {
            /* the stack pointer moves once the load did not fault. */
            ldr(e, size, host_reg(reg), T3, 0);
            add_imm(e, size, host_reg(X86_RSP), T3, size);
            return x->keep = 1, 1;
        }
        ldr(e, size, T0, T3, 0);
        move_stack(x, 1);
        store_reg(e, 8, T3, X86_RSP, 1);
        store_reg(e, size, T0, reg, 1);
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
        if (sub == 6 && op == 0xFF && size == stack_word(x)) { /* push rm */
            load_rm(x, i, size, T0);
            push_value(x, T0);
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

/* control transfers ending the block natively. returns 0 to leave it to the interpreter. */
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
        exit_const(x, branch_target(x, x->next + i->imm), x->count);
        return 1;
    case 0xE8: /* call rel */
        if (i->p66)
            return 0;
        mov_imm(e, T0, x->next);
        push_value(x, T0);
        exit_const(x, branch_target(x, x->next + i->imm), x->count);
        return 1;
    case 0xC3: /* ret */
    case 0xC2: /* ret imm16 */ {
        int w = stack_word(x);
        if (i->opsize != w)
            return 0;
        load_reg(e, w, T3, X86_RSP, 1);
        ldr(e, w, T0, T3, 0);
        mov_imm(e, T1, (uint64_t)w + (op == 0xC2 ? (i->imm & 0xFFFF) : 0));
        rrr(e, OP_ADD, w, T3, T3, T1);
        store_reg(e, 8, T3, X86_RSP, 1);
        exit_computed(x, T0);
        return 1;
    }
    case 0xFF: {
        int sub = (i->modrm >> 3) & 7, w = stack_word(x);
        /* near calls and jumps take targets of the stack word: 64 bits in 64-bit code without rex.w. */
        if ((sub != 2 && sub != 4) || i->p66)
            return 0;
        load_rm(x, i, w, T0);
        if (i->mod != 3 && i->base == -2 && x->cpu->mode64) {
            /*
             * a pointer beside the code that leads to a bare jmp rax is control flow guard's dispatch without the
             * checks, which every indirect call of a module goes through: going to rax from here spares the one
             * shared branch, which predicts badly.
             */
            put(e, 0x79400000u | (uint32_t)T0 << 5 | T1); /* ldrh t1, [t0] */
            mov_imm(e, T2, 0xE0FF);
            rrr(e, OP_EOR, 4, T1, T1, T2);
            uint32_t *other = e->p;
            put(e, 0x35000000u | T1); /* cbnz w */
            mov_reg(e, 8, T0, host_reg(X86_RAX));
            patch19(e, other);
        }
        if (sub == 2) {
            mov_imm(e, T1, x->next);
            push_value(x, T1);
        }
        exit_computed(x, T0);
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
    sync_regs(e, 1);
    xstore_dirty(x);
    mov_reg(e, 8, 0, CPU);
    mov_imm(e, 1, (uint64_t)(uintptr_t)i);
    call(e, (const void *)x86_exec_insn);
    sync_regs(e, 0);
    xreload(x);
    x->nzcv_alu = -1;
}

/* the kind of arm64 flags the translation of i leaves for the instruction after it when that one reads them, or -1. */
static int nzcv_kind(const X86Insn *i)
{
    if (i->map != 0 || i->lock)
        return -1;
    int kind = alu_op(i);
    if (kind >= 0)
        return kind;
    if (carry_op(i) >= 0)
        return alu_size(i) < 4 ? -1 : carry_op(i) == ALU_ADC ? ALU_ADD : ALU_SUB;
    uint8_t op = i->opcode;
    int sub = (i->modrm >> 3) & 7;
    if ((op >= 0x40 && op <= 0x4F) || ((op == 0xFE || op == 0xFF) && sub < 2))
        return ALU_INCDEC;
    if (op >= 0xC0 && op <= 0xD1 && (op <= 0xC1 || op >= 0xD0) && sub >= 4) {
        unsigned n = (op <= 0xC1 ? (unsigned)(i->imm & 0xFF) : 1) & ((op & 1) && i->opsize == 8 ? 63 : 31);
        return n ? ALU_SHIFT : -1;
    }
    return -1;
}

/* the flags the places the block continues at may read. */
static int flags_live_after(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint64_t end)
{
    const X86Insn *last = &insns[count - 1];
    uint64_t lo = rip & ~0xFFFull, hi = ((end - 1) | 0xFFFull) + 1, start, stop;
    /* the region the block came from is readable past its own pages. */
    if (cpu->env && cpu->env->code_bounds && cpu->env->code_bounds(cpu, rip, &start, &stop) && start <= lo && hi <= stop)
        lo = start, hi = stop;
    uint64_t next = end, target = cpu->mode64 ? next + last->imm : (uint32_t)(next + last->imm);
    if (!x86_ends_block(last))
        return flags_read_at(cpu, next, lo, hi);
    if (is_jcc(last))
        return flags_read_at(cpu, next, lo, hi) | flags_read_at(cpu, target, lo, hi);
    if (last->map == 0 && (last->opcode == 0xE9 || last->opcode == 0xEB || last->opcode == 0xE8))
        return flags_read_at(cpu, target, lo, hi);
    return LIVE_ALL;
}

uint32_t x86_jit_translate(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint32_t *code,
                           uint32_t capacity, uint16_t *offsets, uint16_t *xmm_held)
{
    Emit e = {code, code + capacity, code, 0};
    uint8_t live[64], lazy[64];
    if (!count || count > 64)
        return 0;
    uint64_t end_addr = rip;
    for (uint32_t k = 0; k < count; k++)
        end_addr += insns[k].len;
    /* liveness of the flags each instruction sets. */
    int flags_live = flags_live_after(cpu, rip, insns, count, end_addr);
    uint64_t lo = rip & ~0xFFFull, hi = ((end_addr - 1) | 0xFFFull) + 1, region_lo, region_hi;
    if (cpu->env && cpu->env->code_bounds && cpu->env->code_bounds(cpu, rip, &region_lo, &region_hi) && region_lo <= lo &&
        hi <= region_hi)
        lo = region_lo, hi = region_hi;
    uint64_t starts[64], a = rip;
    for (uint32_t k = 0; k < count; a += insns[k++].len)
        starts[k] = a;
    for (uint32_t k = count; k-- > 0;) {
        /* a branch before the end: the flags its target reads too. */
        if (k + 1 < count && is_jcc(&insns[k])) {
            uint64_t next = starts[k] + insns[k].len;
            flags_live |= flags_read_at(cpu, cpu->mode64 ? next + insns[k].imm : (uint32_t)(next + insns[k].imm), lo, hi);
        }
        live[k] = (uint8_t)flags_live;
        flags_live = live_before(&insns[k], flags_live);
    }
    /* a condition right after the instruction setting the flags reads the arm64 ones: the lazy ones only serve later readers. */
    for (uint32_t k = 0; k < count; k++) {
        lazy[k] = live[k];
        int kind = k + 1 < count && reads_condition(&insns[k + 1]) ? nzcv_kind(&insns[k]) : -1;
        if (kind >= 0 && arm_cond(kind, insns[k + 1].opcode & 15) != -1)
            lazy[k] = live[k + 1];
    }
    Ctx x = {&e, cpu, rip, rip, 0, count, 0, 0, -1, 0, 0, nullptr, rip, 0, 0};
    prologue(&e);
    uint32_t end = count;
    if (nests(&insns[count - 1]))
        end = count - 1;
    uint64_t addr = rip;
    for (uint32_t k = 0; k < end; k++) {
        const X86Insn *i = &insns[k];
        offsets[k] = (uint16_t)(e.p - code);
        xmm_held[k] = x.xloaded;
        x.addr = addr;
        x.next = addr + i->len;
        x.index = k;
        x.insn = i;
        x.flags_live = live[k];
        x.flags_lazy = lazy[k];
        if (k == count - 1 && x86_ends_block(i)) {
            /* the stores leave the arm64 flags alone, which a conditional branch may still test. */
            xstore_dirty(&x);
            x.xdirty = 0;
            if (!emit_branch(&x, i)) {
                emit_fallback(&x, i);
                /* the interpreter set rip to wherever the instruction went. */
                exit_computed(&x, -1);
            }
            offsets[count] = (uint16_t)(e.p - code);
            return e.overflow || e.p - code > 0xFFFF ? 0 : (uint32_t)(e.p - code);
        }
        if (!emit_insn(&x, i))
            emit_fallback(&x, i);
        addr = x.next;
    }
    offsets[end] = (uint16_t)(e.p - code);
    if (end < count)
        xmm_held[end] = x.xloaded;
    xstore_dirty(&x);
    x.xdirty = 0;
    exit_const(&x, addr, end);
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
