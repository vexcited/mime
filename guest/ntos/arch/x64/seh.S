/* kernel mode structured exception handling: capturing, raising and restoring contexts. */
#include "ktrap.h"

    .text

/* void RtlCaptureContext(PCONTEXT context): the caller's state at the return address, with kernel selectors. */
    .globl  RtlCaptureContext
    .p2align 4
RtlCaptureContext:
    pushfq
    movq    %rax, 0x78(%rcx)
    movq    %rcx, 0x80(%rcx)
    movq    %rdx, 0x88(%rcx)
    movq    %rbx, 0x90(%rcx)
    leaq    16(%rsp), %rax
    movq    %rax, 0x98(%rcx)
    movq    %rbp, 0xa0(%rcx)
    movq    %rsi, 0xa8(%rcx)
    movq    %rdi, 0xb0(%rcx)
    movq    %r8, 0xb8(%rcx)
    movq    %r9, 0xc0(%rcx)
    movq    %r10, 0xc8(%rcx)
    movq    %r11, 0xd0(%rcx)
    movq    %r12, 0xd8(%rcx)
    movq    %r13, 0xe0(%rcx)
    movq    %r14, 0xe8(%rcx)
    movq    %r15, 0xf0(%rcx)
    movq    8(%rsp), %rax
    movq    %rax, 0xf8(%rcx)
    popq    %rax
    movl    %eax, 0x44(%rcx)
    movl    $0x10000b, 0x30(%rcx)
    movw    $KGDT64_R0_CODE, 0x38(%rcx)
    movw    $(KGDT64_R3_DATA | RPL_USER), 0x3a(%rcx)
    movw    $(KGDT64_R3_DATA | RPL_USER), 0x3c(%rcx)
    movw    $(KGDT64_R3_CMTEB | RPL_USER), 0x3e(%rcx)
    movw    $(KGDT64_R3_DATA | RPL_USER), 0x40(%rcx)
    movw    $KGDT64_R0_DATA, 0x42(%rcx)
    fxsave  0x100(%rcx)
    stmxcsr 0x34(%rcx)
    movq    0x78(%rcx), %rax
    ret

/* void RtlRaiseException(PEXCEPTION_RECORD record): raises with the caller's state, resuming after the call. */
    .globl  RtlRaiseException
    .p2align 4
    .seh_proc RtlRaiseException
RtlRaiseException:
    subq    $0x508, %rsp
    .seh_stackalloc 0x508
    .seh_endprologue
    movq    %rcx, 0x508+0x08(%rsp)
    leaq    0x30(%rsp), %rcx
    call    RtlCaptureContext
    leaq    0x30(%rsp), %rdx
    leaq    0x508+8(%rsp), %rax
    movq    %rax, 0x98(%rdx)
    movq    0x508(%rsp), %rax
    movq    %rax, 0xf8(%rdx)
    movq    0x508+0x08(%rsp), %rcx
    movq    %rcx, 0x80(%rdx)
    movq    %rax, 0x10(%rcx)
    call    KiRaiseKernelException
    int3
    .seh_endproc

/*
 * EXCEPTION_DISPOSITION RtlpExecuteHandlerForUnwind(record, frame, context, dispatcher, handler)
 * calls the handler under RtlpUnwindHandler, which turns an exception raised from inside it into a collided unwind.
 */
    .globl  RtlpExecuteHandlerForUnwind
    .p2align 4
    .seh_proc RtlpExecuteHandlerForUnwind
RtlpExecuteHandlerForUnwind:
    subq    $0x28, %rsp
    .seh_stackalloc 0x28
    .seh_handler RtlpUnwindHandler, @unwind, @except
    .seh_endprologue
    movq    %r9, 0x20(%rsp)
    call    *0x50(%rsp)
    nop
    addq    $0x28, %rsp
    ret
    .seh_endproc

/* void KiRestoreKernelContext(PCONTEXT context): continues in kernel mode where the context says. */
    .globl  KiRestoreKernelContext
    .p2align 4
KiRestoreKernelContext:
    movq    %rcx, %rax
    fxrstor 0x100(%rax)
    ldmxcsr 0x34(%rax)
    movq    0x88(%rax), %rdx
    movq    0x90(%rax), %rbx
    movq    0xa0(%rax), %rbp
    movq    0xa8(%rax), %rsi
    movq    0xb0(%rax), %rdi
    movq    0xb8(%rax), %r8
    movq    0xc0(%rax), %r9
    movq    0xc8(%rax), %r10
    movq    0xd0(%rax), %r11
    movq    0xd8(%rax), %r12
    movq    0xe0(%rax), %r13
    movq    0xe8(%rax), %r14
    movq    0xf0(%rax), %r15
    /* an iretq frame: ss, rsp, rflags, cs, rip. */
    pushq   $KGDT64_R0_DATA
    pushq   0x98(%rax)
    movl    0x44(%rax), %ecx
    pushq   %rcx
    pushq   $KGDT64_R0_CODE
    pushq   0xf8(%rax)
    movq    0x80(%rax), %rcx
    movq    0x78(%rax), %rax
    iretq
