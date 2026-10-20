/*
 * metadata: the handlers that read and write the blocks of a container or a frame, and the query language over them.
 */
#ifndef WIC_META_H
#define WIC_META_H

#include "wic_codec.h"

/* a block reader over readers, which it references. */
HRESULT WicBlockReaderCreate(const GUID *container, IWICMetadataReader *const *readers, UINT count,
                             IWICMetadataBlockReader **reader);
/* a block writer, empty. */
HRESULT WicBlockWriterCreate(const GUID *container, IWICMetadataBlockWriter **writer);

/* handlers that hold their items in memory, for the codecs to fill and to read. */
HRESULT WicMetaHandlerCreate(const GUID *format, IWICMetadataWriter **handler);
HRESULT WicMetaSetItem(IWICMetadataWriter *handler, const PROPVARIANT *schema, const PROPVARIANT *id,
                       const PROPVARIANT *value);
/* items named by a string, as the png and gif handlers have. */
HRESULT WicMetaSetU8(IWICMetadataWriter *h, const WCHAR *name, BYTE value);
HRESULT WicMetaSetU16(IWICMetadataWriter *h, const WCHAR *name, UINT16 value);
HRESULT WicMetaSetU32(IWICMetadataWriter *h, const WCHAR *name, UINT32 value);
HRESULT WicMetaSetBool(IWICMetadataWriter *h, const WCHAR *name, BOOL value);
HRESULT WicMetaSetAnsi(IWICMetadataWriter *h, const WCHAR *name, const char *text, SIZE_T length);
HRESULT WicMetaSetBytes(IWICMetadataWriter *h, const WCHAR *name, const BYTE *data, UINT size);
/* items numbered, as in an ifd. */
HRESULT WicMetaSetNumbered(IWICMetadataWriter *h, UINT16 id, const PROPVARIANT *value);
/* reads. the result is false when the handler has no such item or it is of another kind. */
BOOL WicMetaGetU32(IWICMetadataReader *h, const WCHAR *name, UINT32 *value);
BOOL WicMetaGetBool(IWICMetadataReader *h, const WCHAR *name, BOOL *value);
BOOL WicMetaGetBytes(IWICMetadataReader *h, const WCHAR *name, BYTE **data, UINT *size);
BOOL WicMetaGetAnsi(IWICMetadataReader *h, const WCHAR *name, char **text);
/* the reader of the block of the format, referenced, or NULL. */
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
