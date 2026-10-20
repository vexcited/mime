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

/*
 * The arguments of a call that are references go over the wire twice in a way: the variants that refer to a value are taken
 * out of the arguments, which hold an empty variant in their place, and are sent as an array of their own with the places
 * they had. The variants of that array are sent as they are, and a variant that refers to a value is sent with the value, which
 * comes back in the variant the caller has when the call is done. The server puts them back where they were. The flags are
 * the flags of the call with three more for the results the caller did not ask for.
 */
#define NO_RESULT 0x20000
#define NO_EXCEPTION 0x40000
#define NO_ARG_ERROR 0x80000
#define INVALID_ARG ((HRESULT)0x80070057)
#define BAD_CALLEE ((HRESULT)0x80020010)
/* the arguments that are kept on the stack, as many as a call to a method of automation mostly has. */
#define STACK_ARGS 16

HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Proxy(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                 DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    if (!IsEqualGUID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    DWORD call = flags & 0xF;
    if (!params || params->cNamedArgs > params->cArgs)
        return INVALID_ARG;
    UINT count = params->cArgs;
    if (!count) {
        /* a property that is set has the value to set. */
        if (call & 0xC)
            return INVALID_ARG;
    } else if (!params->rgvarg || (params->cNamedArgs && !params->rgdispidNamedArgs)) {
        return INVALID_ARG;
    }
    if (call & 0xC)
        result = nullptr;
    VARIANT *original = params->rgvarg;
    UINT refs = 0;
    for (UINT i = 0; i < count; i++)
        if (original[i].vt & VT_BYREF)
            refs++;
    /* the wire form has arrays of the references, which are of nothing when there are none and still are there. */
    UINT dummy_index = 0;
    VARIANT dummy_value;
    VariantInit(&dummy_value);
    UINT *indices = &dummy_index;
    VARIANT *values = &dummy_value;
    VARIANT *args = original;
    VARIANT stack_args[STACK_ARGS];
    UINT stack_indices[STACK_ARGS];
    VARIANT stack_values[STACK_ARGS];
    BYTE *block = nullptr;
    if (refs) {
        if (count <= STACK_ARGS) {
            args = stack_args;
            indices = stack_indices;
            values = stack_values;
        } else {
            block = AutAlloc((SIZE_T)count * sizeof(VARIANT) + (SIZE_T)refs * (sizeof(VARIANT) + sizeof(UINT)));
            if (!block)
                return E_OUTOFMEMORY;
            args = (VARIANT *)block;
            values = args + count;
            indices = (UINT *)(values + refs);
        }
        UINT k = 0;
        for (UINT i = 0; i < count; i++) {
            if (original[i].vt & VT_BYREF) {
                values[k] = original[i];
                indices[k++] = i;
                memset(&args[i], 0, sizeof(VARIANT));
            } else {
                args[i] = original[i];
            }
        }
    }
    DWORD wire_flags = call;
    VARIANT local_result;
    EXCEPINFO local_excep;
    UINT local_err = 0;
    if (!result) {
        wire_flags |= NO_RESULT;
        result = &local_result;
    }
    if (!excep) {
        wire_flags |= NO_EXCEPTION;
        excep = &local_excep;
        memset(excep, 0, sizeof(*excep));
    }
    if (!arg_err) {
        wire_flags |= NO_ARG_ERROR;
        arg_err = &local_err;
    }
    params->rgvarg = args;
    HRESULT hr = IDispatch_RemoteInvoke_Proxy(This, id, riid, lcid, wire_flags, params, result, excep, arg_err, refs, indices,
                                              values);
    params->rgvarg = original;
    if (!(wire_flags & NO_EXCEPTION))
        excep->pfnDeferredFillIn = nullptr;
    else if (hr == DISP_E_EXCEPTION) {
        SysFreeString(local_excep.bstrSource);
        SysFreeString(local_excep.bstrDescription);
        SysFreeString(local_excep.bstrHelpFile);
    }
    if (wire_flags & NO_RESULT)
        VariantClear(&local_result);
    AutFree(block);
    return hr;
}

HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Stub(IDispatch *This, long id, REFIID riid, unsigned long lcid,
                                                unsigned long flags, DISPPARAMS *params, VARIANT *result,
                                                EXCEPINFO *excep, unsigned int *arg_err, unsigned int refs,
                                                unsigned int *indices, VARIANT *values)
{
    UINT count = params->cArgs;
    WORD *types = nullptr;
    if (count) {
        if (!params->rgvarg || (params->cNamedArgs && !params->rgdispidNamedArgs))
            return INVALID_ARG;
        types = AutAlloc((SIZE_T)count * sizeof(WORD));
        if (!types)
            return E_OUTOFMEMORY;
        /* the references take the places they had. */
        for (UINT k = 0; k < refs; k++) {
            if (indices[k] >= count) {
                AutFree(types);
                return INVALID_ARG;
            }
            params->rgvarg[indices[k]] = values[k];
        }
        for (UINT i = 0; i < count; i++)
            types[i] = params->rgvarg[i].vt;
    } else {
        params->cNamedArgs = 0;
    }
    VARIANT *out_result = result;
    EXCEPINFO *out_excep = excep;
    UINT *out_err = arg_err;
    if (flags & NO_RESULT)
        out_result = nullptr;
    if (flags & NO_EXCEPTION)
        out_excep = nullptr;
    else
        excep->pfnDeferredFillIn = nullptr;
    if (flags & NO_ARG_ERROR)
        out_err = nullptr;
    HRESULT hr = This->lpVtbl->Invoke(This, id, riid, lcid, (WORD)flags, params, out_result, out_excep, out_err);
    if (hr < 0) {
        /* a fill in routine is of the process of the object, so the exception is completed here. */
        if (hr == DISP_E_EXCEPTION && out_excep && out_excep->pfnDeferredFillIn) {
            out_excep->pfnDeferredFillIn(out_excep);
            out_excep->pfnDeferredFillIn = nullptr;
        }
    } else if (count && refs) {
        /* an argument that the object changed to another type is a call that did not do what it was asked. */
        for (UINT i = 0; i < count; i++)
            if (params->rgvarg[i].vt != types[i]) {
                hr = BAD_CALLEE;
                break;
            }
    }
    /* the variants of the references are the engine's to free, not the arguments'. */
    for (UINT k = 0; k < refs; k++)
        params->rgvarg[indices[k]].vt = VT_EMPTY;
    AutFree(types);
    DcomChannelSetHResult(nullptr, nullptr, hr);
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
