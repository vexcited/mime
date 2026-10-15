/*
 * kernel entry points, interrupt and system call entry, and the exit to
 * user mode.
 *
 * gs holds the KPCR in kernel mode and the TEB in user mode, swapped on
 * every crossing. an entry from user mode clears KPCR.UserMode before it
 * takes the kernel lock, and the exit sets it before releasing the lock,
 * so a processor holding the lock sees exactly which others run user code.
 */
#include "ktrap.h"

    .text

/* KiSystemStartup(MimeBootInfo *boot): rcx = boot information, on the boot stack, interrupts off. */
    .globl  KiSystemStartup
    .p2align 4
KiSystemStartup:
    leaq    KiBootPcr(%rip), %rax
    movq    %rax, KPCR_SELF(%rax)
    movq    %rcx, %rbx
    movq    %rax, %rdx
    shrq    $32, %rdx
    movl    $0xC0000101, %ecx       /* MSR_GS_BASE */
    wrmsr
    movq    %rbx, %rcx
    xorl    %ebp, %ebp
    andq    $-16, %rsp
    subq    $0x20, %rsp
    call    KiInitializeKernel
1:  hlt
    jmp     1b

/* KiSecondaryStartup(PKPCR pcr): rcx = pcr, started by the host with the stack set and interrupts off. */
    .globl  KiSecondaryStartup
    .p2align 4
KiSecondaryStartup:
    movq    %rcx, %rbx
    movq    %rcx, %rax
    movq    %rcx, %rdx
    shrq    $32, %rdx
    movl    $0xC0000101, %ecx       /* MSR_GS_BASE */
    wrmsr
    movq    %rbx, %rcx
    xorl    %ebp, %ebp
    andq    $-16, %rsp
    subq    $0x20, %rsp
    call    KiInitializeProcessor
1:  hlt
    jmp     1b

/*
 * the kernel lock, a ticket lock: the low half serves, the high half hands
 * out tickets. a waiter spins for a few microseconds, then sleeps in the
 * host on the lock word: a spinning vcpu would otherwise take the host
 * core the owner's vcpu needs. a releaser that sees sleepers wakes them.
 * rax, rcx, rdx and r8 to r11 change, as in any call.
 */
#define LOCK_OWNER 4
#define LOCK_SLEEPERS 8
#define LOCK_SPINS 2048
#define HC_LOCK_WAIT 0x0A
#define HC_LOCK_WAKE 0x0B

    .globl  KiAcquireKernelLock
    .p2align 4
KiAcquireKernelLock:
    leaq    KiKernelLock(%rip), %r8
    movl    $0x10000, %eax
    lock xaddl %eax, (%r8)
    movl    %eax, %r9d
    shrl    $16, %r9d               /* our ticket */
    movzwl  %ax, %eax
    cmpl    %r9d, %eax
    je      4f
1:  movl    $LOCK_SPINS, %r10d
2:  pause
    movzwl  (%r8), %eax
    cmpl    %r9d, %eax
    je      4f
    decl    %r10d
    jnz     2b
    cmpq    $0, KiKernelLockPhys(%rip)
    je      1b
    lock incl LOCK_SLEEPERS(%r8)
    movzwl  (%r8), %eax
    cmpl    %r9d, %eax
    je      5f
    pushq   %rdi
    pushq   %rsi
    movl    $HC_LOCK_WAIT, %eax
    movq    KiKernelLockPhys(%rip), %rdi
    movl    %r9d, %esi
    outb    %al, $MIME_HC_PORT
    popq    %rsi
    popq    %rdi
    leaq    KiKernelLock(%rip), %r8
5:  lock decl LOCK_SLEEPERS(%r8)
    movzwl  (%r8), %eax
    cmpl    %r9d, %eax
    jne     1b
4:  movl    %gs:KPCR_NUMBER, %eax
    movl    %eax, LOCK_OWNER(%r8)
    /* kernel translations another processor changed while this one was away. */
    testb   $KPCR_FLUSH_GLOBAL, %gs:KPCR_FLUSH_PENDING
    jz      6f
    movb    $0, %gs:KPCR_FLUSH_PENDING
    movq    %cr4, %rax
    movq    %rax, %rcx
    andq    $~0x80, %rcx
    movq    %rcx, %cr4
    movq    %rax, %cr4
6:  ret

    .globl  KiReleaseKernelLock
    .p2align 4
KiReleaseKernelLock:
    leaq    KiKernelLock(%rip), %r8
    movl    LOCK_OWNER(%r8), %eax
    cmpl    %gs:KPCR_NUMBER, %eax
    je      1f
    ud2
1:  movl    $-1, LOCK_OWNER(%r8)
    lock incw (%r8)
    cmpl    $0, LOCK_SLEEPERS(%r8)
    je      2f
    pushq   %rdi
    movl    $HC_LOCK_WAKE, %eax
    movq    KiKernelLockPhys(%rip), %rdi
    outb    %al, $MIME_HC_PORT
    popq    %rdi
2:  ret

.macro SAVE_GPRS
    movq    %rax, KTRAP_RAX(%rsp)
    movq    %rcx, KTRAP_RCX(%rsp)
    movq    %rdx, KTRAP_RDX(%rsp)
    movq    %rbx, KTRAP_RBX(%rsp)
    movq    %rbp, KTRAP_RBP(%rsp)
    movq    %rsi, KTRAP_RSI(%rsp)
    movq    %rdi, KTRAP_RDI(%rsp)
    movq    %r8, KTRAP_R8(%rsp)
    movq    %r9, KTRAP_R9(%rsp)
    movq    %r10, KTRAP_R10(%rsp)
    movq    %r11, KTRAP_R11(%rsp)
    movq    %r12, KTRAP_R12(%rsp)
    movq    %r13, KTRAP_R13(%rsp)
    movq    %r14, KTRAP_R14(%rsp)
    movq    %r15, KTRAP_R15(%rsp)
.endm

.macro RESTORE_GPRS
    movq    KTRAP_RAX(%rsp), %rax
    movq    KTRAP_RCX(%rsp), %rcx
    movq    KTRAP_RDX(%rsp), %rdx
    movq    KTRAP_RBX(%rsp), %rbx
    movq    KTRAP_RBP(%rsp), %rbp
    movq    KTRAP_RSI(%rsp), %rsi
    movq    KTRAP_RDI(%rsp), %rdi
    movq    KTRAP_R8(%rsp), %r8
    movq    KTRAP_R9(%rsp), %r9
    movq    KTRAP_R10(%rsp), %r10
    movq    KTRAP_R11(%rsp), %r11
    movq    KTRAP_R12(%rsp), %r12
    movq    KTRAP_R13(%rsp), %r13
    movq    KTRAP_R14(%rsp), %r14
    movq    KTRAP_R15(%rsp), %r15
.endm

.macro SAVE_XMM
    movaps  %xmm0, KTRAP_XMM0 + 0x00(%rsp)
    movaps  %xmm1, KTRAP_XMM0 + 0x10(%rsp)
    movaps  %xmm2, KTRAP_XMM0 + 0x20(%rsp)
    movaps  %xmm3, KTRAP_XMM0 + 0x30(%rsp)
    movaps  %xmm4, KTRAP_XMM0 + 0x40(%rsp)
    movaps  %xmm5, KTRAP_XMM0 + 0x50(%rsp)
    movaps  %xmm6, KTRAP_XMM0 + 0x60(%rsp)
    movaps  %xmm7, KTRAP_XMM0 + 0x70(%rsp)
    movaps  %xmm8, KTRAP_XMM0 + 0x80(%rsp)
    movaps  %xmm9, KTRAP_XMM0 + 0x90(%rsp)
    movaps  %xmm10, KTRAP_XMM0 + 0xA0(%rsp)
    movaps  %xmm11, KTRAP_XMM0 + 0xB0(%rsp)
    movaps  %xmm12, KTRAP_XMM0 + 0xC0(%rsp)
    movaps  %xmm13, KTRAP_XMM0 + 0xD0(%rsp)
    movaps  %xmm14, KTRAP_XMM0 + 0xE0(%rsp)
    movaps  %xmm15, KTRAP_XMM0 + 0xF0(%rsp)
    stmxcsr KTRAP_MXCSR(%rsp)
    ldmxcsr KiKernelMxcsr(%rip)
    movl    $KTRAP_FLAG_FP_SAVED, KTRAP_FLAGS(%rsp)
.endm

.macro RESTORE_XMM
    testl   $KTRAP_FLAG_FP_SAVED, KTRAP_FLAGS(%rsp)
    jz      9f
    movaps  KTRAP_XMM0 + 0x00(%rsp), %xmm0
    movaps  KTRAP_XMM0 + 0x10(%rsp), %xmm1
    movaps  KTRAP_XMM0 + 0x20(%rsp), %xmm2
    movaps  KTRAP_XMM0 + 0x30(%rsp), %xmm3
    movaps  KTRAP_XMM0 + 0x40(%rsp), %xmm4
    movaps  KTRAP_XMM0 + 0x50(%rsp), %xmm5
    movaps  KTRAP_XMM0 + 0x60(%rsp), %xmm6
    movaps  KTRAP_XMM0 + 0x70(%rsp), %xmm7
    movaps  KTRAP_XMM0 + 0x80(%rsp), %xmm8
    movaps  KTRAP_XMM0 + 0x90(%rsp), %xmm9
    movaps  KTRAP_XMM0 + 0xA0(%rsp), %xmm10
    movaps  KTRAP_XMM0 + 0xB0(%rsp), %xmm11
    movaps  KTRAP_XMM0 + 0xC0(%rsp), %xmm12
    movaps  KTRAP_XMM0 + 0xD0(%rsp), %xmm13
    movaps  KTRAP_XMM0 + 0xE0(%rsp), %xmm14
    movaps  KTRAP_XMM0 + 0xF0(%rsp), %xmm15
9:  ldmxcsr KTRAP_MXCSR(%rsp)
.endm

/*
 * syscall from user mode: rcx = return rip, r11 = rflags, interrupts off.
 * the frame is built where the hardware would push one, at the stack
 * top KPCR.KernelRsp names.
 */
    .globl  KiSystemCall64
    .p2align 4
KiSystemCall64:
    swapgs
    movq    %rsp, %gs:KPCR_USER_RSP
    movq    %gs:KPCR_KERNEL_RSP, %rsp
    subq    $KTRAP_FRAME_SIZE, %rsp
    SAVE_GPRS
    movq    %gs:KPCR_USER_RSP, %rax
    movq    %rax, KTRAP_RSP(%rsp)
    movq    $(KGDT64_R3_DATA | RPL_USER), KTRAP_SS(%rsp)
    movq    %r11, KTRAP_RFLAGS(%rsp)
    movq    $(KGDT64_R3_CODE | RPL_USER), KTRAP_CS(%rsp)
    movq    %rcx, KTRAP_RIP(%rsp)
    movq    $0, KTRAP_ERROR(%rsp)
    movq    $KTRAP_VECTOR_SYSCALL, KTRAP_VECTOR(%rsp)
    SAVE_XMM
    movb    $0, %gs:KPCR_USER_MODE
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    call    KiAcquireKernelLock
    movq    %rbx, %rcx
    call    KiSystemService
    movq    %rbx, %rsp
    jmp     KiExceptionExit

/* a 32-bit system call has no service: it ends the process like an illegal instruction would. */
    .globl  KiSystemCall32
    .p2align 4
KiSystemCall32:
    ud2

/*
 * the interrupt stubs, 16 bytes each from KiIsrBase: an error code where
 * the processor pushes none, then the vector.
 */
.macro ISR vec
    .p2align 4
.if \vec == 8 || \vec == 10 || \vec == 11 || \vec == 12 || \vec == 13 || \vec == 14 || \vec == 17 || \vec == 21 || \vec == 29 || \vec == 30
    .byte   0x66, 0x90              /* two byte nop, in place of pushing an error code */
.else
    pushq   $0
.endif
    pushq   $\vec
    jmp     KiTrapCommon
.endm

    .p2align 4
    .globl  KiIsrBase
KiIsrBase:
    .set    vec, 0
    .rept   256
    ISR     vec
    .set    vec, vec + 1
    .endr

/*
 * common entry: the processor pushed ss, rsp, rflags, cs, rip, the stub an
 * error code and the vector. builds the rest of the trap frame below.
 */
    .p2align 4
KiTrapCommon:
    subq    $KTRAP_VECTOR, %rsp
    SAVE_GPRS
    testb   $RPL_USER, KTRAP_CS(%rsp)
    jz      1f
    swapgs
1:  cld
    SAVE_XMM
    movq    %cr2, %rax
    movq    %rax, KTRAP_CR2(%rsp)
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    testb   $RPL_USER, KTRAP_CS(%rbx)
    jz      2f
    movb    $0, %gs:KPCR_USER_MODE
    call    KiAcquireKernelLock
    jmp     3f
    /* an idle processor halted without the lock takes it back before handling anything. */
2:  cmpb    $0, %gs:KPCR_IDLE_SLEEPING
    je      3f
    movb    $0, %gs:KPCR_IDLE_SLEEPING
    call    KiAcquireKernelLock
3:  movq    %rbx, %rcx
    call    KiTrapDispatch
    movq    %rbx, %rsp
    jmp     KiExceptionExit

/* restores the trap frame at rsp and returns from the interrupt, exception or system call. */
    .globl  KiExceptionExit
    .p2align 4
KiExceptionExit:
    cli
    testb   $RPL_USER, KTRAP_CS(%rsp)
    jz      2f
    /* back to user mode, lock held: settle the tlb flushes other processors asked for. */
    cmpb    $0, %gs:KPCR_FLUSH_PENDING
    je      1f
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    call    KiSettleTlbFlush
    movq    %rbx, %rsp
1:  leaq    KTRAP_FRAME_SIZE(%rsp), %rax
    movq    %rax, %gs:KPCR_KERNEL_RSP
    movq    %gs:KPCR_TSS, %rcx
    movq    %rax, KTSS_RSP0(%rcx)
    /* the next processor waiting may enter the kernel. */
    movb    $1, %gs:KPCR_USER_MODE
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    call    KiReleaseKernelLock
    movq    %rbx, %rsp
    RESTORE_XMM
    RESTORE_GPRS
    addq    $KTRAP_RIP, %rsp
    swapgs
    iretq

2:  RESTORE_XMM
    RESTORE_GPRS
    addq    $KTRAP_RIP, %rsp
    iretq

    .section .rdata, "dr"
    .p2align 2
    .globl  KiKernelMxcsr
KiKernelMxcsr:
    .long   MXCSR_DEFAULT
