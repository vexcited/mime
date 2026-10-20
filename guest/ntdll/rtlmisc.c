/*
 * runtime functions of ntdll that belong to no larger part: a privilege taken for a while and given back, the
 * shape of the installation as the shared data tells it, and the filter of exceptions nothing handled.
 */
#include "ntdll_private.h"

#define STATUS_NOT_ALL_ASSIGNED_ ((NTSTATUS)0x00000106)
#define STATUS_PRIVILEGE_NOT_HELD_ ((NTSTATUS)0xC0000061)
#define TOKEN_QUERY_ 0x0008
#define TOKEN_ADJUST_PRIVILEGES_ 0x0020
#define TOKEN_IMPERSONATE_ 0x0004
#define SE_PRIVILEGE_ENABLED_ 2
#define ThreadImpersonationToken_ 5
#define STATUS_POSSIBLE_DEADLOCK ((NTSTATUS)0xC0000194)
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0

NTSYSAPI NTSTATUS NTAPI RtlImpersonateSelfEx(ULONG level, ACCESS_MASK additional, PHANDLE thread_token);

NTSYSCALLAPI NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE token, BOOLEAN disable_all, PVOID state, ULONG length,
                                                    PVOID previous, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenThreadTokenEx(HANDLE thread, ACCESS_MASK access, BOOLEAN self, ULONG attributes,
                                                PHANDLE token);

typedef struct {
    ULONG Count;
    struct {
        LUID Luid;
        ULONG Attributes;
    } Privileges[1];
} PRIVILEGES;

/* ------------------------------------------------------------------ one privilege */

/*
 * enables or disables a privilege of the token of the process, or of the thread when current_thread is 1, and
 * says whether it was enabled before. A privilege the token does not have is STATUS_PRIVILEGE_NOT_HELD.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlAdjustPrivilege(ULONG privilege, BOOLEAN enable, BOOLEAN current_thread,
                                               PBOOLEAN was_enabled)
{
    HANDLE token = nullptr;
    NTSTATUS status;
    if (current_thread == 1)
        status = NtOpenThreadToken(NtCurrentThread(), TOKEN_ADJUST_PRIVILEGES_ | TOKEN_QUERY_, FALSE, &token);
    else
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES_ | TOKEN_QUERY_, &token);
    if (!NT_SUCCESS(status))
        return status;
    PRIVILEGES wanted = {1, {{{privilege, 0}, enable ? SE_PRIVILEGE_ENABLED_ : 0}}};
    PRIVILEGES previous = {};
    ULONG returned = 0;
    status = NtAdjustPrivilegesToken(token, FALSE, &wanted, sizeof(previous), &previous, &returned);
    NtClose(token);
    if (status == STATUS_NOT_ALL_ASSIGNED_)
        return STATUS_PRIVILEGE_NOT_HELD_;
    if (NT_SUCCESS(status))
        *was_enabled = previous.Count ? (previous.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED_) != 0 : enable;
    return status;
}

/* ------------------------------------------------------------------ privileges for a while */

#define ACQUIRE_THREAD_TOKEN 1 /* the thread's token is the one adjusted, and was set up here */
#define ACQUIRE_PROCESS_TOKEN 2
#define ACQUIRE_FLAGS_MASK 3

typedef struct {
    HANDLE Token;          /* the token that was adjusted */
    HANDLE SavedToken;     /* the impersonation the thread had, to give back */
    ULONG Flags;
    PRIVILEGES *Previous;  /* what the token had, as it was */
    PRIVILEGES *Wanted;
    ULONG Capacity;        /* of Previous, in bytes */
    BOOLEAN PreviousOwned;
} PRIVILEGE_STATE;

/*
 * enables privileges and keeps what they were. With flag 2 the process token is adjusted, which every thread
 * sees; otherwise the thread impersonates itself first, so only it is changed. A thread that already
 * impersonates keeps its token when flag 1 is not given, and is reverted for the while when it is.
 * One privilege that is not held is STATUS_PRIVILEGE_NOT_HELD; several, a success with what could be had.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlAcquirePrivilege(PULONG privileges, ULONG count, ULONG flags, PVOID *state)
{
    if (flags & ~ACQUIRE_FLAGS_MASK)
        return STATUS_INVALID_PARAMETER;
    if (flags & ACQUIRE_PROCESS_TOKEN)
        flags |= ACQUIRE_THREAD_TOKEN;
    SIZE_T size = sizeof(PRIVILEGE_STATE) + 2 * (sizeof(ULONG) + (SIZE_T)count * 12) + 1024;
    PRIVILEGE_STATE *s = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
    if (!s)
        return STATUS_NO_MEMORY;
    s->Wanted = (PRIVILEGES *)(s + 1);
    s->Previous = (PRIVILEGES *)((PUCHAR)s->Wanted + sizeof(ULONG) + (SIZE_T)count * 12 + 4);
    s->Capacity = 1024 + (ULONG)(sizeof(ULONG) + (SIZE_T)count * 12);
    NTSTATUS status;
    HANDLE probe;
    if (NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY_, TRUE, &probe))) {
        NtClose(probe);
        if (flags & ACQUIRE_THREAD_TOKEN) {
            status = NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE_, TRUE, &s->SavedToken);
            if (NT_SUCCESS(status)) {
                s->Flags |= ACQUIRE_THREAD_TOKEN;
                HANDLE none = nullptr;
                NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken_, &none, sizeof(none));
            }
        } else {
            status = NtOpenThreadToken(NtCurrentThread(), TOKEN_ADJUST_PRIVILEGES_ | TOKEN_QUERY_, TRUE, &s->Token);
        }
        if (!NT_SUCCESS(status)) {
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s);
            return status;
        }
    }
    if (!s->Token) {
        if (flags & ACQUIRE_PROCESS_TOKEN) {
            status = NtOpenProcessTokenEx(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES_ | TOKEN_QUERY_, 0x200, &s->Token);
            if (NT_SUCCESS(status))
                s->Flags |= ACQUIRE_PROCESS_TOKEN;
        } else {
            status = RtlImpersonateSelfEx(3, TOKEN_ADJUST_PRIVILEGES_ | TOKEN_QUERY_, &s->Token);
            if (NT_SUCCESS(status))
                s->Flags |= ACQUIRE_THREAD_TOKEN;
        }
        if (!NT_SUCCESS(status))
            goto fail;
    }
    s->Wanted->Count = count;
    for (ULONG i = 0; i < count; i++) {
        s->Wanted->Privileges[i].Luid.LowPart = privileges[i];
        s->Wanted->Privileges[i].Luid.HighPart = 0;
        s->Wanted->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED_;
    }
    ULONG returned = s->Capacity;
    status = NtAdjustPrivilegesToken(s->Token, FALSE, s->Wanted, s->Capacity, s->Previous, &returned);
    if (status == STATUS_BUFFER_TOO_SMALL) {
        s->Previous = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, returned);
        if (!s->Previous) {
            status = STATUS_NO_MEMORY;
            goto fail;
        }
        s->PreviousOwned = TRUE;
        s->Capacity = returned;
        status = NtAdjustPrivilegesToken(s->Token, FALSE, s->Wanted, returned, s->Previous, &returned);
    }
    if (status == STATUS_NOT_ALL_ASSIGNED_ && count == 1)
        status = STATUS_PRIVILEGE_NOT_HELD_;
    if (status == STATUS_NOT_ALL_ASSIGNED_ || NT_SUCCESS(status)) {
        *state = s;
        return STATUS_SUCCESS;
    }
fail:
    if (s->PreviousOwned)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s->Previous);
    if (s->Token)
        NtClose(s->Token);
    if (s->Flags & ACQUIRE_THREAD_TOKEN) {
        NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken_, &s->SavedToken, sizeof(HANDLE));
        if (s->SavedToken)
            NtClose(s->SavedToken);
    }
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s);
    return status;
}

/* gives the privileges back as they were and puts the impersonation of the thread back. */
NTDLL_EXPORT void NTAPI RtlReleasePrivilege(PVOID state)
{
    PRIVILEGE_STATE *s = state;
    /* a thread that impersonated itself for this has nothing to restore, its token goes away. */
    if ((s->Flags & ACQUIRE_FLAGS_MASK) != ACQUIRE_THREAD_TOKEN)
        NtAdjustPrivilegesToken(s->Token, FALSE, s->Previous, 0, nullptr, nullptr);
    if (s->Flags & ACQUIRE_THREAD_TOKEN) {
        NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken_, &s->SavedToken, sizeof(HANDLE));
        if (s->SavedToken)
            NtClose(s->SavedToken);
    }
    if (s->PreviousOwned)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s->Previous);
    NtClose(s->Token);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, s);
}

/* ------------------------------------------------------------------ the installation */

NTDLL_EXPORT ULONG NTAPI RtlGetSuiteMask(void)
{
    return USER_SHARED_DATA->SuiteMask;
}

/* the session that services run in, which is 0 outside of a server silo. */
NTDLL_EXPORT ULONG NTAPI RtlGetCurrentServiceSessionId(void)
{
    return 0;
}

/* the session whose desktop is at the console. */
NTDLL_EXPORT ULONG NTAPI RtlGetActiveConsoleId(void)
{
    return USER_SHARED_DATA->ActiveConsoleId;
}

/* the second byte of the shared data flags: bit 0 of it tells a sku that has several interactive sessions. */
NTDLL_EXPORT BOOLEAN NTAPI RtlIsMultiSessionSku(void)
{
    return (USER_SHARED_DATA->SharedDataFlags >> 8) & 1;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlIsMultiUsersInSessionSku(void)
{
    return (USER_SHARED_DATA->SharedDataFlags & 0x200) != 0;
}

/* ------------------------------------------------------------------ diagnostics */

NTDLL_EXPORT void NTAPI RtlLogUnexpectedCodepath(void)
{
}

static BOOLEAN debugger_present(void)
{
    return NtCurrentPeb()->BeingDebugged || USER_SHARED_DATA->KdDebuggerEnabled;
}

/*
 * what the last chance filter of a process does with an exception nothing handled: with a debugger attached
 * it describes the fault on the debug output and stops there. The answer is EXCEPTION_CONTINUE_SEARCH, but for
 * a possible deadlock of a critical section, which continues the execution.
 */
NTDLL_EXPORT LONG NTAPI RtlUnhandledExceptionFilter2(PEXCEPTION_POINTERS pointers, ULONG flags)
{
    PEXCEPTION_RECORD record = pointers->ExceptionRecord;
    PRTL_USER_PROCESS_PARAMETERS parameters = NtCurrentPeb()->ProcessParameters;
    PCWSTR program = parameters ? parameters->CommandLine.Buffer : L"<unknown>";
    if (debugger_present()) {
        switch (record->ExceptionCode) {
        case STATUS_ACCESS_VIOLATION:
            DbgPrintEx(101, 0, "\n\n *** An Access Violation occurred in %ws:%s\n\n", program, "");
            DbgPrintEx(101, 0, "The instruction at %p tried to %s ", record->ExceptionAddress,
                       record->ExceptionInformation[0] ? "write to" : "read from");
            if (record->ExceptionInformation[1])
                DbgPrintEx(101, 0, "an invalid address, %p\n\n", (PVOID)record->ExceptionInformation[1]);
            else
                DbgPrintEx(101, 0, "a NULL pointer\n\n");
            break;
        case STATUS_IN_PAGE_ERROR:
            DbgPrintEx(101, 0, "\n\n *** Inpage error in %ws:%s\n\n", program, "");
            DbgPrintEx(101, 0, "The instruction at %p referenced memory at %p.\n", record->ExceptionAddress,
                       (PVOID)record->ExceptionInformation[1]);
            DbgPrintEx(101, 0, "This failed because of error %Ix.\n\n", (ULONG_PTR)record->ExceptionInformation[2]);
            break;
        case STATUS_STACK_BUFFER_OVERRUN:
            DbgPrintEx(101, 0, "\n\n *** A stack buffer overrun occurred in %ws:%s\n\n", program, "");
            DbgPrintEx(101, 0, "This is usually the result of a memory copy to a local buffer or structure where the size "
                               "is not properly calculated/checked.\n");
            break;
        default:
            DbgPrintEx(101, 0, "\n\n *** Unhandled exception 0x%08lx, hit in %ws:%s\n\n", record->ExceptionCode, program, "");
            break;
        }
        DbgPrintEx(101, 0, " *** enter .exr %p for the exception record\n", record);
        if (record->ExceptionCode != STATUS_STACK_BUFFER_OVERRUN)
            DbgPrintEx(101, 0, " ***  enter .cxr %p for the context\n", pointers->ContextRecord);
        DbgPrintEx(101, 0, " *** then kb to get the faulting stack\n\n");
        DbgBreakPoint();
    }
    return record->ExceptionCode == STATUS_POSSIBLE_DEADLOCK ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

NTDLL_EXPORT LONG NTAPI RtlUnhandledExceptionFilter(PEXCEPTION_POINTERS pointers)
{
    return RtlUnhandledExceptionFilter2(pointers, 0);
}

/* ------------------------------------------------------------------ the transaction of a thread */

/*
 * the transaction handle that the file functions of the thread use, kept in the thread's block. -1 is refused, any
 * other value is stored without a check and the answer is TRUE, as ntdll build 26300 does. Mime has no transaction
 * manager, so nothing reads the handle but the transacted functions of kernel32.
 */
NTDLL_EXPORT PVOID NTAPI RtlGetCurrentTransaction(void)
{
    return NtCurrentTeb()->CurrentTransactionHandle;
}

NTDLL_EXPORT LOGICAL NTAPI RtlSetCurrentTransaction(PVOID transaction)
{
    if (transaction == (PVOID)(LONG_PTR)-1)
        return FALSE;
    NtCurrentTeb()->CurrentTransactionHandle = transaction;
    return TRUE;
}

/* a processor that runs arm64 code in an x64 process is never this one. */
NTDLL_EXPORT BOOLEAN NTAPI RtlIsEcCode(ULONG_PTR address)
{
    return FALSE;
}
