/* media types over the attribute store, and their conversion to and from wave formats. */
#include "mfplat_private.h"

typedef struct {
    IMFAttributesVtbl attributes;
    HRESULT(STDMETHODCALLTYPE *GetMajorType)(IMFMediaType *This, GUID *major);
    HRESULT(STDMETHODCALLTYPE *IsCompressedFormat)(IMFMediaType *This, BOOL *compressed);
    HRESULT(STDMETHODCALLTYPE *IsEqual)(IMFMediaType *This, IMFMediaType *type, DWORD *flags);
    HRESULT(STDMETHODCALLTYPE *GetRepresentation)(IMFMediaType *This, GUID representation, void **data);
    HRESULT(STDMETHODCALLTYPE *FreeRepresentation)(IMFMediaType *This, GUID representation, void *data);
    const MFVIDEOFORMAT *(STDMETHODCALLTYPE *GetVideoFormat)(IMFMediaType *This);
    HRESULT(STDMETHODCALLTYPE *GetVideoRepresentation)(IMFMediaType *This, GUID representation, void **data,
                                                       LONG stride);
} MediaTypeVtbl;

/* like windows, one object serves plain and video media types, and keeps the format block it hands out. */
typedef struct MediaType MediaType;

/* the video interface of a plain type: another pointer into the same object, made once and kept. */
typedef struct {
    const MediaTypeVtbl *vtbl;
    MediaType *owner;
} VideoFace;

typedef struct MediaType {
    MfObject object;
    MFVIDEOFORMAT *video;
    VideoFace *face;
} MediaType;
#define VIDEO_FORMAT_ROOM 0x8B0

static void type_destroy(MfObject *object)
{
    CoTaskMemFree(((MediaType *)object)->video);
    MfFree(((MediaType *)object)->face);
}

static const GUID *const MEDIA_TYPE_IIDS[] = {&IID_IMFMediaType, nullptr};
static const MfClass MEDIA_TYPE_CLASS = {MEDIA_TYPE_IIDS, type_destroy};
/* only a type made as a video type answers for the video interface. */
static const GUID *const VIDEO_TYPE_IIDS[] = {&IID_IMFMediaType, &IID_IMFVideoMediaType, nullptr};
static const MfClass VIDEO_TYPE_CLASS = {VIDEO_TYPE_IIDS, type_destroy};

static HRESULT STDMETHODCALLTYPE type_get_major_type(IMFMediaType *iface, GUID *major)
{
    return iface->lpVtbl->GetGUID(iface, &MF_MT_MAJOR_TYPE, major);
}

/* compressed unless every sample is independent. */
static HRESULT STDMETHODCALLTYPE type_is_compressed_format(IMFMediaType *iface, BOOL *compressed)
{
    UINT32 independent = 0;
    if (FAILED(iface->lpVtbl->GetUINT32(iface, &MF_MT_ALL_SAMPLES_INDEPENDENT, &independent)))
        independent = 0;
    *compressed = !independent;
    return S_OK;
}

/* ratios match within a thousandth, or when either type has none. */
static BOOL ratios_equal(IMFMediaType *a, IMFMediaType *b, REFGUID key)
{
    UINT64 x, y;
    if (FAILED(a->lpVtbl->GetUINT64(a, key, &x)) || FAILED(b->lpVtbl->GetUINT64(b, key, &y)))
        return TRUE;
    ULONGLONG ours = (x >> 32) * (DWORD)y, theirs = (y >> 32) * (DWORD)x;
    ULONGLONG difference = ours > theirs ? ours - theirs : theirs - ours;
    return difference <= (ULONGLONG)(DWORD)x * (DWORD)y / 1000;
}

static HRESULT STDMETHODCALLTYPE type_is_equal(IMFMediaType *iface, IMFMediaType *other, DWORD *flags)
{
    static const GUID FORMAT_SKIP[] = {
        MF_MT_USER_DATA, MF_MT_PIXEL_ASPECT_RATIO, MF_MT_FRAME_RATE, MF_MT_FRAME_RATE_RANGE_MIN,
        MF_MT_FRAME_RATE_RANGE_MAX,
    };
    *flags = 0;
    GUID ours, theirs;
    if (FAILED(iface->lpVtbl->GetMajorType(iface, &ours)) || FAILED(other->lpVtbl->GetMajorType(other, &theirs)))
        return E_INVALIDARG;
    if (IsEqualGUID(&ours, &theirs))
        *flags |= MF_MEDIATYPE_EQUAL_MAJOR_TYPES;
    HRESULT ours_hr = iface->lpVtbl->GetGUID(iface, &MF_MT_SUBTYPE, &ours);
    HRESULT theirs_hr = other->lpVtbl->GetGUID(other, &MF_MT_SUBTYPE, &theirs);
    if ((FAILED(ours_hr) && FAILED(theirs_hr)) ||
        (SUCCEEDED(ours_hr) && SUCCEEDED(theirs_hr) && IsEqualGUID(&ours, &theirs)))
        *flags |= MF_MEDIATYPE_EQUAL_FORMAT_TYPES;

    IMFAttributes *attributes = nullptr;
    BOOL same = FALSE;
    HRESULT hr = other->lpVtbl->QueryInterface(other, &IID_IMFAttributes, (void **)&attributes);
    if (SUCCEEDED(hr)) {
        hr = MfStoreCompare(&mf_object(iface)->store, attributes, MF_ATTRIBUTES_MATCH_SMALLER, FORMAT_SKIP,
                            RTL_NUMBER_OF(FORMAT_SKIP), &same);
        attributes->lpVtbl->Release(attributes);
    } else {
        hr = E_INVALIDARG;
    }
    if (FAILED(hr))
        return hr;
    if (same && ratios_equal(iface, other, &MF_MT_FRAME_RATE) && ratios_equal(iface, other, &MF_MT_PIXEL_ASPECT_RATIO))
        *flags |= MF_MEDIATYPE_EQUAL_FORMAT_DATA;

    UINT32 our_size = 0, their_size = 0;
    if (FAILED(iface->lpVtbl->GetBlobSize(iface, &MF_MT_USER_DATA, &our_size)))
        our_size = 0;
    if (FAILED(other->lpVtbl->GetBlobSize(other, &MF_MT_USER_DATA, &their_size)))
        their_size = 0;
    if (our_size == their_size) {
        BOOL equal = TRUE;
        if (our_size) {
            PROPVARIANT value = {};
            hr = iface->lpVtbl->GetItem(iface, &MF_MT_USER_DATA, &value);
            if (FAILED(hr))
                return E_UNEXPECTED;
            hr = other->lpVtbl->CompareItem(other, &MF_MT_USER_DATA, &value, &equal);
            PropVariantClear(&value);
            if (FAILED(hr))
                return hr;
        }
        if (equal)
            *flags |= MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;
    }
    return *flags == 15 ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE type_get_representation(IMFMediaType *iface, GUID representation, void **data)
{
    return MF_E_UNSUPPORTED_REPRESENTATION;
}

static HRESULT STDMETHODCALLTYPE type_free_representation(IMFMediaType *iface, GUID representation, void *data)
{
    return MF_E_UNSUPPORTED_REPRESENTATION;
}

/* the block is built again on every call, in the room the type keeps, so the pointer stays good. */
static const MFVIDEOFORMAT *STDMETHODCALLTYPE type_get_video_format(IMFMediaType *iface)
{
    MediaType *type = (MediaType *)iface;
    const MFVIDEOFORMAT *out = nullptr;
    iface->lpVtbl->LockStore(iface);
    if (!type->video) {
        type->video = CoTaskMemAlloc(VIDEO_FORMAT_ROOM);
        if (type->video)
            memset(type->video, 0, VIDEO_FORMAT_ROOM);
    }
    MFVIDEOFORMAT *made = nullptr;
    UINT32 size = 0;
    if (type->video && SUCCEEDED(MFCreateMFVideoFormatFromMFMediaType(iface, &made, &size))) {
        if (size <= VIDEO_FORMAT_ROOM) {
            memcpy(type->video, made, size);
            out = type->video;
        }
        CoTaskMemFree(made);
    }
    iface->lpVtbl->UnlockStore(iface);
    return out;
}

/* the representation with another stride: the stride of the type is put back afterwards. */
static HRESULT STDMETHODCALLTYPE type_get_video_representation(IMFMediaType *iface, GUID representation, void **data,
                                                               LONG stride)
{
    UINT32 before = 0;
    if (stride) {
        if (FAILED(iface->lpVtbl->GetUINT32(iface, &MF_MT_DEFAULT_STRIDE, &before)))
            before = 0;
        HRESULT hr = iface->lpVtbl->SetUINT32(iface, &MF_MT_DEFAULT_STRIDE, (UINT32)stride);
        if (FAILED(hr))
            return hr;
    }
    HRESULT hr = iface->lpVtbl->GetRepresentation(iface, representation, data);
    if (stride) {
        if (before)
            iface->lpVtbl->SetUINT32(iface, &MF_MT_DEFAULT_STRIDE, before);
        else
            iface->lpVtbl->DeleteItem(iface, &MF_MT_DEFAULT_STRIDE);
    }
    return hr;
}

#define OWNER(This) ((IMFMediaType *)((VideoFace *)(This))->owner)
#define FACE_FORWARD(name, params, args)                                                                               \
    static HRESULT STDMETHODCALLTYPE face_##name params                                                               \
    {                                                                                                                  \
        return OWNER(This)->lpVtbl->name args;                                                                        \
    }

static HRESULT STDMETHODCALLTYPE face_QueryInterface(IMFAttributes *This, REFIID riid, void **out)
{
    return OWNER(This)->lpVtbl->QueryInterface(OWNER(This), riid, out);
}

static ULONG STDMETHODCALLTYPE face_AddRef(IMFAttributes *This)
{
    return OWNER(This)->lpVtbl->AddRef(OWNER(This));
}

static ULONG STDMETHODCALLTYPE face_Release(IMFAttributes *This)
{
    return OWNER(This)->lpVtbl->Release(OWNER(This));
}

FACE_FORWARD(GetItem, (IMFAttributes * This, REFGUID k, PROPVARIANT *v), (OWNER(This), k, v))
FACE_FORWARD(GetItemType, (IMFAttributes * This, REFGUID k, MF_ATTRIBUTE_TYPE *v), (OWNER(This), k, v))
FACE_FORWARD(CompareItem, (IMFAttributes * This, REFGUID k, const PROPVARIANT *v, BOOL *r), (OWNER(This), k, v, r))
FACE_FORWARD(Compare, (IMFAttributes * This, IMFAttributes *a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL *r),
             (OWNER(This), a, m, r))
FACE_FORWARD(GetUINT32, (IMFAttributes * This, REFGUID k, UINT32 *v), (OWNER(This), k, v))
FACE_FORWARD(GetUINT64, (IMFAttributes * This, REFGUID k, UINT64 *v), (OWNER(This), k, v))
FACE_FORWARD(GetDouble, (IMFAttributes * This, REFGUID k, double *v), (OWNER(This), k, v))
FACE_FORWARD(GetGUID, (IMFAttributes * This, REFGUID k, GUID *v), (OWNER(This), k, v))
FACE_FORWARD(GetStringLength, (IMFAttributes * This, REFGUID k, UINT32 *v), (OWNER(This), k, v))
FACE_FORWARD(GetString, (IMFAttributes * This, REFGUID k, LPWSTR v, UINT32 n, UINT32 *l), (OWNER(This), k, v, n, l))
FACE_FORWARD(GetAllocatedString, (IMFAttributes * This, REFGUID k, LPWSTR *v, UINT32 *l), (OWNER(This), k, v, l))
FACE_FORWARD(GetBlobSize, (IMFAttributes * This, REFGUID k, UINT32 *v), (OWNER(This), k, v))
FACE_FORWARD(GetBlob, (IMFAttributes * This, REFGUID k, UINT8 *b, UINT32 n, UINT32 *w), (OWNER(This), k, b, n, w))
FACE_FORWARD(GetAllocatedBlob, (IMFAttributes * This, REFGUID k, UINT8 **b, UINT32 *n), (OWNER(This), k, b, n))
FACE_FORWARD(GetUnknown, (IMFAttributes * This, REFGUID k, REFIID i, void **o), (OWNER(This), k, i, o))
FACE_FORWARD(SetItem, (IMFAttributes * This, REFGUID k, const PROPVARIANT *v), (OWNER(This), k, v))
FACE_FORWARD(DeleteItem, (IMFAttributes * This, REFGUID k), (OWNER(This), k))
FACE_FORWARD(DeleteAllItems, (IMFAttributes * This), (OWNER(This)))
FACE_FORWARD(SetUINT32, (IMFAttributes * This, REFGUID k, UINT32 v), (OWNER(This), k, v))
FACE_FORWARD(SetUINT64, (IMFAttributes * This, REFGUID k, UINT64 v), (OWNER(This), k, v))
FACE_FORWARD(SetDouble, (IMFAttributes * This, REFGUID k, double v), (OWNER(This), k, v))
FACE_FORWARD(SetGUID, (IMFAttributes * This, REFGUID k, REFGUID v), (OWNER(This), k, v))
FACE_FORWARD(SetString, (IMFAttributes * This, REFGUID k, LPCWSTR v), (OWNER(This), k, v))
FACE_FORWARD(SetBlob, (IMFAttributes * This, REFGUID k, const UINT8 *b, UINT32 n), (OWNER(This), k, b, n))
FACE_FORWARD(SetUnknown, (IMFAttributes * This, REFGUID k, IUnknown *u), (OWNER(This), k, u))
FACE_FORWARD(LockStore, (IMFAttributes * This), (OWNER(This)))
FACE_FORWARD(UnlockStore, (IMFAttributes * This), (OWNER(This)))
FACE_FORWARD(GetCount, (IMFAttributes * This, UINT32 *n), (OWNER(This), n))
FACE_FORWARD(GetItemByIndex, (IMFAttributes * This, UINT32 i, GUID *k, PROPVARIANT *v), (OWNER(This), i, k, v))
FACE_FORWARD(CopyAllItems, (IMFAttributes * This, IMFAttributes *d), (OWNER(This), d))

static HRESULT STDMETHODCALLTYPE face_GetMajorType(IMFMediaType *This, GUID *major)
{
    return type_get_major_type(OWNER(This), major);
}

static HRESULT STDMETHODCALLTYPE face_IsCompressedFormat(IMFMediaType *This, BOOL *compressed)
{
    return type_is_compressed_format(OWNER(This), compressed);
}

static HRESULT STDMETHODCALLTYPE face_IsEqual(IMFMediaType *This, IMFMediaType *other, DWORD *flags)
{
    return type_is_equal(OWNER(This), other, flags);
}

static HRESULT STDMETHODCALLTYPE face_GetRepresentation(IMFMediaType *This, GUID representation, void **data)
{
    return type_get_representation(OWNER(This), representation, data);
}

static HRESULT STDMETHODCALLTYPE face_FreeRepresentation(IMFMediaType *This, GUID representation, void *data)
{
    return type_free_representation(OWNER(This), representation, data);
}

static const MFVIDEOFORMAT *STDMETHODCALLTYPE face_GetVideoFormat(IMFMediaType *This)
{
    return type_get_video_format(OWNER(This));
}

static HRESULT STDMETHODCALLTYPE face_GetVideoRepresentation(IMFMediaType *This, GUID representation, void **data,
                                                             LONG stride)
{
    return type_get_video_representation(OWNER(This), representation, data, stride);
}

static const MediaTypeVtbl FACE_VTBL = {
    {
        face_QueryInterface, face_AddRef, face_Release, face_GetItem, face_GetItemType, face_CompareItem, face_Compare,
        face_GetUINT32, face_GetUINT64, face_GetDouble, face_GetGUID, face_GetStringLength, face_GetString,
        face_GetAllocatedString, face_GetBlobSize, face_GetBlob, face_GetAllocatedBlob, face_GetUnknown, face_SetItem,
        face_DeleteItem, face_DeleteAllItems, face_SetUINT32, face_SetUINT64, face_SetDouble, face_SetGUID,
        face_SetString, face_SetBlob, face_SetUnknown, face_LockStore, face_UnlockStore, face_GetCount,
        face_GetItemByIndex, face_CopyAllItems,
    },
    face_GetMajorType, face_IsCompressedFormat, face_IsEqual, face_GetRepresentation, face_FreeRepresentation,
    face_GetVideoFormat, face_GetVideoRepresentation,
};

/*
 * measured on windows 11 with vtqi.exe and vtqi2.exe of scratch/dlls/evr: a plain type whose major type is video
 * answers IMFVideoMediaType too, with the same pointer every time but not its own, sharing its items and its count.
 */
static HRESULT STDMETHODCALLTYPE type_query_interface(IMFAttributes *iface, REFIID riid, void **out)
{
    MediaType *type = (MediaType *)iface;
    GUID major;
    if (out && type->object.cls == &MEDIA_TYPE_CLASS && IsEqualGUID(riid, &IID_IMFVideoMediaType) &&
        SUCCEEDED(iface->lpVtbl->GetGUID(iface, &MF_MT_MAJOR_TYPE, &major)) &&
        IsEqualGUID(&major, &MFMediaType_Video)) {
        iface->lpVtbl->LockStore(iface);
        if (!type->face && (type->face = MfAlloc(sizeof(*type->face)))) {
            type->face->vtbl = &FACE_VTBL;
            type->face->owner = type;
        }
        iface->lpVtbl->UnlockStore(iface);
        if (!type->face) {
            *out = nullptr;
            return E_OUTOFMEMORY;
        }
        iface->lpVtbl->AddRef(iface);
        *out = type->face;
        return S_OK;
    }
    return MfObjectQueryInterface(iface, riid, out);
}

static const MediaTypeVtbl MEDIA_TYPE_VTBL = {
    MF_ATTRIBUTES_VTBL_WITH_QUERY(type_query_interface),      type_get_major_type,   type_is_compressed_format,     type_is_equal,
    type_get_representation, type_free_representation, type_get_video_format, type_get_video_representation,
};

static HRESULT create_type(const MfClass *cls, IMFMediaType **type)
{
    MediaType *object = MfAllocZero(sizeof(*object));
    if (!object) {
        *type = nullptr;
        return E_OUTOFMEMORY;
    }
    MfObjectInit(&object->object, &MEDIA_TYPE_VTBL, cls, 0);
    *type = (IMFMediaType *)object;
    return S_OK;
}

HRESULT WINAPI MFCreateMediaType(IMFMediaType **type)
{
    if (!type)
        return E_POINTER;
    return create_type(&MEDIA_TYPE_CLASS, type);
}

HRESULT MfCreateVideoMediaType(IMFMediaType **type)
{
    return create_type(&VIDEO_TYPE_CLASS, type);
}

#define WAVE_FORMAT_MPEG_HEAAC 0x1610

/* default speaker masks for 0 to 8 channels: mono, stereo, 2.1, quad, 4.1, 5.1, 6.1 and 7.1. */
static const DWORD DEFAULT_MASKS[9] = {0x0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3F, 0x13F, 0x63F};

static DWORD default_mask(UINT32 channels)
{
    return channels < RTL_NUMBER_OF(DEFAULT_MASKS) ? DEFAULT_MASKS[channels] : 0;
}

static UINT32 get_uint32(IMFMediaType *type, REFGUID key)
{
    UINT32 value = 0;
    if (FAILED(type->lpVtbl->GetUINT32(type, key, &value)))
        value = 0;
    return value;
}

/* 0xFFFF when the subtype is not in the base guid. */
static DWORD format_tag(REFGUID subtype)
{
    GUID base = MFAudioFormat_Base;
    base.Data1 = subtype->Data1;
    return IsEqualGUID(&base, subtype) ? subtype->Data1 : 0xFFFFFFFF;
}

/*
 * an extensible format unless the subtype is a wave tag a plain format can
 * describe: only the default channel mask, all bits valid, and no pcm or
 * float over 16 bits or 2 channels. MF_MT_AUDIO_PREFER_WAVEFORMATEX asks
 * for a plain one, but is ignored when the type has a channel mask.
 */
HRESULT WINAPI MFCreateWaveFormatExFromMFMediaType(IMFMediaType *type, WAVEFORMATEX **format, UINT32 *size,
                                                   UINT32 flags)
{
    GUID major, subtype;
    HRESULT hr = type->lpVtbl->GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
    if (FAILED(hr))
        return hr;
    hr = type->lpVtbl->GetGUID(type, &MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
        return hr;
    BOOL prefer = get_uint32(type, &MF_MT_AUDIO_PREFER_WAVEFORMATEX) != 0;
    if (prefer && SUCCEEDED(type->lpVtbl->GetItem(type, &MF_MT_AUDIO_CHANNEL_MASK, nullptr)))
        prefer = FALSE;
    if (!IsEqualGUID(&major, &MFMediaType_Audio))
        return E_INVALIDARG;
    UINT32 user = 0;
    type->lpVtbl->GetBlobSize(type, &MF_MT_USER_DATA, &user);
    if (user >= 0xFFD8)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    WAVEFORMATEXTENSIBLE *wfx = CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE) + user);
    if (!wfx)
        return E_OUTOFMEMORY;
    memset(wfx, 0, sizeof(WAVEFORMATEXTENSIBLE) + user);
    wfx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx->Format.cbSize = (WORD)(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) + user);
    wfx->Format.nChannels = (WORD)get_uint32(type, &MF_MT_AUDIO_NUM_CHANNELS);
    wfx->Format.nSamplesPerSec = get_uint32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND);
    wfx->Format.nAvgBytesPerSec = get_uint32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
    wfx->Format.nBlockAlign = (WORD)get_uint32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT);
    wfx->Format.wBitsPerSample = (WORD)get_uint32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE);
    UINT32 valid;
    if (wfx->Format.wBitsPerSample) {
        valid = wfx->Format.wBitsPerSample;
        type->lpVtbl->GetUINT32(type, &MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, &valid);
    } else {
        valid = get_uint32(type, &MF_MT_AUDIO_SAMPLES_PER_BLOCK);
    }
    wfx->Samples.wValidBitsPerSample = (WORD)valid;
    wfx->dwChannelMask = get_uint32(type, &MF_MT_AUDIO_CHANNEL_MASK);
    wfx->SubFormat = subtype;

    BYTE *user_data = (BYTE *)(wfx + 1);
    UINT32 total = sizeof(WAVEFORMATEXTENSIBLE) + user;
    DWORD tag = format_tag(&subtype);
    BOOL extensible = (flags & MFWaveFormatExConvertFlag_ForceExtensible) || tag >= WAVE_FORMAT_EXTENSIBLE;
    if (!extensible && !prefer) {
        WORD samples = wfx->Samples.wValidBitsPerSample;
        if (wfx->dwChannelMask && wfx->dwChannelMask != default_mask(wfx->Format.nChannels))
            extensible = TRUE;
        else if (samples && samples != wfx->Format.wBitsPerSample)
            extensible = TRUE;
        else if ((tag == WAVE_FORMAT_PCM || tag == WAVE_FORMAT_IEEE_FLOAT) &&
                 (wfx->Format.wBitsPerSample > 16 || wfx->Format.nChannels > 2))
            extensible = TRUE;
    }
    if (!extensible) {
        wfx->Format.wFormatTag = (WORD)tag;
        wfx->Format.cbSize = (WORD)user;
        user_data = (BYTE *)wfx + sizeof(WAVEFORMATEX);
        total = sizeof(WAVEFORMATEX) + user;
    }
    if (user && FAILED(hr = type->lpVtbl->GetBlob(type, &MF_MT_USER_DATA, user_data, user, nullptr))) {
        CoTaskMemFree(wfx);
        return hr;
    }
    if (extensible) {
        if (!wfx->Samples.wValidBitsPerSample && wfx->Format.wBitsPerSample)
            wfx->Samples.wValidBitsPerSample = wfx->Format.wBitsPerSample;
        if (!wfx->dwChannelMask)
            wfx->dwChannelMask = default_mask(wfx->Format.nChannels);
        wfx->Format.wBitsPerSample = (wfx->Format.wBitsPerSample + 7) & ~7;
    }
    *format = &wfx->Format;
    if (size)
        *size = total;
    return S_OK;
}

HRESULT WINAPI MFInitMediaTypeFromWaveFormatEx(IMFMediaType *type, const WAVEFORMATEX *format, UINT32 size)
{
    if (!type || !format)
        return E_POINTER;
    if (size < sizeof(WAVEFORMATEX) || size < sizeof(WAVEFORMATEX) + (UINT32)format->cbSize)
        return E_INVALIDARG;
    HRESULT hr = type->lpVtbl->DeleteAllItems(type);
    if (SUCCEEDED(hr))
        hr = type->lpVtbl->SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (FAILED(hr))
        return hr;
    const WAVEFORMATEXTENSIBLE *wfx = nullptr;
    GUID subtype;
    const BYTE *user;
    UINT32 user_size;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return E_INVALIDARG;
        wfx = (const WAVEFORMATEXTENSIBLE *)format;
        memcpy(&subtype, &wfx->SubFormat, sizeof(subtype));
        user = (const BYTE *)(wfx + 1);
        user_size = format->cbSize - (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    } else {
        subtype = MFAudioFormat_Base;
        subtype.Data1 = format->wFormatTag;
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);
        if (FAILED(hr))
            return hr;
        user = (const BYTE *)format + sizeof(WAVEFORMATEX);
        user_size = format->cbSize;
        if (format->wFormatTag == WAVE_FORMAT_MPEG_HEAAC) {
            /* payload type, profile and level: the first words of HEAACWAVEINFO. */
            if (sizeof(WAVEFORMATEX) + (UINT32)format->cbSize < 30)
                return E_INVALIDARG;
            WORD info[2];
            memcpy(info, user, sizeof(info));
            hr = type->lpVtbl->SetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, info[0]);
            if (SUCCEEDED(hr))
                hr = type->lpVtbl->SetUINT32(type, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, info[1]);
            if (FAILED(hr))
                return hr;
        }
    }
    hr = type->lpVtbl->SetGUID(type, &MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr) && format->nChannels)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, format->nChannels);
    if (SUCCEEDED(hr) && format->nSamplesPerSec)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, format->nSamplesPerSec);
    if (SUCCEEDED(hr) && format->nBlockAlign)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, format->nBlockAlign);
    if (SUCCEEDED(hr) && format->nAvgBytesPerSec)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, format->nAvgBytesPerSec);
    if (SUCCEEDED(hr) && format->wBitsPerSample)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, format->wBitsPerSample);
    if (SUCCEEDED(hr) && (IsEqualGUID(&subtype, &MFAudioFormat_PCM) || IsEqualGUID(&subtype, &MFAudioFormat_Float)))
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    if (SUCCEEDED(hr) && user_size)
        hr = type->lpVtbl->SetBlob(type, &MF_MT_USER_DATA, user, user_size);
    if (FAILED(hr) || !wfx)
        return hr;
    WORD samples = wfx->Samples.wValidBitsPerSample;
    if (samples) {
        if (!format->wBitsPerSample)
            hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_BLOCK, samples);
        else if (samples != format->wBitsPerSample)
            hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, samples);
        if (FAILED(hr))
            return hr;
    }
    if (wfx->dwChannelMask)
        hr = type->lpVtbl->SetUINT32(type, &MF_MT_AUDIO_CHANNEL_MASK, wfx->dwChannelMask);
    return hr;
}
