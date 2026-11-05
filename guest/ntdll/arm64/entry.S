/*
 * user mode entry points called by the kernel.
 *
 * KiUserExceptionDispatcher  x0 = &EXCEPTION_RECORD, x1 = &CONTEXT, sp = context
 * KiUserApcDispatcher        x0 = context, x1 = arg1, x2 = arg2, x3 = routine, x4 = &CONTEXT
 * KiUserCallbackDispatcher   x0 = api index, x1 = arguments, x2 = argument length
 * LdrInitializeThunk         x0 = &CONTEXT of the new thread, x1 = ntdll base
 */
    .text

/*
 * the kernel leaves the interrupted code's CONTEXT at the stack pointer, with the EXCEPTION_RECORD above it.
 * the unwind data says so, as on Windows: an unwind through the dispatcher restores every register from that
 * context and resumes at the faulting pc.
 */
    .globl  KiUserExceptionDispatcher
    .p2align 4
    .seh_proc KiUserExceptionDispatcher
KiUserExceptionDispatcher:
    .seh_context
    .seh_endprologue
    mov     x19, x0
    mov     x20, x1
    bl      KiUserExceptionDispatcherC
    /* not reached unless the c dispatcher returns: raise as second chance. */
    mov     x0, x19
    mov     x1, x20
    mov     w2, #0
    bl      NtRaiseException
    b       .
    .seh_endproc

/* the same context sits at the stack pointer of an apc, below the interrupted code's. */
    .globl  KiUserApcDispatcher
    .p2align 4
    .seh_proc KiUserApcDispatcher
KiUserApcDispatcher:
    .seh_context
    .seh_endprologue
    mov     x19, x4
    blr     x3
    mov     x0, x19
    mov     w1, #1
    bl      NtContinue
    b       .
    .seh_endproc

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

/*
 * RtlpResumeContext continues at the context in x0, loading the parts its flags name. x16 and x17 are
 * scratch for the jump, and x18 stays the thread's. a context without a control part has nowhere to
 * continue, so it returns after loading what it holds.
 */
    .globl  RtlpResumeContext
    .p2align 4
RtlpResumeContext:
    mov     x16, x0
    ldr     w17, [x16]              /* ContextFlags */
    tbz     w17, #2, 1f             /* CONTEXT_FLOATING_POINT */
    add     x17, x16, #0x110
    ldp     q0, q1, [x17, #0x000]
    ldp     q2, q3, [x17, #0x020]
    ldp     q4, q5, [x17, #0x040]
    ldp     q6, q7, [x17, #0x060]
    ldp     q8, q9, [x17, #0x080]
    ldp     q10, q11, [x17, #0x0A0]
    ldp     q12, q13, [x17, #0x0C0]
    ldp     q14, q15, [x17, #0x0E0]
    ldp     q16, q17, [x17, #0x100]
    ldp     q18, q19, [x17, #0x120]
    ldp     q20, q21, [x17, #0x140]
    ldp     q22, q23, [x17, #0x160]
    ldp     q24, q25, [x17, #0x180]
    ldp     q26, q27, [x17, #0x1A0]
    ldp     q28, q29, [x17, #0x1C0]
    ldp     q30, q31, [x17, #0x1E0]
    ldr     w17, [x16, #0x310]
    msr     fpcr, x17
    ldr     w17, [x16, #0x314]
    msr     fpsr, x17
    ldr     w17, [x16]
1:  tbz     w17, #1, 2f             /* CONTEXT_INTEGER */
    ldp     x0, x1, [x16, #0x008]
    ldp     x2, x3, [x16, #0x018]
    ldp     x4, x5, [x16, #0x028]
    ldp     x6, x7, [x16, #0x038]
    ldp     x8, x9, [x16, #0x048]
    ldp     x10, x11, [x16, #0x058]
    ldp     x12, x13, [x16, #0x068]
    ldp     x14, x15, [x16, #0x078]
    ldp     x19, x20, [x16, #0x0A0]
    ldp     x21, x22, [x16, #0x0B0]
    ldp     x23, x24, [x16, #0x0C0]
    ldp     x25, x26, [x16, #0x0D0]
    ldp     x27, x28, [x16, #0x0E0]
2:  ldr     w17, [x16]
    tbz     w17, #0, 3f             /* CONTEXT_CONTROL */
    ldp     x29, x30, [x16, #0x0F0]
    ldr     w17, [x16, #0x004]      /* Cpsr: only the condition flags belong to the program */
    and     x17, x17, #0xF0000000
    msr     nzcv, x17
    /* nothing is read from the context once the stack pointer moves, since it may lie below it. */
    ldr     x17, [x16, #0x108]      /* Pc */
    ldr     x16, [x16, #0x100]      /* Sp */
    mov     sp, x16
    br      x17
3:  ret

/*
 * RtlpCallConsolidate: x0 = context, x1 = callback, x2 = exception record.
 *
 * runs the STATUS_UNWIND_CONSOLIDATE callback, the catch block of a c++ exception, with the target frame's
 * nonvolatile registers, and returns where it says to continue. a copy of the target's context at the stack
 * pointer makes an unwind from inside the callback continue straight at the target frame, as on Windows.
 */
    .globl  RtlpCallConsolidate
    .p2align 4
    .seh_proc RtlpCallConsolidate
RtlpCallConsolidate:
    .seh_context
    .seh_endprologue
    sub     sp, sp, #0x430
    add     x12, sp, #0x390
    stp     x29, x30, [x12, #0x00]
    stp     x19, x20, [x12, #0x10]
    stp     x21, x22, [x12, #0x20]
    stp     x23, x24, [x12, #0x30]
    stp     x25, x26, [x12, #0x40]
    stp     x27, x28, [x12, #0x50]
    stp     d8, d9, [x12, #0x60]
    stp     d10, d11, [x12, #0x70]
    stp     d12, d13, [x12, #0x80]
    stp     d14, d15, [x12, #0x90]
    /* only the first 0x310 bytes of the context matter to an unwind. */
    mov     x9, x0
    mov     x10, sp
    mov     w11, #24
4:  ldp     q16, q17, [x9], #32
    stp     q16, q17, [x10], #32
    subs    w11, w11, #1
    b.ne    4b
    ldr     q16, [x9]
    str     q16, [x10]
    ldp     x19, x20, [x0, #0x0A0]
    ldp     x21, x22, [x0, #0x0B0]
    ldp     x23, x24, [x0, #0x0C0]
    ldp     x25, x26, [x0, #0x0D0]
    ldp     x27, x28, [x0, #0x0E0]
    ldr     x29, [x0, #0x0F0]
    ldr     d8, [x0, #0x190]
    ldr     d9, [x0, #0x1A0]
    ldr     d10, [x0, #0x1B0]
    ldr     d11, [x0, #0x1C0]
    ldr     d12, [x0, #0x1D0]
    ldr     d13, [x0, #0x1E0]
    ldr     d14, [x0, #0x1F0]
    ldr     d15, [x0, #0x200]
    mov     x0, x2
    blr     x1
    add     x12, sp, #0x390
    ldp     x29, x30, [x12, #0x00]
    ldp     x19, x20, [x12, #0x10]
    ldp     x21, x22, [x12, #0x20]
    ldp     x23, x24, [x12, #0x30]
    ldp     x25, x26, [x12, #0x40]
    ldp     x27, x28, [x12, #0x50]
    ldp     d8, d9, [x12, #0x60]
    ldp     d10, d11, [x12, #0x70]
    ldp     d12, d13, [x12, #0x80]
    ldp     d14, d15, [x12, #0x90]
    add     sp, sp, #0x430
    ret
    .seh_endproc

/*
 * EXCEPTION_DISPOSITION RtlpExecuteHandlerForUnwind(record, frame, context, dispatcher, handler)
 *
 * calls a handler of the frame that an unwind is on. an exception raised inside the handler meets this frame and gets
 * RtlpUnwindHandler, which tells it to go on from the frame under unwind, as the frames between are unwound already.
 * the dispatcher context is kept 16 bytes below the frame's establisher, the stack pointer its caller gets back.
 */
    .globl  RtlpExecuteHandlerForUnwind
    .p2align 4
    .seh_proc RtlpExecuteHandlerForUnwind
RtlpExecuteHandlerForUnwind:
    stp     x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    .seh_handler RtlpUnwindHandler, @unwind, @except
    .seh_endprologue
    str     x3, [sp, #16]
    blr     x4
    nop                             /* a return address before the epilog would unwind as one. */
    .seh_startepilogue
    ldp     x29, x30, [sp], #32
    .seh_save_fplr_x 32
    .seh_endepilogue
    ret
    .seh_endproc
