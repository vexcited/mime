/* reading an executable and making the source look like the target: the transforms that follow how addresses move. */
#include "internal.h"

enum { SCN_EXECUTE = 0x20000000 };

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

/* a section header: name 0, virtual size 8, address 12, raw size 16, raw pointer 20, characteristics 36. */
static const uint8_t *section(const MD_PE *pe, unsigned index)
{
    return pe->Image + pe->SectionTable + 40u * index;
}

uint32_t MdPeOffset(const MD_PE *pe, uint32_t rva, uint32_t length)
{
    if (pe->HeadersSize <= rva) {
        unsigned i;
        for (i = 0; i < pe->SectionCount; i++) {
            const uint8_t *s = section(pe, i);
            uint32_t va = rd32(s + 12);
            if (va <= rva && va + rd32(s + 16) > rva)
                break;
        }
        if (i >= pe->SectionCount)
            return 0;
        const uint8_t *s = section(pe, i);
        uint32_t delta = rva - rd32(s + 12);
        if (rd32(s + 16) >= delta + length)
            return delta + rd32(s + 20);
        return 0;
    }
    if (rva + length >= rva && pe->HeadersSize >= rva + length)
        return rva;
    return 0;
}

uint32_t MdPeMaxSize(const MD_PE *pe, uint32_t rva)
{
    if (pe->HeadersSize > rva)
        return pe->HeadersSize - rva;
    for (unsigned i = 0; i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        uint32_t va = rd32(s + 12);
        if (va <= rva && va + rd32(s + 16) > rva)
            return va + rd32(s + 16) - rva;
    }
    return 0;
}

bool MdPeOffsetToRva(const MD_PE *pe, MD_RIFT *rift)
{
    memset(rift, 0, sizeof(*rift));
    bool ok = MdRiftAdd(rift, 0, 0);
    for (unsigned i = 0; ok && i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        if (rd32(s + 20) && rd32(s + 12))
            ok = MdRiftAdd(rift, rd32(s + 20), rd32(s + 12));
    }
    if (!ok) {
        MdRiftFree(rift);
        return false;
    }
    MdRiftSort(rift);
    return true;
}

uint64_t MdPeImageBase(const MD_PE *pe)
{
    return pe->Is32 ? rd32(pe->Image + pe->Optional + 28) : rd64(pe->Image + pe->Optional + 24);
}

bool MdPeLoad(MD_PE *pe, uint8_t *image, uint32_t size)
{
    memset(pe, 0, sizeof(*pe));
    pe->Image = image;
    pe->Size = size;
    if (size < 0x200 || rd16(image) != 0x5A4D)
        return false;
    uint32_t nt = rd32(image + 0x3C);
    if ((int32_t)nt < 0 || nt > size)
        return false;
    pe->Nt = nt;
    pe->Optional = nt + 24;
    pe->Is32 = nt + 248 >= nt && size >= (uint64_t)nt + 248 && rd16(image + nt + 24) == 0x10B && rd16(image + nt + 20) == 224;
    if (!pe->Is32) {
        if (nt + 264 < nt || size < (uint64_t)nt + 264 || rd16(image + nt + 24) != 0x20B || rd16(image + nt + 20) != 240)
            return false;
    }
    if (rd32(image + nt) != 0x4550)
        return false;
    pe->Machine = rd16(image + nt + 4);
    if (pe->Machine == 0x14C ? !pe->Is32 : ((pe->Machine == 0x200 || pe->Machine == 0x8664) && pe->Is32))
        return false;
    pe->SectionCount = rd16(image + nt + 6);
    pe->SectionTable = nt + 24 + rd16(image + nt + 20);
    if (pe->SectionTable < nt + 24 || (uint64_t)pe->SectionTable + 40u * pe->SectionCount > size)
        return false;
    for (unsigned i = 0; i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        uint32_t pointer = rd32(s + 20), raw = rd32(s + 16);
        if (pointer > size || pointer + raw < pointer || pointer + raw > size)
            return false;
    }
    uint32_t headers = rd32(image + pe->Optional + 60);
    pe->HeadersSize = headers < size ? headers : size;
    pe->DirectoryCount = rd32(image + pe->Optional + (pe->Is32 ? 92 : 108));
    uint32_t directories = pe->Optional + (pe->Is32 ? 96 : 112);
    for (unsigned d = 0; d < 16; d++) {
        if (d >= pe->DirectoryCount || directories + 8u * d + 8 > size)
            continue;
        uint32_t rva = rd32(image + directories + 8u * d), length = rd32(image + directories + 8u * d + 4);
        pe->DirRva[d] = rva;
        uint32_t offset = MdPeOffset(pe, rva, 0);
        if (!offset) {
            if (size <= rva)
                continue;
            offset = rva;
        }
        if (offset) {
            pe->DirOffset[d] = offset;
            uint32_t room = size - offset;
            pe->DirSize[d] = length < room ? length : room;
        }
    }
    /* the exports. */
    if (pe->DirOffset[0] && pe->DirSize[0] >= 0x28) {
        pe->ExportOffset = pe->DirOffset[0];
        const uint8_t *e = image + pe->ExportOffset;
        uint32_t functions = rd32(e + 20), names = rd32(e + 24);
        if (functions <= 0x3FFFFFFF) {
            uint32_t offset = MdPeOffset(pe, rd32(e + 28), 4 * functions);
            if (offset) {
                pe->FunctionsOffset = offset;
                pe->FunctionCount = functions;
            }
        }
        if (names <= 0x3FFFFFFF) {
            uint32_t n = MdPeOffset(pe, rd32(e + 32), 4 * names), o = MdPeOffset(pe, rd32(e + 36), 2 * names);
            if (n && o) {
                pe->NamesOffset = n;
                pe->OrdinalsOffset = o;
                pe->NameCount = names;
            }
        }
    }
    /* the imports end at the first descriptor with no thunks. */
    if (pe->DirOffset[1]) {
        pe->ImportOffset = pe->DirOffset[1];
        pe->ImportCount = pe->DirSize[1] / 20;
        for (uint32_t i = 0; i < pe->ImportCount; i++) {
            if (!rd32(image + pe->ImportOffset + 20 * i)) {
                pe->ImportCount = i;
                break;
            }
        }
    }
    /* the relocations take what their section holds after them, when that is more than the directory says. */
    if (pe->DirOffset[5]) {
        pe->RelocOffset = pe->DirOffset[5];
        uint32_t room = 0;
        for (unsigned i = 0; i < pe->SectionCount; i++) {
            const uint8_t *s = section(pe, i);
            if (!memcmp(s, ".reloc\0\0", 8)) {
                uint32_t pointer = rd32(s + 20), raw = rd32(s + 16);
                if (pointer <= pe->RelocOffset && pointer + raw > pe->RelocOffset)
                    room = pointer + raw - pe->RelocOffset;
                break;
            }
        }
        pe->RelocSize = room > pe->DirSize[5] ? room : pe->DirSize[5];
    }
    if (pe->DirOffset[10] && pe->DirSize[10] >= (pe->Is32 ? 64u : 96u))
        pe->LoadConfigOffset = pe->DirOffset[10];
    return true;
}

/* ------------------------------------------------------------------ the context of the transforms */

typedef struct MD_XFORM {
    MD_PE *Pe;
    uint8_t *Hint;
    const MD_RIFT *Used;
    const MD_PE_TARGET *Target;
    MD_CLI Cli; /* the metadata of the source. */
} MD_XFORM;

static uint32_t map_rva(const MD_XFORM *x, uint32_t rva)
{
    return (uint32_t)MdRiftMap(x->Used, (int64_t)(uint64_t)rva);
}

static bool inside(const MD_PE *pe, uint64_t offset, uint64_t length)
{
    return offset + length <= pe->Size;
}

static void fill_hint(const MD_XFORM *x, uint64_t offset, uint64_t length, uint8_t value)
{
    if (inside(x->Pe, offset, length))
        memset(x->Hint + offset, value, (size_t)length);
}

static void map_in_place(const MD_XFORM *x, uint32_t offset)
{
    if (inside(x->Pe, offset, 4))
        wr32(x->Pe->Image + offset, map_rva(x, rd32(x->Pe->Image + offset)));
}

/* ------------------------------------------------------------------ the marks of what is not code */

static void mark_non_executable(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    for (unsigned i = 0; i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        if (!(rd32(s + 36) & SCN_EXECUTE)) {
            uint32_t pointer = rd32(s + 20);
            if (pointer < pe->Size) {
                uint32_t length = pe->Size - pointer;
                if (length > rd32(s + 16))
                    length = rd32(s + 16);
                memset(x->Hint + pointer, 1, length);
            }
        }
    }
    memset(x->Hint, 3, pe->HeadersSize);
    for (unsigned d = 0; d < 16 && d < pe->DirectoryCount; d++)
        if (pe->DirOffset[d])
            fill_hint(x, pe->DirOffset[d], pe->DirSize[d], 3);
    MdPeMarkCliMethods(pe, &x->Cli, x->Hint);
}

/* ------------------------------------------------------------------ imports and exports */

/* the name of an import: the hint, the characters and the terminator. returns the length of the characters, or -1. */
static int64_t import_name(const MD_PE *pe, uint32_t rva, uint32_t *offset)
{
    uint32_t at;
    if (pe->HeadersSize > rva) {
        if (rva + 4 < rva || pe->HeadersSize < rva + 4)
            return -1;
        at = rva;
    } else {
        unsigned i;
        for (i = 0; i < pe->SectionCount; i++) {
            const uint8_t *s = section(pe, i);
            uint32_t va = rd32(s + 12);
            if (va <= rva && va + rd32(s + 16) > rva)
                break;
        }
        if (i >= pe->SectionCount)
            return -1;
        const uint8_t *s = section(pe, i);
        uint32_t delta = rva - rd32(s + 12);
        if (rd32(s + 16) < delta + 4)
            return -1;
        at = delta + rd32(s + 20);
    }
    if (!at)
        return -1;
    uint32_t end = pe->HeadersSize > rva ? pe->HeadersSize : 0;
    if (!end) {
        for (unsigned i = 0; i < pe->SectionCount; i++) {
            const uint8_t *s = section(pe, i);
            uint32_t va = rd32(s + 12);
            if (va <= rva && va + rd32(s + 16) > rva) {
                end = va + rd32(s + 16);
                break;
            }
        }
        if (!end)
            return -1;
    }
    uint32_t room = end - rva, n = 2;
    if (room <= 2)
        return -1;
    do {
        if (!pe->Image[at + n])
            break;
        n++;
    } while (room > n);
    if (room <= n)
        return -1;
    *offset = at;
    return n - 2;
}

static void import_thunk_name(const MD_XFORM *x, uint32_t rva_of_name, uint32_t location)
{
    uint32_t at;
    int64_t length = import_name(x->Pe, rva_of_name, &at);
    if (length >= 0) {
        fill_hint(x, at, (uint64_t)length + 3, 3);
        map_in_place(x, location);
    }
}

static void imports(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    uint8_t *image = pe->Image;
    unsigned width = pe->Is32 ? 4 : 8;
    uint64_t base = MdPeImageBase(pe);
    for (uint32_t d = 0; d < pe->ImportCount; d++) {
        uint32_t descriptor = pe->ImportOffset + 20 * d;
        uint32_t original = rd32(image + descriptor), stamp = rd32(image + descriptor + 4);
        uint32_t first = rd32(image + descriptor + 16);
        uint32_t room = MdPeMaxSize(pe, original) / width;
        uint32_t offset = MdPeOffset(pe, original, 0);
        if (offset) {
            uint32_t k = 0;
            /* the test is of the first entry, as in the other tools of the same format. */
            bool by_name = pe->Is32 ? (int32_t)rd32(image + offset) >= 0 : (int64_t)rd64(image + offset) >= 0;
            for (; k < room; k++) {
                uint32_t at = offset + k * width;
                if (!inside(pe, at, width) || (pe->Is32 ? !rd32(image + at) : !rd64(image + at)))
                    break;
                if (by_name)
                    import_thunk_name(x, rd32(image + at), at);
            }
            fill_hint(x, offset, (uint64_t)k * width, 3);
        }
        room = MdPeMaxSize(pe, first) / width;
        offset = MdPeOffset(pe, first, 0);
        if (offset) {
            uint32_t k = 0;
            for (; k < room; k++) {
                uint32_t at = offset + k * width;
                if (!inside(pe, at, width))
                    break;
                uint64_t entry = pe->Is32 ? rd32(image + at) : rd64(image + at);
                if (!entry)
                    break;
                if (stamp) {
                    if (pe->Is32) {
                        wr32(image + at, (uint32_t)((uint64_t)MdRiftMap(x->Used, (int64_t)(uint32_t)((uint32_t)entry - (uint32_t)base)) +
                                                    (uint32_t)base));
                    } else {
                        wr64(image + at, (uint64_t)MdRiftMap(x->Used, (int64_t)(entry - base)) + base);
                    }
                } else if (pe->Is32 ? (int32_t)entry >= 0 : (int64_t)entry >= 0) {
                    import_thunk_name(x, (uint32_t)entry, at);
                }
            }
            fill_hint(x, offset, (uint64_t)k * width, 3);
        }
        map_in_place(x, descriptor + 12);
        map_in_place(x, descriptor);
        map_in_place(x, descriptor + 16);
    }
}

static void exports(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (!pe->ExportOffset)
        return;
    if (pe->FunctionsOffset) {
        fill_hint(x, pe->FunctionsOffset, 4ull * pe->FunctionCount, 3);
        for (uint32_t k = 0; k < pe->FunctionCount; k++)
            map_in_place(x, pe->FunctionsOffset + 4 * k);
    }
    if (pe->NamesOffset) {
        fill_hint(x, pe->NamesOffset, 4ull * pe->NameCount, 3);
        for (uint32_t k = 0; k < pe->NameCount; k++)
            map_in_place(x, pe->NamesOffset + 4 * k);
    }
    if (pe->OrdinalsOffset)
        fill_hint(x, pe->OrdinalsOffset, 2ull * pe->NameCount, 3);
    map_in_place(x, pe->ExportOffset + 12);
    map_in_place(x, pe->ExportOffset + 28);
    map_in_place(x, pe->ExportOffset + 32);
    map_in_place(x, pe->ExportOffset + 36);
}

/* ------------------------------------------------------------------ resources */

static void resource_directory(const MD_XFORM *x, uint32_t dir, uint32_t base, uint32_t end, uint32_t rva)
{
    if (!dir)
        return;
    uint8_t *image = x->Pe->Image;
    uint32_t mapped_base = map_rva(x, rva);
    uint32_t count = (uint32_t)rd16(image + dir + 12) + rd16(image + dir + 14);
    uint32_t entry = dir + 16;
    if (end > entry && ((end - entry) >> 3) < count)
        count = (end - entry) >> 3;
    else if (end <= entry)
        count = 0;
    for (; count; count--, entry += 8) {
        uint32_t offset = rd32(image + entry + 4);
        if ((int32_t)offset < 0) {
            uint32_t sub = base + (offset & 0x7FFFFFFF);
            bool ok = entry >= base && entry + 8 <= end && sub > entry;
            if (ok && (sub < base || sub + 16 > end))
                sub = 0;
            resource_directory(x, ok ? sub : 0, base, end, rva);
            uint32_t value = map_rva(x, rva + (offset & 0x7FFFFFFF)) - mapped_base;
            wr32(image + entry + 4, offset ^ ((offset ^ value) & 0x7FFFFFFF));
        } else if (entry >= base && entry + 8 <= end) {
            uint32_t data = base + offset;
            if (data >= base && data + 16 <= end)
                wr32(image + data, map_rva(x, rva + rd32(image + data)) - mapped_base);
        }
        uint32_t name = rd32(image + entry);
        if ((int32_t)name < 0) {
            uint32_t value = map_rva(x, (name & 0x7FFFFFFF) + rva) - mapped_base;
            wr32(image + entry, name ^ ((value ^ name) & 0x7FFFFFFF));
        }
    }
}

static void resources(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (!pe->DirOffset[2])
        return;
    uint32_t dir = pe->DirOffset[2], end = dir + pe->DirSize[2];
    if (dir + 16 > end)
        return;
    resource_directory(x, dir, dir, end, pe->DirRva[2]);
}

/* ------------------------------------------------------------------ relocations */

typedef struct MD_RELOC {
    uint32_t Location;
    uint8_t Type;
    uint16_t Extra;
} MD_RELOC;

static bool relocations(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (!pe->RelocOffset)
        return true;
    uint8_t *image = pe->Image;
    uint64_t base = MdPeImageBase(pe);
    MD_RELOC *list = nullptr;
    size_t count = 0, capacity = 0;
    uint32_t at = pe->RelocOffset, remaining = pe->DirSize[5];
    while (remaining >= 8) {
        uint32_t block = rd32(image + at + 4), page = rd32(image + at);
        if (block < 8 || block > remaining)
            break;
        uint32_t file = MdPeOffset(pe, page, 0);
        if (file) {
            uint32_t entries = (block - 8) >> 1;
            const uint8_t *word = image + at + 8;
            while (entries) {
                uint16_t w = rd16(word);
                unsigned type = w >> 12;
                uint32_t offset = w & 0xFFF;
                uint32_t place = offset + file;
                if (!type)
                    break;
                MD_RELOC r = {map_rva(x, offset + page), (uint8_t)type, 0};
                switch (type) {
                case 3:
                    if (inside(pe, place, 4)) {
                        uint8_t *hint = x->Hint + place;
                        wr32(hint, rd32(hint) | 0x01010101u);
                        uint32_t value = rd32(image + place) - (uint32_t)base;
                        wr32(image + place, (uint32_t)MdRiftMap(x->Used, (int64_t)value) + (uint32_t)x->Target->ImageBase);
                    }
                    break;
                case 1:
                case 2:
                    if (inside(pe, place, 2))
                        wr16(x->Hint + place, rd16(x->Hint + place) | 0x0101);
                    break;
                case 4:
                    if (inside(pe, place, 2))
                        wr16(x->Hint + place, rd16(x->Hint + place) | 0x0101);
                    if (entries > 1) {
                        word += 2;
                        entries--;
                        r.Extra = rd16(word);
                    } else
                        r.Type = 0;
                    break;
                case 10:
                    if (inside(pe, place, 8)) {
                        uint8_t *hint = x->Hint + place;
                        wr64(hint, rd64(hint) | 0x0101010101010101ull);
                        uint64_t value = rd64(image + place) - base;
                        wr64(image + place, x->Target->ImageBase + (uint64_t)MdRiftMap(x->Used, (int64_t)value));
                    }
                    break;
                default:
                    r.Type = 0;
                    break;
                }
                if (count == capacity) {
                    capacity = capacity ? capacity * 2 : 1024;
                    MD_RELOC *grown = MdRealloc(list, capacity * sizeof(*list));
                    if (!grown) {
                        MsDeltaFree(list);
                        return false;
                    }
                    list = grown;
                }
                list[count++] = r;
                word += 2;
                entries--;
            }
        }
        at += block;
        remaining -= block;
    }
    if (count) {
        /* a stable sort on the new location, and the directory is written again in that order. */
        for (size_t i = 1; i < count; i++) {
            MD_RELOC r = list[i];
            size_t j = i;
            while (j > 0 && list[j - 1].Location > r.Location) {
                list[j] = list[j - 1];
                j--;
            }
            list[j] = r;
        }
        uint32_t space = pe->RelocSize & ~1u;
        uint8_t *block = image + pe->RelocOffset;
        size_t k = 0;
        while (space >= 8 && k < count) {
            uint8_t *word = block + 8;
            wr32(block, list[k].Location & 0xFFFFF000u);
            space -= 8;
            while (space) {
                if (k >= count || (list[k].Location & 0xFFFFF000u) != rd32(block)) {
                    if ((((word - (block + 8)) >> 1) & 1) == 1) {
                        wr16(word, 0);
                        word += 2;
                        space -= 2;
                    }
                    break;
                }
                space -= 2;
                wr16(word, (uint16_t)((list[k].Location & 0xFFF) | list[k].Type << 12));
                word += 2;
                if (list[k].Type == 4 && space) {
                    wr16(word, list[k].Extra);
                    word += 2;
                    space -= 2;
                }
                k++;
            }
            uint32_t size = (uint32_t)(word - block);
            wr32(block + 4, size);
            block += size;
        }
    }
    MsDeltaFree(list);
    return true;
}

/* without a table of relocations every word that looks like an address inside the image is taken for one. */
static void inferred_relocations(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    uint8_t *image = pe->Image;
    uint32_t base = (uint32_t)MdPeImageBase(pe), limit = base + rd32(image + pe->Optional + 56);
    uint32_t headers = rd32(image + pe->Optional + 60);
    for (uint32_t i = headers; pe->Size >= 4 && i < pe->Size - 4;) {
        uint32_t value = rd32(image + i);
        if (value >= limit || value <= base) {
            i += 1;
            continue;
        }
        uint32_t marks = rd32(x->Hint + i);
        if (!(marks & 0x02020202u)) {
            wr32(x->Hint + i, marks | 0x01010101u);
            wr32(image + i, (uint32_t)MdRiftMap(x->Used, (int64_t)(value - base)) + (uint32_t)x->Target->ImageBase);
        }
        i += 4;
    }
}

/* ------------------------------------------------------------------ x86 code */

static void smash_lock_prefixes(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (pe->Machine != 0x14C || !pe->Is32 || !pe->LoadConfigOffset)
        return;
    uint8_t *image = pe->Image;
    uint32_t table = rd32(image + pe->LoadConfigOffset + 32);
    if (!table)
        return;
    uint32_t base = (uint32_t)MdPeImageBase(pe);
    uint32_t offset = MdPeOffset(pe, table - base, 0);
    if (!offset)
        return;
    uint32_t room = MdPeMaxSize(pe, table - base) >> 2;
    for (uint32_t i = 0; i < room; i++) {
        if (!inside(pe, offset + 4ull * i, 4))
            break;
        uint32_t address = rd32(image + offset + 4 * i);
        if (!address)
            break;
        uint32_t place = MdPeOffset(pe, address - base, 1);
        if (place) {
            unsigned s;
            for (s = 0; s < pe->SectionCount; s++) {
                const uint8_t *h = section(pe, s);
                if (rd32(h + 20) <= place && rd32(h + 8) + rd32(h + 12) > place)
                    break;
            }
            if (s < pe->SectionCount && inside(pe, place, 1))
                image[place] = 0xF0;
        }
    }
}

/* the file offset of a relative address in the data of a section, for a target of a call or jump. */
static uint32_t code_target(const MD_PE *pe, uint32_t rva)
{
    if (pe->HeadersSize > rva)
        return rva;
    unsigned i;
    for (i = 0; i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        uint32_t va = rd32(s + 12);
        if (va <= rva && va + rd32(s + 16) > rva)
            break;
    }
    if (i >= pe->SectionCount)
        return 0;
    const uint8_t *s = section(pe, i);
    uint32_t delta = rva - rd32(s + 12);
    if (rd32(s + 16) < delta)
        return 0;
    return delta + rd32(s + 20);
}

typedef struct MD_CODE {
    uint32_t Start, End; /* file offsets, End leaves room for a whole instruction. */
    uint32_t Rva;
} MD_CODE;

static bool code_section(const MD_PE *pe, unsigned index, MD_CODE *code)
{
    const uint8_t *s = section(pe, index);
    if (!(rd32(s + 36) & SCN_EXECUTE))
        return false;
    uint32_t length = rd32(s + 8);
    if (rd32(s + 16) < length)
        length = rd32(s + 16);
    uint32_t pointer = rd32(s + 20);
    if (pe->Size < pointer || pe->Size < pointer + length || length < 6)
        return false;
    code->Start = pointer;
    code->End = pointer + length - 5;
    code->Rva = rd32(s + 12);
    return code->End > code->Start;
}

static void relative_jumps(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (pe->Machine != 0x14C)
        return;
    uint8_t *image = pe->Image;
    uint32_t size_of_image = rd32(image + pe->Optional + 56);
    for (unsigned s = 0; s < pe->SectionCount; s++) {
        MD_CODE code;
        if (!code_section(pe, s, &code))
            continue;
        for (uint32_t p = code.Start; p < code.End; p++) {
            if (image[p] != 0xE9) {
                if (image[p] != 0x0F || code.End <= p + 1 || (image[p + 1] & 0xF0) != 0x80)
                    continue;
                p++;
            }
            bool previous_free = image[p] == 0xE9 || !(x->Hint[p - 1] & 1);
            bool clear = true;
            for (unsigned i = 0; i < 5; i++)
                if (x->Hint[p + i] & 1)
                    clear = false;
            if (!clear || !previous_free)
                continue;
            uint32_t rel = rd32(image + p + 1);
            if (rel + 128 <= 0xFF)
                continue;
            uint32_t next = code.Rva + (p - code.Start) + 5, target = next + rel;
            if (target >= size_of_image)
                continue;
            uint32_t at = code_target(pe, target);
            if (!at || (x->Hint[at] & 1))
                continue;
            int32_t moved = (int32_t)(map_rva(x, target) - map_rva(x, next));
            if (moved > 127 || moved < -128) {
                if ((uint32_t)moved != rel)
                    wr32(image + p + 1, (uint32_t)moved);
            } else {
                uint8_t opcode = image[p];
                if (opcode == 0xE9) {
                    image[p] = 0xEB;
                    image[p + 1] = (uint8_t)moved;
                } else {
                    image[p] = (uint8_t)moved;
                    image[p - 1] = (uint8_t)((opcode & 0xF) | 0x70);
                }
            }
            p += 4;
        }
    }
}

static void relative_calls(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (pe->Machine != 0x14C)
        return;
    uint8_t *image = pe->Image;
    uint32_t size_of_image = rd32(image + pe->Optional + 56);
    for (unsigned s = 0; s < pe->SectionCount; s++) {
        MD_CODE code;
        if (!code_section(pe, s, &code))
            continue;
        for (uint32_t p = code.Start; p < code.End; p++) {
            if (image[p] != 0xE8)
                continue;
            bool clear = true;
            for (unsigned i = 0; i < 5; i++)
                if (x->Hint[p + i] & 1)
                    clear = false;
            if (!clear)
                continue;
            uint32_t rel = rd32(image + p + 1);
            uint32_t next = code.Rva + (p - code.Start) + 5, target = next + rel;
            if (target >= size_of_image)
                continue;
            uint32_t at = code_target(pe, target);
            if (!at || (x->Hint[at] & 1))
                continue;
            uint32_t moved = map_rva(x, target) - map_rva(x, next);
            if (moved != rel)
                wr32(image + p + 1, moved);
            p += 4;
        }
    }
}

/* ------------------------------------------------------------------ amd64 code */

/* the file offset of an address in the headers or the data of a section, with the bytes the data has from it on. */
static bool amd64_locate(const MD_PE *pe, uint32_t rva, uint32_t *offset, uint32_t *available)
{
    if (pe->HeadersSize > rva) {
        *offset = rva;
        *available = pe->HeadersSize - rva;
        return true;
    }
    for (unsigned i = 0; i < pe->SectionCount; i++) {
        const uint8_t *s = section(pe, i);
        uint32_t va = rd32(s + 12), raw = rd32(s + 16);
        if (va <= rva && (uint64_t)va + raw > rva) {
            *offset = rva - va + rd32(s + 20);
            *available = va + raw - rva;
            return true;
        }
    }
    return false;
}

/* the displacements from the instruction pointer in the functions the table of exceptions lists. */
static bool disassembly_amd64(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (pe->Machine != 0x8664)
        return false;
    uint32_t table = pe->DirOffset[3];
    if (!table)
        return true;
    uint8_t *image = pe->Image;
    for (uint32_t i = 0, n = pe->DirSize[3] / 12; i < n; i++) {
        if (!inside(pe, (uint64_t)table + 12ull * i, 12))
            break;
        uint32_t begin = rd32(image + table + 12ull * i), end = rd32(image + table + 12ull * i + 4);
        if (end <= begin)
            continue;
        uint32_t length = end - begin, offset, available;
        if (pe->HeadersSize > begin) {
            if (pe->HeadersSize < end)
                continue;
            offset = begin;
        } else {
            if (!amd64_locate(pe, begin, &offset, &available) || available < length)
                continue;
        }
        if (!offset)
            continue;
        uint32_t rva = begin;
        while (length && inside(pe, offset, 1)) {
            if (x->Hint[offset] & 3) {
                length--, offset++, rva++;
                continue;
            }
            size_t room = pe->Size - offset;
            MD_INSN insn = MdAmd64Disassemble(image + offset, room < length ? room : length);
            unsigned k = 1;
            while (k < insn.Length && !(x->Hint[offset + k] & 2))
                k++;
            if (insn.Length > 1 && k < insn.Length) {
                length -= k, offset += k, rva += k;
                continue;
            }
            if (insn.Kind == 5 && inside(pe, (uint64_t)offset + insn.Offset, 4)) {
                uint32_t next = rva + insn.Length;
                uint32_t target = next + rd32(image + offset + insn.Offset);
                wr32(image + offset + insn.Offset, map_rva(x, target) - map_rva(x, next));
            }
            if (!insn.Length)
                break;
            length -= insn.Length, offset += insn.Length, rva += insn.Length;
        }
    }
    return true;
}

/* the table of exceptions: the bounds of every function, its unwind information, and the handler that information names. */
static bool exceptions_amd64(const MD_XFORM *x)
{
    const MD_PE *pe = x->Pe;
    if (pe->Machine != 0x8664)
        return false;
    uint32_t table = pe->DirOffset[3], size = pe->DirSize[3];
    if (!table)
        return true;
    uint8_t *image = pe->Image;
    uint32_t n = size / 12;
    if (pe->Size <= table + 4ull * n)
        return false;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *entry = image + table + 12ull * i;
        if (!inside(pe, (uint64_t)table + 12ull * i, 12))
            break;
        wr32(entry, map_rva(x, rd32(entry)));
        wr32(entry + 4, map_rva(x, rd32(entry + 4)));
        uint32_t unwind = rd32(entry + 8), at = 0, available = 0;
        bool found = amd64_locate(pe, unwind, &at, &available);
        if (found && at && (table > at || table + size < at)) {
            if (table > at && table < at + available)
                available = at - table;
            if (available >= 4 && inside(pe, at, 4) && (image[at] & 0x38)) {
                uint32_t handler = 4 + 4 * (((uint32_t)image[at + 2] + 1) >> 1);
                if (available > handler + 4 && inside(pe, (uint64_t)at + handler, 4))
                    wr32(image + at + handler, map_rva(x, rd32(image + at + handler)));
            }
        }
        wr32(entry + 8, map_rva(x, unwind));
    }
    return true;
}

/* ------------------------------------------------------------------ the whole */

void MdPeUnbind(MD_PE *pe)
{
    uint8_t *image = pe->Image;
    if (pe->DirOffset[11]) {
        memset(image + pe->DirOffset[11], 0, pe->DirSize[11]);
        uint32_t directories = pe->Optional + (pe->Is32 ? 96 : 112);
        wr32(image + directories + 8 * 11, 0);
        wr32(image + directories + 8 * 11 + 4, 0);
    }
    unsigned width = pe->Is32 ? 4 : 8;
    for (uint32_t d = 0; d < pe->ImportCount; d++) {
        uint32_t descriptor = pe->ImportOffset + 20 * d;
        if (!rd32(image + descriptor + 4))
            continue;
        wr32(image + descriptor + 4, 0);
        uint32_t table = MdPeOffset(pe, rd32(image + descriptor + 16), 0);
        uint32_t names = MdPeOffset(pe, rd32(image + descriptor), 0);
        if (table && names) {
            uint32_t a = MdPeMaxSize(pe, rd32(image + descriptor + 16)) / width;
            uint32_t b = MdPeMaxSize(pe, rd32(image + descriptor)) / width;
            uint32_t j = 0;
            while (a && b) {
                if (!inside(pe, table + j, width) || !inside(pe, names + j, width))
                    break;
                if ((width == 4 ? rd32(image + table + j) : rd64(image + table + j)) == 0)
                    break;
                if ((width == 4 ? rd32(image + names + j) : rd64(image + names + j)) == 0)
                    break;
                j += width;
                a--;
                b--;
            }
            memmove(image + table, image + names, j);
        }
        wr32(image + descriptor + 8, 0);
    }
    for (unsigned i = 0; i < pe->SectionCount; i++) {
        uint8_t *s = pe->Image + pe->SectionTable + 40u * i;
        if ((rd64(s) | 0x2020202020202020ull) == 0x202061746164692Eull) {
            wr32(s + 36, rd32(s + 36) | 0xC0000000u);
            return;
        }
    }
}

int MdPeTransform(MD_PE *pe, uint32_t file_type, uint64_t flags, const MD_RIFT *used, const MD_PE_TARGET *target)
{
    MD_XFORM x = {pe, nullptr, used, target, {}};
    x.Hint = MdAllocZero(pe->Size);
    if (!x.Hint)
        return MSDELTA_MEMORY;
    int status = MSDELTA_OK;
    bool i386 = file_type == 16 || (file_type == 32 && pe->Machine == 0x14C);
    bool amd64 = file_type == 32 && !i386;
    if (!i386 && !amd64) {
        status = MSDELTA_UNSUPPORTED;
        goto done;
    }
    if (!MdPeCli(pe, &x.Cli)) {
        status = MSDELTA_BAD;
        goto done;
    }
    if (flags & 1ull << 1)
        mark_non_executable(&x);
    if (flags & 1ull << 2)
        imports(&x);
    if (flags & 1ull << 3)
        exports(&x);
    if (flags & 1ull << 4)
        resources(&x);
    if (flags & 1ull << 5) {
        if (pe->DirOffset[5]) {
            if (!relocations(&x))
                status = MSDELTA_MEMORY;
        } else if (i386 && pe->Machine == 0x14C)
            inferred_relocations(&x);
        else if (i386) {
            status = MSDELTA_BAD;
            goto done;
        }
    }
    if (flags & 1ull << 21)
        MdPeCliDisassemble(pe, &x.Cli, &target->Map);
    if (flags & 1ull << 22)
        MdPeCliMetadata(pe, &x.Cli, &target->Map, used, &target->Cli);
    if (amd64) {
        if (((flags & 1ull << 9) && !disassembly_amd64(&x)) || ((flags & 1ull << 10) && !exceptions_amd64(&x)))
            status = MSDELTA_BAD;
        if (status != MSDELTA_OK)
            goto done;
    }
    if (flags & 1ull << 6)
        smash_lock_prefixes(&x);
    if (flags & 1ull << 7)
        relative_jumps(&x);
    if (flags & 1ull << 8)
        relative_calls(&x);
    /* the target's own numbers. */
    if (pe->Is32)
        wr32(pe->Image + pe->Optional + 28, (uint32_t)target->ImageBase);
    else
        wr64(pe->Image + pe->Optional + 24, target->ImageBase);
    if (8 < pe->DirectoryCount)
        wr32(pe->Image + pe->Optional + (pe->Is32 ? 96 : 112) + 8 * 8, target->GlobalPointer);
    wr32(pe->Image + pe->Nt + 8, target->TimeStamp);
    wr32(pe->Image + pe->Optional + 64, 0);
done:
    MsDeltaFree(x.Hint);
    return status;
}
