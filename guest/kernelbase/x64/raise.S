/*
 * RaiseException raises from a context that lies inside itself, as Windows does: a handler that looks at the
 * instruction pointer of the context finds it in kernelbase, not in the caller, and a handler that continues
 * resumes in RaiseException, which then returns to the caller. The runtime of the .NET Framework relies on it, it
 * takes an access violation whose instruction pointer is in its own module for a fault of its own.
 */
    .text

    .globl  RaiseException
    .p2align 4
    .seh_proc RaiseException
RaiseException:
    /* CONTEXT at rsp + 0x30, above the home area of the calls below. */
    subq    $0x508, %rsp
    .seh_stackalloc 0x508
    .seh_endprologue
    movq    %rcx, 0x508+0x08(%rsp)
    movq    %rdx, 0x508+0x10(%rsp)
    movq    %r8, 0x508+0x18(%rsp)
    movq    %r9, 0x508+0x20(%rsp)
    leaq    0x30(%rsp), %rcx
    call    RtlCaptureContext
    leaq    0x30(%rsp), %rax
    leaq    RaiseExceptionResume(%rip), %rcx
    movq    %rcx, 0xf8(%rax)        /* Rip: where a handler that continues goes on */
    movq    %rsp, 0x98(%rax)        /* Rsp of that place, the frame as it stands */
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
RaiseExceptionResume:
    addq    $0x508, %rsp
    ret
    .seh_endproc

    .section .drectve, "yn"
    .ascii  " -export:RaiseException"
