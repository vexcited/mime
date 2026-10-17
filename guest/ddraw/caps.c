/*
 * IDirectDraw::GetCaps: the caps of a display driver of the windows
 * display driver model, as windows' ddraw makes them from the d3d9 user
 * mode driver, and those of windows' emulation. blt.c checks blts
 * against the same values.
 */
#include "ddraw_private.h"

#define ROPS_SRCCOPY {[6] = 1u << 12}
#define ROPS_HEL {[0] = 1, [6] = 1u << 12, [7] = 1u << 31}

/* what a modern driver reports, overlays hidden as windows hides them. */
static void driver_caps(DDCAPS *c, DWORD fourccs)
{
    memset(c, 0, sizeof(*c));
    c->dwSize = sizeof(*c);
    c->dwCaps = 0x95D207C1;
    c->dwCaps2 = 0xA06AB300;
    c->dwCKeyCaps = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTBLT;
    c->dwFXCaps = 0x154E0;
    c->dwNumFourCCCodes = fourccs;
    static const DWORD rops[DD_ROP_SPACE] = ROPS_SRCCOPY;
    memcpy(c->dwRops, rops, sizeof(rops));
    c->ddsOldCaps.dwCaps = 0x3046727C;
    c->dwSVBCaps = c->dwVSBCaps = DDCAPS_BLT;
    memcpy(c->dwSVBRops, rops, sizeof(rops));
    memcpy(c->dwVSBRops, rops, sizeof(rops));
    c->dwNLVBCaps = c->dwCaps;
    c->dwNLVBCaps2 = c->dwCaps2;
    c->dwNLVBCKeyCaps = c->dwCKeyCaps;
    c->dwNLVBFXCaps = c->dwFXCaps;
    memcpy(c->dwNLVBRops, rops, sizeof(rops));
    c->ddsCaps.dwCaps = 0x3046727C;
    c->ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP;
}

/* windows' emulation on a display gdi draws on. */
static void hel_caps(DDCAPS *c)
{
    memset(c, 0, sizeof(*c));
    c->dwSize = sizeof(*c);
    c->dwCaps = 0xF4C08241;
    c->dwCaps2 = DDCAPS2_CERTIFIED;
    c->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
    c->dwFXCaps = 0x3FCE3;
    c->dwPalCaps = 0x347;
    c->dwZBufferBitDepths = 0x400; /* DDBD_16 */
    static const DWORD rops[DD_ROP_SPACE] = ROPS_HEL;
    memcpy(c->dwRops, rops, sizeof(rops));
    c->ddsOldCaps.dwCaps = 0x00421350;
    c->dwSVBCaps = c->dwVSBCaps = c->dwSSBCaps = c->dwCaps;
    c->dwSVBCKeyCaps = c->dwVSBCKeyCaps = c->dwSSBCKeyCaps = c->dwCKeyCaps;
    c->dwSVBFXCaps = c->dwVSBFXCaps = c->dwSSBFXCaps = c->dwFXCaps;
    memcpy(c->dwSVBRops, rops, sizeof(rops));
    memcpy(c->dwVSBRops, rops, sizeof(rops));
    memcpy(c->dwSSBRops, rops, sizeof(rops));
    c->dwNLVBCaps = c->dwCaps;
    c->dwNLVBCaps2 = c->dwCaps2;
    c->dwNLVBCKeyCaps = c->dwCKeyCaps;
    c->dwNLVBFXCaps = c->dwFXCaps;
    memcpy(c->dwNLVBRops, rops, sizeof(rops));
    c->ddsCaps.dwCaps = 0x00421350;
    c->ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP;
}

static BOOL caps_size(DWORD size)
{
    return size == DDCAPS_SIZE_DX1 || size == DDCAPS_SIZE_DX3 || size == DDCAPS_SIZE_DX5 || size == DDCAPS_SIZE_DX7;
}

/* a caller's caps get as much as their size holds. */
static void copy_out(DDCAPS *out, const DDCAPS *full)
{
    DWORD size = out->dwSize;
    memset(out, 0, size);
    memcpy(out, full, size);
    out->dwSize = size;
}

HRESULT DdGetCaps(DDRAW *dd, DDCAPS *driver, DDCAPS *emulation)
{
    if (!driver && !emulation)
        return DDERR_INVALIDPARAMS;
    if ((driver && !caps_size(driver->dwSize)) || (emulation && !caps_size(emulation->dwSize)))
        return DDERR_INVALIDPARAMS;
    DDCAPS full;
    if (driver) {
        driver_caps(&full, DdFourCCCount());
        DdGetVidMem(dd, DDSCAPS_VIDEOMEMORY, &full.dwVidMemTotal, &full.dwVidMemFree);
        copy_out(driver, &full);
    }
    if (emulation) {
        hel_caps(&full);
        copy_out(emulation, &full);
    }
    return DD_OK;
}
