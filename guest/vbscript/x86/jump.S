/* a jump back out of the parser: the registers a function keeps across calls are saved and restored whole. */
    .text
    .globl _vbs_setjmp
_vbs_setjmp:
    movl 4(%esp), %eax
    movl %ebx, 0(%eax)
    movl %ebp, 4(%eax)
    movl %esi, 8(%eax)
    movl %edi, 12(%eax)
    leal 4(%esp), %ecx
    movl %ecx, 16(%eax)
    movl (%esp), %ecx
    movl %ecx, 20(%eax)
    xorl %eax, %eax
    ret
    .globl _vbs_longjmp
_vbs_longjmp:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    movl 0(%edx), %ebx
    movl 4(%edx), %ebp
    movl 8(%edx), %esi
    movl 12(%edx), %edi
    movl 16(%edx), %esp
    jmp *20(%edx)
