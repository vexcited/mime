#include "gif.h"
#include "gif_internal.h"

#define BLOCK_EXTENSION 0x21
#define BLOCK_IMAGE 0x2c
#define BLOCK_TRAILER 0x3b

#define EXT_PLAIN_TEXT 0x01
#define EXT_GRAPHIC_CONTROL 0xf9
#define EXT_COMMENT 0xfe
#define EXT_APPLICATION 0xff

#define XMP_TRAILER_SIZE 258

typedef struct Pending {
    bool has;
    uint8_t disposal_raw;
    bool user_input;
    bool has_transparency;
    uint8_t transparent_index;
    uint16_t delay;
} Pending;

typedef struct Scan {
    const uint8_t *d;
    size_t n;
    size_t pos;
    MimeGifInfo info;
    MimeGifFrame *frames; /* NULL while only counting. */
    MimeGifExtension *exts;
    uint32_t frame_count;
    uint32_t ext_count;
    bool restore_previous;
    bool xmp_trailer_absent;
    unsigned max_right;
    unsigned max_bottom;
} Scan;

bool mime_gif_is_gif(const void *data, size_t size)
{
    const uint8_t *d = data;
    return size >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8' && (d[4] == '7' || d[4] == '9') &&
           d[5] == 'a';
}

static inline unsigned load16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

/* skips sub-blocks up to and including the terminator. false when the data ends first. */
static bool skip_chain(const uint8_t *d, size_t n, size_t *pos, size_t *payload)
{
    size_t p = *pos, total = 0;
    while (p < n) {
        size_t len = d[p++];
        if (!len) {
            *pos = p;
            *payload = total;
            return true;
        }
        size_t avail = n - p;
        if (len > avail) {
            total += avail;
            p = n;
            break;
        }
        total += len;
        p += len;
    }
    *pos = n;
    *payload = total;
    return false;
}

/* finds the 258 byte trailer that closes a raw xmp packet, see the xmp specification. */
static bool find_xmp_trailer(const uint8_t *d, size_t n, size_t start, size_t *found)
{
    if (n < XMP_TRAILER_SIZE || start > n - XMP_TRAILER_SIZE)
        return false;
    size_t last = n - XMP_TRAILER_SIZE;
    for (size_t p = start; p <= last; p++) {
        if (d[p] != 0x01 || d[p + 1] != 0xff || d[p + 2] != 0xfe)
            continue;
        unsigned i = 3;
        while (i <= 256 && d[p + i] == ((0x100u - i) & 0xff))
            i++;
        if (i == 257 && d[p + 257] == 0) {
            *found = p;
            return true;
        }
    }
    return false;
}

static bool same_text(const uint8_t *a, const char *b, size_t n)
{
    return __builtin_memcmp(a, b, n) == 0;
}

/* a resynced image descriptor must look sane, else its separator byte was junk. */
static bool image_plausible(const Scan *s, size_t sep)
{
    const uint8_t *d = s->d;
    size_t pos = sep + 1;
    if (s->n - pos < 10)
        return false;
    unsigned left = load16(d + pos), top = load16(d + pos + 2), w = load16(d + pos + 4), h = load16(d + pos + 6);
    unsigned packed = d[pos + 8];
    size_t p = pos + 9;
    if (packed & 0x80)
        p += (size_t)3 << ((packed & 7) + 1);
    if (p >= s->n)
        return false;
    unsigned mcs = d[p];
    if (mcs < 2 || mcs > 8)
        return false;
    if (s->info.declared_width && left + w > s->info.declared_width)
        return false;
    if (s->info.declared_height && top + h > s->info.declared_height)
        return false;
    return true;
}

static uint8_t app_kind(const uint8_t *hdr, size_t size, bool *netscape)
{
    *netscape = false;
    if (size != 11)
        return MIME_GIF_APP_OTHER;
    if (same_text(hdr, "NETSCAPE2.0", 11) || same_text(hdr, "ANIMEXTS1.0", 11)) {
        *netscape = true;
        return MIME_GIF_APP_NETSCAPE;
    }
    if (same_text(hdr, "XMP DataXMP", 11))
        return MIME_GIF_APP_XMP;
    if (same_text(hdr, "ICCRGBG1012", 11))
        return MIME_GIF_APP_ICC;
    return MIME_GIF_APP_OTHER;
}

/* parses one extension after its label. false when the data ends inside it. */
static bool scan_extension(Scan *s, uint8_t label, Pending *pend, uint32_t *warn)
{
    const uint8_t *d = s->d;
    size_t n = s->n;
    size_t p = s->pos;
    size_t payload;

    if (label == EXT_GRAPHIC_CONTROL) {
        if (p < n && d[p] >= 4 && n - p >= 5u) {
            unsigned packed = d[p + 1];
            pend->has = true;
            pend->disposal_raw = (packed >> 2) & 7;
            pend->user_input = (packed & 2) != 0;
            pend->has_transparency = (packed & 1) != 0;
            pend->delay = load16(d + p + 2);
            pend->transparent_index = d[p + 4];
        }
        bool ok = skip_chain(d, n, &p, &payload);
        s->pos = p;
        return ok;
    }

    MimeGifExtension e;
    __builtin_memset(&e, 0, sizeof(e));
    e.label = label;
    e.frame_index = s->frame_count;
    bool netscape = false;
    bool ok;

    if (label == EXT_COMMENT) {
        e.kind = MIME_GIF_EXT_COMMENT;
        s->info.comment_count++;
        e.data = d + p;
        e.chained = true;
        ok = skip_chain(d, n, &p, &payload);
        e.data_size = payload;
    } else if (label == EXT_APPLICATION || label == EXT_PLAIN_TEXT) {
        e.kind = label == EXT_APPLICATION ? MIME_GIF_EXT_APPLICATION : MIME_GIF_EXT_PLAIN_TEXT;
        if (p >= n) {
            s->pos = n;
            return false;
        }
        size_t hlen = d[p++];
        if (hlen > n - p) {
            s->pos = n;
            return false;
        }
        e.header = d + p;
        e.header_size = (uint8_t)hlen;
        p += hlen;
        if (e.kind == MIME_GIF_EXT_APPLICATION)
            e.app = app_kind(e.header, hlen, &netscape);
        if (e.kind == MIME_GIF_EXT_PLAIN_TEXT && pend->has) {
            e.has_gce = true;
            e.gce_disposal = pend->disposal_raw > 3 ? 0 : pend->disposal_raw;
            e.gce_has_transparency = pend->has_transparency;
            e.gce_transparent_index = pend->transparent_index;
            e.gce_delay = pend->delay;
        }
        e.data = d + p;
        size_t trailer;
        if (hlen == 0) {
            /* the length byte was the terminator already. */
            e.chained = true;
            e.data_size = 0;
            ok = true;
        } else if (e.app == MIME_GIF_APP_XMP && !s->xmp_trailer_absent && find_xmp_trailer(d, n, p, &trailer)) {
            e.chained = false;
            e.data_size = trailer - p;
            p = trailer + XMP_TRAILER_SIZE;
            ok = true;
        } else {
            if (e.app == MIME_GIF_APP_XMP)
                s->xmp_trailer_absent = true;
            e.chained = true;
            if (netscape && !s->info.has_loop_count && p + 4 <= n && d[p] >= 3 && d[p + 1] == 1) {
                s->info.has_loop_count = true;
                s->info.loop_count = (uint16_t)load16(d + p + 2);
            }
            ok = skip_chain(d, n, &p, &payload);
            e.data_size = payload;
        }
    } else {
        e.kind = MIME_GIF_EXT_UNKNOWN;
        e.data = d + p;
        e.chained = true;
        ok = skip_chain(d, n, &p, &payload);
        e.data_size = payload;
    }

    s->pos = p;
    if (s->ext_count >= MIME_GIF_MAX_EXTENSIONS) {
        *warn |= MIME_GIF_W_LIMIT;
    } else {
        if (s->exts)
            s->exts[s->ext_count] = e;
        s->ext_count++;
    }
    return ok;
}

/* parses an image descriptor and its data after the 0x2c separator. */
static bool scan_image(Scan *s, size_t sep, Pending *pend, uint32_t *warn)
{
    const uint8_t *d = s->d;
    size_t n = s->n;
    size_t p = sep + 1;

    if (n - p < 9) {
        s->pos = n;
        return false;
    }
    MimeGifFrame f;
    __builtin_memset(&f, 0, sizeof(f));
    f.left = (uint16_t)load16(d + p);
    f.top = (uint16_t)load16(d + p + 2);
    f.width = (uint16_t)load16(d + p + 4);
    f.height = (uint16_t)load16(d + p + 6);
    unsigned packed = d[p + 8];
    p += 9;
    f.interlaced = (packed & 0x40) != 0;
    if (packed & 0x80) {
        size_t bytes = (size_t)3 << ((packed & 7) + 1);
        if (bytes > n - p) {
            s->pos = n;
            return false;
        }
        f.has_local_palette = true;
        f.local_palette_sorted = (packed & 0x20) != 0;
        f.palette = d + p;
        f.palette_size = (uint16_t)(bytes / 3);
        p += bytes;
    } else if (s->info.has_global_palette) {
        f.palette = s->info.global_palette;
        f.palette_size = s->info.global_palette_size;
    }
    if (p >= n) {
        s->pos = n;
        return false;
    }
    f.min_code_size = d[p++];
    if (pend->has) {
        f.has_gce = true;
        f.disposal_raw = pend->disposal_raw;
        f.disposal = pend->disposal_raw > 3 ? 0 : pend->disposal_raw;
        f.user_input = pend->user_input;
        f.has_transparency = pend->has_transparency;
        f.transparent_index = pend->transparent_index;
        f.delay = pend->delay;
        pend->has = false;
    }
    f.decodable = f.min_code_size >= 1 && f.min_code_size <= 8 &&
                  (uint64_t)f.width * f.height <= MIME_GIF_MAX_PIXELS;
    if (!f.decodable)
        *warn |= MIME_GIF_W_BAD_FRAME;
    f.offset = sep;
    f.data = d + p;
    size_t payload;
    f.complete = skip_chain(d, n, &p, &payload);
    f.data_bytes = (uint32_t)payload;
    s->pos = p;

    if (f.disposal == 3)
        s->restore_previous = true;
    unsigned right = (unsigned)f.left + f.width, bottom = (unsigned)f.top + f.height;
    if (right > s->max_right)
        s->max_right = right;
    if (bottom > s->max_bottom)
        s->max_bottom = bottom;

    if (s->frames)
        s->frames[s->frame_count] = f;
    s->frame_count++;
    return f.complete;
}

static void scan_blocks(Scan *s, uint32_t *warn)
{
    const uint8_t *d = s->d;
    size_t n = s->n;
    Pending pend = {0};
    bool after_junk = false;

    for (;;) {
        if (s->pos >= n) {
            *warn |= MIME_GIF_W_NO_TRAILER;
            return;
        }
        size_t at = s->pos;
        uint8_t b = d[s->pos++];
        if (b == BLOCK_TRAILER)
            return;
        if (b == 0) {
            continue;
        } else if (b == BLOCK_IMAGE) {
            if (after_junk && !image_plausible(s, at)) {
                *warn |= MIME_GIF_W_JUNK;
                continue;
            }
            if (s->frame_count >= MIME_GIF_MAX_FRAMES) {
                *warn |= MIME_GIF_W_LIMIT;
                return;
            }
            after_junk = false;
            if (!scan_image(s, at, &pend, warn)) {
                *warn |= MIME_GIF_W_TRUNCATED;
                return;
            }
        } else if (b == BLOCK_EXTENSION) {
            if (s->pos >= n) {
                *warn |= MIME_GIF_W_TRUNCATED;
                return;
            }
            uint8_t label = d[s->pos++];
            if (after_junk && label != EXT_GRAPHIC_CONTROL && label != EXT_APPLICATION && label != EXT_COMMENT &&
                label != EXT_PLAIN_TEXT) {
                s->pos = at + 1;
                *warn |= MIME_GIF_W_JUNK;
                continue;
            }
            after_junk = false;
            if (!scan_extension(s, label, &pend, warn)) {
                *warn |= MIME_GIF_W_TRUNCATED;
                return;
            }
        } else {
            *warn |= MIME_GIF_W_JUNK;
            after_junk = true;
        }
    }
}

/* header and global palette. the caller scans the frames. */
static int scan_header(Scan *s)
{
    const uint8_t *d = s->d;
    if (s->n < 13 || !mime_gif_is_gif(d, s->n))
        return MIME_GIF_ERR_FORMAT;
    MimeGifInfo *info = &s->info;
    info->version = d[4] == '7' ? 87 : 89;
    info->declared_width = (uint16_t)load16(d + 6);
    info->declared_height = (uint16_t)load16(d + 8);
    unsigned packed = d[10];
    info->color_resolution = (uint8_t)(((packed >> 4) & 7) + 1);
    info->background_index = d[11];
    info->pixel_aspect_ratio = d[12];
    s->pos = 13;
    if (packed & 0x80) {
        size_t bytes = (size_t)3 << ((packed & 7) + 1);
        if (bytes > s->n - s->pos)
            return MIME_GIF_ERR_FORMAT;
        info->has_global_palette = true;
        info->global_palette_sorted = (packed & 0x08) != 0;
        info->global_palette_size = (uint16_t)(bytes / 3);
        info->global_palette = d + s->pos;
        s->pos += bytes;
    }
    return 0;
}

int mime_gif_open(const void *data, size_t size, const MimeAllocator *alloc, MimeGif **out)
{
    if (!out)
        return MIME_GIF_ERR_PARAM;
    *out = NULL;
    if (!data || !alloc || !alloc->alloc)
        return MIME_GIF_ERR_PARAM;

    Scan s;
    __builtin_memset(&s, 0, sizeof(s));
    s.d = data;
    s.n = size;
    int rc = scan_header(&s);
    if (rc < 0)
        return rc;
    size_t body = s.pos;
    MimeGifInfo header_info = s.info;

    /* the first pass counts, so that the handle can be sized exactly. */
    uint32_t warn = 0;
    scan_blocks(&s, &warn);
    if (s.frame_count == 0)
        return MIME_GIF_ERR_EMPTY;
    uint32_t nframes = s.frame_count, nexts = s.ext_count;

    size_t head = (sizeof(MimeGif) + 15u) & ~(size_t)15;
    size_t total = head + (size_t)nframes * sizeof(MimeGifFrame) + (size_t)nexts * sizeof(MimeGifExtension);
    MimeGif *g = mime_alloc(alloc, total);
    if (!g)
        return MIME_GIF_ERR_NOMEM;
    __builtin_memset(g, 0, head);
    g->alloc = *alloc;
    g->data = data;
    g->size = size;
    g->frames = (MimeGifFrame *)((uint8_t *)g + head);
    g->exts = (MimeGifExtension *)(g->frames + nframes);

    s.pos = body;
    s.info = header_info;
    s.frames = g->frames;
    s.exts = g->exts;
    s.frame_count = 0;
    s.ext_count = 0;
    s.restore_previous = false;
    s.xmp_trailer_absent = false;
    s.max_right = s.max_bottom = 0;
    warn = 0;
    scan_blocks(&s, &warn);

    g->info = s.info;
    g->info.frame_count = s.frame_count;
    g->info.extension_count = s.ext_count;
    g->has_restore_previous = s.restore_previous;
    g->info.width = g->info.declared_width;
    g->info.height = g->info.declared_height;
    if (!g->info.width || !g->info.height) {
        warn |= MIME_GIF_W_SCREEN_INFERRED;
        g->info.width = (uint16_t)(s.max_right > 65535 ? 65535 : s.max_right);
        g->info.height = (uint16_t)(s.max_bottom > 65535 ? 65535 : s.max_bottom);
    }
    if ((uint64_t)g->info.width * g->info.height > MIME_GIF_MAX_PIXELS) {
        mime_free(alloc, g);
        return MIME_GIF_ERR_UNSUPPORTED;
    }
    g->info.warnings = warn;
    *out = g;
    if (warn & MIME_GIF_W_TRUNCATED)
        return MIME_GIF_TRUNCATED;
    if (warn & ~(uint32_t)MIME_GIF_W_NO_TRAILER)
        return MIME_GIF_DAMAGED;
    return MIME_GIF_OK;
}

void mime_gif_close(MimeGif *gif)
{
    if (gif) {
        MimeAllocator a = gif->alloc;
        mime_free(&a, gif);
    }
}

const MimeGifInfo *mime_gif_info(const MimeGif *gif)
{
    return &gif->info;
}

const MimeGifFrame *mime_gif_frame(const MimeGif *gif, unsigned index)
{
    return index < gif->info.frame_count ? &gif->frames[index] : NULL;
}

const MimeGifExtension *mime_gif_extension(const MimeGif *gif, unsigned index)
{
    return index < gif->info.extension_count ? &gif->exts[index] : NULL;
}

size_t mime_gif_extension_copy(const MimeGifExtension *ext, void *dst, size_t cap)
{
    uint8_t *out = dst;
    size_t done = 0;
    if (!ext->chained) {
        done = ext->data_size < cap ? ext->data_size : cap;
        __builtin_memcpy(out, ext->data, done);
        return done;
    }
    const uint8_t *p = ext->data;
    size_t left = ext->data_size;
    while (left) {
        size_t len = *p++;
        if (!len)
            break;
        if (len > left)
            len = left;
        size_t room = cap - done;
        size_t take = len < room ? len : room;
        __builtin_memcpy(out + done, p, take);
        done += take;
        p += len;
        left -= len;
    }
    return done;
}
