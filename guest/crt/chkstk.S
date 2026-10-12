/*
 * __chkstk for arm64 windows: x15 holds the frame size in 16 byte units.
 * touches every page the caller is about to allocate so stack growth
 * happens in order. only x16 and x17 may be clobbered.
 */
    .text
    .globl __chkstk
    .p2align 2
__chkstk:
    lsl     x16, x15, #4
    mov     x17, sp
1:
    cmp     x16, #4096
    b.lo    2f
    sub     x17, x17, #4096
    ldr     wzr, [x17]
    sub     x16, x16, #4096
    b       1b
2:
    sub     x17, x17, x16
    ldr     wzr, [x17]
    ret
