#ifndef MIME_HOST_AUDIO_H
#define MIME_HOST_AUDIO_H

#include "../vmm/vmm.h"

/* points the audio library at the session's sound server, before the daemon starts threads. */
void mime_audio_prepare(void);
/* hypercalls of the audio output streams, MIME_HC_STATUS_* results. */
uint64_t mime_audio_open(MimeVm *vm, uint64_t pa, uint64_t size, uint64_t *stream);
uint64_t mime_audio_start(uint64_t stream, uint64_t running);
void mime_audio_close(uint64_t stream);
void mime_audio_close_all(void);

#endif
