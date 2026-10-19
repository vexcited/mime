/*
 * calls a function with arguments held in slots, pushed in order, and takes the stack back whether the function popped its
 * arguments or not. The result is in eax. NdrpInvoke(function, slots, count).
 */
    .text
    .globl  _NdrpInvoke
    .p2align 4
_NdrpInvoke:
    pushl   %ebp
    movl    %esp, %ebp
    movl    16(%ebp), %ecx
    movl    12(%ebp), %edx
1:
    testl   %ecx, %ecx
    jz      2f
    pushl   -4(%edx,%ecx,4)
    decl    %ecx
    jmp     1b
2:
    call    *8(%ebp)
    movl    %ebp, %esp
    popl    %ebp
    ret
