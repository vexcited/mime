#include "oleaut32_private.h"

WINOLEAUTAPI HRESULT WINAPI SafeArraySetRecordInfo(SAFEARRAY *array, IRecordInfo *record)
{
    return array && (array->fFeatures & FADF_RECORD) ? E_NOTIMPL : E_INVALIDARG;
}
WINOLEAUTAPI HRESULT WINAPI GetRecordInfoFromTypeInfo(void *type, IRecordInfo **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    return E_NOTIMPL;
}
WINOLEAUTAPI HRESULT WINAPI LoadTypeLibEx(LPCOLESTR path, int kind, void **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    return (HRESULT)0x80029c4aL;
}
WINOLEAUTAPI HRESULT WINAPI LoadRegTypeLib(REFGUID id, WORD major, WORD minor, LCID locale, void **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    return (HRESULT)0x8002801dL;
}
WINOLEAUTAPI HRESULT WINAPI QueryPathOfRegTypeLib(REFGUID id, WORD major, WORD minor, LCID locale, BSTR *result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    return (HRESULT)0x8002801dL;
}
WINOLEAUTAPI HRESULT WINAPI VariantChangeTypeEx(VARIANT *destination, const VARIANT *source,
    LCID locale, USHORT flags, VARTYPE type)
{
    if (!source || !destination) return E_INVALIDARG;
    if (source->vt == type) return VariantCopy(destination, source);
    if (type == VT_EMPTY || type == VT_NULL) {
        HRESULT hr = VariantClear(destination);
        if (SUCCEEDED(hr)) destination->vt = type;
        return hr;
    }
    return DISP_E_TYPEMISMATCH;
}
WINOLEAUTAPI HRESULT WINAPI VariantChangeType(VARIANT *destination, const VARIANT *source, USHORT flags, VARTYPE type)
{
    return VariantChangeTypeEx(destination, source, 0x400, flags, type);
}
WINOLEAUTAPI HRESULT WINAPI VarCyFromDec(const DECIMAL *decimal, CY *result)
{
    if (!decimal || !result || decimal->scale > 28 || (decimal->sign & ~0x80)) return E_INVALIDARG;
    ULONG words[3] = {(ULONG)decimal->Lo64, (ULONG)(decimal->Lo64 >> 32), decimal->Hi32};
    if (decimal->scale < 4) {
        for (UINT i = decimal->scale; i < 4; i++) {
            ULONGLONG carry = 0;
            for (UINT j = 0; j < 3; j++) {
                ULONGLONG value = (ULONGLONG)words[j] * 10 + carry;
                words[j] = (ULONG)value;
                carry = value >> 32;
            }
            if (carry) return DISP_E_OVERFLOW;
        }
    } else if (decimal->scale > 4) {
        ULONG remainder = 0;
        BOOL sticky = FALSE;
        for (UINT i = 4; i < decimal->scale; i++) {
            sticky |= remainder != 0;
            ULONGLONG carry = 0;
            for (int j = 2; j >= 0; j--) {
                ULONGLONG value = (carry << 32) | words[j];
                words[j] = (ULONG)(value / 10);
                carry = value % 10;
            }
            remainder = (ULONG)carry;
        }
        if (remainder > 5 || (remainder == 5 && (sticky || (words[0] & 1)))) {
            if (++words[0] == 0 && ++words[1] == 0) ++words[2];
        }
    }
    ULONGLONG magnitude = ((ULONGLONG)words[1] << 32) | words[0];
    if (words[2] || magnitude > (decimal->sign ? 0x8000000000000000ull : 0x7fffffffffffffffull))
        return DISP_E_OVERFLOW;
    result->int64 = (LONGLONG)(decimal->sign ? 0ull - magnitude : magnitude);
    return S_OK;
}
