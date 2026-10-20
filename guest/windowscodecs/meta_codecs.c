/*
 * the metadata of jpeg and png files: the blocks a decoder reports, and the segments and chunks an encoder writes
 * from the blocks a program gave.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"
#include "meta_codecs.h"
#include <jpeg/jpeg.h>
#include <tiff/tiff.h>
#include <pngio/pngio.h>

/* ------------------------------------------------------------------ small helpers */

static HRESULT add_named(IWICMetadataReader *r, const WCHAR *name, PROPVARIANT *value)
{
    PROPVARIANT id;
    WicPvInit(&id);
    id.vt = VT_LPWSTR;
    id.pwszVal = CoTaskMemAlloc((wcslen(name) + 1) * sizeof(WCHAR));
    if (!id.pwszVal)
        return E_OUTOFMEMORY;
    wcscpy(id.pwszVal, name);
    HRESULT hr = WicMetaAdd(r, nullptr, &id, value);
    PropVariantClear(&id);
    return hr;
}

static HRESULT add_u8(IWICMetadataReader *r, const WCHAR *name, BYTE v)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI1;
    pv.bVal = v;
    return add_named(r, name, &pv);
}

static HRESULT add_u16(IWICMetadataReader *r, const WCHAR *name, UINT16 v)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI2;
    pv.uiVal = v;
    return add_named(r, name, &pv);
}

static HRESULT add_u32(IWICMetadataReader *r, const WCHAR *name, UINT32 v)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI4;
    pv.ulVal = v;
    return add_named(r, name, &pv);
}

static HRESULT add_ansi(IWICMetadataReader *r, const WCHAR *name, const char *text, SIZE_T length)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_LPSTR;
    pv.pszVal = CoTaskMemAlloc(length + 1);
    if (!pv.pszVal)
        return E_OUTOFMEMORY;
    memcpy(pv.pszVal, text, length);
    pv.pszVal[length] = 0;
    HRESULT hr = add_named(r, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

static HRESULT add_utf8_as_wide(IWICMetadataReader *r, const WCHAR *name, const char *text, SIZE_T length)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_LPWSTR;
    pv.pwszVal = CoTaskMemAlloc((length + 1) * sizeof(WCHAR));
    if (!pv.pwszVal)
        return E_OUTOFMEMORY;
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < length;) {
        UINT c = (BYTE)text[i++];
        if (c >= 0xc0 && c < 0xe0 && i < length) {
            c = ((c & 0x1f) << 6) | ((BYTE)text[i++] & 0x3f);
        } else if (c >= 0xe0 && c < 0xf0 && i + 1 < length) {
            c = ((c & 0x0f) << 12) | (((BYTE)text[i] & 0x3f) << 6) | ((BYTE)text[i + 1] & 0x3f);
            i += 2;
        } else if (c >= 0xf0 && i + 2 < length) {
            c = ((c & 0x07) << 18) | (((BYTE)text[i] & 0x3f) << 12) | (((BYTE)text[i + 1] & 0x3f) << 6) |
                ((BYTE)text[i + 2] & 0x3f);
            i += 3;
            c -= 0x10000;
            pv.pwszVal[k++] = (WCHAR)(0xd800 | (c >> 10));
            c = 0xdc00 | (c & 0x3ff);
        }
        pv.pwszVal[k++] = (WCHAR)c;
    }
    pv.pwszVal[k] = 0;
    HRESULT hr = add_named(r, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

static HRESULT add_bytes(IWICMetadataReader *r, const WCHAR *name, const void *data, UINT size)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UI1 | VT_VECTOR;
    pv.caub.pElems = CoTaskMemAlloc(size ? size : 1);
    if (!pv.caub.pElems)
        return E_OUTOFMEMORY;
    memcpy(pv.caub.pElems, data, size);
    pv.caub.cElems = size;
    HRESULT hr = add_named(r, name, &pv);
    PropVariantClear(&pv);
    return hr;
}

/* a block whose only item is another block. */
static HRESULT add_child(IWICMetadataReader *parent, IWICMetadataReader *child)
{
    PROPVARIANT pv;
    WicPvInit(&pv);
    pv.vt = VT_UNKNOWN;
    pv.punkVal = (IUnknown *)child;
    return WicMetaAdd(parent, nullptr, nullptr, &pv);
}

typedef struct ReaderList {
    IWICMetadataReader **items;
    UINT count;
    UINT capacity;
} ReaderList;

static HRESULT list_add(ReaderList *l, IWICMetadataReader *r)
{
    if (l->count == l->capacity) {
        UINT capacity = l->capacity ? l->capacity * 2 : 8;
        IWICMetadataReader **bigger = WicRealloc(l->items, (SIZE_T)capacity * sizeof(*bigger));
        if (!bigger)
            return E_OUTOFMEMORY;
        l->items = bigger;
        l->capacity = capacity;
    }
    l->items[l->count++] = r;
    return S_OK;
}

static void list_free(ReaderList *l)
{
    for (UINT i = 0; i < l->count; i++)
        l->items[i]->lpVtbl->Release(l->items[i]);
    WicFree(l->items);
    memset(l, 0, sizeof(*l));
}

/* ------------------------------------------------------------------ jpeg */

HRESULT WicJpegReaders(const BYTE *data, SIZE_T size, IWICMetadataReader ***out, UINT *count)
{
    ReaderList list = {0};
    MimeJpegSegments it;
    MimeJpegSegment seg;
    HRESULT hr = S_OK;
    BOOL have_luminance = FALSE, have_chrominance = FALSE;
    *out = nullptr;
    *count = 0;
    if (mime_jpeg_segments_init(&it, data, size) != 0)
        return WINCODEC_ERR_BADIMAGE;
    while (SUCCEEDED(hr) && mime_jpeg_next_segment(&it, &seg) == 1) {
        IWICMetadataReader *r = nullptr;
        if (seg.marker == 0xE0 && seg.size >= 5 && !memcmp(seg.data, "JFIF\0", 5)) {
            hr = WicMetaReaderCreate(&GUID_MetadataFormatApp0, &r);
        } else if (seg.marker == 0xE1 && seg.size > 6 && !memcmp(seg.data, "Exif\0\0", 6)) {
            MimeTiff tiff;
            if (mime_tiff_open_exif(&tiff, seg.data + 6, seg.size - 6) != 0)
                continue;
            hr = WicMetaReaderCreate(&GUID_MetadataFormatApp1, &r);
            if (SUCCEEDED(hr)) {
                IWICMetadataReader *ifd;
                if (SUCCEEDED(WicIfdFromTiffIndex(&GUID_MetadataFormatIfd, &tiff, 0, &ifd))) {
                    add_child(r, ifd);
                    ifd->lpVtbl->Release(ifd);
                }
                IWICMetadataReader *thumb;
                if (SUCCEEDED(WicIfdFromTiffIndex(&GUID_MetadataFormatThumbnail, &tiff, 1, &thumb))) {
                    add_child(r, thumb);
                    thumb->lpVtbl->Release(thumb);
                }
            }
        } else if (seg.marker == 0xE1 && seg.size > 29 && !memcmp(seg.data, "http://ns.adobe.com/xap/1.0/\0", 29)) {
            hr = WicMetaReaderCreate(&GUID_MetadataFormatApp1, &r);
            if (SUCCEEDED(hr)) {
                IWICMetadataReader *xmp;
                if (SUCCEEDED(WicXmpFromPacket(seg.data + 29, seg.size - 29, &xmp))) {
                    add_child(r, xmp);
                    xmp->lpVtbl->Release(xmp);
                }
            }
        } else if (seg.marker == 0xFE) {
            hr = WicMetaReaderCreate(&GUID_MetadataFormatJpegComment, &r);
            if (SUCCEEDED(hr))
                hr = add_ansi(r, L"TextEntry", (const char *)seg.data, seg.size);
        } else if (seg.marker == 0xDB) {
            /* a table of 64 entries, 8 or 16 bits each, the first is luminance and the second chrominance. */
            const BYTE *p = seg.data;
            UINT left = seg.size;
            while (left > 64 && SUCCEEDED(hr)) {
                UINT pq = p[0] >> 4, tq = p[0] & 15;
                UINT bytes = 1 + 64 * (pq ? 2 : 1);
                if (left < bytes)
                    break;
                GUID f;
                BOOL *seen;
                if (tq == 0) {
                    f = GUID_MetadataFormatJpegLuminance;
                    seen = &have_luminance;
                } else if (tq == 1) {
                    f = GUID_MetadataFormatJpegChrominance;
                    seen = &have_chrominance;
                } else {
                    p += bytes;
                    left -= bytes;
                    continue;
                }
                if (!*seen) {
                    *seen = TRUE;
                    IWICMetadataReader *table;
                    hr = WicMetaReaderCreate(&f, &table);
                    if (SUCCEEDED(hr)) {
                        PROPVARIANT pv;
                        WicPvInit(&pv);
                        pv.vt = VT_UI2 | VT_VECTOR;
                        pv.caui.pElems = CoTaskMemAlloc(64 * sizeof(USHORT));
                        if (pv.caui.pElems) {
                            pv.caui.cElems = 64;
                            for (UINT i = 0; i < 64; i++)
                                pv.caui.pElems[i] = pq ? (USHORT)((p[1 + i * 2] << 8) | p[2 + i * 2]) : p[1 + i];
                            hr = add_named(table, L"TableEntry", &pv);
                            PropVariantClear(&pv);
                        } else {
                            hr = E_OUTOFMEMORY;
                        }
                        if (SUCCEEDED(hr))
                            hr = list_add(&list, table);
                        else
                            table->lpVtbl->Release(table);
                    }
                }
                p += bytes;
                left -= bytes;
            }
            continue;
        } else {
            continue;
        }
        if (SUCCEEDED(hr) && r)
            hr = list_add(&list, r);
        else if (r)
            r->lpVtbl->Release(r);
    }
    if (FAILED(hr)) {
        list_free(&list);
        return hr;
    }
    *out = list.items ? list.items : WicAlloc(8);
    *count = list.count;
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* the app segments of the blocks a program set. the buffers are in the list and freed with it. */
struct JpegSegmentList {
    MimeJpegAppSegment *segments;
    UINT count;
    UINT capacity;
    BYTE **buffers;
    UINT buffer_count;
};

HRESULT WicJpegSegments(IWICMetadataBlockReader *blocks, JpegSegmentList **out)
{
    JpegSegmentList *l = WicAllocZero(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    *out = l;
    if (!blocks)
        return S_OK;
    UINT n = 0;
    blocks->lpVtbl->GetCount(blocks, &n);
    l->segments = WicAllocZero(((SIZE_T)n + 2) * sizeof(MimeJpegAppSegment));
    l->buffers = WicAllocZero(((SIZE_T)n + 2) * sizeof(BYTE *));
    if (!l->segments || !l->buffers)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < n; i++) {
        IWICMetadataReader *r = nullptr;
        GUID f;
        if (FAILED(blocks->lpVtbl->GetReaderByIndex(blocks, i, &r)) || !r)
            continue;
        if (SUCCEEDED(r->lpVtbl->GetMetadataFormat(r, &f))) {
            if (IsEqualGUID(&f, &GUID_MetadataFormatApp1)) {
                UINT items = WicMetaCount(r);
                for (UINT k = 0; k < items; k++) {
                    const WicMetaItem *item = WicMetaItemAt(r, k);
                    IWICMetadataReader *sub = nullptr;
                    GUID sf;
                    if (item->value.vt != VT_UNKNOWN || !item->value.punkVal ||
                        FAILED(item->value.punkVal->lpVtbl->QueryInterface(item->value.punkVal, &IID_IWICMetadataReader,
                                                                          (void **)&sub)))
                        continue;
                    if (SUCCEEDED(sub->lpVtbl->GetMetadataFormat(sub, &sf)) && IsEqualGUID(&sf, &GUID_MetadataFormatIfd)) {
                        BYTE *blob;
                        SIZE_T length;
                        if (SUCCEEDED(WicIfdToTiff(sub, FALSE, TRUE, &blob, &length)) && length <= 65533) {
                            l->buffers[l->buffer_count++] = blob;
                            l->segments[l->count].marker = 0xE1;
                            l->segments[l->count].data = blob;
                            l->segments[l->count].size = (uint32_t)length;
                            l->count++;
                        } else if (blob) {
                            WicFree(blob);
                        }
                    } else if (SUCCEEDED(sub->lpVtbl->GetMetadataFormat(sub, &sf)) &&
                               IsEqualGUID(&sf, &GUID_MetadataFormatXMP)) {
                        BYTE *packet;
                        SIZE_T length;
                        if (SUCCEEDED(WicXmpToPacket(sub, TRUE, &packet, &length)) && length <= 65533) {
                            l->buffers[l->buffer_count++] = packet;
                            l->segments[l->count].marker = 0xE1;
                            l->segments[l->count].data = packet;
                            l->segments[l->count].size = (uint32_t)length;
                            l->count++;
                        } else if (packet) {
                            WicFree(packet);
                        }
                    }
                    sub->lpVtbl->Release(sub);
                }
            } else if (IsEqualGUID(&f, &GUID_MetadataFormatJpegComment)) {
                char *text;
                if (WicMetaGetAnsi(r, L"TextEntry", &text)) {
                    SIZE_T length = strlen(text);
                    if (length <= 65533) {
                        l->buffers[l->buffer_count++] = (BYTE *)text;
                        l->segments[l->count].marker = 0xFE;
                        l->segments[l->count].data = text;
                        l->segments[l->count].size = (uint32_t)length;
                        l->count++;
                    } else {
                        WicFree(text);
                    }
                }
            }
        }
        r->lpVtbl->Release(r);
    }
    return S_OK;
}

void WicJpegSegmentsFree(JpegSegmentList *l)
{
    if (!l)
        return;
    for (UINT i = 0; i < l->buffer_count; i++)
        WicFree(l->buffers[i]);
    WicFree(l->buffers);
    WicFree(l->segments);
    WicFree(l);
}

const MimeJpegAppSegment *WicJpegSegmentsData(const JpegSegmentList *l, UINT *count)
{
    *count = l->count;
    return l->segments;
}

/* ------------------------------------------------------------------ png */

HRESULT WicPngReaders(MimePngioDecoder *dec, IWICMetadataReader ***out, UINT *count)
{
    const MimePngioInfo *info = mime_pngio_info(dec);
    ReaderList list = {0};
    HRESULT hr = S_OK;
    *out = nullptr;
    *count = 0;
#define NEW(format, var)                                                                                               \
    IWICMetadataReader *var = nullptr;                                                                                 \
    hr = WicMetaReaderCreate(&format, &var);                                                                           \
    if (FAILED(hr))                                                                                                    \
        goto fail;
#define PUSH(var)                                                                                                      \
    hr = list_add(&list, var);                                                                                         \
    if (FAILED(hr)) {                                                                                                  \
        var->lpVtbl->Release(var);                                                                                     \
        goto fail;                                                                                                     \
    }
    if (info->has_gama) {
        NEW(GUID_MetadataFormatChunkgAMA, r);
        hr = add_u32(r, L"ImageGamma", info->gama);
        PUSH(r);
    }
    if (info->has_chrm) {
        NEW(GUID_MetadataFormatChunkcHRM, r);
        static const WCHAR *const names[8] = {L"WhitePointX", L"WhitePointY", L"RedX",  L"RedY",
                                              L"GreenX",      L"GreenY",      L"BlueX", L"BlueY"};
        for (int i = 0; i < 8 && SUCCEEDED(hr); i++)
            hr = add_u32(r, names[i], info->chrm[i]);
        PUSH(r);
    }
    if (info->has_srgb) {
        NEW(GUID_MetadataFormatChunksRGB, r);
        hr = add_u8(r, L"RenderingIntent", info->srgb_intent);
        PUSH(r);
    }
    if (info->has_iccp) {
        NEW(GUID_MetadataFormatChunkiCCP, r);
        hr = add_ansi(r, L"ProfileName", info->iccp_name, strlen(info->iccp_name));
        void *profile = nullptr;
        size_t length = 0;
        if (SUCCEEDED(hr) && mime_pngio_iccp_data(dec, &profile, &length) == MIME_PNGIO_OK) {
            hr = add_bytes(r, L"ProfileData", profile, (UINT)length);
            mime_pngio_free(dec, profile);
        }
        PUSH(r);
    }
    if (info->has_bkgd) {
        NEW(GUID_MetadataFormatChunkbKGD, r);
        if (info->color_type == MIME_PNGIO_COLOR_PALETTE) {
            hr = add_u8(r, L"BackgroundColor", (BYTE)info->bkgd[0]);
        } else {
            BYTE rgb[3] = {(BYTE)info->bkgd[0], (BYTE)info->bkgd[1], (BYTE)info->bkgd[2]};
            hr = add_bytes(r, L"BackgroundColor", rgb, info->color_type == MIME_PNGIO_COLOR_GRAY ||
                                                       info->color_type == MIME_PNGIO_COLOR_GRAY_ALPHA ? 1 : 3);
        }
        PUSH(r);
    }
    if (info->has_time) {
        NEW(GUID_MetadataFormatChunktIME, r);
        hr = add_u16(r, L"Year", info->time.year);
        if (SUCCEEDED(hr))
            hr = add_u8(r, L"Month", info->time.month);
        if (SUCCEEDED(hr))
            hr = add_u8(r, L"Day", info->time.day);
        if (SUCCEEDED(hr))
            hr = add_u8(r, L"Hour", info->time.hour);
        if (SUCCEEDED(hr))
            hr = add_u8(r, L"Minute", info->time.minute);
        if (SUCCEEDED(hr))
            hr = add_u8(r, L"Second", info->time.second);
        PUSH(r);
    }
    if (info->has_exif) {
        IWICMetadataReader *ifd;
        if (SUCCEEDED(WicIfdFromTiff(&GUID_MetadataFormatIfd, info->exif, info->exif_size, &ifd))) {
            if (FAILED(list_add(&list, ifd))) {
                ifd->lpVtbl->Release(ifd);
                hr = E_OUTOFMEMORY;
                goto fail;
            }
        }
    }
    for (UINT i = 0; i < info->text_count; i++) {
        MimePngioText t;
        void *data = nullptr;
        size_t length = 0;
        if (mime_pngio_text(dec, i, &t) != MIME_PNGIO_OK)
            continue;
        if (mime_pngio_text_data(dec, i, &data, &length) != MIME_PNGIO_OK)
            continue;
        if (t.kind == MIME_PNGIO_TEXT_ITXT) {
            IWICMetadataReader *r = nullptr;
            hr = WicMetaReaderCreate(&GUID_MetadataFormatChunkiTXt, &r);
            if (SUCCEEDED(hr)) {
                hr = add_ansi(r, L"Keyword", t.keyword, strlen(t.keyword));
                if (SUCCEEDED(hr))
                    hr = add_u8(r, L"CompressionFlag", t.compressed ? 1 : 0);
                if (SUCCEEDED(hr))
                    hr = add_ansi(r, L"LanguageTag", t.language, strlen(t.language));
                if (SUCCEEDED(hr))
                    hr = add_utf8_as_wide(r, L"TranslatedKeyword", t.translated_keyword, strlen(t.translated_keyword));
                if (SUCCEEDED(hr))
                    hr = add_utf8_as_wide(r, L"TextEntry", data, length);
                if (SUCCEEDED(hr))
                    hr = list_add(&list, r);
                if (FAILED(hr))
                    r->lpVtbl->Release(r);
            }
        } else {
            IWICMetadataReader *r = nullptr;
            hr = WicMetaReaderCreate(&GUID_MetadataFormatChunktEXt, &r);
            if (SUCCEEDED(hr)) {
                hr = add_ansi(r, L"Keyword", t.keyword, strlen(t.keyword));
                if (SUCCEEDED(hr))
                    hr = add_ansi(r, L"TextEntry", data, length);
                if (SUCCEEDED(hr))
                    hr = list_add(&list, r);
                if (FAILED(hr))
                    r->lpVtbl->Release(r);
            }
        }
        mime_pngio_free(dec, data);
        if (FAILED(hr))
            goto fail;
    }
#undef NEW
#undef PUSH
    *out = list.items ? list.items : WicAlloc(8);
    *count = list.count;
    return *out ? S_OK : E_OUTOFMEMORY;
fail:
    list_free(&list);
    return hr;
}

struct PngMeta {
    MimePngioTextItem *texts;
    UINT text_count;
    char **owned;
    UINT owned_count;
    UINT owned_capacity;
    BYTE *profile;
    BYTE *exif;
};

static char *pm_own(PngMeta *m, char *p)
{
    if (!p)
        return nullptr;
    if (m->owned_count == m->owned_capacity) {
        UINT capacity = m->owned_capacity ? m->owned_capacity * 2 : 16;
        char **bigger = WicRealloc(m->owned, (SIZE_T)capacity * sizeof(char *));
        if (!bigger) {
            WicFree(p);
            return nullptr;
        }
        m->owned = bigger;
        m->owned_capacity = capacity;
    }
    m->owned[m->owned_count++] = p;
    return p;
}

static char *wide_to_utf8(const WCHAR *w, SIZE_T *length)
{
    SIZE_T n = wcslen(w);
    char *out = WicAlloc(n * 3 + 1);
    if (!out)
        return nullptr;
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < n; i++) {
        UINT c = w[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && w[i + 1] >= 0xdc00 && w[i + 1] < 0xe000) {
            c = 0x10000 + ((c & 0x3ff) << 10) + (w[i + 1] & 0x3ff);
            i++;
        }
        if (c < 0x80) {
            out[k++] = (char)c;
        } else if (c < 0x800) {
            out[k++] = (char)(0xc0 | (c >> 6));
            out[k++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out[k++] = (char)(0xe0 | (c >> 12));
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[k++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[k++] = (char)(0xf0 | (c >> 18));
            out[k++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[k++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[k] = 0;
    *length = k;
    return out;
}

/* copies the blocks of a frame writer into the parameters of the png encoder. */
void WicPngApplyMetadata(PngMeta **out, MimePngioEncode *p, IWICMetadataBlockReader *blocks)
{
    *out = nullptr;
    if (!blocks)
        return;
    PngMeta *m = WicAllocZero(sizeof(*m));
    if (!m)
        return;
    *out = m;
    UINT n = 0;
    blocks->lpVtbl->GetCount(blocks, &n);
    m->texts = WicAllocZero(((SIZE_T)n + 1) * sizeof(MimePngioTextItem));
    if (!m->texts)
        return;
    for (UINT i = 0; i < n; i++) {
        IWICMetadataReader *r = nullptr;
        GUID f;
        if (FAILED(blocks->lpVtbl->GetReaderByIndex(blocks, i, &r)) || !r)
            continue;
        if (FAILED(r->lpVtbl->GetMetadataFormat(r, &f))) {
            r->lpVtbl->Release(r);
            continue;
        }
        UINT32 v;
        if (IsEqualGUID(&f, &GUID_MetadataFormatChunkgAMA)) {
            if (WicMetaGetU32(r, L"ImageGamma", &v)) {
                p->has_gama = true;
                p->gama = v;
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunkcHRM)) {
            static const WCHAR *const names[8] = {L"WhitePointX", L"WhitePointY", L"RedX",  L"RedY",
                                                  L"GreenX",      L"GreenY",      L"BlueX", L"BlueY"};
            BOOL all = TRUE;
            for (int k = 0; k < 8; k++)
                all = all && WicMetaGetU32(r, names[k], &p->chrm[k]);
            p->has_chrm = all;
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunksRGB)) {
            if (WicMetaGetU32(r, L"RenderingIntent", &v)) {
                p->has_srgb = true;
                p->srgb_intent = (uint8_t)v;
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunkiCCP)) {
            char *name;
            UINT size;
            BYTE *profile;
            if (WicMetaGetAnsi(r, L"ProfileName", &name) && WicMetaGetBytes(r, L"ProfileData", &profile, &size)) {
                p->iccp_name = pm_own(m, name);
                m->profile = profile;
                p->iccp_data = profile;
                p->iccp_size = size;
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunktIME)) {
            UINT32 y, mo, d, h, mi, s;
            if (WicMetaGetU32(r, L"Year", &y) && WicMetaGetU32(r, L"Month", &mo) && WicMetaGetU32(r, L"Day", &d) &&
                WicMetaGetU32(r, L"Hour", &h) && WicMetaGetU32(r, L"Minute", &mi) && WicMetaGetU32(r, L"Second", &s)) {
                p->has_time = true;
                p->time.year = (uint16_t)y;
                p->time.month = (uint8_t)mo;
                p->time.day = (uint8_t)d;
                p->time.hour = (uint8_t)h;
                p->time.minute = (uint8_t)mi;
                p->time.second = (uint8_t)s;
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunktEXt)) {
            char *keyword, *text;
            if (WicMetaGetAnsi(r, L"Keyword", &keyword) && WicMetaGetAnsi(r, L"TextEntry", &text)) {
                MimePngioTextItem *t = &m->texts[m->text_count++];
                t->kind = MIME_PNGIO_TEXT_LATIN1;
                t->keyword = pm_own(m, keyword);
                t->text = pm_own(m, text);
                t->text_size = text ? strlen(text) : 0;
                t->compress = false;
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatChunkiTXt)) {
            char *keyword, *language = nullptr;
            PROPVARIANT pv, id;
            WicPvInit(&id);
            id.vt = VT_LPWSTR;
            id.pwszVal = CoTaskMemAlloc(10 * sizeof(WCHAR));
            if (id.pwszVal) {
                wcscpy(id.pwszVal, L"TextEntry");
                WicPvInit(&pv);
                if (SUCCEEDED(r->lpVtbl->GetValue(r, nullptr, &id, &pv)) && WicMetaGetAnsi(r, L"Keyword", &keyword)) {
                    SIZE_T length = 0;
                    char *utf8 = pv.vt == VT_LPWSTR && pv.pwszVal ? wide_to_utf8(pv.pwszVal, &length) : nullptr;
                    if (!utf8 && pv.vt == VT_LPSTR && pv.pszVal) {
                        length = strlen(pv.pszVal);
                        utf8 = WicAlloc(length + 1);
                        if (utf8)
                            memcpy(utf8, pv.pszVal, length + 1);
                    }
                    if (utf8) {
                        MimePngioTextItem *t = &m->texts[m->text_count++];
                        t->kind = MIME_PNGIO_TEXT_ITXT;
                        t->keyword = pm_own(m, keyword);
                        t->text = pm_own(m, utf8);
                        t->text_size = length;
                        UINT32 compress = 0;
                        WicMetaGetU32(r, L"CompressionFlag", &compress);
                        t->compress = compress != 0;
                        if (WicMetaGetAnsi(r, L"LanguageTag", &language))
                            t->language = pm_own(m, language);
                    }
                }
                PropVariantClear(&pv);
                PropVariantClear(&id);
            }
        } else if (IsEqualGUID(&f, &GUID_MetadataFormatIfd)) {
            BYTE *blob;
            SIZE_T length;
            if (SUCCEEDED(WicIfdToTiff(r, FALSE, FALSE, &blob, &length))) {
                m->exif = blob;
                p->exif = blob;
                p->exif_size = length;
            }
        }
        r->lpVtbl->Release(r);
    }
    p->texts = m->texts;
    p->text_count = m->text_count;
}

void WicPngMetadataFree(PngMeta *m)
{
    if (!m)
        return;
    for (UINT i = 0; i < m->owned_count; i++)
        WicFree(m->owned[i]);
    WicFree(m->owned);
    WicFree(m->texts);
    WicFree(m->profile);
    WicFree(m->exif);
    WicFree(m);
}
