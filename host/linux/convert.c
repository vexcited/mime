/*
 * conversions between host and windows clipboard formats, see convert.h.
 */
#include "convert.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGE_SIDE 16384
#define MAX_IMAGE_PIXELS (64u * 1024 * 1024)

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------------ text */

uint8_t *mime_conv_text_to_guest(const char *utf8, size_t len, size_t *out_size)
{
    if (!utf8 || !len)
        return nullptr;
    char *z = malloc(len + 1);
    uint16_t *raw = malloc((len + 1) * sizeof(uint16_t));
    uint16_t *text = malloc((len + 1) * 2 * sizeof(uint16_t));
    if (!z || !raw || !text) {
        free(z);
        free(raw);
        free(text);
        return nullptr;
    }
    memcpy(z, utf8, len);
    z[len] = 0;
    size_t n = mime_utf8_to_utf16(raw, len + 1, z);
    size_t chars = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
            text[chars++] = '\r';
        text[chars++] = raw[i];
    }
    free(z);
    free(raw);
    *out_size = chars * sizeof(uint16_t);
    return (uint8_t *)text;
}

char *mime_conv_text_from_guest(const uint16_t *text, size_t chars, size_t *out_len)
{
    size_t n = 0;
    while (n < chars && text[n])
        n++;
    uint16_t *copy = malloc((n ? n : 1) * sizeof(uint16_t));
    char *utf8 = malloc(n * 3 + 1);
    if (!copy || !utf8) {
        free(copy);
        free(utf8);
        return nullptr;
    }
    size_t kept = 0;
    for (size_t i = 0; i < n; i++)
        if (!(text[i] == '\r' && i + 1 < n && text[i + 1] == '\n'))
            copy[kept++] = text[i];
    *out_len = mime_utf16_to_utf8(utf8, n * 3 + 1, copy, kept);
    free(copy);
    return utf8;
}

/* ------------------------------------------------------------------ images */

#define BI_RGB 0
#define BI_BITFIELDS 3
#define BI_ALPHABITFIELDS 6

/* a device independent bitmap read as 0xAARRGGBB pixels, top row first. */
typedef struct Argb {
    uint32_t *pixels;
    int width, height;
    bool alpha; /* the bitmap says what its alpha is */
} Argb;

/* where the pixels of a dib are and how they are laid out. */
typedef struct DibLayout {
    uint32_t header;
    int width, height; /* height as the header has it */
    uint32_t bits, compression;
    uint32_t mask[4];
    uint32_t colors;
    size_t palette;     /* offset of the color table */
    size_t pixels;      /* offset of the first row */
    size_t stride;
    bool has_alpha;
} DibLayout;

static bool dib_layout(const uint8_t *dib, size_t size, DibLayout *l)
{
    if (size < 40)
        return false;
    memset(l, 0, sizeof(*l));
    l->header = rd32(dib);
    if (l->header < 40 || l->header > size)
        return false;
    l->width = (int32_t)rd32(dib + 4);
    l->height = (int32_t)rd32(dib + 8);
    l->bits = rd16(dib + 14);
    l->compression = rd32(dib + 16);
    uint32_t used = rd32(dib + 32);
    if (rd16(dib + 12) != 1 || l->width <= 0 || l->height == 0 || l->width > MAX_IMAGE_SIDE ||
        (l->height > MAX_IMAGE_SIDE) || (l->height < -MAX_IMAGE_SIDE))
        return false;
    if ((uint64_t)l->width * (uint32_t)(l->height < 0 ? -l->height : l->height) > MAX_IMAGE_PIXELS)
        return false;
    if (l->bits != 1 && l->bits != 4 && l->bits != 8 && l->bits != 16 && l->bits != 24 && l->bits != 32)
        return false;
    if (l->compression != BI_RGB && l->compression != BI_BITFIELDS && l->compression != BI_ALPHABITFIELDS)
        return false;
    size_t at = l->header;
    bool fields = l->compression == BI_BITFIELDS || l->compression == BI_ALPHABITFIELDS;
    if (fields && (l->bits == 16 || l->bits == 32)) {
        if (l->header >= 52) {
            l->mask[0] = rd32(dib + 40);
            l->mask[1] = rd32(dib + 44);
            l->mask[2] = rd32(dib + 48);
            if (l->header >= 56)
                l->mask[3] = rd32(dib + 52);
        } else {
            size_t count = l->compression == BI_ALPHABITFIELDS ? 4 : 3;
            if (size < at + count * 4)
                return false;
            for (size_t i = 0; i < count; i++)
                l->mask[i] = rd32(dib + at + i * 4);
            at += count * 4;
        }
    } else if (l->bits == 16) {
        l->mask[0] = 0x7C00;
        l->mask[1] = 0x03E0;
        l->mask[2] = 0x001F;
    } else if (l->bits == 32) {
        l->mask[0] = 0x00FF0000;
        l->mask[1] = 0x0000FF00;
        l->mask[2] = 0x000000FF;
    }
    /* alpha is only believed when the header says where it lives. */
    l->has_alpha = l->mask[3] != 0 && (l->header >= 108 || l->compression == BI_ALPHABITFIELDS);
    l->colors = l->bits <= 8 ? (used ? used : 1u << l->bits) : used;
    if (l->colors > 256 && l->bits <= 8)
        return false;
    l->palette = at;
    at += (size_t)l->colors * 4;
    l->pixels = at;
    l->stride = (((size_t)l->width * l->bits + 31) / 32) * 4;
    size_t rows = (size_t)(l->height < 0 ? -l->height : l->height);
    if (l->pixels > size || size - l->pixels < l->stride * rows)
        return false;
    return true;
}

static uint32_t scale_channel(uint32_t value, uint32_t mask)
{
    if (!mask)
        return 0;
    uint32_t shifted = value & mask;
    while (!(mask & 1)) {
        mask >>= 1;
        shifted >>= 1;
    }
    return mask == 255 ? shifted : (shifted * 255 + mask / 2) / mask;
}

static bool dib_to_argb(const uint8_t *dib, size_t size, Argb *out)
{
    DibLayout l;
    if (!dib_layout(dib, size, &l))
        return false;
    int height = l.height < 0 ? -l.height : l.height;
    uint32_t *pixels = malloc((size_t)l.width * (size_t)height * sizeof(uint32_t));
    if (!pixels)
        return false;
    for (int y = 0; y < height; y++) {
        /* a positive height stores the bottom row first. */
        const uint8_t *row = dib + l.pixels + (size_t)(l.height > 0 ? height - 1 - y : y) * l.stride;
        uint32_t *dst = pixels + (size_t)y * (size_t)l.width;
        for (int x = 0; x < l.width; x++) {
            uint32_t a = 255, r, g, b;
            switch (l.bits) {
            case 1:
            case 4:
            case 8: {
                uint32_t index;
                if (l.bits == 8)
                    index = row[x];
                else if (l.bits == 4)
                    index = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                else
                    index = (row[x / 8] >> (7 - (x & 7))) & 1;
                if (index < l.colors) {
                    const uint8_t *c = dib + l.palette + (size_t)index * 4;
                    b = c[0];
                    g = c[1];
                    r = c[2];
                } else {
                    r = g = b = 0;
                }
                break;
            }
            case 16: {
                uint32_t v = rd16(row + (size_t)x * 2);
                r = scale_channel(v, l.mask[0]);
                g = scale_channel(v, l.mask[1]);
                b = scale_channel(v, l.mask[2]);
                break;
            }
            case 24:
                b = row[x * 3];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
                break;
            default: {
                uint32_t v = rd32(row + (size_t)x * 4);
                r = scale_channel(v, l.mask[0]);
                g = scale_channel(v, l.mask[1]);
                b = scale_channel(v, l.mask[2]);
                if (l.has_alpha)
                    a = scale_channel(v, l.mask[3]);
                break;
            }
            }
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    if (l.has_alpha) {
        /* a producer that names an alpha channel and leaves it empty means the bitmap to be opaque. */
        size_t count = (size_t)l.width * (size_t)height, i = 0;
        while (i < count && !(pixels[i] >> 24))
            i++;
        if (i == count)
            for (i = 0; i < count; i++)
                pixels[i] |= 0xFF000000;
    }
    out->pixels = pixels;
    out->width = l.width;
    out->height = height;
    out->alpha = l.has_alpha;
    return true;
}

/* the bytes an sdl stream in dynamic memory holds. */
static uint8_t *take_stream(SDL_IOStream *io, size_t *out_size)
{
    Sint64 size = SDL_TellIO(io);
    void *data = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    uint8_t *copy = size > 0 && data ? malloc((size_t)size) : nullptr;
    if (copy) {
        memcpy(copy, data, (size_t)size);
        *out_size = (size_t)size;
    }
    return copy;
}

static uint8_t *argb_to_png(uint32_t *pixels, int width, int height, size_t *out_size)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888, pixels, width * 4);
    if (!s)
        return nullptr;
    SDL_IOStream *io = SDL_IOFromDynamicMem();
    uint8_t *png = nullptr;
    if (io) {
        if (SDL_SavePNG_IO(s, io, false))
            png = take_stream(io, out_size);
        SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    return png;
}

uint8_t *mime_conv_dib_to_png(const void *dib, size_t size, size_t *out_size)
{
    Argb a;
    if (!dib_to_argb(dib, size, &a))
        return nullptr;
    uint8_t *png = argb_to_png(a.pixels, a.width, a.height, out_size);
    free(a.pixels);
    return png;
}

uint8_t *mime_conv_dib_to_bmp(const void *dib, size_t size, size_t *out_size)
{
    DibLayout l;
    if (!dib_layout(dib, size, &l))
        return nullptr;
    uint8_t *bmp = malloc(14 + size);
    if (!bmp)
        return nullptr;
    bmp[0] = 'B';
    bmp[1] = 'M';
    wr32(bmp + 2, (uint32_t)(14 + size));
    wr32(bmp + 6, 0);
    wr32(bmp + 10, (uint32_t)(14 + l.pixels));
    memcpy(bmp + 14, dib, size);
    *out_size = 14 + size;
    return bmp;
}

/* the pixels of a png or bmp file as 0xAARRGGBB, top row first. */
static bool decode_image(const void *file, size_t size, Argb *out)
{
    SDL_IOStream *io = SDL_IOFromConstMem(file, size);
    if (!io)
        return false;
    SDL_Surface *loaded = SDL_LoadSurface_IO(io, true);
    if (!loaded)
        return false;
    SDL_Surface *s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(loaded);
    if (!s)
        return false;
    bool ok = false;
    if (s->w > 0 && s->h > 0 && s->w <= MAX_IMAGE_SIDE && s->h <= MAX_IMAGE_SIDE &&
        (uint64_t)s->w * (uint64_t)s->h <= MAX_IMAGE_PIXELS) {
        uint32_t *pixels = malloc((size_t)s->w * (size_t)s->h * 4);
        if (pixels) {
            for (int y = 0; y < s->h; y++)
                memcpy(pixels + (size_t)y * (size_t)s->w, (const uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch,
                       (size_t)s->w * 4);
            out->pixels = pixels;
            out->width = s->w;
            out->height = s->h;
            out->alpha = true;
            ok = true;
        }
    }
    SDL_DestroySurface(s);
    return ok;
}

uint8_t *mime_conv_image_to_png(const void *file, size_t size, size_t *out_size)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size >= 8 && !memcmp(file, signature, 8)) {
        /* a png goes on as it is. */
        uint8_t *copy = malloc(size);
        if (copy) {
            memcpy(copy, file, size);
            *out_size = size;
        }
        return copy;
    }
    Argb a;
    if (!decode_image(file, size, &a))
        return nullptr;
    uint8_t *png = argb_to_png(a.pixels, a.width, a.height, out_size);
    free(a.pixels);
    return png;
}

uint8_t *mime_conv_image_to_dib(const void *file, size_t size, bool v5, size_t *out_size)
{
    Argb a;
    if (!decode_image(file, size, &a))
        return nullptr;
    size_t header = v5 ? 124 : 52;
    size_t stride = (size_t)a.width * 4;
    size_t total = header + stride * (size_t)a.height;
    uint8_t *dib = calloc(1, total);
    if (!dib) {
        free(a.pixels);
        return nullptr;
    }
    wr32(dib, v5 ? 124 : 40);
    wr32(dib + 4, (uint32_t)a.width);
    wr32(dib + 8, (uint32_t)a.height);
    wr16(dib + 12, 1);
    wr16(dib + 14, 32);
    wr32(dib + 16, BI_BITFIELDS);
    wr32(dib + 20, (uint32_t)(stride * (size_t)a.height));
    wr32(dib + 40, 0x00FF0000);
    wr32(dib + 44, 0x0000FF00);
    wr32(dib + 48, 0x000000FF);
    if (v5) {
        wr32(dib + 52, 0xFF000000);
        wr32(dib + 56, 0x73524742); /* LCS_sRGB */
        wr32(dib + 108, 4);         /* LCS_GM_IMAGES */
    }
    /* the plain format keeps its three masks right after the 40 byte header. what it shows through goes on white. */
    uint8_t *rows = dib + header;
    for (int y = 0; y < a.height; y++) {
        uint32_t *src = a.pixels + (size_t)(a.height - 1 - y) * (size_t)a.width;
        uint8_t *dst = rows + (size_t)y * stride;
        for (int x = 0; x < a.width; x++) {
            uint32_t p = src[x], alpha = p >> 24;
            uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            if (!v5 && alpha != 255) {
                r = (r * alpha + 255 * (255 - alpha) + 127) / 255;
                g = (g * alpha + 255 * (255 - alpha) + 127) / 255;
                b = (b * alpha + 255 * (255 - alpha) + 127) / 255;
                alpha = 255;
            }
            dst[x * 4] = (uint8_t)b;
            dst[x * 4 + 1] = (uint8_t)g;
            dst[x * 4 + 2] = (uint8_t)r;
            dst[x * 4 + 3] = (uint8_t)alpha;
        }
    }
    free(a.pixels);
    *out_size = total;
    return dib;
}

/* ------------------------------------------------------------------ html */

#define START_MARK "<!--StartFragment-->"
#define END_MARK "<!--EndFragment-->"

static const char *find_text(const char *text, size_t size, const char *needle)
{
    size_t n = strlen(needle);
    if (n > size)
        return nullptr;
    for (size_t i = 0; i + n <= size; i++)
        if (!memcmp(text + i, needle, n))
            return text + i;
    return nullptr;
}

static const char *find_text_nocase(const char *text, size_t size, const char *needle)
{
    size_t n = strlen(needle);
    if (n > size)
        return nullptr;
    for (size_t i = 0; i + n <= size; i++)
        if (!strncasecmp(text + i, needle, n))
            return text + i;
    return nullptr;
}

uint8_t *mime_conv_html_to_guest(const void *html, size_t size, size_t *out_size)
{
    if (!html || !size)
        return nullptr;
    const uint8_t *in = html;
    char *owned = nullptr;
    const char *text = (const char *)in;
    size_t len = size;
    if (size >= 2 && ((in[0] == 0xFF && in[1] == 0xFE) || (in[0] == 0xFE && in[1] == 0xFF))) {
        /* utf-16 with a byte order mark, as some host browsers copy. */
        bool big = in[0] == 0xFE;
        size_t chars = (size - 2) / 2;
        uint16_t *wide = malloc((chars + 1) * sizeof(uint16_t));
        owned = malloc(chars * 3 + 1);
        if (!wide || !owned) {
            free(wide);
            free(owned);
            return nullptr;
        }
        for (size_t i = 0; i < chars; i++)
            wide[i] = big ? (uint16_t)((in[2 + i * 2] << 8) | in[3 + i * 2]) : rd16(in + 2 + i * 2);
        len = mime_utf16_to_utf8(owned, chars * 3 + 1, wide, chars);
        free(wide);
        text = owned;
    } else if (size >= 3 && in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xBF) {
        text += 3;
        len -= 3;
    }
    while (len && text[len - 1] == 0)
        len--;
#define HEADER 105
    static const char template[] = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\n"
                                   "EndFragment:%010d\r\n";
    const char *start = find_text(text, len, START_MARK), *end = find_text(text, len, END_MARK);
    static const char prefix[] = "<html>\r\n<body>\r\n" START_MARK, suffix[] = END_MARK "\r\n</body>\r\n</html>";
    bool marked = start && end && start + strlen(START_MARK) <= end;
    size_t body = marked ? len : sizeof(prefix) - 1 + len + sizeof(suffix) - 1;
    uint8_t *out = malloc(HEADER + body + 1);
    if (!out) {
        free(owned);
        return nullptr;
    }
    int fragment_start, fragment_end;
    if (marked) {
        memcpy(out + HEADER, text, len);
        fragment_start = HEADER + (int)(start - text) + (int)strlen(START_MARK);
        fragment_end = HEADER + (int)(end - text);
    } else {
        memcpy(out + HEADER, prefix, sizeof(prefix) - 1);
        memcpy(out + HEADER + sizeof(prefix) - 1, text, len);
        memcpy(out + HEADER + sizeof(prefix) - 1 + len, suffix, sizeof(suffix) - 1);
        fragment_start = HEADER + (int)sizeof(prefix) - 1;
        fragment_end = fragment_start + (int)len;
    }
    char head[HEADER + 1];
    int written = snprintf(head, sizeof(head), template, HEADER, HEADER + (int)body, fragment_start, fragment_end);
    if (written != HEADER) {
        free(out);
        free(owned);
        return nullptr;
    }
    memcpy(out, head, HEADER);
    out[HEADER + body] = 0;
    *out_size = HEADER + body + 1;
    free(owned);
    return out;
}

/* the decimal value of a header field, -1 when it is missing. */
static long html_field(const char *data, size_t size, const char *key)
{
    size_t limit = size < 512 ? size : 512;
    const char *at = find_text(data, limit, key);
    if (!at)
        return -1;
    at += strlen(key);
    char *end;
    long value = strtol(at, &end, 10);
    return end == at ? -1 : value;
}

uint8_t *mime_conv_html_from_guest(const void *data, size_t size, size_t *out_size)
{
    const char *text = data;
    while (size && text[size - 1] == 0)
        size--;
    if (!size)
        return nullptr;
    long start_html = html_field(text, size, "StartHTML:"), end_html = html_field(text, size, "EndHTML:");
    long start_fragment = html_field(text, size, "StartFragment:"), end_fragment = html_field(text, size, "EndFragment:");
    const char *doc;
    size_t doc_size;
    if (start_html >= 0 && end_html > start_html && (size_t)end_html <= size) {
        doc = text + start_html;
        doc_size = (size_t)(end_html - start_html);
    } else if (start_fragment >= 0 && end_fragment > start_fragment && (size_t)end_fragment <= size) {
        doc = text + start_fragment;
        doc_size = (size_t)(end_fragment - start_fragment);
    } else {
        const char *lt = memchr(text, '<', size);
        doc = lt ? lt : text;
        doc_size = size - (size_t)(doc - text);
    }
    static const char meta[] = "<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\">";
    const char *probe = doc;
    size_t left = doc_size;
    while (left && isspace((unsigned char)*probe)) {
        probe++;
        left--;
    }
    bool doctype = left >= 9 && !strncasecmp(probe, "<!doctype", 9);
    size_t scan = doc_size < 2048 ? doc_size : 2048;
    bool charset = find_text_nocase(doc, scan, "charset") != nullptr;
    size_t extra = doctype || charset ? 0 : sizeof(meta) - 1;
    uint8_t *out = malloc(extra + doc_size + 1);
    if (!out)
        return nullptr;
    memcpy(out, meta, extra);
    memcpy(out + extra, doc, doc_size);
    out[extra + doc_size] = 0;
    *out_size = extra + doc_size;
    return out;
}

/* ------------------------------------------------------------------ files and urls */

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool mime_conv_uri_to_path(const char *uri, size_t len, char *out, size_t size)
{
    if (len < 6 || strncasecmp(uri, "file:", 5))
        return false;
    const char *p = uri + 5;
    size_t left = len - 5;
    if (left >= 2 && p[0] == '/' && p[1] == '/') {
        p += 2;
        left -= 2;
        /* the authority is empty or this machine. */
        if (left >= 9 && !strncasecmp(p, "localhost", 9) && (left == 9 || p[9] == '/')) {
            p += 9;
            left -= 9;
        } else if (!left || *p != '/') {
            return false;
        }
    }
    if (!left || *p != '/')
        return false;
    size_t o = 0;
    for (size_t i = 0; i < left; i++) {
        int c = (unsigned char)p[i];
        if (c == '%' && i + 2 < left) {
            int hi = hex_digit((unsigned char)p[i + 1]), lo = hex_digit((unsigned char)p[i + 2]);
            if (hi >= 0 && lo >= 0) {
                c = hi * 16 + lo;
                i += 2;
            }
        } else if (c == '?' || c == '#') {
            break;
        }
        if (o + 1 >= size || c == 0)
            return false;
        out[o++] = (char)c;
    }
    out[o] = 0;
    return o > 0;
}

/* the lines of a uri list, without comments or empty lines. */
typedef struct Lines {
    const char *text;
    size_t len, at;
} Lines;

static bool next_line(Lines *l, const char **line, size_t *n)
{
    while (l->at < l->len) {
        size_t start = l->at;
        while (l->at < l->len && l->text[l->at] != '\n' && l->text[l->at] != '\r')
            l->at++;
        size_t end = l->at;
        while (l->at < l->len && (l->text[l->at] == '\n' || l->text[l->at] == '\r'))
            l->at++;
        if (end > start && l->text[start] != '#') {
            *line = l->text + start;
            *n = end - start;
            return true;
        }
    }
    return false;
}

uint8_t *mime_conv_uris_to_hdrop(const MimePaths *paths, const char *list, size_t len, size_t *out_size)
{
    size_t capacity = 20 + (len + 1) * 2 * sizeof(uint16_t) + 4;
    uint8_t *out = calloc(1, capacity);
    if (!out)
        return nullptr;
    wr32(out, 20);
    wr32(out + 16, 1);
    uint16_t *dst = (uint16_t *)(out + 20);
    size_t used = 0;
    Lines lines = {list, len, 0};
    const char *line;
    size_t n;
    int files = 0;
    while (next_line(&lines, &line, &n)) {
        char host[MIME_PATH_MAX], guest[MIME_PATH_MAX];
        if (!mime_conv_uri_to_path(line, n, host, sizeof(host)))
            continue;
        mime_paths_to_guest(paths, host, guest, sizeof(guest));
        size_t chars = mime_utf8_to_utf16(dst + used, (capacity - 20) / 2 - used - 1, guest);
        dst[used + chars] = 0;
        used += chars + 1;
        files++;
    }
    if (!files) {
        free(out);
        return nullptr;
    }
    dst[used] = 0;
    *out_size = 20 + (used + 1) * sizeof(uint16_t);
    return out;
}

static bool uri_safe(unsigned char c)
{
    return isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
}

char *mime_conv_hdrop_to_uris(const MimePaths *paths, const void *data, size_t size, size_t *out_len)
{
    const uint8_t *in = data;
    if (size < 20)
        return nullptr;
    uint32_t offset = rd32(in);
    bool wide = rd32(in + 16) != 0;
    if (offset < 20 || offset >= size)
        return nullptr;
    size_t capacity = size * 9 + 64;
    char *out = malloc(capacity);
    if (!out)
        return nullptr;
    size_t o = 0;
    const uint8_t *p = in + offset, *end = in + size;
    int files = 0;
    while (p < end) {
        char guest[MIME_PATH_MAX], host[MIME_PATH_MAX];
        size_t chars = 0;
        if (wide) {
            size_t max = (size_t)(end - p) / 2;
            while (chars < max && rd16(p + chars * 2))
                chars++;
            if (!chars)
                break;
            uint16_t tmp[MIME_PATH_MAX];
            size_t copy = chars < MIME_PATH_MAX - 1 ? chars : MIME_PATH_MAX - 1;
            for (size_t i = 0; i < copy; i++)
                tmp[i] = rd16(p + i * 2);
            mime_utf16_to_utf8(guest, sizeof(guest), tmp, copy);
            p += (chars + 1) * 2;
        } else {
            while (p + chars < end && p[chars])
                chars++;
            if (!chars)
                break;
            size_t copy = chars < sizeof(guest) - 1 ? chars : sizeof(guest) - 1;
            memcpy(guest, p, copy);
            guest[copy] = 0;
            p += chars + 1;
        }
        if (!mime_paths_to_host(paths, guest, host, sizeof(host)))
            continue;
        memcpy(out + o, "file://", 7);
        o += 7;
        for (const char *c = host; *c && o + 4 < capacity; c++) {
            if (uri_safe((unsigned char)*c)) {
                out[o++] = *c;
            } else {
                o += (size_t)snprintf(out + o, 4, "%%%02X", (unsigned char)*c);
            }
        }
        out[o++] = '\r';
        out[o++] = '\n';
        files++;
    }
    if (!files) {
        free(out);
        return nullptr;
    }
    out[o] = 0;
    *out_len = o;
    return out;
}

uint8_t *mime_conv_uris_to_url(const char *list, size_t len, size_t *out_size)
{
    Lines lines = {list, len, 0};
    const char *line;
    size_t n;
    while (next_line(&lines, &line, &n)) {
        if (n >= 5 && !strncasecmp(line, "file:", 5))
            continue;
        /* a scheme first, as in a url. */
        size_t colon = 0;
        while (colon < n && (isalnum((unsigned char)line[colon]) || line[colon] == '+' || line[colon] == '-' ||
                             line[colon] == '.'))
            colon++;
        if (colon == 0 || colon >= n || line[colon] != ':')
            continue;
        char *z = malloc(n + 1);
        uint16_t *wide = malloc((n + 1) * sizeof(uint16_t));
        if (!z || !wide) {
            free(z);
            free(wide);
            return nullptr;
        }
        memcpy(z, line, n);
        z[n] = 0;
        size_t chars = mime_utf8_to_utf16(wide, n + 1, z);
        wide[chars] = 0;
        free(z);
        *out_size = (chars + 1) * sizeof(uint16_t);
        return (uint8_t *)wide;
    }
    return nullptr;
}

char *mime_conv_url_to_uris(const void *text, size_t size, bool wide, size_t *out_len)
{
    const uint8_t *in = text;
    size_t chars = 0;
    char *out;
    if (wide) {
        size_t max = size / 2;
        while (chars < max && rd16(in + chars * 2))
            chars++;
        if (!chars)
            return nullptr;
        uint16_t *tmp = malloc(chars * sizeof(uint16_t));
        out = malloc(chars * 3 + 3);
        if (!tmp || !out) {
            free(tmp);
            free(out);
            return nullptr;
        }
        for (size_t i = 0; i < chars; i++)
            tmp[i] = rd16(in + i * 2);
        *out_len = mime_utf16_to_utf8(out, chars * 3 + 1, tmp, chars);
        free(tmp);
    } else {
        while (chars < size && in[chars])
            chars++;
        if (!chars)
            return nullptr;
        out = malloc(chars + 3);
        if (!out)
            return nullptr;
        memcpy(out, in, chars);
        *out_len = chars;
    }
    out[(*out_len)++] = '\r';
    out[(*out_len)++] = '\n';
    out[*out_len] = 0;
    return out;
}
