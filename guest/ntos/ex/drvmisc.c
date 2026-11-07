#include "../ntos.h"

/* small kernel routines drivers use, which windows exports and mime's own code has no need of. */

#include <seh/x64seh.h>

void NTAPI RtlCaptureContext(PCONTEXT context);
PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history);
PEXCEPTION_ROUTINE NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PRUNTIME_FUNCTION function, PCONTEXT c,
                                          PVOID *handler_data, PULONG64 establisher_frame,
                                          PKNONVOLATILE_CONTEXT_POINTERS pointers);
NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE handle, PUNICODE_STRING target, PULONG returned);

#define MM_COPY_MEMORY_PHYSICAL 0x1
#define MM_COPY_MEMORY_VIRTUAL 0x2

/* the free build marker over the build number, as windows exports it. */
ULONG NtBuildNumber = 0xF0000000u | MIME_NT_BUILD_NUMBER;

typedef struct _RTL_OSVERSIONINFOEXW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
    USHORT wServicePackMajor;
    USHORT wServicePackMinor;
    USHORT wSuiteMask;
    UCHAR wProductType;
    UCHAR wReserved;
} RTL_OSVERSIONINFOEXW;

NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOEXW *info)
{
    ULONG size = info->dwOSVersionInfoSize;
    if (size != 0x114 && size != sizeof(RTL_OSVERSIONINFOEXW))
        return STATUS_INVALID_PARAMETER;
    memset((PUCHAR)info + sizeof(ULONG), 0, size - sizeof(ULONG));
    info->dwMajorVersion = MIME_NT_MAJOR_VERSION;
    info->dwMinorVersion = MIME_NT_MINOR_VERSION;
    info->dwBuildNumber = MIME_NT_BUILD_NUMBER;
    info->dwPlatformId = 2;
    if (size == sizeof(RTL_OSVERSIONINFOEXW)) {
        info->wSuiteMask = 0x100; /* VER_SUITE_SINGLEUSERTS */
        info->wProductType = 1;   /* VER_NT_WORKSTATION */
    }
    return STATUS_SUCCESS;
}

PIMAGE_NT_HEADERS64 NTAPI RtlImageNtHeader(PVOID base)
{
    PIMAGE_DOS_HEADER dos = base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || (ULONG)dos->e_lfanew >= 0x10000000)
        return nullptr;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

/* a multiplicative generator over 2^31 - 1 with a shuffle table, the kind RtlRandomEx is. */
ULONG NTAPI RtlRandomEx(PULONG seed)
{
    static ULONG table[128];
    static ULONG last;
    static LONG ready;
    ULONG s = *seed;
#define NEXT(x) ((ULONG)(((ULONG64)(x) * 16807u) % 0x7FFFFFFFu))
    if (!__atomic_exchange_n(&ready, 1, __ATOMIC_ACQ_REL)) {
        ULONG x = 0x7FFFFFED;
        for (ULONG i = 0; i < 128; i++)
            table[i] = x = NEXT(x);
        last = NEXT(x);
    }
    s = NEXT(s ? s : 1);
    ULONG index = last & 127;
    last = table[index];
    table[index] = s;
    *seed = s;
#undef NEXT
    return last;
}

/*
 * the return addresses of the current thread's kernel frames, past the first skip ones. with flag 1 the user mode
 * frames are asked for, which are not walked here.
 */
ULONG NTAPI RtlWalkFrameChain(PVOID *callers, ULONG count, ULONG flags)
{
    if (flags & 1)
        return 0;
    CONTEXT context;
    RtlCaptureContext(&context);
    PKTHREAD thread = KiCurrentThread();
    ULONG64 top = thread ? (ULONG64)thread->InitialStack : 0;
    ULONG n = 0;
    for (ULONG depth = 0; depth < 256 && n < count; depth++) {
        ULONG64 base;
        PVOID function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (!function)
            break;
        PVOID data;
        ULONG64 frame;
        RtlVirtualUnwind(0, base, context.Rip, function, &context, &data, &frame, nullptr);
        if (!context.Rip || context.Rsp >= top || !MiIsCodeAddress(context.Rip))
            break;
        if (depth)
            callers[n++] = (PVOID)context.Rip;
    }
    return n;
}


PEPROCESS NTAPI IoGetCurrentProcess(void)
{
    return PsGetCurrentProcess();
}

PEPROCESS NTAPI IoThreadToProcess(PETHREAD thread)
{
    return (PEPROCESS)thread->Tcb.Process;
}

PUCHAR NTAPI PsGetProcessImageFileName(PEPROCESS process)
{
    return (PUCHAR)process->ImageFileName;
}

HANDLE NTAPI PsGetProcessInheritedFromUniqueProcessId(PEPROCESS process)
{
    return process->InheritedFromUniqueProcessId;
}

ULONG NTAPI PsGetProcessSessionId(PEPROCESS process)
{
    return process->SessionId;
}

/* the 32-bit peb of a wow64 process, or null. */
PVOID NTAPI PsGetProcessWow64Process(PEPROCESS process)
{
    return process->Wow64Peb;
}

/* the process stays until the release, or the acquisition fails when it is ending. */
NTSTATUS NTAPI PsAcquireProcessExitSynchronization(PEPROCESS process)
{
    if (process->Exited)
        return STATUS_PROCESS_IS_TERMINATING;
    ObReferenceObject(process);
    return STATUS_SUCCESS;
}

void NTAPI PsReleaseProcessExitSynchronization(PEPROCESS process)
{
    ObDereferenceObject(process);
}

NTSTATUS NTAPI NtGetContextThread(HANDLE handle, PCONTEXT context);

NTSTATUS NTAPI PsGetContextThread(PETHREAD thread, PCONTEXT context, KPROCESSOR_MODE mode)
{
    HANDLE handle;
    NTSTATUS status =
        ObOpenObjectByPointer(thread, OBJ_KERNEL_HANDLE, nullptr, THREAD_GET_CONTEXT, PsThreadTypeObject, KernelMode, &handle);
    if (!NT_SUCCESS(status))
        return status;
    PKTHREAD current = KiCurrentThread();
    KPROCESSOR_MODE old = current->PreviousMode;
    current->PreviousMode = mode;
    status = NtGetContextThread(handle, context);
    current->PreviousMode = old;
    ObCloseHandle(handle, KernelMode);
    return status;
}

PIRP NTAPI IoGetTopLevelIrp(void)
{
    return (PIRP)PsCurrentThread()->TopLevelIrp;
}

void NTAPI IoSetTopLevelIrp(PIRP irp)
{
    PsCurrentThread()->TopLevelIrp = irp;
}

/* a copy that a missing page ends, from a physical address of the machine's ram or a mapped virtual one. */
NTSTATUS NTAPI MmCopyMemory(PVOID target, ULONG64 source, SIZE_T size, ULONG flags, PSIZE_T copied)
{
    *copied = 0;
    if (flags == MM_COPY_MEMORY_PHYSICAL) {
        ULONG64 end = source + size;
        if (end < source || source < HalBootInfo->ram_base || end > HalBootInfo->ram_base + HalBootInfo->ram_size)
            return STATUS_INVALID_ADDRESS;
        for (SIZE_T done = 0; done < size;) {
            ULONG64 pa = source + done;
            SIZE_T chunk = PAGE_SIZE - (pa & (PAGE_SIZE - 1));
            if (chunk > size - done)
                chunk = size - done;
            memcpy((PUCHAR)target + done, HalPhysmap(pa), chunk);
            done += chunk;
            *copied = done;
        }
        return STATUS_SUCCESS;
    }
    if (flags != MM_COPY_MEMORY_VIRTUAL)
        return STATUS_INVALID_PARAMETER;
    for (SIZE_T done = 0; done < size;) {
        ULONG_PTR va = (ULONG_PTR)source + done;
        SIZE_T chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > size - done)
            chunk = size - done;
        if (!MmIsAddressValid((PVOID)va))
            return done ? STATUS_PARTIAL_COPY : STATUS_ACCESS_VIOLATION;
        memcpy((PUCHAR)target + done, (PVOID)va, chunk);
        done += chunk;
        *copied = done;
    }
    return STATUS_SUCCESS;
}

/* a range of physical addresses in system space, uncached unless the caller asks for a cached mapping. */
PVOID NTAPI MmMapIoSpace(ULONG64 address, SIZE_T size, MEMORY_CACHING_TYPE cache)
{
    SIZE_T pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(address, size);
    if (!size || pages > 0x10000)
        return nullptr;
    ULONG64 frames[64], *list = pages <= RTL_NUMBER_OF(frames) ? frames : ExAllocatePool2(POOL_FLAG_NON_PAGED, pages * 8, KI_TAG('M', 'm', 'I', 'o'));
    if (!list)
        return nullptr;
    for (SIZE_T i = 0; i < pages; i++)
        list[i] = (address & ~(ULONG64)(PAGE_SIZE - 1)) + i * PAGE_SIZE;
    ULONG64 attrs = MM_KERNEL_RW | (cache == MmCached ? 0 : MM_PTE_PCD);
    PUCHAR va = MmMapPagesInSystem(list, pages, attrs);
    if (list != frames)
        ExFreePool(list);
    return va ? va + (address & (PAGE_SIZE - 1)) : nullptr;
}

PVOID NTAPI MmMapIoSpaceEx(ULONG64 address, SIZE_T size, ULONG protect)
{
    return MmMapIoSpace(address, size, (protect & PAGE_NOCACHE) ? MmNonCached : MmCached);
}

void NTAPI MmUnmapIoSpace(PVOID base, SIZE_T size)
{
    PUCHAR va = PAGE_ALIGN(base);
    SIZE_T pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(base, size);
    for (SIZE_T i = 0; i < pages; i++)
        MiUnmapPage(MiKernelRoot(), (ULONG_PTR)va + i * PAGE_SIZE, 0);
    MmFreeSystemVa(va, pages);
}

NTSTATUS NTAPI MmUnmapViewOfSection(PEPROCESS process, PVOID base)
{
    return MiUnmapViewOfSection(process, base);
}

/* push locks are never contended here: there is no one to wake. */
void FASTCALL ExfUnblockPushLock(PVOID lock, PVOID wait_block)
{
}

/* the Zw services: the Nt ones with the previous mode kernel, so kernel buffers and handles are taken as they are. */
#define ZW_CALL(call)                                                                                                \
    do {                                                                                                             \
        PKTHREAD current_ = KiCurrentThread();                                                                       \
        KPROCESSOR_MODE old_ = current_->PreviousMode;                                                               \
        current_->PreviousMode = KernelMode;                                                                         \
        NTSTATUS status_ = (call);                                                                                   \
        current_->PreviousMode = old_;                                                                               \
        return status_;                                                                                              \
    } while (0)

NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                     PIO_STATUS_BLOCK iosb, ULONG code, PVOID in, ULONG in_length, PVOID out,
                                     ULONG out_length);
NTSTATUS NTAPI NtOpenDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI NtOpenSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI NtOpenThread(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid);
NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE handle, PVOID buffer, ULONG length, BOOLEAN single, BOOLEAN restart,
                                      PULONG context, PULONG returned);
NTSTATUS NTAPI NtQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length,
                                        PULONG returned);
NTSTATUS NTAPI NtTerminateProcess(HANDLE handle, NTSTATUS status);
NTSTATUS NTAPI NtTraceControl(ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned);

NTSTATUS NTAPI ZwDeviceIoControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                     PIO_STATUS_BLOCK iosb, ULONG code, PVOID in, ULONG in_length, PVOID out,
                                     ULONG out_length)
{
    ZW_CALL(NtDeviceIoControlFile(handle, event, apc, apc_context, iosb, code, in, in_length, out, out_length));
}

NTSTATUS NTAPI ZwOpenDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    ZW_CALL(NtOpenDirectoryObject(handle, access, attributes));
}

NTSTATUS NTAPI ZwOpenSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    ZW_CALL(NtOpenSection(handle, access, attributes));
}

NTSTATUS NTAPI ZwOpenThread(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid)
{
    ZW_CALL(NtOpenThread(handle, access, attributes, cid));
}

NTSTATUS NTAPI ZwQueryDirectoryObject(HANDLE handle, PVOID buffer, ULONG length, BOOLEAN single, BOOLEAN restart,
                                      PULONG context, PULONG returned)
{
    ZW_CALL(NtQueryDirectoryObject(handle, buffer, length, single, restart, context, returned));
}

NTSTATUS NTAPI ZwQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length, PULONG returned)
{
    ZW_CALL(NtQueryInformationThread(handle, klass, info, length, returned));
}

NTSTATUS NTAPI ZwTerminateProcess(HANDLE handle, NTSTATUS exit_status)
{
    ZW_CALL(NtTerminateProcess(handle, exit_status));
}

NTSTATUS NTAPI ZwTraceControl(ULONG code, PVOID in, ULONG in_length, PVOID out, ULONG out_length, PULONG returned)
{
    ZW_CALL(NtTraceControl(code, in, in_length, out, out_length, returned));
}

/*
 * the DOS path of a file, as an OBJECT_NAME_INFORMATION from paged pool the caller frees: the drive letter whose
 * link names the file's volume, then the path in the volume.
 */
static NTSTATUS file_dos_name(PFILE_OBJECT file, POBJECT_NAME_INFORMATION *out);

NTSTATUS NTAPI IoQueryFileDosDeviceName(PFILE_OBJECT file, POBJECT_NAME_INFORMATION *out)
{
    ZW_CALL(file_dos_name(file, out));
}

static NTSTATUS file_dos_name(PFILE_OBJECT file, POBJECT_NAME_INFORMATION *out)
{
    WCHAR full[1024];
    UNICODE_STRING name;
    NTSTATUS status = ObpQueryNameString(file, &name, full, RTL_NUMBER_OF(full));
    if (!NT_SUCCESS(status))
        return status;
    WCHAR dos[1040];
    ULONG length = 0;
    for (WCHAR letter = L'A'; letter <= L'Z' && !length; letter++) {
        WCHAR link_text[] = L"\\GLOBAL??\\X:";
        link_text[10] = letter;
        UNICODE_STRING link_name;
        RtlInitUnicodeString(&link_name, link_text);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &link_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
        HANDLE link;
        if (!NT_SUCCESS(NtOpenSymbolicLinkObject(&link, GENERIC_READ, &oa)))
            continue;
        WCHAR target_text[256];
        UNICODE_STRING target = {0, sizeof(target_text) - sizeof(WCHAR), target_text};
        NTSTATUS queried = NtQuerySymbolicLinkObject(link, &target, nullptr);
        ZwClose(link);
        USHORT n = target.Length;
        if (!NT_SUCCESS(queried) || !n || name.Length <= n || memcmp(name.Buffer, target.Buffer, n) ||
            name.Buffer[n / sizeof(WCHAR)] != L'\\')
            continue;
        dos[0] = letter;
        dos[1] = L':';
        ULONG rest = (name.Length - n) / sizeof(WCHAR);
        if (rest + 2 >= RTL_NUMBER_OF(dos))
            return STATUS_NAME_TOO_LONG;
        memcpy(dos + 2, name.Buffer + n / sizeof(WCHAR), rest * sizeof(WCHAR));
        length = rest + 2;
    }
    if (!length)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    POBJECT_NAME_INFORMATION info = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(*info) + (length + 1) * sizeof(WCHAR),
                                                    KI_TAG('I', 'o', 'D', 'n'));
    if (!info)
        return STATUS_INSUFFICIENT_RESOURCES;
    info->Name.Buffer = (PWSTR)(info + 1);
    memcpy(info->Name.Buffer, dos, length * sizeof(WCHAR));
    info->Name.Buffer[length] = 0;
    info->Name.Length = (USHORT)(length * sizeof(WCHAR));
    info->Name.MaximumLength = (USHORT)((length + 1) * sizeof(WCHAR));
    *out = info;
    return STATUS_SUCCESS;
}
