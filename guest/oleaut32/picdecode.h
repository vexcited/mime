/*
 * the image decoders behind OleLoadPicture: gif and jpeg files decoded to a bitmap in memory. They know nothing of
 * windows, so a host program can test them.
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
 * a decoded picture. Rows run top down without padding. An image of 8 bits a pixel carries its palette as blue,
 * green, red and a zero; an image of 24 bits holds blue, green and red.
 */
typedef struct AUT_IMAGE {
    int width, height;
    int bpp;
    int colors;
    int transparent;
    unsigned char palette[256][4];
    unsigned char *pixels;
} AUT_IMAGE;

/* both return 1 when they made an image and 0 when the data is not one. */
int AutDecodeGif(const unsigned char *data, size_t size, AUT_IMAGE *image);
int AutDecodeJpeg(const unsigned char *data, size_t size, AUT_IMAGE *image);
void AutImageFree(AUT_IMAGE *image);

#endif
