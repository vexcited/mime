/*
 * the clipboard of the session: formats with their data, the owner, the
 * viewer chain and format listeners. memory formats are stored as copies
 * of their bytes so any process can read them; gdi formats keep their
 * handle.
 *
 * the text is shared with the host clipboard: closing the clipboard after a
 * change sends its text to the host, and text another host application
 * copies replaces the contents with an ownerless CF_UNICODETEXT.
 */
#include "win32k.h"

typedef struct CLIP_FORMAT {
    UINT Format;
    BOOLEAN Gdi;
    BOOLEAN Delayed;  /* rendered on demand by the owner with WM_RENDERFORMAT */
    BOOLEAN Synthesized;
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
    return NULL;
}

static void free_format(CLIP_FORMAT *f)
{
    W32Free(f->Data);
    f->Data = NULL;
    f->Size = 0;
    if (f->Gdi && f->Handle) {
        GDI_OBJECT *o = GdiLookup(f->Handle, 0);
        if (o && !(o->Flags & (GDI_FLAG_STOCK | GDI_FLAG_SYSTEM)))
            GdiDeleteObject(o);
    }
    f->Handle = NULL;
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
        WND *w = g_listeners[i] ? WndLookup(g_listeners[i]) : NULL;
        if (!w) {
            g_listeners[i] = NULL;
            continue;
        }
        QueuePost(w->Thread, g_listeners[i], WM_CLIPBOARDUPDATE, 0, 0);
    }
    WND *v = g_viewer ? WndLookup(g_viewer) : NULL;
    if (v)
        MsgSendKernel(v, WM_DRAWCLIPBOARD, (WPARAM)g_owner, 0, MIME_LPARAM_VALUE, 0);
    else
        g_viewer = NULL;
}

/* text formats the system provides from another text format, like windows. */
static void synthesize_text(void)
{
    static const UINT text[] = {CF_UNICODETEXT, CF_TEXT, CF_OEMTEXT};
    BOOLEAN have = FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(text); i++)
        if (find_format(text[i]))
            have = TRUE;
    if (!have)
        return;
    for (ULONG i = 0; i < RTL_NUMBER_OF(text); i++) {
        if (find_format(text[i]) || g_format_count >= MAX_CLIP_FORMATS)
            continue;
        CLIP_FORMAT *f = &g_formats[g_format_count++];
        memset(f, 0, sizeof(*f));
        f->Format = text[i];
        f->Synthesized = TRUE;
    }
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
}

/* ------------------------------------------------------------------ host clipboard */

#define HOST_CHUNK 4096

static PUCHAR g_host_page; /* hypercall bounce page, used under the user lock */

static BOOLEAN host_page(void)
{
    if (!g_host_page)
        g_host_page = MmAllocateContiguousPages(HOST_CHUNK);
    return g_host_page != NULL;
}

static void render_format(CLIP_FORMAT *f);

/* sends the text of the clipboard to the host, an empty text when it has none. */
static void push_to_host(void)
{
    if (!host_page())
        return;
    CLIP_FORMAT *f = find_format(CF_UNICODETEXT);
    if (f && f->Delayed) {
        render_format(f);
        f = find_format(CF_UNICODETEXT);
    }
    WCHAR *text = NULL;
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
                             0, NULL) != MIME_HC_STATUS_OK)
            break;
        offset += n;
    } while (offset < total);
    if (widened)
        W32Free(text);
}

/* replaces the contents with the text of the host clipboard. */
static void pull_from_host(void)
{
    if (!host_page())
        return;
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(g_host_page).QuadPart;
    ULONG64 total = 0;
    if (HalMimeHypercall(MIME_HC_CLIP_GET, pa, 0, 0, 0, 0, 0, &total) != MIME_HC_STATUS_OK)
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
    WND *old = g_owner ? WndLookup(g_owner) : NULL;
    if (old)
        QueuePost(old->Thread, g_owner, WM_DESTROYCLIPBOARD, 0, 0);
    empty_formats();
    g_owner = NULL;
    g_owner_thread = NULL;
    if (total) {
        CLIP_FORMAT *f = &g_formats[g_format_count++];
        memset(f, 0, sizeof(*f));
        f->Format = CF_UNICODETEXT;
        f->Data = text;
        f->Size = (SIZE_T)total + sizeof(WCHAR);
        text = NULL;
        synthesize_text();
    }
    W32Free(text);
    g_sequence++;
    /* posted rather than sent: this runs on the input thread. */
    for (ULONG i = 0; i < MAX_LISTENERS; i++) {
        WND *w = g_listeners[i] ? WndLookup(g_listeners[i]) : NULL;
        if (w)
            QueuePost(w->Thread, g_listeners[i], WM_CLIPBOARDUPDATE, 0, 0);
        else
            g_listeners[i] = NULL;
    }
    WND *v = g_viewer ? WndLookup(g_viewer) : NULL;
    if (v)
        QueuePost(v->Thread, g_viewer, WM_DRAWCLIPBOARD, 0, 0);
}

void ClipboardHostChanged(void)
{
    if (g_open_thread)
        g_host_pending = TRUE;
    else
        pull_from_host();
}

/* ------------------------------------------------------------------ cleanup */

void ClipboardWindowDestroyed(WND *w)
{
    HWND h = WndHandle(w);
    if (g_open_window == h)
        g_open_window = NULL;
    if (g_owner == h) {
        /* windows sends WM_RENDERALLFORMATS first; formats that were never rendered are dropped. */
        ULONG kept = 0;
        for (ULONG i = 0; i < g_format_count; i++) {
            if (g_formats[i].Delayed)
                continue;
            g_formats[kept++] = g_formats[i];
        }
        g_format_count = kept;
        g_owner = NULL;
        g_owner_thread = NULL;
    }
    if (g_viewer == h)
        g_viewer = NULL;
    for (ULONG i = 0; i < MAX_LISTENERS; i++)
        if (g_listeners[i] == h)
            g_listeners[i] = NULL;
}

void ClipboardThreadDestroyed(W32THREAD *t)
{
    if (g_open_thread == t) {
        g_open_thread = NULL;
        g_open_window = NULL;
        if (g_host_pending) {
            g_host_pending = FALSE;
            pull_from_host();
        }
    }
    if (g_owner_thread == t)
        g_owner_thread = NULL;
}

/* ------------------------------------------------------------------ services */

BOOL NTAPI NtUserOpenClipboard(HWND owner, ULONG reserved)
{
    UserEnter();
    W32THREAD *t = W32GetThread();
    WND *w = owner ? WndLookup(owner) : NULL;
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
        g_open_thread = NULL;
        g_open_window = NULL;
        changed = g_changed;
        g_changed = FALSE;
        if (changed) {
            /* contents written after the host change are newer than it. */
            g_host_pending = FALSE;
            synthesize_text();
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
        WND *old = g_owner ? WndLookup(g_owner) : NULL;
        if (old && g_owner != g_open_window)
            MsgSendKernel(old, WM_DESTROYCLIPBOARD, 0, 0, MIME_LPARAM_VALUE, 0);
        empty_formats();
        g_owner = g_open_window;
        g_owner_thread = g_open_window ? t : NULL;
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
    MIME_CLIPBOARD_DATA d = {NULL, 0, 0, FALSE};
    if (user_data && !NT_SUCCESS(MimeCopyFromUser(&d, user_data, sizeof(d)))) {
        W32SetLastError(ERROR_NOACCESS);
        return NULL;
    }
    if (!format || (d.Data && d.Size > 256 * 1024 * 1024)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    PUCHAR copy = NULL;
    if (d.Data) {
        copy = W32Alloc(d.Size);
        if (!copy) {
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        if (d.Size && !NT_SUCCESS(MimeCopyFromUser(copy, d.Data, d.Size))) {
            W32Free(copy);
            W32SetLastError(ERROR_NOACCESS);
            return NULL;
        }
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    HANDLE result = NULL;
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
            f->Gdi = is_gdi_format(format) && !d.Data;
            f->Delayed = !d.Data && !handle;
            f->Handle = f->Gdi ? handle : NULL;
            f->Data = copy;
            f->Size = d.Data ? d.Size : 0;
            copy = NULL;
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
    WND *owner = g_owner ? WndLookup(g_owner) : NULL;
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
        return NULL;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    HANDLE result = NULL;
    PUCHAR copy = NULL;
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
                result = NULL;
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        d.Sequence = g_sequence;
    }
    UserLeave();
    if (result && copy && !NT_SUCCESS(MimeCopyToUser(d.Data, copy, d.Size < size ? d.Size : size))) {
        result = NULL;
        error = ERROR_NOACCESS;
    }
    W32Free(copy);
    if (result && !NT_SUCCESS(MimeCopyToUser(user_data, &d, sizeof(d)))) {
        result = NULL;
        error = ERROR_NOACCESS;
    }
    if (error)
        W32SetLastError(error);
    return result;
}

BOOL NTAPI NtUserIsClipboardFormatAvailable(UINT format)
{
    UserEnter();
    BOOL r = find_format(format) != NULL;
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
    HWND h = g_owner && WndLookup(g_owner) ? g_owner : NULL;
    UserLeave();
    return h;
}

HWND NTAPI NtUserGetOpenClipboardWindow(void)
{
    UserEnter();
    HWND h = g_open_thread ? g_open_window : NULL;
    UserLeave();
    return h;
}

HWND NTAPI NtUserGetClipboardViewer(void)
{
    UserEnter();
    HWND h = g_viewer && WndLookup(g_viewer) ? g_viewer : NULL;
    UserLeave();
    return h;
}

HWND NTAPI NtUserSetClipboardViewer(HWND viewer)
{
    UserEnter();
    WND *w = WndLookup(viewer);
    HWND next = NULL;
    if (w) {
        next = g_viewer && WndLookup(g_viewer) ? g_viewer : NULL;
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
        WND *first = g_viewer ? WndLookup(g_viewer) : NULL;
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
            g_listeners[i] = NULL;
            ok = TRUE;
        }
    UserLeave();
    if (!ok)
        W32SetLastError(ERROR_INVALID_PARAMETER);
    return ok;
}
