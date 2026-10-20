/*
 * the C callable forms of the interface methods that windowscodecs.dll exports. A method of the proxy of an interface
 * is its method, called on the object.
 */
#include "wic_private.h"

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapClipper_Initialize_Proxy(IWICBitmapClipper *This, IWICBitmapSource *pISource, const WICRect *prc)
{
    return This->lpVtbl->Initialize(This, pISource, prc);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportAnimation_Proxy(IWICBitmapCodecInfo *This, BOOL *pfSupportAnimation)
{
    return This->lpVtbl->DoesSupportAnimation(This, pfSupportAnimation);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportLossless_Proxy(IWICBitmapCodecInfo *This, BOOL *pfSupportLossless)
{
    return This->lpVtbl->DoesSupportLossless(This, pfSupportLossless);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportMultiframe_Proxy(IWICBitmapCodecInfo *This, BOOL *pfSupportMultiframe)
{
    return This->lpVtbl->DoesSupportMultiframe(This, pfSupportMultiframe);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_GetContainerFormat_Proxy(IWICBitmapCodecInfo *This, GUID *pguidContainerFormat)
{
    return This->lpVtbl->GetContainerFormat(This, pguidContainerFormat);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_GetDeviceManufacturer_Proxy(IWICBitmapCodecInfo *This, UINT cchDeviceManufacturer, WCHAR *wzDeviceManufacturer, UINT *pcchActual)
{
    return This->lpVtbl->GetDeviceManufacturer(This, cchDeviceManufacturer, wzDeviceManufacturer, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_GetDeviceModels_Proxy(IWICBitmapCodecInfo *This, UINT cchDeviceModels, WCHAR *wzDeviceModels, UINT *pcchActual)
{
    return This->lpVtbl->GetDeviceModels(This, cchDeviceModels, wzDeviceModels, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_GetFileExtensions_Proxy(IWICBitmapCodecInfo *This, UINT cchFileExtensions, WCHAR *wzFileExtensions, UINT *pcchActual)
{
    return This->lpVtbl->GetFileExtensions(This, cchFileExtensions, wzFileExtensions, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapCodecInfo_GetMimeTypes_Proxy(IWICBitmapCodecInfo *This, UINT cchMimeTypes, WCHAR *wzMimeTypes, UINT *pcchActual)
{
    return This->lpVtbl->GetMimeTypes(This, cchMimeTypes, wzMimeTypes, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_CopyPalette_Proxy(IWICBitmapDecoder *This, IWICPalette *pIPalette)
{
    return This->lpVtbl->CopyPalette(This, pIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetColorContexts_Proxy(IWICBitmapDecoder *This, UINT cCount, IWICColorContext **ppIColorContexts, UINT *pcActualCount)
{
    return This->lpVtbl->GetColorContexts(This, cCount, ppIColorContexts, pcActualCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetDecoderInfo_Proxy(IWICBitmapDecoder *This, IWICBitmapDecoderInfo **ppIDecoderInfo)
{
    return This->lpVtbl->GetDecoderInfo(This, ppIDecoderInfo);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetFrameCount_Proxy(IWICBitmapDecoder *This, UINT *pCount)
{
    return This->lpVtbl->GetFrameCount(This, pCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetFrame_Proxy(IWICBitmapDecoder *This, UINT index, IWICBitmapFrameDecode **ppIBitmapFrame)
{
    return This->lpVtbl->GetFrame(This, index, ppIBitmapFrame);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetMetadataQueryReader_Proxy(IWICBitmapDecoder *This, IWICMetadataQueryReader **ppIMetadataQueryReader)
{
    return This->lpVtbl->GetMetadataQueryReader(This, ppIMetadataQueryReader);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetPreview_Proxy(IWICBitmapDecoder *This, IWICBitmapSource **ppIBitmapSource)
{
    return This->lpVtbl->GetPreview(This, ppIBitmapSource);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapDecoder_GetThumbnail_Proxy(IWICBitmapDecoder *This, IWICBitmapSource **ppIThumbnail)
{
    return This->lpVtbl->GetThumbnail(This, ppIThumbnail);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_Commit_Proxy(IWICBitmapEncoder *This)
{
    return This->lpVtbl->Commit(This);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_CreateNewFrame_Proxy(IWICBitmapEncoder *This, IWICBitmapFrameEncode **ppIFrameEncode, IPropertyBag2 **ppIEncoderOptions)
{
    return This->lpVtbl->CreateNewFrame(This, ppIFrameEncode, ppIEncoderOptions);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_GetEncoderInfo_Proxy(IWICBitmapEncoder *This, IWICBitmapEncoderInfo **ppIEncoderInfo)
{
    return This->lpVtbl->GetEncoderInfo(This, ppIEncoderInfo);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_GetMetadataQueryWriter_Proxy(IWICBitmapEncoder *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, ppIMetadataQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_Initialize_Proxy(IWICBitmapEncoder *This, IStream *pIStream, WICBitmapEncoderCacheOption cacheOption)
{
    return This->lpVtbl->Initialize(This, pIStream, cacheOption);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_SetPalette_Proxy(IWICBitmapEncoder *This, IWICPalette *pIPalette)
{
    return This->lpVtbl->SetPalette(This, pIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapEncoder_SetThumbnail_Proxy(IWICBitmapEncoder *This, IWICBitmapSource *pIThumbnail)
{
    return This->lpVtbl->SetThumbnail(This, pIThumbnail);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFlipRotator_Initialize_Proxy(IWICBitmapFlipRotator *This, IWICBitmapSource *pISource, WICBitmapTransformOptions options)
{
    return This->lpVtbl->Initialize(This, pISource, options);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameDecode_GetColorContexts_Proxy(IWICBitmapFrameDecode *This, UINT cCount, IWICColorContext **ppIColorContexts, UINT *pcActualCount)
{
    return This->lpVtbl->GetColorContexts(This, cCount, ppIColorContexts, pcActualCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameDecode_GetMetadataQueryReader_Proxy(IWICBitmapFrameDecode *This, IWICMetadataQueryReader **ppIMetadataQueryReader)
{
    return This->lpVtbl->GetMetadataQueryReader(This, ppIMetadataQueryReader);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameDecode_GetThumbnail_Proxy(IWICBitmapFrameDecode *This, IWICBitmapSource **ppIThumbnail)
{
    return This->lpVtbl->GetThumbnail(This, ppIThumbnail);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_Commit_Proxy(IWICBitmapFrameEncode *This)
{
    return This->lpVtbl->Commit(This);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_GetMetadataQueryWriter_Proxy(IWICBitmapFrameEncode *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, ppIMetadataQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_Initialize_Proxy(IWICBitmapFrameEncode *This, IPropertyBag2 *pIEncoderOptions)
{
    return This->lpVtbl->Initialize(This, pIEncoderOptions);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_SetColorContexts_Proxy(IWICBitmapFrameEncode *This, UINT cCount, IWICColorContext **ppIColorContext)
{
    return This->lpVtbl->SetColorContexts(This, cCount, ppIColorContext);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_SetResolution_Proxy(IWICBitmapFrameEncode *This, double dpiX, double dpiY)
{
    return This->lpVtbl->SetResolution(This, dpiX, dpiY);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_SetSize_Proxy(IWICBitmapFrameEncode *This, UINT uiWidth, UINT uiHeight)
{
    return This->lpVtbl->SetSize(This, uiWidth, uiHeight);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_SetThumbnail_Proxy(IWICBitmapFrameEncode *This, IWICBitmapSource *pIThumbnail)
{
    return This->lpVtbl->SetThumbnail(This, pIThumbnail);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapFrameEncode_WriteSource_Proxy(IWICBitmapFrameEncode *This, IWICBitmapSource *pIBitmapSource, WICRect *prc)
{
    return This->lpVtbl->WriteSource(This, pIBitmapSource, prc);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapLock_GetDataPointer_STA_Proxy(IWICBitmapLock *This, UINT *pcbBufferSize, BYTE **ppbData)
{
    return This->lpVtbl->GetDataPointer(This, pcbBufferSize, ppbData);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapLock_GetStride_Proxy(IWICBitmapLock *This, UINT *pcbStride)
{
    return This->lpVtbl->GetStride(This, pcbStride);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmap_Lock_Proxy(IWICBitmap *This, const WICRect *prcLock, DWORD flags, IWICBitmapLock **ppILock)
{
    return This->lpVtbl->Lock(This, prcLock, flags, ppILock);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapScaler_Initialize_Proxy(IWICBitmapScaler *This, IWICBitmapSource *pISource, UINT uiWidth, UINT uiHeight, WICBitmapInterpolationMode mode)
{
    return This->lpVtbl->Initialize(This, pISource, uiWidth, uiHeight, mode);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmap_SetPalette_Proxy(IWICBitmap *This, IWICPalette *pIPalette)
{
    return This->lpVtbl->SetPalette(This, pIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmap_SetResolution_Proxy(IWICBitmap *This, double dpiX, double dpiY)
{
    return This->lpVtbl->SetResolution(This, dpiX, dpiY);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapSource_CopyPalette_Proxy(IWICBitmapSource *This, IWICPalette *pIPalette)
{
    return This->lpVtbl->CopyPalette(This, pIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapSource_CopyPixels_Proxy(IWICBitmapSource *This, const WICRect *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer)
{
    return This->lpVtbl->CopyPixels(This, prc, cbStride, cbBufferSize, pbBuffer);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapSource_GetPixelFormat_Proxy(IWICBitmapSource *This, WICPixelFormatGUID *pPixelFormat)
{
    return This->lpVtbl->GetPixelFormat(This, pPixelFormat);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapSource_GetResolution_Proxy(IWICBitmapSource *This, double *pDpiX, double *pDpiY)
{
    return This->lpVtbl->GetResolution(This, pDpiX, pDpiY);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICBitmapSource_GetSize_Proxy(IWICBitmapSource *This, UINT *puiWidth, UINT *puiHeight)
{
    return This->lpVtbl->GetSize(This, puiWidth, puiHeight);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICColorContext_InitializeFromMemory_Proxy(IWICColorContext *This, const BYTE *pbBuffer, UINT cbBufferSize)
{
    return This->lpVtbl->InitializeFromMemory(This, pbBuffer, cbBufferSize);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentFactory_CreateMetadataWriterFromReader_Proxy(IWICComponentFactory *This, IWICMetadataReader *pIReader, const GUID *pguidVendor, IWICMetadataWriter **ppIWriter)
{
    return This->lpVtbl->CreateMetadataWriterFromReader(This, pIReader, pguidVendor, ppIWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentFactory_CreateQueryWriterFromBlockWriter_Proxy(IWICComponentFactory *This, IWICMetadataBlockWriter *pIBlockWriter, IWICMetadataQueryWriter **ppIQueryWriter)
{
    return This->lpVtbl->CreateQueryWriterFromBlockWriter(This, pIBlockWriter, ppIQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentInfo_GetAuthor_Proxy(IWICComponentInfo *This, UINT cchAuthor, WCHAR *wzAuthor, UINT *pcchActual)
{
    return This->lpVtbl->GetAuthor(This, cchAuthor, wzAuthor, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentInfo_GetCLSID_Proxy(IWICComponentInfo *This, CLSID *pclsid)
{
    return This->lpVtbl->GetCLSID(This, pclsid);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentInfo_GetFriendlyName_Proxy(IWICComponentInfo *This, UINT cchFriendlyName, WCHAR *wzFriendlyName, UINT *pcchActual)
{
    return This->lpVtbl->GetFriendlyName(This, cchFriendlyName, wzFriendlyName, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentInfo_GetSpecVersion_Proxy(IWICComponentInfo *This, UINT cchSpecVersion, WCHAR *wzSpecVersion, UINT *pcchActual)
{
    return This->lpVtbl->GetSpecVersion(This, cchSpecVersion, wzSpecVersion, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICComponentInfo_GetVersion_Proxy(IWICComponentInfo *This, UINT cchVersion, WCHAR *wzVersion, UINT *pcchActual)
{
    return This->lpVtbl->GetVersion(This, cchVersion, wzVersion, pcchActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICFastMetadataEncoder_Commit_Proxy(IWICFastMetadataEncoder *This)
{
    return This->lpVtbl->Commit(This);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICFastMetadataEncoder_GetMetadataQueryWriter_Proxy(IWICFastMetadataEncoder *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, ppIMetadataQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICFormatConverter_Initialize_Proxy(IWICFormatConverter *This, IWICBitmapSource *pISource, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither, IWICPalette *pIPalette, double alphaThresholdPercent, WICBitmapPaletteType paletteTranslate)
{
    return This->lpVtbl->Initialize(This, pISource, dstFormat, dither, pIPalette, alphaThresholdPercent, paletteTranslate);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapClipper_Proxy(IWICImagingFactory *This, IWICBitmapClipper **ppIBitmapClipper)
{
    return This->lpVtbl->CreateBitmapClipper(This, ppIBitmapClipper);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapFlipRotator_Proxy(IWICImagingFactory *This, IWICBitmapFlipRotator **ppIBitmapFlipRotator)
{
    return This->lpVtbl->CreateBitmapFlipRotator(This, ppIBitmapFlipRotator);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapFromHBITMAP_Proxy(IWICImagingFactory *This, HBITMAP hBitmap, HPALETTE hPalette, WICBitmapAlphaChannelOption options, IWICBitmap **ppIBitmap)
{
    return This->lpVtbl->CreateBitmapFromHBITMAP(This, hBitmap, hPalette, options, ppIBitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapFromHICON_Proxy(IWICImagingFactory *This, HICON hIcon, IWICBitmap **ppIBitmap)
{
    return This->lpVtbl->CreateBitmapFromHICON(This, hIcon, ppIBitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapFromMemory_Proxy(IWICImagingFactory *This, UINT uiWidth, UINT uiHeight, REFWICPixelFormatGUID pixelFormat, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, IWICBitmap **ppIBitmap)
{
    return This->lpVtbl->CreateBitmapFromMemory(This, uiWidth, uiHeight, pixelFormat, cbStride, cbBufferSize, pbBuffer, ppIBitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapFromSource_Proxy(IWICImagingFactory *This, IWICBitmapSource *piBitmapSource, WICBitmapCreateCacheOption option, IWICBitmap **ppIBitmap)
{
    return This->lpVtbl->CreateBitmapFromSource(This, piBitmapSource, option, ppIBitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmap_Proxy(IWICImagingFactory *This, UINT uiWidth, UINT uiHeight, REFWICPixelFormatGUID pixelFormat, WICBitmapCreateCacheOption option, IWICBitmap **ppIBitmap)
{
    return This->lpVtbl->CreateBitmap(This, uiWidth, uiHeight, pixelFormat, option, ppIBitmap);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateBitmapScaler_Proxy(IWICImagingFactory *This, IWICBitmapScaler **ppIBitmapScaler)
{
    return This->lpVtbl->CreateBitmapScaler(This, ppIBitmapScaler);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateComponentInfo_Proxy(IWICImagingFactory *This, REFCLSID clsidComponent, IWICComponentInfo **ppIInfo)
{
    return This->lpVtbl->CreateComponentInfo(This, clsidComponent, ppIInfo);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateDecoderFromFileHandle_Proxy(IWICImagingFactory *This, ULONG_PTR hFile, const GUID *pguidVendor, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder)
{
    return This->lpVtbl->CreateDecoderFromFileHandle(This, hFile, pguidVendor, metadataOptions, ppIDecoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateDecoderFromFilename_Proxy(IWICImagingFactory *This, LPCWSTR wzFilename, const GUID *pguidVendor, DWORD dwDesiredAccess, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder)
{
    return This->lpVtbl->CreateDecoderFromFilename(This, wzFilename, pguidVendor, dwDesiredAccess, metadataOptions, ppIDecoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateDecoderFromStream_Proxy(IWICImagingFactory *This, IStream *pIStream, const GUID *pguidVendor, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder)
{
    return This->lpVtbl->CreateDecoderFromStream(This, pIStream, pguidVendor, metadataOptions, ppIDecoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateEncoder_Proxy(IWICImagingFactory *This, REFGUID guidContainerFormat, const GUID *pguidVendor, IWICBitmapEncoder **ppIEncoder)
{
    return This->lpVtbl->CreateEncoder(This, guidContainerFormat, pguidVendor, ppIEncoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateFastMetadataEncoderFromDecoder_Proxy(IWICImagingFactory *This, IWICBitmapDecoder *pIDecoder, IWICFastMetadataEncoder **ppIFastEncoder)
{
    return This->lpVtbl->CreateFastMetadataEncoderFromDecoder(This, pIDecoder, ppIFastEncoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateFastMetadataEncoderFromFrameDecode_Proxy(IWICImagingFactory *This, IWICBitmapFrameDecode *pIFrameDecoder, IWICFastMetadataEncoder **ppIFastEncoder)
{
    return This->lpVtbl->CreateFastMetadataEncoderFromFrameDecode(This, pIFrameDecoder, ppIFastEncoder);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateFormatConverter_Proxy(IWICImagingFactory *This, IWICFormatConverter **ppIFormatConverter)
{
    return This->lpVtbl->CreateFormatConverter(This, ppIFormatConverter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreatePalette_Proxy(IWICImagingFactory *This, IWICPalette **ppIPalette)
{
    return This->lpVtbl->CreatePalette(This, ppIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateQueryWriterFromReader_Proxy(IWICImagingFactory *This, IWICMetadataQueryReader *pIQueryReader, const GUID *pguidVendor, IWICMetadataQueryWriter **ppIQueryWriter)
{
    return This->lpVtbl->CreateQueryWriterFromReader(This, pIQueryReader, pguidVendor, ppIQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateQueryWriter_Proxy(IWICImagingFactory *This, REFGUID guidMetadataFormat, const GUID *pguidVendor, IWICMetadataQueryWriter **ppIQueryWriter)
{
    return This->lpVtbl->CreateQueryWriter(This, guidMetadataFormat, pguidVendor, ppIQueryWriter);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICImagingFactory_CreateStream_Proxy(IWICImagingFactory *This, IWICStream **ppIWICStream)
{
    return This->lpVtbl->CreateStream(This, ppIWICStream);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataBlockReader_GetCount_Proxy(IWICMetadataBlockReader *This, UINT *pcCount)
{
    return This->lpVtbl->GetCount(This, pcCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataBlockReader_GetReaderByIndex_Proxy(IWICMetadataBlockReader *This, UINT nIndex, IWICMetadataReader **ppIMetadataReader)
{
    return This->lpVtbl->GetReaderByIndex(This, nIndex, ppIMetadataReader);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryReader_GetContainerFormat_Proxy(IWICMetadataQueryReader *This, GUID *pguidContainerFormat)
{
    return This->lpVtbl->GetContainerFormat(This, pguidContainerFormat);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryReader_GetEnumerator_Proxy(IWICMetadataQueryReader *This, IEnumString **ppIEnumString)
{
    return This->lpVtbl->GetEnumerator(This, ppIEnumString);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryReader_GetLocation_Proxy(IWICMetadataQueryReader *This, UINT cchMaxLength, WCHAR *wzNamespace, UINT *pcchActualLength)
{
    return This->lpVtbl->GetLocation(This, cchMaxLength, wzNamespace, pcchActualLength);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryReader_GetMetadataByName_Proxy(IWICMetadataQueryReader *This, LPCWSTR wzName, PROPVARIANT *pvarValue)
{
    return This->lpVtbl->GetMetadataByName(This, wzName, pvarValue);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryWriter_RemoveMetadataByName_Proxy(IWICMetadataQueryWriter *This, LPCWSTR wzName)
{
    return This->lpVtbl->RemoveMetadataByName(This, wzName);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICMetadataQueryWriter_SetMetadataByName_Proxy(IWICMetadataQueryWriter *This, LPCWSTR wzName, const PROPVARIANT *pvarValue)
{
    return This->lpVtbl->SetMetadataByName(This, wzName, pvarValue);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_GetColorCount_Proxy(IWICPalette *This, UINT *pcCount)
{
    return This->lpVtbl->GetColorCount(This, pcCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_GetColors_Proxy(IWICPalette *This, UINT colorCount, WICColor *pColors, UINT *pcActualColors)
{
    return This->lpVtbl->GetColors(This, colorCount, pColors, pcActualColors);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_GetType_Proxy(IWICPalette *This, WICBitmapPaletteType *pePaletteType)
{
    return This->lpVtbl->GetType(This, pePaletteType);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_HasAlpha_Proxy(IWICPalette *This, BOOL *pfHasAlpha)
{
    return This->lpVtbl->HasAlpha(This, pfHasAlpha);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_InitializeCustom_Proxy(IWICPalette *This, WICColor *pColors, UINT colorCount)
{
    return This->lpVtbl->InitializeCustom(This, pColors, colorCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_InitializeFromBitmap_Proxy(IWICPalette *This, IWICBitmapSource *pISurface, UINT colorCount, BOOL fAddTransparentColor)
{
    return This->lpVtbl->InitializeFromBitmap(This, pISurface, colorCount, fAddTransparentColor);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_InitializeFromPalette_Proxy(IWICPalette *This, IWICPalette *pIPalette)
{
    return This->lpVtbl->InitializeFromPalette(This, pIPalette);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPalette_InitializePredefined_Proxy(IWICPalette *This, WICBitmapPaletteType ePaletteType, BOOL fAddTransparentColor)
{
    return This->lpVtbl->InitializePredefined(This, ePaletteType, fAddTransparentColor);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPixelFormatInfo_GetBitsPerPixel_Proxy(IWICPixelFormatInfo *This, UINT *puiBitsPerPixel)
{
    return This->lpVtbl->GetBitsPerPixel(This, puiBitsPerPixel);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPixelFormatInfo_GetChannelCount_Proxy(IWICPixelFormatInfo *This, UINT *puiChannelCount)
{
    return This->lpVtbl->GetChannelCount(This, puiChannelCount);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICPixelFormatInfo_GetChannelMask_Proxy(IWICPixelFormatInfo *This, UINT uiChannelIndex, UINT cbMaskBuffer, BYTE *pbMaskBuffer, UINT *pcbActual)
{
    return This->lpVtbl->GetChannelMask(This, uiChannelIndex, cbMaskBuffer, pbMaskBuffer, pcbActual);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICStream_InitializeFromIStream_Proxy(IWICStream *This, IStream *pIStream)
{
    return This->lpVtbl->InitializeFromIStream(This, pIStream);
}

DECLSPEC_EXPORT HRESULT WINAPI IWICStream_InitializeFromMemory_Proxy(IWICStream *This, BYTE *pbBuffer, DWORD cbBufferSize)
{
    return This->lpVtbl->InitializeFromMemory(This, pbBuffer, cbBufferSize);
}

DECLSPEC_EXPORT HRESULT WINAPI IEnumString_Next_WIC_Proxy(IEnumString *This, ULONG count, LPOLESTR *items, ULONG *fetched)
{
    /* the vtable of IEnumString: the three of IUnknown, then Next, Skip and Reset. */
    HRESULT (STDMETHODCALLTYPE *next)(IEnumString *, ULONG, LPOLESTR *, ULONG *) = (*(void ***)This)[3];
    return next(This, count, items, fetched);
}

DECLSPEC_EXPORT HRESULT WINAPI IEnumString_Reset_WIC_Proxy(IEnumString *This)
{
    HRESULT (STDMETHODCALLTYPE *reset)(IEnumString *) = (*(void ***)This)[5];
    return reset(This);
}

DECLSPEC_EXPORT HRESULT WINAPI IPropertyBag2_Write_Proxy(IPropertyBag2 *This, ULONG count, PROPBAG2 *props, VARIANT *values)
{
    return This->lpVtbl->Write(This, count, props, values);
}
