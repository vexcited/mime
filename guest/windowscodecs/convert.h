/*
 * the pixel conversion core and the palette indexer behind IWICFormatConverter.
 */
#ifndef WIC_CONVERT_H
#define WIC_CONVERT_H

typedef struct WicConverter WicConverter;
typedef struct WicIndexer WicIndexer;

WicIndexer *WicIndexerNew(const WICColor *palette, UINT count, WICBitmapDitherType dither, double threshold_percent,
                          UINT width);
void WicIndexerFree(WicIndexer *indexer);
/* converts a row of straight BGRA to indices of bits each, the row y of the image. */
void WicIndexerRow(WicIndexer *indexer, const BYTE *bgra, BYTE *dst, UINT x0, UINT count, UINT bits, UINT col, UINT y);

/*
 * a color channel of a pixel that was multiplied by its alpha, made straight again. Alpha 255 leaves it, otherwise it is
 * scaled by 255 over alpha in 16.16 and held to 255. An alpha of 0 has nothing to scale.
 */
static inline BYTE WicStraightChannel(BYTE channel, BYTE alpha)
{
    if (alpha == 255 || alpha == 0)
        return channel;
    UINT scaled = ((UINT)channel * (0xff0000u / alpha)) >> 16;
    return scaled > 255 ? 255 : (BYTE)scaled;
}

#endif
