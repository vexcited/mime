/*
 * user process creation, for programs the host starts and for
 * NtCreateUserProcess. the creator's process parameters, captured, become
 * the new process's: its environment, standard handles, window and
 * console choices and the runtime data the c library passes its file
 * descriptors in.
 */
#include "../ntos.h"

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
        return NULL;
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
    PVOID base = NULL;
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
    peb->ActiveProcessAffinityMask = 1;
    peb->CriticalSectionTimeout.QuadPart = -2592000ll * 10000000; /* 30 days */
    peb->HeapSegmentReserve = 0x100000;
    peb->HeapSegmentCommit = 0x2000;
    peb->HeapDeCommitTotalFreeThreshold = 0x10000;
    peb->HeapDeCommitFreeBlockThreshold = 0x1000;
    peb->MaximumNumberOfHeaps = 16;
    peb->SessionId = 1;
    peb->ActiveCodePage = 1252;
    peb->OemCodePage = 437;

    pp->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    pp->ShowWindowFlags = p->ShowWindowFlags;
    pp->WindowFlags = p->WindowFlags;
    pp->ConsoleFlags = p->ConsoleFlags;
    pp->ConsoleHandle = p->Detached ? CONSOLE_DETACHED : NULL;
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
    PUCHAR runtime = ok ? arena_alloc(&a, p->RuntimeDataLength + sizeof(WCHAR)) : NULL;
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

static NTSTATUS create_process(const PSP_PARAMS *p, ULONG64 request_id, MIME_CONSOLE *console, BOOLEAN suspended,
                               PEPROCESS *out, PETHREAD *initial_thread)
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
    status = PspCreateProcessObject(PsCurrentProcess(), &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
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
    PVOID base = NULL;
    SIZE_T view = 0;
    status = MiMapViewOfSection(section, process, &base, 0, &view, PAGE_EXECUTE_WRITECOPY, 0);
    if (NT_SUCCESS(status)) {
        process->SectionBaseAddress = base;
        PVOID ntdll = NULL;
        SIZE_T ntdll_view = 0;
        status = MiMapViewOfSection(system_dll->Section, process, &ntdll, 0, &ntdll_view, PAGE_EXECUTE_WRITECOPY, 0);
        if (status == STATUS_IMAGE_NOT_AT_BASE) {
            /* the system dll must sit at its preferred base: the kernel caches its exports. */
            status = STATUS_CONFLICTING_ADDRESSES;
        }
        process->NtdllBase = ntdll;
    }
    if (NT_SUCCESS(status))
        status = setup_peb(process, p);
    KiAttachProcess(saved);

    PETHREAD thread = NULL;
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
    KiLogPrintf("created process %u (%wZ)", (ULONG)(ULONG_PTR)process->UniqueProcessId, image);
    if (initial_thread)
        *initial_thread = thread;
    else
        ObDereferenceObject(thread);
    *out = process;
    return STATUS_SUCCESS;
}

NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd, ULONG64 request_id,
                             MIME_CONSOLE *console, PEPROCESS *out)
{
    UNICODE_STRING default_cwd = RTL_CONSTANT_STRING(L"C:\\");
    PSP_PARAMS p = {.Image = *image, .CommandLine = *cmdline, .CurrentDirectory = cwd ? *cwd : default_cwd,
                    .ShowWindowFlags = 1 /* SW_SHOWNORMAL */};
    return create_process(&p, request_id, console, FALSE, out, NULL);
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
        p->Environment = NT_SUCCESS(status) ? ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, KI_TAG('P', 's', 'E', 'n')) : NULL;
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

NTSTATUS NTAPI NtCreateUserProcess(PHANDLE process_handle, PHANDLE thread_handle, ACCESS_MASK process_access,
    ACCESS_MASK thread_access, POBJECT_ATTRIBUTES process_attributes, POBJECT_ATTRIBUTES thread_attributes,
    ULONG process_flags, ULONG thread_flags, PRTL_USER_PROCESS_PARAMETERS parameters, PVOID create_info,
    PVOID attributes)
{
    if (!process_handle || !thread_handle || !parameters)
        return STATUS_INVALID_PARAMETER;
    if (process_attributes || thread_attributes ||
        (process_flags & ~(PROCESS_CREATE_FLAGS_BREAKAWAY | PROCESS_CREATE_FLAGS_INHERIT_HANDLES)) ||
        (thread_flags & ~1u) || create_info || attributes)
        return STATUS_NOT_SUPPORTED;
    PSP_PARAMS params;
    memset(&params, 0, sizeof(params));
    NTSTATUS status = capture_params(parameters, &params);
    PEPROCESS process = NULL;
    PETHREAD thread = NULL;
    if (NT_SUCCESS(status))
        status = create_process(&params, 0, PsCurrentProcess()->Console, TRUE, &process, &thread);
    free_params(&params);
    if (NT_SUCCESS(status) && (process_flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES)) {
        status = ObInheritHandles(PsCurrentProcess(), process);
        if (!NT_SUCCESS(status)) {
            PspTerminateProcess(process, status);
            ObDereferenceObject(thread);
            ObDereferenceObject(process);
        }
    }
    if (!NT_SUCCESS(status))
        return status;
    HANDLE ph = NULL, th = NULL;
    ObReferenceObject(process);
    status = ObCreateHandle(PsCurrentProcess(), process, process_access, 0, &ph);
    if (!NT_SUCCESS(status))
        ObDereferenceObject(process);
    if (NT_SUCCESS(status)) {
        ObReferenceObject(thread);
        status = ObCreateHandle(PsCurrentProcess(), thread, thread_access, 0, &th);
        if (!NT_SUCCESS(status))
            ObDereferenceObject(thread);
    }
    if (NT_SUCCESS(status))
        status = KiCopyToUser(process_handle, &ph, sizeof(ph));
    if (NT_SUCCESS(status))
        status = KiCopyToUser(thread_handle, &th, sizeof(th));
    if (NT_SUCCESS(status)) {
        PspInheritJob(process, PsCurrentProcess(), process_flags);
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
    return status;
}
