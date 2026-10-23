/*
 * the ole side of drag and drop with host programs. win32k posts WM_MIME_HOSTDND to the thread of a window registered
 * with RegisterDragDrop, and user32 calls the handler here with the drag's phase. the target's data object is backed
 * by the drag: its formats come from the host's types, and data is read from the host in chunks on demand. for a drag
 * out of DoDragDrop, the host learns which kinds the data object offers and asks for one when another program reads
 * it. this thread answers from the data object, since the request comes through its message loop.
 */
#include "ole32_private.h"

#include <mime/abi.h>
#include <mime/nt.h>
#include <mime/ntuser.h>

#ifndef CF_HDROP
#define CF_HDROP 15
#endif

#define MAX_HOST_FORMATS 20
#define CHUNK 65536

/* ------------------------------------------------------------------ formats */

static UINT g_png, g_html, g_url_w, g_url_a, g_file_w, g_file_a;

static void init_formats(void)
{
    if (g_file_a)
        return;
    g_png = RegisterClipboardFormatW(L"PNG");
    g_html = RegisterClipboardFormatW(L"HTML Format");
    g_url_w = RegisterClipboardFormatW(L"UniformResourceLocatorW");
    g_url_a = RegisterClipboardFormatW(L"UniformResourceLocator");
    g_file_w = RegisterClipboardFormatW(L"FileNameW");
    g_file_a = RegisterClipboardFormatW(L"FileName");
}

#define format_png() (init_formats(), g_png)
#define format_html() (init_formats(), g_html)
#define format_url_w() (init_formats(), g_url_w)
#define format_url_a() (init_formats(), g_url_a)
#define format_file_w() (init_formats(), g_file_w)
#define format_file_a() (init_formats(), g_file_a)

/* the host data kind a clipboard format maps to, 0 for none. */
static ULONG kind_of_format(UINT format)
{
    switch (format) {
    case CF_UNICODETEXT:
        return MIME_CLIP_TEXT;
    case CF_HDROP:
        return MIME_CLIP_HDROP;
    case CF_DIB:
        return MIME_CLIP_DIB;
    case CF_DIBV5:
        return MIME_CLIP_DIBV5;
    }
    if (format < 0xC000)
        return 0;
    if (format == format_png())
        return MIME_CLIP_PNG;
    if (format == format_html())
        return MIME_CLIP_HTML;
    if (format == format_url_w())
        return MIME_CLIP_URL;
    return 0;
}

/* ------------------------------------------------------------------ data object of a host drag */

typedef struct _HOST_DATA {
    IDataObject Interface;
    LONG Refs;
    ULONG Session;
    ULONG Count;
    CLIPFORMAT Formats[MAX_HOST_FORMATS];
} HOST_DATA;

static const IDataObjectVtbl g_host_data_vtbl;

static bool has_format(const HOST_DATA *d, UINT format)
{
    for (ULONG i = 0; i < d->Count; i++)
        if (d->Formats[i] == format)
            return true;
    return false;
}

static void add_format(HOST_DATA *d, UINT format)
{
    if (format && d->Count < MAX_HOST_FORMATS && !has_format(d, format))
        d->Formats[d->Count++] = (CLIPFORMAT)format;
}

/* fetches a whole format from the host, text gets a terminator. */
static BYTE *fetch_whole(ULONG session, UINT format, SIZE_T *size)
{
    ULONG total = 0;
    if (!NtUserMimeDndData(session, format, nullptr, 0, 0, &total) || !total || total > (256u << 20))
        return nullptr;
    BYTE *data = OleAllocZero((SIZE_T)total + 4);
    if (!data)
        return nullptr;
    for (ULONG offset = 0; offset < total;) {
        ULONG n = total - offset < CHUNK ? total - offset : CHUNK, again = 0;
        if (!NtUserMimeDndData(session, format, data + offset, n, offset, &again) || again != total) {
            OleFree(data);
            return nullptr;
        }
        offset += n;
    }
    *size = total;
    return data;
}

static HGLOBAL make_global(const void *data, SIZE_T size)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    void *p = g ? GlobalLock(g) : nullptr;
    if (!p) {
        if (g)
            GlobalFree(g);
        return nullptr;
    }
    memcpy(p, data, size);
    GlobalUnlock(g);
    return g;
}

/* the first file of a DROPFILES block, as wide text. */
static WCHAR *first_file(const BYTE *block, SIZE_T size)
{
    if (size < 24)
        return nullptr;
    ULONG offset = *(const ULONG *)block;
    bool wide = *(const ULONG *)(block + 16) != 0;
    if (offset >= size)
        return nullptr;
    SIZE_T room = size - offset;
    if (wide) {
        const WCHAR *w = (const WCHAR *)(block + offset);
        SIZE_T n = 0;
        while ((n + 1) * sizeof(WCHAR) <= room && w[n])
            n++;
        if (!n)
            return nullptr;
        WCHAR *copy = OleAllocZero((n + 1) * sizeof(WCHAR));
        if (copy)
            memcpy(copy, w, n * sizeof(WCHAR));
        return copy;
    }
    const char *a = (const char *)(block + offset);
    SIZE_T n = 0;
    while (n < room && a[n])
        n++;
    if (!n)
        return nullptr;
    int chars = MultiByteToWideChar(CP_ACP, 0, a, (int)n, nullptr, 0);
    WCHAR *copy = OleAllocZero(((SIZE_T)chars + 1) * sizeof(WCHAR));
    if (copy)
        MultiByteToWideChar(CP_ACP, 0, a, (int)n, copy, chars);
    return copy;
}

/* converts wide text to terminated ansi or oem text. */
static HGLOBAL narrow(const WCHAR *w, UINT codepage)
{
    int bytes = WideCharToMultiByte(codepage, 0, w, -1, nullptr, 0, nullptr, nullptr);
    HGLOBAL g = bytes > 0 ? GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)bytes) : nullptr;
    char *p = g ? GlobalLock(g) : nullptr;
    if (!p) {
        if (g)
            GlobalFree(g);
        return nullptr;
    }
    WideCharToMultiByte(codepage, 0, w, -1, p, bytes, nullptr, nullptr);
    GlobalUnlock(g);
    return g;
}

static HRESULT check_format(const FORMATETC *format)
{
    if (!format)
        return E_INVALIDARG;
    if (format->lindex != -1)
        return DV_E_LINDEX;
    if (format->dwAspect != DVASPECT_CONTENT)
        return DV_E_DVASPECT;
    if (!(format->tymed & TYMED_HGLOBAL))
        return DV_E_TYMED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_query_interface(IDataObject *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        This->lpVtbl->AddRef(This);
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hd_add_ref(IDataObject *This)
{
    return (ULONG)InterlockedIncrement(&((HOST_DATA *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE hd_release(IDataObject *This)
{
    LONG refs = InterlockedDecrement(&((HOST_DATA *)This)->Refs);
    if (!refs)
        OleFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE hd_get_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    HOST_DATA *d = (HOST_DATA *)This;
    if (!format || !medium)
        return E_INVALIDARG;
    memset(medium, 0, sizeof(*medium));
    HRESULT hr = check_format(format);
    if (FAILED(hr))
        return hr;
    if (!has_format(d, format->cfFormat))
        return DV_E_FORMATETC;
    UINT cf = format->cfFormat, from = cf;
    /* derived formats: ansi text from wide text, a file name from the file list. */
    if (cf == CF_TEXT || cf == CF_OEMTEXT)
        from = CF_UNICODETEXT;
    else if (cf == format_url_a())
        from = format_url_w();
    else if (cf == format_file_w() || cf == format_file_a())
        from = CF_HDROP;
    SIZE_T size = 0;
    BYTE *data = fetch_whole(d->Session, from, &size);
    if (!data)
        return DV_E_FORMATETC;
    HGLOBAL g = nullptr;
    if (from == cf) {
        /* host text has no terminator, the zeroes appended after it serve as one. */
        g = make_global(data, cf == CF_UNICODETEXT ? size + sizeof(WCHAR) : size);
    } else if (cf == CF_TEXT || cf == CF_OEMTEXT) {
        g = narrow((const WCHAR *)data, cf == CF_OEMTEXT ? CP_OEMCP : CP_ACP);
    } else if (cf == format_url_a()) {
        g = narrow((const WCHAR *)data, CP_ACP);
    } else {
        WCHAR *file = first_file(data, size);
        if (file) {
            g = cf == format_file_w() ? make_global(file, (lstrlenW(file) + 1) * sizeof(WCHAR)) : narrow(file, CP_ACP);
            OleFree(file);
        }
    }
    OleFree(data);
    if (!g)
        return E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = g;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hd_get_data_here(IDataObject *This, FORMATETC *format, STGMEDIUM *medium)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE hd_query_get_data(IDataObject *This, FORMATETC *format)
{
    HRESULT hr = check_format(format);
    if (FAILED(hr))
        return hr;
    return has_format((HOST_DATA *)This, format->cfFormat) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE hd_get_canonical_format_etc(IDataObject *This, FORMATETC *in, FORMATETC *out)
{
    if (!in || !out)
        return E_INVALIDARG;
    *out = *in;
    out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT STDMETHODCALLTYPE hd_set_data(IDataObject *This, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE hd_enum_format_etc(IDataObject *This, DWORD direction, IEnumFORMATETC **enumerator)
{
    HOST_DATA *d = (HOST_DATA *)This;
    if (!enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    FORMATETC formats[MAX_HOST_FORMATS];
    for (ULONG i = 0; i < d->Count; i++)
        formats[i] = (FORMATETC){d->Formats[i], nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    *enumerator = OleFormatEnumCreate(formats, d->Count);
    return *enumerator ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE hd_d_advise(IDataObject *This, FORMATETC *format, DWORD flags, IAdviseSink *sink,
                                             DWORD *connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE hd_d_unadvise(IDataObject *This, DWORD connection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE hd_enum_d_advise(IDataObject *This, IEnumSTATDATA **enumerator)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl g_host_data_vtbl = {
    hd_query_interface, hd_add_ref,        hd_release,    hd_get_data,   hd_get_data_here, hd_query_get_data,
    hd_get_canonical_format_etc, hd_set_data, hd_enum_format_etc, hd_d_advise, hd_d_unadvise, hd_enum_d_advise,
};

static HOST_DATA *host_data_create(ULONG session, const ULONG *state)
{
    HOST_DATA *d = OleAllocZero(sizeof(*d));
    if (!d)
        return nullptr;
    d->Interface.lpVtbl = &g_host_data_vtbl;
    d->Refs = 1;
    d->Session = session;
    for (ULONG i = 0; i < state[5] && i < MIME_DND_STATE_SIZE - 6; i++)
        add_format(d, state[6 + i]);
    if (has_format(d, CF_UNICODETEXT)) {
        add_format(d, CF_TEXT);
        add_format(d, CF_OEMTEXT);
    }
    if (has_format(d, format_url_w()))
        add_format(d, format_url_a());
    if (has_format(d, CF_HDROP)) {
        add_format(d, format_file_w());
        add_format(d, format_file_a());
    }
    return d;
}

/* ------------------------------------------------------------------ target side */

typedef struct _ENTERED {
    ULONG Session;
    HWND Window;
    IDropTarget *Target;
    HOST_DATA *Data;
} ENTERED;

static SRWLOCK g_lock = SRWLOCK_INIT;
static ENTERED g_entered[4];

/* the drag entered over a window, with its target and data object referenced. */
static bool take_entered(ULONG session, HWND window, ENTERED *out, bool remove)
{
    bool found = false;
    AcquireSRWLockExclusive(&g_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_entered); i++) {
        if (g_entered[i].Target && g_entered[i].Session == session && g_entered[i].Window == window) {
            *out = g_entered[i];
            if (remove)
                g_entered[i] = (ENTERED){};
            else {
                out->Target->lpVtbl->AddRef(out->Target);
                out->Data->Interface.lpVtbl->AddRef(&out->Data->Interface);
            }
            found = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return found;
}

static void put_entered(const ENTERED *e)
{
    AcquireSRWLockExclusive(&g_lock);
    ULONG slot = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_entered); i++)
        if (!g_entered[i].Target) {
            slot = i;
            break;
        }
    ENTERED old = g_entered[slot];
    g_entered[slot] = *e;
    ReleaseSRWLockExclusive(&g_lock);
    if (old.Target) {
        /* a drag that never ended. */
        old.Target->lpVtbl->DragLeave(old.Target);
        old.Target->lpVtbl->Release(old.Target);
        old.Data->Interface.lpVtbl->Release(&old.Data->Interface);
    }
}

static void reply(ULONG session, ULONG phase, DWORD effect)
{
    NtUserMimeDnd(MIME_DND_REPLY, session, phase << 8 | (effect & 7), nullptr);
}

static void release_entered(ENTERED *e)
{
    e->Target->lpVtbl->Release(e->Target);
    e->Data->Interface.lpVtbl->Release(&e->Data->Interface);
}

static void target_phase(HWND hwnd, ULONG phase, ULONG session)
{
    ULONG state[MIME_DND_STATE_SIZE];
    ENTERED e = {};
    if (phase == MIME_DND_PHASE_LEAVE) {
        if (take_entered(session, hwnd, &e, true)) {
            e.Target->lpVtbl->DragLeave(e.Target);
            release_entered(&e);
        }
        return;
    }
    if (!NtUserMimeDnd(MIME_DND_STATE, session, 0, state))
        return;
    DWORD allowed = state[1], keys = state[2], effect = allowed;
    POINTL pt = {(LONG)state[3], (LONG)state[4]};
    bool drop = phase == MIME_DND_PHASE_DROP;
    /* we own the entry's target and data object references, and a drop takes the entry out of the table. */
    bool entered = take_entered(session, hwnd, &e, drop);
    if (!entered) {
        /* the first message for a window enters it, whatever its phase. */
        IDropTarget *target = OleFindDropTarget(hwnd);
        HOST_DATA *data = target ? host_data_create(session, state) : nullptr;
        if (!data) {
            if (target)
                target->lpVtbl->Release(target);
            reply(session, phase, 0);
            return;
        }
        e = (ENTERED){session, hwnd, target, data};
        HRESULT hr = target->lpVtbl->DragEnter(target, &data->Interface, keys, pt, &effect);
        effect = FAILED(hr) ? 0 : effect & allowed;
        if (!drop) {
            e.Target->lpVtbl->AddRef(e.Target);
            e.Data->Interface.lpVtbl->AddRef(&e.Data->Interface);
            put_entered(&e);
            reply(session, phase, effect);
            release_entered(&e);
            return;
        }
        effect = allowed;
    }
    HRESULT hr;
    if (drop)
        hr = e.Target->lpVtbl->Drop(e.Target, &e.Data->Interface, keys, pt, &effect);
    else
        hr = e.Target->lpVtbl->DragOver(e.Target, keys, pt, &effect);
    reply(session, phase, FAILED(hr) ? 0 : effect & allowed);
    release_entered(&e);
}

/* ------------------------------------------------------------------ source side */

typedef struct _HOST_DRAG {
    IDataObject *Data;
    ULONG Session;
    bool Ended;
    bool Dropped;
    DWORD Effect;
} HOST_DRAG;

static HOST_DRAG g_drag;

/* text comes without a terminator. null when the data object lacks the kind. */
static BYTE *gather(IDataObject *data, ULONG kind, SIZE_T *size)
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
        break;
    case MIME_CLIP_DIBV5:
        candidates[0] = CF_DIBV5;
        break;
    case MIME_CLIP_PNG:
        candidates[0] = format_png();
        break;
    case MIME_CLIP_HTML:
        candidates[0] = format_html();
        break;
    case MIME_CLIP_HDROP:
        candidates[0] = CF_HDROP;
        break;
    case MIME_CLIP_URL:
        candidates[0] = format_url_w();
        break;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(candidates); i++) {
        if (!candidates[i])
            continue;
        FORMATETC format = {(CLIPFORMAT)candidates[i], nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (FAILED(data->lpVtbl->GetData(data, &format, &medium)))
            continue;
        BYTE *out = nullptr;
        if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
            SIZE_T n = GlobalSize(medium.hGlobal);
            const BYTE *p = GlobalLock(medium.hGlobal);
            if (p) {
                if (kind != MIME_CLIP_TEXT) {
                    out = OleAlloc(n ? n : 1);
                    if (out) {
                        memcpy(out, p, n);
                        *size = n;
                    }
                } else if (candidates[i] == CF_UNICODETEXT) {
                    const WCHAR *w = (const WCHAR *)p;
                    SIZE_T chars = 0;
                    while ((chars + 1) * sizeof(WCHAR) <= n && w[chars])
                        chars++;
                    out = OleAlloc(chars * sizeof(WCHAR) + 2);
                    if (out) {
                        memcpy(out, w, chars * sizeof(WCHAR));
                        *size = chars * sizeof(WCHAR);
                    }
                } else {
                    UINT cp = candidates[i] == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
                    SIZE_T len = 0;
                    while (len < n && p[len])
                        len++;
                    int chars = MultiByteToWideChar(cp, 0, (const char *)p, (int)len, nullptr, 0);
                    WCHAR *w = OleAlloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
                    if (w) {
                        MultiByteToWideChar(cp, 0, (const char *)p, (int)len, w, chars);
                        out = (BYTE *)w;
                        *size = (SIZE_T)chars * sizeof(WCHAR);
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
        if (out)
            return out;
    }
    return nullptr;
}

static void source_request(ULONG slot)
{
    ULONG pair[2];
    if (!NtUserMimeDnd(MIME_DND_REQUEST, slot, 0, pair))
        return;
    ULONG kind = pair[0], id = pair[1];
    SIZE_T size = 0;
    IDataObject *data = g_drag.Data;
    BYTE *bytes = data && !g_drag.Ended ? gather(data, kind, &size) : nullptr;
    if (!bytes || !size) {
        NtUserMimeDndProvide(id, nullptr, 0, 0, 0xFFFFFFFFu);
    } else {
        for (SIZE_T offset = 0; offset < size;) {
            ULONG n = size - offset < CHUNK ? (ULONG)(size - offset) : CHUNK;
            if (!NtUserMimeDndProvide(id, bytes + offset, n, (ULONG)offset, (ULONG)size))
                break;
            offset += n;
        }
    }
    OleFree(bytes);
}

static void source_end(void)
{
    ULONG result = (ULONG)NtUserMimeDnd(MIME_DND_RESULT, g_drag.Session, 0, nullptr);
    if (result == MIME_DND_GOING)
        return;
    g_drag.Dropped = (result & 0x100) != 0;
    g_drag.Effect = result & 7;
    g_drag.Ended = true;
}

static ULONG kinds_of(IDataObject *data)
{
    ULONG kinds = 0;
    IEnumFORMATETC *e;
    if (SUCCEEDED(data->lpVtbl->EnumFormatEtc(data, DATADIR_GET, &e)) && e) {
        FORMATETC format;
        ULONG fetched;
        while (e->lpVtbl->Next(e, 1, &format, &fetched) == S_OK && fetched == 1) {
            if (format.tymed & TYMED_HGLOBAL) {
                kinds |= kind_of_format(format.cfFormat);
                /* ansi text and links map to the wide kinds, gather widens them. */
                if (format.cfFormat == CF_TEXT || format.cfFormat == CF_OEMTEXT)
                    kinds |= MIME_CLIP_TEXT;
            }
            CoTaskMemFree(format.ptd);
        }
        e->lpVtbl->Release(e);
    }
    return kinds;
}

/* ------------------------------------------------------------------ drag image */

/*
 * turns the drag image the source's helper put in the data object into the MimeDragIcon the host shows. as in shell32's
 * dragdrop.c, "DragContext" says an image is there and "DragImageBits" holds it, bottom row first. images over 512
 * pixels are scaled down by whole pixels. null when there is none, the caller owns the buffer.
 */
static MimeDragIcon *make_icon(IDataObject *data, ULONG *bytes)
{
    UINT context_cf = RegisterClipboardFormatW(L"DragContext"), bits_cf = RegisterClipboardFormatW(L"DragImageBits");
    FORMATETC format = {(CLIPFORMAT)context_cf, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
    STGMEDIUM medium = {};
    if (FAILED(data->lpVtbl->GetData(data, &format, &medium)))
        return nullptr;
    DWORD context[4] = {};
    BOOL has_bits = FALSE;
    if (medium.tymed == TYMED_ISTREAM && medium.pstm) {
        LARGE_INTEGER zero = {};
        ULONG read = 0;
        medium.pstm->lpVtbl->Seek(medium.pstm, zero, STREAM_SEEK_SET, nullptr);
        has_bits = SUCCEEDED(medium.pstm->lpVtbl->Read(medium.pstm, context, sizeof(context), &read)) &&
                   read == sizeof(context) && context[1];
        medium.pstm->lpVtbl->Seek(medium.pstm, zero, STREAM_SEEK_SET, nullptr);
    }
    ReleaseStgMedium(&medium);
    if (!has_bits)
        return nullptr;
    format.cfFormat = (CLIPFORMAT)bits_cf;
    format.tymed = TYMED_HGLOBAL;
    memset(&medium, 0, sizeof(medium));
    if (FAILED(data->lpVtbl->GetData(data, &format, &medium)))
        return nullptr;
    MimeDragIcon *icon = nullptr;
    SIZE_T size = medium.tymed == TYMED_HGLOBAL && medium.hGlobal ? GlobalSize(medium.hGlobal) : 0;
    const LONG *header = size >= 24 ? GlobalLock(medium.hGlobal) : nullptr;
    if (header && header[0] > 0 && header[1] > 0 && header[0] <= 16384 && header[1] <= 16384 &&
        size >= 24 + (SIZE_T)header[0] * header[1] * 4) {
        LONG width = header[0], height = header[1];
        const DWORD *pixels = (const DWORD *)(header + 6);
        LONG step = 1;
        while (width / step > MIME_DRAG_ICON_MAX || height / step > MIME_DRAG_ICON_MAX)
            step++;
        LONG w = width / step, h = height / step;
        icon = OleAlloc(sizeof(MimeDragIcon) + (SIZE_T)w * h * 4);
        if (icon) {
            icon->width = (uint32_t)w;
            icon->height = (uint32_t)h;
            icon->hot_x = header[2] / step;
            icon->hot_y = header[3] / step;
            for (LONG y = 0; y < h; y++)
                for (LONG x = 0; x < w; x++)
                    icon->pixels[(SIZE_T)y * w + x] = pixels[(SIZE_T)(height - 1 - y * step) * width + x * step];
            *bytes = (ULONG)(sizeof(MimeDragIcon) + (SIZE_T)w * h * 4);
        }
    }
    if (header)
        GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    return icon;
}

/*
 * the DoDragDrop drag left the source's window and the host takes it over. false when it cannot, such as without a
 * display or when nothing in the data object crosses over, and the drag then stays in the guest.
 */
bool OleHostDragBegin(IDataObject *data, DWORD allowed)
{
    if (g_drag.Data)
        return false;
    ULONG kinds = kinds_of(data);
    if (!kinds)
        return false;
    /* send the image before the start, which hands it to the compositor as the drag icon. */
    ULONG icon_bytes = 0;
    MimeDragIcon *icon = make_icon(data, &icon_bytes);
    if (icon) {
        NtUserMimeDnd(MIME_DND_ICON, icon_bytes, 0, (PULONG)icon);
        OleFree(icon);
    }
    ULONG session = (ULONG)NtUserMimeDnd(MIME_DND_START, kinds, allowed & 7, (PULONG)(ULONG_PTR)OleHostDndHandler);
    if (!session)
        return false;
    g_drag = (HOST_DRAG){data, session, false, false, 0};
    data->lpVtbl->AddRef(data);
    return true;
}

bool OleHostDragEnded(bool *dropped, DWORD *effect)
{
    if (!g_drag.Data || !g_drag.Ended)
        return false;
    *dropped = g_drag.Dropped;
    *effect = g_drag.Effect;
    return true;
}

bool OleHostDragActive(void)
{
    return g_drag.Data != nullptr;
}

void OleHostDragFinish(void)
{
    if (!g_drag.Data)
        return;
    if (!g_drag.Ended)
        NtUserMimeDnd(MIME_DND_STOP, g_drag.Session, 0, nullptr);
    IDataObject *data = g_drag.Data;
    g_drag = (HOST_DRAG){};
    data->lpVtbl->Release(data);
}

/* ------------------------------------------------------------------ entry points */

void WINAPI OleHostDndHandler(HWND hwnd, LPARAM lparam)
{
    ULONG phase = (ULONG)lparam & 15, rest = (ULONG)lparam >> 4;
    switch (phase) {
    case MIME_DND_PHASE_REQUEST:
        source_request(rest);
        break;
    case MIME_DND_PHASE_END:
        source_end();
        break;
    default:
        target_phase(hwnd, phase, rest);
        break;
    }
}

void OleHostDndRegister(HWND hwnd)
{
    NtUserMimeDnd(MIME_DND_REGISTER, (ULONG_PTR)hwnd, (ULONG_PTR)OleHostDndHandler, nullptr);
}

void OleHostDndRevoke(HWND hwnd)
{
    NtUserMimeDnd(MIME_DND_REVOKE, (ULONG_PTR)hwnd, 0, nullptr);
}

bool OleHostPointerOutside(void)
{
    return NtUserMimeDnd(MIME_DND_OUTSIDE, 0, 0, nullptr) != 0;
}
