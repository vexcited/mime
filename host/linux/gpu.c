/*
 * the gpu channels of opengl32 have no linux backend yet: opening one
 * fails, and opengl32 reports no pixel format with acceleration.
 */
#include "../gpu/gpu.h"

uint64_t mime_gpu_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *channel)
{
    *channel = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_gpu_add_block(MimeVm *vm, uint64_t channel, uint64_t index, uint64_t pa, uint64_t size)
{
    return MIME_HC_STATUS_INVALID;
}

void mime_gpu_kick(uint64_t channel)
{
}

void mime_gpu_close(uint64_t channel)
{
}
