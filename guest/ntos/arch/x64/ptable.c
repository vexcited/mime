/*
 * x64 page tables: walking, mapping and tlb maintenance.
 *
 * the upper half of every process root copies the kernel root, whose 256
 * entries the loader creates, so all address spaces share the kernel tables.
 * x64 has no broadcast invalidation: a processor that changes a translation
 * invalidates its own tlb, sets FlushPending on the others, which settle it
 * before using translations again, and interrupts those running the same
 * address space in user mode.
 */
#include "../../ntos.h"

static ULONG64 *g_kernel_root;
static ULONG64 g_kernel_root_phys;

ULONG64 *MiKernelRoot(void)
{
    return g_kernel_root;
}

void MiInitializeKernelTables(MimeBootInfo *boot)
{
    g_kernel_root_phys = boot->ttbr1_phys;
    g_kernel_root = (ULONG64 *)HalPhysmap(boot->ttbr1_phys);
}

ULONG64 MiCreateSystemRoot(void)
{
    return g_kernel_root_phys;
}

void MiInitializeProcessRoot(ULONG64 *root)
{
    memcpy(root + MI_USER_ROOT_ENTRIES, g_kernel_root + MI_USER_ROOT_ENTRIES, MI_USER_ROOT_ENTRIES * sizeof(ULONG64));
}

ULONG64 MiDirectoryTableBase(PMMADDRESS_SPACE as)
{
    return as->RootPhys;
}

static inline ULONG table_index(ULONG_PTR va, int level)
{
    return (ULONG)((va >> (39 - 9 * level)) & 0x1FF);
}

ULONG64 *MiWalk(ULONG64 *root, ULONG_PTR va, BOOLEAN create, int level)
{
    ULONG64 *table = root;
    BOOLEAN user = va < 0x0000800000000000ull;
    for (int l = 0; l < level; l++) {
        ULONG64 *entry = &table[table_index(va, l)];
        if (!(*entry & MM_PTE_VALID)) {
            /* the kernel's root entries exist from boot, so every process shares them. */
            if (!create || (!user && l == 0))
                return nullptr;
            ULONG64 pa = MmAllocatePage();
            if (!pa)
                return nullptr;
            PMMPFN pfn = MiGetPfn(pa);
            if (pfn)
                pfn->Flags |= MM_PFN_PAGE_TABLE;
            *entry = pa | MM_PTE_VALID | MM_PTE_RW | MM_PTE_ACCESSED | (user ? MM_PTE_USER : 0);
        } else if (!MI_PTE_IS_TABLE(*entry, l)) {
            return nullptr; /* large page, cannot descend */
        }
        table = (ULONG64 *)HalPhysmap(*entry & MM_PTE_ADDR_MASK);
    }
    return &table[table_index(va, level)];
}

static void flush_local_all(BOOLEAN global)
{
    if (global) {
        ULONG64 cr4;
        __asm__ volatile("movq %%cr4, %0" : "=r"(cr4));
        __asm__ volatile("movq %0, %%cr4; movq %1, %%cr4" ::"r"(cr4 & ~(1ull << 7)), "r"(cr4) : "memory");
    } else {
        KiWriteCr3(KiReadCr3());
    }
}

/*
 * the other processors must flush before they next use translations. those
 * in user mode on this address space are interrupted into the kernel, where
 * they wait for the lock this processor holds and flush before going back.
 */
static void shootdown(BOOLEAN global)
{
    PKPRCB self = KeGetCurrentPrcb();
    /* the boot processor has no thread until the scheduler starts, and no other processor runs yet. */
    PKTHREAD thread = KiCurrentThread();
    PKPROCESS process = thread ? thread->ApcStateProcess : nullptr;
    ULONG64 targets = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        if (!p || p == self)
            continue;
        PKPCR pcr = CONTAINING_RECORD(p, KPCR, Prcb);
        __atomic_fetch_or(&pcr->FlushPending, global ? KPCR_FLUSH_GLOBAL | KPCR_FLUSH_USER : KPCR_FLUSH_USER,
                          __ATOMIC_SEQ_CST);
        if (!global && __atomic_load_n(&pcr->UserMode, __ATOMIC_SEQ_CST) &&
            p->CurrentThread->ApcStateProcess == process) {
            targets |= 1ull << i;
        }
    }
    if (targets)
        HalSendFlushIpi(targets);
    /* a processor has answered once it left user mode or flushed and cleared the request. */
    while (targets) {
        ULONG i = (ULONG)__builtin_ctzll(targets);
        PKPCR pcr = CONTAINING_RECORD(KiProcessorBlock[i], KPCR, Prcb);
        if (!__atomic_load_n(&pcr->UserMode, __ATOMIC_SEQ_CST) ||
            !(__atomic_load_n(&pcr->FlushPending, __ATOMIC_SEQ_CST) & KPCR_FLUSH_USER))
            targets &= targets - 1;
        else
            KiPause();
    }
}

void MiFlushTlbPage(ULONG_PTR va, USHORT asid, BOOLEAN global)
{
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
    shootdown(global);
}

/* long runs flush all of the process's translations instead. */
void MiFlushTlbRange(ULONG_PTR va, SIZE_T pages, USHORT asid)
{
    if (pages > 32) {
        flush_local_all(FALSE);
    } else {
        for (SIZE_T i = 0; i < pages; i++)
            __asm__ volatile("invlpg (%0)" ::"r"(va + (i << PAGE_SHIFT)) : "memory");
    }
    shootdown(FALSE);
}

void MiFlushTlbAsid(USHORT asid)
{
    flush_local_all(FALSE);
    shootdown(FALSE);
}

void MiFlushTlbAll(void)
{
    flush_local_all(TRUE);
    shootdown(TRUE);
}

/* kernel lock held. */
void KiSettleTlbFlush(void)
{
    PKPCR pcr = KeGetPcr();
    UCHAR pending = __atomic_exchange_n(&pcr->FlushPending, 0, __ATOMIC_SEQ_CST);
    if (pending)
        flush_local_all((pending & KPCR_FLUSH_GLOBAL) != 0);
}

ULONG64 MiReadPte(ULONG64 *root, ULONG_PTR va)
{
    ULONG64 *pte = MiWalk(root, va, FALSE, 3);
    return pte ? *pte : 0;
}

void MiWritePte(ULONG64 *root, ULONG_PTR va, ULONG64 value, USHORT asid)
{
    ULONG64 *pte = MiWalk(root, va, TRUE, 3);
    if (!pte)
        KeBugCheckEx(0x0000001A, va, 0, 0, 0); /* MEMORY_MANAGEMENT */
    ULONG64 old = *pte;
    *pte = value;
    if (old & MM_PTE_VALID)
        MiFlushTlbPage(va, asid, va >= 0xFFFF800000000000ull);
}

BOOLEAN MiMapPage(ULONG64 *root, ULONG_PTR va, ULONG64 pa, ULONG64 attrs)
{
    ULONG64 *pte = MiWalk(root, va, TRUE, 3);
    if (!pte)
        return FALSE;
    ULONG64 old = *pte;
    *pte = (pa & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID;
    if (old & MM_PTE_VALID)
        MiFlushTlbPage(va, 0, va >= 0xFFFF800000000000ull);
    return TRUE;
}

ULONG64 MiUnmapPage(ULONG64 *root, ULONG_PTR va, USHORT asid)
{
    ULONG64 *pte = MiWalk(root, va, FALSE, 3);
    if (!pte)
        return 0;
    ULONG64 old = *pte;
    *pte = 0;
    if (old & MM_PTE_VALID)
        MiFlushTlbPage(va, asid, va >= 0xFFFF800000000000ull);
    return old;
}

/* invalidates once for the whole run. */
void MiRewritePtes(ULONG64 *pte, ULONG_PTR va, SIZE_T count, const ULONG64 *values, USHORT asid)
{
    SIZE_T first = count, last = 0;
    for (SIZE_T i = 0; i < count; i++) {
        if (pte[i] & MM_PTE_VALID) {
            if (first == count)
                first = i;
            last = i;
        }
        pte[i] = values[i];
    }
    if (first != count)
        MiFlushTlbRange(va + (first << PAGE_SHIFT), last - first + 1, asid);
}

/* old gets the previous entries once the translations are gone. */
void MiClearPtes(ULONG64 *pte, ULONG_PTR va, SIZE_T count, USHORT asid, ULONG64 *old)
{
    SIZE_T first = count, last = 0;
    for (SIZE_T i = 0; i < count; i++) {
        ULONG64 e = pte[i];
        old[i] = e;
        if (e & MM_PTE_VALID) {
            if (first == count)
                first = i;
            last = i;
        }
        pte[i] = 0;
    }
    if (first != count)
        MiFlushTlbRange(va + (first << PAGE_SHIFT), last - first + 1, asid);
}

ULONG64 MiProtectToPte(ULONG protect, BOOLEAN user)
{
    ULONG64 pte = MM_PTE_ACCESSED | MM_PTE_DIRTY | (user ? MM_PTE_USER : MM_PTE_GLOBAL);
    ULONG base = protect & 0xFF;
    BOOLEAN exec = base & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    BOOLEAN write = base & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE);
    BOOLEAN cow = base & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY);
    if (!exec)
        pte |= MM_PTE_NX;
    if (write)
        pte |= MM_PTE_RW;
    if (cow)
        pte |= MM_PTE_SW_COW;
    if (protect & PAGE_NOCACHE)
        pte |= MM_PTE_PCD | MM_PTE_PWT;
    /* the page and its data stay, but user mode cannot reach them. */
    if (user && (base == PAGE_NOACCESS || (protect & PAGE_GUARD))) {
        pte &= ~MM_PTE_USER;
        if (protect & PAGE_GUARD)
            pte |= MM_PTE_SW_GUARD;
    }
    return pte;
}

ULONG MiPteToProtect(ULONG64 pte)
{
    if (!(pte & MM_PTE_VALID)) {
        if (pte & MM_SWPTE_COMMITTED)
            return (ULONG)((pte & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        return 0;
    }
    BOOLEAN ro = !(pte & (MM_PTE_RW | MM_PTE_SW_WATCH | MM_PTE_SW_CODE));
    BOOLEAN exec = !(pte & MM_PTE_NX);
    ULONG protect;
    if (pte & MM_PTE_SW_COW)
        protect = exec ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY;
    else if (exec)
        protect = ro ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE;
    else
        protect = ro ? PAGE_READONLY : PAGE_READWRITE;
    if (!(pte & MM_PTE_USER))
        return (pte & MM_PTE_SW_GUARD) ? protect | PAGE_GUARD : PAGE_NOACCESS;
    return protect;
}

/* adds access without a shootdown: a processor still holding the old entry faults, which flushes it. */
void MiUpgradePte(ULONG64 *root, ULONG_PTR va, ULONG64 value, USHORT asid)
{
    ULONG64 *pte = MiWalk(root, va, FALSE, 3);
    if (!pte)
        return;
    *pte = value;
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
}

BOOLEAN NTAPI MmIsAddressValid(PVOID va)
{
    ULONG_PTR v = (ULONG_PTR)va;
    if (v >= MIME_KVA_PHYSMAP && v < MIME_KVA_PHYSMAP + HalBootInfo->ram_base + HalBootInfo->ram_size)
        return TRUE;
    ULONG64 *table = (ULONG64 *)HalPhysmap(KiReadCr3() & MM_PTE_ADDR_MASK);
    for (int level = 0; level < 4; level++) {
        ULONG64 e = table[table_index(v, level)];
        if (!(e & MM_PTE_VALID))
            return FALSE;
        if (!MI_PTE_IS_TABLE(e, level))
            return TRUE;
        table = (ULONG64 *)HalPhysmap(e & MM_PTE_ADDR_MASK);
    }
    return FALSE;
}
