/*
 * __chkstk for x64 windows: rax holds the frame size. touches every page
 * the caller is about to allocate so stack growth happens in order. only
 * r10 and r11 may be clobbered.
 */
    .text
    .globl  __chkstk
    .p2align 4
__chkstk:
    movq    %rax, %r10
    leaq    8(%rsp), %r11       /* the caller's stack pointer */
1:
    cmpq    $4096, %r10
    jb      2f
    subq    $4096, %r11
    testb   %al, (%r11)
    subq    $4096, %r10
    jmp     1b
2:
    subq    %r10, %r11
    testb   %al, (%r11)
    ret
