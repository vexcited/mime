#ifndef MIME_HOST_GPU_H
#define MIME_HOST_GPU_H

#include "../vmm/vmm.h"

/* hypercalls of the gpu channels, MIME_HC_STATUS_* results. */
uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel);
uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size);
void mime_gpu_kick(uint64_t channel);
void mime_gpu_close(uint64_t channel);

#endif
