/*
 * the activation context a process starts with. on windows the program that starts another sends csrss the manifest
 * of the new one, sxs.dll there generates the data, and csrss maps it read-only in the new process and writes its
 * address in the peb before the first thread runs. mime has no csrss: the kernel generates the data when it makes
 * the process, from resource 1 of the image or the manifest beside it.
 */
#include "../ntos.h"
#include <mime/ntactctx.h>
#include "sxsk.h"

#define SXS_TAG KI_TAG('S', 'x', 's', 'K')
#define SXS_LIVE_FILES 8
#define SXS_FILE_MAXIMUM (64u * 1024 * 1024)
#define PEB32_ACTIVATION_CONTEXT_DATA 0x1F8

static FAST_MUTEX g_lock;
/* of the generation under way, which the lock makes the only one. */
static USHORT g_machine;
static PVOID g_root;
static SIZE_T g_root_size;
static NTSTATUS g_root_status;
static struct {
    const void *Data;
    SIZE_T Size;
} g_files[SXS_LIVE_FILES];

void PspSxsInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
}

PVOID SxsKAlloc(SIZE_T size, ULONG flags)
{
    PVOID p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, SXS_TAG);
    if (p)
        memset(p, 0, size);
    return p;
}

void SxsKFree(PVOID p)
{
    if (!p)
        return;
    for (ULONG i = 0; i < SXS_LIVE_FILES; i++)
        if (g_files[i].Data == p)
            g_files[i].Data = nullptr;
    ExFreePool(p);
}

SIZE_T SxsKFileSize(const void *data)
{
    for (ULONG i = 0; i < SXS_LIVE_FILES; i++)
        if (g_files[i].Data == data)
            return g_files[i].Size;
    return 0;
}

NTSTATUS SxsKReadFile(PCWSTR path, PVOID *data, SIZE_T *size)
{
    *data = nullptr;
    *size = 0;
    static const WCHAR prefix[] = L"\\??\\";
    SIZE_T chars = wcslen(path), lead = path[0] == '\\' ? 0 : RTL_NUMBER_OF(prefix) - 1;
    if ((chars + lead + 1) * sizeof(WCHAR) > 0xFFFE)
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    PWSTR name = SxsKAlloc((chars + lead + 1) * sizeof(WCHAR), 0);
    if (!name)
        return STATUS_NO_MEMORY;
    memcpy(name, prefix, lead * sizeof(WCHAR));
    memcpy(name + lead, path, chars * sizeof(WCHAR));
    UNICODE_STRING nt = {(USHORT)((chars + lead) * sizeof(WCHAR)), (USHORT)((chars + lead + 1) * sizeof(WCHAR)), name};
    PFILE_OBJECT file;
    NTSTATUS status = IopOpenFileObject(&nt, FILE_READ_DATA | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file);
    SxsKFree(name);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 length = 0;
    status = IoQueryFileSize(file, &length);
    if (NT_SUCCESS(status) && (!length || length > SXS_FILE_MAXIMUM))
        status = STATUS_SXS_CANT_GEN_ACTCTX;
    ULONG slot = 0;
    while (slot < SXS_LIVE_FILES && g_files[slot].Data)
        slot++;
    if (NT_SUCCESS(status) && slot == SXS_LIVE_FILES)
        status = STATUS_INSUFFICIENT_RESOURCES;
    PVOID bytes = NT_SUCCESS(status) ? SxsKAlloc(length, 0) : nullptr;
    if (NT_SUCCESS(status) && !bytes)
        status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(status)) {
        ULONG got = 0;
        status = IoReadFileObject(file, bytes, (ULONG)length, 0, &got);
        if (NT_SUCCESS(status) && got != length)
            status = STATUS_END_OF_FILE;
    }
    ObDereferenceObject(file);
    if (!NT_SUCCESS(status)) {
        if (bytes)
            ExFreePool(bytes);
        return status;
    }
    g_files[slot].Data = bytes;
    g_files[slot].Size = length;
    *data = bytes;
    *size = length;
    return STATUS_SUCCESS;
}

/* the manifest of a new process is resource 1 of its image and no other. */
NTSTATUS SxsKRootResource(ULONG_PTR name, const void **data, SIZE_T *size)
{
    if (name != 1)
        return STATUS_RESOURCE_NAME_NOT_FOUND;
    *data = g_root;
    *size = g_root_size;
    return g_root_status;
}

PCWSTR SxsKSystemDirectory(void)
{
    return g_machine == IMAGE_FILE_MACHINE_I386 ? L"C:\\Windows\\SysWOW64\\" : L"C:\\Windows\\System32\\";
}

PCWSTR SxsKProcessArch(void)
{
    return g_machine == IMAGE_FILE_MACHINE_I386 ? L"x86" : g_machine == IMAGE_FILE_MACHINE_ARM64 ? L"arm64" : L"amd64";
}

NTSTATUS SxsKDataAlloc(PVOID *base, SIZE_T *size)
{
    *base = SxsKAlloc(*size, 0);
    return *base ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

void SxsKDataFree(PVOID base)
{
    if (base)
        ExFreePool(base);
}

NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI ZwQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length, PULONG returned);

NTSTATUS NTAPI SxsKOpenKey(PHANDLE key, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return ZwOpenKey(key, access, attributes);
}

NTSTATUS NTAPI SxsKQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return ZwQueryValueKey(key, name, klass, info, length, returned);
}

NTSTATUS NTAPI SxsKClose(HANDLE handle)
{
    return ZwClose(handle);
}

/* resource 1 of the image the process has mapped, copied out of its address space. */
static NTSTATUS fetch_root(PEPROCESS process)
{
    SXS_IMAGE image = {process->SectionBaseAddress, process->ImageSection->Image.SizeOfImage, FALSE};
    NTSTATUS status;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        const void *data = nullptr;
        SIZE_T size = 0;
        status = SxsKFindManifest(&image, 1, &data, &size);
        if (NT_SUCCESS(status) && (!size || size > SXS_FILE_MAXIMUM))
            status = STATUS_SXS_CANT_GEN_ACTCTX;
        if (NT_SUCCESS(status)) {
            g_root = SxsKAlloc(size, 0);
            if (g_root) {
                memcpy(g_root, data, size);
                g_root_size = size;
            } else {
                status = STATUS_NO_MEMORY;
            }
        }
        KI_END_TRY_USER(jb);
    } else {
        status = KiFaultResult();
    }
    KiAttachProcess(saved);
    return status;
}

/* the data in a section of its own, mapped read-only in the process. */
static NTSTATUS map_data(PEPROCESS process, const void *data, PVOID *base)
{
    SIZE_T size = ((const ULONG *)data)[3];
    PVOID kernel, section;
    *base = nullptr;
    NTSTATUS status = MmCreateSharedSection(size, &kernel, &section);
    if (!NT_SUCCESS(status))
        return status;
    memcpy(kernel, data, size);
    SIZE_T view = 0;
    status = MiMapViewOfSection(section, process, base, 0, &view, PAGE_READONLY, 0);
    ObDereferenceObject(section);
    return status;
}

/* the address of the data in the peb the program reads. */
static NTSTATUS install(PEPROCESS process, const void *data)
{
    PVOID base;
    NTSTATUS status = map_data(process, data, &base);
    if (!NT_SUCCESS(status))
        return status;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    /* a 32-bit program reads its own peb, which wow64 fills from the 64-bit one when the process starts. */
    process->Peb->ActivationContextData = base;
    if (process->Machine == IMAGE_FILE_MACHINE_I386 && process->Wow64Peb)
        *(PULONG)((PUCHAR)process->Wow64Peb + PEB32_ACTIVATION_CONTEXT_DATA) = (ULONG)(ULONG_PTR)base;
    KiAttachProcess(saved);
    return STATUS_SUCCESS;
}

static BOOLEAN no_manifest(NTSTATUS status)
{
    return status == STATUS_RESOURCE_DATA_NOT_FOUND || status == STATUS_RESOURCE_TYPE_NOT_FOUND ||
           status == STATUS_RESOURCE_NAME_NOT_FOUND || status == STATUS_RESOURCE_LANG_NOT_FOUND;
}

NTSTATUS PspSxsCreateProcessContext(PEPROCESS process, PCUNICODE_STRING image)
{
    PWSTR path = SxsKAlloc(image->Length + sizeof(WCHAR), 0);
    if (!path)
        return STATUS_NO_MEMORY;
    memcpy(path, image->Buffer, image->Length);
    ExAcquireFastMutex(&g_lock);
    g_machine = process->Machine;
    g_root = nullptr;
    g_root_size = 0;
    g_root_status = fetch_root(process);
    MIME_SXS_GENERATE request = {.Source = path, .ResourceName = (PCWSTR)1, .Module = (PVOID)1, .ApplicationConfig = TRUE};
    PVOID data = nullptr;
    NTSTATUS status = no_manifest(g_root_status) || NT_SUCCESS(g_root_status) ? SxsGenerateActivationContext(&request, &data)
                                                                              : g_root_status;
    if (g_root)
        ExFreePool(g_root);
    g_root = nullptr;
    if (no_manifest(status))
        status = STATUS_SUCCESS;
    else if (NT_SUCCESS(status) && data)
        status = install(process, data);
    SxsKDataFree(data);
    ExReleaseFastMutex(&g_lock);
    if (!NT_SUCCESS(status))
        KiLogPrintf("sxs: no activation context for %wZ: %08x", image, status);
    ExFreePool(path);
    return status;
}

/* ------------------------------------------------------------------ CreateActCtx */

/* kernel32 refuses a manifest larger than this before it asks. */
#define SXS_STREAM_MAXIMUM 0x800000

/* a string of the message, whose characters lie in the client, with the end the generator reads to. */
static NTSTATUS capture_string(const UNICODE_STRING *string, PWSTR *out)
{
    *out = nullptr;
    if (!string->Length || !string->Buffer)
        return STATUS_SUCCESS;
    if (string->Length & 1)
        return STATUS_INVALID_PARAMETER;
    PWSTR copy = SxsKAlloc(string->Length + sizeof(WCHAR), 0);
    if (!copy)
        return STATUS_NO_MEMORY;
    NTSTATUS status = KiCopyFromUser(copy, string->Buffer, string->Length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy);
        return status;
    }
    *out = copy;
    return STATUS_SUCCESS;
}

/* the bytes of a stream: a resource is named by its address in the client, a file by a section the client made. */
static NTSTATUS read_stream(const BASE_SXS_STREAM *stream, PVOID *bytes, SIZE_T *size)
{
    *bytes = nullptr;
    *size = 0;
    if (!stream->Size || stream->Size > SXS_STREAM_MAXIMUM)
        return STATUS_SXS_CANT_GEN_ACTCTX;
    PVOID copy = SxsKAlloc(stream->Size, 0);
    if (!copy)
        return STATUS_NO_MEMORY;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (stream->HandleType == BASE_SXS_HANDLE_PROCESS) {
        status = KiCopyFromUser(copy, (PVOID)(ULONG_PTR)stream->Offset, stream->Size);
    } else if (stream->HandleType == BASE_SXS_HANDLE_SECTION) {
        PEPROCESS process = PsCurrentProcess();
        PSECTION section;
        status = ObReferenceHandleFor(stream->Handle, SECTION_MAP_READ, MmSectionType, UserMode, &section);
        PVOID base = nullptr;
        SIZE_T view = 0;
        if (NT_SUCCESS(status)) {
            status = MiMapViewOfSection(section, process, &base, 0, &view, PAGE_READONLY, 0);
            ObDereferenceObject(section);
        }
        if (NT_SUCCESS(status)) {
            status = stream->Offset <= view && stream->Size <= view - stream->Offset
                         ? KiCopyFromUser(copy, (PUCHAR)base + stream->Offset, stream->Size)
                         : STATUS_INVALID_PARAMETER;
            MiUnmapViewOfSection(process, base);
        }
    }
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy);
        return status;
    }
    *bytes = copy;
    *size = stream->Size;
    return STATUS_SUCCESS;
}

/* the architecture of the message is a PROCESSOR_ARCHITECTURE: 10 and 14 are the 32-bit programs of a 64-bit system. */
static USHORT machine_of(USHORT architecture, PEPROCESS process)
{
    switch (architecture) {
    case 0:
    case 10:
    case 14:
        return IMAGE_FILE_MACHINE_I386;
    case 9:
        return IMAGE_FILE_MACHINE_AMD64;
    case 12:
        return IMAGE_FILE_MACHINE_ARM64;
    default:
        return process->Machine;
    }
}

/*
 * what csrss does for CreateActCtx: kernel32 found the manifest and sends where it is, sxs.dll generates the data,
 * and csrss maps it read-only in the client and writes the address where the message says. kernel32 unmaps the view
 * when the context is released.
 */
NTSTATUS PspSxsCreateClientContext(const BASE_SXS_CREATE_ACTIVATION_CONTEXT *message)
{
    if (!(message->Flags &
          (BASE_SXS_MANIFEST_PRESENT | BASE_SXS_SYSTEM_DEFAULT_IDENTITY | BASE_SXS_TEXTUAL_IDENTITY)))
        return STATUS_SUCCESS;
    if (!(message->Flags & BASE_SXS_MANIFEST_PRESENT)) {
        KiLogPrintf("sxs: a context from the identity of an assembly alone is not generated");
        return STATUS_SXS_CANT_GEN_ACTCTX;
    }
    PEPROCESS process = PsCurrentProcess();
    PWSTR source = nullptr, directory = nullptr, config = nullptr;
    PVOID text = nullptr, data = nullptr, base = nullptr;
    SIZE_T size = 0;
    NTSTATUS status = capture_string(&message->Manifest.Path, &source);
    if (NT_SUCCESS(status) && !source)
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status))
        status = capture_string(&message->AssemblyDirectory, &directory);
    if (NT_SUCCESS(status) && (message->Flags & BASE_SXS_POLICY_PRESENT))
        status = capture_string(&message->Policy.Path, &config);
    if (NT_SUCCESS(status))
        status = read_stream(&message->Manifest, &text, &size);
    if (NT_SUCCESS(status)) {
        /* the name of the resource is its number, and all ones when it is a string. */
        MIME_SXS_GENERATE request = {.Source = source,
                                     .AssemblyDirectory = directory,
                                     .ResourceName = message->ResourceName < 0x10000 ? (PCWSTR)message->ResourceName : nullptr,
                                     .ApplicationConfig = config != nullptr,
                                     .Config = config};
        ExAcquireFastMutex(&g_lock);
        g_machine = machine_of(message->ProcessorArchitecture, process);
        status = SxsGenerateFromStream(&request, text, size, &data);
        ExReleaseFastMutex(&g_lock);
    }
    if (NT_SUCCESS(status) && message->ActivationContextData) {
        status = map_data(process, data, &base);
        if (NT_SUCCESS(status))
            status = KiCopyToUser(message->ActivationContextData, &base, sizeof(ULONG64));
        if (NT_SUCCESS(status) && message->NativeData)
            status = KiCopyToUser(message->NativeData, &base,
                                  process->Machine == IMAGE_FILE_MACHINE_I386 ? sizeof(ULONG) : sizeof(ULONG64));
        if (!NT_SUCCESS(status) && base)
            MiUnmapViewOfSection(process, base);
    }
    if (!NT_SUCCESS(status))
        KiLogPrintf("sxs: no activation context from %ls: %08x", source ? source : L"?", status);
    SxsKDataFree(data);
    if (text)
        ExFreePool(text);
    if (config)
        ExFreePool(config);
    if (directory)
        ExFreePool(directory);
    if (source)
        ExFreePool(source);
    return status;
}
