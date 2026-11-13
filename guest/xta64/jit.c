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
/* x28 holds the top of the return stack, see ras_call, and x29 counts down to the next poll, see poll. */
enum { RAS = 28, POLL = 29 };
enum { CPU = 19, ADDR = 9, T0 = 10, T1 = 11, T2 = 12, T3 = 13, T4 = 14, T5 = 15, S0 = 8, XZR = 31 };
enum { EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, VS = 6, VC = 7, HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13 };

typedef struct Emit {
    uint32_t *code, *end, *p;
    int overflow;
    uint32_t *leave;  /* the block's return to the dispatcher, once one was emitted: the others branch to it. */
    const void *self; /* the block translated. */
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

/* stores or loads the first count xmm registers, which live in v16 on, to or from the cpu. */
static void sync_xmm(Emit *e, int store, int count)
{
    for (int k = 0; k < count; k += 2) /* stp or ldp of two q registers */
        put(e, (store ? 0xAD000000u : 0xAD400000u) | (uint32_t)((offsetof(X86Cpu, xmm) + 16 * (uint32_t)k) / 16) << 15 |
                   (uint32_t)(17 + k) << 10 | (uint32_t)CPU << 5 | (uint32_t)(16 + k));
}

/*
 * enters or leaves the floating point mode of translated code, where the host has it: AH and NEP of fpcr set, see
 * x86_jit_afp. writing fpcr is slow and reading it is not, so it is only written when it has to change. AH stays
 * set once it is, see x86_run, and the dispatcher and what it calls run without NEP, as c code is compiled for. the
 * interpreter runs single instructions with it, which the tests check it does right. x16 is lost.
 */
static void fp_mode(Emit *e, int translated)
{
    if (!x86_jit_afp)
        return;
    put(e, 0xD53B4410u); /* mrs x16, fpcr */
    if (translated) {
        put(e, 0x37100070u); /* tbnz w16, #2, past the write */
        put(e, 0xB27F0610u); /* orr x16, x16, #6 */
    } else {
        put(e, 0x927DFA10u); /* and x16, x16, #~4 */
    }
    put(e, 0xD51B4410u); /* msr fpcr, x16 */
}

static void prologue(Emit *e)
{
    put(e, 0xA9BA7BF3u); /* stp x19, x30, [sp, #-96]! */
    for (int k = 0; k < 4; k++)
        pair(e, 1, HOST_REGS + 2 * k, HOST_REGS + 2 * k + 1, 31, 16 + 16 * (uint32_t)k);
    pair(e, 1, RAS, POLL, 31, 80);
    mov_reg(e, 8, CPU, 0);
    sync_regs(e, 0);
    sync_xmm(e, 0, 16);
    ldr(e, 8, RAS, CPU, offsetof(X86Cpu, ras));
    mov_imm(e, POLL, 1); /* the first way back polls. */
    fp_mode(e, 1);
    for (int k = x86_jit_afp ? 4 : 0; k < 4; k++)
        put(e, 0xD503201Fu); /* nop: the prologue is as long on every host. */
}

/*
 * returns index to the dispatcher, with the registers stored back and the block as the one that ran last: translated
 * code keeps no record of that while it runs on from block to block. a block has one such return, which its exits
 * share. t5 is lost.
 */
static void epilogue(Emit *e, uint32_t index)
{
    mov_imm(e, T5, index);
    if (e->leave) {
        put(e, 0x14000000u | ((uint32_t)(e->leave - e->p) & 0x3FFFFFFu));
        return;
    }
    e->leave = e->p;
    mov_imm(e, 17, (uint64_t)(uintptr_t)e->self);
    str(e, 8, 17, CPU, offsetof(X86Cpu, jit_block));
    str(e, 8, RAS, CPU, offsetof(X86Cpu, ras));
    fp_mode(e, 0);
    sync_xmm(e, 1, 16);
    sync_regs(e, 1);
    for (int k = 0; k < 4; k++)
        pair(e, 0, HOST_REGS + 2 * k, HOST_REGS + 2 * k + 1, 31, 16 + 16 * (uint32_t)k);
    pair(e, 0, RAS, POLL, 31, 80);
    put(e, 0xA8C67BF3u); /* ldp x19, x30, [sp], #96 */
    mov_reg(e, 8, 0, T5);
    put(e, 0xD65F03C0u); /* ret */
}

/*
 * a block begins with its prologue, which the dispatcher enters and a chained exit skips, then the poll that a loop
 * of the block alone comes back to, at X86_JIT_LOOP, then its code, at X86_JIT_PROLOGUE.
 */
enum { X86_JIT_LOOP = X86_JIT_PROLOGUE - 2 };

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
    uint32_t sha_second; /* the second sha256rnds2 of a pair whose result is in the cpu's jit_sha, or 0. */
    /* the x87 stack as the block holds it, see emit_x87: masks are of registers counted from the base. */
    int fbase;        /* x17 holds the base. */
    int fcur;         /* the top, counted from the base. */
    uint8_t floaded, fdirty, ftag_set, ftag_clear;
    int fmoved;       /* the top was moved, 2 once it was stored somewhere else than where it began. */
    uint8_t fmap[8];  /* which of v4 to v11 holds each: fxch only swaps two of these. */
    int final;        /* the fallback being emitted is on every path. */
    /*
     * an inc or dec whose flags only the code after the conditional branch that follows it may read: the branch taken
     * needs no record of them, so the record is made after the branch, on the way that stays.
     */
    uint32_t sink_at;  /* the index of that branch, ~0 for none. */
    int sink_live, sink_reg;
    uint32_t sink_lf;
    struct ColdList *cold;
    const void *self; /* the block translated. */
    int x87_seen;     /* an x87 instruction of the block ran, with nothing after it that leaves mmx mode set. */
    int x87_pending;  /* the cpu's last x87 instruction pointer and opcode are these, not stored yet. */
    uint32_t x87_fop;
    uint64_t x87_ip;
} Ctx;

/*
 * the xmm registers live in v16 to v31 while translated code runs, like the general ones in theirs: loaded by the
 * prologue, stored by the returns to the dispatcher, and kept through chained exits. c code keeps none of them, so
 * a call stores them before and loads them after. a fault takes them from the faulting context, see x86_jit_fault.
 */
enum { XCACHE = 16 };

static void vldr(Emit *e, int bytes, int vt, int rn, uint32_t off);
static void vstr(Emit *e, int bytes, int vt, int rn, uint32_t off);
static void vmov(Emit *e, int vd, int vn);

static int xget(Ctx *x, int n)
{
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
}

/* the ones the mode has: 32-bit code only reaches the first eight. */
static void xstore_dirty(Ctx *x) { sync_xmm(x->e, 1, x->cpu->mode64 ? 16 : 8); }
static void xreload(Ctx *x) { sync_xmm(x->e, 0, x->cpu->mode64 ? 16 : 8); }

static void x87_store(Ctx *x);
static void x87_reload(Ctx *x);

/* around a call to c code, which keeps none of x0 to x7, v16 to v31 and what the block holds of the x87 stack. */
static void call_begin(Ctx *x)
{
    sync_high(x->e, 1);
    xstore_dirty(x);
    x87_store(x);
}

static void call_end(Ctx *x)
{
    sync_high(x->e, 0);
    xreload(x);
    x87_reload(x);
}

static void emit_fallback(Ctx *x, const X86Insn *i);
static void add_imm(Emit *e, int size, int rd, int rn, int64_t imm);
static void x87_reset(Ctx *x);
static void leave_at(Ctx *x, uint32_t *site, uint64_t target);
static void sink_record(Ctx *x);
static int is_bt(const X86Insn *i);

static uint32_t *cbz_x(Emit *e, int rt)
{
    uint32_t *at = e->p;
    put(e, 0xB4000000u | (uint32_t)rt);
    return at;
}

/*
 * translated code that goes back, to a block at or before its own, could run without end: there it asks whether the
 * thread is to be suspended or code changed, in which case the dispatcher takes over. both are words in memory, so it
 * only looks once in X86_POLL_EVERY times, counted down in x29, and the looking is done after the block's code.
 * poll_fails says where a poll that found something goes. x17 and t5 are lost.
 */
enum { X86_POLL_EVERY = 512 };

static uint32_t poll(Ctx *x);
static void poll_fails(Ctx *x, uint32_t poll);

/*
 * leaves for an address in rip_reg, or in the cpu's rip when rip_reg is -1: straight into the block translated there
 * when the block table has one, otherwise back to the dispatcher. x16, x17 and t1 to t5 are free at an exit.
 */
static void exit_computed(Ctx *x, int rip_reg)
{
    Emit *e = x->e;
    x87_store(x);
    int stored = rip_reg < 0;
    if (stored) {
        ldr(e, 8, T0, CPU, offsetof(X86Cpu, rip));
        rip_reg = T0;
    }
    ldr(e, 8, 17, CPU, offsetof(X86Cpu, jit_slots));
    ldr(e, 8, T5, CPU, offsetof(X86Cpu, jit_mask));
    put(e, 0xCA400000u | (uint32_t)rip_reg << 16 | 4u << 10 | (uint32_t)rip_reg << 5 | T1);  /* eor t1, rip, rip, lsr #4 */
    put(e, 0xCA400000u | (uint32_t)rip_reg << 16 | 15u << 10 | (uint32_t)T1 << 5 | T1);      /* eor t1, t1, rip, lsr #15 */
    rrr(e, OP_AND, 8, T1, T1, T5);
    uint32_t *probe = e->p;
    add_shifted(e, T2, 17, T1, 5);
    put(e, 0xA9400000u | 16u << 10 | (uint32_t)T2 << 5 | T3); /* ldp t3, x16, [t2]: the slot's rip and code */
    put(e, 0xEB00001Fu | (uint32_t)rip_reg << 16 | (uint32_t)T3 << 5); /* cmp t3, rip */
    uint32_t *other = b_cond(e, NE);
    uint32_t *untranslated = cbz_x(e, 16);
    uint32_t polled = poll(x);
    put(e, 0xD61F0200u); /* br x16 */
    /* another address in the slot: the next one, unless this one is free. */
    patch19(e, other);
    ldr(e, 8, T4, T2, offsetof(X86Slot, block));
    uint32_t *free_slot = cbz_x(e, T4);
    add_imm(e, 8, T1, T1, 1);
    rrr(e, OP_AND, 8, T1, T1, T5);
    put(e, 0x14000000u | ((uint32_t)(probe - e->p) & 0x3FFFFFFu)); /* b probe */
    patch19(e, free_slot);
    patch19(e, untranslated);
    poll_fails(x, polled);
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
    x87_store(x);
    if (index != x->count) {
        exit_to(e, -1, rip, index);
        return;
    }
    if (rip == x->start) {
        /* a loop of the block alone goes straight back to the checks at its start. */
        put(e, 0x14000000u | ((uint32_t)(e->code + X86_JIT_LOOP - e->p) & 0x3FFFFFFu));
        return;
    }
    /*
     * a branch goes to the block the exit leads to once the dispatcher linked it: one the dispatcher writes over the
     * branch here, which leads back to it while the exit is not linked.
     */
    int back = rip <= x->start;
    uint32_t polled = back ? poll(x) : 0;
    uint32_t *branch = e->p;
    put(e, X86_CHAIN_UNLINKED);
    if (back)
        poll_fails(x, polled);
    mov_imm(e, T4, rip);
    str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    uint32_t *address_slot = e->p;
    put(e, 0x10000000u | 17); /* adr x17, slot */
    str(e, 8, 17, CPU, offsetof(X86Cpu, exit_slot));
    epilogue(e, index);
    if ((e->p - e->code) & 1)
        put(e, 0xD503201Fu); /* nop: the slot's words are 8 byte aligned. */
    patch19(e, address_slot);
    put(e, 0);
    put(e, 0);
    put(e, 0);
    put(e, 0);
    put(e, (uint32_t)rip);
    put(e, (uint32_t)(rip >> 32));
    put(e, (uint32_t)(uintptr_t)branch);
    put(e, (uint32_t)((uint64_t)(uintptr_t)branch >> 32));
    /* for a block too far for a branch to reach: X86_CHAIN_FAR words after the slot. */
    put(e, 0x58000000u | (uint32_t)(-8 & 0x7FFFF) << 5 | 16); /* ldr x16, slot.code */
    put(e, 0xD61F0200u);                                       /* br x16 */
}

static int stack_word(const Ctx *x) { return x->cpu->mode64 ? 8 : 4; }

static uint64_t branch_target(const Ctx *x, uint64_t target) { return x->cpu->mode64 ? target : (uint32_t)target; }

static void set_nzcv(Ctx *x, int kind)
{
    x->nzcv_alu = kind;
    x->nzcv_set = 1;
}

/* ------------------------------------------------------------ operands */

static uint64_t size_mask(int size) { return size == 8 ? ~0ull : (1ull << (size * 8)) - 1; }

/* rd = rn + imm at size, with t4 for immediates that are not one add's. rd and rn are never 31, the stack pointer here. */
static void add_imm(Emit *e, int size, int rd, int rn, int64_t imm)
{
    uint64_t m = imm < 0 ? -(uint64_t)imm : (uint64_t)imm;
    uint32_t op = (imm < 0 ? 0x51000000u : 0x11000000u) | sf(size);
    if (m >= 4096 && (m >= 1u << 24 || (m & 0xFFF))) {
        /* one add of a constant made aside, where two adds would both be waited for. */
        mov_imm(e, T4, (uint64_t)imm);
        rrr(e, OP_ADD, size, rd, rn, T4);
    } else if (m >= 4096) {
        put(e, op | 1u << 22 | (uint32_t)(m >> 12) << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
    } else {
        put(e, op | (uint32_t)m << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
    }
}

/*
 * computes the memory operand's address into a register, which may be one it is made of. 32-bit addresses are
 * computed with 32-bit operations, which wrap and zero extend like x86.
 */
static void ea_into(Ctx *x, const X86Insn *i, int A)
{
    Emit *e = x->e;
    int size = i->addrsize == 4 ? 4 : 8;
    int64_t disp = i->disp;
    int base = -1, index = -1;
    if (i->base == -2) {
        mov_imm(e, A, (x->next + (uint64_t)disp) & size_mask(size));
        base = A;
        disp = 0;
    } else if (i->base >= 0) {
        base = host_reg(i->base);
        if (base < 0) {
            ldr(e, 8, A, CPU, OFF_R(i->base));
            base = A;
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
        mov_imm(e, A, (uint64_t)disp & size_mask(size));
    } else if (index >= 0 && base < 0 && disp) {
        /* a table at a constant address: the index is added to it in one step. */
        mov_imm(e, T4, (uint64_t)disp & size_mask(size));
        rrr(e, OP_ADD | (uint32_t)i->scale << 10, size, A, T4, index);
    } else if (index >= 0) {
        rrr(e, OP_ADD | (uint32_t)i->scale << 10, size, A, base < 0 ? XZR : base, index); /* add rd, rn, rm, lsl #scale */
        if (disp)
            add_imm(e, size, A, A, disp);
    } else if (disp) {
        add_imm(e, size, A, base, disp);
    } else if (base != A || size == 4) {
        mov_reg(e, size, A, base);
    }
    if (i->seg == 4 || i->seg == 5) {
        ldr(e, 8, T4, CPU, i->seg == 4 ? offsetof(X86Cpu, fs_base) : offsetof(X86Cpu, gs_base));
        add_shifted(e, A, A, T4, 0);
    }
}

static void ea(Ctx *x, const X86Insn *i) { ea_into(x, i, ADDR); }

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
    ALU_TEST, ALU_UCOMI, ALU_INCDEC, ALU_SHIFT, ALU_BT,
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
    if (i->map == 0 && (op == 0xDB || op == 0xDF)) /* fucomi, fcomi and the ones that pop */
        return i->mod == 3 && i->modrm >= 0xE8 && i->modrm <= 0xF7;
    if (i->map == 0)
        return op == 0x69 || op == 0x6B || ((op == 0xF6 || op == 0xF7) && ((i->modrm >> 3) & 7) == 3);
    if (i->map != 1)
        return 0;
    if (op == 0xAF)
        return !i->mandatory;
    return (op == 0x2E || op == 0x2F) && (!i->mandatory || i->mandatory == 0x66);
}

static int is_x87(const X86Insn *i) { return i->map == 0 && i->opcode >= 0xD8 && i->opcode <= 0xDF; }

/* fcmov reads the flags, and fcomi and its kin write them. */
static int x87_uses_flags(const X86Insn *i)
{
    int sub = (i->modrm >> 3) & 7;
    if (i->mod != 3)
        return 0;
    return ((i->opcode == 0xDA || i->opcode == 0xDB) && sub < 4) || ((i->opcode == 0xDB || i->opcode == 0xDF) && sub >= 5 && sub <= 6);
}

/* instructions known to leave the flags alone and not to read them. */
static int ignores_flags(const X86Insn *i)
{
    uint8_t op = i->opcode;
    if (is_x87(i))
        return !x87_uses_flags(i);
    if (i->map == 0)
        return op == 0x9B || (op >= 0x88 && op <= 0x8B) || op == 0x8D || op == 0xC6 || op == 0xC7 || (op >= 0xB0 && op <= 0xBF) ||
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
    if (lf != X86_LF_LOGIC)
        pair(e, 1, ra, rb, CPU, offsetof(X86Cpu, lf_a));
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

/* the modrm operand at 4 or 8 bytes: its host register, or tmp loaded from memory. */
static int rm_operand(Ctx *x, const X86Insn *i, int size, int tmp)
{
    if (i->mod == 3)
        return host_reg(i->rm);
    ea(x, i);
    ldr(x->e, size, tmp, ADDR, 0);
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
    case ALU_BT: return cc == 2 ? HS : cc == 3 ? LO : -1; /* after a compare of the bit with 1. */
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
    if (target == x->start && !x->fbase && !x->x87_pending) {
        leave_at(x, taken, target);
        sink_record(x);
        exit_const(x, x->next, x->count);
        return;
    }
    sink_record(x);
    exit_const(x, x->next, x->count);
    patch19(e, taken);
    exit_const(x, target, x->count);
}

/* the record of the flags of the inc or dec before the branch being emitted, when it was left for here. t3 holds cf. */
static void sink_record(Ctx *x)
{
    Emit *e = x->e;
    if (x->sink_at != x->index)
        return;
    mov_imm(e, T4, x->sink_lf);
    str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
    str(e, 8, x->sink_reg, CPU, offsetof(X86Cpu, lf_res));
    if (x->sink_live & LIVE_CF)
        str(e, 8, T3, CPU, offsetof(X86Cpu, lf_carry));
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
    if (cond == -3) {
        exit_const(x, target, x->count);
        return 1;
    }
    uint32_t *leave;
    if (cond >= 0) {
        leave = b_cond(e, cond);
    } else {
        x->keep = condition_t3(x, cc);
        leave = cbnz_w(e, T3);
    }
    leave_at(x, leave, target);
    sink_record(x);
    return 1;
}

/* inc and dec keep the carry flag, read before their own flags replace the arm64 ones. */
static int emit_incdec(Ctx *x, const X86Insn *i, int size, int dec)
{
    Emit *e = x->e;
    int lf = X86_LF(dec ? X86_LF_DEC : X86_LF_INC, log2_size(size));
    int sunk = x->sink_at == x->index + 1 ? x->sink_live : 0;
    if ((x->flags_lazy | sunk) & LIVE_CF)
        carry_t3(x);
    if (size >= 4 && i->mod == 3 && host_reg(i->rm) >= 0) {
        int h = host_reg(i->rm);
        x->sink_reg = h;
        x->sink_lf = (uint32_t)lf;
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
    /* a register of 4 or 8 bytes is shifted where it is, unless the lazy flags need what it held. */
    int direct = size >= 4 && i->mod == 3 && !(x->flags_live && x->flags_lazy);
    int from = direct ? host_reg(i->rm) : T0, to = direct ? from : T2;
    if (!direct)
        load_rm(x, i, size, T0);
    if (sub == 7 && size < 4)
        sbfm(e, 4, from, from, 0, bits - 1);
    int work = size == 8 ? 8 : 4;
    if (op <= 0xD1) {
        unsigned n = op <= 0xC1 ? (unsigned)(i->imm & 0xFF) : 1;
        n &= size == 8 ? 63 : 31;
        if (!n) {
            /* no flags, but a 32 bit register is still written: its upper half clears. */
            if (size == 4 && i->mod == 3)
                store_reg(e, 4, from, i->rm, i->rex);
            return x->keep = 1, 1;
        }
        switch (sub) {
        case 4: case 6: ubfm(e, work, to, from, (int)((unsigned)(work * 8) - n) & (work * 8 - 1), work * 8 - 1 - (int)n); break;
        case 5: ubfm(e, work, to, from, (int)n, work * 8 - 1); break;
        case 7: sbfm(e, work, to, from, (int)(n < 32 || work == 8 ? n : 31), work * 8 - 1); break;
        case 1: /* ror */
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)from << 16 | n << 10 | (uint32_t)from << 5 | to);
            break;
        default: /* rol */
            n %= (unsigned)bits;
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)from << 16 | (((unsigned)bits - n) % (unsigned)bits) << 10 |
                       (uint32_t)from << 5 | to);
            break;
        }
        if (x->flags_live) {
            /* only shifts get here with live flags. */
            static const int lf[] = {X86_LF_SHL, X86_LF_SHR, X86_LF_SHL, X86_LF_SAR};
            if (x->flags_lazy) {
                mov_imm(e, T4, X86_LF(lf[sub - 4], log2_size(size)));
                str(e, 4, T4, CPU, offsetof(X86Cpu, lf_op));
                str(e, 8, from, CPU, offsetof(X86Cpu, lf_a));
                mov_imm(e, T1, n);
                str(e, 8, T1, CPU, offsetof(X86Cpu, lf_b));
                str(e, 8, to, CPU, offsetof(X86Cpu, lf_res));
            }
            if (size >= 4) {
                rrr(e, OP_ANDS, size, XZR, to, to);
            } else {
                ubfm(e, 4, S0, to, size * 8, size * 8 - 1); /* lsl s0, t2, #32 - bits */
                rrr(e, OP_ANDS, 4, XZR, S0, S0);
            }
            if (!direct)
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
        rrr(e, ops[sub], work, to, from, T1);
    }
    if (!direct)
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

/* the register holding the xmm or memory source operand: the one xmm lives in, or tmp loaded from memory. */
static int xsrc(Ctx *x, const X86Insn *i, int bytes, int tmp)
{
    if (i->mod == 3)
        return xget(x, i->rm);
    ea(x, i);
    vldr(x->e, bytes, tmp, ADDR, 0);
    return tmp;
}

/* the register xmm n lives in, to change. */
static int xdst(Ctx *x, int n) { return xget(x, n); }
static int xnew(Ctx *x, int n) { return xget(x, n); }

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

/*
 * the rare case of an instruction, which the interpreter runs: its code goes after the block's, out of the way of
 * the one that runs. the instruction must not have changed anything yet where it branches there.
 */
typedef struct Cold {
    uint32_t *branch, *resume;
    int leaves;      /* an exit to target, not the interpreter. */
    uint64_t target;
    Ctx at;
} Cold;

typedef struct Poll {
    uint32_t *branch, *resume, *fail;
} Poll;

typedef struct ColdList {
    uint32_t count, polls;
    Poll poll[280]; /* the block's own, and one for an exit of each instruction and of each of its rare cases. */
    Cold list[136]; /* one for an instruction at most. */
} ColdList;

/* the branch at site, a conditional one with its target not set, goes to the interpreter. */
static uint32_t cold_at(Ctx *x, uint32_t *site)
{
    if (x->cold->count == sizeof(x->cold->list) / sizeof(x->cold->list[0])) {
        x->e->overflow = 1;
        return 0;
    }
    Cold *c = &x->cold->list[x->cold->count];
    c->branch = site;
    c->resume = nullptr;
    c->leaves = 0;
    c->at = *x;
    return x->cold->count++;
}

/* branches to the interpreter on cond. cold_resume says where it comes back, past what the instruction did itself. */
static uint32_t cold_branch(Ctx *x, int cond)
{
    return cold_at(x, b_cond(x->e, cond));
}

/*
 * where the branch at site, a conditional one with its target not set, leaves the block for target: the exit goes
 * after the block's code, so the code that stays in the block runs straight on. a loop of the block alone goes
 * right back to its start when nothing of the x87 state is left to store.
 */
static void leave_at(Ctx *x, uint32_t *site, uint64_t target)
{
    Emit *e = x->e;
    if (target == x->start && !x->fbase && !x->x87_pending) {
        if (!e->overflow)
            *site |= ((uint32_t)(e->code + X86_JIT_LOOP - site) & 0x7FFFFu) << 5;
        return;
    }
    if (x->cold->count == sizeof(x->cold->list) / sizeof(x->cold->list[0])) {
        e->overflow = 1;
        return;
    }
    Cold *c = &x->cold->list[x->cold->count++];
    c->branch = site;
    c->leaves = 1;
    c->target = target;
    c->at = *x;
}

static void cold_resume(Ctx *x, uint32_t stub) { x->cold->list[stub].resume = x->e->p; }

static uint32_t poll(Ctx *x)
{
    Emit *e = x->e;
    ColdList *c = x->cold;
    if (c->polls == sizeof(c->poll) / sizeof(c->poll[0])) {
        e->overflow = 1;
        return 0;
    }
    put(e, 0xF1000400u | (uint32_t)POLL << 5 | POLL); /* subs x29, x29, #1 */
    c->poll[c->polls].branch = b_cond(e, EQ);
    c->poll[c->polls].resume = e->p;
    c->poll[c->polls].fail = nullptr;
    return c->polls++;
}

static void poll_fails(Ctx *x, uint32_t poll) { x->cold->poll[poll].fail = x->e->p; }

static void cold_emit(Ctx *x)
{
    Emit *e = x->e;
    /* the poll at the block's start found something: the dispatcher takes over there. */
    poll_fails(x, 0);
    exit_to(e, -1, x->start, 0);
    for (uint32_t k = 0; k < x->cold->count; k++) {
        Cold *c = &x->cold->list[k];
        patch19(e, c->branch);
        if (c->leaves) {
            exit_const(&c->at, c->target, x->count);
            continue;
        }
        c->at.final = 0;
        emit_fallback(&c->at, c->at.insn);
        put(e, 0x14000000u | ((uint32_t)(c->resume - e->p) & 0x3FFFFFFu));
    }
    /* the exits emitted above polled too. */
    for (uint32_t k = 0; k < x->cold->polls && !e->overflow; k++) {
        Poll *p = &x->cold->poll[k];
        patch19(e, p->branch);
        mov_imm(e, POLL, X86_POLL_EVERY);
        ldr(e, 8, 17, CPU, offsetof(X86Cpu, suspend_pending));
        ldr(e, 4, 17, 17, 0);
        put(e, 0x35000000u | ((uint32_t)(p->fail - e->p) & 0x7FFFFu) << 5 | 17u); /* cbnz w17, fail */
        ldr(e, 8, 17, CPU, offsetof(X86Cpu, code_epoch));
        ldr(e, 4, 17, 17, 0);
        ldr(e, 4, T5, CPU, offsetof(X86Cpu, blocks_epoch));
        put(e, 0x6B00001Fu | (uint32_t)T5 << 16 | 17u << 5); /* cmp w17, w15 */
        put(e, 0x54000001u | ((uint32_t)(p->fail - e->p) & 0x7FFFFu) << 5); /* b.ne fail */
        put(e, 0x14000000u | ((uint32_t)(p->resume - e->p) & 0x3FFFFFFu));
    }
}

/* a nan result goes through the interpreter, which applies the x86 nan rules. checks lane 0 of a scalar result. */
static uint32_t scalar_nan_check(Ctx *x, int dbl, int v)
{
    put(x->e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)v << 16 | (uint32_t)v << 5);
    return cold_branch(x, VS);
}

/* checks every lane of a packed result. */
static uint32_t packed_nan_check(Ctx *x, int dbl, int v)
{
    Emit *e = x->e;
    v3(e, dbl ? 0x4E60E400u : 0x4E20E400u, V3, v, v); /* fcmeq v3, v, v */
    put(e, (dbl ? 0x0EA12800u : 0x0E612800u) | (uint32_t)V3 << 5 | V3); /* xtn */
    put(e, 0x9E660000u | (uint32_t)V3 << 5 | 16);                       /* fmov x16, d3 */
    put(e, 0xB100061Fu);                                                /* cmn x16, #1 */
    return cold_branch(x, NE);
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
    x->keep = 1; /* every case but the arithmetic, conversions to integers and compares. */
    if (pfx == 0x66 || op == 0x70) {
        int r = emit_simd_int(x, i);
        if (r >= 0)
            return r;
    }
    switch (op) {
    case 0x10: /* movups, movupd, movss, movsd */
        if (i->mod == 3) {
            int from = xget(x, i->rm);
            if (scalar)
                ins(e, lane, xdst(x, i->reg), 0, from, 0);
            else
                vmov(e, xnew(x, i->reg), from);
        } else {
            /* the load faults before the register changes. */
            ea(x, i);
            vldr(e, scalar ? lane : 16, xnew(x, i->reg), ADDR, 0);
        }
        return 1;
    case 0x11: {
        int from = xget(x, i->reg);
        if (i->mod == 3 && scalar) {
            ins(e, lane, xdst(x, i->rm), 0, from, 0);
        } else if (i->mod == 3) {
            vmov(e, xnew(x, i->rm), from);
        } else {
            ea(x, i);
            vstr(e, scalar ? lane : 16, from, ADDR, 0);
        }
        return 1;
    }
    case 0x28: /* movaps, movapd */
    case 0x6F: /* movdqa, movdqu */
        if ((op == 0x28 && scalar) || (op == 0x6F && pfx != 0x66 && pfx != 0xF3))
            return 0;
        if (i->mod == 3) {
            int from = xget(x, i->rm);
            vmov(e, xnew(x, i->reg), from);
        } else {
            ea(x, i);
            vldr(e, 16, xnew(x, i->reg), ADDR, 0);
        }
        return 1;
    case 0x2B: /* movntps, movntpd */
    case 0xE7: /* movntdq */
        if (i->mod == 3 || scalar || (op == 0xE7 && pfx != 0x66))
            return 0;
        ea(x, i);
        vstr(e, 16, xget(x, i->reg), ADDR, 0);
        return 1;
    case 0x50: { /* movmskps, movmskpd: the sign of each element, gathered into the low byte. */
        if (scalar || i->mod != 3)
            return 0;
        int from = xget(x, i->rm), to = host_reg(i->reg);
        if (dbl) {
            put(e, 0x6F410400u | (uint32_t)from << 5 | V0); /* ushr v0.2d, from, #63 */
            put(e, 0x4E083C00u | (uint32_t)V0 << 5 | T0);   /* umov x, v0.d[0] */
            put(e, 0x4E183C00u | (uint32_t)V0 << 5 | T1);   /* umov x, v0.d[1] */
            put(e, 0x2A000000u | (uint32_t)T1 << 16 | 1u << 10 | (uint32_t)T0 << 5 | (uint32_t)to);
        } else {
            put(e, 0x6F210400u | (uint32_t)from << 5 | V0); /* ushr v0.4s, from, #31 */
            put(e, 0x6F611400u | (uint32_t)V0 << 5 | V0);   /* usra v0.2d, v0.2d, #31: each half's two bits together */
            put(e, 0x0E013C00u | (uint32_t)V0 << 5 | T0);   /* umov w, v0.b[0] */
            put(e, 0x0E113C00u | (uint32_t)V0 << 5 | T1);   /* umov w, v0.b[8] */
            put(e, 0x2A000000u | (uint32_t)T1 << 16 | 2u << 10 | (uint32_t)T0 << 5 | (uint32_t)to);
        }
        return 1;
    }
    case 0xC4: { /* pinsrw into an xmm register */
        if (pfx != 0x66)
            return 0;
        int word = T0;
        if (i->mod == 3) {
            word = host_reg(i->rm);
        } else {
            ea(x, i);
            ldr(e, 2, T0, ADDR, 0);
        }
        put(e, 0x4E001C00u | ((uint32_t)(i->imm & 7) << 2 | 2u) << 16 | (uint32_t)word << 5 | (uint32_t)xdst(x, i->reg));
        return 1;
    }
    case 0xC5: /* pextrw from an xmm register */
        if (pfx != 0x66 || i->mod != 3)
            return 0;
        put(e, 0x0E003C00u | ((uint32_t)(i->imm & 7) << 2 | 2u) << 16 | (uint32_t)xget(x, i->rm) << 5 | (uint32_t)host_reg(i->reg));
        return 1;
    case 0x29:
    case 0x7F: {
        if ((op == 0x29 && scalar) || (op == 0x7F && pfx != 0x66 && pfx != 0xF3))
            return 0;
        int from = xget(x, i->reg);
        if (i->mod == 3)
            vmov(e, xnew(x, i->rm), from);
        else {
            ea(x, i);
            vstr(e, 16, from, ADDR, 0);
        }
        return 1;
    }
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
        {
            int b = xsrc(x, i, 16, V1), a = xdst(x, i->reg);
            v3(e, (op == 0x14 ? 0x0E003800u : 0x0E007800u) | 0x40000000u | (dbl ? 3u : 2u) << 22, a, a, b);
        }
        return 1;
    case 0x54: case 0x55: case 0x56: case 0x57: /* andps, andnps, orps, xorps and the pd forms */
    case 0xDB: case 0xDF: case 0xEB: case 0xEF: { /* pand, pandn, por, pxor */
        if (op >= 0xDB ? pfx != 0x66 : scalar)
            return 0;
        int k = op >= 0xDB ? (op == 0xDB ? 0 : op == 0xDF ? 1 : op == 0xEB ? 2 : 3) : op - 0x54;
        if (k == 3 && i->mod == 3 && i->rm == i->reg) { /* a register cleared */
            vzero(e, xnew(x, i->reg));
            return 1;
        }
        int b = xsrc(x, i, 16, V1), a = xdst(x, i->reg);
        if (k == 1)
            v3(e, V_BIC, a, b, a); /* ~dst & src */
        else
            v3(e, k == 0 ? V_AND : k == 2 ? V_ORR : V_EOR, a, a, b);
        return 1;
    }
    case 0x58: case 0x59: case 0x5C: case 0x5E: case 0x51: { /* add, mul, sub, div, sqrt */
        x->keep = 0;
        static const uint32_t vec[] = {0x4E20D400u, 0x6E20DC00u, 0, 0, 0x4EA0D400u, 0, 0x6E20FC00u};
        static const uint32_t sca[] = {0x1E202800u, 0x1E200800u, 0, 0, 0x1E203800u, 0, 0x1E201800u};
        int b = xsrc(x, i, scalar ? lane : 16, V1), a = xget(x, i->reg);
        if (x86_jit_afp) {
            /* nans come out as x86 makes them, and a scalar operation keeps the rest of its first operand, or of the
             * register it writes when it has one operand. */
            if (op == 0x51)
                put(e, (scalar ? 0x1E21C000u : 0x6EA1F800u) | sz | (uint32_t)b << 5 | (uint32_t)a);
            else
                v3(e, (scalar ? sca : vec)[op - 0x58] | sz, a, a, b);
            return x->keep = 1, 1;
        }
        /* the result is made aside: a nan leaves the registers to the interpreter as they were. */
        uint32_t stub;
        if (scalar) {
            if (op == 0x51)
                put(e, 0x1E21C000u | sz | (uint32_t)b << 5 | V2);
            else
                v3(e, sca[op - 0x58] | sz, V2, a, b);
            stub = scalar_nan_check(x, dbl, V2);
            ins(e, lane, xdst(x, i->reg), 0, V2, 0);
        } else {
            if (op == 0x51)
                put(e, 0x6EA1F800u | sz | (uint32_t)b << 5 | V2);
            else
                v3(e, vec[op - 0x58] | sz, V2, a, b);
            stub = packed_nan_check(x, dbl, V2);
            vmov(e, xdst(x, i->reg), V2);
        }
        cold_resume(x, stub);
        return 1;
    }
    case 0x5D: /* min: a < b ? a : b, the second operand for nans and equal values. */
    case 0x5F: { /* max: a > b ? a : b */
        int b = xsrc(x, i, scalar ? lane : 16, V1), a = xdst(x, i->reg);
        if (x86_jit_afp) { /* fmin and fmax give the second operand for nans and equal values there, as x86 does. */
            v3(e, (scalar ? (op == 0x5D ? 0x1E205800u : 0x1E204800u) : (op == 0x5D ? 0x4EA0F400u : 0x4E20F400u)) | sz, a, a, b);
            return 1;
        }
        if (op == 0x5D)
            v3(e, 0x6EA0E400u | sz, V2, b, a); /* fcmgt v2, b, a */
        else
            v3(e, 0x6EA0E400u | sz, V2, a, b); /* fcmgt v2, a, b */
        v3(e, V_BSL, V2, a, b);
        if (scalar)
            ins(e, lane, a, 0, V2, 0);
        else
            vmov(e, a, V2);
        return 1;
    }
    case 0xC2: { /* cmpps, cmppd, cmpss, cmpsd */
        int pred = (int)(i->imm & 7);
        int b = xsrc(x, i, scalar ? lane : 16, V1), a = xdst(x, i->reg);
        if (x86_jit_afp && scalar && pred < 3) {
            /* the scalar compares keep the rest of their second operand there, as the processor was seen to do. */
            static const uint32_t ops[3] = {0x5E20E400u, 0x7EA0E400u, 0x7E20E400u}; /* fcmeq, fcmgt, fcmge */
            v3(e, ops[pred] | sz, a, b, a);
            return 1;
        }
        switch (pred & 3) {
        case 0: v3(e, 0x4E20E400u | sz, V2, a, b); break; /* eq: fcmeq */
        case 1: v3(e, 0x6EA0E400u | sz, V2, b, a); break; /* lt: fcmgt b, a */
        case 2: v3(e, 0x6E20E400u | sz, V2, b, a); break; /* le: fcmge b, a */
        default: /* unordered: not both ordered */
            v3(e, 0x4E20E400u | sz, V2, a, a);
            v3(e, 0x4E20E400u | sz, V3, b, b);
            v3(e, V_AND, V2, V2, V3);
            pred ^= 4;
            break;
        }
        if (pred & 4)
            put(e, 0x6E205800u | (uint32_t)V2 << 5 | V2); /* not */
        if (scalar)
            ins(e, lane, a, 0, V2, 0);
        else
            vmov(e, a, V2);
        return 1;
    }
    case 0x2E: /* ucomiss, ucomisd */
    case 0x2F: { /* comiss, comisd */
        if (scalar)
            return 0;
        x->keep = 0;
        int b = xsrc(x, i, lane, V1), a = xget(x, i->reg);
        put(e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)b << 16 | (uint32_t)a << 5);
        if (x->flags_lazy) {
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
        }
        set_nzcv(x, ALU_UCOMI);
        return 1;
    }
    case 0x2A: { /* cvtsi2ss, cvtsi2sd */
        if (!scalar)
            return 0;
        int wide = (i->rex & 8) != 0;
        int from = rm_operand(x, i, wide ? 8 : 4, T0);
        put(e, (wide ? 0x80000000u : 0) | (dbl ? 0x1E620000u : 0x1E220000u) | (uint32_t)from << 5 |
                   (uint32_t)(x86_jit_afp ? xdst(x, i->reg) : V2));
        if (!x86_jit_afp)
            ins(e, lane, xdst(x, i->reg), 0, V2, 0);
        return 1;
    }
    case 0x2C:
    case 0x2D: { /* cvttss2si, cvttsd2si: nans and out of range values take the interpreter's indefinite value. */
        if (!scalar)
            return 0;
        x->keep = 0;
        int wide = (i->rex & 8) != 0;
        int from = xsrc(x, i, lane, V1), to = host_reg(i->reg);
        if (op == 0x2D) {
            /* cvtss2si, cvtsd2si: rounded as mxcsr says, which fpcr mirrors. */
            put(e, (dbl ? 0x1E67C000u : 0x1E27C000u) | (uint32_t)from << 5 | V2); /* frinti */
            from = V2;
        }
        /* the interpreter writes the register again when it runs, so it is converted into here. */
        put(e, (wide ? 0x80000000u : 0) | (dbl ? 0x1E780000u : 0x1E380000u) | (uint32_t)from << 5 | (uint32_t)to);
        /* saturated: x - min is 0 or -1, all ones, for the two limits. a nan converts to 0 and fails with them. */
        mov_imm(e, T1, wide ? 0x8000000000000000ull : 0x80000000ull);
        rrr(e, OP_EOR, wide ? 8 : 4, T1, to, T1);
        add_imm(e, wide ? 8 : 4, T1, T1, 1);
        put(e, (dbl ? 0x1E602000u : 0x1E202000u) | (uint32_t)from << 16 | (uint32_t)from << 5); /* fcmp */
        put(e, (wide ? 0xFA400800u : 0x7A400800u) | 1u << 16 | (uint32_t)VC << 12 | (uint32_t)T1 << 5 | 4u); /* ccmp t1, #1, #z, vc */
        cold_resume(x, cold_branch(x, LS));
        return 1;
    }
    case 0x5A: { /* cvtss2sd, cvtsd2ss, and cvtps2pd, cvtpd2ps */
        if (!scalar) {
            int wide = xsrc(x, i, dbl ? 16 : 8, V1);
            put(e, (dbl ? 0x0E616800u : 0x0E617800u) | (uint32_t)wide << 5 | (uint32_t)xnew(x, i->reg)); /* fcvtn, fcvtl */
            return 1;
        }
        int from = xsrc(x, i, lane, V1);
        put(e, (dbl ? 0x1E624000u : 0x1E22C000u) | (uint32_t)from << 5 | (uint32_t)(x86_jit_afp ? xdst(x, i->reg) : V2));
        if (!x86_jit_afp)
            ins(e, dbl ? 4 : 8, xdst(x, i->reg), 0, V2, 0);
        return 1;
    }
    case 0xE6: /* cvtdq2pd */
        if (pfx != 0xF3)
            return 0;
        load_xsrc(x, i, 16, V1);
        put(e, 0x0F20A400u | (uint32_t)V1 << 5 | V0); /* sxtl v0.2d, v1.2s */
        put(e, 0x4E61D800u | (uint32_t)V0 << 5 | V0); /* scvtf v0.2d */
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
    x87_store(x);
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
    x87_reload(x);
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
/* ------------------------------------------------------------ sha256 */

uint32_t x86_jit_sha256;

#define V_ZIP1_2D 0x4EC03800u
#define V_ZIP2_2D 0x4EC07800u

static void rev64_4s(Emit *e, int vd, int vn) { put(e, 0x4EA00800u | (uint32_t)vn << 5 | (uint32_t)vd); }

/* instructions that write no xmm register but the one in modrm.reg, if any. */
static int writes_xmm_reg_only(const X86Insn *i)
{
    uint8_t op = i->opcode;
    if (i->map == 0 || i->map == 2)
        return 1;
    if (i->map == 3)
        return op < 0x60 || op > 0x63; /* pcmpestrm and pcmpistrm write xmm0. */
    return op == 0x10 || op == 0x12 || op == 0x14 || op == 0x15 || op == 0x16 || op == 0x1F || op == 0x28 ||
           (op >= 0x51 && op <= 0x70) || (op >= 0x74 && op <= 0x76) || op == 0xC2 || op == 0xC6 ||
           (op >= 0xD1 && op <= 0xD5) || (op >= 0xD8 && op <= 0xE5) || (op >= 0xE8 && op <= 0xEF) ||
           (op >= 0xF1 && op <= 0xFE);
}

/*
 * sha256 code runs four rounds as sha256rnds2 x, y, then the keys moved down by pshufd xmm0, xmm0, 0xe, psrldq
 * xmm0, 8 or punpckhqdq xmm0, xmm0, then sha256rnds2 y, x, as openssl and linux do. the arm64 instructions run four rounds, so the first one's
 * already computed the second's result. returns the index of the second one when the block holds the pattern and
 * nothing in between writes x, y or xmm0 or is another sha256rnds2, else 0.
 */
static uint32_t sha256_pair(const Ctx *x, const X86Insn *first)
{
    const X86Insn *all = x->insn - x->index;
    int a = first->reg, b = first->rm, keyed = 0;
    if (first->mod != 3 || a == b || !a || !b)
        return 0;
    for (uint32_t k = x->index + 1; k < x->count; k++) {
        const X86Insn *i = &all[k];
        if (i->map == 2 && i->opcode == 0xCB && !i->mandatory)
            return keyed && i->mod == 3 && i->reg == b && i->rm == a ? k : 0;
        if (i->map == 0)
            continue;
        int shuffle = i->map == 1 && i->mandatory == 0x66 && i->mod == 3 && i->rm == 0 &&
                      ((i->opcode == 0x70 && i->reg == 0 && (i->imm & 0xFF) == 0x0E) ||
                       (i->opcode == 0x6D && i->reg == 0) ||
                       (i->opcode == 0x73 && ((i->modrm >> 3) & 7) == 3 && (i->imm & 0xFF) == 8));
        if (shuffle && !keyed) {
            keyed = 1;
            continue;
        }
        if (!writes_xmm_reg_only(i) || i->reg == a || i->reg == b || i->reg == 0)
            return 0;
    }
    return 0;
}

/*
 * sha256rnds2, sha256msg1 and sha256msg2 with the arm64 instructions. the x86 states hold abef and cdgh with a in
 * the top lane, the arm64 ones abcd and efgh with a in lane 0. sha256h and sha256h2 run four rounds with the four
 * keys of xmm0, of which x86 uses two: after them c, d, g and h are the a, b, e and f of two rounds.
 */
static int emit_sha256(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    if (!x86_jit_sha256)
        return 0;
    if (i->opcode == 0xCB && x->sha_second && x->sha_second == x->index) {
        vldr(e, 16, V0, CPU, offsetof(X86Cpu, jit_sha));
        xstore(x, V0, i->reg);
        x->sha_second = 0;
        return x->keep = 1, 1;
    }
    xload(x, 16, V0, i->reg);
    load_xsrc(x, i, 16, V1);
    if (i->opcode == 0xCC) { /* sha256msg1 */
        put(e, 0x5E282800u | (uint32_t)V1 << 5 | V0); /* sha256su0 */
    } else if (i->opcode == 0xCD) { /* sha256msg2: sha256su1 without its w[t - 7] term */
        put(e, 0x4E041FE0u | V1); /* mov v1.s[0], wzr */
        vzero(e, V2);
        put(e, 0x5E006000u | (uint32_t)V1 << 16 | (uint32_t)V2 << 5 | V0); /* sha256su1 */
    } else {
        int key = xget(x, 0);
        v3(e, V_ZIP2_2D, V2, V1, V0);
        rev64_4s(e, V2, V2); /* abcd */
        v3(e, V_ZIP1_2D, V3, V1, V0);
        rev64_4s(e, V3, V3); /* efgh */
        vmov(e, 4, V2);
        put(e, 0x5E004000u | (uint32_t)key << 16 | (uint32_t)V3 << 5 | V2); /* sha256h */
        put(e, 0x5E005000u | (uint32_t)key << 16 | 4u << 5 | V3);         /* sha256h2 */
        v3(e, V_ZIP2_2D, V0, V3, V2);
        rev64_4s(e, V0, V0);
        uint32_t second = sha256_pair(x, i);
        if (second) {
            v3(e, V_ZIP1_2D, V1, V3, V2);
            rev64_4s(e, V1, V1);
            vstr(e, 16, V1, CPU, offsetof(X86Cpu, jit_sha));
            x->sha_second = second;
        }
    }
    xstore(x, V0, i->reg);
    return x->keep = 1, 1;
}

/* ------------------------------------------------------------ div and idiv */

enum { X16 = 16, X17 = 17 };

static void div_op(Emit *e, int size, int is_signed, int rd, int rn, int rm)
{
    put(e, (size == 8 ? 0x9AC00800u : 0x1AC00800u) | (is_signed ? 0x400u : 0) | (uint32_t)rm << 16 | (uint32_t)rn << 5 |
               (uint32_t)rd);
}

/* rd = ra - rn * rm. */
static void msub(Emit *e, int rd, int rn, int rm, int ra)
{
    put(e, 0x9B008000u | (uint32_t)rm << 16 | (uint32_t)ra << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
}

/* rd = rn | rm << shift. */
static void orr_shifted(Emit *e, int rd, int rn, int rm, int shift)
{
    put(e, 0xAA000000u | (uint32_t)rm << 16 | (uint32_t)shift << 10 | (uint32_t)rn << 5 | (uint32_t)rd);
}

/* lslv or lsrv. */
static void shift_reg(Emit *e, int right, int rd, int rn, int rm)
{
    put(e, (right ? 0x9AC02400u : 0x9AC02000u) | (uint32_t)rm << 16 | (uint32_t)rn << 5 | (uint32_t)rd);
}

static uint32_t *cbnz_x(Emit *e, int rt)
{
    uint32_t *at = e->p;
    put(e, 0xB5000000u | (uint32_t)rt);
    return at;
}

/* cbz rt back to an earlier instruction. */
static void cbz_to(Emit *e, int rt, const uint32_t *to)
{
    put(e, 0xB4000000u | ((uint32_t)(to - e->p) & 0x7FFFF) << 5 | (uint32_t)rt);
}

/*
 * corrects a quotient digit q estimated from the top digit of the divisor alone: at most twice, while it is too large
 * for a digit or q times the low digit exceeds the remainder rhat with the next dividend digit un. ta and tb are free.
 */
static void div_digit_fix(Emit *e, int q, int un, int ta, int tb)
{
    enum { VN1 = T4, VN0 = T5, RHAT = X17 };
    const uint32_t *again = e->p;
    ubfm(e, 8, ta, q, 32, 63);
    uint32_t *adjust = cbnz_x(e, ta);
    put(e, 0x9B007C00u | (uint32_t)VN0 << 16 | (uint32_t)q << 5 | (uint32_t)ta); /* mul ta, q, vn0 */
    orr_shifted(e, tb, un, RHAT, 32);
    rrr(e, OP_SUBS, 8, XZR, ta, tb);
    uint32_t *done = b_cond(e, LS);
    patch19(e, adjust);
    put(e, 0xD1000400u | (uint32_t)q << 5 | (uint32_t)q); /* sub q, q, #1 */
    rrr(e, OP_ADD, 8, RHAT, RHAT, VN1);
    ubfm(e, 8, ta, RHAT, 32, 63);
    cbz_to(e, ta, again);
    patch19(e, done);
}

/*
 * T3:T0 / T1 with T3 below T1 into the quotient T2 and the remainder T3, as the interpreter's div128: the divisor
 * shifted to its top bit, then two 32 bit quotient digits. clobbers T4, T5, S0, ADDR, x16 and x17.
 */
static void div_wide(Emit *e)
{
    enum { S = S0, VN1 = T4, VN0 = T5, UN0 = T0, UN1 = X16, RHAT = X17, Q1 = T2, Q0 = X16, TMP = ADDR };
    put(e, 0xDAC01000u | (uint32_t)T1 << 5 | S); /* clz */
    shift_reg(e, 0, T1, T1, S);
    shift_reg(e, 0, T3, T3, S);
    /* the dividend's top bits moving up: lo >> 1 >> (63 - s), as lo >> (64 - s) would not give 0 for s = 0. */
    ubfm(e, 8, X16, T0, 1, 63);
    put(e, 0xAA2003E0u | (uint32_t)S << 16 | X17); /* mvn: lsrv takes ~s as 63 - s */
    shift_reg(e, 1, X16, X16, X17);
    rrr(e, OP_ORR, 8, T3, T3, X16);
    shift_reg(e, 0, T0, T0, S);
    ubfm(e, 8, VN1, T1, 32, 63);
    mov_reg(e, 4, VN0, T1);
    ubfm(e, 8, UN1, T0, 32, 63);
    mov_reg(e, 4, UN0, T0);
    div_op(e, 8, 0, Q1, T3, VN1);
    msub(e, RHAT, Q1, VN1, T3);
    div_digit_fix(e, Q1, UN1, T3, TMP);
    orr_shifted(e, TMP, UN1, RHAT, 32);
    msub(e, TMP, Q1, VN0, TMP);
    div_op(e, 8, 0, Q0, TMP, VN1);
    msub(e, RHAT, Q0, VN1, TMP);
    div_digit_fix(e, Q0, UN0, T3, TMP);
    orr_shifted(e, TMP, UN0, RHAT, 32);
    msub(e, TMP, Q0, VN0, TMP);
    shift_reg(e, 1, T3, TMP, S);
    orr_shifted(e, T2, Q0, Q1, 32);
}

/*
 * div and idiv of 32 and 64 bits. a zero divisor, a quotient too large and a signed 128 bit dividend go to the
 * interpreter, which raises the faults. x86 leaves the flags undefined, the interpreter keeps them, and so does this.
 */
static int emit_div(Ctx *x, const X86Insn *i, int size, int is_signed)
{
    Emit *e = x->e;
    uint32_t *slow[4], *done = nullptr;
    int n = 0;
    load_rm(x, i, size, T1);
    load_reg(e, size, T0, X86_RAX, 1);
    load_reg(e, size, T3, X86_RDX, 1);
    slow[n++] = cbz_x(e, T1);
    if (size == 4) {
        orr_shifted(e, T0, T0, T3, 32);
        if (is_signed)
            sbfm(e, 8, T1, T1, 0, 31);
        div_op(e, 8, is_signed, T2, T0, T1);
        if (is_signed) {
            sbfm(e, 8, X16, T2, 0, 31);
            rrr(e, OP_SUBS, 8, XZR, X16, T2);
            slow[n++] = b_cond(e, NE);
        } else {
            ubfm(e, 8, X16, T2, 32, 63);
            slow[n++] = cbnz_x(e, X16);
        }
        msub(e, T3, T2, T1, T0);
    } else if (is_signed) {
        sbfm(e, 8, X16, T0, 63, 63);
        rrr(e, OP_SUBS, 8, XZR, T3, X16);
        slow[n++] = b_cond(e, NE);
        put(e, 0xB100041Fu | (uint32_t)T1 << 5); /* cmn: the divisor is -1 */
        uint32_t *fits = b_cond(e, NE);
        mov_imm(e, X16, 1ull << 63);
        rrr(e, OP_SUBS, 8, XZR, T0, X16);
        slow[n++] = b_cond(e, EQ);
        patch19(e, fits);
        div_op(e, 8, 1, T2, T0, T1);
        msub(e, T3, T2, T1, T0);
    } else {
        uint32_t *wide = cbnz_x(e, T3);
        div_op(e, 8, 0, T2, T0, T1);
        msub(e, T3, T2, T1, T0);
        uint32_t *narrow_done = b_always(e);
        patch19(e, wide);
        rrr(e, OP_SUBS, 8, XZR, T3, T1);
        slow[n++] = b_cond(e, HS);
        div_wide(e);
        patch26(e, narrow_done);
    }
    store_reg(e, size, T2, X86_RAX, 1);
    store_reg(e, size, T3, X86_RDX, 1);
    done = b_always(e);
    for (int k = 0; k < n; k++)
        patch19(e, slow[k]);
    emit_fallback(x, i);
    patch26(e, done);
    return 1;
}


/* ------------------------------------------------------------ x87 */

/*
 * the x87 stack, in double precision as the interpreter keeps it. within a block the registers a program uses live
 * in v4 to v11: x17 holds the top the stack had when the block first used it, the base, and v4 + n holds the
 * register n places past it. pushes and pops only move the block's own count, and the top, the tags and the changed
 * registers go to the cpu where something reads them: at the exits and around calls, with the last instruction
 * pointer and opcode. a fault takes them from the faulting context, see x86_jit_fault.
 */
enum { FCACHE = 4 };

static uint32_t OFF_FPR(void) { return (uint32_t)offsetof(X86Cpu, fpr); }
static void fmov(Emit *e, int vd, int vn) { put(e, 0x1E604000u | (uint32_t)vn << 5 | (uint32_t)vd); }

static void x87_base(Ctx *x)
{
    if (!x->fbase)
        ldr(x->e, 1, 17, CPU, offsetof(X86Cpu, ftop));
    x->fbase = 1;
}

/* reg = the address of the register n places past the base, less the offset of the registers. */
static void x87_addr(Ctx *x, int n, int reg)
{
    Emit *e = x->e;
    x87_base(x);
    if (!n) {
        add_shifted(e, reg, CPU, 17, 3);
        return;
    }
    add_imm(e, 4, reg, 17, n);
    put(e, 0x12000800u | (uint32_t)reg << 5 | (uint32_t)reg); /* and w, w, #7 */
    add_shifted(e, reg, CPU, reg, 3);
}

/* the host register of st(i), loaded. */
static int x87_st(Ctx *x, int i)
{
    int n = (x->fcur + i) & 7;
    if (!(x->floaded & 1u << n)) {
        x87_addr(x, n, 16);
        vldr(x->e, 8, FCACHE + x->fmap[n], 16, OFF_FPR());
        x->floaded |= (uint8_t)(1u << n);
    }
    return FCACHE + x->fmap[n];
}

/* the host register of st(i) to write: all of it when whole is set, so that it is not loaded. */
static int x87_write(Ctx *x, int i, int whole)
{
    int n = (x->fcur + i) & 7;
    if (whole)
        x87_base(x);
    else
        x87_st(x, i);
    x->floaded |= (uint8_t)(1u << n);
    x->fdirty |= (uint8_t)(1u << n);
    return FCACHE + x->fmap[n];
}

/* the host register the next push writes: a load may go straight into it, which leaves it alone when it faults. */
static int x87_next(const Ctx *x) { return FCACHE + x->fmap[(x->fcur - 1) & 7]; }

/* the host register of the new st(0). */
static int x87_push(Ctx *x)
{
    x->fcur = (x->fcur - 1) & 7;
    x->fmoved = 1;
    x->ftag_set |= (uint8_t)(1u << x->fcur);
    x->ftag_clear &= (uint8_t)~(1u << x->fcur);
    return x87_write(x, 0, 1);
}

static void x87_pop(Ctx *x)
{
    x87_base(x);
    x->ftag_clear |= (uint8_t)(1u << x->fcur);
    x->ftag_set &= (uint8_t)~(1u << x->fcur);
    x->fcur = (x->fcur + 1) & 7;
    x->fmoved = 1;
}

/* t0 = a mask of registers counted from the base, as one of the registers themselves in its low byte. */
static void x87_rotate(Emit *e, uint32_t mask)
{
    mov_imm(e, T0, mask);
    put(e, 0x1AC02000u | 17u << 16 | (uint32_t)T0 << 5 | T0);                       /* lsl w, w, w17 */
    put(e, 0x2A400000u | (uint32_t)T0 << 16 | 8u << 10 | (uint32_t)T0 << 5 | T0); /* orr w, w, w, lsr #8 */
}

/*
 * stores what the block holds of the x87 state. the state the translator tracks stays as it is: what follows may
 * be a path that leaves, and storing again does no harm. only x16 is lost.
 */
static void x87_store(Ctx *x)
{
    Emit *e = x->e;
    if (x->x87_pending) {
        mov_imm(e, 16, x->x87_fop);
        str(e, 2, 16, CPU, offsetof(X86Cpu, fop));
        mov_imm(e, 16, x->x87_ip);
        str(e, 8, 16, CPU, offsetof(X86Cpu, fpu_ip));
    }
    if (!x->fbase)
        return;
    /* what a popped register held is left where it is: nothing reads it before it is pushed again. */
    for (int n = 0; n < 8; n++)
        if (x->fdirty & ~x->ftag_clear & 1u << n) {
            x87_addr(x, n, 16);
            vstr(e, 8, FCACHE + x->fmap[n], 16, OFF_FPR());
        }
    if (x->fcur)
        x->fmoved = 2; /* stored away from where it began. */
    if (x->fmoved == 2) {
        add_imm(e, 4, 16, 17, x->fcur);
        put(e, 0x12000800u | 16u << 5 | 16u);
        str(e, 1, 16, CPU, offsetof(X86Cpu, ftop));
    }
    if (x->ftag_set | x->ftag_clear) {
        put(e, 0xF81F0FE0u | T0); /* str t0, [sp, #-16]! */
        ldr(e, 1, 16, CPU, offsetof(X86Cpu, ftag));
        if (x->ftag_set) {
            x87_rotate(e, x->ftag_set);
            rrr(e, OP_ORR, 4, 16, 16, T0);
        }
        if (x->ftag_clear) {
            x87_rotate(e, x->ftag_clear);
            rrr(e, 0x0A200000u, 4, 16, 16, T0);
        }
        str(e, 1, 16, CPU, offsetof(X86Cpu, ftag));
        put(e, 0xF84107E0u | T0); /* ldr t0, [sp], #16 */
    }
}

/* after a call that x87_store went before: the base and the registers held, which c code does not keep. */
static void x87_reload(Ctx *x)
{
    Emit *e = x->e;
    if (!x->fbase)
        return;
    ldr(e, 1, 17, CPU, offsetof(X86Cpu, ftop));
    if (x->fcur) {
        add_imm(e, 4, 17, 17, -x->fcur);
        put(e, 0x12000800u | 17u << 5 | 17u);
    }
    for (int n = 0; n < 8; n++)
        if (x->floaded & 1u << n) {
            x87_addr(x, n, 16);
            vldr(e, 8, FCACHE + x->fmap[n], 16, OFF_FPR());
        }
}

/* after x87_store and something that may have changed the x87 state: the block starts over from the cpu's. */
static void x87_reset(Ctx *x)
{
    x->fbase = 0;
    x->fcur = x->fmoved = 0;
    for (int n = 0; n < 8; n++)
        x->fmap[n] = (uint8_t)n;
    x->floaded = x->fdirty = x->ftag_set = x->ftag_clear = 0;
    x->x87_pending = 0;
    x->x87_seen = 0;
}

/*
 * where the host keeps the rest of the register a scalar operation writes, see x86_jit_afp, one with a single operand
 * waits for what the register held. so those are done in place on a copy, which the register then takes whole.
 * v = the signed integer in t2 as a double.
 */
static void x87_int(Emit *e, int v)
{
    put(e, 0x9E670000u | (uint32_t)T2 << 5 | (uint32_t)v);       /* fmov d, x */
    put(e, 0x5E61D800u | (uint32_t)v << 5 | (uint32_t)v);        /* scvtf d, d */
}

/* vd = a op b, as the interpreter's arith. */
static void x87_arith(Emit *e, int op, int vd, int a, int b)
{
    static const uint32_t ops[8] = {0x1E602800u, 0x1E600800u, 0, 0, 0x1E603800u, 0x1E603800u, 0x1E601800u, 0x1E601800u};
    int n = op == 5 || op == 7 ? b : a, m = op == 5 || op == 7 ? a : b;
    put(e, ops[op] | (uint32_t)m << 16 | (uint32_t)n << 5 | (uint32_t)vd);
}

/* the condition codes of a compared with b. loses t0 to t2 and the arm64 flags. */
static void x87_compare(Ctx *x, int a, int b)
{
    Emit *e = x->e;
    x->keep = 0;
    put(e, 0x1E602000u | (uint32_t)b << 16 | (uint32_t)a << 5); /* fcmp */
    cset(e, T0, MI);
    cset(e, T1, EQ);
    mov_imm(e, T2, 0x4500);
    put(e, 0x1A800000u | 31u << 16 | (uint32_t)VS << 12 | (uint32_t)T2 << 5 | T2); /* csel w, w, wzr, vs */
    put(e, 0x2A000000u | (uint32_t)T0 << 16 | 8u << 10 | (uint32_t)T2 << 5 | T2);
    put(e, 0x2A000000u | (uint32_t)T1 << 16 | 14u << 10 | (uint32_t)T2 << 5 | T2);
    ldr(e, 2, T0, CPU, offsetof(X86Cpu, fsw));
    mov_imm(e, T1, 0x4700);
    rrr(e, 0x0A200000u, 4, T0, T0, T1);
    rrr(e, OP_ORR, 4, T0, T0, T2);
    str(e, 2, T0, CPU, offsetof(X86Cpu, fsw));
}

/* d1 = v rounded to an integer as the control word says. loses t0 and the arm64 flags. */
static void x87_round(Ctx *x, int v)
{
    Emit *e = x->e;
    static const uint32_t ops[4] = {0x1E644000u, 0x1E654000u, 0x1E64C000u, 0x1E65C000u}; /* frintn, m, p, z */
    uint32_t *done[3];
    x->keep = 0;
    put(e, 0x2F00E400u | V1); /* movi d1, #0 */
    ldr(e, 2, T0, CPU, offsetof(X86Cpu, fcw));
    ubfm(e, 4, T0, T0, 10, 11);
    for (int mode = 0; mode < 4; mode++) {
        uint32_t *next = nullptr;
        if (mode < 3) {
            put(e, 0x7100001Fu | (uint32_t)mode << 10 | (uint32_t)T0 << 5); /* cmp w, #mode */
            next = b_cond(e, NE);
        }
        put(e, ops[mode] | (uint32_t)v << 5 | V1);
        if (mode < 3) {
            done[mode] = b_always(e);
            patch19(e, next);
        }
    }
    for (int mode = 0; mode < 3; mode++)
        patch26(e, done[mode]);
}

/* t2 = the integer of size bytes that v holds, or the indefinite one when it does not fit. loses t0 and t3. */
static void x87_to_int(Ctx *x, int v, int size)
{
    Emit *e = x->e;
    x->keep = 0;
    put(e, 0x9E780000u | (uint32_t)v << 5 | T2); /* fcvtzs x, d */
    if (size == 8) {
        mov_imm(e, T0, 0x43E0000000000000ull);
        put(e, 0x9E670000u | (uint32_t)T0 << 5 | V2); /* fmov d2, x */
        put(e, 0x1E602000u | (uint32_t)V2 << 16 | (uint32_t)v << 5);
        mov_imm(e, T0, 0x8000000000000000ull);
        put(e, 0x9A800000u | (uint32_t)T2 << 16 | (uint32_t)HS << 12 | (uint32_t)T0 << 5 | T2); /* csel: hs is over or nan */
        return;
    }
    sbfm(e, 8, T3, T2, 0, size * 8 - 1);
    put(e, 0x1E602000u | (uint32_t)v << 16 | (uint32_t)v << 5);
    put(e, 0xFA400000u | (uint32_t)T2 << 16 | (uint32_t)VC << 12 | (uint32_t)T3 << 5); /* ccmp x, x, #0, vc */
    mov_imm(e, T0, size == 4 ? 0x80000000u : 0x8000u);
    put(e, 0x9A800000u | (uint32_t)T2 << 16 | (uint32_t)NE << 12 | (uint32_t)T0 << 5 | T2);
}

/* t0 = the status word with the top in it. loses t1. */
static void x87_status(Ctx *x)
{
    Emit *e = x->e;
    x87_base(x);
    ldr(e, 2, T0, CPU, offsetof(X86Cpu, fsw));
    add_imm(e, 4, T1, 17, x->fcur);
    put(e, 0x33150800u | (uint32_t)T1 << 5 | T0); /* bfi w, w, #11, #3 */
}

static int x87_supported(const X86Insn *i)
{
    uint8_t op = i->opcode, m = i->modrm;
    int sub = (m >> 3) & 7;
    if (i->mod != 3) {
        switch (op) {
        case 0xD9: return sub == 0 || sub == 2 || sub == 3 || sub == 5 || sub == 7;
        case 0xDB: return sub < 4;
        case 0xDD: return sub < 4 || sub == 7;
        case 0xDF: return sub < 4 || sub == 5 || sub == 7;
        default: return 1;
        }
    }
    switch (op) {
    case 0xD8:
    case 0xDC: return 1;
    case 0xD9: return m <= 0xD0 || m == 0xE0 || m == 0xE1 || m == 0xE4 || (m >= 0xE8 && m <= 0xEE) || m == 0xFA || m == 0xFC;
    case 0xDA: return m == 0xE9 || sub < 4;
    case 0xDB: return sub < 4 || sub == 5 || sub == 6;
    case 0xDD: return sub == 0 || (sub >= 2 && sub <= 5);
    case 0xDE: return sub != 3 || m == 0xD9;
    case 0xDF: return m == 0xE0 || sub == 5 || sub == 6;
    default: return 0;
    }
}

/* memory operands are read into v0 to v2 or written before the stack changes, so a fault leaves it as it was. */
static int emit_x87(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode, m = i->modrm;
    int sub = (m >> 3) & 7, r = m & 7, mem = i->mod != 3;
    if (!x87_supported(i))
        return 0;
    x->keep = 1; /* but for the compares and the conversions to integers. */
    if (!x->x87_seen) {
        /* mmx instructions left the registers theirs: the stack starts over. */
        ldr(e, 8, 16, CPU, offsetof(X86Cpu, mmx_mode));
        uint32_t *none = cbz_x(e, 16);
        str(e, 8, XZR, CPU, offsetof(X86Cpu, mmx_mode));
        str(e, 1, XZR, CPU, offsetof(X86Cpu, ftop));
        patch19(e, none);
        x->x87_seen = 1;
    }
    if (mem)
        ea(x, i);
    if (!x86_x87_control(i)) {
        x->x87_pending = 1;
        x->x87_fop = (uint32_t)((op & 7) << 8) | m;
        x->x87_ip = x->addr;
        if (mem)
            str(e, 8, ADDR, CPU, offsetof(X86Cpu, fpu_dp));
    }
    if (mem) {
        switch (op) {
        case 0xD8:
        case 0xDA:
        case 0xDC:
        case 0xDE: {
            if (op == 0xD8) {
                vldr(e, 4, V1, ADDR, 0);
                put(e, 0x1E22C000u | (uint32_t)V1 << 5 | V1); /* fcvt d, s */
            } else if (op == 0xDC) {
                vldr(e, 8, V1, ADDR, 0);
            } else {
                ldr(e, op == 0xDA ? 4 : 2, T2, ADDR, 0);
                sbfm(e, 8, T2, T2, 0, op == 0xDA ? 31 : 15);
                x87_int(e, V1);
            }
            int s = x87_st(x, 0);
            if (sub == 2 || sub == 3) {
                x87_compare(x, s, V1);
                if (sub == 3)
                    x87_pop(x);
            } else {
                x87_arith(e, sub, x87_write(x, 0, 0), s, V1);
            }
            return 1;
        }
        case 0xD9:
            if (sub == 0) {
                vldr(e, 4, x87_next(x), ADDR, 0);
                put(e, 0x1E22C000u | (uint32_t)x87_next(x) << 5 | (uint32_t)x87_next(x));
                x87_push(x);
            } else if (sub == 5) {
                ldr(e, 2, T0, ADDR, 0);
                str(e, 2, T0, CPU, offsetof(X86Cpu, fcw));
            } else if (sub == 7) {
                ldr(e, 2, T0, CPU, offsetof(X86Cpu, fcw));
                str(e, 2, T0, ADDR, 0);
            } else {
                put(e, 0x2F00E400u | V1); /* movi d1, #0: the conversion does not wait for what v1 held. */
                put(e, 0x1E624000u | (uint32_t)x87_st(x, 0) << 5 | V1); /* fcvt s, d */
                vstr(e, 4, V1, ADDR, 0);
                if (sub == 3)
                    x87_pop(x);
            }
            return 1;
        case 0xDD:
            if (sub == 0) {
                vldr(e, 8, x87_next(x), ADDR, 0);
                x87_push(x);
                return 1;
            }
            if (sub == 7) {
                x87_status(x);
                str(e, 2, T0, ADDR, 0);
                return 1;
            }
            if (sub == 1) {
                x87_to_int(x, x87_st(x, 0), 8);
                str(e, 8, T2, ADDR, 0);
            } else {
                vstr(e, 8, x87_st(x, 0), ADDR, 0);
            }
            if (sub != 2)
                x87_pop(x);
            return 1;
        default: { /* db and df */
            int size = op == 0xDB ? 4 : 2;
            if (sub == 0 || sub == 5) {
                if (sub == 5)
                    size = 8;
                ldr(e, size, T2, ADDR, 0);
                if (size < 8)
                    sbfm(e, 8, T2, T2, 0, size * 8 - 1);
                x87_int(e, x87_next(x));
                x87_push(x);
                return 1;
            }
            if (sub == 7)
                size = 8;
            int s = x87_st(x, 0);
            if (sub == 1) {
                x87_to_int(x, s, size);
            } else {
                x87_round(x, s);
                x87_to_int(x, V1, size);
            }
            str(e, size, T2, ADDR, 0);
            if (sub != 2)
                x87_pop(x);
            return 1;
        }
        }
    }
    switch (op) {
    case 0xD8: {
        int a = x87_st(x, 0), b = x87_st(x, r);
        if (sub == 2 || sub == 3) {
            x87_compare(x, a, b);
            if (sub == 3)
                x87_pop(x);
        } else {
            x87_arith(e, sub, x87_write(x, 0, 0), a, b);
        }
        return 1;
    }
    case 0xD9:
        if (m == 0xD0)
            return 1;
        if (m < 0xC8) { /* fld st(i) */
            int from = x87_st(x, r);
            fmov(e, x87_push(x), from);
        } else if (m < 0xD0) { /* fxch */
            int top = x->fcur, other = (x->fcur + r) & 7;
            x87_write(x, 0, 0);
            x87_write(x, r, 0);
            uint8_t held = x->fmap[top];
            x->fmap[top] = x->fmap[other];
            x->fmap[other] = held;
        } else if (m == 0xE4) { /* ftst */
            put(e, 0x2F00E400u | V1);
            x87_compare(x, x87_st(x, 0), V1);
        } else if (m >= 0xE8 && m <= 0xEE) {
            static const uint64_t constants[7] = {0x3FF0000000000000ull, 0x400A934F0979A371ull, 0x3FF71547652B82FEull,
                                                  0x400921FB54442D18ull, 0x3FD34413509F79FFull, 0x3FE62E42FEFA39EFull, 0};
            mov_imm(e, T0, constants[m - 0xE8]);
            put(e, 0x9E670000u | (uint32_t)T0 << 5 | (uint32_t)x87_push(x));
        } else if (m == 0xFC) { /* frndint */
            int s = x87_write(x, 0, 0);
            x87_round(x, s);
            fmov(e, s, V1);
        } else { /* fchs, fabs, fsqrt */
            int s = x87_write(x, 0, 0);
            put(e, (m == 0xE0 ? 0x1E614000u : m == 0xE1 ? 0x1E60C000u : 0x1E61C000u) | (uint32_t)s << 5 | (uint32_t)s);
        }
        return 1;
    case 0xDA:
    case 0xDB:
    case 0xDF:
        if (op == 0xDF && m == 0xE0) { /* fnstsw ax */
            x87_status(x);
            store_reg(e, 2, T0, X86_RAX, 1);
            return 1;
        }
        if (op != 0xDA && sub >= 5) { /* fucomi, fcomi, and with a pop: the flags as ucomisd sets them, and c1 cleared */
            int a = x87_st(x, 0), b = x87_st(x, r);
            x->keep = 0;
            ldr(e, 2, T0, CPU, offsetof(X86Cpu, fsw));
            put(e, 0x12167800u | (uint32_t)T0 << 5 | T0); /* and w, w, #~0x200 */
            str(e, 2, T0, CPU, offsetof(X86Cpu, fsw));
            if (op == 0xDF)
                x87_pop(x);
            put(e, 0x1E602000u | (uint32_t)b << 16 | (uint32_t)a << 5);
            if (x->flags_lazy) {
                cset(e, T0, EQ);
                cset(e, T1, VS);
                cset(e, T2, LO);
                rrr(e, OP_ORR, 4, T0, T0, T1);
                rrr(e, OP_ORR, 4, T2, T2, T1);
                put(e, OP_ORR | 6u << 10 | (uint32_t)T0 << 16 | (uint32_t)T2 << 5 | T2);
                put(e, OP_ORR | 2u << 10 | (uint32_t)T1 << 16 | (uint32_t)T2 << 5 | T2);
                ldr(e, 4, T3, CPU, offsetof(X86Cpu, flags));
                mov_imm(e, T4, X86_ARITH_FLAGS);
                rrr(e, 0x0A200000u, 4, T3, T3, T4);
                rrr(e, OP_ORR, 4, T3, T3, T2);
                str(e, 4, T3, CPU, offsetof(X86Cpu, flags));
                str(e, 4, XZR, CPU, offsetof(X86Cpu, lf_op));
            }
            set_nzcv(x, ALU_UCOMI);
            return 1;
        }
        if (sub < 4) { /* fcmov */
            static const int cc[2][4] = {{2, 4, 6, 0xA}, {3, 5, 7, 0xB}};
            condition_t3(x, cc[op & 1][sub]);
            int from = x87_st(x, r), to = x87_write(x, 0, 0);
            x->keep = 0;
            put(e, 0x7100001Fu | (uint32_t)T3 << 5); /* cmp w, #0 */
            put(e, 0x1E600C00u | (uint32_t)to << 16 | (uint32_t)NE << 12 | (uint32_t)from << 5 | (uint32_t)to); /* fcsel */
            return 1;
        }
        /* fucompp */
        x87_compare(x, x87_st(x, 0), x87_st(x, 1));
        x87_pop(x);
        x87_pop(x);
        return 1;
    case 0xDC:
    case 0xDE: {
        int b = x87_st(x, 0), a = x87_st(x, r);
        if (sub == 2 || sub == 3) {
            x87_compare(x, b, a);
            if (op == 0xDE || sub == 3)
                x87_pop(x);
            if (op == 0xDE && sub == 3)
                x87_pop(x);
            return 1;
        }
        x87_arith(e, sub >= 4 ? sub ^ 1 : sub, x87_write(x, r, 0), a, b);
        if (op == 0xDE)
            x87_pop(x);
        return 1;
    }
    case 0xDD:
        if (sub == 0) { /* ffree */
            int n = (x->fcur + r) & 7;
            x87_base(x);
            x->ftag_clear |= (uint8_t)(1u << n);
            x->ftag_set &= (uint8_t)~(1u << n);
            return 1;
        }
        if (sub < 4) {
            int from = x87_st(x, 0), to = x87_write(x, r, 1);
            if (from != to)
                fmov(e, to, from);
            if (sub == 3)
                x87_pop(x);
        } else {
            x87_compare(x, x87_st(x, 0), x87_st(x, r));
            if (sub == 5)
                x87_pop(x);
        }
        return 1;
    default: return 0;
    }
}

/*
 * movs and stos, alone or repeated, going up: one element at a time as the processor does, so a fault leaves the
 * registers where the instruction goes on from. a repeated one first goes 16 bytes at a time while that many are
 * left, when what a copy reads is not what it wrote a moment before. the interpreter does the ones going down.
 */
static int emit_string(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int copy = i->opcode <= 0xA5, size = i->opcode & 1 ? i->opsize : 1, w = x->cpu->mode64 ? 8 : 4;
    if (i->seg || i->addrsize != w || i->rep == 0xF2)
        return 0;
    int si = host_reg(X86_RSI), di = host_reg(X86_RDI), cx = host_reg(X86_RCX), ax = host_reg(X86_RAX);
    ldr(e, 4, T0, CPU, offsetof(X86Cpu, flags));
    ubfm(e, 4, T0, T0, 10, 10);
    uint32_t stub = cold_at(x, cbnz_w(e, T0)); /* df */
    uint32_t *done = nullptr;
    if (i->rep) {
        uint32_t *slow = nullptr;
        if (copy) {
            /* a copy onto what it is about to read repeats it: that goes element by element. */
            rrr(e, OP_SUB, 8, T2, di, si);
            put(e, 0xF100001Fu | 16u << 10 | (uint32_t)T2 << 5); /* cmp t2, #16 */
            slow = b_cond(e, LO);
        } else {
            static const uint32_t dup[9] = {0, 0x4E010C00u, 0x4E020C00u, 0, 0x4E040C00u, 0, 0, 0, 0x4E080C00u};
            put(e, dup[size] | (uint32_t)ax << 5 | V0);
        }
        uint32_t *wide = e->p;
        put(e, (w == 8 ? 0xF100001Fu : 0x7100001Fu) | (uint32_t)(16 / size) << 10 | (uint32_t)cx << 5); /* cmp cx, #n */
        uint32_t *few = b_cond(e, LO);
        if (copy)
            vldr(e, 16, V0, si, 0);
        vstr(e, 16, V0, di, 0);
        if (copy)
            add_imm(e, w, si, si, 16);
        add_imm(e, w, di, di, 16);
        add_imm(e, w, cx, cx, -(16 / size));
        put(e, 0x14000000u | ((uint32_t)(wide - e->p) & 0x3FFFFFFu));
        patch19(e, slow);
        patch19(e, few);
        uint32_t *top = e->p;
        done = e->p;
        put(e, (w == 8 ? 0xB4000000u : 0x34000000u) | (uint32_t)cx); /* cbz cx, done */
        if (copy)
            ldr(e, size, T1, si, 0);
        str(e, size, copy ? T1 : ax, di, 0);
        if (copy)
            add_imm(e, w, si, si, size);
        add_imm(e, w, di, di, size);
        add_imm(e, w, cx, cx, -1);
        put(e, 0x14000000u | ((uint32_t)(top - e->p) & 0x3FFFFFFu));
        patch19(e, done);
    } else {
        if (copy)
            ldr(e, size, T1, si, 0);
        str(e, size, copy ? T1 : ax, di, 0);
        if (copy)
            add_imm(e, w, si, si, size);
        add_imm(e, w, di, di, size);
    }
    cold_resume(x, stub);
    return 1;
}

/*
 * the flags of a result of 4 or 8 bytes as x86_set_szp leaves them, in the cpu's flags themselves: zf, sf and pf of
 * res, with cf and of as t3 has them, bits 0 and 11. t1, t4, v0 and the arm64 flags are lost.
 */
static void emit_szp(Ctx *x, int size, int res)
{
    Emit *e = x->e;
    put(e, (size == 8 ? 0xF100001Fu : 0x7100001Fu) | (uint32_t)res << 5); /* cmp res, #0 */
    cset(e, T1, EQ);
    put(e, 0x2A000000u | (uint32_t)T1 << 16 | 6u << 10 | (uint32_t)T3 << 5 | T3);
    ubfm(e, size, T1, res, size * 8 - 1, size * 8 - 1);
    put(e, 0x2A000000u | (uint32_t)T1 << 16 | 7u << 10 | (uint32_t)T3 << 5 | T3);
    put(e, 0x1E270000u | (uint32_t)res << 5 | V0);  /* fmov s0, w */
    put(e, 0x0E205800u | (uint32_t)V0 << 5 | V0);   /* cnt v0.8b: the bits set in the low byte */
    put(e, 0x0E013C00u | (uint32_t)V0 << 5 | T1);   /* umov w, v0.b[0] */
    put(e, 0x52000000u | (uint32_t)T1 << 5 | T1);   /* eor w, w, #1: pf is set for an even count */
    put(e, 0x12000000u | (uint32_t)T1 << 5 | T1);   /* and w, w, #1 */
    put(e, 0x2A000000u | (uint32_t)T1 << 16 | 2u << 10 | (uint32_t)T3 << 5 | T3);
    ldr(e, 4, T1, CPU, offsetof(X86Cpu, flags));
    mov_imm(e, T4, X86_ARITH_FLAGS);
    rrr(e, 0x0A200000u, 4, T1, T1, T4);
    rrr(e, OP_ORR, 4, T1, T1, T3);
    str(e, 4, T1, CPU, offsetof(X86Cpu, flags));
    str(e, 4, XZR, CPU, offsetof(X86Cpu, lf_op));
}

/* shld and shrd of registers, with their flags when something reads them: as the interpreter's double_shift. */
static int emit_double_shift(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    int size = i->opsize, bits = size * 8, left = i->opcode <= 0xA5, by_cl = i->opcode & 1;
    if (i->mod != 3 || size < 4 || (by_cl && size != 4))
        return 0;
    int d = host_reg(i->rm), s = host_reg(i->reg), live = x->flags_live != 0;
    if (!by_cl) {
        int n = (int)(i->imm & (unsigned)(bits - 1));
        if (!n) {
            if (size == 4)
                mov_reg(e, 4, d, d);
            return x->keep = 1, 1;
        }
        if (live)
            mov_reg(e, 8, T0, d);
        /* extr takes the low bits of the pair shifted right: for the shift left, by what is left of the width. */
        if (left)
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)s << 16 | (uint32_t)(bits - n) << 10 | (uint32_t)d << 5 | (uint32_t)d);
        else
            put(e, (size == 8 ? 0x93C00000u : 0x13800000u) | (uint32_t)d << 16 | (uint32_t)n << 10 | (uint32_t)s << 5 | (uint32_t)d);
        if (!live)
            return x->keep = 1, 1;
        int out = left ? bits - n : n - 1;
        ubfm(e, size, T3, T0, out, out); /* cf: the last bit shifted out */
        rrr(e, OP_EOR, size, T1, d, T0);
        ubfm(e, size, T1, T1, bits - 1, bits - 1);
        put(e, 0x2A000000u | (uint32_t)T1 << 16 | 11u << 10 | (uint32_t)T3 << 5 | T3); /* of: the sign changed */
        emit_szp(x, size, d);
        return 1;
    }
    /* by cl, of 4 bytes: a count of 0 changes nothing, flags included. */
    put(e, 0x12001000u | (uint32_t)host_reg(X86_RCX) << 5 | T5); /* and w, cl, #31 */
    mov_reg(e, 4, d, d); /* the register is written all the same: its upper half clears. */
    uint32_t *none = cbz_x(e, T5);
    mov_reg(e, 4, T0, d);
    if (left) {
        mov_reg(e, 4, T1, s);
        put(e, 0xAA000000u | (uint32_t)T0 << 16 | 32u << 10 | (uint32_t)T1 << 5 | T2); /* orr x, src, dst, lsl #32 */
        rrr(e, OP_LSLV, 8, T2, T2, T5);
        ubfm(e, 8, d, T2, 32, 63);
    } else {
        put(e, 0xAA000000u | (uint32_t)s << 16 | 32u << 10 | (uint32_t)T0 << 5 | T2); /* orr x, dst, src, lsl #32 */
        rrr(e, OP_LSRV, 8, T2, T2, T5);
        mov_reg(e, 4, d, T2);
    }
    if (live) {
        if (left) {
            mov_imm(e, T4, 32);
            rrr(e, OP_SUB, 4, T4, T4, T5);
        } else {
            add_imm(e, 4, T4, T5, -1);
        }
        rrr(e, OP_LSRV, 4, T3, T0, T4);
        put(e, 0x12000000u | (uint32_t)T3 << 5 | T3); /* and w, w, #1 */
        rrr(e, OP_EOR, 4, T1, d, T0);
        ubfm(e, 4, T1, T1, 31, 31);
        put(e, 0x2A000000u | (uint32_t)T1 << 16 | 11u << 10 | (uint32_t)T3 << 5 | T3);
        emit_szp(x, 4, d);
    }
    patch19(e, none);
    return 1;
}

static int emit_one(Ctx *x, const X86Insn *i)
{
    Emit *e = x->e;
    uint8_t op = i->opcode;
    int size = i->opsize;
    if (is_jcc(i))
        return emit_side_exit(x, i);
    if (is_x87(i))
        return emit_x87(x, i);
    if (i->map == 0 && op == 0x9B) /* fwait */
        return x->keep = 1, 1;
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
            if (size >= 4 && !(i->mod == 3 && high_byte(src, i->rm, i->rex))) {
                int rd = host_reg(i->reg);
                if (i->mod != 3) {
                    ea(x, i);
                    ldr(e, src, rd, ADDR, 0);
                }
                if (op >= 0xBE)
                    sbfm(e, size, rd, i->mod == 3 ? host_reg(i->rm) : rd, 0, src * 8 - 1);
                else if (i->mod == 3)
                    ubfm(e, 4, rd, host_reg(i->rm), 0, src * 8 - 1);
                return x->keep = 1, 1;
            }
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
            if (size >= 4) {
                int rs = rm_operand(x, i, size, T1), rd = host_reg(i->reg);
                if (cond == -1) {
                    put(e, 0x7100001Fu | (uint32_t)T3 << 5); /* cmp w13, #0 */
                    cond = NE;
                }
                /* the 4 byte one clears the upper half whether it moves or not. */
                if (cond == -2 || cond == -3)
                    mov_reg(e, size, rd, cond == -3 ? rs : rd);
                else
                    put(e, (size == 8 ? 0x9A800000u : 0x1A800000u) | (uint32_t)rd << 16 | (uint32_t)cond << 12 |
                               (uint32_t)rs << 5 | (uint32_t)rd);
                return 1;
            }
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
        if (!i->lock && !i->mandatory && (op == 0xA4 || op == 0xA5 || op == 0xAC || op == 0xAD))
            return emit_double_shift(x, i);
        if (is_bt(i)) {
            /* cf alone changes: it is left in the arm64 flags for what reads it next, when nothing else reads flags. */
            if ((x->flags_live & LIVE_REST) || x->flags_lazy)
                return 0;
            if (!x->flags_live)
                return x->keep = 1, 1;
            int v = rm_operand(x, i, size, T0);
            if (op == 0xA3) {
                rrr(e, OP_LSRV, size, T3, v, host_reg(i->reg));
            } else {
                int bit = (int)(i->imm & (size * 8 - 1));
                ubfm(e, size, T3, v, bit, bit);
            }
            put(e, 0x12000000u | (uint32_t)T3 << 5 | T3);        /* and w, w, #1 */
            put(e, 0x7100041Fu | (uint32_t)T3 << 5);              /* cmp w, #1 */
            set_nzcv(x, ALU_BT);
            return 1;
        }
        if (op == 0xC3 && !i->mandatory && i->mod != 3 && size >= 4) { /* movnti */
            store_rm(x, i, size, host_reg(i->reg), 0);
            return x->keep = 1, 1;
        }
        if ((op == 0xAF) && !i->mandatory && !x->flags_live && size >= 4) { /* imul r, rm */
            int rb = rm_operand(x, i, size, T1), rd = host_reg(i->reg);
            put(e, (size == 8 ? 0x9B007C00u : 0x1B007C00u) | (uint32_t)rb << 16 | (uint32_t)rd << 5 | (uint32_t)rd);
            return 1;
        }
        return emit_sse(x, i);
    }
    if (i->map == 2 && op >= 0xCB && op <= 0xCD && !i->mandatory)
        return emit_sha256(x, i);
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
            /* in 32-bit code the registers hold nothing past 4 bytes, and a move of all 8 is one the host does for free. */
            if (i->mod == 3)
                mov_reg(e, x->cpu->mode64 ? size : 8, host_reg(i->rm), host_reg(i->reg));
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
            if (i->mod == 3 && !x->cpu->mode64)
                mov_reg(e, 8, host_reg(i->reg), host_reg(i->rm));
            else
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
        if (size >= 4) {
            /* 4 byte address arithmetic leaves the upper half clear by itself. */
            ea_into(x, &flat, host_reg(i->reg));
            if (size == 4 && i->addrsize != 4)
                mov_reg(e, 4, host_reg(i->reg), host_reg(i->reg));
            return x->keep = 1, 1;
        }
        ea(x, &flat);
        if (size == 4)
            mov_reg(e, 4, ADDR, ADDR);
        store_reg(e, size, ADDR, i->reg, 1);
        return x->keep = 1, 1;
    }
    case 0x63: /* movsxd */
        if (size != 8)
            return 0;
        sbfm(e, 8, host_reg(i->reg), rm_operand(x, i, 4, T0), 0, 31);
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
        sbfm(e, size, host_reg(X86_RDX), host_reg(X86_RAX), size * 8 - 1, size * 8 - 1);
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
        if (host_reg(reg) >= 0) {
            /* the stack pointer moves once the load did not fault. */
            ldr(e, size, host_reg(reg), host_reg(X86_RSP), 0);
            add_imm(e, size, host_reg(X86_RSP), host_reg(X86_RSP), size);
            return x->keep = 1, 1;
        }
        load_reg(e, size, T3, X86_RSP, 1);
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
    case 0xA4: case 0xA5: case 0xAA: case 0xAB:
        return emit_string(x, i);
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { /* xchg with the accumulator */
        if (size < 4)
            return 0;
        int a = host_reg(X86_RAX), b = host_reg((op & 7) | ((i->rex & 1) << 3));
        int width = size == 8 || !x->cpu->mode64 ? 8 : 4;
        mov_reg(e, width, T0, a);
        mov_reg(e, width, a, b);
        mov_reg(e, width, b, T0);
        return x->keep = 1, 1;
    }
    case 0x9F: /* lahf: sf, zf, af, pf and cf, with bit 1 set, as the flags' low byte has them */
        call_begin(x);
        mov_reg(e, 8, 0, CPU);
        call(e, (const void *)x86_arith_flags);
        mov_reg(e, 4, T3, 0);
        call_end(x);
        mov_imm(e, T4, 0xD5);
        rrr(e, OP_AND, 4, T3, T3, T4);
        mov_imm(e, T4, 2);
        rrr(e, OP_ORR, 4, T3, T3, T4);
        store_reg(e, 1, T3, 4, 0);
        return 1;
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
        if (sub == 2 && sz >= 4 && i->mod == 3) {
            put(e, (sz == 8 ? 0xAA2003E0u : 0x2A2003E0u) | (uint32_t)host_reg(i->rm) << 16 | (uint32_t)host_reg(i->rm));
            return x->keep = 1, 1;
        }
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
        if ((sub == 6 || sub == 7) && sz >= 4)
            return emit_div(x, i, sz, sub == 7);
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
        {
            int ra = rm_operand(x, i, size, T0);
            mov_imm(e, T1, i->imm & size_mask(size));
            put(e, (size == 8 ? 0x9B007C00u : 0x1B007C00u) | (uint32_t)T1 << 16 | (uint32_t)ra << 5 | (uint32_t)host_reg(i->reg));
        }
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

/*
 * the return stack. a call pushes the address it returns to with the code that goes on there, and jumps to its target
 * with bl. a return pops a pair, and when the address is the one it returns to it leaves with ret, which the host
 * predicts like the return of the bl. the pairs sit in a ring that the dispatcher clears when blocks go: any pair in
 * it is good for its address, whatever was pushed and popped in between. reg holds the return address.
 */
static void ras_call(Ctx *x, int reg)
{
    Emit *e = x->e;
    x87_store(x);
    x87_reset(x);
    put(e, 0xD1004391u);        /* sub x17, x28, #16 */
    put(e, 0xB340263Cu);        /* bfxil x28, x17, #0, #10: one down, around the ring */
    put(e, 0x10000071u);        /* adr x17, the code after the bl */
    pair(e, 1, reg, 17, RAS, 0);
    uint32_t *call = e->p;
    put(e, 0x94000000u);        /* bl the exit to the target */
    exit_const(x, x->next, x->count);
    patch26(e, call);
}

/* reg holds the address returned to. falls through when the stack does not have it. x16, x17 and t1 are lost. */
static void ras_return(Ctx *x, int reg)
{
    Emit *e = x->e;
    x87_store(x);
    x87_reset(x);
    pair(e, 0, 16, 17, RAS, 0);
    put(e, 0x91004000u | (uint32_t)RAS << 5 | T1);              /* add t1, x28, #16 */
    put(e, 0xB3402400u | (uint32_t)T1 << 5 | (uint32_t)RAS);    /* bfxil x28, t1, #0, #10 */
    put(e, 0xEB00001Fu | (uint32_t)reg << 16 | 16u << 5);       /* cmp x16, reg */
    uint32_t *other = b_cond(e, NE);
    put(e, 0xD65F0220u);                                         /* ret x17 */
    patch19(e, other);
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
        ras_call(x, T0);
        exit_const(x, branch_target(x, x->next + i->imm), x->count);
        return 1;
    case 0xC3: /* ret */
    case 0xC2: /* ret imm16 */ {
        int w = stack_word(x);
        if (i->opsize != w)
            return 0;
        /* the stack pointer is whole in its host register: what writes 4 bytes of one clears the rest. */
        ldr(e, w, T0, host_reg(X86_RSP), 0);
        add_imm(e, w, host_reg(X86_RSP), host_reg(X86_RSP), w + (op == 0xC2 ? (int64_t)(i->imm & 0xFFFF) : 0));
        ras_return(x, T0);
        exit_computed(x, T0);
        return 1;
    }
    case 0xFF: {
        int sub = (i->modrm >> 3) & 7, w = stack_word(x);
        /* near calls and jumps take targets of the stack word: 64 bits in 64-bit code without rex.w. */
        if ((sub != 2 && sub != 4) || i->p66)
            return 0;
        if (i->mod == 3 && !x->cpu->mode64)
            mov_reg(e, 8, T0, host_reg(i->rm));
        else
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
            ras_call(x, T1);
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
    x87_store(x);
    mov_imm(e, T4, x->addr);
    str(e, 8, T4, CPU, offsetof(X86Cpu, rip));
    sync_regs(e, 1);
    xstore_dirty(x);
    mov_reg(e, 8, 0, CPU);
    mov_imm(e, 1, (uint64_t)(uintptr_t)i);
    call(e, (const void *)x86_exec_insn);
    /* emulation the instruction nested left the mode as it returned. */
    fp_mode(e, 1);
    sync_regs(e, 0);
    xreload(x);
    /* on every path the instruction may have changed the x87 state, as an x87 or mmx one or fxrstor does. */
    if (x->final)
        x87_reset(x);
    else
        x87_reload(x);
    x->final = 0;
    x->nzcv_alu = -1;
}

/* the kind of arm64 flags the translation of i leaves for the instruction after it when that one reads them, or -1. */
/* bt of 4 or 8 bytes, with the bit in a register for a register or in the instruction. */
static int is_bt(const X86Insn *i)
{
    if (i->map != 1 || i->lock || i->opsize < 4)
        return 0;
    return (i->opcode == 0xA3 && i->mod == 3) || (i->opcode == 0xBA && ((i->modrm >> 3) & 7) == 4);
}

/* the host register an inc or dec of 4 or 8 bytes changes, -1 for one of memory or less, -2 for another instruction. */
static int incdec_reg(const X86Cpu *cpu, const X86Insn *i)
{
    if (i->map != 0 || i->lock)
        return -2;
    if (i->opcode >= 0x40 && i->opcode <= 0x4F && !cpu->mode64)
        return i->opsize >= 4 ? host_reg(i->opcode & 7) : -1;
    if ((i->opcode == 0xFE || i->opcode == 0xFF) && ((i->modrm >> 3) & 7) < 2)
        return i->opcode == 0xFF && i->mod == 3 && i->opsize >= 4 ? host_reg(i->rm) : -1;
    return -2;
}

static int nzcv_kind(const X86Insn *i)
{
    /* the compares of floats, whose flags the cpu only gets when something later reads them. */
    if (i->map == 1 && (i->opcode == 0x2E || i->opcode == 0x2F) && (!i->mandatory || i->mandatory == 0x66))
        return ALU_UCOMI;
    if (i->map == 0 && (i->opcode == 0xDB || i->opcode == 0xDF) && i->mod == 3 && i->modrm >= 0xE8 && i->modrm <= 0xF7)
        return ALU_UCOMI;
    if (is_bt(i))
        return ALU_BT;
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

static uint64_t x87_held(const Ctx *x)
{
    uint64_t held = (uint64_t)(x->fbase != 0) | (uint64_t)x->fcur << 1 | (uint64_t)(x->fdirty & ~x->ftag_clear) << 8 |
                    (uint64_t)x->ftag_set << 16 | (uint64_t)x->ftag_clear << 24;
    for (int n = 0; n < 8; n++)
        held |= (uint64_t)x->fmap[n] << (32 + 3 * n);
    return held;
}

uint32_t x86_jit_translate(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint32_t *code,
                           uint32_t capacity, uint16_t *offsets, uint16_t *xmm_held, uint64_t *x87, const void *block)
{
    Emit e = {.code = code, .end = code + capacity, .p = code};
    uint8_t live[128], lazy[128];
    if (!count || count > 128)
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
    uint64_t starts[128], a = rip;
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
        /* an inc or dec only keeps cf of what came before it, which it reads from the arm64 flags too. */
        if (k + 1 < count && incdec_reg(cpu, &insns[k + 1]) >= -1 && nzcv_kind(&insns[k]) >= 0 &&
            arm_cond(nzcv_kind(&insns[k]), 2) != -1)
            lazy[k] = 0;
    }
    /* see sink_at: an inc or dec of a register, then a branch on its flags to where nothing reads them. */
    uint32_t sink_at = ~0u;
    int sink_live = 0;
    for (uint32_t k = 0; k + 1 < count; k++) {
        const X86Insn *j = &insns[k + 1];
        if (!lazy[k] || incdec_reg(cpu, &insns[k]) < 0 || !is_jcc(j) || arm_cond(ALU_INCDEC, j->opcode & 15) < 0)
            continue;
        uint64_t after = starts[k + 1] + j->len;
        if (flags_read_at(cpu, cpu->mode64 ? after + j->imm : (uint32_t)(after + j->imm), lo, hi))
            continue;
        /* one a block: the first is the one of a loop that ends where the block goes on. */
        sink_at = k + 1;
        sink_live = lazy[k];
        lazy[k] = 0;
        break;
    }
    e.self = block;
    ColdList *cold = x86_alloc(sizeof(ColdList));
    if (!cold)
        return 0;
    cold->count = 0;
    Ctx x = {.e = &e, .cpu = cpu, .addr = rip, .next = rip, .count = count, .nzcv_alu = -1, .start = rip, .cold = cold,
             .sink_at = sink_at, .sink_live = sink_live};
    x87_reset(&x);
    cold->polls = 0;
    prologue(&e);
    poll(&x);
    uint32_t end = count;
    if (nests(&insns[count - 1]))
        end = count - 1;
    uint64_t addr = rip;
    for (uint32_t k = 0; k < end; k++) {
        const X86Insn *i = &insns[k];
        offsets[k] = (uint16_t)(e.p - code);
        xmm_held[k] = 0xFFFF;
        x87[k] = x87_held(&x);
        x.addr = addr;
        x.next = addr + i->len;
        x.index = k;
        x.insn = i;
        x.flags_live = live[k];
        x.flags_lazy = lazy[k];
        if (k == count - 1 && x86_ends_block(i)) {
            if (!emit_branch(&x, i)) {
                x.final = 1;
                emit_fallback(&x, i);
                /* the interpreter set rip to wherever the instruction went. */
                exit_computed(&x, -1);
            }
            offsets[count] = (uint16_t)(e.p - code);
            cold_emit(&x);
            x86_free(cold, sizeof(*cold));
            return e.overflow || e.p - code > 0xFFFF ? 0 : (uint32_t)(e.p - code);
        }
        if (!emit_insn(&x, i)) {
            x.final = 1;
            emit_fallback(&x, i);
        }
        addr = x.next;
    }
    offsets[end] = (uint16_t)(e.p - code);
    if (end < count) {
        xmm_held[end] = 0xFFFF;
        x87[end] = x87_held(&x);
    }
    exit_const(&x, addr, end);
    if (end < count)
        offsets[count] = (uint16_t)(e.p - code);
    cold_emit(&x);
    x86_free(cold, sizeof(*cold));
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
