#ifndef MIME_HOST_CAMERA_CONVERT_H
#define MIME_HOST_CAMERA_CONVERT_H

/*
 * frames of one format into another, for the formats cameras send (yuy2, uyvy, nv12, i420, yv12 and motion jpeg) and
 * the formats the guest asks for (yuy2, nv12 and i420). the outputs are studio range bt.601 as windows' video
 * processor makes them, so a motion jpeg frame, whose samples are full range, is scaled.
 */
#include "jpeg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CameraConverter {
    MimeJpeg jpeg;
    uint8_t *rows;
    size_t rows_capacity;
} CameraConverter;

/* whether a frame of format src can become one of format dst. */
bool camera_can_convert(uint32_t src, uint32_t dst);

enum {
    CAMERA_CONVERT_FAILED,  /* nothing was converted: the frame is no frame of that format and size */
    CAMERA_CONVERT_OK,
    CAMERA_CONVERT_DAMAGED, /* all of it was converted, but the frame was cut short or damaged, and some of it is stale */
};

/* out has room for mime_camera_frame_bytes(dst, width, height). */
int camera_convert(CameraConverter *c, uint32_t src, const uint8_t *data, size_t length, uint32_t width,
                    uint32_t height, uint32_t dst, uint8_t *out);

void camera_converter_free(CameraConverter *c);

#endif
