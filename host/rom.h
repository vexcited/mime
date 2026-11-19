/*
 * the system files embedded in mime by the build, see MimeRomHeader.
 */
#ifndef MIME_ROM_H
#define MIME_ROM_H

#include "../include/mime/abi.h"

#include <stdbool.h>
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

/*
 * an icon group of an entry that is a file of the genuine manifest, by id or -1 for the first, from the host's copy or
 * from its .mun of SystemResources, see host/genuine.c. false for any other entry.
 */
bool mime_genuine_icon(const MimeRomEntry *rom, int32_t group, void **icon, size_t *icon_size);

#endif
