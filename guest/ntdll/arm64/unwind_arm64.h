/*
 * the arm64 virtual unwinder for .pdata and .xdata unwind data, as Microsoft documents it in "ARM64
 * exception handling". it depends on nothing in ntdll.
 */
#ifndef MIME_NTDLL_ARM64_UNWIND_H
#define MIME_NTDLL_ARM64_UNWIND_H

#include <stddef.h>
#include <stdint.h>

/* one .pdata record, the layout of IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY. */
typedef struct A64_FUNCTION {
    uint32_t begin;
    uint32_t unwind;
} A64_FUNCTION;

typedef struct A64_VECTOR {
    uint64_t low;
    uint64_t high;
} A64_VECTOR;

/* the leading part of ARM64_NT_CONTEXT, which is all an unwind reads and writes. */
typedef struct __attribute__((may_alias)) A64_CONTEXT {
    uint32_t flags;
    uint32_t cpsr;
    uint64_t x[31]; /* x29 is the frame pointer, x30 the link register */
    uint64_t sp;
    uint64_t pc;
    A64_VECTOR v[32];
} A64_CONTEXT;

#define A64_FP 29
#define A64_LR 30

/* set in the flags of a context an unwind made: its pc is a return address, the instruction before it the call. */
#define A64_CONTEXT_UNWOUND_TO_CALL 0x20000000u

/* where the unwind found the nonvolatile registers: x19 to x28, fp and lr, then d8 to d15. */
typedef struct A64_POINTERS {
    uint64_t *x[12];
    uint64_t *d[8];
} A64_POINTERS;

typedef struct A64_UNWIND_RESULT {
    /* the caller's stack pointer after the frame has been unwound. */
    uint64_t establisher;
    /* the rva of the language handler and its data, none for a function without one. */
    uint32_t handler;
    const void *handler_data;
} A64_UNWIND_RESULT;

/*
 * the length in bytes of the function a record describes, 0 when its unwind data does not tell: a link to a
 * parent record, which a jump island has.
 */
uint32_t a64_function_length(uint64_t image_base, const A64_FUNCTION *function);

/*
 * the record of a sorted table that covers an offset into the image, or null. a link record covers the bytes
 * up to the next record.
 */
const A64_FUNCTION *a64_find_function(uint64_t image_base, const A64_FUNCTION *table, uint32_t count, uint32_t rva);

/*
 * undoes the frame of the function at pc: the context becomes the caller's, as it was when it made the call.
 * reads of the stack outside of [low, high) fail the unwind, which returns 0, as does unwind data it does not know.
 * a function without unwind data is a leaf: a64_unwind_leaf.
 */
int a64_virtual_unwind(uint64_t image_base, uint64_t pc, const A64_FUNCTION *function, A64_CONTEXT *context,
                       A64_POINTERS *pointers, A64_UNWIND_RESULT *result, uint64_t low, uint64_t high);

/* the caller of a function that uses neither the stack nor a register of the caller: it returns through lr. */
void a64_unwind_leaf(A64_CONTEXT *context);

#endif
