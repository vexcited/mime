/*
 * the thunks of proxies, as in the x64 file. The methods are stdcall, so after the call the common code takes the
 * arguments off the stack for the caller, as many bytes as the procedure's frame is.
 */
    .text
    .p2align 4
    .globl  _NdrpStublessThunks
_NdrpStublessThunks:
    .set    i, 0
    .rept   1024
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

    .p2align 5
    .globl  _NdrpDelegateThunks
_NdrpDelegateThunks:
    .set    i, 0
    .rept   128
    movl    4(%esp), %eax
    movl    4(%eax), %eax
    movl    %eax, 4(%esp)
    movl    (%eax), %eax
    jmp     *(i * 4)(%eax)
    .p2align 5
    .set    i, i + 1
    .endr
