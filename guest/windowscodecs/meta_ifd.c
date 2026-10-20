/*
 * ifd metadata: the entries of tiff and exif directories as items with a numeric id and a typed value, and back to
 * the bytes of a directory. a pointer to another directory becomes an item whose value is a block of its own.
 */
#include "wic_codec.h"
#include "wic_meta.h"
#include "meta_internal.h"
#include "meta_codecs.h"

#define MAX_DEPTH 4

/* ------------------------------------------------------------------ from tiff */

static void *pv_alloc(SIZE_T n) { return CoTaskMemAlloc(n ? n : 1); }

static UINT64 rd(const MimeTiffEntry *e, UINT64 i, UINT size)
{
    UINT64 v = 0;
    const BYTE *p = e->data + i * size;
    if (e->big_endian) {
        for (UINT k = 0; k < size; k++)
            v = (v << 8) | p[k];
    } else {
        for (UINT k = size; k > 0; k--)
            v = (v << 8) | p[k - 1];
    }
    return v;
}

/* the value of an entry in the form wic gives it. */
static HRESULT entry_value(const MimeTiffEntry *e, PROPVARIANT *pv)
{
    WicPvInit(pv);
    if (!e->data || e->count > 0x1000000u)
        return E_FAIL;
    UINT n = (UINT)e->count;
    VARTYPE vt;
    UINT size;
    switch (e->type) {
    case MIME_TIFF_TYPE_BYTE:
        vt = VT_UI1;
        size = 1;
        break;
    case MIME_TIFF_TYPE_SBYTE:
        vt = VT_I1;
        size = 1;
        break;
    case MIME_TIFF_TYPE_SHORT:
        vt = VT_UI2;
        size = 2;
        break;
    case MIME_TIFF_TYPE_SSHORT:
        vt = VT_I2;
        size = 2;
        break;
    case MIME_TIFF_TYPE_LONG:
    case MIME_TIFF_TYPE_IFD:
        vt = VT_UI4;
        size = 4;
        break;
    case MIME_TIFF_TYPE_SLONG:
        vt = VT_I4;
        size = 4;
        break;
    case MIME_TIFF_TYPE_RATIONAL:
        vt = VT_UI8;
        size = 8;
        break;
    case MIME_TIFF_TYPE_SRATIONAL:
        vt = VT_I8;
        size = 8;
        break;
    case MIME_TIFF_TYPE_FLOAT:
        vt = VT_R4;
        size = 4;
        break;
    case MIME_TIFF_TYPE_DOUBLE:
        vt = VT_R8;
        size = 8;
        break;
    case MIME_TIFF_TYPE_ASCII: {
        const char *text;
        size_t length;
        if (!mime_tiff_entry_string(e, &text, &length))
            return E_FAIL;
        pv->vt = VT_LPSTR;
        pv->pszVal = pv_alloc(length + 1);
        if (!pv->pszVal)
            return E_OUTOFMEMORY;
        memcpy(pv->pszVal, text, length);
        pv->pszVal[length] = 0;
        return S_OK;
    }
    case MIME_TIFF_TYPE_UNDEFINED:
        pv->vt = VT_BLOB;
        pv->blob.cbSize = n;
        pv->blob.pBlobData = pv_alloc(n);
        if (!pv->blob.pBlobData) {
            pv->vt = VT_EMPTY;
            return E_OUTOFMEMORY;
        }
        memcpy(pv->blob.pBlobData, e->data, n);
        return S_OK;
    default:
        return E_FAIL;
    }
    if (n == 0)
        return E_FAIL;
    BOOL vector = n > 1;
    BYTE *buffer = pv_alloc((SIZE_T)n * (vt == VT_UI8 || vt == VT_I8 || vt == VT_R8 ? 8 : size));
    if (!buffer)
        return E_OUTOFMEMORY;
    UINT item = size;
    for (UINT i = 0; i < n; i++) {
        if (e->type == MIME_TIFF_TYPE_RATIONAL || e->type == MIME_TIFF_TYPE_SRATIONAL) {
            UINT64 num = rd(e, (UINT64)i * 2, 4), den = rd(e, (UINT64)i * 2 + 1, 4);
            ((UINT64 *)buffer)[i] = num | (den << 32);
        } else if (size == 1) {
            buffer[i] = (BYTE)rd(e, i, 1);
        } else if (size == 2) {
            ((UINT16 *)buffer)[i] = (UINT16)rd(e, i, 2);
        } else if (size == 4) {
            ((UINT32 *)buffer)[i] = (UINT32)rd(e, i, 4);
        } else {
            ((UINT64 *)buffer)[i] = rd(e, i, 8);
        }
    }
    (void)item;
    if (!vector) {
        pv->vt = vt;
        switch (vt) {
        case VT_UI1:
        case VT_I1:
            pv->bVal = buffer[0];
            break;
        case VT_UI2:
        case VT_I2:
            pv->uiVal = *(UINT16 *)buffer;
            break;
        case VT_UI4:
        case VT_I4:
        case VT_R4:
            pv->ulVal = *(UINT32 *)buffer;
            break;
        default:
            pv->uhVal.QuadPart = *(UINT64 *)buffer;
        }
        CoTaskMemFree(buffer);
        return S_OK;
    }
    pv->vt = vt | VT_VECTOR;
    pv->caub.cElems = n;
    pv->caub.pElems = buffer;
    return S_OK;
}

static HRESULT ifd_from(const GUID *format, const MimeTiffIfd *ifd, UINT depth, IWICMetadataReader **out);

static const GUID *sub_format(UINT16 tag)
{
    switch (tag) {
    case MIME_TIFF_TAG_EXIF_IFD:
        return &GUID_MetadataFormatExif;
    case MIME_TIFF_TAG_GPS_IFD:
        return &GUID_MetadataFormatGps;
    case MIME_TIFF_TAG_INTEROP_IFD:
        return &GUID_MetadataFormatInterop;
    case MIME_TIFF_TAG_SUB_IFDS:
        return &GUID_MetadataFormatSubIfd;
    default:
        return nullptr;
    }
}

static BOOL skipped_tag(UINT16 tag)
{
    switch (tag) {
    case MIME_TIFF_TAG_STRIP_OFFSETS:
    case MIME_TIFF_TAG_STRIP_BYTE_COUNTS:
    case MIME_TIFF_TAG_TILE_OFFSETS:
    case MIME_TIFF_TAG_TILE_BYTE_COUNTS:
    case MIME_TIFF_TAG_JPEG_INTERCHANGE_FORMAT:
    case MIME_TIFF_TAG_JPEG_INTERCHANGE_LENGTH:
        return TRUE;
    default:
        return FALSE;
    }
}

static HRESULT ifd_from(const GUID *format, const MimeTiffIfd *ifd, UINT depth, IWICMetadataReader **out)
{
    IWICMetadataReader *reader;
    HRESULT hr = WicMetaReaderCreate(format, &reader);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < ifd->count; i++) {
        MimeTiffEntry e;
        if (mime_tiff_ifd_entry(ifd, i, &e) != 0 || skipped_tag(e.tag))
            continue;
        PROPVARIANT id, value;
        WicPvInit(&id);
        id.vt = VT_UI2;
        id.uiVal = e.tag;
        const GUID *sub = sub_format(e.tag);
        if (sub && depth < MAX_DEPTH) {
            UINT64 count = e.tag == MIME_TIFF_TAG_SUB_IFDS ? e.count : 1;
            for (UINT64 k = 0; k < count && k < 8; k++) {
                MimeTiffIfd child;
                IWICMetadataReader *block;
                if (mime_tiff_ifd_sub(ifd, e.tag, k, &child) != 0)
                    break;
                if (FAILED(ifd_from(sub, &child, depth + 1, &block)))
                    break;
                WicPvInit(&value);
                value.vt = VT_UNKNOWN;
                value.punkVal = (IUnknown *)block;
                WicMetaAdd(reader, nullptr, &id, &value);
                block->lpVtbl->Release(block);
            }
            continue;
        }
        if (sub)
            continue;
        if (FAILED(entry_value(&e, &value)))
            continue;
        WicMetaAdd(reader, nullptr, &id, &value);
        PropVariantClear(&value);
    }
    *out = reader;
    return S_OK;
}

HRESULT WicIfdFromTiff(const GUID *format, const void *data, size_t size, IWICMetadataReader **out)
{
    MimeTiff tiff;
    MimeTiffIfd ifd;
    if (mime_tiff_open_exif(&tiff, data, size) != 0 || mime_tiff_ifd_index(&tiff, 0, &ifd) != 0)
        return WINCODEC_ERR_BADMETADATAHEADER;
    return ifd_from(format, &ifd, 0, out);
}

/* the ifd at the position in the chain of a parsed blob, as a block of the format. */
HRESULT WicIfdFromTiffIndex(const GUID *format, const MimeTiff *tiff, UINT index, IWICMetadataReader **out)
{
    MimeTiffIfd ifd;
    if (mime_tiff_ifd_index(tiff, index, &ifd) != 0)
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    return ifd_from(format, &ifd, 0, out);
}

HRESULT WicIfdFromParsed(const GUID *format, const MimeTiffIfd *ifd, IWICMetadataReader **out)
{
    return ifd_from(format, ifd, 0, out);
}

/* ------------------------------------------------------------------ to tiff */

typedef struct WriteIfd {
    MimeTiffWEntry *entries;
    UINT count;
    UINT capacity;
    BYTE **buffers; /* allocations the entries point into. */
    UINT buffer_count;
    UINT buffer_capacity;
    MimeTiffWIfd *subs;
    UINT sub_count;
    UINT sub_capacity;
    MimeTiffWIfd ifd;
} WriteIfd;

typedef struct WriteState {
    BYTE **allocations;
    UINT count;
    UINT capacity;
    BOOL failed;
} WriteState;

static void *ws_alloc(WriteState *ws, SIZE_T size)
{
    if (ws->count == ws->capacity) {
        UINT capacity = ws->capacity ? ws->capacity * 2 : 32;
        BYTE **bigger = WicRealloc(ws->allocations, (SIZE_T)capacity * sizeof(BYTE *));
        if (!bigger) {
            ws->failed = TRUE;
            return nullptr;
        }
        ws->allocations = bigger;
        ws->capacity = capacity;
    }
    BYTE *p = WicAllocZero(size);
    if (!p) {
        ws->failed = TRUE;
        return nullptr;
    }
    ws->allocations[ws->count++] = p;
    return p;
}

static void ws_adopt(WriteState *ws, void *p)
{
    if (!p)
        return;
    if (ws->count == ws->capacity) {
        UINT capacity = ws->capacity ? ws->capacity * 2 : 32;
        BYTE **bigger = WicRealloc(ws->allocations, (SIZE_T)capacity * sizeof(BYTE *));
        if (!bigger) {
            ws->failed = TRUE;
            WicFree(p);
            return;
        }
        ws->allocations = bigger;
        ws->capacity = capacity;
    }
    ws->allocations[ws->count++] = p;
}

static BOOL type_of(const PROPVARIANT *v, UINT *type, UINT *size, BOOL *vector)
{
    VARTYPE vt = v->vt & ~VT_VECTOR;
    *vector = (v->vt & VT_VECTOR) != 0;
    switch (vt) {
    case VT_UI1:
        *type = MIME_TIFF_TYPE_BYTE;
        *size = 1;
        return TRUE;
    case VT_I1:
        *type = MIME_TIFF_TYPE_SBYTE;
        *size = 1;
        return TRUE;
    case VT_UI2:
        *type = MIME_TIFF_TYPE_SHORT;
        *size = 2;
        return TRUE;
    case VT_I2:
        *type = MIME_TIFF_TYPE_SSHORT;
        *size = 2;
        return TRUE;
    case VT_UI4:
        *type = MIME_TIFF_TYPE_LONG;
        *size = 4;
        return TRUE;
    case VT_I4:
        *type = MIME_TIFF_TYPE_SLONG;
        *size = 4;
        return TRUE;
    case VT_UI8:
        *type = MIME_TIFF_TYPE_RATIONAL;
        *size = 8;
        return TRUE;
    case VT_I8:
        *type = MIME_TIFF_TYPE_SRATIONAL;
        *size = 8;
        return TRUE;
    case VT_R4:
        *type = MIME_TIFF_TYPE_FLOAT;
        *size = 4;
        return TRUE;
    case VT_R8:
        *type = MIME_TIFF_TYPE_DOUBLE;
        *size = 8;
        return TRUE;
    default:
        return FALSE;
    }
}

static HRESULT build_ifd(WriteState *ws, IWICMetadataReader *reader, WriteIfd *w, UINT depth);

static BOOL add_entry(WriteIfd *w, const MimeTiffWEntry *e)
{
    if (w->count == w->capacity) {
        UINT capacity = w->capacity ? w->capacity * 2 : 16;
        MimeTiffWEntry *bigger = WicRealloc(w->entries, (SIZE_T)capacity * sizeof(*bigger));
        if (!bigger)
            return FALSE;
        w->entries = bigger;
        w->capacity = capacity;
    }
    w->entries[w->count++] = *e;
    return TRUE;
}

static HRESULT build_ifd(WriteState *ws, IWICMetadataReader *reader, WriteIfd *w, UINT depth)
{
    UINT count = WicMetaCount(reader);
    /* the sub ifds are built first, as arrays that stay put. */
    UINT sub_slots = 0;
    for (UINT i = 0; i < count; i++) {
        const WicMetaItem *item = WicMetaItemAt(reader, i);
        if (item->value.vt == VT_UNKNOWN)
            sub_slots++;
    }
    WriteIfd *subs = sub_slots ? ws_alloc(ws, sizeof(WriteIfd) * sub_slots) : nullptr;
    if (sub_slots && !subs)
        return E_OUTOFMEMORY;
    UINT sub_used = 0;
    for (UINT i = 0; i < count; i++) {
        const WicMetaItem *item = WicMetaItemAt(reader, i);
        if (item->id.vt != VT_UI2 && item->id.vt != VT_UI4)
            continue;
        UINT16 tag = (UINT16)(item->id.vt == VT_UI2 ? item->id.uiVal : item->id.ulVal);
        MimeTiffWEntry e;
        memset(&e, 0, sizeof(e));
        e.tag = tag;
        const PROPVARIANT *v = &item->value;
        if (v->vt == VT_UNKNOWN) {
            IWICMetadataReader *sub = nullptr;
            if (!v->punkVal || depth >= MAX_DEPTH ||
                FAILED(v->punkVal->lpVtbl->QueryInterface(v->punkVal, &IID_IWICMetadataReader, (void **)&sub)))
                continue;
            /* only the pointers of a directory have a place in one. */
            if (tag != MIME_TIFF_TAG_EXIF_IFD && tag != MIME_TIFF_TAG_GPS_IFD && tag != MIME_TIFF_TAG_INTEROP_IFD) {
                sub->lpVtbl->Release(sub);
                continue;
            }
            WriteIfd *s = &subs[sub_used++];
            HRESULT hr = build_ifd(ws, sub, s, depth + 1);
            sub->lpVtbl->Release(sub);
            ws_adopt(ws, s->entries);
            if (FAILED(hr))
                return hr;
            s->ifd.entries = s->entries;
            s->ifd.entry_count = s->count;
            e.sub = &s->ifd;
            if (!add_entry(w, &e))
                return E_OUTOFMEMORY;
            continue;
        }
        if (v->vt == VT_LPSTR || v->vt == VT_LPWSTR) {
            SIZE_T length = v->vt == VT_LPSTR ? (v->pszVal ? strlen(v->pszVal) : 0) : (v->pwszVal ? wcslen(v->pwszVal) : 0);
            BYTE *buffer = ws_alloc(ws, length + 1);
            if (!buffer)
                return E_OUTOFMEMORY;
            for (SIZE_T k = 0; k < length; k++)
                buffer[k] = v->vt == VT_LPSTR ? (BYTE)v->pszVal[k] : (BYTE)v->pwszVal[k];
            e.type = MIME_TIFF_TYPE_ASCII;
            e.count = (uint32_t)length + 1;
            e.data = buffer;
        } else if (v->vt == VT_BLOB) {
            e.type = MIME_TIFF_TYPE_UNDEFINED;
            e.count = v->blob.cbSize;
            e.data = v->blob.pBlobData;
            if (!e.data)
                continue;
        } else {
            UINT type, size;
            BOOL vector;
            if (!type_of(v, &type, &size, &vector))
                continue;
            e.type = (uint16_t)type;
            if (vector) {
                e.count = v->caub.cElems;
                e.data = v->caub.pElems;
                if (!e.data || !e.count)
                    continue;
                if (type == MIME_TIFF_TYPE_RATIONAL || type == MIME_TIFF_TYPE_SRATIONAL) {
                    /* a rational is two longs in the file. */
                    BYTE *buffer = ws_alloc(ws, (SIZE_T)e.count * 8);
                    if (!buffer)
                        return E_OUTOFMEMORY;
                    memcpy(buffer, e.data, (SIZE_T)e.count * 8);
                    e.data = buffer;
                }
            } else {
                BYTE *buffer = ws_alloc(ws, 8);
                if (!buffer)
                    return E_OUTOFMEMORY;
                memcpy(buffer, &v->bVal, size);
                e.count = 1;
                e.data = buffer;
            }
        }
        if (!add_entry(w, &e))
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT WicIfdToTiff(IWICMetadataReader *ifd, BOOL big_endian, BOOL prefix, BYTE **blob, SIZE_T *size)
{
    WriteState ws = {0};
    WriteIfd root;
    memset(&root, 0, sizeof(root));
    HRESULT hr = build_ifd(&ws, ifd, &root, 0);
    if (SUCCEEDED(hr) && ws.failed)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        root.ifd.entries = root.entries;
        root.ifd.entry_count = root.count;
        unsigned flags = (big_endian ? MIME_TIFF_WRITE_BIG_ENDIAN : 0) | (prefix ? MIME_TIFF_WRITE_EXIF_PREFIX : 0);
        void *out = nullptr;
        size_t length = 0;
        int rc = mime_tiff_write(&root.ifd, flags, &WicAllocator, &out, &length);
        if (rc != 0)
            hr = E_FAIL;
        else {
            *blob = out;
            *size = length;
        }
    }
    for (UINT i = 0; i < ws.count; i++)
        WicFree(ws.allocations[i]);
    WicFree(ws.allocations);
    WicFree(root.entries);
    return hr;
}

/* ------------------------------------------------------------------ entries for the tiff encoder */

struct WicIfdSet {
    WriteState ws;
    WriteIfd root;
    MimeTiffWEntry *entries;
    UINT count;
    const MimeTiffWIfd *exif;
    const MimeTiffWIfd *gps;
};

HRESULT WicIfdSetCreate(IWICMetadataReader *ifd, WicIfdSet **out)
{
    WicIfdSet *set = WicAllocZero(sizeof(*set));
    if (!set)
        return E_OUTOFMEMORY;
    HRESULT hr = build_ifd(&set->ws, ifd, &set->root, 0);
    if (SUCCEEDED(hr) && set->ws.failed)
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        WicIfdSetFree(set);
        return hr;
    }
    /* the pointers to directories are given to the encoder apart from the other entries. */
    set->entries = set->root.entries;
    UINT n = 0;
    for (UINT i = 0; i < set->root.count; i++) {
        MimeTiffWEntry e = set->entries[i];
        if (e.sub && e.tag == MIME_TIFF_TAG_EXIF_IFD)
            set->exif = e.sub;
        else if (e.sub && e.tag == MIME_TIFF_TAG_GPS_IFD)
            set->gps = e.sub;
        else if (!e.sub)
            set->entries[n++] = e;
    }
    set->count = n;
    for (UINT i = 0; i < set->count; i++) {
        /* the generated entries are the encoder's own, so a few tags are not taken from a program. */
        (void)i;
    }
    *out = set;
    return S_OK;
}

void WicIfdSetFree(WicIfdSet *set)
{
    if (!set)
        return;
    for (UINT i = 0; i < set->ws.count; i++)
        WicFree(set->ws.allocations[i]);
    WicFree(set->ws.allocations);
    WicFree(set->root.entries);
    WicFree(set);
}

const MimeTiffWEntry *WicIfdSetEntries(const WicIfdSet *set, UINT *count)
{
    *count = set->count;
    return set->entries;
}

const MimeTiffWIfd *WicIfdSetExif(const WicIfdSet *set) { return set->exif; }
const MimeTiffWIfd *WicIfdSetGps(const WicIfdSet *set) { return set->gps; }
