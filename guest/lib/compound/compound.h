#ifndef MIME_COMPOUND_H
#define MIME_COMPOUND_H

#include <stddef.h>
#include <stdint.h>

typedef void *(*MIME_CFB_ALLOC)(size_t size);
typedef void (*MIME_CFB_FREE)(void *memory);

typedef struct MIME_CFB_ENTRY {
    uint16_t Name[32];
    uint32_t Left, Right, Child, Start;
    uint64_t Size;
    uint8_t Type;
} MIME_CFB_ENTRY;

typedef struct MIME_CFB {
    const uint8_t *Data;
    size_t Size;
    uint32_t SectorSize, SectorCount, Version;
    uint32_t *Fat, *MiniFat;
    size_t MiniFatCount;
    uint8_t *Directory, *MiniStream;
    size_t DirectorySize, MiniStreamSize;
    MIME_CFB_ALLOC Allocate;
    MIME_CFB_FREE Free;
} MIME_CFB;

int MimeCfbOpen(MIME_CFB *file, const void *data, size_t size, MIME_CFB_ALLOC allocate, MIME_CFB_FREE release);
void MimeCfbClose(MIME_CFB *file);
int MimeCfbEntry(const MIME_CFB *file, uint32_t index, MIME_CFB_ENTRY *entry);
int MimeCfbRead(const MIME_CFB *file, const MIME_CFB_ENTRY *entry, void *out, size_t capacity);

/* a storage or stream to write, with its siblings in no particular order. a stream's data stays with the caller. */
typedef struct MIME_CFB_NODE {
    struct MIME_CFB_NODE *Next, *Children;
    uint16_t Name[32];
    uint8_t Type;
    uint8_t ClassId[16];
    const uint8_t *Data;
    size_t Size;
} MIME_CFB_NODE;

/* writes a compound file whose root holds the nodes. the output comes from the allocator and is the caller's. */
int MimeCfbWrite(const MIME_CFB_NODE *children, const uint8_t root_class[16], MIME_CFB_ALLOC allocate,
                 MIME_CFB_FREE release, uint8_t **out, size_t *size);

#endif
