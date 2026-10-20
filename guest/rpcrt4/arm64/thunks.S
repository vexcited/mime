/*
 * the thunks of proxies, as in the x64 file. The eight integer registers are saved right below the arguments the caller
 * put on the stack, so that the arguments are one frame, and the eight floating point ones next to them.
 */
    .text
    .p2align 4
    .globl  NdrpStublessThunks
NdrpStublessThunks:
    .set    i, 0
    .rept   1024
    mov     w9, #i
    b       NdrpStublessCommon
    .p2align 4
    .set    i, i + 1
    .endr

    .p2align 2
    .seh_proc NdrpStublessCommon
NdrpStublessCommon:
    sub     sp, sp, #144
    .seh_stackalloc 144
    stp     x29, x30, [sp]
    .seh_save_fplr 0
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    stp     x0, x1, [sp, #80]
    stp     x2, x3, [sp, #96]
    stp     x4, x5, [sp, #112]
    stp     x6, x7, [sp, #128]
    stp     d0, d1, [sp, #16]
    stp     d2, d3, [sp, #32]
    stp     d4, d5, [sp, #48]
    stp     d6, d7, [sp, #64]
    mov     w0, w9
    add     x1, sp, #80
    add     x2, sp, #16
    mov     x3, xzr
    bl      NdrpStublessClient
    ldp     x29, x30, [sp]
    add     sp, sp, #144
    ret
    .seh_endproc

    .p2align 5
    .globl  NdrpDelegateThunks
NdrpDelegateThunks:
    .set    i, 0
    .rept   128
    ldr     x0, [x0, #8]
    ldr     x9, [x0]
    ldr     x9, [x9, #(i * 8)]
    br      x9
    .p2align 5
    .set    i, i + 1
    .endr
