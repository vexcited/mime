/*
 * user address spaces: vads, commit state, page faults and the virtual memory system services.
 *
 * vads track reservations and the page tables hold commit state. a committed but untouched page is an
 * invalid pte carrying MM_SWPTE_COMMITTED and its protection, like MMPTE_SOFTWARE. vads sit in arrays
 * sorted by address, and range operations walk the tables one table at a time, skipping holes, so a
 * terabyte reservation costs only what its committed pages cost.
 *
 * a placeholder is a vad that only reserves address space. an allocation or view of exactly its range
 * replaces it, and can turn back into one.
 */
#include "../ntos.h"

#define MM_LOWEST_USER_ADDRESS 0x10000ull
#define MM_HIGHEST_USER_ADDRESS 0x7FFFFFFEFFFFull
#define MM_ALLOCATION_GRANULARITY 0x10000ull
#define MM_TOP_DOWN_START 0x7FF000000000ull
#define MM_TABLE_SPAN (1ull << 21) /* the range one table of ptes covers. */

#define MM_VAD_TAG KI_TAG('M', 'm', 'V', 'd')

#define MI_ALLOCATION_TYPES                                                                                       \
    (MEM_COMMIT | MEM_RESERVE | MEM_REPLACE_PLACEHOLDER | MEM_RESERVE_PLACEHOLDER | MEM_RESET | MEM_TOP_DOWN |       \
     MEM_WRITE_WATCH | MEM_PHYSICAL | MEM_ROTATE | MEM_RESET_UNDO | MEM_LARGE_PAGES)

static ULONG64 g_kuser_page;

void MiSetKuserPage(ULONG64 pa)
{
    g_kuser_page = pa;
}

static inline ULONG64 sw_pte(ULONG protect)
{
    return MM_SWPTE_COMMITTED | ((ULONG64)(protect & 0x7FF) << MM_SWPTE_PROT_SHIFT);
}

static inline BOOLEAN pte_committed(ULONG64 pte)
{
    return (pte & MM_PTE_VALID) || (pte & MM_SWPTE_COMMITTED);
}

#define EXECUTABLE_PROTECTION (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
#define STATUS_DYNAMIC_CODE_BLOCKED ((NTSTATUS)0xC0000604L)

/*
 * Windows' rules: one base protection, at most one of guard, no cache and write combine, none of them
 * with PAGE_NOACCESS, and write combine only with the plain data protections.
 */
BOOLEAN MiProtectionIsValid(ULONG protect)
{
    if (protect >= 0x800)
        return FALSE;
    ULONG base = protect & 0xFF, modifiers = protect & 0x700;
    if (!base || (base & (base - 1)))
        return FALSE;
    if (!modifiers)
        return TRUE;
    if (base == PAGE_NOACCESS)
        return FALSE;
    if (modifiers & PAGE_GUARD)
        return !(modifiers & (PAGE_NOCACHE | PAGE_WRITECOMBINE));
    if (modifiers & PAGE_NOCACHE)
        return !(modifiers & PAGE_WRITECOMBINE);
    return !(base & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

/* ----------------------------------------------------------------- commit charge */

/*
 * private commit is charged against the machine's limit, its memory, as on a windows machine without a paging file: the
 * host grants no memory past its floor, so its swap is no paging file for the guest. the free memory the host could not
 * back now counts as charged already, as the commit of other processes does on windows, so commit fails when a program
 * asks for more than the host can give.
 */
static volatile LONG64 g_commit_pages, g_commit_peak;
static volatile LONG64 g_resident_pages; /* private pages backed by memory, of every process. */

void MiAddResident(LONG64 pages)
{
    __atomic_add_fetch(&g_resident_pages, pages, __ATOMIC_RELAXED);
}

static LONG64 commit_limit(void)
{
    return (LONG64)MmGetTotalPages();
}

/* what the machine has promised: the memory in use, the commit not touched yet, and what the host could not back. */
static LONG64 committed(LONG64 charged)
{
    LONG64 resident = __atomic_load_n(&g_resident_pages, __ATOMIC_RELAXED);
    LONG64 untouched = charged > resident ? charged - resident : 0;
    LONG64 in_use = (LONG64)(MmGetTotalPages() - MmGetFreePages());
    return untouched + in_use + (LONG64)MmGetUnbackablePages();
}

SIZE_T MmGetCommittedPages(void)
{
    return (SIZE_T)committed(__atomic_load_n(&g_commit_pages, __ATOMIC_RELAXED));
}

SIZE_T MmGetPeakCommittedPages(void)
{
    return (SIZE_T)__atomic_load_n(&g_commit_peak, __ATOMIC_RELAXED);
}

SIZE_T MmGetCommitLimitPages(void)
{
    return (SIZE_T)commit_limit();
}

static BOOLEAN charge_commit(PMMADDRESS_SPACE as, SIZE_T pages)
{
    if (!pages)
        return TRUE;
    /* a job can limit what the process or the whole job commits. */
    PEPROCESS process = CONTAINING_RECORD(as, EPROCESS, AddressSpace);
    BOOLEAN in_job = process->Job != nullptr;
    if (in_job && !PspJobChargeCommit(process, as->CommitCharge, pages))
        return FALSE;
    LONG64 total = committed(__atomic_add_fetch(&g_commit_pages, (LONG64)pages, __ATOMIC_SEQ_CST));
    if (total > commit_limit()) {
        __atomic_sub_fetch(&g_commit_pages, (LONG64)pages, __ATOMIC_SEQ_CST);
        if (in_job)
            PspJobReturnCommit(process, pages);
        return FALSE;
    }
    LONG64 peak = __atomic_load_n(&g_commit_peak, __ATOMIC_RELAXED);
    while (total > peak && !__atomic_compare_exchange_n(&g_commit_peak, &peak, total, FALSE, __ATOMIC_RELAXED,
                                                         __ATOMIC_RELAXED))
        ;
    as->CommitCharge += pages;
    if (as->CommitCharge > as->PeakCommitCharge)
        as->PeakCommitCharge = as->CommitCharge;
    return TRUE;
}

static void return_commit(PMMADDRESS_SPACE as, SIZE_T pages)
{
    if (pages > as->CommitCharge)
        pages = as->CommitCharge;
    as->CommitCharge -= pages;
    __atomic_sub_fetch(&g_commit_pages, (LONG64)pages, __ATOMIC_SEQ_CST);
    PEPROCESS process = CONTAINING_RECORD(as, EPROCESS, AddressSpace);
    if (process->Job)
        PspJobReturnCommit(process, pages);
}

/* ----------------------------------------------------------------- vad arrays */

static BOOLEAN vad_reserve_slots(PMMADDRESS_SPACE as, ULONG extra)
{
    if (as->VadCount + extra <= as->VadCapacity)
        return TRUE;
    ULONG capacity = as->VadCapacity ? as->VadCapacity : 64;
    while (capacity < as->VadCount + extra)
        capacity *= 2;
    SIZE_T bytes = (SIZE_T)capacity * (sizeof(MMVAD_SPAN) + sizeof(PMMVAD));
    PUCHAR block = ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, bytes, KI_TAG('M', 'm', 'V', 'a'));
    if (!block)
        return FALSE;
    MMVAD_SPAN *spans = (MMVAD_SPAN *)block;
    PMMVAD *vads = (PMMVAD *)(spans + capacity);
    if (as->VadCount) {
        memcpy(spans, as->Spans, (SIZE_T)as->VadCount * sizeof(MMVAD_SPAN));
        memcpy(vads, as->Vads, (SIZE_T)as->VadCount * sizeof(PMMVAD));
    }
    if (as->Spans)
        ExFreePool(as->Spans);
    as->Spans = spans;
    as->Vads = vads;
    as->VadCapacity = capacity;
    return TRUE;
}

/* the first vad ending above va. it holds va if it starts at or below it. */
static ULONG vad_lower_bound(PMMADDRESS_SPACE as, ULONG_PTR va)
{
    ULONG low = 0, high = as->VadCount;
    while (low < high) {
        ULONG mid = low + (high - low) / 2;
        if (as->Spans[mid].End <= va)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

static ULONG vad_index(PMMADDRESS_SPACE as, PMMVAD vad)
{
    return vad_lower_bound(as, vad->StartVa);
}

PMMVAD MiFindVad(PMMADDRESS_SPACE as, ULONG_PTR va)
{
    PMMVAD hint = as->Hint;
    if (hint && va >= hint->StartVa && va < hint->EndVa)
        return hint;
    ULONG i = vad_lower_bound(as, va);
    if (i >= as->VadCount || as->Spans[i].Start > va)
        return nullptr;
    return as->Hint = as->Vads[i];
}

BOOLEAN MiRangeIsFree(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    if (start < MM_LOWEST_USER_ADDRESS || end > MM_HIGHEST_USER_ADDRESS + 1 || end <= start)
        return FALSE;
    ULONG i = vad_lower_bound(as, start);
    return i >= as->VadCount || as->Spans[i].Start >= end;
}

NTSTATUS MiInsertVad(PMMADDRESS_SPACE as, PMMVAD vad)
{
    if (!vad_reserve_slots(as, 1))
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG i = vad_lower_bound(as, vad->StartVa);
    ULONG tail = as->VadCount - i;
    if (tail) {
        memmove(&as->Spans[i + 1], &as->Spans[i], (SIZE_T)tail * sizeof(MMVAD_SPAN));
        memmove(&as->Vads[i + 1], &as->Vads[i], (SIZE_T)tail * sizeof(PMMVAD));
    }
    as->Spans[i].Start = vad->StartVa;
    as->Spans[i].End = vad->EndVa;
    as->Vads[i] = vad;
    as->VadCount++;
    return STATUS_SUCCESS;
}

void MiRemoveVad(PMMADDRESS_SPACE as, PMMVAD vad)
{
    if (as->Hint == vad)
        as->Hint = nullptr;
    ULONG i = vad_index(as, vad);
    if (i >= as->VadCount || as->Vads[i] != vad)
        return;
    ULONG tail = as->VadCount - i - 1;
    if (tail) {
        memmove(&as->Spans[i], &as->Spans[i + 1], (SIZE_T)tail * sizeof(MMVAD_SPAN));
        memmove(&as->Vads[i], &as->Vads[i + 1], (SIZE_T)tail * sizeof(PMMVAD));
    }
    as->VadCount--;
}

/* for a split or trim, where the vad keeps its slot. */
static void vad_set_range(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR start, ULONG_PTR end)
{
    ULONG i = vad_index(as, vad);
    vad->StartVa = start;
    vad->EndVa = end;
    as->Spans[i].Start = start;
    as->Spans[i].End = end;
}

static PMMVAD vad_create(ULONG_PTR start, ULONG_PTR end, ULONG type, ULONG protect, ULONG flags)
{
    PMMVAD vad = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MMVAD), MM_VAD_TAG);
    if (!vad)
        return nullptr;
    vad->StartVa = start;
    vad->EndVa = end;
    vad->Type = type;
    vad->InitialProtect = protect;
    vad->Flags = flags;
    return vad;
}

static void vad_destroy(PMMVAD vad)
{
    if (vad->Section)
        ObDereferenceObject(vad->Section);
    ExFreePool(vad);
}

/* the lowest or highest aligned free range inside [lowest, highest], found by scanning the vad array. */
NTSTATUS MiFindFreeRangeEx(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, ULONG_PTR lowest,
                           ULONG_PTR highest, BOOLEAN top_down, ULONG_PTR *out)
{
    /* a search from above a 32-bit program's limit is native code's, which windows lets past it too. */
    ULONG_PTR maximum = as->AllocationLimit && lowest < as->AllocationLimit ? as->AllocationLimit
                                                                            : MM_HIGHEST_USER_ADDRESS + 1;
    if (!highest || highest + 1 > maximum)
        highest = maximum - 1;
    if (lowest < MM_LOWEST_USER_ADDRESS)
        lowest = MM_LOWEST_USER_ADDRESS;
    if (!alignment)
        alignment = MM_ALLOCATION_GRANULARITY;
    if (size == 0 || lowest >= highest || size - 1 > highest - lowest)
        return STATUS_NO_MEMORY;
    ULONG_PTR limit = highest + 1;
    if (top_down) {
        if (limit > MM_TOP_DOWN_START && highest == maximum - 1)
            limit = MM_TOP_DOWN_START;
        /* the first vad ending at or above the limit holds the limit if it starts below it. */
        ULONG i = vad_lower_bound(as, limit - 1);
        for (;;) {
            if (i < as->VadCount && as->Spans[i].Start < limit)
                limit = as->Spans[i].Start;
            ULONG_PTR floor = i ? as->Spans[i - 1].End : 0;
            if (floor < lowest)
                floor = lowest;
            if (limit >= size && limit - size >= floor) {
                ULONG_PTR candidate = (limit - size) & ~(alignment - 1);
                if (candidate >= floor) {
                    *out = candidate;
                    return STATUS_SUCCESS;
                }
            }
            if (!i || limit < lowest + size)
                break;
            i--;
            limit = as->Spans[i].Start;
        }
        return STATUS_NO_MEMORY;
    }
    ULONG_PTR candidate = (lowest + alignment - 1) & ~(alignment - 1);
    ULONG i = vad_lower_bound(as, candidate);
    for (;;) {
        ULONG_PTR next = i < as->VadCount ? as->Spans[i].Start : limit;
        if (next > limit)
            next = limit;
        if (next > candidate && next - candidate >= size) {
            *out = candidate;
            return STATUS_SUCCESS;
        }
        if (i >= as->VadCount)
            break;
        if (as->Spans[i].End > candidate)
            candidate = (as->Spans[i].End + alignment - 1) & ~(alignment - 1);
        if (candidate >= limit || limit - candidate < size)
            break;
        i++;
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS MiFindFreeRange(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, BOOLEAN top_down, ULONG_PTR *out)
{
    return MiFindFreeRangeEx(as, size, alignment, 0, 0, top_down, out);
}

/* ----------------------------------------------------------------- page table runs */

static inline ULONG pt_index(ULONG_PTR va, int level)
{
    return (ULONG)((va >> (39 - 9 * level)) & 0x1FF);
}

typedef struct {
    ULONG64 *root;
    ULONG_PTR va;
    ULONG_PTR end;
} PTE_CURSOR;

/*
 * the next existing table in the cursor's range: returns its first pte in range, and sets the address
 * that pte maps and how many of the table's ptes are in range. missing tables and subtrees are skipped.
 */
static ULONG64 *cursor_next(PTE_CURSOR *c, ULONG_PTR *va_out, SIZE_T *count)
{
    ULONG_PTR va = c->va;
    while (va < c->end) {
        ULONG64 e = c->root[pt_index(va, 0)];
        if (!(e & MM_PTE_VALID) || !MI_PTE_IS_TABLE(e, 0)) {
            va = (va + (1ull << 39)) & ~((1ull << 39) - 1);
            continue;
        }
        ULONG64 *table = HalPhysmap(e & MM_PTE_ADDR_MASK);
        e = table[pt_index(va, 1)];
        if (!(e & MM_PTE_VALID) || !MI_PTE_IS_TABLE(e, 1)) {
            va = (va + (1ull << 30)) & ~((1ull << 30) - 1);
            continue;
        }
        table = HalPhysmap(e & MM_PTE_ADDR_MASK);
        e = table[pt_index(va, 2)];
        if (!(e & MM_PTE_VALID) || !MI_PTE_IS_TABLE(e, 2)) {
            va = (va + MM_TABLE_SPAN) & ~(MM_TABLE_SPAN - 1);
            continue;
        }
        table = HalPhysmap(e & MM_PTE_ADDR_MASK);
        ULONG_PTR next = (va + MM_TABLE_SPAN) & ~(MM_TABLE_SPAN - 1);
        if (next > c->end || next < va)
            next = c->end;
        *va_out = va;
        *count = (next - va) >> PAGE_SHIFT;
        c->va = next;
        return &table[pt_index(va, 3)];
    }
    c->va = va;
    return nullptr;
}

static BOOLEAN ensure_tables(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    for (ULONG_PTR va = start; va < end; va = (va + MM_TABLE_SPAN) & ~(MM_TABLE_SPAN - 1))
        if (!MiWalk(as->Root, va, TRUE, 3))
            return FALSE;
    return TRUE;
}

/* counts private and section pages alike. */
static SIZE_T count_committed(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T committed = 0, count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));)
        for (SIZE_T i = 0; i < count; i++)
            committed += pte_committed(pte[i]);
    return committed;
}

/*
 * a write to x86 code xta64 translated, see MIME_VM_CODE_WATCH. the address space lock orders the entries, and the
 * count is published after the page so xta64 never reads an entry before it is there.
 */
static void code_log(PMMADDRESS_SPACE as, ULONG_PTR page)
{
    MIME_CODE_LOG *log = as->CodeLog;
    if (!log)
        return;
    ULONG64 n = log->Written;
    log->Pages[n % MIME_CODE_LOG_PAGES] = page;
    __atomic_store_n(&log->Written, n + 1, __ATOMIC_RELEASE);
}

/* works a table at a time so translations are flushed once per table. returns how many pages were committed. */
static SIZE_T release_pages(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    ULONG64 old[512];
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T committed = 0, count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));) {
        BOOLEAN any = FALSE;
        for (SIZE_T i = 0; i < count; i++)
            any |= pte[i] != 0;
        if (!any)
            continue;
        MiClearPtes(pte, va, count, as->Asid, old);
        ULONG64 freed[512];
        ULONG n = 0;
        for (SIZE_T i = 0; i < count; i++) {
            ULONG64 e = old[i];
            committed += pte_committed(e);
            if (e & MM_PTE_SW_CODE)
                code_log(as, va + (i << PAGE_SHIFT));
            if ((e & MM_PTE_VALID) && !(e & MM_PTE_SW_SHARED)) {
                freed[n++] = e & MM_PTE_ADDR_MASK;
                if (as->CommittedPages) {
                    as->CommittedPages--;
                    MiAddResident(-1);
                }
            }
        }
        if (n)
            MmFreePages(freed, n);
    }
    return committed;
}

static void free_tables(ULONG64 *table, int level)
{
    /* the kernel half of an x64 root belongs to every process. */
    for (int i = 0; i < (level ? 512 : MI_USER_ROOT_ENTRIES); i++) {
        ULONG64 e = table[i];
        if (!(e & MM_PTE_VALID) || !MI_PTE_IS_TABLE(e, level))
            continue;
        ULONG64 *child = HalPhysmap(e & MM_PTE_ADDR_MASK);
        if (level < 2)
            free_tables(child, level + 1);
        MmFreePage(e & MM_PTE_ADDR_MASK);
    }
}

SIZE_T MiReleasePages(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    return release_pages(as, start, end);
}

/* keeps the page's contents. a write watched page stays read-only while clean and writable once written. */
static ULONG64 reprotect_pte(ULONG64 v, ULONG protect, BOOLEAN watch)
{
    if (v & MM_PTE_VALID) {
        ULONG64 attrs = MiProtectToPte(protect, TRUE) | (v & MM_PTE_SW_SHARED);
        /* shared pages become copy on write when they are made writable. */
        if ((v & MM_PTE_SW_SHARED) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_COW;
        if (watch && !(v & MM_PTE_SW_SHARED) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
            BOOLEAN written = !(v & MM_PTE_SW_WATCH) && (MiPteToProtect(v) & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
            if (!written)
                attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_WATCH;
        }
        /* translated code stays watched while it can be written; a page that cannot be written needs no watch. */
        if ((v & MM_PTE_SW_CODE) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                                                PAGE_EXECUTE_WRITECOPY)))
            attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_CODE;
        return (v & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    }
    return sw_pte(protect) | (v & MM_SWPTE_SECTION);
}

/* commits the range with a protection. its tables must exist. */
static void apply_protection(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR start, ULONG_PTR end, ULONG protect)
{
    ULONG64 values[512];
    BOOLEAN watch = (vad->Flags & MM_VAD_WRITE_WATCH) != 0;
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));) {
        for (SIZE_T i = 0; i < count; i++)
            values[i] = reprotect_pte(pte[i], protect, watch);
        MiRewritePtes(pte, va, count, values, as->Asid);
    }
}

static NTSTATUS commit_range(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR start, ULONG_PTR end, ULONG protect)
{
    SIZE_T fresh = 0;
    if (vad->Type == MiVadPrivate) {
        fresh = ((end - start) >> PAGE_SHIFT) - count_committed(as, start, end);
        if (!charge_commit(as, fresh))
            return STATUS_COMMITMENT_LIMIT;
    }
    if (!ensure_tables(as, start, end)) {
        return_commit(as, fresh);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    apply_protection(as, vad, start, end, protect);
    return STATUS_SUCCESS;
}

/* the contents no longer matter, so the pages go back to demand zero. */
static void reset_range(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    ULONG64 values[512], old[512];
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));) {
        BOOLEAN any = FALSE;
        for (SIZE_T i = 0; i < count; i++) {
            old[i] = values[i] = pte[i];
            if ((old[i] & MM_PTE_VALID) && !(old[i] & MM_PTE_SW_SHARED)) {
                values[i] = sw_pte(MiPteToProtect(old[i])) | (old[i] & MM_SWPTE_SECTION);
                any = TRUE;
                if (old[i] & MM_PTE_SW_CODE)
                    code_log(as, va + (i << PAGE_SHIFT));
            }
        }
        if (!any)
            continue;
        MiRewritePtes(pte, va, count, values, as->Asid);
        ULONG64 freed[512];
        ULONG n = 0;
        for (SIZE_T i = 0; i < count; i++) {
            if ((old[i] & MM_PTE_VALID) && !(old[i] & MM_PTE_SW_SHARED)) {
                freed[n++] = old[i] & MM_PTE_ADDR_MASK;
                if (as->CommittedPages) {
                    as->CommittedPages--;
                    MiAddResident(-1);
                }
            }
        }
        if (n)
            MmFreePages(freed, n);
    }
}

NTSTATUS MmCreateAddressSpace(PMMADDRESS_SPACE as)
{
    ExInitializeFastMutex(&as->Lock);
    as->RootPhys = MmAllocatePage();
    if (!as->RootPhys)
        return STATUS_INSUFFICIENT_RESOURCES;
    as->Root = HalPhysmap(as->RootPhys);
    MiInitializeProcessRoot(as->Root);
    PMMVAD vad = vad_create(MIME_USER_KUSER_SHARED, MIME_USER_KUSER_SHARED + PAGE_SIZE, MiVadShared, PAGE_READONLY, 0);
    if (!vad || !NT_SUCCESS(MiInsertVad(as, vad)))
        return STATUS_INSUFFICIENT_RESOURCES;
    if (g_kuser_page)
        MiMapPage(as->Root, MIME_USER_KUSER_SHARED, g_kuser_page,
                  MiProtectToPte(PAGE_READONLY, TRUE) | MM_PTE_SW_SHARED);
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------- placeholders */

static PMMVAD find_placeholder(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    PMMVAD vad = MiFindVad(as, start);
    if (vad && (vad->Flags & MM_VAD_PLACEHOLDER) && vad->StartVa == start && vad->EndVa == end)
        return vad;
    return nullptr;
}

/* swaps in a vad of the same range, keeping its slot in the arrays. */
void MiSwapVad(PMMADDRESS_SPACE as, PMMVAD old, PMMVAD fresh)
{
    ULONG i = vad_index(as, old);
    as->Vads[i] = fresh;
    if (as->Hint == old)
        as->Hint = nullptr;
}

/* the placeholders must span the range exactly. */
static NTSTATUS coalesce_placeholders(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    ULONG first = vad_lower_bound(as, start);
    if (first >= as->VadCount || as->Spans[first].Start != start)
        return STATUS_CONFLICTING_ADDRESSES;
    ULONG last = first;
    for (;;) {
        PMMVAD vad = as->Vads[last];
        if (!(vad->Flags & MM_VAD_PLACEHOLDER))
            return STATUS_CONFLICTING_ADDRESSES;
        if (vad->EndVa == end)
            break;
        if (vad->EndVa > end || last + 1 >= as->VadCount || as->Spans[last + 1].Start != vad->EndVa)
            return STATUS_CONFLICTING_ADDRESSES;
        last++;
    }
    if (last == first)
        return STATUS_CONFLICTING_ADDRESSES;
    PMMVAD head = as->Vads[first];
    for (ULONG i = first + 1; i <= last; i++) {
        if (as->Hint == as->Vads[i])
            as->Hint = nullptr;
        ExFreePool(as->Vads[i]);
    }
    ULONG tail = as->VadCount - last - 1;
    memmove(&as->Spans[first + 1], &as->Spans[last + 1], (SIZE_T)tail * sizeof(MMVAD_SPAN));
    memmove(&as->Vads[first + 1], &as->Vads[last + 1], (SIZE_T)tail * sizeof(PMMVAD));
    as->VadCount -= last - first;
    head->EndVa = end;
    as->Spans[first].End = end;
    return STATUS_SUCCESS;
}

/* frees all of a private vad or a part that splits it. with preserve the range stays reserved as a placeholder. */
static NTSTATUS release_vad_range(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR start, ULONG_PTR end, BOOLEAN preserve)
{
    BOOLEAN whole = start == vad->StartVa && end == vad->EndVa;
    if (whole && preserve) {
        SIZE_T committed = release_pages(as, start, end);
        return_commit(as, committed);
        vad->Flags = MM_VAD_PLACEHOLDER;
        vad->InitialProtect = PAGE_NOACCESS;
        return STATUS_SUCCESS;
    }
    if (whole) {
        SIZE_T committed = release_pages(as, start, end);
        return_commit(as, committed);
        MiRemoveVad(as, vad);
        vad_destroy(vad);
        return STATUS_SUCCESS;
    }
    /* a part: the pieces left on each side keep the vad's flags. */
    if (!vad_reserve_slots(as, 2))
        return STATUS_INSUFFICIENT_RESOURCES;
    PMMVAD middle = nullptr, right = nullptr;
    if (preserve) {
        middle = vad_create(start, end, MiVadPrivate, PAGE_NOACCESS, MM_VAD_PLACEHOLDER);
        if (!middle)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (start > vad->StartVa && end < vad->EndVa) {
        right = vad_create(end, vad->EndVa, vad->Type, vad->InitialProtect, vad->Flags);
        if (!right) {
            if (middle)
                ExFreePool(middle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    SIZE_T committed = release_pages(as, start, end);
    return_commit(as, committed);
    ULONG_PTR old_end = vad->EndVa;
    if (start == vad->StartVa) {
        vad_set_range(as, vad, end, old_end);
    } else {
        vad_set_range(as, vad, vad->StartVa, start);
        if (right)
            MiInsertVad(as, right);
    }
    if (middle)
        MiInsertVad(as, middle);
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------- extended parameters */

/* each type may appear at most once, and only the types in the allowed bit mask. */
NTSTATUS MiCaptureExtendedParameters(const MEM_EXTENDED_PARAMETER *parameters, ULONG count, ULONG allowed,
                                     BOOLEAN user, MI_EXTENDED *out)
{
    memset(out, 0, sizeof(*out));
    if (!count)
        return parameters ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (!parameters)
        return STATUS_INVALID_PARAMETER;
    if (user && ((ULONG_PTR)parameters & 7))
        return STATUS_DATATYPE_MISALIGNMENT;
    ULONG seen = 0;
    for (ULONG i = 0; i < count; i++) {
        MEM_EXTENDED_PARAMETER p;
        if (user) {
            NTSTATUS status = KiCopyFromUser(&p, &parameters[i], sizeof(p));
            if (!NT_SUCCESS(status))
                return status;
        } else {
            p = parameters[i];
        }
        ULONG64 header = *(const ULONG64 *)&p;
        ULONG type = (ULONG)(header & 0xFF);
        if (header >= 0x100 || type - 1 > 5)
            return STATUS_INVALID_PARAMETER;
        ULONG bit = 1u << type;
        if (!(bit & allowed) || (bit & seen))
            return STATUS_INVALID_PARAMETER;
        seen |= bit;
        switch (type) {
        case MemExtendedParameterAddressRequirements: {
            MEM_ADDRESS_REQUIREMENTS r;
            if (user) {
                if ((ULONG_PTR)p.Pointer & 7)
                    return STATUS_DATATYPE_MISALIGNMENT;
                NTSTATUS status = KiCopyFromUser(&r, p.Pointer, sizeof(r));
                if (!NT_SUCCESS(status))
                    return status;
            } else {
                r = *(const MEM_ADDRESS_REQUIREMENTS *)p.Pointer;
            }
            out->Lowest = (ULONG_PTR)r.LowestStartingAddress;
            out->Highest = (ULONG_PTR)r.HighestEndingAddress;
            out->Alignment = r.Alignment;
            break;
        }
        case MemExtendedParameterNumaNode: {
            ULONG64 v = p.ULong64;
            if (v > 0xFFFFFFFFull) {
                v &= ~0x8000000000000000ull;
                if (v > 0xFFFFFFFFull)
                    return STATUS_INVALID_PARAMETER;
            }
            if ((ULONG)v == 0xFFFFFFFFu)
                return STATUS_INVALID_PARAMETER;
            out->Node = (ULONG)v + 1;
            break;
        }
        case MemExtendedParameterPartitionHandle:
            if (!p.ULong64)
                return STATUS_INVALID_PARAMETER;
            out->Partition = p.Handle;
            break;
        case MemExtendedParameterUserPhysicalHandle:
            out->PhysicalHandle = p.Handle;
            break;
        case MemExtendedParameterAttributeFlags:
            if (p.ULong64 & 0xFFFFFFFFFFFFFDC0ull)
                return STATUS_INVALID_PARAMETER;
            out->Attributes = p.ULong64;
            break;
        case MemExtendedParameterImageMachine:
            if (p.ULong64 > 0xFFFF)
                return STATUS_INVALID_PARAMETER;
            out->Machine = (USHORT)p.ULong64;
            break;
        }
    }
    out->Present = seen;
    return STATUS_SUCCESS;
}

/* the highest address zero_bits allows, read the way Windows reads it. */
NTSTATUS MiZeroBitsHighest(ULONG_PTR zero_bits, ULONG_PTR *highest)
{
    if (zero_bits) {
        ULONG64 leading = zero_bits < 32 ? zero_bits + 32 : (ULONG64)__builtin_clzll(zero_bits);
        if (leading > 0x35)
            return STATUS_INVALID_PARAMETER;
        ULONG_PTR limit = ~0ull >> leading;
        if (limit < *highest)
            *highest = limit;
    }
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------- allocation */

NTSTATUS MiAllocateVirtualEx(PEPROCESS process, PVOID *base_io, SIZE_T *size_io, ULONG type, ULONG protect,
                             ULONG_PTR zero_bits, const MI_EXTENDED *extended)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    MI_EXTENDED none = {};
    const MI_EXTENDED *ext = extended ? extended : &none;
    ULONG_PTR base = (ULONG_PTR)*base_io;
    SIZE_T size = *size_io;

    if ((type & ~(ULONG)MI_ALLOCATION_TYPES) || !(type & (MEM_COMMIT | MEM_RESERVE | MEM_RESET | MEM_RESET_UNDO)))
        return STATUS_INVALID_PARAMETER;
    BOOLEAN reserve = (type & MEM_RESERVE) != 0, replace = (type & MEM_REPLACE_PLACEHOLDER) != 0;
    BOOLEAN reset = (type & (MEM_RESET | MEM_RESET_UNDO)) != 0;
    if (((type & MEM_RESET) && type != MEM_RESET) || ((type & MEM_RESET_UNDO) && type != MEM_RESET_UNDO))
        return STATUS_INVALID_PARAMETER;
    if ((type & MEM_WRITE_WATCH) && !reserve)
        return STATUS_INVALID_PARAMETER;
    if (replace && (!reserve || (type & ~(ULONG)(MEM_COMMIT | MEM_RESERVE | MEM_REPLACE_PLACEHOLDER | MEM_WRITE_WATCH))))
        return STATUS_INVALID_PARAMETER;
    if ((type & MEM_RESERVE_PLACEHOLDER) &&
        (!reserve || (type & ~(ULONG)(MEM_RESERVE | MEM_RESERVE_PLACEHOLDER | MEM_TOP_DOWN)) || protect != PAGE_NOACCESS))
        return STATUS_INVALID_PARAMETER;
    if (ext->Partition && !(type & MEM_LARGE_PAGES))
        return STATUS_INVALID_PARAMETER;
    if (ext->PhysicalHandle && !(type & MEM_PHYSICAL))
        return STATUS_INVALID_PARAMETER;
    if (ext->Attributes & ~0x5Full)
        return STATUS_INVALID_PARAMETER;
    ULONG paged_kind = (ULONG)ext->Attributes & (MEM_EXTENDED_PARAMETER_NONPAGED | MEM_EXTENDED_PARAMETER_NONPAGED_LARGE |
                                                  MEM_EXTENDED_PARAMETER_NONPAGED_HUGE);
    if (paged_kind & (paged_kind - 1))
        return STATUS_INVALID_PARAMETER;
    if (protect & PAGE_TARGETS_INVALID) {
        if (!(protect & 0xF0))
            return STATUS_INVALID_PARAMETER;
        protect &= ~(ULONG)PAGE_TARGETS_INVALID;
    }
    if ((ext->Attributes & MEM_EXTENDED_PARAMETER_EC_CODE) && !(protect & 0xF0))
        return STATUS_INVALID_PARAMETER;

    /* committing at no address reserves too. */
    if ((type & MEM_COMMIT) && !base) {
        type |= MEM_RESERVE;
        reserve = TRUE;
    }

    ULONG_PTR granularity = reserve && !replace ? MM_ALLOCATION_GRANULARITY : PAGE_SIZE;
    ULONG_PTR alignment = ext->Alignment;
    if (alignment) {
        if (alignment < granularity || (alignment & (alignment - 1)) || alignment >= 0x7FFFFFFF0000ull)
            return STATUS_INVALID_PARAMETER;
    } else {
        alignment = granularity;
    }
    if (size == 0)
        return STATUS_INVALID_PARAMETER;
    ULONG_PTR last = base + size - 1;
    if (last < base || last > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN searching = reserve && !replace && base == 0;
    if (!searching && (ext->Lowest || ext->Highest || ext->Alignment))
        return STATUS_INVALID_PARAMETER;
    ULONG_PTR start, end, lowest = ext->Lowest, highest = ext->Highest;
    if (reset) {
        start = (base + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
        end = (base + size) & ~(ULONG_PTR)(PAGE_SIZE - 1);
        if (start >= end)
            return STATUS_CONFLICTING_ADDRESSES;
    } else if (replace) {
        if ((base | size) & (PAGE_SIZE - 1))
            return STATUS_INVALID_PARAMETER;
        start = base;
        end = base + size;
    } else if (reserve) {
        start = base & ~(MM_ALLOCATION_GRANULARITY - 1);
        end = ROUND_TO_PAGES(base + size);
    } else {
        start = base & ~(ULONG_PTR)(PAGE_SIZE - 1);
        end = ROUND_TO_PAGES(base + size);
    }
    SIZE_T bytes = end - start;
    if (searching) {
        ULONG_PTR limit = as->AllocationLimit && lowest < as->AllocationLimit ? as->AllocationLimit - 1
                                                                                : MM_HIGHEST_USER_ADDRESS;
        if (limit > MM_HIGHEST_USER_ADDRESS)
            limit = MM_HIGHEST_USER_ADDRESS;
        if (zero_bits) {
            NTSTATUS status = MiZeroBitsHighest(zero_bits, &limit);
            if (!NT_SUCCESS(status))
                return status;
        }
        if (highest) {
            if (highest > MM_HIGHEST_USER_ADDRESS || ((highest + 1) & (PAGE_SIZE - 1)))
                return STATUS_INVALID_PARAMETER;
            if (zero_bits && highest > limit)
                highest = limit;
        } else {
            highest = limit;
        }
        if (lowest & (alignment - 1))
            return STATUS_INVALID_PARAMETER;
        if (lowest >= highest || highest - lowest + 1 < bytes)
            return STATUS_INVALID_PARAMETER;
    }
    if (!MiProtectionIsValid(protect))
        return STATUS_INVALID_PAGE_PROTECTION;
    if (type & (MEM_LARGE_PAGES | MEM_PHYSICAL))
        return STATUS_PRIVILEGE_NOT_HELD;
    if ((type & MEM_ROTATE) || paged_kind)
        return STATUS_NOT_SUPPORTED;
    if (ext->Partition && ext->Partition != (HANDLE)(LONG_PTR)-3)
        return STATUS_INVALID_HANDLE;

    NTSTATUS status = STATUS_SUCCESS;
    MiLockAddressSpace(as);
    if (reserve) {
        BOOLEAN commit = (type & MEM_COMMIT) != 0;
        BOOLEAN placeholder = (type & MEM_RESERVE_PLACEHOLDER) != 0;
        PMMVAD old = nullptr;
        if (replace) {
            old = find_placeholder(as, start, end);
            if (!old)
                status = STATUS_CONFLICTING_ADDRESSES;
        } else if (searching) {
            status = MiFindFreeRangeEx(as, bytes, alignment, lowest, highest, (type & MEM_TOP_DOWN) != 0, &start);
            end = start + bytes;
        } else if (!MiRangeIsFree(as, start, end)) {
            status = STATUS_CONFLICTING_ADDRESSES;
        }
        PMMVAD vad = nullptr;
        if (NT_SUCCESS(status)) {
            vad = vad_create(start, end, MiVadPrivate, placeholder ? PAGE_NOACCESS : protect,
                             (placeholder ? MM_VAD_PLACEHOLDER : 0) | (replace ? MM_VAD_REPLACED : 0) |
                                 ((type & MEM_WRITE_WATCH) ? MM_VAD_WRITE_WATCH : 0));
            if (!vad)
                status = STATUS_INSUFFICIENT_RESOURCES;
        }
        if (NT_SUCCESS(status) && commit)
            status = commit_range(as, vad, start, end, protect);
        if (NT_SUCCESS(status)) {
            if (old) {
                MiSwapVad(as, old, vad);
                ExFreePool(old);
            } else {
                status = MiInsertVad(as, vad);
                if (!NT_SUCCESS(status) && commit)
                    return_commit(as, release_pages(as, start, end));
            }
        }
        if (!NT_SUCCESS(status) && vad)
            ExFreePool(vad);
    } else {
        PMMVAD vad = MiFindVad(as, start);
        if (!vad || end > vad->EndVa || (vad->Flags & MM_VAD_PLACEHOLDER) || vad->Type == MiVadShared ||
            vad->Type == MiVadImage)
            status = STATUS_CONFLICTING_ADDRESSES;
        else if (reset && vad->Type == MiVadMapped && vad->Section && !vad->Section->File &&
                 !(vad->Section->Flags & (SEC_IMAGE | MI_SECTION_PHYSICAL))) {
            /* windows takes the hint on a pagefile backed view. other views share the pages, so they stay. */
        } else if (reset && vad->Type != MiVadPrivate)
            status = STATUS_SECTION_PROTECTION;
        else if (type & MEM_RESET)
            reset_range(as, start, end);
        else if (type & MEM_COMMIT)
            status = commit_range(as, vad, start, end, protect);
    }
    MiUnlockAddressSpace(as);
    if (NT_SUCCESS(status)) {
        *base_io = (PVOID)start;
        *size_io = end - start;
    }
    return status;
}

NTSTATUS MiAllocateVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type, ULONG protect)
{
    return MiAllocateVirtualEx(process, base, size, type, protect, 0, nullptr);
}

/* ----------------------------------------------------------------- freeing */

static NTSTATUS free_in_vad(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR start, ULONG_PTR end, ULONG type,
                            BOOLEAN preserve)
{
    if (vad->Type != MiVadPrivate)
        return STATUS_UNABLE_TO_DELETE_SECTION;
    if (preserve) {
        BOOLEAN whole = start == vad->StartVa && end == vad->EndVa;
        if (!(vad->Flags & (MM_VAD_PLACEHOLDER | MM_VAD_REPLACED)) || ((vad->Flags & MM_VAD_PLACEHOLDER) && whole))
            return STATUS_CONFLICTING_ADDRESSES;
    }
    if (type & MEM_RELEASE)
        return release_vad_range(as, vad, start, end, preserve);
    if (vad->Flags & MM_VAD_PLACEHOLDER)
        return STATUS_MEMORY_NOT_ALLOCATED;
    return_commit(as, release_pages(as, start, end));
    return STATUS_SUCCESS;
}

NTSTATUS MiFreeVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    BOOLEAN preserve = (type & MEM_PRESERVE_PLACEHOLDER) != 0, coalesce = (type & MEM_COALESCE_PLACEHOLDERS) != 0;
    type &= ~(ULONG)(MEM_PRESERVE_PLACEHOLDER | MEM_COALESCE_PLACEHOLDERS);
    ULONG_PTR start = (ULONG_PTR)*base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR end = *size ? (((ULONG_PTR)*base + *size - 1) | (PAGE_SIZE - 1)) + 1 : 0;
    NTSTATUS status;
    MiLockAddressSpace(as);
    PMMVAD vad = coalesce ? nullptr : MiFindVad(as, start);
    if (coalesce) {
        status = coalesce_placeholders(as, start, end);
    } else if (!vad) {
        status = STATUS_MEMORY_NOT_ALLOCATED;
    } else if (!*size && start != vad->StartVa) {
        /* a zero size frees the rest of the allocation, so it must start at the base. */
        status = STATUS_FREE_VM_NOT_AT_BASE;
    } else if (*size && end > vad->EndVa) {
        status = STATUS_UNABLE_TO_FREE_VM;
    } else {
        if (!*size)
            end = vad->EndVa;
        status = free_in_vad(as, vad, start, end, type, preserve);
    }
    MiUnlockAddressSpace(as);
    if (NT_SUCCESS(status)) {
        *base = (PVOID)start;
        *size = end - start;
    }
    return status;
}

/* ----------------------------------------------------------------- protection */

NTSTATUS MiProtectVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG protect, PULONG old)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!MiProtectionIsValid(protect))
        return STATUS_INVALID_PAGE_PROTECTION;
    ULONG_PTR start = (ULONG_PTR)*base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR end = ROUND_TO_PAGES((ULONG_PTR)*base + *size);
    MiLockAddressSpace(as);
    PMMVAD vad = MiFindVad(as, start);
    if (!vad || end > vad->EndVa) {
        MiUnlockAddressSpace(as);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    if (vad->Flags & MM_VAD_PLACEHOLDER) {
        /* a placeholder holds no pages, so only its current protection can be asked for. */
        MiUnlockAddressSpace(as);
        if (protect != PAGE_NOACCESS)
            return STATUS_CONFLICTING_ADDRESSES;
        if (old)
            *old = PAGE_NOACCESS;
        *base = (PVOID)start;
        *size = end - start;
        return STATUS_SUCCESS;
    }
    /* every page must be committed before any is changed. */
    if (count_committed(as, start, end) != (end - start) >> PAGE_SHIFT) {
        MiUnlockAddressSpace(as);
        return STATUS_NOT_COMMITTED;
    }
    ULONG first_old = MiPteToProtect(MiReadPte(as->Root, start));
    apply_protection(as, vad, start, end, protect);
    MiUnlockAddressSpace(as);
    if (old)
        *old = first_old;
    *base = (PVOID)start;
    *size = end - start;
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------- query */

/*
 * the region of a page, for a caller that keeps the vads and the tables as they are. the region is followed over at most
 * budget table entries, and FALSE tells it is longer.
 */
static BOOLEAN query_region(PMMADDRESS_SPACE as, ULONG_PTR va, PMEMORY_BASIC_INFORMATION info, SIZE_T budget)
{
    memset(info, 0, sizeof(*info));
    PMMVAD vad = MiFindVad(as, va);
    if (!vad) {
        ULONG i = vad_lower_bound(as, va);
        ULONG_PTR next = i < as->VadCount ? as->Spans[i].Start : MM_HIGHEST_USER_ADDRESS + 1;
        info->BaseAddress = (PVOID)va;
        info->RegionSize = next - va;
        info->State = MEM_FREE;
        info->Protect = PAGE_NOACCESS;
        return TRUE;
    }
    ULONG64 pte = MiReadPte(as->Root, va);
    BOOLEAN committed = pte_committed(pte);
    ULONG prot = committed ? MiPteToProtect(pte) : 0;
    ULONG_PTR end = va + PAGE_SIZE, limit = vad->EndVa;
    /* the region extends while pages match the first one's state and protection. */
    PTE_CURSOR c = {as->Root, end, limit};
    SIZE_T count;
    ULONG_PTR run_va;
    BOOLEAN stopped = FALSE;
    for (ULONG64 *p; !stopped && (p = cursor_next(&c, &run_va, &count));) {
        if (run_va > end) {
            /* a stretch with no table is uncommitted. */
            if (committed)
                break;
            end = run_va;
        }
        if (count > budget)
            return FALSE;
        budget -= count;
        for (SIZE_T i = 0; i < count; i++) {
            ULONG64 e = __atomic_load_n(&p[i], __ATOMIC_RELAXED);
            if (pte_committed(e) != committed || (committed && MiPteToProtect(e) != prot)) {
                stopped = TRUE;
                break;
            }
            end = run_va + ((i + 1) << PAGE_SHIFT);
        }
    }
    if (!stopped && !committed && end < limit && c.va >= limit)
        end = limit;
    info->BaseAddress = (PVOID)va;
    info->AllocationBase = (PVOID)vad->StartVa;
    info->AllocationProtect = vad->InitialProtect;
    info->RegionSize = end - va;
    info->State = committed ? MEM_COMMIT : MEM_RESERVE;
    info->Protect = prot;
    info->Type = vad->Type == MiVadImage ? MEM_IMAGE : vad->Type == MiVadPrivate ? MEM_PRIVATE : MEM_MAPPED;
    return TRUE;
}

NTSTATUS MiQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR va = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (va > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    MiLockAddressSpace(as);
    query_region(as, va, info, (SIZE_T)-1);
    MiUnlockAddressSpace(as);
    return STATUS_SUCCESS;
}

/* maps the prototype page, a relocated one, or a private copy. */
static BOOLEAN section_fault(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG protect)
{
    ULONG64 pa;
    BOOLEAN shared;
    if (!MiSectionFaultPage(vad, page, protect, &pa, &shared))
        return FALSE;
    if (!shared) {
        as->CommittedPages++;
        MiAddResident(1);
    }
    *pte = pa | MiSectionPteAttrs(vad->Section, protect, pa, shared) | MM_PTE_VALID | MM_PTE_PAGE;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

static BOOLEAN map_zero_page(PMMADDRESS_SPACE as, ULONG64 *pte, ULONG64 v, BOOLEAN clean)
{
    ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return FALSE;
    ULONG64 attrs = MiProtectToPte(protect, TRUE) & ~MM_PTE_SW_COW;
    if (protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
        attrs = MI_PTE_SET_RW(attrs);
    /* an unwritten page of a write watched allocation stays read-only until written. */
    if (clean && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_WATCH;
    *pte = pa | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    MiAddResident(1);
    if (++as->CommittedPages > as->PeakCommittedPages)
        as->PeakCommittedPages = as->CommittedPages;
    return TRUE;
}

/*
 * a demand zero fault also maps the other committed pages of its 16 KiB window: the host backs that
 * whole host page anyway, so this saves faults at no extra host memory. a fault continuing where a
 * recent window ended gets a 64 KiB window instead.
 */
#define FAULT_WINDOW (16 * 1024)
#define FAULT_WINDOW_SEQUENTIAL (64 * 1024)

static BOOLEAN demand_zero(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG64 v, BOOLEAN write)
{
    BOOLEAN watch = (vad->Flags & MM_VAD_WRITE_WATCH) != 0;
    if (!map_zero_page(as, pte, v, watch && !write))
        return FALSE;
    /* a touch right where a recent window ended continues a sequential stream. */
    ULONG stream = (ULONG)-1;
    for (ULONG i = 0; i < 8; i++)
        if (as->LastFaultEnd[i] && page >= as->LastFaultEnd[i] && page < as->LastFaultEnd[i] + FAULT_WINDOW)
            stream = i;
    ULONG_PTR size = stream != (ULONG)-1 ? FAULT_WINDOW_SEQUENTIAL : FAULT_WINDOW;
    ULONG_PTR start = page & ~(size - 1), end = start + size;
    if (start < vad->StartVa)
        start = vad->StartVa;
    if (end > vad->EndVa)
        end = vad->EndVa;
    /* the window lies within one table, so its ptes are contiguous. */
    ULONG64 *first = pte - ((page - start) >> PAGE_SHIFT);
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *p = first + ((va - start) >> PAGE_SHIFT), w = *p;
        ULONG protect = (ULONG)((w & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (va == page || (w & MM_PTE_VALID) || !(w & MM_SWPTE_COMMITTED) || (w & MM_SWPTE_SECTION) ||
            (protect & 0xFF) == PAGE_NOACCESS || (protect & PAGE_GUARD))
            continue;
        if (!map_zero_page(as, p, w, watch))
            break;
    }
    if (stream == (ULONG)-1)
        stream = as->NextFaultStream++ & 7;
    as->LastFaultEnd[stream] = end;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

/*
 * first touch of a shared view page, without the kernel lock. the prototype page is built if needed and
 * published with a compare and swap, so two processors building it do no harm. neighbours in the same
 * 16-page window whose prototype already exists are mapped along with it. a writable image page is
 * private from the start and gets its copy here, alone.
 */
#define SECTION_FAULT_WINDOW (16 * PAGE_SIZE)

static BOOLEAN fast_section_fault(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG64 v,
                                  ULONG protect)
{
    PSECTION section = vad->Section;
    ULONG64 pa;
    BOOLEAN shared;
    if (!MiSectionFaultPage(vad, page, protect, &pa, &shared))
        return FALSE;
    ULONG64 entry = pa | MiSectionPteAttrs(section, protect, pa, shared) | MM_PTE_VALID | MM_PTE_PAGE;
    if (!__atomic_compare_exchange_n(pte, &v, entry, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        /* another processor mapped the page first. */
        if (!shared)
            MmFreePagesQuiet(&pa, 1);
        return TRUE;
    }
    if (!shared) {
        SIZE_T now = __atomic_add_fetch(&as->CommittedPages, 1, __ATOMIC_RELAXED);
        SIZE_T peak = __atomic_load_n(&as->PeakCommittedPages, __ATOMIC_RELAXED);
        while (now > peak && !__atomic_compare_exchange_n(&as->PeakCommittedPages, &peak, now, FALSE,
                                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            ;
    }
    __atomic_add_fetch(&as->PageFaults, 1, __ATOMIC_RELAXED);

    /* map only the neighbours whose page is ready already. */
    ULONG_PTR start = page & ~(ULONG_PTR)(SECTION_FAULT_WINDOW - 1), end = start + SECTION_FAULT_WINDOW;
    if (start < vad->StartVa)
        start = vad->StartVa;
    if (end > vad->EndVa)
        end = vad->EndVa;
    ULONG64 *first = pte - ((page - start) >> PAGE_SHIFT);
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *p = first + ((va - start) >> PAGE_SHIFT), w = __atomic_load_n(p, __ATOMIC_RELAXED);
        ULONG wp = (ULONG)((w & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (va == page || (w & MM_PTE_VALID) || !(w & MM_SWPTE_COMMITTED) || !(w & MM_SWPTE_SECTION) ||
            (wp & 0xFF) == PAGE_NOACCESS || (wp & PAGE_GUARD))
            continue;
        ULONG64 ready = MiSectionReadyPage(vad, (vad->SectionOffset + (va - vad->StartVa)) >> PAGE_SHIFT, wp);
        if (!ready)
            continue;
        ULONG64 neighbour = ready | MiSectionPteAttrs(section, wp, ready, TRUE) | MM_PTE_VALID | MM_PTE_PAGE;
        __atomic_compare_exchange_n(p, &w, neighbour, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    }
    (void)section;
    return TRUE;
}

/*
 * demand zero fault of a committed private page, without the kernel lock. it runs with interrupts off
 * from the start of the trap, as a reader of the address space gate: the lock holder sets the writer
 * bit and waits for readers to leave before changing a vad or a table, and a reader that sees the bit
 * gives up and takes the slow path. ptes go from absent to present with a compare and swap, which is all
 * concurrent faults on a table need, and only the allocator's lock is waited on. FALSE leaves the fault
 * to MmAccessFault, whatever the reason.
 */
static BOOLEAN fast_fault(PMMADDRESS_SPACE as, ULONG_PTR page, BOOLEAN write, BOOLEAN execute)
{
    PMMVAD vad = MiFindVad(as, page);
    if (!vad || (vad->Flags & MM_VAD_PLACEHOLDER))
        return FALSE;
    ULONG64 *pte = MiWalk(as->Root, page, FALSE, 3);
    if (!pte)
        return FALSE;
    ULONG64 v = __atomic_load_n(pte, __ATOMIC_RELAXED);
    if (v & MM_PTE_VALID) {
        /* another processor fixed the page first. */
        return MI_PTE_IS_USER(v) && (!write || !MI_PTE_IS_RO(v)) && (!execute || MI_PTE_USER_EXEC(v));
    }
    ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
    if (!(v & MM_SWPTE_COMMITTED) || (protect & 0xFF) == PAGE_NOACCESS || (protect & PAGE_GUARD))
        return FALSE;
    if (v & MM_SWPTE_SECTION)
        return vad->Section && !(KiOptions & KI_OPT_NO_SECTION_FAULT) && fast_section_fault(as, vad, page, pte, v, protect);

    BOOLEAN watch = (vad->Flags & MM_VAD_WRITE_WATCH) != 0;
    ULONG stream = (ULONG)-1;
    for (ULONG i = 0; i < 8; i++) {
        ULONG_PTR end = as->LastFaultEnd[i];
        if (end && page >= end && page < end + FAULT_WINDOW)
            stream = i;
    }
    ULONG_PTR size = stream != (ULONG)-1 ? FAULT_WINDOW_SEQUENTIAL : FAULT_WINDOW;
    ULONG_PTR start = page & ~(size - 1), end = start + size;
    if (start < vad->StartVa)
        start = vad->StartVa;
    if (end > vad->EndVa)
        end = vad->EndVa;

    /* the pages to map, the faulting one first. */
    ULONG64 *first = pte - ((page - start) >> PAGE_SHIFT);
    ULONG64 *slot[MM_BATCH_PAGES], old[MM_BATCH_PAGES];
    ULONG count = 1;
    slot[0] = pte;
    old[0] = v;
    for (ULONG_PTR va = start; va < end && count < MM_BATCH_PAGES; va += PAGE_SIZE) {
        ULONG64 *p = first + ((va - start) >> PAGE_SHIFT), w = __atomic_load_n(p, __ATOMIC_RELAXED);
        ULONG wp = (ULONG)((w & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (va == page || (w & MM_PTE_VALID) || !(w & MM_SWPTE_COMMITTED) || (w & MM_SWPTE_SECTION) ||
            (wp & 0xFF) == PAGE_NOACCESS || (wp & PAGE_GUARD))
            continue;
        slot[count] = p;
        old[count] = w;
        count++;
    }
    ULONG64 pages[MM_BATCH_PAGES];
    ULONG got = MmAllocateZeroedPages(count, pages);
    if (!got)
        return FALSE;

    ULONG mapped = 0, unused = 0;
    ULONG64 spare[MM_BATCH_PAGES];
    for (ULONG i = 0; i < got; i++) {
        ULONG p = (ULONG)((old[i] & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        ULONG64 attrs = MiProtectToPte(p, TRUE) & ~MM_PTE_SW_COW;
        if (p & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
            attrs = MI_PTE_SET_RW(attrs);
        /* an unwritten page of a write watched allocation stays read-only until written. */
        if (watch && (i || !write) && (p & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_WATCH;
        ULONG64 expected = old[i];
        if (__atomic_compare_exchange_n(slot[i], &expected, pages[i] | attrs | MM_PTE_VALID | MM_PTE_PAGE, FALSE,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
            mapped++;
        else
            spare[unused++] = pages[i];
    }
    if (unused)
        MmFreeZeroedPagesQuiet(spare, unused);

    if (mapped) {
        MiAddResident(mapped);
        SIZE_T now = __atomic_add_fetch(&as->CommittedPages, mapped, __ATOMIC_RELAXED);
        SIZE_T peak = __atomic_load_n(&as->PeakCommittedPages, __ATOMIC_RELAXED);
        while (now > peak && !__atomic_compare_exchange_n(&as->PeakCommittedPages, &peak, now, FALSE,
                                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            ;
    }
    __atomic_add_fetch(&as->PageFaults, 1, __ATOMIC_RELAXED);
    if (stream == (ULONG)-1)
        stream = __atomic_fetch_add(&as->NextFaultStream, 1, __ATOMIC_RELAXED) & 7;
    as->LastFaultEnd[stream] = end;
    return TRUE;
}

/* for the thread dump. one row per processor so counting shares nothing. */
MM_FAULT_STATS MmFaultStats[MAXIMUM_PROCESSORS];

BOOLEAN MmFastFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute)
{
    if (va > MM_HIGHEST_USER_ADDRESS || (KiOptions & KI_OPT_NO_FAST_FAULT))
        return FALSE;
    /* a thread preempted inside the gate would keep a writer waiting. */
    ULONG64 state = KiDisableInterrupts();
    PMMADDRESS_SPACE as = &((PEPROCESS)KiCurrentThread()->ApcStateProcess)->AddressSpace;
    if (!as->Root) {
        KiRestoreInterrupts(state);
        return FALSE;
    }
    ULONG gate = __atomic_load_n(&as->FaultGate, __ATOMIC_RELAXED);
    do {
        if (gate & MI_GATE_WRITER) {
            MmFaultStats[KeGetCurrentPrcb()->Number].Gated++;
            KiRestoreInterrupts(state);
            return FALSE;
        }
    } while (!__atomic_compare_exchange_n(&as->FaultGate, &gate, gate + MI_GATE_READER, TRUE, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
    BOOLEAN handled = fast_fault(as, va & ~(ULONG_PTR)(PAGE_SIZE - 1), write, execute);
    __atomic_fetch_sub(&as->FaultGate, MI_GATE_READER, __ATOMIC_RELEASE);
    MmFaultStats[KeGetCurrentPrcb()->Number].Fast += handled;
    MmFaultStats[KeGetCurrentPrcb()->Number].Rejected += !handled;
    KiRestoreInterrupts(state);
    return handled;
}

/* a longer region keeps a writer of the gate waiting with the kernel lock for too long, and is the usual way's. */
#define FAST_QUERY_ENTRIES 16384

/*
 * the region of an address of a process, the current one when none is given, without the kernel lock and with
 * interrupts off. it is a reader of the gate as MmFastFault is: no vad and no table changes meanwhile, and an entry
 * only goes from absent to present, which leaves its state and its protection as they were. the end of the process
 * is a writer too. FALSE leaves the call to the usual way.
 */
BOOLEAN MmFastQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info)
{
    PMMADDRESS_SPACE as = &(process ? process : (PEPROCESS)KiCurrentThread()->ApcStateProcess)->AddressSpace;
    ULONG_PTR va = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (va > MM_HIGHEST_USER_ADDRESS || (KiOptions & KI_OPT_NO_FAST_FAULT))
        return FALSE;
    ULONG gate = __atomic_load_n(&as->FaultGate, __ATOMIC_RELAXED);
    do {
        if (gate & MI_GATE_WRITER)
            return FALSE;
    } while (!__atomic_compare_exchange_n(&as->FaultGate, &gate, gate + MI_GATE_READER, TRUE, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
    BOOLEAN done = as->Root && query_region(as, va, info, FAST_QUERY_ENTRIES);
    __atomic_fetch_sub(&as->FaultGate, MI_GATE_READER, __ATOMIC_RELEASE);
    return done;
}

/*
 * copies into another process without attaching to it, which would switch page tables twice per request
 * and, with no tagged translations, flush this processor's each time. present writable pages are written
 * through their physical addresses. returns how much it copied: everything unless a page is absent, copy
 * on write, guarded or read-only, and the caller copies the rest the slow way. the kernel lock keeps the
 * map and pages stable, and lock-free faults on other processors only make pages present.
 */
SIZE_T MmCopyToProcessFast(PEPROCESS process, PVOID dst, const void *src, SIZE_T length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR va = (ULONG_PTR)dst;
    if (!as->Root || va > MM_HIGHEST_USER_ADDRESS || length > MM_HIGHEST_USER_ADDRESS - va + 1)
        return 0;
    SIZE_T done = 0;
    while (done < length) {
        ULONG64 pte = MiReadPte(as->Root, (va + done) & ~(ULONG_PTR)(PAGE_SIZE - 1));
        /* file window pages have no pfn and are never written through the physmap, whatever their protection. */
        if (!(pte & MM_PTE_VALID) || !MI_PTE_IS_USER(pte) || MI_PTE_IS_RO(pte) || (pte & (MM_PTE_SW_COW | MM_PTE_SW_WATCH)) ||
            !MiGetPfn(pte & MM_PTE_ADDR_MASK))
            break;
        SIZE_T offset = (va + done) & (PAGE_SIZE - 1), n = PAGE_SIZE - offset;
        if (n > length - done)
            n = length - done;
        memcpy((PUCHAR)HalPhysmap(pte & MM_PTE_ADDR_MASK) + offset, (const UCHAR *)src + done, n);
        done += n;
    }
    return done;
}

/*
 * touching a guard page raises STATUS_GUARD_PAGE_VIOLATION, or STATUS_STACK_OVERFLOW for the thread's own
 * last stack guard. the guard is removed, so the access runs again if the exception is handled.
 */
/* a guard of the thread's own stack above its last one: the stack grows instead of faulting, see MiGrowUserStack. */
static BOOLEAN stack_growth_guard(ULONG_PTR page)
{
    PETHREAD thread = PsCurrentThread();
    ULONG_PTR top = (ULONG_PTR)thread->UserStackAllocation + thread->UserStackSize;
    return thread->UserStackGuard && page > thread->UserStackGuard && page < top &&
           thread->Tcb.ApcStateProcess == thread->Tcb.Process;
}

static void guard_fault(ULONG_PTR page)
{
    PETHREAD thread = PsCurrentThread();
    BOOLEAN own = page == thread->UserStackGuard && thread->Tcb.ApcStateProcess == thread->Tcb.Process;
    thread->Tcb.FaultStatus = own ? STATUS_STACK_OVERFLOW : STATUS_GUARD_PAGE_VIOLATION;
}

/*
 * like MiCheckForUserStackOverflow: the guard page the thread touched is now plain committed memory, the three pages
 * below it are the guard down to the last guard, and the stack limit of the teb follows. the access
 * runs again without an exception.
 */
static void grow_user_stack(ULONG_PTR page)
{
    PETHREAD thread = PsCurrentThread();
    PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
    ULONG_PTR next = page - MI_STACK_GUARD_SIZE;
    if (next <= thread->UserStackGuard)
        next = thread->UserStackGuard + PAGE_SIZE;
    if (next < page) {
        PVOID at = (PVOID)next;
        SIZE_T size = page - next;
        MiAllocateVirtual(process, &at, &size, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    }
    PUCHAR teb = (PUCHAR)thread->Tcb.Teb;
    if (PspIsWow64Process(process)) {
        ULONG limit = (ULONG)page;
        KiCopyToUser(teb + MIME_TEB_SIZE + 8, &limit, sizeof(limit));
    } else {
        PVOID limit = (PVOID)page;
        KiCopyToUser(&((PTEB)teb)->NtTib.StackLimit, &limit, sizeof(limit));
    }
}

static BOOLEAN access_fault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode)
{
    if (va > MM_HIGHEST_USER_ADDRESS)
        return FALSE;
    PEPROCESS process = (PEPROCESS)KiCurrentThread()->ApcStateProcess;
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!as->Root)
        return FALSE;
    ULONG_PTR page = va & ~(ULONG_PTR)(PAGE_SIZE - 1);
    /* a page that is not present yet only needs the gate, and taking the lock would close it for everyone else. */
    if (MmFastFault(va, write, execute))
        return TRUE;
    MmFaultStats[KeGetCurrentPrcb()->Number].Slow++;
    KiCurrentThread()->FaultStatus = 0;
    MiLockAddressSpace(as);
    BOOLEAN handled = FALSE, grow = FALSE;
    as->PageFaults++;
    PMMVAD vad = MiFindVad(as, page);
    ULONG64 *pte = vad && !(vad->Flags & MM_VAD_PLACEHOLDER) ? MiWalk(as->Root, page, FALSE, 3) : nullptr;
    if (pte) {
        ULONG64 v = *pte;
        ULONG sw_protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (!(v & MM_PTE_VALID) && (v & MM_SWPTE_SECTION) && vad->Section && (sw_protect & 0xFF) != PAGE_NOACCESS &&
            !(sw_protect & PAGE_GUARD)) {
            handled = section_fault(as, vad, page, pte, sw_protect);
        } else if (!(v & MM_PTE_VALID) && (v & MM_SWPTE_COMMITTED)) {
            ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
            if ((protect & 0xFF) != PAGE_NOACCESS && !(protect & PAGE_GUARD)) {
                handled = demand_zero(as, vad, page, pte, v, write);
            } else if (protect & PAGE_GUARD) {
                *pte = sw_pte(protect & ~PAGE_GUARD) | (v & MM_SWPTE_SECTION);
                if (stack_growth_guard(page)) {
                    grow = TRUE;
                    handled = TRUE;
                } else {
                    guard_fault(page);
                    handled = FALSE;
                }
            }
        } else if ((v & MM_PTE_VALID) && write && (v & MM_PTE_SW_COW)) {
            ULONG64 pa = MmAllocatePageNoZero();
            if (pa) {
                memcpy(HalPhysmap(pa), HalPhysmap(v & MM_PTE_ADDR_MASK), PAGE_SIZE);
                if (v & MM_PTE_SW_CODE)
                    code_log(as, page);
                ULONG64 attrs = MI_PTE_SET_RW(v & ~(MM_PTE_ADDR_MASK | MM_PTE_SW_COW | MM_PTE_SW_SHARED | MM_PTE_SW_CODE));
                MiWritePte(as->Root, page, pa | attrs, as->Asid);
                as->CommittedPages++;
                MiAddResident(1);
                handled = TRUE;
            }
        } else if ((v & MM_PTE_VALID) && write && (v & MM_PTE_SW_CODE)) {
            /* a write watch the page also has makes it writable on the next fault. */
            code_log(as, page);
            ULONG64 attrs = v & ~MM_PTE_SW_CODE;
            MiUpgradePte(as->Root, page, (attrs & MM_PTE_SW_WATCH) ? attrs : MI_PTE_SET_RW(attrs), as->Asid);
            handled = TRUE;
        } else if ((v & MM_PTE_VALID) && write && (v & MM_PTE_SW_WATCH)) {
            /* first write since the last reset: the page is now writable. */
            MiUpgradePte(as->Root, page, MI_PTE_SET_RW(v & ~MM_PTE_SW_WATCH), as->Asid);
            handled = TRUE;
        } else if ((v & MM_PTE_VALID) && mode == UserMode && !MI_PTE_IS_USER(v) && (v & MM_PTE_SW_TRIMMED)) {
            /* a trimmed page comes back into the working set, a soft fault. */
            MiWritePte(as->Root, page, MI_PTE_SET_USER(v & ~MM_PTE_SW_TRIMMED), as->Asid);
            handled = TRUE;
        } else if ((v & MM_PTE_VALID) && mode == UserMode && !MI_PTE_IS_USER(v) && MI_PTE_IS_GUARD(v)) {
            /* like Windows, the first touch of a guard page faults and removes the guard. */
            MiWritePte(as->Root, page, MI_PTE_CLEAR_GUARD(v), as->Asid);
            if (stack_growth_guard(page)) {
                grow = TRUE;
                handled = TRUE;
            } else {
                guard_fault(page);
                handled = FALSE;
            }
        } else if ((v & MM_PTE_VALID) && (mode == KernelMode || MI_PTE_IS_USER(v)) &&
                   (!write || !MI_PTE_IS_RO(v)) && (!execute || MI_PTE_USER_EXEC(v))) {
            /* another thread fixed the page first. */
            handled = TRUE;
        }
    }
    MiUnlockAddressSpace(as);
    if (grow)
        grow_user_stack(page);
    return handled;
}

/*
 * returns FALSE for real access violations. a user fault that found no page the host would back waits for one, as a
 * fault waits for the paging file on windows, and fails with STATUS_NO_MEMORY after about a second.
 */
#define FAULT_MEMORY_TRIES 20
#define FAULT_MEMORY_WAIT_HNS (50ll * 10000)

BOOLEAN MmAccessFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode)
{
    ULONG64 failures = MmGetAllocationFailures();
    for (ULONG tries = 0;; tries++) {
        if (access_fault(va, write, execute, mode))
            return TRUE;
        if (mode != UserMode || MmGetAllocationFailures() == failures)
            return FALSE;
        if (tries == FAULT_MEMORY_TRIES) {
            KiCurrentThread()->FaultStatus = STATUS_NO_MEMORY;
            return FALSE;
        }
        failures = MmGetAllocationFailures();
        LARGE_INTEGER wait = {.QuadPart = -FAULT_MEMORY_WAIT_HNS};
        KeDelayExecutionThread(KernelMode, FALSE, &wait);
    }
}

/*
 * the user half, which the last thread takes along. the root stays until the process object goes, see
 * MmFreeAddressSpaceRoot: a thread attached to the process loads it, and on x64 its kernel half maps the kernel.
 */
void MmDeleteAddressSpace(PMMADDRESS_SPACE as)
{
    if (!as->Root)
        return;
    /* another process may be reading or changing it, and the fast queries read it without the kernel lock. */
    MiLockAddressSpace(as);
    while (as->VadCount) {
        PMMVAD v = as->Vads[as->VadCount - 1];
        release_pages(as, v->StartVa, v->EndVa);
        MiRemoveVad(as, v);
        vad_destroy(v);
    }
    /* what the releases did not count off. */
    MiAddResident(-(LONG64)as->CommittedPages);
    if (as->Spans) {
        ExFreePool(as->Spans);
        as->Spans = nullptr;
        as->Vads = nullptr;
        as->VadCapacity = 0;
    }
    return_commit(as, as->CommitCharge);
    if (as->CodeLogPage) {
        as->CodeLog = nullptr;
        MmFreePage(as->CodeLogPage);
        as->CodeLogPage = 0;
    }
    free_tables(as->Root, 0);
    memset(as->Root, 0, MI_USER_ROOT_ENTRIES * sizeof(ULONG64));
    MiFlushTlbAsid(as->Asid);
    as->Root = nullptr;
    MiUnlockAddressSpace(as);
}

void MmFreeAddressSpaceRoot(PMMADDRESS_SPACE as)
{
    MmDeleteAddressSpace(as);
    if (as->RootPhys)
        MmFreePage(as->RootPhys);
    as->RootPhys = 0;
}

/* -------------------------------------------------------- system services */

static NTSTATUS reference_process(HANDLE handle, ACCESS_MASK access, PEPROCESS *process)
{
    return PsReferenceProcessHandle(handle, access, process);
}

/* -------------------------------------------------------- write watching */

#define MI_WATCH_WRITABLE (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)

static inline BOOLEAN page_is_written(ULONG64 pte)
{
    return (pte & MM_PTE_VALID) && !(pte & (MM_PTE_SW_WATCH | MM_PTE_SW_SHARED)) &&
           (MiPteToProtect(pte) & MI_WATCH_WRITABLE);
}

/*
 * collects written pages of [*resume, end), up to a maximum, resetting them when asked. *resume is where
 * to continue, 0 at the end. only reported pages are reset.
 */
static SIZE_T gather_written(PMMADDRESS_SPACE as, ULONG_PTR *resume, ULONG_PTR end, BOOLEAN reset, ULONG_PTR *out,
                             SIZE_T maximum)
{
    ULONG64 values[512];
    SIZE_T n = 0, count;
    ULONG_PTR va;
    PTE_CURSOR c = {as->Root, *resume, end};
    *resume = 0;
    for (ULONG64 *pte; n < maximum && (pte = cursor_next(&c, &va, &count));) {
        BOOLEAN any = FALSE;
        SIZE_T taken = count;
        for (SIZE_T i = 0; i < count; i++) {
            values[i] = pte[i];
            if (n < maximum && page_is_written(pte[i])) {
                out[n++] = va + (i << PAGE_SHIFT);
                values[i] = MI_PTE_SET_RO(pte[i]) | MM_PTE_SW_WATCH;
                any = TRUE;
                if (n == maximum) {
                    taken = i + 1;
                    *resume = va + (taken << PAGE_SHIFT);
                    break;
                }
            }
        }
        if (reset && any)
            MiRewritePtes(pte, va, taken, values, as->Asid);
        if (*resume)
            break;
    }
    if (*resume >= end)
        *resume = 0;
    return n;
}

/* null unless the whole range lies in one write watched vad. */
static PMMVAD watched_vad(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    PMMVAD vad = MiFindVad(as, start);
    return vad && (vad->Flags & MM_VAD_WRITE_WATCH) && end <= vad->EndVa ? vad : nullptr;
}

NTSTATUS NTAPI NtGetWriteWatch(HANDLE process_handle, ULONG flags, PVOID base, SIZE_T size, PVOID *addresses,
                               PULONG_PTR count, PULONG granularity)
{
    if (flags & ~(ULONG)WRITE_WATCH_FLAG_RESET)
        return STATUS_INVALID_PARAMETER_2;
    ULONG_PTR requested;
    if (!NT_SUCCESS(KiCopyFromUser(&requested, count, sizeof(requested))))
        return STATUS_ACCESS_VIOLATION;
    ULONG_PTR last = (ULONG_PTR)base + size - 1;
    if (!size || last < (ULONG_PTR)base || last > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER_4;
    if (!requested || !addresses)
        return STATUS_INVALID_PARAMETER_5;
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR start = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1), end = ROUND_TO_PAGES((ULONG_PTR)base + size);
    ULONG_PTR chunk[256], resume = start, done = 0;
    do {
        MiLockAddressSpace(as);
        SIZE_T room = requested - done < 256 ? requested - done : 256;
        SIZE_T n = 0;
        if (!watched_vad(as, start, end))
            status = STATUS_INVALID_PARAMETER;
        else
            n = gather_written(as, &resume, end, (flags & WRITE_WATCH_FLAG_RESET) != 0, chunk, room);
        MiUnlockAddressSpace(as);
        if (!NT_SUCCESS(status))
            break;
        if (n)
            status = KiCopyToUser(addresses + done, chunk, n * sizeof(ULONG_PTR));
        done += n;
    } while (NT_SUCCESS(status) && resume && done < requested);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        ULONG page = PAGE_SIZE;
        KiCopyToUser(count, &done, sizeof(done));
        if (granularity)
            KiCopyToUser(granularity, &page, sizeof(page));
    }
    return status;
}

NTSTATUS NTAPI NtResetWriteWatch(HANDLE process_handle, PVOID base, SIZE_T size)
{
    ULONG_PTR last = (ULONG_PTR)base + size - 1;
    if (!size || last < (ULONG_PTR)base || last > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER_3;
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR start = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1), end = ROUND_TO_PAGES((ULONG_PTR)base + size);
    ULONG_PTR chunk[256], resume = start;
    MiLockAddressSpace(as);
    if (!watched_vad(as, start, end))
        status = STATUS_INVALID_PARAMETER;
    else
        do
            gather_written(as, &resume, end, TRUE, chunk, 256);
        while (resume);
    MiUnlockAddressSpace(as);
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE process_handle, PVOID *base, ULONG_PTR zero_bits, PSIZE_T size,
                                       ULONG type, ULONG protect)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    /* the low bits of the type hold a preferred node plus one, and the machine has one node. */
    if ((type & 0x7F) > 1) {
        ObDereferenceObject(process);
        return STATUS_INVALID_PARAMETER;
    }
    /* new executable memory counts as dynamic code. */
    if ((type & (MEM_COMMIT | MEM_RESERVE)) && (protect & EXECUTABLE_PROTECTION) && PsDynamicCodeBlocked(process))
        status = STATUS_DYNAMIC_CODE_BLOCKED;
    else
        status = MiAllocateVirtualEx(process, &b, &s, type & ~0x7Fu, protect, zero_bits, nullptr);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI NtAllocateVirtualMemoryEx(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG type, ULONG protect,
                                         PMEM_EXTENDED_PARAMETER parameters, ULONG count)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    MI_EXTENDED ext;
    NTSTATUS status = MiCaptureExtendedParameters(parameters, count, 0x7E, KeGetPreviousMode() == UserMode, &ext);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (ext.Node > 1)
        status = STATUS_INVALID_PARAMETER;
    else if ((type & (MEM_COMMIT | MEM_RESERVE)) && (protect & EXECUTABLE_PROTECTION) && PsDynamicCodeBlocked(process))
        status = STATUS_DYNAMIC_CODE_BLOCKED;
    else
        status = MiAllocateVirtualEx(process, &b, &s, type, protect, 0, &ext);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI ZwAllocateVirtualMemory(HANDLE process, PVOID *base, ULONG_PTR zero_bits, PSIZE_T size, ULONG type,
                                       ULONG protect)
{
    PEPROCESS p;
    NTSTATUS status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = MiAllocateVirtualEx(p, base, size, type, protect, zero_bits, nullptr);
    ObDereferenceObject(p);
    return status;
}

/* the checks NtFreeVirtualMemory makes before looking at the process, in Windows' order. */
static NTSTATUS validate_free(ULONG_PTR base, SIZE_T size, ULONG *type)
{
    ULONG t = *type;
    if ((t & MEM_DECOMMIT) == MEM_DECOMMIT && (t & MEM_RELEASE) == MEM_RELEASE)
        return STATUS_INVALID_PARAMETER_4;
    if (!(t & (MEM_DECOMMIT | MEM_RELEASE)))
        return STATUS_INVALID_PARAMETER_4;
    ULONG_PTR last = base + (size ? size - 1 : 0);
    if (last < base || last > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN preserve = (t & MEM_PRESERVE_PLACEHOLDER) != 0, coalesce = (t & MEM_COALESCE_PLACEHOLDERS) != 0;
    if (preserve && coalesce)
        return STATUS_INVALID_PARAMETER;
    if (preserve || coalesce) {
        if ((t & ~(ULONG)(MEM_PRESERVE_PLACEHOLDER | MEM_COALESCE_PLACEHOLDERS)) != MEM_RELEASE)
            return STATUS_INVALID_PARAMETER_4;
        if (!size || (size & (PAGE_SIZE - 1)))
            return STATUS_INVALID_PARAMETER_3;
        if (base & (PAGE_SIZE - 1))
            return STATUS_INVALID_PARAMETER_2;
    }
    if (t & ~(ULONG)(MEM_DECOMMIT | MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER | MEM_COALESCE_PLACEHOLDERS))
        return STATUS_INVALID_PARAMETER_4;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG type)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = validate_free((ULONG_PTR)b, s, &type);
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    status = MiFreeVirtual(process, &b, &s, type);
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI ZwFreeVirtualMemory(HANDLE process, PVOID *base, PSIZE_T size, ULONG type)
{
    PEPROCESS p;
    NTSTATUS status = validate_free((ULONG_PTR)*base, *size, &type);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandle(process, PsProcessTypeObject, KernelMode, (PVOID *)&p, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    status = MiFreeVirtual(p, base, size, type);
    ObDereferenceObject(p);
    return status;
}

static BOOLEAN region_is_image(PEPROCESS process, PVOID base)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    MiLockAddressSpace(as);
    PMMVAD vad = (ULONG_PTR)base <= MM_HIGHEST_USER_ADDRESS ? MiFindVad(as, (ULONG_PTR)base) : nullptr;
    BOOLEAN image = vad && vad->Type == MiVadImage;
    MiUnlockAddressSpace(as);
    return image;
}

NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG protect, PULONG old)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    ULONG checked = protect & ~(ULONG)PAGE_TARGETS_INVALID;
    if (!MiProtectionIsValid(checked))
        return STATUS_INVALID_PAGE_PROTECTION;
    ULONG_PTR last = (ULONG_PTR)b + s - 1;
    if (s == 0 || last < (ULONG_PTR)b || last > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    /* code may become data or stay code, but nothing else may become code. */
    if ((protect & EXECUTABLE_PROTECTION) && PsDynamicCodeBlocked(process) &&
        ((protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) || !region_is_image(process, b))) {
        ObDereferenceObject(process);
        return STATUS_DYNAMIC_CODE_BLOCKED;
    }
    ULONG o = PAGE_NOACCESS;
    if ((protect & PAGE_TARGETS_INVALID) && !(protect & 0xF0)) {
        status = STATUS_INVALID_PARAMETER_4;
    } else {
        PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
        if (&process->Pcb != saved)
            KiAttachProcess(&process->Pcb);
        status = MiProtectVirtual(process, &b, &s, checked, &o);
        if (&process->Pcb != saved)
            KiAttachProcess(saved);
    }
    ObDereferenceObject(process);
    /* Windows returns the base, size and old protection whether the change worked or not. */
    KiCopyToUser(base, &b, sizeof(b));
    KiCopyToUser(size, &s, sizeof(s));
    if (old)
        KiCopyToUser(old, &o, sizeof(o));
    return status;
}

/*
 * a page backed by memory is valid, with its protection and the normal priority 5. a committed page not
 * touched yet is not in the working set. view pages are shared unless they belong to a private allocation.
 */
static BOOLEAN page_locked(PEPROCESS process, ULONG_PTR page);

static NTSTATUS query_working_set_ex(PEPROCESS process, PVOID info, SIZE_T length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (length < 16)
        return STATUS_INFO_LENGTH_MISMATCH;
    PUCHAR entry = info;
    for (SIZE_T i = 0; i < length / 16; i++, entry += 16) {
        ULONG_PTR va;
        NTSTATUS status = KiCopyFromUser(&va, entry, sizeof(va));
        if (!NT_SUCCESS(status))
            return status;
        ULONG64 attributes = 0;
        if (va <= MM_HIGHEST_USER_ADDRESS) {
            MiLockAddressSpace(as);
            PMMVAD vad = MiFindVad(as, va);
            if (vad && !(vad->Flags & MM_VAD_PLACEHOLDER)) {
                ULONG64 pte = MiReadPte(as->Root, va & ~(ULONG_PTR)(PAGE_SIZE - 1));
                ULONG prot = pte_committed(pte) ? MiPteToProtect(pte) : 0;
                if ((pte & MM_PTE_VALID) && !(pte & MM_PTE_SW_TRIMMED) && !(prot & (PAGE_NOACCESS | PAGE_GUARD))) {
                    attributes = 1 | (ULONG64)(prot & 0x7FF) << 4 | 5ull << 24;
                    if (vad->Type != MiVadPrivate)
                        attributes |= 1ull << 1 | 1ull << 15;
                    if (page_locked(process, va & ~(ULONG_PTR)(PAGE_SIZE - 1)))
                        attributes |= 1ull << 22; /* Locked */
                }
            }
            MiUnlockAddressSpace(as);
        }
        status = KiCopyToUser(entry + 8, &attributes, sizeof(attributes));
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

/* working set entries use the memory manager's own protection numbering. */
static ULONG working_set_protection(ULONG protect)
{
    ULONG mask;
    switch (protect & 0xFF) {
    case PAGE_READONLY: mask = 1; break;
    case PAGE_EXECUTE: mask = 2; break;
    case PAGE_EXECUTE_READ: mask = 3; break;
    case PAGE_READWRITE: mask = 4; break;
    case PAGE_WRITECOPY: mask = 5; break;
    case PAGE_EXECUTE_READWRITE: mask = 6; break;
    case PAGE_EXECUTE_WRITECOPY: mask = 7; break;
    default: mask = 0; break;
    }
    if (protect & PAGE_NOCACHE)
        mask |= 8;
    if (protect & PAGE_GUARD)
        mask |= 0x10;
    return mask;
}

/* resident pages from *resume on, up to maximum. *resume is where to continue, 0 at the end. */
static SIZE_T gather_working_set(PMMADDRESS_SPACE as, ULONG_PTR *resume, ULONG_PTR *out, SIZE_T maximum)
{
    SIZE_T n = 0;
    if (!maximum)
        return 0;
    for (ULONG i = vad_lower_bound(as, *resume); i < as->VadCount; i++) {
        PMMVAD vad = as->Vads[i];
        if (vad->Flags & MM_VAD_PLACEHOLDER)
            continue;
        PTE_CURSOR c = {as->Root, vad->StartVa > *resume ? vad->StartVa : *resume, vad->EndVa};
        SIZE_T count;
        ULONG_PTR va;
        for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));)
            for (SIZE_T k = 0; k < count; k++) {
                if (!(pte[k] & MM_PTE_VALID) || (pte[k] & MM_PTE_SW_TRIMMED))
                    continue;
                ULONG_PTR flags = working_set_protection(MiPteToProtect(pte[k])) | 1u << 5;
                if (vad->Type != MiVadPrivate)
                    flags |= 1u << 8;
                out[n++] = (va + (k << PAGE_SHIFT)) | flags;
                if (n == maximum) {
                    *resume = va + ((k + 1) << PAGE_SHIFT);
                    return n;
                }
            }
    }
    *resume = 0;
    return n;
}

/*
 * a count, then each page's address with the protection, share count and a shared bit in the low bits.
 * a buffer too small gets the count so the caller can ask again. pages are gathered a few at a time and
 * copied out with the lock released, since the copy can fault.
 */
static NTSTATUS query_working_set(PEPROCESS process, PVOID info, SIZE_T length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (length < sizeof(ULONG_PTR))
        return STATUS_INFO_LENGTH_MISMATCH;
    SIZE_T capacity = (length - sizeof(ULONG_PTR)) / sizeof(ULONG_PTR), total = 0;
    ULONG_PTR chunk[256], resume = 0;
    MiLockAddressSpace(as);
    do
        total += gather_working_set(as, &resume, chunk, 256);
    while (resume);
    MiUnlockAddressSpace(as);
    NTSTATUS status = KiCopyToUser(info, &total, sizeof(total));
    if (!NT_SUCCESS(status))
        return status;
    if (total > capacity)
        return STATUS_INFO_LENGTH_MISMATCH;
    SIZE_T written = 0;
    resume = 0;
    do {
        MiLockAddressSpace(as);
        SIZE_T n = gather_working_set(as, &resume, chunk, capacity - written < 256 ? capacity - written : 256);
        MiUnlockAddressSpace(as);
        if (n) {
            status = KiCopyToUser((PULONG_PTR)info + 1 + written, chunk, n * sizeof(ULONG_PTR));
            if (!NT_SUCCESS(status))
                return status;
        }
        written += n;
    } while (resume && written < capacity);
    return STATUS_SUCCESS;
}

/* ProcessVmCounters in its three Windows sizes: plain, with private bytes, and with the private working set. */
ULONG MmQueryVmCounters(PEPROCESS process, PVOID out, ULONG length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    SIZE_T v[14] = {};
    MiLockAddressSpace(as);
    SIZE_T virtual_size = 0;
    for (ULONG i = 0; i < as->VadCount; i++)
        virtual_size += as->Spans[i].End - as->Spans[i].Start;
    SIZE_T working = as->CommittedPages * PAGE_SIZE, peak_working = as->PeakCommittedPages * PAGE_SIZE;
    SIZE_T charge = as->CommitCharge * PAGE_SIZE, peak_charge = as->PeakCommitCharge * PAGE_SIZE;
    SIZE_T faults = as->PageFaults;
    MiUnlockAddressSpace(as);
    if (virtual_size > as->PeakVirtualSize)
        as->PeakVirtualSize = virtual_size;
    if (peak_working < working)
        peak_working = working;
    v[0] = as->PeakVirtualSize;
    v[1] = virtual_size;
    v[2] = faults;
    v[3] = peak_working;
    v[4] = working;
    v[9] = charge;
    v[10] = peak_charge;
    v[11] = charge;
    v[12] = working;
    ULONG size = length >= 112 ? 112 : length >= 96 ? 96 : 88;
    memcpy(out, v, size);
    return size;
}

NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE process_handle, PVOID base, MEMORY_INFORMATION_CLASS klass, PVOID info,
                                    SIZE_T length, PSIZE_T returned)
{
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_QUERY_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == MemoryBasicInformation) {
        MEMORY_BASIC_INFORMATION mbi;
        if (length < sizeof(mbi)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            status = MiQueryVirtual(process, base, &mbi);
            if (NT_SUCCESS(status))
                status = KiCopyToUser(info, &mbi, sizeof(mbi));
            SIZE_T n = sizeof(mbi);
            if (returned)
                KiCopyToUser(returned, &n, sizeof(n));
        }
    } else if (klass == MemoryImageInformation) {
        /* the image view holding the address. like Windows, any other memory gets zeros. */
        MEMORY_IMAGE_INFORMATION mi = {};
        PMMADDRESS_SPACE as = &process->AddressSpace;
        if (length < sizeof(mi)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            MiLockAddressSpace(as);
            PMMVAD vad = (ULONG_PTR)base <= MM_HIGHEST_USER_ADDRESS ? MiFindVad(as, (ULONG_PTR)base) : nullptr;
            if (!vad) {
                status = STATUS_INVALID_ADDRESS;
            } else if (vad->Type == MiVadImage && vad->Section) {
                mi.ImageBase = (PVOID)vad->StartVa;
                mi.SizeOfImage = (SIZE_T)vad->Section->Size;
                if (vad->SectionOffset + (vad->EndVa - vad->StartVa) < vad->Section->Size)
                    mi.ImageFlags |= MEMORY_IMAGE_PARTIAL_MAP;
            }
            MiUnlockAddressSpace(as);
            if (NT_SUCCESS(status)) {
                status = KiCopyToUser(info, &mi, sizeof(mi));
                SIZE_T n = sizeof(mi);
                if (returned)
                    KiCopyToUser(returned, &n, sizeof(n));
            }
        }
    } else if (klass == MemoryWorkingSetExInformation) {
        status = query_working_set_ex(process, info, length);
    } else if (klass == MemoryWorkingSetInformation) {
        status = query_working_set(process, info, length);
    } else if (klass == MemoryMappedFilenameInformation) {
        PMMADDRESS_SPACE as = &process->AddressSpace;
        MiLockAddressSpace(as);
        PMMVAD vad = MiFindVad(as, (ULONG_PTR)base);
        PSECTION section = vad ? vad->Section : nullptr;
        if (section)
            ObReferenceObject(section);
        MiUnlockAddressSpace(as);
        if (!section || !section->FileName.Buffer) {
            status = STATUS_INVALID_ADDRESS;
        } else {
            SIZE_T need = sizeof(UNICODE_STRING) + section->FileName.Length + sizeof(WCHAR);
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            if (length < need) {
                status = STATUS_BUFFER_OVERFLOW;
            } else {
                UNICODE_STRING us;
                us.Length = section->FileName.Length;
                us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
                us.Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
                status = KiCopyToUser(info, &us, sizeof(us));
                if (NT_SUCCESS(status))
                    status = KiCopyToUser(us.Buffer, section->FileName.Buffer, us.Length + sizeof(WCHAR));
            }
        }
        if (section)
            ObDereferenceObject(section);
    } else {
        status = STATUS_INVALID_INFO_CLASS;
    }
    ObDereferenceObject(process);
    return status;
}

/*
 * mime never pages out or trims the working set, so prefetching, priorities and working set removal only
 * check their arguments. call targets are not tracked either, since no image is checked against them.
 */
/* -------------------------------------------------------- translated code watching */

static BOOLEAN code_watchable(ULONG64 v)
{
    return (v & MM_PTE_VALID) && MI_PTE_IS_USER(v) && !(v & MM_PTE_SW_CODE) &&
           (MiPteToProtect(v) & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
}

static void watch_code(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    ULONG64 values[512];
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));) {
        BOOLEAN any = FALSE;
        for (SIZE_T i = 0; i < count; i++) {
            values[i] = pte[i];
            if (code_watchable(pte[i])) {
                values[i] = MI_PTE_SET_RO(pte[i]) | MM_PTE_SW_CODE;
                any = TRUE;
            }
        }
        if (any)
            MiRewritePtes(pte, va, count, values, as->Asid);
    }
}

/* the log lives in a page of its own above 4 GB, out of the way of 32-bit programs. */
static NTSTATUS map_code_log(PMMADDRESS_SPACE as)
{
    if (as->CodeLog)
        return STATUS_SUCCESS;
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG_PTR va;
    NTSTATUS status = MiFindFreeRangeEx(as, PAGE_SIZE, 0, 0x100000000ull, 0, TRUE, &va);
    PMMVAD vad = NT_SUCCESS(status) ? vad_create(va, va + PAGE_SIZE, MiVadShared, PAGE_READONLY, 0) : nullptr;
    if (vad && !NT_SUCCESS(MiInsertVad(as, vad))) {
        vad_destroy(vad);
        vad = nullptr;
    }
    if (!vad) {
        MmFreePage(pa);
        return NT_SUCCESS(status) ? STATUS_INSUFFICIENT_RESOURCES : status;
    }
    MiMapPage(as->Root, va, pa, MiProtectToPte(PAGE_READONLY, TRUE) | MM_PTE_SW_SHARED);
    as->CodeLogPage = pa;
    as->CodeLogVa = va;
    as->CodeLog = HalPhysmap(pa);
    return STATUS_SUCCESS;
}

static NTSTATUS code_watch_service(HANDLE process_handle, ULONG klass, ULONG_PTR count, PVOID entries, PVOID info,
                                   ULONG length)
{
    if (process_handle != NtCurrentProcess())
        return STATUS_INVALID_PARAMETER_1;
    BOOLEAN user = KeGetPreviousMode() == UserMode;
    PMMADDRESS_SPACE as = &PsCurrentProcess()->AddressSpace;
    if (klass == MIME_VM_CODE_LOG) {
        if (length != sizeof(PVOID) || !info)
            return STATUS_INVALID_PARAMETER_6;
        MiLockAddressSpace(as);
        NTSTATUS status = map_code_log(as);
        ULONG_PTR va = as->CodeLogVa;
        MiUnlockAddressSpace(as);
        if (!NT_SUCCESS(status))
            return status;
        return user ? KiCopyToUser(info, &va, sizeof(va)) : (*(ULONG_PTR *)info = va, STATUS_SUCCESS);
    }
    for (ULONG_PTR i = 0; i < count; i++) {
        MEMORY_RANGE_ENTRY r;
        NTSTATUS status = user ? KiCopyFromUser(&r, (PUCHAR)entries + i * sizeof(r), sizeof(r))
                               : (r = ((PMEMORY_RANGE_ENTRY)entries)[i], STATUS_SUCCESS);
        if (!NT_SUCCESS(status))
            return status;
        ULONG_PTR start = (ULONG_PTR)r.VirtualAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
        ULONG_PTR end = ROUND_TO_PAGES((ULONG_PTR)r.VirtualAddress + r.NumberOfBytes);
        if (!r.NumberOfBytes || end <= start || end - 1 > MM_HIGHEST_USER_ADDRESS)
            return STATUS_INVALID_PARAMETER_4;
        MiLockAddressSpace(as);
        watch_code(as, start, end);
        MiUnlockAddressSpace(as);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetInformationVirtualMemory(HANDLE process_handle, ULONG klass, ULONG_PTR count, PVOID entries,
                                             PVOID info, ULONG length)
{
    if (klass == MIME_VM_CODE_WATCH || klass == MIME_VM_CODE_LOG)
        return code_watch_service(process_handle, klass, count, entries, info, length);
    if (klass >= VmInformationClassMax)
        return STATUS_INVALID_PARAMETER_2;
    if (klass == VmCfgCallTargetInformation) {
        if (length != 40)
            return STATUS_INVALID_PARAMETER_6;
    } else {
        if (!info)
            return STATUS_INVALID_PARAMETER_5;
        if (length != 4)
            return STATUS_INVALID_PARAMETER_6;
    }
    if (!count)
        return STATUS_INVALID_PARAMETER_3;
    BOOLEAN user = KeGetPreviousMode() == UserMode;
    ULONG flags = 0;
    struct {
        ULONG Entries, Reserved;
        PULONG Processed;
        PVOID Targets, Section;
        ULONG64 Offset;
    } cfg = {};
    if (klass == VmCfgCallTargetInformation) {
        NTSTATUS status = user ? KiCopyFromUser(&cfg, info, sizeof(cfg)) : (memcpy(&cfg, info, sizeof(cfg)), STATUS_SUCCESS);
        if (!NT_SUCCESS(status))
            return status;
        if (!cfg.Entries)
            return STATUS_INVALID_PARAMETER_5;
    } else {
        NTSTATUS status = user ? KiCopyFromUser(&flags, info, sizeof(flags)) : (flags = *(PULONG)info, STATUS_SUCCESS);
        if (!NT_SUCCESS(status))
            return status;
    }
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_SET_INFORMATION | PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (process != PsCurrentProcess() && (klass == VmPageDirtyStateInformation || klass == VmImageHotPatchInformation)) {
        ObDereferenceObject(process);
        return STATUS_INVALID_PARAMETER_1;
    }
    /* every range must be sane. */
    for (ULONG_PTR i = 0; i < count && NT_SUCCESS(status); i++) {
        MEMORY_RANGE_ENTRY r;
        if (user)
            status = KiCopyFromUser(&r, (PUCHAR)entries + i * sizeof(r), sizeof(r));
        else
            r = ((PMEMORY_RANGE_ENTRY)entries)[i];
        if (!NT_SUCCESS(status))
            break;
        ULONG_PTR last = (ULONG_PTR)r.VirtualAddress + r.NumberOfBytes - 1;
        if (!r.NumberOfBytes || last < (ULONG_PTR)r.VirtualAddress || last > MM_HIGHEST_USER_ADDRESS)
            status = STATUS_INVALID_PARAMETER_4;
    }
    if (NT_SUCCESS(status)) {
        switch (klass) {
        case VmPrefetchInformation:
        case VmRemoveFromWorkingSetInformation:
            if (flags & ~1u)
                status = STATUS_INVALID_PARAMETER_5;
            break;
        case VmPagePriorityInformation:
            if (flags > 5)
                status = STATUS_INVALID_PARAMETER_5;
            break;
        case VmCfgCallTargetInformation:
            /* every target counts as processed. */
            if (cfg.Processed && user) {
                ULONG done = cfg.Entries;
                status = KiCopyToUser(cfg.Processed, &done, sizeof(done));
            }
            break;
        case VmPageDirtyStateInformation:
            break;
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
        }
    }
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtReadVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T read)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_READ, &process);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T done = 0;
    if (process == PsCurrentProcess()) {
        PUCHAR tmp = ExAllocatePool2(POOL_FLAG_PAGED, size ? size : 1, KI_TAG('M', 'm', 'R', 'v'));
        if (!tmp) {
            ObDereferenceObject(process);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        status = KiCopyFromUser(tmp, base, size);
        if (NT_SUCCESS(status))
            status = KiCopyToUser(buffer, tmp, size);
        if (NT_SUCCESS(status))
            done = size;
        ExFreePool(tmp);
    } else {
        status = MmCopyVirtualMemory(process, base, PsCurrentProcess(), buffer, size, UserMode, &done);
    }
    if (read)
        KiCopyToUser(read, &done, sizeof(done));
    ObDereferenceObject(process);
    return status;
}

NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T written)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_VM_WRITE, &process);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T done = 0;
    status = MmCopyVirtualMemory(PsCurrentProcess(), buffer, process, base, size, UserMode, &done);
    if (written)
        KiCopyToUser(written, &done, sizeof(done));
    ObDereferenceObject(process);
    return status;
}

#define MM_COPY_CHUNK 0x10000

/*
 * copies between address spaces through a kernel bounce buffer. memory that is not there ends the copy
 * short with STATUS_PARTIAL_COPY, and committed memory that cannot be written is an access violation.
 */
NTSTATUS NTAPI MmCopyVirtualMemory(PEPROCESS from_process, PVOID from, PEPROCESS to_process, PVOID to, SIZE_T size,
                                   KPROCESSOR_MODE mode, PSIZE_T copied)
{
    if (!size) {
        if (copied)
            *copied = 0;
        return STATUS_SUCCESS;
    }
    PUCHAR tmp = ExAllocatePool2(POOL_FLAG_PAGED, size < MM_COPY_CHUNK ? size : MM_COPY_CHUNK, KI_TAG('M', 'm', 'C', 'v'));
    if (!tmp)
        return STATUS_INSUFFICIENT_RESOURCES;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN target_failed = FALSE, by_page = FALSE;
    SIZE_T done = 0;
    while (done < size) {
        SIZE_T n = size - done < MM_COPY_CHUNK ? size - done : MM_COPY_CHUNK;
        if (by_page) {
            /* go page by page to find the failing page and keep the bytes before it. */
            SIZE_T source_room = PAGE_SIZE - (((ULONG_PTR)from + done) & (PAGE_SIZE - 1));
            SIZE_T target_room = PAGE_SIZE - (((ULONG_PTR)to + done) & (PAGE_SIZE - 1));
            if (n > source_room)
                n = source_room;
            if (n > target_room)
                n = target_room;
        }
        KiAttachProcess(&from_process->Pcb);
        status = KiCopyFromUser(tmp, (PUCHAR)from + done, n);
        KiAttachProcess(&to_process->Pcb);
        target_failed = FALSE;
        if (NT_SUCCESS(status)) {
            status = KiCopyToUser((PUCHAR)to + done, tmp, n);
            target_failed = !NT_SUCCESS(status);
        }
        if (!NT_SUCCESS(status)) {
            if (!by_page && n > 1) {
                by_page = TRUE;
                continue;
            }
            break;
        }
        done += n;
    }
    KiAttachProcess(saved);
    ExFreePool(tmp);
    if (copied)
        *copied = done;
    if (status != STATUS_ACCESS_VIOLATION)
        return status;
    /* the write stopped at a page that is committed but not writable. */
    MEMORY_BASIC_INFORMATION mbi;
    if (target_failed && NT_SUCCESS(MiQueryVirtual(to_process, (PUCHAR)to + done, &mbi)) && mbi.State == MEM_COMMIT &&
        !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
        !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return STATUS_ACCESS_VIOLATION;
    return (NTSTATUS)0x8000000D /* STATUS_PARTIAL_COPY. */;
}

/* the code another process wrote, as WriteProcessMemory does with a flush after it, is logged for its translator. */
NTSTATUS NTAPI NtFlushInstructionCache(HANDLE process_handle, PVOID base, SIZE_T size)
{
    KiFlushInstructionCache();
    PEPROCESS process;
    if (process_handle != NtCurrentProcess() && NT_SUCCESS(reference_process(process_handle, PROCESS_VM_WRITE, &process))) {
        PMMADDRESS_SPACE as = &process->AddressSpace;
        if (as->CodeLog) {
            ULONG_PTR start = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1);
            ULONG_PTR end = ROUND_TO_PAGES((ULONG_PTR)base + size);
            MiLockAddressSpace(as);
            if (!base || !size || end <= start || end - start > 64 * PAGE_SIZE)
                code_log(as, MIME_CODE_LOG_ALL);
            else
                for (ULONG_PTR page = start; page < end; page += PAGE_SIZE)
                    code_log(as, page);
            MiUnlockAddressSpace(as);
        }
        ObDereferenceObject(process);
    }
    return STATUS_SUCCESS;
}

/*
 * interrupts every processor running this process in user mode, whose kernel entry publishes its stores,
 * then waits for each to leave user mode.
 */
NTSTATUS NTAPI NtFlushProcessWriteBuffers(void)
{
    PKPROCESS process = KiCurrentThread()->Process;
    ULONG64 targets = 0;
    KiDataSyncBarrier();
    for (ULONG i = 0; i < KeNumberProcessors; i++) {
        PKPRCB p = KiProcessorBlock[i];
        PKPCR pcr = CONTAINING_RECORD(p, KPCR, Prcb);
        if (p != KeGetCurrentPrcb() && pcr->UserMode && p->CurrentThread->Process == process) {
            targets |= 1ull << i;
        }
    }
    if (targets)
        HalSendIpiMask(targets);
    while (targets) {
        ULONG i = (ULONG)__builtin_ctzll(targets);
        PKPCR pcr = CONTAINING_RECORD(KiProcessorBlock[i], KPCR, Prcb);
        if (!__atomic_load_n(&pcr->UserMode, __ATOMIC_ACQUIRE) || KiProcessorBlock[i]->CurrentThread->Process != process)
            targets &= targets - 1;
        else
            KiPause();
    }
    KiDataSyncBarrier();
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ locked pages */

/*
 * mime keeps every page of a process in memory, so locking is the record of what was locked: unlocking a page that is
 * not locked fails as on windows. the range must be committed throughout.
 */
#define MAP_PROCESS 1
#define MAP_SYSTEM 2
#define STATUS_NOT_LOCKED ((NTSTATUS)0xC000002AL)

static BOOLEAN range_committed(PEPROCESS process, ULONG_PTR start, ULONG_PTR end)
{
    for (ULONG_PTR va = start; va < end;) {
        MEMORY_BASIC_INFORMATION info;
        if (!NT_SUCCESS(MiQueryVirtual(process, (PVOID)va, &info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return FALSE;
        va = (ULONG_PTR)info.BaseAddress + info.RegionSize;
    }
    return TRUE;
}

static BOOLEAN page_locked(PEPROCESS process, ULONG_PTR page)
{
    for (ULONG i = 0; i < process->MmLockedCount; i++)
        if (page >= process->MmLockedRanges[2 * i] && page < process->MmLockedRanges[2 * i + 1])
            return TRUE;
    return FALSE;
}

static NTSTATUS add_range(PEPROCESS process, ULONG_PTR start, ULONG_PTR end)
{
    if (process->MmLockedCount == process->MmLockedCapacity) {
        ULONG capacity = process->MmLockedCapacity ? process->MmLockedCapacity * 2 : 16;
        ULONG_PTR *grown = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * 2 * sizeof(ULONG_PTR), KI_TAG('M', 'm', 'L', 'k'));
        if (!grown)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (process->MmLockedRanges) {
            memcpy(grown, process->MmLockedRanges, process->MmLockedCount * 2 * sizeof(ULONG_PTR));
            ExFreePool(process->MmLockedRanges);
        }
        process->MmLockedRanges = grown;
        process->MmLockedCapacity = capacity;
    }
    process->MmLockedRanges[2 * process->MmLockedCount] = start;
    process->MmLockedRanges[2 * process->MmLockedCount + 1] = end;
    process->MmLockedCount++;
    return STATUS_SUCCESS;
}

/* takes [start, end) out of every range, splitting one that holds it in its middle. */
static NTSTATUS remove_range(PEPROCESS process, ULONG_PTR start, ULONG_PTR end)
{
    for (ULONG i = 0; i < process->MmLockedCount;) {
        ULONG_PTR s = process->MmLockedRanges[2 * i], e = process->MmLockedRanges[2 * i + 1];
        if (e <= start || s >= end) {
            i++;
            continue;
        }
        if (s < start && e > end) {
            process->MmLockedRanges[2 * i + 1] = start;
            return add_range(process, end, e);
        }
        if (s < start) {
            process->MmLockedRanges[2 * i + 1] = start;
            i++;
        } else if (e > end) {
            process->MmLockedRanges[2 * i] = end;
            i++;
        } else {
            process->MmLockedCount--;
            process->MmLockedRanges[2 * i] = process->MmLockedRanges[2 * process->MmLockedCount];
            process->MmLockedRanges[2 * i + 1] = process->MmLockedRanges[2 * process->MmLockedCount + 1];
        }
    }
    return STATUS_SUCCESS;
}

/*
 * like windows, VirtualUnlock of pages that are not locked takes them out of the working set: they stay where they
 * are, only hidden from user mode, and the next touch puts them back without an exception, a soft fault. locking puts
 * them back at once. MemoryWorkingSetExInformation tells a trimmed page from a resident one.
 */
static void trim_pages(PEPROCESS process, ULONG_PTR start, ULONG_PTR end, BOOLEAN trim)
{
#if !defined(__x86_64__)
    return;
#endif
    PMMADDRESS_SPACE as = &process->AddressSpace;
    MiLockAddressSpace(as);
    for (ULONG_PTR page = start; page < end; page += PAGE_SIZE) {
        PMMVAD vad = MiFindVad(as, page);
        if (!vad || (vad->Flags & MM_VAD_PLACEHOLDER))
            continue;
        ULONG64 v = MiReadPte(as->Root, page);
        if (!(v & MM_PTE_VALID) || (v & (MM_PTE_SW_GUARD | MM_PTE_SW_CODE)))
            continue;
        if (trim && MI_PTE_IS_USER(v))
            MiWritePte(as->Root, page, MI_PTE_CLEAR_USER(v) | MM_PTE_SW_TRIMMED, as->Asid);
        else if (!trim && (v & MM_PTE_SW_TRIMMED))
            MiWritePte(as->Root, page, MI_PTE_SET_USER(v & ~MM_PTE_SW_TRIMMED), as->Asid);
    }
    MiUnlockAddressSpace(as);
}

static NTSTATUS lock_virtual(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG map_type, BOOLEAN lock)
{
    PVOID b;
    SIZE_T s;
    if (!NT_SUCCESS(KiCopyFromUser(&b, base, sizeof(b))) || !NT_SUCCESS(KiCopyFromUser(&s, size, sizeof(s))))
        return STATUS_ACCESS_VIOLATION;
    if ((map_type & ~(MAP_PROCESS | MAP_SYSTEM)) || !map_type)
        return STATUS_INVALID_PARAMETER;
    if ((map_type & MAP_SYSTEM) && !SeThreadHasPrivilege(4 /* SE_LOCK_MEMORY_PRIVILEGE */))
        return STATUS_PRIVILEGE_NOT_HELD;
    ULONG_PTR start = (ULONG_PTR)PAGE_ALIGN(b), end = ((ULONG_PTR)b + s + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (s == 0 || end <= start || end - 1 > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process;
    NTSTATUS status = reference_process(process_handle, PROCESS_VM_OPERATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    if (lock) {
        status = range_committed(process, start, end) ? add_range(process, start, end) : STATUS_ACCESS_VIOLATION;
        if (NT_SUCCESS(status))
            trim_pages(process, start, end, FALSE);
    } else {
        for (ULONG_PTR page = start; page < end && NT_SUCCESS(status); page += PAGE_SIZE)
            if (!page_locked(process, page))
                status = STATUS_NOT_LOCKED;
        if (NT_SUCCESS(status))
            status = remove_range(process, start, end);
        else if (status == STATUS_NOT_LOCKED)
            trim_pages(process, start, end, TRUE);
    }
    ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        b = (PVOID)start;
        s = end - start;
        KiCopyToUser(base, &b, sizeof(b));
        KiCopyToUser(size, &s, sizeof(s));
    }
    return status;
}

NTSTATUS NTAPI NtLockVirtualMemory(HANDLE process, PVOID *base, PSIZE_T size, ULONG map_type)
{
    return lock_virtual(process, base, size, map_type, TRUE);
}

NTSTATUS NTAPI NtUnlockVirtualMemory(HANDLE process, PVOID *base, PSIZE_T size, ULONG map_type)
{
    return lock_virtual(process, base, size, map_type, FALSE);
}
