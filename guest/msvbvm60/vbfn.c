/*
 * the string functions of the library that have a byte form, a split and join pair, search from the end, replacement
 * and conversion, and the type tests of variants.
 */
#include "vb.h"

#define VBERR_VARIABLE_TYPE 458

LONG VbErrorOfWin32(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND: return VBERR_FILE_NOT_FOUND;
    case ERROR_PATH_NOT_FOUND: return VBERR_PATH_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_WRITE_PROTECT: return 70;
    case ERROR_INVALID_DRIVE: return 68;
    case ERROR_NOT_READY: return 71;
    case ERROR_DISK_FULL: return 61;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: return 58;
    case ERROR_DIR_NOT_EMPTY: return 75;
    case ERROR_NOT_SAME_DEVICE: return 74;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME: return VBERR_BAD_FILE_NAME;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return VBERR_OUT_OF_MEMORY;
    default: return VBERR_IO;
    }
}

BOOL VbIsMissing(const VARIANT *v) { return V_VT(v) == VT_ERROR && V_ERROR(v) == DISP_E_PARAMNOTFOUND; }

/* the value of an argument: references followed, objects read through their default member. */
void VbValue(VARIANT *out, const VARIANT *in)
{
    VariantInit(out);
    VbCheck(VariantCopyInd(out, in));
    for (int depth = 0; V_VT(out) == VT_DISPATCH && depth < 8; depth++) {
        IDispatch *object = V_DISPATCH(out);
        if (!object)
            VbRaise(VBERR_OBJECT_REQUIRED);
        DISPPARAMS none = {};
        VARIANT result;
        EXCEPINFO info = {};
        UINT bad = 0;
        VariantInit(&result);
        HRESULT hr = object->lpVtbl->Invoke(object, DISPID_VALUE, &IID_NULL, VbLocale(), DISPATCH_PROPERTYGET, &none, &result, &info, &bad);
        VariantClear(out);
        if (FAILED(hr))
            VbRaiseHr(hr);
        VbCheck(VariantCopyInd(out, &result));
        VariantClear(&result);
    }
}

LONG VbLong(const VARIANT *v)
{
    VARIANT value, converted;
    VbValue(&value, v);
    if (V_VT(&value) == VT_NULL)
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    VariantInit(&converted);
    VbCheck(VariantChangeTypeEx(&converted, &value, VbLocale(), 0, VT_I4));
    VariantClear(&value);
    return V_I4(&converted);
}

/* the text of an argument, or false for Null. the string is owned by the caller. */
BOOL VbText(const VARIANT *v, BSTR *out)
{
    VARIANT value, converted;
    VbValue(&value, v);
    *out = nullptr;
    if (V_VT(&value) == VT_NULL)
        return FALSE;
    if (V_VT(&value) == VT_BSTR) {
        *out = V_BSTR(&value);
        return TRUE;
    }
    VariantInit(&converted);
    HRESULT hr = VariantChangeTypeEx(&converted, &value, VbLocale(), 0, VT_BSTR);
    VariantClear(&value);
    VbCheck(hr);
    *out = V_BSTR(&converted);
    return TRUE;
}

static VARIANT *return_text(VARIANT *dest, BSTR b)
{
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = b;
    return dest;
}

static VARIANT *return_null(VARIANT *dest)
{
    VariantInit(dest);
    V_VT(dest) = VT_NULL;
    return dest;
}

static UINT bstr_chars(BSTR s) { return s ? SysStringLen(s) : 0; }

static BSTR new_chars(const OLECHAR *s, UINT n)
{
    BSTR b = SysAllocStringLen(s, n);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

static BSTR new_bytes(const void *s, UINT n)
{
    BSTR b = SysAllocStringByteLen((const char *)s, n);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

/* ------------------------------------------------------------------ the byte forms */

VB_STD BSTR vb_rtcLeftBstr(const char *s, UINT n)
{
    if ((int)n < 0)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringByteLen((BSTR)s) : 0;
    return new_bytes(s, n > len ? len : n);
}

VB_STD BSTR vb_rtcRightBstr(const char *s, UINT n)
{
    if ((int)n < 0)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringByteLen((BSTR)s) : 0;
    UINT take = n > len ? len : n;
    return new_bytes(s + (len - take), take);
}

VB_STD BSTR vb_rtcMidBstr(const char *s, LONG start, VARIANT *length)
{
    if (start < 1)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringByteLen((BSTR)s) : 0;
    UINT from = (UINT)start - 1;
    if (from > len)
        from = len;
    UINT take = len - from;
    if (!VbIsMissing(length)) {
        LONG n = VbLong(length);
        if (n < 0)
            VbRaise(VBERR_INVALID_CALL);
        if ((UINT)n < take)
            take = (UINT)n;
    }
    return new_bytes(s + from, take);
}

/* a function of a string that gives Null for Null. */
static VARIANT *text_function(VARIANT *dest, VARIANT *src, BSTR (*function)(BSTR, LONG), LONG argument)
{
    BSTR s;
    if (!VbText(src, &s))
        return return_null(dest);
    BSTR r = function(s, argument);
    SysFreeString(s);
    return return_text(dest, r);
}

static BSTR left_bytes(BSTR s, LONG n) { return vb_rtcLeftBstr((const char *)s, (UINT)n); }
static BSTR right_bytes(BSTR s, LONG n) { return vb_rtcRightBstr((const char *)s, (UINT)n); }

VB_STD VARIANT *vb_rtcLeftVar(VARIANT *dest, VARIANT *src, UINT n) { return text_function(dest, src, left_bytes, (LONG)n); }
VB_STD VARIANT *vb_rtcRightVar(VARIANT *dest, VARIANT *src, UINT n) { return text_function(dest, src, right_bytes, (LONG)n); }

VB_STD VARIANT *vb_rtcMidVar(VARIANT *dest, VARIANT *src, LONG start, VARIANT *length)
{
    BSTR s;
    if (!VbText(src, &s))
        return return_null(dest);
    BSTR r = vb_rtcMidBstr((const char *)s, start, length);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD BSTR vb_rtcLowerCaseBstr(BSTR s)
{
    UINT n = bstr_chars(s);
    BSTR b = new_chars(s, n);
    CharLowerBuffW(b, n);
    return b;
}

VB_STD BSTR vb_rtcRightTrimBstr(BSTR s)
{
    UINT n = bstr_chars(s);
    while (n && s[n - 1] == L' ')
        n--;
    return n ? new_chars(s, n) : nullptr;
}

VB_STD BSTR vb_rtcStrReverse(BSTR s)
{
    if (!s)
        return nullptr;
    UINT n = SysStringLen(s);
    BSTR b = new_chars(nullptr, n);
    for (UINT i = 0; i < n; i++)
        b[i] = s[n - 1 - i];
    return b;
}

/* String$: the fill is a variant, a string whose first character is used or the code of an ansi character. */
VB_STD BSTR vb_rtcStringBstr(UINT count, VARIANT *fill)
{
    VARIANT value;
    VbValue(&value, fill);
    if (count >= 0x40000000)
        VbRaise(VBERR_INVALID_CALL);
    OLECHAR c;
    if (V_VT(&value) == VT_BSTR) {
        if (!bstr_chars(V_BSTR(&value)))
            VbRaise(VBERR_INVALID_CALL);
        c = V_BSTR(&value)[0];
    } else {
        LONG code = VbLong(&value);
        char byte = (char)code;
        MultiByteToWideChar(CP_ACP, 0, &byte, 1, &c, 1);
    }
    VariantClear(&value);
    BSTR b = new_chars(nullptr, count);
    for (UINT i = 0; i < count; i++)
        b[i] = c;
    return b;
}

VB_STD VARIANT *vb_rtcStringVar(VARIANT *dest, UINT count, VARIANT *fill)
{
    if (V_VT(fill) == VT_NULL)
        return return_null(dest);
    return return_text(dest, vb_rtcStringBstr(count, fill));
}

VB_STD BSTR vb_rtcBstrFromChar(LONG code)
{
    if (code > 0xFFFF || code < -32768)
        VbRaise(VBERR_INVALID_CALL);
    OLECHAR c = (OLECHAR)code;
    return new_chars(&c, 1);
}

VB_STD BSTR vb_rtcBstrFromByte(char code) { return new_bytes(&code, 1); }

VB_STD VARIANT *vb_rtcVarBstrFromByte(VARIANT *dest, char code) { return return_text(dest, vb_rtcBstrFromByte(code)); }

VB_STD unsigned char vb_rtcByteValueBstr(BSTR s)
{
    if (!s || !SysStringByteLen(s))
        VbRaise(VBERR_INVALID_CALL);
    return (unsigned char)((char *)s)[0];
}

VB_STD short vb_rtcCharValueBstr(BSTR s)
{
    if (!bstr_chars(s))
        VbRaise(VBERR_INVALID_CALL);
    return (short)s[0];
}

/* ------------------------------------------------------------------ search and replacement */

static int lcid_and_mode(LCID compare, LCID *lcid)
{
    if (compare < 2) {
        *lcid = GetUserDefaultLCID() ? GetUserDefaultLCID() : 0x409;
        return (int)compare;
    }
    if (!IsValidLocale(compare, LCID_INSTALLED))
        VbRaise(VBERR_INVALID_CALL);
    *lcid = compare;
    return 1;
}

/* the text mode folds case through the locale, which keeps the lengths. */
static BSTR fold(BSTR s, LCID lcid)
{
    UINT n = bstr_chars(s);
    BSTR b = new_chars(s, n);
    if (n && LCMapStringW(lcid, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, s, (int)n, b, (int)n) != (int)n)
        memcpy(b, s, n * sizeof(OLECHAR));
    return b;
}

static LONG find_from_start(const OLECHAR *s, UINT sl, const OLECHAR *f, UINT fl, UINT from)
{
    for (UINT i = from; i + fl <= sl; i++)
        if (!memcmp(s + i, f, fl * sizeof(OLECHAR)))
            return (LONG)i;
    return -1;
}

VB_STD LONG vb_rtcInStrRev(BSTR s, BSTR match, LONG start, LCID compare)
{
    LCID lcid;
    int mode = lcid_and_mode(compare, &lcid);
    UINT sl = bstr_chars(s), ml = bstr_chars(match);
    if (start == 0 || start < -1)
        VbRaise(VBERR_INVALID_CALL);
    if (start == -1)
        start = (LONG)sl;
    else if ((UINT)start > sl)
        return 0;
    if (!ml)
        return start;
    BSTR hs = s, hm = match;
    if (mode == 1) {
        hs = fold(s, lcid);
        hm = fold(match, lcid);
    }
    LONG found = 0;
    if (ml <= (UINT)start) {
        for (LONG i = start - (LONG)ml; i >= 0; i--)
            if (!memcmp(hs + i, hm, ml * sizeof(OLECHAR))) {
                found = i + 1;
                break;
            }
    }
    if (mode == 1) {
        SysFreeString(hs);
        SysFreeString(hm);
    }
    return found;
}

/* the pieces of a string between the delimiters, at most limit of them, the last one holding the rest. */
static SAFEARRAY *split_pieces(BSTR s, BSTR delimiter, LONG limit, LCID lcid, int mode)
{
    UINT sl = bstr_chars(s), dl = bstr_chars(delimiter);
    BSTR hs = mode == 1 ? fold(s, lcid) : s, hd = mode == 1 ? fold(delimiter, lcid) : delimiter;
    UINT capacity = 8, count = 0;
    UINT *starts = (UINT *)HeapAlloc(GetProcessHeap(), 0, capacity * 2 * sizeof(UINT));
    UINT at = 0;
    if (!starts)
        VbRaise(VBERR_OUT_OF_MEMORY);
    for (;;) {
        LONG hit = -1;
        if ((LONG)count + 1 < limit)
            hit = find_from_start(hs, sl, hd, dl, at);
        if (count == capacity) {
            capacity *= 2;
            UINT *grown = (UINT *)HeapReAlloc(GetProcessHeap(), 0, starts, capacity * 2 * sizeof(UINT));
            if (!grown)
                VbRaise(VBERR_OUT_OF_MEMORY);
            starts = grown;
        }
        starts[count * 2] = at;
        starts[count * 2 + 1] = hit < 0 ? sl - at : (UINT)hit - at;
        count++;
        if (hit < 0)
            break;
        at = (UINT)hit + dl;
    }
    SAFEARRAYBOUND bound = {count, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_BSTR, 1, &bound);
    if (!array)
        VbRaise(VBERR_OUT_OF_MEMORY);
    for (UINT i = 0; i < count; i++) {
        LONG index = (LONG)i;
        BSTR piece = new_chars(s + starts[i * 2], starts[i * 2 + 1]);
        SafeArrayPutElement(array, &index, piece);
        SysFreeString(piece);
    }
    HeapFree(GetProcessHeap(), 0, starts);
    if (mode == 1) {
        SysFreeString(hs);
        SysFreeString(hd);
    }
    return array;
}

static SAFEARRAY *empty_string_array(void)
{
    SAFEARRAYBOUND bound = {0, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_BSTR, 1, &bound);
    if (!array)
        VbRaise(VBERR_OUT_OF_MEMORY);
    return array;
}

VB_STD VARIANT *vb_rtcSplit(VARIANT *dest, BSTR s, VARIANT *delimiter, LONG limit, LCID compare)
{
    LCID lcid;
    int mode = lcid_and_mode(compare, &lcid);
    if (limit == -1)
        limit = 0x7FFFFFFF;
    else if (limit < 0)
        VbRaise(VBERR_INVALID_CALL);
    BSTR d;
    if (VbIsMissing(delimiter))
        d = SysAllocString(L" ");
    else if (!VbText(delimiter, &d))
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    SAFEARRAY *array;
    if (!bstr_chars(d)) {
        SAFEARRAYBOUND bound = {1, 0};
        array = SafeArrayCreate(VT_BSTR, 1, &bound);
        if (!array)
            VbRaise(VBERR_OUT_OF_MEMORY);
        LONG index = 0;
        SafeArrayPutElement(array, &index, s ? s : L"");
    } else if (!bstr_chars(s)) {
        array = empty_string_array();
    } else {
        array = split_pieces(s, d, limit, lcid, mode);
    }
    SysFreeString(d);
    VariantInit(dest);
    V_VT(dest) = VT_ARRAY | VT_BSTR;
    V_ARRAY(dest) = array;
    return dest;
}

static SAFEARRAY *array_of(const VARIANT *v)
{
    if (!(V_VT(v) & VT_ARRAY))
        VbRaise(VBERR_TYPE_MISMATCH);
    return (V_VT(v) & VT_BYREF) ? *V_ARRAYREF(v) : V_ARRAY(v);
}

VB_STD BSTR vb_rtcJoin(VARIANT *array_variant, VARIANT *delimiter)
{
    SAFEARRAY *array = array_of(array_variant);
    if (!array || (array->cDims == 1 && array->rgsabound[0].cElements == 0))
        return nullptr;
    if (array->cDims != 1)
        VbRaise(VBERR_INVALID_CALL);
    VARTYPE vt;
    if (FAILED(SafeArrayGetVartype(array, &vt)) || (vt != VT_BSTR && vt != VT_VARIANT))
        VbRaise(VBERR_INVALID_CALL);
    BSTR d;
    if (VbIsMissing(delimiter))
        d = SysAllocString(L" ");
    else if (!VbText(delimiter, &d))
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    UINT count = array->rgsabound[0].cElements, dl = bstr_chars(d), total = 0;
    BSTR *parts = (BSTR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(BSTR));
    if (!parts)
        VbRaise(VBERR_OUT_OF_MEMORY);
    SafeArrayLock(array);
    for (UINT i = 0; i < count; i++) {
        if (vt == VT_BSTR)
            parts[i] = ((BSTR *)array->pvData)[i] ? SysAllocString(((BSTR *)array->pvData)[i]) : nullptr;
        else if (!VbText(&((VARIANT *)array->pvData)[i], &parts[i])) {
            SafeArrayUnlock(array);
            VbRaise(VBERR_INVALID_USE_OF_NULL);
        }
        total += bstr_chars(parts[i]) + (i + 1 < count ? dl : 0);
    }
    SafeArrayUnlock(array);
    BSTR out = new_chars(nullptr, total), at = out;
    for (UINT i = 0; i < count; i++) {
        UINT n = bstr_chars(parts[i]);
        memcpy(at, parts[i], n * sizeof(OLECHAR));
        at += n;
        if (i + 1 < count) {
            memcpy(at, d, dl * sizeof(OLECHAR));
            at += dl;
        }
        SysFreeString(parts[i]);
    }
    HeapFree(GetProcessHeap(), 0, parts);
    SysFreeString(d);
    return out;
}

VB_STD VARIANT *vb_rtcFilter(VARIANT *dest, VARIANT *source, BSTR match, SHORT include, LCID compare)
{
    LCID lcid;
    int mode = lcid_and_mode(compare, &lcid);
    SAFEARRAY *array = array_of(source);
    VariantInit(dest);
    V_VT(dest) = VT_ARRAY | VT_BSTR;
    V_ARRAY(dest) = nullptr;
    if (!array || !array->pvData)
        return dest;
    if (array->cDims != 1)
        VbRaise(VBERR_INVALID_CALL);
    UINT count = array->rgsabound[0].cElements, ml = bstr_chars(match), kept = 0;
    BSTR fm = mode == 1 ? fold(match, lcid) : match;
    BSTR *picked = (BSTR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(BSTR));
    if (!picked)
        VbRaise(VBERR_OUT_OF_MEMORY);
    VARTYPE vt = 0;
    SafeArrayGetVartype(array, &vt);
    if (vt != VT_BSTR && vt != VT_VARIANT)
        VbRaise(VBERR_INVALID_CALL);
    SafeArrayLock(array);
    for (UINT i = 0; i < count; i++) {
        BSTR item;
        if (vt == VT_BSTR)
            item = SysAllocString(((BSTR *)array->pvData)[i]);
        else if (!VbText(&((VARIANT *)array->pvData)[i], &item))
            continue;
        BSTR folded = mode == 1 ? fold(item, lcid) : item;
        BOOL has = ml == 0 || find_from_start(folded, bstr_chars(folded), fm, ml, 0) >= 0;
        if (mode == 1)
            SysFreeString(folded);
        if (has == (include != 0))
            picked[kept++] = item;
        else
            SysFreeString(item);
    }
    SafeArrayUnlock(array);
    if (mode == 1)
        SysFreeString(fm);
    SAFEARRAYBOUND bound = {kept, 0};
    SAFEARRAY *out = SafeArrayCreate(VT_BSTR, 1, &bound);
    for (UINT i = 0; i < kept; i++) {
        LONG index = (LONG)i;
        SafeArrayPutElement(out, &index, picked[i]);
        SysFreeString(picked[i]);
    }
    HeapFree(GetProcessHeap(), 0, picked);
    V_ARRAY(dest) = out;
    return dest;
}

/* Replace: the expression from start on, with at most count replacements. */
VB_STD BSTR vb_rtcReplace(BSTR expression, BSTR find, BSTR replacement, LONG start, LONG count, LCID compare)
{
    LCID lcid;
    int mode = lcid_and_mode(compare, &lcid);
    if (count == -1)
        count = 0x7FFFFFFF;
    else if (count < 0)
        VbRaise(VBERR_INVALID_CALL);
    if (start <= 0)
        VbRaise(VBERR_INVALID_CALL);
    UINT el = bstr_chars(expression), fl = bstr_chars(find), rl = bstr_chars(replacement);
    if ((UINT)start - 1 > el)
        return nullptr;
    const OLECHAR *tail = expression + (start - 1);
    UINT tl = el - (UINT)(start - 1);
    if (!tl)
        return new_chars(L"", 0);
    if (!fl || !count)
        return new_chars(tail, tl);
    BSTR copy = new_chars(tail, tl);
    BSTR folded = mode == 1 ? fold(copy, lcid) : copy, pattern = mode == 1 ? fold(find, lcid) : find;
    UINT capacity = tl + 16, length = 0;
    OLECHAR *out = (OLECHAR *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(OLECHAR));
    UINT at = 0;
    if (!out)
        VbRaise(VBERR_OUT_OF_MEMORY);
    while (count) {
        LONG hit = find_from_start(folded, tl, pattern, fl, at);
        if (hit < 0)
            break;
        UINT need = length + ((UINT)hit - at) + rl + (tl - ((UINT)hit + fl)) + 1;
        if (need > capacity) {
            capacity = need * 2;
            OLECHAR *grown = (OLECHAR *)HeapReAlloc(GetProcessHeap(), 0, out, capacity * sizeof(OLECHAR));
            if (!grown)
                VbRaise(VBERR_OUT_OF_MEMORY);
            out = grown;
        }
        memcpy(out + length, tail + at, ((UINT)hit - at) * sizeof(OLECHAR));
        length += (UINT)hit - at;
        memcpy(out + length, replacement, rl * sizeof(OLECHAR));
        length += rl;
        at = (UINT)hit + fl;
        count--;
    }
    if (length + (tl - at) + 1 > capacity) {
        capacity = length + (tl - at) + 1;
        OLECHAR *grown = (OLECHAR *)HeapReAlloc(GetProcessHeap(), 0, out, capacity * sizeof(OLECHAR));
        if (!grown)
            VbRaise(VBERR_OUT_OF_MEMORY);
        out = grown;
    }
    memcpy(out + length, tail + at, (tl - at) * sizeof(OLECHAR));
    length += tl - at;
    BSTR result = new_chars(out, length);
    HeapFree(GetProcessHeap(), 0, out);
    if (mode == 1) {
        SysFreeString(folded);
        SysFreeString(pattern);
    }
    SysFreeString(copy);
    return result;
}

/* ------------------------------------------------------------------ StrConv */

static BSTR proper_case(BSTR s)
{
    UINT n = bstr_chars(s);
    BSTR b = new_chars(s, n);
    BOOL word_start = TRUE;
    for (UINT i = 0; i < n; i++) {
        BOOL letter = IsCharAlphaW(b[i]);
        if (letter) {
            OLECHAR c = b[i];
            if (word_start)
                CharUpperBuffW(&c, 1);
            else
                CharLowerBuffW(&c, 1);
            b[i] = c;
        }
        word_start = !letter;
    }
    return b;
}

static UINT ansi_code_page(LCID lcid)
{
    WCHAR text[8];
    if (!GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, text, 2))
        VbRaise(VBERR_INVALID_CALL);
    return *(DWORD *)text;
}

VB_STD VARIANT *vb_rtcStrConvVar2(VARIANT *dest, VARIANT *src, UINT conversion, LCID locale)
{
    LCID lcid;
    if (locale < 2)
        lcid = GetUserDefaultLCID() ? GetUserDefaultLCID() : 0x409;
    else if (locale == 2 || !IsValidLocale(locale, LCID_INSTALLED))
        VbRaise(VBERR_INVALID_CALL);
    else
        lcid = locale;
    if (conversion >= 0x100)
        VbRaise(VBERR_INVALID_CALL);
    VARIANT value, text;
    VbValue(&value, src);
    VariantInit(&text);
    if (V_VT(&value) == VT_NULL) {
        VariantClear(&value);
        return return_null(dest);
    }
    if (V_VT(&value) == (VT_ARRAY | VT_UI1) && !V_ARRAY(&value)) {
        V_VT(&value) = VT_BSTR;
        V_BSTR(&value) = nullptr;
    }
    HRESULT hr = VariantChangeTypeEx(&text, &value, VbLocale(), 0, VT_BSTR);
    VariantClear(&value);
    VbCheck(hr);
    BSTR s = V_BSTR(&text);
    if ((conversion & 0xC0) == 0xC0 || ((conversion & ~0xC0u) && (conversion & 0xC0)))
        VbRaise(VBERR_INVALID_CALL);
    BSTR result;
    if (conversion & 0x40) {
        UINT bytes = s ? SysStringByteLen(s) : 0;
        UINT page = ansi_code_page(lcid);
        int n = bytes ? MultiByteToWideChar(page, 0, (const char *)s, (int)bytes, nullptr, 0) : 0;
        result = new_chars(nullptr, (UINT)n);
        if (n)
            MultiByteToWideChar(page, 0, (const char *)s, (int)bytes, result, n);
    } else if (conversion & 0x80) {
        UINT chars = bstr_chars(s);
        UINT page = ansi_code_page(lcid);
        int n = chars ? WideCharToMultiByte(page, 0, s, (int)chars, nullptr, 0, nullptr, nullptr) : 0;
        char *bytes = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n + 1);
        if (!bytes)
            VbRaise(VBERR_OUT_OF_MEMORY);
        if (n)
            WideCharToMultiByte(page, 0, s, (int)chars, bytes, n, nullptr, nullptr);
        result = new_bytes(bytes, (UINT)n);
        HeapFree(GetProcessHeap(), 0, bytes);
    } else {
        DWORD map = 0;
        UINT primary = lcid & 0x3FF;
        if ((conversion & 0x30) && primary != 0x11)
            VbRaise(VBERR_INVALID_CALL);
        if ((conversion & 0xC) && primary != 0x11 && primary != 0x12 && primary != 4)
            VbRaise(VBERR_INVALID_CALL);
        if ((conversion & 0xC) == 0xC)
            VbRaise(VBERR_INVALID_CALL);
        if (conversion & 4)
            map |= LCMAP_FULLWIDTH;
        if (conversion & 8)
            map |= LCMAP_HALFWIDTH;
        if ((conversion & 0x30) == 0x10)
            map |= LCMAP_KATAKANA;
        else if ((conversion & 0x30) == 0x20)
            map |= LCMAP_HIRAGANA;
        else if ((conversion & 0x30) == 0x30)
            VbRaise(VBERR_INVALID_CALL);
        UINT chars = bstr_chars(s);
        BSTR base = s;
        if ((conversion & 3) == 3)
            base = proper_case(s);
        else if ((conversion & 3) == 1)
            map |= LCMAP_UPPERCASE;
        else if ((conversion & 3) == 2)
            map |= LCMAP_LOWERCASE;
        if (!map) {
            result = new_chars(base, chars);
        } else {
            int n = chars ? LCMapStringW(lcid, map, base, (int)chars, nullptr, 0) : 0;
            result = new_chars(nullptr, (UINT)n);
            if (n)
                LCMapStringW(lcid, map, base, (int)chars, result, n);
        }
        if (base != s)
            SysFreeString(base);
    }
    VariantClear(&text);
    return return_text(dest, result);
}

VB_STD VARIANT *vb_rtcStrConvVar(VARIANT *dest, VARIANT *src, UINT conversion)
{
    return vb_rtcStrConvVar2(dest, src, conversion, 0);
}

/* ------------------------------------------------------------------ the type tests */

static VARTYPE type_of(const VARIANT *v)
{
    while (V_VT(v) == (VT_BYREF | VT_VARIANT))
        v = V_VARIANTREF(v);
    return V_VT(v);
}

VB_STD LONG vb_rtcIsArray(VARIANT *v) { return (type_of(v) & VT_ARRAY) ? -1 : 0; }
VB_STD LONG vb_rtcIsEmpty(VARIANT *v) { return type_of(v) == VT_EMPTY ? -1 : 0; }
VB_STD LONG vb_rtcIsError(VARIANT *v) { return type_of(v) == VT_ERROR ? -1 : 0; }
VB_STD LONG vb_rtcIsNull(VARIANT *v) { return type_of(v) == VT_NULL ? -1 : 0; }
VB_STD LONG vb_rtcIsObject(VARIANT *v) { return (V_VT(v) & ~VT_BYREF) == VT_DISPATCH ? -1 : 0; }
VB_STD LONG vb_rtcVarType(VARIANT *v) { return type_of(v); }
VB_STD SHORT vb_rtcIsMissing(VARIANT *v) { return VbIsMissing(v) ? -1 : 0; }

VB_STD SHORT vb_rtcIsDate(VARIANT *v)
{
    VARTYPE vt = V_VT(v);
    if (vt & VT_ARRAY)
        return 0;
    vt &= ~VT_BYREF;
    if (vt == VT_DATE)
        return -1;
    VARIANT value;
    if (vt == VT_DISPATCH) {
        VbValue(&value, v);
        vt = V_VT(&value);
        if (vt == VT_DATE)
            return -1;
    } else {
        VariantInit(&value);
        if (FAILED(VariantCopyInd(&value, v)))
            return 0;
    }
    SHORT result = 0;
    if (V_VT(&value) == VT_BSTR && V_BSTR(&value)) {
        DATE date;
        LCID lcid = GetUserDefaultLCID() ? GetUserDefaultLCID() : 0x409;
        result = SUCCEEDED(VarDateFromStr(V_BSTR(&value), lcid, LOCALE_NOUSEROVERRIDE & 0, &date)) ? -1 : 0;
    }
    VariantClear(&value);
    return result;
}

VB_STD VARIANT *vb_rtcSgnVar(VARIANT *dest, VARIANT *src)
{
    VARIANT v;
    VbValue(&v, src);
    VariantInit(dest);
    V_VT(dest) = VT_I2;
    switch (V_VT(&v)) {
    case VT_EMPTY: V_I2(dest) = 0; break;
    case VT_NULL: V_VT(dest) = VT_NULL; break;
    case VT_I2:
    case VT_BOOL: V_I2(dest) = V_I2(&v) < 0 ? -1 : V_I2(&v) > 0; break;
    case VT_I4: V_I2(dest) = V_I4(&v) < 0 ? -1 : V_I4(&v) > 0; break;
    case VT_R4: V_I2(dest) = V_R4(&v) < 0 ? -1 : V_R4(&v) > 0; break;
    case VT_R8:
    case VT_DATE: V_I2(dest) = V_R8(&v) < 0 ? -1 : V_R8(&v) > 0; break;
    case VT_CY: V_I2(dest) = V_CY(&v).int64 < 0 ? -1 : V_CY(&v).int64 > 0; break;
    case VT_UI1: V_I2(dest) = V_UI1(&v) != 0; break;
    case VT_DECIMAL: V_I2(dest) = (V_DECIMAL(&v).Hi32 | V_DECIMAL(&v).Lo64) ? ((V_DECIMAL(&v).sign & 0x80) ? -1 : 1) : 0; break;
    case VT_BSTR: {
        VARIANT number;
        VariantInit(&number);
        VbCheck(VariantChangeTypeEx(&number, &v, VbLocale(), 0, VT_R8));
        V_I2(dest) = V_R8(&number) < 0 ? -1 : V_R8(&number) > 0;
        break;
    }
    case VT_ERROR:
    case VT_DISPATCH:
    case VT_UNKNOWN: VbRaise(VBERR_TYPE_MISMATCH);
    default: VbRaise(VBERR_VARIABLE_TYPE);
    }
    VariantClear(&v);
    return dest;
}

VB_STD VARIANT *vb_rtcLenVar(VARIANT *dest, VARIANT *src)
{
    VARIANT v;
    VbValue(&v, src);
    VariantInit(dest);
    if (V_VT(&v) == VT_NULL) {
        V_VT(dest) = VT_NULL;
        return dest;
    }
    V_VT(dest) = VT_I4;
    if (V_VT(&v) == VT_BSTR) {
        V_I4(dest) = (LONG)SysStringByteLen(V_BSTR(&v));
    } else {
        VARIANT text;
        VariantInit(&text);
        VbCheck(VariantChangeTypeEx(&text, &v, VbLocale(), 0, VT_BSTR));
        V_I4(dest) = (LONG)SysStringByteLen(V_BSTR(&text));
        VariantClear(&text);
    }
    VariantClear(&v);
    return dest;
}

VB_STD VARIANT *vb_rtcLenCharVar(VARIANT *dest, VARIANT *src)
{
    VARIANT v;
    VbValue(&v, src);
    VariantInit(dest);
    if (V_VT(&v) == VT_NULL) {
        V_VT(dest) = VT_NULL;
        return dest;
    }
    BSTR s;
    VbText(&v, &s);
    V_VT(dest) = VT_I4;
    V_I4(dest) = (LONG)bstr_chars(s);
    SysFreeString(s);
    return dest;
}

VB_STD VARIANT *vb_rtcVarFromVar(VARIANT *dest, VARIANT *src)
{
    VariantInit(dest);
    VbCheck(VariantCopyInd(dest, src));
    return dest;
}

VB_STD BSTR vb_rtcTypeName(VARIANT *v)
{
    static const WCHAR *const names[] = {L"Empty", L"Null", L"Integer", L"Long", L"Single", L"Double", L"Currency", L"Date", L"String",
                                         L"Object", L"Error", L"Boolean", L"Variant", L"Object", L"Decimal"};
    VARTYPE vt = V_VT(v);
    BOOL array = (vt & VT_ARRAY) != 0;
    VARTYPE base = vt & ~(VT_ARRAY | VT_BYREF);
    const WCHAR *name;
    if (base == VT_UI1)
        name = L"Byte";
    else if (base <= VT_DECIMAL)
        name = names[base];
    else
        VbRaise(VBERR_VARIABLE_TYPE);
    if (base == VT_DISPATCH || base == VT_UNKNOWN) {
        IUnknown *object = nullptr;
        if (!array) {
            object = (vt & VT_BYREF) ? *V_UNKNOWNREF(v) : V_UNKNOWN(v);
            if (!object)
                name = L"Nothing";
            else {
                ITypeInfo *info = nullptr;
                IDispatch *dispatch = nullptr;
                if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IDispatch, (void **)&dispatch))) {
                    if (SUCCEEDED(dispatch->lpVtbl->GetTypeInfo(dispatch, 0, 1033, &info))) {
                        BSTR type_name = nullptr;
                        if (SUCCEEDED(info->lpVtbl->GetDocumentation(info, -1, &type_name, nullptr, nullptr, nullptr)) && type_name) {
                            info->lpVtbl->Release(info);
                            dispatch->lpVtbl->Release(dispatch);
                            if (type_name[0] == L'_') {
                                BSTR trimmed = new_chars(type_name + 1, SysStringLen(type_name) - 1);
                                SysFreeString(type_name);
                                return trimmed;
                            }
                            return type_name;
                        }
                        info->lpVtbl->Release(info);
                    }
                    dispatch->lpVtbl->Release(dispatch);
                }
            }
        }
    }
    if (!array)
        return new_chars(name, (UINT)lstrlenW(name));
    UINT n = (UINT)lstrlenW(name);
    BSTR b = new_chars(nullptr, n + 2);
    memcpy(b, name, n * sizeof(OLECHAR));
    b[n] = L'(';
    b[n + 1] = L')';
    return b;
}

VB_STD LONG vb_rtcQBColor(SHORT index)
{
    static const LONG colors[16] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
                                    0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    if (index & 0xFFF0)
        VbRaise(VBERR_INVALID_CALL);
    return colors[index];
}

/* Round of a variant to a number of decimals, half to even as oleaut32 does. */
VB_STD VARIANT *vb_rtcRound(VARIANT *dest, VARIANT *src, LONG decimals)
{
    VARIANT v;
    VbValue(&v, src);
    VariantInit(dest);
    VbCheck(VarRound(&v, decimals, dest));
    VariantClear(&v);
    return dest;
}

