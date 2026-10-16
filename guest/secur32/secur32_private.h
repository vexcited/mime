/*
 * secur32 internal interface shared by its source files.
 */
#ifndef MIME_SECUR32_PRIVATE_H
#define MIME_SECUR32_PRIVATE_H

#define MIME_BUILD_SECUR32
#include <mime/win32/sspi.h>
#include <mime/win32/winnls.h>

#ifndef ERROR_CANT_ACCESS_DOMAIN_INFO
#define ERROR_CANT_ACCESS_DOMAIN_INFO 1351L
#endif
#ifndef ERROR_NO_SUCH_DOMAIN
#define ERROR_NO_SUCH_DOMAIN 1355L
#endif
#ifndef STATUS_NONE_MAPPED
#define STATUS_NONE_MAPPED ((NTSTATUS)0xC0000073L)
#endif

/* what every attempt to establish a security context answers, see docs/secur32.md. */
#define SEC_ESTABLISH_FAILURE SEC_E_NO_AUTHENTICATING_AUTHORITY

/* the expiry windows packages report for credentials that never expire. */
#define SEC_NEVER_EXPIRES 0x7FFFFFFFFFFFFFFFLL

/* ------------------------------------------------------------------ packages */

enum {
    SEC_KIND_NEGOTIATE,
    SEC_KIND_EXTENDER,
    SEC_KIND_KERBEROS,
    SEC_KIND_NTLM,
    SEC_KIND_TSSSP,
    SEC_KIND_PKU2U,
    SEC_KIND_CLOUDAP,
    SEC_KIND_DIGEST,
    SEC_KIND_SCHANNEL,
    SEC_KIND_CREDSSP,
};

typedef struct SEC_PACKAGE {
    PCWSTR Name;
    PCWSTR Comment;
    ULONG Capabilities;
    USHORT Version;
    USHORT RpcId;
    ULONG MaxToken;
    ULONG Kind;
} SEC_PACKAGE;

extern const SEC_PACKAGE SecPackages[];
extern const ULONG SecPackageCount;
/* packages hosted by lsa, before the in process ones. */
extern const ULONG SecLsaPackageCount;

const SEC_PACKAGE *SecFindPackageW(PCWSTR name);
const SEC_PACKAGE *SecFindPackageA(PCSTR name);
SECURITY_STATUS SecCopyPackageInfoW(const SEC_PACKAGE *package, PSecPkgInfoW *info);
SECURITY_STATUS SecCopyPackageInfoA(const SEC_PACKAGE *package, PSecPkgInfoA *info);

/* ------------------------------------------------------------------ handles */

/* the package and use of a live credential handle, false for any other handle. */
bool SecLookupCredential(PCredHandle handle, const SEC_PACKAGE **package, ULONG *use);
/* no security context is ever established, so no context handle exists. */
bool SecContextExists(PCtxtHandle handle);

/* ------------------------------------------------------------------ helpers */

FORCEINLINE PVOID SecHeapAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, size);
}

FORCEINLINE void SecHeapFree(PVOID p)
{
    if (p)
        RtlFreeHeap(NtCurrentTeb()->ProcessEnvironmentBlock->ProcessHeap, 0, p);
}

FORCEINLINE void SecSetError(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* the tls slot of the per thread call data of the Seci functions. */
extern DWORD SecTlsCallData;

/* the ansi code page conversions, allocated from the process heap. */
PWSTR SecAnsiToWide(PCSTR text);
PSTR SecWideToAnsi(PCWSTR text);

/* the security status of an nt status, as sspicli maps them. */
SECURITY_STATUS SecNtStatusToSecStatus(NTSTATUS status);

/* the computer name and the logged on user: user is empty for the system account. */
DWORD SecComputerName(PWSTR out, DWORD capacity);
bool SecUserName(PWSTR out, DWORD capacity, bool *system);

/* names.c */
NTSTATUS SecSamCompatibleName(PWSTR out, DWORD capacity, DWORD *length);

#endif
