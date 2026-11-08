/*
 * arm64 kernel definitions: barriers, interrupt masking, the trap frame
 * and the translation table descriptors.
 */
#ifndef MIME_ARCH_ARM64_H
#define MIME_ARCH_ARM64_H

#include "ktrap.h"

FORCEINLINE void KiMemoryBarrier(void)
{
    __asm__ volatile("dmb ish" ::: "memory");
}

FORCEINLINE void KiMemoryBarrierFull(void)
{
    __asm__ volatile("dmb ish" ::: "memory");
}

FORCEINLINE void KiDataSyncBarrier(void)
{
    __asm__ volatile("dsb ish" ::: "memory");
}

FORCEINLINE void KiInstructionBarrier(void)
{
    __asm__ volatile("isb" ::: "memory");
}

#define KI_READ_SYSREG(name)                                                                                      \
    ({                                                                                                            \
        ULONG64 _v;                                                                                               \
        __asm__ volatile("mrs %0, " #name : "=r"(_v));                                                            \
        _v;                                                                                                       \
    })
#define KI_WRITE_SYSREG(name, v) __asm__ volatile("msr " #name ", %0" ::"r"((ULONG64)(v)) : "memory")

FORCEINLINE ULONG64 KiDisableInterrupts(void)
{
    ULONG64 daif = KI_READ_SYSREG(daif);
    __asm__ volatile("msr daifset, #3" ::: "memory");
    return daif;
}

FORCEINLINE void KiRestoreInterrupts(ULONG64 daif)
{
    KI_WRITE_SYSREG(daif, daif);
}

FORCEINLINE void KiEnableInterrupts(void)
{
    __asm__ volatile("msr daifclr, #3" ::: "memory");
}

FORCEINLINE BOOLEAN KiInterruptsEnabled(void)
{
    return (KI_READ_SYSREG(daif) & (1u << 7)) == 0;
}

typedef struct _KTRAP_FRAME {
    ULONG64 X[31];
    ULONG64 Sp;
    ULONG64 Pc;
    ULONG64 Spsr;
    ULONG64 Esr;
    ULONG64 Far;
    ULONG64 Flags;
    ULONG64 Reserved;
    NEON128 V[32];
    ULONG Fpcr;
    ULONG Fpsr;
    ULONG64 Pad;
} KTRAP_FRAME, *PKTRAP_FRAME;
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Sp) == KTRAP_SP, "trap sp");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Flags) == KTRAP_FLAGS, "trap flags");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, V) == KTRAP_V, "trap v");
MIME_STATIC_ASSERT(offsetof(KTRAP_FRAME, Fpcr) == KTRAP_FPCR, "trap fpcr");
MIME_STATIC_ASSERT(sizeof(KTRAP_FRAME) == KTRAP_FRAME_SIZE, "trap size");

#define MIME_ARCH_NAME "arm64"
#define MIME_NATIVE_MACHINE IMAGE_FILE_MACHINE_ARM64
#define MIME_NATIVE_ARCHITECTURE PROCESSOR_ARCHITECTURE_ARM64
#define MIME_NATIVE_ARCHITECTURE_NAME L"ARM64"

#define KI_TRAP_PC(f) ((f)->Pc)
#define KI_TRAP_SP(f) ((f)->Sp)
#define KI_TRAP_FROM_USER(f) (((f)->Spsr & 0xF) == PSR_MODE_EL0)
#define KI_TRAP_INTERRUPTS_ENABLED(f) (((f)->Spsr & (1u << 7)) == 0)

/* system calls: svc #number, arguments in x0 to x7 then on the stack, result in x0. */
#define KI_SERVICE_REGISTER_ARGS 8
#define KI_SERVICE_NUMBER(f) ((ULONG)(f)->Esr & 0xFFFF)
#define KI_SERVICE_STACK_ARGS(f) ((f)->Sp)
#define KI_SERVICE_RESULT(f) ((f)->X[0])
#define KI_SERVICE_REGISTER_ARG(f, i) ((f)->X[i])

FORCEINLINE void KiPause(void)
{
    __asm__ volatile("yield" ::: "memory");
}

FORCEINLINE struct _KPCR *KeGetPcr(void)
{
    struct _KPCR *pcr;
    __asm__ volatile("mov %0, x18" : "=r"(pcr));
    return pcr;
}

/* the boot processor's id registers. */
typedef struct KI_PROCESSOR_IDS {
    ULONG64 Midr, Pfr0, Pfr1, Dfr0, Dfr1, Afr0, Afr1, Isar0, Isar1, Isar2, Mmfr0, Mmfr1, Mmfr2, Ctr;
} KI_PROCESSOR_IDS;
extern KI_PROCESSOR_IDS KiProcessorIds;

#define PSR_MODE_EL0 0x0
#define PSR_MODE_EL1H 0x5
#define PSR_DAIF_MASK (0xFull << 6)

typedef struct _KJUMP_BUFFER {
    ULONG64 X19_X30[12];
    ULONG64 Sp;
    ULONG64 D8_D15[8];
} KJUMP_BUFFER;


/* stage 1 descriptor bits. */
#define MM_PTE_VALID (1ull << 0)
#define MM_PTE_TABLE (1ull << 1)
#define MM_PTE_PAGE (1ull << 1)
#define MM_PTE_ATTR(i) ((ULONG64)(i) << 2)
#define MM_PTE_AP_EL0 (1ull << 6)
#define MM_PTE_AP_RO (1ull << 7)
#define MM_PTE_SH_INNER (3ull << 8)
#define MM_PTE_AF (1ull << 10)
#define MM_PTE_NG (1ull << 11)
#define MM_PTE_PXN (1ull << 53)
#define MM_PTE_UXN (1ull << 54)
#define MM_PTE_SW_COW (1ull << 55)
#define MM_PTE_SW_SHARED (1ull << 56) /* section page, never freed through the pte. */
#define MM_PTE_SW_GUARD (1ull << 57)  /* guard page, hidden from user mode until first touched. */
#define MM_PTE_SW_WATCH (1ull << 58)  /* write watched page, mapped read only until written. */
/* x86 code xta64 decoded, mapped read only until written. a pbha bit, which the hardware ignores with TCR_EL1.HWU clear. */
#define MM_PTE_SW_CODE (1ull << 59)
#define MM_PTE_SW_TRIMMED 0ull /* the working set is never trimmed on arm64. */
#define MM_PTE_ADDR_MASK 0x0000FFFFFFFFF000ull

/* invalid ptes with this bit describe committed but not yet present pages. */
#define MM_SWPTE_COMMITTED (1ull << 1)
#define MM_SWPTE_GUARD (1ull << 2)
#define MM_SWPTE_SECTION (1ull << 3) /* committed view page, built from the section on first touch. */
#define MM_SWPTE_PROT_SHIFT 4
#define MM_SWPTE_PROT_MASK (0x7FFull << MM_SWPTE_PROT_SHIFT)

#define MI_PTE_SET_RO(p) ((p) | MM_PTE_AP_RO)
#define MI_PTE_SET_RW(p) ((p) & ~MM_PTE_AP_RO)
#define MI_PTE_IS_RO(p) (((p) & MM_PTE_AP_RO) != 0)
#define MI_PTE_IS_USER(p) (((p) & MM_PTE_AP_EL0) != 0)
#define MI_PTE_USER_EXEC(p) (((p) & MM_PTE_UXN) == 0)
/* a present user page that user mode cannot reach: no access, or a guard page before its first touch. */
#define MI_PTE_IS_GUARD(p) (((p) & MM_PTE_SW_GUARD) != 0)
#define MI_PTE_CLEAR_GUARD(p) (((p) & ~MM_PTE_SW_GUARD) | MM_PTE_AP_EL0)
/* a non leaf entry that points to a table rather than mapping a block. */
#define MI_PTE_IS_TABLE(e, level) ((level) < 3 && ((e) & MM_PTE_TABLE))
/* ttbr0 holds user space only. */
#define MI_USER_ROOT_ENTRIES 512

#define MM_KERNEL_RW (MM_PTE_ATTR(0) | MM_PTE_SH_INNER | MM_PTE_AF | MM_PTE_UXN | MM_PTE_PXN)
#define MM_KERNEL_RO (MM_KERNEL_RW | MM_PTE_AP_RO)
#define MM_KERNEL_RX (MM_PTE_ATTR(0) | MM_PTE_SH_INNER | MM_PTE_AF | MM_PTE_UXN | MM_PTE_AP_RO)

#endif
