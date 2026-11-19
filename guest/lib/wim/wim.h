#ifndef MIME_LIB_WIM_H
#define MIME_LIB_WIM_H

#include "../lzms/lzms.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_WIM_MAX_SOLIDS 64
#define MIME_WIM_MAX_CHUNK (64u << 20)

/* the errors of the reader. a read callback's own error, which is positive, comes back as it is. */
enum {
    MIME_WIM_OK = 0,
    MIME_WIM_BAD_FORMAT = -1,
    MIME_WIM_UNSUPPORTED = -2,
    MIME_WIM_NO_MEMORY = -3,
    MIME_WIM_BAD_DATA = -4,
};

typedef struct MimeWimIo {
    void *context;
    int (*read)(void *context, uint64_t offset, size_t length, void *buffer);
    uint64_t (*total)(void *context); /* the size of the image, once a read told it. */
    void *(*alloc)(void *context, size_t size);
    void (*free)(void *context, void *block);
} MimeWimIo;

typedef struct MimeWimSolid {
    uint64_t offset; /* in the image. */
    uint64_t stored;
    uint64_t size; /* uncompressed. */
    uint32_t chunk_size;
    uint32_t chunk_count;
    uint64_t *start; /* image offsets of the chunks and of their end, chunk_count + 1. */
} MimeWimSolid;

/* a stream to find, by its sha-1 and size, and where it was found. */
typedef struct MimeWimStream {
    uint8_t sha1[20];
    uint64_t size;
    bool found;
    int solid;
    uint64_t offset; /* in the uncompressed bytes of the solid resource. */
} MimeWimStream;

typedef struct MimeWim {
    MimeWimIo io;
    uint64_t total;
    MimeWimSolid solids[MIME_WIM_MAX_SOLIDS];
    int solid_count;
    MimeLzms *lzms;
    uint8_t *chunk, *compressed;
    int cached_solid;
    uint32_t cached_chunk;
} MimeWim;

/*
 * reads the header and the table of streams of an esd, the image microsoft's media creation tool fetches, and finds
 * each stream by its sha-1, so a newer image that holds the same file is as good. the ones it does not have stay !found.
 */
int mime_wim_open(MimeWim *wim, const MimeWimIo *io, MimeWimStream *streams, size_t count);
void mime_wim_close(MimeWim *wim);

/* the chunks a stream lies in. */
void mime_wim_chunks(const MimeWim *wim, const MimeWimStream *stream, uint32_t *first, uint32_t *last);
/* where a chunk is in the image, and how big it is there. */
void mime_wim_chunk_range(const MimeWim *wim, int solid, uint32_t chunk, uint64_t *offset, uint64_t *stored);

/* the bytes of a found stream into out, stream->size of them, decoding only the chunks it lies in. */
int mime_wim_read(MimeWim *wim, const MimeWimStream *stream, void *out);

/* the 20 bytes of a sha-1 in hex. */
bool mime_wim_sha1(const char *hex, size_t length, uint8_t *out);

#endif
