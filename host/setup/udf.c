#include "udf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define UDF_SECTOR 2048u

enum {
    UDF_TAG_PRIMARY_VD = 1,
    UDF_TAG_ANCHOR = 2,
    UDF_TAG_VD_POINTER = 3,
    UDF_TAG_PARTITION = 5,
    UDF_TAG_LOGICAL_VOLUME = 6,
    UDF_TAG_TERMINATOR = 8,
    UDF_TAG_FILE_SET = 256,
    UDF_TAG_FILE_ID = 257,
    UDF_TAG_ALLOC_EXT = 258,
    UDF_TAG_FILE_ENTRY = 261,
    UDF_TAG_EXT_FILE_ENTRY = 266,
};

struct MimeUdf {
    int fd;
    uint32_t block_size;
    uint32_t partition_start; /* in blocks. */
    uint32_t partition_length;
    MimeUdfFile root;
};

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static bool read_at(MimeUdf *udf, uint64_t offset, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len) {
        ssize_t n = pread(udf->fd, p, len, (off_t)offset);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return false;
        }
        p += n;
        offset += (uint64_t)n;
        len -= (size_t)n;
    }
    return true;
}

static bool read_block(MimeUdf *udf, uint64_t block, void *buf)
{
    return read_at(udf, block * udf->block_size, buf, udf->block_size);
}

static uint16_t tag_id(const uint8_t *desc)
{
    return rd16(desc);
}

/* appends the extents described by an allocation descriptor list. */
static bool parse_allocs(MimeUdf *udf, MimeUdfFile *file, const uint8_t *ad, uint32_t ad_len, int ad_type,
                         uint64_t *file_pos, int depth)
{
    uint32_t step = ad_type == 0 ? 8 : 16;
    for (uint32_t off = 0; off + step <= ad_len; off += step) {
        uint32_t raw_len = rd32(ad + off);
        uint32_t len = raw_len & 0x3FFFFFFF;
        uint32_t type = raw_len >> 30;
        uint32_t lbn = rd32(ad + off + 4);
        if (len == 0)
            break;
        if (type == 3) {
            /* continuation of the descriptor list in another block. */
            if (depth > 64)
                return false;
            uint8_t *blk = mime_xmalloc(udf->block_size);
            bool ok = read_block(udf, (uint64_t)udf->partition_start + lbn, blk) && tag_id(blk) == UDF_TAG_ALLOC_EXT;
            if (ok) {
                uint32_t inner = rd32(blk + 20);
                if (inner > udf->block_size - 24)
                    inner = udf->block_size - 24;
                ok = parse_allocs(udf, file, blk + 24, inner, ad_type, file_pos, depth + 1);
            }
            free(blk);
            if (!ok)
                return false;
            continue;
        }
        file->extents = mime_xrealloc(file->extents, sizeof(MimeUdfExtent) * (file->extent_count + 1));
        MimeUdfExtent *ext = &file->extents[file->extent_count++];
        ext->file_offset = *file_pos;
        ext->length = len;
        ext->disk_offset = type == 0 ? ((uint64_t)udf->partition_start + lbn) * udf->block_size : UINT64_MAX;
        *file_pos += len;
    }
    return true;
}

/* loads a file entry or extended file entry at partition block lbn. */
static bool load_file_entry(MimeUdf *udf, uint32_t lbn, MimeUdfFile *file)
{
    memset(file, 0, sizeof(*file));
    uint8_t *blk = mime_xmalloc(udf->block_size);
    if (!read_block(udf, (uint64_t)udf->partition_start + lbn, blk)) {
        free(blk);
        return false;
    }
    uint16_t id = tag_id(blk);
    uint32_t ea_len, ad_len, base;
    if (id == UDF_TAG_FILE_ENTRY) {
        ea_len = rd32(blk + 168);
        ad_len = rd32(blk + 172);
        base = 176;
    } else if (id == UDF_TAG_EXT_FILE_ENTRY) {
        ea_len = rd32(blk + 208);
        ad_len = rd32(blk + 212);
        base = 216;
    } else {
        free(blk);
        return false;
    }
    uint8_t file_type = blk[16 + 11];
    uint16_t icb_flags = rd16(blk + 16 + 18);
    file->is_dir = file_type == 4;
    file->size = rd64(blk + 56);
    if (base + ea_len > udf->block_size || base + ea_len + ad_len > udf->block_size) {
        free(blk);
        return false;
    }
    const uint8_t *ad = blk + base + ea_len;
    int ad_type = icb_flags & 7;
    bool ok = true;
    if (ad_type == 3) {
        file->inline_data = mime_xmalloc(ad_len ? ad_len : 1);
        memcpy(file->inline_data, ad, ad_len);
        if (file->size > ad_len)
            file->size = ad_len;
    } else if (ad_type == 0 || ad_type == 1) {
        uint64_t pos = 0;
        ok = parse_allocs(udf, file, ad, ad_len, ad_type, &pos, 0);
    } else {
        ok = false;
    }
    free(blk);
    return ok;
}

void mime_udf_file_free(MimeUdfFile *file)
{
    free(file->extents);
    free(file->inline_data);
    memset(file, 0, sizeof(*file));
}

bool mime_udf_read(MimeUdf *udf, const MimeUdfFile *file, uint64_t offset, void *buf, size_t len)
{
    uint8_t *out = buf;
    if (offset + len > file->size)
        return false;
    if (file->inline_data) {
        memcpy(out, file->inline_data + offset, len);
        return true;
    }
    for (uint32_t i = 0; i < file->extent_count && len; i++) {
        const MimeUdfExtent *ext = &file->extents[i];
        if (offset >= ext->file_offset + ext->length)
            continue;
        uint64_t in_ext = offset - ext->file_offset;
        size_t n = (size_t)MIME_MIN((uint64_t)len, ext->length - in_ext);
        if (ext->disk_offset == UINT64_MAX)
            memset(out, 0, n);
        else if (!read_at(udf, ext->disk_offset + in_ext, out, n))
            return false;
        out += n;
        offset += n;
        len -= n;
    }
    return len == 0;
}

/* decodes a UDF dstring or file identifier to UTF-8 for case-insensitive lookup. */
static void decode_name(const uint8_t *id, uint32_t len, char *out, size_t out_size)
{
    size_t o = 0;
    if (len == 0) {
        out[0] = 0;
        return;
    }
    if (id[0] == 8) {
        for (uint32_t i = 1; i < len && o + 1 < out_size; i++)
            out[o++] = (char)id[i];
        out[o] = 0;
    } else if (id[0] == 16) {
        uint16_t tmp[256];
        uint32_t n = 0;
        for (uint32_t i = 1; i + 1 < len && n < 255; i += 2)
            tmp[n++] = (uint16_t)(id[i] << 8 | id[i + 1]);
        mime_utf16_to_utf8(out, out_size, tmp, n);
    } else {
        out[0] = 0;
    }
}

static bool find_child(MimeUdf *udf, const MimeUdfFile *dir, const char *name, MimeUdfFile *out)
{
    if (!dir->is_dir || dir->size > 64u * 1024 * 1024)
        return false;
    uint8_t *data = mime_xmalloc(dir->size + 1);
    if (!mime_udf_read(udf, dir, 0, data, dir->size)) {
        free(data);
        return false;
    }
    bool found = false;
    uint64_t off = 0;
    while (off + 38 <= dir->size) {
        const uint8_t *fid = data + off;
        if (tag_id(fid) != UDF_TAG_FILE_ID)
            break;
        uint8_t chars = fid[18];
        uint8_t name_len = fid[19];
        uint32_t icb_lbn = rd32(fid + 24);
        uint16_t iu_len = rd16(fid + 36);
        uint64_t total = (38 + (uint64_t)iu_len + name_len + 3) & ~3ull;
        if (off + total > dir->size)
            break;
        if (!(chars & 0x08) && !(chars & 0x04)) {
            char entry[512];
            decode_name(fid + 38 + iu_len, name_len, entry, sizeof(entry));
            if (strcasecmp(entry, name) == 0) {
                found = load_file_entry(udf, icb_lbn, out);
                break;
            }
        }
        off += total;
    }
    free(data);
    return found;
}

bool mime_udf_lookup(MimeUdf *udf, const char *path, MimeUdfFile *out)
{
    char component[512];
    MimeUdfFile cur = udf->root;
    cur.extents = nullptr;
    cur.inline_data = nullptr;
    /* deep copy the root so every level can be freed uniformly. */
    if (udf->root.extent_count) {
        cur.extents = mime_xmalloc(sizeof(MimeUdfExtent) * udf->root.extent_count);
        memcpy(cur.extents, udf->root.extents, sizeof(MimeUdfExtent) * udf->root.extent_count);
    }
    if (udf->root.inline_data) {
        cur.inline_data = mime_xmalloc(udf->root.size + 1);
        memcpy(cur.inline_data, udf->root.inline_data, udf->root.size);
    }

    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        size_t n = 0;
        while (p[n] && p[n] != '/')
            n++;
        if (n >= sizeof(component)) {
            mime_udf_file_free(&cur);
            return false;
        }
        memcpy(component, p, n);
        component[n] = 0;
        p += n;
        MimeUdfFile next;
        bool ok = find_child(udf, &cur, component, &next);
        mime_udf_file_free(&cur);
        if (!ok)
            return false;
        cur = next;
    }
    *out = cur;
    return true;
}

MimeUdf *mime_udf_open(const char *image_path)
{
    MimeUdf *udf = mime_xcalloc(1, sizeof(*udf));
    udf->fd = open(image_path, O_RDONLY);
    if (udf->fd < 0) {
        free(udf);
        return nullptr;
    }
    udf->block_size = UDF_SECTOR;

    uint8_t sec[UDF_SECTOR];
    if (!read_at(udf, 256ull * UDF_SECTOR, sec, sizeof(sec)) || tag_id(sec) != UDF_TAG_ANCHOR) {
        mime_error("image has no udf anchor, not a windows installation iso?");
        mime_udf_close(udf);
        return nullptr;
    }
    uint32_t vds_len = rd32(sec + 16);
    uint32_t vds_loc = rd32(sec + 20);

    uint32_t fsd_lbn = 0;
    bool have_partition = false, have_lvd = false;
    for (uint32_t i = 0; i < vds_len / UDF_SECTOR && i < 64; i++) {
        if (!read_at(udf, ((uint64_t)vds_loc + i) * UDF_SECTOR, sec, sizeof(sec)))
            break;
        uint16_t id = tag_id(sec);
        if (id == UDF_TAG_PARTITION) {
            udf->partition_start = rd32(sec + 188);
            udf->partition_length = rd32(sec + 192);
            have_partition = true;
        } else if (id == UDF_TAG_LOGICAL_VOLUME) {
            udf->block_size = rd32(sec + 212);
            fsd_lbn = rd32(sec + 248 + 4);
            have_lvd = true;
        } else if (id == UDF_TAG_TERMINATOR) {
            break;
        }
    }
    if (!have_partition || !have_lvd || udf->block_size != UDF_SECTOR) {
        mime_error("unsupported udf volume layout");
        mime_udf_close(udf);
        return nullptr;
    }

    if (!read_block(udf, (uint64_t)udf->partition_start + fsd_lbn, sec) || tag_id(sec) != UDF_TAG_FILE_SET) {
        mime_error("udf file set descriptor not found");
        mime_udf_close(udf);
        return nullptr;
    }
    uint32_t root_lbn = rd32(sec + 400 + 4);
    if (!load_file_entry(udf, root_lbn, &udf->root) || !udf->root.is_dir) {
        mime_error("cannot read udf root directory");
        mime_udf_close(udf);
        return nullptr;
    }
    return udf;
}

void mime_udf_close(MimeUdf *udf)
{
    if (!udf)
        return;
    if (udf->fd >= 0)
        close(udf->fd);
    mime_udf_file_free(&udf->root);
    free(udf);
}
