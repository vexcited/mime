/*
 * the proxies and stubs of the interfaces of automation: the factory a class of them is reached through, and what turns
 * the arguments of a call into those that cross processes. IDispatch::Invoke takes arguments that point into the caller's
 * memory, which are sent as the variants they hold and brought back as they were changed, and the enumerators of the
 * interfaces of connection points and of variants may be asked for one item with no place to say how many came.
 */
#include "oleaut32_private.h"
#include <mime/win32/rpcproxy.h>
#include <mime/win32/rpcndr.h>

/* the routines the generated proxy file names: those that stand in on the wire for the methods they are the local form of. */
HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Proxy(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                 DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err);
HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Stub(IDispatch *This, long id, REFIID riid, unsigned long lcid,
                                                unsigned long flags, DISPPARAMS *params, VARIANT *result,
                                                EXCEPINFO *excep, unsigned int *arg_err, unsigned int refs,
                                                unsigned int *indices, VARIANT *values);
HRESULT STDMETHODCALLTYPE IEnumVARIANT_Next_Proxy(IEnumVARIANT *This, ULONG count, VARIANT *items, ULONG *fetched);
HRESULT STDMETHODCALLTYPE IEnumConnectionPoints_Next_Proxy(IEnumConnectionPoints *This, ULONG count,
                                                           IConnectionPoint **items, ULONG *fetched);
HRESULT STDMETHODCALLTYPE IEnumConnections_Next_Proxy(IEnumConnections *This, ULONG count, CONNECTDATA *items,
                                                      ULONG *fetched);

#include "oleaut32/oa_ps.h"

/* ------------------------------------------------------------------ Invoke */

/* the place a pointer to a value is made to point to in the variant that holds it. */
static void *value_of(VARIANT *v, VARTYPE base)
{
    switch (base) {
    case VT_VARIANT: return v;
    case VT_DECIMAL: return &v->decVal;
    default: return &v->llVal;
    }
}

/* takes the value a call left in a variant to the place the argument refers to, which gives up what it held. */
static HRESULT move_back(const VARIANT *arg, VARIANT *value)
{
    VARTYPE base = arg->vt & ~VT_BYREF;
    void *target = arg->byref;
    if (!target)
        return E_INVALIDARG;
    switch (base) {
    case VT_VARIANT:
        VariantClear(target);
        memcpy(target, value, sizeof(VARIANT));
        VariantInit(value);
        return S_OK;
    case VT_BSTR:
        SysFreeString(*(BSTR *)target);
        *(BSTR *)target = value->bstrVal;
        value->vt = VT_EMPTY;
        return S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
        AutRelease(*(void **)target);
        *(void **)target = value->punkVal;
        value->vt = VT_EMPTY;
        return S_OK;
    case VT_DECIMAL:
        memcpy(target, &value->decVal, sizeof(DECIMAL));
        return S_OK;
    case VT_RECORD:
        return E_INVALIDARG;
    default:
        break;
    }
    if (base & VT_ARRAY) {
        SafeArrayDestroy(*(SAFEARRAY **)target);
        *(SAFEARRAY **)target = value->parray;
        value->vt = VT_EMPTY;
        return S_OK;
    }
    switch (base) {
    case VT_I1:
    case VT_UI1: *(BYTE *)target = value->bVal; break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: *(USHORT *)target = value->uiVal; break;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR: *(ULONG *)target = value->ulVal; break;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE: *(ULONGLONG *)target = value->ullVal; break;
    case VT_INT_PTR:
    case VT_UINT_PTR: *(ULONG_PTR *)target = (ULONG_PTR)value->llVal; break;
    default: return DISP_E_BADVARTYPE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Proxy(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                 DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    VARIANT local_result;
    EXCEPINFO local_excep;
    UINT local_err = 0;
    if (!params)
        return E_INVALIDARG;
    if (!result)
        result = &local_result;
    if (!excep)
        excep = &local_excep;
    if (!arg_err)
        arg_err = &local_err;
    VariantInit(result);
    memset(excep, 0, sizeof(*excep));
    /* the arguments that refer to values are sent with those values, which the call may change and send back. */
    UINT refs = 0;
    for (UINT i = 0; i < params->cArgs; i++)
        if (params->rgvarg[i].vt & VT_BYREF)
            refs++;
    /* the wire form has arrays of the references, which are of nothing when there are none and still are there. */
    UINT dummy_index = 0;
    VARIANT dummy_value;
    VariantInit(&dummy_value);
    UINT *indices = &dummy_index;
    VARIANT *values = &dummy_value;
    if (refs) {
        indices = AutAlloc((SIZE_T)refs * sizeof(UINT));
        values = AutAllocZero((SIZE_T)refs * sizeof(VARIANT));
        if (!indices || !values) {
            AutFree(indices);
            AutFree(values);
            return E_OUTOFMEMORY;
        }
    }
    HRESULT hr = S_OK;
    UINT j = 0;
    for (UINT i = 0; i < params->cArgs && SUCCEEDED(hr); i++) {
        if (!(params->rgvarg[i].vt & VT_BYREF))
            continue;
        indices[j] = i;
        hr = VariantCopyInd(&values[j], &params->rgvarg[i]);
        j++;
    }
    if (SUCCEEDED(hr))
        hr = IDispatch_RemoteInvoke_Proxy(This, id, riid, lcid, flags, params, result, excep, arg_err, refs, indices,
                                          values);
    if (SUCCEEDED(hr)) {
        for (UINT k = 0; k < refs; k++)
            move_back(&params->rgvarg[indices[k]], &values[k]);
    }
    for (UINT k = 0; k < refs; k++)
        VariantClear(&values[k]);
    if (refs) {
        AutFree(indices);
        AutFree(values);
    }
    if (result == &local_result)
        VariantClear(result);
    if (excep == &local_excep) {
        SysFreeString(excep->bstrSource);
        SysFreeString(excep->bstrDescription);
        SysFreeString(excep->bstrHelpFile);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Stub(IDispatch *This, long id, REFIID riid, unsigned long lcid,
                                                unsigned long flags, DISPPARAMS *params, VARIANT *result,
                                                EXCEPINFO *excep, unsigned int *arg_err, unsigned int refs,
                                                unsigned int *indices, VARIANT *values)
{
    VariantInit(result);
    memset(excep, 0, sizeof(*excep));
    *arg_err = 0;
    void **saved = nullptr;
    if (refs) {
        saved = AutAllocZero((SIZE_T)refs * sizeof(void *));
        if (!saved)
            return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    UINT done = 0;
    /* the arguments that are references are pointed at the values that come back with the call. */
    for (UINT k = 0; k < refs && SUCCEEDED(hr); k++) {
        if (indices[k] >= params->cArgs || !(params->rgvarg[indices[k]].vt & VT_BYREF)) {
            hr = E_INVALIDARG;
            break;
        }
        VARIANT *arg = &params->rgvarg[indices[k]];
        VARTYPE base = arg->vt & ~VT_BYREF;
        if (base == VT_RECORD) {
            hr = E_INVALIDARG;
            break;
        }
        saved[k] = arg->byref;
        arg->byref = value_of(&values[k], base);
        done = k + 1;
    }
    if (SUCCEEDED(hr))
        hr = This->lpVtbl->Invoke(This, id, riid, lcid, (WORD)flags, params, result, excep, arg_err);
    for (UINT k = 0; k < done; k++)
        params->rgvarg[indices[k]].byref = saved[k];
    AutFree(saved);
    /* a fill in routine is of the process of the object, so the exception is completed here. */
    if (hr == DISP_E_EXCEPTION && excep->pfnDeferredFillIn) {
        excep->pfnDeferredFillIn(excep);
    }
    excep->pfnDeferredFillIn = nullptr;
    return hr;
}

/* ------------------------------------------------------------------ the enumerators */

/* One item may be asked for with no place for the count, and the count is then of no interest. */
#define ENUM_NEXT(iface, type)                                                                                         \
    HRESULT STDMETHODCALLTYPE iface##_Next_Proxy(iface *This, ULONG count, type *items, ULONG *fetched)                \
    {                                                                                                                  \
        ULONG local = 0;                                                                                               \
        if (!fetched) {                                                                                                \
            if (count != 1)                                                                                            \
                return E_INVALIDARG;                                                                                   \
            fetched = &local;                                                                                          \
        }                                                                                                              \
        return iface##_RemoteNext_Proxy(This, count, items, fetched);                                                  \
    }

ENUM_NEXT(IEnumVARIANT, VARIANT)
ENUM_NEXT(IEnumConnectionPoints, IConnectionPoint *)
ENUM_NEXT(IEnumConnections, CONNECTDATA)

/* ------------------------------------------------------------------ the classes */

/* The classes of the proxies of automation: every one of them answers with the factory of all the interfaces above. */
static BOOL is_proxy_class(REFCLSID clsid)
{
    static const BYTE tail[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46};
    return clsid->Data1 >= 0x00020420 && clsid->Data1 <= 0x00020426 && clsid->Data2 == 0 && clsid->Data3 == 0 &&
           !memcmp(clsid->Data4, tail, 8);
}

HRESULT AutUniversalFactory(REFIID iid, void **object);

WINOLEAUTAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!is_proxy_class(clsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (clsid->Data1 == 0x00020424)
        return AutUniversalFactory(iid, object);
    return NdrDllGetClassObject(clsid, iid, object, oaproxy_ProxyFileList, clsid, &oaproxy_PSFactory);
}

WINOLEAUTAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return NdrDllCanUnloadNow(&oaproxy_PSFactory);
}
