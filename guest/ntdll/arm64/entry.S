/*
 * user mode entry points called by the kernel.
 *
 * KiUserExceptionDispatcher  x0 = &EXCEPTION_RECORD, x1 = &CONTEXT, sp = context
 * KiUserApcDispatcher        x0 = context, x1 = arg1, x2 = arg2, x3 = routine, x4 = &CONTEXT
 * KiUserCallbackDispatcher   x0 = api index, x1 = arguments, x2 = argument length
 * LdrInitializeThunk         x0 = &CONTEXT of the new thread, x1 = ntdll base
 */
    .text

    .globl  KiUserExceptionDispatcher
    .p2align 4
KiUserExceptionDispatcher:
    mov     x19, x0
    mov     x20, x1
    bl      KiUserExceptionDispatcherC
    /* not reached unless the c dispatcher returns: raise as second chance. */
    mov     x0, x19
    mov     x1, x20
    mov     w2, #0
    bl      NtRaiseException
    b       .

    .globl  KiUserApcDispatcher
    .p2align 4
KiUserApcDispatcher:
    mov     x19, x4
    blr     x3
    mov     x0, x19
    mov     w1, #1
    bl      NtContinue
    b       .

    .globl  KiUserCallbackDispatcher
    .p2align 4
KiUserCallbackDispatcher:
    /* PEB->KernelCallbackTable[api](arguments, length), then NtCallbackReturn. */
    ldr     x9, [x18, #0x60]        /* TEB->ProcessEnvironmentBlock */
    ldr     x9, [x9, #0x58]         /* PEB->KernelCallbackTable */
    ldr     x9, [x9, w0, uxtw #3]
    mov     x0, x1
    mov     w1, w2
    blr     x9
    mov     w2, w0                  /* status */
    mov     x0, xzr
    mov     w1, #0
    bl      NtCallbackReturn
    b       .

    .globl  LdrInitializeThunk
    .p2align 4
LdrInitializeThunk:
    mov     x19, x0
    bl      LdrpInitialize
    mov     x0, x19
    mov     w1, #1
    bl      NtContinue
    b       .

/* void RtlCaptureContext(PCONTEXT context): the caller's state at the return address. */
    .globl  RtlCaptureContext
    .p2align 4
RtlCaptureContext:
    str     x2, [sp, #-16]!         /* x2 is captured too, so borrow it on the stack */
    mov     w2, #0x0007             /* CONTEXT_FULL: arm64 | control | integer | floating point */
    movk    w2, #0x0040, lsl #16
    str     w2, [x0, #0x000]        /* ContextFlags */
    ldr     x2, [sp], #16
    stp     x0, x1, [x0, #0x008]
    stp     x2, x3, [x0, #0x018]
    stp     x4, x5, [x0, #0x028]
    stp     x6, x7, [x0, #0x038]
    stp     x8, x9, [x0, #0x048]
    stp     x10, x11, [x0, #0x058]
    stp     x12, x13, [x0, #0x068]
    stp     x14, x15, [x0, #0x078]
    stp     x16, x17, [x0, #0x088]
    stp     x18, x19, [x0, #0x098]
    stp     x20, x21, [x0, #0x0A8]
    stp     x22, x23, [x0, #0x0B8]
    stp     x24, x25, [x0, #0x0C8]
    stp     x26, x27, [x0, #0x0D8]
    stp     x28, x29, [x0, #0x0E8]
    str     x30, [x0, #0x0F8]       /* Lr */
    mov     x1, sp
    str     x1, [x0, #0x100]        /* Sp */
    str     x30, [x0, #0x108]       /* Pc */
    mrs     x1, nzcv
    str     w1, [x0, #0x004]        /* Cpsr */
    mrs     x1, fpcr
    str     w1, [x0, #0x310]
    mrs     x1, fpsr
    str     w1, [x0, #0x314]
    add     x1, x0, #0x110
    stp     q0, q1, [x1, #0x000]
    stp     q2, q3, [x1, #0x020]
    stp     q4, q5, [x1, #0x040]
    stp     q6, q7, [x1, #0x060]
    stp     q8, q9, [x1, #0x080]
    stp     q10, q11, [x1, #0x0A0]
    stp     q12, q13, [x1, #0x0C0]
    stp     q14, q15, [x1, #0x0E0]
    stp     q16, q17, [x1, #0x100]
    stp     q18, q19, [x1, #0x120]
    stp     q20, q21, [x1, #0x140]
    stp     q22, q23, [x1, #0x160]
    stp     q24, q25, [x1, #0x180]
    stp     q26, q27, [x1, #0x1A0]
    stp     q28, q29, [x1, #0x1C0]
    stp     q30, q31, [x1, #0x1E0]
    ldr     x1, [x0, #0x010]        /* restore x1 */
    ret
