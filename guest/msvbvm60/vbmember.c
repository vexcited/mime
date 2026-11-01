/*
 * the members of an object of the program that a late bound call reaches by name or by number. the compiler describes each
 * public procedure of a class or a form by a record: its kind and number of arguments, its place in the vtable, its number for
 * IDispatch and the kinds of its arguments. the call is made through the vtable the way the compiled code makes it, one
 * stdcall with the object first and the arguments after it, answering a result code.
 */
#include "vb.h"

/*
 * the record: bits 0 and 1 of the first word are the kind (0 procedure, 1 property get, 2 let, 3 set), bits 2 to 7 the number
 * of arguments in the table of kinds, 0x100 says the last of them is the result and 0x200 that the one before is the locale,
 * and the bits from 10 on give the argument that takes the rest of them or 63 for none. the second word has the public flag in
 * bit 0 and the offset in the vtable in the bits from 2 on. The number for IDispatch is the long word at 12, and the kinds
 * of the result and then the arguments are the bytes from 32 on: the low five bits are the type, 0x20 says by reference,
 * 0x40 an array and 0x80 optional.
 */
#define RECORD_KINDS 32

static const USHORT g_vartype[32] = {VT_NULL, VT_NULL, VT_INT, VT_BOOL, VT_I1, VT_UI1, VT_I2, VT_UI2, VT_I4, VT_UI4, VT_R4,
                                     VT_R8, VT_DATE, VT_CY, VT_DECIMAL, VT_VARIANT, VT_BSTR, VT_ERROR, VT_ERROR, VT_ERROR, VT_RECORD,
                                     VT_ERROR, VT_DISPATCH, VT_ERROR, VT_ERROR, VT_ERROR, VT_I4, VT_DISPATCH, VT_UNKNOWN, VT_DISPATCH,
                                     VT_HRESULT, 0};

enum { KIND_METHOD, KIND_GET, KIND_LET, KIND_SET };

static const BYTE *record_of(const BYTE *priv, DWORD index)
{
    const BYTE *const *records = *(const BYTE *const *const *)(priv + 24);
    return records ? records[index] : nullptr;
}

/* the number of a member for IDispatch. */
static DISPID dispid_of(const BYTE *record) { return *(const DISPID *)(record + 12); }

HRESULT VbMemberIds(const BYTE *descriptor, const BYTE *priv, LPOLESTR *names, UINT count, DISPID *ids)
{
    DWORD members = *(const DWORD *)(descriptor + 0x1C);
    const DWORD *list = (const DWORD *)*(const DWORD *)(descriptor + 0x20);
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    if (count < 1 || !list || !priv)
        return DISP_E_UNKNOWNNAME;
    char narrow[128];
    if (!WideCharToMultiByte(CP_ACP, 0, names[0], -1, narrow, sizeof(narrow), nullptr, nullptr))
        return DISP_E_UNKNOWNNAME;
    for (DWORD i = 0; i < members; i++) {
        const BYTE *record = record_of(priv, i);
        if (record && (*(const USHORT *)(record + 2) & 1) && !lstrcmpiA((const char *)list[i], narrow)) {
            ids[0] = dispid_of(record);
            return count == 1 ? S_OK : DISP_E_UNKNOWNNAME;
        }
    }
    return DISP_E_UNKNOWNNAME;
}

/* the length of a kind byte and what follows it. */
static UINT kind_length(const BYTE *at)
{
    UINT type = *at & 0x1F;
    if (type == 17 || type == 19 || type == 28 || type == 29 || type == 20)
        return ((UINT)(ULONG_PTR)(at + 1 + 3) & ~3u) - (UINT)(ULONG_PTR)at + 4;
    return 1;
}

typedef struct Call {
    DWORD words[96];
    int count;
    VARIANT temp[24];
} Call;

static void push(Call *c, DWORD word)
{
    if (c->count < 96)
        c->words[c->count++] = word;
}

static void push_bytes(Call *c, const void *data, UINT bytes)
{
    for (UINT i = 0; i < bytes; i += 4)
        push(c, *(const DWORD *)((const BYTE *)data + i));
}

static HRESULT convert(VARIANT *out, const VARIANT *in, VARTYPE vt)
{
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = VbCopyInd(&value, in);
    if (FAILED(hr))
        return hr;
    if (V_VT(&value) == VT_EMPTY || (V_VT(&value) == VT_ERROR && V_ERROR(&value) == DISP_E_PARAMNOTFOUND)) {
        VariantClear(&value);
        VariantInit(out);
        V_VT(out) = vt;
        return S_OK;
    }
    if (V_VT(&value) == vt) {
        *out = value;
        return S_OK;
    }
    VariantInit(out);
    hr = VariantChangeTypeEx(out, &value, VbLocale(), 0, vt);
    VariantClear(&value);
    return hr;
}

/* an argument by value or by reference from a variant of the caller. */
static HRESULT marshal(Call *c, int slot, const BYTE *kind, const VARIANT *in, BOOL missing, UINT *bad_argument, UINT position)
{
    UINT type = *kind & 0x1F;
    BOOL by_ref = (*kind & 0x20) != 0, array = (*kind & 0x40) != 0;
    VARTYPE vt = g_vartype[type];
    VARIANT *temp = &c->temp[slot];
    VariantInit(temp);
    if (array) {
        if (missing) {
            V_VT(temp) = VT_ARRAY | VT_VARIANT;
            V_ARRAY(temp) = nullptr;
        } else if ((V_VT(in) & (VT_ARRAY | VT_BYREF)) == (VT_ARRAY | VT_BYREF) && by_ref) {
            push(c, (DWORD)(ULONG_PTR)V_ARRAYREF(in));
            return S_OK;
        } else if ((V_VT(in) & VT_ARRAY)) {
            V_VT(temp) = V_VT(in) & ~VT_BYREF;
            V_ARRAY(temp) = (V_VT(in) & VT_BYREF) ? *V_ARRAYREF(in) : V_ARRAY(in);
        } else {
            *bad_argument = position;
            return DISP_E_TYPEMISMATCH;
        }
        push(c, by_ref ? (DWORD)(ULONG_PTR)&V_ARRAY(temp) : (DWORD)(ULONG_PTR)V_ARRAY(temp));
        return S_OK;
    }
    if (vt == VT_RECORD || vt == VT_HRESULT || vt == VT_NULL) {
        *bad_argument = position;
        return DISP_E_BADVARTYPE;
    }
    /* the caller's own variable is passed on when its type is that of the argument. */
    if (by_ref && !missing && (V_VT(in) & VT_BYREF) && (V_VT(in) & VT_TYPEMASK) == (vt == VT_INT ? VT_I4 : vt) && !(V_VT(in) & VT_ARRAY)) {
        push(c, (DWORD)(ULONG_PTR)V_BYREF(in));
        return S_OK;
    }
    if (vt == VT_VARIANT) {
        if (missing) {
            V_VT(temp) = VT_ERROR;
            V_ERROR(temp) = DISP_E_PARAMNOTFOUND;
        } else if (V_VT(in) == (VT_BYREF | VT_VARIANT) && by_ref) {
            push(c, (DWORD)(ULONG_PTR)V_VARIANTREF(in));
            return S_OK;
        } else {
            HRESULT hr = VbCopyInd(temp, in);
            if (FAILED(hr))
                return hr;
        }
        if (by_ref)
            push(c, (DWORD)(ULONG_PTR)temp);
        else
            push_bytes(c, temp, 16);
        return S_OK;
    }
    if (vt == VT_DISPATCH || vt == VT_UNKNOWN) {
        IUnknown *object = nullptr;
        if (!missing) {
            VARIANT value;
            VariantInit(&value);
            HRESULT hr = VbCopyInd(&value, in);
            if (FAILED(hr))
                return hr;
            VARTYPE have = V_VT(&value);
            if (have == VT_DISPATCH || have == VT_UNKNOWN) {
                object = V_UNKNOWN(&value);
                if (object)
                    object->lpVtbl->AddRef(object);
            } else if (have != VT_EMPTY) {
                VariantClear(&value);
                *bad_argument = position;
                return DISP_E_TYPEMISMATCH;
            }
            VariantClear(&value);
        }
        V_VT(temp) = vt;
        V_UNKNOWN(temp) = object;
        push(c, by_ref ? (DWORD)(ULONG_PTR)&V_UNKNOWN(temp) : (DWORD)(ULONG_PTR)object);
        return S_OK;
    }
    HRESULT hr = missing ? S_OK : convert(temp, in, vt == VT_INT ? VT_I4 : vt);
    if (missing) {
        V_VT(temp) = vt;
    }
    if (FAILED(hr)) {
        *bad_argument = position;
        return hr == DISP_E_TYPEMISMATCH || hr == DISP_E_OVERFLOW ? hr : DISP_E_TYPEMISMATCH;
    }
    if (vt == VT_BSTR) {
        push(c, by_ref ? (DWORD)(ULONG_PTR)&V_BSTR(temp) : (DWORD)(ULONG_PTR)V_BSTR(temp));
        return S_OK;
    }
    if (by_ref) {
        push(c, (DWORD)(ULONG_PTR)&V_I4(temp));
        return S_OK;
    }
    switch (vt) {
    case VT_I2:
    case VT_BOOL: push(c, (DWORD)(LONG)V_I2(temp)); break;
    case VT_I1: push(c, (DWORD)(LONG)V_I1(temp)); break;
    case VT_UI1: push(c, V_UI1(temp)); break;
    case VT_UI2: push(c, V_UI2(temp)); break;
    case VT_R8:
    case VT_DATE:
    case VT_CY: push_bytes(c, &V_R8(temp), 8); break;
    case VT_DECIMAL: push_bytes(c, temp, 16); break;
    default: push(c, (DWORD)V_I4(temp)); break;
    }
    return S_OK;
}

/* the result written by the procedure, as a variant. */
static void result_variant(VARIANT *out, const BYTE *kind, VARIANT *temp)
{
    UINT type = *kind & 0x1F;
    VARTYPE vt = g_vartype[type];
    VariantInit(out);
    if (vt == VT_VARIANT) {
        *out = *temp;
        VariantInit(temp);
        return;
    }
    if (vt == VT_DISPATCH || vt == VT_UNKNOWN) {
        V_VT(out) = vt;
        V_UNKNOWN(out) = V_UNKNOWN(temp);
        V_VT(temp) = VT_EMPTY;
        return;
    }
    if (vt == VT_INT)
        vt = VT_I4;
    memcpy((BYTE *)out + 8, (BYTE *)temp + 8, 8);
    V_VT(out) = vt;
    V_VT(temp) = VT_EMPTY;
}

static void fill_exception(EXCEPINFO *info, HRESULT hr)
{
    if (!info)
        return;
    VbThread *t = VbThreadState();
    memset(info, 0, sizeof(*info));
    info->scode = hr;
    LONG number = t->err_number ? t->err_number : VbErrorOfHresult(hr);
    info->wCode = 0;
    info->bstrSource = SysAllocString(t->err_source && t->err_source[0] ? t->err_source : VbProjectName());
    info->bstrDescription = SysAllocString(t->err_description && t->err_description[0] ? t->err_description : VbErrorText(number));
}

HRESULT VbMemberInvoke(void *object, void **vtable, const BYTE *descriptor, const BYTE *priv, DISPID id, WORD flags, DISPPARAMS *params,
                       VARIANT *result, EXCEPINFO *info, UINT *bad)
{
    DWORD members = *(const DWORD *)(descriptor + 0x1C);
    if (!priv)
        return DISP_E_MEMBERNOTFOUND;
    UINT wanted = flags & 0xF;
    const BYTE *found = nullptr;
    BOOL known = FALSE;
    for (DWORD i = 0; i < members; i++) {
        const BYTE *record = record_of(priv, i);
        if (!record || dispid_of(record) != id)
            continue;
        known = TRUE;
        UINT kind_mask;
        switch (*(const USHORT *)record & 3) {
        case KIND_METHOD: kind_mask = DISPATCH_METHOD; break;
        case KIND_GET: kind_mask = DISPATCH_PROPERTYGET; break;
        case KIND_LET: kind_mask = DISPATCH_PROPERTYPUT; break;
        default: kind_mask = DISPATCH_PROPERTYPUTREF; break;
        }
        BOOL call_form = (kind_mask == DISPATCH_PROPERTYGET && (wanted & DISPATCH_METHOD));
        if ((wanted & kind_mask) || call_form) {
            found = record;
            break;
        }
    }
    if (!found)
        return known ? DISP_E_MEMBERNOTFOUND : DISP_E_MEMBERNOTFOUND;
    USHORT word = *(const USHORT *)found;
    UINT argc = (word & 0xFF) >> 2;
    BOOL has_result = (word & 0x100) != 0, has_locale = (word & 0x200) != 0;
    UINT supplied = params->cArgs;
    UINT named = params->cNamedArgs;
    BOOL property_put = named == 1 && params->rgdispidNamedArgs && params->rgdispidNamedArgs[0] == DISPID_PROPERTYPUT;
    if (named && !property_put)
        return DISP_E_NONAMEDARGS;
    UINT callable = argc - (has_result ? 1 : 0) - (has_locale ? 1 : 0);
    if (supplied > callable)
        return DISP_E_BADPARAMCOUNT;
    const BYTE *kinds = found + RECORD_KINDS;
    const BYTE *kind_at[24];
    const BYTE *cursor = kinds + kind_length(kinds);
    if (argc > 22)
        return DISP_E_BADPARAMCOUNT;
    for (UINT i = 0; i < argc; i++) {
        kind_at[i] = cursor;
        cursor += kind_length(cursor);
    }
    Call *c = (Call *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Call));
    if (!c)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    push(c, (DWORD)(ULONG_PTR)object);
    int result_slot = -1;
    for (UINT i = 0; i < argc && SUCCEEDED(hr); i++) {
        const BYTE *kind = kind_at[i];
        if (has_result && i == argc - 1) {
            /* the result is written through a pointer to storage of its type. */
            BYTE pointee = (BYTE)(*kind & ~0x20);
            VARTYPE vt = g_vartype[pointee & 0x1F];
            VARIANT *temp = &c->temp[i];
            VariantInit(temp);
            V_VT(temp) = vt == VT_INT ? VT_I4 : vt;
            result_slot = (int)i;
            push(c, (DWORD)(ULONG_PTR)(vt == VT_VARIANT ? (void *)temp : (void *)&V_I4(temp)));
            continue;
        }
        if (has_locale && i == argc - (has_result ? 2 : 1)) {
            push(c, (DWORD)GetUserDefaultLCID());
            continue;
        }
        BOOL optional = (*kind & 0x80) != 0;
        VARIANT *source = nullptr;
        if (i < supplied)
            source = &params->rgvarg[supplied - 1 - i];
        BOOL missing = !source || (V_VT(source) == VT_ERROR && V_ERROR(source) == DISP_E_PARAMNOTFOUND);
        if (missing && !optional) {
            hr = DISP_E_PARAMNOTOPTIONAL;
            if (bad)
                *bad = i;
            break;
        }
        UINT position = supplied - 1 - i;
        UINT bad_position = 0;
        hr = marshal(c, (int)i, kind, source ? source : params->rgvarg, missing, &bad_position, position);
        if (FAILED(hr) && bad)
            *bad = bad_position;
    }
    if (SUCCEEDED(hr)) {
        UINT offset = 4u * ((*(const USHORT *)(found + 2)) >> 2);
        void *target = vtable[offset / 4];
        LONG status = VbCallMember(target, (DWORD *)c->words, c->count);
        hr = status;
        if (SUCCEEDED(hr) && result_slot >= 0) {
            if (result)
                result_variant(result, kind_at[result_slot], &c->temp[result_slot]);
            else if (g_vartype[*kind_at[result_slot] & 0x1F] == VT_VARIANT)
                VariantClear(&c->temp[result_slot]);
        } else if (SUCCEEDED(hr) && result) {
            VariantInit(result);
        }
        if (FAILED(hr)) {
            fill_exception(info, hr);
            hr = DISP_E_EXCEPTION;
        }
    }
    for (UINT i = 0; i < argc; i++) {
        VARTYPE vt = V_VT(&c->temp[i]);
        if (vt == VT_BSTR)
            SysFreeString(V_BSTR(&c->temp[i]));
        else if ((vt == VT_DISPATCH || vt == VT_UNKNOWN) && V_UNKNOWN(&c->temp[i]))
            V_UNKNOWN(&c->temp[i])->lpVtbl->Release(V_UNKNOWN(&c->temp[i]));
        else if (vt == VT_VARIANT || (vt == VT_ERROR && i + 1 == argc))
            ;
    }
    HeapFree(GetProcessHeap(), 0, c);
    return hr;
}
