/*
 * information classes of 32-bit system calls. records with narrower pointers,
 * handles and sizes are converted both ways, matching ones pass through. a
 * class the kernel does not answer and whose 32-bit layout differs is refused,
 * as windows' wow64 refuses classes it does not know.
 */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include "wow32_convert.h"

/* classes nt.h does not name. */
enum {
    ProcessExceptionPort = 8,
    ProcessDeviceMap = 23,
    ThreadSetTlsArrayAddress = 15,
    ThreadGroupInformation = 30,
    ThreadNameInformation = 38,
    ObjectHandleFlagInformation = 4,
    ObjectSessionInformation = 5,
    JobObjectBasicAccountingInformation = 1,
    JobObjectBasicLimitInformation = 2,
    JobObjectBasicProcessIdList = 3,
    JobObjectBasicUIRestrictions = 4,
    JobObjectAssociateCompletionPortInformation = 7,
    JobObjectBasicAndIoAccountingInformation = 8,
    JobObjectExtendedLimitInformation = 9,
};

/* query classes whose 32-bit records match the native ones. */
static const UCHAR process_same[] = {2,  4,  10, 12, 18, 19, 20, 22, 24, 28, 29, 31, 32, 33, 34, 36, 39, 45,
                                     46, 47, 50, 52, 54, 58, 59, 61, 64, 66, 67, 70, 71, 77, 82, 85, 88, 97, 109, 110, 111};
static const UCHAR thread_same[] = {1, 12, 14, 16, 17, 18, 20, 22, 24, 25, 27, 28, 32, 33, 34, 35, 39, 42, 44, 54, 55, 59};

static BOOLEAN listed(const UCHAR *list, SIZE_T count, ULONG klass)
{
    for (SIZE_T i = 0; i < count; i++)
        if (list[i] == klass)
            return TRUE;
    return FALSE;
}

PVOID Mx32Scratch(SIZE_T size)
{
    PVOID base = nullptr;
    if (!size)
        size = 1;
    return NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE))
               ? base
               : nullptr;
}

void Mx32FreeScratch(PVOID base)
{
    SIZE_T size = 0;
    if (base)
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}

static ULONG_PTR current_process_id(void)
{
    return (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

/* the native 32-bit peb the kernel keeps for a wow64 process, or 0. */
static ULONG_PTR wow64_peb(HANDLE process)
{
    ULONG_PTR peb = 0;
    if (!NT_SUCCESS(NtQueryInformationProcess(process, ProcessWow64Information, &peb, sizeof(peb), nullptr)))
        return 0;
    return peb;
}

/* this process's own 32-bit peb, else the kernel's for a wow64 process. */
static ULONG peb32_of(HANDLE process, ULONG_PTR id)
{
    if (process == NtCurrentProcess() || id == current_process_id())
        return (ULONG)(ULONG_PTR)MxCurrentPeb32();
    return (ULONG)wow64_peb(process);
}

static BOOLEAN is_current_process(HANDLE process)
{
    if (process == NtCurrentProcess())
        return TRUE;
    PROCESS_BASIC_INFORMATION info;
    return NT_SUCCESS(NtQueryInformationProcess(process, ProcessBasicInformation, &info, sizeof(info), nullptr)) &&
           info.UniqueProcessId == current_process_id();
}

/* the kernel gives each thread of a wow64 process its 32-bit teb right after the native one. */
ULONG Mx32TebOf(HANDLE process, PVOID teb)
{
    if (!teb)
        return 0;
    if (teb == NtCurrentTeb())
        return (ULONG)(ULONG_PTR)MxCurrentTeb32();
    return wow64_peb(process) ? (ULONG)((ULONG_PTR)teb + MIME_TEB_SIZE) : 0;
}

/* moves a counted string from after the native header to after the 8-byte 32-bit header. */
static void narrow_string(const UNICODE_STRING *in, PVOID out)
{
    MIME_STRING32 *o = out;
    PWSTR text = (PWSTR)(o + 1);
    if (in->Buffer)
        memmove(text, in->Buffer, in->MaximumLength);
    *o = (MIME_STRING32){in->Length, in->MaximumLength, in->Buffer ? (ULONG)(ULONG_PTR)text : 0};
}

/*
 * a query answered with a counted string: the buffer is asked 8 bytes longer
 * for the wider header, and the needed length comes back 8 bytes shorter.
 */
typedef NTSTATUS (*STRING_QUERY)(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned);

static NTSTATUS query_string32(STRING_QUERY query, const ULONG *a)
{
    ULONG length = a[3];
    if (length > 0xFFFF0000u)
        return STATUS_INVALID_PARAMETER;
    PVOID native = Mx32Scratch((SIZE_T)length + 8);
    if (!native)
        return STATUS_NO_MEMORY;
    ULONG needed = 0;
    NTSTATUS status = query(handle32(a[0]), a[1], native, length + 8, &needed);
    if (NT_SUCCESS(status) && length < sizeof(MIME_STRING32))
        status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(status))
        narrow_string(native, pointer32(a[2]));
    store32(a[4], needed > 8 ? needed - 8 : needed);
    Mx32FreeScratch(native);
    return status;
}

static NTSTATUS query_process_string(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return NtQueryInformationProcess(handle, klass, info, length, returned);
}

static NTSTATUS query_thread_string(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return NtQueryInformationThread(handle, klass, info, length, returned);
}

static NTSTATUS query_object_string(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return NtQueryObject(handle, klass, info, length, returned);
}

/* ------------------------------------------------------------ processes */

NTSTATUS Mx32QueryInformationProcess(const ULONG *a)
{
    HANDLE process = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    PULONG out = pointer32(a[2]);
    NTSTATUS status;
    switch (klass) {
    case ProcessBasicInformation: {
        /* the plain record, or the extended one: its size, the record, then flags. */
        BOOLEAN extended = length == 32;
        if (length != 24 && !extended)
            return STATUS_INFO_LENGTH_MISMATCH;
        PROCESS_BASIC_INFORMATION info;
        status = NtQueryInformationProcess(process, klass, &info, sizeof(info), nullptr);
        if (!NT_SUCCESS(status))
            return status;
        if (extended) {
            out[0] = 32;
            out[7] = 0;
            out++;
        }
        out[0] = (ULONG)info.ExitStatus;
        out[1] = peb32_of(process, info.UniqueProcessId);
        out[2] = (ULONG)info.AffinityMask | (ULONG)((ULONG64)info.AffinityMask >> 32);
        out[3] = (ULONG)info.BasePriority;
        out[4] = (ULONG)info.UniqueProcessId;
        out[5] = (ULONG)info.InheritedFromUniqueProcessId;
        store32(a[4], length);
        return status;
    }
    case ProcessDebugPort: {
        ULONG_PTR port = 0;
        status = NtQueryInformationProcess(process, klass, &port, sizeof(port), nullptr);
        if (NT_SUCCESS(status) && length < sizeof(ULONG))
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status))
            out[0] = (ULONG)port;
        if (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)
            store32(a[4], sizeof(ULONG));
        return status;
    }
    case ProcessWow64Information:
        if (length != sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (is_current_process(process)) {
            out[0] = (ULONG)(ULONG_PTR)MxCurrentPeb32();
        } else {
            ULONG_PTR peb = 0;
            status = NtQueryInformationProcess(process, klass, &peb, sizeof(peb), nullptr);
            if (!NT_SUCCESS(status))
                return status;
            out[0] = (ULONG)peb;
        }
        store32(a[4], sizeof(ULONG));
        return STATUS_SUCCESS;
    case ProcessVmCounters: {
        /* the sizes are 32 bits wide: eleven counters, the private bytes, and two 64-bit counters after them. */
        if (length != 44 && length != 48 && length != 64)
            return STATUS_INFO_LENGTH_MISMATCH;
        SIZE_T v[14] = {};
        status = NtQueryInformationProcess(process, klass, v, 112, nullptr);
        if (!NT_SUCCESS(status))
            return status;
        for (ULONG i = 0; i < 12 && i < length / 4; i++)
            out[i] = saturate32(v[i]);
        if (length == 64) {
            memcpy(out + 12, &v[12], 8);
            memcpy(out + 14, &v[13], 8);
        }
        store32(a[4], length);
        return STATUS_SUCCESS;
    }
    case ProcessQuotaLimits: {
        /* QUOTA_LIMITS and QUOTA_LIMITS_EX: five sizes, a time, then four sizes, flags and a rate. */
        if (length != 32 && length != 56)
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG64 q[11] = {};
        status = NtQueryInformationProcess(process, klass, q, 88, nullptr);
        if (!NT_SUCCESS(status))
            return status;
        memset(out, 0, length);
        for (ULONG i = 0; i < 5; i++)
            out[i] = saturate32(q[i]);
        memcpy(out + 6, &q[5], 8);
        if (length == 56) {
            for (ULONG i = 0; i < 4; i++)
                out[8 + i] = saturate32(q[6 + i]);
            memcpy(out + 12, &q[10], 8);
        }
        store32(a[4], length);
        return STATUS_SUCCESS;
    }
    case ProcessImageFileName:
    case ProcessImageFileNameWin32:
        return query_string32(query_process_string, a);
    default:
        if (!listed(process_same, sizeof(process_same), klass))
            return STATUS_INVALID_INFO_CLASS;
        return NtQueryInformationProcess(process, klass, out, length, pointer32(a[4]));
    }
}

NTSTATUS Mx32SetInformationProcess(const ULONG *a)
{
    HANDLE process = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    const ULONG *in = pointer32(a[2]);
    switch (klass) {
    case ProcessQuotaLimits: {
        if (length != 32 && length != 56)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return NtSetInformationProcess(process, klass, nullptr, length == 32 ? 48 : 88);
        ULONG64 q[11] = {in[0], in[1], in[2], in[3], in[4]};
        /* a working set bound of -1 stays -1. */
        q[2] = in[2] == 0xFFFFFFFFu ? ~0ull : in[2];
        q[3] = in[3] == 0xFFFFFFFFu ? ~0ull : in[3];
        memcpy(&q[5], in + 6, 8);
        if (length == 56) {
            for (ULONG i = 0; i < 4; i++)
                q[6 + i] = in[8 + i];
            memcpy(&q[10], in + 12, 8);
        }
        return NtSetInformationProcess(process, klass, q, length == 32 ? 48 : 88);
    }
    case ProcessExceptionPort:
    case ProcessDeviceMap: {
        /* a port, or a directory handle followed by what is only read back. */
        if (length < sizeof(ULONG) || (klass == ProcessDeviceMap && length < 36))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        UCHAR map[40] = {};
        *(HANDLE *)map = handle32(in[0]);
        return NtSetInformationProcess(process, klass, map, klass == ProcessDeviceMap ? 40 : sizeof(HANDLE));
    }
    case ProcessAccessToken: {
        if (length < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        HANDLE pair[2] = {handle32(in[0]), handle32(in[1])};
        return NtSetInformationProcess(process, klass, pair, sizeof(pair));
    }
    case ProcessAffinityMask: {
        /* a mask, or a GROUP_AFFINITY whose mask widens. */
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        if (length == 4) {
            ULONG_PTR mask = in[0];
            return NtSetInformationProcess(process, klass, &mask, sizeof(mask));
        }
        if (length == 12) {
            ULONG64 group[2] = {in[0], 0};
            memcpy(&group[1], in + 1, 8);
            return NtSetInformationProcess(process, klass, group, sizeof(group));
        }
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    default:
        return NtSetInformationProcess(process, klass, (PVOID)in, length);
    }
}

/* ------------------------------------------------------------ threads */

NTSTATUS Mx32QueryInformationThread(const ULONG *a)
{
    HANDLE thread = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    PULONG out = pointer32(a[2]);
    NTSTATUS status;
    switch (klass) {
    case ThreadBasicInformation: {
        store32(a[4], 28);
        if (!out)
            return STATUS_INVALID_PARAMETER;
        if (length != 28)
            return STATUS_INFO_LENGTH_MISMATCH;
        THREAD_BASIC_INFORMATION info;
        status = NtQueryInformationThread(thread, klass, &info, sizeof(info), nullptr);
        if (!NT_SUCCESS(status))
            return status;
        ULONG teb = 0;
        if ((ULONG_PTR)info.ClientId.UniqueProcess == current_process_id()) {
            teb = Mx32TebOf(NtCurrentProcess(), info.TebBaseAddress);
        } else if (info.TebBaseAddress) {
            HANDLE process = nullptr;
            OBJECT_ATTRIBUTES oa = {sizeof(oa)};
            CLIENT_ID id = {info.ClientId.UniqueProcess, nullptr};
            if (NT_SUCCESS(NtOpenProcess(&process, PROCESS_QUERY_INFORMATION, &oa, &id))) {
                teb = Mx32TebOf(process, info.TebBaseAddress);
                NtClose(process);
            }
        }
        out[0] = (ULONG)info.ExitStatus;
        out[1] = teb;
        out[2] = (ULONG)(ULONG_PTR)info.ClientId.UniqueProcess;
        out[3] = (ULONG)(ULONG_PTR)info.ClientId.UniqueThread;
        out[4] = (ULONG)info.AffinityMask | (ULONG)((ULONG64)info.AffinityMask >> 32);
        out[5] = (ULONG)info.Priority;
        out[6] = (ULONG)info.BasePriority;
        return status;
    }
    case ThreadQuerySetWin32StartAddress: {
        ULONG_PTR start = 0;
        ULONG needed = 0;
        status = NtQueryInformationThread(thread, klass, &start, sizeof(start), &needed);
        if (NT_SUCCESS(status) && length < sizeof(ULONG))
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status))
            out[0] = (ULONG)start;
        store32(a[4], NT_SUCCESS(status) ? sizeof(ULONG) : needed > 4 ? needed - 4 : needed);
        return status;
    }
    case ThreadNameInformation:
        return query_string32(query_thread_string, a);
    default:
        if (!listed(thread_same, sizeof(thread_same), klass))
            return STATUS_INVALID_INFO_CLASS;
        return NtQueryInformationThread(thread, klass, out, length, pointer32(a[4]));
    }
}

NTSTATUS Mx32SetInformationThread(const ULONG *a)
{
    HANDLE thread = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    const ULONG *in = pointer32(a[2]);
    switch (klass) {
    case ThreadAffinityMask:
    case ThreadQuerySetWin32StartAddress:
    case ThreadSetTlsArrayAddress:
    case 47: {
        /* a mask or an address, zero extended as windows widens them. */
        if (length < sizeof(ULONG) || (klass == ThreadAffinityMask && in && length != sizeof(ULONG)))
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG_PTR value = in ? in[0] : 0;
        return NtSetInformationThread(thread, klass, in ? &value : nullptr, sizeof(value));
    }
    case ThreadImpersonationToken: {
        if (length < sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        HANDLE token = in ? handle32(in[0]) : nullptr;
        return NtSetInformationThread(thread, klass, in ? &token : nullptr, sizeof(token));
    }
    case ThreadGroupInformation: {
        if (length != 12)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        ULONG64 group[2] = {in[0], 0};
        memcpy(&group[1], in + 1, 8);
        return NtSetInformationThread(thread, klass, group, sizeof(group));
    }
    case ThreadNameInformation: {
        if (length < sizeof(MIME_STRING32))
            return STATUS_INFO_LENGTH_MISMATCH;
        UNICODE_STRING name = in ? string32(a[2]) : (UNICODE_STRING){0};
        return NtSetInformationThread(thread, klass, in ? &name : nullptr, sizeof(name));
    }
    default:
        return NtSetInformationThread(thread, klass, (PVOID)in, length);
    }
}

/* ------------------------------------------------------------ the system */

typedef struct {
    LARGE_INTEGER KernelTime, UserTime, CreateTime;
    ULONG WaitTime, StartAddress, UniqueProcess, UniqueThread;
    LONG Priority, BasePriority;
    ULONG ContextSwitches, ThreadState, WaitReason;
} THREAD_INFORMATION32;

typedef struct {
    ULONG NextEntryOffset, NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount, NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime, UserTime, KernelTime;
    MIME_STRING32 ImageName;
    LONG BasePriority;
    ULONG UniqueProcessId, InheritedFromUniqueProcessId, HandleCount, SessionId, UniqueProcessKey;
    ULONG PeakVirtualSize, VirtualSize, PageFaultCount, PeakWorkingSetSize, WorkingSetSize;
    ULONG QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
    ULONG PagefileUsage, PeakPagefileUsage, PrivatePageCount;
    LARGE_INTEGER IoCounters[6];
} PROCESS_INFORMATION32;

static_assert(sizeof(THREAD_INFORMATION32) == 0x40, "x86 thread record");
static_assert(sizeof(PROCESS_INFORMATION32) == 0xB8, "x86 process record");

/*
 * system handles in the x86 layout: a count and 16-byte records, or two words and 28-byte records. as many are
 * returned as fit the caller's buffer, and the native query gets room for the same number.
 */
static NTSTATUS query_handles32(BOOLEAN extended, const ULONG *a)
{
    ULONG length = a[2];
    ULONG header32 = extended ? 8 : 4, entry32 = extended ? 28 : 16;
    ULONG header64 = extended ? 16 : 8, entry64 = extended ? 40 : 24;
    if (length < header32) {
        store32(a[3], header32);
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    ULONG fit = (length - header32) / entry32;
    SIZE_T size = (SIZE_T)header64 + (SIZE_T)fit * entry64;
    PUCHAR native = Mx32Scratch(size);
    if (!native)
        return STATUS_NO_MEMORY;
    ULONG needed = 0;
    NTSTATUS status = NtQuerySystemInformation(extended ? SystemExtendedHandleInformation : SystemHandleInformation,
                                               native, (ULONG)size, &needed);
    if (NT_SUCCESS(status)) {
        ULONG count = extended ? (ULONG)*(PULONG_PTR)native : *(PULONG)native;
        PUCHAR out = pointer32(a[1]);
        memset(out, 0, header32);
        *(PULONG)out = count;
        for (ULONG i = 0; i < count; i++) {
            const UCHAR *in = native + header64 + (SIZE_T)i * entry64;
            PUCHAR o = out + header32 + (SIZE_T)i * entry32;
            if (extended) {
                *(PULONG)(o + 0) = (ULONG)*(PULONG_PTR)(in + 0);
                *(PULONG)(o + 4) = (ULONG)*(PULONG_PTR)(in + 8);
                *(PULONG)(o + 8) = (ULONG)*(PULONG_PTR)(in + 16);
                memcpy(o + 12, in + 24, 16);
            } else {
                memcpy(o, in, 8);
                *(PULONG)(o + 8) = (ULONG)*(PULONG_PTR)(in + 8);
                *(PULONG)(o + 12) = *(PULONG)(in + 16);
            }
        }
        store32(a[3], header32 + count * entry32);
    } else if (status == STATUS_INFO_LENGTH_MISMATCH && needed > header64) {
        store32(a[3], header32 + (needed - header64) / entry64 * entry32);
    }
    Mx32FreeScratch(native);
    return status;
}

/* converts the native process list to the x86 layout, threads and names included. */
static NTSTATUS query_processes32(ULONG out, ULONG length, ULONG returned)
{
    SIZE_T size = 0x10000;
    PVOID native;
    NTSTATUS status;
    for (;;) {
        native = Mx32Scratch(size);
        if (!native)
            return STATUS_NO_MEMORY;
        ULONG need = 0;
        status = NtQuerySystemInformation(SystemProcessInformation, native, (ULONG)size, &need);
        if (status != STATUS_INFO_LENGTH_MISMATCH || size >= 0x1000000)
            break;
        Mx32FreeScratch(native);
        size = need > size ? need + 0x1000 : size * 2;
    }
    ULONG total = 0;
    if (NT_SUCCESS(status)) {
        /* the first pass sizes, the second writes when the caller's buffer holds everything. */
        for (int pass = 0; pass < 2 && NT_SUCCESS(status); pass++) {
            ULONG used = 0;
            PROCESS_INFORMATION32 *last = nullptr;
            for (SYSTEM_PROCESS_INFORMATION *p = native;;
                 p = (SYSTEM_PROCESS_INFORMATION *)((PUCHAR)p + p->NextEntryOffset)) {
                ULONG name = p->ImageName.Length ? p->ImageName.Length + sizeof(WCHAR) : 0;
                ULONG record = (sizeof(PROCESS_INFORMATION32) + p->NumberOfThreads * sizeof(THREAD_INFORMATION32) +
                                name + 7) & ~7u;
                if (pass) {
                    PROCESS_INFORMATION32 *q = pointer32(out + used);
                    memset(q, 0, record);
                    q->NumberOfThreads = p->NumberOfThreads;
                    q->NumberOfThreadsHighWatermark = p->NumberOfThreadsHighWatermark;
                    q->CreateTime = p->CreateTime;
                    q->BasePriority = p->BasePriority;
                    q->UniqueProcessId = (ULONG)(ULONG_PTR)p->UniqueProcessId;
                    q->InheritedFromUniqueProcessId = (ULONG)(ULONG_PTR)p->InheritedFromUniqueProcessId;
                    q->HandleCount = p->HandleCount;
                    q->SessionId = p->SessionId;
                    q->UniqueProcessKey = (ULONG)p->UniqueProcessKey;
                    SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)(p + 1);
                    THREAD_INFORMATION32 *u = (THREAD_INFORMATION32 *)(q + 1);
                    for (ULONG i = 0; i < p->NumberOfThreads; i++, t++, u++) {
                        u->CreateTime = t->CreateTime;
                        u->StartAddress = (ULONG)(ULONG_PTR)t->StartAddress;
                        u->UniqueProcess = (ULONG)(ULONG_PTR)t->ClientId.UniqueProcess;
                        u->UniqueThread = (ULONG)(ULONG_PTR)t->ClientId.UniqueThread;
                        u->Priority = t->Priority;
                        u->BasePriority = t->BasePriority;
                        u->ThreadState = t->ThreadState;
                        u->WaitReason = t->WaitReason;
                    }
                    if (name) {
                        memcpy(u, p->ImageName.Buffer, p->ImageName.Length);
                        q->ImageName.Length = p->ImageName.Length;
                        q->ImageName.MaximumLength = (USHORT)name;
                        q->ImageName.Buffer = out + used + (ULONG)((PUCHAR)u - (PUCHAR)q);
                    }
                    if (last)
                        last->NextEntryOffset = (ULONG)((PUCHAR)q - (PUCHAR)last);
                    last = q;
                }
                used += record;
                if (!p->NextEntryOffset)
                    break;
            }
            total = used;
            if (!pass && total > length)
                status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }
    Mx32FreeScratch(native);
    if (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)
        store32(returned, total);
    return status;
}

/*
 * kernel modules in the 32-bit layout: a count, then 284-byte records with 32-bit pointers and base addresses cut
 * to the low half. the layout windows gives 32-bit programs here is a guess.
 */
static NTSTATUS query_modules32(ULONG address, ULONG length, ULONG returned_address)
{
    typedef struct {
        ULONG Section, MappedBase, ImageBase, ImageSize, Flags;
        USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
        UCHAR FullPathName[256];
    } MODULE32;
    UCHAR native[8 + 32 * 296];
    ULONG needed = 0;
    NTSTATUS status = NtQuerySystemInformation(SystemModuleInformation, native, sizeof(native), &needed);
    if (!NT_SUCCESS(status) && status != STATUS_INFO_LENGTH_MISMATCH)
        return status;
    ULONG count = *(PULONG)native, fit = count < 32 ? count : 32;
    ULONG size = 4 + count * sizeof(MODULE32);
    store32(returned_address, size);
    if (length < 4)
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG room = (length - 4) / sizeof(MODULE32);
    if (room > fit)
        room = fit;
    PUCHAR out = pointer32(address);
    *(PULONG)out = count;
    for (ULONG i = 0; i < room; i++) {
        PUCHAR from = native + 8 + i * 296;
        MODULE32 m = {};
        m.ImageBase = (ULONG)*(PULONG64)(from + 16);
        m.ImageSize = *(PULONG)(from + 24);
        m.Flags = *(PULONG)(from + 28);
        memcpy(&m.LoadOrderIndex, from + 32, 8);
        memcpy(m.FullPathName, from + 40, sizeof(m.FullPathName));
        memcpy(out + 4 + i * sizeof(MODULE32), &m, sizeof(m));
    }
    return room < count ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
}

NTSTATUS Mx32QuerySystemInformation(const ULONG *a)
{
    ULONG klass = a[0], length = a[2];
    PVOID out = pointer32(a[1]);
    NTSTATUS status;
    switch (klass) {
    case SystemBasicInformation:
    case SystemEmulationBasicInformation: {
        /* a 32-bit program's address space ends below 2 GiB. */
        store32(a[3], 44);
        if (length < 44)
            return STATUS_INFO_LENGTH_MISMATCH;
        SYSTEM_BASIC_INFORMATION info;
        status = NtQuerySystemInformation(SystemBasicInformation, &info, sizeof(info), nullptr);
        if (NT_SUCCESS(status)) {
            ULONG o[11] = {info.Reserved, info.TimerResolution, info.PageSize, info.NumberOfPhysicalPages,
                           info.LowestPhysicalPageNumber, info.HighestPhysicalPageNumber, info.AllocationGranularity,
                           0x10000, 0x7FFEFFFF, (ULONG)info.ActiveProcessorsAffinityMask,
                           (UCHAR)info.NumberOfProcessors};
            memcpy(out, o, sizeof(o));
        }
        return status;
    }
    case SystemPerformanceInformation: {
        /* newer systems made this record longer, a 32-bit program may ask for its first 0x138 bytes. */
        UCHAR native[0x200];
        ULONG needed = 0;
        status = NtQuerySystemInformation(klass, native, sizeof(native), &needed);
        if (NT_SUCCESS(status) && length < 0x138) {
            store32(a[3], needed);
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        if (NT_SUCCESS(status)) {
            ULONG n = length < needed ? length : needed;
            memcpy(out, native, n);
            store32(a[3], n);
        }
        return status;
    }
    case SystemProcessInformation:
        return query_processes32(a[1], length, a[3]);
    case SystemHandleInformation:
    case SystemExtendedHandleInformation:
        return query_handles32(klass == SystemExtendedHandleInformation, a);
    case SystemRangeStartInformation:
        /* a 32-bit program's user addresses end at 2 GiB. */
        store32(a[3], sizeof(ULONG));
        if (length != sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        *(PULONG)out = 0x80000000u;
        return STATUS_SUCCESS;
    case SystemModuleInformation:
        return query_modules32(a[1], length, a[3]);
    case SystemProcessorInformation:
    case SystemPageFileInformation:
    case SystemTimeOfDayInformation:
    case SystemProcessorPerformanceInformation:
    case SystemKernelDebuggerInformation:
    case SystemNumaHighestNodeInformation:
    case SystemEmulationProcessorInformation:
    case SystemFirmwareTableInformation:
    case SystemBootEnvironmentInformation:
    case SystemHypervisorDetailInformation:
    case 164:
        return NtQuerySystemInformation(klass, out, length, pointer32(a[3]));
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

/* ------------------------------------------------------------ objects and sections */

NTSTATUS Mx32QueryObject(const ULONG *a)
{
    HANDLE handle = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    switch (klass) {
    case ObjectBasicInformation: {
        /* 56 bytes in both widths, no pointer. */
        UCHAR native[56];
        ULONG needed = 0;
        NTSTATUS status = NtQueryObject(handle, klass, native, sizeof(native), &needed);
        if (NT_SUCCESS(status) && length < sizeof(native))
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status))
            memcpy(pointer32(a[2]), native, sizeof(native));
        store32(a[4], NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH ? sizeof(native) : needed);
        return status;
    }
    case ObjectNameInformation:
        return query_string32(query_object_string, a);
    case ObjectTypeInformation: {
        /* the name header is 8 bytes shorter, the 88 bytes of counts and masks after it match. */
        if (length > 0xFFFF0000u)
            return STATUS_INVALID_PARAMETER;
        PUCHAR native = Mx32Scratch((SIZE_T)length + 8);
        if (!native)
            return STATUS_NO_MEMORY;
        ULONG needed = 0;
        NTSTATUS status = NtQueryObject(handle, klass, native, length + 8, &needed);
        if (NT_SUCCESS(status)) {
            const UNICODE_STRING *name = (const UNICODE_STRING *)native;
            PUCHAR out = pointer32(a[2]);
            *(MIME_STRING32 *)out = (MIME_STRING32){name->Length, name->MaximumLength, (ULONG)(ULONG_PTR)(out + 0x60)};
            memcpy(out + 8, native + 16, 88);
            memcpy(out + 0x60, native + 0x68, name->MaximumLength);
        }
        store32(a[4], needed > 8 ? needed - 8 : needed);
        Mx32FreeScratch(native);
        return status;
    }
    case ObjectHandleFlagInformation:
        return NtQueryObject(handle, klass, pointer32(a[2]), length, pointer32(a[4]));
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS Mx32SetInformationObject(const ULONG *a)
{
    if (a[1] != ObjectHandleFlagInformation && a[1] != ObjectSessionInformation)
        return STATUS_INVALID_INFO_CLASS;
    return NtSetInformationObject(handle32(a[0]), a[1], pointer32(a[2]), a[3]);
}

typedef struct {
    PVOID BaseAddress;
    ULONG AllocationAttributes;
    LARGE_INTEGER MaximumSize;
} SECTION_BASIC64;

typedef struct {
    PVOID TransferAddress;
    ULONG ZeroBits;
    SIZE_T MaximumStackSize, CommittedStackSize;
    ULONG SubSystemType, SubSystemVersion, OperatingSystemVersion;
    USHORT ImageCharacteristics, DllCharacteristics, Machine;
    BOOLEAN ImageContainsCode;
    UCHAR ImageFlags;
    ULONG LoaderFlags, ImageFileSize, CheckSum;
} SECTION_IMAGE64;

NTSTATUS Mx32QuerySection(const ULONG *a)
{
    ULONG klass = a[1], length = a[3];
    PULONG out = pointer32(a[2]);
    union {
        SECTION_BASIC64 basic;
        SECTION_IMAGE64 image;
    } native;
    SIZE_T needed = 0;
    ULONG size32;
    if (klass == 0)
        size32 = 16;
    else if (klass == 1)
        size32 = 48;
    else
        return STATUS_INVALID_INFO_CLASS;
    NTSTATUS status = NtQuerySection(handle32(a[0]), klass, &native, klass ? sizeof(native.image)
                                                                           : sizeof(native.basic), &needed);
    if (NT_SUCCESS(status) && length < size32)
        status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(status) && klass == 0) {
        out[0] = (ULONG)(ULONG_PTR)native.basic.BaseAddress;
        out[1] = native.basic.AllocationAttributes;
        memcpy(out + 2, &native.basic.MaximumSize, 8);
    } else if (NT_SUCCESS(status)) {
        const SECTION_IMAGE64 *i = &native.image;
        out[0] = (ULONG)(ULONG_PTR)i->TransferAddress;
        out[1] = i->ZeroBits;
        out[2] = saturate32(i->MaximumStackSize);
        out[3] = saturate32(i->CommittedStackSize);
        out[4] = i->SubSystemType;
        out[5] = i->SubSystemVersion;
        out[6] = i->OperatingSystemVersion;
        memcpy(out + 7, &i->ImageCharacteristics, 8);
        out[9] = i->LoaderFlags;
        out[10] = i->ImageFileSize;
        out[11] = i->CheckSum;
    }
    if (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)
        store32(a[4], size32);
    return status;
}

/* ------------------------------------------------------------ jobs */

typedef struct {
    LARGE_INTEGER PerProcessUserTimeLimit, PerJobUserTimeLimit;
    ULONG LimitFlags;
    SIZE_T MinimumWorkingSetSize, MaximumWorkingSetSize;
    ULONG ActiveProcessLimit;
    ULONG_PTR Affinity;
    ULONG PriorityClass, SchedulingClass;
} JOB_BASIC_LIMIT64;

typedef struct {
    JOB_BASIC_LIMIT64 Basic;
    ULONG64 IoCounters[6];
    SIZE_T ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
} JOB_EXTENDED_LIMIT64;

/* the 48-byte JOBOBJECT_BASIC_LIMIT_INFORMATION of a 32-bit program. */
static void basic_limit32(const JOB_BASIC_LIMIT64 *in, PULONG out)
{
    memcpy(out, &in->PerProcessUserTimeLimit, 16);
    out[4] = in->LimitFlags;
    out[5] = saturate32(in->MinimumWorkingSetSize);
    out[6] = saturate32(in->MaximumWorkingSetSize);
    out[7] = in->ActiveProcessLimit;
    out[8] = (ULONG)in->Affinity | (ULONG)((ULONG64)in->Affinity >> 32);
    out[9] = in->PriorityClass;
    out[10] = in->SchedulingClass;
    out[11] = 0;
}

static void basic_limit64(const ULONG *in, JOB_BASIC_LIMIT64 *out)
{
    memcpy(&out->PerProcessUserTimeLimit, in, 16);
    out->LimitFlags = in[4];
    out->MinimumWorkingSetSize = in[5];
    out->MaximumWorkingSetSize = in[6];
    out->ActiveProcessLimit = in[7];
    out->Affinity = in[8];
    out->PriorityClass = in[9];
    out->SchedulingClass = in[10];
}

NTSTATUS Mx32QueryInformationJobObject(const ULONG *a)
{
    HANDLE job = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    PULONG out = pointer32(a[2]);
    NTSTATUS status;
    switch (klass) {
    case JobObjectBasicLimitInformation:
    case JobObjectExtendedLimitInformation: {
        BOOLEAN extended = klass == JobObjectExtendedLimitInformation;
        ULONG size32 = extended ? 112 : 48;
        JOB_EXTENDED_LIMIT64 native;
        status = NtQueryInformationJobObject(job, klass, &native,
                                             extended ? sizeof(native) : sizeof(native.Basic), nullptr);
        if (NT_SUCCESS(status) && length < size32)
            status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(status)) {
            basic_limit32(&native.Basic, out);
            if (extended) {
                memcpy(out + 12, native.IoCounters, sizeof(native.IoCounters));
                out[24] = saturate32(native.ProcessMemoryLimit);
                out[25] = saturate32(native.JobMemoryLimit);
                out[26] = saturate32(native.PeakProcessMemoryUsed);
                out[27] = saturate32(native.PeakJobMemoryUsed);
            }
        }
        if (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)
            store32(a[4], size32);
        return status;
    }
    case JobObjectBasicProcessIdList: {
        /* the counts, then 4-byte ids. */
        if (length < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG room = (length - 8) / sizeof(ULONG);
        SIZE_T size = 8 + (SIZE_T)room * sizeof(ULONG_PTR);
        PULONG native = Mx32Scratch(size);
        if (!native)
            return STATUS_NO_MEMORY;
        ULONG needed = 0;
        status = NtQueryInformationJobObject(job, klass, native, (ULONG)size, &needed);
        if (NT_SUCCESS(status)) {
            const ULONG_PTR *ids = (const ULONG_PTR *)(native + 2);
            out[0] = native[0];
            out[1] = native[1];
            for (ULONG i = 0; i < native[1]; i++)
                out[2 + i] = (ULONG)ids[i];
            store32(a[4], 8 + native[1] * sizeof(ULONG));
        }
        Mx32FreeScratch(native);
        return status;
    }
    case JobObjectBasicAccountingInformation:
    case JobObjectBasicUIRestrictions:
    case JobObjectBasicAndIoAccountingInformation:
        return NtQueryInformationJobObject(job, klass, out, length, pointer32(a[4]));
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS Mx32SetInformationJobObject(const ULONG *a)
{
    HANDLE job = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    const ULONG *in = pointer32(a[2]);
    switch (klass) {
    case JobObjectBasicLimitInformation:
    case JobObjectExtendedLimitInformation: {
        BOOLEAN extended = klass == JobObjectExtendedLimitInformation;
        if (length < (extended ? 112u : 48u))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        JOB_EXTENDED_LIMIT64 native = {};
        basic_limit64(in, &native.Basic);
        if (extended) {
            memcpy(native.IoCounters, in + 12, sizeof(native.IoCounters));
            native.ProcessMemoryLimit = in[24];
            native.JobMemoryLimit = in[25];
            native.PeakProcessMemoryUsed = in[26];
            native.PeakJobMemoryUsed = in[27];
        }
        return NtSetInformationJobObject(job, klass, &native, extended ? sizeof(native) : sizeof(native.Basic));
    }
    case JobObjectAssociateCompletionPortInformation: {
        /* the key, then the port. */
        if (length < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        PVOID port[2] = {pointer32(in[0]), handle32(in[1])};
        return NtSetInformationJobObject(job, klass, port, sizeof(port));
    }
    default:
        return NtSetInformationJobObject(job, klass, (PVOID)in, length);
    }
}

/* ------------------------------------------------------------ tokens */

static ULONG sid_bytes(const void *sid)
{
    return 8 + 4 * ((const UCHAR *)sid)[1];
}

/* a kernel token record, kept until the caller frees it. */
typedef struct NATIVE_TOKEN_INFO {
    PUCHAR Data;
    ULONG Length;
    PVOID Scratch;
    UCHAR Local[2048];
} NATIVE_TOKEN_INFO;

static NTSTATUS query_native(HANDLE token, ULONG klass, NATIVE_TOKEN_INFO *n)
{
    ULONG needed = 0;
    n->Scratch = nullptr;
    n->Data = n->Local;
    /* a linked token must be asked for with exactly the size of its handle. */
    NTSTATUS status = NtQueryInformationToken(token, klass, n->Local, klass == 19 ? 8 : sizeof(n->Local), &needed);
    if (status == STATUS_BUFFER_TOO_SMALL && needed > sizeof(n->Local)) {
        n->Scratch = n->Data = Mx32Scratch(needed);
        if (!n->Data)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = NtQueryInformationToken(token, klass, n->Data, needed, &needed);
    }
    n->Length = needed;
    return status;
}

/* the 32-bit form of a record, written to the caller's buffer when it fits. its size is always reported. */
typedef struct OUT32 {
    ULONG Address;  /* the caller's buffer. */
    ULONG Capacity;
    ULONG Size;
    PUCHAR Data;    /* NULL while measuring. */
} OUT32;

static ULONG out32_alloc(OUT32 *o, ULONG bytes)
{
    ULONG at = o->Size;
    o->Size += bytes;
    if (o->Data)
        memset(o->Data + at, 0, bytes);
    return at;
}

static void out32_write(OUT32 *o, ULONG at, const void *source, ULONG bytes)
{
    if (o->Data)
        memcpy(o->Data + at, source, bytes);
}

static void out32_ulong(OUT32 *o, ULONG at, ULONG value)
{
    out32_write(o, at, &value, sizeof(value));
}

/* writes a sid entry, pointer then attributes, at the given offset. */
static void out32_sid_entry(OUT32 *o, ULONG entry, const void *sid, ULONG attributes)
{
    ULONG bytes = sid_bytes(sid);
    ULONG at = out32_alloc(o, bytes);
    out32_write(o, at, sid, bytes);
    out32_ulong(o, entry, o->Address + at);
    out32_ulong(o, entry + 4, attributes);
}

typedef struct NATIVE_ENTRY {
    PVOID Sid;
    ULONG Attributes;
} NATIVE_ENTRY;

/* TOKEN_GROUPS: a count, then entries. */
static void out32_groups(OUT32 *o, const UCHAR *native)
{
    ULONG count = *(const ULONG *)native;
    const NATIVE_ENTRY *e = (const void *)(native + 8);
    ULONG head = out32_alloc(o, 4 + 8 * count);
    out32_ulong(o, head, count);
    for (ULONG i = 0; i < count; i++)
        out32_sid_entry(o, head + 4 + 8 * i, e[i].Sid, e[i].Attributes);
}

static void out32_sid_pointer(OUT32 *o, PVOID sid)
{
    ULONG head = out32_alloc(o, 4);
    if (sid) {
        ULONG bytes = sid_bytes(sid);
        ULONG at = out32_alloc(o, bytes);
        out32_write(o, at, sid, bytes);
        out32_ulong(o, head, o->Address + at);
    }
}

/* narrows the kernel records that hold pointers or handles. */
static NTSTATUS narrow_token_record(ULONG klass, const NATIVE_TOKEN_INFO *n, OUT32 *o)
{
    const UCHAR *d = n->Data;
    switch (klass) {
    case 1:  /* TokenUser */
    case 25: /* TokenIntegrityLevel */ {
        const NATIVE_ENTRY *e = (const void *)d;
        ULONG head = out32_alloc(o, 8);
        out32_sid_entry(o, head, e->Sid, e->Attributes);
        return STATUS_SUCCESS;
    }
    case 4:  /* TokenOwner */
    case 5:  /* TokenPrimaryGroup */
    case 31: /* TokenAppContainerSid */
        out32_sid_pointer(o, *(PVOID const *)d);
        return STATUS_SUCCESS;
    case 6: { /* TokenDefaultDacl */
        ULONG head = out32_alloc(o, 4);
        const UCHAR *acl = *(const UCHAR *const *)d;
        if (acl) {
            ULONG size = *(const USHORT *)(acl + 2);
            ULONG at = out32_alloc(o, size);
            out32_write(o, at, acl, size);
            out32_ulong(o, head, o->Address + at);
        }
        return STATUS_SUCCESS;
    }
    case 2:  /* TokenGroups */
    case 11: /* TokenRestrictedSids */
    case 28: /* TokenLogonSid */
    case 30: /* TokenCapabilities */
        out32_groups(o, d);
        return STATUS_SUCCESS;
    case 13: { /* TokenGroupsAndPrivileges */
        const struct {
            ULONG SidCount, SidLength;
            const NATIVE_ENTRY *Sids;
            ULONG RestrictedCount, RestrictedLength;
            const NATIVE_ENTRY *Restricted;
            ULONG PrivilegeCount, PrivilegeLength;
            const ULONG *Privileges;
            ULONG64 AuthenticationId;
        } *g = (const void *)d;
        ULONG head = out32_alloc(o, 44);
        ULONG sids = out32_alloc(o, 8 * g->SidCount);
        ULONG restricted = out32_alloc(o, 8 * g->RestrictedCount);
        ULONG privileges = out32_alloc(o, 12 * g->PrivilegeCount);
        out32_ulong(o, head, g->SidCount);
        out32_ulong(o, head + 4, 8 * g->SidCount);
        out32_ulong(o, head + 8, o->Address + sids);
        out32_ulong(o, head + 12, g->RestrictedCount);
        out32_ulong(o, head + 16, 8 * g->RestrictedCount);
        out32_ulong(o, head + 20, g->RestrictedCount ? o->Address + restricted : 0);
        out32_ulong(o, head + 24, g->PrivilegeCount);
        out32_ulong(o, head + 28, 12 * g->PrivilegeCount);
        out32_ulong(o, head + 32, o->Address + privileges);
        out32_write(o, head + 36, &g->AuthenticationId, 8);
        for (ULONG i = 0; i < g->SidCount; i++)
            out32_sid_entry(o, sids + 8 * i, g->Sids[i].Sid, g->Sids[i].Attributes);
        for (ULONG i = 0; i < g->RestrictedCount; i++)
            out32_sid_entry(o, restricted + 8 * i, g->Restricted[i].Sid, g->Restricted[i].Attributes);
        out32_write(o, privileges, g->Privileges, 12 * g->PrivilegeCount);
        return STATUS_SUCCESS;
    }
    case 19: { /* TokenLinkedToken */
        ULONG at = out32_alloc(o, 4);
        out32_ulong(o, at, (ULONG)(ULONG_PTR)*(const HANDLE *)d);
        return STATUS_SUCCESS;
    }
    case 33: /* the claims. */
    case 34:
    case 39: {
        ULONG at = out32_alloc(o, 12);
        out32_ulong(o, at, 1);
        return STATUS_SUCCESS;
    }
    case 37: /* TokenDeviceGroups */
        out32_alloc(o, 12);
        return STATUS_SUCCESS;
    case 41: /* TokenProcessTrustLevel */
        out32_alloc(o, 4);
        return STATUS_SUCCESS;
    case 44: /* TokenBnoIsolation */
        out32_alloc(o, 8);
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

/* classes whose records match in both widths. */
static BOOLEAN token_same(ULONG klass)
{
    static const UCHAR same[] = {3, 7, 8, 9, 10, 12, 15, 16, 17, 18, 20, 21, 23, 24, 26, 27, 29, 32, 40, 47, 48};
    return listed(same, sizeof(same), klass);
}

NTSTATUS Mx32QueryInformationToken(const ULONG *a)
{
    HANDLE token = handle32(a[0]);
    ULONG klass = a[1];
    if (!a[4])
        return STATUS_ACCESS_VIOLATION;
    if (token_same(klass))
        return NtQueryInformationToken(token, klass, pointer32(a[2]), a[3], pointer32(a[4]));
    /* queried in native form, then narrowed. */
    NATIVE_TOKEN_INFO *n = Mx32Scratch(sizeof(*n));
    if (!n)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = query_native(token, klass, n);
    if (NT_SUCCESS(status)) {
        OUT32 measure = {a[2], a[3], 0, nullptr};
        status = narrow_token_record(klass, n, &measure);
        if (NT_SUCCESS(status)) {
            store32(a[4], measure.Size);
            if (a[3] < measure.Size) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                OUT32 fill = {a[2], a[3], 0, pointer32(a[2])};
                narrow_token_record(klass, n, &fill);
            }
        }
    } else if (status == STATUS_BUFFER_TOO_SMALL || status == STATUS_INFO_LENGTH_MISMATCH) {
        store32(a[4], n->Length);
    }
    Mx32FreeScratch(n->Scratch);
    Mx32FreeScratch(n);
    return status;
}

/* a 32-bit program's TOKEN_GROUPS widened for the kernel, freed with Mx32FreeScratch. */
static NTSTATUS widen_groups(ULONG address, PVOID *native)
{
    *native = nullptr;
    const ULONG *in = pointer32(address);
    if (!in)
        return STATUS_SUCCESS;
    ULONG count = in[0];
    if (count > 0x1000)
        return STATUS_INVALID_PARAMETER;
    PULONG64 out = Mx32Scratch(8 + 16 * (SIZE_T)count);
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    *(PULONG)out = count;
    for (ULONG i = 0; i < count; i++) {
        out[1 + 2 * i] = in[1 + 2 * i];
        out[2 + 2 * i] = in[2 + 2 * i];
    }
    *native = out;
    return STATUS_SUCCESS;
}

NTSTATUS Mx32SetInformationToken(const ULONG *a)
{
    HANDLE token = handle32(a[0]);
    ULONG klass = a[1], length = a[3];
    const ULONG *in = pointer32(a[2]);
    switch (klass) {
    case 4: /* TokenOwner */
    case 5: /* TokenPrimaryGroup */
    case 6: { /* TokenDefaultDacl */
        if (length < sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        PVOID pointer = pointer32(in[0]);
        return NtSetInformationToken(token, klass, &pointer, sizeof(pointer));
    }
    case 25: { /* TokenIntegrityLevel */
        if (length < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        struct {
            PVOID Sid;
            ULONG Attributes, Pad;
        } label = {pointer32(in[0]), in[1], 0};
        return NtSetInformationToken(token, klass, &label, sizeof(label));
    }
    case 19: { /* TokenLinkedToken */
        if (length != sizeof(ULONG))
            return STATUS_INFO_LENGTH_MISMATCH;
        if (!in)
            return STATUS_ACCESS_VIOLATION;
        HANDLE linked = handle32(in[0]);
        return NtSetInformationToken(token, klass, &linked, sizeof(linked));
    }
    default:
        return NtSetInformationToken(token, klass, (PVOID)in, length);
    }
}

NTSTATUS Mx32FilterToken(const ULONG *a)
{
    PVOID disable = nullptr, restricted = nullptr;
    NTSTATUS status = widen_groups(a[2], &disable);
    if (NT_SUCCESS(status))
        status = widen_groups(a[4], &restricted);
    if (NT_SUCCESS(status)) {
        HANDLE created = nullptr;
        status = NtFilterToken(handle32(a[0]), a[1], disable, pointer32(a[3]), restricted, &created);
        if (NT_SUCCESS(status))
            store32(a[5], (ULONG_PTR)created);
    }
    Mx32FreeScratch(disable);
    Mx32FreeScratch(restricted);
    return status;
}

NTSTATUS Mx32AdjustGroupsToken(const ULONG *a)
{
    PVOID state;
    NTSTATUS status = widen_groups(a[2], &state);
    if (!NT_SUCCESS(status))
        return status;
    PVOID previous_native = nullptr;
    ULONG native_length = 0;
    if (a[4]) {
        /* the previous groups come back in the kernel's form and are narrowed. */
        native_length = a[3] * 2 + 64;
        previous_native = Mx32Scratch(native_length);
        if (!previous_native) {
            Mx32FreeScratch(state);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    ULONG needed = 0;
    status = NtAdjustGroupsToken(handle32(a[0]), (BOOLEAN)a[1], state, native_length, previous_native, &needed);
    if (a[4] && (NT_SUCCESS(status) || status == STATUS_NOT_ALL_ASSIGNED)) {
        NATIVE_TOKEN_INFO *n = Mx32Scratch(sizeof(*n));
        OUT32 measure = {a[4], a[3], 0, nullptr};
        if (n) {
            n->Data = previous_native;
            narrow_token_record(2, n, &measure);
            if (a[5])
                store32(a[5], measure.Size);
            if (a[3] < measure.Size) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                OUT32 fill = {a[4], a[3], 0, pointer32(a[4])};
                narrow_token_record(2, n, &fill);
            }
            Mx32FreeScratch(n);
        }
    } else if (a[5]) {
        store32(a[5], needed);
    }
    Mx32FreeScratch(previous_native);
    Mx32FreeScratch(state);
    return status;
}

/* ------------------------------------------------------------ memory */

/* 32-bit layouts of NtQueryVirtualMemory results. memory above the 32-bit space is not the caller's. */
NTSTATUS Mx32QueryVirtualMemory(const ULONG *a)
{
    PVOID base = pointer32(a[1]);
    ULONG length = a[4];
    PVOID out = pointer32(a[3]);
    NTSTATUS status;
    switch (a[2]) {
    case MemoryBasicInformation: {
        if (length < 28)
            return STATUS_INFO_LENGTH_MISMATCH;
        MEMORY_BASIC_INFORMATION m;
        status = NtQueryVirtualMemory(handle32(a[0]), base, MemoryBasicInformation, &m, sizeof(m), nullptr);
        if (!NT_SUCCESS(status))
            return status;
        ULONG_PTR end = (ULONG_PTR)m.BaseAddress + m.RegionSize;
        if ((ULONG_PTR)m.BaseAddress >= 0x100000000ull)
            return STATUS_INVALID_PARAMETER;
        if (end > 0x100000000ull)
            end = 0x100000000ull;
        ULONG *o = out;
        o[0] = (ULONG)(ULONG_PTR)m.BaseAddress;
        o[1] = (ULONG)(ULONG_PTR)m.AllocationBase;
        o[2] = m.AllocationProtect;
        o[3] = (ULONG)(end - (ULONG_PTR)m.BaseAddress);
        o[4] = m.State;
        o[5] = m.Protect;
        o[6] = m.Type;
        store32(a[5], 28);
        return STATUS_SUCCESS;
    }
    case MemoryWorkingSetInformation: {
        /* a count, then 4-byte page and flags entries. pages above the 32-bit space are not the program's. */
        if (length < 4)
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG capacity = (length - 4) / 4;
        SIZE_T bytes = 8 + (SIZE_T)capacity * 8;
        ULONG_PTR *native = Mx32Scratch(bytes);
        if (!native)
            return STATUS_NO_MEMORY;
        status = NtQueryVirtualMemory(handle32(a[0]), nullptr, MemoryWorkingSetInformation, native, bytes, nullptr);
        ULONG *entries = out;
        if (NT_SUCCESS(status)) {
            ULONG kept = 0;
            for (ULONG_PTR i = 0; i < native[0]; i++)
                if (native[1 + i] < 0x100000000ull)
                    entries[1 + kept++] = (ULONG)native[1 + i];
            entries[0] = kept;
        } else if (status == STATUS_INFO_LENGTH_MISMATCH) {
            entries[0] = (ULONG)native[0];
        }
        Mx32FreeScratch(native);
        return status;
    }
    case MemoryWorkingSetExInformation: {
        /* address and attribute entries, 8 bytes here and 16 bytes natively. */
        ULONG count = length / 8;
        if (!count)
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG *entries = out;
        ULONG_PTR *native = Mx32Scratch((SIZE_T)count * 16);
        if (!native)
            return STATUS_NO_MEMORY;
        for (ULONG i = 0; i < count; i++) {
            native[i * 2] = entries[i * 2];
            native[i * 2 + 1] = 0;
        }
        status = NtQueryVirtualMemory(handle32(a[0]), nullptr, MemoryWorkingSetExInformation, native,
                                      (SIZE_T)count * 16, nullptr);
        if (NT_SUCCESS(status))
            for (ULONG i = 0; i < count; i++)
                entries[i * 2 + 1] = (ULONG)native[i * 2 + 1];
        Mx32FreeScratch(native);
        return status;
    }
    case MemoryImageInformation: {
        if (length < 16)
            return STATUS_INFO_LENGTH_MISMATCH;
        MEMORY_IMAGE_INFORMATION m;
        status = NtQueryVirtualMemory(handle32(a[0]), base, MemoryImageInformation, &m, sizeof(m), nullptr);
        if (!NT_SUCCESS(status))
            return status;
        ULONG *o = out;
        o[0] = (ULONG)(ULONG_PTR)m.ImageBase;
        o[1] = (ULONG)m.SizeOfImage;
        o[2] = m.ImageFlags;
        o[3] = m.Reserved;
        store32(a[5], 16);
        return STATUS_SUCCESS;
    }
    case MemoryMappedFilenameInformation: {
        /* the native string follows its header; the 32-bit one is 8 bytes shorter. */
        PVOID native = Mx32Scratch((SIZE_T)length + 8);
        if (!native)
            return STATUS_NO_MEMORY;
        SIZE_T needed = 0;
        status = NtQueryVirtualMemory(handle32(a[0]), base, MemoryMappedFilenameInformation, native,
                                      (SIZE_T)length + 8, &needed);
        if (NT_SUCCESS(status)) {
            UNICODE_STRING *name = native;
            if (sizeof(MIME_STRING32) + name->MaximumLength > length) {
                status = STATUS_BUFFER_OVERFLOW;
            } else {
                narrow_string(name, out);
                store32(a[5], sizeof(MIME_STRING32) + name->MaximumLength);
            }
        } else if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_INFO_LENGTH_MISMATCH) {
            store32(a[5], needed > 8 ? needed - 8 : needed);
        }
        Mx32FreeScratch(native);
        return status;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}
