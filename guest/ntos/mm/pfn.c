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
static volatile SIZE_T g_freed_since_discard;
static BOOLEAN g_discard_enabled = TRUE; /* the kernel option "nodiscard" keeps freed memory in the guest, for measuring */
static ULONG g_discard_min_order = 4; /* 64 KiB blocks, or the host's memory block */
#define DISCARD_BATCH (16ull << 8) /* pages freed between discard passes, 16 MiB */
#define DISCARD_IDLE (1ull << 8)   /* pages freed before the machine idles, 1 MiB */

/*
 * the free lists and the counters are guarded by a ticket lock taken with interrupts off. page faults
 * of user mode run without the kernel lock (MmFastFault), so the lock is real: it is the innermost
 * one, taken for a few list operations and never held across anything that waits.
 */
static volatile ULONG g_pfn_next, g_pfn_serving;

static ULONG64 pfn_lock(void)
{
    ULONG64 state = KiDisableInterrupts();
    ULONG ticket = __atomic_fetch_add(&g_pfn_next, 1, __ATOMIC_ACQUIRE);
    while (__atomic_load_n(&g_pfn_serving, __ATOMIC_ACQUIRE) != ticket)
        KiPause();
    return state;
}

static void pfn_unlock(ULONG64 state)
{
    __atomic_store_n(&g_pfn_serving, g_pfn_serving + 1, __ATOMIC_RELEASE);
    KiRestoreInterrupts(state);
}

PMMPFN MiGetPfn(ULONG64 pa)
{
    ULONG64 index = (pa - g_ram_base) >> PAGE_SHIFT;
    if (pa < g_ram_base || index >= g_ram_pages)
        return nullptr;
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
 * their memory until the guest writes them again. a block is taken off its
 * list while the host works on it, so the lock is not held meanwhile and
 * no page changes hands under it; blocks already handed back are skipped.
 * a pass hands back a bounded amount, so the kernel lock is never held
 * long; the rest waits for the next pass. a block larger than a pass goes
 * alone, when the machine idles.
 */
#define DISCARD_PASS_BYTES (64ull << 20)

static void discard_free(BOOLEAN idle)
{
    if (!g_discard_enabled)
        return;
    ULONG64 budget = DISCARD_PASS_BYTES;
    for (ULONG order = g_discard_min_order; order <= MM_MAX_ORDER; order++) {
        for (;;) {
            ULONG64 state = pfn_lock();
            PMMPFN p = nullptr;
            /* blocks that were not handed back yet wait at the head, those that were at the tail. */
            if (!IsListEmpty(&g_free[order])) {
                p = CONTAINING_RECORD(g_free[order].Flink, MMPFN, ListEntry);
                if (p->Flags & MM_PFN_DISCARDED)
                    p = nullptr;
            }
            ULONG64 bytes = (1ull << order) << PAGE_SHIFT;
            if (p && bytes > budget && !(idle && budget == DISCARD_PASS_BYTES)) {
                /* the rest goes on the next idle pass, not at every free until then. */
                g_freed_since_discard = DISCARD_IDLE;
                pfn_unlock(state);
                return;
            }
            if (!p) {
                pfn_unlock(state);
                break;
            }
            budget -= (bytes < budget ? bytes : budget);
            RemoveEntryList(&p->ListEntry);
            p->Flags = 0;
            ULONG64 index = (ULONG64)(p - g_pfn);
            pfn_unlock(state);
            HalHypercall(MIME_HC_MEM_DISCARD, index_to_pa(index), bytes, 0, 0, 0, 0, nullptr);
            state = pfn_lock();
            free_block(index, order, TRUE);
            pfn_unlock(state);
        }
    }
    __atomic_store_n(&g_freed_since_discard, 0, __ATOMIC_RELAXED);
}

void MmDiscardIdle(void)
{
    if (g_freed_since_discard >= DISCARD_IDLE)
        discard_free(TRUE);
}

/*
 * a page of zeros, written past the caches where the processor allows it: the program that gets it reads
 * little of it at first, and filling a page the usual way first reads it from memory.
 */
static void zero_page(void *page)
{
#ifdef __x86_64__
    ULONG lines = PAGE_SIZE / 64;
    __asm__ volatile("pxor %%xmm0, %%xmm0\n"
                     "1:\n"
                     "movntdq %%xmm0, (%0)\n"
                     "movntdq %%xmm0, 16(%0)\n"
                     "movntdq %%xmm0, 32(%0)\n"
                     "movntdq %%xmm0, 48(%0)\n"
                     "addq $64, %0\n"
                     "decl %1\n"
                     "jnz 1b\n"
                     "sfence\n"
                     : "+r"(page), "+r"(lines)
                     :
                     : "xmm0", "memory", "cc");
#else
    memset(page, 0, PAGE_SIZE);
#endif
}

/*
 * each processor keeps a few pages of its own, handed out without any lock: a fault wants pages
 * a few at a time, and one lock that every processor takes for each of them was where the faults
 * of a process stopped adding up. a cache is filled and emptied by whole runs of the free lists,
 * and the processor that owns it holds Busy while it uses it, which only another processor
 * emptying it for memory that ran short can find taken. a page in a cache counts as free; the
 * lowest bit of its address says whether the host is known to have left it zero.
 */
#define CACHE_RUN 32
#define CACHE_MAX 96
#define CACHE_ZERO 1ull

typedef struct DECLSPEC_ALIGN(64) _MM_PAGE_CACHE {
    volatile LONG Busy;
    ULONG Count;
    ULONG64 Pages[CACHE_MAX];
} MM_PAGE_CACHE;

static MM_PAGE_CACHE g_cache[MAXIMUM_PROCESSORS];

static void cache_lock(MM_PAGE_CACHE *c)
{
    while (__atomic_exchange_n(&c->Busy, 1, __ATOMIC_ACQUIRE))
        KiPause();
}

static void cache_unlock(MM_PAGE_CACHE *c)
{
    __atomic_store_n(&c->Busy, 0, __ATOMIC_RELEASE);
}

/* takes up to CACHE_RUN pages off the free lists, in runs as long as they are found. the pfn lock is held. */
static ULONG take_run(ULONG64 *pages)
{
    ULONG got = 0;
    for (int order = 5; order >= 0 && got < CACHE_RUN;) {
        ULONG64 index;
        BOOLEAN zero;
        if (got + (1u << order) > CACHE_RUN || !alloc_block((ULONG)order, &index, &zero)) {
            order--;
            continue;
        }
        mark_allocated(index, 1ull << order);
        for (ULONG64 i = 0; i < (1ull << order); i++)
            pages[got++] = index_to_pa(index + i) | (zero ? CACHE_ZERO : 0);
    }
    g_free_pages -= got;
    return got;
}

/* gives pages back to the free lists. the pfn lock is held. */
static void return_run(const ULONG64 *pages, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        ULONG64 pa = pages[i] & ~CACHE_ZERO;
        PMMPFN p = MiGetPfn(pa);
        free_block((ULONG64)(p - g_pfn), 0, (pages[i] & CACHE_ZERO) != 0);
    }
    g_free_pages += count;
}

/* the other processors' caches back to the lists, for an allocation that found them empty. */
static void drain_caches(void)
{
    PKPRCB self = KeGetCurrentPrcb();
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        MM_PAGE_CACHE *c = &g_cache[i];
        if (i == self->Number || __atomic_exchange_n(&c->Busy, 1, __ATOMIC_ACQUIRE))
            continue;
        ULONG64 state = pfn_lock();
        return_run(c->Pages, c->Count);
        c->Count = 0;
        pfn_unlock(state);
        cache_unlock(c);
    }
}

/* a page of this processor's cache, refilled from the lists when it is empty. interrupts are off. */
static ULONG64 cache_pop(MM_PAGE_CACHE *c)
{
    if (!c->Count) {
        ULONG64 state = pfn_lock();
        c->Count = take_run(c->Pages);
        pfn_unlock(state);
        if (!c->Count) {
            /* the lists are empty: the caches of the others may hold what is left. */
            cache_unlock(c);
            drain_caches();
            cache_lock(c);
            state = pfn_lock();
            c->Count = take_run(c->Pages);
            pfn_unlock(state);
            if (!c->Count)
                return 0;
        }
    }
    return c->Pages[--c->Count];
}

/* up to count pages, one lock held for them all, none cleared. returns how many; flags[i] tells that page i is zero. */
static ULONG allocate_pages(ULONG count, ULONG64 *pages, BOOLEAN *zero)
{
    ULONG64 state = KiDisableInterrupts();
    MM_PAGE_CACHE *c = &g_cache[KeGetCurrentPrcb()->Number];
    cache_lock(c);
    ULONG got = 0;
    while (got < count) {
        ULONG64 entry = cache_pop(c);
        if (!entry)
            break;
        pages[got] = entry & ~CACHE_ZERO;
        zero[got] = (entry & CACHE_ZERO) != 0;
        got++;
    }
    cache_unlock(c);
    KiRestoreInterrupts(state);
    return got;
}

ULONG64 MmAllocatePageNoZero(void)
{
    ULONG64 pa;
    BOOLEAN zero;
    return allocate_pages(1, &pa, &zero) ? pa : 0;
}

ULONG64 MmAllocatePage(void)
{
    ULONG64 pa;
    BOOLEAN zero;
    if (!allocate_pages(1, &pa, &zero))
        return 0;
    if (!zero)
        zero_page(HalPhysmap(pa));
    return pa;
}

/* up to count pages, each zeroed, as a fault wants them. returns how many it found. */
ULONG MmAllocateZeroedPages(ULONG count, ULONG64 *pages)
{
    BOOLEAN zero[MM_BATCH_PAGES];
    if (count > MM_BATCH_PAGES)
        count = MM_BATCH_PAGES;
    ULONG got = allocate_pages(count, pages, zero);
    for (ULONG i = 0; i < got; i++)
        if (!zero[i])
            zero_page(HalPhysmap(pages[i]));
    return got;
}

/*
 * frees pages without looking at how much has been freed, for the paths that run without the kernel
 * lock. a page that is known to hold zeros can say so.
 */
static void free_pages(const ULONG64 *pages, ULONG count, BOOLEAN zero)
{
    ULONG64 state = KiDisableInterrupts();
    MM_PAGE_CACHE *c = &g_cache[KeGetCurrentPrcb()->Number];
    cache_lock(c);
    for (ULONG i = 0; i < count; i++) {
        PMMPFN p = MiGetPfn(pages[i]);
        if (!p || (p->Flags & MM_PFN_FREE)) {
            KiLogPrintf("MmFreePage: bad page %llx", pages[i]);
            continue;
        }
        if (c->Count == CACHE_MAX) {
            /* the oldest half goes back to the lists. */
            ULONG64 lock = pfn_lock();
            return_run(c->Pages, CACHE_MAX / 2);
            pfn_unlock(lock);
            memmove(c->Pages, c->Pages + CACHE_MAX / 2, (CACHE_MAX - CACHE_MAX / 2) * sizeof(ULONG64));
            c->Count = CACHE_MAX - CACHE_MAX / 2;
        }
        c->Pages[c->Count++] = pages[i] | (zero ? CACHE_ZERO : 0);
    }
    cache_unlock(c);
    KiRestoreInterrupts(state);
    __atomic_add_fetch(&g_freed_since_discard, count, __ATOMIC_RELAXED);
}

void MmFreePagesQuiet(const ULONG64 *pages, ULONG count)
{
    free_pages(pages, count, FALSE);
}

/* pages taken for a fault and not used: they are still zero. */
void MmFreeZeroedPagesQuiet(const ULONG64 *pages, ULONG count)
{
    free_pages(pages, count, TRUE);
}

void MmFreePages(const ULONG64 *pages, ULONG count)
{
    free_pages(pages, count, FALSE);
    if (g_freed_since_discard >= DISCARD_BATCH)
        discard_free(FALSE);
}

void MmFreePage(ULONG64 pa)
{
    MmFreePages(&pa, 1);
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
    ULONG64 state = pfn_lock();
    ULONG64 index;
    BOOLEAN zeroed;
    BOOLEAN ok = alloc_block(order, &index, &zeroed);
    if (!ok) {
        /* the caches of the processors may hold what keeps a run from forming. */
        pfn_unlock(state);
        drain_caches();
        state = pfn_lock();
        ok = alloc_block(order, &index, &zeroed);
    }
    if (ok) {
        /* give back the tail that exceeds the request. */
        for (ULONG64 i = pages; i < (1ull << order); i++)
            free_block(index + i, 0, zeroed);
        mark_allocated(index, pages);
        g_free_pages -= pages;
    }
    pfn_unlock(state);
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
    SIZE_T pages = __atomic_load_n(&g_free_pages, __ATOMIC_RELAXED);
    for (ULONG i = 0; i < KeNumberProcessors; i++)
        pages += g_cache[i].Count;
    return pages;
}

SIZE_T MmGetTotalPages(void)
{
    return g_total_pages;
}

PVOID NTAPI MmAllocateContiguousPages(SIZE_T bytes)
{
    ULONG64 pa = MmAllocateContiguous(BYTES_TO_PAGES(bytes));
    return pa ? HalPhysmap(pa) : nullptr;
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
    HalHypercall(MIME_HC_MEM_ADVISE, db_phys, db_bytes, MIME_MEM_SPARSE, 0, 0, 0, nullptr);
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
    g_discard_enabled = !strstr(boot->cmdline, "nodiscard");
    KiLogPrintf("mm: %llu MiB free, pfn database %llu KiB", (ULONG64)(g_free_pages >> 8), db_bytes >> 10);

    void MiInitializeKernelTables(MimeBootInfo * boot);
    MiInitializeKernelTables(boot);
}
