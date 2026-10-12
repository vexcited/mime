#include "rom.h"

#include <strings.h>

const MimeRomEntry *mime_rom_find(const char *path)
{
    const MimeRomHeader *h = mime_rom_header();
    for (uint32_t i = 0; i < h->count; i++)
        if (strncasecmp(h->entries[i].path, path, sizeof(h->entries[i].path)) == 0)
            return &h->entries[i];
    return NULL;
}
