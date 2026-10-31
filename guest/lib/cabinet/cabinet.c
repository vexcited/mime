/* a reader of cabinet files in memory: stored, MSZIP and LZX folders. */
#include "cabinet.h"
#ifdef _WIN32
#include <rtl.h>
/* a guest library has no heap of its own: the caller gives the allocators. */
#define malloc(size) ((void *)0)
#define free(memory) ((void)(memory))
#else
#include <stdlib.h>
#include <string.h>
#endif

/* ------------------------------------------------------------------ the file */

#define CFHEADER_SIZE 36
#define CFFOLDER_SIZE 8
#define CFFILE_SIZE 16
#define CFDATA_SIZE 8
#define CAB_FLAG_PREV 1
#define CAB_FLAG_NEXT 2
#define CAB_FLAG_RESERVE 4
#define FOLDER_CONTINUED_FROM_PREV 0xFFFD
#define FOLDER_CONTINUED_TO_NEXT 0xFFFE
#define FOLDER_CONTINUED_PREV_AND_NEXT 0xFFFF
#define FRAME_SIZE 32768u

#define TYPE_NONE 0
#define TYPE_MSZIP 1
#define TYPE_QUANTUM 2
#define TYPE_LZX 3

typedef struct FOLDER {
    uint32_t Start; /* file offset of the first block. */
    uint16_t Blocks;
    uint16_t Type;
} FOLDER;

struct MIME_CAB {
    const uint8_t *Data;
    /* the bytes of the cabinet, and how many of them are held in Data: a streamed cabinet keeps only what comes before
       its blocks. */
    size_t Size, Held;
    MIME_CAB_READ Read;
    void *ReadContext;
    uint8_t *Head;
    uint16_t Folders, Files, Flags, SetId, Index;
    uint8_t FolderReserve, DataReserve;
    bool SkipContinued, Sequential;
    FOLDER *Folder;
    /* file offsets of the file entries, in stored order. */
    uint32_t *FileAt;
    MIME_CAB_ALLOC Allocate;
    MIME_CAB_FREE Release;
};

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t dword(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void *get(MIME_CAB *cab, size_t size)
{
    return cab->Allocate ? cab->Allocate(size) : malloc(size);
}

static void put(MIME_CAB *cab, void *memory)
{
    if (!memory)
        return;
    if (cab->Release)
        cab->Release(memory);
    else
        free(memory);
}

/* one past the terminator of the string at offset, or 0 when it runs off the file. */
static size_t string_end(const MIME_CAB *cab, size_t offset)
{
    while (offset < cab->Held) {
        if (!cab->Data[offset])
            return offset + 1;
        offset++;
    }
    return 0;
}

/* reads the structure of a cabinet from the bytes held of its size. */
static int open_held(MIME_CAB **out, const void *data, size_t held, size_t size, MIME_CAB_READ read, void *read_context,
                     uint8_t *head, MIME_CAB_ALLOC allocate, MIME_CAB_FREE release)
{
    const uint8_t *p = data;
    *out = nullptr;
    if (!data || held < CFHEADER_SIZE || memcmp(p, "MSCF", 4))
        return MIME_CAB_BAD;
    /* only format version 1.3 exists. */
    if (p[24] != 3 || p[25] != 1)
        return MIME_CAB_BAD;
    MIME_CAB *cab = allocate ? allocate(sizeof(*cab)) : malloc(sizeof(*cab));
    if (!cab)
        return MIME_CAB_MEMORY;
    memset(cab, 0, sizeof(*cab));
    cab->Allocate = allocate;
    cab->Release = release;
    cab->Data = p;
    cab->Size = size;
    cab->Held = held;
    cab->Read = read;
    cab->ReadContext = read_context;
    uint32_t total = dword(p + 8), files_at = dword(p + 16);
    cab->Folders = word(p + 26);
    cab->Files = word(p + 28);
    cab->Flags = word(p + 30);
    cab->SetId = word(p + 32);
    cab->Index = word(p + 34);
    /* a truncated cabinet is refused. */
    if (total > size || total < CFHEADER_SIZE || files_at >= total) {
        MimeCabClose(cab);
        return MIME_CAB_BAD;
    }
    cab->Size = total;
    if (!read)
        cab->Held = total;
    size_t at = CFHEADER_SIZE;
    if (cab->Flags & CAB_FLAG_RESERVE) {
        if (at + 4 > cab->Held) {
            MimeCabClose(cab);
            return MIME_CAB_BAD;
        }
        uint16_t header_reserve = word(p + at);
        cab->FolderReserve = p[at + 2];
        cab->DataReserve = p[at + 3];
        at += 4 + header_reserve;
    }
    for (int i = 0; i < ((cab->Flags & CAB_FLAG_PREV) ? 2 : 0) + ((cab->Flags & CAB_FLAG_NEXT) ? 2 : 0); i++) {
        size_t end = string_end(cab, at);
        if (!end) {
            MimeCabClose(cab);
            return MIME_CAB_BAD;
        }
        at = end;
    }
    size_t folder_size = CFFOLDER_SIZE + cab->FolderReserve;
    if (cab->Folders == 0 || at + (size_t)cab->Folders * folder_size > cab->Held) {
        MimeCabClose(cab);
        return MIME_CAB_BAD;
    }
    cab->Folder = get(cab, sizeof(FOLDER) * cab->Folders);
    cab->FileAt = get(cab, sizeof(uint32_t) * (cab->Files ? cab->Files : 1u));
    if (!cab->Folder || !cab->FileAt) {
        MimeCabClose(cab);
        return MIME_CAB_MEMORY;
    }
    for (unsigned i = 0; i < cab->Folders; i++) {
        const uint8_t *f = p + at + i * folder_size;
        cab->Folder[i].Start = dword(f);
        cab->Folder[i].Blocks = word(f + 4);
        cab->Folder[i].Type = word(f + 6);
        if (cab->Folder[i].Start > cab->Size || (cab->Folder[i].Start == cab->Size && cab->Folder[i].Blocks)) {
            MimeCabClose(cab);
            return MIME_CAB_BAD;
        }
    }
    size_t file_at = files_at;
    for (unsigned i = 0; i < cab->Files; i++) {
        if (file_at + CFFILE_SIZE > cab->Held) {
            MimeCabClose(cab);
            return MIME_CAB_BAD;
        }
        size_t end = string_end(cab, file_at + CFFILE_SIZE);
        if (!end) {
            MimeCabClose(cab);
            return MIME_CAB_BAD;
        }
        cab->FileAt[i] = (uint32_t)file_at;
        file_at = end;
    }
    cab->Head = head;
    *out = cab;
    return MIME_CAB_OK;
}

int MimeCabOpen(MIME_CAB **out, const void *data, size_t size, MIME_CAB_ALLOC allocate, MIME_CAB_FREE release)
{
    return open_held(out, data, size, size, nullptr, nullptr, nullptr, allocate, release);
}

int MimeCabOpenStream(MIME_CAB **out, MIME_CAB_READ read, void *context, uint64_t size, MIME_CAB_ALLOC allocate,
                      MIME_CAB_FREE release)
{
    *out = nullptr;
    if (!read || size < CFHEADER_SIZE || size > UINT32_MAX)
        return MIME_CAB_BAD;
    uint8_t first[CFHEADER_SIZE];
    if (read(context, 0, first, sizeof(first)) || memcmp(first, "MSCF", 4))
        return MIME_CAB_BAD;
    /* the folders and the file entries come before the blocks, and a file entry is a name of 256 bytes at most. */
    uint64_t held = (uint64_t)dword(first + 16) + (uint64_t)word(first + 28) * (CFFILE_SIZE + 257);
    if (held < CFHEADER_SIZE || dword(first + 16) >= size)
        return MIME_CAB_BAD;
    if (held > size)
        held = size;
    uint8_t *head = allocate ? allocate((size_t)held) : malloc((size_t)held);
    if (!head)
        return MIME_CAB_MEMORY;
    if (read(context, 0, head, (size_t)held)) {
        if (release)
            release(head);
        else
            free(head);
        return MIME_CAB_BAD;
    }
    int result = open_held(out, head, (size_t)held, (size_t)size, read, context, head, allocate, release);
    if (result) {
        if (release)
            release(head);
        else
            free(head);
    }
    return result;
}

void MimeCabClose(MIME_CAB *cab)
{
    if (!cab)
        return;
    put(cab, cab->Folder);
    put(cab, cab->FileAt);
    put(cab, cab->Head);
    put(cab, cab);
}

void MimeCabSet(const MIME_CAB *cab, uint16_t *id, uint16_t *index, uint16_t *flags)
{
    if (id)
        *id = cab->SetId;
    if (index)
        *index = cab->Index;
    if (flags)
        *flags = cab->Flags;
}

void MimeCabSkipContinued(MIME_CAB *cab)
{
    cab->SkipContinued = true;
}

void MimeCabSequential(MIME_CAB *cab)
{
    cab->Sequential = true;
}

unsigned MimeCabFolderCount(const MIME_CAB *cab)
{
    return cab->Folders;
}

unsigned MimeCabFileCount(const MIME_CAB *cab)
{
    return cab->Files;
}

int MimeCabFile(const MIME_CAB *cab, unsigned index, MIME_CAB_FILE *file)
{
    if (index >= cab->Files)
        return MIME_CAB_BAD;
    const uint8_t *f = cab->Data + cab->FileAt[index];
    file->Size = dword(f);
    file->Offset = dword(f + 4);
    file->Folder = word(f + 8);
    file->Date = word(f + 10);
    file->Time = word(f + 12);
    file->Attributes = word(f + 14);
    file->Name = (const char *)(f + CFFILE_SIZE);
    return MIME_CAB_OK;
}

int MimeCabFolderType(const MIME_CAB *cab, unsigned folder, uint16_t *type)
{
    if (folder >= cab->Folders)
        return MIME_CAB_BAD;
    *type = cab->Folder[folder].Type;
    return MIME_CAB_OK;
}

/* ------------------------------------------------------------------ block checksum */

/* xor of the dwords; the last one to three bytes form the low bytes of a final dword. */
static uint32_t checksum(const uint8_t *data, size_t bytes, uint32_t sum)
{
    for (size_t i = bytes / 4; i; i--, data += 4)
        sum ^= dword(data);
    uint32_t tail = 0;
    switch (bytes & 3) {
    case 3:
        tail |= (uint32_t)*data++ << 16;
        [[fallthrough]];
    case 2:
        tail |= (uint32_t)*data++ << 8;
        [[fallthrough]];
    case 1:
        tail |= *data;
    }
    return sum ^ tail;
}

/* ------------------------------------------------------------------ inflate for MSZIP */

typedef struct INFLATE {
    uint8_t Window[32768];
    uint32_t Position;
    /* input of the current block. */
    const uint8_t *In;
    size_t InSize, InPos;
    uint32_t Bits, BitCount;
    uint8_t *Out;
    size_t OutSize, OutPos;
} INFLATE;

typedef struct CODE {
    uint16_t Count[16];
    uint16_t Symbol[288];
} CODE;

static int need_bits(INFLATE *z, unsigned n)
{
    while (z->BitCount < n) {
        if (z->InPos >= z->InSize)
            return 0;
        z->Bits |= (uint32_t)z->In[z->InPos++] << z->BitCount;
        z->BitCount += 8;
    }
    return 1;
}

static int take_bits(INFLATE *z, unsigned n, uint32_t *value)
{
    if (!n) {
        *value = 0;
        return 1;
    }
    if (!need_bits(z, n))
        return 0;
    *value = z->Bits & ((1u << n) - 1);
    z->Bits >>= n;
    z->BitCount -= n;
    return 1;
}

/* builds the canonical code: 0 when complete, positive when incomplete, negative when oversubscribed. */
static int make_code(CODE *code, const uint8_t *lengths, unsigned n)
{
    memset(code->Count, 0, sizeof(code->Count));
    for (unsigned i = 0; i < n; i++)
        code->Count[lengths[i]]++;
    if (code->Count[0] == n)
        return 0;
    int left = 1;
    for (unsigned length = 1; length < 16; length++) {
        left <<= 1;
        left -= code->Count[length];
        if (left < 0)
            return left;
    }
    uint16_t offsets[16];
    offsets[1] = 0;
    for (unsigned length = 1; length < 15; length++)
        offsets[length + 1] = (uint16_t)(offsets[length] + code->Count[length]);
    for (unsigned i = 0; i < n; i++)
        if (lengths[i])
            code->Symbol[offsets[lengths[i]]++] = (uint16_t)i;
    return left;
}

static int decode_symbol(INFLATE *z, const CODE *code)
{
    int first = 0, index = 0, compare = 0;
    for (unsigned length = 1; length < 16; length++) {
        uint32_t bit;
        if (!take_bits(z, 1, &bit))
            return -1;
        compare |= (int)bit;
        int count = code->Count[length];
        if (compare - count < first)
            return code->Symbol[index + (compare - first)];
        index += count;
        first += count;
        first <<= 1;
        compare <<= 1;
    }
    return -1;
}

static int put_byte(INFLATE *z, uint8_t byte)
{
    if (z->OutPos >= z->OutSize)
        return 0;
    z->Out[z->OutPos++] = byte;
    z->Window[z->Position++ & 32767u] = byte;
    return 1;
}

static const uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int inflate_codes(INFLATE *z, const CODE *literals, const CODE *distances)
{
    for (;;) {
        int symbol = decode_symbol(z, literals);
        if (symbol < 0)
            return 0;
        if (symbol < 256) {
            if (!put_byte(z, (uint8_t)symbol))
                return 0;
            continue;
        }
        if (symbol == 256)
            return 1;
        symbol -= 257;
        if (symbol >= 29)
            return 0;
        uint32_t extra, length, distance;
        if (!take_bits(z, length_extra[symbol], &extra))
            return 0;
        length = length_base[symbol] + extra;
        symbol = decode_symbol(z, distances);
        if (symbol < 0 || symbol >= 30)
            return 0;
        if (!take_bits(z, distance_extra[symbol], &extra))
            return 0;
        distance = distance_base[symbol] + extra;
        /* the window holds the history of this block and the ones before it. */
        if (distance > 32768u)
            return 0;
        for (uint32_t i = 0; i < length; i++)
            if (!put_byte(z, z->Window[(z->Position - distance) & 32767u]))
                return 0;
    }
}

static int inflate_fixed(INFLATE *z)
{
    static CODE literals, distances;
    static int built;
    if (!built) {
        uint8_t lengths[288];
        unsigned i = 0;
        for (; i < 144; i++)
            lengths[i] = 8;
        for (; i < 256; i++)
            lengths[i] = 9;
        for (; i < 280; i++)
            lengths[i] = 7;
        for (; i < 288; i++)
            lengths[i] = 8;
        make_code(&literals, lengths, 288);
        for (i = 0; i < 30; i++)
            lengths[i] = 5;
        make_code(&distances, lengths, 30);
        built = 1;
    }
    return inflate_codes(z, &literals, &distances);
}

static int inflate_dynamic(INFLATE *z)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint32_t hlit, hdist, hclen;
    if (!take_bits(z, 5, &hlit) || !take_bits(z, 5, &hdist) || !take_bits(z, 4, &hclen))
        return 0;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 30)
        return 0;
    uint8_t lengths[320] = {};
    CODE lencode, distcode, litcode;
    for (unsigned i = 0; i < hclen; i++) {
        uint32_t v;
        if (!take_bits(z, 3, &v))
            return 0;
        lengths[order[i]] = (uint8_t)v;
    }
    if (make_code(&lencode, lengths, 19) != 0)
        return 0;
    unsigned index = 0;
    uint8_t all[320] = {};
    while (index < hlit + hdist) {
        int symbol = decode_symbol(z, &lencode);
        if (symbol < 0)
            return 0;
        if (symbol < 16) {
            all[index++] = (uint8_t)symbol;
            continue;
        }
        uint32_t repeat;
        uint8_t value = 0;
        if (symbol == 16) {
            if (!index || !take_bits(z, 2, &repeat))
                return 0;
            value = all[index - 1];
            repeat += 3;
        } else if (symbol == 17) {
            if (!take_bits(z, 3, &repeat))
                return 0;
            repeat += 3;
        } else {
            if (!take_bits(z, 7, &repeat))
                return 0;
            repeat += 11;
        }
        if (index + repeat > hlit + hdist)
            return 0;
        while (repeat--)
            all[index++] = value;
    }
    if (!all[256])
        return 0;
    int left = make_code(&litcode, all, hlit);
    if (left < 0 || (left > 0 && hlit - litcode.Count[0] != 1))
        return 0;
    left = make_code(&distcode, all + hlit, hdist);
    if (left < 0 || (left > 0 && hdist - distcode.Count[0] != 1))
        return 0;
    return inflate_codes(z, &litcode, &distcode);
}

/* an MSZIP block: "CK" then a deflate stream that ends with its final block. */
static int inflate_block(INFLATE *z, const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    if (in_size < 2 || in[0] != 'C' || in[1] != 'K')
        return 0;
    z->In = in + 2;
    z->InSize = in_size - 2;
    z->InPos = 0;
    z->Bits = 0;
    z->BitCount = 0;
    z->Out = out;
    z->OutSize = out_size;
    z->OutPos = 0;
    uint32_t last;
    do {
        uint32_t type;
        if (!take_bits(z, 1, &last) || !take_bits(z, 2, &type))
            return 0;
        int ok;
        if (type == 0) {
            z->Bits = 0;
            z->BitCount = 0;
            if (z->InPos + 4 > z->InSize)
                return 0;
            uint32_t length = word(z->In + z->InPos), inverse = word(z->In + z->InPos + 2);
            z->InPos += 4;
            if ((length ^ 0xFFFFu) != inverse || z->InPos + length > z->InSize)
                return 0;
            ok = 1;
            for (uint32_t i = 0; ok && i < length; i++)
                ok = put_byte(z, z->In[z->InPos++]);
        } else if (type == 1) {
            ok = inflate_fixed(z);
        } else if (type == 2) {
            ok = inflate_dynamic(z);
        } else {
            return 0;
        }
        if (!ok)
            return 0;
    } while (!last);
    return z->OutPos == out_size;
}

/* ------------------------------------------------------------------ LZX */

#define LZX_NUM_CHARS 256
#define LZX_MAX_SLOTS 50
#define LZX_MAIN_MAX (LZX_NUM_CHARS + LZX_MAX_SLOTS * 8)
#define LZX_LENGTH_SYMBOLS 249
#define LZX_ALIGNED_SYMBOLS 8
#define LZX_PRETREE_SYMBOLS 20
#define LZX_MIN_MATCH 2
#define FAST_BITS 10

#ifdef CAB_DEBUG
#include <stdio.h>
#define FAIL(n) (fprintf(stderr, "lzx failure %d\n", n), 0)
#else
#define FAIL(n) 0
#endif

enum { BLOCK_NONE = 0, BLOCK_VERBATIM = 1, BLOCK_ALIGNED = 2, BLOCK_UNCOMPRESSED = 3 };

typedef struct HUFFMAN {
    uint16_t Fast[1u << FAST_BITS]; /* symbol << 5 | length, 0 for longer codes. */
    uint32_t First[17];             /* first code of each length, left aligned. */
    uint16_t Count[17];
    uint16_t Offset[17];
    uint16_t Sorted[LZX_MAIN_MAX];
    uint16_t Symbols;
} HUFFMAN;

typedef struct BITS {
    const uint8_t *Start, *Pointer, *End;
    uint64_t Value;
    int Available;
} BITS;

typedef struct LZX {
    uint8_t *Window;
    uint32_t WindowSize, Position, Frame;
    uint32_t Slots;
    uint32_t Repeat[3];
    int BlockType;
    uint32_t BlockRemaining;
    int RawOdd, PadPending;
    int HeaderRead;
    uint32_t TranslationSize, Translated;
    uint64_t Produced;
    uint8_t MainLengths[LZX_MAIN_MAX], LengthLengths[LZX_LENGTH_SYMBOLS];
    HUFFMAN Main, Length, Aligned;
    BITS Bits;
    uint8_t Output[FRAME_SIZE];
} LZX;

static const uint8_t position_extra[LZX_MAX_SLOTS] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17};

static uint32_t position_base[LZX_MAX_SLOTS];

static void init_position_base(void)
{
    if (position_base[1])
        return;
    uint32_t base = 0;
    for (unsigned i = 0; i < LZX_MAX_SLOTS; i++) {
        position_base[i] = base;
        base += 1u << position_extra[i];
    }
}

static void bits_start(BITS *b, const uint8_t *data, size_t size)
{
    b->Start = b->Pointer = data;
    b->End = data + size;
    b->Value = 0;
    b->Available = 0;
}

/* little endian 16-bit words, bits taken from the top; reads zeros past the end. */
static void bits_fill(BITS *b)
{
    while (b->Available <= 48) {
        uint32_t w = 0;
        if (b->Pointer + 1 < b->End)
            w = word(b->Pointer);
        else if (b->Pointer < b->End)
            w = b->Pointer[0];
        b->Pointer += 2;
        b->Value |= (uint64_t)w << (48 - b->Available);
        b->Available += 16;
    }
}

static uint32_t bits_peek(BITS *b, unsigned n)
{
    return n ? (uint32_t)(b->Value >> (64 - n)) : 0;
}

static void bits_skip(BITS *b, unsigned n)
{
    b->Value <<= n;
    b->Available -= (int)n;
}

static uint32_t bits_read(BITS *b, unsigned n)
{
    bits_fill(b);
    uint32_t v = bits_peek(b, n);
    bits_skip(b, n);
    return v;
}

/* the first unread input byte, for uncompressed blocks. */
static const uint8_t *bits_position(const BITS *b)
{
    return b->Pointer - b->Available / 8;
}

static void huffman_clear(HUFFMAN *h, unsigned symbols)
{
    memset(h, 0, sizeof(*h));
    h->Symbols = (uint16_t)symbols;
}

/* returns 0 when usable: oversubscribed codes are refused, incomplete ones accepted. */
static int huffman_build(HUFFMAN *h, const uint8_t *lengths, unsigned symbols)
{
    huffman_clear(h, symbols);
    for (unsigned i = 0; i < symbols; i++) {
        if (lengths[i] > 16)
            return -1;
        h->Count[lengths[i]]++;
    }
    h->Count[0] = 0;
    uint32_t code = 0;
    unsigned offset = 0;
    for (unsigned length = 1; length <= 16; length++) {
        h->First[length] = code;
        h->Offset[length] = (uint16_t)offset;
        offset += h->Count[length];
        code = (code + h->Count[length]) << 1;
        if (code > (2u << length))
            return -1;
    }
    uint16_t next[17];
    for (unsigned length = 1; length <= 16; length++)
        next[length] = h->Offset[length];
    for (unsigned i = 0; i < symbols; i++)
        if (lengths[i])
            h->Sorted[next[lengths[i]]++] = (uint16_t)i;
    for (unsigned length = 1; length <= FAST_BITS; length++) {
        for (unsigned k = 0; k < h->Count[length]; k++) {
            uint32_t c = h->First[length] + k;
            uint32_t base = c << (FAST_BITS - length);
            uint16_t symbol = h->Sorted[h->Offset[length] + k];
            for (uint32_t fill = 0; fill < (1u << (FAST_BITS - length)); fill++)
                h->Fast[base + fill] = (uint16_t)(symbol << 5 | length);
        }
    }
    return 0;
}

/* -1 when the bits match no code. */
static int huffman_decode(HUFFMAN *h, BITS *b)
{
    bits_fill(b);
    uint16_t e = h->Fast[bits_peek(b, FAST_BITS)];
    if (e) {
        bits_skip(b, e & 31);
        return e >> 5;
    }
    for (unsigned length = FAST_BITS + 1; length <= 16; length++) {
        uint32_t c = bits_peek(b, length);
        if (h->Count[length] && c >= h->First[length] && c - h->First[length] < h->Count[length]) {
            bits_skip(b, length);
            return h->Sorted[h->Offset[length] + (c - h->First[length])];
        }
    }
    return -1;
}

/* lengths are coded as changes mod 17 from the previous block's, through a 20 symbol pretree that also codes runs. */
static int read_lengths(LZX *z, uint8_t *lengths, unsigned first, unsigned last)
{
    uint8_t pre[LZX_PRETREE_SYMBOLS];
    HUFFMAN tree;
    for (unsigned i = 0; i < LZX_PRETREE_SYMBOLS; i++)
        pre[i] = (uint8_t)bits_read(&z->Bits, 4);
    if (huffman_build(&tree, pre, LZX_PRETREE_SYMBOLS))
        return FAIL(7);
    for (unsigned x = first; x < last;) {
        int symbol = huffman_decode(&tree, &z->Bits);
        if (symbol < 0)
            return FAIL(8);
        if (symbol == 17 || symbol == 18) {
            unsigned run = symbol == 17 ? bits_read(&z->Bits, 4) + 4 : bits_read(&z->Bits, 5) + 20;
            if (x + run > last)
                return FAIL(9);
            while (run--)
                lengths[x++] = 0;
        } else if (symbol == 19) {
            /* a run of one length, coded as a change from the first symbol's previous length. */
            unsigned run = bits_read(&z->Bits, 1) + 4;
            int delta = huffman_decode(&tree, &z->Bits);
            if (delta < 0 || delta > 16 || x + run > last)
                return FAIL(10);
            uint8_t value = (uint8_t)((lengths[x] - delta + 17) % 17);
            while (run--)
                lengths[x++] = value;
        } else {
            lengths[x] = (uint8_t)((lengths[x] - symbol + 17) % 17);
            x++;
        }
    }
    return 1;
}

static int lzx_read_block_header(LZX *z)
{
    BITS *b = &z->Bits;
    int type = (int)bits_read(b, 3);
    uint32_t high = bits_read(b, 16), low = bits_read(b, 8);
    uint32_t size = high << 8 | low;
    if (type < BLOCK_VERBATIM || type > BLOCK_UNCOMPRESSED || !size)
        return FAIL(11);
    if (type == BLOCK_ALIGNED) {
        uint8_t lengths[LZX_ALIGNED_SYMBOLS];
        for (unsigned i = 0; i < LZX_ALIGNED_SYMBOLS; i++)
            lengths[i] = (uint8_t)bits_read(b, 3);
        if (huffman_build(&z->Aligned, lengths, LZX_ALIGNED_SYMBOLS))
            return FAIL(12);
    }
    if (type == BLOCK_ALIGNED || type == BLOCK_VERBATIM) {
        unsigned main_symbols = LZX_NUM_CHARS + z->Slots * 8;
        if (!read_lengths(z, z->MainLengths, 0, LZX_NUM_CHARS) ||
            !read_lengths(z, z->MainLengths, LZX_NUM_CHARS, main_symbols) ||
            !read_lengths(z, z->LengthLengths, 0, LZX_LENGTH_SYMBOLS))
            return FAIL(13);
        if (huffman_build(&z->Main, z->MainLengths, main_symbols) || huffman_build(&z->Length, z->LengthLengths, LZX_LENGTH_SYMBOLS))
            return FAIL(14);
        /* once 0xE8, the x86 call opcode, can be coded, frames are translated from then on. */
        if (z->MainLengths[0xE8] && z->TranslationSize)
            z->Translated = 1;
    } else {
        /* an uncompressed block is word aligned: one to sixteen bits of padding, which is a whole word when the header
           ended on a word, then the three recent offsets and the raw bytes. */
        unsigned padding = (unsigned)(b->Available % 16) ? (unsigned)(b->Available % 16) : 16;
        bits_fill(b);
        bits_skip(b, padding);
        const uint8_t *p = bits_position(b);
        if (p + 12 > b->End)
            return FAIL(15);
        z->Repeat[0] = dword(p);
        z->Repeat[1] = dword(p + 4);
        z->Repeat[2] = dword(p + 8);
        if (!z->Repeat[0] || !z->Repeat[1] || !z->Repeat[2])
            return FAIL(16);
        b->Pointer = p + 12;
        b->Value = 0;
        b->Available = 0;
        if (z->TranslationSize)
            z->Translated = 1;
    }
    z->BlockType = type;
    z->BlockRemaining = size;
    z->RawOdd = (size & 1) != 0;
    return 1;
}

static LZX *lzx_create(MIME_CAB *cab, unsigned window_bits)
{
    if (window_bits < 15 || window_bits > 21)
        return nullptr;
    init_position_base();
    LZX *z = get(cab, sizeof(*z));
    if (!z)
        return nullptr;
    memset(z, 0, sizeof(*z));
    z->WindowSize = 1u << window_bits;
    z->Window = get(cab, z->WindowSize);
    if (!z->Window) {
        put(cab, z);
        return nullptr;
    }
    memset(z->Window, 0, z->WindowSize);
    static const uint8_t slots[7] = {30, 32, 34, 36, 38, 42, 50};
    z->Slots = slots[window_bits - 15];
    z->Repeat[0] = z->Repeat[1] = z->Repeat[2] = 1;
    return z;
}

/* undoes the E8 translation: the compressor made call targets absolute. */
static void lzx_translate(LZX *z, uint8_t *data, size_t size)
{
    if (size <= 10)
        return;
    int32_t position = (int32_t)(z->Produced - size);
    for (size_t i = 0; i < size - 10;) {
        if (data[i] != 0xE8) {
            i++;
            position++;
            continue;
        }
        int32_t absolute = (int32_t)dword(data + i + 1);
        if (absolute >= -position && absolute < (int32_t)z->TranslationSize) {
            int32_t relative = absolute >= 0 ? absolute - position : absolute + (int32_t)z->TranslationSize;
            data[i + 1] = (uint8_t)relative;
            data[i + 2] = (uint8_t)(relative >> 8);
            data[i + 3] = (uint8_t)(relative >> 16);
            data[i + 4] = (uint8_t)(relative >> 24);
        }
        i += 5;
        position += 5;
    }
}

/* decodes one frame of up to 32768 bytes. */
static int lzx_frame(LZX *z, const uint8_t *in, size_t in_size, size_t out_size, const uint8_t **out)
{
    BITS *b = &z->Bits;
    bits_start(b, in, in_size);
    /* the padding after a block that ended with its frame leads the next frame. */
    if (z->PadPending) {
        z->PadPending = 0;
        if (in_size)
            b->Pointer++;
    }
    if (!z->HeaderRead) {
        z->HeaderRead = 1;
        if (bits_read(b, 1)) {
            uint32_t high = bits_read(b, 16), low = bits_read(b, 16);
            z->TranslationSize = high << 16 | low;
        }
    }
    uint32_t mask = z->WindowSize - 1;
    uint32_t start = z->Position & mask;
    size_t todo = out_size;
    while (todo) {
        if (!z->BlockRemaining) {
            if (!lzx_read_block_header(z))
                return FAIL(1);
        }
        size_t run = z->BlockRemaining < todo ? z->BlockRemaining : todo;
        z->BlockRemaining -= (uint32_t)run;
        todo -= run;
        if (z->BlockType == BLOCK_UNCOMPRESSED) {
            const uint8_t *p = b->Pointer;
            if (p + run > b->End)
                return FAIL(2);
            for (size_t i = 0; i < run; i++)
                z->Window[z->Position++ & mask] = p[i];
            b->Pointer = p + run;
            /* an odd sized uncompressed block is padded to a word. */
            if (!z->BlockRemaining && z->RawOdd) {
                if (b->Pointer < b->End)
                    b->Pointer++;
                else
                    z->PadPending = 1;
            }
            continue;
        }
        while (run) {
            int symbol = huffman_decode(&z->Main, b);
            if (symbol < 0)
                return FAIL(3);
            if (symbol < LZX_NUM_CHARS) {
                z->Window[z->Position++ & mask] = (uint8_t)symbol;
                run--;
                continue;
            }
            symbol -= LZX_NUM_CHARS;
            uint32_t length = (uint32_t)symbol & 7;
            if (length == 7) {
                int footer = huffman_decode(&z->Length, b);
                if (footer < 0)
                    return FAIL(4);
                length += (uint32_t)footer;
            }
            length += LZX_MIN_MATCH;
            uint32_t slot = (uint32_t)symbol >> 3, offset;
            if (slot == 0) {
                offset = z->Repeat[0];
            } else if (slot == 1) {
                offset = z->Repeat[1];
                z->Repeat[1] = z->Repeat[0];
                z->Repeat[0] = offset;
            } else if (slot == 2) {
                offset = z->Repeat[2];
                z->Repeat[2] = z->Repeat[0];
                z->Repeat[0] = offset;
            } else {
                unsigned extra = position_extra[slot];
                uint32_t more;
                if (z->BlockType == BLOCK_ALIGNED && extra >= 3) {
                    more = extra > 3 ? bits_read(b, extra - 3) << 3 : 0;
                    int aligned = huffman_decode(&z->Aligned, b);
                    if (aligned < 0)
                        return FAIL(5);
                    more += (uint32_t)aligned;
                } else if (extra > 16) {
                    more = bits_read(b, extra - 16) << 16;
                    more |= bits_read(b, 16);
                } else {
                    more = bits_read(b, extra);
                }
                offset = position_base[slot] - 2 + more;
                z->Repeat[2] = z->Repeat[1];
                z->Repeat[1] = z->Repeat[0];
                z->Repeat[0] = offset;
            }
            /* a match must stay in its block and frame and not reach back before the data starts. */
            uint64_t have = z->Produced + (out_size - todo - run);
            if (length > run || offset > z->WindowSize || offset > have)
                return FAIL(6);
            for (uint32_t i = 0; i < length; i++) {
                z->Window[z->Position & mask] = z->Window[(z->Position - offset) & mask];
                z->Position++;
            }
            run -= length;
        }
    }
    for (size_t i = 0; i < out_size; i++)
        z->Output[i] = z->Window[(start + i) & mask];
    z->Produced += out_size;
    z->Frame++;
    if (z->TranslationSize && z->Translated && z->Frame <= 32768)
        lzx_translate(z, z->Output, out_size);
    *out = z->Output;
    return 1;
}

/* ------------------------------------------------------------------ folders */

int MimeCabFolderRead(MIME_CAB *cab, unsigned folder, MIME_CAB_SINK sink, void *context)
{
    if (folder >= cab->Folders)
        return MIME_CAB_BAD;
    const FOLDER *f = &cab->Folder[folder];
    unsigned method = f->Type & 0xF;
    if (method == TYPE_QUANTUM || method > TYPE_LZX)
        return MIME_CAB_UNSUPPORTED;
    INFLATE *inflate = nullptr;
    LZX *lzx = nullptr;
    uint8_t *frame = nullptr;
    uint8_t *scratch = nullptr;
    int result = MIME_CAB_OK;
    if (cab->Read) {
        scratch = get(cab, CFDATA_SIZE + 255 + 65535);
        if (!scratch)
            return MIME_CAB_MEMORY;
    }
    if (method == TYPE_MSZIP) {
        inflate = get(cab, sizeof(*inflate));
        frame = get(cab, FRAME_SIZE);
        if (!inflate || !frame) {
            result = MIME_CAB_MEMORY;
            goto done;
        }
        memset(inflate, 0, sizeof(*inflate));
    } else if (method == TYPE_LZX) {
        lzx = lzx_create(cab, (f->Type >> 8) & 0x1F);
        if (!lzx) {
            result = ((f->Type >> 8) & 0x1F) < 15 || ((f->Type >> 8) & 0x1F) > 21 ? MIME_CAB_UNSUPPORTED : MIME_CAB_MEMORY;
            goto done;
        }
    }
    size_t at = f->Start;
    uint32_t offset = 0;
    for (unsigned block = 0; block < f->Blocks; block++) {
        if (at + CFDATA_SIZE + cab->DataReserve > cab->Size) {
            result = MIME_CAB_BAD;
            goto done;
        }
        const uint8_t *header = cab->Read ? scratch : cab->Data + at;
        if (cab->Read && cab->Read(cab->ReadContext, at, scratch, CFDATA_SIZE + cab->DataReserve)) {
            result = MIME_CAB_BAD;
            goto done;
        }
        uint32_t sum = dword(header);
        uint16_t compressed = word(header + 4), plain = word(header + 6);
        size_t data_at = at + CFDATA_SIZE + cab->DataReserve;
        if (data_at + compressed > cab->Size || plain > FRAME_SIZE) {
            result = MIME_CAB_BAD;
            goto done;
        }
        const uint8_t *data = cab->Read ? scratch + CFDATA_SIZE + cab->DataReserve : cab->Data + data_at;
        if (cab->Read && cab->Read(cab->ReadContext, data_at, scratch + CFDATA_SIZE + cab->DataReserve, compressed)) {
            result = MIME_CAB_BAD;
            goto done;
        }
        if (sum) {
            uint32_t check = checksum(data, compressed, 0);
            check = checksum(header + 4, 4, check);
            if (check != sum) {
#ifdef CAB_DEBUG
                fprintf(stderr, "checksum folder %u block %u at %zu: %08x vs %08x\n", folder, block, at, check, sum);
#endif
                result = MIME_CAB_CORRUPT;
                goto done;
            }
        }
        const uint8_t *produced = nullptr;
        if (method == TYPE_NONE) {
            if (compressed != plain) {
                result = MIME_CAB_CORRUPT;
                goto done;
            }
            produced = data;
        } else if (method == TYPE_MSZIP) {
            if (!inflate_block(inflate, data, compressed, frame, plain)) {
                result = MIME_CAB_CORRUPT;
                goto done;
            }
            produced = frame;
        } else if (!lzx_frame(lzx, data, compressed, plain, &produced)) {
            result = MIME_CAB_CORRUPT;
            goto done;
        }
        if (plain && sink(context, offset, produced, plain)) {
            result = MIME_CAB_STOPPED;
            goto done;
        }
        offset += plain;
        at = data_at + compressed;
    }
done:
    if (lzx) {
        put(cab, lzx->Window);
        put(cab, lzx);
    }
    put(cab, inflate);
    put(cab, frame);
    put(cab, scratch);
    return result;
}

/* files begin in offset order and may overlap or share data under two names, so several can be open at once. */
typedef struct EXTRACT {
    MIME_CAB *Cab;
    const MIME_CAB_FILE_SINK *Sink;
    void *Context;
    uint16_t *Order;  /* file indexes sorted by offset. */
    uint16_t *Active; /* positions in Order of the files being written. */
    void **Contexts;  /* indexed by position in Order. */
    uint32_t *Written;
    unsigned Count, Next, ActiveCount;
    int Failed, Corrupt;
} EXTRACT;

static int extract_finish(EXTRACT *e, unsigned position_in_order)
{
    MIME_CAB_FILE file;
    MimeCabFile(e->Cab, e->Order[position_in_order], &file);
    if (e->Contexts[position_in_order] && e->Sink->End(e->Context, e->Contexts[position_in_order], &file)) {
        e->Failed = 1;
        return 1;
    }
    return 0;
}

/* in sequential mode a file waits for the open ones unless it starts inside one of them. */
static int extract_waits(const EXTRACT *e, const MIME_CAB_FILE *file)
{
    if (!e->Cab->Sequential)
        return 0;
    for (unsigned i = 0; i < e->ActiveCount; i++) {
        MIME_CAB_FILE active;
        MimeCabFile(e->Cab, e->Order[e->Active[i]], &active);
        if (file->Offset < (uint64_t)active.Offset + active.Size)
            return 0;
    }
    return e->ActiveCount != 0;
}

static int extract_piece(void *context, uint32_t offset, const uint8_t *data, size_t length)
{
    EXTRACT *e = context;
    uint64_t end = (uint64_t)offset + length;
    MIME_CAB_FILE file;
    for (;;) {
        /* begin the files that start before this piece ends, one at a time in sequential mode. */
        while (e->Next < e->Count) {
            MimeCabFile(e->Cab, e->Order[e->Next], &file);
            if ((file.Offset >= end && file.Size) || extract_waits(e, &file))
                break;
            e->Contexts[e->Next] = e->Sink->Begin(e->Context, &file);
            e->Written[e->Next] = 0;
            if (file.Size == 0) {
                if (extract_finish(e, e->Next))
                    return 1;
            } else {
                e->Active[e->ActiveCount++] = (uint16_t)e->Next;
            }
            e->Next++;
        }
        for (unsigned i = 0; i < e->ActiveCount;) {
            unsigned at = e->Active[i];
            MimeCabFile(e->Cab, e->Order[at], &file);
            uint64_t next = (uint64_t)file.Offset + e->Written[at], file_end = (uint64_t)file.Offset + file.Size;
            if (next < offset)
                next = offset;
            /* its data already went by, so it cannot be written whole. */
            if (next > file_end) {
                e->Corrupt = 1;
                return 1;
            }
            if (next < end) {
                uint64_t take = file_end - next < end - next ? file_end - next : end - next;
                if (e->Contexts[at] && take &&
                    e->Sink->Write(e->Context, e->Contexts[at], data + (next - offset), (size_t)take)) {
                    e->Failed = 1;
                    return 1;
                }
                e->Written[at] = (uint32_t)(next + take - file.Offset);
            }
            if (e->Written[at] >= file.Size) {
                e->Active[i] = e->Active[--e->ActiveCount];
                if (extract_finish(e, at))
                    return 1;
                continue;
            }
            i++;
        }
        /* a file waiting on the previous one may begin in this piece. */
        if (!e->Cab->Sequential || e->Next >= e->Count)
            return 0;
        MimeCabFile(e->Cab, e->Order[e->Next], &file);
        if ((file.Offset >= end && file.Size) || extract_waits(e, &file))
            return 0;
    }
}

int MimeCabExtract(MIME_CAB *cab, const MIME_CAB_FILE_SINK *sink, void *context)
{
    for (unsigned i = 0; i < cab->Files; i++) {
        MIME_CAB_FILE file;
        MimeCabFile(cab, i, &file);
        if (file.Folder >= FOLDER_CONTINUED_FROM_PREV) {
            if (cab->SkipContinued)
                continue;
            return MIME_CAB_UNSUPPORTED;
        }
        if (file.Folder >= cab->Folders)
            return MIME_CAB_BAD;
    }
    unsigned files = cab->Files ? cab->Files : 1u;
    uint16_t *order = get(cab, sizeof(uint16_t) * files * 2);
    void **contexts = get(cab, sizeof(void *) * files);
    uint32_t *written = get(cab, sizeof(uint32_t) * files);
    if (!order || !contexts || !written) {
        put(cab, order);
        put(cab, contexts);
        put(cab, written);
        return MIME_CAB_MEMORY;
    }
    int result = MIME_CAB_OK;
    for (unsigned folder = 0; folder < cab->Folders && result == MIME_CAB_OK; folder++) {
        EXTRACT e = {};
        e.Cab = cab;
        e.Sink = sink;
        e.Context = context;
        e.Order = order;
        e.Active = order + files;
        e.Contexts = contexts;
        e.Written = written;
        for (unsigned i = 0; i < cab->Files; i++) {
            MIME_CAB_FILE file, other;
            MimeCabFile(cab, i, &file);
            if (file.Folder != folder)
                continue;
            /* inserted after the files starting no later, so equal offsets keep stored order. */
            unsigned at = e.Count++;
            while (at > 0) {
                MimeCabFile(cab, order[at - 1], &other);
                if (other.Offset <= file.Offset)
                    break;
                order[at] = order[at - 1];
                at--;
            }
            order[at] = (uint16_t)i;
        }
        if (!e.Count)
            continue;
        int r = MimeCabFolderRead(cab, folder, extract_piece, &e);
        if (r == MIME_CAB_STOPPED && e.Corrupt) {
            result = MIME_CAB_CORRUPT;
            break;
        }
        if (r == MIME_CAB_STOPPED && e.Failed) {
            result = MIME_CAB_STOPPED;
            break;
        }
        if (r != MIME_CAB_OK) {
            result = r;
            break;
        }
        /* empty files at the very end get no piece; any file still open or waiting had no data. */
        MIME_CAB_FILE file;
        while (e.Next < e.Count) {
            MimeCabFile(cab, order[e.Next], &file);
            if (file.Size) {
                result = MIME_CAB_CORRUPT;
                break;
            }
            contexts[e.Next] = sink->Begin(context, &file);
            if (extract_finish(&e, e.Next)) {
                result = MIME_CAB_STOPPED;
                break;
            }
            e.Next++;
        }
        if (result == MIME_CAB_OK && e.ActiveCount)
            result = MIME_CAB_CORRUPT;
    }
    put(cab, order);
    put(cab, contexts);
    put(cab, written);
    return result;
}
