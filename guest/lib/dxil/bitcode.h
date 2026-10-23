/*
 * the llvm bitstream, read whole into a tree of blocks and records. dxil is
 * llvm 3.7 bitcode in this container format.
 */
#ifndef MIME_LIB_DXIL_BITCODE_H
#define MIME_LIB_DXIL_BITCODE_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

typedef struct BcRecord {
    uint32_t code;
    uint32_t count;
    uint64_t *ops;
    /* blob operand bytes, if the abbreviation had one. */
    const uint8_t *blob;
    uint32_t blob_size;
} BcRecord;

typedef struct BcBlock BcBlock;

/* a record or a nested block. */
typedef struct BcEntry {
    BcRecord *record;
    BcBlock *block;
} BcEntry;

struct BcBlock {
    uint32_t id;
    uint32_t count;
    BcEntry *entries;
};

/* memory of a parse, freed at once. */
typedef struct BcArena {
    const MimeAllocator *alloc;
    void *chunks;
    uint8_t *at, *end;
} BcArena;

typedef struct BcMark {
    void *chunks;
    uint8_t *at, *end;
} BcMark;

void *bc_arena_alloc(BcArena *a, size_t size);
void bc_arena_free(BcArena *a);
/* releases what was allocated since the mark. */
BcMark bc_arena_mark(const BcArena *a);
void bc_arena_release(BcArena *a, BcMark mark);

/* the top level blocks, as a block of id ~0. NULL when malformed. */
BcBlock *bc_parse(const uint8_t *data, size_t size, BcArena *arena);

#endif
