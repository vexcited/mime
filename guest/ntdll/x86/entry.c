#include "../ntdll_private.h"

void NTAPI LdrpInitialize(PCONTEXT context);

DECLSPEC_NORETURN void NTAPI LdrInitializeThunk(PCONTEXT context, PVOID base)
{
    LdrpInitialize(context);
    NtContinue(context, TRUE);
    NtTerminateProcess(NtCurrentProcess(), STATUS_UNSUCCESSFUL);
    for (;;)
        ;
}

void NTAPI KiUserExceptionDispatcher(PEXCEPTION_RECORD record, PCONTEXT context)
{
    KiUserExceptionDispatcherC(record, context);
}

void NTAPI KiUserCallbackDispatcher(ULONG api, PVOID arguments, ULONG length)
{
    typedef NTSTATUS (CALLBACK *CALLBACK_FN)(PVOID, ULONG);
    CALLBACK_FN *table = NtCurrentPeb()->KernelCallbackTable;
    NTSTATUS status = table[api](arguments, length);
    NtCallbackReturn(nullptr, 0, status);
}
