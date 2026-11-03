/* physical memory: pfn database and buddy allocator. */
#include "../ntos.h"

#define MM_MAX_ORDER 16 /* 256 MiB runs, for the window surfaces of large displays. */

static PMMPFN g_pfn;
static ULONG64 g_ram_base;
static ULONG64 g_ram_pages;
static LIST_ENTRY g_free[MM_MAX_ORDER + 1];
static SIZE_T g_free_pages;
static SIZE_T g_total_pages;
static volatile SIZE_T g_freed_since_discard;
static volatile SIZE_T g_freed_since_sweep; /* pages freed since a trim or sweep handed every free page back. */
static BOOLEAN g_discard_enabled = TRUE; /* the "nodiscard" kernel option keeps freed memory in the guest, for measuring. */
static ULONG g_discard_min_order = 4; /* 64 KiB blocks, or the host's memory block. */
#define DISCARD_BATCH (16ull << 8) /* pages freed between discard passes, 16 MiB. */
#define DISCARD_IDLE (1ull << 8)   /* pages freed before an idle discard, 1 MiB. */

/*
 * ticket lock over the free lists and counters, taken with interrupts off. user page faults run
 * without the kernel lock, see MmFastFault, so it is the innermost lock and never held across a wait.
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
 * a block handed back to the host reads as zeros and costs no host memory. it stays so while it merges
 * only with blocks like it, and its halves stay zero after a split. such blocks sit at the tail of the
 * free lists so allocations reuse memory the host already backs.
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

/* *zeroed tells whether the block is known to hold zeros. */
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
 * hands free blocks back to the host, which drops their memory until the guest writes them again. the
 * blocks of a pass are taken off their lists and sent in one report, 255 runs to a hypercall, so the
 * pfn lock is held only to take and to put back. normally only blocks of 64 KiB and more go, or the
 * host's memory block when it is larger, as a smaller piece would split a huge page of the host for a
 * few kilobytes; a trim goes down to single pages. a block larger than a pass goes alone, when the
 * machine idles, so the kernel lock is never held long.
 */
#define DISCARD_PASS_BYTES (64ull << 20)
#define REPORT_RUNS (PAGE_SIZE / sizeof(MimeReportRun))
#ifdef __x86_64__
#define HOST_PAGE_ORDER 0 /* 4 KiB host pages. */
#else
#define HOST_PAGE_ORDER 2 /* 16 KiB. */
#endif

static MimeReportRun g_report[REPORT_RUNS] __attribute__((aligned(PAGE_SIZE)));
static ULONG64 g_report_index[REPORT_RUNS];
static volatile LONG g_report_busy;
static volatile ULONG64 g_released_bytes;

/* returns the bytes handed back. budget 0 means no limit, unlocked releases the kernel lock during the host's work. */
static ULONG64 report_free(ULONG min_order, ULONG64 limit, BOOLEAN idle, BOOLEAN unlocked)
{
    if (!g_discard_enabled || __atomic_exchange_n(&g_report_busy, 1, __ATOMIC_ACQUIRE))
        return 0;
    ULONG64 budget = limit ? limit : ~0ull, done = 0;
    BOOLEAN left_some = FALSE, refused = FALSE;
    for (ULONG order = min_order; order <= MM_MAX_ORDER && !left_some; order++) {
        ULONG64 bytes = (1ull << order) << PAGE_SHIFT;
        for (;;) {
            ULONG count = 0;
            ULONG64 state = pfn_lock();
            /* blocks not handed back yet sit at the head. */
            while (count < REPORT_RUNS && !IsListEmpty(&g_free[order])) {
                PMMPFN p = CONTAINING_RECORD(g_free[order].Flink, MMPFN, ListEntry);
                if (p->Flags & MM_PFN_DISCARDED)
                    break;
                if (bytes > budget && !(idle && budget == limit && !count)) {
                    /* leave the rest for the next idle pass, not every free until then. */
                    g_freed_since_discard = DISCARD_IDLE;
                    left_some = TRUE;
                    break;
                }
                budget -= (bytes < budget ? bytes : budget);
                RemoveEntryList(&p->ListEntry);
                p->Flags = 0;
                g_report_index[count] = (ULONG64)(p - g_pfn);
                g_report[count] = (MimeReportRun){index_to_pa(g_report_index[count]), bytes};
                count++;
            }
            pfn_unlock(state);
            if (!count)
                break;
            ULONG64 released = 0;
            ULONG64 status = unlocked ? HalHypercallUnlocked(MIME_HC_MEM_REPORT, HalPhysicalOf(g_report), count, 0, 0, 0, 0, &released)
                                      : HalHypercall(MIME_HC_MEM_REPORT, HalPhysicalOf(g_report), count, 0, 0, 0, 0, &released);
            /* a report the host refused leaves the memory as it was, so it must not be taken for zeros. */
            BOOLEAN zero = status == MIME_HC_STATUS_OK;
            state = pfn_lock();
            for (ULONG i = 0; i < count; i++)
                free_block(g_report_index[i], order, zero);
            pfn_unlock(state);
            if (zero)
                done += count * bytes;
            else
                left_some = refused = TRUE; /* the host said no: a retry would find the same blocks again. */
            if (left_some || budget == 0)
                break;
        }
        if (budget == 0)
            break;
    }
    if (!left_some || refused)
        __atomic_store_n(&g_freed_since_discard, 0, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_released_bytes, done, __ATOMIC_RELAXED);
    __atomic_store_n(&g_report_busy, 0, __ATOMIC_RELEASE);
    return done;
}

static void discard_free(BOOLEAN idle)
{
    report_free(g_discard_min_order, DISCARD_PASS_BYTES, idle, FALSE);
}

void MmDiscardIdle(void)
{
    if (g_freed_since_discard >= DISCARD_IDLE)
        discard_free(TRUE);
}

/* non-temporal stores: a normal fill first reads the page from memory, and the new owner touches little of it at first. */
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
 * per processor page caches, so faults on several processors do not contend on the pfn lock. a cache
 * is filled and emptied in runs. its owner holds Busy while using it, so only another processor
 * draining it under memory pressure can find it taken. cached pages count as free, and bit 0 of an
 * entry says the host left the page zero.
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

/* the pfn lock is held. */
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

/* the pfn lock is held. */
static void return_run(const ULONG64 *pages, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        ULONG64 pa = pages[i] & ~CACHE_ZERO;
        PMMPFN p = MiGetPfn(pa);
        free_block((ULONG64)(p - g_pfn), 0, (pages[i] & CACHE_ZERO) != 0);
    }
    g_free_pages += count;
}

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

/* returns the pages the processors keep for their next faults, this one's too. */
static void drain_all_caches(void)
{
    ULONG64 state = KiDisableInterrupts();
    MM_PAGE_CACHE *c = &g_cache[KeGetCurrentPrcb()->Number];
    cache_lock(c);
    ULONG64 lock = pfn_lock();
    return_run(c->Pages, c->Count);
    c->Count = 0;
    pfn_unlock(lock);
    cache_unlock(c);
    KiRestoreInterrupts(state);
    drain_caches();
}

/* interrupts are off. */
static ULONG64 cache_pop(MM_PAGE_CACHE *c)
{
    if (!c->Count) {
        ULONG64 state = pfn_lock();
        c->Count = take_run(c->Pages);
        pfn_unlock(state);
        if (!c->Count) {
            /* the lists are empty, but other processors' caches may not be. */
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

/* pages are not cleared. returns how many it got, and zero[i] says page i is known zero. */
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

/* never triggers a discard pass, for paths that run without the kernel lock. */
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
    __atomic_add_fetch(&g_freed_since_sweep, count, __ATOMIC_RELAXED);
}

/*
 * gives the host every free page, down to single ones, when it asks for memory back. returns the bytes the host
 * dropped now. the caller is a thread below dispatch level, so the kernel lock is let go while the host works.
 */
ULONG64 MmReleaseFreeMemory(void)
{
    __atomic_store_n(&g_freed_since_sweep, 0, __ATOMIC_RELAXED);
    drain_all_caches();
    return report_free(HOST_PAGE_ORDER, 0, FALSE, TRUE);
}

SIZE_T MmFreedSinceSweep(void)
{
    return __atomic_load_n(&g_freed_since_sweep, __ATOMIC_RELAXED);
}

ULONG64 MmGetReleasedBytes(void)
{
    return __atomic_load_n(&g_released_bytes, __ATOMIC_RELAXED);
}

void MmFreePagesQuiet(const ULONG64 *pages, ULONG count)
{
    free_pages(pages, count, FALSE);
}

/* for pages taken for a fault and never used, which are still zero. */
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
        /* cached pages may be what keeps a run from forming. */
        pfn_unlock(state);
        drain_caches();
        state = pfn_lock();
        ok = alloc_block(order, &index, &zeroed);
    }
    if (ok) {
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

    /* the pfn database follows what the loader used. the host's ram starts zeroed, so it needs no clearing. */
    ULONG64 db_phys = boot->first_free_phys;
    ULONG64 db_bytes = ROUND_TO_PAGES(g_ram_pages * sizeof(MMPFN));
    g_pfn = (PMMPFN)HalPhysmap(db_phys);
    /* entries are written sparsely, one per free block, and huge pages would commit each one whole. */
    HalHypercall(MIME_HC_MEM_ADVISE, db_phys, db_bytes, MIME_MEM_SPARSE, 0, 0, 0, nullptr);
    if (boot->memory_block_shift > PAGE_SHIFT + 4 && boot->memory_block_shift - PAGE_SHIFT <= MM_MAX_ORDER)
        g_discard_min_order = boot->memory_block_shift - PAGE_SHIFT;
    ULONG64 start = (db_phys + db_bytes - g_ram_base) >> PAGE_SHIFT;
    for (ULONG64 i = 0; i < start; i++)
        g_pfn[i].Flags = MM_PFN_RESERVED;
    /* the host never backed this memory, so it is all zeros. */
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
