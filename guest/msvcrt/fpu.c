/*
 * the floating point environment: msvcrt's _control87 family over the
 * sse control register on x64 and fpcr and fpsr on arm64.
 */
#include "msvcrt_private.h"

#define _EM_INEXACT 0x00000001
#define _EM_UNDERFLOW 0x00000002
#define _EM_OVERFLOW 0x00000004
#define _EM_ZERODIVIDE 0x00000008
#define _EM_INVALID 0x00000010
#define _EM_DENORMAL 0x00080000
#define _MCW_EM 0x0008001f
#define _MCW_RC 0x00000300
#define _RC_NEAR 0x00000000
#define _RC_DOWN 0x00000100
#define _RC_UP 0x00000200
#define _RC_CHOP 0x00000300
#define _MCW_PC 0x00030000
#define _PC_53 0x00010000
#define _MCW_IC 0x00040000
#define _MCW_DN 0x03000000
#define _DN_FLUSH 0x01000000
#define _CW_DEFAULT (_RC_NEAR | _PC_53 | _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _EM_DENORMAL)

#define _SW_INEXACT 0x00000001
#define _SW_UNDERFLOW 0x00000002
#define _SW_OVERFLOW 0x00000004
#define _SW_ZERODIVIDE 0x00000008
#define _SW_INVALID 0x00000010
#define _SW_DENORMAL 0x00080000

#if defined(__x86_64__)

static unsigned get_mxcsr(void)
{
    unsigned v;
    __asm__ __volatile__("stmxcsr %0" : "=m"(v));
    return v;
}

static void set_mxcsr(unsigned v)
{
    __asm__ __volatile__("ldmxcsr %0" : : "m"(v));
}

/* msvcrt's control word from the sse control register. */
static unsigned read_control(void)
{
    unsigned m = get_mxcsr(), cw = _PC_53;
    if (m & 0x0080)
        cw |= _EM_INVALID;
    if (m & 0x0100)
        cw |= _EM_DENORMAL;
    if (m & 0x0200)
        cw |= _EM_ZERODIVIDE;
    if (m & 0x0400)
        cw |= _EM_OVERFLOW;
    if (m & 0x0800)
        cw |= _EM_UNDERFLOW;
    if (m & 0x1000)
        cw |= _EM_INEXACT;
    switch (m & 0x6000) {
    case 0x2000:
        cw |= _RC_DOWN;
        break;
    case 0x4000:
        cw |= _RC_UP;
        break;
    case 0x6000:
        cw |= _RC_CHOP;
        break;
    }
    if (m & 0x8040)
        cw |= _DN_FLUSH;
    return cw;
}

static void write_control(unsigned cw)
{
    unsigned m = get_mxcsr() & ~0xffc0u;
    if (cw & _EM_INVALID)
        m |= 0x0080;
    if (cw & _EM_DENORMAL)
        m |= 0x0100;
    if (cw & _EM_ZERODIVIDE)
        m |= 0x0200;
    if (cw & _EM_OVERFLOW)
        m |= 0x0400;
    if (cw & _EM_UNDERFLOW)
        m |= 0x0800;
    if (cw & _EM_INEXACT)
        m |= 0x1000;
    switch (cw & _MCW_RC) {
    case _RC_DOWN:
        m |= 0x2000;
        break;
    case _RC_UP:
        m |= 0x4000;
        break;
    case _RC_CHOP:
        m |= 0x6000;
        break;
    }
    if (cw & _DN_FLUSH)
        m |= 0x8040;
    set_mxcsr(m);
}

static unsigned read_status(void)
{
    unsigned m = get_mxcsr(), sw = 0;
    if (m & 0x01)
        sw |= _SW_INVALID;
    if (m & 0x02)
        sw |= _SW_DENORMAL;
    if (m & 0x04)
        sw |= _SW_ZERODIVIDE;
    if (m & 0x08)
        sw |= _SW_OVERFLOW;
    if (m & 0x10)
        sw |= _SW_UNDERFLOW;
    if (m & 0x20)
        sw |= _SW_INEXACT;
    return sw;
}

static void clear_status(void)
{
    set_mxcsr(get_mxcsr() & ~0x3fu);
    __asm__ __volatile__("fnclex");
}

#else

static uint64_t get_fpcr(void)
{
    uint64_t v;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
    return v;
}

static void set_fpcr(uint64_t v)
{
    __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
}

static uint64_t get_fpsr(void)
{
    uint64_t v;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(v));
    return v;
}

static void set_fpsr(uint64_t v)
{
    __asm__ __volatile__("msr fpsr, %0" : : "r"(v));
}

/* arm64 enables traps with bits that mean unmasked, the opposite of msvcrt's masks. */
static unsigned read_control(void)
{
    uint64_t f = get_fpcr();
    unsigned cw = _PC_53 | _MCW_EM;
    if (f & (1 << 8))
        cw &= ~_EM_INVALID;
    if (f & (1 << 9))
        cw &= ~_EM_ZERODIVIDE;
    if (f & (1 << 10))
        cw &= ~_EM_OVERFLOW;
    if (f & (1 << 11))
        cw &= ~_EM_UNDERFLOW;
    if (f & (1 << 12))
        cw &= ~_EM_INEXACT;
    if (f & (1 << 15))
        cw &= ~_EM_DENORMAL;
    switch ((f >> 22) & 3) {
    case 1:
        cw |= _RC_UP;
        break;
    case 2:
        cw |= _RC_DOWN;
        break;
    case 3:
        cw |= _RC_CHOP;
        break;
    }
    if (f & (1 << 24))
        cw |= _DN_FLUSH;
    return cw;
}

static void write_control(unsigned cw)
{
    uint64_t f = get_fpcr() & ~((uint64_t)0x9f00 | (3ull << 22) | (1ull << 24));
    if (!(cw & _EM_INVALID))
        f |= 1 << 8;
    if (!(cw & _EM_ZERODIVIDE))
        f |= 1 << 9;
    if (!(cw & _EM_OVERFLOW))
        f |= 1 << 10;
    if (!(cw & _EM_UNDERFLOW))
        f |= 1 << 11;
    if (!(cw & _EM_INEXACT))
        f |= 1 << 12;
    if (!(cw & _EM_DENORMAL))
        f |= 1 << 15;
    switch (cw & _MCW_RC) {
    case _RC_UP:
        f |= 1ull << 22;
        break;
    case _RC_DOWN:
        f |= 2ull << 22;
        break;
    case _RC_CHOP:
        f |= 3ull << 22;
        break;
    }
    if (cw & _DN_FLUSH)
        f |= 1ull << 24;
    set_fpcr(f);
}

static unsigned read_status(void)
{
    uint64_t s = get_fpsr();
    unsigned sw = 0;
    if (s & 0x01)
        sw |= _SW_INVALID;
    if (s & 0x02)
        sw |= _SW_ZERODIVIDE;
    if (s & 0x04)
        sw |= _SW_OVERFLOW;
    if (s & 0x08)
        sw |= _SW_UNDERFLOW;
    if (s & 0x10)
        sw |= _SW_INEXACT;
    if (s & 0x80)
        sw |= _SW_DENORMAL;
    return sw;
}

static void clear_status(void)
{
    set_fpsr(get_fpsr() & ~0x9full);
}

#endif

unsigned int __cdecl _control87(unsigned int value, unsigned int mask)
{
    unsigned cw = read_control();
    if (mask) {
        cw = (cw & ~mask) | (value & mask);
        write_control(cw);
        cw = read_control();
    }
    return cw;
}

/* _controlfp ignores the denormal exception mask, like msvcrt on 64 bit processors. */
unsigned int __cdecl _controlfp(unsigned int value, unsigned int mask)
{
    return _control87(value, mask & ~(_EM_DENORMAL | _MCW_PC | _MCW_IC));
}

errno_t __cdecl _controlfp_s(unsigned int *current, unsigned int value, unsigned int mask)
{
    if (mask & ~(_MCW_EM | _MCW_RC | _MCW_DN)) {
        if (current)
            *current = _controlfp(0, 0);
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    unsigned r = _controlfp(value, mask);
    if (current)
        *current = r;
    return 0;
}

void __cdecl _set_controlfp(unsigned int value, unsigned int mask)
{
    _controlfp(value, mask);
}

unsigned int __cdecl _status87(void)
{
    return read_status();
}

unsigned int __cdecl _statusfp(void)
{
    return read_status();
}

unsigned int __cdecl _clear87(void)
{
    unsigned sw = read_status();
    clear_status();
    return sw;
}

unsigned int __cdecl _clearfp(void)
{
    return _clear87();
}

void __cdecl _fpreset(void)
{
#if defined(__x86_64__)
    __asm__ __volatile__("fninit");
#endif
    clear_status();
    write_control(_CW_DEFAULT);
}
