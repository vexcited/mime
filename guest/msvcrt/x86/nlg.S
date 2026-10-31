/*
 * the x86 parts the universal crt keeps in assembly for debuggers and the old compilers: the non-local goto
 * notification that tells a debugger a frame's blocks run, the check of the stack pointer after a call, and the
 * report of a failed stack cookie check.
 */
    .text

/* the record debuggers read: its signature, the code, the address and the frame. */
    .data
    .p2align 2
nlg_destination:
    .long   0x19930520, 0, 0, 0

    .text

/* __NLG_Notify1 takes the code in ecx, __NLG_Notify on the stack, and both tell the debugger the address in eax. */
    .p2align 2
nlg_notify1:
    pushl   %ebx
    pushl   %ecx
    movl    $nlg_destination, %ebx
    jmp     1f

nlg_notify:
    pushl   %ebx
    pushl   %ecx
    movl    $nlg_destination, %ebx
    movl    12(%esp), %ecx
1:  movl    %ecx, 8(%ebx)
    movl    %eax, 4(%ebx)
    movl    %ebp, 12(%ebx)
    pushl   %ebp
    pushl   %ecx
    pushl   %eax
    .globl  __NLG_Dispatch2
__NLG_Dispatch2:
    popl    %eax
    popl    %ecx
    popl    %ebp
    popl    %ecx
    popl    %ebx
    retl    $4

nlg_call:
    calll   *%eax
    .globl  __NLG_Return2
__NLG_Return2:
    retl

/* _CallSettingFrame runs a block with the ebp of a frame, and __NLG_Return is where the block returns to. */
    .globl  __CallSettingFrame@12
__CallSettingFrame@12:
    pushl   %ebp
    movl    %esp, %ebp
    subl    $4, %esp
    pushl   %ebx
    pushl   %ecx
    movl    12(%ebp), %eax
    addl    $12, %eax
    movl    %eax, -4(%ebp)
    movl    8(%ebp), %eax
    pushl   %ebp
    pushl   16(%ebp)
    movl    16(%ebp), %ecx
    movl    -4(%ebp), %ebp
    calll   nlg_notify1
    pushl   %esi
    pushl   %edi
    calll   *%eax
    .globl  __NLG_Return
__NLG_Return:
    popl    %edi
    popl    %esi
    movl    %ebp, %ebx
    popl    %ebp
    movl    16(%ebp), %ecx
    pushl   %ebp
    movl    %ebx, %ebp
    cmpl    $0x100, %ecx
    jne     2f
    movl    $2, %ecx
2:  pushl   %ecx
    calll   nlg_notify1
    popl    %ebp
    popl    %ecx
    popl    %ebx
    leave
    retl    $12

/* _chkesp: after a call, the flags say whether esp is where the caller expected, and a difference traps. */
    .globl  __chkesp
__chkesp:
    jne     3f
    retl
3:  pushl   %ebp
    movl    %esp, %ebp
    pushl   %eax
    pushl   %edx
    pushl   %ebx
    pushl   %esi
    pushl   %edi
    int3
    popl    %edi
    popl    %esi
    popl    %ebx
    popl    %edx
    popl    %eax
    movl    %ebp, %esp
    popl    %ebp
    retl

/* _report_gsfailure saves the registers where the check failed for MsvcrtReportGsFailure. */
    .globl  ___report_gsfailure
___report_gsfailure:
    pushfl
    pushl   %edi
    pushl   %esi
    pushl   %ebx
    pushl   %edx
    pushl   %ecx
    pushl   %ebp
    pushl   %ss
    pushl   %cs
    pushl   %ds
    pushl   %es
    pushl   %fs
    pushl   %gs
    pushl   %esp
    calll   _MsvcrtReportGsFailure
