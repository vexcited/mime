/*
 * numbers: the x87 helpers the compiled code calls for floating point, the checked integer conversions, the memory
 * accessors and the object reference helpers.
 */
#include "vb.h"

/* ------------------------------------------------------------------ transcendental functions, argument and result in st0 */

__attribute__((naked)) void vb__CIsin(void) { __asm__("fsin\n\tret"); }
__attribute__((naked)) void vb__CIcos(void) { __asm__("fcos\n\tret"); }
__attribute__((naked)) void vb__CItan(void) { __asm__("fptan\n\tfstp %st(0)\n\tret"); }
__attribute__((naked)) void vb__CIatan(void) { __asm__("fld1\n\tfpatan\n\tret"); }
__attribute__((naked)) void vb__CIlog(void) { __asm__("fldln2\n\tfxch %st(1)\n\tfyl2x\n\tret"); }
__attribute__((naked)) void vb__CIsqrt(void) { __asm__("fsqrt\n\tret"); }
__attribute__((naked)) void vb__CIexp(void)
{
    __asm__(".intel_syntax noprefix\n\tfldl2e\n\tfmulp st(1), st\n\tfld st(0)\n\tfrndint\n\tfsub st(1), st(0)\n\tfxch\n\tf2xm1\n\tfld1\n\tfaddp st(1), st\n\tfscale\n\tfstp st(1)\n\tret\n\t.att_syntax");
}

/* the division helpers worked around a flaw of early processors, and now they divide. */
__attribute__((naked)) void vb__adj_fdiv_m64(void) { __asm__("fdivl 4(%esp)\n\tret $8"); }
__attribute__((naked)) void vb__adj_fdiv_m32(void) { __asm__("fdivs 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fdiv_m32i(void) { __asm__("fidivl 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fdiv_m16i(void) { __asm__("fidivs 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fdivr_m64(void) { __asm__("fdivrl 4(%esp)\n\tret $8"); }
__attribute__((naked)) void vb__adj_fdivr_m32(void) { __asm__("fdivrs 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fdivr_m32i(void) { __asm__("fidivrl 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fdivr_m16i(void) { __asm__("fidivrs 4(%esp)\n\tret $4"); }
__attribute__((naked)) void vb__adj_fpatan(void) { __asm__("fpatan\n\tret"); }
__attribute__((naked)) void vb__adj_fptan(void) { __asm__("fptan\n\tret"); }
__attribute__((naked)) void vb__adj_fprem(void) { __asm__("fprem\n\tfnstsw %ax\n\tret"); }
__attribute__((naked)) void vb__adj_fprem1(void) { __asm__("fprem1\n\tfnstsw %ax\n\tret"); }

__attribute__((naked)) void vb__allmul(void)
{
    __asm__("movl 8(%esp), %eax\n\tmovl 16(%esp), %ecx\n\torl %eax, %ecx\n\tmovl 12(%esp), %ecx\n\tjnz 1f\n\t"
            "movl 4(%esp), %eax\n\tmull %ecx\n\tret $16\n"
            "1:\n\tpushl %ebx\n\tmull %ecx\n\tmovl %eax, %ebx\n\tmovl 8(%esp), %eax\n\tmull 20(%esp)\n\taddl %eax, %ebx\n\t"
            "movl 8(%esp), %eax\n\tmull %ecx\n\taddl %ebx, %edx\n\tpopl %ebx\n\tret $16");
}

/* ------------------------------------------------------------------ x87 value to integer, with the overflow check */

/* rounds to nearest even as CInt does, and an out of range value is an overflow. */
VB_STD __attribute__((naked)) int vb___vbaFpI4(void)
{
    __asm__("subl $4, %esp\n\tfnclex\n\tfistpl (%esp)\n\tfnstsw %ax\n\ttestb $1, %al\n\tjnz 1f\n\tmovl (%esp), %eax\n\taddl $4, %esp\n\tret\n"
            "1:\n\tfnclex\n\tcall _vb___vbaErrorOverflow@0");
}

VB_STD __attribute__((naked)) int vb___vbaFpI2(void)
{
    __asm__("subl $4, %esp\n\tfnclex\n\tfistps (%esp)\n\tfnstsw %ax\n\ttestb $1, %al\n\tjnz 1f\n\tmovswl (%esp), %eax\n\taddl $4, %esp\n\tret\n"
            "1:\n\tfnclex\n\tcall _vb___vbaErrorOverflow@0");
}

VB_STD __attribute__((naked)) int vb___vbaR8IntI4(void)
{
    __asm__("jmp _vb___vbaFpI4@0");
}

VB_STD __attribute__((naked)) int vb___vbaR8IntI2(void)
{
    __asm__("jmp _vb___vbaFpI2@0");
}

/* a double that is not a number or infinite is an overflow, a good one stays on the stack. */
__attribute__((naked)) double vb___vbaFpR8(void)
{
    __asm__("fld %st(0)\n\tfxam\n\tfnstsw %ax\n\tfstp %st(0)\n\ttestb $1, %ah\n\tjz 1f\n\ttestb $0x40, %ah\n\tjnz 1f\n\tcall _vb___vbaErrorOverflow@0\n1:\n\tret");
}

/* Int rounds toward minus infinity. */
__attribute__((naked)) double vb___vbaFPInt(void)
{
    __asm__("subl $8, %esp\n\tfnstcw (%esp)\n\tmovw (%esp), %ax\n\tandw $0xF3FF, %ax\n\torw $0x0400, %ax\n\tmovw %ax, 4(%esp)\n\tfldcw 4(%esp)\n\tfrndint\n\tfldcw (%esp)\n\taddl $8, %esp\n\tret");
}

/* ------------------------------------------------------------------ checked integer conversions */

VB_FAST short vb___vbaI2I4(LONG v)
{
    if ((short)v != v)
        VbRaise(VBERR_OVERFLOW);
    return (short)v;
}

VB_FAST LONG vb___vbaI4Abs(LONG v)
{
    if (v == (LONG)0x80000000)
        VbRaise(VBERR_OVERFLOW);
    return v < 0 ? -v : v;
}

VB_FAST unsigned char vb___vbaUI1I2(short v)
{
    if (v < 0 || v > 255)
        VbRaise(VBERR_OVERFLOW);
    return (unsigned char)v;
}

VB_FAST unsigned char vb___vbaUI1I4(LONG v)
{
    if (v < 0 || v > 255)
        VbRaise(VBERR_OVERFLOW);
    return (unsigned char)v;
}

/* two to the power of the product of y and the base two logarithm of x. */
__attribute__((naked)) static double pow_x87(double x, double y)
{
    __asm__(".intel_syntax noprefix\n\tfld qword ptr [esp+12]\n\tfld qword ptr [esp+4]\n\tfyl2x\n\tfld st(0)\n\tfrndint\n\tfsub st(1), st(0)\n\tfxch\n\tf2xm1\n\tfld1\n\tfaddp st(1), st\n\tfscale\n\tfstp st(1)\n\tret\n\t.att_syntax");
}

static double power(double x, double y)
{
    if (y == 0.0)
        return 1.0;
    if (x == 0.0) {
        if (y > 0)
            return 0.0;
        VbRaise(VBERR_DIVISION_BY_ZERO);
    }
    BOOL negative = FALSE;
    if (x < 0) {
        /* a negative base needs a whole exponent. */
        double r = y < 0 ? -y : y;
        if (r != (double)(long long)r)
            VbRaise(VBERR_INVALID_CALL);
        negative = ((long long)r & 1) != 0;
        x = -x;
    }
    double result = pow_x87(x, y);
    return negative ? -result : result;
}

VB_STD double vb___vbaPowerR8(double x, double y) { return power(x, y); }

/* ------------------------------------------------------------------ memory accessors and object references */

VB_STD int vb_VarPtr(int address) { return address; }

VB_STD int vb_GetMem2(short *src, short *dst)
{
    *dst = *src;
    return 0;
}

VB_STD int vb_GetMem4(LONG *src, LONG *dst)
{
    *dst = *src;
    return 0;
}

VB_STD int vb_PutMem2(short *dst, short v)
{
    *dst = v;
    return 0;
}

VB_STD int vb_PutMem4(LONG *dst, LONG v)
{
    *dst = v;
    return 0;
}

/* a new reference replaces the old one, the first form keeps the reference it is given. */
VB_STD IUnknown *vb___vbaObjSet(IUnknown **dst, IUnknown *src)
{
    IUnknown *old = *dst;
    *dst = src;
    if (old)
        old->lpVtbl->Release(old);
    return src;
}

VB_STD IUnknown *vb___vbaObjSetAddref(IUnknown **dst, IUnknown *src)
{
    if (src)
        src->lpVtbl->AddRef(src);
    IUnknown *old = *dst;
    *dst = src;
    if (old)
        old->lpVtbl->Release(old);
    return src;
}

static IUnknown *identity(IUnknown *o)
{
    IUnknown *id = nullptr;
    if (o && SUCCEEDED(o->lpVtbl->QueryInterface(o, &IID_IUnknown, (void **)&id)))
        id->lpVtbl->Release(id);
    return id;
}

VB_STD int vb___vbaObjIs(IUnknown *a, IUnknown *b) { return identity(a) == identity(b) ? -1 : 0; }

VB_STD IUnknown *vb___vbaCastObj(IUnknown *obj, const IID *iid)
{
    IUnknown *out = nullptr;
    if (!obj)
        return nullptr;
    VbCheck(obj->lpVtbl->QueryInterface(obj, iid, (void **)&out));
    return out;
}

VB_STD int vb___vbaCheckType(IUnknown *obj, const IID *iid)
{
    IUnknown *out = nullptr;
    if (!obj)
        return 0;
    if (FAILED(obj->lpVtbl->QueryInterface(obj, iid, (void **)&out)))
        VbRaise(VBERR_TYPE_MISMATCH);
    out->lpVtbl->Release(out);
    return -1;
}

/* an object of an external class is made from its class id, and its interface comes back in the slot. */
VB_STD int vb___vbaNew2(const CLSID *clsid, void **slot)
{
    IUnknown *old = *slot;
    IUnknown *obj = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, &IID_IDispatch, (void **)&obj);
    if (FAILED(hr))
        VbRaise(VBERR_CANNOT_CREATE_OBJECT);
    *slot = obj;
    if (old)
        old->lpVtbl->Release(old);
    return 0;
}
