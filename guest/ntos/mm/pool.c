/*
 * kernel pool. small allocations come from per size class slabs carved
 * out of physmap pages; allocations above 2 KiB are whole pages, page
 * aligned like on windows, with their size recorded in the pfn.
 */
#include "../ntos.h"

#define POOL_MAGIC 0x504Du
#define POOL_LARGE_MARK 0x4C415247u /* stored in MMPFN.ShareCount of the first page */

typedef struct POOL_HEADER {
    ULONG Tag;
    USHORT Class;
    USHORT Magic;
    ULONG64 Size;
} POOL_HEADER;
MIME_STATIC_ASSERT(sizeof(POOL_HEADER) == 16, "pool header");

typedef struct POOL_FREE {
    struct POOL_FREE *Next;
} POOL_FREE;

static constexpr ULONG g_class_size[] = {32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2064};
#define POOL_CLASSES (sizeof(g_class_size) / sizeof(g_class_size[0]))
static POOL_FREE *g_class_free[POOL_CLASSES];
static SIZE_T g_pool_bytes;

static int class_for(SIZE_T total)
{
    for (int i = 0; i < (int)POOL_CLASSES; i++)
        if (total <= g_class_size[i])
            return i;
    return -1;
}

static BOOLEAN refill(int cls)
{
    SIZE_T pages = g_class_size[cls] > 512 ? 4 : 1;
    ULONG64 pa = MmAllocateContiguous(pages);
    if (!pa)
        return FALSE;
    for (SIZE_T i = 0; i < pages; i++) {
        PMMPFN p = MiGetPfn(pa + i * PAGE_SIZE);
        if (p)
            p->Flags |= MM_PFN_POOL;
    }
    PUCHAR base = HalPhysmap(pa);
    SIZE_T count = (pages * PAGE_SIZE) / g_class_size[cls];
    for (SIZE_T i = 0; i < count; i++) {
        POOL_FREE *f = (POOL_FREE *)(base + i * g_class_size[cls]);
        f->Next = g_class_free[cls];
        g_class_free[cls] = f;
    }
    return TRUE;
}

/* the largest request that is tried: more than the address space holds, which a size added to a header would wrap. */
#define POOL_MAX_REQUEST (1ull << 46)

static PVOID pool_alloc(SIZE_T size, ULONG tag, BOOLEAN zero)
{
    if (size > POOL_MAX_REQUEST)
        return nullptr;
    if (size == 0)
        size = 1;
    SIZE_T total = size + sizeof(POOL_HEADER);
    int cls = class_for(total);
    if (cls < 0) {
        SIZE_T pages = BYTES_TO_PAGES(size);
        ULONG64 pa = MmAllocateContiguous(pages);
        if (!pa)
            return nullptr;
        PMMPFN p = MiGetPfn(pa);
        p->ShareCount = POOL_LARGE_MARK;
        p->Order = 0;
        p->Flags |= MM_PFN_POOL;
        /* remember the page count in the second word of the pfn list entry. */
        p->ListEntry.Flink = (PLIST_ENTRY)(ULONG_PTR)pages;
        p->ListEntry.Blink = (PLIST_ENTRY)(ULONG_PTR)tag;
        ULONG64 state = KiDisableInterrupts();
        g_pool_bytes += pages * PAGE_SIZE;
        KiRestoreInterrupts(state);
        return HalPhysmap(pa);
    }
    ULONG64 state = KiDisableInterrupts();
    if (!g_class_free[cls] && !refill(cls)) {
        KiRestoreInterrupts(state);
        return nullptr;
    }
    POOL_FREE *f = g_class_free[cls];
    g_class_free[cls] = f->Next;
    g_pool_bytes += g_class_size[cls];
    KiRestoreInterrupts(state);
    POOL_HEADER *h = (POOL_HEADER *)f;
    h->Tag = tag;
    h->Class = (USHORT)cls;
    h->Magic = POOL_MAGIC;
    h->Size = size;
    PVOID body = h + 1;
    if (zero)
        memset(body, 0, size);
    return body;
}

static void pool_free(PVOID p)
{
    if (!p)
        return;
    if (((ULONG_PTR)p & (PAGE_SIZE - 1)) == 0) {
        ULONG64 pa = HalPhysicalOf(p);
        PMMPFN pfn = MiGetPfn(pa);
        if (!pfn || pfn->ShareCount != POOL_LARGE_MARK)
            KeBugCheckEx(0x000000C2, 7, (ULONG_PTR)p, (ULONG_PTR)__builtin_return_address(0), 0); /* BAD_POOL_CALLER, with the caller */
        SIZE_T pages = (SIZE_T)(ULONG_PTR)pfn->ListEntry.Flink;
        pfn->ShareCount = 0;
        ULONG64 state = KiDisableInterrupts();
        g_pool_bytes -= pages * PAGE_SIZE;
        KiRestoreInterrupts(state);
        MmFreeContiguous(pa, pages);
        return;
    }
    POOL_HEADER *h = (POOL_HEADER *)p - 1;
    if (h->Magic != POOL_MAGIC || h->Class >= POOL_CLASSES)
        KeBugCheckEx(0x00000019, 0x20, (ULONG_PTR)p, h->Magic, h->Class); /* BAD_POOL_HEADER */
    int cls = h->Class;
    h->Magic = 0;
    ULONG64 state = KiDisableInterrupts();
    POOL_FREE *f = (POOL_FREE *)h;
    f->Next = g_class_free[cls];
    g_class_free[cls] = f;
    g_pool_bytes -= g_class_size[cls];
    KiRestoreInterrupts(state);
}

PVOID NTAPI ExAllocatePool2(POOL_FLAGS flags, SIZE_T size, ULONG tag)
{
    PVOID p = pool_alloc(size, tag, !(flags & POOL_FLAG_UNINITIALIZED));
    if (!p && (flags & POOL_FLAG_RAISE_ON_FAILURE))
        KeBugCheckEx(0x00000041, size, 0, 0, 0); /* MUST_SUCCEED_POOL_EMPTY */
    return p;
}

PVOID NTAPI ExAllocatePoolWithTag(POOL_TYPE type, SIZE_T size, ULONG tag)
{
    return pool_alloc(size, tag, FALSE);
}

PVOID NTAPI ExAllocatePool(POOL_TYPE type, SIZE_T size)
{
    return pool_alloc(size, KI_TAG('N', 'o', 'n', 'e'), FALSE);
}

PVOID NTAPI ExAllocatePoolZero(POOL_TYPE type, SIZE_T size, ULONG tag)
{
    return pool_alloc(size, tag, TRUE);
}

void NTAPI ExFreePoolWithTag(PVOID p, ULONG tag)
{
    pool_free(p);
}

void NTAPI ExFreePool(PVOID p)
{
    pool_free(p);
}

SIZE_T ExQueryPoolUsage(void)
{
    return g_pool_bytes;
}

PVOID MimeRtlAllocate(SIZE_T size)
{
    return pool_alloc(size, KI_TAG('R', 't', 'l', ' '), TRUE);
}

void MimeRtlFree(PVOID p)
{
    pool_free(p);
}

void ExInitializePool(void)
{
}
