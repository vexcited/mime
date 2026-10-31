/*
 * the floating point exception paths of the universal crt's math functions: _except1 settles the exceptions a
 * math function reports with the result the masked ones give, hands the unmasked ones to the program's handler as
 * an IEEE exception record and sets errno or calls _matherr. x64 works on an MXCSR image of the control word, x86
 * on the x87 one.
 */
#include "msvcrt_private.h"

#if defined(__x86_64__) || defined(__i386__)

struct _exception {
    int type;
    char *name;
    double arg1;
    double arg2;
    double retval;
};

int __cdecl _matherr(struct _exception *e);
int __cdecl fegetround(void);
int MsvcrtHasUserMatherr(void);

#ifdef _WIN64
/* the control word as an MXCSR image: the masks of the five exceptions and the rounding mode. */
#define CW_INVALID 0x80
#define CW_ZERODIVIDE 0x200
#define CW_OVERFLOW 0x400
#define CW_UNDERFLOW 0x800
#define CW_INEXACT 0x1000
#define CW_ROUND_MASK 0x6000
#define CW_ROUND_DOWN 0x2000
#define CW_ROUND_UP 0x4000
#define CW_ROUND_ZERO 0x6000
#else
#define CW_INVALID 0x01
#define CW_ZERODIVIDE 0x04
#define CW_OVERFLOW 0x08
#define CW_UNDERFLOW 0x10
#define CW_INEXACT 0x20
#define CW_ROUND_MASK 0xC00
#define CW_ROUND_DOWN 0x400
#define CW_ROUND_UP 0x800
#define CW_ROUND_ZERO 0xC00
#endif

/* the exceptions as the math functions report them, and the status flags they set. */
#define FPE_OVERFLOW 0x01
#define FPE_UNDERFLOW 0x02
#define FPE_ZERODIVIDE 0x04
#define FPE_INVALID 0x08
#define FPE_INEXACT 0x10
#define FPE_TLOSS 0x20

#define SW_INVALID 0x01
#define SW_ZERODIVIDE 0x04
#define SW_OVERFLOW 0x08
#define SW_UNDERFLOW 0x10
#define SW_INEXACT 0x20


#define STATUS_FLOAT_MULTIPLE_FAULTS ((NTSTATUS)0xC00002B4)
#define STATUS_FLOAT_MULTIPLE_TRAPS ((NTSTATUS)0xC00002B5)

#define DOUBLE_INFINITY 0x7FF0000000000000ull
#define DOUBLE_MAX 0x7FEFFFFFFFFFFFFFull
#define SIGN_BIT 0x8000000000000000ull

/* the record the exception handler gets: control bits, the flags, and the operands and result of the operation. */
typedef struct FPIEEE_VALUE {
    ULONG value[4];
    ULONG flags;
    ULONG reserved[3];
} FPIEEE_VALUE;

typedef struct FPIEEE_RECORD {
    ULONG control;
    ULONG cause;
    ULONG enable;
    ULONG status;
    FPIEEE_VALUE operand1;
    FPIEEE_VALUE operand2;
    FPIEEE_VALUE result;
} FPIEEE_RECORD;

#define VALUE_VALID 0x01
#define VALUE_FORMAT_MASK 0x1E
#define VALUE_FORMAT_FP64 0x02

static uint64_t bits_of(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static double from_bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof(x));
    return x;
}

/* ------------------------------------------------------------------ status flags */

#ifdef _WIN64

static unsigned get_csr(void)
{
    unsigned v;
    __asm__ volatile("stmxcsr %0" : "=m"(v));
    return v;
}

static void set_csr(unsigned v) { __asm__ volatile("ldmxcsr %0" : : "m"(v)); }

static void set_statfp(unsigned flags)
{
    if (flags & 0x3F)
        set_csr(get_csr() | (flags & 0x3F));
}

/* whether the processor has the denormals are zero bit, which MXCSR_MASK in an fxsave image says. */
static int daz_supported(void)
{
    static int known = -1;
    if (known < 0) {
        static unsigned char image[512] __attribute__((aligned(16)));
        __asm__ volatile("fxsave %0" : "=m"(image));
        unsigned mask = *(unsigned *)(image + 28);
        known = (mask ? mask : 0xFFBF) & 0x40 ? 1 : 0;
    }
    return known;
}

/* sets the masks, rounding and flush bits of MXCSR from a control word and returns the old register. */
static unsigned ctrlfp(ULONG_PTR cw)
{
    unsigned old = get_csr();
    unsigned wanted = (old & 0xFFFF807F) | (unsigned)(cw & 0xFFC0);
    if (!daz_supported())
        wanted &= ~0x40u;
    set_csr(wanted);
    return old;
}

#else

static const unsigned char g_huge[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, 0x10, 0x44};
static const unsigned char g_tiny[10] = {1, 0, 0, 0, 0, 0, 0, 0x80, 0x00, 0x30};

/* raises the flags by doing what sets them, which traps for the exceptions that are unmasked. */
static void set_statfp(unsigned flags)
{
    volatile int integer;
    volatile double d;
    if (flags & SW_INVALID) {
        __asm__ volatile("fldt %1\n\tfistpl %0\n\tfwait" : "=m"(integer) : "m"(g_huge) : "memory");
    }
    if (flags & SW_OVERFLOW) {
        __asm__ volatile("fldt %1\n\tfstpl %0\n\tfwait" : "=m"(d) : "m"(g_huge) : "memory");
    }
    if (flags & SW_UNDERFLOW) {
        __asm__ volatile("fldt %1\n\tfstpl %0\n\tfwait" : "=m"(d) : "m"(g_tiny) : "memory");
    }
    if (flags & SW_ZERODIVIDE) {
        __asm__ volatile("fldz\n\tfld1\n\tfdivp %%st, %%st(1)\n\tfstp %%st(0)\n\tfwait" : : : "memory");
    }
    if (flags & SW_INEXACT) {
        __asm__ volatile("fldpi\n\tfstpl %0\n\tfwait" : "=m"(d) : : "memory");
    }
}

#endif

/* ------------------------------------------------------------------ settling an exception */

/* the result and flags a masked exception has: returns whether every exception reported is settled. */
static int handle_exc(unsigned char fpe, double *result, unsigned cw)
{
    unsigned status = 0;
    unsigned remaining = fpe & 0x1F;

    if ((fpe & FPE_INVALID) && (cw & CW_INVALID)) {
        status = SW_INVALID;
        remaining = fpe & 0x17;
    } else if ((fpe & FPE_ZERODIVIDE) && (cw & CW_ZERODIVIDE)) {
        status = SW_ZERODIVIDE;
        remaining = fpe & 0x1B;
    } else if ((fpe & FPE_OVERFLOW) && (cw & CW_OVERFLOW)) {
        status = SW_OVERFLOW;
        /* the result goes to infinity or the largest number, depending on the rounding and the sign. */
        int positive = *result > 0.0;
        uint64_t magnitude;
        switch (cw & CW_ROUND_MASK) {
        case CW_ROUND_DOWN:
            magnitude = positive ? DOUBLE_MAX : DOUBLE_INFINITY;
            *result = from_bits(positive ? magnitude : (magnitude | SIGN_BIT));
            break;
        case CW_ROUND_UP:
            magnitude = positive ? DOUBLE_INFINITY : DOUBLE_MAX;
            *result = from_bits(positive ? magnitude : (magnitude | SIGN_BIT));
            break;
        case CW_ROUND_ZERO:
            *result = from_bits(positive ? DOUBLE_MAX : (DOUBLE_MAX | SIGN_BIT));
            break;
        default:
            *result = from_bits(positive ? DOUBLE_INFINITY : (DOUBLE_INFINITY | SIGN_BIT));
            break;
        }
        remaining = fpe & 0x1E;
    } else if ((fpe & FPE_UNDERFLOW) && (cw & CW_UNDERFLOW)) {
        /* the result comes scaled up by 2^1536 and is rounded again into the denormal range. */
        double value = *result;
        int inexact = (fpe & FPE_INEXACT) != 0;
        if (value == 0.0) {
            inexact = 1;
        } else {
            uint64_t u = bits_of(value);
            int exponent;
            uint64_t scaled;
            if ((u & DOUBLE_INFINITY) || !(u & 0xFFFFFFFFFFFFFull)) {
                exponent = (int)((u >> 52) & 0x7FF) - 2558;
                scaled = (u & 0x800FFFFFFFFFFFFFull) | 0x3FE0000000000000ull;
            } else {
                int top = 63 - __builtin_clzll(u & 0xFFFFFFFFFFFFFull);
                uint64_t shifted = (u & 0xFFFFFFFFFFFFFull) << (52 - top);
                shifted = value < 0.0 ? ((shifted & 0x7FEFFFFFFFFFFFFFull) | SIGN_BIT) : (shifted & 0xFFEFFFFFFFFFFFFFull);
                exponent = top - 2609;
                scaled = (shifted & 0x800FFFFFFFFFFFFFull) | 0x3FE0000000000000ull;
            }
            if (exponent < -1074) {
                *result = from_bits(scaled) * 0.0;
                inexact = 1;
            } else {
                int negative = from_bits(scaled) < 0.0;
                uint64_t m = (scaled & 0x000FFFFFFFFFFFFFull) | 0x0010000000000000ull;
                int round = 0, sticky = 0;
                if (exponent < -1021) {
                    for (int n = -1021 - exponent; n > 0; n--) {
                        if (m & 1)
                            inexact = 1;
                        if (round)
                            sticky = 1;
                        round = (int)(m & 1);
                        m >>= 1;
                    }
                }
                uint64_t out = negative ? (m | SIGN_BIT) : m;
                if (round || sticky) {
                    int increment;
                    switch (fegetround()) {
                    case 0:
                        increment = round && (sticky || (m & 1));
                        break;
                    case 0x100:
                        increment = negative;
                        break;
                    case 0x200:
                        increment = !negative;
                        break;
                    default:
                        increment = 0;
                        break;
                    }
                    if (increment)
                        out++;
                }
                *result = from_bits(out);
            }
        }
        remaining &= ~FPE_UNDERFLOW;
        status = inexact ? SW_UNDERFLOW : 0;
    }

    if ((fpe & FPE_INEXACT) && (cw & CW_INEXACT)) {
        status |= SW_INEXACT;
        remaining &= ~FPE_INEXACT;
    }
    if (status)
        set_statfp(status);
    return remaining == 0;
}

/* the status flags that were set, as the cause of the record. */
static unsigned current_status(void)
{
#ifdef _WIN64
    return get_csr() & 0x3F;
#else
    unsigned short sw;
    __asm__ volatile("fnstsw %0" : "=m"(sw));
    return sw & 0x3F;
#endif
}

/* raises the exception for the program's handler and takes the controls and result it leaves in the record. */
static void raise_exc_ex(FPIEEE_RECORD *record, ULONG_PTR *cw, unsigned char fpe, int op, const double *arg,
                         double *result)
{
    DWORD code = STATUS_INVALID_PARAMETER;
    record->cause = 0;
    record->enable = 0;
    record->status = 0;
    if (fpe & FPE_INEXACT) {
        code = STATUS_FLOAT_INEXACT_RESULT;
        record->cause |= 1;
    }
    if (fpe & FPE_UNDERFLOW) {
        code = STATUS_FLOAT_UNDERFLOW;
        record->cause |= 2;
    }
    if (fpe & FPE_OVERFLOW) {
        code = STATUS_FLOAT_OVERFLOW;
        record->cause |= 4;
    }
    if (fpe & FPE_ZERODIVIDE) {
        code = STATUS_FLOAT_DIVIDE_BY_ZERO;
        record->cause |= 8;
    }
    if (fpe & FPE_INVALID) {
        code = STATUS_FLOAT_INVALID_OPERATION;
        record->cause |= 0x10;
    }
    record->enable |= !(*cw & CW_INVALID) ? 0x10 : 0;
    record->enable |= !(*cw & CW_ZERODIVIDE) ? 8 : 0;
    record->enable |= !(*cw & CW_OVERFLOW) ? 4 : 0;
    record->enable |= !(*cw & CW_UNDERFLOW) ? 2 : 0;
    record->enable |= !(*cw & CW_INEXACT) ? 1 : 0;

    unsigned status = current_status();
    if (status & SW_INVALID)
        record->status |= 0x10;
    if (status & SW_ZERODIVIDE)
        record->status |= 8;
    if (status & SW_OVERFLOW)
        record->status |= 4;
    if (status & SW_UNDERFLOW)
        record->status |= 2;
    if (status & SW_INEXACT)
        record->status |= 1;

    switch (*cw & CW_ROUND_MASK) {
    case 0:
        record->control &= ~3u;
        break;
    case CW_ROUND_DOWN:
        record->control = (record->control & ~3u) | 1;
        break;
    case CW_ROUND_UP:
        record->control = (record->control & ~3u) | 2;
        break;
    default:
        record->control |= 3;
        break;
    }
#ifndef _WIN64
    switch (*cw & 0x300) {
    case 0:
        record->control = (record->control & ~0x1Cu) | 8;
        break;
    case 0x200:
        record->control = (record->control & ~0x1Cu) | 4;
        break;
    case 0x300:
        record->control &= ~0x1Cu;
        break;
    }
#endif
    record->control = (record->control & ~0x1FFE0u) | ((unsigned)(op & 0xFFF) << 5);
    record->operand1.flags = ((record->operand1.flags | VALUE_VALID) & ~VALUE_FORMAT_MASK) | VALUE_FORMAT_FP64;
    memcpy(record->operand1.value, arg, sizeof(double));
    record->result.flags = ((record->result.flags | VALUE_VALID) & ~VALUE_FORMAT_MASK) | VALUE_FORMAT_FP64;
    memcpy(record->result.value, result, sizeof(double));
#ifdef _WIN64
    set_csr(get_csr() & 0xFFFFFFC0);
#else
    __asm__ volatile("fnclex");
#endif
    ULONG_PTR argument = (ULONG_PTR)record;
    RaiseException(code, 0, 1, &argument);

    if (record->enable & 0x10)
        *cw &= ~(ULONG_PTR)CW_INVALID;
    if (record->enable & 8)
        *cw &= ~(ULONG_PTR)CW_ZERODIVIDE;
    if (record->enable & 4)
        *cw &= ~(ULONG_PTR)CW_OVERFLOW;
    if (record->enable & 2)
        *cw &= ~(ULONG_PTR)CW_UNDERFLOW;
    if (record->enable & 1)
        *cw &= ~(ULONG_PTR)CW_INEXACT;
    switch (record->control & 3) {
    case 0:
        *cw &= ~(ULONG_PTR)CW_ROUND_MASK;
        break;
    case 1:
        *cw = (*cw & ~(ULONG_PTR)CW_ROUND_MASK) | CW_ROUND_DOWN;
        break;
    case 2:
        *cw = (*cw & ~(ULONG_PTR)CW_ROUND_MASK) | CW_ROUND_UP;
        break;
    default:
        *cw |= CW_ROUND_MASK;
        break;
    }
#ifndef _WIN64
    switch ((record->control >> 2) & 7) {
    case 0:
        *cw = (*cw & 0xFFFFF0FF) | 0x300;
        break;
    case 1:
        *cw = (*cw & 0xFFFFF1FF) | 0x200;
        break;
    case 2:
        *cw &= 0xFFFFF3FF;
        break;
    }
#endif
    memcpy(result, record->result.value, sizeof(double));
}

static unsigned error_type(unsigned char fpe)
{
    if (fpe & FPE_TLOSS)
        return 5;
    if (fpe & FPE_INVALID)
        return 1;
    if (fpe & FPE_ZERODIVIDE)
        return 2;
    if (fpe & FPE_OVERFLOW)
        return 3;
    return 2 * (fpe & FPE_UNDERFLOW);
}

static void set_errno_from_matherr(unsigned type)
{
    if (type == 1)
        MsvcrtSetErrno(EDOM);
    else if (type == 2 || type == 3)
        MsvcrtSetErrno(ERANGE);
}

static const struct {
    int op;
    const char *name;
} g_functions[] = {
    {0x14, "exp"},   {0x1d, "pow"},   {0x1a, "log"},   {0x1b, "log10"}, {0x1f, "sinh"},  {0x13, "cosh"},
    {0x21, "tanh"},  {0x0e, "asin"},  {0x0d, "acos"},  {0x0f, "atan"},  {0x10, "atan2"}, {0x05, "sqrt"},
    {0x1e, "sin"},   {0x12, "cos"},   {0x20, "tan"},   {0x0c, "ceil"},  {0x0b, "floor"}, {0x15, "fabs"},
    {0x1c, "modf"},  {0x19, "ldexp"}, {0x11, "_cabs"}, {0x18, "_hypot"}, {0x16, "fmod"},  {0x17, "frexp"},
    {0x22, "_y0"},   {0x23, "_y1"},   {0x24, "_yn"},   {0x25, "_logb"}, {0x26, "_nextafter"},
};

static const char *function_name(int op)
{
    for (size_t i = 0; i < sizeof(g_functions) / sizeof(g_functions[0]); i++)
        if (g_functions[i].op == op)
            return g_functions[i].name;
    return nullptr;
}

/* reports the error to the program's _matherr and returns the result it leaves. */
static double umatherr(unsigned type, int op, double arg1, double arg2, double result, ULONG_PTR cw)
{
    const char *name = function_name(op);
#ifdef _WIN64
    ctrlfp(cw);
#else
    unsigned short control = (unsigned short)cw;
    __asm__ volatile("fldcw %0" : : "m"(control));
#endif
    if (!name) {
        set_errno_from_matherr(type);
        return result;
    }
    struct _exception e = {(int)type, (char *)name, arg1, arg2, result};
    if (!_matherr(&e))
        set_errno_from_matherr(type);
    return e.retval;
}

/*
 * settles the exceptions fpe of the function op, which got arg and computed result, under the control word cw.
 * x64 programs get the result back, x86 ones get it on the x87 stack with the control word in force.
 */
DECLSPEC_EXPORT double __cdecl _except1(unsigned fpe, int op, double arg, double result, ULONG_PTR cw)
{
    FPIEEE_RECORD record;
    if (!handle_exc((unsigned char)fpe, &result, (unsigned short)cw)) {
        memset(&record, 0, sizeof(record));
        raise_exc_ex(&record, &cw, (unsigned char)fpe, op, &arg, &result);
    }
    unsigned type = error_type((unsigned char)fpe);
    if (MsvcrtHasUserMatherr() && type)
        return umatherr(type, op, arg, 0.0, result, cw);
    set_errno_from_matherr(type);
#ifdef _WIN64
    ctrlfp(cw);
#else
    {
        unsigned short control = (unsigned short)cw;
        __asm__ volatile("fldcw %0" : : "m"(control));
    }
#endif
    return result;
}


typedef int(__cdecl *FPIEEE_HANDLER)(FPIEEE_RECORD *);

/*
 * lets a program's handler settle a floating point exception from an exception filter. a record that _except1
 * raised comes with the record in its first parameter and goes to the handler as it is. what the processor raised
 * has to be decoded from the faulting instruction, which is left to the next filter.
 */
DECLSPEC_EXPORT int __cdecl _fpieee_flt(NTSTATUS code, EXCEPTION_POINTERS *pointers, FPIEEE_HANDLER handler)
{
    if (code != STATUS_FLOAT_DIVIDE_BY_ZERO && code != STATUS_FLOAT_INEXACT_RESULT &&
        code != STATUS_FLOAT_INVALID_OPERATION && code != STATUS_FLOAT_OVERFLOW && code != STATUS_FLOAT_UNDERFLOW
#ifndef _WIN64
        && code != STATUS_FLOAT_MULTIPLE_FAULTS && code != STATUS_FLOAT_MULTIPLE_TRAPS
#endif
    )
        return 0;
#ifndef _WIN64
    __asm__ volatile("fninit");
#endif
    const EXCEPTION_RECORD *record = pointers->ExceptionRecord;
    if (code == STATUS_FLOAT_MULTIPLE_FAULTS || code == STATUS_FLOAT_MULTIPLE_TRAPS || !record->ExceptionInformation[0])
        return 0;
    int result = handler((FPIEEE_RECORD *)record->ExceptionInformation[0]);
#ifndef _WIN64
    if (result == -1)
        *(unsigned char *)&pointers->ContextRecord->FloatSave.StatusWord = 0;
#endif
    return result;
}

#endif
