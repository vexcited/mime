/*
 * properties: values attached to spans of a context's text, such as a composition's display attributes. a property
 * is a list of non overlapping segments that follow the text. the edit record tells the context's sinks what an edit
 * session changed.
 */
#include "msctf_private.h"

LONG CtxMovePosition(LONG position, LONG start, LONG old_end, LONG new_end, TfGravity gravity);

static PROP_ENTRY *find_prop(CTX *ctx, REFGUID guid, BOOL create)
{
    for (ULONG i = 0; i < ctx->PropCount; i++)
        if (IsEqualGUID(&ctx->Props[i].Guid, guid))
            return &ctx->Props[i];
    if (!create)
        return nullptr;
    if (ctx->PropCount == ctx->PropCapacity) {
        ULONG capacity = ctx->PropCapacity ? ctx->PropCapacity * 2 : 4;
        PROP_ENTRY *items = CtfAlloc(capacity * sizeof(PROP_ENTRY));
        if (!items)
            return nullptr;
        if (ctx->Props)
            memcpy(items, ctx->Props, ctx->PropCount * sizeof(PROP_ENTRY));
        CtfFree(ctx->Props);
        ctx->Props = items;
        ctx->PropCapacity = capacity;
    }
    PROP_ENTRY *entry = &ctx->Props[ctx->PropCount++];
    memset(entry, 0, sizeof(*entry));
    entry->Guid = *guid;
    return entry;
}

static BOOL add_segment(PROP_ENTRY *entry, LONG start, LONG end, const VARIANT *value, ULONG at)
{
    if (entry->Count == entry->Capacity) {
        ULONG capacity = entry->Capacity ? entry->Capacity * 2 : 8;
        PROP_SEGMENT *items = CtfAlloc(capacity * sizeof(PROP_SEGMENT));
        if (!items)
            return FALSE;
        if (entry->Segments)
            memcpy(items, entry->Segments, entry->Count * sizeof(PROP_SEGMENT));
        CtfFree(entry->Segments);
        entry->Segments = items;
        entry->Capacity = capacity;
    }
    memmove(&entry->Segments[at + 1], &entry->Segments[at], (entry->Count - at) * sizeof(PROP_SEGMENT));
    PROP_SEGMENT *seg = &entry->Segments[at];
    seg->Start = start;
    seg->End = end;
    VariantInit(&seg->Value);
    entry->Count++;
    if (FAILED(VariantCopy(&seg->Value, value))) {
        memmove(seg, seg + 1, (entry->Count - at - 1) * sizeof(PROP_SEGMENT));
        entry->Count--;
        return FALSE;
    }
    return TRUE;
}

static void drop_segment(PROP_ENTRY *entry, ULONG at)
{
    VariantClear(&entry->Segments[at].Value);
    memmove(&entry->Segments[at], &entry->Segments[at + 1], (entry->Count - at - 1) * sizeof(PROP_SEGMENT));
    entry->Count--;
}

/* removes [start, end) from the segments, splitting the ones it cuts. */
static void cut(PROP_ENTRY *entry, LONG start, LONG end)
{
    for (ULONG i = 0; i < entry->Count;) {
        PROP_SEGMENT *seg = &entry->Segments[i];
        if (seg->End <= start || seg->Start >= end) {
            i++;
            continue;
        }
        if (seg->Start < start && seg->End > end) {
            /* a cut inside a segment splits off its tail. */
            VARIANT value;
            LONG tail_end = seg->End;
            VariantInit(&value);
            VariantCopy(&value, &seg->Value);
            seg->End = start;
            add_segment(entry, end, tail_end, &value, i + 1);
            VariantClear(&value);
            i += 2;
            continue;
        }
        if (seg->Start < start) {
            seg->End = start;
            i++;
        } else if (seg->End > end) {
            seg->Start = end;
            i++;
        } else {
            drop_segment(entry, i);
        }
    }
}

void PropsSet(CTX *ctx, REFGUID guid, LONG start, LONG end, const VARIANT *value)
{
    PROP_ENTRY *entry = find_prop(ctx, guid, TRUE);
    if (!entry || end < start)
        return;
    cut(entry, start, end);
    if (value && V_VT(value) != VT_EMPTY && end > start) {
        ULONG at = 0;
        while (at < entry->Count && entry->Segments[at].Start < start)
            at++;
        add_segment(entry, start, end, value, at);
    }
    if (ctx->Record)
        EditRecordProperty(ctx->Record, guid, start, end);
}

BOOL PropsGetInt(CTX *ctx, REFGUID guid, LONG position, LONG *value, LONG *start, LONG *end)
{
    PROP_ENTRY *entry = find_prop(ctx, guid, FALSE);
    if (!entry)
        return FALSE;
    for (ULONG i = 0; i < entry->Count; i++) {
        PROP_SEGMENT *seg = &entry->Segments[i];
        if (position >= seg->Start && position < seg->End && V_VT(&seg->Value) == VT_I4) {
            if (value)
                *value = V_I4(&seg->Value);
            if (start)
                *start = seg->Start;
            if (end)
                *end = seg->End;
            return TRUE;
        }
    }
    return FALSE;
}

void PropsAdjust(CTX *ctx, LONG start, LONG old_end, LONG new_end)
{
    for (ULONG p = 0; p < ctx->PropCount; p++) {
        PROP_ENTRY *entry = &ctx->Props[p];
        for (ULONG i = 0; i < entry->Count;) {
            PROP_SEGMENT *seg = &entry->Segments[i];
            LONG s = CtxMovePosition(seg->Start, start, old_end, new_end, TF_GRAVITY_FORWARD);
            LONG e = CtxMovePosition(seg->End, start, old_end, new_end, TF_GRAVITY_BACKWARD);
            if (e <= s) {
                drop_segment(entry, i);
                continue;
            }
            seg->Start = s;
            seg->End = e;
            i++;
        }
    }
}

void PropsFree(CTX *ctx)
{
    for (ULONG p = 0; p < ctx->PropCount; p++) {
        PROP_ENTRY *entry = &ctx->Props[p];
        for (ULONG i = 0; i < entry->Count; i++)
            VariantClear(&entry->Segments[i].Value);
        CtfFree(entry->Segments);
    }
    CtfFree(ctx->Props);
    ctx->Props = nullptr;
    ctx->PropCount = ctx->PropCapacity = 0;
}

/* ------------------------------------------------------------------ range enumerators */

HRESULT EnumRangesCreate(CTX *ctx, const LONG (*spans)[2], ULONG count, IEnumTfRanges **out)
{
    IUnknown **items = count ? CtfAlloc(count * sizeof(IUnknown *)) : nullptr;
    HRESULT hr = S_OK;
    ULONG made = 0;
    if (count && !items)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        ITfRange *range;
        hr = RngCreate(ctx, spans[i][0], spans[i][1], &range);
        if (FAILED(hr))
            break;
        items[made++] = (IUnknown *)range;
    }
    if (SUCCEEDED(hr))
        hr = EnumCreate(&IID_IEnumTfRanges, items, made, (void **)out);
    for (ULONG i = 0; i < made; i++)
        items[i]->lpVtbl->Release(items[i]);
    CtfFree(items);
    return hr;
}

/* ------------------------------------------------------------------ ITfProperty */

typedef struct PROPERTY {
    ITfProperty Iface;
    LONG Refs;
    CTX *Context;
    GUID Guid;
} PROPERTY;

static HRESULT STDMETHODCALLTYPE pr_query(ITfProperty *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfReadOnlyProperty) ||
        IsEqualIID(riid, &IID_ITfProperty)) {
        *object = This;
        InterlockedIncrement(&((PROPERTY *)This)->Refs);
        return S_OK;
    }
    CTF_TRACE("msctf: property query %08lx-%04x-%04x refused\n", riid->Data1, riid->Data2, riid->Data3);
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pr_add_ref(ITfProperty *This)
{
    return InterlockedIncrement(&((PROPERTY *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE pr_release(ITfProperty *This)
{
    PROPERTY *property = (PROPERTY *)This;
    LONG refs = InterlockedDecrement(&property->Refs);
    if (!refs) {
        CtxRelease(property->Context);
        CtfFree(property);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE pr_get_type(ITfProperty *This, GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    *guid = ((PROPERTY *)This)->Guid;
    return S_OK;
}

static HRESULT cookie_ok(PROPERTY *property, TfEditCookie cookie, BOOL write)
{
    return CtxCheckCookie(property->Context, cookie, write) ? S_OK : TF_E_NOLOCK;
}

static HRESULT STDMETHODCALLTYPE pr_enum_ranges(ITfProperty *This, TfEditCookie cookie, IEnumTfRanges **out,
                                                ITfRange *target)
{
    PROPERTY *property = (PROPERTY *)This;
    CTX *ctx = property->Context;
    HRESULT hr = cookie_ok(property, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    RNG *range = target ? RngFromInterface(target) : nullptr;
    if (target && (!range || range->Context != ctx))
        return E_INVALIDARG;
    PROP_ENTRY *entry = find_prop(ctx, &property->Guid, FALSE);
    LONG (*spans)[2] = entry && entry->Count ? CtfAlloc(entry->Count * sizeof(*spans)) : nullptr;
    ULONG n = 0;
    for (ULONG i = 0; entry && i < entry->Count; i++) {
        LONG s = entry->Segments[i].Start, e = entry->Segments[i].End;
        if (range) {
            if (e <= range->Start || s >= range->End)
                continue;
            if (s < range->Start)
                s = range->Start;
            if (e > range->End)
                e = range->End;
        }
        spans[n][0] = s;
        spans[n][1] = e;
        n++;
    }
    hr = EnumRangesCreate(ctx, (const LONG (*)[2])spans, n, out);
    CtfFree(spans);
    return hr;
}

static HRESULT STDMETHODCALLTYPE pr_get_value(ITfProperty *This, TfEditCookie cookie, ITfRange *range, VARIANT *value)
{
    PROPERTY *property = (PROPERTY *)This;
    HRESULT hr = cookie_ok(property, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    RNG *rng = RngFromInterface(range);
    if (!value || !rng || rng->Context != property->Context)
        return E_INVALIDARG;
    VariantInit(value);
    PROP_ENTRY *entry = find_prop(property->Context, &property->Guid, FALSE);
    PROP_SEGMENT *found = nullptr;
    BOOL touched = FALSE, mixed = FALSE;
    LONG covered_to = rng->Start;
    for (ULONG i = 0; entry && i < entry->Count; i++) {
        PROP_SEGMENT *seg = &entry->Segments[i];
        BOOL touches = rng->Start == rng->End ? seg->Start <= rng->Start && rng->Start < seg->End
                                              : seg->End > rng->Start && seg->Start < rng->End;
        if (!touches)
            continue;
        /* a single value over the whole range is the result, anything else is mixed. */
        if (touched || seg->Start > covered_to)
            mixed = TRUE;
        else
            found = seg;
        touched = TRUE;
        covered_to = seg->End;
    }
    if (!touched)
        return S_FALSE;
    if (rng->Start != rng->End && covered_to < rng->End)
        mixed = TRUE;
    /* a gap or a different value leaves the range with no value. */
    if (mixed || !found)
        return S_FALSE;
    return VariantCopy(value, &found->Value);
}

static HRESULT STDMETHODCALLTYPE pr_get_context(ITfProperty *This, ITfContext **context)
{
    PROPERTY *property = (PROPERTY *)This;
    if (!context)
        return E_INVALIDARG;
    *context = CtxInterface(property->Context);
    (*context)->lpVtbl->AddRef(*context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_find_range(ITfProperty *This, TfEditCookie cookie, ITfRange *range,
                                               ITfRange **out, TfAnchor anchor)
{
    PROPERTY *property = (PROPERTY *)This;
    HRESULT hr = cookie_ok(property, cookie, FALSE);
    if (FAILED(hr))
        return hr;
    RNG *rng = RngFromInterface(range);
    if (!out || !rng || rng->Context != property->Context)
        return E_INVALIDARG;
    *out = nullptr;
    PROP_ENTRY *entry = find_prop(property->Context, &property->Guid, FALSE);
    LONG position = anchor == TF_ANCHOR_START ? rng->Start : rng->End;
    for (ULONG i = 0; entry && i < entry->Count; i++) {
        PROP_SEGMENT *seg = &entry->Segments[i];
        if (position >= seg->Start && position < seg->End)
            return RngCreate(property->Context, seg->Start, seg->End, out);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_set_value_store(ITfProperty *This, TfEditCookie cookie, ITfRange *range,
                                                    ITfPropertyStore *store)
{
    HRESULT hr = cookie_ok((PROPERTY *)This, cookie, TRUE);
    return FAILED(hr) ? hr : E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE pr_set_value(ITfProperty *This, TfEditCookie cookie, ITfRange *range,
                                              const VARIANT *value)
{
    PROPERTY *property = (PROPERTY *)This;
    HRESULT hr = cookie_ok(property, cookie, TRUE);
    if (FAILED(hr))
        return hr;
    RNG *rng = RngFromInterface(range);
    if (!value || !rng || rng->Context != property->Context)
        return E_INVALIDARG;
    /* values are empty, numbers, strings or objects, set over a non empty range. */
    if ((V_VT(value) != VT_EMPTY && V_VT(value) != VT_I4 && V_VT(value) != VT_BSTR && V_VT(value) != VT_UNKNOWN) ||
        rng->Start == rng->End)
        return E_INVALIDARG;
    PropsSet(property->Context, &property->Guid, rng->Start, rng->End, value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pr_clear(ITfProperty *This, TfEditCookie cookie, ITfRange *range)
{
    PROPERTY *property = (PROPERTY *)This;
    HRESULT hr = cookie_ok(property, cookie, TRUE);
    if (FAILED(hr))
        return hr;
    RNG *rng = RngFromInterface(range);
    if (!rng || rng->Context != property->Context)
        return E_INVALIDARG;
    PropsSet(property->Context, &property->Guid, rng->Start, rng->End, nullptr);
    return S_OK;
}

static const ITfPropertyVtbl property_vtbl = {pr_query, pr_add_ref, pr_release, pr_get_type, pr_enum_ranges,
                                              pr_get_value, pr_get_context, pr_find_range, pr_set_value_store,
                                              pr_set_value, pr_clear};

HRESULT PropsGetProperty(CTX *ctx, REFGUID guid, ITfProperty **out)
{
    if (!guid || !out)
        return E_INVALIDARG;
    *out = nullptr;
    PROPERTY *property = CtfAlloc(sizeof(*property));
    if (!property)
        return E_OUTOFMEMORY;
    property->Iface.lpVtbl = &property_vtbl;
    property->Refs = 1;
    property->Context = ctx;
    CtxAddRef(ctx);
    property->Guid = *guid;
    CtfObjectCreated();
    *out = &property->Iface;
    return S_OK;
}

/* ------------------------------------------------------------------ tracked properties */

/*
 * ITfContext::TrackProperties: one read only property standing for several. its ranges are the runs where each tracked
 * property holds one value, and its value over a range enumerates their values. programs use it to read a
 * composition's display attribute and composing mark at once.
 */
typedef struct TRACKED {
    ITfReadOnlyProperty Iface;
    LONG Refs;
    CTX *Context;
    GUID Guids[8];
    ULONG Count;
} TRACKED;

static HRESULT STDMETHODCALLTYPE tr_query(ITfReadOnlyProperty *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfReadOnlyProperty)) {
        *object = This;
        InterlockedIncrement(&((TRACKED *)This)->Refs);
        return S_OK;
    }
    CTF_TRACE("msctf: tracked property query %08lx-%04x-%04x refused\n", riid->Data1, riid->Data2, riid->Data3);
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tr_add_ref(ITfReadOnlyProperty *This)
{
    return InterlockedIncrement(&((TRACKED *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE tr_release(ITfReadOnlyProperty *This)
{
    TRACKED *tracked = (TRACKED *)This;
    LONG refs = InterlockedDecrement(&tracked->Refs);
    if (!refs) {
        CtxRelease(tracked->Context);
        CtfFree(tracked);
        CtfObjectDestroyed();
    }
    return refs;
}

/* the union of properties has no type of its own. */
static HRESULT STDMETHODCALLTYPE tr_get_type(ITfReadOnlyProperty *This, GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    memset(guid, 0, sizeof(*guid));
    return S_OK;
}

/* the positions in [from, to) where any tracked property starts or stops having a value. */
static ULONG breakpoints(TRACKED *tracked, LONG from, LONG to, LONG *out, ULONG max)
{
    ULONG n = 0;
    out[n++] = from;
    out[n++] = to;
    for (ULONG g = 0; g < tracked->Count; g++) {
        PROP_ENTRY *entry = find_prop(tracked->Context, &tracked->Guids[g], FALSE);
        for (ULONG i = 0; entry && i < entry->Count; i++) {
            LONG edges[2] = {entry->Segments[i].Start, entry->Segments[i].End};
            for (int k = 0; k < 2; k++) {
                if (edges[k] > from && edges[k] < to && n < max)
                    out[n++] = edges[k];
            }
        }
    }
    /* sorted, without repeats. */
    for (ULONG i = 1; i < n; i++) {
        LONG v = out[i];
        ULONG j = i;
        while (j > 0 && out[j - 1] > v) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    ULONG unique = 0;
    for (ULONG i = 0; i < n; i++)
        if (!unique || out[i] != out[unique - 1])
            out[unique++] = out[i];
    return unique;
}

static BOOL has_value(TRACKED *tracked, LONG position)
{
    for (ULONG g = 0; g < tracked->Count; g++) {
        PROP_ENTRY *entry = find_prop(tracked->Context, &tracked->Guids[g], FALSE);
        for (ULONG i = 0; entry && i < entry->Count; i++)
            if (position >= entry->Segments[i].Start && position < entry->Segments[i].End)
                return TRUE;
    }
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE tr_enum_ranges(ITfReadOnlyProperty *This, TfEditCookie cookie, IEnumTfRanges **out,
                                                ITfRange *target)
{
    TRACKED *tracked = (TRACKED *)This;
    CTX *ctx = tracked->Context;
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    RNG *range = target ? RngFromInterface(target) : nullptr;
    if (target && (!range || range->Context != ctx))
        return E_INVALIDARG;
    LONG end = 0;
    if (!range && FAILED(CtxStoreGetEnd(ctx, &end)))
        end = 0;
    LONG from = range ? range->Start : 0, to = range ? range->End : end;
    LONG points[64];
    ULONG count = breakpoints(tracked, from, to, points, 64);
    LONG (*spans)[2] = CtfAlloc(64 * sizeof(*spans));
    ULONG n = 0;
    if (!spans)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i + 1 < count; i++) {
        if (!has_value(tracked, points[i]))
            continue;
        spans[n][0] = points[i];
        spans[n][1] = points[i + 1];
        n++;
    }
    HRESULT hr = EnumRangesCreate(ctx, (const LONG (*)[2])spans, n, out);
    CtfFree(spans);
    return hr;
}

/* enumerates the tracked properties' values over a range. */
typedef struct VALUES {
    IEnumTfPropertyValue Iface;
    LONG Refs;
    TF_PROPERTYVAL *Items;
    ULONG Count, Position;
} VALUES;

static HRESULT STDMETHODCALLTYPE vl_query(IEnumTfPropertyValue *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfPropertyValue)) {
        *object = This;
        InterlockedIncrement(&((VALUES *)This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE vl_add_ref(IEnumTfPropertyValue *This)
{
    return InterlockedIncrement(&((VALUES *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE vl_release(IEnumTfPropertyValue *This)
{
    VALUES *values = (VALUES *)This;
    LONG refs = InterlockedDecrement(&values->Refs);
    if (!refs) {
        for (ULONG i = 0; i < values->Count; i++)
            VariantClear(&values->Items[i].varValue);
        CtfFree(values->Items);
        CtfFree(values);
        CtfObjectDestroyed();
    }
    return refs;
}

static const IEnumTfPropertyValueVtbl values_vtbl;

static HRESULT values_new(const TF_PROPERTYVAL *items, ULONG count, ULONG position, IEnumTfPropertyValue **out)
{
    VALUES *values = CtfAlloc(sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;
    values->Items = count ? CtfAlloc(count * sizeof(TF_PROPERTYVAL)) : nullptr;
    if (count && !values->Items) {
        CtfFree(values);
        return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < count; i++) {
        values->Items[i].guidId = items[i].guidId;
        VariantInit(&values->Items[i].varValue);
        VariantCopy(&values->Items[i].varValue, &items[i].varValue);
    }
    values->Iface.lpVtbl = &values_vtbl;
    values->Refs = 1;
    values->Count = count;
    values->Position = position;
    CtfObjectCreated();
    *out = &values->Iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vl_clone(IEnumTfPropertyValue *This, IEnumTfPropertyValue **out)
{
    VALUES *values = (VALUES *)This;
    if (!out)
        return E_INVALIDARG;
    return values_new(values->Items, values->Count, values->Position, out);
}

static HRESULT STDMETHODCALLTYPE vl_next(IEnumTfPropertyValue *This, ULONG count, TF_PROPERTYVAL *items, ULONG *fetched)
{
    VALUES *values = (VALUES *)This;
    ULONG n = 0;
    if (!items || (!fetched && count != 1))
        return E_INVALIDARG;
    while (n < count && values->Position < values->Count) {
        items[n].guidId = values->Items[values->Position].guidId;
        VariantInit(&items[n].varValue);
        VariantCopy(&items[n].varValue, &values->Items[values->Position].varValue);
        values->Position++;
        n++;
    }
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE vl_reset(IEnumTfPropertyValue *This)
{
    ((VALUES *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vl_skip(IEnumTfPropertyValue *This, ULONG count)
{
    VALUES *values = (VALUES *)This;
    if (count > values->Count - values->Position) {
        values->Position = values->Count;
        return S_FALSE;
    }
    values->Position += count;
    return S_OK;
}

static const IEnumTfPropertyValueVtbl values_vtbl = {vl_query, vl_add_ref, vl_release, vl_clone, vl_next, vl_reset,
                                                     vl_skip};

static HRESULT STDMETHODCALLTYPE tr_get_value(ITfReadOnlyProperty *This, TfEditCookie cookie, ITfRange *range,
                                              VARIANT *value)
{
    TRACKED *tracked = (TRACKED *)This;
    CTX *ctx = tracked->Context;
    if (!CtxCheckCookie(ctx, cookie, FALSE))
        return TF_E_NOLOCK;
    RNG *rng = RngFromInterface(range);
    if (!value || !rng || rng->Context != ctx)
        return E_INVALIDARG;
    VariantInit(value);
    TF_PROPERTYVAL found[8];
    ULONG n = 0;
    for (ULONG g = 0; g < tracked->Count; g++) {
        PROP_ENTRY *entry = find_prop(ctx, &tracked->Guids[g], FALSE);
        for (ULONG i = 0; entry && i < entry->Count; i++) {
            PROP_SEGMENT *seg = &entry->Segments[i];
            BOOL covers = rng->Start == rng->End ? seg->Start <= rng->Start && rng->Start < seg->End
                                                  : seg->Start <= rng->Start && seg->End >= rng->End;
            if (!covers)
                continue;
            found[n].guidId = tracked->Guids[g];
            VariantInit(&found[n].varValue);
            VariantCopy(&found[n].varValue, &seg->Value);
            n++;
            break;
        }
    }
    IEnumTfPropertyValue *values = nullptr;
    HRESULT hr = values_new(found, n, 0, &values);
    for (ULONG i = 0; i < n; i++)
        VariantClear(&found[i].varValue);
    if (FAILED(hr))
        return hr;
    V_VT(value) = VT_UNKNOWN;
    V_UNKNOWN(value) = (IUnknown *)values;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tr_get_context(ITfReadOnlyProperty *This, ITfContext **context)
{
    if (!context)
        return E_INVALIDARG;
    *context = CtxInterface(((TRACKED *)This)->Context);
    (*context)->lpVtbl->AddRef(*context);
    return S_OK;
}

static const ITfReadOnlyPropertyVtbl tracked_vtbl = {tr_query, tr_add_ref, tr_release, tr_get_type, tr_enum_ranges,
                                                     tr_get_value, tr_get_context};

HRESULT PropsTrack(CTX *ctx, const GUID **guids, ULONG count, ITfReadOnlyProperty **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (count > 8 || (count && !guids))
        return E_INVALIDARG;
    TRACKED *tracked = CtfAlloc(sizeof(*tracked));
    if (!tracked)
        return E_OUTOFMEMORY;
    tracked->Iface.lpVtbl = &tracked_vtbl;
    tracked->Refs = 1;
    tracked->Context = ctx;
    CtxAddRef(ctx);
    for (ULONG i = 0; i < count; i++)
        tracked->Guids[i] = *guids[i];
    tracked->Count = count;
    CtfObjectCreated();
    *out = &tracked->Iface;
    return S_OK;
}

/* ------------------------------------------------------------------ the edit record */

typedef struct SPAN_LIST {
    LONG (*Spans)[2];
    ULONG Count, Capacity;
} SPAN_LIST;

typedef struct RECORD_PROP {
    GUID Guid;
    SPAN_LIST Spans;
} RECORD_PROP;

typedef struct RECORD {
    ITfEditRecord Iface;
    LONG Refs;
    CTX *Context;
    SPAN_LIST Text;
    RECORD_PROP *Props;
    ULONG PropCount;
    BOOL Selection;
} RECORD;

static void span_add(SPAN_LIST *list, LONG start, LONG end)
{
    if (end < start)
        return;
    /* merges the span with the ones it touches. */
    for (ULONG i = 0; i < list->Count; i++) {
        if (start <= list->Spans[i][1] && end >= list->Spans[i][0]) {
            if (start < list->Spans[i][0])
                list->Spans[i][0] = start;
            if (end > list->Spans[i][1])
                list->Spans[i][1] = end;
            return;
        }
    }
    if (list->Count == list->Capacity) {
        ULONG capacity = list->Capacity ? list->Capacity * 2 : 4;
        LONG (*items)[2] = CtfAlloc(capacity * sizeof(*items));
        if (!items)
            return;
        if (list->Spans)
            memcpy(items, list->Spans, list->Count * sizeof(*items));
        CtfFree(list->Spans);
        list->Spans = items;
        list->Capacity = capacity;
    }
    list->Spans[list->Count][0] = start;
    list->Spans[list->Count][1] = end;
    list->Count++;
}

static HRESULT STDMETHODCALLTYPE rec_query(ITfEditRecord *This, REFIID riid, void **object)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditRecord)) {
        *object = This;
        InterlockedIncrement(&((RECORD *)This)->Refs);
        return S_OK;
    }
    CTF_TRACE("msctf: edit record query %08lx-%04x-%04x refused\n", riid->Data1, riid->Data2, riid->Data3);
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE rec_add_ref(ITfEditRecord *This)
{
    return InterlockedIncrement(&((RECORD *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE rec_release(ITfEditRecord *This)
{
    RECORD *record = (RECORD *)This;
    LONG refs = InterlockedDecrement(&record->Refs);
    if (!refs) {
        CtfFree(record->Text.Spans);
        for (ULONG i = 0; i < record->PropCount; i++)
            CtfFree(record->Props[i].Spans.Spans);
        CtfFree(record->Props);
        CtxRelease(record->Context);
        CtfFree(record);
        CtfObjectDestroyed();
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE rec_selection(ITfEditRecord *This, BOOL *changed)
{
    if (!changed)
        return E_INVALIDARG;
    *changed = ((RECORD *)This)->Selection;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rec_updates(ITfEditRecord *This, DWORD flags, const GUID **props, ULONG count,
                                             IEnumTfRanges **out)
{
    RECORD *record = (RECORD *)This;
    SPAN_LIST all = {};
    if (!out || (count && !props))
        return E_INVALIDARG;
    if (flags & TF_GTP_INCL_TEXT)
        for (ULONG i = 0; i < record->Text.Count; i++)
            span_add(&all, record->Text.Spans[i][0], record->Text.Spans[i][1]);
    for (ULONG i = 0; i < count; i++)
        for (ULONG p = 0; p < record->PropCount; p++)
            if (IsEqualGUID(&record->Props[p].Guid, props[i]))
                for (ULONG s = 0; s < record->Props[p].Spans.Count; s++)
                    span_add(&all, record->Props[p].Spans.Spans[s][0], record->Props[p].Spans.Spans[s][1]);
    HRESULT hr = EnumRangesCreate(record->Context, (const LONG (*)[2])all.Spans, all.Count, out);
    CtfFree(all.Spans);
    return hr;
}

static const ITfEditRecordVtbl record_vtbl = {rec_query, rec_add_ref, rec_release, rec_selection, rec_updates};

HRESULT EditRecordCreate(CTX *ctx, ITfEditRecord **out)
{
    RECORD *record = CtfAlloc(sizeof(*record));
    if (!record)
        return E_OUTOFMEMORY;
    record->Iface.lpVtbl = &record_vtbl;
    record->Refs = 1;
    record->Context = ctx;
    CtxAddRef(ctx);
    CtfObjectCreated();
    *out = &record->Iface;
    return S_OK;
}

void EditRecordText(ITfEditRecord *iface, LONG start, LONG end)
{
    RECORD *record = (RECORD *)iface;
    span_add(&record->Text, start, end);
}

void EditRecordProperty(ITfEditRecord *iface, REFGUID guid, LONG start, LONG end)
{
    RECORD *record = (RECORD *)iface;
    for (ULONG i = 0; i < record->PropCount; i++) {
        if (IsEqualGUID(&record->Props[i].Guid, guid)) {
            span_add(&record->Props[i].Spans, start, end);
            return;
        }
    }
    RECORD_PROP *items = CtfAlloc((record->PropCount + 1) * sizeof(RECORD_PROP));
    if (!items)
        return;
    if (record->Props)
        memcpy(items, record->Props, record->PropCount * sizeof(RECORD_PROP));
    CtfFree(record->Props);
    record->Props = items;
    record->Props[record->PropCount].Guid = *guid;
    span_add(&record->Props[record->PropCount].Spans, start, end);
    record->PropCount++;
}

void EditRecordSelection(ITfEditRecord *iface)
{
    ((RECORD *)iface)->Selection = TRUE;
}

BOOL EditRecordChanged(ITfEditRecord *iface)
{
    RECORD *record = (RECORD *)iface;
    return record->Selection || record->Text.Count || record->PropCount;
}
