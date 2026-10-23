/*
 * thread switching, kernel to user transitions and nested user mode
 * callbacks (KeUserModeCallback).
 */
#include "ktrap.h"

    .text

.macro PUSH_CALLEE size
    sub     sp, sp, #\size
    stp     x19, x20, [sp, #0x00]
    stp     x21, x22, [sp, #0x10]
    stp     x23, x24, [sp, #0x20]
    stp     x25, x26, [sp, #0x30]
    stp     x27, x28, [sp, #0x40]
    stp     x29, x30, [sp, #0x50]
    stp     d8, d9, [sp, #0x60]
    stp     d10, d11, [sp, #0x70]
    stp     d12, d13, [sp, #0x80]
    stp     d14, d15, [sp, #0x90]
.endm

.macro POP_CALLEE size
    ldp     x19, x20, [sp, #0x00]
    ldp     x21, x22, [sp, #0x10]
    ldp     x23, x24, [sp, #0x20]
    ldp     x25, x26, [sp, #0x30]
    ldp     x27, x28, [sp, #0x40]
    ldp     x29, x30, [sp, #0x50]
    ldp     d8, d9, [sp, #0x60]
    ldp     d10, d11, [sp, #0x70]
    ldp     d12, d13, [sp, #0x80]
    ldp     d14, d15, [sp, #0x90]
    add     sp, sp, #\size
.endm

/* void KiSwapContextInternal(ULONG64 *old_sp, ULONG64 new_sp). */
    .globl  KiSwapContextInternal
    .p2align 2
KiSwapContextInternal:
    PUSH_CALLEE KSWITCH_FRAME_SIZE
    /* a thread blocked in a system call keeps its own rounding mode. */
    mrs     x9, fpcr
    str     x9, [sp, #KSWITCH_FPCR]
    mov     x9, sp
    str     x9, [x0]
    mov     sp, x1
    ldr     x9, [sp, #KSWITCH_FPCR]
    msr     fpcr, x9
    POP_CALLEE KSWITCH_FRAME_SIZE
    ret

/* first code run by a new thread: x19 = start routine, x20 = context. */
    .globl  KiThreadStartup
    .p2align 2
KiThreadStartup:
    mov     x0, x19
    mov     x1, x20
    mov     x29, xzr
    bl      KiThreadStartupC
1:  wfi
    b       1b

/* void KiEnterUserMode(PKTRAP_FRAME frame): frame becomes the stack top. */
    .globl  KiEnterUserMode
    .p2align 2
KiEnterUserMode:
    mov     sp, x0
    b       KiExceptionExit

/*
 * NTSTATUS KiCallUserMode(ULONG64 *callback_sp_slot, PKTRAP_FRAME frame)
 * saves the callee saved state, records the stack pointer that
 * NtCallbackReturn resumes from, copies the user frame below it and
 * returns to user mode through it.
 */
    .globl  KiCallUserMode
    .p2align 2
KiCallUserMode:
    PUSH_CALLEE KCALLOUT_FRAME_SIZE
    ldr     x9, [x0]
    str     x9, [sp, #KCALLOUT_PREVIOUS_SP]
    mov     x9, sp
    str     x9, [x0]
    sub     sp, sp, #KTRAP_FRAME_SIZE
    mov     x10, sp
    mov     x11, #KTRAP_FRAME_SIZE
1:  ldp     x12, x13, [x1], #16
    stp     x12, x13, [x10], #16
    subs    x11, x11, #16
    b.ne    1b
    b       KiExceptionExit

/* void KiCallbackReturnInternal(ULONG64 callback_sp, NTSTATUS status). */
    .globl  KiCallbackReturnInternal
    .p2align 2
KiCallbackReturnInternal:
    mov     sp, x0
    mov     x0, x1
    POP_CALLEE KCALLOUT_FRAME_SIZE
    ret

/* int KiSetJump(KJUMP_BUFFER *buf) and void KiLongJump(KJUMP_BUFFER *buf, int value). */
    .globl  KiSetJump
    .p2align 2
KiSetJump:
    stp     x19, x20, [x0, #0x00]
    stp     x21, x22, [x0, #0x10]
    stp     x23, x24, [x0, #0x20]
    stp     x25, x26, [x0, #0x30]
    stp     x27, x28, [x0, #0x40]
    stp     x29, x30, [x0, #0x50]
    mov     x9, sp
    str     x9, [x0, #0x60]
    stp     d8, d9, [x0, #0x68]
    stp     d10, d11, [x0, #0x78]
    stp     d12, d13, [x0, #0x88]
    stp     d14, d15, [x0, #0x98]
    mov     w0, #0
    ret

    .globl  KiLongJump
    .p2align 2
KiLongJump:
    ldp     x19, x20, [x0, #0x00]
    ldp     x21, x22, [x0, #0x10]
    ldp     x23, x24, [x0, #0x20]
    ldp     x25, x26, [x0, #0x30]
    ldp     x27, x28, [x0, #0x40]
    ldp     x29, x30, [x0, #0x50]
    ldr     x9, [x0, #0x60]
    mov     sp, x9
    ldp     d8, d9, [x0, #0x68]
    ldp     d10, d11, [x0, #0x78]
    ldp     d12, d13, [x0, #0x88]
    ldp     d14, d15, [x0, #0x98]
    cmp     w1, #0
    csinc   w0, w1, wzr, ne
    ret

/* NTSTATUS KiCallService(PVOID routine, ULONG64 *args, ULONG stack_args): up to 16 arguments from an array. */
    .globl  KiCallService
    .p2align 2
KiCallService:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x9, x0
    mov     x10, x1
    lsl     x11, x2, #3
    add     x11, x11, #15
    and     x11, x11, #~15
    sub     sp, sp, x11
    mov     x12, xzr
1:  cmp     x12, x2
    b.hs    2f
    add     x13, x10, #64
    ldr     x14, [x13, x12, lsl #3]
    str     x14, [sp, x12, lsl #3]
    add     x12, x12, #1
    b       1b
2:  ldp     x0, x1, [x10, #0x00]
    ldp     x2, x3, [x10, #0x10]
    ldp     x4, x5, [x10, #0x20]
    ldp     x6, x7, [x10, #0x30]
    blr     x9
    mov     sp, x29
    ldp     x29, x30, [sp], #16
    ret
