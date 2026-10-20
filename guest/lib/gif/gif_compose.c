#include "gif.h"
#include "gif_internal.h"

struct MimeGifComposer {
    const MimeGif *gif;
    unsigned width;
    unsigned height;
    uint32_t clear_color;
    uint8_t *canvas;
    uint8_t *backup; /* NULL when no frame disposes with 3. */
    uint8_t *indices;
    size_t indices_cap;
    int current; /* last frame drawn, -1 for none. */
    /* disposal of the current frame, applied before the next one is drawn. */
    unsigned pending_method;
    unsigned px0, py0, px1, py1;
};

static void fill_rect(MimeGifComposer *c, unsigned x0, unsigned y0, unsigned x1, unsigned y1, uint32_t color)
{
    size_t stride = (size_t)c->width * 4;
    for (unsigned y = y0; y < y1; y++) {
        uint8_t *row = c->canvas + y * stride + (size_t)x0 * 4;
        for (unsigned x = x0; x < x1; x++, row += 4)
            __builtin_memcpy(row, &color, 4);
    }
}

void mime_gif_blit(const MimeGifFrame *f, const uint8_t *indices, size_t indices_stride, uint8_t *canvas,
                   unsigned canvas_width, unsigned canvas_height, size_t canvas_stride)
{
    if (f->left >= canvas_width || f->top >= canvas_height || !f->width || !f->height)
        return;
    unsigned w = f->width, h = f->height;
    if (w > canvas_width - f->left)
        w = canvas_width - f->left;
    if (h > canvas_height - f->top)
        h = canvas_height - f->top;
    uint32_t pal[256];
    mime_gif_palette_to_bgra(f->palette, f->palette_size, f->has_transparency ? f->transparent_index : -1, pal);
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *s = indices + y * indices_stride;
        uint8_t *d = canvas + (size_t)(f->top + y) * canvas_stride + (size_t)f->left * 4;
        for (unsigned x = 0; x < w; x++, d += 4) {
            uint32_t color = pal[s[x]];
            if (color)
                __builtin_memcpy(d, &color, 4);
        }
    }
}

int mime_gif_composer_create(const MimeGif *gif, unsigned flags, MimeGifComposer **out)
{
    if (!out)
        return MIME_GIF_ERR_PARAM;
    *out = NULL;
    if (!gif)
        return MIME_GIF_ERR_PARAM;
    const MimeGifInfo *info = &gif->info;
    if (!info->width || !info->height)
        return MIME_GIF_ERR_EMPTY;
    MimeGifComposer *c = mime_alloc(&gif->alloc, sizeof(*c));
    if (!c)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(c, 0, sizeof(*c));
    c->gif = gif;
    c->width = info->width;
    c->height = info->height;
    c->current = -1;
    size_t bytes = (size_t)c->width * c->height * 4;
    c->canvas = mime_alloc(&gif->alloc, bytes);
    c->backup = gif->has_restore_previous ? mime_alloc(&gif->alloc, bytes) : NULL;
    if (!c->canvas || (gif->has_restore_previous && !c->backup)) {
        mime_gif_composer_free(c);
        return MIME_GIF_ERR_NOMEM;
    }
    if ((flags & MIME_GIF_COMPOSE_USE_BACKGROUND) && info->has_global_palette &&
        info->background_index < info->global_palette_size) {
        const uint8_t *p = info->global_palette + (size_t)info->background_index * 3;
        c->clear_color = 0xff000000u | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    *out = c;
    return MIME_GIF_OK;
}

void mime_gif_composer_free(MimeGifComposer *c)
{
    if (!c)
        return;
    MimeAllocator a = c->gif->alloc;
    mime_free(&a, c->canvas);
    mime_free(&a, c->backup);
    mime_free(&a, c->indices);
    mime_free(&a, c);
}

const uint8_t *mime_gif_composer_pixels(const MimeGifComposer *c)
{
    return c->canvas;
}

static void reset(MimeGifComposer *c)
{
    fill_rect(c, 0, 0, c->width, c->height, c->clear_color);
    c->pending_method = 0;
    c->current = -1;
}

/* a frame that paints the whole screen opaquely and does not depend on what came before. */
static bool is_key(const MimeGifComposer *c, const MimeGifFrame *f)
{
    return f->left == 0 && f->top == 0 && f->width >= c->width && f->height >= c->height && !f->has_transparency &&
           f->disposal != 3 && f->decodable && f->palette_size != 0;
}

static int draw(MimeGifComposer *c, unsigned index)
{
    const MimeGifFrame *f = &c->gif->frames[index];
    size_t stride = (size_t)c->width * 4;

    if (c->pending_method == 2) {
        fill_rect(c, c->px0, c->py0, c->px1, c->py1, c->clear_color);
    } else if (c->pending_method == 3) {
        size_t row = (size_t)(c->px1 - c->px0) * 4;
        for (unsigned y = c->py0; y < c->py1; y++)
            __builtin_memcpy(c->canvas + y * stride + (size_t)c->px0 * 4, c->backup + (size_t)(y - c->py0) * row,
                             row);
    }
    c->pending_method = 0;

    if (f->left >= c->width || f->top >= c->height || !f->width || !f->height)
        return MIME_GIF_OK;
    unsigned x1 = f->left + f->width < c->width ? f->left + f->width : c->width;
    unsigned y1 = f->top + f->height < c->height ? f->top + f->height : c->height;

    if (f->disposal == 3) {
        size_t row = (size_t)(x1 - f->left) * 4;
        for (unsigned y = f->top; y < y1; y++)
            __builtin_memcpy(c->backup + (size_t)(y - f->top) * row, c->canvas + y * stride + (size_t)f->left * 4,
                             row);
    }
    if (f->disposal >= 2) {
        c->pending_method = f->disposal;
        c->px0 = f->left;
        c->py0 = f->top;
        c->px1 = x1;
        c->py1 = y1;
    }

    int rc = MIME_GIF_OK;
    if (!f->decodable)
        return MIME_GIF_DAMAGED;
    size_t area = (size_t)f->width * f->height;
    if (area > c->indices_cap) {
        MimeAllocator a = c->gif->alloc;
        mime_free(&a, c->indices);
        c->indices = mime_alloc(&a, area);
        c->indices_cap = c->indices ? area : 0;
        if (!c->indices)
            return MIME_GIF_ERR_NOMEM;
    }
    rc = mime_gif_decode_frame(c->gif, index, c->indices, f->width);
    if (rc < 0)
        return rc == MIME_GIF_ERR_DATA ? MIME_GIF_DAMAGED : rc;
    mime_gif_blit(f, c->indices, f->width, c->canvas, c->width, c->height, stride);
    return rc;
}

int mime_gif_composer_render(MimeGifComposer *c, unsigned index)
{
    if (!c || index >= c->gif->info.frame_count)
        return MIME_GIF_ERR_PARAM;
    if (c->current >= 0 && (unsigned)c->current == index)
        return MIME_GIF_OK;

    const MimeGifFrame *frames = c->gif->frames;
    unsigned start;
    if (c->current >= 0 && index > (unsigned)c->current) {
        start = (unsigned)c->current + 1;
        for (unsigned i = index; i >= start; i--) {
            if (is_key(c, &frames[i])) {
                start = i;
                break;
            }
        }
    } else {
        reset(c);
        start = 0;
        for (unsigned i = index + 1; i-- > 0;) {
            if (is_key(c, &frames[i])) {
                start = i;
                break;
            }
        }
    }

    int worst = MIME_GIF_OK;
    for (unsigned i = start; i <= index; i++) {
        int rc = draw(c, i);
        if (rc < 0) {
            c->current = -1;
            return rc;
        }
        if (rc > worst)
            worst = rc;
        c->current = (int)i;
    }
    return worst;
}
