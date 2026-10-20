/*
 * gdiplus internals: the objects behind the flat api, the render targets and the drawing core the modules share.
 *
 * every object starts with a tag, and the api functions check it before they touch anything, which is how a
 * stray pointer or a deleted object ends as InvalidParameter instead of a crash. the objects the sdk keeps in
 * classes (GpPath, GpBrush and so on) are the G... structures here, the exports take them as void pointers.
 *
 * coordinates: world space is what the program draws in, page space is world space under the world transform,
 * and device space is page space scaled by the page unit, in pixels. the transforms are MimeVecMatrix, a point
 * is a row, x' = x * m11 + y * m21 + dx.
 */
#ifndef MIME_GDIPLUS_PRIVATE_H
#define MIME_GDIPLUS_PRIVATE_H

#define MIME_BUILD_GDIPLUS
#include <mime/win32/gdiplus.h>
#include <mime/win32/winuser.h>
#include <mime/stub.h>
#include <vector/vector.h>

/* ------------------------------------------------------------------ objects */

enum {
    GP_TAG_BITMAP = 0x50696d47,     /* 'Gmip' */
    GP_TAG_METAFILE = 0x7074654d,   /* 'Metp' */
    GP_TAG_GRAPHICS = 0x50726747,   /* 'Ggrp' */
    GP_TAG_MATRIX = 0x7274614d,     /* 'Matr' */
    GP_TAG_BRUSH = 0x68737242,      /* 'Brsh' */
    GP_TAG_PEN = 0x206e6550,        /* 'Pen ' */
    GP_TAG_PATH = 0x68746150,       /* 'Path' */
    GP_TAG_PATHITER = 0x72657449,   /* 'Iter' */
    GP_TAG_REGION = 0x6e676552,     /* 'Regn' */
    GP_TAG_ATTRS = 0x72747441,      /* 'Attr' */
    GP_TAG_CAP = 0x20706143,        /* 'Cap ' */
    GP_TAG_CACHED = 0x68636143,     /* 'Cach' */
    GP_TAG_FONT = 0x746e6f46,       /* 'Font' */
    GP_TAG_FAMILY = 0x796d6146,     /* 'Famy' */
    GP_TAG_COLLECTION = 0x6c6c6f43, /* 'Coll' */
    GP_TAG_STRFMT = 0x74724653,     /* 'SFrt' */
    GP_TAG_EFFECT = 0x74636645,     /* 'Efct' */
};

typedef struct GObject {
    DWORD tag;
} GObject;

/* larger bitmaps fail with OutOfMemory, which keeps every size and offset in 32 bits. */
#define GP_MAX_DIMENSION 32767
#define GP_MAX_BYTES 0x40000000u

/* the library is usable between GdiplusStartup and the matching GdiplusShutdown, the functions that make objects say so. */
BOOL gp_started(void);
#define GP_REQUIRE_INIT()                  \
    do {                                   \
        if (!gp_started())                 \
            return GdiplusNotInitialized;  \
    } while (0)

/* memory, from the process heap. gp_alloc_zero clears. */
void *gp_alloc(SIZE_T size);
void *gp_alloc_zero(SIZE_T size);
void *gp_realloc(void *p, SIZE_T size);
void gp_free(void *p);
/* the allocator the vector library asks for. */
extern const MimeAllocator gp_allocator;
/* a copy of count elements, NULL for a count of 0. */
void *gp_dup(const void *p, SIZE_T bytes);

#define GP_CAST(type, tag_, p) ((p) && ((GObject *)(p))->tag == (tag_) ? (type *)(p) : nullptr)

/* math: the natural logarithm, the exponential and the power, for positive bases. */
double gp_log(double x);
double gp_exp(double x);
double gp_pow(double x, double y);
/* the 128 steps of the rising half of the sigma bell blend shape, scaled to 0..65536 over 256 steps. */
const float *gp_sigma_table(void);

/* ------------------------------------------------------------------ matrices */

typedef struct GMatrix {
    DWORD tag;
    union {
        float M[6];
        MimeVecMatrix m;
    };
} GMatrix;

GMatrix *gp_matrix_cast(const void *p);
/* a matrix is invertible when its determinant is not tiny, as gdi+ has it. */
BOOL gp_matrix_invertible(const MimeVecMatrix *m);
/* a followed by b: the product that applies a to a point first. */
void gp_matrix_multiply(const float *a, const float *b, float *out);
void gp_matrix_point(const float *m, float x, float y, float *ox, float *oy);
/* the sdk order flag: MatrixOrderPrepend applies the new operation first. */
void gp_matrix_apply(MimeVecMatrix *m, const MimeVecMatrix *op, MatrixOrder order);
/* the inverse, false when the matrix cannot be inverted. */
BOOL gp_matrix_inverse(MimeVecMatrix *out, const MimeVecMatrix *m);

/* ------------------------------------------------------------------ paths */

typedef struct GPath {
    DWORD tag;
    FillMode fill_mode;
    MimeVecPath path;
    /* the end of the last figure that was closed or the start of the open one, for the sdk's "last point". */
} GPath;

GPath *gp_path_cast(const void *p);
GPath *gp_path_new(FillMode mode);
/* the point types of a path make figures: a start, then lines and groups of three curve points. */
BOOL gp_path_types_valid(const BYTE *types, INT count);
GpStatus gp_path_status(int vec_status);
void gp_path_free(GPath *p);
GPath *gp_path_clone(const GPath *p);
/* flattens the path through a matrix into polygons, flatness is in the output space. */
GpStatus gp_path_flatten(const MimeVecPath *path, const MimeVecMatrix *m, float flatness, MimeVecPolys *out);
/* the points that stand for a rectangle or ellipse, for the shape draw calls. */
GpStatus gp_path_add_shape_rect(MimeVecPath *p, float x, float y, float w, float h);

/* ------------------------------------------------------------------ colors */

/* straight argb to the premultiplied form the pipeline works in, with the rounding of gdiplus.dll. */
UINT32 gp_premultiply(UINT32 argb);
UINT32 gp_unpremultiply(UINT32 pargb);
/* scales the four bytes of a premultiplied pixel by v / 255 (rounded) or by count / total in the AA way. */
UINT32 gp_scale_pargb(UINT32 pargb, UINT32 v255);

/* ------------------------------------------------------------------ images and bitmaps */

typedef struct GImage {
    DWORD tag;
    ImageType type;
} GImage;

typedef struct GProperty {
    ULONG id;
    WORD type;
    ULONG length;
    BYTE *value;
} GProperty;

struct GFrameSource;

typedef struct GBitmap {
    GImage image;
    INT width;
    INT height;
    PixelFormat format;
    INT stride;       /* bytes, negative for bottom up memory */
    BYTE *bits;       /* the top row */
    BYTE *owned;      /* the allocation to free, NULL when the pixels belong to the caller */
    ColorPalette *palette;
    float dpi_x, dpi_y;
    GUID raw_format;
    UINT flags;       /* ImageFlags */
    GProperty *props;
    UINT prop_count;
    /* several frames: the decoder the current frame was read from, and the dimension that selects among them. */
    struct GFrameSource *source;
    UINT frame_index;
    /* LockBits */
    BOOL locked;
    GpRect lock_rect;
    UINT lock_flags;
    PixelFormat lock_format;
    BYTE *lock_scan0;
    INT lock_stride;
    BYTE *lock_buffer;
    ColorPalette *lock_palette;
    /* an encoder started by SaveImage with MultiFrame, for SaveAdd. */
    void *save_state;
    /* a graphics that draws on the bitmap holds it, a bitmap cannot be disposed while one does. */
    INT graphics_refs;
    BOOL disposed;
} GBitmap;

GBitmap *gp_bitmap_cast(const void *image);
GImage *gp_image_cast(const void *image);
BOOL gp_format_known(PixelFormat format);
INT gp_default_stride(INT width, PixelFormat format);
GpStatus gp_bitmap_create(INT width, INT height, PixelFormat format, GBitmap **bitmap);
/* wraps caller memory or makes room; stride is the row distance and may be negative. */
GpStatus gp_bitmap_create_with_bits(INT width, INT height, INT stride, PixelFormat format, BYTE *scan0, GBitmap **bitmap);
GpStatus gp_bitmap_copy(const GBitmap *source, GBitmap **copy);
void gp_bitmap_free(GBitmap *bitmap);
/* the rows of pixels of any format as straight 8 bit argb, and the other way around. count pixels from x. */
void gp_bitmap_read_argb(const GBitmap *b, INT x, INT y, INT count, UINT32 *out);
void gp_bitmap_write_argb(GBitmap *b, INT x, INT y, INT count, const UINT32 *argb);
/* converts a row between any two formats, through straight argb. */
void gp_convert_row(BYTE *dst, PixelFormat dst_format, const ColorPalette *dst_palette, const BYTE *src,
                    PixelFormat src_format, const ColorPalette *src_palette, INT x_count);

/* palettes: the defaults of the indexed formats, copies, the halftone palettes and the colors of a picture. */
ColorPalette *gp_palette_default(PixelFormat format);
ColorPalette *gp_palette_copy(const ColorPalette *src);
void gp_palette_halftone256(ARGB *entries);
UINT gp_palette_halftone(PaletteType type, ARGB *entries, BOOL transparent);
UINT gp_palette_optimal(GBitmap *b, UINT count, BOOL transparent, ARGB *entries);

/* the codecs. the bytes of an image are decoded into a bitmap, or a metafile for the vector formats. */
struct GFrameSource;
void gp_frame_source_free(struct GFrameSource *s);
GpStatus gp_frame_source_clone(const struct GFrameSource *src, GBitmap *dst);
GpStatus gp_source_reopen(const BYTE *data, SIZE_T size, GBitmap *dst, GBitmap **unused);
GpStatus gp_decode_bytes(BYTE *data, SIZE_T size, GImage **image);
GpStatus gp_read_file(const WCHAR *file, BYTE **data, SIZE_T *size);
GpStatus gp_read_stream(IStream *stream, BYTE **data, SIZE_T *size);
UINT gp_frame_count(const GBitmap *b);
/* the imaging component, one reference for the caller. */
struct IWICImagingFactory;
GpStatus gp_wic_factory(struct IWICImagingFactory **factory);
const GUID *gp_wic_format_of(PixelFormat f);
const GUID *gp_encoder_format(const CLSID *clsid);
void gp_save_state_free(void *state);
const GUID *gp_frame_dimension(const GBitmap *b);
GpStatus gp_select_frame(GBitmap *b, UINT index);
/* metafiles are the business of metafile.c. */
GpStatus gp_metafile_from_bytes(BYTE *data, SIZE_T size, GImage **image);
GpStatus gp_metafile_to_bitmap(GImage *image, GBitmap **bitmap);
GpStatus gp_metafile_clone(GpImage *image, GpImage **clone);
GpStatus gp_metafile_dispose(GpImage *image);
GpStatus gp_metafile_dimension(GpImage *image, float *width, float *height);
GpStatus gp_metafile_bounds(GpImage *image, GpRectF *bounds, GpUnit *unit);
GpStatus gp_metafile_raw_format(GpImage *image, GUID *format);
/* either resolution may be NULL. */
GpStatus gp_metafile_resolution(GpImage *image, float *dpi_x, float *dpi_y);
/* the width and height a metafile reports in pixels, either may be NULL, and its image flags. */
GpStatus gp_metafile_pixel_size(GpImage *image, UINT *width, UINT *height);
GpStatus gp_metafile_flags(GpImage *image, UINT *flags);
GpStatus gp_metafile_thumbnail(GpImage *image, UINT width, UINT height, GpImage **thumbnail);
/* the size a metafile has on a page of the graphics, in the units of its world. */
struct GGraphics;
GpStatus gp_metafile_page_size(struct GGraphics *g, GpImage *image, float width, float height, GpUnit unit,
                               float *out_w, float *out_h);

/* ------------------------------------------------------------------ brushes and pens */

typedef struct GBlend {
    INT count;
    float *positions;
    float *factors;
} GBlend;

typedef struct GPreset {
    INT count;
    float *positions;
    ARGB *colors;
} GPreset;

struct GAttrs;

typedef struct GBrush {
    DWORD tag;
    BrushType type;
    MimeVecMatrix xform;
    WrapMode wrap;
    /* solid */
    ARGB color;
    /* hatch */
    HatchStyle hatch_style;
    ARGB hatch_fore, hatch_back;
    /* texture: a private copy of the image area, drawn through the attributes and the wrap mode */
    GBitmap *image;
    struct GAttrs *attrs;
    /*
     * linear gradient. mode is a LinearGradientMode, or GP_LINE_POINTS or GP_LINE_ANGLE, and the rectangle is
     * the one the brush was made with, for the points the rectangle that holds them.
     */
    INT lg_mode;
    GpPointF p1, p2;
    GpRectF rect;
    float angle;
    BOOL scalable;
    ARGB color1, color2;
    BOOL gamma;
    GBlend blend;
    GPreset preset;
    /* path gradient */
    GPath *pg_path;
    GpPointF center;
    ARGB center_color;
    ARGB *surround;
    INT surround_count;
    float focus_x, focus_y;
} GBrush;

#define GP_LINE_POINTS 4
#define GP_LINE_ANGLE 5

GBrush *gp_brush_cast(const void *p);
GBrush *gp_brush_new(BrushType type);
GBrush *gp_brush_clone(const GBrush *b);
void gp_brush_free(GBrush *b);
GBrush *gp_brush_solid(ARGB color);

typedef struct GCap {
    DWORD tag;
    BOOL arrow;        /* adjustable arrow cap */
    GPath *fill;       /* a custom cap: the fill and the stroke paths, as the sdk has them */
    GPath *stroke;
    LineCap base_cap;
    LineJoin join;
    float inset;
    float width_scale;
    float base_inset;
    float arrow_width, arrow_height, arrow_inset;
    BOOL arrow_fill;
    LineCap start_cap, end_cap;
} GCap;

typedef struct GPen {
    DWORD tag;
    GBrush *brush;
    float width;
    GpUnit unit;
    LineCap start_cap, end_cap;
    DashCap dash_cap;
    LineJoin join;
    float miter_limit;
    PenAlignment align;
    DashStyle dash_style;
    float dash_offset;
    float *dash;
    INT dash_count;
    float *compound;
    INT compound_count;
    MimeVecMatrix xform;
    GCap *custom_start, *custom_end;
} GPen;

GPen *gp_pen_cast(const void *p);
GCap *gp_cap_cast(const void *p);
GCap *gp_cap_clone(const GCap *c);
void gp_cap_free(GCap *c);
/* the factor from a length in the unit to pixels at the resolution. */
float gp_unit_to_pixels(GpUnit unit, float dpi);
/*
 * the outline of a path drawn with a pen. the path is in world space and the polygons come out in the space the
 * matrix leads to. a pen whose width is not in world units takes its width in pixels at the resolution.
 */
GpStatus gp_pen_widen(const GPen *pen, const MimeVecPath *path, const MimeVecMatrix *matrix, float flatness,
                      MimeVecPolys *out);
GpStatus gp_pen_widen_ex(const GPen *pen, const MimeVecPath *path, const MimeVecMatrix *world_to_out, float flatness,
                         float dpi, MimeVecPolys *out);

/* custom line caps: the ends of the flattened line are shortened and the shapes of the caps made. */
BOOL gp_pen_has_custom_caps(const GPen *pen);
GpStatus gp_pen_custom_caps(const GPen *pen, MimeVecPolys *flat, float width, float min_width,
                            const MimeVecMatrix *to_out, float flatness, MimeVecPolys *fills, MimeVecPolys *strokes);
/* 1 for polygons that wind one way and -1 for the other, from the first one with an area. */
float gp_polys_orientation(const MimeVecPolys *p);
float gp_polys_orientation_of(const MimeVecPoint *pts, UINT n);

/* ------------------------------------------------------------------ regions */

typedef enum GRegionKind {
    GREGION_INFINITE,
    GREGION_EMPTY,
    GREGION_RECT,
    GREGION_PATH,
    GREGION_OP,
} GRegionKind;

typedef struct GRegionNode {
    GRegionKind kind;
    GpRectF rect;
    GPath *path;
    CombineMode op;
    struct GRegionNode *left, *right;
} GRegionNode;

typedef struct GRegion {
    DWORD tag;
    GRegionNode *root;
} GRegion;

GRegion *gp_region_cast(const void *p);
GRegion *gp_region_new_infinite(void);
GRegion *gp_region_clone(const GRegion *r);
void gp_region_free(GRegion *r);
/* the pixels of the region under a matrix, as a mask, clipped to the rectangle. */
GpStatus gp_region_to_mask(const GRegion *r, const MimeVecMatrix *m, const MimeVecRect *clip, MimeVecMask *mask);
/* a mask of polygons filled without antialiasing, inside the clip. */
GpStatus gp_mask_from_polys(MimeVecMask *m, const MimeVecPolys *polys, INT rule, const MimeVecRect *clip);

/* ------------------------------------------------------------------ graphics */

typedef struct GState {
    struct GState *next;
    INT kind;             /* 0 save, 1 begin container */
    UINT id;
    MimeVecMatrix world;
    GpUnit page_unit;
    float page_scale;
    SmoothingMode smoothing;
    CompositingMode compositing_mode;
    CompositingQuality compositing_quality;
    InterpolationMode interpolation;
    PixelOffsetMode pixel_offset;
    TextRenderingHint text_hint;
    UINT text_contrast;
    MimeVecMask clip;       /* the clip in device space */
    BOOL clip_set;
    POINT origin;
    /* a container also has the rectangles it was made with, for the sdk's scaling of its units. */
    MimeVecMatrix container;
} GState;

typedef struct GGraphics {
    DWORD tag;
    HDC hdc;
    HWND hwnd;
    BOOL owns_dc;
    BOOL got_dc;          /* GdipGetDC is outstanding */
    HANDLE token;
    GBitmap *target;      /* the bitmap of GdipGetImageGraphicsContext */
    struct GMetafile *meta; /* a recording metafile */
    INT width, height;    /* the target size in pixels */
    float dpi_x, dpi_y;
    InterpolationMode interpolation;
    SmoothingMode smoothing;
    CompositingMode compositing_mode;
    CompositingQuality compositing_quality;
    PixelOffsetMode pixel_offset;
    TextRenderingHint text_hint;
    UINT text_contrast;
    GpUnit page_unit;
    float page_scale;
    MimeVecMatrix world;
    MimeVecMatrix container; /* the transform a container puts below the world, identity otherwise */
    MimeVecMatrix w2d;    /* world to device, with the page scale and the pixel offset */
    BOOL w2d_valid;
    MimeVecMask clip;     /* the clip in device space, valid when clip_set */
    BOOL clip_set;
    POINT origin;         /* the render origin of hatch and texture fills */
    GState *states;
    UINT next_state_id;
    INT busy;
    /* the device context GdipGetDC makes for a graphics on a bitmap */
    HDC bitmap_dc;
    HBITMAP bitmap_dib;
    HGDIOBJ bitmap_old;
    BYTE *bitmap_bits;
    UINT32 *bitmap_copy;
} GGraphics;

GGraphics *gp_graphics_cast(const void *p);
/* world to device, rebuilt when the transforms changed. */
const MimeVecMatrix *gp_graphics_w2d(GGraphics *g);
/* the same without the half pixel of the pixel offset modes, for the containers. */
MimeVecMatrix gp_graphics_w2d_base(const GGraphics *g);
GpStatus gp_graphics_make_bitmap_dc(GGraphics *g, HDC *dc);
void gp_graphics_release_bitmap_dc(GGraphics *g, HDC dc, BOOL write_back);
/* the multiplier from page units to pixels along the x and y axes. */
void gp_graphics_page_scale(const GGraphics *g, float *sx, float *sy);
/* how many world units one unit of the given kind is: the size of a font in points in the world of the graphics. */
float gp_graphics_unit_to_world(const GGraphics *g, GpUnit unit);

/* ------------------------------------------------------------------ the drawing core */

/*
 * the source of a fill: a brush made ready for a rectangle of device pixels. fetch gives the premultiplied
 * argb of count pixels from x, y.
 */
typedef struct GSource {
    void (*fetch)(struct GSource *s, INT x, INT y, INT count, UINT32 *out);
    void (*destroy)(struct GSource *s);
    BOOL constant;      /* the same color everywhere */
    UINT32 color;       /* the premultiplied color when constant */
    BOOL opaque;        /* no pixel has an alpha below 255, so there is nothing to blend */
} GSource;

/* a source for a brush in a graphics, valid for pixels inside bounds. NULL with a status on failure. */
GpStatus gp_source_create(GGraphics *g, const GBrush *brush, const RECT *bounds, GSource **out);
/* the same for a graphics-less use: the matrix is the world to device transform. */
void gp_source_free(GSource *s);

/* the antialiasing a smoothing mode stands for: 0 none, 1 eight by four, 2 eight by eight. */
INT gp_smoothing_samples(SmoothingMode mode);

/*
 * fills polygons in device space with a source. fill_rule is MIME_VEC_FILL_ALTERNATE or MIME_VEC_FILL_WINDING.
 * aa is 0, 1 or 2 as above. the clip of the graphics and of the device context apply.
 */
GpStatus gp_fill_polys(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, const GBrush *brush);
/* the same with a ready source and the area it was made for. */
GpStatus gp_fill_polys_src(GGraphics *g, const MimeVecPolys *polys, INT fill_rule, INT aa, GSource *src,
                           const RECT *area);
/* fills the pixels of a mask without antialiasing. */
GpStatus gp_fill_mask(GGraphics *g, const MimeVecMask *mask, const GBrush *brush);
/* narrows an area to the clip, a clip that is no rectangle comes back as a mask. FALSE when nothing shows. */
BOOL gp_clip_narrow(GGraphics *g, RECT *area, const MimeVecMask **mask);
/* a path in world space: transformed and flattened, then filled. */
GpStatus gp_fill_path(GGraphics *g, const MimeVecPath *path, FillMode mode, const GBrush *brush);
/* a path in world space under an extra matrix applied first, as the glyph runs of text have. */
GpStatus gp_fill_path_xform(GGraphics *g, const MimeVecPath *path, const MimeVecMatrix *extra, FillMode mode,
                            const GBrush *brush);
/* strokes a path in world space with a pen: the widened outline is filled with the pen's brush. */
GpStatus gp_stroke_path(GGraphics *g, const MimeVecPath *path, const GPen *pen);
/* a rectangle in world space with the exact pixel rules of a transform that only scales and moves. */
GpStatus gp_fill_rect_world(GGraphics *g, const GBrush *brush, float x, float y, float w, float h);

/* the scan converter. coverage is 0..cmax per pixel, where cmax is 1 (aliased), 32 or 64. */
typedef void (*GSpanFn)(void *user, INT y, INT x0, INT count, const UINT8 *coverage, UINT cmax);
GpStatus gp_raster(const MimeVecPolys *polys, INT fill_rule, INT aa, const RECT *clip, GSpanFn fn, void *user);

/* the destination a graphics draws on, as pixels of one format covering a rectangle of device space. */
typedef struct GSurface {
    BYTE *bits;       /* the pixel at (x, y) of the rectangle */
    INT stride;
    INT x, y;         /* device coordinates of the first pixel */
    INT width, height;
    PixelFormat format;
    const ColorPalette *palette;
    /* for the surfaces made from a device context */
    HDC hdc;
    HBITMAP dib;
    HDC mem;
    HGDIOBJ old;
    BOOL write_back;
    BOOL direct;
} GSurface;

/* the device rectangle of the whole target of a graphics. */
void gp_graphics_bounds(const GGraphics *g, RECT *bounds);
GpStatus gp_surface_acquire(GGraphics *g, const RECT *area, GSurface *s);
void gp_surface_release(GGraphics *g, GSurface *s, BOOL modified);
/* rows of the surface as premultiplied pixels, and the composition of premultiplied pixels onto it. */
void gp_surface_read(const GSurface *s, INT x, INT y, INT count, UINT32 *out);
void gp_surface_write(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb);
void gp_compose_over(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb);
void gp_compose_copy(const GSurface *s, INT x, INT y, INT count, const UINT32 *pargb, const UINT8 *cov, UINT cmax);

/* any pixel format to straight argb and back, the pixels of count from x on a row. */
void gp_row_to_argb(const BYTE *row, PixelFormat f, const ColorPalette *pal, INT x, INT n, UINT32 *out);
void gp_row_from_argb(BYTE *row, PixelFormat f, const ColorPalette *pal, INT x, INT n, const UINT32 *in);


/* ------------------------------------------------------------------ images drawn */

GpStatus gp_draw_image(GGraphics *g, GImage *image, const GpPointF *dst_points, INT dst_count, float sx, float sy,
                       float sw, float sh, GpUnit src_unit, struct GAttrs *attrs);

/* draws a metafile into a graphics, the source rectangle onto three corners of the page. */
GpStatus gp_metafile_draw(GGraphics *g, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                          GpUnit src_unit, struct GAttrs *attrs);

/* ------------------------------------------------------------------ image attributes and text */

/*
 * the color adjustment of an image attributes object, applied to a row of straight argb pixels. the type
 * selects the settings: ColorAdjustTypeDefault, Bitmap, Brush, Pen or Text.
 */
struct GAttrs *gp_attrs_cast(const void *p);
struct GAttrs *gp_attrs_clone(const struct GAttrs *a);
void gp_attrs_free(struct GAttrs *a);
void gp_attrs_apply(const struct GAttrs *attrs, ColorAdjustType type, UINT32 *argb, INT count);
BOOL gp_attrs_active(const struct GAttrs *attrs, ColorAdjustType type);
WrapMode gp_attrs_wrap(const struct GAttrs *attrs, ARGB *clamp_color);

/* ------------------------------------------------------------------ recording graphics (emfrecord.c) */

/*
 * a graphics with a metafile in its meta field records: the drawing core and the graphics calls hand it what they
 * were asked to do and it writes emf+ records. these calls are made before the real work, which a recording
 * graphics does not do for what it draws.
 */
GGraphics *gp_graphics_new_recording(struct GMetafile *meta, float dpi_x, float dpi_y, INT width, INT height);
GpStatus gp_rec_fill_path(GGraphics *g, const MimeVecPath *path, FillMode mode, const GBrush *brush);
GpStatus gp_rec_stroke_path(GGraphics *g, const MimeVecPath *path, const GPen *pen);
GpStatus gp_rec_fill_rect(GGraphics *g, const GBrush *brush, float x, float y, float w, float h);
GpStatus gp_rec_fill_region(GGraphics *g, const GBrush *brush, const GRegion *region);
GpStatus gp_rec_clear(GGraphics *g, ARGB color);
GpStatus gp_rec_draw_image(GGraphics *g, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                           GpUnit unit, struct GAttrs *attrs);
void gp_rec_sync(GGraphics *g);
void gp_rec_save(GGraphics *g, UINT id);
/* kind 0 is a save and 1 a container. */
void gp_rec_restore(GGraphics *g, UINT id, INT kind);
void gp_rec_begin_container(GGraphics *g, UINT id, const GpRectF *dst, const GpRectF *src, GpUnit unit);
GpStatus gp_rec_set_clip(GGraphics *g, const GRegion *region, CombineMode mode);
void gp_rec_reset_clip(GGraphics *g);
void gp_rec_offset_clip(GGraphics *g, float dx, float dy);
void gp_rec_comment(GGraphics *g, UINT size, const BYTE *data);
GpStatus gp_rec_get_dc(GGraphics *g, HDC *dc);
void gp_rec_release_dc(GGraphics *g);
/* a recording graphics that is deleted ends the recording. */
void gp_meta_graphics_deleted(GGraphics *g);
/* the graphics of a recording metafile, for GdipGetImageGraphicsContext. */
GpStatus gp_meta_graphics_context(GpImage *image, GpGraphics **graphics);

#endif
