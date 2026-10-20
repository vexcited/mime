/*
 * calls a function with arguments held in slots: the first eight go to the integer registers and to the floating point
 * ones, the rest on the stack. x0 is the function, x1 the slots, x2 their number, x4 where to keep d0, which holds the result
 * of a function that returns a float, or null; the result is in x0.
 */
    .text
    .globl  NdrpInvokeEx
    .p2align 2
    .seh_proc NdrpInvokeEx
NdrpInvokeEx:
    stp     x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    str     x19, [sp, #16]
    .seh_save_reg x19, 16
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    mov     x19, x4
    mov     x9, x0
    mov     x10, x1
    mov     x11, x2
    cmp     x11, #8
    b.ls    2f
    sub     x12, x11, #8
    add     x13, x12, #1
    and     x13, x13, #-2
    lsl     x13, x13, #3
    mov     x14, x13
1:
    cmp     x14, #4096
    b.lo    3f
    sub     sp, sp, #4096
    ldr     wzr, [sp]
    sub     x14, x14, #4096
    b       1b
3:
    sub     sp, sp, x14
    mov     x15, #0
4:
    cmp     x15, x12
    b.hs    2f
    add     x16, x15, #8
    ldr     x17, [x10, x16, lsl #3]
    str     x17, [sp, x15, lsl #3]
    add     x15, x15, #1
    b       4b
2:
    ldp     x0, x1, [x10, #0]
    ldp     x2, x3, [x10, #16]
    ldp     x4, x5, [x10, #32]
    ldp     x6, x7, [x10, #48]
    ldr     d0, [x10, #0]
    ldr     d1, [x10, #8]
    ldr     d2, [x10, #16]
    ldr     d3, [x10, #24]
    ldr     d4, [x10, #32]
    ldr     d5, [x10, #40]
    ldr     d6, [x10, #48]
    ldr     d7, [x10, #56]
    blr     x9
    cbz     x19, 6f
    str     d0, [x19]
6:
    mov     sp, x29
    ldr     x19, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .seh_endproc
