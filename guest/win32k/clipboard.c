/*
 * the clipboard of the session: formats with their data, the owner, the
 * viewer chain and format listeners. memory formats are stored as copies
 * of their bytes so any process can read them; gdi formats keep their
 * handle.
 *
 * the text is shared with the host clipboard: closing the clipboard after a
 * change sends its text to the host, and text another host application
 * copies replaces the contents with an ownerless CF_UNICODETEXT, read from
 * the host only when a program asks for it.
 */
#include "win32k.h"

typedef struct CLIP_FORMAT {
    UINT Format;
    BOOLEAN Gdi;
    BOOLEAN Delayed;  /* rendered on demand by the owner with WM_RENDERFORMAT */
    BOOLEAN Synthesized;
    BOOLEAN Host;     /* the host's text, fetched on demand */
    HANDLE Handle;    /* gdi formats */
    PUCHAR Data;
    SIZE_T Size;
} CLIP_FORMAT;

#define MAX_CLIP_FORMATS 64
#define MAX_LISTENERS 32

static CLIP_FORMAT g_formats[MAX_CLIP_FORMATS];
static ULONG g_format_count;
static W32THREAD *g_open_thread;
static HWND g_open_window;
static HWND g_owner;
static W32THREAD *g_owner_thread;
static HWND g_viewer;
static HWND g_listeners[MAX_LISTENERS];
static DWORD g_sequence = 1;
static BOOLEAN g_changed;    /* the clipboard changed while open */
static BOOLEAN g_rendering;  /* the owner is answering WM_RENDERFORMAT */
static BOOLEAN g_host_pending; /* the host clipboard changed while the clipboard was open */
static BOOLEAN g_host_has_text;

static BOOLEAN is_gdi_format(UINT format)
{
    return format == CF_BITMAP || format == CF_PALETTE || format == CF_ENHMETAFILE || format == CF_DSPBITMAP ||
           format == CF_DSPENHMETAFILE || (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST);
}

static CLIP_FORMAT *find_format(UINT format)
{
    for (ULONG i = 0; i < g_format_count; i++)
        if (g_formats[i].Format == format)
            return &g_formats[i];
    return nullptr;
}

static void free_format(CLIP_FORMAT *f)
{
    W32Free(f->Data);
    f->Data = nullptr;
    f->Size = 0;
    if (f->Gdi && f->Handle) {
        GDI_OBJECT *o = GdiLookup(f->Handle, 0);
        if (o && !(o->Flags & (GDI_FLAG_STOCK | GDI_FLAG_SYSTEM)))
            GdiDeleteObject(o);
    }
    f->Handle = nullptr;
}

static void empty_formats(void)
{
    for (ULONG i = 0; i < g_format_count; i++)
        free_format(&g_formats[i]);
    g_format_count = 0;
}

static BOOLEAN caller_has_open(W32THREAD *t)
{
    return t && g_open_thread == t;
}

static void notify_listeners(void)
{
    for (ULONG i = 0; i < MAX_LISTENERS; i++) {
        WND *w = g_listeners[i] ? WndLookup(g_listeners[i]) : nullptr;
        if (!w) {
            g_listeners[i] = nullptr;
            continue;
        }
        QueuePost(w->Thread, g_listeners[i], WM_CLIPBOARDUPDATE, 0, 0);
    }
    WND *v = g_viewer ? WndLookup(g_viewer) : nullptr;
    if (v)
        MsgSendKernel(v, WM_DRAWCLIPBOARD, (WPARAM)g_owner, 0, MIME_LPARAM_VALUE, 0);
    else
        g_viewer = nullptr;
}

static void add_synthesized(UINT format)
{
    if (find_format(format) || g_format_count >= MAX_CLIP_FORMATS)
        return;
    CLIP_FORMAT *f = &g_formats[g_format_count++];
    memset(f, 0, sizeof(*f));
    f->Format = format;
    f->Synthesized = TRUE;
}

/*
 * the formats the system provides from the ones a program set, in the order windows lists them: the locale
 * and the other text formats, then the other image formats. user32 converts their data when asked.
 */
static void synthesize_formats(void)
{
    if (find_format(CF_TEXT) || find_format(CF_OEMTEXT) || find_format(CF_UNICODETEXT)) {
        if (!find_format(CF_LOCALE) && g_format_count < MAX_CLIP_FORMATS) {
            CLIP_FORMAT *f = &g_formats[g_format_count++];
            memset(f, 0, sizeof(*f));
            f->Format = CF_LOCALE;
            f->Data = W32Alloc(sizeof(DWORD));
            if (f->Data) {
                *(DWORD *)f->Data = 0x0409; /* en-US, the locale of the session */
                f->Size = sizeof(DWORD);
            }
        }
        add_synthesized(CF_TEXT);
        add_synthesized(CF_OEMTEXT);
        add_synthesized(CF_UNICODETEXT);
    }
    if (find_format(CF_BITMAP) || find_format(CF_DIB) || find_format(CF_DIBV5)) {
        add_synthesized(CF_BITMAP);
        add_synthesized(CF_DIB);
        add_synthesized(CF_DIBV5);
    }
}

/* ------------------------------------------------------------------ host clipboard */

#define HOST_CHUNK 4096

static PUCHAR g_host_page; /* hypercall bounce page, used under the user lock */

static BOOLEAN host_page(void)
{
    if (!g_host_page)
        g_host_page = MmAllocateContiguousPages(HOST_CHUNK);
    return g_host_page != nullptr;
}

static void render_format(CLIP_FORMAT *f);

/* sends the text of the clipboard to the host, an empty text when it has none. */
static void push_to_host(void)
{
    if (!host_page())
        return;
    CLIP_FORMAT *f = find_format(CF_UNICODETEXT);
    if (f && f->Host)
        return; /* the host's own text, unchanged */
    if (f && f->Delayed) {
        render_format(f);
        f = find_format(CF_UNICODETEXT);
    }
    WCHAR *text = nullptr;
    SIZE_T chars = 0;
    BOOLEAN widened = FALSE;
    if (f && f->Data && !f->Delayed) {
        const WCHAR *src = (const WCHAR *)f->Data;
        SIZE_T max = f->Size / sizeof(WCHAR);
        while (chars < max && src[chars])
            chars++;
        text = (WCHAR *)src;
    } else if ((f = find_format(CF_TEXT)) && f->Data && !f->Delayed) {
        /* ansi text only: widen it as latin 1, which covers ascii. */
        SIZE_T n = 0;
        while (n < f->Size && f->Data[n])
            n++;
        text = W32Alloc((n ? n : 1) * sizeof(WCHAR));
        if (!text)
            return;
        for (SIZE_T i = 0; i < n; i++)
            text[i] = f->Data[i];
        chars = n;
        widened = TRUE;
    }
    ULONG64 total = (ULONG64)chars * sizeof(WCHAR);
    ULONG64 offset = 0;
    do {
        ULONG64 n = total - offset < HOST_CHUNK ? total - offset : HOST_CHUNK;
        if (n)
            memcpy(g_host_page, (PUCHAR)text + offset, (SIZE_T)n);
        if (HalMimeHypercall(MIME_HC_CLIP_SET, (ULONG64)MmGetPhysicalAddress(g_host_page).QuadPart, n, offset, total, 0,
                             0, nullptr) != MIME_HC_STATUS_OK)
            break;
        offset += n;
    } while (offset < total);
    if (widened)
        W32Free(text);
}

/* reads the host text into a host format; leaves it without data when the host has none. */
static void fetch_host_text(CLIP_FORMAT *f)
{
    f->Delayed = FALSE;
    if (!host_page())
        return;
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(g_host_page).QuadPart;
    ULONG64 total = 0;
    if (HalMimeHypercall(MIME_HC_CLIP_GET, pa, 0, 0, 0, 0, 0, &total) != MIME_HC_STATUS_OK || !total)
        return;
    PUCHAR text = W32Alloc((SIZE_T)total + sizeof(WCHAR));
    if (!text)
        return;
    ULONG64 offset = 0;
    while (offset < total) {
        ULONG64 n = total - offset < HOST_CHUNK ? total - offset : HOST_CHUNK;
        ULONG64 now = 0;
        if (HalMimeHypercall(MIME_HC_CLIP_GET, pa, n, offset, 0, 0, 0, &now) != MIME_HC_STATUS_OK || now != total) {
            /* the host text changed meanwhile: a new notification follows. */
            W32Free(text);
            return;
        }
        memcpy(text + offset, g_host_page, (SIZE_T)n);
        offset += n;
    }
    f->Data = text;
    f->Size = (SIZE_T)total + sizeof(WCHAR);
}

/* another host application copied text: the contents become that text, not read yet. */
static void pull_from_host(void)
{
    WND *old = g_owner ? WndLookup(g_owner) : nullptr;
    if (old)
        QueuePost(old->Thread, g_owner, WM_DESTROYCLIPBOARD, 0, 0);
    empty_formats();
    g_owner = nullptr;
    g_owner_thread = nullptr;
    if (g_host_has_text) {
        CLIP_FORMAT *f = &g_formats[g_format_count++];
        memset(f, 0, sizeof(*f));
        f->Format = CF_UNICODETEXT;
        f->Delayed = TRUE;
        f->Host = TRUE;
        synthesize_formats();
    }
    g_sequence++;
    /* posted rather than sent: this runs on the input thread. */
    for (ULONG i = 0; i < MAX_LISTENERS; i++) {
        WND *w = g_listeners[i] ? WndLookup(g_listeners[i]) : nullptr;
        if (w)
            QueuePost(w->Thread, g_listeners[i], WM_CLIPBOARDUPDATE, 0, 0);
        else
            g_listeners[i] = nullptr;
    }
    WND *v = g_viewer ? WndLookup(g_viewer) : nullptr;
    if (v)
        QueuePost(v->Thread, g_viewer, WM_DRAWCLIPBOARD, 0, 0);
}

void ClipboardHostChanged(BOOLEAN has_text)
{
    g_host_has_text = has_text;
    if (g_open_thread)
        g_host_pending = TRUE;
    else
        pull_from_host();
}

/* ------------------------------------------------------------------ cleanup */

/*
 * the owner is destroyed: it renders every delayed format first, from WM_RENDERALLFORMATS, before it gets
 * WM_DESTROY. what it leaves unrendered is dropped, which counts as a change.
 */
void ClipboardDisown(WND *w)
{
    HWND h = WndHandle(w);
    if (g_owner != h)
        return;
    BOOLEAN delayed = FALSE;
    for (ULONG i = 0; i < g_format_count; i++)
        if (g_formats[i].Delayed && !g_formats[i].Host)
            delayed = TRUE;
    if (!delayed)
        return;
    g_rendering = TRUE;
    MsgSendKernel(w, WM_RENDERALLFORMATS, 0, 0, MIME_LPARAM_VALUE, 0);
    g_rendering = FALSE;
    ULONG kept = 0;
    for (ULONG i = 0; i < g_format_count; i++) {
        if (g_formats[i].Delayed && !g_formats[i].Host)
            continue;
        g_formats[kept++] = g_formats[i];
    }
    if (kept != g_format_count) {
        g_format_count = kept;
        g_sequence++;
        synthesize_formats();
        notify_listeners();
    }
}

void ClipboardWindowDestroyed(WND *w)
{
    HWND h = WndHandle(w);
    if (g_open_window == h)
        g_open_window = nullptr;
    if (g_owner == h) {
        /* windows sends WM_RENDERALLFORMATS first; formats that were never rendered are dropped. */
        ULONG kept = 0;
        for (ULONG i = 0; i < g_format_count; i++) {
            if (g_formats[i].Delayed)
                continue;
            g_formats[kept++] = g_formats[i];
        }
        g_format_count = kept;
        g_owner = nullptr;
        g_owner_thread = nullptr;
    }
    if (g_viewer == h)
        g_viewer = nullptr;
    for (ULONG i = 0; i < MAX_LISTENERS; i++)
        if (g_listeners[i] == h)
            g_listeners[i] = nullptr;
}

void ClipboardThreadDestroyed(W32THREAD *t)
{
    if (g_open_thread == t) {
        g_open_thread = nullptr;
        g_open_window = nullptr;
        if (g_host_pending) {
            g_host_pending = FALSE;
            pull_from_host();
        }
    }
    if (g_owner_thread == t)
        g_owner_thread = nullptr;
}

/* ------------------------------------------------------------------ services */

BOOL NTAPI NtUserOpenClipboard(HWND owner, ULONG reserved)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *w = owner ? WndLookup(owner) : nullptr;
    BOOL ok = FALSE;
    ULONG error = 0;
    if (owner && !w)
        error = ERROR_INVALID_WINDOW_HANDLE;
    else if (g_open_thread && g_open_thread != t)
        error = ERROR_ACCESS_DENIED;
    else if (t) {
        g_open_thread = t;
        g_open_window = owner;
        g_changed = FALSE;
        ok = TRUE;
    }
    UserLeave();
    if (error)
        W32SetLastError(error);
    (void)reserved;
    return ok;
}

BOOL NTAPI NtUserCloseClipboard(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = caller_has_open(t);
    BOOLEAN changed = FALSE;
    if (ok) {
        g_open_thread = nullptr;
        g_open_window = nullptr;
        changed = g_changed;
        g_changed = FALSE;
        if (changed) {
            /* contents written after the host change are newer than it. */
            g_host_pending = FALSE;
            synthesize_formats();
            g_sequence++;
            push_to_host();
            notify_listeners();
        } else if (g_host_pending) {
            g_host_pending = FALSE;
            pull_from_host();
        }
    }
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    return ok;
}

BOOL NTAPI NtUserEmptyClipboard(void)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = caller_has_open(t);
    if (ok) {
        WND *old = g_owner ? WndLookup(g_owner) : nullptr;
        if (old && g_owner != g_open_window)
            MsgSendKernel(old, WM_DESTROYCLIPBOARD, 0, 0, MIME_LPARAM_VALUE, 0);
        empty_formats();
        g_owner = g_open_window;
        g_owner_thread = g_open_window ? t : nullptr;
        g_changed = TRUE;
        g_sequence++;
    }
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    return ok;
}

HANDLE NTAPI NtUserSetClipboardData(UINT format, HANDLE handle, MIME_CLIPBOARD_DATA *user_data)
{
    MIME_CLIPBOARD_DATA d = {nullptr, 0, 0, FALSE};
    if (user_data && !NT_SUCCESS(MimeCopyFromUser(&d, user_data, sizeof(d)))) {
        W32SetLastError(ERROR_NOACCESS);
        return nullptr;
    }
    if (!format || (d.Data && d.Size > 256 * 1024 * 1024)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PUCHAR copy = nullptr;
    if (d.Data) {
        copy = W32Alloc(d.Size);
        if (!copy) {
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        if (d.Size && !NT_SUCCESS(MimeCopyFromUser(copy, d.Data, d.Size))) {
            W32Free(copy);
            W32SetLastError(ERROR_NOACCESS);
            return nullptr;
        }
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    HANDLE result = nullptr;
    /* the owner may render a delayed format without opening the clipboard. */
    BOOLEAN allowed = caller_has_open(t) || (g_rendering && g_owner_thread == t);
    if (allowed) {
        CLIP_FORMAT *f = find_format(format);
        if (!f && g_format_count < MAX_CLIP_FORMATS) {
            f = &g_formats[g_format_count++];
            memset(f, 0, sizeof(*f));
            f->Format = format;
        }
        if (f) {
            free_format(f);
            f->Synthesized = FALSE;
            f->Host = FALSE;
            f->Gdi = is_gdi_format(format) && !d.Data;
            f->Delayed = !d.Data && !handle;
            f->Handle = f->Gdi ? handle : nullptr;
            f->Data = copy;
            f->Size = d.Data ? d.Size : 0;
            copy = nullptr;
            if (f->Gdi && handle) {
                /* the clipboard owns gdi objects from now on, beyond the life of the process. */
                GDI_OBJECT *o = GdiLookup(handle, 0);
                if (o)
                    o->OwnerPid = 0;
            }
            g_changed = TRUE;
            if (!g_rendering)
                g_sequence++;
            result = handle ? handle : (HANDLE)(ULONG_PTR)(d.Data ? 1 : 0);
        }
    }
    UserLeave();
    W32Free(copy);
    if (!allowed)
        W32SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    else if (!result && (d.Data || handle))
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return result;
}

/* asks the owner to render a delayed format; returns with the lock held. */
static void render_format(CLIP_FORMAT *f)
{
    if (f->Host) {
        fetch_host_text(f);
        return;
    }
    WND *owner = g_owner ? WndLookup(g_owner) : nullptr;
    if (!owner)
        return;
    g_rendering = TRUE;
    MsgSendKernel(owner, WM_RENDERFORMAT, f->Format, 0, MIME_LPARAM_VALUE, 0);
    g_rendering = FALSE;
}

HANDLE NTAPI NtUserGetClipboardData(UINT format, MIME_CLIPBOARD_DATA *user_data)
{
    MIME_CLIPBOARD_DATA d;
    if (!user_data || !NT_SUCCESS(MimeCopyFromUser(&d, user_data, sizeof(d)))) {
        W32SetLastError(ERROR_NOACCESS);
        return nullptr;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    HANDLE result = nullptr;
    PUCHAR copy = nullptr;
    SIZE_T size = 0;
    ULONG error = 0;
    if (!caller_has_open(t)) {
        error = ERROR_CLIPBOARD_NOT_OPEN;
    } else {
        CLIP_FORMAT *f = find_format(format);
        if (f && f->Delayed) {
            render_format(f);
            f = find_format(format);
        }
        if (!f || f->Delayed || f->Synthesized) {
            /* missing format; synthesized ones are converted by user32 from a stored text format. */
        } else if (f->Gdi) {
            result = f->Handle;
            d.Gdi = TRUE;
            d.Size = 0;
        } else {
            size = f->Size;
            SIZE_T n = d.Data ? (d.Size < size ? d.Size : size) : 0;
            if (n) {
                copy = W32Alloc(n);
                if (copy)
                    memcpy(copy, f->Data, n);
            }
            d.Gdi = FALSE;
            d.Size = size;
            result = (HANDLE)1;
            if (n && !copy) {
                result = nullptr;
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        d.Sequence = g_sequence;
    }
    UserLeave();
    if (result && copy && !NT_SUCCESS(MimeCopyToUser(d.Data, copy, d.Size < size ? d.Size : size))) {
        result = nullptr;
        error = ERROR_NOACCESS;
    }
    W32Free(copy);
    if (result && !NT_SUCCESS(MimeCopyToUser(user_data, &d, sizeof(d)))) {
        result = nullptr;
        error = ERROR_NOACCESS;
    }
    if (error)
        W32SetLastError(error);
    return result;
}

BOOL NTAPI NtUserIsClipboardFormatAvailable(UINT format)
{
    UserEnter();
    BOOL r = find_format(format) != nullptr;
    UserLeave();
    return r;
}

int NTAPI NtUserCountClipboardFormats(void)
{
    UserEnter();
    int n = (int)g_format_count;
    UserLeave();
    return n;
}

UINT NTAPI NtUserEnumClipboardFormats(UINT format)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    UINT next = 0;
    BOOL open = caller_has_open(t);
    if (open) {
        if (!format) {
            next = g_format_count ? g_formats[0].Format : 0;
        } else {
            for (ULONG i = 0; i + 1 < g_format_count; i++)
                if (g_formats[i].Format == format) {
                    next = g_formats[i + 1].Format;
                    break;
                }
        }
    }
    UserLeave();
    W32SetLastError(open ? ERROR_SUCCESS : ERROR_CLIPBOARD_NOT_OPEN);
    return next;
}

int NTAPI NtUserGetClipboardFormatName(UINT format, LPWSTR user_name, int max)
{
    if (max <= 0 || !user_name) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR name[256];
    UserEnter();
    /* only registered formats have names; the predefined ones do not. */
    ULONG n = format >= 0xC000 && format <= 0xFFFF ? AtomGetName((ATOM)format, name, RTL_NUMBER_OF(name)) : 0;
    UserLeave();
    if (!n) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (n >= (ULONG)max)
        n = (ULONG)max - 1;
    name[n] = 0;
    if (!NT_SUCCESS(MimeCopyToUser(user_name, name, (n + 1) * sizeof(WCHAR)))) {
        W32SetLastError(ERROR_NOACCESS);
        return 0;
    }
    return (int)n;
}

HWND NTAPI NtUserGetClipboardOwner(void)
{
    UserEnter();
    HWND h = g_owner && WndLookup(g_owner) ? g_owner : nullptr;
    UserLeave();
    return h;
}

HWND NTAPI NtUserGetOpenClipboardWindow(void)
{
    UserEnter();
    HWND h = g_open_thread ? g_open_window : nullptr;
    UserLeave();
    return h;
}

HWND NTAPI NtUserGetClipboardViewer(void)
{
    UserEnter();
    HWND h = g_viewer && WndLookup(g_viewer) ? g_viewer : nullptr;
    UserLeave();
    return h;
}

HWND NTAPI NtUserSetClipboardViewer(HWND viewer)
{
    UserEnter();
    WND *w = WndLookup(viewer);
    HWND next = nullptr;
    if (w) {
        next = g_viewer && WndLookup(g_viewer) ? g_viewer : nullptr;
        g_viewer = viewer;
        /* the new viewer draws the current contents at once. */
        MsgSendKernel(w, WM_DRAWCLIPBOARD, (WPARAM)g_owner, 0, MIME_LPARAM_VALUE, 0);
    }
    UserLeave();
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return next;
}

BOOL NTAPI NtUserChangeClipboardChain(HWND remove, HWND next)
{
    UserEnter();
    WND *w = WndLookup(remove);
    BOOL r = FALSE;
    if (w) {
        WND *first = g_viewer ? WndLookup(g_viewer) : nullptr;
        if (g_viewer == remove) {
            /* the first viewer leaves: the next one takes its place. */
            g_viewer = next;
            r = TRUE;
        } else if (first) {
            r = (BOOL)MsgSendKernel(first, WM_CHANGECBCHAIN, (WPARAM)remove, (LPARAM)next, MIME_LPARAM_VALUE, 0);
        }
    }
    UserLeave();
    if (!w)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return r;
}

DWORD NTAPI NtUserGetClipboardSequenceNumber(void)
{
    UserEnter();
    DWORD n = g_sequence;
    UserLeave();
    return n;
}

BOOL NTAPI NtUserAddClipboardFormatListener(HWND hwnd)
{
    UserEnter();
    WND *w = WndLookup(hwnd);
    BOOL ok = FALSE;
    if (w) {
        for (ULONG i = 0; i < MAX_LISTENERS && !ok; i++)
            if (g_listeners[i] == hwnd)
                ok = TRUE;
        for (ULONG i = 0; i < MAX_LISTENERS && !ok; i++)
            if (!g_listeners[i] || !WndLookup(g_listeners[i])) {
                g_listeners[i] = hwnd;
                ok = TRUE;
            }
    }
    UserLeave();
    if (!ok)
        W32SetLastError(w ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_WINDOW_HANDLE);
    return ok;
}

BOOL NTAPI NtUserRemoveClipboardFormatListener(HWND hwnd)
{
    UserEnter();
    BOOL ok = FALSE;
    for (ULONG i = 0; i < MAX_LISTENERS; i++)
        if (g_listeners[i] == hwnd) {
            g_listeners[i] = nullptr;
            ok = TRUE;
        }
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    return ok;
}
