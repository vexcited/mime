/*
 * the exports of mscoree.dll that only forward to the runtime. each loads its index into eax and comes here, where
 * the function of the runtime is found and jumped to with the registers and the stack as the caller made them.
 * without a runtime the call fails with an error and returns.
 */
    .text
    .globl  mscoree_thunk_common
    .p2align 4
    .seh_proc mscoree_thunk_common
mscoree_thunk_common:
    movq    %rcx, 8(%rsp)
    movq    %rdx, 16(%rsp)
    movq    %r8, 24(%rsp)
    movq    %r9, 32(%rsp)
    subq    $40, %rsp
    .seh_stackalloc 40
    .seh_endprologue
    movl    %eax, %ecx
    call    mscoree_resolve
    addq    $40, %rsp
    movq    8(%rsp), %rcx
    movq    16(%rsp), %rdx
    movq    24(%rsp), %r8
    movq    32(%rsp), %r9
    testq   %rax, %rax
    jz      1f
    jmp     *%rax
1:
    movl    $0x80131701, %eax
    ret
    .seh_endproc
