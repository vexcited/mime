/* 7z archives: the container of the .NET Framework offline installer. */
#include "stream.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

/* ------------------------------------------------------------------ streams */

void CbsStreamDestroy(CBS_STREAM *stream)
{
    if (stream)
        stream->Destroy(stream);
}

static int slice_read(CBS_STREAM *stream, uint8_t *buffer, size_t want, size_t *got)
{
    /* the position is kept in the first bytes after the base, see CbsStreamSlice. */
    size_t *position = (size_t *)(stream + 1);
    size_t left = stream->Size - *position;
    if (want > left)
        want = left;
    memcpy(buffer, stream->Data + *position, want);
    *position += want;
    *got = want;
    return CBS_OK;
}

static void slice_destroy(CBS_STREAM *stream)
{
    CbsFree(stream);
}

int CbsStreamSlice(CBS_STREAM **out, const uint8_t *data, size_t size)
{
    CBS_STREAM *s = CbsAllocZero(sizeof(CBS_STREAM) + sizeof(size_t));
    if (!s)
        return CBS_MEMORY;
    s->Read = slice_read;
    s->Destroy = slice_destroy;
    s->Data = data;
    s->Size = size;
    *out = s;
    return CBS_OK;
}

/* ------------------------------------------------------------------ the header */

enum {
    K_END = 0x00,
    K_HEADER = 0x01,
    K_ARCHIVE_PROPERTIES = 0x02,
    K_ADDITIONAL_STREAMS = 0x03,
    K_MAIN_STREAMS = 0x04,
    K_FILES_INFO = 0x05,
    K_PACK_INFO = 0x06,
    K_UNPACK_INFO = 0x07,
    K_SUBSTREAMS_INFO = 0x08,
    K_SIZE = 0x09,
    K_CRC = 0x0A,
    K_FOLDER = 0x0B,
    K_CODERS_UNPACK_SIZE = 0x0C,
    K_NUM_UNPACK_STREAM = 0x0D,
    K_EMPTY_STREAM = 0x0E,
    K_EMPTY_FILE = 0x0F,
    K_NAMES = 0x11,
    K_ENCODED_HEADER = 0x17,
};

#define MAX_CODERS 8
#define MAX_STREAMS 32

typedef struct CODER {
    uint32_t Method; /* the id bytes read as a big endian number. */
    unsigned NumIn, NumOut;
    unsigned PropSize;
    uint8_t Props[8];
} CODER;

typedef struct FOLDER {
    CODER Coder[MAX_CODERS];
    unsigned CoderCount;
    unsigned BindIn[MAX_CODERS * 2], BindOut[MAX_CODERS * 2];
    unsigned BindCount;
    unsigned Packed[MAX_STREAMS];
    unsigned PackedCount;
    unsigned FirstPack; /* index of its first packed stream. */
    unsigned OutTotal, InTotal;
    uint64_t UnpackSize[MAX_STREAMS]; /* per out stream. */
    uint32_t Crc;
    bool HasCrc;
    unsigned SubCount;
    unsigned FirstSub; /* index in the substream tables. */
} FOLDER;

typedef struct STREAMS_INFO {
    uint64_t PackPosition;
    unsigned PackCount;
    uint64_t *PackSize;
    FOLDER *Folder;
    unsigned FolderCount;
    unsigned SubTotal;
    uint64_t *SubSize;
    uint32_t *SubCrc;
    bool *SubHasCrc;
} STREAMS_INFO;

typedef struct READER {
    const uint8_t *Cursor, *End;
    bool Bad;
} READER;

static uint8_t r_byte(READER *r)
{
    if (r->Cursor >= r->End) {
        r->Bad = true;
        return 0;
    }
    return *r->Cursor++;
}

static uint32_t r_dword(READER *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= (uint32_t)r_byte(r) << (8 * i);
    return v;
}

static uint64_t r_number(READER *r)
{
    uint8_t first = r_byte(r);
    uint8_t mask = 0x80;
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (!(first & mask)) {
            value += (uint64_t)(first & (mask - 1u)) << (8 * i);
            return value;
        }
        value |= (uint64_t)r_byte(r) << (8 * i);
        mask >>= 1;
    }
    return value;
}

/* a count that has to fit in what remains: each item needs at least a byte. */
static unsigned r_count(READER *r, unsigned limit)
{
    uint64_t n = r_number(r);
    if (n > limit) {
        r->Bad = true;
        return 0;
    }
    return (unsigned)n;
}

static void r_skip(READER *r, uint64_t n)
{
    if (n > (uint64_t)(r->End - r->Cursor)) {
        r->Bad = true;
        r->Cursor = r->End;
    } else
        r->Cursor += n;
}

/* a vector of bits, most significant first. */
static bool *r_bits(READER *r, unsigned count)
{
    bool *bits = CbsAllocZero(count ? count : 1);
    if (!bits) {
        r->Bad = true;
        return nullptr;
    }
    uint8_t byte = 0, mask = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!mask) {
            byte = r_byte(r);
            mask = 0x80;
        }
        bits[i] = (byte & mask) != 0;
        mask >>= 1;
    }
    return bits;
}

/* an "all defined" byte then, when it is 0, a bit vector. */
static bool *r_defined(READER *r, unsigned count)
{
    uint8_t all = r_byte(r);
    if (!all)
        return r_bits(r, count);
    bool *bits = CbsAllocZero(count ? count : 1);
    if (!bits) {
        r->Bad = true;
        return nullptr;
    }
    for (unsigned i = 0; i < count; i++)
        bits[i] = true;
    return bits;
}

static void streams_free(STREAMS_INFO *s)
{
    CbsFree(s->PackSize);
    CbsFree(s->Folder);
    CbsFree(s->SubSize);
    CbsFree(s->SubCrc);
    CbsFree(s->SubHasCrc);
    memset(s, 0, sizeof(*s));
}

static bool read_folder(READER *r, FOLDER *f)
{
    memset(f, 0, sizeof(*f));
    f->CoderCount = r_count(r, MAX_CODERS);
    if (!f->CoderCount)
        return false;
    for (unsigned i = 0; i < f->CoderCount; i++) {
        CODER *c = &f->Coder[i];
        uint8_t flags = r_byte(r);
        unsigned id_size = flags & 0x0F;
        if ((flags & 0x80) || id_size > 4)
            return false;
        uint32_t method = 0;
        for (unsigned k = 0; k < id_size; k++)
            method = method << 8 | r_byte(r);
        c->Method = method;
        c->NumIn = c->NumOut = 1;
        if (flags & 0x10) {
            c->NumIn = r_count(r, 4);
            c->NumOut = r_count(r, 4);
        }
        if (flags & 0x20) {
            uint64_t n = r_number(r);
            if (n > sizeof(c->Props)) {
                /* larger properties are of no coder that is supported. */
                r_skip(r, n);
                c->PropSize = 0;
            } else {
                c->PropSize = (unsigned)n;
                for (unsigned k = 0; k < c->PropSize; k++)
                    c->Props[k] = r_byte(r);
            }
        }
        f->InTotal += c->NumIn;
        f->OutTotal += c->NumOut;
    }
    if (r->Bad || !f->OutTotal || f->OutTotal > MAX_STREAMS || f->InTotal > MAX_STREAMS)
        return false;
    f->BindCount = f->OutTotal - 1;
    if (f->InTotal < f->BindCount)
        return false;
    for (unsigned i = 0; i < f->BindCount; i++) {
        f->BindIn[i] = r_count(r, MAX_STREAMS);
        f->BindOut[i] = r_count(r, MAX_STREAMS);
    }
    f->PackedCount = f->InTotal - f->BindCount;
    if (f->PackedCount == 1) {
        unsigned in = 0;
        for (; in < f->InTotal; in++) {
            bool bound = false;
            for (unsigned k = 0; k < f->BindCount; k++)
                bound |= f->BindIn[k] == in;
            if (!bound)
                break;
        }
        if (in == f->InTotal)
            return false;
        f->Packed[0] = in;
    } else {
        for (unsigned i = 0; i < f->PackedCount; i++)
            f->Packed[i] = r_count(r, MAX_STREAMS);
    }
    return !r->Bad;
}

/* the index of the out stream that no bind pair consumes: the folder's result. */
static unsigned folder_result(const FOLDER *f)
{
    for (unsigned out = 0; out < f->OutTotal; out++) {
        bool bound = false;
        for (unsigned k = 0; k < f->BindCount; k++)
            bound |= f->BindOut[k] == out;
        if (!bound)
            return out;
    }
    return 0;
}

static int read_streams(READER *r, STREAMS_INFO *s)
{
    memset(s, 0, sizeof(*s));
    unsigned first_pack = 0;
    for (;;) {
        uint8_t id = r_byte(r);
        if (r->Bad)
            return CBS_BAD;
        if (id == K_END)
            break;
        if (id == K_PACK_INFO) {
            s->PackPosition = r_number(r);
            s->PackCount = r_count(r, 0x10000);
            s->PackSize = CbsAllocZero(sizeof(uint64_t) * (s->PackCount + 1));
            if (!s->PackSize)
                return CBS_MEMORY;
            for (;;) {
                uint8_t pid = r_byte(r);
                if (r->Bad)
                    return CBS_BAD;
                if (pid == K_END)
                    break;
                if (pid == K_SIZE) {
                    for (unsigned i = 0; i < s->PackCount; i++)
                        s->PackSize[i] = r_number(r);
                } else if (pid == K_CRC) {
                    bool *defined = r_defined(r, s->PackCount);
                    for (unsigned i = 0; i < s->PackCount; i++)
                        if (defined && defined[i])
                            r_dword(r);
                    CbsFree(defined);
                } else
                    return CBS_UNSUPPORTED;
            }
        } else if (id == K_UNPACK_INFO) {
            if (r_byte(r) != K_FOLDER)
                return CBS_BAD;
            s->FolderCount = r_count(r, 0x10000);
            if (r_byte(r) != 0)
                return CBS_UNSUPPORTED;
            s->Folder = CbsAllocZero(sizeof(FOLDER) * (s->FolderCount + 1));
            if (!s->Folder)
                return CBS_MEMORY;
            for (unsigned i = 0; i < s->FolderCount; i++) {
                if (!read_folder(r, &s->Folder[i]))
                    return CBS_BAD;
                s->Folder[i].FirstPack = first_pack;
                first_pack += s->Folder[i].PackedCount;
                s->Folder[i].SubCount = 1;
            }
            if (r_byte(r) != K_CODERS_UNPACK_SIZE)
                return CBS_BAD;
            for (unsigned i = 0; i < s->FolderCount; i++)
                for (unsigned k = 0; k < s->Folder[i].OutTotal; k++)
                    s->Folder[i].UnpackSize[k] = r_number(r);
            for (;;) {
                uint8_t uid = r_byte(r);
                if (r->Bad)
                    return CBS_BAD;
                if (uid == K_END)
                    break;
                if (uid == K_CRC) {
                    bool *defined = r_defined(r, s->FolderCount);
                    for (unsigned i = 0; i < s->FolderCount; i++)
                        if (defined && defined[i]) {
                            s->Folder[i].Crc = r_dword(r);
                            s->Folder[i].HasCrc = true;
                        }
                    CbsFree(defined);
                } else
                    return CBS_UNSUPPORTED;
            }
        } else if (id == K_SUBSTREAMS_INFO) {
            /* the counts first, then sizes and digests of what the folders do not already give. */
            uint8_t sid = r_byte(r);
            if (sid == K_NUM_UNPACK_STREAM) {
                for (unsigned i = 0; i < s->FolderCount; i++)
                    s->Folder[i].SubCount = r_count(r, 0x1000000);
                sid = r_byte(r);
            }
            unsigned total = 0;
            for (unsigned i = 0; i < s->FolderCount; i++) {
                s->Folder[i].FirstSub = total;
                total += s->Folder[i].SubCount;
            }
            s->SubTotal = total;
            s->SubSize = CbsAllocZero(sizeof(uint64_t) * (total + 1));
            s->SubCrc = CbsAllocZero(sizeof(uint32_t) * (total + 1));
            s->SubHasCrc = CbsAllocZero(total + 1);
            if (!s->SubSize || !s->SubCrc || !s->SubHasCrc)
                return CBS_MEMORY;
            for (unsigned i = 0; i < s->FolderCount; i++) {
                FOLDER *f = &s->Folder[i];
                uint64_t whole = f->UnpackSize[folder_result(f)];
                if (!f->SubCount)
                    continue;
                if (sid == K_SIZE) {
                    uint64_t sum = 0;
                    for (unsigned k = 0; k + 1 < f->SubCount; k++) {
                        s->SubSize[f->FirstSub + k] = r_number(r);
                        sum += s->SubSize[f->FirstSub + k];
                    }
                    if (sum > whole)
                        return CBS_BAD;
                    s->SubSize[f->FirstSub + f->SubCount - 1] = whole - sum;
                } else if (f->SubCount == 1)
                    s->SubSize[f->FirstSub] = whole;
                else
                    return CBS_BAD;
            }
            if (sid == K_SIZE)
                sid = r_byte(r);
            /* digests: folders with one substream and a folder crc already have theirs. */
            unsigned unknown = 0;
            for (unsigned i = 0; i < s->FolderCount; i++) {
                FOLDER *f = &s->Folder[i];
                if (f->SubCount == 1 && f->HasCrc) {
                    s->SubCrc[f->FirstSub] = f->Crc;
                    s->SubHasCrc[f->FirstSub] = true;
                } else
                    unknown += f->SubCount;
            }
            while (sid != K_END) {
                if (r->Bad)
                    return CBS_BAD;
                if (sid == K_CRC) {
                    bool *defined = r_defined(r, unknown);
                    unsigned at = 0;
                    for (unsigned i = 0; i < s->FolderCount; i++) {
                        FOLDER *f = &s->Folder[i];
                        if (f->SubCount == 1 && f->HasCrc)
                            continue;
                        for (unsigned k = 0; k < f->SubCount; k++, at++)
                            if (defined && defined[at]) {
                                s->SubCrc[f->FirstSub + k] = r_dword(r);
                                s->SubHasCrc[f->FirstSub + k] = true;
                            }
                    }
                    CbsFree(defined);
                } else
                    return CBS_UNSUPPORTED;
                sid = r_byte(r);
            }
        } else
            return CBS_UNSUPPORTED;
    }
    if (!s->SubSize) {
        /* no substream table: one stream per folder. */
        s->SubTotal = s->FolderCount;
        s->SubSize = CbsAllocZero(sizeof(uint64_t) * (s->SubTotal + 1));
        s->SubCrc = CbsAllocZero(sizeof(uint32_t) * (s->SubTotal + 1));
        s->SubHasCrc = CbsAllocZero(s->SubTotal + 1);
        if (!s->SubSize || !s->SubCrc || !s->SubHasCrc)
            return CBS_MEMORY;
        for (unsigned i = 0; i < s->FolderCount; i++) {
            s->Folder[i].FirstSub = i;
            s->Folder[i].SubCount = 1;
            s->SubSize[i] = s->Folder[i].UnpackSize[folder_result(&s->Folder[i])];
            s->SubCrc[i] = s->Folder[i].Crc;
            s->SubHasCrc[i] = s->Folder[i].HasCrc;
        }
    }
    return r->Bad ? CBS_BAD : CBS_OK;
}

/* ------------------------------------------------------------------ the archive */

struct CBS_7Z {
    const uint8_t *Data;
    size_t Size;
    uint8_t *HeaderCopy; /* the decoded header when it was stored compressed. */
    STREAMS_INFO Streams;
    unsigned FileCount;
    char **Name;
    bool *HasStream; /* per file. */
    unsigned *StreamIndex; /* per file with a stream, its index in the substream tables, else ~0u. */
    unsigned *StreamFolder;
};

static int decode_folder_to_memory(const CBS_7Z *z, const STREAMS_INFO *s, unsigned folder, uint8_t **out, size_t *size);

static uint64_t pack_offset(const CBS_7Z *z, const STREAMS_INFO *s, unsigned pack_index)
{
    uint64_t at = 32 + s->PackPosition;
    for (unsigned i = 0; i < pack_index; i++)
        at += s->PackSize[i];
    (void)z;
    return at;
}

/* builds the reader of one out stream of a folder, and of the readers it takes input from. */
static int build_stream(const CBS_7Z *z, const STREAMS_INFO *s, const FOLDER *f, unsigned out_index,
                        CBS_STREAM **result)
{
    *result = nullptr;
    unsigned in_base = 0, out_base = 0, coder_index = 0;
    for (; coder_index < f->CoderCount; coder_index++) {
        const CODER *c = &f->Coder[coder_index];
        if (out_index < out_base + c->NumOut)
            break;
        in_base += c->NumIn;
        out_base += c->NumOut;
    }
    if (coder_index == f->CoderCount)
        return CBS_BAD;
    const CODER *c = &f->Coder[coder_index];
    if (c->NumOut != 1)
        return CBS_UNSUPPORTED;
    CBS_STREAM *inputs[4] = {};
    if (c->NumIn > 4)
        return CBS_UNSUPPORTED;
    int status = CBS_OK;
    for (unsigned i = 0; i < c->NumIn && status == CBS_OK; i++) {
        unsigned in_index = in_base + i;
        unsigned bound = ~0u;
        for (unsigned k = 0; k < f->BindCount; k++)
            if (f->BindIn[k] == in_index)
                bound = f->BindOut[k];
        if (bound != ~0u) {
            status = build_stream(z, s, f, bound, &inputs[i]);
            continue;
        }
        unsigned pack = ~0u;
        for (unsigned k = 0; k < f->PackedCount; k++)
            if (f->Packed[k] == in_index)
                pack = k;
        if (pack == ~0u || f->FirstPack + pack >= s->PackCount) {
            status = CBS_BAD;
            break;
        }
        uint64_t at = pack_offset(z, s, f->FirstPack + pack);
        uint64_t length = s->PackSize[f->FirstPack + pack];
        if (at > z->Size || length > z->Size - at) {
            status = CBS_BAD;
            break;
        }
        status = CbsStreamSlice(&inputs[i], z->Data + at, (size_t)length);
    }
    if (status == CBS_OK) {
        uint64_t size = f->UnpackSize[out_index];
        switch (c->Method) {
        case 0x00:
            *result = inputs[0];
            inputs[0] = nullptr;
            break;
        case 0x030101:
            status = CbsStreamLzma(result, c->Props, c->PropSize, inputs[0], size);
            if (status == CBS_OK)
                inputs[0] = nullptr;
            break;
        case 0x0303011B:
            if (c->NumIn != 4) {
                status = CBS_BAD;
                break;
            }
            status = CbsStreamBcj2(result, inputs, size);
            if (status == CBS_OK)
                for (int i = 0; i < 4; i++)
                    inputs[i] = nullptr;
            break;
        default:
            status = CBS_UNSUPPORTED;
        }
    }
    for (int i = 0; i < 4; i++)
        CbsStreamDestroy(inputs[i]);
    return status;
}

/* reads and discards, or hands to the sink, the folder's result. */
typedef struct EXTRACT {
    uint64_t Skip, Take;
    uint32_t Crc;
    CBS_SINK Sink;
    void *Context;
} EXTRACT;

static int run_folder(const CBS_7Z *z, const STREAMS_INFO *s, unsigned folder, EXTRACT *e)
{
    const FOLDER *f = &s->Folder[folder];
    CBS_STREAM *stream;
    int status = build_stream(z, s, f, folder_result(f), &stream);
    if (status != CBS_OK)
        return status;
    uint8_t *buffer = CbsAlloc(1u << 18);
    if (!buffer) {
        CbsStreamDestroy(stream);
        return CBS_MEMORY;
    }
    while (e->Skip || e->Take) {
        uint64_t want = e->Skip ? e->Skip : e->Take;
        if (want > (1u << 18))
            want = 1u << 18;
        size_t got = 0;
        status = stream->Read(stream, buffer, (size_t)want, &got);
        if (status != CBS_OK)
            break;
        if (!got) {
            status = CBS_CORRUPT;
            break;
        }
        if (e->Skip) {
            e->Skip -= got;
            continue;
        }
        e->Take -= got;
        e->Crc = CbsCrc32(e->Crc, buffer, got);
        if (e->Sink && e->Sink(e->Context, buffer, got)) {
            status = CBS_STOPPED;
            break;
        }
    }
    CbsFree(buffer);
    CbsStreamDestroy(stream);
    return status;
}

static int decode_folder_to_memory(const CBS_7Z *z, const STREAMS_INFO *s, unsigned folder, uint8_t **out, size_t *size)
{
    const FOLDER *f = &s->Folder[folder];
    uint64_t whole = f->UnpackSize[folder_result(f)];
    if (whole > 0x40000000u)
        return CBS_UNSUPPORTED;
    uint8_t *data = CbsAlloc((size_t)whole + 1);
    if (!data)
        return CBS_MEMORY;
    CBS_STREAM *stream;
    int status = build_stream(z, s, f, folder_result(f), &stream);
    if (status == CBS_OK) {
        size_t have = 0;
        while (have < whole && status == CBS_OK) {
            size_t got = 0;
            status = stream->Read(stream, data + have, (size_t)whole - have, &got);
            if (status == CBS_OK && !got)
                status = CBS_CORRUPT;
            have += got;
        }
        CbsStreamDestroy(stream);
        if (status == CBS_OK && f->HasCrc && CbsCrc32(0, data, (size_t)whole) != f->Crc)
            status = CBS_CORRUPT;
    }
    if (status != CBS_OK) {
        CbsFree(data);
        return status;
    }
    *out = data;
    *size = (size_t)whole;
    return CBS_OK;
}

/* ------------------------------------------------------------------ files info */

static size_t utf16_to_utf8(const uint8_t *in, size_t units, char *out)
{
    size_t n = 0;
    for (size_t i = 0; i < units; i++) {
        uint32_t c = (uint32_t)in[2 * i] | (uint32_t)in[2 * i + 1] << 8;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
            uint32_t low = (uint32_t)in[2 * i + 2] | (uint32_t)in[2 * i + 3] << 8;
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (c < 0x80)
            out[n++] = (char)c;
        else if (c < 0x800) {
            out[n++] = (char)(0xC0 | c >> 6);
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[n++] = (char)(0xE0 | c >> 12);
            out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[n++] = (char)(0xF0 | c >> 18);
            out[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = 0;
    return n;
}

static int read_files(READER *r, CBS_7Z *z)
{
    z->FileCount = r_count(r, 0x1000000);
    unsigned n = z->FileCount;
    z->Name = CbsAllocZero(sizeof(char *) * (n + 1));
    z->HasStream = CbsAllocZero(n + 1);
    z->StreamIndex = CbsAllocZero(sizeof(unsigned) * (n + 1));
    z->StreamFolder = CbsAllocZero(sizeof(unsigned) * (n + 1));
    if (!z->Name || !z->HasStream || !z->StreamIndex || !z->StreamFolder)
        return CBS_MEMORY;
    for (unsigned i = 0; i < n; i++)
        z->HasStream[i] = true;
    for (;;) {
        uint8_t id = r_byte(r);
        if (r->Bad)
            return CBS_BAD;
        if (id == K_END)
            break;
        uint64_t size = r_number(r);
        if (size > (uint64_t)(r->End - r->Cursor))
            return CBS_BAD;
        READER sub = {r->Cursor, r->Cursor + size, false};
        r->Cursor += size;
        if (id == K_EMPTY_STREAM) {
            bool *empty = r_bits(&sub, n);
            if (!empty)
                return CBS_MEMORY;
            for (unsigned i = 0; i < n; i++)
                z->HasStream[i] = !empty[i];
            CbsFree(empty);
        } else if (id == K_NAMES) {
            if (r_byte(&sub) != 0)
                return CBS_UNSUPPORTED;
            for (unsigned i = 0; i < n; i++) {
                const uint8_t *start = sub.Cursor;
                size_t units = 0;
                while (sub.Cursor + 2 <= sub.End && (sub.Cursor[0] | sub.Cursor[1])) {
                    sub.Cursor += 2;
                    units++;
                }
                if (sub.Cursor + 2 > sub.End)
                    return CBS_BAD;
                sub.Cursor += 2;
                z->Name[i] = CbsAlloc(units * 4 + 1);
                if (!z->Name[i])
                    return CBS_MEMORY;
                utf16_to_utf8(start, units, z->Name[i]);
            }
        }
        if (sub.Bad)
            return CBS_BAD;
    }
    unsigned stream = 0;
    for (unsigned i = 0; i < n; i++) {
        z->StreamIndex[i] = ~0u;
        if (!z->HasStream[i])
            continue;
        if (stream >= z->Streams.SubTotal)
            return CBS_BAD;
        z->StreamIndex[i] = stream++;
    }
    /* the folder of each substream. */
    for (unsigned i = 0; i < n; i++) {
        if (z->StreamIndex[i] == ~0u)
            continue;
        unsigned folder = 0;
        while (folder < z->Streams.FolderCount &&
               z->StreamIndex[i] >= z->Streams.Folder[folder].FirstSub + z->Streams.Folder[folder].SubCount)
            folder++;
        if (folder == z->Streams.FolderCount)
            return CBS_BAD;
        z->StreamFolder[i] = folder;
    }
    return CBS_OK;
}

static int read_header(READER *r, CBS_7Z *z)
{
    for (;;) {
        uint8_t id = r_byte(r);
        if (r->Bad)
            return CBS_BAD;
        if (id == K_END)
            return CBS_OK;
        int status;
        if (id == K_ARCHIVE_PROPERTIES) {
            for (;;) {
                uint8_t pid = r_byte(r);
                if (pid == K_END || r->Bad)
                    break;
                r_skip(r, r_number(r));
            }
        } else if (id == K_ADDITIONAL_STREAMS) {
            STREAMS_INFO extra;
            status = read_streams(r, &extra);
            streams_free(&extra);
            if (status != CBS_OK)
                return status;
        } else if (id == K_MAIN_STREAMS) {
            status = read_streams(r, &z->Streams);
            if (status != CBS_OK)
                return status;
        } else if (id == K_FILES_INFO) {
            status = read_files(r, z);
            if (status != CBS_OK)
                return status;
        } else
            return CBS_BAD;
    }
}

int Cbs7zOpen(CBS_7Z **out, const uint8_t *data, size_t size)
{
    *out = nullptr;
    static const uint8_t signature[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    if (size < 32 || memcmp(data, signature, 6) || data[6] != 0)
        return CBS_BAD;
    READER start = {data + 8, data + 32, false};
    uint32_t start_crc = r_dword(&start);
    if (CbsCrc32(0, data + 12, 20) != start_crc)
        return CBS_BAD;
    uint64_t next_offset = 0, next_size = 0;
    for (int i = 0; i < 8; i++)
        next_offset |= (uint64_t)data[12 + i] << (8 * i);
    for (int i = 0; i < 8; i++)
        next_size |= (uint64_t)data[20 + i] << (8 * i);
    uint32_t next_crc = (uint32_t)data[28] | (uint32_t)data[29] << 8 | (uint32_t)data[30] << 16 | (uint32_t)data[31] << 24;
    if (next_offset > size - 32 || next_size > size - 32 - next_offset)
        return CBS_BAD;
    if (!next_size)
        return CBS_BAD;
    const uint8_t *header = data + 32 + next_offset;
    if (CbsCrc32(0, header, (size_t)next_size) != next_crc)
        return CBS_CORRUPT;
    CBS_7Z *z = CbsAllocZero(sizeof(CBS_7Z));
    if (!z)
        return CBS_MEMORY;
    z->Data = data;
    z->Size = size;
    const uint8_t *cursor = header;
    size_t length = (size_t)next_size;
    int status = CBS_OK;
    for (int depth = 0; depth < 4; depth++) {
        if (length < 1) {
            status = CBS_BAD;
            break;
        }
        if (cursor[0] == K_ENCODED_HEADER) {
            READER r = {cursor + 1, cursor + length, false};
            STREAMS_INFO info;
            status = read_streams(&r, &info);
            if (status == CBS_OK && !info.FolderCount)
                status = CBS_BAD;
            uint8_t *decoded = nullptr;
            size_t decoded_size = 0;
            if (status == CBS_OK)
                status = decode_folder_to_memory(z, &info, 0, &decoded, &decoded_size);
            streams_free(&info);
            if (status != CBS_OK)
                break;
            CbsFree(z->HeaderCopy);
            z->HeaderCopy = decoded;
            cursor = decoded;
            length = decoded_size;
            continue;
        }
        if (cursor[0] != K_HEADER) {
            status = CBS_BAD;
            break;
        }
        READER r = {cursor + 1, cursor + length, false};
        status = read_header(&r, z);
        break;
    }
    if (status != CBS_OK) {
        Cbs7zClose(z);
        return status;
    }
    *out = z;
    return CBS_OK;
}

void Cbs7zClose(CBS_7Z *z)
{
    if (!z)
        return;
    for (unsigned i = 0; z->Name && i < z->FileCount; i++)
        CbsFree(z->Name[i]);
    CbsFree(z->Name);
    CbsFree(z->HasStream);
    CbsFree(z->StreamIndex);
    CbsFree(z->StreamFolder);
    CbsFree(z->HeaderCopy);
    streams_free(&z->Streams);
    CbsFree(z);
}

unsigned Cbs7zCount(const CBS_7Z *z)
{
    return z->FileCount;
}

bool Cbs7zInfo(const CBS_7Z *z, unsigned index, const char **name, uint64_t *size, uint32_t *crc, bool *has_crc)
{
    if (index >= z->FileCount)
        return false;
    *name = z->Name[index] ? z->Name[index] : "";
    unsigned s = z->StreamIndex[index];
    *size = s == ~0u ? 0 : z->Streams.SubSize[s];
    *crc = s == ~0u ? 0 : z->Streams.SubCrc[s];
    *has_crc = s != ~0u && z->Streams.SubHasCrc[s];
    return true;
}

int Cbs7zExtract(CBS_7Z *z, unsigned index, CBS_SINK sink, void *context)
{
    if (index >= z->FileCount)
        return CBS_BAD;
    unsigned s = z->StreamIndex[index];
    if (s == ~0u)
        return CBS_OK;
    unsigned folder = z->StreamFolder[index];
    const FOLDER *f = &z->Streams.Folder[folder];
    EXTRACT e = {};
    for (unsigned i = f->FirstSub; i < s; i++)
        e.Skip += z->Streams.SubSize[i];
    e.Take = z->Streams.SubSize[s];
    e.Sink = sink;
    e.Context = context;
    int status = run_folder(z, &z->Streams, folder, &e);
    if (status == CBS_OK && z->Streams.SubHasCrc[s] && e.Crc != z->Streams.SubCrc[s])
        return CBS_CORRUPT;
    return status;
}
