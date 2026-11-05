/* component information, factories and enumeration for codecs, pixel formats, converters and metadata handlers. */
#include "wic_codec.h"

#include "meta_internal.h"

#include "codecs_table.inc"

/* the tables. */

const WicCodec *WicFindDecoder(const GUID *clsid)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_decoders); i++) {
        if (IsEqualGUID(g_decoders[i].clsid, clsid))
            return &g_decoders[i];
    }
    return nullptr;
}

const WicCodec *WicFindEncoder(const GUID *clsid)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_encoders); i++) {
        if (IsEqualGUID(g_encoders[i].clsid, clsid))
            return &g_encoders[i];
    }
    return nullptr;
}

/* the newest decoder has the highest version bits and appears later in the table. */
const WicCodec *WicDecoderByContainer(const GUID *container, const GUID *vendor)
{
    const WicCodec *found = nullptr;
    if (vendor && !WicVendorOurs(vendor))
        return nullptr;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_decoders); i++) {
        if (IsEqualGUID(g_decoders[i].container, container) && (!found || g_decoders[i].versions >= found->versions))
            found = &g_decoders[i];
    }
    /* the first PNG decoder belongs to library version 1 and is not selected by container. */
    return found;
}

const WicCodec *WicEncoderByContainer(const GUID *container, const GUID *vendor)
{
    if (vendor && !WicVendorOurs(vendor))
        return nullptr;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_encoders); i++) {
        if (IsEqualGUID(g_encoders[i].container, container))
            return &g_encoders[i];
    }
    return nullptr;
}

UINT WicDecoderCount(void) { return WIC_ARRAY_SIZE(g_decoders); }
UINT WicEncoderCount(void) { return WIC_ARRAY_SIZE(g_encoders); }
const WicCodec *WicDecoderAt(UINT index) { return index < WIC_ARRAY_SIZE(g_decoders) ? &g_decoders[index] : nullptr; }
const WicCodec *WicEncoderAt(UINT index) { return index < WIC_ARRAY_SIZE(g_encoders) ? &g_encoders[index] : nullptr; }

BOOL WicIsCodecClass(REFCLSID clsid) { return WicFindDecoder(clsid) || WicFindEncoder(clsid) || WicIsMetadataClass(clsid); }

BOOL WicPatternMatches(const WicPattern *pattern, const BYTE *data, SIZE_T size)
{
    if (pattern->position + pattern->length > size)
        return FALSE;
    const BYTE *p = data + pattern->position;
    for (UINT i = 0; i < pattern->length; i++) {
        if ((p[i] & pattern->mask[i]) != (pattern->pattern[i] & pattern->mask[i]))
            return FALSE;
    }
    return TRUE;
}

const WicCodec *WicMatchDecoder(const BYTE *data, SIZE_T size)
{
    const WicCodec *found = nullptr;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_decoders); i++) {
        const WicCodec *codec = &g_decoders[i];
        for (UINT k = 0; k < codec->pattern_count; k++) {
            if (WicPatternMatches(&codec->patterns[k], data, size)) {
                /* later entries for a container take precedence. */
                if (!found || !IsEqualGUID(found->container, codec->container) || codec->versions >= found->versions)
                    found = codec;
                break;
            }
        }
    }
    return found;
}

/* strings of components. */

static HRESULT return_string(const WCHAR *text, UINT size, WCHAR *buffer, UINT *actual)
{
    UINT length = text ? (UINT)wcslen(text) + 1 : 0;
    if (!actual)
        return E_INVALIDARG;
    *actual = length;
    if (!buffer) {
        return size == 0 ? S_OK : E_INVALIDARG;
    }
    if (!size)
        return E_INVALIDARG;
    if (size < length)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    if (length)
        memcpy(buffer, text, length * sizeof(WCHAR));
    return S_OK;
}

/* the info objects. */

typedef enum InfoKind { INFO_DECODER, INFO_ENCODER, INFO_PIXELFORMAT, INFO_CONVERTER } InfoKind;

typedef struct WicInfo {
    WicBase base;
    WicSlot slot;
    InfoKind kind;
    const WicCodec *codec;
    const WicFormat *format;
    const GUID *clsid;
} WicInfo;

static WicInfo *info_from(void *This) { return WIC_FROM_SLOT(WicInfo, slot, This); }

#define INFO_THIS(T) WicInfo *info = info_from(This)

static HRESULT STDMETHODCALLTYPE info_component_type(void *This, WICComponentType *type)
{
    INFO_THIS(IWICComponentInfo);
    if (!type)
        return E_INVALIDARG;
    *type = info->kind == INFO_DECODER ? WICDecoder : info->kind == INFO_ENCODER ? WICEncoder
            : info->kind == INFO_PIXELFORMAT ? WICPixelFormat : WICPixelFormatConverter;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_clsid(void *This, CLSID *clsid)
{
    INFO_THIS(IWICComponentInfo);
    if (!clsid)
        return E_INVALIDARG;
    *clsid = *info->clsid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_signing(void *This, DWORD *status)
{
    if (!status)
        return E_INVALIDARG;
    *status = WICComponentSigned;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_author(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    return return_string(L"Microsoft", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE info_vendor(void *This, GUID *vendor)
{
    if (!vendor)
        return E_INVALIDARG;
    *vendor = GUID_VendorMicrosoft;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_version(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICComponentInfo);
    return return_string(info->codec ? info->codec->version : L"1.0.0.0", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE info_spec_version(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICComponentInfo);
    return return_string(info->codec ? info->codec->spec : L"1.0.0.0", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE info_friendly_name(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICComponentInfo);
    const WCHAR *name = info->codec ? info->codec->name : info->format ? info->format->name : L"Default Pixel Format Converter";
    return return_string(name, size, buffer, actual);
}

#define COMPONENT_FUNCS                                                                                                \
    WIC_IUNKNOWN_FUNCS, VTBL_FN(info_component_type), VTBL_FN(info_clsid), VTBL_FN(info_signing),                      \
        VTBL_FN(info_author), VTBL_FN(info_vendor), VTBL_FN(info_version), VTBL_FN(info_spec_version),                 \
        VTBL_FN(info_friendly_name)


static HRESULT STDMETHODCALLTYPE codec_container(void *This, GUID *container)
{
    INFO_THIS(IWICBitmapCodecInfo);
    if (!container)
        return E_INVALIDARG;
    *container = *info->codec->container;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE codec_pixel_formats(void *This, UINT count, GUID *formats, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    if (!actual)
        return E_INVALIDARG;
    const WicCodec *codec = info->codec;
    *actual = codec->format_count;
    if (!formats) {
        return count == 0 ? S_OK : E_INVALIDARG;
    }
    if (count < codec->format_count)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT i = 0; i < codec->format_count; i++)
        formats[i] = *codec->formats[i];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE codec_color_version(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    return return_string(info->codec->color_version, size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE codec_manufacturer(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    return return_string(info->codec->manufacturer, size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE codec_models(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    return return_string(info->codec->models, size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE codec_mime(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    return return_string(info->codec->mime, size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE codec_extensions(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    INFO_THIS(IWICBitmapCodecInfo);
    return return_string(info->codec->extensions, size, buffer, actual);
}

static HRESULT flag_of(BYTE value, BOOL *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = value != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE codec_animation(void *This, BOOL *r) { return flag_of(info_from(This)->codec->animation, r); }
static HRESULT STDMETHODCALLTYPE codec_chromakey(void *This, BOOL *r) { return flag_of(info_from(This)->codec->chromakey, r); }
static HRESULT STDMETHODCALLTYPE codec_lossless(void *This, BOOL *r) { return flag_of(info_from(This)->codec->lossless, r); }
static HRESULT STDMETHODCALLTYPE codec_multiframe(void *This, BOOL *r) { return flag_of(info_from(This)->codec->multiframe, r); }

static BOOL wild_match(const WCHAR *list, const WCHAR *text)
{
    /* match comma-separated names without case sensitivity. */
    if (!list)
        return FALSE;
    SIZE_T length = wcslen(text);
    while (*list) {
        const WCHAR *end = list;
        while (*end && *end != ',')
            end++;
        if ((SIZE_T)(end - list) == length) {
            SIZE_T i = 0;
            for (; i < length; i++) {
                WCHAR a = list[i], b = text[i];
                if (a >= 'A' && a <= 'Z')
                    a += 32;
                if (b >= 'A' && b <= 'Z')
                    b += 32;
                if (a != b)
                    break;
            }
            if (i == length)
                return TRUE;
        }
        list = *end ? end + 1 : end;
    }
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE codec_matches_mime(void *This, LPCWSTR mime, BOOL *matches)
{
    INFO_THIS(IWICBitmapCodecInfo);
    if (!mime || !matches)
        return E_INVALIDARG;
    *matches = wild_match(info->codec->mime, mime);
    return S_OK;
}

#define CODEC_FUNCS                                                                                                    \
    VTBL_FN(codec_container), VTBL_FN(codec_pixel_formats), VTBL_FN(codec_color_version),                              \
        VTBL_FN(codec_manufacturer), VTBL_FN(codec_models), VTBL_FN(codec_mime), VTBL_FN(codec_extensions),           \
        VTBL_FN(codec_animation), VTBL_FN(codec_chromakey), VTBL_FN(codec_lossless), VTBL_FN(codec_multiframe),        \
        VTBL_FN(codec_matches_mime)

static HRESULT STDMETHODCALLTYPE decoder_info_patterns(IWICBitmapDecoderInfo *This, UINT size,
                                                       WICBitmapPattern *patterns, UINT *count, UINT *actual)
{
    INFO_THIS(IWICBitmapDecoderInfo);
    const WicCodec *codec = info->codec;
    if (!count || !actual)
        return E_INVALIDARG;
    UINT total = 0;
    for (UINT i = 0; i < codec->pattern_count; i++)
        total += (UINT)sizeof(WICBitmapPattern) + 2 * codec->patterns[i].length;
    *count = codec->pattern_count;
    *actual = total;
    if (!patterns)
        return size == 0 ? S_OK : E_INVALIDARG;
    if (size < total)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    BYTE *data = (BYTE *)(patterns + codec->pattern_count);
    for (UINT i = 0; i < codec->pattern_count; i++) {
        const WicPattern *p = &codec->patterns[i];
        patterns[i].Position.QuadPart = p->position;
        patterns[i].Length = p->length;
        patterns[i].Pattern = data;
        memcpy(data, p->pattern, p->length);
        data += p->length;
        patterns[i].Mask = data;
        memcpy(data, p->mask, p->length);
        data += p->length;
        patterns[i].EndOfStream = p->end_of_stream;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_info_matches(IWICBitmapDecoderInfo *This, IStream *stream, BOOL *matches)
{
    INFO_THIS(IWICBitmapDecoderInfo);
    if (!stream || !matches)
        return E_INVALIDARG;
    *matches = FALSE;
    LARGE_INTEGER zero;
    ULARGE_INTEGER start;
    zero.QuadPart = 0;
    HRESULT hr = stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start);
    if (FAILED(hr))
        return hr;
    BYTE head[64];
    UINT want = 0;
    for (UINT i = 0; i < info->codec->pattern_count; i++) {
        UINT end = (UINT)info->codec->patterns[i].position + info->codec->patterns[i].length;
        if (end > want)
            want = end;
    }
    if (want > sizeof(head))
        want = sizeof(head);
    ULONG got = 0;
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)start.QuadPart;
    hr = stream->lpVtbl->Read(stream, head, want, &got);
    stream->lpVtbl->Seek(stream, at, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < info->codec->pattern_count && !*matches; i++)
        *matches = WicPatternMatches(&info->codec->patterns[i], head, got);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE decoder_info_create(IWICBitmapDecoderInfo *This, IWICBitmapDecoder **decoder)
{
    INFO_THIS(IWICBitmapDecoderInfo);
    if (!decoder)
        return E_INVALIDARG;
    return WicDecoderCreate(info->codec, decoder);
}

static HRESULT STDMETHODCALLTYPE encoder_info_create(IWICBitmapEncoderInfo *This, IWICBitmapEncoder **encoder)
{
    INFO_THIS(IWICBitmapEncoderInfo);
    if (!encoder)
        return E_INVALIDARG;
    return WicEncoderCreate(info->codec, encoder);
}

static const IWICBitmapDecoderInfoVtbl g_decoder_info_vtbl = {
    COMPONENT_FUNCS, CODEC_FUNCS, decoder_info_patterns, decoder_info_matches, decoder_info_create,
};

static const IWICBitmapEncoderInfoVtbl g_encoder_info_vtbl = {
    COMPONENT_FUNCS, CODEC_FUNCS, encoder_info_create,
};


static HRESULT STDMETHODCALLTYPE format_guid(void *This, GUID *guid)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!guid)
        return E_INVALIDARG;
    *guid = *info->format->guid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_color_context(void *This, IWICColorContext **context)
{
    if (!context)
        return E_INVALIDARG;
    IWICColorContext *c;
    HRESULT hr = WicColorContextCreate(&c);
    if (FAILED(hr))
        return hr;
    hr = c->lpVtbl->InitializeFromExifColorSpace(c, 1);
    if (FAILED(hr)) {
        c->lpVtbl->Release(c);
        return hr;
    }
    *context = c;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_bpp(void *This, UINT *bpp)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!bpp)
        return E_INVALIDARG;
    *bpp = info->format->bpp;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_channels(void *This, UINT *count)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!count)
        return E_INVALIDARG;
    *count = info->format->channels;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_mask(void *This, UINT channel, UINT size, BYTE *buffer, UINT *actual)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!actual || channel >= info->format->channels)
        return E_INVALIDARG;
    UINT bytes = (info->format->bpp + 7) / 8;
    *actual = bytes;
    if (buffer) {
        if (size < bytes)
            return E_INVALIDARG;
        memcpy(buffer, info->format->masks + (SIZE_T)channel * bytes, bytes);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_transparency(void *This, BOOL *transparent)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!transparent)
        return E_INVALIDARG;
    *transparent = info->format->alpha;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE format_numeric(void *This, WICPixelFormatNumericRepresentation *numeric)
{
    INFO_THIS(IWICPixelFormatInfo);
    if (!numeric)
        return E_INVALIDARG;
    *numeric = (WICPixelFormatNumericRepresentation)info->format->numeric;
    return S_OK;
}

static const IWICPixelFormatInfo2Vtbl g_format_info_vtbl = {
    COMPONENT_FUNCS, VTBL_FN(format_guid), VTBL_FN(format_color_context), VTBL_FN(format_bpp),
    VTBL_FN(format_channels), VTBL_FN(format_mask), VTBL_FN(format_transparency), VTBL_FN(format_numeric),
};


static HRESULT STDMETHODCALLTYPE converter_info_formats(IWICFormatConverterInfo *This, UINT count, GUID *formats,
                                                        UINT *actual)
{
    UINT total = WicFormatCount(), n = 0;
    if (!actual)
        return E_INVALIDARG;
    for (UINT i = 0; i < total; i++) {
        const WicFormat *f = WicFormatAt(i);
        if (f->numeric == WICPixelFormatNumericRepresentationUnspecified)
            continue;
        n++;
    }
    *actual = n;
    if (!formats)
        return count == 0 ? S_OK : E_INVALIDARG;
    if (count < n)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    n = 0;
    for (UINT i = 0; i < total; i++) {
        const WicFormat *f = WicFormatAt(i);
        formats[n++] = *f->guid;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE converter_info_create(IWICFormatConverterInfo *This, IWICFormatConverter **converter)
{
    if (!converter)
        return E_INVALIDARG;
    return WicFormatConverterCreate(converter);
}

static const IWICFormatConverterInfoVtbl g_converter_info_vtbl = {
    COMPONENT_FUNCS, converter_info_formats, converter_info_create,
};

static const WicIfaceEntry g_decoder_info_ifaces[] = {
    WIC_ENTRY(WicInfo, IID_IWICBitmapDecoderInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICBitmapCodecInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICComponentInfo, slot),
};
static const WicIfaceEntry g_encoder_info_ifaces[] = {
    WIC_ENTRY(WicInfo, IID_IWICBitmapEncoderInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICBitmapCodecInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICComponentInfo, slot),
};
static const WicIfaceEntry g_format_info_ifaces[] = {
    WIC_ENTRY(WicInfo, IID_IWICPixelFormatInfo2, slot),
    WIC_ENTRY(WicInfo, IID_IWICPixelFormatInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICComponentInfo, slot),
};
static const WicIfaceEntry g_converter_info_ifaces[] = {
    WIC_ENTRY(WicInfo, IID_IWICFormatConverterInfo, slot),
    WIC_ENTRY(WicInfo, IID_IWICComponentInfo, slot),
};
static const WicClass g_decoder_info_class = {nullptr, g_decoder_info_ifaces, 3};
static const WicClass g_encoder_info_class = {nullptr, g_encoder_info_ifaces, 3};
static const WicClass g_format_info_class = {nullptr, g_format_info_ifaces, 3};
static const WicClass g_converter_info_class = {nullptr, g_converter_info_ifaces, 2};

static WicInfo *info_new(const WicClass *cls, const void *vtbl, InfoKind kind)
{
    WicInfo *info = WicNew(sizeof(*info), cls);
    if (!info)
        return nullptr;
    WIC_SLOT(info, slot, vtbl);
    info->kind = kind;
    return info;
}

HRESULT WicDecoderInfoCreate(const WicCodec *codec, IWICBitmapDecoderInfo **out)
{
    WicInfo *info = info_new(&g_decoder_info_class, &g_decoder_info_vtbl, INFO_DECODER);
    if (!info)
        return E_OUTOFMEMORY;
    info->codec = codec;
    info->clsid = codec->clsid;
    *out = (IWICBitmapDecoderInfo *)&info->slot;
    return S_OK;
}

HRESULT WicEncoderInfoCreate(const WicCodec *codec, IWICBitmapEncoderInfo **out)
{
    WicInfo *info = info_new(&g_encoder_info_class, &g_encoder_info_vtbl, INFO_ENCODER);
    if (!info)
        return E_OUTOFMEMORY;
    info->codec = codec;
    info->clsid = codec->clsid;
    *out = (IWICBitmapEncoderInfo *)&info->slot;
    return S_OK;
}

HRESULT WicPixelFormatInfoCreate(const WicFormat *format, IWICPixelFormatInfo **out)
{
    WicInfo *info = info_new(&g_format_info_class, &g_format_info_vtbl, INFO_PIXELFORMAT);
    if (!info)
        return E_OUTOFMEMORY;
    info->format = format;
    info->clsid = format->guid;
    *out = (IWICPixelFormatInfo *)&info->slot;
    return S_OK;
}

HRESULT WicConverterInfoCreate(const GUID *clsid, IWICFormatConverterInfo **out)
{
    WicInfo *info = info_new(&g_converter_info_class, &g_converter_info_vtbl, INFO_CONVERTER);
    if (!info)
        return E_OUTOFMEMORY;
    info->clsid = clsid;
    *out = (IWICFormatConverterInfo *)&info->slot;
    return S_OK;
}

/* finding and enumerating. */


HRESULT WicComponentInfo(const GUID *clsid, IWICComponentInfo **out)
{
    *out = nullptr;
    const WicCodec *codec = WicFindDecoder(clsid);
    if (codec) {
        IWICBitmapDecoderInfo *info;
        HRESULT hr = WicDecoderInfoCreate(codec, &info);
        *out = (IWICComponentInfo *)info;
        return hr;
    }
    codec = WicFindEncoder(clsid);
    if (codec) {
        IWICBitmapEncoderInfo *info;
        HRESULT hr = WicEncoderInfoCreate(codec, &info);
        *out = (IWICComponentInfo *)info;
        return hr;
    }
    const WicFormat *format = WicFindFormat(clsid);
    if (format) {
        IWICPixelFormatInfo *info;
        HRESULT hr = WicPixelFormatInfoCreate(format, &info);
        *out = (IWICComponentInfo *)info;
        return hr;
    }
    if (IsEqualGUID(clsid, &CLSID_WICDefaultFormatConverter) || IsEqualGUID(clsid, &CLSID_WICFormatConverterHighColor)) {
        IWICFormatConverterInfo *info;
        HRESULT hr = WicConverterInfoCreate(clsid, &info);
        *out = (IWICComponentInfo *)info;
        return hr;
    }
    if (SUCCEEDED(WicMetadataInfoFind(clsid, out)))
        return S_OK;
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}

typedef struct WicEnum {
    WicBase base;
    WicSlot slot;
    IUnknown **items;
    UINT count;
    UINT position;
} WicEnum;

static WicEnum *enum_from(IEnumUnknown *This) { return WIC_FROM_SLOT(WicEnum, slot, This); }

static void enum_destroy(WicBase *base)
{
    WicEnum *e = (WicEnum *)base;
    for (UINT i = 0; i < e->count; i++)
        e->items[i]->lpVtbl->Release(e->items[i]);
    WicFree(e->items);
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumUnknown *This, ULONG count, IUnknown **items, ULONG *fetched)
{
    WicEnum *e = enum_from(This);
    ULONG got = 0;
    if (!items && count)
        return E_INVALIDARG;
    while (got < count && e->position < e->count) {
        IUnknown *item = e->items[e->position++];
        item->lpVtbl->AddRef(item);
        items[got++] = item;
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumUnknown *This, ULONG count)
{
    WicEnum *e = enum_from(This);
    if (e->position + count > e->count) {
        e->position = e->count;
        return S_FALSE;
    }
    e->position += count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumUnknown *This)
{
    enum_from(This)->position = 0;
    return S_OK;
}

static HRESULT enum_create(IUnknown **items, UINT count, UINT position, IEnumUnknown **out);

HRESULT WicEnumUnknownCreate(IUnknown **items, UINT count, IEnumUnknown **out) { return enum_create(items, count, 0, out); }

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumUnknown *This, IEnumUnknown **out)
{
    WicEnum *e = enum_from(This);
    if (!out)
        return E_INVALIDARG;
    IUnknown **items = WicAlloc(((SIZE_T)e->count + 1) * sizeof(IUnknown *));
    if (!items)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < e->count; i++) {
        items[i] = e->items[i];
        items[i]->lpVtbl->AddRef(items[i]);
    }
    return enum_create(items, e->count, e->position, out);
}

static const IEnumUnknownVtbl g_enum_vtbl = {WIC_IUNKNOWN_FUNCS, enum_next, enum_skip, enum_reset, enum_clone};
static const WicIfaceEntry g_enum_ifaces[] = {WIC_ENTRY(WicEnum, IID_IEnumUnknown, slot)};
static const WicClass g_enum_class = {enum_destroy, g_enum_ifaces, 1};

/* takes the items and their references. */
static HRESULT enum_create(IUnknown **items, UINT count, UINT position, IEnumUnknown **out)
{
    WicEnum *e = WicNew(sizeof(*e), &g_enum_class);
    if (!e) {
        for (UINT i = 0; i < count; i++)
            items[i]->lpVtbl->Release(items[i]);
        WicFree(items);
        return E_OUTOFMEMORY;
    }
    WIC_SLOT(e, slot, &g_enum_vtbl);
    e->items = items;
    e->count = count;
    e->position = position;
    *out = (IEnumUnknown *)&e->slot;
    return S_OK;
}

HRESULT WicEnumerateComponents(DWORD types, DWORD options, IEnumUnknown **out)
{
    *out = nullptr;
    /* none of our components are disabled or unsigned. */
    if ((options & WICComponentEnumerateDisabled) || (options & WICComponentEnumerateUnsigned))
        return enum_create(WicAlloc(sizeof(IUnknown *)), 0, 0, out);
    UINT capacity = 128 + WicFormatCount();
    IUnknown **items = WicAlloc((SIZE_T)capacity * sizeof(IUnknown *));
    UINT count = 0;
    if (!items)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (types & WICDecoder) {
        for (UINT i = 0; i < WicDecoderCount() && SUCCEEDED(hr); i++) {
            IWICBitmapDecoderInfo *info;
            hr = WicDecoderInfoCreate(WicDecoderAt(i), &info);
            if (SUCCEEDED(hr))
                items[count++] = (IUnknown *)info;
        }
    }
    if ((types & WICEncoder) && SUCCEEDED(hr)) {
        for (UINT i = 0; i < WicEncoderCount() && SUCCEEDED(hr); i++) {
            IWICBitmapEncoderInfo *info;
            hr = WicEncoderInfoCreate(WicEncoderAt(i), &info);
            if (SUCCEEDED(hr))
                items[count++] = (IUnknown *)info;
        }
    }
    if ((types & WICPixelFormatConverter) && SUCCEEDED(hr)) {
        IWICFormatConverterInfo *info;
        hr = WicConverterInfoCreate(&CLSID_WICDefaultFormatConverter, &info);
        if (SUCCEEDED(hr))
            items[count++] = (IUnknown *)info;
    }
    if ((types & WICPixelFormat) && SUCCEEDED(hr)) {
        for (UINT i = 0; i < WicFormatCount() && SUCCEEDED(hr); i++) {
            IWICPixelFormatInfo *info;
            hr = WicPixelFormatInfoCreate(WicFormatAt(i), &info);
            if (SUCCEEDED(hr))
                items[count++] = (IUnknown *)info;
        }
    }
    if ((types & (WICMetadataReader | WICMetadataWriter)) && SUCCEEDED(hr)) {
        IUnknown **meta;
        UINT meta_count;
        hr = WicMetadataInfoList(types, &meta, &meta_count);
        if (SUCCEEDED(hr)) {
            if (count + meta_count > capacity) {
                capacity = count + meta_count;
                IUnknown **bigger = WicRealloc(items, (SIZE_T)capacity * sizeof(IUnknown *));
                if (!bigger)
                    hr = E_OUTOFMEMORY;
                else
                    items = bigger;
            }
            for (UINT i = 0; i < meta_count; i++) {
                if (SUCCEEDED(hr))
                    items[count++] = meta[i];
                else
                    meta[i]->lpVtbl->Release(meta[i]);
            }
            WicFree(meta);
        }
    }
    if (FAILED(hr)) {
        for (UINT i = 0; i < count; i++)
            items[i]->lpVtbl->Release(items[i]);
        WicFree(items);
        return hr;
    }
    return enum_create(items, count, 0, out);
}
