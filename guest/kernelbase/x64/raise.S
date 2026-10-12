/*
 * RaiseException captures the caller's state at its own entry, so a handler
 * that continues execution resumes right after the call with every callee
 * saved register intact. KbRaiseExceptionWithContext builds the record.
 */
    .text

    .globl  RaiseException
    .p2align 4
RaiseException:
    /* CONTEXT at rsp + 0x30, the home area of the call below it. */
    subq    $0x508, %rsp
    movq    %rcx, 0x508+0x08(%rsp)
    movq    %rdx, 0x508+0x10(%rsp)
    movq    %r8, 0x508+0x18(%rsp)
    movq    %r9, 0x508+0x20(%rsp)
    leaq    0x30(%rsp), %rcx
    call    RtlCaptureContext
    leaq    0x30(%rsp), %rax
    leaq    0x508+8(%rsp), %rcx
    movq    %rcx, 0x98(%rax)        /* Rsp as the caller left it */
    movq    0x508(%rsp), %rcx
    movq    %rcx, 0xf8(%rax)        /* Rip: resume after the call */
    movq    0x508+0x08(%rsp), %rcx
    movq    0x508+0x10(%rsp), %rdx
    movq    0x508+0x18(%rsp), %r8
    movq    0x508+0x20(%rsp), %r9
    movq    %rcx, 0x80(%rax)
    movq    %rdx, 0x88(%rax)
    movq    %r8, 0xb8(%rax)
    movq    %r9, 0xc0(%rax)
    movq    %rax, 0x20(%rsp)        /* fifth argument: the context */
    call    KbRaiseExceptionWithContext
    /* only reached when the raise itself failed. */
    addq    $0x508, %rsp
    ret

    .section .drectve, "yn"
    .ascii  " -export:RaiseException"
