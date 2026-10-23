/* metadata handlers, block readers and writers, and query interfaces. */
#ifndef WIC_META_H
#define WIC_META_H

#include "wic_codec.h"

/* block readers retain references to their readers. */
HRESULT WicBlockReaderCreate(const GUID *container, IWICMetadataReader *const *readers, UINT count,
                             IWICMetadataBlockReader **reader);
HRESULT WicBlockWriterCreate(const GUID *container, IWICMetadataBlockWriter **writer);

/* in-memory handlers filled and read by codecs. */
HRESULT WicMetaHandlerCreate(const GUID *format, IWICMetadataWriter **handler);
HRESULT WicMetaSetItem(IWICMetadataWriter *handler, const PROPVARIANT *schema, const PROPVARIANT *id,
                       const PROPVARIANT *value);
/* string-keyed items used by PNG and GIF handlers. */
HRESULT WicMetaSetU8(IWICMetadataWriter *h, const WCHAR *name, BYTE value);
HRESULT WicMetaSetU16(IWICMetadataWriter *h, const WCHAR *name, UINT16 value);
HRESULT WicMetaSetU32(IWICMetadataWriter *h, const WCHAR *name, UINT32 value);
HRESULT WicMetaSetBool(IWICMetadataWriter *h, const WCHAR *name, BOOL value);
HRESULT WicMetaSetAnsi(IWICMetadataWriter *h, const WCHAR *name, const char *text, SIZE_T length);
HRESULT WicMetaSetBytes(IWICMetadataWriter *h, const WCHAR *name, const BYTE *data, UINT size);
/* items numbered, as in an ifd. */
HRESULT WicMetaSetNumbered(IWICMetadataWriter *h, UINT16 id, const PROPVARIANT *value);
/* reads return false for missing items or type mismatches. */
BOOL WicMetaGetU32(IWICMetadataReader *h, const WCHAR *name, UINT32 *value);
BOOL WicMetaGetBool(IWICMetadataReader *h, const WCHAR *name, BOOL *value);
BOOL WicMetaGetBytes(IWICMetadataReader *h, const WCHAR *name, BYTE **data, UINT *size);
BOOL WicMetaGetAnsi(IWICMetadataReader *h, const WCHAR *name, char **text);
/* return a referenced block reader for this format, or NULL. */
IWICMetadataReader *WicBlocksFind(IWICMetadataBlockReader *blocks, const GUID *format);
/* a query reader over the readers of a block reader. */
HRESULT WicQueryFromBlocks(IWICMetadataBlockReader *blocks, IWICMetadataQueryReader **query);


/* readers and writers of single blocks, and query objects. */
HRESULT WicMetaCreateReader(const GUID *format, const GUID *vendor, DWORD options, IStream *stream,
                            IWICMetadataReader **reader);
HRESULT WicMetaCreateReaderFromContainer(const GUID *container, const GUID *vendor, DWORD options, IStream *stream,
                                         IWICMetadataReader **reader);
HRESULT WicMetaCreateWriter(const GUID *format, const GUID *vendor, DWORD options, IWICMetadataWriter **writer);
HRESULT WicMetaWriterFromReader(IWICMetadataReader *reader, const GUID *vendor, IWICMetadataWriter **writer);
HRESULT WicMetaQueryWriterCreate(const GUID *format, const GUID *vendor, IWICMetadataQueryWriter **query);
HRESULT WicMetaQueryWriterFromReader(IWICMetadataQueryReader *reader, const GUID *vendor,
                                     IWICMetadataQueryWriter **query);
HRESULT WicMetaQueryWriterFromBlocks(IWICMetadataBlockWriter *blocks, IWICMetadataQueryWriter **query);
HRESULT WicMetaFastEncoderFromDecoder(IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **encoder);
HRESULT WicMetaFastEncoderFromFrame(IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **encoder);
HRESULT WicEncoderPropertyBag(const WicCodec *codec, IPropertyBag2 **bag);

#endif
