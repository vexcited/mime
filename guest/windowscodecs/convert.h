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

#endif
