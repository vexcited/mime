/*
 * surface creation, as windows checks and builds surfaces: the desc and
 * caps checks of checkSurfaceDesc and checkCaps, the memory a surface
 * goes in, the surfaces of a complex one and how they are linked, their
 * caps, sizes, formats and pitches. mime's "video memory" is memory like
 * the rest, created where a driver of the windows display driver model
 * would create it. see docs/ddraw.md.
 */
#include "ddraw_private.h"
#include <mime/win32/dxgi.h>

#define DDERR_NOEXECUTEHW MAKE_DDHRESULT(593)

/* the surface caps of the driver and of the emulation: what each can make. */
#define HAL_SURFACE_CAPS 0x30C6727Cu
#define HEL_SURFACE_CAPS 0x00C21350u

/* ------------------------------------------------------------------ formats */

typedef struct KNOWN {
    DWORD flags, bits, m0, m1, m2, m3;
} KNOWN;

/* windows' emulation: the formats of textures and cube maps. */
static const KNOWN TEXTURE_FORMATS[] = {
    {0x840, 1}, {0x850, 1}, {0x1040, 2}, {0x1050, 2}, {0x48, 4}, {0x58, 4}, {0x60, 8},
    {0x40, 8, 0xE0, 0x1C, 0x03, 0},
    {0x41, 16, 0xF00, 0xF0, 0xF, 0xF000},
    {0x40, 16, 0xF800, 0x7E0, 0x1F, 0},
    {0x40, 16, 0x1F, 0x7E0, 0xF800, 0},
    {0x40, 16, 0x7C00, 0x3E0, 0x1F, 0},
    {0x41, 16, 0x7C00, 0x3E0, 0x1F, 0x8000},
    {0x40, 24, 0xFF0000, 0xFF00, 0xFF, 0},
    {0x40, 24, 0xFF, 0xFF00, 0xFF0000, 0},
    {0x40, 32, 0xFF0000, 0xFF00, 0xFF, 0},
    {0x40, 32, 0xFF, 0xFF00, 0xFF0000, 0},
    {0x41, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
    {0x41, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000},
    {0x20000, 8, 0xFF},
    {0x20001, 16, 0xFF, 0, 0, 0xFF00},
    {0x80000, 16, 0xFF, 0xFF00},
    {0xC0000, 16, 0x1F, 0x3E0, 0xFC00},
    {0xC0000, 24, 0xFF, 0xFF00, 0xFF0000},
    {0x400, 16, 0, 0xFFFF},
    {0x4400, 32, 8, 0xFFFFFF, 0xFF000000},
    {0x4400, 32, 8, 0xFFFFFF00, 0xFF},
    {0x4400, 16, 1, 0xFFFE, 0x1},
    {0x4400, 16, 1, 0x7FFF, 0x8000},
    {0x400, 32, 0, 0xFFFFFFFF},
    {0x4, 0, FOURCC_DXT1}, {0x4, 0, FOURCC_DXT2}, {0x4, 0, FOURCC_DXT3}, {0x4, 0, FOURCC_DXT4}, {0x4, 0, FOURCC_DXT5},
    {0x2, 8},
    {0x20001, 8, 0xF, 0, 0, 0xF0},
    {0x41, 16, 0xE0, 0x1C, 0x03, 0xFF00},
    {0x61, 16, 0, 0, 0, 0xFF00},
    {0x4, 0, MAKEFOURCC('U', 'Y', 'V', 'Y')}, {0x4, 0, MAKEFOURCC('Y', 'U', 'Y', '2')},
    {0x40, 16, 0xF00, 0xF0, 0xF, 0},
    {0xC0000, 32, 0xFF, 0xFF00, 0xFF0000},
};

/* windows' emulation: the formats of offscreen plain surfaces other than the display's. */
static const KNOWN OFFSCREEN_FORMATS[] = {
    {0x840, 1}, {0x1040, 2}, {0x48, 4}, {0x60, 8},
    {0x40, 16, 0xF800, 0x7E0, 0x1F, 0},
    {0x40, 16, 0x7C00, 0x3E0, 0x1F, 0},
    {0x8041, 16, 0x7C00, 0x3E0, 0x1F, 0x8000},
    {0x41, 16, 0x7C00, 0x3E0, 0x1F, 0x8000},
    {0x40, 24, 0xFF0000, 0xFF00, 0xFF, 0},
    {0x40, 24, 0xFF, 0xFF00, 0xFF0000, 0},
    {0x40, 32, 0xFF0000, 0xFF00, 0xFF, 0},
    {0x40, 32, 0xFF, 0xFF00, 0xFF0000, 0},
    {0x8041, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
    {0x41, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
    {0x8041, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000},
    {0x41, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000},
    {0x2040, 32, 0xF800, 0x7E0, 0x1F, 0xFFFF0000},
    {0x2040, 32, 0x7C00, 0x3E0, 0x1F, 0xFFFF0000},
    {0x8041, 16, 0xF00, 0xF0, 0xF, 0xF000},
    {0x41, 16, 0xF00, 0xF0, 0xF, 0xF000},
};

/* windows' compare of a format with a known one: the flags exactly, then what those flags give meaning to. */
static BOOL format_is(const DDPIXELFORMAT *pf, const KNOWN *k)
{
    if (pf->dwFlags != k->flags)
        return FALSE;
    DWORD flags = pf->dwFlags;
    if (flags & (DDPF_RGB | DDPF_YUV)) {
        if (pf->dwRGBBitCount != k->bits)
            return FALSE;
        if (!(flags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) &&
            (pf->dwRBitMask != k->m0 || pf->dwGBitMask != k->m1 || pf->dwBBitMask != k->m2))
            return FALSE;
        if ((flags & (DDPF_ALPHAPIXELS | DDPF_ZPIXELS)) && pf->dwRGBAlphaBitMask != k->m3)
            return FALSE;
        return TRUE;
    }
    if (flags & DDPF_ZBUFFER) {
        if (pf->dwZBufferBitDepth != k->bits || pf->dwZBitMask != k->m1)
            return FALSE;
        return !(flags & DDPF_STENCILBUFFER) || (pf->dwStencilBitDepth == k->m0 && pf->dwStencilBitMask == k->m2);
    }
    if (flags & DDPF_LUMINANCE)
        return pf->dwLuminanceBitCount == k->bits && pf->dwLuminanceBitMask == k->m0 &&
               (!(flags & DDPF_ALPHAPIXELS) || pf->dwLuminanceAlphaBitMask == k->m3);
    if (flags & DDPF_BUMPDUDV)
        return pf->dwBumpBitCount == k->bits && pf->dwBumpDuBitMask == k->m0 && pf->dwBumpDvBitMask == k->m1 &&
               (!(flags & DDPF_BUMPLUMINANCE) || pf->dwBumpLuminanceBitMask == k->m2);
    if (flags & DDPF_FOURCC)
        return pf->dwFourCC == k->m0;
    if (flags & DDPF_ALPHA)
        return pf->dwAlphaBitDepth == k->bits;
    return TRUE;
}

static BOOL format_in(const DDPIXELFORMAT *pf, const KNOWN *table, DWORD count)
{
    for (DWORD i = 0; i < count; i++)
        if (format_is(pf, &table[i]))
            return TRUE;
    return FALSE;
}

/* the display format: rgb, palettized at 8 bits, 565 at 16. */
static void display_format(DDPIXELFORMAT *pf, DWORD *bpp_out)
{
    DDMODE mode;
    DdCurrentMode(&mode);
    DdPixelFormatForBpp(mode.bpp, pf);
    if (bpp_out)
        *bpp_out = mode.bpp;
}

/* windows' doPixelFormatsMatch, by what a format's flags give meaning to. */
static BOOL formats_match(const DDPIXELFORMAT *a, const DDPIXELFORMAT *b)
{
    if (a->dwFlags != b->dwFlags)
        return FALSE;
    if (!(a->dwFlags & (DDPF_FOURCC | DDPF_YUV)) && a->dwRGBBitCount != b->dwRGBBitCount)
        return FALSE;
    if ((a->dwFlags & DDPF_RGB) && !(a->dwFlags & DDPF_PALETTEINDEXED8) &&
        (a->dwRBitMask != b->dwRBitMask || a->dwGBitMask != b->dwGBitMask || a->dwBBitMask != b->dwBBitMask))
        return FALSE;
    if ((a->dwFlags & (DDPF_ALPHAPIXELS | DDPF_ZPIXELS)) && a->dwRGBAlphaBitMask != b->dwRGBAlphaBitMask)
        return FALSE;
    if ((a->dwFlags & DDPF_FOURCC) && a->dwFourCC != b->dwFourCC)
        return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ checkCaps */

static HRESULT check_caps(DWORD caps, DWORD version, const DDSCAPS2 *c2)
{
    DWORD caps2 = c2->dwCaps2, caps3 = c2->dwCaps3, caps4 = c2->dwCaps4;
    if (!caps || (caps & 0x3000000) || (caps2 & 1))
        return DDERR_INVALIDCAPS;
    if ((caps3 & 0xFFFFE000) || (!(caps2 & DDSCAPS2_VOLUME) && caps4))
        return DDERR_INVALIDCAPS;
    if (caps & (DDSCAPS_VISIBLE | DDSCAPS_PALETTE))
        return DDERR_INVALIDCAPS;
    DWORD managed = caps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE);
    if ((caps & (DDSCAPS_WRITEONLY | DDSCAPS_EXECUTEBUFFER)) == DDSCAPS_WRITEONLY && !managed)
        return DDERR_INVALIDCAPS;
    if ((caps2 & DDSCAPS2_DONOTPERSIST) && !managed)
        return DDERR_INVALIDCAPS;
    if (managed && !(caps & DDSCAPS_TEXTURE))
        return DDERR_INVALIDCAPS;
    if (managed == (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))
        return DDERR_INVALIDCAPS;
    if ((caps2 & (DDSCAPS2_HINTDYNAMIC | DDSCAPS2_HINTSTATIC | DDSCAPS2_OPAQUE)) && !(caps & DDSCAPS_TEXTURE))
        return DDERR_INVALIDCAPS;
    if ((caps2 & (DDSCAPS2_HINTDYNAMIC | DDSCAPS2_HINTSTATIC)) == (DDSCAPS2_HINTDYNAMIC | DDSCAPS2_HINTSTATIC) ||
        (caps2 & (DDSCAPS2_OPAQUE | DDSCAPS2_HINTDYNAMIC)) == (DDSCAPS2_OPAQUE | DDSCAPS2_HINTDYNAMIC) ||
        (caps2 & (DDSCAPS2_OPAQUE | DDSCAPS2_HINTSTATIC)) == (DDSCAPS2_OPAQUE | DDSCAPS2_HINTSTATIC))
        return DDERR_INVALIDCAPS;
    if (caps & DDSCAPS_OPTIMIZED)
        return DDERR_INVALIDCAPS;
    if ((caps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) &&
        ((caps & DDSCAPS_SYSTEMMEMORY) ||
         (caps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) == (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)))
        return DDERR_INVALIDCAPS;
    if (version != DDV1 && (caps & (DDSCAPS_BACKBUFFER | DDSCAPS_FRONTBUFFER)))
        return DDERR_INVALIDCAPS;
    if ((caps & 0xFF7EB7FF) && (caps & DDSCAPS_EXECUTEBUFFER))
        return DDERR_INVALIDCAPS;
    if (caps & DDSCAPS_COMPLEX) {
        if (caps & DDSCAPS_FRONTBUFFER)
            return DDERR_INVALIDCAPS;
        if ((caps & DDSCAPS_BACKBUFFER) && (!(caps & (DDSCAPS_ALPHA | DDSCAPS_ZBUFFER)) || (caps & DDSCAPS_FLIP)))
            return DDERR_INVALIDCAPS;
        if (!(caps & 0x12C4) || (!(caps & 0x420012) && !(caps2 & DDSCAPS2_CUBEMAP)))
            return DDERR_INVALIDCAPS;
    }
    if (((caps & (DDSCAPS_FRONTBUFFER | DDSCAPS_ALPHA)) && (caps & DDSCAPS_BACKBUFFER)) ||
        ((caps & DDSCAPS_FLIP) && !(caps & DDSCAPS_COMPLEX)))
        return DDERR_INVALIDCAPS;
    if ((caps & DDSCAPS_PRIMARYSURFACE) &&
        ((caps & 0x10C4) || (caps & (DDSCAPS_ZBUFFER | DDSCAPS_FLIP)) == DDSCAPS_ZBUFFER))
        return DDERR_INVALIDCAPS;
    DWORD zf = caps & (DDSCAPS_ZBUFFER | DDSCAPS_FLIP);
    if (((caps & 0x1084) && (caps & DDSCAPS_OFFSCREENPLAIN)) ||
        ((caps & DDSCAPS_OVERLAY) && ((caps & 0x1044) || zf == DDSCAPS_ZBUFFER)) ||
        ((caps & DDSCAPS_MIPMAP) && (!(caps & DDSCAPS_TEXTURE) || (caps & DDSCAPS_ZBUFFER))) ||
        (caps & (DDSCAPS_ZBUFFER | DDSCAPS_COMPLEX | DDSCAPS_BACKBUFFER)) == (DDSCAPS_ZBUFFER | DDSCAPS_BACKBUFFER) ||
        ((caps & DDSCAPS_ALLOCONLOAD) && !(caps & DDSCAPS_TEXTURE)) ||
        ((caps2 & DDSCAPS2_CUBEMAP_ALLFACES) && !(caps2 & DDSCAPS2_CUBEMAP)))
        return DDERR_INVALIDCAPS;
    if (caps2 & DDSCAPS2_CUBEMAP) {
        BOOL simple = !(caps3 & 0xFFFFE5FF) && !(caps2 & 0xF7A90060) && !(caps & 0xC3BA87F7);
        if (!simple || c2->dwCaps4 || !(caps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE)))
            return DDERR_INVALIDPARAMS;
        if (!(caps & DDSCAPS_COMPLEX))
            return DDERR_DDSCAPSCOMPLEXREQUIRED;
    }
    if ((caps2 & DDSCAPS2_STEREOSURFACELEFT) &&
        (caps & 0x4218) != (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_VIDEOMEMORY))
        return DDERR_UNSUPPORTED;
    return DD_OK;
}

/* ------------------------------------------------------------------ checkSurfaceDesc */

static BOOL power_of_two(DWORD v, DWORD *log)
{
    if (!v || (v & (v - 1)))
        return FALSE;
    if (log) {
        DWORD n = 0;
        while (v >>= 1)
            n++;
        *log = n;
    }
    return TRUE;
}

/* keys windows' surfaces can have at creation: the driver's for video memory, the emulation's for system memory. */
static HRESULT check_key(DWORD slot, const DDCOLORKEY *key, DWORD caps)
{
    BOOL range = key->dwColorSpaceLowValue != key->dwColorSpaceHighValue;
    BOOL hel = (caps & DDSCAPS_SYSTEMMEMORY) != 0;
    switch (slot) {
    case DDSD_CKSRCBLT:
        return range ? DDERR_NOCOLORKEYHW : DD_OK;
    case DDSD_CKDESTBLT:
        return range || hel ? DDERR_NOCOLORKEYHW : DD_OK;
    default:
        return DDERR_NOCOLORKEYHW;
    }
}

static HRESULT check_desc(DDSURFACEDESC2 *d, DWORD version, BOOL v1_desc)
{
    DWORD caps = d->ddsCaps.dwCaps, flags = d->dwFlags;
    if (caps & DDSCAPS_COMPLEX) {
        if ((flags & (DDSD_BACKBUFFERCOUNT | DDSD_MIPMAPCOUNT)) == (DDSD_BACKBUFFERCOUNT | DDSD_MIPMAPCOUNT))
            return DDERR_INVALIDPARAMS;
    } else if (flags & (DDSD_BACKBUFFERCOUNT | DDSD_MIPMAPCOUNT)) {
        return DDERR_INVALIDCAPS;
    }
    if (caps & DDSCAPS_FLIP) {
        if (!(caps & DDSCAPS_COMPLEX) || !(flags & DDSD_BACKBUFFERCOUNT) || !d->dwBackBufferCount)
            return DDERR_INVALIDCAPS;
        if (flags & DDSD_MIPMAPCOUNT)
            return DDERR_UNSUPPORTED;
    }
    HRESULT hr = check_caps(caps, version, &d->ddsCaps);
    if (FAILED(hr))
        return hr;
    if ((caps & DDSCAPS_ALPHA) || (flags & DDSD_ALPHABITDEPTH))
        return DDERR_INVALIDPARAMS;
    DDPIXELFORMAT *pf = &d->ddpfPixelFormat;
    if (caps & DDSCAPS_ZBUFFER) {
        if (!v1_desc) {
            if ((caps & DDSCAPS_COMPLEX) || (flags & DDSD_ZBUFFERBITDEPTH) || !(flags & DDSD_PIXELFORMAT) ||
                pf->dwSize != sizeof(DDPIXELFORMAT) || !(pf->dwFlags & DDPF_ZBUFFER) || !pf->dwZBitMask)
                return DDERR_INVALIDPARAMS;
            if (!(pf->dwFlags & DDPF_STENCILBUFFER))
                d->dwMipMapCount = pf->dwZBufferBitDepth;
        } else if (flags & DDSD_PIXELFORMAT) {
            if (pf->dwSize != sizeof(DDPIXELFORMAT) || (pf->dwFlags & DDPF_STENCILBUFFER) || !(pf->dwFlags & DDPF_ZBUFFER))
                return DDERR_INVALIDPARAMS;
            d->dwFlags = flags |= DDSD_ZBUFFERBITDEPTH;
            d->dwZBufferBitDepth = pf->dwZBufferBitDepth;
        } else {
            if (!(flags & DDSD_ZBUFFERBITDEPTH))
                return DDERR_INVALIDPARAMS;
            if (!(caps & DDSCAPS_COMPLEX)) {
                memset(pf, 0, sizeof(*pf));
                pf->dwSize = sizeof(*pf);
                pf->dwFlags = DDPF_ZBUFFER;
                pf->dwZBufferBitDepth = d->dwZBufferBitDepth;
                pf->dwZBitMask = d->dwZBufferBitDepth >= 32 ? 0xFFFFFFFF : (1u << d->dwZBufferBitDepth) - 1;
                d->dwFlags = flags |= DDSD_PIXELFORMAT;
            }
        }
        DWORD depth = (caps & DDSCAPS_COMPLEX) ? d->dwZBufferBitDepth : pf->dwZBufferBitDepth;
        if (depth < 8 || ((pf->dwFlags & DDPF_STENCILBUFFER) && (!pf->dwStencilBitDepth || !pf->dwStencilBitMask)))
            return DDERR_INVALIDPARAMS;
    } else {
        if (flags & DDSD_ZBUFFERBITDEPTH)
            return DDERR_INVALIDPARAMS;
        if ((flags & DDSD_PIXELFORMAT) && (pf->dwFlags & (DDPF_ZBUFFER | DDPF_STENCILBUFFER)))
            return DDERR_INVALIDPARAMS;
    }
    if (flags & DDSD_HEIGHT) {
        if ((caps & DDSCAPS_PRIMARYSURFACE) || !d->dwHeight)
            return DDERR_INVALIDPARAMS;
    } else if (!(caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_EXECUTEBUFFER))) {
        return DDERR_INVALIDPARAMS;
    }
    if (flags & DDSD_WIDTH) {
        if ((caps & DDSCAPS_PRIMARYSURFACE) || !d->dwWidth)
            return DDERR_INVALIDPARAMS;
    } else if (!(caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_EXECUTEBUFFER))) {
        return DDERR_INVALIDPARAMS;
    }
    if (caps & DDSCAPS_MIPMAP) {
        if ((flags & DDSD_MIPMAPCOUNT) && !d->dwMipMapCount)
            return DDERR_INVALIDPARAMS;
        DWORD lw = 0, lh = 0;
        if ((flags & DDSD_WIDTH) && !power_of_two(d->dwWidth, &lw))
            return DDERR_INVALIDPARAMS;
        if ((flags & DDSD_HEIGHT) && !power_of_two(d->dwHeight, &lh))
            return DDERR_INVALIDPARAMS;
        DWORD most = lw > lh ? lw : lh;
        if ((flags & DDSD_MIPMAPCOUNT) && d->dwMipMapCount > most + 1)
            return DDERR_INVALIDPARAMS;
    }
    if (flags & DDSD_PIXELFORMAT) {
        if (caps & DDSCAPS_PRIMARYSURFACE)
            return DDERR_INVALIDPARAMS;
        DWORD pff = pf->dwFlags;
        if (!(pff & 0xE2463)) {
            if (pff & DDPF_FOURCC) {
                DWORD block;
                switch (pf->dwFourCC) {
                case FOURCC_DXT1:
                case FOURCC_DXT2:
                case FOURCC_DXT3:
                case FOURCC_DXT4:
                case FOURCC_DXT5:
                    block = 1;
                    break;
                default:
                    block = 0;
                }
                if (block && (((flags & DDSD_WIDTH) && (d->dwWidth & 3)) || ((flags & DDSD_HEIGHT) && (d->dwHeight & 3))))
                    return DDERR_INVALIDPARAMS;
            }
        } else {
            DWORD bits = pf->dwRGBBitCount;
            if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
                return DDERR_INVALIDPIXELFORMAT;
            if ((pff & (DDPF_YUV | DDPF_RGB)) == (DDPF_YUV | DDPF_RGB))
                return DDERR_INVALIDPIXELFORMAT;
            if ((pff & 0x27E6D) && (pff & DDPF_ALPHA))
                return DDERR_INVALIDPARAMS;
            if ((pff & DDPF_LUMINANCE) && (pff & ~(DDPF_LUMINANCE | DDPF_ALPHAPIXELS)))
                return DDERR_INVALIDPIXELFORMAT;
            if ((pff & DDPF_BUMPDUDV) && (pff & ~(DDPF_BUMPDUDV | DDPF_BUMPLUMINANCE)))
                return DDERR_INVALIDPIXELFORMAT;
            if (pff & DDPF_PALETTEINDEXED8) {
                if (pff & (DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXEDTO8 | DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2))
                    return DDERR_INVALIDPIXELFORMAT;
                pf->dwRBitMask = pf->dwGBitMask = pf->dwBBitMask = 0;
                if (!(pff & (DDPF_ALPHAPIXELS | DDPF_ZPIXELS)))
                    pf->dwRGBAlphaBitMask = 0;
            }
            pf->dwFourCC = 0;
            if ((pff & DDPF_ZPIXELS) && (!pf->dwRGBZBitMask || (pff & (DDPF_RGBTOYUV | DDPF_ALPHAPIXELS | DDPF_ALPHA))))
                return DDERR_INVALIDPIXELFORMAT;
        }
    }
    static const DWORD KEYS[] = {DDSD_CKDESTOVERLAY, DDSD_CKDESTBLT, DDSD_CKSRCOVERLAY, DDSD_CKSRCBLT};
    const DDCOLORKEY *key_values[] = {&d->ddckCKDestOverlay, &d->ddckCKDestBlt, &d->ddckCKSrcOverlay, &d->ddckCKSrcBlt};
    for (int i = 0; i < 4; i++)
        if (flags & KEYS[i]) {
            hr = check_key(KEYS[i], key_values[i], caps);
            if (FAILED(hr))
                return hr;
        }
    if (d->ddsCaps.dwCaps2 & DDSCAPS2_CUBEMAP) {
        if (version == DDV7 && (flags & (DDSD_WIDTH | DDSD_HEIGHT)) && d->dwWidth == d->dwHeight &&
            power_of_two(d->dwWidth, nullptr) && !(flags & DDSD_BACKBUFFERCOUNT))
            return DD_OK;
        return DDERR_INVALIDPARAMS;
    }
    return DD_OK;
}

/* windows' initMipMapDim: the size of a level count, or the count of a size. */
static HRESULT mip_dims(DDSURFACEDESC2 *d, DWORD version)
{
    if ((d->ddsCaps.dwCaps & (DDSCAPS_COMPLEX | DDSCAPS_FLIP)) != DDSCAPS_COMPLEX)
        return DD_OK;
    DWORD sizes = d->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT);
    if (sizes == (DDSD_WIDTH | DDSD_HEIGHT) && !(d->dwFlags & DDSD_MIPMAPCOUNT)) {
        DWORD lw, lh;
        if (!power_of_two(d->dwWidth, &lw) || !power_of_two(d->dwHeight, &lh))
            return DDERR_INVALIDPARAMS;
        d->dwMipMapCount = (version == DDV7 ? (lw > lh ? lw : lh) : (lw < lh ? lw : lh)) + 1;
        d->dwFlags |= DDSD_MIPMAPCOUNT;
    } else if (!sizes && (d->dwFlags & DDSD_MIPMAPCOUNT)) {
        if (!d->dwMipMapCount || d->dwMipMapCount > 32)
            return DDERR_INVALIDPARAMS;
        d->dwWidth = d->dwHeight = 1u << (d->dwMipMapCount - 1);
        d->dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
    }
    return DD_OK;
}

/* ------------------------------------------------------------------ memory */

static DWORD block_size(DWORD fourcc)
{
    switch (fourcc) {
    case FOURCC_DXT1:
        return 8;
    case FOURCC_DXT2:
    case FOURCC_DXT3:
    case FOURCC_DXT4:
    case FOURCC_DXT5:
        return 16;
    default:
        return 0;
    }
}

/* system memory: rows a multiple of 4 bytes, zeroed; block compressed surfaces of transparent black blocks. */
static BOOL allocate(DDSURF *s)
{
    DWORD w = s->desc.dwWidth, h = s->desc.dwHeight;
    SIZE_T size;
    if (s->format.block) {
        size = (SIZE_T)s->format.block * ((w + 3) / 4) * ((h + 3) / 4);
        s->desc.lPitch = (LONG)size;
    } else if (s->desc.ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) {
        size = (SIZE_T)((s->desc.lPitch + 3) & ~3);
    } else {
        SIZE_T row = ((SIZE_T)w * s->format.bpp + 7) / 8;
        if (!row)
            row = 1;
        s->desc.lPitch = (LONG)((row + 3) & ~(SIZE_T)3);
        size = (SIZE_T)s->desc.lPitch * h;
    }
    if (!size)
        size = 4;
    DDMEM *mem = HeapAlloc(GetProcessHeap(), 0, sizeof(*mem));
    BYTE *bytes = mem ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size) : nullptr;
    if (!bytes) {
        HeapFree(GetProcessHeap(), 0, mem);
        return FALSE;
    }
    if (s->format.block) {
        static const BYTE black[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        DWORD b = s->format.block;
        for (SIZE_T at = 0; at + b <= size; at += b)
            memcpy(bytes + at, black + 16 - b, b);
    }
    mem->ref = 1;
    mem->base = bytes;
    s->mem = mem;
    s->memory = bytes;
    s->bytes = size;
    return TRUE;
}

ULONG64 DdVideoMemoryUsed(void)
{
    ULONG64 used = 0;
    for (LIST_ENTRY *o = DdObjects.Flink; o != &DdObjects; o = o->Flink) {
        DDRAW *dd = CONTAINING_RECORD(o, DDRAW, links);
        for (LIST_ENTRY *e = dd->surfaces.Flink; e != &dd->surfaces; e = e->Flink) {
            DDSURF *s = CONTAINING_RECORD(e, DDSURF, links);
            if (s->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY)
                used += s->bytes;
        }
    }
    return used;
}

/* ------------------------------------------------------------------ building */

static void free_partial(DDSURF **list, DWORD n)
{
    for (DWORD i = 0; i < n; i++) {
        DDSURF *s = list[i];
        if (!s)
            continue;
        while (!IsListEmpty(&s->attached)) {
            DDATTACH *at = CONTAINING_RECORD(s->attached.Flink, DDATTACH, links);
            RemoveEntryList(&at->links);
            HeapFree(GetProcessHeap(), 0, at);
        }
        if (s->mem && !--s->mem->ref) {
            HeapFree(GetProcessHeap(), 0, s->mem->base);
            HeapFree(GetProcessHeap(), 0, s->mem);
        }
        HeapFree(GetProcessHeap(), 0, s);
    }
}

static BOOL link_implicit(DDSURF *from, DDSURF *to)
{
    DDATTACH *at = HeapAlloc(GetProcessHeap(), 0, sizeof(*at));
    if (!at)
        return FALSE;
    at->surface = to;
    at->version = DDV1;
    at->implicit = TRUE;
    InsertHeadList(&from->attached, &at->links);
    return TRUE;
}

/*
 * windows' createSurface after the checks: the surfaces of the structure,
 * their caps from the root's, their sizes, formats and memory, linked as
 * windows links them.
 */
static HRESULT build(DDRAW *dd, DWORD version, DDSURFACEDESC2 *d, BOOL hal, BOOL own_format_forced, DDSURF **out)
{
    DWORD caps = d->ddsCaps.dwCaps, caps2 = d->ddsCaps.dwCaps2;
    DWORD n = 0;
    if (d->dwFlags & DDSD_BACKBUFFERCOUNT) {
        if (d->dwBackBufferCount >= 0x80000000)
            return DDERR_INVALIDPARAMS;
        n = d->dwBackBufferCount;
    } else if (d->dwFlags & DDSD_MIPMAPCOUNT) {
        if (!d->dwMipMapCount)
            return DDERR_INVALIDPARAMS;
        n = d->dwMipMapCount - 1;
    }
    DWORD surface_caps = hal ? HAL_SURFACE_CAPS : HEL_SURFACE_CAPS;
    if ((caps & DDSCAPS_ALPHA) && !(surface_caps & DDSCAPS_ALPHA))
        return DDERR_NOALPHAHW;
    if (caps2 & DDSCAPS2_STEREOSURFACELEFT)
        return DDERR_NOSTEREOHARDWARE;
    if ((caps & DDSCAPS_ZBUFFER) && !(surface_caps & DDSCAPS_ZBUFFER))
        return DDERR_NOZBUFFERHW;
    if ((caps & DDSCAPS_TEXTURE) && !(surface_caps & DDSCAPS_TEXTURE))
        return DDERR_NOTEXTUREHW;
    if ((caps & DDSCAPS_MIPMAP) && !(surface_caps & DDSCAPS_MIPMAP))
        return DDERR_NOMIPMAPHW;
    if ((caps & DDSCAPS_EXECUTEBUFFER) && !(surface_caps & DDSCAPS_EXECUTEBUFFER))
        return DDERR_NOEXECUTEHW;
    if (caps & DDSCAPS_OVERLAY)
        return DDERR_NOOVERLAYHW;
    DDRAW *owner = DdExclusiveOwner();
    BOOL we_exclusive = owner == dd && (dd->flags & DDF_FULLSCREEN);
    if ((caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP)) == (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP) && !we_exclusive)
        return DDERR_NOEXCLUSIVEMODE;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        if (dd->primary)
            return DDERR_PRIMARYSURFACEALREADYEXISTS;
        if (owner && owner != dd)
            return DDERR_NOEXCLUSIVEMODE;
    }
    /* the format: the desc's, else the display's; a surface keeps its own when asked or different. */
    DDPIXELFORMAT display;
    DWORD display_bpp;
    display_format(&display, &display_bpp);
    DDPIXELFORMAT pf = display;
    BOOL explicit_format = FALSE;
    if ((d->dwFlags & DDSD_PIXELFORMAT) && !(caps & DDSCAPS_PRIMARYSURFACE)) {
        pf = d->ddpfPixelFormat;
        pf.dwSize = sizeof(pf);
        if ((pf.dwFlags & 0xE2463) && !(pf.dwFlags & DDPF_ZBUFFER) && !pf.dwRGBBitCount)
            pf.dwRGBBitCount = display_bpp;
        explicit_format = own_format_forced || !formats_match(&pf, &display) || !(pf.dwFlags & 0xE2463) ||
                          (caps & DDSCAPS_ZBUFFER);
    } else if (own_format_forced) {
        explicit_format = TRUE;
    }
    if ((caps & DDSCAPS_ZBUFFER) && !(caps & DDSCAPS_COMPLEX) && !(d->dwFlags & DDSD_PIXELFORMAT)) {
        memset(&pf, 0, sizeof(pf));
        pf.dwSize = sizeof(pf);
        pf.dwFlags = DDPF_ZBUFFER;
        pf.dwZBufferBitDepth = d->dwZBufferBitDepth;
        pf.dwZBitMask = d->dwZBufferBitDepth >= 32 ? 0xFFFFFFFF : (1u << d->dwZBufferBitDepth) - 1;
        explicit_format = TRUE;
    }
    /* what can be made: the emulation's formats; the driver's are those and the display's, palettes aside. */
    BOOL different = explicit_format && !formats_match(&pf, &display);
    if (!(caps & DDSCAPS_EXECUTEBUFFER)) {
        BOOL ok = TRUE;
        if (caps & (DDSCAPS_TEXTURE)) {
            ok = format_in(&pf, TEXTURE_FORMATS, RTL_NUMBER_OF(TEXTURE_FORMATS));
            if (hal && (pf.dwFlags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 |
                                      DDPF_PALETTEINDEXED8)))
                ok = FALSE;
        } else if ((caps & DDSCAPS_ZBUFFER) && !(caps & DDSCAPS_COMPLEX)) {
            DWORD depth = pf.dwZBufferBitDepth;
            ok = hal ? format_in(&pf, TEXTURE_FORMATS, RTL_NUMBER_OF(TEXTURE_FORMATS)) : (depth == 16 || depth == 32);
        } else if (different && (caps & DDSCAPS_OFFSCREENPLAIN)) {
            ok = format_in(&pf, OFFSCREEN_FORMATS, RTL_NUMBER_OF(OFFSCREEN_FORMATS));
        } else if (different && !(caps & (DDSCAPS_ZBUFFER | DDSCAPS_ALPHA))) {
            ok = format_in(&pf, OFFSCREEN_FORMATS, RTL_NUMBER_OF(OFFSCREEN_FORMATS)) ||
                 format_in(&pf, TEXTURE_FORMATS, RTL_NUMBER_OF(TEXTURE_FORMATS));
        }
        if (!ok)
            return hal ? DDERR_UNSUPPORTED : DDERR_INVALIDPIXELFORMAT;
    }
    DDFORMAT format;
    if (!(caps & DDSCAPS_EXECUTEBUFFER) && !DdFormatFromPixelFormat(&pf, &format))
        return DDERR_INVALIDPIXELFORMAT;
    if (caps & DDSCAPS_EXECUTEBUFFER) {
        memset(&format, 0, sizeof(format));
        format.bpp = 8;
    }
    /* how many: the back buffers or levels, a z buffer of a complex surface, the faces of a cube map. */
    DWORD count = n + 1;
    BOOL has_z = (caps & (DDSCAPS_ZBUFFER | DDSCAPS_COMPLEX)) == (DDSCAPS_ZBUFFER | DDSCAPS_COMPLEX);
    if (has_z)
        count++;
    DWORD faces = 0, face_bits[6];
    if (caps2 & DDSCAPS2_CUBEMAP) {
        static const DWORD ORDER[6] = {DDSCAPS2_CUBEMAP_POSITIVEX, DDSCAPS2_CUBEMAP_NEGATIVEX,
                                       DDSCAPS2_CUBEMAP_POSITIVEY, DDSCAPS2_CUBEMAP_NEGATIVEY,
                                       DDSCAPS2_CUBEMAP_POSITIVEZ, DDSCAPS2_CUBEMAP_NEGATIVEZ};
        for (int i = 0; i < 6; i++)
            if (caps2 & ORDER[i])
                face_bits[faces++] = ORDER[i];
        if (!faces)
            return DDERR_INVALIDPARAMS;
        count = faces * (n + 1);
        has_z = FALSE;
    }
    BOOL flip_chain = count > 1 && (caps & DDSCAPS_FLIP);
    BOOL mip_chain = (caps & (DDSCAPS_MIPMAP | DDSCAPS_COMPLEX)) == (DDSCAPS_MIPMAP | DDSCAPS_COMPLEX) &&
                     (d->dwFlags & DDSD_MIPMAPCOUNT);
    DWORD z_index = has_z ? count - 1 : ~0u;
    DDMODE mode;
    DdCurrentMode(&mode);
    DWORD width = (d->dwFlags & DDSD_WIDTH) ? d->dwWidth : mode.width;
    DWORD height = (d->dwFlags & DDSD_HEIGHT) ? d->dwHeight : mode.height;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        width = mode.width;
        height = mode.height;
    }
    if (width >= 0x10000 || height >= 0x10000)
        return DDERR_INVALIDPARAMS;
    if (count > 4096)
        return DDERR_OUTOFMEMORY;
    DDSURF **list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*list));
    if (!list)
        return DDERR_OUTOFMEMORY;
    DWORD levels = n + 1;
    DWORD c = caps;
    DWORD c2 = caps2 & ~DDSCAPS2_MIPMAPSUBLEVEL;
    DWORD memory_caps = hal ? DDSCAPS_VIDEOMEMORY | (caps & DDSCAPS_NONLOCALVIDMEM ? 0 : DDSCAPS_LOCALVIDMEM)
                            : DDSCAPS_SYSTEMMEMORY;
    if (has_z)
        c &= ~DDSCAPS_ZBUFFER;
    HRESULT hr = DD_OK;
    for (DWORD i = 0; i < count; i++) {
        DDSURF *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s) {
            hr = DDERR_OUTOFMEMORY;
            break;
        }
        list[i] = s;
        DdSurfInitIfaces(s);
        s->version = version < DDV4 ? DDV1 : version;
        s->dd = dd;
        s->creator_iface = DdIface(dd, version);
        s->uniqueness = (c & (DDSCAPS_VIDEOPORT | DDSCAPS_OWNDC)) ? 0 : 1;
        s->mode = mode;
        s->sysmem_requested = !hal && (caps & DDSCAPS_SYSTEMMEMORY);
        DWORD sc = c, sc2 = c2;
        DWORD w = width, h = height;
        if (i == 0) {
            if (sc & DDSCAPS_PRIMARYSURFACE) {
                sc |= DDSCAPS_VISIBLE;
                if ((dd->flags & DDF_MODEX) && mode.width == 320)
                    sc |= DDSCAPS_MODEX;
            }
            if (sc & DDSCAPS_FLIP) {
                sc |= DDSCAPS_FRONTBUFFER;
                s->created_front = TRUE;
            }
        } else {
            s->implicit = TRUE;
            sc &= 0xFFFD7DD9;
            sc2 &= ~DDSCAPS2_STEREOSURFACELEFT;
            if (i == z_index) {
                sc = (sc & 0xFFFDCF2F) | DDSCAPS_ZBUFFER;
            } else if (flip_chain && !mip_chain && !faces) {
                sc |= DDSCAPS_FLIP;
                if (i == 1) {
                    sc |= DDSCAPS_BACKBUFFER;
                    s->created_back = TRUE;
                }
            }
        }
        if (faces) {
            DWORD face = i / levels, level = i % levels;
            sc2 = (sc2 & ~DDSCAPS2_CUBEMAP_ALLFACES) | DDSCAPS2_CUBEMAP | face_bits[face];
            if (level)
                sc2 |= DDSCAPS2_MIPMAPSUBLEVEL;
            w = width >> level ? width >> level : 1;
            h = height >> level ? height >> level : 1;
        } else if (mip_chain && i != z_index) {
            if (i)
                sc2 |= DDSCAPS2_MIPMAPSUBLEVEL;
            w = width >> i ? width >> i : 1;
            h = height >> i ? height >> i : 1;
        }
        if (!(sc & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)) || hal)
            sc = (sc & ~(DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM)) | memory_caps |
                 (caps & DDSCAPS_NONLOCALVIDMEM);
        s->desc.dwSize = sizeof(DDSURFACEDESC2);
        s->desc.dwWidth = w;
        s->desc.dwHeight = h;
        s->desc.ddsCaps.dwCaps = sc;
        s->desc.ddsCaps.dwCaps2 = sc2;
        s->desc.ddsCaps.dwCaps3 = d->ddsCaps.dwCaps3;
        s->desc.ddsCaps.dwCaps4 = d->ddsCaps.dwCaps4;
        if (d->dwFlags & DDSD_TEXTURESTAGE) {
            s->desc.dwFlags |= DDSD_TEXTURESTAGE;
            s->desc.dwTextureStage = d->dwTextureStage;
        }
        if (i == 0 && flip_chain)
            s->desc.dwBackBufferCount = n;
        if (i == z_index) {
            DDPIXELFORMAT z = {sizeof(z), DDPF_ZBUFFER};
            z.dwZBufferBitDepth = d->dwZBufferBitDepth;
            z.dwZBitMask = d->dwZBufferBitDepth >= 32 ? 0 : (1u << d->dwZBufferBitDepth) - 1;
            DdFormatFromPixelFormat(&z, &s->format);
            s->explicit_format = TRUE;
        } else {
            s->format = format;
            s->explicit_format = explicit_format;
        }
        if (sc & DDSCAPS_EXECUTEBUFFER) {
            s->desc.dwWidth = 0;
            s->desc.dwHeight = 0;
            s->desc.lPitch = (LONG)d->dwWidth;
        }
        if ((d->dwFlags & DDSD_LPSURFACE) && i == 0) {
            s->memory = d->lpSurface;
            s->desc.lPitch = s->format.block ? (LONG)d->dwLinearSize : d->lPitch;
            s->bytes = s->format.block ? d->dwLinearSize : (SIZE_T)d->lPitch * h;
        } else if (!allocate(s)) {
            hr = DDERR_OUTOFMEMORY;
            break;
        }
        if ((sc & DDSCAPS_PRIMARYSURFACE) || (i && (list[0]->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)))
            s->gdi = i == 0;
    }
    /* video memory: the budget a driver would charge. */
    if (SUCCEEDED(hr) && hal) {
        DWORD total = 0, free_bytes = 0;
        DdGetVidMem(dd, DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM, &total, &free_bytes);
        SIZE_T need = 0;
        for (DWORD i = 0; i < count; i++)
            need += list[i]->bytes;
        if (need > free_bytes + (DdVideoMemoryUsed() ? 0 : (SIZE_T)mode.width * mode.height * (mode.bpp + 7) / 8))
            hr = DDERR_OUTOFVIDEOMEMORY;
    }
    /* the links: a ring for a flip chain, a line for levels, faces from the root, the z buffer to the first back buffer. */
    for (DWORD i = 1; SUCCEEDED(hr) && i < count; i++) {
        BOOL ok = TRUE;
        if (i == z_index)
            ok = link_implicit(list[flip_chain && count > 2 ? 1 : 0], list[i]);
        else if (faces)
            ok = link_implicit(i % levels ? list[i - 1] : list[0], list[i]);
        else
            ok = link_implicit(list[i - 1], list[i]);
        if (!ok)
            hr = DDERR_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && flip_chain && !mip_chain && !faces) {
        DWORD last = has_z ? count - 2 : count - 1;
        if (!link_implicit(list[last], list[0]))
            hr = DDERR_OUTOFMEMORY;
    }
    if (FAILED(hr)) {
        free_partial(list, count);
        HeapFree(GetProcessHeap(), 0, list);
        return hr;
    }
    for (DWORD i = 0; i < count; i++) {
        DDSURF *s = list[i];
        s->ref = 1;
        *DdSurfRefOf(s, s->version) = 1;
        InsertTailList(&dd->surfaces, &s->links);
    }
    if (caps & DDSCAPS_PRIMARYSURFACE)
        dd->primary = list[0];
    *out = list[0];
    HeapFree(GetProcessHeap(), 0, list);
    return DD_OK;
}

/* the key slots of a desc, set as SetColorKey would set them. */
static void apply_keys(DDSURF *s, const DDSURFACEDESC2 *d)
{
    if (d->dwFlags & DDSD_CKSRCBLT) {
        s->desc.ddckCKSrcBlt = d->ddckCKSrcBlt;
        s->ckeys |= DDSD_CKSRCBLT;
    }
    if (d->dwFlags & DDSD_CKDESTBLT) {
        s->desc.ddckCKDestBlt = d->ddckCKDestBlt;
        s->ckeys |= DDSD_CKDESTBLT;
    }
}

/*
 * windows' InternalCreateSurface: client memory rules, then the memory a
 * surface goes in: video memory unless asked otherwise, system memory when
 * that fails for anything but bad parameters, managed textures in system
 * memory as on the windows display driver model.
 */
HRESULT DdCreateSurface(DDRAW *dd, DWORD version, const DDSURFACEDESC2 *in, DDSURF **out)
{
    *out = nullptr;
    DDSURFACEDESC2 d = *in;
    BOOL v1_desc = version < DDV4;
    DWORD caps = d.ddsCaps.dwCaps;
    if ((caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)) == (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY))
        return DDERR_INVALIDCAPS;
    if (version < DDV4 || !(d.dwFlags & DDSD_LPSURFACE))
        d.dwFlags &= ~(DDSD_PITCH | DDSD_LINEARSIZE);
    if (d.dwFlags & DDSD_LPSURFACE) {
        if (!(caps & DDSCAPS_SYSTEMMEMORY))
            return DDERR_INVALIDCAPS;
        if (!d.lpSurface)
            return DDERR_INVALIDPARAMS;
        if (caps & (DDSCAPS_OPTIMIZED | DDSCAPS_COMPLEX | DDSCAPS_EXECUTEBUFFER | DDSCAPS_OVERLAY |
                    DDSCAPS_PRIMARYSURFACE | DDSCAPS_RESERVED3 | DDSCAPS_VIDEOPORT))
            return DDERR_INVALIDCAPS;
        if ((d.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT))
            return DDERR_INVALIDPARAMS;
        if (((ULONG_PTR)d.lpSurface & 3) && version != DDV4)
            return DDERR_INVALIDPARAMS;
        DDPIXELFORMAT pf;
        if (d.dwFlags & DDSD_PIXELFORMAT)
            pf = d.ddpfPixelFormat;
        else
            display_format(&pf, nullptr);
        if (!(pf.dwFlags & DDPF_FOURCC)) {
            if (!(d.dwFlags & DDSD_PITCH) || d.lPitch < (LONG)((pf.dwRGBBitCount * d.dwWidth + 7) / 8) || (d.lPitch & 3))
                return DDERR_INVALIDPARAMS;
        } else {
            DWORD block = block_size(pf.dwFourCC);
            if (!block)
                return DDERR_UNSUPPORTED;
            DWORD need = block * ((d.dwWidth + 3) / 4) * ((d.dwHeight + 3) / 4);
            if (d.dwFlags & DDSD_PITCH) {
                if (version != DDV4)
                    return DDERR_INVALIDPARAMS;
                d.dwFlags = (d.dwFlags & ~DDSD_PITCH) | DDSD_LINEARSIZE;
                d.dwLinearSize = need;
            }
            if (!(d.dwFlags & DDSD_LINEARSIZE) || d.dwLinearSize < need)
                return DDERR_INVALIDPARAMS;
        }
    }
    if ((d.ddsCaps.dwCaps2 & 1) ||
        ((caps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) && !(caps & DDSCAPS_VIDEOMEMORY)))
        return DDERR_INVALIDCAPS;
    if ((caps & DDSCAPS_OWNDC) && !(caps & DDSCAPS_SYSTEMMEMORY))
        return DDERR_UNSUPPORTED;
    d.ddsCaps.dwCaps2 &= ~DDSCAPS2_MIPMAPSUBLEVEL;
    if (caps & DDSCAPS_MIPMAP) {
        HRESULT hr = mip_dims(&d, version);
        if (FAILED(hr))
            return hr;
    }
    if (d.ddsCaps.dwCaps2 & DDSCAPS2_VOLUME)
        return DDERR_INVALIDCAPS;
    HRESULT hr = check_desc(&d, version, v1_desc);
    if (FAILED(hr))
        return hr;
    DWORD managed = d.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE);
    DDSURF *s = nullptr;
    if (managed) {
        if (caps & (DDSCAPS_VIDEOMEMORY | DDSCAPS_SYSTEMMEMORY | DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM))
            return DDERR_INVALIDCAPS;
        DDSURFACEDESC2 sys = d;
        sys.ddsCaps.dwCaps2 &= ~DDSCAPS2_DONOTPERSIST;
        sys.ddsCaps.dwCaps = (sys.ddsCaps.dwCaps & ~DDSCAPS_WRITEONLY) | DDSCAPS_SYSTEMMEMORY;
        hr = build(dd, version, &sys, FALSE, TRUE, &s);
    } else if (caps & DDSCAPS_VIDEOMEMORY) {
        hr = build(dd, version, &d, TRUE, FALSE, &s);
    } else if (caps & DDSCAPS_SYSTEMMEMORY) {
        hr = build(dd, version, &d, FALSE, TRUE, &s);
    } else {
        hr = build(dd, version, &d, TRUE, FALSE, &s);
        if (FAILED(hr) && hr != DDERR_INVALIDCAPS && hr != DDERR_INVALIDPARAMS && hr != DDERR_NOEXCLUSIVEMODE &&
            hr != DDERR_PRIMARYSURFACEALREADYEXISTS) {
            DDSURFACEDESC2 sys = d;
            sys.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
            hr = build(dd, version, &sys, FALSE, FALSE, &s);
        }
    }
    if (FAILED(hr))
        return hr;
    apply_keys(s, &d);
    *out = s;
    return DD_OK;
}

/* ------------------------------------------------------------------ duplicates and probes */

/* the same memory, its own state: no attachments, palette or clipper, one key carried over. */
HRESULT DdSurfDuplicate(DDRAW *dd, DDSURF *src, DDSURF **out)
{
    DDSURF *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return DDERR_OUTOFMEMORY;
    DdSurfInitIfaces(s);
    s->version = src->version;
    s->dd = dd;
    s->creator_iface = src->creator_iface;
    s->desc = src->desc;
    s->desc.dwBackBufferCount = 0;
    s->desc.dwMipMapCount = 0;
    s->desc.ddsCaps.dwCaps &= ~(DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER);
    s->format = src->format;
    s->explicit_format = src->explicit_format;
    s->memory = src->memory;
    s->mem = src->mem;
    if (s->mem)
        s->mem->ref++;
    s->bytes = src->bytes;
    s->mode = src->mode;
    s->uniqueness = 1;
    s->sysmem_requested = src->sysmem_requested;
    if (src->ckeys & DDSD_CKSRCBLT) {
        s->ckeys = DDSD_CKSRCBLT;
        s->desc.ddckCKSrcBlt = src->desc.ddckCKSrcBlt;
    }
    s->ref = 1;
    *DdSurfRefOf(s, s->version) = 1;
    InsertTailList(&dd->surfaces, &s->links);
    *out = s;
    return DD_OK;
}

/* windows' CANBECREATED: each size of the modes and each rgb depth, tried by making the surface. */
HRESULT DdEnumCreatable(DDRAW *dd, DWORD version, DDSURFACEDESC2 *desc, void *context, void *callback)
{
    static const DWORD DEPTHS[][4] = {{8, 0xE0, 0x1C, 0x03}, {16, 0xF800, 0x7E0, 0x1F},
                                      {24, 0xFF0000, 0xFF00, 0xFF}, {32, 0xFF0000, 0xFF00, 0xFF}};
    DDSURFACEDESC2 probe = *desc;
    BOOL sizes = !(desc->dwFlags & DDSD_HEIGHT) && !(desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE);
    BOOL rgb = (desc->dwFlags & DDSD_PIXELFORMAT) && (desc->ddpfPixelFormat.dwFlags & DDPF_RGB);
    if (!(desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE))
        probe.dwFlags |= DDSD_HEIGHT | DDSD_WIDTH;
    DEVMODEW dm = {.dmSize = sizeof(dm)};
    DWORD seen[64][2], n = 0;
    for (DWORD m = 0; sizes ? EnumDisplaySettingsW(nullptr, m, &dm) : m == 0; m++) {
        if (sizes) {
            BOOL dup = FALSE;
            for (DWORD j = 0; j < n && !dup; j++)
                dup = seen[j][0] == dm.dmPelsWidth && seen[j][1] == dm.dmPelsHeight;
            if (dup || n >= RTL_NUMBER_OF(seen))
                continue;
            seen[n][0] = dm.dmPelsWidth;
            seen[n++][1] = dm.dmPelsHeight;
            probe.dwWidth = dm.dmPelsWidth;
            probe.dwHeight = dm.dmPelsHeight;
        }
        for (DWORD f = 0; f < (rgb ? 4u : 1u); f++) {
            if (rgb) {
                DDPIXELFORMAT *pf = &probe.ddpfPixelFormat;
                pf->dwFlags = DEPTHS[f][0] == 8 ? DDPF_RGB | DDPF_PALETTEINDEXED8 : DDPF_RGB;
                pf->dwRGBBitCount = DEPTHS[f][0];
                pf->dwRBitMask = DEPTHS[f][1];
                pf->dwGBitMask = DEPTHS[f][2];
                pf->dwBBitMask = DEPTHS[f][3];
                pf->dwRGBAlphaBitMask = 0;
            }
            DDSURF *s;
            if (FAILED(DdCreateSurface(dd, version, &probe, &s)))
                continue;
            DDSURFACEDESC2 out;
            DdSurfFillDesc(s, version, &out);
            RtlLeaveCriticalSection(&DdLock);
            if (version < DDV4)
                ((LPDDENUMSURFACESCALLBACK)callback)(nullptr, (DDSURFACEDESC *)&out, context);
            else
                ((LPDDENUMSURFACESCALLBACK7)callback)(nullptr, &out, context);
            RtlEnterCriticalSection(&DdLock);
            DdSurfRelease(s, DdSurfRefOf(s, s->version));
        }
    }
    return DD_OK;
}

/* the d3d interfaces of a surface are d3dim's, which 64-bit windows has none of. */
HRESULT DdSurfQueryD3D(DDSURF *s, REFIID riid, void **out)
{
#ifdef _WIN64
    return E_NOINTERFACE;
#else
    return D3dSurfQuery(s, riid, out);
#endif
}

/* ------------------------------------------------------------------ the adapter */

void DdAdapterIds(DWORD *vendor, DWORD *device, DWORD *subsys, DWORD *revision)
{
    static BOOL done;
    static DXGI_ADAPTER_DESC1 desc;
    if (!done) {
        done = TRUE;
        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        typedef HRESULT(WINAPI * CREATE)(REFIID, void **);
        CREATE create = dxgi ? (CREATE)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
        IDXGIFactory1 *factory;
        if (create && SUCCEEDED(create(&IID_IDXGIFactory1, (void **)&factory))) {
            IDXGIAdapter1 *adapter;
            if (SUCCEEDED(factory->lpVtbl->EnumAdapters1(factory, 0, &adapter))) {
                adapter->lpVtbl->GetDesc1(adapter, &desc);
                adapter->lpVtbl->Release(adapter);
            }
            factory->lpVtbl->Release(factory);
        }
    }
    *vendor = desc.VendorId;
    *device = desc.DeviceId;
    *subsys = desc.SubSysId;
    *revision = desc.Revision;
}
