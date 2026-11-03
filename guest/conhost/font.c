/*
 * the font of the console window. a window draws one cell at a time, so any monospaced TrueType face does: Consolas
 * when the machine has it, then Cascadia Mono, Lucida Console and Courier New. with none of them the window draws its
 * own 8 by 16 bitmap font, which has the letters of Latin, Greek and Cyrillic and the box drawing characters.
 */
#include "conhost.h"
#include "rasterfont.h"

int g_font_cell_w = 8, g_font_cell_h = 16;
WCHAR g_font_face[32];
BOOL g_font_raster;

static HFONT g_fonts[4]; /* regular, bold, italic, bold italic */
static HDC g_atlas_dc;
static HBITMAP g_atlas, g_atlas_old;
static int g_scale = 1;

static const WCHAR *const g_faces[] = {L"Consolas", L"Cascadia Mono", L"Lucida Console", L"Courier New"};

static int CALLBACK face_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM parameter)
{
    (void)tm;
    (void)type;
    const WCHAR *wanted = (const WCHAR *)parameter;
    if (!lstrcmpiW(lf->lfFaceName, wanted))
        return 0;
    return 1;
}

static BOOL face_exists(HDC dc, const WCHAR *face)
{
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    /* the callback returns 0 when it finds the face, which ends the enumeration with that result. */
    return EnumFontFamiliesExW(dc, &lf, face_found, (LPARAM)face, 0) == 0;
}

static HFONT make_font(const WCHAR *face, int height, BOOL bold, BOOL italic)
{
    return CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, face);
}

static int glyph_index(ULONG ch)
{
    int lo = 0, hi = RASTER_GLYPHS - 1;
    if (ch > 0xFFFF)
        return -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_raster_codes[mid] == ch)
            return mid;
        if (g_raster_codes[mid] < ch)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static BOOL open_raster(HDC dc)
{
    /* the glyphs side by side in a bitmap of one bit a pixel, where a clear bit is ink: a copy to a color device paints
     * the clear bits with the text color and the set ones with the background color. */
    int width = RASTER_GLYPHS * 8;
    int stride = ((width + 15) / 16) * 2;
    BYTE *bits = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)stride * 16);
    if (!bits)
        return FALSE;
    for (int y = 0; y < 16; y++) {
        for (int k = 0; k < RASTER_GLYPHS; k++)
            bits[y * stride + k] = (BYTE)~g_raster_bits[k][y];
        for (int k = RASTER_GLYPHS; k < stride; k++)
            bits[y * stride + k] = 0xFF;
    }
    g_atlas = CreateBitmap(width, 16, 1, 1, bits);
    HeapFree(GetProcessHeap(), 0, bits);
    if (!g_atlas)
        return FALSE;
    g_atlas_dc = CreateCompatibleDC(dc);
    g_atlas_old = (HBITMAP)SelectObject(g_atlas_dc, g_atlas);
    g_font_raster = TRUE;
    g_font_cell_w = 8 * g_scale;
    g_font_cell_h = 16 * g_scale;
    lstrcpyW(g_font_face, L"Terminal");
    return TRUE;
}

/* the face and the cell height the user chose, as HKEY_CURRENT_USER\\Console keeps them. */
static void user_choice(WCHAR *face, DWORD *cell_height)
{
    HKEY key;
    face[0] = 0;
    *cell_height = 16;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Console", 0, KEY_QUERY_VALUE, &key))
        return;
    DWORD type, size = (DWORD)(LF_FACESIZE * sizeof(WCHAR));
    if (RegQueryValueExW(key, L"FaceName", nullptr, &type, (BYTE *)face, &size) || type != REG_SZ)
        face[0] = 0;
    DWORD value = 0;
    size = sizeof(value);
    if (!RegQueryValueExW(key, L"FontSize", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD && (value >> 16) >= 8 &&
        (value >> 16) <= 72)
        *cell_height = value >> 16;
    RegCloseKey(key);
}

BOOL FontOpen(int dpi)
{
    FontClose();
    HDC dc = GetDC(nullptr);
    WCHAR chosen[LF_FACESIZE];
    DWORD cell_height;
    user_choice(chosen, &cell_height);
    int height = MulDiv((int)cell_height, dpi, 96);
    g_scale = (dpi + 48) / 96;
    if (g_scale < 1)
        g_scale = 1;
    /* the face the user named comes first, as a name of the list at the end of it. */
    const WCHAR *faces[RTL_NUMBER_OF(g_faces) + 1];
    int face_count = 0;
    if (chosen[0])
        faces[face_count++] = chosen;
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_faces); i++)
        faces[face_count++] = g_faces[i];
    /* "Terminal" is the raster font of Windows, which here is the bitmap font of the window. */
    if (!lstrcmpiW(chosen, L"Terminal")) {
        BOOL ok = open_raster(dc);
        ReleaseDC(nullptr, dc);
        return ok;
    }
    for (int i = 0; i < face_count; i++) {
        if (!face_exists(dc, faces[i]))
            continue;
        HFONT regular = make_font(faces[i], height, FALSE, FALSE);
        if (!regular)
            continue;
        HGDIOBJ old = SelectObject(dc, regular);
        TEXTMETRICW tm;
        SIZE size;
        GetTextMetricsW(dc, &tm);
        GetTextExtentPoint32W(dc, L"MMMMMMMMMM", 10, &size);
        SelectObject(dc, old);
        g_fonts[0] = regular;
        g_fonts[1] = make_font(faces[i], height, TRUE, FALSE);
        g_fonts[2] = make_font(faces[i], height, FALSE, TRUE);
        g_fonts[3] = make_font(faces[i], height, TRUE, TRUE);
        g_font_cell_w = (size.cx + 5) / 10;
        g_font_cell_h = tm.tmHeight;
        if (g_font_cell_w < 4 || g_font_cell_h < 8)
            g_font_cell_w = 8 * g_scale, g_font_cell_h = 16 * g_scale;
        lstrcpynW(g_font_face, faces[i], RTL_NUMBER_OF(g_font_face));
        g_font_raster = FALSE;
        ReleaseDC(nullptr, dc);
        return TRUE;
    }
    BOOL ok = open_raster(dc);
    ReleaseDC(nullptr, dc);
    return ok;
}

void FontClose(void)
{
    for (int i = 0; i < 4; i++) {
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
        g_fonts[i] = nullptr;
    }
    if (g_atlas_dc) {
        SelectObject(g_atlas_dc, g_atlas_old);
        DeleteDC(g_atlas_dc);
        g_atlas_dc = nullptr;
    }
    if (g_atlas) {
        DeleteObject(g_atlas);
        g_atlas = nullptr;
    }
    g_font_raster = FALSE;
}

/* a glyph the bitmap font does not have is a box. */
static void draw_missing(HDC dc, int x, int y, int width, COLORREF fg)
{
    HBRUSH brush = CreateSolidBrush(fg);
    int t = g_scale;
    RECT r = {x + t, y + 2 * t, x + width - t, y + g_font_cell_h - 2 * t};
    FrameRect(dc, &r, brush);
    DeleteObject(brush);
}

static void draw_lines(HDC dc, int x, int y, int width, COLORREF fg, UINT style)
{
    HBRUSH brush = CreateSolidBrush(fg);
    int thick = g_font_cell_h >= 24 ? g_font_cell_h / 16 : 1;
    unsigned underline = (style & MIME_CONDRV_CELL_UNDERLINE_MASK) >> MIME_CONDRV_CELL_UNDERLINE_SHIFT;
    if (underline == 0 && (style & MIME_CONDRV_CELL_UNDERSCORE))
        underline = 1;
    if (underline) {
        RECT r = {x, y + g_font_cell_h - 1 - thick, x + width, y + g_font_cell_h - 1};
        FillRect(dc, &r, brush);
        if (underline == 2) {
            RECT r2 = {x, y + g_font_cell_h - 1 - 3 * thick, x + width, y + g_font_cell_h - 1 - 2 * thick};
            FillRect(dc, &r2, brush);
        }
    }
    if (style & MIME_CONDRV_CELL_STRIKE) {
        RECT r = {x, y + g_font_cell_h / 2, x + width, y + g_font_cell_h / 2 + thick};
        FillRect(dc, &r, brush);
    }
    if (style & MIME_CONDRV_CELL_OVERLINE) {
        RECT r = {x, y, x + width, y + thick};
        FillRect(dc, &r, brush);
    }
    if (style & 0x0800) {
        RECT r = {x, y, x + thick, y + g_font_cell_h};
        FillRect(dc, &r, brush);
    }
    if (style & 0x1000) {
        RECT r = {x + width - thick, y, x + width, y + g_font_cell_h};
        FillRect(dc, &r, brush);
    }
    DeleteObject(brush);
}

/* draws a run of cells that share their colors and style, from x and y of the device context. */
void FontDrawRun(HDC dc, int x, int y, const Cell *cells, int count, COLORREF fg, COLORREF bg, UINT style)
{
    int width = 0;
    for (int i = 0; i < count; i++)
        width += g_font_cell_w;
    BOOL conceal = (style & MIME_CONDRV_CELL_CONCEAL) != 0;
    if (g_font_raster) {
        SetTextColor(dc, fg);
        SetBkColor(dc, bg);
        for (int i = 0; i < count; i++) {
            int cx = x + i * g_font_cell_w;
            const Cell *c = &cells[i];
            int k = -1;
            if (c->Flags & MIME_CONDRV_CELL_TRAIL)
                continue;
            ULONG ch = c->Char;
            int span = (c->Flags & MIME_CONDRV_CELL_LEAD) ? 2 : 1;
            if (ch < 0x20 || conceal)
                ch = ' ';
            if (span == 1)
                k = glyph_index(ch);
            if (k >= 0) {
                if (g_scale == 1)
                    BitBlt(dc, cx, y, 8, 16, g_atlas_dc, k * 8, 0, SRCCOPY);
                else
                    StretchBlt(dc, cx, y, 8 * g_scale, 16 * g_scale, g_atlas_dc, k * 8, 0, 8, 16, SRCCOPY);
            } else {
                RECT r = {cx, y, cx + span * g_font_cell_w, y + g_font_cell_h};
                HBRUSH back = CreateSolidBrush(bg);
                FillRect(dc, &r, back);
                DeleteObject(back);
                if (ch != ' ')
                    draw_missing(dc, cx, y, span * g_font_cell_w, fg);
            }
        }
    } else {
        WCHAR text[1024];
        int dx[1024];
        int n = 0;
        RECT rc = {x, y, x + width, y + g_font_cell_h};
        for (int i = 0; i < count && n < 1020; i++) {
            const Cell *c = &cells[i];
            if (c->Flags & MIME_CONDRV_CELL_TRAIL) {
                /* the lead half drew both. */
                continue;
            }
            ULONG ch = c->Char;
            int span = (c->Flags & MIME_CONDRV_CELL_LEAD) ? 2 : 1;
            if (ch < 0x20 || conceal)
                ch = ' ';
            if (ch >= 0x10000) {
                ch -= 0x10000;
                text[n] = (WCHAR)(0xD800 + (ch >> 10));
                dx[n++] = span * g_font_cell_w;
                text[n] = (WCHAR)(0xDC00 + (ch & 0x3FF));
                dx[n++] = 0;
            } else {
                text[n] = (WCHAR)ch;
                dx[n++] = span * g_font_cell_w;
            }
        }
        int variant = ((style & MIME_CONDRV_CELL_BOLD) ? 1 : 0) | ((style & MIME_CONDRV_CELL_ITALIC) ? 2 : 0);
        HGDIOBJ old = SelectObject(dc, g_fonts[variant] ? g_fonts[variant] : g_fonts[0]);
        SetTextColor(dc, fg);
        SetBkColor(dc, bg);
        ExtTextOutW(dc, x, y, ETO_OPAQUE, &rc, text, (UINT)n, dx);
        SelectObject(dc, old);
    }
    draw_lines(dc, x, y, width, fg, style);
}
