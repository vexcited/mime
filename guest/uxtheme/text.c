/*
 * theme text: DrawThemeText and DrawThemeTextEx use the font and the text
 * color of the part when the visual style defines them, and the current ones
 * of the dc otherwise.
 */
#include "uxtheme_private.h"

typedef struct TEXT_STATE {
    HFONT Font;
    HGDIOBJ OldFont;
    COLORREF OldColor;
    BOOL ColorSet;
    int OldMode;
} TEXT_STATE;

static void begin_text(UX_THEME *theme, HDC dc, int part, int state, int font_prop, BOOL set_color, int color_prop,
                       const COLORREF *color, TEXT_STATE *ts)
{
    memset(ts, 0, sizeof(*ts));
    MimeMsstylesFont f;
    if (mime_msstyles_get_font(&UxStyles, theme->Class, part, state, font_prop, theme->Dpi, &f)) {
        LOGFONTW lf;
        memcpy(&lf, &f, sizeof(lf));
        ts->Font = CreateFontIndirectW(&lf);
        if (ts->Font)
            ts->OldFont = SelectObject(dc, ts->Font);
    }
    if (set_color) {
        uint32_t c;
        if (color) {
            ts->OldColor = SetTextColor(dc, *color);
            ts->ColorSet = TRUE;
        } else if (mime_msstyles_get_color(&UxStyles, theme->Class, part, state, color_prop, &c)) {
            ts->OldColor = SetTextColor(dc, c);
            ts->ColorSet = TRUE;
        }
    }
    ts->OldMode = SetBkMode(dc, TRANSPARENT);
}

static void end_text(HDC dc, TEXT_STATE *ts)
{
    SetBkMode(dc, ts->OldMode);
    if (ts->ColorSet)
        SetTextColor(dc, ts->OldColor);
    if (ts->Font) {
        SelectObject(dc, ts->OldFont);
        DeleteObject(ts->Font);
    }
}

/*
 * composited text for glass and layered surfaces: gdi text leaves the alpha
 * of a 32 bpp target at 0, so the text is drawn white on black into a
 * scratch dib whose coverage becomes the alpha of the text color.
 */
static void draw_composited(HDC dc, LPCWSTR text, int count, DWORD flags, const RECT *rect)
{
    int w = rect->right - rect->left, h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    DWORD *bits = nullptr;
    HBITMAP bm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
    if (!bm)
        return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old_bm = SelectObject(mem, bm);
    HGDIOBJ old_font = SelectObject(mem, GetCurrentObject(dc, OBJ_FONT));
    memset(bits, 0, (SIZE_T)w * h * 4);
    SetTextColor(mem, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    RECT local = {0, 0, w, h};
    DrawTextW(mem, text, count, &local, flags);
    GdiFlush();
    COLORREF color = GetTextColor(dc);
    DWORD r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    for (SIZE_T i = 0; i < (SIZE_T)w * h; i++) {
        DWORD p = bits[i], a = p & 0xFF;
        if ((p >> 8 & 0xFF) > a)
            a = p >> 8 & 0xFF;
        if ((p >> 16 & 0xFF) > a)
            a = p >> 16 & 0xFF;
        bits[i] = a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
    }
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc, rect->left, rect->top, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old_font);
    SelectObject(mem, old_bm);
    DeleteDC(mem);
    DeleteObject(bm);
}

THEMEAPI DrawThemeTextEx(HTHEME handle, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                         LPRECT rect, const DTTOPTS *options)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!dc)
        hr = E_HANDLE;
    else if (!text || !rect)
        hr = E_POINTER;
    else if (options && options->dwSize != sizeof(DTTOPTS))
        hr = E_INVALIDARG;
    if (FAILED(hr)) {
        UxRelease(theme);
        return hr;
    }
    DWORD opt = options ? options->dwFlags : 0;
    if (opt & DTT_STATEID)
        state = options->iStateId;
    int font_prop = (opt & DTT_FONTPROP) ? options->iFontPropId : TMT_FONT;
    int color_prop = (opt & DTT_COLORPROP) ? options->iColorPropId : TMT_TEXTCOLOR;
    TEXT_STATE ts;
    begin_text(theme, dc, part, state, font_prop, TRUE, color_prop, (opt & DTT_TEXTCOLOR) ? &options->crText : nullptr,
               &ts);
    if (opt & DTT_CALCRECT) {
        DrawTextW(dc, text, count, rect, flags | DT_CALCRECT);
    } else if (opt & DTT_CALLBACK) {
        if (options->pfnDrawTextCallback)
            options->pfnDrawTextCallback(dc, (LPWSTR)text, count, rect, flags, options->lParam);
    } else if (opt & DTT_COMPOSITED) {
        draw_composited(dc, text, count, flags, rect);
    } else {
        DrawTextW(dc, text, count, rect, flags);
    }
    end_text(dc, &ts);
    UxRelease(theme);
    return S_OK;
}

THEMEAPI DrawThemeText(HTHEME handle, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                       DWORD flags2, LPCRECT rect)
{
    if (!rect)
        return E_POINTER;
    RECT rc = *rect;
    DTTOPTS opts;
    memset(&opts, 0, sizeof(opts));
    opts.dwSize = sizeof(opts);
    if (flags2 & DTT_GRAYED) {
        opts.dwFlags = DTT_TEXTCOLOR;
        opts.crText = GetSysColor(COLOR_GRAYTEXT);
    }
    return DrawThemeTextEx(handle, dc, part, state, text, count, flags, &rc, &opts);
}

THEMEAPI GetThemeTextExtent(HTHEME handle, HDC dc, int part, int state, LPCWSTR text, int count, DWORD flags,
                            LPCRECT bounds, LPRECT extent)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!dc)
        hr = E_HANDLE;
    else if (!text || !extent)
        hr = E_POINTER;
    if (FAILED(hr)) {
        UxRelease(theme);
        return hr;
    }
    TEXT_STATE ts;
    begin_text(theme, dc, part, state, TMT_FONT, FALSE, 0, nullptr, &ts);
    RECT rc = {0, 0, 0, 0};
    if (bounds)
        rc = *bounds;
    DrawTextW(dc, text, count, &rc, flags | DT_CALCRECT);
    end_text(dc, &ts);
    *extent = rc;
    UxRelease(theme);
    return S_OK;
}

THEMEAPI GetThemeTextMetrics(HTHEME handle, HDC dc, int part, int state, TEXTMETRICW *metrics)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    if (!dc)
        hr = E_HANDLE;
    else if (!metrics)
        hr = E_POINTER;
    if (SUCCEEDED(hr)) {
        TEXT_STATE ts;
        begin_text(theme, dc, part, state, TMT_FONT, FALSE, 0, nullptr, &ts);
        if (!GetTextMetricsW(dc, metrics))
            hr = E_FAIL;
        end_text(dc, &ts);
    }
    UxRelease(theme);
    return hr;
}
