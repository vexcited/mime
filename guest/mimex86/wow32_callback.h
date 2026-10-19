#ifndef MIME_WOW32_CALLBACK_H
#define MIME_WOW32_CALLBACK_H

#include <mime/ntuser.h>

typedef struct MX_CALLBACK32 {
    ULONG Api;
    PVOID Native;
    PVOID Guest;
    ULONG Length;
    PVOID External;  /* a block made for a large lparam, released with the result */
} MX_CALLBACK32;

/* copies the arguments below *stack, the 32-bit stack pointer, which moves past them. */
BOOLEAN Mx32MarshalCallback(ULONG *stack, ULONG api, PVOID arguments, ULONG length, MX_CALLBACK32 *callback);
PVOID Mx32CallbackResult(const MX_CALLBACK32 *callback, PVOID result, ULONG *length);

#endif
