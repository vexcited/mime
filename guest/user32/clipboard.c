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
    if (c->Handle)
        GlobalFree(c->Handle);
    c->Handle = NULL;
    c->Format = 0;
    c->Sequence = 0;
}

static CLIP_CACHE *cache_find(UINT format)
{
    for (int i = 0; i < CACHE_SIZE; i++)
        if (g_cache[i].Handle && g_cache[i].Format == format)
            return &g_cache[i];
    return NULL;
}

static void cache_store(UINT format, HGLOBAL handle, DWORD sequence)
{
    CLIP_CACHE *c = cache_find(format);
    if (c && c->Handle != handle)
        cache_drop(c);
    if (!c || !c->Format) {
        c = NULL;
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
    MIME_CLIPBOARD_DATA d = {NULL, 0, 0, FALSE};
    if (!data) {
        /* delayed rendering: the owner gets WM_RENDERFORMAT when the format is read. */
        NtUserSetClipboardData(format, NULL, NULL);
        return NULL;
    }
    if (is_gdi_format(format))
        return NtUserSetClipboardData(format, data, &d) ? data : NULL;
    SIZE_T size = GlobalSize(data);
    void *p = GlobalLock(data);
    if (!p) {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    d.Data = p;
    d.Size = size;
    HANDLE r = NtUserSetClipboardData(format, NULL, &d);
    GlobalUnlock(data);
    if (!r)
        return NULL;
    /* the clipboard owns the memory now; this process keeps reading it until the contents change. */
    cache_store(format, data, NtUserGetClipboardSequenceNumber());
    return data;
}

static HGLOBAL fetch_memory(UINT format, DWORD *sequence)
{
    MIME_CLIPBOARD_DATA d = {NULL, 0, 0, FALSE};
    HANDLE h = NtUserGetClipboardData(format, &d);
    if (!h || d.Gdi)
        return NULL;
    *sequence = d.Sequence;
    CLIP_CACHE *c = cache_find(format);
    if (c && c->Sequence == d.Sequence)
        return c->Handle;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, d.Size ? d.Size : 1);
    if (!mem)
        return NULL;
    void *p = GlobalLock(mem);
    d.Data = p;
    h = NtUserGetClipboardData(format, &d);
    GlobalUnlock(mem);
    if (!h) {
        GlobalFree(mem);
        return NULL;
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
        HGLOBAL out = NULL;
        if (from == CF_UNICODETEXT) {
            const WCHAR *w = p;
            int chars = 0;
            while ((SIZE_T)(chars + 1) * sizeof(WCHAR) <= size && w[chars])
                chars++;
            UINT cp = format == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
            int n = WideCharToMultiByte(cp, 0, w, chars, NULL, 0, NULL, NULL);
            out = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n + 1);
            char *a = out ? GlobalLock(out) : NULL;
            if (a) {
                WideCharToMultiByte(cp, 0, w, chars, a, n, NULL, NULL);
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
                int n = MultiByteToWideChar(cp, 0, a, chars, NULL, 0);
                out = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)n + 1) * sizeof(WCHAR));
                WCHAR *w = out ? GlobalLock(out) : NULL;
                if (w) {
                    MultiByteToWideChar(cp, 0, a, chars, w, n);
                    w[n] = 0;
                    GlobalUnlock(out);
                }
            } else {
                /* ansi and oem: through unicode. */
                int n = MultiByteToWideChar(cp, 0, a, chars, NULL, 0);
                WCHAR *w = UserAlloc(((SIZE_T)n + 1) * sizeof(WCHAR));
                if (w) {
                    MultiByteToWideChar(cp, 0, a, chars, w, n);
                    UINT to = format == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
                    int m = WideCharToMultiByte(to, 0, w, n, NULL, 0, NULL, NULL);
                    out = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)m + 1);
                    char *b = out ? GlobalLock(out) : NULL;
                    if (b) {
                        WideCharToMultiByte(to, 0, w, n, b, m, NULL, NULL);
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
    return NULL;
}

WINUSERAPI HANDLE WINAPI GetClipboardData(UINT format)
{
    MIME_CLIPBOARD_DATA d = {NULL, 0, 0, FALSE};
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
    return NULL;
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
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, name, max - 1, NULL, NULL);
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
