/*
 * user process creation for programs started by the host.
 */
#include "../ntos.h"

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
static NTSTATUS setup_peb(PEPROCESS process, PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd)
{
    PWCHAR environment;
    SIZE_T environment_bytes;
    NTSTATUS status = PspBuildEnvironment(&environment, &environment_bytes);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T size = 64 * 1024 + environment_bytes;
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
    pp->ShowWindowFlags = 1; /* SW_SHOWNORMAL */
    pp->WindowFlags = 0;
    BOOLEAN ok = arena_string(&a, &pp->ImagePathName, image->Buffer, image->Length / sizeof(WCHAR)) &&
                 arena_string(&a, &pp->CommandLine, cmdline->Buffer, cmdline->Length / sizeof(WCHAR)) &&
                 arena_string(&a, &pp->WindowTitle, image->Buffer, image->Length / sizeof(WCHAR)) &&
                 arena_string(&a, &pp->DesktopInfo, L"WinSta0\\Default", 15) && arena_string(&a, &pp->ShellInfo, L"", 0) &&
                 arena_string(&a, &pp->RuntimeData, L"", 0);
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

NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image, PCUNICODE_STRING cmdline, PCUNICODE_STRING cwd, ULONG64 request_id,
                             PEPROCESS *out)
{
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

    PEPROCESS process;
    status = PspCreateProcessObject(PsCurrentProcess(), &process);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(section);
        return status;
    }
    process->ImageSection = section;
    process->MimeRequestId = request_id;
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
        status = MiMapViewOfSection(PspSystemDllSection, process, &ntdll, 0, &ntdll_view, PAGE_EXECUTE_WRITECOPY, 0);
        if (status == STATUS_IMAGE_NOT_AT_BASE) {
            /* ntdll must sit at its preferred base: the kernel caches its exports. */
            status = STATUS_CONFLICTING_ADDRESSES;
        }
        process->NtdllBase = ntdll;
    }
    if (NT_SUCCESS(status))
        status = setup_peb(process, image, cmdline, cwd);
    KiAttachProcess(saved);

    PETHREAD thread = NULL;
    if (NT_SUCCESS(status)) {
        PVOID entry = (PUCHAR)process->SectionBaseAddress + section->Image.EntryPoint;
        status = PspCreateUserThread(process, entry, process->Peb, FALSE, section->Image.StackReserve,
                                     section->Image.StackCommit, &thread);
    }
    if (!NT_SUCCESS(status)) {
        PspTerminateProcess(process, status);
        ObDereferenceObject(process);
        return status;
    }
    PspNotifyProcess(process, TRUE);
    KiLogPrintf("created process %u (%wZ)", (ULONG)(ULONG_PTR)process->UniqueProcessId, image);
    ObDereferenceObject(thread);
    *out = process;
    return STATUS_SUCCESS;
}
