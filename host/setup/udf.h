/*
 * read-only udf reader, enough for windows installation media.
 * windows isos are mastered with udf 1.02 next to an iso 9660 bridge,
 * but install.wim is larger than 4 GiB so only the udf view is usable.
 */
#ifndef MIME_UDF_H
#define MIME_UDF_H

#include "../mime_host.h"

typedef struct MimeUdf MimeUdf;

typedef struct MimeUdfExtent {
    uint64_t file_offset;
    uint64_t disk_offset; /* byte offset in the image, UINT64_MAX for sparse extents */
    uint64_t length;
} MimeUdfExtent;

typedef struct MimeUdfFile {
    uint64_t size;
    bool is_dir;
    uint32_t extent_count;
    MimeUdfExtent *extents;
    uint8_t *inline_data; /* embedded data for tiny files */
} MimeUdfFile;

MimeUdf *mime_udf_open(const char *image_path);
void mime_udf_close(MimeUdf *udf);

/* case-insensitive lookup of a '/' separated path. */
bool mime_udf_lookup(MimeUdf *udf, const char *path, MimeUdfFile *out);
void mime_udf_file_free(MimeUdfFile *file);
bool mime_udf_read(MimeUdf *udf, const MimeUdfFile *file, uint64_t offset, void *buf, size_t len);

#endif
