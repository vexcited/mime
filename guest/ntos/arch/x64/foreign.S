#include "ktrap.h"

#define LOCK_OWNER (KI_LOCK_OWNER * 4)

    .text

/*
 * r11 is the KI_FOREIGN_IMPORT of the export, see KiForeignImport. the call is counted, then made as it is when the
 * export needs no kernel lock or this processor holds it already, and through KiForeignCallLocked otherwise.
 */
    .globl  KiForeignCall
    .p2align 4
KiForeignCall:
    lock incq 8(%r11)
    testb   $1, 16(%r11)
    jnz     1f
    /* rax is no scratch here: __chkstk-like exports take it as an argument. */
    movl    %gs:KPCR_NUMBER, %r10d
    cmpl    %r10d, KiKernelLock+LOCK_OWNER(%rip)
    jne     KiForeignCallLocked
1:  jmpq    *(%r11)

/*
 * a frame of its own: the four register arguments and the first four vector ones are kept across KiForeignEnter,
 * and sixteen stack arguments are copied below them. the frame holds 0x20 of home space, the copied arguments at
 * 0x20, the registers at 0xA0, the vectors at 0xC0 and what KiForeignEnter answered at 0x100.
 */
    .p2align 4
    .seh_proc KiForeignCallLocked
KiForeignCallLocked:
    pushq   %rbx
    .seh_pushreg %rbx
    subq    $0x110, %rsp
    .seh_stackalloc 0x110
    .seh_endprologue
    movq    %r11, %rbx
    movq    %rcx, 0xA0(%rsp)
    movq    %rdx, 0xA8(%rsp)
    movq    %r8, 0xB0(%rsp)
    movq    %r9, 0xB8(%rsp)
    movdqu  %xmm0, 0xC0(%rsp)
    movdqu  %xmm1, 0xD0(%rsp)
    movdqu  %xmm2, 0xE0(%rsp)
    movdqu  %xmm3, 0xF0(%rsp)
    leaq    0x20(%rsp), %rcx
    leaq    0x140(%rsp), %rdx           /* the caller's fifth argument, past the return address and its home space */
    call    KiForeignEnter
    movq    %rax, 0x100(%rsp)
    movq    0xA0(%rsp), %rcx
    movq    0xA8(%rsp), %rdx
    movq    0xB0(%rsp), %r8
    movq    0xB8(%rsp), %r9
    movdqu  0xC0(%rsp), %xmm0
    movdqu  0xD0(%rsp), %xmm1
    movdqu  0xE0(%rsp), %xmm2
    movdqu  0xF0(%rsp), %xmm3
    callq   *(%rbx)
    .globl  KiForeignReturn
KiForeignReturn:
    movq    %rax, 0xA0(%rsp)
    movdqu  %xmm0, 0xC0(%rsp)
    movq    0x100(%rsp), %rcx
    call    KiForeignLeave
    movq    0xA0(%rsp), %rax
    movdqu  0xC0(%rsp), %xmm0
    addq    $0x110, %rsp
    popq    %rbx
    ret
    .seh_endproc
