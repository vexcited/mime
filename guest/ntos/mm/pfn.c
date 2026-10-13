/*
 * physical memory: pfn database and buddy allocator.
 */
#include "../ntos.h"

#define MM_MAX_ORDER 16 /* 256 MiB runs, for window surfaces of large displays */

static PMMPFN g_pfn;
static ULONG64 g_ram_base;
static ULONG64 g_ram_pages;
static LIST_ENTRY g_free[MM_MAX_ORDER + 1];
static SIZE_T g_free_pages;
static SIZE_T g_total_pages;
static SIZE_T g_freed_since_discard;

#define DISCARD_MIN_ORDER 4         /* 64 KiB blocks, four host pages */
#define DISCARD_BATCH (16ull << 8) /* pages freed between discard passes, 16 MiB */
#define DISCARD_IDLE (1ull << 8)   /* pages freed before the machine idles, 1 MiB */

PMMPFN MiGetPfn(ULONG64 pa)
{
    ULONG64 index = (pa - g_ram_base) >> PAGE_SHIFT;
    if (pa < g_ram_base || index >= g_ram_pages)
        return NULL;
    return &g_pfn[index];
}

static ULONG64 index_to_pa(ULONG64 index)
{
    return g_ram_base + (index << PAGE_SHIFT);
}

static void free_block(ULONG64 index, ULONG order)
{
    while (order < MM_MAX_ORDER) {
        ULONG64 buddy = index ^ (1ull << order);
        if (buddy + (1ull << order) > g_ram_pages)
            break;
        PMMPFN b = &g_pfn[buddy];
        if (!(b->Flags & MM_PFN_FREE) || b->Order != order)
            break;
        RemoveEntryList(&b->ListEntry);
        b->Flags = 0;
        index &= ~(1ull << order);
        order++;
    }
    PMMPFN p = &g_pfn[index];
    p->Flags = MM_PFN_FREE;
    p->Order = (UCHAR)order;
    InsertHeadList(&g_free[order], &p->ListEntry);
}

static BOOLEAN alloc_block(ULONG order, ULONG64 *out_index)
{
    ULONG o = order;
    while (o <= MM_MAX_ORDER && IsListEmpty(&g_free[o]))
        o++;
    if (o > MM_MAX_ORDER)
        return FALSE;
    PMMPFN p = CONTAINING_RECORD(RemoveHeadList(&g_free[o]), MMPFN, ListEntry);
    ULONG64 index = (ULONG64)(p - g_pfn);
    p->Flags = 0;
    while (o > order) {
        o--;
        ULONG64 upper = index + (1ull << o);
        PMMPFN u = &g_pfn[upper];
        u->Flags = MM_PFN_FREE;
        u->Order = (UCHAR)o;
        InsertHeadList(&g_free[o], &u->ListEntry);
    }
    *out_index = index;
    return TRUE;
}

static void mark_allocated(ULONG64 index, ULONG64 count)
{
    for (ULONG64 i = 0; i < count; i++) {
        PMMPFN p = &g_pfn[index + i];
        p->Flags = MM_PFN_ALLOCATED;
        p->Order = 0;
        p->RefCount = 1;
        p->ShareCount = 0;
    }
}

/*
 * hands the free blocks of 64 KiB and more back to the host, which drops
 * their memory until the guest writes them again. called with interrupts
 * disabled, so no page changes hands meanwhile; blocks already handed back
 * are skipped.
 */
static void discard_free(void)
{
    g_freed_since_discard = 0;
    for (ULONG order = DISCARD_MIN_ORDER; order <= MM_MAX_ORDER; order++) {
        for (PLIST_ENTRY e = g_free[order].Flink; e != &g_free[order]; e = e->Flink) {
            PMMPFN p = CONTAINING_RECORD(e, MMPFN, ListEntry);
            if (p->Flags & MM_PFN_DISCARDED)
                continue;
            p->Flags |= MM_PFN_DISCARDED;
            HalHypercall(MIME_HC_MEM_DISCARD, index_to_pa((ULONG64)(p - g_pfn)), (1ull << order) << PAGE_SHIFT, 0, 0, 0,
                         0, NULL);
        }
    }
}

static void note_freed(SIZE_T pages)
{
    g_freed_since_discard += pages;
    if (g_freed_since_discard >= DISCARD_BATCH)
        discard_free();
}

void MmDiscardIdle(void)
{
    if (g_freed_since_discard >= DISCARD_IDLE)
        discard_free();
}

ULONG64 MmAllocatePageNoZero(void)
{
    ULONG64 state = KiDisableInterrupts();
    ULONG64 index;
    BOOLEAN ok = alloc_block(0, &index);
    if (ok) {
        mark_allocated(index, 1);
        g_free_pages--;
    }
    KiRestoreInterrupts(state);
    return ok ? index_to_pa(index) : 0;
}

ULONG64 MmAllocatePage(void)
{
    ULONG64 pa = MmAllocatePageNoZero();
    if (pa)
        memset(HalPhysmap(pa), 0, PAGE_SIZE);
    return pa;
}

void MmFreePage(ULONG64 pa)
{
    PMMPFN p = MiGetPfn(pa);
    if (!p || (p->Flags & MM_PFN_FREE)) {
        KiLogPrintf("MmFreePage: bad page %llx", pa);
        return;
    }
    ULONG64 state = KiDisableInterrupts();
    free_block((ULONG64)(p - g_pfn), 0);
    g_free_pages++;
    note_freed(1);
    KiRestoreInterrupts(state);
}

ULONG64 MmAllocateContiguous(SIZE_T pages)
{
    if (pages == 0)
        return 0;
    ULONG order = 0;
    while ((1ull << order) < pages)
        order++;
    if (order > MM_MAX_ORDER)
        return 0;
    ULONG64 state = KiDisableInterrupts();
    ULONG64 index;
    BOOLEAN ok = alloc_block(order, &index);
    if (ok) {
        /* give back the tail that exceeds the request. */
        for (ULONG64 i = pages; i < (1ull << order); i++)
            free_block(index + i, 0);
        mark_allocated(index, pages);
        g_free_pages -= pages;
    }
    KiRestoreInterrupts(state);
    if (!ok)
        return 0;
    ULONG64 pa = index_to_pa(index);
    memset(HalPhysmap(pa), 0, pages << PAGE_SHIFT);
    return pa;
}

void MmFreeContiguous(ULONG64 pa, SIZE_T pages)
{
    for (SIZE_T i = 0; i < pages; i++)
        MmFreePage(pa + (i << PAGE_SHIFT));
}

SIZE_T MmGetFreePages(void)
{
    return g_free_pages;
}

SIZE_T MmGetTotalPages(void)
{
    return g_total_pages;
}

PVOID NTAPI MmAllocateContiguousPages(SIZE_T bytes)
{
    ULONG64 pa = MmAllocateContiguous(BYTES_TO_PAGES(bytes));
    return pa ? HalPhysmap(pa) : NULL;
}

void NTAPI MmFreeContiguousPages(PVOID va, SIZE_T bytes)
{
    if (va)
        MmFreeContiguous(HalPhysicalOf(va), BYTES_TO_PAGES(bytes));
}

PVOID NTAPI MmAllocateContiguousMemory(SIZE_T size, PHYSICAL_ADDRESS highest)
{
    /* pool allocations above 2 KiB are page aligned physically contiguous runs. */
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size < 4096 ? 4096 : size, KI_TAG('M', 'm', 'C', 'm'));
}

void NTAPI MmFreeContiguousMemory(PVOID va)
{
    ExFreePool(va);
}

LARGE_INTEGER NTAPI MmGetPhysicalAddress(PVOID va)
{
    LARGE_INTEGER r;
    if ((ULONG_PTR)va >= MIME_KVA_SYSTEM && (ULONG_PTR)va < MIME_KVA_SYSTEM + MIME_KVA_SYSTEM_SIZE) {
        r.QuadPart = (LONGLONG)((MiReadPte(MiKernelRoot(), (ULONG_PTR)va) & MM_PTE_ADDR_MASK) + ((ULONG_PTR)va & 0xFFF));
        return r;
    }
    r.QuadPart = (LONGLONG)HalPhysicalOf(va);
    return r;
}

void MmInitialize(MimeBootInfo *boot)
{
    g_ram_base = boot->ram_base;
    g_ram_pages = boot->ram_size >> PAGE_SHIFT;
    for (int i = 0; i <= MM_MAX_ORDER; i++)
        InitializeListHead(&g_free[i]);

    /*
     * the pfn database lives right after everything the loader used. the host
     * hands over zeroed ram, so the database starts zeroed without touching
     * it: entries are only written as their pages are used, and the free
     * memory is released as the largest aligned blocks.
     */
    ULONG64 db_phys = boot->first_free_phys;
    ULONG64 db_bytes = ROUND_TO_PAGES(g_ram_pages * sizeof(MMPFN));
    g_pfn = (PMMPFN)HalPhysmap(db_phys);
    ULONG64 start = (db_phys + db_bytes - g_ram_base) >> PAGE_SHIFT;
    for (ULONG64 i = 0; i < start; i++)
        g_pfn[i].Flags = MM_PFN_RESERVED;
    for (ULONG64 i = start; i < g_ram_pages;) {
        ULONG order = MM_MAX_ORDER;
        while (order && ((i & ((1ull << order) - 1)) || i + (1ull << order) > g_ram_pages))
            order--;
        free_block(i, order);
        g_free_pages += 1ull << order;
        i += 1ull << order;
    }
    /* the host has never been given this memory. */
    for (ULONG order = 0; order <= MM_MAX_ORDER; order++)
        for (PLIST_ENTRY e = g_free[order].Flink; e != &g_free[order]; e = e->Flink)
            CONTAINING_RECORD(e, MMPFN, ListEntry)->Flags |= MM_PFN_DISCARDED;
    g_total_pages = g_ram_pages;
    KiLogPrintf("mm: %llu MiB free, pfn database %llu KiB", (ULONG64)(g_free_pages >> 8), db_bytes >> 10);

    void MiInitializeKernelTables(MimeBootInfo * boot);
    MiInitializeKernelTables(boot);
}
