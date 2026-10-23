/*
 * converts 32-bit win32k calls through wow32_win32k.c and converts native window callbacks through wow32_callback.c
 * before dispatching them to 32-bit ntdll via wow64.dll.
 */
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/wow64.h>
#include "mime_svc_nt.h"
#include "mime_svc_win32k.h"
#include "wow32_callback.h"

#define WIN_EXPORT __declspec(dllexport)

ULONG Mx32Win32kCall(ULONG number, const ULONG *arguments);

extern UCHAR Wow64WinServicesNt[];
extern UCHAR Wow64WinServicesWin32k[];

typedef ULONG64 (*SVC16)(ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64,
                         ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64);

/* dispatches a service number selected at runtime. */
ULONG64 MxCallService(ULONG number, const ULONG64 *a)
{
    ULONG table = number >> 12, index = number & 0xFFF;
    UCHAR *stub;
    if (table == 0 && index < MIME_NT_SERVICES_COUNT)
        stub = Wow64WinServicesNt + 16 * index;
    else if (table == 1 && index < MIME_WIN32K_SERVICES_COUNT)
        stub = Wow64WinServicesWin32k + 16 * index;
    else
        return (ULONG64)(LONG64)STATUS_INVALID_SYSTEM_SERVICE;
    return ((SVC16)(void *)stub)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12],
                                 a[13], a[14], a[15]);
}

void Mx32Unsupported(ULONG number)
{
    static ULONG reported[256];
    if (number < 8192) {
        ULONG bit = 1u << (number & 31);
        if (__atomic_fetch_or(&reported[number / 32], bit, __ATOMIC_RELAXED) & bit)
            return;
    }
    DbgPrint("wow64win: unsupported 32-bit win32k service %04x\n", number);
}

WIN_EXPORT ULONG NTAPI Wow64WinSystemCall(ULONG number, const ULONG *arguments)
{
    return Mx32Win32kCall(number, arguments);
}

/*
 * converts callback arguments onto the suspended 32-bit stack, calls its dispatcher, and converts the result back to
 * the native layout.
 */
static NTSTATUS callback(ULONG api, PVOID arguments, ULONG length)
{
    ULONG stack = Wow64CpuArea()->Context.Esp - 0x100;
    MX_CALLBACK32 marshaled;
    if (!Mx32MarshalCallback(&stack, api, arguments, length, &marshaled))
        return NtCallbackReturn(nullptr, 0, STATUS_NOT_SUPPORTED);
    ULONG result = 0, result_length = 0;
    NTSTATUS status = Wow64KiUserCallbackDispatcher(api, (ULONG)(ULONG_PTR)marshaled.Guest, marshaled.Length,
                                                    &result, &result_length);
    PVOID out = result ? Mx32CallbackResult(&marshaled, (PVOID)(ULONG_PTR)result, &result_length) : nullptr;
    return NtCallbackReturn(out, out ? result_length : 0, status);
}

#define CALLBACK_THUNK(n)                                                                                              \
    static NTSTATUS CALLBACK callback##n(PVOID arguments, ULONG length)                                                \
    {                                                                                                                  \
        return callback(n, arguments, length);                                                                         \
    }
CALLBACK_THUNK(0)
CALLBACK_THUNK(1)
CALLBACK_THUNK(2)
CALLBACK_THUNK(3)
CALLBACK_THUNK(4)
CALLBACK_THUNK(5)
CALLBACK_THUNK(6)
CALLBACK_THUNK(7)

static PVOID g_callbacks[MIME_CB_COUNT] = {callback0, callback1, callback2, callback3,
                                          callback4, callback5, callback6, callback7};

/* the 64-bit ntdll's KiUserCallbackDispatcher calls the table of the 64-bit peb. */
WIN_EXPORT NTSTATUS NTAPI Wow64WinInitialize(void)
{
    NtCurrentPeb()->KernelCallbackTable = g_callbacks;
    return STATUS_SUCCESS;
}

BOOL WINAPI DllMain(PVOID instance, ULONG reason, PVOID reserved)
{
    return TRUE;
}
