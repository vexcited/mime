/*
 * a process audio output stream played by the host through mime/audio.h: a ring of frames the caller fills and the host
 * consumes. winmm and dsound write their mixes here, like windows sends them to the audio engine.
 */
#ifndef MIME_LIB_AUDIO_OUTPUT_H
#define MIME_LIB_AUDIO_OUTPUT_H

#include <mime/win32/winbase.h>
#include <mime/audio.h>

typedef struct MimeOutput {
    MimeAudioHeader *hdr;
    BYTE *ring;
    ULONG slot;
    UINT32 frames;      /* ring capacity */
    UINT32 frame_bytes;
    UINT32 rate, channels;
    BOOL started;
} MimeOutput;

/* bits is 16, 24 or 32, and 32 with is_float for floats. */
NTSTATUS MimeOutputOpen(MimeOutput *o, UINT32 rate, UINT32 channels, UINT32 bits, BOOL is_float);
void MimeOutputClose(MimeOutput *o);
NTSTATUS MimeOutputStart(MimeOutput *o);
void MimeOutputStop(MimeOutput *o);

/* frames the host has not played yet, and room for more. */
UINT32 MimeOutputQueued(const MimeOutput *o);
UINT32 MimeOutputSpace(const MimeOutput *o);
/* frames played since the stream opened. */
UINT64 MimeOutputPlayed(const MimeOutput *o);
UINT64 MimeOutputWritten(const MimeOutput *o);

/* returns at most what fits before the ring wraps. commit the frames afterwards. */
BYTE *MimeOutputBegin(MimeOutput *o, UINT32 *n);
void MimeOutputCommit(MimeOutput *o, UINT32 n);
/* NULL data writes silence. returns the frames that fit. */
UINT32 MimeOutputWrite(MimeOutput *o, const void *data, UINT32 n);
/* discards unplayed frames while stopped. */
void MimeOutputDrop(MimeOutput *o);
/* level is 0 to 1, applied by the host. */
void MimeOutputVolume(MimeOutput *o, float level);

/*
 * the threads that keep a stream fed wake every few milliseconds, which a process that never asked for a finer clock
 * does not get: they hold a one millisecond period while they run.
 */
void MimeOutputPeriodBegin(void);
void MimeOutputPeriodEnd(void);

#endif
