/*
 * AutCallEx(function, slots, count, kind, keep): pushes the slots in order and restores the stack whether or not the
 * function popped its arguments. the result is in edx:eax. for a float result, kind 1, or a double, kind 2, the value
 * is popped off the x87 stack into keep.
 */
    .text
    .globl  _AutCallEx
    .p2align 4
_AutCallEx:
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
    movl    20(%ebp), %ecx
    cmpl    $1, %ecx
    je      4f
    cmpl    $2, %ecx
    je      5f
    jmp     3f
4:
    movl    24(%ebp), %ecx
    fstps   (%ecx)
    jmp     3f
5:
    movl    24(%ebp), %ecx
    fstpl   (%ecx)
3:
    movl    %ebp, %esp
    popl    %ebp
    ret
