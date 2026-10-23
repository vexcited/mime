/*
 * the pixel formats of every dc, shared by gdi32 and opengl32. the first ones are the gpu formats opengl32 renders
 * through, double buffered with a back buffer that persists across swaps. the generic ones after them match
 * windows' gdi opengl: they can be selected but render nothing.
 */
#ifndef MIME_PIXELFORMAT_H
#define MIME_PIXELFORMAT_H

#include <mime/win32/wingdi.h>

typedef struct MIME_PIXEL_FORMAT {
    DWORD Flags;
    BYTE ColorBits;
    BYTE AlphaBits;
    BYTE AccumBits;
    BYTE DepthBits;
    BYTE StencilBits;
} MIME_PIXEL_FORMAT;

#define MIME_PF_GPU_WINDOW (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SWAP_COPY | 0x8000)
#define MIME_PF_GENERIC_WINDOW (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT)
#define MIME_PF_GENERIC_BITMAP (PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT)

static const MIME_PIXEL_FORMAT MimePixelFormats[] = {
    {MIME_PF_GPU_WINDOW, 32, 8, 64, 24, 8},
    {MIME_PF_GPU_WINDOW, 32, 0, 64, 24, 8},
    {MIME_PF_GPU_WINDOW, 32, 8, 64, 24, 0},
    {MIME_PF_GPU_WINDOW, 32, 8, 64, 0, 0},
    {MIME_PF_GPU_WINDOW, 32, 0, 64, 0, 0},
    {MIME_PF_GPU_WINDOW, 32, 8, 64, 16, 0},
    {MIME_PF_GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 0, 64, 32, 8},
    {MIME_PF_GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 0, 64, 16, 8},
    {MIME_PF_GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 8, 64, 32, 8},
    {MIME_PF_GENERIC_BITMAP, 24, 0, 64, 32, 8},
    {MIME_PF_GENERIC_BITMAP, 24, 0, 64, 16, 8},
    {MIME_PF_GENERIC_BITMAP, 16, 0, 32, 32, 8},
    {MIME_PF_GENERIC_BITMAP, 16, 0, 32, 16, 8},
    {MIME_PF_GENERIC_BITMAP, 8, 0, 16, 32, 8},
    {MIME_PF_GENERIC_BITMAP, 8, 0, 16, 16, 8},
};

#define MIME_PIXEL_FORMAT_COUNT ((int)(sizeof(MimePixelFormats) / sizeof(MimePixelFormats[0])))

/* index is 1-based. */
static inline void MimeDescribePixelFormat(int index, PIXELFORMATDESCRIPTOR *pfd)
{
    const MIME_PIXEL_FORMAT *f = &MimePixelFormats[index - 1];
    memset(pfd, 0, sizeof(*pfd));
    pfd->nSize = sizeof(*pfd);
    pfd->nVersion = 1;
    pfd->dwFlags = f->Flags;
    pfd->iPixelType = f->ColorBits == 8 ? PFD_TYPE_COLORINDEX : PFD_TYPE_RGBA;
    pfd->cColorBits = f->ColorBits;
    switch (f->ColorBits) {
    case 32:
    case 24:
        pfd->cRedBits = pfd->cGreenBits = pfd->cBlueBits = 8;
        pfd->cRedShift = 16;
        pfd->cGreenShift = 8;
        pfd->cBlueShift = 0;
        break;
    case 16:
        pfd->cRedBits = pfd->cBlueBits = 5;
        pfd->cGreenBits = 6;
        pfd->cRedShift = 11;
        pfd->cGreenShift = 5;
        break;
    default:
        pfd->dwFlags |= PFD_NEED_PALETTE;
        break;
    }
    pfd->cAlphaBits = f->AlphaBits;
    pfd->cAlphaShift = f->AlphaBits ? 24 : 0;
    pfd->cAccumBits = f->AccumBits;
    pfd->cAccumRedBits = pfd->cAccumGreenBits = pfd->cAccumBlueBits = (BYTE)(f->AccumBits / 4);
    pfd->cAccumAlphaBits = f->AlphaBits ? (BYTE)(f->AccumBits / 4) : 0;
    pfd->cDepthBits = f->DepthBits;
    pfd->cStencilBits = f->StencilBits;
    pfd->iLayerType = PFD_MAIN_PLANE;
}

/* returns 0 when nothing matches. */
static inline int MimeChoosePixelFormat(const PIXELFORMATDESCRIPTOR *want)
{
    DWORD required = want->dwFlags & (PFD_DRAW_TO_WINDOW | PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI | PFD_SUPPORT_OPENGL);
    int best = 0;
    LONG best_score = 0x7FFFFFFF;
    for (int i = 1; i <= MIME_PIXEL_FORMAT_COUNT; i++) {
        PIXELFORMATDESCRIPTOR pfd;
        MimeDescribePixelFormat(i, &pfd);
        if ((pfd.dwFlags & required) != required)
            continue;
        LONG score = 0;
        if (!(want->dwFlags & PFD_DOUBLEBUFFER_DONTCARE) &&
            !!(want->dwFlags & PFD_DOUBLEBUFFER) != !!(pfd.dwFlags & PFD_DOUBLEBUFFER))
            score += 10000;
        if ((want->iPixelType == PFD_TYPE_COLORINDEX) != (pfd.iPixelType == PFD_TYPE_COLORINDEX))
            score += 5000;
        if (want->cColorBits)
            score += (LONG)(want->cColorBits > pfd.cColorBits ? want->cColorBits - pfd.cColorBits
                                                             : pfd.cColorBits - want->cColorBits) * 20;
        if (want->cAlphaBits && !pfd.cAlphaBits)
            score += 1000;
        if (!want->cAlphaBits && pfd.cAlphaBits)
            score += 1;
        if (!(want->dwFlags & PFD_DEPTH_DONTCARE) && want->cDepthBits > pfd.cDepthBits)
            score += 500;
        if (want->cStencilBits > pfd.cStencilBits)
            score += 500;
        /* prefer formats without depth or stencil buffers nobody asked for. */
        if (!want->cDepthBits && pfd.cDepthBits)
            score += 2;
        if (!want->cStencilBits && pfd.cStencilBits)
            score += 2;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

#endif
