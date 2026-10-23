/*
 * proxy thunks. a stubless method loads its method number and jumps to the common code, which builds the frame the
 * interpreter reads. a method of a base interface with its own proxy is forwarded to that proxy with its own interface
 * pointer as this. thunk i is at 16 * i from the start of its table.
 */
    .altmacro
    .text

/* methods 3 to 32 also get the exported names that proxy dlls reach through combase. */
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
 * eax is the method number. the integer registers go to the home slots the caller reserved below the stack
 * arguments, and the float registers to a separate area.
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
