/*
 * gdi32 paths, world transforms, device dependent bitmap bits, printing
 * and pixel formats.
 */
#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>

/* ------------------------------------------------------------------ paths */

WINGDIAPI BOOL WINAPI BeginPath(HDC dc)
{
    return NtGdiBeginPath(dc);
}

WINGDIAPI BOOL WINAPI EndPath(HDC dc)
{
    return NtGdiEndPath(dc);
}

WINGDIAPI BOOL WINAPI AbortPath(HDC dc)
{
    return NtGdiAbortPath(dc);
}

WINGDIAPI BOOL WINAPI CloseFigure(HDC dc)
{
    return NtGdiCloseFigure(dc);
}

WINGDIAPI BOOL WINAPI FillPath(HDC dc)
{
    return NtGdiFillPath(dc);
}

WINGDIAPI BOOL WINAPI StrokePath(HDC dc)
{
    return NtGdiStrokePath(dc);
}

WINGDIAPI BOOL WINAPI StrokeAndFillPath(HDC dc)
{
    return NtGdiStrokeAndFillPath(dc);
}

WINGDIAPI BOOL WINAPI FlattenPath(HDC dc)
{
    return NtGdiFlattenPath(dc);
}

WINGDIAPI BOOL WINAPI SelectClipPath(HDC dc, int mode)
{
    return NtGdiSelectClipPath(dc, mode);
}

WINGDIAPI HRGN WINAPI PathToRegion(HDC dc)
{
    return NtGdiPathToRegion(dc);
}

WINGDIAPI int WINAPI GetPath(HDC dc, LPPOINT points, LPBYTE types, int count)
{
    if (count < 0 || (count && (!points || !types))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    return NtGdiGetPath(dc, points, types, count);
}

static BOOL poly_draw(HDC dc, const POINT *points, DWORD count, int function)
{
    ULONG n = count;
    if (!points || !count)
        return FALSE;
    return NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, function) != 0;
}

WINGDIAPI BOOL WINAPI PolyBezier(HDC dc, const POINT *points, DWORD count)
{
    if (count < 4 || (count - 1) % 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return poly_draw(dc, points, count, MIME_POLY_BEZIER);
}

WINGDIAPI BOOL WINAPI PolyBezierTo(HDC dc, const POINT *points, DWORD count)
{
    if (!count || count % 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return poly_draw(dc, points, count, MIME_POLY_BEZIERTO);
}

WINGDIAPI BOOL WINAPI PolylineTo(HDC dc, const POINT *points, DWORD count)
{
    return poly_draw(dc, points, count, MIME_POLY_LINETO);
}

/* ------------------------------------------------------------------ world transform */

WINGDIAPI BOOL WINAPI SetWorldTransform(HDC dc, const XFORM *xform)
{
    if (!xform)
        return FALSE;
    return NtGdiModifyWorldTransform(dc, xform, MWT_SET);
}

WINGDIAPI BOOL WINAPI ModifyWorldTransform(HDC dc, const XFORM *xform, DWORD mode)
{
    if (mode != MWT_IDENTITY && !xform)
        return FALSE;
    return NtGdiModifyWorldTransform(dc, xform, mode);
}

WINGDIAPI BOOL WINAPI GetWorldTransform(HDC dc, LPXFORM xform)
{
    if (!xform)
        return FALSE;
    return NtGdiGetTransform(dc, MIME_XFORM_WORLD, xform);
}

/* ------------------------------------------------------------------ device dependent bits */

WINGDIAPI LONG WINAPI GetBitmapBits(HBITMAP bitmap, LONG size, LPVOID bits)
{
    if (size < 0)
        return 0;
    return NtGdiGetBitmapBits(bitmap, (ULONG)size, bits);
}

WINGDIAPI LONG WINAPI SetBitmapBits(HBITMAP bitmap, DWORD size, const void *bits)
{
    return NtGdiSetBitmapBits(bitmap, size, bits);
}

/* ------------------------------------------------------------------ printing */

/*
 * there are no printers: every dc is a display dc, and the document calls
 * fail the way gdi32 fails them on a display dc, with SP_ERROR and no
 * error code. an invalid dc reports ERROR_INVALID_HANDLE.
 */
static int print_call(HDC dc)
{
    DWORD v;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v))
        SetLastError(ERROR_INVALID_HANDLE);
    return SP_ERROR;
}

WINGDIAPI int WINAPI StartDocW(HDC dc, const DOCINFOW *info)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI StartDocA(HDC dc, const DOCINFOA *info)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI EndDoc(HDC dc)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI StartPage(HDC dc)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI EndPage(HDC dc)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI AbortDoc(HDC dc)
{
    return print_call(dc);
}

WINGDIAPI int WINAPI SetAbortProc(HDC dc, ABORTPROC proc)
{
    return print_call(dc);
}

/* only printer dcs can be reset. */
WINGDIAPI HDC WINAPI ResetDCW(HDC dc, const void *mode)
{
    print_call(dc);
    return NULL;
}

WINGDIAPI HDC WINAPI ResetDCA(HDC dc, const void *mode)
{
    return ResetDCW(dc, mode);
}

/* the display driver implements no escapes; QUERYESCSUPPORT answers for the escapes gdi32 handles itself. */
WINGDIAPI int WINAPI ExtEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out)
{
    if (escape == QUERYESCSUPPORT) {
        if (!in || in_size < 4)
            return 0;
        DWORD query;
        memcpy(&query, in, sizeof(query));
        return query == QUERYESCSUPPORT;
    }
    return 0;
}

WINGDIAPI int WINAPI Escape(HDC dc, int escape, int in_size, LPCSTR in, LPVOID out)
{
    return ExtEscape(dc, escape, in_size, in, 0, out);
}

/* ------------------------------------------------------------------ pixel formats */

/*
 * the formats of the generic gdi implementation of opengl32 on a 32 bpp
 * display. windows reports them through opengl32; mime has no opengl, so
 * the formats can be described and selected but nothing renders through
 * them. the exact list is a guess modeled on the generic implementation.
 */
typedef struct PIXEL_FORMAT {
    DWORD Flags;
    BYTE ColorBits;
    BYTE AlphaBits;
    BYTE AccumBits;
    BYTE DepthBits;
    BYTE StencilBits;
} PIXEL_FORMAT;

#define GENERIC_WINDOW (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT)
#define GENERIC_BITMAP (PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT)

static const PIXEL_FORMAT g_formats[] = {
    {GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 0, 64, 32, 8},
    {GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 0, 64, 16, 8},
    {GENERIC_WINDOW | PFD_DOUBLEBUFFER | PFD_SWAP_COPY, 32, 0, 64, 32, 8},
    {GENERIC_WINDOW | PFD_DOUBLEBUFFER | PFD_SWAP_COPY, 32, 0, 64, 16, 8},
    {GENERIC_WINDOW | PFD_SUPPORT_GDI | PFD_DRAW_TO_BITMAP, 32, 8, 64, 32, 8},
    {GENERIC_WINDOW | PFD_DOUBLEBUFFER | PFD_SWAP_COPY, 32, 8, 64, 32, 8},
    {GENERIC_BITMAP, 24, 0, 64, 32, 8},
    {GENERIC_BITMAP, 24, 0, 64, 16, 8},
    {GENERIC_BITMAP, 16, 0, 32, 32, 8},
    {GENERIC_BITMAP, 16, 0, 32, 16, 8},
    {GENERIC_BITMAP, 8, 0, 16, 32, 8},
    {GENERIC_BITMAP, 8, 0, 16, 16, 8},
};

#define FORMAT_COUNT ((int)RTL_NUMBER_OF(g_formats))

static void describe(int index, PIXELFORMATDESCRIPTOR *pfd)
{
    const PIXEL_FORMAT *f = &g_formats[index - 1];
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

WINGDIAPI int WINAPI DescribePixelFormat(HDC dc, int format, UINT size, LPPIXELFORMATDESCRIPTOR pfd)
{
    DWORD v;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!pfd)
        return FORMAT_COUNT;
    if (format < 1 || format > FORMAT_COUNT || size < sizeof(PIXELFORMATDESCRIPTOR)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    describe(format, pfd);
    return FORMAT_COUNT;
}

/* the best match for the requested flags, color depth, alpha, depth and stencil bits. */
WINGDIAPI int WINAPI ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *want)
{
    if (!want || want->nSize != sizeof(PIXELFORMATDESCRIPTOR)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DWORD v;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    DWORD required = want->dwFlags & (PFD_DRAW_TO_WINDOW | PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI | PFD_SUPPORT_OPENGL);
    int best = 0;
    LONG best_score = 0x7FFFFFFF;
    for (int i = 1; i <= FORMAT_COUNT; i++) {
        PIXELFORMATDESCRIPTOR pfd;
        describe(i, &pfd);
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
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

WINGDIAPI BOOL WINAPI SetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd)
{
    if (format < 1 || format > FORMAT_COUNT) {
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    return NtGdiSetPixelFormat(dc, format);
}

WINGDIAPI int WINAPI GetPixelFormat(HDC dc)
{
    DWORD v = 0;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_PIXELFORMAT, &v)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!v)
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return (int)v;
}

/* nothing renders through opengl, so a double buffered window has nothing to show. */
WINGDIAPI BOOL WINAPI SwapBuffers(HDC dc)
{
    int format = GetPixelFormat(dc);
    if (!format)
        return FALSE;
    PIXELFORMATDESCRIPTOR pfd;
    describe(format, &pfd);
    if (!(pfd.dwFlags & PFD_DOUBLEBUFFER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}
