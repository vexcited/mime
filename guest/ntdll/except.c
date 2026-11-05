/*
 * exception raising and first chance dispatch.
 *
 * dispatch order: vectored handlers, frame based handlers, the top level
 * filter, then back to the kernel as second chance. the frame based
 * handlers are in x64/unwind.c, x86/seh.c and arm64/unwind.c.
 */
#include "ntdll_private.h"

#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0

typedef struct VECTORED_HANDLER {
    LIST_ENTRY Links;
    PVECTORED_EXCEPTION_HANDLER Handler;
    ULONG Refs;
} VECTORED_HANDLER;

static LIST_ENTRY g_vectored = {&g_vectored, &g_vectored};
static LIST_ENTRY g_continue = {&g_continue, &g_continue};
static RTL_SRWLOCK g_vectored_lock;
static LONG(NTAPI *g_unhandled_filter)(PEXCEPTION_POINTERS);

static PVOID add_handler(PLIST_ENTRY list, ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
    VECTORED_HANDLER *h = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*h));
    if (!h)
        return nullptr;
    h->Handler = handler;
    h->Refs = 1;
    RtlAcquireSRWLockExclusive(&g_vectored_lock);
    if (first)
        InsertHeadList(list, &h->Links);
    else
        InsertTailList(list, &h->Links);
    RtlReleaseSRWLockExclusive(&g_vectored_lock);
    return h;
}

static ULONG remove_handler(PLIST_ENTRY list, PVOID handle)
{
    ULONG found = 0;
    RtlAcquireSRWLockExclusive(&g_vectored_lock);
    for (PLIST_ENTRY e = list->Flink; e != list; e = e->Flink) {
        if (e == handle) {
            RemoveEntryList(e);
            found = 1;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_vectored_lock);
    if (found)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, handle);
    return found;
}

NTDLL_EXPORT PVOID NTAPI RtlAddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
    return add_handler(&g_vectored, first, handler);
}

NTDLL_EXPORT ULONG NTAPI RtlRemoveVectoredExceptionHandler(PVOID handle)
{
    return remove_handler(&g_vectored, handle);
}

NTDLL_EXPORT PVOID NTAPI RtlAddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
    return add_handler(&g_continue, first, handler);
}

NTDLL_EXPORT ULONG NTAPI RtlRemoveVectoredContinueHandler(PVOID handle)
{
    return remove_handler(&g_continue, handle);
}

/* backs kernel32's SetUnhandledExceptionFilter. */
NTDLL_EXPORT void NTAPI RtlSetUnhandledExceptionFilter(PVOID filter)
{
    g_unhandled_filter = filter;
}

static LONG call_handlers(PLIST_ENTRY list, PEXCEPTION_RECORD record, PCONTEXT context)
{
    EXCEPTION_POINTERS info = {record, context};
    LONG result = EXCEPTION_CONTINUE_SEARCH;
    RtlAcquireSRWLockShared(&g_vectored_lock);
    for (PLIST_ENTRY e = list->Flink; e != list; e = e->Flink) {
        VECTORED_HANDLER *h = CONTAINING_RECORD(e, VECTORED_HANDLER, Links);
        RtlReleaseSRWLockShared(&g_vectored_lock);
        result = h->Handler(&info);
        RtlAcquireSRWLockShared(&g_vectored_lock);
        if (result == EXCEPTION_CONTINUE_EXECUTION)
            break;
    }
    RtlReleaseSRWLockShared(&g_vectored_lock);
    return result;
}

BOOLEAN NTAPI RtlDispatchFrameException(PEXCEPTION_RECORD record, PCONTEXT context);

NTDLL_EXPORT BOOLEAN NTAPI RtlDispatchException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    if (call_handlers(&g_vectored, record, context) == EXCEPTION_CONTINUE_EXECUTION)
        return TRUE;
    if (RtlDispatchFrameException(record, context))
        return TRUE;
    if (g_unhandled_filter) {
        EXCEPTION_POINTERS info = {record, context};
        if (g_unhandled_filter(&info) == EXCEPTION_CONTINUE_EXECUTION)
            return TRUE;
    }
    return FALSE;
}

BOOLEAN LdrpUserCallbackExceptions(void)
{
    PRTL_USER_PROCESS_PARAMETERS parameters = NtCurrentPeb()->ProcessParameters;
    return parameters && (parameters->Flags & RTL_USER_PROC_CALLBACK_EXCEPTIONS);
}

/*
 * what a kernel callback leaves unhandled goes to the unhandled exception filter, and unless that continues the
 * execution, ends the process with STATUS_FATAL_USER_CALLBACK_EXCEPTION, as windows does.
 */
LONG NTAPI LdrpLogFatalUserCallbackException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    EXCEPTION_POINTERS info = {record, context};
    LONG result = g_unhandled_filter ? g_unhandled_filter(&info) : RtlUnhandledExceptionFilter2(&info, 0);
    if (result == EXCEPTION_CONTINUE_EXECUTION)
        return result;
    EXCEPTION_RECORD fatal = {};
    fatal.ExceptionCode = STATUS_FATAL_USER_CALLBACK_EXCEPTION;
    fatal.ExceptionFlags = record->ExceptionFlags | EXCEPTION_NONCONTINUABLE;
    fatal.ExceptionRecord = record;
#if defined(__x86_64__)
    fatal.ExceptionAddress = (PVOID)context->Rip;
#elif defined(__i386__)
    fatal.ExceptionAddress = (PVOID)context->Eip;
#else
    fatal.ExceptionAddress = (PVOID)context->Pc;
#endif
    RtlRaiseStatus(NtRaiseException(&fatal, context, FALSE));
}

void NTAPI KiUserExceptionDispatcherC(PEXCEPTION_RECORD record, PCONTEXT context)
{
    NTSTATUS status;
    /* wow64 sees the exceptions of 32-bit code first, see ldr.c. */
    if (LdrpWow64PrepareForException)
        LdrpWow64PrepareForException(record, context);
    if (RtlDispatchException(record, context)) {
        call_handlers(&g_continue, record, context);
        status = NtContinue(context, FALSE);
    } else {
        status = NtRaiseException(record, context, FALSE);
    }
    /* both calls only return on failure. */
    EXCEPTION_RECORD nested = {};
    nested.ExceptionCode = status;
    nested.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    nested.ExceptionRecord = record;
    RtlRaiseException(&nested);
}

NTDLL_EXPORT DECLSPEC_NORETURN void NTAPI RtlRaiseStatus(NTSTATUS status)
{
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = status;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    RtlRaiseException(&record);
    NtTerminateProcess(NtCurrentProcess(), status);
    for (;;)
        ;
}
