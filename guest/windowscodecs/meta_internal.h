/*
 * the parts of the metadata code that its files share.
 */
#ifndef WIC_META_INTERNAL_H
#define WIC_META_INTERNAL_H

#include "wic_meta.h"

#ifndef STRSAFE_E_INSUFFICIENT_BUFFER
#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007aL)
#endif

typedef struct WicMetaItem {
    PROPVARIANT schema;
    PROPVARIANT id;
    PROPVARIANT value;
} WicMetaItem;

HRESULT WicMetadataInfoFind(const GUID *clsid, IWICComponentInfo **info);
HRESULT WicMetadataInfoList(DWORD types, IUnknown ***list, UINT *count);

/* property variants. */
void WicPvInit(PROPVARIANT *pv);
BOOL WicPvIsEmpty(const PROPVARIANT *pv);
BOOL WicPvSame(const PROPVARIANT *a, const PROPVARIANT *b);

/* handlers. a reader made here is filled with WicMetaAdd by the decoders. */
HRESULT WicMetaReaderCreate(const GUID *format, IWICMetadataReader **reader);
HRESULT WicMetaAdd(IWICMetadataReader *reader, const PROPVARIANT *schema, const PROPVARIANT *id,
                   const PROPVARIANT *value);
/* adds a named item of a number to a reader a decoder fills. a bool takes 0 or 1. */
HRESULT WicMetaPutNamed(IWICMetadataReader *reader, const WCHAR *name, VARTYPE vt, UINT64 value);
HRESULT WicMetaPutAnsi(IWICMetadataReader *reader, const WCHAR *name, const char *text, SIZE_T length);
HRESULT WicMetaPutBytes(IWICMetadataReader *reader, const WCHAR *name, const void *data, UINT size);
UINT WicMetaCount(IWICMetadataReader *reader);
const WicMetaItem *WicMetaItemAt(IWICMetadataReader *reader, UINT index);
HRESULT WicMetaCopyToWriter(IWICMetadataReader *reader, IWICMetadataWriter **writer);
HRESULT WicMetaHandlerInfoCreate(const GUID *format, BOOL writer, IWICMetadataHandlerInfo **info);

/* enumerators. the items array and the references in it are taken. */
HRESULT WicEnumFromItems(IUnknown **items, UINT count, IEnumUnknown **out);
HRESULT WicEnumUnknownCreate(IUnknown **items, UINT count, IEnumUnknown **out);
HRESULT WicEnumStringCreate(WCHAR *const *names, UINT count, IEnumString **out);

/* names. */
const WCHAR *WicFormatShortName(const GUID *guid);
const GUID *WicFormatFromShortName(const WCHAR *name);
const WCHAR *WicSchemaUri(const GUID *format, const WCHAR *prefix, SIZE_T prefix_length);
const WCHAR *WicSchemaPrefix(const GUID *format, const WCHAR *uri);
const GUID *WicFormatOfClass(const GUID *clsid);
BOOL WicIsMetadataClass(const GUID *clsid);

/* the values of the standard items, made from and turned into the property variants of tiff and exif. */
HRESULT WicIfdFromTiff(const GUID *format, const void *data, size_t size, IWICMetadataReader **reader);
/* the blob of an ifd handler and the handlers below it, in the byte order, with or without the exif prefix. */
HRESULT WicIfdToTiff(IWICMetadataReader *ifd, BOOL big_endian, BOOL prefix, BYTE **blob, SIZE_T *size);

#endif
