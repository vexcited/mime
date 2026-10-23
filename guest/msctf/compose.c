/*
 * compositions: text a text service is composing in a context, not final until it ends. the context's owner is
 * asked before one starts and told when it changes and ends, so it can show the text as being composed.
 */
#include "msctf_private.h"

extern const ITfCompositionVtbl g_composition_vtbl;
extern const ITfCompositionViewVtbl g_view_vtbl;

static COMP *comp_from_view(ITfCompositionView *view)
{
    return view && view->lpVtbl == &g_view_vtbl ? CONTAINING_RECORD(view, COMP, View) : nullptr;
}

static void comp_add_ref(COMP *comp)
{
    InterlockedIncrement(&comp->Refs);
}

static void comp_release(COMP *comp)
{
    if (InterlockedDecrement(&comp->Refs))
        return;
    if (comp->Range) {
        ITfRangeACP *range = &comp->Range->Iface;
        comp->Range = nullptr;
        range->lpVtbl->Release(range);
    }
    if (comp->Sink)
        comp->Sink->lpVtbl->Release(comp->Sink);
    CtfFree(comp);
    CtfObjectDestroyed();
}

static void list_remove(CTX *ctx, COMP *comp)
{
    for (COMP **link = &ctx->Compositions; *link; link = &(*link)->Next) {
        if (*link == comp) {
            *link = comp->Next;
            comp->Next = nullptr;
            return;
        }
    }
}

/* removes a composition from the context: the owner is told and the property goes. */
static void finish(CTX *ctx, COMP *comp)
{
    if (comp->Ended)
        return;
    comp->Ended = TRUE;
    comp_add_ref(comp);
    if (ctx->OwnerSink)
        ctx->OwnerSink->lpVtbl->OnEndComposition(ctx->OwnerSink, &comp->View);
    PropsSet(ctx, &GUID_PROP_COMPOSING, comp->Range->Start, comp->Range->End, nullptr);
    list_remove(ctx, comp);
    comp_release(comp);
    comp_release(comp);
}

void CompFreeAll(CTX *ctx)
{
    while (ctx->Compositions) {
        COMP *comp = ctx->Compositions;
        ctx->Compositions = comp->Next;
        comp->Next = nullptr;
        comp->Ended = TRUE;
        comp->Context = nullptr;
        comp_release(comp);
    }
}

void CompAbortAll(CTX *ctx)
{
    while (ctx->Compositions)
        finish(ctx, ctx->Compositions);
}

/* ------------------------------------------------------------------ the composition */

static HRESULT STDMETHODCALLTYPE cm_query(ITfComposition *This, REFIID riid, void **object)
{
    COMP *comp = (COMP *)This;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfComposition))
        *object = &comp->Iface;
    else if (IsEqualIID(riid, &IID_ITfCompositionView))
        *object = &comp->View;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    comp_add_ref(comp);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cm_add_ref(ITfComposition *This)
{
    return InterlockedIncrement(&((COMP *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE cm_release(ITfComposition *This)
{
    COMP *comp = (COMP *)This;
    LONG refs = comp->Refs - 1;
    comp_release(comp);
    return refs;
}

static HRESULT clone_range(COMP *comp, ITfRange **range)
{
    if (!range)
        return E_INVALIDARG;
    *range = nullptr;
    if (comp->Ended || !comp->Context)
        return E_UNEXPECTED;
    return RngCreate(comp->Context, comp->Range->Start, comp->Range->End, range);
}

static HRESULT STDMETHODCALLTYPE cm_get_range(ITfComposition *This, ITfRange **range)
{
    return clone_range((COMP *)This, range);
}

/* a start or end moved: the owner is asked before the composition changes. */
static HRESULT shift(COMP *comp, TfEditCookie cookie, ITfRange *range, BOOL start_side)
{
    CTX *ctx = comp->Context;
    RNG *rng = RngFromInterface(range);
    if (comp->Ended || !ctx)
        return E_UNEXPECTED;
    if (!CtxCheckCookie(ctx, cookie, TRUE))
        return TS_E_NOLOCK;
    if (!rng || rng->Context != ctx)
        return E_INVALIDARG;
    LONG start = comp->Range->Start, end = comp->Range->End;
    if (start_side) {
        if (rng->Start > end)
            return E_INVALIDARG;
        start = rng->Start;
    } else {
        if (rng->End < start)
            return E_INVALIDARG;
        end = rng->End;
    }
    PropsSet(ctx, &GUID_PROP_COMPOSING, comp->Range->Start, comp->Range->End, nullptr);
    if (ctx->OwnerSink) {
        ITfRange *updated;
        if (SUCCEEDED(RngCreate(ctx, start, end, &updated))) {
            ctx->OwnerSink->lpVtbl->OnUpdateComposition(ctx->OwnerSink, &comp->View, updated);
            updated->lpVtbl->Release(updated);
        }
    }
    comp->Range->Start = start;
    comp->Range->End = end;
    if (end > start) {
        VARIANT one;
        VariantInit(&one);
        V_VT(&one) = VT_I4;
        V_I4(&one) = 1;
        PropsSet(ctx, &GUID_PROP_COMPOSING, start, end, &one);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cm_shift_start(ITfComposition *This, TfEditCookie cookie, ITfRange *range)
{
    return shift((COMP *)This, cookie, range, TRUE);
}

static HRESULT STDMETHODCALLTYPE cm_shift_end(ITfComposition *This, TfEditCookie cookie, ITfRange *range)
{
    return shift((COMP *)This, cookie, range, FALSE);
}

static HRESULT STDMETHODCALLTYPE cm_end(ITfComposition *This, TfEditCookie cookie)
{
    COMP *comp = (COMP *)This;
    CTX *ctx = comp->Context;
    if (comp->Ended || !ctx)
        return E_UNEXPECTED;
    if (!CtxCheckCookie(ctx, cookie, TRUE))
        return TS_E_NOLOCK;
    finish(ctx, comp);
    return S_OK;
}

const ITfCompositionVtbl g_composition_vtbl = {cm_query, cm_add_ref, cm_release, cm_get_range, cm_shift_start,
                                               cm_shift_end, cm_end};

/* ------------------------------------------------------------------ the view the program gets */

static HRESULT STDMETHODCALLTYPE view_query(ITfCompositionView *This, REFIID riid, void **object)
{
    return cm_query(&CONTAINING_RECORD(This, COMP, View)->Iface, riid, object);
}

static ULONG STDMETHODCALLTYPE view_add_ref(ITfCompositionView *This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, COMP, View)->Refs);
}

static ULONG STDMETHODCALLTYPE view_release(ITfCompositionView *This)
{
    return cm_release(&CONTAINING_RECORD(This, COMP, View)->Iface);
}

static HRESULT STDMETHODCALLTYPE view_get_owner(ITfCompositionView *This, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    *clsid = CONTAINING_RECORD(This, COMP, View)->Owner;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE view_get_range(ITfCompositionView *This, ITfRange **range)
{
    return clone_range(CONTAINING_RECORD(This, COMP, View), range);
}

const ITfCompositionViewVtbl g_view_vtbl = {view_query, view_add_ref, view_release, view_get_owner, view_get_range};

/* ------------------------------------------------------------------ starting and finding */

/* tells the composition's sink it ended, then removes it from the context. */
static void end_through_sink(CTX *ctx, COMP *comp, TfEditCookie cookie)
{
    comp_add_ref(comp);
    if (comp->Sink)
        comp->Sink->lpVtbl->OnCompositionTerminated(comp->Sink, cookie, &comp->Iface);
    finish(ctx, comp);
    comp_release(comp);
}

static COMP *comp_create(CTX *ctx, const RNG *extent, ITfCompositionSink *sink, const CLSID *owner)
{
    COMP *comp = CtfAlloc(sizeof(*comp));
    if (!comp)
        return nullptr;
    if (FAILED(RngCreateWeak(ctx, extent->Start, extent->End, &comp->Range))) {
        CtfFree(comp);
        return nullptr;
    }
    comp->Iface.lpVtbl = &g_composition_vtbl;
    comp->View.lpVtbl = &g_view_vtbl;
    comp->Refs = 1;
    comp->Context = ctx;
    comp->Owner = *owner;
    comp->Sink = sink;
    sink->lpVtbl->AddRef(sink);
    CtfObjectCreated();
    return comp;
}

/* whether the context's owner lets the composition start. a context without an owner sink allows anything. */
static HRESULT owner_allows(CTX *ctx, COMP *comp, BOOL *allowed)
{
    *allowed = TRUE;
    if (!ctx->OwnerSink)
        return S_OK;
    return ctx->OwnerSink->lpVtbl->OnStartComposition(ctx->OwnerSink, &comp->View, allowed);
}

HRESULT CompStart(CTX *ctx, TfEditCookie cookie, ITfRange *range, ITfCompositionSink *sink, const CLSID *owner,
                  ITfComposition **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!range || !sink)
        return E_INVALIDARG;
    RNG *extent = RngFromInterface(range);
    if (!extent || extent->Context != ctx)
        return E_INVALIDARG;
    if (!ctx->Doc)
        return TF_E_DISCONNECTED;
    if (!CtxCheckCookie(ctx, cookie, TRUE))
        return TS_E_NOLOCK;

    COMP *comp = comp_create(ctx, extent, sink, owner);
    if (!comp)
        return E_OUTOFMEMORY;

    BOOL allowed;
    HRESULT hr = owner_allows(ctx, comp, &allowed);
    if (FAILED(hr)) {
        comp_release(comp);
        return E_FAIL;
    }
    if (!allowed && ctx->Compositions) {
        /* the newest composition makes room for this one, and the owner is asked again. */
        end_through_sink(ctx, ctx->Compositions, cookie);
        hr = owner_allows(ctx, comp, &allowed);
        if (FAILED(hr))
            allowed = FALSE;
    }
    if (!allowed) {
        comp_release(comp);
        return S_OK;
    }

    if (comp->Range->End > comp->Range->Start) {
        VARIANT one;
        VariantInit(&one);
        V_VT(&one) = VT_I4;
        V_I4(&one) = 1;
        PropsSet(ctx, &GUID_PROP_COMPOSING, comp->Range->Start, comp->Range->End, &one);
    }
    comp->Next = ctx->Compositions;
    ctx->Compositions = comp;
    comp_add_ref(comp);
    *out = &comp->Iface;
    return S_OK;
}

HRESULT CompEnum(CTX *ctx, ITfRange *test, IEnumITfCompositionView **out)
{
    IUnknown *items[8];
    ULONG n = 0;
    RNG *rng = test ? RngFromInterface(test) : nullptr;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!ctx->Doc)
        return TF_E_DISCONNECTED;
    if (test && (!rng || rng->Context != ctx))
        return E_INVALIDARG;
    for (COMP *comp = ctx->Compositions; comp && n < 8; comp = comp->Next) {
        if (rng) {
            LONG s = comp->Range->Start, e = comp->Range->End;
            BOOL overlap = rng->Start == rng->End ? rng->Start >= s && rng->Start <= e
                                                   : rng->Start < e && rng->End > s;
            if (!overlap)
                continue;
        }
        items[n++] = (IUnknown *)&comp->View;
    }
    return EnumCreate(&IID_IEnumITfCompositionView, items, n, (void **)out);
}

/* text a service sets inside a composition is the composition's. */
void CompMarkAll(CTX *ctx)
{
    for (COMP *comp = ctx->Compositions; comp; comp = comp->Next) {
        if (comp->Range->End <= comp->Range->Start)
            continue;
        VARIANT one;
        VariantInit(&one);
        V_VT(&one) = VT_I4;
        V_I4(&one) = 1;
        PropsSet(ctx, &GUID_PROP_COMPOSING, comp->Range->Start, comp->Range->End, &one);
    }
}

BOOL CompExtent(CTX *ctx, LONG *start, LONG *end)
{
    if (!ctx->Compositions)
        return FALSE;
    *start = ctx->Compositions->Range->Start;
    *end = ctx->Compositions->Range->End;
    return TRUE;
}

HRESULT CompTakeOwnership(CTX *ctx, TfEditCookie cookie, ITfCompositionView *view, ITfCompositionSink *sink,
                          ITfComposition **out)
{
    COMP *comp = comp_from_view(view);
    if (!out || !view || !sink)
        return E_INVALIDARG;
    *out = nullptr;
    if (!comp || comp->Context != ctx || comp->Ended)
        return E_INVALIDARG;
    if (!CtxCheckCookie(ctx, cookie, TRUE))
        return TS_E_NOLOCK;
    sink->lpVtbl->AddRef(sink);
    if (comp->Sink)
        comp->Sink->lpVtbl->Release(comp->Sink);
    comp->Sink = sink;
    comp_add_ref(comp);
    *out = &comp->Iface;
    return S_OK;
}

/* ------------------------------------------------------------------ termination */

typedef struct TERMINATE {
    ITfEditSession Iface;
    CTX *Context;
    COMP *Only;
} TERMINATE;

static HRESULT STDMETHODCALLTYPE term_query(ITfEditSession *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

/* a session on the stack, so nobody holds a reference. */
static ULONG STDMETHODCALLTYPE term_add_ref(ITfEditSession *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE term_release(ITfEditSession *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE term_do(ITfEditSession *This, TfEditCookie cookie)
{
    TERMINATE *t = (TERMINATE *)This;
    CTX *ctx = t->Context;
    for (;;) {
        COMP *comp = ctx->Compositions;
        while (comp && t->Only && comp != t->Only)
            comp = comp->Next;
        if (!comp)
            break;
        end_through_sink(ctx, comp, cookie);
        if (t->Only)
            break;
    }
    return S_OK;
}

static const ITfEditSessionVtbl terminate_vtbl = {term_query, term_add_ref, term_release, term_do};

HRESULT CompTerminateAll(CTX *ctx, ITfCompositionView *view)
{
    COMP *only = view ? comp_from_view(view) : nullptr;
    if (view && (!only || only->Context != ctx))
        return E_INVALIDARG;
    if (!ctx->Doc)
        return TF_E_DISCONNECTED;
    if (!ctx->Compositions)
        return S_OK;
    TERMINATE t = {{&terminate_vtbl}, ctx, only};
    HRESULT session = E_FAIL;
    /* the sinks end a composition inside a write session of the system's. */
    HRESULT hr = CtxRequestSession(ctx, 64, &t.Iface, TF_ES_SYNC | TF_ES_READWRITE, &session);
    if (FAILED(hr) || FAILED(session) || ctx->Compositions == nullptr) {
        /* the store could not grant a lock now, so the compositions end without asking their sinks. */
        if (only)
            finish(ctx, only);
        else
            CompAbortAll(ctx);
    }
    return S_OK;
}
