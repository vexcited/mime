/*
 * dcomp.dll: the objects a program builds with DirectComposition and the
 * compositor that draws them into windows. Everything lives in the calling
 * process: a device keeps the visual tree, targets are bound to windows,
 * and Commit makes the changes since the last one the tree the compositor
 * draws, which it does on the calling thread with the direct3d device the
 * device was made on.
 */
#ifndef DCOMP_PRIVATE_H
#define DCOMP_PRIVATE_H

#define MIME_BUILD_DCOMP
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/d3d11.h>
#include <mime/win32/dcomp.h>
#include <mime/dxgi_mime.h>
#include <mime/dcomp_mime.h>
#include <mime/vblank.h>

#define VTBL_FN(f) ((void *)(f))

#define E_ACCESSDENIED_HR ((HRESULT)0x80070005)
#define E_HANDLE_HR ((HRESULT)0x80070006)
#define RPC_E_WRONG_THREAD_HR ((HRESULT)0x8001010E)

#define DC_PI 3.14159265358979323846
#define DC_MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct DcDevice DcDevice;
typedef struct DcObject DcObject;
typedef struct DcAnimation DcAnimation;
typedef struct DcVisual DcVisual;
typedef struct DcTarget DcTarget;
typedef struct DcContent DcContent;
typedef struct DcSurface DcSurface;
typedef struct DcGpu DcGpu;

/* ------------------------------------------------------------------ objects */

typedef enum DcKind {
    DC_ANIMATION,
    DC_VISUAL,
    DC_TARGET,
    DC_TRANSFORM,
    DC_EFFECT_GROUP,
    DC_CLIP,
    DC_FILTER,
    DC_SURFACE,
    DC_TEXTURE,
    DC_DYNAMIC_TEXTURE,
    DC_FACTORY,
    DC_HANDLE_SURFACE
} DcKind;

typedef struct DcClass {
    DcKind kind;
    /* the committed state takes the pending one. */
    void (*commit)(DcObject *object);
    void (*destroy)(DcObject *object);
} DcClass;

/*
 * what every object has. refs counts the interface references of programs
 * and the ones the tree holds; the device is kept by each object.
 */
struct DcObject {
    const DcClass *cls;
    LONG refs;
    DcDevice *device;
    DcObject *dirty_next, *dirty_prev;
    BOOL dirty;
};

void DcObjectInit(DcObject *object, DcDevice *device, const DcClass *cls);
ULONG DcObjectAddRef(DcObject *object);
ULONG DcObjectRelease(DcObject *object);
/* marks the pending state as not yet committed; the device's lock is held. */
void DcObjectDirty(DcObject *object);
void DcObjectUndirty(DcObject *object);
/* the object behind an interface pointer of this dll, with a reference, or NULL. */
DcObject *DcObjectFrom(IUnknown *unknown);

MIME_DEFINE_GUID(IID_MimeDcObject, 0x2F1B6A90, 0x3C74, 0x4D58, 0x81, 0xE6, 0xA9, 0x47, 0x0B, 0x5C, 0xDE, 0x12);

/* queries an object for one of the interfaces it lists. */
typedef struct DcInterface {
    const GUID *iid;
    void *pointer;
} DcInterface;
HRESULT DcQuery(DcObject *object, const DcInterface *interfaces, UINT count, REFIID riid, void **out);

/* ------------------------------------------------------------------ animation */

typedef struct DcAnimPrimitive {
    UINT kind;
    UINT64 offset;
    UINT64 duration;
    float a, b, c, d;
} DcAnimPrimitive;

enum { DC_PRIM_CUBIC = 1, DC_PRIM_SINE, DC_PRIM_REPEAT, DC_PRIM_END, DC_PRIM_FREEZE };

typedef struct DcAnimData {
    DcAnimPrimitive *items;
    UINT count, capacity;
    BOOL ended, absolute;
    LARGE_INTEGER absolute_begin;
} DcAnimData;

struct DcAnimation {
    IDCompositionAnimation iface;
    DcObject base;
    DcAnimData pending, committed;
    /* when the committed data began: the commit that brought it, or the absolute time. */
    UINT64 begin;
};

extern const IDCompositionAnimationVtbl g_animation_vtbl;
HRESULT DcCreateAnimation(DcDevice *device, IDCompositionAnimation **out);
DcAnimation *DcAnimationFrom(IDCompositionAnimation *animation);
float DcAnimationValue(const DcAnimation *animation, UINT64 now);

/* a number a property holds, or an animation that gives it. */
typedef struct DcFloat {
    float value;
    DcAnimation *animation;
} DcFloat;

float DcFloatValue(const DcFloat *f, UINT64 now);
void DcFloatCopy(DcFloat *dst, const DcFloat *src);
void DcFloatClear(DcFloat *f);

/* ------------------------------------------------------------------ transforms, effects and clips */

typedef enum DcTransformKind {
    DCT_TRANSLATE,
    DCT_SCALE,
    DCT_ROTATE,
    DCT_SKEW,
    DCT_MATRIX,
    DCT_GROUP,
    DCT_TRANSLATE3D,
    DCT_SCALE3D,
    DCT_ROTATE3D,
    DCT_MATRIX3D,
    DCT_GROUP3D
} DcTransformKind;

#define DC_TRANSFORM_FLOATS 16

typedef struct DcTransformState {
    DcFloat p[DC_TRANSFORM_FLOATS];
    DcObject **list;
    UINT count;
} DcTransformState;

typedef struct DcTransform {
    union {
        IDCompositionTranslateTransform translate;
        IDCompositionScaleTransform scale;
        IDCompositionRotateTransform rotate;
        IDCompositionSkewTransform skew;
        IDCompositionMatrixTransform matrix;
        IDCompositionTransform group;
        IDCompositionTranslateTransform3D translate3d;
        IDCompositionScaleTransform3D scale3d;
        IDCompositionRotateTransform3D rotate3d;
        IDCompositionMatrixTransform3D matrix3d;
        IDCompositionTransform3D group3d;
        const void *vtbl;
    } iface;
    DcObject base;
    DcTransformKind kind;
    DcTransformState pending, committed;
} DcTransform;

typedef struct DcEffectGroup {
    IDCompositionEffectGroup iface;
    DcObject base;
    struct {
        DcFloat opacity;
        DcObject *transform3d;
    } pending, committed;
} DcEffectGroup;

#define DC_CLIP_FLOATS 12

typedef struct DcClip {
    IDCompositionRectangleClip iface;
    DcObject base;
    /* left, top, right, bottom, then the radii of the top left, top right, bottom left and bottom right corners. */
    DcFloat pending[DC_CLIP_FLOATS], committed[DC_CLIP_FLOATS];
} DcClip;

typedef enum DcFilterKind { DCF_SATURATION, DCF_COLOR_MATRIX, DCF_TABLE, DCF_BLUR } DcFilterKind;

typedef struct DcFilterState {
    DcFloat value[20];
    float table[4][64];
    UINT table_count[4];
    BOOL table_disable[4], clamp;
    UINT alpha_mode;
    UINT border_mode;
} DcFilterState;

typedef struct DcFilter {
    union {
        IDCompositionSaturationEffect saturation;
        IDCompositionColorMatrixEffect color_matrix;
        IDCompositionTableTransferEffect table;
        IDCompositionGaussianBlurEffect blur;
        const void *vtbl;
    } iface;
    DcObject base;
    DcFilterKind kind;
    DcFilterState pending, committed;
} DcFilter;

HRESULT DcCreateTransform(DcDevice *device, DcTransformKind kind, void **out);
HRESULT DcCreateTransformGroup(DcDevice *device, BOOL three_d, void *const *transforms, UINT count, void **out);
HRESULT DcCreateEffectGroup(DcDevice *device, IDCompositionEffectGroup **out);
HRESULT DcCreateRectangleClip(DcDevice *device, IDCompositionRectangleClip **out);
HRESULT DcCreateFilter(DcDevice *device, DcFilterKind kind, void **out);

/* the matrix a committed transform makes at a time, in the row vector convention direct3d has. */
BOOL DcTransformMatrix(const DcObject *transform, UINT64 now, float m[16]);
BOOL DcEffectOpacity(const DcObject *effect, UINT64 now, float *opacity, float transform3d[16], BOOL *has_transform);
/* the color transform a committed filter effect applies, as five rows of four: a 4x4 matrix and an offset. */
BOOL DcFilterColor(const DcObject *effect, UINT64 now, float color[20]);
BOOL DcClipValues(const DcObject *clip, UINT64 now, float rect[4], float radii[8]);

/* ------------------------------------------------------------------ visuals and targets */

typedef struct DcVisualState {
    DcFloat offset_x, offset_y, offset_z, opacity;
    /* the transform: none, an object, a matrix of 3 by 2 or one of 4 by 4. */
    int transform_kind;
    DcObject *transform;
    float matrix[16];
    DcVisual *transform_parent;
    DcObject *effect;
    int interpolation, border_mode, composite_mode, opacity_mode, backface, depth_mode;
    BOOL visible;
    int clip_kind;
    D2D_RECT_F clip_rect;
    DcObject *clip;
    DcContent *content;
    DcVisual **children;
    UINT child_count, child_capacity;
} DcVisualState;

struct DcVisual {
    IDCompositionVisual3 iface;
    DcObject base;
    DcVisualState pending, committed;
    /* the visual this one is a child of in the pending tree. */
    DcVisual *parent;
};

extern const IDCompositionVisual3Vtbl g_visual_vtbl;
HRESULT DcCreateVisual(DcDevice *device, IDCompositionVisual3 **out);

/* the compositor's state for a window: a swap chain of the device that draws it, and its back buffer. */
typedef struct DcOutput {
    ID3D11Device *device;
    IDXGISwapChain1 *swapchain;
    ID3D11Texture2D *back;
    ID3D11RenderTargetView *rtv;
    UINT width, height;
    BOOL composed;
} DcOutput;

struct DcTarget {
    IDCompositionTarget iface;
    DcObject base;
    HWND hwnd;
    BOOL topmost;
    DcVisual *root_pending, *root_committed;
    DcOutput output;
    BOOL needs_compose;
    DcTarget *next_in_device;
    DcTarget *next_global;
};

HRESULT DcCreateTarget(DcDevice *device, HWND hwnd, BOOL topmost, IDCompositionTarget **out);

/* ------------------------------------------------------------------ content */

typedef enum DcContentKind { DCC_SWAPCHAIN, DCC_SURFACE, DCC_TEXTURE, DCC_DYNAMIC } DcContentKind;

/*
 * what a visual shows, shared by the visuals of a device that show the same
 * thing: the swap chain's frames, a surface's texture, or a texture of the
 * program's. The shader view of the texture is kept as long as the texture is.
 */
struct DcContent {
    LONG refs;
    DcDevice *device;
    DcContent *next;
    DcContentKind kind;
    /* what the device knows it by: the swap chain's source or the object. */
    void *identity;
    DcObject *object;
    IMimeDXGICompositionSource *source;
    /* the view of the texture last used, and the texture it was made for. */
    IUnknown *texture;
    ID3D11ShaderResourceView *view;
    DXGI_FORMAT view_format;
};

DcContent *DcContentRef(DcContent *content);
void DcContentUnref(DcContent *content);
HRESULT DcContentForObject(DcDevice *device, IUnknown *object, DcContent **out);

/* a frame of the content for the compositor: the view, its size, the part shown and how it blends. */
typedef struct DcFrame {
    ID3D11ShaderResourceView *view;
    UINT width, height;
    RECT source;
    DXGI_ALPHA_MODE alpha;
    float matrix[6];
    BOOL has_matrix;
} DcFrame;

BOOL DcContentFrame(DcContent *content, ID3D11Device *device, DcFrame *frame);

/* ------------------------------------------------------------------ surfaces */

struct DcSurface {
    IDCompositionVirtualSurface iface;
    DcObject base;
    BOOL is_virtual;
    UINT width, height;
    DXGI_FORMAT format;
    DXGI_ALPHA_MODE alpha;
    /* 0 idle, 1 drawing, 2 suspended. */
    int state;
    RECT update;
    ID3D11Device *d3d;
    ID3D11Texture2D *texture;
    UINT texture_width, texture_height;
};

/* a surface factory draws surfaces with the direct3d device it was given. */
typedef struct DcFactory {
    IDCompositionSurfaceFactory iface;
    DcObject base;
    ID3D11Device *d3d;
} DcFactory;

HRESULT DcCreateSurface(DcDevice *device, ID3D11Device *d3d, BOOL is_virtual, UINT width, UINT height,
                        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha, void **out);
HRESULT DcCreateSurfaceFactory(DcDevice *device, ID3D11Device *d3d, IDCompositionSurfaceFactory **out);
HRESULT DcCreateTexture(DcDevice *device, IUnknown *texture, IDCompositionTexture **out);
HRESULT DcCreateDynamicTexture(DcDevice *device, IDCompositionDynamicTexture **out);
HRESULT DcCreateSurfaceFromHandle(DcDevice *device, HANDLE handle, IUnknown **out);
HRESULT DcResolveRenderingDevice(IUnknown *rendering_device, ID3D11Device **d3d);

/* ------------------------------------------------------------------ the device */

/* an interface of the device is a pointer to a table and the device it belongs to. */
typedef struct DcDeviceFace {
    const void *lpVtbl;
    DcDevice *device;
} DcDeviceFace;

struct DcDevice {
    DcDeviceFace dev1, desktop, dev5, debug, mime;
    LONG refs;
    UINT version;
    CRITICAL_SECTION lock;
    /* the direct3d device surfaces and the compositor draw with; none when the program gave none. */
    ID3D11Device *d3d;
    DcObject *dirty;
    BOOL content_changed;
    DcTarget *targets;
    DcContent *contents;
    DcGpu *gpu;
    UINT64 commit_count;
    DcDevice *next_global;
};

void DcDeviceLock(DcDevice *device);
void DcDeviceUnlock(DcDevice *device);
HRESULT DcDeviceCreate(UINT version, IUnknown *rendering_device, DcDevice **out);
ULONG DcDeviceAddRef(DcDevice *device);
ULONG DcDeviceRelease(DcDevice *device);
HRESULT DcDeviceCommit(DcDevice *device);
HRESULT DcDeviceQuery(DcDevice *device, REFIID riid, void **out);
/* whether the device is still alive, adding a reference if so: for callbacks that outlive their registration. */
DcDevice *DcDeviceTryRef(DcDevice *device);

extern const IDCompositionDeviceVtbl g_device1_vtbl;
extern const IDCompositionDesktopDeviceVtbl g_desktop_vtbl;
extern const IDCompositionDevice5Vtbl g_device5_vtbl;
extern const IDCompositionDeviceDebugVtbl g_debug_vtbl;
extern const IMimeDCompositionDeviceVtbl g_mime_vtbl;

/* ------------------------------------------------------------------ the compositor */

/* draws the committed tree of a target into its window. */
HRESULT DcCompose(DcTarget *target);
void DcComposeAll(DcDevice *device, BOOL only_flagged);
void DcOutputRelease(DcOutput *output);
void DcGpuRelease(DcDevice *device);
HRESULT DcReadTarget(DcTarget *target, const RECT *rect, void *pixels, UINT pitch);
/* a frame of a content source arrived: compose the targets that show it. */
void DcSourceFrame(DcDevice *device, IUnknown *source);

/* ------------------------------------------------------------------ matrices, clocks and the registry */

double DcSin(double x);
double DcCos(double x);
double DcTan(double x);
double DcSqrt(double x);
void DcMatrixIdentity(float m[16]);
void DcMatrixMultiply(float out[16], const float a[16], const float b[16]);
void DcMatrixFrom3x2(float m[16], const D2D_MATRIX_3X2_F *matrix);

UINT64 DcNow(void);
UINT64 DcFrequency(void);

HRESULT DcResultFromStatus(LONG status);
void DcListenerAdd(DcDevice *device);
void DcListenerRemove(DcDevice *device);
void WINAPI DcSourceNotify(void *context, IUnknown *source);
void DcObjectCountChange(LONG delta);

#endif
