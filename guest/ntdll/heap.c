/*
 * process heaps.
 *
 * small and medium blocks live in segments carved with boundary tags, so
 * neighbours coalesce on free. free blocks sit in segregated bins: exact
 * 16 byte classes up to 1 KiB, then power of two classes. requests above
 * the segment threshold get their own virtual allocation.
 */
#include "ntdll_private.h"

#define HEAP_SIGNATURE 0x50414548u /* "HEAP" */
#define HEAP_GRANULE 16
#define HEAP_SEGMENT_SIZE (1u << 20)
#define HEAP_LARGE_THRESHOLD (HEAP_SEGMENT_SIZE / 2)
#define HEAP_SMALL_BINS 64 /* 16 .. 1024 bytes */
#define HEAP_BINS (HEAP_SMALL_BINS + 24)

#define BLOCK_BUSY 0x01
#define BLOCK_LAST 0x02  /* last block of its segment */
#define BLOCK_LARGE 0x04 /* standalone virtual allocation */
#define BLOCK_MAGIC 0xB10Cu

/* every block starts with this 16 byte header; sizes count granules. */
typedef struct HEAP_BLOCK {
    ULONG PrevSize;  /* granules of the previous block in the segment, 0 for the first */
    ULONG Size;      /* granules of this block, header included */
    USHORT Unused;   /* bytes past the requested size, for RtlSizeHeap */
    UCHAR Flags;
    UCHAR Pad;
    USHORT Magic;
    USHORT Pad2;
} HEAP_BLOCK;
MIME_STATIC_ASSERT(sizeof(HEAP_BLOCK) == HEAP_GRANULE, "HEAP_BLOCK");

/* free blocks link through their payload. */
typedef struct HEAP_FREE {
    HEAP_BLOCK Block;
    LIST_ENTRY Links;
} HEAP_FREE;

typedef struct DECLSPEC_ALIGN(16) HEAP_SEGMENT {
    LIST_ENTRY Links;
    SIZE_T Size;
    ULONG Separate; /* its own virtual allocation, released with the heap */
    ULONG Pad;
} HEAP_SEGMENT;
MIME_STATIC_ASSERT(sizeof(HEAP_SEGMENT) % HEAP_GRANULE == 0, "HEAP_SEGMENT");

/* large allocations: this header, then a block header with BLOCK_LARGE. */
typedef struct HEAP_LARGE {
    LIST_ENTRY Links;
    SIZE_T Reserved;
    SIZE_T Requested;
} HEAP_LARGE;
MIME_STATIC_ASSERT(sizeof(HEAP_LARGE) % HEAP_GRANULE == 0, "HEAP_LARGE");

typedef struct HEAP {
    ULONG Signature;
    ULONG Flags;
    RTL_CRITICAL_SECTION Lock;
    LIST_ENTRY Segments;
    LIST_ENTRY LargeBlocks;
    LIST_ENTRY Bins[HEAP_BINS];
    ULONG64 BinMask[(HEAP_BINS + 63) / 64];
    BOOLEAN Growable;
    BOOLEAN External; /* memory supplied by the caller */
    SIZE_T TotalSegments;
} HEAP, *PHEAP;

static PHEAP g_heaps[64];
static ULONG g_heap_count;
static RTL_CRITICAL_SECTION g_heap_list_lock = {NULL, -1, 0, NULL, NULL, 0};

static ULONG bin_index(ULONG granules)
{
    if (granules <= HEAP_SMALL_BINS)
        return granules - 1;
    ULONG bin = HEAP_SMALL_BINS + (31 - (ULONG)__builtin_clz(granules)) - 6;
    return bin < HEAP_BINS ? bin : HEAP_BINS - 1;
}

static HEAP_BLOCK *next_block(HEAP_BLOCK *b)
{
    return (b->Flags & BLOCK_LAST) ? NULL : (HEAP_BLOCK *)((PUCHAR)b + (SIZE_T)b->Size * HEAP_GRANULE);
}

static HEAP_BLOCK *prev_block(HEAP_BLOCK *b)
{
    return b->PrevSize ? (HEAP_BLOCK *)((PUCHAR)b - (SIZE_T)b->PrevSize * HEAP_GRANULE) : NULL;
}

static void insert_free(PHEAP heap, HEAP_BLOCK *b)
{
    ULONG bin = bin_index(b->Size);
    b->Flags &= (UCHAR)~BLOCK_BUSY;
    InsertHeadList(&heap->Bins[bin], &((HEAP_FREE *)b)->Links);
    heap->BinMask[bin / 64] |= 1ull << (bin % 64);
}

static void remove_free(PHEAP heap, HEAP_BLOCK *b)
{
    ULONG bin = bin_index(b->Size);
    RemoveEntryList(&((HEAP_FREE *)b)->Links);
    if (IsListEmpty(&heap->Bins[bin]))
        heap->BinMask[bin / 64] &= ~(1ull << (bin % 64));
}

static void set_size(HEAP_BLOCK *b, ULONG granules)
{
    b->Size = granules;
    HEAP_BLOCK *n = next_block(b);
    if (n)
        n->PrevSize = granules;
}

static void lock_heap(PHEAP heap, ULONG flags)
{
    if (!((heap->Flags | flags) & HEAP_NO_SERIALIZE))
        RtlEnterCriticalSection(&heap->Lock);
}

static void unlock_heap(PHEAP heap, ULONG flags)
{
    if (!((heap->Flags | flags) & HEAP_NO_SERIALIZE))
        RtlLeaveCriticalSection(&heap->Lock);
}

static void add_region(PHEAP heap, PVOID base, SIZE_T size, BOOLEAN separate)
{
    HEAP_SEGMENT *seg = base;
    seg->Size = size;
    seg->Separate = separate;
    InsertTailList(&heap->Segments, &seg->Links);
    heap->TotalSegments += size;
    HEAP_BLOCK *b = (HEAP_BLOCK *)(seg + 1);
    memset(b, 0, sizeof(*b));
    b->Size = (ULONG)((size - sizeof(HEAP_SEGMENT)) / HEAP_GRANULE);
    b->Flags = BLOCK_LAST;
    b->Magic = BLOCK_MAGIC;
    insert_free(heap, b);
}

static BOOLEAN grow(PHEAP heap, ULONG granules)
{
    if (!heap->Growable)
        return FALSE;
    SIZE_T size = HEAP_SEGMENT_SIZE;
    SIZE_T need = (SIZE_T)granules * HEAP_GRANULE + sizeof(HEAP_SEGMENT);
    while (size < need)
        size *= 2;
    PVOID base = NULL;
    ULONG protect = (heap->Flags & HEAP_CREATE_ENABLE_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, protect)))
        return FALSE;
    add_region(heap, base, size, TRUE);
    return TRUE;
}

static HEAP_BLOCK *find_free(PHEAP heap, ULONG granules)
{
    ULONG bin = bin_index(granules);
    /* exact small bins hold blocks of one size only. */
    if (bin < HEAP_SMALL_BINS && !IsListEmpty(&heap->Bins[bin]))
        return (HEAP_BLOCK *)CONTAINING_RECORD(heap->Bins[bin].Flink, HEAP_FREE, Links);
    for (ULONG word = bin / 64; word < RTL_NUMBER_OF(heap->BinMask); word++) {
        ULONG64 mask = heap->BinMask[word];
        if (word == bin / 64)
            mask &= ~0ull << (bin % 64);
        while (mask) {
            ULONG b = word * 64 + (ULONG)__builtin_ctzll(mask);
            mask &= mask - 1;
            for (PLIST_ENTRY e = heap->Bins[b].Flink; e != &heap->Bins[b]; e = e->Flink) {
                HEAP_BLOCK *blk = &CONTAINING_RECORD(e, HEAP_FREE, Links)->Block;
                if (blk->Size >= granules)
                    return blk;
            }
        }
    }
    return NULL;
}

static PVOID allocate_large(PHEAP heap, SIZE_T size)
{
    SIZE_T total = sizeof(HEAP_LARGE) + sizeof(HEAP_BLOCK) + size;
    PVOID base = NULL;
    ULONG protect = (heap->Flags & HEAP_CREATE_ENABLE_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &total, MEM_RESERVE | MEM_COMMIT, protect)))
        return NULL;
    HEAP_LARGE *large = base;
    large->Reserved = total;
    large->Requested = size;
    InsertTailList(&heap->LargeBlocks, &large->Links);
    HEAP_BLOCK *b = (HEAP_BLOCK *)(large + 1);
    b->Flags = BLOCK_BUSY | BLOCK_LARGE | BLOCK_LAST;
    b->Magic = BLOCK_MAGIC;
    return b + 1;
}

static PVOID allocate_locked(PHEAP heap, SIZE_T size)
{
    if (size >= HEAP_LARGE_THRESHOLD)
        return allocate_large(heap, size);
    SIZE_T bytes = size ? size : 1;
    ULONG granules = (ULONG)((bytes + sizeof(HEAP_BLOCK) + HEAP_GRANULE - 1) / HEAP_GRANULE);
    if (granules < 2)
        granules = 2; /* room for the free list links once freed */
    HEAP_BLOCK *b = find_free(heap, granules);
    if (!b) {
        if (!grow(heap, granules))
            return NULL;
        b = find_free(heap, granules);
        if (!b)
            return NULL;
    }
    remove_free(heap, b);
    if (b->Size >= granules + 2) {
        /* split, the tail stays free. */
        ULONG rest = b->Size - granules;
        UCHAR last = b->Flags & BLOCK_LAST;
        b->Flags &= (UCHAR)~BLOCK_LAST;
        b->Size = granules;
        HEAP_BLOCK *t = (HEAP_BLOCK *)((PUCHAR)b + (SIZE_T)granules * HEAP_GRANULE);
        memset(t, 0, sizeof(*t));
        t->PrevSize = granules;
        t->Flags = last;
        t->Magic = BLOCK_MAGIC;
        set_size(t, rest);
        insert_free(heap, t);
    }
    b->Flags |= BLOCK_BUSY;
    b->Unused = (USHORT)((SIZE_T)b->Size * HEAP_GRANULE - sizeof(HEAP_BLOCK) - size);
    return b + 1;
}

static HEAP_BLOCK *block_of(PVOID p)
{
    HEAP_BLOCK *b = (HEAP_BLOCK *)p - 1;
    if (((ULONG_PTR)p & (HEAP_GRANULE - 1)) || b->Magic != BLOCK_MAGIC || !(b->Flags & BLOCK_BUSY))
        return NULL;
    return b;
}

static void free_locked(PHEAP heap, HEAP_BLOCK *b)
{
    if (b->Flags & BLOCK_LARGE) {
        HEAP_LARGE *large = (HEAP_LARGE *)b - 1;
        RemoveEntryList(&large->Links);
        PVOID base = large;
        SIZE_T size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        return;
    }
    b->Flags &= (UCHAR)~BLOCK_BUSY;
    HEAP_BLOCK *n = next_block(b);
    if (n && !(n->Flags & BLOCK_BUSY)) {
        remove_free(heap, n);
        b->Flags |= n->Flags & BLOCK_LAST;
        n->Magic = 0;
        set_size(b, b->Size + n->Size);
    }
    HEAP_BLOCK *p = prev_block(b);
    if (p && !(p->Flags & BLOCK_BUSY)) {
        remove_free(heap, p);
        p->Flags |= b->Flags & BLOCK_LAST;
        b->Magic = 0;
        set_size(p, p->Size + b->Size);
        b = p;
    }
    insert_free(heap, b);
}

NTDLL_EXPORT PVOID NTAPI RtlCreateHeap(ULONG flags, PVOID base, SIZE_T reserve, SIZE_T commit, PVOID lock,
                                       PVOID parameters)
{
    SIZE_T size = reserve ? reserve : HEAP_SEGMENT_SIZE;
    size = (size + 0xFFFF) & ~(SIZE_T)0xFFFF;
    BOOLEAN external = base != NULL;
    if (!base) {
        ULONG protect = (flags & HEAP_CREATE_ENABLE_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT, protect)))
            return NULL;
    }
    PHEAP heap = base;
    memset(heap, 0, sizeof(*heap));
    heap->Signature = HEAP_SIGNATURE;
    heap->Flags = flags;
    heap->Growable = !reserve || (flags & HEAP_GROWABLE);
    heap->External = external;
    RtlInitializeCriticalSection(&heap->Lock);
    InitializeListHead(&heap->Segments);
    InitializeListHead(&heap->LargeBlocks);
    for (ULONG i = 0; i < HEAP_BINS; i++)
        InitializeListHead(&heap->Bins[i]);
    /* the rest of the first region becomes the first segment. */
    SIZE_T header = (sizeof(HEAP) + HEAP_GRANULE - 1) & ~(SIZE_T)(HEAP_GRANULE - 1);
    if (size > header + sizeof(HEAP_SEGMENT) + 4 * HEAP_GRANULE)
        add_region(heap, (PUCHAR)base + header, size - header, FALSE);

    RtlEnterCriticalSection(&g_heap_list_lock);
    if (g_heap_count < RTL_NUMBER_OF(g_heaps))
        g_heaps[g_heap_count++] = heap;
    PPEB peb = NtCurrentPeb();
    peb->NumberOfHeaps = g_heap_count;
    peb->ProcessHeaps = (PVOID *)g_heaps;
    RtlLeaveCriticalSection(&g_heap_list_lock);
    return heap;
}

NTDLL_EXPORT PVOID NTAPI RtlDestroyHeap(PVOID handle)
{
    PHEAP heap = handle;
    if (!heap || heap->Signature != HEAP_SIGNATURE || heap == NtCurrentPeb()->ProcessHeap)
        return handle;
    RtlEnterCriticalSection(&g_heap_list_lock);
    for (ULONG i = 0; i < g_heap_count; i++) {
        if (g_heaps[i] == heap) {
            g_heaps[i] = g_heaps[--g_heap_count];
            break;
        }
    }
    NtCurrentPeb()->NumberOfHeaps = g_heap_count;
    RtlLeaveCriticalSection(&g_heap_list_lock);

    while (!IsListEmpty(&heap->LargeBlocks)) {
        PVOID base = RemoveHeadList(&heap->LargeBlocks);
        SIZE_T size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
    }
    /* the first segment lives inside the heap allocation itself. */
    PLIST_ENTRY e = heap->Segments.Flink;
    while (e != &heap->Segments) {
        PLIST_ENTRY next = e->Flink;
        if (((HEAP_SEGMENT *)e)->Separate) {
            PVOID base = e;
            SIZE_T size = 0;
            NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
        }
        e = next;
    }
    RtlDeleteCriticalSection(&heap->Lock);
    heap->Signature = 0;
    if (!heap->External) {
        PVOID base = heap;
        SIZE_T size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
    }
    return NULL;
}

static void heap_failure(ULONG flags, NTSTATUS status)
{
    if (flags & HEAP_GENERATE_EXCEPTIONS)
        RtlRaiseStatus(status);
}

NTDLL_EXPORT PVOID NTAPI RtlAllocateHeap(PVOID handle, ULONG flags, SIZE_T size)
{
    PHEAP heap = handle;
    if (!heap || heap->Signature != HEAP_SIGNATURE)
        return NULL;
    if (size > (sizeof(SIZE_T) == 4 ? 0x7FFFFFFFull : 1ull << 46)) {
        heap_failure(flags | heap->Flags, STATUS_NO_MEMORY);
        return NULL;
    }
    lock_heap(heap, flags);
    PVOID p = allocate_locked(heap, size);
    unlock_heap(heap, flags);
    if (!p) {
        heap_failure(flags | heap->Flags, STATUS_NO_MEMORY);
        return NULL;
    }
    if (flags & HEAP_ZERO_MEMORY)
        memset(p, 0, size);
    return p;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlFreeHeap(PVOID handle, ULONG flags, PVOID p)
{
    PHEAP heap = handle;
    if (!p)
        return TRUE;
    if (!heap || heap->Signature != HEAP_SIGNATURE)
        return FALSE;
    HEAP_BLOCK *b = block_of(p);
    if (!b) {
        DbgPrint("heap: invalid free of %p in heap %p\n", p, heap);
        return FALSE;
    }
    lock_heap(heap, flags);
    free_locked(heap, b);
    unlock_heap(heap, flags);
    return TRUE;
}

static SIZE_T usable_size(HEAP_BLOCK *b)
{
    if (b->Flags & BLOCK_LARGE)
        return ((HEAP_LARGE *)b - 1)->Requested;
    return (SIZE_T)b->Size * HEAP_GRANULE - sizeof(HEAP_BLOCK) - b->Unused;
}

NTDLL_EXPORT SIZE_T NTAPI RtlSizeHeap(PVOID handle, ULONG flags, const void *p)
{
    HEAP_BLOCK *b = block_of((PVOID)p);
    return b ? usable_size(b) : (SIZE_T)-1;
}

NTDLL_EXPORT PVOID NTAPI RtlReAllocateHeap(PVOID handle, ULONG flags, PVOID p, SIZE_T size)
{
    PHEAP heap = handle;
    if (!p)
        return NULL;
    if (!heap || heap->Signature != HEAP_SIGNATURE)
        return NULL;
    HEAP_BLOCK *b = block_of(p);
    if (!b)
        return NULL;
    SIZE_T old = usable_size(b);
    lock_heap(heap, flags);
    PVOID result = NULL;
    if (!(b->Flags & BLOCK_LARGE) && size < HEAP_LARGE_THRESHOLD) {
        ULONG granules = (ULONG)(((size ? size : 1) + sizeof(HEAP_BLOCK) + HEAP_GRANULE - 1) / HEAP_GRANULE);
        if (granules < 2)
            granules = 2;
        HEAP_BLOCK *n = next_block(b);
        if (granules > b->Size && n && !(n->Flags & BLOCK_BUSY) && b->Size + n->Size >= granules) {
            /* absorb the free neighbour. */
            remove_free(heap, n);
            b->Flags |= n->Flags & BLOCK_LAST;
            n->Magic = 0;
            set_size(b, b->Size + n->Size);
        }
        if (granules <= b->Size) {
            if (b->Size >= granules + 2) {
                ULONG rest = b->Size - granules;
                UCHAR last = b->Flags & BLOCK_LAST;
                b->Flags &= (UCHAR)~BLOCK_LAST;
                b->Size = granules;
                HEAP_BLOCK *t = (HEAP_BLOCK *)((PUCHAR)b + (SIZE_T)granules * HEAP_GRANULE);
                memset(t, 0, sizeof(*t));
                t->PrevSize = granules;
                t->Flags = last | BLOCK_BUSY;
                t->Magic = BLOCK_MAGIC;
                set_size(t, rest);
                free_locked(heap, t);
            }
            b->Unused = (USHORT)((SIZE_T)b->Size * HEAP_GRANULE - sizeof(HEAP_BLOCK) - size);
            result = p;
        }
    }
    if (!result && !(flags & HEAP_REALLOC_IN_PLACE_ONLY)) {
        result = allocate_locked(heap, size);
        if (result) {
            memcpy(result, p, old < size ? old : size);
            free_locked(heap, b);
        }
    }
    unlock_heap(heap, flags);
    if (!result) {
        heap_failure(flags | heap->Flags, STATUS_NO_MEMORY);
        return NULL;
    }
    if ((flags & HEAP_ZERO_MEMORY) && size > old)
        memset((PUCHAR)result + old, 0, size - old);
    return result;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlLockHeap(PVOID handle)
{
    RtlEnterCriticalSection(&((PHEAP)handle)->Lock);
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlUnlockHeap(PVOID handle)
{
    RtlLeaveCriticalSection(&((PHEAP)handle)->Lock);
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlValidateHeap(PVOID handle, ULONG flags, PVOID p)
{
    PHEAP heap = handle;
    if (!heap || heap->Signature != HEAP_SIGNATURE)
        return FALSE;
    return p ? block_of(p) != NULL : TRUE;
}

NTDLL_EXPORT SIZE_T NTAPI RtlCompactHeap(PVOID handle, ULONG flags)
{
    return HEAP_SEGMENT_SIZE;
}

NTDLL_EXPORT ULONG NTAPI RtlGetProcessHeaps(ULONG count, PVOID *heaps)
{
    RtlEnterCriticalSection(&g_heap_list_lock);
    ULONG n = g_heap_count;
    for (ULONG i = 0; i < count && i < n; i++)
        heaps[i] = g_heaps[i];
    RtlLeaveCriticalSection(&g_heap_list_lock);
    return n;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSetHeapInformation(PVOID heap, ULONG klass, PVOID info, SIZE_T length)
{
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryHeapInformation(PVOID heap, ULONG klass, PVOID info, SIZE_T length,
                                                    PSIZE_T returned)
{
    /* HeapCompatibilityInformation: report the low fragmentation heap. */
    if (klass == 0 && length >= sizeof(ULONG)) {
        *(PULONG)info = 2;
        if (returned)
            *returned = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

void LdrpInitializeProcessHeap(void)
{
    PPEB peb = NtCurrentPeb();
    peb->ProcessHeap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
}

PVOID MimeRtlAllocate(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
}

void MimeRtlFree(PVOID p)
{
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}
