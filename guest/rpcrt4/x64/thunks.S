/*
 * the thunks of proxies. A stubless method of a proxy is a thunk that says which method it is and joins the common code,
 * which puts the arguments in a frame the interpreter reads. A method of a base interface that has a proxy of its own goes
 * to that proxy, with the proxy's own interface pointer as this. Thunk i is at the start of its table plus 16 times i.
 */
    .text
    .p2align 4
    .globl  NdrpStublessThunks
NdrpStublessThunks:
    .set    i, 0
    .rept   1024
    movl    $i, %eax
    jmp     NdrpStublessCommon
    .p2align 4
    .set    i, i + 1
    .endr

/*
 * eax is the method. The four registers go to their home slots, which the caller left room for right below the other
 * arguments, and the four floating point ones to a place of their own.
 */
    .globl  NdrpStublessCommon
    .seh_proc NdrpStublessCommon
NdrpStublessCommon:
    movq    %rcx, 8(%rsp)
    movq    %rdx, 16(%rsp)
    movq    %r8, 24(%rsp)
    movq    %r9, 32(%rsp)
    subq    $72, %rsp
    .seh_stackalloc 72
    .seh_endprologue
    movq    %xmm0, 32(%rsp)
    movq    %xmm1, 40(%rsp)
    movq    %xmm2, 48(%rsp)
    movq    %xmm3, 56(%rsp)
    movl    %eax, %ecx
    leaq    80(%rsp), %rdx
    leaq    32(%rsp), %r8
    xorl    %r9d, %r9d
    call    NdrpStublessClient
    addq    $72, %rsp
    ret
    .seh_endproc

    .p2align 5
    .globl  NdrpDelegateThunks
NdrpDelegateThunks:
    .set    i, 0
    .rept   128
    movq    8(%rcx), %rcx
    movq    (%rcx), %rax
    jmp     *(i * 8)(%rax)
    .p2align 5
    .set    i, i + 1
    .endr
