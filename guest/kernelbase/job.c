/*
 * job objects, over the kernel's (ntos/ps/job.c).
 */
#include "kernelbase_private.h"

#define JOB_OBJECT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3F)

DECLSPEC_EXPORT HANDLE WINAPI CreateJobObjectW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!KbNamedObjectAttributes(&oa, &us, name, FALSE, sa))
        return NULL;
    if (name)
        oa.Attributes |= OBJ_OPENIF;
    HANDLE h = NULL;
    NTSTATUS status = NtCreateJobObject(&h, JOB_OBJECT_ALL_ACCESS, &oa);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    KbSetError(status == STATUS_OBJECT_NAME_EXISTS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return h;
}

DECLSPEC_EXPORT HANDLE WINAPI CreateJobObjectA(LPSECURITY_ATTRIBUTES sa, LPCSTR name)
{
    PWSTR wide = name ? KbAnsiToWideCp(CP_ACP, name) : NULL;
    if (name && !wide)
        return NULL;
    HANDLE h = CreateJobObjectW(sa, wide);
    KbFree(wide);
    return h;
}

DECLSPEC_EXPORT HANDLE WINAPI OpenJobObjectW(DWORD access, BOOL inherit, LPCWSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!KbNamedObjectAttributes(&oa, &us, name, inherit, NULL))
        return NULL;
    HANDLE h = NULL;
    NTSTATUS status = NtOpenJobObject(&h, access, &oa);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    return h;
}

DECLSPEC_EXPORT HANDLE WINAPI OpenJobObjectA(DWORD access, BOOL inherit, LPCSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    PWSTR wide = KbAnsiToWideCp(CP_ACP, name);
    if (!wide)
        return NULL;
    HANDLE h = OpenJobObjectW(access, inherit, wide);
    KbFree(wide);
    return h;
}

DECLSPEC_EXPORT BOOL WINAPI AssignProcessToJobObject(HANDLE job, HANDLE process)
{
    NTSTATUS status = NtAssignProcessToJobObject(job, process);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI TerminateJobObject(HANDLE job, UINT exit_code)
{
    NTSTATUS status = NtTerminateJobObject(job, (NTSTATUS)exit_code);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI SetInformationJobObject(HANDLE job, int info_class, void *info, DWORD size)
{
    NTSTATUS status = NtSetInformationJobObject(job, (ULONG)info_class, info, size);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

DECLSPEC_EXPORT BOOL WINAPI QueryInformationJobObject(HANDLE job, int info_class, void *info, DWORD size, DWORD *returned)
{
    ULONG got = 0;
    NTSTATUS status = NtQueryInformationJobObject(job, (ULONG)info_class, info, size, &got);
    if (returned)
        *returned = got;
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

BOOL WINAPI IsProcessInJob(HANDLE process, HANDLE job, PBOOL result)
{
    if (!result) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NTSTATUS status = NtIsProcessInJob(process, job);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *result = status == STATUS_PROCESS_IN_JOB;
    return TRUE;
}
