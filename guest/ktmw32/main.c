/*
 * ktmw32.dll: the kernel transaction manager. there is none here, so no transaction, resource manager or enlistment
 * can be made, and every handle given to these calls is not one. creating or opening says that transactions are not
 * supported, and the calls on a handle say that the handle is not valid.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

typedef ULONG_PTR ARG;

static BOOL bad_handle(void)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

static HANDLE not_supported(void)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

#define ON_HANDLE_1(name) BOOL WINAPI name(ARG a) { return bad_handle(); }
#define ON_HANDLE_2(name) BOOL WINAPI name(ARG a, ARG b) { return bad_handle(); }
#define ON_HANDLE_3(name) BOOL WINAPI name(ARG a, ARG b, ARG c) { return bad_handle(); }
#define ON_HANDLE_4(name) BOOL WINAPI name(ARG a, ARG b, ARG c, ARG d) { return bad_handle(); }
#define ON_HANDLE_5(name) BOOL WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e) { return bad_handle(); }
#define ON_HANDLE_7(name) BOOL WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g) { return bad_handle(); }
#define CREATE_3(name) HANDLE WINAPI name(ARG a, ARG b, ARG c) { return not_supported(); }
#define CREATE_4(name) HANDLE WINAPI name(ARG a, ARG b, ARG c, ARG d) { return not_supported(); }
#define CREATE_5(name) HANDLE WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e) { return not_supported(); }
#define CREATE_6(name) HANDLE WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f) { return not_supported(); }
#define CREATE_7(name) HANDLE WINAPI name(ARG a, ARG b, ARG c, ARG d, ARG e, ARG f, ARG g) { return not_supported(); }

/* a handle and the virtual clock of the transaction manager. */
ON_HANDLE_2(CommitComplete)
ON_HANDLE_2(CommitEnlistment)
ON_HANDLE_2(PrePrepareComplete)
ON_HANDLE_2(PrePrepareEnlistment)
ON_HANDLE_2(PrepareComplete)
ON_HANDLE_2(PrepareEnlistment)
ON_HANDLE_2(ReadOnlyEnlistment)
ON_HANDLE_2(RollbackComplete)
ON_HANDLE_2(RollbackEnlistment)
ON_HANDLE_2(RollforwardTransactionManager)
ON_HANDLE_2(SinglePhaseReject)
ON_HANDLE_2(GetCurrentClockTransactionManager)
ON_HANDLE_2(GetEnlistmentId)
ON_HANDLE_2(GetTransactionId)
ON_HANDLE_2(GetTransactionManagerId)
ON_HANDLE_2(RecoverEnlistment)
ON_HANDLE_2(RenameTransactionManager)

ON_HANDLE_1(CommitTransaction)
ON_HANDLE_1(CommitTransactionAsync)
ON_HANDLE_1(RollbackTransaction)
ON_HANDLE_1(RollbackTransactionAsync)
ON_HANDLE_1(RecoverResourceManager)
ON_HANDLE_1(RecoverTransactionManager)

ON_HANDLE_3(SetResourceManagerCompletionPort)
ON_HANDLE_3(SetEnlistmentRecoveryInformation)
ON_HANDLE_4(GetEnlistmentRecoveryInformation)
ON_HANDLE_5(GetNotificationResourceManagerAsync)
ON_HANDLE_5(GetNotificationResourceManager)
ON_HANDLE_5(SetTransactionInformation)
ON_HANDLE_7(GetTransactionInformation)

CREATE_3(OpenEnlistment)
CREATE_3(OpenResourceManager)
CREATE_3(OpenTransactionManager)
CREATE_3(OpenTransactionManagerById)
HANDLE WINAPI OpenTransaction(ARG a, ARG b)
{
    return not_supported();
}
CREATE_4(CreateTransactionManager)
CREATE_5(CreateResourceManager)
CREATE_6(CreateEnlistment)
CREATE_7(CreateTransaction)
