/*
 * user address spaces: vads, commit state, page faults and the virtual
 * memory system services.
 *
 * reservations are tracked by vads; commit state lives in the page
 * tables. a committed page that was never touched is an invalid pte
 * carrying MM_SWPTE_COMMITTED and its protection, like MMPTE_SOFTWARE.
 * the vads sit in arrays sorted by address, and every range operation
 * walks the translation tables a table at a time, skipping the holes, so
 * a terabyte reservation costs what its few committed pages cost.
 *
 * a placeholder is a vad that reserves address space and nothing else. it
 * is replaced by an allocation or a view of exactly its range, and an
 * allocation or view made that way can become a placeholder again.
 */
#include "../ntos.h"

#define MM_LOWEST_USER_ADDRESS 0x10000ull
#define MM_HIGHEST_USER_ADDRESS 0x7FFFFFFEFFFFull
#define MM_ALLOCATION_GRANULARITY 0x10000ull
#define MM_TOP_DOWN_START 0x7FF000000000ull
#define MM_TABLE_SPAN (1ull << 21) /* what one translation table of ptes covers */

#define MM_VAD_TAG KI_TAG('M', 'm', 'V', 'd')

/* every allocation type bit mapped views and allocations understand. */
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
 * windows' rules for a protection: one base protection, and at most one
 * of guard, no cache and write combine, none with no access, write
 * combine only with the plain data protections.
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
 * private committed memory is charged against the whole machine's limit,
 * its memory plus the host's swap, so that commit fails when a program
 * asks for more than could ever be backed.
 */
static volatile LONG64 g_commit_pages, g_commit_peak;

static LONG64 commit_limit(void)
{
    return (LONG64)(MmGetTotalPages() + (HalBootInfo->swap_size >> PAGE_SHIFT));
}

SIZE_T MmGetCommittedPages(void)
{
    return (SIZE_T)__atomic_load_n(&g_commit_pages, __ATOMIC_RELAXED);
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
    LONG64 total = __atomic_add_fetch(&g_commit_pages, (LONG64)pages, __ATOMIC_SEQ_CST);
    if (total > commit_limit()) {
        __atomic_sub_fetch(&g_commit_pages, (LONG64)pages, __ATOMIC_SEQ_CST);
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
}

/* ----------------------------------------------------------------- the vad arrays */

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

/* the first vad that ends above va, which is the vad holding it when it starts at or below it. */
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

/* changes the range of a vad that stays in place, as a split or a trim does. */
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

/*
 * the lowest or highest free range of a size inside [lowest, highest],
 * aligned. the search is a scan of the dense array of ranges.
 */
NTSTATUS MiFindFreeRangeEx(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, ULONG_PTR lowest,
                           ULONG_PTR highest, BOOLEAN top_down, ULONG_PTR *out)
{
    ULONG_PTR maximum = as->AllocationLimit ? as->AllocationLimit : MM_HIGHEST_USER_ADDRESS + 1;
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
        /* the first vad ending at or above the limit; one that starts below it holds the limit. */
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
 * the next translation table that exists inside the cursor's range: its
 * first pte in the range, the address that pte maps and how many ptes of
 * the table fall in the range. missing tables, and with them whole tables
 * of tables, are stepped over.
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

/* makes every translation table of a range exist. */
static BOOLEAN ensure_tables(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    for (ULONG_PTR va = start; va < end; va = (va + MM_TABLE_SPAN) & ~(MM_TABLE_SPAN - 1))
        if (!MiWalk(as->Root, va, TRUE, 3))
            return FALSE;
    return TRUE;
}

/* how many pages of a range are committed, private or not. */
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
 * frees the pages of a range and clears its ptes, a table at a time so
 * that the translations are invalidated once for the pages of a table.
 * returns how many pages were committed.
 */
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
        for (SIZE_T i = 0; i < count; i++) {
            ULONG64 e = old[i];
            committed += pte_committed(e);
            if ((e & MM_PTE_VALID) && !(e & MM_PTE_SW_SHARED)) {
                MmFreePage(e & MM_PTE_ADDR_MASK);
                if (as->CommittedPages)
                    as->CommittedPages--;
            }
        }
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

/* the pte of a page with a new protection, keeping what the page holds. */
static ULONG64 reprotect_pte(ULONG64 v, ULONG protect)
{
    if (v & MM_PTE_VALID) {
        ULONG64 attrs = MiProtectToPte(protect, TRUE) | (v & MM_PTE_SW_SHARED);
        /* shared pages become copy on write when they are made writable. */
        if ((v & MM_PTE_SW_SHARED) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            attrs = MI_PTE_SET_RO(attrs) | MM_PTE_SW_COW;
        return (v & MM_PTE_ADDR_MASK) | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    }
    return sw_pte(protect) | (v & MM_SWPTE_SECTION);
}

/* commits and gives a protection to a range whose tables exist. */
static void apply_protection(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end, ULONG protect)
{
    ULONG64 values[512];
    PTE_CURSOR c = {as->Root, start, end};
    SIZE_T count;
    ULONG_PTR va;
    for (ULONG64 *pte; (pte = cursor_next(&c, &va, &count));) {
        for (SIZE_T i = 0; i < count; i++)
            values[i] = reprotect_pte(pte[i], protect);
        MiRewritePtes(pte, va, count, values, as->Asid);
    }
}

/* commits a range of a private vad. */
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
    apply_protection(as, start, end, protect);
    return STATUS_SUCCESS;
}

/* the pages of a range whose contents no longer matter go back to demand zero. */
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
            }
        }
        if (!any)
            continue;
        MiRewritePtes(pte, va, count, values, as->Asid);
        for (SIZE_T i = 0; i < count; i++) {
            if ((old[i] & MM_PTE_VALID) && !(old[i] & MM_PTE_SW_SHARED)) {
                MmFreePage(old[i] & MM_PTE_ADDR_MASK);
                if (as->CommittedPages)
                    as->CommittedPages--;
            }
        }
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

/* the placeholder that spans exactly a range, or null. */
static PMMVAD find_placeholder(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end)
{
    PMMVAD vad = MiFindVad(as, start);
    if (vad && (vad->Flags & MM_VAD_PLACEHOLDER) && vad->StartVa == start && vad->EndVa == end)
        return vad;
    return nullptr;
}

/* puts a vad where another of the same range was, without changing the arrays' layout. */
void MiSwapVad(PMMADDRESS_SPACE as, PMMVAD old, PMMVAD fresh)
{
    ULONG i = vad_index(as, old);
    as->Vads[i] = fresh;
    if (as->Hint == old)
        as->Hint = nullptr;
}

/* merges adjacent placeholders that span exactly a range into one. */
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

/*
 * frees a range of one private vad: the whole vad, or a part that splits it.
 * with preserve the freed range stays reserved as a placeholder.
 */
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
    /* a part: what remains of the vad on each side keeps its flags. */
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

/*
 * reads the extended parameters of a call: each type at most once, and
 * only those the call takes, as a bit mask of the types.
 */
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

/* the highest address a number of zero bits allows, windows' reading of the value. */
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

    /* the allocation type and what goes with it. */
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

    /* the range. */
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
        ULONG_PTR limit = as->AllocationLimit ? as->AllocationLimit - 1 : MM_HIGHEST_USER_ADDRESS;
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
    ExAcquireFastMutex(&as->Lock);
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
                             (placeholder ? MM_VAD_PLACEHOLDER : 0) | (replace ? MM_VAD_REPLACED : 0));
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
        else if ((type & (MEM_RESET | MEM_RESET_UNDO)) && vad->Type != MiVadPrivate)
            status = STATUS_SECTION_PROTECTION;
        else if (type & MEM_RESET)
            reset_range(as, start, end);
        else if (type & MEM_COMMIT)
            status = commit_range(as, vad, start, end, protect);
    }
    ExReleaseFastMutex(&as->Lock);
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

/* frees or decommits what a call asked for, once the vad it falls in is known. */
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
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = coalesce ? nullptr : MiFindVad(as, start);
    if (coalesce) {
        status = coalesce_placeholders(as, start, end);
    } else if (!vad) {
        status = STATUS_MEMORY_NOT_ALLOCATED;
    } else if (!*size && start != vad->StartVa) {
        /* with no size the rest of the allocation goes, so it has to be freed from its base. */
        status = STATUS_FREE_VM_NOT_AT_BASE;
    } else if (*size && end > vad->EndVa) {
        status = STATUS_UNABLE_TO_FREE_VM;
    } else {
        if (!*size)
            end = vad->EndVa;
        status = free_in_vad(as, vad, start, end, type, preserve);
    }
    ExReleaseFastMutex(&as->Lock);
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
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, start);
    if (!vad || end > vad->EndVa) {
        ExReleaseFastMutex(&as->Lock);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    if (vad->Flags & MM_VAD_PLACEHOLDER) {
        /* a placeholder holds no pages, so only the protection it has can be asked for. */
        ExReleaseFastMutex(&as->Lock);
        if (protect != PAGE_NOACCESS)
            return STATUS_CONFLICTING_ADDRESSES;
        if (old)
            *old = PAGE_NOACCESS;
        *base = (PVOID)start;
        *size = end - start;
        return STATUS_SUCCESS;
    }
    /* every page has to be committed before the first one is changed. */
    if (count_committed(as, start, end) != (end - start) >> PAGE_SHIFT) {
        ExReleaseFastMutex(&as->Lock);
        return STATUS_NOT_COMMITTED;
    }
    ULONG first_old = MiPteToProtect(MiReadPte(as->Root, start));
    apply_protection(as, start, end, protect);
    ExReleaseFastMutex(&as->Lock);
    if (old)
        *old = first_old;
    *base = (PVOID)start;
    *size = end - start;
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------- query */

NTSTATUS MiQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    ULONG_PTR va = (ULONG_PTR)base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    if (va > MM_HIGHEST_USER_ADDRESS)
        return STATUS_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, va);
    if (!vad) {
        ULONG i = vad_lower_bound(as, va);
        ULONG_PTR next = i < as->VadCount ? as->Spans[i].Start : MM_HIGHEST_USER_ADDRESS + 1;
        info->BaseAddress = (PVOID)va;
        info->RegionSize = next - va;
        info->State = MEM_FREE;
        info->Protect = PAGE_NOACCESS;
        ExReleaseFastMutex(&as->Lock);
        return STATUS_SUCCESS;
    }
    ULONG64 pte = MiReadPte(as->Root, va);
    BOOLEAN committed = pte_committed(pte);
    ULONG prot = committed ? MiPteToProtect(pte) : 0;
    ULONG_PTR end = va + PAGE_SIZE, limit = vad->EndVa;
    /* the region goes on while pages keep the state and protection of the first. */
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
        for (SIZE_T i = 0; i < count; i++) {
            ULONG64 e = p[i];
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
    ExReleaseFastMutex(&as->Lock);
    return STATUS_SUCCESS;
}

/* maps the first touched page of a section view: the prototype page, or a private copy of a writable image page. */
static BOOLEAN section_fault(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG protect)
{
    PSECTION s = vad->Section;
    ULONG64 index = (vad->SectionOffset + (page - vad->StartVa)) >> PAGE_SHIFT;
    ULONG64 pa = index < s->PageCount ? MiSectionPage(s, (ULONG)index) : 0;
    if (!pa)
        return FALSE;
    BOOLEAN shared = TRUE;
    if ((s->Flags & SEC_IMAGE) && (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        ULONG64 copy = MmAllocatePageNoZero();
        if (!copy)
            return FALSE;
        memcpy(HalPhysmap(copy), HalPhysmap(pa), PAGE_SIZE);
        pa = copy;
        shared = FALSE;
        as->CommittedPages++;
    }
    *pte = pa | MiSectionPteAttrs(protect, pa, shared) | MM_PTE_VALID | MM_PTE_PAGE;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

static BOOLEAN map_zero_page(PMMADDRESS_SPACE as, ULONG64 *pte, ULONG64 v)
{
    ULONG protect = (ULONG)((v & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
    ULONG64 pa = MmAllocatePage();
    if (!pa)
        return FALSE;
    ULONG64 attrs = MiProtectToPte(protect, TRUE) & ~MM_PTE_SW_COW;
    if (protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
        attrs = MI_PTE_SET_RW(attrs);
    *pte = pa | attrs | MM_PTE_VALID | MM_PTE_PAGE;
    if (++as->CommittedPages > as->PeakCommittedPages)
        as->PeakCommittedPages = as->CommittedPages;
    return TRUE;
}

/*
 * a demand zero fault maps the other committed pages of its window too:
 * the 16 KiB host page holding them is backed as a whole anyway, so this
 * saves faults without using more host memory. touching that goes on
 * where the last window ended gets a 64 KiB window, ahead of the program.
 */
#define FAULT_WINDOW (16 * 1024)
#define FAULT_WINDOW_SEQUENTIAL (64 * 1024)

static BOOLEAN demand_zero(PMMADDRESS_SPACE as, PMMVAD vad, ULONG_PTR page, ULONG64 *pte, ULONG64 v)
{
    if (!map_zero_page(as, pte, v))
        return FALSE;
    /* a touch that goes on where one of the recent windows ended continues a sequence. */
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
    /* one translation table holds the whole window, so its ptes sit side by side. */
    ULONG64 *first = pte - ((page - start) >> PAGE_SHIFT);
    for (ULONG_PTR va = start; va < end; va += PAGE_SIZE) {
        ULONG64 *p = first + ((va - start) >> PAGE_SHIFT), w = *p;
        ULONG protect = (ULONG)((w & MM_SWPTE_PROT_MASK) >> MM_SWPTE_PROT_SHIFT);
        if (va == page || (w & MM_PTE_VALID) || !(w & MM_SWPTE_COMMITTED) || (w & MM_SWPTE_SECTION) ||
            (protect & 0xFF) == PAGE_NOACCESS || (protect & PAGE_GUARD))
            continue;
        if (!map_zero_page(as, p, w))
            break;
    }
    if (stream == (ULONG)-1)
        stream = as->NextFaultStream++ & 7;
    as->LastFaultEnd[stream] = end;
    KiDataSyncBarrier();
    KiInstructionBarrier();
    return TRUE;
}

/* resolves demand zero, section and copy on write faults. returns FALSE for real access violations. */
BOOLEAN MmAccessFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode)
{
    if (va > MM_HIGHEST_USER_ADDRESS)
        return FALSE;
    PEPROCESS process = (PEPROCESS)KiCurrentThread()->ApcStateProcess;
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (!as->Root)
        return FALSE;
    ULONG_PTR page = va & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ExAcquireFastMutex(&as->Lock);
    BOOLEAN handled = FALSE;
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
                handled = demand_zero(as, vad, page, pte, v);
            } else if (protect & PAGE_GUARD) {
                *pte = sw_pte(protect & ~PAGE_GUARD) | (v & MM_SWPTE_SECTION);
                handled = FALSE;
            }
        } else if ((v & MM_PTE_VALID) && write && (v & MM_PTE_SW_COW)) {
            ULONG64 pa = MmAllocatePageNoZero();
            if (pa) {
                memcpy(HalPhysmap(pa), HalPhysmap(v & MM_PTE_ADDR_MASK), PAGE_SIZE);
                ULONG64 attrs = MI_PTE_SET_RW(v & ~(MM_PTE_ADDR_MASK | MM_PTE_SW_COW | MM_PTE_SW_SHARED));
                MiWritePte(as->Root, page, pa | attrs, as->Asid);
                as->CommittedPages++;
                handled = TRUE;
            }
        } else if ((v & MM_PTE_VALID) && mode == UserMode && !MI_PTE_IS_USER(v) && MI_PTE_IS_GUARD(v)) {
            /* a guard page: the first touch faults and takes the guard away, like windows. */
            MiWritePte(as->Root, page, MI_PTE_CLEAR_GUARD(v), as->Asid);
            handled = FALSE;
        } else if ((v & MM_PTE_VALID) && (mode == KernelMode || MI_PTE_IS_USER(v)) &&
                   (!write || !MI_PTE_IS_RO(v)) && (!execute || MI_PTE_USER_EXEC(v))) {
            /* another thread, possibly on another processor, fixed the page up first. */
            handled = TRUE;
        }
    }
    ExReleaseFastMutex(&as->Lock);
    return handled;
}

void MmDeleteAddressSpace(PMMADDRESS_SPACE as)
{
    if (!as->Root)
        return;
    while (as->VadCount) {
        PMMVAD v = as->Vads[as->VadCount - 1];
        release_pages(as, v->StartVa, v->EndVa);
        MiRemoveVad(as, v);
        vad_destroy(v);
    }
    if (as->Spans) {
        ExFreePool(as->Spans);
        as->Spans = nullptr;
        as->Vads = nullptr;
        as->VadCapacity = 0;
    }
    return_commit(as, as->CommitCharge);
    free_tables(as->Root, 0);
    MmFreePage(as->RootPhys);
    MiFlushTlbAsid(as->Asid);
    as->Root = nullptr;
    as->RootPhys = 0;
}

/* -------------------------------------------------------- system services */

static NTSTATUS reference_process(HANDLE handle, ACCESS_MASK access, PEPROCESS *process)
{
    return PsReferenceProcessHandle(handle, access, process);
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
    /* the low bits of the type carry a preferred node plus one, and the machine has one node. */
    if ((type & 0x7F) > 1) {
        ObDereferenceObject(process);
        return STATUS_INVALID_PARAMETER;
    }
    /* new executable memory is dynamic code. */
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

/* what NtFreeVirtualMemory checks before it looks at the process, in windows' order. */
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
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = (ULONG_PTR)base <= MM_HIGHEST_USER_ADDRESS ? MiFindVad(as, (ULONG_PTR)base) : nullptr;
    BOOLEAN image = vad && vad->Type == MiVadImage;
    ExReleaseFastMutex(&as->Lock);
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
    /* code may be made data and code that already was code may stay, nothing else becomes code. */
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
    /* windows hands the base, size and old protection back whether the change worked or not. */
    KiCopyToUser(base, &b, sizeof(b));
    KiCopyToUser(size, &s, sizeof(s));
    if (old)
        KiCopyToUser(old, &o, sizeof(o));
    return status;
}

/*
 * the attributes of each page of a caller's list. a page backed by memory
 * is valid, with its protection and the normal priority 5; one that is
 * committed but not touched yet is not in the working set. pages of views
 * are shared when they belong to no private allocation.
 */
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
            ExAcquireFastMutex(&as->Lock);
            PMMVAD vad = MiFindVad(as, va);
            if (vad && !(vad->Flags & MM_VAD_PLACEHOLDER)) {
                ULONG64 pte = MiReadPte(as->Root, va & ~(ULONG_PTR)(PAGE_SIZE - 1));
                ULONG prot = pte_committed(pte) ? MiPteToProtect(pte) : 0;
                if ((pte & MM_PTE_VALID) && !(prot & (PAGE_NOACCESS | PAGE_GUARD))) {
                    attributes = 1 | (ULONG64)(prot & 0x7FF) << 4 | 5ull << 24;
                    if (vad->Type != MiVadPrivate)
                        attributes |= 1ull << 1 | 1ull << 15;
                }
            }
            ExReleaseFastMutex(&as->Lock);
        }
        status = KiCopyToUser(entry + 8, &attributes, sizeof(attributes));
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

/* the protection field of a working set entry: the memory manager's own numbering of the protections. */
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

/* the resident pages from an address on, up to a number of them; resume says where to go on, 0 once it is the end. */
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
                if (!(pte[k] & MM_PTE_VALID))
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
 * every page of the working set: a count, then for each page its address
 * with the protection, the share count and a shared bit in the low bits.
 * a buffer that is too small gets the count, so the caller can ask again.
 * the pages are gathered a few at a time and copied out with the lock
 * released, as the copy can fault.
 */
static NTSTATUS query_working_set(PEPROCESS process, PVOID info, SIZE_T length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (length < sizeof(ULONG_PTR))
        return STATUS_INFO_LENGTH_MISMATCH;
    SIZE_T capacity = (length - sizeof(ULONG_PTR)) / sizeof(ULONG_PTR), total = 0;
    ULONG_PTR chunk[256], resume = 0;
    ExAcquireFastMutex(&as->Lock);
    do
        total += gather_working_set(as, &resume, chunk, 256);
    while (resume);
    ExReleaseFastMutex(&as->Lock);
    NTSTATUS status = KiCopyToUser(info, &total, sizeof(total));
    if (!NT_SUCCESS(status))
        return status;
    if (total > capacity)
        return STATUS_INFO_LENGTH_MISMATCH;
    SIZE_T written = 0;
    resume = 0;
    do {
        ExAcquireFastMutex(&as->Lock);
        SIZE_T n = gather_working_set(as, &resume, chunk, capacity - written < 256 ? capacity - written : 256);
        ExReleaseFastMutex(&as->Lock);
        if (n) {
            status = KiCopyToUser((PULONG_PTR)info + 1 + written, chunk, n * sizeof(ULONG_PTR));
            if (!NT_SUCCESS(status))
                return status;
        }
        written += n;
    } while (resume && written < capacity);
    return STATUS_SUCCESS;
}

/*
 * the counters of ProcessVmCounters in the three sizes windows has: the
 * plain ones, with the private bytes, and with the private working set.
 */
ULONG MmQueryVmCounters(PEPROCESS process, PVOID out, ULONG length)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    SIZE_T v[14] = {};
    ExAcquireFastMutex(&as->Lock);
    SIZE_T virtual_size = 0;
    for (ULONG i = 0; i < as->VadCount; i++)
        virtual_size += as->Spans[i].End - as->Spans[i].Start;
    SIZE_T working = as->CommittedPages * PAGE_SIZE, peak_working = as->PeakCommittedPages * PAGE_SIZE;
    SIZE_T charge = as->CommitCharge * PAGE_SIZE, peak_charge = as->PeakCommitCharge * PAGE_SIZE;
    SIZE_T faults = as->PageFaults;
    ExReleaseFastMutex(&as->Lock);
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
        /* the image view holding the address; zeros for any other memory, as windows answers. */
        MEMORY_IMAGE_INFORMATION mi = {};
        PMMADDRESS_SPACE as = &process->AddressSpace;
        if (length < sizeof(mi)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            ExAcquireFastMutex(&as->Lock);
            PMMVAD vad = (ULONG_PTR)base <= MM_HIGHEST_USER_ADDRESS ? MiFindVad(as, (ULONG_PTR)base) : nullptr;
            if (!vad) {
                status = STATUS_INVALID_ADDRESS;
            } else if (vad->Type == MiVadImage && vad->Section) {
                mi.ImageBase = (PVOID)vad->StartVa;
                mi.SizeOfImage = (SIZE_T)vad->Section->Size;
                if (vad->SectionOffset + (vad->EndVa - vad->StartVa) < vad->Section->Size)
                    mi.ImageFlags |= MEMORY_IMAGE_PARTIAL_MAP;
            }
            ExReleaseFastMutex(&as->Lock);
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
        ExAcquireFastMutex(&as->Lock);
        PMMVAD vad = MiFindVad(as, (ULONG_PTR)base);
        PSECTION section = vad ? vad->Section : nullptr;
        if (section)
            ObReferenceObject(section);
        ExReleaseFastMutex(&as->Lock);
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
 * information about ranges of virtual memory. no page is ever paged out or
 * missing from the working set in mime, so prefetching, priorities and
 * removing from the working set check their arguments and change nothing;
 * call targets are not tracked either, as no image is checked against them.
 */
NTSTATUS NTAPI NtSetInformationVirtualMemory(HANDLE process_handle, ULONG klass, ULONG_PTR count, PVOID entries,
                                             PVOID info, ULONG length)
{
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
    /* the ranges have to be sane, and are copied as they sit in the caller's memory. */
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
 * copies between address spaces through a kernel bounce buffer. memory that is not there ends the copy short
 * with STATUS_PARTIAL_COPY, memory that is there and cannot be written is an access violation.
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
            /* what failed is told apart by the page: the bytes before it are copied. */
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
    /* the page the write stopped at: there, but not writable. */
    MEMORY_BASIC_INFORMATION mbi;
    if (target_failed && NT_SUCCESS(MiQueryVirtual(to_process, (PUCHAR)to + done, &mbi)) && mbi.State == MEM_COMMIT &&
        !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
        !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return STATUS_ACCESS_VIOLATION;
    return (NTSTATUS)0x8000000D /* STATUS_PARTIAL_COPY */;
}

NTSTATUS NTAPI NtFlushInstructionCache(HANDLE process, PVOID base, SIZE_T size)
{
    KiFlushInstructionCache();
    return STATUS_SUCCESS;
}

/*
 * every processor running this process in user mode takes an interrupt,
 * and its entry into the kernel publishes its stores; the caller waits
 * for each to leave user mode.
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
