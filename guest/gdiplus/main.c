/*
 * gdiplus.dll: startup, the allocator, shared color helpers and the constants the sdk keeps in its import library.
 * there is no background thread, so the notification hooks only hand out tokens.
 */
#include "gp_private.h"

/* the compiler expects this symbol in modules that use floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

void *gp_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *gp_alloc_zero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *gp_realloc(void *p, SIZE_T size)
{
    if (!p)
        return gp_alloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, p, size ? size : 1);
}

void gp_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

void *gp_dup(const void *p, SIZE_T bytes)
{
    if (!p || !bytes)
        return nullptr;
    void *copy = gp_alloc(bytes);
    if (copy)
        memcpy(copy, p, bytes);
    return copy;
}

static void *vec_alloc(void *ctx, size_t size) { return gp_alloc(size); }
static void vec_free(void *ctx, void *p) { gp_free(p); }

const MimeAllocator gp_allocator = {vec_alloc, vec_free, nullptr};

/* exact rounded x / 255: add 128, add that sum shifted by 8, shift by 8. */
static UINT32 div255(UINT32 x)
{
    x += 128;
    return (x + (x >> 8)) >> 8;
}

UINT32 gp_premultiply(UINT32 argb)
{
    UINT32 a = argb >> 24;
    if (a == 255)
        return argb;
    if (a == 0)
        return 0;
    UINT32 r = div255(a * ((argb >> 16) & 255)), g = div255(a * ((argb >> 8) & 255)), b = div255(a * (argb & 255));
    return a << 24 | r << 16 | g << 8 | b;
}

/* the 16.16 reciprocal 255 / a, rounded down, like gdi+. */
static UINT32 unpremultiply_factor(UINT32 a)
{
    return (UINT32)((255ull << 16) / a);
}

UINT32 gp_unpremultiply(UINT32 pargb)
{
    UINT32 a = pargb >> 24;
    if (a == 0 || a == 255)
        return pargb;
    UINT32 f = unpremultiply_factor(a);
    UINT32 r = (f * ((pargb >> 16) & 255)) >> 16, g = (f * ((pargb >> 8) & 255)) >> 16, b = (f * (pargb & 255)) >> 16;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return a << 24 | r << 16 | g << 8 | b;
}

UINT32 gp_scale_pargb(UINT32 pargb, UINT32 v255)
{
    if (v255 >= 255)
        return pargb;
    if (!v255)
        return 0;
    UINT32 a = div255(v255 * (pargb >> 24)), r = div255(v255 * ((pargb >> 16) & 255)),
           g = div255(v255 * ((pargb >> 8) & 255)), b = div255(v255 * (pargb & 255));
    return a << 24 | r << 16 | g << 8 | b;
}

static LONG volatile g_token;
/* GdiplusStartup calls not yet matched by GdiplusShutdown. */
static LONG volatile g_init_count;
static ULONG_PTR g_init_token;

BOOL gp_started(void) { return g_init_count > 0; }

static GpStatus WINAPI notification_hook(ULONG_PTR *token)
{
    if (!token)
        return InvalidParameter;
    *token = (ULONG_PTR)InterlockedIncrement(&g_token);
    return Ok;
}

static void WINAPI notification_unhook(ULONG_PTR token)
{
}

/* the first startup makes the token, later ones count up from it. */
WINGDIPAPI GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input,
                                          GdiplusStartupOutput *output)
{
    if (!token || !input || (input->SuppressBackgroundThread && !output))
        return InvalidParameter;
    /* version 1 is gdi+ 1.0, 2 and 3 are the GdiplusStartupInputEx of gdi+ 1.1. */
    if (input->GdiplusVersion < 1 || input->GdiplusVersion > 3)
        return UnsupportedGdiplusVersion;
    LONG count = InterlockedIncrement(&g_init_count);
    if (count == 1)
        g_init_token = (ULONG_PTR)(GetTickCount() & 0x7fffff) << 8 | 0x10;
    *token = g_init_token + (ULONG_PTR)(count - 1);
    if (input->SuppressBackgroundThread) {
        output->NotificationHook = notification_hook;
        output->NotificationUnhook = notification_unhook;
    }
    return Ok;
}

/* the token is ignored: every call undoes one startup. */
WINGDIPAPI VOID WINAPI GdiplusShutdown(ULONG_PTR token)
{
    LONG count = g_init_count;
    while (count > 0) {
        LONG seen = InterlockedCompareExchange(&g_init_count, count - 1, count);
        if (seen == count)
            break;
        count = seen;
    }
}

WINGDIPAPI GpStatus WINAPI GdiplusNotificationHook(ULONG_PTR *token)
{
    return notification_hook(token);
}

WINGDIPAPI VOID WINAPI GdiplusNotificationUnhook(ULONG_PTR token)
{
}

/* only while the library is started. */
WINGDIPAPI void *WINAPI GdipAlloc(SIZE_T size)
{
    return gp_started() ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
}

WINGDIPAPI void WINAPI GdipFree(void *p)
{
    gp_free(p);
}

/* the constants of the sdk. */

#define GUID_DEF(name, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    const GUID name = {a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}}

GUID_DEF(ImageFormatUndefined, 0xb96b3ca9, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatMemoryBMP, 0xb96b3caa, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatBMP, 0xb96b3cab, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatEMF, 0xb96b3cac, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatWMF, 0xb96b3cad, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatJPEG, 0xb96b3cae, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatPNG, 0xb96b3caf, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatGIF, 0xb96b3cb0, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatTIFF, 0xb96b3cb1, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatEXIF, 0xb96b3cb2, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);
GUID_DEF(ImageFormatIcon, 0xb96b3cb5, 0x0728, 0x11d3, 0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e);

GUID_DEF(FrameDimensionTime, 0x6aedbd6d, 0x3fb5, 0x418a, 0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72);
GUID_DEF(FrameDimensionResolution, 0x84236f7b, 0x3bd3, 0x428f, 0x8d, 0xab, 0x4e, 0xa1, 0x43, 0x9c, 0xa3, 0x15);
GUID_DEF(FrameDimensionPage, 0x7462dc86, 0x6180, 0x4c7e, 0x8e, 0x3f, 0xee, 0x73, 0x33, 0xa7, 0xa4, 0x83);

GUID_DEF(EncoderCompression, 0xe09d739d, 0xccd4, 0x44ee, 0x8e, 0xba, 0x3f, 0xbf, 0x8b, 0xe4, 0xfc, 0x58);
GUID_DEF(EncoderColorDepth, 0x66087055, 0xad66, 0x4c7c, 0x9a, 0x18, 0x38, 0xa2, 0x31, 0x0b, 0x83, 0x37);
GUID_DEF(EncoderScanMethod, 0x3a4e2661, 0x3109, 0x4e56, 0x85, 0x36, 0x42, 0xc1, 0x56, 0xe7, 0xdc, 0xfa);
GUID_DEF(EncoderVersion, 0x24d18c76, 0x814a, 0x41a4, 0xbf, 0x53, 0x1c, 0x21, 0x9c, 0xcc, 0xf7, 0x97);
GUID_DEF(EncoderRenderMethod, 0x6d42c53a, 0x229a, 0x4825, 0x8b, 0xb7, 0x5c, 0x99, 0xe2, 0xb9, 0xa8, 0xb8);
GUID_DEF(EncoderQuality, 0x1d5be4b5, 0xfa4a, 0x452d, 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb);
GUID_DEF(EncoderTransformation, 0x8d0eb2d1, 0xa58e, 0x4ea8, 0xaa, 0x14, 0x10, 0x80, 0x74, 0xb7, 0xb6, 0xf9);
GUID_DEF(EncoderLuminanceTable, 0xedb33bce, 0x0266, 0x4a77, 0xb9, 0x04, 0x27, 0x21, 0x60, 0x99, 0xe7, 0x17);
GUID_DEF(EncoderChrominanceTable, 0xf2e455dc, 0x09b3, 0x4316, 0x82, 0x60, 0x67, 0x6a, 0xda, 0x32, 0x48, 0x1c);
GUID_DEF(EncoderSaveFlag, 0x292266fc, 0xac40, 0x47bf, 0x8c, 0xfc, 0xa8, 0x5b, 0x89, 0xa6, 0x55, 0xde);
GUID_DEF(EncoderColorSpace, 0xae7a62a0, 0xee2c, 0x49d8, 0x9d, 0x07, 0x1b, 0xa8, 0xa9, 0x27, 0x59, 0x6e);
GUID_DEF(EncoderImageItems, 0x63875e13, 0x1f1d, 0x45ab, 0x91, 0x95, 0xa2, 0x9b, 0x60, 0x66, 0xa6, 0x50);
GUID_DEF(EncoderSaveAsCMYK, 0xa219bbc9, 0x0a9d, 0x4005, 0xa3, 0xee, 0x3a, 0x42, 0x1b, 0x8b, 0xb0, 0x6c);

GUID_DEF(BlurEffectGuid, 0x633c80a4, 0x1843, 0x482b, 0x9e, 0xf2, 0xbe, 0x28, 0x34, 0xc5, 0xfd, 0xd4);
GUID_DEF(SharpenEffectGuid, 0x63cbf3ee, 0xc526, 0x402c, 0x8f, 0x71, 0x62, 0xc5, 0x40, 0xbf, 0x51, 0x42);
GUID_DEF(ColorMatrixEffectGuid, 0x718f2615, 0x7933, 0x40e3, 0xa5, 0x11, 0x5f, 0x68, 0xfe, 0x14, 0xdd, 0x74);
GUID_DEF(ColorLUTEffectGuid, 0xa7ce72a9, 0x0f7f, 0x40d7, 0xb3, 0xcc, 0xd0, 0xc0, 0x2d, 0x5c, 0x32, 0x12);
GUID_DEF(BrightnessContrastEffectGuid, 0xd3a1dbe1, 0x8ec4, 0x4c17, 0x9f, 0x4c, 0xea, 0x97, 0xad, 0x1c, 0x34, 0x3d);
GUID_DEF(HueSaturationLightnessEffectGuid, 0x8b2dd6c3, 0xeb07, 0x4d87, 0xa5, 0xf0, 0x71, 0x08, 0xe2, 0x6a, 0x9c, 0x5f);
GUID_DEF(LevelsEffectGuid, 0x99c354ec, 0x2a31, 0x4f3a, 0x8c, 0x34, 0x17, 0xa8, 0x03, 0xb3, 0x3a, 0x25);
GUID_DEF(TintEffectGuid, 0x1077af00, 0x2848, 0x4441, 0x94, 0x89, 0x44, 0xad, 0x4c, 0x2d, 0x7a, 0x2c);
GUID_DEF(ColorBalanceEffectGuid, 0x537e597d, 0x251e, 0x48da, 0x96, 0x64, 0x29, 0xca, 0x49, 0x6b, 0x70, 0xf8);
GUID_DEF(RedEyeCorrectionEffectGuid, 0x74d29d05, 0x69a4, 0x4266, 0x95, 0x49, 0x3c, 0xc5, 0x28, 0x36, 0xb6, 0x32);
GUID_DEF(ColorCurveEffectGuid, 0xdd6a0022, 0x58e4, 0x4a67, 0x9d, 0x9b, 0xd4, 0x8e, 0xb8, 0x81, 0xa5, 0x3d);
