/*
 * user process creation, for programs the host starts and for NtCreateUserProcess. the creator's captured
 * process parameters become the new process's: environment, standard handles, window and console choices,
 * and the runtime data the c library passes its file descriptors in.
 */
#include "../ntos.h"
#include <mime/wow32.h>

#define CONSOLE_DETACHED ((HANDLE)(LONG_PTR)-1)  /* DETACHED_PROCESS, as kernelbase marks it. */
#define CONSOLE_NEW ((HANDLE)(LONG_PTR)-2)       /* CREATE_NEW_CONSOLE. */
#define CONSOLE_NO_WINDOW ((HANDLE)(LONG_PTR)-3) /* CREATE_NO_WINDOW. */
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004
#define MAX_ENVIRONMENT (32u << 20)

/* the new process's parameters, in kernel memory. */
typedef struct PSP_PARAMS {
    UNICODE_STRING Image, CommandLine, CurrentDirectory, WindowTitle, DesktopInfo;
    PUCHAR RuntimeData; /* bytes, not text. */
    USHORT RuntimeDataLength;
    PWCHAR Environment; /* NULL for the default one. */
    SIZE_T EnvironmentBytes;
    HANDLE StandardInput, StandardOutput, StandardError;
    ULONG WindowFlags, ShowWindowFlags, ConsoleFlags;
    ULONG StartingX, StartingY, CountX, CountY, CountCharsX, CountCharsY, FillAttribute;
    BOOLEAN Detached;
    BOOLEAN NewConsole; /* the process gets a console of its own. */
    BOOLEAN NoWindow;   /* and nothing shows it. */
    HANDLE PtyHandle;   /* the creator's handle to the pseudo console the process attaches to. */
} PSP_PARAMS;

NTSTATUS PspCreateProcessObject(PEPROCESS parent, PEPROCESS *out);
void PspNotifyProcess(PEPROCESS process, BOOLEAN create);

/* user mode bump allocator used while building a new process. */
typedef struct USER_ARENA {
    PUCHAR Base;
    SIZE_T Used;
    SIZE_T Size;
} USER_ARENA;

static PVOID arena_alloc(USER_ARENA *a, SIZE_T size)
{
    SIZE_T off = (a->Used + 15) & ~(SIZE_T)15;
    if (off + size > a->Size)
        return nullptr;
    a->Used = off + size;
    return a->Base + off;
}

/* the next bytes, with no padding before them: the strings of the process parameters lie one after the other. */
static PVOID packed_alloc(USER_ARENA *a, SIZE_T size)
{
    if (a->Used + size > a->Size)
        return nullptr;
    PVOID at = a->Base + a->Used;
    a->Used += size;
    return at;
}

static BOOLEAN packed_string(USER_ARENA *a, PUNICODE_STRING dst, const WCHAR *src, SIZE_T chars)
{
    PWSTR buf = packed_alloc(a, (chars + 1) * sizeof(WCHAR));
    if (!buf)
        return FALSE;
    memcpy(buf, src, chars * sizeof(WCHAR));
    buf[chars] = 0;
    dst->Buffer = buf;
    dst->Length = (USHORT)(chars * sizeof(WCHAR));
    dst->MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    return TRUE;
}

/*
 * the 32-bit peb of a 32-bit process, below 2 GiB, mirroring what the 64-bit peb says about the process: the fields
 * MiInitializeWowPeb of ntoskrnl 26300 copies, the view of the api set schema MmMapApiSetView names in both pebs, and
 * the processor feature mask KeCopyXfdMaskToPeb writes to both. the process parameters follow, see
 * thunk_process_parameters.
 */
static NTSTATUS setup_wow64_peb(PEPROCESS process)
{
    PVOID base = nullptr;
    SIZE_T size = PAGE_SIZE;
    NTSTATUS status = MiAllocateVirtual(process, &base, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status))
        return status;
    PPEB native = process->Peb;
    MIME_PEB32 *peb = base;
    peb->ImageBaseAddress = (ULONG)(ULONG_PTR)native->ImageBaseAddress;
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
    peb->InheritedAddressSpace = native->InheritedAddressSpace;
    peb->BitField = native->BitField;
    peb->Mutant = (ULONG)(ULONG_PTR)native->Mutant;
    peb->NtGlobalFlag = native->NtGlobalFlag;
    peb->OSCSDVersion = native->OSCSDVersion;
    peb->ApiSetMap = (ULONG)(ULONG_PTR)native->ApiSetMap;
    /* a 32-bit heap reserves no more than 1 MiB unless the image asks for less than 1 GiB, and commits within that. */
    if (peb->HeapSegmentReserve > 0x40000000)
        peb->HeapSegmentReserve = 0x100000;
    if (peb->HeapSegmentCommit > peb->HeapSegmentReserve)
        peb->HeapSegmentCommit = 0x2000;
    PUCHAR raw = base;
    *(PULONG)(raw + 0x208) = (ULONG)native->MinimumStackCommit;
    *(PUSHORT)(raw + MIME_PEB32_ACTIVE_CODE_PAGE) = native->ActiveCodePage;
    *(PUSHORT)(raw + MIME_PEB32_OEM_CODE_PAGE) = native->OemCodePage;
    *(PUSHORT)(raw + MIME_PEB32_USE_CASE_MAPPING) = native->UseCaseMapping;
    *(PULONG)(raw + 0x470) = (ULONG)(ULONG_PTR)native->LeapSecondData;
    *(PULONG)(raw + 0x474) = native->LeapSecondFlags;
    *(PULONG)(raw + 0x478) = native->NtGlobalFlag2;
    *(PULONG64)(raw + 0x480) = native->ExtendedFeatureDisableMask;
    process->Wow64Peb = peb;
    return STATUS_SUCCESS;
}

/* copies a string of the 64-bit parameters to the 32-bit block whole, as long as its room. */
static void thunk_string(MIME_STRING32 *to, const UNICODE_STRING *from, PUCHAR *at)
{
    to->Length = from->Length;
    to->MaximumLength = from->MaximumLength;
    to->Buffer = 0;
    if (!from->Buffer)
        return;
    to->Buffer = (ULONG)(ULONG_PTR)*at;
    memcpy(*at, from->Buffer, from->MaximumLength);
    *at += from->MaximumLength;
}

/*
 * like PspWow64ThunkProcessParameters of ntoskrnl 26300: the 32-bit process parameters, a block of their own in
 * the 32-bit peb. the structure, then each string with the whole of its room in the order of the structure, then the
 * environment, which starts as far from the block as in the 64-bit one less what the structure is smaller by. the
 * runtime data keeps its count and flags, and each of its handles becomes 32 bits.
 */
static NTSTATUS thunk_process_parameters(PEPROCESS process)
{
    const RTL_USER_PROCESS_PARAMETERS *pp = process->Peb->ProcessParameters;
    MIME_PEB32 *peb32 = process->Wow64Peb;
    ULONG smaller = sizeof(RTL_USER_PROCESS_PARAMETERS) - sizeof(MIME_PARAMETERS32) - (pp->DefaultThreadpoolCpuSetMaskCount ? 4 : 0);
    ULONG strings = pp->MaximumLength - smaller;
    SIZE_T size = strings + pp->EnvironmentSize;
    PVOID base = nullptr;
    NTSTATUS status = MiAllocateVirtual(process, &base, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status))
        return status;
    MIME_PARAMETERS32 *p32 = base;
    PUCHAR at = (PUCHAR)(p32 + 1);
    p32->MaximumLength = p32->Length = strings + (ULONG)pp->EnvironmentSize;
    p32->Flags = pp->Flags;
    p32->DebugFlags = pp->DebugFlags;
    p32->ConsoleHandle = (ULONG)(ULONG_PTR)pp->ConsoleHandle;
    p32->ConsoleFlags = pp->ConsoleFlags;
    p32->StandardInput = (ULONG)(ULONG_PTR)pp->StandardInput;
    p32->StandardOutput = (ULONG)(ULONG_PTR)pp->StandardOutput;
    p32->StandardError = (ULONG)(ULONG_PTR)pp->StandardError;
    p32->StartingX = pp->StartingX;
    p32->StartingY = pp->StartingY;
    p32->CountX = pp->CountX;
    p32->CountY = pp->CountY;
    p32->CountCharsX = pp->CountCharsX;
    p32->CountCharsY = pp->CountCharsY;
    p32->FillAttribute = pp->FillAttribute;
    p32->WindowFlags = pp->WindowFlags;
    p32->ShowWindowFlags = pp->ShowWindowFlags;
    p32->LoaderThreads = pp->LoaderThreads;
    p32->DefaultThreadpoolThreadMaximum = pp->DefaultThreadpoolThreadMaximum;
    thunk_string(&p32->CurrentDirectory, &pp->CurrentDirectory.DosPath, &at);
    p32->CurrentDirectoryHandle = (ULONG)(ULONG_PTR)pp->CurrentDirectory.Handle;
    thunk_string(&p32->DllPath, &pp->DllPath, &at);
    thunk_string(&p32->ImagePathName, &pp->ImagePathName, &at);
    thunk_string(&p32->CommandLine, &pp->CommandLine, &at);
    thunk_string(&p32->WindowTitle, &pp->WindowTitle, &at);
    thunk_string(&p32->DesktopInfo, &pp->DesktopInfo, &at);
    thunk_string(&p32->ShellInfo, &pp->ShellInfo, &at);
    thunk_string(&p32->RedirectionDllName, &pp->RedirectionDllName, &at);
    thunk_string(&p32->HeapPartitionName, &pp->HeapPartitionName, &at);
    if (pp->RuntimeData.Length && pp->RuntimeData.Buffer) {
        const UCHAR *from = (const UCHAR *)pp->RuntimeData.Buffer;
        ULONG length = pp->RuntimeData.Length, count = length >= 4 ? *(const ULONG *)from : 0;
        ULONG64 wide = (ULONG64)count * 9 + 4;
        p32->RuntimeData.Buffer = (ULONG)(ULONG_PTR)at;
        if (length >= wide) {
            memcpy(at, from, count + 4);
            const ULONG64 *handles = (const ULONG64 *)(from + 4 + count);
            PULONG narrow = (PULONG)(at + 4 + count);
            for (ULONG i = 0; i < count; i++)
                narrow[i] = (ULONG)handles[i];
            memcpy(narrow + count, handles + count, length - wide);
            p32->RuntimeData.Length = (USHORT)(length - wide + 5 * count + 4);
        } else {
            memcpy(at, from, length);
            p32->RuntimeData.Length = (USHORT)length;
        }
        p32->RuntimeData.MaximumLength = p32->RuntimeData.Length;
        at += p32->RuntimeData.Length;
    }
    if (pp->DefaultThreadpoolCpuSetMaskCount) {
        at = (PUCHAR)(((ULONG_PTR)at + 3) & ~(ULONG_PTR)3);
        p32->DefaultThreadpoolCpuSetMaskCount = pp->DefaultThreadpoolCpuSetMaskCount;
        p32->DefaultThreadpoolCpuSetMasks = (ULONG)(ULONG_PTR)at;
        memcpy(at, pp->DefaultThreadpoolCpuSetMasks, 8ull * pp->DefaultThreadpoolCpuSetMaskCount);
    }
    PUCHAR environment = (PUCHAR)base + strings;
    p32->Environment = (ULONG)(ULONG_PTR)environment;
    p32->EnvironmentSize = (ULONG)pp->EnvironmentSize;
    p32->EnvironmentVersion = (ULONG)pp->EnvironmentVersion;
    p32->ProcessGroupId = pp->ProcessGroupId;
    memcpy(environment, pp->Environment, pp->EnvironmentSize);
    peb32->ProcessParameters = (ULONG)(ULONG_PTR)p32;
    return STATUS_SUCCESS;
}

/* windows keeps the peb in one page, and the teb of the first thread starts on the next. */
_Static_assert(sizeof(PEB) <= PAGE_SIZE, "the peb is one page");

static NTSTATUS setup_peb(PEPROCESS process, const PSP_PARAMS *p)
{
    PCUNICODE_STRING image = &p->Image, cmdline = &p->CommandLine, cwd = &p->CurrentDirectory;
    PWCHAR environment;
    SIZE_T environment_bytes;
    NTSTATUS status = STATUS_SUCCESS;
    if (p->Environment) {
        status = PspAdaptEnvironment(process->Machine, p->Environment, p->EnvironmentBytes, &environment, &environment_bytes);
    } else {
        status = PspBuildEnvironment(process->Machine, &environment, &environment_bytes);
    }
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T size = 64 * 1024 + environment_bytes + p->RuntimeDataLength + p->WindowTitle.Length + p->DesktopInfo.Length;
    PVOID base = nullptr, peb_page = nullptr;
    /*
     * like windows, the peb is a page among the tebs, see MiAllocatePebOrTeb, and the parameters are an allocation
     * of their own where a search from below starts, which the system dll releases whole once it has copied them
     * to its heap. the peb of a 32-bit program is a block of its own, as before.
     */
    if (process->Machine == IMAGE_FILE_MACHINE_I386) {
        SIZE_T peb_size = PAGE_SIZE;
        status = MiAllocateThreadMemory(process, &peb_page, &peb_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    } else {
        status = MiAllocatePebOrTeb(process, sizeof(PEB), TRUE, &peb_page);
    }
    if (NT_SUCCESS(status))
        status = MiAllocateVirtual(process, &base, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        ExFreePool(environment);
        return status;
    }
    USER_ARENA a = {base, 0, size};
    PPEB peb = peb_page;
    PRTL_USER_PROCESS_PARAMETERS pp = arena_alloc(&a, sizeof(RTL_USER_PROCESS_PARAMETERS));
    if (!peb || !pp) {
        ExFreePool(environment);
        return STATUS_NO_MEMORY;
    }
    memset(peb, 0, sizeof(*peb));
    memset(pp, 0, sizeof(*pp));
    PSECTION img = process->ImageSection;

    /* like MmCreatePeb on 26300: no mutant, the relocation bit of an image away from its base, an empty worker list. */
    peb->Mutant = (HANDLE)-1;
    if ((ULONG_PTR)process->SectionBaseAddress != img->Image.ImageBase)
        peb->BitField |= PEB_IMAGE_DYNAMICALLY_RELOCATED;
    InitializeListHead(&peb->TppWorkerpList);
    peb->ImageBaseAddress = process->SectionBaseAddress;
    peb->ProcessParameters = pp;
    peb->NumberOfProcessors = HalBootInfo->cpu_count;
    peb->OSMajorVersion = MIME_NT_MAJOR_VERSION;
    peb->OSMinorVersion = MIME_NT_MINOR_VERSION;
    peb->OSBuildNumber = MIME_NT_BUILD_NUMBER;
    peb->OSPlatformId = 2;
    peb->ImageSubsystem = img->Image.Subsystem;
    peb->ImageSubsystemMajorVersion = img->Image.SubsystemMajor;
    peb->ImageSubsystemMinorVersion = img->Image.SubsystemMinor;
    peb->ActiveProcessAffinityMask = KiProcessAffinity(&process->Pcb);
    peb->CriticalSectionTimeout.QuadPart = -2592000ll * 10000000; /* 30 days. */
    peb->HeapSegmentReserve = 0x100000;
    peb->HeapSegmentCommit = 0x2000;
    peb->HeapDeCommitTotalFreeThreshold = 0x10000;
    peb->HeapDeCommitFreeBlockThreshold = 0x1000;
    peb->MaximumNumberOfHeaps = 16;
    peb->SessionId = process->SessionId;
    peb->ActiveCodePage = HalBootInfo->ansi_code_page ? HalBootInfo->ansi_code_page : 1252;
    peb->OemCodePage = HalBootInfo->oem_code_page ? HalBootInfo->oem_code_page : 437;
    peb->UseCaseMapping = 0x6a5; /* read on 26300, the same in every process. */
    extern BOOLEAN PspLoaderLog;
    if (PspLoaderLog) {
        peb->NtGlobalFlag |= 2; /* test: let the loader DbgPrint its own log. */
        SharedUserData->KdDebuggerEnabled = 3; /* test: prints go through int 2d. */
    }

    pp->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    pp->ShowWindowFlags = p->ShowWindowFlags;
    pp->WindowFlags = p->WindowFlags;
    pp->ConsoleFlags = p->ConsoleFlags;
    pp->ConsoleHandle = p->Detached ? CONSOLE_DETACHED : p->NewConsole ? CONSOLE_NEW : nullptr;
    pp->StandardInput = p->StandardInput;
    pp->StandardOutput = p->StandardOutput;
    pp->StandardError = p->StandardError;
    pp->StartingX = p->StartingX;
    pp->StartingY = p->StartingY;
    pp->CountX = p->CountX;
    pp->CountY = p->CountY;
    pp->CountCharsX = p->CountCharsX;
    pp->CountCharsY = p->CountCharsY;
    pp->FillAttribute = p->FillAttribute;
    PCUNICODE_STRING title = p->WindowTitle.Length ? &p->WindowTitle : image;
    /*
     * measured on windows 11 26300 in a suspended child, scratch/dlls/probe/ppchild.c of the research repository: the
     * block starts its allocation and is one piece. the strings follow the structure with nothing between them, the
     * current directory first with the room of a whole path, and both lengths tell the structure and the strings.
     * the dll path is empty, a program without runtime data has no buffer for it, and the environment begins where
     * the block ends. the loader of windows copies as many bytes as the length tells to its heap, moves each string
     * by the same step and frees the allocation.
     */
    /* like windows, the current directory always ends with a backslash. */
    WCHAR dir[MIME_IMAGE_NAME_CHARS + 2];
    SIZE_T dlen = cwd->Length / sizeof(WCHAR);
    if (dlen > MIME_IMAGE_NAME_CHARS - 2)
        dlen = MIME_IMAGE_NAME_CHARS - 2;
    memcpy(dir, cwd->Buffer, dlen * sizeof(WCHAR));
    if (!dlen || dir[dlen - 1] != '\\')
        dir[dlen++] = '\\';
    /* the room SetCurrentDirectory grows the path in. */
    PWSTR current = packed_alloc(&a, MIME_IMAGE_NAME_CHARS * sizeof(WCHAR));
    BOOLEAN ok = current != nullptr;
    if (ok) {
        memset(current, 0, MIME_IMAGE_NAME_CHARS * sizeof(WCHAR));
        memcpy(current, dir, dlen * sizeof(WCHAR));
        pp->CurrentDirectory.DosPath.Buffer = current;
        pp->CurrentDirectory.DosPath.Length = (USHORT)(dlen * sizeof(WCHAR));
        pp->CurrentDirectory.DosPath.MaximumLength = (USHORT)(MIME_IMAGE_NAME_CHARS * sizeof(WCHAR));
    }
    ok = ok && packed_string(&a, &pp->ImagePathName, image->Buffer, image->Length / sizeof(WCHAR)) &&
         packed_string(&a, &pp->CommandLine, cmdline->Buffer, cmdline->Length / sizeof(WCHAR)) &&
         packed_string(&a, &pp->WindowTitle, title->Buffer, title->Length / sizeof(WCHAR)) &&
         (p->DesktopInfo.Length
              ? packed_string(&a, &pp->DesktopInfo, p->DesktopInfo.Buffer, p->DesktopInfo.Length / sizeof(WCHAR))
              : packed_string(&a, &pp->DesktopInfo, L"WinSta0\\Default", 15)) &&
         packed_string(&a, &pp->ShellInfo, L"", 0);
    /* the runtime data is bytes: the c library's inherited file descriptors. */
    if (ok && p->RuntimeDataLength) {
        PUCHAR runtime = packed_alloc(&a, (p->RuntimeDataLength + sizeof(WCHAR) + 1) & ~(SIZE_T)1);
        ok = runtime != nullptr;
        if (ok) {
            memcpy(runtime, p->RuntimeData, p->RuntimeDataLength);
            memset(runtime + p->RuntimeDataLength, 0, sizeof(WCHAR));
            pp->RuntimeData.Buffer = (PWSTR)runtime;
            pp->RuntimeData.Length = p->RuntimeDataLength;
            pp->RuntimeData.MaximumLength = (USHORT)(p->RuntimeDataLength + sizeof(WCHAR));
        }
    }
    pp->MaximumLength = pp->Length = (ULONG)(a.Base + a.Used - (PUCHAR)pp);
    PWSTR env = packed_alloc(&a, environment_bytes);
    if (ok && env)
        memcpy(env, environment, environment_bytes);
    ExFreePool(environment);
    if (!ok || !env)
        return STATUS_NO_MEMORY;
    pp->Environment = env;
    pp->EnvironmentSize = environment_bytes;
    status = PspMapPebBlocks(process, peb);
    if (!NT_SUCCESS(status))
        return status;
    process->Peb = peb;
    return STATUS_SUCCESS;
}

/* Z:\a\b.exe becomes \??\Z:\a\b.exe. */
static NTSTATUS dos_to_nt(PCUNICODE_STRING dos, PUNICODE_STRING nt)
{
    static const WCHAR prefix[] = L"\\??\\";
    SIZE_T chars = dos->Length / sizeof(WCHAR) + 4;
    nt->Buffer = ExAllocatePool2(POOL_FLAG_PAGED, (chars + 1) * sizeof(WCHAR), KI_TAG('P', 's', 'N', 'p'));
    if (!nt->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(nt->Buffer, prefix, 8);
    memcpy(nt->Buffer + 4, dos->Buffer, dos->Length);
    nt->Buffer[chars] = 0;
    nt->Length = (USHORT)(chars * sizeof(WCHAR));
    nt->MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static NTSTATUS create_process(const PSP_PARAMS *p, struct _TOKEN *token, ULONG64 request_id, MIME_CONSOLE *console,
                               BOOLEAN suspended, PEPROCESS parent, PEPROCESS *out, PETHREAD *initial_thread,
                               SD_BLOB *process_offer, SD_BLOB *thread_offer, KPROCESSOR_MODE mode)
{
    PCUNICODE_STRING image = &p->Image;
    UNICODE_STRING nt_path;
    NTSTATUS status = dos_to_nt(image, &nt_path);
    if (!NT_SUCCESS(status))
        return status;
    PFILE_OBJECT file;
    status = IopOpenFileObject(&nt_path, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file);
    ExFreePool(nt_path.Buffer);
    if (!NT_SUCCESS(status))
        return status;
    PSECTION section;
    status = MiCreateImageSection(file, &section);
    ObDereferenceObject(file);
    if (!NT_SUCCESS(status))
        return status;
    if (section->Image.Characteristics & IMAGE_FILE_DLL) {
        ObDereferenceObject(section);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    /* managed code for any processor runs in a process of the machine's width. */
    USHORT machine = MiImageIsPortableIl(section) ? MIME_NATIVE_MACHINE : section->Image.Machine;
    PPSP_SYSTEM_DLL system_dll = PspSystemDllForMachine(machine);
    if (!system_dll) {
        ObDereferenceObject(section);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    PEPROCESS process;
    status = PspCreateProcessObject(parent, &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    /* the token given for the new process, or else the creator's. */
    if (token) {
        SepSetProcessToken(process, token);
    } else {
        status = SepAssignProcessToken(process, parent);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(section);
            PspTerminateProcess(process, status);
            ObDereferenceObject(process);
            return status;
        }
    }
    process->SessionId = SepTokenSessionId(process);
    process->ImageSection = section;
    process->Subsystem = section->Image.Subsystem;
    process->SystemDll = system_dll;
    process->Machine = machine;
    /* windows 11 26300: the error mode of the parent goes on to the process; one launched by the shell reads 1. */
    process->DefaultHardErrorMode = parent ? parent->DefaultHardErrorMode : 0x8000;
    if (machine != IMAGE_FILE_MACHINE_I386 &&
        (section->Image.DllCharacteristics & (IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA)) ==
            (IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA))
        MiRandomizeAddressSpace(&process->AddressSpace);
    if (machine != IMAGE_FILE_MACHINE_I386 && (section->Image.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
        MiRandomizeTopDown(&process->AddressSpace);
    /* a large address aware 32-bit program has the whole 4 GiB on 64-bit windows. */
    if (process->Machine == IMAGE_FILE_MACHINE_I386)
        process->AddressSpace.AllocationLimit =
            section->Image.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE ? 0xFFFF0000 : 0x7FFF0000;
    process->MimeRequestId = request_id;
    /* a program that cmd's start waits for tells its debug output where the run of cmd does. */
    process->MimeDebugRequest = request_id ? request_id : parent ? parent->MimeDebugRequest : 0;
    MimeSessionUserProgram(process, parent);
    /* a gui program gets no console. a console program shares the one it is given. */
    if (!p->Detached && section->Image.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
        MIME_CONSOLE *pty = p->PtyHandle ? ConsoleOfPtyHandle(p->PtyHandle) : nullptr;
        if (pty) {
            if (NT_SUCCESS(ConsoleAttach(process, pty)))
                process->ConsoleShared = TRUE;
            ConsoleInitialTitle(pty, p->WindowTitle.Length ? &p->WindowTitle : image);
            ConsoleDereference(pty);
        } else if (p->NewConsole || (!console && !p->NoWindow)) {
            /* a console of its own, in a window of the user's session unless it was asked for without one. */
            BOOLEAN window = !p->NoWindow && process->SessionId != 0;
            CON_STARTUP startup = {p->WindowFlags, p->ShowWindowFlags, (LONG)p->StartingX, (LONG)p->StartingY, p->CountX,
                                   p->CountY, p->CountCharsX, p->CountCharsY, p->FillAttribute};
            MIME_CONSOLE *own = window ? ConsoleCreateWindow(&startup) : ConsoleCreateNew();
            if (own) {
                ConsoleAttach(process, own);
                ConsoleInitialTitle(own, p->WindowTitle.Length ? &p->WindowTitle : image);
                if (window)
                    ConsoleStartHost(own, parent);
                ConsoleDereference(own);
            }
        } else if (console) {
            if (NT_SUCCESS(ConsoleAttach(process, console)))
                process->ConsoleShared = TRUE;
            ConsoleInitialTitle(console, p->WindowTitle.Length ? &p->WindowTitle : image);
        }
    }
    /* the terminal of the run it descends from stays available to AllocConsole. */
    MIME_CONSOLE *hint = parent && parent->ConsoleHint ? parent->ConsoleHint : console;
    if (hint)
        ConsoleSetHint(process, hint);
    SIZE_T n = image->Length / sizeof(WCHAR);
    if (n >= MIME_IMAGE_NAME_CHARS)
        n = MIME_IMAGE_NAME_CHARS - 1;
    memcpy(process->ImagePathBuffer, image->Buffer, n * sizeof(WCHAR));
    process->ImagePathBuffer[n] = 0;
    RtlInitUnicodeString(&process->ImagePath, process->ImagePathBuffer);
    SIZE_T slash = n;
    while (slash > 0 && image->Buffer[slash - 1] != '\\')
        slash--;
    for (SIZE_T i = 0; i < 15 && slash + i < n; i++)
        process->ImageFileName[i] = (CHAR)image->Buffer[slash + i];

    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    PVOID base = nullptr;
    SIZE_T view = 0;
    status = MiMapViewOfSection(section, process, &base, 0, &view, PAGE_EXECUTE_WRITECOPY, 0);
    if (NT_SUCCESS(status)) {
        process->SectionBaseAddress = base;
        PVOID ntdll = nullptr;
        SIZE_T ntdll_view = 0;
        status = MiMapViewOfSection(system_dll->Section, process, &ntdll, 0, &ntdll_view, PAGE_EXECUTE_WRITECOPY, 0);
        if (status == STATUS_IMAGE_NOT_AT_BASE) {
            /* the system dll must sit at the base of its section, since the kernel caches its exports. */
            status = STATUS_CONFLICTING_ADDRESSES;
        }
        process->NtdllBase = ntdll;
    }
    /* a 32-bit program also gets the 32-bit ntdll, at its base, so a parent can patch it before the child runs. */
    PVOID ntdll32 = nullptr;
    if (NT_SUCCESS(status) && process->Machine == IMAGE_FILE_MACHINE_I386 && PspWow64NtdllSection) {
        SIZE_T ntdll32_view = 0;
        status = MiMapViewOfSection(PspWow64NtdllSection, process, &ntdll32, 0, &ntdll32_view, PAGE_EXECUTE_WRITECOPY, 0);
        if (status == STATUS_IMAGE_NOT_AT_BASE)
            status = STATUS_SUCCESS;
    }
    /* so does an x64 one on arm64 with the x64 ntdll. elsewhere than its base, xta64 maps and relocates its own. */
    if (NT_SUCCESS(status) && process->Machine == IMAGE_FILE_MACHINE_AMD64 && PspX64NtdllSection) {
        SIZE_T view = 0;
        NTSTATUS mapped = MiMapViewOfSection(PspX64NtdllSection, process, &ntdll32, 0, &view, PAGE_EXECUTE_WRITECOPY, 0);
        if (mapped == STATUS_IMAGE_NOT_AT_BASE)
            MiUnmapViewOfSection(process, ntdll32);
        if (mapped != STATUS_SUCCESS)
            ntdll32 = nullptr;
    }
    if (NT_SUCCESS(status))
        status = setup_peb(process, p);
    /* the first program of a run allocates its console, which is the run's terminal, and gets the redirected streams. */
    if (NT_SUCCESS(status) && request_id && process->Console) {
        HANDLE streams[3];
        status = ConsoleCreateStreamHandles(process, streams);
        PRTL_USER_PROCESS_PARAMETERS pp = process->Peb->ProcessParameters;
        if (NT_SUCCESS(status) && (streams[0] || streams[1] || streams[2])) {
            pp->StandardInput = streams[0];
            pp->StandardOutput = streams[1];
            pp->StandardError = streams[2];
            pp->WindowFlags |= 0x100; /* STARTF_USESTDHANDLES */
        }
    }
    /* wow64.dll and xta64 find the guest ntdll through a spare peb pointer. */
    if (NT_SUCCESS(status) && ntdll32)
        process->Peb->SparePointers[0] = ntdll32;
    /* every 32-bit process has its 32-bit peb, which wow64.dll on x64 and xta64 on arm64 complete. */
    if (NT_SUCCESS(status) && process->Machine == IMAGE_FILE_MACHINE_I386)
        status = setup_wow64_peb(process);
    if (NT_SUCCESS(status) && PspIsWow64Process(process))
        status = thunk_process_parameters(process);
    if (NT_SUCCESS(status))
        status = PspPrepareSystemDllInitBlocks(process, ntdll32);
    KiAttachProcess(saved);
    /* what csrss does for the program that starts another, before the first thread of the new one runs. */
    if (NT_SUCCESS(status) && (section->Image.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ||
                               section->Image.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI))
        status = PspSxsCreateProcessContext(process, image);

    PETHREAD thread = nullptr;
    if (NT_SUCCESS(status)) {
        PVOID entry = (PUCHAR)process->SectionBaseAddress + section->Image.EntryPoint;
        status = PspCreateUserThread(process, entry, process->Peb, suspended, section->Image.StackReserve,
                                     section->Image.StackCommit, &thread);
    }
    if (!NT_SUCCESS(status)) {
        PspTerminateProcess(process, status);
        ObDereferenceObject(process);
        return status;
    }
    /* the descriptors come from the identity the process runs as. */
    status = SepAssignNewProcessSecurity(process, thread, process_offer, thread_offer, mode);
    if (!NT_SUCCESS(status)) {
        PspTerminateProcess(process, status);
        ObDereferenceObject(thread);
        ObDereferenceObject(process);
        return status;
    }
    status = PspNotifyProcessCreate(process, image, &p->CommandLine);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("a driver refused process %u (%wZ): %08x", (ULONG)(ULONG_PTR)process->UniqueProcessId, image, status);
        PspTerminateProcess(process, status);
        ObDereferenceObject(thread);
        ObDereferenceObject(process);
        return status;
    }
    /* the arguments help tell processes of one program apart in the log. */
    UNICODE_STRING shown = p->CommandLine;
    if (shown.Length > 300 * sizeof(WCHAR))
        shown.Length = 300 * sizeof(WCHAR);
    KiLogPrintf("created process %u (%wZ) %wZ", (ULONG)(ULONG_PTR)process->UniqueProcessId, image, &shown);
    if (initial_thread)
        *initial_thread = thread;
    else
        ObDereferenceObject(thread);
    *out = process;
    return STATUS_SUCCESS;
}

/*
 * the service control manager starts with the machine, in session 0 with a system token. a machine without
 * services.exe runs without services.
 */
NTSTATUS PsStartServiceController(void)
{
    UNICODE_STRING image = RTL_CONSTANT_STRING(L"C:\\Windows\\System32\\services.exe");
    UNICODE_STRING cmdline = RTL_CONSTANT_STRING(L"C:\\Windows\\system32\\services.exe");
    UNICODE_STRING cwd = RTL_CONSTANT_STRING(L"C:\\Windows\\system32");
    PSP_PARAMS p = {.Image = image, .CommandLine = cmdline, .CurrentDirectory = cwd, .Detached = TRUE};
    struct _TOKEN *token = SepCreateServiceControllerToken();
    if (!token)
        return STATUS_INSUFFICIENT_RESOURCES;
    PEPROCESS process;
    NTSTATUS status = create_process(&p, token, 0, nullptr, FALSE, PsCurrentProcess(), &process, nullptr, nullptr, nullptr, KernelMode);
    ObDereferenceObject(token);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot start the service control manager: %08x", status);
        return status;
    }
    process->ServiceController = TRUE;
    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

/*
 * the process that draws a console window, conhost.exe, runs as the program that asked for the console. it finds its
 * console by the number on its command line.
 */
NTSTATUS PsStartConsoleHost(PEPROCESS parent, ULONG64 console, PULONG pid)
{
    static const WCHAR prefix[] = L"C:\\Windows\\System32\\conhost.exe 0xffffffff -ForceV1 -Console 0x";
    WCHAR command[128];
    SIZE_T n = RTL_NUMBER_OF(prefix) - 1;
    memcpy(command, prefix, n * sizeof(WCHAR));
    for (int shift = 60; shift >= 0; shift -= 4)
        command[n++] = L"0123456789abcdef"[(console >> shift) & 15];
    command[n] = 0;
    UNICODE_STRING image = RTL_CONSTANT_STRING(L"C:\\Windows\\System32\\conhost.exe");
    UNICODE_STRING cwd = RTL_CONSTANT_STRING(L"C:\\Windows\\System32");
    UNICODE_STRING cmdline = {(USHORT)(n * sizeof(WCHAR)), (USHORT)((n + 1) * sizeof(WCHAR)), command};
    PSP_PARAMS p = {.Image = image, .CommandLine = cmdline, .CurrentDirectory = cwd, .Detached = TRUE,
                    .ShowWindowFlags = 1 /* SW_SHOWNORMAL */};
    PEPROCESS process;
    NTSTATUS status = create_process(&p, nullptr, 0, nullptr, FALSE, parent, &process, nullptr, nullptr, nullptr, KernelMode);
    if (!NT_SUCCESS(status))
        return status;
    *pid = (ULONG)(ULONG_PTR)process->UniqueProcessId;
    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd, ULONG64 request_id,
                             MIME_CONSOLE *console, BOOLEAN elevated, PEPROCESS *out)
{
    UNICODE_STRING default_cwd = RTL_CONSTANT_STRING(L"C:\\");
    PSP_PARAMS p = {.Image = *image, .CommandLine = *cmdline, .CurrentDirectory = cwd ? *cwd : default_cwd,
                    .ShowWindowFlags = 1 /* SW_SHOWNORMAL */};
    /* all ones for the console asks for none at all, as DETACHED_PROCESS does. */
    if (console == (MIME_CONSOLE *)(LONG_PTR)-1) {
        p.Detached = TRUE;
        console = nullptr;
    }
    struct _TOKEN *token = elevated ? SepCreateElevatedUserToken() : nullptr;
    if (elevated && !token)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = create_process(&p, token, request_id, console, FALSE, PsCurrentProcess(), out, nullptr, nullptr, nullptr, KernelMode);
    if (token)
        ObDereferenceObject(token);
    return status;
}

static void free_params(PSP_PARAMS *p)
{
    KiFreeCapturedString(&p->Image);
    KiFreeCapturedString(&p->CommandLine);
    KiFreeCapturedString(&p->CurrentDirectory);
    KiFreeCapturedString(&p->WindowTitle);
    KiFreeCapturedString(&p->DesktopInfo);
    if (p->RuntimeData)
        ExFreePool(p->RuntimeData);
    if (p->Environment)
        ExFreePool(p->Environment);
}

/* takes the image from an attribute instead of the parameters, dropping the \??\ prefix. */
static NTSTATUS replace_image(PSP_PARAMS *p, PCUNICODE_STRING image)
{
    SIZE_T chars = image->Length / sizeof(WCHAR), skip = 0;
    if (chars >= 4 && !memcmp(image->Buffer, L"\\??\\", 4 * sizeof(WCHAR)))
        skip = 4;
    PWSTR copy = ExAllocatePool2(POOL_FLAG_PAGED, (chars - skip + 1) * sizeof(WCHAR), KI_TAG('C', 'a', 'p', 'S'));
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(copy, image->Buffer + skip, (chars - skip) * sizeof(WCHAR));
    copy[chars - skip] = 0;
    KiFreeCapturedString(&p->Image);
    p->Image.Buffer = copy;
    p->Image.Length = (USHORT)((chars - skip) * sizeof(WCHAR));
    p->Image.MaximumLength = (USHORT)(p->Image.Length + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* reads the creator's parameters once from its memory. */
static NTSTATUS capture_params(PRTL_USER_PROCESS_PARAMETERS user, PSP_PARAMS *p)
{
    RTL_USER_PROCESS_PARAMETERS pp;
    NTSTATUS status = KiCopyFromUser(&pp, user, sizeof(pp));
    if (!NT_SUCCESS(status))
        return status;
    /* denormalized parameters, as RtlCreateProcessParameters gives them, hold offsets from the block. */
    if (!(pp.Flags & RTL_USER_PROC_PARAMS_NORMALIZED)) {
        UNICODE_STRING *strings[] = {&pp.ImagePathName, &pp.CommandLine, &pp.CurrentDirectory.DosPath,
                                     &pp.WindowTitle, &pp.DesktopInfo, &pp.RuntimeData};
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(strings); i++)
            if (strings[i]->Buffer)
                strings[i]->Buffer = (PWSTR)((ULONG_PTR)strings[i]->Buffer + (ULONG_PTR)user);
    }
    status = KiCaptureUnicodeStringValue(&p->Image, pp.ImagePathName);
    if (NT_SUCCESS(status))
        status = KiCaptureUnicodeStringValue(&p->CommandLine, pp.CommandLine);
    if (NT_SUCCESS(status))
        status = KiCaptureUnicodeStringValue(&p->CurrentDirectory, pp.CurrentDirectory.DosPath);
    if (NT_SUCCESS(status) && pp.WindowTitle.Length)
        status = KiCaptureUnicodeStringValue(&p->WindowTitle, pp.WindowTitle);
    if (NT_SUCCESS(status) && pp.DesktopInfo.Length)
        status = KiCaptureUnicodeStringValue(&p->DesktopInfo, pp.DesktopInfo);
    if (NT_SUCCESS(status) && pp.RuntimeData.Length && pp.RuntimeData.Buffer) {
        p->RuntimeData = ExAllocatePool2(POOL_FLAG_NON_PAGED, pp.RuntimeData.Length, KI_TAG('P', 's', 'R', 'd'));
        status = p->RuntimeData ? KiCopyFromUser(p->RuntimeData, pp.RuntimeData.Buffer, pp.RuntimeData.Length)
                                : STATUS_NO_MEMORY;
        p->RuntimeDataLength = pp.RuntimeData.Length;
    }
    /* an environment ends with an empty string. its size, when given, bounds the copy. */
    if (NT_SUCCESS(status) && pp.Environment) {
        SIZE_T bytes = pp.EnvironmentSize;
        if (!bytes || bytes > MAX_ENVIRONMENT) {
            WCHAR pair[2] = {1, 1};
            for (bytes = 0; NT_SUCCESS(status) && bytes + sizeof(pair) <= MAX_ENVIRONMENT; bytes += sizeof(WCHAR)) {
                status = KiCopyFromUser(pair, (PUCHAR)pp.Environment + bytes, sizeof(pair));
                if (!pair[0] && !pair[1])
                    break;
            }
            bytes += sizeof(pair);
        }
        p->Environment = NT_SUCCESS(status) ? ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, KI_TAG('P', 's', 'E', 'n')) : nullptr;
        if (NT_SUCCESS(status))
            status = p->Environment ? KiCopyFromUser(p->Environment, pp.Environment, bytes) : STATUS_NO_MEMORY;
        p->EnvironmentBytes = bytes;
    }
    p->StandardInput = pp.StandardInput;
    p->StandardOutput = pp.StandardOutput;
    p->StandardError = pp.StandardError;
    p->WindowFlags = pp.WindowFlags;
    p->ShowWindowFlags = pp.ShowWindowFlags;
    p->ConsoleFlags = pp.ConsoleFlags;
    p->StartingX = pp.StartingX;
    p->StartingY = pp.StartingY;
    p->CountX = pp.CountX;
    p->CountY = pp.CountY;
    p->CountCharsX = pp.CountCharsX;
    p->CountCharsY = pp.CountCharsY;
    p->FillAttribute = pp.FillAttribute;
    p->Detached = pp.ConsoleHandle == CONSOLE_DETACHED;
    p->NewConsole = pp.ConsoleHandle == CONSOLE_NEW || pp.ConsoleHandle == CONSOLE_NO_WINDOW;
    p->NoWindow = pp.ConsoleHandle == CONSOLE_NO_WINDOW;
    if (pp.ConsoleHandle && pp.ConsoleHandle != CONSOLE_DETACHED && !p->NewConsole)
        p->PtyHandle = pp.ConsoleHandle;
    return status;
}

/*
 * NtCreateUserProcess's attribute list. an attribute is a number, a size, a value and an optional length
 * pointer. an input attribute whose number has the additive bit carries its value, the others point at it.
 * mime takes the token, parent process, handle list, job list, mitigation options, child process policy,
 * win32k filter, app container policies, and the client id and teb address outputs. the image name
 * replaces the parameters' image. every other attribute Windows knows is size checked and ignored.
 */
#define PS_ATTRIBUTE_PARENT_PROCESS 0x00060000u
#define PS_ATTRIBUTE_DEBUG_PORT 0x00060001u
#define PS_ATTRIBUTE_CLIENT_ID 0x00010003u
#define PS_ATTRIBUTE_TEB_ADDRESS 0x00010004u
#define PS_ATTRIBUTE_IMAGE_NAME 0x00020005u
#define PS_ATTRIBUTE_IMAGE_INFO 0x00000006u
#define PS_ATTRIBUTE_MEMORY_RESERVE 0x00020007u
#define PS_ATTRIBUTE_PRIORITY_CLASS 0x00020008u
#define PS_ATTRIBUTE_ERROR_MODE 0x00020009u
#define PS_ATTRIBUTE_STD_HANDLE_INFO 0x0002000Au
#define PS_ATTRIBUTE_HANDLE_LIST 0x0002000Bu
#define PS_ATTRIBUTE_GROUP_AFFINITY 0x0003000Cu
#define PS_ATTRIBUTE_PREFERRED_NODE 0x0002000Du
#define PS_ATTRIBUTE_IDEAL_PROCESSOR 0x0003000Eu
#define PS_ATTRIBUTE_MITIGATION_OPTIONS 0x00020010u
#define PS_ATTRIBUTE_PROTECTION_LEVEL 0x00060011u
#define PS_ATTRIBUTE_JOB_LIST 0x00020013u
#define PS_ATTRIBUTE_CHILD_PROCESS_POLICY 0x00020014u
#define PS_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY 0x00020015u
#define PS_ATTRIBUTE_WIN32K_FILTER 0x00020016u
#define PS_ATTRIBUTE_SAFE_OPEN_PROMPT_ORIGIN_CLAIM 0x00020017u
#define PS_ATTRIBUTE_BNO_ISOLATION 0x00020018u
#define PS_ATTRIBUTE_DESKTOP_APP_POLICY 0x00020019u
#define PS_ATTRIBUTE_CHPE 0x0006001Au
#define PS_ATTRIBUTE_MITIGATION_AUDIT_OPTIONS 0x0002001Bu
#define PS_ATTRIBUTE_MACHINE_TYPE 0x0006001Cu
#define PS_ATTRIBUTE_COMPONENT_FILTER 0x0002001Du
#define PS_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES 0x0003001Eu

#define PS_ATTRIBUTE_MAX_COUNT 32
#define PS_HANDLES_MAX 0x10000

typedef struct PSP_ATTRIBUTES {
    HANDLE Token;
    HANDLE Parent;
    PHANDLE HandleList; /* kernel copies, sorted. */
    ULONG HandleCount;
    BOOLEAN HasHandleList;
    PHANDLE JobList;
    ULONG JobCount;
    BOOLEAN HasMitigation;
    ULONG64 Mitigation[3];
    BOOLEAN HasChildPolicy;
    ULONG ChildPolicy;
    ULONG Win32kFilter[2];
    ULONG AllApplicationPackages, DesktopApp, ComponentFilter;
    BOOLEAN HasErrorMode;
    ULONG ErrorMode;
    UNICODE_STRING Image;    /* the image name, captured. */
    PVOID ClientId;          /* where the process and thread ids go, and their length. */
    PSIZE_T ClientIdLength;
    PVOID TebAddress;
    PSIZE_T TebAddressLength;
    PVOID ImageInfo; /* where the SECTION_IMAGE_INFORMATION of the image goes, and its length. */
    PSIZE_T ImageInfoLength;
    ULONG StdHandleFlags; /* PS_STD_HANDLE_INFO: the duplicate state in bits 0 and 1, pseudo handles in bits 2 to 4. */
    ULONG StdHandleSubsystem;
} PSP_ATTRIBUTES;

static void free_attributes(PSP_ATTRIBUTES *a)
{
    if (a->HandleList)
        ExFreePool(a->HandleList);
    if (a->JobList)
        ExFreePool(a->JobList);
    KiFreeCapturedString(&a->Image);
}

/* copies a list of 8-byte handles into the kernel. */
static NTSTATUS capture_handles(const PS_ATTRIBUTE *attribute, PHANDLE *list, ULONG *count)
{
    SIZE_T size = attribute->Size;
    if (!size || (size & 7) || size > PS_HANDLES_MAX * sizeof(HANDLE))
        return STATUS_INVALID_PARAMETER;
    PHANDLE copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('P', 's', 'H', 'l'));
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(copy, attribute->ValuePtr, size);
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy);
        return status;
    }
    *list = copy;
    *count = (ULONG)(size / sizeof(HANDLE));
    return STATUS_SUCCESS;
}

/* sorted by the handle's low word, the order inheritance expects. */
static void sort_handles(PHANDLE list, ULONG count)
{
    for (ULONG gap = count / 2; gap; gap /= 2) {
        for (ULONG i = gap; i < count; i++) {
            HANDLE value = list[i];
            ULONG j = i;
            for (; j >= gap && (ULONG)(ULONG_PTR)list[j - gap] > (ULONG)(ULONG_PTR)value; j -= gap)
                list[j] = list[j - gap];
            list[j] = value;
        }
    }
}

static NTSTATUS capture_one(const PS_ATTRIBUTE *attribute, PSP_ATTRIBUTES *a)
{
    ULONG number = (ULONG)attribute->Attribute;
    SIZE_T size = attribute->Size;
    ULONG word = 0;
    NTSTATUS status;
    /* reads a value of exactly the given size. */
#define VALUE(length, destination) \
    if (size != (length)) return STATUS_INVALID_PARAMETER; \
    status = KiCopyFromUser((destination), attribute->ValuePtr, (length)); \
    if (!NT_SUCCESS(status)) return status
    switch (number) {
    case PS_ATTRIBUTE_PARENT_PROCESS:
        if (size != sizeof(HANDLE))
            return STATUS_INVALID_PARAMETER;
        a->Parent = attribute->ValuePtr;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_DEBUG_PORT:
        return STATUS_NOT_SUPPORTED;
    case PS_ATTRIBUTE_TOKEN:
        if (size != sizeof(HANDLE))
            return STATUS_INFO_LENGTH_MISMATCH;
        a->Token = attribute->ValuePtr;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_CLIENT_ID:
        if (size != 16)
            return STATUS_INVALID_PARAMETER;
        a->ClientId = attribute->ValuePtr;
        a->ClientIdLength = attribute->ReturnLength;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_TEB_ADDRESS:
        if (size != sizeof(PVOID))
            return STATUS_INVALID_PARAMETER;
        a->TebAddress = attribute->ValuePtr;
        a->TebAddressLength = attribute->ReturnLength;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_IMAGE_NAME:
        if (!size || (size & 1) || size > 0xFFFE)
            return STATUS_INVALID_PARAMETER;
        a->Image.Buffer = ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(WCHAR), KI_TAG('P', 's', 'I', 'm'));
        if (!a->Image.Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        a->Image.Length = (USHORT)size;
        a->Image.MaximumLength = (USHORT)(size + sizeof(WCHAR));
        status = KiCopyFromUser(a->Image.Buffer, attribute->ValuePtr, size);
        a->Image.Buffer[size / sizeof(WCHAR)] = 0;
        return status;
    case PS_ATTRIBUTE_HANDLE_LIST:
        a->HasHandleList = TRUE;
        return capture_handles(attribute, &a->HandleList, &a->HandleCount);
    case PS_ATTRIBUTE_JOB_LIST:
        return capture_handles(attribute, &a->JobList, &a->JobCount);
    case PS_ATTRIBUTE_MITIGATION_OPTIONS:
        if (size != 8 && size != 16 && size != 24)
            return STATUS_INVALID_PARAMETER;
        status = KiCopyFromUser(a->Mitigation, attribute->ValuePtr, size);
        a->HasMitigation = TRUE;
        return status;
    case PS_ATTRIBUTE_CHILD_PROCESS_POLICY:
        VALUE(4, &word);
        if (word & ~7u)
            return STATUS_INVALID_PARAMETER;
        a->ChildPolicy = word;
        a->HasChildPolicy = TRUE;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY:
        VALUE(4, &a->AllApplicationPackages);
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_DESKTOP_APP_POLICY:
        VALUE(4, &a->DesktopApp);
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_COMPONENT_FILTER:
        VALUE(4, &a->ComponentFilter);
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_WIN32K_FILTER:
        VALUE(8, a->Win32kFilter);
        return STATUS_SUCCESS;
    /* known and size checked, but ignored. */
    case PS_ATTRIBUTE_PRIORITY_CLASS:
    case PS_ATTRIBUTE_PROTECTION_LEVEL:
        return size == 1 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_ERROR_MODE:
        VALUE(4, &a->ErrorMode);
        a->HasErrorMode = TRUE;
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_CHPE:
        return STATUS_SUCCESS;
    case PS_ATTRIBUTE_PREFERRED_NODE:
    case PS_ATTRIBUTE_MACHINE_TYPE:
        return size == 2 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_IDEAL_PROCESSOR:
        return size == 4 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_GROUP_AFFINITY:
        return size == 16 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_MEMORY_RESERVE:
        return size == 16 || size == 24 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_STD_HANDLE_INFO: {
        if (size != 8 && size != 16)
            return STATUS_INVALID_PARAMETER;
        ULONG info[2];
        VALUE(8, info);
        a->StdHandleFlags = info[0];
        a->StdHandleSubsystem = info[1];
        return STATUS_SUCCESS;
    }
    case PS_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES:
        return size == 8 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_BNO_ISOLATION:
        return size == 40 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_SAFE_OPEN_PROMPT_ORIGIN_CLAIM:
        return size == 524 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_MITIGATION_AUDIT_OPTIONS:
        return size == 8 || size == 16 || size == 24 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_IMAGE_INFO:
        if (size != sizeof(SECTION_IMAGE_INFORMATION))
            return STATUS_INVALID_PARAMETER;
        a->ImageInfo = attribute->ValuePtr;
        a->ImageInfoLength = attribute->ReturnLength;
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_PARAMETER;
    }
#undef VALUE
}

/*
 * the list's byte length is a length field plus a whole number of attributes, at most PS_ATTRIBUTE_MAX_COUNT,
 * none repeated, and input attributes have no length pointer.
 */
static NTSTATUS capture_attributes(const PS_ATTRIBUTE_LIST *user, PSP_ATTRIBUTES *a)
{
    SIZE_T total;
    NTSTATUS status = KiCopyFromUser(&total, user, sizeof(total));
    if (!NT_SUCCESS(status))
        return status;
    if (total < sizeof(SIZE_T) + sizeof(PS_ATTRIBUTE) || (total - sizeof(SIZE_T)) % sizeof(PS_ATTRIBUTE))
        return STATUS_INVALID_PARAMETER;
    SIZE_T count = (total - sizeof(SIZE_T)) / sizeof(PS_ATTRIBUTE);
    if (count > PS_ATTRIBUTE_MAX_COUNT)
        return STATUS_INVALID_PARAMETER;
    ULONG64 seen = 0;
    for (SIZE_T i = 0; i < count && NT_SUCCESS(status); i++) {
        PS_ATTRIBUTE attribute;
        status = KiCopyFromUser(&attribute, &user->Attributes[i], sizeof(attribute));
        if (!NT_SUCCESS(status))
            break;
        ULONG number = (ULONG)attribute.Attribute;
        ULONG64 bit = 1ull << (number & 0x3F);
        if (((number & 0x20000) && attribute.ReturnLength) || (seen & bit))
            return STATUS_INVALID_PARAMETER;
        seen |= bit;
        status = capture_one(&attribute, a);
    }
    if (NT_SUCCESS(status) && a->HasHandleList) {
        sort_handles(a->HandleList, a->HandleCount);
        /* a negative handle, pseudo handles included, is not a handle of the creator. */
        if ((LONG)(ULONG_PTR)a->HandleList[0] < 0)
            status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

/* writes back the process and thread ids and the thread's teb. */
static void return_attributes(const PSP_ATTRIBUTES *a, PEPROCESS process, PETHREAD thread)
{
    if (a->ClientId) {
        CLIENT_ID id = {process->UniqueProcessId, thread->Cid.UniqueThread};
        KiCopyToUser(a->ClientId, &id, sizeof(id));
        if (a->ClientIdLength) {
            SIZE_T n = sizeof(id);
            KiCopyToUser(a->ClientIdLength, &n, sizeof(n));
        }
    }
    if (a->TebAddress) {
        PVOID teb = thread->Tcb.Teb;
        KiCopyToUser(a->TebAddress, &teb, sizeof(teb));
        if (a->TebAddressLength) {
            SIZE_T n = sizeof(teb);
            KiCopyToUser(a->TebAddressLength, &n, sizeof(n));
        }
    }
    /* kernelbase asks every creation for the image's information, as NtQuerySection gives it. */
    if (a->ImageInfo) {
        SECTION_IMAGE_INFORMATION info;
        MmGetSectionImageInformation(process->ImageSection, &info);
        KiCopyToUser(a->ImageInfo, &info, sizeof(info));
        if (a->ImageInfoLength) {
            SIZE_T n = sizeof(info);
            KiCopyToUser(a->ImageInfoLength, &n, sizeof(n));
        }
    }
}

/* NtCreateThreadEx takes the same list: kernelbase's CreateThread asks every new thread for its client id there. */
NTSTATUS PspCaptureThreadAttributes(PVOID user, PVOID *captured)
{
    *captured = nullptr;
    if (!user)
        return STATUS_SUCCESS;
    PSP_ATTRIBUTES *a = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*a), KI_TAG('P', 's', 'A', 't'));
    if (!a)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(a, 0, sizeof(*a));
    NTSTATUS status = capture_attributes(user, a);
    if (!NT_SUCCESS(status)) {
        free_attributes(a);
        ExFreePool(a);
        return status;
    }
    *captured = a;
    return STATUS_SUCCESS;
}

void PspReturnThreadAttributes(PVOID captured, PEPROCESS process, PETHREAD thread)
{
    if (captured)
        return_attributes(captured, process, thread);
}

void PspFreeThreadAttributes(PVOID captured)
{
    if (!captured)
        return;
    free_attributes(captured);
    ExFreePool(captured);
}

/*
 * PS_STD_HANDLE_INFO. on request, for a program of the subsystem it names that inherits nothing, the child gets a copy
 * of each standard handle of its creator. always, as with a parent process attribute, the handles kernelbase put in the
 * parameters are copied from that parent. a handle the mask calls a console pseudo handle stays as it is, one that
 * cannot be copied becomes empty.
 */
static void duplicate_std_handles(PEPROCESS process, PEPROCESS creator, PEPROCESS parent, const PSP_ATTRIBUTES *a,
                                  BOOLEAN inherit)
{
    ULONG state = a->StdHandleFlags & 3;
    if (!state || state == 3)
        return;
    if (state == 1 && (inherit || process->Subsystem != a->StdHandleSubsystem))
        return;
    HANDLE values[3] = {};
    PEPROCESS source = state == 1 ? creator : parent;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(state == 1 ? &creator->Pcb : &process->Pcb);
    PRTL_USER_PROCESS_PARAMETERS pp = (state == 1 ? creator : process)->Peb->ProcessParameters;
    values[0] = pp->StandardInput;
    values[1] = pp->StandardOutput;
    values[2] = pp->StandardError;
    KiAttachProcess(saved);
    for (int i = 0; i < 3; i++) {
        if (!values[i] || ((a->StdHandleFlags & (4u << i)) && ((ULONG_PTR)values[i] & 3) == 3))
            continue;
        HANDLE copy = nullptr;
        if (!NT_SUCCESS(ObDuplicateHandle(source, values[i], process, &copy, 0, 0, 2 /* DUPLICATE_SAME_ACCESS */)))
            copy = nullptr;
        values[i] = copy;
    }
    KiAttachProcess(&process->Pcb);
    pp = process->Peb->ProcessParameters;
    pp->StandardInput = values[0];
    pp->StandardOutput = values[1];
    pp->StandardError = values[2];
    KiAttachProcess(saved);
}

static NTSTATUS give_console_reference(PEPROCESS process)
{
    HANDLE handle;
    NTSTATUS status = ConsoleCreateReference(process, &handle);
    if (!NT_SUCCESS(status))
        return status;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    process->Peb->ProcessParameters->ConsoleHandle = handle;
    KiAttachProcess(saved);
    return STATUS_SUCCESS;
}

static void apply_attributes(PEPROCESS process, PEPROCESS parent, const PSP_ATTRIBUTES *a)
{
    ULONG64 requested[3] = {};
    if (a->HasMitigation)
        memcpy(requested, a->Mitigation, sizeof(requested));
    PspApplyMitigationOptions(process, parent, requested);
    if (a->HasChildPolicy)
        PspApplyChildProcessPolicy(process, a->ChildPolicy);
    memcpy(process->Win32kFilter, a->Win32kFilter, sizeof(process->Win32kFilter));
    process->AllApplicationPackagesPolicy = a->AllApplicationPackages;
    process->DesktopAppPolicy = a->DesktopApp;
    process->ComponentFilter = a->ComponentFilter;
}

NTSTATUS NTAPI NtCreateUserProcess(PHANDLE process_handle, PHANDLE thread_handle, ACCESS_MASK process_access,
    ACCESS_MASK thread_access, POBJECT_ATTRIBUTES process_attributes, POBJECT_ATTRIBUTES thread_attributes,
    ULONG process_flags, ULONG thread_flags, PRTL_USER_PROCESS_PARAMETERS parameters, PVOID create_info,
    PVOID attributes)
{
    if (!process_handle || !thread_handle || !parameters)
        return STATUS_INVALID_PARAMETER;
    /* kernelbase of 26100 adds 0x200 to every creation without CREATE_SUSPENDED, and resumes only the thread after. */
    if ((process_flags & ~(PROCESS_CREATE_FLAGS_BREAKAWAY | PROCESS_CREATE_FLAGS_INHERIT_HANDLES | 0x200)) ||
        (thread_flags & ~1u))
        return STATUS_NOT_SUPPORTED;
    if (create_info) {
        PS_CREATE_INFO info;
        NTSTATUS status = KiCopyFromUser(&info, create_info, offsetof(PS_CREATE_INFO, InitState));
        if (!NT_SUCCESS(status))
            return status;
        if (info.Size != sizeof(PS_CREATE_INFO) || info.State != PsCreateInitialState)
            return STATUS_INVALID_PARAMETER;
    }
    /* the new objects' attributes may only give them a descriptor. */
    OBJECT_ATTRIBUTES process_oa, thread_oa;
    UNICODE_STRING process_name, thread_name;
    NTSTATUS attribute_status = ObCaptureObjectAttributes(process_attributes, &process_oa, &process_name);
    if (NT_SUCCESS(attribute_status))
        attribute_status = ObCaptureObjectAttributes(thread_attributes, &thread_oa, &thread_name);
    if (NT_SUCCESS(attribute_status) &&
        (process_oa.ObjectName || process_oa.RootDirectory || thread_oa.ObjectName || thread_oa.RootDirectory))
        attribute_status = STATUS_INVALID_PARAMETER;
    KiFreeCapturedString(&process_name);
    KiFreeCapturedString(&thread_name);
    if (!NT_SUCCESS(attribute_status))
        return attribute_status;
    /* capture the offered descriptors first, so a bad one starts nothing. */
    SD_BLOB *process_sd = nullptr, *thread_sd = nullptr;
    if (process_attributes && process_oa.SecurityDescriptor)
        attribute_status = SeSdCapture(process_oa.SecurityDescriptor, KeGetPreviousMode(), &process_sd);
    if (NT_SUCCESS(attribute_status) && thread_attributes && thread_oa.SecurityDescriptor)
        attribute_status = SeSdCapture(thread_oa.SecurityDescriptor, KeGetPreviousMode(), &thread_sd);
    if (!NT_SUCCESS(attribute_status)) {
        if (process_sd)
            SeSdDereference(process_sd);
        return attribute_status;
    }
    PSP_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    NTSTATUS status = attributes ? capture_attributes(attributes, &attrs) : STATUS_SUCCESS;
    PEPROCESS creator = PsCurrentProcess(), parent = creator;
    /* a handle list only makes sense when inheriting handles. */
    if (NT_SUCCESS(status) && attrs.HasHandleList && !(process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES))
        status = STATUS_INVALID_PARAMETER;
    /* a process barred from creating processes, unless the new one's policy overrides that. */
    if (NT_SUCCESS(status) && (creator->MitigationPolicy[MIME_POLICY_CHILD_PROCESS] & MIME_CHILD_PROCESS_NO_CREATION) &&
        !(attrs.HasChildPolicy && (attrs.ChildPolicy & MIME_CHILD_PROCESS_OVERRIDE)))
        status = STATUS_CHILD_PROCESS_BLOCKED;
    BOOLEAN parent_referenced = FALSE;
    if (NT_SUCCESS(status) && attrs.Parent) {
        PEPROCESS named;
        status = PsReferenceProcessHandle(attrs.Parent, PROCESS_CREATE_PROCESS, &named);
        if (NT_SUCCESS(status)) {
            parent = named;
            parent_referenced = TRUE;
        }
    }
    struct _TOKEN *token = nullptr;
    if (NT_SUCCESS(status) && attrs.Token)
        status = SepReferenceTokenForNewProcess(attrs.Token, &token);
    /* the jobs the process starts in, which must accept it. */
    struct EJOB *job = nullptr;
    if (NT_SUCCESS(status) && attrs.JobCount)
        status = PspReferenceJobForCreation(attrs.JobList, attrs.JobCount, &job);
    PSP_PARAMS params;
    memset(&params, 0, sizeof(params));
    if (NT_SUCCESS(status))
        status = capture_params(parameters, &params);
    if (NT_SUCCESS(status) && attrs.Image.Length)
        status = replace_image(&params, &attrs.Image);
    PEPROCESS process = nullptr;
    PETHREAD thread = nullptr;
    if (NT_SUCCESS(status))
        /* a creator without a console, like the elevation broker, passes on the console of the parent it names. */
        status = create_process(&params, token, 0, creator->Console ? creator->Console : parent->Console, TRUE, parent,
                                &process, &thread, process_sd, thread_sd, KeGetPreviousMode());
    free_params(&params);
    if (token)
        ObDereferenceObject(token);
    if (NT_SUCCESS(status)) {
        apply_attributes(process, parent, &attrs);
        /* the error mode asked for, CREATE_DEFAULT_ERROR_MODE sends 1, replaces the one taken from the parent. */
        if (attrs.HasErrorMode)
            process->DefaultHardErrorMode = attrs.ErrorMode;
        if (process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES)
            status = attrs.HasHandleList ? ObInheritHandleList(parent, process, attrs.HandleList, attrs.HandleCount)
                                         : ObInheritHandles(parent, process);
        if (NT_SUCCESS(status))
            duplicate_std_handles(process, creator, parent, &attrs, (process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES) != 0);
        /* a program that shares its creator's console gets its own handle to it, after the inherited ones. */
        if (NT_SUCCESS(status) && process->ConsoleShared)
            status = give_console_reference(process);
        /* the parent's job first, then the listed job nested below it. */
        if (NT_SUCCESS(status))
            status = PspInheritJob(process, parent, process_flags);
        if (NT_SUCCESS(status) && job)
            status = PspAssignNewProcessToJob(job, process);
        if (!NT_SUCCESS(status)) {
            PspTerminateProcess(process, status);
            ObDereferenceObject(thread);
            ObDereferenceObject(process);
        }
    }
    if (job)
        ObDereferenceObject(job);
    if (process_sd)
        SeSdDereference(process_sd);
    if (thread_sd)
        SeSdDereference(thread_sd);
    if (!NT_SUCCESS(status)) {
        if (parent_referenced)
            ObDereferenceObject(parent);
        free_attributes(&attrs);
        return status;
    }
    HANDLE ph = nullptr, th = nullptr;
    /* kernelbase asks for MAXIMUM_ALLOWED, which is everything for the creator of the process. */
    if (process_access & MAXIMUM_ALLOWED)
        process_access = (process_access & ~(ACCESS_MASK)MAXIMUM_ALLOWED) | PROCESS_ALL_ACCESS;
    if (thread_access & MAXIMUM_ALLOWED)
        thread_access = (thread_access & ~(ACCESS_MASK)MAXIMUM_ALLOWED) | THREAD_ALL_ACCESS;
    ObReferenceObject(process);
    status = ObCreateHandle(creator, process, process_access, 0, &ph);
    if (!NT_SUCCESS(status))
        ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        ObReferenceObject(thread);
        status = ObCreateHandle(creator, thread, thread_access, 0, &th);
        if (!NT_SUCCESS(status))
            ObDereferenceObject(thread);
    }
    if (NT_SUCCESS(status))
        status = KiCopyToUser(process_handle, &ph, sizeof(ph));
    if (NT_SUCCESS(status))
        status = KiCopyToUser(thread_handle, &th, sizeof(th));
    if (NT_SUCCESS(status) && create_info) {
        /* no file or section handle is given back, so the caller has none to close. */
        PS_CREATE_INFO info = {.Size = sizeof(info), .State = PsCreateSuccess};
        info.SuccessState.PebAddressNative = (ULONG_PTR)process->Peb;
        info.SuccessState.PebAddressWow64 = (ULONG)(ULONG_PTR)process->Wow64Peb;
        status = KiCopyToUser(create_info, &info, sizeof(info));
    }
    if (NT_SUCCESS(status)) {
        return_attributes(&attrs, process, thread);
        if (!(thread_flags & 1)) {
            thread->Tcb.SuspendCount = 0;
            KiStartThread(&thread->Tcb);
        }
    } else {
        if (th)
            ObCloseHandle(th, KernelMode);
        if (ph)
            ObCloseHandle(ph, KernelMode);
        PspTerminateProcess(process, status);
    }
    ObDereferenceObject(thread);
    ObDereferenceObject(process);
    if (parent_referenced)
        ObDereferenceObject(parent);
    free_attributes(&attrs);
    return status;
}
