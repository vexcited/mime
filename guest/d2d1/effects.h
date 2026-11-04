#ifndef D2D_EFFECTS_H
#define D2D_EFFECTS_H

#include "d2d_private.h"

enum {
    EFF_FLOOD, EFF_COLORMATRIX, EFF_BLUR, EFF_COMPOSITE, EFF_CROP, EFF_AFFINE, EFF_OPACITY, EFF_TINT, EFF_SHADOW,
    EFF_BRIGHTNESS, EFF_SATURATION, EFF_GRAYSCALE, EFF_INVERT, EFF_SEPIA, EFF_PREMULTIPLY, EFF_UNPREMULTIPLY, EFF_BLEND,
    EFF_BORDER, EFF_ARITHMETIC, EFF_HUEROTATE, EFF_CONTRAST, EFF_EXPOSURE, EFF_LUMINANCE, EFF_TILE, EFF_COLORMANAGEMENT,
    EFF_3DTRANSFORM, EFF_3DPERSPECTIVE, EFF_ATLAS, EFF_BITMAPSOURCE, EFF_CONVOLVE, EFF_DIRECTIONALBLUR,
    EFF_DISCRETETRANSFER, EFF_DISPLACEMENT, EFF_DISTANTDIFFUSE, EFF_DISTANTSPECULAR, EFF_DPICOMPENSATION, EFF_GAMMATRANSFER,
    EFF_SCALE, EFF_HISTOGRAM, EFF_LINEARTRANSFER, EFF_MORPHOLOGY, EFF_OPACITYMETADATA, EFF_POINTDIFFUSE, EFF_POINTSPECULAR,
    EFF_SPOTDIFFUSE, EFF_SPOTSPECULAR, EFF_TABLETRANSFER, EFF_TURBULENCE, EFF_YCBCR, EFF_RGBTOHUE, EFF_HUETORGB, EFF_CHROMAKEY,
    EFF_EMBOSS, EFF_POSTERIZE, EFF_SHARPEN, EFF_STRAIGHTEN, EFF_TEMPERATURETINT, EFF_VIGNETTE, EFF_EDGEDETECTION,
    EFF_HIGHLIGHTSSHADOWS, EFF_LOOKUPTABLE3D, EFF_ALPHAMASK, EFF_CROSSFADE, EFF_WHITELEVEL, EFF_HDRTONEMAP, EFF_COUNT
};

#define EFF_MAX_PROPS 17

/* values of effect enumerations the headers do not name. */
enum { FX_STRAIGHT = 2, FX_HARD = 1, FX_PREWITT = 1, FX_GAMMA_SRGB = 1, FX_FRACTAL_SUM = 0, FX_DILATE = 1 };

/* a list of floats of a blob property. */
typedef struct EffBlob {
    BYTE *data;
    UINT32 size;
} EffBlob;

struct D2dEffect {
    D2dObj obj;
    const void *image_vtbl;
    int id;
    BYTE props[EFF_MAX_PROPS][80];
    ID2D1Image *inputs[8];
    UINT32 input_count;
    BOOL cached;
    D2D1_BUFFER_PRECISION precision;
    D2dRt *rt;
    /* what the histogram effect counted the last time it was drawn. */
    float *histogram;
    UINT32 histogram_bins;
};

static inline float *D2dFxF(D2dEffect *e, int i) { return (float *)e->props[i]; }
static inline UINT32 *D2dFxU(D2dEffect *e, int i) { return (UINT32 *)e->props[i]; }
static inline EffBlob *D2dFxB(D2dEffect *e, int i) { return (EffBlob *)e->props[i]; }

typedef D2dIRect FxRect;

static inline FxRect fx_intersect(FxRect a, FxRect b)
{
    FxRect r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
}
static inline BOOL fx_empty(FxRect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
static inline FxRect fx_union(FxRect a, FxRect b)
{
    if (fx_empty(a))
        return b;
    if (fx_empty(b))
        return a;
    FxRect r = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
    return r;
}
static inline FxRect fx_inflate(FxRect r, int dx, int dy)
{
    if (r.x0 > -D2D_INF_EXTENT)
        r.x0 -= dx;
    if (r.x1 < D2D_INF_EXTENT)
        r.x1 += dx;
    if (r.y0 > -D2D_INF_EXTENT)
        r.y0 -= dy;
    if (r.y1 < D2D_INF_EXTENT)
        r.y1 += dy;
    return r;
}
static inline BOOL fx_infinite(FxRect r) { return r.x0 <= -D2D_INF_EXTENT || r.y0 <= -D2D_INF_EXTENT || r.x1 >= D2D_INF_EXTENT || r.y1 >= D2D_INF_EXTENT; }
static const FxRect fx_inf = {-D2D_INF_EXTENT, -D2D_INF_EXTENT, D2D_INF_EXTENT, D2D_INF_EXTENT};

/* a float image: premultiplied rgba, four floats a pixel, placed at x and y of the image space. */
typedef struct FxImg {
    int x, y, w, h;
    float *px;
} FxImg;

static inline float *fx_px(const FxImg *im, int x, int y) { return im->px + ((SIZE_T)(y - im->y) * im->w + (x - im->x)) * 4; }
static inline FxRect fx_rect(const FxImg *im) { return (FxRect){im->x, im->y, im->x + im->w, im->y + im->h}; }

HRESULT FxNew(FxImg *im, FxRect r);
void FxFree(FxImg *im);

/* what an effect renders with. */
typedef struct FxCtx {
    D2dRt *rt;
    D2dEffect *e;
    int depth;
    /* device pixels per dip. */
    float scale;
    /* the output keeps its float pixels, set when an input had them or the precision asks for more than 8 bits. */
    BOOL keep_float;
} FxCtx;

HRESULT FxInputBounds(FxCtx *c, UINT32 i, FxRect *b);
/* the pixels of input i over want, transparent where it has none. */
HRESULT FxInput(FxCtx *c, UINT32 i, FxRect want, FxImg *out);

/* rendering of the built in effects, in device pixels. */
HRESULT D2dFxBounds(D2dRt *rt, D2dEffect *e, FxRect *b, int depth);
HRESULT D2dFxRender(D2dRt *rt, D2dEffect *e, FxRect want, D2dImageData *out, int depth);
/* checks and clamps a value given to a property, as windows' setters do. data may be rewritten in place. */
HRESULT D2dFxCheckValue(D2dEffect *e, UINT32 index, BYTE *data, UINT32 size);
HRESULT D2dFxHistogram(D2dEffect *e, BYTE *data, UINT32 size);

/* the parts. */
typedef HRESULT (*FxBoundsFn)(FxCtx *c, FxRect *b);
typedef HRESULT (*FxRenderFn)(FxCtx *c, FxRect area, FxImg *out);

HRESULT FxColorBounds(FxCtx *c, FxRect *b);
HRESULT FxColorRender(FxCtx *c, FxRect area, FxImg *out);
HRESULT FxCompositeBounds(FxCtx *c, FxRect *b);
HRESULT FxCompositeRender(FxCtx *c, FxRect area, FxImg *out);
HRESULT FxFilterBounds(FxCtx *c, FxRect *b);
HRESULT FxFilterRender(FxCtx *c, FxRect area, FxImg *out);
HRESULT FxTransformBounds(FxCtx *c, FxRect *b);
HRESULT FxTransformRender(FxCtx *c, FxRect area, FxImg *out);
HRESULT FxLightBounds(FxCtx *c, FxRect *b);
HRESULT FxLightRender(FxCtx *c, FxRect area, FxImg *out);

/* a 3d lookup table made by a device context or an effect context. */
typedef struct D2dLut3D {
    D2dObj obj;
    UINT32 n[3];
    /* rgba floats, blue fastest, then green, then red. */
    float *data;
} D2dLut3D;
D2dLut3D *D2dLut3DOf(void *p);
HRESULT D2dLut3DCreate(D2dFactory *f, D2D1_BUFFER_PRECISION precision, const UINT32 *extents, const BYTE *data, UINT32 size,
                       const UINT32 *strides, ID2D1LookupTable3D **out);

/* gaussian blur of a float image in place by d2d's discrete kernel, the edges reading transparent or clamped. */
void FxGaussian(FxImg *im, float sigma_x, float sigma_y, float range, BOOL hard);

/* sampling of a float image at a point of its space, pixel centers at half integers. */
enum { FX_NEAREST, FX_LINEAR, FX_CUBIC, FX_MULTISAMPLE, FX_ANISOTROPIC, FX_HQCUBIC };
void FxSample(const FxImg *im, float x, float y, int mode, BOOL clamp, float *out);
/* an image scaled by sx and sy about the origin, for the kernel unit lengths of convolution and lighting. */
HRESULT FxScale(const FxImg *src, float sx, float sy, int mode, FxImg *out);

#endif
