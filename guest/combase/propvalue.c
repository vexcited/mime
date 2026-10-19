/*
 * Windows.Foundation.PropertyValue: the boxed values of the runtime. a box holds one scalar, a string, a date or
 * nothing, and answers IPropertyValue; the numbers convert to one another where the value fits, as the system's
 * boxes do. the arrays, points, sizes and rectangles are not made.
 */
#include "xmldom.h"

const WCHAR CbPropertyValueClass[] = L"Windows.Foundation.PropertyValue";
static const WCHAR g_box_class[] = L"Windows.Foundation.IReference";

#define TYPE_MISMATCH ((HRESULT)0x80028CA0)
#define OVERFLOW_ERR ((HRESULT)0x8002000A)

typedef struct BOX {
    WRT_OBJECT Base;
    PropertyType Type;
    union {
        INT64 Signed;
        UINT64 Unsigned;
        double Real;
        GUID Guid;
        WCHAR Char;
        BOOLEAN Boolean;
        HSTRING String;
        IInspectable *Object;
        DateTime Date;
        TimeSpan Span;
    } Value;
} BOX;

WRT_INSPECTABLE_THUNKS(IPropertyValue)
WRT_INSPECTABLE_THUNKS(IReferenceDateTime)

static BOX *box_of(void *iface)
{
    return (BOX *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE pv_get_type(IPropertyValue *This, PropertyType *value)
{
    *value = box_of(This)->Type;
    return S_OK;
}

static BOOL numeric(PropertyType t)
{
    return (t >= PropertyType_UInt8 && t <= PropertyType_Double);
}

static HRESULT STDMETHODCALLTYPE pv_is_numeric(IPropertyValue *This, BOOLEAN *value)
{
    PropertyType t = box_of(This)->Type;
    *value = numeric(t) && t != PropertyType_Char16;
    return S_OK;
}

/* an integer or a real, whichever the box holds, as a number to range check. */
static HRESULT as_number(BOX *b, BOOL *is_real, INT64 *s, UINT64 *u, double *r)
{
    *is_real = FALSE;
    *s = 0;
    *u = 0;
    *r = 0;
    switch (b->Type) {
    case PropertyType_UInt8:
    case PropertyType_UInt16:
    case PropertyType_UInt32:
    case PropertyType_UInt64:
    case PropertyType_Char16:
        *u = b->Value.Unsigned;
        *s = b->Value.Unsigned > (UINT64)INT64_MAX ? INT64_MAX : (INT64)b->Value.Unsigned;
        *r = (double)b->Value.Unsigned;
        return S_OK;
    case PropertyType_Int16:
    case PropertyType_Int32:
    case PropertyType_Int64:
        *s = b->Value.Signed;
        *u = b->Value.Signed < 0 ? 0 : (UINT64)b->Value.Signed;
        *r = (double)b->Value.Signed;
        return S_OK;
    case PropertyType_Single:
    case PropertyType_Double:
        *is_real = TRUE;
        *r = b->Value.Real;
        return S_OK;
    default:
        return TYPE_MISMATCH;
    }
}

static HRESULT get_signed(BOX *b, INT64 low, INT64 high, INT64 *out)
{
    BOOL real;
    INT64 s;
    UINT64 u;
    double r;
    HRESULT hr = as_number(b, &real, &s, &u, &r);
    if (FAILED(hr))
        return hr;
    if (real) {
        if (r < (double)low || r > (double)high)
            return OVERFLOW_ERR;
        *out = (INT64)r;
        return S_OK;
    }
    if (b->Type >= PropertyType_UInt8 && b->Type <= PropertyType_UInt64 ? u > (UINT64)high : (s < low || s > high))
        return OVERFLOW_ERR;
    *out = s;
    return S_OK;
}

static HRESULT get_unsigned(BOX *b, UINT64 high, UINT64 *out)
{
    BOOL real;
    INT64 s;
    UINT64 u;
    double r;
    HRESULT hr = as_number(b, &real, &s, &u, &r);
    if (FAILED(hr))
        return hr;
    if (real) {
        if (r < 0 || r > (double)high)
            return OVERFLOW_ERR;
        *out = (UINT64)r;
        return S_OK;
    }
    if ((b->Type >= PropertyType_Int16 && b->Type <= PropertyType_Int64 && b->Value.Signed < 0) || u > high)
        return OVERFLOW_ERR;
    *out = u;
    return S_OK;
}

#define GET_SIGNED(name, type, low, high)                                                                              \
    static HRESULT STDMETHODCALLTYPE pv_get_##name(IPropertyValue *This, type *value)                                  \
    {                                                                                                                  \
        INT64 v;                                                                                                       \
        HRESULT hr = get_signed(box_of(This), low, high, &v);                                                          \
        if (SUCCEEDED(hr))                                                                                             \
            *value = (type)v;                                                                                          \
        return hr;                                                                                                     \
    }
#define GET_UNSIGNED(name, type, high)                                                                                 \
    static HRESULT STDMETHODCALLTYPE pv_get_##name(IPropertyValue *This, type *value)                                  \
    {                                                                                                                  \
        UINT64 v;                                                                                                      \
        HRESULT hr = get_unsigned(box_of(This), high, &v);                                                             \
        if (SUCCEEDED(hr))                                                                                             \
            *value = (type)v;                                                                                          \
        return hr;                                                                                                     \
    }

GET_UNSIGNED(uint8, BYTE, 0xFF)
GET_SIGNED(int16, INT16, INT16_MIN, INT16_MAX)
GET_UNSIGNED(uint16, UINT16, 0xFFFF)
GET_SIGNED(int32, INT32, INT32_MIN, INT32_MAX)
GET_UNSIGNED(uint32, UINT32, 0xFFFFFFFFull)

static HRESULT STDMETHODCALLTYPE pv_get_int64(IPropertyValue *This, INT64 *value)
{
    return get_signed(box_of(This), INT64_MIN, INT64_MAX, value);
}

static HRESULT STDMETHODCALLTYPE pv_get_uint64(IPropertyValue *This, UINT64 *value)
{
    return get_unsigned(box_of(This), UINT64_MAX, value);
}

static HRESULT STDMETHODCALLTYPE pv_get_single(IPropertyValue *This, float *value)
{
    BOOL real;
    INT64 s;
    UINT64 u;
    double r;
    HRESULT hr = as_number(box_of(This), &real, &s, &u, &r);
    if (SUCCEEDED(hr))
        *value = (float)r;
    return hr;
}

static HRESULT STDMETHODCALLTYPE pv_get_double(IPropertyValue *This, double *value)
{
    BOOL real;
    INT64 s;
    UINT64 u;
    double r;
    HRESULT hr = as_number(box_of(This), &real, &s, &u, &r);
    if (SUCCEEDED(hr))
        *value = r;
    return hr;
}

static HRESULT STDMETHODCALLTYPE pv_get_char(IPropertyValue *This, WCHAR *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_Char16)
        return TYPE_MISMATCH;
    *value = b->Value.Char;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_boolean(IPropertyValue *This, BOOLEAN *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_Boolean)
        return TYPE_MISMATCH;
    *value = b->Value.Boolean;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_string(IPropertyValue *This, HSTRING *value)
{
    BOX *b = box_of(This);
    *value = nullptr;
    if (b->Type != PropertyType_String)
        return TYPE_MISMATCH;
    return WindowsDuplicateString(b->Value.String, value);
}

static HRESULT STDMETHODCALLTYPE pv_get_guid(IPropertyValue *This, GUID *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_Guid)
        return TYPE_MISMATCH;
    *value = b->Value.Guid;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_datetime(IPropertyValue *This, DateTime *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_DateTime)
        return TYPE_MISMATCH;
    *value = b->Value.Date;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_timespan(IPropertyValue *This, TimeSpan *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_TimeSpan)
        return TYPE_MISMATCH;
    *value = b->Value.Span;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_no_value(IPropertyValue *This, void *value)
{
    return TYPE_MISMATCH;
}

static HRESULT STDMETHODCALLTYPE pv_no_array(IPropertyValue *This, UINT32 *size, void *value)
{
    *size = 0;
    return TYPE_MISMATCH;
}

static const IPropertyValueVtbl pv_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IPropertyValue), pv_get_type, pv_is_numeric, pv_get_uint8, pv_get_int16, pv_get_uint16,
    pv_get_int32, pv_get_uint32, pv_get_int64, pv_get_uint64, pv_get_single, pv_get_double, pv_get_char,
    pv_get_boolean, pv_get_string, pv_get_guid, pv_get_datetime, pv_get_timespan, pv_no_value, pv_no_value,
    pv_no_value, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array,
    pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array, pv_no_array,
    pv_no_array, pv_no_array, pv_no_array, pv_no_array};

static HRESULT STDMETHODCALLTYPE ref_get_value(IReferenceDateTime *This, DateTime *value)
{
    BOX *b = box_of(This);
    if (b->Type != PropertyType_DateTime)
        return TYPE_MISMATCH;
    *value = b->Value.Date;
    return S_OK;
}

static const IReferenceDateTimeVtbl ref_vtbl = {WRT_INSPECTABLE_ENTRIES(IReferenceDateTime), ref_get_value};

static void box_destroy(WRT_OBJECT *object)
{
    BOX *b = (BOX *)object;
    if (b->Type == PropertyType_String)
        WindowsDeleteString(b->Value.String);
    else if (b->Type == PropertyType_Inspectable && b->Value.Object)
        b->Value.Object->lpVtbl->Release(b->Value.Object);
    CbFree(b);
}

static const WRT_CLASS box_class = {g_box_class, 1, {&IID_IPropertyValue}, 0, box_destroy};
static const WRT_CLASS date_box_class = {g_box_class, 2, {&IID_IPropertyValue, &IID_IReferenceDateTime}, 0, box_destroy};

static BOX *new_box(PropertyType type)
{
    BOX *b = CbAlloc(sizeof(*b));
    if (!b)
        return nullptr;
    memset(b, 0, sizeof(*b));
    b->Type = type;
    const void *vtables[2] = {&pv_vtbl, &ref_vtbl};
    WrtInit(&b->Base, type == PropertyType_DateTime ? &date_box_class : &box_class, vtables);
    return b;
}

HRESULT CbBoxString(const WCHAR *text, IInspectable **box)
{
    BOX *b = new_box(PropertyType_String);
    *box = nullptr;
    if (!b)
        return E_OUTOFMEMORY;
    HRESULT hr = WindowsCreateString(text, (UINT32)wcslen(text), &b->Value.String);
    if (FAILED(hr)) {
        CbFree(b);
        return hr;
    }
    *box = (IInspectable *)&b->Base.Ifaces[0];
    return S_OK;
}

HRESULT CbUnboxString(IInspectable *box, WCHAR **text)
{
    *text = nullptr;
    if (!box)
        return E_INVALIDARG;
    IPropertyValue *value;
    HRESULT hr = box->lpVtbl->QueryInterface(box, &IID_IPropertyValue, (void **)&value);
    if (FAILED(hr))
        return hr;
    HSTRING string;
    hr = value->lpVtbl->GetString(value, &string);
    value->lpVtbl->Release(value);
    if (FAILED(hr))
        return hr;
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(string, &length);
    WCHAR *out = CbAlloc((length + 1) * sizeof(WCHAR));
    if (out) {
        memcpy(out, raw, length * sizeof(WCHAR));
        out[length] = 0;
        *text = out;
    }
    WindowsDeleteString(string);
    return out ? S_OK : E_OUTOFMEMORY;
}

HRESULT CbBoxDateTime(DateTime time, IReferenceDateTime **box)
{
    BOX *b = new_box(PropertyType_DateTime);
    *box = nullptr;
    if (!b)
        return E_OUTOFMEMORY;
    b->Value.Date = time;
    *box = (IReferenceDateTime *)&b->Base.Ifaces[1];
    return S_OK;
}

/* ------------------------------------------------------------------ the statics */

WRT_INSPECTABLE_THUNKS(IPropertyValueStatics)

static HRESULT made(BOX *b, IInspectable **out)
{
    *out = nullptr;
    if (!b)
        return E_OUTOFMEMORY;
    *out = (IInspectable *)&b->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE st_empty(IPropertyValueStatics *This, IInspectable **value)
{
    return made(new_box(PropertyType_Empty), value);
}

#define CREATE_UNSIGNED(name, type, kind)                                                                              \
    static HRESULT STDMETHODCALLTYPE st_##name(IPropertyValueStatics *This, type v, IInspectable **value)              \
    {                                                                                                                  \
        BOX *b = new_box(kind);                                                                                        \
        if (b)                                                                                                         \
            b->Value.Unsigned = (UINT64)v;                                                                             \
        return made(b, value);                                                                                         \
    }
#define CREATE_SIGNED(name, type, kind)                                                                                \
    static HRESULT STDMETHODCALLTYPE st_##name(IPropertyValueStatics *This, type v, IInspectable **value)              \
    {                                                                                                                  \
        BOX *b = new_box(kind);                                                                                        \
        if (b)                                                                                                         \
            b->Value.Signed = (INT64)v;                                                                                \
        return made(b, value);                                                                                         \
    }

CREATE_UNSIGNED(uint8, BYTE, PropertyType_UInt8)
CREATE_SIGNED(int16, INT16, PropertyType_Int16)
CREATE_UNSIGNED(uint16, UINT16, PropertyType_UInt16)
CREATE_SIGNED(int32, INT32, PropertyType_Int32)
CREATE_UNSIGNED(uint32, UINT32, PropertyType_UInt32)
CREATE_SIGNED(int64, INT64, PropertyType_Int64)
CREATE_UNSIGNED(uint64, UINT64, PropertyType_UInt64)
CREATE_UNSIGNED(char16, WCHAR, PropertyType_Char16)

static HRESULT STDMETHODCALLTYPE st_single(IPropertyValueStatics *This, float v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_Single);
    if (b)
        b->Value.Real = v;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_double(IPropertyValueStatics *This, double v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_Double);
    if (b)
        b->Value.Real = v;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_boolean(IPropertyValueStatics *This, BOOLEAN v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_Boolean);
    if (b)
        b->Value.Boolean = v != 0;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_string(IPropertyValueStatics *This, HSTRING v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_String);
    *value = nullptr;
    if (!b)
        return E_OUTOFMEMORY;
    HRESULT hr = WindowsDuplicateString(v, &b->Value.String);
    if (FAILED(hr)) {
        CbFree(b);
        return hr;
    }
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_inspectable(IPropertyValueStatics *This, IInspectable *v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_Inspectable);
    if (b) {
        b->Value.Object = v;
        if (v)
            v->lpVtbl->AddRef(v);
    }
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_guid(IPropertyValueStatics *This, GUID v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_Guid);
    if (b)
        b->Value.Guid = v;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_datetime(IPropertyValueStatics *This, DateTime v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_DateTime);
    if (b)
        b->Value.Date = v;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_timespan(IPropertyValueStatics *This, TimeSpan v, IInspectable **value)
{
    BOX *b = new_box(PropertyType_TimeSpan);
    if (b)
        b->Value.Span = v;
    return made(b, value);
}

static HRESULT STDMETHODCALLTYPE st_geometry2(IPropertyValueStatics *This, double a, double b, IInspectable **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE st_rect(IPropertyValueStatics *This, double a, double b, double c, double d,
                                         IInspectable **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE st_array(IPropertyValueStatics *This, UINT32 n, void *v, IInspectable **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static const IPropertyValueStaticsVtbl statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IPropertyValueStatics), st_empty, st_uint8, st_int16, st_uint16, st_int32, st_uint32,
    st_int64, st_uint64, st_single, st_double, st_char16, st_boolean, st_string, st_inspectable, st_guid, st_datetime,
    st_timespan, st_geometry2, st_geometry2, st_rect, st_array, st_array, st_array, st_array, st_array, st_array,
    st_array, st_array, st_array, st_array, st_array, st_array, st_array, st_array, st_array, st_array, st_array,
    st_array, st_array};

static void statics_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS statics_class = {CbPropertyValueClass, 1, {&IID_IPropertyValueStatics}, 0, statics_destroy};
static WRT_OBJECT g_statics;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[1] = {&statics_vtbl};
    WrtInit(&g_statics, &statics_class, vtables);
    return TRUE;
}

HRESULT CbPropertyValueFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_statics.Ifaces[0], riid, factory);
}
