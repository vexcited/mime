/*
 * user mode entry points of the x64 ntdll, reached from mimex86.dll the way
 * the x64 kernel would reach them.
 *
 * KiUserExceptionDispatcher  rcx = &EXCEPTION_RECORD, rdx = &CONTEXT
 * KiUserApcDispatcher        unused: mimex86.dll calls apc routines itself
 * KiUserCallbackDispatcher   ecx = api index, rdx = arguments, r8d = argument length
 * LdrInitializeThunk         rcx = &CONTEXT of the new thread, rdx = ntdll base
 */
    .text

    .globl  KiUserExceptionDispatcher
    .p2align 4
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

    .globl  KiUserApcDispatcher
    .p2align 4
KiUserApcDispatcher:
    int3
    jmp     .

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
