/* wow64 process structures and system-call argument conversion. */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <mime/wow32.h>
#include <mime/afd.h>
#include "mime_svc_nt.h"
#include "x86.h"
#include "wow32_convert.h"

/* advapi32_private.h declares them for the dlls that use them. */
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, ULONG info, PVOID descriptor, ULONG length,
                                                  PULONG needed);
NTSYSCALLAPI NTSTATUS NTAPI NtAccessCheck(PVOID descriptor, HANDLE token, ACCESS_MASK desired, PVOID mapping, PVOID privileges,
                                          PULONG privileges_length, PACCESS_MASK granted, PNTSTATUS status);
NTSYSCALLAPI NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE handle, BOOLEAN disable_all, PVOID state, ULONG length,
                                                    PVOID previous, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateToken(HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                             BOOLEAN effective_only, ULONG type, PHANDLE result);

static PVOID allocate32(SIZE_T size)
{
    PVOID base = nullptr;
    NTSTATUS status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
                                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status))
        return nullptr;
    if ((ULONG_PTR)base + size > 0x80000000ull) {
        SIZE_T release = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &base, &release, MEM_RELEASE);
        return nullptr;
    }
    return base;
}

static MIME_STRING32 copy_string32(PUCHAR *cursor, const UNICODE_STRING *source)
{
    PWSTR text = (PWSTR)*cursor;
    memcpy(text, source->Buffer, source->Length);
    text[source->Length / sizeof(WCHAR)] = 0;
    *cursor += source->Length + sizeof(WCHAR);
    return (MIME_STRING32){source->Length, (USHORT)(source->Length + sizeof(WCHAR)), (ULONG)(ULONG_PTR)text};
}

/* fills a 32-bit peb from the native one, its process parameters and environment in memory below 2 GiB. */
BOOLEAN MxFillPeb32(PVOID memory)
{
    PPEB native = NtCurrentPeb();
    PRTL_USER_PROCESS_PARAMETERS pp = native->ProcessParameters;
    SIZE_T strings = pp->ImagePathName.Length + pp->CommandLine.Length + pp->CurrentDirectory.DosPath.Length +
                     pp->DllPath.Length + pp->WindowTitle.Length + pp->DesktopInfo.Length + pp->ShellInfo.Length +
                     pp->RuntimeData.Length + 32;
    SIZE_T size = sizeof(MIME_PARAMETERS32) + strings + pp->EnvironmentSize;
    MIME_PARAMETERS32 *out = allocate32(size);
    if (!out)
        return FALSE;
    MIME_PEB32 *peb = memory;
    PUCHAR cursor = (void *)(out + 1);
    peb->ImageBaseAddress = (ULONG)(ULONG_PTR)native->ImageBaseAddress;
    peb->ProcessParameters = (ULONG)(ULONG_PTR)out;
    peb->NumberOfProcessors = native->NumberOfProcessors;
    peb->OSMajorVersion = native->OSMajorVersion;
    peb->OSMinorVersion = native->OSMinorVersion;
    peb->OSBuildNumber = native->OSBuildNumber;
    peb->OSPlatformId = native->OSPlatformId;
    peb->ImageSubsystem = native->ImageSubsystem;
    peb->ImageSubsystemMajorVersion = native->ImageSubsystemMajorVersion;
    peb->ImageSubsystemMinorVersion = native->ImageSubsystemMinorVersion;
    peb->ActiveProcessAffinityMask = (ULONG)native->ActiveProcessAffinityMask;
    peb->CriticalSectionTimeout = native->CriticalSectionTimeout;
    peb->HeapSegmentReserve = (ULONG)native->HeapSegmentReserve;
    peb->HeapSegmentCommit = (ULONG)native->HeapSegmentCommit;
    peb->HeapDeCommitTotalFreeThreshold = (ULONG)native->HeapDeCommitTotalFreeThreshold;
    peb->HeapDeCommitFreeBlockThreshold = (ULONG)native->HeapDeCommitFreeBlockThreshold;
    peb->MaximumNumberOfHeaps = native->MaximumNumberOfHeaps;
    peb->SessionId = native->SessionId;
    out->MaximumLength = out->Length = sizeof(*out);
    out->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    out->ConsoleHandle = (ULONG)(ULONG_PTR)pp->ConsoleHandle;
    out->StandardInput = (ULONG)(ULONG_PTR)pp->StandardInput;
    out->StandardOutput = (ULONG)(ULONG_PTR)pp->StandardOutput;
    out->StandardError = (ULONG)(ULONG_PTR)pp->StandardError;
    out->ShowWindowFlags = pp->ShowWindowFlags;
    out->ImagePathName = copy_string32(&cursor, &pp->ImagePathName);
    out->CommandLine = copy_string32(&cursor, &pp->CommandLine);
    out->CurrentDirectory = copy_string32(&cursor, &pp->CurrentDirectory.DosPath);
    out->DllPath = copy_string32(&cursor, &pp->DllPath);
    out->WindowTitle = copy_string32(&cursor, &pp->WindowTitle);
    out->DesktopInfo = copy_string32(&cursor, &pp->DesktopInfo);
    out->ShellInfo = copy_string32(&cursor, &pp->ShellInfo);
    out->RuntimeData = copy_string32(&cursor, &pp->RuntimeData);
    out->Environment = (ULONG)(ULONG_PTR)cursor;
    out->EnvironmentSize = (ULONG)pp->EnvironmentSize;
    memcpy(cursor, pp->Environment, pp->EnvironmentSize);
    return TRUE;
}

PVOID MxCreatePeb32(void)
{
    PVOID peb = allocate32(0x1000);
    return peb && MxFillPeb32(peb) ? peb : nullptr;
}

PVOID MxCreateTeb32(PVOID peb)
{
    PULONG teb = allocate32(MIME_TEB_SIZE);
    if (!teb)
        return nullptr;
    PTEB native = NtCurrentTeb();
    teb[0] = 0xFFFFFFFFu;
    teb[1] = (ULONG)(ULONG_PTR)native->NtTib.StackBase;
    teb[2] = (ULONG)(ULONG_PTR)native->NtTib.StackLimit;
    teb[MIME_TEB32_SELF / 4] = (ULONG)(ULONG_PTR)teb;
    teb[MIME_TEB32_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)native->ClientId.UniqueProcess;
    teb[MIME_TEB32_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)native->ClientId.UniqueThread;
    teb[MIME_TEB32_REAL_CLIENT_ID / 4] = (ULONG)(ULONG_PTR)native->ClientId.UniqueProcess;
    teb[MIME_TEB32_REAL_CLIENT_ID / 4 + 1] = (ULONG)(ULONG_PTR)native->ClientId.UniqueThread;
    teb[MIME_TEB32_PEB / 4] = (ULONG)(ULONG_PTR)peb;
    teb[MIME_TEB32_LOCALE / 4] = native->CurrentLocale;
    teb[MIME_TEB32_DEALLOCATION_STACK / 4] = teb[2];
    MIME_STRING32 *s = (void *)((PUCHAR)teb + MIME_TEB32_STATIC_STRING);
    s->MaximumLength = 522;
    s->Buffer = (ULONG)(ULONG_PTR)teb + MIME_TEB32_STATIC_BUFFER;
    teb[MIME_TEB32_TLS_LINKS / 4] = teb[MIME_TEB32_TLS_LINKS / 4 + 1] = (ULONG)(ULONG_PTR)teb + MIME_TEB32_TLS_LINKS;
    return teb;
}

ULONG Mx32Win32kCall(ULONG number, const ULONG *arguments);


/* how this cpu runs a 32-bit apc routine(context, ...): the native routine and its first argument. */
void Mx32ApcRoutine(ULONG routine, ULONG context, PVOID *native, PVOID *argument);

/* i/o that may complete later: the kernel writes the 32-bit status block itself, told by bit 0 of the apc routine. */
static PVOID io_apc32(ULONG routine, ULONG context, PVOID *apc_context)
{
    PVOID native = nullptr;
    *apc_context = pointer32(context);
    if (routine)
        Mx32ApcRoutine(routine, context, &native, apc_context);
    return (PVOID)((ULONG_PTR)native | 1);
}

/* the handle a call made, narrowed into the caller's variable when it succeeded. */
static NTSTATUS handle_out(NTSTATUS status, ULONG address, HANDLE handle)
{
    if (NT_SUCCESS(status))
        store32(address, (ULONG_PTR)handle);
    return status;
}

/* ------------------------------------------------------------ processes and threads */

/* a string of 32-bit process parameters, at an offset from them when they are not normalized. */
static UNICODE_STRING parameter_string(const MIME_STRING32 *s, ULONG base)
{
    PWSTR text = s->Buffer ? pointer32(s->Buffer + base) : nullptr;
    return (UNICODE_STRING){s->Length, s->MaximumLength, text};
}

static void parameters64(const MIME_PARAMETERS32 *p, RTL_USER_PROCESS_PARAMETERS *pp)
{
    ULONG base = p->Flags & RTL_USER_PROC_PARAMS_NORMALIZED ? 0 : (ULONG)(ULONG_PTR)p;
    memset(pp, 0, sizeof(*pp));
    pp->MaximumLength = pp->Length = sizeof(*pp);
    pp->Flags = p->Flags | RTL_USER_PROC_PARAMS_NORMALIZED;
    pp->DebugFlags = p->DebugFlags;
    pp->ConsoleHandle = handle32(p->ConsoleHandle);
    pp->ConsoleFlags = p->ConsoleFlags;
    pp->StandardInput = handle32(p->StandardInput);
    pp->StandardOutput = handle32(p->StandardOutput);
    pp->StandardError = handle32(p->StandardError);
    pp->CurrentDirectory.DosPath = parameter_string(&p->CurrentDirectory, base);
    pp->CurrentDirectory.Handle = handle32(p->CurrentDirectoryHandle);
    pp->DllPath = parameter_string(&p->DllPath, base);
    pp->ImagePathName = parameter_string(&p->ImagePathName, base);
    pp->CommandLine = parameter_string(&p->CommandLine, base);
    pp->Environment = pointer32(p->Environment);
    pp->StartingX = p->StartingX;
    pp->StartingY = p->StartingY;
    pp->CountX = p->CountX;
    pp->CountY = p->CountY;
    pp->CountCharsX = p->CountCharsX;
    pp->CountCharsY = p->CountCharsY;
    pp->FillAttribute = p->FillAttribute;
    pp->WindowFlags = p->WindowFlags;
    pp->ShowWindowFlags = p->ShowWindowFlags;
    pp->WindowTitle = parameter_string(&p->WindowTitle, base);
    pp->DesktopInfo = parameter_string(&p->DesktopInfo, base);
    pp->ShellInfo = parameter_string(&p->ShellInfo, base);
    pp->RuntimeData = parameter_string(&p->RuntimeData, base);
    if (p->Length >= offsetof(MIME_PARAMETERS32, EnvironmentVersion)) {
        pp->EnvironmentSize = p->EnvironmentSize;
        pp->EnvironmentVersion = p->EnvironmentVersion;
    }
}

/* PS_ATTRIBUTE in a list after its total length, 16 bytes in a 32-bit program. */
typedef struct {
    ULONG_PTR Attribute;
    SIZE_T Size;
    ULONG_PTR Value;
    PSIZE_T ReturnLength;
} PS_ATTRIBUTE64;

#define PS_ATTRIBUTE_CLIENT_ID 0x10003
#define PS_ATTRIBUTE_TEB_ADDRESS 0x10004
#define PS_ATTRIBUTE_MAXIMUM 32

/*
 * NtCreateThreadEx with its attribute list widened. the thread's client id
 * and teb are given back in the 32-bit layout; the values of the other
 * attributes are handles or the addresses of what they describe.
 */
static NTSTATUS create_thread32(const ULONG *a)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa = attributes32(a[2], &name);
    struct {
        SIZE_T TotalLength;
        PS_ATTRIBUTE64 Attributes[PS_ATTRIBUTE_MAXIMUM];
    } list;
    const ULONG *in = pointer32(a[10]);
    ULONG count = 0;
    ULONG client = 0, teb = 0, client_length = 0, teb_length = 0;
    if (in) {
        if (in[0] < 20 || (in[0] - 4) % 16)
            return STATUS_INVALID_PARAMETER;
        count = (in[0] - 4) / 16;
        if (count > PS_ATTRIBUTE_MAXIMUM)
            return STATUS_INVALID_PARAMETER;
        for (ULONG i = 0; i < count; i++) {
            const ULONG *e = in + 1 + 4 * i;
            PS_ATTRIBUTE64 *o = &list.Attributes[i];
            /* the parent process, debug object and token are handles, sign extended; the rest addresses. */
            BOOLEAN handle = e[0] >= 0x60000 && e[0] <= 0x60002;
            *o = (PS_ATTRIBUTE64){e[0], e[1], handle ? (ULONG_PTR)(LONG_PTR)(LONG)e[2] : e[2], nullptr};
            if (e[0] == PS_ATTRIBUTE_CLIENT_ID) {
                client = e[2];
                client_length = e[3];
                o->Value = 0;
            } else if (e[0] == PS_ATTRIBUTE_TEB_ADDRESS) {
                teb = e[2];
                teb_length = e[3];
                o->Value = 0;
            }
        }
        list.TotalLength = sizeof(SIZE_T) + count * sizeof(PS_ATTRIBUTE64);
    }
    HANDLE thread = nullptr;
    NTSTATUS status = NtCreateThreadEx(&thread, a[1], a[2] ? &oa : nullptr, handle32(a[3]), pointer32(a[4]),
                                       pointer32(a[5]), a[6], a[7], a[8], a[9], in ? &list : nullptr);
    if (!NT_SUCCESS(status))
        return status;
    store32(a[0], (ULONG_PTR)thread);
    if (client || teb) {
        THREAD_BASIC_INFORMATION info = {};
        NtQueryInformationThread(thread, ThreadBasicInformation, &info, sizeof(info), nullptr);
        if (client) {
            PULONG id = pointer32(client);
            id[0] = (ULONG)(ULONG_PTR)info.ClientId.UniqueProcess;
            id[1] = (ULONG)(ULONG_PTR)info.ClientId.UniqueThread;
            store32(client_length, 8);
        }
        if (teb) {
            store32(teb, Mx32TebOf(handle32(a[3]), info.TebBaseAddress));
            store32(teb_length, 4);
        }
    }
    return status;
}

static NTSTATUS virtual_memory_io32(BOOLEAN write, const ULONG *a)
{
    SIZE_T done = 0;
    NTSTATUS status = write ? NtWriteVirtualMemory(handle32(a[0]), pointer32(a[1]), pointer32(a[2]), a[3], &done)
                            : NtReadVirtualMemory(handle32(a[0]), pointer32(a[1]), pointer32(a[2]), a[3], &done);
    store32(a[4], saturate32(done));
    return status;
}

/*
 * the extended parameters of an allocation or a view: each is 16 bytes in a
 * 32-bit program as well, with a 32-bit pointer or handle in the union, and
 * the address requirements are three 32-bit values. the native array and the
 * requirements it points to are made in one block of scratch memory.
 */
static NTSTATUS extended32(ULONG address, ULONG count, PMEM_EXTENDED_PARAMETER *out, PVOID *block)
{
    *out = nullptr;
    *block = nullptr;
    if (!count)
        return address ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (!address || count > 16)
        return STATUS_INVALID_PARAMETER;
    SIZE_T bytes = (SIZE_T)count * (sizeof(MEM_EXTENDED_PARAMETER) + sizeof(MEM_ADDRESS_REQUIREMENTS));
    PMEM_EXTENDED_PARAMETER native = Mx32Scratch(bytes);
    if (!native)
        return STATUS_NO_MEMORY;
    PMEM_ADDRESS_REQUIREMENTS requirements = (PMEM_ADDRESS_REQUIREMENTS)(native + count);
    const ULONG *in = pointer32(address);
    for (ULONG i = 0; i < count; i++) {
        ULONG64 header = in[i * 4] | (ULONG64)in[i * 4 + 1] << 32;
        ULONG type = (ULONG)(header & 0xFF);
        memcpy(&native[i], &header, sizeof(header));
        native[i].ULong64 = in[i * 4 + 2] | (ULONG64)in[i * 4 + 3] << 32;
        if (header < 0x100 && type == MemExtendedParameterAddressRequirements) {
            const ULONG *r = pointer32(in[i * 4 + 2]);
            if (!r) {
                native[i].Pointer = nullptr;
            } else {
                requirements[i].LowestStartingAddress = pointer32(r[0]);
                requirements[i].HighestEndingAddress = pointer32(r[1]);
                requirements[i].Alignment = r[2];
                native[i].Pointer = &requirements[i];
            }
        } else if (header < 0x100 && (type == MemExtendedParameterPartitionHandle ||
                                      type == MemExtendedParameterUserPhysicalHandle)) {
            native[i].Handle = handle32((ULONG)native[i].ULong64);
        }
    }
    *out = native;
    *block = native;
    return STATUS_SUCCESS;
}

/* MEMORY_RANGE_ENTRY: an address and a size, 8 bytes in a 32-bit program. */
static NTSTATUS set_virtual_memory32(const ULONG *a)
{
    ULONG count = a[2];
    if (!count || count > 0x10000)
        return STATUS_INVALID_PARAMETER;
    const ULONG *in = pointer32(a[3]);
    ULONG_PTR *entries = Mx32Scratch((SIZE_T)count * 2 * sizeof(ULONG_PTR));
    if (!entries)
        return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < 2 * count; i++)
        entries[i] = in[i];
    NTSTATUS status;
    if (a[1] == VmCfgCallTargetInformation) {
        /* call targets: a count, the count processed, the targets, a section and an offset, 32 bytes here. */
        const ULONG *info = pointer32(a[4]);
        if (a[5] != 32 || !info) {
            Mx32FreeScratch(entries);
            return a[5] != 32 ? STATUS_INVALID_PARAMETER_6 : STATUS_INVALID_PARAMETER_5;
        }
        struct {
            ULONG Entries, Reserved;
            PVOID Processed, Targets, Section;
            ULONG64 Offset;
        } native = {info[0], info[1], pointer32(info[2]), pointer32(info[3]), pointer32(info[4]), 0};
        memcpy(&native.Offset, info + 6, sizeof(native.Offset));
        status = NtSetInformationVirtualMemory(handle32(a[0]), a[1], count, entries, &native, sizeof(native));
    } else {
        status = NtSetInformationVirtualMemory(handle32(a[0]), a[1], count, entries, pointer32(a[4]), a[5]);
    }
    Mx32FreeScratch(entries);
    return status;
}

/* ------------------------------------------------------------ files */

/*
 * FILE_RENAME_INFORMATION and FILE_LINK_INFORMATION, with or without
 * flags: a word of flags or a boolean, the root directory, then the
 * name's length and characters.
 */
static NTSTATUS set_rename32(const ULONG *a)
{
    const ULONG *p = pointer32(a[2]);
    if (!p || a[3] < 12 || (p[2] & 1) || p[2] > a[3] - 12 || p[2] > 65534)
        return STATUS_INVALID_PARAMETER;
    struct RENAME {
        ULONG Flags;
        HANDLE Root;
        ULONG Length;
        WCHAR Name[1];
    };
    SIZE_T size = offsetof(struct RENAME, Name) + p[2];
    struct RENAME *info = Mx32Scratch(size);
    if (!info)
        return STATUS_NO_MEMORY;
    info->Flags = p[0];
    info->Root = handle32(p[1]);
    info->Length = p[2];
    memcpy(info->Name, p + 3, p[2]);
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = NtSetInformationFile(handle32(a[0]), &io, info, (ULONG)size, a[4]);
    Mx32FreeScratch(info);
    store_iosb32(a[1], &io);
    return status;
}

/* the classes whose records are the same in both, a large integer at most. */
static BOOLEAN same_set_class(ULONG klass)
{
    switch (klass) {
    case FileBasicInformation:
    case FileDispositionInformation:
    case FilePositionInformation:
    case FileModeInformation:
    case FileAllocationInformation:
    case FileEndOfFileInformation:
    case FilePipeInformation:
    case 39: /* FileValidDataLengthInformation */
    case 40: /* FileShortNameInformation */
    case FileIoCompletionNotificationInformation:
    case 43: /* FileIoPriorityHintInformation */
    case 44: /* FileSfioReserveInformation */
    case FileDispositionInformationEx:
    case 67:
    case 71: /* FileCaseSensitiveInformation */
    case 74:
    case 76:
        return TRUE;
    default:
        return FALSE;
    }
}

static NTSTATUS set_file32(const ULONG *a)
{
    IO_STATUS_BLOCK io = {};
    NTSTATUS status;
    switch (a[4]) {
    case FileRenameInformation:
    case 11: /* FileLinkInformation */
    case 65: /* FileRenameInformationEx */
    case 72: /* FileLinkInformationEx */
        return set_rename32(a);
    case FileCompletionInformation:
    case 61: { /* FileReplaceCompletionInformation */
        /* the port, then the key. */
        const ULONG *p = pointer32(a[2]);
        if (a[3] < 8)
            return STATUS_INFO_LENGTH_MISMATCH;
        PVOID info[2] = {p ? handle32(p[0]) : nullptr, p ? pointer32(p[1]) : nullptr};
        status = NtSetInformationFile(handle32(a[0]), &io, p ? info : nullptr, sizeof(info), a[4]);
        break;
    }
    default:
        if (!same_set_class(a[4]))
            return STATUS_INVALID_INFO_CLASS;
        status = NtSetInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
        break;
    }
    store_iosb32(a[1], &io);
    return status;
}

/* WSABUF: a length then the data, 8 bytes in a 32-bit program, each widened for the socket driver. */
static NTSTATUS socket_transfer32(const ULONG *a, HANDLE file, HANDLE event, PVOID apc, PVOID context)
{
    if (a[7] != sizeof(MIME_AFD_TRANSFER) || !a[6])
        return STATUS_INVALID_PARAMETER;
    MIME_AFD_TRANSFER t;
    memcpy(&t, pointer32(a[6]), sizeof(t));
    if (t.BufferCount > 1024)
        return STATUS_INVALID_PARAMETER;
    MIME_AFD_BUFFER small[16];
    MIME_AFD_BUFFER *buffers = small;
    if (t.BufferCount > RTL_NUMBER_OF(small) && !(buffers = Mx32Scratch(t.BufferCount * sizeof(*buffers))))
        return STATUS_NO_MEMORY;
    const ULONG *in = pointer32((ULONG)t.Buffers);
    for (ULONG i = 0; i < t.BufferCount; i++)
        buffers[i] = (MIME_AFD_BUFFER){in[2 * i], 0, in[2 * i + 1]};
    t.Buffers = (ULONG64)(ULONG_PTR)buffers;
    /* the driver takes the buffers when the request starts, so they may go once it returns. */
    NTSTATUS status = NtDeviceIoControlFile(file, event, apc, context, pointer32(a[4]), a[5], &t, sizeof(t),
                                            pointer32(a[8]), a[9]);
    if (buffers != small)
        Mx32FreeScratch(buffers);
    return status;
}

/* ------------------------------------------------------------ completion ports */

static NTSTATUS remove_completion32(const ULONG *a)
{
    PVOID key = a[1] ? pointer32(*(PULONG)pointer32(a[1])) : nullptr;
    PVOID context = a[2] ? pointer32(*(PULONG)pointer32(a[2])) : nullptr;
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = NtRemoveIoCompletion(handle32(a[0]), &key, &context, &io, pointer32(a[4]));
    if (status == STATUS_SUCCESS) {
        store32(a[1], (ULONG_PTR)key);
        store32(a[2], (ULONG_PTR)context);
        store_iosb32(a[3], &io);
    }
    return status;
}

typedef struct {
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} COMPLETION64;

/* FILE_IO_COMPLETION_INFORMATION: a key, a context and a status block, 16 bytes in a 32-bit program. */
static NTSTATUS remove_completions32(const ULONG *a)
{
    COMPLETION64 native[64];
    ULONG count = a[2] < RTL_NUMBER_OF(native) ? a[2] : RTL_NUMBER_OF(native), removed = 0;
    if (!count || !a[1])
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = NtRemoveIoCompletionEx(handle32(a[0]), native, count, &removed, pointer32(a[4]), (BOOLEAN)a[5]);
    PULONG out = pointer32(a[1]);
    for (ULONG i = 0; i < removed; i++) {
        out[4 * i] = (ULONG)(ULONG_PTR)native[i].KeyContext;
        out[4 * i + 1] = (ULONG)(ULONG_PTR)native[i].ApcContext;
        out[4 * i + 2] = (ULONG)native[i].IoStatusBlock.Status;
        out[4 * i + 3] = (ULONG)native[i].IoStatusBlock.Information;
    }
    store32(a[3], removed);
    return status;
}

/* ------------------------------------------------------------ the call */

/*
 * a descriptor with pointers of 32 bits made self relative, the form the kernel keeps: the header, then
 * the owner, group, sacl and dacl where they are asked for. a self relative one passes through, and so does
 * none. the copy comes from the scratch space of the call and is given back with Mx32FreeScratch.
 */
static const UCHAR *relative_descriptor32(ULONG pointer, UCHAR **scratch)
{
    const UCHAR *sd = pointer32(pointer);
    *scratch = nullptr;
    if (!sd || (*(const USHORT *)(sd + 2) & 0x8000))
        return sd;
    const UCHAR *parts[4];
    ULONG lengths[4], total = 20;
    for (int i = 0; i < 4; i++) {
        parts[i] = pointer32(*(const ULONG *)(sd + 4 + 4 * i));
        lengths[i] = 0;
        if (!parts[i])
            continue;
        lengths[i] = i < 2 ? 8 + 4 * (ULONG)parts[i][1] : *(const USHORT *)(parts[i] + 2);
        total += (lengths[i] + 3) & ~3u;
    }
    UCHAR *relative = Mx32Scratch(total);
    if (!relative)
        return nullptr;
    memset(relative, 0, 20);
    relative[0] = sd[0];
    *(USHORT *)(relative + 2) = *(const USHORT *)(sd + 2) | 0x8000;
    ULONG at = 20;
    for (int i = 0; i < 4; i++) {
        if (!lengths[i])
            continue;
        *(ULONG *)(relative + 4 + 4 * i) = at;
        memcpy(relative + at, parts[i], lengths[i]);
        at += (lengths[i] + 3) & ~3u;
    }
    *scratch = relative;
    return relative;
}

static NTSTATUS set_security32(const ULONG *a)
{
    UCHAR *scratch;
    const UCHAR *sd = relative_descriptor32(a[2], &scratch);
    if (a[2] && !sd)
        return STATUS_NO_MEMORY;
    NTSTATUS status = NtSetSecurityObject(handle32(a[0]), a[1], (PVOID)sd);
    if (scratch)
        Mx32FreeScratch(scratch);
    return status;
}

/* the structures of an access check are the same in both widths, only the descriptor has pointers. */
static NTSTATUS access_check32(const ULONG *a)
{
    UCHAR *scratch;
    const UCHAR *sd = relative_descriptor32(a[0], &scratch);
    if (a[0] && !sd)
        return STATUS_NO_MEMORY;
    NTSTATUS status = NtAccessCheck((PVOID)sd, handle32(a[1]), a[2], pointer32(a[3]), pointer32(a[4]), pointer32(a[5]),
                                    pointer32(a[6]), pointer32(a[7]));
    if (scratch)
        Mx32FreeScratch(scratch);
    return status;
}

NTSTATUS Mx32SystemCall(ULONG number, const ULONG *a)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK io = {};
    HANDLE handle = nullptr;
    NTSTATUS status;
    if ((number >> 12) == 1)
        return (NTSTATUS)Mx32Win32kCall(number, a);
    switch (number) {
    case MIME_SVC_NtTerminateProcess:
        return NtTerminateProcess(handle32(a[0]), a[1]);
    case MIME_SVC_NtTerminateThread:
        return NtTerminateThread(handle32(a[0]), a[1]);
    case MIME_SVC_NtClose:
        return NtClose(handle32(a[0]));
    case MIME_SVC_NtPlugPlayControl:
        /* mime's requests hold no pointers. */
        return NtPlugPlayControl(a[0], pointer32(a[1]), a[2]);
    case MIME_SVC_NtQueryDirectoryObject: {
        /* the 64-bit entries are twice as large, so what they hold always fits the caller's buffer once packed. */
        ULONG length = a[2];
        PUCHAR wide = Mx32Scratch(length);
        if (!wide)
            return STATUS_NO_MEMORY;
        ULONG returned = 0;
        status = NtQueryDirectoryObject(handle32(a[0]), wide, length, (BOOLEAN)a[3], (BOOLEAN)a[4], pointer32(a[5]),
                                        &returned);
        if (NT_SUCCESS(status)) {
            const UNICODE_STRING *in = (const UNICODE_STRING *)wide;
            ULONG n = 0;
            while (in[n * 2].Buffer)
                n++;
            PUCHAR out = pointer32(a[1]);
            PUCHAR text = out + (n + 1) * 16;
            ULONG *entry = (ULONG *)out;
            for (ULONG i = 0; i < 2 * n; i++) {
                SIZE_T bytes = in[i].Length + sizeof(WCHAR);
                memcpy(text, in[i].Buffer, bytes);
                entry[i * 2] = in[i].Length | (ULONG)(in[i].MaximumLength) << 16;
                entry[i * 2 + 1] = (ULONG)(ULONG_PTR)text;
                text += bytes;
            }
            memset(out + n * 16, 0, 16);
            returned = (ULONG)(text - out);
        }
        if (a[6] && (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL))
            store32(a[6], returned);
        Mx32FreeScratch(wide);
        return status;
    }
    case MIME_SVC_NtDuplicateObject: {
        HANDLE target = nullptr;
        status = NtDuplicateObject(handle32(a[0]), handle32(a[1]), handle32(a[2]), a[3] ? &target : nullptr, a[4], a[5],
                                   a[6]);
        if (NT_SUCCESS(status) && a[3])
            store32(a[3], (ULONG_PTR)target);
        return status;
    }
    case MIME_SVC_NtTestAlert:
        return NtTestAlert();
    case MIME_SVC_NtDisplayString:
        name = string32(a[0]);
        return NtDisplayString(&name);

    /* virtual memory: addresses and sizes zero extended, given back narrowed, inside the 32-bit space. */
    case MIME_SVC_NtAllocateVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[3]);
        if ((ULONG_PTR)base + size > 0x7FFF0000ull)
            return STATUS_NO_MEMORY;
        status = NtAllocateVirtualMemory(handle32(a[0]), &base, a[2], &size, a[4], a[5]);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            SIZE_T release = 0;
            NtFreeVirtualMemory(handle32(a[0]), &base, &release, MEM_RELEASE);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[3], size);
        }
        return status;
    }
    case MIME_SVC_NtAllocateVirtualMemoryEx: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[2]);
        PMEM_EXTENDED_PARAMETER ext;
        PVOID block;
        status = extended32(a[5], a[6], &ext, &block);
        if (!NT_SUCCESS(status))
            return status;
        if ((ULONG_PTR)base + size > 0x7FFF0000ull) {
            Mx32FreeScratch(block);
            return STATUS_NO_MEMORY;
        }
        status = NtAllocateVirtualMemoryEx(handle32(a[0]), &base, &size, a[3], a[4], ext, a[6]);
        Mx32FreeScratch(block);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            SIZE_T release = 0;
            NtFreeVirtualMemory(handle32(a[0]), &base, &release, MEM_RELEASE);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[2], size);
        }
        return status;
    }
    case MIME_SVC_NtFreeVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[2]);
        status = NtFreeVirtualMemory(handle32(a[0]), &base, &size, a[3]);
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[2], size);
        }
        return status;
    }
    case MIME_SVC_NtProtectVirtualMemory: {
        PVOID base = pointer32(*(PULONG)pointer32(a[1]));
        SIZE_T size = *(PULONG)pointer32(a[2]);
        status = NtProtectVirtualMemory(handle32(a[0]), &base, &size, a[3], pointer32(a[4]));
        if (NT_SUCCESS(status)) {
            store32(a[1], (ULONG_PTR)base);
            store32(a[2], size);
        }
        return status;
    }
    case MIME_SVC_NtQueryVirtualMemory:
        return Mx32QueryVirtualMemory(a);
    case MIME_SVC_NtReadVirtualMemory:
    case MIME_SVC_NtWriteVirtualMemory:
        return virtual_memory_io32(number == MIME_SVC_NtWriteVirtualMemory, a);
    case MIME_SVC_NtGetWriteWatch: {
        /* the addresses come back 32 bits wide, and the count is a 32-bit value. */
        ULONG_PTR requested = *(PULONG)pointer32(a[5]);
        PULONG out = pointer32(a[4]);
        if (!requested || !out || requested > 0x100000)
            return STATUS_INVALID_PARAMETER_5;
        PVOID *native = Mx32Scratch(requested * sizeof(PVOID));
        if (!native)
            return STATUS_NO_MEMORY;
        status = NtGetWriteWatch(handle32(a[0]), a[1], pointer32(a[2]), a[3], native, &requested, pointer32(a[6]));
        if (NT_SUCCESS(status)) {
            for (ULONG_PTR i = 0; i < requested; i++)
                out[i] = (ULONG)(ULONG_PTR)native[i];
            store32(a[5], requested);
        }
        Mx32FreeScratch(native);
        return status;
    }
    case MIME_SVC_NtResetWriteWatch:
        return NtResetWriteWatch(handle32(a[0]), pointer32(a[1]), a[2]);
    case MIME_SVC_NtFlushInstructionCache:
        return NtFlushInstructionCache(handle32(a[0]), pointer32(a[1]), a[2]);
    case MIME_SVC_NtFlushProcessWriteBuffers:
        return NtFlushProcessWriteBuffers();
    case MIME_SVC_NtSetInformationVirtualMemory:
        return set_virtual_memory32(a);

    /* processes and threads. */
    case MIME_SVC_NtCreateUserProcess: {
        /* an image's creation information, which the kernel refuses too, and the attribute list. */
        if (a[9] || !a[8])
            return a[8] ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER;
        PS_ATTRIBUTE_LIST *native_list = nullptr;
        UCHAR list_storage[sizeof(SIZE_T) + 32 * sizeof(PS_ATTRIBUTE)];
        PVOID handle_arrays[2] = {};
        if (a[10]) {
            /* the 32-bit list: a size, then attributes of four words. */
            const ULONG *in = pointer32(a[10]);
            ULONG total = in[0];
            if (total < sizeof(ULONG) || (total - sizeof(ULONG)) % 16 || (total - sizeof(ULONG)) / 16 > 32)
                return STATUS_INVALID_PARAMETER;
            ULONG count = (total - sizeof(ULONG)) / 16, arrays = 0;
            native_list = (PS_ATTRIBUTE_LIST *)list_storage;
            native_list->TotalLength = sizeof(SIZE_T) + count * sizeof(PS_ATTRIBUTE);
            for (ULONG i = 0; i < count; i++) {
                const ULONG *e = in + 1 + 4 * i;
                PS_ATTRIBUTE *n = &native_list->Attributes[i];
                n->Attribute = e[0];
                n->Size = e[1];
                /* the handle of an additive attribute is the value itself, the others point at it. */
                n->Value = e[0] == PS_ATTRIBUTE_TOKEN || e[0] == 0x60000 ? (ULONG_PTR)handle32(e[2]) : e[2];
                n->ReturnLength = nullptr;
                if ((e[0] == PS_ATTRIBUTE_TOKEN || e[0] == 0x60000) && e[1] == sizeof(ULONG))
                    n->Size = sizeof(HANDLE);
                /* lists of handles are widened. */
                if ((e[0] == 0x2000B || e[0] == 0x20013) && e[1] && !(e[1] & 3) && e[1] <= 0x40000 && arrays < 2) {
                    ULONG handles = e[1] / sizeof(ULONG);
                    HANDLE *wide = Mx32Scratch((SIZE_T)handles * sizeof(HANDLE));
                    if (!wide) {
                        for (ULONG k = 0; k < arrays; k++)
                            Mx32FreeScratch(handle_arrays[k]);
                        return STATUS_NO_MEMORY;
                    }
                    const ULONG *narrow = pointer32(e[2]);
                    for (ULONG k = 0; k < handles; k++)
                        wide[k] = handle32(narrow[k]);
                    handle_arrays[arrays++] = wide;
                    n->Size = (SIZE_T)handles * sizeof(HANDLE);
                    n->Value = (ULONG_PTR)wide;
                }
            }
        }
        RTL_USER_PROCESS_PARAMETERS pp;
        parameters64(pointer32(a[8]), &pp);
        UNICODE_STRING process_name, thread_name;
        OBJECT_ATTRIBUTES process_oa = attributes32(a[4], &process_name);
        OBJECT_ATTRIBUTES thread_oa = attributes32(a[5], &thread_name);
        HANDLE process = nullptr, thread = nullptr;
        status = NtCreateUserProcess(&process, &thread, a[2], a[3], a[4] ? &process_oa : nullptr,
                                     a[5] ? &thread_oa : nullptr, a[6], a[7], &pp, nullptr, native_list);
        for (ULONG k = 0; k < 2; k++)
            if (handle_arrays[k])
                Mx32FreeScratch(handle_arrays[k]);
        if (NT_SUCCESS(status)) {
            store32(a[0], (ULONG_PTR)process);
            store32(a[1], (ULONG_PTR)thread);
        }
        return status;
    }
    case MIME_SVC_NtCreateThreadEx:
        return create_thread32(a);
    case MIME_SVC_NtQueueApcThread:
        return NtQueueApcThread(handle32(a[0]), pointer32(a[1]), pointer32(a[2]), pointer32(a[3]), pointer32(a[4]));
    case MIME_SVC_NtResumeThread:
        return NtResumeThread(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtSuspendThread:
        return NtSuspendThread(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtAlertThread:
        return NtAlertThread(handle32(a[0]));
    case MIME_SVC_NtOpenProcess: {
        /* the 32-bit client id holds two zero extended ids. */
        const ULONG *source = pointer32(a[3]);
        CLIENT_ID cid = {};
        if (source) {
            cid.UniqueProcess = (HANDLE)(ULONG_PTR)source[0];
            cid.UniqueThread = (HANDLE)(ULONG_PTR)source[1];
        }
        oa = attributes32(a[2], &name);
        status = NtOpenProcess(&handle, a[1], a[2] ? &oa : nullptr, source ? &cid : nullptr);
        return handle_out(status, a[0], handle);
    }
    case MIME_SVC_NtSuspendProcess:
        return NtSuspendProcess(handle32(a[0]));
    case MIME_SVC_NtResumeProcess:
        return NtResumeProcess(handle32(a[0]));
    case MIME_SVC_NtOpenThread: {
        const ULONG *source = pointer32(a[3]);
        CLIENT_ID cid = {};
        if (source) {
            cid.UniqueProcess = (HANDLE)(ULONG_PTR)source[0];
            cid.UniqueThread = (HANDLE)(ULONG_PTR)source[1];
        }
        oa = attributes32(a[2], &name);
        status = NtOpenThread(&handle, a[1], a[2] ? &oa : nullptr, source ? &cid : nullptr);
        return handle_out(status, a[0], handle);
    }
    case MIME_SVC_NtCreateLowBoxToken: {
        /* the capabilities are sid and attribute pairs, the handles are 32-bit values. */
        ULONG capability_count = a[5], handle_count = a[7];
        if (capability_count > 0x1000 || handle_count > 0x10)
            return STATUS_INVALID_PARAMETER;
        const ULONG *capabilities32 = pointer32(a[6]);
        const ULONG *handles32 = pointer32(a[8]);
        struct {
            PVOID Sid;
            ULONG Attributes, Pad;
        } *capabilities = nullptr;
        HANDLE handles[0x10];
        if (capability_count && capabilities32) {
            capabilities = Mx32Scratch((SIZE_T)capability_count * sizeof(*capabilities));
            if (!capabilities)
                return STATUS_NO_MEMORY;
            for (ULONG i = 0; i < capability_count; i++) {
                capabilities[i].Sid = pointer32(capabilities32[2 * i]);
                capabilities[i].Attributes = capabilities32[2 * i + 1];
                capabilities[i].Pad = 0;
            }
        }
        if (handles32)
            for (ULONG i = 0; i < handle_count; i++)
                handles[i] = handle32(handles32[i]);
        oa = attributes32(a[3], &name);
        status = NtCreateLowBoxToken(&handle, handle32(a[1]), a[2], a[3] ? &oa : nullptr, pointer32(a[4]),
                                     capability_count, capabilities32 ? (PVOID)capabilities : nullptr, handle_count,
                                     handles32 ? handles : nullptr);
        if (capabilities)
            Mx32FreeScratch(capabilities);
        return handle_out(status, a[0], handle);
    }
    case MIME_SVC_NtQueryInformationProcess:
        return Mx32QueryInformationProcess(a);
    case MIME_SVC_NtSetInformationProcess:
        return Mx32SetInformationProcess(a);
    case MIME_SVC_NtQueryInformationThread:
        return Mx32QueryInformationThread(a);
    case MIME_SVC_NtSetInformationThread:
        return Mx32SetInformationThread(a);

    /* jobs. */
    case MIME_SVC_NtCreateJobObject:
    case MIME_SVC_NtOpenJobObject:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtCreateJobObject)
            status = NtCreateJobObject(&handle, a[1], a[2] ? &oa : nullptr);
        else
            status = NtOpenJobObject(&handle, a[1], &oa);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtAssignProcessToJobObject:
        return NtAssignProcessToJobObject(handle32(a[0]), handle32(a[1]));
    case MIME_SVC_NtIsProcessInJob:
        return NtIsProcessInJob(handle32(a[0]), handle32(a[1]));
    case MIME_SVC_NtTerminateJobObject:
        return NtTerminateJobObject(handle32(a[0]), a[1]);
    case MIME_SVC_NtQueryInformationJobObject:
        return Mx32QueryInformationJobObject(a);
    case MIME_SVC_NtSetInformationJobObject:
        return Mx32SetInformationJobObject(a);

    /* synchronization objects. */
    case MIME_SVC_NtCreateEvent:
        oa = attributes32(a[2], &name);
        status = NtCreateEvent(&handle, a[1], a[2] ? &oa : nullptr, a[3], a[4]);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtOpenEvent:
    case MIME_SVC_NtOpenSemaphore:
    case MIME_SVC_NtOpenTimer:
    case MIME_SVC_NtOpenSection:
    case MIME_SVC_NtOpenIoCompletion:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtOpenEvent)
            status = NtOpenEvent(&handle, a[1], &oa);
        else if (number == MIME_SVC_NtOpenSemaphore)
            status = NtOpenSemaphore(&handle, a[1], &oa);
        else if (number == MIME_SVC_NtOpenTimer)
            status = NtOpenTimer(&handle, a[1], &oa);
        else if (number == MIME_SVC_NtOpenSection)
            status = NtOpenSection(&handle, a[1], &oa);
        else
            status = NtOpenIoCompletion(&handle, a[1], &oa);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtClearEvent:
        return NtClearEvent(handle32(a[0]));
    case MIME_SVC_NtPulseEvent:
        return NtPulseEvent(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtCreateSemaphore:
        oa = attributes32(a[2], &name);
        status = NtCreateSemaphore(&handle, a[1], a[2] ? &oa : nullptr, a[3], a[4]);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtReleaseSemaphore:
        return NtReleaseSemaphore(handle32(a[0]), a[1], pointer32(a[2]));
    case MIME_SVC_NtCreateTimer:
        oa = attributes32(a[2], &name);
        status = NtCreateTimer(&handle, a[1], a[2] ? &oa : nullptr, a[3]);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtSetTimer: {
        /* the apc routine, which takes the context and the time, is 32-bit code. */
        PVOID routine = nullptr, context = pointer32(a[3]);
        if (a[2])
            Mx32ApcRoutine(a[2], a[3], &routine, &context);
        return NtSetTimer(handle32(a[0]), pointer32(a[1]), routine, context, (BOOLEAN)a[4], a[5], pointer32(a[6]));
    }
    case MIME_SVC_NtCancelTimer:
        return NtCancelTimer(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtQueryTimerResolution:
        return NtQueryTimerResolution(pointer32(a[0]), pointer32(a[1]), pointer32(a[2]));
    case MIME_SVC_NtSetTimerResolution:
        return NtSetTimerResolution(a[0], (BOOLEAN)a[1], pointer32(a[2]));
    case MIME_SVC_NtSignalAndWaitForSingleObject:
        return NtSignalAndWaitForSingleObject(handle32(a[0]), handle32(a[1]), (BOOLEAN)a[2], pointer32(a[3]));
    case MIME_SVC_NtCreateMutant:
    case MIME_SVC_NtOpenMutant:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtCreateMutant)
            status = NtCreateMutant(&handle, a[1], a[2] ? &oa : nullptr, a[3]);
        else
            status = NtOpenMutant(&handle, a[1], a[2] ? &oa : nullptr);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtReleaseMutant:
        return NtReleaseMutant(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtSetEvent:
        return NtSetEvent(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtResetEvent:
        return NtResetEvent(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtYieldExecution:
        return NtYieldExecution();
    case MIME_SVC_NtWaitForSingleObject:
        return NtWaitForSingleObject(handle32(a[0]), a[1], pointer32(a[2]));
    case MIME_SVC_NtWaitForMultipleObjects: {
        /* the handle array holds 32-bit handles, sign extended like every other x86 handle. */
        if (!a[0] || a[0] > 64)
            return STATUS_INVALID_PARAMETER_1;
        HANDLE handles[64]; /* MAXIMUM_WAIT_OBJECTS */
        const ULONG *source = pointer32(a[1]);
        for (ULONG i = 0; i < a[0]; i++)
            handles[i] = handle32(source[i]);
        return NtWaitForMultipleObjects(a[0], handles, a[2], (BOOLEAN)a[3], pointer32(a[4]));
    }
    /* the waits of locks: an address only names the wait, and a thread id is a number, zero extended. */
    case MIME_SVC_NtWaitForAlertByThreadId:
        return NtWaitForAlertByThreadId(pointer32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtAlertThreadByThreadId:
        return NtAlertThreadByThreadId((HANDLE)(ULONG_PTR)a[0]);
    case MIME_SVC_NtDelayExecution:
        return NtDelayExecution(a[0], pointer32(a[1]));

    /* completion ports: keys and contexts are pointer sized, zero extended and narrowed back. */
    case MIME_SVC_NtCreateIoCompletion:
        oa = attributes32(a[2], &name);
        status = NtCreateIoCompletion(&handle, a[1], a[2] ? &oa : nullptr, a[3]);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtSetIoCompletion:
        return NtSetIoCompletion(handle32(a[0]), pointer32(a[1]), pointer32(a[2]), a[3], a[4]);
    case MIME_SVC_NtSetIoCompletionEx:
        return NtSetIoCompletionEx(handle32(a[0]), handle32(a[1]), pointer32(a[2]), pointer32(a[3]), a[4], a[5]);
    case MIME_SVC_NtRemoveIoCompletion:
        return remove_completion32(a);
    case MIME_SVC_NtRemoveIoCompletionEx:
        return remove_completions32(a);
    case MIME_SVC_NtQueryIoCompletion:
        /* IO_COMPLETION_BASIC_INFORMATION is a count. */
        return NtQueryIoCompletion(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));

    /* files and devices. */
    case MIME_SVC_NtOpenFile:
    case MIME_SVC_NtCreateFile:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtOpenFile)
            status = NtOpenFile(&handle, a[1], &oa, &io, a[4], a[5]);
        else
            status = NtCreateFile(&handle, a[1], &oa, &io, pointer32(a[4]), a[5], a[6], a[7], a[8], pointer32(a[9]), a[10]);
        store_iosb32(a[3], &io);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtCreateNamedPipeFile:
        oa = attributes32(a[2], &name);
        status = NtCreateNamedPipeFile(&handle, a[1], &oa, &io, a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11],
                                       a[12], pointer32(a[13]));
        store_iosb32(a[3], &io);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtReadFile:
    case MIME_SVC_NtWriteFile: {
        PVOID context;
        PVOID apc = io_apc32(a[2], a[3], &context);
        if (number == MIME_SVC_NtReadFile)
            return NtReadFile(handle32(a[0]), handle32(a[1]), apc, context, pointer32(a[4]), pointer32(a[5]), a[6],
                              pointer32(a[7]), pointer32(a[8]));
        return NtWriteFile(handle32(a[0]), handle32(a[1]), apc, context, pointer32(a[4]), pointer32(a[5]), a[6],
                           pointer32(a[7]), pointer32(a[8]));
    }
    case MIME_SVC_NtDeviceIoControlFile:
    case MIME_SVC_NtFsControlFile: {
        /*
         * the buffers of every control mime's drivers answer hold numbers or
         * fixed width addresses, so they pass through: the console's, the
         * named pipes', hid's, xusb's, the system buttons' and the socket
         * driver's, whose transfers only point at narrower WSABUF arrays.
         */
        PVOID context;
        PVOID apc = io_apc32(a[2], a[3], &context);
        if (number == MIME_SVC_NtFsControlFile)
            return NtFsControlFile(handle32(a[0]), handle32(a[1]), apc, context, pointer32(a[4]), a[5],
                                   pointer32(a[6]), a[7], pointer32(a[8]), a[9]);
        if (a[5] == IOCTL_AFD_SEND || a[5] == IOCTL_AFD_RECV)
            return socket_transfer32(a, handle32(a[0]), handle32(a[1]), apc, context);
        return NtDeviceIoControlFile(handle32(a[0]), handle32(a[1]), apc, context, pointer32(a[4]), a[5],
                                     pointer32(a[6]), a[7], pointer32(a[8]), a[9]);
    }
    case MIME_SVC_NtCancelIoFile:
        status = NtCancelIoFile(handle32(a[0]), &io);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtCancelIoFileEx:
        /* requests keep the caller's 32-bit status block, which names the one to cancel. */
        status = NtCancelIoFileEx(handle32(a[0]), pointer32(a[1]), &io);
        store_iosb32(a[2], &io);
        return status;
    case MIME_SVC_NtQueryDirectoryFile:
    case MIME_SVC_NtQueryDirectoryFileEx:
        /*
         * the kernel answers before returning, for an asynchronous handle too,
         * whose event is then set. it would not run an apc routine.
         */
        if (a[2])
            return STATUS_NOT_SUPPORTED;
        name = a[9] ? string32(a[9]) : (UNICODE_STRING){0};
        if (number == MIME_SVC_NtQueryDirectoryFile)
            status = NtQueryDirectoryFile(handle32(a[0]), nullptr, nullptr, pointer32(a[3]), &io, pointer32(a[5]),
                                          a[6], a[7], a[8], a[9] ? &name : nullptr, a[10]);
        else
            status = NtQueryDirectoryFileEx(handle32(a[0]), nullptr, nullptr, pointer32(a[3]), &io, pointer32(a[5]),
                                            a[6], a[7], a[8], a[9] ? &name : nullptr);
        store_iosb32(a[4], &io);
        if (a[1] && status != STATUS_PENDING)
            NtSetEvent(handle32(a[1]), nullptr);
        return status;
    case MIME_SVC_NtFlushBuffersFile:
        status = NtFlushBuffersFile(handle32(a[0]), &io);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtSetInformationFile:
        return set_file32(a);
    case MIME_SVC_NtQueryInformationFile:
        /* the classes mime's file systems answer hold no pointers. */
        status = NtQueryInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtQueryVolumeInformationFile:
        status = NtQueryVolumeInformationFile(handle32(a[0]), &io, pointer32(a[2]), a[3], a[4]);
        store_iosb32(a[1], &io);
        return status;
    case MIME_SVC_NtQueryAttributesFile:
        oa = attributes32(a[0], &name);
        return NtQueryAttributesFile(&oa, pointer32(a[1]));
    case MIME_SVC_NtQueryFullAttributesFile:
        oa = attributes32(a[0], &name);
        return NtQueryFullAttributesFile(&oa, pointer32(a[1]));

    /* sections. */
    case MIME_SVC_NtCreateSection:
        oa = attributes32(a[2], &name);
        status = NtCreateSection(&handle, a[1], a[2] ? &oa : nullptr, pointer32(a[3]), a[4], a[5], handle32(a[6]));
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtMapViewOfSection: {
        PVOID base = pointer32(*(PULONG)pointer32(a[2]));
        SIZE_T size = *(PULONG)pointer32(a[6]);
        if ((ULONG_PTR)base + size > 0x7FFF0000ull)
            return STATUS_NO_MEMORY;
        status = NtMapViewOfSection(handle32(a[0]), handle32(a[1]), &base, a[3], a[4], pointer32(a[5]), &size,
                                    a[7], a[8], a[9]);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            NtUnmapViewOfSection(handle32(a[1]), base);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[2], (ULONG_PTR)base);
            store32(a[6], size);
        }
        return status;
    }
    case MIME_SVC_NtMapViewOfSectionEx: {
        PVOID base = pointer32(*(PULONG)pointer32(a[2]));
        SIZE_T size = *(PULONG)pointer32(a[4]);
        PMEM_EXTENDED_PARAMETER ext;
        PVOID block;
        status = extended32(a[7], a[8], &ext, &block);
        if (!NT_SUCCESS(status))
            return status;
        if ((ULONG_PTR)base + size > 0x7FFF0000ull) {
            Mx32FreeScratch(block);
            return STATUS_NO_MEMORY;
        }
        status = NtMapViewOfSectionEx(handle32(a[0]), handle32(a[1]), &base, pointer32(a[3]), &size, a[5], a[6], ext,
                                      a[8]);
        Mx32FreeScratch(block);
        if (NT_SUCCESS(status) && (ULONG_PTR)base + size > 0x7FFF0000ull) {
            NtUnmapViewOfSection(handle32(a[1]), base);
            return STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(status)) {
            store32(a[2], (ULONG_PTR)base);
            store32(a[4], size);
        }
        return status;
    }
    case MIME_SVC_NtUnmapViewOfSection:
        return NtUnmapViewOfSection(handle32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtUnmapViewOfSectionEx:
        return NtUnmapViewOfSectionEx(handle32(a[0]), pointer32(a[1]), a[2]);
    case MIME_SVC_NtQuerySection:
        return Mx32QuerySection(a);

    /* the object namespace. */
    case MIME_SVC_NtQuerySecurityObject:
        /* a self relative descriptor has no pointers, so the buffer passes through. */
        return NtQuerySecurityObject(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
    case MIME_SVC_NtSetSecurityObject:
        return set_security32(a);
    case MIME_SVC_NtAccessCheck:
        return access_check32(a);
    case MIME_SVC_NtQueryObject:
        return Mx32QueryObject(a);
    case MIME_SVC_NtSetInformationObject:
        return Mx32SetInformationObject(a);
    case MIME_SVC_NtOpenDirectoryObject:
    case MIME_SVC_NtCreateDirectoryObject:
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtOpenDirectoryObject)
            status = NtOpenDirectoryObject(&handle, a[1], &oa);
        else
            status = NtCreateDirectoryObject(&handle, a[1], a[2] ? &oa : nullptr);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtOpenSymbolicLinkObject:
        oa = attributes32(a[2], &name);
        status = NtOpenSymbolicLinkObject(&handle, a[1], &oa);
        return handle_out(status, a[0], handle);
    case MIME_SVC_NtCreateSymbolicLinkObject: {
        oa = attributes32(a[2], &name);
        UNICODE_STRING target = {};
        if (a[3])
            target = string32(a[3]);
        status = NtCreateSymbolicLinkObject(&handle, a[1], &oa, a[3] ? &target : nullptr);
        return handle_out(status, a[0], handle);
    }
    case MIME_SVC_NtMakeTemporaryObject:
        return NtMakeTemporaryObject(handle32(a[0]));
    case MIME_SVC_NtQuerySymbolicLinkObject: {
        name = string32(a[1]);
        status = NtQuerySymbolicLinkObject(handle32(a[0]), &name, pointer32(a[2]));
        ((MIME_STRING32 *)pointer32(a[1]))->Length = name.Length;
        return status;
    }

    /* the registry: key and value records hold no pointers. */
    case MIME_SVC_NtOpenKey:
    case MIME_SVC_NtOpenKeyEx:
    case MIME_SVC_NtCreateKey: {
        oa = attributes32(a[2], &name);
        if (number == MIME_SVC_NtCreateKey) {
            UNICODE_STRING klass = a[4] ? string32(a[4]) : (UNICODE_STRING){0};
            status = NtCreateKey(&handle, a[1], &oa, a[3], a[4] ? &klass : nullptr, a[5], pointer32(a[6]));
        } else if (number == MIME_SVC_NtOpenKeyEx) {
            status = NtOpenKeyEx(&handle, a[1], &oa, a[3]);
        } else {
            status = NtOpenKey(&handle, a[1], &oa);
        }
        return handle_out(status, a[0], handle);
    }
    case MIME_SVC_NtQueryKey:
        return NtQueryKey(handle32(a[0]), a[1], pointer32(a[2]), a[3], pointer32(a[4]));
    case MIME_SVC_NtQueryValueKey:
        name = string32(a[1]);
        return NtQueryValueKey(handle32(a[0]), &name, a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtEnumerateKey:
        return NtEnumerateKey(handle32(a[0]), a[1], a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtEnumerateValueKey:
        return NtEnumerateValueKey(handle32(a[0]), a[1], a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtSetValueKey:
        name = string32(a[1]);
        return NtSetValueKey(handle32(a[0]), &name, a[2], a[3], pointer32(a[4]), a[5]);
    case MIME_SVC_NtDeleteKey:
        return NtDeleteKey(handle32(a[0]));
    case MIME_SVC_NtDeleteValueKey:
        name = string32(a[1]);
        return NtDeleteValueKey(handle32(a[0]), &name);
    case MIME_SVC_NtFlushKey:
        return NtFlushKey(handle32(a[0]));

    /* tokens. */
    case MIME_SVC_NtOpenProcessToken:
    case MIME_SVC_NtOpenProcessTokenEx:
        if (number == MIME_SVC_NtOpenProcessToken)
            status = NtOpenProcessToken(handle32(a[0]), a[1], &handle);
        else
            status = NtOpenProcessTokenEx(handle32(a[0]), a[1], a[2], &handle);
        return handle_out(status, a[number == MIME_SVC_NtOpenProcessToken ? 2 : 3], handle);
    case MIME_SVC_NtOpenThreadToken:
    case MIME_SVC_NtOpenThreadTokenEx:
        if (number == MIME_SVC_NtOpenThreadToken)
            status = NtOpenThreadToken(handle32(a[0]), a[1], (BOOLEAN)a[2], &handle);
        else
            status = NtOpenThreadTokenEx(handle32(a[0]), a[1], (BOOLEAN)a[2], a[3], &handle);
        return handle_out(status, a[number == MIME_SVC_NtOpenThreadToken ? 3 : 4], handle);
    case MIME_SVC_NtQueryInformationToken:
        return Mx32QueryInformationToken(a);
    case MIME_SVC_NtDuplicateToken:
        oa = attributes32(a[2], &name);
        status = NtDuplicateToken(handle32(a[0]), a[1], a[2] ? &oa : nullptr, (BOOLEAN)a[3], a[4], &handle);
        return handle_out(status, a[5], handle);
    case MIME_SVC_NtFilterToken:
        return Mx32FilterToken(a);
    case MIME_SVC_NtSetInformationToken:
        return Mx32SetInformationToken(a);
    case MIME_SVC_NtAdjustGroupsToken:
        return Mx32AdjustGroupsToken(a);
    case MIME_SVC_NtAllocateLocallyUniqueId:
        return NtAllocateLocallyUniqueId(pointer32(a[0]));
    case MIME_SVC_NtImpersonateAnonymousToken:
        return NtImpersonateAnonymousToken(handle32(a[0]));
    case MIME_SVC_NtAdjustPrivilegesToken:
        /* TOKEN_PRIVILEGES holds identifiers and attributes only. */
        return NtAdjustPrivilegesToken(handle32(a[0]), (BOOLEAN)a[1], pointer32(a[2]), a[3], pointer32(a[4]),
                                       pointer32(a[5]));

    /* the system. */
    case MIME_SVC_NtQuerySystemInformation:
        return Mx32QuerySystemInformation(a);
    case MIME_SVC_NtQueryPerformanceCounter:
        return NtQueryPerformanceCounter(pointer32(a[0]), pointer32(a[1]));
    case MIME_SVC_NtQuerySystemTime:
        return NtQuerySystemTime(pointer32(a[0]));
    case MIME_SVC_NtGetCurrentProcessorNumber:
        return (NTSTATUS)NtGetCurrentProcessorNumber();
    case MIME_SVC_NtQueryDefaultLocale:
        return NtQueryDefaultLocale(a[0], pointer32(a[1]));
    case MIME_SVC_NtQueryDefaultUILanguage:
        return NtQueryDefaultUILanguage(pointer32(a[0]));
    case MIME_SVC_NtQueryInstallUILanguage:
        return NtQueryInstallUILanguage(pointer32(a[0]));
    case MIME_SVC_NtQueryDebugFilterState:
        return NtQueryDebugFilterState(a[0], a[1]);
    case MIME_SVC_NtQueryLicenseValue:
        name = a[0] ? string32(a[0]) : (UNICODE_STRING){0};
        return NtQueryLicenseValue(a[0] ? &name : nullptr, pointer32(a[1]), pointer32(a[2]), a[3], pointer32(a[4]));
    case MIME_SVC_NtTraceEvent:
        /* the high bit of the flags tells the event's records are 32-bit ones. */
        return NtTraceEvent(handle32(a[0]), a[1] | 0x80000000u, a[2], pointer32(a[3]));
    case MIME_SVC_NtTraceControl:
        return NtTraceControl(a[0], pointer32(a[1]), a[2], pointer32(a[3]), a[4], pointer32(a[5]));
    case MIME_SVC_NtApphelpCacheControl:
        /* the kernel keeps no application compatibility cache, whatever the request holds. */
        return NtApphelpCacheControl(a[0], pointer32(a[1]));

    /* atoms: a name of 16-bit characters and its length in bytes, the atom 16 bits. */
    case MIME_SVC_NtAddAtom:
        return NtAddAtom(pointer32(a[0]), a[1], pointer32(a[2]));
    case MIME_SVC_NtAddAtomEx:
        return NtAddAtomEx(pointer32(a[0]), a[1], pointer32(a[2]), a[3]);
    case MIME_SVC_NtFindAtom:
        return NtFindAtom(pointer32(a[0]), a[1], pointer32(a[2]));
    case MIME_SVC_NtDeleteAtom:
        return NtDeleteAtom((USHORT)a[0]);
    case MIME_SVC_NtQueryInformationAtom:
        /* the basic record and the table are counts, flags and characters. */
        if (a[1] > 1)
            return STATUS_INVALID_INFO_CLASS;
        return NtQueryInformationAtom((USHORT)a[0], a[1], pointer32(a[2]), a[3], pointer32(a[4]));
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}
