/*
 * dxgi: adapters, outputs and swap chains. the interfaces and their method
 * order are windows', as programs call them through the vtables.
 */
#ifndef MIME_WIN32_DXGI_H
#define MIME_WIN32_DXGI_H

#include <mime/win32/objbase.h>
#include <mime/win32/dxgiformat.h>

#define DXGI_ERROR_INVALID_CALL ((HRESULT)0x887A0001)
#define DXGI_ERROR_NOT_FOUND ((HRESULT)0x887A0002)
#define DXGI_ERROR_MORE_DATA ((HRESULT)0x887A0003)
#define DXGI_ERROR_UNSUPPORTED ((HRESULT)0x887A0004)
#define DXGI_ERROR_DEVICE_REMOVED ((HRESULT)0x887A0005)
#define DXGI_ERROR_DEVICE_HUNG ((HRESULT)0x887A0006)
#define DXGI_ERROR_DEVICE_RESET ((HRESULT)0x887A0007)
#define DXGI_ERROR_WAS_STILL_DRAWING ((HRESULT)0x887A000A)
#define DXGI_ERROR_FRAME_STATISTICS_DISJOINT ((HRESULT)0x887A000B)
#define DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE ((HRESULT)0x887A000C)
#define DXGI_ERROR_DRIVER_INTERNAL_ERROR ((HRESULT)0x887A0020)
#define DXGI_ERROR_NONEXCLUSIVE ((HRESULT)0x887A0021)
#define DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ((HRESULT)0x887A0022)
#define DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED ((HRESULT)0x887A0023)
#define DXGI_ERROR_REMOTE_OUTOFMEMORY ((HRESULT)0x887A0024)
#define DXGI_ERROR_ACCESS_LOST ((HRESULT)0x887A0026)
#define DXGI_ERROR_WAIT_TIMEOUT ((HRESULT)0x887A0027)
#define DXGI_ERROR_SESSION_DISCONNECTED ((HRESULT)0x887A0028)
#define DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE ((HRESULT)0x887A0029)
#define DXGI_ERROR_CANNOT_PROTECT_CONTENT ((HRESULT)0x887A002A)
#define DXGI_ERROR_ACCESS_DENIED ((HRESULT)0x887A002B)
#define DXGI_ERROR_NAME_ALREADY_EXISTS ((HRESULT)0x887A002C)
#define DXGI_ERROR_SDK_COMPONENT_MISSING ((HRESULT)0x887A002D)
#define DXGI_ERROR_NOT_CURRENT ((HRESULT)0x887A002E)
#define DXGI_ERROR_MODE_CHANGE_IN_PROGRESS ((HRESULT)0x887A0025)
#define DXGI_STATUS_OCCLUDED ((HRESULT)0x087A0001)
#define DXGI_STATUS_MODE_CHANGED ((HRESULT)0x087A0007)
#define DXGI_STATUS_MODE_CHANGE_IN_PROGRESS ((HRESULT)0x087A0008)

#define DXGI_USAGE_SHADER_INPUT 0x00000010
#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x00000020
#define DXGI_USAGE_BACK_BUFFER 0x00000040
#define DXGI_USAGE_SHARED 0x00000080
#define DXGI_USAGE_READ_ONLY 0x00000100
#define DXGI_USAGE_DISCARD_ON_PRESENT 0x00000200
#define DXGI_USAGE_UNORDERED_ACCESS 0x00000400
typedef UINT DXGI_USAGE;

#define DXGI_PRESENT_TEST 0x00000001
#define DXGI_PRESENT_DO_NOT_SEQUENCE 0x00000002
#define DXGI_PRESENT_RESTART 0x00000004
#define DXGI_PRESENT_DO_NOT_WAIT 0x00000008
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200

#define DXGI_MWA_NO_WINDOW_CHANGES 0x1
#define DXGI_MWA_NO_ALT_ENTER 0x2
#define DXGI_MWA_NO_PRINT_SCREEN 0x4

#define DXGI_ENUM_MODES_INTERLACED 0x1
#define DXGI_ENUM_MODES_SCALING 0x2

#define DXGI_MAP_READ 0x1
#define DXGI_MAP_WRITE 0x2
#define DXGI_MAP_DISCARD 0x4

#define DXGI_CREATE_FACTORY_DEBUG 0x1

#define DXGI_RESOURCE_PRIORITY_MINIMUM 0x28000000
#define DXGI_RESOURCE_PRIORITY_LOW 0x50000000
#define DXGI_RESOURCE_PRIORITY_NORMAL 0x78000000
#define DXGI_RESOURCE_PRIORITY_HIGH 0xA0000000
#define DXGI_RESOURCE_PRIORITY_MAXIMUM 0xC8000000

#define DXGI_MAX_SWAP_CHAIN_BUFFERS 16

typedef struct DXGI_RATIONAL {
    UINT Numerator;
    UINT Denominator;
} DXGI_RATIONAL;

typedef struct DXGI_SAMPLE_DESC {
    UINT Count;
    UINT Quality;
} DXGI_SAMPLE_DESC;

typedef struct DXGI_RGB {
    float Red, Green, Blue;
} DXGI_RGB;

typedef struct DXGI_RGBA {
    float r, g, b, a;
} DXGI_RGBA;

typedef struct DXGI_GAMMA_CONTROL {
    DXGI_RGB Scale;
    DXGI_RGB Offset;
    DXGI_RGB GammaCurve[1025];
} DXGI_GAMMA_CONTROL;

typedef struct DXGI_GAMMA_CONTROL_CAPABILITIES {
    BOOL ScaleAndOffsetSupported;
    float MaxConvertedValue;
    float MinConvertedValue;
    UINT NumGammaControlPoints;
    float ControlPointPositions[1025];
} DXGI_GAMMA_CONTROL_CAPABILITIES;

typedef enum DXGI_MODE_SCANLINE_ORDER {
    DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED = 0,
    DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE = 1,
    DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST = 2,
    DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST = 3
} DXGI_MODE_SCANLINE_ORDER;

typedef enum DXGI_MODE_SCALING {
    DXGI_MODE_SCALING_UNSPECIFIED = 0,
    DXGI_MODE_SCALING_CENTERED = 1,
    DXGI_MODE_SCALING_STRETCHED = 2
} DXGI_MODE_SCALING;

typedef enum DXGI_MODE_ROTATION {
    DXGI_MODE_ROTATION_UNSPECIFIED = 0,
    DXGI_MODE_ROTATION_IDENTITY = 1,
    DXGI_MODE_ROTATION_ROTATE90 = 2,
    DXGI_MODE_ROTATION_ROTATE180 = 3,
    DXGI_MODE_ROTATION_ROTATE270 = 4
} DXGI_MODE_ROTATION;

typedef struct DXGI_MODE_DESC {
    UINT Width;
    UINT Height;
    DXGI_RATIONAL RefreshRate;
    DXGI_FORMAT Format;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_MODE_SCALING Scaling;
} DXGI_MODE_DESC;

typedef struct DXGI_MODE_DESC1 {
    UINT Width;
    UINT Height;
    DXGI_RATIONAL RefreshRate;
    DXGI_FORMAT Format;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_MODE_SCALING Scaling;
    BOOL Stereo;
} DXGI_MODE_DESC1;

typedef enum DXGI_COLOR_SPACE_TYPE {
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 = 0,
    DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1,
    DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709 = 2,
    DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020 = 3,
    DXGI_COLOR_SPACE_RESERVED = 4,
    DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601 = 5,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 = 6,
    DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 = 7,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 = 8,
    DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 = 9,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020 = 10,
    DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 = 11,
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 = 13,
    DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 = 14,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020 = 15,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 = 16,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 = 17,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 = 18,
    DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 = 19,
    DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709 = 20,
    DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020 = 21,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709 = 22,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020 = 23,
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 = 24,
    DXGI_COLOR_SPACE_CUSTOM = 0xFFFFFFFF
} DXGI_COLOR_SPACE_TYPE;

typedef struct DXGI_SURFACE_DESC {
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
} DXGI_SURFACE_DESC;

typedef struct DXGI_MAPPED_RECT {
    INT Pitch;
    BYTE *pBits;
} DXGI_MAPPED_RECT;

typedef struct DXGI_FRAME_STATISTICS {
    UINT PresentCount;
    UINT PresentRefreshCount;
    UINT SyncRefreshCount;
    LARGE_INTEGER SyncQPCTime;
    LARGE_INTEGER SyncGPUTime;
} DXGI_FRAME_STATISTICS;

typedef struct DXGI_ADAPTER_DESC {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
} DXGI_ADAPTER_DESC;

typedef enum DXGI_ADAPTER_FLAG {
    DXGI_ADAPTER_FLAG_NONE = 0,
    DXGI_ADAPTER_FLAG_REMOTE = 1,
    DXGI_ADAPTER_FLAG_SOFTWARE = 2
} DXGI_ADAPTER_FLAG;

typedef struct DXGI_ADAPTER_DESC1 {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
} DXGI_ADAPTER_DESC1;

typedef enum DXGI_GRAPHICS_PREEMPTION_GRANULARITY {
    DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY = 0,
    DXGI_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY = 1,
    DXGI_GRAPHICS_PREEMPTION_TRIANGLE_BOUNDARY = 2,
    DXGI_GRAPHICS_PREEMPTION_PIXEL_BOUNDARY = 3,
    DXGI_GRAPHICS_PREEMPTION_INSTRUCTION_BOUNDARY = 4
} DXGI_GRAPHICS_PREEMPTION_GRANULARITY;

typedef enum DXGI_COMPUTE_PREEMPTION_GRANULARITY {
    DXGI_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY = 0,
    DXGI_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY = 1,
    DXGI_COMPUTE_PREEMPTION_THREAD_GROUP_BOUNDARY = 2,
    DXGI_COMPUTE_PREEMPTION_THREAD_BOUNDARY = 3,
    DXGI_COMPUTE_PREEMPTION_INSTRUCTION_BOUNDARY = 4
} DXGI_COMPUTE_PREEMPTION_GRANULARITY;

typedef struct DXGI_ADAPTER_DESC2 {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
    DXGI_GRAPHICS_PREEMPTION_GRANULARITY GraphicsPreemptionGranularity;
    DXGI_COMPUTE_PREEMPTION_GRANULARITY ComputePreemptionGranularity;
} DXGI_ADAPTER_DESC2;

typedef enum DXGI_ADAPTER_FLAG3 {
    DXGI_ADAPTER_FLAG3_NONE = 0,
    DXGI_ADAPTER_FLAG3_REMOTE = 1,
    DXGI_ADAPTER_FLAG3_SOFTWARE = 2,
    DXGI_ADAPTER_FLAG3_ACG_COMPATIBLE = 4,
    DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES = 8,
    DXGI_ADAPTER_FLAG3_SUPPORT_NON_MONITORED_FENCES = 0x10,
    DXGI_ADAPTER_FLAG3_KEYED_MUTEX_CONFORMANCE = 0x20
} DXGI_ADAPTER_FLAG3;

typedef struct DXGI_ADAPTER_DESC3 {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    DXGI_ADAPTER_FLAG3 Flags;
    DXGI_GRAPHICS_PREEMPTION_GRANULARITY GraphicsPreemptionGranularity;
    DXGI_COMPUTE_PREEMPTION_GRANULARITY ComputePreemptionGranularity;
} DXGI_ADAPTER_DESC3;

typedef struct DXGI_OUTPUT_DESC {
    WCHAR DeviceName[32];
    RECT DesktopCoordinates;
    BOOL AttachedToDesktop;
    DXGI_MODE_ROTATION Rotation;
    HMONITOR Monitor;
} DXGI_OUTPUT_DESC;

typedef struct DXGI_OUTPUT_DESC1 {
    WCHAR DeviceName[32];
    RECT DesktopCoordinates;
    BOOL AttachedToDesktop;
    DXGI_MODE_ROTATION Rotation;
    HMONITOR Monitor;
    UINT BitsPerColor;
    DXGI_COLOR_SPACE_TYPE ColorSpace;
    float RedPrimary[2];
    float GreenPrimary[2];
    float BluePrimary[2];
    float WhitePoint[2];
    float MinLuminance;
    float MaxLuminance;
    float MaxFullFrameLuminance;
} DXGI_OUTPUT_DESC1;

typedef struct DXGI_SHARED_RESOURCE {
    HANDLE Handle;
} DXGI_SHARED_RESOURCE;

typedef enum DXGI_RESIDENCY {
    DXGI_RESIDENCY_FULLY_RESIDENT = 1,
    DXGI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY = 2,
    DXGI_RESIDENCY_EVICTED_TO_DISK = 3
} DXGI_RESIDENCY;

typedef enum DXGI_SWAP_EFFECT {
    DXGI_SWAP_EFFECT_DISCARD = 0,
    DXGI_SWAP_EFFECT_SEQUENTIAL = 1,
    DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL = 3,
    DXGI_SWAP_EFFECT_FLIP_DISCARD = 4
} DXGI_SWAP_EFFECT;

typedef enum DXGI_SWAP_CHAIN_FLAG {
    DXGI_SWAP_CHAIN_FLAG_NONPREROTATED = 1,
    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 2,
    DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE = 4,
    DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT = 8,
    DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER = 16,
    DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY = 32,
    DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT = 64,
    DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER = 128,
    DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO = 256,
    DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO = 512,
    DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED = 1024,
    DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING = 2048,
    DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS = 4096
} DXGI_SWAP_CHAIN_FLAG;

typedef struct DXGI_SWAP_CHAIN_DESC {
    DXGI_MODE_DESC BufferDesc;
    DXGI_SAMPLE_DESC SampleDesc;
    DXGI_USAGE BufferUsage;
    UINT BufferCount;
    HWND OutputWindow;
    BOOL Windowed;
    DXGI_SWAP_EFFECT SwapEffect;
    UINT Flags;
} DXGI_SWAP_CHAIN_DESC;

typedef enum DXGI_SCALING {
    DXGI_SCALING_STRETCH = 0,
    DXGI_SCALING_NONE = 1,
    DXGI_SCALING_ASPECT_RATIO_STRETCH = 2
} DXGI_SCALING;

typedef enum DXGI_ALPHA_MODE {
    DXGI_ALPHA_MODE_UNSPECIFIED = 0,
    DXGI_ALPHA_MODE_PREMULTIPLIED = 1,
    DXGI_ALPHA_MODE_STRAIGHT = 2,
    DXGI_ALPHA_MODE_IGNORE = 3
} DXGI_ALPHA_MODE;

typedef struct DXGI_SWAP_CHAIN_DESC1 {
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    BOOL Stereo;
    DXGI_SAMPLE_DESC SampleDesc;
    DXGI_USAGE BufferUsage;
    UINT BufferCount;
    DXGI_SCALING Scaling;
    DXGI_SWAP_EFFECT SwapEffect;
    DXGI_ALPHA_MODE AlphaMode;
    UINT Flags;
} DXGI_SWAP_CHAIN_DESC1;

typedef struct DXGI_SWAP_CHAIN_FULLSCREEN_DESC {
    DXGI_RATIONAL RefreshRate;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_MODE_SCALING Scaling;
    BOOL Windowed;
} DXGI_SWAP_CHAIN_FULLSCREEN_DESC;

typedef struct DXGI_PRESENT_PARAMETERS {
    UINT DirtyRectsCount;
    RECT *pDirtyRects;
    RECT *pScrollRect;
    POINT *pScrollOffset;
} DXGI_PRESENT_PARAMETERS;

typedef struct DXGI_MATRIX_3X2_F {
    float _11, _12, _21, _22, _31, _32;
} DXGI_MATRIX_3X2_F;

typedef struct DXGI_DECODE_SWAP_CHAIN_DESC {
    UINT Flags;
} DXGI_DECODE_SWAP_CHAIN_DESC;

typedef enum DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAGS {
    DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAG_NOMINAL_RANGE = 0x1,
    DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAG_BT709 = 0x2,
    DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAG_xvYCC = 0x4
} DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAGS;

typedef enum DXGI_FRAME_PRESENTATION_MODE {
    DXGI_FRAME_PRESENTATION_MODE_COMPOSED = 0,
    DXGI_FRAME_PRESENTATION_MODE_OVERLAY = 1,
    DXGI_FRAME_PRESENTATION_MODE_NONE = 2,
    DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE = 3
} DXGI_FRAME_PRESENTATION_MODE;

typedef struct DXGI_FRAME_STATISTICS_MEDIA {
    UINT PresentCount;
    UINT PresentRefreshCount;
    UINT SyncRefreshCount;
    LARGE_INTEGER SyncQPCTime;
    LARGE_INTEGER SyncGPUTime;
    DXGI_FRAME_PRESENTATION_MODE CompositionMode;
    UINT ApprovedPresentDuration;
} DXGI_FRAME_STATISTICS_MEDIA;

typedef enum DXGI_OFFER_RESOURCE_PRIORITY {
    DXGI_OFFER_RESOURCE_PRIORITY_LOW = 1,
    DXGI_OFFER_RESOURCE_PRIORITY_NORMAL = 2,
    DXGI_OFFER_RESOURCE_PRIORITY_HIGH = 3
} DXGI_OFFER_RESOURCE_PRIORITY;

typedef enum DXGI_MEMORY_SEGMENT_GROUP {
    DXGI_MEMORY_SEGMENT_GROUP_LOCAL = 0,
    DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL = 1
} DXGI_MEMORY_SEGMENT_GROUP;

typedef struct DXGI_QUERY_VIDEO_MEMORY_INFO {
    UINT64 Budget;
    UINT64 CurrentUsage;
    UINT64 AvailableForReservation;
    UINT64 CurrentReservation;
} DXGI_QUERY_VIDEO_MEMORY_INFO;

typedef enum DXGI_HDR_METADATA_TYPE {
    DXGI_HDR_METADATA_TYPE_NONE = 0,
    DXGI_HDR_METADATA_TYPE_HDR10 = 1,
    DXGI_HDR_METADATA_TYPE_HDR10PLUS = 2
} DXGI_HDR_METADATA_TYPE;

typedef enum DXGI_FEATURE {
    DXGI_FEATURE_PRESENT_ALLOW_TEARING = 0
} DXGI_FEATURE;

typedef enum DXGI_GPU_PREFERENCE {
    DXGI_GPU_PREFERENCE_UNSPECIFIED = 0,
    DXGI_GPU_PREFERENCE_MINIMUM_POWER = 1,
    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE = 2
} DXGI_GPU_PREFERENCE;

typedef struct DXGI_OUTDUPL_DESC DXGI_OUTDUPL_DESC;
typedef struct DXGI_OUTDUPL_FRAME_INFO DXGI_OUTDUPL_FRAME_INFO;
typedef struct DXGI_OUTDUPL_MOVE_RECT DXGI_OUTDUPL_MOVE_RECT;
typedef struct DXGI_OUTDUPL_POINTER_SHAPE_INFO DXGI_OUTDUPL_POINTER_SHAPE_INFO;

typedef struct IDXGIObject IDXGIObject;
typedef struct IDXGIDeviceSubObject IDXGIDeviceSubObject;
typedef struct IDXGIResource IDXGIResource;
typedef struct IDXGIResource1 IDXGIResource1;
typedef struct IDXGIKeyedMutex IDXGIKeyedMutex;
typedef struct IDXGISurface IDXGISurface;
typedef struct IDXGISurface1 IDXGISurface1;
typedef struct IDXGISurface2 IDXGISurface2;
typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct IDXGIAdapter1 IDXGIAdapter1;
typedef struct IDXGIAdapter2 IDXGIAdapter2;
typedef struct IDXGIAdapter3 IDXGIAdapter3;
typedef struct IDXGIAdapter4 IDXGIAdapter4;
typedef struct IDXGIOutput IDXGIOutput;
typedef struct IDXGIOutput1 IDXGIOutput1;
typedef struct IDXGIOutput2 IDXGIOutput2;
typedef struct IDXGIOutput3 IDXGIOutput3;
typedef struct IDXGIOutput4 IDXGIOutput4;
typedef struct IDXGIOutput5 IDXGIOutput5;
typedef struct IDXGIOutput6 IDXGIOutput6;
typedef struct IDXGIOutputDuplication IDXGIOutputDuplication;
typedef struct IDXGISwapChain IDXGISwapChain;
typedef struct IDXGISwapChain1 IDXGISwapChain1;
typedef struct IDXGISwapChain2 IDXGISwapChain2;
typedef struct IDXGISwapChain3 IDXGISwapChain3;
typedef struct IDXGISwapChain4 IDXGISwapChain4;
typedef struct IDXGIFactory IDXGIFactory;
typedef struct IDXGIFactory1 IDXGIFactory1;
typedef struct IDXGIFactory2 IDXGIFactory2;
typedef struct IDXGIFactory3 IDXGIFactory3;
typedef struct IDXGIFactory4 IDXGIFactory4;
typedef struct IDXGIFactory5 IDXGIFactory5;
typedef struct IDXGIFactory6 IDXGIFactory6;
typedef struct IDXGIFactory7 IDXGIFactory7;
typedef struct IDXGIFactoryMedia IDXGIFactoryMedia;
typedef struct IDXGIDecodeSwapChain IDXGIDecodeSwapChain;
typedef struct IDXGISwapChainMedia IDXGISwapChainMedia;
typedef struct IDXGIDevice IDXGIDevice;
typedef struct IDXGIDevice1 IDXGIDevice1;
typedef struct IDXGIDevice2 IDXGIDevice2;
typedef struct IDXGIDevice3 IDXGIDevice3;
typedef struct IDXGIDevice4 IDXGIDevice4;

#define IDXGIObject_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T * This, REFGUID name, UINT size, const void *data);                  \
    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)(T * This, REFGUID name, const IUnknown *unknown);             \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T * This, REFGUID name, UINT *size, void *data);                       \
    HRESULT(STDMETHODCALLTYPE *GetParent)(T * This, REFIID riid, void **parent);

#define IDXGIDeviceSubObject_METHODS(T)                                                                                \
    IDXGIObject_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, REFIID riid, void **device);

#define IDXGIResource_METHODS(T)                                                                                       \
    IDXGIDeviceSubObject_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetSharedHandle)(T * This, HANDLE * handle);                                            \
    HRESULT(STDMETHODCALLTYPE *GetUsage)(T * This, DXGI_USAGE * usage);                                                \
    HRESULT(STDMETHODCALLTYPE *SetEvictionPriority)(T * This, UINT priority);                                          \
    HRESULT(STDMETHODCALLTYPE *GetEvictionPriority)(T * This, UINT * priority);

#define IDXGIResource1_METHODS(T)                                                                                      \
    IDXGIResource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSubresourceSurface)(T * This, UINT index, IDXGISurface2 * *surface);              \
    HRESULT(STDMETHODCALLTYPE *CreateSharedHandle)(T * This, const SECURITY_ATTRIBUTES *attributes, DWORD access,     \
                                                   LPCWSTR name, HANDLE *handle);

#define IDXGIKeyedMutex_METHODS(T)                                                                                     \
    IDXGIDeviceSubObject_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *AcquireSync)(T * This, UINT64 key, DWORD milliseconds);                                 \
    HRESULT(STDMETHODCALLTYPE *ReleaseSync)(T * This, UINT64 key);

#define IDXGISurface_METHODS(T)                                                                                        \
    IDXGIDeviceSubObject_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, DXGI_SURFACE_DESC * desc);                                           \
    HRESULT(STDMETHODCALLTYPE *Map)(T * This, DXGI_MAPPED_RECT * rect, UINT flags);                                    \
    HRESULT(STDMETHODCALLTYPE *Unmap)(T * This);

#define IDXGISurface1_METHODS(T)                                                                                       \
    IDXGISurface_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *GetDC)(T * This, BOOL discard, HDC * dc);                                               \
    HRESULT(STDMETHODCALLTYPE *ReleaseDC)(T * This, RECT * dirty);

#define IDXGISurface2_METHODS(T)                                                                                       \
    IDXGISurface1_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetResource)(T * This, REFIID riid, void **parent, UINT *index);

#define IDXGIAdapter_METHODS(T)                                                                                        \
    IDXGIObject_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *EnumOutputs)(T * This, UINT index, IDXGIOutput * *output);                              \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, DXGI_ADAPTER_DESC * desc);                                           \
    HRESULT(STDMETHODCALLTYPE *CheckInterfaceSupport)(T * This, REFGUID guid, LARGE_INTEGER * version);

#define IDXGIAdapter1_METHODS(T)                                                                                       \
    IDXGIAdapter_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *GetDesc1)(T * This, DXGI_ADAPTER_DESC1 * desc);

#define IDXGIAdapter2_METHODS(T)                                                                                       \
    IDXGIAdapter1_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDesc2)(T * This, DXGI_ADAPTER_DESC2 * desc);

#define IDXGIAdapter3_METHODS(T)                                                                                       \
    IDXGIAdapter2_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterHardwareContentProtectionTeardownStatusEvent)(T * This, HANDLE event,           \
                                                                                     DWORD * cookie);                  \
    void(STDMETHODCALLTYPE *UnregisterHardwareContentProtectionTeardownStatus)(T * This, DWORD cookie);                \
    HRESULT(STDMETHODCALLTYPE *QueryVideoMemoryInfo)(T * This, UINT node, DXGI_MEMORY_SEGMENT_GROUP group,             \
                                                     DXGI_QUERY_VIDEO_MEMORY_INFO * info);                             \
    HRESULT(STDMETHODCALLTYPE *SetVideoMemoryReservation)(T * This, UINT node, DXGI_MEMORY_SEGMENT_GROUP group,        \
                                                          UINT64 reservation);                                         \
    HRESULT(STDMETHODCALLTYPE *RegisterVideoMemoryBudgetChangeNotificationEvent)(T * This, HANDLE event,               \
                                                                                 DWORD * cookie);                      \
    void(STDMETHODCALLTYPE *UnregisterVideoMemoryBudgetChangeNotification)(T * This, DWORD cookie);

#define IDXGIAdapter4_METHODS(T)                                                                                       \
    IDXGIAdapter3_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDesc3)(T * This, DXGI_ADAPTER_DESC3 * desc);

#define IDXGIOutput_METHODS(T)                                                                                         \
    IDXGIObject_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, DXGI_OUTPUT_DESC * desc);                                            \
    HRESULT(STDMETHODCALLTYPE *GetDisplayModeList)(T * This, DXGI_FORMAT format, UINT flags, UINT * count,             \
                                                   DXGI_MODE_DESC * modes);                                            \
    HRESULT(STDMETHODCALLTYPE *FindClosestMatchingMode)(T * This, const DXGI_MODE_DESC *mode,                          \
                                                        DXGI_MODE_DESC *closest, IUnknown *device);                    \
    HRESULT(STDMETHODCALLTYPE *WaitForVBlank)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *TakeOwnership)(T * This, IUnknown * device, BOOL exclusive);                            \
    void(STDMETHODCALLTYPE *ReleaseOwnership)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetGammaControlCapabilities)(T * This, DXGI_GAMMA_CONTROL_CAPABILITIES * caps);         \
    HRESULT(STDMETHODCALLTYPE *SetGammaControl)(T * This, const DXGI_GAMMA_CONTROL *control);                          \
    HRESULT(STDMETHODCALLTYPE *GetGammaControl)(T * This, DXGI_GAMMA_CONTROL * control);                               \
    HRESULT(STDMETHODCALLTYPE *SetDisplaySurface)(T * This, IDXGISurface * surface);                                   \
    HRESULT(STDMETHODCALLTYPE *GetDisplaySurfaceData)(T * This, IDXGISurface * surface);                               \
    HRESULT(STDMETHODCALLTYPE *GetFrameStatistics)(T * This, DXGI_FRAME_STATISTICS * stats);

#define IDXGIOutput1_METHODS(T)                                                                                        \
    IDXGIOutput_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *GetDisplayModeList1)(T * This, DXGI_FORMAT format, UINT flags, UINT * count,            \
                                                    DXGI_MODE_DESC1 * modes);                                          \
    HRESULT(STDMETHODCALLTYPE *FindClosestMatchingMode1)(T * This, const DXGI_MODE_DESC1 *mode,                        \
                                                         DXGI_MODE_DESC1 *closest, IUnknown *device);                  \
    HRESULT(STDMETHODCALLTYPE *GetDisplaySurfaceData1)(T * This, IDXGIResource * resource);                            \
    HRESULT(STDMETHODCALLTYPE *DuplicateOutput)(T * This, IUnknown * device, IDXGIOutputDuplication * *duplication);

#define IDXGIOutput2_METHODS(T)                                                                                        \
    IDXGIOutput1_METHODS(T)                                                                                            \
    BOOL(STDMETHODCALLTYPE *SupportsOverlays)(T * This);

#define IDXGIOutput3_METHODS(T)                                                                                        \
    IDXGIOutput2_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CheckOverlaySupport)(T * This, DXGI_FORMAT format, IUnknown * device, UINT * flags);

#define IDXGIOutput4_METHODS(T)                                                                                        \
    IDXGIOutput3_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CheckOverlayColorSpaceSupport)(T * This, DXGI_FORMAT format,                            \
                                                              DXGI_COLOR_SPACE_TYPE space, IUnknown * device,          \
                                                              UINT * flags);

#define IDXGIOutput5_METHODS(T)                                                                                        \
    IDXGIOutput4_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *DuplicateOutput1)(T * This, IUnknown * device, UINT flags, UINT count,                  \
                                                 const DXGI_FORMAT *formats, IDXGIOutputDuplication **duplication);

#define IDXGIOutput6_METHODS(T)                                                                                        \
    IDXGIOutput5_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *GetDesc1)(T * This, DXGI_OUTPUT_DESC1 * desc);                                          \
    HRESULT(STDMETHODCALLTYPE *CheckHardwareCompositionSupport)(T * This, UINT * flags);

#define IDXGISwapChain_METHODS(T)                                                                                      \
    IDXGIDeviceSubObject_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, UINT interval, UINT flags);                                          \
    HRESULT(STDMETHODCALLTYPE *GetBuffer)(T * This, UINT index, REFIID riid, void **surface);                          \
    HRESULT(STDMETHODCALLTYPE *SetFullscreenState)(T * This, BOOL fullscreen, IDXGIOutput * target);                   \
    HRESULT(STDMETHODCALLTYPE *GetFullscreenState)(T * This, BOOL * fullscreen, IDXGIOutput * *target);                \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, DXGI_SWAP_CHAIN_DESC * desc);                                        \
    HRESULT(STDMETHODCALLTYPE *ResizeBuffers)(T * This, UINT count, UINT width, UINT height, DXGI_FORMAT format,        \
                                              UINT flags);                                                             \
    HRESULT(STDMETHODCALLTYPE *ResizeTarget)(T * This, const DXGI_MODE_DESC *mode);                                    \
    HRESULT(STDMETHODCALLTYPE *GetContainingOutput)(T * This, IDXGIOutput * *output);                                  \
    HRESULT(STDMETHODCALLTYPE *GetFrameStatistics)(T * This, DXGI_FRAME_STATISTICS * stats);                           \
    HRESULT(STDMETHODCALLTYPE *GetLastPresentCount)(T * This, UINT * count);

#define IDXGISwapChain1_METHODS(T)                                                                                     \
    IDXGISwapChain_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetDesc1)(T * This, DXGI_SWAP_CHAIN_DESC1 * desc);                                      \
    HRESULT(STDMETHODCALLTYPE *GetFullscreenDesc)(T * This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC * desc);                   \
    HRESULT(STDMETHODCALLTYPE *GetHwnd)(T * This, HWND * hwnd);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetCoreWindow)(T * This, REFIID riid, void **window);                                   \
    HRESULT(STDMETHODCALLTYPE *Present1)(T * This, UINT interval, UINT flags,                                          \
                                         const DXGI_PRESENT_PARAMETERS *parameters);                                   \
    BOOL(STDMETHODCALLTYPE *IsTemporaryMonoSupported)(T * This);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetRestrictToOutput)(T * This, IDXGIOutput * *output);                                  \
    HRESULT(STDMETHODCALLTYPE *SetBackgroundColor)(T * This, const DXGI_RGBA *color);                                  \
    HRESULT(STDMETHODCALLTYPE *GetBackgroundColor)(T * This, DXGI_RGBA * color);                                       \
    HRESULT(STDMETHODCALLTYPE *SetRotation)(T * This, DXGI_MODE_ROTATION rotation);                                    \
    HRESULT(STDMETHODCALLTYPE *GetRotation)(T * This, DXGI_MODE_ROTATION * rotation);

#define IDXGISwapChain2_METHODS(T)                                                                                     \
    IDXGISwapChain1_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *SetSourceSize)(T * This, UINT width, UINT height);                                      \
    HRESULT(STDMETHODCALLTYPE *GetSourceSize)(T * This, UINT * width, UINT * height);                                  \
    HRESULT(STDMETHODCALLTYPE *SetMaximumFrameLatency)(T * This, UINT latency);                                        \
    HRESULT(STDMETHODCALLTYPE *GetMaximumFrameLatency)(T * This, UINT * latency);                                      \
    HANDLE(STDMETHODCALLTYPE *GetFrameLatencyWaitableObject)(T * This);                                                \
    HRESULT(STDMETHODCALLTYPE *SetMatrixTransform)(T * This, const DXGI_MATRIX_3X2_F *matrix);                         \
    HRESULT(STDMETHODCALLTYPE *GetMatrixTransform)(T * This, DXGI_MATRIX_3X2_F * matrix);

#define IDXGISwapChain3_METHODS(T)                                                                                     \
    IDXGISwapChain2_METHODS(T)                                                                                         \
    UINT(STDMETHODCALLTYPE *GetCurrentBackBufferIndex)(T * This);                                                      \
    HRESULT(STDMETHODCALLTYPE *CheckColorSpaceSupport)(T * This, DXGI_COLOR_SPACE_TYPE space, UINT * support);         \
    HRESULT(STDMETHODCALLTYPE *SetColorSpace1)(T * This, DXGI_COLOR_SPACE_TYPE space);                                 \
    HRESULT(STDMETHODCALLTYPE *ResizeBuffers1)(T * This, UINT count, UINT width, UINT height, DXGI_FORMAT format,       \
                                               UINT flags, const UINT *node_masks, IUnknown *const *queues);

#define IDXGISwapChain4_METHODS(T)                                                                                     \
    IDXGISwapChain3_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *SetHDRMetaData)(T * This, DXGI_HDR_METADATA_TYPE type, UINT size, void *metadata);

#define IDXGIFactory_METHODS(T)                                                                                        \
    IDXGIObject_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *EnumAdapters)(T * This, UINT index, IDXGIAdapter * *adapter);                           \
    HRESULT(STDMETHODCALLTYPE *MakeWindowAssociation)(T * This, HWND window, UINT flags);                              \
    HRESULT(STDMETHODCALLTYPE *GetWindowAssociation)(T * This, HWND * window);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChain)(T * This, IUnknown * device, DXGI_SWAP_CHAIN_DESC * desc,              \
                                                IDXGISwapChain * *swapchain);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateSoftwareAdapter)(T * This, HMODULE module, IDXGIAdapter * *adapter);

#define IDXGIFactory1_METHODS(T)                                                                                       \
    IDXGIFactory_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *EnumAdapters1)(T * This, UINT index, IDXGIAdapter1 * *adapter);                         \
    BOOL(STDMETHODCALLTYPE *IsCurrent)(T * This);

#define IDXGIFactory2_METHODS(T)                                                                                       \
    IDXGIFactory1_METHODS(T)                                                                                           \
    BOOL(STDMETHODCALLTYPE *IsWindowedStereoEnabled)(T * This);                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChainForHwnd)(T * This, IUnknown * device, HWND window,                       \
                                                       const DXGI_SWAP_CHAIN_DESC1 *desc,                              \
                                                       const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,              \
                                                       IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);         \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChainForCoreWindow)(T * This, IUnknown * device, IUnknown * window,           \
                                                             const DXGI_SWAP_CHAIN_DESC1 *desc,                        \
                                                             IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);   \
    HRESULT(STDMETHODCALLTYPE *GetSharedResourceAdapterLuid)(T * This, HANDLE resource, LUID * luid);                  \
    HRESULT(STDMETHODCALLTYPE *RegisterStereoStatusWindow)(T * This, HWND window, UINT message, DWORD * cookie);       \
    HRESULT(STDMETHODCALLTYPE *RegisterStereoStatusEvent)(T * This, HANDLE event, DWORD * cookie);                     \
    void(STDMETHODCALLTYPE *UnregisterStereoStatus)(T * This, DWORD cookie);                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterOcclusionStatusWindow)(T * This, HWND window, UINT message, DWORD * cookie);    \
    HRESULT(STDMETHODCALLTYPE *RegisterOcclusionStatusEvent)(T * This, HANDLE event, DWORD * cookie);                  \
    void(STDMETHODCALLTYPE *UnregisterOcclusionStatus)(T * This, DWORD cookie);                                        \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChainForComposition)(T * This, IUnknown * device,                             \
                                                              const DXGI_SWAP_CHAIN_DESC1 *desc,                       \
                                                              IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain);

#define IDXGIFactory3_METHODS(T)                                                                                       \
    IDXGIFactory2_METHODS(T)                                                                                           \
    UINT(STDMETHODCALLTYPE *GetCreationFlags)(T * This);

#define IDXGIFactory4_METHODS(T)                                                                                       \
    IDXGIFactory3_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnumAdapterByLuid)(T * This, LUID luid, REFIID riid, void **adapter);                   \
    HRESULT(STDMETHODCALLTYPE *EnumWarpAdapter)(T * This, REFIID riid, void **adapter);

#define IDXGIFactory5_METHODS(T)                                                                                       \
    IDXGIFactory4_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CheckFeatureSupport)(T * This, DXGI_FEATURE feature, void *data, UINT size);

#define IDXGIFactory6_METHODS(T)                                                                                       \
    IDXGIFactory5_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnumAdapterByGpuPreference)(T * This, UINT index, DXGI_GPU_PREFERENCE preference,      \
                                                           REFIID riid, void **adapter);

#define IDXGIFactory7_METHODS(T)                                                                                       \
    IDXGIFactory6_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *RegisterAdaptersChangedEvent)(T * This, HANDLE event, DWORD * cookie);                  \
    HRESULT(STDMETHODCALLTYPE *UnregisterAdaptersChangedEvent)(T * This, DWORD cookie);

#define IDXGIDecodeSwapChain_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *PresentBuffer)(T * This, UINT buffer, UINT interval, UINT flags);                       \
    HRESULT(STDMETHODCALLTYPE *SetSourceRect)(T * This, const RECT *rect);                                             \
    HRESULT(STDMETHODCALLTYPE *SetTargetRect)(T * This, const RECT *rect);                                             \
    HRESULT(STDMETHODCALLTYPE *SetDestSize)(T * This, UINT width, UINT height);                                        \
    HRESULT(STDMETHODCALLTYPE *GetSourceRect)(T * This, RECT * rect);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetTargetRect)(T * This, RECT * rect);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetDestSize)(T * This, UINT * width, UINT * height);                                    \
    HRESULT(STDMETHODCALLTYPE *SetColorSpace)(T * This, DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAGS space);                    \
    DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAGS(STDMETHODCALLTYPE *GetColorSpace)(T * This);

#define IDXGISwapChainMedia_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFrameStatisticsMedia)(T * This, DXGI_FRAME_STATISTICS_MEDIA * stats);                \
    HRESULT(STDMETHODCALLTYPE *SetPresentDuration)(T * This, UINT duration);                                           \
    HRESULT(STDMETHODCALLTYPE *CheckPresentDurationSupport)(T * This, UINT desired, UINT * closest_smaller,            \
                                                            UINT * closest_larger);

#define IDXGIFactoryMedia_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSwapChainForCompositionSurfaceHandle)(                                            \
        T * This, IUnknown * device, HANDLE surface, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_to,      \
        IDXGISwapChain1 **swapchain);                                                                                  \
    HRESULT(STDMETHODCALLTYPE *CreateDecodeSwapChainForCompositionSurfaceHandle)(                                      \
        T * This, IUnknown * device, HANDLE surface, DXGI_DECODE_SWAP_CHAIN_DESC *desc, IDXGIResource *yuv_buffers,    \
        IDXGIOutput *restrict_to, IDXGIDecodeSwapChain **swapchain);

#define IDXGIDevice_METHODS(T)                                                                                         \
    IDXGIObject_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *GetAdapter)(T * This, IDXGIAdapter * *adapter);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateSurface)(T * This, const DXGI_SURFACE_DESC *desc, UINT count, DXGI_USAGE usage,   \
                                              const DXGI_SHARED_RESOURCE *shared, IDXGISurface **surfaces);            \
    HRESULT(STDMETHODCALLTYPE *QueryResourceResidency)(T * This, IUnknown *const *resources,                           \
                                                       DXGI_RESIDENCY *status, UINT count);                            \
    HRESULT(STDMETHODCALLTYPE *SetGPUThreadPriority)(T * This, INT priority);                                          \
    HRESULT(STDMETHODCALLTYPE *GetGPUThreadPriority)(T * This, INT * priority);

#define IDXGIDevice1_METHODS(T)                                                                                        \
    IDXGIDevice_METHODS(T)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *SetMaximumFrameLatency)(T * This, UINT latency);                                        \
    HRESULT(STDMETHODCALLTYPE *GetMaximumFrameLatency)(T * This, UINT * latency);

#define IDXGIDevice2_METHODS(T)                                                                                        \
    IDXGIDevice1_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *OfferResources)(T * This, UINT count, IDXGIResource *const *resources,                  \
                                               DXGI_OFFER_RESOURCE_PRIORITY priority);                                 \
    HRESULT(STDMETHODCALLTYPE *ReclaimResources)(T * This, UINT count, IDXGIResource *const *resources,                \
                                                 BOOL *discarded);                                                     \
    HRESULT(STDMETHODCALLTYPE *EnqueueSetEvent)(T * This, HANDLE event);

#define IDXGIDevice3_METHODS(T)                                                                                        \
    IDXGIDevice2_METHODS(T)                                                                                            \
    void(STDMETHODCALLTYPE *Trim)(T * This);

#define IDXGIDevice4_METHODS(T)                                                                                        \
    IDXGIDevice3_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *OfferResources1)(T * This, UINT count, IDXGIResource *const *resources,                 \
                                                DXGI_OFFER_RESOURCE_PRIORITY priority, UINT flags);                    \
    HRESULT(STDMETHODCALLTYPE *ReclaimResources1)(T * This, UINT count, IDXGIResource *const *resources,               \
                                                  UINT *results);

MIME_COM_INTERFACE(IDXGIObject)
MIME_COM_INTERFACE(IDXGIDeviceSubObject)
MIME_COM_INTERFACE(IDXGIResource)
MIME_COM_INTERFACE(IDXGIResource1)
MIME_COM_INTERFACE(IDXGIKeyedMutex)
MIME_COM_INTERFACE(IDXGISurface)
MIME_COM_INTERFACE(IDXGISurface1)
MIME_COM_INTERFACE(IDXGISurface2)
MIME_COM_INTERFACE(IDXGIAdapter)
MIME_COM_INTERFACE(IDXGIAdapter1)
MIME_COM_INTERFACE(IDXGIAdapter2)
MIME_COM_INTERFACE(IDXGIAdapter3)
MIME_COM_INTERFACE(IDXGIAdapter4)
MIME_COM_INTERFACE(IDXGIOutput)
MIME_COM_INTERFACE(IDXGIOutput1)
MIME_COM_INTERFACE(IDXGIOutput2)
MIME_COM_INTERFACE(IDXGIOutput3)
MIME_COM_INTERFACE(IDXGIOutput4)
MIME_COM_INTERFACE(IDXGIOutput5)
MIME_COM_INTERFACE(IDXGIOutput6)
MIME_COM_INTERFACE(IDXGISwapChain)
MIME_COM_INTERFACE(IDXGISwapChain1)
MIME_COM_INTERFACE(IDXGISwapChain2)
MIME_COM_INTERFACE(IDXGISwapChain3)
MIME_COM_INTERFACE(IDXGISwapChain4)
MIME_COM_INTERFACE(IDXGIFactory)
MIME_COM_INTERFACE(IDXGIFactory1)
MIME_COM_INTERFACE(IDXGIFactory2)
MIME_COM_INTERFACE(IDXGIFactory3)
MIME_COM_INTERFACE(IDXGIFactory4)
MIME_COM_INTERFACE(IDXGIFactory5)
MIME_COM_INTERFACE(IDXGIFactory6)
MIME_COM_INTERFACE(IDXGIFactory7)
MIME_COM_INTERFACE(IDXGIFactoryMedia)
MIME_COM_INTERFACE(IDXGIDecodeSwapChain)
MIME_COM_INTERFACE(IDXGISwapChainMedia)
MIME_COM_INTERFACE(IDXGIDevice)
MIME_COM_INTERFACE(IDXGIDevice1)
MIME_COM_INTERFACE(IDXGIDevice2)
MIME_COM_INTERFACE(IDXGIDevice3)
MIME_COM_INTERFACE(IDXGIDevice4)

MIME_DEFINE_GUID(IID_IDXGIObject, 0xAEC22FB8, 0x76F3, 0x4639, 0x9B, 0xE0, 0x28, 0xEB, 0x43, 0xA6, 0x7A, 0x2E);
MIME_DEFINE_GUID(IID_IDXGIDeviceSubObject, 0x3D3E0379, 0xF9DE, 0x4D58, 0xBB, 0x6C, 0x18, 0xD6, 0x29, 0x92, 0xF1, 0xA6);
MIME_DEFINE_GUID(IID_IDXGIResource, 0x035F3AB4, 0x482E, 0x4E50, 0xB4, 0x1F, 0x8A, 0x7F, 0x8B, 0xD8, 0x96, 0x0B);
MIME_DEFINE_GUID(IID_IDXGIKeyedMutex, 0x9D8E1289, 0xD7B3, 0x465F, 0x81, 0x26, 0x25, 0x0E, 0x34, 0x9A, 0xF8, 0x5D);
MIME_DEFINE_GUID(IID_IDXGISurface, 0xCAFCB56C, 0x6AC3, 0x4889, 0xBF, 0x47, 0x9E, 0x23, 0xBB, 0xD2, 0x60, 0xEC);
MIME_DEFINE_GUID(IID_IDXGISurface1, 0x4AE63092, 0x6327, 0x4C1B, 0x80, 0xAE, 0xBF, 0xE1, 0x2E, 0xA3, 0x2B, 0x86);
MIME_DEFINE_GUID(IID_IDXGIAdapter, 0x2411E7E1, 0x12AC, 0x4CCF, 0xBD, 0x14, 0x97, 0x98, 0xE8, 0x53, 0x4D, 0xC0);
MIME_DEFINE_GUID(IID_IDXGIOutput, 0xAE02EEDB, 0xC735, 0x4690, 0x8D, 0x52, 0x5A, 0x8D, 0xC2, 0x02, 0x13, 0xAA);
MIME_DEFINE_GUID(IID_IDXGISwapChain, 0x310D36A0, 0xD2E7, 0x4C0A, 0xAA, 0x04, 0x6A, 0x9D, 0x23, 0xB8, 0x88, 0x6A);
MIME_DEFINE_GUID(IID_IDXGIFactory, 0x7B7166EC, 0x21C7, 0x44AE, 0xB2, 0x1A, 0xC9, 0xAE, 0x32, 0x1A, 0xE3, 0x69);
MIME_DEFINE_GUID(IID_IDXGIDevice, 0x54EC77FA, 0x1377, 0x44E6, 0x8C, 0x32, 0x88, 0xFD, 0x5F, 0x44, 0xC8, 0x4C);
MIME_DEFINE_GUID(IID_IDXGIFactory1, 0x770AAE78, 0xF26F, 0x4DBA, 0xA8, 0x29, 0x25, 0x3C, 0x83, 0xD1, 0xB3, 0x87);
MIME_DEFINE_GUID(IID_IDXGIAdapter1, 0x29038F61, 0x3839, 0x4626, 0x91, 0xFD, 0x08, 0x68, 0x79, 0x01, 0x1A, 0x05);
MIME_DEFINE_GUID(IID_IDXGIDevice1, 0x77DB970F, 0x6276, 0x48BA, 0xBA, 0x28, 0x07, 0x01, 0x43, 0xB4, 0x39, 0x2C);
MIME_DEFINE_GUID(IID_IDXGIOutputDuplication, 0x191CFAC3, 0xA341, 0x470D, 0xB2, 0x6E, 0xA8, 0x64, 0xF4, 0x28, 0x31, 0x9C);
MIME_DEFINE_GUID(IID_IDXGISurface2, 0xABA496DD, 0xB617, 0x4CB8, 0xA8, 0x66, 0xBC, 0x44, 0xD7, 0xEB, 0x1F, 0xA2);
MIME_DEFINE_GUID(IID_IDXGIResource1, 0x30961379, 0x4609, 0x4A41, 0x99, 0x8E, 0x54, 0xFE, 0x56, 0x7E, 0xE0, 0xC1);
MIME_DEFINE_GUID(IID_IDXGIDevice2, 0x05008617, 0xFBFD, 0x4051, 0xA7, 0x90, 0x14, 0x48, 0x84, 0xB4, 0xF6, 0xA9);
MIME_DEFINE_GUID(IID_IDXGISwapChain1, 0x790A45F7, 0x0D42, 0x4876, 0x98, 0x3A, 0x0A, 0x55, 0xCF, 0xE6, 0xF4, 0xAA);
MIME_DEFINE_GUID(IID_IDXGIFactory2, 0x50C83A1C, 0xE072, 0x4C48, 0x87, 0xB0, 0x36, 0x30, 0xFA, 0x36, 0xA6, 0xD0);
MIME_DEFINE_GUID(IID_IDXGIAdapter2, 0x0AA1AE0A, 0xFA0E, 0x4B84, 0x86, 0x44, 0xE0, 0x5F, 0xF8, 0xE5, 0xAC, 0xB5);
MIME_DEFINE_GUID(IID_IDXGIOutput1, 0x00CDDEA8, 0x939B, 0x4B83, 0xA3, 0x40, 0xA6, 0x85, 0x22, 0x66, 0x66, 0xCC);
MIME_DEFINE_GUID(IID_IDXGIDevice3, 0x6007896C, 0x3244, 0x4AFD, 0xBF, 0x18, 0xA6, 0xD3, 0xBE, 0xDA, 0x50, 0x23);
MIME_DEFINE_GUID(IID_IDXGISwapChain2, 0xA8BE2AC4, 0x199F, 0x4946, 0xB3, 0x31, 0x79, 0x59, 0x9F, 0xB9, 0x8D, 0xE7);
MIME_DEFINE_GUID(IID_IDXGIOutput2, 0x595E39D1, 0x2724, 0x4663, 0x99, 0xB1, 0xDA, 0x96, 0x9D, 0xE2, 0x83, 0x64);
MIME_DEFINE_GUID(IID_IDXGIFactory3, 0x25483823, 0xCD46, 0x4C7D, 0x86, 0xCA, 0x47, 0xAA, 0x95, 0xB8, 0x37, 0xBD);
MIME_DEFINE_GUID(IID_IDXGIOutput3, 0x8A6BB301, 0x7E7E, 0x41F4, 0xA8, 0xE0, 0x5B, 0x32, 0xF7, 0xF9, 0x9B, 0x18);
MIME_DEFINE_GUID(IID_IDXGISwapChain3, 0x94D99BDB, 0xF1F8, 0x4AB0, 0xB2, 0x36, 0x7D, 0xA0, 0x17, 0x0E, 0xDA, 0xB1);
MIME_DEFINE_GUID(IID_IDXGIOutput4, 0xDC7DCA35, 0x2196, 0x414D, 0x9F, 0x53, 0x61, 0x78, 0x84, 0x03, 0x2A, 0x60);
MIME_DEFINE_GUID(IID_IDXGIFactory4, 0x1BC6EA02, 0xEF36, 0x464F, 0xBF, 0x0C, 0x21, 0xCA, 0x39, 0xE5, 0x16, 0x8A);
MIME_DEFINE_GUID(IID_IDXGIAdapter3, 0x645967A4, 0x1392, 0x4310, 0xA7, 0x98, 0x80, 0x53, 0xCE, 0x3E, 0x93, 0xFD);
MIME_DEFINE_GUID(IID_IDXGIOutput5, 0x80A07424, 0xAB52, 0x42EB, 0x83, 0x3C, 0x0C, 0x42, 0xFD, 0x28, 0x2D, 0x98);
MIME_DEFINE_GUID(IID_IDXGISwapChain4, 0x3D585D5A, 0xBD4A, 0x489E, 0xB1, 0xF4, 0x3D, 0xBC, 0xB6, 0x45, 0x2F, 0xFB);
MIME_DEFINE_GUID(IID_IDXGIDevice4, 0x95B4F95F, 0xD8DA, 0x4CA4, 0x9E, 0xE6, 0x3B, 0x76, 0xD5, 0x96, 0x8A, 0x10);
MIME_DEFINE_GUID(IID_IDXGIFactory5, 0x7632E1F5, 0xEE65, 0x4DCA, 0x87, 0xFD, 0x84, 0xCD, 0x75, 0xF8, 0x83, 0x8D);
MIME_DEFINE_GUID(IID_IDXGIAdapter4, 0x3C8D99D1, 0x4FBF, 0x4181, 0xA8, 0x2C, 0xAF, 0x66, 0xBF, 0x7B, 0xD2, 0x4E);
MIME_DEFINE_GUID(IID_IDXGIOutput6, 0x068346E8, 0xAAEC, 0x4B84, 0xAD, 0xD7, 0x13, 0x7F, 0x51, 0x3F, 0x77, 0xA1);
MIME_DEFINE_GUID(IID_IDXGIFactory6, 0xC1B6694F, 0xFF09, 0x44A9, 0xB0, 0x3C, 0x77, 0x90, 0x0A, 0x0A, 0x1D, 0x17);
MIME_DEFINE_GUID(IID_IDXGIFactory7, 0xA4966EED, 0x76DB, 0x44DA, 0x84, 0xC1, 0xEE, 0x9A, 0x7A, 0xFB, 0x20, 0xA8);
MIME_DEFINE_GUID(IID_IDXGIFactoryMedia, 0x41E7D1F2, 0xA591, 0x4F7B, 0xA2, 0xE5, 0xFA, 0x9C, 0x84, 0x3E, 0x1C, 0x12);
MIME_DEFINE_GUID(IID_IDXGIDecodeSwapChain, 0x2633066B, 0x4514, 0x4C7A, 0x8F, 0xD8, 0x12, 0xEA, 0x98, 0x05, 0x9D, 0x18);
MIME_DEFINE_GUID(IID_IDXGISwapChainMedia, 0xDD95B90B, 0xF05F, 0x4F6A, 0xBD, 0x65, 0x25, 0xBF, 0xB2, 0x64, 0xBD, 0x84);

#ifndef MIME_BUILD_DXGI
#define DXGIAPI DECLSPEC_IMPORT
#else
#define DXGIAPI DECLSPEC_EXPORT
#endif

DXGIAPI HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory);
DXGIAPI HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory);
DXGIAPI HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
DXGIAPI HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void **debug);
DXGIAPI HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void);

#endif
