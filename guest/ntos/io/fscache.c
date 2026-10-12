/*
 * file data cache of the host backed volumes, so reading a file again
 * costs no exit to the host. files are cached in 64K blocks loaded on
 * demand. an entry is checked against the file size and write time on
 * every open, and dropped as soon as the guest opens the file for writing.
 */
#include "../ntos.h"

#define CACHE_BLOCK (64 * 1024)
#define CACHE_BLOCK_PAGES (CACHE_BLOCK / PAGE_SIZE)
#define CACHE_BUDGET (256ull << 20)
#define CACHE_BUCKETS 256

struct _MIMEFS_CACHE {
    struct _MIMEFS_CACHE *Next; /* hash chain */
    LIST_ENTRY Lru;             /* most recently opened first */
    ULONG Volume;
    LONG References;
    ULONG64 FileId;
    ULONG64 Size;
    ULONG64 WriteTime;
    ULONG BlockCount;
    ULONG64 *Blocks; /* physical address of each loaded block, 0 when absent */
};

#define DETACHED (~0ull)

static FAST_MUTEX g_lock;
static MIMEFS_CACHE *g_buckets[CACHE_BUCKETS];
static LIST_ENTRY g_lru;
static ULONG64 g_cached_bytes;

void FsCacheInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
    InitializeListHead(&g_lru);
}

static ULONG bucket(ULONG volume, ULONG64 file_id)
{
    return (ULONG)((file_id * 0x9E3779B97F4A7C15ull) >> 56 ^ volume) % CACHE_BUCKETS;
}

static void drop_blocks(MIMEFS_CACHE *c)
{
    for (ULONG i = 0; i < c->BlockCount; i++) {
        if (c->Blocks[i]) {
            MmFreeContiguous(c->Blocks[i], CACHE_BLOCK_PAGES);
            c->Blocks[i] = 0;
            g_cached_bytes -= CACHE_BLOCK;
        }
    }
}

/* frees blocks of the least recently opened files until a new block fits. */
static void make_room(MIMEFS_CACHE *keep)
{
    PLIST_ENTRY e = g_lru.Blink;
    while (g_cached_bytes + CACHE_BLOCK > CACHE_BUDGET && e != &g_lru) {
        MIMEFS_CACHE *c = CONTAINING_RECORD(e, MIMEFS_CACHE, Lru);
        e = e->Blink;
        if (c != keep)
            drop_blocks(c);
    }
}

static MIMEFS_CACHE *find(ULONG volume, ULONG64 file_id)
{
    for (MIMEFS_CACHE *c = g_buckets[bucket(volume, file_id)]; c; c = c->Next)
        if (c->Volume == volume && c->FileId == file_id)
            return c;
    return NULL;
}

static void release(MIMEFS_CACHE *c)
{
    RemoveEntryList(&c->Lru);
    ExFreePool(c->Blocks);
    ExFreePool(c);
}

/* forgets a hashed entry; open readers keep it until their close, reading from the host. */
static void detach(MIMEFS_CACHE *c)
{
    MIMEFS_CACHE **link = &g_buckets[bucket(c->Volume, c->FileId)];
    while (*link != c)
        link = &(*link)->Next;
    *link = c->Next;
    drop_blocks(c);
    c->FileId = DETACHED;
    if (c->References == 0)
        release(c);
}

MIMEFS_CACHE *FsCacheOpen(ULONG volume, const MimeFsInfo *info)
{
    if (info->size == 0 || info->file_id == DETACHED)
        return NULL;
    ExAcquireFastMutex(&g_lock);
    MIMEFS_CACHE *c = find(volume, info->file_id);
    if (c && (c->Size != info->size || c->WriteTime != info->write_time)) {
        /* changed behind our back, typically by a host program. */
        detach(c);
        c = NULL;
    }
    if (!c) {
        ULONG64 blocks = (info->size + CACHE_BLOCK - 1) / CACHE_BLOCK;
        c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('M', 'f', 'C', 'f'));
        ULONG64 *array = blocks <= 0x100000 ? ExAllocatePool2(POOL_FLAG_NON_PAGED, blocks * sizeof(ULONG64),
                                                              KI_TAG('M', 'f', 'C', 'b'))
                                            : NULL;
        if (!c || !array) {
            ExFreePool(c);
            ExFreePool(array);
            ExReleaseFastMutex(&g_lock);
            return NULL;
        }
        c->Volume = volume;
        c->FileId = info->file_id;
        c->Size = info->size;
        c->WriteTime = info->write_time;
        c->BlockCount = (ULONG)blocks;
        c->Blocks = array;
        ULONG b = bucket(volume, info->file_id);
        c->Next = g_buckets[b];
        g_buckets[b] = c;
    } else {
        RemoveEntryList(&c->Lru);
    }
    InsertHeadList(&g_lru, &c->Lru);
    c->References++;
    ExReleaseFastMutex(&g_lock);
    return c;
}

void FsCacheClose(MIMEFS_CACHE *c)
{
    ExAcquireFastMutex(&g_lock);
    if (--c->References == 0 && c->FileId == DETACHED)
        release(c);
    ExReleaseFastMutex(&g_lock);
}

void FsCacheInvalidate(ULONG volume, ULONG64 file_id)
{
    ExAcquireFastMutex(&g_lock);
    MIMEFS_CACHE *c = find(volume, file_id);
    if (c)
        detach(c);
    ExReleaseFastMutex(&g_lock);
}

/*
 * reads through the cache. returns FALSE when the data must come from the
 * host instead: the entry was invalidated or no memory is left.
 */
BOOLEAN FsCacheRead(MIMEFS_CACHE *c, ULONG64 host_handle, ULONG64 offset, PUCHAR out, ULONG length, PULONG done)
{
    *done = 0;
    ExAcquireFastMutex(&g_lock);
    if (c->FileId == DETACHED) {
        ExReleaseFastMutex(&g_lock);
        return FALSE;
    }
    if (offset >= c->Size) {
        ExReleaseFastMutex(&g_lock);
        return TRUE;
    }
    if (length > c->Size - offset)
        length = (ULONG)(c->Size - offset);
    while (*done < length) {
        ULONG64 pos = offset + *done;
        ULONG index = (ULONG)(pos / CACHE_BLOCK);
        ULONG in = (ULONG)(pos % CACHE_BLOCK);
        if (!c->Blocks[index]) {
            make_room(c);
            ULONG64 pa = MmAllocateContiguous(CACHE_BLOCK_PAGES);
            if (!pa) {
                ExReleaseFastMutex(&g_lock);
                return *done == 0 ? FALSE : TRUE;
            }
            ULONG64 want = c->Size - (ULONG64)index * CACHE_BLOCK;
            if (want > CACHE_BLOCK)
                want = CACHE_BLOCK;
            ULONG64 got = 0;
            ULONG64 hs = HalHypercall(MIME_HC_FS_READ, host_handle, (ULONG64)index * CACHE_BLOCK, pa, want, 0, 0, &got);
            if (hs != MIME_HC_STATUS_OK || got != want) {
                MmFreeContiguous(pa, CACHE_BLOCK_PAGES);
                ExReleaseFastMutex(&g_lock);
                return *done == 0 ? FALSE : TRUE;
            }
            c->Blocks[index] = pa;
            g_cached_bytes += CACHE_BLOCK;
        }
        ULONG n = CACHE_BLOCK - in < length - *done ? CACHE_BLOCK - in : length - *done;
        memcpy(out + *done, (PUCHAR)HalPhysmap(c->Blocks[index]) + in, n);
        *done += n;
    }
    ExReleaseFastMutex(&g_lock);
    return TRUE;
}
