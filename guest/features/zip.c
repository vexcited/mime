/*
 * The fonts of a feature that are members of a zip file at an address, such as the release of Cascadia: the end of the
 * file and its central directory are read, then the compressed bytes of the members the catalog names, and nothing
 * else of the many megabytes. Each font is inflated and checked against its hash before it goes into the pack.
 */
#include "featurep.h"
#include "../lib/png/inflate.h"

#define TAIL (64 * 1024)
#define MAX_MEMBER (64u << 20)

static unsigned le16(const BYTE *p)
{
    return p[0] | p[1] << 8;
}

static DWORD le32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
}

typedef struct MEMBER {
    DWORD Offset, Compressed, Size;
    unsigned Method;
    BOOL Found;
} MEMBER;

DWORD AcquireZip(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination)
{
    RANGES *ranges = RangesOpen(run, feature, url);
    if (!ranges)
        return GetLastError() ? GetLastError() : ERROR_OUTOFMEMORY;
    DWORD error = ERROR_SUCCESS, entries = 0, directory_size = 0, directory_offset = 0;
    ULONGLONG total = 0, expect = 0;
    SIZE_T tail_length = 0, eocd = 0;
    BOOL finished = FALSE;
    BYTE *tail = HeapAlloc(GetProcessHeap(), 0, TAIL);
    BYTE *directory = nullptr, *compressed = nullptr, *font = nullptr;
    MEMBER *members = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, feature->FontCount * sizeof(MEMBER));
    HANDLE pack = INVALID_HANDLE_VALUE;
    if (!tail || !members) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    /* A first byte tells the size of the file, the servers of release assets do not answer a request for the last bytes. */
    error = RangesRead(ranges, 0, 1, tail);
    total = RangesTotal(ranges);
    tail_length = total < TAIL ? (SIZE_T)total : TAIL;
    if (!error && tail_length > 22)
        error = RangesRead(ranges, total - tail_length, tail_length, tail);
    if (error || tail_length <= 22) {
        error = error ? error : ERROR_BAD_FORMAT;
        goto done;
    }
    for (eocd = tail_length - 22; eocd > 0 && le32(tail + eocd) != 0x06054B50; eocd--)
        ;
    if (le32(tail + eocd) != 0x06054B50 || !total || total > 0xFFFFFFF0ull) {
        error = ERROR_BAD_FORMAT;
        goto done;
    }
    entries = le16(tail + eocd + 10);
    directory_size = le32(tail + eocd + 12);
    directory_offset = le32(tail + eocd + 16);
    if (entries == 0xFFFF || directory_size == 0xFFFFFFFF || directory_offset == 0xFFFFFFFF || directory_size > (16u << 20) ||
        (ULONGLONG)directory_offset + directory_size > total) {
        error = ERROR_BAD_FORMAT;
        goto done;
    }
    directory = HeapAlloc(GetProcessHeap(), 0, directory_size);
    if (!directory) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    error = RangesRead(ranges, directory_offset, directory_size, directory);
    if (error)
        goto done;
    for (SIZE_T at = 0, i = 0; i < entries; i++) {
        if (at + 46 > directory_size || le32(directory + at) != 0x02014B50) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }
        unsigned name_length = le16(directory + at + 28), extra = le16(directory + at + 30), comment = le16(directory + at + 32);
        if (at + 46 + name_length + extra + comment > directory_size) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }
        char name[260];
        if (name_length < sizeof(name)) {
            memcpy(name, directory + at + 46, name_length);
            name[name_length] = 0;
            for (SIZE_T k = 0; k < feature->FontCount; k++) {
                if (feature->Fonts[k].Member && !strcmp(feature->Fonts[k].Member, name)) {
                    MEMBER *m = &members[k];
                    m->Method = le16(directory + at + 10);
                    m->Compressed = le32(directory + at + 20);
                    m->Size = le32(directory + at + 24);
                    m->Offset = le32(directory + at + 42);
                    m->Found = TRUE;
                    expect += m->Compressed;
                }
            }
        }
        at += 46 + name_length + extra + comment;
    }
    RangesExpect(ranges, expect);
    if (!PackCreate(destination, &pack)) {
        error = GetLastError();
        goto done;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        const FEATURE_FONT *info = &feature->Fonts[k];
        MEMBER *m = &members[k];
        if (!m->Found || m->Size != info->Size || m->Compressed > MAX_MEMBER || m->Size > MAX_MEMBER ||
            (m->Method != 0 && m->Method != 8) || (m->Method == 0 && m->Compressed != m->Size)) {
            error = ERROR_FILE_NOT_FOUND;
            _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"The file %hs is not in the archive as it was.", info->File);
            run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
            goto done;
        }
        BYTE local[30];
        error = RangesRead(ranges, m->Offset, sizeof(local), local);
        if (error)
            goto done;
        if (le32(local) != 0x04034B50) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }
        ULONGLONG start = (ULONGLONG)m->Offset + 30 + le16(local + 26) + le16(local + 28);
        if (start + m->Compressed > total) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }
        compressed = HeapAlloc(GetProcessHeap(), 0, m->Compressed ? m->Compressed : 1);
        font = HeapAlloc(GetProcessHeap(), 0, m->Size ? m->Size : 1);
        if (!compressed || !font) {
            error = ERROR_OUTOFMEMORY;
            goto done;
        }
        error = RangesRead(ranges, start, m->Compressed, compressed);
        if (error)
            goto done;
        if (m->Method == 8) {
            size_t produced = 0;
            if (mime_inflate(compressed, m->Compressed, font, m->Size, &produced, 0) != MIME_INFLATE_OK || produced != m->Size) {
                error = ERROR_INVALID_DATA;
                goto done;
            }
        } else {
            memcpy(font, compressed, m->Size);
        }
        if (!HashMatches(font, m->Size, info->Sha256, info->Sha1)) {
            error = 0x80096010u;
            _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"%hs is not the file the catalog names.", info->File);
            run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
            goto done;
        }
        if (!PackAppend(pack, info->File, font, m->Size)) {
            error = GetLastError();
            goto done;
        }
        HeapFree(GetProcessHeap(), 0, compressed);
        HeapFree(GetProcessHeap(), 0, font);
        compressed = font = nullptr;
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
    if (tail)
        HeapFree(GetProcessHeap(), 0, tail);
    if (directory)
        HeapFree(GetProcessHeap(), 0, directory);
    if (compressed)
        HeapFree(GetProcessHeap(), 0, compressed);
    if (font)
        HeapFree(GetProcessHeap(), 0, font);
    if (members)
        HeapFree(GetProcessHeap(), 0, members);
    RangesClose(ranges);
    return error;
}
