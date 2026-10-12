/*
 * _setjmp and the register half of longjmp for arm64, with msvcrt's jump
 * buffer: Frame, Reserved, X19 to X28, Fp, Lr, Sp, Fpcr and Fpsr as 32
 * bit values, then D8 to D15 from offset 128.
 *
 * _setjmp     x0 = jump buffer, x1 = frame for unwinding or 0
 * MsvcrtLongjmpRestore  x0 = jump buffer, w1 = value, never returns
 * MsvcrtLongjmpLanding  reached by RtlUnwind with x0 = &{buffer, value}
 */
    .text

    .globl  _setjmp
    .globl  _setjmpex
    .p2align 2
_setjmp:
_setjmpex:
    stp     x1, xzr, [x0, #0]
    stp     x19, x20, [x0, #16]
    stp     x21, x22, [x0, #32]
    stp     x23, x24, [x0, #48]
    stp     x25, x26, [x0, #64]
    stp     x27, x28, [x0, #80]
    stp     x29, x30, [x0, #96]
    mov     x2, sp
    str     x2, [x0, #112]
    mrs     x2, fpcr
    str     w2, [x0, #120]
    mrs     x2, fpsr
    str     w2, [x0, #124]
    stp     d8, d9, [x0, #128]
    stp     d10, d11, [x0, #144]
    stp     d12, d13, [x0, #160]
    stp     d14, d15, [x0, #176]
    mov     w0, #0
    ret

    .globl  MsvcrtLongjmpLanding
    .p2align 2
MsvcrtLongjmpLanding:
    ldr     w1, [x0, #8]
    ldr     x0, [x0]

    .globl  MsvcrtLongjmpRestore
MsvcrtLongjmpRestore:
    ldp     x19, x20, [x0, #16]
    ldp     x21, x22, [x0, #32]
    ldp     x23, x24, [x0, #48]
    ldp     x25, x26, [x0, #64]
    ldp     x27, x28, [x0, #80]
    ldp     x29, x30, [x0, #96]
    ldr     x2, [x0, #112]
    mov     sp, x2
    ldr     w2, [x0, #120]
    msr     fpcr, x2
    ldr     w2, [x0, #124]
    msr     fpsr, x2
    ldp     d8, d9, [x0, #128]
    ldp     d10, d11, [x0, #144]
    ldp     d12, d13, [x0, #160]
    ldp     d14, d15, [x0, #176]
    mov     w0, w1
    br      x30
