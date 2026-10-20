/*
 * writing images: a bitmap is handed to the encoder of the imaging component, with the settings of the
 * encoder parameters. a gif or a tiff can take several frames, the encoder then stays open in the first image
 * until the last frame is added.
 */
#include "gp_private.h"
#include <mime/win32/wincodec.h>

GpStatus gp_wic_factory(IWICImagingFactory **factory);
const GUID *gp_wic_format_of(PixelFormat f);
const GUID *gp_encoder_format(const CLSID *clsid);

static BOOL same_guid(const GUID *a, const GUID *b) { return !memcmp(a, b, sizeof(GUID)); }

/* ------------------------------------------------------------------ the parameters */

typedef struct SaveParams {
    int quality;       /* 0..100, -1 when not given */
    int compression;   /* the tiff compression method of the component, -1 when not given */
    int depth;         /* the color depth of a tiff, 0 when not given */
    int transform;     /* WICBitmapTransformOptions, 0 for none */
    int flag;          /* the save flag value, 0 when not given */
    BOOL cmyk;
} SaveParams;

enum {
    VALUE_COMPRESSION_LZW = 2,
    VALUE_COMPRESSION_CCITT3,
    VALUE_COMPRESSION_RLE,
    VALUE_COMPRESSION_CCITT4,
    VALUE_COMPRESSION_NONE,
    VALUE_ROTATE90 = 13,
    VALUE_ROTATE180,
    VALUE_ROTATE270,
    VALUE_FLIP_H,
    VALUE_FLIP_V,
    VALUE_MULTI_FRAME,
    VALUE_LAST_FRAME,
    VALUE_FLUSH,
    VALUE_FRAME_TIME,
    VALUE_RESOLUTION,
    VALUE_FRAME_PAGE,
};

static GpStatus parse_params(const EncoderParameters *p, SaveParams *out)
{
    out->quality = -1;
    out->compression = -1;
    out->depth = 0;
    out->transform = 0;
    out->flag = 0;
    out->cmyk = FALSE;
    if (!p)
        return Ok;
    for (UINT i = 0; i < p->Count; i++) {
        const EncoderParameter *e = &p->Parameter[i];
        if (!e->NumberOfValues || !e->Value)
            continue;
        if (same_guid(&e->Guid, &EncoderQuality)) {
            if (e->Type != EncoderParameterValueTypeLong || e->NumberOfValues != 1)
                return InvalidParameter;
            ULONG v = *(ULONG *)e->Value;
            out->quality = v > 100 ? 100 : (int)v;
        } else if (same_guid(&e->Guid, &EncoderCompression)) {
            if (e->Type != EncoderParameterValueTypeLong || e->NumberOfValues != 1)
                return InvalidParameter;
            switch (*(ULONG *)e->Value) {
            case VALUE_COMPRESSION_LZW:
                out->compression = WICTiffCompressionLZW;
                break;
            case VALUE_COMPRESSION_CCITT3:
                out->compression = WICTiffCompressionCCITT3;
                break;
            case VALUE_COMPRESSION_RLE:
                out->compression = WICTiffCompressionRLE;
                break;
            case VALUE_COMPRESSION_CCITT4:
                out->compression = WICTiffCompressionCCITT4;
                break;
            case VALUE_COMPRESSION_NONE:
                out->compression = WICTiffCompressionNone;
                break;
            default:
                return InvalidParameter;
            }
        } else if (same_guid(&e->Guid, &EncoderColorDepth)) {
            if (e->Type != EncoderParameterValueTypeLong || e->NumberOfValues != 1)
                return InvalidParameter;
            ULONG v = *(ULONG *)e->Value;
            if (v != 1 && v != 4 && v != 8 && v != 24 && v != 32)
                return InvalidParameter;
            out->depth = (int)v;
        } else if (same_guid(&e->Guid, &EncoderTransformation)) {
            if (e->Type != EncoderParameterValueTypeLong || e->NumberOfValues != 1)
                return InvalidParameter;
            switch (*(ULONG *)e->Value) {
            case VALUE_ROTATE90:
                out->transform = WICBitmapTransformRotate90;
                break;
            case VALUE_ROTATE180:
                out->transform = WICBitmapTransformRotate180;
                break;
            case VALUE_ROTATE270:
                out->transform = WICBitmapTransformRotate270;
                break;
            case VALUE_FLIP_H:
                out->transform = WICBitmapTransformFlipHorizontal;
                break;
            case VALUE_FLIP_V:
                out->transform = WICBitmapTransformFlipVertical;
                break;
            default:
                return InvalidParameter;
            }
        } else if (same_guid(&e->Guid, &EncoderSaveFlag)) {
            if (e->Type != EncoderParameterValueTypeLong || e->NumberOfValues != 1)
                return InvalidParameter;
            out->flag = (int)*(ULONG *)e->Value;
        } else if (same_guid(&e->Guid, &EncoderSaveAsCMYK)) {
            out->cmyk = *(ULONG *)e->Value != 0;
        }
    }
    return Ok;
}

/* ------------------------------------------------------------------ the encoder in use */

typedef struct SaveState {
    IWICBitmapEncoder *encoder;
    IStream *stream;
    const GUID *format;
    UINT frames;
} SaveState;

static void state_free(SaveState *s)
{
    if (!s)
        return;
    if (s->encoder)
        s->encoder->lpVtbl->Release(s->encoder);
    if (s->stream)
        s->stream->lpVtbl->Release(s->stream);
    gp_free(s);
}

void gp_save_state_free(void *state) { state_free(state); }

static const GUID *container_of(const GUID *format)
{
    if (same_guid(format, &ImageFormatPNG))
        return &GUID_ContainerFormatPng;
    if (same_guid(format, &ImageFormatJPEG))
        return &GUID_ContainerFormatJpeg;
    if (same_guid(format, &ImageFormatGIF))
        return &GUID_ContainerFormatGif;
    if (same_guid(format, &ImageFormatTIFF))
        return &GUID_ContainerFormatTiff;
    if (same_guid(format, &ImageFormatBMP))
        return &GUID_ContainerFormatBmp;
    return nullptr;
}

static BOOL multi_frame_format(const GUID *format)
{
    return same_guid(format, &ImageFormatGIF) || same_guid(format, &ImageFormatTIFF);
}

/* ------------------------------------------------------------------ the pixels */

/* the bitmap as a bitmap of the component, with its palette. */
static GpStatus make_source(IWICImagingFactory *factory, GBitmap *b, IWICBitmap **out)
{
    const GUID *format = gp_wic_format_of(b->format);
    BYTE *copy = nullptr;
    PixelFormat used = b->format;
    UINT stride;
    if (!format) {
        /* a format the component does not have is written as argb */
        format = &GUID_WICPixelFormat32bppBGRA;
        used = PixelFormat32bppARGB;
    }
    stride = (UINT)gp_default_stride(b->width, used);
    const BYTE *rows;
    if (used == b->format && b->stride > 0 && (UINT)b->stride >= stride) {
        rows = b->bits;
        stride = (UINT)b->stride;
    } else {
        copy = gp_alloc((SIZE_T)stride * b->height);
        if (!copy)
            return OutOfMemory;
        for (INT y = 0; y < b->height; y++) {
            BYTE *row = copy + (SIZE_T)y * stride;
            if (used == b->format)
                memcpy(row, b->bits + (SSIZE_T)y * b->stride, stride);
            else
                gp_bitmap_read_argb(b, 0, y, b->width, (UINT32 *)row);
        }
        rows = copy;
    }
    IWICBitmap *bitmap = nullptr;
    HRESULT hr = factory->lpVtbl->CreateBitmapFromMemory(factory, (UINT)b->width, (UINT)b->height, format, stride,
                                                         stride * b->height, (BYTE *)rows, &bitmap);
    gp_free(copy);
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? OutOfMemory : GenericError;
    if (used == b->format && b->palette && b->palette->Count) {
        IWICPalette *pal = nullptr;
        if (SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &pal))) {
            if (SUCCEEDED(pal->lpVtbl->InitializeCustom(pal, (WICColor *)b->palette->Entries, b->palette->Count)))
                bitmap->lpVtbl->SetPalette(bitmap, pal);
            pal->lpVtbl->Release(pal);
        }
    }
    bitmap->lpVtbl->SetResolution(bitmap, b->dpi_x, b->dpi_y);
    *out = bitmap;
    return Ok;
}

static BOOL has_clear_pixels(GBitmap *b)
{
    if (!(b->format & PixelFormatAlpha))
        return FALSE;
    UINT32 *row = gp_alloc((SIZE_T)b->width * 4);
    if (!row)
        return FALSE;
    BOOL found = FALSE;
    for (INT y = 0; y < b->height && !found; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, row);
        for (INT x = 0; x < b->width; x++) {
            if ((row[x] >> 24) < 128) {
                found = TRUE;
                break;
            }
        }
    }
    gp_free(row);
    return found;
}

static BOOL format_is_indexed(const GUID *f)
{
    return same_guid(f, &GUID_WICPixelFormat1bppIndexed) || same_guid(f, &GUID_WICPixelFormat2bppIndexed) ||
           same_guid(f, &GUID_WICPixelFormat4bppIndexed) || same_guid(f, &GUID_WICPixelFormat8bppIndexed);
}

static UINT indexed_bits(const GUID *f)
{
    if (same_guid(f, &GUID_WICPixelFormat1bppIndexed))
        return 1;
    if (same_guid(f, &GUID_WICPixelFormat2bppIndexed))
        return 2;
    if (same_guid(f, &GUID_WICPixelFormat4bppIndexed))
        return 4;
    return 8;
}

/*
 * the palette that colors get reduced to. a picture that is indexed already keeps its colors, otherwise the
 * halftone palettes stand in, with a clear color when pixels are transparent.
 */
static IWICPalette *palette_for(IWICImagingFactory *factory, IWICBitmapSource *src, UINT bits, BOOL transparent)
{
    IWICPalette *pal = nullptr;
    if (FAILED(factory->lpVtbl->CreatePalette(factory, &pal)))
        return nullptr;
    HRESULT hr = E_FAIL;
    GUID f;
    if (SUCCEEDED(src->lpVtbl->GetPixelFormat(src, &f)) && format_is_indexed(&f) && indexed_bits(&f) <= bits) {
        hr = src->lpVtbl->CopyPalette(src, pal);
    } else if (bits == 1) {
        hr = pal->lpVtbl->InitializePredefined(pal, WICBitmapPaletteTypeFixedBW, FALSE);
    } else if (bits == 4) {
        ColorPalette *vga = gp_palette_default(PixelFormat4bppIndexed);
        if (vga) {
            hr = pal->lpVtbl->InitializeCustom(pal, (WICColor *)vga->Entries, vga->Count);
            gp_free(vga);
        }
    } else {
        hr = pal->lpVtbl->InitializePredefined(pal, WICBitmapPaletteTypeFixedHalftone256, transparent);
    }
    if (FAILED(hr)) {
        pal->lpVtbl->Release(pal);
        return nullptr;
    }
    return pal;
}

static const GUID *depth_format(int depth)
{
    switch (depth) {
    case 1:
        return &GUID_WICPixelFormat1bppIndexed;
    case 4:
        return &GUID_WICPixelFormat4bppIndexed;
    case 8:
        return &GUID_WICPixelFormat8bppIndexed;
    case 24:
        return &GUID_WICPixelFormat24bppBGR;
    default:
        return &GUID_WICPixelFormat32bppBGRA;
    }
}

/* converts a source to a pixel format, with the palette of an indexed one. */
static GpStatus convert_source(IWICImagingFactory *factory, IWICBitmapSource *src, const GUID *target,
                               BOOL transparent, IWICBitmapSource **out, IWICPalette **palette)
{
    IWICFormatConverter *conv = nullptr;
    if (FAILED(factory->lpVtbl->CreateFormatConverter(factory, &conv)))
        return GenericError;
    IWICPalette *pal = nullptr;
    WICBitmapDitherType dither = WICBitmapDitherTypeNone;
    WICBitmapPaletteType type = WICBitmapPaletteTypeCustom;
    if (format_is_indexed(target)) {
        UINT bits = indexed_bits(target);
        pal = palette_for(factory, src, bits, transparent);
        if (!pal) {
            conv->lpVtbl->Release(conv);
            return OutOfMemory;
        }
        GUID f;
        BOOL indexed_source = SUCCEEDED(src->lpVtbl->GetPixelFormat(src, &f)) && format_is_indexed(&f);
        if (!indexed_source)
            dither = bits == 8 ? WICBitmapDitherTypeErrorDiffusion : WICBitmapDitherTypeOrdered8x8;
    }
    HRESULT hr = conv->lpVtbl->Initialize(conv, src, target, dither, pal, transparent ? 50.0 : 0.0, type);
    if (FAILED(hr)) {
        if (pal)
            pal->lpVtbl->Release(pal);
        conv->lpVtbl->Release(conv);
        return hr == E_OUTOFMEMORY ? OutOfMemory : GenericError;
    }
    *out = (IWICBitmapSource *)conv;
    if (palette)
        *palette = pal;
    else if (pal)
        pal->lpVtbl->Release(pal);
    return Ok;
}

/* ------------------------------------------------------------------ properties as metadata */

enum { T_BYTE = 1, T_ASCII = 2, T_SHORT = 3, T_LONG = 4, T_RATIONAL = 5, T_UNDEFINED = 7, T_SLONG = 9, T_SRATIONAL = 10 };

/* the tags the encoder writes on its own. */
static BOOL structural_tag(ULONG id)
{
    switch (id) {
    case 0x0100:
    case 0x0101:
    case 0x0102:
    case 0x0103:
    case 0x0106:
    case 0x0111:
    case 0x0115:
    case 0x0116:
    case 0x0117:
    case 0x011a:
    case 0x011b:
    case 0x011c:
    case 0x0128:
    case 0x0140:
    case 0x0201:
    case 0x0202:
    case 0x8769:
    case 0x8825:
    case 0xa005:
        return TRUE;
    }
    return id >= 0x5000;
}

static BOOL exif_tag(ULONG id)
{
    return (id >= 0x829a && id <= 0x829d) || id == 0x8822 || id == 0x8824 || id == 0x8827 || id == 0x8828 ||
           (id >= 0x8830 && id <= 0x8832) || (id >= 0x9000 && id < 0xa600 && id != 0xa005) || id == 0xa000 ||
           id == 0xa001;
}

static BOOL set_property(IWICMetadataQueryWriter *w, const WCHAR *prefix, const GProperty *p)
{
    const WCHAR *scope = L"";
    if (p->id < 0x20)
        scope = L"/gps";
    else if (exif_tag(p->id))
        scope = L"/exif";
    WCHAR name[64];
    wsprintfW(name, L"%ls%ls/{ushort=%u}", prefix, scope, p->id);
    PROPVARIANT v;
    memset(&v, 0, sizeof(v));
    ULONG n = p->length;
    BYTE *d = p->value;
    switch (p->type) {
    case T_BYTE:
        if (n == 1) {
            v.vt = VT_UI1;
            v.bVal = d[0];
        } else {
            v.vt = VT_UI1 | VT_VECTOR;
            v.caub.cElems = n;
            v.caub.pElems = d;
        }
        break;
    case T_ASCII:
        if (!n || d[n - 1])
            return FALSE;
        v.vt = VT_LPSTR;
        v.pszVal = (char *)d;
        break;
    case T_SHORT:
        if (n == 2) {
            v.vt = VT_UI2;
            memcpy(&v.uiVal, d, 2);
        } else {
            v.vt = VT_UI2 | VT_VECTOR;
            v.caui.cElems = n / 2;
            v.caui.pElems = (USHORT *)d;
        }
        break;
    case T_LONG:
        if (n == 4) {
            v.vt = VT_UI4;
            memcpy(&v.ulVal, d, 4);
        } else {
            v.vt = VT_UI4 | VT_VECTOR;
            v.caul.cElems = n / 4;
            v.caul.pElems = (ULONG *)d;
        }
        break;
    case T_SLONG:
        if (n != 4)
            return FALSE;
        v.vt = VT_I4;
        memcpy(&v.lVal, d, 4);
        break;
    case T_RATIONAL:
        if (n != 8)
            return FALSE;
        v.vt = VT_UI8;
        memcpy(&v.uhVal, d, 8);
        break;
    case T_SRATIONAL:
        if (n != 8)
            return FALSE;
        v.vt = VT_I8;
        memcpy(&v.hVal, d, 8);
        break;
    default:
        v.vt = VT_BLOB;
        v.blob.cbSize = n;
        v.blob.pBlobData = d;
        break;
    }
    return SUCCEEDED(w->lpVtbl->SetMetadataByName(w, name, &v));
}

static void write_properties(IWICBitmapFrameEncode *frame, const GUID *format, GBitmap *b)
{
    const WCHAR *prefix;
    if (same_guid(format, &ImageFormatJPEG))
        prefix = L"/app1/ifd";
    else if (same_guid(format, &ImageFormatTIFF))
        prefix = L"/ifd";
    else
        return;
    IWICMetadataQueryWriter *w = nullptr;
    if (FAILED(frame->lpVtbl->GetMetadataQueryWriter(frame, &w)))
        return;
    for (UINT i = 0; i < b->prop_count; i++) {
        if (!structural_tag(b->props[i].id))
            set_property(w, prefix, &b->props[i]);
    }
    w->lpVtbl->Release(w);
}

/* the delay of a gif frame, in hundredths of a second, from the property that lists them. */
static void write_gif_frame(IWICBitmapFrameEncode *frame, GBitmap *b, UINT index)
{
    for (UINT i = 0; i < b->prop_count; i++) {
        GProperty *p = &b->props[i];
        if (p->id == 0x5100 && p->type == T_LONG && p->length >= 4) {
            UINT count = p->length / 4;
            UINT32 delay;
            memcpy(&delay, p->value + 4 * (index < count ? index : count - 1), 4);
            IWICMetadataQueryWriter *w = nullptr;
            if (SUCCEEDED(frame->lpVtbl->GetMetadataQueryWriter(frame, &w))) {
                PROPVARIANT v;
                memset(&v, 0, sizeof(v));
                v.vt = VT_UI2;
                v.uiVal = (USHORT)(delay > 65535 ? 65535 : delay);
                w->lpVtbl->SetMetadataByName(w, L"/grctlext/Delay", &v);
                w->lpVtbl->Release(w);
            }
        }
    }
}

static void write_gif_loop(IWICBitmapEncoder *encoder, GBitmap *b)
{
    for (UINT i = 0; i < b->prop_count; i++) {
        GProperty *p = &b->props[i];
        if (p->id == 0x5101 && p->type == T_SHORT && p->length >= 2) {
            USHORT loops;
            memcpy(&loops, p->value, 2);
            IWICMetadataQueryWriter *w = nullptr;
            if (SUCCEEDED(encoder->lpVtbl->GetMetadataQueryWriter(encoder, &w))) {
                static BYTE app[11] = {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
                BYTE data[4] = {3, 1, (BYTE)loops, (BYTE)(loops >> 8)};
                PROPVARIANT v;
                memset(&v, 0, sizeof(v));
                v.vt = VT_UI1 | VT_VECTOR;
                v.caub.cElems = sizeof(app);
                v.caub.pElems = app;
                w->lpVtbl->SetMetadataByName(w, L"/appext/Application", &v);
                v.caub.cElems = sizeof(data);
                v.caub.pElems = data;
                w->lpVtbl->SetMetadataByName(w, L"/appext/Data", &v);
                w->lpVtbl->Release(w);
            }
        }
    }
}

/* ------------------------------------------------------------------ one frame */

static void write_option(IPropertyBag2 *bag, const WCHAR *name, VARTYPE vt, const VARIANT *value)
{
    PROPBAG2 prop;
    memset(&prop, 0, sizeof(prop));
    prop.pstrName = (LPOLESTR)name;
    bag->lpVtbl->Write(bag, 1, &prop, (VARIANT *)value);
    (void)vt;
}

static GpStatus encode_frame(IWICImagingFactory *factory, SaveState *s, GBitmap *b, const SaveParams *sp)
{
    IWICBitmapFrameEncode *frame = nullptr;
    IPropertyBag2 *bag = nullptr;
    HRESULT hr = s->encoder->lpVtbl->CreateNewFrame(s->encoder, &frame, &bag);
    if (FAILED(hr))
        return GenericError;
    GpStatus st = Ok;
    IWICBitmap *bitmap = nullptr;
    IWICBitmapSource *src = nullptr, *tmp = nullptr;
    IWICPalette *palette = nullptr;
    VARIANT v;
    if (bag) {
        if (sp->quality >= 0 && same_guid(s->format, &ImageFormatJPEG)) {
            memset(&v, 0, sizeof(v));
            v.vt = VT_R4;
            v.fltVal = (float)sp->quality / 100.0f;
            write_option(bag, L"ImageQuality", VT_R4, &v);
        }
        if (sp->compression >= 0 && same_guid(s->format, &ImageFormatTIFF)) {
            memset(&v, 0, sizeof(v));
            v.vt = VT_UI1;
            v.bVal = (BYTE)sp->compression;
            write_option(bag, L"TiffCompressionMethod", VT_UI1, &v);
        }
    }
    hr = frame->lpVtbl->Initialize(frame, bag);
    if (FAILED(hr)) {
        st = GenericError;
        goto done;
    }
    st = make_source(factory, b, &bitmap);
    if (st != Ok)
        goto done;
    src = (IWICBitmapSource *)bitmap;
    src->lpVtbl->AddRef(src);

    BOOL transparent = has_clear_pixels(b);
    if (sp->depth && same_guid(s->format, &ImageFormatTIFF)) {
        st = convert_source(factory, src, depth_format(sp->depth), transparent, &tmp, &palette);
        if (st != Ok)
            goto done;
        src->lpVtbl->Release(src);
        src = tmp;
        tmp = nullptr;
    }
    if (sp->transform) {
        IWICBitmapFlipRotator *rot = nullptr;
        if (SUCCEEDED(factory->lpVtbl->CreateBitmapFlipRotator(factory, &rot))) {
            if (SUCCEEDED(rot->lpVtbl->Initialize(rot, src, (WICBitmapTransformOptions)sp->transform))) {
                src->lpVtbl->Release(src);
                src = (IWICBitmapSource *)rot;
            } else {
                rot->lpVtbl->Release(rot);
            }
        }
    }

    UINT width, height;
    src->lpVtbl->GetSize(src, &width, &height);
    frame->lpVtbl->SetSize(frame, width, height);
    frame->lpVtbl->SetResolution(frame, b->dpi_x, b->dpi_y);
    GUID wanted;
    src->lpVtbl->GetPixelFormat(src, &wanted);
    GUID chosen = wanted;
    frame->lpVtbl->SetPixelFormat(frame, &chosen);
    if (!same_guid(&chosen, &wanted)) {
        IWICPalette *newpal = nullptr;
        st = convert_source(factory, src, &chosen, transparent, &tmp, &newpal);
        if (st != Ok)
            goto done;
        src->lpVtbl->Release(src);
        src = tmp;
        tmp = nullptr;
        if (palette)
            palette->lpVtbl->Release(palette);
        palette = newpal;
    }
    if (format_is_indexed(&chosen)) {
        if (!palette) {
            if (SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &palette)) &&
                FAILED(src->lpVtbl->CopyPalette(src, palette))) {
                palette->lpVtbl->Release(palette);
                palette = nullptr;
            }
        }
        if (palette) {
            frame->lpVtbl->SetPalette(frame, palette);
            /* the first frame of a gif gives the global color table */
            if (s->frames == 0 && same_guid(s->format, &ImageFormatGIF))
                s->encoder->lpVtbl->SetPalette(s->encoder, palette);
        }
    }
    write_properties(frame, s->format, b);
    if (same_guid(s->format, &ImageFormatGIF))
        write_gif_frame(frame, b, b->frame_index);
    hr = frame->lpVtbl->WriteSource(frame, src, nullptr);
    if (FAILED(hr)) {
        st = hr == E_OUTOFMEMORY ? OutOfMemory : GenericError;
        goto done;
    }
    hr = frame->lpVtbl->Commit(frame);
    if (FAILED(hr))
        st = GenericError;
done:
    if (palette)
        palette->lpVtbl->Release(palette);
    if (tmp)
        tmp->lpVtbl->Release(tmp);
    if (src)
        src->lpVtbl->Release(src);
    if (bitmap)
        bitmap->lpVtbl->Release(bitmap);
    if (bag)
        bag->lpVtbl->Release(bag);
    frame->lpVtbl->Release(frame);
    return st;
}

/* ------------------------------------------------------------------ the api */

static GpStatus open_state(IWICImagingFactory *factory, const GUID *format, const WCHAR *file, IStream *stream,
                           SaveState **out)
{
    const GUID *container = container_of(format);
    if (!container)
        return UnknownImageFormat;
    SaveState *s = gp_alloc_zero(sizeof(*s));
    if (!s)
        return OutOfMemory;
    s->format = format;
    if (file) {
        IWICStream *ws = nullptr;
        if (FAILED(factory->lpVtbl->CreateStream(factory, &ws))) {
            state_free(s);
            return GenericError;
        }
        HRESULT hr = ws->lpVtbl->InitializeFromFilename(ws, file, GENERIC_WRITE);
        if (FAILED(hr)) {
            ws->lpVtbl->Release(ws);
            state_free(s);
            /* a failure of the file system is a win32 error with its code as the last error */
            SetLastError(HRESULT_CODE(hr));
            return Win32Error;
        }
        s->stream = (IStream *)ws;
    } else {
        stream->lpVtbl->AddRef(stream);
        s->stream = stream;
    }
    HRESULT hr = factory->lpVtbl->CreateEncoder(factory, container, nullptr, &s->encoder);
    if (SUCCEEDED(hr))
        hr = s->encoder->lpVtbl->Initialize(s->encoder, s->stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        state_free(s);
        return GenericError;
    }
    *out = s;
    return Ok;
}

/* the bitmap an image stands for when it is written: a vector image is drawn first. */
static GpStatus bitmap_of(GpImage *image, GBitmap **b, BOOL *temporary)
{
    *temporary = FALSE;
    *b = gp_bitmap_cast(image);
    if (*b)
        return Ok;
    GImage *i = gp_image_cast(image);
    if (!i)
        return InvalidParameter;
    GpStatus st = gp_metafile_to_bitmap(i, b);
    if (st == Ok)
        *temporary = TRUE;
    return st;
}

static GpStatus save_image(GpImage *image, const WCHAR *file, IStream *stream, const CLSID *clsid,
                           const EncoderParameters *params)
{
    GBitmap *b;
    BOOL temporary;
    if (!image || !clsid || (!file && !stream))
        return InvalidParameter;
    /* a codec that is not there maps to the status of a missing file, as in windows */
    const GUID *format = gp_encoder_format(clsid);
    if (!format)
        return FileNotFound;
    SaveParams sp;
    GpStatus st = parse_params(params, &sp);
    if (st != Ok)
        return st;
    st = bitmap_of(image, &b, &temporary);
    if (st != Ok)
        return st;
    if (b->locked) {
        st = WrongState;
        goto out;
    }
    if (b->save_state) {
        /* an earlier multi frame save is still open and is finished first */
        state_free(b->save_state);
        b->save_state = nullptr;
    }
    IWICImagingFactory *factory;
    st = gp_wic_factory(&factory);
    if (st != Ok)
        goto out;
    SaveState *s;
    st = open_state(factory, format, file, stream, &s);
    if (st == Ok) {
        st = encode_frame(factory, s, b, &sp);
        s->frames = 1;
        if (st == Ok && sp.flag == VALUE_MULTI_FRAME && multi_frame_format(format) && !temporary) {
            if (same_guid(format, &ImageFormatGIF))
                write_gif_loop(s->encoder, b);
            b->save_state = s;
        } else {
            if (st == Ok && FAILED(s->encoder->lpVtbl->Commit(s->encoder)))
                st = GenericError;
            state_free(s);
        }
    }
    factory->lpVtbl->Release(factory);
out:
    if (temporary)
        gp_bitmap_free(b);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSaveImageToFile(GpImage *image, const WCHAR *file, const CLSID *clsid,
                                               const EncoderParameters *params)
{
    if (!file)
        return InvalidParameter;
    return save_image(image, file, nullptr, clsid, params);
}

WINGDIPAPI GpStatus WINAPI GdipSaveImageToStream(GpImage *image, IStream *stream, const CLSID *clsid,
                                                 const EncoderParameters *params)
{
    if (!stream)
        return InvalidParameter;
    return save_image(image, nullptr, stream, clsid, params);
}

static GpStatus finish_state(GBitmap *b)
{
    SaveState *s = b->save_state;
    b->save_state = nullptr;
    GpStatus st = Ok;
    if (FAILED(s->encoder->lpVtbl->Commit(s->encoder)))
        st = GenericError;
    state_free(s);
    return st;
}

/* adds the frame of an image to the save that image started. */
static GpStatus add_frame(GBitmap *owner, GpImage *frame_image, const EncoderParameters *params)
{
    SaveParams sp;
    GpStatus st = parse_params(params, &sp);
    if (st != Ok)
        return st;
    SaveState *s = owner->save_state;
    if (!s)
        return WrongState;
    if (sp.flag == VALUE_FLUSH && !frame_image)
        return finish_state(owner);
    GBitmap *b;
    BOOL temporary;
    st = bitmap_of(frame_image, &b, &temporary);
    if (st != Ok)
        return st;
    IWICImagingFactory *factory;
    st = gp_wic_factory(&factory);
    if (st == Ok) {
        st = encode_frame(factory, s, b, &sp);
        factory->lpVtbl->Release(factory);
    }
    if (temporary)
        gp_bitmap_free(b);
    if (st == Ok) {
        s->frames++;
        if (sp.flag == VALUE_LAST_FRAME || sp.flag == VALUE_FLUSH)
            st = finish_state(owner);
    }
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSaveAdd(GpImage *image, const EncoderParameters *params)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !params)
        return InvalidParameter;
    SaveParams sp;
    GpStatus st = parse_params(params, &sp);
    if (st != Ok)
        return st;
    if (sp.flag == VALUE_FLUSH) {
        if (!b->save_state)
            return WrongState;
        return finish_state(b);
    }
    return add_frame(b, image, params);
}

WINGDIPAPI GpStatus WINAPI GdipSaveAddImage(GpImage *image, GpImage *new_image, const EncoderParameters *params)
{
    GBitmap *b = gp_bitmap_cast(image);
    if (!b || !new_image || !params)
        return InvalidParameter;
    return add_frame(b, new_image, params);
}
