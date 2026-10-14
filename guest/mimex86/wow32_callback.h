#ifndef MIME_WOW32_CALLBACK_H
#define MIME_WOW32_CALLBACK_H

#include <mime/ntuser.h>
#include "x86.h"

typedef struct MX_CALLBACK32 {
    ULONG Api;
    PVOID Native;
    PVOID Guest;
    ULONG Length;
} MX_CALLBACK32;

BOOLEAN Mx32MarshalCallback(X86Cpu *cpu, ULONG api, PVOID arguments, ULONG length, MX_CALLBACK32 *callback);
PVOID Mx32CallbackResult(const MX_CALLBACK32 *callback, PVOID result, ULONG *length);

#endif
