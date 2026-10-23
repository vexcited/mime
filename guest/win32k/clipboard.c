/*
 * the session clipboard. memory formats are stored as copies of their bytes so any process can read them, gdi
 * formats keep their handle.
 *
 * the contents are shared with the host clipboard, see MIME_CLIP_* in abi.h. closing the clipboard after a change
 * offers the host what it can take, and the host asks for the data only when a host program pastes. a host copy
 * replaces the contents with ownerless formats that are read from the host on demand.
 */
#include "win32k.h"

typedef struct CLIP_FORMAT {
    UINT Format;
    BOOLEAN Gdi;
    BOOLEAN Delayed;  /* the owner renders it on WM_RENDERFORMAT. */
    BOOLEAN Synthesized;
    ULONG HostKind;   /* MIME_CLIP_* kind fetched from the host on demand, 0 for the session's own. */
    HANDLE Handle;    /* gdi formats. */
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
static BOOLEAN g_changed;    /* the clipboard changed while open. */
static BOOLEAN g_rendering;  /* the owner is answering WM_RENDERFORMAT. */
static BOOLEAN g_host_pending; /* the host clipboard changed while the clipboard was open. */
static ULONG g_host_mask;      /* the kinds the host clipboard holds. */
static ULONG g_host_generation;
static BOOLEAN g_host_fetch_failed; /* a fetch got unsupported: the host only shares text. */
static BOOLEAN g_offer_unsupported; /* the host takes text only, in one piece. */
static ULONG g_offer_sequence;      /* the sequence number the host was last offered. */
static ATOM g_format_png, g_format_html, g_format_url, g_format_effect;

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
 * formats the system derives from the ones a program set, in windows' order: the locale and the other text
 * formats, then the other image formats. user32 converts the data when asked.
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
                *(DWORD *)f->Data = 0x0409; /* en-US, the session locale. */
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

#define HOST_CHUNK 65536

static PUCHAR g_host_page; /* hypercall bounce buffer, used under the user lock. */

static BOOLEAN host_page(void)
{
    if (!g_host_page)
        g_host_page = MmAllocateContiguousPages(HOST_CHUNK);
    return g_host_page != nullptr;
}

static void render_format(CLIP_FORMAT *f);

static void register_formats(void)
{
    if (!g_format_png) {
        g_format_png = AtomAdd(L"PNG");
        g_format_html = AtomAdd(L"HTML Format");
        g_format_url = AtomAdd(L"UniformResourceLocatorW");
        g_format_effect = AtomAdd(L"Preferred DropEffect");
    }
}

/* maps a host kind to its clipboard format and back. needs the user lock. */
UINT ClipboardFormatOfKind(ULONG kind)
{
    register_formats();
    switch (kind) {
    case MIME_CLIP_TEXT:
        return CF_UNICODETEXT;
    case MIME_CLIP_DIB:
        return CF_DIB;
    case MIME_CLIP_DIBV5:
        return CF_DIBV5;
    case MIME_CLIP_PNG:
        return g_format_png;
    case MIME_CLIP_HTML:
        return g_format_html;
    case MIME_CLIP_HDROP:
        return CF_HDROP;
    case MIME_CLIP_URL:
        return g_format_url;
    case MIME_CLIP_DROPEFFECT:
        return g_format_effect;
    }
    return 0;
}

ULONG ClipboardKindOfFormat(UINT format)
{
    for (ULONG kind = 1; kind <= MIME_CLIP_DROPEFFECT; kind <<= 1)
        if (format && ClipboardFormatOfKind(kind) == format)
            return kind;
    return 0;
}

/* a format a program stored, not one the system derives or the host owns. */
static CLIP_FORMAT *own_format(UINT format)
{
    CLIP_FORMAT *f = format ? find_format(format) : nullptr;
    return f && !f->Synthesized && !f->HostKind ? f : nullptr;
}

static ULONG offerable_kinds(void)
{
    register_formats();
    ULONG mask = 0;
    if (own_format(CF_UNICODETEXT) || own_format(CF_TEXT) || own_format(CF_OEMTEXT))
        mask |= MIME_CLIP_TEXT;
    if (own_format(CF_DIB) || own_format(CF_BITMAP))
        mask |= MIME_CLIP_DIB;
    if (own_format(CF_DIBV5))
        mask |= MIME_CLIP_DIBV5;
    if (own_format(g_format_png))
        mask |= MIME_CLIP_PNG;
    if (own_format(g_format_html))
        mask |= MIME_CLIP_HTML;
    if (own_format(CF_HDROP))
        mask |= MIME_CLIP_HDROP;
    if (own_format(g_format_url))
        mask |= MIME_CLIP_URL;
    /* the drop effect says whether the files were cut, so it goes with them. */
    if ((mask & MIME_CLIP_HDROP) && own_format(g_format_effect))
        mask |= MIME_CLIP_DROPEFFECT;
    return mask;
}

/* 32 bit dib, bottom row first. */
static PUCHAR bitmap_to_dib(HANDLE bitmap, SIZE_T *size)
{
    SURFACE *s = SurfaceLookup((HBITMAP)bitmap);
    if (!s || s->Width <= 0 || s->Height <= 0 || s->Width > 32768 || s->Height > 32768)
        return nullptr;
    SIZE_T stride = (SIZE_T)s->Width * 4, total = sizeof(BITMAPINFOHEADER) + stride * (SIZE_T)s->Height;
    PUCHAR dib = W32Alloc(total);
    if (!dib)
        return nullptr;
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)dib;
    h->biSize = sizeof(*h);
    h->biWidth = s->Width;
    h->biHeight = s->Height;
    h->biPlanes = 1;
    h->biBitCount = 32;
    h->biCompression = BI_RGB;
    h->biSizeImage = (DWORD)(stride * (SIZE_T)s->Height);
    for (LONG row = 0; row < s->Height; row++) {
        PULONG out = (PULONG)(dib + sizeof(*h) + stride * (SIZE_T)row);
        for (LONG x = 0; x < s->Width; x++)
            out[x] = SurfaceGetPixel(s, x, s->Height - 1 - row) & 0x00FFFFFF;
    }
    *size = total;
    return dib;
}

/*
 * a delayed format is rendered first, which drops the user lock while the owner answers. text comes back as
 * utf-16 without its terminator, other kinds as the raw format bytes.
 */
static PUCHAR gather(ULONG kind, SIZE_T *size)
{
    UINT candidates[3] = {0, 0, 0};
    switch (kind) {
    case MIME_CLIP_TEXT:
        candidates[0] = CF_UNICODETEXT;
        candidates[1] = CF_TEXT;
        candidates[2] = CF_OEMTEXT;
        break;
    case MIME_CLIP_DIB:
        candidates[0] = CF_DIB;
        candidates[1] = CF_BITMAP;
        break;
    case MIME_CLIP_DIBV5:
        candidates[0] = CF_DIBV5;
        break;
    case MIME_CLIP_PNG:
        candidates[0] = g_format_png;
        break;
    case MIME_CLIP_HTML:
        candidates[0] = g_format_html;
        break;
    case MIME_CLIP_HDROP:
        candidates[0] = CF_HDROP;
        break;
    case MIME_CLIP_URL:
        candidates[0] = g_format_url;
        break;
    case MIME_CLIP_DROPEFFECT:
        candidates[0] = g_format_effect;
        break;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(candidates); i++) {
        UINT id = candidates[i];
        CLIP_FORMAT *f = own_format(id);
        if (!f)
            continue;
        if (f->Delayed) {
            render_format(f);
            f = own_format(id);
            if (!f || f->Delayed)
                continue;
        }
        if (f->Gdi) {
            PUCHAR dib = bitmap_to_dib(f->Handle, size);
            if (dib)
                return dib;
            continue;
        }
        if (!f->Data)
            continue;
        if (kind != MIME_CLIP_TEXT) {
            PUCHAR copy = W32Alloc(f->Size ? f->Size : 1);
            if (copy)
                memcpy(copy, f->Data, f->Size);
            *size = f->Size;
            return copy;
        }
        SIZE_T chars = 0;
        if (id == CF_UNICODETEXT) {
            const WCHAR *w = (const WCHAR *)f->Data;
            SIZE_T max = f->Size / sizeof(WCHAR);
            while (chars < max && w[chars])
                chars++;
            PUCHAR copy = W32Alloc(chars * sizeof(WCHAR) + 2);
            if (copy)
                memcpy(copy, w, chars * sizeof(WCHAR));
            *size = chars * sizeof(WCHAR);
            return copy;
        }
        /* ansi and oem text are widened as latin 1, which covers ascii. */
        while (chars < f->Size && f->Data[chars])
            chars++;
        PWCHAR wide = W32Alloc(chars * sizeof(WCHAR) + 2);
        if (!wide)
            return nullptr;
        for (SIZE_T k = 0; k < chars; k++)
            wide[k] = f->Data[k];
        *size = chars * sizeof(WCHAR);
        return (PUCHAR)wide;
    }
    return nullptr;
}

/* sends the answer in chunks, or an empty answer. runs without the user lock. */
static void provide(ULONG request, const UCHAR *data, SIZE_T size)
{
    if (!data || !size) {
        HalMimeHypercall(MIME_HC_CLIP_PROVIDE, request, 0, 0, 0, MIME_CLIP_NONE, 0, nullptr);
        return;
    }
    PUCHAR page = MmAllocateContiguousPages(HOST_CHUNK);
    if (!page) {
        HalMimeHypercall(MIME_HC_CLIP_PROVIDE, request, 0, 0, 0, MIME_CLIP_NONE, 0, nullptr);
        return;
    }
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(page).QuadPart;
    for (SIZE_T offset = 0; offset < size;) {
        SIZE_T n = size - offset < HOST_CHUNK ? size - offset : HOST_CHUNK;
        memcpy(page, data + offset, n);
        if (HalMimeHypercall(MIME_HC_CLIP_PROVIDE, request, pa, n, offset, size, 0, nullptr) != MIME_HC_STATUS_OK)
            break;
        offset += n;
    }
    MmFreeContiguousPages(page, HOST_CHUNK);
}

typedef struct CLIP_REQUEST {
    ULONG Kind;
    ULONG Id;
    ULONG Sequence;
} CLIP_REQUEST;

/* one thread per host paste, since the owner of a delayed format may be slow to answer. */
static void NTAPI request_thread(PVOID context)
{
    CLIP_REQUEST *r = context;
    PUCHAR data = nullptr;
    SIZE_T size = 0;
    UserEnter();
    if (r->Sequence == g_sequence) {
        data = gather(r->Kind, &size);
        /* something else may have taken the clipboard over while the owner rendered. */
        if (r->Sequence != g_sequence) {
            W32Free(data);
            data = nullptr;
        }
    }
    UserLeave();
    provide(r->Id, data, size);
    W32Free(data);
    W32Free(r);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

void ClipboardHostRequest(ULONG kind, ULONG id)
{
    CLIP_REQUEST *r = W32Alloc(sizeof(*r));
    HANDLE thread;
    if (r) {
        *r = (CLIP_REQUEST){kind, id, g_offer_sequence};
        if (NT_SUCCESS(PsCreateSystemThread(&thread, 0, nullptr, nullptr, nullptr, request_thread, r))) {
            ZwClose(thread);
            return;
        }
        W32Free(r);
    }
    HalMimeHypercall(MIME_HC_CLIP_PROVIDE, id, 0, 0, 0, MIME_CLIP_NONE, 0, nullptr);
}

/* for hosts that only take text. sends an empty text when there is none. */
static void push_text_to_host(void)
{
    if (!host_page())
        return;
    SIZE_T size = 0;
    PUCHAR text = gather(MIME_CLIP_TEXT, &size);
    ULONG64 total = size;
    ULONG64 offset = 0;
    do {
        ULONG64 n = total - offset < HOST_CHUNK ? total - offset : HOST_CHUNK;
        if (n)
            memcpy(g_host_page, text + offset, (SIZE_T)n);
        if (HalMimeHypercall(MIME_HC_CLIP_SET, (ULONG64)MmGetPhysicalAddress(g_host_page).QuadPart, n, offset, total, 0,
                             0, nullptr) != MIME_HC_STATUS_OK)
            break;
        offset += n;
    } while (offset < total);
    W32Free(text);
}

/* the host only asks for the data of a kind when it is pasted. */
static void offer_to_host(void)
{
    if (g_offer_unsupported) {
        push_text_to_host();
        return;
    }
    /* unchanged contents that came from the host still belong to it. */
    ULONG mask = offerable_kinds();
    g_offer_sequence = g_sequence;
    if (HalMimeHypercall(MIME_HC_CLIP_OFFER, mask, 0, 0, 0, 0, 0, nullptr) == MIME_HC_STATUS_UNSUPPORTED) {
        g_offer_unsupported = TRUE;
        push_text_to_host();
    }
}

/* leaves the format empty when the host has no data. */
static void fetch_host(CLIP_FORMAT *f)
{
    f->Delayed = FALSE;
    if (!host_page())
        return;
    ULONG64 pa = (ULONG64)MmGetPhysicalAddress(g_host_page).QuadPart;
    ULONG64 total = 0, status;
    if (g_host_fetch_failed) {
        status = f->HostKind == MIME_CLIP_TEXT ? HalMimeHypercall(MIME_HC_CLIP_GET, pa, 0, 0, 0, 0, 0, &total)
                                               : MIME_HC_STATUS_UNSUPPORTED;
    } else {
        status = HalMimeHypercall(MIME_HC_CLIP_FETCH, f->HostKind, pa, 0, 0, g_host_generation, 0, &total);
        /* a text-only host answers unsupported: its text comes through the older call. */
        if (status == MIME_HC_STATUS_UNSUPPORTED) {
            g_host_fetch_failed = TRUE;
            status = f->HostKind == MIME_CLIP_TEXT ? HalMimeHypercall(MIME_HC_CLIP_GET, pa, 0, 0, 0, 0, 0, &total)
                                                   : MIME_HC_STATUS_UNSUPPORTED;
        }
    }
    if (status != MIME_HC_STATUS_OK || !total || total > (256u << 20))
        return;
    PUCHAR data = W32Alloc((SIZE_T)total + sizeof(WCHAR));
    if (!data)
        return;
    ULONG64 offset = 0;
    while (offset < total) {
        ULONG64 n = total - offset < HOST_CHUNK ? total - offset : HOST_CHUNK;
        ULONG64 now = 0;
        if (g_host_fetch_failed)
            status = HalMimeHypercall(MIME_HC_CLIP_GET, pa, n, offset, 0, 0, 0, &now);
        else
            status = HalMimeHypercall(MIME_HC_CLIP_FETCH, f->HostKind, pa, n, offset, g_host_generation, 0, &now);
        if (status != MIME_HC_STATUS_OK || now != total) {
            /* the host contents changed meanwhile: a new notification follows. */
            W32Free(data);
            return;
        }
        memcpy(data + offset, g_host_page, (SIZE_T)n);
        offset += n;
    }
    f->Data = data;
    /* text carries its terminator, which the host leaves out. */
    f->Size = (SIZE_T)total + (f->HostKind == MIME_CLIP_TEXT ? sizeof(WCHAR) : 0);
}

static void add_host_format(UINT format, ULONG kind)
{
    if (!format || g_format_count >= MAX_CLIP_FORMATS)
        return;
    CLIP_FORMAT *f = &g_formats[g_format_count++];
    memset(f, 0, sizeof(*f));
    f->Format = format;
    f->Delayed = TRUE;
    f->HostKind = kind;
}

/* a host application copied: take over its contents, read lazily. */
static void pull_from_host(void)
{
    WND *old = g_owner ? WndLookup(g_owner) : nullptr;
    if (old)
        QueuePost(old->Thread, g_owner, WM_DESTROYCLIPBOARD, 0, 0);
    empty_formats();
    g_owner = nullptr;
    g_owner_thread = nullptr;
    register_formats();
    if (g_host_mask & MIME_CLIP_TEXT)
        add_host_format(CF_UNICODETEXT, MIME_CLIP_TEXT);
    if (g_host_mask & MIME_CLIP_PNG)
        add_host_format(g_format_png, MIME_CLIP_PNG);
    if (g_host_mask & MIME_CLIP_DIBV5)
        add_host_format(CF_DIBV5, MIME_CLIP_DIBV5);
    if (g_host_mask & MIME_CLIP_DIB)
        add_host_format(CF_DIB, MIME_CLIP_DIB);
    if (g_host_mask & MIME_CLIP_HTML)
        add_host_format(g_format_html, MIME_CLIP_HTML);
    if (g_host_mask & MIME_CLIP_HDROP)
        add_host_format(CF_HDROP, MIME_CLIP_HDROP);
    if (g_host_mask & MIME_CLIP_URL)
        add_host_format(g_format_url, MIME_CLIP_URL);
    if (g_host_mask & MIME_CLIP_DROPEFFECT)
        add_host_format(g_format_effect, MIME_CLIP_DROPEFFECT);
    synthesize_formats();
    g_sequence++;
    /* the host no longer holds what the guest offered. */
    g_offer_sequence = g_sequence;
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

void ClipboardHostChanged(ULONG mask, ULONG generation)
{
    g_host_mask = mask;
    g_host_generation = generation;
    if (g_open_thread)
        g_host_pending = TRUE;
    else
        pull_from_host();
}

/* ------------------------------------------------------------------ cleanup */

/*
 * the owner gets WM_RENDERALLFORMATS before WM_DESTROY. whatever it leaves unrendered is dropped, which counts as
 * a change.
 */
void ClipboardDisown(WND *w)
{
    HWND h = WndHandle(w);
    if (g_owner != h)
        return;
    BOOLEAN delayed = FALSE;
    for (ULONG i = 0; i < g_format_count; i++)
        if (g_formats[i].Delayed && !g_formats[i].HostKind)
            delayed = TRUE;
    if (!delayed)
        return;
    g_rendering = TRUE;
    MsgSendKernel(w, WM_RENDERALLFORMATS, 0, 0, MIME_LPARAM_VALUE, 0);
    g_rendering = FALSE;
    ULONG kept = 0;
    for (ULONG i = 0; i < g_format_count; i++) {
        if (g_formats[i].Delayed && !g_formats[i].HostKind)
            continue;
        g_formats[kept++] = g_formats[i];
    }
    if (kept != g_format_count) {
        g_format_count = kept;
        g_sequence++;
        synthesize_formats();
        offer_to_host();
        notify_listeners();
    }
}

void ClipboardWindowDestroyed(WND *w)
{
    HWND h = WndHandle(w);
    if (g_open_window == h)
        g_open_window = nullptr;
    if (g_owner == h) {
        ULONG kept = 0;
        for (ULONG i = 0; i < g_format_count; i++) {
            if (g_formats[i].Delayed)
                continue;
            g_formats[kept++] = g_formats[i];
        }
        BOOLEAN dropped = kept != g_format_count;
        g_format_count = kept;
        g_owner = nullptr;
        g_owner_thread = nullptr;
        if (dropped) {
            g_sequence++;
            offer_to_host();
        }
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
            offer_to_host();
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
    if (W32UiLimited(W32_UILIMIT_WRITECLIPBOARD)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = caller_has_open(t);
    if (ok) {
        /* like windows, the owner gets WM_DESTROYCLIPBOARD even when it is the one emptying the clipboard. */
        WND *old = g_owner ? WndLookup(g_owner) : nullptr;
        if (old)
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
    if (W32UiLimited(W32_UILIMIT_WRITECLIPBOARD)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
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
            f->HostKind = 0;
            f->Gdi = is_gdi_format(format) && !d.Data;
            f->Delayed = !d.Data && !handle;
            f->Handle = f->Gdi ? handle : nullptr;
            f->Data = copy;
            f->Size = d.Data ? d.Size : 0;
            copy = nullptr;
            if (f->Gdi && handle) {
                /* the clipboard now owns the gdi object, beyond the life of the process. */
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

/* returns with the lock held. */
static void render_format(CLIP_FORMAT *f)
{
    if (f->HostKind) {
        fetch_host(f);
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
    if (W32UiLimited(W32_UILIMIT_READCLIPBOARD)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
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
            /* missing. user32 converts synthesized formats from a stored text format. */
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
    if (W32UiLimited(W32_UILIMIT_READCLIPBOARD)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    UserEnter();
    BOOL r = find_format(format) != nullptr;
    UserLeave();
    return r;
}

/* includes synthesized formats, and returns the full count even when there is room for fewer. */
BOOL NTAPI NtUserGetUpdatedClipboardFormats(UINT *user_formats, UINT count, UINT *user_out)
{
    if (!user_out) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT formats[MAX_CLIP_FORMATS], n;
    UserEnter();
    n = g_format_count;
    for (UINT i = 0; i < n; i++)
        formats[i] = g_formats[i].Format;
    UserLeave();
    BOOL ok = n <= count && (!n || user_formats);
    if (ok && n && !NT_SUCCESS(MimeCopyToUser(user_formats, formats, n * sizeof(UINT)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_out, &n, sizeof(n)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if (!ok)
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return ok;
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
    if (W32UiLimited(W32_UILIMIT_READCLIPBOARD)) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
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
