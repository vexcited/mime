/*
 * the application model beyond the names of packages: the information of a package that is open, the notifications of
 * the state of an application and the memory resource notifications. No package is installed and no application is in a
 * state a system manages, so a package is never found and nothing that is open can be asked about.
 */
#include "kernelbase_private.h"

LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *length, BYTE *buffer);

/* a package by its full name: one that is well formed is not there, and the reference of a package is never given. */
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

/* a notification of the memory of the machine is never made (see CreateMemoryResourceNotification), so no handle is one. */
DECLSPEC_EXPORT BOOL WINAPI QueryMemoryResourceNotification(HANDLE handle, PBOOL state)
{
    if (!state) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* the registration of a routine for the changes of the state of the application, which are the suspension and the
   resumption that a system manages. Nothing here changes the state of a process, so the routine is never called. */
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
