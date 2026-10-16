/*
 * the visual style of the common controls: parts of the Button class of
 * aero.msstyles drawn the way uxtheme draws them, with 9 grid stretching of
 * sizing margins and true size images.
 */
#include "user32_private.h"
#include <msstyles/msstyles.h>
#include <png/png.h>

static void *theme_alloc(void *ctx, size_t size)
{
    return UserAlloc(size);
}

static void theme_free(void *ctx, void *p)
{
    UserFree(p);
}

static const MimeAllocator g_alloc = {theme_alloc, theme_free, nullptr};

typedef struct THEME_BITMAP {
    struct THEME_BITMAP *Next;
    int32_t ResId;
    HBITMAP Bitmap;
    int Width, Height;
} THEME_BITMAP;

static struct {
    RTL_RUN_ONCE Once;
    BOOL Ready;
    MimeMsstyles Theme;
    int Button;
    THEME_BITMAP *Bitmaps;
    RTL_CRITICAL_SECTION Lock;
} g_theme;

static PUCHAR read_file(LPCWSTR path, SIZE_T *size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD high = 0, n = GetFileSize(h, &high), read = 0;
    PUCHAR data = n && !high ? UserAlloc(n) : nullptr;
    if (data && (!ReadFile(h, data, n, &read, nullptr) || read != n)) {
        UserFree(data);
        data = nullptr;
    }
    CloseHandle(h);
    if (data)
        *size = n;
    return data;
}

static ULONG NTAPI load_theme(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    WCHAR path[MAX_PATH], mui[MAX_PATH];
    UINT n = GetWindowsDirectoryW(path, MAX_PATH - 64);
    if (!n)
        return TRUE;
    wcscpy(mui, path);
    wcscpy(path + n, L"\\Resources\\Themes\\aero\\aero.msstyles");
    wcscpy(mui + n, L"\\Resources\\Themes\\aero\\en-US\\aero.msstyles.mui");
    SIZE_T size = 0, mui_size = 0;
    PUCHAR file = read_file(path, &size);
    PUCHAR mui_data = read_file(mui, &mui_size);
    if (!file)
        return TRUE;
    if (!mime_msstyles_open(&g_theme.Theme, file, size, mui_data, mui_data ? mui_size : 0, &g_alloc))
        return TRUE;
    g_theme.Button = mime_msstyles_find_class(&g_theme.Theme, nullptr, "Button");
    RtlInitializeCriticalSection(&g_theme.Lock);
    g_theme.Ready = g_theme.Button >= 0;
    return TRUE;
}

BOOL UserThemeActive(void)
{
    RtlRunOnceExecuteOnce(&g_theme.Once, load_theme, nullptr, nullptr);
    return g_theme.Ready;
}

/* decoded theme images, premultiplied, kept for the life of the process. */
static THEME_BITMAP *theme_bitmap(const MimeMsstylesImage *img)
{
    RtlEnterCriticalSection(&g_theme.Lock);
    THEME_BITMAP *b = g_theme.Bitmaps;
    while (b && b->ResId != img->res_id)
        b = b->Next;
    RtlLeaveCriticalSection(&g_theme.Lock);
    if (b)
        return b;
    if (!mime_png_is_png(img->data, img->size))
        return nullptr;
    MimePngInfo info;
    uint8_t *pixels;
    if (mime_png_decode_alloc(img->data, img->size, &g_alloc, MIME_PNG_PREMULTIPLIED, &pixels, &info) != MIME_PNG_OK)
        return nullptr;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)info.width;
    bi.bmiHeader.biHeight = -(LONG)info.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bm)
        memcpy(bits, pixels, (SIZE_T)info.width * info.height * 4);
    UserFree(pixels);
    if (!bm)
        return nullptr;
    b = UserAlloc(sizeof(*b));
    if (!b) {
        DeleteObject(bm);
        return nullptr;
    }
    b->ResId = img->res_id;
    b->Bitmap = bm;
    b->Width = (int)info.width;
    b->Height = (int)info.height;
    RtlEnterCriticalSection(&g_theme.Lock);
    b->Next = g_theme.Bitmaps;
    g_theme.Bitmaps = b;
    RtlLeaveCriticalSection(&g_theme.Lock);
    return b;
}

static void blend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh)
{
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dst, x, y, w, h, src, sx, sy, sw, sh, bf);
}

static BOOL get_image(int part, int state, UINT dpi, const RECT *rect, MimeMsstylesImage *img)
{
    if (!UserThemeActive())
        return FALSE;
    return mime_msstyles_get_image(&g_theme.Theme, g_theme.Button, part, state, dpi, rect ? rect->right - rect->left : 0,
                                   rect ? rect->bottom - rect->top : 0, 0, img);
}

BOOL UserThemeDrawBackground(HDC dc, int part, int state, const RECT *rect, UINT dpi)
{
    MimeMsstylesImage img;
    if (!get_image(part, state, dpi, rect, &img))
        return FALSE;
    THEME_BITMAP *b = theme_bitmap(&img);
    if (!b)
        return FALSE;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, b->Bitmap);
    int sx = img.src.left, sy = img.src.top;
    int sw = img.src.right - img.src.left, sh = img.src.bottom - img.src.top;
    int x = rect->left, y = rect->top, w = rect->right - rect->left, h = rect->bottom - rect->top;

    if (img.sizing_type == MIME_ST_TRUESIZE) {
        int dw = img.draw_width ? img.draw_width : sw, dh = img.draw_height ? img.draw_height : sh;
        int dx = x, dy = y;
        if (img.halign == MIME_HA_CENTER)
            dx = x + (w - dw) / 2;
        else if (img.halign == MIME_HA_RIGHT)
            dx = x + w - dw;
        if (img.valign == MIME_VA_CENTER)
            dy = y + (h - dh) / 2;
        else if (img.valign == MIME_VA_BOTTOM)
            dy = y + h - dh;
        blend(dc, dx, dy, dw, dh, mem, sx, sy, sw, sh);
    } else {
        /* nine grid: corners at their size, edges and center stretched. */
        int l = img.sizing_margins.left, r = img.sizing_margins.right;
        int t = img.sizing_margins.top, bo = img.sizing_margins.bottom;
        if (l + r > w) {
            l = w * l / (l + r);
            r = w - l;
        }
        if (t + bo > h) {
            t = h * t / (t + bo);
            bo = h - t;
        }
        int sl = img.sizing_margins.left, sr = img.sizing_margins.right;
        int st = img.sizing_margins.top, sb = img.sizing_margins.bottom;
        int cw = w - l - r, ch = h - t - bo, scw = sw - sl - sr, sch = sh - st - sb;
        blend(dc, x, y, l, t, mem, sx, sy, sl, st);
        blend(dc, x + l, y, cw, t, mem, sx + sl, sy, scw, st);
        blend(dc, x + w - r, y, r, t, mem, sx + sw - sr, sy, sr, st);
        blend(dc, x, y + t, l, ch, mem, sx, sy + st, sl, sch);
        if (!img.border_only)
            blend(dc, x + l, y + t, cw, ch, mem, sx + sl, sy + st, scw, sch);
        blend(dc, x + w - r, y + t, r, ch, mem, sx + sw - sr, sy + st, sr, sch);
        blend(dc, x, y + h - bo, l, bo, mem, sx, sy + sh - sb, sl, sb);
        blend(dc, x + l, y + h - bo, cw, bo, mem, sx + sl, sy + sh - sb, scw, sb);
        blend(dc, x + w - r, y + h - bo, r, bo, mem, sx + sw - sr, sy + sh - sb, sr, sb);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    return TRUE;
}

BOOL UserThemeContentRect(int part, int state, const RECT *rect, RECT *content, UINT dpi)
{
    MimeMsstylesImage img;
    *content = *rect;
    if (!get_image(part, state, dpi, rect, &img))
        return FALSE;
    content->left += img.content_margins.left;
    content->right -= img.content_margins.right;
    content->top += img.content_margins.top;
    content->bottom -= img.content_margins.bottom;
    return TRUE;
}

BOOL UserThemePartSize(int part, int state, UINT dpi, SIZE *size)
{
    MimeMsstylesImage img;
    if (!get_image(part, state, dpi, nullptr, &img))
        return FALSE;
    size->cx = img.draw_width ? img.draw_width : img.src.right - img.src.left;
    size->cy = img.draw_height ? img.draw_height : img.src.bottom - img.src.top;
    return TRUE;
}

COLORREF UserThemeTextColor(int part, int state, COLORREF fallback)
{
    uint32_t c;
    if (UserThemeActive() &&
        mime_msstyles_get_color(&g_theme.Theme, g_theme.Button, part, state, MIME_TMT_TEXTCOLOR, &c))
        return (COLORREF)c;
    return fallback;
}
