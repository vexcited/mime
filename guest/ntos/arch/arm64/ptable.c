/*
 * translation tables: walking, mapping and tlb maintenance.
 */
#include "../../ntos.h"

static ULONG64 *g_kernel_root;

ULONG64 *MiKernelRoot(void)
{
    return g_kernel_root;
}

void MiInitializeKernelTables(MimeBootInfo *boot)
{
    g_kernel_root = (ULONG64 *)HalPhysmap(boot->ttbr1_phys);
    /* tables built by the vmm are already in use; keep them reserved. */
}

static inline ULONG table_index(ULONG_PTR va, int level)
{
    return (ULONG)((va >> (39 - 9 * level)) & 0x1FF);
}

ULONG64 *MiWalk(ULONG64 *root, ULONG_PTR va, BOOLEAN create, int level)
{
    ULONG64 *table = root;
    for (int l = 0; l < level; l++) {
        ULONG64 *entry = &table[table_index(va, l)];
        if (!(*entry & MM_PTE_VALID)) {
            if (!create)
                return nullptr;
            ULONG64 pa = MmAllocatePage();
            if (!pa)
                return nullptr;
            PMMPFN pfn = MiGetPfn(pa);
            if (pfn)
                pfn->Flags |= MM_PFN_PAGE_TABLE;
            KiDataSyncBarrier();
            *entry = pa | MM_PTE_VALID | MM_PTE_TABLE;
        } else if (!(*entry & MM_PTE_TABLE)) {
            return nullptr; /* block mapping, cannot descend */
        }
        table = (ULONG64 *)HalPhysmap(*entry & MM_PTE_ADDR_MASK);
    }
    return &table[table_index(va, level)];
}

void MiFlushTlbPage(ULONG_PTR va, USHORT asid, BOOLEAN global)
{
    ULONG64 arg = ((va >> 12) & 0xFFFFFFFFFFFull) | ((ULONG64)asid << 48);
    KiDataSyncBarrier();
    if (global)
        __asm__ volatile("tlbi vaae1is, %0" ::"r"(arg) : "memory");
    else
        __asm__ volatile("tlbi vae1is, %0" ::"r"(arg) : "memory");
    KiDataSyncBarrier();
    KiInstructionBarrier();
}

void MiFlushTlbRange(ULONG_PTR va, SIZE_T pages, USHORT asid)
{
    if (pages > 64) {
        MiFlushTlbAsid(asid);
        return;
    }
    BOOLEAN global = va >= 0xFFFF000000000000ull;
    KiDataSyncBarrier();
    for (SIZE_T i = 0; i < pages; i++) {
        ULONG64 arg = (((va >> 12) + i) & 0xFFFFFFFFFFFull) | ((ULONG64)asid << 48);
        if (global)
            __asm__ volatile("tlbi vaae1is, %0" ::"r"(arg) : "memory");
        else
            __asm__ volatile("tlbi vae1is, %0" ::"r"(arg) : "memory");
    }
    KiDataSyncBarrier();
    KiInstructionBarrier();
}

void MiFlushTlbAsid(USHORT asid)
{
    ULONG64 arg = (ULONG64)asid << 48;
    KiDataSyncBarrier();
    __asm__ volatile("tlbi aside1is, %0" ::"r"(arg) : "memory");
    KiDataSyncBarrier();
    KiInstructionBarrier();
}

void MiFlushTlbAll(void)
{
    KiDataSyncBarrier();
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    KiDataSyncBarrier();
    KiInstructionBarrier();
}

ULONG64 MiReadPte(ULONG64 *root, ULONG_PTR va)
{
    ULONG64 *pte = MiWalk(root, va, FALSE, 3);
    return pte ? *pte : 0;
}

/* break-before-make replacement of a translation. */
void MiWritePte(ULONG64 *root, ULONG_PTR va, ULONG64 value, USHORT asid)
{
    ULONG64 *pte = MiWalk(root, va, TRUE, 3);
    if (!pte)
        KeBugCheckEx(0x0000001A, va, 0, 0, 0); /* MEMORY_MANAGEMENT */
    ULONG64 old = *pte;
    BOOLEAN global = va >= 0xFFFF000000000000ull;
    if (old & MM_PTE_VALID) {
        *pte = 0;
        MiFlushTlbPage(va, asid, global);
    }
    KiDataSyncBarrier();
    *pte = value;
    KiDataSyncBarrier();
    if (value & MM_PTE_VALID)
        KiInstructionBarrier();
}

BOOLEAN MiMapPage(ULONG64 *root, ULONG_PTR va, ULONG64 pa, ULONG64 attrs)
{
    ULONG64 *pte = MiWalk(root, va, TRUE, 3);
    if (!pte)
        return FALSE;
    BOOLEAN global = va >= 0xFFFF000000000000ull;
    if (*pte & MM_PTE_VALID) {
        *pte = 0;
        MiFlushTlbPage(va, 0, global);
    }
    KiDataSyncBarrier();
    *pte = (pa & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    KiDataSyncBarrier();
    KiInstructionBarrier();
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
        MiFlushTlbPage(va, asid, va >= 0xFFFF000000000000ull);
    return old;
}

/* break-before-make for a run of ptes: the valid ones are cleared and invalidated together, then all are stored. */
void MiRewritePtes(ULONG64 *pte, ULONG_PTR va, SIZE_T count, const ULONG64 *values, USHORT asid)
{
    SIZE_T first = count, last = 0;
    for (SIZE_T i = 0; i < count; i++) {
        if (pte[i] & MM_PTE_VALID) {
            if (first == count)
                first = i;
            last = i;
            pte[i] = 0;
        }
    }
    if (first != count)
        MiFlushTlbRange(va + (first << PAGE_SHIFT), last - first + 1, asid);
    KiDataSyncBarrier();
    BOOLEAN valid = FALSE;
    for (SIZE_T i = 0; i < count; i++) {
        pte[i] = values[i];
        valid |= (values[i] & MM_PTE_VALID) != 0;
    }
    KiDataSyncBarrier();
    if (valid)
        KiInstructionBarrier();
}

/* clears a run of ptes, handing back the old entries once the translations are gone. */
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

/* windows page protection to a user or kernel page descriptor. */
ULONG64 MiProtectToPte(ULONG protect, BOOLEAN user)
{
    ULONG64 pte = MM_PTE_ATTR(0) | MM_PTE_SH_INNER | MM_PTE_AF;
    if (user)
        pte |= MM_PTE_NG;
    ULONG base = protect & 0xFF;
    BOOLEAN exec = base & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    BOOLEAN write = base & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE);
    BOOLEAN cow = base & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY);
    if (user) {
        pte |= MM_PTE_PXN;
        if (!exec)
            pte |= MM_PTE_UXN;
        pte |= write ? MM_PTE_AP_EL0 : (MM_PTE_AP_EL0 | MM_PTE_AP_RO);
    } else {
        pte |= MM_PTE_UXN;
        if (!exec)
            pte |= MM_PTE_PXN;
        if (!write)
            pte |= MM_PTE_AP_RO;
    }
    if (cow)
        pte |= MM_PTE_SW_COW;
    if (protect & PAGE_NOCACHE)
        pte = (pte & ~MM_PTE_ATTR(7)) | MM_PTE_ATTR(2);
    /* the page and its data stay, out of reach of user mode. */
    if (user && ((protect & 0xFF) == PAGE_NOACCESS || (protect & PAGE_GUARD))) {
        pte &= ~MM_PTE_AP_EL0;
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
    BOOLEAN user = (pte & MM_PTE_AP_EL0) != 0;
    BOOLEAN ro = (pte & MM_PTE_AP_RO) != 0;
    /* a user page out of reach of user mode keeps its user execute permission. */
    BOOLEAN exec = (user || (pte & MM_PTE_NG)) ? !(pte & MM_PTE_UXN) : !(pte & MM_PTE_PXN);
    ULONG protect;
    if (pte & MM_PTE_SW_COW)
        protect = exec ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY;
    else if (exec)
        protect = ro ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE;
    else
        protect = ro ? PAGE_READONLY : PAGE_READWRITE;
    if (!user && (pte & MM_PTE_NG))
        return (pte & MM_PTE_SW_GUARD) ? protect | PAGE_GUARD : PAGE_NOACCESS;
    return protect;
}

BOOLEAN NTAPI MmIsAddressValid(PVOID va)
{
    ULONG_PTR v = (ULONG_PTR)va;
    if (v >= MIME_KVA_PHYSMAP && v < MIME_KVA_PHYSMAP + HalBootInfo->ram_base + HalBootInfo->ram_size)
        return TRUE;
    ULONG64 *root = v >= 0xFFFF000000000000ull ? g_kernel_root
                                               : (ULONG64 *)HalPhysmap(KI_READ_SYSREG(ttbr0_el1) & MM_PTE_ADDR_MASK);
    ULONG64 *pte = MiWalk(root, v, FALSE, 3);
    return pte && (*pte & MM_PTE_VALID);
}

/* ttbr0 of the system process: user space is empty, the kernel lives in ttbr1. */
ULONG64 MiCreateSystemRoot(void)
{
    return MmAllocatePage();
}

void MiInitializeProcessRoot(ULONG64 *root)
{
}

ULONG64 MiDirectoryTableBase(PMMADDRESS_SPACE as)
{
    return as->RootPhys | ((ULONG64)as->Asid << 48);
}
