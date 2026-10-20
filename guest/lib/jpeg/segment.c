/*
 * the marker segments before the image: walking them, and the exif block, xmp packet and icc profile they carry.
 */
#include "internal.h"

bool mime_jpeg_is_jpeg(const uint8_t *data, size_t size)
{
    return data && size >= 3 && data[0] == 0xFF && data[1] == M_SOI && data[2] == 0xFF;
}

int mime_jpeg_segments_init(MimeJpegSegments *it, const uint8_t *data, size_t size)
{
    if (!data || size < 4 || data[0] != 0xFF || data[1] != M_SOI)
        return MIME_JPEG_ERR_FORMAT;
    it->data = data;
    it->size = size;
    it->pos = 2;
    it->done = false;
    return 0;
}

int mime_jpeg_next_segment(MimeJpegSegments *it, MimeJpegSegment *seg)
{
    const uint8_t *d = it->data;
    size_t n = it->size;
    size_t p = it->pos;
    if (it->done)
        return 0;
    for (;;) {
        /* bytes that are not a marker are skipped, and so are the fill bytes of a marker. */
        while (p < n && d[p] != 0xFF)
            p++;
        while (p < n && d[p] == 0xFF)
            p++;
        if (p >= n) {
            it->done = true;
            return 0;
        }
        int m = d[p++];
        if (m == M_EOI) {
            it->done = true;
            return 0;
        }
        /* markers without a length. */
        if (m == 0 || m == 0x01 || (m >= M_RST0 && m <= M_SOI))
            continue;
        if (p + 2 > n) {
            it->done = true;
            return MIME_JPEG_ERR_FORMAT;
        }
        size_t len = ((size_t)d[p] << 8) | d[p + 1];
        if (len < 2 || p + len > n) {
            it->done = true;
            return MIME_JPEG_ERR_FORMAT;
        }
        seg->marker = m;
        seg->data = d + p + 2;
        seg->size = (uint32_t)(len - 2);
        seg->offset = p - 2; /* the 0xff byte the marker code follows. */
        it->pos = p + len;
        if (m == M_SOS)
            it->done = true;
        return 1;
    }
}

static int find_prefixed(const uint8_t *data, size_t size, int marker, const void *prefix, size_t prefix_size,
                         const uint8_t **payload, size_t *length)
{
    MimeJpegSegments it;
    MimeJpegSegment seg;
    int rc = mime_jpeg_segments_init(&it, data, size);
    if (rc)
        return rc;
    while ((rc = mime_jpeg_next_segment(&it, &seg)) > 0) {
        if (seg.marker == marker && seg.size >= prefix_size && !__builtin_memcmp(seg.data, prefix, prefix_size)) {
            *payload = seg.data + prefix_size;
            *length = seg.size - prefix_size;
            return 0;
        }
    }
    return rc < 0 ? rc : MIME_JPEG_ERR_NOT_FOUND;
}

int mime_jpeg_find_exif(const uint8_t *data, size_t size, const uint8_t **payload, size_t *length)
{
    return find_prefixed(data, size, M_APP1, "Exif\0\0", 6, payload, length);
}

int mime_jpeg_find_xmp(const uint8_t *data, size_t size, const uint8_t **payload, size_t *length)
{
    return find_prefixed(data, size, M_APP1, "http://ns.adobe.com/xap/1.0/", 29, payload, length);
}

/* a profile that does not fit one segment comes in numbered chunks, in any order. */
int mime_jpeg_get_icc(const uint8_t *data, size_t size, const MimeAllocator *alloc, uint8_t **profile, size_t *length)
{
    MimeJpegSegments it;
    MimeJpegSegment seg;
    const uint8_t *chunk[256];
    uint32_t chunk_size[256];
    unsigned count = 0;
    int rc = mime_jpeg_segments_init(&it, data, size);
    if (rc)
        return rc;
    __builtin_memset(chunk, 0, sizeof chunk);
    while ((rc = mime_jpeg_next_segment(&it, &seg)) > 0) {
        if (seg.marker != M_APP2 || seg.size < 14 || __builtin_memcmp(seg.data, "ICC_PROFILE\0", 12))
            continue;
        unsigned number = seg.data[12], total = seg.data[13];
        if (number == 0 || number > total || (count && total != count) || chunk[number])
            return MIME_JPEG_ERR_FORMAT;
        count = total;
        chunk[number] = seg.data + 14;
        chunk_size[number] = seg.size - 14;
    }
    if (rc < 0)
        return rc;
    if (!count)
        return MIME_JPEG_ERR_NOT_FOUND;
    size_t total_size = 0;
    for (unsigned i = 1; i <= count; i++) {
        if (!chunk[i])
            return MIME_JPEG_ERR_FORMAT;
        total_size += chunk_size[i];
    }
    uint8_t *out = mime_alloc(alloc, total_size ? total_size : 1);
    if (!out)
        return MIME_JPEG_ERR_NOMEM;
    size_t at = 0;
    for (unsigned i = 1; i <= count; i++) {
        __builtin_memcpy(out + at, chunk[i], chunk_size[i]);
        at += chunk_size[i];
    }
    *profile = out;
    *length = total_size;
    return 0;
}
