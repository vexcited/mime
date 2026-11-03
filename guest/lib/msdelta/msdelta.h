/*
 * the apply side of Microsoft's MSDelta patches, the PA30 files that servicing packages carry. a patch turns a source
 * buffer into a target buffer and holds the hash of the target. what is here was written from the behavior of
 * msdelta.dll and from the public description of the format, see docs/msdelta.md.
 */
#ifndef MIME_LIB_MSDELTA_H
#define MIME_LIB_MSDELTA_H

#include <stddef.h>
#include <stdint.h>

enum {
    MSDELTA_OK = 0,
    MSDELTA_BAD,         /* not a PA30 patch, or truncated or inconsistent. */
    MSDELTA_UNSUPPORTED, /* a file type or a flag this library does not know. */
    MSDELTA_MISMATCH,    /* the target does not have the hash the patch names, or the source is not the one. */
    MSDELTA_MEMORY,
};

typedef struct MSDELTA_INFO {
    uint32_t FileType; /* 1 raw, 2 i386, 4 ia64, 8 amd64, 16 cli4 i386, 32 cli4 amd64, 64 cli4 arm, 128 cli4 arm64. */
    uint64_t Flags;
    uint64_t TargetSize;
    uint64_t FileTime;
    uint32_t HashAlgId; /* 0x8003 is md5, 0x8004 sha-1, 0x800c sha-256. */
    uint32_t HashSize;
    uint8_t Hash[64];
} MSDELTA_INFO;

/* reads the header of a patch. */
int MsDeltaInfo(const uint8_t *patch, size_t patch_size, MSDELTA_INFO *info);

/* applies a patch. the target is allocated by the library and released with MsDeltaFree. */
int MsDeltaApply(const uint8_t *source, size_t source_size, const uint8_t *patch, size_t patch_size, uint8_t **target,
                 size_t *target_size);

void MsDeltaFree(void *memory);

#endif
