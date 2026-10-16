/*
 * the connection of the process to the host's vulkan driver: calls are
 * encoded into per thread scratch memory, then copied into the ring in
 * order. queries wait for their reply; everything else returns at once.
 */
#include "mvk_guest.h"

#include <stdarg.h>

NTSYSCALLAPI NTSTATUS NTAPI NtGdiMimeVkOpen(PVOID *base);
NTSYSCALLAPI NTSTATUS NTAPI NtGdiMimeVkAlloc(SIZE_T size, PVOID *base, PULONG64 pa);
NTSYSCALLAPI NTSTATUS NTAPI NtGdiMimeVkFree(PVOID base);
NTSYSCALLAPI NTSTATUS NTAPI NtGdiMimeVkKick(void);
NTSYSCALLAPI NTSTATUS NTAPI NtGdiMimeVkWait(ULONG64 serial, ULONG timeout_ms);

int _fltused = 0x9875;

static uint8_t *g_base;
static MvkHeader *g_header;
static uint8_t *g_ring, *g_reply;
static SRWLOCK g_connect_lock = SRWLOCK_INIT;
static SRWLOCK g_ring_lock = SRWLOCK_INIT;
static SRWLOCK g_reply_lock = SRWLOCK_INIT;
static uint64_t g_serial;
static DWORD g_tls = TLS_OUT_OF_INDEXES;

void mvk_log(const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(text, sizeof(text) - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = (int)sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

BOOL mvk_connect(void)
{
    if (g_header)
        return TRUE;
    AcquireSRWLockExclusive(&g_connect_lock);
    if (!g_header) {
        PVOID base = NULL;
        if (NT_SUCCESS(NtGdiMimeVkOpen(&base)) && base) {
            g_base = base;
            g_ring = g_base + MVK_RING_OFFSET;
            g_reply = g_base + MVK_REPLY_OFFSET;
            g_tls = TlsAlloc();
            __atomic_store_n(&g_header, (MvkHeader *)g_base, __ATOMIC_RELEASE);
        }
    }
    ReleaseSRWLockExclusive(&g_connect_lock);
    return g_header != NULL;
}

/* ------------------------------------------------------------------ ids */

/* ids are reused once their object is destroyed: the host runs both in ring order. */
static SRWLOCK g_id_lock = SRWLOCK_INIT;
static uint64_t g_next_id = 1;
static uint64_t *g_free_ids;
static size_t g_free_count, g_free_capacity;

uint64_t mvk_new_id(void)
{
    AcquireSRWLockExclusive(&g_id_lock);
    uint64_t id = g_free_count ? g_free_ids[--g_free_count] : g_next_id++;
    ReleaseSRWLockExclusive(&g_id_lock);
    return id;
}

void mvk_free_id(uint64_t id)
{
    if (!id)
        return;
    AcquireSRWLockExclusive(&g_id_lock);
    if (g_free_count == g_free_capacity) {
        size_t n = g_free_capacity ? g_free_capacity * 2 : 4096;
        uint64_t *list = HeapAlloc(GetProcessHeap(), 0, n * sizeof(uint64_t));
        if (list) {
            if (g_free_ids) {
                memcpy(list, g_free_ids, g_free_count * sizeof(uint64_t));
                HeapFree(GetProcessHeap(), 0, g_free_ids);
            }
            g_free_ids = list;
            g_free_capacity = n;
        }
    }
    if (g_free_count < g_free_capacity)
        g_free_ids[g_free_count++] = id;
    ReleaseSRWLockExclusive(&g_id_lock);
}

uint64_t mvk_dispatch_id(const void *object)
{
    return object ? ((const MvkObject *)object)->id : 0;
}

/* ------------------------------------------------------------------ blocks */

typedef struct Deferred {
    struct Deferred *next;
    void *base;
    uint64_t serial;
} Deferred;

static SRWLOCK g_block_lock = SRWLOCK_INIT;
static Deferred *g_deferred;

void *mvk_block_alloc(SIZE_T size, ULONG64 *pa)
{
    PVOID base = NULL;
    if (!mvk_connect() || !NT_SUCCESS(NtGdiMimeVkAlloc(size, &base, pa)))
        return NULL;
    return base;
}

static void release_deferred(BOOL all)
{
    uint64_t done = g_header ? __atomic_load_n(&g_header->done, __ATOMIC_ACQUIRE) : ~0ull;
    AcquireSRWLockExclusive(&g_block_lock);
    for (Deferred **link = &g_deferred; *link;) {
        Deferred *d = *link;
        if (all || d->serial <= done) {
            *link = d->next;
            NtGdiMimeVkFree(d->base);
            HeapFree(GetProcessHeap(), 0, d);
        } else {
            link = &d->next;
        }
    }
    ReleaseSRWLockExclusive(&g_block_lock);
}

void mvk_block_free(void *base)
{
    if (base)
        NtGdiMimeVkFree(base);
}

/* frees a block once the host ran the call that stopped its use. */
void mvk_block_free_after(void *base, uint64_t serial)
{
    Deferred *d = HeapAlloc(GetProcessHeap(), 0, sizeof(*d));
    if (!d)
        return;
    d->base = base;
    d->serial = serial;
    AcquireSRWLockExclusive(&g_block_lock);
    d->next = g_deferred;
    g_deferred = d;
    ReleaseSRWLockExclusive(&g_block_lock);
}

/* ------------------------------------------------------------------ calls */

typedef struct Scratch {
    MvkEncoder enc;
    uint8_t *buffer;
    size_t size;
} Scratch;

static int grow(MvkEncoder *e, size_t n)
{
    Scratch *s = (Scratch *)e;
    size_t used = (size_t)(e->p - e->start);
    size_t want = s->size;
    while (want - used < n + 64)
        want *= 2;
    if (want > MVK_MAX_CALL)
        return 0;
    uint8_t *buffer = HeapAlloc(GetProcessHeap(), 0, want);
    if (!buffer)
        return 0;
    memcpy(buffer, s->buffer, used);
    HeapFree(GetProcessHeap(), 0, s->buffer);
    s->buffer = buffer;
    s->size = want;
    e->start = buffer;
    e->p = buffer + used;
    e->end = buffer + want;
    return 1;
}

static Scratch *scratch(void)
{
    Scratch *s = TlsGetValue(g_tls);
    if (!s) {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s)
            return NULL;
        s->size = 64 << 10;
        s->buffer = HeapAlloc(GetProcessHeap(), 0, s->size);
        TlsSetValue(g_tls, s);
    }
    return s;
}

MvkEncoder *mvk_call_begin(uint32_t op)
{
    if (!mvk_connect())
        return NULL;
    Scratch *s = scratch();
    if (!s || !s->buffer)
        return NULL;
    s->enc = (MvkEncoder){s->buffer, s->buffer, s->buffer + s->size, 0, NULL, grow};
    MvkCallHeader *h = mvk_reserve(&s->enc, sizeof(MvkCallHeader), 8);
    h->op = op;
    return &s->enc;
}

/* copies the call into the ring, waiting for room, and wakes the host when it sleeps. */
static uint64_t submit(MvkEncoder *e, uint32_t reply, uint32_t reply_size)
{
    mvk_align(e, 8);
    uint32_t size = (uint32_t)(e->p - e->start);
    MvkCallHeader *h = (MvkCallHeader *)e->start;
    h->size = size;
    h->reply = reply;
    h->reply_size = reply_size;
    AcquireSRWLockExclusive(&g_ring_lock);
    uint64_t serial = ++g_serial;
    h->serial = serial;
    uint64_t put = g_header->put;
    uint32_t at = (uint32_t)(put % MVK_RING_SIZE);
    uint64_t needed = size;
    if (MVK_RING_SIZE - at < size)
        needed += MVK_RING_SIZE - at;
    while (put + needed - __atomic_load_n(&g_header->get, __ATOMIC_ACQUIRE) > MVK_RING_SIZE) {
        if (g_header->lost)
            break;
        if (__atomic_load_n(&g_header->host_idle, __ATOMIC_SEQ_CST))
            NtGdiMimeVkKick();
        YieldProcessor();
    }
    if (MVK_RING_SIZE - at < size) {
        /* the end of the ring is padding, a nop when one fits. */
        if (MVK_RING_SIZE - at >= sizeof(MvkCallHeader)) {
            MvkCallHeader nop = {MVK_OP_NOP, MVK_RING_SIZE - at, serial, MVK_NO_REPLY, 0};
            memcpy(g_ring + at, &nop, sizeof(nop));
        }
        put += MVK_RING_SIZE - at;
        at = 0;
    }
    memcpy(g_ring + at, e->start, size);
    __atomic_store_n(&g_header->put, put + size, __ATOMIC_RELEASE);
    ReleaseSRWLockExclusive(&g_ring_lock);
    if (__atomic_load_n(&g_header->host_idle, __ATOMIC_SEQ_CST))
        NtGdiMimeVkKick();
    return serial;
}

void mvk_call_end(MvkEncoder *e)
{
    if (!e)
        return;
    if (e->overflow) {
        mvk_log("vulkan: call too large for the ring");
        return;
    }
    submit(e, MVK_NO_REPLY, 0);
    if (g_deferred)
        release_deferred(FALSE);
}

uint64_t mvk_last_serial(void)
{
    return __atomic_load_n(&g_serial, __ATOMIC_ACQUIRE);
}

void mvk_wait_serial(uint64_t serial)
{
    /* a short spin catches a quick reply without sleeping. */
    for (int i = 0; i < 20000; i++) {
        if (__atomic_load_n(&g_header->done, __ATOMIC_ACQUIRE) >= serial)
            return;
        YieldProcessor();
    }
    while (__atomic_load_n(&g_header->done, __ATOMIC_ACQUIRE) < serial && !g_header->lost)
        NtGdiMimeVkWait(serial, 0xFFFFFFFF);
}

/* the count of watched semaphores the host saw reach their values. */
uint64_t mvk_events(void)
{
    return g_header ? __atomic_load_n(&g_header->events, __ATOMIC_ACQUIRE) : 0;
}

void mvk_wait_events(uint64_t seen, ULONG timeout_ms)
{
    /* a short spin catches work about to finish without sleeping. */
    for (int i = 0; i < 20000; i++) {
        if (__atomic_load_n(&g_header->events, __ATOMIC_ACQUIRE) != seen || g_header->lost)
            return;
        YieldProcessor();
    }
    NtGdiMimeVkWait(MVK_WAIT_EVENTS | seen, timeout_ms);
}

static MvkDecoder g_reply_decoder;

/* the reply space is one: a thread holds it from its query until it read the reply. */
MvkDecoder *mvk_call_wait(MvkEncoder *e)
{
    if (!e || e->overflow)
        return NULL;
    AcquireSRWLockExclusive(&g_reply_lock);
    uint64_t serial = submit(e, 0, MVK_REPLY_SIZE);
    mvk_wait_serial(serial);
    uint32_t status = 1;
    memcpy(&status, g_reply, 4);
    if (g_header->lost || status != 0) {
        ReleaseSRWLockExclusive(&g_reply_lock);
        return NULL;
    }
    g_reply_decoder = (MvkDecoder){g_reply, g_reply + 8, g_reply + MVK_REPLY_SIZE, 0, NULL, NULL, 0, 0};
    return &g_reply_decoder;
}

void mvk_call_done(MvkDecoder *d)
{
    if (d)
        ReleaseSRWLockExclusive(&g_reply_lock);
}

/* ------------------------------------------------------------------ chains */

void mvk_put_chain(MvkEncoder *e, const void *next)
{
    const VkBaseInStructure *s = next;
    while (s && !mvk_struct_size(s->sType))
        s = s->pNext;
    if (!s) {
        mvk_put_u32(e, 0);
        return;
    }
    mvk_put_u32(e, (uint32_t)s->sType);
    mvk_put_chain_struct(e, s);
}

/* an output struct: its sType and the sTypes of its chain the protocol knows, ending with 0. */
void mvk_put_out_shape(MvkEncoder *e, const VkBaseOutStructure *s)
{
    mvk_put_u32(e, (uint32_t)s->sType);
    for (const VkBaseOutStructure *n = s->pNext; n; n = n->pNext)
        if (mvk_struct_size(n->sType))
            mvk_put_u32(e, (uint32_t)n->sType);
    mvk_put_u32(e, 0);
}

/* fills the caller's chain from a reply chain, which has its known structs in its order. */
void mvk_out_chain(MvkDecoder *d, void *next)
{
    uint32_t type = mvk_get_u32(d);
    if (!type)
        return;
    VkBaseOutStructure *s = next;
    while (s && (uint32_t)s->sType != type)
        s = s->pNext;
    if (!s) {
        d->error = 1;
        d->p = d->end;
        return;
    }
    mvk_out_chain_struct(d, s);
}

void mvk_skip_pointer(MvkDecoder *d)
{
    mvk_get_u32(d);
}

/* the decoders of input structs only run on the host. */
void *mvk_get_chain(MvkDecoder *d)
{
    return NULL;
}

uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable)
{
    return mvk_get_u64(d);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && g_header)
        release_deferred(TRUE);
    return TRUE;
}

uint64_t mvk_done_serial(void)
{
    return g_header ? __atomic_load_n(&g_header->done, __ATOMIC_ACQUIRE) : 0;
}
