/*
 * theme property getters: GetThemeColor, GetThemeInt and the others, the
 * system metrics of the visual style and property origins.
 */
#include "uxtheme_private.h"

#define BEGIN_THEME(handle)                                                                                       \
    HRESULT hr;                                                                                                   \
    UX_THEME *theme = UxAcquire(handle, &hr);                                                                     \
    if (!theme)                                                                                                   \
    return hr

THEMEAPI GetThemeColor(HTHEME handle, int part, int state, int prop, COLORREF *color)
{
    BEGIN_THEME(handle);
    uint32_t c;
    if (!color)
        hr = E_POINTER;
    else if (mime_msstyles_get_color(&UxStyles, theme->Class, part, state, prop, &c))
        *color = c;
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeInt(HTHEME handle, int part, int state, int prop, int *value)
{
    BEGIN_THEME(handle);
    int32_t v;
    if (!value)
        hr = E_POINTER;
    else if (mime_msstyles_get_int(&UxStyles, theme->Class, part, state, prop, &v))
        *value = v;
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeEnumValue(HTHEME handle, int part, int state, int prop, int *value)
{
    BEGIN_THEME(handle);
    int32_t v;
    if (!value)
        hr = E_POINTER;
    else if (mime_msstyles_get_enum(&UxStyles, theme->Class, part, state, prop, &v))
        *value = v;
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeBool(HTHEME handle, int part, int state, int prop, BOOL *value)
{
    BEGIN_THEME(handle);
    bool v;
    if (!value)
        hr = E_POINTER;
    else if (mime_msstyles_get_bool(&UxStyles, theme->Class, part, state, prop, &v))
        *value = v;
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

/* integer or size properties scaled from 96 dpi to the theme dpi. */
THEMEAPI GetThemeMetric(HTHEME handle, HDC dc, int part, int state, int prop, int *value)
{
    BEGIN_THEME(handle);
    int32_t v;
    if (!value)
        hr = E_POINTER;
    else if (mime_msstyles_get_int(&UxStyles, theme->Class, part, state, prop, &v))
        *value = UxScale(theme, v);
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemePosition(HTHEME handle, int part, int state, int prop, POINT *point)
{
    BEGIN_THEME(handle);
    int32_t x, y;
    if (!point)
        hr = E_POINTER;
    else if (mime_msstyles_get_position(&UxStyles, theme->Class, part, state, prop, &x, &y)) {
        point->x = x;
        point->y = y;
    } else {
        hr = UX_E_NOTFOUND;
    }
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeRect(HTHEME handle, int part, int state, int prop, LPRECT rect)
{
    BEGIN_THEME(handle);
    MimeMsstylesRect r;
    if (!rect)
        hr = E_POINTER;
    else if (mime_msstyles_get_rect(&UxStyles, theme->Class, part, state, prop, &r))
        SetRect(rect, r.left, r.top, r.right, r.bottom);
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeMargins(HTHEME handle, HDC dc, int part, int state, int prop, LPCRECT rect, MARGINS *margins)
{
    BEGIN_THEME(handle);
    MimeMsstylesMargins m;
    if (!margins) {
        hr = E_POINTER;
    } else if (mime_msstyles_get_margins(&UxStyles, theme->Class, part, state, prop, &m)) {
        margins->cxLeftWidth = m.left;
        margins->cxRightWidth = m.right;
        margins->cyTopHeight = m.top;
        margins->cyBottomHeight = m.bottom;
    } else {
        memset(margins, 0, sizeof(*margins));
        hr = UX_E_NOTFOUND;
    }
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeIntList(HTHEME handle, int part, int state, int prop, INTLIST *list)
{
    BEGIN_THEME(handle);
    uint32_t count = 0;
    if (!list)
        hr = E_POINTER;
    else if (mime_msstyles_get_intlist(&UxStyles, theme->Class, part, state, prop, (int32_t *)list->iValues,
                                       MAX_INTLIST_COUNT, &count))
        list->iValueCount = (int)(count > MAX_INTLIST_COUNT ? MAX_INTLIST_COUNT : count);
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeString(HTHEME handle, int part, int state, int prop, LPWSTR buffer, int size)
{
    BEGIN_THEME(handle);
    if (!buffer || size <= 0)
        hr = E_POINTER;
    else if (!mime_msstyles_get_string(&UxStyles, theme->Class, part, state, prop, (uint16_t *)buffer, (size_t)size))
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

static void font_to_logfont(const MimeMsstylesFont *f, LOGFONTW *lf)
{
    memcpy(lf, f, sizeof(*lf));
}

THEMEAPI GetThemeFont(HTHEME handle, HDC dc, int part, int state, int prop, LOGFONTW *font)
{
    BEGIN_THEME(handle);
    MimeMsstylesFont f;
    if (!font)
        hr = E_POINTER;
    else if (mime_msstyles_get_font(&UxStyles, theme->Class, part, state, prop, theme->Dpi, &f))
        font_to_logfont(&f, font);
    else
        hr = UX_E_NOTFOUND;
    UxRelease(theme);
    return hr;
}

/*
 * the files of a packed theme are IMAGE resources: windows returns the value
 * as stored, which for those records is the resource, so the name given here
 * is the resource id in the form "#id".
 */
THEMEAPI GetThemeFilename(HTHEME handle, int part, int state, int prop, LPWSTR buffer, int size)
{
    if (!buffer && size)
        return E_POINTER;
    BEGIN_THEME(handle);
    MimeMsstylesProp p;
    if (!buffer) {
        hr = E_POINTER;
    } else if (!mime_msstyles_find_prop(&UxStyles, theme->Class, part, state, prop, &p)) {
        hr = UX_E_NOTFOUND;
    } else if (p.type == MIME_TMT_FILENAME && p.res_id) {
        WCHAR id[16];
        _snwprintf(id, RTL_NUMBER_OF(id), L"#%d", p.res_id);
        if (size <= (int)wcslen(id))
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        else
            wcscpy(buffer, id);
    } else if (size <= 0 || !mime_msstyles_prop_string(&UxStyles, &p, (uint16_t *)buffer, (size_t)size)) {
        hr = UX_E_NOTFOUND;
    }
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeStream(HTHEME handle, int part, int state, int prop, void **stream, DWORD *size, HINSTANCE inst)
{
    BEGIN_THEME(handle);
    const uint8_t *data;
    uint32_t n;
    if (!stream || !size)
        hr = E_POINTER;
    else if (mime_msstyles_get_stream(&UxStyles, theme->Class, part, state, prop, &data, &n) ||
             mime_msstyles_get_filename(&UxStyles, theme->Class, part, state, prop, nullptr, &data, &n)) {
        *stream = (void *)data;
        *size = n;
    } else {
        hr = UX_E_NOTFOUND;
    }
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemeBitmap(HTHEME handle, int part, int state, int prop, ULONG flags, HBITMAP *bitmap)
{
    if (!bitmap)
        return E_POINTER;
    *bitmap = nullptr;
    if (flags & ~GBF_VALIDBITS)
        return E_INVALIDARG;
    BEGIN_THEME(handle);
    MimeMsstylesImage img;
    unsigned select = prop == TMT_GLYPHDIBDATA || prop == TMT_GLYPHIMAGEFILE ? MIME_MSSTYLES_GLYPH : 0;
    UX_BITMAP *b = nullptr;
    if (mime_msstyles_get_image(&UxStyles, theme->Class, part, state, theme->Dpi, 0, 0, select, &img))
        b = UxGetBitmap(&img);
    if (!b) {
        hr = UX_E_NOTFOUND;
    } else if (flags & GBF_COPY) {
        *bitmap = CopyImage(b->Bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (!*bitmap)
            hr = E_OUTOFMEMORY;
    } else {
        *bitmap = b->Bitmap;
    }
    UxRelease(theme);
    return hr;
}

THEMEAPI GetThemePropertyOrigin(HTHEME handle, int part, int state, int prop, enum PROPERTYORIGIN *origin)
{
    if (!origin)
        return E_POINTER;
    BEGIN_THEME(handle);
    MimeMsstylesProp p;
    if (!prop) {
        hr = E_FAIL;
    } else if (!mime_msstyles_find_prop(&UxStyles, theme->Class, part, state, prop, &p)) {
        *origin = PO_NOTFOUND;
    } else if (p.cls == UxStyles.globals) {
        *origin = PO_GLOBAL;
    } else if (p.part && p.state) {
        *origin = PO_STATE;
    } else if (p.part) {
        *origin = PO_PART;
    } else {
        *origin = PO_CLASS;
    }
    UxRelease(theme);
    return hr;
}

/*
 * IsThemePartDefined: true when the class itself has records for the part,
 * or for the exact state when one is given. inherited parts do not count.
 */
THEMEAPI_(BOOL) IsThemePartDefined(HTHEME handle, int part, int state)
{
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme) {
        SetLastError((DWORD)hr);
        return FALSE;
    }
    SetLastError(0);
    BOOL found = FALSE;
    MimeMsstylesProp p;
    for (uint32_t i = 0; part > 0 && state >= 0 && !found && mime_msstyles_class_record(&UxStyles, theme->Class, i, &p);
         i++)
        if (p.part == part && (state == 0 || p.state == state))
            found = TRUE;
    UxRelease(theme);
    return found;
}

/* TMT_TRANSITIONDURATIONS: the state count n, then the n by n matrix of durations, from state rows. */
THEMEAPI GetThemeTransitionDuration(HTHEME handle, int part, int state_from, int state_to, int prop,
                                    DWORD *duration)
{
    BEGIN_THEME(handle);
    int32_t list[MAX_INTLIST_COUNT + 1];
    uint32_t count = 0;
    if (!duration || state_from <= 0 || state_to <= 0) {
        hr = E_INVALIDARG;
    } else {
        *duration = 0;
        if (!mime_msstyles_get_intlist(&UxStyles, theme->Class, part, state_from, prop, list, RTL_NUMBER_OF(list),
                                       &count) ||
            count > MAX_INTLIST_COUNT || !count) {
            hr = UX_E_NOTFOUND;
        } else if (state_from > list[0] || state_to > list[0]) {
            hr = E_INVALIDARG;
        } else if (count > 1) {
            uint32_t at = (uint32_t)state_to + (uint32_t)list[0] * (uint32_t)(state_from - 1);
            *duration = at < count ? (DWORD)list[at] : 0;
        }
    }
    UxRelease(theme);
    return hr;
}

/* system metrics of the visual style, falling back to user32 without a theme. */

THEMEAPI_(COLORREF) GetThemeSysColor(HTHEME handle, int color)
{
    uint32_t c;
    int index = color >= TMT_FIRSTCOLOR ? color - TMT_FIRSTCOLOR : color;
    if (handle && UxThemeLoaded() && mime_msstyles_sys_color(&UxStyles, index, &c))
        return c;
    return GetSysColor(index);
}

THEMEAPI_(HBRUSH) GetThemeSysColorBrush(HTHEME handle, int color)
{
    return CreateSolidBrush(GetThemeSysColor(handle, color));
}

THEMEAPI_(BOOL) GetThemeSysBool(HTHEME handle, int bool_id)
{
    int32_t v;
    if (bool_id < TMT_FIRSTBOOL || bool_id > TMT_LASTBOOL) {
        SetLastError((DWORD)E_INVALIDARG);
        return FALSE;
    }
    SetLastError(0);
    if (handle && UxThemeLoaded() && mime_msstyles_sys_int(&UxStyles, bool_id, &v))
        return v != 0;
    BOOL flat = FALSE;
    SystemParametersInfoW(0x1022 /* SPI_GETFLATMENU */, 0, &flat, 0);
    return flat;
}

static const struct {
    int Metric, Prop;
} g_sys_sizes[] = {
    {2 /* SM_CXVSCROLL */, TMT_SCROLLBARWIDTH},  {3 /* SM_CYHSCROLL */, TMT_SCROLLBARHEIGHT},
    {30 /* SM_CXSIZE */, TMT_CAPTIONBARWIDTH},  {31 /* SM_CYSIZE */, TMT_CAPTIONBARHEIGHT},
    {52 /* SM_CXSMSIZE */, TMT_SMCAPTIONBARWIDTH}, {53 /* SM_CYSMSIZE */, TMT_SMCAPTIONBARHEIGHT},
    {54 /* SM_CXMENUSIZE */, TMT_MENUBARWIDTH}, {55 /* SM_CYMENUSIZE */, TMT_MENUBARHEIGHT},
    {32 /* SM_CXFRAME */, TMT_SIZINGBORDERWIDTH}, {33 /* SM_CYFRAME */, TMT_SIZINGBORDERWIDTH},
    {92 /* SM_CXPADDEDBORDER */, TMT_PADDEDBORDERWIDTH},
};

THEMEAPI_(int) GetThemeSysSize(HTHEME handle, int size_id)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_sys_sizes); i++) {
        if (g_sys_sizes[i].Metric != size_id)
            continue;
        int32_t v;
        if (handle && UxThemeLoaded() && mime_msstyles_sys_int(&UxStyles, g_sys_sizes[i].Prop, &v)) {
            HRESULT hr;
            UX_THEME *theme = UxAcquire(handle, &hr);
            int dpi = theme ? (int)theme->Dpi : 96;
            if (theme)
                UxRelease(theme);
            return MulDiv(v, dpi, 96);
        }
        return GetSystemMetrics(size_id);
    }
    SetLastError((DWORD)E_INVALIDARG);
    return 0;
}

THEMEAPI GetThemeSysFont(HTHEME handle, int font_id, LOGFONTW *font)
{
    if (!font)
        return E_POINTER;
    if (font_id < TMT_FIRSTFONT || font_id > TMT_LASTFONT)
        return E_INVALIDARG;
    MimeMsstylesFont f;
    if (handle && UxThemeLoaded() && mime_msstyles_sys_font(&UxStyles, font_id, 96, &f)) {
        font_to_logfont(&f, font);
        return S_OK;
    }
    if (font_id == TMT_ICONTITLEFONT)
        return SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(*font), font, 0) ? S_OK : E_FAIL;
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        return E_FAIL;
    switch (font_id) {
    case TMT_CAPTIONFONT:
        *font = ncm.lfCaptionFont;
        break;
    case TMT_SMALLCAPTIONFONT:
        *font = ncm.lfSmCaptionFont;
        break;
    case TMT_MENUFONT:
        *font = ncm.lfMenuFont;
        break;
    case TMT_STATUSFONT:
        *font = ncm.lfStatusFont;
        break;
    default:
        *font = ncm.lfMessageFont;
        break;
    }
    return S_OK;
}

THEMEAPI GetThemeSysString(HTHEME handle, int string_id, LPWSTR buffer, int size)
{
    if (!buffer || size <= 0)
        return E_POINTER;
    if (string_id < TMT_FIRSTSTRING || string_id > TMT_LASTSTRING)
        return E_INVALIDARG;
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    UxRelease(theme);
    return mime_msstyles_sys_string(&UxStyles, string_id, (uint16_t *)buffer, (size_t)size) ? S_OK : UX_E_NOTFOUND;
}

THEMEAPI GetThemeSysInt(HTHEME handle, int int_id, int *value)
{
    if (!value)
        return E_POINTER;
    if (int_id < TMT_FIRSTINT || int_id > TMT_LASTINT)
        return E_INVALIDARG;
    HRESULT hr;
    UX_THEME *theme = UxAcquire(handle, &hr);
    if (!theme)
        return hr;
    UxRelease(theme);
    int32_t v;
    if (!mime_msstyles_sys_int(&UxStyles, int_id, &v))
        return UX_E_NOTFOUND;
    *value = v;
    return S_OK;
}
