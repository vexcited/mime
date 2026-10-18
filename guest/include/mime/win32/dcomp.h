/*
 * dcomp: composition of visuals into windows. the interfaces and their method
 * order are windows', as programs call them through the vtables; where a
 * method has an overload for an animation and one for a value, the animation
 * comes first, as in windows' own vtables.
 */
#ifndef MIME_WIN32_DCOMP_H
#define MIME_WIN32_DCOMP_H

#include <mime/win32/objbase.h>
#include <mime/win32/dxgi.h>
#include <mime/win32/d3d9types.h>

/* ------------------------------------------------------------------ direct2d types the interfaces take */

typedef struct D2D_RECT_F {
    float left, top, right, bottom;
} D2D_RECT_F;

typedef struct D2D_RECT_U {
    UINT32 left, top, right, bottom;
} D2D_RECT_U;

typedef struct D2D_RECT_L {
    INT32 left, top, right, bottom;
} D2D_RECT_L;

typedef struct D2D_MATRIX_3X2_F {
    float _11, _12, _21, _22, _31, _32;
} D2D_MATRIX_3X2_F;

typedef struct D2D_MATRIX_4X4_F {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D2D_MATRIX_4X4_F;

typedef struct D2D_MATRIX_5X4_F {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
            float _51, _52, _53, _54;
        };
        float m[5][4];
    };
} D2D_MATRIX_5X4_F;

typedef struct D2D1_COLOR_F {
    float r, g, b, a;
} D2D1_COLOR_F;

typedef enum D2D1_BORDER_MODE {
    D2D1_BORDER_MODE_SOFT = 0,
    D2D1_BORDER_MODE_HARD = 1
} D2D1_BORDER_MODE;

typedef enum D2D1_COLORMATRIX_ALPHA_MODE {
    D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT = 2
} D2D1_COLORMATRIX_ALPHA_MODE;

/* ------------------------------------------------------------------ types and errors */

typedef enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE {
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR = 1,
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT = -1
} DCOMPOSITION_BITMAP_INTERPOLATION_MODE;

typedef enum DCOMPOSITION_BORDER_MODE {
    DCOMPOSITION_BORDER_MODE_SOFT = 0,
    DCOMPOSITION_BORDER_MODE_HARD = 1,
    DCOMPOSITION_BORDER_MODE_INHERIT = -1
} DCOMPOSITION_BORDER_MODE;

typedef enum DCOMPOSITION_COMPOSITE_MODE {
    DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER = 0,
    DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT = 1,
    DCOMPOSITION_COMPOSITE_MODE_MIN_BLEND = 2,
    DCOMPOSITION_COMPOSITE_MODE_INHERIT = -1
} DCOMPOSITION_COMPOSITE_MODE;

typedef enum DCOMPOSITION_BACKFACE_VISIBILITY {
    DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE = 0,
    DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN = 1,
    DCOMPOSITION_BACKFACE_VISIBILITY_INHERIT = -1
} DCOMPOSITION_BACKFACE_VISIBILITY;

typedef enum DCOMPOSITION_OPACITY_MODE {
    DCOMPOSITION_OPACITY_MODE_LAYER = 0,
    DCOMPOSITION_OPACITY_MODE_MULTIPLY = 1,
    DCOMPOSITION_OPACITY_MODE_INHERIT = -1
} DCOMPOSITION_OPACITY_MODE;

typedef enum DCOMPOSITION_DEPTH_MODE {
    DCOMPOSITION_DEPTH_MODE_TREE = 0,
    DCOMPOSITION_DEPTH_MODE_SPATIAL = 1,
    DCOMPOSITION_DEPTH_MODE_SORTED = 3,
    DCOMPOSITION_DEPTH_MODE_INHERIT = -1
} DCOMPOSITION_DEPTH_MODE;

typedef struct DCOMPOSITION_FRAME_STATISTICS {
    LARGE_INTEGER lastFrameTime;
    DXGI_RATIONAL currentCompositionRate;
    LARGE_INTEGER currentTime;
    LARGE_INTEGER timeFrequency;
    LARGE_INTEGER nextEstimatedFrameTime;
} DCOMPOSITION_FRAME_STATISTICS;

typedef struct DCompositionInkTrailPoint {
    float x, y, radius;
} DCompositionInkTrailPoint;

#define COMPOSITIONOBJECT_READ 0x0001
#define COMPOSITIONOBJECT_WRITE 0x0002
#define COMPOSITIONOBJECT_ALL_ACCESS (COMPOSITIONOBJECT_READ | COMPOSITIONOBJECT_WRITE)

#define DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED ((HRESULT)0x88980800)
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED ((HRESULT)0x88980801)
#define DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED ((HRESULT)0x88980802)

/* the statistics of the compositor's clock. */
typedef UINT64 COMPOSITION_FRAME_ID;

typedef enum COMPOSITION_FRAME_ID_TYPE {
    COMPOSITION_FRAME_ID_CREATED = 0,
    COMPOSITION_FRAME_ID_CONFIRMED = 1,
    COMPOSITION_FRAME_ID_COMPLETED = 2
} COMPOSITION_FRAME_ID_TYPE;

typedef struct COMPOSITION_FRAME_STATS {
    UINT64 startTime;
    UINT64 targetTime;
    UINT64 framePeriod;
} COMPOSITION_FRAME_STATS;

typedef struct COMPOSITION_TARGET_ID {
    LUID adapterLuid;
    UINT vidPnSourceId;
    UINT uniqueId;
    UINT reserved[4];
} COMPOSITION_TARGET_ID;

typedef struct COMPOSITION_STATS {
    UINT presentCount;
    UINT refreshCount;
    UINT virtualRefreshCount;
    UINT64 time;
} COMPOSITION_STATS;

typedef struct COMPOSITION_TARGET_STATS {
    UINT outstandingPresents;
    UINT64 presentTime;
    UINT64 vblankDuration;
    COMPOSITION_STATS presented;
    COMPOSITION_STATS completed;
} COMPOSITION_TARGET_STATS;

/* ------------------------------------------------------------------ interface types */

typedef struct IDCompositionAnimation IDCompositionAnimation;
typedef struct IDCompositionSurface IDCompositionSurface;
typedef struct IDCompositionVirtualSurface IDCompositionVirtualSurface;
typedef struct IDCompositionEffect IDCompositionEffect;
typedef struct IDCompositionTransform3D IDCompositionTransform3D;
typedef struct IDCompositionTransform IDCompositionTransform;
typedef struct IDCompositionTranslateTransform IDCompositionTranslateTransform;
typedef struct IDCompositionScaleTransform IDCompositionScaleTransform;
typedef struct IDCompositionRotateTransform IDCompositionRotateTransform;
typedef struct IDCompositionSkewTransform IDCompositionSkewTransform;
typedef struct IDCompositionMatrixTransform IDCompositionMatrixTransform;
typedef struct IDCompositionTranslateTransform3D IDCompositionTranslateTransform3D;
typedef struct IDCompositionScaleTransform3D IDCompositionScaleTransform3D;
typedef struct IDCompositionRotateTransform3D IDCompositionRotateTransform3D;
typedef struct IDCompositionMatrixTransform3D IDCompositionMatrixTransform3D;
typedef struct IDCompositionEffectGroup IDCompositionEffectGroup;
typedef struct IDCompositionClip IDCompositionClip;
typedef struct IDCompositionRectangleClip IDCompositionRectangleClip;
typedef struct IDCompositionVisual IDCompositionVisual;
typedef struct IDCompositionVisual2 IDCompositionVisual2;
typedef struct IDCompositionVisualDebug IDCompositionVisualDebug;
typedef struct IDCompositionVisual3 IDCompositionVisual3;
typedef struct IDCompositionTarget IDCompositionTarget;
typedef struct IDCompositionDevice IDCompositionDevice;
typedef struct IDCompositionSurfaceFactory IDCompositionSurfaceFactory;
typedef struct IDCompositionDevice2 IDCompositionDevice2;
typedef struct IDCompositionDesktopDevice IDCompositionDesktopDevice;
typedef struct IDCompositionDeviceDebug IDCompositionDeviceDebug;
typedef struct IDCompositionFilterEffect IDCompositionFilterEffect;
typedef struct IDCompositionSaturationEffect IDCompositionSaturationEffect;
typedef struct IDCompositionTableTransferEffect IDCompositionTableTransferEffect;
typedef struct IDCompositionGaussianBlurEffect IDCompositionGaussianBlurEffect;
typedef struct IDCompositionColorMatrixEffect IDCompositionColorMatrixEffect;
typedef struct IDCompositionDevice3 IDCompositionDevice3;
typedef struct IDCompositionTexture IDCompositionTexture;
typedef struct IDCompositionDevice4 IDCompositionDevice4;
typedef struct IDCompositionDynamicTexture IDCompositionDynamicTexture;
typedef struct IDCompositionDevice5 IDCompositionDevice5;
typedef struct IDCompositionDelegatedInkTrail IDCompositionDelegatedInkTrail;
typedef struct IDCompositionInkTrailDevice IDCompositionInkTrailDevice;

/* ------------------------------------------------------------------ animation */

#define IDCompositionAnimation_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *SetAbsoluteBeginTime)(T * This, LARGE_INTEGER begin);                                   \
    HRESULT(STDMETHODCALLTYPE *AddCubic)(T * This, double offset, float constant, float linear, float quadratic,       \
                                         float cubic);                                                                 \
    HRESULT(STDMETHODCALLTYPE *AddSinusoidal)(T * This, double offset, float bias, float amplitude, float frequency,   \
                                              float phase);                                                            \
    HRESULT(STDMETHODCALLTYPE *AddRepeat)(T * This, double offset, double duration);                                   \
    HRESULT(STDMETHODCALLTYPE *End)(T * This, double offset, float value);

/* ------------------------------------------------------------------ surfaces */

#define IDCompositionSurface_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *BeginDraw)(T * This, const RECT *rect, REFIID riid, void **object, POINT *offset);      \
    HRESULT(STDMETHODCALLTYPE *EndDraw)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *SuspendDraw)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *ResumeDraw)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *Scroll)(T * This, const RECT *scroll, const RECT *clip, int offset_x, int offset_y);

#define IDCompositionVirtualSurface_METHODS(T)                                                                         \
    IDCompositionSurface_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *Resize)(T * This, UINT width, UINT height);                                             \
    HRESULT(STDMETHODCALLTYPE *Trim)(T * This, const RECT *rectangles, UINT count);

#define IDCompositionSurfaceFactory_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,                   \
                                              DXGI_ALPHA_MODE alpha, IDCompositionSurface **surface);                  \
    HRESULT(STDMETHODCALLTYPE *CreateVirtualSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,            \
                                                     DXGI_ALPHA_MODE alpha, IDCompositionVirtualSurface **surface);

/* ------------------------------------------------------------------ effects, transforms and clips */

#define IDCompositionEffect_METHODS(T) MIME_IUNKNOWN_METHODS(T)
#define IDCompositionTransform3D_METHODS(T) IDCompositionEffect_METHODS(T)
#define IDCompositionTransform_METHODS(T) IDCompositionTransform3D_METHODS(T)

/* a property that is a value or an animation: the animation comes first. */
#define DCOMP_FLOAT_PROPERTY(T, name)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *name##A)(T * This, IDCompositionAnimation * animation);                                 \
    HRESULT(STDMETHODCALLTYPE *name)(T * This, float value);

#define IDCompositionTranslateTransform_METHODS(T)                                                                     \
    IDCompositionTransform_METHODS(T)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetOffsetX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetOffsetY)

#define IDCompositionScaleTransform_METHODS(T)                                                                         \
    IDCompositionTransform_METHODS(T)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetScaleX)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetScaleY)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetCenterX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterY)

#define IDCompositionRotateTransform_METHODS(T)                                                                        \
    IDCompositionTransform_METHODS(T)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetAngle)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetCenterX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterY)

#define IDCompositionSkewTransform_METHODS(T)                                                                          \
    IDCompositionTransform_METHODS(T)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetAngleX)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetAngleY)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetCenterX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterY)

#define IDCompositionMatrixTransform_METHODS(T)                                                                        \
    IDCompositionTransform_METHODS(T)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *SetMatrix)(T * This, const D2D_MATRIX_3X2_F *matrix);                                   \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElementA)(T * This, int row, int column, IDCompositionAnimation *animation);   \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElement)(T * This, int row, int column, float value);

#define IDCompositionTranslateTransform3D_METHODS(T)                                                                   \
    IDCompositionTransform3D_METHODS(T)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetOffsetX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetOffsetY)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetOffsetZ)

#define IDCompositionScaleTransform3D_METHODS(T)                                                                       \
    IDCompositionTransform3D_METHODS(T)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetScaleX)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetScaleY)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetScaleZ)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetCenterX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterY)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterZ)

#define IDCompositionRotateTransform3D_METHODS(T)                                                                      \
    IDCompositionTransform3D_METHODS(T)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetAngle)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetAxisX)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetAxisY)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetAxisZ)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetCenterX)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterY)                                                                                \
    DCOMP_FLOAT_PROPERTY(T, SetCenterZ)

#define IDCompositionMatrixTransform3D_METHODS(T)                                                                      \
    IDCompositionTransform3D_METHODS(T)                                                                                \
    HRESULT(STDMETHODCALLTYPE *SetMatrix)(T * This, const D3DMATRIX *matrix);                                          \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElementA)(T * This, int row, int column, IDCompositionAnimation *animation);   \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElement)(T * This, int row, int column, float value);

#define IDCompositionEffectGroup_METHODS(T)                                                                            \
    IDCompositionEffect_METHODS(T)                                                                                     \
    DCOMP_FLOAT_PROPERTY(T, SetOpacity)                                                                                \
    HRESULT(STDMETHODCALLTYPE *SetTransform3D)(T * This, IDCompositionTransform3D * transform);

#define IDCompositionClip_METHODS(T) MIME_IUNKNOWN_METHODS(T)

#define IDCompositionRectangleClip_METHODS(T)                                                                          \
    IDCompositionClip_METHODS(T)                                                                                       \
    DCOMP_FLOAT_PROPERTY(T, SetLeft)                                                                                   \
    DCOMP_FLOAT_PROPERTY(T, SetTop)                                                                                    \
    DCOMP_FLOAT_PROPERTY(T, SetRight)                                                                                  \
    DCOMP_FLOAT_PROPERTY(T, SetBottom)                                                                                 \
    DCOMP_FLOAT_PROPERTY(T, SetTopLeftRadiusX)                                                                         \
    DCOMP_FLOAT_PROPERTY(T, SetTopLeftRadiusY)                                                                         \
    DCOMP_FLOAT_PROPERTY(T, SetTopRightRadiusX)                                                                        \
    DCOMP_FLOAT_PROPERTY(T, SetTopRightRadiusY)                                                                        \
    DCOMP_FLOAT_PROPERTY(T, SetBottomLeftRadiusX)                                                                      \
    DCOMP_FLOAT_PROPERTY(T, SetBottomLeftRadiusY)                                                                      \
    DCOMP_FLOAT_PROPERTY(T, SetBottomRightRadiusX)                                                                     \
    DCOMP_FLOAT_PROPERTY(T, SetBottomRightRadiusY)

/* ------------------------------------------------------------------ visuals and targets */

#define IDCompositionVisual_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetOffsetXA)(T * This, IDCompositionAnimation * animation);                             \
    HRESULT(STDMETHODCALLTYPE *SetOffsetX)(T * This, float offset);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetOffsetYA)(T * This, IDCompositionAnimation * animation);                             \
    HRESULT(STDMETHODCALLTYPE *SetOffsetY)(T * This, float offset);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetTransformObject)(T * This, IDCompositionTransform * transform);                      \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, const D2D_MATRIX_3X2_F *matrix);                                \
    HRESULT(STDMETHODCALLTYPE *SetTransformParent)(T * This, IDCompositionVisual * visual);                            \
    HRESULT(STDMETHODCALLTYPE *SetEffect)(T * This, IDCompositionEffect * effect);                                     \
    HRESULT(STDMETHODCALLTYPE *SetBitmapInterpolationMode)(T * This, DCOMPOSITION_BITMAP_INTERPOLATION_MODE mode);     \
    HRESULT(STDMETHODCALLTYPE *SetBorderMode)(T * This, DCOMPOSITION_BORDER_MODE mode);                                \
    HRESULT(STDMETHODCALLTYPE *SetClipObject)(T * This, IDCompositionClip * clip);                                     \
    HRESULT(STDMETHODCALLTYPE *SetClip)(T * This, const D2D_RECT_F *rect);                                             \
    HRESULT(STDMETHODCALLTYPE *SetContent)(T * This, IUnknown * content);                                              \
    HRESULT(STDMETHODCALLTYPE *AddVisual)(T * This, IDCompositionVisual * visual, BOOL insert_above,                   \
                                          IDCompositionVisual *reference);                                             \
    HRESULT(STDMETHODCALLTYPE *RemoveVisual)(T * This, IDCompositionVisual * visual);                                  \
    HRESULT(STDMETHODCALLTYPE *RemoveAllVisuals)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *SetCompositeMode)(T * This, DCOMPOSITION_COMPOSITE_MODE mode);

#define IDCompositionVisual2_METHODS(T)                                                                                \
    IDCompositionVisual_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetOpacityMode)(T * This, DCOMPOSITION_OPACITY_MODE mode);                              \
    HRESULT(STDMETHODCALLTYPE *SetBackFaceVisibility)(T * This, DCOMPOSITION_BACKFACE_VISIBILITY visibility);

#define IDCompositionVisualDebug_METHODS(T)                                                                            \
    IDCompositionVisual2_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *EnableHeatMap)(T * This, const D2D1_COLOR_F *color);                                    \
    HRESULT(STDMETHODCALLTYPE *DisableHeatMap)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *EnableRedrawRegions)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *DisableRedrawRegions)(T * This);

#define IDCompositionVisual3_METHODS(T)                                                                                \
    IDCompositionVisualDebug_METHODS(T)                                                                                \
    HRESULT(STDMETHODCALLTYPE *SetDepthMode)(T * This, DCOMPOSITION_DEPTH_MODE mode);                                  \
    HRESULT(STDMETHODCALLTYPE *SetOffsetZA)(T * This, IDCompositionAnimation * animation);                             \
    HRESULT(STDMETHODCALLTYPE *SetOffsetZ)(T * This, float offset);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetOpacityA)(T * This, IDCompositionAnimation * animation);                             \
    HRESULT(STDMETHODCALLTYPE *SetOpacity)(T * This, float opacity);                                                   \
    HRESULT(STDMETHODCALLTYPE *SetTransform3DObject)(T * This, IDCompositionTransform3D * transform);                  \
    HRESULT(STDMETHODCALLTYPE *SetTransform3D)(T * This, const D2D_MATRIX_4X4_F *matrix);                              \
    HRESULT(STDMETHODCALLTYPE *SetVisible)(T * This, BOOL visible);

#define IDCompositionTarget_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetRoot)(T * This, IDCompositionVisual * visual);

/* ------------------------------------------------------------------ devices */

#define IDCompositionDevice_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Commit)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *WaitForCommitCompletion)(T * This);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetFrameStatistics)(T * This, DCOMPOSITION_FRAME_STATISTICS * statistics);              \
    HRESULT(STDMETHODCALLTYPE *CreateTargetForHwnd)(T * This, HWND hwnd, BOOL topmost, IDCompositionTarget **target);  \
    HRESULT(STDMETHODCALLTYPE *CreateVisual)(T * This, IDCompositionVisual * *visual);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,                   \
                                              DXGI_ALPHA_MODE alpha, IDCompositionSurface **surface);                  \
    HRESULT(STDMETHODCALLTYPE *CreateVirtualSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,            \
                                                     DXGI_ALPHA_MODE alpha, IDCompositionVirtualSurface **surface);    \
    HRESULT(STDMETHODCALLTYPE *CreateSurfaceFromHandle)(T * This, HANDLE handle, IUnknown * *surface);                 \
    HRESULT(STDMETHODCALLTYPE *CreateSurfaceFromHwnd)(T * This, HWND hwnd, IUnknown * *surface);                       \
    HRESULT(STDMETHODCALLTYPE *CreateTranslateTransform)(T * This, IDCompositionTranslateTransform * *transform);     \
    HRESULT(STDMETHODCALLTYPE *CreateScaleTransform)(T * This, IDCompositionScaleTransform * *transform);             \
    HRESULT(STDMETHODCALLTYPE *CreateRotateTransform)(T * This, IDCompositionRotateTransform * *transform);           \
    HRESULT(STDMETHODCALLTYPE *CreateSkewTransform)(T * This, IDCompositionSkewTransform * *transform);               \
    HRESULT(STDMETHODCALLTYPE *CreateMatrixTransform)(T * This, IDCompositionMatrixTransform * *transform);           \
    HRESULT(STDMETHODCALLTYPE *CreateTransformGroup)(T * This, IDCompositionTransform * *transforms, UINT count,      \
                                                     IDCompositionTransform **group);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateTranslateTransform3D)(T * This, IDCompositionTranslateTransform3D * *transform);  \
    HRESULT(STDMETHODCALLTYPE *CreateScaleTransform3D)(T * This, IDCompositionScaleTransform3D * *transform);         \
    HRESULT(STDMETHODCALLTYPE *CreateRotateTransform3D)(T * This, IDCompositionRotateTransform3D * *transform);       \
    HRESULT(STDMETHODCALLTYPE *CreateMatrixTransform3D)(T * This, IDCompositionMatrixTransform3D * *transform);       \
    HRESULT(STDMETHODCALLTYPE *CreateTransform3DGroup)(T * This, IDCompositionTransform3D * *transforms, UINT count,  \
                                                       IDCompositionTransform3D **group);                              \
    HRESULT(STDMETHODCALLTYPE *CreateEffectGroup)(T * This, IDCompositionEffectGroup * *group);                        \
    HRESULT(STDMETHODCALLTYPE *CreateRectangleClip)(T * This, IDCompositionRectangleClip * *clip);                     \
    HRESULT(STDMETHODCALLTYPE *CreateAnimation)(T * This, IDCompositionAnimation * *animation);                        \
    HRESULT(STDMETHODCALLTYPE *CheckDeviceState)(T * This, BOOL * valid);

#define IDCompositionDevice2_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Commit)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *WaitForCommitCompletion)(T * This);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetFrameStatistics)(T * This, DCOMPOSITION_FRAME_STATISTICS * statistics);              \
    HRESULT(STDMETHODCALLTYPE *CreateVisual)(T * This, IDCompositionVisual2 * *visual);                                \
    HRESULT(STDMETHODCALLTYPE *CreateSurfaceFactory)(T * This, IUnknown * device,                                      \
                                                     IDCompositionSurfaceFactory **factory);                           \
    HRESULT(STDMETHODCALLTYPE *CreateSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,                   \
                                              DXGI_ALPHA_MODE alpha, IDCompositionSurface **surface);                  \
    HRESULT(STDMETHODCALLTYPE *CreateVirtualSurface)(T * This, UINT width, UINT height, DXGI_FORMAT format,            \
                                                     DXGI_ALPHA_MODE alpha, IDCompositionVirtualSurface **surface);    \
    HRESULT(STDMETHODCALLTYPE *CreateTranslateTransform)(T * This, IDCompositionTranslateTransform * *transform);     \
    HRESULT(STDMETHODCALLTYPE *CreateScaleTransform)(T * This, IDCompositionScaleTransform * *transform);             \
    HRESULT(STDMETHODCALLTYPE *CreateRotateTransform)(T * This, IDCompositionRotateTransform * *transform);           \
    HRESULT(STDMETHODCALLTYPE *CreateSkewTransform)(T * This, IDCompositionSkewTransform * *transform);               \
    HRESULT(STDMETHODCALLTYPE *CreateMatrixTransform)(T * This, IDCompositionMatrixTransform * *transform);           \
    HRESULT(STDMETHODCALLTYPE *CreateTransformGroup)(T * This, IDCompositionTransform * *transforms, UINT count,      \
                                                     IDCompositionTransform **group);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateTranslateTransform3D)(T * This, IDCompositionTranslateTransform3D * *transform);  \
    HRESULT(STDMETHODCALLTYPE *CreateScaleTransform3D)(T * This, IDCompositionScaleTransform3D * *transform);         \
    HRESULT(STDMETHODCALLTYPE *CreateRotateTransform3D)(T * This, IDCompositionRotateTransform3D * *transform);       \
    HRESULT(STDMETHODCALLTYPE *CreateMatrixTransform3D)(T * This, IDCompositionMatrixTransform3D * *transform);       \
    HRESULT(STDMETHODCALLTYPE *CreateTransform3DGroup)(T * This, IDCompositionTransform3D * *transforms, UINT count,  \
                                                       IDCompositionTransform3D **group);                              \
    HRESULT(STDMETHODCALLTYPE *CreateEffectGroup)(T * This, IDCompositionEffectGroup * *group);                        \
    HRESULT(STDMETHODCALLTYPE *CreateRectangleClip)(T * This, IDCompositionRectangleClip * *clip);                     \
    HRESULT(STDMETHODCALLTYPE *CreateAnimation)(T * This, IDCompositionAnimation * *animation);

#define IDCompositionDesktopDevice_METHODS(T)                                                                          \
    IDCompositionDevice2_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateTargetForHwnd)(T * This, HWND hwnd, BOOL topmost, IDCompositionTarget **target);  \
    HRESULT(STDMETHODCALLTYPE *CreateSurfaceFromHandle)(T * This, HANDLE handle, IUnknown * *surface);                 \
    HRESULT(STDMETHODCALLTYPE *CreateSurfaceFromHwnd)(T * This, HWND hwnd, IUnknown * *surface);

#define IDCompositionDeviceDebug_METHODS(T)                                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EnableDebugCounters)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *DisableDebugCounters)(T * This);

/* ------------------------------------------------------------------ filter effects and device 3 to 5 */

#define IDCompositionFilterEffect_METHODS(T)                                                                           \
    IDCompositionEffect_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetInput)(T * This, UINT index, IUnknown * input, UINT flags);

#define IDCompositionSaturationEffect_METHODS(T)                                                                       \
    IDCompositionFilterEffect_METHODS(T)                                                                               \
    DCOMP_FLOAT_PROPERTY(T, SetSaturation)

#define IDCompositionTableTransferEffect_METHODS(T)                                                                    \
    IDCompositionFilterEffect_METHODS(T)                                                                               \
    HRESULT(STDMETHODCALLTYPE *SetRedTable)(T * This, const float *values, UINT count);                                \
    HRESULT(STDMETHODCALLTYPE *SetGreenTable)(T * This, const float *values, UINT count);                              \
    HRESULT(STDMETHODCALLTYPE *SetBlueTable)(T * This, const float *values, UINT count);                               \
    HRESULT(STDMETHODCALLTYPE *SetAlphaTable)(T * This, const float *values, UINT count);                              \
    HRESULT(STDMETHODCALLTYPE *SetRedDisable)(T * This, BOOL disable);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetGreenDisable)(T * This, BOOL disable);                                               \
    HRESULT(STDMETHODCALLTYPE *SetBlueDisable)(T * This, BOOL disable);                                                \
    HRESULT(STDMETHODCALLTYPE *SetAlphaDisable)(T * This, BOOL disable);                                               \
    HRESULT(STDMETHODCALLTYPE *SetClampOutput)(T * This, BOOL clamp);                                                  \
    HRESULT(STDMETHODCALLTYPE *SetRedTableValueA)(T * This, UINT index, IDCompositionAnimation *animation);            \
    HRESULT(STDMETHODCALLTYPE *SetRedTableValue)(T * This, UINT index, float value);                                   \
    HRESULT(STDMETHODCALLTYPE *SetGreenTableValueA)(T * This, UINT index, IDCompositionAnimation *animation);          \
    HRESULT(STDMETHODCALLTYPE *SetGreenTableValue)(T * This, UINT index, float value);                                 \
    HRESULT(STDMETHODCALLTYPE *SetBlueTableValueA)(T * This, UINT index, IDCompositionAnimation *animation);           \
    HRESULT(STDMETHODCALLTYPE *SetBlueTableValue)(T * This, UINT index, float value);                                  \
    HRESULT(STDMETHODCALLTYPE *SetAlphaTableValueA)(T * This, UINT index, IDCompositionAnimation *animation);          \
    HRESULT(STDMETHODCALLTYPE *SetAlphaTableValue)(T * This, UINT index, float value);

#define IDCompositionGaussianBlurEffect_METHODS(T)                                                                     \
    IDCompositionFilterEffect_METHODS(T)                                                                               \
    DCOMP_FLOAT_PROPERTY(T, SetStandardDeviation)                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetBorderMode)(T * This, D2D1_BORDER_MODE mode);

#define IDCompositionColorMatrixEffect_METHODS(T)                                                                      \
    IDCompositionFilterEffect_METHODS(T)                                                                               \
    HRESULT(STDMETHODCALLTYPE *SetMatrix)(T * This, const D2D_MATRIX_5X4_F *matrix);                                   \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElementA)(T * This, int row, int column, IDCompositionAnimation *animation);   \
    HRESULT(STDMETHODCALLTYPE *SetMatrixElement)(T * This, int row, int column, float value);                          \
    HRESULT(STDMETHODCALLTYPE *SetAlphaMode)(T * This, D2D1_COLORMATRIX_ALPHA_MODE mode);                              \
    HRESULT(STDMETHODCALLTYPE *SetClampOutput)(T * This, BOOL clamp);

#define IDCompositionDevice3_METHODS(T)                                                                                \
    IDCompositionDevice2_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateGaussianBlurEffect)(T * This, IDCompositionGaussianBlurEffect * *effect);         \
    HRESULT(STDMETHODCALLTYPE *CreateBrightnessEffect)(T * This, void **effect);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateColorMatrixEffect)(T * This, IDCompositionColorMatrixEffect * *effect);           \
    HRESULT(STDMETHODCALLTYPE *CreateShadowEffect)(T * This, void **effect);                                           \
    HRESULT(STDMETHODCALLTYPE *CreateHueRotationEffect)(T * This, void **effect);                                      \
    HRESULT(STDMETHODCALLTYPE *CreateSaturationEffect)(T * This, IDCompositionSaturationEffect * *effect);             \
    HRESULT(STDMETHODCALLTYPE *CreateTurbulenceEffect)(T * This, void **effect);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateLinearTransferEffect)(T * This, void **effect);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateTableTransferEffect)(T * This, IDCompositionTableTransferEffect * *effect);       \
    HRESULT(STDMETHODCALLTYPE *CreateCompositeEffect)(T * This, void **effect);                                        \
    HRESULT(STDMETHODCALLTYPE *CreateBlendEffect)(T * This, void **effect);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateArithmeticCompositeEffect)(T * This, void **effect);                              \
    HRESULT(STDMETHODCALLTYPE *CreateAffineTransform2DEffect)(T * This, void **effect);

#define IDCompositionTexture_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetSourceRect)(T * This, const D2D_RECT_U *rect);                                       \
    HRESULT(STDMETHODCALLTYPE *SetColorSpace)(T * This, DXGI_COLOR_SPACE_TYPE color_space);                            \
    HRESULT(STDMETHODCALLTYPE *SetAlphaMode)(T * This, DXGI_ALPHA_MODE alpha);                                         \
    HRESULT(STDMETHODCALLTYPE *GetAvailableFence)(T * This, UINT64 * value, REFIID riid, void **fence);

#define IDCompositionDevice4_METHODS(T)                                                                                \
    IDCompositionDevice3_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *CheckCompositionTextureSupport)(T * This, IUnknown * device, BOOL * supported);         \
    HRESULT(STDMETHODCALLTYPE *CreateCompositionTexture)(T * This, IUnknown * texture,                                 \
                                                         IDCompositionTexture **composition_texture);

#define IDCompositionDynamicTexture_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetTextureWithRects)(T * This, IDCompositionTexture * texture,                          \
                                                    const D2D_RECT_L *rects, SIZE_T count);                            \
    HRESULT(STDMETHODCALLTYPE *SetTexture)(T * This, IDCompositionTexture * texture);

#define IDCompositionDevice5_METHODS(T)                                                                                \
    IDCompositionDevice4_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateDynamicTexture)(T * This, IDCompositionDynamicTexture * *texture);

/* ------------------------------------------------------------------ delegated ink */

#define IDCompositionDelegatedInkTrail_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddTrailPoints)(T * This, const DCompositionInkTrailPoint *points, UINT count,          \
                                               UINT *accepted);                                                        \
    HRESULT(STDMETHODCALLTYPE *AddTrailPointsWithPrediction)(T * This, const DCompositionInkTrailPoint *points,        \
                                                             UINT count, const DCompositionInkTrailPoint *predicted,   \
                                                             UINT predicted_count, UINT *accepted);                    \
    HRESULT(STDMETHODCALLTYPE *RemoveTrailPoints)(T * This, UINT count);                                               \
    HRESULT(STDMETHODCALLTYPE *StartNewTrail)(T * This, const D2D1_COLOR_F *color);

#define IDCompositionInkTrailDevice_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDelegatedInkTrail)(T * This, IDCompositionDelegatedInkTrail * *trail);            \
    HRESULT(STDMETHODCALLTYPE *CreateDelegatedInkTrailForSwapChain)(T * This, IUnknown * swap_chain,                   \
                                                                    IDCompositionDelegatedInkTrail **trail);

MIME_COM_INTERFACE(IDCompositionAnimation)
MIME_COM_INTERFACE(IDCompositionSurface)
MIME_COM_INTERFACE(IDCompositionVirtualSurface)
MIME_COM_INTERFACE(IDCompositionSurfaceFactory)
MIME_COM_INTERFACE(IDCompositionEffect)
MIME_COM_INTERFACE(IDCompositionTransform3D)
MIME_COM_INTERFACE(IDCompositionTransform)
MIME_COM_INTERFACE(IDCompositionTranslateTransform)
MIME_COM_INTERFACE(IDCompositionScaleTransform)
MIME_COM_INTERFACE(IDCompositionRotateTransform)
MIME_COM_INTERFACE(IDCompositionSkewTransform)
MIME_COM_INTERFACE(IDCompositionMatrixTransform)
MIME_COM_INTERFACE(IDCompositionTranslateTransform3D)
MIME_COM_INTERFACE(IDCompositionScaleTransform3D)
MIME_COM_INTERFACE(IDCompositionRotateTransform3D)
MIME_COM_INTERFACE(IDCompositionMatrixTransform3D)
MIME_COM_INTERFACE(IDCompositionEffectGroup)
MIME_COM_INTERFACE(IDCompositionClip)
MIME_COM_INTERFACE(IDCompositionRectangleClip)
MIME_COM_INTERFACE(IDCompositionVisual)
MIME_COM_INTERFACE(IDCompositionVisual2)
MIME_COM_INTERFACE(IDCompositionVisualDebug)
MIME_COM_INTERFACE(IDCompositionVisual3)
MIME_COM_INTERFACE(IDCompositionTarget)
MIME_COM_INTERFACE(IDCompositionDevice)
MIME_COM_INTERFACE(IDCompositionDevice2)
MIME_COM_INTERFACE(IDCompositionDesktopDevice)
MIME_COM_INTERFACE(IDCompositionDeviceDebug)
MIME_COM_INTERFACE(IDCompositionFilterEffect)
MIME_COM_INTERFACE(IDCompositionSaturationEffect)
MIME_COM_INTERFACE(IDCompositionTableTransferEffect)
MIME_COM_INTERFACE(IDCompositionGaussianBlurEffect)
MIME_COM_INTERFACE(IDCompositionColorMatrixEffect)
MIME_COM_INTERFACE(IDCompositionDevice3)
MIME_COM_INTERFACE(IDCompositionTexture)
MIME_COM_INTERFACE(IDCompositionDevice4)
MIME_COM_INTERFACE(IDCompositionDynamicTexture)
MIME_COM_INTERFACE(IDCompositionDevice5)
MIME_COM_INTERFACE(IDCompositionDelegatedInkTrail)
MIME_COM_INTERFACE(IDCompositionInkTrailDevice)

MIME_DEFINE_GUID(IID_IDCompositionAnimation, 0xCBFD91D9, 0x51B2, 0x45E4, 0xB3, 0xDE, 0xD1, 0x9C, 0xCF, 0xB8, 0x63, 0xC5);
MIME_DEFINE_GUID(IID_IDCompositionSurface, 0xBB8A4953, 0x2C99, 0x4F5A, 0x96, 0xF5, 0x48, 0x19, 0x02, 0x7F, 0xA3, 0xAC);
MIME_DEFINE_GUID(IID_IDCompositionVirtualSurface, 0xAE471C51, 0x5F53, 0x4A24, 0x8D, 0x3E, 0xD0, 0xC3, 0x9C, 0x30, 0xB3, 0xF0);
MIME_DEFINE_GUID(IID_IDCompositionEffect, 0xEC81B08F, 0xBFCB, 0x4E8D, 0xB1, 0x93, 0xA9, 0x15, 0x58, 0x79, 0x99, 0xE8);
MIME_DEFINE_GUID(IID_IDCompositionTransform3D, 0x71185722, 0x246B, 0x41F2, 0xAA, 0xD1, 0x04, 0x43, 0xF7, 0xF4, 0xBF, 0xC2);
MIME_DEFINE_GUID(IID_IDCompositionTransform, 0xFD55FAA7, 0x37E0, 0x4C20, 0x95, 0xD2, 0x9B, 0xE4, 0x5B, 0xC3, 0x3F, 0x55);
MIME_DEFINE_GUID(IID_IDCompositionTranslateTransform, 0x06791122, 0xC6F0, 0x417D, 0x83, 0x23, 0x26, 0x9E, 0x98, 0x7F, 0x59, 0x54);
MIME_DEFINE_GUID(IID_IDCompositionScaleTransform, 0x71FDE914, 0x40EF, 0x45EF, 0xBD, 0x51, 0x68, 0xB0, 0x37, 0xC3, 0x39, 0xF9);
MIME_DEFINE_GUID(IID_IDCompositionRotateTransform, 0x641ED83C, 0xAE96, 0x46C5, 0x90, 0xDC, 0x32, 0x77, 0x4C, 0xC5, 0xC6, 0xD5);
MIME_DEFINE_GUID(IID_IDCompositionSkewTransform, 0xE57AA735, 0xDCDB, 0x4C72, 0x9C, 0x61, 0x05, 0x91, 0xF5, 0x88, 0x89, 0xEE);
MIME_DEFINE_GUID(IID_IDCompositionMatrixTransform, 0x16CDFF07, 0xC503, 0x419C, 0x83, 0xF2, 0x09, 0x65, 0xC7, 0xAF, 0x1F, 0xA6);
MIME_DEFINE_GUID(IID_IDCompositionTranslateTransform3D, 0x91636D4B, 0x9BA1, 0x4532, 0xAA, 0xF7, 0xE3, 0x34, 0x49, 0x94, 0xD7, 0x88);
MIME_DEFINE_GUID(IID_IDCompositionScaleTransform3D, 0x2A9E9EAD, 0x364B, 0x4B15, 0xA7, 0xC4, 0xA1, 0x99, 0x7F, 0x78, 0xB3, 0x89);
MIME_DEFINE_GUID(IID_IDCompositionRotateTransform3D, 0xD8F5B23F, 0xD429, 0x4A91, 0xB5, 0x5A, 0xD2, 0xF4, 0x5F, 0xD7, 0x5B, 0x18);
MIME_DEFINE_GUID(IID_IDCompositionMatrixTransform3D, 0x4B3363F0, 0x643B, 0x41B7, 0xB6, 0xE0, 0xCC, 0xF2, 0x2D, 0x34, 0x46, 0x7C);
MIME_DEFINE_GUID(IID_IDCompositionEffectGroup, 0xA7929A74, 0xE6B2, 0x4BD6, 0x8B, 0x95, 0x40, 0x40, 0x11, 0x9C, 0xA3, 0x4D);
MIME_DEFINE_GUID(IID_IDCompositionClip, 0x64AC3703, 0x9D3F, 0x45EC, 0xA1, 0x09, 0x7C, 0xAC, 0x0E, 0x7A, 0x13, 0xA7);
MIME_DEFINE_GUID(IID_IDCompositionRectangleClip, 0x9842AD7D, 0xD9CF, 0x4908, 0xAE, 0xD7, 0x48, 0xB5, 0x1D, 0xA5, 0xE7, 0xC2);
MIME_DEFINE_GUID(IID_IDCompositionVisual, 0x4D93059D, 0x097B, 0x4651, 0x9A, 0x60, 0xF0, 0xF2, 0x51, 0x16, 0xE2, 0xF3);
MIME_DEFINE_GUID(IID_IDCompositionVisual2, 0xE8DE1639, 0x4331, 0x4B26, 0xBC, 0x5F, 0x6A, 0x32, 0x1D, 0x34, 0x7A, 0x85);
MIME_DEFINE_GUID(IID_IDCompositionVisualDebug, 0xFED2B808, 0x5EB4, 0x43A0, 0xAE, 0xA3, 0x35, 0xF6, 0x52, 0x80, 0xF9, 0x1B);
MIME_DEFINE_GUID(IID_IDCompositionVisual3, 0x2775F462, 0xB6C1, 0x4015, 0xB0, 0xBE, 0xB3, 0xE7, 0xD6, 0xA4, 0x97, 0x6D);
MIME_DEFINE_GUID(IID_IDCompositionTarget, 0xEACDD04C, 0x117E, 0x4E17, 0x88, 0xF4, 0xD1, 0xB1, 0x2B, 0x0E, 0x3D, 0x89);
MIME_DEFINE_GUID(IID_IDCompositionDevice, 0xC37EA93A, 0xE7AA, 0x450D, 0xB1, 0x6F, 0x97, 0x46, 0xCB, 0x04, 0x07, 0xF3);
MIME_DEFINE_GUID(IID_IDCompositionSurfaceFactory, 0xE334BC12, 0x3937, 0x4E02, 0x85, 0xEB, 0xFC, 0xF4, 0xEB, 0x30, 0xD2, 0xC8);
MIME_DEFINE_GUID(IID_IDCompositionDevice2, 0x75F6468D, 0x1B8E, 0x447C, 0x9B, 0xC6, 0x75, 0xFE, 0xA8, 0x0B, 0x5B, 0x25);
MIME_DEFINE_GUID(IID_IDCompositionDesktopDevice, 0x5F4633FE, 0x1E08, 0x4CB8, 0x8C, 0x75, 0xCE, 0x24, 0x33, 0x3F, 0x56, 0x02);
MIME_DEFINE_GUID(IID_IDCompositionDeviceDebug, 0xA1A3C64A, 0x224F, 0x4A81, 0x97, 0x73, 0x4F, 0x03, 0xA8, 0x9D, 0x3C, 0x6C);
MIME_DEFINE_GUID(IID_IDCompositionFilterEffect, 0x30C421D5, 0x8CB2, 0x4E9F, 0xB1, 0x33, 0x37, 0xBE, 0x27, 0x0D, 0x4A, 0xC2);
MIME_DEFINE_GUID(IID_IDCompositionSaturationEffect, 0xA08DEBDA, 0x3258, 0x4FA4, 0x9F, 0x16, 0x91, 0x74, 0xD3, 0xFE, 0x93, 0xB1);
MIME_DEFINE_GUID(IID_IDCompositionTableTransferEffect, 0x9B7E82E2, 0x69C5, 0x4EB4, 0xA5, 0xF5, 0xA7, 0x03, 0x3F, 0x51, 0x32, 0xCD);
MIME_DEFINE_GUID(IID_IDCompositionGaussianBlurEffect, 0x45D4D0B7, 0x1BD4, 0x454E, 0x88, 0x94, 0x2B, 0xFA, 0x68, 0x44, 0x30, 0x33);
MIME_DEFINE_GUID(IID_IDCompositionColorMatrixEffect, 0xC1170A22, 0x3CE2, 0x4966, 0x90, 0xD4, 0x55, 0x40, 0x8B, 0xFC, 0x84, 0xC4);
MIME_DEFINE_GUID(IID_IDCompositionDevice3, 0x0987CB06, 0xF916, 0x48BF, 0x8D, 0x35, 0xCE, 0x76, 0x41, 0x78, 0x1B, 0xD9);
MIME_DEFINE_GUID(IID_IDCompositionTexture, 0x929BB1AA, 0x725F, 0x433B, 0xAB, 0xD7, 0x27, 0x30, 0x75, 0xA8, 0x35, 0xF2);
MIME_DEFINE_GUID(IID_IDCompositionDevice4, 0x85FC5CCA, 0x2DA6, 0x494C, 0x86, 0xB6, 0x4A, 0x77, 0x5C, 0x04, 0x9B, 0x8A);
MIME_DEFINE_GUID(IID_IDCompositionDynamicTexture, 0xA1DE1D3F, 0x6405, 0x447F, 0x8E, 0x95, 0x13, 0x83, 0xA3, 0x4B, 0x02, 0x77);
MIME_DEFINE_GUID(IID_IDCompositionDevice5, 0x2C6BEBFE, 0xA603, 0x472F, 0xAF, 0x34, 0xD2, 0x44, 0x33, 0x56, 0xE6, 0x1B);
MIME_DEFINE_GUID(IID_IDCompositionDelegatedInkTrail, 0xC2448E9B, 0x547D, 0x4057, 0x8C, 0xF5, 0x81, 0x44, 0xED, 0xE1, 0xC2, 0xDA);
MIME_DEFINE_GUID(IID_IDCompositionInkTrailDevice, 0xDF0C7CEC, 0xCDEB, 0x4D4A, 0xB9, 0x1C, 0x72, 0x1B, 0xF2, 0x2F, 0x4E, 0x6C);

/* ------------------------------------------------------------------ exports */

#ifdef MIME_BUILD_DCOMP
#define DCOMPAPI
#else
#define DCOMPAPI DECLSPEC_IMPORT
#endif

DCOMPAPI HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device);
DCOMPAPI HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device);
DCOMPAPI HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device);
DCOMPAPI HRESULT WINAPI DCompositionCreateSurfaceHandle(DWORD access, SECURITY_ATTRIBUTES *attributes, HANDLE *handle);
DCOMPAPI HRESULT WINAPI DCompositionAttachMouseWheelToHwnd(IDCompositionVisual *visual, HWND hwnd, BOOL enable);
DCOMPAPI HRESULT WINAPI DCompositionAttachMouseDragToHwnd(IDCompositionVisual *visual, HWND hwnd, BOOL enable);
DCOMPAPI HRESULT WINAPI DCompositionGetFrameId(COMPOSITION_FRAME_ID_TYPE type, COMPOSITION_FRAME_ID *frame_id);
DCOMPAPI HRESULT WINAPI DCompositionGetStatistics(COMPOSITION_FRAME_ID frame_id, COMPOSITION_FRAME_STATS *stats,
                                                  UINT target_count, COMPOSITION_TARGET_ID *targets,
                                                  UINT *actual_target_count);
DCOMPAPI HRESULT WINAPI DCompositionGetTargetStatistics(COMPOSITION_FRAME_ID frame_id,
                                                        const COMPOSITION_TARGET_ID *target,
                                                        COMPOSITION_TARGET_STATS *stats);
DCOMPAPI HRESULT WINAPI DCompositionBoostCompositorClock(BOOL enable);
DCOMPAPI DWORD WINAPI DCompositionWaitForCompositorClock(UINT count, const HANDLE *handles, DWORD timeout);

#endif
