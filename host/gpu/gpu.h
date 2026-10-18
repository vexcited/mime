#ifndef MIME_HOST_GPU_H
#define MIME_HOST_GPU_H

#include "../vmm/vmm.h"

/* hypercalls of the gpu channels, MIME_HC_STATUS_* results. */
uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel);
uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size);
void mime_gpu_kick(uint64_t channel);
void mime_gpu_close(uint64_t channel);
/* at the machine's end, whose processes never closed theirs: the host stops using the guest memory before it is unmapped. */
void mime_gpu_close_all(void);

/* hypercalls of the vulkan connections, see mime/mvk_wire.h. */
uint64_t mime_vk_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *connection);
void mime_vk_kick(uint64_t connection);
void mime_vk_close(uint64_t connection);
void mime_vk_close_all(void);
/* loads the host's vulkan driver in the background, so the first connection of the guest does not wait for it. */
void mime_vk_warm_up(void);
/* what the host spent on the guest's calls since the last time, to the log. */
void mime_vk_dump_stats(void);

#endif
