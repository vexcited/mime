/*
 * proxies and stubs for the automation interfaces. IDispatch::Invoke arguments that point into caller memory are
 * sent as the variants they hold and copied back, and the connection point and variant enumerators accept a request
 * for one item with no fetched count.
 */
#include "oleaut32_private.h"
#include <mime/win32/rpcproxy.h>
#include <mime/win32/rpcndr.h>

/* the [call_as] routines the generated proxy file refers to. */
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

/*
 * by reference arguments are taken out of rgvarg, leaving VT_EMPTY behind, and sent as a separate array with their
 * indices. Each is sent with its value, which comes back into the caller's variant, and the server puts them back in
 * place. The flags carry three extra bits for results the caller did not ask for.
 */
#define NO_RESULT 0x20000
#define NO_EXCEPTION 0x40000
#define NO_ARG_ERROR 0x80000
#define BAD_CALLEE ((HRESULT)0x80020010)
/* enough for most automation calls without allocating. */
#define STACK_ARGS 16

HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Proxy(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                 DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    if (!IsEqualGUID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    if (!params || params->cNamedArgs > params->cArgs)
        return E_INVALIDARG;
    UINT call = flags & 0xF;
    UINT count = params->cArgs;
    if (!count) {
        if (call & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF))
            return E_INVALIDARG;
    } else if (!params->rgvarg || (params->cNamedArgs && !params->rgdispidNamedArgs)) {
        return E_INVALIDARG;
    }
    if (call & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF))
        result = nullptr;

    UINT refs = 0;
    for (UINT i = 0; i < count; i++)
        if (params->rgvarg[i].vt & VT_BYREF)
            refs++;

    VARIANT stack_args[STACK_ARGS], stack_values[STACK_ARGS];
    UINT stack_indices[STACK_ARGS];
    VARIANT dummy_value;
    UINT dummy_index = 0;
    VARIANT *args = params->rgvarg, *values = &dummy_value;
    UINT *indices = &dummy_index;
    void *block = nullptr;
    if (refs) {
        if (count <= STACK_ARGS) {
            args = stack_args;
            values = stack_values;
            indices = stack_indices;
        } else {
            block = AutAlloc((SIZE_T)(count + refs) * sizeof(VARIANT) + (SIZE_T)refs * sizeof(UINT));
            if (!block)
                return E_OUTOFMEMORY;
            args = block;
            values = args + count;
            indices = (UINT *)(values + refs);
        }
        UINT k = 0;
        for (UINT i = 0; i < count; i++) {
            if (params->rgvarg[i].vt & VT_BYREF) {
                values[k] = params->rgvarg[i];
                indices[k++] = i;
                VariantInit(&args[i]);
            } else {
                args[i] = params->rgvarg[i];
            }
        }
    }

    DWORD wire_flags = call;
    VARIANT local_result;
    EXCEPINFO local_excep;
    UINT local_arg_err;
    VariantInit(&local_result);
    memset(&local_excep, 0, sizeof(local_excep));
    if (!result) {
        wire_flags |= NO_RESULT;
        result = &local_result;
    }
    if (!excep) {
        wire_flags |= NO_EXCEPTION;
        excep = &local_excep;
    }
    if (!arg_err) {
        wire_flags |= NO_ARG_ERROR;
        arg_err = &local_arg_err;
    }

    DISPPARAMS sent = *params;
    sent.rgvarg = args;
    HRESULT hr = IDispatch_RemoteInvoke_Proxy(This, id, riid, lcid, wire_flags, &sent, result, excep, arg_err, refs,
                                              indices, values);
    if (excep != &local_excep)
        excep->pfnDeferredFillIn = nullptr;
    AutFree(block);
    return hr;
}

/* the by reference values belong to the marshaller, so their slots are emptied before returning. */
HRESULT STDMETHODCALLTYPE IDispatch_Invoke_Stub(IDispatch *This, long id, REFIID riid, unsigned long lcid,
                                                unsigned long flags, DISPPARAMS *params, VARIANT *result,
                                                EXCEPINFO *excep, unsigned int *arg_err, unsigned int refs,
                                                unsigned int *indices, VARIANT *values)
{
    UINT count = params->cArgs;
    VARTYPE stack_types[STACK_ARGS];
    VARTYPE *types = stack_types;
    if (!count) {
        params->cNamedArgs = 0;
    } else {
        if (!params->rgvarg || (params->cNamedArgs && !params->rgdispidNamedArgs))
            return E_INVALIDARG;
        if (count > STACK_ARGS) {
            types = AutAlloc((SIZE_T)count * sizeof(VARTYPE));
            if (!types)
                return E_OUTOFMEMORY;
        }
        for (UINT k = 0; k < refs; k++) {
            if (indices[k] >= count) {
                if (types != stack_types)
                    AutFree(types);
                return E_INVALIDARG;
            }
            params->rgvarg[indices[k]] = values[k];
        }
        for (UINT i = 0; i < count; i++)
            types[i] = params->rgvarg[i].vt;
    }

    EXCEPINFO *object_excep = excep;
    if (flags & NO_EXCEPTION)
        object_excep = nullptr;
    else if (excep)
        excep->pfnDeferredFillIn = nullptr;
    HRESULT hr = This->lpVtbl->Invoke(This, id, riid, lcid, (WORD)flags, params, (flags & NO_RESULT) ? nullptr : result,
                                      object_excep, (flags & NO_ARG_ERROR) ? nullptr : arg_err);
    if (FAILED(hr)) {
        if (hr == DISP_E_EXCEPTION && object_excep && object_excep->pfnDeferredFillIn) {
            object_excep->pfnDeferredFillIn(object_excep);
            object_excep->pfnDeferredFillIn = nullptr;
        }
    } else if (count && refs) {
        for (UINT i = 0; i < count; i++) {
            if (params->rgvarg[i].vt != types[i]) {
                hr = BAD_CALLEE;
                break;
            }
        }
    }
    for (UINT k = 0; k < refs && count; k++)
        params->rgvarg[indices[k]].vt = VT_EMPTY;
    if (types != stack_types)
        AutFree(types);
    DcomChannelSetHResult(nullptr, nullptr, hr);
    return hr;
}

/* fetched may be NULL when exactly one item is asked for. */
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

/* the automation proxy CLSIDs, all served by the proxy file factory except PSOAInterface. */
static BOOL is_proxy_class(REFCLSID clsid)
{
    static const BYTE tail[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46};
    return clsid->Data1 >= 0x00020420 && clsid->Data1 <= 0x00020426 && clsid->Data2 == 0 && clsid->Data3 == 0 &&
           !memcmp(clsid->Data4, tail, 8);
}

HRESULT AutUniversalFactory(REFIID iid, void **object);
HRESULT AutStandardFactory(REFCLSID clsid, REFIID iid, void **object);

WINOLEAUTAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!is_proxy_class(clsid))
        return AutStandardFactory(clsid, iid, object);
    if (clsid->Data1 == 0x00020424)
        return AutUniversalFactory(iid, object);
    return NdrDllGetClassObject(clsid, iid, object, oaproxy_ProxyFileList, clsid, &oaproxy_PSFactory);
}

WINOLEAUTAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return NdrDllCanUnloadNow(&oaproxy_PSFactory);
}
