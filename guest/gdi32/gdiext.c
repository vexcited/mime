/*
 * gdi32 functions that the api sweep found missing: the ANSI forms of the text functions, the small wrappers
 * over what win32k offers, palette and color table calls, line enumeration and the brush origin.
 */
#include "gdi32_private.h"
#include <mime/win32/wingdi.h>

static void *heap_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
}

static void heap_free(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static BOOL dc_valid(HDC dc)
{
    DWORD v;
    return NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v);
}

/* ------------------------------------------------------------------ the ANSI forms of the text functions */

/*
 * a character code of the ANSI code page as the wide character it stands for. A double byte code has its lead
 * byte in the high byte. Characters the code page lacks fail.
 */
static BOOL code_to_wide(UINT code, WCHAR *wide)
{
    char bytes[2];
    int n = 0;
    if (code > 0xFF)
        bytes[n++] = (char)(code >> 8);
    bytes[n++] = (char)code;
    return MultiByteToWideChar(CP_ACP, 0, bytes, n, wide, 1) == 1;
}

/* the wide characters of a range of codes, an unmapped one standing for the default character. */
static WCHAR *wide_range(UINT first, UINT last)
{
    if (last < first || last - first > 0xFFFF)
        return nullptr;
    UINT count = last - first + 1;
    WCHAR *w = heap_alloc((SIZE_T)count * sizeof(WCHAR));
    if (!w)
        return nullptr;
    for (UINT i = 0; i < count; i++)
        if (!code_to_wide(first + i, &w[i]))
            w[i] = 0xFFFF;
    return w;
}

WINGDIAPI BOOL WINAPI GetCharWidthA(HDC dc, UINT first, UINT last, LPINT widths)
{
    if (!widths || last < first) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return GetCharWidth32A(dc, first, last, widths);
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsA(HDC dc, UINT first, UINT last, LPABC abc)
{
    if (!abc || last < first) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR *w = wide_range(first, last);
    if (!w) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = NtGdiGetCharABCWidthsW(dc, 0, last - first + 1, w, 0, abc);
    heap_free(w);
    return ok;
}

WINGDIAPI BOOL WINAPI GetCharWidthFloatW(HDC dc, UINT first, UINT last, PFLOAT widths)
{
    if (!widths || last < first || last - first > 0xFFFF) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT count = last - first + 1;
    LPINT whole = heap_alloc((SIZE_T)count * sizeof(INT));
    if (!whole) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    /* widths in logical units, whole numbers: the fractions of a scalable font are not kept. */
    BOOL ok = NtGdiGetCharWidthW(dc, first, count, nullptr, 0, whole);
    if (ok)
        for (UINT i = 0; i < count; i++)
            widths[i] = (FLOAT)whole[i];
    heap_free(whole);
    return ok;
}

WINGDIAPI BOOL WINAPI GetCharWidthFloatA(HDC dc, UINT first, UINT last, PFLOAT widths)
{
    if (!widths || last < first || last - first > 0xFFFF) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT count = last - first + 1;
    LPINT whole = heap_alloc((SIZE_T)count * sizeof(INT));
    if (!whole) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = GetCharWidth32A(dc, first, last, whole);
    if (ok)
        for (UINT i = 0; i < count; i++)
            widths[i] = (FLOAT)whole[i];
    heap_free(whole);
    return ok;
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsFloatA(HDC dc, UINT first, UINT last, LPABCFLOAT abc)
{
    if (!abc || last < first) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR *w = wide_range(first, last);
    if (!w) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = NtGdiGetCharABCWidthsW(dc, 0, last - first + 1, w, MIME_GCABCW_FLOAT, abc);
    heap_free(w);
    return ok;
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsFloatI(HDC dc, UINT first, UINT count, LPWORD glyphs, LPABCFLOAT abc)
{
    if (!abc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetCharABCWidthsW(dc, first, count, (PWCHAR)glyphs, MIME_GCABCW_INDICES | MIME_GCABCW_FLOAT, abc);
}

/* a count of 0x2AAAAAAA characters or more, or a limit under -1, is an invalid parameter. */
WINGDIAPI BOOL WINAPI GetTextExtentExPointA(HDC dc, LPCSTR text, int count, int max_extent, LPINT fit, LPINT dx,
                                            LPSIZE size)
{
    if ((UINT)count > 0x2AAAAAAA || max_extent < -1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR *w = heap_alloc(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (!w) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    int n = 0;
    if (count) {
        n = MultiByteToWideChar(CP_ACP, 0, text, count, w, count);
        if (!n) {
            heap_free(w);
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    w[n] = 0;
    BOOL ok = GetTextExtentExPointW(dc, w, n, max_extent, fit, dx, size);
    heap_free(w);
    return ok;
}

WINGDIAPI BOOL WINAPI GetTextExtentExPointI(HDC dc, LPWORD glyphs, int count, int max_extent, LPINT fit, LPINT dx,
                                            LPSIZE size)
{
    if ((UINT)count > 0x2AAAAAAA || max_extent < -1 || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetTextExtentExW(dc, (LPCWSTR)glyphs, (ULONG)count, (ULONG)max_extent, (ULONG *)fit, (ULONG *)dx,
                                 size, MIME_GTEE_INDICES);
}

/* the glyphs of an ANSI string; a count of 0 is 0 glyphs, a negative one -1 (GDI_ERROR) with a bad parameter. */
WINGDIAPI DWORD WINAPI GetGlyphIndicesA(HDC dc, LPCSTR text, int count, LPWORD indices, DWORD flags)
{
    if (count < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return GDI_ERROR;
    }
    if (!count)
        return 0;
    if (!dc_valid(dc)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return GDI_ERROR;
    }
    WCHAR *w = heap_alloc((SIZE_T)count * sizeof(WCHAR));
    if (!w) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return GDI_ERROR;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, text, count, w, count);
    DWORD result = GDI_ERROR;
    if (n)
        result = GetGlyphIndicesW(dc, w, n, indices, flags);
    else
        SetLastError(ERROR_INVALID_PARAMETER);
    heap_free(w);
    return result;
}

/* ------------------------------------------------------------------ fonts */

WINGDIAPI int WINAPI EnumFontFamiliesA(HDC dc, LPCSTR family, FONTENUMPROCA proc, LPARAM param)
{
    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    if (family)
        for (int i = 0; i < LF_FACESIZE - 1 && family[i]; i++)
            lf.lfFaceName[i] = family[i];
    return EnumFontFamiliesExA(dc, &lf, proc, param, 0);
}

/* the callbacks of EnumFonts get the same records as those of EnumFontFamilies. */
WINGDIAPI int WINAPI EnumFontsA(HDC dc, LPCSTR face, FONTENUMPROCA proc, LPARAM param)
{
    return EnumFontFamiliesA(dc, face, proc, param);
}

WINGDIAPI int WINAPI EnumFontsW(HDC dc, LPCWSTR face, FONTENUMPROCW proc, LPARAM param)
{
    return EnumFontFamiliesW(dc, face, proc, param);
}

WINGDIAPI int WINAPI AddFontResourceExA(LPCSTR name, DWORD flags, PVOID reserved)
{
    if (flags & ~(FR_PRIVATE | FR_NOT_ENUM)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR w[MAX_PATH];
    int n = MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH);
    if (!n)
        return 0;
    return AddFontResourceExW(w, flags, reserved);
}

WINGDIAPI int WINAPI AddFontResourceA(LPCSTR name)
{
    return AddFontResourceExA(name, 0, nullptr);
}

WINGDIAPI BOOL WINAPI RemoveFontResourceExA(LPCSTR name, DWORD flags, PVOID reserved)
{
    if (flags & ~(FR_PRIVATE | FR_NOT_ENUM)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR w[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH))
        return FALSE;
    return RemoveFontResourceExW(w, flags, reserved);
}

WINGDIAPI BOOL WINAPI RemoveFontResourceA(LPCSTR name)
{
    return RemoveFontResourceExA(name, 0, nullptr);
}

/* the rasterizer: truetype is enabled and available, the language id is the system's; fewer bytes than the structure holds copy as far as they go. */
WINGDIAPI BOOL WINAPI GetRasterizerCaps(LPRASTERIZER_STATUS status, UINT bytes)
{
    RASTERIZER_STATUS full = {sizeof(RASTERIZER_STATUS), TT_ENABLED | TT_AVAILABLE, 0x0409};
    if (!status)
        return FALSE;
    memcpy(status, &full, bytes < sizeof(full) ? bytes : sizeof(full));
    return TRUE;
}

/* ------------------------------------------------------------------ text */

WINGDIAPI BOOL WINAPI PolyTextOutW(HDC dc, const POLYTEXTW *texts, int count)
{
    if (count < 0 || (count && !texts)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < count; i++) {
        const POLYTEXTW *t = &texts[i];
        if (!ExtTextOutW(dc, t->x, t->y, t->uiFlags, &t->rcl, t->lpstr, t->n, t->pdx))
            return FALSE;
    }
    return TRUE;
}

WINGDIAPI BOOL WINAPI PolyTextOutA(HDC dc, const POLYTEXTA *texts, int count)
{
    if (count < 0 || (count && !texts)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < count; i++) {
        const POLYTEXTA *t = &texts[i];
        if (!ExtTextOutA(dc, t->x, t->y, t->uiFlags, &t->rcl, t->lpstr, t->n, t->pdx))
            return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ objects and shapes */

WINGDIAPI HBITMAP WINAPI CreateDiscardableBitmap(HDC dc, int width, int height)
{
    return CreateCompatibleBitmap(dc, width, height);
}

WINGDIAPI HRGN WINAPI CreateEllipticRgnIndirect(const RECT *r)
{
    return CreateEllipticRgn(r->left, r->top, r->right, r->bottom);
}

/* the polygons are filled as a path of a dc of their own, as CreatePolygonRgn does. */
WINGDIAPI HRGN WINAPI CreatePolyPolygonRgn(const POINT *points, const INT *counts, int polygons, int mode)
{
    if (!points || !counts || polygons <= 0 || (mode != ALTERNATE && mode != WINDING)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return nullptr;
    HRGN region = nullptr;
    SetPolyFillMode(dc, mode);
    if (BeginPath(dc) && PolyPolygon(dc, points, counts, polygons) && EndPath(dc))
        region = PathToRegion(dc);
    DeleteDC(dc);
    return region;
}

WINGDIAPI HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL packed, UINT usage)
{
    const void *dib = GlobalLock(packed);
    if (!dib)
        return nullptr;
    HBRUSH brush = CreateDIBPatternBrushPt(dib, usage);
    GlobalUnlock(packed);
    return brush;
}

/*
 * fills the area around a point with the brush of the dc, reading the pixels back to find where the area ends.
 * FLOODFILLBORDER spreads over everything that is not the color given, FLOODFILLSURFACE over everything that is.
 * The point itself must be inside the clip and be part of the area. The area is built as a region of runs and
 * painted in one call.
 */
BOOL FloodFillDc(HDC dc, int x, int y, COLORREF color, UINT type)
{
    if (type != FLOODFILLBORDER && type != FLOODFILLSURFACE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RECT box;
    if (GetClipBox(dc, &box) == ERROR || x < box.left || x >= box.right || y < box.top || y >= box.bottom)
        return FALSE;
    color &= 0x00FFFFFF;
    int width = box.right - box.left, height = box.bottom - box.top;
    SIZE_T cells = (SIZE_T)width * height;
    if (!cells || cells > 0x10000000)
        return FALSE;
    BYTE *seen = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, (cells + 7) / 8);
    POINT *stack = heap_alloc(sizeof(POINT) * 1024);
    SIZE_T capacity = 1024, top = 0;
    HRGN area = CreateRectRgn(0, 0, 0, 0), run = CreateRectRgn(0, 0, 0, 0);
    BOOL ok = seen && stack && area && run;
    BOOL started = FALSE;

    #define INSIDE(px, py) \
        ({ COLORREF c_ = GetPixel(dc, (px), (py)); \
           c_ != CLR_INVALID && ((type == FLOODFILLBORDER) ? c_ != color : c_ == color); })
    #define SEEN(px, py) (seen[((SIZE_T)((py) - box.top) * width + ((px) - box.left)) / 8] & \
                          (1 << (((SIZE_T)((py) - box.top) * width + ((px) - box.left)) & 7)))
    #define MARK(px, py) (seen[((SIZE_T)((py) - box.top) * width + ((px) - box.left)) / 8] |= \
                          (1 << (((SIZE_T)((py) - box.top) * width + ((px) - box.left)) & 7)))

    if (ok && INSIDE(x, y)) {
        started = TRUE;
        stack[top++] = (POINT){x, y};
        while (ok && top) {
            POINT seed = stack[--top];
            if (SEEN(seed.x, seed.y))
                continue;
            int left = seed.x, right = seed.x;
            while (left > box.left && !SEEN(left - 1, seed.y) && INSIDE(left - 1, seed.y))
                left--;
            while (right + 1 < box.right && !SEEN(right + 1, seed.y) && INSIDE(right + 1, seed.y))
                right++;
            for (int i = left; i <= right; i++)
                MARK(i, seed.y);
            SetRectRgn(run, left, seed.y, right + 1, seed.y + 1);
            CombineRgn(area, area, run, RGN_OR);
            for (int row = seed.y - 1; row <= seed.y + 1; row += 2) {
                if (row < box.top || row >= box.bottom)
                    continue;
                BOOL in_run = FALSE;
                for (int i = left; i <= right; i++) {
                    BOOL want = !SEEN(i, row) && INSIDE(i, row);
                    if (want && !in_run) {
                        if (top == capacity) {
                            POINT *grown = heap_alloc(sizeof(POINT) * capacity * 2);
                            if (!grown) {
                                ok = FALSE;
                                break;
                            }
                            memcpy(grown, stack, sizeof(POINT) * top);
                            heap_free(stack);
                            stack = grown;
                            capacity *= 2;
                        }
                        stack[top++] = (POINT){i, row};
                    }
                    in_run = want;
                }
            }
        }
    }
    #undef INSIDE
    #undef SEEN
    #undef MARK

    if (ok && started) {
        HGDIOBJ brush = GetCurrentObject(dc, OBJ_BRUSH);
        ok = FillRgn(dc, area, brush);
    } else {
        ok = FALSE;
    }
    if (area)
        DeleteObject(area);
    if (run)
        DeleteObject(run);
    heap_free(stack);
    heap_free(seen);
    return ok;
}

WINGDIAPI BOOL WINAPI FloodFill(HDC dc, int x, int y, COLORREF color)
{
    return ExtFloodFill(dc, x, y, color, FLOODFILLBORDER);
}

/*
 * the points of a line, the way windows steps it: along the longer axis, a step in the other when the error
 * term goes below zero. The end point is not given.
 */
WINGDIAPI BOOL WINAPI LineDDA(int x0, int y0, int x1, int y1, LINEDDAPROC proc, LPARAM param)
{
    if (!proc)
        return FALSE;
    int step_x = x1 > x0 ? 1 : -1, step_y = y1 > y0 ? 1 : -1;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int x = x0, y = y0;
    if (dy < dx) {
        int error = dx / 2;
        for (int n = dx; n; n--) {
            proc(x, y, param);
            x += step_x;
            error -= dy;
            if (error < 0)
                error += dx, y += step_y;
        }
    } else {
        int error = dy / 2;
        for (int n = dy; n; n--) {
            proc(x, y, param);
            y += step_y;
            error -= dx;
            if (error < 0)
                error += dy, x += step_x;
        }
    }
    return TRUE;
}

/* xform1 followed by xform2: the product of the matrices as windows lays them out, row vectors. */
WINGDIAPI BOOL WINAPI CombineTransform(LPXFORM out, const XFORM *first, const XFORM *second)
{
    if (!out || !first || !second)
        return FALSE;
    XFORM r;
    r.eM11 = first->eM11 * second->eM11 + first->eM12 * second->eM21;
    r.eM12 = first->eM11 * second->eM12 + first->eM12 * second->eM22;
    r.eM21 = first->eM21 * second->eM11 + first->eM22 * second->eM21;
    r.eM22 = first->eM21 * second->eM12 + first->eM22 * second->eM22;
    r.eDx = first->eDx * second->eM11 + first->eDy * second->eM21 + second->eDx;
    r.eDy = first->eDx * second->eM12 + first->eDy * second->eM22 + second->eDy;
    *out = r;
    return TRUE;
}

/*
 * there is no aspect ratio filter on a display, and a printer's is its device's; this is the answer a device with
 * none gives.
 */
WINGDIAPI BOOL WINAPI GetAspectRatioFilterEx(HDC dc, LPSIZE size)
{
    if (!dc_valid(dc) || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    size->cx = size->cy = 0;
    return TRUE;
}

/* a true color device has no palette to refresh. */
WINGDIAPI BOOL WINAPI UpdateColors(HDC dc)
{
    if (!dc_valid(dc)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

/*
 * a brush is realized as it is selected, so there is nothing to undo; a palette's realization is not kept, and
 * any other object only has to exist. The stock objects of a program count as existing.
 */
WINGDIAPI BOOL WINAPI UnrealizeObject(HGDIOBJ object)
{
    if (GetObjectType(object) == OBJ_BRUSH)
        return TRUE;
    if (PalIsPalette(object))
        return TRUE;
    return GetObjectType(object) != 0;
}

/* ------------------------------------------------------------------ the brush origin of a dc */

#define ORIGIN_MAX 64

static struct {
    HDC Dc;
    POINT Origin;
} g_origins[ORIGIN_MAX];
static RTL_SRWLOCK g_origin_lock;

void GdiForgetBrushOrigin(HDC dc)
{
    RtlAcquireSRWLockExclusive(&g_origin_lock);
    for (UINT i = 0; i < ORIGIN_MAX; i++)
        if (g_origins[i].Dc == dc)
            g_origins[i].Dc = nullptr;
    RtlReleaseSRWLockExclusive(&g_origin_lock);
}

/* the origin is kept and read back, and drawing does not use it: patterns always start at the dc's origin. */
BOOL GdiSwapBrushOrigin(HDC dc, int x, int y, LPPOINT old)
{
    if (!dc_valid(dc))
        return FALSE;
    RtlAcquireSRWLockExclusive(&g_origin_lock);
    UINT free_slot = ORIGIN_MAX, found = ORIGIN_MAX;
    for (UINT i = 0; i < ORIGIN_MAX; i++) {
        if (g_origins[i].Dc == dc)
            found = i;
        else if (!g_origins[i].Dc && free_slot == ORIGIN_MAX)
            free_slot = i;
    }
    POINT previous = found < ORIGIN_MAX ? g_origins[found].Origin : (POINT){0, 0};
    UINT slot = found < ORIGIN_MAX ? found : free_slot;
    if (slot < ORIGIN_MAX) {
        g_origins[slot].Dc = dc;
        g_origins[slot].Origin = (POINT){x, y};
    }
    RtlReleaseSRWLockExclusive(&g_origin_lock);
    if (old)
        *old = previous;
    return TRUE;
}

WINGDIAPI BOOL WINAPI GetBrushOrgEx(HDC dc, LPPOINT point)
{
    if (!dc_valid(dc) || !point) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    POINT origin = {0, 0};
    RtlAcquireSRWLockShared(&g_origin_lock);
    for (UINT i = 0; i < ORIGIN_MAX; i++)
        if (g_origins[i].Dc == dc)
            origin = g_origins[i].Origin;
    RtlReleaseSRWLockShared(&g_origin_lock);
    *point = origin;
    return TRUE;
}

/* ------------------------------------------------------------------ dib color tables */

/* the color table of the dib section selected into a dc: how many entries it gave, 0 for other bitmaps. */
WINGDIAPI UINT WINAPI GetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD *colors)
{
    if (!count)
        return 0;
    return (UINT)NtGdiDoPalette(dc, (WORD)start, (WORD)count, colors, MIME_PAL_GETDIBCOLORTABLE, FALSE);
}

WINGDIAPI UINT WINAPI SetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD *colors)
{
    if (!count)
        return 0;
    return (UINT)NtGdiDoPalette(dc, (WORD)start, (WORD)count, (PVOID)colors, MIME_PAL_SETDIBCOLORTABLE, TRUE);
}
