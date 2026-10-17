/* the pixel loops of blit.c. */
#ifndef DDRAW_BLIT_H
#define DDRAW_BLIT_H

typedef struct DDBLIT {
    BYTE *dst;
    LONG dst_pitch;
    LONG width, height;        /* of the destination rectangle */
    const BYTE *src;
    LONG src_pitch;
    LONG src_width, src_height; /* of the source rectangle */
    DWORD size;                /* bytes a pixel, the same in both */
    BOOL mirror_x, mirror_y;
    BOOL src_key, dst_key;
    DWORD src_low, src_high;   /* source pixels in the range are not copied */
    DWORD dst_low, dst_high;   /* only destination pixels in the range are written */
    DWORD key_mask;            /* the bits of a pixel keys compare */
} DDBLIT;

void DdBlit(const DDBLIT *b);
void DdFill(BYTE *dst, LONG pitch, DWORD size, LONG width, LONG height, DWORD value);

#endif
