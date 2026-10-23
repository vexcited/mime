/* writes each PDF page as one image, compressed with Flate and PNG's up predictor to compress repeated rows. */
#include "winspool_private.h"
#include "pdf.h"
#include <png/deflate.h>

#define MAX_PAGES 100000

struct PdfWriter {
    HANDLE File;
    ULONGLONG Offset;
    ULONGLONG *Offsets;
    UINT ObjectCount, ObjectCapacity;
    UINT PageCount;
    UINT *PageObjects;
    UINT PageCapacity;
    WCHAR Title[260];
    BOOL Failed;
    BYTE Buffer[8192];
    UINT Buffered;
};

static void *pdf_alloc(void *ctx, size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void pdf_free(void *ctx, void *p)
{
    HeapFree(GetProcessHeap(), 0, p);
}

static const MimeAllocator g_allocator = {pdf_alloc, pdf_free, nullptr};

static void flush(PdfWriter *w)
{
    if (w->Buffered && !w->Failed) {
        DWORD written;
        if (!WriteFile(w->File, w->Buffer, w->Buffered, &written, nullptr) || written != w->Buffered)
            w->Failed = TRUE;
    }
    w->Buffered = 0;
}

static void put(PdfWriter *w, const void *data, SIZE_T size)
{
    const BYTE *p = data;
    w->Offset += size;
    while (size && !w->Failed) {
        SIZE_T room = sizeof(w->Buffer) - w->Buffered;
        SIZE_T n = size < room ? size : room;
        if (n == sizeof(w->Buffer)) {
            /* whole buffers go straight out. */
            DWORD written;
            if (!WriteFile(w->File, p, (DWORD)n, &written, nullptr) || written != n)
                w->Failed = TRUE;
        } else {
            memcpy(w->Buffer + w->Buffered, p, n);
            w->Buffered += (UINT)n;
            if (w->Buffered == sizeof(w->Buffer))
                flush(w);
        }
        p += n;
        size -= n;
    }
}

static void putf(PdfWriter *w, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    int n = _vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (n > 0)
        put(w, text, (SIZE_T)(n < (int)sizeof(text) ? n : (int)sizeof(text) - 1));
}

/* records the offset of the next object. */
static UINT begin_object(PdfWriter *w)
{
    if (w->ObjectCount == w->ObjectCapacity) {
        UINT cap = w->ObjectCapacity ? w->ObjectCapacity * 2 : 64;
        ULONGLONG *grown = w->Offsets ? HeapReAlloc(GetProcessHeap(), 0, w->Offsets, cap * sizeof(ULONGLONG))
                                      : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(ULONGLONG));
        if (!grown) {
            w->Failed = TRUE;
            return 0;
        }
        w->Offsets = grown;
        w->ObjectCapacity = cap;
    }
    UINT number = w->ObjectCount + 1;
    w->Offsets[w->ObjectCount++] = w->Offset;
    putf(w, "%u 0 obj\n", number);
    return number;
}

/* document strings use hexadecimal UTF-16 with a byte order mark. */
static void put_title(PdfWriter *w, LPCWSTR text)
{
    putf(w, "<FEFF");
    for (; *text; text++)
        putf(w, "%04X", (unsigned)*text);
    putf(w, ">");
}

/* converts pixels at a given resolution to points with two decimals. */
static void put_points(PdfWriter *w, int pixels, int dpi)
{
    long long hundredths = ((long long)pixels * 7200 + dpi / 2) / dpi;
    putf(w, "%lld.%02lld", hundredths / 100, hundredths % 100);
}

PdfWriter *PdfCreate(HANDLE file, LPCWSTR title)
{
    PdfWriter *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return nullptr;
    w->File = file;
    _snwprintf(w->Title, ARRAYSIZE(w->Title), L"%ls", title ? title : L"");
    putf(w, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    /* objects 1 to 3 are the catalog, the page tree and the information, written last. */
    w->ObjectCount = 3;
    w->ObjectCapacity = 64;
    w->Offsets = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64 * sizeof(ULONGLONG));
    if (!w->Offsets) {
        HeapFree(GetProcessHeap(), 0, w);
        return nullptr;
    }
    return w;
}

BOOL PdfAddImagePage(PdfWriter *w, const BYTE *bgra, int width, int height, int stride, int xdpi, int ydpi,
                     BOOL gray)
{
    if (w->Failed || width <= 0 || height <= 0 || w->PageCount >= MAX_PAGES)
        return FALSE;
    int channels = gray ? 1 : 3;
    SIZE_T row = (SIZE_T)width * channels;
    SIZE_T size = (row + 1) * (SIZE_T)height;
    BYTE *filtered = HeapAlloc(GetProcessHeap(), 0, size);
    if (!filtered)
        return FALSE;
    /* each row is stored as its difference from the one above, a filter byte of 2 in front. */
    BYTE *current = HeapAlloc(GetProcessHeap(), 0, row * 2);
    if (!current) {
        HeapFree(GetProcessHeap(), 0, filtered);
        return FALSE;
    }
    BYTE *above = current + row;
    memset(above, 0, row);
    for (int y = 0; y < height; y++) {
        const BYTE *src = bgra + (SIZE_T)y * (SIZE_T)stride;
        if (gray) {
            for (int x = 0; x < width; x++)
                current[x] = (BYTE)((src[4 * x] * 29 + src[4 * x + 1] * 150 + src[4 * x + 2] * 77) >> 8);
        } else {
            for (int x = 0; x < width; x++) {
                current[3 * x] = src[4 * x + 2];
                current[3 * x + 1] = src[4 * x + 1];
                current[3 * x + 2] = src[4 * x];
            }
        }
        BYTE *dst = filtered + (SIZE_T)y * (row + 1);
        dst[0] = 2;
        for (SIZE_T i = 0; i < row; i++)
            dst[1 + i] = (BYTE)(current[i] - above[i]);
        memcpy(above, current, row);
    }
    HeapFree(GetProcessHeap(), 0, current);
    void *packed = nullptr;
    size_t packed_size = 0;
    int status = mime_deflate_zlib(filtered, size, 4, &g_allocator, &packed, &packed_size);
    HeapFree(GetProcessHeap(), 0, filtered);
    if (status != MIME_DEFLATE_OK)
        return FALSE;

    if (w->PageCount == w->PageCapacity) {
        UINT cap = w->PageCapacity ? w->PageCapacity * 2 : 16;
        UINT *grown = w->PageObjects ? HeapReAlloc(GetProcessHeap(), 0, w->PageObjects, cap * sizeof(UINT))
                                     : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(UINT));
        if (!grown) {
            pdf_free(nullptr, packed);
            return FALSE;
        }
        w->PageObjects = grown;
        w->PageCapacity = cap;
    }
    /* the page, then its contents, then its image: the numbers follow each other. */
    UINT page = begin_object(w);
    w->PageObjects[w->PageCount++] = page;
    putf(w, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    put_points(w, width, xdpi);
    putf(w, " ");
    put_points(w, height, ydpi);
    putf(w, "] /Resources << /XObject << /Im0 %u 0 R >> >> /Contents %u 0 R >>\nendobj\n", page + 2, page + 1);
    UINT contents = begin_object(w);
    char text[160];
    int n = _snprintf(text, sizeof(text), "q %lld.%02lld 0 0 %lld.%02lld 0 0 cm /Im0 Do Q\n",
                      (long long)width * 7200 / xdpi / 100, (long long)width * 7200 / xdpi % 100,
                      (long long)height * 7200 / ydpi / 100, (long long)height * 7200 / ydpi % 100);
    putf(w, "<< /Length %d >>\nstream\n", n);
    put(w, text, (SIZE_T)n);
    putf(w, "endstream\nendobj\n");
    (void)contents;
    begin_object(w);
    putf(w,
         "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s /BitsPerComponent 8 "
         "/Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors %d /Columns %d >> /Length %llu >>\nstream\n",
         width, height, gray ? "DeviceGray" : "DeviceRGB", channels, width, (unsigned long long)packed_size);
    put(w, packed, packed_size);
    putf(w, "\nendstream\nendobj\n");
    pdf_free(nullptr, packed);
    return !w->Failed;
}

BOOL PdfFinish(PdfWriter *w)
{
    if (!w)
        return FALSE;
    /* writes the page tree, catalog and information as objects 2, 1 and 3. */
    w->Offsets[1] = w->Offset;
    putf(w, "2 0 obj\n<< /Type /Pages /Count %u /Kids [", w->PageCount);
    for (UINT i = 0; i < w->PageCount; i++)
        putf(w, "%s%u 0 R", i ? " " : "", w->PageObjects[i]);
    putf(w, "] >>\nendobj\n");
    w->Offsets[0] = w->Offset;
    putf(w, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    w->Offsets[2] = w->Offset;
    putf(w, "3 0 obj\n<< /Producer (Mime) /Title ");
    put_title(w, w->Title);
    putf(w, " >>\nendobj\n");
    ULONGLONG xref = w->Offset;
    putf(w, "xref\n0 %u\n0000000000 65535 f \n", w->ObjectCount + 1);
    for (UINT i = 0; i < w->ObjectCount; i++)
        putf(w, "%010llu 00000 n \n", (unsigned long long)w->Offsets[i]);
    putf(w, "trailer\n<< /Size %u /Root 1 0 R /Info 3 0 R >>\nstartxref\n%llu\n%%%%EOF\n", w->ObjectCount + 1,
         (unsigned long long)xref);
    flush(w);
    BOOL ok = !w->Failed;
    PdfAbort(w);
    return ok;
}

void PdfAbort(PdfWriter *w)
{
    if (!w)
        return;
    if (w->Offsets)
        HeapFree(GetProcessHeap(), 0, w->Offsets);
    if (w->PageObjects)
        HeapFree(GetProcessHeap(), 0, w->PageObjects);
    HeapFree(GetProcessHeap(), 0, w);
}

UINT PdfPageCount(const PdfWriter *w)
{
    return w->PageCount;
}
