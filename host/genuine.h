#ifndef MIME_HOST_GENUINE_H
#define MIME_HOST_GENUINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GenuineEntry {
    char path[128]; /* below \Windows, forward slashes, e.g. System32/wow64.dll. */
    char name[64];  /* the leaf, for the symbol server and the messages. */
    char sha256[65];
    char key[40]; /* time-date stamp and size-of-image, the symbol server key. empty for a file of the image. */
    bool image;   /* read out of the windows image the manifest names, by its sha-1. */
    uint8_t sha1[20];
    uint64_t size;
} GenuineEntry;

/* genuine.c. */
extern GenuineEntry g_genuine[];
extern uint32_t g_genuine_count;
extern char g_genuine_build[32];
extern char g_genuine_dir[];
extern bool g_genuine_terminal;
bool genuine_cached(uint32_t index);
bool genuine_install(uint32_t index, const uint8_t *bytes, size_t length, const char *from, char *detail,
                     size_t detail_size);
bool genuine_make_parents(const char *path);
void genuine_remove_tree(const char *path);

/* genuine_image.c: the missing files of the image at url, read by ranges, the chunks that hold them each once. */
bool genuine_from_image(const char *url, char *detail, size_t detail_size);

#endif
