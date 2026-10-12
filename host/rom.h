/*
 * the system files embedded in mime by the build, see MimeRomHeader.
 */
#ifndef MIME_ROM_H
#define MIME_ROM_H

#include "../include/mime/abi.h"

#include <stddef.h>

extern const uint8_t mime_rom[];
extern const uint8_t mime_rom_end[];

static inline const MimeRomHeader *mime_rom_header(void)
{
    return (const MimeRomHeader *)mime_rom;
}

static inline size_t mime_rom_size(void)
{
    return (size_t)(mime_rom_end - mime_rom);
}

/* path is relative to C:, with backslashes; matching ignores case. */
const MimeRomEntry *mime_rom_find(const char *path);

#endif
