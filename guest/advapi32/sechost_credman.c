/*
 * the credential manager. windows keeps it in lsass and sechost.dll reaches it through a local procedure call. mime
 * has no lsass, so the functions of sechost run it in the calling process, over the same stores: credentials that
 * persist for the machine or the enterprise live in the user's AppData\Local and AppData\Roaming folders, in
 * Microsoft\Credentials, one file for each credential named by the MD5 of its canonical target. a file holds a header
 * of three DWORD, then a DPAPI blob of the marshaled credential. credentials of the logon session live in a volatile
 * key of the user's registry, encrypted for the logon session, and go away with the machine as they do with the
 * session on windows.
 *
 * a target name is stored in its canonical form, namespace:attribute=name, with the namespace Domain for domain
 * credentials and LegacyGeneric for the others. the layout of the marshaled credential and the rules of validation
 * were read from lsasrv.dll, build 26300.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/ntapi.h>
#include <mime/win32/dpapi.h>
#include <crypto/crypto.h>

enum { CRED_TYPE_GENERIC = 1, CRED_TYPE_DOMAIN_PASSWORD, CRED_TYPE_DOMAIN_CERTIFICATE, CRED_TYPE_DOMAIN_VISIBLE_PASSWORD,
       CRED_TYPE_GENERIC_CERTIFICATE, CRED_TYPE_DOMAIN_EXTENDED };

#define CRED_PERSIST_SESSION 1
#define CRED_PERSIST_LOCAL_MACHINE 2
#define CRED_PERSIST_ENTERPRISE 3

#define CRED_FLAGS_PASSWORD_FOR_CERT 0x01
#define CRED_FLAGS_PROMPT_NOW 0x02
#define CRED_FLAGS_USERNAME_TARGET 0x04
#define CRED_FLAGS_OWF_CRED_BLOB 0x08
#define CRED_FLAGS_REQUIRE_CONFIRMATION 0x10
#define CRED_FLAGS_WILDCARD_MATCH 0x20
#define CRED_FLAGS_NGC_CERT 0x80

#define CRED_PRESERVE_CREDENTIAL_BLOB 0x1
#define CRED_ENUMERATE_ALL_CREDENTIALS 0x1
#define CRED_CACHE_TARGET_INFORMATION 0x1

#define CRED_MAX_STRING_LENGTH 256
#define CRED_MAX_USERNAME_LENGTH 513
#define CRED_MAX_GENERIC_TARGET_NAME_LENGTH 32767
#define CRED_MAX_DOMAIN_TARGET_NAME_LENGTH 337
#define CRED_MAX_CREDENTIAL_BLOB_SIZE 2560
#define CRED_MAX_ATTRIBUTES 64
#define CRED_MAX_VALUE_SIZE 256
#define ERROR_NO_SUCH_LOGON_SESSION 1312
#define ERROR_ENCRYPTION_FAILED 6000

#define STATUS_INVALID_PARAMETER_1_ ((int)0xC00000EF)
#define STATUS_INVALID_PARAMETER_2_ ((int)0xC00000F0)
#define STATUS_INVALID_PARAMETER_3_ ((int)0xC00000F1)
#define STATUS_INVALID_PARAMETER_5_ ((int)0xC00000F3)
#define STATUS_INVALID_ACCOUNT_NAME__ ((int)0xC0000062)
#define STATUS_NO_SUCH_LOGON_SESSION_ ((int)0xC000005F)
#define STATUS_NOT_FOUND_ ((int)0xC0000225)
#define STATUS_ACCESS_DENIED_ ((int)0xC0000022)
#define STATUS_NO_MEMORY_ ((int)0xC0000017)
#define STATUS_BUFFER_TOO_SMALL_ ((int)0xC0000023)
#define STATUS_INVALID_PARAMETER__ ((int)0xC000000D)

typedef struct CREDENTIAL_ATTRIBUTEW_ {
    PWSTR Keyword;
    DWORD Flags;
    DWORD ValueSize;
    PBYTE Value;
} CREDENTIAL_ATTRIBUTEW_;

typedef struct CREDENTIALW_ {
    DWORD Flags;
    DWORD Type;
    PWSTR TargetName;
    PWSTR Comment;
    FILETIME LastWritten;
    DWORD CredentialBlobSize;
    PBYTE CredentialBlob;
    DWORD Persist;
    DWORD AttributeCount;
    CREDENTIAL_ATTRIBUTEW_ *Attributes;
    PWSTR TargetAlias;
    PWSTR UserName;
} CREDENTIALW_;

typedef struct CREDENTIAL_TARGET_INFORMATIONW_ {
    PWSTR TargetName;
    PWSTR NetbiosServerName;
    PWSTR DnsServerName;
    PWSTR NetbiosDomainName;
    PWSTR DnsDomainName;
    PWSTR DnsTreeName;
    PWSTR PackageName;
    ULONG Flags;
    DWORD CredTypeCount;
    DWORD *CredTypes;
} CREDENTIAL_TARGET_INFORMATIONW_;

ADVAPI NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG length, ULONG flags);
ADVAPI NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG length, ULONG flags);
ADVAPI BOOL WINAPI CredUnmarshalCredentialW(LPCWSTR marshaled, PDWORD type, PVOID *credential);
ADVAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *result);

/* ------------------------------------------------------------------ names of the target attributes */

static const WCHAR *const g_attribute_names[] = {L"target", L"name", L"batch", L"interactive", L"service", L"network",
                                                 L"networkcleartext", L"remoteinteractive", L"cachedinteractive"};
#define ATTRIBUTE_COUNT 9
#define ATTRIBUTE_TARGET 0
#define ATTRIBUTE_NAME 1

static const WCHAR g_generic_namespace[] = L"LegacyGeneric";
static const WCHAR g_domain_namespace[] = L"Domain";

/* ------------------------------------------------------------------ the credential in the store */

typedef struct CM_ATTRIBUTE {
    PWSTR Keyword;
    DWORD Flags;
    DWORD ValueSize;
    PBYTE Value;
} CM_ATTRIBUTE;

typedef struct CM_CREDENTIAL {
    DWORD Flags;
    DWORD Type;
    DWORD Visible;
    DWORD Persist;
    DWORD AttributeType;
    FILETIME LastWritten;
    PWSTR Namespace;
    PWSTR Name;
    PWSTR Comment;
    PWSTR Alias;
    PWSTR User;
    PBYTE Blob;
    DWORD BlobSize;
    DWORD AttributeCount;
    CM_ATTRIBUTE *Attributes;
} CM_CREDENTIAL;

static PWSTR copy_string(PCWSTR s)
{
    if (!s)
        return nullptr;
    SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    PWSTR copy = AdvAlloc(bytes);
    if (copy)
        memcpy(copy, s, bytes);
    return copy;
}

static PBYTE copy_bytes(const BYTE *data, DWORD size)
{
    if (!data || !size)
        return nullptr;
    PBYTE copy = AdvAlloc(size);
    if (copy)
        memcpy(copy, data, size);
    return copy;
}

static void free_credential(CM_CREDENTIAL *c)
{
    if (!c)
        return;
    AdvFree(c->Namespace);
    AdvFree(c->Name);
    AdvFree(c->Comment);
    AdvFree(c->Alias);
    AdvFree(c->User);
    if (c->Blob) {
        memset(c->Blob, 0, c->BlobSize);
        AdvFree(c->Blob);
    }
    for (DWORD i = 0; i < c->AttributeCount && c->Attributes; i++) {
        AdvFree(c->Attributes[i].Keyword);
        AdvFree(c->Attributes[i].Value);
    }
    AdvFree(c->Attributes);
    AdvFree(c);
}

static BOOL equal_ci(PCWSTR a, PCWSTR b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

/* ------------------------------------------------------------------ statuses and errors */

static DWORD status_error(int status)
{
    if (!status)
        return ERROR_SUCCESS;
    if (status == STATUS_INVALID_ACCOUNT_NAME__)
        return 2202;
    if (status == STATUS_INVALID_PARAMETER_1_)
        return ERROR_INVALID_FLAGS;
    return RtlNtStatusToDosError(status);
}

/* ------------------------------------------------------------------ names and validation */

static BOOL name_char_bad(WCHAR c)
{
    return c < 0x20 || wcschr(L"\"/\\[]:|<>+=;?,*", c);
}

/* a user name of an account or a computer: up to 256 characters without control characters or the separators. */
static BOOL user_part_valid(PCWSTR s, SIZE_T length)
{
    if (!length || length > CRED_MAX_STRING_LENGTH)
        return FALSE;
    for (SIZE_T i = 0; i < length; i++)
        if (name_char_bad(s[i]))
            return FALSE;
    return TRUE;
}

/* a netbios name: up to 15 characters without the characters a name cannot hold. */
static BOOL netbios_name_valid(PCWSTR s, SIZE_T length)
{
    if (!length || length > 15)
        return FALSE;
    for (SIZE_T i = 0; i < length; i++)
        if (name_char_bad(s[i]) || (s[i] == L' ' && (i == 0 || i == length - 1)))
            return FALSE;
    return s[0] != L'.';
}

/* a dns name: labels of up to 63 letters, digits, hyphens and underscores, a wildcard for the first label allowed. */
static BOOL dns_name_valid(PCWSTR s, SIZE_T length, BOOL wildcard)
{
    if (!length || length > 255 || s[length - 1] == L'.')
        return FALSE;
    SIZE_T start = 0;
    for (SIZE_T i = 0; i <= length; i++) {
        if (i < length && s[i] != L'.')
            continue;
        SIZE_T label = i - start;
        if (!label || label > 63)
            return FALSE;
        if (!(wildcard && start == 0 && label == 1 && s[0] == L'*')) {
            for (SIZE_T k = start; k < i; k++) {
                WCHAR c = s[k];
                if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_'))
                    return FALSE;
            }
            if (s[start] == L'-' || s[i - 1] == L'-')
                return FALSE;
        }
        start = i + 1;
    }
    return TRUE;
}

/*
 * a user name of a credential. domain credentials need DOMAIN\user, user@dns.name or a bare account name, a certificate
 * credential a marshaled certificate. returns a status, STATUS_INVALID_ACCOUNT_NAME for a name that is not valid.
 */
static int validate_user_name(PCWSTR user, DWORD type)
{
    if (!user)
        return STATUS_INVALID_ACCOUNT_NAME__;
    SIZE_T length = wcslen(user);
    if (!length || length > CRED_MAX_USERNAME_LENGTH)
        return STATUS_INVALID_ACCOUNT_NAME__;
    if (type == CRED_TYPE_DOMAIN_CERTIFICATE) {
        DWORD kind = 0;
        PVOID cert = nullptr;
        if (!CredUnmarshalCredentialW(user, &kind, &cert) || kind != 1) {
            if (cert)
                LocalFree(cert);
            return STATUS_INVALID_ACCOUNT_NAME__;
        }
        LocalFree(cert);
        return 0;
    }
    if (type != CRED_TYPE_DOMAIN_PASSWORD && type != CRED_TYPE_DOMAIN_VISIBLE_PASSWORD)
        return 0;
    PCWSTR slash = wcsrchr(user, L'\\');
    if (slash) {
        SIZE_T domain = slash - user;
        PCWSTR host = user;
        if (domain >= 8 && !_wcsnicmp(user, L"TERMSRV/", 8)) {
            host += 8;
            domain -= 8;
        }
        if (!(dns_name_valid(host, domain, FALSE) || netbios_name_valid(host, domain)) ||
            !user_part_valid(slash + 1, wcslen(slash + 1)))
            return STATUS_INVALID_ACCOUNT_NAME__;
        return 0;
    }
    PCWSTR at = wcsrchr(user, L'@');
    if (at) {
        if (at == user || !dns_name_valid(at + 1, wcslen(at + 1), FALSE))
            return STATUS_INVALID_ACCOUNT_NAME__;
        return 0;
    }
    return user_part_valid(user, length) ? 0 : STATUS_INVALID_ACCOUNT_NAME__;
}

typedef struct TARGET_NAME {
    PWSTR Namespace;
    PWSTR Name;
    DWORD Attribute;
    BOOL Plain;
    BOOL Wildcard;
} TARGET_NAME;

static void free_target(TARGET_NAME *t)
{
    AdvFree(t->Namespace);
    AdvFree(t->Name);
    t->Namespace = t->Name = nullptr;
}

static BOOL attribute_from_name(PCWSTR s, SIZE_T length, DWORD *attribute)
{
    for (DWORD i = 0; i < ATTRIBUTE_COUNT; i++)
        if (wcslen(g_attribute_names[i]) == length && !_wcsnicmp(s, g_attribute_names[i], length)) {
            *attribute = i;
            return TRUE;
        }
    return FALSE;
}

/*
 * splits a target name into namespace, attribute and name. a name already in the canonical form keeps its parts; a
 * plain name takes the namespace of its type. domain targets keep the host or domain that names them.
 */
static int parse_target(PCWSTR target, DWORD type, BOOL validate, TARGET_NAME *out)
{
    memset(out, 0, sizeof(*out));
    if (!target)
        return STATUS_INVALID_PARAMETER__;
    PCWSTR colon = wcschr(target, L':');
    PCWSTR equals = colon ? wcschr(colon + 1, L'=') : nullptr;
    DWORD attribute;
    PCWSTR ns = nullptr, name = nullptr;
    SIZE_T ns_length = 0;
    if (equals && attribute_from_name(colon + 1, equals - colon - 1, &attribute)) {
        ns = target;
        ns_length = colon - target;
        name = equals + 1;
        if (!ns_length || ns_length > CRED_MAX_STRING_LENGTH)
            return STATUS_INVALID_PARAMETER__;
        if (ns_length == 6 && !_wcsnicmp(ns, g_domain_namespace, 6)) {
            if (validate && type != CRED_TYPE_DOMAIN_PASSWORD && type != CRED_TYPE_DOMAIN_CERTIFICATE)
                return STATUS_INVALID_PARAMETER__;
        } else if (validate && type != CRED_TYPE_GENERIC && type != CRED_TYPE_GENERIC_CERTIFICATE) {
            return STATUS_INVALID_PARAMETER__;
        }
        out->Attribute = attribute;
    } else {
        out->Plain = TRUE;
        out->Attribute = ATTRIBUTE_TARGET;
        name = target;
        if (type == CRED_TYPE_DOMAIN_EXTENDED) {
            return STATUS_INVALID_PARAMETER__;
        } else if (type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD) {
            PCWSTR slash = wcschr(target, L'\\');
            if (slash) {
                ns = target;
                ns_length = slash - target;
                name = slash + 1;
                if (!*name || !ns_length)
                    return STATUS_INVALID_PARAMETER__;
            } else {
                ns = g_generic_namespace;
                ns_length = 13;
            }
        } else if (type == CRED_TYPE_GENERIC || type == CRED_TYPE_GENERIC_CERTIFICATE) {
            ns = g_generic_namespace;
            ns_length = 13;
        } else if (type == CRED_TYPE_DOMAIN_PASSWORD || type == CRED_TYPE_DOMAIN_CERTIFICATE) {
            ns = g_domain_namespace;
            ns_length = 6;
        } else {
            return STATUS_INVALID_PARAMETER__;
        }
    }
    SIZE_T name_length = wcslen(name);
    SIZE_T limit = out->Attribute == ATTRIBUTE_NAME ? CRED_MAX_USERNAME_LENGTH
                   : type == CRED_TYPE_GENERIC || type == CRED_TYPE_GENERIC_CERTIFICATE ? CRED_MAX_GENERIC_TARGET_NAME_LENGTH - 1
                                                                                      : CRED_MAX_DOMAIN_TARGET_NAME_LENGTH;
    if (!name_length || name_length > limit)
        return STATUS_INVALID_PARAMETER__;
    out->Namespace = AdvAlloc((ns_length + 1) * sizeof(WCHAR));
    out->Name = copy_string(name);
    if (!out->Namespace || !out->Name) {
        free_target(out);
        return STATUS_NO_MEMORY_;
    }
    memcpy(out->Namespace, ns, ns_length * sizeof(WCHAR));
    out->Namespace[ns_length] = 0;
    return 0;
}

/* a domain target is a host, a domain, a wildcard of them or one of the special names. */
static int validate_domain_target(PCWSTR name, DWORD persist)
{
    if (!wcscmp(name, L"*"))
        return 0;
    if (!_wcsicmp(name, L"*Session"))
        return persist == CRED_PERSIST_SESSION ? 0 : STATUS_INVALID_PARAMETER__;
    PCWSTR host = name;
    if (!_wcsnicmp(host, L"TERMSRV/", 8))
        host += 8;
    SIZE_T length = wcslen(host);
    if (dns_name_valid(host, length, TRUE) || netbios_name_valid(host, length))
        return 0;
    PCWSTR slash = wcschr(host, L'\\');
    if (slash && !wcscmp(slash + 1, L"*") && netbios_name_valid(host, slash - host))
        return 0;
    return STATUS_INVALID_PARAMETER__;
}

/* the credential of the caller's record, validated and in its canonical form. returns a status. */
static int make_credential(const CREDENTIALW_ *in, CM_CREDENTIAL **result)
{
    *result = nullptr;
    if (!in)
        return STATUS_INVALID_PARAMETER__;
    DWORD flags = in->Flags;
    if (flags & 0xFFFF0F20)
        return STATUS_INVALID_PARAMETER__;
    if ((flags & CRED_FLAGS_NGC_CERT) && (in->Type != CRED_TYPE_DOMAIN_CERTIFICATE || in->Persist != CRED_PERSIST_SESSION))
        return STATUS_INVALID_PARAMETER__;
    if (!(flags & CRED_FLAGS_USERNAME_TARGET) && (flags & 0xF000))
        return STATUS_INVALID_PARAMETER__;
    if ((in->Type == CRED_TYPE_DOMAIN_PASSWORD || in->Type == CRED_TYPE_DOMAIN_CERTIFICATE) &&
        (flags & CRED_FLAGS_REQUIRE_CONFIRMATION))
        return STATUS_INVALID_PARAMETER__;
    if (in->Persist < CRED_PERSIST_SESSION || in->Persist > CRED_PERSIST_ENTERPRISE)
        return STATUS_INVALID_PARAMETER__;
    flags &= ~(CRED_FLAGS_PROMPT_NOW | CRED_FLAGS_PASSWORD_FOR_CERT | CRED_FLAGS_OWF_CRED_BLOB);

    TARGET_NAME target;
    int status = parse_target(in->TargetName, in->Type, TRUE, &target);
    if (status < 0)
        return status;
    DWORD type = in->Type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD ? CRED_TYPE_GENERIC : in->Type;
    if (target.Plain && (in->Type == CRED_TYPE_DOMAIN_PASSWORD || in->Type == CRED_TYPE_DOMAIN_CERTIFICATE)) {
        status = validate_domain_target(target.Name, in->Persist);
        if (status < 0) {
            free_target(&target);
            return status;
        }
    }
    if (target.Plain && in->Type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD)
        type = CRED_TYPE_GENERIC;

    SIZE_T comment = in->Comment ? wcslen(in->Comment) : 0, alias = in->TargetAlias ? wcslen(in->TargetAlias) : 0;
    SIZE_T user = in->UserName ? wcslen(in->UserName) : 0;
    if (comment > CRED_MAX_STRING_LENGTH || alias > CRED_MAX_STRING_LENGTH || user > CRED_MAX_USERNAME_LENGTH) {
        free_target(&target);
        return STATUS_INVALID_PARAMETER__;
    }
    BOOL needs_user = in->Type == CRED_TYPE_DOMAIN_PASSWORD || in->Type == CRED_TYPE_DOMAIN_CERTIFICATE ||
                      in->Type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD;
    if (needs_user) {
        if (flags & CRED_FLAGS_PASSWORD_FOR_CERT) {
            if (user) {
                free_target(&target);
                return STATUS_INVALID_ACCOUNT_NAME__;
            }
        } else {
            status = validate_user_name(user ? in->UserName : nullptr, in->Type);
            if (status < 0) {
                free_target(&target);
                return status;
            }
        }
    }
    DWORD blob_multiple = in->Type == CRED_TYPE_GENERIC || in->Type == CRED_TYPE_GENERIC_CERTIFICATE ? 1 : 2;
    if (in->CredentialBlob) {
        if (!in->CredentialBlobSize || in->CredentialBlobSize > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
            in->CredentialBlobSize % blob_multiple) {
            free_target(&target);
            return STATUS_INVALID_PARAMETER__;
        }
    } else if (in->CredentialBlobSize) {
        free_target(&target);
        return STATUS_INVALID_PARAMETER__;
    }
    if (in->AttributeCount) {
        if (!in->Attributes || in->AttributeCount > CRED_MAX_ATTRIBUTES) {
            free_target(&target);
            return STATUS_INVALID_PARAMETER__;
        }
        for (DWORD i = 0; i < in->AttributeCount; i++) {
            const CREDENTIAL_ATTRIBUTEW_ *a = &in->Attributes[i];
            SIZE_T k = a->Keyword ? wcslen(a->Keyword) : 0;
            if (!k || k > CRED_MAX_STRING_LENGTH || (a->Value ? !a->ValueSize || a->ValueSize > CRED_MAX_VALUE_SIZE : a->ValueSize)) {
                free_target(&target);
                return STATUS_INVALID_PARAMETER__;
            }
        }
    }

    CM_CREDENTIAL *c = AdvAllocZero(sizeof(*c));
    if (!c) {
        free_target(&target);
        return STATUS_NO_MEMORY_;
    }
    c->Flags = flags;
    c->Type = type;
    c->Visible = in->Type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD;
    c->Persist = in->Persist;
    c->AttributeType = target.Attribute;
    c->Namespace = target.Namespace;
    c->Name = target.Name;
    c->Comment = comment ? copy_string(in->Comment) : nullptr;
    c->Alias = alias ? copy_string(in->TargetAlias) : nullptr;
    c->User = user ? copy_string(in->UserName) : nullptr;
    c->Blob = copy_bytes(in->CredentialBlob, in->CredentialBlobSize);
    c->BlobSize = c->Blob ? in->CredentialBlobSize : 0;
    BOOL ok = (!comment || c->Comment) && (!alias || c->Alias) && (!user || c->User) && (!in->CredentialBlob || c->Blob);
    if (in->AttributeCount) {
        c->Attributes = AdvAllocZero(sizeof(CM_ATTRIBUTE) * in->AttributeCount);
        ok = ok && c->Attributes;
        for (DWORD i = 0; ok && i < in->AttributeCount; i++) {
            c->Attributes[i].Keyword = copy_string(in->Attributes[i].Keyword);
            c->Attributes[i].Flags = in->Attributes[i].Flags;
            c->Attributes[i].ValueSize = in->Attributes[i].ValueSize;
            c->Attributes[i].Value = copy_bytes(in->Attributes[i].Value, in->Attributes[i].ValueSize);
            c->AttributeCount = i + 1;
            ok = c->Attributes[i].Keyword && (!in->Attributes[i].Value || c->Attributes[i].Value);
        }
    }
    if (!ok) {
        free_credential(c);
        return STATUS_NO_MEMORY_;
    }
    *result = c;
    return 0;
}

/* ------------------------------------------------------------------ marshaling, the layout of the stored credential */

typedef struct WRITER {
    BYTE *Data;
    DWORD Size;
    DWORD Capacity;
    BOOL Failed;
} WRITER;

static void put(WRITER *w, const void *data, DWORD size)
{
    if (w->Failed)
        return;
    if (w->Size + size > w->Capacity) {
        DWORD capacity = w->Capacity ? w->Capacity * 2 : 512;
        while (capacity < w->Size + size)
            capacity *= 2;
        BYTE *grown = AdvAlloc(capacity);
        if (!grown) {
            w->Failed = TRUE;
            return;
        }
        if (w->Data) {
            memcpy(grown, w->Data, w->Size);
            memset(w->Data, 0, w->Size);
            AdvFree(w->Data);
        }
        w->Data = grown;
        w->Capacity = capacity;
    }
    if (size)
        memcpy(w->Data + w->Size, data, size);
    w->Size += size;
}

static void put_dword(WRITER *w, DWORD value)
{
    put(w, &value, sizeof(value));
}

static void align_even(WRITER *w)
{
    if (w->Size & 1)
        put(w, "", 1);
}

/* a length and the bytes behind it, started at an even offset. */
static void put_block(WRITER *w, const void *data, DWORD size)
{
    align_even(w);
    put_dword(w, size);
    put(w, data, size);
}

static void put_string_block(WRITER *w, PCWSTR s)
{
    put_block(w, s, s ? (DWORD)((wcslen(s) + 1) * sizeof(WCHAR)) : 0);
}

/* the canonical form of the target: namespace:attribute=name. */
static PWSTR canonical_name(const CM_CREDENTIAL *c)
{
    SIZE_T chars = wcslen(c->Namespace) + 1 + wcslen(g_attribute_names[c->AttributeType]) + 1 + wcslen(c->Name) + 1;
    PWSTR s = AdvAlloc(chars * sizeof(WCHAR));
    if (s)
        _snwprintf(s, chars, L"%ls:%ls=%ls", c->Namespace, g_attribute_names[c->AttributeType], c->Name);
    return s;
}

/* the 48 byte header, then the strings, the secret and the attributes. a certificate credential keeps no secret. */
static BOOL marshal_credential(const CM_CREDENTIAL *c, BOOL keep_secret, BYTE **data, DWORD *size)
{
    WRITER w = {};
    PWSTR canonical = canonical_name(c);
    if (!canonical)
        return FALSE;
    DWORD padded = (c->BlobSize + 7) & ~7u;
    put_dword(&w, 48);
    put_dword(&w, 0);
    put_dword(&w, c->Flags);
    put_dword(&w, c->Type);
    put_dword(&w, c->Visible);
    put(&w, &c->LastWritten, sizeof(c->LastWritten));
    put_dword(&w, padded);
    put_dword(&w, c->Persist);
    put_dword(&w, c->AttributeCount);
    put_dword(&w, 0);
    put_dword(&w, 0);
    put_string_block(&w, canonical);
    put_block(&w, nullptr, 0);
    put_string_block(&w, c->Comment);
    put_string_block(&w, c->Alias);
    put_string_block(&w, c->User);
    if (c->Type != CRED_TYPE_DOMAIN_CERTIFICATE || keep_secret)
        put_dword(&w, c->BlobSize), put(&w, c->Blob, c->BlobSize);
    else
        put_dword(&w, 0);
    for (DWORD i = 0; i < c->AttributeCount; i++) {
        put_dword(&w, c->Attributes[i].Flags);
        put_string_block(&w, c->Attributes[i].Keyword);
        put_dword(&w, c->Attributes[i].ValueSize);
        put(&w, c->Attributes[i].Value, c->Attributes[i].ValueSize);
    }
    AdvFree(canonical);
    if (w.Failed) {
        AdvFree(w.Data);
        return FALSE;
    }
    memcpy(w.Data + 4, &w.Size, sizeof(DWORD));
    *data = w.Data;
    *size = w.Size;
    return TRUE;
}

typedef struct READER {
    const BYTE *Data;
    DWORD Size;
    DWORD Offset;
    BOOL Failed;
} READER;

static DWORD get_dword(READER *r)
{
    DWORD v = 0;
    if (r->Offset + 4 > r->Size) {
        r->Failed = TRUE;
        return 0;
    }
    memcpy(&v, r->Data + r->Offset, 4);
    r->Offset += 4;
    return v;
}

static const BYTE *get_block(READER *r, DWORD *size)
{
    if (r->Offset & 1)
        r->Offset++;
    *size = get_dword(r);
    if (r->Failed || r->Offset + *size > r->Size) {
        r->Failed = TRUE;
        *size = 0;
        return nullptr;
    }
    const BYTE *p = r->Data + r->Offset;
    r->Offset += *size;
    return *size ? p : nullptr;
}

/* a string block with its terminator, copied. */
static PWSTR get_string(READER *r)
{
    DWORD size;
    const BYTE *p = get_block(r, &size);
    if (!p)
        return nullptr;
    if (size < sizeof(WCHAR) || size % sizeof(WCHAR) || ((const WCHAR *)p)[size / sizeof(WCHAR) - 1]) {
        r->Failed = TRUE;
        return nullptr;
    }
    return copy_string((const WCHAR *)p);
}

static CM_CREDENTIAL *unmarshal_credential(const BYTE *data, DWORD size)
{
    READER r = {data, size, 0, FALSE};
    if (size < 48 || get_dword(&r) != 48 || get_dword(&r) != size)
        return nullptr;
    CM_CREDENTIAL *c = AdvAllocZero(sizeof(*c));
    if (!c)
        return nullptr;
    c->Flags = get_dword(&r);
    c->Type = get_dword(&r);
    c->Visible = get_dword(&r);
    memcpy(&c->LastWritten, data + r.Offset, sizeof(FILETIME));
    r.Offset += sizeof(FILETIME);
    get_dword(&r);
    c->Persist = get_dword(&r);
    c->AttributeCount = get_dword(&r);
    r.Offset = 48;
    PWSTR canonical = get_string(&r);
    DWORD skip;
    get_block(&r, &skip);
    c->Comment = get_string(&r);
    c->Alias = get_string(&r);
    c->User = get_string(&r);
    if (!r.Failed && canonical) {
        PWSTR colon = wcschr(canonical, L':'), equals = colon ? wcschr(colon + 1, L'=') : nullptr;
        DWORD attribute;
        if (equals && attribute_from_name(colon + 1, equals - colon - 1, &attribute)) {
            c->AttributeType = attribute;
            *colon = 0;
            c->Namespace = copy_string(canonical);
            c->Name = copy_string(equals + 1);
        } else {
            r.Failed = TRUE;
        }
    } else {
        r.Failed = TRUE;
    }
    AdvFree(canonical);
    DWORD blob = get_dword(&r);
    if (blob) {
        if (r.Offset + blob > r.Size || blob > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            r.Failed = TRUE;
        else
            c->Blob = copy_bytes(data + r.Offset, blob);
        c->BlobSize = c->Blob ? blob : 0;
        r.Offset += blob;
    }
    if (!r.Failed && c->AttributeCount > CRED_MAX_ATTRIBUTES)
        r.Failed = TRUE;
    DWORD declared = c->AttributeCount;
    c->AttributeCount = 0;
    if (!r.Failed && declared) {
        c->Attributes = AdvAllocZero(sizeof(CM_ATTRIBUTE) * declared);
        if (!c->Attributes)
            r.Failed = TRUE;
    }
    for (DWORD i = 0; !r.Failed && i < declared; i++) {
        c->Attributes[i].Flags = get_dword(&r);
        c->Attributes[i].Keyword = get_string(&r);
        DWORD value = get_dword(&r);
        if (r.Failed || r.Offset + value > r.Size || value > CRED_MAX_VALUE_SIZE) {
            r.Failed = TRUE;
            break;
        }
        c->Attributes[i].ValueSize = value;
        c->Attributes[i].Value = copy_bytes(data + r.Offset, value);
        r.Offset += value;
        c->AttributeCount = i + 1;
        if (!c->Attributes[i].Keyword)
            r.Failed = TRUE;
    }
    if (r.Failed || !c->Namespace || !c->Name || c->Persist < 1 || c->Persist > 3 || r.Offset != size) {
        free_credential(c);
        return nullptr;
    }
    return c;
}

/* ------------------------------------------------------------------ where the credentials are kept */

static HMODULE g_crypt32;

typedef struct DATA_BLOB_ {
    DWORD cbData;
    BYTE *pbData;
} DATA_BLOB_;

typedef BOOL(WINAPI *PROTECT_DATA)(DATA_BLOB_ *, PCWSTR, DATA_BLOB_ *, PVOID, PVOID, DWORD, DATA_BLOB_ *);
typedef BOOL(WINAPI *UNPROTECT_DATA)(DATA_BLOB_ *, PWSTR *, DATA_BLOB_ *, PVOID, PVOID, DWORD, DATA_BLOB_ *);

static BOOL dpapi_load(PROTECT_DATA *protect, UNPROTECT_DATA *unprotect)
{
    HMODULE module = g_crypt32 ? g_crypt32 : LoadLibraryW(L"crypt32.dll");
    if (!module)
        return FALSE;
    g_crypt32 = module;
    if (protect)
        *protect = (PROTECT_DATA)(void *)GetProcAddress(module, "CryptProtectData");
    if (unprotect)
        *unprotect = (UNPROTECT_DATA)(void *)GetProcAddress(module, "CryptUnprotectData");
    return (!protect || *protect) && (!unprotect || *unprotect);
}

static BYTE *dpapi_protect(const BYTE *data, DWORD size, PCWSTR description, DWORD *out_size)
{
    PROTECT_DATA protect;
    if (!dpapi_load(&protect, nullptr))
        return nullptr;
    DATA_BLOB_ in = {size, (BYTE *)data}, out = {};
    if (!protect(&in, description, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return nullptr;
    BYTE *copy = copy_bytes(out.pbData, out.cbData);
    LocalFree(out.pbData);
    *out_size = copy ? out.cbData : 0;
    return copy;
}

static BYTE *dpapi_unprotect(const BYTE *data, DWORD size, DWORD *out_size)
{
    UNPROTECT_DATA unprotect;
    if (!dpapi_load(nullptr, &unprotect))
        return nullptr;
    DATA_BLOB_ in = {size, (BYTE *)data}, out = {};
    if (!unprotect(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return nullptr;
    BYTE *copy = copy_bytes(out.pbData, out.cbData);
    memset(out.pbData, 0, out.cbData);
    LocalFree(out.pbData);
    *out_size = copy ? out.cbData : 0;
    return copy;
}

/* the file of a credential: the MD5 of namespace, attribute, name and type, as 32 hexadecimal digits. */
static void credential_file_name(const CM_CREDENTIAL *c, WCHAR name[33])
{
    BYTE hash[16];
    MimeHashCtx ctx;
    mime_md5.init(&ctx);
    mime_md5.update(&ctx, c->Namespace, (DWORD)(wcslen(c->Namespace) * sizeof(WCHAR)));
    mime_md5.update(&ctx, &c->AttributeType, sizeof(DWORD));
    mime_md5.update(&ctx, c->Name, (DWORD)(wcslen(c->Name) * sizeof(WCHAR)));
    mime_md5.update(&ctx, &c->Type, sizeof(DWORD));
    mime_md5.final(&ctx, hash);
    static const WCHAR digits[] = L"0123456789ABCDEF";
    for (int i = 0; i < 16; i++) {
        name[2 * i] = digits[hash[i] >> 4];
        name[2 * i + 1] = digits[hash[i] & 15];
    }
    name[32] = 0;
}

/* the folder of the credentials of a persistence, created when it is missing. */
static BOOL credentials_folder(DWORD persist, WCHAR path[MAX_PATH], BOOL create)
{
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(persist == CRED_PERSIST_ENTERPRISE ? L"APPDATA" : L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        n = GetEnvironmentVariableW(L"USERPROFILE", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return FALSE;
        wcscat(base, persist == CRED_PERSIST_ENTERPRISE ? L"\\AppData\\Roaming" : L"\\AppData\\Local");
    }
    _snwprintf(path, MAX_PATH, L"%ls\\Microsoft", base);
    if (create)
        CreateDirectoryW(path, nullptr);
    _snwprintf(path, MAX_PATH, L"%ls\\Microsoft\\Credentials", base);
    if (create) {
        CreateDirectoryW(path, nullptr);
        SetFileAttributesW(path, FILE_ATTRIBUTE_HIDDEN);
    }
    return TRUE;
}

static PCWSTR const g_data_text[] = {nullptr, nullptr, L"Local Credential Data", L"Enterprise Credential Data"};

static DWORD file_write(const CM_CREDENTIAL *c)
{
    WCHAR folder[MAX_PATH], name[33], path[MAX_PATH];
    if (!credentials_folder(c->Persist, folder, TRUE))
        return ERROR_NO_SUCH_LOGON_SESSION;
    BYTE *plain;
    DWORD plain_size;
    if (!marshal_credential(c, FALSE, &plain, &plain_size))
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD blob_size = 0;
    BYTE *blob = dpapi_protect(plain, plain_size, g_data_text[c->Persist], &blob_size);
    memset(plain, 0, plain_size);
    AdvFree(plain);
    if (!blob)
        return GetLastError() ? GetLastError() : ERROR_ENCRYPTION_FAILED;
    credential_file_name(c, name);
    _snwprintf(path, MAX_PATH, L"%ls\\%ls", folder, name);
    DWORD error = ERROR_SUCCESS;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
    } else {
        DWORD header[3] = {1, blob_size, 0}, written;
        if (!WriteFile(file, header, sizeof(header), &written, nullptr) || !WriteFile(file, blob, blob_size, &written, nullptr))
            error = GetLastError();
        CloseHandle(file);
        if (error)
            DeleteFileW(path);
    }
    AdvFree(blob);
    return error;
}

static void file_delete(const CM_CREDENTIAL *c)
{
    WCHAR folder[MAX_PATH], name[33], path[MAX_PATH];
    if (!credentials_folder(c->Persist, folder, FALSE))
        return;
    credential_file_name(c, name);
    _snwprintf(path, MAX_PATH, L"%ls\\%ls", folder, name);
    DeleteFileW(path);
}

static CM_CREDENTIAL *file_read(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    CM_CREDENTIAL *c = nullptr;
    if (size >= 12 && size < 0x100000) {
        BYTE *data = AdvAlloc(size);
        if (data && ReadFile(file, data, size, &read, nullptr) && read == size) {
            DWORD version, blob;
            memcpy(&version, data, 4);
            memcpy(&blob, data + 4, 4);
            if (version == 1 && blob == size - 12) {
                DWORD plain_size = 0;
                BYTE *plain = dpapi_unprotect(data + 12, blob, &plain_size);
                if (plain) {
                    c = unmarshal_credential(plain, plain_size);
                    memset(plain, 0, plain_size);
                    AdvFree(plain);
                }
            }
        }
        AdvFree(data);
    }
    CloseHandle(file);
    return c;
}

/* the credentials of the logon session are values of a volatile key, encrypted for the logon session. */
static HKEY session_key(BOOL create)
{
    HKEY key = nullptr;
    DWORD disposition;
    if (create)
        RegCreateKeyExW(HKEY_CURRENT_USER, L"Volatile Environment\\Credentials", 0, nullptr, REG_OPTION_VOLATILE,
                        KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
    else
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Volatile Environment\\Credentials", 0, KEY_READ | KEY_WRITE, &key);
    return key;
}

static DWORD session_write(const CM_CREDENTIAL *c)
{
    BYTE *plain;
    DWORD plain_size;
    if (!marshal_credential(c, TRUE, &plain, &plain_size))
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD padded = (plain_size + 11) & ~7u;
    BYTE *sealed = AdvAllocZero(padded);
    if (!sealed) {
        memset(plain, 0, plain_size);
        AdvFree(plain);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(sealed, plain, plain_size);
    memcpy(sealed + padded - 4, &plain_size, 4);
    memset(plain, 0, plain_size);
    AdvFree(plain);
    DWORD error = NT_SUCCESS(SystemFunction040(sealed, padded, 2)) ? ERROR_SUCCESS : ERROR_ENCRYPTION_FAILED;
    HKEY key = error ? nullptr : session_key(TRUE);
    if (!error && !key)
        error = ERROR_NO_SUCH_LOGON_SESSION;
    if (!error) {
        WCHAR name[33];
        credential_file_name(c, name);
        error = RegSetValueExW(key, name, 0, REG_BINARY, sealed, padded);
    }
    if (key)
        RegCloseKey(key);
    memset(sealed, 0, padded);
    AdvFree(sealed);
    return error;
}

static void session_delete(const CM_CREDENTIAL *c)
{
    HKEY key = session_key(FALSE);
    if (!key)
        return;
    WCHAR name[33];
    credential_file_name(c, name);
    RegDeleteValueW(key, name);
    RegCloseKey(key);
}

static CM_CREDENTIAL *session_read(HKEY key, PCWSTR name)
{
    DWORD size = 0, type;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) || type != REG_BINARY || size < 16 || size % 8)
        return nullptr;
    BYTE *data = AdvAlloc(size);
    CM_CREDENTIAL *c = nullptr;
    if (data && !RegQueryValueExW(key, name, nullptr, &type, data, &size) && NT_SUCCESS(SystemFunction041(data, size, 2))) {
        DWORD plain;
        memcpy(&plain, data + size - 4, 4);
        if (plain && plain <= size - 4)
            c = unmarshal_credential(data, plain);
    }
    if (data) {
        memset(data, 0, size);
        AdvFree(data);
    }
    return c;
}

/* ------------------------------------------------------------------ the store, serialized between the processes of the user */

typedef struct STORE {
    CM_CREDENTIAL **Items;
    DWORD Count;
    DWORD Capacity;
    HANDLE Lock;
} STORE;

static BOOL store_add(STORE *s, CM_CREDENTIAL *c)
{
    if (s->Count == s->Capacity) {
        DWORD capacity = s->Capacity ? s->Capacity * 2 : 16;
        CM_CREDENTIAL **grown = AdvAlloc(capacity * sizeof(*grown));
        if (!grown)
            return FALSE;
        if (s->Items)
            memcpy(grown, s->Items, s->Count * sizeof(*grown));
        AdvFree(s->Items);
        s->Items = grown;
        s->Capacity = capacity;
    }
    s->Items[s->Count++] = c;
    return TRUE;
}

static void store_remove(STORE *s, DWORD index)
{
    free_credential(s->Items[index]);
    memmove(&s->Items[index], &s->Items[index + 1], (s->Count - index - 1) * sizeof(*s->Items));
    s->Count--;
}

static void store_close(STORE *s)
{
    for (DWORD i = 0; i < s->Count; i++)
        free_credential(s->Items[i]);
    AdvFree(s->Items);
    if (s->Lock) {
        ReleaseMutex(s->Lock);
        CloseHandle(s->Lock);
    }
}

/* orders credentials by the time they were written, oldest first. */
static void sort_written(CM_CREDENTIAL **items, DWORD count)
{
    for (DWORD i = 1; i < count; i++) {
        CM_CREDENTIAL *item = items[i];
        DWORD k = i;
        while (k > 0 && CompareFileTime(&items[k - 1]->LastWritten, &item->LastWritten) > 0) {
            items[k] = items[k - 1];
            k--;
        }
        items[k] = item;
    }
}

/*
 * takes the user's lock and reads the three sets: the session's credentials first, then the machine's and the
 * enterprise's, each oldest first. the lock is named for the user so that other users of the session do not wait.
 */
static int store_open(STORE *s)
{
    memset(s, 0, sizeof(*s));
    WCHAR name[96] = L"Local\\CredMan.";
    BYTE storage[SECURITY_MAX_SID_SIZE];
    PWSTR text = nullptr;
    if (AdvCurrentUserSid(storage) && ConvertSidToStringSidW(storage, &text)) {
        SIZE_T used = wcslen(name);
        _snwprintf(name + used, RTL_NUMBER_OF(name) - used, L"%.60ls", text);
        LocalFree(text);
    }
    s->Lock = CreateMutexW(nullptr, FALSE, name);
    if (!s->Lock)
        return STATUS_NO_MEMORY_;
    WaitForSingleObject(s->Lock, INFINITE);

    HKEY key = session_key(FALSE);
    if (key) {
        WCHAR value[40];
        for (DWORD i = 0;; i++) {
            DWORD chars = RTL_NUMBER_OF(value);
            if (RegEnumValueW(key, i, value, &chars, nullptr, nullptr, nullptr, nullptr))
                break;
            CM_CREDENTIAL *c = session_read(key, value);
            if (c && (c->Persist != CRED_PERSIST_SESSION || !store_add(s, c)))
                free_credential(c);
        }
        RegCloseKey(key);
        sort_written(s->Items, s->Count);
    }
    for (DWORD persist = CRED_PERSIST_LOCAL_MACHINE; persist <= CRED_PERSIST_ENTERPRISE; persist++) {
        WCHAR folder[MAX_PATH], pattern[MAX_PATH];
        if (!credentials_folder(persist, folder, FALSE))
            continue;
        _snwprintf(pattern, MAX_PATH, L"%ls\\*", folder);
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileW(pattern, &found);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        DWORD first = s->Count;
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            WCHAR path[MAX_PATH];
            _snwprintf(path, MAX_PATH, L"%ls\\%ls", folder, found.cFileName);
            CM_CREDENTIAL *c = file_read(path);
            if (c && (c->Persist != persist || !store_add(s, c)))
                free_credential(c);
        } while (FindNextFileW(find, &found));
        FindClose(find);
        sort_written(s->Items + first, s->Count - first);
    }
    return 0;
}

static BOOL same_target(const CM_CREDENTIAL *c, DWORD type, const TARGET_NAME *t)
{
    return c->Type == type && c->AttributeType == t->Attribute && equal_ci(c->Namespace, t->Namespace) && equal_ci(c->Name, t->Name);
}

static int store_find(STORE *s, DWORD type, const TARGET_NAME *t)
{
    for (DWORD i = 0; i < s->Count; i++)
        if (same_target(s->Items[i], type, t))
            return (int)i;
    return -1;
}

static DWORD stored_write(const CM_CREDENTIAL *c)
{
    return c->Persist == CRED_PERSIST_SESSION ? session_write(c) : file_write(c);
}

static void stored_delete(const CM_CREDENTIAL *c)
{
    if (c->Persist == CRED_PERSIST_SESSION)
        session_delete(c);
    else
        file_delete(c);
}

/* ------------------------------------------------------------------ the machine's settings */

/* true when the policy that stops the storage of domain credentials is set. */
static BOOL domain_credentials_disabled(void)
{
    DWORD value = 0, size = sizeof(value);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", L"DisableDomainCreds", RRF_RT_REG_DWORD,
                      nullptr, &value, &size))
        return value != 0;
    return FALSE;
}

static BOOL credential_manager_disabled(void)
{
    DWORD value = 0, size = sizeof(value);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", L"DisableCredMan", RRF_RT_REG_DWORD,
                      nullptr, &value, &size))
        return value != 0;
    return FALSE;
}

/* ------------------------------------------------------------------ results for the caller */

/* the sizes and the placing of a credential in one LocalAlloc block, with its strings behind it. */
static SIZE_T string_bytes(PCWSTR s)
{
    return s ? (wcslen(s) + 1) * sizeof(WCHAR) : 0;
}

static SIZE_T result_size(const CM_CREDENTIAL *c, BOOL strip_namespace, BOOL strip_secret)
{
    SIZE_T size = (sizeof(CREDENTIALW_) + 7) & ~7ull;
    PWSTR canonical = strip_namespace ? nullptr : canonical_name(c);
    size += (strip_namespace ? string_bytes(c->Name) : string_bytes(canonical)) + string_bytes(c->Comment) +
            string_bytes(c->Alias) + string_bytes(c->User);
    AdvFree(canonical);
    size = (size + 7) & ~7ull;
    if (!strip_secret)
        size += (c->BlobSize + 7) & ~7ull;
    size += c->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
    for (DWORD i = 0; i < c->AttributeCount; i++)
        size += string_bytes(c->Attributes[i].Keyword) + ((c->Attributes[i].ValueSize + 7) & ~7ull) + 8;
    return size + 16;
}

static BYTE *put_string_at(BYTE **at, PCWSTR s)
{
    if (!s)
        return nullptr;
    SIZE_T bytes = string_bytes(s);
    BYTE *p = *at;
    memcpy(p, s, bytes);
    *at += bytes;
    return p;
}

/* builds the record the caller gets; the secret of a domain credential is not given out. */
static CREDENTIALW_ *build_result(const CM_CREDENTIAL *c, BYTE *memory, BOOL strip_namespace, BOOL strip_secret)
{
    CREDENTIALW_ *r = (CREDENTIALW_ *)memory;
    BYTE *at = memory + ((sizeof(CREDENTIALW_) + 7) & ~7ull);
    memset(r, 0, sizeof(*r));
    r->Flags = c->Flags;
    r->Type = c->Visible ? CRED_TYPE_DOMAIN_VISIBLE_PASSWORD : c->Type;
    if (c->AttributeType == ATTRIBUTE_NAME && !(r->Flags & CRED_FLAGS_USERNAME_TARGET))
        r->Flags |= CRED_FLAGS_USERNAME_TARGET;
    PWSTR canonical = strip_namespace || c->Visible ? nullptr : canonical_name(c);
    r->TargetName = (PWSTR)put_string_at(&at, strip_namespace || c->Visible ? c->Name : canonical);
    AdvFree(canonical);
    r->Comment = (PWSTR)put_string_at(&at, c->Comment);
    r->TargetAlias = (PWSTR)put_string_at(&at, c->Alias);
    r->UserName = (PWSTR)put_string_at(&at, c->User);
    r->LastWritten = c->LastWritten;
    r->Persist = c->Persist;
    at = (BYTE *)(((ULONG_PTR)at + 7) & ~7ull);
    if (strip_secret) {
        r->Flags &= ~0x40u;
    } else if (c->Blob) {
        r->CredentialBlob = at;
        r->CredentialBlobSize = c->BlobSize;
        memcpy(at, c->Blob, c->BlobSize);
        at += (c->BlobSize + 7) & ~7ull;
    }
    if (c->AttributeCount) {
        r->Attributes = (CREDENTIAL_ATTRIBUTEW_ *)at;
        r->AttributeCount = c->AttributeCount;
        at += c->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
        for (DWORD i = 0; i < c->AttributeCount; i++) {
            r->Attributes[i].Flags = c->Attributes[i].Flags;
            r->Attributes[i].Keyword = (PWSTR)put_string_at(&at, c->Attributes[i].Keyword);
            r->Attributes[i].ValueSize = c->Attributes[i].ValueSize;
            if (c->Attributes[i].ValueSize) {
                r->Attributes[i].Value = at;
                memcpy(at, c->Attributes[i].Value, c->Attributes[i].ValueSize);
                at += (c->Attributes[i].ValueSize + 7) & ~7ull;
            }
        }
    }
    return r;
}

static BOOL secret_withheld(const CM_CREDENTIAL *c)
{
    return c->Type == CRED_TYPE_DOMAIN_PASSWORD || c->Type == CRED_TYPE_DOMAIN_CERTIFICATE || c->Visible;
}

static CREDENTIALW_ *result_one(const CM_CREDENTIAL *c, BOOL strip_namespace, BOOL give_secret)
{
    BOOL strip = secret_withheld(c) && !give_secret;
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, result_size(c, strip_namespace, strip));
    return memory ? build_result(c, memory, strip_namespace, strip) : nullptr;
}

/* a list of credentials in one block: the pointers, then each record. */
static CREDENTIALW_ **result_list(CM_CREDENTIAL *const *items, const DWORD *indexes, DWORD count, BOOL strip_namespace,
                                  BOOL give_secret)
{
    SIZE_T total = ((count * sizeof(PVOID)) + 7) & ~7ull;
    for (DWORD i = 0; i < count; i++) {
        const CM_CREDENTIAL *c = items[indexes[i]];
        total += result_size(c, strip_namespace, secret_withheld(c) && !give_secret);
    }
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, total);
    if (!memory)
        return nullptr;
    CREDENTIALW_ **list = (CREDENTIALW_ **)memory;
    BYTE *at = memory + (((count * sizeof(PVOID)) + 7) & ~7ull);
    for (DWORD i = 0; i < count; i++) {
        const CM_CREDENTIAL *c = items[indexes[i]];
        BOOL strip = secret_withheld(c) && !give_secret;
        list[i] = build_result(c, at, strip_namespace, strip);
        at += (result_size(c, strip_namespace, strip) + 7) & ~7ull;
    }
    return list;
}

/* ------------------------------------------------------------------ writing, reading and deleting */

static int store_write(STORE *s, const CREDENTIALW_ *in, DWORD flags)
{
    if (flags & ~CRED_PRESERVE_CREDENTIAL_BLOB)
        return STATUS_INVALID_PARAMETER_2_;
    CM_CREDENTIAL *c;
    int status = make_credential(in, &c);
    if (status < 0)
        return status;
    if ((in->Type == CRED_TYPE_DOMAIN_PASSWORD || in->Type == CRED_TYPE_DOMAIN_CERTIFICATE) && domain_credentials_disabled()) {
        free_credential(c);
        return STATUS_NO_SUCH_LOGON_SESSION_;
    }
    TARGET_NAME t = {c->Namespace, c->Name, c->AttributeType, FALSE, FALSE};
    int at = store_find(s, c->Type, &t);
    if (flags & CRED_PRESERVE_CREDENTIAL_BLOB) {
        if (c->BlobSize) {
            free_credential(c);
            return STATUS_INVALID_PARAMETER__;
        }
        if (at < 0) {
            free_credential(c);
            return STATUS_NOT_FOUND_;
        }
        c->Blob = copy_bytes(s->Items[at]->Blob, s->Items[at]->BlobSize);
        c->BlobSize = c->Blob ? s->Items[at]->BlobSize : 0;
    }
    GetSystemTimeAsFileTime(&c->LastWritten);
    DWORD error = stored_write(c);
    if (error) {
        free_credential(c);
        return error == ERROR_NO_SUCH_LOGON_SESSION ? STATUS_NO_SUCH_LOGON_SESSION_ : (int)(0xC0010000 | (error & 0xFFFF));
    }
    if (at >= 0) {
        CM_CREDENTIAL *old = s->Items[at];
        WCHAR a[33], b[33];
        credential_file_name(old, a);
        credential_file_name(c, b);
        if (old->Persist != c->Persist || wcscmp(a, b))
            stored_delete(old);
        store_remove(s, at);
    }
    if (!store_add(s, c)) {
        free_credential(c);
        return STATUS_NO_MEMORY_;
    }
    return 0;
}

static DWORD store_load_error(int status)
{
    if ((status & 0xFFFF0000) == 0xC0010000)
        return status & 0xFFFF;
    return status_error(status);
}

/* looks a credential up by the name it was written under. */
static int store_read(STORE *s, PCWSTR target, DWORD type, DWORD flags, BOOL give_secret, CREDENTIALW_ **result)
{
    *result = nullptr;
    if (flags & 0xFFFF0F60)
        return STATUS_INVALID_PARAMETER_3_;
    TARGET_NAME t;
    int status = parse_target(target, type, FALSE, &t);
    if (status < 0)
        return status;
    DWORD stored = type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD ? CRED_TYPE_GENERIC : type;
    int at = store_find(s, stored, &t);
    free_target(&t);
    if (at < 0)
        return STATUS_NOT_FOUND_;
    *result = result_one(s->Items[at], FALSE, give_secret);
    return *result ? 0 : STATUS_NO_MEMORY_;
}

/* ------------------------------------------------------------------ the interface of credential records in both character sets */

static PWSTR wide_of(LPCSTR s)
{
    return AdvAnsiToWide(s);
}

static SIZE_T ansi_bytes(PCWSTR s)
{
    if (!s)
        return 0;
    return WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
}

typedef struct ANSI_CURSOR {
    BYTE *At;
} ANSI_CURSOR;

static PSTR put_ansi(BYTE **at, PCWSTR s)
{
    if (!s)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, (PSTR)*at, 0x10000, nullptr, nullptr);
    PSTR p = (PSTR)*at;
    *at += n;
    return p;
}

/* the same records as the ansi ones, the strings converted with the ansi code page. */
static SIZE_T ansi_size_of(const CREDENTIALW_ *w)
{
    SIZE_T size = (sizeof(CREDENTIALW_) + 7) & ~7ull;
    size += ansi_bytes(w->TargetName) + ansi_bytes(w->Comment) + ansi_bytes(w->TargetAlias) + ansi_bytes(w->UserName);
    size = (size + 7) & ~7ull;
    size += (w->CredentialBlobSize + 7) & ~7ull;
    size += w->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
    for (DWORD i = 0; i < w->AttributeCount; i++)
        size += ansi_bytes(w->Attributes[i].Keyword) + ((w->Attributes[i].ValueSize + 7) & ~7ull);
    return size + 16;
}

static CREDENTIALW_ *ansi_of(const CREDENTIALW_ *w, BYTE *memory)
{
    CREDENTIALW_ *a = (CREDENTIALW_ *)memory;
    BYTE *at = memory + ((sizeof(CREDENTIALW_) + 7) & ~7ull);
    *a = *w;
    a->TargetName = (PWSTR)put_ansi(&at, w->TargetName);
    a->Comment = (PWSTR)put_ansi(&at, w->Comment);
    a->TargetAlias = (PWSTR)put_ansi(&at, w->TargetAlias);
    a->UserName = (PWSTR)put_ansi(&at, w->UserName);
    at = (BYTE *)(((ULONG_PTR)at + 7) & ~7ull);
    a->CredentialBlob = nullptr;
    if (w->CredentialBlobSize) {
        a->CredentialBlob = at;
        memcpy(at, w->CredentialBlob, w->CredentialBlobSize);
        at += (w->CredentialBlobSize + 7) & ~7ull;
    }
    a->Attributes = nullptr;
    if (w->AttributeCount) {
        a->Attributes = (CREDENTIAL_ATTRIBUTEW_ *)at;
        at += w->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
        for (DWORD i = 0; i < w->AttributeCount; i++) {
            a->Attributes[i] = w->Attributes[i];
            a->Attributes[i].Keyword = (PWSTR)put_ansi(&at, w->Attributes[i].Keyword);
            a->Attributes[i].Value = nullptr;
            if (w->Attributes[i].ValueSize) {
                a->Attributes[i].Value = at;
                memcpy(at, w->Attributes[i].Value, w->Attributes[i].ValueSize);
                at += (w->Attributes[i].ValueSize + 7) & ~7ull;
            }
        }
    }
    return a;
}

static CREDENTIALW_ *to_ansi_one(CREDENTIALW_ *w)
{
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, ansi_size_of(w));
    CREDENTIALW_ *a = memory ? ansi_of(w, memory) : nullptr;
    LocalFree(w);
    return a;
}

static CREDENTIALW_ **to_ansi_list(CREDENTIALW_ **list, DWORD count)
{
    SIZE_T total = ((count * sizeof(PVOID)) + 7) & ~7ull;
    for (DWORD i = 0; i < count; i++)
        total += (ansi_size_of(list[i]) + 7) & ~7ull;
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, total);
    if (memory) {
        CREDENTIALW_ **out = (CREDENTIALW_ **)memory;
        BYTE *at = memory + (((count * sizeof(PVOID)) + 7) & ~7ull);
        for (DWORD i = 0; i < count; i++) {
            out[i] = ansi_of(list[i], at);
            at += (ansi_size_of(list[i]) + 7) & ~7ull;
        }
    }
    LocalFree(list);
    return (CREDENTIALW_ **)memory;
}

/* a record from the caller's ansi record: strings converted, the rest as it is. the strings live in one block. */
static CREDENTIALW_ *from_ansi(const CREDENTIALW_ *a, BYTE **block)
{
    *block = nullptr;
    SIZE_T bytes = sizeof(CREDENTIALW_) + a->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
    PCSTR strings[] = {(PCSTR)a->TargetName, (PCSTR)a->Comment, (PCSTR)a->TargetAlias, (PCSTR)a->UserName};
    for (int i = 0; i < 4; i++)
        bytes += strings[i] ? (strlen(strings[i]) + 1) * sizeof(WCHAR) : 0;
    for (DWORD i = 0; i < a->AttributeCount && a->Attributes; i++)
        bytes += a->Attributes[i].Keyword ? (strlen((PCSTR)a->Attributes[i].Keyword) + 1) * sizeof(WCHAR) : 0;
    BYTE *memory = AdvAllocZero(bytes + 16);
    if (!memory)
        return nullptr;
    CREDENTIALW_ *w = (CREDENTIALW_ *)memory;
    BYTE *at = memory + sizeof(CREDENTIALW_);
    *w = *a;
    PWSTR *fields[] = {&w->TargetName, &w->Comment, &w->TargetAlias, &w->UserName};
    for (int i = 0; i < 4; i++) {
        *fields[i] = nullptr;
        if (strings[i]) {
            *fields[i] = (PWSTR)at;
            int n = MultiByteToWideChar(CP_ACP, 0, strings[i], -1, (PWSTR)at, (int)strlen(strings[i]) + 1);
            at += n * sizeof(WCHAR);
        }
    }
    w->Attributes = nullptr;
    if (a->AttributeCount && a->Attributes) {
        w->Attributes = (CREDENTIAL_ATTRIBUTEW_ *)at;
        at += a->AttributeCount * sizeof(CREDENTIAL_ATTRIBUTEW_);
        for (DWORD i = 0; i < a->AttributeCount; i++) {
            w->Attributes[i] = a->Attributes[i];
            w->Attributes[i].Keyword = nullptr;
            if (a->Attributes[i].Keyword) {
                PCSTR k = (PCSTR)a->Attributes[i].Keyword;
                w->Attributes[i].Keyword = (PWSTR)at;
                int n = MultiByteToWideChar(CP_ACP, 0, k, -1, (PWSTR)at, (int)strlen(k) + 1);
                at += n * sizeof(WCHAR);
            }
        }
    }
    *block = memory;
    return w;
}

/* ------------------------------------------------------------------ the exports */

static BOOL fail_status(int status)
{
    SetLastError(store_load_error(status));
    return FALSE;
}

static BOOL write_one(const CREDENTIALW_ *w, DWORD flags)
{
    STORE s;
    int status = store_open(&s);
    if (status >= 0)
        status = store_write(&s, w, flags);
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredWriteW(CREDENTIALW_ *credential, DWORD flags)
{
    if (!credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return write_one(credential, flags);
}

ADVAPI BOOL WINAPI CredWriteA(CREDENTIALW_ *credential, DWORD flags)
{
    if (!credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE *block;
    CREDENTIALW_ *w = from_ansi(credential, &block);
    if (!w) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = write_one(w, flags);
    DWORD error = GetLastError();
    AdvFree(block);
    SetLastError(error);
    return ok;
}

static BOOL read_one(PCWSTR target, DWORD type, DWORD flags, BOOL give_secret, CREDENTIALW_ **credential)
{
    if (!target || !credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!*target) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    if (status >= 0)
        status = store_read(&s, target, type, flags, give_secret, credential);
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredReadW(PCWSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    return read_one(target, type, flags, FALSE, credential);
}

ADVAPI BOOL WINAPI CredReadA(PCSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    PWSTR wide = wide_of(target);
    if (!wide) {
        SetLastError(target ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = read_one(wide, type, flags, FALSE, credential);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (ok) {
        *credential = to_ansi_one(*credential);
        if (!*credential) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    } else {
        SetLastError(error);
    }
    return ok;
}

static BOOL delete_one(PCWSTR target, DWORD type, DWORD flags)
{
    if (!target || !*target) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & 0xFFFF0F60) {
        SetLastError(store_load_error(STATUS_INVALID_PARAMETER_3_));
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    if (status >= 0) {
        TARGET_NAME t;
        status = parse_target(target, type, FALSE, &t);
        if (status >= 0) {
            int at = store_find(&s, type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD ? CRED_TYPE_GENERIC : type, &t);
            free_target(&t);
            if (at < 0) {
                status = STATUS_NOT_FOUND_;
            } else {
                stored_delete(s.Items[at]);
                store_remove(&s, at);
            }
        }
    }
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredDeleteW(PCWSTR target, DWORD type, DWORD flags)
{
    return delete_one(target, type, flags);
}

ADVAPI BOOL WINAPI CredDeleteA(PCSTR target, DWORD type, DWORD flags)
{
    PWSTR wide = wide_of(target);
    if (!wide) {
        SetLastError(target ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = delete_one(wide, type, flags);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}

/* the filter is a name, or a name with a star at its start or its end. */
static int enumerate(STORE *s, PCWSTR filter, DWORD flags, DWORD *count, CREDENTIALW_ ***list)
{
    *count = 0;
    *list = nullptr;
    if (flags & ~CRED_ENUMERATE_ALL_CREDENTIALS)
        return STATUS_INVALID_PARAMETER_2_;
    if ((flags & CRED_ENUMERATE_ALL_CREDENTIALS) && filter)
        return STATUS_INVALID_PARAMETER_1_;
    BOOL everything = !filter || !*filter;
    TARGET_NAME t = {};
    PWSTR pattern = nullptr;
    DWORD star = 0;
    if (!everything) {
        int status = parse_target(filter, CRED_TYPE_GENERIC, FALSE, &t);
        if (status < 0)
            return status;
        SIZE_T length = wcslen(t.Name);
        pattern = t.Name;
        if (t.Name[length - 1] == L'*') {
            t.Name[--length] = 0;
            star = 2;
        } else if (t.Name[0] == L'*') {
            pattern = t.Name + 1;
            star = 1;
        }
    }
    DWORD *indexes = AdvAlloc((s->Count + 1) * sizeof(DWORD));
    if (!indexes) {
        free_target(&t);
        return STATUS_NO_MEMORY_;
    }
    DWORD found = 0;
    for (DWORD i = 0; i < s->Count; i++) {
        const CM_CREDENTIAL *c = s->Items[i];
        if (domain_credentials_disabled() && c->Type == CRED_TYPE_DOMAIN_PASSWORD)
            continue;
        BOOL match;
        if (flags & CRED_ENUMERATE_ALL_CREDENTIALS) {
            match = TRUE;
        } else if (everything) {
            match = (c->AttributeType == ATTRIBUTE_TARGET || c->AttributeType == ATTRIBUTE_NAME) &&
                    (equal_ci(c->Namespace, g_domain_namespace) || equal_ci(c->Namespace, g_generic_namespace));
        } else {
            match = (t.Plain ? (c->AttributeType == ATTRIBUTE_TARGET || c->AttributeType == ATTRIBUTE_NAME) &&
                                   (equal_ci(c->Namespace, g_domain_namespace) || equal_ci(c->Namespace, g_generic_namespace))
                             : c->AttributeType == t.Attribute && equal_ci(c->Namespace, t.Namespace));
            if (match) {
                SIZE_T have = wcslen(c->Name), want = wcslen(pattern);
                if (!star)
                    match = equal_ci(c->Name, pattern);
                else if (have < want)
                    match = FALSE;
                else if (star == 2)
                    match = CompareStringOrdinal(c->Name, (int)want, pattern, (int)want, TRUE) == CSTR_EQUAL;
                else
                    match = CompareStringOrdinal(c->Name + have - want, (int)want, pattern, (int)want, TRUE) == CSTR_EQUAL;
            }
        }
        if (match)
            indexes[found++] = i;
    }
    int status = 0;
    if (!found) {
        status = STATUS_NOT_FOUND_;
    } else {
        *list = result_list(s->Items, indexes, found, (flags & CRED_ENUMERATE_ALL_CREDENTIALS) == 0 && (everything || t.Plain), FALSE);
        if (*list)
            *count = found;
        else
            status = STATUS_NO_MEMORY_;
    }
    AdvFree(indexes);
    free_target(&t);
    return status;
}

static BOOL enumerate_one(PCWSTR filter, DWORD flags, DWORD *count, CREDENTIALW_ ***credentials)
{
    if (!count || !credentials) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 0;
    *credentials = nullptr;
    if (credential_manager_disabled()) {
        SetLastError(ERROR_NO_SUCH_LOGON_SESSION);
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    if (status >= 0)
        status = enumerate(&s, filter, flags, count, credentials);
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredEnumerateW(PCWSTR filter, DWORD flags, DWORD *count, CREDENTIALW_ ***credentials)
{
    return enumerate_one(filter, flags, count, credentials);
}

ADVAPI BOOL WINAPI CredEnumerateA(PCSTR filter, DWORD flags, DWORD *count, CREDENTIALW_ ***credentials)
{
    PWSTR wide = filter ? wide_of(filter) : nullptr;
    if (filter && !wide) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = enumerate_one(wide, flags, count, credentials);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (ok) {
        *credentials = to_ansi_list(*credentials, *count);
        if (!*credentials) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            *count = 0;
            return FALSE;
        }
    } else {
        SetLastError(error);
    }
    return ok;
}

/* the type of persistence each credential type can have: the enterprise's when the profile is loaded. */
ADVAPI BOOL WINAPI CredGetSessionTypes(DWORD count, LPDWORD maximum)
{
    if (count && (!maximum || count > 0x3EF)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (DWORD i = 0; i < count; i++) {
        maximum[i] = CRED_PERSIST_ENTERPRISE;
        if (i != 1 && i != 4 && i != 5 && (((i - 2) & 0xFFFFFFFA) != 0 || i == 7 || domain_credentials_disabled()))
            maximum[i] = 0;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the most specific credential for a target */

/* the names a domain target may be stored under, the most specific first. */
static DWORD candidates(PCWSTR name, DWORD type, PWSTR out[16])
{
    DWORD n = 0;
    out[n++] = copy_string(name);
    if (type == CRED_TYPE_DOMAIN_PASSWORD || type == CRED_TYPE_DOMAIN_CERTIFICATE) {
        PCWSTR host = name;
        if (!_wcsnicmp(host, L"TERMSRV/", 8))
            host += 8;
        for (PCWSTR dot = wcschr(host, L'.'); dot && n < 12; dot = wcschr(dot + 1, L'.')) {
            if (!dot[1])
                break;
            SIZE_T chars = wcslen(dot) + 2;
            PWSTR wild = AdvAlloc(chars * sizeof(WCHAR));
            if (wild) {
                _snwprintf(wild, chars, L"*%ls", dot);
                out[n++] = wild;
            }
        }
        out[n++] = copy_string(L"*Session");
        out[n++] = copy_string(L"*");
    }
    return n;
}

static int find_best(STORE *s, PCWSTR target, DWORD type, DWORD flags, BOOL give_secret, CREDENTIALW_ **result)
{
    *result = nullptr;
    if (flags)
        return STATUS_INVALID_PARAMETER_2_;
    TARGET_NAME t;
    int status = parse_target(target, type, TRUE, &t);
    if (status < 0)
        return status;
    if (t.Plain) {
        free_target(&t);
        return STATUS_INVALID_PARAMETER__;
    }
    DWORD stored = type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD ? CRED_TYPE_GENERIC : type;
    PWSTR names[16] = {};
    DWORD count = candidates(t.Name, type, names);
    int found = -1;
    for (DWORD i = 0; i < count && found < 0; i++) {
        if (!names[i])
            continue;
        for (DWORD k = 0; k < s->Count; k++)
            if (s->Items[k]->Type == stored && s->Items[k]->AttributeType == t.Attribute &&
                equal_ci(s->Items[k]->Namespace, t.Namespace) && equal_ci(s->Items[k]->Name, names[i])) {
                found = (int)k;
                break;
            }
    }
    for (DWORD i = 0; i < count; i++)
        AdvFree(names[i]);
    free_target(&t);
    if (found < 0)
        return STATUS_NOT_FOUND_;
    *result = result_one(s->Items[found], FALSE, give_secret);
    return *result ? 0 : STATUS_NO_MEMORY_;
}

static BOOL find_best_one(PCWSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    if (!target || !credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    if (status >= 0)
        status = find_best(&s, target, type, flags, FALSE, credential);
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredFindBestCredentialW(PCWSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    return find_best_one(target, type, flags, credential);
}

ADVAPI BOOL WINAPI CredFindBestCredentialA(PCSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    PWSTR wide = wide_of(target);
    if (!wide) {
        SetLastError(target ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = find_best_one(wide, type, flags, credential);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (ok) {
        *credential = to_ansi_one(*credential);
        if (!*credential) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    } else {
        SetLastError(error);
    }
    return ok;
}

/* a read for a token: it gives out the secret of domain credentials, which only trusted callers may have. */
ADVAPI BOOL WINAPI CredReadByTokenHandle(HANDLE token, PCWSTR target, DWORD type, DWORD flags, CREDENTIALW_ **credential)
{
    if (!target || !credential || !*target) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *credential = nullptr;
    if (token) {
        HANDLE real = nullptr, opened = nullptr;
        NTSTATUS status = AdvResolveToken(token, TOKEN_QUERY, &real, &opened);
        if (!NT_SUCCESS(status)) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        TOKEN_USER *user = AdvQueryTokenUser(real);
        BYTE mine[SECURITY_MAX_SID_SIZE];
        BOOL same = user && AdvCurrentUserSid(mine) && AdvSidEqual(user->User.Sid, (const SID *)mine);
        AdvFree(user);
        if (opened)
            NtClose(opened);
        if (!same) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
    }
    return read_one(target, type, flags, TRUE, credential);
}

/* ------------------------------------------------------------------ target information */

static void put_info_string(WRITER *w, PCWSTR s)
{
    put_string_block(w, s);
}

static BOOL marshal_info(const CREDENTIAL_TARGET_INFORMATIONW_ *t, BYTE **data, DWORD *size)
{
    WRITER w = {};
    put_dword(&w, t->Flags);
    put_dword(&w, t->CredTypeCount);
    put(&w, t->CredTypes, t->CredTypeCount * sizeof(DWORD));
    PCWSTR strings[] = {t->TargetName, t->NetbiosServerName, t->DnsServerName, t->NetbiosDomainName, t->DnsDomainName, t->DnsTreeName, t->PackageName};
    for (int i = 0; i < 7; i++)
        put_info_string(&w, strings[i]);
    if (w.Failed) {
        AdvFree(w.Data);
        return FALSE;
    }
    *data = w.Data;
    *size = w.Size;
    return TRUE;
}

typedef struct INFO {
    CREDENTIAL_TARGET_INFORMATIONW_ T;
    PWSTR Strings[7];
    DWORD Types[16];
} INFO;

static void free_info(INFO *i)
{
    if (!i)
        return;
    for (int k = 0; k < 7; k++)
        AdvFree(i->Strings[k]);
    AdvFree(i);
}

static INFO *unmarshal_info(const BYTE *data, DWORD size)
{
    READER r = {data, size, 0, FALSE};
    INFO *info = AdvAllocZero(sizeof(*info));
    if (!info)
        return nullptr;
    info->T.Flags = get_dword(&r);
    info->T.CredTypeCount = get_dword(&r);
    if (info->T.CredTypeCount > 16)
        r.Failed = TRUE;
    for (DWORD i = 0; !r.Failed && i < info->T.CredTypeCount; i++)
        info->Types[i] = get_dword(&r);
    info->T.CredTypes = info->T.CredTypeCount ? info->Types : nullptr;
    for (int i = 0; !r.Failed && i < 7; i++)
        info->Strings[i] = get_string(&r);
    if (r.Failed) {
        free_info(info);
        return nullptr;
    }
    info->T.TargetName = info->Strings[0];
    info->T.NetbiosServerName = info->Strings[1];
    info->T.DnsServerName = info->Strings[2];
    info->T.NetbiosDomainName = info->Strings[3];
    info->T.DnsDomainName = info->Strings[4];
    info->T.DnsTreeName = info->Strings[5];
    info->T.PackageName = info->Strings[6];
    return info;
}

static HKEY info_key(BOOL create)
{
    HKEY key = nullptr;
    DWORD disposition;
    if (create)
        RegCreateKeyExW(HKEY_CURRENT_USER, L"Volatile Environment\\Credentials\\TargetInfo", 0, nullptr, REG_OPTION_VOLATILE,
                        KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
    else
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Volatile Environment\\Credentials\\TargetInfo", 0, KEY_READ, &key);
    return key;
}

/* validates the information the way the credential manager does: names must be hosts, domains or netbios names. */
static int validate_info(const CREDENTIAL_TARGET_INFORMATIONW_ *t)
{
    if (!t || !t->TargetName || !*t->TargetName || wcslen(t->TargetName) > CRED_MAX_DOMAIN_TARGET_NAME_LENGTH)
        return STATUS_INVALID_PARAMETER__;
    PCWSTR strings[] = {t->NetbiosServerName, t->DnsServerName, t->NetbiosDomainName, t->DnsDomainName, t->DnsTreeName, t->PackageName};
    for (int i = 0; i < 6; i++)
        if (strings[i] && wcslen(strings[i]) > CRED_MAX_DOMAIN_TARGET_NAME_LENGTH)
            return STATUS_INVALID_PARAMETER__;
    if (t->CredTypeCount > 16 || (t->CredTypeCount && !t->CredTypes))
        return STATUS_INVALID_PARAMETER__;
    return 0;
}

static void cache_info(const CREDENTIAL_TARGET_INFORMATIONW_ *t)
{
    BYTE *data;
    DWORD size;
    HKEY key = info_key(TRUE);
    if (!key)
        return;
    if (marshal_info(t, &data, &size)) {
        RegSetValueExW(key, t->TargetName, 0, REG_BINARY, data, size);
        AdvFree(data);
    }
    RegCloseKey(key);
}

static INFO *find_info(PCWSTR name)
{
    HKEY key = info_key(FALSE);
    if (!key)
        return nullptr;
    INFO *found = nullptr;
    WCHAR value[CRED_MAX_DOMAIN_TARGET_NAME_LENGTH + 2];
    for (DWORD i = 0; !found; i++) {
        DWORD chars = RTL_NUMBER_OF(value), size = 0, type;
        if (RegEnumValueW(key, i, value, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) || type != REG_BINARY)
            continue;
        BYTE *data = AdvAlloc(size);
        if (data && !RegQueryValueExW(key, value, nullptr, &type, data, &size)) {
            INFO *info = unmarshal_info(data, size);
            if (info && (equal_ci(info->T.TargetName, name) ||
                         (info->T.NetbiosServerName && equal_ci(info->T.NetbiosServerName, name)) ||
                         (info->T.DnsServerName && equal_ci(info->T.DnsServerName, name))))
                found = info;
            else
                free_info(info);
        }
        AdvFree(data);
    }
    RegCloseKey(key);
    return found;
}

static SIZE_T info_bytes(const INFO *info)
{
    SIZE_T size = sizeof(CREDENTIAL_TARGET_INFORMATIONW_) + info->T.CredTypeCount * sizeof(DWORD);
    for (int i = 0; i < 7; i++)
        size += string_bytes(info->Strings[i]);
    return size + 8;
}

ADVAPI BOOL WINAPI CredGetTargetInfoW(PCWSTR target, DWORD flags, CREDENTIAL_TARGET_INFORMATIONW_ **info)
{
    if (!target || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *info = nullptr;
    if (flags & ~1u) {
        SetLastError(store_load_error(STATUS_INVALID_PARAMETER_2_));
        return FALSE;
    }
    if (!*target || wcslen(target) > CRED_MAX_DOMAIN_TARGET_NAME_LENGTH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    INFO *found = find_info(target);
    if (!found) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, info_bytes(found));
    if (memory) {
        CREDENTIAL_TARGET_INFORMATIONW_ *t = (CREDENTIAL_TARGET_INFORMATIONW_ *)memory;
        BYTE *at = memory + sizeof(*t);
        t->Flags = found->T.Flags;
        t->CredTypeCount = found->T.CredTypeCount;
        if (t->CredTypeCount) {
            t->CredTypes = (DWORD *)at;
            memcpy(at, found->Types, t->CredTypeCount * sizeof(DWORD));
            at += t->CredTypeCount * sizeof(DWORD);
        }
        PWSTR *fields[] = {&t->TargetName, &t->NetbiosServerName, &t->DnsServerName, &t->NetbiosDomainName, &t->DnsDomainName, &t->DnsTreeName, &t->PackageName};
        for (int i = 0; i < 7; i++)
            *fields[i] = (PWSTR)put_string_at(&at, found->Strings[i]);
        *info = t;
    }
    free_info(found);
    if (!memory)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return memory != nullptr;
}

ADVAPI BOOL WINAPI CredGetTargetInfoA(PCSTR target, DWORD flags, CREDENTIAL_TARGET_INFORMATIONW_ **info)
{
    PWSTR wide = wide_of(target);
    if (!wide) {
        SetLastError(target ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    CREDENTIAL_TARGET_INFORMATIONW_ *w = nullptr;
    BOOL ok = CredGetTargetInfoW(wide, flags, &w);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (!ok) {
        SetLastError(error);
        return FALSE;
    }
    PWSTR *fields[] = {&w->TargetName, &w->NetbiosServerName, &w->DnsServerName, &w->NetbiosDomainName, &w->DnsDomainName, &w->DnsTreeName, &w->PackageName};
    SIZE_T size = sizeof(*w) + w->CredTypeCount * sizeof(DWORD) + 8;
    for (int i = 0; i < 7; i++)
        size += ansi_bytes(*fields[i]);
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, size);
    if (!memory) {
        LocalFree(w);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    CREDENTIAL_TARGET_INFORMATIONW_ *a = (CREDENTIAL_TARGET_INFORMATIONW_ *)memory;
    BYTE *at = memory + sizeof(*a);
    a->Flags = w->Flags;
    a->CredTypeCount = w->CredTypeCount;
    if (a->CredTypeCount) {
        a->CredTypes = (DWORD *)at;
        memcpy(at, w->CredTypes, a->CredTypeCount * sizeof(DWORD));
        at += a->CredTypeCount * sizeof(DWORD);
    }
    PWSTR *out[] = {&a->TargetName, &a->NetbiosServerName, &a->DnsServerName, &a->NetbiosDomainName, &a->DnsDomainName, &a->DnsTreeName, &a->PackageName};
    for (int i = 0; i < 7; i++)
        *out[i] = (PWSTR)put_ansi(&at, *fields[i]);
    LocalFree(w);
    *info = a;
    return TRUE;
}

/* ------------------------------------------------------------------ domain credentials */

/* a credential's target must be one of the names of the target information, or a wildcard that covers them. */
static BOOL target_covered(const CREDENTIAL_TARGET_INFORMATIONW_ *t, PCWSTR name)
{
    PCWSTR host = name;
    if (!_wcsnicmp(host, L"TERMSRV/", 8))
        host += 8;
    if (!wcscmp(host, L"*") || !_wcsicmp(host, L"*Session"))
        return TRUE;
    PCWSTR names[] = {t->TargetName, t->NetbiosServerName, t->DnsServerName};
    for (int i = 0; i < 3; i++)
        if (names[i] && (equal_ci(names[i], name) || equal_ci(names[i], host)))
            return TRUE;
    if (host[0] == L'*' && host[1] == L'.') {
        PCWSTR domains[] = {t->DnsDomainName, t->DnsTreeName, t->DnsServerName};
        for (int i = 0; i < 3; i++) {
            if (!domains[i])
                continue;
            SIZE_T have = wcslen(domains[i]), want = wcslen(host + 1);
            PCWSTR tail = domains[i];
            for (PCWSTR dot = domains[i]; dot; dot = wcschr(dot + 1, L'.'))
                if (equal_ci(dot, host + 1))
                    return TRUE;
            (void)have;
            (void)want;
            (void)tail;
        }
    }
    PCWSTR slash = wcsrchr(host, L'\\');
    if (slash && !wcscmp(slash + 1, L"*") && t->NetbiosDomainName)
        return !_wcsnicmp(host, t->NetbiosDomainName, slash - host) && wcslen(t->NetbiosDomainName) == (SIZE_T)(slash - host);
    return FALSE;
}

static BOOL write_domain(const CREDENTIAL_TARGET_INFORMATIONW_ *info, const CREDENTIALW_ *w, DWORD flags)
{
    if (!info || !w) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    if (status >= 0) {
        if (flags & ~1u) {
            status = STATUS_INVALID_PARAMETER_3_;
        } else if (w->Type == CRED_TYPE_DOMAIN_VISIBLE_PASSWORD) {
            status = store_write(&s, w, flags);
        } else {
            status = validate_info(info);
            CM_CREDENTIAL *check = nullptr;
            if (status >= 0)
                status = make_credential(w, &check);
            if (status >= 0 && !target_covered(info, check->Name))
                status = STATUS_INVALID_PARAMETER__;
            if (status >= 0 && (w->Type != CRED_TYPE_DOMAIN_PASSWORD && w->Type != CRED_TYPE_DOMAIN_CERTIFICATE && w->Type != CRED_TYPE_GENERIC))
                status = STATUS_INVALID_PARAMETER__;
            free_credential(check);
            if (status >= 0)
                status = store_write(&s, w, flags);
            if (status >= 0)
                cache_info(info);
        }
    }
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredWriteDomainCredentialsW(CREDENTIAL_TARGET_INFORMATIONW_ *info, CREDENTIALW_ *credential, DWORD flags)
{
    return write_domain(info, credential, flags);
}

static PWSTR info_string(PCSTR s)
{
    return s ? AdvAnsiToWide(s) : nullptr;
}

/* wide target information from ansi, the strings in a block of the caller to free. */
static CREDENTIAL_TARGET_INFORMATIONW_ *info_from_ansi(const CREDENTIAL_TARGET_INFORMATIONW_ *a, PWSTR strings[7])
{
    CREDENTIAL_TARGET_INFORMATIONW_ *w = AdvAllocZero(sizeof(*w));
    if (!w)
        return nullptr;
    *w = *a;
    PCSTR in[] = {(PCSTR)a->TargetName, (PCSTR)a->NetbiosServerName, (PCSTR)a->DnsServerName, (PCSTR)a->NetbiosDomainName, (PCSTR)a->DnsDomainName, (PCSTR)a->DnsTreeName, (PCSTR)a->PackageName};
    PWSTR *fields[] = {&w->TargetName, &w->NetbiosServerName, &w->DnsServerName, &w->NetbiosDomainName, &w->DnsDomainName, &w->DnsTreeName, &w->PackageName};
    for (int i = 0; i < 7; i++) {
        strings[i] = info_string(in[i]);
        *fields[i] = strings[i];
    }
    return w;
}

ADVAPI BOOL WINAPI CredWriteDomainCredentialsA(CREDENTIAL_TARGET_INFORMATIONW_ *info, CREDENTIALW_ *credential, DWORD flags)
{
    if (!info || !credential) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR strings[7] = {};
    CREDENTIAL_TARGET_INFORMATIONW_ *w = info_from_ansi(info, strings);
    BYTE *block;
    CREDENTIALW_ *c = from_ansi(credential, &block);
    BOOL ok = FALSE;
    if (w && c)
        ok = write_domain(w, c, flags);
    else
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    DWORD error = GetLastError();
    for (int i = 0; i < 7; i++)
        AdvFree(strings[i]);
    AdvFree(w);
    AdvFree(block);
    SetLastError(error);
    return ok;
}

static BOOL read_domain(const CREDENTIAL_TARGET_INFORMATIONW_ *info, DWORD flags, DWORD *count, CREDENTIALW_ ***credentials)
{
    if (!info || !count || !credentials) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *count = 0;
    *credentials = nullptr;
    STORE s;
    int status = store_open(&s);
    if (status >= 0) {
        if (flags & ~CRED_CACHE_TARGET_INFORMATION) {
            status = STATUS_INVALID_PARAMETER_2_;
        } else if ((status = validate_info(info)) >= 0) {
            DWORD default_types[2] = {CRED_TYPE_DOMAIN_PASSWORD, CRED_TYPE_DOMAIN_CERTIFICATE};
            const DWORD *types = info->CredTypeCount ? info->CredTypes : default_types;
            DWORD type_count = info->CredTypeCount ? info->CredTypeCount : 2;
            PCWSTR servers[] = {info->TargetName, info->DnsServerName, info->NetbiosServerName};
            DWORD *indexes = AdvAlloc((type_count * 16 + 1) * sizeof(DWORD));
            DWORD found = 0;
            for (DWORD ti = 0; indexes && ti < type_count; ti++) {
                int best = -1;
                for (int pass = 0; pass < 3 && best < 0; pass++) {
                    if (!servers[pass])
                        continue;
                    PWSTR names[16] = {};
                    DWORD n = candidates(servers[pass], types[ti], names);
                    for (DWORD ni = 0; ni < n && best < 0; ni++)
                        for (DWORD k = 0; names[ni] && k < s.Count; k++)
                            if (s.Items[k]->Type == types[ti] && s.Items[k]->AttributeType == ATTRIBUTE_TARGET &&
                                equal_ci(s.Items[k]->Namespace, g_domain_namespace) && equal_ci(s.Items[k]->Name, names[ni])) {
                                best = (int)k;
                                break;
                            }
                    for (DWORD ni = 0; ni < n; ni++)
                        AdvFree(names[ni]);
                }
                if (best >= 0)
                    indexes[found++] = best;
            }
            if (!indexes)
                status = STATUS_NO_MEMORY_;
            else if (!found)
                status = STATUS_NOT_FOUND_;
            else {
                *credentials = result_list(s.Items, indexes, found, FALSE, FALSE);
                if (*credentials)
                    *count = found;
                else
                    status = STATUS_NO_MEMORY_;
                if (*credentials && (flags & CRED_CACHE_TARGET_INFORMATION))
                    cache_info(info);
            }
            AdvFree(indexes);
        }
    }
    store_close(&s);
    return status < 0 ? fail_status(status) : TRUE;
}

ADVAPI BOOL WINAPI CredReadDomainCredentialsW(CREDENTIAL_TARGET_INFORMATIONW_ *info, DWORD flags, DWORD *count,
                                              CREDENTIALW_ ***credentials)
{
    return read_domain(info, flags, count, credentials);
}

ADVAPI BOOL WINAPI CredReadDomainCredentialsA(CREDENTIAL_TARGET_INFORMATIONW_ *info, DWORD flags, DWORD *count,
                                              CREDENTIALW_ ***credentials)
{
    if (!info || !count || !credentials) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR strings[7] = {};
    CREDENTIAL_TARGET_INFORMATIONW_ *w = info_from_ansi(info, strings);
    BOOL ok = w ? read_domain(w, flags, count, credentials) : FALSE;
    DWORD error = w ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    for (int i = 0; i < 7; i++)
        AdvFree(strings[i]);
    AdvFree(w);
    if (ok) {
        *credentials = to_ansi_list(*credentials, *count);
        if (!*credentials) {
            *count = 0;
            error = ERROR_NOT_ENOUGH_MEMORY;
            ok = FALSE;
        }
    }
    SetLastError(error);
    return ok;
}

/* ------------------------------------------------------------------ profiles, backup and restore */

/* the store follows the profile's folders and the logon session's key, so loading one has nothing to bring in. */
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

/* the backup is the credentials of the machine and the enterprise, one after the other, with a header of three DWORD. */
static BYTE *backup_encrypt(const BYTE *data, DWORD size, const BYTE *key, DWORD key_size, DWORD *out_size)
{
    BYTE secret[32], iv[16];
    mime_hash(&mime_sha256, key, key_size, secret);
    if (!mime_crypto_random(iv, sizeof(iv)))
        return nullptr;
    DWORD padded = (size + 16) & ~15u;
    BYTE *out = AdvAllocZero(16 + padded + 32);
    if (!out)
        return nullptr;
    memcpy(out, iv, 16);
    memcpy(out + 16, data, size);
    memset(out + 16 + size, (BYTE)(padded - size), padded - size);
    MimeAes aes;
    mime_aes_init(&aes, secret, sizeof(secret));
    BYTE chain[16];
    memcpy(chain, iv, 16);
    mime_cbc_encrypt(&mime_cipher_aes, &aes, chain, out + 16, out + 16, padded);
    mime_hmac(&mime_sha256, secret, sizeof(secret), out, 16 + padded, out + 16 + padded);
    *out_size = 16 + padded + 32;
    return out;
}

static BYTE *backup_decrypt(const BYTE *data, DWORD size, const BYTE *key, DWORD key_size, DWORD *out_size)
{
    if (size < 16 + 16 + 32 || (size - 48) % 16)
        return nullptr;
    BYTE secret[32], mac[32];
    mime_hash(&mime_sha256, key, key_size, secret);
    DWORD padded = size - 48;
    mime_hmac(&mime_sha256, secret, sizeof(secret), data, 16 + padded, mac);
    if (!mime_ct_equal(mac, data + 16 + padded, 32))
        return nullptr;
    BYTE *out = AdvAlloc(padded);
    if (!out)
        return nullptr;
    MimeAes aes;
    mime_aes_init(&aes, secret, sizeof(secret));
    BYTE chain[16];
    memcpy(chain, data, 16);
    mime_cbc_decrypt(&mime_cipher_aes, &aes, chain, data + 16, out, padded);
    BYTE pad = out[padded - 1];
    if (!pad || pad > 16 || pad > padded) {
        AdvFree(out);
        return nullptr;
    }
    *out_size = padded - pad;
    return out;
}

ADVAPI BOOL WINAPI CredBackupCredentials(HANDLE token, PCWSTR path, PVOID key, DWORD key_size, DWORD flags)
{
    if (flags & ~1u) {
        SetLastError(store_load_error(STATUS_INVALID_PARAMETER_5_));
        return FALSE;
    }
    if (!path) {
        SetLastError(store_load_error(STATUS_INVALID_PARAMETER_3_));
        return FALSE;
    }
    if (token) {
        HANDLE real = nullptr, opened = nullptr;
        if (!NT_SUCCESS(AdvResolveToken(token, TOKEN_QUERY, &real, &opened))) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        if (opened)
            NtClose(opened);
    }
    STORE s;
    int status = store_open(&s);
    BYTE *blob = nullptr, *protected_blob = nullptr;
    DWORD blob_size = 0, protected_size = 0;
    if (status >= 0) {
        WRITER w = {};
        for (DWORD i = 0; i < s.Count; i++) {
            if (s.Items[i]->Persist == CRED_PERSIST_SESSION)
                continue;
            BYTE *one;
            DWORD one_size;
            if (!marshal_credential(s.Items[i], FALSE, &one, &one_size)) {
                w.Failed = TRUE;
                break;
            }
            put(&w, one, one_size);
            align_even(&w);
            memset(one, 0, one_size);
            AdvFree(one);
        }
        if (w.Failed)
            status = STATUS_NO_MEMORY_;
        blob = w.Data;
        blob_size = w.Size;
    }
    store_close(&s);
    DWORD error = ERROR_SUCCESS;
    if (status >= 0) {
        BYTE *payload = blob;
        DWORD payload_size = blob_size, mode = 2;
        BYTE *sealed = nullptr;
        if (!(flags & 1) && key) {
            sealed = backup_encrypt(blob, blob_size, key, key_size, &payload_size);
            payload = sealed;
            mode = 1;
        }
        if (!payload && blob_size) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            WRITER w = {};
            put_dword(&w, 1);
            put_dword(&w, payload_size);
            put_dword(&w, mode);
            put(&w, payload, payload_size);
            protected_blob = w.Failed ? nullptr : dpapi_protect(w.Data, w.Size, L"Credman Backup Credentials", &protected_size);
            if (w.Data) {
                memset(w.Data, 0, w.Size);
                AdvFree(w.Data);
            }
            if (!protected_blob)
                error = GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        }
        AdvFree(sealed);
        if (!error) {
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                error = GetLastError();
            } else {
                DWORD written;
                if (GetFileType(file) != FILE_TYPE_DISK)
                    error = ERROR_NOT_SUPPORTED;
                else if (!WriteFile(file, protected_blob, protected_size, &written, nullptr))
                    error = GetLastError();
                CloseHandle(file);
                if (error)
                    DeleteFileW(path);
            }
        }
    } else {
        error = store_load_error(status);
    }
    if (blob) {
        memset(blob, 0, blob_size);
        AdvFree(blob);
    }
    AdvFree(protected_blob);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOL WINAPI CredRestoreCredentials(HANDLE token, PCWSTR path, PVOID key, DWORD key_size)
{
    if (!path) {
        SetLastError(store_load_error(STATUS_INVALID_PARAMETER_2_));
        return FALSE;
    }
    if (token) {
        HANDLE real = nullptr, opened = nullptr;
        if (!NT_SUCCESS(AdvResolveToken(token, TOKEN_QUERY, &real, &opened))) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        if (opened)
            NtClose(opened);
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = size >= 12 && size < 0x1000000 ? AdvAlloc(size) : nullptr;
    BOOL ok = data && ReadFile(file, data, size, &read, nullptr) && read == size;
    CloseHandle(file);
    if (!ok) {
        AdvFree(data);
        SetLastError(data ? GetLastError() : ERROR_BAD_FORMAT);
        return FALSE;
    }
    DWORD plain_size = 0;
    BYTE *plain = dpapi_unprotect(data, size, &plain_size);
    AdvFree(data);
    if (!plain) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    DWORD version, length, mode;
    BYTE *payload = nullptr;
    DWORD error = ERROR_INVALID_DATA, payload_size = 0;
    if (plain_size >= 12) {
        memcpy(&version, plain, 4);
        memcpy(&length, plain + 4, 4);
        memcpy(&mode, plain + 8, 4);
        if (version == 1 && length == plain_size - 12) {
            if (mode == 1 && key) {
                payload = backup_decrypt(plain + 12, length, key, key_size, &payload_size);
            } else if (mode == 2) {
                payload = copy_bytes(plain + 12, length);
                payload_size = length;
                if (!payload && length)
                    error = ERROR_NOT_ENOUGH_MEMORY;
            }
            if (payload || !length)
                error = ERROR_SUCCESS;
        }
    }
    memset(plain, 0, plain_size);
    AdvFree(plain);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    STORE s;
    int status = store_open(&s);
    DWORD offset = 0;
    while (status >= 0 && offset + 8 <= payload_size) {
        DWORD one;
        memcpy(&one, payload + offset + 4, 4);
        if (one < 48 || offset + one > payload_size) {
            status = (int)(0xC0010000 | ERROR_INVALID_DATA);
            break;
        }
        CM_CREDENTIAL *c = unmarshal_credential(payload + offset, one);
        if (!c) {
            status = (int)(0xC0010000 | ERROR_INVALID_DATA);
            break;
        }
        TARGET_NAME t = {c->Namespace, c->Name, c->AttributeType, FALSE, FALSE};
        int at = store_find(&s, c->Type, &t);
        DWORD wrote = stored_write(c);
        if (wrote) {
            free_credential(c);
            status = (int)(0xC0010000 | (wrote & 0xFFFF));
            break;
        }
        if (at >= 0) {
            WCHAR a[33], b[33];
            credential_file_name(s.Items[at], a);
            credential_file_name(c, b);
            if (s.Items[at]->Persist != c->Persist || wcscmp(a, b))
                stored_delete(s.Items[at]);
            store_remove(&s, at);
        }
        if (!store_add(&s, c))
            free_credential(c);
        offset += (one + 1) & ~1u;
    }
    store_close(&s);
    if (payload) {
        memset(payload, 0, payload_size);
        AdvFree(payload);
    }
    if (status < 0) {
        SetLastError(store_load_error(status));
        return FALSE;
    }
    return TRUE;
}

/* the manager does not rename a credential. */
ADVAPI BOOL WINAPI CredRenameW(PCWSTR old_name, PCWSTR new_name, DWORD type, DWORD flags)
{
    (void)old_name;
    (void)new_name;
    (void)type;
    (void)flags;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

ADVAPI BOOL WINAPI CredRenameA(PCSTR old_name, PCSTR new_name, DWORD type, DWORD flags)
{
    (void)old_name;
    (void)new_name;
    (void)type;
    (void)flags;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
