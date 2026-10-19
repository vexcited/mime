/*
 * ranges: a stretch of a context's text between two positions that follow the text as it changes. a range works
 * only inside an edit session, with the cookie the session was given.
 */
#include "msctf_private.h"

static HRESULT STDMETHODCALLTYPE rng_query(ITfRangeACP *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfRange) || IsEqualIID(riid, &IID_ITfRangeACP)) {
        *object = This;
        InterlockedIncrement(&((RNG *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE rng_add_ref(ITfRangeACP *This)
{
    return InterlockedIncrement(&((RNG *)This)->Refs);
}

static void rng_unlink(RNG *rng)
{
    CTX *ctx = rng->Context;
    if (!ctx)
        return;
    if (rng->Prev)
        rng->Prev->Next = rng->Next;
    else
        ctx->Ranges = rng->Next;
    if (rng->Next)
        rng->Next->Prev = rng->Prev;
    rng->Next = rng->Prev = nullptr;
    rng->Context = nullptr;
    if (!rng->Weak)
        CtxRelease(ctx);
}

static ULONG STDMETHODCALLTYPE rng_release(ITfRangeACP *This)
{
    RNG *rng = (RNG *)This;
    LONG refs = InterlockedDecrement(&rng->Refs);
    if (!refs) {
        rng_unlink(rng);
        CtfFree(rng);
        CtfObjectDestroyed();
    }
    return refs;
}

RNG *RngFromInterface(ITfRange *iface)
{
    /* every range is one of ours: the vtable says so. */
    extern const ITfRangeACPVtbl g_range_vtbl;
    return iface && (const void *)iface->lpVtbl == (const void *)&g_range_vtbl ? (RNG *)iface : nullptr;
}

/* the range must be one of the context, and the cookie one it gave. */
static HRESULT check(RNG *rng, TfEditCookie cookie, BOOL write)
{
    if (!rng->Context)
        return TF_E_DISCONNECTED;
    return CtxCheckCookie(rng->Context, cookie, write) ? S_OK : TF_E_NOLOCK;
}

/* a range of the same context as this one, or why not. */
static HRESULT other(RNG *rng, ITfRange *iface, RNG **out)
{
    *out = RngFromInterface(iface);
    if (!*out)
        return E_INVALIDARG;
    if ((*out)->Context != rng->Context)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT RngCreateWeak(CTX *ctx, LONG start, LONG end, RNG **out)
{
    extern const ITfRangeACPVtbl g_range_vtbl;
    RNG *rng = CtfAlloc(sizeof(*rng));
    if (!rng)
        return E_OUTOFMEMORY;
    rng->Iface.lpVtbl = &g_range_vtbl;
    rng->Refs = 1;
    rng->Weak = TRUE;
    rng->Context = ctx;
    rng->Start = start;
    rng->End = end;
    /* text inserted at either end of a range becomes part of it. */
    rng->GravityStart = TF_GRAVITY_BACKWARD;
    rng->GravityEnd = TF_GRAVITY_FORWARD;
    rng->Next = ctx->Ranges;
    if (ctx->Ranges)
        ctx->Ranges->Prev = rng;
    ctx->Ranges = rng;
    CtfObjectCreated();
    *out = rng;
    return S_OK;
}

HRESULT RngCreate(CTX *ctx, LONG start, LONG end, ITfRange **range)
{
    RNG *rng;
    HRESULT hr = RngCreateWeak(ctx, start, end, &rng);
    if (FAILED(hr))
        return hr;
    rng->Weak = FALSE;
    CtxAddRef(ctx);
    *range = (ITfRange *)&rng->Iface;
    return S_OK;
}

/* ------------------------------------------------------------------ following the text */

/* where a position goes when [start, old_end) becomes [start, new_end). */
LONG CtxMovePosition(LONG position, LONG start, LONG old_end, LONG new_end, TfGravity gravity)
{
    if (position < start)
        return position;
    if (position > old_end)
        return position + (new_end - old_end);
    /* inside the replaced text or at its edges: the gravity says which side the position stays on. */
    if (position == start && position == old_end)
        return gravity == TF_GRAVITY_FORWARD ? new_end : start;
    if (position == start)
        return start;
    if (position == old_end)
        return new_end;
    return gravity == TF_GRAVITY_FORWARD ? new_end : start;
}

void RngAdjust(CTX *ctx, LONG start, LONG old_end, LONG new_end)
{
    for (RNG *rng = ctx->Ranges; rng; rng = rng->Next) {
        LONG s = CtxMovePosition(rng->Start, start, old_end, new_end, rng->GravityStart);
        LONG e = CtxMovePosition(rng->End, start, old_end, new_end, rng->GravityEnd);
        if (e < s)
            e = s;
        rng->Start = s;
        rng->End = e;
    }
}

void RngDetachAll(CTX *ctx)
{
    while (ctx->Ranges) {
        RNG *rng = ctx->Ranges;
        /* the range stays alive for whoever holds it, now belonging to no context. */
        ctx->Ranges = rng->Next;
        if (rng->Next)
            rng->Next->Prev = nullptr;
        rng->Next = rng->Prev = nullptr;
        rng->Context = nullptr;
    }
}

/* ------------------------------------------------------------------ ITfRange */

static HRESULT STDMETHODCALLTYPE rng_get_text(ITfRangeACP *This, TfEditCookie cookie, DWORD flags, WCHAR *text,
                                              ULONG max, ULONG *got)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if (!got || (max && !text))
        return E_INVALIDARG;
    *got = 0;
    if (!max)
        return S_OK;
    LONG end = rng->End;
    if ((ULONG)(end - rng->Start) > max)
        end = rng->Start + (LONG)max;
    ULONG count = 0;
    if (end > rng->Start) {
        hr = CtxStoreGetText(rng->Context, rng->Start, end, text, max, &count);
        if (FAILED(hr))
            return hr;
    }
    if (count < max)
        text[count] = 0;
    *got = count;
    if (flags & TF_TF_MOVESTART)
        rng->Start += (LONG)count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_set_text(ITfRangeACP *This, TfEditCookie cookie, DWORD flags, const WCHAR *text,
                                              LONG cch)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, TRUE);
    if (FAILED(hr))
        return hr;
    if (!text && cch)
        return E_INVALIDARG;
    if (cch < 0) {
        cch = 0;
        while (text[cch])
            cch++;
    }
    CTX *ctx = rng->Context;
    LONG start = rng->Start;
    TS_TEXTCHANGE change = {};
    hr = CtxStoreSetText(ctx, flags & TF_ST_CORRECTION ? TS_ST_CORRECTION : 0, rng->Start, rng->End, text, cch, &change);
    if (FAILED(hr))
        return hr;
    CtxAddRef(ctx);
    CtxTextChanged(ctx, change.acpStart, change.acpOldEnd, change.acpNewEnd, TRUE);
    /* the range covers the text it was given. */
    if (rng->Context) {
        rng->Start = start;
        rng->End = start + cch;
    }
    CtxRelease(ctx);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_get_formatted(ITfRangeACP *This, TfEditCookie cookie, IDataObject **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE rng_get_embedded(ITfRangeACP *This, TfEditCookie cookie, REFGUID service, REFIID riid,
                                                  IUnknown **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    return TS_E_NOOBJECT;
}

static HRESULT STDMETHODCALLTYPE rng_insert_embedded(ITfRangeACP *This, TfEditCookie cookie, DWORD flags,
                                                     IDataObject *object)
{
    return TS_E_FORMAT;
}

/* shifting a start or an end: a range never turns inside out, the other end goes along. */
static HRESULT shift(RNG *rng, TfEditCookie cookie, LONG request, LONG *shifted, BOOL start_side)
{
    HRESULT hr = check(rng, cookie, FALSE);
    LONG end_of_text = 0;
    if (FAILED(hr))
        return hr;
    if (!shifted)
        return E_INVALIDARG;
    hr = CtxStoreGetEnd(rng->Context, &end_of_text);
    if (FAILED(hr))
        return hr;
    LONG position = start_side ? rng->Start : rng->End;
    LONG target = position + request;
    if (target < 0)
        target = 0;
    if (target > end_of_text)
        target = end_of_text;
    *shifted = target - position;
    if (start_side) {
        rng->Start = target;
        if (rng->End < target)
            rng->End = target;
    } else {
        rng->End = target;
        if (rng->Start > target)
            rng->Start = target;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_shift_start(ITfRangeACP *This, TfEditCookie cookie, LONG request, LONG *shifted,
                                                 const TF_HALTCOND *halt)
{
    return shift((RNG *)This, cookie, request, shifted, TRUE);
}

static HRESULT STDMETHODCALLTYPE rng_shift_end(ITfRangeACP *This, TfEditCookie cookie, LONG request, LONG *shifted,
                                               const TF_HALTCOND *halt)
{
    return shift((RNG *)This, cookie, request, shifted, FALSE);
}

static LONG anchor_position(RNG *rng, TfAnchor anchor)
{
    return anchor == TF_ANCHOR_START ? rng->Start : rng->End;
}

static HRESULT STDMETHODCALLTYPE rng_shift_start_to_range(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                          TfAnchor anchor)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr))
        return hr;
    rng->Start = anchor_position(with, anchor);
    if (rng->End < rng->Start)
        rng->End = rng->Start;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_shift_end_to_range(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                        TfAnchor anchor)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr))
        return hr;
    rng->End = anchor_position(with, anchor);
    if (rng->Start > rng->End)
        rng->Start = rng->End;
    return S_OK;
}

/* the text has no regions, so there is none to move into. */
static HRESULT STDMETHODCALLTYPE rng_shift_start_region(ITfRangeACP *This, TfEditCookie cookie, TfShiftDir dir,
                                                        BOOL *no_region)
{
    HRESULT hr = check((RNG *)This, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if (!no_region)
        return E_INVALIDARG;
    *no_region = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_shift_end_region(ITfRangeACP *This, TfEditCookie cookie, TfShiftDir dir,
                                                      BOOL *no_region)
{
    return rng_shift_start_region(This, cookie, dir, no_region);
}

static HRESULT STDMETHODCALLTYPE rng_is_empty(ITfRangeACP *This, TfEditCookie cookie, BOOL *empty)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if (!empty)
        return E_INVALIDARG;
    *empty = rng->Start == rng->End;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_collapse(ITfRangeACP *This, TfEditCookie cookie, TfAnchor anchor)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if (anchor == TF_ANCHOR_START)
        rng->End = rng->Start;
    else
        rng->Start = rng->End;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_is_equal_start(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                    TfAnchor anchor, BOOL *equal)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr) || !equal)
        return FAILED(hr) ? hr : E_INVALIDARG;
    *equal = rng->Start == anchor_position(with, anchor);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_is_equal_end(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                  TfAnchor anchor, BOOL *equal)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr) || !equal)
        return FAILED(hr) ? hr : E_INVALIDARG;
    *equal = rng->End == anchor_position(with, anchor);
    return S_OK;
}

static LONG compare(LONG a, LONG b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

static HRESULT STDMETHODCALLTYPE rng_compare_start(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                   TfAnchor anchor, LONG *result)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr) || !result)
        return FAILED(hr) ? hr : E_INVALIDARG;
    *result = compare(rng->Start, anchor_position(with, anchor));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_compare_end(ITfRangeACP *This, TfEditCookie cookie, ITfRange *range,
                                                 TfAnchor anchor, LONG *result)
{
    RNG *rng = (RNG *)This, *with;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    hr = other(rng, range, &with);
    if (FAILED(hr) || !result)
        return FAILED(hr) ? hr : E_INVALIDARG;
    *result = compare(rng->End, anchor_position(with, anchor));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_adjust_for_insert(ITfRangeACP *This, TfEditCookie cookie, ULONG cch, BOOL *ok)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, TRUE);
    if (FAILED(hr))
        return hr;
    if (!ok)
        return E_INVALIDARG;
    /* a plain text store takes text anywhere. */
    *ok = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_get_gravity(ITfRangeACP *This, TfGravity *start, TfGravity *end)
{
    RNG *rng = (RNG *)This;
    if (!start || !end)
        return E_INVALIDARG;
    *start = rng->GravityStart;
    *end = rng->GravityEnd;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_set_gravity(ITfRangeACP *This, TfEditCookie cookie, TfGravity start, TfGravity end)
{
    RNG *rng = (RNG *)This;
    HRESULT hr = check(rng, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if ((start != TF_GRAVITY_BACKWARD && start != TF_GRAVITY_FORWARD) ||
        (end != TF_GRAVITY_BACKWARD && end != TF_GRAVITY_FORWARD))
        return E_INVALIDARG;
    rng->GravityStart = start;
    rng->GravityEnd = end;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_clone(ITfRangeACP *This, ITfRange **clone)
{
    RNG *rng = (RNG *)This;
    if (!clone)
        return E_INVALIDARG;
    *clone = nullptr;
    if (!rng->Context)
        return TF_E_DISCONNECTED;
    HRESULT hr = RngCreate(rng->Context, rng->Start, rng->End, clone);
    if (SUCCEEDED(hr)) {
        RNG *copy = (RNG *)*clone;
        copy->GravityStart = rng->GravityStart;
        copy->GravityEnd = rng->GravityEnd;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE rng_get_context(ITfRangeACP *This, ITfContext **context)
{
    RNG *rng = (RNG *)This;
    if (!context)
        return E_INVALIDARG;
    *context = nullptr;
    if (!rng->Context)
        return TF_E_DISCONNECTED;
    *context = CtxInterface(rng->Context);
    (*context)->lpVtbl->AddRef(*context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_get_extent(ITfRangeACP *This, LONG *anchor, LONG *cch)
{
    RNG *rng = (RNG *)This;
    if (!anchor || !cch)
        return E_INVALIDARG;
    *anchor = rng->Start;
    *cch = rng->End - rng->Start;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rng_set_extent(ITfRangeACP *This, LONG anchor, LONG cch)
{
    RNG *rng = (RNG *)This;
    if (anchor < 0 || cch < 0)
        return E_INVALIDARG;
    rng->Start = anchor;
    rng->End = anchor + cch;
    return S_OK;
}

const ITfRangeACPVtbl g_range_vtbl = {
    rng_query, rng_add_ref, rng_release, rng_get_text, rng_set_text, rng_get_formatted, rng_get_embedded,
    rng_insert_embedded, rng_shift_start, rng_shift_end, rng_shift_start_to_range, rng_shift_end_to_range,
    rng_shift_start_region, rng_shift_end_region, rng_is_empty, rng_collapse, rng_is_equal_start, rng_is_equal_end,
    rng_compare_start, rng_compare_end, rng_adjust_for_insert, rng_get_gravity, rng_set_gravity, rng_clone,
    rng_get_context, rng_get_extent, rng_set_extent};
