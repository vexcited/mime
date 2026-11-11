/*
 * the exports of mscoree.dll that only forward to the runtime. each loads its index into w16 and comes here, where
 * the function of the runtime is found and jumped to with the registers and the stack as the caller made them.
 * without a runtime the call fails with an error and returns.
 */
    .text
    .globl  mscoree_thunk_common
    .p2align 2
    .seh_proc mscoree_thunk_common
mscoree_thunk_common:
    stp     x29, x30, [sp, #-224]!
    .seh_save_fplr_x 224
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    stp     x0, x1, [sp, #16]
    stp     x2, x3, [sp, #32]
    stp     x4, x5, [sp, #48]
    stp     x6, x7, [sp, #64]
    str     x8, [sp, #80]           /* where a function returns a structure. */
    stp     q0, q1, [sp, #96]
    stp     q2, q3, [sp, #128]
    stp     q4, q5, [sp, #160]
    stp     q6, q7, [sp, #192]
    mov     w0, w16
    bl      mscoree_resolve
    mov     x16, x0
    ldp     x0, x1, [sp, #16]
    ldp     x2, x3, [sp, #32]
    ldp     x4, x5, [sp, #48]
    ldp     x6, x7, [sp, #64]
    ldr     x8, [sp, #80]
    ldp     q0, q1, [sp, #96]
    ldp     q2, q3, [sp, #128]
    ldp     q4, q5, [sp, #160]
    ldp     q6, q7, [sp, #192]
    .seh_startepilogue
    ldp     x29, x30, [sp], #224
    .seh_save_fplr_x 224
    .seh_endepilogue
    cbz     x16, 1f
    br      x16
1:
    mov     w0, #0x1701
    movk    w0, #0x8013, lsl #16
    ret
    .seh_endproc
