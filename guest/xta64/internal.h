/* helpers shared by the interpreter files of the x86 core. */
#ifndef MIME_X86_INTERNAL_H
#define MIME_X86_INTERNAL_H

#include "x86.h"



#define X86_STATUS_ACCESS_VIOLATION 0xC0000005u
#define X86_STATUS_ILLEGAL_INSTRUCTION 0xC000001Du
#define X86_STATUS_PRIVILEGED_INSTRUCTION 0xC0000096u
#define X86_STATUS_INTEGER_DIVIDE_BY_ZERO 0xC0000094u
#define X86_STATUS_INTEGER_OVERFLOW 0xC0000095u
#define X86_STATUS_BREAKPOINT 0x80000003u
#define X86_STATUS_SINGLE_STEP 0x80000004u
#define X86_STATUS_STACK_BUFFER_OVERRUN 0xC0000409u
#define X86_STATUS_FLOAT_INVALID_OPERATION 0xC0000090u
#define X86_STATUS_FLOAT_DIVIDE_BY_ZERO 0xC000008Eu
#define X86_STATUS_FLOAT_MULTIPLE_TRAPS 0xC00002B5u
#define X86_STATUS_FLOAT_MULTIPLE_FAULTS 0xC00002B4u

static inline uint64_t x86_mask(int size)
{
    return size >= 8 ? ~0ull : (1ull << (size * 8)) - 1;
}

static inline uint64_t x86_sext(uint64_t v, int size)
{
    switch (size) {
    case 1:
        return (uint64_t)(int64_t)(int8_t)v;
    case 2:
        return (uint64_t)(int64_t)(int16_t)v;
    case 4:
        return (uint64_t)(int64_t)(int32_t)v;
    default:
        return v;
    }
}

/* guest memory is accessed in place. */
static inline uint8_t rd8(uint64_t a) { return *(volatile uint8_t *)(uintptr_t)a; }
static inline uint16_t rd16(uint64_t a) { uint16_t v; __builtin_memcpy(&v, (void *)(uintptr_t)a, 2); return v; }
static inline uint32_t rd32(uint64_t a) { uint32_t v; __builtin_memcpy(&v, (void *)(uintptr_t)a, 4); return v; }
static inline uint64_t rd64(uint64_t a) { uint64_t v; __builtin_memcpy(&v, (void *)(uintptr_t)a, 8); return v; }
static inline void wr8(uint64_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t)a = v; }
static inline void wr16(uint64_t a, uint16_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 2); }
static inline void wr32(uint64_t a, uint32_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 4); }
static inline void wr64(uint64_t a, uint64_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 8); }

static inline uint64_t rdn(uint64_t a, int size)
{
    switch (size) {
    case 1: return rd8(a);
    case 2: return rd16(a);
    case 4: return rd32(a);
    default: return rd64(a);
    }
}

static inline void wrn(uint64_t a, int size, uint64_t v)
{
    switch (size) {
    case 1: wr8(a, (uint8_t)v); break;
    case 2: wr16(a, (uint16_t)v); break;
    case 4: wr32(a, (uint32_t)v); break;
    default: wr64(a, v); break;
    }
}

/* c->rip must already point at the next instruction. */
static inline uint64_t x86_ea(X86Cpu *c, const X86Insn *i)
{
    uint64_t a = (uint64_t)(int64_t)i->disp;
    if (i->base >= 0)
        a += c->r[i->base];
    else if (i->base == -2)
        a += c->rip;
    if (i->index >= 0)
        a += c->r[i->index] << i->scale;
    if (i->addrsize == 4)
        a = (uint32_t)a;
    if (i->seg == 4)
        a += c->fs_base;
    else if (i->seg == 5)
        a += c->gs_base;
    return a;
}

[[noreturn]] void x86_fault(X86Cpu *c, uint32_t code, uint64_t p0, uint64_t p1);
[[noreturn]] void x86_undefined(X86Cpu *c);

uint32_t x86_arith_flags(X86Cpu *c);
int x86_cond(X86Cpu *c, int cc);
void x86_set_szp(X86Cpu *c, uint64_t res, int size, uint32_t others);

void x86_exec_sse(X86Cpu *c, const X86Insn *i);
void x86_exec_x87(X86Cpu *c, const X86Insn *i);
/* whether an x87 instruction leaves the last instruction and data pointers and opcode alone. */
int x86_x87_control(const X86Insn *i);
void x86_exec_mmx(X86Cpu *c, const X86Insn *i);
void x86_ldmxcsr(X86Cpu *c, uint32_t v);
void x86_fpu_init(X86Cpu *c);

#endif
