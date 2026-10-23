/* pixel formats: a surface's layout, its pitch, and the dc gdi can make over it. */
#include "ddraw_private.h"

static BOOL is_dxt(DWORD fourcc, DWORD *block)
{
    switch (fourcc) {
    case FOURCC_DXT1:
        *block = 8;
        return TRUE;
    case FOURCC_DXT2:
    case FOURCC_DXT3:
    case FOURCC_DXT4:
    case FOURCC_DXT5:
        *block = 16;
        return TRUE;
    default:
        return FALSE;
    }
}

/* yuv four character codes ddraw blts and locks as 16 bits per pixel. */
static BOOL is_packed_yuv(DWORD fourcc)
{
    return fourcc == MAKEFOURCC('Y', 'U', 'Y', '2') || fourcc == MAKEFOURCC('U', 'Y', 'V', 'Y') ||
           fourcc == MAKEFOURCC('Y', 'V', 'Y', 'U');
}

BOOL DdFormatFromPixelFormat(const DDPIXELFORMAT *pf, DDFORMAT *out)
{
    memset(out, 0, sizeof(*out));
    out->pf = *pf;
    out->pf.dwSize = sizeof(DDPIXELFORMAT);
    out->gdi = D3DDDIFMT_UNKNOWN;
    if (pf->dwFlags & DDPF_FOURCC) {
        if (is_dxt(pf->dwFourCC, &out->block))
            return TRUE;
        if (is_packed_yuv(pf->dwFourCC)) {
            out->bpp = 16;
            return TRUE;
        }
        return FALSE;
    }
    DWORD bpp = pf->dwRGBBitCount;
    if (pf->dwFlags & DDPF_PALETTEINDEXED1)
        bpp = 1;
    else if (pf->dwFlags & DDPF_PALETTEINDEXED2)
        bpp = 2;
    else if (pf->dwFlags & DDPF_PALETTEINDEXED4)
        bpp = 4;
    else if (pf->dwFlags & DDPF_PALETTEINDEXED8)
        bpp = 8;
    switch (bpp) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 24:
    case 32:
        break;
    default:
        return FALSE;
    }
    out->bpp = bpp;
    out->pf.dwRGBBitCount = bpp;
    if (pf->dwFlags & DDPF_PALETTEINDEXED8) {
        out->gdi = D3DDDIFMT_P8;
    } else if ((pf->dwFlags & DDPF_RGB) && !(pf->dwFlags & (DDPF_ZBUFFER | DDPF_LUMINANCE | DDPF_BUMPDUDV))) {
        DWORD r = pf->dwRBitMask, g = pf->dwGBitMask, b = pf->dwBBitMask;
        BOOL alpha = (pf->dwFlags & DDPF_ALPHAPIXELS) && pf->dwRGBAlphaBitMask;
        if (bpp == 16 && r == 0xF800 && g == 0x07E0 && b == 0x001F)
            out->gdi = D3DDDIFMT_R5G6B5;
        else if (bpp == 16 && r == 0x7C00 && g == 0x03E0 && b == 0x001F)
            out->gdi = alpha ? D3DDDIFMT_A1R5G5B5 : D3DDDIFMT_X1R5G5B5;
        else if (bpp == 24 && r == 0xFF0000 && g == 0xFF00 && b == 0xFF)
            out->gdi = D3DDDIFMT_R8G8B8;
        else if (bpp == 32 && r == 0xFF0000 && g == 0xFF00 && b == 0xFF)
            out->gdi = alpha ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_X8R8G8B8;
    }
    return TRUE;
}

/* byte offset of pixel x in a row. */
DWORD DdSurfBytes(const DDSURF *s, LONG x)
{
    if (s->format.block)
        return (DWORD)(x / 4) * s->format.block;
    return (DWORD)((ULONG64)x * s->format.bpp / 8);
}
