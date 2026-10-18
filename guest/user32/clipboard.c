/*
 * the clipboard client. win32k stores the data of every format; memory
 * formats travel as bytes and come back as global memory owned by the
 * clipboard, cached per process until the contents change. text formats
 * the owner did not store are converted from another text format here.
 */
#include "user32_private.h"

typedef struct CLIP_CACHE {
    UINT Format;
    HGLOBAL Handle;
    BOOL Gdi;       /* the handle is a bitmap */
    DWORD Sequence; /* clipboard sequence number the data belongs to */
} CLIP_CACHE;

#define CACHE_SIZE 32
static CLIP_CACHE g_cache[CACHE_SIZE];

static BOOL is_gdi_format(UINT format)
{
    return format == CF_BITMAP || format == CF_PALETTE || format == CF_ENHMETAFILE || format == CF_DSPBITMAP ||
           format == CF_DSPENHMETAFILE || (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST);
}

static void cache_drop(CLIP_CACHE *c)
{
    if (c->Handle && c->Gdi)
        DeleteObject((HGDIOBJ)c->Handle);
    else if (c->Handle)
        GlobalFree(c->Handle);
    c->Handle = nullptr;
    c->Gdi = FALSE;
    c->Format = 0;
    c->Sequence = 0;
}

static CLIP_CACHE *cache_find(UINT format)
{
    for (int i = 0; i < CACHE_SIZE; i++)
        if (g_cache[i].Handle && g_cache[i].Format == format)
            return &g_cache[i];
    return nullptr;
}

static void cache_store_kind(UINT format, HGLOBAL handle, DWORD sequence, BOOL gdi);

static void cache_store(UINT format, HGLOBAL handle, DWORD sequence)
{
    cache_store_kind(format, handle, sequence, FALSE);
}

static void cache_store_kind(UINT format, HGLOBAL handle, DWORD sequence, BOOL gdi)
{
    CLIP_CACHE *c = cache_find(format);
    if (c && c->Handle != handle)
        cache_drop(c);
    if (!c || !c->Format) {
        c = nullptr;
        for (int i = 0; i < CACHE_SIZE && !c; i++)
            if (!g_cache[i].Handle)
                c = &g_cache[i];
        if (!c) {
            c = &g_cache[0];
            cache_drop(c);
        }
    }
    c->Format = format;
    c->Handle = handle;
    c->Gdi = gdi;
    c->Sequence = sequence;
}

/* ------------------------------------------------------------------ opening and ownership */

WINUSERAPI BOOL WINAPI OpenClipboard(HWND owner)
{
    return NtUserOpenClipboard(owner, 0);
}

WINUSERAPI BOOL WINAPI CloseClipboard(void)
{
    return NtUserCloseClipboard();
}

WINUSERAPI BOOL WINAPI EmptyClipboard(void)
{
    if (!NtUserEmptyClipboard())
        return FALSE;
    /* the clipboard frees the data it owned. */
    for (int i = 0; i < CACHE_SIZE; i++)
        cache_drop(&g_cache[i]);
    return TRUE;
}

WINUSERAPI HWND WINAPI GetClipboardOwner(void)
{
    return NtUserGetClipboardOwner();
}

WINUSERAPI HWND WINAPI GetOpenClipboardWindow(void)
{
    return NtUserGetOpenClipboardWindow();
}

WINUSERAPI DWORD WINAPI GetClipboardSequenceNumber(void)
{
    return NtUserGetClipboardSequenceNumber();
}

/* ------------------------------------------------------------------ data */

WINUSERAPI HANDLE WINAPI SetClipboardData(UINT format, HANDLE data)
{
    MIME_CLIPBOARD_DATA d = {nullptr, 0, 0, FALSE};
    if (!data) {
        /* delayed rendering: the owner gets WM_RENDERFORMAT when the format is read. */
        NtUserSetClipboardData(format, nullptr, nullptr);
        return nullptr;
    }
    if (is_gdi_format(format))
        return NtUserSetClipboardData(format, data, &d) ? data : nullptr;
    SIZE_T size = GlobalSize(data);
    void *p = GlobalLock(data);
    if (!p) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    d.Data = p;
    d.Size = size;
    HANDLE r = NtUserSetClipboardData(format, nullptr, &d);
    GlobalUnlock(data);
    if (!r)
        return nullptr;
    /* the clipboard owns the memory now; this process keeps reading it until the contents change. */
    cache_store(format, data, NtUserGetClipboardSequenceNumber());
    return data;
}

static HGLOBAL fetch_memory(UINT format, DWORD *sequence)
{
    MIME_CLIPBOARD_DATA d = {nullptr, 0, 0, FALSE};
    HANDLE h = NtUserGetClipboardData(format, &d);
    if (!h || d.Gdi)
        return nullptr;
    *sequence = d.Sequence;
    CLIP_CACHE *c = cache_find(format);
    if (c && c->Sequence == d.Sequence)
        return c->Handle;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, d.Size ? d.Size : 1);
    if (!mem)
        return nullptr;
    void *p = GlobalLock(mem);
    d.Data = p;
    h = NtUserGetClipboardData(format, &d);
    GlobalUnlock(mem);
    if (!h) {
        GlobalFree(mem);
        return nullptr;
    }
    cache_store(format, mem, d.Sequence);
    return mem;
}

/* builds a text format from another one the clipboard holds. */
static HANDLE synthesize_text(UINT format)
{
    static const UINT sources[] = {CF_UNICODETEXT, CF_TEXT, CF_OEMTEXT};
    for (int i = 0; i < 3; i++) {
        UINT from = sources[i];
        if (from == format)
            continue;
        DWORD sequence = 0;
        HGLOBAL src = fetch_memory(from, &sequence);
        if (!src)
            continue;
        SIZE_T size = GlobalSize(src);
        const void *p = GlobalLock(src);
        if (!p)
            continue;
        HGLOBAL out = nullptr;
        if (from == CF_UNICODETEXT) {
            const WCHAR *w = p;
            int chars = 0;
            while ((SIZE_T)(chars + 1) * sizeof(WCHAR) <= size && w[chars])
                chars++;
            UINT cp = format == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
            int n = WideCharToMultiByte(cp, 0, w, chars, nullptr, 0, nullptr, nullptr);
            out = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n + 1);
            char *a = out ? GlobalLock(out) : nullptr;
            if (a) {
                WideCharToMultiByte(cp, 0, w, chars, a, n, nullptr, nullptr);
                a[n] = 0;
                GlobalUnlock(out);
            }
        } else {
            const char *a = p;
            int chars = 0;
            while ((SIZE_T)chars < size && a[chars])
                chars++;
            UINT cp = from == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
            if (format == CF_UNICODETEXT) {
                int n = MultiByteToWideChar(cp, 0, a, chars, nullptr, 0);
                out = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)n + 1) * sizeof(WCHAR));
                WCHAR *w = out ? GlobalLock(out) : nullptr;
                if (w) {
                    MultiByteToWideChar(cp, 0, a, chars, w, n);
                    w[n] = 0;
                    GlobalUnlock(out);
                }
            } else {
                /* ansi and oem: through unicode. */
                int n = MultiByteToWideChar(cp, 0, a, chars, nullptr, 0);
                WCHAR *w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
                if (w) {
                    MultiByteToWideChar(cp, 0, a, chars, w, n);
                    UINT to = format == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
                    int m = WideCharToMultiByte(to, 0, w, n, nullptr, 0, nullptr, nullptr);
                    out = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)m + 1);
                    char *b = out ? GlobalLock(out) : nullptr;
                    if (b) {
                        WideCharToMultiByte(to, 0, w, n, b, m, nullptr, nullptr);
                        b[m] = 0;
                        GlobalUnlock(out);
                    }
                    UserFree(w);
                }
            }
        }
        GlobalUnlock(src);
        if (out)
            cache_store(format, out, sequence);
        return out;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ image formats */

#define BI_ALPHABITFIELDS 6

/* the header of a version 5 dib. */
typedef struct DIBV5 {
    DWORD bV5Size;
    LONG bV5Width;
    LONG bV5Height;
    WORD bV5Planes;
    WORD bV5BitCount;
    DWORD bV5Compression;
    DWORD bV5SizeImage;
    LONG bV5XPelsPerMeter;
    LONG bV5YPelsPerMeter;
    DWORD bV5ClrUsed;
    DWORD bV5ClrImportant;
    DWORD bV5RedMask;
    DWORD bV5GreenMask;
    DWORD bV5BlueMask;
    DWORD bV5AlphaMask;
    DWORD bV5CSType;
    LONG bV5Endpoints[9];
    DWORD bV5GammaRed;
    DWORD bV5GammaGreen;
    DWORD bV5GammaBlue;
    DWORD bV5Intent;
    DWORD bV5ProfileData;
    DWORD bV5ProfileSize;
    DWORD bV5Reserved;
} DIBV5;

/* the bytes of the color table of a device independent bitmap, which include the masks of 16 and 32 bit ones. */
static SIZE_T dib_table_size(const BITMAPINFOHEADER *h, SIZE_T available)
{
    if (h->biBitCount <= 8) {
        DWORD colors = h->biClrUsed ? h->biClrUsed : 1u << h->biBitCount;
        return (SIZE_T)colors * 4;
    }
    if (h->biSize == sizeof(BITMAPINFOHEADER) && (h->biBitCount == 16 || h->biBitCount == 32)) {
        if (h->biCompression == BI_BITFIELDS)
            return 12;
        if (h->biCompression == BI_ALPHABITFIELDS)
            return available >= h->biSize + 16 ? 16 : 12;
    }
    return h->biClrUsed ? (SIZE_T)h->biClrUsed * 4 : 0;
}

/* the size of the pixels, the bytes the header says or its rows. */
static SIZE_T dib_pixel_size(const BITMAPINFOHEADER *h)
{
    if (h->biSizeImage && h->biCompression != BI_RGB && h->biCompression != BI_BITFIELDS)
        return h->biSizeImage;
    SIZE_T stride = (((SIZE_T)h->biWidth * h->biBitCount + 31) / 32) * 4;
    LONG height = h->biHeight < 0 ? -h->biHeight : h->biHeight;
    return stride * (SIZE_T)height;
}

/* a version 5 dib as a plain one: the header shrinks, the masks of a bit field dib follow it. */
static HGLOBAL v5_to_dib(const BYTE *in, SIZE_T size)
{
    const DIBV5 *v5 = (const DIBV5 *)in;
    if (size < sizeof(DIBV5) || v5->bV5Size < sizeof(DIBV5))
        return nullptr;
    BITMAPINFOHEADER h;
    memcpy(&h, in, sizeof(h));
    h.biSize = sizeof(BITMAPINFOHEADER);
    BOOL masks = (h.biBitCount == 16 || h.biBitCount == 32) && (h.biCompression == BI_BITFIELDS || h.biCompression == BI_ALPHABITFIELDS);
    if (masks)
        h.biCompression = BI_BITFIELDS;
    SIZE_T palette = h.biBitCount <= 8 ? (h.biClrUsed ? h.biClrUsed : 1u << h.biBitCount) * 4 : 0;
    SIZE_T pixels = dib_pixel_size(&h);
    SIZE_T source = v5->bV5Size + palette;
    if (size < source)
        return nullptr;
    if (pixels > size - source)
        pixels = size - source;
    SIZE_T table = masks ? 12 : palette;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(h) + table + pixels);
    BYTE *out = g ? GlobalLock(g) : nullptr;
    if (!out) {
        if (g)
            GlobalFree(g);
        return nullptr;
    }
    memcpy(out, &h, sizeof(h));
    if (masks) {
        DWORD m[3] = {v5->bV5RedMask, v5->bV5GreenMask, v5->bV5BlueMask};
        memcpy(out + sizeof(h), m, sizeof(m));
    } else if (palette) {
        memcpy(out + sizeof(h), in + v5->bV5Size, palette);
    }
    memcpy(out + sizeof(h) + table, in + source, pixels);
    GlobalUnlock(g);
    return g;
}

/* a plain dib as a version 5 one, in the srgb color space, with the masks in the header. */
static HGLOBAL dib_to_v5(const BYTE *in, SIZE_T size)
{
    const BITMAPINFOHEADER *h = (const BITMAPINFOHEADER *)in;
    if (size < sizeof(*h) || h->biSize != sizeof(BITMAPINFOHEADER))
        return nullptr;
    SIZE_T table = dib_table_size(h, size);
    if (size < sizeof(*h) + table)
        return nullptr;
    SIZE_T rest = size - sizeof(*h) - table;
    DIBV5 v5;
    memset(&v5, 0, sizeof(v5));
    memcpy(&v5, h, sizeof(*h));
    v5.bV5Size = sizeof(v5);
    v5.bV5CSType = 0x73524742; /* LCS_sRGB */
    v5.bV5Intent = 4;          /* LCS_GM_IMAGES */
    BOOL masks = h->biCompression == BI_BITFIELDS && (h->biBitCount == 16 || h->biBitCount == 32);
    if (masks) {
        const DWORD *m = (const DWORD *)(in + sizeof(*h));
        v5.bV5RedMask = m[0];
        v5.bV5GreenMask = m[1];
        v5.bV5BlueMask = m[2];
    }
    SIZE_T palette = masks ? 0 : table;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(v5) + palette + rest);
    BYTE *out = g ? GlobalLock(g) : nullptr;
    if (!out) {
        if (g)
            GlobalFree(g);
        return nullptr;
    }
    memcpy(out, &v5, sizeof(v5));
    memcpy(out + sizeof(v5), in + sizeof(*h), palette);
    memcpy(out + sizeof(v5) + palette, in + sizeof(*h) + table, rest);
    GlobalUnlock(g);
    return g;
}

/* a device dependent bitmap of the display's format from a dib. */
static HBITMAP dib_to_bitmap(const BYTE *in, SIZE_T size)
{
    const BITMAPINFOHEADER *h = (const BITMAPINFOHEADER *)in;
    if (size < sizeof(*h) || h->biSize != sizeof(BITMAPINFOHEADER))
        return nullptr;
    SIZE_T table = dib_table_size(h, size);
    if (size < sizeof(*h) + table)
        return nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBitmap(dc, h, CBM_INIT, in + sizeof(*h) + table, (const BITMAPINFO *)in, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return bitmap;
}

/* a dib of a bitmap: the depth of the bitmap, up to 32 bits, with masks for the 16 and 32 bit ones. */
static HGLOBAL bitmap_to_dib(HBITMAP bitmap)
{
    BITMAP info;
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return nullptr;
    UINT depth = (UINT)info.bmPlanes * info.bmBitsPixel;
    WORD bits = depth <= 1 ? 1 : depth <= 4 ? 4 : depth <= 8 ? 8 : depth <= 16 ? 16 : depth <= 24 ? 24 : 32;
    BOOL masks = bits == 16 || bits == 32;
    SIZE_T table = bits <= 8 ? (SIZE_T)4 << bits : masks ? 12 : 0;
    SIZE_T stride = (((SIZE_T)info.bmWidth * bits + 31) / 32) * 4;
    SIZE_T pixels = stride * (SIZE_T)info.bmHeight;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + table + pixels);
    BYTE *out = g ? GlobalLock(g) : nullptr;
    if (!out) {
        if (g)
            GlobalFree(g);
        return nullptr;
    }
    memset(out, 0, sizeof(BITMAPINFOHEADER) + table);
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)out;
    h->biSize = sizeof(*h);
    h->biWidth = info.bmWidth;
    h->biHeight = info.bmHeight;
    h->biPlanes = 1;
    h->biBitCount = bits;
    h->biCompression = masks ? BI_BITFIELDS : BI_RGB;
    HDC dc = GetDC(nullptr);
    /* the masks need the header first: the bits of a 16 or 32 bit dib are read with the header's own compression. */
    BYTE *bits_at = out + sizeof(*h) + table;
    int lines = GetDIBits(dc, bitmap, 0, (UINT)info.bmHeight, bits_at, (BITMAPINFO *)out, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != info.bmHeight) {
        GlobalUnlock(g);
        GlobalFree(g);
        return nullptr;
    }
    h->biCompression = masks ? BI_BITFIELDS : BI_RGB;
    h->biSizeImage = (DWORD)pixels;
    if (bits == 32) {
        DWORD m[3] = {0x00FF0000, 0x0000FF00, 0x000000FF};
        memcpy(out + sizeof(*h), m, sizeof(m));
    } else if (bits == 16) {
        DWORD m[3] = {0xF800, 0x07E0, 0x001F};
        memcpy(out + sizeof(*h), m, sizeof(m));
    }
    GlobalUnlock(g);
    return g;
}

/* an image format built from another one the clipboard holds. */
static HANDLE synthesize_image(UINT format)
{
    /* the first source is the one that needs the least conversion. */
    UINT preferred[2];
    if (format == CF_DIB) {
        preferred[0] = CF_DIBV5;
        preferred[1] = CF_BITMAP;
    } else if (format == CF_DIBV5) {
        preferred[0] = CF_DIB;
        preferred[1] = CF_BITMAP;
    } else {
        preferred[0] = CF_DIBV5;
        preferred[1] = CF_DIB;
    }
    for (int i = 0; i < 2; i++) {
        UINT from = preferred[i];
        HANDLE result = nullptr;
        DWORD sequence = 0;
        if (from == CF_BITMAP) {
            MIME_CLIPBOARD_DATA d = {nullptr, 0, 0, FALSE};
            HANDLE bitmap = NtUserGetClipboardData(CF_BITMAP, &d);
            if (!bitmap || !d.Gdi)
                continue;
            sequence = d.Sequence;
            HGLOBAL dib = bitmap_to_dib((HBITMAP)bitmap);
            if (!dib)
                continue;
            if (format == CF_DIB) {
                result = dib;
            } else {
                const BYTE *p = GlobalLock(dib);
                result = p ? dib_to_v5(p, GlobalSize(dib)) : nullptr;
                GlobalUnlock(dib);
                GlobalFree(dib);
            }
        } else {
            HGLOBAL src = fetch_memory(from, &sequence);
            if (!src)
                continue;
            const BYTE *p = GlobalLock(src);
            SIZE_T size = GlobalSize(src);
            if (!p)
                continue;
            if (from == CF_DIBV5 && format == CF_DIB)
                result = v5_to_dib(p, size);
            else if (from == CF_DIB && format == CF_DIBV5)
                result = dib_to_v5(p, size);
            else if (format == CF_BITMAP) {
                HGLOBAL plain = from == CF_DIBV5 ? v5_to_dib(p, size) : nullptr;
                const BYTE *q = plain ? GlobalLock(plain) : p;
                result = q ? (HANDLE)dib_to_bitmap(q, plain ? GlobalSize(plain) : size) : nullptr;
                if (plain) {
                    GlobalUnlock(plain);
                    GlobalFree(plain);
                }
            }
            GlobalUnlock(src);
        }
        if (result) {
            cache_store_kind(format, result, sequence, format == CF_BITMAP);
            return result;
        }
    }
    return nullptr;
}

WINUSERAPI HANDLE WINAPI GetClipboardData(UINT format)
{
    MIME_CLIPBOARD_DATA d = {nullptr, 0, 0, FALSE};
    HANDLE h = NtUserGetClipboardData(format, &d);
    if (h && d.Gdi)
        return h;
    if (h) {
        DWORD sequence;
        return fetch_memory(format, &sequence);
    }
    if ((format == CF_TEXT || format == CF_OEMTEXT || format == CF_UNICODETEXT) &&
        NtUserIsClipboardFormatAvailable(format)) {
        CLIP_CACHE *c = cache_find(format);
        if (c && c->Sequence == NtUserGetClipboardSequenceNumber())
            return c->Handle;
        return synthesize_text(format);
    }
    if ((format == CF_BITMAP || format == CF_DIB || format == CF_DIBV5) && NtUserIsClipboardFormatAvailable(format)) {
        CLIP_CACHE *c = cache_find(format);
        if (c && c->Sequence == NtUserGetClipboardSequenceNumber())
            return c->Handle;
        return synthesize_image(format);
    }
    return nullptr;
}

WINUSERAPI BOOL WINAPI IsClipboardFormatAvailable(UINT format)
{
    return NtUserIsClipboardFormatAvailable(format);
}

WINUSERAPI int WINAPI CountClipboardFormats(void)
{
    return NtUserCountClipboardFormats();
}

WINUSERAPI UINT WINAPI EnumClipboardFormats(UINT format)
{
    return NtUserEnumClipboardFormats(format);
}

WINUSERAPI int WINAPI GetPriorityClipboardFormat(UINT *formats, int count)
{
    if (!CountClipboardFormats())
        return 0;
    for (int i = 0; i < count; i++)
        if (IsClipboardFormatAvailable(formats[i]))
            return (int)formats[i];
    return -1;
}

/* ------------------------------------------------------------------ registered formats */

WINUSERAPI UINT WINAPI RegisterClipboardFormatW(LPCWSTR name)
{
    if (!name || !*name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* registered formats share the atom table of registered window messages, like windows. */
    UNICODE_STRING s;
    RtlInitUnicodeString(&s, name);
    return NtUserRegisterWindowMessage(&s);
}

WINUSERAPI UINT WINAPI RegisterClipboardFormatA(LPCSTR name)
{
    LPWSTR w = UserAtoW(name);
    UINT r = RegisterClipboardFormatW(w);
    UserFreeConverted(w, name);
    return r;
}

WINUSERAPI int WINAPI GetClipboardFormatNameW(UINT format, LPWSTR name, int max)
{
    return NtUserGetClipboardFormatName(format, name, max);
}

WINUSERAPI int WINAPI GetClipboardFormatNameA(UINT format, LPSTR name, int max)
{
    WCHAR w[256];
    if (!name || max <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    int n = NtUserGetClipboardFormatName(format, w, RTL_NUMBER_OF(w));
    if (!n)
        return 0;
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, name, max - 1, nullptr, nullptr);
    name[a] = 0;
    return a;
}

/* ------------------------------------------------------------------ viewers and listeners */

WINUSERAPI HWND WINAPI GetClipboardViewer(void)
{
    return NtUserGetClipboardViewer();
}

WINUSERAPI HWND WINAPI SetClipboardViewer(HWND viewer)
{
    return NtUserSetClipboardViewer(viewer);
}

WINUSERAPI BOOL WINAPI ChangeClipboardChain(HWND remove, HWND next)
{
    return NtUserChangeClipboardChain(remove, next);
}

WINUSERAPI BOOL WINAPI AddClipboardFormatListener(HWND hwnd)
{
    return NtUserAddClipboardFormatListener(hwnd);
}

WINUSERAPI BOOL WINAPI RemoveClipboardFormatListener(HWND hwnd)
{
    return NtUserRemoveClipboardFormatListener(hwnd);
}
