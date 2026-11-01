/*
 * application model beyond package names: package info, app state notifications and memory resource notifications.
 * no package is installed and the system manages no app state, so a package is never found.
 */
#include "kernelbase_private.h"

LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *length, BYTE *buffer);

/* a well formed full name is simply not installed, and no reference is ever returned. */
DECLSPEC_EXPORT LONG WINAPI OpenPackageInfoByFullName(PCWSTR full, UINT32 reserved, PVOID *reference)
{
    UINT32 length = 0;
    if (!full || reserved || !reference)
        return ERROR_INVALID_PARAMETER;
    *reference = nullptr;
    LONG error = PackageIdFromFullName(full, 0, &length, nullptr);
    return error == ERROR_INSUFFICIENT_BUFFER ? ERROR_NOT_FOUND : error;
}

DECLSPEC_EXPORT LONG WINAPI ClosePackageInfo(PVOID reference)
{
    return ERROR_INVALID_PARAMETER;
}

DECLSPEC_EXPORT LONG WINAPI GetPackageApplicationIds(PVOID reference, UINT32 *length, BYTE *buffer, UINT32 *count)
{
    return ERROR_INVALID_PARAMETER;
}

DECLSPEC_EXPORT LONG WINAPI GetPackageInfo(PVOID reference, UINT32 flags, UINT32 *length, BYTE *buffer, UINT32 *count)
{
    return ERROR_INVALID_PARAMETER;
}

/* the notification is an event, set while its condition holds. */
DECLSPEC_EXPORT BOOL WINAPI QueryMemoryResourceNotification(HANDLE handle, PBOOL state)
{
    if (!state) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD wait = WaitForSingleObject(handle, 0);
    if (wait == WAIT_FAILED)
        return FALSE;
    *state = wait == WAIT_OBJECT_0;
    return TRUE;
}

/* registers for system managed suspend and resume. nothing here changes a process's state, so the routine is never
   called. */
DECLSPEC_EXPORT ULONG WINAPI RegisterAppStateChangeNotification(PVOID routine, PVOID context, PVOID *registration)
{
    if (!routine || !registration)
        return ERROR_INVALID_PARAMETER;
    PVOID *entry = KbAllocZero(sizeof(PVOID) * 2);
    if (!entry)
        return ERROR_NOT_ENOUGH_MEMORY;
    entry[0] = routine;
    entry[1] = context;
    *registration = entry;
    return ERROR_SUCCESS;
}

DECLSPEC_EXPORT VOID WINAPI UnregisterAppStateChangeNotification(PVOID registration)
{
    KbFree(registration);
}
