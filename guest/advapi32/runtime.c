/* no impersonation or kernel object security updates are available yet. */
#include "advapi32_private.h"

ADVAPI BOOL WINAPI SetThreadToken(HANDLE *thread, HANDLE token)
{
    if (token) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    THREAD_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryInformationThread(thread ? *thread : NtCurrentThread(), ThreadBasicInformation,
        &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status)) {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI RevertToSelf(void)
{
    return SetThreadToken(nullptr, nullptr);
}

ADVAPI BOOL WINAPI SetKernelObjectSecurity(HANDLE handle, DWORD information, PVOID descriptor)
{
    SetLastError(descriptor ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}
