/* the profiling services of the kernel, which this system does not offer: they fail as the kernel does for all calls. */
#include "../ntdll_private.h"

NTSTATUS NTAPI NtCreateProfile(PHANDLE profile, HANDLE process, PVOID base, SIZE_T size, ULONG bucket_shift,
                               PULONG buffer, ULONG buffer_size, ULONG source, ULONG affinity)
{
    if (profile)
        *profile = nullptr;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NTAPI NtStartProfile(HANDLE profile)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NTAPI NtStopProfile(HANDLE profile)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NTAPI NtSetIntervalProfile(ULONG interval, ULONG source)
{
    return STATUS_NOT_IMPLEMENTED;
}
