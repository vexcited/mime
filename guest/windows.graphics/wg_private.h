#ifndef MIME_WINDOWS_GRAPHICS_PRIVATE_H
#define MIME_WINDOWS_GRAPHICS_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/winrt_streams.h>
#include <winrt/winrt.h>

extern HINSTANCE WgModule;

MIME_DEFINE_GUID(IID_IDisplayInformation, 0xBED112AE, 0xADC3, 0x4DC9, 0xAE, 0x65, 0x85, 0x1F, 0x4D, 0x7D, 0x47, 0x99);
MIME_DEFINE_GUID(IID_IDisplayInformation2, 0x4DCD0021, 0xFAD1, 0x4B8E, 0x8E, 0xDF, 0x77, 0x58, 0x87, 0xB8, 0xBF, 0x19);
MIME_DEFINE_GUID(IID_IDisplayInformation3, 0xDB15011D, 0x0F09, 0x4466, 0x8F, 0xF3, 0x11, 0xDE, 0x9A, 0x3C, 0x92, 0x9A);
MIME_DEFINE_GUID(IID_IDisplayInformation4, 0xC972CE2F, 0x1242, 0x46BE, 0xB5, 0x36, 0xE1, 0xAA, 0xFE, 0x9E, 0x7A, 0xCF);
MIME_DEFINE_GUID(IID_IDisplayInformation5, 0x3A5442DC, 0x2CDE, 0x4A8D, 0x80, 0xD1, 0x21, 0xDC, 0x5A, 0xDC, 0xC1, 0xAA);
MIME_DEFINE_GUID(IID_IDisplayInformationStatics, 0xC6A02A6C, 0xD452, 0x44DC, 0xBA, 0x07, 0x96, 0xF3, 0xC6, 0xAD, 0xF9,
                 0xD1);
MIME_DEFINE_GUID(IID_IDisplayInformationStaticsInterop, 0x7449121C, 0x382B, 0x4705, 0x8D, 0xA7, 0xA7, 0x95, 0xBA, 0x48,
                 0x20, 0x13);
MIME_DEFINE_GUID(IID_IAdvancedColorInfo, 0x8797DCFB, 0xB229, 0x4081, 0xAE, 0x9A, 0x2C, 0xC8, 0x5E, 0x34, 0xAD, 0x6A);
MIME_DEFINE_GUID(IID_IDispatcherQueueStatics_, 0xA96D83D7, 0x9371, 0x4517, 0x92, 0x45, 0xD0, 0x82, 0x4A, 0xC1, 0x2C,
                 0x74);
MIME_DEFINE_GUID(IID_IDispatcherQueueHandler_, 0xDFA2DC9C, 0x1A2D, 0x4917, 0x98, 0xF2, 0x93, 0x9A, 0xF1, 0xD6, 0xE0,
                 0xC8);
MIME_DEFINE_GUID(IID_IPropertyValueStatics_, 0x629BDBC8, 0xD932, 0x4FF4, 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8,
                 0x58);
/* IReference<Double>. */
MIME_DEFINE_GUID(IID_IReferenceDouble, 0x2F2D6C29, 0x5473, 0x5F3E, 0x92, 0xE7, 0x96, 0x57, 0x2B, 0xB9, 0x90, 0xE2);

#define E_NOTFOUND_ ((HRESULT)0x80070490L)
#define RPC_E_WRONG_THREAD_ ((HRESULT)0x8001010EL)

typedef enum DisplayOrientations {
    DisplayOrientations_None = 0,
    DisplayOrientations_Landscape = 1,
    DisplayOrientations_Portrait = 2,
    DisplayOrientations_LandscapeFlipped = 4,
    DisplayOrientations_PortraitFlipped = 8,
} DisplayOrientations;

typedef INT32 ResolutionScale;

typedef enum AdvancedColorKind {
    AdvancedColorKind_StandardDynamicRange = 0,
    AdvancedColorKind_WideColorGamut = 1,
    AdvancedColorKind_HighDynamicRange = 2,
} AdvancedColorKind;

typedef enum HdrMetadataFormat {
    HdrMetadataFormat_Hdr10 = 0,
    HdrMetadataFormat_Hdr10Plus = 1,
} HdrMetadataFormat;

typedef struct WgPoint {
    float X, Y;
} WgPoint;

typedef struct IAdvancedColorInfo IAdvancedColorInfo;
#define IAdvancedColorInfo_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_CurrentAdvancedColorKind)(T * This, AdvancedColorKind * value);                     \
    HRESULT(STDMETHODCALLTYPE *get_RedPrimary)(T * This, WgPoint * value);                                             \
    HRESULT(STDMETHODCALLTYPE *get_GreenPrimary)(T * This, WgPoint * value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_BluePrimary)(T * This, WgPoint * value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_WhitePoint)(T * This, WgPoint * value);                                             \
    HRESULT(STDMETHODCALLTYPE *get_MaxLuminanceInNits)(T * This, float *value);                                        \
    HRESULT(STDMETHODCALLTYPE *get_MinLuminanceInNits)(T * This, float *value);                                        \
    HRESULT(STDMETHODCALLTYPE *get_MaxAverageFullFrameLuminanceInNits)(T * This, float *value);                        \
    HRESULT(STDMETHODCALLTYPE *get_SdrWhiteLevelInNits)(T * This, float *value);                                       \
    HRESULT(STDMETHODCALLTYPE *IsHdrMetadataFormatCurrentlySupported)(T * This, HdrMetadataFormat format,              \
                                                                      BOOLEAN * result);                               \
    HRESULT(STDMETHODCALLTYPE *IsAdvancedColorKindAvailable)(T * This, AdvancedColorKind kind, BOOLEAN * result);
MIME_COM_INTERFACE(IAdvancedColorInfo)

typedef struct IDisplayInformation IDisplayInformation;
#define IDisplayInformation_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_CurrentOrientation)(T * This, DisplayOrientations * value);                         \
    HRESULT(STDMETHODCALLTYPE *get_NativeOrientation)(T * This, DisplayOrientations * value);                          \
    HRESULT(STDMETHODCALLTYPE *add_OrientationChanged)(T * This, IWrtEventHandler * handler,                           \
                                                       EventRegistrationToken * token);                                \
    HRESULT(STDMETHODCALLTYPE *remove_OrientationChanged)(T * This, EventRegistrationToken token);                     \
    HRESULT(STDMETHODCALLTYPE *get_ResolutionScale)(T * This, ResolutionScale * value);                                \
    HRESULT(STDMETHODCALLTYPE *get_LogicalDpi)(T * This, float *value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_RawDpiX)(T * This, float *value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_RawDpiY)(T * This, float *value);                                                   \
    HRESULT(STDMETHODCALLTYPE *add_DpiChanged)(T * This, IWrtEventHandler * handler, EventRegistrationToken * token);  \
    HRESULT(STDMETHODCALLTYPE *remove_DpiChanged)(T * This, EventRegistrationToken token);                             \
    HRESULT(STDMETHODCALLTYPE *get_StereoEnabled)(T * This, BOOLEAN * value);                                          \
    HRESULT(STDMETHODCALLTYPE *add_StereoEnabledChanged)(T * This, IWrtEventHandler * handler,                         \
                                                         EventRegistrationToken * token);                              \
    HRESULT(STDMETHODCALLTYPE *remove_StereoEnabledChanged)(T * This, EventRegistrationToken token);                   \
    HRESULT(STDMETHODCALLTYPE *GetColorProfileAsync)(T * This, void **operation);                                      \
    HRESULT(STDMETHODCALLTYPE *add_ColorProfileChanged)(T * This, IWrtEventHandler * handler,                          \
                                                        EventRegistrationToken * token);                               \
    HRESULT(STDMETHODCALLTYPE *remove_ColorProfileChanged)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDisplayInformation)

typedef struct IDisplayInformation2 IDisplayInformation2;
#define IDisplayInformation2_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_RawPixelsPerViewPixel)(T * This, double *value);
MIME_COM_INTERFACE(IDisplayInformation2)

typedef struct IDisplayInformation3 IDisplayInformation3;
#define IDisplayInformation3_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_DiagonalSizeInInches)(T * This, IInspectable * *value);
MIME_COM_INTERFACE(IDisplayInformation3)

typedef struct IDisplayInformation4 IDisplayInformation4;
#define IDisplayInformation4_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_ScreenWidthInRawPixels)(T * This, UINT32 * value);                                  \
    HRESULT(STDMETHODCALLTYPE *get_ScreenHeightInRawPixels)(T * This, UINT32 * value);
MIME_COM_INTERFACE(IDisplayInformation4)

typedef struct IDisplayInformation5 IDisplayInformation5;
#define IDisplayInformation5_METHODS(T)                                                                                \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetAdvancedColorInfo)(T * This, IAdvancedColorInfo * *value);                           \
    HRESULT(STDMETHODCALLTYPE *add_AdvancedColorInfoChanged)(T * This, IWrtEventHandler * handler,                     \
                                                             EventRegistrationToken * token);                          \
    HRESULT(STDMETHODCALLTYPE *remove_AdvancedColorInfoChanged)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDisplayInformation5)

typedef struct IDisplayInformationStatics IDisplayInformationStatics;
#define IDisplayInformationStatics_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetForCurrentView)(T * This, IDisplayInformation * *current);                           \
    HRESULT(STDMETHODCALLTYPE *get_AutoRotationPreferences)(T * This, DisplayOrientations * value);                    \
    HRESULT(STDMETHODCALLTYPE *put_AutoRotationPreferences)(T * This, DisplayOrientations value);                      \
    HRESULT(STDMETHODCALLTYPE *add_DisplayContentsInvalidated)(T * This, IWrtEventHandler * handler,                   \
                                                               EventRegistrationToken * token);                        \
    HRESULT(STDMETHODCALLTYPE *remove_DisplayContentsInvalidated)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDisplayInformationStatics)

typedef struct IDisplayInformationStaticsInterop IDisplayInformationStaticsInterop;
#define IDisplayInformationStaticsInterop_METHODS(T)                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetForWindow)(T * This, HWND window, REFIID riid, void **result);                       \
    HRESULT(STDMETHODCALLTYPE *GetForMonitor)(T * This, HMONITOR monitor, REFIID riid, void **result);
MIME_COM_INTERFACE(IDisplayInformationStaticsInterop)

/* the parts of the DispatcherQueue of coremessaging that are used here. */
typedef struct IWgQueueHandler IWgQueueHandler;
#define IWgQueueHandler_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This);
MIME_COM_INTERFACE(IWgQueueHandler)

typedef struct IWgQueue IWgQueue;
#define IWgQueue_METHODS(T)                                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateTimer)(T * This, IInspectable * *result);                                         \
    HRESULT(STDMETHODCALLTYPE *TryEnqueue)(T * This, IWgQueueHandler * callback, BOOLEAN * result);                    \
    HRESULT(STDMETHODCALLTYPE *TryEnqueueWithPriority)(T * This, INT32 priority, IWgQueueHandler * callback,           \
                                                       BOOLEAN *result);
MIME_COM_INTERFACE(IWgQueue)

typedef struct IWgQueueStatics IWgQueueStatics;
#define IWgQueueStatics_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetForCurrentThread)(T * This, IWgQueue * *result);
MIME_COM_INTERFACE(IWgQueueStatics)

typedef struct IWgPropertyValueStatics IWgPropertyValueStatics;
#define IWgPropertyValueStatics_METHODS(T)                                                                             \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateEmpty)(T * This, IInspectable * *value);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateUInt8)(T * This, BYTE v, IInspectable * *value);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateInt16)(T * This, INT16 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt16)(T * This, UINT16 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateInt32)(T * This, INT32 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt32)(T * This, UINT32 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateInt64)(T * This, INT64 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt64)(T * This, UINT64 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateSingle)(T * This, float v, IInspectable * *value);                                \
    HRESULT(STDMETHODCALLTYPE *CreateDouble)(T * This, double v, IInspectable * *value);
MIME_COM_INTERFACE(IWgPropertyValueStatics)

/* ------------------------------------------------------------------ the display */

/* what changed, as Windows tells its events apart. */
enum {
    WG_ORIENTATION = 0x1,
    WG_DPI = 0x2,
    WG_STEREO = 0x4,
    WG_COLOR_PROFILE = 0x8,
    WG_ADVANCED_COLOR = 0x40,
};

typedef struct WG_COLOR {
    AdvancedColorKind Kind;
    UINT32 Available;
    UINT32 HdrMetadata;
    WgPoint Red, Green, Blue, White;
    float MinLuminance, MaxLuminance, MaxFullFrameLuminance, SdrWhiteLevel;
} WG_COLOR;

/* the state of a monitor that events follow. */
typedef struct WG_STATE {
    HMONITOR Monitor;
    UINT Scale;
    float RawDpiX, RawDpiY;
    DisplayOrientations Current, Native;
    UINT32 Width, Height;
    WCHAR Profile[MAX_PATH];
    WG_COLOR Color;
} WG_STATE;

typedef struct WG_DISPLAY WG_DISPLAY;
struct WG_DISPLAY {
    WRT_AGILE Object;
    HWND Window;
    HMONITOR Monitor;
    DWORD Thread;
    IWgQueue *Queue;
    SRWLOCK Lock;
    WG_STATE Last;
    BOOL Watching;
    WRT_EVENT Orientation, Dpi, Stereo, ColorProfile, AdvancedColor;
    WG_DISPLAY *NextWatched;
};

/* monitor.c */
HMONITOR WgMonitorOf(HWND window, HMONITOR monitor);
UINT WgQueryScale(HMONITOR monitor);
void WgQueryRawDpi(HMONITOR monitor, float *x, float *y);
/* the raw size and the orientations. */
void WgQueryMode(HMONITOR monitor, WG_STATE *state);
void WgQueryState(HMONITOR monitor, WG_STATE *state);
void WgQueryColor(HMONITOR monitor, WG_COLOR *color);
HRESULT WgColorProfilePath(HMONITOR monitor, WCHAR *path, UINT32 capacity);

/* color.c */
HRESULT WgAdvancedColorCreate(const WG_COLOR *color, IAdvancedColorInfo **result);
HRESULT WgColorProfileAsync(HMONITOR monitor, void **operation);

/* display.c */
HRESULT WgDisplayCreate(HWND window, HMONITOR monitor, REFIID riid, void **result);
/* compares the display with what it was and raises the events of what changed, on the thread of its queue. */
void WgDisplayRefresh(WG_DISPLAY *display);

/* watch.c */
HRESULT WgWatchStart(WG_DISPLAY *display);
void WgWatchStop(WG_DISPLAY *display);

#endif
