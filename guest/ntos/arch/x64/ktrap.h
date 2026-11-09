/* x64 definitions shared between c and assembly. */
#ifndef MIME_KTRAP_H
#define MIME_KTRAP_H

/*
 * KTRAP_FRAME layout, see arch.h. it keeps every xmm register so the user
 * context stays whole whatever the kernel did since, and ends with the
 * hardware frame, so its top is the stack top of an entry.
 */
#define KTRAP_PHYSMAP 0xFFFF800000000000ull

#define KTRAP_XMM0 0x000
#define KTRAP_RAX 0x100
#define KTRAP_RCX 0x108
#define KTRAP_RDX 0x110
#define KTRAP_RBX 0x118
#define KTRAP_RBP 0x120
#define KTRAP_RSI 0x128
#define KTRAP_RDI 0x130
#define KTRAP_R8 0x138
#define KTRAP_R9 0x140
#define KTRAP_R10 0x148
#define KTRAP_R11 0x150
#define KTRAP_R12 0x158
#define KTRAP_R13 0x160
#define KTRAP_R14 0x168
#define KTRAP_R15 0x170
#define KTRAP_CR2 0x178
#define KTRAP_MXCSR 0x180
#define KTRAP_FLAGS 0x184
#define KTRAP_VECTOR 0x188
#define KTRAP_ERROR 0x190
#define KTRAP_RIP 0x198
#define KTRAP_CS 0x1A0
#define KTRAP_RFLAGS 0x1A8
#define KTRAP_RSP 0x1B0
#define KTRAP_SS 0x1B8
#define KTRAP_FRAME_SIZE 0x1C0

/* the xmm registers hold the interrupted values, restored on exit. */
#define KTRAP_FLAG_FP_SAVED 1

#define KTRAP_VECTOR_SYSCALL 0x100

/* callee saved frame of KiSwapContextInternal and KiCallUserMode: 8 registers, xmm6 to xmm15, a slot. */
#define KSWITCH_FRAME_SIZE 0xF0
#define KSWITCH_RETURN 0xF0
#define KCALLOUT_FRAME_SIZE 0xF0
#define KCALLOUT_PREVIOUS_SP 0xE0

/* KPCR fields, see ntos.h. */
#define KPCR_USER_RSP 0x10
#define KPCR_SELF 0x18
#define KPCR_KERNEL_RSP 0x28
#define KPCR_TSS 0x08
#define KPCR_IRQL 0x50
#define KPCR_USER_MODE 0x51
#define KPCR_IDLE_SLEEPING 0x52
#define KPCR_FLUSH_PENDING 0x53
#define KPCR_FLUSH_ACK 0x54
#define KPCR_PRCB 0x180
#define KPCR_NUMBER (KPCR_PRCB + 0x00)
#define KPCR_CURRENT_THREAD (KPCR_PRCB + 0x08)
#define KPCR_DEFERRED_IPIS (KPCR_PRCB + 0xA0)

/*
 * words of KiKernelLock. the lock word and its sleepers have a cache line to themselves: the waiters read it while
 * they spin, and the owner's writes to the rest would take it from them each time.
 */
#define KI_LOCK_SLEEPERS 1
#define KI_LOCK_OWNER 16
#define KI_LOCK_REASON 17
#define KI_LOCK_SINCE 18
#define KI_LOCK_HELD 20

/* FlushPending bits: user translations, or global ones too. */
#define KPCR_FLUSH_USER 1
#define KPCR_FLUSH_GLOBAL 2

#define KTSS_RSP0 0x04

/* selectors, the same as on windows. */
#define KGDT64_R0_CODE 0x10
#define KGDT64_R0_DATA 0x18
#define KGDT64_R3_CMCODE 0x20
#define KGDT64_R3_DATA 0x28
#define KGDT64_R3_CODE 0x30
#define KGDT64_SYS_TSS 0x40
#define KGDT64_R3_CMTEB 0x50
#define RPL_USER 3

#define KI_VECTOR_FIRST 0x30   /* the interrupts start past the exceptions and the software interrupts of user code */
#define KI_VECTOR_HOST 0x81    /* the host's interrupt, see MimeSharedPage */
#define KI_VECTOR_CLOCK 0xD1   /* the local apic timer */
#define KI_VECTOR_IPI 0xE1     /* reschedule and leave user mode */
#define KI_VECTOR_FLUSH 0xE2   /* flush user translations, without the kernel lock */
#define KI_VECTOR_SPURIOUS 0xFF

/* the hypercall port: rax = number, rdi, rsi, rdx, r10, r8, r9 = arguments. */
#define MIME_HC_PORT 0x5E

#define MXCSR_DEFAULT 0x1F80

#endif
