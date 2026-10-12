/*
 * system virtual address space: kernel stacks, driver images and other
 * mappings that need their own page tables instead of the physmap.
 */
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
        return NULL;
    }
    g_bump += pages * PAGE_SIZE;
    KeReleaseSpinLock(&g_lock, irql);
    return (PVOID)va;
}

void MmFreeSystemVa(PVOID va, SIZE_T pages)
{
    SYSVA_RANGE *r = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), KI_TAG('M', 'm', 'S', 'v'));
    if (!r)
        return; /* leaks the range, harmless */
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
        return NULL;
    for (SIZE_T i = 0; i < count; i++)
        MiMapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, pages[i], attrs);
    return va;
}

/* allocates a kernel stack with an unmapped guard page below it. */
PVOID MmAllocateKernelStack(SIZE_T size, PVOID *limit)
{
    SIZE_T pages = BYTES_TO_PAGES(size);
    PUCHAR va = MmAllocateSystemVa(pages + 1);
    if (!va)
        return NULL;
    for (SIZE_T i = 1; i <= pages; i++) {
        ULONG64 pa = MmAllocatePage();
        if (!pa) {
            for (SIZE_T j = 1; j < i; j++)
                MmFreePage(MiUnmapPage(MiKernelRoot(), (ULONG_PTR)va + j * PAGE_SIZE, 0) & MM_PTE_ADDR_MASK);
            MmFreeSystemVa(va, pages + 1);
            return NULL;
        }
        MiMapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, pa, MM_KERNEL_RW);
    }
    if (limit)
        *limit = va + PAGE_SIZE;
    return va;
}

void MmFreeKernelStack(PVOID allocation, SIZE_T size)
{
    SIZE_T pages = BYTES_TO_PAGES(size);
    for (SIZE_T i = 1; i <= pages; i++) {
        ULONG64 pte = MiUnmapPage(MiKernelRoot(), (ULONG_PTR)allocation + i * PAGE_SIZE, 0);
        if (pte & MM_PTE_VALID)
            MmFreePage(pte & MM_PTE_ADDR_MASK);
    }
    MmFreeSystemVa(allocation, pages + 1);
}
