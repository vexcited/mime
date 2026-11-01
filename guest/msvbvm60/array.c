/*
 * arrays of Visual Basic are safe arrays. a program makes one with Dim and ReDim, keeps one locked while it works on it,
 * and gives it up with Erase.
 */
#include "vb.h"
#include <stdarg.h>

/* a new array: the features and the size of an element, the bounds as pairs of the upper and the lower. */
static void redim_core(USHORT features, USHORT element, LONG type, USHORT dims, const LONG *bounds, SAFEARRAY **slot)
{
    BOOL bounded = dims != 0;
    HRESULT hr;
    if (!dims)
        dims = 1;
    SAFEARRAY *psa = *slot;
    if (psa) {
        if ((psa->fFeatures & 0x16) || psa->cLocks)
            VbRaise(10);
        if (psa->pvData) {
            hr = SafeArrayDestroyData(psa);
            if (FAILED(hr))
                VbCheck(hr);
            psa->pvData = nullptr;
        }
        if (psa->cDims != dims || psa->fFeatures != features) {
            hr = SafeArrayDestroyDescriptor(psa);
            if (FAILED(hr))
                VbCheck(hr);
            *slot = psa = nullptr;
        }
    }
    if (!psa) {
        if (features & 0xE0) {
            VARTYPE vt = features & 0x40 ? VT_VARIANT : features & 0x20 ? VT_RECORD : (VARTYPE)type;
            hr = SafeArrayAllocDescriptorEx(vt, dims, slot);
            if (FAILED(hr))
                VbCheck(hr);
            if ((features & 0x20) && type)
                SafeArraySetRecordInfo(*slot, (IRecordInfo *)(ULONG_PTR)type);
        } else {
            hr = SafeArrayAllocDescriptor(dims, slot);
            if (FAILED(hr))
                VbCheck(hr);
        }
        psa = *slot;
        psa->fFeatures = features;
    }
    psa->cbElements = element;
    for (USHORT i = 0; i < dims; i++) {
        LONG upper = bounds[i * 2], lower = bounds[i * 2 + 1];
        if (bounded && upper < lower) {
            SafeArrayDestroyDescriptor(psa);
            *slot = nullptr;
            VbRaise(VBERR_SUBSCRIPT);
        }
        psa->rgsabound[i].cElements = (ULONG)(upper - lower + 1);
        psa->rgsabound[i].lLbound = lower;
    }
    hr = SafeArrayAllocData(psa);
    if (FAILED(hr)) {
        SafeArrayDestroyDescriptor(psa);
        *slot = nullptr;
        VbCheck(hr);
    }
}

/* only the last dimension of an array grows or shrinks and keeps what it holds. */
static void redim_preserve_core(USHORT features, USHORT element, LONG type, USHORT dims, const LONG *bounds, SAFEARRAY **slot)
{
    SAFEARRAY *psa = *slot;
    if (!psa) {
        redim_core(features, element, type, dims, bounds, slot);
        return;
    }
    if (!dims || psa->cDims != dims)
        VbRaise(VBERR_SUBSCRIPT);
    if ((psa->fFeatures & 0x16) || psa->cLocks)
        VbRaise(10);
    if (bounds[0] < bounds[1] || bounds[1] != psa->rgsabound[0].lLbound)
        VbRaise(VBERR_SUBSCRIPT);
    SAFEARRAYBOUND last = {(ULONG)(bounds[0] - bounds[1] + 1), bounds[1]};
    for (USHORT i = 1; i < dims; i++) {
        LONG upper = bounds[i * 2], lower = bounds[i * 2 + 1];
        if (lower != psa->rgsabound[i].lLbound || upper < lower || (ULONG)(upper - lower + 1) != psa->rgsabound[i].cElements)
            VbRaise(VBERR_SUBSCRIPT);
    }
    VbCheck(SafeArrayRedim(psa, &last));
}

/* the bounds follow as pairs of the upper and the lower, one pair for each dimension. */
#define MAX_DIMS 60

static USHORT read_bounds(USHORT dims, LONG *bounds, va_list args)
{
    if (dims > MAX_DIMS)
        VbRaise(VBERR_SUBSCRIPT);
    for (USHORT i = 0; i < (dims ? dims : 1) * 2u; i++)
        bounds[i] = va_arg(args, LONG);
    return dims;
}

VB_C SAFEARRAY *vb___vbaRedim(LONG feature_word, LONG element_word, SAFEARRAY **slot, LONG *type, LONG dimensions, ...)
{
    USHORT features = (USHORT)feature_word, element = (USHORT)element_word, dims = (USHORT)dimensions;
    LONG bounds[MAX_DIMS * 2];
    va_list args;
    va_start(args, dimensions);
    read_bounds(dims, bounds, args);
    va_end(args);
    LONG argument = (features & 0x20) ? *type : (LONG)(ULONG_PTR)type;
    redim_core(features, element, argument, dims, bounds, slot);
    return *slot;
}

VB_C SAFEARRAY *vb___vbaRedimPreserve(LONG feature_word, LONG element_word, SAFEARRAY **slot, LONG *type, LONG dimensions, ...)
{
    USHORT features = (USHORT)feature_word, element = (USHORT)element_word, dims = (USHORT)dimensions;
    LONG bounds[MAX_DIMS * 2];
    va_list args;
    va_start(args, dimensions);
    read_bounds(dims, bounds, args);
    va_end(args);
    LONG argument = (features & 0x20) ? *type : (LONG)(ULONG_PTR)type;
    redim_preserve_core(features, element, argument, dims, bounds, slot);
    return *slot;
}

/* the same for a variant that holds an array, whose slot is the array of the variant. */
VB_C VARIANT *vb___vbaRedimVar(LONG type_word, VARIANT *v, LONG dimensions, ...)
{
    VARTYPE vt = (VARTYPE)type_word;
    USHORT dims = (USHORT)dimensions;
    LONG bounds[MAX_DIMS * 2];
    va_list args;
    va_start(args, dimensions);
    read_bounds(dims, bounds, args);
    va_end(args);
    SAFEARRAY *psa = nullptr;
    if (V_VT(v) & VT_ARRAY)
        psa = V_VT(v) & VT_BYREF ? *v->pparray : v->parray;
    redim_core(0x80 | 0x40, sizeof(VARIANT), (LONG)vt, dims, bounds, &psa);
    VariantClear(v);
    V_VT(v) = VT_ARRAY | VT_VARIANT;
    v->parray = psa;
    return v;
}

/* Erase gives the data of an array back, and the descriptor too unless the array is a fixed one of the program. */
static LONG erase(SAFEARRAY **slot)
{
    SAFEARRAY *psa = *slot;
    if (!psa)
        return 0;
    if (psa->cLocks)
        return 10;
    HRESULT hr = SafeArrayDestroyData(psa);
    if (SUCCEEDED(hr)) {
        if ((psa->fFeatures & 7) != 0)
            return 0;
        hr = SafeArrayDestroyDescriptor(psa);
        if (SUCCEEDED(hr)) {
            *slot = nullptr;
            return 0;
        }
    }
    return VbErrorOfHresult(hr);
}

VB_STD LONG vb___vbaErase(LONG type, SAFEARRAY **slot)
{
    LONG error = erase(slot);
    if (error)
        VbRaise(error);
    return error;
}

VB_STD SAFEARRAY *vb___vbaAryDestruct(LONG type, SAFEARRAY **slot)
{
    SAFEARRAY *psa = *slot;
    if (!psa)
        return nullptr;
    if (psa->fFeatures & 5)
        return psa;
    LONG error = erase(slot);
    if (error)
        VbRaise(error);
    psa = *slot;
    if (psa && psa->pvData) {
        psa->fFeatures &= 0xFFED;
        SafeArrayDestroyData(psa);
    }
    return psa;
}

/* a fixed array of the program is built from a template that holds its features and bounds. */
VB_STD LONG vb___vbaAryConstruct2(SAFEARRAY *psa, const SAFEARRAY *model, LONG type)
{
    ULONG size = model->cDims ? 8u * model->cDims - 8u + 24u : 24u;
    __builtin_memcpy(psa, model, size);
    if (!(psa->fFeatures & 4)) {
        HRESULT hr = SafeArrayAllocData(psa);
        if (FAILED(hr) || !psa->pvData) {
            if (!(psa->fFeatures & 7) || (psa->fFeatures & 0x1000))
                SafeArrayDestroyDescriptor(psa);
            VbCheck(FAILED(hr) ? hr : E_OUTOFMEMORY);
        }
    }
    return 0;
}

VB_STD SAFEARRAY *vb___vbaAryLock(SAFEARRAY **slot, SAFEARRAY *psa)
{
    if (!psa)
        VbRaise(VBERR_SUBSCRIPT);
    SafeArrayLock(psa);
    *slot = psa;
    return psa;
}

VB_STD HRESULT vb___vbaAryUnlock(SAFEARRAY **slot)
{
    HRESULT hr = 0;
    if (*slot) {
        hr = SafeArrayUnlock(*slot);
        *slot = nullptr;
    }
    return hr;
}

/* the upper bound of a dimension, counted from the first. */
VB_STD LONG vb___vbaUbound(SHORT dimension, SAFEARRAY *psa)
{
    if (dimension < 1 || !psa || dimension > (SHORT)psa->cDims)
        VbRaise(VBERR_SUBSCRIPT);
    const SAFEARRAYBOUND *bound = &psa->rgsabound[psa->cDims - dimension];
    return bound->lLbound + (LONG)bound->cElements - 1;
}

/* the slot of the array a variant holds. */
VB_STD SAFEARRAY **vb___vbaRefVarAry(VARIANT *v)
{
    if (!(V_VT(v) & VT_ARRAY))
        VbRaise(VBERR_TYPE_MISMATCH);
    if (V_VT(v) & VT_BYREF)
        return v->pparray;
    return &v->parray;
}

/* an object a variant holds, as another of its interfaces. */
VB_STD IUnknown *vb___vbaCastObjVar(VARIANT *v, const IID *iid)
{
    if (V_VT(v) != VT_DISPATCH && V_VT(v) != VT_UNKNOWN)
        VbRaise(VBERR_OBJECT_REQUIRED);
    IUnknown *out = nullptr;
    if (!V_UNKNOWN(v))
        return nullptr;
    VbCheck(V_UNKNOWN(v)->lpVtbl->QueryInterface(V_UNKNOWN(v), iid, (void **)&out));
    return out;
}
