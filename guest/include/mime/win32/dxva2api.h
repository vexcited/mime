/* the DirectX video acceleration 2 services. */
#ifndef MIME_WIN32_DXVA2API_H
#define MIME_WIN32_DXVA2API_H

#include <mime/win32/d3d9.h>

#ifndef MIME_REFERENCE_TIME_DEFINED
#define MIME_REFERENCE_TIME_DEFINED
typedef LONGLONG REFERENCE_TIME;
#endif

typedef struct _DXVA2_ExtendedFormat {
    union {
        struct {
            UINT SampleFormat : 8;
            UINT VideoChromaSubsampling : 4;
            UINT NominalRange : 3;
            UINT VideoTransferMatrix : 3;
            UINT VideoLighting : 4;
            UINT VideoPrimaries : 5;
            UINT VideoTransferFunction : 5;
        };
        UINT value;
    };
} DXVA2_ExtendedFormat;

#define DXVA2_SampleUnknown 0
#define DXVA2_SampleProgressiveFrame 2
#define DXVA2_SampleFieldInterleavedEvenFirst 3
#define DXVA2_SampleFieldInterleavedOddFirst 4
#define DXVA2_SampleFieldSingleEven 5
#define DXVA2_SampleFieldSingleOdd 6
#define DXVA2_SampleSubStream 7
#define DXVA2_NominalRange_Unknown 0
#define DXVA2_NominalRange_0_255 1
#define DXVA2_NominalRange_16_235 2
#define DXVA2_NominalRange_48_208 3
#define DXVA2_VideoTransferMatrix_Unknown 0
#define DXVA2_VideoTransferMatrix_BT709 1
#define DXVA2_VideoTransferMatrix_BT601 2
#define DXVA2_VideoTransferMatrix_SMPTE240M 3

typedef struct _DXVA2_Frequency {
    UINT Numerator;
    UINT Denominator;
} DXVA2_Frequency;

typedef struct _DXVA2_VideoDesc {
    UINT SampleWidth;
    UINT SampleHeight;
    DXVA2_ExtendedFormat SampleFormat;
    D3DFORMAT Format;
    DXVA2_Frequency InputSampleFreq;
    DXVA2_Frequency OutputFrameFreq;
    UINT UABProtectionLevel;
    UINT Reserved;
} DXVA2_VideoDesc;

typedef struct _DXVA2_VideoProcessorCaps {
    UINT DeviceCaps;
    D3DPOOL InputPool;
    UINT NumForwardRefSamples;
    UINT NumBackwardRefSamples;
    UINT Reserved;
    UINT DeinterlaceTechnology;
    UINT ProcAmpControlCaps;
    UINT VideoProcessorOperations;
    UINT NoiseFilterTechnology;
    UINT DetailFilterTechnology;
} DXVA2_VideoProcessorCaps;

typedef struct _DXVA2_Fixed32 {
    union {
        struct {
            USHORT Fraction;
            SHORT Value;
        };
        LONG ll;
    };
} DXVA2_Fixed32;

typedef struct _DXVA2_ValueRange {
    DXVA2_Fixed32 MinValue, MaxValue, DefaultValue, StepSize;
} DXVA2_ValueRange;

typedef struct _DXVA2_ProcAmpValues {
    DXVA2_Fixed32 Brightness, Contrast, Hue, Saturation;
} DXVA2_ProcAmpValues;

typedef struct _DXVA2_AYUVSample16 {
    USHORT Cr, Cb, Y, Alpha;
} DXVA2_AYUVSample16;

typedef struct _DXVA2_AYUVSample8 {
    UCHAR Cr, Cb, Y, Alpha;
} DXVA2_AYUVSample8;

typedef struct _DXVA2_FilterValues {
    DXVA2_Fixed32 Level, Threshold, Radius;
} DXVA2_FilterValues;

typedef struct _DXVA2_VideoProcessBltParams {
    REFERENCE_TIME TargetFrame;
    RECT TargetRect;
    SIZE ConstrictionSize;
    UINT StreamingFlags;
    DXVA2_AYUVSample16 BackgroundColor;
    DXVA2_ExtendedFormat DestFormat;
    DXVA2_ProcAmpValues ProcAmpValues;
    DXVA2_Fixed32 Alpha;
    DXVA2_FilterValues NoiseFilterLuma, NoiseFilterChroma, DetailFilterLuma, DetailFilterChroma;
    DWORD DestData;
} DXVA2_VideoProcessBltParams;

typedef struct _DXVA2_VideoSample {
    REFERENCE_TIME Start;
    REFERENCE_TIME End;
    DXVA2_ExtendedFormat SampleFormat;
    IDirect3DSurface9 *SrcSurface;
    RECT SrcRect;
    RECT DstRect;
    DXVA2_AYUVSample8 Pal[16];
    DXVA2_Fixed32 PlanarAlpha;
    DWORD SampleData;
} DXVA2_VideoSample;

#define DXVA2_VideoDecoderRenderTarget 0
#define DXVA2_VideoProcessorRenderTarget 1
#define DXVA2_VideoSoftwareRenderTarget 2

#define DXVA2_VPDev_HardwareDevice 0x1
#define DXVA2_VPDev_EmulatedDXVA1 0x2
#define DXVA2_VPDev_SoftwareDevice 0x4

#define DXVA2_ProcAmp_None 0
#define DXVA2_ProcAmp_Brightness 0x1
#define DXVA2_ProcAmp_Contrast 0x2
#define DXVA2_ProcAmp_Hue 0x4
#define DXVA2_ProcAmp_Saturation 0x8

#define DXVA2_VideoProcess_YUV2RGB 0x1
#define DXVA2_VideoProcess_StretchX 0x2
#define DXVA2_VideoProcess_StretchY 0x4
#define DXVA2_VideoProcess_AlphaBlend 0x8
#define DXVA2_VideoProcess_SubRects 0x10
#define DXVA2_VideoProcess_SubStreams 0x20
#define DXVA2_VideoProcess_SubStreamsExtended 0x40
#define DXVA2_VideoProcess_YUV2RGBExtended 0x80
#define DXVA2_VideoProcess_AlphaBlendExtended 0x100
#define DXVA2_VideoProcess_Constriction 0x200
#define DXVA2_VideoProcess_NoiseFilter 0x400
#define DXVA2_VideoProcess_DetailFilter 0x800
#define DXVA2_VideoProcess_PlanarAlpha 0x1000
#define DXVA2_VideoProcess_LinearScaling 0x2000
#define DXVA2_VideoProcess_GammaCompensated 0x4000
#define DXVA2_VideoProcess_MaintainsOriginalFieldData 0x8000

typedef struct IDirectXVideoAccelerationService IDirectXVideoAccelerationService;
typedef struct IDirectXVideoProcessorService IDirectXVideoProcessorService;
typedef struct IDirectXVideoDecoderService IDirectXVideoDecoderService;
typedef struct IDirectXVideoProcessor IDirectXVideoProcessor;

#define IDirectXVideoAccelerationService_METHODS(T)                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSurface)(T * This, UINT width, UINT height, UINT back_buffers, D3DFORMAT format,  \
                                              D3DPOOL pool, DWORD usage, DWORD type, IDirect3DSurface9 **surfaces,     \
                                              HANDLE *shared);

#define IDirectXVideoProcessorService_METHODS(T)                                                                       \
    IDirectXVideoAccelerationService_METHODS(T)                                                                        \
    HRESULT(STDMETHODCALLTYPE *RegisterVideoProcessorSoftwareDevice)(T * This, void *callbacks);                      \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorDeviceGuids)(T * This, const DXVA2_VideoDesc *desc, UINT *count,      \
                                                             GUID **guids);                                            \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorRenderTargets)(T * This, REFGUID device, const DXVA2_VideoDesc *desc, \
                                                               UINT *count, D3DFORMAT **formats);                      \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorSubStreamFormats)(T * This, REFGUID device,                           \
                                                                  const DXVA2_VideoDesc *desc, D3DFORMAT target,       \
                                                                  UINT *count, D3DFORMAT **formats);                   \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorCaps)(T * This, REFGUID device, const DXVA2_VideoDesc *desc,          \
                                                      D3DFORMAT target, DXVA2_VideoProcessorCaps *caps);               \
    HRESULT(STDMETHODCALLTYPE *GetProcAmpRange)(T * This, REFGUID device, const DXVA2_VideoDesc *desc,                \
                                                D3DFORMAT target, UINT property, DXVA2_ValueRange *range);             \
    HRESULT(STDMETHODCALLTYPE *GetFilterPropertyRange)(T * This, REFGUID device, const DXVA2_VideoDesc *desc,         \
                                                       D3DFORMAT target, UINT setting, DXVA2_ValueRange *range);       \
    HRESULT(STDMETHODCALLTYPE *CreateVideoProcessor)(T * This, REFGUID device, const DXVA2_VideoDesc *desc,           \
                                                     D3DFORMAT target, UINT substreams,                                \
                                                     IDirectXVideoProcessor **processor);

#define IDirectXVideoDecoderService_METHODS(T)                                                                         \
    IDirectXVideoAccelerationService_METHODS(T)                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDecoderDeviceGuids)(T * This, UINT *count, GUID **guids);                            \
    HRESULT(STDMETHODCALLTYPE *GetDecoderRenderTargets)(T * This, REFGUID guid, UINT *count, D3DFORMAT **formats);    \
    HRESULT(STDMETHODCALLTYPE *GetDecoderConfigurations)(T * This, REFGUID guid, const DXVA2_VideoDesc *desc,         \
                                                         void *reserved, UINT *count, void **configs);                 \
    HRESULT(STDMETHODCALLTYPE *CreateVideoDecoder)(T * This, REFGUID guid, const DXVA2_VideoDesc *desc,               \
                                                   const void *config, IDirect3DSurface9 **targets, UINT count,         \
                                                   void **decoder);

#define IDirectXVideoProcessor_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorService)(T * This, IDirectXVideoProcessorService **service);           \
    HRESULT(STDMETHODCALLTYPE *GetCreationParameters)(T * This, GUID *device, DXVA2_VideoDesc *desc,                  \
                                                      D3DFORMAT *target, UINT *substreams);                            \
    HRESULT(STDMETHODCALLTYPE *GetVideoProcessorCaps)(T * This, DXVA2_VideoProcessorCaps *caps);                       \
    HRESULT(STDMETHODCALLTYPE *GetProcAmpRange)(T * This, UINT property, DXVA2_ValueRange *range);                     \
    HRESULT(STDMETHODCALLTYPE *GetFilterPropertyRange)(T * This, UINT setting, DXVA2_ValueRange *range);               \
    HRESULT(STDMETHODCALLTYPE *VideoProcessBlt)(T * This, IDirect3DSurface9 *target,                                  \
                                                const DXVA2_VideoProcessBltParams *params,                             \
                                                const DXVA2_VideoSample *samples, UINT count, HANDLE *complete);

MIME_COM_INTERFACE(IDirectXVideoAccelerationService)
MIME_COM_INTERFACE(IDirectXVideoProcessorService)
MIME_COM_INTERFACE(IDirectXVideoDecoderService)
MIME_COM_INTERFACE(IDirectXVideoProcessor)

MIME_DEFINE_GUID(IID_IDirectXVideoAccelerationService, 0xFC51A550, 0xD5E7, 0x11D9, 0xAF, 0x55, 0x00, 0x05, 0x4E, 0x43,
                 0xFF, 0x02);
MIME_DEFINE_GUID(IID_IDirectXVideoDecoderService, 0xFC51A551, 0xD5E7, 0x11D9, 0xAF, 0x55, 0x00, 0x05, 0x4E, 0x43, 0xFF,
                 0x02);
MIME_DEFINE_GUID(IID_IDirectXVideoProcessorService, 0xFC51A552, 0xD5E7, 0x11D9, 0xAF, 0x55, 0x00, 0x05, 0x4E, 0x43, 0xFF,
                 0x02);
MIME_DEFINE_GUID(IID_IDirectXVideoProcessor, 0x8C3A39F0, 0x916E, 0x4690, 0x80, 0x4F, 0x4C, 0x80, 0x01, 0x35, 0x5D, 0x25);
MIME_DEFINE_GUID(DXVA2_VideoProcProgressiveDevice, 0x5A54A0C9, 0xC7EC, 0x4BD9, 0x8E, 0xDE, 0xF3, 0xC7, 0x5D, 0xC4, 0x39,
                 0x3B);
MIME_DEFINE_GUID(DXVA2_VideoProcBobDevice, 0x335AA36E, 0x7884, 0x43A4, 0x9C, 0x91, 0x7F, 0x87, 0xFA, 0xEF, 0x7E, 0x91);
MIME_DEFINE_GUID(DXVA2_VideoProcSoftwareDevice, 0x4553D47F, 0xEE7E, 0x4E3F, 0x94, 0x75, 0xDB, 0xF1, 0x37, 0x6C, 0x48,
                 0x10);

#ifdef MIME_BUILD_DXVA2
#define DXVA2API
#else
#define DXVA2API DECLSPEC_IMPORT
#endif

typedef struct IDirect3DDeviceManager9 IDirect3DDeviceManager9;
DXVA2API HRESULT WINAPI DXVA2CreateDirect3DDeviceManager9(UINT *token, IDirect3DDeviceManager9 **manager);
DXVA2API HRESULT WINAPI DXVA2CreateVideoService(IDirect3DDevice9 *device, REFIID riid, void **service);

#endif
