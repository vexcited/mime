/*
 * security contexts.
 *
 * mime has no network stack and no kerberos, ntlm, digest or tls
 * implementation, so a context is never established: the first call of
 * InitializeSecurityContext or AcceptSecurityContext with a valid
 * credential fails with SEC_ESTABLISH_FAILURE, and since no context handle
 * ever exists, every function taking one rejects it with
 * SEC_E_INVALID_HANDLE, as sspicli's SecpValidateHandle does for handles it
 * does not know.
 */
#include "secur32_private.h"

bool SecContextExists(PCtxtHandle handle)
{
    return false;
}

static SECURITY_STATUS establish(PCredHandle credential, PCtxtHandle context, ULONG direction)
{
    /* InitializeSecurityContextCommon and AcceptSecurityContext refuse two missing handles first. */
    if (!credential && !context)
        return SEC_E_INVALID_HANDLE;
    if (context && !SecContextExists(context))
        return SEC_E_INVALID_HANDLE;
    const SEC_PACKAGE *package;
    ULONG use;
    if (!credential || !SecLookupCredential(credential, &package, &use))
        return SEC_E_INVALID_HANDLE;
    if (!(use & direction))
        return SEC_E_NO_CREDENTIALS;
    return SEC_ESTABLISH_FAILURE;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextW(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_WCHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry)
{
    return establish(credential, context, SECPKG_CRED_OUTBOUND);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextA(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_CHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry)
{
    return establish(credential, context, SECPKG_CRED_OUTBOUND);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY AcceptSecurityContext(PCredHandle credential, PCtxtHandle context,
                                                            PSecBufferDesc input, ULONG flags, ULONG representation,
                                                            PCtxtHandle new_context, PSecBufferDesc output,
                                                            ULONG *attributes, PTimeStamp expiry)
{
    return establish(credential, context, SECPKG_CRED_INBOUND);
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
    return SEC_E_INVALID_HANDLE;
}

static SECURITY_STATUS no_context(PCtxtHandle context)
{
    return SecContextExists(context) ? SEC_E_UNSUPPORTED_FUNCTION : SEC_E_INVALID_HANDLE;
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesW(PCtxtHandle context, ULONG attribute, void *buffer)
{
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesA(PCtxtHandle context, ULONG attribute, void *buffer)
{
    return no_context(context);
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
    return no_context(context);
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
    return no_context(context);
}

SECURITYAPI SECURITY_STATUS SEC_ENTRY DecryptMessage(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                     ULONG *qop)
{
    return no_context(context);
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

/* no packed context can come from mime, which never establishes one. */
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
