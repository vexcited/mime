/*
 * gdi32: objects live in win32k, these wrappers marshal arguments to the
 * NtGdi services.
 */
#include "gdi32_private.h"
#include <mime/win32/winnls.h>
#include <mime/win32/d3dkmthk.h>
#include <mime/stub.h>

/* tells the linker floating point is used, which the metafile code does. */
int _fltused = 0x9875;

static UCHAR handle_type(HGDIOBJ h)
{
    return (UCHAR)(((ULONG_PTR)h >> 16) & 0xFF);
}

/* ------------------------------------------------------------------ objects */

WINGDIAPI HGDIOBJ WINAPI GetStockObject(int index)
{
    if (index == DEFAULT_PALETTE)
        return PalDefault();
    return NtGdiGetStockObject(index);
}

WINGDIAPI DWORD WINAPI GetObjectType(HGDIOBJ h)
{
    DWORD meta = MfObjectType(h);
    if (!meta)
        meta = WmfObjectType(h);
    if (meta)
        return meta;
    switch (handle_type(h)) {
    case 0x01: {
        /* like windows, a deleted dc has no type. */
        DWORD v;
        return NtGdiGetDCDword(h, MIME_DCDW_MAPMODE, &v) ? OBJ_DC : 0;
    }
    case 0x04:
        return OBJ_REGION;
    case 0x05:
        return OBJ_BITMAP;
    case 0x08:
        return OBJ_PAL;
    case 0x0A:
        return OBJ_FONT;
    case 0x10:
        return OBJ_BRUSH;
    case 0x30:
        return OBJ_PEN;
    case 0x50:
        return OBJ_EXTPEN;
    }
    return 0;
}

WINGDIAPI HGDIOBJ WINAPI SelectObject(HDC dc, HGDIOBJ h)
{
    if (MfActive(dc)) {
        HGDIOBJ previous = nullptr;
        if (!MfSelectObject(dc, h, &previous))
            return nullptr;
        if (MfIsWmfDc(dc))
            return handle_type(h) == 0x04 ? (HGDIOBJ)(LONG_PTR)SIMPLEREGION : previous;
    }
    switch (handle_type(h)) {
    case 0x05:
        return NtGdiSelectBitmap(dc, h);
    case 0x10:
        return NtGdiSelectBrush(dc, h);
    case 0x30:
    case 0x50:
        return NtGdiSelectPen(dc, h);
    case 0x0A:
        return NtGdiSelectFont(dc, h);
    case 0x04:
        return (HGDIOBJ)(LONG_PTR)NtGdiExtSelectClipRgn(dc, h, RGN_COPY);
    }
    return nullptr;
}

WINGDIAPI BOOL WINAPI DeleteObject(HGDIOBJ h)
{
    if (h && MfDcCount)
        MfDeleteObject(h);
    if (handle_type(h) == 0x08)
        return PalDelete(h);
    return h ? NtGdiDeleteObjectApp(h) : FALSE;
}

WINGDIAPI int WINAPI GetObjectW(HANDLE h, int size, LPVOID buffer)
{
    if (handle_type(h) == 0x08)
        return PalGetObject(h, size, buffer);
    return NtGdiExtGetObjectW(h, size, buffer);
}

WINGDIAPI int WINAPI GetObjectA(HANDLE h, int size, LPVOID buffer)
{
    if (GetObjectType(h) != OBJ_FONT)
        return GetObjectW(h, size, buffer);
    LOGFONTW lfw;
    if (!NtGdiExtGetObjectW(h, sizeof(lfw), &lfw))
        return 0;
    if (!buffer)
        return sizeof(LOGFONTA);
    LOGFONTA lfa;
    memcpy(&lfa, &lfw, offsetof(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_ACP, 0, lfw.lfFaceName, -1, lfa.lfFaceName, LF_FACESIZE, nullptr, nullptr);
    int n = size < (int)sizeof(lfa) ? size : (int)sizeof(lfa);
    memcpy(buffer, &lfa, (size_t)n);
    return n;
}

WINGDIAPI HGDIOBJ WINAPI GetCurrentObject(HDC dc, UINT type)
{
    if (type == OBJ_PAL) {
        DWORD v;
        return NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v) ? PalCurrent(dc) : nullptr;
    }
    return NtGdiGetDCObject(dc, (int)type);
}

/* ------------------------------------------------------------------ device contexts */

WINGDIAPI HDC WINAPI CreateCompatibleDC(HDC dc)
{
    return NtGdiCreateCompatibleDC(dc);
}

/*
 * a device that starts with \\.\DISPLAY wins, then the driver DISPLAY, which is the whole screen, then a driver that
 * starts with \\.\DISPLAY. anything else is no display, and CreateDC fails.
 */
static HDC open_display(LPCWSTR driver, LPCWSTR device)
{
    static const WCHAR prefix[] = L"\\\\.\\DISPLAY";
    UNICODE_STRING name = {};
    if (device && !_wcsnicmp(device, prefix, 11)) {
        RtlInitUnicodeString(&name, device);
    } else if (driver && !_wcsicmp(driver, L"DISPLAY")) {
        name.Buffer = nullptr;
    } else if (driver && !_wcsnicmp(driver, prefix, 11)) {
        RtlInitUnicodeString(&name, driver);
    } else {
        return nullptr;
    }
    return NtGdiOpenDCW(name.Buffer ? &name : nullptr, nullptr, nullptr, 0, TRUE, nullptr, nullptr, nullptr);
}

/* a printer dc when the driver and device name a printer, else a display dc. */
static HDC create_dc(LPCWSTR driver, LPCWSTR device, const void *mode, BOOL info_only)
{
    BOOL handled;
    HDC printer = PrnCreateDC(driver, device, mode, info_only, &handled);
    if (handled)
        return printer;
    return open_display(driver, device);
}

static HDC create_dc_a(LPCSTR driver, LPCSTR device, const void *mode, BOOL info_only)
{
    BOOL handled;
    HDC printer = PrnCreateDCA(driver, device, mode, info_only, &handled);
    if (handled)
        return printer;
    WCHAR wide_driver[64], wide_device[64];
    LPCWSTR d = nullptr, n = nullptr;
    if (driver && MultiByteToWideChar(CP_ACP, 0, driver, -1, wide_driver, ARRAYSIZE(wide_driver)))
        d = wide_driver;
    if (device && MultiByteToWideChar(CP_ACP, 0, device, -1, wide_device, ARRAYSIZE(wide_device)))
        n = wide_device;
    return open_display(d, n);
}

WINGDIAPI HDC WINAPI CreateDCW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const void *mode)
{
    return create_dc(driver, device, mode, FALSE);
}

WINGDIAPI HDC WINAPI CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR output, const void *mode)
{
    return create_dc_a(driver, device, mode, FALSE);
}

WINGDIAPI HDC WINAPI CreateICW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const void *mode)
{
    return create_dc(driver, device, mode, TRUE);
}

WINGDIAPI HDC WINAPI CreateICA(LPCSTR driver, LPCSTR device, LPCSTR output, const void *mode)
{
    return create_dc_a(driver, device, mode, TRUE);
}

WINGDIAPI BOOL WINAPI DeleteDC(HDC dc)
{
    if (MfActive(dc))
        return MfDeleteDC(dc);
    PalForgetDC(dc);
    if (PrnDcCount && PrnDeleteDC(dc))
        return TRUE;
    return NtGdiDeleteObjectApp(dc);
}

WINGDIAPI int WINAPI SaveDC(HDC dc)
{
    MF_RECORD(dc, MfRecordEmpty(dc, EMR_SAVEDC, META_SAVEDC), 0, 1);
    return NtGdiSaveDC(dc);
}

WINGDIAPI BOOL WINAPI RestoreDC(HDC dc, int saved)
{
    MF_RECORD(dc, MfRestoreDC(dc, saved), FALSE, TRUE);
    return NtGdiRestoreDC(dc, saved);
}

WINGDIAPI int WINAPI GetDeviceCaps(HDC dc, int index)
{
    if (PrnDcCount) {
        int value;
        if (PrnGetDeviceCaps(dc, index, &value))
            return value;
    }
    if (MfDcCount) {
        BOOL handled;
        int value = MfGetDeviceCaps(dc, index, &handled);
        if (handled)
            return value;
    }
    return NtGdiGetDeviceCaps(dc, index);
}

static DWORD set_dword(HDC dc, UINT index, DWORD value, DWORD fail)
{
    DWORD old;
    return NtGdiGetAndSetDCDword(dc, index, value, &old) ? old : fail;
}

static DWORD get_dword(HDC dc, UINT index, DWORD fail)
{
    DWORD v;
    return NtGdiGetDCDword(dc, index, &v) ? v : fail;
}

WINGDIAPI COLORREF WINAPI SetTextColor(HDC dc, COLORREF color)
{
    MF_RECORD(dc, MfSetColor(dc, EMR_SETTEXTCOLOR, META_SETTEXTCOLOR, color), CLR_INVALID, TRUE);
    return set_dword(dc, MIME_DCDW_TEXTCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetTextColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_TEXTCOLOR, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI SetBkColor(HDC dc, COLORREF color)
{
    MF_RECORD(dc, MfSetColor(dc, EMR_SETBKCOLOR, META_SETBKCOLOR, color), CLR_INVALID, TRUE);
    return set_dword(dc, MIME_DCDW_BKCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetBkColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_BKCOLOR, CLR_INVALID);
}

WINGDIAPI int WINAPI SetBkMode(HDC dc, int mode)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETBKMODE, META_SETBKMODE, (DWORD)mode), 0, TRUE);
    return (int)set_dword(dc, MIME_DCDW_BKMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetBkMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_BKMODE, 0);
}

WINGDIAPI UINT WINAPI SetTextAlign(HDC dc, UINT align)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETTEXTALIGN, META_SETTEXTALIGN, align), GDI_ERROR, TRUE);
    return set_dword(dc, MIME_DCDW_TEXTALIGN, align, GDI_ERROR);
}

WINGDIAPI UINT WINAPI GetTextAlign(HDC dc)
{
    return get_dword(dc, MIME_DCDW_TEXTALIGN, GDI_ERROR);
}

WINGDIAPI COLORREF WINAPI SetDCBrushColor(HDC dc, COLORREF color)
{
    return set_dword(dc, MIME_DCDW_BRUSHCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetDCBrushColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_BRUSHCOLOR, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI SetDCPenColor(HDC dc, COLORREF color)
{
    return set_dword(dc, MIME_DCDW_PENCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetDCPenColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_PENCOLOR, CLR_INVALID);
}

WINGDIAPI int WINAPI SetROP2(HDC dc, int rop)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETROP2, META_SETROP2, (DWORD)rop), 0, TRUE);
    return (int)set_dword(dc, MIME_DCDW_ROP2, (DWORD)rop, 0);
}

WINGDIAPI int WINAPI GetROP2(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_ROP2, 0);
}

WINGDIAPI int WINAPI SetStretchBltMode(HDC dc, int mode)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETSTRETCHBLTMODE, META_SETSTRETCHBLTMODE, (DWORD)mode), 0, TRUE);
    return (int)set_dword(dc, MIME_DCDW_STRETCHMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetStretchBltMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_STRETCHMODE, 0);
}

WINGDIAPI int WINAPI SetGraphicsMode(HDC dc, int mode)
{
    return (int)set_dword(dc, MIME_DCDW_GRAPHICSMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetGraphicsMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_GRAPHICSMODE, 0);
}

WINGDIAPI int WINAPI SetMapMode(HDC dc, int mode)
{
    MF_RECORD(dc, MfSetMapMode(dc, mode), 0, TRUE);
    return (int)set_dword(dc, MIME_DCDW_MAPMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetMapMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_MAPMODE, 0);
}

WINGDIAPI DWORD WINAPI SetLayout(HDC dc, DWORD layout)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETLAYOUT, META_SETLAYOUT, layout), GDI_ERROR, TRUE);
    return set_dword(dc, MIME_DCDW_LAYOUT, layout, GDI_ERROR);
}

WINGDIAPI DWORD WINAPI GetLayout(HDC dc)
{
    return get_dword(dc, MIME_DCDW_LAYOUT, GDI_ERROR);
}

WINGDIAPI int WINAPI SetPolyFillMode(HDC dc, int mode)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETPOLYFILLMODE, META_SETPOLYFILLMODE, (DWORD)mode), 0, TRUE);
    return (int)set_dword(dc, MIME_DCDW_POLYFILLMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetPolyFillMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_POLYFILLMODE, 0);
}

WINGDIAPI int WINAPI SetArcDirection(HDC dc, int direction)
{
    return (int)set_dword(dc, MIME_DCDW_ARCDIRECTION, (DWORD)direction, 0);
}

WINGDIAPI int WINAPI GetArcDirection(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_ARCDIRECTION, 0);
}

WINGDIAPI int WINAPI SetTextCharacterExtra(HDC dc, int extra)
{
    MF_RECORD(dc, MfSetValue(dc, 0, META_SETTEXTCHAREXTRA, (DWORD)extra), 0x80000000u, TRUE);
    return (int)set_dword(dc, MIME_DCDW_TEXTCHAREXTRA, (DWORD)extra, 0x80000000u);
}

WINGDIAPI int WINAPI GetTextCharacterExtra(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_TEXTCHAREXTRA, 0x80000000u);
}

WINGDIAPI BOOL WINAPI SetBrushOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    if (MfActive(dc) && !MfIsWmfDc(dc) && !MfSetPoint(dc, EMR_SETBRUSHORGEX, 0, x, y))
        return FALSE;
    return GdiSwapBrushOrigin(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI GdiFlush(void)
{
    return NtGdiFlush();
}

WINGDIAPI DWORD WINAPI GdiSetBatchLimit(DWORD limit)
{
    return 1;
}

/* ------------------------------------------------------------------ coordinates */

WINGDIAPI BOOL WINAPI SetViewportOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_SETVIEWPORTORGEX, META_SETVIEWPORTORG, x, y), FALSE, TRUE);
    return NtGdiSetViewportOrgEx(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI GetViewportOrgEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_VIEWPORTORG, p);
}

WINGDIAPI BOOL WINAPI SetWindowOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_SETWINDOWORGEX, META_SETWINDOWORG, x, y), FALSE, TRUE);
    return NtGdiSetWindowOrgEx(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI GetWindowOrgEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_WINDOWORG, p);
}

WINGDIAPI BOOL WINAPI OffsetViewportOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    MF_RECORD(dc, MfOffsetOrg(dc, TRUE, x, y), FALSE, TRUE);
    POINT p;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_VIEWPORTORG, &p))
        return FALSE;
    if (old)
        *old = p;
    return NtGdiSetViewportOrgEx(dc, p.x + x, p.y + y, nullptr);
}

WINGDIAPI BOOL WINAPI OffsetWindowOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    MF_RECORD(dc, MfOffsetOrg(dc, FALSE, x, y), FALSE, TRUE);
    POINT p;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_WINDOWORG, &p))
        return FALSE;
    if (old)
        *old = p;
    return NtGdiSetWindowOrgEx(dc, p.x + x, p.y + y, nullptr);
}

WINGDIAPI BOOL WINAPI GetCurrentPositionEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_CURRENTPOSITION, p);
}

WINGDIAPI BOOL WINAPI GetDCOrgEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_DCORG, p);
}

WINGDIAPI BOOL WINAPI LPtoDP(HDC dc, LPPOINT points, int count)
{
    return NtGdiTransformPoints(dc, points, points, count, 1);
}

WINGDIAPI BOOL WINAPI DPtoLP(HDC dc, LPPOINT points, int count)
{
    return NtGdiTransformPoints(dc, points, points, count, 0);
}

/* the extents only change in the isotropic and anisotropic modes; zero fails there. */
WINGDIAPI BOOL WINAPI SetViewportExtEx(HDC dc, int x, int y, LPSIZE old)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_SETVIEWPORTEXTEX, META_SETVIEWPORTEXT, x, y), FALSE, TRUE);
    return NtGdiMimeSetExtent(dc, MIME_EXTENT_VIEWPORT, x, y, old);
}

WINGDIAPI BOOL WINAPI SetWindowExtEx(HDC dc, int x, int y, LPSIZE old)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_SETWINDOWEXTEX, META_SETWINDOWEXT, x, y), FALSE, TRUE);
    return NtGdiMimeSetExtent(dc, MIME_EXTENT_WINDOW, x, y, old);
}

static BOOL dc_valid(HDC dc)
{
    DWORD v;
    if (NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v))
        return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

static BOOL get_extent(HDC dc, UINT which, LPSIZE size)
{
    POINT p;
    if (!NtGdiGetDCPoint(dc, which, &p)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (size)
        *size = (SIZE){p.x, p.y};
    return TRUE;
}

WINGDIAPI BOOL WINAPI GetWindowExtEx(HDC dc, LPSIZE size)
{
    return get_extent(dc, MIME_DCPT_WINDOWEXT, size);
}

WINGDIAPI BOOL WINAPI GetViewportExtEx(HDC dc, LPSIZE size)
{
    return get_extent(dc, MIME_DCPT_VIEWPORTEXT, size);
}

/* the fixed mapping modes keep their extents. */
static BOOL scale_extent(HDC dc, BOOL viewport, int x_num, int x_denom, int y_num, int y_denom, LPSIZE old)
{
    if (!x_num || !x_denom || !y_num || !y_denom) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MF_RECORD(dc, MfScaleExt(dc, viewport, x_num, x_denom, y_num, y_denom), FALSE, TRUE);
    SIZE ext;
    if (!get_extent(dc, viewport ? MIME_DCPT_VIEWPORTEXT : MIME_DCPT_WINDOWEXT, &ext))
        return FALSE;
    if (old)
        *old = ext;
    if (GetMapMode(dc) < MM_ISOTROPIC)
        return TRUE;
    return NtGdiMimeSetExtent(dc, viewport ? MIME_EXTENT_VIEWPORT : MIME_EXTENT_WINDOW,
                              MulDiv(ext.cx, x_num, x_denom), MulDiv(ext.cy, y_num, y_denom), nullptr);
}

WINGDIAPI BOOL WINAPI ScaleViewportExtEx(HDC dc, int x_num, int x_denom, int y_num, int y_denom, LPSIZE old)
{
    return scale_extent(dc, TRUE, x_num, x_denom, y_num, y_denom, old);
}

WINGDIAPI BOOL WINAPI ScaleWindowExtEx(HDC dc, int x_num, int x_denom, int y_num, int y_denom, LPSIZE old)
{
    return scale_extent(dc, FALSE, x_num, x_denom, y_num, y_denom, old);
}

/* text justification, font mapper flags, color adjustment and bounds accumulation have no effect yet. */
WINGDIAPI BOOL WINAPI SetTextJustification(HDC dc, int extra, int breaks)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_SETTEXTJUSTIFICATION, META_SETTEXTJUSTIFICATION, extra, breaks), FALSE, TRUE);
    return dc_valid(dc);
}

WINGDIAPI DWORD WINAPI SetMapperFlags(HDC dc, DWORD flags)
{
    MF_RECORD(dc, MfSetValue(dc, EMR_SETMAPPERFLAGS, META_SETMAPPERFLAGS, flags), GDI_ERROR, TRUE);
    return dc_valid(dc) ? 0 : GDI_ERROR;
}

WINGDIAPI UINT WINAPI GetBoundsRect(HDC dc, LPRECT rect, UINT flags)
{
    UINT result;
    if (MfDcCount && MfGetBounds(dc, rect, flags, &result))
        return result;
    if (!dc_valid(dc))
        return 0;
    if (rect)
        *rect = (RECT){0, 0, 0, 0};
    return DCB_RESET;
}

/* metafile dcs accumulate the bounds of what they record; other dcs accumulate nothing. */
WINGDIAPI UINT WINAPI SetBoundsRect(HDC dc, const RECT *rect, UINT flags)
{
    RECT old;
    UINT result;
    if (MfDcCount && MfGetBounds(dc, &old, 0, &result)) {
        struct EMF_DC *emf = EmfDc(dc);
        MfSetBounds(emf, rect, flags);
        return result;
    }
    if (!dc_valid(dc))
        return 0;
    return DCB_RESET | DCB_DISABLE;
}

/* stops a metafile playback on the dc; nothing else is ever pending. */
WINGDIAPI BOOL WINAPI CancelDC(HDC dc)
{
    if (MfIsWmfDc(dc))
        return FALSE;
    EmfCancelPlay(dc);
    DWORD v;
    return MfActive(dc) || NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v);
}

/*
 * without an associated profile, windows reports the color directory's srgb
 * profile for devices of more than 256 colors.
 */
WINGDIAPI BOOL WINAPI GetICMProfileW(HDC dc, LPDWORD size, LPWSTR file)
{
    DWORD v;
    if (!size || !NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    static const WCHAR tail[] = L"\\spool\\drivers\\color\\sRGB Color Space Profile.icm";
    if (!n || n + sizeof(tail) / sizeof(WCHAR) > MAX_PATH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(path + n, tail, sizeof(tail));
    DWORD need = n + sizeof(tail) / sizeof(WCHAR);
    if (*size < need) {
        *size = need;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!file) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(file, path, need * sizeof(WCHAR));
    *size = need;
    return TRUE;
}

WINGDIAPI BOOL WINAPI GetICMProfileA(HDC dc, LPDWORD size, LPSTR file)
{
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    DWORD chars = MAX_PATH;
    if (!GetICMProfileW(dc, &chars, path))
        return FALSE;
    int need = WideCharToMultiByte(CP_ACP, 0, path, (int)chars, nullptr, 0, nullptr, nullptr);
    if (!file || *size < (DWORD)need) {
        *size = (DWORD)need;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, path, (int)chars, file, need, nullptr, nullptr);
    *size = (DWORD)need;
    return TRUE;
}

/* ------------------------------------------------------------------ bitmaps */

WINGDIAPI HBITMAP WINAPI CreateCompatibleBitmap(HDC dc, int width, int height)
{
    return NtGdiCreateCompatibleBitmap(dc, width, height);
}

WINGDIAPI HBITMAP WINAPI CreateBitmap(int width, int height, UINT planes, UINT bpp, const void *bits)
{
    return NtGdiCreateBitmap(width, height, planes, bpp, bits);
}

WINGDIAPI HBITMAP WINAPI CreateBitmapIndirect(const BITMAP *bm)
{
    return NtGdiCreateBitmap(bm->bmWidth, bm->bmHeight, bm->bmPlanes, bm->bmBitsPixel, bm->bmBits);
}

/* a dc drawing into caller memory, as ddraw surfaces use. */
WINGDIAPI NTSTATUS APIENTRY D3DKMTCreateDCFromMemory(D3DKMT_CREATEDCFROMMEMORY *desc)
{
    return NtGdiDdDDICreateDCFromMemory(desc);
}

WINGDIAPI NTSTATUS APIENTRY D3DKMTDestroyDCFromMemory(const D3DKMT_DESTROYDCFROMMEMORY *desc)
{
    return NtGdiDdDDIDestroyDCFromMemory(desc);
}

/*
 * mime has no gpu scheduler, so every process runs in the normal class. a request for another is accepted and kept so
 * that a query answers what was asked.
 */
static volatile LONG g_gpu_priority_class = 1; /* D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL */

WINGDIAPI NTSTATUS APIENTRY D3DKMTSetProcessSchedulingPriorityClass(HANDLE process, LONG priority_class)
{
    /* classes run from idle, 0, to realtime, 5. */
    if (!process || priority_class < 0 || priority_class > 5)
        return (NTSTATUS)0xC000000D; /* STATUS_INVALID_PARAMETER */
    /* only the current process's class is kept, which is all a program asking for its own needs. */
    if (process == (HANDLE)(LONG_PTR)-1)
        g_gpu_priority_class = priority_class;
    return 0;
}

WINGDIAPI NTSTATUS APIENTRY D3DKMTGetProcessSchedulingPriorityClass(HANDLE process, LONG *priority_class)
{
    if (!process || !priority_class)
        return (NTSTATUS)0xC000000D;
    *priority_class = process == (HANDLE)(LONG_PTR)-1 ? g_gpu_priority_class : 1;
    return 0;
}

/* ------------------------------------------------------------------ gamma */

/* windows maps a display dc to its monitor; printers and metafiles have no ramp. */
static BOOL display_dc(HDC dc)
{
    return GetObjectType(dc) == OBJ_DC && GetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY;
}

/* how far each entry's high byte may stray from its index, read once like windows. */
static LONG gamma_range(void)
{
    static LONG range = -1;
    if (range < 0) {
        DWORD value, size = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM",
                         L"GdiIcmGammaRange", RRF_RT_REG_DWORD, nullptr, &value, &size) ||
            value > 0x7FFFFFFF)
            value = 128;
        range = (LONG)value;
    }
    return range;
}

WINGDIAPI BOOL WINAPI SetDeviceGammaRamp(HDC dc, LPVOID ramp)
{
    if (!ramp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!dc || !display_dc(dc))
        return FALSE;
    /* the ramp need not be monotonic, and the low bytes are free. */
    const WORD *values = ramp;
    LONG range = gamma_range();
    for (LONG i = 0; i < 3 * 256; i++) {
        LONG high = values[i] >> 8, index = i & 255;
        if (high < index - range || high > index + range)
            return FALSE;
    }
    return NT_SUCCESS(NtGdiMimeGamma(MIME_GAMMA_SET, (PUSHORT)ramp));
}

/*
 * windows keeps a count of display setting changes in the shared gdi memory and exports it unnamed to its own
 * components: direct3d programs, WPF among them, ask it with GetProcAddress to know when to make their devices again.
 */
WINGDIAPI DWORD WINAPI GdiEntry13(void)
{
    return (DWORD)NtUserCallTwoParam(0, 0, MIME_TWOPARAM_DISPLAYUNIQUENESS);
}

/* gdi's ramp, not what a fullscreen program shows over it. */
WINGDIAPI BOOL WINAPI GetDeviceGammaRamp(HDC dc, LPVOID ramp)
{
    if (!ramp) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!dc)
        return FALSE;
    if (!display_dc(dc) || !NT_SUCCESS(NtGdiMimeGamma(MIME_GAMMA_GET, ramp))) {
        memset(ramp, 0, 3 * 256 * sizeof(WORD));
        return FALSE;
    }
    return TRUE;
}

WINGDIAPI HBITMAP WINAPI CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section,
                                          DWORD offset)
{
    void *dummy;
    return NtGdiCreateDIBSection(dc, section, offset, info, usage, info ? info->bmiHeader.biSize : 0, 0, 0,
                                 bits ? bits : &dummy);
}

DWORD GdiDibBitsSize(const BITMAPINFOHEADER *h)
{
    UINT stride = (UINT)(((h->biWidth < 0 ? -h->biWidth : h->biWidth) * h->biBitCount + 31) / 32 * 4);
    return stride * (UINT)(h->biHeight < 0 ? -h->biHeight : h->biHeight);
}

WINGDIAPI int WINAPI SetDIBitsToDevice(HDC dc, int x, int y, DWORD width, DWORD height, int src_x, int src_y,
                                       UINT start, UINT lines, const void *bits, const BITMAPINFO *info, UINT usage)
{
    if (MfActive(dc))
        return MfSetDIBitsToDevice(dc, x, y, width, height, src_x, src_y, start, lines, bits, info, usage) ? (int)lines
                                                                                                           : 0;
    return NtGdiSetDIBitsToDeviceInternal(dc, x, y, width, height, src_x, src_y, start, lines, bits, info, usage,
                                          GdiDibBitsSize(&info->bmiHeader), info->bmiHeader.biSize + 256 * 4, TRUE,
                                          nullptr);
}

WINGDIAPI int WINAPI StretchDIBits(HDC dc, int x, int y, int width, int height, int src_x, int src_y, int src_width,
                                   int src_height, const void *bits, const BITMAPINFO *info, UINT usage, DWORD rop)
{
    if (MfActive(dc)) {
        if (!MfStretchDIBits(dc, x, y, width, height, src_x, src_y, src_width, src_height, bits, info, usage, rop))
            return 0;
        LONG h = info ? info->bmiHeader.biHeight : height;
        return h < 0 ? -h : h ? h : 1;
    }
    return NtGdiStretchDIBitsInternal(dc, x, y, width, height, src_x, src_y, src_width, src_height, bits, info, usage,
                                      rop, info->bmiHeader.biSize + 256 * 4, GdiDibBitsSize(&info->bmiHeader), nullptr);
}

WINGDIAPI int WINAPI GetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, LPVOID bits, LPBITMAPINFO info,
                               UINT usage)
{
    return NtGdiGetDIBitsInternal(dc, bitmap, start, lines, bits, info, usage, bits ? GdiDibBitsSize(&info->bmiHeader) : 0,
                                  info->bmiHeader.biSize + 256 * 4);
}

WINGDIAPI int WINAPI SetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, const void *bits,
                               const BITMAPINFO *info, UINT usage)
{
    HDC mem = CreateCompatibleDC(dc);
    if (!mem)
        return 0;
    HGDIOBJ old = SelectObject(mem, bitmap);
    BITMAP bm;
    GetObjectW(bitmap, sizeof(bm), &bm);
    int r = SetDIBitsToDevice(mem, 0, bm.bmHeight - (int)start - (int)lines, (DWORD)bm.bmWidth, lines, 0, 0, 0,
                              lines, bits, info, usage);
    SelectObject(mem, old);
    DeleteDC(mem);
    return r;
}

WINGDIAPI HBITMAP WINAPI CreateDIBitmap(HDC dc, const BITMAPINFOHEADER *header, DWORD init, const void *bits,
                                        const BITMAPINFO *info, UINT usage)
{
    LONG height = header->biHeight < 0 ? -header->biHeight : header->biHeight;
    HBITMAP bm = dc ? CreateCompatibleBitmap(dc, header->biWidth, height)
                    : CreateBitmap(header->biWidth, height, 1, 1, nullptr);
    if (bm && init == CBM_INIT && bits && info)
        SetDIBits(dc, bm, 0, (UINT)height, bits, info, usage);
    return bm;
}

/* the dimensions an application attaches to a bitmap, in 0.1 mm units. */
#define DIMENSION_MAX 64

static RTL_SRWLOCK g_dimension_lock;
static struct {
    HBITMAP Bitmap;
    SIZE Size;
} g_dimensions[DIMENSION_MAX];

static BOOL bitmap_valid(HBITMAP bitmap)
{
    BITMAP bm;
    if (handle_type(bitmap) == 0x05 && NtGdiExtGetObjectW(bitmap, sizeof(bm), &bm))
        return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINGDIAPI BOOL WINAPI GetBitmapDimensionEx(HBITMAP bitmap, LPSIZE size)
{
    if (!bitmap_valid(bitmap))
        return FALSE;
    SIZE result = {0, 0};
    RtlAcquireSRWLockShared(&g_dimension_lock);
    for (UINT i = 0; i < DIMENSION_MAX; i++)
        if (g_dimensions[i].Bitmap == bitmap)
            result = g_dimensions[i].Size;
    RtlReleaseSRWLockShared(&g_dimension_lock);
    if (size)
        *size = result;
    return TRUE;
}

WINGDIAPI BOOL WINAPI SetBitmapDimensionEx(HBITMAP bitmap, int width, int height, LPSIZE old)
{
    if (!bitmap_valid(bitmap))
        return FALSE;
    SIZE previous = {0, 0};
    int slot = -1;
    RtlAcquireSRWLockExclusive(&g_dimension_lock);
    for (UINT i = 0; i < DIMENSION_MAX; i++) {
        if (g_dimensions[i].Bitmap == bitmap) {
            previous = g_dimensions[i].Size;
            slot = (int)i;
        }
    }
    if (slot < 0) {
        for (UINT i = 0; i < DIMENSION_MAX && slot < 0; i++)
            if (!g_dimensions[i].Bitmap)
                slot = (int)i;
        /* when full, overwrite the first slot. */
        if (slot < 0)
            slot = 0;
    }
    if (width || height) {
        g_dimensions[slot].Bitmap = bitmap;
        g_dimensions[slot].Size = (SIZE){width, height};
    } else if (g_dimensions[slot].Bitmap == bitmap) {
        g_dimensions[slot].Bitmap = nullptr;
    }
    RtlReleaseSRWLockExclusive(&g_dimension_lock);
    if (old)
        *old = previous;
    return TRUE;
}

WINGDIAPI BOOL WINAPI BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y, DWORD rop)
{
    if (MfActive(dst))
        return MfBitBlt(dst, x, y, width, height, src, src_x, src_y, width, height, rop, EMR_BITBLT);
    return NtGdiBitBlt(dst, x, y, width, height, src, src_x, src_y, rop, 0, 0);
}

WINGDIAPI BOOL WINAPI StretchBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                 int src_width, int src_height, DWORD rop)
{
    if (MfActive(dst))
        return MfBitBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, rop, EMR_STRETCHBLT);
    return NtGdiStretchBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, rop, 0);
}

WINGDIAPI BOOL WINAPI PatBlt(HDC dc, int x, int y, int width, int height, DWORD rop)
{
    if (MfActive(dc))
        return MfBitBlt(dc, x, y, width, height, nullptr, 0, 0, 0, 0, rop, EMR_BITBLT);
    return NtGdiPatBlt(dc, x, y, width, height, rop);
}

WINGDIAPI BOOL WINAPI GdiAlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                    int src_width, int src_height, BLENDFUNCTION blend)
{
    if (MfActive(dst)) {
        DWORD rop;
        memcpy(&rop, &blend, sizeof(rop));
        return MfBitBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, rop, EMR_ALPHABLEND);
    }
    return NtGdiAlphaBlend(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, blend, nullptr);
}

/* ------------------------------------------------------------------ brushes and pens */

WINGDIAPI HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
    return NtGdiCreateSolidBrush(color, nullptr);
}

WINGDIAPI HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH *lb)
{
    if (lb->lbStyle == BS_NULL)
        return GetStockObject(NULL_BRUSH);
    return NtGdiCreateSolidBrush(lb->lbColor, nullptr);
}

WINGDIAPI HBRUSH WINAPI CreateHatchBrush(int hatch, COLORREF color)
{
    return NtGdiCreateSolidBrush(color, nullptr);
}

#define PATTERN_SAMPLE 8

DWORD GdiDibTableSize(const BITMAPINFOHEADER *h, UINT usage)
{
    DWORD colors = h->biClrUsed;
    if (!colors && h->biBitCount <= 8)
        colors = 1u << h->biBitCount;
    DWORD size = colors * (usage == DIB_PAL_COLORS ? sizeof(WORD) : sizeof(RGBQUAD));
    if (h->biCompression == BI_BITFIELDS && h->biSize == sizeof(BITMAPINFOHEADER))
        size += 3 * sizeof(DWORD);
    return size;
}

/*
 * win32k has no pattern brushes yet: a pattern becomes a solid brush of its
 * average color, sampled from an 8 by 8 reduction of the bitmap or dib.
 */
static BOOL average_pattern(HBITMAP bitmap, const BITMAPINFO *info, UINT usage, COLORREF *color)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = PATTERN_SAMPLE;
    bi.bmiHeader.biHeight = -PATTERN_SAMPLE;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = nullptr;
    HDC mem = CreateCompatibleDC(nullptr);
    HBITMAP canvas = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0) : nullptr;
    BOOL ok = FALSE;
    if (canvas && pixels) {
        HGDIOBJ old = SelectObject(mem, canvas);
        SetStretchBltMode(mem, STRETCH_DELETESCANS);
        if (bitmap) {
            BITMAP bm;
            HDC src = CreateCompatibleDC(nullptr);
            if (src && GetObjectW(bitmap, sizeof(bm), &bm)) {
                HGDIOBJ old_src = SelectObject(src, bitmap);
                ok = old_src && StretchBlt(mem, 0, 0, PATTERN_SAMPLE, PATTERN_SAMPLE, src, 0, 0, bm.bmWidth,
                                           bm.bmHeight, SRCCOPY);
                if (old_src)
                    SelectObject(src, old_src);
            }
            if (src)
                DeleteDC(src);
        } else {
            const BITMAPINFOHEADER *h = &info->bmiHeader;
            const BYTE *bits = (const BYTE *)info + h->biSize + GdiDibTableSize(h, usage);
            ok = StretchDIBits(mem, 0, 0, PATTERN_SAMPLE, PATTERN_SAMPLE, 0, 0, h->biWidth,
                               h->biHeight < 0 ? -h->biHeight : h->biHeight, bits, info, usage, SRCCOPY) != 0;
        }
        if (ok) {
            GdiFlush();
            DWORD r = 0, g = 0, b = 0;
            for (int i = 0; i < PATTERN_SAMPLE * PATTERN_SAMPLE; i++) {
                r += (pixels[i] >> 16) & 0xFF;
                g += (pixels[i] >> 8) & 0xFF;
                b += pixels[i] & 0xFF;
            }
            *color = RGB(r / (PATTERN_SAMPLE * PATTERN_SAMPLE), g / (PATTERN_SAMPLE * PATTERN_SAMPLE),
                         b / (PATTERN_SAMPLE * PATTERN_SAMPLE));
        }
        SelectObject(mem, old);
    }
    if (canvas)
        DeleteObject(canvas);
    if (mem)
        DeleteDC(mem);
    return ok;
}

WINGDIAPI HBRUSH WINAPI CreatePatternBrush(HBITMAP bitmap)
{
    BITMAP bm;
    if (handle_type(bitmap) != 0x05 || !NtGdiExtGetObjectW(bitmap, sizeof(bm), &bm)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    COLORREF color;
    if (!average_pattern(bitmap, nullptr, DIB_RGB_COLORS, &color))
        color = RGB(128, 128, 128);
    return NtGdiCreateSolidBrush(color, nullptr);
}

WINGDIAPI HBRUSH WINAPI CreateDIBPatternBrushPt(const void *packed, UINT usage)
{
    const BITMAPINFO *info = packed;
    if (!info || info->bmiHeader.biSize < sizeof(BITMAPINFOHEADER) || info->bmiHeader.biWidth <= 0 ||
        !info->bmiHeader.biHeight || usage > DIB_PAL_COLORS) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    COLORREF color;
    if (!average_pattern(nullptr, info, usage, &color))
        color = RGB(128, 128, 128);
    return NtGdiCreateSolidBrush(color, nullptr);
}

WINGDIAPI HPEN WINAPI CreatePen(int style, int width, COLORREF color)
{
    return NtGdiCreatePen(style, width, color, nullptr);
}

WINGDIAPI HPEN WINAPI CreatePenIndirect(const LOGPEN *lp)
{
    return NtGdiCreatePen((int)lp->lopnStyle, lp->lopnWidth.x, lp->lopnColor, nullptr);
}

/* the pen keeps its type, end cap and join; brush patterns become their color. */
WINGDIAPI HPEN WINAPI ExtCreatePen(DWORD style, DWORD width, const LOGBRUSH *lb, DWORD count, const DWORD *styles)
{
    if (!lb || ((style & PS_STYLE_MASK) == PS_USERSTYLE) != (count != 0) || (count && !styles) || count > 16) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return NtGdiExtCreatePen(style, width, lb->lbStyle, lb->lbColor, lb->lbHatch, count, styles);
}

/* ------------------------------------------------------------------ fonts and text */

void GdiAskForFont(const LOGFONTW *lf);
void GdiAskForText(const WCHAR *text, int count);

WINGDIAPI HFONT WINAPI CreateFontIndirectW(const LOGFONTW *lf)
{
    if (!lf)
        return nullptr;
    GdiAskForFont(lf);
    return NtGdiHfontCreate(lf, sizeof(*lf), 0, 0, nullptr);
}

static void logfont_a_to_w(const LOGFONTA *a, LOGFONTW *w)
{
    memcpy(w, a, offsetof(LOGFONTA, lfFaceName));
    MultiByteToWideChar(CP_ACP, 0, a->lfFaceName, -1, w->lfFaceName, LF_FACESIZE);
    w->lfFaceName[LF_FACESIZE - 1] = 0;
}

WINGDIAPI HFONT WINAPI CreateFontIndirectA(const LOGFONTA *lf)
{
    if (!lf)
        return nullptr;
    LOGFONTW w;
    logfont_a_to_w(lf, &w);
    return CreateFontIndirectW(&w);
}

WINGDIAPI HFONT WINAPI CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                                   DWORD underline, DWORD strikeout, DWORD charset, DWORD out_precision,
                                   DWORD clip_precision, DWORD quality, DWORD pitch, LPCWSTR face)
{
    LOGFONTW lf = {height, width, escapement, orientation, weight, (BYTE)italic, (BYTE)underline, (BYTE)strikeout,
                   (BYTE)charset, (BYTE)out_precision, (BYTE)clip_precision, (BYTE)quality, (BYTE)pitch, {0}};
    if (face) {
        int i = 0;
        for (; i < LF_FACESIZE - 1 && face[i]; i++)
            lf.lfFaceName[i] = face[i];
        lf.lfFaceName[i] = 0;
    }
    return CreateFontIndirectW(&lf);
}

WINGDIAPI HFONT WINAPI CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                                   DWORD underline, DWORD strikeout, DWORD charset, DWORD out_precision,
                                   DWORD clip_precision, DWORD quality, DWORD pitch, LPCSTR face)
{
    WCHAR wface[LF_FACESIZE] = {};
    if (face)
        MultiByteToWideChar(CP_ACP, 0, face, -1, wface, LF_FACESIZE);
    wface[LF_FACESIZE - 1] = 0;
    return CreateFontW(height, width, escapement, orientation, weight, italic, underline, strikeout, charset,
                       out_precision, clip_precision, quality, pitch, face ? wface : nullptr);
}

WINGDIAPI BOOL WINAPI ExtTextOutW(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count,
                                  const INT *dx)
{
    if (MfActive(dc))
        return MfTextOut(dc, x, y, options, rect, text, count, dx);
    if (!(options & ETO_GLYPH_INDEX))
        GdiAskForText(text, (int)count);
    return NtGdiExtTextOutW(dc, x, y, options, rect, text, (int)count, dx, 0);
}

WINGDIAPI BOOL WINAPI TextOutW(HDC dc, int x, int y, LPCWSTR text, int count)
{
    if (MfActive(dc))
        return count >= 0 && MfTextOut(dc, x, y, 0, nullptr, text, (UINT)count, nullptr);
    GdiAskForText(text, count);
    return NtGdiExtTextOutW(dc, x, y, 0, nullptr, text, count, nullptr, 0);
}

/* converts an ansi string to a heap copy; the caller frees it. */
static LPWSTR to_wide(LPCSTR s, int count, int *out_count)
{
    if (count < 0)
        count = s ? (int)strlen(s) : 0;
    int n = count ? MultiByteToWideChar(CP_ACP, 0, s, count, nullptr, 0) : 0;
    LPWSTR w = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return nullptr;
    if (n)
        MultiByteToWideChar(CP_ACP, 0, s, count, w, n);
    w[n] = 0;
    *out_count = n;
    return w;
}

static void free_wide(LPWSTR w)
{
    if (w)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, w);
}

WINGDIAPI BOOL WINAPI ExtTextOutA(HDC dc, int x, int y, UINT options, const RECT *rect, LPCSTR text, UINT count,
                                  const INT *dx)
{
    if (MfActive(dc))
        return MfTextOutA(dc, x, y, options, rect, text, count, dx);
    int n;
    LPWSTR w = to_wide(text, (int)count, &n);
    if (!w)
        return FALSE;
    if (!(options & ETO_GLYPH_INDEX))
        GdiAskForText(w, n);
    BOOL ok = NtGdiExtTextOutW(dc, x, y, options, rect, w, n, (dx && (int)count == n) ? dx : nullptr, 0);
    free_wide(w);
    return ok;
}

WINGDIAPI BOOL WINAPI TextOutA(HDC dc, int x, int y, LPCSTR text, int count)
{
    return ExtTextOutA(dc, x, y, 0, nullptr, text, (UINT)count, nullptr);
}

WINGDIAPI BOOL WINAPI GetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int max_extent, LPINT fit, LPINT dx,
                                            LPSIZE size)
{
    GdiAskForText(text, count);
    return NtGdiGetTextExtentExW(dc, text, (ULONG)count, (ULONG)max_extent, (ULONG *)fit, (ULONG *)dx, size, 0);
}

WINGDIAPI BOOL WINAPI GetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size)
{
    GdiAskForText(text, count);
    return NtGdiGetTextExtentExW(dc, text, (ULONG)count, 0, nullptr, nullptr, size, 0);
}

WINGDIAPI BOOL WINAPI GetTextExtentPointW(HDC dc, LPCWSTR text, int count, LPSIZE size)
{
    return GetTextExtentPoint32W(dc, text, count, size);
}

WINGDIAPI BOOL WINAPI GetTextExtentPoint32A(HDC dc, LPCSTR text, int count, LPSIZE size)
{
    int n;
    LPWSTR w = to_wide(text, count, &n);
    if (!w)
        return FALSE;
    BOOL ok = GetTextExtentPoint32W(dc, w, n, size);
    free_wide(w);
    return ok;
}

WINGDIAPI BOOL WINAPI GetTextExtentPointA(HDC dc, LPCSTR text, int count, LPSIZE size)
{
    return GetTextExtentPoint32A(dc, text, count, size);
}

WINGDIAPI BOOL WINAPI GetTextMetricsW(HDC dc, LPTEXTMETRICW tm)
{
    return NtGdiGetTextMetricsW(dc, tm, sizeof(*tm));
}

WINGDIAPI BOOL WINAPI GetTextMetricsA(HDC dc, LPTEXTMETRICA tm)
{
    TEXTMETRICW w;
    if (!NtGdiGetTextMetricsW(dc, &w, sizeof(w)))
        return FALSE;
    tm->tmHeight = w.tmHeight;
    tm->tmAscent = w.tmAscent;
    tm->tmDescent = w.tmDescent;
    tm->tmInternalLeading = w.tmInternalLeading;
    tm->tmExternalLeading = w.tmExternalLeading;
    tm->tmAveCharWidth = w.tmAveCharWidth;
    tm->tmMaxCharWidth = w.tmMaxCharWidth;
    tm->tmWeight = w.tmWeight;
    tm->tmOverhang = w.tmOverhang;
    tm->tmDigitizedAspectX = w.tmDigitizedAspectX;
    tm->tmDigitizedAspectY = w.tmDigitizedAspectY;
    tm->tmFirstChar = w.tmFirstChar > 0xFF ? 0xFF : (BYTE)w.tmFirstChar;
    tm->tmLastChar = w.tmLastChar > 0xFF ? 0xFF : (BYTE)w.tmLastChar;
    tm->tmDefaultChar = w.tmDefaultChar > 0xFF ? '?' : (BYTE)w.tmDefaultChar;
    tm->tmBreakChar = w.tmBreakChar > 0xFF ? ' ' : (BYTE)w.tmBreakChar;
    tm->tmItalic = w.tmItalic;
    tm->tmUnderlined = w.tmUnderlined;
    tm->tmStruckOut = w.tmStruckOut;
    tm->tmPitchAndFamily = w.tmPitchAndFamily;
    tm->tmCharSet = w.tmCharSet;
    return TRUE;
}

WINGDIAPI int WINAPI GetTextFaceW(HDC dc, int count, LPWSTR name)
{
    return NtGdiGetTextFaceW(dc, count, name, FALSE);
}

WINGDIAPI int WINAPI GetTextFaceA(HDC dc, int count, LPSTR name)
{
    WCHAR w[LF_FACESIZE];
    int n = NtGdiGetTextFaceW(dc, LF_FACESIZE, w, FALSE);
    if (!n)
        return 0;
    if (!name)
        return n;
    return WideCharToMultiByte(CP_ACP, 0, w, -1, name, count, nullptr, nullptr);
}

WINGDIAPI BOOL WINAPI GetCharWidth32W(HDC dc, UINT first, UINT last, LPINT widths)
{
    return last >= first && NtGdiGetCharWidthW(dc, first, last - first + 1, nullptr, 0, widths);
}

WINGDIAPI BOOL WINAPI GetCharWidthW(HDC dc, UINT first, UINT last, LPINT widths)
{
    return GetCharWidth32W(dc, first, last, widths);
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsW(HDC dc, UINT first, UINT last, LPABC abc)
{
    return last >= first && NtGdiGetCharABCWidthsW(dc, first, last - first + 1, nullptr, 0, abc);
}

WINGDIAPI DWORD WINAPI GetFontLanguageInfo(HDC dc)
{
    return 0;
}

WINGDIAPI int WINAPI GetTextCharset(HDC dc)
{
    return DEFAULT_CHARSET;
}

WINGDIAPI int WINAPI GetTextCharsetInfo(HDC dc, LPVOID sig, DWORD flags)
{
    return DEFAULT_CHARSET;
}

/* the dialog base unit algorithm: average width of a..z and A..Z, rounded, and the font height. */
WINGDIAPI LONG WINAPI GdiGetCharDimensions(HDC dc, LPTEXTMETRICW tm, LONG *height)
{
    static const WCHAR alphabet[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    TEXTMETRICW local;
    if (!tm)
        tm = &local;
    if (!GetTextMetricsW(dc, tm))
        return 0;
    SIZE size;
    if (!GetTextExtentPointW(dc, alphabet, 52, &size))
        return 0;
    if (height)
        *height = tm->tmHeight;
    return (size.cx / 26 + 1) / 2;
}

/* ------------------------------------------------------------------ lines and shapes */

WINGDIAPI BOOL WINAPI MoveToEx(HDC dc, int x, int y, LPPOINT old)
{
    MF_RECORD(dc, MfMoveTo(dc, x, y), FALSE, TRUE);
    return NtGdiMoveTo(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI LineTo(HDC dc, int x, int y)
{
    MF_RECORD(dc, MfLineTo(dc, x, y), FALSE, TRUE);
    return NtGdiLineTo(dc, x, y);
}

WINGDIAPI BOOL WINAPI Rectangle(HDC dc, int left, int top, int right, int bottom)
{
    MF_RECORD(dc, MfBox(dc, EMR_RECTANGLE, META_RECTANGLE, left, top, right, bottom), FALSE, TRUE);
    return NtGdiRectangle(dc, left, top, right, bottom);
}

WINGDIAPI BOOL WINAPI RoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height)
{
    MF_RECORD(dc, MfRoundRect(dc, left, top, right, bottom, width, height), FALSE, TRUE);
    return NtGdiRoundRect(dc, left, top, right, bottom, width, height);
}

WINGDIAPI BOOL WINAPI Ellipse(HDC dc, int left, int top, int right, int bottom)
{
    MF_RECORD(dc, MfBox(dc, EMR_ELLIPSE, META_ELLIPSE, left, top, right, bottom), FALSE, TRUE);
    return NtGdiEllipse(dc, left, top, right, bottom);
}

WINGDIAPI BOOL WINAPI Polyline(HDC dc, const POINT *points, int count)
{
    MF_RECORD(dc, count > 0 && points && MfPoly(dc, EMR_POLYLINE, META_POLYLINE, points, (DWORD)count), FALSE, TRUE);
    ULONG n = (ULONG)count;
    return count > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, MIME_POLY_POLYLINE);
}

WINGDIAPI BOOL WINAPI Polygon(HDC dc, const POINT *points, int count)
{
    MF_RECORD(dc, count > 0 && points && MfPoly(dc, EMR_POLYGON, META_POLYGON, points, (DWORD)count), FALSE, TRUE);
    ULONG n = (ULONG)count;
    return count > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, MIME_POLY_POLYGON);
}

WINGDIAPI BOOL WINAPI PolyPolygon(HDC dc, const POINT *points, const INT *counts, int polygons)
{
    MF_RECORD(dc, polygons > 0 && MfPolyPoly(dc, EMR_POLYPOLYGON, META_POLYPOLYGON, points, (const DWORD *)counts,
                                              (DWORD)polygons),
              FALSE, TRUE);
    return polygons > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, (ULONG *)counts, (ULONG)polygons, MIME_POLY_POLYGON);
}

WINGDIAPI BOOL WINAPI PolyPolyline(HDC dc, const POINT *points, const DWORD *counts, DWORD lines)
{
    MF_RECORD(dc, lines && MfPolyPoly(dc, EMR_POLYPOLYLINE, 0, points, counts, lines), FALSE, TRUE);
    return lines && NtGdiPolyPolyDraw(dc, (POINT *)points, (ULONG *)counts, lines, MIME_POLY_POLYLINE);
}

WINGDIAPI COLORREF WINAPI SetPixel(HDC dc, int x, int y, COLORREF color)
{
    if (MfActive(dc))
        return MfSetPixel(dc, x, y, color) ? color : CLR_INVALID;
    return NtGdiSetPixel(dc, x, y, color);
}

WINGDIAPI BOOL WINAPI SetPixelV(HDC dc, int x, int y, COLORREF color)
{
    if (MfActive(dc))
        return MfSetPixel(dc, x, y, color);
    return NtGdiSetPixel(dc, x, y, color) != CLR_INVALID;
}

WINGDIAPI COLORREF WINAPI GetPixel(HDC dc, int x, int y)
{
    return NtGdiGetPixel(dc, x, y);
}

/* metafiles record the fill; any other device context is filled by FloodFillDc. */
WINGDIAPI BOOL WINAPI ExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type)
{
    if (MfActive(dc))
        return MfExtFloodFill(dc, x, y, color, type);
    return FloodFillDc(dc, x, y, color, type);
}

/* ------------------------------------------------------------------ regions and clipping */

WINGDIAPI HRGN WINAPI CreateRectRgn(int left, int top, int right, int bottom)
{
    return NtGdiCreateRectRgn(left, top, right, bottom);
}

WINGDIAPI HRGN WINAPI CreateRectRgnIndirect(const RECT *r)
{
    return NtGdiCreateRectRgn(r->left, r->top, r->right, r->bottom);
}

WINGDIAPI HRGN WINAPI CreateRoundRectRgn(int left, int top, int right, int bottom, int width, int height)
{
    return NtGdiCreateRoundRectRgn(left, top, right, bottom, width, height);
}

WINGDIAPI HRGN WINAPI ExtCreateRegion(const XFORM *xform, DWORD size, const RGNDATA *data)
{
    return NtGdiExtCreateRegion(xform, size, data);
}

WINGDIAPI int WINAPI CombineRgn(HRGN dst, HRGN a, HRGN b, int mode)
{
    return NtGdiCombineRgn(dst, a, b, mode);
}

WINGDIAPI BOOL WINAPI SetRectRgn(HRGN region, int left, int top, int right, int bottom)
{
    return NtGdiSetRectRgn(region, left, top, right, bottom);
}

WINGDIAPI int WINAPI OffsetRgn(HRGN region, int x, int y)
{
    return NtGdiOffsetRgn(region, x, y);
}

WINGDIAPI int WINAPI GetRgnBox(HRGN region, LPRECT rect)
{
    return NtGdiGetRgnBox(region, rect);
}

WINGDIAPI BOOL WINAPI PtInRegion(HRGN region, int x, int y)
{
    return NtGdiPtInRegion(region, x, y);
}

WINGDIAPI BOOL WINAPI RectInRegion(HRGN region, const RECT *rect)
{
    RECT r = *rect;
    return NtGdiRectInRegion(region, &r);
}

WINGDIAPI BOOL WINAPI EqualRgn(HRGN a, HRGN b)
{
    return NtGdiEqualRgn(a, b);
}

WINGDIAPI DWORD WINAPI GetRegionData(HRGN region, DWORD size, LPRGNDATA data)
{
    return NtGdiGetRegionData(region, size, data);
}

WINGDIAPI BOOL WINAPI FillRgn(HDC dc, HRGN region, HBRUSH brush)
{
    if (MfActive(dc))
        return MfRegion(dc, EMR_FILLRGN, META_FILLREGION, region, brush, 0, 0);
    return NtGdiFillRgn(dc, region, brush);
}

WINGDIAPI BOOL WINAPI FrameRgn(HDC dc, HRGN region, HBRUSH brush, int width, int height)
{
    if (MfActive(dc))
        return MfRegion(dc, EMR_FRAMERGN, META_FRAMEREGION, region, brush, width, height);
    return NtGdiFrameRgn(dc, region, brush, width, height);
}

WINGDIAPI BOOL WINAPI InvertRgn(HDC dc, HRGN region)
{
    if (MfActive(dc))
        return MfRegion(dc, EMR_INVERTRGN, META_INVERTREGION, region, nullptr, 0, 0);
    return NtGdiInvertRgn(dc, region);
}

WINGDIAPI BOOL WINAPI PaintRgn(HDC dc, HRGN region)
{
    if (MfActive(dc))
        return MfRegion(dc, EMR_PAINTRGN, META_PAINTREGION, region, nullptr, 0, 0);
    return NtGdiFillRgn(dc, region, NtGdiGetDCObject(dc, OBJ_BRUSH));
}

WINGDIAPI int WINAPI ExtSelectClipRgn(HDC dc, HRGN region, int mode)
{
    MF_RECORD(dc, MfSelectClipRgn(dc, region, mode), ERROR, SIMPLEREGION);
    return NtGdiExtSelectClipRgn(dc, region, mode);
}

WINGDIAPI int WINAPI SelectClipRgn(HDC dc, HRGN region)
{
    MF_RECORD(dc, MfSelectClipRgn(dc, region, RGN_COPY), ERROR, SIMPLEREGION);
    return NtGdiExtSelectClipRgn(dc, region, RGN_COPY);
}

WINGDIAPI int WINAPI GetClipBox(HDC dc, LPRECT rect)
{
    return NtGdiGetAppClipBox(dc, rect);
}

/* GetRandomRgn selectors. */
#define CLIPRGN 1

WINGDIAPI int WINAPI GetClipRgn(HDC dc, HRGN region)
{
    return NtGdiGetRandomRgn(dc, region, CLIPRGN);
}

WINGDIAPI int WINAPI GetRandomRgn(HDC dc, HRGN region, INT which)
{
    return NtGdiGetRandomRgn(dc, region, which);
}

/* the clip region joins the meta region, which later clip regions cannot leave. */
WINGDIAPI int WINAPI SetMetaRgn(HDC dc)
{
    MF_RECORD(dc, MfRecordEmpty(dc, EMR_SETMETARGN, 0), ERROR, ERROR);
    return NtGdiSetMetaRgn(dc);
}

/* 2 is the METARGN selector. */
WINGDIAPI int WINAPI GetMetaRgn(HDC dc, HRGN region)
{
    return NtGdiGetRandomRgn(dc, region, 2);
}

WINGDIAPI int WINAPI IntersectClipRect(HDC dc, int left, int top, int right, int bottom)
{
    MF_RECORD(dc, MfClipRect(dc, EMR_INTERSECTCLIPRECT, META_INTERSECTCLIPRECT, left, top, right, bottom), ERROR,
              SIMPLEREGION);
    return NtGdiIntersectClipRect(dc, left, top, right, bottom);
}

WINGDIAPI int WINAPI ExcludeClipRect(HDC dc, int left, int top, int right, int bottom)
{
    MF_RECORD(dc, MfClipRect(dc, EMR_EXCLUDECLIPRECT, META_EXCLUDECLIPRECT, left, top, right, bottom), ERROR,
              SIMPLEREGION);
    return NtGdiExcludeClipRect(dc, left, top, right, bottom);
}

/* the application clip region in device coordinates, or NULL when the dc has none. */
static HRGN clip_region(HDC dc)
{
    HRGN region = NtGdiCreateRectRgn(0, 0, 0, 0);
    if (region && NtGdiGetRandomRgn(dc, region, CLIPRGN) == 1)
        return region;
    if (region)
        NtGdiDeleteObjectApp(region);
    return nullptr;
}

WINGDIAPI int WINAPI OffsetClipRgn(HDC dc, int x, int y)
{
    MF_RECORD(dc, MfSetPoint(dc, EMR_OFFSETCLIPRGN, META_OFFSETCLIPRGN, x, y), ERROR, SIMPLEREGION);
    if (!dc_valid(dc))
        return ERROR;
    HRGN region = clip_region(dc);
    if (!region)
        return SIMPLEREGION;
    /* the offset is logical, the region device: scale it through the mapping. */
    POINT p[2] = {{0, 0}, {x, y}};
    NtGdiTransformPoints(dc, p, p, 2, 1);
    NtGdiOffsetRgn(region, p[1].x - p[0].x, p[1].y - p[0].y);
    int result = NtGdiExtSelectClipRgn(dc, region, RGN_COPY);
    NtGdiDeleteObjectApp(region);
    return result;
}

/* tests against the clip box, then against the clip region when there is one. */
WINGDIAPI BOOL WINAPI RectVisible(HDC dc, const RECT *rect)
{
    RECT box;
    int kind = rect ? NtGdiGetAppClipBox(dc, &box) : ERROR;
    if (kind == ERROR || kind == NULLREGION)
        return FALSE;
    RECT r = {rect->left < rect->right ? rect->left : rect->right, rect->top < rect->bottom ? rect->top : rect->bottom,
              rect->left < rect->right ? rect->right : rect->left, rect->top < rect->bottom ? rect->bottom : rect->top};
    if (r.left >= box.right || r.right <= box.left || r.top >= box.bottom || r.bottom <= box.top ||
        r.left == r.right || r.top == r.bottom)
        return FALSE;
    HRGN region = kind == COMPLEXREGION ? clip_region(dc) : nullptr;
    if (!region)
        return TRUE;
    POINT p[2] = {{r.left, r.top}, {r.right, r.bottom}};
    NtGdiTransformPoints(dc, p, p, 2, 1);
    RECT d = {p[0].x < p[1].x ? p[0].x : p[1].x, p[0].y < p[1].y ? p[0].y : p[1].y,
              p[0].x < p[1].x ? p[1].x : p[0].x, p[0].y < p[1].y ? p[1].y : p[0].y};
    BOOL visible = NtGdiRectInRegion(region, &d);
    NtGdiDeleteObjectApp(region);
    return visible;
}

WINGDIAPI BOOL WINAPI PtVisible(HDC dc, int x, int y)
{
    RECT box;
    int kind = NtGdiGetAppClipBox(dc, &box);
    if (kind == ERROR)
        return -1;
    if (kind == NULLREGION || x < box.left || x >= box.right || y < box.top || y >= box.bottom)
        return FALSE;
    HRGN region = kind == COMPLEXREGION ? clip_region(dc) : nullptr;
    if (!region)
        return TRUE;
    POINT p = {x, y};
    NtGdiTransformPoints(dc, &p, &p, 1, 1);
    BOOL visible = NtGdiPtInRegion(region, p.x, p.y);
    NtGdiDeleteObjectApp(region);
    return visible;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
