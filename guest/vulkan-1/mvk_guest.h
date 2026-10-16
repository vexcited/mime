/*
 * vulkan-1.dll: the vulkan api of the guest, run by the host's driver
 * through the wire of mime/mvk_wire.h.
 */
#ifndef MVK_GUEST_H
#define MVK_GUEST_H

#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <mime/mvk_wire.h>
#include <vulkan/vulkan_win32.h>

#include "mvk_protocol.h"
#include "mvk_guest_out.h"

#define MVK_LOADER_MAGIC 0x01CDC0DEu

/* every dispatchable object: what loaders expect first, then the wire id. */
typedef struct MvkObject {
    uintptr_t loader_magic;
    uint64_t id;
} MvkObject;

struct VkInstance_T {
    MvkObject obj;
    uint32_t api_version;
    uint32_t physical_count;
    struct VkPhysicalDevice_T *physical[16];
};

struct VkPhysicalDevice_T {
    MvkObject obj;
    VkInstance instance;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    uint32_t memory_known;
};

struct VkQueue_T {
    MvkObject obj;
    VkDevice device;
    uint32_t family, index, flags;
    struct VkQueue_T *next;
};

typedef struct MvkStreamPool MvkStreamPool;

struct VkDevice_T {
    MvkObject obj;
    VkPhysicalDevice physical;
    struct VkQueue_T *queues;
    MvkStreamPool *streams;
};

struct VkCommandBuffer_T {
    MvkObject obj;
    VkDevice device;
    uint64_t pool;
    struct VkCommandBuffer_T *pool_next;
    /* the stream: chunks of guest memory the host reads when the buffer ends. */
    MvkStreamChunk *first, *last;
    uint64_t first_pa, last_pa;
    uint64_t bytes;
    VkCommandBufferBeginInfo begin;
    VkCommandBufferInheritanceInfo inheritance;
    int has_inheritance;
    int recording;
    /* the stream, and the record being encoded: in the stream's last chunk, or in scratch memory. */
    struct Stream *stream;
    MvkEncoder enc;
    int in_place;
    uint8_t *scratch;
    size_t scratch_size;
};

/* main.c: the connection and the calls. */
BOOL mvk_connect(void);
uint64_t mvk_new_id(void);
void mvk_free_id(uint64_t id);
MvkEncoder *mvk_call_begin(uint32_t op);
void mvk_call_end(MvkEncoder *e);
MvkDecoder *mvk_call_wait(MvkEncoder *e);
void mvk_call_done(MvkDecoder *d);
uint64_t mvk_last_serial(void);
void mvk_wait_serial(uint64_t serial);
uint64_t mvk_events(void);
void mvk_wait_events(uint64_t seen, ULONG timeout_ms);
void *mvk_block_alloc(SIZE_T size, ULONG64 *pa);
void mvk_block_free(void *base);
void mvk_block_free_after(void *base, uint64_t serial);
void mvk_log(const char *fmt, ...);

/* chains: input chains are encoded, output chains are described then filled. */
void mvk_put_chain(MvkEncoder *e, const void *next);
void mvk_put_out_shape(MvkEncoder *e, const VkBaseOutStructure *s);
void mvk_out_chain(MvkDecoder *d, void *next);
void mvk_out_chain_struct(MvkDecoder *d, VkBaseOutStructure *s);
void mvk_skip_pointer(MvkDecoder *d);
uint64_t mvk_dispatch_id(const void *object);

/* command buffers: commands go to the buffer's stream. */
MvkEncoder *mvk_cmd_begin(VkCommandBuffer buffer, uint32_t op);
void mvk_cmd_end(VkCommandBuffer buffer, MvkEncoder *e);

typedef struct MvkProc {
    const char *name;
    PFN_vkVoidFunction fn;
} MvkProc;
extern const MvkProc mvk_procs[];

/* the generated entry points the hand written ones extend. */
VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physical,
                                                                       VkPhysicalDeviceMemoryProperties *properties);
VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physical,
                                                                        VkPhysicalDeviceMemoryProperties2 *properties);
VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceProperties(VkPhysicalDevice physical,
                                                                 VkPhysicalDeviceProperties *properties);
VKAPI_ATTR void VKAPI_CALL mvk_raw_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physical,
                                                                  VkPhysicalDeviceProperties2 *properties);
VKAPI_ATTR VkResult VKAPI_CALL mvk_raw_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
                                                           const VkAllocationCallbacks *allocator, VkCommandPool *pool);


#endif
