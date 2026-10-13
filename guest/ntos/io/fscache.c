/*
 * file windows of the host backed volumes. a file opened for reading is
 * placed whole and read only in the guest window range by the host
 * (MIME_HC_FS_MAP) when a section maps it or a large read needs it: reads
 * copy from it, and sections use its pages in place. the host fills the
 * window as it is touched, so the guest keeps no copy of its own. small
 * reads of files without a window go to the host, so scanning a few
 * bytes of many files does not fill windows.
 *
 * an entry is checked against the file size and write time on every open,
 * and dropped as soon as the guest opens the file for writing. a dropped
 * entry keeps its window until its last reference goes.
 */
#include "../ntos.h"

#define CACHE_BUCKETS 256
#define CACHE_IDLE_MAX 256 /* unreferenced entries kept for reopening */
#define CACHE_SMALL_READ 0x10000 /* smaller reads of an unmapped file go to the host directly */

struct _MIMEFS_CACHE {
    struct _MIMEFS_CACHE *Next; /* hash chain */
    LIST_ENTRY Lru;             /* most recently opened first */
    ULONG Volume;
    LONG References; /* opens and sections */
    ULONG64 FileId;
    ULONG64 Size;
    ULONG64 WriteTime;
    ULONG64 Window; /* guest physical address, 0 until mapped */
};

#define DETACHED (~0ull)

static FAST_MUTEX g_lock;
static MIMEFS_CACHE *g_buckets[CACHE_BUCKETS];
static LIST_ENTRY g_lru;
static ULONG g_entries;

void FsCacheInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
    InitializeListHead(&g_lru);
}

static ULONG bucket(ULONG volume, ULONG64 file_id)
{
    return (ULONG)((file_id * 0x9E3779B97F4A7C15ull) >> 56 ^ volume) % CACHE_BUCKETS;
}

static MIMEFS_CACHE *find(ULONG volume, ULONG64 file_id)
{
    for (MIMEFS_CACHE *c = g_buckets[bucket(volume, file_id)]; c; c = c->Next)
        if (c->Volume == volume && c->FileId == file_id)
            return c;
    return NULL;
}

static void unhash(MIMEFS_CACHE *c)
{
    MIMEFS_CACHE **link = &g_buckets[bucket(c->Volume, c->FileId)];
    while (*link != c)
        link = &(*link)->Next;
    *link = c->Next;
    c->FileId = DETACHED;
}

static void release(MIMEFS_CACHE *c)
{
    if (c->Window)
        HalHypercall(MIME_HC_FS_UNMAP, c->Window, 0, 0, 0, 0, 0, NULL);
    RemoveEntryList(&c->Lru);
    g_entries--;
    ExFreePool(c);
}

/* forgets a hashed entry; its references keep the window until they go. */
static void detach(MIMEFS_CACHE *c)
{
    unhash(c);
    if (c->References == 0)
        release(c);
}

/* drops unreferenced entries, oldest first, while there are more than `keep`. */
static void trim(ULONG keep)
{
    PLIST_ENTRY e = g_lru.Blink;
    while (g_entries > keep && e != &g_lru) {
        MIMEFS_CACHE *c = CONTAINING_RECORD(e, MIMEFS_CACHE, Lru);
        e = e->Blink;
        if (c->References == 0) {
            if (c->FileId != DETACHED)
                unhash(c);
            release(c);
        }
    }
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
        c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('M', 'f', 'C', 'f'));
        if (!c) {
            ExReleaseFastMutex(&g_lock);
            return NULL;
        }
        c->Volume = volume;
        c->FileId = info->file_id;
        c->Size = info->size;
        c->WriteTime = info->write_time;
        ULONG b = bucket(volume, info->file_id);
        c->Next = g_buckets[b];
        g_buckets[b] = c;
        g_entries++;
    } else {
        RemoveEntryList(&c->Lru);
    }
    InsertHeadList(&g_lru, &c->Lru);
    c->References++;
    trim(CACHE_IDLE_MAX);
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

/* maps the window of an entry on first use. called with the lock held. */
static BOOLEAN map_window(MIMEFS_CACHE *c, ULONG64 host_handle)
{
    if (c->Window)
        return TRUE;
    if (c->FileId == DETACHED)
        return FALSE;
    for (int attempt = 0; attempt < 2; attempt++) {
        ULONG64 pa = 0;
        ULONG64 hs = HalHypercall(MIME_HC_FS_MAP, host_handle, 0, 0, 0, 0, 0, &pa);
        if (hs == MIME_HC_STATUS_OK) {
            c->Window = pa;
            return TRUE;
        }
        if (hs != MIME_HC_STATUS_NO_MEMORY)
            return FALSE;
        trim(0); /* the window range is full, give back the idle ones */
    }
    return FALSE;
}

/*
 * reads through the window. returns FALSE when the data must come from the
 * host instead: the entry was invalidated or the file cannot be mapped.
 */
BOOLEAN FsCacheRead(MIMEFS_CACHE *c, ULONG64 host_handle, ULONG64 offset, PUCHAR out, ULONG length, PULONG done)
{
    *done = 0;
    if (length < CACHE_SMALL_READ && !__atomic_load_n(&c->Window, __ATOMIC_ACQUIRE))
        return FALSE;
    ExAcquireFastMutex(&g_lock);
    BOOLEAN ok = map_window(c, host_handle);
    ULONG64 window = c->Window;
    ExReleaseFastMutex(&g_lock);
    if (!ok)
        return FALSE;
    if (offset >= c->Size)
        return TRUE;
    if (length > c->Size - offset)
        length = (ULONG)(c->Size - offset);
    /* the caller's open reference keeps the window mapped while it copies. */
    memcpy(out, (PUCHAR)HalPhysmap(window) + offset, length);
    *done = length;
    return TRUE;
}

/* the window of an entry for a section, with a reference released by FsCacheClose. */
BOOLEAN FsCacheGetWindow(MIMEFS_CACHE *c, ULONG64 host_handle, PULONG64 pa, PULONG64 size)
{
    ExAcquireFastMutex(&g_lock);
    BOOLEAN ok = map_window(c, host_handle);
    if (ok) {
        c->References++;
        *pa = c->Window;
        *size = c->Size;
    }
    ExReleaseFastMutex(&g_lock);
    return ok;
}
