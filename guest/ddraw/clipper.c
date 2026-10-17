/*
 * IDirectDrawClipper: a window whose client area clips, or a list of
 * rectangles. mime's windows are each a window of the host, so a window's
 * visible region is its client area.
 */
#include "ddraw_private.h"

static const IDirectDrawClipperVtbl clipper_vtbl;

DDCLIP *DdClipFromIface(void *iface)
{
    if (!iface || *(const void **)iface != &clipper_vtbl)
        return nullptr;
    DDCLIP *c = CONTAINING_RECORD(iface, DDCLIP, iface);
    return c->magic == CLIP_MAGIC ? c : nullptr;
}

/* clippers of DirectDrawCreateClipper belong to no object and live on their own. */
static LIST_ENTRY g_loose = {&g_loose, &g_loose};

HRESULT DdCreateClipper(DDRAW *dd, DWORD flags, DDCLIP **out, BOOL initialized)
{
    *out = nullptr;
    if (flags)
        return DDERR_INVALIDPARAMS;
    DDCLIP *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return DDERR_OUTOFMEMORY;
    c->iface.lpVtbl = &clipper_vtbl;
    c->ref = 1;
    c->magic = CLIP_MAGIC;
    c->dd = dd;
    c->initialized = initialized;
    InsertTailList(dd ? &dd->clippers : &g_loose, &c->links);
    *out = c;
    return DD_OK;
}

void DdClipKill(DDCLIP *c)
{
    RemoveEntryList(&c->links);
    InsertTailList(&g_loose, &c->links);
    c->dd = nullptr;
}

static RGNDATA *make_region(const RECT *rects, DWORD count)
{
    RGNDATA *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RGNDATAHEADER) + count * sizeof(RECT));
    if (!r)
        return nullptr;
    r->rdh.dwSize = sizeof(RGNDATAHEADER);
    r->rdh.iType = RDH_RECTANGLES;
    r->rdh.nCount = count;
    r->rdh.nRgnSize = count * sizeof(RECT);
    SetRectEmpty(&r->rdh.rcBound);
    memcpy(r->Buffer, rects, count * sizeof(RECT));
    for (DWORD i = 0; i < count; i++)
        UnionRect(&r->rdh.rcBound, &r->rdh.rcBound, &rects[i]);
    return r;
}

/* the window's client area in screen coordinates, nothing when it is minimized. */
static BOOL window_rect(HWND window, RECT *rect)
{
    if (!IsWindow(window) || IsIconic(window) || !GetClientRect(window, rect))
        return FALSE;
    MapWindowPoints(window, nullptr, (POINT *)rect, 2);
    return TRUE;
}

/* windows' ClipRgnToRect: each rectangle cut to the bounds, empty ones dropped; the header's bounds stay. */
static void clip_to(RGNDATA *r, const RECT *bounds)
{
    if (!bounds)
        return;
    RECT inside;
    if (IntersectRect(&inside, &r->rdh.rcBound, bounds) && EqualRect(&inside, &r->rdh.rcBound))
        return;
    RECT *rects = (RECT *)r->Buffer;
    DWORD n = 0;
    for (DWORD i = 0; i < r->rdh.nCount; i++)
        if (IntersectRect(&rects[n], &rects[i], bounds))
            n++;
    r->rdh.nCount = n;
}

RGNDATA *DdClipRegion(DDCLIP *c, const RECT *bounds)
{
    RGNDATA *r = nullptr;
    if (c->window) {
        RECT rect;
        if (!window_rect(c->window, &rect))
            return make_region(nullptr, 0);
        r = make_region(&rect, 1);
    } else if (c->list) {
        DWORD size = sizeof(RGNDATAHEADER) + c->list->rdh.nCount * sizeof(RECT);
        r = HeapAlloc(GetProcessHeap(), 0, size);
        if (r)
            memcpy(r, c->list, size);
    }
    if (r)
        clip_to(r, bounds);
    return r;
}

/* ------------------------------------------------------------------ methods */

static HRESULT STDMETHODCALLTYPE clipper_query(IDirectDrawClipper *This, REFIID riid, void **out)
{
    DDCLIP *c = DdClipFromIface(This);
    if (!c || !riid || !out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectDrawClipper))
        return E_NOINTERFACE;
    DdEnter();
    c->ref++;
    *out = This;
    return DdLeave(DD_OK);
}

static ULONG STDMETHODCALLTYPE clipper_add_ref(IDirectDrawClipper *This)
{
    DDCLIP *c = DdClipFromIface(This);
    if (!c)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)++c->ref;
    DdLeave(DD_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE clipper_release(IDirectDrawClipper *This)
{
    DDCLIP *c = DdClipFromIface(This);
    if (!c)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)--c->ref;
    if (ref) {
        DdLeave(DD_OK);
        return ref;
    }
    void *owner = c->owner;
    RemoveEntryList(&c->links);
    HeapFree(GetProcessHeap(), 0, c->list);
    c->magic = 0;
    HeapFree(GetProcessHeap(), 0, c);
    DdLeave(DD_OK);
    if (owner)
        ((IUnknown *)owner)->lpVtbl->Release(owner);
    return 0;
}

static DDCLIP *enter(IDirectDrawClipper *This, HRESULT *hr)
{
    DDCLIP *c = DdClipFromIface(This);
    if (!c) {
        *hr = DDERR_INVALIDPARAMS;
        return nullptr;
    }
    DdEnter();
    if (!c->initialized) {
        *hr = DdLeave(DDERR_NOTINITIALIZED);
        return nullptr;
    }
    *hr = DD_OK;
    return c;
}

static HRESULT STDMETHODCALLTYPE clipper_get_clip_list(IDirectDrawClipper *This, LPRECT rect, LPRGNDATA data,
                                                       LPDWORD size)
{
    HRESULT hr;
    DDCLIP *c = enter(This, &hr);
    if (!c)
        return hr;
    if (!size)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (data)
        data->rdh.nCount = 0;
    if (!c->window && !c->list)
        return DdLeave(DDERR_NOCLIPLIST);
    RGNDATA *r = DdClipRegion(c, nullptr);
    if (!r)
        return DdLeave(DDERR_GENERIC);
    DWORD needed = sizeof(RGNDATAHEADER) + r->rdh.nCount * sizeof(RECT);
    if (!data) {
        *size = needed;
    } else if (*size < needed) {
        *size = needed;
        hr = DDERR_REGIONTOOSMALL;
    } else {
        memcpy(data, r, needed);
        clip_to(data, rect);
    }
    HeapFree(GetProcessHeap(), 0, r);
    return DdLeave(hr);
}

static HRESULT STDMETHODCALLTYPE clipper_get_hwnd(IDirectDrawClipper *This, HWND *window)
{
    HRESULT hr;
    DDCLIP *c = enter(This, &hr);
    if (!c)
        return hr;
    if (!window)
        return DdLeave(DDERR_INVALIDPARAMS);
    *window = c->window;
    return DdLeave(DD_OK);
}

/* a clipper of CoCreateInstance joins the object it is initialized with, without referencing it. */
static HRESULT STDMETHODCALLTYPE clipper_initialize(IDirectDrawClipper *This, LPDIRECTDRAW ddraw, DWORD flags)
{
    DDCLIP *c = DdClipFromIface(This);
    if (!c)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    if (c->initialized)
        return DdLeave(DDERR_ALREADYINITIALIZED);
    if (flags)
        return DdLeave(DDERR_INVALIDPARAMS);
    DDRAW *dd = DdFromIface(ddraw);
    if (dd) {
        RemoveEntryList(&c->links);
        InsertTailList(&dd->clippers, &c->links);
        c->dd = dd;
    }
    c->initialized = TRUE;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE clipper_is_clip_list_changed(IDirectDrawClipper *This, BOOL *changed)
{
    HRESULT hr;
    DDCLIP *c = enter(This, &hr);
    if (!c)
        return hr;
    if (!changed)
        return DdLeave(DDERR_INVALIDPARAMS);
    *changed = TRUE;
    return DdLeave(DD_OK);
}

/* clippers of the first IDirectDraw take any header; later ones want rectangles. */
static HRESULT STDMETHODCALLTYPE clipper_set_clip_list(IDirectDrawClipper *This, LPRGNDATA data, DWORD flags)
{
    HRESULT hr;
    DDCLIP *c = enter(This, &hr);
    if (!c)
        return hr;
    if (flags)
        return DdLeave(DDERR_INVALIDPARAMS);
    if (data && (!data->rdh.nCount ||
                 (c->version != DDV1 && (data->rdh.dwSize < sizeof(RGNDATAHEADER) || data->rdh.iType != RDH_RECTANGLES))))
        return DdLeave(DDERR_INVALIDCLIPLIST);
    if (c->window)
        return DdLeave(DDERR_CLIPPERISUSINGHWND);
    RGNDATA *copy = nullptr;
    if (data) {
        DWORD size = sizeof(RGNDATAHEADER) + data->rdh.nCount * sizeof(RECT);
        copy = HeapAlloc(GetProcessHeap(), 0, size);
        if (!copy)
            return DdLeave(DDERR_OUTOFMEMORY);
        memcpy(copy, data, size);
    }
    HeapFree(GetProcessHeap(), 0, c->list);
    c->list = copy;
    return DdLeave(DD_OK);
}

static HRESULT STDMETHODCALLTYPE clipper_set_hwnd(IDirectDrawClipper *This, DWORD flags, HWND window)
{
    HRESULT hr;
    DDCLIP *c = enter(This, &hr);
    if (!c)
        return hr;
    if (flags || (window && !IsWindow(window)))
        return DdLeave(DDERR_INVALIDPARAMS);
    c->window = window;
    return DdLeave(DD_OK);
}

static const IDirectDrawClipperVtbl clipper_vtbl = {
    clipper_query,      clipper_add_ref,     clipper_release,
    clipper_get_clip_list, clipper_get_hwnd, clipper_initialize,
    clipper_is_clip_list_changed, clipper_set_clip_list, clipper_set_hwnd,
};
