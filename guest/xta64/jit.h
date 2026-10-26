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

/*
 * where translated code goes for an address it computed, like a return: a direct mapped cache of the translated
 * blocks the dispatcher ran, which exits to computed addresses look the address up in before returning to it.
 */
typedef struct X86Target {
    uint64_t rip;
    const uint32_t *code; /* past the prologue. */
    const void *block;
    uint64_t unused;
} X86Target;

#define X86_TARGET_BITS 12
#define X86_TARGET_COUNT (1u << X86_TARGET_BITS)

static inline uint32_t x86_target_index(uint64_t rip) { return (uint32_t)(rip ^ (rip >> 12)) & (X86_TARGET_COUNT - 1); }

/* words of a translated block's prologue, which a chained exit skips. */
#define X86_JIT_PROLOGUE 10

/*
 * writes at most capacity words. offsets gets count + 1 entries: the code
 * offset of each instruction and of the end. returns the code size in words,
 * 0 when it does not fit.
 */
uint32_t x86_jit_translate(X86Cpu *cpu, uint64_t rip, const X86Insn *insns, uint32_t count, uint32_t *code,
                           uint32_t capacity, uint16_t *offsets);

/* the address of the instruction whose code contains code_offset. */
uint64_t x86_jit_fault_rip(uint64_t rip, const X86Insn *insns, uint32_t count, const uint16_t *offsets,
                           uint32_t code_offset);

/* runs one instruction in the interpreter, with rip at its start. */
void x86_exec_insn(X86Cpu *cpu, const X86Insn *insn);
int x86_ends_block(const X86Insn *insn);
/* group 2 with the interpreter's flags: op and size as op << 8 | size. */
uint64_t x86_jit_shift(X86Cpu *cpu, uint32_t op_size, uint64_t v, uint64_t count);

#endif
