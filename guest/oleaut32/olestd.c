#include "olestd.h"

/* the quotient rounded to the nearest integer, halves away from zero. */
LONGLONG OleStdRoundDiv(LONGLONG numerator, LONGLONG denominator)
{
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    LONGLONG half = denominator / 2;
    return numerator >= 0 ? (numerator + half) / denominator : -((-numerator + half) / denominator);
}

/*
 * GetIDsOfNames of the standard objects. The first name is a member, any other one would be a parameter of it, and
 * these members have none. Names are not case sensitive. A name that is not known gets DISPID_UNKNOWN and fails the
 * call, the others are still looked up.
 */
HRESULT OleStdIdsOfNames(const OLESTD_MEMBER *members, int count, REFIID riid, LPOLESTR *names, UINT name_count,
                         DISPID *ids)
{
    if (!riid)
        return E_INVALIDARG;
    if (!IsEqualIID(riid, &GUID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    if (!names || !ids || !name_count)
        return E_INVALIDARG;
    HRESULT result = S_OK;
    for (UINT i = 0; i < name_count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i == 0 && names[0]) {
            for (int m = 0; m < count; m++) {
                if (!_wcsicmp(names[0], members[m].Name)) {
                    ids[0] = members[m].Id;
                    break;
                }
            }
        }
        if (ids[i] == DISPID_UNKNOWN)
            result = DISP_E_UNKNOWNNAME;
    }
    return result;
}

/* a control error becomes an exception of the call, which is how automation clients learn of it. */
HRESULT OleStdException(HRESULT hr, EXCEPINFO *info)
{
    if ((hr & OLESTD_CONTROL_MASK) != OLESTD_CONTROL_ERROR)
        return hr;
    if (info) {
        memset(info, 0, sizeof(*info));
        info->scode = hr;
    }
    return DISP_E_EXCEPTION;
}

/* the type information of one of the interfaces of stdole2.tlb, whose library is the only one that has any. */
HRESULT OleStdTypeInfo(UINT index, REFGUID id, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index != 0)
        return DISP_E_BADINDEX;
    ITypeLib *library = nullptr;
    HRESULT hr = LoadRegTypeLib(&LIBID_StdOle, 2, 0, 0, &library);
    if (FAILED(hr))
        hr = LoadTypeLib(L"stdole2.tlb", &library);
    if (FAILED(hr))
        return hr;
    hr = library->lpVtbl->GetTypeInfoOfGuid(library, id, info);
    library->lpVtbl->Release(library);
    return hr;
}
