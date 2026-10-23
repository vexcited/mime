/*
 * windows imaging component of windowscodecs.dll: decoders, encoders, pixel format converters, scalers and
 * metadata. method order and guids match windows, since programs call through the vtables and ask by guid.
 */
#ifndef MIME_WIN32_WINCODEC_H
#define MIME_WIN32_WINCODEC_H

#include <mime/win32/objbase.h>
#include <mime/win32/objidl.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/propidl.h>
#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_WINDOWSCODECS
#define WINCODECAPI DECLSPEC_EXPORT
#else
#define WINCODECAPI DECLSPEC_IMPORT
#endif

#define WINCODEC_SDK_VERSION1 0x0236
#define WINCODEC_SDK_VERSION2 0x0237

/* ------------------------------------------------------------------ errors */

#define WINCODEC_ERR_GENERIC_ERROR E_FAIL
#define WINCODEC_ERR_INVALIDPARAMETER E_INVALIDARG
#define WINCODEC_ERR_OUTOFMEMORY E_OUTOFMEMORY
#define WINCODEC_ERR_NOTIMPLEMENTED E_NOTIMPL
#define WINCODEC_ERR_ABORTED E_ABORT
#define WINCODEC_ERR_ACCESSDENIED E_ACCESSDENIED
#ifndef INTSAFE_E_ARITHMETIC_OVERFLOW
#define INTSAFE_E_ARITHMETIC_OVERFLOW ((HRESULT)0x80070216L)
#endif
#define WINCODEC_ERR_VALUEOVERFLOW INTSAFE_E_ARITHMETIC_OVERFLOW
#define WINCODEC_ERR_WRONGSTATE ((HRESULT)0x88982f04L)
#define WINCODEC_ERR_VALUEOUTOFRANGE ((HRESULT)0x88982f05L)
#define WINCODEC_ERR_UNKNOWNIMAGEFORMAT ((HRESULT)0x88982f07L)
#define WINCODEC_ERR_UNSUPPORTEDVERSION ((HRESULT)0x88982f0bL)
#define WINCODEC_ERR_NOTINITIALIZED ((HRESULT)0x88982f0cL)
#define WINCODEC_ERR_ALREADYLOCKED ((HRESULT)0x88982f0dL)
#define WINCODEC_ERR_PROPERTYNOTFOUND ((HRESULT)0x88982f40L)
#define WINCODEC_ERR_PROPERTYNOTSUPPORTED ((HRESULT)0x88982f41L)
#define WINCODEC_ERR_PROPERTYSIZE ((HRESULT)0x88982f42L)
#define WINCODEC_ERR_CODECPRESENT ((HRESULT)0x88982f43L)
#define WINCODEC_ERR_CODECNOTHUMBNAIL ((HRESULT)0x88982f44L)
#define WINCODEC_ERR_PALETTEUNAVAILABLE ((HRESULT)0x88982f45L)
#define WINCODEC_ERR_CODECTOOMANYSCANLINES ((HRESULT)0x88982f46L)
#define WINCODEC_ERR_INTERNALERROR ((HRESULT)0x88982f48L)
#define WINCODEC_ERR_SOURCERECTDOESNOTMATCHDIMENSIONS ((HRESULT)0x88982f49L)
#define WINCODEC_ERR_COMPONENTNOTFOUND ((HRESULT)0x88982f50L)
#define WINCODEC_ERR_IMAGESIZEOUTOFRANGE ((HRESULT)0x88982f51L)
#define WINCODEC_ERR_TOOMUCHMETADATA ((HRESULT)0x88982f52L)
#define WINCODEC_ERR_BADIMAGE ((HRESULT)0x88982f60L)
#define WINCODEC_ERR_BADHEADER ((HRESULT)0x88982f61L)
#define WINCODEC_ERR_FRAMEMISSING ((HRESULT)0x88982f62L)
#define WINCODEC_ERR_BADMETADATAHEADER ((HRESULT)0x88982f63L)
#define WINCODEC_ERR_BADSTREAMDATA ((HRESULT)0x88982f70L)
#define WINCODEC_ERR_STREAMWRITE ((HRESULT)0x88982f71L)
#define WINCODEC_ERR_STREAMREAD ((HRESULT)0x88982f72L)
#define WINCODEC_ERR_STREAMNOTAVAILABLE ((HRESULT)0x88982f73L)
#define WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT ((HRESULT)0x88982f80L)
#define WINCODEC_ERR_UNSUPPORTEDOPERATION ((HRESULT)0x88982f81L)
#define WINCODEC_ERR_INVALIDREGISTRATION ((HRESULT)0x88982f8aL)
#define WINCODEC_ERR_COMPONENTINITIALIZEFAILURE ((HRESULT)0x88982f8bL)
#define WINCODEC_ERR_INSUFFICIENTBUFFER ((HRESULT)0x88982f8cL)
#define WINCODEC_ERR_DUPLICATEMETADATAPRESENT ((HRESULT)0x88982f8dL)
#define WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE ((HRESULT)0x88982f8eL)
#define WINCODEC_ERR_UNEXPECTEDSIZE ((HRESULT)0x88982f8fL)
#define WINCODEC_ERR_INVALIDQUERYREQUEST ((HRESULT)0x88982f90L)
#define WINCODEC_ERR_UNEXPECTEDMETADATATYPE ((HRESULT)0x88982f91L)
#define WINCODEC_ERR_REQUESTONLYVALIDATMETADATAROOT ((HRESULT)0x88982f92L)
#define WINCODEC_ERR_INVALIDQUERYCHARACTER ((HRESULT)0x88982f93L)
#define WINCODEC_ERR_WIN32ERROR ((HRESULT)0x88982f94L)
#define WINCODEC_ERR_INVALIDPROGRESSIVELEVEL ((HRESULT)0x88982f95L)
#define WINCODEC_ERR_INVALIDJPEGSCANINDEX ((HRESULT)0x88982f96L)

/* ------------------------------------------------------------------ types */

typedef GUID WICPixelFormatGUID;
typedef const GUID *REFWICPixelFormatGUID;
typedef BYTE *WICInProcPointer;
typedef UINT32 WICColor;

typedef struct WICRect {
    INT X;
    INT Y;
    INT Width;
    INT Height;
} WICRect;

typedef enum WICColorContextType {
    WICColorContextUninitialized = 0,
    WICColorContextProfile = 1,
    WICColorContextExifColorSpace = 2
} WICColorContextType;

typedef enum WICDecodeOptions {
    WICDecodeMetadataCacheOnDemand = 0,
    WICDecodeMetadataCacheOnLoad = 0x1
} WICDecodeOptions;

typedef enum WICBitmapCreateCacheOption {
    WICBitmapNoCache = 0,
    WICBitmapCacheOnDemand = 0x1,
    WICBitmapCacheOnLoad = 0x2
} WICBitmapCreateCacheOption;

typedef enum WICBitmapAlphaChannelOption {
    WICBitmapUseAlpha = 0,
    WICBitmapUsePremultipliedAlpha = 0x1,
    WICBitmapIgnoreAlpha = 0x2
} WICBitmapAlphaChannelOption;

typedef enum WICBitmapDecoderCapabilities {
    WICBitmapDecoderCapabilitySameEncoder = 0x1,
    WICBitmapDecoderCapabilityCanDecodeAllImages = 0x2,
    WICBitmapDecoderCapabilityCanDecodeSomeImages = 0x4,
    WICBitmapDecoderCapabilityCanEnumerateMetadata = 0x8,
    WICBitmapDecoderCapabilityCanDecodeThumbnail = 0x10
} WICBitmapDecoderCapabilities;

typedef enum WICBitmapDitherType {
    WICBitmapDitherTypeNone = 0,
    WICBitmapDitherTypeSolid = 0,
    WICBitmapDitherTypeOrdered4x4 = 0x1,
    WICBitmapDitherTypeOrdered8x8 = 0x2,
    WICBitmapDitherTypeOrdered16x16 = 0x3,
    WICBitmapDitherTypeSpiral4x4 = 0x4,
    WICBitmapDitherTypeSpiral8x8 = 0x5,
    WICBitmapDitherTypeDualSpiral4x4 = 0x6,
    WICBitmapDitherTypeDualSpiral8x8 = 0x7,
    WICBitmapDitherTypeErrorDiffusion = 0x8
} WICBitmapDitherType;

typedef enum WICBitmapPaletteType {
    WICBitmapPaletteTypeCustom = 0,
    WICBitmapPaletteTypeMedianCut = 0x1,
    WICBitmapPaletteTypeFixedBW = 0x2,
    WICBitmapPaletteTypeFixedHalftone8 = 0x3,
    WICBitmapPaletteTypeFixedHalftone27 = 0x4,
    WICBitmapPaletteTypeFixedHalftone64 = 0x5,
    WICBitmapPaletteTypeFixedHalftone125 = 0x6,
    WICBitmapPaletteTypeFixedHalftone216 = 0x7,
    WICBitmapPaletteTypeFixedWebPalette = WICBitmapPaletteTypeFixedHalftone216,
    WICBitmapPaletteTypeFixedHalftone252 = 0x8,
    WICBitmapPaletteTypeFixedHalftone256 = 0x9,
    WICBitmapPaletteTypeFixedGray4 = 0xa,
    WICBitmapPaletteTypeFixedGray16 = 0xb,
    WICBitmapPaletteTypeFixedGray256 = 0xc
} WICBitmapPaletteType;

typedef enum WICBitmapInterpolationMode {
    WICBitmapInterpolationModeNearestNeighbor = 0,
    WICBitmapInterpolationModeLinear = 0x1,
    WICBitmapInterpolationModeCubic = 0x2,
    WICBitmapInterpolationModeFant = 0x3,
    WICBitmapInterpolationModeHighQualityCubic = 0x4
} WICBitmapInterpolationMode;

typedef enum WICBitmapLockFlags {
    WICBitmapLockRead = 0x1,
    WICBitmapLockWrite = 0x2
} WICBitmapLockFlags;

typedef enum WICBitmapTransformOptions {
    WICBitmapTransformRotate0 = 0,
    WICBitmapTransformRotate90 = 0x1,
    WICBitmapTransformRotate180 = 0x2,
    WICBitmapTransformRotate270 = 0x3,
    WICBitmapTransformFlipHorizontal = 0x8,
    WICBitmapTransformFlipVertical = 0x10
} WICBitmapTransformOptions;

typedef enum WICBitmapEncoderCacheOption {
    WICBitmapEncoderCacheInMemory = 0,
    WICBitmapEncoderCacheTempFile = 0x1,
    WICBitmapEncoderNoCache = 0x2
} WICBitmapEncoderCacheOption;

typedef enum WICComponentType {
    WICDecoder = 0x1,
    WICEncoder = 0x2,
    WICPixelFormatConverter = 0x4,
    WICMetadataReader = 0x8,
    WICMetadataWriter = 0x10,
    WICPixelFormat = 0x20,
    WICAllComponents = 0x3f
} WICComponentType;

typedef enum WICComponentEnumerateOptions {
    WICComponentEnumerateDefault = 0,
    WICComponentEnumerateRefresh = 0x1,
    WICComponentEnumerateDisabled = 0x80000000,
    WICComponentEnumerateUnsigned = 0x40000000,
    WICComponentEnumerateBuiltInOnly = 0x20000000
} WICComponentEnumerateOptions;

typedef enum WICComponentSigning {
    WICComponentSigned = 0x1,
    WICComponentUnsigned = 0x2,
    WICComponentSafe = 0x4,
    WICComponentDisabled = 0x80000000
} WICComponentSigning;

typedef enum WICTiffCompressionOption {
    WICTiffCompressionDontCare = 0,
    WICTiffCompressionNone = 0x1,
    WICTiffCompressionCCITT3 = 0x2,
    WICTiffCompressionCCITT4 = 0x3,
    WICTiffCompressionLZW = 0x4,
    WICTiffCompressionRLE = 0x5,
    WICTiffCompressionZIP = 0x6,
    WICTiffCompressionLZWHDifferencing = 0x7
} WICTiffCompressionOption;

typedef enum WICJpegYCrCbSubsamplingOption {
    WICJpegYCrCbSubsamplingDefault = 0,
    WICJpegYCrCbSubsampling420 = 0x1,
    WICJpegYCrCbSubsampling422 = 0x2,
    WICJpegYCrCbSubsampling444 = 0x3,
    WICJpegYCrCbSubsampling440 = 0x4
} WICJpegYCrCbSubsamplingOption;

typedef enum WICPngFilterOption {
    WICPngFilterUnspecified = 0,
    WICPngFilterNone = 0x1,
    WICPngFilterSub = 0x2,
    WICPngFilterUp = 0x3,
    WICPngFilterAverage = 0x4,
    WICPngFilterPaeth = 0x5,
    WICPngFilterAdaptive = 0x6
} WICPngFilterOption;

typedef enum WICNamedWhitePoint {
    WICWhitePointDefault = 0x1,
    WICWhitePointDaylight = 0x2,
    WICWhitePointCloudy = 0x4,
    WICWhitePointShade = 0x8,
    WICWhitePointTungsten = 0x10,
    WICWhitePointFluorescent = 0x20,
    WICWhitePointFlash = 0x40,
    WICWhitePointUnderwater = 0x80,
    WICWhitePointCustom = 0x100,
    WICWhitePointAutoWhiteBalance = 0x200,
    WICWhitePointAsShot = WICWhitePointDefault
} WICNamedWhitePoint;

typedef enum WICPixelFormatNumericRepresentation {
    WICPixelFormatNumericRepresentationUnspecified = 0,
    WICPixelFormatNumericRepresentationIndexed = 0x1,
    WICPixelFormatNumericRepresentationUnsignedInteger = 0x2,
    WICPixelFormatNumericRepresentationSignedInteger = 0x3,
    WICPixelFormatNumericRepresentationFixed = 0x4,
    WICPixelFormatNumericRepresentationFloat = 0x5
} WICPixelFormatNumericRepresentation;

typedef enum WICPlanarOptions {
    WICPlanarOptionsDefault = 0,
    WICPlanarOptionsPreserveSubsampling = 0x1
} WICPlanarOptions;

typedef enum WICJpegIndexingOptions {
    WICJpegIndexingOptionsGenerateOnDemand = 0,
    WICJpegIndexingOptionsGenerateOnLoad = 0x1
} WICJpegIndexingOptions;

typedef enum WICJpegTransferMatrix {
    WICJpegTransferMatrixIdentity = 0,
    WICJpegTransferMatrixBT601 = 1
} WICJpegTransferMatrix;

typedef enum WICJpegScanType {
    WICJpegScanTypeInterleaved = 0,
    WICJpegScanTypePlanarComponents = 1,
    WICJpegScanTypeProgressive = 2
} WICJpegScanType;

typedef enum WICDdsDimension {
    WICDdsTexture1D = 0,
    WICDdsTexture2D = 1,
    WICDdsTexture3D = 2,
    WICDdsTextureCube = 3
} WICDdsDimension;

typedef enum WICDdsAlphaMode {
    WICDdsAlphaModeUnknown = 0,
    WICDdsAlphaModeStraight = 1,
    WICDdsAlphaModePremultiplied = 2,
    WICDdsAlphaModeOpaque = 3,
    WICDdsAlphaModeCustom = 4
} WICDdsAlphaMode;

typedef enum WICMetadataCreationOptions {
    WICMetadataCreationDefault = 0,
    WICMetadataCreationAllowUnknown = 0,
    WICMetadataCreationFailUnknown = 0x10000,
    WICMetadataCreationMask = 0xffff0000
} WICMetadataCreationOptions;

typedef enum WICPersistOptions {
    WICPersistOptionDefault = 0,
    WICPersistOptionLittleEndian = 0,
    WICPersistOptionBigEndian = 0x1,
    WICPersistOptionStrictFormat = 0x2,
    WICPersistOptionNoCacheStream = 0x4,
    WICPersistOptionPreferUTF8 = 0x8,
    WICPersistOptionMask = 0xffff
} WICPersistOptions;

typedef struct WICBitmapPattern {
    ULARGE_INTEGER Position;
    ULONG Length;
    BYTE *Pattern;
    BYTE *Mask;
    BOOL EndOfStream;
} WICBitmapPattern;

typedef struct WICMetadataPattern {
    ULARGE_INTEGER Position;
    ULONG Length;
    BYTE *Pattern;
    BYTE *Mask;
    ULARGE_INTEGER DataOffset;
} WICMetadataPattern;

typedef struct WICMetadataHeader {
    ULARGE_INTEGER Position;
    ULONG Length;
    BYTE *Header;
    ULARGE_INTEGER DataOffset;
} WICMetadataHeader;

typedef struct WICBitmapPlaneDescription {
    WICPixelFormatGUID Format;
    UINT Width;
    UINT Height;
} WICBitmapPlaneDescription;

typedef struct WICBitmapPlane {
    WICPixelFormatGUID Format;
    BYTE *pbBuffer;
    UINT cbStride;
    UINT cbBufferSize;
} WICBitmapPlane;

typedef struct WICJpegFrameHeader {
    UINT Width;
    UINT Height;
    WICJpegTransferMatrix TransferMatrix;
    WICJpegScanType ScanType;
    UINT cComponents;
    DWORD ComponentIdentification;
    DWORD SampleFactors;
    DWORD QuantizationTableIndices;
} WICJpegFrameHeader;

typedef struct WICJpegScanHeader {
    UINT cComponents;
    UINT RestartInterval;
    DWORD ComponentSelectors;
    DWORD HuffmanTableIndices;
    BYTE StartSpectralSelection;
    BYTE EndSpectralSelection;
    BYTE SuccessiveApproximationHigh;
    BYTE SuccessiveApproximationLow;
} WICJpegScanHeader;

typedef struct WICDdsParameters {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    UINT ArraySize;
    UINT DxgiFormat;
    WICDdsDimension Dimension;
    WICDdsAlphaMode AlphaMode;
} WICDdsParameters;

typedef struct WICDdsFormatInfo {
    UINT DxgiFormat;
    UINT BytesPerBlock;
    UINT BlockWidth;
    UINT BlockHeight;
} WICDdsFormatInfo;

typedef struct WICImageParameters {
    UINT PixelFormatFormat;
    UINT PixelFormatAlphaMode;
    FLOAT DpiX;
    FLOAT DpiY;
    FLOAT Top;
    FLOAT Left;
    UINT32 PixelWidth;
    UINT32 PixelHeight;
} WICImageParameters;

typedef struct IWICImageEncoder IWICImageEncoder;
typedef struct ID2D1Device ID2D1Device;
typedef struct ID2D1Image ID2D1Image;
typedef struct ID2D1Bitmap1 ID2D1Bitmap1;
typedef struct ID2D1DeviceContext ID2D1DeviceContext;

/* property bags for codec options. */
typedef struct IErrorLog IErrorLog;
typedef struct IEnumUnknown IEnumUnknown;

typedef struct tagPROPBAG2 {
    DWORD dwType;
    VARTYPE vt;
    CLIPFORMAT cfType;
    DWORD dwHint;
    LPOLESTR pstrName;
    CLSID clsid;
} PROPBAG2;

#define PROPBAG2_TYPE_UNDEFINED 0
#define PROPBAG2_TYPE_DATA 1
#define PROPBAG2_TYPE_URL 2
#define PROPBAG2_TYPE_OBJECT 3
#define PROPBAG2_TYPE_STREAM 4
#define PROPBAG2_TYPE_STORAGE 5
#define PROPBAG2_TYPE_MONIKER 6

typedef struct IPropertyBag2 IPropertyBag2;
typedef struct IWICPalette IWICPalette;
typedef struct IWICBitmapSource IWICBitmapSource;
typedef struct IWICMetadataQueryReader IWICMetadataQueryReader;
typedef struct IWICMetadataQueryWriter IWICMetadataQueryWriter;
typedef struct IWICMetadataReader IWICMetadataReader;
typedef struct IWICMetadataWriter IWICMetadataWriter;
typedef struct IWICBitmapDecoder IWICBitmapDecoder;
typedef struct IWICBitmapDecoderInfo IWICBitmapDecoderInfo;
typedef struct IWICBitmapEncoder IWICBitmapEncoder;
typedef struct IWICBitmapEncoderInfo IWICBitmapEncoderInfo;
typedef struct IWICBitmapFrameDecode IWICBitmapFrameDecode;
typedef struct IWICBitmapFrameEncode IWICBitmapFrameEncode;
typedef struct IWICColorContext IWICColorContext;
typedef struct IWICComponentInfo IWICComponentInfo;
typedef struct IWICBitmap IWICBitmap;
typedef struct IWICBitmapLock IWICBitmapLock;
typedef struct IWICStream IWICStream;
typedef struct IWICBitmapScaler IWICBitmapScaler;
typedef struct IWICBitmapClipper IWICBitmapClipper;
typedef struct IWICBitmapFlipRotator IWICBitmapFlipRotator;
typedef struct IWICFormatConverter IWICFormatConverter;
typedef struct IWICFormatConverterInfo IWICFormatConverterInfo;
typedef struct IWICColorTransform IWICColorTransform;
typedef struct IWICFastMetadataEncoder IWICFastMetadataEncoder;
typedef struct IWICMetadataBlockReader IWICMetadataBlockReader;
typedef struct IWICMetadataBlockWriter IWICMetadataBlockWriter;
typedef struct IWICMetadataHandlerInfo IWICMetadataHandlerInfo;
typedef struct IWICMetadataReaderInfo IWICMetadataReaderInfo;
typedef struct IWICMetadataWriterInfo IWICMetadataWriterInfo;
typedef struct IWICEnumMetadataItem IWICEnumMetadataItem;
typedef struct IWICPersistStream IWICPersistStream;
typedef struct IWICStreamProvider IWICStreamProvider;
typedef struct IWICPixelFormatInfo IWICPixelFormatInfo;
typedef struct IWICPixelFormatInfo2 IWICPixelFormatInfo2;
typedef struct IWICBitmapCodecInfo IWICBitmapCodecInfo;
typedef struct IWICImagingFactory IWICImagingFactory;
typedef struct IWICImagingFactory2 IWICImagingFactory2;
typedef struct IWICComponentFactory IWICComponentFactory;
typedef struct IWICDdsDecoder IWICDdsDecoder;
typedef struct IWICDdsEncoder IWICDdsEncoder;
typedef struct IWICDdsFrameDecode IWICDdsFrameDecode;
typedef struct IWICBitmapCodecProgressNotification IWICBitmapCodecProgressNotification;
typedef struct IWICBitmapSourceTransform IWICBitmapSourceTransform;
typedef struct IWICPlanarBitmapSourceTransform IWICPlanarBitmapSourceTransform;
typedef struct IWICPlanarFormatConverter IWICPlanarFormatConverter;
typedef struct IWICPlanarBitmapFrameEncode IWICPlanarBitmapFrameEncode;
typedef struct IWICJpegFrameDecode IWICJpegFrameDecode;
typedef struct IWICJpegFrameEncode IWICJpegFrameEncode;
typedef struct IWICProgressiveLevelControl IWICProgressiveLevelControl;

typedef enum WICProgressOperation {
    WICProgressOperationCopyPixels = 0x1,
    WICProgressOperationWritePixels = 0x2,
    WICProgressOperationAll = 0xffff
} WICProgressOperation;

typedef enum WICProgressNotification {
    WICProgressNotificationBegin = 0x10000,
    WICProgressNotificationEnd = 0x20000,
    WICProgressNotificationFrequent = 0x40000,
    WICProgressNotificationAll = 0xffff0000
} WICProgressNotification;

typedef enum WICSectionAccessLevel {
    WICSectionAccessLevelRead = 0x1,
    WICSectionAccessLevelReadWrite = 0x3
} WICSectionAccessLevel;

typedef HRESULT(CALLBACK *PFNProgressNotification)(void *pvData, ULONG uFrameNum, WICProgressOperation operation,
                                                  double dblProgress);

#define MIME_ISTREAM_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Read)(T * This, void *pv, ULONG cb, ULONG *pcbRead);                                    \
    HRESULT(STDMETHODCALLTYPE *Write)(T * This, const void *pv, ULONG cb, ULONG *pcbWritten);                          \
    HRESULT(STDMETHODCALLTYPE *Seek)(T * This, LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);\
    HRESULT(STDMETHODCALLTYPE *SetSize)(T * This, ULARGE_INTEGER libNewSize);                                          \
    HRESULT(STDMETHODCALLTYPE *CopyTo)(T * This, IStream * pstm, ULARGE_INTEGER cb, ULARGE_INTEGER * pcbRead,          \
                                       ULARGE_INTEGER * pcbWritten);                                                   \
    HRESULT(STDMETHODCALLTYPE *Commit)(T * This, DWORD grfCommitFlags);                                                \
    HRESULT(STDMETHODCALLTYPE *Revert)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *LockRegion)(T * This, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);   \
    HRESULT(STDMETHODCALLTYPE *UnlockRegion)(T * This, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType); \
    HRESULT(STDMETHODCALLTYPE *Stat)(T * This, STATSTG * pstatstg, DWORD grfStatFlag);                                 \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IStream * *ppstm);

#define MIME_IPERSISTSTREAM_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassID)(T * This, CLSID * pClassID);                                                \
    HRESULT(STDMETHODCALLTYPE *IsDirty)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, IStream * pStm);                                                        \
    HRESULT(STDMETHODCALLTYPE *Save)(T * This, IStream * pStm, BOOL fClearDirty);                                      \
    HRESULT(STDMETHODCALLTYPE *GetSizeMax)(T * This, ULARGE_INTEGER * pcbSize);

#define IEnumUnknown_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, ULONG celt, IUnknown * *rgelt, ULONG * pceltFetched);                   \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, ULONG celt);                                                            \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IEnumUnknown * *ppenum);
MIME_COM_INTERFACE(IEnumUnknown)

#define IPropertyBag2_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Read)(T * This, ULONG cProperties, PROPBAG2 * pPropBag, IErrorLog * pErrLog,            \
                                     VARIANT * pvarValue, HRESULT * phrError);                                         \
    HRESULT(STDMETHODCALLTYPE *Write)(T * This, ULONG cProperties, PROPBAG2 * pPropBag, VARIANT * pvarValue);          \
    HRESULT(STDMETHODCALLTYPE *CountProperties)(T * This, ULONG * pcProperties);                                       \
    HRESULT(STDMETHODCALLTYPE *GetPropertyInfo)(T * This, ULONG iProperty, ULONG cProperties, PROPBAG2 * pPropBag,     \
                                                ULONG * pcProperties);                                                 \
    HRESULT(STDMETHODCALLTYPE *LoadObject)(T * This, LPCOLESTR pstrName, DWORD dwHint, IUnknown * pUnkObject,          \
                                           IErrorLog * pErrLog);
MIME_COM_INTERFACE(IPropertyBag2)

/* ------------------------------------------------------------------ interfaces */

#define IWICColorContext_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *InitializeFromFilename)(T *This, LPCWSTR wzFilename); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromMemory)(T *This, const BYTE *pbBuffer, UINT cbBufferSize); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromExifColorSpace)(T *This, UINT value); \
    HRESULT (STDMETHODCALLTYPE *GetType)(T *This, WICColorContextType *pType); \
    HRESULT (STDMETHODCALLTYPE *GetProfileBytes)(T *This, UINT cbBuffer, BYTE *pbBuffer, UINT *pcbActual); \
    HRESULT (STDMETHODCALLTYPE *GetExifColorSpace)(T *This, UINT *pValue);
MIME_COM_INTERFACE(IWICColorContext)

#define IWICBitmapSource_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetSize)(T *This, UINT *puiWidth, UINT *puiHeight); \
    HRESULT (STDMETHODCALLTYPE *GetPixelFormat)(T *This, WICPixelFormatGUID *pPixelFormat); \
    HRESULT (STDMETHODCALLTYPE *GetResolution)(T *This, double *pDpiX, double *pDpiY); \
    HRESULT (STDMETHODCALLTYPE *CopyPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *CopyPixels)(T *This, const WICRect *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer);
MIME_COM_INTERFACE(IWICBitmapSource)

#define IWICBitmapLock_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetSize)(T *This, UINT *pWidth, UINT *pHeight); \
    HRESULT (STDMETHODCALLTYPE *GetStride)(T *This, UINT *pcbStride); \
    HRESULT (STDMETHODCALLTYPE *GetDataPointer)(T *This, UINT *pcbBufferSize, BYTE **ppbData); \
    HRESULT (STDMETHODCALLTYPE *GetPixelFormat)(T *This, WICPixelFormatGUID *pPixelFormat);
MIME_COM_INTERFACE(IWICBitmapLock)

#define IWICBitmapFlipRotator_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IWICBitmapSource *pISource, WICBitmapTransformOptions options);
MIME_COM_INTERFACE(IWICBitmapFlipRotator)

#define IWICBitmap_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Lock)(T *This, const WICRect *prcLock, DWORD flags, IWICBitmapLock **ppILock); \
    HRESULT (STDMETHODCALLTYPE *SetPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *SetResolution)(T *This, double dpiX, double dpiY);
MIME_COM_INTERFACE(IWICBitmap)

#define IWICPalette_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *InitializePredefined)(T *This, WICBitmapPaletteType ePaletteType, BOOL fAddTransparentColor); \
    HRESULT (STDMETHODCALLTYPE *InitializeCustom)(T *This, WICColor *pColors, UINT colorCount); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromBitmap)(T *This, IWICBitmapSource *pISurface, UINT colorCount, BOOL fAddTransparentColor); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *GetType)(T *This, WICBitmapPaletteType *pePaletteType); \
    HRESULT (STDMETHODCALLTYPE *GetColorCount)(T *This, UINT *pcCount); \
    HRESULT (STDMETHODCALLTYPE *GetColors)(T *This, UINT colorCount, WICColor *pColors, UINT *pcActualColors); \
    HRESULT (STDMETHODCALLTYPE *IsBlackWhite)(T *This, BOOL *pfIsBlackWhite); \
    HRESULT (STDMETHODCALLTYPE *IsGrayscale)(T *This, BOOL *pfIsGrayscale); \
    HRESULT (STDMETHODCALLTYPE *HasAlpha)(T *This, BOOL *pfHasAlpha);
MIME_COM_INTERFACE(IWICPalette)

#define IWICComponentInfo_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetComponentType)(T *This, WICComponentType *pType); \
    HRESULT (STDMETHODCALLTYPE *GetCLSID)(T *This, CLSID *pclsid); \
    HRESULT (STDMETHODCALLTYPE *GetSigningStatus)(T *This, DWORD *pStatus); \
    HRESULT (STDMETHODCALLTYPE *GetAuthor)(T *This, UINT cchAuthor, WCHAR *wzAuthor, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetVendorGUID)(T *This, GUID *pguidVendor); \
    HRESULT (STDMETHODCALLTYPE *GetVersion)(T *This, UINT cchVersion, WCHAR *wzVersion, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetSpecVersion)(T *This, UINT cchSpecVersion, WCHAR *wzSpecVersion, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetFriendlyName)(T *This, UINT cchFriendlyName, WCHAR *wzFriendlyName, UINT *pcchActual);
MIME_COM_INTERFACE(IWICComponentInfo)

#define IWICMetadataQueryReader_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormat)(T *This, GUID *pguidContainerFormat); \
    HRESULT (STDMETHODCALLTYPE *GetLocation)(T *This, UINT cchMaxLength, WCHAR *wzNamespace, UINT *pcchActualLength); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataByName)(T *This, LPCWSTR wzName, PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *GetEnumerator)(T *This, IEnumString **ppIEnumString);
MIME_COM_INTERFACE(IWICMetadataQueryReader)

#define IWICMetadataQueryWriter_METHODS(T) \
    IWICMetadataQueryReader_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *SetMetadataByName)(T *This, LPCWSTR wzName, const PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *RemoveMetadataByName)(T *This, LPCWSTR wzName);
MIME_COM_INTERFACE(IWICMetadataQueryWriter)

#define IWICBitmapFrameDecode_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetMetadataQueryReader)(T *This, IWICMetadataQueryReader **ppIMetadataQueryReader); \
    HRESULT (STDMETHODCALLTYPE *GetColorContexts)(T *This, UINT cCount, IWICColorContext **ppIColorContexts, UINT *pcActualCount); \
    HRESULT (STDMETHODCALLTYPE *GetThumbnail)(T *This, IWICBitmapSource **ppIThumbnail);
MIME_COM_INTERFACE(IWICBitmapFrameDecode)

#define IWICPixelFormatInfo_METHODS(T) \
    IWICComponentInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetFormatGUID)(T *This, GUID *pFormat); \
    HRESULT (STDMETHODCALLTYPE *GetColorContext)(T *This, IWICColorContext **ppIColorContext); \
    HRESULT (STDMETHODCALLTYPE *GetBitsPerPixel)(T *This, UINT *puiBitsPerPixel); \
    HRESULT (STDMETHODCALLTYPE *GetChannelCount)(T *This, UINT *puiChannelCount); \
    HRESULT (STDMETHODCALLTYPE *GetChannelMask)(T *This, UINT uiChannelIndex, UINT cbMaskBuffer, BYTE *pbMaskBuffer, UINT *pcbActual);
MIME_COM_INTERFACE(IWICPixelFormatInfo)

#define IWICPixelFormatInfo2_METHODS(T) \
    IWICPixelFormatInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *SupportsTransparency)(T *This, BOOL *pfSupportsTransparency); \
    HRESULT (STDMETHODCALLTYPE *GetNumericRepresentation)(T *This, WICPixelFormatNumericRepresentation *pNumericRepresentation);
MIME_COM_INTERFACE(IWICPixelFormatInfo2)

#define IWICBitmapCodecInfo_METHODS(T) \
    IWICComponentInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormat)(T *This, GUID *pguidContainerFormat); \
    HRESULT (STDMETHODCALLTYPE *GetPixelFormats)(T *This, UINT cFormats, GUID *pguidPixelFormats, UINT *pcActual); \
    HRESULT (STDMETHODCALLTYPE *GetColorManagementVersion)(T *This, UINT cchColorManagementVersion, WCHAR *wzColorManagementVersion, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetDeviceManufacturer)(T *This, UINT cchDeviceManufacturer, WCHAR *wzDeviceManufacturer, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetDeviceModels)(T *This, UINT cchDeviceModels, WCHAR *wzDeviceModels, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetMimeTypes)(T *This, UINT cchMimeTypes, WCHAR *wzMimeTypes, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetFileExtensions)(T *This, UINT cchFileExtensions, WCHAR *wzFileExtensions, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *DoesSupportAnimation)(T *This, BOOL *pfSupportAnimation); \
    HRESULT (STDMETHODCALLTYPE *DoesSupportChromaKey)(T *This, BOOL *pfSupportChromaKey); \
    HRESULT (STDMETHODCALLTYPE *DoesSupportLossless)(T *This, BOOL *pfSupportLossless); \
    HRESULT (STDMETHODCALLTYPE *DoesSupportMultiframe)(T *This, BOOL *pfSupportMultiframe); \
    HRESULT (STDMETHODCALLTYPE *MatchesMimeType)(T *This, LPCWSTR wzMimeType, BOOL *pfMatches);
MIME_COM_INTERFACE(IWICBitmapCodecInfo)

#define IWICBitmapDecoderInfo_METHODS(T) \
    IWICBitmapCodecInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetPatterns)(T *This, UINT cbSizePatterns, WICBitmapPattern *pPatterns, UINT *pcPatterns, UINT *pcbPatternsActual); \
    HRESULT (STDMETHODCALLTYPE *MatchesPattern)(T *This, IStream *pIStream, BOOL *pfMatches); \
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(T *This, IWICBitmapDecoder **ppIBitmapDecoder);
MIME_COM_INTERFACE(IWICBitmapDecoderInfo)

#define IWICBitmapDecoder_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *QueryCapability)(T *This, IStream *pIStream, DWORD *pdwCapability); \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IStream *pIStream, WICDecodeOptions cacheOptions); \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormat)(T *This, GUID *pguidContainerFormat); \
    HRESULT (STDMETHODCALLTYPE *GetDecoderInfo)(T *This, IWICBitmapDecoderInfo **ppIDecoderInfo); \
    HRESULT (STDMETHODCALLTYPE *CopyPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataQueryReader)(T *This, IWICMetadataQueryReader **ppIMetadataQueryReader); \
    HRESULT (STDMETHODCALLTYPE *GetPreview)(T *This, IWICBitmapSource **ppIBitmapSource); \
    HRESULT (STDMETHODCALLTYPE *GetColorContexts)(T *This, UINT cCount, IWICColorContext **ppIColorContexts, UINT *pcActualCount); \
    HRESULT (STDMETHODCALLTYPE *GetThumbnail)(T *This, IWICBitmapSource **ppIThumbnail); \
    HRESULT (STDMETHODCALLTYPE *GetFrameCount)(T *This, UINT *pCount); \
    HRESULT (STDMETHODCALLTYPE *GetFrame)(T *This, UINT index, IWICBitmapFrameDecode **ppIBitmapFrame);
MIME_COM_INTERFACE(IWICBitmapDecoder)

#define IWICBitmapFrameEncode_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IPropertyBag2 *pIEncoderOptions); \
    HRESULT (STDMETHODCALLTYPE *SetSize)(T *This, UINT uiWidth, UINT uiHeight); \
    HRESULT (STDMETHODCALLTYPE *SetResolution)(T *This, double dpiX, double dpiY); \
    HRESULT (STDMETHODCALLTYPE *SetPixelFormat)(T *This, WICPixelFormatGUID *pPixelFormat); \
    HRESULT (STDMETHODCALLTYPE *SetColorContexts)(T *This, UINT cCount, IWICColorContext **ppIColorContext); \
    HRESULT (STDMETHODCALLTYPE *SetPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *SetThumbnail)(T *This, IWICBitmapSource *pIThumbnail); \
    HRESULT (STDMETHODCALLTYPE *WritePixels)(T *This, UINT lineCount, UINT cbStride, UINT cbBufferSize, BYTE *pbPixels); \
    HRESULT (STDMETHODCALLTYPE *WriteSource)(T *This, IWICBitmapSource *pIBitmapSource, WICRect *prc); \
    HRESULT (STDMETHODCALLTYPE *Commit)(T *This); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataQueryWriter)(T *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter);
MIME_COM_INTERFACE(IWICBitmapFrameEncode)

#define IWICBitmapEncoderInfo_METHODS(T) \
    IWICBitmapCodecInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(T *This, IWICBitmapEncoder **ppIBitmapEncoder);
MIME_COM_INTERFACE(IWICBitmapEncoderInfo)

#define IWICBitmapEncoder_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IStream *pIStream, WICBitmapEncoderCacheOption cacheOption); \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormat)(T *This, GUID *pguidContainerFormat); \
    HRESULT (STDMETHODCALLTYPE *GetEncoderInfo)(T *This, IWICBitmapEncoderInfo **ppIEncoderInfo); \
    HRESULT (STDMETHODCALLTYPE *SetColorContexts)(T *This, UINT cCount, IWICColorContext **ppIColorContext); \
    HRESULT (STDMETHODCALLTYPE *SetPalette)(T *This, IWICPalette *pIPalette); \
    HRESULT (STDMETHODCALLTYPE *SetThumbnail)(T *This, IWICBitmapSource *pIThumbnail); \
    HRESULT (STDMETHODCALLTYPE *SetPreview)(T *This, IWICBitmapSource *pIPreview); \
    HRESULT (STDMETHODCALLTYPE *CreateNewFrame)(T *This, IWICBitmapFrameEncode **ppIFrameEncode, IPropertyBag2 **ppIEncoderOptions); \
    HRESULT (STDMETHODCALLTYPE *Commit)(T *This); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataQueryWriter)(T *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter);
MIME_COM_INTERFACE(IWICBitmapEncoder)

#define IWICFormatConverter_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IWICBitmapSource *pISource, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither, IWICPalette *pIPalette, double alphaThresholdPercent, WICBitmapPaletteType paletteTranslate); \
    HRESULT (STDMETHODCALLTYPE *CanConvert)(T *This, REFWICPixelFormatGUID srcPixelFormat, REFWICPixelFormatGUID dstPixelFormat, BOOL *pfCanConvert);
MIME_COM_INTERFACE(IWICFormatConverter)

#define IWICFormatConverterInfo_METHODS(T) \
    IWICComponentInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetPixelFormats)(T *This, UINT cFormats, WICPixelFormatGUID *pPixelFormatGUIDs, UINT *pcActual); \
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(T *This, IWICFormatConverter **ppIConverter);
MIME_COM_INTERFACE(IWICFormatConverterInfo)

#define IWICStream_METHODS(T) \
    MIME_ISTREAM_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *InitializeFromIStream)(T *This, IStream *pIStream); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromFilename)(T *This, LPCWSTR wzFileName, DWORD dwAccessMode); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromMemory)(T *This, BYTE *pbBuffer, DWORD cbBufferSize); \
    HRESULT (STDMETHODCALLTYPE *InitializeFromIStreamRegion)(T *This, IStream *pIStream, ULARGE_INTEGER ulOffset, ULARGE_INTEGER ulMaxSize);
MIME_COM_INTERFACE(IWICStream)

#define IWICBitmapScaler_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IWICBitmapSource *pISource, UINT uiWidth, UINT uiHeight, WICBitmapInterpolationMode mode);
MIME_COM_INTERFACE(IWICBitmapScaler)

#define IWICBitmapClipper_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IWICBitmapSource *pISource, const WICRect *prc);
MIME_COM_INTERFACE(IWICBitmapClipper)

#define IWICColorTransform_METHODS(T) \
    IWICBitmapSource_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *This, IWICBitmapSource *pIBitmapSource, IWICColorContext *pIContextSource, IWICColorContext *pIContextDest, REFWICPixelFormatGUID pixelFmtDest);
MIME_COM_INTERFACE(IWICColorTransform)

#define IWICFastMetadataEncoder_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Commit)(T *This); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataQueryWriter)(T *This, IWICMetadataQueryWriter **ppIMetadataQueryWriter);
MIME_COM_INTERFACE(IWICFastMetadataEncoder)

#define IWICImagingFactory_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *CreateDecoderFromFilename)(T *This, LPCWSTR wzFilename, const GUID *pguidVendor, DWORD dwDesiredAccess, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder); \
    HRESULT (STDMETHODCALLTYPE *CreateDecoderFromStream)(T *This, IStream *pIStream, const GUID *pguidVendor, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder); \
    HRESULT (STDMETHODCALLTYPE *CreateDecoderFromFileHandle)(T *This, ULONG_PTR hFile, const GUID *pguidVendor, WICDecodeOptions metadataOptions, IWICBitmapDecoder **ppIDecoder); \
    HRESULT (STDMETHODCALLTYPE *CreateComponentInfo)(T *This, REFCLSID clsidComponent, IWICComponentInfo **ppIInfo); \
    HRESULT (STDMETHODCALLTYPE *CreateDecoder)(T *This, REFGUID guidContainerFormat, const GUID *pguidVendor, IWICBitmapDecoder **ppIDecoder); \
    HRESULT (STDMETHODCALLTYPE *CreateEncoder)(T *This, REFGUID guidContainerFormat, const GUID *pguidVendor, IWICBitmapEncoder **ppIEncoder); \
    HRESULT (STDMETHODCALLTYPE *CreatePalette)(T *This, IWICPalette **ppIPalette); \
    HRESULT (STDMETHODCALLTYPE *CreateFormatConverter)(T *This, IWICFormatConverter **ppIFormatConverter); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapScaler)(T *This, IWICBitmapScaler **ppIBitmapScaler); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapClipper)(T *This, IWICBitmapClipper **ppIBitmapClipper); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFlipRotator)(T *This, IWICBitmapFlipRotator **ppIBitmapFlipRotator); \
    HRESULT (STDMETHODCALLTYPE *CreateStream)(T *This, IWICStream **ppIWICStream); \
    HRESULT (STDMETHODCALLTYPE *CreateColorContext)(T *This, IWICColorContext **ppIWICColorContext); \
    HRESULT (STDMETHODCALLTYPE *CreateColorTransformer)(T *This, IWICColorTransform **ppIWICColorTransform); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmap)(T *This, UINT uiWidth, UINT uiHeight, REFWICPixelFormatGUID pixelFormat, WICBitmapCreateCacheOption option, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromSource)(T *This, IWICBitmapSource *piBitmapSource, WICBitmapCreateCacheOption option, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromSourceRect)(T *This, IWICBitmapSource *piBitmapSource, UINT x, UINT y, UINT width, UINT height, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromMemory)(T *This, UINT uiWidth, UINT uiHeight, REFWICPixelFormatGUID pixelFormat, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromHBITMAP)(T *This, HBITMAP hBitmap, HPALETTE hPalette, WICBitmapAlphaChannelOption options, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateBitmapFromHICON)(T *This, HICON hIcon, IWICBitmap **ppIBitmap); \
    HRESULT (STDMETHODCALLTYPE *CreateComponentEnumerator)(T *This, DWORD componentTypes, DWORD options, IEnumUnknown **ppIEnumUnknown); \
    HRESULT (STDMETHODCALLTYPE *CreateFastMetadataEncoderFromDecoder)(T *This, IWICBitmapDecoder *pIDecoder, IWICFastMetadataEncoder **ppIFastEncoder); \
    HRESULT (STDMETHODCALLTYPE *CreateFastMetadataEncoderFromFrameDecode)(T *This, IWICBitmapFrameDecode *pIFrameDecoder, IWICFastMetadataEncoder **ppIFastEncoder); \
    HRESULT (STDMETHODCALLTYPE *CreateQueryWriter)(T *This, REFGUID guidMetadataFormat, const GUID *pguidVendor, IWICMetadataQueryWriter **ppIQueryWriter); \
    HRESULT (STDMETHODCALLTYPE *CreateQueryWriterFromReader)(T *This, IWICMetadataQueryReader *pIQueryReader, const GUID *pguidVendor, IWICMetadataQueryWriter **ppIQueryWriter);
MIME_COM_INTERFACE(IWICImagingFactory)

#define IWICImageEncoder_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *WriteFrame)(T * This, ID2D1Image * pImage, IWICBitmapFrameEncode * pFrameEncode,        \
                                           const WICImageParameters *pImageParameters);                                \
    HRESULT(STDMETHODCALLTYPE *WriteFrameThumbnail)(T * This, ID2D1Image * pImage,                                     \
                                                    IWICBitmapFrameEncode * pFrameEncode,                              \
                                                    const WICImageParameters *pImageParameters);                       \
    HRESULT(STDMETHODCALLTYPE *WriteThumbnail)(T * This, ID2D1Image * pImage, IWICBitmapEncoder * pEncoder,            \
                                               const WICImageParameters *pImageParameters);
MIME_COM_INTERFACE(IWICImageEncoder)

#define IWICImagingFactory2_METHODS(T) \
    IWICImagingFactory_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *CreateImageEncoder)(T *This, ID2D1Device *device, IWICImageEncoder **encoder);
MIME_COM_INTERFACE(IWICImagingFactory2)

#define IWICEnumMetadataItem_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *Next)(T *This, ULONG celt, PROPVARIANT *rgeltSchema, PROPVARIANT *rgeltId, PROPVARIANT *rgeltValue, ULONG *pceltFetched); \
    HRESULT (STDMETHODCALLTYPE *Skip)(T *This, ULONG celt); \
    HRESULT (STDMETHODCALLTYPE *Reset)(T *This); \
    HRESULT (STDMETHODCALLTYPE *Clone)(T *This, IWICEnumMetadataItem **ppIEnumMetadataItem);
MIME_COM_INTERFACE(IWICEnumMetadataItem)

#define IWICDdsDecoder_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetParameters)(T *This, WICDdsParameters *parameters); \
    HRESULT (STDMETHODCALLTYPE *GetFrame)(T *This, UINT arrayIndex, UINT mipLevel, UINT sliceIndex, IWICBitmapFrameDecode **bitmapFrame);
MIME_COM_INTERFACE(IWICDdsDecoder)

#define IWICDdsEncoder_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *SetParameters)(T *This, WICDdsParameters *parameters); \
    HRESULT (STDMETHODCALLTYPE *GetParameters)(T *This, WICDdsParameters *parameters); \
    HRESULT (STDMETHODCALLTYPE *CreateNewFrame)(T *This, IWICBitmapFrameEncode **frameEncode, UINT *arrayIndex, UINT *mipLevel, UINT *sliceIndex);
MIME_COM_INTERFACE(IWICDdsEncoder)

#define IWICDdsFrameDecode_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetSizeInBlocks)(T *This, UINT *widthInBlocks, UINT *heightInBlocks); \
    HRESULT (STDMETHODCALLTYPE *GetFormatInfo)(T *This, WICDdsFormatInfo *formatInfo); \
    HRESULT (STDMETHODCALLTYPE *CopyBlocks)(T *This, const WICRect *boundsInBlocks, UINT stride, UINT bufferSize, BYTE *buffer);
MIME_COM_INTERFACE(IWICDdsFrameDecode)

#define IWICMetadataHandlerInfo_METHODS(T) \
    IWICComponentInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetMetadataFormat)(T *This, GUID *pguidMetadataFormat); \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormats)(T *This, UINT cContainerFormats, GUID *pguidContainerFormats, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetDeviceManufacturer)(T *This, UINT cchDeviceManufacturer, WCHAR *wzDeviceManufacturer, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *GetDeviceModels)(T *This, UINT cchDeviceModels, WCHAR *wzDeviceModels, UINT *pcchActual); \
    HRESULT (STDMETHODCALLTYPE *DoesRequireFullStream)(T *This, BOOL *pfRequiresFullStream); \
    HRESULT (STDMETHODCALLTYPE *DoesSupportPadding)(T *This, BOOL *pfSupportsPadding); \
    HRESULT (STDMETHODCALLTYPE *DoesRequireFixedSize)(T *This, BOOL *pfFixedSize);
MIME_COM_INTERFACE(IWICMetadataHandlerInfo)

#define IWICMetadataReader_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetMetadataFormat)(T *This, GUID *pguidMetadataFormat); \
    HRESULT (STDMETHODCALLTYPE *GetMetadataHandlerInfo)(T *This, IWICMetadataHandlerInfo **ppIHandler); \
    HRESULT (STDMETHODCALLTYPE *GetCount)(T *This, UINT *pcCount); \
    HRESULT (STDMETHODCALLTYPE *GetValueByIndex)(T *This, UINT nIndex, PROPVARIANT *pvarSchema, PROPVARIANT *pvarId, PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *GetValue)(T *This, const PROPVARIANT *pvarSchema, const PROPVARIANT *pvarId, PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *GetEnumerator)(T *This, IWICEnumMetadataItem **ppIEnumMetadata);
MIME_COM_INTERFACE(IWICMetadataReader)

#define IWICMetadataReaderInfo_METHODS(T) \
    IWICMetadataHandlerInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetPatterns)(T *This, REFGUID guidContainerFormat, UINT cbSize, WICMetadataPattern *pPattern, UINT *pcCount, UINT *pcbActual); \
    HRESULT (STDMETHODCALLTYPE *MatchesPattern)(T *This, REFGUID guidContainerFormat, IStream *pIStream, BOOL *pfMatches); \
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(T *This, IWICMetadataReader **ppIReader);
MIME_COM_INTERFACE(IWICMetadataReaderInfo)

#define IWICMetadataWriter_METHODS(T) \
    IWICMetadataReader_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *SetValue)(T *This, const PROPVARIANT *pvarSchema, const PROPVARIANT *pvarId, const PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *SetValueByIndex)(T *This, UINT nIndex, const PROPVARIANT *pvarSchema, const PROPVARIANT *pvarId, const PROPVARIANT *pvarValue); \
    HRESULT (STDMETHODCALLTYPE *RemoveValue)(T *This, const PROPVARIANT *pvarSchema, const PROPVARIANT *pvarId); \
    HRESULT (STDMETHODCALLTYPE *RemoveValueByIndex)(T *This, UINT nIndex);
MIME_COM_INTERFACE(IWICMetadataWriter)

#define IWICMetadataWriterInfo_METHODS(T) \
    IWICMetadataHandlerInfo_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetHeader)(T *This, REFGUID guidContainerFormat, UINT cbSize, WICMetadataHeader *pHeader, UINT *pcbActual); \
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(T *This, IWICMetadataWriter **ppIWriter);
MIME_COM_INTERFACE(IWICMetadataWriterInfo)

#define IWICMetadataBlockReader_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetContainerFormat)(T *This, GUID *pguidContainerFormat); \
    HRESULT (STDMETHODCALLTYPE *GetCount)(T *This, UINT *pcCount); \
    HRESULT (STDMETHODCALLTYPE *GetReaderByIndex)(T *This, UINT nIndex, IWICMetadataReader **ppIMetadataReader); \
    HRESULT (STDMETHODCALLTYPE *GetEnumerator)(T *This, IEnumUnknown **ppIEnumMetadata);
MIME_COM_INTERFACE(IWICMetadataBlockReader)

#define IWICMetadataBlockWriter_METHODS(T) \
    IWICMetadataBlockReader_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *InitializeFromBlockReader)(T *This, IWICMetadataBlockReader *pIMDBlockReader); \
    HRESULT (STDMETHODCALLTYPE *GetWriterByIndex)(T *This, UINT nIndex, IWICMetadataWriter **ppIMetadataWriter); \
    HRESULT (STDMETHODCALLTYPE *AddWriter)(T *This, IWICMetadataWriter *pIMetadataWriter); \
    HRESULT (STDMETHODCALLTYPE *SetWriterByIndex)(T *This, UINT nIndex, IWICMetadataWriter *pIMetadataWriter); \
    HRESULT (STDMETHODCALLTYPE *RemoveWriterByIndex)(T *This, UINT nIndex);
MIME_COM_INTERFACE(IWICMetadataBlockWriter)

#define IWICPersistStream_METHODS(T) \
    MIME_IPERSISTSTREAM_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *LoadEx)(T *This, IStream *pIStream, const GUID *pguidPreferredVendor, DWORD dwPersistOptions); \
    HRESULT (STDMETHODCALLTYPE *SaveEx)(T *This, IStream *pIStream, DWORD dwPersistOptions, BOOL fClearDirty);
MIME_COM_INTERFACE(IWICPersistStream)

#define IWICStreamProvider_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *GetStream)(T *This, IStream **stream); \
    HRESULT (STDMETHODCALLTYPE *GetPersistOptions)(T *This, DWORD *options); \
    HRESULT (STDMETHODCALLTYPE *GetPreferredVendorGUID)(T *This, GUID *guid); \
    HRESULT (STDMETHODCALLTYPE *RefreshStream)(T *This);
MIME_COM_INTERFACE(IWICStreamProvider)

#define IWICComponentFactory_METHODS(T) \
    IWICImagingFactory_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *CreateMetadataReader)(T *This, REFGUID guidMetadataFormat, const GUID *pguidVendor, DWORD dwOptions, IStream *pIStream, IWICMetadataReader **ppIReader); \
    HRESULT (STDMETHODCALLTYPE *CreateMetadataReaderFromContainer)(T *This, REFGUID guidMetadataFormat, const GUID *pguidVendor, DWORD dwOptions, IStream *pIStream, IWICMetadataReader **ppIReader); \
    HRESULT (STDMETHODCALLTYPE *CreateMetadataWriter)(T *This, REFGUID guidMetadataFormat, const GUID *pguidVendor, DWORD dwMetadataOptions, IWICMetadataWriter **ppIWriter); \
    HRESULT (STDMETHODCALLTYPE *CreateMetadataWriterFromReader)(T *This, IWICMetadataReader *pIReader, const GUID *pguidVendor, IWICMetadataWriter **ppIWriter); \
    HRESULT (STDMETHODCALLTYPE *CreateQueryReaderFromBlockReader)(T *This, IWICMetadataBlockReader *pIBlockReader, IWICMetadataQueryReader **ppIQueryReader); \
    HRESULT (STDMETHODCALLTYPE *CreateQueryWriterFromBlockWriter)(T *This, IWICMetadataBlockWriter *pIBlockWriter, IWICMetadataQueryWriter **ppIQueryWriter); \
    HRESULT (STDMETHODCALLTYPE *CreateEncoderPropertyBag)(T *This, PROPBAG2 *ppropOptions, UINT cCount, IPropertyBag2 **ppIPropertyBag);
MIME_COM_INTERFACE(IWICComponentFactory)

#define IWICBitmapCodecProgressNotification_METHODS(T)                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterProgressNotification)(T * This, PFNProgressNotification pfnProgressNotification, \
                                                             void *pvData, DWORD dwProgressFlags);
MIME_COM_INTERFACE(IWICBitmapCodecProgressNotification)

#define IWICBitmapSourceTransform_METHODS(T)                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CopyPixels)(T * This, const WICRect *prc, UINT uiWidth, UINT uiHeight,                  \
                                           WICPixelFormatGUID *pguidDstFormat, WICBitmapTransformOptions dstTransform, \
                                           UINT nStride, UINT cbBufferSize, BYTE *pbBuffer);                           \
    HRESULT(STDMETHODCALLTYPE *GetClosestSize)(T * This, UINT *puiWidth, UINT *puiHeight);                             \
    HRESULT(STDMETHODCALLTYPE *GetClosestPixelFormat)(T * This, WICPixelFormatGUID *pguidDstFormat);                   \
    HRESULT(STDMETHODCALLTYPE *DoesSupportTransform)(T * This, WICBitmapTransformOptions dstTransform, BOOL *pfIsSupported);
MIME_COM_INTERFACE(IWICBitmapSourceTransform)

#define IWICPlanarBitmapSourceTransform_METHODS(T)                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *DoesSupportTransform)(                                                                  \
        T * This, UINT *puiWidth, UINT *puiHeight, WICBitmapTransformOptions dstTransform,                             \
        WICPlanarOptions dstPlanarOptions, const WICPixelFormatGUID *pguidDstFormats,                                  \
        WICBitmapPlaneDescription *pPlaneDescriptions, UINT cPlanes, BOOL *pfIsSupported);                             \
    HRESULT(STDMETHODCALLTYPE *CopyPixels)(T * This, const WICRect *prcSource, UINT uiWidth, UINT uiHeight,            \
                                           WICBitmapTransformOptions dstTransform, WICPlanarOptions dstPlanarOptions,  \
                                           const WICBitmapPlane *pDstPlanes, UINT cPlanes);
MIME_COM_INTERFACE(IWICPlanarBitmapSourceTransform)

#define IWICPlanarFormatConverter_METHODS(T)                                                                           \
    IWICBitmapSource_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, IWICBitmapSource * *ppPlanes, UINT cPlanes,                       \
                                           REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither,                \
                                           IWICPalette *pIPalette, double alphaThresholdPercent,                       \
                                           WICBitmapPaletteType paletteTranslate);                                     \
    HRESULT(STDMETHODCALLTYPE *CanConvert)(T * This, const WICPixelFormatGUID *pSrcPixelFormats, UINT cSrcPlanes,      \
                                           REFWICPixelFormatGUID dstPixelFormat, BOOL *pfCanConvert);
MIME_COM_INTERFACE(IWICPlanarFormatConverter)

#define IWICPlanarBitmapFrameEncode_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *WritePixels)(T * This, UINT lineCount, WICBitmapPlane *pPlanes, UINT cPlanes);          \
    HRESULT(STDMETHODCALLTYPE *WriteSource)(T * This, IWICBitmapSource * *ppPlanes, UINT cPlanes, WICRect *prcSource);
MIME_COM_INTERFACE(IWICPlanarBitmapFrameEncode)

typedef struct WICJpegAcHuffmanTable {
    UINT CodeCounts[16];
    UINT CodeValues[162];
} WICJpegAcHuffmanTable;

typedef struct WICJpegDcHuffmanTable {
    UINT CodeCounts[12];
    UINT CodeValues[12];
} WICJpegDcHuffmanTable;

typedef struct WICJpegQuantizationTable {
    UINT Elements[64];
} WICJpegQuantizationTable;

#define IWICJpegFrameDecode_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *DoesSupportIndexing)(T * This, BOOL *pfIndexingSupported);                              \
    HRESULT(STDMETHODCALLTYPE *SetIndexing)(T * This, WICJpegIndexingOptions options, UINT horizontalIntervalSize);    \
    HRESULT(STDMETHODCALLTYPE *ClearIndexing)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetAcHuffmanTable)(T * This, UINT scanIndex, UINT tableIndex,                           \
                                                  WICJpegAcHuffmanTable *pAcHuffmanTable);                             \
    HRESULT(STDMETHODCALLTYPE *GetDcHuffmanTable)(T * This, UINT scanIndex, UINT tableIndex,                           \
                                                  WICJpegDcHuffmanTable *pDcHuffmanTable);                             \
    HRESULT(STDMETHODCALLTYPE *GetQuantizationTable)(T * This, UINT scanIndex, UINT tableIndex,                        \
                                                     WICJpegQuantizationTable *pQuantizationTable);                    \
    HRESULT(STDMETHODCALLTYPE *GetFrameHeader)(T * This, WICJpegFrameHeader *pFrameHeader);                            \
    HRESULT(STDMETHODCALLTYPE *GetScanHeader)(T * This, UINT scanIndex, WICJpegScanHeader *pScanHeader);               \
    HRESULT(STDMETHODCALLTYPE *CopyScan)(T * This, UINT scanIndex, UINT scanOffset, UINT cbScanData, BYTE *pbScanData, \
                                         UINT *pcbScanDataActual);                                                     \
    HRESULT(STDMETHODCALLTYPE *CopyMinimalStream)(T * This, UINT streamOffset, UINT cbStreamData, BYTE *pbStreamData,  \
                                                  UINT *pcbStreamDataActual);
MIME_COM_INTERFACE(IWICJpegFrameDecode)

#define IWICJpegFrameEncode_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetAcHuffmanTable)(T * This, UINT scanIndex, UINT tableIndex,                           \
                                                  WICJpegAcHuffmanTable *pAcHuffmanTable);                             \
    HRESULT(STDMETHODCALLTYPE *GetDcHuffmanTable)(T * This, UINT scanIndex, UINT tableIndex,                           \
                                                  WICJpegDcHuffmanTable *pDcHuffmanTable);                             \
    HRESULT(STDMETHODCALLTYPE *GetQuantizationTable)(T * This, UINT scanIndex, UINT tableIndex,                        \
                                                     WICJpegQuantizationTable *pQuantizationTable);                    \
    HRESULT(STDMETHODCALLTYPE *WriteScan)(T * This, UINT cbScanData, const BYTE *pbScanData);
MIME_COM_INTERFACE(IWICJpegFrameEncode)

#define IWICProgressiveLevelControl_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetLevelCount)(T * This, UINT *pcLevels);                                               \
    HRESULT(STDMETHODCALLTYPE *GetCurrentLevel)(T * This, UINT *pnLevel);                                              \
    HRESULT(STDMETHODCALLTYPE *SetCurrentLevel)(T * This, UINT nLevel);
MIME_COM_INTERFACE(IWICProgressiveLevelControl)

/* ------------------------------------------------------------------ guids */

MIME_DEFINE_GUID(GUID_WICPixelFormatDontCare, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x00);
MIME_DEFINE_GUID(GUID_WICPixelFormat1bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x01);
MIME_DEFINE_GUID(GUID_WICPixelFormat2bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x02);
MIME_DEFINE_GUID(GUID_WICPixelFormat4bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x03);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x04);
MIME_DEFINE_GUID(GUID_WICPixelFormatBlackWhite, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x05);
MIME_DEFINE_GUID(GUID_WICPixelFormat2bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x06);
MIME_DEFINE_GUID(GUID_WICPixelFormat4bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x07);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x08);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0b);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppAlpha, 0xe6cd0116, 0xeeba, 0x4161, 0xaa,0x85,0x27,0xdd,0x9f,0xb3,0xa8,0x95);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppBGR555, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x09);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppBGR565, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0a);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppBGRA5551, 0x05ec7c2b, 0xf1e6, 0x4961, 0xad,0x46,0xe1,0xcc,0x81,0x0a,0x87,0xd2);
MIME_DEFINE_GUID(GUID_WICPixelFormat24bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0c);
MIME_DEFINE_GUID(GUID_WICPixelFormat24bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0d);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0e);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0f);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppPBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x10);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppRGB, 0xd98c6b95, 0x3efe, 0x47d6, 0xbb,0x25,0xeb,0x17,0x48,0xab,0x0c,0xf1);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppRGBA, 0xf5c7ad2d, 0x6a8d, 0x43dd, 0xa7,0xa8,0xa2,0x99,0x35,0x26,0x1a,0xe9);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppPRGBA, 0x3cc4a650, 0xa527, 0x4d37, 0xa9,0x16,0x31,0x42,0xc7,0xeb,0xed,0xba);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppGrayFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x11);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x15);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bppBGR, 0xe605a384, 0xb468, 0x46ce, 0xbb,0x2e,0x36,0xf1,0x80,0xe6,0x43,0x13);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGB, 0xa1182111, 0x186d, 0x4d42, 0xbc,0x6a,0x9c,0x83,0x03,0xa8,0xdf,0xf9);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x16);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppBGRA, 0x1562ff7c, 0xd352, 0x46f9, 0x97,0x9e,0x42,0x97,0x6b,0x79,0x22,0x46);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppPRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x17);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppPBGRA, 0x8c518e8e, 0xa4ec, 0x468b, 0xae,0x70,0xc9,0xa3,0x5a,0x9c,0x55,0x30);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x13);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppBGR101010, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x14);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x12);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bppBGRFixedPoint, 0x49ca140e, 0xcab6, 0x493b, 0x9d,0xdf,0x60,0x18,0x7c,0x37,0x53,0x2a);
MIME_DEFINE_GUID(GUID_WICPixelFormat96bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x18);
MIME_DEFINE_GUID(GUID_WICPixelFormat96bppRGBFloat, 0xe3fed78f, 0xe8db, 0x4acf, 0x84,0xc1,0xe9,0x7f,0x61,0x36,0xb3,0x27);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bppRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x19);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bppPRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1a);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bppRGBFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1b);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppCMYK, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1c);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1d);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppBGRAFixedPoint, 0x356de33c, 0x54d2, 0x4a23, 0xbb,0x4,0x9b,0x7b,0xf9,0xb1,0xd4,0x2d);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x40);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1e);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x41);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGBAHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x3a);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppPRGBAHalf, 0x58ad26c2, 0xc623, 0x4d9d, 0xb3,0x20,0x38,0x7e,0x49,0xf8,0xc4,0x42);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x42);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x3b);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppRGBE, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x3d);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppGrayHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x3e);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x3f);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppRGBA1010102, 0x25238d72, 0xfcf9, 0x4522, 0xb5,0x14,0x55,0x78,0xe5,0xad,0x55,0xe0);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppRGBA1010102XR, 0x00de6b9a, 0xc101, 0x434b, 0xb5,0x02,0xd0,0x16,0x5e,0xe1,0x12,0x2c);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppR10G10B10A2, 0x604e1bb5, 0x8a3c, 0x4b65, 0xb1,0x1c,0xbc,0x0b,0x8d,0xd7,0x5b,0x7f);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bppR10G10B10A2HDR10, 0x9c215c5d, 0x1acc, 0x4f0e, 0xa4,0xbc,0x70,0xfb,0x3a,0xe8,0xfd,0x28);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bppCMYK, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x1f);
MIME_DEFINE_GUID(GUID_WICPixelFormat24bpp3Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x20);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bpp4Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x21);
MIME_DEFINE_GUID(GUID_WICPixelFormat40bpp5Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x22);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bpp6Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x23);
MIME_DEFINE_GUID(GUID_WICPixelFormat56bpp7Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x24);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bpp8Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x25);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bpp3Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x26);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bpp4Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x27);
MIME_DEFINE_GUID(GUID_WICPixelFormat80bpp5Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x28);
MIME_DEFINE_GUID(GUID_WICPixelFormat96bpp6Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x29);
MIME_DEFINE_GUID(GUID_WICPixelFormat112bpp7Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2a);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bpp8Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2b);
MIME_DEFINE_GUID(GUID_WICPixelFormat40bppCMYKAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2c);
MIME_DEFINE_GUID(GUID_WICPixelFormat80bppCMYKAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2d);
MIME_DEFINE_GUID(GUID_WICPixelFormat32bpp3ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2e);
MIME_DEFINE_GUID(GUID_WICPixelFormat40bpp4ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x2f);
MIME_DEFINE_GUID(GUID_WICPixelFormat48bpp5ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x30);
MIME_DEFINE_GUID(GUID_WICPixelFormat56bpp6ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x31);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bpp7ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x32);
MIME_DEFINE_GUID(GUID_WICPixelFormat72bpp8ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x33);
MIME_DEFINE_GUID(GUID_WICPixelFormat64bpp3ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x34);
MIME_DEFINE_GUID(GUID_WICPixelFormat80bpp4ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x35);
MIME_DEFINE_GUID(GUID_WICPixelFormat96bpp5ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x36);
MIME_DEFINE_GUID(GUID_WICPixelFormat112bpp6ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x37);
MIME_DEFINE_GUID(GUID_WICPixelFormat128bpp7ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x38);
MIME_DEFINE_GUID(GUID_WICPixelFormat144bpp8ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x39);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppY, 0x91b4db54, 0x2df9, 0x42f0, 0xb4,0x49,0x29,0x09,0xbb,0x3d,0xf8,0x8e);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppCb, 0x1339f224, 0x6bfe, 0x4c3e, 0x93,0x02,0xe4,0xf3,0xa6,0xd0,0xca,0x2a);
MIME_DEFINE_GUID(GUID_WICPixelFormat8bppCr, 0xb8145053, 0x2116, 0x49f0, 0x88,0x35,0xed,0x84,0x4b,0x20,0x5c,0x51);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppCbCr, 0xff95ba6e, 0x11e0, 0x4263, 0xbb,0x45,0x01,0x72,0x1f,0x34,0x60,0xa4);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppYQuantizedDctCoefficients, 0xa355f433, 0x48e8, 0x4a42, 0x84,0xd8,0xe2,0xaa,0x26,0xca,0x80,0xa4);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppCbQuantizedDctCoefficients, 0xd2c4ff61, 0x56a5, 0x49c2, 0x8b,0x5c,0x4c,0x19,0x25,0x96,0x48,0x37);
MIME_DEFINE_GUID(GUID_WICPixelFormat16bppCrQuantizedDctCoefficients, 0x2fe354f0, 0x1680, 0x42d8, 0x92,0x31,0xe7,0x3c,0x05,0x65,0xbf,0xc1);
MIME_DEFINE_GUID(IID_IWICColorContext, 0x3c613a02, 0x34b2, 0x44ea, 0x9a,0x7c, 0x45,0xae,0xa9,0xc6,0xfd,0x6d);
MIME_DEFINE_GUID(IID_IWICBitmapSource, 0x00000120, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICBitmapLock, 0x00000123, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICBitmapFlipRotator, 0x5009834f, 0x2d6a, 0x41ce, 0x9e,0x1b, 0x17,0xc5,0xaf,0xf7,0xa7,0x82);
MIME_DEFINE_GUID(IID_IWICBitmap, 0x00000121, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICPalette, 0x00000040, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICComponentInfo, 0x23bc3f0a, 0x698b, 0x4357, 0x88,0x6b, 0xf2,0x4d,0x50,0x67,0x13,0x34);
MIME_DEFINE_GUID(IID_IWICMetadataQueryReader, 0x30989668, 0xe1c9, 0x4597, 0xb3,0x95, 0x45,0x8e,0xed,0xb8,0x08,0xdf);
MIME_DEFINE_GUID(IID_IWICMetadataQueryWriter, 0xa721791a, 0x0def, 0x4d06, 0xbd,0x91, 0x21,0x18,0xbf,0x1d,0xb1,0x0b);
MIME_DEFINE_GUID(IID_IWICBitmapFrameDecode, 0x3b16811b, 0x6a43, 0x4ec9, 0xa8,0x13, 0x3d,0x93,0x0c,0x13,0xb9,0x40);
MIME_DEFINE_GUID(IID_IWICPixelFormatInfo, 0xe8eda601, 0x3d48, 0x431a, 0xab,0x44, 0x69,0x05,0x9b,0xe8,0x8b,0xbe);
MIME_DEFINE_GUID(IID_IWICPixelFormatInfo2, 0xa9db33a2, 0xaf5f, 0x43c7, 0xb6,0x79, 0x74,0xf5,0x98,0x4b,0x5a,0xa4);
MIME_DEFINE_GUID(IID_IWICBitmapCodecInfo, 0xe87a44c4, 0xb76e, 0x4c47, 0x8b,0x09, 0x29,0x8e,0xb1,0x2a,0x27,0x14);
MIME_DEFINE_GUID(IID_IWICBitmapDecoderInfo, 0xd8cd007f, 0xd08f, 0x4191, 0x9b,0xfc, 0x23,0x6e,0xa7,0xf0,0xe4,0xb5);
MIME_DEFINE_GUID(IID_IWICBitmapDecoder, 0x9edde9e7, 0x8dee, 0x47ea, 0x99,0xdf, 0xe6,0xfa,0xf2,0xed,0x44,0xbf);
MIME_DEFINE_GUID(IID_IWICBitmapFrameEncode, 0x00000105, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICBitmapEncoderInfo, 0x94c9b4ee, 0xa09f, 0x4f92, 0x8a,0x1e, 0x4a,0x9b,0xce,0x7e,0x76,0xfb);
MIME_DEFINE_GUID(IID_IWICBitmapEncoder, 0x00000103, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICFormatConverter, 0x00000301, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICFormatConverterInfo, 0x9f34fb65, 0x13f4, 0x4f15, 0xbc,0x57, 0x37,0x26,0xb5,0xe5,0x3d,0x9f);
MIME_DEFINE_GUID(IID_IWICStream, 0x135ff860, 0x22b7, 0x4ddf, 0xb0,0xf6, 0x21,0x8f,0x4f,0x29,0x9a,0x43);
MIME_DEFINE_GUID(IID_IWICBitmapScaler, 0x00000302, 0xa8f2, 0x4877, 0xba,0x0a, 0xfd,0x2b,0x66,0x45,0xfb,0x94);
MIME_DEFINE_GUID(IID_IWICBitmapClipper, 0xe4fbcf03, 0x223d, 0x4e81, 0x93,0x33, 0xd6,0x35,0x55,0x6d,0xd1,0xb5);
MIME_DEFINE_GUID(IID_IWICColorTransform, 0xb66f034f, 0xd0e2, 0x40ab, 0xb4,0x36, 0x6d,0xe3,0x9e,0x32,0x1a,0x94);
MIME_DEFINE_GUID(IID_IWICFastMetadataEncoder, 0xb84e2c09, 0x78c9, 0x4ac4, 0x8b,0xd3, 0x52,0x4a,0xe1,0x66,0x3a,0x2f);
MIME_DEFINE_GUID(IID_IWICImageEncoder, 0x04c75bf8, 0x3ce1, 0x473b, 0xac,0xc5, 0x3c,0xc4,0xf5,0xe9,0x49,0x99);
MIME_DEFINE_GUID(CLSID_WICImagingFactory, 0xcacaf262, 0x9370, 0x4615, 0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a);
MIME_DEFINE_GUID(CLSID_WICImagingFactory1, 0xcacaf262, 0x9370, 0x4615, 0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a);
MIME_DEFINE_GUID(CLSID_WICImagingFactory2, 0x317d06e8, 0x5f24, 0x433d, 0xbd,0xf7,0x79,0xce,0x68,0xd8,0xab,0xc2);
MIME_DEFINE_GUID(IID_IWICImagingFactory, 0xec5ec8a9, 0xc395, 0x4314, 0x9c,0x77, 0x54,0xd7,0xa9,0x35,0xff,0x70);
MIME_DEFINE_GUID(IID_IWICImagingFactory2, 0x7b816b45, 0x1996, 0x4476, 0xb1,0x32, 0xde,0x9e,0x24,0x7c,0x8a,0xf0);
MIME_DEFINE_GUID(IID_IWICEnumMetadataItem, 0xdc2bb46d, 0x3f07, 0x481e, 0x86,0x25, 0x22,0x0c,0x4a,0xed,0xbb,0x33);
MIME_DEFINE_GUID(IID_IWICDdsDecoder, 0x409cd537, 0x8532, 0x40cb, 0x97,0x74, 0xe2,0xfe,0xb2,0xdf,0x4e,0x9c);
MIME_DEFINE_GUID(IID_IWICDdsEncoder, 0x5cacdb4c, 0x407e, 0x41b3, 0xb9,0x36, 0xd0,0xf0,0x10,0xcd,0x67,0x32);
MIME_DEFINE_GUID(IID_IWICDdsFrameDecode, 0x3d4c0c61, 0x18a4, 0x41e4, 0xbd,0x80, 0x48,0x1a,0x4f,0xc9,0xf4,0x64);
MIME_DEFINE_GUID(CLSID_WICBmpDecoder, 0x6b462062, 0x7cbf, 0x400d, 0x9f,0xdb,0x81,0x3d,0xd1,0x0f,0x27,0x78);
MIME_DEFINE_GUID(CLSID_WICPngDecoder, 0x389ea17b, 0x5078, 0x4cde, 0xb6,0xef,0x25,0xc1,0x51,0x75,0xc7,0x51);
MIME_DEFINE_GUID(CLSID_WICPngDecoder1, 0x389ea17b, 0x5078, 0x4cde, 0xb6,0xef,0x25,0xc1,0x51,0x75,0xc7,0x51);
MIME_DEFINE_GUID(CLSID_WICPngDecoder2, 0xe018945b, 0xaa86, 0x4008, 0x9b,0xd4,0x67,0x77,0xa1,0xe4,0x0c,0x11);
MIME_DEFINE_GUID(CLSID_WICIcoDecoder, 0xc61bfcdf, 0x2e0f, 0x4aad, 0xa8,0xd7,0xe0,0x6b,0xaf,0xeb,0xcd,0xfe);
MIME_DEFINE_GUID(CLSID_WICJpegDecoder, 0x9456a480, 0xe88b, 0x43ea, 0x9e,0x73,0x0b,0x2d,0x9b,0x71,0xb1,0xca);
MIME_DEFINE_GUID(CLSID_WICGifDecoder, 0x381dda3c, 0x9ce9, 0x4834, 0xa2,0x3e,0x1f,0x98,0xf8,0xfc,0x52,0xbe);
MIME_DEFINE_GUID(CLSID_WICTiffDecoder, 0xb54e85d9, 0xfe23, 0x499f, 0x8b,0x88,0x6a,0xce,0xa7,0x13,0x75,0x2b);
MIME_DEFINE_GUID(CLSID_WICWmpDecoder, 0xa26cec36, 0x234c, 0x4950, 0xae,0x16,0xe3,0x4a,0xac,0xe7,0x1d,0x0d);
MIME_DEFINE_GUID(CLSID_WICDdsDecoder, 0x9053699f, 0xa341, 0x429d, 0x9e,0x90,0xee,0x43,0x7c,0xf8,0x0c,0x73);
MIME_DEFINE_GUID(CLSID_WICBmpEncoder, 0x69be8bb4, 0xd66d, 0x47c8, 0x86,0x5a,0xed,0x15,0x89,0x43,0x37,0x82);
MIME_DEFINE_GUID(CLSID_WICPngEncoder, 0x27949969, 0x876a, 0x41d7, 0x94,0x47,0x56,0x8f,0x6a,0x35,0xa4,0xdc);
MIME_DEFINE_GUID(CLSID_WICJpegEncoder, 0x1a34f5c1, 0x4a5a, 0x46dc, 0xb6,0x44,0x1f,0x45,0x67,0xe7,0xa6,0x76);
MIME_DEFINE_GUID(CLSID_WICGifEncoder, 0x114f5598, 0x0b22, 0x40a0, 0x86,0xa1,0xc8,0x3e,0xa4,0x95,0xad,0xbd);
MIME_DEFINE_GUID(CLSID_WICTiffEncoder, 0x0131be10, 0x2001, 0x4c5f, 0xa9,0xb0,0xcc,0x88,0xfa,0xb6,0x4c,0xe8);
MIME_DEFINE_GUID(CLSID_WICWmpEncoder, 0xac4ce3cb, 0xe1c1, 0x44cd, 0x82,0x15,0x5a,0x16,0x65,0x50,0x9e,0xc2);
MIME_DEFINE_GUID(CLSID_WICDdsEncoder, 0xa61dde94, 0x66ce, 0x4ac1, 0x88,0x1b,0x71,0x68,0x05,0x88,0x89,0x5e);
MIME_DEFINE_GUID(CLSID_WICAdngDecoder, 0x981d9411, 0x909e, 0x42a7, 0x8f,0x5d,0xa7,0x47,0xff,0x05,0x2e,0xdb);
MIME_DEFINE_GUID(CLSID_WICJpegQualcommPhoneEncoder, 0x68ed5c62, 0xf534, 0x4979, 0xb2,0xb3,0x68,0x6a,0x12,0xb2,0xb3,0x4c);
MIME_DEFINE_GUID(CLSID_WICHeifDecoder, 0xe9a4a80a, 0x44fe, 0x4de4, 0x89,0x71,0x71,0x50,0xb1,0x0a,0x51,0x99);
MIME_DEFINE_GUID(CLSID_WICHeifEncoder, 0x0dbecec1, 0x9eb3, 0x4860, 0x9c,0x6f,0xdd,0xbe,0x86,0x63,0x45,0x75);
MIME_DEFINE_GUID(CLSID_WICWebpDecoder, 0x7693e886, 0x51c9, 0x4070, 0x84,0x19,0x9f,0x70,0x73,0x8e,0xc8,0xfa);
MIME_DEFINE_GUID(CLSID_WICRAWDecoder, 0x41945702, 0x8302, 0x44a6, 0x94,0x45,0xac,0x98,0xe8,0xaf,0xa0,0x86);
MIME_DEFINE_GUID(CLSID_WICDefaultFormatConverter, 0x1a3f11dc, 0xb514, 0x4b17, 0x8c,0x5f,0x21,0x54,0x51,0x38,0x52,0xf1);
MIME_DEFINE_GUID(CLSID_WICFormatConverterHighColor, 0xac75d454, 0x9f37, 0x48f8, 0xb9,0x72,0x4e,0x19,0xbc,0x85,0x60,0x11);
MIME_DEFINE_GUID(CLSID_WICFormatConverterNChannel, 0xc17cabb2, 0xd4a3, 0x47d7, 0xa5,0x57,0x33,0x9b,0x2e,0xfb,0xd4,0xf1);
MIME_DEFINE_GUID(CLSID_WICFormatConverterWMPhoto, 0x9cb5172b, 0xd600, 0x46ba, 0xab,0x77,0x77,0xbb,0x7e,0x3a,0x00,0xd9);
MIME_DEFINE_GUID(CLSID_WICPlanarFormatConverter, 0x184132b8, 0x32f8, 0x4784, 0x91,0x31,0xdd,0x72,0x24,0xb2,0x34,0x38);
MIME_DEFINE_GUID(GUID_ContainerFormatBmp, 0x0af1d87e, 0xfcfe, 0x4188, 0xbd,0xeb,0xa7,0x90,0x64,0x71,0xcb,0xe3);
MIME_DEFINE_GUID(GUID_ContainerFormatPng, 0x1b7cfaf4, 0x713f, 0x473c, 0xbb,0xcd,0x61,0x37,0x42,0x5f,0xae,0xaf);
MIME_DEFINE_GUID(GUID_ContainerFormatIco, 0xa3a860c4, 0x338f, 0x4c17, 0x91,0x9a,0xfb,0xa4,0xb5,0x62,0x8f,0x21);
MIME_DEFINE_GUID(GUID_ContainerFormatJpeg, 0x19e4a5aa, 0x5662, 0x4fc5, 0xa0,0xc0,0x17,0x58,0x02,0x8e,0x10,0x57);
MIME_DEFINE_GUID(GUID_ContainerFormatTiff, 0x163bcc30, 0xe2e9, 0x4f0b, 0x96,0x1d,0xa3,0xe9,0xfd,0xb7,0x88,0xa3);
MIME_DEFINE_GUID(GUID_ContainerFormatGif, 0x1f8a5601, 0x7d4d, 0x4cbd, 0x9c,0x82,0x1b,0xc8,0xd4,0xee,0xb9,0xa5);
MIME_DEFINE_GUID(GUID_ContainerFormatWmp, 0x57a37caa, 0x367a, 0x4540, 0x91,0x6b,0xf1,0x83,0xc5,0x09,0x3a,0x4b);
MIME_DEFINE_GUID(GUID_ContainerFormatDds, 0x9967cb95, 0x2e85, 0x4ac8, 0x8c,0xa2,0x83,0xd7,0xcc,0xd4,0x25,0xc9);
MIME_DEFINE_GUID(GUID_ContainerFormatAdng, 0xf3ff6d0d, 0x38c0, 0x41c4, 0xb1,0xfe,0x1f,0x38,0x24,0xf1,0x7b,0x84);
MIME_DEFINE_GUID(GUID_ContainerFormatHeif, 0xe1e62521, 0x6787, 0x405b, 0xa3,0x39,0x50,0x07,0x15,0xb5,0x76,0x3f);
MIME_DEFINE_GUID(GUID_ContainerFormatWebp, 0xe094b0e2, 0x67f2, 0x45b3, 0xb0,0xea,0x11,0x53,0x37,0xca,0x7c,0xf3);
MIME_DEFINE_GUID(GUID_ContainerFormatRaw, 0xfe99ce60, 0xf19c, 0x433c, 0xa3,0xae,0x00,0xac,0xef,0xa9,0xca,0x21);
MIME_DEFINE_GUID(GUID_VendorMicrosoft, 0xf0e749ca, 0xedef, 0x4589, 0xa7,0x3a,0xee,0x0e,0x62,0x6a,0x2a,0x2b);
MIME_DEFINE_GUID(GUID_VendorMicrosoftBuiltIn, 0x257a30fd, 0x6b6, 0x462b, 0xae,0xa4,0x63,0xf7,0xb,0x86,0xe5,0x33);
MIME_DEFINE_GUID(CLSID_WICImagingCategories, 0xfae3d380, 0xfea4, 0x4623, 0x8c,0x75,0xc6,0xb6,0x11,0x10,0xb6,0x81);
MIME_DEFINE_GUID(CATID_WICBitmapDecoders, 0x7ed96837, 0x96f0, 0x4812, 0xb2,0x11,0xf1,0x3c,0x24,0x11,0x7e,0xd3);
MIME_DEFINE_GUID(CATID_WICBitmapEncoders, 0xac757296, 0x3522, 0x4e11, 0x98,0x62,0xc1,0x7b,0xe5,0xa1,0x76,0x7e);
MIME_DEFINE_GUID(CATID_WICFormatConverters, 0x7835eae8, 0xbf14, 0x49d1, 0x93,0xce,0x53,0x3a,0x40,0x7b,0x22,0x48);
MIME_DEFINE_GUID(CATID_WICMetadataReader, 0x05af94d8, 0x7174, 0x4cd2, 0xbe,0x4a,0x41,0x24,0xb8,0x0e,0xe4,0xb8);
MIME_DEFINE_GUID(CATID_WICMetadataWriter, 0xabe3b9a4, 0x257d, 0x4b97, 0xbd,0x1a,0x29,0x4a,0xf4,0x96,0x22,0x2e);
MIME_DEFINE_GUID(CATID_WICPixelFormats, 0x2b46e70f, 0xcda7, 0x473e, 0x89,0xf6,0xdc,0x96,0x30,0xa2,0x39,0x0b);
MIME_DEFINE_GUID(GUID_MetadataFormatUnknown, 0xa45e592f, 0x9078, 0x4a7c, 0xad,0xb5,0x4e,0xdc,0x4f,0xd6,0x1b,0x1f);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkcHRM, 0x9db3655b, 0x2842, 0x44b3, 0x80,0x67,0x12,0xe9,0xb3,0x75,0x55,0x6a);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkgAMA, 0xf00935a5, 0x1d5d, 0x4cd1, 0x81,0xb2,0x93,0x24,0xd7,0xec,0xa7,0x81);
MIME_DEFINE_GUID(GUID_MetadataFormatChunktEXt, 0x568d8936, 0xc0a9, 0x4923, 0x90,0x5d,0xdf,0x2b,0x38,0x23,0x8f,0xbc);
MIME_DEFINE_GUID(GUID_MetadataFormatChunktIME, 0x6b00ae2d, 0xe24b, 0x460a, 0x98,0xb6,0x87,0x8b,0xd0,0x30,0x72,0xfd);
MIME_DEFINE_GUID(GUID_MetadataFormatIfd, 0x537396c6, 0x2d8a, 0x4bb6, 0x9b,0xf8,0x2f,0x0a,0x8e,0x2a,0x3a,0xdf);
MIME_DEFINE_GUID(GUID_MetadataFormatExif, 0x1c3c4f9d, 0xb84a, 0x467d, 0x94,0x93,0x36,0xcf,0xbd,0x59,0xea,0x57);
MIME_DEFINE_GUID(GUID_MetadataFormatXMP, 0xbb5acc38, 0xf216, 0x4cec, 0xa6,0xc5,0x5f,0x6e,0x73,0x97,0x63,0xa9);
MIME_DEFINE_GUID(GUID_MetadataFormatXMPStruct, 0x22383cf1, 0xed17, 0x4e2e, 0xaf,0x17,0xd8,0x5b,0x8f,0x6b,0x30,0xd0);
MIME_DEFINE_GUID(GUID_MetadataFormatIMD, 0xbd2bb086, 0x4d52, 0x48dd, 0x96,0x77,0xdb,0x48,0x3e,0x85,0xae,0x8f);
MIME_DEFINE_GUID(GUID_MetadataFormatLSD, 0xe256031e, 0x6299, 0x4929, 0xb9,0x8d,0x5a,0xc8,0x84,0xaf,0xba,0x92);
MIME_DEFINE_GUID(GUID_MetadataFormatGCE, 0x2a25cad8, 0xdeeb, 0x4c69, 0xa7,0x88,0x0e,0xc2,0x26,0x6d,0xca,0xfd);
MIME_DEFINE_GUID(GUID_MetadataFormatAPE, 0x2e043dc2, 0xc967, 0x4e05, 0x87,0x5e,0x61,0x8b,0xf6,0x7e,0x85,0xc3);
MIME_DEFINE_GUID(GUID_MetadataFormatGifComment, 0xc4b6e0e0, 0xcfb4, 0x4ad3, 0xab,0x33,0x9a,0xad,0x23,0x55,0xa3,0x4a);
MIME_DEFINE_GUID(GUID_MetadataFormatSubIfd, 0x58a2e128, 0x2db9, 0x4e57, 0xbb,0x14,0x51,0x77,0x89,0x1e,0xd3,0x31);
MIME_DEFINE_GUID(GUID_MetadataFormatGps, 0x7134ab8a, 0x9351, 0x44ad, 0xaf,0x62,0x44,0x8d,0xb6,0xb5,0x02,0xec);
MIME_DEFINE_GUID(GUID_MetadataFormatInterop, 0xed686f8e, 0x681f, 0x4c8b, 0xbd,0x41,0xa8,0xad,0xdb,0xf6,0xb3,0xfc);
MIME_DEFINE_GUID(GUID_MetadataFormatApp0, 0x79007028, 0x268d, 0x45d6, 0xa3,0xc2,0x35,0x4e,0x6a,0x50,0x4b,0xc9);
MIME_DEFINE_GUID(GUID_MetadataFormatApp1, 0x8fd3dfc3, 0xf951, 0x492b, 0x81,0x7f,0x69,0xc2,0xe6,0xd9,0xa5,0xb0);
MIME_DEFINE_GUID(GUID_MetadataFormatApp13, 0x326556a2, 0xf502, 0x4354, 0x9c,0xc0,0x8e,0x3f,0x48,0xea,0xf6,0xb5);
MIME_DEFINE_GUID(GUID_MetadataFormatIPTC, 0x4fab0914, 0xe129, 0x4087, 0xa1,0xd1,0xbc,0x81,0x2d,0x45,0xa7,0xb5);
MIME_DEFINE_GUID(GUID_MetadataFormatIRB, 0x16100d66, 0x8570, 0x4bb9, 0xb9,0x2d,0xfd,0xa4,0xb2,0x3e,0xce,0x67);
MIME_DEFINE_GUID(GUID_MetadataFormat8BIMIPTC, 0x0010568c, 0x0852, 0x4e6a, 0xb1,0x91,0x5c,0x33,0xac,0x5b,0x04,0x30);
MIME_DEFINE_GUID(GUID_MetadataFormat8BIMResolutionInfo, 0x739f305d, 0x81db, 0x43cb, 0xac,0x5e,0x55,0x01,0x3e,0xf9,0xf0,0x03);
MIME_DEFINE_GUID(GUID_MetadataFormat8BIMIPTCDigest, 0x1ca32285, 0x9ccd, 0x4786, 0x8b,0xd8,0x79,0x53,0x9d,0xb6,0xa0,0x06);
MIME_DEFINE_GUID(GUID_MetadataFormatThumbnail, 0x243dcee9, 0x8703, 0x40ee, 0x8e,0xf0,0x22,0xa6,0x0,0xb8,0x5,0x8c);
MIME_DEFINE_GUID(GUID_MetadataFormatXMPBag, 0x833cca5f, 0xdcb7, 0x4516, 0x80,0x6f,0x65,0x96,0xab,0x26,0xdc,0xe4);
MIME_DEFINE_GUID(GUID_MetadataFormatXMPSeq, 0x63e8df02, 0xeb6c, 0x456c, 0xa2,0x24,0xb2,0x5e,0x79,0x4f,0xd6,0x48);
MIME_DEFINE_GUID(GUID_MetadataFormatXMPAlt, 0x7b08a675, 0x91aa, 0x481b, 0xa7,0x98,0x4d,0xa9,0x49,0x08,0x61,0x3b);
MIME_DEFINE_GUID(GUID_MetadataFormatJpegChrominance, 0xf73d0dcf, 0xcec6, 0x4f85, 0x9b,0x0e,0x1c,0x39,0x56,0xb1,0xbe,0xf7);
MIME_DEFINE_GUID(GUID_MetadataFormatJpegLuminance, 0x86908007, 0xedfc, 0x4860, 0x8d,0x4b,0x4e,0xe6,0xe8,0x3e,0x60,0x58);
MIME_DEFINE_GUID(GUID_MetadataFormatJpegComment, 0x220e5f33, 0xafd3, 0x474e, 0x9d,0x31,0x7d,0x4f,0xe7,0x30,0xf5,0x57);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkbKGD, 0xe14d3571, 0x6b47, 0x4dea, 0xb6,0xa,0x87,0xce,0xa,0x78,0xdf,0xb7);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkiTXt, 0xc2bec729, 0xb68, 0x4b77, 0xaa,0xe,0x62,0x95,0xa6,0xac,0x18,0x14);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkhIST, 0xc59a82da, 0xdb74, 0x48a4, 0xbd,0x6a,0xb6,0x9c,0x49,0x31,0xef,0x95);
MIME_DEFINE_GUID(GUID_MetadataFormatChunkiCCP, 0xeb4349ab, 0xb685, 0x450f, 0x91,0xb5,0xe8,0x2,0xe8,0x92,0x53,0x6c);
MIME_DEFINE_GUID(GUID_MetadataFormatChunksRGB, 0xc115fd36, 0xcc6f, 0x4e3f, 0x83,0x63,0x52,0x4b,0x87,0xc6,0xb0,0xd9);
MIME_DEFINE_GUID(GUID_MetadataFormatDds, 0x4a064603, 0x8c33, 0x4e60, 0x9c,0x29,0x13,0x62,0x31,0x70,0x2d,0x08);
MIME_DEFINE_GUID(GUID_MetadataFormatHeif, 0x817ef3e1, 0x1288, 0x45f4, 0xa8,0x52,0x26,0x0d,0x9e,0x7c,0xce,0x83);
MIME_DEFINE_GUID(GUID_MetadataFormatHeifHDR, 0x568b8d8a, 0x1e65, 0x438c, 0x89,0x68,0xd6,0x0e,0x10,0x12,0xbe,0xb9);
MIME_DEFINE_GUID(GUID_MetadataFormatWebpANIM, 0x6dc4fda6, 0x78e6, 0x4102, 0xae,0x35,0xbc,0xfa,0x1e,0xdc,0xc7,0x8b);
MIME_DEFINE_GUID(GUID_MetadataFormatWebpANMF, 0x43c105ee, 0xb93b, 0x4abb, 0xb0,0x03,0xa0,0x8c,0x0d,0x87,0x04,0x71);
MIME_DEFINE_GUID(CLSID_WICUnknownMetadataReader, 0x699745c2, 0x5066, 0x4b82, 0xa8,0xe3,0xd4,0x04,0x78,0xdb,0xec,0x8c);
MIME_DEFINE_GUID(CLSID_WICUnknownMetadataWriter, 0xa09cca86, 0x27ba, 0x4f39, 0x90,0x53,0x12,0x1f,0xa4,0xdc,0x08,0xfc);
MIME_DEFINE_GUID(CLSID_WICPngBkgdMetadataReader, 0x0ce7a4a6, 0x03e8, 0x4a60, 0x9d,0x15,0x28,0x2e,0xf3,0x2e,0xe7,0xda);
MIME_DEFINE_GUID(CLSID_WICPngBkgdMetadataWriter, 0x68e3f2fd, 0x31ae, 0x4441, 0xbb,0x6a,0xfd,0x70,0x47,0x52,0x5f,0x90);
MIME_DEFINE_GUID(CLSID_WICPngChrmMetadataReader, 0xf90b5f36, 0x367b, 0x402a, 0x9d,0xd1,0xbc,0x0f,0xd5,0x9d,0x8f,0x62);
MIME_DEFINE_GUID(CLSID_WICPngChrmMetadataWriter, 0xe23ce3eb, 0x5608, 0x4e83, 0xbc,0xef,0x27,0xb1,0x98,0x7e,0x51,0xd7);
MIME_DEFINE_GUID(CLSID_WICPngGamaMetadataReader, 0x3692ca39, 0xe082, 0x4350, 0x9e,0x1f,0x37,0x04,0xcb,0x08,0x3c,0xd5);
MIME_DEFINE_GUID(CLSID_WICPngGamaMetadataWriter, 0xff036d13, 0x5d4b, 0x46dd, 0xb1,0x0f,0x10,0x66,0x93,0xd9,0xfe,0x4f);
MIME_DEFINE_GUID(CLSID_WICPngHistMetadataReader, 0x877a0bb7, 0xa313, 0x4491, 0x87,0xb5,0x2e,0x6d,0x05,0x94,0xf5,0x20);
MIME_DEFINE_GUID(CLSID_WICPngHistMetadataWriter, 0x8a03e749, 0x672e, 0x446e, 0xbf,0x1f,0x2c,0x11,0xd2,0x33,0xb6,0xff);
MIME_DEFINE_GUID(CLSID_WICPngIccpMetadataReader, 0xf5d3e63b, 0xcb0f, 0x4628, 0xa4,0x78,0x6d,0x82,0x44,0xbe,0x36,0xb1);
MIME_DEFINE_GUID(CLSID_WICPngIccpMetadataWriter, 0x16671e5f, 0x0ce6, 0x4cc4, 0x97,0x68,0xe8,0x9f,0xe5,0x01,0x8a,0xde);
MIME_DEFINE_GUID(CLSID_WICPngItxtMetadataReader, 0xaabfb2fa, 0x3e1e, 0x4a8f, 0x89,0x77,0x55,0x56,0xfb,0x94,0xea,0x23);
MIME_DEFINE_GUID(CLSID_WICPngItxtMetadataWriter, 0x31879719, 0xe751, 0x4df8, 0x98,0x1d,0x68,0xdf,0xf6,0x77,0x04,0xed);
MIME_DEFINE_GUID(CLSID_WICPngSrgbMetadataReader, 0xfb40360c, 0x547e, 0x4956, 0xa3,0xb9,0xd4,0x41,0x88,0x59,0xba,0x66);
MIME_DEFINE_GUID(CLSID_WICPngSrgbMetadataWriter, 0xa6ee35c6, 0x87ec, 0x47df, 0x9f,0x22,0x1d,0x5a,0xad,0x84,0x0c,0x82);
MIME_DEFINE_GUID(CLSID_WICPngTextMetadataReader, 0x4b59afcc, 0xb8c3, 0x408a, 0xb6,0x70,0x89,0xe5,0xfa,0xb6,0xfd,0xa7);
MIME_DEFINE_GUID(CLSID_WICPngTextMetadataWriter, 0xb5ebafb9, 0x253e, 0x4a72, 0xa7,0x44,0x07,0x62,0xd2,0x68,0x56,0x83);
MIME_DEFINE_GUID(CLSID_WICPngTimeMetadataReader, 0xd94edf02, 0xefe5, 0x4f0d, 0x85,0xc8,0xf5,0xa6,0x8b,0x30,0x00,0xb1);
MIME_DEFINE_GUID(CLSID_WICPngTimeMetadataWriter, 0x1ab78400, 0xb5a3, 0x4d91, 0x8a,0xce,0x33,0xfc,0xd1,0x49,0x9b,0xe6);
MIME_DEFINE_GUID(CLSID_WICIfdMetadataReader, 0x8f914656, 0x9d0a, 0x4eb2, 0x90,0x19,0x0b,0xf9,0x6d,0x8a,0x9e,0xe6);
MIME_DEFINE_GUID(CLSID_WICIfdMetadataWriter, 0xb1ebfc28, 0xc9bd, 0x47a2, 0x8d,0x33,0xb9,0x48,0x76,0x97,0x77,0xa7);
MIME_DEFINE_GUID(CLSID_WICExifMetadataReader, 0xd9403860, 0x297f, 0x4a49, 0xbf,0x9b,0x77,0x89,0x81,0x50,0xa4,0x42);
MIME_DEFINE_GUID(CLSID_WICExifMetadataWriter, 0xc9a14cda, 0xc339, 0x460b, 0x90,0x78,0xd4,0xde,0xbc,0xfa,0xbe,0x91);
MIME_DEFINE_GUID(CLSID_WICXMPMetadataReader, 0x72b624df, 0xae11, 0x4948, 0xa6,0x5c,0x35,0x1e,0xb0,0x82,0x94,0x19);
MIME_DEFINE_GUID(CLSID_WICXMPMetadataWriter, 0x1765e14e, 0x1bd4, 0x462e, 0xb6,0xb1,0x59,0x0b,0xf1,0x26,0x2a,0xc6);
MIME_DEFINE_GUID(CLSID_WICXMPAltMetadataReader, 0xaa94dcc2, 0xb8b0, 0x4898, 0xb8,0x35,0x00,0x0a,0xab,0xd7,0x43,0x93);
MIME_DEFINE_GUID(CLSID_WICXMPAltMetadataWriter, 0x076c2a6c, 0xf78f, 0x4c46, 0xa7,0x23,0x35,0x83,0xe7,0x08,0x76,0xea);
MIME_DEFINE_GUID(CLSID_WICXMPBagMetadataReader, 0xe7e79a30, 0x4f2c, 0x4fab, 0x8d,0x00,0x39,0x4f,0x2d,0x6b,0xbe,0xbe);
MIME_DEFINE_GUID(CLSID_WICXMPBagMetadataWriter, 0xed822c8c, 0xd6be, 0x4301, 0xa6,0x31,0x0e,0x14,0x16,0xba,0xd2,0x8f);
MIME_DEFINE_GUID(CLSID_WICXMPSeqMetadataReader, 0x7f12e753, 0xfc71, 0x43d7, 0xa5,0x1d,0x92,0xf3,0x59,0x77,0xab,0xb5);
MIME_DEFINE_GUID(CLSID_WICXMPSeqMetadataWriter, 0x6d68d1de, 0xd432, 0x4b0f, 0x92,0x3a,0x09,0x11,0x83,0xa9,0xbd,0xa7);
MIME_DEFINE_GUID(CLSID_WICXMPStructMetadataReader, 0x01b90d9a, 0x8209, 0x47f7, 0x9c,0x52,0xe1,0x24,0x4b,0xf5,0x0c,0xed);
MIME_DEFINE_GUID(CLSID_WICXMPStructMetadataWriter, 0x22c21f93, 0x7ddb, 0x411c, 0x9b,0x17,0xc5,0xb7,0xbd,0x06,0x4a,0xbc);
MIME_DEFINE_GUID(CLSID_WICLSDMetadataReader, 0x41070793, 0x59e4, 0x479a, 0xa1,0xf7,0x95,0x4a,0xdc,0x2e,0xf5,0xfc);
MIME_DEFINE_GUID(CLSID_WICLSDMetadataWriter, 0x73c037e7, 0xe5d9, 0x4954, 0x87,0x6a,0x6d,0xa8,0x1d,0x6e,0x57,0x68);
MIME_DEFINE_GUID(CLSID_WICIMDMetadataReader, 0x7447a267, 0x0015, 0x42c8, 0xa8,0xf1,0xfb,0x3b,0x94,0xc6,0x83,0x61);
MIME_DEFINE_GUID(CLSID_WICIMDMetadataWriter, 0x8c89071f, 0x452e, 0x4e95, 0x96,0x82,0x9d,0x10,0x24,0x62,0x71,0x72);
MIME_DEFINE_GUID(CLSID_WICGCEMetadataReader, 0xb92e345d, 0xf52d, 0x41f3, 0xb5,0x62,0x08,0x1b,0xc7,0x72,0xe3,0xb9);
MIME_DEFINE_GUID(CLSID_WICGCEMetadataWriter, 0xaf95dc76, 0x16b2, 0x47f4, 0xb3,0xea,0x3c,0x31,0x79,0x66,0x93,0xe7);
MIME_DEFINE_GUID(CLSID_WICAPEMetadataReader, 0x1767b93a, 0xb021, 0x44ea, 0x92,0x0f,0x86,0x3c,0x11,0xf4,0xf7,0x68);
MIME_DEFINE_GUID(CLSID_WICAPEMetadataWriter, 0xbd6edfca, 0x2890, 0x482f, 0xb2,0x33,0x8d,0x73,0x39,0xa1,0xcf,0x8d);
MIME_DEFINE_GUID(CLSID_WICGifCommentMetadataReader, 0x32557d3b, 0x69dc, 0x4f95, 0x83,0x6e,0xf5,0x97,0x2b,0x2f,0x61,0x59);
MIME_DEFINE_GUID(CLSID_WICGifCommentMetadataWriter, 0xa02797fc, 0xc4ae, 0x418c, 0xaf,0x95,0xe6,0x37,0xc7,0xea,0xd2,0xa1);
MIME_DEFINE_GUID(CLSID_WICApp0MetadataWriter, 0xf3c633a2, 0x46c8, 0x498e, 0x8f,0xbb,0xcc,0x6f,0x72,0x1b,0xbc,0xde);
MIME_DEFINE_GUID(CLSID_WICApp0MetadataReader, 0x43324b33, 0xa78f, 0x480f, 0x91,0x11,0x96,0x38,0xaa,0xcc,0xc8,0x32);
MIME_DEFINE_GUID(CLSID_WICApp1MetadataWriter, 0xee366069, 0x1832, 0x420f, 0xb3,0x81,0x04,0x79,0xad,0x06,0x6f,0x19);
MIME_DEFINE_GUID(CLSID_WICApp1MetadataReader, 0xdde33513, 0x774e, 0x4bcd, 0xae,0x79,0x02,0xf4,0xad,0xfe,0x62,0xfc);
MIME_DEFINE_GUID(CLSID_WICApp13MetadataWriter, 0x7b19a919, 0xa9d6, 0x49e5, 0xbd,0x45,0x02,0xc3,0x4e,0x4e,0x4c,0xd5);
MIME_DEFINE_GUID(CLSID_WICApp13MetadataReader, 0xaa7e3c50, 0x864c, 0x4604, 0xbc,0x04,0x8b,0x0b,0x76,0xe6,0x37,0xf6);
MIME_DEFINE_GUID(CLSID_WICSubIfdMetadataReader, 0x50d42f09, 0xecd1, 0x4b41, 0xb6,0x5d,0xda,0x1f,0xda,0xa7,0x56,0x63);
MIME_DEFINE_GUID(CLSID_WICSubIfdMetadataWriter, 0x8ade5386, 0x8e9b, 0x4f4c, 0xac,0xf2,0xf0,0x00,0x87,0x06,0xb2,0x38);
MIME_DEFINE_GUID(CLSID_WICGpsMetadataReader, 0x3697790b, 0x223b, 0x484e, 0x99,0x25,0xc4,0x86,0x92,0x18,0xf1,0x7a);
MIME_DEFINE_GUID(CLSID_WICGpsMetadataWriter, 0xcb8c13e4, 0x62b5, 0x4c96, 0xa4,0x8b,0x6b,0xa6,0xac,0xe3,0x9c,0x76);
MIME_DEFINE_GUID(CLSID_WICInteropMetadataReader, 0xb5c8b898, 0x0074, 0x459f, 0xb7,0x00,0x86,0x0d,0x46,0x51,0xea,0x14);
MIME_DEFINE_GUID(CLSID_WICInteropMetadataWriter, 0x122ec645, 0xcd7e, 0x44d8, 0xb1,0x86,0x2c,0x8c,0x20,0xc3,0xb5,0x0f);
MIME_DEFINE_GUID(CLSID_WICThumbnailMetadataReader, 0xfb012959, 0xf4f6, 0x44d7, 0x9d,0x09,0xda,0xa0,0x87,0xa9,0xdb,0x57);
MIME_DEFINE_GUID(CLSID_WICThumbnailMetadataWriter, 0xd049b20c, 0x5dd0, 0x44fe, 0xb0,0xb3,0x8f,0x92,0xc8,0xe6,0xd0,0x80);
MIME_DEFINE_GUID(CLSID_WICIPTCMetadataReader, 0x03012959, 0xf4f6, 0x44d7, 0x9d,0x09,0xda,0xa0,0x87,0xa9,0xdb,0x57);
MIME_DEFINE_GUID(CLSID_WICIPTCMetadataWriter, 0x1249b20c, 0x5dd0, 0x44fe, 0xb0,0xb3,0x8f,0x92,0xc8,0xe6,0xd0,0x80);
MIME_DEFINE_GUID(CLSID_WICIRBMetadataReader, 0xd4dcd3d7, 0xb4c2, 0x47d9, 0xa6,0xbf,0xb8,0x9b,0xa3,0x96,0xa4,0xa3);
MIME_DEFINE_GUID(CLSID_WICIRBMetadataWriter, 0x5c5c1935, 0x0235, 0x4434, 0x80,0xbc,0x25,0x1b,0xc1,0xec,0x39,0xc6);
MIME_DEFINE_GUID(CLSID_WIC8BIMIPTCMetadataReader, 0x0010668c, 0x0801, 0x4da6, 0xa4,0xa4,0x82,0x65,0x22,0xb6,0xd2,0x8f);
MIME_DEFINE_GUID(CLSID_WIC8BIMIPTCMetadataWriter, 0x00108226, 0xee41, 0x44a2, 0x9e,0x9c,0x4b,0xe4,0xd5,0xb1,0xd2,0xcd);
MIME_DEFINE_GUID(CLSID_WIC8BIMIPTCDigestMetadataReader, 0x02805f1e, 0xd5aa, 0x415b, 0x82,0xc5,0x61,0xc0,0x33,0xa9,0x88,0xa6);
MIME_DEFINE_GUID(CLSID_WIC8BIMIPTCDigestMetadataWriter, 0x2db5e62b, 0x0d67, 0x495f, 0x8f,0x9d,0xc2,0xf0,0x18,0x86,0x47,0xac);
MIME_DEFINE_GUID(CLSID_WIC8BIMResolutionInfoMetadataReader, 0x5805137a, 0xe348, 0x4f7c, 0xb3,0xcc,0x6d,0xb9,0x96,0x5a,0x05,0x99);
MIME_DEFINE_GUID(CLSID_WIC8BIMResolutionInfoMetadataWriter, 0x4ff2fe0e, 0xe74a, 0x4b71, 0x98,0xc4,0xab,0x7d,0xc1,0x67,0x07,0xba);
MIME_DEFINE_GUID(CLSID_WICJpegChrominanceMetadataReader, 0x50b1904b, 0xf28f, 0x4574, 0x93,0xf4,0x0b,0xad,0xe8,0x2c,0x69,0xe9);
MIME_DEFINE_GUID(CLSID_WICJpegChrominanceMetadataWriter, 0x3ff566f0, 0x6e6b, 0x49d4, 0x96,0xe6,0xb7,0x88,0x86,0x69,0x2c,0x62);
MIME_DEFINE_GUID(CLSID_WICJpegCommentMetadataReader, 0x9f66347c, 0x60c4, 0x4c4d, 0xab,0x58,0xd2,0x35,0x86,0x85,0xf6,0x07);
MIME_DEFINE_GUID(CLSID_WICJpegCommentMetadataWriter, 0xe573236f, 0x55b1, 0x4eda, 0x81,0xea,0x9f,0x65,0xdb,0x02,0x90,0xd3);
MIME_DEFINE_GUID(CLSID_WICJpegLuminanceMetadataReader, 0x356f2f88, 0x05a6, 0x4728, 0xb9,0xa4,0x1b,0xfb,0xce,0x04,0xd8,0x38);
MIME_DEFINE_GUID(CLSID_WICJpegLuminanceMetadataWriter, 0x1d583abc, 0x8a0e, 0x4657, 0x99,0x82,0xa3,0x80,0xca,0x58,0xfb,0x4b);
MIME_DEFINE_GUID(CLSID_WICDdsMetadataReader, 0x276c88ca, 0x7533, 0x4a86, 0xb6,0x76,0x66,0xb3,0x60,0x80,0xd4,0x84);
MIME_DEFINE_GUID(CLSID_WICDdsMetadataWriter, 0xfd688bbd, 0x31ed, 0x4db7, 0xa7,0x23,0x93,0x49,0x27,0xd3,0x83,0x67);
MIME_DEFINE_GUID(CLSID_WICHeifMetadataReader, 0xacddfc3f, 0x85ec, 0x41bc, 0xbd,0xef,0x1b,0xc2,0x62,0xe4,0xdb,0x05);
MIME_DEFINE_GUID(CLSID_WICHeifMetadataWriter, 0x3ae45e79, 0x40bc, 0x4401, 0xac,0xe5,0xdd,0x3c,0xb1,0x6e,0x6a,0xfe);
MIME_DEFINE_GUID(CLSID_WICHeifHDRMetadataReader, 0x2438de3d, 0x94d9, 0x4be8, 0x84,0xa8,0x4d,0xe9,0x5a,0x57,0x5e,0x75);
MIME_DEFINE_GUID(CLSID_WICWebpAnimMetadataReader, 0x076f9911, 0xa348, 0x465c, 0xa8,0x07,0xa2,0x52,0xf3,0xf2,0xd3,0xde);
MIME_DEFINE_GUID(CLSID_WICWebpAnmfMetadataReader, 0x85a10b03, 0xc9f6, 0x439f, 0xbe,0x5e,0xc0,0xfb,0xef,0x67,0x80,0x7c);
MIME_DEFINE_GUID(IID_IWICMetadataHandlerInfo, 0xaba958bf, 0xc672, 0x44d1, 0x8d,0x61, 0xce,0x6d,0xf2,0xe6,0x82,0xc2);
MIME_DEFINE_GUID(IID_IWICMetadataReader, 0x9204fe99, 0xd8fc, 0x4fd5, 0xa0,0x01, 0x95,0x36,0xb0,0x67,0xa8,0x99);
MIME_DEFINE_GUID(IID_IWICMetadataReaderInfo, 0xeebf1f5b, 0x07c1, 0x4447, 0xa3,0xab, 0x22,0xac,0xaf,0x78,0xa8,0x04);
MIME_DEFINE_GUID(IID_IWICMetadataWriter, 0xf7836e16, 0x3be0, 0x470b, 0x86,0xbb, 0x16,0x0d,0x0a,0xec,0xd7,0xde);
MIME_DEFINE_GUID(IID_IWICMetadataWriterInfo, 0xb22e3fba, 0x3925, 0x4323, 0xb5,0xc1, 0x9e,0xbf,0xc4,0x30,0xf2,0x36);
MIME_DEFINE_GUID(IID_IWICMetadataBlockReader, 0xfeaa2a8d, 0xb3f3, 0x43e4, 0xb2,0x5c, 0xd1,0xde,0x99,0x0a,0x1a,0xe1);
MIME_DEFINE_GUID(IID_IWICMetadataBlockWriter, 0x08fb9676, 0xb444, 0x41e8, 0x8d,0xbe, 0x6a,0x53,0xa5,0x42,0xbf,0xf1);
MIME_DEFINE_GUID(IID_IWICPersistStream, 0x00675040, 0x6908, 0x45f8, 0x86,0xa3, 0x49,0xc7,0xdf,0xd6,0xd9,0xad);
MIME_DEFINE_GUID(IID_IWICStreamProvider, 0x449494bc, 0xb468, 0x4927, 0x96,0xd7, 0xba,0x90,0xd3,0x1a,0xb5,0x05);
MIME_DEFINE_GUID(IID_IWICComponentFactory, 0x412d0c3a, 0x9650, 0x44fa, 0xaf,0x5b, 0xdd,0x2a,0x06,0xc8,0xe8,0xfb);
MIME_DEFINE_GUID(IID_IWICBitmapCodecProgressNotification, 0x64c1024e, 0xc3cf, 0x4462, 0x80, 0x78, 0x88, 0xc2, 0xb1, 0x1c, 0x46, 0xd9);
MIME_DEFINE_GUID(IID_IWICBitmapSourceTransform, 0x3b16811b, 0x6a43, 0x4ec9, 0xb7, 0x13, 0x3d, 0x5a, 0x0c, 0x13, 0xb9, 0x40);
MIME_DEFINE_GUID(IID_IWICPlanarBitmapSourceTransform, 0x3aff9cce, 0xbe95, 0x4303, 0xb9, 0x27, 0xe7, 0xd1, 0x6f, 0xf4, 0xa6, 0x13);
MIME_DEFINE_GUID(IID_IWICPlanarFormatConverter, 0xbebee9cb, 0x83b0, 0x4dcc, 0x81, 0x32, 0xb0, 0xaa, 0xa5, 0x5e, 0xac, 0x96);
MIME_DEFINE_GUID(IID_IWICPlanarBitmapFrameEncode, 0xf928b7b8, 0x2221, 0x40c1, 0xb7, 0x2e, 0x7e, 0x82, 0xf1, 0x97, 0x4d, 0x1a);
MIME_DEFINE_GUID(IID_IWICJpegFrameDecode, 0x8939f66e, 0xc46a, 0x4c21, 0xa9, 0xd1, 0x98, 0xb3, 0x27, 0xce, 0x16, 0x79);
MIME_DEFINE_GUID(IID_IWICJpegFrameEncode, 0x2f0c601f, 0xd2c6, 0x468c, 0xab, 0xfa, 0x49, 0x49, 0x5d, 0x98, 0x3e, 0xd1);
MIME_DEFINE_GUID(IID_IWICProgressiveLevelControl, 0xdaac296f, 0x7aa5, 0x4dbf, 0x8d, 0x15, 0x22, 0x5c, 0x59, 0x76, 0xf8, 0x91);
MIME_DEFINE_GUID(IID_IWICImagingFactory3, 0x489b3d8b, 0x624a, 0x4258, 0xb6, 0x78, 0x7e, 0xec, 0xe7, 0x0f, 0x29, 0x9d);
MIME_DEFINE_GUID(IID_IWICBitmapSourceTransform2, 0xc3373fdf, 0x6d39, 0x4e5f, 0x8e, 0x79, 0xbf, 0x40, 0xc0, 0xb7, 0xed, 0x77);
MIME_DEFINE_GUID(IID_IWICProgressCallback, 0x4776f9cd, 0x9517, 0x45fa, 0xbf, 0x24, 0xe8, 0x9c, 0x5e, 0xc5, 0xc6, 0x0c);
MIME_DEFINE_GUID(IID_IWICColorTransformOverride, 0x00000401, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
MIME_DEFINE_GUID(IID_IPropertyBag2, 0x22f55882, 0x280b, 0x11d0, 0xa8, 0xa9, 0x00, 0xa0, 0xc9, 0x0c, 0x20, 0x04);
MIME_DEFINE_GUID(IID_IWICGdiPlusInternal, 0x88a640d2, 0x3a21, 0x4367, 0xac, 0xfb, 0xf4, 0x48, 0xda, 0x43, 0xe6, 0xe0);

MIME_DEFINE_GUID(GUID_MetadataFormatJpegXLAnim, 0x501c2e24, 0x7a7d, 0x42b2, 0x93, 0xc7, 0xb4, 0xf4, 0x5b, 0xcc, 0x92, 0xf7);
MIME_DEFINE_GUID(GUID_MetadataFormatJpegXLAnimFrame, 0x958ecc2c, 0x36cb, 0x4af9, 0x9e, 0xa8, 0x0b, 0x74, 0xba, 0xcc, 0xfd, 0x3e);
MIME_DEFINE_GUID(GUID_MetadataFormatGainMap, 0x568d3138, 0xc446, 0x4ec2, 0xa7, 0xa8, 0x59, 0xab, 0xb1, 0x6d, 0x21, 0xe3);
MIME_DEFINE_GUID(GUID_ContainerFormatJpegXL, 0xfec14e3f, 0x427a, 0x4736, 0xaa, 0xe6, 0x27, 0xed, 0x84, 0xf6, 0x93, 0x22);
MIME_DEFINE_GUID(CLSID_WICCurDecoder, 0x22696b76, 0x881b, 0x48d7, 0x88, 0xf0, 0xdc, 0x61, 0x11, 0xff, 0x9f, 0x0b);
MIME_DEFINE_GUID(GUID_ContainerFormatCur, 0x0444f35f, 0x587c, 0x4570, 0x96, 0x46, 0x64, 0xdc, 0xd8, 0xf1, 0x75, 0x73);

/* ------------------------------------------------------------------ entry points */

#ifdef __cplusplus
extern "C" {
#endif
WINCODECAPI HRESULT WINAPI WICConvertBitmapSource(REFWICPixelFormatGUID dstFormat, IWICBitmapSource *pISrc,
                                                  IWICBitmapSource **ppIDst);
WINCODECAPI HRESULT WINAPI WICCreateBitmapFromSection(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat,
                                                      HANDLE hSection, UINT stride, UINT offset,
                                                      IWICBitmap **ppIBitmap);
WINCODECAPI HRESULT WINAPI WICCreateBitmapFromSectionEx(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat,
                                                        HANDLE hSection, UINT stride, UINT offset,
                                                        WICSectionAccessLevel desiredAccessLevel,
                                                        IWICBitmap **ppIBitmap);
WINCODECAPI HRESULT WINAPI WICMapGuidToShortName(REFGUID guid, UINT cchName, WCHAR *wzName, UINT *pcchActual);
WINCODECAPI HRESULT WINAPI WICMapShortNameToGuid(PCWSTR wzName, GUID *pguid);
WINCODECAPI HRESULT WINAPI WICMapSchemaToName(REFGUID guidMetadataFormat, LPWSTR pwzSchema, UINT cchName,
                                              WCHAR *wzName, UINT *pcchActual);
WINCODECAPI HRESULT WINAPI WICMatchMetadataContent(REFGUID guidContainerFormat, const GUID *pguidVendor,
                                                   IStream *pIStream, GUID *pguidMetadataFormat);
WINCODECAPI HRESULT WINAPI WICSerializeMetadataContent(REFGUID guidContainerFormat, IWICMetadataWriter *pIWriter,
                                                       DWORD dwPersistOptions, IStream *pIStream);
WINCODECAPI HRESULT WINAPI WICGetMetadataContentSize(REFGUID guidContainerFormat, IWICMetadataWriter *pIWriter,
                                                     ULARGE_INTEGER *pcbSize);
WINCODECAPI HRESULT WINAPI WICCreateColorContext_Proxy(IWICImagingFactory *pFactory, IWICColorContext **ppColorContext);
WINCODECAPI HRESULT WINAPI WICCreateImagingFactory_Proxy(UINT SDKVersion, IWICImagingFactory **ppIImagingFactory);
WINCODECAPI HRESULT WINAPI WICSetEncoderFormat_Proxy(IWICBitmapSource *pSourceIn, IWICPalette *pIPalette,
                                                     IWICBitmapFrameEncode *pIFrameEncode,
                                                     IWICBitmapSource **ppSourceOut);
#ifdef __cplusplus
}
#endif

#endif
