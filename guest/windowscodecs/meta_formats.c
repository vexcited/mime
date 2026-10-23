/* metadata format names, handler classes, XMP schemas and information objects. */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"

#include "meta_names.inc"

typedef struct FormatClasses {
    const GUID *format;
    const GUID *reader;
    const GUID *writer;
    const WCHAR *name;
} FormatClasses;

#define F(fmt, r, w, name) {&GUID_MetadataFormat##fmt, &CLSID_WIC##r##MetadataReader, &CLSID_WIC##w##MetadataWriter, name}

static const FormatClasses g_classes[] = {
    F(Unknown, Unknown, Unknown, L"Unknown"),
    F(Ifd, Ifd, Ifd, L"IFD"),
    F(Exif, Exif, Exif, L"Exif"),
    F(Gps, Gps, Gps, L"GPS"),
    F(Interop, Interop, Interop, L"Interoperability"),
    F(SubIfd, SubIfd, SubIfd, L"Sub IFD"),
    F(Thumbnail, Thumbnail, Thumbnail, L"Thumbnail"),
    F(App0, App0, App0, L"APP0"),
    F(App1, App1, App1, L"APP1"),
    F(App13, App13, App13, L"APP13"),
    F(IRB, IRB, IRB, L"Image Resource Block"),
    F(IPTC, IPTC, IPTC, L"IPTC"),
    F(8BIMIPTC, 8BIMIPTC, 8BIMIPTC, L"8BIM IPTC"),
    F(8BIMIPTCDigest, 8BIMIPTCDigest, 8BIMIPTCDigest, L"8BIM IPTC Digest"),
    F(8BIMResolutionInfo, 8BIMResolutionInfo, 8BIMResolutionInfo, L"8BIM Resolution Info"),
    F(XMP, XMP, XMP, L"XMP"),
    F(XMPStruct, XMPStruct, XMPStruct, L"XMP Struct"),
    F(XMPBag, XMPBag, XMPBag, L"XMP Bag"),
    F(XMPSeq, XMPSeq, XMPSeq, L"XMP Seq"),
    F(XMPAlt, XMPAlt, XMPAlt, L"XMP Alt"),
    F(JpegComment, JpegComment, JpegComment, L"JPEG Comment"),
    F(JpegChrominance, JpegChrominance, JpegChrominance, L"JPEG Chrominance"),
    F(JpegLuminance, JpegLuminance, JpegLuminance, L"JPEG Luminance"),
    F(ChunktEXt, PngText, PngText, L"PNG tEXt"),
    F(ChunkiTXt, PngItxt, PngItxt, L"PNG iTXt"),
    F(ChunkgAMA, PngGama, PngGama, L"PNG gAMA"),
    F(ChunkcHRM, PngChrm, PngChrm, L"PNG cHRM"),
    F(ChunksRGB, PngSrgb, PngSrgb, L"PNG sRGB"),
    F(ChunkiCCP, PngIccp, PngIccp, L"PNG iCCP"),
    F(ChunkbKGD, PngBkgd, PngBkgd, L"PNG bKGD"),
    F(ChunkhIST, PngHist, PngHist, L"PNG hIST"),
    F(ChunktIME, PngTime, PngTime, L"PNG tIME"),
    F(LSD, LSD, LSD, L"GIF Logical Screen Descriptor"),
    F(IMD, IMD, IMD, L"GIF Image Descriptor"),
    F(GCE, GCE, GCE, L"GIF Graphic Control Extension"),
    F(APE, APE, APE, L"GIF Application Extension"),
    F(GifComment, GifComment, GifComment, L"GIF Comment Extension"),
    F(Dds, Dds, Dds, L"DDS"),
};

#undef F

const WCHAR *WicFormatShortName(const GUID *guid)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_short_names); i++) {
        if (IsEqualGUID(g_short_names[i].guid, guid))
            return g_short_names[i].name;
    }
    return nullptr;
}

const GUID *WicFormatFromShortName(const WCHAR *name)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_short_names); i++) {
        if (!wcscmp(g_short_names[i].name, name))
            return g_short_names[i].guid;
    }
    return nullptr;
}

/* exports. */

DECLSPEC_EXPORT HRESULT WINAPI WICMapGuidToShortName(REFGUID guid, UINT size, WCHAR *name, UINT *actual)
{
    if (!guid)
        return E_INVALIDARG;
    const WCHAR *short_name = WicFormatShortName(guid);
    if (!short_name)
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    UINT length = (UINT)wcslen(short_name) + 1;
    if (name) {
        if (size < length)
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        memcpy(name, short_name, length * sizeof(WCHAR));
    }
    if (actual)
        *actual = length;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WICMapShortNameToGuid(PCWSTR name, GUID *guid)
{
    if (!name || !guid)
        return E_INVALIDARG;
    const GUID *found = WicFormatFromShortName(name);
    if (!found)
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    *guid = *found;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WICMapSchemaToName(REFGUID format, LPWSTR schema, UINT size, WCHAR *name, UINT *actual)
{
    if (!format || !schema)
        return E_INVALIDARG;
    for (UINT g = 0; g < WIC_ARRAY_SIZE(g_schema_groups); g++) {
        if (!IsEqualGUID(g_schema_groups[g].format, format))
            continue;
        const struct { const WCHAR *name; const WCHAR *uri; } *table = g_schema_groups[g].table;
        for (UINT i = 0; i < g_schema_groups[g].count; i++) {
            if (wcscmp(table[i].uri, schema))
                continue;
            UINT length = (UINT)wcslen(table[i].name) + 1;
            if (name) {
                if (size < length)
                    return STRSAFE_E_INSUFFICIENT_BUFFER;
                memcpy(name, table[i].name, length * sizeof(WCHAR));
            }
            if (actual)
                *actual = length;
            return S_OK;
        }
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    }
    return WINCODEC_ERR_PROPERTYNOTFOUND;
}

const WCHAR *WicSchemaUri(const GUID *format, const WCHAR *prefix, SIZE_T prefix_length)
{
    for (UINT g = 0; g < WIC_ARRAY_SIZE(g_schema_groups); g++) {
        if (!IsEqualGUID(g_schema_groups[g].format, format))
            continue;
        const struct { const WCHAR *name; const WCHAR *uri; } *table = g_schema_groups[g].table;
        for (UINT i = 0; i < g_schema_groups[g].count; i++) {
            if (wcslen(table[i].name) == prefix_length && !wcsncmp(table[i].name, prefix, prefix_length))
                return table[i].uri;
        }
    }
    return nullptr;
}

const WCHAR *WicSchemaPrefix(const GUID *format, const WCHAR *uri)
{
    for (UINT g = 0; g < WIC_ARRAY_SIZE(g_schema_groups); g++) {
        if (!IsEqualGUID(g_schema_groups[g].format, format))
            continue;
        const struct { const WCHAR *name; const WCHAR *uri; } *table = g_schema_groups[g].table;
        for (UINT i = 0; i < g_schema_groups[g].count; i++) {
            if (!wcscmp(table[i].uri, uri))
                return table[i].name;
        }
    }
    return nullptr;
}

/* handler information. */

typedef struct WicHandlerInfo {
    WicBase base;
    WicSlot slot;
    const FormatClasses *cls;
    BOOL writer;
} WicHandlerInfo;

static WicHandlerInfo *hi_of(void *This) { return (WicHandlerInfo *)WicSlotBase(This); }

static HRESULT hi_string(const WCHAR *text, UINT size, WCHAR *buffer, UINT *actual)
{
    UINT length = (UINT)wcslen(text) + 1;
    if (!actual)
        return E_INVALIDARG;
    *actual = length;
    if (!buffer)
        return size == 0 ? S_OK : E_INVALIDARG;
    if (!size)
        return E_INVALIDARG;
    if (size < length)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    memcpy(buffer, text, length * sizeof(WCHAR));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_type(void *This, WICComponentType *type)
{
    if (!type)
        return E_INVALIDARG;
    *type = hi_of(This)->writer ? WICMetadataWriter : WICMetadataReader;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_clsid(void *This, CLSID *clsid)
{
    WicHandlerInfo *h = hi_of(This);
    if (!clsid)
        return E_INVALIDARG;
    *clsid = h->writer ? *h->cls->writer : *h->cls->reader;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_signing(void *This, DWORD *status)
{
    if (!status)
        return E_INVALIDARG;
    *status = WICComponentSigned;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_author(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    return hi_string(L"Microsoft", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE hi_vendor(void *This, GUID *vendor)
{
    if (!vendor)
        return E_INVALIDARG;
    *vendor = GUID_VendorMicrosoft;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_version(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    return hi_string(L"1.0.0.0", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE hi_friendly(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    WicHandlerInfo *h = hi_of(This);
    WCHAR text[96];
    wcscpy(text, h->cls->name);
    wcscat(text, h->writer ? L" Writer" : L" Reader");
    return hi_string(text, size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE hi_format(void *This, GUID *format)
{
    if (!format)
        return E_INVALIDARG;
    *format = *hi_of(This)->cls->format;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_containers(void *This, UINT count, GUID *formats, UINT *actual)
{
    if (!actual)
        return E_INVALIDARG;
    /* guessed container support: the three containers carrying most metadata. */
    static const GUID *const containers[] = {&GUID_ContainerFormatJpeg, &GUID_ContainerFormatTiff,
                                              &GUID_ContainerFormatPng};
    *actual = 3;
    if (!formats)
        return count == 0 ? S_OK : E_INVALIDARG;
    if (count < 3)
        return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT i = 0; i < 3; i++)
        formats[i] = *containers[i];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_manufacturer(void *This, UINT size, WCHAR *buffer, UINT *actual)
{
    return hi_string(L"", size, buffer, actual);
}

static HRESULT STDMETHODCALLTYPE hi_flag(void *This, BOOL *flag)
{
    if (!flag)
        return E_INVALIDARG;
    *flag = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_patterns(void *This, REFGUID container, UINT size, WICMetadataPattern *patterns,
                                             UINT *count, UINT *actual)
{
    if (!count || !actual)
        return E_INVALIDARG;
    *count = 0;
    *actual = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_matches(void *This, REFGUID container, IStream *stream, BOOL *matches)
{
    if (!stream || !matches)
        return E_INVALIDARG;
    *matches = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_create_reader(void *This, IWICMetadataReader **reader)
{
    if (!reader)
        return E_INVALIDARG;
    IWICMetadataWriter *w;
    HRESULT hr = WicMetaHandlerCreate(hi_of(This)->cls->format, &w);
    if (SUCCEEDED(hr))
        *reader = (IWICMetadataReader *)w;
    return hr;
}

static HRESULT STDMETHODCALLTYPE hi_header(void *This, REFGUID container, UINT size, WICMetadataHeader *header,
                                           UINT *actual)
{
    if (!actual)
        return E_INVALIDARG;
    *actual = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hi_create_writer(void *This, IWICMetadataWriter **writer)
{
    if (!writer)
        return E_INVALIDARG;
    return WicMetaHandlerCreate(hi_of(This)->cls->format, writer);
}

#define HI_COMPONENT                                                                                                   \
    VTBL_FN(hi_type), VTBL_FN(hi_clsid), VTBL_FN(hi_signing), VTBL_FN(hi_author), VTBL_FN(hi_vendor),                   \
        VTBL_FN(hi_version), VTBL_FN(hi_version), VTBL_FN(hi_friendly)
#define HI_HANDLER                                                                                                     \
    VTBL_FN(hi_format), VTBL_FN(hi_containers), VTBL_FN(hi_manufacturer), VTBL_FN(hi_manufacturer),                    \
        VTBL_FN(hi_flag), VTBL_FN(hi_flag), VTBL_FN(hi_flag)

static const IWICMetadataReaderInfoVtbl g_reader_info_vtbl = {
    WIC_IUNKNOWN_FUNCS, HI_COMPONENT, HI_HANDLER, VTBL_FN(hi_patterns), VTBL_FN(hi_matches), VTBL_FN(hi_create_reader),
};
static const IWICMetadataWriterInfoVtbl g_writer_info_vtbl = {
    WIC_IUNKNOWN_FUNCS, HI_COMPONENT, HI_HANDLER, VTBL_FN(hi_header), VTBL_FN(hi_create_writer),
};

static const WicIfaceEntry g_reader_info_ifaces[] = {
    WIC_ENTRY(WicHandlerInfo, IID_IWICMetadataReaderInfo, slot),
    WIC_ENTRY(WicHandlerInfo, IID_IWICMetadataHandlerInfo, slot),
    WIC_ENTRY(WicHandlerInfo, IID_IWICComponentInfo, slot),
};
static const WicIfaceEntry g_writer_info_ifaces[] = {
    WIC_ENTRY(WicHandlerInfo, IID_IWICMetadataWriterInfo, slot),
    WIC_ENTRY(WicHandlerInfo, IID_IWICMetadataHandlerInfo, slot),
    WIC_ENTRY(WicHandlerInfo, IID_IWICComponentInfo, slot),
};
static const WicClass g_reader_info_class = {nullptr, g_reader_info_ifaces, 3};
static const WicClass g_writer_info_class = {nullptr, g_writer_info_ifaces, 3};

static HRESULT info_create(const FormatClasses *cls, BOOL writer, void **out)
{
    WicHandlerInfo *h = WicNew(sizeof(*h), writer ? &g_writer_info_class : &g_reader_info_class);
    if (!h)
        return E_OUTOFMEMORY;
    WIC_SLOT(h, slot, writer ? (const void *)&g_writer_info_vtbl : (const void *)&g_reader_info_vtbl);
    h->cls = cls;
    h->writer = writer;
    *out = &h->slot;
    return S_OK;
}

static const FormatClasses *find_class(const GUID *format)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_classes); i++) {
        if (IsEqualGUID(g_classes[i].format, format))
            return &g_classes[i];
    }
    return &g_classes[0];
}

HRESULT WicMetaHandlerInfoCreate(const GUID *format, BOOL writer, IWICMetadataHandlerInfo **out)
{
    return info_create(find_class(format), writer, (void **)out);
}

HRESULT WicMetadataInfoFind(const GUID *clsid, IWICComponentInfo **out)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_classes); i++) {
        if (IsEqualGUID(g_classes[i].reader, clsid))
            return info_create(&g_classes[i], FALSE, (void **)out);
        if (IsEqualGUID(g_classes[i].writer, clsid))
            return info_create(&g_classes[i], TRUE, (void **)out);
    }
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}

HRESULT WicMetadataInfoList(DWORD types, IUnknown ***list, UINT *count)
{
    *count = 0;
    *list = WicAlloc((SIZE_T)WIC_ARRAY_SIZE(g_classes) * 2 * sizeof(IUnknown *) + 8);
    if (!*list)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_classes); i++) {
        for (int w = 0; w < 2; w++) {
            if (!(types & (w ? WICMetadataWriter : WICMetadataReader)))
                continue;
            void *info;
            HRESULT hr = info_create(&g_classes[i], w, &info);
            if (FAILED(hr)) {
                for (UINT k = 0; k < *count; k++)
                    (*list)[k]->lpVtbl->Release((*list)[k]);
                WicFree(*list);
                *list = nullptr;
                *count = 0;
                return hr;
            }
            (*list)[(*count)++] = info;
        }
    }
    return S_OK;
}

/* map a handler class id to its format. */
const GUID *WicFormatOfClass(const GUID *clsid)
{
    for (UINT i = 0; i < WIC_ARRAY_SIZE(g_classes); i++) {
        if (IsEqualGUID(g_classes[i].reader, clsid) || IsEqualGUID(g_classes[i].writer, clsid))
            return g_classes[i].format;
    }
    return nullptr;
}

BOOL WicIsMetadataClass(const GUID *clsid) { return WicFormatOfClass(clsid) != nullptr; }
