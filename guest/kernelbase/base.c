/* shared helpers, last error and generic handle functions. */
#include "kernelbase_private.h"

/* ------------------------------------------------------------------ helpers */

int KbFormat(PWSTR buffer, SIZE_T size, PCWSTR format, ...)
{
    if (!size)
        return -1;
    va_list ap;
    va_start(ap, format);
    int n = _vsnwprintf(buffer, size - 1, format, ap);
    va_end(ap);
    buffer[size - 1] = 0;
    return n;
}

int KbFormatA(PSTR buffer, SIZE_T size, PCSTR format, ...)
{
    if (!size)
        return -1;
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(buffer, size - 1, format, ap);
    va_end(ap);
    buffer[size - 1] = 0;
    return n;
}

BOOL KbSetStatus(NTSTATUS status)
{
    KbSetError(RtlNtStatusToDosError(status));
    return FALSE;
}

PLARGE_INTEGER KbTimeout(PLARGE_INTEGER storage, DWORD milliseconds)
{
    if (milliseconds == INFINITE)
        return nullptr;
    storage->QuadPart = -(LONGLONG)milliseconds * 10000;
    return storage;
}

BOOL KbDosPathToNt(LPCWSTR dos, PUNICODE_STRING nt)
{
    if (!dos) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!*dos) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    if (!RtlDosPathNameToNtPathName_U(dos, nt, nullptr, nullptr)) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

static BOOL g_file_apis_oem;

UINT KbFileApiCodePage(void)
{
    return g_file_apis_oem ? CP_OEMCP : CP_ACP;
}

BOOL WINAPI AreFileApisANSI(void)
{
    return !g_file_apis_oem;
}

void WINAPI SetFileApisToANSI(void)
{
    g_file_apis_oem = FALSE;
}

void WINAPI SetFileApisToOEM(void)
{
    g_file_apis_oem = TRUE;
}

PWSTR KbAnsiToWideCp(UINT cp, LPCSTR s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    PWSTR w = KbAlloc((SIZE_T)n * sizeof(WCHAR));
    if (!w) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    MultiByteToWideChar(cp, 0, s, -1, w, n);
    return w;
}

PWSTR KbAnsiToWide(LPCSTR s)
{
    return KbAnsiToWideCp(KbFileApiCodePage(), s);
}

PSTR KbWideToAnsi(LPCWSTR s)
{
    if (!s)
        return nullptr;
    UINT cp = KbFileApiCodePage();
    int n = WideCharToMultiByte(cp, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return nullptr;
    PSTR a = KbAlloc((SIZE_T)n);
    if (!a) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    WideCharToMultiByte(cp, 0, s, -1, a, n, nullptr, nullptr);
    return a;
}

DWORD KbWideResultToAnsi(LPCWSTR wide, LPSTR buffer, DWORD size)
{
    UINT cp = KbFileApiCodePage();
    int need = WideCharToMultiByte(cp, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return 0;
    if ((DWORD)need > size || !buffer)
        return (DWORD)need;
    WideCharToMultiByte(cp, 0, wide, -1, buffer, (int)size, nullptr, nullptr);
    return (DWORD)need - 1;
}

/* ------------------------------------------------------------------ named objects */

static HANDLE g_named_root;

static HANDLE named_object_root(void)
{
    HANDLE root = g_named_root;
    if (root)
        return root;
    WCHAR path[64];
    KbFormat(path, RTL_NUMBER_OF(path), L"\\Sessions\\%u\\BaseNamedObjects", KbPeb()->SessionId);
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    NTSTATUS status = NtOpenDirectoryObject(&root, DIRECTORY_QUERY | DIRECTORY_TRAVERSE | DIRECTORY_CREATE_OBJECT |
                                                       DIRECTORY_CREATE_SUBDIRECTORY,
                                            &oa);
    if (!NT_SUCCESS(status)) {
        RtlInitUnicodeString(&us, L"\\BaseNamedObjects");
        status = NtOpenDirectoryObject(&root, DIRECTORY_QUERY | DIRECTORY_TRAVERSE | DIRECTORY_CREATE_OBJECT |
                                                  DIRECTORY_CREATE_SUBDIRECTORY,
                                       &oa);
        if (!NT_SUCCESS(status))
            return nullptr;
    }
    HANDLE previous = __sync_val_compare_and_swap(&g_named_root, nullptr, root);
    if (previous) {
        NtClose(root);
        return previous;
    }
    return root;
}

HANDLE KbNamedObjectRoot(void)
{
    return named_object_root();
}

BOOL KbNamedObjectAttributes(POBJECT_ATTRIBUTES oa, PUNICODE_STRING us, LPCWSTR name, BOOL inherit,
                             const SECURITY_ATTRIBUTES *sa)
{
    ULONG attributes = 0;
    if (inherit || (sa && sa->bInheritHandle))
        attributes |= OBJ_INHERIT;
    InitializeObjectAttributes(oa, nullptr, attributes, nullptr, sa ? sa->lpSecurityDescriptor : nullptr);
    if (!name)
        return TRUE;
    /* a private namespace alias maps to its own directory. */
    HANDLE private_root = KbPrivateNamespaceLookup(&name);
    if (!private_root && !_wcsnicmp(name, L"Local\\", 6))
        name += 6;
    RtlInitUnicodeString(us, name);
    oa->ObjectName = us;
    oa->RootDirectory = private_root ? private_root : named_object_root();
    if (!oa->RootDirectory) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ last error */

DWORD WINAPI GetLastError(void)
{
    return NtCurrentTeb()->LastErrorValue;
}

void WINAPI SetLastError(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

void WINAPI RestoreLastError(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

ULONG WINAPI BaseSetLastNTError(NTSTATUS status)
{
    ULONG error = RtlNtStatusToDosError(status);
    KbSetError(error);
    return error;
}

/* the mode lives in the process, where a job can force bits on. */
UINT WINAPI GetErrorMode(void)
{
    ULONG mode = 0;
    NtQueryInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &mode, sizeof(mode), nullptr);
    return mode;
}

UINT WINAPI SetErrorMode(UINT mode)
{
    UINT old = GetErrorMode();
    /* SEM_NOALIGNMENTFAULTEXCEPT cannot be cleared once set. */
    ULONG next = mode | (old & SEM_NOALIGNMENTFAULTEXCEPT);
    NtSetInformationProcess(NtCurrentProcess(), ProcessDefaultHardErrorMode, &next, sizeof(next));
    return old;
}

BOOL WINAPI SetThreadErrorMode(DWORD mode, LPDWORD old)
{
    if (mode & ~(DWORD)(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PTEB teb = NtCurrentTeb();
    if (old)
        *old = teb->HardErrorMode;
    teb->HardErrorMode = mode;
    return TRUE;
}

DWORD WINAPI GetThreadErrorMode(void)
{
    return NtCurrentTeb()->HardErrorMode;
}

/* ------------------------------------------------------------------ handles */

BOOL WINAPI CloseHandle(HANDLE handle)
{
    NTSTATUS status = NtClose(handle);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI DuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, LPHANDLE target,
                            DWORD access, BOOL inherit, DWORD options)
{
    NTSTATUS status = NtDuplicateObject(source_process, source, target_process, target, access,
                                        inherit ? OBJ_INHERIT : 0, options);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetHandleInformation(HANDLE handle, LPDWORD flags)
{
    if (!flags) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    struct {
        BOOLEAN Inherit;
        BOOLEAN ProtectFromClose;
    } info;
    NTSTATUS status = NtQueryObject(handle, 4 /* ObjectHandleFlagInformation */, &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *flags = (info.Inherit ? HANDLE_FLAG_INHERIT : 0) | (info.ProtectFromClose ? HANDLE_FLAG_PROTECT_FROM_CLOSE : 0);
    return TRUE;
}

BOOL WINAPI SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags)
{
    /* only the bits of the mask change. */
    DWORD current;
    if (!GetHandleInformation(handle, &current))
        return FALSE;
    current = (current & ~mask) | (flags & mask);
    struct {
        BOOLEAN Inherit;
        BOOLEAN ProtectFromClose;
    } info = {(current & HANDLE_FLAG_INHERIT) != 0, (current & HANDLE_FLAG_PROTECT_FROM_CLOSE) != 0};
    NTSTATUS status = NtSetInformationObject(handle, 4 /* ObjectHandleFlagInformation. */, &info, sizeof(info));
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI CompareObjectHandles(HANDLE a, HANDLE b)
{
    return a == b;
}

/* ------------------------------------------------------------------ pointer encoding */

PVOID WINAPI EncodePointer(PVOID p)
{
    return RtlEncodePointer(p);
}

PVOID WINAPI DecodePointer(PVOID p)
{
    return RtlDecodePointer(p);
}

PVOID WINAPI EncodeSystemPointer(PVOID p)
{
    return RtlEncodeSystemPointer(p);
}

PVOID WINAPI DecodeSystemPointer(PVOID p)
{
    return RtlDecodeSystemPointer(p);
}

/* ntdll does not export RtlImageNtHeader, so headers are located here. */
PIMAGE_NT_HEADERS KbImageNtHeader(PVOID base)
{
    PIMAGE_DOS_HEADER dos = base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000000)
        return nullptr;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}
