/*
 * the names of the user and the computer: GetUserNameEx, the directory
 * name translations, and the computer account.
 *
 * GetUserNameEx asks lsa through sspicli. a standalone machine knows the
 * account only by its sam compatible name, computer\user; the directory
 * formats answer ERROR_NONE_MAPPED there, and the directory functions of
 * secur32 itself fail because a workgroup has no domain controller.
 */
#include "secur32_private.h"

NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate);

#define TOKEN_QUERY 0x0008
#define TOKEN_USER_CLASS 1
#define KEY_VALUE_PARTIAL_INFORMATION 2
/* the flag GetUserNameExW accepts above the format: the size then counts the terminating zero. */
#define NAME_FLAG_COUNT_NUL 0x10000

DWORD SecComputerName(PWSTR out, DWORD capacity)
{
    DWORD n = capacity;
    if (!GetComputerNameW(out, &n)) {
        out[0] = 0;
        return 0;
    }
    return n;
}

static bool read_string(PCUNICODE_STRING key, PCWSTR name, PWSTR out, DWORD capacity)
{
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, (PUNICODE_STRING)key, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE h;
    if (!NT_SUCCESS(NtOpenKey(&h, KEY_READ, &oa)))
        return false;
    UNICODE_STRING value;
    RtlInitUnicodeString(&value, name);
    ULONG buffer[(12 + 260 * sizeof(WCHAR)) / sizeof(ULONG)];
    ULONG length;
    NTSTATUS status = NtQueryValueKey(h, &value, KEY_VALUE_PARTIAL_INFORMATION, buffer, sizeof(buffer), &length);
    NtClose(h);
    if (!NT_SUCCESS(status) || (buffer[1] != REG_SZ && buffer[1] != REG_EXPAND_SZ))
        return false;
    DWORD n = buffer[2] / sizeof(WCHAR);
    PCWSTR text = (PCWSTR)&buffer[3];
    while (n && !text[n - 1])
        n--;
    if (!n || n >= capacity)
        return false;
    memcpy(out, text, n * sizeof(WCHAR));
    out[n] = 0;
    return true;
}

/*
 * the account of the thread or process token: S-1-5-18 is the system
 * account, any other user is named by the USERNAME winlogon wrote in the
 * volatile environment of its hive, then by %USERNAME%.
 */
bool SecUserName(PWSTR out, DWORD capacity, bool *system)
{
    *system = false;
    out[0] = 0;
    HANDLE token;
    NTSTATUS status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &token);
    if (!NT_SUCCESS(status))
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token);
    if (NT_SUCCESS(status)) {
        ULONG64 buffer[32];
        ULONG length;
        status = NtQueryInformationToken(token, TOKEN_USER_CLASS, buffer, sizeof(buffer), &length);
        NtClose(token);
        if (NT_SUCCESS(status)) {
            static const UCHAR local_system[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
            const UCHAR *sid = (const UCHAR *)(ULONG_PTR)buffer[0];
            if (!memcmp(sid, local_system, sizeof(local_system))) {
                *system = true;
                return true;
            }
            static const WCHAR prefix[] = L"\\REGISTRY\\USER\\";
            static const WCHAR suffix[] = L"\\Volatile Environment";
            WCHAR path[256];
            UNICODE_STRING text = {0, 0, path};
            memcpy(path, prefix, sizeof(prefix));
            UNICODE_STRING sid_text = {0, (USHORT)(sizeof(path) - sizeof(prefix) - sizeof(suffix)),
                                       path + RTL_NUMBER_OF(prefix) - 1};
            if (NT_SUCCESS(RtlConvertSidToUnicodeString(&sid_text, (PVOID)sid, FALSE))) {
                PWSTR end = sid_text.Buffer + sid_text.Length / sizeof(WCHAR);
                memcpy(end, suffix, sizeof(suffix));
                text.Length = (USHORT)((end - path) * sizeof(WCHAR) + sizeof(suffix) - sizeof(WCHAR));
                text.MaximumLength = text.Length + sizeof(WCHAR);
                if (read_string(&text, L"USERNAME", out, capacity))
                    return true;
            }
        }
    }
    DWORD n = GetEnvironmentVariableW(L"USERNAME", out, capacity);
    if (!n || n >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}

/*
 * computer\user. the system account of a workgroup machine is its computer
 * account, WORKGROUP\computer$; guessed from what services observe, lsasrv
 * was not read.
 */
NTSTATUS SecSamCompatibleName(PWSTR out, DWORD capacity, DWORD *length)
{
    WCHAR computer[64], user[260];
    bool system;
    if (!SecUserName(user, RTL_NUMBER_OF(user), &system))
        return STATUS_NONE_MAPPED;
    SecComputerName(computer, RTL_NUMBER_OF(computer));
    int n = system ? _snwprintf(out, capacity, L"WORKGROUP\\%ls$", computer)
                   : _snwprintf(out, capacity, L"%ls\\%ls", computer, user);
    if (n < 0 || (DWORD)n >= capacity)
        return STATUS_BUFFER_OVERFLOW;
    out[n] = 0;
    *length = (DWORD)n;
    return STATUS_SUCCESS;
}

/* the name lsa gives for a format, or the win32 error it fails with. */
static DWORD user_name(EXTENDED_NAME_FORMAT format, PWSTR out, DWORD capacity, DWORD *length)
{
    switch (format) {
    case NameSamCompatible:
        return NT_SUCCESS(SecSamCompatibleName(out, capacity, length)) ? ERROR_SUCCESS : ERROR_NONE_MAPPED;
    case NameFullyQualifiedDN:
    case NameDisplay:
    case NameUniqueId:
    case NameCanonical:
    case NameUserPrincipal:
    case NameCanonicalEx:
    case NameServicePrincipal:
    case NameDnsDomain:
    case NameGivenName:
    case NameSurname:
        /* a local account has no directory names; NameDisplay would be the sam full name, which mime does not keep. */
        return ERROR_NONE_MAPPED;
    default:
        return ERROR_INVALID_PARAMETER;
    }
}

/*
 * the size rules of sspicli's GetUserNameExW: the buffer holds the name and
 * its terminating zero, a short buffer fails with ERROR_MORE_DATA and the
 * size in characters with the zero, and success returns the length without
 * it. the 0x10000 flag counts the zero on success and fails short buffers
 * with ERROR_INSUFFICIENT_BUFFER.
 */
SECURITYAPI BOOLEAN SEC_ENTRY GetUserNameExW(EXTENDED_NAME_FORMAT format, LPWSTR buffer, PULONG size)
{
    if (!size) {
        SecSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    bool count_nul = (ULONG)format & NAME_FLAG_COUNT_NUL;
    ULONG capacity = buffer ? *size : 0;
    WCHAR name[330];
    DWORD n;
    DWORD error = user_name((EXTENDED_NAME_FORMAT)((ULONG)format & ~NAME_FLAG_COUNT_NUL), name, RTL_NUMBER_OF(name), &n);
    if (error) {
        SecSetError(error);
        return FALSE;
    }
    if (n >= capacity) {
        *size = n + 1;
        SecSetError(count_nul ? ERROR_INSUFFICIENT_BUFFER : ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(buffer, name, n * sizeof(WCHAR));
    buffer[n] = 0;
    *size = count_nul ? n + 1 : n;
    return TRUE;
}

/*
 * sspicli's GetUserNameExA calls the wide function with the same size and
 * doubles the size it reports on failure.
 */
SECURITYAPI BOOLEAN SEC_ENTRY GetUserNameExA(EXTENDED_NAME_FORMAT format, LPSTR buffer, PULONG size)
{
    if (!size) {
        SecSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG capacity = *size;
    ULONG wide_size = capacity;
    PWSTR wide = SecHeapAlloc((SIZE_T)capacity * sizeof(WCHAR) + sizeof(WCHAR));
    if (!wide) {
        SecSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!GetUserNameExW(format, buffer ? wide : nullptr, &wide_size)) {
        SecHeapFree(wide);
        *size = wide_size * 2;
        return FALSE;
    }
    UNICODE_STRING source;
    RtlInitUnicodeString(&source, wide);
    ANSI_STRING ansi = {0, (USHORT)(capacity > 0xFFFF ? 0xFFFF : capacity), buffer};
    NTSTATUS status = RtlUnicodeStringToAnsiString(&ansi, &source, FALSE);
    SecHeapFree(wide);
    ULONG length = ansi.Length;
    if (!NT_SUCCESS(status)) {
        SecSetError(RtlNtStatusToDosError(status == STATUS_BUFFER_OVERFLOW ? STATUS_BUFFER_TOO_SMALL : status));
        *size = length + 1;
        return FALSE;
    }
    *size = (ULONG)format & NAME_FLAG_COUNT_NUL ? length + 1 : length;
    return TRUE;
}

/*
 * secur32's GetComputerObjectNameW reads the dns domain policy of lsa; a
 * workgroup machine has no domain sid there, so every format but
 * NameUnknown fails with STATUS_CANT_ACCESS_DOMAIN_INFO.
 */
SECURITYAPI BOOLEAN SEC_ENTRY GetComputerObjectNameW(EXTENDED_NAME_FORMAT format, LPWSTR buffer, PULONG size)
{
    SecSetError(format == NameUnknown ? ERROR_INVALID_PARAMETER : ERROR_CANT_ACCESS_DOMAIN_INFO);
    return FALSE;
}

SECURITYAPI BOOLEAN SEC_ENTRY GetComputerObjectNameA(EXTENDED_NAME_FORMAT format, LPSTR buffer, PULONG size)
{
    if (!size) {
        SecSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOLEAN ok = GetComputerObjectNameW(format, nullptr, size);
    if (!ok)
        *size *= 2;
    return ok;
}

/*
 * SecpTranslateName locates a domain controller with DsGetDcNameW, which
 * fails with ERROR_NO_SUCH_DOMAIN on a workgroup machine.
 */
SECURITYAPI BOOLEAN SEC_ENTRY SecpTranslateName(PCWSTR domain, PCWSTR name, EXTENDED_NAME_FORMAT offered,
                                                EXTENDED_NAME_FORMAT desired, PWSTR buffer, PULONG size)
{
    SecSetError(ERROR_NO_SUCH_DOMAIN);
    return FALSE;
}

SECURITYAPI BOOLEAN SEC_ENTRY SecpTranslateNameEx(PCWSTR domain, PCWSTR name, EXTENDED_NAME_FORMAT offered,
                                                  EXTENDED_NAME_FORMAT *desired, ULONG count, PWSTR **names)
{
    if (names)
        *names = nullptr;
    SecSetError(ERROR_NO_SUCH_DOMAIN);
    return FALSE;
}

SECURITYAPI HLOCAL SEC_ENTRY SecpFreeMemory(HLOCAL memory)
{
    return LocalFree(memory);
}

SECURITYAPI BOOLEAN SEC_ENTRY TranslateNameW(LPCWSTR name, EXTENDED_NAME_FORMAT format, EXTENDED_NAME_FORMAT desired,
                                             LPWSTR buffer, PULONG size)
{
    if (desired == NameUnknown) {
        SecSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SecpTranslateName(nullptr, name, format, desired, buffer, size);
}

SECURITYAPI BOOLEAN SEC_ENTRY TranslateNameA(LPCSTR name, EXTENDED_NAME_FORMAT format, EXTENDED_NAME_FORMAT desired,
                                             LPSTR buffer, PULONG size)
{
    if (!size) {
        SecSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = name ? SecAnsiToWide(name) : nullptr;
    if (name && !wide) {
        SecSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOLEAN ok = TranslateNameW(wide, format, desired, nullptr, size);
    SecHeapFree(wide);
    if (!ok)
        *size *= 2;
    return ok;
}
