/*
 * gif decoding: the first image of a file, drawn on a canvas of the logical screen. Colors stay indexed.
 */
#include "picdecode.h"

typedef struct {
    const unsigned char *data;
    size_t size, pos;
} GIF_READER;

static int gif_byte(GIF_READER *r)
{
    return r->pos < r->size ? r->data[r->pos++] : -1;
}

static int gif_word(GIF_READER *r)
{
    int low = gif_byte(r), high = gif_byte(r);
    return low < 0 || high < 0 ? -1 : low | high << 8;
}

static int gif_palette(GIF_READER *r, unsigned char palette[256][4], int count)
{
    if (r->size - r->pos < (size_t)count * 3)
        return 0;
    for (int i = 0; i < count; i++) {
        palette[i][2] = r->data[r->pos++];
        palette[i][1] = r->data[r->pos++];
        palette[i][0] = r->data[r->pos++];
        palette[i][3] = 0;
    }
    return 1;
}

/* the sub-blocks of one extension or one image, ended by a zero length. */
static int gif_skip_blocks(GIF_READER *r)
{
    for (;;) {
        int length = gif_byte(r);
        if (length < 0)
            return 0;
        if (length == 0)
            return 1;
        if (r->size - r->pos < (size_t)length)
            return 0;
        r->pos += length;
    }
}

/* the sub-blocks of image data, taken one byte at a time. */
typedef struct {
    GIF_READER *r;
    int left;
    int done;
} GIF_BLOCKS;

static int gif_data_byte(GIF_BLOCKS *b)
{
    while (b->left == 0) {
        if (b->done)
            return -1;
        int length = gif_byte(b->r);
        if (length <= 0) {
            b->done = 1;
            return -1;
        }
        b->left = length;
    }
    int value = gif_byte(b->r);
    if (value < 0) {
        b->done = 1;
        b->left = 0;
        return -1;
    }
    b->left--;
    return value;
}

/* lzw decoding into the indexed rows of the frame; a truncated stream leaves the rest of the frame as it was. */
static void gif_lzw(GIF_READER *r, int minimum, unsigned char *frame, int width, int height, int interlaced)
{
    static const int pass_start[4] = {0, 4, 2, 1};
    static const int pass_step[4] = {8, 8, 4, 2};
    unsigned short prefix[4096];
    unsigned char suffix[4096];
    unsigned char stack[4097];
    GIF_BLOCKS blocks = {r, 0, 0};
    int clear = 1 << minimum, end = clear + 1;
    int next = end + 1, size = minimum + 1, previous = -1;
    unsigned long bits = 0;
    int have = 0;
    int pass = 0, row = 0, column = 0;
    for (int i = 0; i < clear; i++) {
        prefix[i] = 0;
        suffix[i] = (unsigned char)i;
    }
    for (;;) {
        while (have < size) {
            int value = gif_data_byte(&blocks);
            if (value < 0)
                return;
            bits |= (unsigned long)value << have;
            have += 8;
        }
        int code = (int)(bits & ((1UL << size) - 1));
        bits >>= size;
        have -= size;
        if (code == clear) {
            next = end + 1;
            size = minimum + 1;
            previous = -1;
            continue;
        }
        if (code == end)
            return;
        int top = 0;
        if (previous < 0) {
            if (code >= clear)
                return;
            stack[top++] = (unsigned char)code;
        } else {
            if (code > next)
                return;
            /* the code being defined is the previous string followed by its own first byte. */
            int current = code == next ? previous : code;
            int first = current;
            while (first >= clear)
                first = prefix[first];
            if (code == next)
                stack[top++] = (unsigned char)first;
            while (current >= clear) {
                stack[top++] = suffix[current];
                current = prefix[current];
            }
            stack[top++] = (unsigned char)current;
            if (next < 4096) {
                prefix[next] = (unsigned short)previous;
                suffix[next] = (unsigned char)first;
                next++;
                if (next == (1 << size) && size < 12)
                    size++;
            }
        }
        previous = code;
        while (top > 0) {
            unsigned char value = stack[--top];
            if (row < height)
                frame[(size_t)row * width + column] = value;
            if (++column == width) {
                column = 0;
                if (!interlaced) {
                    row++;
                } else {
                    row += pass_step[pass];
                    while (row >= height && pass < 3) {
                        pass++;
                        row = pass_start[pass];
                    }
                }
                if (row >= height)
                    return;
            }
        }
    }
}

int AutDecodeGif(const unsigned char *data, size_t size, AUT_IMAGE *image)
{
    GIF_READER reader = {data, size, 0};
    GIF_READER *r = &reader;
    /* the signature is accepted in lower case too, as the first word of the file may be either. */
    if (size < 13 || (data[0] | 0x20) != 'g' || (data[1] | 0x20) != 'i' || (data[2] | 0x20) != 'f' || data[3] != '8' ||
        (data[4] != '7' && data[4] != '9') || (data[5] | 0x20) != 'a')
        return 0;
    r->pos = 6;
    int screen_width = gif_word(r), screen_height = gif_word(r);
    int packed = gif_byte(r), background = gif_byte(r);
    gif_byte(r);
    if (screen_width < 0 || screen_height < 0 || packed < 0 || background < 0)
        return 0;
    unsigned char global[256][4] = {};
    int global_count = 0;
    if (packed & 0x80) {
        global_count = 2 << (packed & 7);
        if (!gif_palette(r, global, global_count))
            return 0;
    }
    int transparent = -1;
    for (;;) {
        int block = gif_byte(r);
        if (block == 0x21) {
            int label = gif_byte(r);
            if (label == 0xF9) {
                int length = gif_byte(r);
                if (length >= 4 && r->size - r->pos >= (size_t)length) {
                    int flags = r->data[r->pos];
                    transparent = flags & 1 ? r->data[r->pos + 3] : -1;
                }
                if (length < 0 || r->size - r->pos < (size_t)length)
                    return 0;
                r->pos += length;
                if (!gif_skip_blocks(r))
                    return 0;
            } else if (label < 0 || !gif_skip_blocks(r)) {
                return 0;
            }
        } else if (block == 0x2C) {
            break;
        } else {
            return 0;
        }
    }
    int left = gif_word(r), top = gif_word(r), width = gif_word(r), height = gif_word(r);
    int flags = gif_byte(r);
    if (left < 0 || top < 0 || width <= 0 || height <= 0 || flags < 0)
        return 0;
    unsigned char local[256][4] = {};
    int count = global_count;
    unsigned char (*palette)[4] = global;
    if (flags & 0x80) {
        count = 2 << (flags & 7);
        if (!gif_palette(r, local, count))
            return 0;
        palette = local;
    }
    int minimum = gif_byte(r);
    if (minimum < 1 || minimum > 8)
        return 0;
    if (screen_width == 0 || screen_height == 0) {
        screen_width = left + width;
        screen_height = top + height;
    }
    if (screen_width > 32767 || screen_height > 32767 || width > 32767 || height > 32767)
        return 0;
    unsigned char *frame = AUT_ALLOC((size_t)width * height);
    unsigned char *canvas = AUT_ALLOC((size_t)screen_width * screen_height);
    if (!frame || !canvas) {
        AUT_FREE(frame);
        AUT_FREE(canvas);
        return 0;
    }
    gif_lzw(r, minimum, frame, width, height, flags & 0x40);
    int fill = transparent >= 0 ? transparent : (global_count && palette == global && background < count ? background : 0);
    memset(canvas, fill, (size_t)screen_width * screen_height);
    for (int y = 0; y < height && top + y < screen_height; y++) {
        for (int x = 0; x < width && left + x < screen_width; x++)
            canvas[(size_t)(top + y) * screen_width + left + x] = frame[(size_t)y * width + x];
    }
    AUT_FREE(frame);
    if (count == 0) {
        /* a file without any palette is shown in black and white. */
        count = 2;
        local[1][0] = local[1][1] = local[1][2] = 255;
        palette = local;
    }
    memset(image, 0, sizeof(*image));
    image->width = screen_width;
    image->height = screen_height;
    image->bpp = 8;
    image->colors = count;
    image->transparent = transparent >= 0 && transparent < count ? transparent : -1;
    memcpy(image->palette, palette, sizeof(image->palette));
    image->pixels = canvas;
    return 1;
}

void AutImageFree(AUT_IMAGE *image)
{
    AUT_FREE(image->pixels);
    image->pixels = nullptr;
}
