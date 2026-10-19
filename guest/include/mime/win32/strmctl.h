/*
 * what directshow and kernel streaming leave programs for the controls of a camera: the camera control and the video
 * processing amplifier, which media foundation's capture sources answer too, and the kernel streaming control that
 * reaches a device's properties. methods are in windows' vtable order.
 */
#ifndef MIME_WIN32_STRMCTL_H
#define MIME_WIN32_STRMCTL_H

#include <mime/win32/objbase.h>

typedef struct IAMCameraControl IAMCameraControl;
typedef struct IAMVideoProcAmp IAMVideoProcAmp;
typedef struct IKsControl IKsControl;

typedef enum tagVideoProcAmpProperty {
    VideoProcAmp_Brightness = 0,
    VideoProcAmp_Contrast,
    VideoProcAmp_Hue,
    VideoProcAmp_Saturation,
    VideoProcAmp_Sharpness,
    VideoProcAmp_Gamma,
    VideoProcAmp_ColorEnable,
    VideoProcAmp_WhiteBalance,
    VideoProcAmp_BacklightCompensation,
    VideoProcAmp_Gain,
} VideoProcAmpProperty;

typedef enum tagVideoProcAmpFlags {
    VideoProcAmp_Flags_Auto = 0x1,
    VideoProcAmp_Flags_Manual = 0x2,
} VideoProcAmpFlags;

typedef enum tagCameraControlProperty {
    CameraControl_Pan = 0,
    CameraControl_Tilt,
    CameraControl_Roll,
    CameraControl_Zoom,
    CameraControl_Exposure,
    CameraControl_Iris,
    CameraControl_Focus,
} CameraControlProperty;

typedef enum tagCameraControlFlags {
    CameraControl_Flags_Auto = 0x1,
    CameraControl_Flags_Manual = 0x2,
} CameraControlFlags;

#define IAMCameraControl_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetRange)(T * This, LONG property, LONG *min, LONG *max, LONG *step, LONG *def,         \
                                         LONG *flags);                                                                 \
    HRESULT(STDMETHODCALLTYPE *Set)(T * This, LONG property, LONG value, LONG flags);                                  \
    HRESULT(STDMETHODCALLTYPE *Get)(T * This, LONG property, LONG *value, LONG *flags);

#define IAMVideoProcAmp_METHODS(T) IAMCameraControl_METHODS(T)

/* the property, method and event requests of kernel streaming: a set, an id in it and what to do. */
typedef struct KSIDENTIFIER {
    GUID Set;
    ULONG Id;
    ULONG Flags;
} KSIDENTIFIER, KSPROPERTY, KSMETHOD, KSEVENT, *PKSPROPERTY, *PKSMETHOD, *PKSEVENT;

#define IKsControl_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *KsProperty)(T * This, PKSPROPERTY property, ULONG length, void *data, ULONG data_length, \
                                           ULONG *returned);                                                           \
    HRESULT(STDMETHODCALLTYPE *KsMethod)(T * This, PKSMETHOD method, ULONG length, void *data, ULONG data_length,      \
                                         ULONG *returned);                                                             \
    HRESULT(STDMETHODCALLTYPE *KsEvent)(T * This, PKSEVENT event, ULONG length, void *data, ULONG data_length,         \
                                        ULONG *returned);

MIME_COM_INTERFACE(IAMCameraControl)
MIME_COM_INTERFACE(IAMVideoProcAmp)
MIME_COM_INTERFACE(IKsControl)

MIME_DEFINE_GUID(IID_IAMCameraControl, 0xC6E13370, 0x30AC, 0x11D0, 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56);
MIME_DEFINE_GUID(IID_IAMVideoProcAmp, 0xC6E13360, 0x30AC, 0x11D0, 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56);
MIME_DEFINE_GUID(IID_IKsControl, 0x28F54685, 0x06FD, 0x11D2, 0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);

/* the categories cameras are in, and their interfaces are opened with the reference string "global". */
MIME_DEFINE_GUID(KSCATEGORY_CAPTURE, 0x65E8773D, 0x8F56, 0x11D0, 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);
MIME_DEFINE_GUID(KSCATEGORY_RENDER, 0x65E8773E, 0x8F56, 0x11D0, 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);
MIME_DEFINE_GUID(KSCATEGORY_VIDEO, 0x6994AD05, 0x93EF, 0x11D0, 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);
MIME_DEFINE_GUID(KSCATEGORY_RENDER_EXT, 0xCC7BFB41, 0xF175, 0x11D1, 0xA3, 0x92, 0x00, 0xE0, 0x29, 0x1F, 0x39, 0x59);
MIME_DEFINE_GUID(KSCATEGORY_VIDEO_CAMERA, 0xE5323777, 0xF976, 0x4F5B, 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44);
MIME_DEFINE_GUID(KSCATEGORY_SENSOR_CAMERA, 0x24E552D7, 0x6523, 0x47F7, 0xA6, 0x47, 0xD3, 0x46, 0x5B, 0xF1, 0xF5, 0xCA);

#endif
