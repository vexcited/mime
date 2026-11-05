#include "mscms_private.h"

#define LCS_GM_BUSINESS 1
#define LCS_GM_GRAPHICS 2
#define LCS_GM_IMAGES 4
#define LCS_GM_ABS_COLORIMETRIC 8

#define ENABLE_GAMUT_CHECKING 0x00010000
#define USE_RELATIVE_COLORIMETRIC 0x00020000

#define INDEX_DONT_CARE 0
#define CMM_FROM_PROFILE INDEX_DONT_CARE

static HANDLE make_transform(const IccSource *sources, DWORD count, const DWORD *intents, DWORD intent_count, DWORD flags)
{
    int error;
    IccXform *x = icc_xform_create(&mscms_alloc, sources, count, (const uint32_t *)intents, intent_count, flags, &error);
    if (!x) {
        SetLastError(error == ICC_ERR_MEMORY ? ERROR_NOT_ENOUGH_MEMORY
                     : error == ICC_ERR_COLORSPACE ? ERROR_COLORSPACE_MISMATCH
                     : error == ICC_ERR_ARGUMENT ? ERROR_INVALID_PARAMETER
                                                 : ERROR_INVALID_PROFILE);
        return nullptr;
    }
    Transform *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t) {
        icc_xform_destroy(x);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    t->magic = TRANSFORM_MAGIC;
    t->xform = x;
    InitializeCriticalSection(&t->lock);
    return (HANDLE)((ULONG_PTR)t ^ HANDLE_KEY);
}

/* the profiles of handles, locked while the transform copies them. */
static BOOL sources_of(HANDLE *profiles, DWORD count, IccSource *sources)
{
    for (DWORD i = 0; i < count; i++) {
        Profile *p = ProfileOf(profiles[i]);
        if (!p) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        sources[i].data = p->edit.data;
        sources[i].size = p->edit.size;
    }
    return TRUE;
}

static void lock_profiles(HANDLE *profiles, DWORD count, BOOL on)
{
    for (DWORD i = 0; i < count; i++) {
        Profile *p = ProfileOf(profiles[i]);
        if (on)
            EnterCriticalSection(&p->lock);
        else
            LeaveCriticalSection(&p->lock);
    }
}

HANDLE WINAPI CreateMultiProfileTransform(HANDLE *profiles, DWORD count, DWORD *intents, DWORD intent_count, DWORD flags,
                                          DWORD cmm)
{
    if (!profiles || !count || count > 64 || !intents || !intent_count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    IccSource sources[64];
    if (!sources_of(profiles, count, sources))
        return nullptr;
    for (DWORD i = 0; i < intent_count; i++)
        if (intents[i] > 3) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
    lock_profiles(profiles, count, TRUE);
    HANDLE h = make_transform(sources, count, intents, intent_count, 0);
    lock_profiles(profiles, count, FALSE);
    return h;
}

static DWORD intent_of_lcs(LONG gm)
{
    switch (gm) {
    case LCS_GM_BUSINESS:
        return ICC_INTENT_SATURATION;
    case LCS_GM_GRAPHICS:
        return ICC_INTENT_RELATIVE;
    case LCS_GM_ABS_COLORIMETRIC:
        return ICC_INTENT_ABSOLUTE;
    }
    return ICC_INTENT_PERCEPTUAL;
}

/* the source of a logical color space: its file, else a profile made from it, read into a buffer of the heap. */
static BYTE *lcs_profile(LOGCOLORSPACEW *lcs, DWORD *size)
{
    if (lcs->lcsFilename[0]) {
        PROFILE desc = {PROFILE_FILENAME, lcs->lcsFilename, (DWORD)(wcslen(lcs->lcsFilename) + 1) * sizeof(WCHAR)};
        HANDLE h = OpenColorProfileW(&desc, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING);
        Profile *p = ProfileOf(h);
        if (!p)
            return nullptr;
        BYTE *copy = HeapAlloc(GetProcessHeap(), 0, p->edit.size);
        if (copy) {
            memcpy(copy, p->edit.data, p->edit.size);
            *size = p->edit.size;
        }
        CloseColorProfile(h);
        return copy;
    }
    BYTE *global = nullptr;
    LOGCOLORSPACEW plain = *lcs;
    if (!CreateProfileFromLogColorSpaceW(&plain, &global))
        return nullptr;
    *size = icc_be32(global);
    BYTE *copy = HeapAlloc(GetProcessHeap(), 0, *size);
    if (copy)
        memcpy(copy, global, *size);
    GlobalFree(global);
    return copy;
}

/*
 * from the logical color space to the destination, through the target as a proof when there is one: the target is
 * left with absolute colorimetric, or relative when asked.
 */
HANDLE WINAPI CreateColorTransformW(LOGCOLORSPACEW *lcs, HANDLE dest, HANDLE target, DWORD flags)
{
    if (!lcs || (dest && !ProfileOf(dest)) || (target && !ProfileOf(target))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    DWORD size = 0;
    BYTE *source = lcs_profile(lcs, &size);
    if (!source)
        return nullptr;
    HANDLE list[2];
    DWORD n = 0;
    if (target)
        list[n++] = target;
    if (dest)
        list[n++] = dest;
    IccSource sources[3] = {{source, size}};
    sources_of(list, n, sources + 1);
    DWORD intent = intent_of_lcs(lcs->lcsIntent);
    DWORD proof = flags & USE_RELATIVE_COLORIMETRIC ? ICC_INTENT_RELATIVE : ICC_INTENT_ABSOLUTE;
    DWORD intents[3] = {intent, intent, target && dest ? proof : intent};
    lock_profiles(list, n, TRUE);
    HANDLE h = make_transform(sources, n + 1, intents, n + 1, target && dest ? ICC_XFORM_PROOF : 0);
    lock_profiles(list, n, FALSE);
    HeapFree(GetProcessHeap(), 0, source);
    return h;
}

HANDLE WINAPI CreateColorTransformA(LOGCOLORSPACEA *lcs, HANDLE dest, HANDLE target, DWORD flags)
{
    if (!lcs) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    LOGCOLORSPACEW w;
    memcpy(&w, lcs, offsetof(LOGCOLORSPACEW, lcsFilename));
    MultiByteToWideChar(CP_ACP, 0, lcs->lcsFilename, -1, w.lcsFilename, MAX_PATH);
    w.lcsFilename[MAX_PATH - 1] = 0;
    return CreateColorTransformW(&w, dest, target, flags);
}

BOOL WINAPI DeleteColorTransform(HANDLE handle)
{
    Transform *t = TransformOf(handle);
    if (!t) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    t->magic = 0;
    icc_xform_destroy(t->xform);
    DeleteCriticalSection(&t->lock);
    HeapFree(GetProcessHeap(), 0, t);
    return TRUE;
}

/* ------------------------------------------------------------------ bitmap formats */

enum {
    BM_x555RGB = 0x0000, BM_565RGB = 0x0001, BM_RGBTRIPLETS = 0x0002, BM_BGRTRIPLETS = 0x0004, BM_xRGBQUADS = 0x0008,
    BM_10b_RGB = 0x0009, BM_16b_RGB = 0x000A, BM_xBGRQUADS = 0x0010, BM_CMYKQUADS = 0x0020,
    BM_x555XYZ = 0x0101, BM_x555Yxy, BM_x555Lab, BM_x555G3CH,
    BM_XYZTRIPLETS = 0x0201, BM_YxyTRIPLETS, BM_LabTRIPLETS, BM_G3CHTRIPLETS, BM_5CHANNEL, BM_6CHANNEL, BM_7CHANNEL,
    BM_8CHANNEL, BM_GRAY,
    BM_xXYZQUADS = 0x0301, BM_xYxyQUADS, BM_xLabQUADS, BM_xG3CHQUADS, BM_KYMCQUADS,
    BM_10b_XYZ = 0x0401, BM_10b_Yxy, BM_10b_Lab, BM_10b_G3CH, BM_NAMED_INDEX,
    BM_16b_XYZ = 0x0501, BM_16b_Yxy, BM_16b_Lab, BM_16b_G3CH, BM_16b_GRAY,
    BM_32b_scRGB = 0x0601, BM_32b_scARGB, BM_S2DOT13FIXED_scRGB, BM_S2DOT13FIXED_scARGB,
    BM_R10G10B10A2 = 0x0701, BM_R10G10B10A2_XR, BM_R16G16B16A16_FLOAT,
};

enum { K_BYTES, K_WORDS, K_555, K_565, K_10B, K_FLOAT, K_S2DOT13, K_R10A2, K_R10A2_XR, K_HALF };

typedef struct Format {
    int kind;
    uint32_t channels;
    uint32_t bits;          /* per pixel. */
    int8_t offset[8];       /* where each channel is, in units of the channel size. */
    int8_t alpha;           /* where alpha or the unused channel is, or -1. */
} Format;

static BOOL format_of(DWORD id, Format *f)
{
    static const struct {
        DWORD id;
        Format f;
    } table[] = {
        {BM_x555RGB, {K_555, 3, 16, {0, 1, 2}, -1}},       {BM_x555XYZ, {K_555, 3, 16, {0, 1, 2}, -1}},
        {BM_x555Yxy, {K_555, 3, 16, {0, 1, 2}, -1}},       {BM_x555Lab, {K_555, 3, 16, {0, 1, 2}, -1}},
        {BM_x555G3CH, {K_555, 3, 16, {0, 1, 2}, -1}},      {BM_565RGB, {K_565, 3, 16, {0, 1, 2}, -1}},
        {BM_RGBTRIPLETS, {K_BYTES, 3, 24, {2, 1, 0}, -1}}, {BM_BGRTRIPLETS, {K_BYTES, 3, 24, {0, 1, 2}, -1}},
        {BM_XYZTRIPLETS, {K_BYTES, 3, 24, {0, 1, 2}, -1}}, {BM_YxyTRIPLETS, {K_BYTES, 3, 24, {0, 1, 2}, -1}},
        {BM_LabTRIPLETS, {K_BYTES, 3, 24, {0, 1, 2}, -1}}, {BM_G3CHTRIPLETS, {K_BYTES, 3, 24, {0, 1, 2}, -1}},
        {BM_5CHANNEL, {K_BYTES, 5, 40, {0, 1, 2, 3, 4}, -1}},
        {BM_6CHANNEL, {K_BYTES, 6, 48, {0, 1, 2, 3, 4, 5}, -1}},
        {BM_7CHANNEL, {K_BYTES, 7, 56, {0, 1, 2, 3, 4, 5, 6}, -1}},
        {BM_8CHANNEL, {K_BYTES, 8, 64, {0, 1, 2, 3, 4, 5, 6, 7}, -1}},
        {BM_GRAY, {K_BYTES, 1, 8, {0}, -1}},
        {BM_xRGBQUADS, {K_BYTES, 3, 32, {2, 1, 0}, 3}},    {BM_xBGRQUADS, {K_BYTES, 3, 32, {0, 1, 2}, 3}},
        {BM_xXYZQUADS, {K_BYTES, 3, 32, {0, 1, 2}, 3}},    {BM_xYxyQUADS, {K_BYTES, 3, 32, {0, 1, 2}, 3}},
        {BM_xLabQUADS, {K_BYTES, 3, 32, {0, 1, 2}, 3}},    {BM_xG3CHQUADS, {K_BYTES, 3, 32, {0, 1, 2}, 3}},
        {BM_CMYKQUADS, {K_BYTES, 4, 32, {0, 1, 2, 3}, -1}}, {BM_KYMCQUADS, {K_BYTES, 4, 32, {3, 2, 1, 0}, -1}},
        {BM_10b_RGB, {K_10B, 3, 32, {0, 1, 2}, -1}},       {BM_10b_XYZ, {K_10B, 3, 32, {0, 1, 2}, -1}},
        {BM_10b_Yxy, {K_10B, 3, 32, {0, 1, 2}, -1}},       {BM_10b_Lab, {K_10B, 3, 32, {0, 1, 2}, -1}},
        {BM_10b_G3CH, {K_10B, 3, 32, {0, 1, 2}, -1}},
        {BM_16b_RGB, {K_WORDS, 3, 48, {0, 1, 2}, -1}},     {BM_16b_XYZ, {K_WORDS, 3, 48, {0, 1, 2}, -1}},
        {BM_16b_Yxy, {K_WORDS, 3, 48, {0, 1, 2}, -1}},     {BM_16b_Lab, {K_WORDS, 3, 48, {0, 1, 2}, -1}},
        {BM_16b_G3CH, {K_WORDS, 3, 48, {0, 1, 2}, -1}},    {BM_16b_GRAY, {K_WORDS, 1, 16, {0}, -1}},
        {BM_32b_scRGB, {K_FLOAT, 3, 96, {0, 1, 2}, -1}},   {BM_32b_scARGB, {K_FLOAT, 3, 128, {1, 2, 3}, 0}},
        {BM_S2DOT13FIXED_scRGB, {K_S2DOT13, 3, 48, {0, 1, 2}, -1}},
        {BM_S2DOT13FIXED_scARGB, {K_S2DOT13, 3, 64, {1, 2, 3}, 0}},
        {BM_R10G10B10A2, {K_R10A2, 3, 32, {0, 1, 2}, 3}}, {BM_R10G10B10A2_XR, {K_R10A2_XR, 3, 32, {0, 1, 2}, 3}},
        {BM_R16G16B16A16_FLOAT, {K_HALF, 3, 64, {0, 1, 2}, 3}},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].id == id) {
            *f = table[i].f;
            return TRUE;
        }
    return FALSE;
}

static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31, e = h >> 10 & 31, m = h & 1023, bits;
    if (!e) {
        if (!m) {
            bits = sign;
        } else {
            e = 113;
            while (!(m & 1024)) {
                m <<= 1;
                e--;
            }
            bits = sign | e << 23 | (m & 1023) << 13;
        }
    } else if (e == 31) {
        bits = sign | 0x7f800000 | m << 13;
    } else {
        bits = sign | (e + 112) << 23 | m << 13;
    }
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static uint16_t float_to_half(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    uint32_t sign = bits >> 16 & 0x8000;
    int e = (int)(bits >> 23 & 255) - 112;
    uint32_t m = bits & 0x7fffff;
    if (e >= 31)
        return (uint16_t)(sign | 0x7c00);
    if (e <= 0)
        return (uint16_t)sign;
    return (uint16_t)(sign | e << 10 | (m + 0x1000) >> 13);
}

static uint32_t quantize(float v, uint32_t max)
{
    v = v * max + 0.5f;
    return v <= 0 ? 0 : v >= max ? max : (uint32_t)v;
}

/* one pixel to floats, with its alpha in 0 to 1. */
static void read_pixel(const Format *f, const BYTE *p, float *c, float *alpha)
{
    *alpha = 1;
    switch (f->kind) {
    case K_BYTES:
        for (uint32_t i = 0; i < f->channels; i++)
            c[i] = p[f->offset[i]] / 255.0f;
        if (f->alpha >= 0)
            *alpha = p[f->alpha] / 255.0f;
        break;
    case K_WORDS:
        for (uint32_t i = 0; i < f->channels; i++)
            c[i] = ((const WORD *)p)[f->offset[i]] / 65535.0f;
        break;
    case K_555: {
        WORD w = *(const WORD *)p;
        c[0] = (w >> 10 & 31) / 31.0f;
        c[1] = (w >> 5 & 31) / 31.0f;
        c[2] = (w & 31) / 31.0f;
        break;
    }
    case K_565: {
        WORD w = *(const WORD *)p;
        c[0] = (w >> 11 & 31) / 31.0f;
        c[1] = (w >> 5 & 63) / 63.0f;
        c[2] = (w & 31) / 31.0f;
        break;
    }
    case K_10B: {
        DWORD d = *(const DWORD *)p;
        c[0] = (d >> 20 & 1023) / 1023.0f;
        c[1] = (d >> 10 & 1023) / 1023.0f;
        c[2] = (d & 1023) / 1023.0f;
        break;
    }
    case K_FLOAT:
        for (uint32_t i = 0; i < f->channels; i++)
            c[i] = ((const float *)p)[f->offset[i]];
        if (f->alpha >= 0)
            *alpha = ((const float *)p)[f->alpha];
        break;
    case K_S2DOT13:
        for (uint32_t i = 0; i < f->channels; i++)
            c[i] = ((const SHORT *)p)[f->offset[i]] / 8192.0f;
        if (f->alpha >= 0)
            *alpha = ((const SHORT *)p)[f->alpha] / 8192.0f;
        break;
    case K_R10A2:
    case K_R10A2_XR: {
        DWORD d = *(const DWORD *)p;
        for (int i = 0; i < 3; i++) {
            float v = (float)(d >> (10 * i) & 1023);
            c[i] = f->kind == K_R10A2 ? v / 1023.0f : (v - 384.0f) / 510.0f;
        }
        *alpha = (d >> 30) / 3.0f;
        break;
    }
    case K_HALF:
        for (int i = 0; i < 3; i++)
            c[i] = half_to_float(((const WORD *)p)[i]);
        *alpha = half_to_float(((const WORD *)p)[3]);
        break;
    }
}

static void write_pixel(const Format *f, BYTE *p, const float *c, float alpha)
{
    switch (f->kind) {
    case K_BYTES:
        for (uint32_t i = 0; i < f->channels; i++)
            p[f->offset[i]] = (BYTE)quantize(c[i], 255);
        if (f->alpha >= 0)
            p[f->alpha] = (BYTE)quantize(alpha, 255);
        break;
    case K_WORDS:
        for (uint32_t i = 0; i < f->channels; i++)
            ((WORD *)p)[f->offset[i]] = (WORD)quantize(c[i], 65535);
        break;
    case K_555:
        *(WORD *)p = (WORD)(quantize(c[0], 31) << 10 | quantize(c[1], 31) << 5 | quantize(c[2], 31));
        break;
    case K_565:
        *(WORD *)p = (WORD)(quantize(c[0], 31) << 11 | quantize(c[1], 63) << 5 | quantize(c[2], 31));
        break;
    case K_10B:
        *(DWORD *)p = quantize(c[0], 1023) << 20 | quantize(c[1], 1023) << 10 | quantize(c[2], 1023);
        break;
    case K_FLOAT:
        for (uint32_t i = 0; i < f->channels; i++)
            ((float *)p)[f->offset[i]] = c[i];
        if (f->alpha >= 0)
            ((float *)p)[f->alpha] = alpha;
        break;
    case K_S2DOT13:
        for (uint32_t i = 0; i < f->channels; i++) {
            float v = c[i] * 8192.0f;
            v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
            ((SHORT *)p)[f->offset[i]] = (SHORT)(v < 0 ? v - 0.5f : v + 0.5f);
        }
        if (f->alpha >= 0)
            ((SHORT *)p)[f->alpha] = (SHORT)(alpha * 8192.0f + 0.5f);
        break;
    case K_R10A2:
    case K_R10A2_XR: {
        DWORD d = quantize(alpha, 3) << 30;
        for (int i = 0; i < 3; i++) {
            float v = f->kind == K_R10A2 ? c[i] : (c[i] * 510.0f + 384.0f) / 1023.0f;
            d |= quantize(v, 1023) << (10 * i);
        }
        *(DWORD *)p = d;
        break;
    }
    case K_HALF:
        for (int i = 0; i < 3; i++)
            ((WORD *)p)[i] = float_to_half(c[i]);
        ((WORD *)p)[3] = float_to_half(alpha);
        break;
    }
}

static BOOL float_format(const Format *f) { return f->kind >= K_FLOAT; }

typedef BOOL(CALLBACK *PBMCALLBACKFN)(ULONG line, ULONG lines, ULONG_PTR data);

/*
 * converts or checks a bitmap row by row. formats of 8 bit channels on both sides go through the 8 bit tables of the
 * transform, others through floats. result receives the gamut bytes of a check instead of pixels.
 */
static BOOL bitmap(HANDLE handle, const BYTE *in, DWORD in_format, DWORD width, DWORD height, DWORD in_stride, BYTE *out,
                   DWORD out_format, DWORD out_stride, BYTE *result, PBMCALLBACKFN callback, ULONG_PTR data)
{
    Transform *t = TransformOf(handle);
    Format fi, fo;
    if (!t || !in || !width || !height || (!out && !result) || !format_of(in_format, &fi) ||
        (!result && !format_of(out_format, &fo))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    IccXform *x = t->xform;
    uint32_t ni = icc_xform_inputs(x), no = icc_xform_outputs(x);
    if (fi.channels != ni || (!result && fo.channels != no)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!in_stride)
        in_stride = (width * fi.bits + 31) / 32 * 4;
    if (!result && !out_stride)
        out_stride = (width * fo.bits + 31) / 32 * 4;
    BOOL bytes = !result && fi.kind == K_BYTES && fo.kind == K_BYTES;
    if (bytes) {
        EnterCriticalSection(&t->lock);
        BOOL ready = icc_xform_prepare8(x);
        LeaveCriticalSection(&t->lock);
        if (!ready) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    float *fin = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * (ni + no + 1) * sizeof(float));
    BYTE *row8 = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * (ni + no));
    if (!fin || !row8) {
        HeapFree(GetProcessHeap(), 0, fin);
        HeapFree(GetProcessHeap(), 0, row8);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    float *fout = fin + (SIZE_T)width * ni, *alpha = fout + (SIZE_T)width * no;
    BOOL ok = TRUE;
    for (DWORD y = 0; y < height; y++) {
        const BYTE *src = in + (SIZE_T)y * in_stride;
        if (result) {
            for (DWORD i = 0; i < width; i++)
                read_pixel(&fi, src + (SIZE_T)i * fi.bits / 8, fin + i * ni, &alpha[i]);
            icc_xform_gamut(x, fin, result + (SIZE_T)y * width, width);
        } else if (bytes) {
            BYTE *a = row8, *b = row8 + (SIZE_T)width * ni;
            DWORD si = fi.bits / 8, so = fo.bits / 8;
            for (DWORD i = 0; i < width; i++)
                for (uint32_t c = 0; c < ni; c++)
                    a[i * ni + c] = src[i * si + fi.offset[c]];
            icc_xform_eval8(x, a, ni, b, no, width);
            BYTE *dst = out + (SIZE_T)y * out_stride;
            for (DWORD i = 0; i < width; i++) {
                for (uint32_t c = 0; c < no; c++)
                    dst[i * so + fo.offset[c]] = b[i * no + c];
                if (fo.alpha >= 0)
                    dst[i * so + fo.alpha] = fi.alpha >= 0 ? src[i * si + fi.alpha] : 0;
            }
        } else {
            for (DWORD i = 0; i < width; i++)
                read_pixel(&fi, src + (SIZE_T)i * fi.bits / 8, fin + i * ni, &alpha[i]);
            icc_xform_eval(x, fin, fout, width, float_format(&fo));
            BYTE *dst = out + (SIZE_T)y * out_stride;
            for (DWORD i = 0; i < width; i++)
                write_pixel(&fo, dst + (SIZE_T)i * fo.bits / 8, fout + i * no, fi.alpha >= 0 || float_format(&fi) ? alpha[i] : 0);
        }
        if (callback && !callback(y, height, data)) {
            SetLastError(ERROR_CANCELLED);
            ok = FALSE;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, fin);
    HeapFree(GetProcessHeap(), 0, row8);
    return ok;
}

BOOL WINAPI TranslateBitmapBits(HANDLE transform, PVOID input, DWORD input_format, DWORD width, DWORD height,
                                DWORD input_stride, PVOID output, DWORD output_format, DWORD output_stride,
                                PBMCALLBACKFN callback, ULONG_PTR data)
{
    return bitmap(transform, input, input_format, width, height, input_stride, output, output_format, output_stride,
                  nullptr, callback, data);
}

BOOL WINAPI CheckBitmapBits(HANDLE transform, PVOID input, DWORD input_format, DWORD width, DWORD height, DWORD stride,
                            BYTE *result, PBMCALLBACKFN callback, ULONG_PTR data)
{
    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return bitmap(transform, input, input_format, width, height, stride, nullptr, 0, 0, result, callback, data);
}

/* ------------------------------------------------------------------ colors */

enum { COLOR_GRAY = 1, COLOR_RGB, COLOR_XYZ, COLOR_Yxy, COLOR_Lab, COLOR_3_CHANNEL, COLOR_CMYK, COLOR_5_CHANNEL,
       COLOR_6_CHANNEL, COLOR_7_CHANNEL, COLOR_8_CHANNEL, COLOR_NAMED };

/* COLOR is a union of words, of 8 bytes for the hifi channels, and of a dword and a pointer. */
typedef union COLOR {
    WORD words[4];
    BYTE channel[8];
    struct {
        DWORD reserved1;
        void *reserved2;
    } reserved;
} COLOR;

static uint32_t color_channels(DWORD type)
{
    switch (type) {
    case COLOR_GRAY:
        return 1;
    case COLOR_RGB:
    case COLOR_XYZ:
    case COLOR_Yxy:
    case COLOR_Lab:
    case COLOR_3_CHANNEL:
        return 3;
    case COLOR_CMYK:
        return 4;
    case COLOR_5_CHANNEL:
    case COLOR_6_CHANNEL:
    case COLOR_7_CHANNEL:
    case COLOR_8_CHANNEL:
        return type - COLOR_5_CHANNEL + 5;
    }
    return 0;
}

static BOOL colors(HANDLE handle, COLOR *in, DWORD count, DWORD in_type, COLOR *out, DWORD out_type, BYTE *result)
{
    /* the types are checked before the handle is read, a handle that is not one is only safe with a bad type. */
    if (in_type - 1 > COLOR_NAMED - 1 || (!result && out_type - 1 > COLOR_NAMED - 1)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Transform *t = TransformOf(handle);
    if (!t || !in || (!out && !result)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    IccXform *x = t->xform;
    uint32_t ni = color_channels(in_type), no = result ? icc_xform_outputs(x) : color_channels(out_type);
    if (!ni || ni != icc_xform_inputs(x) || no != icc_xform_outputs(x)) {
        SetLastError(in_type == COLOR_NAMED || out_type == COLOR_NAMED ? ERROR_INVALID_COLORSPACE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (DWORD i = 0; i < count; i++) {
        float a[8], b[16];
        for (uint32_t c = 0; c < ni; c++)
            a[c] = in_type >= COLOR_5_CHANNEL ? in[i].channel[c] / 255.0f : in[i].words[c] / 65535.0f;
        if (result) {
            icc_xform_gamut(x, a, &result[i], 1);
            continue;
        }
        icc_xform_eval(x, a, b, 1, false);
        COLOR o;
        memset(&o, 0, sizeof(o));
        for (uint32_t c = 0; c < no; c++) {
            if (out_type >= COLOR_5_CHANNEL)
                o.channel[c] = (BYTE)quantize(b[c], 255);
            else
                o.words[c] = (WORD)quantize(b[c], 65535);
        }
        out[i] = o;
    }
    return TRUE;
}

BOOL WINAPI TranslateColors(HANDLE transform, COLOR *input, DWORD count, DWORD input_type, COLOR *output, DWORD output_type)
{
    return colors(transform, input, count, input_type, output, output_type, nullptr);
}

BOOL WINAPI CheckColors(HANDLE transform, COLOR *input, DWORD count, DWORD input_type, BYTE *result)
{
    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return colors(transform, input, count, input_type, nullptr, 0, result);
}

/* COLORDATATYPE of the wcs calls. */
enum { COLOR_BYTE = 1, COLOR_WORD, COLOR_FLOAT, COLOR_S2DOT13FIXED, COLOR_10b_R10G10B10A2, COLOR_10b_R10G10B10A2_XR,
       COLOR_FLOAT16 };

static uint32_t data_size(DWORD type)
{
    switch (type) {
    case COLOR_BYTE:
        return 1;
    case COLOR_WORD:
    case COLOR_S2DOT13FIXED:
    case COLOR_FLOAT16:
        return 2;
    case COLOR_FLOAT:
        return 4;
    }
    return 0;
}

static float data_read(DWORD type, const BYTE *p)
{
    switch (type) {
    case COLOR_BYTE:
        return *p / 255.0f;
    case COLOR_WORD:
        return *(const WORD *)p / 65535.0f;
    case COLOR_S2DOT13FIXED:
        return *(const SHORT *)p / 8192.0f;
    case COLOR_FLOAT16:
        return half_to_float(*(const WORD *)p);
    }
    return *(const float *)p;
}

static void data_write(DWORD type, BYTE *p, float v)
{
    switch (type) {
    case COLOR_BYTE:
        *p = (BYTE)quantize(v, 255);
        break;
    case COLOR_WORD:
        *(WORD *)p = (WORD)quantize(v, 65535);
        break;
    case COLOR_S2DOT13FIXED: {
        float s = v * 8192.0f;
        s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
        *(SHORT *)p = (SHORT)(s < 0 ? s - 0.5f : s + 0.5f);
        break;
    }
    case COLOR_FLOAT16:
        *(WORD *)p = float_to_half(v);
        break;
    default:
        *(float *)p = v;
    }
}

static BOOL wcs_colors(HANDLE handle, DWORD count, DWORD in_channels, DWORD in_type, DWORD in_size, const BYTE *in,
                       DWORD out_channels, DWORD out_type, DWORD out_size, BYTE *out, BYTE *result)
{
    Transform *t = TransformOf(handle);
    uint32_t si = data_size(in_type), so = result ? 1 : data_size(out_type);
    if (!t || !in || (!out && !result) || !si || !so || in_channels != icc_xform_inputs(t->xform) ||
        (!result && out_channels != icc_xform_outputs(t->xform)) || (uint64_t)count * in_channels * si > in_size ||
        (!result && (uint64_t)count * out_channels * so > out_size)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint32_t no = icc_xform_outputs(t->xform);
    for (DWORD i = 0; i < count; i++) {
        float a[ICC_MAX_CHANNELS], b[ICC_MAX_CHANNELS];
        for (DWORD c = 0; c < in_channels; c++)
            a[c] = data_read(in_type, in + ((SIZE_T)i * in_channels + c) * si);
        if (result) {
            icc_xform_gamut(t->xform, a, &result[i], 1);
            continue;
        }
        icc_xform_eval(t->xform, a, b, 1, out_type == COLOR_FLOAT || out_type == COLOR_FLOAT16 || out_type == COLOR_S2DOT13FIXED);
        for (DWORD c = 0; c < no; c++)
            data_write(out_type, out + ((SIZE_T)i * out_channels + c) * so, b[c]);
    }
    return TRUE;
}

BOOL WINAPI WcsTranslateColors(HANDLE transform, DWORD count, DWORD in_channels, DWORD in_type, DWORD in_size, PVOID in,
                               DWORD out_channels, DWORD out_type, DWORD out_size, PVOID out)
{
    return wcs_colors(transform, count, in_channels, in_type, in_size, in, out_channels, out_type, out_size, out, nullptr);
}

BOOL WINAPI WcsCheckColors(HANDLE transform, DWORD count, DWORD in_channels, DWORD in_type, DWORD in_size, PVOID in,
                           BYTE *result)
{
    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return wcs_colors(transform, count, in_channels, in_type, in_size, in, 0, 0, 0, nullptr, result);
}

/* ------------------------------------------------------------------ device links */

/*
 * a device link profile sampling the transform of the profiles into a lut16: identity curves, a grid of 33 points for
 * 3 inputs, 17 for 4 and 9 beyond. the buffer comes from GlobalAlloc.
 */
BOOL WINAPI CreateDeviceLinkProfile(HANDLE *profiles, DWORD count, DWORD *intents, DWORD intent_count, DWORD flags,
                                    BYTE **out, DWORD cmm)
{
    if (!out || !profiles || count < 2) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE h = CreateMultiProfileTransform(profiles, count, intents, intent_count, flags, cmm);
    Transform *t = TransformOf(h);
    if (!t)
        return FALSE;
    IccXform *x = t->xform;
    uint32_t ni = icc_xform_inputs(x), no = icc_xform_outputs(x);
    uint32_t grid = ni == 1 ? 256 : ni == 2 ? 65 : ni == 3 ? 33 : ni == 4 ? 17 : 9;
    if (ni > 8)
        grid = 3;
    SIZE_T cells = 1;
    for (uint32_t i = 0; i < ni; i++)
        cells *= grid;
    SIZE_T table = 52 + (SIZE_T)ni * 2 * 2 + cells * no * 2 + (SIZE_T)no * 2 * 2;
    static const char desc[] = "Device link";
    SIZE_T desc_size = 12 + sizeof(desc) + 4 + 4 + 3 + 67;
    SIZE_T at_desc = 128 + 4 + 2 * 12, at_lut = (at_desc + desc_size + 3) & ~(SIZE_T)3;
    SIZE_T size = at_lut + table;
    BYTE *p = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size);
    if (!p) {
        DeleteColorTransform(h);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    icc_put32(p, (uint32_t)size);
    icc_put32(p + 8, 0x02100000);
    icc_put32(p + 12, ICC_SIG('l', 'i', 'n', 'k'));
    icc_put32(p + 16, icc_xform_input_space(x));
    icc_put32(p + 20, icc_xform_output_space(x));
    icc_put32(p + 36, ICC_SIG('a', 'c', 's', 'p'));
    icc_put32(p + 40, ICC_SIG('M', 'S', 'F', 'T'));
    icc_put32(p + 64, intents && intent_count ? intents[0] : 0);
    icc_put32(p + 68, 0x0000F6D6);
    icc_put32(p + 72, 0x00010000);
    icc_put32(p + 76, 0x0000D32D);
    icc_put32(p + 128, 2);
    icc_put32(p + 132, ICC_SIG('d', 'e', 's', 'c'));
    icc_put32(p + 136, (uint32_t)at_desc);
    icc_put32(p + 140, (uint32_t)desc_size);
    icc_put32(p + 144, ICC_SIG('A', '2', 'B', '0'));
    icc_put32(p + 148, (uint32_t)at_lut);
    icc_put32(p + 152, (uint32_t)table);
    icc_put32(p + at_desc, ICC_SIG('d', 'e', 's', 'c'));
    icc_put32(p + at_desc + 8, sizeof(desc));
    memcpy(p + at_desc + 12, desc, sizeof(desc));
    BYTE *l = p + at_lut;
    icc_put32(l, ICC_SIG('m', 'f', 't', '2'));
    l[8] = (BYTE)ni;
    l[9] = (BYTE)no;
    l[10] = (BYTE)grid;
    icc_put32(l + 12, 0x10000);
    icc_put32(l + 28, 0x10000);
    icc_put32(l + 44, 0x10000);
    icc_put16(l + 48, 2);
    icc_put16(l + 50, 2);
    BYTE *q = l + 52;
    for (uint32_t i = 0; i < ni; i++, q += 4)
        icc_put16(q + 2, 0xffff);
    float in[ICC_MAX_CHANNELS], o[ICC_MAX_CHANNELS];
    for (SIZE_T cell = 0; cell < cells; cell++) {
        SIZE_T rest = cell;
        for (int i = (int)ni - 1; i >= 0; i--) {
            in[i] = (float)(rest % grid) / (grid - 1);
            rest /= grid;
        }
        icc_xform_eval(x, in, o, 1, false);
        for (uint32_t c = 0; c < no; c++, q += 2)
            icc_put16(q, quantize(o[c], 65535));
    }
    for (uint32_t i = 0; i < no; i++, q += 4)
        icc_put16(q + 2, 0xffff);
    DeleteColorTransform(h);
    *out = p;
    return TRUE;
}

/* ------------------------------------------------------------------ color matching modules */

#define CMM_WIN_VERSION 0
#define CMM_IDENT 1
#define CMM_DRIVER_VERSION 2
#define CMM_DLL_VERSION 3
#define CMM_VERSION 4
#define CMM_DESCRIPTION 5
#define CMM_LOGOICON 6
#define CMM_WINDOWS_DEFAULT 0x57696E20

/* the engine here is the windows default one, 'Win '. */
DWORD WINAPI GetCMMInfo(HANDLE transform, DWORD info)
{
    switch (info) {
    case CMM_WIN_VERSION:
        return 0x00050000;
    case CMM_IDENT:
        return CMM_WINDOWS_DEFAULT;
    case CMM_DRIVER_VERSION:
        return 2;
    case CMM_DLL_VERSION:
        return 0x00050000;
    case CMM_VERSION:
        return 0x00040000;
    case CMM_DESCRIPTION:
    case CMM_LOGOICON:
        return 0;
    }
    return 0;
}

static const WCHAR cmm_key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM\\RegisteredCMMs";

static BOOL register_cmm(const WCHAR *machine, DWORD id, const WCHAR *dll)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!dll || !id) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[5] = {(WCHAR)(id >> 24 & 255), (WCHAR)(id >> 16 & 255), (WCHAR)(id >> 8 & 255), (WCHAR)(id & 255), 0};
    LSTATUS s = RegSetKeyValueW(HKEY_LOCAL_MACHINE, cmm_key, name, REG_SZ, dll, (DWORD)(wcslen(dll) + 1) * sizeof(WCHAR));
    if (s) {
        SetLastError(s);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI RegisterCMMW(const WCHAR *machine, DWORD id, const WCHAR *dll) { return register_cmm(machine, id, dll); }

BOOL WINAPI RegisterCMMA(const char *machine, DWORD id, const char *dll)
{
    WCHAR *m = WideFromAnsi(machine), *d = WideFromAnsi(dll);
    BOOL ok = register_cmm(m, id, d);
    HeapFree(GetProcessHeap(), 0, m);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

static BOOL unregister_cmm(BOOL remote, DWORD id)
{
    if (remote) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    WCHAR name[5] = {(WCHAR)(id >> 24 & 255), (WCHAR)(id >> 16 & 255), (WCHAR)(id >> 8 & 255), (WCHAR)(id & 255), 0};
    HKEY key;
    LSTATUS s = RegOpenKeyExW(HKEY_LOCAL_MACHINE, cmm_key, 0, KEY_SET_VALUE, &key);
    if (!s) {
        s = RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
    if (s) {
        SetLastError(s);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI UnregisterCMMW(const WCHAR *machine, DWORD id) { return unregister_cmm(machine != nullptr, id); }
BOOL WINAPI UnregisterCMMA(const char *machine, DWORD id) { return unregister_cmm(machine != nullptr, id); }

/* only the default engine is here, choosing it is all there is. */
BOOL WINAPI SelectCMM(DWORD id)
{
    if (id && id != CMM_WINDOWS_DEFAULT) {
        SetLastError(ERROR_INVALID_CMM);
        return FALSE;
    }
    return TRUE;
}
