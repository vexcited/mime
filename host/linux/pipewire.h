#ifndef MIME_HOST_LINUX_PIPEWIRE_H
#define MIME_HOST_LINUX_PIPEWIRE_H

#include <mime/audio.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct MimePwStream MimePwStream;

/*
 * runs on the sound server's own thread, which may not wait. an output stream fills data with frames, an input
 * stream takes them from it.
 */
typedef void (*MimePwProcess)(void *ctx, uint8_t *data, uint32_t frames);

/* connects to the sound server and keeps what it tells of its devices. false without PipeWire. */
bool mime_pipewire_start(void);
/* the devices of a direction as the server has them now, the one it plays to or records from flagged. */
void mime_pipewire_list(bool capture, MimeAudioDeviceList *out);
/*
 * a stream of samples of 16, 24 or 32 bits, on the device of that id or, with 0, on the default one as it changes.
 * the server converts the format, the rate and the channels. it starts paused. null when it cannot be made.
 */
MimePwStream *mime_pipewire_open(bool capture, uint32_t device, uint32_t rate, uint32_t channels, uint32_t bits,
                                 bool is_float, MimePwProcess process, void *ctx);
bool mime_pipewire_run(MimePwStream *stream, bool running);
/* the server gave the stream up, as when it cannot play its format. it plays nothing from then on. */
bool mime_pipewire_failed(const MimePwStream *stream);
/* returns once no call of process is running or will run. */
void mime_pipewire_close(MimePwStream *stream);

#endif
