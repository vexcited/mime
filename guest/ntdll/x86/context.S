    .text
    .globl _RtlCaptureContext@4
_RtlCaptureContext@4:
    pushfl
    pushal
    movl 40(%esp), %eax
    movl $0x10007, 0(%eax)
    movl 0(%esp), %edx
    movl %edx, 156(%eax)
    movl 4(%esp), %edx
    movl %edx, 160(%eax)
    movl 16(%esp), %edx
    movl %edx, 164(%eax)
    movl 20(%esp), %edx
    movl %edx, 168(%eax)
    movl 24(%esp), %edx
    movl %edx, 172(%eax)
    movl 28(%esp), %edx
    movl %edx, 176(%eax)
    movl 8(%esp), %edx
    movl %edx, 180(%eax)
    movl 36(%esp), %edx
    movl %edx, 184(%eax)
    movl 32(%esp), %edx
    movl %edx, 192(%eax)
    leal 44(%esp), %edx
    movl %edx, 196(%eax)
    movl $0x23, 188(%eax)
    movl $0x2b, 200(%eax)
    movl $0x2b, 152(%eax)
    movl $0x2b, 148(%eax)
    movl $0x53, 144(%eax)
    movl $0, 140(%eax)
    popal
    popfl
    retl $4

    .globl _RtlRaiseException@4
_RtlRaiseException@4:
    subl $716, %esp
    pushl %esp
    calll _RtlCaptureContext@4
    movl 716(%esp), %eax
    movl %eax, 184(%esp)
    leal 724(%esp), %edx
    movl %edx, 196(%esp)
    movl 720(%esp), %edx
    movl %eax, 12(%edx)
    movl %esp, %eax
    pushl $1
    pushl %eax
    pushl %edx
    calll _NtRaiseException@12
    addl $716, %esp
    retl $4
