#ifndef MIME_HOST_GPU_H
#define MIME_HOST_GPU_H

#include "../vmm/vmm.h"

/* hypercalls of the GPU channels, MIME_HC_STATUS_* results. */
uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel);
uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size);
void mime_gpu_kick(uint64_t channel);
void mime_gpu_close(uint64_t channel);
/* stops all channels before guest memory is unmapped, including channels their processes never closed. */
void mime_gpu_close_all(void);

/* hypercalls of the Vulkan connections, see mime/mvk_wire.h. */
uint64_t mime_vk_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *connection);
void mime_vk_kick(uint64_t connection);
void mime_vk_close(uint64_t connection);
void mime_vk_close_all(void);
/* releases host storage when the last shared Direct3D resource or fence handle closes. */
void mime_vk_share_free(uint64_t id);
/* loads the Vulkan driver in the background to reduce first-connection latency. */
void mime_vk_warm_up(void);
/* logs host time spent on guest calls since the previous report. */
void mime_vk_dump_stats(void);

#endif
