/*
 * the information gdi+ keeps about an image beyond its pixels: resolution, flags, the format it came from, frames
 * and properties, the thumbnail, rotation and flipping, the list of codecs and saving. A bitmap here has one frame
 * and no properties unless a program sets them. Only windows bitmaps and png images are written and read.
 */
#include "gdiplus_private.h"

const GUID gp_format_memory_bmp = {0xb96b3caa, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
const GUID gp_format_bmp = {0xb96b3cab, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
const GUID gp_format_png = {0xb96b3caf, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};

static const GUID g_dimension_page = {0x7462dc86, 0x6180, 0x4c7e, {0x8e, 0x3f, 0xee, 0x73, 0x33, 0xa7, 0xa4, 0x83}};
static const GUID g_dimension_time = {0x6aedbd6d, 0x3fb5, 0x418a, {0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72}};
static const GUID g_dimension_resolution = {0x84236f7b, 0x3bd3, 0x428f, {0x8d, 0xab, 0x4e, 0xa1, 0x43, 0x9c, 0xa3, 0x15}};

static const CLSID g_clsid_bmp = {0x557cf400, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
static const CLSID g_clsid_png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
/* the encoders windows has that are not written here: jpeg, gif, emf, wmf, tiff and icon. */
static const CLSID g_clsid_other[] = {
    {0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
    {0x557cf402, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
    {0x557cf403, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
    {0x557cf404, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
    {0x557cf405, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
    {0x557cf407, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}},
};

static BOOL same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

/* ------------------------------------------------------------------ resource and device bitmaps */

/*
 * the bitmap resource of a module, loaded as a dib section and taken over by pixels. A name that is no bitmap
 * resource is an invalid parameter, as it is for windows.
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipCreateBitmapFromResource(HINSTANCE instance, const WCHAR *name, GpBitmap **bitmap)
{
    if (!bitmap)
        return InvalidParameter;
    HANDLE image = LoadImageW(instance, name, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!image)
        return InvalidParameter;
    GpStatus status = GdipCreateBitmapFromHBITMAP((HBITMAP)image, nullptr, bitmap);
    DeleteObject(image);
    return status;
}

/*
 * a 32 bit dib section, bottom up, filled with the background color and drawn over with the bitmap. The alpha of
 * the background is ignored, and so is the alpha channel of the result: it is opaque (a guess for the byte).
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap *bitmap, HBITMAP *hbitmap, ARGB background)
{
    GpBitmap *b = gp_bitmap_cast((GpImage *)bitmap);
    if (!b || !hbitmap)
        return InvalidParameter;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = b->Width;
    info.bmiHeader.biHeight = b->Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!dib || !bits) {
        if (dib)
            DeleteObject(dib);
        return GenericError;
    }
    UINT bg_r = background >> 16 & 0xff, bg_g = background >> 8 & 0xff, bg_b = background & 0xff;
    for (INT y = 0; y < b->Height; y++) {
        const BYTE *source = b->Bits + (INT_PTR)y * b->Stride;
        BYTE *row = bits + (SIZE_T)(b->Height - 1 - y) * b->Width * 4;
        for (INT x = 0; x < b->Width; x++) {
            ARGB c = gp_read_pixel(source, x, b->Format);
            UINT a = c >> 24;
            UINT r = ((c >> 16 & 0xff) * a + bg_r * (255 - a) + 127) / 255;
            UINT g = ((c >> 8 & 0xff) * a + bg_g * (255 - a) + 127) / 255;
            UINT bl = ((c & 0xff) * a + bg_b * (255 - a) + 127) / 255;
            row[x * 4] = (BYTE)bl;
            row[x * 4 + 1] = (BYTE)g;
            row[x * 4 + 2] = (BYTE)r;
            row[x * 4 + 3] = 0xff;
        }
    }
    *hbitmap = dib;
    return Ok;
}

/* ------------------------------------------------------------------ loading images */

/* every image here is a bitmap, so loading an image is loading a bitmap. */
DECLSPEC_EXPORT GpStatus WINAPI GdipLoadImageFromFile(const WCHAR *file, GpImage **image)
{
    if (!file || !image)
        return InvalidParameter;
    GpBitmap *b;
    GpStatus status = GdipCreateBitmapFromFile(file, &b);
    if (status == Ok)
        *image = &b->Image;
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipLoadImageFromFileICM(const WCHAR *file, GpImage **image)
{
    return GdipLoadImageFromFile(file, image);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipLoadImageFromStream(IStream *stream, GpImage **image)
{
    if (!stream || !image)
        return InvalidParameter;
    GpBitmap *b;
    GpStatus status = GdipCreateBitmapFromStream(stream, &b);
    if (status == Ok)
        *image = &b->Image;
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipLoadImageFromStreamICM(IStream *stream, GpImage **image)
{
    return GdipLoadImageFromStream(stream, image);
}

/* ------------------------------------------------------------------ queries */

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageType(GpImage *image, ImageType *type)
{
    if (!gp_bitmap_cast(image) || !type)
        return InvalidParameter;
    *type = ImageTypeBitmap;
    return Ok;
}

/* the size in the units of the image, which are pixels for a bitmap. */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageDimension(GpImage *image, float *width, float *height)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !width || !height)
        return InvalidParameter;
    *width = (float)b->Width;
    *height = (float)b->Height;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageBounds(GpImage *image, GpRectF *bounds, GpUnit *unit)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !bounds || !unit)
        return InvalidParameter;
    bounds->X = bounds->Y = 0;
    bounds->Width = (float)b->Width;
    bounds->Height = (float)b->Height;
    *unit = UnitPixel;
    return Ok;
}

/*
 * the colors are rgb; an image with an alpha channel says so, and one that was loaded from a file knows its
 * resolution and size and is read only. The translucent flag is not set, a guess.
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageFlags(GpImage *image, UINT *flags)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !flags)
        return InvalidParameter;
    UINT f = ImageFlagsColorSpaceRGB;
    if (IsAlphaPixelFormat(b->Format))
        f |= ImageFlagsHasAlpha;
    if (b->Decoded)
        f |= ImageFlagsHasRealDPI | ImageFlagsHasRealPixelSize | ImageFlagsReadOnly;
    *flags = f;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageRawFormat(GpImage *image, GUID *format)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !format)
        return InvalidParameter;
    *format = b->RawFormat;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageHorizontalResolution(GpImage *image, float *resolution)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !resolution)
        return InvalidParameter;
    *resolution = b->DpiX;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageVerticalResolution(GpImage *image, float *resolution)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !resolution)
        return InvalidParameter;
    *resolution = b->DpiY;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipBitmapSetResolution(GpBitmap *bitmap, float xdpi, float ydpi)
{
    GpBitmap *b = gp_bitmap_cast((GpImage *)bitmap);
    if (!b || !(xdpi > 0) || !(ydpi > 0))
        return InvalidParameter;
    b->DpiX = xdpi;
    b->DpiY = ydpi;
    return Ok;
}

/* ------------------------------------------------------------------ frames */

/* a bitmap has the page dimension, with one frame; gif's time and tiff's resolution are accepted for one frame. */
DECLSPEC_EXPORT GpStatus WINAPI GdipImageGetFrameDimensionsCount(GpImage *image, UINT *count)
{
    if (!gp_bitmap_cast(image) || !count)
        return InvalidParameter;
    *count = 1;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipImageGetFrameDimensionsList(GpImage *image, GUID *dimensions, UINT count)
{
    if (!gp_bitmap_cast(image) || !dimensions || count != 1)
        return InvalidParameter;
    dimensions[0] = g_dimension_page;
    return Ok;
}

static BOOL dimension_known(const GUID *dimension)
{
    return same_guid(dimension, &g_dimension_page) || same_guid(dimension, &g_dimension_time) ||
           same_guid(dimension, &g_dimension_resolution);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipImageGetFrameCount(GpImage *image, const GUID *dimension, UINT *count)
{
    if (!gp_bitmap_cast(image) || !count)
        return InvalidParameter;
    if (dimension && !dimension_known(dimension))
        return InvalidParameter;
    *count = 1;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipImageSelectActiveFrame(GpImage *image, const GUID *dimension, UINT index)
{
    if (!gp_bitmap_cast(image) || !dimension || !dimension_known(dimension) || index != 0)
        return InvalidParameter;
    return Ok;
}

/* ------------------------------------------------------------------ properties */

static GpProperty *find_property(GpBitmap *b, ULONG id)
{
    for (UINT i = 0; i < b->PropertyCount; i++)
        if (b->Properties[i].Id == id)
            return &b->Properties[i];
    return nullptr;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPropertyCount(GpImage *image, UINT *count)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !count)
        return InvalidParameter;
    *count = b->PropertyCount;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPropertyIdList(GpImage *image, UINT count, ULONG *ids)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !ids || count != b->PropertyCount)
        return InvalidParameter;
    for (UINT i = 0; i < count; i++)
        ids[i] = b->Properties[i].Id;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPropertyItemSize(GpImage *image, ULONG id, UINT *size)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !size)
        return InvalidParameter;
    GpProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    *size = (UINT)(sizeof(PropertyItem) + p->Length);
    return Ok;
}

/* the item is written at the start of the buffer, its value follows it. */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetPropertyItem(GpImage *image, ULONG id, UINT size, PropertyItem *item)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !item)
        return InvalidParameter;
    GpProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    if (size != sizeof(PropertyItem) + p->Length)
        return InvalidParameter;
    item->id = p->Id;
    item->length = p->Length;
    item->type = p->Type;
    item->value = p->Length ? (BYTE *)(item + 1) : nullptr;
    memcpy(item + 1, p->Value, p->Length);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetPropertySize(GpImage *image, UINT *total, UINT *count)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !total || !count)
        return InvalidParameter;
    UINT sum = 0;
    for (UINT i = 0; i < b->PropertyCount; i++)
        sum += (UINT)(sizeof(PropertyItem) + b->Properties[i].Length);
    *total = sum;
    *count = b->PropertyCount;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetAllPropertyItems(GpImage *image, UINT total, UINT count, PropertyItem *items)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !items)
        return InvalidParameter;
    UINT sum = 0;
    for (UINT i = 0; i < b->PropertyCount; i++)
        sum += (UINT)(sizeof(PropertyItem) + b->Properties[i].Length);
    if (total != sum || count != b->PropertyCount)
        return InvalidParameter;
    BYTE *values = (BYTE *)(items + count);
    for (UINT i = 0; i < count; i++) {
        GpProperty *p = &b->Properties[i];
        items[i].id = p->Id;
        items[i].length = p->Length;
        items[i].type = p->Type;
        items[i].value = p->Length ? values : nullptr;
        memcpy(values, p->Value, p->Length);
        values += p->Length;
    }
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSetPropertyItem(GpImage *image, const PropertyItem *item)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !item || (item->length && !item->value))
        return InvalidParameter;
    BYTE *value = gp_alloc(item->length);
    if (!value)
        return OutOfMemory;
    memcpy(value, item->value, item->length);
    GpProperty *p = find_property(b, item->id);
    if (p) {
        gp_free(p->Value);
    } else {
        GpProperty *bigger = b->Properties ? HeapReAlloc(GetProcessHeap(), 0, b->Properties,
                                                         sizeof(GpProperty) * (b->PropertyCount + 1))
                                           : gp_alloc(sizeof(GpProperty));
        if (!bigger) {
            gp_free(value);
            return OutOfMemory;
        }
        b->Properties = bigger;
        p = &b->Properties[b->PropertyCount++];
    }
    p->Id = item->id;
    p->Type = item->type;
    p->Length = item->length;
    p->Value = value;
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipRemovePropertyItem(GpImage *image, ULONG id)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return InvalidParameter;
    GpProperty *p = find_property(b, id);
    if (!p)
        return PropertyNotFound;
    gp_free(p->Value);
    *p = b->Properties[--b->PropertyCount];
    return Ok;
}

/* ------------------------------------------------------------------ rotating, flipping, thumbnails */

/* a quarter turn clockwise for each of rotate by 90, 180 and 270, then a mirror across the vertical axis. */
DECLSPEC_EXPORT GpStatus WINAPI GdipImageRotateFlip(GpImage *image, RotateFlipType type)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b)
        return InvalidParameter;
    if ((UINT)type > Rotate270FlipX)
        return InvalidParameter;
    if (b->Locked)
        return WrongState;
    INT turns = type & 3;
    BOOL flip = (type & 4) != 0;
    if (!turns && !flip)
        return Ok;
    INT width = turns & 1 ? b->Height : b->Width, height = turns & 1 ? b->Width : b->Height;
    GpBitmap *t;
    GpStatus status = gp_bitmap_create(width, height, b->Format, &t);
    if (status != Ok)
        return status;
    INT depth = GetPixelFormatSize(b->Format) / 8;
    for (INT y = 0; y < b->Height; y++) {
        for (INT x = 0; x < b->Width; x++) {
            INT nx, ny;
            switch (turns) {
            case 1:
                nx = b->Height - 1 - y;
                ny = x;
                break;
            case 2:
                nx = b->Width - 1 - x;
                ny = b->Height - 1 - y;
                break;
            case 3:
                nx = y;
                ny = b->Width - 1 - x;
                break;
            default:
                nx = x;
                ny = y;
                break;
            }
            if (flip)
                nx = width - 1 - nx;
            memcpy(t->Bits + (INT_PTR)ny * t->Stride + (INT_PTR)nx * depth,
                   b->Bits + (INT_PTR)y * b->Stride + (INT_PTR)x * depth, depth);
        }
    }
    /* the new pixels replace the old ones; the bitmap is the same object. */
    BYTE *old = b->Owned;
    b->Owned = t->Owned;
    b->Bits = t->Bits;
    b->Stride = t->Stride;
    b->Width = width;
    b->Height = height;
    if (turns & 1) {
        float dpi = b->DpiX;
        b->DpiX = b->DpiY;
        b->DpiY = dpi;
    }
    gp_free(old);
    t->Owned = nullptr;
    t->Image.Magic = 0;
    gp_free(t);
    return Ok;
}

/*
 * the picture scaled to the size, 120 pixels on the long side when a size is zero, as a bitmap with alpha. The
 * image keeps its proportions in the width and height given only when one of them is zero, which is how windows
 * treats it as well.
 */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageThumbnail(GpImage *image, UINT width, UINT height, GpImage **thumbnail,
                                                      GetThumbnailImageAbort callback, void *data)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !thumbnail)
        return InvalidParameter;
    if (!width && !height) {
        if (b->Width >= b->Height) {
            width = 120;
            height = (UINT)(((ULONGLONG)120 * b->Height + b->Width / 2) / b->Width);
        } else {
            height = 120;
            width = (UINT)(((ULONGLONG)120 * b->Width + b->Height / 2) / b->Height);
        }
    } else if (!width) {
        width = (UINT)(((ULONGLONG)height * b->Width + b->Height / 2) / b->Height);
    } else if (!height) {
        height = (UINT)(((ULONGLONG)width * b->Height + b->Width / 2) / b->Width);
    }
    if (!width)
        width = 1;
    if (!height)
        height = 1;
    GpBitmap *t;
    GpStatus status = gp_bitmap_create((INT)width, (INT)height, PixelFormat32bppPARGB, &t);
    if (status != Ok)
        return status;
    GpGraphics *g;
    status = GdipGetImageGraphicsContext(&t->Image, &g);
    if (status == Ok) {
        status = GdipDrawImageRectI(g, image, 0, 0, (INT)width, (INT)height);
        GdipDeleteGraphics(g);
    }
    if (status != Ok) {
        GdipDisposeImage(&t->Image);
        return status;
    }
    *thumbnail = &t->Image;
    return Ok;
}

/* ------------------------------------------------------------------ codecs */

typedef struct CODEC {
    const CLSID *Clsid;
    const GUID *Format;
    const WCHAR *Name;
    const WCHAR *Description;
    const WCHAR *Extensions;
    const WCHAR *Mime;
    const BYTE *Signature;
    UINT SignatureSize;
} CODEC;

static const BYTE g_signature_bmp[2] = {'B', 'M'};
static const BYTE g_signature_png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
static const BYTE g_mask_full[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static const CODEC g_codecs[] = {
    {&g_clsid_bmp, &gp_format_bmp, L"Built-in BMP Codec", L"BMP", L"*.BMP;*.DIB;*.RLE", L"image/bmp", g_signature_bmp,
     2},
    {&g_clsid_png, &gp_format_png, L"Built-in PNG Codec", L"PNG", L"*.PNG", L"image/png", g_signature_png, 8},
};

static SIZE_T string_bytes(const WCHAR *s)
{
    return (wcslen(s) + 1) * sizeof(WCHAR);
}

static SIZE_T codec_extra(const CODEC *c)
{
    return string_bytes(c->Name) + string_bytes(c->Description) + string_bytes(c->Extensions) + string_bytes(c->Mime) +
           2 * c->SignatureSize;
}

static GpStatus codec_sizes(UINT *count, UINT *size)
{
    SIZE_T total = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_codecs); i++)
        total += sizeof(ImageCodecInfo) + codec_extra(&g_codecs[i]);
    *count = RTL_NUMBER_OF(g_codecs);
    *size = (UINT)total;
    return Ok;
}

/* the list is the structures, then the strings and signatures they point at. */
static GpStatus codec_list(UINT count, UINT size, ImageCodecInfo *list)
{
    UINT want_count, want_size;
    codec_sizes(&want_count, &want_size);
    if (!list || count != want_count || size != want_size)
        return GenericError;
    BYTE *extra = (BYTE *)(list + count);
    for (UINT i = 0; i < count; i++) {
        const CODEC *c = &g_codecs[i];
        ImageCodecInfo *info = &list[i];
        memset(info, 0, sizeof(*info));
        info->Clsid = *c->Clsid;
        info->FormatID = *c->Format;
        const WCHAR *strings[4] = {c->Name, c->Description, c->Extensions, c->Mime};
        const WCHAR **slots[4] = {&info->CodecName, &info->FormatDescription, &info->FilenameExtension,
                                  &info->MimeType};
        for (int k = 0; k < 4; k++) {
            SIZE_T bytes = string_bytes(strings[k]);
            memcpy(extra, strings[k], bytes);
            *slots[k] = (const WCHAR *)extra;
            extra += bytes;
        }
        info->Flags = ImageCodecFlagsEncoder | ImageCodecFlagsDecoder | ImageCodecFlagsSupportBitmap |
                      ImageCodecFlagsBuiltin;
        info->Version = 1;
        info->SigCount = 1;
        info->SigSize = c->SignatureSize;
        memcpy(extra, c->Signature, c->SignatureSize);
        info->SigPattern = extra;
        extra += c->SignatureSize;
        memcpy(extra, g_mask_full, c->SignatureSize);
        info->SigMask = extra;
        extra += c->SignatureSize;
    }
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageEncodersSize(UINT *count, UINT *size)
{
    if (!count || !size)
        return InvalidParameter;
    return codec_sizes(count, size);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageEncoders(UINT count, UINT size, ImageCodecInfo *encoders)
{
    return codec_list(count, size, encoders);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageDecodersSize(UINT *count, UINT *size)
{
    if (!count || !size)
        return InvalidParameter;
    return codec_sizes(count, size);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetImageDecoders(UINT count, UINT size, ImageCodecInfo *decoders)
{
    return codec_list(count, size, decoders);
}

/* the encoder of a class id: found, a codec windows has that is absent here, or unknown. */
static const CODEC *encoder_of(const CLSID *clsid, GpStatus *status)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(g_codecs); i++)
        if (same_guid(clsid, g_codecs[i].Clsid))
            return &g_codecs[i];
    *status = InvalidParameter;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_clsid_other); i++)
        if (same_guid(clsid, &g_clsid_other[i]))
            *status = NotImplemented;
    return nullptr;
}

/* the encoders take no parameters here: the list is its count of zero, in the room of the structure up to it. */
DECLSPEC_EXPORT GpStatus WINAPI GdipGetEncoderParameterListSize(GpImage *image, const CLSID *encoder, UINT *size)
{
    if (!gp_bitmap_cast(image) || !encoder || !size)
        return InvalidParameter;
    GpStatus status = Ok;
    if (!encoder_of(encoder, &status))
        return status;
    *size = offsetof(EncoderParameters, Parameter);
    return Ok;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipGetEncoderParameterList(GpImage *image, const CLSID *encoder, UINT size,
                                                            EncoderParameters *parameters)
{
    if (!gp_bitmap_cast(image) || !encoder || !parameters)
        return InvalidParameter;
    GpStatus status = Ok;
    if (!encoder_of(encoder, &status))
        return status;
    if (size < offsetof(EncoderParameters, Parameter))
        return InsufficientBuffer;
    parameters->Count = 0;
    return Ok;
}

/* ------------------------------------------------------------------ saving */

static GpStatus encode(GpBitmap *b, const CLSID *clsid, GpBuffer *out)
{
    GpStatus status = Ok;
    const CODEC *codec = encoder_of(clsid, &status);
    if (!codec)
        return status;
    return codec->Clsid == &g_clsid_bmp ? gp_encode_bmp(b, out) : gp_encode_png(b, out);
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSaveImageToStream(GpImage *image, IStream *stream, const CLSID *encoder,
                                                      const EncoderParameters *parameters)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !stream || !encoder)
        return InvalidParameter;
    GpBuffer out = {};
    GpStatus status = encode(b, encoder, &out);
    if (status == Ok) {
        SIZE_T done = 0;
        while (done < out.Size) {
            ULONG chunk = out.Size - done > 0x10000000 ? 0x10000000 : (ULONG)(out.Size - done), written = 0;
            HRESULT hr = stream->lpVtbl->Write(stream, out.Data + done, chunk, &written);
            if (FAILED(hr) || !written) {
                status = Win32Error;
                break;
            }
            done += written;
        }
    }
    gp_free(out.Data);
    return status;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSaveImageToFile(GpImage *image, const WCHAR *file, const CLSID *encoder,
                                                    const EncoderParameters *parameters)
{
    GpBitmap *b = gp_bitmap_cast(image);
    if (!b || !file || !encoder)
        return InvalidParameter;
    GpBuffer out = {};
    GpStatus status = encode(b, encoder, &out);
    if (status == Ok) {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            status = error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ? AccessDenied : Win32Error;
        } else {
            SIZE_T done = 0;
            while (done < out.Size) {
                DWORD written = 0;
                DWORD chunk = out.Size - done > 0x10000000 ? 0x10000000 : (DWORD)(out.Size - done);
                if (!WriteFile(h, out.Data + done, chunk, &written, nullptr) || !written) {
                    status = Win32Error;
                    break;
                }
                done += written;
            }
            CloseHandle(h);
            if (status != Ok)
                DeleteFileW(file);
        }
    }
    gp_free(out.Data);
    return status;
}

/* multiple frames are written by tiff and gif only, which this library has no encoder for. */
DECLSPEC_EXPORT GpStatus WINAPI GdipSaveAdd(GpImage *image, const EncoderParameters *parameters)
{
    if (!gp_bitmap_cast(image) || !parameters)
        return InvalidParameter;
    return WrongState;
}

DECLSPEC_EXPORT GpStatus WINAPI GdipSaveAddImage(GpImage *image, GpImage *added, const EncoderParameters *parameters)
{
    if (!gp_bitmap_cast(image) || !gp_bitmap_cast(added) || !parameters)
        return InvalidParameter;
    return WrongState;
}
