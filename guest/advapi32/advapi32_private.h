/*
 * advapi32 internal interface: the security types of winnt.h and the
 * helpers shared by its source files.
 */
#ifndef MIME_ADVAPI32_PRIVATE_H
#define MIME_ADVAPI32_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

#define ADVAPI DECLSPEC_EXPORT

/* ---------------------------------------------------------------- security identifiers */

typedef struct _SID_IDENTIFIER_AUTHORITY {
    BYTE Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

typedef struct _SID {
    BYTE Revision;
    BYTE SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    DWORD SubAuthority[1];
} SID;

#define SID_REVISION 1
#define SID_MAX_SUB_AUTHORITIES 15
#define SECURITY_MAX_SID_SIZE (8 + 4 * SID_MAX_SUB_AUTHORITIES)

typedef enum _SID_NAME_USE {
    SidTypeUser = 1,
    SidTypeGroup,
    SidTypeDomain,
    SidTypeAlias,
    SidTypeWellKnownGroup,
    SidTypeDeletedAccount,
    SidTypeInvalid,
    SidTypeUnknown,
    SidTypeComputer,
    SidTypeLabel,
    SidTypeLogonSession,
} SID_NAME_USE, *PSID_NAME_USE;

typedef struct _SID_AND_ATTRIBUTES {
    PSID Sid;
    DWORD Attributes;
} SID_AND_ATTRIBUTES, *PSID_AND_ATTRIBUTES;

/* ---------------------------------------------------------------- tokens */

typedef struct _LUID {
    DWORD LowPart;
    LONG HighPart;
} LUID, *PLUID;

typedef struct _LUID_AND_ATTRIBUTES {
    LUID Luid;
    DWORD Attributes;
} LUID_AND_ATTRIBUTES, *PLUID_AND_ATTRIBUTES;

typedef struct _TOKEN_USER {
    SID_AND_ATTRIBUTES User;
} TOKEN_USER, *PTOKEN_USER;

typedef struct _TOKEN_GROUPS {
    DWORD GroupCount;
    SID_AND_ATTRIBUTES Groups[1];
} TOKEN_GROUPS, *PTOKEN_GROUPS;

typedef struct _TOKEN_PRIVILEGES {
    DWORD PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;

typedef enum _TOKEN_INFORMATION_CLASS {
    TokenUser = 1,
    TokenGroups,
    TokenPrivileges,
    TokenOwner,
    TokenPrimaryGroup,
    TokenDefaultDacl,
    TokenSource,
    TokenType,
    TokenImpersonationLevel,
    TokenStatistics,
} TOKEN_INFORMATION_CLASS;

typedef enum _SECURITY_IMPERSONATION_LEVEL {
    SecurityAnonymous,
    SecurityIdentification,
    SecurityImpersonation,
    SecurityDelegation,
} SECURITY_IMPERSONATION_LEVEL;

typedef enum _TOKEN_TYPE {
    TokenPrimary = 1,
    TokenImpersonation,
} TOKEN_TYPE;

#define TOKEN_ASSIGN_PRIMARY 0x0001
#define TOKEN_DUPLICATE 0x0002
#define TOKEN_IMPERSONATE 0x0004
#define TOKEN_QUERY 0x0008
#define TOKEN_ADJUST_PRIVILEGES 0x0020

#define SE_GROUP_MANDATORY 0x00000001
#define SE_GROUP_ENABLED_BY_DEFAULT 0x00000002
#define SE_GROUP_ENABLED 0x00000004
#define SE_GROUP_OWNER 0x00000008
#define SE_GROUP_USE_FOR_DENY_ONLY 0x00000010
#define SE_GROUP_INTEGRITY 0x00000020
#define SE_GROUP_LOGON_ID 0xC0000000

#define SE_PRIVILEGE_ENABLED_BY_DEFAULT 0x00000001
#define SE_PRIVILEGE_ENABLED 0x00000002
#define SE_PRIVILEGE_REMOVED 0x00000004

/* the pseudo handles of GetCurrentProcessToken and friends. */
#define ADV_CURRENT_PROCESS_TOKEN ((HANDLE)(LONG_PTR)-4)
#define ADV_CURRENT_THREAD_TOKEN ((HANDLE)(LONG_PTR)-5)
#define ADV_CURRENT_THREAD_EFFECTIVE_TOKEN ((HANDLE)(LONG_PTR)-6)

/* ---------------------------------------------------------------- access control lists */

typedef struct _ACL {
    BYTE AclRevision;
    BYTE Sbz1;
    WORD AclSize;
    WORD AceCount;
    WORD Sbz2;
} ACL, *PACL;

typedef struct _ACE_HEADER {
    BYTE AceType;
    BYTE AceFlags;
    WORD AceSize;
} ACE_HEADER, *PACE_HEADER;

/* the layout shared by the allowed, denied, audit and label aces. */
typedef struct _ACCESS_ALLOWED_ACE {
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    DWORD SidStart;
} ACCESS_ALLOWED_ACE, *PACCESS_ALLOWED_ACE;

#define ACL_REVISION 2
#define ACL_REVISION_DS 4
#define ACCESS_ALLOWED_ACE_TYPE 0x0
#define ACCESS_DENIED_ACE_TYPE 0x1
#define SYSTEM_AUDIT_ACE_TYPE 0x2
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#define NO_PROPAGATE_INHERIT_ACE 0x04
#define SUCCESSFUL_ACCESS_ACE_FLAG 0x40
#define FAILED_ACCESS_ACE_FLAG 0x80
#define OBJECT_INHERIT_ACE 0x01
#define CONTAINER_INHERIT_ACE 0x02
#define INHERIT_ONLY_ACE 0x08
#define INHERITED_ACE 0x10

typedef enum _ACL_INFORMATION_CLASS {
    AclRevisionInformation = 1,
    AclSizeInformation,
} ACL_INFORMATION_CLASS;

typedef struct _ACL_SIZE_INFORMATION {
    DWORD AceCount;
    DWORD AclBytesInUse;
    DWORD AclBytesFree;
} ACL_SIZE_INFORMATION;

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead;
    ACCESS_MASK GenericWrite;
    ACCESS_MASK GenericExecute;
    ACCESS_MASK GenericAll;
} GENERIC_MAPPING, *PGENERIC_MAPPING;

typedef struct _PRIVILEGE_SET {
    DWORD PrivilegeCount;
    DWORD Control;
    LUID_AND_ATTRIBUTES Privilege[1];
} PRIVILEGE_SET, *PPRIVILEGE_SET;

/* ---------------------------------------------------------------- security descriptors */

typedef WORD SECURITY_DESCRIPTOR_CONTROL, *PSECURITY_DESCRIPTOR_CONTROL;
typedef DWORD SECURITY_INFORMATION;

typedef struct _SECURITY_DESCRIPTOR {
    BYTE Revision;
    BYTE Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID Owner;
    PSID Group;
    PACL Sacl;
    PACL Dacl;
} SECURITY_DESCRIPTOR;

typedef struct _SECURITY_DESCRIPTOR_RELATIVE {
    BYTE Revision;
    BYTE Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    DWORD Owner;
    DWORD Group;
    DWORD Sacl;
    DWORD Dacl;
} SECURITY_DESCRIPTOR_RELATIVE;

#define SECURITY_DESCRIPTOR_REVISION 1
#define SE_OWNER_DEFAULTED 0x0001
#define SE_GROUP_DEFAULTED 0x0002
#define SE_DACL_PRESENT 0x0004
#define SE_DACL_DEFAULTED 0x0008
#define SE_SACL_PRESENT 0x0010
#define SE_SACL_DEFAULTED 0x0020
#define SE_DACL_AUTO_INHERIT_REQ 0x0100
#define SE_SACL_AUTO_INHERIT_REQ 0x0200
#define SE_DACL_AUTO_INHERITED 0x0400
#define SE_SACL_AUTO_INHERITED 0x0800
#define SE_DACL_PROTECTED 0x1000
#define SE_SACL_PROTECTED 0x2000
#define SE_SELF_RELATIVE 0x8000

#define OWNER_SECURITY_INFORMATION 0x00000001
#define GROUP_SECURITY_INFORMATION 0x00000002
#define DACL_SECURITY_INFORMATION 0x00000004
#define SACL_SECURITY_INFORMATION 0x00000008
#define LABEL_SECURITY_INFORMATION 0x00000010

typedef enum _SE_OBJECT_TYPE {
    SE_UNKNOWN_OBJECT_TYPE,
    SE_FILE_OBJECT,
    SE_SERVICE,
    SE_PRINTER,
    SE_REGISTRY_KEY,
    SE_LMSHARE,
    SE_KERNEL_OBJECT,
    SE_WINDOW_OBJECT,
    SE_DS_OBJECT,
    SE_DS_OBJECT_ALL,
    SE_PROVIDER_DEFINED_OBJECT,
    SE_WMIGUID_OBJECT,
    SE_REGISTRY_WOW64_32KEY,
    SE_REGISTRY_WOW64_64KEY,
} SE_OBJECT_TYPE;

/* ---------------------------------------------------------------- errors missing from winerror.h */

#ifndef ERROR_NONE_MAPPED
#define ERROR_NONE_MAPPED 1332L
#endif
#ifndef ERROR_INVALID_SID
#define ERROR_INVALID_SID 1337L
#endif
#ifndef ERROR_INVALID_ACL
#define ERROR_INVALID_ACL 1336L
#endif
#ifndef ERROR_INVALID_SECURITY_DESCR
#define ERROR_INVALID_SECURITY_DESCR 1338L
#endif
#ifndef ERROR_NO_SUCH_PRIVILEGE
#define ERROR_NO_SUCH_PRIVILEGE 1313L
#endif
#ifndef ERROR_PRIVILEGE_NOT_HELD
#define ERROR_PRIVILEGE_NOT_HELD 1314L
#endif
#ifndef ERROR_NOT_ALL_ASSIGNED
#define ERROR_NOT_ALL_ASSIGNED 1300L
#endif
#ifndef ERROR_NO_TOKEN
#define ERROR_NO_TOKEN 1008L
#endif
#ifndef ERROR_ALLOTTED_SPACE_EXCEEDED
#define ERROR_ALLOTTED_SPACE_EXCEEDED 1344L
#endif
#ifndef ERROR_UNKNOWN_REVISION
#define ERROR_UNKNOWN_REVISION 1305L
#endif
#ifndef ERROR_GENERIC_NOT_MAPPED
#define ERROR_GENERIC_NOT_MAPPED 1360L
#endif
#ifndef ERROR_REGISTRY_CORRUPT
#define ERROR_REGISTRY_CORRUPT 1015L
#endif
#ifndef ERROR_INTERNAL_ERROR
#define ERROR_INTERNAL_ERROR 1359L
#endif
#ifndef ERROR_SERVICE_DOES_NOT_EXIST
#define ERROR_SERVICE_DOES_NOT_EXIST 1060L
#endif
#ifndef RPC_S_SERVER_UNAVAILABLE
#define RPC_S_SERVER_UNAVAILABLE 1722L
#endif
#ifndef ERROR_INVALID_FLAGS
#define ERROR_INVALID_FLAGS 1004L
#endif
#ifndef ERROR_NO_IMPERSONATION_TOKEN
#define ERROR_NO_IMPERSONATION_TOKEN 1309L
#endif

/* ---------------------------------------------------------------- native services */

NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateToken(HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                             BOOLEAN effective_only, TOKEN_TYPE type, PHANDLE result);
NTSYSCALLAPI NTSTATUS NTAPI NtAccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, ACCESS_MASK desired,
                                          PGENERIC_MAPPING mapping, PPRIVILEGE_SET privileges, PULONG privileges_length,
                                          PACCESS_MASK granted, PNTSTATUS status);
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, SECURITY_INFORMATION info,
                                                  PSECURITY_DESCRIPTOR sd, ULONG length, PULONG needed);
NTSYSCALLAPI NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE token, BOOLEAN disable_all, PTOKEN_PRIVILEGES state,
                                                    ULONG length, PTOKEN_PRIVILEGES previous, PULONG returned);

/* ---------------------------------------------------------------- helpers */

FORCEINLINE void AdvSetError(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* stores the win32 error of a status and returns FALSE. */
FORCEINLINE BOOL AdvSetStatus(NTSTATUS status)
{
    AdvSetError(RtlNtStatusToDosError(status));
    return FALSE;
}

FORCEINLINE PVOID AdvAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
}

FORCEINLINE PVOID AdvAllocZero(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void AdvFree(PVOID p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

/* ansi strings of the A functions, converted with the ansi code page; results come from the process heap. */
PWSTR AdvAnsiToWide(LPCSTR s);
PSTR AdvWideToAnsi(LPCWSTR s);

/* sid.c */
DWORD AdvSidLength(const SID *sid);
BOOL AdvSidValid(const SID *sid);
BOOL AdvSidEqual(const SID *a, const SID *b);
/* builds a sid in storage, which holds SECURITY_MAX_SID_SIZE bytes. */
PSID AdvMakeSid(void *storage, BYTE authority, int count, ...);
/* parses an sddl sid string, an alias or the S-1- form, into storage of SECURITY_MAX_SID_SIZE bytes. */
BOOL AdvParseSidString(PCWSTR string, void *storage);

/* token.c: the user and groups of a token, the process token for NULL; free with AdvFree. */
TOKEN_USER *AdvQueryTokenUser(HANDLE token);
TOKEN_GROUPS *AdvQueryTokenGroups(HANDLE token);
PVOID AdvQueryToken(HANDLE token, DWORD klass);
/* the sid of the user running the process, copied into storage of SECURITY_MAX_SID_SIZE bytes. */
BOOL AdvCurrentUserSid(void *storage);
/* resolves the pseudo token handles to a real handle, opening one into *opened when needed. */
NTSTATUS AdvResolveToken(HANDLE token, ACCESS_MASK access, HANDLE *real, HANDLE *opened);

/* account.c: account names of sids, and whether a server name designates this machine. */
BOOL AdvIsLocalSystem(LPCWSTR system);
BOOL AdvLookupSid(const SID *sid, PWSTR name, DWORD name_size, PWSTR domain, DWORD domain_size, SID_NAME_USE *use);

/* exports used across files. */
ADVAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *result);
ADVAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR string, PSID *result);
ADVAPI BOOL WINAPI LookupAccountNameW(LPCWSTR system, LPCWSTR account, PSID sid, LPDWORD sid_size, LPWSTR domain,
                                      LPDWORD domain_size, PSID_NAME_USE use);

/* random.c: fills a buffer from the process random generator. */
BOOL AdvGenRandom(PVOID buffer, SIZE_T length);

#endif
