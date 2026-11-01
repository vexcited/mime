/*
 * loops: For Each over a collection or an array, and a For with a counter that is a Variant, and the checks the compiler
 * leaves to the library for the other counters.
 */
#include "vb.h"

static const IID IID_EnumVariant = {0x00020404, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

typedef struct EnumVariant EnumVariant;
struct EnumVariant {
    const struct {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(EnumVariant *, REFIID, void **);
        ULONG(STDMETHODCALLTYPE *AddRef)(EnumVariant *);
        ULONG(STDMETHODCALLTYPE *Release)(EnumVariant *);
        HRESULT(STDMETHODCALLTYPE *Next)(EnumVariant *, ULONG, VARIANT *, ULONG *);
        HRESULT(STDMETHODCALLTYPE *Skip)(EnumVariant *, ULONG);
        HRESULT(STDMETHODCALLTYPE *Reset)(EnumVariant *);
        HRESULT(STDMETHODCALLTYPE *Clone)(EnumVariant *, EnumVariant **);
    } *lpVtbl;
};

/* ------------------------------------------------------------------ collections */

/* the enumerator of a collection is what its member of number -4 gives. */
static EnumVariant *enumerator_of(IDispatch *collection)
{
    DISPPARAMS params = {};
    VARIANT result;
    EXCEPINFO info = {};
    UINT bad = 0;
    VariantInit(&result);
    HRESULT hr = collection->lpVtbl->Invoke(collection, DISPID_NEWENUM, &IID_NULL, VbLocale(), DISPATCH_METHOD | DISPATCH_PROPERTYGET, &params, &result, &info, &bad);
    EnumVariant *e = nullptr;
    if (SUCCEEDED(hr) && (V_VT(&result) == VT_UNKNOWN || V_VT(&result) == VT_DISPATCH) && V_UNKNOWN(&result)) {
        hr = V_UNKNOWN(&result)->lpVtbl->QueryInterface(V_UNKNOWN(&result), &IID_EnumVariant, (void **)&e);
        if (FAILED(hr))
            e = nullptr;
    }
    VariantClear(&result);
    if (!e)
        VbRaise(451);
    return e;
}

VB_STD LONG vb___vbaNextEachCollVar(EnumVariant **slot, VARIANT *item)
{
    if (V_VT(item) & VT_BYREF)
        VbRaise(VBERR_TYPE_MISMATCH);
    VariantClear(item);
    EnumVariant *e = *slot;
    if (!e)
        VbRaise(92);
    ULONG got = 0;
    HRESULT hr = e->lpVtbl->Next(e, 1, item, &got);
    if (hr == S_OK && got == 1)
        return -1;
    *slot = nullptr;
    e->lpVtbl->Release(e);
    if (hr == S_FALSE || hr == S_OK)
        return 0;
    VbRaiseHr(hr);
}

VB_STD LONG vb___vbaForEachCollVar(EnumVariant **slot, VARIANT *item, IDispatch *collection)
{
    if (!collection)
        VbRaise(VBERR_OBJECT_REQUIRED);
    if (*slot) {
        (*slot)->lpVtbl->Release(*slot);
        *slot = nullptr;
    }
    *slot = enumerator_of(collection);
    return vb___vbaNextEachCollVar(slot, item);
}

VB_STD void vb___vbaExitEachColl(EnumVariant **slot)
{
    if (!*slot)
        VbRaise(92);
    (*slot)->lpVtbl->Release(*slot);
    *slot = nullptr;
}

/* the items as objects: a dispatch interface, or the interface the variable of the loop asks for. */
static LONG next_object(EnumVariant **slot, const IID *iid, IUnknown **variable, IDispatch **dispatch_out)
{
    VARIANT item;
    VariantInit(&item);
    LONG more = vb___vbaNextEachCollVar(slot, &item);
    if (variable && *variable) {
        IUnknown *old = *variable;
        *variable = nullptr;
        old->lpVtbl->Release(old);
    }
    if (dispatch_out && *dispatch_out) {
        IDispatch *old = *dispatch_out;
        *dispatch_out = nullptr;
        old->lpVtbl->Release(old);
    }
    if (!more)
        return 0;
    if ((V_VT(&item) != VT_DISPATCH && V_VT(&item) != VT_UNKNOWN) || !V_UNKNOWN(&item)) {
        VariantClear(&item);
        VbRaise(VBERR_OBJECT_REQUIRED);
    }
    HRESULT hr;
    if (dispatch_out) {
        hr = V_UNKNOWN(&item)->lpVtbl->QueryInterface(V_UNKNOWN(&item), &IID_IDispatch, (void **)dispatch_out);
    } else {
        hr = V_UNKNOWN(&item)->lpVtbl->QueryInterface(V_UNKNOWN(&item), iid, (void **)variable);
    }
    VariantClear(&item);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return -1;
}

VB_STD LONG vb___vbaNextEachCollAd(EnumVariant **slot, IDispatch **out) { return next_object(slot, nullptr, nullptr, out); }

VB_STD LONG vb___vbaForEachCollAd(EnumVariant **slot, IDispatch **out, IDispatch *collection)
{
    VARIANT first;
    VariantInit(&first);
    LONG more = vb___vbaForEachCollVar(slot, &first, collection);
    if (*out) {
        IDispatch *old = *out;
        *out = nullptr;
        old->lpVtbl->Release(old);
    }
    if (!more)
        return 0;
    if (V_VT(&first) != VT_DISPATCH || !V_DISPATCH(&first)) {
        VariantClear(&first);
        VbRaise(VBERR_OBJECT_REQUIRED);
    }
    *out = V_DISPATCH(&first);
    return -1;
}

VB_STD LONG vb___vbaNextEachCollObj(const IID *iid, EnumVariant **slot, IUnknown **variable) { return next_object(slot, iid, variable, nullptr); }

VB_STD LONG vb___vbaForEachCollObj(const IID *iid, EnumVariant **slot, IUnknown **variable, IDispatch *collection)
{
    VARIANT first;
    VariantInit(&first);
    LONG more = vb___vbaForEachCollVar(slot, &first, collection);
    if (*variable) {
        IUnknown *old = *variable;
        *variable = nullptr;
        old->lpVtbl->Release(old);
    }
    if (!more)
        return 0;
    if ((V_VT(&first) != VT_DISPATCH && V_VT(&first) != VT_UNKNOWN) || !V_UNKNOWN(&first)) {
        VariantClear(&first);
        VbRaise(VBERR_OBJECT_REQUIRED);
    }
    HRESULT hr = V_UNKNOWN(&first)->lpVtbl->QueryInterface(V_UNKNOWN(&first), iid, (void **)variable);
    VariantClear(&first);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return -1;
}

/* ------------------------------------------------------------------ arrays */

VB_STD LONG vb___vbaNextEachAry(VARTYPE type, SAFEARRAY **slot, ULONG *index, VARIANT *item)
{
    if (!*slot)
        VbRaise(92);
    if (V_VT(item) & VT_BYREF)
        VbRaise(VBERR_TYPE_MISMATCH);
    VariantClear(item);
    SAFEARRAY *psa = *slot;
    ULONG total = 1;
    for (int i = 0; i < psa->cDims; i++)
        total *= psa->rgsabound[i].cElements;
    if (*index >= total) {
        SafeArrayUnlock(psa);
        *slot = nullptr;
        *index = 0;
        return 0;
    }
    BYTE *element = (BYTE *)psa->pvData + (size_t)*index * psa->cbElements;
    switch (type) {
    case VT_VARIANT: VbCheck(VariantCopy(item, (VARIANT *)element)); break;
    case VT_I2:
    case VT_BOOL: V_I2(item) = *(SHORT *)element; V_VT(item) = type; break;
    case VT_I4:
    case VT_R4:
    case VT_ERROR: V_I4(item) = *(LONG *)element; V_VT(item) = type; break;
    case VT_R8:
    case VT_DATE:
    case VT_CY: V_R8(item) = *(double *)element; V_VT(item) = type; break;
    case VT_UI1: V_UI1(item) = *element; V_VT(item) = type; break;
    case VT_DECIMAL: memcpy(&V_DECIMAL(item), element, sizeof(DECIMAL)); V_VT(item) = VT_DECIMAL; break;
    case VT_BSTR: {
        BSTR text = *(BSTR *)element;
        V_BSTR(item) = text ? SysAllocStringLen(text, SysStringLen(text)) : nullptr;
        V_VT(item) = VT_BSTR;
        break;
    }
    case VT_DISPATCH:
    case VT_UNKNOWN: {
        IUnknown *object = *(IUnknown **)element;
        if (object)
            object->lpVtbl->AddRef(object);
        V_UNKNOWN(item) = object;
        V_VT(item) = type;
        break;
    }
    default: VbRaise(VBERR_TYPE_MISMATCH);
    }
    (*index)++;
    return -1;
}

VB_STD LONG vb___vbaForEachAry(VARTYPE type, SAFEARRAY **slot, ULONG *index, VARIANT *item, SAFEARRAY *psa)
{
    if (!psa || !psa->cDims)
        VbRaise(92);
    if (*slot)
        SafeArrayUnlock(*slot);
    SafeArrayLock(psa);
    *slot = psa;
    *index = 0;
    return vb___vbaNextEachAry(type, slot, index, item);
}

VB_STD void vb___vbaExitEachAry(SAFEARRAY **slot)
{
    if (!*slot)
        VbRaise(92);
    SafeArrayUnlock(*slot);
    *slot = nullptr;
}

/* ------------------------------------------------------------------ either */

/* For Each over a variant takes an object as a collection, and an array as an array. */
VB_STD LONG vb___vbaForEachVar(SAFEARRAY **array_slot, ULONG *index, WORD *kind, EnumVariant **slot, VARIANT *item, VARIANT *source)
{
    if (V_VT(source) == VT_DISPATCH) {
        *kind = 0;
        return vb___vbaForEachCollVar(slot, item, V_DISPATCH(source));
    }
    if (!(V_VT(source) & VT_ARRAY))
        VbRaise(VBERR_TYPE_MISMATCH);
    VARTYPE type = V_VT(source) & ~VT_ARRAY & ~VT_BYREF;
    SAFEARRAY *psa = V_VT(source) & VT_BYREF ? *source->pparray : V_ARRAY(source);
    *kind = type;
    return vb___vbaForEachAry(type, array_slot, index, item, psa);
}

VB_STD LONG vb___vbaNextEachVar(SAFEARRAY **array_slot, ULONG *index, WORD *kind, EnumVariant **slot, VARIANT *item)
{
    if (*kind == 0)
        return vb___vbaNextEachCollVar(slot, item);
    return vb___vbaNextEachAry(*kind, array_slot, index, item);
}

VB_STD void vb___vbaExitEachVar(SAFEARRAY **array_slot, WORD *kind, EnumVariant **slot)
{
    if (*kind)
        vb___vbaExitEachAry(array_slot);
    else
        vb___vbaExitEachColl(slot);
}

/* ------------------------------------------------------------------ counters */

VB_STD LONG vb___vbaI2ForNextCheck(SHORT counter, SHORT limit, SHORT step) { return (step < 0 ? limit <= counter : counter <= limit) ? -1 : 0; }
VB_STD LONG vb___vbaI4ForNextCheck(LONG counter, LONG limit, LONG step) { return (step < 0 ? limit <= counter : counter <= limit) ? -1 : 0; }
VB_STD LONG vb___vbaR4ForNextCheck(float counter, float limit, float step) { return (step < 0 ? limit <= counter : counter <= limit) ? -1 : 0; }
VB_STD LONG vb___vbaR8ForNextCheck(double counter, double limit, double step) { return (step < 0 ? limit <= counter : counter <= limit) ? -1 : 0; }

/* the type a loop with a variant counter runs in: the widest of what the start, the step and the limit are. */
static VARTYPE loop_type(const VARIANT *start, const VARIANT *step, const VARIANT *limit)
{
    const VARIANT *all[3] = {start, step, limit};
    int rank = 0;
    for (int i = 0; i < 3; i++) {
        int r;
        switch (V_VT(all[i])) {
        case VT_EMPTY:
        case VT_I2:
        case VT_BOOL:
        case VT_UI1: r = 1; break;
        case VT_I4: r = 2; break;
        case VT_R4: r = 3; break;
        case VT_R8:
        case VT_DATE:
        case VT_CY:
        case VT_DECIMAL:
        case VT_BSTR: r = 4; break;
        case VT_NULL: VbRaise(VBERR_INVALID_USE_OF_NULL);
        default: VbRaise(VBERR_TYPE_MISMATCH);
        }
        if (r > rank)
            rank = r;
    }
    static const VARTYPE types[] = {VT_I2, VT_I2, VT_I4, VT_R4, VT_R8};
    return types[rank];
}

static void convert_into(VARIANT *target, const VARIANT *source, VARTYPE type)
{
    VARIANT in, out;
    VariantInit(&in);
    VariantInit(&out);
    VbCheck(VbCopyInd(&in, (VARIANT *)source));
    HRESULT hr = VariantChangeTypeEx(&out, &in, VbLocale(), 0, type);
    VariantClear(&in);
    VbCheck(hr);
    VariantClear(target);
    *target = out;
}

/* a step of the loop, or only the question whether it goes on. */
static LONG loop_test(VARIANT *counter, VARIANT *step, VARIANT *limit, BOOL increment)
{
    if (V_VT(limit) == VT_EMPTY)
        VbRaise(92);
    VARTYPE type = V_VT(step);
    if (V_VT(counter) != type) {
        if (V_VT(counter) & VT_BYREF)
            VbRaise(VBERR_TYPE_MISMATCH);
        convert_into(counter, counter, type);
    }
    BOOL down = FALSE;
    switch (type) {
    case VT_I2: {
        LONG sum = V_I2(counter) + (increment ? V_I2(step) : 0);
        if (sum != (SHORT)sum) {
            V_VT(counter) = VT_I4;
            V_I4(counter) = sum;
            return 0;
        }
        V_I2(counter) = (SHORT)sum;
        down = V_I2(step) < 0;
        return down ? V_I2(counter) >= V_I2(limit) : V_I2(counter) <= V_I2(limit);
    }
    case VT_I4: {
        long long sum = (long long)V_I4(counter) + (increment ? V_I4(step) : 0);
        if (sum != (LONG)sum) {
            V_VT(counter) = VT_R8;
            V_R8(counter) = (double)sum;
            return 0;
        }
        V_I4(counter) = (LONG)sum;
        down = V_I4(step) < 0;
        return down ? V_I4(counter) >= V_I4(limit) : V_I4(counter) <= V_I4(limit);
    }
    case VT_R4: {
        double sum = (double)V_R4(counter) + (increment ? (double)V_R4(step) : 0);
        if (sum > 3.402823466e38 || sum < -3.402823466e38) {
            V_VT(counter) = VT_R8;
            V_R8(counter) = sum;
            return 0;
        }
        V_R4(counter) = (float)sum;
        down = V_R4(step) < 0;
        return down ? V_R4(counter) >= V_R4(limit) : V_R4(counter) <= V_R4(limit);
    }
    case VT_R8: {
        V_R8(counter) = V_R8(counter) + (increment ? V_R8(step) : 0);
        down = V_R8(step) < 0;
        return down ? V_R8(counter) >= V_R8(limit) : V_R8(counter) <= V_R8(limit);
    }
    }
    VbRaise(VBERR_TYPE_MISMATCH);
}

/* the counter takes the start, the step and the limit are kept in the type they share, and the first test says whether the loop runs. */
VB_STD LONG vb___vbaVarForInit(VARIANT *counter, VARIANT *step_state, VARIANT *limit_state, VARIANT *start, VARIANT *limit, VARIANT *step)
{
    if (V_VT(counter) & VT_BYREF)
        VbRaise(VBERR_TYPE_MISMATCH);
    VARIANT s, l, t;
    VariantInit(&s);
    VariantInit(&l);
    VariantInit(&t);
    VbCheck(VbCopyInd(&s, start));
    VbCheck(VbCopyInd(&l, limit));
    VbCheck(VbCopyInd(&t, step));
    VARTYPE type = loop_type(&s, &t, &l);
    convert_into(counter, &s, type);
    convert_into(step_state, &t, type);
    convert_into(limit_state, &l, type);
    VariantClear(&s);
    VariantClear(&l);
    VariantClear(&t);
    return loop_test(counter, step_state, limit_state, FALSE);
}

VB_STD LONG vb___vbaVarForNext(VARIANT *counter, VARIANT *step_state, VARIANT *limit_state)
{
    return loop_test(counter, step_state, limit_state, TRUE);
}
