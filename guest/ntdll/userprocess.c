#include "ntdll_private.h"

#define PS_ATTRIBUTE_PARENT_PROCESS 0x00060000u
#define PS_ATTRIBUTE_DEBUG_PORT 0x00060001u
#define PS_ATTRIBUTE_CLIENT_ID 0x00010003u
#define PS_ATTRIBUTE_IMAGE_NAME 0x00020005u
#define PS_ATTRIBUTE_JOB_LIST 0x00020013u
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004u

/* what the image's headers tell, read from the new process, whose image is mapped and not started. */
static void image_information(HANDLE process, SECTION_IMAGE_INFORMATION *out)
{
    PROCESS_BASIC_INFORMATION basic;
    PVOID base = nullptr;
    if (NtQueryInformationProcess(process, ProcessBasicInformation, &basic, sizeof(basic), nullptr) ||
        !basic.PebBaseAddress ||
        NtReadVirtualMemory(process, &basic.PebBaseAddress->ImageBaseAddress, &base, sizeof(base), nullptr))
        return;
    IMAGE_DOS_HEADER dos;
    union {
        IMAGE_NT_HEADERS32 pe32;
        IMAGE_NT_HEADERS64 pe64;
    } nt;
    if (NtReadVirtualMemory(process, base, &dos, sizeof(dos), nullptr) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        NtReadVirtualMemory(process, (PUCHAR)base + dos.e_lfanew, &nt, sizeof(nt), nullptr) ||
        nt.pe32.Signature != IMAGE_NT_SIGNATURE)
        return;
    const IMAGE_FILE_HEADER *file = &nt.pe32.FileHeader;
    out->ImageCharacteristics = file->Characteristics;
    out->Machine = file->Machine;
    if (nt.pe32.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        const IMAGE_OPTIONAL_HEADER32 *o = &nt.pe32.OptionalHeader;
        out->TransferAddress = (PUCHAR)base + o->AddressOfEntryPoint;
        out->MaximumStackSize = o->SizeOfStackReserve;
        out->CommittedStackSize = o->SizeOfStackCommit;
        out->SubSystemType = o->Subsystem;
        out->SubSystemMajorVersion = o->MajorSubsystemVersion;
        out->SubSystemMinorVersion = o->MinorSubsystemVersion;
        out->MajorOperatingSystemVersion = o->MajorOperatingSystemVersion;
        out->MinorOperatingSystemVersion = o->MinorOperatingSystemVersion;
        out->DllCharacteristics = o->DllCharacteristics;
        out->ImageContainsCode = o->SizeOfCode != 0;
        out->LoaderFlags = o->LoaderFlags;
        out->CheckSum = o->CheckSum;
    } else {
        const IMAGE_OPTIONAL_HEADER64 *o = &nt.pe64.OptionalHeader;
        out->TransferAddress = (PUCHAR)base + o->AddressOfEntryPoint;
        out->MaximumStackSize = (SIZE_T)o->SizeOfStackReserve;
        out->CommittedStackSize = (SIZE_T)o->SizeOfStackCommit;
        out->SubSystemType = o->Subsystem;
        out->SubSystemMajorVersion = o->MajorSubsystemVersion;
        out->SubSystemMinorVersion = o->MinorSubsystemVersion;
        out->MajorOperatingSystemVersion = o->MajorOperatingSystemVersion;
        out->MinorOperatingSystemVersion = o->MinorOperatingSystemVersion;
        out->DllCharacteristics = o->DllCharacteristics;
        out->ImageContainsCode = o->SizeOfCode != 0;
        out->LoaderFlags = o->LoaderFlags;
        out->CheckSum = o->CheckSum;
    }
}

/* the process starts with its first thread suspended, as on windows. */
static NTSTATUS create_user_process(PUNICODE_STRING nt_image, PRTL_USER_PROCESS_PARAMETERS parameters,
                                    BOOLEAN inherit_handles, PVOID process_sd,
                                    PVOID thread_sd, HANDLE parent, HANDLE debug_port, HANDLE token,
                                    HANDLE job, PRTL_USER_PROCESS_INFORMATION info)
{
    if (!nt_image || !parameters || !info)
        return STATUS_INVALID_PARAMETER;
    CLIENT_ID client = {};
    struct {
        SIZE_T TotalLength;
        PS_ATTRIBUTE Attributes[6];
    } list = {};
    ULONG n = 0;
    list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_IMAGE_NAME, nt_image->Length, {.ValuePtr = nt_image->Buffer}};
    list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_CLIENT_ID, sizeof(client), {.ValuePtr = &client}};
    if (parent)
        list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_PARENT_PROCESS, sizeof(HANDLE), {.Value = (ULONG_PTR)parent}};
    if (debug_port)
        list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_DEBUG_PORT, sizeof(HANDLE), {.Value = (ULONG_PTR)debug_port}};
    if (token)
        list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_TOKEN, sizeof(HANDLE), {.Value = (ULONG_PTR)token}};
    if (job)
        list.Attributes[n++] = (PS_ATTRIBUTE){PS_ATTRIBUTE_JOB_LIST, sizeof(HANDLE), {.ValuePtr = &job}};
    list.TotalLength = sizeof(SIZE_T) + n * sizeof(PS_ATTRIBUTE);
    OBJECT_ATTRIBUTES process_oa, thread_oa;
    InitializeObjectAttributes(&process_oa, nullptr, 0, nullptr, process_sd);
    InitializeObjectAttributes(&thread_oa, nullptr, 0, nullptr, thread_sd);
    PS_CREATE_INFO create = {.Size = sizeof(create)};
    HANDLE process, thread;
    NTSTATUS status = NtCreateUserProcess(&process, &thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS,
                                          &process_oa, &thread_oa,
                                          inherit_handles ? PROCESS_CREATE_FLAGS_INHERIT_HANDLES : 0, 1, parameters,
                                          &create, &list);
    if (!NT_SUCCESS(status))
        return status;
    memset(info, 0, sizeof(*info));
    info->Length = sizeof(*info);
    info->ProcessHandle = process;
    info->ThreadHandle = thread;
    info->ClientId = client;
    image_information(process, &info->ImageInformation);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateUserProcess(PUNICODE_STRING nt_image, ULONG attributes,
                                                 PRTL_USER_PROCESS_PARAMETERS parameters, PVOID process_sd,
                                                 PVOID thread_sd, HANDLE parent, BOOLEAN inherit_handles,
                                                 HANDLE debug_port, HANDLE token, PRTL_USER_PROCESS_INFORMATION info)
{
    (void)attributes;
    return create_user_process(nt_image, parameters, inherit_handles, process_sd, thread_sd, parent, debug_port, token,
                               nullptr, info);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateUserProcessEx(PUNICODE_STRING nt_image, PRTL_USER_PROCESS_PARAMETERS parameters,
                                                   BOOLEAN inherit_handles,
                                                   PRTL_USER_PROCESS_EXTENDED_PARAMETERS extended,
                                                   PRTL_USER_PROCESS_INFORMATION info)
{
    RTL_USER_PROCESS_EXTENDED_PARAMETERS none = {};
    if (!extended)
        extended = &none;
    else if (extended->Version != 1)
        return STATUS_INVALID_PARAMETER;
    return create_user_process(nt_image, parameters, inherit_handles, extended->ProcessSecurityDescriptor,
                               extended->ThreadSecurityDescriptor, extended->ParentProcess, extended->DebugPort,
                               extended->TokenHandle, extended->JobHandle, info);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateUserThread(HANDLE process, PVOID sd, BOOLEAN suspended,
                                                ULONG zero_bits, SIZE_T maximum_stack, SIZE_T committed_stack,
                                                PVOID start, PVOID parameter, PHANDLE thread, PCLIENT_ID client_id)
{
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, sd);
    HANDLE handle;
    NTSTATUS status = NtCreateThreadEx(&handle, THREAD_ALL_ACCESS, &oa, process, start, parameter, suspended ? 1 : 0,
                                       zero_bits, committed_stack, maximum_stack, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (client_id) {
        THREAD_BASIC_INFORMATION basic = {};
        NtQueryInformationThread(handle, ThreadBasicInformation, &basic, sizeof(basic), nullptr);
        *client_id = basic.ClientId;
    }
    if (thread)
        *thread = handle;
    else
        NtClose(handle);
    return STATUS_SUCCESS;
}
