#ifndef MIME_CABINET_H
#define MIME_CABINET_H

/*
 * a reader of cabinet files held in memory. folders are stored, MSZIP with history kept across blocks, or LZX with
 * windows of 2^15 to 2^21 bytes; Quantum is not supported. folders or files spanning cabinets are not read either:
 * this is for standalone cabinets such as installer media.
 */
#include <stddef.h>
#include <stdint.h>

typedef void *(*MIME_CAB_ALLOC)(size_t size);
typedef void (*MIME_CAB_FREE)(void *memory);

typedef struct MIME_CAB MIME_CAB;

typedef struct MIME_CAB_FILE {
    const char *Name; /* raw bytes in the creator's code page. */
    uint32_t Size;    /* uncompressed. */
    uint32_t Offset;  /* in the folder's uncompressed data. */
    uint16_t Folder;
    uint16_t Date, Time; /* dos format. */
    uint16_t Attributes; /* _A_NAME_IS_UTF is 0x80. */
} MIME_CAB_FILE;

enum {
    MIME_CAB_OK = 0,
    MIME_CAB_BAD,         /* not a cabinet, or truncated or inconsistent. */
    MIME_CAB_UNSUPPORTED, /* unsupported compression, or a folder spanning cabinets. */
    MIME_CAB_CORRUPT,     /* fails to decompress or to match its checksum. */
    MIME_CAB_MEMORY,
    MIME_CAB_STOPPED, /* the sink asked to stop. */
};

/* data is not copied and must outlive the cabinet. null allocate and release mean the C library's. */
int MimeCabOpen(MIME_CAB **cab, const void *data, size_t size, MIME_CAB_ALLOC allocate, MIME_CAB_FREE release);
void MimeCabClose(MIME_CAB *cab);

/* the cabinet's set id and index in it, for callers that handle spanned cabinets. */
void MimeCabSet(const MIME_CAB *cab, uint16_t *id, uint16_t *index, uint16_t *flags);

/* MimeCabExtract skips files continued from or to another cabinet instead of refusing the cabinet. */
void MimeCabSkipContinued(MIME_CAB *cab);
/* MimeCabExtract finishes each file before beginning the next where it can, so only one is open at a time. */
void MimeCabSequential(MIME_CAB *cab);

unsigned MimeCabFolderCount(const MIME_CAB *cab);
unsigned MimeCabFileCount(const MIME_CAB *cab);
int MimeCabFile(const MIME_CAB *cab, unsigned index, MIME_CAB_FILE *file);
/* 0 none, 1 MSZIP, 3 LZX with the window bits in the high byte. */
int MimeCabFolderType(const MIME_CAB *cab, unsigned folder, uint16_t *type);

/*
 * streams a folder's uncompressed data to the sink in pieces, each with its folder offset; the sink returns 0 to go
 * on. folders only read forwards, so take the files you want in one pass.
 */
typedef int (*MIME_CAB_SINK)(void *context, uint32_t offset, const uint8_t *data, size_t length);
int MimeCabFolderRead(MIME_CAB *cab, unsigned folder, MIME_CAB_SINK sink, void *context);

/*
 * extracts a folder's files in one pass. Begin is called for each in stored order and returns a context, or null to
 * skip the file; Write gets its pieces in order and End is called once it is whole.
 */
typedef struct MIME_CAB_FILE_SINK {
    void *(*Begin)(void *context, const MIME_CAB_FILE *file);
    int (*Write)(void *context, void *file_context, const uint8_t *data, size_t length);
    int (*End)(void *context, void *file_context, const MIME_CAB_FILE *file);
} MIME_CAB_FILE_SINK;

int MimeCabExtract(MIME_CAB *cab, const MIME_CAB_FILE_SINK *sink, void *context);

#endif
