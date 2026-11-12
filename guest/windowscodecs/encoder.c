/*
 * generic WIC encoder, frame and option-bag interfaces. frames collect pixels until commit, when the format backend
 * writes them.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"

#define MAX_OPTIONS 24
#define MAX_PROFILES 8

typedef struct WicEncoder WicEncoder;

typedef struct WicOptionBag {
    WicBase base;
    WicSlot bag;
    const WicOptionDesc *descs;
    UINT count;
    double values[MAX_OPTIONS];
    UINT set;
} WicOptionBag;

struct WicEncoder {
    WicBase base;
    WicSlot encoder;
    CRITICAL_SECTION lock;
    const WicCodec *codec;
    const WicEncoderOps *ops;
    void *state;
    IStream *stream;
    BOOL ready;
    BOOL committed;
    UINT frames;
    UINT frames_committed;
    BOOL frame_open;
    IWICPalette *palette;
    IWICBitmapSource *thumbnail;
    IWICMetadataBlockWriter *blocks;
};

typedef struct WicFrameEncoder {
    WicBase base;
    WicSlot frame;
    WicEncoder *encoder;
    UINT index;
    BOOL initialized;
    BOOL size_set;
    BOOL format_set;
    BOOL written;
    BOOL committed;
    UINT width;
    UINT height;
    GUID format;
    double dpi_x;
    double dpi_y;
    BYTE *pixels;
    UINT stride;
    UINT rows;
    IWICPalette *palette;
    IWICBitmapSource *thumbnail;
    IWICColorContext *profiles[MAX_PROFILES];
    UINT profile_count;
    double values[MAX_OPTIONS];
    UINT set;
    IWICMetadataBlockWriter *blocks;
} WicFrameEncoder;

static WicEncoder *encoder_from(IWICBitmapEncoder *This) { return WIC_FROM_SLOT(WicEncoder, encoder, This); }
static WicFrameEncoder *fenc_from(IWICBitmapFrameEncode *This) { return WIC_FROM_SLOT(WicFrameEncoder, frame, This); }
static WicOptionBag *bag_from(IPropertyBag2 *This) { return WIC_FROM_SLOT(WicOptionBag, bag, This); }

/* the options. */

static int find_option(const WicOptionBag *bag, const WCHAR *name)
{
    for (UINT i = 0; i < bag->count; i++) {
        if (!wcscmp(bag->descs[i].name, name))
            return (int)i;
    }
    return -1;
}

static HRESULT option_value(const WicOptionDesc *desc, const VARIANT *value, double *out)
{
    double v;
    if (value->vt != desc->type)
        return E_INVALIDARG;
    switch (desc->type) {
    case VT_R4:
        v = value->fltVal;
        break;
    case VT_BOOL:
        v = value->boolVal != VARIANT_FALSE;
        break;
    case VT_UI1:
        v = value->bVal;
        break;
    case VT_UI2:
        v = value->uiVal;
        break;
    default:
        v = value->lVal;
    }
    if (!(v >= desc->min && v <= desc->max))
        return WINCODEC_ERR_VALUEOUTOFRANGE;
    *out = v;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bag_read(IPropertyBag2 *This, ULONG count, PROPBAG2 *props, IErrorLog *log,
                                          VARIANT *values, HRESULT *errors)
{
    WicOptionBag *bag = bag_from(This);
    HRESULT result = S_OK;
    if (!count)
        return S_OK;
    if (!props || !values || !errors)
        return E_POINTER;
    for (ULONG i = 0; i < count; i++) {
        memset(&values[i], 0, sizeof(values[i]));
        errors[i] = S_OK;
        int k = props[i].pstrName ? find_option(bag, props[i].pstrName) : (int)props[i].dwHint;
        if (k < 0 || (UINT)k >= bag->count) {
            errors[i] = E_INVALIDARG;
            result = E_FAIL;
            continue;
        }
        const WicOptionDesc *desc = &bag->descs[k];
        values[i].vt = desc->type;
        switch (desc->type) {
        case VT_R4:
            values[i].fltVal = (float)bag->values[k];
            break;
        case VT_BOOL:
            values[i].boolVal = bag->values[k] != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case VT_UI1:
            values[i].bVal = (BYTE)bag->values[k];
            break;
        case VT_UI2:
            values[i].uiVal = (USHORT)bag->values[k];
            break;
        default:
            values[i].lVal = (LONG)bag->values[k];
        }
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE bag_write(IPropertyBag2 *This, ULONG count, PROPBAG2 *props, VARIANT *values)
{
    WicOptionBag *bag = bag_from(This);
    if (!count)
        return S_OK;
    if (!props || !values)
        return E_POINTER;
    for (ULONG i = 0; i < count; i++) {
        int k = props[i].pstrName ? find_option(bag, props[i].pstrName) : (int)props[i].dwHint;
        if (k < 0 || (UINT)k >= bag->count)
            return E_INVALIDARG;
        double v;
        HRESULT hr = option_value(&bag->descs[k], &values[i], &v);
        if (FAILED(hr))
            return hr;
        bag->values[k] = v;
        bag->set |= 1u << k;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bag_count(IPropertyBag2 *This, ULONG *count)
{
    if (!count)
        return E_POINTER;
    *count = bag_from(This)->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bag_info(IPropertyBag2 *This, ULONG index, ULONG count, PROPBAG2 *props,
                                          ULONG *actual)
{
    WicOptionBag *bag = bag_from(This);
    if (!props || !actual)
        return E_POINTER;
    if (index >= bag->count && !(index == 0 && !bag->count))
        return E_INVALIDARG;
    ULONG n = 0;
    for (ULONG i = index; i < bag->count && n < count; i++, n++) {
        memset(&props[n], 0, sizeof(props[n]));
        props[n].dwType = PROPBAG2_TYPE_DATA;
        props[n].vt = bag->descs[i].type;
        props[n].dwHint = i;
        SIZE_T length = wcslen(bag->descs[i].name) + 1;
        props[n].pstrName = CoTaskMemAlloc(length * sizeof(WCHAR));
        if (!props[n].pstrName)
            return E_OUTOFMEMORY;
        memcpy(props[n].pstrName, bag->descs[i].name, length * sizeof(WCHAR));
    }
    *actual = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE bag_load_object(IPropertyBag2 *This, LPCOLESTR name, DWORD hint, IUnknown *object,
                                                 IErrorLog *log)
{
    return E_NOTIMPL;
}

static const IPropertyBag2Vtbl g_bag_vtbl = {
    WIC_IUNKNOWN_FUNCS, bag_read, bag_write, bag_count, bag_info, bag_load_object,
};
static const WicIfaceEntry g_bag_ifaces[] = {WIC_ENTRY(WicOptionBag, IID_IPropertyBag2, bag)};
static const WicClass g_bag_class = {nullptr, g_bag_ifaces, 1};

static HRESULT bag_create(const WicOptionDesc *descs, UINT count, IPropertyBag2 **out)
{
    WicOptionBag *bag = WicNew(sizeof(*bag), &g_bag_class);
    if (!bag)
        return E_OUTOFMEMORY;
    WIC_SLOT(bag, bag, &g_bag_vtbl);
    bag->descs = descs;
    bag->count = count < MAX_OPTIONS ? count : MAX_OPTIONS;
    for (UINT i = 0; i < bag->count; i++)
        bag->values[i] = descs[i].def;
    *out = (IPropertyBag2 *)&bag->bag;
    return S_OK;
}

/* a frame. */

/* choose the closest supported codec format. */
static void closest_format(const WicCodec *codec, const WicEncoderOps *ops, GUID *format)
{
    for (UINT i = 0; i < codec->format_count; i++) {
        if (IsEqualGUID(codec->formats[i], format))
            return;
    }
    if (ops->closest_format && ops->closest_format(format))
        return;
    const WicFormat *want = WicFindFormat(format);
    const GUID *best = codec->formats[0];
    int best_score = 1 << 30;
    for (UINT i = 0; i < codec->format_count; i++) {
        const WicFormat *have = WicFindFormat(codec->formats[i]);
        int score = 0;
        if (want && have) {
            BOOL want_index = want->numeric == WICPixelFormatNumericRepresentationIndexed;
            BOOL have_index = have->numeric == WICPixelFormatNumericRepresentationIndexed;
            BOOL want_gray = want->channels == 1 && !want_index;
            BOOL have_gray = have->channels == 1 && !have_index;
            BOOL want_cmyk = want->channels == 4 && !want->alpha && (!wcscmp(want->name, L"32bpp CMYK") || !wcscmp(want->name, L"64bpp CMYK"));
            BOOL have_cmyk = !wcscmp(have->name, L"32bpp CMYK");
            if (want->alpha && !have->alpha)
                score += 100000;
            if (want_index != have_index)
                score += 50000;
            if (want_gray != have_gray)
                score += 20000;
            if (want_cmyk != have_cmyk)
                score += 40000;
            int bits_want = (int)want->bpp, bits_have = (int)have->bpp;
            if (have->alpha != want->alpha && !want->alpha)
                score += 300;
            if (bits_have < bits_want)
                score += (bits_want - bits_have) * 4;
            else
                score += bits_have - bits_want;
            /* prefer straight alpha when selecting between straight and premultiplied formats. */
            if (want->alpha && have->alpha && wcsstr(want->name, L"PBGRA") != nullptr && wcsstr(have->name, L"PBGRA") == nullptr)
                score += 1;
        } else {
            score = 1 << 29;
        }
        if (score < best_score) {
            best_score = score;
            best = codec->formats[i];
        }
    }
    *format = *best;
}

static void fenc_destroy(WicBase *base)
{
    WicFrameEncoder *f = (WicFrameEncoder *)base;
    WicFree(f->pixels);
    if (f->palette)
        f->palette->lpVtbl->Release(f->palette);
    if (f->thumbnail)
        f->thumbnail->lpVtbl->Release(f->thumbnail);
    for (UINT i = 0; i < f->profile_count; i++)
        f->profiles[i]->lpVtbl->Release(f->profiles[i]);
    if (f->blocks)
        f->blocks->lpVtbl->Release(f->blocks);
    if (f->encoder)
        WicRelease(&f->encoder->encoder);
}

static HRESULT STDMETHODCALLTYPE fenc_initialize(IWICBitmapFrameEncode *This, IPropertyBag2 *options)
{
    WicFrameEncoder *f = fenc_from(This);
    if (f->initialized)
        return WINCODEC_ERR_WRONGSTATE;
    if (options) {
        /* read our option bags directly. */
        WicOptionBag *bag = bag_from(options);
        const WicEncoderOps *ops = f->encoder->ops;
        if (bag->base.cls == &g_bag_class && bag->descs == ops->options) {
            memcpy(f->values, bag->values, sizeof(f->values));
            f->set = bag->set;
        } else {
            /* query other bags by property name. */
            for (UINT i = 0; i < ops->option_count && i < MAX_OPTIONS; i++) {
                PROPBAG2 prop;
                VARIANT value;
                HRESULT error = S_OK;
                memset(&prop, 0, sizeof(prop));
                prop.pstrName = (WCHAR *)ops->options[i].name;
                memset(&value, 0, sizeof(value));
                if (SUCCEEDED(options->lpVtbl->Read(options, 1, &prop, nullptr, &value, &error)) &&
                    value.vt == ops->options[i].type) {
                    double v;
                    HRESULT hr = option_value(&ops->options[i], &value, &v);
                    if (FAILED(hr))
                        return hr;
                    f->values[i] = v;
                    f->set |= 1u << i;
                }
            }
        }
    }
    f->initialized = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_size(IWICBitmapFrameEncode *This, UINT width, UINT height)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    if (!width || !height)
        return E_INVALIDARG;
    f->width = width;
    f->height = height;
    f->size_set = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_resolution(IWICBitmapFrameEncode *This, double dpi_x, double dpi_y)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    f->dpi_x = dpi_x;
    f->dpi_y = dpi_y;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_format(IWICBitmapFrameEncode *This, WICPixelFormatGUID *format)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!format)
        return E_INVALIDARG;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    closest_format(f->encoder->codec, f->encoder->ops, format);
    f->format = *format;
    f->format_set = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_contexts(IWICBitmapFrameEncode *This, UINT count, IWICColorContext **contexts)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    if (count && !contexts)
        return E_INVALIDARG;
    if (count > MAX_PROFILES)
        count = MAX_PROFILES;
    for (UINT i = 0; i < f->profile_count; i++)
        f->profiles[i]->lpVtbl->Release(f->profiles[i]);
    f->profile_count = 0;
    for (UINT i = 0; i < count; i++) {
        if (!contexts[i])
            return E_INVALIDARG;
        contexts[i]->lpVtbl->AddRef(contexts[i]);
        f->profiles[f->profile_count++] = contexts[i];
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_palette(IWICBitmapFrameEncode *This, IWICPalette *palette)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    IWICPalette *copy;
    HRESULT hr = WicPaletteCreate(&copy);
    if (FAILED(hr))
        return hr;
    hr = copy->lpVtbl->InitializeFromPalette(copy, palette);
    if (FAILED(hr)) {
        copy->lpVtbl->Release(copy);
        return hr;
    }
    if (f->palette)
        f->palette->lpVtbl->Release(f->palette);
    f->palette = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_set_thumbnail(IWICBitmapFrameEncode *This, IWICBitmapSource *thumbnail)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!thumbnail)
        return E_INVALIDARG;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->written)
        return WINCODEC_ERR_WRONGSTATE;
    if (!f->encoder->ops->container_thumbnail)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    thumbnail->lpVtbl->AddRef(thumbnail);
    if (f->thumbnail)
        f->thumbnail->lpVtbl->Release(f->thumbnail);
    f->thumbnail = thumbnail;
    return S_OK;
}

static HRESULT ensure_buffer(WicFrameEncoder *f)
{
    if (f->pixels)
        return S_OK;
    UINT row, total;
    if (!WicRowBytes(&f->format, f->width, &row) || !WicMul32(row, f->height, &total))
        return INTSAFE_E_ARITHMETIC_OVERFLOW;
    f->pixels = WicAllocZero(total ? total : 1);
    if (!f->pixels)
        return E_OUTOFMEMORY;
    f->stride = row;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_write_pixels(IWICBitmapFrameEncode *This, UINT lines, UINT stride, UINT size,
                                                   BYTE *pixels)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!f->size_set || !f->format_set)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->committed)
        return WINCODEC_ERR_WRONGSTATE;
    if (!lines)
        return E_INVALIDARG;
    if (!pixels)
        return E_INVALIDARG;
    if (lines > f->height - f->rows)
        return WINCODEC_ERR_CODECTOOMANYSCANLINES;
    WICRect r = {0, 0, (INT)f->width, (INT)lines};
    HRESULT hr = WicCheckBuffer(&r, &f->format, stride, size, pixels);
    if (FAILED(hr))
        return hr;
    hr = ensure_buffer(f);
    if (FAILED(hr))
        return hr;
    for (UINT y = 0; y < lines; y++)
        memcpy(f->pixels + (SIZE_T)(f->rows + y) * f->stride, pixels + (SIZE_T)y * stride, f->stride);
    f->rows += lines;
    f->written = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fenc_write_source(IWICBitmapFrameEncode *This, IWICBitmapSource *source,
                                                   WICRect *rect)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!source)
        return E_INVALIDARG;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->committed)
        return WINCODEC_ERR_WRONGSTATE;
    UINT sw, sh;
    GUID sformat;
    HRESULT hr = source->lpVtbl->GetSize(source, &sw, &sh);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetPixelFormat(source, &sformat);
    if (FAILED(hr))
        return hr;
    /* a rectangle without a size stands for the whole source, which is what WPF's encoders pass. */
    if (rect && !rect->Width && !rect->Height)
        rect = nullptr;
    WICRect r;
    hr = WicCheckRect(rect, sw, sh, &r);
    if (FAILED(hr))
        return hr;
    if (!r.Width || !r.Height)
        return E_INVALIDARG;
    /* a rectangle as wide as the frame adds its rows after those written, so a frame can be written in bands. */
    if (!f->size_set) {
        f->width = (UINT)r.Width;
        f->height = (UINT)r.Height;
        f->size_set = TRUE;
    } else if (f->width != (UINT)r.Width) {
        return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    } else if ((UINT)r.Height > f->height - f->rows) {
        return WINCODEC_ERR_CODECTOOMANYSCANLINES;
    }
    if (!f->format_set) {
        GUID want = sformat;
        closest_format(f->encoder->codec, f->encoder->ops, &want);
        f->format = want;
        f->format_set = TRUE;
    }
    if (f->dpi_x == 0.0 && f->dpi_y == 0.0) {
        double dx = 0, dy = 0;
        if (SUCCEEDED(source->lpVtbl->GetResolution(source, &dx, &dy))) {
            f->dpi_x = dx;
            f->dpi_y = dy;
        }
    }
    if (!f->palette && WicFormatIsIndexed(&f->format) && IsEqualGUID(&sformat, &f->format)) {
        IWICPalette *palette;
        if (SUCCEEDED(WicPaletteCreate(&palette))) {
            if (SUCCEEDED(source->lpVtbl->CopyPalette(source, palette)))
                f->palette = palette;
            else
                palette->lpVtbl->Release(palette);
        }
    }
    IWICBitmapSource *use = source;
    use->lpVtbl->AddRef(use);
    if (!IsEqualGUID(&sformat, &f->format)) {
        IWICBitmapSource *converted;
        if (WicFormatIsIndexed(&f->format) && f->palette) {
            IWICFormatConverter *converter;
            hr = WicFormatConverterCreate(&converter);
            if (SUCCEEDED(hr)) {
                hr = converter->lpVtbl->Initialize(converter, source, &f->format, WICBitmapDitherTypeNone, f->palette,
                                                   0.0, WICBitmapPaletteTypeCustom);
                converted = (IWICBitmapSource *)converter;
            }
        } else if (WicFormatIsIndexed(&f->format)) {
            /* generate a palette from the image, like the Windows GIF encoder. */
            IWICPalette *palette;
            hr = WicPaletteCreate(&palette);
            if (SUCCEEDED(hr)) {
                UINT colors = 1u << WicFormatBits(&f->format);
                hr = palette->lpVtbl->InitializeFromBitmap(palette, source, colors > 256 ? 256 : colors,
                                                           WicFormatHasAlpha(&sformat));
                if (SUCCEEDED(hr)) {
                    IWICFormatConverter *converter;
                    hr = WicFormatConverterCreate(&converter);
                    if (SUCCEEDED(hr)) {
                        hr = converter->lpVtbl->Initialize(converter, source, &f->format, WICBitmapDitherTypeNone,
                                                           palette, 0.0, WICBitmapPaletteTypeCustom);
                        converted = (IWICBitmapSource *)converter;
                    }
                }
                if (SUCCEEDED(hr))
                    f->palette = palette;
                else
                    palette->lpVtbl->Release(palette);
            }
        } else {
            hr = WicEnsureFormat(source, &f->format, &converted);
        }
        use->lpVtbl->Release(use);
        if (FAILED(hr))
            return hr;
        use = converted;
    }
    hr = ensure_buffer(f);
    if (SUCCEEDED(hr)) {
        UINT lines = (UINT)r.Height;
        hr = use->lpVtbl->CopyPixels(use, &r, f->stride, f->stride * lines, f->pixels + (SIZE_T)f->rows * f->stride);
        if (SUCCEEDED(hr)) {
            f->rows += lines;
            f->written = TRUE;
        }
    }
    use->lpVtbl->Release(use);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fenc_commit(IWICBitmapFrameEncode *This)
{
    WicFrameEncoder *f = fenc_from(This);
    WicEncoder *e = f->encoder;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (f->committed)
        return WINCODEC_ERR_WRONGSTATE;
    if (!f->size_set || !f->format_set || f->rows != f->height)
        return WINCODEC_ERR_FRAMEMISSING;
    if (WicFormatIsIndexed(&f->format) && !f->palette)
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    WicEncFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = f->width;
    frame.height = f->height;
    frame.format = f->format;
    frame.dpi_x = f->dpi_x;
    frame.dpi_y = f->dpi_y;
    frame.pixels = f->pixels;
    frame.stride = f->stride;
    frame.options = f->values;
    frame.options_set = f->set;
    frame.thumbnail = f->thumbnail;
    frame.profiles = f->profiles;
    frame.profile_count = f->profile_count;
    if (f->blocks)
        f->blocks->lpVtbl->QueryInterface(f->blocks, &IID_IWICMetadataBlockReader, (void **)&frame.metadata);
    if (f->palette) {
        UINT got = 0;
        f->palette->lpVtbl->GetColors(f->palette, 256, frame.palette, &got);
        frame.palette_count = got;
    }
    EnterCriticalSection(&e->lock);
    HRESULT hr = e->ops->write_frame(e->state, f->index, &frame);
    if (SUCCEEDED(hr)) {
        f->committed = TRUE;
        e->frames_committed++;
        e->frame_open = FALSE;
    }
    LeaveCriticalSection(&e->lock);
    if (frame.metadata)
        frame.metadata->lpVtbl->Release(frame.metadata);
    return hr;
}

/* a new tiff or jpeg xr frame has its root ifd and a jpeg one its jfif block, as on windows. */
static HRESULT fenc_blocks(WicFrameEncoder *f)
{
    if (f->blocks)
        return S_OK;
    const GUID *container = f->encoder->codec->container;
    HRESULT hr = WicBlockWriterCreate(container, &f->blocks);
    if (FAILED(hr))
        return hr;
    BOOL ifd = IsEqualGUID(container, &GUID_ContainerFormatTiff) || IsEqualGUID(container, &GUID_ContainerFormatWmp);
    const GUID *root = ifd                                                 ? &GUID_MetadataFormatIfd
                       : IsEqualGUID(container, &GUID_ContainerFormatJpeg) ? &GUID_MetadataFormatApp0
                                                                           : nullptr;
    if (!root)
        return S_OK;
    IWICMetadataWriter *w;
    hr = WicMetaHandlerCreate(root, &w);
    if (SUCCEEDED(hr)) {
        hr = f->blocks->lpVtbl->AddWriter(f->blocks, w);
        w->lpVtbl->Release(w);
    }
    if (FAILED(hr)) {
        f->blocks->lpVtbl->Release(f->blocks);
        f->blocks = nullptr;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE fenc_get_metadata_writer(IWICBitmapFrameEncode *This, IWICMetadataQueryWriter **query)
{
    WicFrameEncoder *f = fenc_from(This);
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!f->initialized)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!f->encoder->ops->frame_metadata)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    HRESULT hr = fenc_blocks(f);
    if (SUCCEEDED(hr))
        hr = WicMetaQueryWriterFromBlocks(f->blocks, query);
    return hr;
}

/* delegate metadata block interfaces to the frame's block object. */
static HRESULT STDMETHODCALLTYPE fenc_query_interface(IWICBitmapFrameEncode *This, REFIID riid, void **object)
{
    WicFrameEncoder *f = fenc_from(This);
    if (riid && (IsEqualGUID(riid, &IID_IWICMetadataBlockWriter) || IsEqualGUID(riid, &IID_IWICMetadataBlockReader))) {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (!f->encoder->ops->frame_metadata)
            return E_NOINTERFACE;
        HRESULT hr = fenc_blocks(f);
        if (FAILED(hr))
            return hr;
        return f->blocks->lpVtbl->QueryInterface(f->blocks, riid, object);
    }
    return WicQueryInterface(This, riid, object);
}

static const IWICBitmapFrameEncodeVtbl g_fenc_vtbl = {
    VTBL_FN(fenc_query_interface), VTBL_FN(WicAddRef), VTBL_FN(WicRelease), fenc_initialize, fenc_set_size, fenc_set_resolution, fenc_set_format, fenc_set_contexts,
    fenc_set_palette, fenc_set_thumbnail, fenc_write_pixels, fenc_write_source, fenc_commit, fenc_get_metadata_writer,
};
static const WicIfaceEntry g_fenc_ifaces[] = {WIC_ENTRY(WicFrameEncoder, IID_IWICBitmapFrameEncode, frame)};
static const WicClass g_fenc_class = {fenc_destroy, g_fenc_ifaces, 1};

/* the encoder. */

static void encoder_destroy(WicBase *base)
{
    WicEncoder *e = (WicEncoder *)base;
    if (e->state)
        e->ops->destroy(e->state);
    if (e->stream)
        e->stream->lpVtbl->Release(e->stream);
    if (e->palette)
        e->palette->lpVtbl->Release(e->palette);
    if (e->thumbnail)
        e->thumbnail->lpVtbl->Release(e->thumbnail);
    if (e->blocks)
        e->blocks->lpVtbl->Release(e->blocks);
    DeleteCriticalSection(&e->lock);
}

static HRESULT STDMETHODCALLTYPE encoder_initialize(IWICBitmapEncoder *This, IStream *stream,
                                                    WICBitmapEncoderCacheOption option)
{
    WicEncoder *e = encoder_from(This);
    if (!stream)
        return E_INVALIDARG;
    EnterCriticalSection(&e->lock);
    HRESULT hr;
    if (e->ready) {
        hr = WINCODEC_ERR_WRONGSTATE;
    } else {
        hr = e->ops->begin(stream, &e->state);
        if (SUCCEEDED(hr)) {
            stream->lpVtbl->AddRef(stream);
            e->stream = stream;
            e->ready = TRUE;
        }
    }
    LeaveCriticalSection(&e->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE encoder_get_container(IWICBitmapEncoder *This, GUID *container)
{
    WicEncoder *e = encoder_from(This);
    if (!container)
        return E_INVALIDARG;
    *container = *e->codec->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE encoder_get_info(IWICBitmapEncoder *This, IWICBitmapEncoderInfo **info)
{
    WicEncoder *e = encoder_from(This);
    if (!info)
        return E_INVALIDARG;
    return WicEncoderInfoCreate(e->codec, info);
}

static HRESULT STDMETHODCALLTYPE encoder_set_contexts(IWICBitmapEncoder *This, UINT count, IWICColorContext **contexts)
{
    WicEncoder *e = encoder_from(This);
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT STDMETHODCALLTYPE encoder_set_palette(IWICBitmapEncoder *This, IWICPalette *palette)
{
    WicEncoder *e = encoder_from(This);
    if (!palette)
        return E_INVALIDARG;
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!e->ops->container_palette)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICPalette *copy;
    HRESULT hr = WicPaletteCreate(&copy);
    if (SUCCEEDED(hr))
        hr = copy->lpVtbl->InitializeFromPalette(copy, palette);
    if (FAILED(hr)) {
        if (SUCCEEDED(WicPaletteCreate(&copy)))
            copy->lpVtbl->Release(copy);
        return hr;
    }
    if (e->palette)
        e->palette->lpVtbl->Release(e->palette);
    e->palette = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE encoder_set_thumbnail(IWICBitmapEncoder *This, IWICBitmapSource *thumbnail)
{
    WicEncoder *e = encoder_from(This);
    if (!thumbnail)
        return E_INVALIDARG;
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!e->ops->container_thumbnail)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    thumbnail->lpVtbl->AddRef(thumbnail);
    if (e->thumbnail)
        e->thumbnail->lpVtbl->Release(e->thumbnail);
    e->thumbnail = thumbnail;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE encoder_set_preview(IWICBitmapEncoder *This, IWICBitmapSource *preview)
{
    WicEncoder *e = encoder_from(This);
    if (!preview)
        return E_INVALIDARG;
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT STDMETHODCALLTYPE encoder_create_frame(IWICBitmapEncoder *This, IWICBitmapFrameEncode **frame,
                                                      IPropertyBag2 **options)
{
    WicEncoder *e = encoder_from(This);
    if (!frame)
        return E_INVALIDARG;
    *frame = nullptr;
    if (options)
        *options = nullptr;
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    EnterCriticalSection(&e->lock);
    HRESULT hr = S_OK;
    if (e->committed || e->frame_open)
        hr = WINCODEC_ERR_WRONGSTATE;
    else if (e->frames && !e->ops->multiframe)
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (SUCCEEDED(hr)) {
        WicFrameEncoder *f = WicNew(sizeof(*f), &g_fenc_class);
        if (!f) {
            hr = E_OUTOFMEMORY;
        } else {
            WIC_SLOT(f, frame, &g_fenc_vtbl);
            WicAddRef(&e->encoder);
            f->encoder = e;
            f->index = e->frames;
            for (UINT i = 0; i < e->ops->option_count && i < MAX_OPTIONS; i++)
                f->values[i] = e->ops->options[i].def;
            if (options) {
                hr = bag_create(e->ops->options, e->ops->option_count, options);
                if (FAILED(hr)) {
                    WicRelease(&f->frame);
                    f = nullptr;
                }
            }
            if (f) {
                e->frames++;
                e->frame_open = TRUE;
                *frame = (IWICBitmapFrameEncode *)&f->frame;
            }
        }
    }
    LeaveCriticalSection(&e->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE encoder_commit(IWICBitmapEncoder *This)
{
    WicEncoder *e = encoder_from(This);
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    EnterCriticalSection(&e->lock);
    HRESULT hr = S_OK;
    if (e->committed)
        hr = WINCODEC_ERR_WRONGSTATE;
    else if (!e->frames)
        hr = WINCODEC_ERR_FRAMEMISSING;
    else if (e->frames_committed != e->frames)
        hr = WINCODEC_ERR_FRAMEMISSING;
    if (SUCCEEDED(hr)) {
        IWICMetadataBlockReader *container_blocks = nullptr;
        if (e->blocks)
            e->blocks->lpVtbl->QueryInterface(e->blocks, &IID_IWICMetadataBlockReader, (void **)&container_blocks);
        hr = e->ops->finish(e->state, e->palette, e->thumbnail, container_blocks);
        if (container_blocks)
            container_blocks->lpVtbl->Release(container_blocks);
        if (SUCCEEDED(hr)) {
            e->committed = TRUE;
            e->stream->lpVtbl->Commit(e->stream, 0);
        }
    }
    LeaveCriticalSection(&e->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE encoder_get_metadata_writer(IWICBitmapEncoder *This, IWICMetadataQueryWriter **query)
{
    WicEncoder *e = encoder_from(This);
    if (!query)
        return E_INVALIDARG;
    *query = nullptr;
    if (!e->ready)
        return WINCODEC_ERR_NOTINITIALIZED;
    if (!e->ops->container_metadata)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    HRESULT hr = S_OK;
    if (!e->blocks)
        hr = WicBlockWriterCreate(e->codec->container, &e->blocks);
    if (SUCCEEDED(hr))
        hr = WicMetaQueryWriterFromBlocks(e->blocks, query);
    return hr;
}

static HRESULT STDMETHODCALLTYPE encoder_query_interface(IWICBitmapEncoder *This, REFIID riid, void **object)
{
    WicEncoder *e = encoder_from(This);
    if (riid && (IsEqualGUID(riid, &IID_IWICMetadataBlockWriter) || IsEqualGUID(riid, &IID_IWICMetadataBlockReader))) {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (!e->ops->container_metadata)
            return E_NOINTERFACE;
        if (!e->blocks) {
            HRESULT hr = WicBlockWriterCreate(e->codec->container, &e->blocks);
            if (FAILED(hr))
                return hr;
        }
        return e->blocks->lpVtbl->QueryInterface(e->blocks, riid, object);
    }
    return WicQueryInterface(This, riid, object);
}

static const IWICBitmapEncoderVtbl g_encoder_vtbl = {
    VTBL_FN(encoder_query_interface), VTBL_FN(WicAddRef), VTBL_FN(WicRelease), encoder_initialize, encoder_get_container, encoder_get_info, encoder_set_contexts,
    encoder_set_palette, encoder_set_thumbnail, encoder_set_preview, encoder_create_frame, encoder_commit,
    encoder_get_metadata_writer,
};
static const WicIfaceEntry g_encoder_ifaces[] = {WIC_ENTRY(WicEncoder, IID_IWICBitmapEncoder, encoder)};
static const WicClass g_encoder_class = {encoder_destroy, g_encoder_ifaces, 1};

HRESULT WicEncoderCreate(const WicCodec *codec, IWICBitmapEncoder **out)
{
    WicEncoder *e = WicNew(sizeof(*e), &g_encoder_class);
    if (!e)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&e->lock);
    WIC_SLOT(e, encoder, &g_encoder_vtbl);
    e->codec = codec;
    e->ops = codec->ops;
    *out = (IWICBitmapEncoder *)&e->encoder;
    return S_OK;
}

HRESULT WicEncoderPropertyBag(const WicCodec *codec, IPropertyBag2 **bag)
{
    const WicEncoderOps *ops = codec->ops;
    return bag_create(ops->options, ops->option_count, bag);
}
