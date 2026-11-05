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

typedef struct CALLBACK_REGISTRATION {
    struct CALLBACK_REGISTRATION *Next;
    PVOID Handler;
} CALLBACK_REGISTRATION;

/*
 * like windows, what the callback leaves is fatal by default. with the process flag, the callback is unwound and
 * returns the code to the kernel, and an unwind that crosses it ends the callback there.
 */
static ULONG __cdecl KiUserCallbackExceptionHandler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                    PVOID dispatcher)
{
    NTSTATUS code = record->ExceptionCode;
    if (!LdrpUserCallbackExceptions()) {
        /* the registrations of the dispatch below the faulting code are not the filter's. */
        CALLBACK_REGISTRATION **list = (CALLBACK_REGISTRATION **)&NtCurrentTeb()->NtTib.ExceptionList;
        while ((ULONG_PTR)*list < context->Esp)
            *list = (*list)->Next;
        LdrpLogFatalUserCallbackException(record, context);
        return 0; /* ExceptionContinueExecution. */
    }
    if (!(record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)))
        RtlUnwind(frame, nullptr, record, (PVOID)(ULONG_PTR)code);
    RtlRaiseStatus(NtCallbackReturn(nullptr, 0, code));
}

void NTAPI KiUserCallbackDispatcher(ULONG api, PVOID arguments, ULONG length)
{
    typedef NTSTATUS (CALLBACK *CALLBACK_FN)(PVOID, ULONG);
    CALLBACK_REGISTRATION registration = {(CALLBACK_REGISTRATION *)NtCurrentTeb()->NtTib.ExceptionList,
                                          (PVOID)KiUserCallbackExceptionHandler};
    NtCurrentTeb()->NtTib.ExceptionList = (PVOID)&registration;
    CALLBACK_FN *table = NtCurrentPeb()->KernelCallbackTable;
    NTSTATUS status = table[api](arguments, length);
    RtlRaiseStatus(NtCallbackReturn(nullptr, 0, status));
}
