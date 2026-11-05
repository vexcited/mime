/* the translator from x64 blocks to arm64 code, and what it shares with the interpreter. */
#ifndef MIME_X86_JIT_H
#define MIME_X86_JIT_H

#include "x86.h"

/*
 * where a translated block leaves for a constant address. the dispatcher links it to the translated block there,
 * which the exit then jumps to directly, past its prologue, while no suspension is pending and code is unchanged.
 */
typedef struct X86ChainSlot {
    const uint32_t *code; /* the target's code past its prologue, null until linked. */
    const void *block;
    uint64_t rip;
} X86ChainSlot;

/* a chain slot linked to a block, unlinked when the block is dropped. */
typedef struct X86Link {
    struct X86Link *next;
    X86ChainSlot *slot;
} X86Link;

/* the fields the dispatcher and the exits of translated code read come first. */
typedef struct X86Block {
    uint64_t rip;
    uint32_t *jit;          /* translated arm64 code. */
    X86Native native;
    uint32_t runs;          /* runs before translation, ~0u once translation failed. */
    uint32_t count;
    uint32_t dropped;       /* the code changed: the block only lives on in references to it. */
    struct X86Block *page_next; /* the next block starting in a page of the same page bucket. */
    uint64_t end;           /* the address after the last instruction. */
    struct X86Block *fallback; /* x64 code of a native block, for when the native function declines. */
    uint32_t *jit_ready;    /* translated code waiting for the next batch to become visible to fetch. */
    uint16_t *jit_offsets;  /* code offset of each instruction and of the end. */
    uint16_t *jit_xmm;      /* the xmm registers the translated code holds in v16 to v31 at each instruction. */
    X86Link *links;
    X86Insn insns[];
} X86Block;

/*
 * every block of a block cache by its address: open addressing with linear probing over a power of two of slots, at
 * most half of them used. the dispatcher and the exits of translated code to computed addresses probe the same slots,
 * which hold the code to jump to as well, so a hit reads one line. a slot without a block is free.
 */
typedef struct X86Slot {
    uint64_t rip;
    const uint32_t *code; /* the block's translated code past its prologue, null until it is visible. */
    X86Block *block;
    uint64_t unused;
} X86Slot;

/* the start of every block cache, which translated code reads through the cpu's blocks. */
typedef struct X86Table {
    X86Slot *slots;
    uint64_t mask;
    uint64_t used;
} X86Table;

static inline uint64_t x86_slot_hash(uint64_t rip) { return rip ^ (rip >> 4) ^ (rip >> 15); }

/* words of a translated block's prologue, which a chained exit skips. */
#define X86_JIT_PROLOGUE 14

/*
 * writes at most capacity words. offsets gets count + 1 entries: the code
 * offset of each instruction and of the end, and xmm_held count entries: the
 * xmm registers v16 to v31 hold at the start of each instruction, see the xmm
 * cache. returns the code size in words, 0 when it does not fit.
 */
uint32_t x86_jit_translate(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint32_t *code,
                           uint32_t capacity, uint16_t *offsets, uint16_t *xmm_held);

/* the address of the instruction whose code contains code_offset. */
uint64_t x86_jit_fault_rip(uint64_t rip, const X86Insn *insns, uint32_t count, const uint16_t *offsets,
                           uint32_t code_offset);

/*
 * the locked read modify writes at addresses arm64 atomics fault on, done as the interpreter does them: op << 8 | size
 * with op one of these, as ldaddal, ldclral, ldeoral, ldsetal and swpal. both return the old value.
 */
enum { X86_ATOMIC_ADD = 0, X86_ATOMIC_CLR = 1, X86_ATOMIC_EOR = 2, X86_ATOMIC_SET = 3, X86_ATOMIC_SWP = 8 };
uint64_t x86_jit_atomic(uint64_t addr, uint64_t op_size, uint64_t operand);
uint64_t x86_jit_cas(uint64_t addr, uint64_t size, uint64_t expected, uint64_t desired);

/* runs one instruction in the interpreter, with rip at its start. */
void x86_exec_insn(X86Cpu *cpu, const X86Insn *insn);
int x86_ends_block(const X86Insn *insn);
/* group 2 with the interpreter's flags: op and size as op << 8 | size. */
uint64_t x86_jit_shift(X86Cpu *cpu, uint32_t op_size, uint64_t v, uint64_t count);

#endif
