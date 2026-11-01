/*
 * credentials as sechost.dll handles them before they reach the credential manager: user names split into account and
 * domain, credentials marshaled to text, blobs encoded with the logon session, the copies of records in the three
 * character sets and the protected strings. the credential manager is in sechost_credman.c.
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

/* ------------------------------------------------------------------ records in the three character sets */

typedef struct CONVERT_ATTRIBUTE {
    PVOID Keyword;
    DWORD Flags;
    DWORD ValueSize;
    PBYTE Value;
} CONVERT_ATTRIBUTE;

/* the bytes a string takes in the character set: 1 gives ansi from unicode, 2 unicode from ansi, 3 unicode again. */
static SIZE_T convert_bytes(DWORD charset, const void *s)
{
    if (!s)
        return 0;
    if (charset == 1)
        return WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (charset == 2)
        return MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0) * sizeof(WCHAR);
    return (wcslen(s) + 1) * sizeof(WCHAR);
}

static PVOID convert_put(DWORD charset, const void *s, BYTE **at)
{
    if (!s)
        return nullptr;
    PVOID p = *at;
    if (charset == 1) {
        *at += WideCharToMultiByte(CP_ACP, 0, s, -1, (PSTR)p, 0x10000, nullptr, nullptr);
    } else if (charset == 2) {
        *at += MultiByteToWideChar(CP_ACP, 0, s, -1, (PWSTR)p, 0x8000) * sizeof(WCHAR);
    } else {
        SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
        memcpy(p, s, bytes);
        *at += bytes;
    }
    return p;
}

/* the size of a converted credential: the record of the manager, the secret, the attributes and the strings. */
ADVAPI DWORD WINAPI CredpConvertOneCredentialSize(DWORD charset, PVOID credential)
{
    const BYTE *c = credential;
    DWORD count, blob;
    memcpy(&blob, c + 32, 4);
    memcpy(&count, c + 52, 4);
    SIZE_T size = 88 + (SIZE_T)count * 24;
    if (blob)
        size += (blob + 7) & ~7ull;
    static const int offsets[] = {8, 16, 64, 72};
    for (int i = 0; i < 4; i++)
        size += convert_bytes(charset, *(const void *const *)(c + offsets[i]));
    const CONVERT_ATTRIBUTE *attributes = *(const CONVERT_ATTRIBUTE *const *)(c + 56);
    for (DWORD i = 0; i < count; i++) {
        size += convert_bytes(charset, attributes[i].Keyword);
        size += (attributes[i].ValueSize + 7) & ~7ull;
    }
    size = (size + 7) & ~7ull;
    return size > 0xFFFFFFFFull ? 0 : (DWORD)size;
}

/*
 * converts a credential into a new block. mode 0 takes the public record and encodes its secret for the logon session,
 * 1 takes a record of the manager and decodes the secret, 2 keeps it as it is.
 */
ADVAPI DWORD WINAPI CredpConvertCredential(DWORD charset, DWORD mode, PVOID credential, PVOID *converted)
{
    if (!credential)
        return ERROR_INVALID_PARAMETER;
    DWORD size = CredpConvertOneCredentialSize(charset, credential);
    if (!size)
        return ERROR_NOT_ENOUGH_MEMORY;
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, size);
    if (!memory)
        return ERROR_NOT_ENOUGH_MEMORY;
    const BYTE *in = credential;
    CREDENTIAL_RECORD *r = (CREDENTIAL_RECORD *)memory;
    BYTE *at = memory + 88;
    DWORD blob;
    memcpy(&r->Flags, in, 8);
    memcpy(&r->LastWritten, in + 24, 8);
    memcpy(&blob, in + 32, 4);
    memcpy(&r->Persist, in + 48, 8);
    DWORD count = r->AttributeCount;
    r->BlobSize = blob;
    if (blob) {
        const BYTE *source = *(const BYTE *const *)(in + 40);
        r->Blob = at;
        memcpy(at, source, blob);
        at += (blob + 7) & ~7ull;
        if (mode == 1) {
            memcpy(&r->PlainSize, in + 80, 4);
            CredpDecodeCredential(r);
        } else if (mode == 2) {
            r->PlainSize = blob;
        } else {
            r->PlainSize = blob;
            if (!CredpEncodeCredential(r)) {
                LocalFree(memory);
                return ERROR_INVALID_PARAMETER;
            }
        }
    }
    const CONVERT_ATTRIBUTE *attributes = *(const CONVERT_ATTRIBUTE *const *)(in + 56);
    CONVERT_ATTRIBUTE *out = nullptr;
    if (count) {
        out = (CONVERT_ATTRIBUTE *)at;
        r->Attributes = out;
        at += (SIZE_T)count * 24;
        for (DWORD i = 0; i < count; i++) {
            out[i].Flags = attributes[i].Flags;
            out[i].ValueSize = attributes[i].ValueSize;
            if (attributes[i].ValueSize) {
                out[i].Value = at;
                memcpy(at, attributes[i].Value, attributes[i].ValueSize);
                at += (attributes[i].ValueSize + 7) & ~7ull;
            }
        }
    }
    r->TargetName = convert_put(charset, *(const void *const *)(in + 8), &at);
    r->Comment = convert_put(charset, *(const void *const *)(in + 16), &at);
    r->TargetAlias = convert_put(charset, *(const void *const *)(in + 64), &at);
    r->UserName = convert_put(charset, *(const void *const *)(in + 72), &at);
    for (DWORD i = 0; i < count; i++)
        out[i].Keyword = convert_put(charset, attributes[i].Keyword, &at);
    *converted = memory;
    return ERROR_SUCCESS;
}

/* the target information in a character set: the names, the credential types and the flags, in one block. */
ADVAPI DWORD WINAPI CredpConvertTargetInfo(DWORD charset, PVOID info, PVOID *converted, PDWORD size)
{
    if (!info)
        return ERROR_INVALID_PARAMETER;
    const BYTE *in = info;
    DWORD flags, count;
    memcpy(&flags, in + 56, 4);
    memcpy(&count, in + 60, 4);
    SIZE_T bytes = 72 + (SIZE_T)count * 4;
    for (int i = 0; i < 7; i++)
        bytes += convert_bytes(charset, *(const void *const *)(in + 8 * i));
    bytes = (bytes + 7) & ~7ull;
    if (size)
        *size = (DWORD)bytes;
    if (!converted)
        return ERROR_SUCCESS;
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, bytes);
    if (!memory)
        return ERROR_NOT_ENOUGH_MEMORY;
    BYTE *at = memory + 72;
    memcpy(memory + 56, &flags, 4);
    memcpy(memory + 60, &count, 4);
    if (count) {
        *(PVOID *)(memory + 64) = at;
        memcpy(at, *(const void *const *)(in + 64), count * 4);
        at += count * 4;
    }
    for (int i = 0; i < 7; i++)
        *(PVOID *)(memory + 8 * i) = convert_put(charset, *(const void *const *)(in + 8 * i), &at);
    *converted = memory;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ protected credential strings */

#define MEMORY_FOR_SYSTEM 4
#define MARSHAL_BLOB 3
#define MARSHAL_BLOB_FOR_SYSTEM 5

/* the logon id of the caller's token. */
static BOOL caller_logon_id(LUID *id)
{
    HANDLE token;
    NTSTATUS status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &token);
    if (!NT_SUCCESS(status))
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token);
    if (!NT_SUCCESS(status))
        return FALSE;
    BYTE statistics[64] = {};
    ULONG length;
    status = NtQueryInformationToken(token, 10, statistics, sizeof(statistics), &length);
    NtClose(token);
    if (NT_SUCCESS(status))
        memcpy(id, statistics + 8, sizeof(*id));
    return NT_SUCCESS(status);
}

/* runs with the thread's impersonation put aside when the secret is the process's own. */
static NTSTATUS protect_secret(BOOL as_self, BOOL system, const void *data, DWORD size, PVOID *encoded, ULONG *encoded_size,
                               LUID *logon)
{
    if (!data || !size) {
        *encoded = nullptr;
        *encoded_size = 0;
        return STATUS_SUCCESS;
    }
    HANDLE impersonation = nullptr, none = nullptr;
    BOOL reverted = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD padded = (size + 11) & ~7u;
    BYTE *buffer = LocalAlloc(LMEM_ZEROINIT, padded);
    if (!buffer)
        return STATUS_NO_MEMORY;
    if (as_self && NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE | TOKEN_QUERY, TRUE, &impersonation))) {
        status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &none, sizeof(none));
        reverted = NT_SUCCESS(status);
    }
    if (NT_SUCCESS(status) && logon && !caller_logon_id(logon))
        status = STATUS_UNSUCCESSFUL;
    if (NT_SUCCESS(status)) {
        memcpy(buffer, data, size);
        memcpy(buffer + padded - sizeof(DWORD), &size, sizeof(size));
        status = SystemFunction040(buffer, padded, system ? MEMORY_FOR_SYSTEM : MEMORY_SAME_LOGON);
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

ADVAPI BOOL WINAPI CredProtectEx(ULONG flags, LPWSTR credentials, DWORD chars, LPWSTR protected_text, DWORD *max_chars,
                                 DWORD *protection)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (credentials && max_chars && (!*max_chars || protected_text) && (flags & 3) == flags) {
        BOOL system = (flags & 2) != 0;
        PVOID encoded = nullptr;
        ULONG encoded_size = 0;
        LUID logon = {};
        status = protect_secret(flags & 1, system, credentials, chars * sizeof(WCHAR), &encoded, &encoded_size, &logon);
        LPWSTR text = nullptr;
        if (NT_SUCCESS(status) && encoded) {
            BYTE *data = LocalAlloc(LMEM_FIXED, encoded_size + sizeof(LUID));
            if (!data) {
                status = STATUS_NO_MEMORY;
            } else {
                memcpy(data, &logon, sizeof(LUID));
                memcpy(data + sizeof(LUID), encoded, encoded_size);
                BINARY_BLOB_INFO_ blob = {encoded_size + sizeof(LUID), data};
                if (!CredMarshalCredentialW(system ? MARSHAL_BLOB_FOR_SYSTEM : MARSHAL_BLOB, &blob, &text))
                    status = STATUS_INVALID_PARAMETER;
                LocalFree(data);
            }
        }
        if (encoded) {
            memset(encoded, 0, encoded_size);
            LocalFree(encoded);
        }
        if (NT_SUCCESS(status)) {
            DWORD needed = text ? (DWORD)wcslen(text) + 1 : 0;
            if (needed > *max_chars) {
                *max_chars = needed;
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                if (needed)
                    memcpy(protected_text, text, needed * sizeof(WCHAR));
                *max_chars = needed;
                if (protection)
                    *protection = system ? 3 : (logon.LowPart == 0x3E7 && !logon.HighPart) + 1;
            }
        }
        if (text)
            LocalFree(text);
    }
    if (NT_SUCCESS(status))
        return TRUE;
    SetLastError(RtlNtStatusToDosError(status));
    return FALSE;
}

ADVAPI BOOL WINAPI CredProtectW(BOOL as_self, LPWSTR credentials, DWORD chars, LPWSTR protected_text, DWORD *max_chars,
                                DWORD *protection)
{
    return CredProtectEx(as_self != 0, credentials, chars, protected_text, max_chars, protection);
}

/* what a protected string holds: the logon id, then the encrypted secret. */
static NTSTATUS unprotect_secret(BOOL as_self, BOOL system, const BYTE *data, DWORD size, PVOID *plain, ULONG *plain_size)
{
    if (size < sizeof(LUID))
        return STATUS_INVALID_PARAMETER;
    LUID logon;
    memcpy(&logon, data, sizeof(logon));
    data += sizeof(LUID);
    size -= sizeof(LUID);
    if (!size) {
        *plain = nullptr;
        *plain_size = 0;
        return STATUS_SUCCESS;
    }
    BYTE *buffer = LocalAlloc(LMEM_ZEROINIT, size);
    if (!buffer)
        return STATUS_NO_MEMORY;
    memcpy(buffer, data, size);
    HANDLE impersonation = nullptr, none = nullptr;
    BOOL reverted = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    if (as_self && NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE | TOKEN_QUERY, TRUE, &impersonation))) {
        status = NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &none, sizeof(none));
        reverted = NT_SUCCESS(status);
    }
    if (NT_SUCCESS(status) && !system) {
        LUID mine = {};
        if (!caller_logon_id(&mine))
            status = STATUS_UNSUCCESSFUL;
        else if (mine.LowPart != logon.LowPart || mine.HighPart != logon.HighPart)
            status = (NTSTATUS)0xC0000429;
    }
    if (NT_SUCCESS(status))
        status = SystemFunction041(buffer, size, system ? MEMORY_FOR_SYSTEM : MEMORY_SAME_LOGON);
    if (reverted)
        NtSetInformationThread(NtCurrentThread(), ThreadImpersonationToken, &impersonation, sizeof(impersonation));
    if (impersonation)
        NtClose(impersonation);
    DWORD length = 0;
    if (NT_SUCCESS(status)) {
        if (size >= 4)
            memcpy(&length, buffer + size - 4, 4);
        if (!length || length > size - 4)
            status = STATUS_INVALID_PARAMETER;
    }
    if (NT_SUCCESS(status)) {
        *plain = buffer;
        *plain_size = length;
    } else {
        memset(buffer, 0, size);
        LocalFree(buffer);
    }
    return status;
}

ADVAPI BOOL WINAPI CredUnprotectEx(ULONG flags, LPWSTR protected_text, DWORD chars, LPWSTR credentials, DWORD *max_chars)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (protected_text && max_chars && (!*max_chars || credentials) && (flags & 3) == flags) {
        BOOL system = (flags & 2) != 0, not_protected = FALSE;
        PVOID plain = nullptr;
        ULONG plain_size = 0;
        DWORD kind = 0;
        BINARY_BLOB_INFO_ *blob = nullptr;
        if (CredUnmarshalCredentialW(protected_text, &kind, (PVOID *)&blob)) {
            if (kind == MARSHAL_BLOB || (system && kind == MARSHAL_BLOB_FOR_SYSTEM))
                status = blob ? unprotect_secret(flags & 1, kind == MARSHAL_BLOB_FOR_SYSTEM, blob->Data, blob->Size, &plain, &plain_size)
                              : STATUS_INVALID_PARAMETER;
        } else if (GetLastError() == ERROR_INVALID_PARAMETER) {
            not_protected = TRUE;
        }
        if (blob)
            LocalFree(blob);
        const void *source = plain;
        DWORD source_chars = plain_size / sizeof(WCHAR);
        if (!NT_SUCCESS(status) && not_protected) {
            status = STATUS_SUCCESS;
            source = protected_text;
            source_chars = chars;
        }
        if (NT_SUCCESS(status)) {
            if (source_chars <= *max_chars) {
                if (source_chars)
                    memcpy(credentials, source, source_chars * sizeof(WCHAR));
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            *max_chars = source_chars;
        }
        if (plain) {
            memset(plain, 0, plain_size);
            LocalFree(plain);
        }
    }
    if (NT_SUCCESS(status))
        return TRUE;
    SetLastError(RtlNtStatusToDosError(status));
    return FALSE;
}

ADVAPI BOOL WINAPI CredUnprotectW(BOOL as_self, LPWSTR protected_text, DWORD chars, LPWSTR credentials, DWORD *max_chars)
{
    return CredUnprotectEx(as_self != 0, protected_text, chars, credentials, max_chars);
}

/* a protected string: the logon id of the one who protected it tells whose protection it is. */
ADVAPI BOOL WINAPI CredIsProtectedW(LPWSTR protected_text, DWORD *protection)
{
    if (!protected_text || !protection) {
        SetLastError(RtlNtStatusToDosError(STATUS_INVALID_PARAMETER));
        return FALSE;
    }
    DWORD kind = 0;
    BINARY_BLOB_INFO_ *blob = nullptr;
    if (!CredUnmarshalCredentialW(protected_text, &kind, (PVOID *)&blob) || (kind != MARSHAL_BLOB && kind != MARSHAL_BLOB_FOR_SYSTEM)) {
        *protection = 0;
    } else if (blob && blob->Data && blob->Size > 8) {
        LUID system = {0x3E7, 0};
        *protection = RtlCompareMemory(blob->Data, &system, sizeof(system)) == sizeof(system) ? 2 : 1;
    }
    if (blob)
        LocalFree(blob);
    return TRUE;
}

ADVAPI BOOL WINAPI CredIsProtectedA(LPSTR protected_text, DWORD *protection)
{
    PWSTR wide = AdvAnsiToWide(protected_text);
    if (!wide) {
        SetLastError(protected_text ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = CredIsProtectedW(wide, protection);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (!ok)
        SetLastError(error);
    return ok;
}

ADVAPI BOOL WINAPI CredProtectA(BOOL as_self, LPSTR credentials, DWORD chars, LPSTR protected_text, DWORD *max_chars,
                                DWORD *protection)
{
    PWSTR wide = AdvAnsiToWide(credentials);
    if (!wide) {
        SetLastError(credentials ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR out = nullptr;
    if (protected_text && max_chars && *max_chars) {
        out = AdvAlloc(*max_chars * sizeof(WCHAR));
        if (!out) {
            AdvFree(wide);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    BOOL ok = CredProtectEx(as_self != 0, wide, chars, out, max_chars, protection);
    DWORD error = ok ? 0 : GetLastError();
    if (ok && out) {
        PSTR ansi = AdvWideToAnsi(out);
        if (ansi) {
            memcpy(protected_text, ansi, strlen(ansi) + 1);
            AdvFree(ansi);
        }
    }
    memset(wide, 0, chars * sizeof(WCHAR));
    AdvFree(wide);
    AdvFree(out);
    if (!ok)
        SetLastError(error);
    return ok;
}

ADVAPI BOOL WINAPI CredUnprotectA(BOOL as_self, LPSTR protected_text, DWORD chars, LPSTR credentials, DWORD *max_chars)
{
    PWSTR wide = AdvAnsiToWide(protected_text);
    if (!wide) {
        SetLastError(protected_text ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR out = nullptr;
    if (credentials && max_chars && *max_chars) {
        out = AdvAlloc(*max_chars * sizeof(WCHAR));
        if (!out) {
            AdvFree(wide);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    BOOL ok = CredUnprotectEx(as_self != 0, wide, chars, out, max_chars);
    DWORD error = ok ? 0 : GetLastError();
    if (ok && out) {
        PSTR ansi = AdvWideToAnsi(out);
        if (ansi) {
            memcpy(credentials, ansi, strlen(ansi) + 1);
            memset(ansi, 0, strlen(ansi));
            AdvFree(ansi);
        }
        memset(out, 0, *max_chars * sizeof(WCHAR));
    }
    AdvFree(wide);
    AdvFree(out);
    if (!ok)
        SetLastError(error);
    return ok;
}
