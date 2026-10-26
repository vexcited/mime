/*
 * the exports of mscoree.dll that only forward to the runtime. each pushes its index and comes here, where the
 * function of the runtime is found and jumped to with the arguments as the caller pushed them. without a runtime the
 * call fails with an error, popping the bytes of arguments the function would have.
 */
    .text
    .globl  _mscoree_thunk_common
    .p2align 4
_mscoree_thunk_common:
    call    _mscoree_resolve
    popl    %edx
    testl   %eax, %eax
    jz      1f
    jmpl    *%eax
1:
    movzwl  _mscoree_thunk_bytes(,%edx,2), %ecx
    popl    %edx
    addl    %ecx, %esp
    movl    $0x80131701, %eax
    jmpl    *%edx
