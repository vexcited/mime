/*
 * security contexts.
 *
 * the schannel package establishes real contexts through its tls client,
 * see schannel.c. mime has no kerberos, ntlm or digest implementation, so
 * the other packages never establish one: the first call of
 * InitializeSecurityContext or AcceptSecurityContext with a valid
 * credential fails with SEC_ESTABLISH_FAILURE. a handle that names no live
 * context is rejected with SEC_E_INVALID_HANDLE, as sspicli's
 * SecpValidateHandle does for handles it does not know.
 */
#include "schannel.h"

/*
 * a live context: the lower half of its handle points to the package and
 * the upper half to this record, which stays alive while a call holds a
 * reference to it.
 */
typedef struct SEC_CONTEXT {
    struct SEC_CONTEXT *Next;
    const SEC_PACKAGE *Package;
    TLS *Tls;
    LONG Refs;
    RTL_CRITICAL_SECTION Lock;
} SEC_CONTEXT;

static RTL_CRITICAL_SECTION g_lock = RTL_CRITICAL_SECTION_INIT;
static SEC_CONTEXT *g_contexts;

/* the context of a handle; the global lock must be held. */
static SEC_CONTEXT *find(PCtxtHandle handle)
{
    if (!handle)
        return nullptr;
    for (SEC_CONTEXT *c = g_contexts; c; c = c->Next)
        if ((ULONG_PTR)c == handle->dwUpper && (ULONG_PTR)c->Package == handle->dwLower)
            return c;
    return nullptr;
}

static void release(SEC_CONTEXT *c)
{
    if (InterlockedDecrement(&c->Refs))
        return;
    TlsDestroy(c->Tls);
    RtlDeleteCriticalSection(&c->Lock);
    SecHeapFree(c);
}

/* the context with a reference taken, or NULL. */
static SEC_CONTEXT *reference(PCtxtHandle handle)
{
    RtlEnterCriticalSection(&g_lock);
    SEC_CONTEXT *c = find(handle);
    if (c)
        InterlockedIncrement(&c->Refs);
    RtlLeaveCriticalSection(&g_lock);
    return c;
}

bool SecNewSchannelContext(const SEC_PACKAGE *package, TLS *t, PCtxtHandle handle)
{
    SEC_CONTEXT *c = SecHeapAlloc(sizeof(*c));
    if (!c)
        return false;
    c->Package = package;
    c->Tls = t;
    c->Refs = 1;
    RtlInitializeCriticalSection(&c->Lock);
    RtlEnterCriticalSection(&g_lock);
    c->Next = g_contexts;
    g_contexts = c;
    RtlLeaveCriticalSection(&g_lock);
    handle->dwLower = (ULONG_PTR)package;
    handle->dwUpper = (ULONG_PTR)c;
    return true;
}

TLS *SecAcquireSchannelContext(PCtxtHandle handle)
{
    SEC_CONTEXT *c = reference(handle);
    if (!c)
        return nullptr;
    RtlEnterCriticalSection(&c->Lock);
    return c->Tls;
}

void SecReleaseSchannelContext(PCtxtHandle handle)
{
    SEC_CONTEXT *c = (SEC_CONTEXT *)handle->dwUpper;
    RtlLeaveCriticalSection(&c->Lock);
    release(c);
}

bool SecContextExists(PCtxtHandle handle)
{
    RtlEnterCriticalSection(&g_lock);
    bool found = find(handle) != nullptr;
    RtlLeaveCriticalSection(&g_lock);
    return found;
}

static SECURITY_STATUS establish(PCredHandle credential, PCtxtHandle context, ULONG direction, PCWSTR target,
                                 ULONG flags, PSecBufferDesc input, PCtxtHandle new_context, PSecBufferDesc output,
                                 ULONG *attributes, PTimeStamp expiry)
{
    /* InitializeSecurityContextCommon and AcceptSecurityContext refuse two missing handles first. */
    if (!credential && !context)
        return SEC_E_INVALID_HANDLE;
    if (context) {
        if (!SecContextExists(context))
            return SEC_E_INVALID_HANDLE;
        if (direction != SECPKG_CRED_OUTBOUND)
            return SEC_E_UNSUPPORTED_FUNCTION;
        return SchInitialize(credential, context, target, flags, input, new_context, output, attributes, expiry);
    }
    const SEC_PACKAGE *package;
    ULONG use;
    if (!SecLookupCredential(credential, &package, &use))
        return SEC_E_INVALID_HANDLE;
    if (!(use & direction))
        return SEC_E_NO_CREDENTIALS;
    /* the schannel client is the only package with an implementation; servers need certificates mime cannot hold. */
    if (package->Kind == SEC_KIND_SCHANNEL && direction == SECPKG_CRED_OUTBOUND)
        return SchInitialize(credential, nullptr, target, flags, input, new_context, output, attributes, expiry);
    return SEC_ESTABLISH_FAILURE;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextW(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_WCHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry)
{
    return establish(credential, context, SECPKG_CRED_OUTBOUND, target, flags, input, new_context, output,
                     attributes, expiry);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextA(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_CHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry)
{
    PWSTR wide = nullptr;
    if (target && !(wide = SecAnsiToWide(target)))
        return SEC_E_INSUFFICIENT_MEMORY;
    SECURITY_STATUS status = establish(credential, context, SECPKG_CRED_OUTBOUND, wide, flags, input, new_context,
                                       output, attributes, expiry);
    SecHeapFree(wide);
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AcceptSecurityContext(PCredHandle credential, PCtxtHandle context,
                                                            PSecBufferDesc input, ULONG flags, ULONG representation,
                                                            PCtxtHandle new_context, PSecBufferDesc output,
                                                            ULONG *attributes, PTimeStamp expiry)
{
    return establish(credential, context, SECPKG_CRED_INBOUND, nullptr, flags, input, new_context, output, attributes,
                     expiry);
}

/*
 * sspicli returns success for a handle whose lower half is at most the
 * number of lsa packages, which includes a zeroed handle, before looking
 * the handle up.
 */
SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityContext(PCtxtHandle context)
{
    if (context && context->dwLower <= SecLsaPackageCount)
        return SEC_E_OK;
    RtlEnterCriticalSection(&g_lock);
    SEC_CONTEXT *c = find(context);
    if (c) {
        SEC_CONTEXT **link = &g_contexts;
        while (*link != c)
            link = &(*link)->Next;
        *link = c->Next;
    }
    RtlLeaveCriticalSection(&g_lock);
    if (!c)
        return SEC_E_INVALID_HANDLE;
    release(c);
    return SEC_E_OK;
}

static SECURITY_STATUS no_context(PCtxtHandle context)
{
    return SecContextExists(context) ? SEC_E_UNSUPPORTED_FUNCTION : SEC_E_INVALID_HANDLE;
}

static SECURITY_STATUS query(PCtxtHandle context, ULONG attribute, void *buffer, bool ansi)
{
    SEC_CONTEXT *c = reference(context);
    if (!c)
        return SEC_E_INVALID_HANDLE;
    SECURITY_STATUS status;
    if (attribute == SECPKG_ATTR_PACKAGE_INFO) {
        PSecPkgInfoW *info = buffer;
        status = !buffer ? SEC_E_INVALID_PARAMETER
                 : ansi  ? SecCopyPackageInfoA(c->Package, (PSecPkgInfoA *)info)
                         : SecCopyPackageInfoW(c->Package, info);
    } else {
        RtlEnterCriticalSection(&c->Lock);
        status = SchQueryAttributes(c->Tls, attribute, buffer, ansi);
        RtlLeaveCriticalSection(&c->Lock);
    }
    release(c);
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesW(PCtxtHandle context, ULONG attribute, void *buffer)
{
    return query(context, attribute, buffer, false);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesA(PCtxtHandle context, ULONG attribute, void *buffer)
{
    return query(context, attribute, buffer, true);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SetContextAttributesW(PCtxtHandle context, ULONG attribute, void *buffer,
                                                            ULONG size)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY SetContextAttributesA(PCtxtHandle context, ULONG attribute, void *buffer,
                                                            ULONG size)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY CompleteAuthToken(PCtxtHandle context, PSecBufferDesc token)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ApplyControlToken(PCtxtHandle context, PSecBufferDesc input)
{
    TLS *t = SecAcquireSchannelContext(context);
    if (!t)
        return SEC_E_INVALID_HANDLE;
    SECURITY_STATUS status = SchApplyControlToken(t, input);
    SecReleaseSchannelContext(context);
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ImpersonateSecurityContext(PCtxtHandle context)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY RevertSecurityContext(PCtxtHandle context)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY MakeSignature(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                    ULONG sequence)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY VerifySignature(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                      ULONG *qop)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY EncryptMessage(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                     ULONG sequence)
{
    TLS *t = SecAcquireSchannelContext(context);
    if (!t)
        return SEC_E_INVALID_HANDLE;
    SECURITY_STATUS status = SchEncrypt(t, message);
    SecReleaseSchannelContext(context);
    return status;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY DecryptMessage(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                     ULONG *qop)
{
    TLS *t = SecAcquireSchannelContext(context);
    if (!t)
        return SEC_E_INVALID_HANDLE;
    if (qop)
        *qop = 0;
    SECURITY_STATUS status = SchDecrypt(t, message);
    SecReleaseSchannelContext(context);
    return status;
}

/* the names of EncryptMessage and DecryptMessage before windows 2000; sspicli exports the same code. */
SECURITYAPI SECURITY_STATUS SEC_ENTRY SealMessage(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                  ULONG sequence)
{
    return EncryptMessage(context, qop, message, sequence);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY UnsealMessage(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                    ULONG *qop)
{
    return DecryptMessage(context, message, sequence, qop);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityContextToken(PCtxtHandle context, void **token)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ExportSecurityContext(PCtxtHandle context, ULONG flags, PSecBuffer packed,
                                                            void **token)
{
    return no_context(context);
}

/* no packed context can come from mime, which never exports one. */
static SECURITY_STATUS import(const SEC_PACKAGE *package, PCtxtHandle context)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ImportSecurityContextW(SEC_WCHAR *package, PSecBuffer packed, void *token,
                                                             PCtxtHandle context)
{
    return import(SecFindPackageW(package), context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ImportSecurityContextA(SEC_CHAR *package, PSecBuffer packed, void *token,
                                                             PCtxtHandle context)
{
    return import(SecFindPackageA(package), context);
}

/* a password change is an exchange with the account's authority, like establishing a context. */
static SECURITY_STATUS change_password(const SEC_PACKAGE *package)
{
    if (!package)
        return SEC_E_SECPKG_NOT_FOUND;
    switch (package->Kind) {
    case SEC_KIND_NEGOTIATE:
    case SEC_KIND_KERBEROS:
    case SEC_KIND_NTLM:
        return SEC_ESTABLISH_FAILURE;
    default:
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ChangeAccountPasswordW(SEC_WCHAR *package, SEC_WCHAR *domain, SEC_WCHAR *account,
                                                             SEC_WCHAR *old_password, SEC_WCHAR *new_password,
                                                             BOOLEAN impersonating, ULONG reserved,
                                                             PSecBufferDesc output)
{
    return change_password(SecFindPackageW(package));
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY ChangeAccountPasswordA(SEC_CHAR *package, SEC_CHAR *domain, SEC_CHAR *account,
                                                             SEC_CHAR *old_password, SEC_CHAR *new_password,
                                                             BOOLEAN impersonating, ULONG reserved,
                                                             PSecBufferDesc output)
{
    return change_password(SecFindPackageA(package));
}
