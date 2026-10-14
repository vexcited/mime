/* exact integer accumulation prevents intermediate overflow and cancellation loss. */
#include "msvcrt_private.h"
#define LIMBS 132

typedef union { double value; uint64_t bits; } FLOAT_BITS;
static uint64_t unpack(double x, int *exponent, BOOL *negative)
{
    FLOAT_BITS b = {x};
    *negative = (BOOL)(b.bits >> 63);
    unsigned e = (unsigned)((b.bits >> 52) & 2047);
    *exponent = e ? (int)e - 1075 : -1074;
    return (b.bits & 0xfffffffffffffull) | (e ? 0x10000000000000ull : 0);
}
static void insert(uint32_t *out, const uint32_t *value, unsigned count, unsigned shift)
{
    unsigned word = shift / 32, bit = shift % 32;
    for (unsigned i = 0; i < count; i++) {
        out[word + i] |= value[i] << bit;
        if (bit) out[word + i + 1] |= value[i] >> (32 - bit);
    }
}
static unsigned get_bit(const uint32_t *value, unsigned bit)
{
    return (value[bit / 32] >> (bit % 32)) & 1;
}
static double fused(double x, double y, double z, unsigned precision, int minimum, int maximum)
{
    FLOAT_BITS bx = {x}, by = {y}, bz = {z};
    if (((bx.bits >> 52) & 2047) == 2047 || ((by.bits >> 52) & 2047) == 2047 ||
        ((bz.bits >> 52) & 2047) == 2047 || !x || !y)
        return x * y + z;
    BOOL sx, sy, sz;
    int ex, ey, ez;
    uint64_t mx = unpack(x, &ex, &sx), my = unpack(y, &ey, &sy), mz = unpack(z, &ez, &sz);
    uint32_t a[2] = {(uint32_t)mx, (uint32_t)(mx >> 32)}, b[2] = {(uint32_t)my, (uint32_t)(my >> 32)};
    uint32_t product[4] = {0}, addend[2] = {(uint32_t)mz, (uint32_t)(mz >> 32)};
    for (unsigned i = 0; i < 2; i++) {
        uint64_t carry = 0;
        for (unsigned j = 0; j < 2; j++) {
            uint64_t value = (uint64_t)a[i] * b[j] + product[i + j] + carry;
            product[i + j] = (uint32_t)value;
            carry = value >> 32;
        }
        product[i + 2] = (uint32_t)carry;
    }
    uint32_t p[LIMBS] = {0}, q[LIMBS] = {0};
    insert(p, product, 4, (unsigned)(ex + ey + 2148));
    insert(q, addend, 2, (unsigned)(ez + 2148));
    BOOL sign = sx != sy;
    if (sign == sz) {
        uint64_t carry = 0;
        for (unsigned i = 0; i < LIMBS; i++) {
            uint64_t value = (uint64_t)p[i] + q[i] + carry;
            p[i] = (uint32_t)value;
            carry = value >> 32;
        }
    } else {
        int compare = 0;
        for (int i = LIMBS - 1; i >= 0 && !compare; i--)
            if (p[i] != q[i]) compare = p[i] > q[i] ? 1 : -1;
        if (!compare) return 0;
        uint32_t *large = compare > 0 ? p : q, *small = compare > 0 ? q : p;
        uint64_t borrow = 0;
        for (unsigned i = 0; i < LIMBS; i++) {
            uint64_t sub = (uint64_t)small[i] + borrow;
            uint32_t word = large[i];
            p[i] = (uint32_t)((uint64_t)word - sub);
            borrow = (uint64_t)word < sub;
        }
        if (compare < 0) sign = sz;
    }
    int top = LIMBS * 32 - 1;
    while (top >= 0 && !get_bit(p, (unsigned)top)) top--;
    if (top < 0) return sign ? -0.0 : 0.0;
    int shift = top - (int)precision + 1;
    if (shift < minimum + 2148) shift = minimum + 2148;
    uint64_t mantissa = 0;
    for (int bit = top; bit >= shift; bit--) mantissa = (mantissa << 1) | get_bit(p, (unsigned)bit);
    BOOL sticky = FALSE;
    for (int bit = 0; bit < shift - 1 && !sticky; bit++) sticky = get_bit(p, (unsigned)bit) != 0;
    if (shift && get_bit(p, (unsigned)(shift - 1)) && (sticky || (mantissa & 1))) mantissa++;
    if (mantissa == (1ull << precision)) { mantissa >>= 1; shift++; }
    int exponent = shift - 2148;
    FLOAT_BITS result;
    if (exponent + (int)precision - 1 > maximum) result.bits = 0x7ff0000000000000ull;
    else if (!mantissa) result.bits = 0;
    else if (exponent == -1074 && mantissa < 0x10000000000000ull) result.bits = mantissa;
    else {
        while (mantissa < 0x10000000000000ull) { mantissa <<= 1; exponent--; }
        result.bits = ((uint64_t)(exponent + 1075) << 52) | (mantissa & 0xfffffffffffffull);
    }
    result.bits |= (uint64_t)sign << 63;
    return result.value;
}
DECLSPEC_EXPORT double __cdecl fma(double x, double y, double z) { return fused(x, y, z, 53, -1074, 1023); }
DECLSPEC_EXPORT float __cdecl fmaf(float x, float y, float z) { return (float)fused(x, y, z, 24, -149, 127); }
