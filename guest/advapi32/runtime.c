/* no kernel object security updates are available yet. */
#include "advapi32_private.h"

ADVAPI BOOL WINAPI SetKernelObjectSecurity(HANDLE handle, DWORD information, PVOID descriptor)
{
    SetLastError(descriptor ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}
