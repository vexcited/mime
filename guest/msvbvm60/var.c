/*
 * variants: copying, moving and freeing, the conversions to numbers and text, comparison and arithmetic, and the
 * variant forms of the library functions.
 */
#include "vb.h"

/* ------------------------------------------------------------------ freeing, moving and copying */

VB_THIS void vb___vbaFreeVar(VARIANT *v) { VariantClear(v); V_VT(v) = VT_EMPTY; }

VB_C void vb___vbaFreeVarList(unsigned int count, VARIANT *first, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, first);
    VARIANT *p = first;
    for (unsigned int i = 0; i < count; i++) {
        VariantClear(p);
        if (i + 1 < count)
            p = __builtin_va_arg(ap, VARIANT *);
    }
    __builtin_va_end(ap);
}

VB_FAST VARIANT *vb___vbaVarMove(VARIANT *dst, VARIANT *src)
{
    VariantClear(dst);
    *dst = *src;
    V_VT(src) = VT_EMPTY;
    return dst;
}

VB_FAST VARIANT *vb___vbaVarDup(VARIANT *dst, VARIANT *src)
{
    VariantInit(dst);
    VbCheck(VbCopyInd(dst, src));
    return dst;
}

VB_FAST VARIANT *vb___vbaVarCopy(VARIANT *dst, VARIANT *src)
{
    VariantClear(dst);
    VbCheck(VbCopyInd(dst, src));
    return dst;
}

VB_STD VARIANT *vb___vbaVarSetVar(VARIANT *dst, VARIANT *src)
{
    VariantClear(dst);
    VbCheck(VbCopyInd(dst, src));
    return dst;
}

/* a by reference variant that stands for a variant argument. */
VB_THIS VARIANT *vb___vbaVargVar(VARIANT *src, VARIANT *dst)
{
    if (V_VT(src) & VT_BYREF) {
        *dst = *src;
    } else {
        V_VT(dst) = VT_VARIANT | VT_BYREF;
        V_VARIANTREF(dst) = src;
    }
    return dst;
}

VB_FAST VARIANT *vb___vbaVarVargNofree(VARIANT *tmp, VARIANT *v)
{
    if (V_VT(v) & VT_BYREF) {
        if (V_VT(v) == (VT_VARIANT | VT_BYREF))
            return V_VARIANTREF(v);
        VariantInit(tmp);
        VbCopyInd(tmp, v);
        return tmp;
    }
    return v;
}

VB_FAST VARIANT *vb___vbaVargVarCopy(VARIANT *dst, VARIANT *src)
{
    VariantInit(dst);
    VbCheck(VariantCopy(dst, src));
    return dst;
}

VB_FAST VARIANT *vb___vbaVargVarMove(VARIANT *dst, VARIANT *src)
{
    *dst = *src;
    V_VT(src) = VT_EMPTY;
    return dst;
}

/* ------------------------------------------------------------------ conversions */

static void convert(VARIANT *out, const VARIANT *source, VARTYPE vt)
{
    VARIANT masked = *source;
    const VARIANT *v = &masked;
    V_VT(&masked) &= 0x7FFF;
    if (V_VT(v) == VT_NULL)
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    VariantInit(out);
    VbCheck(VariantChangeTypeEx(out, v, VbLocale(), 0, vt));
}

VB_STD short vb___vbaI2Var(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_I2);
    return V_I2(&t);
}

VB_THIS LONG vb___vbaI4Var(void *unused, VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_I4);
    return V_I4(&t);
}

VB_STD float vb___vbaR4Var(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_R4);
    return V_R4(&t);
}

VB_STD double vb___vbaR8Var(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_R8);
    return V_R8(&t);
}

VB_STD unsigned char vb___vbaUI1Var(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_UI1);
    return V_UI1(&t);
}

VB_STD short vb___vbaBoolVar(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_BOOL);
    return V_BOOL(&t);
}

/* null counts as false. */
VB_STD short vb___vbaBoolVarNull(VARIANT *v)
{
    if (V_VT(v) == VT_NULL)
        return 0;
    return vb___vbaBoolVar(v);
}

VB_STD double vb___vbaDateVar(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_DATE);
    return V_DATE(&t);
}

VB_STD double vb___vbaDateR8(double d) { return d; }

VB_STD const char *vb___vbaStrVarCopy(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_BSTR);
    BSTR b = V_BSTR(&t);
    return (const char *)b;
}

VB_STD BSTR vb___vbaStrVarMove(VARIANT *v)
{
    if (V_VT(v) != VT_BSTR) {
        VARIANT t;
        convert(&t, v, VT_BSTR);
        VariantClear(v);
        *v = t;
    }
    V_VT(v) = VT_EMPTY;
    return V_BSTR(v);
}

/* a borrowed string when the variant holds one, else a converted copy that the caller frees through the slot. */
VB_STD BSTR vb___vbaStrVarVal(BSTR *temp, VARIANT *v)
{
    if (V_VT(v) == VT_BSTR) {
        *temp = nullptr;
        return V_BSTR(v);
    }
    BSTR b = (BSTR)vb___vbaStrVarCopy(v);
    *temp = b;
    return b;
}

VB_STD BSTR vb___vbaStrErrVarCopy(VARIANT *v) { return (BSTR)vb___vbaStrVarCopy(v); }

VB_STD IUnknown *vb___vbaUnkVar(VARIANT *v)
{
    VARIANT t;
    VariantInit(&t);
    VbCheck(VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_UNKNOWN));
    return V_UNKNOWN(&t);
}

VB_STD IDispatch *vb___vbaObjVar(VARIANT *v)
{
    VARIANT t;
    VariantInit(&t);
    if (V_VT(v) == VT_DISPATCH || (V_VT(v) == (VT_DISPATCH | VT_BYREF))) {
        IDispatch *d = V_VT(v) & VT_BYREF ? *(IDispatch **)V_BYREF(v) : V_DISPATCH(v);
        return d;
    }
    VbCheck(VariantChangeTypeEx(&t, v, VbLocale(), 0, VT_DISPATCH));
    return V_DISPATCH(&t);
}

VB_STD VARIANT *vb___vbaVarSetObj(VARIANT *dst, IUnknown *obj)
{
    VariantClear(dst);
    V_VT(dst) = VT_DISPATCH;
    V_DISPATCH(dst) = (IDispatch *)obj;
    return dst;
}

VB_STD VARIANT *vb___vbaVarSetObjAddref(VARIANT *dst, IUnknown *obj)
{
    VariantClear(dst);
    V_VT(dst) = VT_DISPATCH;
    V_DISPATCH(dst) = (IDispatch *)obj;
    if (obj)
        obj->lpVtbl->AddRef(obj);
    return dst;
}

VB_STD VARIANT *vb___vbaVerifyVarObj(VARIANT *v)
{
    VARTYPE vt = V_VT(v) & VT_TYPEMASK;
    if (vt != VT_DISPATCH && vt != VT_UNKNOWN && vt != VT_EMPTY)
        VbRaise(VBERR_OBJECT_REQUIRED);
    return v;
}

/* ------------------------------------------------------------------ comparison */

/* the order of the operands is that the first argument is the right one. */
static int compare_variants(VARIANT *right, VARIANT *left, ULONG flags)
{
    /* the operands are read through their references and default members first, as every other operation does. */
    VARIANT l, r;
    VbValue(&l, left);
    VbValue(&r, right);
    int result;
    if (V_VT(&l) == VT_NULL || V_VT(&r) == VT_NULL) {
        result = 3;
    } else {
        HRESULT hr = VarCmp(&l, &r, VbLocale(), flags);
        if (FAILED(hr)) {
            VariantClear(&l);
            VariantClear(&r);
            VbCheck(hr);
        }
        result = (int)hr;
    }
    VariantClear(&l);
    VariantClear(&r);
    return result;
}

/* equal, not equal, less, greater, less or equal, greater or equal as booleans for the three ways a compare ends. */
static const short g_tst[6][4] = {
    {0, -1, 0, 0},   /* equal */
    {-1, 0, -1, 0},  /* not equal */
    {-1, 0, 0, 0},   /* less */
    {0, 0, -1, 0},   /* greater */
    {-1, -1, 0, 0},  /* less or equal */
    {0, -1, -1, 0}}; /* greater or equal */

VB_STD int vb___vbaVarTstEq(VARIANT *right, VARIANT *left) { return g_tst[0][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTstNe(VARIANT *right, VARIANT *left) { return g_tst[1][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTstLt(VARIANT *right, VARIANT *left) { return g_tst[2][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTextTstLe(VARIANT *right, VARIANT *left) { return g_tst[4][compare_variants(right, left, NORM_IGNORECASE)]; }
VB_STD int vb___vbaVarTstLe(VARIANT *right, VARIANT *left) { return g_tst[4][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTstGt(VARIANT *right, VARIANT *left) { return g_tst[3][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTstGe(VARIANT *right, VARIANT *left) { return g_tst[5][compare_variants(right, left, 0)]; }
VB_STD int vb___vbaVarTextTstEq(VARIANT *right, VARIANT *left) { return g_tst[0][compare_variants(right, left, NORM_IGNORECASE)]; }
VB_STD int vb___vbaVarTextTstNe(VARIANT *right, VARIANT *left) { return g_tst[1][compare_variants(right, left, NORM_IGNORECASE)]; }
VB_STD int vb___vbaVarTextTstLt(VARIANT *right, VARIANT *left) { return g_tst[2][compare_variants(right, left, NORM_IGNORECASE)]; }
VB_STD int vb___vbaVarTextTstGt(VARIANT *right, VARIANT *left) { return g_tst[3][compare_variants(right, left, NORM_IGNORECASE)]; }
VB_STD int vb___vbaVarTextTstGe(VARIANT *right, VARIANT *left) { return g_tst[5][compare_variants(right, left, NORM_IGNORECASE)]; }

static VARIANT *cmp_result(VARIANT *dest, int result, int op)
{
    VariantInit(dest);
    if (result == 3) {
        V_VT(dest) = VT_NULL;
    } else {
        V_VT(dest) = VT_BOOL;
        V_BOOL(dest) = g_tst[op][result];
    }
    return dest;
}

VB_STD VARIANT *vb___vbaVarCmpEq(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 0); }
VB_STD VARIANT *vb___vbaVarCmpNe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 1); }
VB_STD VARIANT *vb___vbaVarCmpLt(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 2); }
VB_STD VARIANT *vb___vbaVarCmpGt(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 3); }
VB_STD VARIANT *vb___vbaVarTextCmpEq(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 0); }
VB_STD VARIANT *vb___vbaVarTextCmpNe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 1); }
VB_STD VARIANT *vb___vbaVarTextCmpLt(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 2); }
VB_STD VARIANT *vb___vbaVarTextCmpGt(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 3); }
VB_STD VARIANT *vb___vbaVarTextCmpLe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 4); }
VB_STD VARIANT *vb___vbaVarTextCmpGe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, NORM_IGNORECASE), 5); }
VB_STD VARIANT *vb___vbaVarCmpLe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 4); }
VB_STD VARIANT *vb___vbaVarCmpGe(VARIANT *dest, VARIANT *right, VARIANT *left) { return cmp_result(dest, compare_variants(right, left, 0), 5); }

/* ------------------------------------------------------------------ arithmetic */

/*
 * the operations are those of oleaut32. an operand that is a reference is read through first, and a failed operation
 * is the runtime error its status stands for.
 */
typedef HRESULT(WINAPI *BinaryOp)(LPVARIANT, LPVARIANT, LPVARIANT);
typedef HRESULT(WINAPI *UnaryOp)(LPVARIANT, LPVARIANT);

static VARIANT *binary(BinaryOp op, VARIANT *dest, VARIANT *right, VARIANT *left)
{
    VARIANT l, r, out;
    VariantInit(&l);
    VariantInit(&r);
    VariantInit(&out);
    VbCheck(VbCopyInd(&l, left));
    VbCheck(VbCopyInd(&r, right));
    HRESULT hr = op(&l, &r, &out);
    VariantClear(&l);
    VariantClear(&r);
    VbCheck(hr);
    *dest = out;
    return dest;
}

static VARIANT *unary(UnaryOp op, VARIANT *dest, VARIANT *v)
{
    VARIANT in, out;
    VariantInit(&in);
    VariantInit(&out);
    VbCheck(VbCopyInd(&in, v));
    HRESULT hr = op(&in, &out);
    VariantClear(&in);
    VbCheck(hr);
    *dest = out;
    return dest;
}

VB_STD VARIANT *vb___vbaVarAdd(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarAdd, dest, right, left); }
VB_STD VARIANT *vb___vbaVarSub(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarSub, dest, right, left); }
VB_STD VARIANT *vb___vbaVarMul(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarMul, dest, right, left); }
VB_STD VARIANT *vb___vbaVarDiv(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarDiv, dest, right, left); }
VB_STD VARIANT *vb___vbaVarIdiv(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarIdiv, dest, right, left); }
VB_STD VARIANT *vb___vbaVarMod(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarMod, dest, right, left); }
VB_STD VARIANT *vb___vbaVarPow(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarPow, dest, right, left); }
VB_STD VARIANT *vb___vbaVarAnd(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarAnd, dest, right, left); }
VB_STD VARIANT *vb___vbaVarOr(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarOr, dest, right, left); }
VB_STD VARIANT *vb___vbaVarXor(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarXor, dest, right, left); }
VB_STD VARIANT *vb___vbaVarEqv(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarEqv, dest, right, left); }
VB_STD VARIANT *vb___vbaVarImp(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarImp, dest, right, left); }
VB_STD VARIANT *vb___vbaVarCat(VARIANT *dest, VARIANT *right, VARIANT *left) { return binary(VarCat, dest, right, left); }
VB_STD VARIANT *vb___vbaVarNeg(VARIANT *dest, VARIANT *v) { return unary(VarNeg, dest, v); }
VB_STD VARIANT *vb___vbaVarNot(VARIANT *dest, VARIANT *v) { return unary(VarNot, dest, v); }
VB_STD VARIANT *vb___vbaVarAbs(VARIANT *dest, VARIANT *v) { return unary(VarAbs, dest, v); }
VB_STD VARIANT *vb___vbaVarFix(VARIANT *dest, VARIANT *v) { return unary(VarFix, dest, v); }
VB_STD VARIANT *vb___vbaVarInt(VARIANT *dest, VARIANT *v) { return unary(VarInt, dest, v); }
VB_STD VARIANT *vb_rtcAbsVar(VARIANT *dest, VARIANT *v) { return unary(VarAbs, dest, v); }
VB_STD VARIANT *vb_rtcFixVar(VARIANT *dest, VARIANT *v) { return unary(VarFix, dest, v); }
VB_STD VARIANT *vb_rtcIntVar(VARIANT *dest, VARIANT *v) { return unary(VarInt, dest, v); }

VB_STD LONG vb___vbaLenVar(VARIANT *v, unsigned short *unused)
{
    VARIANT t;
    convert(&t, v, VT_BSTR);
    LONG n = (LONG)SysStringLen(V_BSTR(&t));
    VariantClear(&t);
    return n;
}

/* ------------------------------------------------------------------ immediate if, hex, errors */

VB_STD VARIANT *vb_rtcImmediateIf(VARIANT *dest, VARIANT *condition, const VARIANT *when_true, VARIANT *when_false)
{
    VARIANT t;
    convert(&t, condition, VT_BOOL);
    VariantInit(dest);
    VbCheck(VbCopyInd(dest, V_BOOL(&t) ? when_true : when_false));
    return dest;
}

VB_STD VARIANT *vb_rtcVarFromError(VARIANT *dest, VARIANT *error)
{
    VariantInit(dest);
    V_VT(dest) = VT_ERROR;
    V_ERROR(dest) = 0x800A0000u | (V_I4(error) & 0xFFFF);
    return dest;
}

VB_STD BSTR vb_rtcHexBstrFromVar(VARIANT *v)
{
    VARIANT t;
    convert(&t, v, VT_I4);
    WCHAR text[16];
    wsprintfW(text, L"%X", (unsigned)V_I4(&t));
    return SysAllocString(text);
}

VB_STD VARIANT *vb_rtcHexVarFromVar(VARIANT *dest, VARIANT *v)
{
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = vb_rtcHexBstrFromVar(v);
    return dest;
}
