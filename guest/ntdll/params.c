#include "ntdll_private.h"

static void move_strings(PRTL_USER_PROCESS_PARAMETERS pp, LONG_PTR delta)
{
    UNICODE_STRING *strings[] = {&pp->CurrentDirectory.DosPath, &pp->DllPath, &pp->ImagePathName, &pp->CommandLine,
                                 &pp->WindowTitle, &pp->DesktopInfo, &pp->ShellInfo, &pp->RuntimeData};
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(strings); i++)
        if (strings[i]->Buffer)
            strings[i]->Buffer = (PWSTR)((ULONG_PTR)strings[i]->Buffer + delta);
}

NTDLL_EXPORT PRTL_USER_PROCESS_PARAMETERS NTAPI RtlNormalizeProcessParams(PRTL_USER_PROCESS_PARAMETERS pp)
{
    if (pp && !(pp->Flags & RTL_USER_PROC_PARAMS_NORMALIZED)) {
        move_strings(pp, (LONG_PTR)pp);
        pp->Flags |= RTL_USER_PROC_PARAMS_NORMALIZED;
    }
    return pp;
}

NTDLL_EXPORT PRTL_USER_PROCESS_PARAMETERS NTAPI RtlDeNormalizeProcessParams(PRTL_USER_PROCESS_PARAMETERS pp)
{
    if (pp && (pp->Flags & RTL_USER_PROC_PARAMS_NORMALIZED)) {
        move_strings(pp, -(LONG_PTR)pp);
        pp->Flags &= ~RTL_USER_PROC_PARAMS_NORMALIZED;
    }
    return pp;
}

static SIZE_T align_room(SIZE_T bytes)
{
    return (bytes + sizeof(WCHAR) + sizeof(ULONG_PTR) - 1) & ~(sizeof(ULONG_PTR) - 1);
}

static SIZE_T string_room(PCUNICODE_STRING s)
{
    return align_room(!s ? 0 : s->Length > s->MaximumLength ? s->Length : s->MaximumLength);
}

/* copies s at *cursor with a terminator, room is the bytes it may take there. */
static void place_string(UNICODE_STRING *dst, PCUNICODE_STRING s, PUCHAR *cursor, SIZE_T room)
{
    USHORT length = s ? s->Length : 0;
    dst->Buffer = (PWSTR)*cursor;
    dst->Length = length;
    dst->MaximumLength = (USHORT)(room > 0xFFFE ? 0xFFFE : room);
    if (length)
        memcpy(dst->Buffer, s->Buffer, length);
    dst->Buffer[length / sizeof(WCHAR)] = 0;
    *cursor += room;
}

static SIZE_T environment_bytes(PCWSTR env)
{
    PCWSTR p = env;
    while (*p)
        p += wcslen(p) + 1;
    return (SIZE_T)(p - env + 1) * sizeof(WCHAR);
}

/*
 * one block holds the parameters and their strings. what the caller leaves out comes from the current process:
 * its dll path, current directory and environment, which is referenced and not copied. the command line defaults
 * to the image path. the block is given normalized only when the flags ask for it.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlCreateProcessParametersEx(PRTL_USER_PROCESS_PARAMETERS *out, PUNICODE_STRING image,
                                                         PUNICODE_STRING dll_path, PUNICODE_STRING current_directory,
                                                         PUNICODE_STRING command_line, PVOID environment,
                                                         PUNICODE_STRING window_title, PUNICODE_STRING desktop_info,
                                                         PUNICODE_STRING shell_info, PUNICODE_STRING runtime_data,
                                                         ULONG flags)
{
    if (!out || !image)
        return STATUS_INVALID_PARAMETER;
    *out = nullptr;
    RtlAcquirePebLock();
    PRTL_USER_PROCESS_PARAMETERS own = NtCurrentPeb()->ProcessParameters;
    if (!dll_path)
        dll_path = &own->DllPath;
    UNICODE_STRING cwd = own->CurrentDirectory.DosPath;
    if (current_directory)
        cwd = *current_directory;
    if (!command_line)
        command_line = image;
    if (!environment)
        environment = own->Environment;
    /* the current directory has room for any path and always ends with a backslash. */
    SIZE_T dir_room = align_room(cwd.Length + sizeof(WCHAR) > MAX_PATH * sizeof(WCHAR) ? cwd.Length + sizeof(WCHAR)
                                                                                         : MAX_PATH * sizeof(WCHAR));
    SIZE_T size = sizeof(RTL_USER_PROCESS_PARAMETERS) + dir_room + string_room(dll_path) +
                  string_room(image) + string_room(command_line) + string_room(window_title) +
                  string_room(desktop_info) + string_room(shell_info) + string_room(runtime_data);
    PVOID base = nullptr;
    SIZE_T allocated = size;
    NTSTATUS status =
        NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &allocated, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        RtlReleasePebLock();
        return status;
    }
    PRTL_USER_PROCESS_PARAMETERS pp = base;
    pp->MaximumLength = (ULONG)allocated;
    pp->Length = (ULONG)size;
    pp->Flags = RTL_USER_PROC_PARAMS_NORMALIZED;
    pp->Environment = environment;
    pp->EnvironmentSize = environment ? environment_bytes(environment) : 0;
    PUCHAR cursor = (PUCHAR)(pp + 1);
    place_string(&pp->CurrentDirectory.DosPath, &cwd, &cursor, dir_room);
    UNICODE_STRING *dir = &pp->CurrentDirectory.DosPath;
    if (dir->Length && dir->Buffer[dir->Length / sizeof(WCHAR) - 1] != L'\\') {
        dir->Buffer[dir->Length / sizeof(WCHAR)] = L'\\';
        dir->Length += sizeof(WCHAR);
        dir->Buffer[dir->Length / sizeof(WCHAR)] = 0;
    }
    place_string(&pp->DllPath, dll_path, &cursor, string_room(dll_path));
    place_string(&pp->ImagePathName, image, &cursor, string_room(image));
    place_string(&pp->CommandLine, command_line, &cursor, string_room(command_line));
    place_string(&pp->WindowTitle, window_title, &cursor, string_room(window_title));
    place_string(&pp->DesktopInfo, desktop_info, &cursor, string_room(desktop_info));
    place_string(&pp->ShellInfo, shell_info, &cursor, string_room(shell_info));
    place_string(&pp->RuntimeData, runtime_data, &cursor, string_room(runtime_data));
    RtlReleasePebLock();
    if (!(flags & RTL_USER_PROC_PARAMS_NORMALIZED))
        RtlDeNormalizeProcessParams(pp);
    *out = pp;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateProcessParameters(PRTL_USER_PROCESS_PARAMETERS *out, PUNICODE_STRING image,
                                                       PUNICODE_STRING dll_path, PUNICODE_STRING current_directory,
                                                       PUNICODE_STRING command_line, PVOID environment,
                                                       PUNICODE_STRING window_title, PUNICODE_STRING desktop_info,
                                                       PUNICODE_STRING shell_info, PUNICODE_STRING runtime_data)
{
    return RtlCreateProcessParametersEx(out, image, dll_path, current_directory, command_line, environment,
                                        window_title, desktop_info, shell_info, runtime_data, 0);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS pp)
{
    PVOID base = pp;
    SIZE_T size = 0;
    return NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}
