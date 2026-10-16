/*
 * the x86 helpers compilers call in msvcrt: _ftol and its kin, which
 * truncate st(0) to a 64-bit integer in edx:eax, the _CI functions of the
 * math library, whose arguments are on the x87 stack, the last one on
 * top, and the _adj_ fdiv workarounds, plain instructions on processors
 * without the flaw.
 */
    .text

    .globl  __ftol
    .globl  __ftol2
    .globl  __ftol2_sse
    .globl  __ftol2_sse_excpt
__ftol:
__ftol2:
__ftol2_sse:
__ftol2_sse_excpt:
    pushl   %ebp
    movl    %esp, %ebp
    subl    $16, %esp
    fnstcw  -2(%ebp)
    movzwl  -2(%ebp), %eax
    orl     $0x0C00, %eax
    movw    %ax, -4(%ebp)
    fldcw   -4(%ebp)
    fistpll -12(%ebp)
    fldcw   -2(%ebp)
    movl    -12(%ebp), %eax
    movl    -8(%ebp), %edx
    leave
    retl

/* one argument: st(0) to the c function's double, its result back in st(0). */
.macro CI1 name, function
    .globl  __CI\name
__CI\name:
    subl    $8, %esp
    fstpl   (%esp)
    calll   _\function
    addl    $8, %esp
    retl
.endm

/* two arguments: st(1) the first, st(0) the second. */
.macro CI2 name, function
    .globl  __CI\name
__CI\name:
    subl    $16, %esp
    fstpl   8(%esp)
    fstpl   (%esp)
    calll   _\function
    addl    $16, %esp
    retl
.endm

    CI1 acos, acos
    CI1 asin, asin
    CI1 atan, atan
    CI2 atan2, atan2
    CI1 cos, cos
    CI1 cosh, cosh
    CI1 exp, exp
    CI2 fmod, fmod
    CI1 log, log
    CI1 log10, log10
    CI2 pow, pow
    CI1 sin, sin
    CI1 sinh, sinh
    CI1 sqrt, sqrt
    CI1 tan, tan
    CI1 tanh, tanh

/* st(0) divided by the operand, which sits after the return address. */
    .globl  __adj_fdiv_m32
__adj_fdiv_m32:
    movl    4(%esp), %eax
    fdivs   (%eax)
    retl    $4
    .globl  __adj_fdiv_m64
__adj_fdiv_m64:
    movl    4(%esp), %eax
    fdivl   (%eax)
    retl    $4
    .globl  __adj_fdiv_m16i
__adj_fdiv_m16i:
    movl    4(%esp), %eax
    fidivs  (%eax)
    retl    $4
    .globl  __adj_fdiv_m32i
__adj_fdiv_m32i:
    movl    4(%esp), %eax
    fidivl  (%eax)
    retl    $4
    .globl  __adj_fdivr_m32
__adj_fdivr_m32:
    movl    4(%esp), %eax
    fdivrs  (%eax)
    retl    $4
    .globl  __adj_fdivr_m64
__adj_fdivr_m64:
    movl    4(%esp), %eax
    fdivrl  (%eax)
    retl    $4
    .globl  __adj_fdivr_m16i
__adj_fdivr_m16i:
    movl    4(%esp), %eax
    fidivrs (%eax)
    retl    $4
    .globl  __adj_fdivr_m32i
__adj_fdivr_m32i:
    movl    4(%esp), %eax
    fidivrl (%eax)
    retl    $4
    .globl  __adj_fdiv_r
    .globl  __safe_fdiv
__adj_fdiv_r:
__safe_fdiv:
    fdivrp  %st, %st(1)
    retl
    .globl  __safe_fdivr
__safe_fdivr:
    fdivp   %st, %st(1)
    retl
    .globl  __adj_fprem
    .globl  __safe_fprem
__adj_fprem:
__safe_fprem:
    fprem
    retl
    .globl  __adj_fprem1
    .globl  __safe_fprem1
__adj_fprem1:
__safe_fprem1:
    fprem1
    retl
    .globl  __adj_fpatan
__adj_fpatan:
    fpatan
    retl
    .globl  __adj_fptan
__adj_fptan:
    fptan
    retl
