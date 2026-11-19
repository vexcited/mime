/*
 * the providers themselves. where advapi32 is microsoft's, its CryptoAPI calls this dll, as on windows, so the
 * providers live here. the 32-bit advapi32 is still mime's and keeps them, and this dll forwards there.
 */
#ifndef __i386__
#include "../advapi32/crypt.c"
#endif
