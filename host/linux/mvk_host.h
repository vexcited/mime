/*
 * the host half of the vulkan wire, for the generated decoders and the
 * hand written calls: a connection per guest process, its handle table and
 * the calls being run.
 */
#ifndef MIME_MVK_HOST_H
#define MIME_MVK_HOST_H

#include "../vmm/vmm.h"

#include <pthread.h>
#include <stdbool.h>

#include <mime/mvk_wire.h>

#include "mvk_protocol.h"
#include "mvk_dispatch.h"

#define ARENA_SIZE (64u << 20)

typedef struct MvkConnection MvkConnection;

struct MvkConnection {
    MimeVm *vm;
    uint32_t index;
    MvkHeader *header;
    uint8_t *ring;
    uint8_t *reply;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool kicked;
    volatile bool closing;

    uint64_t *handles; /* by guest id */
    size_t handle_count;
    struct {
        uint64_t handle, id;
    } *reverse;        /* host handle to guest id, open addressing */
    size_t reverse_size, reverse_used;

    void *library;
    PFN_vkGetInstanceProcAddr gipa;
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physical_device;
    MvkDispatch fn;
    bool host_import; /* the device imports guest memory */
    bool timelines;   /* the device has timeline semaphores, whether the guest asked for them or not */
    pthread_mutex_t queue_lock; /* the queues, which the presenting thread submits to as well */
    uint32_t queue_count;
    struct {
        VkQueue queue;
        uint32_t family;
    } queues[64];
    struct MvkWsi *wsi; /* vkwsi.c */
    VkDeviceSize import_alignment;

    /* pipelines being created aside, in the order of their calls, see vulkan.c. */
    struct MvkJob *jobs, *last_job;
    pthread_mutex_t job_lock;
    pthread_cond_t job_done;
    volatile uint32_t jobs_finished;
    uint32_t jobs_seen;

    /* timeline values the guest waits for, see vulkan.c. */
    pthread_t watcher;
    bool watching;
    volatile bool watch_stop;
    pthread_mutex_t watch_lock;
    pthread_cond_t watch_round;
    struct MvkWatch *watches;
    uint32_t watch_count, watch_capacity;
    VkSemaphore wake;
    uint64_t wake_value;
    uint64_t rounds;

    uint8_t *call;   /* a private copy of the call being run */
    uint8_t *arena;
    uint8_t *stream; /* a private copy of a command buffer's stream */
    size_t stream_size;
};

uint64_t mvk_lookup(MvkConnection *c, uint64_t id);
void mvk_bind(MvkConnection *c, uint64_t id, uint64_t handle);

typedef struct MvkCall {
    MvkDecoder in;
    MvkConnection *conn;
    const MvkDispatch *fn;
    VkCommandBuffer command_buffer; /* while a recorded stream is replayed */
    MvkCallHeader header;
    MvkEncoder out;
    int replying;
} MvkCall;

typedef void (*MvkRun)(MvkCall *c);

extern const MvkRun mvk_generated_commands[MVK_OP_GENERATED_COUNT];
extern const char *const mvk_generated_names[MVK_OP_GENERATED_COUNT];

uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable);
void mvk_bind_id(MvkCall *c, uint64_t id, uint64_t handle);
uint64_t mvk_host_id(MvkEncoder *e, uint64_t handle);
void mvk_check(MvkCall *c, VkResult result, const char *what);
void mvk_unsupported(MvkCall *c, const char *what);
/* a call whose arguments did not decode: not run, an error for the guest. */
void mvk_malformed(MvkCall *c, const char *what);
MvkEncoder *mvk_reply_begin(MvkCall *c);
void mvk_reply_end(MvkCall *c);

void *mvk_get_chain(MvkDecoder *d);
void *mvk_get_out_shape(MvkDecoder *d, size_t size);
void mvk_get_out_shape_into(MvkDecoder *d, void *dst, size_t size);
void mvk_reply_chain(MvkEncoder *e, const void *next);
void mvk_reply_chain_struct(MvkEncoder *e, const VkBaseOutStructure *s);

/* the guest side encoders also build on the host; these never run there. */
void mvk_put_chain(MvkEncoder *e, const void *next);
uint64_t mvk_dispatch_id(const void *object);

/* vkwsi.c: surfaces and swapchains. */
extern const char *const mvk_wsi_instance_extensions[];
bool mvk_wsi_device_extension(const char *name);
void mvk_wsi_create_swapchain(MvkCall *call);
void mvk_wsi_destroy_swapchain(MvkCall *call);
void mvk_wsi_acquire(MvkCall *call);
void mvk_wsi_present(MvkCall *call);
void mvk_wsi_close(MvkConnection *c);
/* every present sent so far submitted. */
void mvk_wsi_drain(MvkConnection *c);

#endif
