#ifndef MIME_HOST_CAMERA_JPEG_H
#define MIME_HOST_CAMERA_JPEG_H

/*
 * baseline jpeg, as webcams send it: decoded to the planes of its three components (or one) and encoded from 4:2:0
 * planes, the latter only for the host's test camera. the planes are full range, the way jfif means ycbcr.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MimeJpeg {
    int width, height;
    int components;             /* 1 or 3 */
    int h[3], v[3];             /* sampling factors */
    int h_max, v_max;
    uint8_t *plane[3];
    int stride[3];              /* bytes per row of each plane */
    int rows[3];                /* rows of each plane, padded to whole blocks */
    size_t capacity[3];
    /* what the frame left in the decoder. */
    bool restart_errors;
} MimeJpeg;

/* decodes a frame into the decoder's planes, reusing them. false for what is not a baseline or extended sequential
   huffman jpeg of 8 bit samples, or is cut short before its first row. a damaged tail leaves the rows it covers. */
bool mime_jpeg_decode(MimeJpeg *jpeg, const uint8_t *data, size_t length);
void mime_jpeg_free(MimeJpeg *jpeg);

/* the width and height a frame says, or false when it is no jpeg. */
bool mime_jpeg_size(const uint8_t *data, size_t length, int *width, int *height);

/* a 4:2:0 frame as a baseline jpeg: the room needed is at most width * height * 2 + 1024 bytes. returns the length,
   0 when it does not fit. quality is 1 to 100. */
size_t mime_jpeg_encode(const uint8_t *y, int y_stride, const uint8_t *cb, const uint8_t *cr, int c_stride, int width,
                        int height, int quality, uint8_t *out, size_t capacity);

#endif
