/*
 * windows runtime screen capture: Windows.Graphics.Capture, the Direct3D11 wrappers it uses and the interop
 * interfaces. method lists are macros so implementations and callers share the layouts.
 */
#ifndef MIME_WIN32_WGC_H
#define MIME_WIN32_WGC_H

#include <mime/win32/roapi.h>

#define MIME_INSPECTABLE_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetIids)(T * This, ULONG * count, IID * *iids);                                         \
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(T * This, HSTRING * name);                                         \
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(T * This, TrustLevel * level);

typedef struct SizeInt32 {
    INT32 Width, Height;
} SizeInt32;

typedef struct TimeSpan {
    INT64 Duration; /* 100 nanosecond units. */
} TimeSpan;

typedef struct EventRegistrationToken {
    INT64 value;
} EventRegistrationToken;

/* DirectXPixelFormat holds DXGI_FORMAT values. */
typedef INT32 DirectXPixelFormat;
#define DirectXPixelFormat_B8G8R8A8UIntNormalized 87

typedef struct Direct3DMultisampleDescription {
    INT32 Count, Quality;
} Direct3DMultisampleDescription;

typedef struct Direct3DSurfaceDescription {
    INT32 Width, Height;
    DirectXPixelFormat Format;
    Direct3DMultisampleDescription MultisampleDescription;
} Direct3DSurfaceDescription;

typedef struct IClosable IClosable;
#define IClosable_METHODS(T)                                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(IClosable)

typedef struct IDirect3DDevice IDirect3DDevice;
#define IDirect3DDevice_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Trim)(T * This);
MIME_COM_INTERFACE(IDirect3DDevice)

typedef struct IDirect3DSurface IDirect3DSurface;
#define IDirect3DSurface_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Description)(T * This, Direct3DSurfaceDescription * description);
MIME_COM_INTERFACE(IDirect3DSurface)

/* not a runtime interface: reaches the Direct3D object behind a wrapper. */
typedef struct IDirect3DDxgiInterfaceAccess IDirect3DDxgiInterfaceAccess;
#define IDirect3DDxgiInterfaceAccess_METHODS(T)                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetInterface)(T * This, REFIID riid, void **object);
MIME_COM_INTERFACE(IDirect3DDxgiInterfaceAccess)

typedef struct ITypedEventHandler ITypedEventHandler;
#define ITypedEventHandler_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IInspectable * sender, IInspectable * args);
MIME_COM_INTERFACE(ITypedEventHandler)

typedef struct IGraphicsCaptureItem IGraphicsCaptureItem;
#define IGraphicsCaptureItem_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_DisplayName)(T * This, HSTRING * value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, SizeInt32 * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *add_Closed)(T * This, ITypedEventHandler * handler, EventRegistrationToken * token);    \
    HRESULT(STDMETHODCALLTYPE *remove_Closed)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IGraphicsCaptureItem)

typedef struct IGraphicsCaptureItemStatics IGraphicsCaptureItemStatics;
#define IGraphicsCaptureItemStatics_METHODS(T)                                                                         \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateFromVisual)(T * This, IInspectable * visual, IGraphicsCaptureItem * *result);
MIME_COM_INTERFACE(IGraphicsCaptureItemStatics)

typedef struct IGraphicsCaptureItemInterop IGraphicsCaptureItemInterop;
#define IGraphicsCaptureItemInterop_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateForWindow)(T * This, HWND window, REFIID riid, void **result);                    \
    HRESULT(STDMETHODCALLTYPE *CreateForMonitor)(T * This, HMONITOR monitor, REFIID riid, void **result);
MIME_COM_INTERFACE(IGraphicsCaptureItemInterop)

typedef struct IGraphicsCaptureSession IGraphicsCaptureSession;
#define IGraphicsCaptureSession_METHODS(T)                                                                             \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *StartCapture)(T * This);
MIME_COM_INTERFACE(IGraphicsCaptureSession)

typedef struct IGraphicsCaptureSession2 IGraphicsCaptureSession2;
#define IGraphicsCaptureSession2_METHODS(T)                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_IsCursorCaptureEnabled)(T * This, BOOLEAN * value);                                 \
    HRESULT(STDMETHODCALLTYPE *put_IsCursorCaptureEnabled)(T * This, BOOLEAN value);
MIME_COM_INTERFACE(IGraphicsCaptureSession2)

typedef struct IGraphicsCaptureSession3 IGraphicsCaptureSession3;
#define IGraphicsCaptureSession3_METHODS(T)                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_IsBorderRequired)(T * This, BOOLEAN * value);                                       \
    HRESULT(STDMETHODCALLTYPE *put_IsBorderRequired)(T * This, BOOLEAN value);
MIME_COM_INTERFACE(IGraphicsCaptureSession3)

typedef struct IGraphicsCaptureSessionStatics IGraphicsCaptureSessionStatics;
#define IGraphicsCaptureSessionStatics_METHODS(T)                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *IsSupported)(T * This, BOOLEAN * result);
MIME_COM_INTERFACE(IGraphicsCaptureSessionStatics)

typedef struct IDirect3D11CaptureFrame IDirect3D11CaptureFrame;
#define IDirect3D11CaptureFrame_METHODS(T)                                                                             \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Surface)(T * This, IDirect3DSurface * *value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_SystemRelativeTime)(T * This, TimeSpan * value);                                    \
    HRESULT(STDMETHODCALLTYPE *get_ContentSize)(T * This, SizeInt32 * value);
MIME_COM_INTERFACE(IDirect3D11CaptureFrame)

typedef struct IDirect3D11CaptureFramePool IDirect3D11CaptureFramePool;
#define IDirect3D11CaptureFramePool_METHODS(T)                                                                         \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Recreate)(T * This, IDirect3DDevice * device, DirectXPixelFormat format,                \
                                         INT32 buffers, SizeInt32 size);                                               \
    HRESULT(STDMETHODCALLTYPE *TryGetNextFrame)(T * This, IDirect3D11CaptureFrame * *result);                          \
    HRESULT(STDMETHODCALLTYPE *add_FrameArrived)(T * This, ITypedEventHandler * handler,                               \
                                                 EventRegistrationToken * token);                                      \
    HRESULT(STDMETHODCALLTYPE *remove_FrameArrived)(T * This, EventRegistrationToken token);                           \
    HRESULT(STDMETHODCALLTYPE *CreateCaptureSession)(T * This, IGraphicsCaptureItem * item,                            \
                                                     IGraphicsCaptureSession * *result);                               \
    HRESULT(STDMETHODCALLTYPE *get_DispatcherQueue)(T * This, IInspectable * *value);
MIME_COM_INTERFACE(IDirect3D11CaptureFramePool)

typedef struct IDirect3D11CaptureFramePoolStatics IDirect3D11CaptureFramePoolStatics;
#define IDirect3D11CaptureFramePoolStatics_METHODS(T)                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Create)(T * This, IDirect3DDevice * device, DirectXPixelFormat format, INT32 buffers,   \
                                       SizeInt32 size, IDirect3D11CaptureFramePool * *result);
MIME_COM_INTERFACE(IDirect3D11CaptureFramePoolStatics)

typedef struct IDirect3D11CaptureFramePoolStatics2 IDirect3D11CaptureFramePoolStatics2;
#define IDirect3D11CaptureFramePoolStatics2_METHODS(T)                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateFreeThreaded)(T * This, IDirect3DDevice * device, DirectXPixelFormat format,      \
                                                   INT32 buffers, SizeInt32 size, IDirect3D11CaptureFramePool * *result);
MIME_COM_INTERFACE(IDirect3D11CaptureFramePoolStatics2)

MIME_DEFINE_GUID(IID_IClosable, 0x30D5A829, 0x7FA4, 0x4026, 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E);
MIME_DEFINE_GUID(IID_IDirect3DDevice, 0xA37624AB, 0x8D5F, 0x4650, 0x9D, 0x3E, 0x9E, 0xAE, 0x3D, 0x9B, 0xC6, 0x70);
MIME_DEFINE_GUID(IID_IDirect3DSurface, 0x0BF4A146, 0x13C1, 0x4694, 0xBE, 0xE3, 0x7A, 0xBF, 0x15, 0xEA, 0xF5, 0x86);
MIME_DEFINE_GUID(IID_IDirect3DDxgiInterfaceAccess, 0xA9B3D012, 0x3DF2, 0x4EE3, 0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3,
                 0xC1);
MIME_DEFINE_GUID(IID_IGraphicsCaptureItem, 0x79C3F95B, 0x31F7, 0x4EC2, 0xA4, 0x64, 0x63, 0x2E, 0xF5, 0xD3, 0x07, 0x60);
MIME_DEFINE_GUID(IID_IGraphicsCaptureItemStatics, 0xA87EBEA5, 0x457C, 0x5788, 0xAB, 0x47, 0x0C, 0xF1, 0xD3, 0x63, 0x7E,
                 0x74);
MIME_DEFINE_GUID(IID_IGraphicsCaptureItemInterop, 0x3628E81B, 0x3CAC, 0x4C60, 0xB7, 0xF4, 0x23, 0xCE, 0x0E, 0x0C, 0x33,
                 0x56);
MIME_DEFINE_GUID(IID_IGraphicsCaptureSession, 0x814E42A9, 0xF70F, 0x4AD7, 0x93, 0x9B, 0xFD, 0xDC, 0xC6, 0xEB, 0x88, 0x0D);
MIME_DEFINE_GUID(IID_IGraphicsCaptureSession2, 0x2C39AE40, 0x7D2E, 0x5044, 0x80, 0x4E, 0x8B, 0x67, 0x99, 0xD4, 0xCF,
                 0x9E);
MIME_DEFINE_GUID(IID_IGraphicsCaptureSession3, 0xF2CDD966, 0x22AE, 0x5EA1, 0x95, 0x96, 0x3A, 0x28, 0x93, 0x44, 0xC3,
                 0xBE);
MIME_DEFINE_GUID(IID_IGraphicsCaptureSessionStatics, 0x2224A540, 0x5974, 0x49AA, 0xB2, 0x32, 0x08, 0x82, 0x53, 0x6F,
                 0x4C, 0xB5);
MIME_DEFINE_GUID(IID_IDirect3D11CaptureFrame, 0xFA50C623, 0x38DA, 0x4B32, 0xAC, 0xF3, 0xFA, 0x97, 0x34, 0xAD, 0x80, 0x0E);
MIME_DEFINE_GUID(IID_IDirect3D11CaptureFramePool, 0x24EB6D22, 0x1975, 0x422E, 0x82, 0xE7, 0x78, 0x0D, 0xBD, 0x8D, 0xDF,
                 0x24);
MIME_DEFINE_GUID(IID_IDirect3D11CaptureFramePoolStatics, 0x7784056A, 0x67AA, 0x4D53, 0xAE, 0x54, 0x10, 0x88, 0xD5, 0xA8,
                 0xCA, 0x21);
MIME_DEFINE_GUID(IID_IDirect3D11CaptureFramePoolStatics2, 0x589B103F, 0x6BBC, 0x5DF5, 0xA9, 0x91, 0x02, 0xE2, 0x8B,
                 0x3B, 0x66, 0xD5);
/* delegates for the frame pool's FrameArrived and the item's Closed. */
MIME_DEFINE_GUID(IID_FramePoolHandler, 0x51A947F7, 0x79CF, 0x5A3E, 0xA3, 0xA5, 0x12, 0x89, 0xCF, 0xA6, 0xDF, 0xE8);
MIME_DEFINE_GUID(IID_ItemClosedHandler, 0xE9C610C0, 0xA68C, 0x5BD9, 0x80, 0x21, 0x85, 0x89, 0x34, 0x6E, 0xEE, 0xE2);

#define RO_E_CLOSED ((HRESULT)0x80000013)

#endif
