/*
 * calls a function with arguments held in slots: the first four go to the integer registers and, the same bits, to the
 * floating point ones, since the engine does not know which are which, and the rest on the stack. The function is given at
 * least the four slots of home space. rcx is the function, rdx the slots, r8 their number, the fifth argument where to keep xmm0,
 * which holds the result of a function that returns a float, or null; the result is in rax.
 */
    .text
    .globl  NdrpInvokeEx
    .p2align 4
    .seh_proc NdrpInvokeEx
NdrpInvokeEx:
    pushq   %rbp
    .seh_pushreg %rbp
    movq    %rsp, %rbp
    .seh_setframe %rbp, 0
    .seh_endprologue
    pushq   %rcx
    pushq   %rdx
    pushq   0x30(%rbp)
    pushq   0x30(%rbp)
    movl    %r8d, %r8d
    movq    %r8, %rcx
    cmpq    $4, %rcx
    jae     1f
    movl    $4, %ecx
1:
    addq    $1, %rcx
    andq    $-2, %rcx
    shlq    $3, %rcx
    movq    %rcx, %rax
    call    __chkstk
    subq    %rax, %rsp
    movq    -16(%rbp), %rdx
    xorl    %r9d, %r9d
2:
    cmpq    %r8, %r9
    jae     3f
    movq    (%rdx,%r9,8), %rax
    movq    %rax, (%rsp,%r9,8)
    incq    %r9
    jmp     2b
3:
    movq    (%rsp), %rcx
    movq    %rcx, %xmm0
    movq    8(%rsp), %rdx
    movq    %rdx, %xmm1
    movq    16(%rsp), %r8
    movq    %r8, %xmm2
    movq    24(%rsp), %r9
    movq    %r9, %xmm3
    call    *-8(%rbp)
    movq    -24(%rbp), %rcx
    testq   %rcx, %rcx
    jz      4f
    movq    %xmm0, (%rcx)
4:
    movq    %rbp, %rsp
    popq    %rbp
    ret
    .seh_endproc
