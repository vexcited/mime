/*
 * the metadata of the image formats, for the codecs.
 */
#ifndef WIC_META_CODECS_H
#define WIC_META_CODECS_H

#include "wic_codec.h"
#include "meta_internal.h"
#include <jpeg/jpeg.h>
#include <pngio/pngio.h>
#include <tiff/tiff.h>

/* ifd blocks. */
HRESULT WicIfdFromTiffIndex(const GUID *format, const MimeTiff *tiff, UINT index, IWICMetadataReader **out);
HRESULT WicIfdFromParsed(const GUID *format, const MimeTiffIfd *ifd, IWICMetadataReader **out);

/* the entries of an ifd block for the encoder of tiff. */
typedef struct WicIfdSet WicIfdSet;
HRESULT WicIfdSetCreate(IWICMetadataReader *ifd, WicIfdSet **set);
void WicIfdSetFree(WicIfdSet *set);
const MimeTiffWEntry *WicIfdSetEntries(const WicIfdSet *set, UINT *count);
const MimeTiffWIfd *WicIfdSetExif(const WicIfdSet *set);
const MimeTiffWIfd *WicIfdSetGps(const WicIfdSet *set);

/* xmp. the packet of a jpeg starts after its identifier, the writer can add it. */
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
