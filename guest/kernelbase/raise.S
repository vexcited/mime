/*
 * RaiseException captures the caller's state at its own entry, so a handler
 * that continues execution resumes right after the call with every callee
 * saved register intact. KbRaiseExceptionWithContext builds the record.
 */
    .text

    .globl  RaiseException
    .p2align 4
RaiseException:
    sub     sp, sp, #0x3A0          /* CONTEXT at sp, saved lr above it. */
    str     x30, [sp, #0x390]
    stp     x0, x1, [sp, #0x008]
    stp     x2, x3, [sp, #0x018]
    stp     x4, x5, [sp, #0x028]
    stp     x6, x7, [sp, #0x038]
    stp     x8, x9, [sp, #0x048]
    stp     x10, x11, [sp, #0x058]
    stp     x12, x13, [sp, #0x068]
    stp     x14, x15, [sp, #0x078]
    stp     x16, x17, [sp, #0x088]
    stp     x18, x19, [sp, #0x098]
    stp     x20, x21, [sp, #0x0A8]
    stp     x22, x23, [sp, #0x0B8]
    stp     x24, x25, [sp, #0x0C8]
    stp     x26, x27, [sp, #0x0D8]
    stp     x28, x29, [sp, #0x0E8]
    str     x30, [sp, #0x0F8]       /* Lr. */
    add     x9, sp, #0x3A0
    str     x9, [sp, #0x100]        /* Sp as the caller left it. */
    str     x30, [sp, #0x108]       /* Pc: resume after the call. */
    mov     w9, #0x0007             /* CONTEXT_FULL. */
    movk    w9, #0x0040, lsl #16
    str     w9, [sp, #0x000]
    mrs     x9, nzcv
    str     w9, [sp, #0x004]
    mrs     x9, fpcr
    str     w9, [sp, #0x310]
    mrs     x9, fpsr
    str     w9, [sp, #0x314]
    add     x9, sp, #0x110
    stp     q0, q1, [x9, #0x000]
    stp     q2, q3, [x9, #0x020]
    stp     q4, q5, [x9, #0x040]
    stp     q6, q7, [x9, #0x060]
    stp     q8, q9, [x9, #0x080]
    stp     q10, q11, [x9, #0x0A0]
    stp     q12, q13, [x9, #0x0C0]
    stp     q14, q15, [x9, #0x0E0]
    stp     q16, q17, [x9, #0x100]
    stp     q18, q19, [x9, #0x120]
    stp     q20, q21, [x9, #0x140]
    stp     q22, q23, [x9, #0x160]
    stp     q24, q25, [x9, #0x180]
    stp     q26, q27, [x9, #0x1A0]
    stp     q28, q29, [x9, #0x1C0]
    stp     q30, q31, [x9, #0x1E0]
    mov     x4, sp
    bl      KbRaiseExceptionWithContext
    /* only reached when the raise itself failed. */
    ldr     x30, [sp, #0x390]
    add     sp, sp, #0x3A0
    ret

    .section .drectve, "yn"
    .ascii  " -export:RaiseException"
