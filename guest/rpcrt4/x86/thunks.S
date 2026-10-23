/*
 * proxy thunks, as on x64. the methods are stdcall, so the common code pops the procedure's frame size off the
 * caller's stack after the call.
 */
    .altmacro
    .text

/* methods 3 to 32 also get the exported names that proxy dlls reach through combase. */
    .macro  stubless_thunk n
    .if \n >= 3 && \n <= 32
    .globl  _ObjectStublessClient\n
_ObjectStublessClient\n:
    .endif
    pushl   $\n
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
    .globl  _NdrpStublessThunks
_NdrpStublessThunks:
    stubless_thunks 0, 15
    stubless_thunks 16, 32
    .set    i, 33
    .rept   991
    pushl   $i
    jmp     NdrpStublessCommon
    .p2align 4
    .set    i, i + 1
    .endr

NdrpStublessCommon:
    pushl   %ebp
    movl    %esp, %ebp
    subl    $8, %esp
    leal    -4(%ebp), %eax
    pushl   %eax
    pushl   $0
    leal    12(%ebp), %eax
    pushl   %eax
    pushl   4(%ebp)
    call    _NdrpStublessClient
    movl    -4(%ebp), %ecx
    movl    8(%ebp), %edx
    leal    12(%ebp,%ecx), %ecx
    movl    (%ebp), %ebp
    leal    -4(%ecx), %esp
    movl    %edx, (%esp)
    ret

    .macro  delegate_thunk n
    .if \n >= 3 && \n <= 32
    .globl  _NdrProxyForwardingFunction\n
_NdrProxyForwardingFunction\n:
    .endif
    movl    4(%esp), %eax
    movl    4(%eax), %eax
    movl    %eax, 4(%esp)
    movl    (%eax), %eax
    jmp     *(\n * 4)(%eax)
    .p2align 5
    .endm

    .macro  delegate_thunks first, last
    delegate_thunk %first
    .if \first < \last
    delegate_thunks %(\first + 1), \last
    .endif
    .endm

    .p2align 5
    .globl  _NdrpDelegateThunks
_NdrpDelegateThunks:
    delegate_thunks 0, 15
    delegate_thunks 16, 32
    .set    i, 33
    .rept   95
    movl    4(%esp), %eax
    movl    4(%eax), %eax
    movl    %eax, 4(%esp)
    movl    (%eax), %eax
    jmp     *(i * 4)(%eax)
    .p2align 5
    .set    i, i + 1
    .endr
