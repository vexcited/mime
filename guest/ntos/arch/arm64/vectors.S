/*
 * kernel entry point and exception vectors.
 *
 * x18 holds the KPCR in kernel mode and the TEB in user mode, matching
 * the windows arm64 abi. exception entry from el0 saves the user x18 in
 * the trap frame and reloads the KPCR from TPIDR_EL1.
 */
#include "ktrap.h"

    .text

/* KiSystemStartup(MimeBootInfo *boot), entered at el1 with the mmu on. */
    .globl  KiSystemStartup
    .p2align 2
KiSystemStartup:
    adrp    x9, KiVectorTable
    add     x9, x9, :lo12:KiVectorTable
    msr     vbar_el1, x9
    adrp    x18, KiBootPcr
    add     x18, x18, :lo12:KiBootPcr
    msr     tpidr_el1, x18
    isb
    mov     x29, xzr
    mov     x30, xzr
    bl      KiInitializeKernel
1:  wfi
    b       1b

/* KiSecondaryStartup(PKPCR pcr), started by the host with the stack set and interrupts masked. */
    .globl  KiSecondaryStartup
    .p2align 2
KiSecondaryStartup:
    adrp    x9, KiVectorTable
    add     x9, x9, :lo12:KiVectorTable
    msr     vbar_el1, x9
    mov     x18, x0
    msr     tpidr_el1, x18
    isb
    mov     x29, xzr
    mov     x30, xzr
    bl      KiInitializeProcessor
1:  wfi
    b       1b

/*
 * the kernel lock. its word is odd while held and every release adds one, so a waiter sleeping on the value it saw
 * wakes on any release since. a free lock goes to whichever processor takes it first, never to one still asleep in
 * the host, whose wakeup would hold up every processor behind it. a waiter spins on loads for a few microseconds,
 * then sleeps in the host, and a releaser that sees sleepers wakes one. the processors interrupted while the lock was
 * held are interrupted once it is let go, see KiSendIpi.
 * clobbers x0 to x2 and x9 to x12.
 */
#define LOCK_OWNER 4
#define LOCK_SLEEPERS 8
#define LOCK_REASON 12
#define LOCK_SINCE 16
#define LOCK_HELD 24
#define LOCK_SPINS 2048
#define HC_WORD_WAIT 0x0D
#define HC_WORD_WAKE 0x0E
#define ICC_SGI1R_EL1 s3_0_c12_c11_5

    .globl  KiAcquireKernelLock
    .p2align 2
KiAcquireKernelLock:
    adrp    x9, KiKernelLock
    add     x9, x9, :lo12:KiKernelLock
    mov     x12, xzr                /* when waiting began, 0 if the lock was free */
    ldr     w11, [x9]
    tbnz    w11, #0, 1f
    add     w10, w11, #1
    mov     w0, w11
    casa    w0, w10, [x9]
    cmp     w0, w11
    b.eq    4f
1:  mrs     x12, cntvct_el0
2:  mov     w10, #LOCK_SPINS
3:  yield
    ldr     w11, [x9]
    tbz     w11, #0, 6f
    subs    w10, w10, #1
    b.ne    3b
    adrp    x1, KiKernelLockPhys
    ldr     x1, [x1, :lo12:KiKernelLockPhys]
    cbz     x1, 2b
    mov     w10, #1
    add     x0, x9, #LOCK_SLEEPERS
    stadd   w10, [x0]
    dmb     ish
    ldr     w2, [x9]
    tbz     w2, #0, 5f
    mov     x0, #HC_WORD_WAIT
    hvc     #0
5:  mov     w10, #-1
    add     x0, x9, #LOCK_SLEEPERS
    stadd   w10, [x0]
    b       2b
6:  add     w10, w11, #1
    mov     w0, w11
    casa    w0, w10, [x9]
    cmp     w0, w11
    b.ne    2b
4:  ldr     w10, [x18, #KPCR_NUMBER]
    str     w10, [x9, #LOCK_OWNER]
    mrs     x11, cntvct_el0
    str     x11, [x9, #LOCK_SINCE]
    /* lock statistics: KiLockStats has eight counters per processor. */
    adrp    x0, KiLockStats
    add     x0, x0, :lo12:KiLockStats
    add     x0, x0, x10, lsl #6
    ldr     x1, [x0]
    add     x1, x1, #1
    str     x1, [x0]
    cbz     x12, 7f
    sub     x11, x11, x12
    ldp     x1, x2, [x0, #8]
    add     x1, x1, #1
    add     x2, x2, x11
    stp     x1, x2, [x0, #8]
    ldr     x1, [x0, #24]
    cmp     x11, x1
    b.ls    7f
    str     x11, [x0, #24]
7:  ret

    .globl  KiReleaseKernelLock
    .p2align 2
KiReleaseKernelLock:
    adrp    x9, KiKernelLock
    add     x9, x9, :lo12:KiKernelLock
    ldr     w10, [x9, #LOCK_OWNER]
    ldr     w11, [x18, #KPCR_NUMBER]
    cmp     w10, w11
    b.eq    1f
    brk     #0x10c
1:  mrs     x10, cntvct_el0
    ldr     x11, [x9, #LOCK_SINCE]
    sub     x10, x10, x11
    ldr     x11, [x9, #LOCK_HELD]
    add     x11, x11, x10
    str     x11, [x9, #LOCK_HELD]
    str     wzr, [x9, #LOCK_REASON]
    mov     w10, #-1
    str     w10, [x9, #LOCK_OWNER]
    ldr     w10, [x9]
    add     w10, w10, #1
    stlr    w10, [x9]
    dmb     ish
    ldr     w10, [x9, #LOCK_SLEEPERS]
    cbz     w10, 2f
    mov     x0, #HC_WORD_WAKE
    adrp    x1, KiKernelLockPhys
    ldr     x1, [x1, :lo12:KiKernelLockPhys]
    hvc     #0
2:  add     x10, x18, #KPCR_DEFERRED_IPIS
    ldr     x11, [x10]
    cbz     x11, 5f
    swp     xzr, x11, [x10]
    dsb     ish
    mov     x12, #(HAL_IPI_INTID << 24)
    /* one target list per cluster of sixteen. */
3:  ands    x10, x11, #0xFFFF
    b.eq    4f
    orr     x10, x10, x12
    msr     ICC_SGI1R_EL1, x10
4:  add     x12, x12, #0x10000
    lsr     x11, x11, #16
    cbnz    x11, 3b
    isb
5:  ret

.macro VENTRY target
    .p2align 7
    b       \target
.endm

    .p2align 11
    .globl  KiVectorTable
KiVectorTable:
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector
    VENTRY  KiKernelSyncVector
    VENTRY  KiKernelIrqVector
    VENTRY  KiInvalidVector
    VENTRY  KiKernelSErrorVector
    VENTRY  KiUserSyncVector
    VENTRY  KiUserIrqVector
    VENTRY  KiInvalidVector
    VENTRY  KiUserSErrorVector
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector
    VENTRY  KiInvalidVector

.macro SAVE_GPRS from_user
    sub     sp, sp, #KTRAP_FRAME_SIZE
    stp     x0, x1, [sp, #0x00]
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    stp     x6, x7, [sp, #0x30]
    stp     x8, x9, [sp, #0x40]
    stp     x10, x11, [sp, #0x50]
    stp     x12, x13, [sp, #0x60]
    stp     x14, x15, [sp, #0x70]
    stp     x16, x17, [sp, #0x80]
    stp     x18, x19, [sp, #0x90]
    stp     x20, x21, [sp, #0xA0]
    stp     x22, x23, [sp, #0xB0]
    stp     x24, x25, [sp, #0xC0]
    stp     x26, x27, [sp, #0xD0]
    stp     x28, x29, [sp, #0xE0]
.if \from_user
    mrs     x9, sp_el0
.else
    add     x9, sp, #KTRAP_FRAME_SIZE
.endif
    stp     x30, x9, [sp, #KTRAP_X30]
    mrs     x10, elr_el1
    mrs     x11, spsr_el1
    stp     x10, x11, [sp, #KTRAP_PC]
    mrs     x12, esr_el1
    mrs     x13, far_el1
    stp     x12, x13, [sp, #KTRAP_ESR]
    str     xzr, [sp, #KTRAP_FLAGS]
.if \from_user
    mrs     x18, tpidr_el1
    /* make user mode stores visible before the kernel sees the processor leave it. */
    dmb     ish
    strb    wzr, [x18, #KPCR_USER_MODE]
    bl      KiAcquireKernelLock
.endif
    mov     x29, xzr
.endm

.macro SAVE_FP
    add     x9, sp, #KTRAP_V
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
    mrs     x10, fpcr
    mrs     x11, fpsr
    add     x9, sp, #KTRAP_FPCR
    stp     w10, w11, [x9]
    mov     x12, #KTRAP_FLAG_FP_SAVED
    str     x12, [sp, #KTRAP_FLAGS]
.endm

    .p2align 2
KiUserSyncVector:
    SAVE_GPRS 1
    mrs     x9, esr_el1
    lsr     x9, x9, #26
    cmp     x9, #ESR_EC_SVC64
    b.ne    1f
    mov     x0, sp
    bl      KiSystemService
    b       KiExceptionExit
1:  SAVE_FP
    mov     x0, sp
    bl      KiUserExceptionHandler
    b       KiExceptionExit

    .p2align 2
KiUserIrqVector:
    SAVE_GPRS 1
    SAVE_FP
    mov     x0, sp
    bl      KiInterruptHandler
    b       KiExceptionExit

    .p2align 2
KiUserSErrorVector:
    SAVE_GPRS 1
    SAVE_FP
    mov     x0, sp
    mov     x1, #1
    bl      KiSErrorHandler
    b       KiExceptionExit

    .p2align 2
KiKernelSyncVector:
    SAVE_GPRS 0
    SAVE_FP
    mov     x0, sp
    bl      KiKernelExceptionHandler
    b       KiExceptionExit

    .p2align 2
KiKernelIrqVector:
    SAVE_GPRS 0
    SAVE_FP
    mov     x0, sp
    bl      KiInterruptHandler
    b       KiExceptionExit

    .p2align 2
KiKernelSErrorVector:
    SAVE_GPRS 0
    SAVE_FP
    mov     x0, sp
    mov     x1, #0
    bl      KiSErrorHandler
    b       KiExceptionExit

    .p2align 2
KiInvalidVector:
    SAVE_GPRS 0
    mov     x0, sp
    bl      KiInvalidVectorHandler
2:  wfi
    b       2b

/* restores the trap frame at sp and returns from the exception. */
    .globl  KiExceptionExit
    .p2align 2
KiExceptionExit:
    msr     daifset, #3
    ldr     x9, [sp, #KTRAP_FLAGS]
    tbz     x9, #0, 1f
    add     x9, sp, #KTRAP_V
    ldp     q0, q1, [x9, #0x000]
    ldp     q2, q3, [x9, #0x020]
    ldp     q4, q5, [x9, #0x040]
    ldp     q6, q7, [x9, #0x060]
    ldp     q8, q9, [x9, #0x080]
    ldp     q10, q11, [x9, #0x0A0]
    ldp     q12, q13, [x9, #0x0C0]
    ldp     q14, q15, [x9, #0x0E0]
    ldp     q16, q17, [x9, #0x100]
    ldp     q18, q19, [x9, #0x120]
    ldp     q20, q21, [x9, #0x140]
    ldp     q22, q23, [x9, #0x160]
    ldp     q24, q25, [x9, #0x180]
    ldp     q26, q27, [x9, #0x1A0]
    ldp     q28, q29, [x9, #0x1C0]
    ldp     q30, q31, [x9, #0x1E0]
    add     x9, sp, #KTRAP_FPCR
    ldp     w10, w11, [x9]
    msr     fpcr, x10
    msr     fpsr, x11
1:  ldp     x10, x11, [sp, #KTRAP_PC]
    msr     elr_el1, x10
    msr     spsr_el1, x11
    and     x12, x11, #0xF
    cbnz    x12, 2f
    ldr     x13, [sp, #KTRAP_SP]
    msr     sp_el0, x13
    /* back to user mode, the next waiting processor may enter the kernel. */
    bl      KiReleaseKernelLock
    mov     w9, #1
    strb    w9, [x18, #KPCR_USER_MODE]
    ldr     x18, [sp, #0x90]
    b       3f
    /* a thread preempted here may resume on another processor, so reload the current pcr. */
2:  mrs     x18, tpidr_el1
3:  ldp     x0, x1, [sp, #0x00]
    ldp     x2, x3, [sp, #0x10]
    ldp     x4, x5, [sp, #0x20]
    ldp     x6, x7, [sp, #0x30]
    ldp     x8, x9, [sp, #0x40]
    ldp     x10, x11, [sp, #0x50]
    ldp     x12, x13, [sp, #0x60]
    ldp     x14, x15, [sp, #0x70]
    ldp     x16, x17, [sp, #0x80]
    ldr     x19, [sp, #0x98]
    ldp     x20, x21, [sp, #0xA0]
    ldp     x22, x23, [sp, #0xB0]
    ldp     x24, x25, [sp, #0xC0]
    ldp     x26, x27, [sp, #0xD0]
    ldp     x28, x29, [sp, #0xE0]
    ldr     x30, [sp, #KTRAP_X30]
    add     sp, sp, #KTRAP_FRAME_SIZE
    eret
    dsb     nsh
    isb
