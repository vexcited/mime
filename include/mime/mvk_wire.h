/*
 * the vulkan wire of mime, shared by the guest's vulkan-1.dll and the host.
 *
 * a guest process talks to the host through one connection: a block of
 * physically contiguous guest memory holding a header, a call ring and
 * reply space, seen by the host in place. calls are encoded by the guest
 * and run by a host thread in ring order. creations name their objects
 * with ids the guest picks, so they never wait for the host; only queries
 * wait, for their reply. command buffers are recorded into guest memory
 * and handed over whole when they end.
 *
 * values cross as aligned items, see tools/genvk.py: scalars at their
 * size, handles as 64-bit ids, structs member by member, pointed data
 * behind a presence word, pnext chains as sType tagged structs ending with
 * a zero sType.
 */
#ifndef MIME_MVK_WIRE_H
#define MIME_MVK_WIRE_H

#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
/* the guest is freestanding: its runtime has these. */
void *(memcpy)(void *dst, const void *src, size_t n);
void *(memset)(void *dst, int c, size_t n);
size_t strlen(const char *s);
#else
#include <string.h>
#endif

/* the guest implements the api, the host calls its driver through tables. */
#ifndef MVK_GUEST
#define VK_NO_PROTOTYPES 1
#endif
#include <vulkan/vulkan_core.h>

#define MVK_CONNECTION_SIZE (32u << 20) /* the header, the call ring, then reply space */
#define MVK_HEADER_SIZE 4096u
#define MVK_RING_OFFSET MVK_HEADER_SIZE
#define MVK_RING_SIZE (16u << 20)
#define MVK_REPLY_OFFSET (MVK_RING_OFFSET + MVK_RING_SIZE)
#define MVK_REPLY_SIZE (MVK_CONNECTION_SIZE - MVK_REPLY_OFFSET)
#define MVK_MAX_CALL (MVK_RING_SIZE / 2)

#define MVK_MIRRORS 384

typedef struct MvkHeader {
    /* written by the guest. */
    volatile uint64_t put;         /* bytes written to the ring since creation */
    volatile uint64_t wait_serial; /* a thread sleeps until done reaches this, 0 when none */
    uint64_t guest_pad[6];
    /* written by the host. */
    volatile uint64_t get;         /* bytes run */
    volatile uint64_t done;        /* serial of the last call run */
    volatile uint32_t host_idle;   /* the host sleeps until kicked */
    volatile uint32_t lost;        /* the host stopped running this connection after an error */
    volatile uint64_t events;      /* counts the watched semaphores that reached their values */
    uint64_t host_pad[4];
    /* the values of mirrored timeline semaphores, kept by the host as they advance, never ahead. */
    volatile uint64_t mirror[MVK_MIRRORS];
} MvkHeader;

_Static_assert(sizeof(MvkHeader) <= MVK_HEADER_SIZE, "the header fits its page");

/* NtGdiMimeVkWait of a serial with this bit: until events differs from the rest of it. */
#define MVK_WAIT_EVENTS (1ull << 63)

/*
 * a call: this header, then its arguments. serial counts calls; a call
 * waiting for its reply names the place of the reply in the reply space.
 */
typedef struct MvkCallHeader {
    uint32_t op;
    uint32_t size;      /* bytes, header included, multiple of 8 */
    uint64_t serial;
    uint32_t reply;     /* offset in the reply space, or MVK_NO_REPLY */
    uint32_t reply_size;
} MvkCallHeader;

#define MVK_NO_REPLY 0xFFFFFFFFu

/* the calls the generator does not describe, numbered from here. */
enum {
    MVK_OP_HAND = 0x10000,
    MVK_OP_NOP = MVK_OP_HAND,       /* padding to the end of the ring */
    MVK_OP_CREATE_INSTANCE,
    MVK_OP_DESTROY_INSTANCE,
    MVK_OP_INSTANCE_EXTENSIONS,
    MVK_OP_PHYSICAL_DEVICES,
    MVK_OP_DEVICE_EXTENSIONS,
    MVK_OP_CREATE_DEVICE,
    MVK_OP_DESTROY_DEVICE,
    MVK_OP_GET_QUEUE,
    MVK_OP_ALLOCATE_MEMORY,
    MVK_OP_FREE_MEMORY,
    MVK_OP_ALLOCATE_COMMAND_BUFFERS,
    MVK_OP_FREE_COMMAND_BUFFERS,
    MVK_OP_RECORD,                  /* a command buffer's commands, from its stream */
    MVK_OP_RESET_COMMAND_BUFFER,
    MVK_OP_RESET_COMMAND_POOL,
    MVK_OP_DESTROY_COMMAND_POOL,
    MVK_OP_QUEUE_SUBMIT,
    MVK_OP_QUEUE_SUBMIT2,
    MVK_OP_QUEUE_WAIT_IDLE,
    MVK_OP_DEVICE_WAIT_IDLE,
    MVK_OP_CREATE_FENCE,
    MVK_OP_DESTROY_FENCE,
    MVK_OP_WAIT_FENCES,
    MVK_OP_FENCE_STATUS,
    MVK_OP_RESET_FENCES,
    MVK_OP_CREATE_SEMAPHORE,
    MVK_OP_DESTROY_SEMAPHORE,
    MVK_OP_WAIT_SEMAPHORES,
    MVK_OP_SEMAPHORE_VALUE,
    MVK_OP_SIGNAL_SEMAPHORE,
    MVK_OP_CREATE_EVENT,
    MVK_OP_DESTROY_EVENT,
    MVK_OP_EVENT_STATUS,
    MVK_OP_SET_EVENT,
    MVK_OP_RESET_EVENT,
    MVK_OP_CREATE_DESCRIPTOR_POOL,
    MVK_OP_DESTROY_DESCRIPTOR_POOL,
    MVK_OP_RESET_DESCRIPTOR_POOL,
    MVK_OP_ALLOCATE_DESCRIPTOR_SETS,
    MVK_OP_FREE_DESCRIPTOR_SETS,
    MVK_OP_QUERY_POOL_RESULTS,
    MVK_OP_QUEUE_BIND_SPARSE,
    MVK_OP_CREATE_SWAPCHAIN,
    MVK_OP_DESTROY_SWAPCHAIN,
    MVK_OP_ACQUIRE_IMAGE,
    MVK_OP_PRESENT,
    MVK_OP_GET_DESCRIPTOR,
    MVK_OP_WATCH_SEMAPHORES,        /* timeline values whose reaching counts in events */
    MVK_OP_MIRROR_SEMAPHORE,        /* a timeline semaphore whose value the host keeps in a header slot */
    MVK_OP_VIDEO_CAPS,              /* the decoding profiles of the gpu, see mvk_video.h */
    MVK_OP_VIDEO_CREATE,            /* a decoder session */
    MVK_OP_VIDEO_DESTROY,
    MVK_OP_VIDEO_DECODE,            /* a picture, from dxva buffers */
    MVK_OP_HAND_END,
};

/* the stream of a command buffer: chunks of guest memory linked by guest physical address. */
#define MVK_STREAM_CHUNK (64u << 10)
typedef struct MvkStreamChunk {
    uint64_t next_pa; /* 0 at the end */
    uint32_t used;    /* bytes of commands after this header */
    uint32_t pad;
} MvkStreamChunk;

/* a command of a stream: its op, and its size, this header included, a multiple of 8. */
typedef struct MvkStreamCommand {
    uint32_t op;
    uint32_t size;
} MvkStreamCommand;

/* ------------------------------------------------------------------ items */

typedef struct MvkEncoder {
    uint8_t *start, *p, *end;
    int overflow;
    void *owner; /* the side's own context */
    /* makes room for n more bytes, for encoders over memory that can grow. */
    int (*grow)(struct MvkEncoder *e, size_t n);
} MvkEncoder;

typedef struct MvkDecoder {
    const uint8_t *start, *p, *end;
    int error;
    void *owner;
    /* the arena pointed data is decoded into, reset after each call. */
    uint8_t *arena;
    size_t arena_used, arena_size;
} MvkDecoder;

/* fixed sizes inline even where the runtime is built without builtins. */
#if defined(__GNUC__) || defined(__clang__)
#define MVK_COPY __builtin_memcpy
#else
#define MVK_COPY memcpy
#endif

static inline void mvk_align(MvkEncoder *e, size_t a)
{
    size_t pad = (size_t)(-(uintptr_t)(e->p - e->start)) & (a - 1);
    if ((size_t)(e->end - e->p) < pad && e->grow)
        e->grow(e, pad);
    if ((size_t)(e->end - e->p) < pad) {
        e->overflow = 1;
        e->p = e->end;
        return;
    }
    for (size_t i = 0; i < pad; i++)
        e->p[i] = 0;
    e->p += pad;
}

static inline void *mvk_reserve(MvkEncoder *e, size_t n, size_t a)
{
    mvk_align(e, a);
    if ((size_t)(e->end - e->p) < n && e->grow)
        e->grow(e, n);
    if ((size_t)(e->end - e->p) < n) {
        e->overflow = 1;
        return nullptr;
    }
    void *at = e->p;
    e->p += n;
    return at;
}

static inline void mvk_put_u8(MvkEncoder *e, uint8_t v)
{
    uint8_t *at = mvk_reserve(e, 1, 1);
    if (at)
        *at = v;
}

static inline void mvk_put_u16(MvkEncoder *e, uint16_t v)
{
    void *at = mvk_reserve(e, 2, 2);
    if (at)
        MVK_COPY(at, &v, 2);
}

static inline void mvk_put_u32(MvkEncoder *e, uint32_t v)
{
    void *at = mvk_reserve(e, 4, 4);
    if (at)
        MVK_COPY(at, &v, 4);
}

static inline void mvk_put_u64(MvkEncoder *e, uint64_t v)
{
    void *at = mvk_reserve(e, 8, 8);
    if (at)
        MVK_COPY(at, &v, 8);
}

static inline void mvk_put_bytes(MvkEncoder *e, const void *data, size_t n)
{
    void *at = mvk_reserve(e, n, 4);
    if (at && n)
        MVK_COPY(at, data, n);
}

/* bytes of a known length, NULL as an all ones length. */
static inline void mvk_put_blob(MvkEncoder *e, const void *data, size_t n)
{
    mvk_put_u64(e, data ? (uint64_t)n : ~0ull);
    if (data)
        mvk_put_bytes(e, data, n);
}

static inline void mvk_put_string(MvkEncoder *e, const char *s)
{
    if (!s) {
        mvk_put_u32(e, 0xFFFFFFFFu);
        return;
    }
    uint32_t n = (uint32_t)strlen(s);
    mvk_put_u32(e, n);
    mvk_put_bytes(e, s, n + 1);
}

static inline void mvk_put_handle(MvkEncoder *e, int dispatchable, uint64_t id)
{
    (void)dispatchable;
    mvk_put_u64(e, id);
}

static inline uint32_t mvk_f32_bits(float f)
{
    uint32_t v;
    MVK_COPY(&v, &f, 4);
    return v;
}

static inline uint64_t mvk_f64_bits(double f)
{
    uint64_t v;
    MVK_COPY(&v, &f, 8);
    return v;
}

static inline float mvk_bits_f32(uint32_t v)
{
    float f;
    MVK_COPY(&f, &v, 4);
    return f;
}

static inline double mvk_bits_f64(uint64_t v)
{
    double f;
    MVK_COPY(&f, &v, 8);
    return f;
}

static inline const void *mvk_take(MvkDecoder *d, size_t n, size_t a)
{
    size_t pad = (size_t)(-(uintptr_t)(d->p - d->start)) & (a - 1);
    if ((size_t)(d->end - d->p) < pad + n) {
        d->error = 1;
        d->p = d->end;
        return nullptr;
    }
    d->p += pad;
    const void *at = d->p;
    d->p += n;
    return at;
}

static inline uint8_t mvk_get_u8(MvkDecoder *d)
{
    const uint8_t *at = mvk_take(d, 1, 1);
    return at ? *at : 0;
}

static inline uint16_t mvk_get_u16(MvkDecoder *d)
{
    uint16_t v = 0;
    const void *at = mvk_take(d, 2, 2);
    if (at)
        MVK_COPY(&v, at, 2);
    return v;
}

static inline uint32_t mvk_get_u32(MvkDecoder *d)
{
    uint32_t v = 0;
    const void *at = mvk_take(d, 4, 4);
    if (at)
        MVK_COPY(&v, at, 4);
    return v;
}

static inline uint64_t mvk_get_u64(MvkDecoder *d)
{
    uint64_t v = 0;
    const void *at = mvk_take(d, 8, 8);
    if (at)
        MVK_COPY(&v, at, 8);
    return v;
}

static inline void mvk_get_bytes(MvkDecoder *d, void *out, size_t n)
{
    const void *at = mvk_take(d, n, 4);
    if (at && n)
        MVK_COPY(out, at, n);
    else if (n)
        memset(out, 0, n);
}

static inline void *mvk_alloc(MvkDecoder *d, size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!n)
        n = 16;
    if (d->arena_size - d->arena_used < n) {
        d->error = 1;
        static uint8_t scratch[64] __attribute__((aligned(16)));
        memset(scratch, 0, sizeof(scratch));
        return n <= sizeof(scratch) ? scratch : nullptr;
    }
    void *p = d->arena + d->arena_used;
    d->arena_used += n;
    memset(p, 0, n);
    return p;
}

/* a string, pointing into the call itself. */
static inline const char *mvk_get_string(MvkDecoder *d)
{
    uint32_t n = mvk_get_u32(d);
    if (n == 0xFFFFFFFFu)
        return nullptr;
    const char *s = mvk_take(d, (size_t)n + 1, 4);
    if (!s || s[n] != 0) {
        d->error = 1;
        return "";
    }
    return s;
}

static inline const void *mvk_get_blob(MvkDecoder *d, size_t *size)
{
    uint64_t n = mvk_get_u64(d);
    if (n == ~0ull) {
        if (size)
            *size = 0;
        return nullptr;
    }
    if (size)
        *size = (size_t)n;
    return mvk_take(d, (size_t)n, 4);
}

#endif
