/*
 * credui: the credential prompts are the part windows itself leaves to an
 * internal api set, and without it every prompt answers ERROR_NOT_SUPPORTED,
 * as here. user name parsing and credential packing are real.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define ERROR_INVALID_ACCOUNT_NAME 1315L

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

BOOL WINAPI CredUIInitControls(void)
{
    return TRUE;
}

/* the characters account and domain names may not hold. */
static BOOL valid_name(LPCWSTR name, SIZE_T length, SIZE_T maximum)
{
    if (!length || length > maximum)
        return FALSE;
    for (SIZE_T i = 0; i < length; i++)
        if (name[i] < 0x20 || wcschr(L"\"/\\[]:|<>+=;?,*", name[i]))
            return FALSE;
    return TRUE;
}

static DWORD copy_out(LPCWSTR source, SIZE_T length, WCHAR *buffer, ULONG size)
{
    if (length + 1 > size)
        return ERROR_INSUFFICIENT_BUFFER;
    memcpy(buffer, source, length * sizeof(WCHAR));
    buffer[length] = 0;
    return ERROR_SUCCESS;
}

/* "domain\user" splits, "user@domain" stays whole as a upn, a marshaled credential stays whole, a bare name is
   refused. */
DWORD WINAPI CredUIParseUserNameW(LPCWSTR name, WCHAR *user, ULONG user_size, WCHAR *domain, ULONG domain_size)
{
    if (!name || !user || !domain || !user_size || !domain_size)
        return ERROR_INVALID_PARAMETER;
    *user = 0;
    *domain = 0;
    SIZE_T length = wcslen(name);
    if (length > 513)
        return ERROR_INVALID_ACCOUNT_NAME;
    if (name[0] == '@' && name[1] == '@')
        return copy_out(name, length, user, user_size);
    LPCWSTR slash = wcsrchr(name, '\\');
    if (slash) {
        SIZE_T domain_length = (SIZE_T)(slash - name);
        if (!valid_name(name, domain_length, 255) || !valid_name(slash + 1, wcslen(slash + 1), 256))
            return ERROR_INVALID_ACCOUNT_NAME;
        DWORD error = copy_out(slash + 1, wcslen(slash + 1), user, user_size);
        if (!error)
            error = copy_out(name, domain_length, domain, domain_size);
        return error;
    }
    LPCWSTR at = wcsrchr(name, '@');
    if (at) {
        if (at == name || !valid_name(at + 1, wcslen(at + 1), 255))
            return ERROR_INVALID_ACCOUNT_NAME;
        return copy_out(name, length, user, user_size);
    }
    return ERROR_INVALID_ACCOUNT_NAME;
}

DWORD WINAPI CredUIParseUserNameA(LPCSTR name, CHAR *user, ULONG user_size, CHAR *domain, ULONG domain_size)
{
    if (!name || !user || !domain || !user_size || !domain_size)
        return ERROR_INVALID_PARAMETER;
    WCHAR wide[514], wuser[514], wdomain[514];
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 514))
        return ERROR_INVALID_ACCOUNT_NAME;
    DWORD error = CredUIParseUserNameW(wide, wuser, 514, wdomain, 514);
    *user = 0;
    *domain = 0;
    if (error)
        return error;
    if (!WideCharToMultiByte(CP_ACP, 0, wuser, -1, user, (int)user_size, nullptr, nullptr) ||
        !WideCharToMultiByte(CP_ACP, 0, wdomain, -1, domain, (int)domain_size, nullptr, nullptr))
        return ERROR_INSUFFICIENT_BUFFER;
    return ERROR_SUCCESS;
}

/* the errors after which a new credential could help. */
BOOLEAN WINAPI SspiIsPromptingNeeded(ULONG status)
{
    static const ULONG codes[] = {
        0xC05D0002, 0xC05D0003, 0xC05D0004, 0xD05D0002, 0x000004F1, 0x000004F8, 0x00000520, 0x00000525, 0x0000052E,
        0x800704F1, 0x800704F8, 0x80070520, 0x80070525, 0x8007052E, 0xC05D0006, 0xD05D0006, 0x00000791, 0x80070791,
        0xC000005F, 0xC0000064, 0xC000006A, 0xC000006D, 0xC0000071, 0xD000005F, 0xD0000064, 0xD000006A, 0xD000006D,
        0xD0000071, 0x00000005, 0x80070005, 0xC0000022, 0xD0000022, 0x00000056, 0x80070056, 0x8009030E, 0x8009030C,
        0x80090361, 0xC000042D, 0xC0000388, 0xD0000388, 0x80090350, 0x00000532, 0x80070532, 0x00000773, 0x80070773,
        0xC0000224, 0xD0000224, 0x000008C2, 0x800708C2,
    };
    for (UINT i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        if (codes[i] == status)
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ packed credentials */

/* KERB_INTERACTIVE_LOGON with its strings after it, their buffers as offsets from the start. */
typedef struct {
    USHORT Length, MaximumLength;
    ULONG_PTR Buffer;
} PACKED_STRING;

typedef struct {
    ULONG MessageType;
    PACKED_STRING Domain, User, Password;
} PACKED_LOGON;

#define KERB_INTERACTIVE_LOGON 2

BOOL WINAPI CredPackAuthenticationBufferW(DWORD flags, LPWSTR name, LPWSTR password, PBYTE packed, DWORD *size)
{
    if (!name || !password || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & ~1u) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    LPCWSTR user = name, domain = L"";
    SIZE_T domain_length = 0;
    LPCWSTR slash = wcsrchr(name, '\\');
    if (slash) {
        domain = name;
        domain_length = (SIZE_T)(slash - name);
        user = slash + 1;
    }
    SIZE_T user_length = wcslen(user), password_length = wcslen(password);
    SIZE_T needed = sizeof(PACKED_LOGON) + (domain_length + user_length + password_length) * sizeof(WCHAR);
    if (needed > 0xFFFF * 3 || !packed || *size < needed) {
        *size = (DWORD)needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    PACKED_LOGON *logon = (PACKED_LOGON *)packed;
    BYTE *strings = packed + sizeof(*logon);
    logon->MessageType = KERB_INTERACTIVE_LOGON;
    const struct {
        PACKED_STRING *field;
        LPCWSTR text;
        SIZE_T length;
    } parts[] = {{&logon->Domain, domain, domain_length}, {&logon->User, user, user_length},
                 {&logon->Password, password, password_length}};
    for (UINT i = 0; i < 3; i++) {
        parts[i].field->Length = parts[i].field->MaximumLength = (USHORT)(parts[i].length * sizeof(WCHAR));
        parts[i].field->Buffer = (ULONG_PTR)(strings - packed);
        memcpy(strings, parts[i].text, parts[i].length * sizeof(WCHAR));
        strings += parts[i].length * sizeof(WCHAR);
    }
    *size = (DWORD)needed;
    return TRUE;
}

static BOOL unpack_string(const BYTE *buffer, DWORD size, const PACKED_STRING *field, LPWSTR out, DWORD *capacity)
{
    if (!capacity)
        return TRUE;
    DWORD chars = field->Length / sizeof(WCHAR);
    if (field->Buffer > size || field->Length > size - field->Buffer)
        return FALSE;
    if (!out || *capacity < chars + 1) {
        *capacity = chars + 1;
        return FALSE;
    }
    memcpy(out, buffer + field->Buffer, field->Length);
    out[chars] = 0;
    *capacity = chars + 1;
    return TRUE;
}

BOOL WINAPI CredUnPackAuthenticationBufferW(DWORD flags, PVOID buffer, DWORD size, LPWSTR user, DWORD *user_size,
                                            LPWSTR domain, DWORD *domain_size, LPWSTR password, DWORD *password_size)
{
    if (!buffer || !user_size || !password_size || size < 4) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const PACKED_LOGON *logon = buffer;
    if (size < sizeof(*logon) || logon->MessageType != KERB_INTERACTIVE_LOGON) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    DWORD saved_user = *user_size, saved_password = *password_size, saved_domain = domain_size ? *domain_size : 0;
    BOOL ok = unpack_string(buffer, size, &logon->User, user, user_size);
    ok &= unpack_string(buffer, size, &logon->Domain, domain, domain_size);
    ok &= unpack_string(buffer, size, &logon->Password, password, password_size);
    if (!ok) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        if (user && saved_user)
            *user = 0;
        if (domain && saved_domain)
            *domain = 0;
        if (password && saved_password)
            *password = 0;
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI CredUIReadSSOCredW(LPCWSTR realm, LPWSTR *user)
{
    if (realm && !*realm)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

DWORD WINAPI CredUIReadSSOCredA(LPCSTR realm, LPSTR *user)
{
    if (realm && !*realm)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}
