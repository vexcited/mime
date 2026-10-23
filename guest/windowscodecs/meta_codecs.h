/* codec metadata interfaces. */
#ifndef WIC_META_CODECS_H
#define WIC_META_CODECS_H

#include "wic_codec.h"
#include "meta_internal.h"
#include <jpeg/jpeg.h>
#include <pngio/pngio.h>
#include <tiff/tiff.h>

/* IFD blocks. */
HRESULT WicIfdFromTiffIndex(const GUID *format, const MimeTiff *tiff, UINT index, IWICMetadataReader **out);
HRESULT WicIfdFromParsed(const GUID *format, const MimeTiffIfd *ifd, IWICMetadataReader **out);

/* IFD entries for TIFF encoding. */
typedef struct WicIfdSet WicIfdSet;
HRESULT WicIfdSetCreate(IWICMetadataReader *ifd, WicIfdSet **set);
void WicIfdSetFree(WicIfdSet *set);
const MimeTiffWEntry *WicIfdSetEntries(const WicIfdSet *set, UINT *count);
const MimeTiffWIfd *WicIfdSetExif(const WicIfdSet *set);
const MimeTiffWIfd *WicIfdSetGps(const WicIfdSet *set);

/* XMP packets exclude the JPEG identifier; the writer can prepend it. */
HRESULT WicXmpFromPacket(const BYTE *data, SIZE_T size, IWICMetadataReader **out);
HRESULT WicXmpToPacket(IWICMetadataReader *xmp, BOOL jpeg_header, BYTE **out, SIZE_T *size);

/* jpeg. */
typedef struct JpegSegmentList JpegSegmentList;
HRESULT WicJpegReaders(const BYTE *data, SIZE_T size, IWICMetadataReader ***readers, UINT *count);
HRESULT WicJpegSegments(IWICMetadataBlockReader *blocks, JpegSegmentList **list);
void WicJpegSegmentsFree(JpegSegmentList *list);
const MimeJpegAppSegment *WicJpegSegmentsData(const JpegSegmentList *list, UINT *count);

/* png. */
typedef struct PngMeta PngMeta;
HRESULT WicPngReaders(MimePngioDecoder *dec, const BYTE *file, SIZE_T file_size, IWICMetadataReader ***readers,
                      UINT *count);
void WicPngApplyMetadata(PngMeta **meta, MimePngioEncode *params, IWICMetadataBlockReader *blocks);
void WicPngMetadataFree(PngMeta *meta);

#endif
