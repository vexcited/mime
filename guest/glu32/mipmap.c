/*
 * glu32 images: gluScaleImage and the mipmap builders. pixels are read into doubles with the unpack state of
 * opengl32, resampled one axis at a time and written back with the pack state.
 */
#include "glu32_private.h"

typedef struct IMAGE {
    double *Data;
    int Width, Height, Components;
} IMAGE;

typedef struct PIXEL_STORE {
    GLint SwapBytes, RowLength, SkipRows, SkipPixels, Alignment;
} PIXEL_STORE;

static int type_size(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        return 4;
    }
    return 0;
}

static int format_components(GLenum format)
{
    switch (format) {
    case GL_COLOR_INDEX:
    case GL_STENCIL_INDEX:
    case GL_DEPTH_COMPONENT:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        return 1;
    case GL_LUMINANCE_ALPHA:
        return 2;
    case GL_RGB:
    case GL_BGR_EXT:
        return 3;
    case GL_RGBA:
    case GL_BGRA_EXT:
        return 4;
    }
    return 0;
}

int GluPixelSize(GLenum format, GLenum type, int *components)
{
    int count = format_components(format), size = type_size(type);
    if (!count || !size)
        return 0;
    if (components)
        *components = count;
    return count * size;
}

static BOOL is_power_of_two(int value)
{
    return value > 0 && !(value & (value - 1));
}

static double read_sample(const BYTE *at, GLenum type, BOOL swap)
{
    BYTE bytes[4];
    int size = type_size(type);
    for (int i = 0; i < size; i++)
        bytes[i] = at[swap ? size - 1 - i : i];
    switch (type) {
    case GL_BYTE:
        return *(signed char *)bytes;
    case GL_UNSIGNED_BYTE:
        return bytes[0];
    case GL_SHORT:
        return *(short *)bytes;
    case GL_UNSIGNED_SHORT:
        return *(unsigned short *)bytes;
    case GL_INT:
        return *(int *)bytes;
    case GL_UNSIGNED_INT:
        return *(unsigned int *)bytes;
    default:
        return *(float *)bytes;
    }
}

static void write_sample(BYTE *at, GLenum type, BOOL swap, double value)
{
    BYTE bytes[4];
    double low, high;
    switch (type) {
    case GL_BYTE: low = -128, high = 127; break;
    case GL_UNSIGNED_BYTE: low = 0, high = 255; break;
    case GL_SHORT: low = -32768, high = 32767; break;
    case GL_UNSIGNED_SHORT: low = 0, high = 65535; break;
    case GL_INT: low = -2147483648.0, high = 2147483647.0; break;
    case GL_UNSIGNED_INT: low = 0, high = 4294967295.0; break;
    default: low = high = 0; break;
    }
    if (type == GL_FLOAT) {
        *(float *)bytes = (float)value;
    } else {
        value = value < 0 ? value - 0.5 : value + 0.5;
        if (value < low)
            value = low;
        if (value > high)
            value = high;
        switch (type) {
        case GL_BYTE: *(signed char *)bytes = (signed char)value; break;
        case GL_UNSIGNED_BYTE: bytes[0] = (BYTE)value; break;
        case GL_SHORT: *(short *)bytes = (short)value; break;
        case GL_UNSIGNED_SHORT: *(unsigned short *)bytes = (unsigned short)value; break;
        case GL_INT: *(int *)bytes = (int)value; break;
        default: *(unsigned int *)bytes = (unsigned int)value; break;
        }
    }
    int size = type_size(type);
    for (int i = 0; i < size; i++)
        at[swap ? size - 1 - i : i] = bytes[i];
}

static SIZE_T row_stride(const PIXEL_STORE *store, int width, int pixel_size)
{
    SIZE_T row = (store->RowLength > 0 ? store->RowLength : width) * (SIZE_T)pixel_size;
    SIZE_T alignment = store->Alignment > 0 ? store->Alignment : 1;
    if (pixel_size < (int)alignment)
        row = (row + alignment - 1) / alignment * alignment;
    return row;
}

static void get_store(const GL_API *gl, BOOL unpack, PIXEL_STORE *store)
{
    /* the values of a new context stay when there is no context to ask. */
    *store = (PIXEL_STORE){.Alignment = 4};
    gl->GetIntegerv(unpack ? GL_UNPACK_SWAP_BYTES : GL_PACK_SWAP_BYTES, &store->SwapBytes);
    gl->GetIntegerv(unpack ? GL_UNPACK_ROW_LENGTH : GL_PACK_ROW_LENGTH, &store->RowLength);
    gl->GetIntegerv(unpack ? GL_UNPACK_SKIP_ROWS : GL_PACK_SKIP_ROWS, &store->SkipRows);
    gl->GetIntegerv(unpack ? GL_UNPACK_SKIP_PIXELS : GL_PACK_SKIP_PIXELS, &store->SkipPixels);
    gl->GetIntegerv(unpack ? GL_UNPACK_ALIGNMENT : GL_PACK_ALIGNMENT, &store->Alignment);
}

static BOOL load_image(const PIXEL_STORE *store, int width, int height, GLenum format, GLenum type,
                       const void *pixels, IMAGE *image)
{
    int components, pixel_size = GluPixelSize(format, type, &components);
    SIZE_T stride = row_stride(store, width, pixel_size);
    int size = type_size(type);
    image->Width = width;
    image->Height = height;
    image->Components = components;
    image->Data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * components * sizeof(double));
    if (!image->Data)
        return FALSE;
    const BYTE *base = (const BYTE *)pixels + store->SkipRows * stride + store->SkipPixels * (SIZE_T)pixel_size;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            for (int c = 0; c < components; c++)
                image->Data[((SIZE_T)y * width + x) * components + c] =
                    read_sample(base + y * stride + (SIZE_T)x * pixel_size + c * size, type, store->SwapBytes);
    return TRUE;
}

/* one axis of the resample. smaller images average the source pixels they cover and larger ones interpolate. */
static BOOL resample_axis(const IMAGE *source, int length, BOOL horizontal, IMAGE *result)
{
    int old = horizontal ? source->Width : source->Height;
    int width = horizontal ? length : source->Width, height = horizontal ? source->Height : length;
    int components = source->Components;
    result->Width = width;
    result->Height = height;
    result->Components = components;
    result->Data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * components * sizeof(double));
    if (!result->Data)
        return FALSE;
    int other = horizontal ? height : width;
    for (int line = 0; line < other; line++) {
        for (int i = 0; i < length; i++) {
            for (int c = 0; c < components; c++) {
                #define SAMPLE(index) \
                    source->Data[(horizontal ? ((SIZE_T)line * old + (index)) : ((SIZE_T)(index) * source->Width + line)) \
                                     * components + c]
                double value;
                if (length < old) {
                    double from = (double)i * old / length, to = (double)(i + 1) * old / length, total = 0;
                    int first = (int)from, last = (int)to;
                    if (last >= old)
                        last = old - 1;
                    for (int k = first; k <= last; k++) {
                        double low = k > from ? k : from, high = k + 1 < to ? k + 1 : to;
                        if (high > low)
                            total += SAMPLE(k) * (high - low);
                    }
                    value = total / (to - from);
                } else if (length == old) {
                    value = SAMPLE(i);
                } else {
                    double position = ((double)i + 0.5) * old / length - 0.5;
                    int k = position < 0 ? 0 : (int)position;
                    int next = k + 1 < old ? k + 1 : old - 1;
                    double blend = position - k;
                    if (blend < 0)
                        blend = 0;
                    value = SAMPLE(k) * (1 - blend) + SAMPLE(next) * blend;
                }
                #undef SAMPLE
                SIZE_T index = horizontal ? ((SIZE_T)line * width + i) : ((SIZE_T)i * width + line);
                result->Data[index * components + c] = value;
            }
        }
    }
    return TRUE;
}

static void free_image(IMAGE *image)
{
    if (image->Data)
        HeapFree(GetProcessHeap(), 0, image->Data);
    image->Data = nullptr;
}

static BOOL resample(IMAGE *image, int width, int height)
{
    IMAGE next;
    if (image->Width != width) {
        if (!resample_axis(image, width, TRUE, &next))
            return FALSE;
        free_image(image);
        *image = next;
    }
    if (image->Height != height) {
        if (!resample_axis(image, height, FALSE, &next))
            return FALSE;
        free_image(image);
        *image = next;
    }
    return TRUE;
}

static GLenum check_arguments(int width, int height, GLenum format, GLenum type)
{
    if (width < 1 || height < 1)
        return GLU_INVALID_VALUE;
    if (!format_components(format) || !type_size(type))
        return GLU_INVALID_ENUM;
    return 0;
}

GLUAPI GLint GLAPIENTRY gluScaleImage(GLenum format, GLint width_in, GLint height_in, GLenum type_in,
                                      const void *data_in, GLint width_out, GLint height_out, GLenum type_out,
                                      void *data_out)
{
    if (!data_in || !data_out)
        return GLU_INVALID_VALUE;
    GLenum error = check_arguments(width_in, height_in, format, type_in);
    if (!error)
        error = check_arguments(width_out, height_out, format, type_out);
    if (error)
        return error;

    const GL_API *gl = GluGl();
    PIXEL_STORE unpack, pack;
    get_store(gl, TRUE, &unpack);
    get_store(gl, FALSE, &pack);
    IMAGE image;
    if (!load_image(&unpack, width_in, height_in, format, type_in, data_in, &image))
        return GLU_OUT_OF_MEMORY;
    if (!resample(&image, width_out, height_out)) {
        free_image(&image);
        return GLU_OUT_OF_MEMORY;
    }
    int components = image.Components, size = type_size(type_out), pixel_size = components * size;
    SIZE_T stride = row_stride(&pack, width_out, pixel_size);
    BYTE *base = (BYTE *)data_out + pack.SkipRows * stride + pack.SkipPixels * (SIZE_T)pixel_size;
    for (int y = 0; y < height_out; y++)
        for (int x = 0; x < width_out; x++)
            for (int c = 0; c < components; c++)
                write_sample(base + y * stride + (SIZE_T)x * pixel_size + c * size, type_out, pack.SwapBytes,
                             image.Data[((SIZE_T)y * width_out + x) * components + c]);
    free_image(&image);
    return 0;
}

/* ------------------------------------------------------------------ mipmaps */

static int nearest_power_of_two(int value)
{
    int below = 1;
    while (below * 2 <= value)
        below *= 2;
    return value - below < below * 2 - value ? below : below * 2;
}

/* the image as bytes of the given type, tightly packed, for the texture calls. */
static void *store_levels(const IMAGE *image, GLenum type)
{
    int size = type_size(type), components = image->Components;
    BYTE *bytes = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)image->Width * image->Height * components * size);
    if (!bytes)
        return nullptr;
    for (SIZE_T i = 0; i < (SIZE_T)image->Width * image->Height * components; i++)
        write_sample(bytes + i * size, type, FALSE, image->Data[i]);
    return bytes;
}

static GLint build_mipmaps(GLenum target, GLint internal_format, GLint width, GLint height, GLenum format,
                           GLenum type, const void *data)
{
    if (!data)
        return GLU_INVALID_VALUE;
    GLenum error = check_arguments(width, height, format, type);
    if (error)
        return error;

    const GL_API *gl = GluGl();
    PIXEL_STORE unpack;
    get_store(gl, TRUE, &unpack);
    GLint limit = 1024;
    gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (limit < 1)
        limit = 1024;

    IMAGE image;
    if (!load_image(&unpack, width, height, format, type, data, &image))
        return GLU_OUT_OF_MEMORY;
    int new_width = is_power_of_two(width) ? width : nearest_power_of_two(width);
    int new_height = height == 1 && target == GL_TEXTURE_1D ? 1 : is_power_of_two(height) ? height : nearest_power_of_two(height);
    if (new_width > limit)
        new_width = limit;
    if (new_height > limit)
        new_height = limit;
    if (!resample(&image, new_width, new_height)) {
        free_image(&image);
        return GLU_OUT_OF_MEMORY;
    }

    /* the levels are tightly packed, whatever the caller's unpack state was. */
    gl->PixelStorei(GL_UNPACK_SWAP_BYTES, 0);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gl->PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLint result = 0;
    for (int level = 0;; level++) {
        void *bytes = store_levels(&image, type);
        if (!bytes) {
            result = GLU_OUT_OF_MEMORY;
            break;
        }
        if (target == GL_TEXTURE_1D)
            gl->TexImage1D(target, level, internal_format, image.Width, 0, format, type, bytes);
        else
            gl->TexImage2D(target, level, internal_format, image.Width, image.Height, 0, format, type, bytes);
        HeapFree(GetProcessHeap(), 0, bytes);
        if (image.Width == 1 && image.Height == 1)
            break;
        if (!resample(&image, image.Width > 1 ? image.Width / 2 : 1, image.Height > 1 ? image.Height / 2 : 1)) {
            result = GLU_OUT_OF_MEMORY;
            break;
        }
    }
    free_image(&image);

    gl->PixelStorei(GL_UNPACK_SWAP_BYTES, unpack.SwapBytes);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, unpack.RowLength);
    gl->PixelStorei(GL_UNPACK_SKIP_ROWS, unpack.SkipRows);
    gl->PixelStorei(GL_UNPACK_SKIP_PIXELS, unpack.SkipPixels);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, unpack.Alignment);
    return result;
}

GLUAPI GLint GLAPIENTRY gluBuild1DMipmaps(GLenum target, GLint internal_format, GLint width, GLenum format,
                                          GLenum type, const void *data)
{
    if (target != GL_TEXTURE_1D)
        return GLU_INVALID_ENUM;
    return build_mipmaps(target, internal_format, width, 1, format, type, data);
}

GLUAPI GLint GLAPIENTRY gluBuild2DMipmaps(GLenum target, GLint internal_format, GLint width, GLint height,
                                          GLenum format, GLenum type, const void *data)
{
    if (target != GL_TEXTURE_2D)
        return GLU_INVALID_ENUM;
    return build_mipmaps(target, internal_format, width, height, format, type, data);
}
