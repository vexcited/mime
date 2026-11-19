/*
 * The fonts of a feature that are streams of a Windows image at an address, read by guest/lib/wim over range requests.
 * Only the chunks that hold the fonts are read from the server, and every stream is checked against its SHA-1 and
 * SHA-256 before it goes into the pack.
 */
#include "featurep.h"
#include "../lib/wim/wim.h"

#define MAX_JOBS 4096

typedef struct JOB {
    SIZE_T Font;
    MimeWimStream Stream;
} JOB;

static int compare_jobs(const JOB *a, const JOB *b)
{
    if (a->Stream.solid != b->Stream.solid)
        return a->Stream.solid < b->Stream.solid ? -1 : 1;
    return a->Stream.offset < b->Stream.offset ? -1 : a->Stream.offset > b->Stream.offset;
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

static int io_read(void *context, uint64_t offset, size_t length, void *buffer)
{
    return (int)RangesRead(context, offset, length, buffer);
}

static uint64_t io_total(void *context)
{
    return RangesTotal(context);
}

static void *io_alloc(void *context, size_t size)
{
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void io_free(void *context, void *block)
{
    VirtualFree(block, 0, MEM_RELEASE);
}

static DWORD win32_error(int error)
{
    switch (error) {
    case MIME_WIM_OK:
        return ERROR_SUCCESS;
    case MIME_WIM_BAD_FORMAT:
        return ERROR_BAD_FORMAT;
    case MIME_WIM_UNSUPPORTED:
        return ERROR_NOT_SUPPORTED;
    case MIME_WIM_NO_MEMORY:
        return ERROR_OUTOFMEMORY;
    case MIME_WIM_BAD_DATA:
        return ERROR_INVALID_DATA;
    default:
        return (DWORD)error;
    }
}

DWORD AcquireWim(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination)
{
    if (feature->FontCount > MAX_JOBS)
        return ERROR_NOT_SUPPORTED;
    JOB *jobs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, feature->FontCount * sizeof(JOB));
    MimeWim *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    MimeWimStream *streams = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, feature->FontCount * sizeof(MimeWimStream));
    RANGES *ranges = nullptr;
    BYTE *stream = nullptr;
    HANDLE pack = INVALID_HANDLE_VALUE;
    BOOL opened = FALSE;
    DWORD error = ERROR_SUCCESS;
    if (!jobs || !w || !streams) {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    ranges = RangesOpen(run, feature, url);
    if (!ranges) {
        error = GetLastError() ? GetLastError() : ERROR_OUTOFMEMORY;
        goto done;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        const FEATURE_FONT *font = &feature->Fonts[k];
        if (!mime_wim_sha1(font->Sha1, font->Sha1 ? strlen(font->Sha1) : 0, streams[k].sha1)) {
            error = ERROR_BAD_FORMAT;
            goto done;
        }
        streams[k].size = font->Size;
    }
    MimeWimIo io = {ranges, io_read, io_total, io_alloc, io_free};
    error = win32_error(mime_wim_open(w, &io, streams, feature->FontCount));
    opened = TRUE;
    if (error)
        goto done;
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        if (!streams[k].found) {
            error = ERROR_FILE_NOT_FOUND;
            fail_detail(run, L"The image does not have %hs, the file of Microsoft that the feature names.", feature->Fonts[k].File);
            goto done;
        }
        jobs[k] = (JOB){k, streams[k]};
    }

    /* About how much will be read: every chunk that holds a part of a font, once. */
    sort_jobs(jobs, feature->FontCount);
    ULONGLONG expect = 0;
    int last_solid = -1;
    uint32_t last_chunk = 0;
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        uint32_t first, final;
        mime_wim_chunks(w, &jobs[k].Stream, &first, &final);
        for (uint32_t c = first; c <= final; c++) {
            if (jobs[k].Stream.solid == last_solid && c == last_chunk)
                continue;
            uint64_t offset, stored;
            mime_wim_chunk_range(w, jobs[k].Stream.solid, c, &offset, &stored);
            expect += stored;
            last_solid = jobs[k].Stream.solid;
            last_chunk = c;
        }
    }
    RangesExpect(ranges, expect);

    if (!PackCreate(destination, &pack)) {
        error = GetLastError();
        goto done;
    }
    for (SIZE_T k = 0; k < feature->FontCount; k++) {
        const FEATURE_FONT *font = &feature->Fonts[jobs[k].Font];
        stream = HeapAlloc(GetProcessHeap(), 0, font->Size ? font->Size : 1);
        if (!stream) {
            error = ERROR_OUTOFMEMORY;
            goto done;
        }
        error = win32_error(mime_wim_read(w, &jobs[k].Stream, stream));
        if (error)
            goto done;
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
    BOOL finished = PackFinish(pack, (DWORD)feature->FontCount);
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
    if (opened)
        mime_wim_close(w);
    if (ranges)
        RangesClose(ranges);
    if (streams)
        HeapFree(GetProcessHeap(), 0, streams);
    if (w)
        HeapFree(GetProcessHeap(), 0, w);
    if (jobs)
        HeapFree(GetProcessHeap(), 0, jobs);
    return error;
}
