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
static ULONG g_discard_min_order = 4; /* 64 KiB blocks, or the host's memory block */
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

/*
 * a block the host was handed back holds zeros and costs no host memory:
 * it stays so while its buddies merge only with blocks like it, and its
 * parts are still zero after a split. such blocks wait at the tail of the
 * free lists, so allocations reuse memory the host already backs.
 */
static void insert_free(PMMPFN p, ULONG order, BOOLEAN discarded)
{
    p->Flags = MM_PFN_FREE | (discarded ? MM_PFN_DISCARDED : 0);
    p->Order = (UCHAR)order;
    if (discarded)
        InsertTailList(&g_free[order], &p->ListEntry);
    else
        InsertHeadList(&g_free[order], &p->ListEntry);
}

static void free_block(ULONG64 index, ULONG order, BOOLEAN discarded)
{
    while (order < MM_MAX_ORDER) {
        ULONG64 buddy = index ^ (1ull << order);
        if (buddy + (1ull << order) > g_ram_pages)
            break;
        PMMPFN b = &g_pfn[buddy];
        if (!(b->Flags & MM_PFN_FREE) || b->Order != order)
            break;
        discarded = discarded && (b->Flags & MM_PFN_DISCARDED);
        RemoveEntryList(&b->ListEntry);
        b->Flags = 0;
        index &= ~(1ull << order);
        order++;
    }
    insert_free(&g_pfn[index], order, discarded);
}

/* returns whether the block is known to hold zeros. */
static BOOLEAN alloc_block(ULONG order, ULONG64 *out_index, BOOLEAN *zeroed)
{
    ULONG o = order;
    while (o <= MM_MAX_ORDER && IsListEmpty(&g_free[o]))
        o++;
    if (o > MM_MAX_ORDER)
        return FALSE;
    PMMPFN p = CONTAINING_RECORD(RemoveHeadList(&g_free[o]), MMPFN, ListEntry);
    ULONG64 index = (ULONG64)(p - g_pfn);
    BOOLEAN discarded = (p->Flags & MM_PFN_DISCARDED) != 0;
    p->Flags = 0;
    while (o > order) {
        o--;
        insert_free(&g_pfn[index + (1ull << o)], o, discarded);
    }
    *out_index = index;
    *zeroed = discarded;
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
 * are skipped. a pass hands back a bounded amount, so the kernel lock is
 * never held long; the rest waits for the next pass. a block larger than
 * a pass goes alone, when the machine idles.
 */
#define DISCARD_PASS_BYTES (64ull << 20)

static void discard_free(BOOLEAN idle)
{
    ULONG64 budget = DISCARD_PASS_BYTES;
    for (ULONG order = g_discard_min_order; order <= MM_MAX_ORDER; order++) {
        for (PLIST_ENTRY e = g_free[order].Flink, next; e != &g_free[order]; e = next) {
            next = e->Flink;
            PMMPFN p = CONTAINING_RECORD(e, MMPFN, ListEntry);
            if (p->Flags & MM_PFN_DISCARDED)
                continue;
            ULONG64 bytes = (1ull << order) << PAGE_SHIFT;
            if (bytes > budget && !(idle && budget == DISCARD_PASS_BYTES)) {
                /* the rest goes on the next idle pass, not at every free until then. */
                g_freed_since_discard = DISCARD_IDLE;
                return;
            }
            budget -= (bytes < budget ? bytes : budget);
            RemoveEntryList(&p->ListEntry);
            insert_free(p, order, TRUE);
            HalHypercall(MIME_HC_MEM_DISCARD, index_to_pa((ULONG64)(p - g_pfn)), bytes, 0, 0, 0, 0, NULL);
        }
    }
    g_freed_since_discard = 0;
}

static void note_freed(SIZE_T pages)
{
    g_freed_since_discard += pages;
    if (g_freed_since_discard >= DISCARD_BATCH)
        discard_free(FALSE);
}

void MmDiscardIdle(void)
{
    if (g_freed_since_discard >= DISCARD_IDLE)
        discard_free(TRUE);
}

static ULONG64 allocate_page(BOOLEAN *zeroed)
{
    ULONG64 state = KiDisableInterrupts();
    ULONG64 index;
    BOOLEAN ok = alloc_block(0, &index, zeroed);
    if (ok) {
        mark_allocated(index, 1);
        g_free_pages--;
    }
    KiRestoreInterrupts(state);
    return ok ? index_to_pa(index) : 0;
}

ULONG64 MmAllocatePageNoZero(void)
{
    BOOLEAN zeroed;
    return allocate_page(&zeroed);
}

ULONG64 MmAllocatePage(void)
{
    BOOLEAN zeroed;
    ULONG64 pa = allocate_page(&zeroed);
    if (pa && !zeroed)
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
    free_block((ULONG64)(p - g_pfn), 0, FALSE);
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
    BOOLEAN zeroed;
    BOOLEAN ok = alloc_block(order, &index, &zeroed);
    if (ok) {
        /* give back the tail that exceeds the request. */
        for (ULONG64 i = pages; i < (1ull << order); i++)
            free_block(index + i, 0, zeroed);
        mark_allocated(index, pages);
        g_free_pages -= pages;
    }
    KiRestoreInterrupts(state);
    if (!ok)
        return 0;
    ULONG64 pa = index_to_pa(index);
    if (!zeroed)
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
    /* entries are written sparsely, one per free block, which huge pages would each commit whole. */
    HalHypercall(MIME_HC_MEM_ADVISE, db_phys, db_bytes, MIME_MEM_SPARSE, 0, 0, 0, NULL);
    if (boot->memory_block_shift > PAGE_SHIFT + 4 && boot->memory_block_shift - PAGE_SHIFT <= MM_MAX_ORDER)
        g_discard_min_order = boot->memory_block_shift - PAGE_SHIFT;
    ULONG64 start = (db_phys + db_bytes - g_ram_base) >> PAGE_SHIFT;
    for (ULONG64 i = 0; i < start; i++)
        g_pfn[i].Flags = MM_PFN_RESERVED;
    /* the host has never been given this memory: it is all zeros. */
    for (ULONG64 i = start; i < g_ram_pages;) {
        ULONG order = MM_MAX_ORDER;
        while (order && ((i & ((1ull << order) - 1)) || i + (1ull << order) > g_ram_pages))
            order--;
        free_block(i, order, TRUE);
        g_free_pages += 1ull << order;
        i += 1ull << order;
    }
    g_total_pages = g_ram_pages;
    KiLogPrintf("mm: %llu MiB free, pfn database %llu KiB", (ULONG64)(g_free_pages >> 8), db_bytes >> 10);

    void MiInitializeKernelTables(MimeBootInfo * boot);
    MiInitializeKernelTables(boot);
}
