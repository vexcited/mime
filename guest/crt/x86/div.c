#include <stdint.h>

uint64_t __stdcall MimeDivideUnsigned64(uint64_t numerator, uint64_t denominator)
{
    uint64_t quotient = 0, remainder = 0;
    if (!denominator) {
        __asm__ volatile("xorl %%ecx, %%ecx; divl %%ecx" ::: "eax", "edx", "ecx");
        return 0;
    }
    for (int bit = 63; bit >= 0; bit--) {
        uint64_t carry = remainder >> 63;
        remainder = (remainder << 1) | ((numerator >> bit) & 1);
        if (carry || remainder >= denominator) {
            remainder -= denominator;
            quotient |= (uint64_t)1 << bit;
        }
    }
    return quotient;
}

int64_t __stdcall MimeDivideSigned64(int64_t numerator, int64_t denominator)
{
    uint64_t a = numerator < 0 ? -(uint64_t)numerator : (uint64_t)numerator;
    uint64_t b = denominator < 0 ? -(uint64_t)denominator : (uint64_t)denominator;
    uint64_t result = MimeDivideUnsigned64(a, b);
    return (numerator < 0) != (denominator < 0) ? (int64_t)-result : (int64_t)result;
}

int64_t __stdcall MimeRemainderSigned64(int64_t numerator, int64_t denominator)
{
    return (int64_t)((uint64_t)numerator - (uint64_t)MimeDivideSigned64(numerator, denominator) * (uint64_t)denominator);
}

uint64_t __stdcall MimeRemainderUnsigned64(uint64_t numerator, uint64_t denominator)
{
    return numerator - MimeDivideUnsigned64(numerator, denominator) * denominator;
}
