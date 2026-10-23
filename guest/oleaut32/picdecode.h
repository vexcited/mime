/*
 * the gif and jpeg decoders behind OleLoadPicture, decoding to an in-memory bitmap. They do not depend on Windows.
 */
#ifndef MIME_OLEAUT32_PICDECODE_H
#define MIME_OLEAUT32_PICDECODE_H

#ifdef AUT_STANDALONE
#include <stdlib.h>
#include <string.h>
#define AUT_ALLOC(n) calloc(1, (n))
#define AUT_FREE(p) free(p)
#else
#include "oleaut32_private.h"
#define AUT_ALLOC(n) AutAllocZero(n)
#define AUT_FREE(p) AutFree(p)
#endif

/*
 * rows run top down without padding. 8 bpp images carry a palette of blue, green, red and zero; 24 bpp pixels are
 * blue, green, red.
 */
typedef struct AUT_IMAGE {
    int width, height;
    int bpp;
    int colors;
    int transparent;
    unsigned char palette[256][4];
    unsigned char *pixels;
} AUT_IMAGE;

/* return 1 when an image was made, 0 when the data is not one. */
int AutDecodeGif(const unsigned char *data, size_t size, AUT_IMAGE *image);
int AutDecodeJpeg(const unsigned char *data, size_t size, AUT_IMAGE *image);
void AutImageFree(AUT_IMAGE *image);

#endif
