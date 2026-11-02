/*
 * fonts: enumeration, outline metrics, glyph indices and outlines, character
 * placement and fonts added at run time. the font engine lives in win32k.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

static void *heap_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size ? size : 1);
}

static void heap_free(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static void tm_w_to_a(const TEXTMETRICW *w, TEXTMETRICA *a)
{
    a->tmHeight = w->tmHeight;
    a->tmAscent = w->tmAscent;
    a->tmDescent = w->tmDescent;
    a->tmInternalLeading = w->tmInternalLeading;
    a->tmExternalLeading = w->tmExternalLeading;
    a->tmAveCharWidth = w->tmAveCharWidth;
    a->tmMaxCharWidth = w->tmMaxCharWidth;
    a->tmWeight = w->tmWeight;
    a->tmOverhang = w->tmOverhang;
    a->tmDigitizedAspectX = w->tmDigitizedAspectX;
    a->tmDigitizedAspectY = w->tmDigitizedAspectY;
    a->tmFirstChar = w->tmFirstChar > 0xFF ? 0xFF : (BYTE)w->tmFirstChar;
    a->tmLastChar = w->tmLastChar > 0xFF ? 0xFF : (BYTE)w->tmLastChar;
    a->tmDefaultChar = w->tmDefaultChar > 0xFF ? '?' : (BYTE)w->tmDefaultChar;
    a->tmBreakChar = w->tmBreakChar > 0xFF ? ' ' : (BYTE)w->tmBreakChar;
    a->tmItalic = w->tmItalic;
    a->tmUnderlined = w->tmUnderlined;
    a->tmStruckOut = w->tmStruckOut;
    a->tmPitchAndFamily = w->tmPitchAndFamily;
    a->tmCharSet = w->tmCharSet;
}

/* ------------------------------------------------------------------ enumeration */

static MIME_FONT_ENUM_RECORD *enum_fonts(HDC dc, LPCWSTR face, BYTE charset, ULONG *count)
{
    ULONG chars = face ? (ULONG)wcslen(face) : 0;
    if (chars >= LF_FACESIZE)
        chars = LF_FACESIZE - 1;
    ULONG size = 0;
    *count = 0;
    ULONG n = NtGdiEnumFonts(dc, 0, 0, chars, face, charset, &size, nullptr);
    if (!n || !size)
        return nullptr;
    MIME_FONT_ENUM_RECORD *records = heap_alloc(size);
    if (!records)
        return nullptr;
    ULONG got = size;
    n = NtGdiEnumFonts(dc, 0, 0, chars, face, charset, &got, records);
    if (!n || got > size) {
        heap_free(records);
        return nullptr;
    }
    *count = n;
    return records;
}

WINGDIAPI int WINAPI EnumFontFamiliesExW(HDC dc, LPLOGFONTW lf, FONTENUMPROCW proc, LPARAM param, DWORD flags)
{
    if (!lf || !proc)
        return 0;
    ULONG count;
    MIME_FONT_ENUM_RECORD *records = enum_fonts(dc, lf->lfFaceName[0] ? lf->lfFaceName : nullptr, lf->lfCharSet, &count);
    /* the result is the last value the procedure returned, 1 when nothing was enumerated. */
    int result = 1;
    for (ULONG i = 0; i < count; i++) {
        result = proc(&records[i].Font.elfLogFont, (const TEXTMETRICW *)&records[i].Metrics, records[i].Type, param);
        if (!result)
            break;
    }
    heap_free(records);
    (void)flags;
    return result;
}

WINGDIAPI int WINAPI EnumFontFamiliesW(HDC dc, LPCWSTR family, FONTENUMPROCW proc, LPARAM param)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    if (family) {
        int i = 0;
        for (; i < LF_FACESIZE - 1 && family[i]; i++)
            lf.lfFaceName[i] = family[i];
    }
    return EnumFontFamiliesExW(dc, &lf, proc, param, 0);
}

typedef struct ENUM_A_CONTEXT {
    FONTENUMPROCA Proc;
    LPARAM Param;
} ENUM_A_CONTEXT;

static int CALLBACK enum_a_thunk(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    const ENUMLOGFONTEXW *ew = (const ENUMLOGFONTEXW *)lf;
    const NEWTEXTMETRICEXW *nw = (const NEWTEXTMETRICEXW *)tm;
    ENUM_A_CONTEXT *ctx = (ENUM_A_CONTEXT *)param;
    ENUMLOGFONTEXA ea;
    NEWTEXTMETRICEXA na;
    memset(&ea, 0, sizeof(ea));
    memset(&na, 0, sizeof(na));
    memcpy(&ea.elfLogFont, &ew->elfLogFont, offsetof(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_ACP, 0, ew->elfLogFont.lfFaceName, -1, ea.elfLogFont.lfFaceName, LF_FACESIZE, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, ew->elfFullName, -1, (LPSTR)ea.elfFullName, LF_FULLFACESIZE, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, ew->elfStyle, -1, (LPSTR)ea.elfStyle, LF_FACESIZE, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, ew->elfScript, -1, (LPSTR)ea.elfScript, LF_FACESIZE, nullptr, nullptr);
    tm_w_to_a((const TEXTMETRICW *)&nw->ntmTm, (TEXTMETRICA *)&na.ntmTm);
    na.ntmTm.ntmFlags = nw->ntmTm.ntmFlags;
    na.ntmTm.ntmSizeEM = nw->ntmTm.ntmSizeEM;
    na.ntmTm.ntmCellHeight = nw->ntmTm.ntmCellHeight;
    na.ntmTm.ntmAvgWidth = nw->ntmTm.ntmAvgWidth;
    na.ntmFontSig = nw->ntmFontSig;
    return ctx->Proc(&ea.elfLogFont, (const TEXTMETRICA *)&na, type, ctx->Param);
}

WINGDIAPI int WINAPI EnumFontFamiliesExA(HDC dc, LPLOGFONTA lf, FONTENUMPROCA proc, LPARAM param, DWORD flags)
{
    if (!lf || !proc)
        return 0;
    LOGFONTW w;
    memcpy(&w, lf, offsetof(LOGFONTA, lfFaceName));
    MultiByteToWideChar(CP_ACP, 0, lf->lfFaceName, -1, w.lfFaceName, LF_FACESIZE);
    w.lfFaceName[LF_FACESIZE - 1] = 0;
    ENUM_A_CONTEXT ctx = {proc, param};
    return EnumFontFamiliesExW(dc, &w, enum_a_thunk, (LPARAM)&ctx, flags);
}

/* ------------------------------------------------------------------ metrics */

WINGDIAPI DWORD WINAPI GetFontData(HDC dc, DWORD table, DWORD offset, PVOID buffer, DWORD size)
{
    return NtGdiGetFontData(dc, table, offset, buffer, size);
}

/* like windows, the string fields stay offsets from the start of the structure. */
WINGDIAPI UINT WINAPI GetOutlineTextMetricsW(HDC dc, UINT size, LPOUTLINETEXTMETRICW otm)
{
    if (!otm)
        return NtGdiGetOutlineTextMetricsInternalW(dc, 0, nullptr, nullptr);
    UINT needed = NtGdiGetOutlineTextMetricsInternalW(dc, 0, nullptr, nullptr);
    if (!needed)
        return 0;
    if (size >= needed) {
        return NtGdiGetOutlineTextMetricsInternalW(dc, size, otm, nullptr);
    }
    /* a short buffer receives the fixed part; the string offsets point past it then. */
    OUTLINETEXTMETRICW *full = heap_alloc(needed);
    if (!full)
        return 0;
    UINT n = NtGdiGetOutlineTextMetricsInternalW(dc, needed, full, nullptr);
    if (n) {
        memcpy(otm, full, size < sizeof(*full) ? size : sizeof(*full));
        n = size;
    }
    heap_free(full);
    return n;
}

WINGDIAPI UINT WINAPI GetOutlineTextMetricsA(HDC dc, UINT size, LPOUTLINETEXTMETRICA otm)
{
    UINT needed_w = NtGdiGetOutlineTextMetricsInternalW(dc, 0, nullptr, nullptr);
    if (!needed_w)
        return 0;
    OUTLINETEXTMETRICW *w = heap_alloc(needed_w);
    if (!w)
        return 0;
    if (!GetOutlineTextMetricsW(dc, needed_w, w)) {
        heap_free(w);
        return 0;
    }
    LPCWSTR names[4] = {(LPCWSTR)((PUCHAR)w + (ULONG_PTR)w->otmpFamilyName),
                        (LPCWSTR)((PUCHAR)w + (ULONG_PTR)w->otmpFaceName),
                        (LPCWSTR)((PUCHAR)w + (ULONG_PTR)w->otmpStyleName),
                        (LPCWSTR)((PUCHAR)w + (ULONG_PTR)w->otmpFullName)};
    int lengths[4];
    UINT needed = sizeof(OUTLINETEXTMETRICA);
    for (int i = 0; i < 4; i++) {
        lengths[i] = WideCharToMultiByte(CP_ACP, 0, names[i], -1, nullptr, 0, nullptr, nullptr);
        needed += (UINT)lengths[i];
    }
    UINT result = needed;
    if (otm) {
        PUCHAR a = heap_alloc(needed);
        if (!a) {
            heap_free(w);
            return 0;
        }
        OUTLINETEXTMETRICA *out = (OUTLINETEXTMETRICA *)a;
        out->otmSize = needed;
        tm_w_to_a(&w->otmTextMetrics, &out->otmTextMetrics);
        /* the fields after the text metrics are identical. */
        memcpy(&out->otmFiller, &w->otmFiller, offsetof(OUTLINETEXTMETRICW, otmpFamilyName) -
                                                   offsetof(OUTLINETEXTMETRICW, otmFiller));
        UINT offset = sizeof(OUTLINETEXTMETRICA);
        PSTR *fields[4] = {&out->otmpFamilyName, &out->otmpFaceName, &out->otmpStyleName, &out->otmpFullName};
        for (int i = 0; i < 4; i++) {
            WideCharToMultiByte(CP_ACP, 0, names[i], -1, (LPSTR)a + offset, lengths[i], nullptr, nullptr);
            *fields[i] = (PSTR)(ULONG_PTR)offset;
            offset += (UINT)lengths[i];
        }
        UINT n = size < needed ? size : needed;
        memcpy(otm, a, n);
        result = n;
        heap_free(a);
    }
    heap_free(w);
    return result;
}

WINGDIAPI DWORD WINAPI GetFontUnicodeRanges(HDC dc, LPGLYPHSET set)
{
    return NtGdiGetFontUnicodeRanges(dc, set);
}

WINGDIAPI DWORD WINAPI GetGlyphIndicesW(HDC dc, LPCWSTR text, int count, LPWORD indices, DWORD flags)
{
    return NtGdiGetGlyphIndicesW(dc, text, count, indices, flags);
}

WINGDIAPI DWORD WINAPI GetGlyphOutlineW(HDC dc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buffer,
                                        const MAT2 *mat)
{
    if (!mat) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return GDI_ERROR;
    }
    return NtGdiGetGlyphOutline(dc, (WCHAR)ch, format, gm, size, buffer, (LPMAT2)mat, FALSE);
}

WINGDIAPI DWORD WINAPI GetGlyphOutlineA(HDC dc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buffer,
                                        const MAT2 *mat)
{
    if (!(format & GGO_GLYPH_INDEX)) {
        /* a lead byte in the high byte makes a double byte character. */
        char a[2];
        int n = 0;
        if (ch > 0xFF)
            a[n++] = (char)(ch >> 8);
        a[n++] = (char)ch;
        WCHAR w = 0;
        if (!MultiByteToWideChar(CP_ACP, 0, a, n, &w, 1))
            return GDI_ERROR;
        ch = w;
    }
    return GetGlyphOutlineW(dc, ch, format, gm, size, buffer, mat);
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsFloatW(HDC dc, UINT first, UINT last, LPABCFLOAT abc)
{
    if (last < first || !abc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetCharABCWidthsW(dc, first, last - first + 1, nullptr, MIME_GCABCW_FLOAT, abc);
}

WINGDIAPI BOOL WINAPI GetCharABCWidthsI(HDC dc, UINT first, UINT count, LPWORD glyphs, LPABC abc)
{
    if (!abc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetCharABCWidthsW(dc, first, count, (PWCHAR)glyphs, MIME_GCABCW_INDICES, abc);
}

WINGDIAPI BOOL WINAPI GetCharWidthI(HDC dc, UINT first, UINT count, LPWORD glyphs, LPINT widths)
{
    if (!widths) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetCharWidthW(dc, first, count, (PWCHAR)glyphs, MIME_GCW_INDICES, widths);
}

WINGDIAPI BOOL WINAPI GetTextExtentPointI(HDC dc, LPWORD glyphs, int count, LPSIZE size)
{
    if (count < 0 || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiGetTextExtentExW(dc, (LPCWSTR)glyphs, (ULONG)count, 0, nullptr, nullptr, size, MIME_GTEE_INDICES);
}

WINGDIAPI BOOL WINAPI GetCharWidth32A(HDC dc, UINT first, UINT last, LPINT widths)
{
    if (last < first || last - first > 0xFFFF || !widths) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT count = last - first + 1;
    char *a = heap_alloc((SIZE_T)count * 2);
    WCHAR *w = heap_alloc((SIZE_T)count * sizeof(WCHAR));
    BOOL ok = FALSE;
    if (a && w) {
        for (UINT i = 0; i < count; i++) {
            UINT c = first + i;
            char bytes[2];
            int n = 0;
            if (c > 0xFF)
                bytes[n++] = (char)(c >> 8);
            bytes[n++] = (char)c;
            if (!MultiByteToWideChar(CP_ACP, 0, bytes, n, &w[i], 1))
                w[i] = (WCHAR)c;
        }
        ok = NtGdiGetCharWidthW(dc, 0, count, w, 0, widths);
    }
    heap_free(a);
    heap_free(w);
    return ok;
}

/* ------------------------------------------------------------------ character placement */

/*
 * left to right without shaping: glyphs are the font's glyph indices, the
 * order is the identity and widths are the advances.
 */
WINGDIAPI DWORD WINAPI GetCharacterPlacementW(HDC dc, LPCWSTR text, int count, int max_extent,
                                              LPGCP_RESULTSW results, DWORD flags)
{
    if (count < 0 || (count && !text))
        return 0;
    SIZE size = {0, 0};
    int fit = count;
    INT *dx = count ? heap_alloc((SIZE_T)count * sizeof(INT)) : nullptr;
    if (count && !dx)
        return 0;
    if (!GetTextExtentExPointW(dc, text, count, (flags & GCP_MAXEXTENT) ? max_extent : 0,
                               (flags & GCP_MAXEXTENT) ? &fit : nullptr, dx, &size)) {
        heap_free(dx);
        return 0;
    }
    if (!(flags & GCP_MAXEXTENT))
        fit = count;
    if (results) {
        UINT n = results->nGlyphs < (UINT)fit ? results->nGlyphs : (UINT)fit;
        if (results->lpOutString)
            memcpy(results->lpOutString, text, n * sizeof(WCHAR));
        for (UINT i = 0; i < n; i++) {
            if (results->lpOrder)
                results->lpOrder[i] = i;
            if (results->lpDx)
                results->lpDx[i] = dx[i] - (i ? dx[i - 1] : 0);
            if (results->lpCaretPos)
                results->lpCaretPos[i] = i ? dx[i - 1] : 0;
            if (results->lpClass)
                results->lpClass[i] = 0;
        }
        if (results->lpGlyphs && n)
            GetGlyphIndicesW(dc, text, (int)n, (LPWORD)results->lpGlyphs, 0);
        results->nGlyphs = n;
        results->nMaxFit = fit;
        size.cx = fit ? dx[fit - 1] : 0;
    }
    heap_free(dx);
    return (DWORD)MAKELONG(size.cx, size.cy);
}

/* converts the text through the ansi code page, places it with the wide form, and converts the string back. */
WINGDIAPI DWORD WINAPI GetCharacterPlacementA(HDC dc, LPCSTR text, int count, int max_extent,
                                              LPGCP_RESULTSA results, DWORD flags)
{
    if (count < 0 || (count && !text))
        return 0;
    int wide_count = count ? MultiByteToWideChar(CP_ACP, 0, text, count, nullptr, 0) : 0;
    WCHAR *wide = heap_alloc((SIZE_T)(wide_count + 1) * sizeof(WCHAR));
    if (!wide)
        return 0;
    if (wide_count)
        MultiByteToWideChar(CP_ACP, 0, text, count, wide, wide_count);
    GCP_RESULTSW w;
    WCHAR *out = nullptr;
    if (results) {
        w.lStructSize = sizeof(w);
        w.lpOrder = results->lpOrder;
        w.lpDx = results->lpDx;
        w.lpCaretPos = results->lpCaretPos;
        w.lpClass = results->lpClass;
        w.lpGlyphs = results->lpGlyphs;
        w.nGlyphs = results->nGlyphs;
        w.nMaxFit = results->nMaxFit;
        w.lpOutString = out = results->lpOutString ? heap_alloc((SIZE_T)(w.nGlyphs + 1) * sizeof(WCHAR)) : nullptr;
    }
    DWORD r = GetCharacterPlacementW(dc, wide, wide_count, max_extent, results ? &w : nullptr, flags);
    if (results) {
        if (out && r)
            WideCharToMultiByte(CP_ACP, 0, out, (int)w.nGlyphs, results->lpOutString, (int)results->nGlyphs, nullptr,
                                nullptr);
        results->nGlyphs = w.nGlyphs;
        results->nMaxFit = w.nMaxFit;
    }
    heap_free(out);
    heap_free(wide);
    return r;
}

/* each charset, its ansi code page, and its bit in a font signature's code page ranges. */
static const struct {
    UINT charset, codepage, bit;
} CHARSETS[] = {
    {ANSI_CHARSET, 1252, 0},     {EASTEUROPE_CHARSET, 1250, 1}, {RUSSIAN_CHARSET, 1251, 2},
    {GREEK_CHARSET, 1253, 3},    {TURKISH_CHARSET, 1254, 4},    {HEBREW_CHARSET, 1255, 5},
    {ARABIC_CHARSET, 1256, 6},   {BALTIC_CHARSET, 1257, 7},     {VIETNAMESE_CHARSET, 1258, 8},
    {THAI_CHARSET, 874, 16},     {SHIFTJIS_CHARSET, 932, 17},   {GB2312_CHARSET, 936, 18},
    {HANGEUL_CHARSET, 949, 19},  {CHINESEBIG5_CHARSET, 950, 20}, {JOHAB_CHARSET, 1361, 21},
    {SYMBOL_CHARSET, CP_SYMBOL, 31},
};

/* maps a charset, code page, font signature or locale to its charset, code page and signature bit. */
WINGDIAPI BOOL WINAPI TranslateCharsetInfo(DWORD *source, LPCHARSETINFO info, DWORD flags)
{
    if (!info)
        return FALSE;
    UINT value = (UINT)(ULONG_PTR)source;
    int found = -1;
    for (int i = 0; i < (int)ARRAYSIZE(CHARSETS) && found < 0; i++) {
        switch (flags) {
        case TCI_SRCCHARSET:
            if (CHARSETS[i].charset == value)
                found = i;
            break;
        case TCI_SRCCODEPAGE:
            if (CHARSETS[i].codepage == value)
                found = i;
            break;
        case TCI_SRCFONTSIG:
            if (source && (source[0] & (1u << CHARSETS[i].bit)))
                found = i;
            break;
        case TCI_SRCLOCALE: {
            WCHAR cp[8];
            if (GetLocaleInfoW(value, LOCALE_IDEFAULTANSICODEPAGE, cp, ARRAYSIZE(cp))) {
                UINT n = 0;
                for (WCHAR *c = cp; *c >= L'0' && *c <= L'9'; c++)
                    n = n * 10 + (*c - L'0');
                if (CHARSETS[i].codepage == n)
                    found = i;
            }
            break;
        }
        default:
            return FALSE;
        }
    }
    if (found < 0)
        return FALSE;
    memset(info, 0, sizeof(*info));
    info->ciCharset = CHARSETS[found].charset;
    info->ciACP = CHARSETS[found].codepage;
    info->fs.fsCsb[0] = 1u << CHARSETS[found].bit;
    return TRUE;
}

/* ------------------------------------------------------------------ fonts added at run time */

/* win32k opens the file by its nt path. */
static int font_resource(LPCWSTR name, DWORD flags, BOOL add)
{
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(name, &nt, nullptr, nullptr)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    ULONG chars = nt.Length / sizeof(WCHAR);
    int r = add ? NtGdiAddFontResourceW(nt.Buffer, chars, 1, flags, 0, nullptr)
                : NtGdiRemoveFontResourceW(nt.Buffer, chars, 1, flags, 0, nullptr);
    RtlFreeUnicodeString(&nt);
    return r;
}

WINGDIAPI int WINAPI AddFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved)
{
    return font_resource(name, flags, TRUE);
}

WINGDIAPI int WINAPI AddFontResourceW(LPCWSTR name)
{
    return font_resource(name, 0, TRUE);
}

WINGDIAPI BOOL WINAPI RemoveFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved)
{
    return font_resource(name, flags, FALSE);
}

/*
 * a .fot file that holds the name of a font for a program that adds fonts by their resource file. Windows 10 and later
 * write none and fail this call, and so does this machine.
 */
WINGDIAPI BOOL WINAPI CreateScalableFontResourceW(DWORD hidden, LPCWSTR resource, LPCWSTR font, LPCWSTR path)
{
    if (!resource || !font) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlSetLastWin32Error(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * private characters are drawn from the font of the user's choice when this is on, which it is for every session. No
 * machine here has such a font, so a program that switches it gets success and nothing changes.
 */
WINGDIAPI BOOL WINAPI EnableEUDC(BOOL enable)
{
    return TRUE;
}

WINGDIAPI BOOL WINAPI RemoveFontResourceW(LPCWSTR name)
{
    return font_resource(name, 0, FALSE);
}

WINGDIAPI HANDLE WINAPI AddFontMemResourceEx(PVOID data, DWORD size, PVOID reserved, DWORD *count)
{
    if (!data || !size || !count || reserved) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return NtGdiAddFontMemResourceEx(data, size, nullptr, 0, count);
}

WINGDIAPI BOOL WINAPI RemoveFontMemResourceEx(HANDLE handle)
{
    if (!handle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return NtGdiRemoveFontMemResourceEx(handle);
}
