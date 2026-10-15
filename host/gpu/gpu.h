#ifndef MIME_HOST_GPU_H
#define MIME_HOST_GPU_H

#include "../vmm/vmm.h"

/* hypercalls of the gpu channels, MIME_HC_STATUS_* results. */
uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel);
uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size);
void mime_gpu_kick(uint64_t channel);
void mime_gpu_close(uint64_t channel);

/* hypercalls of the vulkan connections, see mime/mvk_wire.h. */
uint64_t mime_vk_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *connection);
void mime_vk_kick(uint64_t connection);
void mime_vk_close(uint64_t connection);

#endif
