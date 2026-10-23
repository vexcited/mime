/*
 * small ntdll runtime functions: temporary privileges, installation info from the shared data, and the
 * unhandled exception filter.
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

/* ------------------------------------------------------------------ single privilege */

/* adjusts the thread token when current_thread is 1, else the process token. */
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

/* ------------------------------------------------------------------ temporary privileges */

#define ACQUIRE_THREAD_TOKEN 1 /* the thread token is adjusted and was set up here. */
#define ACQUIRE_PROCESS_TOKEN 2
#define ACQUIRE_FLAGS_MASK 3

typedef struct {
    HANDLE Token;
    HANDLE SavedToken;     /* the thread's previous impersonation, restored on release. */
    ULONG Flags;
    PRIVILEGES *Previous;  /* the token's privileges before the change. */
    PRIVILEGES *Wanted;
    ULONG Capacity;        /* of Previous, in bytes. */
    BOOLEAN PreviousOwned;
} PRIVILEGE_STATE;

/*
 * flag 2 adjusts the process token, seen by every thread. otherwise the thread impersonates itself first so only
 * it changes. a thread that already impersonates keeps its token without flag 1, and is reverted until release
 * with it. a single missing privilege is STATUS_PRIVILEGE_NOT_HELD, with several it succeeds with what it got.
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

NTDLL_EXPORT void NTAPI RtlReleasePrivilege(PVOID state)
{
    PRIVILEGE_STATE *s = state;
    /* a self impersonation token is simply dropped, nothing to restore on it. */
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

/* ------------------------------------------------------------------ installation */

NTDLL_EXPORT ULONG NTAPI RtlGetSuiteMask(void)
{
    return USER_SHARED_DATA->SuiteMask;
}

/* always 0 outside a server silo. */
NTDLL_EXPORT ULONG NTAPI RtlGetCurrentServiceSessionId(void)
{
    return 0;
}

NTDLL_EXPORT ULONG NTAPI RtlGetActiveConsoleId(void)
{
    return USER_SHARED_DATA->ActiveConsoleId;
}

/* bit 8 of the shared data flags marks a sku with several interactive sessions. */
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

/* with a debugger attached, prints the fault to the debug output and breaks. a possible critical section deadlock
   continues execution, anything else continues the search. */
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

/* ------------------------------------------------------------------ thread transaction */

/* the file functions' transaction handle lives in the teb. like windows, set refuses -1 and stores anything else
   unchecked. Mime has no transaction manager, only kernel32's transacted functions read it. */
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

/* this is never arm64ec code. */
NTDLL_EXPORT BOOLEAN NTAPI RtlIsEcCode(ULONG_PTR address)
{
    return FALSE;
}
