/*
 * reading images: WIC decodes files and streams one frame at a time into gdi+ pixel formats. a multi-frame
 * image, a gif or a tiff, keeps its decoder to switch frames.
 */
#include "gp_private.h"
#include <mime/win32/wincodec.h>

/* ------------------------------------------------------------------ WIC */

typedef struct WicSession {
    IWICImagingFactory *factory;
} WicSession;

static GpStatus wic_open(WicSession *s)
{
    s->factory = nullptr;
    /* joins com if the thread has not, a thread already in another mode is left alone. */
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                  (void **)&s->factory);
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? OutOfMemory : GenericError;
    return Ok;
}

static void wic_close(WicSession *s)
{
    if (s->factory)
        s->factory->lpVtbl->Release(s->factory);
    s->factory = nullptr;
}

GpStatus gp_wic_factory(IWICImagingFactory **factory)
{
    WicSession s;
    GpStatus st = wic_open(&s);
    *factory = s.factory;
    return st;
}

/* ------------------------------------------------------------------ frame sources */

typedef struct GFrameSource {
    BYTE *data;
    SIZE_T size;
    IWICBitmapDecoder *decoder;
    GUID container;
    UINT count;
    const GUID *dimension; /* the frame dimension. */
    /* gif. */
    BOOL gif;
    UINT screen_w, screen_h;
    UINT background;
    INT transparent;       /* canvas transparent index, -1 for none. */
    BYTE *canvas;          /* the picture so far, one index per pixel. */
    BYTE *saved;           /* for frames that restore the previous picture. */
    INT composed;          /* the frame the canvas holds, -1 for none. */
    WICColor palette[256];
    UINT palette_count;
    UINT prev_l, prev_t, prev_w, prev_h, prev_disposal; /* the frame drawn last. */
    UINT *delays;          /* in 1/100 s, one per frame. */
    UINT loops;
    BOOL has_loops;
} GFrameSource;

void gp_frame_source_free(GFrameSource *s)
{
    if (!s)
        return;
    if (s->decoder)
        s->decoder->lpVtbl->Release(s->decoder);
    gp_free(s->data);
    gp_free(s->canvas);
    gp_free(s->saved);
    gp_free(s->delays);
    gp_free(s);
}

GpStatus gp_frame_source_clone(const GFrameSource *src, GBitmap *dst)
{
    dst->source = nullptr;
    if (!src)
        return Ok;
    return gp_source_reopen(src->data, src->size, dst, nullptr);
}

/* ------------------------------------------------------------------ pixel formats */

typedef struct FormatMap {
    const GUID *wic;
    PixelFormat gp;
} FormatMap;

/* the WIC formats gdi+ keeps as they are. */
static const FormatMap g_formats[] = {
    {&GUID_WICPixelFormat1bppIndexed, PixelFormat1bppIndexed},
    {&GUID_WICPixelFormat4bppIndexed, PixelFormat4bppIndexed},
    {&GUID_WICPixelFormat8bppIndexed, PixelFormat8bppIndexed},
    {&GUID_WICPixelFormatBlackWhite, PixelFormat1bppIndexed},
    {&GUID_WICPixelFormat4bppGray, PixelFormat4bppIndexed},
    {&GUID_WICPixelFormat8bppGray, PixelFormat8bppIndexed},
    {&GUID_WICPixelFormat16bppBGR555, PixelFormat16bppRGB555},
    {&GUID_WICPixelFormat16bppBGR565, PixelFormat16bppRGB565},
    {&GUID_WICPixelFormat16bppGray, PixelFormat16bppGrayScale},
    {&GUID_WICPixelFormat24bppBGR, PixelFormat24bppRGB},
    {&GUID_WICPixelFormat32bppBGR, PixelFormat32bppRGB},
    {&GUID_WICPixelFormat32bppBGRA, PixelFormat32bppARGB},
    {&GUID_WICPixelFormat32bppPBGRA, PixelFormat32bppPARGB},
    {&GUID_WICPixelFormat48bppBGR, PixelFormat48bppRGB},
    {&GUID_WICPixelFormat64bppBGRA, PixelFormat64bppARGB},
    {&GUID_WICPixelFormat64bppPBGRA, PixelFormat64bppPARGB},
};

const GUID *gp_wic_format_of(PixelFormat f)
{
    switch (f) {
    case PixelFormat1bppIndexed:
        return &GUID_WICPixelFormat1bppIndexed;
    case PixelFormat4bppIndexed:
        return &GUID_WICPixelFormat4bppIndexed;
    case PixelFormat8bppIndexed:
        return &GUID_WICPixelFormat8bppIndexed;
    case PixelFormat16bppRGB555:
        return &GUID_WICPixelFormat16bppBGR555;
    case PixelFormat16bppRGB565:
        return &GUID_WICPixelFormat16bppBGR565;
    case PixelFormat16bppGrayScale:
        return &GUID_WICPixelFormat16bppGray;
    case PixelFormat24bppRGB:
        return &GUID_WICPixelFormat24bppBGR;
    case PixelFormat32bppRGB:
        return &GUID_WICPixelFormat32bppBGR;
    case PixelFormat32bppARGB:
        return &GUID_WICPixelFormat32bppBGRA;
    case PixelFormat32bppPARGB:
        return &GUID_WICPixelFormat32bppPBGRA;
    case PixelFormat48bppRGB:
        return &GUID_WICPixelFormat48bppBGR;
    case PixelFormat64bppARGB:
        return &GUID_WICPixelFormat64bppBGRA;
    case PixelFormat64bppPARGB:
        return &GUID_WICPixelFormat64bppPBGRA;
    case PixelFormat32bppCMYK:
        return &GUID_WICPixelFormat32bppCMYK;
    default:
        return nullptr;
    }
}

static BOOL guid_equal(const GUID *a, const GUID *b) { return !memcmp(a, b, sizeof(GUID)); }

static PixelFormat gp_format_of(const GUID *wic, const GUID **target)
{
    for (UINT i = 0; i < sizeof(g_formats) / sizeof(g_formats[0]); i++) {
        if (guid_equal(wic, g_formats[i].wic)) {
            *target = g_formats[i].wic;
            return g_formats[i].gp;
        }
    }
    /* 2 bpp grows to 4 bpp, everything else becomes argb. */
    if (guid_equal(wic, &GUID_WICPixelFormat2bppIndexed)) {
        *target = &GUID_WICPixelFormat4bppIndexed;
        return PixelFormat4bppIndexed;
    }
    if (guid_equal(wic, &GUID_WICPixelFormat2bppGray)) {
        *target = &GUID_WICPixelFormat4bppGray;
        return PixelFormat4bppIndexed;
    }
    *target = &GUID_WICPixelFormat32bppBGRA;
    return PixelFormat32bppARGB;
}

static const GUID *raw_format_of(const GUID *container)
{
    if (guid_equal(container, &GUID_ContainerFormatPng))
        return &ImageFormatPNG;
    if (guid_equal(container, &GUID_ContainerFormatJpeg))
        return &ImageFormatJPEG;
    if (guid_equal(container, &GUID_ContainerFormatGif))
        return &ImageFormatGIF;
    if (guid_equal(container, &GUID_ContainerFormatTiff))
        return &ImageFormatTIFF;
    if (guid_equal(container, &GUID_ContainerFormatIco))
        return &ImageFormatIcon;
    return &ImageFormatBMP;
}

/* ------------------------------------------------------------------ palettes */

static ColorPalette *palette_from_wic(IWICImagingFactory *factory, IWICBitmapSource *source, UINT bits)
{
    UINT size = 1u << bits;
    ColorPalette *p = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * size);
    if (!p)
        return nullptr;
    p->Count = size;
    IWICPalette *pal = nullptr;
    if (SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &pal))) {
        if (SUCCEEDED(source->lpVtbl->CopyPalette(source, pal))) {
            UINT count = 0;
            pal->lpVtbl->GetColorCount(pal, &count);
            if (count > size)
                count = size;
            WICColor colors[256];
            UINT got = 0;
            if (SUCCEEDED(pal->lpVtbl->GetColors(pal, count, colors, &got))) {
                for (UINT i = 0; i < got; i++)
                    p->Entries[i] = colors[i];
            }
        }
        pal->lpVtbl->Release(pal);
    }
    return p;
}

static ColorPalette *gray_palette(UINT bits)
{
    UINT size = 1u << bits;
    ColorPalette *p = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * size);
    if (!p)
        return nullptr;
    p->Count = size;
    p->Flags = PaletteFlagsGrayScale;
    for (UINT i = 0; i < size; i++) {
        UINT v = i * 255 / (size - 1);
        p->Entries[i] = 0xff000000u | v << 16 | v << 8 | v;
    }
    return p;
}

/* ------------------------------------------------------------------ frame properties */

static char lower_of(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static BOOL same_text_ci(const char *a, const char *b)
{
    while (*a && lower_of(*a) == lower_of(*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

static GProperty *prop_add(GBitmap *b, ULONG id, WORD type, ULONG length, const void *value)
{
    for (UINT i = 0; i < b->prop_count; i++) {
        if (b->props[i].id == id)
            return nullptr;
    }
    GProperty *bigger = gp_realloc(b->props, sizeof(GProperty) * (b->prop_count + 1));
    if (!bigger)
        return nullptr;
    b->props = bigger;
    GProperty *p = &b->props[b->prop_count];
    p->id = id;
    p->type = type;
    p->length = length;
    p->value = length ? gp_alloc(length) : nullptr;
    if (length && !p->value)
        return nullptr;
    if (length)
        memcpy(p->value, value, length);
    b->prop_count++;
    return p;
}

/* exif value types. */
enum { T_BYTE = 1, T_ASCII = 2, T_SHORT = 3, T_LONG = 4, T_RATIONAL = 5, T_UNDEFINED = 7, T_SLONG = 9, T_SRATIONAL = 10 };

static void prop_from_variant(GBitmap *b, ULONG id, const PROPVARIANT *v)
{
    switch (v->vt) {
    case VT_UI1:
        prop_add(b, id, T_BYTE, 1, &v->bVal);
        break;
    case VT_LPSTR:
        if (v->pszVal)
            prop_add(b, id, T_ASCII, (ULONG)strlen(v->pszVal) + 1, v->pszVal);
        break;
    case VT_LPWSTR:
        if (v->pwszVal) {
            ULONG n = (ULONG)wcslen(v->pwszVal);
            char *a = gp_alloc(n + 1);
            if (a) {
                for (ULONG i = 0; i <= n; i++)
                    a[i] = (char)v->pwszVal[i];
                prop_add(b, id, T_ASCII, n + 1, a);
                gp_free(a);
            }
        }
        break;
    case VT_UI2:
        prop_add(b, id, T_SHORT, 2, &v->uiVal);
        break;
    case VT_UI4:
        prop_add(b, id, T_LONG, 4, &v->ulVal);
        break;
    case VT_UI8: {
        UINT32 r[2] = {v->uhVal.LowPart, v->uhVal.HighPart};
        prop_add(b, id, T_RATIONAL, 8, r);
        break;
    }
    case VT_I4:
        prop_add(b, id, T_SLONG, 4, &v->lVal);
        break;
    case VT_I8: {
        INT32 r[2] = {(INT32)v->hVal.LowPart, v->hVal.HighPart};
        prop_add(b, id, T_SRATIONAL, 8, r);
        break;
    }
    case VT_UI1 | VT_VECTOR:
        prop_add(b, id, T_UNDEFINED, v->caub.cElems, v->caub.pElems);
        break;
    case VT_BLOB:
        prop_add(b, id, T_UNDEFINED, v->blob.cbSize, v->blob.pBlobData);
        break;
    case VT_UI2 | VT_VECTOR:
        prop_add(b, id, T_SHORT, v->caui.cElems * 2, v->caui.pElems);
        break;
    case VT_UI4 | VT_VECTOR:
        prop_add(b, id, T_LONG, v->caul.cElems * 4, v->caul.pElems);
        break;
    case VT_UI8 | VT_VECTOR: {
        ULONG n = v->cauh.cElems;
        UINT32 *r = gp_alloc(n * 8);
        if (r) {
            for (ULONG i = 0; i < n; i++) {
                r[i * 2] = v->cauh.pElems[i].LowPart;
                r[i * 2 + 1] = (UINT32)v->cauh.pElems[i].HighPart;
            }
            prop_add(b, id, T_RATIONAL, n * 8, r);
            gp_free(r);
        }
        break;
    }
    default:
        break;
    }
}

/* nested ifds are flattened in. */
static void props_from_ifd(GBitmap *b, IWICMetadataReader *ifd, INT depth)
{
    if (depth > 4)
        return;
    UINT count = 0;
    if (FAILED(ifd->lpVtbl->GetCount(ifd, &count)))
        return;
    for (UINT i = 0; i < count; i++) {
        PROPVARIANT id, value;
        memset(&id, 0, sizeof(id));
        memset(&value, 0, sizeof(value));
        if (SUCCEEDED(ifd->lpVtbl->GetValueByIndex(ifd, i, nullptr, &id, &value)) && id.vt == VT_UI2) {
            if (value.vt == VT_UNKNOWN && value.punkVal) {
                IWICMetadataReader *sub = nullptr;
                if (SUCCEEDED(value.punkVal->lpVtbl->QueryInterface(value.punkVal, &IID_IWICMetadataReader,
                                                                   (void **)&sub))) {
                    props_from_ifd(b, sub, depth + 1);
                    sub->lpVtbl->Release(sub);
                }
            } else {
                prop_from_variant(b, id.uiVal, &value);
            }
        }
        PropVariantClear(&id);
        PropVariantClear(&value);
    }
}

static void props_from_blocks(GBitmap *b, IWICBitmapFrameDecode *frame, const GUID *container)
{
    IWICMetadataBlockReader *blocks = nullptr;
    if (FAILED(frame->lpVtbl->QueryInterface(frame, &IID_IWICMetadataBlockReader, (void **)&blocks)))
        return;
    UINT n = 0;
    blocks->lpVtbl->GetCount(blocks, &n);
    for (UINT i = 0; i < n; i++) {
        IWICMetadataReader *r = nullptr;
        if (FAILED(blocks->lpVtbl->GetReaderByIndex(blocks, i, &r)))
            continue;
        GUID f;
        if (SUCCEEDED(r->lpVtbl->GetMetadataFormat(r, &f))) {
            if (guid_equal(&f, &GUID_MetadataFormatApp1)) {
                /* the exif block holds the ifd. */
                UINT items = 0;
                r->lpVtbl->GetCount(r, &items);
                for (UINT k = 0; k < items; k++) {
                    PROPVARIANT v;
                    memset(&v, 0, sizeof(v));
                    if (SUCCEEDED(r->lpVtbl->GetValueByIndex(r, k, nullptr, nullptr, &v)) && v.vt == VT_UNKNOWN &&
                        v.punkVal) {
                        IWICMetadataReader *ifd = nullptr;
                        if (SUCCEEDED(v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IWICMetadataReader,
                                                                       (void **)&ifd))) {
                            props_from_ifd(b, ifd, 0);
                            ifd->lpVtbl->Release(ifd);
                        }
                    }
                    PropVariantClear(&v);
                }
            } else if (guid_equal(&f, &GUID_MetadataFormatIfd)) {
                props_from_ifd(b, r, 0);
            } else if (guid_equal(&f, &GUID_MetadataFormatChunkgAMA)) {
                PROPVARIANT id, v;
                memset(&id, 0, sizeof(id));
                memset(&v, 0, sizeof(v));
                if (SUCCEEDED(r->lpVtbl->GetValueByIndex(r, 0, nullptr, &id, &v)) && v.vt == VT_UI4) {
                    /* gamma as a rational, in hundred thousandths. */
                    UINT32 q[2] = {v.ulVal, 100000};
                    prop_add(b, 0x0301, T_RATIONAL, 8, q);
                }
                PropVariantClear(&id);
                PropVariantClear(&v);
            } else if (guid_equal(&f, &GUID_MetadataFormatChunktEXt)) {
                PROPVARIANT id, v;
                memset(&id, 0, sizeof(id));
                memset(&v, 0, sizeof(v));
                if (SUCCEEDED(r->lpVtbl->GetValueByIndex(r, 0, nullptr, &id, &v)) && id.vt == VT_LPSTR && id.pszVal &&
                    v.vt == VT_LPSTR) {
                    static const struct { const char *key; ULONG tag; } keys[] = {
                        {"Title", 0x0320},       {"Author", 0x013b},     {"Description", 0x010e},
                        {"Copyright", 0x8298},   {"Software", 0x0131},   {"Creation Time", 0x0132},
                        {"Source", 0x010f},      {"Comment", 0x9286},
                    };
                    for (UINT k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
                        if (same_text_ci(id.pszVal, keys[k].key))
                            prop_from_variant(b, keys[k].tag, &v);
                    }
                }
                PropVariantClear(&id);
                PropVariantClear(&v);
            }
        }
        r->lpVtbl->Release(r);
    }
    blocks->lpVtbl->Release(blocks);
}

/* ------------------------------------------------------------------ gif canvas */

static UINT query_u(IWICMetadataQueryReader *q, const WCHAR *name, UINT fallback)
{
    PROPVARIANT v;
    memset(&v, 0, sizeof(v));
    UINT r = fallback;
    if (q && SUCCEEDED(q->lpVtbl->GetMetadataByName(q, name, &v))) {
        switch (v.vt) {
        case VT_UI1:
            r = v.bVal;
            break;
        case VT_UI2:
            r = v.uiVal;
            break;
        case VT_UI4:
            r = v.ulVal;
            break;
        case VT_BOOL:
            r = v.boolVal != 0;
            break;
        default:
            break;
        }
    }
    PropVariantClear(&v);
    return r;
}

static GpStatus gif_setup(WicSession *w, GFrameSource *s)
{
    IWICMetadataQueryReader *q = nullptr;
    s->gif = TRUE;
    s->transparent = -1;
    s->composed = -1;
    s->delays = gp_alloc_zero(sizeof(UINT) * (s->count + 1));
    if (!s->delays)
        return OutOfMemory;
    if (SUCCEEDED(s->decoder->lpVtbl->GetMetadataQueryReader(s->decoder, &q))) {
        s->screen_w = query_u(q, L"/logscrdesc/Width", 0);
        s->screen_h = query_u(q, L"/logscrdesc/Height", 0);
        s->background = query_u(q, L"/logscrdesc/BackgroundColorIndex", 0);
        PROPVARIANT v;
        memset(&v, 0, sizeof(v));
        if (SUCCEEDED(q->lpVtbl->GetMetadataByName(q, L"/appext/Data", &v)) && v.vt == (VT_UI1 | VT_VECTOR) &&
            v.caub.cElems >= 3 && v.caub.pElems[0] == 1) {
            s->has_loops = TRUE;
            s->loops = v.caub.pElems[1] | (UINT)v.caub.pElems[2] << 8;
        }
        PropVariantClear(&v);
        q->lpVtbl->Release(q);
    }
    IWICPalette *pal = nullptr;
    if (SUCCEEDED(w->factory->lpVtbl->CreatePalette(w->factory, &pal))) {
        if (SUCCEEDED(s->decoder->lpVtbl->CopyPalette(s->decoder, pal))) {
            UINT n = 0;
            pal->lpVtbl->GetColorCount(pal, &n);
            if (n > 256)
                n = 256;
            UINT got = 0;
            pal->lpVtbl->GetColors(pal, n, s->palette, &got);
            s->palette_count = got;
        }
        pal->lpVtbl->Release(pal);
    }
    /* a file with no global color table uses the colors of its first frame. */
    if (!s->palette_count && s->count) {
        IWICBitmapFrameDecode *first = nullptr;
        if (SUCCEEDED(s->decoder->lpVtbl->GetFrame(s->decoder, 0, &first))) {
            IWICPalette *fp = nullptr;
            if (SUCCEEDED(w->factory->lpVtbl->CreatePalette(w->factory, &fp))) {
                UINT n = 0;
                if (SUCCEEDED(first->lpVtbl->CopyPalette(first, fp)) && SUCCEEDED(fp->lpVtbl->GetColorCount(fp, &n))) {
                    if (n > 256)
                        n = 256;
                    UINT got = 0;
                    fp->lpVtbl->GetColors(fp, n, s->palette, &got);
                    s->palette_count = got;
                }
                fp->lpVtbl->Release(fp);
            }
            first->lpVtbl->Release(first);
        }
    }
    for (UINT i = 0; i < s->count; i++) {
        IWICBitmapFrameDecode *frame = nullptr;
        if (FAILED(s->decoder->lpVtbl->GetFrame(s->decoder, i, &frame)))
            continue;
        IWICMetadataQueryReader *fq = nullptr;
        if (SUCCEEDED(frame->lpVtbl->GetMetadataQueryReader(frame, &fq))) {
            s->delays[i] = query_u(fq, L"/grctlext/Delay", 0);
            if (s->transparent < 0 && query_u(fq, L"/grctlext/TransparencyFlag", 0))
                s->transparent = (INT)query_u(fq, L"/grctlext/TransparentColorIndex", 0);
            if (!s->screen_w || !s->screen_h) {
                UINT w = 0, h = 0;
                frame->lpVtbl->GetSize(frame, &w, &h);
                UINT l = query_u(fq, L"/imgdesc/Left", 0), t = query_u(fq, L"/imgdesc/Top", 0);
                if (l + w > s->screen_w)
                    s->screen_w = l + w;
                if (t + h > s->screen_h)
                    s->screen_h = t + h;
            }
            fq->lpVtbl->Release(fq);
        }
        frame->lpVtbl->Release(frame);
    }
    if (s->transparent >= 0 && (UINT)s->transparent < 256) {
        if ((UINT)s->transparent >= s->palette_count) {
            for (UINT i = s->palette_count; i <= (UINT)s->transparent; i++)
                s->palette[i] = 0xff000000;
            s->palette_count = (UINT)s->transparent + 1;
        }
        s->palette[s->transparent] &= 0x00ffffffu;
    }
    if (s->screen_w == 0 || s->screen_h == 0)
        return GenericError;
    s->canvas = gp_alloc((SIZE_T)s->screen_w * s->screen_h);
    s->saved = gp_alloc((SIZE_T)s->screen_w * s->screen_h);
    return s->canvas && s->saved ? Ok : OutOfMemory;
}

static void gif_clear(GFrameSource *s, UINT l, UINT t, UINT w, UINT h)
{
    BYTE fill = s->transparent >= 0 ? (BYTE)s->transparent : (BYTE)s->background;
    for (UINT y = t; y < t + h && y < s->screen_h; y++) {
        for (UINT x = l; x < l + w && x < s->screen_w; x++)
            s->canvas[(SIZE_T)y * s->screen_w + x] = fill;
    }
}

static GpStatus gif_draw_frame(WicSession *w, GFrameSource *s, UINT index, UINT *l, UINT *t, UINT *fw, UINT *fh,
                               UINT *disposal)
{
    IWICBitmapFrameDecode *frame = nullptr;
    if (FAILED(s->decoder->lpVtbl->GetFrame(s->decoder, index, &frame)))
        return GenericError;
    GpStatus st = Ok;
    IWICMetadataQueryReader *fq = nullptr;
    frame->lpVtbl->GetMetadataQueryReader(frame, &fq);
    *l = query_u(fq, L"/imgdesc/Left", 0);
    *t = query_u(fq, L"/imgdesc/Top", 0);
    *disposal = query_u(fq, L"/grctlext/Disposal", 0);
    INT tr = query_u(fq, L"/grctlext/TransparencyFlag", 0) ? (INT)query_u(fq, L"/grctlext/TransparentColorIndex", 0) : -1;
    if (fq)
        fq->lpVtbl->Release(fq);
    frame->lpVtbl->GetSize(frame, fw, fh);
    BYTE *row = gp_alloc(*fw ? *fw : 1);
    if (!row) {
        frame->lpVtbl->Release(frame);
        return OutOfMemory;
    }
    /* when the frame has its own palette, each index is looked up in the canvas palette. */
    WICColor local[256];
    UINT local_count = 0;
    BOOL same = TRUE;
    IWICPalette *pal = nullptr;
    if (SUCCEEDED(w->factory->lpVtbl->CreatePalette(w->factory, &pal))) {
        if (SUCCEEDED(frame->lpVtbl->CopyPalette(frame, pal))) {
            UINT n = 0;
            pal->lpVtbl->GetColorCount(pal, &n);
            if (n > 256)
                n = 256;
            pal->lpVtbl->GetColors(pal, n, local, &local_count);
            for (UINT i = 0; i < local_count && i < s->palette_count; i++) {
                if ((local[i] & 0x00ffffffu) != (s->palette[i] & 0x00ffffffu)) {
                    same = FALSE;
                    break;
                }
            }
        }
        pal->lpVtbl->Release(pal);
    }
    for (UINT y = 0; y < *fh && st == Ok; y++) {
        WICRect rc = {0, (INT)y, (INT)*fw, 1};
        if (FAILED(frame->lpVtbl->CopyPixels(frame, &rc, *fw, *fw, row))) {
            st = GenericError;
            break;
        }
        UINT cy = *t + y;
        if (cy >= s->screen_h)
            continue;
        for (UINT x = 0; x < *fw; x++) {
            UINT cx = *l + x;
            if (cx >= s->screen_w)
                continue;
            BYTE v = row[x];
            if (tr >= 0 && v == tr)
                continue;
            if (!same && v < local_count) {
                UINT best = v, best_d = ~0u;
                for (UINT k = 0; k < s->palette_count; k++) {
                    INT dr = (INT)((local[v] >> 16) & 255) - (INT)((s->palette[k] >> 16) & 255);
                    INT dg = (INT)((local[v] >> 8) & 255) - (INT)((s->palette[k] >> 8) & 255);
                    INT db = (INT)(local[v] & 255) - (INT)(s->palette[k] & 255);
                    UINT d = (UINT)(dr * dr + dg * dg + db * db);
                    if (d < best_d) {
                        best_d = d;
                        best = k;
                    }
                }
                v = (BYTE)best;
            }
            s->canvas[(SIZE_T)cy * s->screen_w + cx] = v;
        }
    }
    gp_free(row);
    frame->lpVtbl->Release(frame);
    return st;
}

/* recomposes from the start when the frame is not the next one. */
static GpStatus gif_compose(WicSession *w, GFrameSource *s, UINT index)
{
    UINT l = 0, t = 0, fw = 0, fh = 0, disposal = 0;
    INT first = s->composed + 1;
    if (s->composed < 0 || (INT)index <= s->composed) {
        gif_clear(s, 0, 0, s->screen_w, s->screen_h);
        first = 0;
        s->prev_disposal = 0;
    }
    for (UINT i = (UINT)first; i <= index; i++) {
        if (i > 0 || first > 0) {
            if (s->prev_disposal == 2)
                gif_clear(s, s->prev_l, s->prev_t, s->prev_w, s->prev_h);
            else if (s->prev_disposal == 3)
                memcpy(s->canvas, s->saved, (SIZE_T)s->screen_w * s->screen_h);
        }
        /* the frame may need the canvas restored after it. */
        memcpy(s->saved, s->canvas, (SIZE_T)s->screen_w * s->screen_h);
        GpStatus st = gif_draw_frame(w, s, i, &l, &t, &fw, &fh, &disposal);
        if (st != Ok) {
            s->composed = -1;
            return st;
        }
        s->prev_l = l;
        s->prev_t = t;
        s->prev_w = fw;
        s->prev_h = fh;
        s->prev_disposal = disposal;
        s->composed = (INT)i;
    }
    return Ok;
}

/* ------------------------------------------------------------------ one frame into a bitmap */

static void set_flags(GBitmap *b, const GUID *container, BOOL cmyk, BOOL gray)
{
    UINT f = ImageFlagsReadOnly | ImageFlagsHasRealDPI | ImageFlagsHasRealPixelSize;
    if (guid_equal(container, &GUID_ContainerFormatJpeg))
        f |= cmyk ? ImageFlagsColorSpaceCMYK : (gray ? ImageFlagsColorSpaceGRAY : ImageFlagsColorSpaceYCBCR);
    else
        f |= ImageFlagsColorSpaceRGB;
    if (guid_equal(container, &GUID_ContainerFormatGif) || guid_equal(container, &GUID_ContainerFormatPng) ||
        guid_equal(container, &GUID_ContainerFormatTiff) || guid_equal(container, &GUID_ContainerFormatIco))
        f |= 0;
    BOOL alpha = IsAlphaPixelFormat(b->format);
    BOOL translucent = FALSE;
    if (IsIndexedPixelFormat(b->format) && b->palette) {
        for (UINT i = 0; i < b->palette->Count; i++) {
            UINT a = b->palette->Entries[i] >> 24;
            if (a != 255)
                alpha = TRUE;
            if (a != 255 && a != 0)
                translucent = TRUE;
        }
    } else if (alpha) {
        UINT32 *row = gp_alloc(sizeof(UINT32) * b->width);
        if (row) {
            for (INT y = 0; y < b->height && !translucent; y++) {
                gp_bitmap_read_argb(b, 0, y, b->width, row);
                for (INT x = 0; x < b->width; x++) {
                    UINT a = row[x] >> 24;
                    if (a != 255 && a != 0) {
                        translucent = TRUE;
                        break;
                    }
                }
            }
            gp_free(row);
        }
    }
    if (alpha)
        f |= ImageFlagsHasAlpha;
    if (translucent)
        f |= ImageFlagsHasTranslucent;
    b->flags = f;
}

static void props_clear(GBitmap *b)
{
    for (UINT i = 0; i < b->prop_count; i++)
        gp_free(b->props[i].value);
    gp_free(b->props);
    b->props = nullptr;
    b->prop_count = 0;
}

static GpStatus decode_frame(WicSession *w, GFrameSource *s, UINT index, GBitmap **out)
{
    IWICBitmapFrameDecode *frame = nullptr;
    if (FAILED(s->decoder->lpVtbl->GetFrame(s->decoder, index, &frame)))
        return InvalidParameter;
    GpStatus st = Ok;
    GBitmap *b = nullptr;
    UINT width = 0, height = 0;
    GUID wic_format;
    frame->lpVtbl->GetSize(frame, &width, &height);
    frame->lpVtbl->GetPixelFormat(frame, &wic_format);
    double dx = 96.0, dy = 96.0;
    frame->lpVtbl->GetResolution(frame, &dx, &dy);
    if (s->gif) {
        st = gif_compose(w, s, index);
        if (st != Ok) {
            frame->lpVtbl->Release(frame);
            return st;
        }
        st = gp_bitmap_create((INT)s->screen_w, (INT)s->screen_h, PixelFormat8bppIndexed, &b);
        if (st == Ok) {
            for (UINT y = 0; y < s->screen_h; y++)
                memcpy(b->bits + (SSIZE_T)y * b->stride, s->canvas + (SIZE_T)y * s->screen_w, s->screen_w);
            gp_free(b->palette);
            ColorPalette *p = gp_alloc_zero(sizeof(ColorPalette) + sizeof(ARGB) * 256);
            if (p) {
                p->Count = 256;
                for (UINT i = 0; i < 256; i++)
                    p->Entries[i] = i < s->palette_count ? s->palette[i] : 0xff000000u;
            }
            b->palette = p;
        }
    } else {
        const GUID *target;
        PixelFormat format = gp_format_of(&wic_format, &target);
        IWICBitmapSource *source = (IWICBitmapSource *)frame;
        IWICFormatConverter *conv = nullptr;
        BOOL two_bits = guid_equal(&wic_format, &GUID_WICPixelFormat2bppIndexed);
        if (!guid_equal(&wic_format, target) && !two_bits) {
            if (SUCCEEDED(w->factory->lpVtbl->CreateFormatConverter(w->factory, &conv)) &&
                SUCCEEDED(conv->lpVtbl->Initialize(conv, (IWICBitmapSource *)frame, target, WICBitmapDitherTypeNone,
                                                   nullptr, 0.0, WICBitmapPaletteTypeCustom)))
                source = (IWICBitmapSource *)conv;
            else
                st = UnknownImageFormat;
        }
        if (st == Ok)
            st = gp_bitmap_create((INT)width, (INT)height, format, &b);
        if (st == Ok) {
            UINT stride = (UINT)b->stride;
            HRESULT hr = source->lpVtbl->CopyPixels(source, nullptr, stride, stride * height, b->bits);
            /* the indices keep their values in four bits, from the end of each row as it grows. */
            for (UINT y = 0; SUCCEEDED(hr) && two_bits && y < height; y++) {
                BYTE *row = b->bits + (SIZE_T)y * stride;
                for (UINT x = width; x-- > 0;) {
                    BYTE index = (row[x / 4] >> (6 - 2 * (x % 4))) & 3;
                    row[x / 2] = x % 2 ? (BYTE)((row[x / 2] & 0xf0) | index) : (BYTE)((row[x / 2] & 0x0f) | index << 4);
                }
            }
            if (FAILED(hr))
                st = hr == E_OUTOFMEMORY ? OutOfMemory : (hr == WINCODEC_ERR_BADIMAGE || hr == WINCODEC_ERR_BADHEADER ? InvalidParameter : GenericError);
        }
        if (st == Ok && IsIndexedPixelFormat(format)) {
            ColorPalette *p;
            UINT bits = GetPixelFormatSize(format);
            if (guid_equal(&wic_format, &GUID_WICPixelFormatBlackWhite)) {
                p = gp_palette_default(PixelFormat1bppIndexed);
            } else if (guid_equal(&wic_format, &GUID_WICPixelFormat2bppGray) ||
                       guid_equal(&wic_format, &GUID_WICPixelFormat4bppGray) ||
                       guid_equal(&wic_format, &GUID_WICPixelFormat8bppGray)) {
                p = gray_palette(guid_equal(&wic_format, &GUID_WICPixelFormat8bppGray) ? 8 : 4);
                if (p && guid_equal(&wic_format, &GUID_WICPixelFormat2bppGray)) {
                    /* 2 bpp gray in a 4 bpp picture: the four levels spread over the sixteen entries. */
                }
            } else {
                p = palette_from_wic(w->factory, (IWICBitmapSource *)frame, bits);
            }
            if (!p)
                st = OutOfMemory;
            else {
                gp_free(b->palette);
                b->palette = p;
            }
        }
        if (conv)
            conv->lpVtbl->Release(conv);
    }
    frame->lpVtbl->Release(frame);
    if (st != Ok) {
        gp_bitmap_free(b);
        return st;
    }
    b->dpi_x = (float)dx;
    b->dpi_y = (float)dy;
    b->raw_format = *raw_format_of(&s->container);
    b->frame_index = index;
    BOOL cmyk = guid_equal(&wic_format, &GUID_WICPixelFormat32bppCMYK);
    BOOL gray = guid_equal(&wic_format, &GUID_WICPixelFormat8bppGray);
    set_flags(b, &s->container, cmyk, gray);
    *out = b;
    return Ok;
}

static void load_props(WicSession *w, GFrameSource *s, UINT index, GBitmap *b)
{
    IWICBitmapFrameDecode *frame = nullptr;
    if (FAILED(s->decoder->lpVtbl->GetFrame(s->decoder, index, &frame)))
        return;
    props_from_blocks(b, frame, &s->container);
    frame->lpVtbl->Release(frame);
    if (s->gif) {
        prop_add(b, 0x5100, T_LONG, 4 * s->count, s->delays);
        if (s->has_loops) {
            USHORT loops = (USHORT)s->loops;
            prop_add(b, 0x5101, T_SHORT, 2, &loops);
        }
        if (s->transparent >= 0) {
            BYTE t = (BYTE)s->transparent;
            prop_add(b, 0x5104, T_BYTE, 1, &t);
        }
        BYTE bg = (BYTE)s->background;
        prop_add(b, 0x5103, T_BYTE, 1, &bg);
    }
}

/* ------------------------------------------------------------------ opening */

static BOOL metafile_signature(const BYTE *d, SIZE_T n)
{
    if (n >= 44 && d[0] == 1 && d[1] == 0 && d[2] == 0 && d[3] == 0 && d[40] == ' ' && d[41] == 'E' && d[42] == 'M' &&
        d[43] == 'F')
        return TRUE;
    if (n >= 4 && d[0] == 0xd7 && d[1] == 0xcd && d[2] == 0xc6 && d[3] == 0x9a)
        return TRUE;
    return FALSE;
}

/* the source takes ownership of the bytes. */
static GpStatus open_source(WicSession *w, BYTE *data, SIZE_T size, GFrameSource **out)
{
    GFrameSource *s = gp_alloc_zero(sizeof(*s));
    if (!s) {
        gp_free(data);
        return OutOfMemory;
    }
    s->data = data;
    s->size = size;
    s->composed = -1;
    IWICStream *stream = nullptr;
    GpStatus st = UnknownImageFormat;
    if (SUCCEEDED(w->factory->lpVtbl->CreateStream(w->factory, &stream))) {
        if (SUCCEEDED(stream->lpVtbl->InitializeFromMemory(stream, data, (DWORD)size))) {
            HRESULT hr = w->factory->lpVtbl->CreateDecoderFromStream(w->factory, (IStream *)stream, nullptr,
                                                                     WICDecodeMetadataCacheOnDemand, &s->decoder);
            if (SUCCEEDED(hr))
                st = Ok;
            else if (hr == E_OUTOFMEMORY)
                st = OutOfMemory;
        }
        stream->lpVtbl->Release(stream);
    }
    if (st == Ok) {
        s->decoder->lpVtbl->GetContainerFormat(s->decoder, &s->container);
        s->decoder->lpVtbl->GetFrameCount(s->decoder, &s->count);
        if (!s->count)
            st = UnknownImageFormat;
        if (st == Ok) {
            if (guid_equal(&s->container, &GUID_ContainerFormatGif)) {
                s->dimension = &FrameDimensionTime;
                st = gif_setup(w, s);
            } else if (guid_equal(&s->container, &GUID_ContainerFormatTiff)) {
                s->dimension = &FrameDimensionPage;
            } else if (guid_equal(&s->container, &GUID_ContainerFormatIco)) {
                s->dimension = &FrameDimensionResolution;
            } else {
                s->dimension = &FrameDimensionPage;
            }
        }
    }
    if (st != Ok) {
        gp_frame_source_free(s);
        return st;
    }
    *out = s;
    return Ok;
}

/* for a clone: the first frame is not decoded, the bitmap's frame is selected instead. */
GpStatus gp_source_reopen(const BYTE *data, SIZE_T size, GBitmap *dst, GBitmap **unused)
{
    WicSession w;
    GpStatus st = wic_open(&w);
    if (st != Ok)
        return st;
    BYTE *copy = gp_dup(data, size);
    if (!copy) {
        wic_close(&w);
        return OutOfMemory;
    }
    GFrameSource *s;
    st = open_source(&w, copy, size, &s);
    wic_close(&w);
    if (st == Ok)
        dst->source = s;
    return st;
}

GpStatus gp_decode_bytes(BYTE *data, SIZE_T size, GImage **image)
{
    if (metafile_signature(data, size)) {
        GpStatus st = gp_metafile_from_bytes(data, size, image);
        gp_free(data);
        return st;
    }
    WicSession w;
    GpStatus st = wic_open(&w);
    if (st != Ok) {
        gp_free(data);
        return st;
    }
    GFrameSource *s;
    st = open_source(&w, data, size, &s);
    if (st != Ok) {
        wic_close(&w);
        return st;
    }
    GBitmap *b;
    st = decode_frame(&w, s, 0, &b);
    if (st == Ok) {
        load_props(&w, s, 0, b);
        b->source = s;
        *image = &b->image;
    } else {
        gp_frame_source_free(s);
    }
    wic_close(&w);
    return st;
}

#define GP_MAX_ENCODED 0x10000000u

GpStatus gp_read_file(const WCHAR *file, BYTE **data, SIZE_T *size)
{
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? FileNotFound
               : (error == ERROR_ACCESS_DENIED ? AccessDenied : Win32Error);
    }
    LARGE_INTEGER sz;
    GpStatus st = Ok;
    BYTE *buf = nullptr;
    if (!GetFileSizeEx(h, &sz))
        st = Win32Error;
    else if (sz.QuadPart > GP_MAX_ENCODED)
        st = OutOfMemory;
    else if (!(buf = gp_alloc((SIZE_T)sz.QuadPart + 1)))
        st = OutOfMemory;
    else {
        DWORD total = 0, want = (DWORD)sz.QuadPart;
        while (total < want) {
            DWORD got = 0;
            if (!ReadFile(h, buf + total, want - total, &got, nullptr) || !got)
                break;
            total += got;
        }
        if (total != want)
            st = Win32Error;
    }
    CloseHandle(h);
    if (st != Ok) {
        gp_free(buf);
        return st;
    }
    *data = buf;
    *size = (SIZE_T)sz.QuadPart;
    return Ok;
}

GpStatus gp_read_stream(IStream *stream, BYTE **data, SIZE_T *size)
{
    SIZE_T capacity = 0x10000, n = 0;
    BYTE *buf = gp_alloc(capacity);
    if (!buf)
        return OutOfMemory;
    for (;;) {
        if (n == capacity) {
            if (capacity >= GP_MAX_ENCODED) {
                gp_free(buf);
                return OutOfMemory;
            }
            BYTE *bigger = gp_realloc(buf, capacity * 2);
            if (!bigger) {
                gp_free(buf);
                return OutOfMemory;
            }
            buf = bigger;
            capacity *= 2;
        }
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, buf + n, (ULONG)(capacity - n), &got);
        if (FAILED(hr)) {
            gp_free(buf);
            return GenericError;
        }
        if (got > capacity - n)
            got = (ULONG)(capacity - n);
        n += got;
        if (!got || hr != S_OK)
            break;
    }
    *data = buf;
    *size = n;
    return Ok;
}

static GpStatus load_file(const WCHAR *file, GImage **image)
{
    BYTE *data;
    SIZE_T size;
    GpStatus st = gp_read_file(file, &data, &size);
    if (st != Ok)
        return st;
    return gp_decode_bytes(data, size, image);
}

static GpStatus load_stream(IStream *stream, GImage **image)
{
    BYTE *data;
    SIZE_T size;
    GpStatus st = gp_read_stream(stream, &data, &size);
    if (st != Ok)
        return st;
    return gp_decode_bytes(data, size, image);
}

static GpStatus bitmap_only(GImage *image, GpBitmap **bitmap)
{
    /* a bitmap constructor makes a bitmap of any picture. */
    if (image->tag == GP_TAG_BITMAP) {
        *bitmap = image;
        return Ok;
    }
    GpStatus st = gp_metafile_to_bitmap(image, (GBitmap **)bitmap);
    GdipDisposeImage(image);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFile(const WCHAR *file, GpImage **image)
{
    GP_REQUIRE_INIT();
    if (!file || !image)
        return InvalidParameter;
    GImage *i;
    GpStatus st = load_file(file, &i);
    if (st == Ok)
        *image = i;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipLoadImageFromFileICM(const WCHAR *file, GpImage **image)
{
    GP_REQUIRE_INIT();
    return GdipLoadImageFromFile(file, image);
}

WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStream(IStream *stream, GpImage **image)
{
    GP_REQUIRE_INIT();
    if (!stream || !image)
        return InvalidParameter;
    GImage *i;
    GpStatus st = load_stream(stream, &i);
    if (st == Ok)
        *image = i;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipLoadImageFromStreamICM(IStream *stream, GpImage **image)
{
    GP_REQUIRE_INIT();
    return GdipLoadImageFromStream(stream, image);
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR *file, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!file || !bitmap)
        return InvalidParameter;
    GImage *i;
    GpStatus st = load_file(file, &i);
    if (st != Ok)
        return st;
    return bitmap_only(i, bitmap);
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR *file, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    return GdipCreateBitmapFromFile(file, bitmap);
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStream(IStream *stream, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    if (!stream || !bitmap)
        return InvalidParameter;
    GImage *i;
    GpStatus st = load_stream(stream, &i);
    if (st != Ok)
        return st;
    return bitmap_only(i, bitmap);
}

WINGDIPAPI GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream *stream, GpBitmap **bitmap)
{
    GP_REQUIRE_INIT();
    return GdipCreateBitmapFromStream(stream, bitmap);
}

/* ------------------------------------------------------------------ frames */

UINT gp_frame_count(const GBitmap *b) { return b->source ? b->source->count : 1; }
const GUID *gp_frame_dimension(const GBitmap *b) { return b->source ? b->source->dimension : &FrameDimensionPage; }

GpStatus gp_select_frame(GBitmap *b, UINT index)
{
    GFrameSource *s = b->source;
    if (!s) {
        return index == 0 ? Ok : InvalidParameter;
    }
    if (index >= s->count)
        return InvalidParameter;
    if (index == b->frame_index)
        return Ok;
    WicSession w;
    GpStatus st = wic_open(&w);
    if (st != Ok)
        return st;
    GBitmap *n;
    st = decode_frame(&w, s, index, &n);
    if (st == Ok) {
        /* the new picture replaces the old one in the same object. */
        load_props(&w, s, index, n);
        gp_free(b->owned);
        gp_free(b->palette);
        props_clear(b);
        b->owned = n->owned;
        b->bits = n->bits;
        b->stride = n->stride;
        b->width = n->width;
        b->height = n->height;
        b->format = n->format;
        b->palette = n->palette;
        b->props = n->props;
        b->prop_count = n->prop_count;
        b->dpi_x = n->dpi_x;
        b->dpi_y = n->dpi_y;
        b->flags = n->flags;
        b->frame_index = index;
        n->owned = nullptr;
        n->palette = nullptr;
        n->props = nullptr;
        n->prop_count = 0;
        gp_bitmap_free(n);
    }
    wic_close(&w);
    return st;
}
