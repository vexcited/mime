/*
 * strings: the BSTR helpers the compiler calls for assignments, concatenation, comparison and the conversions between
 * numbers and text, and the rtc functions of the string library of Visual Basic.
 */
#include "vb.h"

static BSTR alloc_len(const OLECHAR *s, UINT n)
{
    BSTR b = SysAllocStringLen(s, n);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

/* a copy of a string, which is nullptr for an empty one. */
static BSTR copy_bstr(BSTR s)
{
    if (!s)
        return nullptr;
    BSTR b = SysAllocStringByteLen((const char *)s, SysStringByteLen(s));
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

/* ------------------------------------------------------------------ freeing and moving */

VB_THIS void vb___vbaFreeStr(BSTR *s)
{
    if (*s) {
        SysFreeString(*s);
        *s = nullptr;
    }
}

VB_C void vb___vbaFreeStrList(unsigned int count, BSTR *first, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, first);
    BSTR *p = first;
    for (unsigned int i = 0; i < count; i++) {
        if (*p) {
            SysFreeString(*p);
            *p = nullptr;
        }
        if (i + 1 < count)
            p = __builtin_va_arg(ap, BSTR *);
    }
    __builtin_va_end(ap);
}

VB_THIS int vb___vbaFreeObj(IUnknown **obj)
{
    IUnknown *o = *obj;
    if (o) {
        o->lpVtbl->Release(o);
        *obj = nullptr;
    }
    return 0;
}

VB_C int vb___vbaFreeObjList(unsigned int count, IUnknown **first, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, first);
    IUnknown **p = first;
    for (unsigned int i = 0; i < count; i++) {
        if (*p) {
            (*p)->lpVtbl->Release(*p);
            *p = nullptr;
        }
        if (i + 1 < count)
            p = __builtin_va_arg(ap, IUnknown **);
    }
    __builtin_va_end(ap);
    return 0;
}

VB_FAST BSTR vb___vbaStrCopy(BSTR *dst, BSTR src)
{
    BSTR b = copy_bstr(src);
    if (*dst)
        SysFreeString(*dst);
    *dst = b;
    return b;
}

VB_FAST BSTR vb___vbaStrMove(BSTR *dst, BSTR src)
{
    if (*dst)
        SysFreeString(*dst);
    *dst = src;
    return src;
}

/* ------------------------------------------------------------------ concatenation and comparison */

/* the first argument is the right operand, since the compiler pushes the left one first. */
VB_STD BSTR vb___vbaStrCat(BSTR right, BSTR left)
{
    BSTR out = nullptr;
    HRESULT hr = VarBstrCat(left, right, &out);
    if (FAILED(hr))
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return out;
}

static int compare(int text, BSTR right, BSTR left)
{
    if (!left && !right)
        return 0;
    HRESULT hr = VarBstrCmp(left, right, VbLocale(), text ? (NORM_IGNORECASE | NORM_IGNOREKANATYPE | NORM_IGNOREWIDTH) : 0);
    if (FAILED(hr))
        VbCheck(hr);
    return (int)hr - 1;
}

VB_STD int vb___vbaStrCmp(BSTR right, BSTR left)
{
    if (left == right)
        return 0;
    /* a binary comparison of the characters, as visual basic does unless it was told to compare text. */
    UINT ln = left ? SysStringLen(left) : 0, rn = right ? SysStringLen(right) : 0;
    UINT n = ln < rn ? ln : rn;
    for (UINT i = 0; i < n; i++) {
        if (left[i] != right[i])
            return left[i] < right[i] ? -1 : 1;
    }
    return ln == rn ? 0 : (ln < rn ? -1 : 1);
}

VB_STD int vb___vbaStrTextCmp(BSTR right, BSTR left) { return compare(1, right, left); }

VB_STD int vb___vbaLenBstr(BSTR s) { return s ? (int)(*((DWORD *)s - 1) >> 1) : 0; }

VB_STD BSTR vb___vbaStrFixstr(UINT length, OLECHAR *s) { return alloc_len(s, length); }

VB_STD int vb___vbaFixstrConstruct(UINT length, BSTR *out)
{
    BSTR b = alloc_len(nullptr, length);
    for (UINT i = 0; i < length; i++)
        b[i] = 0;
    *out = b;
    return 0;
}

VB_STD void *vb___vbaCopyBytes(unsigned int count, void *dst, const void *src)
{
    memcpy(dst, src, count);
    return dst;
}

/* ------------------------------------------------------------------ ansi and unicode */

/* a unicode string becomes a string of the ansi bytes, as the arguments of a Declare of a function want. */
VB_STD BSTR vb___vbaStrToAnsi(BSTR *dst, BSTR src)
{
    if (*dst)
        SysFreeString(*dst);
    *dst = nullptr;
    if (src) {
        UINT n = SysStringLen(src);
        int bytes = n ? WideCharToMultiByte(CP_ACP, 0, src, (int)n, nullptr, 0, nullptr, nullptr) : 0;
        char *tmp = HeapAlloc(GetProcessHeap(), 0, bytes + 1);
        if (!tmp)
            VbRaise(VBERR_OUT_OF_STRING_SPACE);
        if (bytes)
            WideCharToMultiByte(CP_ACP, 0, src, (int)n, tmp, bytes, nullptr, nullptr);
        *dst = SysAllocStringByteLen(tmp, (UINT)bytes);
        HeapFree(GetProcessHeap(), 0, tmp);
        if (!*dst)
            VbRaise(VBERR_OUT_OF_STRING_SPACE);
    }
    return *dst;
}

VB_STD BSTR vb___vbaStrToUnicode(BSTR *dst, BSTR src)
{
    if (*dst)
        SysFreeString(*dst);
    *dst = nullptr;
    if (src) {
        UINT bytes = SysStringByteLen(src);
        int n = bytes ? MultiByteToWideChar(CP_ACP, 0, (const char *)src, (int)bytes, nullptr, 0) : 0;
        *dst = alloc_len(nullptr, (UINT)n);
        if (n)
            MultiByteToWideChar(CP_ACP, 0, (const char *)src, (int)bytes, *dst, n);
    }
    return *dst;
}

VB_STD BSTR vb_rtcBstrFromAnsi(const char *ansi)
{
    if (!ansi)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    BSTR b = alloc_len(nullptr, (UINT)(n > 0 ? n - 1 : 0));
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, ansi, -1, b, n);
    return b;
}

VB_STD short vb_rtcAnsiValueBstr(BSTR s)
{
    if (!s || !SysStringLen(s))
        VbRaise(VBERR_INVALID_CALL);
    char c[4] = {};
    int n = WideCharToMultiByte(CP_ACP, 0, s, 1, c, 4, nullptr, nullptr);
    if (n >= 2)
        return (short)(((unsigned char)c[0] << 8) | (unsigned char)c[1]);
    return (short)(unsigned char)c[0];
}

/* ------------------------------------------------------------------ numbers to text and back */

VB_THIS BSTR vb___vbaStrI2(void *out, short v)
{
    BSTR b;
    VbCheck(VarBstrFromI2(v, VbLocale(), 0, &b));
    return b;
}

VB_THIS BSTR vb___vbaStrI4(void *out, LONG v)
{
    BSTR b;
    VbCheck(VarBstrFromI4(v, VbLocale(), 0, &b));
    return b;
}

VB_THIS BSTR vb___vbaStrUI1(void *out, unsigned char v)
{
    BSTR b;
    VbCheck(VarBstrFromUI1(v, VbLocale(), 0, &b));
    return b;
}

VB_THIS BSTR vb___vbaStrR4(void *out, float v)
{
    BSTR b;
    VbCheck(VarBstrFromR4(v, VbLocale(), 0, &b));
    return b;
}

VB_THIS BSTR vb___vbaStrR8(void *out, double v)
{
    BSTR b;
    VbCheck(VarBstrFromR8(v, VbLocale(), 0, &b));
    return b;
}

VB_STD double vb___vbaR8Str(const OLECHAR *s)
{
    double d;
    VbCheck(VarR8FromStr(s, VbLocale(), 0, &d));
    return d;
}

VB_THIS LONG vb___vbaI4Str(void *unused, const OLECHAR *s)
{
    LONG v;
    VbCheck(VarI4FromStr(s, VbLocale(), 0, &v));
    return v;
}

VB_THIS short vb___vbaI2Str(void *unused, const OLECHAR *s)
{
    SHORT v;
    VbCheck(VarI2FromStr(s, VbLocale(), 0, &v));
    return v;
}

VB_STD unsigned char vb___vbaUI1Str(const OLECHAR *s)
{
    BYTE v;
    VbCheck(VarUI1FromStr(s, VbLocale(), 0, &v));
    return v;
}

VB_STD double vb_rtcR8ValFromBstr(BSTR s)
{
    /* Val reads as much of the text as is a number and ignores the rest. */
    double v = 0;
    if (!s)
        return 0;
    char buf[64];
    int n = 0;
    BOOL point = FALSE, expo = FALSE;
    for (UINT i = 0; i < SysStringLen(s) && n < 62; i++) {
        OLECHAR c = s[i];
        if (c == ' ' && n == 0)
            continue;
        if ((c >= '0' && c <= '9') || ((c == '-' || c == '+') && (n == 0 || buf[n - 1] == 'e')) || (c == '.' && !point && !expo)) {
            if (c == '.')
                point = TRUE;
            buf[n++] = (char)c;
        } else if ((c == 'e' || c == 'E') && n > 0 && !expo) {
            expo = TRUE;
            buf[n++] = 'e';
        } else {
            break;
        }
    }
    buf[n] = 0;
    WCHAR w[64];
    for (int i = 0; i <= n; i++)
        w[i] = (WCHAR)buf[i];
    if (FAILED(VarR8FromStr(w, 0x0409, 0, &v)))
        v = 0;
    return v;
}

/* ------------------------------------------------------------------ trimming, case, padding and slicing */

static BSTR trim(BSTR s, BOOL left, BOOL right)
{
    if (!s)
        return nullptr;
    UINT n = SysStringLen(s), a = 0, b = n;
    if (left)
        while (a < b && s[a] == ' ')
            a++;
    if (right)
        while (b > a && s[b - 1] == ' ')
            b--;
    return alloc_len(s + a, b - a);
}

VB_STD BSTR vb_rtcTrimBstr(BSTR s) { return trim(s, TRUE, TRUE); }
VB_STD BSTR vb_rtcLeftTrimBstr(BSTR s) { return trim(s, TRUE, FALSE); }

VB_STD BSTR vb_rtcUpperCaseBstr(BSTR s)
{
    if (!s)
        return nullptr;
    BSTR b = alloc_len(s, SysStringLen(s));
    CharUpperBuffW(b, SysStringLen(b));
    return b;
}

VB_STD BSTR vb_rtcSpaceBstr(UINT n)
{
    BSTR b = alloc_len(nullptr, n);
    for (UINT i = 0; i < n; i++)
        b[i] = ' ';
    return b;
}

VB_STD BSTR vb_rtcLeftCharBstr(BSTR s, LONG n)
{
    if (n < 0)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringLen(s) : 0;
    return alloc_len(s, (UINT)n < len ? (UINT)n : len);
}

VB_STD BSTR vb_rtcRightCharBstr(BSTR s, LONG n)
{
    if (n < 0)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringLen(s) : 0;
    UINT take = (UINT)n < len ? (UINT)n : len;
    return alloc_len(s + (len - take), take);
}

/* the length is an optional variant: missing or empty gives the rest of the string. */
VB_STD BSTR vb_rtcMidCharBstr(BSTR s, LONG start, VARIANT *length)
{
    if (start < 1)
        VbRaise(VBERR_INVALID_CALL);
    UINT len = s ? SysStringLen(s) : 0;
    if ((UINT)start > len)
        return alloc_len(nullptr, 0);
    UINT avail = len - (UINT)start + 1, take = avail;
    if (length && V_VT(length) != VT_ERROR && V_VT(length) != VT_EMPTY) {
        VARIANT tmp;
        VariantInit(&tmp);
        VbCheck(VariantChangeTypeEx(&tmp, length, VbLocale(), 0, VT_I4));
        if (V_I4(&tmp) < 0)
            VbRaise(VBERR_INVALID_CALL);
        if ((UINT)V_I4(&tmp) < take)
            take = (UINT)V_I4(&tmp);
    }
    return alloc_len(s + start - 1, take);
}

VB_STD LONG vb___vbaInStr(LONG locale_mode, BSTR find, BSTR haystack, LONG start)
{
    UINT hl = haystack ? SysStringLen(haystack) : 0, fl = find ? SysStringLen(find) : 0;
    if (start < 1)
        VbRaise(VBERR_INVALID_CALL);
    if (!fl)
        return (UINT)start <= hl ? start : 0;
    for (UINT i = (UINT)start - 1; i + fl <= hl; i++) {
        BOOL same = TRUE;
        for (UINT j = 0; j < fl && same; j++) {
            OLECHAR a = haystack[i + j], b = find[j];
            if (locale_mode == 1) {
                a = (OLECHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)a);
                b = (OLECHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)b);
            }
            same = a == b;
        }
        if (same)
            return (LONG)i + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ the variant forms of the string functions */

static BSTR variant_text(const VARIANT *v)
{
    VARIANT tmp;
    VariantInit(&tmp);
    VbCheck(VariantChangeTypeEx(&tmp, v, VbLocale(), 0, VT_BSTR));
    BSTR b = V_BSTR(&tmp);
    V_VT(&tmp) = VT_EMPTY;
    return b;
}

static VARIANT *return_text(VARIANT *dest, BSTR b)
{
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = b;
    return dest;
}

VB_STD VARIANT *vb_rtcTrimVar(VARIANT *dest, VARIANT *src)
{
    BSTR s = variant_text(src);
    BSTR r = trim(s, TRUE, TRUE);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcLeftTrimVar(VARIANT *dest, VARIANT *src)
{
    BSTR s = variant_text(src);
    BSTR r = trim(s, TRUE, FALSE);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcRightTrimVar(VARIANT *dest, VARIANT *src)
{
    BSTR s = variant_text(src);
    BSTR r = trim(s, FALSE, TRUE);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcUpperCaseVar(VARIANT *dest, VARIANT *src)
{
    BSTR s = variant_text(src);
    BSTR r = vb_rtcUpperCaseBstr(s);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcLowerCaseVar(VARIANT *dest, VARIANT *src)
{
    BSTR s = variant_text(src);
    BSTR r = s ? alloc_len(s, SysStringLen(s)) : nullptr;
    if (r)
        CharLowerBuffW(r, SysStringLen(r));
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcSpaceVar(VARIANT *dest, UINT n) { return return_text(dest, vb_rtcSpaceBstr(n)); }

VB_STD VARIANT *vb_rtcLeftCharVar(VARIANT *dest, VARIANT *src, LONG n)
{
    BSTR s = variant_text(src);
    BSTR r = vb_rtcLeftCharBstr(s, n);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcRightCharVar(VARIANT *dest, VARIANT *src, LONG n)
{
    BSTR s = variant_text(src);
    BSTR r = vb_rtcRightCharBstr(s, n);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD VARIANT *vb_rtcMidCharVar(VARIANT *dest, VARIANT *src, LONG start, VARIANT *length)
{
    BSTR s = variant_text(src);
    BSTR r = vb_rtcMidCharBstr(s, start, length);
    SysFreeString(s);
    return return_text(dest, r);
}

VB_STD BSTR vb_rtcStrFromVar(VARIANT *v) { return variant_text(v); }

VB_STD VARIANT *vb_rtcVarStrFromVar(VARIANT *dest, VARIANT *v) { return return_text(dest, variant_text(v)); }

VB_STD VARIANT *vb_rtcVarBstrFromAnsi(VARIANT *dest, const char *ansi) { return return_text(dest, vb_rtcBstrFromAnsi(ansi)); }

VB_STD VARIANT *vb_rtcVarBstrFromChar(VARIANT *dest, LONG code)
{
    OLECHAR c = (OLECHAR)code;
    return return_text(dest, alloc_len(&c, 1));
}

VB_STD LONG vb_rtcRgb(short r, short g, short b)
{
    if (r < 0 || g < 0 || b < 0)
        VbRaise(VBERR_INVALID_CALL);
    return (r > 255 ? 255 : r) | ((g > 255 ? 255 : g) << 8) | ((b > 255 ? 255 : b) << 16);
}

VB_STD short vb_rtcIsNumeric(VARIANT *v)
{
    VARIANT tmp;
    VariantInit(&tmp);
    if (V_VT(v) == VT_BSTR || (V_VT(v) & VT_BYREF)) {
        HRESULT hr = VariantChangeTypeEx(&tmp, v, 0x0409, 0, VT_R8);
        VariantClear(&tmp);
        return SUCCEEDED(hr) ? -1 : 0;
    }
    switch (V_VT(v) & VT_TYPEMASK) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DECIMAL: case VT_UI1: case VT_BOOL: case VT_EMPTY: case VT_I1:
    case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT:
        return -1;
    default:
        return 0;
    }
}
