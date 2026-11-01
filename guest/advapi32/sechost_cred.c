/*
 * credentials as sechost.dll handles them before they reach the credential manager: user names split into account and
 * domain, credentials marshaled to text, blobs encoded with the logon session and the per user notifications. the
 * credential manager itself, which stores credentials, is not part of this machine.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/ntapi.h>

#define STATUS_INVALID_ACCOUNT_NAME_ ((NTSTATUS)0xC0000062L)

enum { CERT_CREDENTIAL = 1, USERNAME_TARGET_CREDENTIAL, BINARY_BLOB_CREDENTIAL, USERNAME_FOR_PACKED_CREDENTIALS, BINARY_BLOB_FOR_SYSTEM };

ADVAPI BOOL WINAPI CredUnmarshalCredentialW(LPCWSTR marshaled, PDWORD type, PVOID *credential);
ADVAPI BOOL WINAPI CredMarshalCredentialW(DWORD type, PVOID credential, LPWSTR *marshaled);
ADVAPI NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG length, ULONG flags);
ADVAPI NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG length, ULONG flags);

/* ------------------------------------------------------------------ user names */

/*
 * splits "domain\user" and "@@marshaled@domain", keeping a user principal name or a bare name whole, and says which
 * of the forms it is: 1 an address, 2 a domain account, 3 a marshaled certificate, 4 a bare name. the text is cut in
 * place at the separator.
 */
ADVAPI NTSTATUS WINAPI CredParseUserNameWithType(PWSTR source, PUNICODE_STRING user, PUNICODE_STRING domain, int *type)
{
    if (!source)
        return STATUS_INVALID_ACCOUNT_NAME_;
    SIZE_T length = wcslen(source);
    if (length > 0x201)
        return STATUS_INVALID_ACCOUNT_NAME_;
    PWSTR name = source, domain_text = nullptr;
    int kind;
    PWSTR slash = wcsrchr(source, L'\\');
    if (slash) {
        domain_text = source;
        name = slash + 1;
        *slash = 0;
        kind = 2;
    } else if (wcsrchr(source, L'@')) {
        kind = 1;
        if (length > 2 && source[0] == L'@' && source[1] == L'@') {
            PWSTR at = wcschr(source + 2, L'@');
            if (at) {
                domain_text = at + 1;
                *at = 0;
            }
        }
    } else if (*source && length <= 0x100) {
        kind = 4;
    } else {
        return STATUS_INVALID_ACCOUNT_NAME_;
    }
    DWORD marshal_type;
    PVOID credential = nullptr;
    if (CredUnmarshalCredentialW(name, &marshal_type, &credential)) {
        if (marshal_type == CERT_CREDENTIAL)
            kind = 3;
        if (credential)
            LocalFree(credential);
    }
    if (user)
        RtlInitUnicodeString(user, name);
    if (domain)
        RtlInitUnicodeString(domain, domain_text);
    *type = kind;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ marshaled credentials */

static const WCHAR g_alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789#-";
static const WCHAR g_blob_mark[5] = {7, 8, 12, 10, 13};
static const WCHAR g_system_blob_mark[5] = {8, 7, 12, 10, 13};

static void put_char(WCHAR **at, DWORD value)
{
    *(*at)++ = value <= 0x3F ? g_alphabet[value] : L'=';
}

/* three bytes become four characters, the last group as many as it needs. */
static void put_bytes(WCHAR **at, const BYTE *bytes, DWORD count)
{
    for (DWORD i = 0; i < count; i += 3) {
        DWORD left = count - i < 3 ? count - i : 3, group = 0;
        memcpy(&group, bytes + i, left);
        put_char(at, group & 0x3F);
        put_char(at, (group >> 6) & 0x3F);
        if (left > 1) {
            put_char(at, (group >> 12) & 0x3F);
            if (left > 2)
                put_char(at, (group >> 18) & 0x3F);
        }
    }
}

static DWORD marshal_chars(DWORD bytes)
{
    return bytes / 3 * 4 + (bytes % 3 == 1 ? 2 : bytes % 3 == 2 ? 3 : 0);
}

typedef struct CERT_CREDENTIAL_INFO_ {
    DWORD Size;
    UCHAR Hash[20];
} CERT_CREDENTIAL_INFO_;

typedef struct BINARY_BLOB_INFO_ {
    ULONG Size;
    PUCHAR Data;
} BINARY_BLOB_INFO_;

ADVAPI BOOL WINAPI CredMarshalCredentialW(DWORD type, PVOID credential, LPWSTR *marshaled)
{
    DWORD error = ERROR_INVALID_PARAMETER;
    const CERT_CREDENTIAL_INFO_ *cert = credential;
    const BINARY_BLOB_INFO_ *blob = credential;
    PWSTR text = credential ? *(PWSTR *)credential : nullptr;
    DWORD text_bytes = 0, chars;
    if (!credential || !marshaled)
        goto fail;
    switch (type) {
    case CERT_CREDENTIAL:
        if (cert->Size < sizeof(*cert))
            goto fail;
        chars = marshal_chars(20);
        break;
    case USERNAME_TARGET_CREDENTIAL:
    case USERNAME_FOR_PACKED_CREDENTIALS:
        if (!text || !*text)
            goto fail;
        text_bytes = (DWORD)(wcslen(text) * sizeof(WCHAR));
        chars = marshal_chars(4) + marshal_chars(text_bytes);
        break;
    case BINARY_BLOB_CREDENTIAL:
    case BINARY_BLOB_FOR_SYSTEM:
        chars = 5 + marshal_chars(4) + marshal_chars(blob->Size);
        break;
    default:
        goto fail;
    }
    WCHAR *out = LocalAlloc(LMEM_ZEROINIT, (chars + 4) * sizeof(WCHAR));
    if (!out) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto fail;
    }
    WCHAR *at = out;
    *at++ = L'@';
    *at++ = L'@';
    put_char(&at, type);
    switch (type) {
    case CERT_CREDENTIAL:
        put_bytes(&at, cert->Hash, 20);
        break;
    case BINARY_BLOB_CREDENTIAL:
    case BINARY_BLOB_FOR_SYSTEM:
        memcpy(at, type == BINARY_BLOB_CREDENTIAL ? g_blob_mark : g_system_blob_mark, sizeof(g_blob_mark));
        at += 5;
        put_bytes(&at, (const BYTE *)&blob->Size, 4);
        put_bytes(&at, blob->Data, blob->Size);
        break;
    default:
        put_bytes(&at, (const BYTE *)&text_bytes, 4);
        put_bytes(&at, (const BYTE *)text, text_bytes);
        break;
    }
    *at = 0;
    *marshaled = out;
    return TRUE;
fail:
    SetLastError(error);
    return FALSE;
}

static BOOL take_char(const WCHAR **at, const WCHAR *end, DWORD *value)
{
    if (*at >= end)
        return FALSE;
    WCHAR c = *(*at)++;
    if (c >= L'A' && c <= L'Z')
        *value = c - L'A';
    else if (c >= L'a' && c <= L'z')
        *value = c - L'a' + 26;
    else if (c >= L'0' && c <= L'9')
        *value = c - L'0' + 52;
    else if (c == L'#')
        *value = 62;
    else if (c == L'-')
        *value = 63;
    else
        return FALSE;
    return TRUE;
}

static BOOL take_bytes(const WCHAR **at, const WCHAR *end, BYTE *bytes, DWORD count)
{
    for (DWORD i = 0; i < count; i += 3) {
        DWORD left = count - i < 3 ? count - i : 3, group = 0, value;
        for (DWORD n = 0; n < (left == 1 ? 2u : left == 2 ? 3u : 4u); n++) {
            if (!take_char(at, end, &value))
                return FALSE;
            group |= value << (6 * n);
        }
        memcpy(bytes + i, &group, left);
    }
    return TRUE;
}

ADVAPI BOOL WINAPI CredUnmarshalCredentialW(LPCWSTR marshaled, PDWORD type, PVOID *credential)
{
    DWORD error = ERROR_INVALID_PARAMETER;
    PVOID block = nullptr;
    if (!credential)
        return FALSE;
    *credential = nullptr;
    SIZE_T length = marshaled ? wcslen(marshaled) : 0;
    if (length <= 2 || marshaled[0] != L'@' || marshaled[1] != L'@')
        goto fail;
    const WCHAR *at = marshaled + 2, *end = marshaled + length;
    DWORD kind;
    if (!take_char(&at, end, &kind))
        goto fail;
    if (type)
        *type = kind;
    switch (kind) {
    case CERT_CREDENTIAL: {
        CERT_CREDENTIAL_INFO_ *cert = block = LocalAlloc(LMEM_ZEROINIT, sizeof(*cert));
        if (!cert) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto fail;
        }
        cert->Size = sizeof(*cert);
        if (!take_bytes(&at, end, cert->Hash, 20))
            goto fail;
        break;
    }
    case BINARY_BLOB_CREDENTIAL:
    case BINARY_BLOB_FOR_SYSTEM:
    case USERNAME_TARGET_CREDENTIAL:
    case USERNAME_FOR_PACKED_CREDENTIALS: {
        BOOL binary = kind == BINARY_BLOB_CREDENTIAL || kind == BINARY_BLOB_FOR_SYSTEM;
        if (binary) {
            const WCHAR *mark = kind == BINARY_BLOB_CREDENTIAL ? g_blob_mark : g_system_blob_mark;
            if (end - at < 5 || memcmp(at, mark, sizeof(g_blob_mark)))
                goto fail;
            at += 5;
        }
        DWORD size = 0;
        if (!take_bytes(&at, end, (BYTE *)&size, 4) || !size || (size & 1) != 0)
            goto fail;
        SIZE_T header = sizeof(BINARY_BLOB_INFO_);
        block = LocalAlloc(LMEM_ZEROINIT, header + size + sizeof(WCHAR));
        if (!block) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto fail;
        }
        BYTE *data = (BYTE *)block + header;
        if (binary) {
            BINARY_BLOB_INFO_ *blob = block;
            blob->Size = size;
            blob->Data = data;
        } else {
            *(PWSTR *)block = (PWSTR)data;
        }
        if (!take_bytes(&at, end, data, size))
            goto fail;
        break;
    }
    default:
        goto fail;
    }
    if (at != end)
        goto fail;
    *credential = block;
    return TRUE;
fail:
    if (block)
        LocalFree(block);
    SetLastError(error);
    return FALSE;
}

ADVAPI BOOL WINAPI CredIsMarshaledCredentialW(LPCWSTR marshaled)
{
    DWORD type;
    PVOID credential = nullptr;
    if (!CredUnmarshalCredentialW(marshaled, &type, &credential))
        return FALSE;
    LocalFree(credential);
    return TRUE;
}

ADVAPI BOOL WINAPI CredMarshalCredentialA(DWORD type, PVOID credential, LPSTR *marshaled)
{
    LPWSTR wide = nullptr;
    if (!CredMarshalCredentialW(type, credential, &wide))
        return FALSE;
    PSTR ansi = AdvWideToAnsi(wide);
    LocalFree(wide);
    if (!ansi) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    SIZE_T bytes = strlen(ansi) + 1;
    *marshaled = LocalAlloc(LMEM_FIXED, bytes);
    if (*marshaled)
        memcpy(*marshaled, ansi, bytes);
    AdvFree(ansi);
    if (!*marshaled) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI CredUnmarshalCredentialA(LPCSTR marshaled, PDWORD type, PVOID *credential)
{
    PWSTR wide = AdvAnsiToWide(marshaled);
    if (!wide) {
        if (credential)
            *credential = nullptr;
        SetLastError(marshaled ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = CredUnmarshalCredentialW(wide, type, credential);
    DWORD error = ok ? 0 : GetLastError();
    AdvFree(wide);
    if (!ok)
        SetLastError(error);
    return ok;
}

/* ------------------------------------------------------------------ encoded credentials and blobs */

/* a credential as the manager passes it around: the public record, then the size of the blob before it was padded. */
typedef struct CREDENTIAL_RECORD {
    DWORD Flags;
    DWORD Type;
    PWSTR TargetName;
    PWSTR Comment;
    FILETIME LastWritten;
    DWORD BlobSize;
    PBYTE Blob;
    DWORD Persist;
    DWORD AttributeCount;
    PVOID Attributes;
    PWSTR TargetAlias;
    PWSTR UserName;
    DWORD PlainSize;
} CREDENTIAL_RECORD;

#define MEMORY_SAME_LOGON 2

/* pads the blob to the block size of the cipher and encrypts it for the logon session. */
ADVAPI BOOLEAN WINAPI CredpEncodeCredential(CREDENTIAL_RECORD *record)
{
    if (!record->Blob || !record->BlobSize) {
        record->Blob = nullptr;
        record->BlobSize = 0;
        record->PlainSize = 0;
        return TRUE;
    }
    DWORD plain = record->PlainSize, padded = (plain + 7) & ~7u;
    record->BlobSize = padded;
    if (padded != plain)
        memset(record->Blob + plain, 0, padded - plain);
    return SystemFunction040(record->Blob, record->BlobSize, MEMORY_SAME_LOGON) >= 0;
}

ADVAPI BOOLEAN WINAPI CredpDecodeCredential(CREDENTIAL_RECORD *record)
{
    if (!record->BlobSize)
        return TRUE;
    if (record->BlobSize >= record->PlainSize && SystemFunction041(record->Blob, record->BlobSize, MEMORY_SAME_LOGON) >= 0) {
        record->BlobSize = record->PlainSize;
        return TRUE;
    }
    return FALSE;
}

/* the secret with its length behind it, encrypted for the logon session. the caller frees it with LocalFree. */
static NTSTATUS encode_secret(BOOL revert, PVOID data, DWORD size, PVOID *encoded, ULONG *encoded_size)
{
    if (!data || !size) {
        *encoded = nullptr;
        *encoded_size = 0;
        return STATUS_SUCCESS;
    }
    DWORD padded = (size + 11) & ~7u;
    BYTE *buffer = LocalAlloc(LMEM_ZEROINIT, padded);
    if (!buffer)
        return STATUS_NO_MEMORY;
    HANDLE impersonation = nullptr, none = nullptr;
    BOOL reverted = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    if (revert && NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE | TOKEN_QUERY, TRUE, &impersonation))) {
        status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &none, sizeof(none));
        reverted = NT_SUCCESS(status);
    }
    if (NT_SUCCESS(status)) {
        memcpy(buffer, data, size);
        memcpy(buffer + padded - sizeof(DWORD), &size, sizeof(size));
        status = SystemFunction040(buffer, padded, MEMORY_SAME_LOGON);
    }
    if (reverted)
        NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &impersonation, sizeof(impersonation));
    if (impersonation)
        NtClose(impersonation);
    if (NT_SUCCESS(status)) {
        *encoded = buffer;
        *encoded_size = padded;
    } else {
        memset(buffer, 0, padded);
        LocalFree(buffer);
    }
    return status;
}

ADVAPI NTSTATUS WINAPI CredpEncodeSecret(DWORD revert, PVOID data, DWORD size, PVOID *encoded, ULONG *encoded_size)
{
    return encode_secret(revert, data, size, encoded, encoded_size);
}

/* encrypts a blob for the logon session and marshals it as text. */
ADVAPI DWORD WINAPI CredEncryptAndMarshalBinaryBlob(PVOID data, DWORD size, LPWSTR *marshaled)
{
    PVOID encoded = nullptr;
    ULONG encoded_size = 0;
    NTSTATUS status = encode_secret(TRUE, data, size, &encoded, &encoded_size);
    LPWSTR text = nullptr;
    if (NT_SUCCESS(status) && encoded) {
        BINARY_BLOB_INFO_ blob = {encoded_size, encoded};
        if (!CredMarshalCredentialW(BINARY_BLOB_CREDENTIAL, &blob, &text))
            status = STATUS_INVALID_PARAMETER;
    }
    if (encoded) {
        memset(encoded, 0, encoded_size);
        LocalFree(encoded);
    }
    if (NT_SUCCESS(status))
        *marshaled = text;
    return status;
}

/* ------------------------------------------------------------------ the credential manager */

/* a profile that loads or unloads has no credentials of its own here to bring or take away. */
ADVAPI BOOL WINAPI CredProfileLoaded(void)
{
    return TRUE;
}

ADVAPI BOOL WINAPI CredProfileLoadedEx(HANDLE token)
{
    (void)token;
    return TRUE;
}

ADVAPI BOOL WINAPI CredProfileUnloaded(void)
{
    return TRUE;
}

/* the store keeps nothing, so a backup has nothing to save and a restore nothing to read. */
ADVAPI BOOL WINAPI CredBackupCredentials(HANDLE token, PCWSTR path, PVOID key, DWORD key_size, DWORD flags)
{
    (void)token;
    (void)key;
    (void)key_size;
    (void)flags;
    SetLastError(path && *path ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI CredRestoreCredentials(HANDLE token, PCWSTR path, PVOID key, DWORD key_size)
{
    (void)token;
    (void)key;
    (void)key_size;
    SetLastError(path && *path ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI CredReadByTokenHandle(HANDLE token, PCWSTR target, DWORD type, DWORD flags, PVOID *credential)
{
    (void)token;
    (void)type;
    (void)flags;
    if (credential)
        *credential = nullptr;
    SetLastError(target && *target ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* the conversion of credentials between the character sets of the manager's interface needs its marshaled records. */
ADVAPI DWORD WINAPI CredpConvertCredential(DWORD charset, DWORD a2, PVOID credential, PVOID *converted)
{
    (void)charset;
    (void)a2;
    (void)converted;
    return credential ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

ADVAPI DWORD WINAPI CredpConvertOneCredentialSize(DWORD charset, PVOID credential)
{
    (void)charset;
    (void)credential;
    return 0;
}

ADVAPI DWORD WINAPI CredpConvertTargetInfo(DWORD charset, DWORD a2, PVOID info, PVOID *converted)
{
    (void)charset;
    (void)a2;
    (void)info;
    (void)converted;
    return ERROR_NOT_SUPPORTED;
}
