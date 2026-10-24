/*
 * calls from x64 code into native code of the same process, for system dlls whose x64 build only forwards to a
 * native implementation, like opengl32. an x64 stub moves rcx to r10 like a system call stub and runs syscall with
 * eax = MX_NATIVE_SYSCALL | table << 16 | index. xta64 then calls the native function with the x64 argument
 * registers and stack, on the same thread.
 */
#ifndef MIME_MX64_H
#define MIME_MX64_H

#define MX_NATIVE_SYSCALL 0x7E000000u
/* r10 = table, rdx = utf-16 file name in System32, r8 = ascii name of its MX_NATIVE_TABLE export. */
#define MX_NATIVE_LOAD 0x7EFF0000u
#define MX_NATIVE_OPENGL32 1

#define MX_THREAD_SUSPEND_GATE 0x4d580001u
typedef struct MX_SUSPEND_GATE {
    volatile unsigned int Critical;
    volatile unsigned int Pending;
} MX_SUSPEND_GATE;

typedef struct MX_NATIVE_CALL {
    unsigned long long gpr[4];         /* rcx, rdx, r8, r9 */
    unsigned long long xmm[4];         /* low quadwords of xmm0 to xmm3 */
    const unsigned long long *stack;   /* the fifth argument and the ones after it */
    unsigned long long rax;            /* integer result */
    unsigned long long xmm0;           /* floating point result */
} MX_NATIVE_CALL;

typedef void (*MX_NATIVE_FN)(MX_NATIVE_CALL *call);

typedef struct MX_NATIVE_TABLE {
    unsigned int Count;
    unsigned int Pad;
    const MX_NATIVE_FN *Functions;
} MX_NATIVE_TABLE;

#endif
