/* bounded read-only compound storage for installer databases. */
#include "compound.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define CFB_END 0xFFFFFFFEu

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t dword(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const uint8_t *sector(const MIME_CFB *f, uint32_t index)
{
    return index < f->SectorCount ? f->Data + ((size_t)index + 1) * f->SectorSize : nullptr;
}

static int chain_count(const MIME_CFB *f, uint32_t first, size_t *count)
{
    *count = 0;
    while (first != CFB_END) {
        if (first >= f->SectorCount || ++*count > f->SectorCount)
            return 0;
        first = f->Fat[first];
    }
    return 1;
}

static int read_chain(const MIME_CFB *f, uint32_t first, uint8_t *out, size_t size, int mini)
{
    size_t block = mini ? 64 : f->SectorSize;
    size_t limit = mini ? f->MiniFatCount : f->SectorCount;
    const uint32_t *fat = mini ? f->MiniFat : f->Fat;
    size_t steps = 0;
    while (size) {
        if (first >= limit || ++steps > limit)
            return 0;
        const uint8_t *src;
        if (mini) {
            if ((uint64_t)first * 64 > SIZE_MAX)
                return 0;
            size_t offset = (size_t)first * 64;
            if (offset > f->MiniStreamSize || f->MiniStreamSize - offset < 64)
                return 0;
            src = f->MiniStream + offset;
        } else {
            src = sector(f, first);
            if (!src)
                return 0;
        }
        size_t bytes = size < block ? size : block;
        memcpy(out, src, bytes);
        out += bytes;
        size -= bytes;
        first = fat[first];
    }
    return first == CFB_END;
}

int MimeCfbEntry(const MIME_CFB *f, uint32_t index, MIME_CFB_ENTRY *entry)
{
    if (!f || !entry || index >= f->DirectorySize / 128)
        return 0;
    const uint8_t *p = f->Directory + (size_t)index * 128;
    memset(entry, 0, sizeof(*entry));
    entry->Type = p[66];
    if (!entry->Type)
        return 1;
    uint16_t length = word(p + 64);
    if (length < 2 || length > 64 || (length & 1) || word(p + length - 2))
        return 0;
    for (unsigned i = 0; i < length / 2; i++)
        entry->Name[i] = word(p + i * 2);
    entry->Left = dword(p + 68);
    entry->Right = dword(p + 72);
    entry->Child = dword(p + 76);
    entry->Start = dword(p + 116);
    entry->Size = dword(p + 120);
    if (f->Version == 4)
        entry->Size |= (uint64_t)dword(p + 124) << 32;
    return entry->Type == 1 || entry->Type == 2 || entry->Type == 5;
}

int MimeCfbRead(const MIME_CFB *f, const MIME_CFB_ENTRY *entry, void *out, size_t capacity)
{
    if (!f || !entry || entry->Type != 2 || entry->Size > capacity || (!out && entry->Size))
        return 0;
    if (!entry->Size)
        return 1;
    return read_chain(f, entry->Start, out, (size_t)entry->Size, entry->Size < 4096);
}

void MimeCfbClose(MIME_CFB *f)
{
    if (f->Free) {
        f->Free(f->Fat);
        f->Free(f->MiniFat);
        f->Free(f->Directory);
        f->Free(f->MiniStream);
    }
    memset(f, 0, sizeof(*f));
}

static int open_file(MIME_CFB *f)
{
    static const uint8_t signature[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    const uint8_t *h = f->Data;
    if (f->Size < 512 || memcmp(h, signature, sizeof(signature)) || word(h + 28) != 0xFFFE ||
        word(h + 32) != 6 || dword(h + 56) != 4096)
        return 0;
    f->Version = word(h + 26);
    if ((f->Version != 3 || word(h + 30) != 9) && (f->Version != 4 || word(h + 30) != 12))
        return 0;
    f->SectorSize = 1u << word(h + 30);
    if (f->Size % f->SectorSize || f->Size / f->SectorSize < 3 ||
        f->Size / f->SectorSize - 1 > UINT32_MAX)
        return 0;
    f->SectorCount = (uint32_t)(f->Size / f->SectorSize - 1);
    uint32_t fat_count = dword(h + 44), difat_count = dword(h + 72), difat = dword(h + 68);
    uint32_t per_sector = f->SectorSize / 4;
    if (!fat_count || fat_count > f->SectorCount || difat_count > f->SectorCount ||
        (uint64_t)fat_count * per_sector < f->SectorCount)
        return 0;
    f->Fat = f->Allocate((size_t)f->SectorCount * sizeof(uint32_t));
    if (!f->Fat)
        return 0;
    const uint8_t *table = h + 76;
    uint32_t slots = 109, at = 0, difat_seen = 0, entries = 0;
    for (uint32_t i = 0; i < fat_count; i++) {
        if (at == slots) {
            if (++difat_seen > difat_count || !(table = sector(f, difat)))
                return 0;
            slots = per_sector - 1;
            difat = dword(table + slots * 4);
            at = 0;
        }
        const uint8_t *fat = sector(f, dword(table + at++ * 4));
        if (!fat)
            return 0;
        for (uint32_t j = 0; j < per_sector && entries < f->SectorCount; j++)
            f->Fat[entries++] = dword(fat + j * 4);
    }
    size_t count;
    if (!chain_count(f, dword(h + 48), &count) || !count || count > SIZE_MAX / f->SectorSize)
        return 0;
    f->DirectorySize = count * f->SectorSize;
    f->Directory = f->Allocate(f->DirectorySize);
    if (!f->Directory || !read_chain(f, dword(h + 48), f->Directory, f->DirectorySize, 0))
        return 0;
    MIME_CFB_ENTRY root;
    if (!MimeCfbEntry(f, 0, &root) || root.Type != 5 || root.Size > f->Size)
        return 0;
    if (root.Size) {
        f->MiniStreamSize = (size_t)root.Size;
        f->MiniStream = f->Allocate(f->MiniStreamSize);
        if (!f->MiniStream || !read_chain(f, root.Start, f->MiniStream, f->MiniStreamSize, 0))
            return 0;
    }
    count = dword(h + 64);
    if (count > f->SectorCount || count > SIZE_MAX / f->SectorSize)
        return 0;
    if (count) {
        size_t bytes = count * f->SectorSize;
        f->MiniFat = f->Allocate(bytes);
        if (!f->MiniFat || !read_chain(f, dword(h + 60), (uint8_t *)f->MiniFat, bytes, 0))
            return 0;
        f->MiniFatCount = bytes / 4;
        for (size_t i = 0; i < f->MiniFatCount; i++)
            f->MiniFat[i] = dword((const uint8_t *)f->MiniFat + i * 4);
    }
    return 1;
}

int MimeCfbOpen(MIME_CFB *f, const void *data, size_t size, MIME_CFB_ALLOC allocate, MIME_CFB_FREE release)
{
    if (!f)
        return 0;
    memset(f, 0, sizeof(*f));
    if (!data || !allocate || !release)
        return 0;
    f->Data = data;
    f->Size = size;
    f->Allocate = allocate;
    f->Free = release;
    if (open_file(f))
        return 1;
    MimeCfbClose(f);
    return 0;
}
