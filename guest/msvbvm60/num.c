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
/*
 * the helper that stood in for a division on a processor with the flaw in it: the register holds the operation in its low three bits
 * and the register of the stack in the next three, and each case divides with the instruction they name.
 */
__attribute__((naked)) void vb__adj_fdiv_r(void)
{
    __asm__(
        "andl $0x3f, %eax\n\t"
        "jmp *.Lfdrtab(,%eax,4)\n"
        ".Lfdr0:\n\t.byte 0xd8, 0xf0\n\tret\n"
        ".Lfdr1:\n\tint $6\n"
        ".Lfdr2:\n\t.byte 0xd8, 0xf8\n\tret\n"
        ".Lfdr3:\n\tint $6\n"
        ".Lfdr4:\n\t.byte 0xdc, 0xf8\n\tret\n"
        ".Lfdr5:\n\t.byte 0xde, 0xf8\n\tret\n"
        ".Lfdr6:\n\t.byte 0xdc, 0xf0\n\tret\n"
        ".Lfdr7:\n\t.byte 0xde, 0xf0\n\tret\n"
        ".Lfdr8:\n\t.byte 0xd8, 0xf1\n\tret\n"
        ".Lfdr9:\n\tint $6\n"
        ".Lfdr10:\n\t.byte 0xd8, 0xf9\n\tret\n"
        ".Lfdr11:\n\tint $6\n"
        ".Lfdr12:\n\t.byte 0xdc, 0xf9\n\tret\n"
        ".Lfdr13:\n\t.byte 0xde, 0xf9\n\tret\n"
        ".Lfdr14:\n\t.byte 0xdc, 0xf1\n\tret\n"
        ".Lfdr15:\n\t.byte 0xde, 0xf1\n\tret\n"
        ".Lfdr16:\n\t.byte 0xd8, 0xf2\n\tret\n"
        ".Lfdr17:\n\tint $6\n"
        ".Lfdr18:\n\t.byte 0xd8, 0xfa\n\tret\n"
        ".Lfdr19:\n\tint $6\n"
        ".Lfdr20:\n\t.byte 0xdc, 0xfa\n\tret\n"
        ".Lfdr21:\n\t.byte 0xde, 0xfa\n\tret\n"
        ".Lfdr22:\n\t.byte 0xdc, 0xf2\n\tret\n"
        ".Lfdr23:\n\t.byte 0xde, 0xf2\n\tret\n"
        ".Lfdr24:\n\t.byte 0xd8, 0xf3\n\tret\n"
        ".Lfdr25:\n\tint $6\n"
        ".Lfdr26:\n\t.byte 0xd8, 0xfb\n\tret\n"
        ".Lfdr27:\n\tint $6\n"
        ".Lfdr28:\n\t.byte 0xdc, 0xfb\n\tret\n"
        ".Lfdr29:\n\t.byte 0xde, 0xfb\n\tret\n"
        ".Lfdr30:\n\t.byte 0xdc, 0xf3\n\tret\n"
        ".Lfdr31:\n\t.byte 0xde, 0xf3\n\tret\n"
        ".Lfdr32:\n\t.byte 0xd8, 0xf4\n\tret\n"
        ".Lfdr33:\n\tint $6\n"
        ".Lfdr34:\n\t.byte 0xd8, 0xfc\n\tret\n"
        ".Lfdr35:\n\tint $6\n"
        ".Lfdr36:\n\t.byte 0xdc, 0xfc\n\tret\n"
        ".Lfdr37:\n\t.byte 0xde, 0xfc\n\tret\n"
        ".Lfdr38:\n\t.byte 0xdc, 0xf4\n\tret\n"
        ".Lfdr39:\n\t.byte 0xde, 0xf4\n\tret\n"
        ".Lfdr40:\n\t.byte 0xd8, 0xf5\n\tret\n"
        ".Lfdr41:\n\tint $6\n"
        ".Lfdr42:\n\t.byte 0xd8, 0xfd\n\tret\n"
        ".Lfdr43:\n\tint $6\n"
        ".Lfdr44:\n\t.byte 0xdc, 0xfd\n\tret\n"
        ".Lfdr45:\n\t.byte 0xde, 0xfd\n\tret\n"
        ".Lfdr46:\n\t.byte 0xdc, 0xf5\n\tret\n"
        ".Lfdr47:\n\t.byte 0xde, 0xf5\n\tret\n"
        ".Lfdr48:\n\t.byte 0xd8, 0xf6\n\tret\n"
        ".Lfdr49:\n\tint $6\n"
        ".Lfdr50:\n\t.byte 0xd8, 0xfe\n\tret\n"
        ".Lfdr51:\n\tint $6\n"
        ".Lfdr52:\n\t.byte 0xdc, 0xfe\n\tret\n"
        ".Lfdr53:\n\t.byte 0xde, 0xfe\n\tret\n"
        ".Lfdr54:\n\t.byte 0xdc, 0xf6\n\tret\n"
        ".Lfdr55:\n\t.byte 0xde, 0xf6\n\tret\n"
        ".Lfdr56:\n\t.byte 0xd8, 0xf7\n\tret\n"
        ".Lfdr57:\n\tint $6\n"
        ".Lfdr58:\n\t.byte 0xd8, 0xff\n\tret\n"
        ".Lfdr59:\n\tint $6\n"
        ".Lfdr60:\n\t.byte 0xdc, 0xff\n\tret\n"
        ".Lfdr61:\n\t.byte 0xde, 0xff\n\tret\n"
        ".Lfdr62:\n\t.byte 0xdc, 0xf7\n\tret\n"
        ".Lfdr63:\n\t.byte 0xde, 0xf7\n\tret\n"
        ".balign 4\n.Lfdrtab:\n"
        ".long .Lfdr0\n"
        ".long .Lfdr1\n"
        ".long .Lfdr2\n"
        ".long .Lfdr3\n"
        ".long .Lfdr4\n"
        ".long .Lfdr5\n"
        ".long .Lfdr6\n"
        ".long .Lfdr7\n"
        ".long .Lfdr8\n"
        ".long .Lfdr9\n"
        ".long .Lfdr10\n"
        ".long .Lfdr11\n"
        ".long .Lfdr12\n"
        ".long .Lfdr13\n"
        ".long .Lfdr14\n"
        ".long .Lfdr15\n"
        ".long .Lfdr16\n"
        ".long .Lfdr17\n"
        ".long .Lfdr18\n"
        ".long .Lfdr19\n"
        ".long .Lfdr20\n"
        ".long .Lfdr21\n"
        ".long .Lfdr22\n"
        ".long .Lfdr23\n"
        ".long .Lfdr24\n"
        ".long .Lfdr25\n"
        ".long .Lfdr26\n"
        ".long .Lfdr27\n"
        ".long .Lfdr28\n"
        ".long .Lfdr29\n"
        ".long .Lfdr30\n"
        ".long .Lfdr31\n"
        ".long .Lfdr32\n"
        ".long .Lfdr33\n"
        ".long .Lfdr34\n"
        ".long .Lfdr35\n"
        ".long .Lfdr36\n"
        ".long .Lfdr37\n"
        ".long .Lfdr38\n"
        ".long .Lfdr39\n"
        ".long .Lfdr40\n"
        ".long .Lfdr41\n"
        ".long .Lfdr42\n"
        ".long .Lfdr43\n"
        ".long .Lfdr44\n"
        ".long .Lfdr45\n"
        ".long .Lfdr46\n"
        ".long .Lfdr47\n"
        ".long .Lfdr48\n"
        ".long .Lfdr49\n"
        ".long .Lfdr50\n"
        ".long .Lfdr51\n"
        ".long .Lfdr52\n"
        ".long .Lfdr53\n"
        ".long .Lfdr54\n"
        ".long .Lfdr55\n"
        ".long .Lfdr56\n"
        ".long .Lfdr57\n"
        ".long .Lfdr58\n"
        ".long .Lfdr59\n"
        ".long .Lfdr60\n"
        ".long .Lfdr61\n"
        ".long .Lfdr62\n"
        ".long .Lfdr63\n"
    );
}

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

