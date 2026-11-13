#include "../ntos.h"
#include "../cm/cm.h"

#define KEY_SESSION_MANAGER L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager"
#define KEY_VERSION L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion"
#define KEY_VERSION_32 L"\\REGISTRY\\MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion"
#define KEY_USER_RUN_ONCE_FORMAT L"\\REGISTRY\\USER\\S-1-5-21-%u-%u-%u-1001\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"

static NTSTATUS open_for_move(PCWSTR name, ULONG options, PHANDLE handle)
{
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, name);
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    return IopCreateFile(handle, DELETE | SYNCHRONIZE, &attributes, &io, 0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                         FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | options, KernelMode);
}

/* an empty target deletes the source. a target that starts with ! replaces the file already there. */
static NTSTATUS move_one(PCWSTR source, PCWSTR target)
{
    HANDLE handle;
    if (!*target) {
        NTSTATUS status = open_for_move(source, FILE_DELETE_ON_CLOSE, &handle);
        if (NT_SUCCESS(status))
            ZwClose(handle);
        return status;
    }
    BOOLEAN replace = *target == L'!';
    NTSTATUS status = open_for_move(source, 0, &handle);
    if (!NT_SUCCESS(status))
        return status;
    PFILE_OBJECT file;
    status = ObReferenceHandle(handle, IoFileObjectTypeObject, KernelMode, (PVOID *)&file, nullptr);
    if (NT_SUCCESS(status)) {
        status = IoRenameFileObject(file, target + replace, replace);
        ObDereferenceObject(file);
    }
    ZwClose(handle);
    return status;
}

static void apply_moves(PCWSTR value)
{
    ULONG type = 0, length = 0;
    NTSTATUS status = CmQueryValue(KEY_SESSION_MANAGER, value, &type, nullptr, &length);
    if (status != STATUS_BUFFER_OVERFLOW && !NT_SUCCESS(status))
        return;
    if (type == REG_MULTI_SZ && length >= sizeof(WCHAR)) {
        /* two more characters end a list that is not ended. */
        PWCHAR list = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + 2 * sizeof(WCHAR), KI_TAG('E', 'x', 'P', 'm'));
        if (!list)
            return;
        memset(list, 0, length + 2 * sizeof(WCHAR));
        if (NT_SUCCESS(CmQueryValue(KEY_SESSION_MANAGER, value, &type, list, &length))) {
            PWCHAR end = list + length / sizeof(WCHAR);
            for (PWCHAR source = list; source < end && *source;) {
                PWCHAR target = source + wcslen(source) + 1;
                status = move_one(source, target);
                if (NT_SUCCESS(status))
                    KiLogPrintf("startup: %ls %ls %ls", source, *target ? L"moved to" : L"deleted", target);
                else
                    KiLogPrintf("startup: the delayed move of %ls to %ls failed: %08x", source, target, status);
                source = target + wcslen(target) + 1;
            }
        }
        ExFreePool(list);
    }
    CmDeleteValue(KEY_SESSION_MANAGER, value);
}

/* before the first program: it must find the files moved and no restart pending. */
void ExStartupFinishRestart(void)
{
    apply_moves(L"PendingFileRenameOperations");
    apply_moves(L"PendingFileRenameOperations2");
    static const PCWSTR keys[] = {
        KEY_VERSION L"\\WindowsUpdate\\Auto Update\\RebootRequired",
        KEY_VERSION_32 L"\\WindowsUpdate\\Auto Update\\RebootRequired",
        KEY_VERSION L"\\Component Based Servicing\\RebootPending",
        KEY_VERSION L"\\Component Based Servicing\\RebootInProgress",
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(keys); i++)
        if (NT_SUCCESS(CmDeleteKey(keys[i])))
            KiLogPrintf("startup: the restart mark %ls is cleared", keys[i]);
    CmDeleteValue(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Microsoft\\Updates", L"UpdateExeVolatile");
    CmDeleteValue(L"\\REGISTRY\\MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Updates", L"UpdateExeVolatile");
}

static void count_value(PVOID context, PCWSTR name, ULONG name_chars, ULONG type, const void *data, ULONG length)
{
    if (name_chars && (type == REG_SZ || type == REG_EXPAND_SZ))
        ++*(PULONG)context;
}

static BOOLEAN has_values(PCWSTR key)
{
    ULONG count = 0;
    CmEnumerateValues(key, count_value, &count);
    return count != 0;
}

static void run_and_wait(PCWSTR command, BOOLEAN elevated)
{
    UNICODE_STRING image = RTL_CONSTANT_STRING(L"C:\\Windows\\System32\\runonce.exe");
    UNICODE_STRING line;
    RtlInitUnicodeString(&line, command);
    PEPROCESS process;
    NTSTATUS status = PsCreateUserProcess(&image, &line, nullptr, 0, nullptr, elevated, &process);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("startup: cannot start %ls: %08x", command, status);
        return;
    }
    MimeSessionAdoptUserProgram(process);
    KeWaitForSingleObject(&process->Pcb, Executive, KernelMode, FALSE, nullptr);
    ObDereferenceObject(process);
}

/* the machine's entries run first and with the rights of an administrator, then the user's, as at a logon. */
static void NTAPI run_once_thread(PVOID context)
{
    ULONG_PTR which = (ULONG_PTR)context;
    if (which & 1)
        run_and_wait(L"runonce.exe /Machine", TRUE);
    if (which & 2)
        run_and_wait(L"runonce.exe /User", FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

void ExStartupRunOnce(void)
{
    WCHAR user[160];
    _snwprintf(user, RTL_NUMBER_OF(user) - 1, KEY_USER_RUN_ONCE_FORMAT, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1],
               HalBootInfo->user_sid[2]);
    user[RTL_NUMBER_OF(user) - 1] = 0;
    ULONG_PTR which = 0;
    if (has_values(KEY_VERSION L"\\RunOnce") || has_values(KEY_VERSION_32 L"\\RunOnce"))
        which |= 1;
    if (has_values(user))
        which |= 2;
    if (which)
        PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, run_once_thread, (PVOID)which);
}
