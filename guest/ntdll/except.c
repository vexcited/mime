/*
 * exception raising and first chance dispatch.
 *
 * dispatch order: vectored exception handlers, then frame based handlers,
 * then vectored continue handlers. frame based dispatch needs the arm64
 * unwinder, which is not written yet, so unhandled exceptions go straight
 * to the top level filter and then back to the kernel as second chance.
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
        return NULL;
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

/* used by kernel32!SetUnhandledExceptionFilter through RtlSetUnhandledExceptionFilter. */
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

NTDLL_EXPORT BOOLEAN NTAPI RtlDispatchException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    if (call_handlers(&g_vectored, record, context) == EXCEPTION_CONTINUE_EXECUTION)
        return TRUE;
    if (g_unhandled_filter) {
        EXCEPTION_POINTERS info = {record, context};
        if (g_unhandled_filter(&info) == EXCEPTION_CONTINUE_EXECUTION)
            return TRUE;
    }
    return FALSE;
}

void NTAPI KiUserExceptionDispatcherC(PEXCEPTION_RECORD record, PCONTEXT context)
{
    NTSTATUS status;
    if (RtlDispatchException(record, context)) {
        call_handlers(&g_continue, record, context);
        status = NtContinue(context, FALSE);
    } else {
        status = NtRaiseException(record, context, FALSE);
    }
    /* both calls only return on failure. */
    EXCEPTION_RECORD nested = {0};
    nested.ExceptionCode = status;
    nested.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    nested.ExceptionRecord = record;
    RtlRaiseException(&nested);
}

NTDLL_EXPORT void NTAPI RtlRaiseException(PEXCEPTION_RECORD record)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    /* resume, if ever, right after the call to this function. */
    context.Pc = (ULONG64)__builtin_return_address(0);
    context.Sp = (ULONG64)__builtin_frame_address(0) + 16;
    record->ExceptionAddress = (PVOID)context.Pc;
    NTSTATUS status = NtRaiseException(record, &context, TRUE);
    RtlRaiseStatus(status);
}

NTDLL_EXPORT DECLSPEC_NORETURN void NTAPI RtlRaiseStatus(NTSTATUS status)
{
    EXCEPTION_RECORD record = {0};
    record.ExceptionCode = status;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    RtlRaiseException(&record);
    NtTerminateProcess(NtCurrentProcess(), status);
    for (;;)
        ;
}

NTDLL_EXPORT void NTAPI RtlUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value)
{
    DbgPrint("RtlUnwind: frame based unwinding is not supported yet\n");
    NtTerminateProcess(NtCurrentProcess(), STATUS_NOT_IMPLEMENTED);
}

NTDLL_EXPORT void NTAPI RtlUnwindEx(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value,
                                    PCONTEXT context, PVOID history)
{
    RtlUnwind(target_frame, target_ip, record, value);
}

NTDLL_EXPORT PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history)
{
    PLDR_DATA_TABLE_ENTRY entry;
    if (!NT_SUCCESS(LdrFindEntryForAddress((PVOID)pc, &entry)))
        return NULL;
    ULONG size;
    IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY *table = LdrpImageDirectory(entry->DllBase, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &size);
    if (!table)
        return NULL;
    ULONG rva = (ULONG)(pc - (ULONG64)entry->DllBase);
    ULONG lo = 0, hi = size / sizeof(*table);
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        ULONG begin = table[mid].BeginAddress;
        ULONG length = (table[mid].UnwindData & 3) ? ((table[mid].UnwindData >> 2) & 0x7FF) * 4 : 0;
        if (rva < begin) {
            hi = mid;
        } else if (length && rva >= begin + length) {
            lo = mid + 1;
        } else if (!length && mid + 1 < size / sizeof(*table) && rva >= table[mid + 1].BeginAddress) {
            lo = mid + 1;
        } else {
            *image_base = (ULONG64)entry->DllBase;
            return &table[mid];
        }
    }
    return NULL;
}
