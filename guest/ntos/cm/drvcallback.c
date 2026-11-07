#include "../ntos.h"

/*
 * registry callbacks drivers register. the registry does not call them yet: a registration is kept so that the
 * driver's cookie stays valid until it unregisters.
 */

typedef NTSTATUS(NTAPI *EX_CALLBACK_FUNCTION)(PVOID context, PVOID argument1, PVOID argument2);

typedef struct CM_CALLBACK {
    struct CM_CALLBACK *Next;
    EX_CALLBACK_FUNCTION Function;
    PVOID Context;
    LARGE_INTEGER Cookie;
} CM_CALLBACK;

static CM_CALLBACK *g_callbacks;
static KSPIN_LOCK g_lock;
static LONG64 g_next_cookie = 0x1000;

NTSTATUS NTAPI CmRegisterCallbackEx(EX_CALLBACK_FUNCTION function, PCUNICODE_STRING altitude, PVOID driver,
                                    PVOID context, PLARGE_INTEGER cookie, PVOID reserved)
{
    if (!function || !cookie)
        return STATUS_INVALID_PARAMETER;
    CM_CALLBACK *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('C', 'm', 'C', 'b'));
    if (!c)
        return STATUS_INSUFFICIENT_RESOURCES;
    c->Function = function;
    c->Context = context;
    c->Cookie.QuadPart = __atomic_add_fetch(&g_next_cookie, 1, __ATOMIC_RELAXED);
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    c->Next = g_callbacks;
    g_callbacks = c;
    KeReleaseSpinLock(&g_lock, irql);
    *cookie = c->Cookie;
    KiLogPrintf("registry callback %p registered, not called by the registry yet", (PVOID)function);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI CmRegisterCallback(EX_CALLBACK_FUNCTION function, PVOID context, PLARGE_INTEGER cookie)
{
    return CmRegisterCallbackEx(function, nullptr, nullptr, context, cookie, nullptr);
}

NTSTATUS NTAPI CmUnRegisterCallback(LARGE_INTEGER cookie)
{
    CM_CALLBACK *found = nullptr;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (CM_CALLBACK **at = &g_callbacks; *at; at = &(*at)->Next) {
        if ((*at)->Cookie.QuadPart == cookie.QuadPart) {
            found = *at;
            *at = found->Next;
            break;
        }
    }
    KeReleaseSpinLock(&g_lock, irql);
    if (!found)
        return STATUS_INVALID_PARAMETER;
    ExFreePool(found);
    return STATUS_SUCCESS;
}
