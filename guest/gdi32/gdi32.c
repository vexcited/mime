/*
 * gdi32: the gdi client. objects live in win32k; these wrappers marshal
 * arguments to the NtGdi services.
 */
#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winnls.h>

static UCHAR handle_type(HGDIOBJ h)
{
    return (UCHAR)(((ULONG_PTR)h >> 16) & 0xFF);
}

/* ------------------------------------------------------------------ objects */

WINGDIAPI HGDIOBJ WINAPI GetStockObject(int index)
{
    return NtGdiGetStockObject(index);
}

WINGDIAPI DWORD WINAPI GetObjectType(HGDIOBJ h)
{
    switch (handle_type(h)) {
    case 0x01:
        return OBJ_DC;
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
    }
    return 0;
}

WINGDIAPI HGDIOBJ WINAPI SelectObject(HDC dc, HGDIOBJ h)
{
    switch (handle_type(h)) {
    case 0x05:
        return NtGdiSelectBitmap(dc, h);
    case 0x10:
        return NtGdiSelectBrush(dc, h);
    case 0x30:
        return NtGdiSelectPen(dc, h);
    case 0x0A:
        return NtGdiSelectFont(dc, h);
    case 0x04:
        return (HGDIOBJ)(LONG_PTR)NtGdiExtSelectClipRgn(dc, h, RGN_COPY);
    }
    return NULL;
}

WINGDIAPI BOOL WINAPI DeleteObject(HGDIOBJ h)
{
    return h ? NtGdiDeleteObjectApp(h) : FALSE;
}

WINGDIAPI int WINAPI GetObjectW(HANDLE h, int size, LPVOID buffer)
{
    return NtGdiExtGetObjectW(h, size, buffer);
}

WINGDIAPI int WINAPI GetObjectA(HANDLE h, int size, LPVOID buffer)
{
    if (GetObjectType(h) != OBJ_FONT)
        return NtGdiExtGetObjectW(h, size, buffer);
    LOGFONTW lfw;
    if (!NtGdiExtGetObjectW(h, sizeof(lfw), &lfw))
        return 0;
    if (!buffer)
        return sizeof(LOGFONTA);
    LOGFONTA lfa;
    memcpy(&lfa, &lfw, offsetof(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_ACP, 0, lfw.lfFaceName, -1, lfa.lfFaceName, LF_FACESIZE, NULL, NULL);
    int n = size < (int)sizeof(lfa) ? size : (int)sizeof(lfa);
    memcpy(buffer, &lfa, (size_t)n);
    return n;
}

WINGDIAPI HGDIOBJ WINAPI GetCurrentObject(HDC dc, UINT type)
{
    return NtGdiGetDCObject(dc, (int)type);
}

/* ------------------------------------------------------------------ device contexts */

WINGDIAPI HDC WINAPI CreateCompatibleDC(HDC dc)
{
    return NtGdiCreateCompatibleDC(dc);
}

WINGDIAPI HDC WINAPI CreateDCW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const void *mode)
{
    return NtGdiOpenDCW(NULL, NULL, NULL, 0, TRUE, NULL, NULL, NULL);
}

WINGDIAPI HDC WINAPI CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR output, const void *mode)
{
    return CreateDCW(NULL, NULL, NULL, NULL);
}

WINGDIAPI HDC WINAPI CreateICW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const void *mode)
{
    return CreateDCW(NULL, NULL, NULL, NULL);
}

WINGDIAPI BOOL WINAPI DeleteDC(HDC dc)
{
    return NtGdiDeleteObjectApp(dc);
}

WINGDIAPI int WINAPI SaveDC(HDC dc)
{
    return NtGdiSaveDC(dc);
}

WINGDIAPI BOOL WINAPI RestoreDC(HDC dc, int saved)
{
    return NtGdiRestoreDC(dc, saved);
}

WINGDIAPI int WINAPI GetDeviceCaps(HDC dc, int index)
{
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
    return set_dword(dc, MIME_DCDW_TEXTCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetTextColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_TEXTCOLOR, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI SetBkColor(HDC dc, COLORREF color)
{
    return set_dword(dc, MIME_DCDW_BKCOLOR, color, CLR_INVALID);
}

WINGDIAPI COLORREF WINAPI GetBkColor(HDC dc)
{
    return get_dword(dc, MIME_DCDW_BKCOLOR, CLR_INVALID);
}

WINGDIAPI int WINAPI SetBkMode(HDC dc, int mode)
{
    return (int)set_dword(dc, MIME_DCDW_BKMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetBkMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_BKMODE, 0);
}

WINGDIAPI UINT WINAPI SetTextAlign(HDC dc, UINT align)
{
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
    return (int)set_dword(dc, MIME_DCDW_ROP2, (DWORD)rop, 0);
}

WINGDIAPI int WINAPI GetROP2(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_ROP2, 0);
}

WINGDIAPI int WINAPI SetStretchBltMode(HDC dc, int mode)
{
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
    return (int)set_dword(dc, MIME_DCDW_MAPMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI GetMapMode(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_MAPMODE, 0);
}

WINGDIAPI DWORD WINAPI SetLayout(HDC dc, DWORD layout)
{
    return set_dword(dc, MIME_DCDW_LAYOUT, layout, GDI_ERROR);
}

WINGDIAPI DWORD WINAPI GetLayout(HDC dc)
{
    return get_dword(dc, MIME_DCDW_LAYOUT, GDI_ERROR);
}

WINGDIAPI int WINAPI SetPolyFillMode(HDC dc, int mode)
{
    return (int)set_dword(dc, MIME_DCDW_POLYFILLMODE, (DWORD)mode, 0);
}

WINGDIAPI int WINAPI SetTextCharacterExtra(HDC dc, int extra)
{
    return (int)set_dword(dc, MIME_DCDW_TEXTCHAREXTRA, (DWORD)extra, 0x80000000u);
}

WINGDIAPI int WINAPI GetTextCharacterExtra(HDC dc)
{
    return (int)get_dword(dc, MIME_DCDW_TEXTCHAREXTRA, 0x80000000u);
}

WINGDIAPI BOOL WINAPI SetBrushOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    if (old)
        *old = (POINT){0, 0};
    return TRUE;
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
    return NtGdiSetViewportOrgEx(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI GetViewportOrgEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_VIEWPORTORG, p);
}

WINGDIAPI BOOL WINAPI SetWindowOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    return NtGdiSetWindowOrgEx(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI GetWindowOrgEx(HDC dc, LPPOINT p)
{
    return NtGdiGetDCPoint(dc, MIME_DCPT_WINDOWORG, p);
}

WINGDIAPI BOOL WINAPI OffsetViewportOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    POINT p;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_VIEWPORTORG, &p))
        return FALSE;
    if (old)
        *old = p;
    return NtGdiSetViewportOrgEx(dc, p.x + x, p.y + y, NULL);
}

WINGDIAPI BOOL WINAPI OffsetWindowOrgEx(HDC dc, int x, int y, LPPOINT old)
{
    POINT p;
    if (!NtGdiGetDCPoint(dc, MIME_DCPT_WINDOWORG, &p))
        return FALSE;
    if (old)
        *old = p;
    return NtGdiSetWindowOrgEx(dc, p.x + x, p.y + y, NULL);
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

WINGDIAPI BOOL WINAPI SetViewportExtEx(HDC dc, int x, int y, LPSIZE old)
{
    if (old)
        *old = (SIZE){1, 1};
    return TRUE;
}

WINGDIAPI BOOL WINAPI SetWindowExtEx(HDC dc, int x, int y, LPSIZE old)
{
    if (old)
        *old = (SIZE){1, 1};
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

WINGDIAPI HBITMAP WINAPI CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section,
                                          DWORD offset)
{
    void *dummy;
    return NtGdiCreateDIBSection(dc, section, offset, info, usage, info ? info->bmiHeader.biSize : 0, 0, 0,
                                 bits ? bits : &dummy);
}

static UINT dib_bits_size(const BITMAPINFOHEADER *h)
{
    UINT stride = (UINT)(((h->biWidth < 0 ? -h->biWidth : h->biWidth) * h->biBitCount + 31) / 32 * 4);
    return stride * (UINT)(h->biHeight < 0 ? -h->biHeight : h->biHeight);
}

WINGDIAPI int WINAPI SetDIBitsToDevice(HDC dc, int x, int y, DWORD width, DWORD height, int src_x, int src_y,
                                       UINT start, UINT lines, const void *bits, const BITMAPINFO *info, UINT usage)
{
    return NtGdiSetDIBitsToDeviceInternal(dc, x, y, width, height, src_x, src_y, start, lines, bits, info, usage,
                                          dib_bits_size(&info->bmiHeader), info->bmiHeader.biSize + 256 * 4, TRUE,
                                          NULL);
}

WINGDIAPI int WINAPI StretchDIBits(HDC dc, int x, int y, int width, int height, int src_x, int src_y, int src_width,
                                   int src_height, const void *bits, const BITMAPINFO *info, UINT usage, DWORD rop)
{
    return NtGdiStretchDIBitsInternal(dc, x, y, width, height, src_x, src_y, src_width, src_height, bits, info, usage,
                                      rop, info->bmiHeader.biSize + 256 * 4, dib_bits_size(&info->bmiHeader), NULL);
}

WINGDIAPI int WINAPI GetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, LPVOID bits, LPBITMAPINFO info,
                               UINT usage)
{
    return NtGdiGetDIBitsInternal(dc, bitmap, start, lines, bits, info, usage, bits ? dib_bits_size(&info->bmiHeader) : 0,
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
                    : CreateBitmap(header->biWidth, height, 1, 1, NULL);
    if (bm && init == CBM_INIT && bits && info)
        SetDIBits(dc, bm, 0, (UINT)height, bits, info, usage);
    return bm;
}

WINGDIAPI BOOL WINAPI BitBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y, DWORD rop)
{
    return NtGdiBitBlt(dst, x, y, width, height, src, src_x, src_y, rop, 0, 0);
}

WINGDIAPI BOOL WINAPI StretchBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                 int src_width, int src_height, DWORD rop)
{
    return NtGdiStretchBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, rop, 0);
}

WINGDIAPI BOOL WINAPI PatBlt(HDC dc, int x, int y, int width, int height, DWORD rop)
{
    return NtGdiPatBlt(dc, x, y, width, height, rop);
}

WINGDIAPI BOOL WINAPI GdiAlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                    int src_width, int src_height, BLENDFUNCTION blend)
{
    return NtGdiAlphaBlend(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, blend, NULL);
}

WINGDIAPI BOOL WINAPI GdiTransparentBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                        int src_width, int src_height, UINT transparent)
{
    /* no color keying yet: a plain copy. */
    return StretchBlt(dst, x, y, width, height, src, src_x, src_y, src_width, src_height, SRCCOPY);
}

/* ------------------------------------------------------------------ brushes and pens */

WINGDIAPI HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
    return NtGdiCreateSolidBrush(color, NULL);
}

WINGDIAPI HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH *lb)
{
    if (lb->lbStyle == BS_NULL)
        return GetStockObject(NULL_BRUSH);
    return NtGdiCreateSolidBrush(lb->lbColor, NULL);
}

WINGDIAPI HBRUSH WINAPI CreateHatchBrush(int hatch, COLORREF color)
{
    return NtGdiCreateSolidBrush(color, NULL);
}

WINGDIAPI HPEN WINAPI CreatePen(int style, int width, COLORREF color)
{
    return NtGdiCreatePen(style, width, color, NULL);
}

WINGDIAPI HPEN WINAPI CreatePenIndirect(const LOGPEN *lp)
{
    return NtGdiCreatePen((int)lp->lopnStyle, lp->lopnWidth.x, lp->lopnColor, NULL);
}

WINGDIAPI HPEN WINAPI ExtCreatePen(DWORD style, DWORD width, const LOGBRUSH *lb, DWORD count, const DWORD *styles)
{
    return NtGdiCreatePen((int)(style & PS_STYLE_MASK), (int)width, lb ? lb->lbColor : 0, NULL);
}

/* ------------------------------------------------------------------ fonts and text */

WINGDIAPI HFONT WINAPI CreateFontIndirectW(const LOGFONTW *lf)
{
    return lf ? NtGdiHfontCreate(lf, sizeof(*lf), 0, 0, NULL) : NULL;
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
        return NULL;
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
    WCHAR wface[LF_FACESIZE] = {0};
    if (face)
        MultiByteToWideChar(CP_ACP, 0, face, -1, wface, LF_FACESIZE);
    wface[LF_FACESIZE - 1] = 0;
    return CreateFontW(height, width, escapement, orientation, weight, italic, underline, strikeout, charset,
                       out_precision, clip_precision, quality, pitch, face ? wface : NULL);
}

WINGDIAPI BOOL WINAPI ExtTextOutW(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count,
                                  const INT *dx)
{
    return NtGdiExtTextOutW(dc, x, y, options, rect, text, (int)count, dx, 0);
}

WINGDIAPI BOOL WINAPI TextOutW(HDC dc, int x, int y, LPCWSTR text, int count)
{
    return NtGdiExtTextOutW(dc, x, y, 0, NULL, text, count, NULL, 0);
}

/* converts an ansi string to a heap copy; the caller frees it. */
static LPWSTR to_wide(LPCSTR s, int count, int *out_count)
{
    if (count < 0)
        count = s ? (int)strlen(s) : 0;
    int n = count ? MultiByteToWideChar(CP_ACP, 0, s, count, NULL, 0) : 0;
    LPWSTR w = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, ((SIZE_T)n + 1) * sizeof(WCHAR));
    if (!w)
        return NULL;
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
    int n;
    LPWSTR w = to_wide(text, (int)count, &n);
    if (!w)
        return FALSE;
    BOOL ok = NtGdiExtTextOutW(dc, x, y, options, rect, w, n, (dx && (int)count == n) ? dx : NULL, 0);
    free_wide(w);
    return ok;
}

WINGDIAPI BOOL WINAPI TextOutA(HDC dc, int x, int y, LPCSTR text, int count)
{
    return ExtTextOutA(dc, x, y, 0, NULL, text, (UINT)count, NULL);
}

WINGDIAPI BOOL WINAPI GetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int max_extent, LPINT fit, LPINT dx,
                                            LPSIZE size)
{
    return NtGdiGetTextExtentExW(dc, text, (ULONG)count, (ULONG)max_extent, (ULONG *)fit, (ULONG *)dx, size, 0);
}

WINGDIAPI BOOL WINAPI GetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size)
{
    return NtGdiGetTextExtentExW(dc, text, (ULONG)count, 0, NULL, NULL, size, 0);
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
    return WideCharToMultiByte(CP_ACP, 0, w, -1, name, count, NULL, NULL);
}

WINGDIAPI BOOL WINAPI GetCharWidth32W(HDC dc, UINT first, UINT last, LPINT widths)
{
    return last >= first && NtGdiGetCharWidthW(dc, first, last - first + 1, NULL, 0, widths);
}

WINGDIAPI BOOL WINAPI GetCharWidthW(HDC dc, UINT first, UINT last, LPINT widths)
{
    return GetCharWidth32W(dc, first, last, widths);
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsW(HDC dc, UINT first, UINT last, LPABC abc)
{
    return last >= first && NtGdiGetCharABCWidthsW(dc, first, last - first + 1, NULL, 0, abc);
}

WINGDIAPI int WINAPI AddFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved)
{
    if (!name)
        return 0;
    return NtGdiAddFontResourceW(name, (ULONG)wcslen(name) + 1, 1, flags, 0, NULL);
}

WINGDIAPI int WINAPI AddFontResourceW(LPCWSTR name)
{
    return AddFontResourceExW(name, 0, NULL);
}

WINGDIAPI DWORD WINAPI GetFontData(HDC dc, DWORD table, DWORD offset, PVOID buffer, DWORD size)
{
    return NtGdiGetFontData(dc, table, offset, buffer, size);
}

WINGDIAPI int WINAPI EnumFontFamiliesExW(HDC dc, LPLOGFONTW lf, FONTENUMPROCW proc, LPARAM param, DWORD flags)
{
    /* reports the requested face when it can be realized, enough for existence checks. */
    if (!lf || !proc)
        return 0;
    HFONT font = CreateFontIndirectW(lf);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    LOGFONTW found = *lf;
    GetTextMetricsW(dc, &tm);
    GetTextFaceW(dc, LF_FACESIZE, found.lfFaceName);
    SelectObject(dc, old);
    DeleteObject(font);
    if (lf->lfFaceName[0] && _wcsicmp(found.lfFaceName, lf->lfFaceName))
        return 1;
    return proc(&found, &tm, 4 /* TRUETYPE_FONTTYPE */, param);
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
    return NtGdiMoveTo(dc, x, y, old);
}

WINGDIAPI BOOL WINAPI LineTo(HDC dc, int x, int y)
{
    return NtGdiLineTo(dc, x, y);
}

WINGDIAPI BOOL WINAPI Rectangle(HDC dc, int left, int top, int right, int bottom)
{
    return NtGdiRectangle(dc, left, top, right, bottom);
}

WINGDIAPI BOOL WINAPI RoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height)
{
    return NtGdiRoundRect(dc, left, top, right, bottom, width, height);
}

WINGDIAPI BOOL WINAPI Ellipse(HDC dc, int left, int top, int right, int bottom)
{
    return NtGdiEllipse(dc, left, top, right, bottom);
}

/* NtGdiPolyPolyDraw functions. */
#define GDI_POLYPOLYGON 1
#define GDI_POLYPOLYLINE 2

WINGDIAPI BOOL WINAPI Polyline(HDC dc, const POINT *points, int count)
{
    ULONG n = (ULONG)count;
    return count > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, GDI_POLYPOLYLINE);
}

WINGDIAPI BOOL WINAPI Polygon(HDC dc, const POINT *points, int count)
{
    ULONG n = (ULONG)count;
    return count > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, &n, 1, GDI_POLYPOLYGON);
}

WINGDIAPI BOOL WINAPI PolyPolygon(HDC dc, const POINT *points, const INT *counts, int polygons)
{
    return polygons > 0 && NtGdiPolyPolyDraw(dc, (POINT *)points, (ULONG *)counts, (ULONG)polygons, GDI_POLYPOLYGON);
}

WINGDIAPI BOOL WINAPI PolyPolyline(HDC dc, const POINT *points, const DWORD *counts, DWORD lines)
{
    return lines && NtGdiPolyPolyDraw(dc, (POINT *)points, (ULONG *)counts, lines, GDI_POLYPOLYLINE);
}

WINGDIAPI COLORREF WINAPI SetPixel(HDC dc, int x, int y, COLORREF color)
{
    return NtGdiSetPixel(dc, x, y, color);
}

WINGDIAPI BOOL WINAPI SetPixelV(HDC dc, int x, int y, COLORREF color)
{
    return NtGdiSetPixel(dc, x, y, color) != CLR_INVALID;
}

WINGDIAPI COLORREF WINAPI GetPixel(HDC dc, int x, int y)
{
    return NtGdiGetPixel(dc, x, y);
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
    return NtGdiFillRgn(dc, region, brush);
}

WINGDIAPI BOOL WINAPI FrameRgn(HDC dc, HRGN region, HBRUSH brush, int width, int height)
{
    return NtGdiFrameRgn(dc, region, brush, width, height);
}

WINGDIAPI BOOL WINAPI InvertRgn(HDC dc, HRGN region)
{
    return NtGdiInvertRgn(dc, region);
}

WINGDIAPI BOOL WINAPI PaintRgn(HDC dc, HRGN region)
{
    return NtGdiFillRgn(dc, region, NtGdiGetDCObject(dc, OBJ_BRUSH));
}

WINGDIAPI int WINAPI ExtSelectClipRgn(HDC dc, HRGN region, int mode)
{
    return NtGdiExtSelectClipRgn(dc, region, mode);
}

WINGDIAPI int WINAPI SelectClipRgn(HDC dc, HRGN region)
{
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

WINGDIAPI int WINAPI IntersectClipRect(HDC dc, int left, int top, int right, int bottom)
{
    return NtGdiIntersectClipRect(dc, left, top, right, bottom);
}

WINGDIAPI int WINAPI ExcludeClipRect(HDC dc, int left, int top, int right, int bottom)
{
    return NtGdiExcludeClipRect(dc, left, top, right, bottom);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
