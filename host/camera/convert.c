#include "convert.h"

#include "../../include/mime/camera.h"

#include <stdlib.h>
#include <string.h>

typedef struct Source {
    uint32_t fourcc;
    int width, height;
    const uint8_t *plane[3];
    int stride[3];
    bool packed;        /* YUY2 and UYVY: one plane of pairs of pixels. */
    bool uyvy;
    bool interleaved;   /* NV12: the second plane holds cb and cr in turns. */
    bool swapped;       /* YV12: the third plane is cb. */
    int chroma_rows;    /* 1 when a chroma row serves two rows of the frame. */
    int chroma_cols;    /* 1 when a chroma sample serves two pixels, 0 when each pixel has its own. */
    bool full_range;
    bool grey;          /* one component: the chroma is neutral. */
    uint8_t luma_map[256], chroma_map[256];
} Source;

bool camera_can_convert(uint32_t src, uint32_t dst)
{
    switch (src) {
    case MIME_CAMERA_FOURCC_YUY2:
    case MIME_CAMERA_FOURCC_UYVY:
    case MIME_CAMERA_FOURCC_NV12:
    case MIME_CAMERA_FOURCC_I420:
    case MIME_CAMERA_FOURCC_YV12:
    case MIME_CAMERA_FOURCC_MJPG:
        return MIME_CAMERA_CONVERTS_TO(dst);
    default:
        return false;
    }
}

/* converts one row to luma and half-width chroma, or luma alone when no chroma rows are supplied. */
static void source_row(const Source *s, int r, uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    int w = s->width, cw = (w + 1) / 2;
    int crow = s->chroma_rows ? r >> 1 : r;
    if (s->grey) {
        memcpy(y, s->plane[0] + (ptrdiff_t)r * s->stride[0], (size_t)w);
        if (cb) {
            memset(cb, 128, (size_t)cw);
            memset(cr, 128, (size_t)cw);
        }
        for (int i = 0; i < w && s->full_range; i++)
            y[i] = s->luma_map[y[i]];
        return;
    }
    if (s->packed) {
        const uint8_t *p = s->plane[0] + (ptrdiff_t)r * s->stride[0];
        int ya = s->uyvy ? 1 : 0, ca = s->uyvy ? 0 : 1;
        for (int i = 0; i < w; i += 2) {
            y[i] = p[ya];
            if (i + 1 < w)
                y[i + 1] = p[ya + 2];
            if (cb) {
                cb[i / 2] = p[ca];
                cr[i / 2] = p[ca + 2];
            }
            p += 4;
        }
        return;
    }
    memcpy(y, s->plane[0] + (ptrdiff_t)r * s->stride[0], (size_t)w);
    if (!cb) {
        for (int i = 0; i < w && s->full_range; i++)
            y[i] = s->luma_map[y[i]];
        return;
    }
    if (s->interleaved) {
        const uint8_t *p = s->plane[1] + (ptrdiff_t)crow * s->stride[1];
        for (int i = 0; i < cw; i++) {
            cb[i] = p[2 * i];
            cr[i] = p[2 * i + 1];
        }
    } else if (s->chroma_cols) {
        const uint8_t *u = s->plane[s->swapped ? 2 : 1] + (ptrdiff_t)crow * s->stride[s->swapped ? 2 : 1];
        const uint8_t *v = s->plane[s->swapped ? 1 : 2] + (ptrdiff_t)crow * s->stride[s->swapped ? 1 : 2];
        memcpy(cb, u, (size_t)cw);
        memcpy(cr, v, (size_t)cw);
    } else {
        /* a chroma sample for each pixel: two of them become one. */
        const uint8_t *u = s->plane[1] + (ptrdiff_t)crow * s->stride[1];
        const uint8_t *v = s->plane[2] + (ptrdiff_t)crow * s->stride[2];
        for (int i = 0; i < cw; i++) {
            int j = 2 * i + 1 < w ? 2 * i + 1 : 2 * i;
            cb[i] = (uint8_t)((u[2 * i] + u[j] + 1) >> 1);
            cr[i] = (uint8_t)((v[2 * i] + v[j] + 1) >> 1);
        }
    }
    if (s->full_range) {
        for (int i = 0; i < w; i++)
            y[i] = s->luma_map[y[i]];
        for (int i = 0; i < cw; i++) {
            cb[i] = s->chroma_map[cb[i]];
            cr[i] = s->chroma_map[cr[i]];
        }
    }
}

static bool describe(CameraConverter *c, uint32_t fourcc, const uint8_t *data, size_t length, uint32_t width,
                     uint32_t height, Source *s, bool *damaged)
{
    memset(s, 0, sizeof(*s));
    s->fourcc = fourcc;
    s->width = (int)width;
    s->height = (int)height;
    int cw = (int)(width + 1) / 2, ch = (int)(height + 1) / 2;
    size_t need = mime_camera_frame_bytes(fourcc, width, height);
    switch (fourcc) {
    case MIME_CAMERA_FOURCC_YUY2:
    case MIME_CAMERA_FOURCC_UYVY:
        if (length < need)
            return false;
        s->packed = true;
        s->uyvy = fourcc == MIME_CAMERA_FOURCC_UYVY;
        s->plane[0] = data;
        s->stride[0] = (int)((width + 1) / 2 * 4);
        s->chroma_cols = 1;
        return true;
    case MIME_CAMERA_FOURCC_NV12:
        if (length < need)
            return false;
        s->interleaved = true;
        s->plane[0] = data;
        s->plane[1] = data + (size_t)width * height;
        s->stride[0] = (int)width;
        s->stride[1] = cw * 2;
        s->chroma_rows = 1;
        s->chroma_cols = 1;
        return true;
    case MIME_CAMERA_FOURCC_I420:
    case MIME_CAMERA_FOURCC_YV12:
        if (length < need)
            return false;
        s->plane[0] = data;
        s->plane[1] = data + (size_t)width * height;
        s->plane[2] = s->plane[1] + (size_t)cw * ch;
        s->stride[0] = (int)width;
        s->stride[1] = s->stride[2] = cw;
        s->swapped = fourcc == MIME_CAMERA_FOURCC_YV12;
        s->chroma_rows = 1;
        s->chroma_cols = 1;
        return true;
    case MIME_CAMERA_FOURCC_MJPG: {
        MimeJpeg *j = &c->jpeg;
        if (!mime_jpeg_decode(j, data, length) || j->width != (int)width || j->height != (int)height)
            return false;
        *damaged = j->restart_errors;
        s->plane[0] = j->plane[0];
        s->stride[0] = j->stride[0];
        if (j->components == 3) {
            if (j->h[0] != j->h_max || j->v[0] != j->v_max)
                return false;
            s->plane[1] = j->plane[1];
            s->plane[2] = j->plane[2];
            s->stride[1] = j->stride[1];
            s->stride[2] = j->stride[2];
            s->chroma_cols = j->h_max / j->h[1] == 2;
            s->chroma_rows = j->v_max / j->v[1] == 2;
            if (j->h[1] != j->h[2] || j->v[1] != j->v[2])
                return false;
        } else {
            /* a grey frame has no colour: every chroma sample is neutral. */
            s->grey = true;
            s->chroma_cols = 1;
            s->chroma_rows = 1;
        }
        s->full_range = true;
        for (int i = 0; i < 256; i++) {
            s->luma_map[i] = (uint8_t)(16 + (i * 219 + 127) / 255);
            int cv = 128 + ((i - 128) * 224 + (i >= 128 ? 127 : -127)) / 255;
            s->chroma_map[i] = (uint8_t)(cv < 16 ? 16 : cv > 240 ? 240 : cv);
        }
        return true;
    }
    default:
        return false;
    }
}

int camera_convert(CameraConverter *c, uint32_t src, const uint8_t *data, size_t length, uint32_t width,
                    uint32_t height, uint32_t dst, uint8_t *out)
{
    if (!width || !height || !camera_can_convert(src, dst))
        return CAMERA_CONVERT_FAILED;
    size_t out_bytes = mime_camera_frame_bytes(dst, width, height);
    if (src == dst) {
        if (length < out_bytes)
            return CAMERA_CONVERT_FAILED;
        memcpy(out, data, out_bytes);
        return CAMERA_CONVERT_OK;
    }
    Source s;
    bool damaged = false;
    if (!describe(c, src, data, length, width, height, &s, &damaged))
        return CAMERA_CONVERT_FAILED;
    int w = (int)width, h = (int)height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    /* three rows of luma and chroma, a second set for the averages. */
    size_t row_bytes = (size_t)w * 2 + (size_t)cw * 4 + 64;
    if (c->rows_capacity < row_bytes) {
        uint8_t *p = realloc(c->rows, row_bytes);
        if (!p)
            return CAMERA_CONVERT_FAILED;
        c->rows = p;
        c->rows_capacity = row_bytes;
    }
    uint8_t *y1 = c->rows, *cb1 = y1 + w + 32, *cr1 = cb1 + cw + 32, *cb2 = cr1 + cw + 32, *cr2 = cb2 + cw + 32;
    if (dst == MIME_CAMERA_FOURCC_YUY2) {
        for (int r = 0; r < h; r++) {
            source_row(&s, r, y1, cb1, cr1);
            uint8_t *o = out + (size_t)r * cw * 4;
            for (int i = 0; i < w; i += 2) {
                o[0] = y1[i];
                o[1] = cb1[i / 2];
                o[2] = i + 1 < w ? y1[i + 1] : y1[i];
                o[3] = cr1[i / 2];
                o += 4;
            }
        }
        return damaged ? CAMERA_CONVERT_DAMAGED : CAMERA_CONVERT_OK;
    }
    uint8_t *oy = out, *ou = out + (size_t)w * h, *ov = ou + (size_t)cw * ch;
    for (int r = 0; r < h; r += 2) {
        source_row(&s, r, oy + (size_t)r * w, cb1, cr1);
        if (r + 1 < h) {
            if (!s.chroma_rows) {
                /* the chroma of two rows is averaged. */
                source_row(&s, r + 1, oy + (size_t)(r + 1) * w, cb2, cr2);
                for (int i = 0; i < cw; i++) {
                    cb1[i] = (uint8_t)((cb1[i] + cb2[i] + 1) >> 1);
                    cr1[i] = (uint8_t)((cr1[i] + cr2[i] + 1) >> 1);
                }
            } else {
                /* the rows share chroma, so only the second row's luma is needed. */
                source_row(&s, r + 1, oy + (size_t)(r + 1) * w, nullptr, nullptr);
            }
        }
        int k = r / 2;
        if (dst == MIME_CAMERA_FOURCC_NV12) {
            uint8_t *o = ou + (size_t)k * cw * 2;
            for (int i = 0; i < cw; i++) {
                o[2 * i] = cb1[i];
                o[2 * i + 1] = cr1[i];
            }
        } else {
            memcpy(ou + (size_t)k * cw, cb1, (size_t)cw);
            memcpy(ov + (size_t)k * cw, cr1, (size_t)cw);
        }
    }
    return damaged ? CAMERA_CONVERT_DAMAGED : CAMERA_CONVERT_OK;
}

void camera_converter_free(CameraConverter *c)
{
    mime_jpeg_free(&c->jpeg);
    free(c->rows);
    memset(c, 0, sizeof(*c));
}
