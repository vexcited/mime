/*
 * the thunks of proxies. A stubless method of a proxy is a thunk that says which method it is and joins the common code,
 * which puts the arguments in a frame the interpreter reads. A method of a base interface that has a proxy of its own goes
 * to that proxy, with the proxy's own interface pointer as this. Thunk i is at the start of its table plus 16 times i.
 */
    .altmacro
    .text

/* a thunk, and for the methods 3 to 32 a name that windows' proxy dlls call through combase, which forwards here. */
    .macro  stubless_thunk n
    .if \n >= 3 && \n <= 32
    .globl  ObjectStublessClient\n
    .section .drectve,"yn"
    .ascii  " /EXPORT:ObjectStublessClient\n"
    .text
ObjectStublessClient\n:
    .endif
    movl    $\n, %eax
    jmp     NdrpStublessCommon
    .p2align 4
    .endm

    .macro  stubless_thunks first, last
    stubless_thunk %first
    .if \first < \last
    stubless_thunks %(\first + 1), \last
    .endif
    .endm

    .p2align 4
    .globl  NdrpStublessThunks
NdrpStublessThunks:
    stubless_thunks 0, 15
    stubless_thunks 16, 32
    .set    i, 33
    .rept   991
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

    .macro  delegate_thunk n
    .if \n >= 3 && \n <= 32
    .globl  NdrProxyForwardingFunction\n
    .section .drectve,"yn"
    .ascii  " /EXPORT:NdrProxyForwardingFunction\n"
    .text
NdrProxyForwardingFunction\n:
    .endif
    movq    8(%rcx), %rcx
    movq    (%rcx), %rax
    jmp     *(\n * 8)(%rax)
    .p2align 5
    .endm

    .macro  delegate_thunks first, last
    delegate_thunk %first
    .if \first < \last
    delegate_thunks %(\first + 1), \last
    .endif
    .endm

    .p2align 5
    .globl  NdrpDelegateThunks
NdrpDelegateThunks:
    delegate_thunks 0, 15
    delegate_thunks 16, 32
    .set    i, 33
    .rept   95
    movq    8(%rcx), %rcx
    movq    (%rcx), %rax
    jmp     *(i * 8)(%rax)
    .p2align 5
    .set    i, i + 1
    .endr
