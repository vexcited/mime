#ifndef MIME_HOST_CAMERA_CONVERT_H
#define MIME_HOST_CAMERA_CONVERT_H

/*
 * converts YUY2, UYVY, NV12, I420, YV12 and motion JPEG to YUY2, NV12 or I420. output uses studio-range BT.601, so
 * full-range motion JPEG samples are scaled.
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

bool camera_can_convert(uint32_t src, uint32_t dst);

enum {
    CAMERA_CONVERT_FAILED,  /* invalid frame format or size; nothing converted. */
    CAMERA_CONVERT_OK,
    CAMERA_CONVERT_DAMAGED, /* conversion completed, but damage or truncation left some stale pixels. */
};

/* out has room for mime_camera_frame_bytes(dst, width, height). */
int camera_convert(CameraConverter *c, uint32_t src, const uint8_t *data, size_t length, uint32_t width,
                    uint32_t height, uint32_t dst, uint8_t *out);

void camera_converter_free(CameraConverter *c);

#endif
