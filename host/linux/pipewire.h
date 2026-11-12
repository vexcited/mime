#ifndef MIME_HOST_LINUX_PIPEWIRE_H
#define MIME_HOST_LINUX_PIPEWIRE_H

#include <mime/audio.h>

#include <stdbool.h>
#include <stdint.h>

/* connects to the sound server and keeps what it tells of its devices. false without PipeWire. */
bool mime_pipewire_start(void);
/*
 * fills the format, the speakers, the form, the adapter and the port of the device the server names like d, the
 * twin-th of that name in the direction. returns -1 when the server has no such device, 1 when it is the default
 * one of the direction and 0 otherwise.
 */
int mime_pipewire_describe(bool capture, uint32_t twin, MimeAudioDevice *d);

#endif
