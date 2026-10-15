/*
 * audio output streams have no linux backend yet: opening one fails, as
 * on a machine without an audio device.
 */
#include "../audio/audio.h"

uint64_t mime_audio_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *stream)
{
    *stream = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_audio_start(uint64_t stream, uint64_t running)
{
    return MIME_HC_STATUS_INVALID;
}

void mime_audio_close(uint64_t stream)
{
}
