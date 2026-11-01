/*
 * the functions and constants of vbscript, and Eval and Execute.
 */
#include "vbs.h"

#define FN(name) static int fn_##name(Frame *f, VARIANT *a, UINT n, VARIANT *r)

static BOOL missing(VARIANT *a, UINT n, UINT i)
{
    return i >= n || (a[i].vt == VT_ERROR && a[i].scode == DISP_E_PARAMNOTFOUND);
}

static VARIANT *plain(VARIANT *v)
{
    while ((v->vt & VT_BYREF) && (v->vt & VT_TYPEMASK) == VT_VARIANT && v->pvarVal)
        v = v->pvarVal;
    return v;
}

static int fail(Frame *f, LONG number, PCWSTR name)
{
    return raise_error(f, number, name);
}

/* converts an argument, naming the function when it cannot be. null is for the callers to take first. */
static int conv(Frame *f, VARIANT *v, VARTYPE vt, VARIANT *out, PCWSTR name)
{
    v = plain(v);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, name);
    VariantInit(out);
    HRESULT hr = VariantChangeTypeEx(out, v, f->E->Lcid, vt == VT_BSTR ? VARIANT_ALPHABOOL : 0, vt);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, name);
    return R_OK;
}

static int get_long(Frame *f, VARIANT *v, LONG *out, PCWSTR name)
{
    VARIANT t;
    int rc = conv(f, v, VT_I4, &t, name);
    if (rc == R_OK)
        *out = t.lVal;
    return rc;
}

static int get_double(Frame *f, VARIANT *v, double *out, PCWSTR name)
{
    VARIANT t;
    int rc = conv(f, v, VT_R8, &t, name);
    if (rc == R_OK)
        *out = t.dblVal;
    return rc;
}

/* a string argument; the bstr is the caller's to free. */
static int get_str(Frame *f, VARIANT *v, BSTR *out, PCWSTR name)
{
    VARIANT t;
    int rc = conv(f, v, VT_BSTR, &t, name);
    if (rc == R_OK)
        *out = t.bstrVal;
    return rc;
}

static void ret_str(VARIANT *r, PCWSTR s, SIZE_T len)
{
    r->vt = VT_BSTR;
    r->bstrVal = SysAllocStringLen(s, (UINT)len);
}

static void ret_long(VARIANT *r, LONG v)
{
    r->vt = VT_I4;
    r->lVal = v;
}

static void ret_int(VARIANT *r, int v)
{
    if (v >= -32768 && v <= 32767) {
        r->vt = VT_I2;
        r->iVal = (SHORT)v;
    } else {
        r->vt = VT_I4;
        r->lVal = v;
    }
}

static void ret_bool(VARIANT *r, BOOL v)
{
    r->vt = VT_BOOL;
    r->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
}

static void ret_null(VARIANT *r)
{
    r->vt = VT_NULL;
}

/* ------------------------------------------------------------------ conversions */

static int convert_to(Frame *f, VARIANT *a, VARIANT *r, VARTYPE vt, PCWSTR name)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, name);
    HRESULT hr = VariantChangeTypeEx(r, v, f->E->Lcid, vt == VT_BSTR ? VARIANT_ALPHABOOL : 0, vt);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, name);
    return R_OK;
}

FN(cbool) { return convert_to(f, a, r, VT_BOOL, L"CBool"); }
FN(cbyte) { return convert_to(f, a, r, VT_UI1, L"CByte"); }
FN(ccur) { return convert_to(f, a, r, VT_CY, L"CCur"); }
FN(cdate) { return convert_to(f, a, r, VT_DATE, L"CDate"); }
FN(cdbl) { return convert_to(f, a, r, VT_R8, L"CDbl"); }
FN(cint) { return convert_to(f, a, r, VT_I2, L"CInt"); }
FN(clng) { return convert_to(f, a, r, VT_I4, L"CLng"); }
FN(csng) { return convert_to(f, a, r, VT_R4, L"CSng"); }
FN(cstr) { return convert_to(f, a, r, VT_BSTR, L"CStr"); }

FN(int)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    HRESULT hr = VarInt(v, r);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, L"Int");
    return R_OK;
}

FN(fix)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    HRESULT hr = VarFix(v, r);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, L"Fix");
    return R_OK;
}

FN(abs)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    if (v->vt == VT_I2 && v->iVal == -32768) {
        ret_long(r, 32768);
        return R_OK;
    }
    if (v->vt == VT_I4 && v->lVal == (LONG)0x80000000) {
        r->vt = VT_R8;
        r->dblVal = 2147483648.0;
        return R_OK;
    }
    HRESULT hr = VarAbs(v, r);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, L"Abs");
    return R_OK;
}

FN(sgn)
{
    VARIANT *v = plain(a);
    double d;
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_double(f, v, &d, L"Sgn");
    if (rc != R_OK)
        return rc;
    ret_int(r, d > 0 ? 1 : d < 0 ? -1 : 0);
    return R_OK;
}

FN(round)
{
    VARIANT *v = plain(&a[0]);
    LONG digits = 0;
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Round");
    if (!missing(a, n, 1)) {
        int rc = get_long(f, &a[1], &digits, L"Round");
        if (rc != R_OK)
            return rc;
        if (digits < 0)
            return fail(f, E_VBS_INVALID_CALL, L"Round");
    }
    VARIANT src;
    VariantInit(&src);
    if (v->vt == VT_BSTR || v->vt == VT_EMPTY || v->vt == VT_BOOL) {
        int rc = conv(f, v, v->vt == VT_BSTR ? VT_R8 : VT_I2, &src, L"Round");
        if (rc != R_OK)
            return rc;
        if (v->vt == VT_BOOL)
            src.vt = VT_I2;
        v = &src;
    }
    HRESULT hr = VarRound(v, digits, r);
    if (FAILED(hr))
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : hr == DISP_E_TYPEMISMATCH ? E_VBS_TYPE_MISMATCH : E_VBS_INVALID_CALL, L"Round");
    return R_OK;
}

static int math_arg(Frame *f, VARIANT *a, double *d, PCWSTR name)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    return get_double(f, v, d, name);
}

static int ret_double(Frame *f, VARIANT *r, double d, PCWSTR name)
{
    if (d != d || d > 1.7976931348623157e308 || d < -1.7976931348623157e308)
        return fail(f, E_VBS_OVERFLOW, name);
    r->vt = VT_R8;
    r->dblVal = d;
    return R_OK;
}

FN(sqr)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Sqr");
    if (rc != R_OK)
        return rc;
    if (d < 0)
        return fail(f, E_VBS_INVALID_CALL, L"Sqr");
    return ret_double(f, r, sqrt(d), L"Sqr");
}

FN(exp)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Exp");
    if (rc != R_OK)
        return rc;
    return ret_double(f, r, exp(d), L"Exp");
}

FN(log)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Log");
    if (rc != R_OK)
        return rc;
    if (d <= 0)
        return fail(f, E_VBS_INVALID_CALL, L"Log");
    return ret_double(f, r, log(d), L"Log");
}

FN(sin)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Sin");
    return rc != R_OK ? rc : ret_double(f, r, sin(d), L"Sin");
}

FN(cos)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Cos");
    return rc != R_OK ? rc : ret_double(f, r, cos(d), L"Cos");
}

FN(tan)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Tan");
    return rc != R_OK ? rc : ret_double(f, r, tan(d), L"Tan");
}

FN(atn)
{
    double d;
    int rc = math_arg(f, &a[0], &d, L"Atn");
    return rc != R_OK ? rc : ret_double(f, r, atan(d), L"Atn");
}

static ULONG g_rnd_seed = 0x50000;
static float g_rnd_last;

FN(rnd)
{
    float x = 1;
    if (!missing(a, n, 0)) {
        double d;
        int rc = math_arg(f, &a[0], &d, L"Rnd");
        if (rc != R_OK)
            return rc;
        x = (float)d;
    }
    if (x < 0) {
        ULONG l;
        memcpy(&l, &x, 4);
        g_rnd_seed = (l & 0xFFFFFF) + ((l >> 24) & 0xFF);
    }
    if (x != 0) {
        g_rnd_seed = (g_rnd_seed * 0x43FD43FD + 0xC39EC3) & 0xFFFFFF;
        g_rnd_last = (float)g_rnd_seed / 16777216.0f;
    }
    r->vt = VT_R4;
    r->fltVal = g_rnd_last;
    return R_OK;
}

int vbs_randomize(Frame *f, VARIANT *a, UINT n)
{
    double t = (double)GetTickCount() / 1000.0;
    if (n) {
        double d;
        int rc = get_double(f, &a[0], &d, L"Randomize");
        if (rc != R_OK)
            return rc;
        t = d;
    }
    float ft = (float)t;
    ULONG l;
    memcpy(&l, &ft, 4);
    g_rnd_seed = ((g_rnd_seed & 0xFF) | ((l & 0xFFFF) << 8)) & 0xFFFFFF;
    return R_OK;
}

static int radix(Frame *f, VARIANT *a, VARIANT *r, int base, PCWSTR name)
{
    VARIANT *v = plain(a);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    VARIANT t;
    VariantInit(&t);
    HRESULT hr = VariantChangeTypeEx(&t, v, f->E->Lcid, 0, VT_R8);
    if (FAILED(hr)) {
        WCHAR d[80];
        describe_value(v, d);
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    }
    double dv = t.dblVal;
    double rounded = floor(dv + 0.5);
    if (rounded - dv == 0.5 || dv + 0.5 == rounded) {
        if (fmod(rounded, 2) != 0)
            rounded -= 1;
    }
    if (rounded > 2147483647.0 || rounded < -2147483648.0) {
        WCHAR d[80];
        describe_value(v, d);
        return fail(f, E_VBS_OVERFLOW, d);
    }
    LONG l = (LONG)rounded;
    ULONG u = (ULONG)l;
    if (l >= -32768 && l < 0)
        u &= 0xFFFF;
    WCHAR buffer[40];
    int len = 0;
    WCHAR tmp[40];
    if (u == 0)
        tmp[len++] = L'0';
    while (u) {
        int digit = u % base;
        tmp[len++] = digit < 10 ? L'0' + digit : L'A' + digit - 10;
        u /= base;
    }
    for (int i = 0; i < len; i++)
        buffer[i] = tmp[len - 1 - i];
    ret_str(r, buffer, len);
    return R_OK;
}

FN(hex) { return radix(f, &a[0], r, 16, L"Hex"); }
FN(oct) { return radix(f, &a[0], r, 8, L"Oct"); }

/* ------------------------------------------------------------------ strings */

FN(len)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    if (v->vt == VT_DISPATCH || v->vt == VT_UNKNOWN)
        return fail(f, E_VBS_NO_MEMBER, nullptr);
    BSTR s;
    int rc = get_str(f, v, &s, L"Len");
    if (rc != R_OK)
        return rc;
    ret_long(r, (LONG)SysStringLen(s));
    SysFreeString(s);
    return R_OK;
}

FN(lenb)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    BSTR s;
    int rc = get_str(f, v, &s, L"LenB");
    if (rc != R_OK)
        return rc;
    ret_long(r, (LONG)SysStringByteLen(s));
    SysFreeString(s);
    return R_OK;
}

static int round_long(Frame *f, VARIANT *v, LONG *out, PCWSTR name)
{
    return get_long(f, v, out, name);
}

FN(left)
{
    VARIANT *v = plain(&a[0]);
    LONG len;
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    int rc = round_long(f, &a[1], &len, L"Left");
    if (rc != R_OK)
        return rc;
    if (len < 0)
        return fail(f, E_VBS_INVALID_CALL, L"Left");
    BSTR s;
    rc = get_str(f, v, &s, L"Left");
    if (rc != R_OK)
        return rc;
    UINT sl = SysStringLen(s);
    ret_str(r, s, (UINT)len < sl ? (UINT)len : sl);
    SysFreeString(s);
    return R_OK;
}

FN(right)
{
    VARIANT *v = plain(&a[0]);
    LONG len;
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    int rc = round_long(f, &a[1], &len, L"Right");
    if (rc != R_OK)
        return rc;
    if (len < 0)
        return fail(f, E_VBS_INVALID_CALL, L"Right");
    BSTR s;
    rc = get_str(f, v, &s, L"Right");
    if (rc != R_OK)
        return rc;
    UINT sl = SysStringLen(s);
    UINT k = (UINT)len < sl ? (UINT)len : sl;
    ret_str(r, s + sl - k, k);
    SysFreeString(s);
    return R_OK;
}

FN(mid)
{
    VARIANT *v = plain(&a[0]);
    LONG start, len = -1;
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    int rc = round_long(f, &a[1], &start, L"Mid");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 2)) {
        rc = round_long(f, &a[2], &len, L"Mid");
        if (rc != R_OK)
            return rc;
        if (len < 0)
            return fail(f, E_VBS_INVALID_CALL, L"Mid");
    }
    if (start < 1)
        return fail(f, E_VBS_INVALID_CALL, L"Mid");
    BSTR s;
    rc = get_str(f, v, &s, L"Mid");
    if (rc != R_OK)
        return rc;
    UINT sl = SysStringLen(s);
    if ((UINT)start > sl) {
        ret_str(r, L"", 0);
    } else {
        UINT avail = sl - (UINT)start + 1;
        UINT k = len < 0 || (UINT)len > avail ? avail : (UINT)len;
        ret_str(r, s + start - 1, k);
    }
    SysFreeString(s);
    return R_OK;
}

static int bytes_fn(Frame *f, VARIANT *a, UINT n, VARIANT *r, int kind, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    LONG x, len = -1;
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    int rc = get_long(f, &a[1], &x, name);
    if (rc != R_OK)
        return rc;
    if (kind == 2 && !missing(a, n, 2)) {
        rc = get_long(f, &a[2], &len, name);
        if (rc != R_OK)
            return rc;
    }
    if ((kind != 2 && x < 0) || (kind == 2 && (x < 1 || len < -1)))
        return fail(f, E_VBS_INVALID_CALL, name);
    BSTR s;
    rc = get_str(f, v, &s, name);
    if (rc != R_OK)
        return rc;
    UINT bl = SysStringByteLen(s);
    const BYTE *p = (const BYTE *)s;
    UINT from = 0, count = 0;
    if (kind == 0) {
        count = (UINT)x < bl ? (UINT)x : bl;
    } else if (kind == 1) {
        count = (UINT)x < bl ? (UINT)x : bl;
        from = bl - count;
    } else {
        from = (UINT)x - 1;
        if (from > bl)
            from = bl;
        count = bl - from;
        if (len >= 0 && (UINT)len < count)
            count = (UINT)len;
    }
    r->vt = VT_BSTR;
    r->bstrVal = SysAllocStringByteLen((const char *)p + from, count);
    SysFreeString(s);
    return R_OK;
}

FN(leftb) { return bytes_fn(f, a, n, r, 0, L"LeftB"); }
FN(rightb) { return bytes_fn(f, a, n, r, 1, L"RightB"); }
FN(midb) { return bytes_fn(f, a, n, r, 2, L"MidB"); }

static BSTR lower_of(Frame *f, BSTR s)
{
    UINT l = SysStringLen(s);
    BSTR d = SysAllocStringLen(nullptr, l);
    if (l)
        LCMapStringW(f->E->Lcid, LCMAP_LOWERCASE, s, (int)l, d, (int)l);
    return d;
}

static int find_text(const WCHAR *hay, UINT hl, const WCHAR *needle, UINT nl, UINT from)
{
    if (nl == 0)
        return from <= hl ? (int)from : -1;
    for (UINT i = from; i + nl <= hl; i++)
        if (!memcmp(hay + i, needle, nl * sizeof(WCHAR)))
            return (int)i;
    return -1;
}

FN(instr)
{
    UINT k = 0;
    LONG start = 1;
    LONG compare = 0;
    UINT args = n;
    if (n >= 3) {
        VARIANT *sv = plain(&a[0]);
        if (sv->vt == VT_NULL)
            return fail(f, E_VBS_NULL, L"Null");
        int rc = get_long(f, sv, &start, L"InStr");
        if (rc != R_OK)
            return rc;
        if (start < 1)
            return fail(f, E_VBS_INVALID_CALL, L"InStr");
        k = 1;
        args = n - 1;
    }
    if (args == 3 || (n == 4)) {
        if (!missing(a, n, k + 2)) {
            int rc = get_long(f, &a[k + 2], &compare, L"InStr");
            if (rc != R_OK)
                return rc;
            if (compare < 0 || compare > 1)
                return fail(f, E_VBS_INVALID_CALL, L"InStr");
        }
    }
    VARIANT *v1 = plain(&a[k]), *v2 = plain(&a[k + 1]);
    if (v1->vt == VT_NULL || v2->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    BSTR s1, s2;
    int rc = get_str(f, v1, &s1, L"InStr");
    if (rc != R_OK)
        return rc;
    rc = get_str(f, v2, &s2, L"InStr");
    if (rc != R_OK) {
        SysFreeString(s1);
        return rc;
    }
    if (compare == 1) {
        BSTR l1 = lower_of(f, s1), l2 = lower_of(f, s2);
        SysFreeString(s1);
        SysFreeString(s2);
        s1 = l1;
        s2 = l2;
    }
    UINT l1 = SysStringLen(s1), l2 = SysStringLen(s2);
    int pos = -1;
    if ((UINT)start - 1 <= l1 && l1)
        pos = find_text(s1, l1, s2, l2, (UINT)start - 1);
    ret_long(r, pos < 0 ? 0 : pos + 1);
    SysFreeString(s1);
    SysFreeString(s2);
    return R_OK;
}

FN(instrb)
{
    VARIANT *v1 = plain(&a[n >= 3 ? 1 : 0]), *v2 = plain(&a[n >= 3 ? 2 : 1]);
    if (v1->vt == VT_NULL || v2->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    LONG start = 1;
    if (n >= 3) {
        int rc = get_long(f, &a[0], &start, L"InStrB");
        if (rc != R_OK)
            return rc;
    }
    BSTR s1, s2;
    int rc = get_str(f, v1, &s1, L"InStrB");
    if (rc != R_OK)
        return rc;
    rc = get_str(f, v2, &s2, L"InStrB");
    if (rc != R_OK) {
        SysFreeString(s1);
        return rc;
    }
    UINT b1 = SysStringByteLen(s1), b2 = SysStringByteLen(s2);
    int pos = -1;
    for (UINT i = start - 1; b2 && i + b2 <= b1; i++)
        if (!memcmp((BYTE *)s1 + i, s2, b2)) {
            pos = (int)i;
            break;
        }
    ret_long(r, pos < 0 ? 0 : pos + 1);
    SysFreeString(s1);
    SysFreeString(s2);
    return R_OK;
}

FN(instrrev)
{
    VARIANT *v1 = plain(&a[0]), *v2 = plain(&a[1]);
    LONG start = -1, compare = 0;
    if (v1->vt == VT_NULL || v2->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"InStrRev");
    if (!missing(a, n, 2)) {
        int rc = get_long(f, &a[2], &start, L"InStrRev");
        if (rc != R_OK)
            return rc;
        if (start == 0 || start < -1)
            return fail(f, E_VBS_INVALID_CALL, L"InStrRev");
    }
    if (!missing(a, n, 3)) {
        int rc = get_long(f, &a[3], &compare, L"InStrRev");
        if (rc != R_OK)
            return rc;
        if (compare < 0 || compare > 1)
            return fail(f, E_VBS_INVALID_CALL, L"InStrRev");
    }
    BSTR s1, s2;
    int rc = get_str(f, v1, &s1, L"InStrRev");
    if (rc != R_OK)
        return rc;
    rc = get_str(f, v2, &s2, L"InStrRev");
    if (rc != R_OK) {
        SysFreeString(s1);
        return rc;
    }
    if (compare == 1) {
        BSTR l1 = lower_of(f, s1), l2 = lower_of(f, s2);
        SysFreeString(s1);
        SysFreeString(s2);
        s1 = l1;
        s2 = l2;
    }
    UINT l1 = SysStringLen(s1), l2 = SysStringLen(s2);
    UINT from = start < 0 ? l1 : (UINT)start;
    int pos = 0;
    if (from > l1)
        from = l1;
    if (l2 == 0)
        pos = l1 ? (int)from : 0;
    else {
        for (int i = (int)from - (int)l2; i >= 0; i--)
            if (!memcmp(s1 + i, s2, l2 * sizeof(WCHAR))) {
                pos = i + 1;
                break;
            }
    }
    ret_long(r, pos);
    SysFreeString(s1);
    SysFreeString(s2);
    return R_OK;
}

FN(replace)
{
    for (UINT i = 0; i < 3; i++)
        if (plain(&a[i])->vt == VT_NULL)
            return fail(f, E_VBS_NULL, L"Replace");
    LONG start = 1, count = -1, compare = 0;
    if (!missing(a, n, 3) && get_long(f, &a[3], &start, L"Replace") != R_OK)
        return R_ERR;
    if (!missing(a, n, 4) && get_long(f, &a[4], &count, L"Replace") != R_OK)
        return R_ERR;
    if (!missing(a, n, 5) && get_long(f, &a[5], &compare, L"Replace") != R_OK)
        return R_ERR;
    if (start < 1 || count < -1 || compare < 0 || compare > 1)
        return fail(f, E_VBS_INVALID_CALL, L"Replace");
    BSTR s, find, rep;
    int rc = get_str(f, &a[0], &s, L"Replace");
    if (rc != R_OK)
        return rc;
    rc = get_str(f, &a[1], &find, L"Replace");
    if (rc != R_OK) {
        SysFreeString(s);
        return rc;
    }
    rc = get_str(f, &a[2], &rep, L"Replace");
    if (rc != R_OK) {
        SysFreeString(s);
        SysFreeString(find);
        return rc;
    }
    UINT sl = SysStringLen(s), fl = SysStringLen(find), rl = SysStringLen(rep);
    if ((UINT)start > sl && sl) {
        ret_str(r, L"", 0);
    } else if (!fl || !sl || count == 0) {
        UINT from = sl ? (UINT)start - 1 : 0;
        if (!fl || !sl)
            ret_str(r, sl ? s : L"", sl);
        else
            ret_str(r, s + from, sl - from);
    } else {
        BSTR hay = s, nd = find;
        if (compare == 1) {
            hay = lower_of(f, s);
            nd = lower_of(f, find);
        }
        UINT from = (UINT)start - 1;
        SIZE_T cap = (sl - from) + 16, len = 0;
        WCHAR *out = vbs_alloc(cap * sizeof(WCHAR));
        UINT i = from;
        LONG done = 0;
        while (i < sl) {
            int p = (count < 0 || done < count) ? find_text(hay, sl, nd, fl, i) : -1;
            UINT upto = p < 0 ? sl : (UINT)p;
            SIZE_T need = len + (upto - i) + rl + 1;
            if (need > cap) {
                cap = need * 2;
                out = vbs_realloc(out, cap * sizeof(WCHAR));
            }
            memcpy(out + len, s + i, (upto - i) * sizeof(WCHAR));
            len += upto - i;
            if (p < 0)
                break;
            memcpy(out + len, rep, rl * sizeof(WCHAR));
            len += rl;
            i = (UINT)p + fl;
            done++;
        }
        ret_str(r, out, len);
        vbs_free(out);
        if (compare == 1) {
            SysFreeString(hay);
            SysFreeString(nd);
        }
    }
    SysFreeString(s);
    SysFreeString(find);
    SysFreeString(rep);
    return R_OK;
}

static int trim_fn(Frame *f, VARIANT *a, VARIANT *r, BOOL left, BOOL right, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    BSTR s;
    int rc = get_str(f, v, &s, name);
    if (rc != R_OK)
        return rc;
    UINT b = 0, e = SysStringLen(s);
    if (left)
        while (b < e && s[b] == L' ')
            b++;
    if (right)
        while (e > b && s[e - 1] == L' ')
            e--;
    ret_str(r, s + b, e - b);
    SysFreeString(s);
    return R_OK;
}

FN(trim) { return trim_fn(f, a, r, TRUE, TRUE, L"Trim"); }
FN(ltrim) { return trim_fn(f, a, r, TRUE, FALSE, L"LTrim"); }
FN(rtrim) { return trim_fn(f, a, r, FALSE, TRUE, L"RTrim"); }

static int case_fn(Frame *f, VARIANT *a, VARIANT *r, DWORD flag, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    BSTR s;
    int rc = get_str(f, v, &s, name);
    if (rc != R_OK)
        return rc;
    UINT l = SysStringLen(s);
    BSTR d = SysAllocStringLen(nullptr, l);
    if (l)
        LCMapStringW(f->E->Lcid, flag, s, (int)l, d, (int)l);
    r->vt = VT_BSTR;
    r->bstrVal = d;
    SysFreeString(s);
    return R_OK;
}

FN(ucase) { return case_fn(f, a, r, LCMAP_UPPERCASE, L"UCase"); }
FN(lcase) { return case_fn(f, a, r, LCMAP_LOWERCASE, L"LCase"); }

FN(space)
{
    LONG k;
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_long(f, v, &k, L"Space");
    if (rc != R_OK)
        return rc;
    if (k < 0)
        return fail(f, E_VBS_INVALID_CALL, L"Space");
    BSTR s = SysAllocStringLen(nullptr, k);
    for (LONG i = 0; i < k; i++)
        s[i] = L' ';
    r->vt = VT_BSTR;
    r->bstrVal = s;
    return R_OK;
}

FN(string)
{
    LONG k;
    if (plain(&a[0])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_long(f, &a[0], &k, L"String");
    if (rc != R_OK)
        return rc;
    VARIANT *cv = plain(&a[1]);
    if (cv->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    if (k < 0)
        return fail(f, E_VBS_INVALID_CALL, L"String");
    WCHAR ch;
    if (cv->vt == VT_BSTR) {
        if (!SysStringLen(cv->bstrVal))
            return fail(f, E_VBS_INVALID_CALL, L"String");
        ch = cv->bstrVal[0];
    } else {
        LONG c;
        rc = get_long(f, cv, &c, L"String");
        if (rc != R_OK)
            return rc;
        if (c < 0 || c > 65535)
            return fail(f, E_VBS_INVALID_CALL, L"String");
        ch = (WCHAR)c;
    }
    BSTR s = SysAllocStringLen(nullptr, k);
    for (LONG i = 0; i < k; i++)
        s[i] = ch;
    r->vt = VT_BSTR;
    r->bstrVal = s;
    return R_OK;
}

FN(strreverse)
{
    BSTR s;
    int rc = get_str(f, &a[0], &s, L"StrReverse");
    if (rc != R_OK)
        return rc;
    UINT l = SysStringLen(s);
    BSTR d = SysAllocStringLen(nullptr, l);
    for (UINT i = 0; i < l; i++)
        d[i] = s[l - 1 - i];
    r->vt = VT_BSTR;
    r->bstrVal = d;
    SysFreeString(s);
    return R_OK;
}

FN(strcomp)
{
    LONG compare = 0;
    if (!missing(a, n, 2)) {
        int rc = get_long(f, &a[2], &compare, L"StrComp");
        if (rc != R_OK)
            return rc;
        if (compare < 0 || compare > 1)
            return fail(f, E_VBS_INVALID_CALL, L"StrComp");
    }
    if (plain(&a[0])->vt == VT_NULL || plain(&a[1])->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    BSTR s1, s2;
    int rc = get_str(f, &a[0], &s1, L"StrComp");
    if (rc != R_OK)
        return rc;
    rc = get_str(f, &a[1], &s2, L"StrComp");
    if (rc != R_OK) {
        SysFreeString(s1);
        return rc;
    }
    int res;
    if (compare == 1) {
        int c = CompareStringW(f->E->Lcid, NORM_IGNORECASE, s1, (int)SysStringLen(s1), s2, (int)SysStringLen(s2));
        res = c == CSTR_LESS_THAN ? -1 : c == CSTR_GREATER_THAN ? 1 : 0;
    } else {
        VARIANT x, y;
        x.vt = y.vt = VT_BSTR;
        x.bstrVal = s1;
        y.bstrVal = s2;
        var_compare(&x, &y, f->E->Lcid, &res);
    }
    ret_int(r, res);
    SysFreeString(s1);
    SysFreeString(s2);
    return R_OK;
}

FN(asc)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    BSTR s;
    int rc = get_str(f, v, &s, L"Asc");
    if (rc != R_OK)
        return rc;
    if (!SysStringLen(s)) {
        SysFreeString(s);
        return fail(f, E_VBS_INVALID_CALL, L"Asc");
    }
    char buf[4];
    int k = WideCharToMultiByte(CP_ACP, 0, s, 1, buf, 4, nullptr, nullptr);
    int code = k == 1 ? (BYTE)buf[0] : k == 2 ? (short)(((BYTE)buf[0] << 8) | (BYTE)buf[1]) : 0;
    SysFreeString(s);
    ret_int(r, code);
    return R_OK;
}

FN(ascw)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    BSTR s;
    int rc = get_str(f, v, &s, L"AscW");
    if (rc != R_OK)
        return rc;
    if (!SysStringLen(s)) {
        SysFreeString(s);
        return fail(f, E_VBS_INVALID_CALL, L"AscW");
    }
    LONG c = s[0];
    SysFreeString(s);
    ret_int(r, c > 32767 ? c - 65536 : c);
    return R_OK;
}

FN(ascb)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    BSTR s;
    int rc = get_str(f, v, &s, L"AscB");
    if (rc != R_OK)
        return rc;
    if (!SysStringByteLen(s)) {
        SysFreeString(s);
        return fail(f, E_VBS_INVALID_CALL, L"AscB");
    }
    r->vt = VT_UI1;
    r->bVal = ((BYTE *)s)[0];
    SysFreeString(s);
    return R_OK;
}

FN(chr)
{
    LONG c;
    if (plain(&a[0])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_long(f, &a[0], &c, L"Chr");
    if (rc != R_OK)
        return rc;
    if (c < 0 || c > 255)
        return fail(f, E_VBS_INVALID_CALL, L"Chr");
    char b = (char)c;
    WCHAR w;
    if (c < 128)
        w = (WCHAR)c;
    else if (MultiByteToWideChar(CP_ACP, 0, &b, 1, &w, 1) != 1)
        w = (WCHAR)c;
    ret_str(r, &w, 1);
    return R_OK;
}

FN(chrw)
{
    LONG c;
    if (plain(&a[0])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_long(f, &a[0], &c, L"ChrW");
    if (rc != R_OK)
        return rc;
    if (c < -32768 || c > 65535)
        return fail(f, E_VBS_INVALID_CALL, L"ChrW");
    WCHAR w = (WCHAR)(c < 0 ? c + 65536 : c);
    ret_str(r, &w, 1);
    return R_OK;
}

FN(chrb)
{
    LONG c;
    if (plain(&a[0])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    int rc = get_long(f, &a[0], &c, L"ChrB");
    if (rc != R_OK)
        return rc;
    if (c < 0 || c > 255)
        return fail(f, E_VBS_INVALID_CALL, L"ChrB");
    char b = (char)c;
    r->vt = VT_BSTR;
    r->bstrVal = SysAllocStringByteLen(&b, 1);
    return R_OK;
}

static int array_arg(Frame *f, VARIANT *v, SAFEARRAY **sa, PCWSTR name)
{
    v = plain(v);
    if (!(v->vt & VT_ARRAY))
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    *sa = (v->vt & VT_BYREF) ? *v->pparray : v->parray;
    if (!*sa || (*sa)->cDims != 1)
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    return R_OK;
}

static int array_element_at(SAFEARRAY *sa, LONG i, VARIANT *out)
{
    LONG lo;
    SafeArrayGetLBound(sa, 1, &lo);
    LONG idx = lo + i;
    VARTYPE vt = 0;
    SafeArrayGetVartype(sa, &vt);
    VariantInit(out);
    if (vt == VT_VARIANT) {
        VARIANT *p;
        SafeArrayPtrOfIndex(sa, &idx, (void **)&p);
        return SUCCEEDED(VariantCopyInd(out, p));
    }
    if (vt == VT_BSTR) {
        BSTR b = nullptr;
        SafeArrayGetElement(sa, &idx, &b);
        out->vt = VT_BSTR;
        out->bstrVal = b;
        return 1;
    }
    out->vt = vt;
    return SUCCEEDED(SafeArrayGetElement(sa, &idx, &out->lVal));
}

static LONG array_count(SAFEARRAY *sa)
{
    LONG lo, hi;
    SafeArrayGetLBound(sa, 1, &lo);
    SafeArrayGetUBound(sa, 1, &hi);
    return hi - lo + 1;
}

FN(join)
{
    SAFEARRAY *sa;
    int rc = array_arg(f, &a[0], &sa, L"Join");
    if (rc != R_OK)
        return rc;
    BSTR delim = nullptr;
    if (!missing(a, n, 1)) {
        rc = get_str(f, &a[1], &delim, L"Join");
        if (rc != R_OK)
            return rc;
    }
    UINT dl = delim ? SysStringLen(delim) : 1;
    LONG count = array_count(sa);
    SIZE_T cap = 64, len = 0;
    WCHAR *out = vbs_alloc(cap * sizeof(WCHAR));
    for (LONG i = 0; i < count; i++) {
        VARIANT e, s;
        array_element_at(sa, i, &e);
        if (e.vt == VT_NULL) {
            vbs_free(out);
            SysFreeString(delim);
            VariantClear(&e);
            return fail(f, E_VBS_TYPE_MISMATCH, L"Join");
        }
        VariantInit(&s);
        HRESULT hr = VariantChangeTypeEx(&s, &e, f->E->Lcid, VARIANT_ALPHABOOL, VT_BSTR);
        VariantClear(&e);
        if (FAILED(hr)) {
            vbs_free(out);
            SysFreeString(delim);
            return fail(f, E_VBS_TYPE_MISMATCH, L"Join");
        }
        UINT sl = SysStringLen(s.bstrVal);
        SIZE_T need = len + sl + dl + 2;
        if (need > cap) {
            cap = need * 2;
            out = vbs_realloc(out, cap * sizeof(WCHAR));
        }
        if (i) {
            if (delim)
                memcpy(out + len, delim, dl * sizeof(WCHAR));
            else
                out[len] = L' ';
            len += dl;
        }
        memcpy(out + len, s.bstrVal, sl * sizeof(WCHAR));
        len += sl;
        SysFreeString(s.bstrVal);
    }
    ret_str(r, out, len);
    vbs_free(out);
    SysFreeString(delim);
    return R_OK;
}

static SAFEARRAY *string_array(UINT count)
{
    SAFEARRAYBOUND b = {count, 0};
    return SafeArrayCreate(VT_VARIANT, 1, &b);
}

FN(split)
{
    if (plain(&a[0])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Split");
    BSTR s, delim = nullptr;
    LONG count = -1, compare = 0;
    int rc = get_str(f, &a[0], &s, L"Split");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 1)) {
        if (plain(&a[1])->vt == VT_NULL) {
            SysFreeString(s);
            return fail(f, E_VBS_NULL, L"Split");
        }
        rc = get_str(f, &a[1], &delim, L"Split");
        if (rc != R_OK) {
            SysFreeString(s);
            return rc;
        }
    }
    if (!missing(a, n, 2) && get_long(f, &a[2], &count, L"Split") != R_OK)
        return R_ERR;
    if (!missing(a, n, 3) && get_long(f, &a[3], &compare, L"Split") != R_OK)
        return R_ERR;
    if (count < -1 || compare < 0 || compare > 1) {
        SysFreeString(s);
        SysFreeString(delim);
        return fail(f, E_VBS_INVALID_CALL, L"Split");
    }
    WCHAR sp = L' ';
    const WCHAR *d = delim ? delim : &sp;
    UINT dl = delim ? SysStringLen(delim) : 1;
    UINT sl = SysStringLen(s);
    BSTR hay = s, nd = delim ? delim : nullptr;
    if (compare == 1) {
        hay = lower_of(f, s);
        if (delim)
            nd = lower_of(f, delim);
        else
            nd = SysAllocString(L" ");
        d = nd;
    }
    SAFEARRAY *sa;
    if (sl == 0 || count == 0) {
        SAFEARRAYBOUND b = {0, 0};
        sa = SafeArrayCreate(VT_VARIANT, 1, &b);
    } else {
        UINT pieces = 1;
        if (dl) {
            UINT i = 0;
            while ((count < 0 || (LONG)pieces < count)) {
                int p = find_text(hay, sl, d, dl, i);
                if (p < 0)
                    break;
                pieces++;
                i = (UINT)p + dl;
            }
        }
        sa = string_array(pieces);
        UINT i = 0;
        for (UINT k = 0; k < pieces; k++) {
            int p = (k + 1 < pieces) ? find_text(hay, sl, d, dl, i) : -1;
            UINT upto = p < 0 ? sl : (UINT)p;
            VARIANT *e;
            LONG idx = (LONG)k;
            SafeArrayPtrOfIndex(sa, &idx, (void **)&e);
            e->vt = VT_BSTR;
            e->bstrVal = SysAllocStringLen(s + i, upto - i);
            i = upto + dl;
        }
    }
    if (compare == 1) {
        SysFreeString(hay);
        SysFreeString(nd);
    }
    SysFreeString(s);
    SysFreeString(delim);
    r->vt = VT_ARRAY | VT_VARIANT;
    r->parray = sa;
    return R_OK;
}

FN(filter)
{
    if (plain(&a[0])->vt == VT_NULL || plain(&a[1])->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Filter");
    SAFEARRAY *sa;
    int rc = array_arg(f, &a[0], &sa, L"Filter");
    if (rc != R_OK)
        return rc;
    BSTR match;
    rc = get_str(f, &a[1], &match, L"Filter");
    if (rc != R_OK)
        return rc;
    BOOL include = TRUE;
    LONG compare = 0;
    if (!missing(a, n, 2)) {
        BOOL b;
        rc = to_bool(f, plain(&a[2]), &b);
        if (rc != R_OK) {
            SysFreeString(match);
            return rc;
        }
        include = b;
    }
    if (!missing(a, n, 3) && get_long(f, &a[3], &compare, L"Filter") != R_OK)
        return R_ERR;
    LONG count = array_count(sa);
    BSTR m = compare == 1 ? lower_of(f, match) : SysAllocStringLen(match, SysStringLen(match));
    VARIANT *kept = vbs_alloc((count + 1) * sizeof(VARIANT));
    LONG k = 0;
    for (LONG i = 0; i < count; i++) {
        VARIANT e, s;
        array_element_at(sa, i, &e);
        VariantInit(&s);
        if (e.vt == VT_NULL || FAILED(VariantChangeTypeEx(&s, &e, f->E->Lcid, VARIANT_ALPHABOOL, VT_BSTR))) {
            VariantClear(&e);
            SysFreeString(match);
            SysFreeString(m);
            vbs_free(kept);
            return fail(f, E_VBS_TYPE_MISMATCH, L"Filter");
        }
        VariantClear(&e);
        BSTR t = compare == 1 ? lower_of(f, s.bstrVal) : s.bstrVal;
        BOOL found = find_text(t, SysStringLen(t), m, SysStringLen(m), 0) >= 0;
        if (compare == 1)
            SysFreeString(t);
        if (found == include)
            kept[k++] = s;
        else
            SysFreeString(s.bstrVal);
    }
    SAFEARRAY *out = string_array(k);
    for (LONG i = 0; i < k; i++) {
        VARIANT *e;
        SafeArrayPtrOfIndex(out, &i, (void **)&e);
        *e = kept[i];
    }
    vbs_free(kept);
    SysFreeString(match);
    SysFreeString(m);
    r->vt = VT_ARRAY | VT_VARIANT;
    r->parray = out;
    return R_OK;
}

FN(escape)
{
    BSTR s;
    int rc = get_str(f, &a[0], &s, L"Escape");
    if (rc != R_OK)
        return rc;
    UINT l = SysStringLen(s);
    WCHAR *out = vbs_alloc((l * 6 + 1) * sizeof(WCHAR));
    SIZE_T k = 0;
    static const WCHAR hex[] = L"0123456789ABCDEF";
    for (UINT i = 0; i < l; i++) {
        WCHAR c = s[i];
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'@' || c == L'*' ||
            c == L'_' || c == L'+' || c == L'-' || c == L'.' || c == L'/')
            out[k++] = c;
        else if (c < 256) {
            out[k++] = L'%';
            out[k++] = hex[c >> 4];
            out[k++] = hex[c & 15];
        } else {
            out[k++] = L'%';
            out[k++] = L'u';
            out[k++] = hex[c >> 12];
            out[k++] = hex[(c >> 8) & 15];
            out[k++] = hex[(c >> 4) & 15];
            out[k++] = hex[c & 15];
        }
    }
    ret_str(r, out, k);
    vbs_free(out);
    SysFreeString(s);
    return R_OK;
}

static int hexval(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

FN(unescape)
{
    BSTR s;
    int rc = get_str(f, &a[0], &s, L"Unescape");
    if (rc != R_OK)
        return rc;
    UINT l = SysStringLen(s);
    WCHAR *out = vbs_alloc((l + 1) * sizeof(WCHAR));
    SIZE_T k = 0;
    for (UINT i = 0; i < l; i++) {
        if (s[i] == L'%') {
            if (i + 5 < l + 0 && s[i + 1] == L'u' && hexval(s[i + 2]) >= 0 && hexval(s[i + 3]) >= 0 &&
                hexval(s[i + 4]) >= 0 && hexval(s[i + 5]) >= 0) {
                out[k++] = (WCHAR)((hexval(s[i + 2]) << 12) | (hexval(s[i + 3]) << 8) | (hexval(s[i + 4]) << 4) | hexval(s[i + 5]));
                i += 5;
                continue;
            }
            if (i + 2 < l + 0 + 1 && i + 2 <= l - 1 && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
                out[k++] = (WCHAR)((hexval(s[i + 1]) << 4) | hexval(s[i + 2]));
                i += 2;
                continue;
            }
        }
        out[k++] = s[i];
    }
    ret_str(r, out, k);
    vbs_free(out);
    SysFreeString(s);
    return R_OK;
}

/* ------------------------------------------------------------------ arrays and types */

FN(array)
{
    SAFEARRAY *sa = string_array(n);
    for (UINT i = 0; i < n; i++) {
        VARIANT *e;
        LONG idx = (LONG)i;
        SafeArrayPtrOfIndex(sa, &idx, (void **)&e);
        var_deref(e, &a[i]);
    }
    r->vt = VT_ARRAY | VT_VARIANT;
    r->parray = sa;
    return R_OK;
}

static int bound_fn(Frame *f, VARIANT *a, UINT n, VARIANT *r, BOOL upper, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    LONG dim = 1;
    if (!(v->vt & VT_ARRAY))
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    SAFEARRAY *sa = (v->vt & VT_BYREF) ? *v->pparray : v->parray;
    if (!missing(a, n, 1)) {
        int rc = get_long(f, &a[1], &dim, name);
        if (rc != R_OK)
            return rc;
    }
    if (!sa || dim < 1 || (UINT)dim > sa->cDims)
        return fail(f, E_VBS_SUBSCRIPT, name);
    LONG b;
    if (upper)
        SafeArrayGetUBound(sa, (UINT)dim, &b);
    else
        SafeArrayGetLBound(sa, (UINT)dim, &b);
    ret_long(r, b);
    return R_OK;
}

FN(ubound) { return bound_fn(f, a, n, r, TRUE, L"UBound"); }
FN(lbound) { return bound_fn(f, a, n, r, FALSE, L"LBound"); }

static int default_of(Frame *f, VARIANT *v, VARIANT *out)
{
    VariantInit(out);
    VariantCopy(out, v);
    if (out->vt == VT_DISPATCH || out->vt == VT_UNKNOWN) {
        if (!out->pdispVal)
            return R_ERR;
        Engine *e = f->E;
        ErrState saved = e->Err;
        memset(&e->Err, 0, sizeof(e->Err));
        Fault sf = e->Fault;
        memset(&e->Fault, 0, sizeof(e->Fault));
        int rc = value_of(f, out);
        err_clear(e);
        e->Err = saved;
        fault_clear(e);
        e->Fault = sf;
        if (rc != R_OK) {
            VariantClear(out);
            return R_ERR;
        }
    }
    return R_OK;
}

FN(vartype)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_DISPATCH || v->vt == VT_UNKNOWN) {
        if (!v->pdispVal) {
            ret_int(r, VT_DISPATCH);
            return R_OK;
        }
        VARIANT d;
        if (default_of(f, v, &d) == R_OK) {
            ret_int(r, d.vt);
            VariantClear(&d);
            return R_OK;
        }
        ret_int(r, VT_DISPATCH);
        return R_OK;
    }
    ret_int(r, v->vt & ~VT_BYREF);
    return R_OK;
}

FN(typename)
{
    VARIANT *v = plain(&a[0]);
    PCWSTR name = nullptr;
    VARTYPE vt = v->vt & VT_TYPEMASK;
    static const PCWSTR names[] = {L"Empty", L"Null", L"Integer", L"Long", L"Single", L"Double", L"Currency", L"Date",
                                   L"String", L"Object", L"Error", L"Boolean", L"Variant", L"Unknown", L"Decimal"};
    WCHAR buf[80];
    if (v->vt & VT_ARRAY) {
        switch (vt) {
        case VT_I2: name = L"Integer()"; break;
        case VT_I4: name = L"Long()"; break;
        case VT_R4: name = L"Single()"; break;
        case VT_R8: name = L"Double()"; break;
        case VT_CY: name = L"Currency()"; break;
        case VT_DATE: name = L"Date()"; break;
        case VT_BSTR: name = L"String()"; break;
        case VT_DISPATCH: case VT_UNKNOWN: name = L"Object()"; break;
        case VT_ERROR: name = L"Error()"; break;
        case VT_BOOL: name = L"Boolean()"; break;
        case VT_VARIANT: name = L"Variant()"; break;
                case VT_UI1: name = L"Byte()"; break;
        default: return fail(f, E_VBS_BAD_VARTYPE, L"TypeName");
        }
    } else if (vt == VT_DISPATCH || vt == VT_UNKNOWN) {
        if (!v->pdispVal)
            name = L"Nothing";
        else if (vt == VT_DISPATCH && is_script_instance(v->pdispVal)) {
            _snwprintf(buf, 80, L"%s", ((Instance *)v->pdispVal)->Cls->Name);
            name = buf;
        } else
            name = L"Object";
    } else if (vt == VT_UI1)
        name = L"Byte";
    else if (vt <= VT_DECIMAL && vt != 15 && vt != VT_DECIMAL && vt < 13)
        name = names[vt];
    else if (vt == VT_VARIANT)
        name = L"Variant";
    else
        return fail(f, E_VBS_BAD_VARTYPE, L"TypeName");
    ret_str(r, name, wcslen(name));
    return R_OK;
}

FN(isarray) { ret_bool(r, (plain(&a[0])->vt & VT_ARRAY) != 0); return R_OK; }
FN(isnull) { ret_bool(r, plain(&a[0])->vt == VT_NULL); return R_OK; }
FN(isempty) { ret_bool(r, plain(&a[0])->vt == VT_EMPTY); return R_OK; }

FN(isobject)
{
    VARTYPE vt = plain(&a[0])->vt;
    ret_bool(r, vt == VT_DISPATCH || vt == VT_UNKNOWN);
    return R_OK;
}

FN(isnumeric)
{
    VARIANT d;
    if (default_of(f, plain(&a[0]), &d) != R_OK) {
        ret_bool(r, FALSE);
        return R_OK;
    }
    BOOL ok = FALSE;
    switch (d.vt) {
    case VT_EMPTY: case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_BOOL: case VT_UI1:
    case VT_DECIMAL: case VT_I1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT:
        ok = TRUE;
        break;
    case VT_BSTR: {
        double dv;
        ok = SUCCEEDED(VarR8FromStr(d.bstrVal, f->E->Lcid, 0, &dv));
        break;
    }
    }
    VariantClear(&d);
    ret_bool(r, ok);
    return R_OK;
}

FN(isdate)
{
    VARIANT d;
    if (default_of(f, plain(&a[0]), &d) != R_OK) {
        ret_bool(r, FALSE);
        return R_OK;
    }
    BOOL ok = FALSE;
    if (d.vt == VT_DATE)
        ok = TRUE;
    else if (d.vt == VT_BSTR) {
        DATE dt;
        ok = SUCCEEDED(VarDateFromStr(d.bstrVal, f->E->Lcid, 0, &dt));
    }
    VariantClear(&d);
    ret_bool(r, ok);
    return R_OK;
}

/* ------------------------------------------------------------------ dates */

static void now_parts(UDATE *u)
{
    memset(u, 0, sizeof(*u));
    GetLocalTime(&u->st);
}

FN(now)
{
    UDATE u;
    now_parts(&u);
    r->vt = VT_DATE;
    return SUCCEEDED(VarDateFromUdate(&u, 0, &r->date)) ? R_OK : fail(f, E_VBS_INVALID_CALL, L"Now");
}

FN(date)
{
    UDATE u;
    now_parts(&u);
    r->vt = VT_DATE;
    return SUCCEEDED(VarDateFromUdate(&u, VAR_DATEVALUEONLY, &r->date)) ? R_OK : fail(f, E_VBS_INVALID_CALL, L"Date");
}

FN(time)
{
    UDATE u;
    now_parts(&u);
    r->vt = VT_DATE;
    return SUCCEEDED(VarDateFromUdate(&u, VAR_TIMEVALUEONLY, &r->date)) ? R_OK : fail(f, E_VBS_INVALID_CALL, L"Time");
}

FN(timer)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    r->vt = VT_R4;
    r->fltVal = (float)(st.wHour * 3600 + st.wMinute * 60 + st.wSecond + st.wMilliseconds / 1000.0);
    return R_OK;
}

/* the date a value stands for. null is the caller's to handle. */
static int to_date(Frame *f, VARIANT *v, DATE *out, PCWSTR name)
{
    VARIANT t;
    v = plain(v);
    VariantInit(&t);
    HRESULT hr = VariantChangeTypeEx(&t, v, f->E->Lcid, 0, VT_DATE);
    if (FAILED(hr)) {
        WCHAR d[80];
        if (name) {
            return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, name);
        }
        describe_value(v, d);
        return fail(f, hr == DISP_E_OVERFLOW ? E_VBS_OVERFLOW : E_VBS_TYPE_MISMATCH, d);
    }
    *out = t.date;
    return R_OK;
}

static int date_part_fn(Frame *f, VARIANT *a, VARIANT *r, int part, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    DATE d;
    int rc = to_date(f, v, &d, nullptr);
    if (rc != R_OK)
        return rc;
    UDATE u;
    if (FAILED(VarUdateFromDate(d, 0, &u)))
        return fail(f, E_VBS_INVALID_CALL, name);
    int x = part == 0 ? u.st.wYear : part == 1 ? u.st.wMonth : part == 2 ? u.st.wDay : part == 3 ? u.st.wHour :
            part == 4 ? u.st.wMinute : u.st.wSecond;
    ret_int(r, x);
    return R_OK;
}

FN(year) { return date_part_fn(f, a, r, 0, L"Year"); }
FN(month) { return date_part_fn(f, a, r, 1, L"Month"); }
FN(day) { return date_part_fn(f, a, r, 2, L"Day"); }
FN(hour) { return date_part_fn(f, a, r, 3, L"Hour"); }
FN(minute) { return date_part_fn(f, a, r, 4, L"Minute"); }
FN(second) { return date_part_fn(f, a, r, 5, L"Second"); }

static int first_day(Frame *f, VARIANT *a, UINT n, UINT i, LONG *first, PCWSTR name)
{
    *first = 1;
    if (!missing(a, n, i)) {
        int rc = get_long(f, &a[i], first, name);
        if (rc != R_OK)
            return rc;
        if (*first < 0 || *first > 7)
            return fail(f, E_VBS_INVALID_CALL, name);
    }
    if (*first == 0) {
        WCHAR buf[4];
        GetLocaleInfoW(f->E->Lcid, LOCALE_IFIRSTDAYOFWEEK, buf, 4);
        /* the locale counts from monday as zero. */
        int w = buf[0] - L'0';
        *first = ((w + 1) % 7) + 1;
    }
    return R_OK;
}

FN(weekday)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    LONG first;
    int rc = first_day(f, a, n, 1, &first, L"Weekday");
    if (rc != R_OK)
        return rc;
    DATE d;
    rc = to_date(f, v, &d, nullptr);
    if (rc != R_OK)
        return rc;
    UDATE u;
    VarUdateFromDate(d, 0, &u);
    int w = (u.st.wDayOfWeek - (first - 1) + 7) % 7 + 1;
    ret_int(r, w);
    return R_OK;
}

static int locale_string(Frame *f, LCTYPE type, WCHAR *buf, int size)
{
    return GetLocaleInfoW(f->E->Lcid, type, buf, size);
}

FN(weekdayname)
{
    LONG d, first = 0;
    BOOL abbr = FALSE;
    int rc = get_long(f, &a[0], &d, L"WeekdayName");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 1)) {
        rc = to_bool(f, plain(&a[1]), &abbr);
        if (rc != R_OK)
            return rc;
    }
    rc = first_day(f, a, n, 2, &first, L"WeekdayName");
    if (rc != R_OK)
        return rc;
    if (d < 1 || d > 7)
        return fail(f, E_VBS_INVALID_CALL, L"WeekdayName");
    int sunday_based = (int)((d - 1 + (first - 1)) % 7);
    int monday_based = (sunday_based + 6) % 7;
    WCHAR buf[64];
    int len = locale_string(f, (abbr ? LOCALE_SABBREVDAYNAME1 : LOCALE_SDAYNAME1) + monday_based, buf, 64);
    ret_str(r, buf, len > 0 ? len - 1 : 0);
    return R_OK;
}

FN(monthname)
{
    LONG m;
    BOOL abbr = FALSE;
    int rc = get_long(f, &a[0], &m, L"MonthName");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 1)) {
        rc = to_bool(f, plain(&a[1]), &abbr);
        if (rc != R_OK)
            return rc;
    }
    if (m < 1 || m > 12)
        return fail(f, E_VBS_INVALID_CALL, L"MonthName");
    WCHAR buf[64];
    int len = locale_string(f, (abbr ? LOCALE_SABBREVMONTHNAME1 : LOCALE_SMONTHNAME1) + (m - 1), buf, 64);
    ret_str(r, buf, len > 0 ? len - 1 : 0);
    return R_OK;
}

FN(dateserial)
{
    LONG v[3];
    for (int i = 0; i < 3; i++) {
        int rc = get_long(f, &a[i], &v[i], L"Null");
        if (rc != R_OK) {
            if (f->E->Err.Number == E_VBS_NULL || plain(&a[i])->vt == VT_NULL)
                return fail(f, E_VBS_NULL, L"Null");
            return rc;
        }
    }
    UDATE u;
    memset(&u, 0, sizeof(u));
    u.st.wYear = (WORD)v[0];
    u.st.wMonth = (WORD)v[1];
    u.st.wDay = (WORD)v[2];
    r->vt = VT_DATE;
    if (FAILED(VarDateFromUdate(&u, VAR_DATEVALUEONLY, &r->date))) {
        r->vt = VT_EMPTY;
        return fail(f, E_VBS_INVALID_CALL, L"DateSerial");
    }
    return R_OK;
}

FN(timeserial)
{
    LONG v[3];
    for (int i = 0; i < 3; i++) {
        int rc = get_long(f, &a[i], &v[i], L"Null");
        if (rc != R_OK)
            return rc;
    }
    UDATE u;
    memset(&u, 0, sizeof(u));
    u.st.wHour = (WORD)v[0];
    u.st.wMinute = (WORD)v[1];
    u.st.wSecond = (WORD)v[2];
    r->vt = VT_DATE;
    if (FAILED(VarDateFromUdate(&u, VAR_TIMEVALUEONLY, &r->date))) {
        r->vt = VT_EMPTY;
        return fail(f, E_VBS_INVALID_CALL, L"TimeSerial");
    }
    return R_OK;
}

static int value_date_fn(Frame *f, VARIANT *a, VARIANT *r, ULONG flag, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    BSTR s;
    int rc = get_str(f, v, &s, name);
    if (rc != R_OK)
        return rc;
    DATE d;
    HRESULT hr = VarDateFromStr(s, f->E->Lcid, 0, &d);
    SysFreeString(s);
    if (FAILED(hr))
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    UDATE u;
    VarUdateFromDate(d, 0, &u);
    r->vt = VT_DATE;
    VarDateFromUdate(&u, flag, &r->date);
    return R_OK;
}

FN(datevalue) { return value_date_fn(f, a, r, VAR_DATEVALUEONLY, L"DateValue"); }
FN(timevalue) { return value_date_fn(f, a, r, VAR_TIMEVALUEONLY, L"TimeValue"); }

/* the interval of DateAdd, DateDiff and DatePart. */
enum { IV_YEAR, IV_QUARTER, IV_MONTH, IV_DAYOFYEAR, IV_DAY, IV_WEEKDAY, IV_WEEK, IV_HOUR, IV_MINUTE, IV_SECOND, IV_BAD };

static int interval_of(Frame *f, VARIANT *v, PCWSTR name, int *out)
{
    BSTR s;
    int rc = get_str(f, v, &s, name);
    if (rc != R_OK)
        return rc;
    static const struct {
        PCWSTR T;
        int K;
    } t[] = {{L"yyyy", IV_YEAR}, {L"q", IV_QUARTER}, {L"m", IV_MONTH}, {L"y", IV_DAYOFYEAR}, {L"d", IV_DAY},
             {L"w", IV_WEEKDAY}, {L"ww", IV_WEEK}, {L"h", IV_HOUR}, {L"n", IV_MINUTE}, {L"s", IV_SECOND}};
    *out = IV_BAD;
    for (UINT i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (name_equal(s, t[i].T))
            *out = t[i].K;
    SysFreeString(s);
    if (*out == IV_BAD)
        return fail(f, E_VBS_INVALID_CALL, name);
    return R_OK;
}

static LONG days_in_month_of(LONG y, LONG m)
{
    static const BYTE d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d[m - 1] + (m == 2 && leap);
}

FN(dateadd)
{
    int iv;
    int rc = interval_of(f, &a[0], L"DateAdd", &iv);
    if (rc != R_OK)
        return rc;
    VARIANT *nv = plain(&a[1]);
    if (nv->vt == VT_NULL)
        return fail(f, E_VBS_NULL, L"Null");
    double num;
    rc = get_double(f, nv, &num, L"DateAdd");
    if (rc != R_OK)
        return rc;
    VARIANT *dv = plain(&a[2]);
    if (dv->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    DATE d;
    rc = to_date(f, dv, &d, L"DateAdd");
    if (rc != R_OK)
        return rc;
    double fixed = num < 0 ? -floor(-num) : floor(num);
    if (fixed > 2147483647.0 || fixed < -2147483648.0)
        return fail(f, E_VBS_OVERFLOW, L"DateAdd");
    LONG k = (LONG)fixed;
    DATE out;
    if (iv == IV_YEAR || iv == IV_QUARTER || iv == IV_MONTH) {
        UDATE u;
        if (FAILED(VarUdateFromDate(d, 0, &u)))
            return fail(f, E_VBS_INVALID_CALL, L"DateAdd");
        LONGLONG months = (LONGLONG)u.st.wYear * 12 + (u.st.wMonth - 1) + (iv == IV_YEAR ? k * 12LL : iv == IV_QUARTER ? k * 3LL : k);
        LONGLONG y = months / 12;
        LONG m = (LONG)(months % 12) + 1;
        if (y < 100 || y > 9999)
            return fail(f, E_VBS_INVALID_CALL, L"DateAdd");
        LONG day = u.st.wDay;
        LONG dim = days_in_month_of((LONG)y, m);
        if (day > dim)
            day = dim;
        u.st.wYear = (WORD)y;
        u.st.wMonth = (WORD)m;
        u.st.wDay = (WORD)day;
        if (FAILED(VarDateFromUdate(&u, 0, &out)))
            return fail(f, E_VBS_INVALID_CALL, L"DateAdd");
    } else {
        double add = iv == IV_WEEK ? k * 7.0 : (iv == IV_DAYOFYEAR || iv == IV_DAY || iv == IV_WEEKDAY) ? (double)k :
                     iv == IV_HOUR ? k / 24.0 : iv == IV_MINUTE ? k / 1440.0 : k / 86400.0;
        double t = d + (d < 0 ? -add : add);
        if (d < 0) {
            double frac = d - floor(d);
            t = floor(d) + frac + add;
            (void)frac;
        } else
            t = d + add;
        if (!(t > -657435.0 && t < 2958466.0))
            return fail(f, E_VBS_INVALID_CALL, L"DateAdd");
        out = t;
    }
    r->vt = VT_DATE;
    r->date = out;
    return R_OK;
}

static int week_start_offset(UDATE *u, LONG first)
{
    return (u->st.wDayOfWeek - (first - 1) + 7) % 7;
}

static double day_floor(double d)
{
    return floor(d);
}

FN(datediff)
{
    int iv;
    int rc = interval_of(f, &a[0], L"DateDiff", &iv);
    if (rc != R_OK)
        return rc;
    VARIANT *d1v = plain(&a[1]), *d2v = plain(&a[2]);
    if (d1v->vt == VT_NULL || d2v->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    LONG first = 1;
    rc = first_day(f, a, n, 3, &first, L"DateDiff");
    if (rc != R_OK)
        return rc;
    DATE d1, d2;
    rc = to_date(f, d1v, &d1, L"DateDiff");
    if (rc == R_OK)
        rc = to_date(f, d2v, &d2, L"DateDiff");
    if (rc != R_OK)
        return rc;
    UDATE u1, u2;
    VarUdateFromDate(d1, 0, &u1);
    VarUdateFromDate(d2, 0, &u2);
    double result = 0;
    switch (iv) {
    case IV_YEAR: result = (double)u2.st.wYear - u1.st.wYear; break;
    case IV_QUARTER: result = (double)(u2.st.wYear * 4 + (u2.st.wMonth - 1) / 3) - (u1.st.wYear * 4 + (u1.st.wMonth - 1) / 3); break;
    case IV_MONTH: result = (double)(u2.st.wYear * 12 + u2.st.wMonth) - (u1.st.wYear * 12 + u1.st.wMonth); break;
    case IV_DAYOFYEAR:
    case IV_DAY: result = day_floor(d2) - day_floor(d1); break;
    case IV_WEEKDAY: {
        double diff = day_floor(d2) - day_floor(d1);
        result = diff < 0 ? -floor(-diff / 7) : floor(diff / 7);
        break;
    }
    case IV_WEEK: {
        double b1 = day_floor(d1) - week_start_offset(&u1, first);
        double b2 = day_floor(d2) - week_start_offset(&u2, first);
        result = (b2 - b1) / 7;
        break;
    }
    case IV_HOUR: result = floor(d2 * 24 + 0.0000001) - floor(d1 * 24 + 0.0000001); break;
    case IV_MINUTE: result = floor(d2 * 1440 + 0.00001) - floor(d1 * 1440 + 0.00001); break;
    case IV_SECOND: result = floor((d2 - d1) * 86400 + (d2 >= d1 ? 0.5 : -0.5)); if (d2 < d1) result = -floor(-((d2 - d1) * 86400) + 0.5); break;
    }
    if (iv == IV_HOUR || iv == IV_MINUTE) {
        /* boundaries crossed, counted on whole seconds. */
        double s1 = floor(d1 * 86400 + 0.5), s2 = floor(d2 * 86400 + 0.5);
        double unit = iv == IV_HOUR ? 3600 : 60;
        result = floor(s2 / unit) - floor(s1 / unit);
    } else if (iv == IV_SECOND) {
        result = floor(d2 * 86400 + 0.5) - floor(d1 * 86400 + 0.5);
    }
    if (result > 2147483647.0 || result < -2147483648.0)
        return fail(f, E_VBS_OVERFLOW, L"DateDiff");
    ret_long(r, (LONG)result);
    return R_OK;
}

static int days_from_civil_v(LONG y, LONG m, LONG d)
{
    y -= m <= 2;
    LONG era = (y >= 0 ? y : y - 399) / 400;
    LONG yoe = y - era * 400;
    LONG doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    LONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static LONG week_of_year(LONG y, LONG days_since_epoch, LONG first, LONG rule)
{
    LONG jan1 = days_from_civil_v(y, 1, 1);
    LONG dow = ((jan1 % 7) + 11) % 7;
    LONG w = (dow - (first - 1) + 7) % 7;
    LONG start;
    if (rule == 1)
        start = jan1 - w;
    else if (rule == 2)
        start = w <= 3 ? jan1 - w : jan1 - w + 7;
    else
        start = w == 0 ? jan1 : jan1 - w + 7;
    return (days_since_epoch - start) >= 0 ? (days_since_epoch - start) / 7 + 1 : 0;
}

FN(datepart)
{
    int iv;
    int rc = interval_of(f, &a[0], L"DatePart", &iv);
    if (rc != R_OK)
        return rc;
    VARIANT *dv = plain(&a[1]);
    if (dv->vt == VT_NULL) {
        ret_null(r);
        return R_OK;
    }
    LONG first = 1, rule = 1;
    rc = first_day(f, a, n, 2, &first, L"DatePart");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 3)) {
        rc = get_long(f, &a[3], &rule, L"DatePart");
        if (rc != R_OK)
            return rc;
        if (rule < 0 || rule > 3)
            return fail(f, E_VBS_INVALID_CALL, L"DatePart");
        if (rule == 0)
            rule = 1;
    }
    DATE d;
    rc = to_date(f, dv, &d, L"DatePart");
    if (rc != R_OK)
        return rc;
    UDATE u;
    if (FAILED(VarUdateFromDate(d, 0, &u)))
        return fail(f, E_VBS_INVALID_CALL, L"DatePart");
    int x = 0;
    switch (iv) {
    case IV_YEAR: x = u.st.wYear; break;
    case IV_QUARTER: x = (u.st.wMonth - 1) / 3 + 1; break;
    case IV_MONTH: x = u.st.wMonth; break;
    case IV_DAYOFYEAR: x = u.wDayOfYear; break;
    case IV_DAY: x = u.st.wDay; break;
    case IV_WEEKDAY: x = (u.st.wDayOfWeek - (first - 1) + 7) % 7 + 1; break;
    case IV_HOUR: x = u.st.wHour; break;
    case IV_MINUTE: x = u.st.wMinute; break;
    case IV_SECOND: x = u.st.wSecond; break;
    case IV_WEEK: {
        LONG days = days_from_civil_v(u.st.wYear, u.st.wMonth, u.st.wDay);
        LONG wk = week_of_year(u.st.wYear, days, first, rule);
        if (wk == 0) {
            wk = week_of_year(u.st.wYear - 1, days, first, rule);
        }
        x = wk;
        break;
    }
    }
    ret_int(r, x);
    return R_OK;
}

FN(formatdatetime)
{
    VARIANT *v = plain(&a[0]);
    LONG fmt = 0;
    if (!missing(a, n, 1)) {
        int rc = get_long(f, &a[1], &fmt, L"FormatDateTime");
        if (rc != R_OK)
            return rc;
    }
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_TYPE_MISMATCH, L"FormatDateTime");
    if (fmt < 0 || fmt > 4)
        return fail(f, E_VBS_INVALID_CALL, L"FormatDateTime");
    DATE d;
    VARIANT t;
    VariantInit(&t);
    if (FAILED(VariantChangeTypeEx(&t, v, f->E->Lcid, 0, VT_DATE)))
        return fail(f, E_VBS_TYPE_MISMATCH, L"FormatDateTime");
    d = t.date;
    if (fmt == 0) {
        BSTR s;
        HRESULT hr = VarBstrFromDate(d, f->E->Lcid, 0, &s);
        if (FAILED(hr))
            return fail(f, E_VBS_INVALID_CALL, L"FormatDateTime");
        r->vt = VT_BSTR;
        r->bstrVal = s;
        return R_OK;
    }
    UDATE u;
    VarUdateFromDate(d, 0, &u);
    WCHAR buf[128];
    int len;
    if (fmt == 1 || fmt == 2)
        len = GetDateFormatW(f->E->Lcid, fmt == 1 ? DATE_LONGDATE : DATE_SHORTDATE, &u.st, nullptr, buf, 128);
    else if (fmt == 3)
        len = GetTimeFormatW(f->E->Lcid, 0, &u.st, nullptr, buf, 128);
    else
        len = GetTimeFormatW(f->E->Lcid, 0, &u.st, L"HH:mm", buf, 128);
    if (len <= 0)
        return fail(f, E_VBS_INVALID_CALL, L"FormatDateTime");
    ret_str(r, buf, len - 1);
    return R_OK;
}

/* ------------------------------------------------------------------ number formats */

static int number_format(Frame *f, VARIANT *a, UINT n, VARIANT *r, int kind, PCWSTR name)
{
    VARIANT *v = plain(&a[0]);
    LONG digits = -1, lead = -2, parens = -2, group = -2;
    if (!missing(a, n, 1) && get_long(f, &a[1], &digits, name) != R_OK)
        return R_ERR;
    if (!missing(a, n, 2) && get_long(f, &a[2], &lead, name) != R_OK)
        return R_ERR;
    if (!missing(a, n, 3) && get_long(f, &a[3], &parens, name) != R_OK)
        return R_ERR;
    if (!missing(a, n, 4) && get_long(f, &a[4], &group, name) != R_OK)
        return R_ERR;
    if (v->vt == VT_NULL)
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    if (digits < -1 || digits > 99)
        return fail(f, E_VBS_INVALID_CALL, name);
    VARIANT t;
    VariantInit(&t);
    if (FAILED(VariantChangeTypeEx(&t, v, f->E->Lcid, 0, VT_R8)))
        return fail(f, E_VBS_TYPE_MISMATCH, name);
    double dv = t.dblVal;
    if (kind == 2)
        dv *= 100;
    LCID lcid = f->E->Lcid;
    WCHAR sep[8], tsep[8], grp[16], neg[4], lz[4], digs[4], cneg[4], cpos[4], sym[16];
    GetLocaleInfoW(lcid, LOCALE_SDECIMAL, sep, 8);
    GetLocaleInfoW(lcid, LOCALE_STHOUSAND, tsep, 8);
    GetLocaleInfoW(lcid, LOCALE_SGROUPING, grp, 16);
    GetLocaleInfoW(lcid, LOCALE_INEGNUMBER, neg, 4);
    GetLocaleInfoW(lcid, LOCALE_ILZERO, lz, 4);
    GetLocaleInfoW(lcid, kind == 1 ? LOCALE_ICURRDIGITS : LOCALE_IDIGITS, digs, 4);
    GetLocaleInfoW(lcid, LOCALE_INEGCURR, cneg, 4);
    GetLocaleInfoW(lcid, LOCALE_ICURRENCY, cpos, 4);
    GetLocaleInfoW(lcid, LOCALE_SCURRENCY, sym, 16);
    if (kind == 1)
        GetLocaleInfoW(lcid, LOCALE_SMONDECIMALSEP, sep, 8);
    UINT grouping = 0;
    for (WCHAR *p = grp; *p; p++) {
        if (*p >= L'0' && *p <= L'9') {
            if (*p == L'0')
                break;
            grouping = grouping * 10 + (*p - L'0');
        }
    }
    int nd = digits >= 0 ? digits : digs[0] - L'0';
    if (digs[1] >= L'0' && digs[1] <= L'9' && digits < 0)
        nd = (digs[0] - L'0') * 10 + digs[1] - L'0';
    UINT leadzero = lead == -2 ? (lz[0] - L'0') : (lead == 0 ? 0 : 1);
    UINT neg_order = parens == -2 ? (neg[0] - L'0') : (parens == 0 ? 1 : 0);
    UINT g = group == -2 ? grouping : (group == 0 ? 0 : (grouping ? grouping : 3));
    WCHAR text[64];
    {
        VARIANT x;
        x.vt = VT_R8;
        x.dblVal = dv;
        BSTR s;
        double rounded = dv;
        _snwprintf(text, 64, L"%.*f", nd < 0 ? 0 : (nd > 15 ? 15 : nd), rounded);
        (void)x;
        (void)s;
    }
    {
        /* the exact digits come from the system's own conversion of the number. */
        VARIANT x, s;
        VariantInit(&s);
        x.vt = VT_R8;
        x.dblVal = dv;
        if (SUCCEEDED(VariantChangeTypeEx(&s, &x, 0x409, 0, VT_BSTR))) {
            _snwprintf(text, 64, L"%s", s.bstrVal);
            VariantClear(&s);
        }
    }
    if (wcschr(text, L'E') || wcschr(text, L'e')) {
        _snwprintf(text, 64, L"%.*f", nd < 0 ? 0 : (nd > 15 ? 15 : nd), dv);
    }
    WCHAR out[200];
    int len;
    if (kind == 1) {
        CURRENCYFMTW cf = {(UINT)nd, leadzero, g, sep, tsep, parens == -2 ? (UINT)(cneg[0] - L'0') : (parens == 0 ? 1 : 0),
                           (UINT)(cpos[0] - L'0'), sym};
        len = GetCurrencyFormatW(lcid, 0, text, &cf, out, 200);
    } else {
        NUMBERFMTW nf = {(UINT)nd, leadzero, g, sep, tsep, neg_order};
        len = GetNumberFormatW(lcid, 0, text, &nf, out, 200);
    }
    if (len <= 0)
        return fail(f, E_VBS_INVALID_CALL, name);
    len--;
    if (kind == 2) {
        if (out[len - 1] == L')') {
            out[len - 1] = L'%';
            out[len++] = L')';
        } else if (out[0] == L'-' && neg_order == 3) {
            out[len++] = L'-';
            out[len++] = L'%';
            len--;
            out[len - 1] = L'%';
        } else
            out[len++] = L'%';
        out[len] = 0;
    }
    ret_str(r, out, len);
    return R_OK;
}

FN(formatnumber) { return number_format(f, a, n, r, 0, L"FormatNumber"); }
FN(formatcurrency) { return number_format(f, a, n, r, 1, L"FormatCurrency"); }
FN(formatpercent) { return number_format(f, a, n, r, 2, L"FormatPercent"); }

/* ------------------------------------------------------------------ the host and the engine */

FN(rgb)
{
    LONG c[3];
    for (int i = 0; i < 3; i++) {
        int rc = get_long(f, &a[i], &c[i], L"RGB");
        if (rc != R_OK)
            return rc;
        if (c[i] < 0)
            return fail(f, E_VBS_INVALID_CALL, L"RGB");
        if (c[i] > 255)
            c[i] = 255;
    }
    ret_long(r, c[0] | (c[1] << 8) | (c[2] << 16));
    return R_OK;
}

FN(scriptengine) { ret_str(r, L"VBScript", 8); return R_OK; }
FN(scriptenginemajor) { ret_long(r, 10); return R_OK; }
FN(scriptengineminor) { ret_long(r, 8); return R_OK; }
FN(scriptenginebuild) { ret_long(r, 16384); return R_OK; }

FN(getlocale) { ret_long(r, (LONG)f->E->Lcid); return R_OK; }

FN(setlocale)
{
    LCID old = f->E->Lcid;
    VARIANT *v = plain(&a[0]);
    LCID id;
    if (v->vt == VT_BSTR) {
        id = LocaleNameToLCID(v->bstrVal, 0);
        if (!id && SysStringLen(v->bstrVal)) {
            LONG l;
            if (get_long(f, v, &l, L"SetLocale") != R_OK)
                return R_ERR;
            id = (LCID)l;
        }
    } else {
        LONG l;
        int rc = get_long(f, v, &l, L"SetLocale");
        if (rc != R_OK)
            return rc;
        id = (LCID)l;
    }
    if (id == 0)
        id = GetUserDefaultLCID();
    if (!IsValidLocale(id, LCID_INSTALLED))
        return fail(f, E_VBS_NO_LOCALE, L"SetLocale");
    f->E->Lcid = id;
    ret_long(r, (LONG)old);
    return R_OK;
}

FN(createobject)
{
    BSTR s;
    int rc = get_str(f, &a[0], &s, L"CreateObject");
    if (rc != R_OK)
        return rc;
    if (!SysStringLen(s)) {
        SysFreeString(s);
        return fail(f, E_VBS_INVALID_CALL, L"CreateObject");
    }
    rc = create_object(f, s, r);
    SysFreeString(s);
    return rc;
}

FN(getobject)
{
    BSTR path = nullptr, cls = nullptr;
    if (!missing(a, n, 0)) {
        int rc = get_str(f, &a[0], &path, L"GetObject");
        if (rc != R_OK)
            return rc;
    }
    if (!missing(a, n, 1)) {
        int rc = get_str(f, &a[1], &cls, L"GetObject");
        if (rc != R_OK) {
            SysFreeString(path);
            return rc;
        }
    }
    IDispatch *d = nullptr;
    int rc = R_OK;
    if (path && SysStringLen(path)) {
        HRESULT hr = CoGetObject(path, nullptr, &IID_IDispatch, (void **)&d);
        if (FAILED(hr))
            rc = raise_hresult(f, hr, nullptr, nullptr);
    } else if (cls && SysStringLen(cls)) {
        CLSID clsid;
        IUnknown *u = nullptr;
        HRESULT hr = CLSIDFromProgID(cls, &clsid);
        if (SUCCEEDED(hr))
            hr = GetActiveObject(&clsid, nullptr, &u);
        if (SUCCEEDED(hr) && u) {
            u->lpVtbl->QueryInterface(u, &IID_IDispatch, (void **)&d);
            u->lpVtbl->Release(u);
        }
        if (!d)
            rc = fail(f, E_VBS_CANNOT_CREATE, L"GetObject");
    } else
        rc = fail(f, E_VBS_INVALID_CALL, L"GetObject");
    SysFreeString(path);
    SysFreeString(cls);
    if (rc == R_OK && d) {
        r->vt = VT_DISPATCH;
        r->pdispVal = d;
    }
    return rc;
}

FN(getref)
{
    BSTR s;
    int rc = get_str(f, &a[0], &s, L"GetRef");
    if (rc != R_OK)
        return rc;
    Sym *sym = sym_find(f->E, s, name_hash(s));
    Proc *p = sym && sym->Kind == SYM_PROC ? sym->Procedure : nullptr;
    Instance *me = nullptr;
    if (!p && f->Me) {
        Member *m = member_find(f->Me->Cls, s, name_hash(s));
        if (m && m->Kind == M_METHOD) {
            p = m->Method;
            me = f->Me;
        }
    }
    SysFreeString(s);
    if (!p)
        return fail(f, E_VBS_INVALID_CALL, L"GetRef");
    r->vt = VT_DISPATCH;
    r->pdispVal = proc_ref_create(f->E, p, me);
    return R_OK;
}

FN(msgbox)
{
    BSTR prompt = nullptr, title = nullptr;
    LONG buttons = 0;
    int rc = get_str(f, &a[0], &prompt, L"MsgBox");
    if (rc != R_OK)
        return rc;
    if (!missing(a, n, 1) && get_long(f, &a[1], &buttons, L"MsgBox") != R_OK)
        return R_ERR;
    if (!missing(a, n, 2) && get_str(f, &a[2], &title, L"MsgBox") != R_OK)
        return R_ERR;
    HWND window = f->E->Window;
    if (!window && f->E->Site) {
        IActiveScriptSiteWindow *sw = nullptr;
        if (SUCCEEDED(f->E->Site->lpVtbl->QueryInterface(f->E->Site, &IID_IActiveScriptSiteWindow, (void **)&sw)) && sw) {
            sw->lpVtbl->GetWindow(sw, &window);
            sw->lpVtbl->Release(sw);
        }
    }
    int res = MessageBoxW(window, prompt, title ? title : L"VBScript", (UINT)buttons);
    SysFreeString(prompt);
    SysFreeString(title);
    ret_int(r, res);
    return R_OK;
}

FN(inputbox)
{
    BSTR def = nullptr;
    if (!missing(a, n, 2) && get_str(f, &a[2], &def, L"InputBox") != R_OK)
        return R_ERR;
    ret_str(r, def ? def : L"", def ? SysStringLen(def) : 0);
    SysFreeString(def);
    return R_OK;
}

FN(eval)
{
    return builtin_eval(f, &a[0], r, FALSE, FALSE);
}

int builtin_eval(Frame *f, VARIANT *source, VARIANT *result, BOOL execute, BOOL global)
{
    Engine *e = f->E;
    BSTR text;
    int rc = get_str(f, source, &text, execute ? (global ? L"ExecuteGlobal" : L"Execute") : L"Eval");
    if (rc != R_OK)
        return rc;
    Unit *unit = parse_unit(e, text, 0, 0, !execute, execute);
    SysFreeString(text);
    if (!unit) {
        /* the compile error of text run at run time belongs to the statement that ran it. */
        vbs_free(e->Fault.Text);
        e->Fault.Text = nullptr;
        e->Fault.Compile = FALSE;
        e->Fault.Line = f->Line;
        e->Fault.Col = f->Col;
        return R_ERR;
    }
    unit->Global = global;
    Frame *target = f;
    if (global)
        target = &e->Global;
    BOOL ok = TRUE;
    if (execute)
        ok = unit_declare(e, unit, target);
    if (!ok) {
        unit_free(unit);
        return R_ERR;
    }
    unit_link(e, unit, target);
    Unit *saved_unit = target->Unit;
    ULONG sl = target->Line, sc = target->Col;
    target->Unit = unit;
    target->Line = 0;
    target->Col = 0;
    if (execute) {
        /* the executed text has an error handling of its own. */
        BOOL resume = target->ResumeNext;
        target->ResumeNext = FALSE;
        rc = run_statements(target, unit->Statements);
        target->ResumeNext = resume;
        if (rc >= R_EXIT_FOR)
            rc = R_OK;
    } else {
        Stmt *s = unit->Statements;
        rc = eval_unit_expression(e, s->u.Call->u.Operand, target, result);
    }
    target->Unit = saved_unit;
    target->Line = sl;
    target->Col = sc;
    if (unit->Procedures || unit->Classes) {
        unit->Next = e->Units;
        e->Units = unit;
    } else if (unit->NameCount == 0 || rc == R_OK)
        unit_free(unit);
    else {
        unit->Next = e->Units;
        e->Units = unit;
    }
    return rc;
}

/* ------------------------------------------------------------------ the table */

#define B(name, fn, lo, hi) {name, fn_##fn, lo, hi, 0}
#define BR(name, fn, lo, hi) {name, fn_##fn, lo, hi, 1}

static const BuiltinDef g_builtins[] = {
    B(L"Abs", abs, 1, 1), BR(L"Array", array, 0, 255), B(L"Asc", asc, 1, 1), B(L"AscB", ascb, 1, 1), B(L"AscW", ascw, 1, 1),
    B(L"Atn", atn, 1, 1), B(L"CBool", cbool, 1, 1), B(L"CByte", cbyte, 1, 1), B(L"CCur", ccur, 1, 1),
    B(L"CDate", cdate, 1, 1), B(L"CDbl", cdbl, 1, 1), B(L"Chr", chr, 1, 1), B(L"ChrB", chrb, 1, 1),
    B(L"ChrW", chrw, 1, 1), B(L"CInt", cint, 1, 1), B(L"CLng", clng, 1, 1), B(L"Cos", cos, 1, 1),
    B(L"CreateObject", createobject, 1, 2), B(L"CSng", csng, 1, 1), B(L"CStr", cstr, 1, 1), B(L"Date", date, 0, 0),
    B(L"DateAdd", dateadd, 3, 3), B(L"DateDiff", datediff, 3, 5), B(L"DatePart", datepart, 2, 4),
    B(L"DateSerial", dateserial, 3, 3), B(L"DateValue", datevalue, 1, 1), B(L"Day", day, 1, 1),
    B(L"Escape", escape, 1, 1), B(L"Eval", eval, 1, 1), B(L"Exp", exp, 1, 1), B(L"Filter", filter, 2, 4),
    B(L"Fix", fix, 1, 1), B(L"FormatCurrency", formatcurrency, 1, 5), B(L"FormatDateTime", formatdatetime, 1, 2),
    B(L"FormatNumber", formatnumber, 1, 5), B(L"FormatPercent", formatpercent, 1, 5), B(L"GetLocale", getlocale, 0, 0),
    B(L"GetObject", getobject, 0, 2), B(L"GetRef", getref, 1, 1), B(L"Hex", hex, 1, 1), B(L"Hour", hour, 1, 1),
    B(L"InputBox", inputbox, 1, 7), B(L"InStr", instr, 2, 4), B(L"InStrB", instrb, 2, 4), B(L"InStrRev", instrrev, 2, 4),
    B(L"Int", int, 1, 1), BR(L"IsArray", isarray, 1, 1), BR(L"IsDate", isdate, 1, 1), BR(L"IsEmpty", isempty, 1, 1),
    BR(L"IsNull", isnull, 1, 1), BR(L"IsNumeric", isnumeric, 1, 1), BR(L"IsObject", isobject, 1, 1),
    B(L"Join", join, 1, 2), B(L"LBound", lbound, 1, 2), B(L"LCase", lcase, 1, 1), B(L"Left", left, 2, 2),
    B(L"LeftB", leftb, 2, 2), B(L"Len", len, 1, 1), B(L"LenB", lenb, 1, 1), B(L"Log", log, 1, 1),
    B(L"LTrim", ltrim, 1, 1), B(L"Mid", mid, 2, 3), B(L"MidB", midb, 2, 3), B(L"Minute", minute, 1, 1),
    B(L"Month", month, 1, 1), B(L"MonthName", monthname, 1, 2), B(L"MsgBox", msgbox, 1, 5), B(L"Now", now, 0, 0),
    B(L"Oct", oct, 1, 1), B(L"Replace", replace, 3, 6), B(L"RGB", rgb, 3, 3), B(L"Right", right, 2, 2),
    B(L"RightB", rightb, 2, 2), B(L"Rnd", rnd, 0, 1), B(L"Round", round, 1, 2), B(L"RTrim", rtrim, 1, 1),
    B(L"ScriptEngine", scriptengine, 0, 0), B(L"ScriptEngineBuildVersion", scriptenginebuild, 0, 0),
    B(L"ScriptEngineMajorVersion", scriptenginemajor, 0, 0), B(L"ScriptEngineMinorVersion", scriptengineminor, 0, 0),
    B(L"Second", second, 1, 1), B(L"SetLocale", setlocale, 1, 1), B(L"Sgn", sgn, 1, 1), B(L"Sin", sin, 1, 1),
    B(L"Space", space, 1, 1), B(L"Split", split, 1, 4), B(L"Sqr", sqr, 1, 1), B(L"StrComp", strcomp, 2, 3),
    B(L"String", string, 2, 2), B(L"StrReverse", strreverse, 1, 1), B(L"Tan", tan, 1, 1), B(L"Time", time, 0, 0),
    B(L"Timer", timer, 0, 0), B(L"TimeSerial", timeserial, 3, 3), B(L"TimeValue", timevalue, 1, 1),
    B(L"Trim", trim, 1, 1), BR(L"TypeName", typename, 1, 1), B(L"UBound", ubound, 1, 2), B(L"UCase", ucase, 1, 1),
    B(L"Unescape", unescape, 1, 1), BR(L"VarType", vartype, 1, 1), B(L"Weekday", weekday, 1, 2),
    B(L"WeekdayName", weekdayname, 1, 3), B(L"Year", year, 1, 1),
};

const BuiltinDef *builtin_find(PCWSTR name, ULONG hash)
{
    for (UINT i = 0; i < sizeof(g_builtins) / sizeof(g_builtins[0]); i++)
        if (name_equal(g_builtins[i].Name, name))
            return &g_builtins[i];
    return nullptr;
}

static const struct {
    PCWSTR Name;
    LONG Value;
} g_constants[] = {
    {L"vbTrue", -1}, {L"vbFalse", 0}, {L"vbUseDefault", -2}, {L"vbBinaryCompare", 0}, {L"vbTextCompare", 1},
    {L"vbDatabaseCompare", 2}, {L"vbGeneralDate", 0}, {L"vbLongDate", 1}, {L"vbShortDate", 2}, {L"vbLongTime", 3},
    {L"vbShortTime", 4}, {L"vbSunday", 1}, {L"vbMonday", 2}, {L"vbTuesday", 3}, {L"vbWednesday", 4},
    {L"vbThursday", 5}, {L"vbFriday", 6}, {L"vbSaturday", 7}, {L"vbUseSystem", 0}, {L"vbUseSystemDayOfWeek", 0},
    {L"vbFirstJan1", 1}, {L"vbFirstFourDays", 2}, {L"vbFirstFullWeek", 3}, {L"vbEmpty", 0}, {L"vbNull", 1},
    {L"vbInteger", 2}, {L"vbLong", 3}, {L"vbSingle", 4}, {L"vbDouble", 5}, {L"vbCurrency", 6}, {L"vbDate", 7},
    {L"vbString", 8}, {L"vbObject", 9}, {L"vbError", 10}, {L"vbBoolean", 11}, {L"vbVariant", 12},
    {L"vbDataObject", 13}, {L"vbDecimal", 14}, {L"vbByte", 17}, {L"vbArray", 8192}, {L"vbOKOnly", 0},
    {L"vbOKCancel", 1}, {L"vbAbortRetryIgnore", 2}, {L"vbYesNoCancel", 3}, {L"vbYesNo", 4}, {L"vbRetryCancel", 5},
    {L"vbCritical", 16}, {L"vbQuestion", 32}, {L"vbExclamation", 48}, {L"vbInformation", 64},
    {L"vbDefaultButton1", 0}, {L"vbDefaultButton2", 256}, {L"vbDefaultButton3", 512}, {L"vbDefaultButton4", 768},
    {L"vbApplicationModal", 0}, {L"vbSystemModal", 4096}, {L"vbOK", 1}, {L"vbCancel", 2}, {L"vbAbort", 3},
    {L"vbRetry", 4}, {L"vbIgnore", 5}, {L"vbYes", 6}, {L"vbNo", 7}, {L"vbBlack", 0}, {L"vbRed", 255},
    {L"vbGreen", 65280}, {L"vbYellow", 65535}, {L"vbBlue", 16711680}, {L"vbMagenta", 16711935},
    {L"vbCyan", 16776960}, {L"vbWhite", 16777215}, {L"vbObjectError", (LONG)0x80040000},
    {L"vbMsgBoxHelpButton", 16384}, {L"vbMsgBoxSetForeground", 65536}, {L"vbMsgBoxRight", 524288},
    {L"vbMsgBoxRtlReading", 1048576},
};

BOOL constant_find(PCWSTR name, VARIANT *out)
{
    VariantInit(out);
    for (UINT i = 0; i < sizeof(g_constants) / sizeof(g_constants[0]); i++)
        if (name_equal(g_constants[i].Name, name)) {
            LONG v = g_constants[i].Value;
            PCWSTR cn = g_constants[i].Name;
            BOOL is_long = !wcsncmp(cn, L"vbMsgBox", 8) || !wcscmp(cn, L"vbObjectError") || (v > 8192 && v != 4096);
            if (!is_long && v >= -32768 && v <= 32767) {
                out->vt = VT_I2;
                out->iVal = (SHORT)v;
            } else {
                out->vt = VT_I4;
                out->lVal = v;
            }
            return TRUE;
        }
    static const struct {
        PCWSTR Name;
        PCWSTR Value;
    } s[] = {{L"vbCr", L"\r"}, {L"vbLf", L"\n"}, {L"vbCrLf", L"\r\n"}, {L"vbNewLine", L"\r\n"},
             {L"vbFormFeed", L"\f"}, {L"vbVerticalTab", L"\v"}, {L"vbTab", L"\t"}, {L"vbNullChar", L"\0"},
             {L"vbNullString", L""}};
    for (UINT i = 0; i < sizeof(s) / sizeof(s[0]); i++)
        if (name_equal(s[i].Name, name)) {
            out->vt = VT_BSTR;
            UINT len = name_equal(name, L"vbNullChar") ? 1 : (UINT)wcslen(s[i].Value);
            out->bstrVal = name_equal(name, L"vbNullString") ? nullptr : SysAllocStringLen(s[i].Value, len);
            return TRUE;
        }
    return FALSE;
}
