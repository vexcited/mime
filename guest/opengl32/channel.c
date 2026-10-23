/*
 * the process's gpu channel: the command ring, fences, and the heap of guest memory the host gpu reads in place.
 *
 * the ring is published to the host at flushes, waits and every 256K, not per command, and the host is only kicked
 * when it went idle. transient data such as client arrays, uniforms of large programs and texture rows goes into a
 * stream region reused once the gpu passed the fence that covered it. buffer objects get blocks from a first fit heap
 * whose freed ranges wait for their last fence.
 */
#include "opengl32.h"

GlDevice GlDev;

#define STREAM_OFFSET (MIME_GPU_HEADER_SIZE + MIME_GPU_RING_SIZE)
#define STREAM_SIZE (12u << 20)
#define HEAP0_OFFSET (STREAM_OFFSET + STREAM_SIZE)
#define PUBLISH_BYTES (256u << 10)

void gl_device_lock(void)
{
    gl_lock(&GlDev.lock);
}

void gl_device_unlock(void)
{
    gl_unlock(&GlDev.lock);
}

static void heap_add_range(uint32_t block, uint64_t offset, uint64_t size);

BOOL gl_device_open(void)
{
    if (GlDev.open)
        return TRUE;
    if (GlDev.failed)
        return FALSE;
    gl_device_lock();
    if (!GlDev.open && !GlDev.failed) {
        PVOID base = nullptr;
        ULONG size = 0;
        NTSTATUS status = NtGdiMimeGpuOpen(&base, &size);
        if (!NT_SUCCESS(status)) {
            gl_log("no gpu channel: %08x", (ULONG)status);
            GlDev.failed = TRUE;
        } else {
            GlDev.base = base;
            GlDev.hdr = base;
            GlDev.ring = (uint8_t *)base + MIME_GPU_HEADER_SIZE;
            GlDev.put = GlDev.published = GlDev.hdr->put;
            GlDev.block_base[0] = base;
            GlDev.block_size[0] = size;
            GlDev.block_count = 1;
            heap_add_range(0, HEAP0_OFFSET, size - HEAP0_OFFSET);
            GlDev.open = TRUE;
        }
    }
    gl_device_unlock();
    return GlDev.open;
}

/* ---------------------------------------------------------------- ids */

uint32_t gl_new_id(int kind)
{
    gl_device_lock();
    uint32_t id;
    if (GlDev.free_count[kind])
        id = GlDev.free_ids[kind][--GlDev.free_count[kind]];
    else
        id = ++GlDev.next_id[kind];
    gl_device_unlock();
    return id;
}

void gl_free_id(int kind, uint32_t id)
{
    if (!id)
        return;
    gl_device_lock();
    if (GlDev.free_count[kind] == GlDev.free_cap[kind]) {
        uint32_t cap = GlDev.free_cap[kind] ? GlDev.free_cap[kind] * 2 : 64;
        uint32_t *ids = gl_realloc(GlDev.free_ids[kind], cap * sizeof(uint32_t));
        if (!ids) {
            gl_device_unlock();
            return;
        }
        GlDev.free_ids[kind] = ids;
        GlDev.free_cap[kind] = cap;
    }
    GlDev.free_ids[kind][GlDev.free_count[kind]++] = id;
    gl_device_unlock();
}

/* ---------------------------------------------------------------- ring */

void gl_publish(void)
{
    if (!GlDev.open)
        return;
    __atomic_store_n(&GlDev.hdr->put, GlDev.put, __ATOMIC_RELEASE);
    GlDev.published = GlDev.put;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (GlDev.hdr->host_idle)
        NtGdiMimeGpuKick();
}

static void wait_space(uint32_t size)
{
    while (GlDev.put + size - __atomic_load_n(&GlDev.hdr->get, __ATOMIC_ACQUIRE) > MIME_GPU_RING_SIZE) {
        if (GlDev.hdr->lost)
            return;
        gl_publish();
        NtYieldExecution();
    }
}

static uint8_t g_sink[MIME_GPU_MAX_RECORD] __attribute__((aligned(16)));

void *gl_emit(uint32_t op, uint32_t size)
{
    size = (size + 7) & ~7u;
    if (!GlDev.open || GlDev.hdr->lost || size > MIME_GPU_MAX_RECORD) {
        /* nothing reaches the host: the record lands in a sink. */
        memset(g_sink, 0, size < sizeof(g_sink) ? size : sizeof(g_sink));
        return g_sink;
    }
    uint64_t pos = GlDev.put % MIME_GPU_RING_SIZE;
    if (pos + size > MIME_GPU_RING_SIZE) {
        uint32_t pad = (uint32_t)(MIME_GPU_RING_SIZE - pos);
        wait_space(pad);
        MimeGpuCmd *nop = (MimeGpuCmd *)(GlDev.ring + pos);
        nop->op = MIME_GPU_NOP;
        nop->size = pad;
        GlDev.put += pad;
        pos = 0;
    }
    wait_space(size);
    MimeGpuCmd *cmd = (MimeGpuCmd *)(GlDev.ring + pos);
    memset(cmd, 0, size);
    cmd->op = op;
    cmd->size = size;
    GlDev.put += size;
    if (GlDev.put - GlDev.published >= PUBLISH_BYTES)
        gl_publish();
    return cmd;
}

/* ---------------------------------------------------------------- fences */

#define MAX_MARKS 256

/* the stream position each fence covers, oldest first. */
static struct {
    uint64_t pos, fence;
} g_marks[MAX_MARKS];
static uint32_t g_mark_head, g_mark_count;
static uint64_t g_stream_head, g_stream_tail, g_stream_marked;

uint64_t gl_fence(void)
{
    gl_device_lock();
    uint64_t value = ++GlDev.fence;
    MimeGpuFence *f = gl_emit(MIME_GPU_FENCE, sizeof(MimeGpuFence));
    f->value = value;
    if (g_stream_head != g_stream_marked && g_mark_count < MAX_MARKS) {
        uint32_t i = (g_mark_head + g_mark_count++) % MAX_MARKS;
        g_marks[i].pos = g_stream_head;
        g_marks[i].fence = value;
        g_stream_marked = g_stream_head;
    }
    gl_device_unlock();
    return value;
}

uint64_t gl_batch_fence(void)
{
    return GlDev.fence + 1;
}

BOOL gl_fence_done(uint64_t fence)
{
    return !GlDev.open || __atomic_load_n(&GlDev.hdr->done_fence, __ATOMIC_ACQUIRE) >= fence || GlDev.hdr->lost;
}

/* asks the host to commit a fence it holds back while a pass goes on. */
void gl_want(uint64_t fence)
{
    if (!GlDev.open)
        return;
    uint64_t want = __atomic_load_n(&GlDev.hdr->want_fence, __ATOMIC_ACQUIRE);
    while (want < fence &&
           !__atomic_compare_exchange_n(&GlDev.hdr->want_fence, &want, fence, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        ;
}

void gl_wait(uint64_t fence)
{
    if (gl_fence_done(fence))
        return;
    gl_want(fence);
    gl_device_lock();
    gl_publish();
    gl_device_unlock();
    /* short waits spin: ending a sleep costs a host interrupt. */
    for (int i = 0; i < 4000; i++) {
        if (gl_fence_done(fence))
            return;
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause");
#else
        __asm__ volatile("yield");
#endif
    }
    while (!gl_fence_done(fence)) {
        NTSTATUS s = NtGdiMimeGpuWait(fence, 1000);
        if (s == STATUS_DEVICE_REMOVED || s == STATUS_INVALID_DEVICE_STATE)
            return;
    }
}

/* ---------------------------------------------------------------- stream */

MimeGpuMem gl_mem(const GlMem *m)
{
    MimeGpuMem r = {m->block, 0, m->offset};
    return r;
}

/* waits for the oldest fence, making its stream data reusable. */
static BOOL retire_mark(void)
{
    if (!g_mark_count) {
        gl_fence();
        if (!g_mark_count)
            return FALSE;
    }
    uint32_t i = g_mark_head;
    gl_wait(g_marks[i].fence);
    g_stream_tail = g_marks[i].pos;
    g_mark_head = (g_mark_head + 1) % MAX_MARKS;
    g_mark_count--;
    return TRUE;
}

BOOL gl_stream_alloc(uint64_t size, uint32_t align, GlMem *mem)
{
    if (!GlDev.open || size > STREAM_SIZE / 2)
        return FALSE;
    if (align < 16)
        align = 16;
    gl_device_lock();
    uint64_t head = (g_stream_head + align - 1) & ~(uint64_t)(align - 1);
    if (head % STREAM_SIZE + size > STREAM_SIZE)
        head = (head / STREAM_SIZE + 1) * STREAM_SIZE;
    while (head + size - g_stream_tail > STREAM_SIZE) {
        if (!retire_mark()) {
            gl_device_unlock();
            return FALSE;
        }
    }
    g_stream_head = head + size;
    mem->block = 0;
    mem->offset = STREAM_OFFSET + head % STREAM_SIZE;
    mem->ptr = GlDev.base + mem->offset;
    mem->size = size;
    gl_device_unlock();
    return TRUE;
}

/* ---------------------------------------------------------------- heap */

typedef struct Range {
    uint32_t block;
    uint64_t offset, size;
    uint64_t fence; /* free once the gpu passed it. */
} Range;

static Range *g_ranges;
static uint32_t g_range_count, g_range_cap;
static Range *g_retired;
static uint32_t g_retired_count, g_retired_cap;

static void push_range(Range **list, uint32_t *count, uint32_t *cap, Range r)
{
    if (*count == *cap) {
        uint32_t n = *cap ? *cap * 2 : 64;
        Range *l = gl_realloc(*list, n * sizeof(Range));
        if (!l)
            return;
        *list = l;
        *cap = n;
    }
    (*list)[(*count)++] = r;
}

/* keeps free ranges sorted by block and offset, merged with their neighbors. */
static void heap_add_range(uint32_t block, uint64_t offset, uint64_t size)
{
    uint32_t i = 0;
    while (i < g_range_count && (g_ranges[i].block < block || (g_ranges[i].block == block && g_ranges[i].offset < offset)))
        i++;
    if (i > 0 && g_ranges[i - 1].block == block && g_ranges[i - 1].offset + g_ranges[i - 1].size == offset) {
        g_ranges[i - 1].size += size;
        if (i < g_range_count && g_ranges[i].block == block && offset + size == g_ranges[i].offset) {
            g_ranges[i - 1].size += g_ranges[i].size;
            memmove(&g_ranges[i], &g_ranges[i + 1], (g_range_count - i - 1) * sizeof(Range));
            g_range_count--;
        }
        return;
    }
    if (i < g_range_count && g_ranges[i].block == block && offset + size == g_ranges[i].offset) {
        g_ranges[i].offset = offset;
        g_ranges[i].size += size;
        return;
    }
    push_range(&g_ranges, &g_range_count, &g_range_cap, (Range){0});
    memmove(&g_ranges[i + 1], &g_ranges[i], (g_range_count - 1 - i) * sizeof(Range));
    g_ranges[i] = (Range){block, offset, size, 0};
}

static void reclaim(void)
{
    for (uint32_t i = 0; i < g_retired_count;) {
        if (gl_fence_done(g_retired[i].fence)) {
            heap_add_range(g_retired[i].block, g_retired[i].offset, g_retired[i].size);
            g_retired[i] = g_retired[--g_retired_count];
        } else {
            i++;
        }
    }
}

static BOOL take(uint64_t size, GlMem *mem)
{
    for (uint32_t i = 0; i < g_range_count; i++) {
        if (g_ranges[i].size >= size) {
            mem->block = g_ranges[i].block;
            mem->offset = g_ranges[i].offset;
            mem->ptr = GlDev.block_base[mem->block] + mem->offset;
            mem->size = size;
            g_ranges[i].offset += size;
            g_ranges[i].size -= size;
            if (!g_ranges[i].size) {
                memmove(&g_ranges[i], &g_ranges[i + 1], (g_range_count - i - 1) * sizeof(Range));
                g_range_count--;
            }
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL grow(uint64_t size)
{
    uint64_t block = 16u << 20;
    while (block < size)
        block <<= 1;
    if (block > MIME_GPU_MAX_BLOCK_SIZE)
        return FALSE;
    PVOID base = nullptr;
    ULONG index = 0;
    if (!NT_SUCCESS(NtGdiMimeGpuAddBlock((ULONG)block, &base, &index)) || index >= MIME_GPU_MAX_BLOCKS)
        return FALSE;
    GlDev.block_base[index] = base;
    GlDev.block_size[index] = block;
    if (index >= GlDev.block_count)
        GlDev.block_count = index + 1;
    heap_add_range(index, 0, block);
    return TRUE;
}

BOOL gl_heap_alloc(uint64_t size, GlMem *mem)
{
    if (!GlDev.open)
        return FALSE;
    size = (size + 255) & ~255ull;
    if (!size)
        size = 256;
    gl_device_lock();
    reclaim();
    BOOL ok = take(size, mem);
    if (!ok && g_retired_count) {
        /* ranges the gpu still reads are freed once it is done. */
        gl_wait(GlDev.fence);
        gl_fence();
        gl_wait(GlDev.fence);
        reclaim();
        ok = take(size, mem);
    }
    if (!ok && grow(size))
        ok = take(size, mem);
    gl_device_unlock();
    return ok;
}

void gl_heap_free(GlMem *mem, uint64_t fence)
{
    if (!mem->size)
        return;
    gl_device_lock();
    uint64_t size = (mem->size + 255) & ~255ull;
    if (gl_fence_done(fence))
        heap_add_range(mem->block, mem->offset, size);
    else
        push_range(&g_retired, &g_retired_count, &g_retired_cap, (Range){mem->block, mem->offset, size, fence});
    gl_device_unlock();
    memset(mem, 0, sizeof(*mem));
}

/* ---------------------------------------------------------------- caches */

/* state objects by content: pipelines, depth stencil states and samplers. */
typedef struct CacheEntry {
    uint64_t hash;
    uint32_t id;
    uint32_t size;
    void *key;
} CacheEntry;

static struct {
    CacheEntry *entries;
    uint32_t count, cap; /* cap is a power of two. */
} g_cache[ID_KINDS];

uint64_t gl_cache_lookup(int kind, const void *key, SIZE_T size, BOOL *created)
{
    uint64_t h = gl_hash(key, size, (uint64_t)kind);
    *created = FALSE;
    gl_device_lock();
    if (g_cache[kind].count * 2 >= g_cache[kind].cap) {
        uint32_t cap = g_cache[kind].cap ? g_cache[kind].cap * 2 : 256;
        CacheEntry *e = gl_alloc(cap * sizeof(CacheEntry));
        for (uint32_t i = 0; e && i < g_cache[kind].cap; i++) {
            CacheEntry *o = &g_cache[kind].entries[i];
            if (!o->id)
                continue;
            uint32_t j = (uint32_t)o->hash & (cap - 1);
            while (e[j].id)
                j = (j + 1) & (cap - 1);
            e[j] = *o;
        }
        if (e) {
            gl_free(g_cache[kind].entries);
            g_cache[kind].entries = e;
            g_cache[kind].cap = cap;
        }
    }
    uint32_t j = (uint32_t)h & (g_cache[kind].cap - 1);
    for (;;) {
        CacheEntry *e = &g_cache[kind].entries[j];
        if (!e->id) {
            e->hash = h;
            e->size = (uint32_t)size;
            e->key = gl_alloc(size);
            if (e->key)
                memcpy(e->key, key, size);
            e->id = gl_new_id(kind);
            g_cache[kind].count++;
            *created = TRUE;
            gl_device_unlock();
            return e->id;
        }
        if (e->hash == h && e->size == size && e->key && !memcmp(e->key, key, size)) {
            gl_device_unlock();
            return e->id;
        }
        j = (j + 1) & (g_cache[kind].cap - 1);
    }
}
