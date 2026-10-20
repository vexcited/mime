/*
 * what an image knows about itself besides its pixels: its size, flags, resolution and format, its frames and
 * properties, the thumbnail, turning and flipping, and the list of codecs.
 */
#include "gp_private.h"

static BOOL same_guid(const GUID *a, const GUID *b) { return !memcmp(a, b, sizeof(GUID)); }

/* ------------------------------------------------------------------ queries */

WINGDIPAPI GpStatus WINAPI GdipGetImageType(GpImage *image, ImageType *type)
{
    GImage *i = gp_image_cast(image);
    if (!i || !type)
        return InvalidParameter;
    *type = i->type;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageWidth(GpImage *image, UINT *width)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b) {
        float w, h;
        if (!width || !gp_image_cast(image))
            return InvalidParameter;
        GpStatus st = gp_metafile_dimension(image, &w, &h);
        if (st == Ok)
            *width = (UINT)w;
        return st;
    }
    if (!width)
        return InvalidParameter;
    *width = (UINT)b->width;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageHeight(GpImage *image, UINT *height)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b) {
        float w, h;
        if (!height || !gp_image_cast(image))
            return InvalidParameter;
        GpStatus st = gp_metafile_dimension(image, &w, &h);
        if (st == Ok)
            *height = (UINT)h;
        return st;
    }
    if (!height)
        return InvalidParameter;
    *height = (UINT)b->height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImagePixelFormat(GpImage *image, PixelFormat *format)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!format)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        *format = PixelFormat32bppARGB;
        return Ok;
    }
    *format = b->format;
    return Ok;
}

/* the size in the units of the image, which are pixels for a bitmap. */
WINGDIPAPI GpStatus WINAPI GdipGetImageDimension(GpImage *image, REAL *width, REAL *height)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!width || !height)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        return gp_metafile_dimension(image, width, height);
    }
    *width = (float)b->width;
    *height = (float)b->height;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageBounds(GpImage *image, GpRectF *bounds, GpUnit *unit)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!bounds || !unit)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        return gp_metafile_bounds(image, bounds, unit);
    }
    bounds->X = bounds->Y = 0;
    bounds->Width = (float)b->width;
    bounds->Height = (float)b->height;
    *unit = UnitPixel;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageFlags(GpImage *image, UINT *flags)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!flags)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        *flags = ImageFlagsScalable | ImageFlagsHasRealPixelSize | ImageFlagsHasRealDPI | ImageFlagsColorSpaceRGB |
                 ImageFlagsReadOnly;
        return Ok;
    }
    *flags = b->flags;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageRawFormat(GpImage *image, GUID *format)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!format)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        return gp_metafile_raw_format(image, format);
    }
    *format = b->raw_format;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageHorizontalResolution(GpImage *image, REAL *resolution)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!resolution)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        return gp_metafile_resolution(image, resolution, nullptr);
    }
    *resolution = b->dpi_x;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageVerticalResolution(GpImage *image, REAL *resolution)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!resolution)
        return InvalidParameter;
    if (!b) {
        if (!gp_image_cast(image))
            return InvalidParameter;
        return gp_metafile_resolution(image, nullptr, resolution);
    }
    *resolution = b->dpi_y;
    return Ok;
}

/* ------------------------------------------------------------------ frames */

static BOOL dimension_known(const GUID *d)
{
    return same_guid(d, &FrameDimensionPage) || same_guid(d, &FrameDimensionTime) ||
           same_guid(d, &FrameDimensionResolution);
}

WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsCount(GpImage *image, UINT *count)
{
    if (!gp_image_cast(image) || !count)
        return InvalidParameter;
    *count = 1;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipImageGetFrameDimensionsList(GpImage *image, GUID *dimensions, UINT count)
{
    GImage *i = gp_image_cast(image);
    if (!i || !dimensions || count != 1)
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(image);
    dimensions[0] = b ? *gp_frame_dimension(b) : FrameDimensionPage;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipImageGetFrameCount(GpImage *image, const GUID *dimension, UINT *count)
{
    GImage *i = gp_image_cast(image);
    if (!i || !count)
        return InvalidParameter;
    if (dimension && !dimension_known(dimension))
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(image);
    if (b && (!dimension || same_guid(dimension, gp_frame_dimension(b))))
        *count = gp_frame_count(b);
    else
        *count = 1;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipImageSelectActiveFrame(GpImage *image, const GUID *dimension, UINT index)
{
    GImage *i = gp_image_cast(image);
    if (!i || !dimension || !dimension_known(dimension))
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return index == 0 ? Ok : InvalidParameter;
    if (b->locked)
        return WrongState;
    if (!same_guid(dimension, gp_frame_dimension(b)))
        return index == 0 ? Ok : InvalidParameter;
    return gp_select_frame(b, index);
}

/* ------------------------------------------------------------------ properties */

static GProperty *find_property(GBitmap *b, ULONG id)
{
    for (UINT i = 0; i < b->prop_count; i++)
        if (b->props[i].id == id)
            return &b->props[i];
    return nullptr;
}

WINGDIPAPI GpStatus WINAPI GdipGetPropertyCount(GpImage *image, UINT *count)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !count)
        return InvalidParameter;
    *count = b->prop_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPropertyIdList(GpImage *image, UINT count, PROPID *ids)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !ids || count != b->prop_count)
        return InvalidParameter;
    for (UINT i = 0; i < count; i++)
        ids[i] = b->props[i].id;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPropertyItemSize(GpImage *image, PROPID id, UINT *size)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !size)
        return InvalidParameter;
    GProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    *size = (UINT)(sizeof(PropertyItem) + p->length);
    return Ok;
}

/* the item is written at the start of the buffer, its value follows it. */
WINGDIPAPI GpStatus WINAPI GdipGetPropertyItem(GpImage *image, PROPID id, UINT size, PropertyItem *item)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !item)
        return InvalidParameter;
    GProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    if (size != sizeof(PropertyItem) + p->length)
        return InvalidParameter;
    item->id = p->id;
    item->length = p->length;
    item->type = p->type;
    item->value = p->length ? (BYTE *)(item + 1) : nullptr;
    memcpy(item + 1, p->value, p->length);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetPropertySize(GpImage *image, UINT *total, UINT *count)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !total || !count)
        return InvalidParameter;
    UINT sum = 0;
    for (UINT i = 0; i < b->prop_count; i++)
        sum += (UINT)(sizeof(PropertyItem) + b->props[i].length);
    *total = sum;
    *count = b->prop_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetAllPropertyItems(GpImage *image, UINT total, UINT count, PropertyItem *items)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !items)
        return InvalidParameter;
    UINT sum = 0;
    for (UINT i = 0; i < b->prop_count; i++)
        sum += (UINT)(sizeof(PropertyItem) + b->props[i].length);
    if (total != sum || count != b->prop_count)
        return InvalidParameter;
    BYTE *values = (BYTE *)(items + count);
    for (UINT i = 0; i < count; i++) {
        GProperty *p = &b->props[i];
        items[i].id = p->id;
        items[i].length = p->length;
        items[i].type = p->type;
        items[i].value = p->length ? values : nullptr;
        memcpy(values, p->value, p->length);
        values += p->length;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetPropertyItem(GpImage *image, GDIPCONST PropertyItem *item)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !item || (item->length && !item->value))
        return InvalidParameter;
    BYTE *value = gp_alloc(item->length);
    if (!value)
        return OutOfMemory;
    memcpy(value, item->value, item->length);
    GProperty *p = find_property(b, item->id);
    if (p) {
        gp_free(p->value);
    } else {
        GProperty *bigger = gp_realloc(b->props, sizeof(GProperty) * (b->prop_count + 1));
        if (!bigger) {
            gp_free(value);
            return OutOfMemory;
        }
        b->props = bigger;
        p = &b->props[b->prop_count++];
    }
    p->id = item->id;
    p->type = item->type;
    p->length = item->length;
    p->value = value;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipRemovePropertyItem(GpImage *image, PROPID id)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return InvalidParameter;
    GProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    gp_free(p->value);
    *p = b->props[--b->prop_count];
    return Ok;
}

/* ------------------------------------------------------------------ turning, flipping, thumbnails */

static UINT get_index(const BYTE *row, INT x, INT bits)
{
    if (bits == 1)
        return (row[x >> 3] >> (7 - (x & 7))) & 1;
    return (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
}

static void set_index(BYTE *row, INT x, INT bits, UINT v)
{
    if (bits == 1) {
        BYTE bit = (BYTE)(0x80 >> (x & 7));
        row[x >> 3] = v ? (row[x >> 3] | bit) : (row[x >> 3] & (BYTE)~bit);
    } else if (x & 1) {
        row[x >> 1] = (row[x >> 1] & 0xf0) | (BYTE)v;
    } else {
        row[x >> 1] = (row[x >> 1] & 0x0f) | (BYTE)(v << 4);
    }
}

/* a quarter turn clockwise for each of rotate by 90, 180 and 270, then a mirror across the vertical axis. */
WINGDIPAPI GpStatus WINAPI GdipImageRotateFlip(GpImage *image, RotateFlipType type)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return InvalidParameter;
    if ((UINT)type > Rotate270FlipX)
        return InvalidParameter;
    if (b->locked)
        return WrongState;
    INT turns = type & 3;
    BOOL flip = (type & 4) != 0;
    if (!turns && !flip)
        return Ok;
    INT width = turns & 1 ? b->height : b->width, height = turns & 1 ? b->width : b->height;
    GBitmap *t;
    GpStatus st = gp_bitmap_create(width, height, b->format, &t);
    if (st != Ok)
        return st;
    INT bits = GetPixelFormatSize(b->format);
    INT depth = bits / 8;
    for (INT y = 0; y < b->height; y++) {
        for (INT x = 0; x < b->width; x++) {
            INT nx, ny;
            switch (turns) {
            case 1:
                nx = b->height - 1 - y;
                ny = x;
                break;
            case 2:
                nx = b->width - 1 - x;
                ny = b->height - 1 - y;
                break;
            case 3:
                nx = y;
                ny = b->width - 1 - x;
                break;
            default:
                nx = x;
                ny = y;
                break;
            }
            if (flip)
                nx = width - 1 - nx;
            if (bits >= 8)
                memcpy(t->bits + (SSIZE_T)ny * t->stride + (SSIZE_T)nx * depth,
                       b->bits + (SSIZE_T)y * b->stride + (SSIZE_T)x * depth, depth);
            else
                set_index(t->bits + (SSIZE_T)ny * t->stride, nx, bits,
                          get_index(b->bits + (SSIZE_T)y * b->stride, x, bits));
        }
    }
    /* the new pixels replace the old ones; the bitmap is the same object. */
    BYTE *old = b->owned;
    b->owned = t->owned;
    b->bits = t->bits;
    b->stride = t->stride;
    b->width = width;
    b->height = height;
    if (turns & 1) {
        float dpi = b->dpi_x;
        b->dpi_x = b->dpi_y;
        b->dpi_y = dpi;
    }
    gp_free(old);
    t->owned = nullptr;
    gp_bitmap_free(t);
    return Ok;
}

/*
 * the picture scaled to the size, 120 pixels on the long side when a size is zero, as a bitmap with alpha. The
 * image keeps its proportions in the width and height given only when one of them is zero, which is how windows
 * treats it as well.
 */
WINGDIPAPI GpStatus WINAPI GdipGetImageThumbnail(GpImage *image, UINT width, UINT height, GpImage **thumbnail,
                                                 GetThumbnailImageAbort callback, VOID *data)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !thumbnail)
        return InvalidParameter;
    if (!width && !height) {
        if (b->width >= b->height) {
            width = 120;
            height = (UINT)(((ULONGLONG)120 * b->height + b->width / 2) / b->width);
        } else {
            height = 120;
            width = (UINT)(((ULONGLONG)120 * b->width + b->height / 2) / b->height);
        }
    } else if (!width) {
        width = (UINT)(((ULONGLONG)height * b->width + b->height / 2) / b->height);
    } else if (!height) {
        height = (UINT)(((ULONGLONG)width * b->height + b->width / 2) / b->width);
    }
    if (!width)
        width = 1;
    if (!height)
        height = 1;
    GBitmap *t;
    GpStatus st = gp_bitmap_create((INT)width, (INT)height, PixelFormat32bppPARGB, &t);
    if (st != Ok)
        return st;
    GpGraphics *g;
    st = GdipGetImageGraphicsContext(&t->image, &g);
    if (st == Ok) {
        st = GdipDrawImageRectI(g, image, 0, 0, (INT)width, (INT)height);
        GdipDeleteGraphics(g);
    }
    if (st != Ok) {
        gp_bitmap_free(t);
        return st;
    }
    *thumbnail = &t->image;
    return Ok;
}

/* ------------------------------------------------------------------ codecs */

typedef struct Codec {
    CLSID clsid;
    GUID format;
    const WCHAR *name;
    const WCHAR *description;
    const WCHAR *extensions;
    const WCHAR *mime;
    DWORD flags;
    UINT sig_count, sig_size;
    const BYTE *pattern, *mask;
} Codec;

static const BYTE bmp_p[] = {'B', 'M'}, bmp_m[] = {0xff, 0xff};
static const BYTE jpg_p[] = {0xff, 0xd8}, jpg_m[] = {0xff, 0xff};
static const BYTE gif_p[] = {'G', 'I', 'F', '8', '9', 'a', 'G', 'I', 'F', '8', '7', 'a'};
static const BYTE gif_m[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const BYTE emf_p[44] = {[40] = 0x20, 'E', 'M', 'F'};
static const BYTE emf_m[44] = {[40] = 0xff, 0xff, 0xff, 0xff};
static const BYTE wmf_p[] = {0xd7, 0xcd, 0xc6, 0x9a}, wmf_m[] = {0xff, 0xff, 0xff, 0xff};
static const BYTE tif_p[] = {'I', 'I', 'M', 'M'}, tif_m[] = {0xff, 0xff, 0xff, 0xff};
static const BYTE png_p[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
static const BYTE png_m[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const BYTE ico_p[] = {0, 0, 1, 0}, ico_m[] = {0xff, 0xff, 0xff, 0xff};

#define FLAGS_BOTH (ImageCodecFlagsEncoder | ImageCodecFlagsDecoder | ImageCodecFlagsSupportBitmap | ImageCodecFlagsBuiltin)
#define FLAGS_DEC (ImageCodecFlagsDecoder | ImageCodecFlagsSupportBitmap | ImageCodecFlagsBuiltin)

static const Codec g_codecs[] = {
    {{0x557cf400, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cab, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in BMP Codec", L"BMP",
     L"*.BMP;*.DIB;*.RLE", L"image/bmp", FLAGS_BOTH, 1, 2, bmp_p, bmp_m},
    {{0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cae, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in JPEG Codec", L"JPEG",
     L"*.JPG;*.JPEG;*.JPE;*.JFIF", L"image/jpeg", FLAGS_BOTH, 1, 2, jpg_p, jpg_m},
    {{0x557cf402, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cb0, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in GIF Codec", L"GIF",
     L"*.GIF", L"image/gif", FLAGS_BOTH, 2, 6, gif_p, gif_m},
    {{0x557cf403, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cac, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in EMF Codec", L"EMF",
     L"*.EMF", L"image/x-emf", (FLAGS_DEC & ~ImageCodecFlagsSupportBitmap) | ImageCodecFlagsSupportVector, 1, 44, emf_p, emf_m},
    {{0x557cf404, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cad, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in WMF Codec", L"WMF",
     L"*.WMF", L"image/x-wmf", (FLAGS_DEC & ~ImageCodecFlagsSupportBitmap) | ImageCodecFlagsSupportVector, 1, 4, wmf_p, wmf_m},
    {{0x557cf405, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cb1, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in TIFF Codec", L"TIFF",
     L"*.TIF;*.TIFF", L"image/tiff", FLAGS_BOTH, 2, 2, tif_p, tif_m},
    {{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3caf, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in PNG Codec", L"PNG",
     L"*.PNG", L"image/png", FLAGS_BOTH, 1, 8, png_p, png_m},
    {{0x557cf407, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
     {0xb96b3cb5, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}, L"Built-in ICO Codec", L"ICO",
     L"*.ICO", L"image/x-icon", FLAGS_DEC, 1, 4, ico_p, ico_m},
};

static SIZE_T string_bytes(const WCHAR *s) { return (wcslen(s) + 1) * sizeof(WCHAR); }

static SIZE_T codec_extra(const Codec *c)
{
    return string_bytes(c->name) + string_bytes(c->description) + string_bytes(c->extensions) + string_bytes(c->mime) +
           2 * (SIZE_T)c->sig_count * c->sig_size;
}

static BOOL codec_wanted(const Codec *c, DWORD flag) { return (c->flags & flag) != 0; }

static GpStatus codec_sizes(DWORD flag, UINT *count, UINT *size)
{
    SIZE_T total = 0;
    UINT n = 0;
    for (UINT i = 0; i < sizeof(g_codecs) / sizeof(g_codecs[0]); i++) {
        if (!codec_wanted(&g_codecs[i], flag))
            continue;
        total += sizeof(ImageCodecInfo) + codec_extra(&g_codecs[i]);
        n++;
    }
    *count = n;
    *size = (UINT)total;
    return Ok;
}

/* the list is the structures, then the strings and signatures they point at. */
static GpStatus codec_list(DWORD flag, UINT count, UINT size, ImageCodecInfo *list)
{
    UINT want_count, want_size;
    codec_sizes(flag, &want_count, &want_size);
    if (!list || count != want_count || size != want_size)
        return GenericError;
    BYTE *extra = (BYTE *)(list + count);
    UINT k = 0;
    for (UINT i = 0; i < sizeof(g_codecs) / sizeof(g_codecs[0]); i++) {
        const Codec *c = &g_codecs[i];
        if (!codec_wanted(c, flag))
            continue;
        ImageCodecInfo *info = &list[k++];
        memset(info, 0, sizeof(*info));
        info->Clsid = c->clsid;
        info->FormatID = c->format;
        const WCHAR *strings[4] = {c->name, c->description, c->extensions, c->mime};
        WCHAR **slots[4] = {&info->CodecName, &info->FormatDescription, &info->FilenameExtension, &info->MimeType};
        for (int s = 0; s < 4; s++) {
            SIZE_T bytes = string_bytes(strings[s]);
            memcpy(extra, strings[s], bytes);
            *slots[s] = (WCHAR *)extra;
            extra += bytes;
        }
        info->Flags = c->flags;
        info->Version = 1;
        info->SigCount = c->sig_count;
        info->SigSize = c->sig_size;
        SIZE_T sig_bytes = (SIZE_T)c->sig_count * c->sig_size;
        memcpy(extra, c->pattern, sig_bytes);
        info->SigPattern = extra;
        extra += sig_bytes;
        memcpy(extra, c->mask, sig_bytes);
        info->SigMask = extra;
        extra += sig_bytes;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageEncodersSize(UINT *count, UINT *size)
{
    if (!count || !size)
        return InvalidParameter;
    return codec_sizes(ImageCodecFlagsEncoder, count, size);
}

WINGDIPAPI GpStatus WINAPI GdipGetImageEncoders(UINT count, UINT size, ImageCodecInfo *encoders)
{
    return codec_list(ImageCodecFlagsEncoder, count, size, encoders);
}

WINGDIPAPI GpStatus WINAPI GdipGetImageDecodersSize(UINT *count, UINT *size)
{
    if (!count || !size)
        return InvalidParameter;
    return codec_sizes(ImageCodecFlagsDecoder, count, size);
}

WINGDIPAPI GpStatus WINAPI GdipGetImageDecoders(UINT count, UINT size, ImageCodecInfo *decoders)
{
    return codec_list(ImageCodecFlagsDecoder, count, size, decoders);
}

/* the encoder of a class id. */
const GUID *gp_encoder_format(const CLSID *clsid)
{
    for (UINT i = 0; i < sizeof(g_codecs) / sizeof(g_codecs[0]); i++) {
        if ((g_codecs[i].flags & ImageCodecFlagsEncoder) && same_guid(clsid, &g_codecs[i].clsid))
            return &g_codecs[i].format;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the parameters of the encoders */

/* the lists of the encoders, in the sizes gdiplus.dll has for them: the structures, then the values. */
static UINT parameter_list_size(const GUID *format)
{
    UINT header = (UINT)offsetof(EncoderParameters, Parameter);
    UINT p = (UINT)sizeof(EncoderParameter);
    UINT values = 0, count = 0;
    if (same_guid(format, &ImageFormatJPEG)) {
        count = 5;
        values = 5 * 4 + 2 * 4;
    } else if (same_guid(format, &ImageFormatTIFF)) {
        count = 4;
        values = 5 * 4 + 5 * 4 + 4 + 4;
    } else if (same_guid(format, &ImageFormatGIF)) {
        count = 2;
        values = 4;
    } else if (same_guid(format, &ImageFormatPNG)) {
        count = 1;
        values = 0;
    } else {
        return 0;
    }
    UINT total = header + count * p + values;
    return (total + 7) & ~7u;
}

static void fill_parameter(EncoderParameter *p, const GUID *guid, ULONG count, ULONG type, void *value)
{
    p->Guid = *guid;
    p->NumberOfValues = count;
    p->Type = type;
    p->Value = value;
}

WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterListSize(GpImage *image, const CLSID *encoder, UINT *size)
{
    if (!gp_image_cast(image) || !encoder || !size)
        return InvalidParameter;
    const GUID *format = gp_encoder_format(encoder);
    if (!format)
        return InvalidParameter;
    UINT n = parameter_list_size(format);
    if (!n) {
        *size = 0;
        return NotImplemented;
    }
    *size = n;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetEncoderParameterList(GpImage *image, const CLSID *encoder, UINT size,
                                                       EncoderParameters *parameters)
{
    if (!gp_image_cast(image) || !encoder || !parameters)
        return InvalidParameter;
    const GUID *format = gp_encoder_format(encoder);
    if (!format)
        return InvalidParameter;
    UINT want = parameter_list_size(format);
    if (!want)
        return NotImplemented;
    if (size != want)
        return InvalidParameter;
    memset(parameters, 0, size);
    BYTE *values = (BYTE *)parameters + offsetof(EncoderParameters, Parameter);
    EncoderParameter *p = parameters->Parameter;
    LONG *v;
    if (same_guid(format, &ImageFormatJPEG)) {
        parameters->Count = 5;
        values += 5 * sizeof(EncoderParameter);
        v = (LONG *)values;
        for (INT i = 0; i < 5; i++)
            v[i] = EncoderValueTransformRotate90 + i;
        fill_parameter(&p[0], &EncoderTransformation, 5, EncoderParameterValueTypeLong, v);
        v += 5;
        v[0] = 0;
        v[1] = 100;
        fill_parameter(&p[1], &EncoderQuality, 1, EncoderParameterValueTypeLongRange, v);
        fill_parameter(&p[2], &EncoderLuminanceTable, 0, EncoderParameterValueTypeShort, nullptr);
        fill_parameter(&p[3], &EncoderChrominanceTable, 0, EncoderParameterValueTypeShort, nullptr);
        fill_parameter(&p[4], &EncoderImageItems, 0, EncoderParameterValueTypePointer, nullptr);
    } else if (same_guid(format, &ImageFormatTIFF)) {
        parameters->Count = 4;
        values += 4 * sizeof(EncoderParameter);
        v = (LONG *)values;
        static const LONG compressions[5] = {EncoderValueCompressionLZW, EncoderValueCompressionCCITT3,
                                             EncoderValueCompressionRle, EncoderValueCompressionCCITT4,
                                             EncoderValueCompressionNone};
        for (INT i = 0; i < 5; i++)
            v[i] = compressions[i];
        fill_parameter(&p[0], &EncoderCompression, 5, EncoderParameterValueTypeLong, v);
        v += 5;
        static const LONG depths[5] = {1, 4, 8, 24, 32};
        for (INT i = 0; i < 5; i++)
            v[i] = depths[i];
        fill_parameter(&p[1], &EncoderColorDepth, 5, EncoderParameterValueTypeLong, v);
        v += 5;
        v[0] = EncoderValueMultiFrame;
        fill_parameter(&p[2], &EncoderSaveFlag, 1, EncoderParameterValueTypeLong, v);
        v += 1;
        v[0] = 1;
        fill_parameter(&p[3], &EncoderSaveAsCMYK, 1, EncoderParameterValueTypeLong, v);
    } else if (same_guid(format, &ImageFormatGIF)) {
        parameters->Count = 2;
        values += 2 * sizeof(EncoderParameter);
        v = (LONG *)values;
        v[0] = EncoderValueMultiFrame;
        fill_parameter(&p[0], &EncoderImageItems, 0, EncoderParameterValueTypePointer, nullptr);
        fill_parameter(&p[1], &EncoderSaveFlag, 1, EncoderParameterValueTypeLong, v);
    } else {
        parameters->Count = 1;
        fill_parameter(&p[0], &EncoderImageItems, 0, EncoderParameterValueTypePointer, nullptr);
    }
    return Ok;
}
