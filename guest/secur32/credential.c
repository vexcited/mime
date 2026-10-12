/*
 * credential handles: acquiring, querying and freeing them.
 *
 * like sspicli, the lower half of a handle points to the package and the
 * upper half identifies the credential. credentials live in a list under a
 * lock, so a stale or forged handle is rejected instead of dereferenced.
 */
#include "secur32_private.h"

typedef struct SEC_CREDENTIAL {
    struct SEC_CREDENTIAL *Next;
    const SEC_PACKAGE *Package;
    ULONG Use;
    /* domain\user of the supplied identity, or NULL for the logged on user. */
    PWSTR Name;
} SEC_CREDENTIAL;

static RTL_CRITICAL_SECTION g_lock = RTL_CRITICAL_SECTION_INIT;
static SEC_CREDENTIAL *g_credentials;

static bool is_package(ULONG_PTR p)
{
    ULONG_PTR first = (ULONG_PTR)&SecPackages[0];
    ULONG_PTR end = (ULONG_PTR)&SecPackages[SecPackageCount];
    return p >= first && p < end && (p - first) % sizeof(SEC_PACKAGE) == 0;
}

/* the credential behind a handle; the lock must be held. */
static SEC_CREDENTIAL *find(PCredHandle handle)
{
    if (!handle || !is_package(handle->dwLower))
        return NULL;
    for (SEC_CREDENTIAL *c = g_credentials; c; c = c->Next) {
        if ((ULONG_PTR)c == handle->dwUpper && (ULONG_PTR)c->Package == handle->dwLower)
            return c;
    }
    return NULL;
}

bool SecLookupCredential(PCredHandle handle, const SEC_PACKAGE **package, ULONG *use)
{
    RtlEnterCriticalSection(&g_lock);
    SEC_CREDENTIAL *c = find(handle);
    if (c) {
        *package = c->Package;
        *use = c->Use;
    }
    RtlLeaveCriticalSection(&g_lock);
    return c != NULL;
}

/* packages that take a SEC_WINNT_AUTH_IDENTITY as authentication data. */
static bool takes_identity(const SEC_PACKAGE *p)
{
    switch (p->Kind) {
    case SEC_KIND_NEGOTIATE:
    case SEC_KIND_EXTENDER:
    case SEC_KIND_KERBEROS:
    case SEC_KIND_NTLM:
    case SEC_KIND_PKU2U:
    case SEC_KIND_DIGEST:
        return true;
    default:
        return false;
    }
}

static PWSTR join_name(const void *domain, ULONG domain_length, const void *user, ULONG user_length, bool ansi)
{
    if (!user || !user_length)
        return NULL;
    if (!domain)
        domain_length = 0;
    ULONG n = domain_length + (domain_length ? 1 : 0) + user_length;
    PWSTR out = SecHeapAlloc((n + 1) * sizeof(WCHAR));
    if (!out)
        return NULL;
    ULONG at = 0;
    for (int part = 0; part < 2; part++) {
        const void *text = part ? user : domain;
        ULONG length = part ? user_length : domain_length;
        if (!length)
            continue;
        if (ansi) {
            at += MultiByteToWideChar(CP_ACP, 0, text, (int)length, out + at, (int)(n - at));
        } else {
            memcpy(out + at, text, length * sizeof(WCHAR));
            at += length;
        }
        if (!part)
            out[at++] = L'\\';
    }
    out[at] = 0;
    return out;
}

/* domain\user of an identity in any of the three layouts, NULL when it names no user. */
static PWSTR identity_name(const void *data, bool ansi_call)
{
    ULONG version = *(const ULONG *)data;
    if (version == SEC_WINNT_AUTH_IDENTITY_VERSION_2) {
        const SEC_WINNT_AUTH_IDENTITY_EX2 *e = data;
        const UCHAR *base = data;
        return join_name(base + e->DomainOffset, e->DomainLength / sizeof(WCHAR), base + e->UserOffset,
                         e->UserLength / sizeof(WCHAR), false);
    }
    if (version == SEC_WINNT_AUTH_IDENTITY_VERSION) {
        const SEC_WINNT_AUTH_IDENTITY_EXW *e = data;
        bool ansi = e->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI;
        return join_name(e->Domain, e->DomainLength, e->User, e->UserLength, ansi);
    }
    const SEC_WINNT_AUTH_IDENTITY_W *w = data;
    bool ansi = (w->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI) || (ansi_call && !(w->Flags & SEC_WINNT_AUTH_IDENTITY_UNICODE));
    return join_name(w->Domain, w->DomainLength, w->User, w->UserLength, ansi);
}

static SECURITY_STATUS acquire(const SEC_PACKAGE *package, ULONG use, void *auth_data, SEC_GET_KEY_FN get_key,
                               PCredHandle credential, PTimeStamp expiry, bool ansi)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    if (package->Kind == SEC_KIND_SCHANNEL) {
        /* SpUniAcquireCredentialsHandle wants an inbound or outbound use, SpCommonAcquireCredentialsHandle
           a SCHANNEL_CRED, SCH_CREDENTIALS or identity structure version. */
        if (!(use & SECPKG_CRED_BOTH))
            return SEC_E_NO_CREDENTIALS;
        if (auth_data) {
            ULONG version = *(const ULONG *)auth_data;
            if (version != SCH_CRED_V3 && version != SCHANNEL_CRED_VERSION && version != SCH_CREDENTIALS_VERSION &&
                version != SEC_WINNT_AUTH_IDENTITY_VERSION)
                return SEC_E_UNKNOWN_CREDENTIALS;
        }
    } else {
        /* msv1_0 SpAcquireCredentialsHandle refuses a credential without a direction and key callbacks. */
        if (!(use & SECPKG_CRED_BOTH) || get_key)
            return SEC_E_UNSUPPORTED_FUNCTION;
    }
    if (!credential)
        return SEC_E_INVALID_PARAMETER;

    SEC_CREDENTIAL *c = SecHeapAlloc(sizeof(*c));
    if (!c)
        return SEC_E_INSUFFICIENT_MEMORY;
    c->Package = package;
    c->Use = use;
    c->Name = auth_data && takes_identity(package) ? identity_name(auth_data, ansi) : NULL;
    RtlEnterCriticalSection(&g_lock);
    c->Next = g_credentials;
    g_credentials = c;
    RtlLeaveCriticalSection(&g_lock);

    credential->dwLower = (ULONG_PTR)package;
    credential->dwUpper = (ULONG_PTR)c;
    if (expiry)
        expiry->QuadPart = SEC_NEVER_EXPIRES;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AcquireCredentialsHandleW(SEC_WCHAR *principal, SEC_WCHAR *package, ULONG use,
                                                                void *logon_id, void *auth_data, SEC_GET_KEY_FN get_key,
                                                                void *get_key_argument, PCredHandle credential,
                                                                PTimeStamp expiry)
{
    return acquire(SecFindPackageW(package), use, auth_data, get_key, credential, expiry, false);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AcquireCredentialsHandleA(SEC_CHAR *principal, SEC_CHAR *package, ULONG use,
                                                                void *logon_id, void *auth_data, SEC_GET_KEY_FN get_key,
                                                                void *get_key_argument, PCredHandle credential,
                                                                PTimeStamp expiry)
{
    return acquire(SecFindPackageA(package), use, auth_data, get_key, credential, expiry, true);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY FreeCredentialsHandle(PCredHandle credential)
{
    RtlEnterCriticalSection(&g_lock);
    SEC_CREDENTIAL *c = find(credential);
    if (c) {
        SEC_CREDENTIAL **link = &g_credentials;
        while (*link != c)
            link = &(*link)->Next;
        *link = c->Next;
    }
    RtlLeaveCriticalSection(&g_lock);
    if (!c)
        return SEC_E_INVALID_HANDLE;
    SecHeapFree(c->Name);
    SecHeapFree(c);
    return SEC_E_OK;
}

/* the user name of SECPKG_CRED_ATTR_NAMES, in a heap copy. */
static SECURITY_STATUS credential_name(PCredHandle credential, PWSTR *name)
{
    *name = NULL;
    RtlEnterCriticalSection(&g_lock);
    SEC_CREDENTIAL *c = find(credential);
    bool names = c && takes_identity(c->Package);
    if (c && c->Name) {
        SIZE_T bytes = (wcslen(c->Name) + 1) * sizeof(WCHAR);
        *name = SecHeapAlloc(bytes);
        if (*name)
            memcpy(*name, c->Name, bytes);
    }
    bool own = c && c->Name;
    RtlLeaveCriticalSection(&g_lock);
    if (!c)
        return SEC_E_INVALID_HANDLE;
    if (!names)
        return SEC_E_UNSUPPORTED_FUNCTION;
    if (own)
        return *name ? SEC_E_OK : SEC_E_INSUFFICIENT_MEMORY;
    WCHAR text[256];
    DWORD n;
    if (!NT_SUCCESS(SecSamCompatibleName(text, RTL_NUMBER_OF(text), &n)))
        return SEC_E_INTERNAL_ERROR;
    *name = SecHeapAlloc((n + 1) * sizeof(WCHAR));
    if (!*name)
        return SEC_E_INSUFFICIENT_MEMORY;
    memcpy(*name, text, (n + 1) * sizeof(WCHAR));
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryCredentialsAttributesW(PCredHandle credential, ULONG attribute, void *buffer)
{
    const SEC_PACKAGE *package;
    ULONG use;
    if (!SecLookupCredential(credential, &package, &use))
        return SEC_E_INVALID_HANDLE;
    if (attribute != SECPKG_CRED_ATTR_NAMES)
        return SEC_E_UNSUPPORTED_FUNCTION;
    if (!buffer)
        return SEC_E_INVALID_PARAMETER;
    PWSTR name;
    SECURITY_STATUS status = credential_name(credential, &name);
    if (status != SEC_E_OK)
        return status;
    SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
    PWSTR out = LocalAlloc(LMEM_FIXED, bytes);
    if (out)
        memcpy(out, name, bytes);
    SecHeapFree(name);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    ((SecPkgCredentials_NamesW *)buffer)->sUserName = out;
    return SEC_E_OK;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryCredentialsAttributesA(PCredHandle credential, ULONG attribute, void *buffer)
{
    const SEC_PACKAGE *package;
    ULONG use;
    if (!SecLookupCredential(credential, &package, &use))
        return SEC_E_INVALID_HANDLE;
    if (attribute != SECPKG_CRED_ATTR_NAMES)
        return SEC_E_UNSUPPORTED_FUNCTION;
    if (!buffer)
        return SEC_E_INVALID_PARAMETER;
    PWSTR name;
    SECURITY_STATUS status = credential_name(credential, &name);
    if (status != SEC_E_OK)
        return status;
    int n = WideCharToMultiByte(CP_ACP, 0, name, -1, NULL, 0, NULL, NULL);
    PSTR out = n > 0 ? LocalAlloc(LMEM_FIXED, n) : NULL;
    if (out)
        WideCharToMultiByte(CP_ACP, 0, name, -1, out, n, NULL, NULL);
    SecHeapFree(name);
    if (!out)
        return SEC_E_INSUFFICIENT_MEMORY;
    ((SecPkgCredentials_NamesA *)buffer)->sUserName = out;
    return SEC_E_OK;
}

/* no package of mime accepts credential attributes or extra credentials. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SetCredentialsAttributesW(PCredHandle credential, ULONG attribute, void *buffer,
                                                                ULONG size)
{
    const SEC_PACKAGE *package;
    ULONG use;
    if (!SecLookupCredential(credential, &package, &use))
        return SEC_E_INVALID_HANDLE;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SetCredentialsAttributesA(PCredHandle credential, ULONG attribute, void *buffer,
                                                                ULONG size)
{
    return SetCredentialsAttributesW(credential, attribute, buffer, size);
}

static SECURITY_STATUS add_credentials(PCredHandle credential, const SEC_PACKAGE *package)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    const SEC_PACKAGE *owner;
    ULONG use;
    if (!SecLookupCredential(credential, &owner, &use))
        return SEC_E_INVALID_HANDLE;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AddCredentialsW(PCredHandle credential, SEC_WCHAR *principal, SEC_WCHAR *package,
                                                      ULONG use, void *auth_data, SEC_GET_KEY_FN get_key,
                                                      void *get_key_argument, PTimeStamp expiry)
{
    return add_credentials(credential, SecFindPackageW(package));
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AddCredentialsA(PCredHandle credential, SEC_CHAR *principal, SEC_CHAR *package,
                                                      ULONG use, void *auth_data, SEC_GET_KEY_FN get_key,
                                                      void *get_key_argument, PTimeStamp expiry)
{
    return add_credentials(credential, SecFindPackageA(package));
}
