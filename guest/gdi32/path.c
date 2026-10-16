/*
 * gdi32 paths, world transforms, device dependent bitmap bits, printing
 * and pixel formats.
 */
#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/pixelformat.h>
#include <mime/stub.h>

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

/* validates every type first, like windows, then replays the figures through the current position. */
WINGDIAPI BOOL WINAPI PolyDraw(HDC dc, const POINT *points, const BYTE *types, int count)
{
    if (count < 0 || (count && (!points || !types))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < count; i++) {
        BYTE type = types[i] & ~PT_CLOSEFIGURE;
        if (type == PT_BEZIERTO) {
            if (i + 2 >= count || (types[i + 1] & ~PT_CLOSEFIGURE) != PT_BEZIERTO ||
                (types[i + 2] & ~PT_CLOSEFIGURE) != PT_BEZIERTO)
                return FALSE;
            i += 2;
        } else if (type != PT_MOVETO && type != PT_LINETO) {
            return FALSE;
        }
    }
    DWORD state = 0;
    NtGdiGetDCDword(dc, MIME_DCDW_PATHSTATE, &state);
    POINT start;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_CURRENTPOSITION, &start))
        return FALSE;
    for (int i = 0; i < count; i++) {
        BYTE type = types[i] & ~PT_CLOSEFIGURE;
        BOOL ok;
        if (type == PT_MOVETO) {
            start = points[i];
            ok = NtGdiMoveTo(dc, points[i].x, points[i].y, nullptr);
        } else if (type == PT_LINETO) {
            ok = NtGdiLineTo(dc, points[i].x, points[i].y);
        } else {
            ok = PolyBezierTo(dc, &points[i], 3);
            i += 2;
        }
        if (!ok)
            return FALSE;
        if (type != PT_MOVETO && (types[i] & PT_CLOSEFIGURE)) {
            if (state == 1)
                NtGdiCloseFigure(dc);
            else
                NtGdiLineTo(dc, start.x, start.y);
        }
    }
    return TRUE;
}

/* win32k draws no elliptic arcs yet. */
WINGDIAPI BOOL WINAPI ArcTo(HDC dc, int left, int top, int right, int bottom, int x1, int y1, int x2, int y2)
{
    MIME_STUB("gdi32");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

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
    return nullptr;
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

#define FORMAT_COUNT MIME_PIXEL_FORMAT_COUNT
#define describe MimeDescribePixelFormat

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
    return MimeChoosePixelFormat(want);
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

/* like windows, the opengl32 the process loaded presents. */
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
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    BOOL(WINAPI * swap)(HDC) = gl ? (BOOL(WINAPI *)(HDC))GetProcAddress(gl, "wglSwapBuffers") : nullptr;
    return swap ? swap(dc) : TRUE;
}
