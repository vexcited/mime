/* values of the property system as the boxed values of the windows runtime, and back. */
#include "propsys_private.h"
#include <mime/win32/roapi.h>
#include <mime/win32/toast.h>

#define E_BOUNDS_ ((HRESULT)0x8000000BL)

MIME_DEFINE_GUID(IID_PsPropertyValueStatics, 0x629bdbc8, 0xd932, 0x4ff4, 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58);
MIME_DEFINE_GUID(IID_PsPropertyValue, 0x4bd682dd, 0x7554, 0x40e9, 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62);

static HRESULT array_of(IPropertyValueStatics *s, const PROPVARIANT *v, IInspectable **out)
{
    UINT32 n = v->cac.cElems;
    void *p = v->cac.pElems;
    switch (v->vt & VT_TYPEMASK) {
    case VT_UI1: return s->lpVtbl->CreateUInt8Array(s, n, p, out);
    case VT_I2: return s->lpVtbl->CreateInt16Array(s, n, p, out);
    case VT_UI2: return s->lpVtbl->CreateUInt16Array(s, n, p, out);
    case VT_I4: return s->lpVtbl->CreateInt32Array(s, n, p, out);
    case VT_UI4: return s->lpVtbl->CreateUInt32Array(s, n, p, out);
    case VT_I8: return s->lpVtbl->CreateInt64Array(s, n, p, out);
    case VT_UI8: return s->lpVtbl->CreateUInt64Array(s, n, p, out);
    case VT_R4: return s->lpVtbl->CreateSingleArray(s, n, p, out);
    case VT_R8: return s->lpVtbl->CreateDoubleArray(s, n, p, out);
    case VT_CLSID: return s->lpVtbl->CreateGuidArray(s, n, p, out);
    case VT_BOOL: {
        BOOLEAN *b = n ? PsAllocZero(n) : nullptr;
        HRESULT hr = n && !b ? E_OUTOFMEMORY : S_OK;
        for (UINT32 i = 0; SUCCEEDED(hr) && i < n; i++)
            b[i] = ((VARIANT_BOOL *)p)[i] != 0;
        if (SUCCEEDED(hr))
            hr = s->lpVtbl->CreateBooleanArray(s, n, b, out);
        HeapFree(GetProcessHeap(), 0, b);
        return hr;
    }
    case VT_LPWSTR: {
        HSTRING *h = n ? PsAllocZero(n * sizeof(HSTRING)) : nullptr;
        HRESULT hr = n && !h ? E_OUTOFMEMORY : S_OK;
        for (UINT32 i = 0; SUCCEEDED(hr) && i < n; i++) {
            LPCWSTR text = v->calpwstr.pElems[i] ? v->calpwstr.pElems[i] : L"";
            hr = WindowsCreateString(text, (UINT32)wcslen(text), &h[i]);
        }
        if (SUCCEEDED(hr))
            hr = s->lpVtbl->CreateStringArray(s, n, h, out);
        for (UINT32 i = 0; h && i < n; i++)
            WindowsDeleteString(h[i]);
        HeapFree(GetProcessHeap(), 0, h);
        return hr;
    }
    }
    return E_INVALIDARG;
}

HRESULT WINAPI PropVariantToWinRTPropertyValue(const PROPVARIANT *value, REFIID riid, void **object)
{
    HSTRING_HEADER header;
    HSTRING name;
    IPropertyValueStatics *statics = nullptr;
    *object = nullptr;
    HRESULT hr = WindowsCreateStringReference(L"Windows.Foundation.PropertyValue", 32, &header, &name);
    if (SUCCEEDED(hr))
        hr = RoGetActivationFactory(name, &IID_PsPropertyValueStatics, (void **)&statics);
    if (FAILED(hr))
        return hr;
    IInspectable *box = nullptr;
    VARTYPE vt = value->vt;
    if (vt & VT_VECTOR)
        hr = array_of(statics, value, &box);
    else
        switch (vt) {
        case VT_EMPTY: case VT_NULL: hr = statics->lpVtbl->CreateEmpty(statics, &box); break;
        case VT_UI1: hr = statics->lpVtbl->CreateUInt8(statics, value->bVal, &box); break;
        case VT_I2: hr = statics->lpVtbl->CreateInt16(statics, value->iVal, &box); break;
        case VT_UI2: hr = statics->lpVtbl->CreateUInt16(statics, value->uiVal, &box); break;
        case VT_I4: hr = statics->lpVtbl->CreateInt32(statics, value->lVal, &box); break;
        case VT_UI4: hr = statics->lpVtbl->CreateUInt32(statics, value->ulVal, &box); break;
        case VT_I8: hr = statics->lpVtbl->CreateInt64(statics, value->hVal.QuadPart, &box); break;
        case VT_UI8: hr = statics->lpVtbl->CreateUInt64(statics, value->uhVal.QuadPart, &box); break;
        case VT_R4: hr = statics->lpVtbl->CreateSingle(statics, value->fltVal, &box); break;
        case VT_R8: hr = statics->lpVtbl->CreateDouble(statics, value->dblVal, &box); break;
        case VT_BOOL: hr = statics->lpVtbl->CreateBoolean(statics, value->boolVal != 0, &box); break;
        case VT_CLSID: hr = value->puuid ? statics->lpVtbl->CreateGuid(statics, *value->puuid, &box) : E_INVALIDARG; break;
        case VT_FILETIME: {
            DateTime time;
            if ((INT64)value->uhVal.QuadPart < 0)
                hr = E_BOUNDS_;
            else {
                time.UniversalTime = (INT64)value->uhVal.QuadPart;
                hr = statics->lpVtbl->CreateDateTime(statics, time, &box);
            }
            break;
        }
        case VT_LPWSTR: case VT_BSTR: {
            LPCWSTR text = vt == VT_BSTR ? value->bstrVal : value->pwszVal;
            HSTRING string;
            if (!text)
                text = L"";
            hr = WindowsCreateString(text, (UINT32)wcslen(text), &string);
            if (SUCCEEDED(hr)) {
                hr = statics->lpVtbl->CreateString(statics, string, &box);
                WindowsDeleteString(string);
            }
            break;
        }
        case VT_UNKNOWN: {
            IInspectable *inner = nullptr;
            hr = value->punkVal ? value->punkVal->lpVtbl->QueryInterface(value->punkVal, &IID_IInspectable, (void **)&inner) :
                                  E_INVALIDARG;
            if (SUCCEEDED(hr)) {
                hr = statics->lpVtbl->CreateInspectable(statics, inner, &box);
                inner->lpVtbl->Release(inner);
            }
            break;
        }
        default:
            hr = E_INVALIDARG;
        }
    statics->lpVtbl->Release(statics);
    if (SUCCEEDED(hr)) {
        hr = box->lpVtbl->QueryInterface(box, riid, object);
        box->lpVtbl->Release(box);
    }
    return hr;
}

static HRESULT vector_from(IPropertyValue *pv, PropertyType type, PROPVARIANT *out)
{
    UINT32 n = 0;
    void *items = nullptr;
    HRESULT hr;
    VARTYPE element;
    switch ((int)type) {
    case PropertyType_UInt8 + 1024: hr = pv->lpVtbl->GetUInt8Array(pv, &n, &items); element = VT_UI1; break;
    case PropertyType_Int16 + 1024: hr = pv->lpVtbl->GetInt16Array(pv, &n, &items); element = VT_I2; break;
    case PropertyType_UInt16 + 1024: hr = pv->lpVtbl->GetUInt16Array(pv, &n, &items); element = VT_UI2; break;
    case PropertyType_Int32 + 1024: hr = pv->lpVtbl->GetInt32Array(pv, &n, &items); element = VT_I4; break;
    case PropertyType_UInt32 + 1024: hr = pv->lpVtbl->GetUInt32Array(pv, &n, &items); element = VT_UI4; break;
    case PropertyType_Int64 + 1024: hr = pv->lpVtbl->GetInt64Array(pv, &n, &items); element = VT_I8; break;
    case PropertyType_UInt64 + 1024: hr = pv->lpVtbl->GetUInt64Array(pv, &n, &items); element = VT_UI8; break;
    case PropertyType_Single + 1024: hr = pv->lpVtbl->GetSingleArray(pv, &n, &items); element = VT_R4; break;
    case PropertyType_Double + 1024: hr = pv->lpVtbl->GetDoubleArray(pv, &n, &items); element = VT_R8; break;
    case PropertyType_Guid + 1024: hr = pv->lpVtbl->GetGuidArray(pv, &n, &items); element = VT_CLSID; break;
    default: return E_UNEXPECTED;
    }
    if (FAILED(hr))
        return hr;
    out->vt = (VARTYPE)(element | VT_VECTOR);
    out->cac.cElems = n;
    out->cac.pElems = items;
    return S_OK;
}

HRESULT WINAPI WinRTPropertyValueToPropVariant(IUnknown *unknown, PROPVARIANT *value)
{
    memset(value, 0, sizeof(*value));
    if (!unknown)
        return S_OK;
    IPropertyValue *pv = nullptr;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, &IID_PsPropertyValue, (void **)&pv);
    if (FAILED(hr))
        return hr;
    PropertyType type = PropertyType_Empty;
    hr = pv->lpVtbl->get_Type(pv, &type);
    if (SUCCEEDED(hr)) {
        switch ((int)type) {
        case PropertyType_Empty: break;
        case PropertyType_UInt8: value->vt = VT_UI1; hr = pv->lpVtbl->GetUInt8(pv, &value->bVal); break;
        case PropertyType_Int16: value->vt = VT_I2; hr = pv->lpVtbl->GetInt16(pv, &value->iVal); break;
        case PropertyType_UInt16: value->vt = VT_UI2; hr = pv->lpVtbl->GetUInt16(pv, &value->uiVal); break;
        case PropertyType_Int32: value->vt = VT_I4; hr = pv->lpVtbl->GetInt32(pv, (INT32 *)&value->lVal); break;
        case PropertyType_UInt32: value->vt = VT_UI4; hr = pv->lpVtbl->GetUInt32(pv, (UINT32 *)&value->ulVal); break;
        case PropertyType_Int64: value->vt = VT_I8; hr = pv->lpVtbl->GetInt64(pv, &value->hVal.QuadPart); break;
        case PropertyType_UInt64: value->vt = VT_UI8; hr = pv->lpVtbl->GetUInt64(pv, &value->uhVal.QuadPart); break;
        case PropertyType_Single: value->vt = VT_R4; hr = pv->lpVtbl->GetSingle(pv, &value->fltVal); break;
        case PropertyType_Double: value->vt = VT_R8; hr = pv->lpVtbl->GetDouble(pv, &value->dblVal); break;
        case PropertyType_Boolean: {
            BOOLEAN b = 0;
            hr = pv->lpVtbl->GetBoolean(pv, &b);
            value->vt = VT_BOOL;
            value->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        }
        case PropertyType_Char16:
            value->vt = VT_UI2;
            hr = pv->lpVtbl->GetChar16(pv, &value->uiVal);
            break;
        case PropertyType_String: {
            HSTRING string = nullptr;
            hr = pv->lpVtbl->GetString(pv, &string);
            if (SUCCEEDED(hr)) {
                UINT32 length;
                LPCWSTR raw = WindowsGetStringRawBuffer(string, &length);
                value->pwszVal = CoTaskMemAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
                if (!value->pwszVal)
                    hr = E_OUTOFMEMORY;
                else {
                    memcpy(value->pwszVal, raw, length * sizeof(WCHAR));
                    value->pwszVal[length] = 0;
                    value->vt = VT_LPWSTR;
                }
                WindowsDeleteString(string);
            }
            break;
        }
        case PropertyType_DateTime: {
            DateTime time;
            hr = pv->lpVtbl->GetDateTime(pv, &time);
            value->vt = VT_FILETIME;
            value->uhVal.QuadPart = (ULONGLONG)time.UniversalTime;
            break;
        }
        case PropertyType_Guid: {
            GUID guid;
            hr = pv->lpVtbl->GetGuid(pv, &guid);
            if (SUCCEEDED(hr)) {
                value->puuid = CoTaskMemAlloc(sizeof(GUID));
                if (!value->puuid)
                    hr = E_OUTOFMEMORY;
                else {
                    *value->puuid = guid;
                    value->vt = VT_CLSID;
                }
            }
            break;
        }
        default:
            hr = vector_from(pv, type, value);
        }
    }
    pv->lpVtbl->Release(pv);
    if (FAILED(hr)) {
        PropVariantClear(value);
        memset(value, 0, sizeof(*value));
    }
    return hr;
}
