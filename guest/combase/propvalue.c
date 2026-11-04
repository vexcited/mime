/*
 * Windows.Foundation.PropertyValue: a box holds one scalar, a string, a date or nothing behind IPropertyValue and
 * the IReference of its type. like the system's boxes, numbers convert to each other where the value fits. arrays,
 * points, sizes and rectangles are not made.
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

/* reads the box as an integer or a real, for range checks. */
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

/* IReference<T> of the type in the box, which writes the value as its type is laid out. */
static HRESULT STDMETHODCALLTYPE ref_get_value(IReferenceDateTime *This, DateTime *out)
{
    BOX *b = box_of(This);
    void *value = out;
    if (!value)
        return E_POINTER;
    switch (b->Type) {
    case PropertyType_UInt8:
        *(BYTE *)value = (BYTE)b->Value.Unsigned;
        break;
    case PropertyType_Int16:
        *(INT16 *)value = (INT16)b->Value.Signed;
        break;
    case PropertyType_UInt16:
    case PropertyType_Char16:
        *(UINT16 *)value = (UINT16)b->Value.Unsigned;
        break;
    case PropertyType_Int32:
        *(INT32 *)value = (INT32)b->Value.Signed;
        break;
    case PropertyType_UInt32:
        *(UINT32 *)value = (UINT32)b->Value.Unsigned;
        break;
    case PropertyType_Int64:
        *(INT64 *)value = b->Value.Signed;
        break;
    case PropertyType_UInt64:
        *(UINT64 *)value = b->Value.Unsigned;
        break;
    case PropertyType_Single:
        *(float *)value = (float)b->Value.Real;
        break;
    case PropertyType_Double:
        *(double *)value = b->Value.Real;
        break;
    case PropertyType_Boolean:
        *(BOOLEAN *)value = b->Value.Boolean;
        break;
    case PropertyType_String:
        return WindowsDuplicateString(b->Value.String, (HSTRING *)value);
    case PropertyType_Guid:
        *(GUID *)value = b->Value.Guid;
        break;
    case PropertyType_DateTime:
        *(DateTime *)value = b->Value.Date;
        break;
    case PropertyType_TimeSpan:
        *(TimeSpan *)value = b->Value.Span;
        break;
    default:
        return TYPE_MISMATCH;
    }
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

MIME_DEFINE_GUID(IID_IReferenceUInt8, 0xE5198CC8, 0x2873, 0x55F5, 0xB0, 0xA1, 0x84, 0xFF, 0x9E, 0x4A, 0xAD, 0x62);
MIME_DEFINE_GUID(IID_IReferenceInt16, 0x6EC9E41B, 0x6709, 0x5647, 0x99, 0x18, 0xA1, 0x27, 0x01, 0x10, 0xFC, 0x4E);
MIME_DEFINE_GUID(IID_IReferenceUInt16, 0x5AB7D2C3, 0x6B62, 0x5E71, 0xA4, 0xB6, 0x2D, 0x49, 0xC4, 0xF2, 0x38, 0xFD);
MIME_DEFINE_GUID(IID_IReferenceInt32, 0x548CEFBD, 0xBC8A, 0x5FA0, 0x8D, 0xF2, 0x95, 0x74, 0x40, 0xFC, 0x8B, 0xF4);
MIME_DEFINE_GUID(IID_IReferenceUInt32, 0x513EF3AF, 0xE784, 0x5325, 0xA9, 0x1E, 0x97, 0xC2, 0xB8, 0x11, 0x1C, 0xF3);
MIME_DEFINE_GUID(IID_IReferenceInt64, 0x4DDA9E24, 0xE69F, 0x5C6A, 0xA0, 0xA6, 0x93, 0x42, 0x73, 0x65, 0xAF, 0x2A);
MIME_DEFINE_GUID(IID_IReferenceUInt64, 0x6755E376, 0x53BB, 0x568B, 0xA1, 0x1D, 0x17, 0x23, 0x98, 0x68, 0x30, 0x9E);
MIME_DEFINE_GUID(IID_IReferenceSingle, 0x719CC2BA, 0x3E76, 0x5DEF, 0x9F, 0x1A, 0x38, 0xD8, 0x5A, 0x14, 0x5E, 0xA8);
MIME_DEFINE_GUID(IID_IReferenceDouble, 0x2F2D6C29, 0x5473, 0x5F3E, 0x92, 0xE7, 0x96, 0x57, 0x2B, 0xB9, 0x90, 0xE2);
MIME_DEFINE_GUID(IID_IReferenceChar16, 0xFB393EF3, 0xBBAC, 0x5BD5, 0x91, 0x44, 0x84, 0xF2, 0x35, 0x76, 0xF4, 0x15);
MIME_DEFINE_GUID(IID_IReferenceBoolean, 0x3C00FD60, 0x2950, 0x5939, 0xA2, 0x1A, 0x2D, 0x12, 0xC5, 0xA0, 0x1B, 0x8A);
MIME_DEFINE_GUID(IID_IReferenceString, 0xFD416DFB, 0x2A07, 0x52EB, 0xAA, 0xE3, 0xDF, 0xCE, 0x14, 0x11, 0x6C, 0x05);
MIME_DEFINE_GUID(IID_IReferenceGuid, 0x7D50F649, 0x632C, 0x51F9, 0x84, 0x9A, 0xEE, 0x49, 0x42, 0x89, 0x33, 0xEA);
MIME_DEFINE_GUID(IID_IReferenceTimeSpan, 0x604D0C4C, 0x91DE, 0x5C2A, 0x93, 0x5F, 0x36, 0x2F, 0x13, 0xEA, 0xF8, 0x00);

static const WRT_CLASS box_class = {g_box_class, 1, {&IID_IPropertyValue}, 0, box_destroy};

/* a box of a scalar is also the IReference of its type, as the boxes of windows are. */
#define REF_CLASS(type, iid) [type] = {g_box_class, 2, {&IID_IPropertyValue, &iid}, 0, box_destroy}
static const WRT_CLASS g_ref_classes[PropertyType_Guid + 1] = {
    REF_CLASS(PropertyType_UInt8, IID_IReferenceUInt8),       REF_CLASS(PropertyType_Int16, IID_IReferenceInt16),
    REF_CLASS(PropertyType_UInt16, IID_IReferenceUInt16),     REF_CLASS(PropertyType_Int32, IID_IReferenceInt32),
    REF_CLASS(PropertyType_UInt32, IID_IReferenceUInt32),     REF_CLASS(PropertyType_Int64, IID_IReferenceInt64),
    REF_CLASS(PropertyType_UInt64, IID_IReferenceUInt64),     REF_CLASS(PropertyType_Single, IID_IReferenceSingle),
    REF_CLASS(PropertyType_Double, IID_IReferenceDouble),     REF_CLASS(PropertyType_Char16, IID_IReferenceChar16),
    REF_CLASS(PropertyType_Boolean, IID_IReferenceBoolean),   REF_CLASS(PropertyType_String, IID_IReferenceString),
    REF_CLASS(PropertyType_DateTime, IID_IReferenceDateTime), REF_CLASS(PropertyType_TimeSpan, IID_IReferenceTimeSpan),
    REF_CLASS(PropertyType_Guid, IID_IReferenceGuid),
};

static BOX *new_box(PropertyType type)
{
    BOX *b = CbAlloc(sizeof(*b));
    if (!b)
        return nullptr;
    memset(b, 0, sizeof(*b));
    b->Type = type;
    const void *vtables[2] = {&pv_vtbl, &ref_vtbl};
    const WRT_CLASS *class = (UINT32)type < RTL_NUMBER_OF(g_ref_classes) && g_ref_classes[type].Count
                                 ? &g_ref_classes[type]
                                 : &box_class;
    WrtInit(&b->Base, class, vtables);
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

/* ------------------------------------------------------------------ statics */

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
