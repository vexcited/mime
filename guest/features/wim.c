/*
 * The fonts of a feature that are streams of a Windows image at an address, the ESD files that Microsoft's media
 * creation tool fetches. The image is a WIM whose files are kept in a few solid resources, each in chunks of 64 MiB
 * that are compressed one by one with LZMS. A stream is found by its SHA-1 in the table of streams, so the catalog
 * names no position and a newer image that holds the same file is as good. Only the chunks that hold the fonts are
 * read from the server, and every stream is checked against its SHA-1 and SHA-256 before it goes into the pack.
 *
 * The table of streams has 50 bytes for each: the size and the flags in the top byte, the offset, the original
 * size, the part, the reference count and the SHA-1. The entries with the solid flag are streams of a solid resource,
 * their offset counts from the start of the uncompressed bytes of the run of resources, and the entries whose original
 * size is 2^32 describe the solid resources themselves: where they are in the file, and how big.
 */
#include "featurep.h"
#include "../lib/lzms/lzms.h"

#define WIM_HEADER 208
#define WIM_ENTRY 50
#define WIM_VERSION_SOLID 0xE00u
#define WIM_FLAG_COMPRESSED 0x04
#define WIM_FLAG_SOLID 0x10
#define WIM_SOLID_MARK 0x100000000ull
#define WIM_COMPRESS_LZMS 3
#define MAX_TABLE (64u << 20)
#define MAX_CHUNK (64u << 20)
#define MAX_SOLIDS 64
#define MAX_JOBS 4096

typedef struct SOLID {
    ULONGLONG Offset;  /* In the file. */
    ULONGLONG Stored;
    ULONGLONG Size;    /* Uncompressed. */
    DWORD ChunkSize;
    DWORD ChunkCount;
    ULONGLONG *Start;  /* File offsets of the chunks and the end, ChunkCount + 1. */
} SOLID;

typedef struct JOB {
    SIZE_T Font;
    int Solid;
    ULONGLONG Offset; /* In the uncompressed bytes of the solid resource. */
    BOOL Found;
} JOB;

static ULONGLONG le64(const BYTE *p)
{
    return (ULONGLONG)p[0] | (ULONGLONG)p[1] << 8 | (ULONGLONG)p[2] << 16 | (ULONGLONG)p[3] << 24 | (ULONGLONG)p[4] << 32 |
           (ULONGLONG)p[5] << 40 | (ULONGLONG)p[6] << 48 | (ULONGLONG)p[7] << 56;
}

static DWORD le32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* The 20 bytes of a SHA-1 in hex. */
static BOOL parse_sha1(const char *hex, BYTE *out)
{
    if (!hex || strlen(hex) != 40)
        return FALSE;
    for (int i = 0; i < 20; i++) {
        int a = hex_digit(hex[2 * i]), b = hex_digit(hex[2 * i + 1]);
        if (a < 0 || b < 0)
            return FALSE;
        out[i] = (BYTE)(a * 16 + b);
    }
    return TRUE;
}

typedef struct WIM {
    RUN *Run;
    RANGES *Ranges;
    const FEATURE_DEF *Feature;
    SOLID Solids[MAX_SOLIDS];
    int SolidCount;
    BYTE (*Hashes)[20];
    JOB Jobs[MAX_JOBS];
    BYTE *Chunk, *Compressed;
    MimeLzms *Decoder;
    int CachedSolid;
    DWORD CachedChunk;
} WIM;

/* Loads the header of a solid resource and its table of chunk sizes. */
static DWORD load_solid(WIM *w, SOLID *s)
{
    BYTE header[16];
    DWORD error = RangesRead(w->Ranges, s->Offset, sizeof(header), header);
    if (error)
        return error;
    s->Size = le64(header);
    s->ChunkSize = le32(header + 8);
    if (le32(header + 12) != WIM_COMPRESS_LZMS || !s->ChunkSize || s->ChunkSize > MAX_CHUNK || (s->ChunkSize & (s->ChunkSize - 1)) ||
        !s->Size || s->Size > (1ull << 40))
        return ERROR_NOT_SUPPORTED;
    ULONGLONG count = (s->Size + s->ChunkSize - 1) / s->ChunkSize;
    if (count > 1 << 20 || 16 + count * 4 > s->Stored)
        return ERROR_BAD_FORMAT;
    s->ChunkCount = (DWORD)count;
    BYTE *table = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * 4);
    s->Start = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(count + 1) * sizeof(ULONGLONG));
    if (!table || !s->Start) {
        if (table)
            HeapFree(GetProcessHeap(), 0, table);
        return ERROR_OUTOFMEMORY;
    }
    error = RangesRead(w->Ranges, s->Offset + 16, (SIZE_T)count * 4, table);
    ULONGLONG at = s->Offset + 16 + count * 4;
    for (ULONGLONG c = 0; !error && c < count; c++) {
        s->Start[c] = at;
        at += le32(table + c * 4);
    }
    s->Start[count] = at;
    if (!error && at > s->Offset + s->Stored)
        error = ERROR_BAD_FORMAT;
    HeapFree(GetProcessHeap(), 0, table);
    return error;
}

/* Decodes a chunk of a solid resource into w->Chunk, unless it is the one that is there. */
static DWORD decode_chunk(WIM *w, int solid, DWORD chunk)
{
    if (w->CachedSolid == solid && w->CachedChunk == chunk)
        return ERROR_SUCCESS;
    SOLID *s = &w->Solids[solid];
    w->CachedSolid = -1;
    ULONGLONG stored = s->Start[chunk + 1] - s->Start[chunk];
    ULONGLONG size = s->Size - (ULONGLONG)chunk * s->ChunkSize;
    if (size > s->ChunkSize)
        size = s->ChunkSize;
    if (stored > size || stored > MAX_CHUNK)
        return ERROR_BAD_FORMAT;
    DWORD error = RangesRead(w->Ranges, s->Start[chunk], (SIZE_T)stored, w->Compressed);
    if (error)
        return error;
    if (stored == size)
        memcpy(w->Chunk, w->Compressed, (SIZE_T)size); /* A chunk that does not shrink is kept as it is. */
    else if (!mime_lzms_decompress(w->Decoder, w->Compressed, (SIZE_T)stored, w->Chunk, (SIZE_T)size))
        return ERROR_INVALID_DATA;
    w->CachedSolid = solid;
    w->CachedChunk = chunk;
    return ERROR_SUCCESS;
}

static int compare_jobs(const JOB *a, const JOB *b)
{
    if (a->Solid != b->Solid)
        return a->Solid < b->Solid ? -1 : 1;
    return a->Offset < b->Offset ? -1 : a->Offset > b->Offset;
}

static void sort_jobs(JOB *jobs, SIZE_T n)
{
    for (SIZE_T i = 1; i < n; i++) {
        JOB v = jobs[i];
        SIZE_T j = i;
        for (; j > 0 && compare_jobs(&jobs[j - 1], &v) > 0; j--)
            jobs[j] = jobs[j - 1];
        jobs[j] = v;
    }
}

static void fail_detail(RUN *run, PCWSTR format, const char *name)
{
    _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), format, name);
    run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
}

DWORD AcquireWim(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination)
{
    if (feature->FontCount > MAX_JOBS)
        return ERROR_NOT_SUPPORTED;
    WIM *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return ERROR_OUTOFMEMORY;
    w->Run = run;
    w->Feature = feature;
    w->CachedSolid = -1;
    DWORD error = ERROR_SUCCESS;
    BYTE *table = nullptr, *stream = nullptr;
    HANDLE pack = INVALID_HANDLE_VALUE;
    BOOL finished = FALSE;
    w->Ranges = RangesOpen(run, feature, url);
    if (!w->Ranges) {
        error = GetLastError() ? GetLastError() : ERROR_OUTOFMEMORY;
        goto done;
    }
    w->Hashes = HeapAlloc(GetProcessHeap(), 0, feature->FontCount * 20);
    if (!w->Hashes) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++)
        if (!parse_sha1(feature->Fonts[k].Sha1, w->Hashes[k])) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }

    BYTE header[WIM_HEADER];
    error = RangesRead(w->Ranges, 0, sizeof(header), header);
    if (error)
        goto done;
    ULONGLONG total = RangesTotal(w->Ranges);
    if (memcmp(header, "MSWIM\0\0\0", 8) || le32(header + 8) != WIM_HEADER || le32(header + 12) != WIM_VERSION_SOLID) {
        error = ERROR_BAD_FORMAT;
        goto done;
    }
    ULONGLONG table_size = le64(header + 0x30) & 0x00FFFFFFFFFFFFFFull, table_flags = le64(header + 0x30) >> 56,
              table_offset = le64(header + 0x38), table_original = le64(header + 0x40);
    if ((table_flags & WIM_FLAG_COMPRESSED) && table_size != table_original) {
        error = ERROR_NOT_SUPPORTED;
        goto done;
    }
    if (!table_size || table_size > MAX_TABLE || table_size % WIM_ENTRY || !total || table_offset + table_size > total) {
        error = ERROR_BAD_FORMAT;
        goto done;
    }
    table = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)table_size);
    if (!table) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    error = RangesRead(w->Ranges, table_offset, (SIZE_T)table_size, table);
    if (error)
        goto done;

    /* The runs of solid entries: the resources first, then the streams that lie in them. */
    SIZE_T count = (SIZE_T)(table_size / WIM_ENTRY);
    for (SIZE_T i = 0; i < count;) {
        if (!(table[i * WIM_ENTRY + 7] & WIM_FLAG_SOLID)) {
            i++;
            continue;
        }
        SIZE_T end = i + 1;
        while (end < count && (table[end * WIM_ENTRY + 7] & WIM_FLAG_SOLID))
            end++;
        int base = w->SolidCount;
        for (SIZE_T e = i; e < end; e++) {
            const BYTE *entry = table + e * WIM_ENTRY;
            if (le64(entry + 16) != WIM_SOLID_MARK)
                continue;
            if (w->SolidCount == MAX_SOLIDS) {
                error = ERROR_NOT_SUPPORTED;
                goto done;
            }
            SOLID *s = &w->Solids[w->SolidCount++];
            s->Stored = le64(entry) & 0x00FFFFFFFFFFFFFFull;
            s->Offset = le64(entry + 8);
            if (s->Offset + s->Stored > total) {
                error = ERROR_BAD_FORMAT;
                goto done;
            }
            error = load_solid(w, s);
            if (error)
                goto done;
        }
        for (SIZE_T e = i; e < end; e++) {
            const BYTE *entry = table + e * WIM_ENTRY;
            if (le64(entry + 16) == WIM_SOLID_MARK)
                continue;
            for (SIZE_T k = 0; k < feature->FontCount; k++) {
                if (w->Jobs[k].Found || memcmp(entry + 30, w->Hashes[k], 20))
                    continue;
                ULONGLONG offset = le64(entry + 8), size = le64(entry) & 0x00FFFFFFFFFFFFFFull;
                int s = base;
                while (s < w->SolidCount && (offset > w->Solids[s].Size || size > w->Solids[s].Size - offset)) {
                    offset -= w->Solids[s].Size;
                    s++;
                }
                if (s == w->SolidCount || size != feature->Fonts[k].Size)
                    continue;
                w->Jobs[k] = (JOB){k, s, offset, TRUE};
            }
        }
        i = end;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++)
        if (!w->Jobs[k].Found) {
            error = ERROR_FILE_NOT_FOUND;
            fail_detail(run, L"The image does not have %hs, the file of Microsoft that the feature names.", feature->Fonts[k].File);
            goto done;
        }
    HeapFree(GetProcessHeap(), 0, table);
    table = nullptr;

    /* About how much will be read: every chunk that holds a part of a font, once. */
    sort_jobs(w->Jobs, feature->FontCount);
    ULONGLONG expect = 0;
    int last_solid = -1;
    DWORD last_chunk = 0;
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        JOB *job = &w->Jobs[k];
        SOLID *s = &w->Solids[job->Solid];
        DWORD first = (DWORD)(job->Offset / s->ChunkSize), final = (DWORD)((job->Offset + feature->Fonts[job->Font].Size - 1) / s->ChunkSize);
        for (DWORD c = first; c <= final; c++) {
            if (job->Solid == last_solid && c == last_chunk)
                continue;
            expect += s->Start[c + 1] - s->Start[c];
            last_solid = job->Solid;
            last_chunk = c;
        }
    }
    RangesExpect(w->Ranges, expect);

    w->Decoder = VirtualAlloc(nullptr, mime_lzms_size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    w->Chunk = VirtualAlloc(nullptr, MAX_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    w->Compressed = VirtualAlloc(nullptr, MAX_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!w->Decoder || !w->Chunk || !w->Compressed) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    mime_lzms_init(w->Decoder);
    if (!PackCreate(destination, &pack)) {
        error = GetLastError();
        goto done;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        JOB *job = &w->Jobs[k];
        const FEATURE_FONT *font = &feature->Fonts[job->Font];
        SOLID *s = &w->Solids[job->Solid];
        stream = HeapAlloc(GetProcessHeap(), 0, font->Size);
        if (!stream) {
            error = ERROR_OUTOFMEMORY;
            goto done;
        }
        ULONGLONG offset = job->Offset;
        SIZE_T done_bytes = 0;
        while (done_bytes < font->Size) {
            DWORD chunk = (DWORD)(offset / s->ChunkSize);
            error = decode_chunk(w, job->Solid, chunk);
            if (error)
                goto done;
            SIZE_T within = (SIZE_T)(offset - (ULONGLONG)chunk * s->ChunkSize);
            ULONGLONG available = s->Size - (ULONGLONG)chunk * s->ChunkSize;
            if (available > s->ChunkSize)
                available = s->ChunkSize;
            SIZE_T take = (SIZE_T)(available - within);
            if (take > font->Size - done_bytes)
                take = font->Size - done_bytes;
            memcpy(stream + done_bytes, w->Chunk + within, take);
            done_bytes += take;
            offset += take;
        }
        if (!HashMatches(stream, font->Size, font->Sha256, font->Sha1)) {
            error = 0x80096010u; /* TRUST_E_BAD_DIGEST */
            fail_detail(run, L"%hs from the image is not the file the catalog names.", font->File);
            goto done;
        }
        if (!PackAppend(pack, font->File, stream, font->Size)) {
            error = GetLastError();
            goto done;
        }
        HeapFree(GetProcessHeap(), 0, stream);
        stream = nullptr;
    }
    finished = PackFinish(pack, (DWORD)feature->FontCount);
    pack = INVALID_HANDLE_VALUE;
    if (!finished)
        error = GetLastError();
done:
    if (pack != INVALID_HANDLE_VALUE)
        CloseHandle(pack);
    if (error)
        DeleteFileW(destination);
    if (stream)
        HeapFree(GetProcessHeap(), 0, stream);
    if (table)
        HeapFree(GetProcessHeap(), 0, table);
    for (int i = 0; i < w->SolidCount; i++)
        if (w->Solids[i].Start)
            HeapFree(GetProcessHeap(), 0, w->Solids[i].Start);
    if (w->Decoder)
        VirtualFree(w->Decoder, 0, MEM_RELEASE);
    if (w->Chunk)
        VirtualFree(w->Chunk, 0, MEM_RELEASE);
    if (w->Compressed)
        VirtualFree(w->Compressed, 0, MEM_RELEASE);
    if (w->Hashes)
        HeapFree(GetProcessHeap(), 0, w->Hashes);
    if (w->Ranges)
        RangesClose(w->Ranges);
    HeapFree(GetProcessHeap(), 0, w);
    return error;
}
