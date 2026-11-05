/* metadata handler construction, stream parsing and serialization. */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"
#include "meta_codecs.h"

static BOOL is_ifd_format(const GUID *f)
{
    return IsEqualGUID(f, &GUID_MetadataFormatIfd) || IsEqualGUID(f, &GUID_MetadataFormatExif) ||
           IsEqualGUID(f, &GUID_MetadataFormatGps) || IsEqualGUID(f, &GUID_MetadataFormatInterop) ||
           IsEqualGUID(f, &GUID_MetadataFormatSubIfd) || IsEqualGUID(f, &GUID_MetadataFormatThumbnail);
}

static BOOL known_format(const GUID *f) { return WicFormatShortName(f) != nullptr; }

HRESULT WicMetaCreateWriter(const GUID *format, const GUID *vendor, DWORD options, IWICMetadataWriter **writer)
{
    if (vendor && !WicVendorOurs(vendor) && !IsEqualGUID(vendor, &GUID_NULL))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    if (!known_format(format) && (options & WICMetadataCreationFailUnknown))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    if (!known_format(format))
        return WicMetaHandlerCreate(&GUID_MetadataFormatUnknown, writer);
    return WicMetaHandlerCreate(format, writer);
}

HRESULT WicMetaCreateReader(const GUID *format, const GUID *vendor, DWORD options, IStream *stream,
                            IWICMetadataReader **reader)
{
    if (vendor && !WicVendorOurs(vendor) && !IsEqualGUID(vendor, &GUID_NULL))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    BOOL known = known_format(format);
    if (!known && (options & WICMetadataCreationFailUnknown))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    const GUID *use = known ? format : &GUID_MetadataFormatUnknown;
    if (!stream) {
        IWICMetadataWriter *w;
        HRESULT hr = WicMetaHandlerCreate(use, &w);
        if (SUCCEEDED(hr))
            *reader = (IWICMetadataReader *)w;
        return hr;
    }
    BYTE *data;
    SIZE_T size;
    HRESULT hr = WicReadStream(stream, &data, &size, 0x10000000);
    if (FAILED(hr))
        return hr;
    if (is_ifd_format(use))
        hr = WicIfdFromTiff(use, data, size, reader);
    else if (IsEqualGUID(use, &GUID_MetadataFormatXMP))
        hr = WicXmpFromPacket(data, size, reader);
    else
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    WicFree(data);
    return hr;
}

HRESULT WicMetaCreateReaderFromContainer(const GUID *container, const GUID *vendor, DWORD options, IStream *stream,
                                         IWICMetadataReader **reader)
{
    BYTE *data;
    SIZE_T size;
    HRESULT hr = WicReadStream(stream, &data, &size, 0x10000000);
    if (FAILED(hr))
        return hr;
    /* identify the block from its leading bytes. */
    if (size >= 6 && !memcmp(data, "Exif\0\0", 6)) {
        IWICMetadataReader *app1, *ifd;
        hr = WicMetaReaderCreate(&GUID_MetadataFormatApp1, &app1);
        if (SUCCEEDED(hr)) {
            hr = WicIfdFromTiff(&GUID_MetadataFormatIfd, data + 6, size - 6, &ifd);
            if (SUCCEEDED(hr)) {
                PROPVARIANT pv;
                WicPvInit(&pv);
                pv.vt = VT_UNKNOWN;
                pv.punkVal = (IUnknown *)ifd;
                WicMetaAdd(app1, nullptr, nullptr, &pv);
                ifd->lpVtbl->Release(ifd);
                *reader = app1;
            } else {
                app1->lpVtbl->Release(app1);
            }
        }
    } else if (size >= 8 && ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M'))) {
        hr = WicIfdFromTiff(&GUID_MetadataFormatIfd, data, size, reader);
    } else if (size > 5 && (!memcmp(data, "<?xpa", 5) || !memcmp(data, "<x:xm", 5))) {
        hr = WicXmpFromPacket(data, size, reader);
    } else {
        hr = WINCODEC_ERR_COMPONENTNOTFOUND;
    }
    WicFree(data);
    return hr;
}

HRESULT WicMetaWriterFromReader(IWICMetadataReader *reader, const GUID *vendor, IWICMetadataWriter **writer)
{
    if (vendor && !WicVendorOurs(vendor) && !IsEqualGUID(vendor, &GUID_NULL))
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    return WicMetaCopyToWriter(reader, writer);
}

HRESULT WicMetaFastEncoderFromDecoder(IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **encoder)
{
    *encoder = nullptr;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT WicMetaFastEncoderFromFrame(IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **encoder)
{
    *encoder = nullptr;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}


static HRESULT content_of(IWICMetadataWriter *writer, BYTE **blob, SIZE_T *size, DWORD options)
{
    GUID f;
    HRESULT hr = writer->lpVtbl->GetMetadataFormat(writer, &f);
    if (FAILED(hr))
        return hr;
    if (is_ifd_format(&f))
        return WicIfdToTiff((IWICMetadataReader *)writer, (options & WICPersistOptionBigEndian) != 0, FALSE, blob, size);
    if (IsEqualGUID(&f, &GUID_MetadataFormatXMP))
        return WicXmpToPacket((IWICMetadataReader *)writer, FALSE, blob, size);
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

DECLSPEC_EXPORT HRESULT WINAPI WICSerializeMetadataContent(REFGUID container, IWICMetadataWriter *writer,
                                                           DWORD options, IStream *stream)
{
    if (!container || !writer || !stream)
        return E_INVALIDARG;
    BYTE *blob;
    SIZE_T size;
    HRESULT hr = content_of(writer, &blob, &size, options);
    if (FAILED(hr))
        return hr;
    hr = WicWriteStream(stream, blob, size);
    WicFree(blob);
    return hr;
}

DECLSPEC_EXPORT HRESULT WINAPI WICGetMetadataContentSize(REFGUID container, IWICMetadataWriter *writer,
                                                         ULARGE_INTEGER *size)
{
    if (!container || !writer || !size)
        return E_INVALIDARG;
    BYTE *blob;
    SIZE_T length;
    HRESULT hr = content_of(writer, &blob, &length, 0);
    if (FAILED(hr))
        return hr;
    WicFree(blob);
    size->QuadPart = length;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WICMatchMetadataContent(REFGUID container, const GUID *vendor, IStream *stream,
                                                       GUID *format)
{
    if (!container || !stream || !format)
        return E_INVALIDARG;
    LARGE_INTEGER zero;
    ULARGE_INTEGER start;
    zero.QuadPart = 0;
    HRESULT hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start);
    if (FAILED(hr))
        return hr;
    BYTE head[32];
    ULONG got = 0;
    hr = stream->lpVtbl->Read(stream, head, sizeof(head), &got);
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)start.QuadPart;
    stream->lpVtbl->Seek(stream, at, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr))
        return hr;
    if (got >= 6 && !memcmp(head, "Exif\0\0", 6))
        *format = GUID_MetadataFormatApp1;
    else if (got >= 4 && ((head[0] == 'I' && head[1] == 'I' && head[2] == 42) || (head[0] == 'M' && head[1] == 'M' && head[3] == 42)))
        *format = GUID_MetadataFormatIfd;
    else if (got >= 5 && !memcmp(head, "<?xpa", 5))
        *format = GUID_MetadataFormatXMP;
    else
        return WINCODEC_ERR_COMPONENTNOTFOUND;
    return S_OK;
}
