/*
 * records the compiler describes: assignment, the conversion between the unicode and the ansi form of a record that a
 * Declare passes on, the destruction of what it owns, the Lset and Rset of fixed strings, and the indexing of arrays that a
 * variant holds.
 *
 * the table of a record is a header of twelve bytes and then one item for each field. the header has its own length, the
 * size of the record in memory, the number of fields, and the size of its ansi form. an item starts with the offset of the
 * field and its flags, which say what it is in the low four bits: 1 string, 2 variant, 3 object, 4 and 10 fixed string, 5 array,
 * 9 record in a record, 11 bytes. an item that has the flag 0x2000 also has the offset in the ansi form.
 */
#include "vb.h"

VB_STD BSTR vb___vbaStrToAnsi(BSTR *dst, BSTR src);
VB_STD BSTR vb___vbaStrToUnicode(BSTR *dst, BSTR src);

#define U16(p, i) (((const UINT16 *)(p))[i])

static BOOL scalar_kind(UINT16 type) { return (type & 0xF) == 1 || (type & 0xF) == 2 || (type & 0xF) == 3 || (type & 0x60); }

static const BYTE *array_header(const BYTE *item) { return item + ((U16(item, 4) & 0x60) ? 32 : 16); }

/* the length of an item in the table. */
static UINT item_size(const BYTE *item)
{
    UINT16 flags = U16(item, 1);
    UINT minimum = (flags & 0x400) ? 6 : 0;
    UINT size;
    switch (flags & 0xF) {
    case 1:
    case 2:
    case 3:
    case 0xB:
        size = (flags & 0x2000) ? 8 : 4;
        return size < minimum ? minimum : size;
    case 6:
        return 4 < minimum ? minimum : 4;
    case 4:
    case 0xA: return 10;
    case 8: return 6;
    case 9: return 28;
    case 5: {
        UINT16 element = U16(item, 4);
        if (flags & 0x100) {
            if (element & 0x60)
                return 32;
            return (element & 0xF) == 10 ? 14 : 10;
        }
        if (scalar_kind(element) || !(flags & 0x80) || (flags & 0x2200)) {
            const BYTE *header = array_header(item);
            UINT dims = U16(header, 0);
            return (dims ? 8 * dims - 8 : 0) + ((header[2] & 0xE0) ? 4 : 0) + ((element & 0x60) ? 56 : 40);
        }
        return 0;
    }
    default: return 0;
    }
}

static UINT array_count(const BYTE *header)
{
    UINT dims = U16(header, 0), n = dims ? 1 : 0;
    const UINT32 *bound = (const UINT32 *)(header + 16);
    for (UINT d = 0; d < dims; d++)
        n *= bound[d * 2];
    return n;
}

typedef enum { LAYOUT_UNICODE, LAYOUT_ANSI } Layout;

/* the offset of a field in the unicode form or in the ansi form of the record. */
static UINT field_offset(const BYTE *item, Layout layout)
{
    if (layout == LAYOUT_ANSI && (U16(item, 1) & 0x2000) && (U16(item, 1) & 0xF) != 5)
        return U16(item, 3);
    return U16(item, 0);
}

static void free_element(UINT16 type, BYTE *at)
{
    switch (type & 0xF) {
    case 1:
    case 4:
        if (*(BSTR *)at)
            SysFreeString(*(BSTR *)at);
        *(BSTR *)at = nullptr;
        break;
    case 2: VariantClear((VARIANT *)at); break;
    case 3:
        if (*(IUnknown **)at) {
            IUnknown *object = *(IUnknown **)at;
            *(IUnknown **)at = nullptr;
            object->lpVtbl->Release(object);
        }
        break;
    default: break;
    }
}

static void destroy_fields(const BYTE *table, BYTE *record, Layout layout);

static void destroy_item(const BYTE *item, BYTE *record, Layout layout)
{
    UINT16 flags = U16(item, 1);
    BYTE *at = record + field_offset(item, layout);
    switch (flags & 0xF) {
    case 1:
    case 2:
    case 3: free_element(flags, at); break;
    case 5:
        if (flags & 0x100) {
            SAFEARRAY *array = *(SAFEARRAY **)at;
            if (array) {
                SafeArrayDestroy(array);
                *(SAFEARRAY **)at = nullptr;
            }
        } else {
            UINT16 element = U16(item, 4);
            const BYTE *header = array_header(item);
            UINT count = array_count(header), size = *(const UINT32 *)(header + 4);
            if (scalar_kind(element) && size)
                for (UINT k = 0; k < count; k++)
                    free_element(element, at + (SIZE_T)k * size);
        }
        break;
    case 9:
        if (flags & 0x20) {
            const BYTE *nested = *(const BYTE *const *)(item + 8);
            if (nested)
                destroy_fields(nested, at, (flags & 0x1000) ? LAYOUT_ANSI : layout);
        }
        break;
    default: break;
    }
}

static void destroy_fields(const BYTE *table, BYTE *record, Layout layout)
{
    const BYTE *item = table + 12;
    for (UINT n = U16(table, 3); n; n--) {
        destroy_item(item, record, layout);
        item += item_size(item);
    }
}

VB_STD BYTE *vb___vbaRecDestruct(const BYTE *table, BYTE *record)
{
    destroy_fields(table, record, LAYOUT_UNICODE);
    return record;
}

VB_STD BYTE *vb___vbaRecDestructAnsi(const BYTE *table, BYTE *record)
{
    destroy_fields(table, record, LAYOUT_ANSI);
    return record;
}

/* ------------------------------------------------------------------ assignment */

static HRESULT assign_fields(const BYTE *table, BYTE *dst, const BYTE *src);

static HRESULT assign_fields(const BYTE *table, BYTE *dst, const BYTE *src)
{
    if (dst == src)
        return S_OK;
    UINT size = U16(table, 1);
    memcpy(dst, src, size);
    const BYTE *item = table + 12;
    for (UINT n = U16(table, 3); n; n--, item += item_size(item)) {
        UINT16 flags = U16(item, 1);
        UINT offset = U16(item, 0);
        switch (flags & 0xF) {
        case 1:
        case 2:
        case 3: {
            /* the raw copy made the destination hold the source's pointers, so the old ones are gone from it. */
            BYTE *to = dst + offset;
            const BYTE *from = src + offset;
            if ((flags & 0xF) == 1) {
                BSTR s = *(BSTR const *)from;
                BSTR copy = nullptr;
                if (s && !(copy = SysAllocStringByteLen((const char *)s, SysStringByteLen(s))))
                    return E_OUTOFMEMORY;
                *(BSTR *)to = copy;
            } else if ((flags & 0xF) == 2) {
                if (V_VT((VARIANT *)from) < VT_BSTR)
                    break;
                V_VT((VARIANT *)to) = VT_EMPTY;
                HRESULT hr = VariantCopy((VARIANT *)to, (VARIANT *)from);
                if (FAILED(hr))
                    return hr;
            } else if (*(IUnknown *const *)from) {
                (*(IUnknown *const *)from)->lpVtbl->AddRef(*(IUnknown *const *)from);
            }
            break;
        }
        case 9:
            if (flags & 0x20) {
                const BYTE *nested = *(const BYTE *const *)(item + 8);
                HRESULT hr = nested ? assign_fields(nested, dst + offset, src + offset) : S_OK;
                if (FAILED(hr))
                    return hr;
            }
            break;
        case 5: {
            if (flags & 0x100) {
                SAFEARRAY *array = *(SAFEARRAY *const *)(src + offset);
                *(SAFEARRAY **)(dst + offset) = nullptr;
                if (array) {
                    HRESULT hr = SafeArrayCopy(array, (SAFEARRAY **)(dst + offset));
                    if (FAILED(hr))
                        return hr;
                }
            } else {
                UINT16 element = U16(item, 4);
                const BYTE *header = array_header(item);
                UINT count = array_count(header), esize = *(const UINT32 *)(header + 4);
                if (scalar_kind(element) && esize)
                    for (UINT k = 0; k < count; k++) {
                        BYTE *to = dst + offset + (SIZE_T)k * esize;
                        const BYTE *from = src + offset + (SIZE_T)k * esize;
                        if ((element & 0xF) == 1) {
                            BSTR s = *(BSTR const *)from;
                            *(BSTR *)to = s ? SysAllocStringByteLen((const char *)s, SysStringByteLen(s)) : nullptr;
                        } else if ((element & 0xF) == 2) {
                            V_VT((VARIANT *)to) = VT_EMPTY;
                            VariantCopy((VARIANT *)to, (const VARIANT *)from);
                        } else if ((element & 0xF) == 3 && *(IUnknown *const *)from) {
                            (*(IUnknown *const *)from)->lpVtbl->AddRef(*(IUnknown *const *)from);
                        }
                    }
            }
            break;
        }
        default: break;
        }
    }
    return S_OK;
}

VB_STD BYTE *vb___vbaRecAssign(const BYTE *table, BYTE *dst, const BYTE *src)
{
    if (dst != src) {
        destroy_fields(table, dst, LAYOUT_UNICODE);
        HRESULT hr = assign_fields(table, dst, src);
        if (FAILED(hr))
            VbRaiseHr(hr);
    }
    return dst;
}

/* ------------------------------------------------------------------ the ansi form */

static HRESULT convert_fields(const BYTE *table, BYTE *dst, const BYTE *src, BOOL to_ansi)
{
    memcpy(dst, src, U16(table, 4));
    BOOL diverged = FALSE;
    const BYTE *item = table + 12;
    for (UINT n = U16(table, 3); n; n--, item += item_size(item)) {
        UINT16 flags = U16(item, 1);
        UINT unicode_offset = U16(item, 0);
        UINT ansi_offset = (flags & 0x2000) && (flags & 0xF) != 5 ? U16(item, 3) : unicode_offset;
        const BYTE *from;
        BYTE *to;
        if (diverged) {
            from = src + (to_ansi ? unicode_offset : ansi_offset);
            to = dst + (to_ansi ? ansi_offset : unicode_offset);
        } else {
            from = src + unicode_offset;
            to = dst + unicode_offset;
        }
        switch (flags & 0xF) {
        case 1:
            *(BSTR *)to = nullptr;
            if (to_ansi)
                vb___vbaStrToAnsi((BSTR *)to, *(BSTR const *)from);
            else
                vb___vbaStrToUnicode((BSTR *)to, *(BSTR const *)from);
            break;
        case 2:
            if (diverged || V_VT((const VARIANT *)to) >= VT_BSTR) {
                V_VT((VARIANT *)to) = VT_EMPTY;
                HRESULT hr = VariantCopy((VARIANT *)to, (const VARIANT *)from);
                if (FAILED(hr))
                    return hr;
            }
            break;
        case 9:
            if (flags & 0x40) {
                const BYTE *nested = *(const BYTE *const *)(item + 8);
                memset(to, 0, U16(nested, 4));
                HRESULT hr = convert_fields(nested, to, from, to_ansi);
                if (FAILED(hr))
                    return hr;
                diverged = TRUE;
            } else if (diverged) {
                const BYTE *nested = *(const BYTE *const *)(item + 8);
                memcpy(to, from, (flags & 0x20) ? U16(nested, 1) : U16(item, 2));
            }
            break;
        case 0xA: {
            UINT chars = U16(item, 4);
            if (to_ansi)
                WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)from, (int)chars, (char *)to, (int)chars, nullptr, nullptr);
            else
                MultiByteToWideChar(CP_ACP, 0, (const char *)from, (int)chars, (WCHAR *)to, (int)chars);
            diverged = TRUE;
            break;
        }
        case 0xB:
            if (diverged)
                memcpy(to, from, U16(item, 2));
            break;
        case 5: {
            if (flags & 0x100) {
                SAFEARRAY *array = *(SAFEARRAY *const *)from;
                *(SAFEARRAY **)to = nullptr;
                if (array) {
                    HRESULT hr = SafeArrayCopy(array, (SAFEARRAY **)to);
                    if (FAILED(hr))
                        return hr;
                }
                break;
            }
            UINT16 element = U16(item, 4);
            const BYTE *header = array_header(item);
            UINT count = array_count(header), esize = *(const UINT32 *)(header + 4);
            if (!count || !esize)
                break;
            if ((element & 0xF) == 1) {
                for (UINT k = 0; k < count; k++) {
                    BSTR *slot = (BSTR *)(to + (SIZE_T)k * esize);
                    *slot = nullptr;
                    if (to_ansi)
                        vb___vbaStrToAnsi(slot, *(BSTR const *)(from + (SIZE_T)k * esize));
                    else
                        vb___vbaStrToUnicode(slot, *(BSTR const *)(from + (SIZE_T)k * esize));
                }
            } else if ((element & 0xF) == 0xA) {
                UINT chars = U16(item, 5);
                for (UINT k = 0; k < count; k++) {
                    if (to_ansi)
                        WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)from + (SIZE_T)k * chars, (int)chars, (char *)to + (SIZE_T)k * chars, (int)chars, nullptr, nullptr);
                    else
                        MultiByteToWideChar(CP_ACP, 0, (const char *)from + (SIZE_T)k * chars, (int)chars, (WCHAR *)to + (SIZE_T)k * chars, (int)chars);
                }
                diverged = TRUE;
            } else if (diverged) {
                memcpy(to, from, (SIZE_T)count * esize);
            }
            break;
        }
        default: break;
        }
    }
    return S_OK;
}

VB_STD BYTE *vb___vbaRecAnsiToUni(const BYTE *table, BYTE *dst, const BYTE *src)
{
    destroy_fields(table, dst, LAYOUT_UNICODE);
    HRESULT hr = convert_fields(table, dst, src, FALSE);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return dst;
}

VB_STD BYTE *vb___vbaRecUniToAnsi(const BYTE *table, BYTE *dst, const BYTE *src)
{
    destroy_fields(table, dst, LAYOUT_ANSI);
    HRESULT hr = convert_fields(table, dst, src, TRUE);
    if (FAILED(hr))
        VbRaiseHr(hr);
    return dst;
}

/* ------------------------------------------------------------------ Lset and Rset of fixed strings */

/* a length of 0xFFFF says the target is a variable length string held by the variant that is passed. */
static WCHAR *fixed_target(UINT length, VARIANT *target, UINT *chars)
{
    if (length != 0xFFFF) {
        *chars = length;
        return (WCHAR *)target;
    }
    BSTR s;
    if (V_VT(target) & VT_BYREF) {
        if (V_VT(target) != (VT_BYREF | VT_BSTR))
            VbRaise(VBERR_TYPE_MISMATCH);
        s = *V_BSTRREF(target);
    } else {
        if (V_VT(target) != VT_BSTR)
            VbCheck(VariantChangeTypeEx(target, target, VbLocale(), 0, VT_BSTR));
        s = V_BSTR(target);
    }
    *chars = s ? SysStringLen(s) : 0;
    return s;
}

static WCHAR *fixed_set(UINT length, VARIANT *target, BSTR source, BOOL right)
{
    UINT chars;
    WCHAR *text = fixed_target(length, target, &chars);
    UINT have = source ? SysStringLen(source) : 0, take = have < chars ? have : chars;
    if (!right) {
        memcpy(text, source, take * sizeof(WCHAR));
        for (UINT i = take; i < chars; i++)
            text[i] = L' ';
    } else {
        for (UINT i = 0; i < chars - take; i++)
            text[i] = L' ';
        memcpy(text + chars - take, source, take * sizeof(WCHAR));
    }
    return text;
}

VB_STD WCHAR *vb___vbaLsetFixstr(UINT length, VARIANT *target, BSTR source) { return fixed_set(length, target, source, FALSE); }

VB_STD WCHAR *vb___vbaLsetFixstrFree(UINT length, VARIANT *target, BSTR source)
{
    WCHAR *text = fixed_set(length, target, source, FALSE);
    SysFreeString(source);
    return text;
}

VB_STD WCHAR *vb___vbaRsetFixstr(UINT length, VARIANT *target, BSTR source) { return fixed_set(length, target, source, TRUE); }

VB_STD WCHAR *vb___vbaRsetFixstrFree(UINT length, VARIANT *target, BSTR source)
{
    WCHAR *text = fixed_set(length, target, source, TRUE);
    SysFreeString(source);
    return text;
}

/* ------------------------------------------------------------------ arrays held by variants, indexed by variants */

static SAFEARRAY *array_of_variant(const VARIANT *v)
{
    return (V_VT(v) & VT_BYREF) ? *V_ARRAYREF(v) : V_ARRAY(v);
}

/* the address of the element the index variants name. */
static BYTE *element_address(SAFEARRAY *array, const VARIANT *index, UINT dims)
{
    if (!array || array->cDims != dims)
        VbRaise(VBERR_SUBSCRIPT);
    UINT32 offset = 0;
    UINT32 previous = 0;
    const SAFEARRAYBOUND *bound = array->rgsabound;
    for (UINT d = 0; d < dims; d++, index++) {
        VARIANT value;
        VbValue(&value, index);
        LONG at = VbLong(&value);
        LONG relative = at - bound[d].lLbound;
        if (relative < 0 || (ULONG)relative >= bound[d].cElements)
            VbRaise(VBERR_SUBSCRIPT);
        offset = (UINT32)relative + previous;
        if (d + 1 < dims)
            previous = bound[d + 1].cElements * offset;
    }
    if (!array->pvData)
        VbRaise(VBERR_SUBSCRIPT);
    return (BYTE *)array->pvData + (SIZE_T)offset * array->cbElements;
}

static HRESULT invoke_default(IDispatch *object, WORD flags, const VARIANT *index, UINT dims, VARIANT *value, VARIANT *result)
{
    VARIANT *args = (VARIANT *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (dims + 1) * sizeof(VARIANT));
    DISPID named = DISPID_PROPERTYPUT;
    DISPPARAMS params = {};
    EXCEPINFO info = {};
    UINT bad = 0, count = 0;
    if (!args)
        return E_OUTOFMEMORY;
    if (value)
        args[count++] = *value;
    for (UINT d = dims; d; d--)
        args[count++] = index[d - 1];
    params.rgvarg = args;
    params.cArgs = count;
    if (value) {
        params.cNamedArgs = 1;
        params.rgdispidNamedArgs = &named;
    }
    HRESULT hr = object->lpVtbl->Invoke(object, DISPID_VALUE, &IID_NULL, VbLocale(), flags, &params, result, &info, &bad);
    HeapFree(GetProcessHeap(), 0, args);
    return hr;
}

static IDispatch *object_of(const VARIANT *v)
{
    if ((V_VT(v) & ~VT_BYREF) != VT_DISPATCH)
        VbRaise(VBERR_TYPE_MISMATCH);
    IDispatch *object = (V_VT(v) & VT_BYREF) ? *V_DISPATCHREF(v) : V_DISPATCH(v);
    if (!object)
        VbRaise(VBERR_OBJECT_REQUIRED);
    return object;
}

static VARIANT *index_load(VARIANT *dest, const VARIANT *array_variant, UINT dims, const VARIANT *index)
{
    if (!dims)
        VbRaise(VBERR_SUBSCRIPT);
    if (!(V_VT(array_variant) & VT_ARRAY)) {
        IDispatch *object = object_of(array_variant);
        HRESULT hr = invoke_default(object, DISPATCH_METHOD | DISPATCH_PROPERTYGET, index, dims, nullptr, dest);
        if (FAILED(hr))
            VbRaiseHr(hr);
        return dest;
    }
    SAFEARRAY *array = array_of_variant(array_variant);
    BYTE *at = element_address(array, index, dims);
    VARTYPE vt = V_VT(array_variant) & VT_TYPEMASK;
    VariantInit(dest);
    switch (vt) {
    case VT_VARIANT: VbCheck(VariantCopy(dest, (VARIANT *)at)); return dest;
    case VT_BSTR: {
        BSTR s = *(BSTR *)at;
        V_BSTR(dest) = s ? SysAllocStringByteLen((const char *)s, SysStringByteLen(s)) : nullptr;
        break;
    }
    case VT_DISPATCH:
    case VT_UNKNOWN:
        V_UNKNOWN(dest) = *(IUnknown **)at;
        if (V_UNKNOWN(dest))
            V_UNKNOWN(dest)->lpVtbl->AddRef(V_UNKNOWN(dest));
        break;
    case VT_I2:
    case VT_BOOL: V_I2(dest) = *(SHORT *)at; break;
    case VT_I4:
    case VT_R4:
    case VT_ERROR: V_I4(dest) = *(LONG *)at; break;
    case VT_R8:
    case VT_DATE:
    case VT_CY: V_I8(dest) = *(LONGLONG *)at; break;
    case VT_UI1: V_UI1(dest) = *at; break;
    case VT_DECIMAL: memcpy((BYTE *)dest + 2, at + 2, 14); break;
    default: VbRaise(458);
    }
    V_VT(dest) = vt;
    return dest;
}

VB_C VARIANT *vb___vbaVarIndexLoad(VARIANT *dest, VARIANT *array, UINT dims, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, dims);
    /* the indices lie on the stack as variants, in the order of the source. */
    VARIANT *index = (VARIANT *)ap;
    index_load(dest, array, dims, index);
    __builtin_va_end(ap);
    return dest;
}

static BYTE *index_reference(VARIANT *dest, VARIANT *array_variant, SAFEARRAY **lock, UINT dims, const VARIANT *index)
{
    if (!dims)
        VbRaise(VBERR_SUBSCRIPT);
    if (lock)
        *lock = nullptr;
    if (!(V_VT(array_variant) & VT_ARRAY)) {
        IDispatch *object = object_of(array_variant);
        HRESULT hr = invoke_default(object, DISPATCH_METHOD | DISPATCH_PROPERTYGET, index, dims, nullptr, dest);
        if (FAILED(hr))
            VbRaiseHr(hr);
        return (BYTE *)dest;
    }
    SAFEARRAY *array = array_of_variant(array_variant);
    if (!array)
        VbRaise(VBERR_SUBSCRIPT);
    if (lock) {
        SafeArrayLock(array);
        *lock = array;
    }
    BYTE *at = element_address(array, index, dims);
    VARTYPE vt = V_VT(array_variant) & VT_TYPEMASK;
    if (vt == VT_VARIANT)
        return at;
    VariantInit(dest);
    V_VT(dest) = vt | VT_BYREF;
    V_BYREF(dest) = at;
    return (BYTE *)dest;
}

VB_C BYTE *vb___vbaVarIndexLoadRef(VARIANT *dest, VARIANT *array, UINT dims, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, dims);
    if (V_VT(dest))
        VariantClear(dest);
    BYTE *result = index_reference(dest, array, nullptr, dims, (VARIANT *)ap);
    __builtin_va_end(ap);
    return result;
}

VB_C BYTE *vb___vbaVarIndexLoadRefLock(VARIANT *dest, VARIANT *array, SAFEARRAY **lock, UINT dims, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, dims);
    if (V_VT(dest))
        VariantClear(dest);
    BYTE *result = index_reference(dest, array, lock, dims, (VARIANT *)ap);
    __builtin_va_end(ap);
    return result;
}

static void index_store(VARIANT *array_variant, UINT dims, VARIANT *value, const VARIANT *index, BOOL object)
{
    if (!dims)
        VbRaise(VBERR_SUBSCRIPT);
    if (!(V_VT(array_variant) & VT_ARRAY)) {
        IDispatch *target = object_of(array_variant);
        VARIANT ignored;
        VariantInit(&ignored);
        HRESULT hr = invoke_default(target, object ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT, index, dims, value, &ignored);
        if (FAILED(hr))
            VbRaiseHr(hr);
        return;
    }
    SAFEARRAY *array = array_of_variant(array_variant);
    BYTE *at = element_address(array, index, dims);
    VARTYPE vt = V_VT(array_variant) & VT_TYPEMASK;
    VARIANT source;
    VbValue(&source, value);
    VARIANT converted;
    VariantInit(&converted);
    if (object) {
        VARTYPE kind = V_VT(&source) & ~VT_BYREF;
        if (kind != VT_DISPATCH && kind != VT_UNKNOWN)
            VbRaise(VBERR_OBJECT_REQUIRED);
        IUnknown *incoming = V_UNKNOWN(&source);
        if (vt == VT_VARIANT) {
            VariantClear((VARIANT *)at);
            VbCheck(VariantCopy((VARIANT *)at, &source));
        } else if (vt == VT_DISPATCH || vt == VT_UNKNOWN) {
            IUnknown *old = *(IUnknown **)at;
            if (incoming)
                incoming->lpVtbl->AddRef(incoming);
            *(IUnknown **)at = incoming;
            if (old)
                old->lpVtbl->Release(old);
        } else {
            VbRaise(VBERR_TYPE_MISMATCH);
        }
        VariantClear(&source);
        return;
    }
    VARTYPE target = vt == VT_DATE ? VT_DATE : vt;
    switch (vt) {
    case VT_VARIANT:
        VariantClear((VARIANT *)at);
        VbCheck(VariantCopy((VARIANT *)at, &source));
        break;
    case VT_DISPATCH:
    case VT_UNKNOWN: {
        IUnknown *incoming = V_UNKNOWN(&source), *old = *(IUnknown **)at;
        if ((V_VT(&source) & ~VT_BYREF) != VT_DISPATCH && (V_VT(&source) & ~VT_BYREF) != VT_UNKNOWN)
            VbRaise(VBERR_TYPE_MISMATCH);
        if (incoming)
            incoming->lpVtbl->AddRef(incoming);
        *(IUnknown **)at = incoming;
        if (old)
            old->lpVtbl->Release(old);
        break;
    }
    case VT_I2:
    case VT_I4:
    case VT_R4:
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_BOOL:
    case VT_UI1:
    case VT_BSTR:
    case VT_DECIMAL:
        if (V_VT(&source) == VT_NULL)
            VbRaise(VBERR_INVALID_USE_OF_NULL);
        VbCheck(VariantChangeTypeEx(&converted, &source, VbLocale(), 0, target));
        switch (vt) {
        case VT_I2:
        case VT_BOOL: *(SHORT *)at = V_I2(&converted); break;
        case VT_I4:
        case VT_R4: *(LONG *)at = V_I4(&converted); break;
        case VT_R8:
        case VT_DATE:
        case VT_CY: *(LONGLONG *)at = V_I8(&converted); break;
        case VT_UI1: *at = V_UI1(&converted); break;
        case VT_DECIMAL: memcpy(at + 2, (BYTE *)&converted + 2, 14); break;
        default:
            if (*(BSTR *)at)
                SysFreeString(*(BSTR *)at);
            *(BSTR *)at = V_BSTR(&converted);
            V_VT(&converted) = VT_EMPTY;
            break;
        }
        break;
    default: VbRaise(458);
    }
    VariantClear(&source);
}

/* the value is passed by value, and the indices follow it. */
VB_C VARIANT *vb___vbaVarIndexStore(VARIANT *array, UINT dims, VARIANT value, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, value);
    index_store(array, dims, &value, (VARIANT *)ap, FALSE);
    __builtin_va_end(ap);
    return array;
}

VB_C VARIANT *vb___vbaVarIndexStoreObj(VARIANT *array, UINT dims, VARIANT value, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, value);
    index_store(array, dims, &value, (VARIANT *)ap, TRUE);
    __builtin_va_end(ap);
    return array;
}
