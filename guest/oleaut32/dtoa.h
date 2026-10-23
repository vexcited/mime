/* exact conversions between doubles and decimal digits. */
#ifndef MIME_OLEAUT32_DTOA_H
#define MIME_OLEAUT32_DTOA_H

#ifdef AUT_STANDALONE
#include <stdint.h>
#include <string.h>
typedef uint32_t ULONG;
typedef uint8_t BYTE;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#else
#include "oleaut32_private.h"
#endif

/*
 * the significant digits of |x| rounded to at most ndigits, capped at 40, as '0' to '9' without trailing zeros:
 * x = 0.d1d2...dn * 10^*point. Returns n, or 0 for zero, infinities and NaNs. Ties round away from zero.
 */
int AutDoubleDigits(double x, int ndigits, char *digits, int *point);

/* the double nearest to 0.d1...dn * 10^point, digits as values 0 to 9. Out of range gives infinity or zero. */
double AutDigitsToDouble(const BYTE *digits, int count, int point);

#endif
