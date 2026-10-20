#include "internal.h"

/* marker segment walking and the metadata that lives in application segments. */

int mime_jpeg_segments_init(MimeJpegSegments *it, const void *data, size_t size)
{
    __builtin_memset(it, 0, sizeof *it);
    if (!data || size < 4)
        return MIME_JPEG_ERR_FORMAT;
    const uint8_t *p = data;
    if (p[0] != 0xFF || p[1] != 0xD8)
        return MIME_JPEG_ERR_FORMAT;
    it->base = p;
    it->size = size;
    it->pos = 2;
    it->started = true;
    return MIME_JPEG_OK;
}

int mime_jpeg_next_segment(MimeJpegSegments *it, MimeJpegSegment *seg)
{
    if (!it->started || it->done)
        return 0;
    const uint8_t *p = it->base;
    size_t pos = it->pos;
    for (;;) {
        /* skip anything up to the next marker, fill bytes included. */
        while (pos < it->size && p[pos] != 0xFF)
            pos++;
        while (pos < it->size && p[pos] == 0xFF)
            pos++;
        if (pos >= it->size) {
            it->done = true;
            return 0;
        }
        unsigned m = p[pos++];
        if (m == 0) {
            continue;
        }
        if (m == 0xD9) {
            it->done = true;
            return 0;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
            continue;
        if (pos + 2 > it->size) {
            it->done = true;
            return MIME_JPEG_ERR_FORMAT;
        }
        size_t len = (size_t)p[pos] << 8 | p[pos + 1];
        if (len < 2 || pos + len > it->size) {
            it->done = true;
            return MIME_JPEG_ERR_FORMAT;
        }
        seg->marker = (uint8_t)m;
        seg->data = p + pos + 2;
        seg->size = (uint32_t)(len - 2);
        seg->offset = pos - 2;
        it->pos = pos + len;
        if (m == 0xDA)
            it->done = true;
        return 1;
    }
}

static int find_app1(const void *data, size_t size, const uint8_t *sig, size_t sig_len, const uint8_t **payload,
                     size_t *payload_size)
{
    MimeJpegSegments it;
    MimeJpegSegment seg;
    int rc = mime_jpeg_segments_init(&it, data, size);
    if (rc < 0)
        return rc;
    while ((rc = mime_jpeg_next_segment(&it, &seg)) > 0) {
        if (seg.marker == 0xE1 && seg.size >= sig_len && !__builtin_memcmp(seg.data, sig, sig_len)) {
            *payload = seg.data + sig_len;
            *payload_size = seg.size - sig_len;
            return MIME_JPEG_OK;
        }
    }
    return rc < 0 ? rc : MIME_JPEG_ERR_NOT_FOUND;
}

int mime_jpeg_find_exif(const void *data, size_t size, const uint8_t **payload, size_t *payload_size)
{
    static const uint8_t sig[6] = {'E', 'x', 'i', 'f', 0, 0};
    return find_app1(data, size, sig, sizeof sig, payload, payload_size);
}

int mime_jpeg_find_xmp(const void *data, size_t size, const uint8_t **payload, size_t *payload_size)
{
    static const uint8_t sig[29] = "http://ns.adobe.com/xap/1.0/";
    return find_app1(data, size, sig, sizeof sig, payload, payload_size);
}

int mime_jpeg_get_icc(const void *data, size_t size, const MimeAllocator *alloc, uint8_t **profile,
                      size_t *profile_size)
{
    static const uint8_t sig[12] = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0};
    MimeJpegSegments it;
    MimeJpegSegment seg;
    uint32_t chunk_size[256] = {0};
    const uint8_t *chunk_data[256] = {nullptr};
    unsigned count = 0, found = 0;
    uint64_t total = 0;
    int rc = mime_jpeg_segments_init(&it, data, size);

    if (rc < 0)
        return rc;
    if (!alloc || !profile || !profile_size)
        return MIME_JPEG_ERR_PARAM;
    *profile = nullptr;
    while ((rc = mime_jpeg_next_segment(&it, &seg)) > 0) {
        if (seg.marker != 0xE2 || seg.size < 14 || __builtin_memcmp(seg.data, sig, sizeof sig))
            continue;
        unsigned seq = seg.data[12], n = seg.data[13];
        if (seq == 0 || n == 0 || seq > n || (count && n != count))
            return MIME_JPEG_ERR_FORMAT;
        count = n;
        if (chunk_data[seq])
            return MIME_JPEG_ERR_FORMAT;
        chunk_data[seq] = seg.data + 14;
        chunk_size[seq] = seg.size - 14;
        total += chunk_size[seq];
        found++;
    }
    if (rc < 0 && !found)
        return rc;
    if (!found)
        return MIME_JPEG_ERR_NOT_FOUND;
    if (found != count)
        return MIME_JPEG_ERR_FORMAT;
    if (total == 0 || total > SIZE_MAX / 2)
        return MIME_JPEG_ERR_FORMAT;
    uint8_t *buf = mime_alloc(alloc, (size_t)total);
    if (!buf)
        return MIME_JPEG_ERR_NOMEM;
    size_t off = 0;
    for (unsigned i = 1; i <= count; i++) {
        __builtin_memcpy(buf + off, chunk_data[i], chunk_size[i]);
        off += chunk_size[i];
    }
    *profile = buf;
    *profile_size = (size_t)total;
    return MIME_JPEG_OK;
}
