/* the translator from x64 blocks to arm64 code, and what it shares with the interpreter. */
#ifndef MIME_X86_JIT_H
#define MIME_X86_JIT_H

#include "x86.h"

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

#endif
