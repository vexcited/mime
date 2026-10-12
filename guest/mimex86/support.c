/*
 * what the translator would otherwise take from libraries: 128 bit
 * division for the core and the allocation hooks of the shared runtime.
 */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>

#include "x86.h"

typedef unsigned __int128 u128;
typedef __int128 s128;

u128 __udivti3(u128 n, u128 d);
s128 __divti3(s128 n, s128 d);
u128 __umodti3(u128 n, u128 d);
s128 __modti3(s128 n, s128 d);

static u128 udivmod(u128 n, u128 d, u128 *rem)
{
    if (!(n >> 64) && !(d >> 64)) {
        uint64_t a = (uint64_t)n, b = (uint64_t)d;
        if (rem)
            *rem = a % b;
        return a / b;
    }
    u128 q = 0, r = 0;
    for (int bit = 127; bit >= 0; bit--) {
        r = (r << 1) | ((n >> bit) & 1);
        if (r >= d) {
            r -= d;
            q |= (u128)1 << bit;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

u128 __udivti3(u128 n, u128 d)
{
    return udivmod(n, d, NULL);
}

u128 __umodti3(u128 n, u128 d)
{
    u128 r;
    udivmod(n, d, &r);
    return r;
}

s128 __divti3(s128 n, s128 d)
{
    int negative = (n < 0) != (d < 0);
    u128 q = udivmod(n < 0 ? -(u128)n : (u128)n, d < 0 ? -(u128)d : (u128)d, NULL);
    return negative ? -(s128)q : (s128)q;
}

s128 __modti3(s128 n, s128 d)
{
    u128 r;
    udivmod(n < 0 ? -(u128)n : (u128)n, d < 0 ? -(u128)d : (u128)d, &r);
    return n < 0 ? -(s128)r : (s128)r;
}

/* the runtime's strings are rare here: whole pages are fine. */
PVOID MimeRtlAllocate(SIZE_T size)
{
    SIZE_T *p = x86_alloc(size + 16);
    p[0] = size + 16;
    return p + 2;
}

void MimeRtlFree(PVOID p)
{
    if (p) {
        SIZE_T *h = (SIZE_T *)p - 2;
        x86_free(h, h[0]);
    }
}
