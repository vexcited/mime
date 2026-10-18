/*
 * x64 definitions of the kernel: barriers, interrupt masking, the trap
 * frame and the page table entries.
 */
#ifndef MIME_ARCH_X64_H
#define MIME_ARCH_X64_H

#include "ktrap.h"

FORCEINLINE void KiMemoryBarrier(void)
{
    __asm__ volatile("" ::: "memory");
}

FORCEINLINE void KiDataSyncBarrier(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

FORCEINLINE void KiInstructionBarrier(void)
{
    __asm__ volatile("" ::: "memory");
}

FORCEINLINE ULONG64 KiDisableInterrupts(void)
{
    ULONG64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags)::"memory");
    return flags;
}

FORCEINLINE void KiRestoreInterrupts(ULONG64 flags)
{
    if (flags & 0x200)
        __asm__ volatile("sti" ::: "memory");
    else
        __asm__ volatile("cli" ::: "memory");
}

FORCEINLINE void KiEnableInterrupts(void)
{
    __asm__ volatile("sti" ::: "memory");
}

FORCEINLINE BOOLEAN KiInterruptsEnabled(void)
{
    ULONG64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

FORCEINLINE void KiPause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

FORCEINLINE ULONG64 KiReadMsr(ULONG msr)
{
    ULONG lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (ULONG64)hi << 32 | lo;
}

FORCEINLINE void KiWriteMsr(ULONG msr, ULONG64 value)
{
    __asm__ volatile("wrmsr" ::"c"(msr), "a"((ULONG)value), "d"((ULONG)(value >> 32)) : "memory");
}

FORCEINLINE ULONG64 KiReadCr3(void)
{
    ULONG64 v;
    __asm__ volatile("movq %%cr3, %0" : "=r"(v));
    return v;
}

FORCEINLINE void KiWriteCr3(ULONG64 v)
{
    __asm__ volatile("movq %0, %%cr3" ::"r"(v) : "memory");
}

FORCEINLINE void KiCpuid(ULONG leaf, ULONG subleaf, ULONG regs[4])
{
    __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(leaf), "c"(subleaf));
}

FORCEINLINE struct _KPCR *KeGetPcr(void)
{
    struct _KPCR *pcr;
    __asm__ volatile("movq %%gs:0x18, %0" : "=r"(pcr));
    return pcr;
}

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_CSTAR 0xC0000083
#define MSR_FMASK 0xC0000084
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102
#define MSR_TSC_AUX 0xC0000103
#define MSR_PAT 0x277
#define MSR_TSC_DEADLINE 0x6E0
#define MSR_X2APIC_ID 0x802
#define MSR_X2APIC_TPR 0x808
#define MSR_X2APIC_EOI 0x80B
#define MSR_X2APIC_SVR 0x80F
#define MSR_X2APIC_ICR 0x830
#define MSR_X2APIC_LVT_TIMER 0x832
#define MSR_APIC_BASE 0x1B

typedef struct DECLSPEC_ALIGN(16) _KTRAP_FRAME {
    M128A Xmm[16];
    ULONG64 Rax, Rcx, Rdx, Rbx, Rbp, Rsi, Rdi;
    ULONG64 R8, R9, R10, R11, R12, R13, R14, R15;
    ULONG64 Cr2;
    ULONG MxCsr;
    ULONG Flags;
    ULONG64 Vector;
    ULONG64 ErrorCode;
    ULONG64 Rip;
    ULONG64 Cs;
    ULONG64 Rflags;
    ULONG64 Rsp;
    ULONG64 Ss;
} KTRAP_FRAME, *PKTRAP_FRAME;
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Rax) == KTRAP_RAX, "trap rax");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, R15) == KTRAP_R15, "trap r15");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, MxCsr) == KTRAP_MXCSR, "trap mxcsr");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Flags) == KTRAP_FLAGS, "trap flags");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Vector) == KTRAP_VECTOR, "trap vector");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Rip) == KTRAP_RIP, "trap rip");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Ss) == KTRAP_SS, "trap ss");
MIME_STATIC_ASSERT(sizeof(KTRAP_FRAME) == KTRAP_FRAME_SIZE, "trap size");

#define MIME_ARCH_NAME "x64"
#define MIME_NATIVE_MACHINE IMAGE_FILE_MACHINE_AMD64
#define MIME_NATIVE_ARCHITECTURE PROCESSOR_ARCHITECTURE_AMD64
#define MIME_NATIVE_ARCHITECTURE_NAME L"AMD64"

#define KI_TRAP_PC(f) ((f)->Rip)
#define KI_TRAP_SP(f) ((f)->Rsp)
#define KI_TRAP_FROM_USER(f) (((f)->Cs & RPL_USER) == RPL_USER)
#define KI_TRAP_INTERRUPTS_ENABLED(f) (((f)->Rflags & 0x200) != 0)

/* system calls: eax = number, r10 rdx r8 r9 then the stack past the home area, the result in rax. */
#define KI_SERVICE_REGISTER_ARGS 4
#define KI_SERVICE_NUMBER(f) ((ULONG)(f)->Rax & 0xFFFF)
#define KI_SERVICE_STACK_ARGS(f) ((f)->Rsp + 0x28)
#define KI_SERVICE_RESULT(f) ((f)->Rax)
#define KI_SERVICE_REGISTER_ARG(f, i) ((i) == 0 ? (f)->R10 : (i) == 1 ? (f)->Rdx : (i) == 2 ? (f)->R8 : (f)->R9)

#define EFLAGS_USER_MASK 0x00000DD5ull /* cf pf af zf sf tf df of, what user mode may set */
#define EFLAGS_IF 0x200ull

typedef struct _KJUMP_BUFFER {
    ULONG64 Rbx, Rbp, Rdi, Rsi, R12, R13, R14, R15, Rsp, Rip;
    M128A Xmm6_15[10];
} KJUMP_BUFFER;

/*
 * the extended processor state of a user thread, saved at each switch
 * away from it: xsaveopt with the components of xcr0.
 */
extern ULONG KiXsaveSize;
extern ULONG64 KiXsaveMask;

/* page table entries. */
#define MM_PTE_VALID (1ull << 0)
#define MM_PTE_RW (1ull << 1)
#define MM_PTE_USER (1ull << 2)
#define MM_PTE_PWT (1ull << 3)
#define MM_PTE_PCD (1ull << 4)
#define MM_PTE_ACCESSED (1ull << 5)
#define MM_PTE_DIRTY (1ull << 6)
#define MM_PTE_LARGE (1ull << 7)
#define MM_PTE_GLOBAL (1ull << 8)
#define MM_PTE_SW_COW (1ull << 9)
#define MM_PTE_SW_SHARED (1ull << 10) /* page belongs to a section, never freed by the pte */
#define MM_PTE_SW_WATCH (1ull << 11)  /* a writable page of a write watched allocation, mapped read only until it is written */
#define MM_PTE_SW_GUARD (1ull << 52)  /* a guard page, kept from user mode until first touched */
#define MM_PTE_NX (1ull << 63)
#define MM_PTE_ADDR_MASK 0x000FFFFFFFFFF000ull
/* no leaf marker on x64: present without the large bit at the last level. */
#define MM_PTE_PAGE 0ull

/* invalid ptes with this bit describe committed but not yet present pages. */
#define MM_SWPTE_COMMITTED (1ull << 1)
#define MM_SWPTE_GUARD (1ull << 2)
#define MM_SWPTE_SECTION (1ull << 3) /* committed view page, built from the section on first touch */
#define MM_SWPTE_PROT_SHIFT 4
#define MM_SWPTE_PROT_MASK (0x7FFull << MM_SWPTE_PROT_SHIFT)

#define MM_KERNEL_RW (MM_PTE_RW | MM_PTE_GLOBAL | MM_PTE_ACCESSED | MM_PTE_DIRTY | MM_PTE_NX)
#define MM_KERNEL_RO (MM_PTE_GLOBAL | MM_PTE_ACCESSED | MM_PTE_NX)
#define MM_KERNEL_RX (MM_PTE_GLOBAL | MM_PTE_ACCESSED)

#define MI_PTE_SET_RO(p) ((p) & ~MM_PTE_RW)
#define MI_PTE_SET_RW(p) ((p) | MM_PTE_RW)
#define MI_PTE_IS_RO(p) (((p) & MM_PTE_RW) == 0)
#define MI_PTE_IS_USER(p) (((p) & MM_PTE_USER) != 0)
#define MI_PTE_USER_EXEC(p) (((p) & MM_PTE_NX) == 0)
/* a present user page that user mode cannot reach: no access, or a guard page until its first touch. */
#define MI_PTE_IS_GUARD(p) (((p) & MM_PTE_SW_GUARD) != 0)
#define MI_PTE_CLEAR_GUARD(p) (((p) & ~MM_PTE_SW_GUARD) | MM_PTE_USER)
/* an entry above the last level that points to a table rather than mapping a large page. */
#define MI_PTE_IS_TABLE(e, level) ((level) < 3 && !((e) & MM_PTE_LARGE))
/* the first root entries a process owns; the upper half is the kernel's, shared by every process. */
#define MI_USER_ROOT_ENTRIES 256

#endif
