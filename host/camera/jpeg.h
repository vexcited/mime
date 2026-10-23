#ifndef MIME_HOST_CAMERA_JPEG_H
#define MIME_HOST_CAMERA_JPEG_H

/*
 * decodes baseline JPEG into one or three full-range component planes. encodes 4:2:0 planes for the synthetic
 * camera, using JFIF YCbCr ranges.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MimeJpeg {
    int width, height;
    int components;             /* 1 or 3. */
    int h[3], v[3];             /* sampling factors. */
    int h_max, v_max;
    uint8_t *plane[3];
    int stride[3];              /* bytes per row of each plane. */
    int rows[3];                /* rows of each plane, padded to whole blocks. */
    size_t capacity[3];
    bool restart_errors;
} MimeJpeg;

/*
 * decodes into reusable planes. accepts baseline or extended sequential Huffman JPEG with 8-bit samples; rejects
 * other formats and truncation before the first row. damaged tails leave their rows in place.
 */
bool mime_jpeg_decode(MimeJpeg *jpeg, const uint8_t *data, size_t length);
void mime_jpeg_free(MimeJpeg *jpeg);

/* reads dimensions, returning false for invalid JPEG input. */
bool mime_jpeg_size(const uint8_t *data, size_t length, int *width, int *height);

/*
 * encodes 4:2:0 at quality 1 to 100. needs at most width * height * 2 + 1024 bytes; returns length or 0 if the
 * buffer is too small.
 */
size_t mime_jpeg_encode(const uint8_t *y, int y_stride, const uint8_t *cb, const uint8_t *cr, int c_stride, int width,
                        int height, int quality, uint8_t *out, size_t capacity);

#endif
