/*
 * jpeg pictures decoded by libjpeg, which gives gray, bgr or cmyk pixels. Gray stays 8 bpp with a gray palette, the
 * rest become 24 bpp.
 */
#include "picdecode.h"
#include <jpeg/jpeg.h>

static void *jpeg_alloc(void *context, size_t size)
{
    (void)context;
    return AutAlloc(size);
}

static void jpeg_free(void *context, void *block)
{
    (void)context;
    AutFree(block);
}

static const MimeAllocator g_allocator = {jpeg_alloc, jpeg_free, nullptr};

/* ink 0 is none; each ink absorbs its share of the light the others let through. */
static void cmyk_to_bgr(const unsigned char *ink, unsigned char *bgr)
{
    unsigned black_left = 255u - ink[3];
    for (int channel = 0; channel < 3; channel++) {
        unsigned left = (255u - ink[channel]) * black_left;
        bgr[2 - channel] = (unsigned char)((left + 127u) / 255u);
    }
}

static void gray_palette(AUT_IMAGE *image)
{
    for (int i = 0; i < 256; i++) {
        image->palette[i][0] = image->palette[i][1] = image->palette[i][2] = (unsigned char)i;
        image->palette[i][3] = 0;
    }
    image->colors = 256;
}

int AutDecodeJpeg(const unsigned char *data, size_t size, AUT_IMAGE *image)
{
    MimeJpegInfo info;
    if (!mime_jpeg_is_jpeg(data, size) || mime_jpeg_info(data, size, 0, &info) != MIME_JPEG_OK)
        return 0;
    size_t per_pixel = info.out_format;
    size_t stride = (size_t)info.out_width * per_pixel;
    unsigned char *decoded = AutAlloc(stride * info.out_height);
    if (!decoded)
        return 0;
    int status = mime_jpeg_decode(data, size, &g_allocator, MIME_JPEG_TOLERANT, decoded, stride,
                                  stride * info.out_height, nullptr);
    if (status != MIME_JPEG_OK && status != MIME_JPEG_TRUNCATED) {
        AutFree(decoded);
        return 0;
    }

    image->width = (int)info.out_width;
    image->height = (int)info.out_height;
    image->transparent = -1;
    if (info.out_format == MIME_JPEG_FORMAT_CMYK32) {
        size_t count = (size_t)info.out_width * info.out_height;
        unsigned char *bgr = AutAlloc(count * 3);
        if (!bgr) {
            AutFree(decoded);
            return 0;
        }
        for (size_t i = 0; i < count; i++)
            cmyk_to_bgr(decoded + i * 4, bgr + i * 3);
        AutFree(decoded);
        decoded = bgr;
        per_pixel = 3;
    }
    image->bpp = per_pixel == 1 ? 8 : 24;
    if (per_pixel == 1)
        gray_palette(image);
    image->pixels = decoded;
    return 1;
}
