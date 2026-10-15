/*
 * user mode entry points of the x64 ntdll, reached from the x64 kernel, or
 * from mimex86.dll the way the x64 kernel would reach them.
 *
 * KiUserExceptionDispatcher  rcx = &EXCEPTION_RECORD, rdx = &CONTEXT
 * KiUserApcDispatcher        rsp = &CONTEXT, the routine and its arguments in the home slots,
 *                            unused by mimex86.dll, which calls apc routines itself
 * KiUserCallbackDispatcher   ecx = api index, rdx = arguments, r8d = argument length
 * LdrInitializeThunk         rcx = &CONTEXT of the new thread, rdx = ntdll base
 */
    .text

/*
 * mimex86 leaves 0x30 bytes, the CONTEXT, the EXCEPTION_RECORD in 0xa0
 * bytes and a machine frame above the stack pointer. the unwind codes say
 * so, as on windows: an unwind through the dispatcher restores the
 * nonvolatile registers from the context and goes on at the faulting rip
 * and rsp.
 */
    .globl  KiUserExceptionDispatcher
    .p2align 4
    .seh_proc KiUserExceptionDispatcher
    .seh_pushframe
    .seh_stackalloc 0x5a0
    .seh_savereg %rbx, 0x30+0x90
    .seh_savereg %rbp, 0x30+0xa0
    .seh_savereg %rsi, 0x30+0xa8
    .seh_savereg %rdi, 0x30+0xb0
    .seh_savereg %r12, 0x30+0xd8
    .seh_savereg %r13, 0x30+0xe0
    .seh_savereg %r14, 0x30+0xe8
    .seh_savereg %r15, 0x30+0xf0
    .seh_endprologue
KiUserExceptionDispatcher:
    movq    %rcx, %rbx
    movq    %rdx, %rsi
    call    KiUserExceptionDispatcherC
    /* not reached unless the c dispatcher returns: raise as second chance. */
    movq    %rbx, %rcx
    movq    %rsi, %rdx
    xorl    %r8d, %r8d
    call    NtRaiseException
    jmp     .
    .seh_endproc

/*
 * the kernel leaves the interrupted CONTEXT at the stack pointer, P1Home
 * to P4Home holding the context, both arguments and the routine, then a
 * machine frame, so an unwind through here goes on at the interrupted code.
 */
    .globl  KiUserApcDispatcher
    .p2align 4
    .seh_proc KiUserApcDispatcher
    .seh_pushframe
    .seh_stackalloc 0x4d0
    .seh_endprologue
KiUserApcDispatcher:
    movq    0x00(%rsp), %rcx
    movq    0x08(%rsp), %rdx
    movq    0x10(%rsp), %r8
    movq    0x18(%rsp), %rax
    movq    %rsp, %r9
    call    *%rax
    movq    %rsp, %rcx
    movl    $1, %edx
    call    NtContinue
    int3
    .seh_endproc

    .globl  KiUserCallbackDispatcher
    .p2align 4
KiUserCallbackDispatcher:
    /* PEB->KernelCallbackTable[api](arguments, length), then NtCallbackReturn. */
    subq    $0x28, %rsp
    movq    %gs:0x60, %rax          /* TEB->ProcessEnvironmentBlock */
    movq    0x58(%rax), %rax        /* PEB->KernelCallbackTable */
    movl    %ecx, %ecx
    movq    (%rax,%rcx,8), %rax
    movq    %rdx, %rcx
    movl    %r8d, %edx
    call    *%rax
    movl    %eax, %r8d              /* status */
    xorl    %ecx, %ecx
    xorl    %edx, %edx
    call    NtCallbackReturn
    jmp     .

    .globl  LdrInitializeThunk
    .p2align 4
LdrInitializeThunk:
    movq    %rcx, %rbx
    subq    $0x28, %rsp
    call    LdrpInitialize
    movq    %rbx, %rcx
    movl    $1, %edx
    call    NtContinue
    jmp     .

/* void RtlCaptureContext(PCONTEXT context): the caller's state at the return address. */
    .globl  RtlCaptureContext
    .p2align 4
RtlCaptureContext:
    pushfq
    movq    %rax, 0x78(%rcx)
    movq    %rcx, 0x80(%rcx)
    movq    %rdx, 0x88(%rcx)
    movq    %rbx, 0x90(%rcx)
    leaq    16(%rsp), %rax          /* the caller's rsp, past the return address and the flags */
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
    movq    %rax, 0xf8(%rcx)        /* Rip */
    popq    %rax
    movl    %eax, 0x44(%rcx)        /* EFlags */
    movl    $0x10000b, 0x30(%rcx)   /* CONTEXT_AMD64_FULL */
    movw    $0x33, 0x38(%rcx)
    movw    $0x2b, 0x3a(%rcx)
    movw    $0x2b, 0x3c(%rcx)
    movw    $0x53, 0x3e(%rcx)
    movw    $0x2b, 0x40(%rcx)
    movw    $0x2b, 0x42(%rcx)
    fxsave  0x100(%rcx)
    stmxcsr 0x34(%rcx)
    movq    0x78(%rcx), %rax
    ret

/* void RtlRaiseException(PEXCEPTION_RECORD record): raises with the caller's state, resuming after the call. */
    .globl  RtlRaiseException
    .p2align 4
RtlRaiseException:
    subq    $0x508, %rsp
    movq    %rcx, 0x508+0x08(%rsp)
    leaq    0x30(%rsp), %rcx
    call    RtlCaptureContext
    leaq    0x30(%rsp), %rdx
    leaq    0x508+8(%rsp), %rax
    movq    %rax, 0x98(%rdx)        /* Rsp as the caller left it */
    movq    0x508(%rsp), %rax
    movq    %rax, 0xf8(%rdx)        /* Rip: resume after the call */
    movq    0x508+0x08(%rsp), %rcx
    movq    %rcx, 0x80(%rdx)
    movq    %rax, 0x10(%rcx)        /* record->ExceptionAddress */
    movl    $1, %r8d
    call    NtRaiseException
    movl    %eax, %ecx
    call    RtlRaiseStatus
    int3

/*
 * PVOID RtlpCallConsolidate(PCONTEXT context, PVOID (*callback)(PEXCEPTION_RECORD), PEXCEPTION_RECORD record)
 *
 * runs the callback of STATUS_UNWIND_CONSOLIDATE, the catch block of a c++
 * exception, with the nonvolatile registers of the target frame. a machine
 * frame holding the target's rip and rsp makes an unwind from inside the
 * callback continue straight at the target frame, as on windows.
 */
    .globl  RtlpCallConsolidate
    .p2align 4
    .seh_proc RtlpCallConsolidate
RtlpCallConsolidate:
    subq    $0x28, %rsp
    movq    0xf8(%rcx), %rax        /* context->Rip */
    movq    %rax, (%rsp)
    movq    0x98(%rcx), %rax        /* context->Rsp */
    movq    %rax, 0x18(%rsp)
    .seh_pushframe
    subq    $0x100, %rsp            /* the shadow space, seven registers, xmm6 to xmm15 */
    .seh_stackalloc 0x100
    .seh_endprologue
    movq    %rbx, 0x20(%rsp)
    movq    %rsi, 0x28(%rsp)
    movq    %rdi, 0x30(%rsp)
    movq    %r12, 0x38(%rsp)
    movq    %r13, 0x40(%rsp)
    movq    %r14, 0x48(%rsp)
    movq    %r15, 0x50(%rsp)
    movdqa  %xmm6, 0x60(%rsp)
    movdqa  %xmm7, 0x70(%rsp)
    movdqa  %xmm8, 0x80(%rsp)
    movdqa  %xmm9, 0x90(%rsp)
    movdqa  %xmm10, 0xa0(%rsp)
    movdqa  %xmm11, 0xb0(%rsp)
    movdqa  %xmm12, 0xc0(%rsp)
    movdqa  %xmm13, 0xd0(%rsp)
    movdqa  %xmm14, 0xe0(%rsp)
    movdqa  %xmm15, 0xf0(%rsp)
    movq    0x90(%rcx), %rbx
    movq    0xa0(%rcx), %rax
    movq    %rbp, 0x58(%rsp)
    movq    %rax, %rbp
    movq    0xa8(%rcx), %rsi
    movq    0xb0(%rcx), %rdi
    movq    0xd8(%rcx), %r12
    movq    0xe0(%rcx), %r13
    movq    0xe8(%rcx), %r14
    movq    0xf0(%rcx), %r15
    movdqu  0x200(%rcx), %xmm6
    movdqu  0x210(%rcx), %xmm7
    movdqu  0x220(%rcx), %xmm8
    movdqu  0x230(%rcx), %xmm9
    movdqu  0x240(%rcx), %xmm10
    movdqu  0x250(%rcx), %xmm11
    movdqu  0x260(%rcx), %xmm12
    movdqu  0x270(%rcx), %xmm13
    movdqu  0x280(%rcx), %xmm14
    movdqu  0x290(%rcx), %xmm15
    movq    %r8, %rcx
    call    *%rdx
    movq    0x20(%rsp), %rbx
    movq    0x28(%rsp), %rsi
    movq    0x30(%rsp), %rdi
    movq    0x38(%rsp), %r12
    movq    0x40(%rsp), %r13
    movq    0x48(%rsp), %r14
    movq    0x50(%rsp), %r15
    movq    0x58(%rsp), %rbp
    movdqa  0x60(%rsp), %xmm6
    movdqa  0x70(%rsp), %xmm7
    movdqa  0x80(%rsp), %xmm8
    movdqa  0x90(%rsp), %xmm9
    movdqa  0xa0(%rsp), %xmm10
    movdqa  0xb0(%rsp), %xmm11
    movdqa  0xc0(%rsp), %xmm12
    movdqa  0xd0(%rsp), %xmm13
    movdqa  0xe0(%rsp), %xmm14
    movdqa  0xf0(%rsp), %xmm15
    addq    $0x128, %rsp
    ret
    .seh_endproc
