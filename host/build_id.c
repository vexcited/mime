#include "mime_host.h"

#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

static size_t read_build_id(uint8_t *out, size_t size)
{
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!header)
        return 0;
    const uint8_t *at = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        const struct load_command *command = (const struct load_command *)at;
        if (command->cmd == LC_UUID) {
            const struct uuid_command *uuid = (const struct uuid_command *)command;
            size_t n = MIME_MIN(size, sizeof(uuid->uuid));
            memcpy(out, uuid->uuid, n);
            return n;
        }
        at += command->cmdsize;
    }
    return 0;
}
#else
#include <elf.h>
#include <link.h>

typedef struct BuildIdSearch {
    uint8_t *out;
    size_t size;
    size_t found;
} BuildIdSearch;

static int search_notes(struct dl_phdr_info *info, size_t info_size, void *data)
{
    BuildIdSearch *search = data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (phdr->p_type != PT_NOTE)
            continue;
        const uint8_t *at = (const uint8_t *)(info->dlpi_addr + phdr->p_vaddr);
        const uint8_t *end = at + phdr->p_memsz;
        while (at + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *note = (const ElfW(Nhdr) *)at;
            const uint8_t *name = at + sizeof(*note);
            const uint8_t *desc = name + MIME_ALIGN_UP(note->n_namesz, 4);
            if (desc + note->n_descsz > end)
                break;
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && !memcmp(name, "GNU", 4)) {
                search->found = MIME_MIN(search->size, (size_t)note->n_descsz);
                memcpy(search->out, desc, search->found);
                return 1;
            }
            at = desc + MIME_ALIGN_UP(note->n_descsz, 4);
        }
    }
    /* the program comes first, the libraries after it are not mime. */
    return 1;
}

static size_t read_build_id(uint8_t *out, size_t size)
{
    BuildIdSearch search = {.out = out, .size = size};
    dl_iterate_phdr(search_notes, &search);
    return search.found;
}
#endif

static uint8_t g_id[MIME_BUILD_ID_MAX];
static size_t g_length;

static void init_build_id(void)
{
    g_length = read_build_id(g_id, sizeof(g_id));
}

/* the build id the linker gave the running binary, which stays its own after an update replaces the file. */
size_t mime_build_id(uint8_t out[MIME_BUILD_ID_MAX])
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_build_id);
    memcpy(out, g_id, g_length);
    return g_length;
}

void mime_build_id_text(const uint8_t *id, size_t length, char *out, size_t size)
{
    if (!length) {
        mime_strlcpy(out, "unknown", size);
        return;
    }
    out[0] = 0;
    for (size_t i = 0; i < length && 2 * i + 2 < size; i++)
        snprintf(out + 2 * i, 3, "%02x", id[i]);
}
