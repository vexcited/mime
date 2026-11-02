/* system virtual address space: kernel stacks, driver images and other mappings outside the physmap. */
#include "../ntos.h"

typedef struct SYSVA_RANGE {
    struct SYSVA_RANGE *Next;
    ULONG_PTR Start;
    SIZE_T Pages;
} SYSVA_RANGE;

static SYSVA_RANGE *g_free_ranges;
static ULONG_PTR g_bump = MIME_KVA_SYSTEM;
static KSPIN_LOCK g_lock;

PVOID MmAllocateSystemVa(SIZE_T pages)
{
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    SYSVA_RANGE **link = &g_free_ranges;
    for (SYSVA_RANGE *r = g_free_ranges; r; link = &r->Next, r = r->Next) {
        if (r->Pages >= pages) {
            ULONG_PTR va = r->Start;
            r->Start += pages * PAGE_SIZE;
            r->Pages -= pages;
            if (r->Pages == 0) {
                *link = r->Next;
                ExFreePool(r);
            }
            KeReleaseSpinLock(&g_lock, irql);
            return (PVOID)va;
        }
    }
    ULONG_PTR va = g_bump;
    if (va + pages * PAGE_SIZE > MIME_KVA_SYSTEM + MIME_KVA_SYSTEM_SIZE) {
        KeReleaseSpinLock(&g_lock, irql);
        return nullptr;
    }
    g_bump += pages * PAGE_SIZE;
    KeReleaseSpinLock(&g_lock, irql);
    return (PVOID)va;
}

void MmFreeSystemVa(PVOID va, SIZE_T pages)
{
    SYSVA_RANGE *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('M', 'm', 'S', 'v'));
    if (!r)
        return; /* leaks the range, which is harmless. */
    r->Start = (ULONG_PTR)va;
    r->Pages = pages;
    KIRQL irql = KeAcquireSpinLockRaiseToDpc(&g_lock);
    r->Next = g_free_ranges;
    g_free_ranges = r;
    KeReleaseSpinLock(&g_lock, irql);
}

PVOID MmMapPagesInSystem(const ULONG64 *pages, SIZE_T count, ULONG64 attrs)
{
    PUCHAR va = MmAllocateSystemVa(count);
    if (!va)
        return nullptr;
    for (SIZE_T i = 0; i < count; i++)
        MiMapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, pages[i], attrs);
    return va;
}

/* with an unmapped guard page below it. the pages of the top of the reserved room are mapped, the rest when it grows. */
PVOID MmAllocateKernelStack(SIZE_T size, SIZE_T reserve, PVOID *limit, PVOID *top)
{
    SIZE_T pages = BYTES_TO_PAGES(size), total = BYTES_TO_PAGES(reserve);
    if (pages > total)
        total = pages;
    PUCHAR va = MmAllocateSystemVa(total + 1);
    if (!va)
        return nullptr;
    SIZE_T first = total - pages + 1;
    for (SIZE_T i = first; i <= total; i++) {
        ULONG64 pa = MmAllocatePage();
        if (!pa) {
            for (SIZE_T j = first; j < i; j++)
                MmFreePage(MiUnmapPage(MiKernelRoot(), (ULONG_PTR)va + j * PAGE_SIZE, 0) & MM_PTE_ADDR_MASK);
            MmFreeSystemVa(va, total + 1);
            return nullptr;
        }
        MiMapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, pa, MM_KERNEL_RW);
    }
    if (limit)
        *limit = va + first * PAGE_SIZE;
    if (top)
        *top = va + (total + 1) * PAGE_SIZE;
    return va;
}

/* maps more pages below the lowest one the thread has, up to the room that was reserved. */
BOOLEAN MmGrowKernelStack(PKTHREAD thread, SIZE_T bytes)
{
    PUCHAR floor = (PUCHAR)thread->StackAllocation + PAGE_SIZE, limit = thread->StackLimit;
    SIZE_T pages = BYTES_TO_PAGES(bytes), room = (SIZE_T)(limit - floor) / PAGE_SIZE;
    if (pages > room)
        pages = room;
    if (!pages)
        return FALSE;
    for (SIZE_T i = 1; i <= pages; i++) {
        ULONG64 pa = MmAllocatePage();
        if (!pa) {
            for (SIZE_T j = 1; j < i; j++)
                MmFreePage(MiUnmapPage(MiKernelRoot(), (ULONG_PTR)limit - j * PAGE_SIZE, 0) & MM_PTE_ADDR_MASK);
            return FALSE;
        }
        MiMapPage(MiKernelRoot(), (ULONG_PTR)limit - i * PAGE_SIZE, pa, MM_KERNEL_RW);
    }
    thread->StackLimit = limit - pages * PAGE_SIZE;
    return TRUE;
}

void MmFreeKernelStack(PVOID allocation, PVOID limit, SIZE_T reserve)
{
    SIZE_T total = BYTES_TO_PAGES(reserve);
    for (PUCHAR page = limit; page < (PUCHAR)allocation + (total + 1) * PAGE_SIZE; page += PAGE_SIZE) {
        ULONG64 pte = MiUnmapPage(MiKernelRoot(), (ULONG_PTR)page, 0);
        if (pte & MM_PTE_VALID)
            MmFreePage(pte & MM_PTE_ADDR_MASK);
    }
    MmFreeSystemVa(allocation, total + 1);
}
