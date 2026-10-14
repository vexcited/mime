    .text
    .globl __chkstk
__chkstk:
    pushl %ecx
    leal 8(%esp), %ecx
1:
    cmpl $4096, %eax
    jb 2f
    subl $4096, %ecx
    testl %eax, (%ecx)
    subl $4096, %eax
    jmp 1b
2:
    subl %eax, %ecx
    testl %eax, (%ecx)
    movl %esp, %eax
    movl %ecx, %esp
    movl (%eax), %ecx
    pushl 4(%eax)
    retl
