#include "pe_icon.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct PeResources {
    const uint8_t *image;
    size_t size;
    const uint8_t *sections;
    uint16_t section_count;
    uint32_t headers_size;
    const uint8_t *root;
    uint32_t root_size;
} PeResources;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)read16(p) | (uint32_t)read16(p + 2) << 16;
}

static void write16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void write32(uint8_t *p, uint32_t n)
{
    write16(p, (uint16_t)n);
    write16(p + 2, (uint16_t)(n >> 16));
}

static bool range(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static const uint8_t *rva_data(const PeResources *pe, uint32_t rva, uint32_t length)
{
    if (rva < pe->headers_size && range(pe->headers_size, rva, length) && range(pe->size, rva, length))
        return pe->image + rva;
    for (uint16_t i = 0; i < pe->section_count; i++) {
        const uint8_t *s = pe->sections + (size_t)i * 40;
        uint32_t start = read32(s + 12), raw_size = read32(s + 16), offset = read32(s + 20);
        if (rva < start || !range(raw_size, rva - start, length))
            continue;
        size_t file_offset = (size_t)offset + (rva - start);
        if (range(pe->size, file_offset, length))
            return pe->image + file_offset;
    }
    return nullptr;
}

static bool resources_open(PeResources *pe, const void *image, size_t size)
{
    memset(pe, 0, sizeof(*pe));
    pe->image = image;
    pe->size = size;
    if (size < 64 || read16(pe->image) != 0x5a4d)
        return false;
    size_t nt = read32(pe->image + 60);
    if (!range(size, nt, 24) || read32(pe->image + nt) != 0x00004550)
        return false;
    uint16_t optional_size = read16(pe->image + nt + 20);
    size_t optional = nt + 24;
    if (!range(size, optional, optional_size) || optional_size < 64)
        return false;
    uint16_t magic = read16(pe->image + optional);
    size_t directories = magic == 0x20b ? 112 : magic == 0x10b ? 96 : 0;
    if (!directories || optional_size < directories + 24 || read32(pe->image + optional + directories - 4) < 3)
        return false;
    pe->section_count = read16(pe->image + nt + 6);
    size_t sections = optional + optional_size;
    if (!range(size, sections, (size_t)pe->section_count * 40))
        return false;
    pe->sections = pe->image + sections;
    pe->headers_size = read32(pe->image + optional + 60);
    uint32_t resource_rva = read32(pe->image + optional + directories + 16);
    pe->root_size = read32(pe->image + optional + directories + 20);
    if (!resource_rva || pe->root_size < 16)
        return false;
    pe->root = rva_data(pe, resource_rva, pe->root_size);
    return pe->root != nullptr;
}

static const uint8_t *directory(const PeResources *pe, uint32_t offset, uint32_t *count)
{
    if (!range(pe->root_size, offset, 16))
        return nullptr;
    const uint8_t *dir = pe->root + offset;
    *count = (uint32_t)read16(dir + 12) + read16(dir + 14);
    if (!range(pe->root_size, (size_t)offset + 16, (size_t)*count * 8))
        return nullptr;
    return dir + 16;
}

static bool directory_child(const PeResources *pe, uint32_t offset, int32_t identifier, uint32_t *child)
{
    uint32_t count;
    const uint8_t *entries = directory(pe, offset, &count);
    if (!entries)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = entries + (size_t)i * 8;
        if (identifier >= 0 && read32(e) != (uint32_t)identifier)
            continue;
        uint32_t value = read32(e + 4);
        if (!(value & 0x80000000u))
            continue;
        *child = value & 0x7fffffffu;
        return true;
    }
    return false;
}

static const uint8_t *resource_data(const PeResources *pe, uint32_t type_dir, int32_t identifier,
                                    uint32_t *size)
{
    uint32_t language_dir, count;
    if (!directory_child(pe, type_dir, identifier, &language_dir))
        return nullptr;
    const uint8_t *entries = directory(pe, language_dir, &count);
    if (!entries)
        return nullptr;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = read32(entries + (size_t)i * 8 + 4);
        if (offset & 0x80000000u || !range(pe->root_size, offset, 16))
            continue;
        const uint8_t *entry = pe->root + offset;
        *size = read32(entry + 4);
        const uint8_t *data = rva_data(pe, read32(entry), *size);
        if (data)
            return data;
    }
    return nullptr;
}

bool mime_pe_icon_extract(const void *image, size_t size, int32_t group_id, void **icon, size_t *icon_size)
{
    if (!icon || !icon_size)
        return false;
    *icon = nullptr;
    *icon_size = 0;
    PeResources pe;
    uint32_t groups_dir, icons_dir;
    if (!image || !resources_open(&pe, image, size) || !directory_child(&pe, 0, 14, &groups_dir) ||
        !directory_child(&pe, 0, 3, &icons_dir))
        return false;
    uint32_t group_size;
    const uint8_t *group = resource_data(&pe, groups_dir, group_id, &group_size);
    if (!group || group_size < 6 || read16(group) != 0 || read16(group + 2) != 1)
        return false;
    uint16_t count = read16(group + 4);
    if (!count || count > 256 || !range(group_size, 6, (size_t)count * 14))
        return false;
    const uint8_t *images[256];
    uint32_t sizes[256];
    size_t total = 6 + (size_t)count * 16;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = group + 6 + (size_t)i * 14;
        images[i] = resource_data(&pe, icons_dir, read16(entry + 12), &sizes[i]);
        if (!images[i] || !sizes[i] || sizes[i] != read32(entry + 8) || sizes[i] > 16u * 1024 * 1024 ||
            total > 32u * 1024 * 1024 - sizes[i])
            return false;
        total += sizes[i];
    }
    uint8_t *out = malloc(total);
    if (!out)
        return false;
    memcpy(out, group, 6);
    size_t offset = 6 + (size_t)count * 16;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t *entry = out + 6 + (size_t)i * 16;
        memcpy(entry, group + 6 + (size_t)i * 14, 12);
        write32(entry + 12, (uint32_t)offset);
        memcpy(out + offset, images[i], sizes[i]);
        offset += sizes[i];
    }
    *icon = out;
    *icon_size = total;
    return true;
}

bool mime_pe_icon_read(const char *path, int32_t group_id, void **icon, size_t *icon_size)
{
    if (!icon || !icon_size)
        return false;
    *icon = nullptr;
    *icon_size = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st;
    bool result = false;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && (uint64_t)st.st_size <= SIZE_MAX) {
        void *image = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (image != MAP_FAILED) {
            result = mime_pe_icon_extract(image, (size_t)st.st_size, group_id, icon, icon_size);
            munmap(image, (size_t)st.st_size);
        }
    }
    close(fd);
    return result;
}
