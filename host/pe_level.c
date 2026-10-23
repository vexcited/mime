/*
 * reads the manifest execution level so `mime run` can request administrator rights as the shell does. the
 * guest decides how to launch with its token.
 */
#include "mime_host.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MANIFEST_LIMIT (256 * 1024)

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool read_at(int fd, uint64_t offset, void *buffer, size_t size)
{
    return pread(fd, buffer, size, (off_t)offset) == (ssize_t)size;
}

typedef struct Layout {
    uint8_t sections[96 * 40];
    uint32_t count;
    uint32_t resources;
} Layout;

static bool to_offset(const Layout *l, uint32_t rva, uint64_t *offset)
{
    for (uint32_t i = 0; i < l->count; i++) {
        const uint8_t *s = l->sections + i * 40;
        uint32_t start = get32(s + 12), size = get32(s + 8) > get32(s + 16) ? get32(s + 8) : get32(s + 16);
        if (rva >= start && rva - start < size) {
            *offset = (uint64_t)get32(s + 20) + (rva - start);
            return true;
        }
    }
    return false;
}

/* finds a resource entry by id, or the first entry when id is 0. */
static bool entry_of(int fd, const Layout *l, uint32_t directory, uint32_t id, uint32_t *next, bool *branch)
{
    uint64_t at;
    uint8_t head[16];
    if (!to_offset(l, l->resources + directory, &at) || !read_at(fd, at, head, sizeof(head)))
        return false;
    uint32_t named = get16(head + 12), ids = get16(head + 14), count = named + ids;
    if (!count || count > 256)
        return false;
    uint8_t entries[256 * 8];
    if (!read_at(fd, at + 16, entries, count * 8))
        return false;
    for (uint32_t i = named; i < count; i++) {
        const uint8_t *e = entries + i * 8;
        if (id && get32(e) != id)
            continue;
        *branch = (get32(e + 4) & 0x80000000u) != 0;
        *next = get32(e + 4) & 0x7FFFFFFFu;
        return true;
    }
    return false;
}

/* loads the process manifest, resource 1 of type 24; null if absent. */
static uint8_t *manifest_of(int fd, uint32_t *size)
{
    uint8_t dos[64], headers[24 + 240];
    if (!read_at(fd, 0, dos, sizeof(dos)) || get16(dos) != 0x5A4D)
        return nullptr;
    uint32_t nt = get32(dos + 0x3C);
    if (nt > 0x10000 || !read_at(fd, nt, headers, sizeof(headers)) || get32(headers) != 0x4550)
        return nullptr;
    Layout *layout = calloc(1, sizeof(*layout));
    uint8_t *manifest = nullptr;
    uint32_t count = get16(headers + 6), optional = get16(headers + 20), directories = get16(headers + 24) == 0x20B ? 112 : 96;
    if (layout && count && count <= 96 && optional >= directories + 24 && optional <= 240 &&
        read_at(fd, (uint64_t)nt + 24 + optional, layout->sections, count * 40)) {
        layout->count = count;
        layout->resources = get32(headers + 24 + directories + 16);
        uint32_t tree, data;
        bool branch;
        if (layout->resources && entry_of(fd, layout, 0, 24, &tree, &branch) && branch &&
            entry_of(fd, layout, tree, 1, &tree, &branch) && branch && entry_of(fd, layout, tree, 0, &data, &branch) &&
            !branch) {
            uint64_t at;
            uint8_t item[16];
            if (to_offset(layout, layout->resources + data, &at) && read_at(fd, at, item, sizeof(item))) {
                uint32_t length = get32(item + 4);
                if (length && length <= MANIFEST_LIMIT && to_offset(layout, get32(item), &at)) {
                    manifest = malloc(length + 1);
                    if (manifest && read_at(fd, at, manifest, length)) {
                        manifest[length] = 0;
                        *size = length;
                    } else {
                        free(manifest);
                        manifest = nullptr;
                    }
                }
            }
        }
    }
    free(layout);
    return manifest;
}

/* extracts ASCII bytes from a manifest, dropping high bytes for UTF-16. */
static size_t narrow(uint8_t *text, size_t size)
{
    bool wide = size >= 4 && ((text[0] == 0xFF && text[1] == 0xFE) || (text[1] == 0 && text[3] == 0));
    if (!wide)
        return size;
    size_t out = 0;
    for (size_t i = text[0] == 0xFF ? 2 : 0; i + 1 < size; i += 2)
        text[out++] = text[i + 1] ? '?' : text[i];
    return out;
}

static bool is_blank(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

uint32_t mime_pe_run_level(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    uint32_t size = 0;
    uint8_t *text = manifest_of(fd, &size);
    close(fd);
    if (!text)
        return 0;
    size = (uint32_t)narrow(text, size);
    uint32_t level = 0;
    static const char tag[] = "requestedExecutionLevel";
    for (uint32_t i = 0; i + sizeof(tag) < size; i++) {
        if (memcmp(text + i, tag, sizeof(tag) - 1))
            continue;
        /* the attributes of the element, up to its end: level="...". */
        uint32_t at = i + (uint32_t)sizeof(tag) - 1;
        while (at < size && text[at] != '>') {
            while (at < size && (is_blank(text[at]) || text[at] == '/'))
                at++;
            uint32_t name = at;
            while (at < size && text[at] != '=' && text[at] != '>' && !is_blank(text[at]))
                at++;
            uint32_t name_end = at;
            while (at < size && is_blank(text[at]))
                at++;
            if (at >= size || text[at] != '=')
                continue;
            at++;
            while (at < size && is_blank(text[at]))
                at++;
            uint8_t quote = at < size ? text[at] : 0;
            if (quote != '"' && quote != '\'')
                continue;
            uint32_t value = ++at;
            while (at < size && text[at] != quote)
                at++;
            if (at < size && name_end - name == 5 && !memcmp(text + name, "level", 5)) {
                uint32_t n = at - value;
                if (n == 9 && !memcmp(text + value, "asInvoker", 9))
                    level = 1;
                else if (n == 16 && !memcmp(text + value, "highestAvailable", 16))
                    level = 2;
                else if (n == 20 && !memcmp(text + value, "requireAdministrator", 20))
                    level = 3;
            }
            at++;
        }
        break;
    }
    free(text);
    return level;
}
