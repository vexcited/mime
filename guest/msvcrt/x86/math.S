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

/*
 * the sse2 entries of the math library that compilers call with the arguments in xmm0 and xmm1 and the result in
 * xmm0: the function of the c library, with a stack for the arguments, in between. The single precision forms widen
 * their arguments and narrow the result.
 */
.macro SSE1 name, function
    .globl  ___libm_sse2_\name
___libm_sse2_\name:
    subl    $8, %esp
    movsd   %xmm0, (%esp)
    calll   _\function
    fstpl   (%esp)
    movsd   (%esp), %xmm0
    addl    $8, %esp
    retl
.endm

.macro SSE1F name, function
    .globl  ___libm_sse2_\name
___libm_sse2_\name:
    subl    $8, %esp
    cvtss2sd %xmm0, %xmm0
    movsd   %xmm0, (%esp)
    calll   _\function
    fstpl   (%esp)
    movsd   (%esp), %xmm0
    cvtsd2ss %xmm0, %xmm0
    addl    $8, %esp
    retl
.endm

.macro SSE2 name, function
    .globl  ___libm_sse2_\name
___libm_sse2_\name:
    subl    $16, %esp
    movsd   %xmm0, (%esp)
    movsd   %xmm1, 8(%esp)
    calll   _\function
    fstpl   (%esp)
    movsd   (%esp), %xmm0
    addl    $16, %esp
    retl
.endm

.macro SSE2F name, function
    .globl  ___libm_sse2_\name
___libm_sse2_\name:
    subl    $16, %esp
    cvtss2sd %xmm0, %xmm0
    cvtss2sd %xmm1, %xmm1
    movsd   %xmm0, (%esp)
    movsd   %xmm1, 8(%esp)
    calll   _\function
    fstpl   (%esp)
    movsd   (%esp), %xmm0
    cvtsd2ss %xmm0, %xmm0
    addl    $16, %esp
    retl
.endm

/* the precise entries have the same conventions, with the export names that start with one underscore less. */
.macro PRECISE1 name, function
    .globl  __libm_sse2_\name\()_precise
__libm_sse2_\name\()_precise:
    jmp     ___libm_sse2_\name
.endm

    SSE1 acos, acos
    SSE1 asin, asin
    SSE1 atan, atan
    SSE1 cos, cos
    SSE1 exp, exp
    SSE1 log, log
    SSE1 log10, log10
    SSE1 sin, sin
    SSE1 tan, tan
    SSE2 atan2, atan2
    SSE2 pow, pow
    SSE1F acosf, acos
    SSE1F asinf, asin
    SSE1F atanf, atan
    SSE1F cosf, cos
    SSE1F expf, exp
    SSE1F logf, log
    SSE1F log10f, log10
    SSE1F sinf, sin
    SSE1F tanf, tan
    SSE2F powf, pow

    PRECISE1 acos, acos
    PRECISE1 asin, asin
    PRECISE1 atan, atan
    PRECISE1 cos, cos
    PRECISE1 exp, exp
    PRECISE1 log, log
    PRECISE1 log10, log10
    PRECISE1 pow, pow
    PRECISE1 sin, sin
    PRECISE1 tan, tan

    .globl  __libm_sse2_sqrt_precise
__libm_sse2_sqrt_precise:
    sqrtsd  %xmm0, %xmm0
    retl
