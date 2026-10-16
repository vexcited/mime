/*
 * the interlocked functions x86's kernel32 exports, which 64-bit code has
 * as compiler intrinsics only. stdcall, the target first.
 */
    .text

    .globl  _InterlockedIncrement@4
_InterlockedIncrement@4:
    movl    4(%esp), %ecx
    movl    $1, %eax
    lock xaddl %eax, (%ecx)
    incl    %eax
    retl    $4

    .globl  _InterlockedDecrement@4
_InterlockedDecrement@4:
    movl    4(%esp), %ecx
    movl    $-1, %eax
    lock xaddl %eax, (%ecx)
    decl    %eax
    retl    $4

    .globl  _InterlockedExchange@8
_InterlockedExchange@8:
    movl    4(%esp), %ecx
    movl    8(%esp), %eax
    xchgl   %eax, (%ecx)
    retl    $8

    .globl  _InterlockedExchangeAdd@8
_InterlockedExchangeAdd@8:
    movl    4(%esp), %ecx
    movl    8(%esp), %eax
    lock xaddl %eax, (%ecx)
    retl    $8

    .globl  _InterlockedCompareExchange@12
_InterlockedCompareExchange@12:
    movl    4(%esp), %ecx
    movl    8(%esp), %edx
    movl    12(%esp), %eax
    lock cmpxchgl %edx, (%ecx)
    retl    $12

    .globl  _InterlockedCompareExchange64@20
_InterlockedCompareExchange64@20:
    pushl   %ebx
    pushl   %esi
    movl    12(%esp), %esi
    movl    16(%esp), %ebx
    movl    20(%esp), %ecx
    movl    24(%esp), %eax
    movl    28(%esp), %edx
    lock cmpxchg8b (%esi)
    popl    %esi
    popl    %ebx
    retl    $20
