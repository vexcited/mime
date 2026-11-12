/*
 * Windows.UI.Composition, which Windows serves from dcomp.dll too. a compositor owns a DirectComposition device: a
 * desktop window target is a target of that device on the window, each visual one of its visuals, and a sprite shows
 * its brush through the visual's layout, a swap chain or surface fitted in its size or a solid color. the classes
 * keep the properties a program sets and turn them into the device's tree at each commit: on the dispatcher queue
 * after a change, or when a compositor controller says so.
 *
 * every interface is a table of functions in the order of its metadata. the ones a program has not needed are
 * filled with a function that logs the object and the slot and answers E_NOTIMPL, so a program that wants more shows
 * what in the log.
 *
 * the classes are for 64 bit processes. a function of a 32 bit table takes back its own arguments, which a log only
 * stub cannot know, so the classes are not served to 32 bit programs.
 */
#include "dcomp_private.h"

#ifdef _WIN64

#include <mime/win32/roapi.h>
#include "winrt/winrt.h"

MIME_DEFINE_GUID(IID_ICompositorController, 0x2D75F35A, 0x70A7, 0x4395, 0xBA, 0x2D, 0xCE, 0xF0, 0xB1, 0x83, 0x99, 0xF9);
MIME_DEFINE_GUID(IID_ICompositor, 0xB403CA50, 0x7F8C, 0x4E83, 0x98, 0x5F, 0xCC, 0x45, 0x06, 0x00, 0x36, 0xD8);
MIME_DEFINE_GUID(IID_ICompositorInterop, 0x25297D5C, 0x3AD4, 0x4C9C, 0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30);
MIME_DEFINE_GUID(IID_ICompositorDesktopInterop, 0x29E691FA, 0x4567, 0x4DCA, 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07);
MIME_DEFINE_GUID(IID_ICompositionObject, 0xBCB4AD45, 0x7609, 0x4550, 0x93, 0x4F, 0x16, 0x00, 0x2A, 0x68, 0xFD, 0xED);
MIME_DEFINE_GUID(IID_ICompositionObject2, 0xEF874EA1, 0x5CFF, 0x4B68, 0x9E, 0x30, 0xA1, 0x51, 0x9D, 0x08, 0xBA, 0x03);
MIME_DEFINE_GUID(IID_ICompositionObject3, 0x4BC27925, 0xDACD, 0x4CF2, 0x98, 0xB1, 0x98, 0x6B, 0x76, 0xE7, 0xEB, 0xE6);
MIME_DEFINE_GUID(IID_ICompositionObject4, 0x0BB3784C, 0x346B, 0x4A7C, 0x96, 0x6B, 0x73, 0x10, 0x96, 0x65, 0x53, 0xD5);
MIME_DEFINE_GUID(IID_ICompositionTarget, 0xA1BEA8BA, 0xD726, 0x4663, 0x81, 0x29, 0x6B, 0x5E, 0x79, 0x27, 0xFF, 0xA6);
MIME_DEFINE_GUID(IID_IDesktopWindowTarget, 0x6329D6CA, 0x3366, 0x490E, 0x9D, 0xB3, 0x25, 0x31, 0x29, 0x29, 0xAC, 0x51);
MIME_DEFINE_GUID(IID_IVisual, 0x117E202D, 0xA859, 0x4C89, 0x87, 0x3B, 0xC2, 0xAA, 0x56, 0x67, 0x88, 0xE3);
MIME_DEFINE_GUID(IID_IVisual2, 0x3052B611, 0x56C3, 0x4C3E, 0x8B, 0xF3, 0xF6, 0xE1, 0xAD, 0x47, 0x3F, 0x06);
MIME_DEFINE_GUID(IID_IVisual3, 0x30BE580D, 0xF4B6, 0x4AB7, 0x80, 0xDD, 0x37, 0x38, 0xCB, 0xAC, 0x9F, 0x2C);
MIME_DEFINE_GUID(IID_IContainerVisual, 0x02F6BC74, 0xED20, 0x4773, 0xAF, 0xE6, 0xD4, 0x9B, 0x4A, 0x93, 0xDB, 0x32);
MIME_DEFINE_GUID(IID_ISpriteVisual, 0x08E05581, 0x1AD1, 0x4F97, 0x97, 0x57, 0x40, 0x2D, 0x76, 0xE4, 0x23, 0x3B);
MIME_DEFINE_GUID(IID_IVisualCollection, 0x8B745505, 0xFD3E, 0x4A98, 0x84, 0xA8, 0xE9, 0x49, 0x46, 0x8C, 0x6B, 0xCB);
MIME_DEFINE_GUID(IID_ICompositionBrush, 0xAB0D7608, 0x30C0, 0x40E9, 0xB5, 0x68, 0xB6, 0x0A, 0x6B, 0xD1, 0xFB, 0x46);
MIME_DEFINE_GUID(IID_ICompositionColorBrush, 0x2B264C5E, 0xBF35, 0x4831, 0x86, 0x42, 0xCF, 0x70, 0xC2, 0x0F, 0xFF, 0x2F);
MIME_DEFINE_GUID(IID_ICompositionSurfaceBrush, 0xAD016D79, 0x1E4C, 0x4C0D, 0x9C, 0x29, 0x83, 0x33, 0x8C, 0x87, 0xC1,
                 0x62);
MIME_DEFINE_GUID(IID_ICompositionSurfaceBrush2, 0xD27174D5, 0x64F5, 0x4692, 0x9D, 0xC7, 0x71, 0xB6, 0x1D, 0x7E, 0x58,
                 0x80);
MIME_DEFINE_GUID(IID_ICompositionClip, 0x1CCD2A52, 0xCFC7, 0x4ACE, 0x99, 0x83, 0x14, 0x6B, 0xB8, 0xEB, 0x6A, 0x3C);
MIME_DEFINE_GUID(IID_ICompositionClip2, 0x5893E069, 0x3516, 0x40E1, 0x89, 0xE0, 0x5B, 0xA9, 0x24, 0x92, 0x72, 0x35);
MIME_DEFINE_GUID(IID_IInsetClip, 0x1E73E647, 0x84C7, 0x477A, 0xB4, 0x74, 0x58, 0x80, 0xE0, 0x44, 0x2E, 0x15);
MIME_DEFINE_GUID(IID_ICompositionSurface, 0x1527540D, 0x42C7, 0x47A6, 0xA4, 0x08, 0x66, 0x8F, 0x79, 0xA9, 0x0D, 0xFB);
MIME_DEFINE_GUID(IID_ICompositionGraphicsDevice, 0xFB22C6E1, 0x80A2, 0x4667, 0x99, 0x36, 0xDB, 0xEA, 0xF6, 0xEE, 0xFE,
                 0x95);
MIME_DEFINE_GUID(IID_ICompositionGraphicsDevice2, 0x0FB8BDF6, 0xC0F0, 0x4BCC, 0x9F, 0xB8, 0x08, 0x49, 0x82, 0x49, 0x0D,
                 0x7D);
MIME_DEFINE_GUID(IID_ICompositionGraphicsDeviceInterop, 0xA116FF71, 0xF8BF, 0x4C8A, 0x9C, 0x98, 0x70, 0x77, 0x9A, 0x32,
                 0xA9, 0xC8);
MIME_DEFINE_GUID(IID_ICompositionDrawingSurface, 0xA166C300, 0xFAD0, 0x4D11, 0x9E, 0x67, 0xE4, 0x33, 0x16, 0x2F, 0xF4,
                 0x9E);
MIME_DEFINE_GUID(IID_ICompositionDrawingSurface2, 0xFAD0E88B, 0xE354, 0x44E8, 0x8E, 0x3D, 0xC4, 0x88, 0x0D, 0x5A, 0x21,
                 0x3F);
MIME_DEFINE_GUID(IID_ICompositionDrawingSurfaceInterop, 0xFD04E6E3, 0xFE0C, 0x4C3C, 0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5,
                 0x76, 0xEE);
MIME_DEFINE_GUID(IID_IClosable_, 0x30D5A829, 0x7FA4, 0x4026, 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E);
MIME_DEFINE_GUID(IID_IDispatcherQueueHandler_, 0xDFA2DC9C, 0x1A2D, 0x4917, 0x98, 0xF2, 0x93, 0x9A, 0xF1, 0xD6, 0xE0,
                 0xC8);
MIME_DEFINE_GUID(IID_IDispatcherQueueStatics_, 0xA96D83D7, 0x9371, 0x4517, 0x92, 0x45, 0xD0, 0x82, 0x4A, 0xC1, 0x2C,
                 0x74);
/* answered by the objects of this file only, with the object itself. */
MIME_DEFINE_GUID(IID_MimeWuc, 0x7C3E91A4, 0x2B6D, 0x4F80, 0x9A, 0x15, 0xE8, 0x4D, 0x60, 0x2F, 0xB3, 0x7C);

#define RPC_E_WRONG_THREAD_ ((HRESULT)0x8001010EL)

typedef struct {
    float X, Y;
} VECTOR2;

typedef struct {
    float X, Y, Z;
} VECTOR3;

typedef struct {
    float X, Y, Z, W;
} QUATERNION;

typedef struct {
    float M[16];
} MATRIX4X4;

typedef struct {
    BYTE A, R, G, B;
} WUC_COLOR;

typedef struct {
    float Width, Height;
} WUC_SIZE;

typedef struct {
    INT32 Width, Height;
} SIZE_INT32;

/* the queue of a thread, called through its table: TryEnqueue is the eighth slot. */
typedef struct WUC_QUEUE WUC_QUEUE;
typedef struct {
    MIME_INSPECTABLE_METHODS(WUC_QUEUE)
    HRESULT(STDMETHODCALLTYPE *CreateTimer)(WUC_QUEUE *This, void **timer);
    HRESULT(STDMETHODCALLTYPE *TryEnqueue)(WUC_QUEUE *This, IUnknown *handler, BOOLEAN *queued);
} WUC_QUEUE_VTBL;
struct WUC_QUEUE {
    const WUC_QUEUE_VTBL *lpVtbl;
};

typedef struct WUC_QUEUE_STATICS WUC_QUEUE_STATICS;
typedef struct {
    MIME_INSPECTABLE_METHODS(WUC_QUEUE_STATICS)
    HRESULT(STDMETHODCALLTYPE *GetForCurrentThread)(WUC_QUEUE_STATICS *This, WUC_QUEUE **queue);
} WUC_QUEUE_STATICS_VTBL;
struct WUC_QUEUE_STATICS {
    const WUC_QUEUE_STATICS_VTBL *lpVtbl;
};

/* ------------------------------------------------------------------ interfaces */

enum {
    I_CONTROLLER,
    I_COMPOSITOR,
    I_COMPOSITOR_INTEROP,
    I_DESKTOP_INTEROP,
    I_OBJECT,
    I_OBJECT2,
    I_OBJECT3,
    I_OBJECT4,
    I_CLOSABLE,
    I_TARGET,
    I_DESKTOP_TARGET,
    I_VISUAL,
    I_VISUAL2,
    I_VISUAL3,
    I_CONTAINER,
    I_SPRITE,
    I_COLLECTION,
    I_BRUSH,
    I_COLOR_BRUSH,
    I_SURFACE_BRUSH,
    I_SURFACE_BRUSH2,
    I_CLIP,
    I_CLIP2,
    I_INSET_CLIP,
    I_SURFACE,
    I_GRAPHICS_DEVICE,
    I_GRAPHICS_DEVICE2,
    I_GRAPHICS_INTEROP,
    I_DRAWING_SURFACE,
    I_DRAWING_SURFACE2,
    I_DRAWING_INTEROP,
    I_HANDLER,
    I_FACTORY,
    I_COUNT
};

#define MAX_SLOTS 64
#define MAX_FACES 12

/* how many slots each table has, the inspectable ones counting their six. com interop interfaces are hidden. */
static const struct {
    const IID *Iid;
    ULONG Slots;
    BOOLEAN Inspectable;
} g_interfaces[I_COUNT] = {
    [I_CONTROLLER] = {&IID_ICompositorController, 11, TRUE},
    [I_COMPOSITOR] = {&IID_ICompositor, 30, TRUE},
    [I_COMPOSITOR_INTEROP] = {&IID_ICompositorInterop, 6, FALSE},
    [I_DESKTOP_INTEROP] = {&IID_ICompositorDesktopInterop, 5, FALSE},
    [I_OBJECT] = {&IID_ICompositionObject, 11, TRUE},
    [I_OBJECT2] = {&IID_ICompositionObject2, 12, TRUE},
    [I_OBJECT3] = {&IID_ICompositionObject3, 7, TRUE},
    [I_OBJECT4] = {&IID_ICompositionObject4, 7, TRUE},
    [I_CLOSABLE] = {&IID_IClosable_, 7, TRUE},
    [I_TARGET] = {&IID_ICompositionTarget, 8, TRUE},
    [I_DESKTOP_TARGET] = {&IID_IDesktopWindowTarget, 7, TRUE},
    [I_VISUAL] = {&IID_IVisual, 39, TRUE},
    [I_VISUAL2] = {&IID_IVisual2, 12, TRUE},
    [I_VISUAL3] = {&IID_IVisual3, 8, TRUE},
    [I_CONTAINER] = {&IID_IContainerVisual, 7, TRUE},
    [I_SPRITE] = {&IID_ISpriteVisual, 8, TRUE},
    [I_COLLECTION] = {&IID_IVisualCollection, 13, TRUE},
    [I_BRUSH] = {&IID_ICompositionBrush, 6, TRUE},
    [I_COLOR_BRUSH] = {&IID_ICompositionColorBrush, 8, TRUE},
    [I_SURFACE_BRUSH] = {&IID_ICompositionSurfaceBrush, 16, TRUE},
    [I_SURFACE_BRUSH2] = {&IID_ICompositionSurfaceBrush2, 20, TRUE},
    [I_CLIP] = {&IID_ICompositionClip, 6, TRUE},
    [I_CLIP2] = {&IID_ICompositionClip2, 20, TRUE},
    [I_INSET_CLIP] = {&IID_IInsetClip, 14, TRUE},
    [I_SURFACE] = {&IID_ICompositionSurface, 6, TRUE},
    [I_GRAPHICS_DEVICE] = {&IID_ICompositionGraphicsDevice, 9, TRUE},
    [I_GRAPHICS_DEVICE2] = {&IID_ICompositionGraphicsDevice2, 8, TRUE},
    [I_GRAPHICS_INTEROP] = {&IID_ICompositionGraphicsDeviceInterop, 5, FALSE},
    [I_DRAWING_SURFACE] = {&IID_ICompositionDrawingSurface, 9, TRUE},
    [I_DRAWING_SURFACE2] = {&IID_ICompositionDrawingSurface2, 12, TRUE},
    [I_DRAWING_INTEROP] = {&IID_ICompositionDrawingSurfaceInterop, 9, FALSE},
    [I_HANDLER] = {&IID_IDispatcherQueueHandler_, 4, FALSE},
    [I_FACTORY] = {&IID_IActivationFactory, 7, TRUE},
};

/* ------------------------------------------------------------------ objects */

typedef enum {
    K_CONTROLLER,
    K_COMPOSITOR,
    K_TARGET,
    K_CONTAINER,
    K_SPRITE,
    K_COLLECTION,
    K_COLOR_BRUSH,
    K_SURFACE_BRUSH,
    K_INSET_CLIP,
    K_SURFACE,
    K_GRAPHICS_DEVICE,
    K_DRAWING_SURFACE,
    K_HANDLER,
    K_FACTORY,
    K_COUNT
} KIND;

#define OBJECT_IFACES I_OBJECT, I_OBJECT2, I_OBJECT3, I_OBJECT4, I_CLOSABLE

static const struct {
    LPCWSTR Name;
    int Ifaces[MAX_FACES];
} g_kinds[K_COUNT] = {
    [K_CONTROLLER] = {L"Windows.UI.Composition.Core.CompositorController", {I_CONTROLLER, I_CLOSABLE, -1}},
    [K_COMPOSITOR] = {L"Windows.UI.Composition.Compositor",
                      {I_COMPOSITOR, I_COMPOSITOR_INTEROP, I_DESKTOP_INTEROP, I_OBJECT, I_CLOSABLE, -1}},
    [K_TARGET] = {L"Windows.UI.Composition.Desktop.DesktopWindowTarget",
                  {I_DESKTOP_TARGET, I_TARGET, OBJECT_IFACES, -1}},
    [K_CONTAINER] = {L"Windows.UI.Composition.ContainerVisual",
                     {I_CONTAINER, I_VISUAL, I_VISUAL2, I_VISUAL3, OBJECT_IFACES, -1}},
    [K_SPRITE] = {L"Windows.UI.Composition.SpriteVisual",
                  {I_SPRITE, I_CONTAINER, I_VISUAL, I_VISUAL2, I_VISUAL3, OBJECT_IFACES, -1}},
    [K_COLLECTION] = {L"Windows.UI.Composition.VisualCollection", {I_COLLECTION, OBJECT_IFACES, -1}},
    [K_COLOR_BRUSH] = {L"Windows.UI.Composition.CompositionColorBrush",
                       {I_COLOR_BRUSH, I_BRUSH, OBJECT_IFACES, -1}},
    [K_SURFACE_BRUSH] = {L"Windows.UI.Composition.CompositionSurfaceBrush",
                         {I_SURFACE_BRUSH, I_SURFACE_BRUSH2, I_BRUSH, OBJECT_IFACES, -1}},
    [K_INSET_CLIP] = {L"Windows.UI.Composition.InsetClip", {I_INSET_CLIP, I_CLIP, I_CLIP2, OBJECT_IFACES, -1}},
    /* the surfaces of swap chains and handles are no class of the metadata, and have no name. */
    [K_SURFACE] = {nullptr, {I_SURFACE, -1}},
    [K_GRAPHICS_DEVICE] = {L"Windows.UI.Composition.CompositionGraphicsDevice",
                           {I_GRAPHICS_DEVICE, I_GRAPHICS_DEVICE2, I_GRAPHICS_INTEROP, OBJECT_IFACES, -1}},
    [K_DRAWING_SURFACE] = {L"Windows.UI.Composition.CompositionDrawingSurface",
                           {I_DRAWING_SURFACE, I_DRAWING_SURFACE2, I_SURFACE, I_DRAWING_INTEROP, OBJECT_IFACES, -1}},
    [K_HANDLER] = {nullptr, {I_HANDLER, -1}},
    [K_FACTORY] = {nullptr, {I_FACTORY, -1}},
};

typedef struct WUC WUC;

typedef struct {
    const void *Vtbl;
    WUC *Owner;
} FACE;

typedef struct {
    VECTOR2 Anchor;
    int Backface, Border, Composite;
    VECTOR3 Center;
    BOOLEAN Visible, HitTest;
    VECTOR3 Offset;
    float Opacity;
    QUATERNION Orientation;
    float Angle;
    VECTOR3 Axis;
    VECTOR3 Scale;
    VECTOR2 Size;
    MATRIX4X4 Matrix;
    VECTOR3 RelativeOffset;
    VECTOR2 RelativeSize;
} PROPS;

/* what a visual last gave its DirectComposition visual. */
typedef struct {
    BOOL Valid;
    D2D_MATRIX_4X4_F Matrix;
    float Opacity;
    BOOL Visible, Clipped;
    int Composite, Border, Backface;
    D2D_RECT_F Clip;
} PUSHED;

struct WUC {
    FACE Faces[MAX_FACES];
    volatile LONG Refs;
    KIND Kind;
    BOOL Closed;
    /* the compositor of the object, referenced. */
    WUC *Compositor;

    /* a compositor: its device and queue, its controller and its targets, which it does not hold. */
    DcDevice *Device;
    WUC_QUEUE *Queue;
    WUC *Controller;
    WUC *Targets;
    BOOL Scheduled;

    /* a controller holds its compositor. */
    WUC *Owned;
    WRT_EVENT CommitNeeded;

    /* a target. */
    IDCompositionTarget *Target;
    HWND Window;
    BOOLEAN Topmost;
    WUC *Root;
    WUC *NextTarget;
    IDCompositionVisual *PushedRoot;

    /* a visual: its parent does not hold it, it holds its children. */
    IDCompositionVisual3 *Visual;
    WUC *Parent;
    WUC **Children;
    ULONG ChildCount, ChildCapacity;
    BOOL ChildrenChanged;
    PROPS P;
    WUC *Clip, *Brush, *TransformParent;
    PUSHED Pushed;
    IUnknown *PushedContent;

    /* a collection holds its container. */
    WUC *Container;

    /* brushes. */
    WUC_COLOR Color;
    int Stretch, Interpolation;
    float AlignX, AlignY;
    WUC *Surface;

    /* an inset clip: left, top, right and bottom. */
    float Insets[4];

    /* a surface: the swap chain, the handle surface or the DirectComposition surface it shows. */
    IUnknown *Content;
    /* a drawing surface's device, size and formats. */
    WUC *Graphics;
    SIZE_INT32 Pixels;
    DXGI_FORMAT Format;
    DXGI_ALPHA_MODE Alpha;

    /* a graphics device. */
    IUnknown *Rendering;
    ID3D11Device *D3d;
    ID2D1Device *D2d;

    /* a factory makes compositors, or controllers with theirs. */
    BOOLEAN MakesController;
};

static const void *g_tables[I_COUNT][MAX_SLOTS];
static INIT_ONCE g_tables_once = INIT_ONCE_STATIC_INIT;

static WUC *wuc_of(void *iface)
{
    return ((FACE *)iface)->Owner;
}

static void *face(WUC *object)
{
    return object ? &object->Faces[0] : nullptr;
}

static void add_ref(WUC *object)
{
    if (object)
        InterlockedIncrement(&object->Refs);
}

static void destroy(WUC *object);

static void release_wuc(WUC *object)
{
    if (object && !InterlockedDecrement(&object->Refs))
        destroy(object);
}

/* the object of this file behind an interface pointer, borrowed from the caller's reference. */
static WUC *own(void *iface)
{
    WUC *object = nullptr;
    if (!iface || FAILED(((IUnknown *)iface)->lpVtbl->QueryInterface(iface, &IID_MimeWuc, (void **)&object)))
        return nullptr;
    release_wuc(object);
    return object;
}

static BOOL is_visual(const WUC *object)
{
    return object && (object->Kind == K_CONTAINER || object->Kind == K_SPRITE);
}

static WUC *compositor_of(WUC *object)
{
    return object->Kind == K_COMPOSITOR ? object : object->Kind == K_CONTROLLER ? object->Owned : object->Compositor;
}

static void lock(WUC *object)
{
    DcDeviceLock(compositor_of(object)->Device);
}

static void unlock(WUC *object)
{
    DcDeviceUnlock(compositor_of(object)->Device);
}

static BOOL finite(float v)
{
    return v - v == 0.0f;
}

/* ------------------------------------------------------------------ unimplemented slots */

static const char *const g_kind_names[K_COUNT] = {
    "controller", "compositor", "target",  "container",       "sprite",          "collection", "color brush",
    "surface brush", "inset clip", "surface", "graphics device", "drawing surface", "handler",    "factory"};

static HRESULT unimplemented(void *This, ULONG slot)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) < 400) {
        char text[120];
        WUC *object = wuc_of(This);
        int at = (int)((FACE *)This - object->Faces);
        int iface = g_kinds[object->Kind].Ifaces[at];
        const IID *iid = g_interfaces[iface].Iid;
        _snprintf(text, sizeof(text), "composition: %s %08lX slot %u is not implemented\n",
                  g_kind_names[object->Kind], iid->Data1, slot);
        OutputDebugStringA(text);
    }
    return E_NOTIMPL;
}

#define ALL_SLOTS(X)                                                                                                   \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) \
    X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) \
    X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) \
    X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63)

#define STUB(n)                                                                                                        \
    static HRESULT STDMETHODCALLTYPE stub_##n(void *This)                                                              \
    {                                                                                                                  \
        return unimplemented(This, n);                                                                                 \
    }
ALL_SLOTS(STUB)
#undef STUB

#define STUB_ENTRY(n) [n] = (const void *)stub_##n,
static const void *const g_stubs[MAX_SLOTS] = {ALL_SLOTS(STUB_ENTRY)};
#undef STUB_ENTRY

/* ------------------------------------------------------------------ the inspectable base */

static HRESULT STDMETHODCALLTYPE base_query(void *This, REFIID riid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    WUC *object = wuc_of(This);
    if (IsEqualIID(riid, &IID_MimeWuc)) {
        add_ref(object);
        *result = object;
        return S_OK;
    }
    void *found = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        (IsEqualIID(riid, &IID_IInspectable) && object->Kind != K_HANDLER))
        found = &object->Faces[0];
    for (int i = 0; !found && i < MAX_FACES && g_kinds[object->Kind].Ifaces[i] >= 0; i++)
        if (IsEqualIID(riid, g_interfaces[g_kinds[object->Kind].Ifaces[i]].Iid))
            found = &object->Faces[i];
    if (!found)
        return E_NOINTERFACE;
    add_ref(object);
    *result = found;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE base_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&wuc_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE base_release(void *This)
{
    WUC *object = wuc_of(This);
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs)
        destroy(object);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE base_iids(void *This, ULONG *count, IID **iids)
{
    if (!count || !iids)
        return E_POINTER;
    const int *ifaces = g_kinds[wuc_of(This)->Kind].Ifaces;
    ULONG shown = 0;
    for (int i = 0; i < MAX_FACES && ifaces[i] >= 0; i++)
        shown += g_interfaces[ifaces[i]].Inspectable;
    *count = 0;
    *iids = shown ? CoTaskMemAlloc(shown * sizeof(IID)) : nullptr;
    if (shown && !*iids)
        return E_OUTOFMEMORY;
    for (int i = 0; i < MAX_FACES && ifaces[i] >= 0; i++)
        if (g_interfaces[ifaces[i]].Inspectable)
            (*iids)[(*count)++] = *g_interfaces[ifaces[i]].Iid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE base_class_name(void *This, HSTRING *name)
{
    if (!name)
        return E_POINTER;
    *name = nullptr;
    LPCWSTR text = g_kinds[wuc_of(This)->Kind].Name;
    return text ? WindowsCreateString(text, (UINT32)wcslen(text), name) : S_OK;
}

static HRESULT STDMETHODCALLTYPE base_trust_level(void *This, TrustLevel *level)
{
    if (!level)
        return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

/* ------------------------------------------------------------------ making and destroying */

/* objects are made once a factory was asked for, which built the tables. */
static WUC *make(KIND kind, WUC *compositor)
{
    WUC *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return nullptr;
    for (int i = 0; i < MAX_FACES && g_kinds[kind].Ifaces[i] >= 0; i++) {
        object->Faces[i].Vtbl = g_tables[g_kinds[kind].Ifaces[i]];
        object->Faces[i].Owner = object;
    }
    object->Refs = 1;
    object->Kind = kind;
    object->Compositor = compositor;
    add_ref(compositor);
    return object;
}

static void set_object(WUC **slot, WUC *object)
{
    add_ref(object);
    release_wuc(*slot);
    *slot = object;
}

static void release_unknown(IUnknown **slot)
{
    if (*slot)
        (*slot)->lpVtbl->Release(*slot);
    *slot = nullptr;
}

static void remove_child_at(WUC *container, ULONG at);

/* what Close and the last release let go of. a visual leaves its parent. */
static void close_object(WUC *object)
{
    if (object->Closed)
        return;
    object->Closed = TRUE;
    switch (object->Kind) {
    case K_TARGET: {
        WUC *c = object->Compositor;
        for (WUC **p = &c->Targets; *p; p = &(*p)->NextTarget)
            if (*p == object) {
                *p = object->NextTarget;
                break;
            }
        if (object->Target) {
            object->Target->lpVtbl->SetRoot(object->Target, nullptr);
            DcDeviceCommit(c->Device);
        }
        release_unknown((IUnknown **)&object->Target);
        set_object(&object->Root, nullptr);
        break;
    }
    case K_CONTAINER:
    case K_SPRITE:
        if (object->Parent)
            for (ULONG i = 0; i < object->Parent->ChildCount; i++)
                if (object->Parent->Children[i] == object) {
                    remove_child_at(object->Parent, i);
                    break;
                }
        while (object->ChildCount)
            remove_child_at(object, object->ChildCount - 1);
        set_object(&object->Clip, nullptr);
        set_object(&object->Brush, nullptr);
        set_object(&object->TransformParent, nullptr);
        break;
    case K_SURFACE_BRUSH:
        set_object(&object->Surface, nullptr);
        break;
    default:
        break;
    }
}

static void destroy(WUC *object)
{
    /* a visual being destroyed has no parent left: its parent would hold it. */
    object->Parent = nullptr;
    WUC *c = object->Kind == K_COMPOSITOR ? object : object->Compositor;
    if (c && object->Kind != K_HANDLER) {
        object->Refs = 1;
        lock(object);
        close_object(object);
        unlock(object);
    }
    switch (object->Kind) {
    case K_CONTROLLER:
        if (object->Owned)
            object->Owned->Controller = nullptr;
        WrtEventClear(&object->CommitNeeded);
        release_wuc(object->Owned);
        break;
    case K_COMPOSITOR:
        if (object->Queue)
            object->Queue->lpVtbl->Release(object->Queue);
        if (object->Device)
            DcDeviceRelease(object->Device);
        break;
    case K_CONTAINER:
    case K_SPRITE:
        HeapFree(GetProcessHeap(), 0, object->Children);
        release_unknown(&object->PushedContent);
        release_unknown((IUnknown **)&object->Visual);
        break;
    case K_COLLECTION:
        release_wuc(object->Container);
        break;
    case K_DRAWING_SURFACE:
        release_wuc(object->Graphics);
        release_unknown(&object->Content);
        break;
    case K_SURFACE:
        release_unknown(&object->Content);
        break;
    case K_GRAPHICS_DEVICE:
        release_unknown(&object->Rendering);
        release_unknown((IUnknown **)&object->D3d);
        release_unknown((IUnknown **)&object->D2d);
        break;
    default:
        break;
    }
    release_wuc(object->Compositor);
    HeapFree(GetProcessHeap(), 0, object);
}

static HRESULT STDMETHODCALLTYPE closable_close(void *This)
{
    WUC *object = wuc_of(This);
    if (object->Kind == K_COMPOSITOR || object->Kind == K_CONTROLLER)
        return S_OK;
    lock(object);
    close_object(object);
    unlock(object);
    return S_OK;
}

/* ------------------------------------------------------------------ commits */

static void commit(WUC *c);

/* the dispatcher queue calls this once after the first change since the last commit. */
static HRESULT STDMETHODCALLTYPE handler_invoke(void *This)
{
    WUC *c = wuc_of(This)->Compositor;
    DcDeviceLock(c->Device);
    c->Scheduled = FALSE;
    WUC *controller = c->Controller;
    add_ref(controller);
    DcDeviceUnlock(c->Device);
    /* a controller's program commits when it is told a commit is needed; a compositor alone commits itself. */
    if (controller) {
        WrtEventInvoke(&controller->CommitNeeded, face(controller), nullptr);
        release_wuc(controller);
    } else {
        commit(c);
    }
    return S_OK;
}

/* called with the lock held after a change the next commit shows. */
static void touch(WUC *object)
{
    WUC *c = compositor_of(object);
    if (c->Scheduled || !c->Queue)
        return;
    WUC *handler = make(K_HANDLER, c);
    if (!handler)
        return;
    BOOLEAN queued = FALSE;
    c->Scheduled = SUCCEEDED(c->Queue->lpVtbl->TryEnqueue(c->Queue, face(handler), &queued)) && queued;
    release_wuc(handler);
}

static void matrix_translate(float m[16], float x, float y, float z)
{
    DcMatrixIdentity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

/* the rotation about an axis, for row vectors as System.Numerics makes it. */
static void matrix_axis_angle(float m[16], VECTOR3 axis, float angle)
{
    DcMatrixIdentity(m);
    double length = DcSqrt((double)axis.X * axis.X + (double)axis.Y * axis.Y + (double)axis.Z * axis.Z);
    if (length == 0.0 || angle == 0.0f)
        return;
    float x = (float)(axis.X / length), y = (float)(axis.Y / length), z = (float)(axis.Z / length);
    float sa = (float)DcSin(angle), ca = (float)DcCos(angle);
    float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z;
    m[0] = xx + ca * (1.0f - xx);
    m[1] = xy - ca * xy + sa * z;
    m[2] = xz - ca * xz - sa * y;
    m[4] = xy - ca * xy - sa * z;
    m[5] = yy + ca * (1.0f - yy);
    m[6] = yz - ca * yz + sa * x;
    m[8] = xz - ca * xz + sa * y;
    m[9] = yz - ca * yz - sa * x;
    m[10] = zz + ca * (1.0f - zz);
}

static void matrix_quaternion(float m[16], QUATERNION q)
{
    DcMatrixIdentity(m);
    float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z;
    float xy = q.X * q.Y, wz = q.Z * q.W, xz = q.Z * q.X, wy = q.Y * q.W, yz = q.Y * q.Z, wx = q.X * q.W;
    m[0] = 1.0f - 2.0f * (yy + zz);
    m[1] = 2.0f * (xy + wz);
    m[2] = 2.0f * (xz - wy);
    m[4] = 2.0f * (xy - wz);
    m[5] = 1.0f - 2.0f * (zz + xx);
    m[6] = 2.0f * (yz + wx);
    m[8] = 2.0f * (xz + wy);
    m[9] = 2.0f * (yz - wx);
    m[10] = 1.0f - 2.0f * (yy + xx);
}

/*
 * a visual's transform: its anchor point moves it by a fraction of its size, then it scales and rotates about its
 * center point, its own matrix applies and its offset moves it into its parent.
 */
static void visual_matrix(const PROPS *p, float width, float height, float px, float py, float pz, float out[16])
{
    float step[16];
    matrix_translate(out, -p->Anchor.X * width - p->Center.X, -p->Anchor.Y * height - p->Center.Y, -p->Center.Z);
    DcMatrixIdentity(step);
    step[0] = p->Scale.X;
    step[5] = p->Scale.Y;
    step[10] = p->Scale.Z;
    DcMatrixMultiply(out, out, step);
    matrix_axis_angle(step, p->Axis, p->Angle);
    DcMatrixMultiply(out, out, step);
    matrix_quaternion(step, p->Orientation);
    DcMatrixMultiply(out, out, step);
    matrix_translate(step, p->Center.X, p->Center.Y, p->Center.Z);
    DcMatrixMultiply(out, out, step);
    DcMatrixMultiply(out, out, p->Matrix.M);
    matrix_translate(step, px, py, pz);
    DcMatrixMultiply(out, out, step);
}

static IUnknown *surface_content(WUC *surface)
{
    return surface ? surface->Content : nullptr;
}

/* the brush interpolation modes that magnify by the nearest neighbor. */
static int interpolation(int mode)
{
    return mode == 0 || mode >= 6 ? DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                  : DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR;
}

/* gives the device's visual what the visual is now, in a parent of a size. */
static void sync_visual(WUC *v, float parent_width, float parent_height, UINT depth)
{
    if (depth > 64)
        return;
    const PROPS *p = &v->P;
    IDCompositionVisual3 *d = v->Visual;
    float width = p->Size.X + p->RelativeSize.X * parent_width;
    float height = p->Size.Y + p->RelativeSize.Y * parent_height;
    PUSHED now = {TRUE};
    visual_matrix(p, width, height, p->Offset.X + p->RelativeOffset.X * parent_width,
                  p->Offset.Y + p->RelativeOffset.Y * parent_height, p->Offset.Z, now.Matrix.m[0]);
    now.Opacity = p->Opacity;
    now.Visible = p->Visible;
    now.Composite = p->Composite - 1;
    now.Border = p->Border - 1;
    now.Backface = p->Backface - 1;
    if (v->Clip) {
        now.Clipped = TRUE;
        now.Clip = (D2D_RECT_F){v->Clip->Insets[0], v->Clip->Insets[1], width - v->Clip->Insets[2],
                                height - v->Clip->Insets[3]};
    }
    if (memcmp(&now, &v->Pushed, sizeof(now))) {
        d->lpVtbl->SetTransform3D(d, &now.Matrix);
        d->lpVtbl->SetOpacity(d, now.Opacity);
        d->lpVtbl->SetVisible(d, now.Visible);
        d->lpVtbl->SetCompositeMode(d, now.Composite);
        d->lpVtbl->SetBorderMode(d, now.Border);
        d->lpVtbl->SetBackFaceVisibility(d, now.Backface);
        if (now.Clipped)
            d->lpVtbl->SetClip(d, &now.Clip);
        else
            d->lpVtbl->SetClipObject(d, nullptr);
        v->Pushed = now;
    }
    DcLayout layout = {};
    IUnknown *content = nullptr;
    WUC *brush = v->Kind == K_SPRITE ? v->Brush : nullptr;
    if (brush) {
        layout.on = TRUE;
        layout.width = width;
        layout.height = height;
        if (brush->Kind == K_COLOR_BRUSH) {
            float a = brush->Color.A / 255.0f;
            layout.solid = TRUE;
            layout.color[0] = brush->Color.R / 255.0f * a;
            layout.color[1] = brush->Color.G / 255.0f * a;
            layout.color[2] = brush->Color.B / 255.0f * a;
            layout.color[3] = a;
        } else {
            layout.stretch = brush->Stretch;
            layout.align_x = brush->AlignX;
            layout.align_y = brush->AlignY;
            layout.interpolation = interpolation(brush->Interpolation);
            content = surface_content(brush->Surface);
        }
    }
    if (content != v->PushedContent) {
        d->lpVtbl->SetContent(d, content);
        if (content)
            content->lpVtbl->AddRef(content);
        release_unknown(&v->PushedContent);
        v->PushedContent = content;
    }
    DcVisualSetLayout(d, &layout);
    if (v->ChildrenChanged) {
        d->lpVtbl->RemoveAllVisuals(d);
        for (ULONG i = 0; i < v->ChildCount; i++)
            d->lpVtbl->AddVisual(d, (IDCompositionVisual *)v->Children[i]->Visual, FALSE, nullptr);
        v->ChildrenChanged = FALSE;
    }
    for (ULONG i = 0; i < v->ChildCount; i++)
        sync_visual(v->Children[i], width, height, depth + 1);
}

/* the targets' trees into the device, which then draws them. */
static void commit(WUC *c)
{
    DcDeviceLock(c->Device);
    c->Scheduled = FALSE;
    for (WUC *t = c->Targets; t; t = t->NextTarget) {
        RECT client = {};
        GetClientRect(t->Window, &client);
        if (t->Root)
            sync_visual(t->Root, (float)client.right, (float)client.bottom, 0);
        IDCompositionVisual *root = t->Root ? (IDCompositionVisual *)t->Root->Visual : nullptr;
        if (root != t->PushedRoot) {
            t->Target->lpVtbl->SetRoot(t->Target, root);
            t->PushedRoot = root;
        }
    }
    DcDeviceCommit(c->Device);
    DcDeviceUnlock(c->Device);
}

/* ------------------------------------------------------------------ the compositor controller */

static HRESULT STDMETHODCALLTYPE controller_get_compositor(void *This, void **value)
{
    if (!value)
        return E_POINTER;
    WUC *compositor = wuc_of(This)->Owned;
    add_ref(compositor);
    *value = face(compositor);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE controller_commit(void *This)
{
    commit(wuc_of(This)->Owned);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE controller_add_commit_needed(void *This, IWrtEventHandler *handler,
                                                              EventRegistrationToken *token)
{
    return WrtEventAdd(&wuc_of(This)->CommitNeeded, handler, token);
}

static HRESULT STDMETHODCALLTYPE controller_remove_commit_needed(void *This, EventRegistrationToken token)
{
    return WrtEventRemove(&wuc_of(This)->CommitNeeded, token);
}

/* ------------------------------------------------------------------ the compositor */

static HRESULT new_object(KIND kind, void *This, void **value, WUC **made)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    WUC *object = make(kind, compositor_of(wuc_of(This)));
    if (!object)
        return E_OUTOFMEMORY;
    *value = face(object);
    if (made)
        *made = object;
    return S_OK;
}

static HRESULT new_visual(KIND kind, void *This, void **value)
{
    WUC *visual;
    HRESULT hr = new_object(kind, This, value, &visual);
    if (FAILED(hr))
        return hr;
    WUC *c = visual->Compositor;
    hr = DcCreateVisual(c->Device, &visual->Visual);
    if (FAILED(hr)) {
        release_wuc(visual);
        *value = nullptr;
        return hr;
    }
    PROPS *p = &visual->P;
    p->Visible = p->HitTest = TRUE;
    p->Opacity = 1.0f;
    p->Orientation.W = 1.0f;
    p->Axis.Z = 1.0f;
    p->Scale = (VECTOR3){1, 1, 1};
    DcMatrixIdentity(p->Matrix.M);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE compositor_create_container_visual(void *This, void **value)
{
    return new_visual(K_CONTAINER, This, value);
}

static HRESULT STDMETHODCALLTYPE compositor_create_sprite_visual(void *This, void **value)
{
    return new_visual(K_SPRITE, This, value);
}

static HRESULT STDMETHODCALLTYPE compositor_create_color_brush_with(void *This, WUC_COLOR color, void **value)
{
    WUC *brush;
    HRESULT hr = new_object(K_COLOR_BRUSH, This, value, &brush);
    if (SUCCEEDED(hr))
        brush->Color = color;
    return hr;
}

static HRESULT STDMETHODCALLTYPE compositor_create_color_brush(void *This, void **value)
{
    return compositor_create_color_brush_with(This, (WUC_COLOR){}, value);
}

static HRESULT STDMETHODCALLTYPE compositor_create_inset_clip_with(void *This, float left, float top, float right,
                                                                   float bottom, void **value)
{
    WUC *clip;
    HRESULT hr = new_object(K_INSET_CLIP, This, value, &clip);
    if (SUCCEEDED(hr)) {
        clip->Insets[0] = left;
        clip->Insets[1] = top;
        clip->Insets[2] = right;
        clip->Insets[3] = bottom;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE compositor_create_inset_clip(void *This, void **value)
{
    return compositor_create_inset_clip_with(This, 0, 0, 0, 0, value);
}

/* a surface brush stretches its surface over the visual and centers it, until told otherwise. */
static HRESULT STDMETHODCALLTYPE compositor_create_surface_brush_with(void *This, void *surface, void **value)
{
    WUC *found = nullptr;
    if (surface) {
        found = own(surface);
        if (!found || (found->Kind != K_SURFACE && found->Kind != K_DRAWING_SURFACE))
            return E_INVALIDARG;
    }
    WUC *brush;
    HRESULT hr = new_object(K_SURFACE_BRUSH, This, value, &brush);
    if (FAILED(hr))
        return hr;
    brush->Stretch = 1;
    brush->Interpolation = 1;
    brush->AlignX = brush->AlignY = 0.5f;
    set_object(&brush->Surface, found);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE compositor_create_surface_brush(void *This, void **value)
{
    return compositor_create_surface_brush_with(This, nullptr, value);
}

/* a program that did not ask for a controller gets the compositor of its own. */
static HRESULT STDMETHODCALLTYPE object_get_compositor(void *This, void **value)
{
    if (!value)
        return E_POINTER;
    WUC *compositor = compositor_of(wuc_of(This));
    add_ref(compositor);
    *value = face(compositor);
    return S_OK;
}

/* objects of a desktop compositor have no core dispatcher. */
static HRESULT STDMETHODCALLTYPE object_get_dispatcher(void *This, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE object_get_dispatcher_queue(void *This, void **value)
{
    if (!value)
        return E_POINTER;
    WUC_QUEUE *queue = compositor_of(wuc_of(This))->Queue;
    if (queue)
        queue->lpVtbl->AddRef(queue);
    *value = queue;
    return S_OK;
}

/* the window must be one of the calling process's, with no other target of the same layer. */
static HRESULT STDMETHODCALLTYPE desktop_create_target(void *This, HWND window, BOOL topmost, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    WUC *c = wuc_of(This);
    if (!window || !IsWindow(window))
        return E_INVALIDARG;
    IDCompositionTarget *dc;
    HRESULT hr = DcCreateTarget(c->Device, window, topmost, &dc);
    if (FAILED(hr))
        return hr;
    WUC *target = make(K_TARGET, c);
    if (!target) {
        dc->lpVtbl->Release(dc);
        return E_OUTOFMEMORY;
    }
    target->Target = dc;
    target->Window = window;
    target->Topmost = !!topmost;
    DcDeviceLock(c->Device);
    target->NextTarget = c->Targets;
    c->Targets = target;
    DcDeviceUnlock(c->Device);
    *value = face(target);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_ensure_on_thread(void *This, DWORD thread)
{
    return S_OK;
}

/* ------------------------------------------------------------------ surfaces */

static HRESULT new_surface(void *This, IUnknown *content, void **value)
{
    WUC *surface;
    HRESULT hr = new_object(K_SURFACE, This, value, &surface);
    if (FAILED(hr)) {
        content->lpVtbl->Release(content);
        return hr;
    }
    surface->Content = content;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE interop_surface_for_handle(void *This, HANDLE handle, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    IUnknown *content;
    HRESULT hr = DcCreateSurfaceFromHandle(wuc_of(This)->Device, handle, &content);
    return FAILED(hr) ? hr : new_surface(This, content, value);
}

/* a swap chain made for composition, shown as it presents. */
static HRESULT STDMETHODCALLTYPE interop_surface_for_swap_chain(void *This, IUnknown *chain, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    IUnknown *swap;
    if (!chain || FAILED(chain->lpVtbl->QueryInterface(chain, &IID_IDXGISwapChain1, (void **)&swap)))
        return E_INVALIDARG;
    return new_surface(This, swap, value);
}

/* the rendering device is a Direct3D device of DXGI, or a Direct2D device made on one. */
static HRESULT STDMETHODCALLTYPE interop_create_graphics_device(void *This, IUnknown *rendering, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr = rendering ? DcResolveRenderingDevice(rendering, &d3d, &d2d) : E_INVALIDARG;
    if (FAILED(hr))
        return hr;
    WUC *device;
    hr = new_object(K_GRAPHICS_DEVICE, This, value, &device);
    if (FAILED(hr)) {
        d3d->lpVtbl->Release(d3d);
        release_unknown((IUnknown **)&d2d);
        return hr;
    }
    device->D3d = d3d;
    device->D2d = d2d;
    device->Rendering = rendering;
    rendering->lpVtbl->AddRef(rendering);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE graphics_get_rendering_device(void *This, IUnknown **value)
{
    if (!value)
        return E_POINTER;
    WUC *device = wuc_of(This);
    lock(device);
    *value = device->Rendering;
    if (*value)
        (*value)->lpVtbl->AddRef(*value);
    unlock(device);
    return S_OK;
}

/* surfaces made after use the new device. */
static HRESULT STDMETHODCALLTYPE graphics_set_rendering_device(void *This, IUnknown *rendering)
{
    WUC *device = wuc_of(This);
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr = rendering ? DcResolveRenderingDevice(rendering, &d3d, &d2d) : E_INVALIDARG;
    if (FAILED(hr))
        return hr;
    lock(device);
    release_unknown((IUnknown **)&device->D3d);
    release_unknown((IUnknown **)&device->D2d);
    release_unknown(&device->Rendering);
    device->D3d = d3d;
    device->D2d = d2d;
    device->Rendering = rendering;
    rendering->lpVtbl->AddRef(rendering);
    unlock(device);
    return S_OK;
}

/* a drawing surface is a virtual surface of the device, which can change its size. */
static HRESULT create_drawing_surface(void *This, SIZE_INT32 size, int format, int alpha, void **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (size.Width < 0 || size.Height < 0)
        return E_INVALIDARG;
    WUC *device = wuc_of(This), *c = device->Compositor;
    IUnknown *surface;
    lock(device);
    HRESULT hr = DcCreateSurface(c->Device, device->D3d, device->D2d, TRUE, (UINT)size.Width, (UINT)size.Height,
                                 (DXGI_FORMAT)format, (DXGI_ALPHA_MODE)alpha, (void **)&surface);
    unlock(device);
    if (FAILED(hr))
        return hr;
    WUC *drawing;
    hr = new_object(K_DRAWING_SURFACE, This, value, &drawing);
    if (FAILED(hr)) {
        surface->lpVtbl->Release(surface);
        return hr;
    }
    drawing->Content = surface;
    drawing->Pixels = size;
    drawing->Format = (DXGI_FORMAT)format;
    drawing->Alpha = (DXGI_ALPHA_MODE)alpha;
    set_object(&drawing->Graphics, device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE graphics_create_drawing_surface(void *This, WUC_SIZE size, int format, int alpha,
                                                                 void **value)
{
    if (!finite(size.Width) || !finite(size.Height))
        return E_INVALIDARG;
    return create_drawing_surface(This, (SIZE_INT32){(INT32)size.Width, (INT32)size.Height}, format, alpha, value);
}

static HRESULT STDMETHODCALLTYPE graphics_create_drawing_surface2(void *This, SIZE_INT32 size, int format, int alpha,
                                                                  void **value)
{
    return create_drawing_surface(This, size, format, alpha, value);
}

static IDCompositionVirtualSurface *drawing_of(void *This)
{
    return (IDCompositionVirtualSurface *)wuc_of(This)->Content;
}

static HRESULT STDMETHODCALLTYPE drawing_get_alpha_mode(void *This, int *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->Alpha;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drawing_get_pixel_format(void *This, int *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->Format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drawing_get_size(void *This, WUC_SIZE *value)
{
    if (!value)
        return E_POINTER;
    WUC *drawing = wuc_of(This);
    *value = (WUC_SIZE){(float)drawing->Pixels.Width, (float)drawing->Pixels.Height};
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drawing_get_size_int(void *This, SIZE_INT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->Pixels;
    return S_OK;
}

static HRESULT resize_drawing(void *This, SIZE_INT32 size)
{
    if (size.Width < 0 || size.Height < 0)
        return E_INVALIDARG;
    WUC *drawing = wuc_of(This);
    IDCompositionVirtualSurface *surface = drawing_of(This);
    HRESULT hr = surface->lpVtbl->Resize(surface, (UINT)size.Width, (UINT)size.Height);
    if (SUCCEEDED(hr)) {
        lock(drawing);
        drawing->Pixels = size;
        touch(drawing);
        unlock(drawing);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE drawing_resize(void *This, SIZE_INT32 size)
{
    return resize_drawing(This, size);
}

static HRESULT STDMETHODCALLTYPE drawing_interop_begin_draw(void *This, const RECT *rect, REFIID riid, void **object,
                                                            POINT *offset)
{
    IDCompositionVirtualSurface *surface = drawing_of(This);
    return surface->lpVtbl->BeginDraw(surface, rect, riid, object, offset);
}

/* what was drawn shows at the next commit. */
static HRESULT STDMETHODCALLTYPE drawing_interop_end_draw(void *This)
{
    WUC *drawing = wuc_of(This);
    IDCompositionVirtualSurface *surface = drawing_of(This);
    HRESULT hr = surface->lpVtbl->EndDraw(surface);
    if (SUCCEEDED(hr)) {
        lock(drawing);
        touch(drawing);
        unlock(drawing);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE drawing_interop_resize(void *This, SIZE size)
{
    return resize_drawing(This, (SIZE_INT32){size.cx, size.cy});
}

static HRESULT STDMETHODCALLTYPE drawing_interop_scroll(void *This, const RECT *scroll, const RECT *clip, int dx,
                                                        int dy)
{
    IDCompositionVirtualSurface *surface = drawing_of(This);
    return surface->lpVtbl->Scroll(surface, scroll, clip, dx, dy);
}

static HRESULT STDMETHODCALLTYPE drawing_interop_resume_draw(void *This)
{
    IDCompositionVirtualSurface *surface = drawing_of(This);
    return surface->lpVtbl->ResumeDraw(surface);
}

static HRESULT STDMETHODCALLTYPE drawing_interop_suspend_draw(void *This)
{
    IDCompositionVirtualSurface *surface = drawing_of(This);
    return surface->lpVtbl->SuspendDraw(surface);
}

/* ------------------------------------------------------------------ targets */

static HRESULT STDMETHODCALLTYPE target_get_root(void *This, void **value)
{
    if (!value)
        return E_POINTER;
    WUC *target = wuc_of(This);
    lock(target);
    add_ref(target->Root);
    *value = face(target->Root);
    unlock(target);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE target_put_root(void *This, void *visual)
{
    WUC *target = wuc_of(This);
    WUC *root = visual ? own(visual) : nullptr;
    if (visual && (!is_visual(root) || root->Compositor != target->Compositor))
        return E_INVALIDARG;
    lock(target);
    if (target->Closed) {
        unlock(target);
        return RO_E_CLOSED_;
    }
    set_object(&target->Root, root);
    touch(target);
    unlock(target);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE target_get_is_topmost(void *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = wuc_of(This)->Topmost;
    return S_OK;
}

/* ------------------------------------------------------------------ visual properties */

#define VISUAL_PROPERTY(name, type, field, check)                                                                      \
    static HRESULT STDMETHODCALLTYPE visual_get_##name(void *This, type *value)                                        \
    {                                                                                                                  \
        if (!value)                                                                                                    \
            return E_POINTER;                                                                                          \
        WUC *v = wuc_of(This);                                                                                         \
        lock(v);                                                                                                       \
        *value = v->P.field;                                                                                           \
        unlock(v);                                                                                                     \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE visual_put_##name(void *This, type value)                                         \
    {                                                                                                                  \
        if (!(check))                                                                                                  \
            return E_INVALIDARG;                                                                                       \
        WUC *v = wuc_of(This);                                                                                         \
        lock(v);                                                                                                       \
        v->P.field = value;                                                                                            \
        touch(v);                                                                                                      \
        unlock(v);                                                                                                     \
        return S_OK;                                                                                                   \
    }

#define V2(v) (finite((v).X) && finite((v).Y))
#define V3(v) (V2(v) && finite((v).Z))

VISUAL_PROPERTY(anchor, VECTOR2, Anchor, V2(value))
VISUAL_PROPERTY(backface, int, Backface, value >= 0 && value <= 2)
VISUAL_PROPERTY(border, int, Border, value >= 0 && value <= 2)
VISUAL_PROPERTY(center, VECTOR3, Center, V3(value))
VISUAL_PROPERTY(composite, int, Composite, value >= 0 && value <= 3)
VISUAL_PROPERTY(visible, BOOLEAN, Visible, TRUE)
VISUAL_PROPERTY(offset, VECTOR3, Offset, V3(value))
VISUAL_PROPERTY(opacity, float, Opacity, finite(value))
VISUAL_PROPERTY(orientation, QUATERNION, Orientation, V3(value) && finite(value.W))
VISUAL_PROPERTY(angle, float, Angle, finite(value))
VISUAL_PROPERTY(axis, VECTOR3, Axis, V3(value))
VISUAL_PROPERTY(scale, VECTOR3, Scale, V3(value))
VISUAL_PROPERTY(size, VECTOR2, Size, V2(value))
VISUAL_PROPERTY(matrix, MATRIX4X4, Matrix, TRUE)
VISUAL_PROPERTY(relative_offset, VECTOR3, RelativeOffset, V3(value))
VISUAL_PROPERTY(relative_size, VECTOR2, RelativeSize, V2(value))
VISUAL_PROPERTY(hit_test, BOOLEAN, HitTest, TRUE)

static HRESULT STDMETHODCALLTYPE visual_get_degrees(void *This, float *value)
{
    HRESULT hr = visual_get_angle(This, value);
    if (SUCCEEDED(hr))
        *value = (float)(*value * 180.0 / DC_PI);
    return hr;
}

static HRESULT STDMETHODCALLTYPE visual_put_degrees(void *This, float value)
{
    return visual_put_angle(This, (float)(value * DC_PI / 180.0));
}

static HRESULT get_object(WUC *v, WUC **slot, void **value)
{
    if (!value)
        return E_POINTER;
    lock(v);
    add_ref(*slot);
    *value = face(*slot);
    unlock(v);
    return S_OK;
}

static HRESULT put_object(WUC *v, WUC **slot, void *iface, BOOL (*valid)(const WUC *))
{
    WUC *object = iface ? own(iface) : nullptr;
    if (iface && (!object || !valid(object) || object->Compositor != v->Compositor))
        return E_INVALIDARG;
    lock(v);
    set_object(slot, object);
    touch(v);
    unlock(v);
    return S_OK;
}

static BOOL is_clip(const WUC *object)
{
    return object->Kind == K_INSET_CLIP;
}

static BOOL is_brush(const WUC *object)
{
    return object->Kind == K_COLOR_BRUSH || object->Kind == K_SURFACE_BRUSH;
}

static HRESULT STDMETHODCALLTYPE visual_get_clip(void *This, void **value)
{
    WUC *v = wuc_of(This);
    return get_object(v, &v->Clip, value);
}

static HRESULT STDMETHODCALLTYPE visual_put_clip(void *This, void *clip)
{
    WUC *v = wuc_of(This);
    return put_object(v, &v->Clip, clip, is_clip);
}

static HRESULT STDMETHODCALLTYPE visual_get_parent(void *This, void **value)
{
    WUC *v = wuc_of(This);
    return get_object(v, &v->Parent, value);
}

static HRESULT STDMETHODCALLTYPE visual_get_transform_parent(void *This, void **value)
{
    WUC *v = wuc_of(This);
    return get_object(v, &v->TransformParent, value);
}

static HRESULT STDMETHODCALLTYPE visual_put_transform_parent(void *This, void *visual)
{
    WUC *v = wuc_of(This);
    return put_object(v, &v->TransformParent, visual, is_visual);
}

static HRESULT STDMETHODCALLTYPE sprite_get_brush(void *This, void **value)
{
    WUC *v = wuc_of(This);
    return get_object(v, &v->Brush, value);
}

static HRESULT STDMETHODCALLTYPE sprite_put_brush(void *This, void *brush)
{
    WUC *v = wuc_of(This);
    return put_object(v, &v->Brush, brush, is_brush);
}

/* ------------------------------------------------------------------ brushes and clips */

static HRESULT STDMETHODCALLTYPE color_get(void *This, WUC_COLOR *value)
{
    if (!value)
        return E_POINTER;
    WUC *brush = wuc_of(This);
    lock(brush);
    *value = brush->Color;
    unlock(brush);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE color_put(void *This, WUC_COLOR value)
{
    WUC *brush = wuc_of(This);
    lock(brush);
    brush->Color = value;
    touch(brush);
    unlock(brush);
    return S_OK;
}

#define FIELD_PROPERTY(prefix, name, type, field, check)                                                               \
    static HRESULT STDMETHODCALLTYPE prefix##_get_##name(void *This, type *value)                                      \
    {                                                                                                                  \
        if (!value)                                                                                                    \
            return E_POINTER;                                                                                          \
        WUC *o = wuc_of(This);                                                                                         \
        lock(o);                                                                                                       \
        *value = o->field;                                                                                             \
        unlock(o);                                                                                                     \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_put_##name(void *This, type value)                                       \
    {                                                                                                                  \
        if (!(check))                                                                                                  \
            return E_INVALIDARG;                                                                                       \
        WUC *o = wuc_of(This);                                                                                         \
        lock(o);                                                                                                       \
        o->field = value;                                                                                              \
        touch(o);                                                                                                      \
        unlock(o);                                                                                                     \
        return S_OK;                                                                                                   \
    }

FIELD_PROPERTY(surface_brush, interpolation, int, Interpolation, value >= 0 && value <= 9)
FIELD_PROPERTY(surface_brush, align_x, float, AlignX, finite(value))
FIELD_PROPERTY(surface_brush, stretch, int, Stretch, value >= 0 && value <= 3)
FIELD_PROPERTY(surface_brush, align_y, float, AlignY, finite(value))
FIELD_PROPERTY(inset, bottom, float, Insets[3], finite(value))
FIELD_PROPERTY(inset, left, float, Insets[0], finite(value))
FIELD_PROPERTY(inset, right, float, Insets[2], finite(value))
FIELD_PROPERTY(inset, top, float, Insets[1], finite(value))

static BOOL is_surface(const WUC *object)
{
    return object->Kind == K_SURFACE || object->Kind == K_DRAWING_SURFACE;
}

static HRESULT STDMETHODCALLTYPE surface_brush_get_surface(void *This, void **value)
{
    WUC *brush = wuc_of(This);
    return get_object(brush, &brush->Surface, value);
}

static HRESULT STDMETHODCALLTYPE surface_brush_put_surface(void *This, void *surface)
{
    WUC *brush = wuc_of(This);
    return put_object(brush, &brush->Surface, surface, is_surface);
}

/* ------------------------------------------------------------------ the children of a visual */

static HRESULT STDMETHODCALLTYPE container_get_children(void *This, void **value)
{
    WUC *container = wuc_of(This), *collection;
    HRESULT hr = new_object(K_COLLECTION, This, value, &collection);
    if (SUCCEEDED(hr))
        set_object(&collection->Container, container);
    return hr;
}

static HRESULT STDMETHODCALLTYPE collection_get_count(void *This, INT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = (INT32)wuc_of(This)->Container->ChildCount;
    return S_OK;
}

static LONG child_index(WUC *container, WUC *child)
{
    for (ULONG i = 0; i < container->ChildCount; i++)
        if (container->Children[i] == child)
            return (LONG)i;
    return -1;
}

static void remove_child_at(WUC *container, ULONG at)
{
    WUC *child = container->Children[at];
    memmove(&container->Children[at], &container->Children[at + 1], (container->ChildCount - at - 1) * sizeof(WUC *));
    container->ChildCount--;
    container->ChildrenChanged = TRUE;
    child->Parent = nullptr;
    touch(container);
    release_wuc(child);
}

/* puts the child at an index, taking it out of its place first. called with the lock held. */
static HRESULT insert(WUC *container, WUC *child, ULONG index)
{
    if (!is_visual(child) || child->Compositor != container->Compositor)
        return E_INVALIDARG;
    for (WUC *up = container; up; up = up->Parent)
        if (up == child)
            return E_INVALIDARG;
    add_ref(child);
    LONG at = child_index(container, child);
    if (at >= 0) {
        remove_child_at(container, (ULONG)at);
        if ((ULONG)at < index)
            index--;
    } else if (child->Parent) {
        LONG from = child_index(child->Parent, child);
        if (from >= 0)
            remove_child_at(child->Parent, (ULONG)from);
    }
    if (container->ChildCount == container->ChildCapacity) {
        ULONG capacity = container->ChildCapacity ? container->ChildCapacity * 2 : 8;
        WUC **grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WUC *));
        if (!grown) {
            release_wuc(child);
            return E_OUTOFMEMORY;
        }
        if (container->Children)
            memcpy(grown, container->Children, container->ChildCount * sizeof(WUC *));
        HeapFree(GetProcessHeap(), 0, container->Children);
        container->Children = grown;
        container->ChildCapacity = capacity;
    }
    if (index > container->ChildCount)
        index = container->ChildCount;
    memmove(&container->Children[index + 1], &container->Children[index], (container->ChildCount - index) * sizeof(WUC *));
    container->Children[index] = child;
    container->ChildCount++;
    container->ChildrenChanged = TRUE;
    child->Parent = container;
    touch(container);
    return S_OK;
}

typedef enum { AT_TOP, AT_BOTTOM, ABOVE, BELOW } PLACE;

static HRESULT place(void *This, void *visual, void *sibling, PLACE where)
{
    WUC *container = wuc_of(This)->Container;
    WUC *child = own(visual), *next = nullptr;
    if (!child)
        return E_INVALIDARG;
    if (where == ABOVE || where == BELOW) {
        next = own(sibling);
        if (!next)
            return E_INVALIDARG;
    }
    lock(container);
    HRESULT hr = E_INVALIDARG;
    LONG at = next ? child_index(container, next) : 0;
    if (at >= 0 && next != child) {
        ULONG index = where == AT_TOP ? container->ChildCount : where == AT_BOTTOM ? 0
                      : where == ABOVE ? (ULONG)at + 1 : (ULONG)at;
        hr = insert(container, child, index);
    }
    unlock(container);
    return hr;
}

static HRESULT STDMETHODCALLTYPE collection_insert_above(void *This, void *visual, void *sibling)
{
    return place(This, visual, sibling, ABOVE);
}

static HRESULT STDMETHODCALLTYPE collection_insert_at_bottom(void *This, void *visual)
{
    return place(This, visual, nullptr, AT_BOTTOM);
}

static HRESULT STDMETHODCALLTYPE collection_insert_at_top(void *This, void *visual)
{
    return place(This, visual, nullptr, AT_TOP);
}

static HRESULT STDMETHODCALLTYPE collection_insert_below(void *This, void *visual, void *sibling)
{
    return place(This, visual, sibling, BELOW);
}

static HRESULT STDMETHODCALLTYPE collection_remove(void *This, void *visual)
{
    WUC *container = wuc_of(This)->Container;
    WUC *child = own(visual);
    if (!child)
        return E_INVALIDARG;
    lock(container);
    LONG at = child_index(container, child);
    if (at >= 0)
        remove_child_at(container, (ULONG)at);
    unlock(container);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE collection_remove_all(void *This)
{
    WUC *container = wuc_of(This)->Container;
    lock(container);
    while (container->ChildCount)
        remove_child_at(container, container->ChildCount - 1);
    unlock(container);
    return S_OK;
}

/* ------------------------------------------------------------------ the tables */

static void fill(int iface, int slot, const void *function)
{
    g_tables[iface][slot] = function;
}

#define FILL(iface, slot, function) fill(iface, slot, (const void *)(function))

static BOOL CALLBACK build_tables(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    for (int i = 0; i < I_COUNT; i++) {
        g_tables[i][0] = (const void *)base_query;
        g_tables[i][1] = (const void *)base_add_ref;
        g_tables[i][2] = (const void *)base_release;
        ULONG first = 3;
        if (g_interfaces[i].Inspectable) {
            g_tables[i][3] = (const void *)base_iids;
            g_tables[i][4] = (const void *)base_class_name;
            g_tables[i][5] = (const void *)base_trust_level;
            first = 6;
        }
        for (ULONG s = first; s < g_interfaces[i].Slots; s++)
            g_tables[i][s] = g_stubs[s];
    }
    FILL(I_CONTROLLER, 6, controller_get_compositor);
    FILL(I_CONTROLLER, 7, controller_commit);
    FILL(I_CONTROLLER, 9, controller_add_commit_needed);
    FILL(I_CONTROLLER, 10, controller_remove_commit_needed);

    FILL(I_COMPOSITOR, 7, compositor_create_color_brush);
    FILL(I_COMPOSITOR, 8, compositor_create_color_brush_with);
    FILL(I_COMPOSITOR, 9, compositor_create_container_visual);
    FILL(I_COMPOSITOR, 15, compositor_create_inset_clip);
    FILL(I_COMPOSITOR, 16, compositor_create_inset_clip_with);
    FILL(I_COMPOSITOR, 22, compositor_create_sprite_visual);
    FILL(I_COMPOSITOR, 23, compositor_create_surface_brush);
    FILL(I_COMPOSITOR, 24, compositor_create_surface_brush_with);
    FILL(I_COMPOSITOR_INTEROP, 3, interop_surface_for_handle);
    FILL(I_COMPOSITOR_INTEROP, 4, interop_surface_for_swap_chain);
    FILL(I_COMPOSITOR_INTEROP, 5, interop_create_graphics_device);
    FILL(I_DESKTOP_INTEROP, 3, desktop_create_target);
    FILL(I_DESKTOP_INTEROP, 4, desktop_ensure_on_thread);

    FILL(I_OBJECT, 6, object_get_compositor);
    FILL(I_OBJECT, 7, object_get_dispatcher);
    FILL(I_OBJECT3, 6, object_get_dispatcher_queue);
    FILL(I_CLOSABLE, 6, closable_close);

    FILL(I_TARGET, 6, target_get_root);
    FILL(I_TARGET, 7, target_put_root);
    FILL(I_DESKTOP_TARGET, 6, target_get_is_topmost);

    FILL(I_VISUAL, 6, visual_get_anchor);
    FILL(I_VISUAL, 7, visual_put_anchor);
    FILL(I_VISUAL, 8, visual_get_backface);
    FILL(I_VISUAL, 9, visual_put_backface);
    FILL(I_VISUAL, 10, visual_get_border);
    FILL(I_VISUAL, 11, visual_put_border);
    FILL(I_VISUAL, 12, visual_get_center);
    FILL(I_VISUAL, 13, visual_put_center);
    FILL(I_VISUAL, 14, visual_get_clip);
    FILL(I_VISUAL, 15, visual_put_clip);
    FILL(I_VISUAL, 16, visual_get_composite);
    FILL(I_VISUAL, 17, visual_put_composite);
    FILL(I_VISUAL, 18, visual_get_visible);
    FILL(I_VISUAL, 19, visual_put_visible);
    FILL(I_VISUAL, 20, visual_get_offset);
    FILL(I_VISUAL, 21, visual_put_offset);
    FILL(I_VISUAL, 22, visual_get_opacity);
    FILL(I_VISUAL, 23, visual_put_opacity);
    FILL(I_VISUAL, 24, visual_get_orientation);
    FILL(I_VISUAL, 25, visual_put_orientation);
    FILL(I_VISUAL, 26, visual_get_parent);
    FILL(I_VISUAL, 27, visual_get_angle);
    FILL(I_VISUAL, 28, visual_put_angle);
    FILL(I_VISUAL, 29, visual_get_degrees);
    FILL(I_VISUAL, 30, visual_put_degrees);
    FILL(I_VISUAL, 31, visual_get_axis);
    FILL(I_VISUAL, 32, visual_put_axis);
    FILL(I_VISUAL, 33, visual_get_scale);
    FILL(I_VISUAL, 34, visual_put_scale);
    FILL(I_VISUAL, 35, visual_get_size);
    FILL(I_VISUAL, 36, visual_put_size);
    FILL(I_VISUAL, 37, visual_get_matrix);
    FILL(I_VISUAL, 38, visual_put_matrix);
    FILL(I_VISUAL2, 6, visual_get_transform_parent);
    FILL(I_VISUAL2, 7, visual_put_transform_parent);
    FILL(I_VISUAL2, 8, visual_get_relative_offset);
    FILL(I_VISUAL2, 9, visual_put_relative_offset);
    FILL(I_VISUAL2, 10, visual_get_relative_size);
    FILL(I_VISUAL2, 11, visual_put_relative_size);
    FILL(I_VISUAL3, 6, visual_get_hit_test);
    FILL(I_VISUAL3, 7, visual_put_hit_test);
    FILL(I_CONTAINER, 6, container_get_children);
    FILL(I_SPRITE, 6, sprite_get_brush);
    FILL(I_SPRITE, 7, sprite_put_brush);

    FILL(I_COLLECTION, 6, collection_get_count);
    FILL(I_COLLECTION, 7, collection_insert_above);
    FILL(I_COLLECTION, 8, collection_insert_at_bottom);
    FILL(I_COLLECTION, 9, collection_insert_at_top);
    FILL(I_COLLECTION, 10, collection_insert_below);
    FILL(I_COLLECTION, 11, collection_remove);
    FILL(I_COLLECTION, 12, collection_remove_all);

    FILL(I_COLOR_BRUSH, 6, color_get);
    FILL(I_COLOR_BRUSH, 7, color_put);
    FILL(I_SURFACE_BRUSH, 6, surface_brush_get_interpolation);
    FILL(I_SURFACE_BRUSH, 7, surface_brush_put_interpolation);
    FILL(I_SURFACE_BRUSH, 8, surface_brush_get_align_x);
    FILL(I_SURFACE_BRUSH, 9, surface_brush_put_align_x);
    FILL(I_SURFACE_BRUSH, 10, surface_brush_get_stretch);
    FILL(I_SURFACE_BRUSH, 11, surface_brush_put_stretch);
    FILL(I_SURFACE_BRUSH, 12, surface_brush_get_surface);
    FILL(I_SURFACE_BRUSH, 13, surface_brush_put_surface);
    FILL(I_SURFACE_BRUSH, 14, surface_brush_get_align_y);
    FILL(I_SURFACE_BRUSH, 15, surface_brush_put_align_y);
    FILL(I_INSET_CLIP, 6, inset_get_bottom);
    FILL(I_INSET_CLIP, 7, inset_put_bottom);
    FILL(I_INSET_CLIP, 8, inset_get_left);
    FILL(I_INSET_CLIP, 9, inset_put_left);
    FILL(I_INSET_CLIP, 10, inset_get_right);
    FILL(I_INSET_CLIP, 11, inset_put_right);
    FILL(I_INSET_CLIP, 12, inset_get_top);
    FILL(I_INSET_CLIP, 13, inset_put_top);

    FILL(I_GRAPHICS_DEVICE, 6, graphics_create_drawing_surface);
    FILL(I_GRAPHICS_DEVICE2, 6, graphics_create_drawing_surface2);
    FILL(I_GRAPHICS_INTEROP, 3, graphics_get_rendering_device);
    FILL(I_GRAPHICS_INTEROP, 4, graphics_set_rendering_device);
    FILL(I_DRAWING_SURFACE, 6, drawing_get_alpha_mode);
    FILL(I_DRAWING_SURFACE, 7, drawing_get_pixel_format);
    FILL(I_DRAWING_SURFACE, 8, drawing_get_size);
    FILL(I_DRAWING_SURFACE2, 6, drawing_get_size_int);
    FILL(I_DRAWING_SURFACE2, 7, drawing_resize);
    FILL(I_DRAWING_INTEROP, 3, drawing_interop_begin_draw);
    FILL(I_DRAWING_INTEROP, 4, drawing_interop_end_draw);
    FILL(I_DRAWING_INTEROP, 5, drawing_interop_resize);
    FILL(I_DRAWING_INTEROP, 6, drawing_interop_scroll);
    FILL(I_DRAWING_INTEROP, 7, drawing_interop_resume_draw);
    FILL(I_DRAWING_INTEROP, 8, drawing_interop_suspend_draw);

    FILL(I_HANDLER, 3, handler_invoke);
    return TRUE;
}

/* ------------------------------------------------------------------ the factories */

/* the queue of the calling thread, or nothing when it has none. */
static WUC_QUEUE *current_queue(void)
{
    HSTRING_HEADER header;
    HSTRING name;
    WUC_QUEUE_STATICS *statics = nullptr;
    WUC_QUEUE *queue = nullptr;
    if (FAILED(WindowsCreateStringReference(L"Windows.System.DispatcherQueue", 30, &header, &name)) ||
        FAILED(RoGetActivationFactory(name, &IID_IDispatcherQueueStatics_, (void **)&statics)))
        return nullptr;
    if (FAILED(statics->lpVtbl->GetForCurrentThread(statics, &queue)))
        queue = nullptr;
    statics->lpVtbl->Release(statics);
    return queue;
}

/* both classes are made with no arguments, on a thread with a dispatcher queue as Windows asks. */
static HRESULT STDMETHODCALLTYPE factory_activate(void *This, void **instance)
{
    if (!instance)
        return E_POINTER;
    *instance = nullptr;
    WUC_QUEUE *queue = current_queue();
    if (!queue)
        return E_ACCESSDENIED;
    WUC *compositor = make(K_COMPOSITOR, nullptr);
    HRESULT hr = compositor ? DcDeviceCreate(2, nullptr, &compositor->Device) : E_OUTOFMEMORY;
    if (FAILED(hr)) {
        queue->lpVtbl->Release(queue);
        if (compositor)
            HeapFree(GetProcessHeap(), 0, compositor);
        return hr;
    }
    compositor->Queue = queue;
    if (!wuc_of(This)->MakesController) {
        *instance = face(compositor);
        return S_OK;
    }
    WUC *controller = make(K_CONTROLLER, nullptr);
    if (!controller) {
        release_wuc(compositor);
        return E_OUTOFMEMORY;
    }
    controller->Owned = compositor;
    compositor->Controller = controller;
    *instance = face(controller);
    return S_OK;
}

static WUC g_controller_factory, g_compositor_factory;
static INIT_ONCE g_factories_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factories(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    InitOnceExecuteOnce(&g_tables_once, build_tables, nullptr, nullptr);
    g_tables[I_FACTORY][6] = (const void *)factory_activate;
    WUC *factories[2] = {&g_controller_factory, &g_compositor_factory};
    for (int i = 0; i < 2; i++) {
        factories[i]->Faces[0] = (FACE){g_tables[I_FACTORY], factories[i]};
        /* the factories live as long as the process: their count never reaches zero. */
        factories[i]->Refs = 1;
        factories[i]->Kind = K_FACTORY;
    }
    g_controller_factory.MakesController = TRUE;
    return TRUE;
}

static BOOL name_is(HSTRING string, LPCWSTR name)
{
    UINT32 length;
    LPCWSTR text = WindowsGetStringRawBuffer(string, &length);
    return length == wcslen(name) && !memcmp(text, name, length * sizeof(WCHAR));
}

HRESULT DcWucFactory(void *class_id, void **factory)
{
    InitOnceExecuteOnce(&g_factories_once, make_factories, nullptr, nullptr);
    WUC *found = name_is(class_id, g_kinds[K_CONTROLLER].Name)   ? &g_controller_factory
                 : name_is(class_id, g_kinds[K_COMPOSITOR].Name) ? &g_compositor_factory
                                                                 : nullptr;
    if (!found)
        return CLASS_E_CLASSNOTAVAILABLE;
    add_ref(found);
    *factory = face(found);
    return S_OK;
}

#endif
