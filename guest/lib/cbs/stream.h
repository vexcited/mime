#ifndef MIME_CBS_STREAM_H
#define MIME_CBS_STREAM_H

/* the decoders of a 7z folder are pulled streams: a reader asks for bytes and gets fewer only at the end. */
#include "cbs.h"

typedef struct CBS_STREAM CBS_STREAM;

struct CBS_STREAM {
    /* fills up to want bytes; *got is 0 at the end of the data. */
    int (*Read)(CBS_STREAM *stream, uint8_t *buffer, size_t want, size_t *got);
    void (*Destroy)(CBS_STREAM *stream);
    /* set by a stream that is a plain range of memory, which the lzma decoder reads in place. */
    const uint8_t *Data;
    size_t Size;
};

int CbsStreamSlice(CBS_STREAM **out, const uint8_t *data, size_t size);
/* props are the five bytes of the lzma coder; the input has to be a slice. */
int CbsStreamLzma(CBS_STREAM **out, const uint8_t *props, size_t prop_size, CBS_STREAM *input, uint64_t out_size);
/* the four inputs are taken over: main, call, jump and range coder. */
int CbsStreamBcj2(CBS_STREAM **out, CBS_STREAM *inputs[4], uint64_t out_size);
void CbsStreamDestroy(CBS_STREAM *stream);

#endif
