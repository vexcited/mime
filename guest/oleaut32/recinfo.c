/*
 * IRecordInfo: the in-memory layout of a type library structure, so automation can create, copy, clear and access its
 * fields. One RECINFO exists per type, looked up by guid. Strings, interfaces, variants, arrays and nested records in
 * fields are owned and need more than a byte copy.
 */
#include "oleaut32_private.h"

#define TYPE_E_UNSUPFORMAT_ ((HRESULT)0x80028019L)
#define TYPE_E_UNDEFINEDTYPE_ ((HRESULT)0x80028027L)
#define TYPE_E_INVALIDSTATE_ ((HRESULT)0x80028029L)
#define MAX_DEPTH 16

typedef struct RECINFO RECINFO;
typedef struct FT FT;

/* a resolved field type: Vt says what it owns. */
struct FT {
    VARTYPE Vt;
    /* arrays: the automation element type, and its interface or record. */
    VARTYPE ElemVt;
    GUID Iid;
    FT *Elem;
    RECINFO *Rec;
    USHORT Dims;
    SAFEARRAYBOUND *Bounds;
    /* declared as a pointer to a named interface, which is assigned differently from a plain pointer. */
    BOOL ViaPointer;
    ULONG Size, Align;
    /* handed out for fixed array fields. */
    SAFEARRAY *Descriptor;
};

typedef struct {
    FT *Type;
    WCHAR *Name;
    ULONG Offset;
} FIELD;

struct RECINFO {
    const IRecordInfoVtbl *lpVtbl;
    LONG Refs;
    GUID Guid;
    GUID LibGuid;
    USHORT Major, Minor;
    LCID Lcid;
    ULONG Size;
    USHORT Align;
    WCHAR *Name;
    ITypeInfo *Info;
    USHORT FieldCount;
    FIELD *Fields;
    ULONG *Order;
    RECINFO *Next;
};

static CRITICAL_SECTION g_lock;
static BOOL g_lock_ready;
static RECINFO *g_records;

static RECINFO *self_of(IRecordInfo *p)
{
    return (RECINFO *)p;
}

/* ------------------------------------------------------------------ field types */

static void ft_free(FT *t);
static HRESULT ft_build(ITypeInfo *scope, const TYPEDESC *td, BOOL behind_pointer, int depth, FT **out);
static ULONG stride_of(const FT *elem);

static ULONG scalar_size(VARTYPE vt)
{
    switch (vt) {
    case VT_I1:
    case VT_UI1: return 1;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: return 2;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
    case VT_HRESULT: return 4;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE: return 8;
    case VT_INT_PTR:
    case VT_UINT_PTR: return sizeof(void *);
    case VT_DECIMAL: return sizeof(DECIMAL);
    default: return 0;
    }
}

static void ft_free(FT *t)
{
    if (!t)
        return;
    ft_free(t->Elem);
    if (t->Rec)
        t->Rec->lpVtbl->Release((IRecordInfo *)t->Rec);
    AutFree(t->Bounds);
    if (t->Descriptor)
        SafeArrayDestroyDescriptor(t->Descriptor);
    AutFree(t);
}

static BOOL kind_is_dispatch(const TYPEATTR *ta)
{
    return ta->typekind == TKIND_DISPATCH || (ta->wTypeFlags & TYPEFLAG_FDUAL) != 0;
}

static HRESULT ft_scalar(VARTYPE vt, FT **out)
{
    ULONG size = scalar_size(vt);
    FT *t = AutAllocZero(sizeof(FT));
    if (!t)
        return E_OUTOFMEMORY;
    t->Vt = vt == VT_HRESULT ? VT_ERROR : vt;
    t->Size = size;
    t->Align = vt == VT_DECIMAL ? 8 : size;
    *out = t;
    return S_OK;
}

static HRESULT ft_make(VARTYPE vt, ULONG size, ULONG align, FT **out)
{
    FT *t = AutAllocZero(sizeof(FT));
    if (!t)
        return E_OUTOFMEMORY;
    t->Vt = vt;
    t->Size = size;
    t->Align = align;
    *out = t;
    return S_OK;
}

static HRESULT ft_interface(const TYPEATTR *ta, BOOL named, FT **out)
{
    HRESULT hr = ft_make(kind_is_dispatch(ta) ? VT_DISPATCH : VT_UNKNOWN, sizeof(void *),
                         sizeof(void *), out);
    if (SUCCEEDED(hr)) {
        (*out)->Iid = ta->guid;
        (*out)->ViaPointer = named;
    }
    return hr;
}

static HRESULT ft_record(ITypeInfo *info, FT **out)
{
    IRecordInfo *record;
    HRESULT hr = GetRecordInfoFromTypeInfo(info, &record);
    if (FAILED(hr))
        return hr;
    RECINFO *r = self_of(record);
    hr = ft_make(VT_RECORD, r->Size, r->Align, out);
    if (FAILED(hr))
        record->lpVtbl->Release(record);
    else
        (*out)->Rec = r;
    return hr;
}

/* behind a pointer only an interface is accepted, enums and records must be held by value. */
static HRESULT ft_build_user(ITypeInfo *scope, HREFTYPE ref, BOOL behind_pointer, int depth, FT **out)
{
    ITypeInfo *info;
    TYPEATTR *ta;
    HRESULT hr = scope->lpVtbl->GetRefTypeInfo(scope, ref, &info);
    if (FAILED(hr))
        return hr;
    hr = info->lpVtbl->GetTypeAttr(info, &ta);
    if (FAILED(hr)) {
        info->lpVtbl->Release(info);
        return hr;
    }
    switch (ta->typekind) {
    case TKIND_ENUM:
        hr = behind_pointer ? TYPE_E_UNSUPFORMAT_ : ft_make(VT_I4, 4, 4, out);
        break;
    case TKIND_RECORD:
        hr = behind_pointer ? TYPE_E_UNSUPFORMAT_ : ft_record(info, out);
        break;
    case TKIND_ALIAS:
        hr = ft_build(info, &ta->tdescAlias, behind_pointer, depth + 1, out);
        break;
    case TKIND_INTERFACE:
    case TKIND_DISPATCH:
        hr = ft_interface(ta, behind_pointer, out);
        break;
    default:
        hr = TYPE_E_UNSUPFORMAT_;
        break;
    }
    info->lpVtbl->ReleaseTypeAttr(info, ta);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT elem_vt(FT *e, VARTYPE *vt)
{
    switch (e->Vt) {
    case VT_SAFEARRAY:
    case VT_CARRAY: return TYPE_E_UNSUPFORMAT_;
    default: *vt = e->Vt; return S_OK;
    }
}

static HRESULT ft_safearray(ITypeInfo *scope, const TYPEDESC *td, int depth, FT **out)
{
    FT *elem = nullptr;
    VARTYPE vt = VT_EMPTY;
    HRESULT hr = ft_build(scope, td->lptdesc, FALSE, depth + 1, &elem);
    if (SUCCEEDED(hr))
        hr = elem_vt(elem, &vt);
    if (SUCCEEDED(hr))
        hr = ft_make(VT_SAFEARRAY, sizeof(void *), sizeof(void *), out);
    if (FAILED(hr)) {
        if (elem)
            ft_free(elem);
        return hr;
    }
    (*out)->Elem = elem;
    (*out)->ElemVt = vt;
    return S_OK;
}

static HRESULT ft_fixed_array(ITypeInfo *scope, const TYPEDESC *td, int depth, FT **out)
{
    const ARRAYDESC *desc = td->lpadesc;
    FT *elem = nullptr;
    VARTYPE vt = VT_EMPTY;
    HRESULT hr = ft_build(scope, &desc->tdescElem, FALSE, depth + 1, &elem);
    if (SUCCEEDED(hr))
        hr = elem_vt(elem, &vt);
    ULONG64 count = 1;
    for (USHORT i = 0; SUCCEEDED(hr) && i < desc->cDims; i++) {
        count *= desc->rgbounds[i].cElements;
        if (count > 0xFFFFFFFFull)
            hr = TYPE_E_UNSUPFORMAT_;
    }
    if (SUCCEEDED(hr) && count * stride_of(elem) > 0xFFFFFFFFull)
        hr = TYPE_E_UNSUPFORMAT_;
    if (SUCCEEDED(hr))
        hr = ft_make(VT_CARRAY, (ULONG)(count * stride_of(elem)), elem->Align, out);
    if (SUCCEEDED(hr)) {
        (*out)->Bounds = AutAlloc((desc->cDims ? desc->cDims : 1) * sizeof(SAFEARRAYBOUND));
        if (!(*out)->Bounds) {
            AutFree(*out);
            hr = E_OUTOFMEMORY;
        }
    }
    if (FAILED(hr)) {
        ft_free(elem);
        return hr;
    }
    memcpy((*out)->Bounds, desc->rgbounds, desc->cDims * sizeof(SAFEARRAYBOUND));
    (*out)->Dims = desc->cDims;
    (*out)->Elem = elem;
    (*out)->ElemVt = vt;
    return S_OK;
}

/* behind_pointer is set while resolving a pointer's target. */
static HRESULT ft_build(ITypeInfo *scope, const TYPEDESC *td, BOOL behind_pointer, int depth, FT **out)
{
    *out = nullptr;
    if (depth > MAX_DEPTH || (behind_pointer && td->vt != VT_USERDEFINED))
        return TYPE_E_UNSUPFORMAT_;
    switch (td->vt) {
    case VT_USERDEFINED:
        return ft_build_user(scope, td->hreftype, behind_pointer, depth, out);
    case VT_PTR:
        if (td->lptdesc->vt != VT_USERDEFINED)
            return TYPE_E_UNSUPFORMAT_;
        return ft_build(scope, td->lptdesc, TRUE, depth + 1, out);
    case VT_LPSTR:
    case VT_LPWSTR:
        return TYPE_E_UNSUPFORMAT_;
    case VT_BSTR:
    case VT_UNKNOWN:
    case VT_DISPATCH:
        return ft_make(td->vt, sizeof(void *), sizeof(void *), out);
    case VT_VARIANT:
        return ft_make(VT_VARIANT, sizeof(VARIANT), 8, out);
    case VT_SAFEARRAY:
        return ft_safearray(scope, td, depth, out);
    case VT_CARRAY:
        return ft_fixed_array(scope, td, depth, out);
    default:
        return ft_scalar(td->vt, out);
    }
}

/* ------------------------------------------------------------------ clearing and copying */

static ULONG stride_of(const FT *elem)
{
    ULONG size = elem->Size, align = elem->Align ? elem->Align : 1;
    if (size % align)
        size += align - size % align;
    return size;
}

static BOOL ft_owns(const FT *t)
{
    switch (t->Vt) {
    case VT_BSTR:
    case VT_UNKNOWN:
    case VT_DISPATCH:
    case VT_VARIANT:
    case VT_SAFEARRAY:
    case VT_RECORD: return TRUE;
    case VT_CARRAY: return ft_owns(t->Elem);
    default: return FALSE;
    }
}

static HRESULT ft_clear(const FT *t, void *p)
{
    switch (t->Vt) {
    case VT_BSTR:
        SysFreeString(*(BSTR *)p);
        *(BSTR *)p = nullptr;
        return S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
        AutRelease(*(IUnknown **)p);
        *(IUnknown **)p = nullptr;
        return S_OK;
    case VT_VARIANT:
        return VariantClear(p);
    case VT_SAFEARRAY: {
        SAFEARRAY *array = *(SAFEARRAY **)p;
        if (!array)
            return S_OK;
        HRESULT hr = SafeArrayDestroy(array);
        if (SUCCEEDED(hr))
            *(SAFEARRAY **)p = nullptr;
        return hr;
    }
    case VT_RECORD:
        return t->Rec->lpVtbl->RecordClear((IRecordInfo *)t->Rec, p);
    case VT_CARRAY: {
        if (!ft_owns(t->Elem))
            return S_OK;
        ULONG stride = stride_of(t->Elem);
        ULONG count = stride ? t->Size / stride : 0;
        HRESULT hr = S_OK;
        for (ULONG i = 0; i < count && SUCCEEDED(hr); i++)
            hr = ft_clear(t->Elem, (BYTE *)p + (SIZE_T)i * stride);
        return hr;
    }
    default:
        return S_OK;
    }
}

/* copies into empty storage, undoing partial work if a part fails. */
static HRESULT ft_copy(const FT *t, const void *from, void *to)
{
    switch (t->Vt) {
    case VT_BSTR: {
        BSTR source = *(BSTR const *)from;
        BSTR copy = nullptr;
        if (source) {
            copy = SysAllocStringByteLen((LPCSTR)source, SysStringByteLen(source));
            if (!copy)
                return E_OUTOFMEMORY;
        }
        *(BSTR *)to = copy;
        return S_OK;
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
        IUnknown *source = *(IUnknown *const *)from;
        AutAddRef(source);
        *(IUnknown **)to = source;
        return S_OK;
    }
    case VT_VARIANT:
        ((VARIANT *)to)->vt = VT_EMPTY;
        return VariantCopy(to, from);
    case VT_SAFEARRAY: {
        SAFEARRAY *source = *(SAFEARRAY *const *)from;
        *(SAFEARRAY **)to = nullptr;
        return source ? SafeArrayCopy(source, (SAFEARRAY **)to) : S_OK;
    }
    case VT_RECORD:
        return t->Rec->lpVtbl->RecordCopy((IRecordInfo *)t->Rec, (void *)from, to);
    case VT_CARRAY: {
        if (!ft_owns(t->Elem)) {
            memcpy(to, from, t->Size);
            return S_OK;
        }
        ULONG stride = stride_of(t->Elem);
        ULONG count = stride ? t->Size / stride : 0;
        for (ULONG i = 0; i < count; i++) {
            HRESULT hr = ft_copy(t->Elem, (const BYTE *)from + (SIZE_T)i * stride, (BYTE *)to + (SIZE_T)i * stride);
            if (FAILED(hr)) {
                while (i--)
                    ft_clear(t->Elem, (BYTE *)to + (SIZE_T)i * stride);
                return hr;
            }
        }
        return S_OK;
    }
    default:
        memcpy(to, from, t->Size);
        return S_OK;
    }
}

/* ------------------------------------------------------------------ the object */

static HRESULT WINAPI ri_query(IRecordInfo *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IRecordInfo)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI ri_addref(IRecordInfo *self)
{
    return InterlockedIncrement(&self_of(self)->Refs);
}

static void rec_destroy(RECINFO *r)
{
    for (USHORT i = 0; i < r->FieldCount && r->Fields; i++) {
        ft_free(r->Fields[i].Type);
        SysFreeString(r->Fields[i].Name);
    }
    AutFree(r->Fields);
    AutFree(r->Order);
    SysFreeString(r->Name);
    AutRelease(r->Info);
    AutFree(r);
}

static ULONG WINAPI ri_release(IRecordInfo *self)
{
    RECINFO *r = self_of(self);
    /* release under the lock, or a lookup by guid could revive the record. */
    EnterCriticalSection(&g_lock);
    LONG refs = InterlockedDecrement(&r->Refs);
    if (!refs) {
        for (RECINFO **link = &g_records; *link; link = &(*link)->Next) {
            if (*link == r) {
                *link = r->Next;
                break;
            }
        }
    }
    LeaveCriticalSection(&g_lock);
    if (!refs)
        rec_destroy(r);
    return refs;
}

static HRESULT WINAPI ri_init(IRecordInfo *self, PVOID record)
{
    if (!record)
        return E_INVALIDARG;
    memset(record, 0, self_of(self)->Size);
    return S_OK;
}

static HRESULT WINAPI ri_clear(IRecordInfo *self, PVOID record)
{
    RECINFO *r = self_of(self);
    if (!record)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    for (USHORT i = 0; i < r->FieldCount && SUCCEEDED(hr); i++)
        hr = ft_clear(r->Fields[i].Type, (BYTE *)record + r->Fields[i].Offset);
    return hr;
}

/* copies into empty storage and clears it again if any field fails. */
static HRESULT copy_fields(RECINFO *r, const BYTE *source, BYTE *dest)
{
    for (USHORT i = 0; i < r->FieldCount; i++) {
        HRESULT hr = ft_copy(r->Fields[i].Type, source + r->Fields[i].Offset, dest + r->Fields[i].Offset);
        if (FAILED(hr)) {
            while (i--)
                ft_clear(r->Fields[i].Type, dest + r->Fields[i].Offset);
            return hr;
        }
    }
    return S_OK;
}

static HRESULT WINAPI ri_copy(IRecordInfo *self, PVOID source, PVOID dest)
{
    if (!source || !dest)
        return E_INVALIDARG;
    if (source == dest)
        return S_OK;
    HRESULT hr = ri_clear(self, dest);
    if (FAILED(hr))
        return hr;
    return copy_fields(self_of(self), source, dest);
}

static HRESULT WINAPI ri_get_guid(IRecordInfo *self, GUID *guid)
{
    if (!guid)
        return E_INVALIDARG;
    *guid = self_of(self)->Guid;
    return S_OK;
}

static HRESULT WINAPI ri_get_name(IRecordInfo *self, BSTR *name)
{
    RECINFO *r = self_of(self);
    if (!name)
        return E_INVALIDARG;
    if (!r->Name)
        return TYPE_E_INVALIDSTATE_;
    *name = SysAllocString(r->Name);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI ri_get_size(IRecordInfo *self, ULONG *size)
{
    if (!size)
        return E_INVALIDARG;
    *size = self_of(self)->Size;
    return S_OK;
}

static HRESULT WINAPI ri_get_type_info(IRecordInfo *self, ITypeInfo **info)
{
    RECINFO *r = self_of(self);
    if (!info)
        return E_INVALIDARG;
    *info = r->Info;
    AutAddRef(r->Info);
    return S_OK;
}

/* field names match case insensitively. */
static HRESULT field_index(RECINFO *r, LPCOLESTR name, ULONG *index)
{
    for (USHORT i = 0; i < r->FieldCount; i++) {
        if (!_wcsicmp(name, r->Fields[i].Name)) {
            *index = i;
            return S_OK;
        }
    }
    return TYPE_E_FIELDNOTFOUND;
}

static HRESULT make_var(RECINFO *r, FIELD *f, void *p, VARIANT *out)
{
    const FT *t = f->Type;
    switch (t->Vt) {
    case VT_BSTR: {
        BSTR s = *(BSTR *)p;
        out->vt = VT_BSTR;
        out->bstrVal = nullptr;
        if (s) {
            out->bstrVal = SysAllocString(s);
            if (!out->bstrVal) {
                out->vt = VT_EMPTY;
                return E_OUTOFMEMORY;
            }
        }
        return S_OK;
    }
    case VT_VARIANT:
        return VariantCopy(out, p);
    case VT_UNKNOWN:
    case VT_DISPATCH:
        out->vt = t->Vt;
        out->punkVal = *(IUnknown **)p;
        AutAddRef(out->punkVal);
        return S_OK;
    case VT_SAFEARRAY: {
        SAFEARRAY *array = *(SAFEARRAY **)p, *copy;
        HRESULT hr;
        if (array) {
            hr = SafeArrayCopy(array, &copy);
        } else {
            SAFEARRAYBOUND bound = {0, 0};
            copy = t->ElemVt == VT_RECORD ? nullptr : SafeArrayCreate(t->ElemVt, 1, &bound);
            hr = copy ? S_OK : E_OUTOFMEMORY;
        }
        if (FAILED(hr))
            return hr;
        out->vt = (VARTYPE)(t->ElemVt | VT_ARRAY);
        out->parray = copy;
        return S_OK;
    }
    case VT_CARRAY: {
        SAFEARRAYBOUND bounds[8];
        if (t->Dims > 8)
            return TYPE_E_UNSUPFORMAT_;
        for (USHORT i = 0; i < t->Dims; i++)
            bounds[i] = t->Bounds[i];
        IRecordInfo *elem_record = t->Elem->Vt == VT_RECORD ? (IRecordInfo *)t->Elem->Rec : nullptr;
        SAFEARRAY *array = SafeArrayCreateEx(t->ElemVt, t->Dims, bounds, elem_record);
        if (!array)
            return E_OUTOFMEMORY;
        ULONG stride = stride_of(t->Elem);
        if (array->cbElements != stride && t->ElemVt != VT_RECORD) {
            /* the struct and the safe array may space elements differently. */
            SafeArrayDestroy(array);
            return TYPE_E_UNSUPFORMAT_;
        }
        HRESULT hr = S_OK;
        BYTE *data = array->pvData;
        ULONG count = stride ? t->Size / stride : 0;
        for (ULONG i = 0; i < count && SUCCEEDED(hr); i++)
            hr = ft_copy(t->Elem, (BYTE *)p + (SIZE_T)i * stride, data + (SIZE_T)i * array->cbElements);
        if (FAILED(hr)) {
            memset(array->pvData, 0, (SIZE_T)count * array->cbElements);
            SafeArrayDestroy(array);
            return hr;
        }
        out->vt = (VARTYPE)(t->ElemVt | VT_ARRAY);
        out->parray = array;
        return S_OK;
    }
    case VT_RECORD: {
        IRecordInfo *record = (IRecordInfo *)t->Rec;
        void *copy = record->lpVtbl->RecordCreate(record);
        if (!copy)
            return E_OUTOFMEMORY;
        HRESULT hr = record->lpVtbl->RecordCopy(record, p, copy);
        if (FAILED(hr)) {
            AutFree(copy);
            return hr;
        }
        out->vt = VT_RECORD;
        out->pvRecord = copy;
        out->pRecInfo = record;
        record->lpVtbl->AddRef(record);
        return S_OK;
    }
    case VT_I1:
    case VT_UI1: out->vt = t->Vt; out->bVal = *(BYTE *)p; return S_OK;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: out->vt = t->Vt; out->uiVal = *(USHORT *)p; return S_OK;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR: out->vt = t->Vt; out->ulVal = *(ULONG *)p; return S_OK;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_INT_PTR:
    case VT_UINT_PTR: out->vt = t->Vt; out->ullVal = *(ULONGLONG *)p; return S_OK;
    case VT_DECIMAL: {
        DECIMAL d = *(DECIMAL *)p;
        out->decVal = d;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    default:
        (void)r;
        return TYPE_E_UNDEFINEDTYPE_;
    }
}

static HRESULT WINAPI ri_get_field(IRecordInfo *self, PVOID record, LPCOLESTR name, VARIANT *value)
{
    RECINFO *r = self_of(self);
    ULONG index;
    if (!record || !value || !name)
        return E_INVALIDARG;
    HRESULT hr = field_index(r, name, &index);
    if (FAILED(hr))
        return hr;
    hr = VariantClear(value);
    if (FAILED(hr))
        return hr;
    return make_var(r, &r->Fields[index], (BYTE *)record + r->Fields[index].Offset, value);
}

/* the caller points the descriptor at the record data to read or write the fixed array. */
static HRESULT array_descriptor(RECINFO *r, FIELD *f, SAFEARRAY ***slot)
{
    FT *t = f->Type;
    (void)r;
    if (!t->Descriptor) {
        SAFEARRAY *array;
        HRESULT hr = SafeArrayAllocDescriptorEx(t->ElemVt == VT_RECORD ? VT_UI1 : t->ElemVt, t->Dims, &array);
        if (FAILED(hr))
            return hr;
        for (USHORT i = 0; i < t->Dims; i++)
            array->rgsabound[t->Dims - 1 - i] = t->Bounds[i];
        array->cbElements = stride_of(t->Elem);
        if (t->ElemVt == VT_RECORD) {
            array->fFeatures = FADF_RECORD;
            SafeArraySetRecordInfo(array, (IRecordInfo *)t->Elem->Rec);
        }
        array->fFeatures |= FADF_STATIC;
        t->Descriptor = array;
    }
    *slot = &t->Descriptor;
    return S_OK;
}

static HRESULT WINAPI ri_get_field_no_copy(IRecordInfo *self, PVOID record, LPCOLESTR name, VARIANT *value,
                                           PVOID *field)
{
    RECINFO *r = self_of(self);
    ULONG index;
    if (!record || !value || !name)
        return E_INVALIDARG;
    HRESULT hr = field_index(r, name, &index);
    if (FAILED(hr))
        return hr;
    hr = VariantClear(value);
    if (FAILED(hr))
        return hr;
    FIELD *f = &r->Fields[index];
    BYTE *p = (BYTE *)record + f->Offset;
    const FT *t = f->Type;
    if (field)
        *field = nullptr;
    switch (t->Vt) {
    case VT_CARRAY: {
        SAFEARRAY **slot;
        hr = array_descriptor(r, f, &slot);
        if (FAILED(hr))
            return hr;
        value->vt = (VARTYPE)(t->ElemVt | VT_ARRAY | VT_BYREF);
        value->pparray = slot;
        if (field)
            *field = p;
        return S_OK;
    }
    case VT_SAFEARRAY:
        if (!*(SAFEARRAY **)p) {
            SAFEARRAY *array;
            hr = SafeArrayAllocDescriptorEx(t->ElemVt, 1, &array);
            if (FAILED(hr))
                return hr;
            *(SAFEARRAY **)p = array;
        }
        value->vt = (VARTYPE)(t->ElemVt | VT_ARRAY | VT_BYREF);
        value->pparray = (SAFEARRAY **)p;
        return S_OK;
    case VT_RECORD:
        value->vt = VT_RECORD | VT_BYREF;
        value->byref = p;
        value->pRecInfo = (IRecordInfo *)t->Rec;
        return S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
    case VT_VARIANT:
        value->vt = (VARTYPE)(t->Vt | VT_BYREF);
        value->byref = p;
        return S_OK;
    default:
        value->vt = (VARTYPE)(t->Vt | VT_BYREF);
        value->byref = p;
        return t->Size ? S_OK : TYPE_E_UNDEFINEDTYPE_;
    }
}

/* converts v to the field type and stores it. */
static HRESULT store_scalar(const FT *t, void *p, VARIANT *v)
{
    VARTYPE vt = t->Vt;
    HRESULT hr = VariantChangeTypeEx(v, v, 0x400, VARIANT_NOVALUEPROP, vt);
    if (FAILED(hr))
        return hr;
    switch (vt) {
    case VT_I1:
    case VT_UI1: *(BYTE *)p = v->bVal; return S_OK;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: *(USHORT *)p = v->uiVal; return S_OK;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR: *(ULONG *)p = v->ulVal; return S_OK;
    case VT_I8:
    case VT_UI8:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_INT_PTR:
    case VT_UINT_PTR: *(ULONGLONG *)p = v->ullVal; return S_OK;
    case VT_DECIMAL: *(DECIMAL *)p = v->decVal; return S_OK;
    default: return TYPE_E_UNDEFINEDTYPE_;
    }
}

/* the element type must match, and for a fixed array the bounds too. */
static BOOL array_fits(const FT *t, const VARIANT *v)
{
    if (!(v->vt & VT_ARRAY) || (v->vt & VT_BYREF) || !v->parray)
        return FALSE;
    if ((VARTYPE)(v->vt & ~VT_ARRAY) != t->ElemVt)
        return FALSE;
    if (t->Vt == VT_CARRAY) {
        if (v->parray->cDims != t->Dims)
            return FALSE;
        for (USHORT i = 0; i < t->Dims; i++) {
            const SAFEARRAYBOUND *b = &v->parray->rgsabound[t->Dims - 1 - i];
            if (b->cElements != t->Bounds[i].cElements || b->lLbound != t->Bounds[i].lLbound)
                return FALSE;
        }
    }
    return TRUE;
}

/* replaces an object with the value of its default property. */
static HRESULT default_value(VARIANT *v)
{
    IDispatch *object = v->pdispVal;
    DISPPARAMS none = {nullptr, nullptr, 0, 0};
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = object->lpVtbl->Invoke(object, DISPID_VALUE, &IID_NULL, 0x800, DISPATCH_PROPERTYGET, &none, &value,
                                        nullptr, nullptr);
    if (FAILED(hr))
        return hr;
    VariantClear(v);
    *v = value;
    return S_OK;
}

/* a value is put into an object field by assigning the object's default property. */
static HRESULT let_dispatch(IDispatch *target, VARIANT *value)
{
    DISPID named = DISPID_PROPERTYPUT;
    DISPPARAMS params = {value, &named, 1, 1};
    return target->lpVtbl->Invoke(target, DISPID_VALUE, &IID_NULL, 0x800, DISPATCH_PROPERTYPUT, &params, nullptr,
                                  nullptr, nullptr);
}

static HRESULT convert_in_place(VARIANT *v, VARTYPE vt)
{
    if (v->vt == vt)
        return S_OK;
    return VariantChangeTypeEx(v, v, 0x400, VARIANT_NOVALUEPROP, vt);
}

/* moves the object out of v into the field. */
static void take_object(IUnknown **slot, VARIANT *v)
{
    AutRelease(*slot);
    *slot = v->punkVal;
    v->vt = VT_EMPTY;
}

static HRESULT put_fixed_array(const FT *t, void *p, const VARIANT *v)
{
    const SAFEARRAY *array = v->parray;
    ULONG stride = stride_of(t->Elem);
    ULONG count = stride ? t->Size / stride : 0;
    BYTE *fresh = AutAllocZero(t->Size ? t->Size : 1);
    if (!fresh)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < count; i++) {
        HRESULT hr = ft_copy(t->Elem, (BYTE *)array->pvData + (SIZE_T)i * array->cbElements,
                             fresh + (SIZE_T)i * stride);
        if (FAILED(hr)) {
            while (i--)
                ft_clear(t->Elem, fresh + (SIZE_T)i * stride);
            AutFree(fresh);
            return hr;
        }
    }
    HRESULT hr = ft_clear(t, p);
    if (SUCCEEDED(hr))
        memcpy(p, fresh, t->Size);
    else
        ft_clear(t, fresh);
    AutFree(fresh);
    return hr;
}

/*
 * v is owned here: what the field takes is moved out of it and the caller clears the rest. With exact, a string field
 * accepts only a string.
 */
static HRESULT put_by_value(FIELD *f, void *p, VARIANT *v, BOOL exact)
{
    const FT *t = f->Type;
    HRESULT hr;
    if (v->vt == VT_DISPATCH && v->pdispVal && t->Vt != VT_DISPATCH) {
        hr = default_value(v);
        if (FAILED(hr))
            return hr;
    }
    switch (t->Vt) {
    case VT_BSTR:
        if (exact && v->vt != VT_BSTR)
            return DISP_E_TYPEMISMATCH;
        hr = convert_in_place(v, VT_BSTR);
        if (FAILED(hr))
            return hr;
        SysFreeString(*(BSTR *)p);
        *(BSTR *)p = v->bstrVal;
        v->vt = VT_EMPTY;
        return S_OK;
    case VT_VARIANT:
        hr = VariantClear(p);
        if (FAILED(hr))
            return hr;
        *(VARIANT *)p = *v;
        v->vt = VT_EMPTY;
        return S_OK;
    case VT_UNKNOWN:
        if (t->ViaPointer)
            return E_INVALIDARG;
        hr = convert_in_place(v, VT_UNKNOWN);
        if (FAILED(hr))
            return hr;
        take_object((IUnknown **)p, v);
        return S_OK;
    case VT_DISPATCH:
        if (!*(IDispatch **)p)
            return TYPE_E_INVALIDSTATE_;
        return let_dispatch(*(IDispatch **)p, v);
    case VT_SAFEARRAY:
        if (!(v->vt & VT_ARRAY) || (v->vt & VT_BYREF) || !v->parray || (VARTYPE)(v->vt & ~VT_ARRAY) != t->ElemVt)
            return E_INVALIDARG;
        if (*(SAFEARRAY **)p) {
            hr = SafeArrayDestroy(*(SAFEARRAY **)p);
            *(SAFEARRAY **)p = nullptr;
            if (FAILED(hr))
                return hr;
        }
        *(SAFEARRAY **)p = v->parray;
        v->vt = VT_EMPTY;
        return S_OK;
    case VT_CARRAY:
        if (!array_fits(t, v))
            return E_INVALIDARG;
        return put_fixed_array(t, p, v);
    case VT_RECORD:
        if (v->vt != VT_RECORD || !v->pRecInfo || !v->pvRecord)
            return E_INVALIDARG;
        if (!((IRecordInfo *)t->Rec)->lpVtbl->IsMatchingType((IRecordInfo *)t->Rec, v->pRecInfo))
            return E_INVALIDARG;
        return ((IRecordInfo *)t->Rec)->lpVtbl->RecordCopy((IRecordInfo *)t->Rec, v->pvRecord, p);
    default:
        if (!t->Size)
            return TYPE_E_UNDEFINEDTYPE_;
        return store_scalar(t, p, v);
    }
}

/* object fields take the new object directly, bypassing the default property. */
static HRESULT put_by_reference(FIELD *f, void *p, VARIANT *v)
{
    const FT *t = f->Type;
    HRESULT hr;
    switch (t->Vt) {
    case VT_VARIANT:
        if (v->vt != VT_DISPATCH && v->vt != VT_UNKNOWN)
            return E_INVALIDARG;
        hr = VariantClear(p);
        if (FAILED(hr))
            return hr;
        *(VARIANT *)p = *v;
        v->vt = VT_EMPTY;
        return S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
        hr = convert_in_place(v, t->Vt);
        if (FAILED(hr))
            return hr;
        if (t->ViaPointer && v->punkVal) {
            IUnknown *wanted;
            hr = v->punkVal->lpVtbl->QueryInterface(v->punkVal, &t->Iid, (void **)&wanted);
            if (FAILED(hr))
                return hr;
            v->punkVal->lpVtbl->Release(v->punkVal);
            v->punkVal = wanted;
        }
        take_object((IUnknown **)p, v);
        return S_OK;
    default:
        return E_INVALIDARG;
    }
}

static HRESULT put_field(RECINFO *r, ULONG flags, PVOID record, LPCOLESTR name, VARIANT *value, BOOL consume)
{
    ULONG index;
    VARIANT work;
    HRESULT hr;
    if ((flags != DISPATCH_PROPERTYPUT && flags != DISPATCH_PROPERTYPUTREF) || !record || !name || !value)
        return E_INVALIDARG;
    hr = field_index(r, name, &index);
    if (FAILED(hr))
        return hr;
    /* a consumed value is used in place, only a by reference one is copied. */
    BOOL shallow = consume && !(value->vt & VT_BYREF);
    if (shallow) {
        work = *value;
    } else {
        VariantInit(&work);
        hr = VariantCopyInd(&work, value);
        if (FAILED(hr))
            return hr;
    }
    FIELD *f = &r->Fields[index];
    BYTE *p = (BYTE *)record + f->Offset;
    if (flags == DISPATCH_PROPERTYPUT)
        hr = put_by_value(f, p, &work, consume);
    else
        hr = put_by_reference(f, p, &work);
    if (!shallow) {
        VariantClear(&work);
    } else if (SUCCEEDED(hr)) {
        VariantClear(&work);
        value->vt = VT_EMPTY;
    } else {
        /* hand back whatever conversion left in the variant. */
        *value = work;
    }
    return hr;
}

static HRESULT WINAPI ri_put_field(IRecordInfo *self, ULONG flags, PVOID record, LPCOLESTR name, VARIANT *value)
{
    return put_field(self_of(self), flags, record, name, value, FALSE);
}

static HRESULT WINAPI ri_put_field_no_copy(IRecordInfo *self, ULONG flags, PVOID record, LPCOLESTR name, VARIANT *value)
{
    return put_field(self_of(self), flags, record, name, value, TRUE);
}

static HRESULT WINAPI ri_get_field_names(IRecordInfo *self, ULONG *count, BSTR *names)
{
    RECINFO *r = self_of(self);
    if (!count)
        return E_INVALIDARG;
    if (!names) {
        *count = r->FieldCount;
        return S_OK;
    }
    memset(names, 0, *count * sizeof(BSTR));
    if (*count > r->FieldCount)
        *count = r->FieldCount;
    for (ULONG i = 0; i < *count; i++) {
        BSTR name = r->Fields[i].Name;
        names[i] = SysAllocStringLen(name, SysStringLen(name));
        if (!names[i]) {
            for (ULONG j = i; j--;) {
                SysFreeString(names[j]);
                names[j] = nullptr;
            }
            if (i)
                *count = i;
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}

/* the declaring library of a record type from another IRecordInfo implementation. */
static BOOL library_of(IRecordInfo *record, TLIBATTR *out)
{
    ITypeInfo *info;
    ITypeLib *lib;
    UINT index;
    TLIBATTR *attr;
    BOOL found = FALSE;
    if (FAILED(record->lpVtbl->GetTypeInfo(record, &info)) || !info)
        return FALSE;
    if (SUCCEEDED(info->lpVtbl->GetContainingTypeLib(info, &lib, &index))) {
        if (SUCCEEDED(lib->lpVtbl->GetLibAttr(lib, &attr))) {
            *out = *attr;
            lib->lpVtbl->ReleaseTLibAttr(lib, attr);
            found = TRUE;
        }
        lib->lpVtbl->Release(lib);
    }
    info->lpVtbl->Release(info);
    return found;
}

static BOOL WINAPI ri_is_matching_type(IRecordInfo *self, IRecordInfo *other)
{
    RECINFO *r = self_of(self);
    if (!other)
        return FALSE;
    if (other->lpVtbl == self->lpVtbl) {
        RECINFO *o = self_of(other);
        return IsEqualGUID(&r->Guid, &o->Guid) && IsEqualGUID(&r->LibGuid, &o->LibGuid) && r->Major == o->Major &&
               r->Minor == o->Minor && r->Lcid == o->Lcid && r->Size == o->Size;
    }
    /* other implementations are compared through their methods. */
    GUID guid;
    ULONG size;
    TLIBATTR lib;
    if (FAILED(other->lpVtbl->GetGuid(other, &guid)) || !IsEqualGUID(&guid, &r->Guid))
        return FALSE;
    if (FAILED(other->lpVtbl->GetSize(other, &size)) || size != r->Size)
        return FALSE;
    if (!library_of(other, &lib))
        return TRUE;
    return IsEqualGUID(&lib.guid, &r->LibGuid) && lib.wMajorVerNum == r->Major && lib.wMinorVerNum == r->Minor &&
           lib.lcid == r->Lcid;
}



static PVOID WINAPI ri_create(IRecordInfo *self)
{
    return AutAllocZero(self_of(self)->Size ? self_of(self)->Size : 1);
}

static HRESULT WINAPI ri_create_copy(IRecordInfo *self, PVOID source, PVOID *dest)
{
    if (!source || !dest)
        return E_INVALIDARG;
    void *copy = AutAllocZero(self_of(self)->Size ? self_of(self)->Size : 1);
    *dest = copy;
    if (!copy)
        return E_OUTOFMEMORY;
    HRESULT hr = copy_fields(self_of(self), source, copy);
    if (FAILED(hr)) {
        AutFree(copy);
        *dest = nullptr;
    }
    return hr;
}

static HRESULT WINAPI ri_destroy(IRecordInfo *self, PVOID record)
{
    if (!record)
        return E_INVALIDARG;
    HRESULT hr = ri_clear(self, record);
    if (SUCCEEDED(hr))
        AutFree(record);
    return hr;
}

static const IRecordInfoVtbl g_recinfo_vtbl = {
    ri_query,      ri_addref,           ri_release,          ri_init,         ri_clear,        ri_copy,
    ri_get_guid,   ri_get_name,         ri_get_size,         ri_get_type_info, ri_get_field,   ri_get_field_no_copy,
    ri_put_field,  ri_put_field_no_copy, ri_get_field_names, ri_is_matching_type, ri_create, ri_create_copy,
    ri_destroy};

/* ------------------------------------------------------------------ lookup */

static BOOL ensure_lock(void)
{
    static LONG state;
    if (g_lock_ready)
        return TRUE;
    if (InterlockedCompareExchange(&state, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = TRUE;
    } else {
        while (!g_lock_ready)
            Sleep(0);
    }
    return TRUE;
}

static RECINFO *lookup(const GUID *guid)
{
    for (RECINFO *r = g_records; r; r = r->Next) {
        if (IsEqualGUID(&r->Guid, guid)) {
            InterlockedIncrement(&r->Refs);
            return r;
        }
    }
    return nullptr;
}

static HRESULT fill(RECINFO *r, ITypeInfo *info, TYPEATTR *ta)
{
    r->Size = ta->cbSizeInstance;
    r->Align = ta->cbAlignment;
    r->FieldCount = ta->cVars;
    ITypeLib *lib;
    UINT index;
    HRESULT hr = info->lpVtbl->GetContainingTypeLib(info, &lib, &index);
    if (FAILED(hr))
        return hr;
    TLIBATTR *la;
    hr = lib->lpVtbl->GetLibAttr(lib, &la);
    if (SUCCEEDED(hr)) {
        r->LibGuid = la->guid;
        r->Major = la->wMajorVerNum;
        r->Minor = la->wMinorVerNum;
        r->Lcid = la->lcid;
        lib->lpVtbl->ReleaseTLibAttr(lib, la);
    }
    lib->lpVtbl->Release(lib);
    if (FAILED(hr))
        return hr;
    r->Fields = AutAllocZero((SIZE_T)(r->FieldCount ? r->FieldCount : 1) * sizeof(FIELD));
    if (!r->Fields)
        return E_OUTOFMEMORY;
    hr = info->lpVtbl->GetDocumentation(info, MEMBERID_NIL, &r->Name, nullptr, nullptr, nullptr);
    if (FAILED(hr))
        return hr;
    for (USHORT i = 0; i < r->FieldCount; i++) {
        VARDESC *vd;
        hr = info->lpVtbl->GetVarDesc(info, i, &vd);
        if (FAILED(hr))
            return hr;
        r->Fields[i].Offset = vd->oInst;
        hr = ft_build(info, &vd->elemdescVar.tdesc, FALSE, 0, &r->Fields[i].Type);
        if (SUCCEEDED(hr)) {
            UINT got;
            hr = info->lpVtbl->GetNames(info, vd->memid, &r->Fields[i].Name, 1, &got);
            if (SUCCEEDED(hr) && !got)
                hr = TYPE_E_ELEMENTNOTFOUND;
        }
        info->lpVtbl->ReleaseVarDesc(info, vd);
        if (FAILED(hr)) {
            /* count this field so its type is freed with the record, even though it has no name. */
            r->FieldCount = (USHORT)(i + 1);
            return hr;
        }
    }
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI GetRecordInfoFromTypeInfo(ITypeInfo *info, IRecordInfo **out)
{
    if (!out || !info)
        return E_INVALIDARG;
    *out = nullptr;
    ensure_lock();
    TYPEATTR *ta;
    HRESULT hr = info->lpVtbl->GetTypeAttr(info, &ta);
    if (FAILED(hr))
        return hr;
    GUID identity = ta->guid;
    BOOL record = ta->typekind == TKIND_RECORD && !IsEqualGUID(&ta->guid, &IID_NULL);
    BOOL alias = ta->typekind == TKIND_ALIAS && !IsEqualGUID(&ta->guid, &IID_NULL) &&
                 ta->tdescAlias.vt == VT_USERDEFINED;
    ITypeInfo *target = info;
    TYPEATTR *target_attr = ta;
    if (!record && !alias) {
        info->lpVtbl->ReleaseTypeAttr(info, ta);
        return E_INVALIDARG;
    }
    if (alias) {
        ITypeInfo *ref;
        hr = info->lpVtbl->GetRefTypeInfo(info, ta->tdescAlias.hreftype, &ref);
        TYPEATTR *ra = nullptr;
        if (SUCCEEDED(hr))
            hr = ref->lpVtbl->GetTypeAttr(ref, &ra);
        if (FAILED(hr) || ra->typekind != TKIND_RECORD) {
            if (ra)
                ref->lpVtbl->ReleaseTypeAttr(ref, ra);
            if (SUCCEEDED(hr) || ra == nullptr)
                AutRelease(SUCCEEDED(hr) ? ref : nullptr);
            info->lpVtbl->ReleaseTypeAttr(info, ta);
            return FAILED(hr) ? E_INVALIDARG : E_INVALIDARG;
        }
        target = ref;
        target_attr = ra;
    }
    EnterCriticalSection(&g_lock);
    RECINFO *found = lookup(&identity);
    if (found) {
        LeaveCriticalSection(&g_lock);
        *out = (IRecordInfo *)found;
        target->lpVtbl->ReleaseTypeAttr(target, target_attr);
        if (alias) {
            target->lpVtbl->Release(target);
            info->lpVtbl->ReleaseTypeAttr(info, ta);
        }
        return S_OK;
    }
    LeaveCriticalSection(&g_lock);

    RECINFO *r = AutAllocZero(sizeof(RECINFO));
    if (!r) {
        hr = E_OUTOFMEMORY;
    } else {
        r->lpVtbl = &g_recinfo_vtbl;
        r->Refs = 1;
        r->Guid = identity;
        r->Info = target;
        AutAddRef(target);
        hr = fill(r, target, target_attr);
    }
    target->lpVtbl->ReleaseTypeAttr(target, target_attr);
    if (alias) {
        target->lpVtbl->Release(target);
        info->lpVtbl->ReleaseTypeAttr(info, ta);
    }
    if (FAILED(hr)) {
        if (r)
            rec_destroy(r);
        return hr;
    }
    EnterCriticalSection(&g_lock);
    /* another thread may have created the same record meanwhile: keep that one. */
    found = lookup(&identity);
    if (found) {
        LeaveCriticalSection(&g_lock);
        rec_destroy(r);
        *out = (IRecordInfo *)found;
        return S_OK;
    }
    r->Next = g_records;
    g_records = r;
    LeaveCriticalSection(&g_lock);
    *out = (IRecordInfo *)r;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI GetRecordInfoFromGuids(REFGUID typelib, ULONG major, ULONG minor, LCID lcid, REFGUID type,
                                                    IRecordInfo **out)
{
    if (!out || !typelib || IsEqualGUID(typelib, &IID_NULL) || !type || IsEqualGUID(type, &IID_NULL))
        return E_INVALIDARG;
    *out = nullptr;
    ensure_lock();
    EnterCriticalSection(&g_lock);
    RECINFO *found = lookup(type);
    LeaveCriticalSection(&g_lock);
    if (found) {
        *out = (IRecordInfo *)found;
        return S_OK;
    }
    ITypeLib *lib;
    HRESULT hr = LoadRegTypeLib(typelib, (WORD)major, (WORD)minor, lcid, &lib);
    if (FAILED(hr))
        return hr;
    ITypeInfo *info;
    hr = lib->lpVtbl->GetTypeInfoOfGuid(lib, type, &info);
    lib->lpVtbl->Release(lib);
    if (FAILED(hr))
        return hr;
    hr = GetRecordInfoFromTypeInfo(info, out);
    info->lpVtbl->Release(info);
    return hr;
}

/* ------------------------------------------------------------------ arrays of records */

WINOLEAUTAPI HRESULT WINAPI SafeArraySetRecordInfo(SAFEARRAY *array, IRecordInfo *record)
{
    if (!array || !(array->fFeatures & FADF_RECORD))
        return E_INVALIDARG;
    IRecordInfo **slot = &((IRecordInfo **)array)[-1];
    AutRelease(*slot);
    *slot = record;
    AutAddRef(record);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI SafeArrayGetRecordInfo(SAFEARRAY *array, IRecordInfo **record)
{
    if (!array || !record || !(array->fFeatures & FADF_RECORD))
        return E_INVALIDARG;
    *record = ((IRecordInfo **)array)[-1];
    AutAddRef(*record);
    return S_OK;
}
