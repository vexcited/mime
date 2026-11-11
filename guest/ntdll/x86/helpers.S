    .text

/* msvc's helpers pop their 64-bit operands from the stack. */
    .globl __allmul
__allmul:
    pushl %ebx
    movl 8(%esp), %eax
    movl 16(%esp), %ebx
    movl 12(%esp), %ecx
    imull %ebx, %ecx
    movl 20(%esp), %edx
    imull %eax, %edx
    addl %edx, %ecx
    mull %ebx
    addl %ecx, %edx
    popl %ebx
    retl $16

/* a shift takes its value in edx:eax and its count in cl. */
    .globl __allshl
__allshl:
    cmpb $64, %cl
    jae 2f
    cmpb $32, %cl
    jae 1f
    shldl %cl, %eax, %edx
    shll %cl, %eax
    retl
1:
    movl %eax, %edx
    xorl %eax, %eax
    andb $31, %cl
    shll %cl, %edx
    retl
2:
    xorl %eax, %eax
    xorl %edx, %edx
    retl

    .globl __allshr
__allshr:
    cmpb $64, %cl
    jae 2f
    cmpb $32, %cl
    jae 1f
    shrdl %cl, %edx, %eax
    sarl %cl, %edx
    retl
1:
    movl %edx, %eax
    sarl $31, %edx
    andb $31, %cl
    sarl %cl, %eax
    retl
2:
    sarl $31, %edx
    movl %edx, %eax
    retl

    .globl __aullshr
__aullshr:
    cmpb $64, %cl
    jae 2f
    cmpb $32, %cl
    jae 1f
    shrdl %cl, %edx, %eax
    shrl %cl, %edx
    retl
1:
    movl %edx, %eax
    xorl %edx, %edx
    andb $31, %cl
    shrl %cl, %eax
    retl
2:
    xorl %eax, %eax
    xorl %edx, %edx
    retl

/* the quotient in edx:eax and the remainder in ebx:ecx, the dividend less quotient times divisor. */
.macro DIVIDE_REMAINDER name, divide
    .globl \name
\name:
    pushl 16(%esp)
    pushl 16(%esp)
    pushl 16(%esp)
    pushl 16(%esp)
    calll \divide
    pushl %edx
    pushl %eax
    pushl 24(%esp)
    pushl 24(%esp)
    pushl %edx
    pushl %eax
    calll __allmul
    movl 12(%esp), %ecx
    movl 16(%esp), %ebx
    subl %eax, %ecx
    sbbl %edx, %ebx
    popl %eax
    popl %edx
    retl $16
.endm

DIVIDE_REMAINDER __alldvrm, _MimeDivideSigned64@16
DIVIDE_REMAINDER __aulldvrm, _MimeDivideUnsigned64@16

    .globl __alloca_probe
__alloca_probe:
    jmp __chkstk

/* the size grows until the stack after the allocation is aligned, and saturates rather than wraps. */
.macro ALIGNED_PROBE name, mask
    .globl \name
\name:
    pushl %ecx
    leal 8(%esp), %ecx
    subl %eax, %ecx
    andl $\mask, %ecx
    addl %ecx, %eax
    sbbl %ecx, %ecx
    orl %ecx, %eax
    popl %ecx
    jmp __chkstk
.endm

ALIGNED_PROBE __alloca_probe_16, 15
ALIGNED_PROBE __alloca_probe_8, 7

/* st(0) truncated to a 64-bit integer in edx:eax, and popped. */
    .globl __ftol
    .globl __ftol2
    .globl __ftol2_sse
__ftol:
__ftol2:
__ftol2_sse:
    pushl %ebp
    movl %esp, %ebp
    subl $16, %esp
    fnstcw -2(%ebp)
    movzwl -2(%ebp), %eax
    orl $0x0C00, %eax
    movw %ax, -4(%ebp)
    fldcw -4(%ebp)
    fistpll -12(%ebp)
    fldcw -2(%ebp)
    movl -12(%ebp), %eax
    movl -8(%ebp), %edx
    leave
    retl
