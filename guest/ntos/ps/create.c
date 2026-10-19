/*
 * user process creation, for programs the host starts and for
 * NtCreateUserProcess. the creator's process parameters, captured, become
 * the new process's: its environment, standard handles, window and
 * console choices and the runtime data the c library passes its file
 * descriptors in.
 */
#include "../ntos.h"
#include <mime/wow32.h>

#define CONSOLE_DETACHED ((HANDLE)(LONG_PTR)-1) /* DETACHED_PROCESS, as kernelbase marks it */
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004
#define MAX_ENVIRONMENT (32u << 20)

/* what a new process is given, in kernel memory. */
typedef struct PSP_PARAMS {
    UNICODE_STRING Image, CommandLine, CurrentDirectory, WindowTitle, DesktopInfo;
    PUCHAR RuntimeData; /* bytes, not text */
    USHORT RuntimeDataLength;
    PWCHAR Environment; /* NULL for the default one */
    SIZE_T EnvironmentBytes;
    HANDLE StandardInput, StandardOutput, StandardError;
    ULONG WindowFlags, ShowWindowFlags, ConsoleFlags;
    ULONG StartingX, StartingY, CountX, CountY, CountCharsX, CountCharsY, FillAttribute;
    BOOLEAN Detached;
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

static BOOLEAN arena_string(USER_ARENA *a, PUNICODE_STRING dst, const WCHAR *src, SIZE_T chars)
{
    PWSTR buf = arena_alloc(a, (chars + 1) * sizeof(WCHAR));
    if (!buf)
        return FALSE;
    memcpy(buf, src, chars * sizeof(WCHAR));
    buf[chars] = 0;
    dst->Buffer = buf;
    dst->Length = (USHORT)(chars * sizeof(WCHAR));
    dst->MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    return TRUE;
}

/* builds the PEB, process parameters and environment in the new process. */
/*
 * the 32-bit peb of a wow64 process, below 2 GiB: what the kernel knows of
 * the process, as the 64-bit peb has it. wow64.dll adds the 32-bit process
 * parameters.
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
    process->Wow64Peb = peb;
    return STATUS_SUCCESS;
}

static NTSTATUS setup_peb(PEPROCESS process, const PSP_PARAMS *p)
{
    PCUNICODE_STRING image = &p->Image, cmdline = &p->CommandLine, cwd = &p->CurrentDirectory;
    PWCHAR environment;
    SIZE_T environment_bytes;
    NTSTATUS status = STATUS_SUCCESS;
    if (p->Environment) {
        environment_bytes = p->EnvironmentBytes;
        environment = ExAllocatePool2(POOL_FLAG_NON_PAGED, environment_bytes, KI_TAG('P', 's', 'E', 'n'));
        if (!environment)
            return STATUS_NO_MEMORY;
        memcpy(environment, p->Environment, environment_bytes);
    } else {
        status = PspBuildEnvironment(process->Machine, &environment, &environment_bytes);
    }
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T size = 64 * 1024 + environment_bytes + p->RuntimeDataLength + p->WindowTitle.Length + p->DesktopInfo.Length;
    PVOID base = nullptr;
    status = MiAllocateVirtual(process, &base, &size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        ExFreePool(environment);
        return status;
    }
    USER_ARENA a = {base, 0, size};
    PPEB peb = arena_alloc(&a, sizeof(PEB));
    PRTL_USER_PROCESS_PARAMETERS pp = arena_alloc(&a, sizeof(RTL_USER_PROCESS_PARAMETERS));
    if (!peb || !pp) {
        ExFreePool(environment);
        return STATUS_NO_MEMORY;
    }
    memset(peb, 0, sizeof(*peb));
    memset(pp, 0, sizeof(*pp));
    PSECTION img = process->ImageSection;

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
    peb->CriticalSectionTimeout.QuadPart = -2592000ll * 10000000; /* 30 days */
    peb->HeapSegmentReserve = 0x100000;
    peb->HeapSegmentCommit = 0x2000;
    peb->HeapDeCommitTotalFreeThreshold = 0x10000;
    peb->HeapDeCommitFreeBlockThreshold = 0x1000;
    peb->MaximumNumberOfHeaps = 16;
    peb->SessionId = process->SessionId;
    peb->ActiveCodePage = 1252;
    peb->OemCodePage = 437;

    pp->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    pp->ShowWindowFlags = p->ShowWindowFlags;
    pp->WindowFlags = p->WindowFlags;
    pp->ConsoleFlags = p->ConsoleFlags;
    pp->ConsoleHandle = p->Detached ? CONSOLE_DETACHED : nullptr;
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
    BOOLEAN ok = arena_string(&a, &pp->ImagePathName, image->Buffer, image->Length / sizeof(WCHAR)) &&
                 arena_string(&a, &pp->CommandLine, cmdline->Buffer, cmdline->Length / sizeof(WCHAR)) &&
                 arena_string(&a, &pp->WindowTitle, title->Buffer, title->Length / sizeof(WCHAR)) &&
                 (p->DesktopInfo.Length
                      ? arena_string(&a, &pp->DesktopInfo, p->DesktopInfo.Buffer, p->DesktopInfo.Length / sizeof(WCHAR))
                      : arena_string(&a, &pp->DesktopInfo, L"WinSta0\\Default", 15)) &&
                 arena_string(&a, &pp->ShellInfo, L"", 0);
    /* the runtime data is bytes: the c library's inherited file descriptors. */
    PUCHAR runtime = ok ? arena_alloc(&a, p->RuntimeDataLength + sizeof(WCHAR)) : nullptr;
    if (runtime) {
        memcpy(runtime, p->RuntimeData, p->RuntimeDataLength);
        memset(runtime + p->RuntimeDataLength, 0, sizeof(WCHAR));
        pp->RuntimeData.Buffer = (PWSTR)runtime;
        pp->RuntimeData.Length = p->RuntimeDataLength;
        pp->RuntimeData.MaximumLength = (USHORT)(p->RuntimeDataLength + sizeof(WCHAR));
    }
    ok = ok && runtime;
    /* the current directory always ends with a backslash, like windows keeps it. */
    WCHAR dir[MIME_IMAGE_NAME_CHARS + 2];
    SIZE_T dlen = cwd->Length / sizeof(WCHAR);
    if (dlen > MIME_IMAGE_NAME_CHARS)
        dlen = MIME_IMAGE_NAME_CHARS;
    memcpy(dir, cwd->Buffer, dlen * sizeof(WCHAR));
    if (!dlen || dir[dlen - 1] != '\\')
        dir[dlen++] = '\\';
    UNICODE_STRING cur;
    ok = ok && arena_string(&a, &cur, dir, dlen);
    if (ok) {
        pp->CurrentDirectory.DosPath = cur;
        pp->CurrentDirectory.DosPath.MaximumLength = (USHORT)((MIME_IMAGE_NAME_CHARS + 1) * sizeof(WCHAR));
        /* reserve room so SetCurrentDirectory can grow the path in place. */
        PWSTR grow = arena_alloc(&a, (MIME_IMAGE_NAME_CHARS + 1) * sizeof(WCHAR));
        if (grow) {
            memcpy(grow, cur.Buffer, cur.Length + sizeof(WCHAR));
            pp->CurrentDirectory.DosPath.Buffer = grow;
        }
    }
    /* dll search path: application directory first. */
    SIZE_T img_chars = image->Length / sizeof(WCHAR);
    SIZE_T slash = img_chars;
    while (slash > 0 && image->Buffer[slash - 1] != '\\')
        slash--;
    ok = ok && arena_string(&a, &pp->DllPath, image->Buffer, slash ? slash - 1 : 0);
    PWSTR env = arena_alloc(&a, environment_bytes);
    if (ok && env)
        memcpy(env, environment, environment_bytes);
    ExFreePool(environment);
    if (!ok || !env)
        return STATUS_NO_MEMORY;
    pp->Environment = env;
    pp->EnvironmentSize = environment_bytes;
    pp->MaximumLength = pp->Length = sizeof(RTL_USER_PROCESS_PARAMETERS);
    process->Peb = peb;
    return STATUS_SUCCESS;
}

/* converts a dos path such as Z:\a\b.exe into \??\Z:\a\b.exe. */
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
                               BOOLEAN suspended, PEPROCESS parent, PEPROCESS *out, PETHREAD *initial_thread)
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
    PPSP_SYSTEM_DLL system_dll = PspSystemDllForMachine(section->Image.Machine);
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
    /* the creator's token, or the one it was given for the new process. */
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
    process->SystemDll = system_dll;
    process->Machine = section->Image.Machine;
    if (process->Machine == IMAGE_FILE_MACHINE_I386)
        process->AddressSpace.AllocationLimit = 0x7FFF0000;
    process->MimeRequestId = request_id;
    /* a gui program has no console, a console program shares the one it is given. */
    if (console && !p->Detached && section->Image.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
        ConsoleAttach(process, console);
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
            /* the system dll must sit at its preferred base: the kernel caches its exports. */
            status = STATUS_CONFLICTING_ADDRESSES;
        }
        process->NtdllBase = ntdll;
    }
    /* a 32-bit program has the 32-bit ntdll beside the native one, at its base. */
    PVOID ntdll32 = nullptr;
    if (NT_SUCCESS(status) && PspIsWow64Process(process) && PspWow64NtdllSection) {
        SIZE_T ntdll32_view = 0;
        status = MiMapViewOfSection(PspWow64NtdllSection, process, &ntdll32, 0, &ntdll32_view, PAGE_EXECUTE_WRITECOPY, 0);
        if (status == STATUS_IMAGE_NOT_AT_BASE)
            status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(status))
        status = setup_peb(process, p);
    /* where it is is told in a spare pointer of the peb, for wow64.dll. */
    if (NT_SUCCESS(status) && ntdll32)
        process->Peb->SparePointers[0] = ntdll32;
    if (NT_SUCCESS(status) && PspIsWow64Process(process))
        status = setup_wow64_peb(process);
    KiAttachProcess(saved);

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
    PspNotifyProcess(process, TRUE);
    /* the arguments help to tell the processes of one program apart. */
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
 * the service control manager starts with the machine, in session 0 with a token of the system. a machine without
 * the file goes on without services.
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
    NTSTATUS status = create_process(&p, token, 0, nullptr, FALSE, PsCurrentProcess(), &process, nullptr);
    ObDereferenceObject(token);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cannot start the service control manager: %08x", status);
        return status;
    }
    process->ServiceController = TRUE;
    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd, ULONG64 request_id,
                             MIME_CONSOLE *console, BOOLEAN elevated, PEPROCESS *out)
{
    UNICODE_STRING default_cwd = RTL_CONSTANT_STRING(L"C:\\");
    PSP_PARAMS p = {.Image = *image, .CommandLine = *cmdline, .CurrentDirectory = cwd ? *cwd : default_cwd,
                    .ShowWindowFlags = 1 /* SW_SHOWNORMAL */};
    struct _TOKEN *token = elevated ? SepCreateElevatedUserToken() : nullptr;
    if (elevated && !token)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = create_process(&p, token, request_id, console, FALSE, PsCurrentProcess(), out, nullptr);
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

/* the image of an attribute instead of the parameters', an nt path without its prefix. */
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

/* the creator's parameters, read once from its memory. */
static NTSTATUS capture_params(PRTL_USER_PROCESS_PARAMETERS user, PSP_PARAMS *p)
{
    RTL_USER_PROCESS_PARAMETERS pp;
    NTSTATUS status = KiCopyFromUser(&pp, user, sizeof(pp));
    if (!NT_SUCCESS(status))
        return status;
    status = KiCaptureUnicodeString(&p->Image, &user->ImagePathName);
    if (NT_SUCCESS(status))
        status = KiCaptureUnicodeString(&p->CommandLine, &user->CommandLine);
    if (NT_SUCCESS(status))
        status = KiCaptureUnicodeString(&p->CurrentDirectory, &user->CurrentDirectory.DosPath);
    if (NT_SUCCESS(status) && pp.WindowTitle.Length)
        status = KiCaptureUnicodeString(&p->WindowTitle, &user->WindowTitle);
    if (NT_SUCCESS(status) && pp.DesktopInfo.Length)
        status = KiCaptureUnicodeString(&p->DesktopInfo, &user->DesktopInfo);
    if (NT_SUCCESS(status) && pp.RuntimeData.Length && pp.RuntimeData.Buffer) {
        p->RuntimeData = ExAllocatePool2(POOL_FLAG_NON_PAGED, pp.RuntimeData.Length, KI_TAG('P', 's', 'R', 'd'));
        status = p->RuntimeData ? KiCopyFromUser(p->RuntimeData, pp.RuntimeData.Buffer, pp.RuntimeData.Length)
                                : STATUS_NO_MEMORY;
        p->RuntimeDataLength = pp.RuntimeData.Length;
    }
    /* an environment ends with an empty string; its size, when given, bounds the copy. */
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
    return status;
}

/*
 * the attribute list of NtCreateUserProcess. an attribute is a number, a size, a value and an optional
 * place for a length. an input attribute whose number has the additive bit carries its value, the
 * others point at it. what mime does with each:
 *   token, parent process, handle list, job list, mitigation options, child process policy,
 *   win32k filter, the policies of app containers, client id and teb address (out)  taken.
 *   image name  replaces the image of the parameters.
 *   the rest of the attributes windows knows: checked for their size and ignored.
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
    PHANDLE HandleList; /* kernel copies, sorted */
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
    UNICODE_STRING Image;    /* the image name, captured */
    PVOID ClientId;          /* where the process and thread ids go, and where their length */
    PSIZE_T ClientIdLength;
    PVOID TebAddress;
    PSIZE_T TebAddressLength;
} PSP_ATTRIBUTES;

static void free_attributes(PSP_ATTRIBUTES *a)
{
    if (a->HandleList)
        ExFreePool(a->HandleList);
    if (a->JobList)
        ExFreePool(a->JobList);
    KiFreeCapturedString(&a->Image);
}

/* a list of handles of eight bytes each, copied to the kernel. */
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

/* ordered by the low word of the handle, the way the inheritance looks for them. */
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
    /* a pointer to a value of a given size, read now. */
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
    /* known, sized, and not acted on. */
    case PS_ATTRIBUTE_PRIORITY_CLASS:
    case PS_ATTRIBUTE_PROTECTION_LEVEL:
        return size == 1 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_ERROR_MODE:
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
    case PS_ATTRIBUTE_STD_HANDLE_INFO:
        return size == 16 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES:
        return size == 8 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_BNO_ISOLATION:
        return size == 40 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_SAFE_OPEN_PROMPT_ORIGIN_CLAIM:
        return size == 524 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_MITIGATION_AUDIT_OPTIONS:
        return size == 8 || size == 16 || size == 24 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case PS_ATTRIBUTE_IMAGE_INFO:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_PARAMETER;
    }
#undef VALUE
}

/*
 * the list: its length in bytes is the size of a length plus a whole number of attributes, at most
 * PS_ATTRIBUTE_MAX_COUNT of them, none twice, and an input attribute has no place for a length.
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
        /* a negative handle, a pseudo handle among them, is no handle of the creator. */
        if ((LONG)(ULONG_PTR)a->HandleList[0] < 0)
            status = STATUS_INVALID_PARAMETER;
    }
    return status;
}

/* the places the process and thread ids and the thread's teb are given back to. */
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
}

/* the policies of the attributes, recorded in the new process. */
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
    if ((process_flags & ~(PROCESS_CREATE_FLAGS_BREAKAWAY | PROCESS_CREATE_FLAGS_INHERIT_HANDLES)) ||
        (thread_flags & ~1u) || create_info)
        return STATUS_NOT_SUPPORTED;
    /* the attributes of the new objects may give them a descriptor, nothing else. */
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
    /* and what they get is worked out first, so a refused descriptor starts nothing. */
    SD_BLOB *process_sd = nullptr, *thread_sd = nullptr;
    attribute_status = ObComputeSecurity(PsProcessTypeObject, process_attributes ? &process_oa : nullptr, nullptr,
                                         KeGetPreviousMode(), &process_sd);
    if (NT_SUCCESS(attribute_status))
        attribute_status = ObComputeSecurity(PsThreadTypeObject, thread_attributes ? &thread_oa : nullptr, nullptr,
                                             KeGetPreviousMode(), &thread_sd);
    if (!NT_SUCCESS(attribute_status)) {
        if (process_sd)
            SeSdDereference(process_sd);
        return attribute_status;
    }
    PSP_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    NTSTATUS status = attributes ? capture_attributes(attributes, &attrs) : STATUS_SUCCESS;
    PEPROCESS creator = PsCurrentProcess(), parent = creator;
    /* a list of handles to inherit is for a creation that inherits. */
    if (NT_SUCCESS(status) && attrs.HasHandleList && !(process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES))
        status = STATUS_INVALID_PARAMETER;
    /* a process that may not create processes, unless the new one is let to override that. */
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
    /* the jobs it is put in at birth, which must accept it. */
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
        status = create_process(&params, token, 0, creator->Console, TRUE, parent, &process, &thread);
    free_params(&params);
    if (token)
        ObDereferenceObject(token);
    if (NT_SUCCESS(status)) {
        ObAttachSecurity(process, process_sd);
        ObAttachSecurity(thread, thread_sd);
        process_sd = thread_sd = nullptr;
        apply_attributes(process, parent, &attrs);
        if (process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES)
            status = attrs.HasHandleList ? ObInheritHandleList(parent, process, attrs.HandleList, attrs.HandleCount)
                                         : ObInheritHandles(parent, process);
        /* the job of the creator first, and a job of the list below it. */
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
