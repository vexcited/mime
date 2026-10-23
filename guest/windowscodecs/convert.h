/* pixel conversion and palette indexing for IWICFormatConverter. */
#ifndef WIC_CONVERT_H
#define WIC_CONVERT_H

typedef struct WicConverter WicConverter;
typedef struct WicIndexer WicIndexer;

WicIndexer *WicIndexerNew(const WICColor *palette, UINT count, WICBitmapDitherType dither, double threshold_percent,
                          UINT width);
void WicIndexerFree(WicIndexer *indexer);
/* convert image row y from straight BGRA to packed indices of bits each. */
void WicIndexerRow(WicIndexer *indexer, const BYTE *bgra, BYTE *dst, UINT x0, UINT count, UINT bits, UINT col, UINT y);

/*
 * undo premultiplication using 16.16 scaling by 255 / alpha, capped at 255. alpha 255 leaves the channel unchanged;
 * alpha 0 cannot be scaled.
 */
static inline BYTE WicStraightChannel(BYTE channel, BYTE alpha)
{
    if (alpha == 255 || alpha == 0)
        return channel;
    UINT scaled = ((UINT)channel * (0xff0000u / alpha)) >> 16;
    return scaled > 255 ? 255 : (BYTE)scaled;
}

#endif
