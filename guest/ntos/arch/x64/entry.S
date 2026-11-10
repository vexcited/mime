/*
 * kernel entry points, interrupt and system call entry, and the exit to user mode.
 *
 * gs holds the KPCR in kernel mode and the TEB in user mode, swapped on
 * every crossing. an entry from user mode clears KPCR.UserMode before it
 * takes the kernel lock, and the exit sets it before releasing the lock,
 * so the lock holder sees exactly which processors run user code.
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
 * the kernel lock. its word is odd while held and every release adds one, so a waiter sleeping on the value it saw
 * wakes on any release since. a free lock goes to whichever processor takes it first, never to one still asleep in
 * the host, whose wakeup would hold up every processor behind it. a waiter spins on loads for a few microseconds,
 * then sleeps in the host, and a releaser that sees sleepers wakes one. the processors interrupted while the lock was
 * held are interrupted once it is let go, see KiSendIpi.
 * clobbers rax, rcx, rdx and r8 to r11, as any call.
 */
#define LOCK_SLEEPERS (KI_LOCK_SLEEPERS * 4)
#define LOCK_OWNER (KI_LOCK_OWNER * 4)
#define LOCK_REASON (KI_LOCK_REASON * 4)
#define LOCK_SINCE (KI_LOCK_SINCE * 4)
#define LOCK_HELD (KI_LOCK_HELD * 4)
#define LOCK_SPINS 2048
#define HC_WORD_WAIT 0x0D
#define HC_WORD_WAKE 0x0E

    .globl  KiAcquireKernelLock
    .p2align 4
KiAcquireKernelLock:
    leaq    KiKernelLock(%rip), %r8
    xorl    %r11d, %r11d            /* when waiting began, 0 if the lock was free */
    movl    (%r8), %eax
    testb   $1, %al
    jnz     1f
    leal    1(%rax), %ecx
    lock cmpxchgl %ecx, (%r8)
    je      4f
1:  rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    movq    %rax, %r11
2:  movl    $LOCK_SPINS, %r10d
3:  pause
    movl    (%r8), %eax
    testb   $1, %al
    jz      6f
    decl    %r10d
    jnz     3b
    cmpq    $0, KiKernelLockPhys(%rip)
    je      2b
    lock incl LOCK_SLEEPERS(%r8)
    movl    (%r8), %eax
    testb   $1, %al
    jz      5f
    pushq   %rdi
    pushq   %rsi
    movl    %eax, %esi
    movl    $HC_WORD_WAIT, %eax
    movq    KiKernelLockPhys(%rip), %rdi
    outb    %al, $MIME_HC_PORT
    popq    %rsi
    popq    %rdi
    leaq    KiKernelLock(%rip), %r8
5:  lock decl LOCK_SLEEPERS(%r8)
    jmp     2b
6:  leal    1(%rax), %ecx
    lock cmpxchgl %ecx, (%r8)
    jne     2b
4:  rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    movq    %rax, LOCK_SINCE(%r8)
    movl    %gs:KPCR_NUMBER, %eax
    movl    %eax, LOCK_OWNER(%r8)
    /* lock statistics: KiLockStats has eight counters per processor. */
    shll    $6, %eax
    leaq    KiLockStats(%rip), %rcx
    addq    %rax, %rcx
    incq    (%rcx)
    testq   %r11, %r11
    jz      7f
    movq    LOCK_SINCE(%r8), %rax
    subq    %r11, %rax
    incq    8(%rcx)
    addq    %rax, 16(%rcx)
    cmpq    24(%rcx), %rax
    jbe     7f
    movq    %rax, 24(%rcx)
7:
    /* kernel translations another processor changed while this one was away. */
    testb   $KPCR_FLUSH_GLOBAL, %gs:KPCR_FLUSH_PENDING
    jz      8f
    movb    $0, %gs:KPCR_FLUSH_PENDING
    movq    %cr4, %rax
    movq    %rax, %rcx
    andq    $~0x80, %rcx
    movq    %rcx, %cr4
    movq    %rax, %cr4
    ret
    /*
     * user translations too: the kernel writes to user memory from here on, and a stale entry
     * for a page another thread freed and the allocator reused would take the write.
     */
8:  testb   $KPCR_FLUSH_USER, %gs:KPCR_FLUSH_PENDING
    jz      9f
    cmpb    $0, KiNoLockFlush(%rip)
    jne     9f
    movb    $0, %gs:KPCR_FLUSH_PENDING
    movq    %cr3, %rax
    movq    %rax, %cr3
9:  ret

    .globl  KiReleaseKernelLock
    .p2align 4
KiReleaseKernelLock:
    leaq    KiKernelLock(%rip), %r8
    movl    LOCK_OWNER(%r8), %eax
    cmpl    %gs:KPCR_NUMBER, %eax
    je      1f
    ud2
1:  /* hold time, counted overall and, for long holds, by holder. */
    rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    subq    LOCK_SINCE(%r8), %rax
    addq    %rax, LOCK_HELD(%r8)
    cmpq    KiLongHoldTicks(%rip), %rax
    jb      3f
    subq    $40, %rsp
    movq    %rax, %rcx
    call    KiNoteLongHold
    addq    $40, %rsp
    leaq    KiKernelLock(%rip), %r8
3:  movl    $0, LOCK_REASON(%r8)
    movl    $-1, LOCK_OWNER(%r8)
    lock incl (%r8)
    cmpl    $0, LOCK_SLEEPERS(%r8)
    je      2f
    pushq   %rdi
    movl    $HC_WORD_WAKE, %eax
    movq    KiKernelLockPhys(%rip), %rdi
    outb    %al, $MIME_HC_PORT
    popq    %rdi
2:  cmpq    $0, %gs:KPCR_DEFERRED_IPIS
    je      4f
    xorl    %ecx, %ecx
    xchgq   %rcx, %gs:KPCR_DEFERRED_IPIS
    subq    $40, %rsp
    call    HalSendIpiMask
    addq    $40, %rsp
4:  ret

/*
 * the user tlb flush interrupt. it touches nothing the kernel lock guards,
 * so it runs without it: flush, clear the request, return. waiting for the
 * lock would queue every processor of a process behind the requester.
 */
    .globl  KiFlushIpi
    .p2align 4
KiFlushIpi:
    pushq   %rax
    pushq   %rcx
    pushq   %rdx
    testb   $RPL_USER, 32(%rsp)         /* the interrupted code segment, above three registers and rip */
    jz      1f
    swapgs
1:  movq    %cr3, %rax
    movq    %rax, %cr3                  /* the translations of the address space, not the global ones */
    lock andb $0xFE, %gs:KPCR_FLUSH_PENDING
    xorl    %eax, %eax
    xorl    %edx, %edx
    movl    $0x80B, %ecx                /* the end of the interrupt, MSR_X2APIC_EOI */
    wrmsr
    testb   $RPL_USER, 32(%rsp)
    jz      2f
    swapgs
2:  popq    %rdx
    popq    %rcx
    popq    %rax
    iretq

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
 * the frame is built where the hardware would push one, at KPCR.KernelRsp.
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
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    /* a few services are first tried without the kernel lock and with UserMode still set, see KiFastSystemService. */
    movq    %rbx, %rcx
    call    KiFastSystemService
    cmpl    $1, %eax
    je      KiFastServiceExit
    cmpl    $2, %eax
    je      1f
    movb    $0, %gs:KPCR_USER_MODE
    call    KiAcquireKernelLock
    movq    %rbx, %rcx
    call    KiSystemService
1:  movq    %rbx, %rsp
    jmp     KiExceptionExit

/*
 * back to user mode from a service that never had the kernel lock, interrupts off. UserMode is stored before
 * FlushPending is read: a processor that changes the address space sets the one and then reads the other, so
 * either this one flushes here or it is interrupted in user mode.
 */
    .p2align 4
KiFastServiceExit:
    movq    %rbx, %rsp
    leaq    KTRAP_FRAME_SIZE(%rsp), %rax
    movq    %rax, %gs:KPCR_KERNEL_RSP
    movq    %gs:KPCR_TSS, %rcx
    movq    %rax, KTSS_RSP0(%rcx)
    movb    $1, %al
    xchgb   %al, %gs:KPCR_USER_MODE
    cmpb    $0, %gs:KPCR_FLUSH_PENDING
    je      1f
    subq    $0x20, %rsp
    call    KiSettleTlbFlush
    movq    %rbx, %rsp
1:  RESTORE_XMM
    RESTORE_GPRS
    addq    $KTRAP_RIP, %rsp
    swapgs
    iretq

/*
 * BOOLEAN KiFastReadUser64(const void *address, ULONG64 *value): reads the caller's memory without the kernel
 * lock. a fault at the load is not handled: KiTrapCommon makes the read return FALSE instead.
 */
    .globl  KiFastReadUser64
    .p2align 4
KiFastReadUser64:
KiFastUserRead:
    movq    (%rcx), %rax
    movq    %rax, (%rdx)
    movb    $1, %al
    ret
KiFastUserReadFailed:
    xorl    %eax, %eax
    ret

/* there is no 32-bit system call: it ends the process like an illegal instruction. */
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
 * error code and the vector. the rest of the trap frame goes below.
 */
    .p2align 4
    /*
     * the unwind data tells what the stack holds where the handlers are called, below the trap frame by the
     * shadow space: a fault of a driver unwinds through here to the frame it happened in, as through windows'
     * trap handlers.
     */
    .seh_proc KiTrapCommon
KiTrapCommon:
    .seh_pushframe @code
    .seh_stackalloc 0x1B0                /* KTRAP_ERROR + 0x20 */
    /* the interrupted code's nonvolatile registers are the trap frame's, whatever ran since. */
    .seh_savereg %rbx, 0x138
    .seh_savereg %rbp, 0x140
    .seh_savereg %rsi, 0x148
    .seh_savereg %rdi, 0x150
    .seh_savereg %r12, 0x178
    .seh_savereg %r13, 0x180
    .seh_savereg %r14, 0x188
    .seh_savereg %r15, 0x190
    .seh_endprologue
    subq    $KTRAP_VECTOR, %rsp
    SAVE_GPRS
    testb   $RPL_USER, KTRAP_CS(%rsp)
    jz      1f
    swapgs
1:  cld
    SAVE_XMM
    movq    %cr2, %rax
    movq    %rax, KTRAP_CR2(%rsp)
    /* keep the exception in the boot trace page; a triple fault then names it. */
    movq    HalExceptionTracePa(%rip), %r10
    testq   %r10, %r10
    jz      9f
    movl    %gs:KPCR_NUMBER, %eax
    shll    $8, %eax
    addq    %rax, %r10
    movabsq $KTRAP_PHYSMAP, %rax
    addq    %rax, %r10
    movzbl  0(%r10), %eax
    movl    %eax, %r11d
    andl    $3, %r11d
    imull   $56, %r11d
    leaq    8(%r10,%r11), %r11
    movq    KTRAP_VECTOR(%rsp), %rax
    movq    %rax, 0(%r11)
    movq    KTRAP_RIP(%rsp), %rax
    movq    %rax, 8(%r11)
    movq    KTRAP_RSP(%rsp), %rax
    movq    %rax, 16(%r11)
    movq    KTRAP_CR2(%rsp), %rax
    movq    %rax, 24(%r11)
    movq    KTRAP_ERROR(%rsp), %rax
    movq    %rax, 32(%r11)
    movzbl  0(%r10), %eax
    incl    %eax
    movb    %al, 0(%r10)
9:
    movq    %rsp, %rbx
    subq    $0x20, %rsp
    /*
     * an interrupt is ended before the kernel lock is waited for: the write exits to the host, and with the lock
     * held every processor waiting for it would pay that. interrupts stay masked until the handler is done.
     */
    movq    KTRAP_VECTOR(%rbx), %rax
    cmpq    $KI_VECTOR_FIRST, %rax
    jb      4f
    cmpq    $KI_VECTOR_SPURIOUS, %rax
    je      4f
    xorl    %eax, %eax
    xorl    %edx, %edx
    movl    $0x80B, %ecx                /* MSR_X2APIC_EOI */
    wrmsr
4:  testb   $RPL_USER, KTRAP_CS(%rbx)
    jz      2f
    /*
     * a not present page fault is first tried without the kernel lock and with UserMode still set,
     * so a processor changing the address space interrupts this one, taken when the fault returns.
     */
    cmpq    $14, KTRAP_VECTOR(%rbx)
    jne     5f
    testb   $1, KTRAP_ERROR(%rbx)
    jnz     5f
    movq    %rbx, %rcx
    call    KiFastPageFault
    testb   %al, %al
    jz      5f
    movq    %rbx, %rsp
    RESTORE_XMM
    RESTORE_GPRS
    addq    $KTRAP_RIP, %rsp
    swapgs
    iretq
5:  movb    $0, %gs:KPCR_USER_MODE
    call    KiAcquireKernelLock
    jmp     3f
    /*
     * an idle processor waits without the lock. the clock and a request from another processor work under the
     * dispatcher lock alone, and leave dpcs and the switch to the idle loop. any other takes the lock first.
     */
2:  cmpq    $14, KTRAP_VECTOR(%rbx)
    jne     6f
    leaq    KiFastUserRead(%rip), %rax
    cmpq    %rax, KTRAP_RIP(%rbx)
    jne     6f
    /* the one read of user memory made without the kernel lock fails instead, see KiFastReadUser64. */
    leaq    KiFastUserReadFailed(%rip), %rax
    movq    %rax, KTRAP_RIP(%rbx)
    movq    %rbx, %rsp
    jmp     KiExceptionExit
6:  cmpb    $0, %gs:KPCR_IDLE_SLEEPING
    je      3f
    movq    KTRAP_VECTOR(%rbx), %rax
    cmpq    $KI_VECTOR_CLOCK, %rax
    je      3f
    cmpq    $KI_VECTOR_IPI, %rax
    je      3f
    movb    $0, %gs:KPCR_IDLE_SLEEPING
    call    KiAcquireKernelLock
3:  movq    %rbx, %rcx
    call    KiTrapDispatch
    movq    %rbx, %rsp
    jmp     KiExceptionExit
    .seh_endproc

/* returns through the trap frame at rsp. */
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
