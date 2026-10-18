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
    TokenRestrictedSids,
    TokenSessionId,
    TokenGroupsAndPrivileges,
    TokenSessionReference,
    TokenSandBoxInert,
    TokenAuditPolicy,
    TokenOrigin,
    TokenElevationType,
    TokenLinkedToken,
    TokenElevation,
    TokenHasRestrictions,
    TokenAccessInformation,
    TokenVirtualizationAllowed,
    TokenVirtualizationEnabled,
    TokenIntegrityLevel,
    TokenUIAccess,
    TokenMandatoryPolicy,
    TokenLogonSid,
    TokenIsAppContainer,
    TokenCapabilities,
    TokenAppContainerSid,
    TokenAppContainerNumber,
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
#define TOKEN_QUERY_SOURCE 0x0010
#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define TOKEN_ADJUST_GROUPS 0x0040
#define TOKEN_ADJUST_DEFAULT 0x0080
#define TOKEN_ADJUST_SESSIONID 0x0100
#define TOKEN_ALL_ACCESS 0xF01FF

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
#define SYSTEM_ALARM_ACE_TYPE 0x3
#define ACCESS_ALLOWED_COMPOUND_ACE_TYPE 0x4
#define ACCESS_ALLOWED_OBJECT_ACE_TYPE 0x5
#define ACCESS_DENIED_OBJECT_ACE_TYPE 0x6
#define SYSTEM_AUDIT_OBJECT_ACE_TYPE 0x7
#define SYSTEM_ALARM_OBJECT_ACE_TYPE 0x8
#define ACCESS_ALLOWED_CALLBACK_ACE_TYPE 0x9
#define ACCESS_DENIED_CALLBACK_ACE_TYPE 0xA
#define ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE 0xB
#define ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE 0xC
#define SYSTEM_AUDIT_CALLBACK_ACE_TYPE 0xD
#define SYSTEM_ALARM_CALLBACK_ACE_TYPE 0xE
#define SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE 0xF
#define SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE 0x10
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#define SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE 0x12
#define SYSTEM_SCOPED_POLICY_ID_ACE_TYPE 0x13
#define SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE 0x14
#define SYSTEM_ACCESS_FILTER_ACE_TYPE 0x15
#define NO_PROPAGATE_INHERIT_ACE 0x04
#define CRITICAL_ACE_FLAG 0x20
#define SUCCESSFUL_ACCESS_ACE_FLAG 0x40
#define FAILED_ACCESS_ACE_FLAG 0x80
#define OBJECT_INHERIT_ACE 0x01
#define CONTAINER_INHERIT_ACE 0x02
#define INHERIT_ONLY_ACE 0x08
#define INHERITED_ACE 0x10
#define ACE_OBJECT_TYPE_PRESENT 0x1
#define ACE_INHERITED_OBJECT_TYPE_PRESENT 0x2
#define SYSTEM_MANDATORY_LABEL_NO_WRITE_UP 0x1
#define SYSTEM_MANDATORY_LABEL_NO_READ_UP 0x2
#define SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP 0x4
#define COMPOUND_ACE_IMPERSONATION 1

/* the object aces carry flags and up to two guids before the sid. */
typedef struct _ACCESS_ALLOWED_OBJECT_ACE {
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    DWORD Flags;
    DWORD Guids[1];
} ACCESS_ALLOWED_OBJECT_ACE, *PACCESS_ALLOWED_OBJECT_ACE;

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
#define ATTRIBUTE_SECURITY_INFORMATION 0x00000020
#define SCOPE_SECURITY_INFORMATION 0x00000040
#define PROCESS_TRUST_LABEL_SECURITY_INFORMATION 0x00000080
#define ACCESS_FILTER_SECURITY_INFORMATION 0x00000100
#define BACKUP_SECURITY_INFORMATION 0x00010000L
#define PROTECTED_DACL_SECURITY_INFORMATION 0x80000000L
#define UNPROTECTED_DACL_SECURITY_INFORMATION 0x20000000L
#define PROTECTED_SACL_SECURITY_INFORMATION 0x40000000L
#define UNPROTECTED_SACL_SECURITY_INFORMATION 0x10000000L

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

/* ---------------------------------------------------------------- trustees and explicit access */

typedef enum _MULTIPLE_TRUSTEE_OPERATION {
    NO_MULTIPLE_TRUSTEE,
    TRUSTEE_IS_IMPERSONATE,
} MULTIPLE_TRUSTEE_OPERATION;

typedef enum _TRUSTEE_FORM {
    TRUSTEE_IS_SID,
    TRUSTEE_IS_NAME,
    TRUSTEE_BAD_FORM,
    TRUSTEE_IS_OBJECTS_AND_SID,
    TRUSTEE_IS_OBJECTS_AND_NAME,
} TRUSTEE_FORM;

typedef enum _TRUSTEE_TYPE {
    TRUSTEE_IS_UNKNOWN,
    TRUSTEE_IS_USER,
    TRUSTEE_IS_GROUP,
    TRUSTEE_IS_DOMAIN,
    TRUSTEE_IS_ALIAS,
    TRUSTEE_IS_WELL_KNOWN_GROUP,
    TRUSTEE_IS_DELETED,
    TRUSTEE_IS_INVALID,
    TRUSTEE_IS_COMPUTER,
} TRUSTEE_TYPE;

/* the name of a trustee is a wide string, or an ansi one in the A structures that otherwise look alike. */
typedef struct _TRUSTEE_W {
    struct _TRUSTEE_W *pMultipleTrustee;
    MULTIPLE_TRUSTEE_OPERATION MultipleTrusteeOperation;
    TRUSTEE_FORM TrusteeForm;
    TRUSTEE_TYPE TrusteeType;
    LPWSTR ptstrName;
} TRUSTEE_W, *PTRUSTEE_W;

typedef struct _OBJECTS_AND_SID {
    DWORD ObjectsPresent;
    GUID ObjectTypeGuid;
    GUID InheritedObjectTypeGuid;
    SID *pSid;
} OBJECTS_AND_SID, *POBJECTS_AND_SID;

typedef struct _OBJECTS_AND_NAME_W {
    DWORD ObjectsPresent;
    SE_OBJECT_TYPE ObjectType;
    LPWSTR ObjectTypeName;
    LPWSTR InheritedObjectTypeName;
    LPWSTR ptstrName;
} OBJECTS_AND_NAME_W, *POBJECTS_AND_NAME_W;

typedef enum _ACCESS_MODE {
    NOT_USED_ACCESS,
    GRANT_ACCESS,
    SET_ACCESS,
    DENY_ACCESS,
    REVOKE_ACCESS,
    SET_AUDIT_SUCCESS,
    SET_AUDIT_FAILURE,
} ACCESS_MODE;

typedef struct _EXPLICIT_ACCESS_W {
    DWORD grfAccessPermissions;
    ACCESS_MODE grfAccessMode;
    DWORD grfInheritance;
    TRUSTEE_W Trustee;
} EXPLICIT_ACCESS_W, *PEXPLICIT_ACCESS_W;

#define VALID_INHERIT_FLAGS 0x1F

/* ---------------------------------------------------------------- errors missing from winerror.h */

#ifndef ERROR_BAD_INHERITANCE_ACL
#define ERROR_BAD_INHERITANCE_ACL 1340L
#endif
#ifndef ERROR_NON_ACCOUNT_SID
#define ERROR_NON_ACCOUNT_SID 1257L
#endif
#ifndef ERROR_NON_DOMAIN_SID
#define ERROR_NON_DOMAIN_SID 1258L
#endif
#ifndef ERROR_REVISION_MISMATCH
#define ERROR_REVISION_MISMATCH 1306L
#endif
#ifndef ERROR_INVALID_OWNER
#define ERROR_INVALID_OWNER 1307L
#endif
#ifndef ERROR_BAD_DESCRIPTOR_FORMAT
#define ERROR_BAD_DESCRIPTOR_FORMAT 1361L
#endif
#ifndef ERROR_ARITHMETIC_OVERFLOW
#define ERROR_ARITHMETIC_OVERFLOW 534L
#endif
#ifndef ERROR_INVALID_DATA
#define ERROR_INVALID_DATA 13L
#endif
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
NTSYSCALLAPI NTSTATUS NTAPI RtlImpersonateSelf(ULONG level);

/* the rtl routines of ntdll on sids, lists and descriptors, which the functions of this dll wrap. */
NTSYSAPI BOOLEAN NTAPI RtlValidSid(PSID sid);
NTSYSAPI ULONG NTAPI RtlLengthSid(PSID sid);
NTSYSAPI ULONG NTAPI RtlLengthRequiredSid(ULONG count);
NTSYSAPI BOOLEAN NTAPI RtlEqualSid(PSID a, PSID b);
NTSYSAPI BOOLEAN NTAPI RtlEqualPrefixSid(PSID a, PSID b);
NTSYSAPI NTSTATUS NTAPI RtlCopySid(ULONG length, PSID dst, PSID src);
NTSYSAPI NTSTATUS NTAPI RtlInitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, UCHAR count);
NTSYSAPI NTSTATUS NTAPI RtlAllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY authority, UCHAR count, ULONG s0,
                                                    ULONG s1, ULONG s2, ULONG s3, ULONG s4, ULONG s5, ULONG s6,
                                                    ULONG s7, PSID *sid);
NTSYSAPI PVOID NTAPI RtlFreeSid(PSID sid);
NTSYSAPI PSID_IDENTIFIER_AUTHORITY NTAPI RtlIdentifierAuthoritySid(PSID sid);
NTSYSAPI PULONG NTAPI RtlSubAuthoritySid(PSID sid, ULONG index);
NTSYSAPI PUCHAR NTAPI RtlSubAuthorityCountSid(PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING string, PSID sid, BOOLEAN allocate);
NTSYSAPI NTSTATUS NTAPI RtlCreateAcl(PACL acl, ULONG length, ULONG revision);
NTSYSAPI BOOLEAN NTAPI RtlValidAcl(PACL acl);
NTSYSAPI NTSTATUS NTAPI RtlGetAce(PACL acl, ULONG index, PVOID *ace);
NTSYSAPI NTSTATUS NTAPI RtlAddAce(PACL acl, ULONG revision, ULONG index, PVOID list, ULONG length);
NTSYSAPI NTSTATUS NTAPI RtlDeleteAce(PACL acl, ULONG index);
NTSYSAPI BOOLEAN NTAPI RtlFirstFreeAce(PACL acl, PVOID *ace);
NTSYSAPI NTSTATUS NTAPI RtlQueryInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass);
NTSYSAPI NTSTATUS NTAPI RtlSetInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessAllowedAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessAllowedAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessDeniedAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessDeniedAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAuditAccessAce(PACL acl, ULONG revision, ACCESS_MASK mask, PSID sid, BOOLEAN success,
                                             BOOLEAN failure);
NTSYSAPI NTSTATUS NTAPI RtlAddAuditAccessAceEx(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid,
                                               BOOLEAN success, BOOLEAN failure);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessAllowedObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                                     const GUID *object, const GUID *inherited, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessDeniedObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                                    const GUID *object, const GUID *inherited, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddAuditAccessObjectAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask,
                                                   const GUID *object, const GUID *inherited, PSID sid,
                                                   BOOLEAN success, BOOLEAN failure);
NTSYSAPI NTSTATUS NTAPI RtlAddMandatoryAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                           ACCESS_MASK mask);
NTSYSAPI NTSTATUS NTAPI RtlAddResourceAttributeAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid, PVOID information, PULONG length);
NTSYSAPI NTSTATUS NTAPI RtlAddScopedPolicyIDAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddProcessTrustLabelAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                                   ACCESS_MASK mask);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessFilterAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                              ACCESS_MASK mask, PVOID condition, USHORT condition_length);
NTSYSAPI VOID NTAPI RtlMapGenericMask(PACCESS_MASK mask, const GENERIC_MAPPING *mapping);
NTSYSAPI NTSTATUS NTAPI RtlCreateSecurityDescriptor(PSECURITY_DESCRIPTOR sd, ULONG revision);
NTSYSAPI BOOLEAN NTAPI RtlValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd);
NTSYSAPI BOOLEAN NTAPI RtlValidRelativeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, ULONG length,
                                                          SECURITY_INFORMATION required);
NTSYSAPI ULONG NTAPI RtlLengthSecurityDescriptor(PSECURITY_DESCRIPTOR sd);
NTSYSAPI NTSTATUS NTAPI RtlGetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID *owner, PBOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlSetOwnerSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID owner, BOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlGetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID *group, PBOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlSetGroupSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PSID group, BOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlGetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PBOOLEAN present, PACL *dacl,
                                                     PBOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlSetDaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, BOOLEAN present, PACL dacl,
                                                     BOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlGetSaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, PBOOLEAN present, PACL *sacl,
                                                     PBOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlSetSaclSecurityDescriptor(PSECURITY_DESCRIPTOR sd, BOOLEAN present, PACL sacl,
                                                     BOOLEAN defaulted);
NTSYSAPI NTSTATUS NTAPI RtlGetControlSecurityDescriptor(PSECURITY_DESCRIPTOR sd,
                                                        PSECURITY_DESCRIPTOR_CONTROL control, PULONG revision);
NTSYSAPI NTSTATUS NTAPI RtlSetControlSecurityDescriptor(PSECURITY_DESCRIPTOR sd, SECURITY_DESCRIPTOR_CONTROL mask,
                                                        SECURITY_DESCRIPTOR_CONTROL bits);
NTSYSAPI BOOLEAN NTAPI RtlGetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control);
NTSYSAPI VOID NTAPI RtlSetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR sd, PUCHAR control);
NTSYSAPI NTSTATUS NTAPI RtlMakeSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative,
                                              PULONG length);
NTSYSAPI NTSTATUS NTAPI RtlAbsoluteToSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative,
                                                    PULONG length);
NTSYSAPI NTSTATUS NTAPI RtlSelfRelativeToAbsoluteSD(PSECURITY_DESCRIPTOR relative, PSECURITY_DESCRIPTOR absolute,
                                                    PULONG absolute_size, PACL dacl, PULONG dacl_size, PACL sacl,
                                                    PULONG sacl_size, PSID owner, PULONG owner_size, PSID group,
                                                    PULONG group_size);
NTSYSAPI NTSTATUS NTAPI RtlSelfRelativeToAbsoluteSD2(PSECURITY_DESCRIPTOR relative, PULONG size);
NTSYSAPI BOOLEAN NTAPI RtlAreAllAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);
NTSYSAPI BOOLEAN NTAPI RtlAreAnyAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);
NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObject(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                             PSECURITY_DESCRIPTOR *object, BOOLEAN container, HANDLE token,
                                             PGENERIC_MAPPING mapping);
NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObjectEx(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                               PSECURITY_DESCRIPTOR *object, const GUID *type, BOOLEAN container,
                                               ULONG flags, HANDLE token, PGENERIC_MAPPING mapping);
NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObjectWithMultipleInheritance(
    PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object, const GUID **types,
    ULONG type_count, BOOLEAN container, ULONG flags, HANDLE token, PGENERIC_MAPPING mapping);
NTSYSAPI NTSTATUS NTAPI RtlDeleteSecurityObject(PSECURITY_DESCRIPTOR *object);
NTSYSAPI NTSTATUS NTAPI RtlQuerySecurityObject(PSECURITY_DESCRIPTOR object, SECURITY_INFORMATION information,
                                               PSECURITY_DESCRIPTOR result, ULONG length, PULONG needed);
NTSYSAPI NTSTATUS NTAPI RtlSetSecurityObject(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                             PSECURITY_DESCRIPTOR *object, PGENERIC_MAPPING mapping, HANDLE token);
NTSYSAPI NTSTATUS NTAPI RtlSetSecurityObjectEx(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                               PSECURITY_DESCRIPTOR *object, ULONG flags, PGENERIC_MAPPING mapping,
                                               HANDLE token);
NTSYSAPI NTSTATUS NTAPI RtlConvertToAutoInheritSecurityObject(PSECURITY_DESCRIPTOR parent,
                                                              PSECURITY_DESCRIPTOR current,
                                                              PSECURITY_DESCRIPTOR *result, const GUID *type,
                                                              BOOLEAN container, PGENERIC_MAPPING mapping);

/* privobj.c: whether a token may stand for a client in a check, the error if not. */
DWORD AdvImpersonationError(HANDLE token);

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
/*
 * parses a sid as sddl writes it: S-1-..., or an alias of two letters. the aliases that name a sid inside a domain
 * resolve with the domains of the context, a null context knows this machine's. without an end the text must be
 * the sid alone, else the parse stops after the sid and stores where. returns a win32 error.
 */
typedef struct SDDL_CONTEXT {
    const SID *Domain;
    const SID *Root;
} SDDL_CONTEXT;
DWORD AdvParseSidString(PCWSTR string, const SDDL_CONTEXT *context, void *storage, PCWSTR *end);

/* token.c: the user and groups of a token, the process token for NULL; free with AdvFree. */
TOKEN_USER *AdvQueryTokenUser(HANDLE token);
TOKEN_GROUPS *AdvQueryTokenGroups(HANDLE token);
PVOID AdvQueryToken(HANDLE token, DWORD klass);
/* the sid of the user running the process, copied into storage of SECURITY_MAX_SID_SIZE bytes. */
BOOL AdvCurrentUserSid(void *storage);
/* resolves the pseudo token handles to a real handle, opening one into *opened when needed. */
NTSTATUS AdvResolveToken(HANDLE token, ACCESS_MASK access, HANDLE *real, HANDLE *opened);

/* the sid of this machine's account domain, S-1-5-21-a-b-c, into storage of SECURITY_MAX_SID_SIZE bytes. */
BOOL AdvMachineSid(void *storage);

/* sddl.c: compiles the text of a condition to the binary token stream, LocalAlloc'd, as AddConditionalAce stores it. */
DWORD AdvConditionFromString(PCWSTR text, PVOID *binary, DWORD *size);

/* account.c: account names of sids, and whether a server name designates this machine. */
BOOL AdvIsLocalSystem(LPCWSTR system);
BOOL AdvLookupSid(const SID *sid, PWSTR name, DWORD name_size, PWSTR domain, DWORD domain_size, SID_NAME_USE *use);

/* exports used across files. */
ADVAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *result);
ADVAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR string, PSID *result);
ADVAPI void WINAPI MapGenericMask(PDWORD mask, PGENERIC_MAPPING mapping);
ADVAPI BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, DWORD revision);
ADVAPI BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID owner, BOOL defaulted);
ADVAPI BOOL WINAPI SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID group, BOOL defaulted);
ADVAPI BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL dacl, BOOL defaulted);
ADVAPI BOOL WINAPI SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL sacl, BOOL defaulted);
ADVAPI BOOL WINAPI MakeSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative, LPDWORD length);
ADVAPI void WINAPI BuildTrusteeWithSidW(PTRUSTEE_W trustee, PSID sid);
ADVAPI void WINAPI BuildImpersonateTrusteeW(PTRUSTEE_W trustee, PTRUSTEE_W impersonate);
ADVAPI void WINAPI BuildTrusteeWithObjectsAndSidW(PTRUSTEE_W trustee, POBJECTS_AND_SID objects, GUID *object,
                                                  GUID *inherited, PSID sid);
ADVAPI DWORD WINAPI SetEntriesInAclW(ULONG count, PEXPLICIT_ACCESS_W entries, PACL old, PACL *result);
ADVAPI BOOL WINAPI LookupAccountNameW(LPCWSTR system, LPCWSTR account, PSID sid, LPDWORD sid_size, LPWSTR domain,
                                      LPDWORD domain_size, PSID_NAME_USE use);

/* random.c: fills a buffer from the process random generator. */
BOOL AdvGenRandom(PVOID buffer, SIZE_T length);

#endif
