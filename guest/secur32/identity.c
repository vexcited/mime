/*
 * the authentication identity helpers: SEC_WINNT_AUTH_IDENTITY_W and _A,
 * the versioned SEC_WINNT_AUTH_IDENTITY_EXW and the self relative
 * SEC_WINNT_AUTH_IDENTITY_EX2 that the opaque Sspi functions exchange.
 *
 * mime builds every identity it returns in the EX2 layout, one LocalAlloc
 * block whose password is a packed password credential, and reads all
 * three layouts. encrypting an identity needs the system's memory
 * encryption, which mime lacks.
 */
#include "secur32_private.h"

/* the credential type of a password in packed credentials. */
static const GUID g_password_type = {0x28BFC32F, 0x10F6, 0x4738, {0x98, 0xD1, 0x1A, 0xC0, 0x61, 0xDF, 0x71, 0x6A}};

typedef struct PACKED_CREDENTIALS {
    USHORT cbHeaderLength;
    USHORT cbStructureLength;
    GUID CredType;
    ULONG ByteArrayOffset;
    USHORT ByteArrayLength;
} PACKED_CREDENTIALS;
MIME_STATIC_ASSERT(sizeof(PACKED_CREDENTIALS) == 28, "SEC_WINNT_AUTH_PACKED_CREDENTIALS size");

#define ENCRYPTED_FLAGS                                                                                                \
    (SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED | SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED |                 \
     SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED)

/* the parts of an identity, as process heap strings. */
typedef struct IDENTITY {
    PWSTR User;
    PWSTR Domain;
    PWSTR Password;
    PWSTR Packages;
    ULONG Flags;
} IDENTITY;

static void identity_free(IDENTITY *id)
{
    if (id->Password)
        memset(id->Password, 0, wcslen(id->Password) * sizeof(WCHAR));
    SecHeapFree(id->User);
    SecHeapFree(id->Domain);
    SecHeapFree(id->Password);
    SecHeapFree(id->Packages);
    memset(id, 0, sizeof(*id));
}

static bool copy_part(PWSTR *out, const void *text, ULONG chars, bool ansi)
{
    *out = NULL;
    if (!text || !chars)
        return true;
    PWSTR s = SecHeapAlloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
    if (!s)
        return false;
    ULONG n = chars;
    if (ansi)
        n = MultiByteToWideChar(CP_ACP, 0, text, (int)chars, s, (int)chars);
    else
        memcpy(s, text, chars * sizeof(WCHAR));
    s[n] = 0;
    *out = s;
    return true;
}

static bool is_ex2(const void *data)
{
    return *(const ULONG *)data == SEC_WINNT_AUTH_IDENTITY_VERSION_2;
}

static bool is_ex(const void *data)
{
    return *(const ULONG *)data == SEC_WINNT_AUTH_IDENTITY_VERSION;
}

/* the plain and EX layouts share their members from User on. */
static const SEC_WINNT_AUTH_IDENTITY_W *plain_part(const void *data)
{
    return is_ex(data) ? (const SEC_WINNT_AUTH_IDENTITY_W *)((const UCHAR *)data + 8) : data;
}

static bool contained(ULONG offset, ULONG length, ULONG size)
{
    return offset + length >= offset && offset + length <= size;
}

/* the checks of SspiValidateAuthIdentity on the self relative layout. */
static bool valid_ex2(const SEC_WINNT_AUTH_IDENTITY_EX2 *e)
{
    ULONG size = e->cbStructureLength;
    if (e->cbHeaderLength < sizeof(*e) || size > 0xFFF7 || size < e->cbHeaderLength)
        return false;
    if (!contained(e->UserOffset, e->UserLength, size) || !contained(e->DomainOffset, e->DomainLength, size) ||
        !contained(e->PackageListOffset, e->PackageListLength, size) ||
        !contained(e->PackedCredentialsOffset, e->PackedCredentialsLength, size))
        return false;
    if (!e->PackedCredentialsOffset)
        return !e->PackedCredentialsLength;
    if (e->PackedCredentialsLength < sizeof(PACKED_CREDENTIALS))
        return false;
    const PACKED_CREDENTIALS *p = (const PACKED_CREDENTIALS *)((const UCHAR *)e + e->PackedCredentialsOffset);
    return p->cbHeaderLength >= sizeof(*p) && p->cbStructureLength <= e->PackedCredentialsLength &&
           contained(p->ByteArrayOffset, p->ByteArrayLength, p->cbStructureLength);
}

static NTSTATUS parse(const void *data, IDENTITY *id)
{
    memset(id, 0, sizeof(*id));
    if (!data)
        return STATUS_INVALID_PARAMETER;
    bool ok;
    if (is_ex2(data)) {
        const SEC_WINNT_AUTH_IDENTITY_EX2 *e = data;
        const UCHAR *base = data;
        if (!valid_ex2(e))
            return STATUS_INVALID_PARAMETER;
        id->Flags = e->Flags;
        ok = copy_part(&id->User, base + e->UserOffset, e->UserLength / sizeof(WCHAR), false) &&
             copy_part(&id->Domain, base + e->DomainOffset, e->DomainLength / sizeof(WCHAR), false) &&
             copy_part(&id->Packages, base + e->PackageListOffset, e->PackageListLength / sizeof(WCHAR), false);
        if (ok && e->PackedCredentialsLength) {
            const PACKED_CREDENTIALS *p = (const PACKED_CREDENTIALS *)(base + e->PackedCredentialsOffset);
            if (!memcmp(&p->CredType, &g_password_type, sizeof(GUID)))
                ok = copy_part(&id->Password, (const UCHAR *)p + p->ByteArrayOffset,
                               p->ByteArrayLength / sizeof(WCHAR), false);
        }
    } else {
        const SEC_WINNT_AUTH_IDENTITY_W *w = plain_part(data);
        bool ansi = w->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI;
        id->Flags = w->Flags;
        ok = copy_part(&id->User, w->User, w->UserLength, ansi) &&
             copy_part(&id->Domain, w->Domain, w->DomainLength, ansi) &&
             copy_part(&id->Password, w->Password, w->PasswordLength, ansi);
        if (ok && is_ex(data)) {
            const SEC_WINNT_AUTH_IDENTITY_EXW *e = data;
            ok = copy_part(&id->Packages, e->PackageList, e->PackageListLength, ansi);
        }
    }
    if (!ok) {
        identity_free(id);
        return STATUS_NO_MEMORY;
    }
    id->Flags &= ~(SEC_WINNT_AUTH_IDENTITY_ANSI | SEC_WINNT_AUTH_IDENTITY_UNICODE);
    return STATUS_SUCCESS;
}

static ULONG bytes_of(PCWSTR s)
{
    return s ? (ULONG)wcslen(s) * sizeof(WCHAR) : 0;
}

/* the identity as one self relative EX2 block from LocalAlloc. */
static NTSTATUS build_ex2(const IDENTITY *id, SEC_WINNT_AUTH_IDENTITY_EX2 **out, ULONG *size)
{
    ULONG user = bytes_of(id->User), domain = bytes_of(id->Domain), password = bytes_of(id->Password);
    ULONG packages = bytes_of(id->Packages);
    ULONG packed = id->Password ? sizeof(PACKED_CREDENTIALS) + password : 0;
    ULONG total = sizeof(SEC_WINNT_AUTH_IDENTITY_EX2) + user + domain + packed + packages;
    if (user > 0xFFFF || domain > 0xFFFF || packed > 0xFFFF || packages > 0xFFFF || total > 0xFFF7)
        return STATUS_INVALID_PARAMETER;
    SEC_WINNT_AUTH_IDENTITY_EX2 *e = LocalAlloc(LPTR, total);
    if (!e)
        return STATUS_NO_MEMORY;
    UCHAR *base = (UCHAR *)e;
    ULONG at = sizeof(*e);
    e->Version = SEC_WINNT_AUTH_IDENTITY_VERSION_2;
    e->cbHeaderLength = sizeof(*e);
    e->cbStructureLength = total;
    e->Flags = id->Flags | SEC_WINNT_AUTH_IDENTITY_UNICODE;
    if (user) {
        e->UserOffset = at;
        e->UserLength = (USHORT)user;
        memcpy(base + at, id->User, user);
        at += user;
    }
    if (domain) {
        e->DomainOffset = at;
        e->DomainLength = (USHORT)domain;
        memcpy(base + at, id->Domain, domain);
        at += domain;
    }
    if (packed) {
        PACKED_CREDENTIALS *p = (PACKED_CREDENTIALS *)(base + at);
        p->cbHeaderLength = sizeof(*p);
        p->cbStructureLength = (USHORT)packed;
        p->CredType = g_password_type;
        p->ByteArrayOffset = sizeof(*p);
        p->ByteArrayLength = (USHORT)password;
        memcpy(p + 1, id->Password, password);
        e->PackedCredentialsOffset = at;
        e->PackedCredentialsLength = (USHORT)packed;
        at += packed;
    }
    if (packages) {
        e->PackageListOffset = at;
        e->PackageListLength = (USHORT)packages;
        memcpy(base + at, id->Packages, packages);
    }
    *out = e;
    if (size)
        *size = total;
    return STATUS_SUCCESS;
}

SECURITYAPI void SEC_ENTRY SspiLocalFree(PVOID buffer)
{
    if (buffer)
        LocalFree(buffer);
}

SECURITYAPI void SEC_ENTRY SspiZeroAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return;
    if (is_ex2(identity)) {
        SEC_WINNT_AUTH_IDENTITY_EX2 *e = identity;
        if (e->cbStructureLength > e->cbHeaderLength)
            memset((UCHAR *)e + e->cbHeaderLength, 0, e->cbStructureLength - e->cbHeaderLength);
        return;
    }
    const SEC_WINNT_AUTH_IDENTITY_W *w = plain_part(identity);
    if (w->Password && w->PasswordLength)
        memset(w->Password, 0, w->PasswordLength * (w->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI ? 1 : 2));
}

/* SspiFreeAuthIdentity frees the members of the plain and EX layouts unless they were packed with the structure. */
SECURITYAPI void SEC_ENTRY SspiFreeAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return;
    SspiZeroAuthIdentity(identity);
    if (!is_ex2(identity)) {
        SEC_WINNT_AUTH_IDENTITY_W *w = (SEC_WINNT_AUTH_IDENTITY_W *)plain_part(identity);
        if (is_ex(identity)) {
            SEC_WINNT_AUTH_IDENTITY_EXW *e = identity;
            if (!(e->Flags & SEC_WINNT_AUTH_IDENTITY_MARSHALLED)) {
                SspiLocalFree(e->PackageList);
                e->PackageList = NULL;
            }
        }
        if (!(w->Flags & SEC_WINNT_AUTH_IDENTITY_MARSHALLED)) {
            SspiLocalFree(w->User);
            SspiLocalFree(w->Domain);
            SspiLocalFree(w->Password);
            w->User = w->Domain = w->Password = NULL;
        }
    }
    SspiLocalFree(identity);
}

SECURITYAPI BOOLEAN SEC_ENTRY SspiIsAuthIdentityEncrypted(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return FALSE;
    if (is_ex2(identity))
        return (((SEC_WINNT_AUTH_IDENTITY_EX2 *)identity)->Flags & 0xF0) != 0;
    return (plain_part(identity)->Flags & ENCRYPTED_FLAGS) != 0;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiValidateAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return SEC_E_INVALID_TOKEN;
    if (is_ex2(identity))
        return valid_ex2(identity) ? SEC_E_OK : SEC_E_INVALID_TOKEN;
    const SEC_WINNT_AUTH_IDENTITY_W *w = plain_part(identity);
    ULONG charset = w->Flags & (SEC_WINNT_AUTH_IDENTITY_ANSI | SEC_WINNT_AUTH_IDENTITY_UNICODE);
    if (charset != SEC_WINNT_AUTH_IDENTITY_ANSI && charset != SEC_WINNT_AUTH_IDENTITY_UNICODE)
        return SEC_E_INVALID_TOKEN;
    if ((!w->User && w->UserLength) || (!w->Domain && w->DomainLength) || (!w->Password && w->PasswordLength))
        return SEC_E_INVALID_TOKEN;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiCopyAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity,
                                                           PSEC_WINNT_AUTH_IDENTITY_OPAQUE *copy)
{
    if (!copy)
        return SEC_E_INVALID_TOKEN;
    *copy = NULL;
    IDENTITY id;
    NTSTATUS status = parse(identity, &id);
    if (NT_SUCCESS(status)) {
        SEC_WINNT_AUTH_IDENTITY_EX2 *e;
        status = build_ex2(&id, &e, NULL);
        if (NT_SUCCESS(status))
            *copy = e;
        identity_free(&id);
    }
    return SecNtStatusToSecStatus(status);
}

static PWSTR local_string(PCWSTR s)
{
    if (!s)
        return NULL;
    SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    PWSTR out = LocalAlloc(LMEM_FIXED, bytes);
    if (out)
        memcpy(out, s, bytes);
    return out;
}

/* sspicli returns the raw STATUS_INVALID_PARAMETER for a missing identity. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiEncodeAuthIdentityAsStrings(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity,
                                                                      PCWSTR *user, PCWSTR *domain, PCWSTR *packed)
{
    if (!identity || !user || !domain)
        return STATUS_INVALID_PARAMETER;
    *user = *domain = NULL;
    if (packed)
        *packed = NULL;
    IDENTITY id;
    NTSTATUS status = parse(identity, &id);
    if (!NT_SUCCESS(status))
        return SecNtStatusToSecStatus(status);
    PWSTR u = local_string(id.User), d = local_string(id.Domain), p = packed ? local_string(id.Password) : NULL;
    bool ok = (!id.User || u) && (!id.Domain || d) && (!packed || !id.Password || p);
    identity_free(&id);
    if (!ok) {
        SspiLocalFree(u);
        SspiLocalFree(d);
        SspiLocalFree(p);
        return SEC_E_INSUFFICIENT_MEMORY;
    }
    *user = u;
    *domain = d;
    if (packed)
        *packed = p;
    return SEC_E_OK;
}

/* a user of the form domain\user without a domain gives both parts; the packed string is a password. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiEncodeStringsAsAuthIdentity(PCWSTR user, PCWSTR domain, PCWSTR packed,
                                                                      PSEC_WINNT_AUTH_IDENTITY_OPAQUE *identity)
{
    if (!identity || (!user && !domain && !packed))
        return SecNtStatusToSecStatus(STATUS_INVALID_PARAMETER);
    *identity = NULL;
    IDENTITY id = {0};
    bool ok = true;
    PCWSTR slash = user && (!domain || !*domain) ? wcschr(user, L'\\') : NULL;
    if (slash) {
        ok = copy_part(&id.Domain, user, (ULONG)(slash - user), false) &&
             copy_part(&id.User, slash + 1, (ULONG)wcslen(slash + 1), false);
    } else {
        ok = copy_part(&id.User, user, user ? (ULONG)wcslen(user) : 0, false) &&
             copy_part(&id.Domain, domain, domain ? (ULONG)wcslen(domain) : 0, false);
    }
    ok = ok && copy_part(&id.Password, packed, packed ? (ULONG)wcslen(packed) : 0, false);
    NTSTATUS status = STATUS_NO_MEMORY;
    if (ok) {
        SEC_WINNT_AUTH_IDENTITY_EX2 *e;
        status = build_ex2(&id, &e, NULL);
        if (NT_SUCCESS(status))
            *identity = e;
    }
    identity_free(&id);
    return SecNtStatusToSecStatus(status);
}

static bool same(PCWSTR a, PCWSTR b, bool case_insensitive)
{
    a = a ? a : L"";
    b = b ? b : L"";
    return case_insensitive ? !_wcsicmp(a, b) : !wcscmp(a, b);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiCompareAuthIdentities(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity1,
                                                                PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity2,
                                                                PBOOLEAN same_user, PBOOLEAN same_identity)
{
    if (same_user)
        *same_user = FALSE;
    if (same_identity)
        *same_identity = FALSE;
    IDENTITY a = {0}, b = {0};
    NTSTATUS status = identity1 ? parse(identity1, &a) : STATUS_SUCCESS;
    if (NT_SUCCESS(status) && identity2)
        status = parse(identity2, &b);
    if (NT_SUCCESS(status) && same(a.User, b.User, true) && same(a.Domain, b.Domain, true)) {
        if (same_user)
            *same_user = TRUE;
        if (same_identity && same(a.Password, b.Password, false))
            *same_identity = TRUE;
    }
    identity_free(&a);
    identity_free(&b);
    return SecNtStatusToSecStatus(status);
}

/* the marshaled form is the self relative EX2 layout itself. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiMarshalAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity, PULONG length,
                                                              PCHAR *bytes)
{
    if (!length || !bytes)
        return SEC_E_INVALID_TOKEN;
    *length = 0;
    *bytes = NULL;
    IDENTITY id;
    NTSTATUS status = parse(identity, &id);
    if (NT_SUCCESS(status)) {
        SEC_WINNT_AUTH_IDENTITY_EX2 *e;
        status = build_ex2(&id, &e, length);
        if (NT_SUCCESS(status))
            *bytes = (PCHAR)e;
        identity_free(&id);
    }
    return SecNtStatusToSecStatus(status);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiUnmarshalAuthIdentity(ULONG length, PCHAR bytes,
                                                                PSEC_WINNT_AUTH_IDENTITY_OPAQUE *identity)
{
    if (!identity)
        return SEC_E_INVALID_TOKEN;
    *identity = NULL;
    const SEC_WINNT_AUTH_IDENTITY_EX2 *e = (const SEC_WINNT_AUTH_IDENTITY_EX2 *)bytes;
    if (!bytes || length < sizeof(*e) || !is_ex2(e) || e->cbStructureLength > length || !valid_ex2(e))
        return SEC_E_INVALID_TOKEN;
    PVOID copy = LocalAlloc(LMEM_FIXED, e->cbStructureLength);
    if (!copy)
        return SEC_E_INSUFFICIENT_MEMORY;
    memcpy(copy, e, e->cbStructureLength);
    *identity = copy;
    return SEC_E_OK;
}

/* process encryption uses the kernel's memory encryption, which mime does not have. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiEncryptAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return SEC_E_INVALID_TOKEN;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiDecryptAuthIdentity(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity)
{
    if (!identity)
        return SEC_E_INVALID_TOKEN;
    return SspiIsAuthIdentityEncrypted(identity) ? SEC_E_UNSUPPORTED_FUNCTION : SEC_E_OK;
}

/* credential manager is not implemented, so there is no target to prepare. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiPrepareForCredRead(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity, PCWSTR target,
                                                             PULONG type, PCWSTR *credman_target)
{
    if (credman_target)
        *credman_target = NULL;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiPrepareForCredWrite(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity, PCWSTR target,
                                                              PULONG type, PCWSTR *credman_target, PCWSTR *user,
                                                              PUCHAR *credential_blob, PULONG credential_size)
{
    if (credman_target)
        *credman_target = NULL;
    if (user)
        *user = NULL;
    if (credential_blob)
        *credential_blob = NULL;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiExcludePackage(PSEC_WINNT_AUTH_IDENTITY_OPAQUE identity, PCWSTR package,
                                                         PSEC_WINNT_AUTH_IDENTITY_OPAQUE *new_identity)
{
    if (new_identity)
        *new_identity = NULL;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/*
 * the host of a service principal name, class/host:port/service@realm, as
 * SspiHelperGetTargetHostName cuts it: between the first two slashes, then
 * before the last colon and the last at sign, without a trailing dollar.
 * a name without a slash is returned whole, cut at its last at sign.
 */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SspiGetTargetHostName(PCWSTR target, PWSTR *host)
{
    if (!host)
        return SEC_E_INVALID_PARAMETER;
    *host = NULL;
    PCWSTR start = target ? wcschr(target, L'/') : NULL;
    SIZE_T n;
    if (!start) {
        n = target ? wcslen(target) : 0;
        PWSTR out = LocalAlloc(LPTR, (n + 2) * sizeof(WCHAR));
        if (!out)
            return SEC_E_INSUFFICIENT_MEMORY;
        if (n)
            memcpy(out, target, n * sizeof(WCHAR));
        PWSTR at = wcsrchr(out, L'@');
        if (at)
            *at = 0;
        *host = out;
        return SEC_E_OK;
    }
    start++;
    PCWSTR end = wcschr(start, L'/');
    n = end ? (SIZE_T)(end - start) : wcslen(start);
    for (SIZE_T i = n; i; i--) {
        if (start[i - 1] == L':') {
            n = i - 1;
            break;
        }
    }
    for (SIZE_T i = n; i; i--) {
        if (start[i - 1] == L'@') {
            n = i - 1;
            break;
        }
    }
    if (n && start[n - 1] == L'$')
        n--;
    PWSTR out = LocalAlloc(LPTR, (n + 1) * sizeof(WCHAR));
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    memcpy(out, start, n * sizeof(WCHAR));
    *host = out;
    return SEC_E_OK;
}
