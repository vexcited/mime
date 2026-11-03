/* applying a patch: the header, the formats of the compressed data, and the decoder that copies from source and target. */
#include "../crypto/crypto.h"
#include "internal.h"

#define PA30 0x30334150u

/* the three code lengths tables of a block: matches and literals, long lengths, aligned offsets. */
#define MAIN_SYMBOLS 600
#define LENGTH_SYMBOLS 256
#define ALIGNED_SYMBOLS 16
#define BLOCK_LENGTHS (MAIN_SYMBOLS + LENGTH_SYMBOLS + ALIGNED_SYMBOLS)
#define PRE_SYMBOLS 39

/* values of a match: below the first a distance through a rift, then the same distance again, then offsets. */
#define VALUE_DELTA_BASE 172032u
#define VALUE_RIFT 344064u

typedef struct MD_BLOCK {
    uint64_t Start;
    uint8_t Lengths[BLOCK_LENGTHS];
} MD_BLOCK;

typedef struct MD_FORMAT {
    MD_BLOCK *Blocks;
    size_t Count;
} MD_FORMAT;

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read_u64(const uint8_t *p)
{
    return read_u32(p) | (uint64_t)read_u32(p + 4) << 32;
}

typedef struct MD_HEADER {
    MSDELTA_INFO Info;
    const uint8_t *PreProcess, *Patch;
    size_t PreProcessSize, PatchSize;
} MD_HEADER;

static int parse_header(const uint8_t *patch, size_t size, MD_HEADER *header)
{
    memset(header, 0, sizeof(*header));
    if (size < 13 || read_u32(patch) != PA30)
        return MSDELTA_BAD;
    header->Info.FileTime = read_u64(patch + 4);
    MD_BITS bits;
    if (!MdBitsOpen(&bits, patch + 12, size - 12))
        return MSDELTA_BAD;
    uint64_t version = MdBitsReadNumber(&bits);
    header->Info.FileType = (uint32_t)MdBitsReadNumber(&bits);
    header->Info.Flags = MdBitsReadNumber(&bits);
    header->Info.TargetSize = MdBitsReadNumber(&bits);
    header->Info.HashAlgId = (uint32_t)MdBitsReadNumber(&bits);
    size_t hash_size;
    const uint8_t *hash = MdBitsReadBuffer(&bits, &hash_size);
    if (bits.Failed || hash_size > sizeof(header->Info.Hash))
        return MSDELTA_BAD;
    (void)version;
    memcpy(header->Info.Hash, hash, hash_size);
    header->Info.HashSize = (uint32_t)hash_size;
    header->PreProcess = MdBitsReadBuffer(&bits, &header->PreProcessSize);
    if (bits.Failed)
        return MSDELTA_BAD;
    header->Patch = MdBitsReadBuffer(&bits, &header->PatchSize);
    return bits.Failed ? MSDELTA_BAD : MSDELTA_OK;
}

int MsDeltaInfo(const uint8_t *patch, size_t patch_size, MSDELTA_INFO *info)
{
    MD_HEADER header;
    int status = parse_header(patch, patch_size, &header);
    if (status == MSDELTA_OK)
        *info = header.Info;
    return status;
}

/* ------------------------------------------------------------------ the formats */

static void default_lengths(uint8_t *lengths, unsigned symbols)
{
    unsigned bits = 1;
    while ((1u << bits) < symbols)
        bits++;
    unsigned short_ones = (1u << bits) - symbols; /* the symbols that take one bit less. */
    for (unsigned i = 0; i < symbols; i++)
        lengths[i] = (uint8_t)(i < short_ones ? bits - 1 : bits);
}

/* lengths of one block, coded against the ones of the block before it. */
static bool read_block_lengths(MD_BITS *bits, const MD_HUFFMAN *pre, const uint8_t *before, uint8_t *lengths)
{
    unsigned position = 0;
    while (position < BLOCK_LENGTHS) {
        int symbol = MdHuffmanDecode(pre, bits);
        if (symbol < 0)
            return false;
        if (symbol <= 16) {
            lengths[position++] = (uint8_t)symbol;
        } else if (symbol < 23) {
            int delta = symbol - 17;
            int length = delta < 3 ? before[position] + delta + 1 : before[position] - (delta - 3) - 1;
            if (length < 0 || length > 16)
                return false;
            lengths[position++] = (uint8_t)length;
        } else {
            unsigned code = (unsigned)(symbol - 23), kind = code & 7, count;
            if (kind < 3)
                count = kind + 1;
            else
                count = (1u << (kind - 1)) | (unsigned)MdBitsRead(bits, kind - 1);
            if (count + position >= BLOCK_LENGTHS)
                return false;
            if (code < 8) {
                if (!position)
                    return false;
                for (unsigned i = 0; i < count; i++, position++)
                    lengths[position] = lengths[position - 1];
            } else {
                for (unsigned i = 0; i < count; i++, position++)
                    lengths[position] = before[position];
            }
        }
        if (bits->Failed)
            return false;
    }
    for (unsigned i = 0; i < BLOCK_LENGTHS; i++)
        if (lengths[i] > 16)
            return false;
    return true;
}

static void format_free(MD_FORMAT *format)
{
    MsDeltaFree(format->Blocks);
    memset(format, 0, sizeof(*format));
}

static bool format_read(MD_BITS *bits, MD_FORMAT *format)
{
    memset(format, 0, sizeof(*format));
    if (MdBitsRead(bits, 1)) {
        format->Blocks = MdAllocZero(sizeof(MD_BLOCK));
        if (!format->Blocks)
            return false;
        format->Count = 1;
        default_lengths(format->Blocks[0].Lengths, MAIN_SYMBOLS);
        default_lengths(format->Blocks[0].Lengths + MAIN_SYMBOLS, LENGTH_SYMBOLS);
        default_lengths(format->Blocks[0].Lengths + MAIN_SYMBOLS + LENGTH_SYMBOLS, ALIGNED_SYMBOLS);
        return true;
    }
    uint64_t count = MdBitsReadNumber(bits);
    if (bits->Failed || !count || count > (1u << 20))
        return false;
    format->Blocks = MdAllocZero(count * sizeof(MD_BLOCK));
    if (!format->Blocks)
        return false;
    format->Count = (size_t)count;
    uint64_t start = 0;
    for (size_t i = 0; i < format->Count; i++) {
        start += MdBitsReadNumber(bits);
        format->Blocks[i].Start = start;
    }
    uint8_t pre_lengths[PRE_SYMBOLS];
    for (unsigned i = 0; i < PRE_SYMBOLS; i++)
        pre_lengths[i] = (uint8_t)MdBitsRead(bits, 4);
    MD_HUFFMAN pre;
    if (bits->Failed || !MdHuffmanBuild(&pre, pre_lengths, PRE_SYMBOLS)) {
        format_free(format);
        return false;
    }
    uint8_t zeros[BLOCK_LENGTHS] = {};
    bool ok = true;
    for (size_t i = 0; ok && i < format->Count; i++)
        ok = read_block_lengths(bits, &pre, i ? format->Blocks[i - 1].Lengths : zeros, format->Blocks[i].Lengths);
    MdHuffmanFree(&pre);
    if (!ok)
        format_free(format);
    return ok;
}

/* ------------------------------------------------------------------ the stretches of the rift table */

typedef struct MD_STRETCH {
    uint64_t Position; /* where it starts, 0 for the sentinels. */
    uint64_t Offset;   /* added to a position in the output to find the position to copy from. */
} MD_STRETCH;

typedef struct MD_STRETCHES {
    MD_STRETCH *Items; /* a sentinel, the stretches, a sentinel. */
    size_t Count;
} MD_STRETCHES;

static uint64_t shift_of(int64_t left, int64_t right)
{
    if (((uint64_t)left & 0xFFFFFFFF00000000ull) != 0 || (((uint64_t)left - (uint64_t)right) & 0xFFFFFFFF00000000ull) != 0)
        return 0;
    return (uint64_t)right - (uint64_t)left;
}

static bool stretches_build(MD_STRETCHES *s, const MD_RIFT *rift)
{
    memset(s, 0, sizeof(*s));
    for (size_t i = 1; i < rift->Count; i++)
        if (rift->Entries[i - 1].Left >= rift->Entries[i].Left)
            return false;
    size_t first = 0;
    while (first < rift->Count && rift->Entries[first].Left <= 0)
        first++;
    size_t skipped = first && rift->Entries[first - 1].Left == 0 ? 1 : 0;
    size_t real = rift->Count - skipped;
    s->Items = MdAllocZero((real + 2) * sizeof(MD_STRETCH));
    if (!s->Items)
        return false;
    s->Count = real + 2;
    uint64_t base = 0;
    if (first)
        base = shift_of(rift->Entries[first - 1].Left, rift->Entries[first - 1].Right);
    else if (rift->Count)
        base = shift_of(rift->Entries[rift->Count - 1].Left, rift->Entries[rift->Count - 1].Right);
    s->Items[0] = (MD_STRETCH){0, base};
    s->Items[real + 1] = (MD_STRETCH){0, base};
    size_t at = first;
    for (size_t i = 0; i < real; i++) {
        s->Items[i + 1].Position = (uint64_t)rift->Entries[at].Left;
        s->Items[i + 1].Offset = shift_of(rift->Entries[at].Left, rift->Entries[at].Right);
        at = at + 1 < rift->Count ? at + 1 : 0;
    }
    return true;
}

/* ------------------------------------------------------------------ the decoder */

typedef struct MD_TABLES {
    MD_HUFFMAN Main, Length, Aligned;
    bool Built;
} MD_TABLES;

static void tables_free(MD_TABLES *t)
{
    if (!t->Built)
        return;
    MdHuffmanFree(&t->Main);
    MdHuffmanFree(&t->Length);
    MdHuffmanFree(&t->Aligned);
    t->Built = false;
}

static bool tables_build(MD_TABLES *t, const MD_BLOCK *block)
{
    tables_free(t);
    memset(&t->Main, 0, sizeof(t->Main));
    memset(&t->Length, 0, sizeof(t->Length));
    memset(&t->Aligned, 0, sizeof(t->Aligned));
    if (MdHuffmanBuild(&t->Main, block->Lengths, MAIN_SYMBOLS) &&
        MdHuffmanBuild(&t->Length, block->Lengths + MAIN_SYMBOLS, LENGTH_SYMBOLS) &&
        MdHuffmanBuild(&t->Aligned, block->Lengths + MAIN_SYMBOLS + LENGTH_SYMBOLS, ALIGNED_SYMBOLS)) {
        t->Built = true;
        return true;
    }
    t->Built = true;
    tables_free(t);
    return false;
}

/* a long length: a run of zeros that says how many bits follow, a one, and the bits. */
static bool read_long_length(MD_BITS *bits, uint32_t *length)
{
    unsigned zeros = 0;
    while (!MdBitsRead(bits, 1)) {
        if (bits->Failed || ++zeros > 23)
            return false;
    }
    uint64_t value = (1ull << (zeros + 8)) | MdBitsRead(bits, zeros + 8);
    if (bits->Failed || value > 0xFFFFFFF7ull)
        return false;
    *length = (uint32_t)value;
    return true;
}

typedef struct MD_SYMBOL {
    uint32_t Value; /* 0 for a literal, which is then in Length. */
    uint32_t Length;
} MD_SYMBOL;

static bool read_symbol(const MD_TABLES *t, MD_BITS *bits, MD_SYMBOL *out)
{
    int symbol = MdHuffmanDecode(&t->Main, bits);
    if (symbol < 0)
        return false;
    if (symbol < 256) {
        out->Value = 0;
        out->Length = (uint32_t)symbol;
        return true;
    }
    unsigned slot = (unsigned)(symbol - 256) >> 3, code = (unsigned)(symbol - 256) & 7;
    uint64_t value;
    if (slot == 0) {
        value = VALUE_DELTA_BASE + MdBitsRead(bits, 14) - 0x2000;
    } else if (slot == 1) {
        uint64_t raw = MdBitsRead(bits, 16);
        value = VALUE_DELTA_BASE + (raw >= 0x8000 ? raw - 0x6000 : raw - 0xA000);
    } else if (slot == 2) {
        uint64_t raw = MdBitsRead(bits, 18);
        value = VALUE_DELTA_BASE + (raw >= 0x20000 ? raw - 0x16000 : raw - 0x2A000);
    } else if (slot < 7) {
        value = VALUE_RIFT + (slot - 3);
    } else {
        if (slot == 7) {
            if (!MdBitsRead(bits, 1))
                slot = 43 + (unsigned)MdBitsRead(bits, 2);
            else if (!MdBitsRead(bits, 1))
                slot = 47 + (unsigned)MdBitsRead(bits, 3);
            else
                slot = 55 + (unsigned)MdBitsRead(bits, 4);
        }
        unsigned s = slot - 7;
        if (s >= 64)
            return false;
        uint64_t offset;
        if (s < 4) {
            offset = s;
        } else {
            unsigned top = (s & 1) + 2, count = (s >> 1) - 1;
            if (count >= 4) {
                unsigned extra = count - 4;
                uint64_t high = top;
                if (extra)
                    high = (high << extra) | MdBitsRead(bits, extra);
                int aligned = MdHuffmanDecode(&t->Aligned, bits);
                if (aligned < 0)
                    return false;
                offset = high << 4 | (unsigned)aligned;
            } else {
                offset = ((uint64_t)top << count) | MdBitsRead(bits, count);
            }
        }
        if (offset > 0xFFFABFFCull)
            return false;
        value = offset + VALUE_RIFT + 3;
    }
    if (bits->Failed)
        return false;
    uint32_t length;
    if (code)
        length = code + 1;
    else {
        int extra = MdHuffmanDecode(&t->Length, bits);
        if (extra < 0)
            return false;
        if (extra) {
            length = (uint32_t)extra + 8;
        } else {
            uint32_t longer;
            if (!read_long_length(bits, &longer))
                return false;
            length = longer + 8;
        }
    }
    out->Value = (uint32_t)value;
    out->Length = length;
    return true;
}

/*
 * decodes the matches into the buffer that holds the source and then the target. positions are in that buffer, the
 * decoding starts after the source.
 */
static int decode(MD_BITS *bits, const MD_FORMAT *format, const MD_STRETCHES *stretches, uint8_t *buffer,
                  uint64_t start, uint64_t end)
{
    MD_TABLES tables = {};
    size_t block = (size_t)-1;
    uint64_t block_end = 0;
    uint64_t lru[3] = {};
    size_t stretch = 0;
    uint64_t position = start;
    int status = MSDELTA_BAD;
    while (MdBitsLeft(bits)) {
        if (block == (size_t)-1 || position > block_end) {
            size_t low = 0, high = format->Count - 1;
            while (low < high) {
                size_t middle = (low + high + 1) >> 1;
                if (format->Blocks[middle].Start <= position)
                    low = middle;
                else
                    high = middle - 1;
            }
            block_end = low + 1 < format->Count ? format->Blocks[low + 1].Start - 1 : UINT64_MAX;
            if (low != block && !tables_build(&tables, &format->Blocks[low]))
                goto done;
            block = low;
        }
        MD_SYMBOL symbol;
        if (!read_symbol(&tables, bits, &symbol))
            goto done;
        if (!symbol.Value) {
            if (position >= end)
                goto done;
            buffer[position++] = (uint8_t)symbol.Length;
            continue;
        }
        if (symbol.Length > end - position)
            goto done;
        uint64_t distance;
        if (symbol.Value == VALUE_RIFT || symbol.Value < VALUE_RIFT) {
            while (stretches->Items[stretch + 1].Position - 1 < position)
                stretch++;
            uint64_t offset = stretches->Items[stretch].Offset;
            if (!offset || position < (uint64_t)-(int64_t)offset)
                goto done;
            if (symbol.Value == VALUE_RIFT) {
                /* the whole match is carried through the stretches it crosses. */
                distance = (uint64_t)-(int64_t)offset;
                uint32_t left = symbol.Length;
                while (left) {
                    uint64_t room = stretches->Items[stretch + 1].Position - position;
                    uint64_t part = left < room ? left : room;
                    uint64_t from = position + stretches->Items[stretch].Offset;
                    if (!stretches->Items[stretch].Offset || from >= position)
                        goto done;
                    for (uint64_t i = 0; i < part; i++)
                        buffer[position + i] = buffer[from + i];
                    position += part;
                    left -= (uint32_t)part;
                    if (left && room == part)
                        stretch++;
                }
            } else {
                distance = (uint64_t)((int64_t)symbol.Value - (int64_t)VALUE_DELTA_BASE) - offset;
            }
        } else {
            uint64_t index = symbol.Value - (VALUE_RIFT + 1);
            distance = index < 3 ? lru[index] : symbol.Value - (VALUE_RIFT + 3);
        }
        if (symbol.Value != VALUE_RIFT) {
            if (!distance || distance > position)
                goto done;
            for (uint32_t i = 0; i < symbol.Length; i++, position++)
                buffer[position] = buffer[position - distance];
        }
        if (lru[0] != distance) {
            if (lru[1] == distance) {
                lru[1] = lru[0];
            } else {
                lru[2] = lru[1];
                lru[1] = lru[0];
            }
            lru[0] = distance;
        }
    }
    status = position == end ? MSDELTA_OK : MSDELTA_BAD;
done:
    tables_free(&tables);
    return status;
}

/* ------------------------------------------------------------------ the whole patch */

int MsDeltaApply(const uint8_t *source, size_t source_size, const uint8_t *patch, size_t patch_size, uint8_t **target,
                 size_t *target_size)
{
    MD_HEADER header;
    int status = parse_header(patch, patch_size, &header);
    if (status != MSDELTA_OK)
        return status;
    if (header.Info.TargetSize > ((size_t)1 << 31))
        return MSDELTA_UNSUPPORTED;
    uint8_t *prepared = nullptr;
    size_t prepared_size = 0;
    MD_RIFT preprocess_rift = {}, base = {}, sum = {}, scratch = {}, one = {};
    MD_FORMAT format = {};
    MD_STRETCHES stretches = {};
    uint8_t *buffer = nullptr;
    MD_BITS preprocess;
    if (header.PreProcessSize && !MdBitsOpen(&preprocess, header.PreProcess, header.PreProcessSize))
        return MSDELTA_BAD;
    if (!header.PreProcessSize)
        preprocess.Failed = true;
    status = MdPrepareSource(header.Info.FileType, header.Info.Flags, source, source_size, &preprocess,
                             header.Info.TargetSize, &prepared, &prepared_size, &preprocess_rift);
    if (status != MSDELTA_OK)
        return status;
    status = MSDELTA_MEMORY;
    /* the stretch that carries every position of the target to the same position of the source, then the preparation's. */
    if (!MdRiftAdd(&one, (int64_t)prepared_size, 0) || !MdRiftMultiply(&scratch, &one, &preprocess_rift))
        goto done;
    MdRiftReduce(&scratch);
    MD_BITS bits;
    status = MSDELTA_BAD;
    if (!MdBitsOpen(&bits, header.Patch, header.PatchSize) || !MdRiftRead(&bits, &base))
        goto done;
    status = MSDELTA_MEMORY;
    if (!MdRiftSum(&sum, &base, &scratch))
        goto done;
    status = MSDELTA_BAD;
    if (!format_read(&bits, &format) || !stretches_build(&stretches, &sum))
        goto done;
    status = MSDELTA_MEMORY;
    uint64_t total = prepared_size + header.Info.TargetSize;
    buffer = MdAlloc(total);
    if (!buffer)
        goto done;
    memcpy(buffer, prepared, prepared_size);
    status = decode(&bits, &format, &stretches, buffer, prepared_size, total);
    if (status != MSDELTA_OK)
        goto done;
    size_t size = (size_t)header.Info.TargetSize;
    memmove(buffer, buffer + prepared_size, size);
    status = MdFinishTarget(&header.Info, buffer, size);
    if (status != MSDELTA_OK)
        goto done;
    *target = buffer;
    *target_size = size;
    buffer = nullptr;
done:
    MsDeltaFree(buffer);
    MsDeltaFree(stretches.Items);
    format_free(&format);
    MdRiftFree(&sum);
    MdRiftFree(&scratch);
    MdRiftFree(&one);
    MdRiftFree(&base);
    MdRiftFree(&preprocess_rift);
    if (prepared != source)
        MsDeltaFree(prepared);
    return status;
}
