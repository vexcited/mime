#ifndef MIME_CABINET_H
#define MIME_CABINET_H

/*
 * a reader of cabinet files held in memory: the folders of compressed data and the files in them. A folder is
 * stored without compression, with MSZIP (a deflate stream for each block, the history of the blocks before it
 * kept) or with LZX (windows of 2^15 to 2^21 bytes); Quantum is not read. A folder or a file that goes on in another
 * cabinet is not read either: the reader is for cabinets that stand alone, such as the media of an installer.
 */
#include <stddef.h>
#include <stdint.h>

typedef void *(*MIME_CAB_ALLOC)(size_t size);
typedef void (*MIME_CAB_FREE)(void *memory);

typedef struct MIME_CAB MIME_CAB;

typedef struct MIME_CAB_FILE {
    const char *Name; /* as stored, a zero terminated string of bytes in the code page of the maker */
    uint32_t Size;    /* when uncompressed */
    uint32_t Offset;  /* in the uncompressed data of its folder */
    uint16_t Folder;
    uint16_t Date, Time; /* as dos has them */
    uint16_t Attributes; /* with _A_NAME_IS_UTF 0x80 */
} MIME_CAB_FILE;

enum {
    MIME_CAB_OK = 0,
    MIME_CAB_BAD,         /* not a cabinet, or one that is cut short or inconsistent */
    MIME_CAB_UNSUPPORTED, /* a compression, or a folder spread over cabinets, that is not read */
    MIME_CAB_CORRUPT,     /* data that does not decompress or does not match its checksum */
    MIME_CAB_MEMORY,
    MIME_CAB_STOPPED, /* the sink asked to stop */
};

/*
 * the cabinet in memory: data is not copied and has to stay as long as the cabinet is open. allocate and release may
 * be null for the ones of the C library.
 */
int MimeCabOpen(MIME_CAB **cab, const void *data, size_t size, MIME_CAB_ALLOC allocate, MIME_CAB_FREE release);
void MimeCabClose(MIME_CAB *cab);

/* the set the cabinet belongs to and its place in it, for the callers that look at spanned cabinets. */
void MimeCabSet(const MIME_CAB *cab, uint16_t *id, uint16_t *index, uint16_t *flags);

/* MimeCabExtract leaves out the files that begin or go on in another cabinet instead of refusing the cabinet. */
void MimeCabSkipContinued(MIME_CAB *cab);
/* MimeCabExtract finishes a file before it begins the next, where it can: it holds the data of one file at a time. */
void MimeCabSequential(MIME_CAB *cab);

unsigned MimeCabFolderCount(const MIME_CAB *cab);
unsigned MimeCabFileCount(const MIME_CAB *cab);
int MimeCabFile(const MIME_CAB *cab, unsigned index, MIME_CAB_FILE *file);
/* the compression type word of a folder: 0 none, 1 mszip, 3 lzx with the window bits in the high byte. */
int MimeCabFolderType(const MIME_CAB *cab, unsigned folder, uint16_t *type);

/*
 * the uncompressed data of a folder, from its start, in pieces whose offset in the folder is given; the sink returns 0
 * to go on. A folder is only read forwards, so the caller that wants some files of it takes them in one pass.
 * *progress, when given, is how many bytes of the folder were produced when the call returned.
 */
typedef int (*MIME_CAB_SINK)(void *context, uint32_t offset, const uint8_t *data, size_t length);
int MimeCabFolderRead(MIME_CAB *cab, unsigned folder, MIME_CAB_SINK sink, void *context);

/*
 * all the files of a folder in one pass: begin is called for each, in the order they are stored, and returns the
 * context for its data, or null to skip it (and its sink is not called); write gets the pieces of the file in order,
 * end when it is whole.
 */
typedef struct MIME_CAB_FILE_SINK {
    void *(*Begin)(void *context, const MIME_CAB_FILE *file);
    int (*Write)(void *context, void *file_context, const uint8_t *data, size_t length);
    int (*End)(void *context, void *file_context, const MIME_CAB_FILE *file);
} MIME_CAB_FILE_SINK;

int MimeCabExtract(MIME_CAB *cab, const MIME_CAB_FILE_SINK *sink, void *context);

#endif
