/*
 * an audio output stream of the process, played by the host (mime/audio.h):
 * a ring of frames the caller writes, the host consuming them. winmm and
 * dsound write their mixes here, as windows' write theirs to the audio
 * engine.
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

/* bits is 16, 24 or 32, 32 with is_float for floats. */
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

/* where n frames go next, at most what fits before the end of the ring; then they are committed. */
BYTE *MimeOutputBegin(MimeOutput *o, UINT32 *n);
void MimeOutputCommit(MimeOutput *o, UINT32 n);
/* copies n frames, or silence for a null data, as space allows; returns the frames written. */
UINT32 MimeOutputWrite(MimeOutput *o, const void *data, UINT32 n);
/* what was written and not played is dropped, while stopped. */
void MimeOutputDrop(MimeOutput *o);
/* 0 to 1, applied by the host. */
void MimeOutputVolume(MimeOutput *o, float level);

#endif
