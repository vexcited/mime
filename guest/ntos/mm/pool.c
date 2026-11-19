/*
 * kernel pool. small allocations come from per size class slabs in physmap pages. allocations above
 * 2 KiB are whole page aligned pages, like on Windows, with their size kept in the pfn.
 */
#include "../ntos.h"

#define POOL_MAGIC 0x504Du
#define POOL_LARGE_MARK 0x4C415247u /* stored in MMPFN.ShareCount of the first page. */

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
/* real locks, one a size class: a service without the kernel lock frees and allocates too. */
static volatile LONG g_class_lock[POOL_CLASSES];
static SIZE_T g_pool_bytes;

#define POOL_CLASS_PAGED 0x8000u /* in POOL_HEADER.Class: asked as paged pool. */

/*
 * what SystemPoolTagInformation and SystemBigPoolInformation tell: the allocations and frees of each tag with the
 * bytes it holds, paged and not, and every block of whole pages with its size and its tag. a tag takes a slot of the
 * table the first time it allocates and keeps it; the counters are atomic, so the pool's own locks are not held longer.
 */
typedef struct POOL_TAG_USE {
    volatile ULONG Tag;
    volatile ULONG Allocs[2], Frees[2]; /* not paged, then paged */
    volatile SIZE_T Used[2];
} POOL_TAG_USE;

#define POOL_TAG_SLOTS 4096
static POOL_TAG_USE g_tag_use[POOL_TAG_SLOTS];

typedef struct POOL_BIG_BLOCK {
    SIZE_T Size;
    ULONG Tag;
    USHORT NextFree; /* 1 based, 0 ends the list */
    BOOLEAN Used, Paged;
} POOL_BIG_BLOCK;

#define POOL_BIG_SLOTS 16384
static POOL_BIG_BLOCK g_big[POOL_BIG_SLOTS];
static USHORT g_big_free, g_big_top;
static volatile LONG g_big_lock;

static POOL_TAG_USE *tag_use(ULONG tag)
{
    if (!tag)
        tag = KI_TAG('N', 'o', 'n', 'e');
    ULONG at = (tag * 0x9E3779B1u) >> 20;
    for (ULONG i = 0; i < POOL_TAG_SLOTS; i++, at = (at + 1) % POOL_TAG_SLOTS) {
        ULONG seen = __atomic_load_n(&g_tag_use[at].Tag, __ATOMIC_RELAXED);
        if (!seen && __atomic_compare_exchange_n(&g_tag_use[at].Tag, &seen, tag, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return &g_tag_use[at];
        if (seen == tag)
            return &g_tag_use[at];
    }
    return nullptr;
}

static void count_alloc(ULONG tag, BOOLEAN paged, SIZE_T bytes)
{
    POOL_TAG_USE *use = tag_use(tag);
    if (!use)
        return;
    __atomic_fetch_add(&use->Allocs[paged], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&use->Used[paged], bytes, __ATOMIC_RELAXED);
}

static void count_free(ULONG tag, BOOLEAN paged, SIZE_T bytes)
{
    POOL_TAG_USE *use = tag_use(tag);
    if (!use)
        return;
    __atomic_fetch_add(&use->Frees[paged], 1, __ATOMIC_RELAXED);
    __atomic_fetch_sub(&use->Used[paged], bytes, __ATOMIC_RELAXED);
}

/* the slot of a block of whole pages, 1 based, 0 when the table is full. */
static USHORT big_take(SIZE_T size, ULONG tag, BOOLEAN paged)
{
    ULONG64 state = KiAcquireRawLock(&g_big_lock);
    USHORT slot = g_big_free;
    if (slot)
        g_big_free = g_big[slot - 1].NextFree;
    else if (g_big_top < POOL_BIG_SLOTS)
        slot = ++g_big_top;
    if (slot)
        g_big[slot - 1] = (POOL_BIG_BLOCK){size, tag, 0, TRUE, paged};
    KiReleaseRawLock(&g_big_lock, state);
    return slot;
}

static void big_give(USHORT slot)
{
    if (!slot)
        return;
    ULONG64 state = KiAcquireRawLock(&g_big_lock);
    g_big[slot - 1].Used = FALSE;
    g_big[slot - 1].NextFree = g_big_free;
    g_big_free = slot;
    KiReleaseRawLock(&g_big_lock, state);
}

/* SYSTEM_POOLTAG for each tag, as many as the room holds: the count of all of them. */
ULONG ExQueryPoolTags(PVOID out, ULONG room)
{
    struct {
        ULONG Tag, PagedAllocs, PagedFrees, Pad;
        SIZE_T PagedUsed;
        ULONG NonPagedAllocs, NonPagedFrees;
        SIZE_T NonPagedUsed;
    } *entry = out;
    ULONG count = 0;
    for (ULONG i = 0; i < POOL_TAG_SLOTS; i++) {
        POOL_TAG_USE *use = &g_tag_use[i];
        if (!use->Tag)
            continue;
        if (count < room) {
            entry[count].Tag = use->Tag;
            entry[count].PagedAllocs = use->Allocs[1];
            entry[count].PagedFrees = use->Frees[1];
            entry[count].Pad = 0;
            entry[count].PagedUsed = use->Used[1];
            entry[count].NonPagedAllocs = use->Allocs[0];
            entry[count].NonPagedFrees = use->Frees[0];
            entry[count].NonPagedUsed = use->Used[0];
        }
        count++;
    }
    return count;
}

/* SYSTEM_BIGPOOL_ENTRY for each block of whole pages: its address is kept, but for the bit that tells it is not paged. */
ULONG ExQueryBigPool(PVOID out, ULONG room)
{
    struct {
        ULONG_PTR NonPaged;
        SIZE_T Size;
        ULONG Tag, Pad;
    } *entry = out;
    ULONG count = 0;
    ULONG64 state = KiAcquireRawLock(&g_big_lock);
    for (ULONG i = 0; i < g_big_top; i++) {
        if (!g_big[i].Used)
            continue;
        if (count < room) {
            entry[count].NonPaged = !g_big[i].Paged;
            entry[count].Size = g_big[i].Size;
            entry[count].Tag = g_big[i].Tag;
            entry[count].Pad = 0;
        }
        count++;
    }
    KiReleaseRawLock(&g_big_lock, state);
    return count;
}

static int class_for(SIZE_T total)
{
    for (int i = 0; i < (int)POOL_CLASSES; i++)
        if (total <= g_class_size[i])
            return i;
    return -1;
}

/* the pages come without the class lock held, which the page allocator never sees taken. */
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
    POOL_FREE *first = nullptr, *last = (POOL_FREE *)base;
    for (SIZE_T i = 0; i < count; i++) {
        POOL_FREE *f = (POOL_FREE *)(base + i * g_class_size[cls]);
        f->Next = first;
        first = f;
    }
    ULONG64 state = KiAcquireRawLock(&g_class_lock[cls]);
    last->Next = g_class_free[cls];
    g_class_free[cls] = first;
    KiReleaseRawLock(&g_class_lock[cls], state);
    return TRUE;
}

/* above any real address space, and low enough that adding a header size cannot wrap. */
#define POOL_MAX_REQUEST (1ull << 46)

[[clang::noinline, noreturn]] __attribute__((cold)) static void bad_large_free(PVOID p, PVOID caller)
{
    KiLogPrintf("pool: free of %p, a page aligned address that is not a large block, called from %p", p, caller);
    KeBugCheckEx(0x000000C2, 7, (ULONG_PTR)p, (ULONG_PTR)caller, 0); /* BAD_POOL_CALLER, with the caller. */
    __builtin_unreachable();
}

[[clang::noinline, noreturn]] __attribute__((cold)) static void bad_header(PVOID p, const POOL_HEADER *h, PVOID caller)
{
    KiLogPrintf("pool: free of %p with a damaged header, tag %08x, class %u, magic %04x, size %llu, called from %p", p,
                h->Tag, h->Class, h->Magic, h->Size, caller);
    KeBugCheckEx(0x00000019, 0x20, (ULONG_PTR)p, h->Magic, h->Class); /* BAD_POOL_HEADER. */
    __builtin_unreachable();
}

/* inline, so that the return address of the verifying pool is that of the caller of the exported function. */
[[clang::always_inline]] static inline PVOID pool_alloc(SIZE_T size, ULONG tag, BOOLEAN zero, BOOLEAN paged)
{
    if (size > POOL_MAX_REQUEST)
        return nullptr;
    if (__builtin_expect(MmVerifyPool, 0)) {
        PVOID special = MiSpecialPoolAllocate(size, tag, zero, __builtin_return_address(0));
        if (special)
            return special;
    }
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
        /* the pfn list entry holds the page count, then the tag, whether it is paged and its slot among the big blocks. */
        p->ListEntry.Flink = (PLIST_ENTRY)(ULONG_PTR)pages;
        p->ListEntry.Blink = (PLIST_ENTRY)((ULONG_PTR)tag | (ULONG_PTR)paged << 32 | (ULONG_PTR)big_take(size, tag, paged) << 40);
        count_alloc(tag, paged, pages * PAGE_SIZE);
        __atomic_fetch_add(&g_pool_bytes, pages * PAGE_SIZE, __ATOMIC_RELAXED);
        return HalPhysmap(pa);
    }
    POOL_FREE *f;
    for (;;) {
        ULONG64 state = KiAcquireRawLock(&g_class_lock[cls]);
        f = g_class_free[cls];
        if (f)
            g_class_free[cls] = f->Next;
        KiReleaseRawLock(&g_class_lock[cls], state);
        if (f)
            break;
        if (!refill(cls))
            return nullptr;
    }
    __atomic_fetch_add(&g_pool_bytes, g_class_size[cls], __ATOMIC_RELAXED);
    POOL_HEADER *h = (POOL_HEADER *)f;
    h->Tag = tag;
    h->Class = (USHORT)(cls | (paged ? POOL_CLASS_PAGED : 0));
    h->Magic = POOL_MAGIC;
    count_alloc(tag, paged, g_class_size[cls]);
    h->Size = size;
    PVOID body = h + 1;
    if (zero)
        memset(body, 0, size);
    return body;
}

[[clang::always_inline]] static inline void pool_free(PVOID p, ULONG tag)
{
    if (!p)
        return;
    if (__builtin_expect(MmVerifyPool, 0) && MiSpecialPoolContains(p)) {
        MiSpecialPoolFree(p, tag, __builtin_return_address(0));
        return;
    }
    if (((ULONG_PTR)p & (PAGE_SIZE - 1)) == 0) {
        ULONG64 pa = HalPhysicalOf(p);
        PMMPFN pfn = MiGetPfn(pa);
        if (!pfn || pfn->ShareCount != POOL_LARGE_MARK)
            bad_large_free(p, __builtin_return_address(0));
        SIZE_T pages = (SIZE_T)(ULONG_PTR)pfn->ListEntry.Flink;
        ULONG_PTR kept = (ULONG_PTR)pfn->ListEntry.Blink;
        big_give((USHORT)(kept >> 40));
        count_free((ULONG)kept, kept >> 32 & 1, pages * PAGE_SIZE);
        pfn->ShareCount = 0;
        __atomic_fetch_sub(&g_pool_bytes, pages * PAGE_SIZE, __ATOMIC_RELAXED);
        MmFreeContiguous(pa, pages);
        return;
    }
    POOL_HEADER *h = (POOL_HEADER *)p - 1;
    if (h->Magic != POOL_MAGIC || (h->Class & ~POOL_CLASS_PAGED) >= POOL_CLASSES)
        bad_header(p, h, __builtin_return_address(0));
    int cls = h->Class & ~POOL_CLASS_PAGED;
    count_free(h->Tag, !!(h->Class & POOL_CLASS_PAGED), g_class_size[cls]);
    h->Magic = 0;
    POOL_FREE *f = (POOL_FREE *)h;
    ULONG64 state = KiAcquireRawLock(&g_class_lock[cls]);
    f->Next = g_class_free[cls];
    g_class_free[cls] = f;
    KiReleaseRawLock(&g_class_lock[cls], state);
    __atomic_fetch_sub(&g_pool_bytes, g_class_size[cls], __ATOMIC_RELAXED);
}

PVOID NTAPI ExAllocatePool2(POOL_FLAGS flags, SIZE_T size, ULONG tag)
{
    PVOID p = pool_alloc(size, tag, !(flags & POOL_FLAG_UNINITIALIZED), !!(flags & POOL_FLAG_PAGED));
    if (!p && (flags & POOL_FLAG_RAISE_ON_FAILURE))
        KeBugCheckEx(0x00000041, size, 0, 0, 0); /* MUST_SUCCEED_POOL_EMPTY. */
    return p;
}

PVOID NTAPI ExAllocatePoolWithTag(POOL_TYPE type, SIZE_T size, ULONG tag)
{
    return pool_alloc(size, tag, FALSE, type & 1);
}

PVOID NTAPI ExAllocatePool(POOL_TYPE type, SIZE_T size)
{
    return pool_alloc(size, KI_TAG('N', 'o', 'n', 'e'), FALSE, type & 1);
}

PVOID NTAPI ExAllocatePoolZero(POOL_TYPE type, SIZE_T size, ULONG tag)
{
    return pool_alloc(size, tag, TRUE, type & 1);
}

void NTAPI ExFreePoolWithTag(PVOID p, ULONG tag)
{
    pool_free(p, tag);
}

void NTAPI ExFreePool(PVOID p)
{
    pool_free(p, 0);
}

SIZE_T ExQueryPoolUsage(void)
{
    return g_pool_bytes;
}

PVOID MimeRtlAllocate(SIZE_T size)
{
    return pool_alloc(size, KI_TAG('R', 't', 'l', ' '), TRUE, FALSE);
}

void MimeRtlFree(PVOID p)
{
    pool_free(p, 0);
}

void ExInitializePool(void)
{
}
