/*
 * d2d1.dll: direct2d drawn by the cpu. every render target draws into a surface of 32 bit premultiplied pixels with the
 * scan converter of lib/vector, and hands the pixels to the window, the device context, the wic bitmap or the dxgi
 * surface it was made for.
 */
#ifndef D2D_PRIVATE_H
#define D2D_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winuser.h>
#include <mime/win32/d2d1.h>
#include <mime/win32/d2d1effectauthor.h>
#include <mime/win32/d3d11.h>
#include <vector/vector.h>

#define D2D_UNUSED [[maybe_unused]]
#define VT(f) ((void *)(f))

#define WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR ((HRESULT)0x88982F80)
#define WINCODEC_ERR_WRONGSTATE_HR ((HRESULT)0x88982F04)

/* ------------------------------------------------------------------ memory */

void *D2dAlloc(SIZE_T size);
void *D2dAllocZero(SIZE_T size);
void *D2dRealloc(void *p, SIZE_T size);
void D2dFree(void *p);
/* memory for pixels, which is not given back to the heap at once. */
void *D2dPixelsAlloc(SIZE_T size, BOOL zero);
void D2dPixelsFree(void *p);
extern const MimeAllocator d2d_allocator;

/* a growing array of elements of one size. */
typedef struct D2dArray {
    void *data;
    UINT32 count, capacity;
} D2dArray;

BOOL D2dArrayGrow(D2dArray *a, UINT32 element_size, UINT32 extra);
static inline void D2dArrayFree(D2dArray *a)
{
    D2dFree(a->data);
    a->data = nullptr;
    a->count = a->capacity = 0;
}

/* ------------------------------------------------------------------ math */

float D2dPowF(float x, float y);
float D2dExpF(float x);
float D2dLogF(float x);
float D2dTanF(float x);
float D2dAbsF(float x);
float D2dFloorF(float x);
float D2dSqrtF(float x);
float D2dAtan2F(float y, float x);
void D2dSinCosF(float a, float *s, float *c);
int D2dRoundInt(float x);
static inline float D2dMinF(float a, float b) { return a < b ? a : b; }
static inline float D2dMaxF(float a, float b) { return a > b ? a : b; }
static inline float D2dClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline BOOL D2dFinite(float v) { return v == v && v - v == 0.0f; }

typedef D2D1_MATRIX_3X2_F D2dMatrix;
extern const D2dMatrix d2d_identity;
void D2dMatMul(D2dMatrix *out, const D2dMatrix *a, const D2dMatrix *b);
BOOL D2dMatInvert(D2dMatrix *out, const D2dMatrix *m);
static inline D2D1_POINT_2F D2dMatApply(const D2dMatrix *m, float x, float y)
{
    D2D1_POINT_2F p = {x * m->_11 + y * m->_21 + m->_31, x * m->_12 + y * m->_22 + m->_32};
    return p;
}
static inline const MimeVecMatrix *D2dVec(const D2dMatrix *m) { return (const MimeVecMatrix *)m; }
float D2dMatScale(const D2dMatrix *m);
BOOL D2dMatIsIdentity(const D2dMatrix *m);

/* ------------------------------------------------------------------ objects */

typedef struct D2dObj D2dObj;
typedef struct D2dClass D2dClass;

struct D2dClass {
    const char *name;
    const GUID *const *iids;
    void (*destroy)(D2dObj *obj);
    /* a second look at an interface the table does not name. */
    HRESULT (*query)(D2dObj *obj, REFIID riid, void **out);
};

typedef struct D2dFactory D2dFactory;

struct D2dObj {
    const void *vtbl;
    LONG refs;
    const D2dClass *cls;
    D2dFactory *factory;
};

/* makes a zeroed object of size bytes and takes a reference on the factory. */
void *D2dNew(SIZE_T size, const void *vtbl, const D2dClass *cls, D2dFactory *factory);
HRESULT STDMETHODCALLTYPE D2dQueryInterface(void *self, REFIID riid, void **out);
ULONG STDMETHODCALLTYPE D2dAddRef(void *self);
ULONG STDMETHODCALLTYPE D2dRelease(void *self);
void STDMETHODCALLTYPE D2dResourceGetFactory(void *self, ID2D1Factory **factory);
static inline void D2dReleaseObj(void *p)
{
    if (p)
        D2dRelease(p);
}
#define D2D_RELEASE(p)                                                                                                 \
    do {                                                                                                               \
        if (p) {                                                                                                       \
            ((IUnknown *)(p))->lpVtbl->Release((IUnknown *)(p));                                                       \
            (p) = nullptr;                                                                                             \
        }                                                                                                              \
    } while (0)
#define D2D_ADDREF(p)                                                                                                  \
    do {                                                                                                               \
        if (p)                                                                                                         \
            ((IUnknown *)(p))->lpVtbl->AddRef((IUnknown *)(p));                                                        \
    } while (0)

/* true when the object was made by this library and is one of the class. */
BOOL D2dIsOfClass(const void *p, const D2dClass *cls);
BOOL D2dIsOurs(const void *p);
BOOL D2dSameFactory(const void *a, const void *b);

#define D2D_IIDS(name, ...) static const GUID *const name[] = {&IID_IUnknown, __VA_ARGS__, nullptr}

/* ------------------------------------------------------------------ pixels */

/* a surface of premultiplied bgra pixels, a row is w pixels. */
typedef struct D2dSurface {
    int w, h;
    uint32_t *px;
} D2dSurface;

BOOL D2dSurfaceInit(D2dSurface *s, int w, int h);
void D2dSurfaceFree(D2dSurface *s);
static inline uint32_t *D2dSurfaceRow(const D2dSurface *s, int y) { return s->px + (SIZE_T)y * s->w; }

/* the pixel formats that bitmaps and targets can hold. */
typedef enum D2dFormat {
    D2D_FMT_BGRA8,
    D2D_FMT_BGRX8,
    D2D_FMT_RGBA8,
    D2D_FMT_A8,
    D2D_FMT_R8,
    D2D_FMT_RGBA16,
    D2D_FMT_RGBA16F,
    D2D_FMT_RGBA32F,
    D2D_FMT_RGB10A2,
    D2D_FMT_COUNT
} D2dFormat;

BOOL D2dFormatFromDxgi(DXGI_FORMAT dxgi, D2dFormat *out, BOOL *srgb);
UINT32 D2dFormatBytes(D2dFormat f);
/* converts n pixels from a native row to premultiplied bgra and back. alpha is a D2D1_ALPHA_MODE. */
void D2dFormatLoad(D2dFormat f, D2D1_ALPHA_MODE alpha, BOOL srgb, const void *src, uint32_t *dst, UINT32 n);
void D2dFormatStore(D2dFormat f, D2D1_ALPHA_MODE alpha, BOOL srgb, const uint32_t *src, void *dst, UINT32 n);
D2D1_ALPHA_MODE D2dDefaultAlpha(DXGI_FORMAT f);

/* srgb transfer, 8 bit in and out. */
float D2dSrgbToLinear(float v);
float D2dLinearToSrgb(float v);

static inline uint32_t D2dPackPremul(float r, float g, float b, float a)
{
    a = D2dClampF(a, 0.0f, 1.0f);
    r = D2dClampF(r, 0.0f, 1.0f) * a;
    g = D2dClampF(g, 0.0f, 1.0f) * a;
    b = D2dClampF(b, 0.0f, 1.0f) * a;
    return ((uint32_t)(a * 255.0f + 0.5f) << 24) | ((uint32_t)(r * 255.0f + 0.5f) << 16) |
           ((uint32_t)(g * 255.0f + 0.5f) << 8) | (uint32_t)(b * 255.0f + 0.5f);
}
/* straight 0xAARRGGBB of a color. */
static inline uint32_t D2dPackStraight(const D2D1_COLOR_F *c)
{
    float a = D2dClampF(c->a, 0.0f, 1.0f);
    return ((uint32_t)(a * 255.0f + 0.5f) << 24) | ((uint32_t)(D2dClampF(c->r, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
           ((uint32_t)(D2dClampF(c->g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
           (uint32_t)(D2dClampF(c->b, 0.0f, 1.0f) * 255.0f + 0.5f);
}
uint32_t D2dPremulPixel(uint32_t straight);
uint32_t D2dUnpremulPixel(uint32_t premul);
uint32_t D2dScalePixel(uint32_t px, uint32_t alpha);

/* ------------------------------------------------------------------ paths and geometry */

enum { D2D_SEG_LINE, D2D_SEG_BEZIER, D2D_SEG_QUAD, D2D_SEG_ARC };

typedef struct D2dSegment {
    UINT8 kind;
    UINT8 flags;
    D2D1_POINT_2F p[3];
    D2D1_ARC_SEGMENT arc;
} D2dSegment;

typedef struct D2dFigure {
    D2D1_POINT_2F start;
    UINT8 begin, end;
    UINT32 first, count;
} D2dFigure;

typedef struct D2dPath {
    D2dArray figures;
    D2dArray segments;
    D2D1_FILL_MODE fill_mode;
} D2dPath;

void D2dPathFree(D2dPath *p);
BOOL D2dPathCopy(D2dPath *dst, const D2dPath *src);
/* appends the figures of a path, transformed. arcs become beziers when the matrix is not a similarity. */
BOOL D2dPathAppend(D2dPath *dst, const D2dPath *src, const D2dMatrix *m);
/* a polygon outline as a path, one figure per subpath. */
BOOL D2dPathFromPolys(D2dPath *dst, const MimeVecPolys *polys);
BOOL D2dPathFlatten(const D2dPath *p, const D2dMatrix *m, float tolerance, MimeVecPolys *out, BOOL only_filled);
BOOL D2dPathCount(const D2dPath *p, UINT32 *figures, UINT32 *segments);

enum { D2D_GEO_RECT, D2D_GEO_ROUNDED, D2D_GEO_ELLIPSE, D2D_GEO_PATH, D2D_GEO_GROUP, D2D_GEO_TRANSFORMED };

typedef struct D2dGeometry D2dGeometry;
struct D2dGeometry {
    D2dObj obj;
    int kind;
    union {
        D2D1_RECT_F rect;
        D2D1_ROUNDED_RECT rounded;
        D2D1_ELLIPSE ellipse;
    } shape;
    D2dPath path;
    BOOL path_open;
    BOOL path_closed;
    UINT32 sink_count;
    D2D1_FILL_MODE group_mode;
    ID2D1Geometry **children;
    UINT32 child_count;
    ID2D1Geometry *source;
    D2dMatrix transform;
};

/* the geometry behind an interface pointer made by this library, nullptr for a foreign object. */
D2dGeometry *D2dGeometryOf(ID2D1Geometry *g);
/*
 * flattens into device polygons. tolerance is in the output space. winding tells the rule the polygons are meant
 * to be filled with, and only_filled drops hollow figures.
 */
HRESULT D2dGeometryFlatten(D2dGeometry *g, const D2dMatrix *m, float tolerance, BOOL only_filled, MimeVecPolys *out,
                           BOOL *winding);
HRESULT D2dGeometryBounds(D2dGeometry *g, const D2dMatrix *m, float tolerance, D2D1_RECT_F *bounds);

typedef struct D2dStroke D2dStroke;
struct D2dStroke {
    D2dObj obj;
    D2D1_STROKE_STYLE_PROPERTIES1 props;
    FLOAT *dashes;
    UINT32 dash_count;
    BOOL v1;
};
/* the stroker settings of a style and a width, nullptr style is a plain solid stroke. */
void D2dStrokeSetup(MimeVecStroke *s, ID2D1StrokeStyle *style, float width, float flatness, float scale,
                    const D2dMatrix *out, float **dash_storage);
/* strokes a geometry: flatten in geometry space, widen, transform into the output space. */
HRESULT D2dStrokeGeometry(D2dGeometry *g, float width, ID2D1StrokeStyle *style, const D2dMatrix *m, float tolerance,
                          MimeVecPolys *out);

HRESULT D2dCreateRectGeometry(D2dFactory *f, const D2D1_RECT_F *rect, ID2D1RectangleGeometry **out);
HRESULT D2dCreateRoundedGeometry(D2dFactory *f, const D2D1_ROUNDED_RECT *rect, ID2D1RoundedRectangleGeometry **out);
HRESULT D2dCreateEllipseGeometry(D2dFactory *f, const D2D1_ELLIPSE *e, ID2D1EllipseGeometry **out);
HRESULT D2dCreateGroupGeometry(D2dFactory *f, D2D1_FILL_MODE mode, ID2D1Geometry **list, UINT32 count,
                               ID2D1GeometryGroup **out);
HRESULT D2dCreateTransformedGeometry(D2dFactory *f, ID2D1Geometry *source, const D2D1_MATRIX_3X2_F *m,
                                     ID2D1TransformedGeometry **out);
HRESULT D2dCreatePathGeometry(D2dFactory *f, ID2D1PathGeometry1 **out);
HRESULT D2dCreateStrokeStyle(D2dFactory *f, const D2D1_STROKE_STYLE_PROPERTIES1 *props, BOOL v1, const FLOAT *dashes,
                             UINT32 count, ID2D1StrokeStyle1 **out);
HRESULT D2dCreateMesh(D2dFactory *f, ID2D1Mesh **out);
/* a mesh as polygons, for FillMesh. */
HRESULT D2dMeshPolys(ID2D1Mesh *mesh, const D2dMatrix *m, MimeVecPolys *out);

/* trapezoid scanning of polygons, for areas, tessellation and boolean operations. */
typedef struct D2dTrap {
    float y0, y1;
    float xl0, xl1, xr0, xr1;
} D2dTrap;
/* a region as trapezoids between two sets of polygons combined by an operation. b may be nullptr. */
HRESULT D2dRegionTraps(const MimeVecPolys *a, BOOL a_winding, const MimeVecPolys *b, BOOL b_winding, int op,
                       D2dArray *traps);
HRESULT D2dTrapsToPolys(const D2dArray *traps, MimeVecPolys *out);

/* ------------------------------------------------------------------ gradients and brushes */

typedef struct D2dGradient D2dGradient;
struct D2dGradient {
    D2dObj obj;
    D2D1_GRADIENT_STOP *stops;
    UINT32 count;
    D2D1_GAMMA gamma;
    D2D1_EXTEND_MODE extend;
    D2D1_COLOR_SPACE pre, post;
    D2D1_BUFFER_PRECISION precision;
    D2D1_COLOR_INTERPOLATION_MODE mode;
    BOOL v1;
    uint32_t lut[1024];
    BOOL lut_valid;
};

enum { D2D_BRUSH_SOLID, D2D_BRUSH_LINEAR, D2D_BRUSH_RADIAL, D2D_BRUSH_BITMAP, D2D_BRUSH_IMAGE };

typedef struct D2dBitmap D2dBitmap;

typedef struct D2dBrush {
    D2dObj obj;
    int kind;
    float opacity;
    D2dMatrix transform;
    D2D1_COLOR_F color;
    D2dGradient *gradient;
    D2D1_POINT_2F p0, p1;
    D2D1_POINT_2F origin;
    float rx, ry;
    D2dBitmap *bitmap;
    ID2D1Image *image;
    D2D1_RECT_F source;
    D2D1_EXTEND_MODE extend_x, extend_y;
    D2D1_INTERPOLATION_MODE interpolation;
} D2dBrush;

D2dBrush *D2dBrushOf(ID2D1Brush *b);
HRESULT D2dCreateGradient(D2dFactory *f, const D2D1_GRADIENT_STOP *stops, UINT32 count, D2D1_GAMMA gamma,
                          D2D1_EXTEND_MODE extend, BOOL v1, D2dGradient **out);
D2dGradient *D2dGradientOf(ID2D1GradientStopCollection *g);
void D2dGradientBuild(D2dGradient *g);
HRESULT D2dCreateSolidBrush(D2dFactory *f, const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *props,
                            ID2D1SolidColorBrush **out);
HRESULT D2dCreateLinearBrush(D2dFactory *f, const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gp,
                             const D2D1_BRUSH_PROPERTIES *props, ID2D1GradientStopCollection *stops,
                             ID2D1LinearGradientBrush **out);
HRESULT D2dCreateRadialBrush(D2dFactory *f, const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gp,
                             const D2D1_BRUSH_PROPERTIES *props, ID2D1GradientStopCollection *stops,
                             ID2D1RadialGradientBrush **out);
HRESULT D2dCreateBitmapBrush(D2dFactory *f, ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES1 *bp,
                             const D2D1_BRUSH_PROPERTIES *props, BOOL v1, ID2D1BitmapBrush1 **out);
HRESULT D2dCreateImageBrush(D2dFactory *f, ID2D1Image *image, const D2D1_IMAGE_BRUSH_PROPERTIES *ip,
                            const D2D1_BRUSH_PROPERTIES *props, ID2D1ImageBrush **out);

/* ------------------------------------------------------------------ bitmaps */

struct D2dBitmap {
    D2dObj obj;
    UINT32 width, height;
    DXGI_FORMAT dxgi;
    D2dFormat format;
    BOOL srgb;
    D2D1_ALPHA_MODE alpha;
    float dpi_x, dpi_y;
    D2D1_BITMAP_OPTIONS options;
    BOOL v1;
    /* the pixels in the native format, and the premultiplied bgra view of them. */
    BYTE *native;
    UINT32 stride;
    /*
     * a bitmap made from an 8 bit indexed source stays as its indices and a premultiplied bgra palette, a quarter
     * of the memory to fill and to read back, until something asks for its pixels. native is null until then.
     */
    BYTE *index;
    uint32_t *palette;
    D2dSurface view;
    BOOL view_valid;
    BOOL view_dirty;
    BOOL mapped;
    /* the target of a source transform, which the cpu may map for writing. */
    BOOL writable;
    /* a dxgi surface that this bitmap mirrors. */
    IDXGISurface *surface;
    ID2D1ColorContext *color_context;
    LONG bound_as_target;
};

D2dBitmap *D2dBitmapOf(void *p);
HRESULT D2dBitmapCreate(D2dFactory *f, UINT32 w, UINT32 h, DXGI_FORMAT format, D2D1_ALPHA_MODE alpha, float dpi_x,
                        float dpi_y, D2D1_BITMAP_OPTIONS options, BOOL v1, D2dBitmap **out);
HRESULT D2dBitmapCreateFilled(D2dFactory *f, UINT32 w, UINT32 h, DXGI_FORMAT format, D2D1_ALPHA_MODE alpha, float dpi_x,
                              float dpi_y, D2D1_BITMAP_OPTIONS options, BOOL v1, D2dBitmap **out);
/* a bitmap of indices, which the caller fills along with the 256 entries of the palette. */
HRESULT D2dBitmapCreateIndexed(D2dFactory *f, UINT32 w, UINT32 h, float dpi_x, float dpi_y, D2D1_BITMAP_OPTIONS options,
                               D2dBitmap **out);
/* the native pixels, made from the indices when the bitmap still has only those. null when memory runs out. */
BYTE *D2dBitmapNative(D2dBitmap *b);
/* the premultiplied bgra pixels of the bitmap, rebuilt when the native ones changed. */
const D2dSurface *D2dBitmapView(D2dBitmap *b);
/* after drawing into the view of a bitmap that is a target, writes the view back to the native pixels. */
void D2dBitmapCommit(D2dBitmap *b);
/* the premultiplied rgba floats of a float bitmap as it stores them, nullptr for other formats. */
float *D2dBitmapFloats(D2dBitmap *b);
BOOL D2dBitmapIsFloat(const D2dBitmap *b);
/* n premultiplied rgba floats of a row of a float bitmap. */
void D2dBitmapFloatRow(const D2dBitmap *b, UINT32 x0, UINT32 y, UINT32 n, float *d);
HRESULT D2dBitmapCopyIn(D2dBitmap *b, UINT32 x, UINT32 y, const void *src, UINT32 pitch, UINT32 w, UINT32 h);
/* the dxgi surface read into the bitmap or the bitmap written to the surface. */
HRESULT D2dBitmapReadSurface(D2dBitmap *b);
HRESULT D2dBitmapWriteSurface(D2dBitmap *b);
HRESULT D2dCreateColorContextDxgi(D2dFactory *f, UINT32 dxgi, ID2D1ColorContext **out);
HRESULT D2dCreateColorContextSimple(D2dFactory *f, const void *profile, ID2D1ColorContext **out);
HRESULT D2dCreateColorContext(D2dFactory *f, D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 size,
                              ID2D1ColorContext **out);
HRESULT D2dColorContextFromFile(D2dFactory *f, const WCHAR *path, ID2D1ColorContext **out);
HRESULT D2dColorContextFromWic(D2dFactory *f, IWICColorContext *wic, ID2D1ColorContext **out);
/* converts premultiplied bgra pixels in place between color contexts, for the color management effect. */
void D2dColorManage(ID2D1ColorContext *source, UINT32 source_intent, ID2D1ColorContext *destination,
                    UINT32 destination_intent, UINT32 alpha_mode, D2dSurface *surface);

/* ------------------------------------------------------------------ the factory */

struct D2dFactory {
    D2dObj obj;
    const void *multithread_vtbl;
    D2D1_FACTORY_TYPE type;
    D2D1_DEBUG_LEVEL debug;
    CRITICAL_SECTION lock;
    float dpi_x, dpi_y;
    LONG registered_count;
    D2dArray effects;
    ID2D1Device *device;
};

D2dFactory *D2dFactoryOf(ID2D1Factory *f);
void D2dFactoryEnter(D2dFactory *f);
void D2dFactoryLeave(D2dFactory *f);
extern const D2dClass d2d_factory_class;
HRESULT D2dFactoryCreate(D2D1_FACTORY_TYPE type, const D2D1_FACTORY_OPTIONS *options, REFIID riid, void **out);

/* ------------------------------------------------------------------ render targets */

typedef struct D2dRt D2dRt;

enum { D2D_RT_BITMAP, D2D_RT_HWND, D2D_RT_DC, D2D_RT_WIC, D2D_RT_DXGI, D2D_RT_CONTEXT };

typedef struct D2dLayerRec {
    D2dSurface surface;
    D2dSurface *parent;
    /* the target of a device context the layer began on, which it goes back to when that is the target again. */
    void *target;
    MimeVecMask mask;
    MimeVecRect bounds;
    float opacity;
    ID2D1Brush *brush;
    D2dMatrix brush_matrix;
    BOOL ignore_alpha;
} D2dLayerRec;

typedef struct D2dClipRec {
    MimeVecMask saved;
    BOOL saved_set;
    D2dLayerRec *layer;
} D2dClipRec;

typedef struct D2dState {
    D2D1_ANTIALIAS_MODE aa;
    D2D1_TEXT_ANTIALIAS_MODE text_aa;
    D2D1_TAG tag1, tag2;
    D2dMatrix transform;
    D2D1_PRIMITIVE_BLEND blend;
    D2D1_UNIT_MODE units;
    IDWriteRenderingParams *params;
} D2dState;

typedef struct D2dStateBlock {
    D2dObj obj;
    D2dState state;
    BOOL v1;
} D2dStateBlock;

struct D2dRt {
    D2dObj obj;
    const void *gdi_vtbl;
    const void *ctx_vtbl;
    int kind;
    BOOL context;
    BOOL context_vtbl;
    /* where the pixels go. */
    D2dSurface surface;
    D2dSurface *draw;
    UINT32 pixel_w, pixel_h;
    D2D1_PIXEL_FORMAT pixel_format;
    D2dFormat format;
    BOOL srgb;
    float dpi_x, dpi_y;
    D2D1_RENDER_TARGET_PROPERTIES props;
    BOOL drawing;
    BOOL gdi_compatible;
    HRESULT error;
    D2dState state;
    D2dArray state_stack;
    D2dArray clips;
    MimeVecMask clip;
    BOOL clip_set;
    UINT32 clip_depth;
    /* the clip of the target drawing began on, put aside while another target is drawn to. */
    MimeVecMask home_clip;
    BOOL home_clip_set;
    void *home_clip_target;
    /* targets. */
    HWND hwnd;
    HDC dc;
    RECT dc_rect;
    IWICBitmap *wic;
    IDXGISurface *dxgi_surface;
    D2dBitmap *target_bitmap;
    D2dBitmap *own_bitmap;
    D2D1_PRESENT_OPTIONS present;
    BOOL got_dc;
    HDC interop_dc;
    HBITMAP interop_bitmap;
    void *interop_bits;
    HGDIOBJ interop_old;
    D2D1_DC_INITIALIZE_MODE interop_mode;
    /* device context state. */
    ID2D1Device *device;
    ID2D1Image *target_image;
    D2D1_RENDERING_CONTROLS controls;
    ID2D1Factory *factory_ref;
    D2D1_DEVICE_CONTEXT_OPTIONS context_options;
    /* a target that is not a device context but is used as one keeps its own surface for a null target. */
    BOOL home_saved;
    D2D1_PIXEL_FORMAT home_format;
    D2dFormat home_fmt;
    BOOL home_srgb;
    float home_dpi_x, home_dpi_y;
    /* text. */
    IDWriteFactory *dwrite;
    LONG pending_layers;
    LONG pending_clips;
};

D2dRt *D2dRtOf(void *p);
/* what a device context draws into now: its target bitmap or command list, nullptr for its own surface. */
static inline void *D2dRtTargetKey(const D2dRt *rt) { return rt->target_bitmap ? (void *)rt->target_bitmap : (void *)rt->target_image; }
extern const D2dClass d2d_rt_class;
HRESULT D2dRtCreate(D2dFactory *f, int kind, const D2D1_RENDER_TARGET_PROPERTIES *props, D2dRt **out);
HRESULT D2dDeviceCreate(D2dFactory *f, IDXGIDevice *dxgi, ID2D1Device **out);
/* the device behind every window, dc, wic and compatible target of a factory, with a reference. */
ID2D1Device *D2dFactoryDevice(D2dFactory *f);
HRESULT D2dRtResize(D2dRt *rt, UINT32 w, UINT32 h);
void D2dRtInitState(D2dRt *rt);
void D2dRtDestroyContents(D2dRt *rt);
/* the vtables of the target classes. */
extern const void *d2d_rt_vtbl_bitmap, *d2d_rt_vtbl_hwnd, *d2d_rt_vtbl_dc, *d2d_rt_vtbl_context;
extern const void *d2d_gdi_vtbl;

HRESULT D2dCreateFactoryTargets(D2dFactory *f);

/* the drawing core. */
void D2dClipReset(D2dRt *rt);
BOOL D2dClipIsEmpty(D2dRt *rt);
void D2dBeginDrawCore(D2dRt *rt);
HRESULT D2dEndDrawCore(D2dRt *rt, D2D1_TAG *tag1, D2D1_TAG *tag2);
void D2dRtSwitchTarget(D2dRt *rt, BOOL begin);
void D2dRtFreeHomeClip(D2dRt *rt);
void D2dClearSurface(D2dRt *rt, const D2D1_COLOR_F *color);

/*
 * paint is what a primitive fills with. a brush with an opacity and a transform, or a solid pixel.
 */

typedef struct D2dPaint {
    D2dBrush *brush;
    BOOL is_solid;
    uint32_t solid;
    float opacity;
    /* device pixel to brush space. */
    D2dMatrix inverse;
    BOOL valid;
    /* a texture to sample: the pixels, and how brush space maps to texels. */
    const D2dSurface *tex;
    /* when set, tex has no pixels: a texel is the palette entry of its index, rows as wide as tex. */
    const BYTE *tex_index;
    const uint32_t *tex_palette;
    void *held;
    float tex_sx, tex_sy, tex_ox, tex_oy;
    int clamp_x0, clamp_y0, clamp_x1, clamp_y1;
    D2D1_EXTEND_MODE ex, ey;
    D2D1_INTERPOLATION_MODE interp;
    D2dGradient *gradient;
} D2dPaint;

BOOL D2dPaintSetup(D2dRt *rt, D2dPaint *paint, ID2D1Brush *brush, const D2dMatrix *extra_world);
/* fills the polygons with paint, the polygons in device pixels. */
void D2dFillPolys(D2dRt *rt, const MimeVecPolys *polys, BOOL winding, BOOL aliased, D2dPaint *paint);
/* the pixels of paint for one row, premultiplied. */
void D2dPaintRow(D2dRt *rt, D2dPaint *paint, int x, int y, int count, uint32_t *out);
void D2dFillRect(D2dRt *rt, const D2D1_RECT_F *rect, ID2D1Brush *brush);
void D2dFillGeometry(D2dRt *rt, ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush);
void D2dDrawGeometry(D2dRt *rt, ID2D1Geometry *geometry, ID2D1Brush *brush, float width, ID2D1StrokeStyle *style);
void D2dDrawBitmapRect(D2dRt *rt, D2dBitmap *bitmap, const D2D1_RECT_F *dest, float opacity,
                       D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source, const D2dMatrix *extra);
void D2dDrawSurface(D2dRt *rt, const D2dSurface *src, const D2D1_RECT_F *dest_dip, float opacity,
                    D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source_px);
/* blends a premultiplied surface onto the target at an integer offset, under clip and opacity. */
void D2dBlendSurface(D2dRt *rt, const D2dSurface *src, int dx, int dy, float opacity);
void D2dPushAxisAlignedClip(D2dRt *rt, const D2D1_RECT_F *clip, D2D1_ANTIALIAS_MODE mode);
void D2dPopAxisAlignedClip(D2dRt *rt);
void D2dPushLayer(D2dRt *rt, const D2D1_LAYER_PARAMETERS1 *params);
void D2dPopLayer(D2dRt *rt);
void D2dDrawGlyphRun(D2dRt *rt, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run, ID2D1Brush *brush,
                     DWRITE_MEASURING_MODE measuring);
void D2dDrawTextLayout(D2dRt *rt, D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush,
                       D2D1_DRAW_TEXT_OPTIONS options);
void D2dDrawText(D2dRt *rt, const WCHAR *string, UINT32 length, IDWriteTextFormat *format, const D2D1_RECT_F *rect,
                 ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring);
void D2dFillOpacityMask(D2dRt *rt, D2dBitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dest,
                        const D2D1_RECT_F *source);
float D2dUnitScaleX(D2dRt *rt);
float D2dUnitScaleY(D2dRt *rt);
/* the matrix from user space to pixels, dpi and unit mode included. */
void D2dWorldMatrix(D2dRt *rt, D2dMatrix *m);
HRESULT D2dGlyphRunToPath(IDWriteFontFace *face, float em_size, const UINT16 *glyphs, const FLOAT *advances,
                          const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl, D2dPath *path);

/* ------------------------------------------------------------------ svg */

HRESULT D2dSvgCreate(D2dRt *rt, IStream *stream, D2D1_SIZE_F viewport, ID2D1SvgDocument **out);
void D2dSvgDraw(D2dRt *rt, ID2D1SvgDocument *document);

/* ------------------------------------------------------------------ effects and images */

typedef struct D2dEffect D2dEffect;
/* a rectangle of device pixels, D2D_INF_EXTENT standing for no edge. */
typedef struct D2dIRect {
    int x0, y0, x1, y1;
} D2dIRect;
#define D2D_INF_EXTENT (1 << 24)
/* how deep images may nest, as effects inside the transform graphs of effects; paint.net goes past 16. */
#define D2D_MAX_DEPTH 64
/* the pixels of an image in device pixels, relative to the origin of the image space. */
typedef struct D2dImageData {
    D2dSurface surface;
    int x, y;
    /* the same pixels as premultiplied rgba floats when they were made with more than 8 bits, or nullptr. */
    float *fpx;
} D2dImageData;
HRESULT D2dRenderImage(D2dRt *rt, ID2D1Image *image, D2dImageData *out);
void D2dImageDataFree(D2dImageData *d);
HRESULT D2dCreateEffect(D2dRt *rt, REFCLSID clsid, ID2D1Effect **out);
HRESULT D2dCreateCommandList(D2dRt *rt, ID2D1CommandList **out);
HRESULT D2dImageBounds(D2dRt *rt, ID2D1Image *image, D2D1_RECT_F *bounds);
void D2dDrawImageCore(D2dRt *rt, ID2D1Image *image, const D2D1_POINT_2F *offset, const D2D1_RECT_F *source,
                      D2D1_INTERPOLATION_MODE mode, UINT32 composite);
HRESULT D2dEffectEnumerate(D2dFactory *f, CLSID *list, UINT32 count, UINT32 *returned, UINT32 *registered);
HRESULT D2dEffectUnregister(D2dFactory *f, REFCLSID clsid);
HRESULT D2dEffectProperties(D2dFactory *f, REFCLSID clsid, ID2D1Properties **out);
HRESULT D2dCreateEffectF(D2dFactory *f, float dpi, REFCLSID clsid, ID2D1Effect **out);
/* the pixels of an image over an area, and its bounds, depth counting the nesting of effects. */
HRESULT D2dImageGet(D2dRt *rt, ID2D1Image *image, D2dIRect want, D2dImageData *out, int depth);
HRESULT D2dImageBoundsPx(D2dRt *rt, ID2D1Image *image, D2dIRect *b, int depth);

/* an image whose bounds and pixels come from callbacks, for the inputs of effects inside transform graphs. */
typedef struct D2dProxyOps {
    HRESULT (*bounds)(void *ctx, D2dIRect *b, int depth);
    HRESULT (*get)(void *ctx, D2dIRect want, D2dImageData *out, int depth);
} D2dProxyOps;
HRESULT D2dProxyCreate(D2dFactory *f, const D2dProxyOps *ops, void *ctx, ID2D1Image **out);

/* ------------------------------------------------------------------ custom effects */

HRESULT D2dCustomRegister(D2dFactory *f, REFCLSID clsid, const WCHAR *xml, const void *bindings, UINT32 count,
                          void *factory);
HRESULT D2dCustomUnregister(D2dFactory *f, REFCLSID clsid);
/* the clsids of the registered custom effects, the count of them returned. */
UINT32 D2dCustomList(D2dFactory *f, CLSID *list, UINT32 max);
HRESULT D2dCustomProperties(D2dFactory *f, REFCLSID clsid, ID2D1Properties **out);
HRESULT D2dCustomCreate(D2dFactory *f, float dpi, REFCLSID clsid, ID2D1Effect **out);
void D2dCustomFreeRegistry(D2dFactory *f);

/* shaders of custom effects run through direct3d 11. kind is 0 for pixel, 1 for vertex and 2 for compute shaders. */
enum { D2D_SHADER_PIXEL, D2D_SHADER_VERTEX, D2D_SHADER_COMPUTE };
HRESULT D2dGpuLoadShader(REFGUID id, int kind, const BYTE *code, UINT32 size);
BOOL D2dGpuShaderLoaded(REFGUID id);
typedef struct D2dGpuTexture D2dGpuTexture;
HRESULT D2dGpuTextureCreate(const D2D1_RESOURCE_TEXTURE_PROPERTIES *p, const BYTE *data, const UINT32 *strides, UINT32 size,
                            D2dGpuTexture **out);
HRESULT D2dGpuTextureUpdate(D2dGpuTexture *t, const UINT32 *lo, const UINT32 *hi, const UINT32 *strides, UINT32 dims, const BYTE *data,
                            UINT32 count);
void D2dGpuTextureFree(D2dGpuTexture *t);
/* one draw or dispatch: inputs in pixels of the scene, resource textures at their slots, the output rectangle. */
typedef struct D2dGpuPass {
    const GUID *shader;
    const BYTE *cb;
    UINT32 cb_size;
    D2dGpuTexture *const *res;
    const UINT32 *res_slot;
    UINT32 res_count;
    const D2dImageData *const *inputs;
    const UINT32 *filters;
    UINT32 input_count;
    D2dIRect out;
    UINT32 groups[3];
    /* the output holds floats, as an intermediate of more than 8 bits does. */
    BOOL wide;
    /* for an analysis transform, where the bytes the shader wrote go instead of an image. */
    BYTE **analysis;
    UINT32 *analysis_size;
} D2dGpuPass;
HRESULT D2dGpuDraw(const D2dGpuPass *p, D2dImageData *out);
HRESULT D2dGpuCompute(const D2dGpuPass *p, D2dImageData *out);
/* a custom effect or its output image, nullptr for anything else. */
void *D2dCustomOf(const void *image);
/* effects are images too: their ID2D1Image interface sits right after the object header. */
extern const void *const d2d_effect_image_vtbl[];
static inline void *D2dEffectFromImage(const void *p)
{
    return p && *(const void *const *)p == (const void *)d2d_effect_image_vtbl ? (BYTE *)p - sizeof(D2dObj) : nullptr;
}
HRESULT D2dEffectQueryImage(D2dObj *obj, REFIID riid, void **out);
BOOL D2dIsEffect(const void *p);
HRESULT D2dCustomBounds(D2dRt *rt, ID2D1Image *image, D2dIRect *b, int depth);
HRESULT D2dCustomRender(D2dRt *rt, ID2D1Image *image, D2dIRect want, D2dImageData *out, int depth);

#define D2DERR_UNSUPPORTED_PIXEL_FORMAT_HR WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT_HR
#define D2DERR_INSUFFICIENT_BUFFER_HR HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)

/* path building, for sinks and for the shapes that draw once. */
BOOL D2dPathBegin(D2dPath *p, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin);
BOOL D2dPathAdd(D2dPath *p, const D2dSegment *s);
BOOL D2dPathLine(D2dPath *p, D2D1_POINT_2F pt);
BOOL D2dPathBezier(D2dPath *p, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c);
void D2dPathEnd(D2dPath *p, D2D1_FIGURE_END end);
/* a geometry on the stack that stands for a shape drawn once. */
void D2dInitTempGeometry(D2dGeometry *g, int kind);
BOOL D2dIsContextClass(const D2dClass *c);
/* the device context interface of a target that is not made as one. */
D2dRt *D2dRtFromContextPtr(void *p);
HRESULT D2dRtQueryContext(D2dObj *obj, REFIID riid, void **out);
void D2dPaintRelease(D2dPaint *p);
BOOL D2dPaintTexture(D2dPaint *paint, const D2dSurface *tex, const D2dMatrix *texel_to_device, float opacity,
                     D2D1_INTERPOLATION_MODE interp, D2D1_EXTEND_MODE ex, D2D1_EXTEND_MODE ey, const MimeVecRect *clamp);
void D2dDrawTexture(D2dRt *rt, const D2dSurface *tex, const D2dMatrix *texel_to_device, const MimeVecPolys *area, float opacity,
                    D2D1_INTERPOLATION_MODE interp, const MimeVecRect *clamp);
HRESULT D2dStateBlockCreate(D2dFactory *f, const D2D1_DRAWING_STATE_DESCRIPTION1 *desc, IDWriteRenderingParams *params, BOOL v1,
                            ID2D1DrawingStateBlock1 **out);

#endif
