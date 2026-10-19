/*
 * direct3d 11 video: decoders and the buffers programs fill for them, video processors and their views,
 * as the interfaces of the video device and the video context present them. the interfaces and their method
 * order are windows', as programs call them through the vtables.
 */
#ifndef MIME_WIN32_D3D11VIDEO_H
#define MIME_WIN32_D3D11VIDEO_H

typedef struct ID3D11VideoDecoder ID3D11VideoDecoder;
typedef struct ID3D11VideoProcessorEnumerator ID3D11VideoProcessorEnumerator;
typedef struct ID3D11VideoProcessorEnumerator1 ID3D11VideoProcessorEnumerator1;
typedef struct ID3D11VideoProcessor ID3D11VideoProcessor;
typedef struct ID3D11AuthenticatedChannel ID3D11AuthenticatedChannel;
typedef struct ID3D11CryptoSession ID3D11CryptoSession;
typedef struct ID3D11VideoDecoderOutputView ID3D11VideoDecoderOutputView;
typedef struct ID3D11VideoProcessorInputView ID3D11VideoProcessorInputView;
typedef struct ID3D11VideoProcessorOutputView ID3D11VideoProcessorOutputView;
typedef struct ID3D11VideoContext ID3D11VideoContext;
typedef struct ID3D11VideoContext1 ID3D11VideoContext1;
typedef struct ID3D11VideoContext2 ID3D11VideoContext2;
typedef struct ID3D11VideoDevice ID3D11VideoDevice;
typedef struct ID3D11VideoDevice1 ID3D11VideoDevice1;

typedef enum D3D11_VIDEO_DECODER_BUFFER_TYPE {
    D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS = 0,
    D3D11_VIDEO_DECODER_BUFFER_MACROBLOCK_CONTROL = 1,
    D3D11_VIDEO_DECODER_BUFFER_RESIDUAL_DIFFERENCE = 2,
    D3D11_VIDEO_DECODER_BUFFER_DEBLOCKING_CONTROL = 3,
    D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX = 4,
    D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL = 5,
    D3D11_VIDEO_DECODER_BUFFER_BITSTREAM = 6,
    D3D11_VIDEO_DECODER_BUFFER_MOTION_VECTOR = 7,
    D3D11_VIDEO_DECODER_BUFFER_FILM_GRAIN = 8
} D3D11_VIDEO_DECODER_BUFFER_TYPE;
typedef enum D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE {
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE = 0,
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_BACKGROUND = 1,
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_DESTINATION = 2,
    D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_SOURCE_STREAM = 3
} D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE;
typedef enum D3D11_VIDEO_PROCESSOR_OUTPUT_RATE {
    D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL = 0,
    D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_HALF = 1,
    D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_CUSTOM = 2
} D3D11_VIDEO_PROCESSOR_OUTPUT_RATE;
typedef enum D3D11_VIDEO_PROCESSOR_STEREO_FORMAT {
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO = 0,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_HORIZONTAL = 1,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_VERTICAL = 2,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_SEPARATE = 3,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO_OFFSET = 4,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_ROW_INTERLEAVED = 5,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_COLUMN_INTERLEAVED = 6,
    D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_CHECKERBOARD = 7
} D3D11_VIDEO_PROCESSOR_STEREO_FORMAT;
typedef enum D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE {
    D3D11_VIDEO_PROCESSOR_STEREO_FLIP_NONE = 0,
    D3D11_VIDEO_PROCESSOR_STEREO_FLIP_FRAME0 = 1,
    D3D11_VIDEO_PROCESSOR_STEREO_FLIP_FRAME1 = 2
} D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE;
typedef enum D3D11_VIDEO_PROCESSOR_ROTATION {
    D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY = 0,
    D3D11_VIDEO_PROCESSOR_ROTATION_90 = 1,
    D3D11_VIDEO_PROCESSOR_ROTATION_180 = 2,
    D3D11_VIDEO_PROCESSOR_ROTATION_270 = 3
} D3D11_VIDEO_PROCESSOR_ROTATION;
typedef enum D3D11_VIDEO_PROCESSOR_DEVICE_CAPS {
    D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_LINEAR_SPACE = 0x1,
    D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_xvYCC = 0x2,
    D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_RGB_RANGE_CONVERSION = 0x4,
    D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_YCbCr_MATRIX_CONVERSION = 0x8,
    D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_NOMINAL_RANGE = 0x10
} D3D11_VIDEO_PROCESSOR_DEVICE_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_FEATURE_CAPS {
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ALPHA_FILL = 0x1,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_CONSTRICTION = 0x2,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_LUMA_KEY = 0x4,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ALPHA_PALETTE = 0x8,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_LEGACY = 0x10,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO = 0x20,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ROTATION = 0x40,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ALPHA_STREAM = 0x80,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_PIXEL_ASPECT_RATIO = 0x100,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_MIRROR = 0x200,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_SHADER_USAGE = 0x400,
    D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_METADATA_HDR10 = 0x800
} D3D11_VIDEO_PROCESSOR_FEATURE_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_FILTER_CAPS {
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_BRIGHTNESS = 0x1,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_CONTRAST = 0x2,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_HUE = 0x4,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_SATURATION = 0x8,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_NOISE_REDUCTION = 0x10,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_EDGE_ENHANCEMENT = 0x20,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_ANAMORPHIC_SCALING = 0x40,
    D3D11_VIDEO_PROCESSOR_FILTER_CAPS_STEREO_ADJUSTMENT = 0x80
} D3D11_VIDEO_PROCESSOR_FILTER_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_FORMAT_CAPS {
    D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_RGB_INTERLACED = 0x1,
    D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_RGB_PROCAMP = 0x2,
    D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_RGB_LUMA_KEY = 0x4,
    D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_PALETTE_INTERLACED = 0x8
} D3D11_VIDEO_PROCESSOR_FORMAT_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS {
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_DENOISE = 0x1,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_DERINGING = 0x2,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_EDGE_ENHANCEMENT = 0x4,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_COLOR_CORRECTION = 0x8,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_FLESH_TONE_MAPPING = 0x10,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_IMAGE_STABILIZATION = 0x20,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_SUPER_RESOLUTION = 0x40,
    D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_ANAMORPHIC_SCALING = 0x80
} D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_STEREO_CAPS {
    D3D11_VIDEO_PROCESSOR_STEREO_CAPS_MONO_OFFSET = 0x1,
    D3D11_VIDEO_PROCESSOR_STEREO_CAPS_ROW_INTERLEAVED = 0x2,
    D3D11_VIDEO_PROCESSOR_STEREO_CAPS_COLUMN_INTERLEAVED = 0x4,
    D3D11_VIDEO_PROCESSOR_STEREO_CAPS_CHECKERBOARD = 0x8,
    D3D11_VIDEO_PROCESSOR_STEREO_CAPS_FLIP_MODE = 0x10
} D3D11_VIDEO_PROCESSOR_STEREO_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS {
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND = 0x1,
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB = 0x2,
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE = 0x4,
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION = 0x8,
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_INVERSE_TELECINE = 0x10,
    D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_FRAME_RATE_CONVERSION = 0x20
} D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS {
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_32 = 0x1,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_22 = 0x2,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_2224 = 0x4,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_2332 = 0x8,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_32322 = 0x10,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_55 = 0x20,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_64 = 0x40,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_87 = 0x80,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_222222222223 = 0x100,
    D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS_OTHER = 0x80000000
} D3D11_VIDEO_PROCESSOR_ITELECINE_CAPS;
typedef enum D3D11_CONTENT_PROTECTION_CAPS {
    D3D11_CONTENT_PROTECTION_CAPS_SOFTWARE = 0x1,
    D3D11_CONTENT_PROTECTION_CAPS_HARDWARE = 0x2,
    D3D11_CONTENT_PROTECTION_CAPS_PROTECTION_ALWAYS_ON = 0x4,
    D3D11_CONTENT_PROTECTION_CAPS_PARTIAL_DECRYPTION = 0x8,
    D3D11_CONTENT_PROTECTION_CAPS_CONTENT_KEY = 0x10,
    D3D11_CONTENT_PROTECTION_CAPS_FRESHEN_SESSION_KEY = 0x20,
    D3D11_CONTENT_PROTECTION_CAPS_ENCRYPTED_READ_BACK = 0x40,
    D3D11_CONTENT_PROTECTION_CAPS_ENCRYPTED_READ_BACK_KEY = 0x80,
    D3D11_CONTENT_PROTECTION_CAPS_SEQUENTIAL_CTR_IV = 0x100,
    D3D11_CONTENT_PROTECTION_CAPS_ENCRYPT_SLICEDATA_ONLY = 0x200,
    D3D11_CONTENT_PROTECTION_CAPS_DECRYPTION_BLT = 0x400,
    D3D11_CONTENT_PROTECTION_CAPS_HARDWARE_PROTECT_UNCOMPRESSED = 0x800,
    D3D11_CONTENT_PROTECTION_CAPS_HARDWARE_PROTECTED_MEMORY_PAGEABLE = 0x1000,
    D3D11_CONTENT_PROTECTION_CAPS_HARDWARE_TEARDOWN = 0x2000,
    D3D11_CONTENT_PROTECTION_CAPS_HARDWARE_DRM_COMMUNICATION = 0x4000
} D3D11_CONTENT_PROTECTION_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE {
    D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_UNDEFINED = 0x0,
    D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235 = 0x1,
    D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255 = 0x2
} D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE;
typedef enum D3D11_AUTHENTICATED_PROCESS_IDENTIFIER_TYPE {
    D3D11_PROCESSIDTYPE_UNKNOWN = 0x0,
    D3D11_PROCESSIDTYPE_DWM = 0x1,
    D3D11_PROCESSIDTYPE_HANDLE = 0x2
} D3D11_AUTHENTICATED_PROCESS_IDENTIFIER_TYPE;
typedef enum D3D11_BUS_TYPE {
    D3D11_BUS_TYPE_OTHER = 0x0,
    D3D11_BUS_TYPE_PCI = 0x1,
    D3D11_BUS_TYPE_PCIX = 0x2,
    D3D11_BUS_TYPE_PCIEXPRESS = 0x3,
    D3D11_BUS_TYPE_AGP = 0x4,
    D3D11_BUS_IMPL_MODIFIER_INSIDE_OF_CHIPSET = 0x10000,
    D3D11_BUS_IMPL_MODIFIER_TRACKS_ON_MOTHER_BOARD_TO_CHIP = 0x20000,
    D3D11_BUS_IMPL_MODIFIER_TRACKS_ON_MOTHER_BOARD_TO_SOCKET = 0x30000,
    D3D11_BUS_IMPL_MODIFIER_DAUGHTER_BOARD_CONNECTOR = 0x40000,
    D3D11_BUS_IMPL_MODIFIER_DAUGHTER_BOARD_CONNECTOR_INSIDE_OF_NUAE = 0x50000,
    D3D11_BUS_IMPL_MODIFIER_NON_STANDARD = 0x80000000
} D3D11_BUS_TYPE;
typedef struct D3D11_VIDEO_DECODER_DESC {
    GUID Guid;
    UINT SampleWidth;
    UINT SampleHeight;
    DXGI_FORMAT OutputFormat;
} D3D11_VIDEO_DECODER_DESC;
typedef struct D3D11_VIDEO_DECODER_CONFIG {
    GUID guidConfigBitstreamEncryption;
    GUID guidConfigMBcontrolEncryption;
    GUID guidConfigResidDiffEncryption;
    UINT ConfigBitstreamRaw;
    UINT ConfigMBcontrolRasterOrder;
    UINT ConfigResidDiffHost;
    UINT ConfigSpatialResid8;
    UINT ConfigResid8Subtraction;
    UINT ConfigSpatialHost8or9Clipping;
    UINT ConfigSpatialResidInterleaved;
    UINT ConfigIntraResidUnsigned;
    UINT ConfigResidDiffAccelerator;
    UINT ConfigHostInverseScan;
    UINT ConfigSpecificIDCT;
    UINT Config4GroupedCoefs;
    USHORT ConfigMinRenderTargetBuffCount;
    USHORT ConfigDecoderSpecific;
} D3D11_VIDEO_DECODER_CONFIG;
typedef enum D3D11_VIDEO_FRAME_FORMAT {
    D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE = 0,
    D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST = 1,
    D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST = 2
} D3D11_VIDEO_FRAME_FORMAT;
typedef enum D3D11_VIDEO_USAGE {
    D3D11_VIDEO_USAGE_PLAYBACK_NORMAL = 0,
    D3D11_VIDEO_USAGE_OPTIMAL_SPEED = 1,
    D3D11_VIDEO_USAGE_OPTIMAL_QUALITY = 2
} D3D11_VIDEO_USAGE;
typedef struct D3D11_VIDEO_PROCESSOR_CONTENT_DESC {
    D3D11_VIDEO_FRAME_FORMAT InputFrameFormat;
    DXGI_RATIONAL InputFrameRate;
    UINT InputWidth;
    UINT InputHeight;
    DXGI_RATIONAL OutputFrameRate;
    UINT OutputWidth;
    UINT OutputHeight;
    D3D11_VIDEO_USAGE Usage;
} D3D11_VIDEO_PROCESSOR_CONTENT_DESC;
typedef struct D3D11_VIDEO_PROCESSOR_CAPS {
    UINT DeviceCaps;
    UINT FeatureCaps;
    UINT FilterCaps;
    UINT InputFormatCaps;
    UINT AutoStreamCaps;
    UINT StereoCaps;
    UINT RateConversionCapsCount;
    UINT MaxInputStreams;
    UINT MaxStreamStates;
} D3D11_VIDEO_PROCESSOR_CAPS;
typedef struct D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS {
    UINT PastFrames;
    UINT FutureFrames;
    UINT ProcessorCaps;
    UINT ITelecineCaps;
    UINT CustomRateCount;
} D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS;
typedef struct D3D11_VIDEO_PROCESSOR_CUSTOM_RATE {
    DXGI_RATIONAL CustomRate;
    UINT OutputFrames;
    BOOL InputInterlaced;
    UINT InputFramesOrFields;
} D3D11_VIDEO_PROCESSOR_CUSTOM_RATE;
typedef enum D3D11_VIDEO_PROCESSOR_FILTER {
    D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS = 0,
    D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST = 1,
    D3D11_VIDEO_PROCESSOR_FILTER_HUE = 2,
    D3D11_VIDEO_PROCESSOR_FILTER_SATURATION = 3,
    D3D11_VIDEO_PROCESSOR_FILTER_NOISE_REDUCTION = 4,
    D3D11_VIDEO_PROCESSOR_FILTER_EDGE_ENHANCEMENT = 5,
    D3D11_VIDEO_PROCESSOR_FILTER_ANAMORPHIC_SCALING = 6,
    D3D11_VIDEO_PROCESSOR_FILTER_STEREO_ADJUSTMENT = 7
} D3D11_VIDEO_PROCESSOR_FILTER;
typedef struct D3D11_VIDEO_PROCESSOR_FILTER_RANGE {
    int Minimum;
    int Maximum;
    int Default;
    float Multiplier;
} D3D11_VIDEO_PROCESSOR_FILTER_RANGE;
typedef enum D3D11_AUTHENTICATED_CHANNEL_TYPE {
    D3D11_AUTHENTICATED_CHANNEL_D3D11 = 1,
    D3D11_AUTHENTICATED_CHANNEL_DRIVER_SOFTWARE = 2,
    D3D11_AUTHENTICATED_CHANNEL_DRIVER_HARDWARE = 3
} D3D11_AUTHENTICATED_CHANNEL_TYPE;
typedef enum D3D11_VDOV_DIMENSION {
    D3D11_VDOV_DIMENSION_UNKNOWN = 0,
    D3D11_VDOV_DIMENSION_TEXTURE2D = 1
} D3D11_VDOV_DIMENSION;
typedef struct D3D11_TEX2D_VDOV {
    UINT ArraySlice;
} D3D11_TEX2D_VDOV;
typedef struct D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC {
    GUID DecodeProfile;
    D3D11_VDOV_DIMENSION ViewDimension;
     union {
        D3D11_TEX2D_VDOV Texture2D;
    } ;
} D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC;
typedef enum D3D11_VPIV_DIMENSION {
    D3D11_VPIV_DIMENSION_UNKNOWN = 0,
    D3D11_VPIV_DIMENSION_TEXTURE2D = 1
} D3D11_VPIV_DIMENSION;
typedef struct D3D11_TEX2D_VPIV {
    UINT MipSlice;
    UINT ArraySlice;
} D3D11_TEX2D_VPIV;
typedef struct D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC {
    UINT FourCC;
    D3D11_VPIV_DIMENSION ViewDimension;
     union {
        D3D11_TEX2D_VPIV Texture2D;
    } ;
} D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC;
typedef enum D3D11_VPOV_DIMENSION {
    D3D11_VPOV_DIMENSION_UNKNOWN = 0,
    D3D11_VPOV_DIMENSION_TEXTURE2D = 1,
    D3D11_VPOV_DIMENSION_TEXTURE2DARRAY = 2
} D3D11_VPOV_DIMENSION;
typedef struct D3D11_TEX2D_VPOV {
    UINT MipSlice;
} D3D11_TEX2D_VPOV;
typedef struct D3D11_TEX2D_ARRAY_VPOV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D11_TEX2D_ARRAY_VPOV;
typedef struct D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC {
    D3D11_VPOV_DIMENSION ViewDimension;
     union {
        D3D11_TEX2D_VPOV Texture2D;
        D3D11_TEX2D_ARRAY_VPOV Texture2DArray;
    } ;
} D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC;
typedef struct D3D11_VIDEO_CONTENT_PROTECTION_CAPS {
    UINT Caps;
    UINT KeyExchangeTypeCount;
    UINT BlockAlignmentSize;
    ULONGLONG ProtectedMemorySize;
} D3D11_VIDEO_CONTENT_PROTECTION_CAPS;
typedef struct D3D11_ENCRYPTED_BLOCK_INFO {
    UINT NumEncryptedBytesAtBeginning;
    UINT NumBytesInSkipPattern;
    UINT NumBytesInEncryptPattern;
} D3D11_ENCRYPTED_BLOCK_INFO;
typedef struct D3D11_VIDEO_DECODER_BUFFER_DESC {
    D3D11_VIDEO_DECODER_BUFFER_TYPE BufferType;
    UINT BufferIndex;
    UINT DataOffset;
    UINT DataSize;
    UINT FirstMBaddress;
    UINT NumMBsInBuffer;
    UINT Width;
    UINT Height;
    UINT Stride;
    UINT ReservedBits;
    void *pIV;
    UINT IVSize;
    BOOL PartialEncryption;
    D3D11_ENCRYPTED_BLOCK_INFO EncryptedBlockInfo;
} D3D11_VIDEO_DECODER_BUFFER_DESC;
typedef struct D3D11_VIDEO_DECODER_EXTENSION {
    UINT Function;
    void *pPrivateInputData;
    UINT PrivateInputDataSize;
    void *pPrivateOutputData;
    UINT PrivateOutputDataSize;
    UINT ResourceCount;
    ID3D11Resource **ppResourceList;
} D3D11_VIDEO_DECODER_EXTENSION;
typedef struct D3D11_VIDEO_COLOR_YCbCrA {
    float Y;
    float Cb;
    float Cr;
    float A;
} D3D11_VIDEO_COLOR_YCbCrA;
typedef struct D3D11_VIDEO_COLOR_RGBA {
    float R;
    float G;
    float B;
    float A;
} D3D11_VIDEO_COLOR_RGBA;
typedef struct D3D11_VIDEO_COLOR {
     union {
        D3D11_VIDEO_COLOR_YCbCrA YCbCr;
        D3D11_VIDEO_COLOR_RGBA RGBA;
    } ;
} D3D11_VIDEO_COLOR;
typedef struct D3D11_VIDEO_PROCESSOR_COLOR_SPACE {
    UINT Usage : 1;
    UINT RGB_Range : 1;
    UINT YCbCr_Matrix : 1;
    UINT YCbCr_xvYCC : 1;
    UINT Nominal_Range : 2;
    UINT Reserved : 26;
} D3D11_VIDEO_PROCESSOR_COLOR_SPACE;
typedef struct D3D11_VIDEO_PROCESSOR_STREAM {
    BOOL Enable;
    UINT OutputIndex;
    UINT InputFrameOrField;
    UINT PastFrames;
    UINT FutureFrames;
    ID3D11VideoProcessorInputView **ppPastSurfaces;
    ID3D11VideoProcessorInputView *pInputSurface;
    ID3D11VideoProcessorInputView **ppFutureSurfaces;
    ID3D11VideoProcessorInputView **ppPastSurfacesRight;
    ID3D11VideoProcessorInputView *pInputSurfaceRight;
    ID3D11VideoProcessorInputView **ppFutureSurfacesRight;
} D3D11_VIDEO_PROCESSOR_STREAM;
typedef enum D3D11_VIDEO_DECODER_CAPS {
    D3D11_VIDEO_DECODER_CAPS_DOWNSAMPLE = 0x1,
    D3D11_VIDEO_DECODER_CAPS_NON_REAL_TIME = 0x2,
    D3D11_VIDEO_DECODER_CAPS_DOWNSAMPLE_DYNAMIC = 0x4,
    D3D11_VIDEO_DECODER_CAPS_DOWNSAMPLE_REQUIRED = 0x8,
    D3D11_VIDEO_DECODER_CAPS_UNSUPPORTED = 0x10
} D3D11_VIDEO_DECODER_CAPS;
typedef enum D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINTS {
    D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINT_MULTIPLANE_OVERLAY_ROTATION = 0x1,
    D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINT_MULTIPLANE_OVERLAY_RESIZE = 0x2,
    D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINT_MULTIPLANE_OVERLAY_COLOR_SPACE_CONVERSION = 0x4,
    D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINT_TRIPLE_BUFFER_OUTPUT = 0x8
} D3D11_VIDEO_PROCESSOR_BEHAVIOR_HINTS;
typedef enum D3D11_CRYPTO_SESSION_STATUS {
    D3D11_CRYPTO_SESSION_STATUS_OK = 0x0,
    D3D11_CRYPTO_SESSION_STATUS_KEY_LOST = 0x1,
    D3D11_CRYPTO_SESSION_STATUS_KEY_AND_CONTENT_LOST = 0x2
} D3D11_CRYPTO_SESSION_STATUS;
typedef struct D3D11_VIDEO_DECODER_SUB_SAMPLE_MAPPING_BLOCK {
    UINT ClearSize;
    UINT EncryptedSize;
} D3D11_VIDEO_DECODER_SUB_SAMPLE_MAPPING_BLOCK;
typedef struct D3D11_VIDEO_DECODER_BUFFER_DESC1 {
    D3D11_VIDEO_DECODER_BUFFER_TYPE BufferType;
    UINT DataOffset;
    UINT DataSize;
    void *pIV;
    UINT IVSize;
    D3D11_VIDEO_DECODER_SUB_SAMPLE_MAPPING_BLOCK *pSubSampleMappingBlock;
    UINT SubSampleMappingCount;
} D3D11_VIDEO_DECODER_BUFFER_DESC1;
typedef struct D3D11_VIDEO_DECODER_BEGIN_FRAME_CRYPTO_SESSION {
    ID3D11CryptoSession *pCryptoSession;
    UINT BlobSize;
    void *pBlob;
    GUID *pKeyInfoId;
    UINT PrivateDataSize;
    void *pPrivateData;
} D3D11_VIDEO_DECODER_BEGIN_FRAME_CRYPTO_SESSION;
typedef struct D3D11_VIDEO_PROCESSOR_STREAM_BEHAVIOR_HINT {
    BOOL Enable;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
} D3D11_VIDEO_PROCESSOR_STREAM_BEHAVIOR_HINT;
typedef struct D3D11_VIDEO_SAMPLE_DESC {
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    DXGI_COLOR_SPACE_TYPE ColorSpace;
} D3D11_VIDEO_SAMPLE_DESC;
typedef enum D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT {
    D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT = 0x1,
    D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT = 0x2
} D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT;
typedef struct D3D11_OMAC {
    BYTE Omac[16];
} D3D11_OMAC;

typedef struct D3D11_AUTHENTICATED_CONFIGURE_INPUT {
    D3D11_OMAC omac;
    GUID ConfigureType;
    HANDLE hChannel;
    UINT SequenceNumber;
} D3D11_AUTHENTICATED_CONFIGURE_INPUT;
typedef struct D3D11_AUTHENTICATED_CONFIGURE_OUTPUT {
    D3D11_OMAC omac;
    GUID ConfigureType;
    HANDLE hChannel;
    UINT SequenceNumber;
    HRESULT ReturnCode;
} D3D11_AUTHENTICATED_CONFIGURE_OUTPUT;
typedef struct D3D11_AUTHENTICATED_QUERY_INPUT {
    GUID QueryType;
    HANDLE hChannel;
    UINT SequenceNumber;
} D3D11_AUTHENTICATED_QUERY_INPUT;
typedef struct D3D11_AUTHENTICATED_QUERY_OUTPUT {
    D3D11_OMAC omac;
    GUID QueryType;
    HANDLE hChannel;
    UINT SequenceNumber;
    HRESULT ReturnCode;
} D3D11_AUTHENTICATED_QUERY_OUTPUT;

#define ID3D11VideoDecoder_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetCreationParameters)(T *This, D3D11_VIDEO_DECODER_DESC *pVideoDesc, D3D11_VIDEO_DECODER_CONFIG *pConfig); \
    HRESULT(STDMETHODCALLTYPE *GetDriverHandle)(T *This, HANDLE *pDriverHandle);

#define ID3D11VideoProcessorEnumerator_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorContentDesc)(T *This, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *pContentDesc); \
    HRESULT(STDMETHODCALLTYPE *CheckVideoProcessorFormat)(T *This, DXGI_FORMAT Format, UINT *pFlags); \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorCaps)(T *This, D3D11_VIDEO_PROCESSOR_CAPS *pCaps); \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorRateConversionCaps)(T *This, UINT TypeIndex, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *pCaps); \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorCustomRate)(T *This, UINT TypeIndex, UINT CustomRateIndex, D3D11_VIDEO_PROCESSOR_CUSTOM_RATE *pRate); \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorFilterRange)(T *This, D3D11_VIDEO_PROCESSOR_FILTER Filter, D3D11_VIDEO_PROCESSOR_FILTER_RANGE *pRange);

#define ID3D11VideoProcessor_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    void(STDMETHODCALLTYPE *GetContentDesc)(T *This, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *pDesc); \
    void(STDMETHODCALLTYPE *GetRateConversionCaps)(T *This, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *pCaps);

#define ID3D11AuthenticatedChannel_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetCertificateSize)(T *This, UINT *pCertificateSize); \
    HRESULT(STDMETHODCALLTYPE *GetCertificate)(T *This, UINT CertificateSize, BYTE *pCertificate); \
    void(STDMETHODCALLTYPE *GetChannelHandle)(T *This, HANDLE *pChannelHandle);

#define ID3D11CryptoSession_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    void(STDMETHODCALLTYPE *GetCryptoType)(T *This, GUID *pCryptoType); \
    void(STDMETHODCALLTYPE *GetDecoderProfile)(T *This, GUID *pDecoderProfile); \
    HRESULT(STDMETHODCALLTYPE *GetCertificateSize)(T *This, UINT *pCertificateSize); \
    HRESULT(STDMETHODCALLTYPE *GetCertificate)(T *This, UINT CertificateSize, BYTE *pCertificate); \
    void(STDMETHODCALLTYPE *GetCryptoSessionHandle)(T *This, HANDLE *pCryptoSessionHandle);

#define ID3D11VideoDecoderOutputView_METHODS(T) \
    ID3D11View_METHODS(T) \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC *pDesc);

#define ID3D11VideoProcessorInputView_METHODS(T) \
    ID3D11View_METHODS(T) \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *pDesc);

#define ID3D11VideoProcessorOutputView_METHODS(T) \
    ID3D11View_METHODS(T) \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *pDesc);

#define ID3D11VideoContext_METHODS(T) \
    ID3D11DeviceChild_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetDecoderBuffer)(T *This, ID3D11VideoDecoder *decoder, D3D11_VIDEO_DECODER_BUFFER_TYPE type, UINT *buffer_size, void **buffer); \
    HRESULT(STDMETHODCALLTYPE *ReleaseDecoderBuffer)(T *This, ID3D11VideoDecoder *decoder, D3D11_VIDEO_DECODER_BUFFER_TYPE type); \
    HRESULT(STDMETHODCALLTYPE *DecoderBeginFrame)(T *This, ID3D11VideoDecoder *decoder, ID3D11VideoDecoderOutputView *view, UINT key_size, const void *key); \
    HRESULT(STDMETHODCALLTYPE *DecoderEndFrame)(T *This, ID3D11VideoDecoder *decoder); \
    HRESULT(STDMETHODCALLTYPE *SubmitDecoderBuffers)(T *This, ID3D11VideoDecoder *decoder, UINT buffers_count, const D3D11_VIDEO_DECODER_BUFFER_DESC *buffer_desc); \
    HRESULT(STDMETHODCALLTYPE *DecoderExtension)(T *This, ID3D11VideoDecoder *decoder, const D3D11_VIDEO_DECODER_EXTENSION *extension); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputTargetRect)(T *This, ID3D11VideoProcessor *processor, BOOL enable, const RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputBackgroundColor)(T *This, ID3D11VideoProcessor *processor, BOOL y_cb_cr, const D3D11_VIDEO_COLOR *color); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputColorSpace)(T *This, ID3D11VideoProcessor *processor, const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputAlphaFillMode)(T *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE alpha_fill_mode, UINT stream_idx); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputConstriction)(T *This, ID3D11VideoProcessor *processor, BOOL enable, SIZE size); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputStereoMode)(T *This, ID3D11VideoProcessor *processor, BOOL enable); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorSetOutputExtension)(T *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputTargetRect)(T *This, ID3D11VideoProcessor *processor, BOOL *enabled, RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputBackgroundColor)(T *This, ID3D11VideoProcessor *processor, BOOL *y_cb_cr, D3D11_VIDEO_COLOR *color); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputColorSpace)(T *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputAlphaFillMode)(T *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE *alpha_fill_mode, UINT *stream_idx); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputConstriction)(T *This, ID3D11VideoProcessor *processor, BOOL *enabled, SIZE *size); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputStereoMode)(T *This, ID3D11VideoProcessor *processor, BOOL *enabled); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorGetOutputExtension)(T *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamFrameFormat)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_FRAME_FORMAT format); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamColorSpace)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamOutputRate)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE rate, BOOL repeat, const DXGI_RATIONAL *custom_rate); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamSourceRect)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamDestRect)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamAlpha)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, float alpha); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamPalette)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, const UINT *entries); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamPixelAspectRatio)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const DXGI_RATIONAL *src_aspect_ratio, const DXGI_RATIONAL *dst_aspect_ratio); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamLumaKey)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, float lower, float upper); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamStereoFormat)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT format, BOOL left_view_frame0, BOOL base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE flip_mode, int mono_offset); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamAutoProcessingMode)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamFilter)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL enable, int level); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorSetStreamExtension)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, const GUID *guid, UINT data_size, void *data); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamFrameFormat)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_FRAME_FORMAT *format); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamColorSpace)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamOutputRate)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE *rate, BOOL *repeat, DXGI_RATIONAL *custom_rate); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamSourceRect)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamDestRect)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, RECT *rect); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamAlpha)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, float *alpha); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamPalette)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, UINT *entries); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamPixelAspectRatio)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, DXGI_RATIONAL *src_aspect_ratio, DXGI_RATIONAL *dst_aspect_ratio); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamLumaKey)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, float *lower, float *upper); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamStereoFormat)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT *format, BOOL *left_view_frame0, BOOL *base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE *flip_mode, int *mono_offset); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamAutoProcessingMode)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamFilter)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL *enabled, int *level); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorGetStreamExtension)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, const GUID *guid, UINT data_size, void *data); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorBlt)(T *This, ID3D11VideoProcessor *processor, ID3D11VideoProcessorOutputView *view, UINT frame_idx, UINT stream_count, const D3D11_VIDEO_PROCESSOR_STREAM *streams); \
    HRESULT(STDMETHODCALLTYPE *NegotiateCryptoSessionKeyExchange)(T *This, ID3D11CryptoSession *session, UINT data_size, void *data); \
    void(STDMETHODCALLTYPE *EncryptionBlt)(T *This, ID3D11CryptoSession *session, ID3D11Texture2D *src_surface, ID3D11Texture2D *dst_surface, UINT iv_size, void *iv); \
    void(STDMETHODCALLTYPE *DecryptionBlt)(T *This, ID3D11CryptoSession *session, ID3D11Texture2D *src_surface, ID3D11Texture2D *dst_surface, D3D11_ENCRYPTED_BLOCK_INFO *block_info, UINT key_size, const void *key, UINT iv_size, void *iv); \
    void(STDMETHODCALLTYPE *StartSessionKeyRefresh)(T *This, ID3D11CryptoSession *session, UINT random_number_size, void *random_number); \
    void(STDMETHODCALLTYPE *FinishSessionKeyRefresh)(T *This, ID3D11CryptoSession *session); \
    HRESULT(STDMETHODCALLTYPE *GetEncryptionBltKey)(T *This, ID3D11CryptoSession *session, UINT key_size, void *key); \
    HRESULT(STDMETHODCALLTYPE *NegotiateAuthenticatedChannelKeyExchange)(T *This, ID3D11AuthenticatedChannel *channel, UINT data_size, void *data); \
    HRESULT(STDMETHODCALLTYPE *QueryAuthenticatedChannel)(T *This, ID3D11AuthenticatedChannel *channel, UINT input_size, const void *input, UINT output_size, void *output); \
    HRESULT(STDMETHODCALLTYPE *ConfigureAuthenticatedChannel)(T *This, ID3D11AuthenticatedChannel *channel, UINT input_size, const void *input, D3D11_AUTHENTICATED_CONFIGURE_OUTPUT *output); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamRotation)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, D3D11_VIDEO_PROCESSOR_ROTATION rotation); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamRotation)(T *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enable, D3D11_VIDEO_PROCESSOR_ROTATION *rotation);

#define ID3D11VideoDevice_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *CreateVideoDecoder)(T *This, const D3D11_VIDEO_DECODER_DESC *pVideoDesc, const D3D11_VIDEO_DECODER_CONFIG *pConfig, ID3D11VideoDecoder **ppDecoder); \
    HRESULT(STDMETHODCALLTYPE *CreateVideoProcessor)(T *This, ID3D11VideoProcessorEnumerator *pEnum, UINT RateConversionIndex, ID3D11VideoProcessor **ppVideoProcessor); \
    HRESULT(STDMETHODCALLTYPE *CreateAuthenticatedChannel)(T *This, D3D11_AUTHENTICATED_CHANNEL_TYPE ChannelType, ID3D11AuthenticatedChannel **ppAuthenticatedChannel); \
    HRESULT(STDMETHODCALLTYPE *CreateCryptoSession)(T *This, const GUID *pCryptoType, const GUID *pDecoderProfile, const GUID *pKeyExchangeType, ID3D11CryptoSession **ppCryptoSession); \
    HRESULT(STDMETHODCALLTYPE *CreateVideoDecoderOutputView)(T *This, ID3D11Resource *pResource, const D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC *pDesc, ID3D11VideoDecoderOutputView **ppVDOVView); \
    HRESULT(STDMETHODCALLTYPE *CreateVideoProcessorInputView)(T *This, ID3D11Resource *pResource, ID3D11VideoProcessorEnumerator *pEnum, const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *pDesc, ID3D11VideoProcessorInputView **ppVPIView); \
    HRESULT(STDMETHODCALLTYPE *CreateVideoProcessorOutputView)(T *This, ID3D11Resource *pResource, ID3D11VideoProcessorEnumerator *pEnum, const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *pDesc, ID3D11VideoProcessorOutputView **ppVPOView); \
    HRESULT(STDMETHODCALLTYPE *CreateVideoProcessorEnumerator)(T *This, const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *pDesc, ID3D11VideoProcessorEnumerator **ppEnum); \
    UINT(STDMETHODCALLTYPE *GetVideoDecoderProfileCount)(T *This); \
    HRESULT(STDMETHODCALLTYPE *GetVideoDecoderProfile)(T *This, UINT Index, GUID *pDecoderProfile); \
    HRESULT(STDMETHODCALLTYPE *CheckVideoDecoderFormat)(T *This, const GUID *pDecoderProfile, DXGI_FORMAT Format, BOOL *pSupported); \
    HRESULT(STDMETHODCALLTYPE *GetVideoDecoderConfigCount)(T *This, const D3D11_VIDEO_DECODER_DESC *pDesc, UINT *pCount); \
    HRESULT(STDMETHODCALLTYPE *GetVideoDecoderConfig)(T *This, const D3D11_VIDEO_DECODER_DESC *pDesc, UINT Index, D3D11_VIDEO_DECODER_CONFIG *pConfig); \
    HRESULT(STDMETHODCALLTYPE *GetContentProtectionCaps)(T *This, const GUID *pCryptoType, const GUID *pDecoderProfile, D3D11_VIDEO_CONTENT_PROTECTION_CAPS *pCaps); \
    HRESULT(STDMETHODCALLTYPE *CheckCryptoKeyExchange)(T *This, const GUID *pCryptoType, const GUID *pDecoderProfile, UINT Index, GUID *pKeyExchangeType); \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T *This, REFGUID guid, UINT DataSize, const void *pData); \
    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)(T *This, REFGUID guid, const IUnknown *pData);

#define ID3D11VideoProcessorEnumerator1_METHODS(T) \
    ID3D11VideoProcessorEnumerator_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *CheckVideoProcessorFormatConversion)(T *This, DXGI_FORMAT input_format, DXGI_COLOR_SPACE_TYPE input_colour_space, DXGI_FORMAT output_format, DXGI_COLOR_SPACE_TYPE output_colour_space, BOOL *supported);

#define ID3D11VideoContext1_METHODS(T) \
    ID3D11VideoContext_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *SubmitDecoderBuffers1)(T *This, ID3D11VideoDecoder *decoder, UINT buffer_count, const D3D11_VIDEO_DECODER_BUFFER_DESC1 *buffer_desc); \
    HRESULT(STDMETHODCALLTYPE *GetDataForNewHardwareKey)(T *This, ID3D11CryptoSession *session, UINT input_size, const void *input_data, UINT64 *output_data); \
    HRESULT(STDMETHODCALLTYPE *CheckCryptoSessionStatus)(T *This, ID3D11CryptoSession *session, D3D11_CRYPTO_SESSION_STATUS *status); \
    HRESULT(STDMETHODCALLTYPE *DecoderEnableDownsampling)(T *This, ID3D11VideoDecoder *decoder, DXGI_COLOR_SPACE_TYPE colour_space, const D3D11_VIDEO_SAMPLE_DESC *output_desc, UINT reference_frame_count); \
    HRESULT(STDMETHODCALLTYPE *DecoderUpdateDownsampling)(T *This, ID3D11VideoDecoder *decoder, const D3D11_VIDEO_SAMPLE_DESC *output_desc); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputColorSpace1)(T *This, ID3D11VideoProcessor *processor, DXGI_COLOR_SPACE_TYPE colour_space); \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputShaderUsage)(T *This, ID3D11VideoProcessor *processor, BOOL shader_usage); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputColorSpace1)(T *This, ID3D11VideoProcessor *processor, DXGI_COLOR_SPACE_TYPE *colour_space); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputShaderUsage)(T *This, ID3D11VideoProcessor *processor, BOOL *shader_usage); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamColorSpace1)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_COLOR_SPACE_TYPE colour_space); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamMirror)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, BOOL enable, BOOL flip_horizontal, BOOL flip_vertical); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamColorSpace1)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_COLOR_SPACE_TYPE *colour_space); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamMirror)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, BOOL *enable, BOOL *flip_horizontal, BOOL *flip_vertical); \
    HRESULT(STDMETHODCALLTYPE *VideoProcessorGetBehaviorHints)(T *This, ID3D11VideoProcessor *processor, UINT output_width, UINT output_height, DXGI_FORMAT output_format, UINT stream_count, const D3D11_VIDEO_PROCESSOR_STREAM_BEHAVIOR_HINT *streams, UINT *behaviour_hints);

#define ID3D11VideoContext2_METHODS(T) \
    ID3D11VideoContext1_METHODS(T) \
    void(STDMETHODCALLTYPE *VideoProcessorSetOutputHDRMetaData)(T *This, ID3D11VideoProcessor *processor, DXGI_HDR_METADATA_TYPE type, UINT size, const void *meta_data); \
    void(STDMETHODCALLTYPE *VideoProcessorGetOutputHDRMetaData)(T *This, ID3D11VideoProcessor *processor, DXGI_HDR_METADATA_TYPE *type, UINT size, void *meta_data); \
    void(STDMETHODCALLTYPE *VideoProcessorSetStreamHDRMetaData)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_HDR_METADATA_TYPE type, UINT size, const void *meta_data); \
    void(STDMETHODCALLTYPE *VideoProcessorGetStreamHDRMetaData)(T *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_HDR_METADATA_TYPE *type, UINT size, void *meta_data);

#define ID3D11VideoDevice1_METHODS(T) \
    ID3D11VideoDevice_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetCryptoSessionPrivateDataSize)(T *This, const GUID *crypto_type, const GUID *decoder_profile, const GUID *key_exchange_type, UINT *input_size, UINT *output_size); \
    HRESULT(STDMETHODCALLTYPE *GetVideoDecoderCaps)(T *This, const GUID *decoder_profile, UINT sample_width, UINT sample_height, const DXGI_RATIONAL *framerate, UINT bitrate, const GUID *crypto_type, UINT *decoder_caps); \
    HRESULT(STDMETHODCALLTYPE *CheckVideoDecoderDownsampling)(T *This, const D3D11_VIDEO_DECODER_DESC *input_desc, DXGI_COLOR_SPACE_TYPE input_colour_space, const D3D11_VIDEO_DECODER_CONFIG *input_config, const DXGI_RATIONAL *framerate, const D3D11_VIDEO_SAMPLE_DESC *output_desc, BOOL *supported, BOOL *real_time_hint); \
    HRESULT(STDMETHODCALLTYPE *RecommendVideoDecoderDownsampleParameters)(T *This, const D3D11_VIDEO_DECODER_DESC *input_desc, DXGI_COLOR_SPACE_TYPE input_colour_space, const D3D11_VIDEO_DECODER_CONFIG *input_config, const DXGI_RATIONAL *framerate, D3D11_VIDEO_SAMPLE_DESC *recommended_output_desc);

MIME_COM_INTERFACE(ID3D11VideoDecoder)
MIME_COM_INTERFACE(ID3D11VideoProcessorEnumerator)
MIME_COM_INTERFACE(ID3D11VideoProcessorEnumerator1)
MIME_COM_INTERFACE(ID3D11VideoProcessor)
MIME_COM_INTERFACE(ID3D11AuthenticatedChannel)
MIME_COM_INTERFACE(ID3D11CryptoSession)
MIME_COM_INTERFACE(ID3D11VideoDecoderOutputView)
MIME_COM_INTERFACE(ID3D11VideoProcessorInputView)
MIME_COM_INTERFACE(ID3D11VideoProcessorOutputView)
MIME_COM_INTERFACE(ID3D11VideoContext)
MIME_COM_INTERFACE(ID3D11VideoContext1)
MIME_COM_INTERFACE(ID3D11VideoContext2)
MIME_COM_INTERFACE(ID3D11VideoDevice)
MIME_COM_INTERFACE(ID3D11VideoDevice1)

MIME_DEFINE_GUID(IID_ID3D11VideoDecoder, 0x3C9C5B51, 0x995D, 0x48D1, 0x9B, 0x8D, 0xFA, 0x5C, 0xAE, 0xDE, 0xD6, 0x5C);
MIME_DEFINE_GUID(IID_ID3D11VideoProcessorEnumerator, 0x31627037, 0x53AB, 0x4200, 0x90, 0x61, 0x05, 0xFA, 0xA9, 0xAB, 0x45, 0xF9);
MIME_DEFINE_GUID(IID_ID3D11VideoProcessorEnumerator1, 0x465217F2, 0x5568, 0x43CF, 0xB5, 0xB9, 0xF6, 0x1D, 0x54, 0x53, 0x1C, 0xA1);
MIME_DEFINE_GUID(IID_ID3D11VideoProcessor, 0x1D7B0652, 0x185F, 0x41C6, 0x85, 0xCE, 0x0C, 0x5B, 0xE3, 0xD4, 0xAE, 0x6C);
MIME_DEFINE_GUID(IID_ID3D11AuthenticatedChannel, 0x3015A308, 0xDCBD, 0x47AA, 0xA7, 0x47, 0x19, 0x24, 0x86, 0xD1, 0x4D, 0x4A);
MIME_DEFINE_GUID(IID_ID3D11CryptoSession, 0x9B32F9AD, 0xBDCC, 0x40A6, 0xA3, 0x9D, 0xD5, 0xC8, 0x65, 0x84, 0x57, 0x20);
MIME_DEFINE_GUID(IID_ID3D11VideoDecoderOutputView, 0xC2931AEA, 0x2A85, 0x4F20, 0x86, 0x0F, 0xFB, 0xA1, 0xFD, 0x25, 0x6E, 0x18);
MIME_DEFINE_GUID(IID_ID3D11VideoProcessorInputView, 0x11EC5A5F, 0x51DC, 0x4945, 0xAB, 0x34, 0x6E, 0x8C, 0x21, 0x30, 0x0E, 0xA5);
MIME_DEFINE_GUID(IID_ID3D11VideoProcessorOutputView, 0xA048285E, 0x25A9, 0x4527, 0xBD, 0x93, 0xD6, 0x8B, 0x68, 0xC4, 0x42, 0x54);
MIME_DEFINE_GUID(IID_ID3D11VideoContext, 0x61F21C45, 0x3C0E, 0x4A74, 0x9C, 0xEA, 0x67, 0x10, 0x0D, 0x9A, 0xD5, 0xE4);
MIME_DEFINE_GUID(IID_ID3D11VideoContext1, 0xA7F026DA, 0xA5F8, 0x4487, 0xA5, 0x64, 0x15, 0xE3, 0x43, 0x57, 0x65, 0x1E);
MIME_DEFINE_GUID(IID_ID3D11VideoContext2, 0xC4E7374C, 0x6243, 0x4D1B, 0xAE, 0x87, 0x52, 0xB4, 0xF7, 0x40, 0xE2, 0x61);
MIME_DEFINE_GUID(IID_ID3D11VideoDevice, 0x10EC4D5B, 0x975A, 0x4689, 0xB9, 0xE4, 0xD0, 0xAA, 0xC3, 0x0F, 0xE3, 0x33);
MIME_DEFINE_GUID(IID_ID3D11VideoDevice1, 0x29DA1D51, 0x1321, 0x4454, 0x80, 0x4B, 0xF5, 0xFC, 0x9F, 0x86, 0x1F, 0x0F);

MIME_DEFINE_GUID(D3D11_DECODER_PROFILE_H264_VLD_NOFGT, 0x1B81BE68, 0xA0C7, 0x11D3, 0xB9, 0x84, 0x00, 0xC0, 0x4F, 0x2E, 0x73, 0xC5);
MIME_DEFINE_GUID(D3D11_DECODER_PROFILE_HEVC_VLD_MAIN, 0x5B11D51B, 0x2F4C, 0x4452, 0xBC, 0xC3, 0x09, 0xF2, 0xA1, 0x16, 0x0C, 0xC0);
MIME_DEFINE_GUID(D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10, 0x107AF0E0, 0xEF1A, 0x4D19, 0xAB, 0xA8, 0x67, 0xA1, 0x63, 0x07, 0x3D, 0x13);
MIME_DEFINE_GUID(D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0, 0x463707F8, 0xA1D0, 0x4585, 0x87, 0x6D, 0x83, 0xAA, 0x6D, 0x60, 0xB8, 0x9E);
MIME_DEFINE_GUID(D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2, 0xA4C749EF, 0x6ECF, 0x48AA, 0x84, 0x48, 0x50, 0xA7, 0xA1, 0x16, 0x5F, 0xF7);
MIME_DEFINE_GUID(DXVA_ModeAV1_VLD_Profile0, 0xB8BE4CCB, 0xCF53, 0x46BA, 0x8D, 0x59, 0xD6, 0xB8, 0xA6, 0xDA, 0x5D, 0x2A);

#endif
