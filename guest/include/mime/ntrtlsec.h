/*
 * the security types of winnt.h and ntifs.h that the runtime library needs, with the rtl routines on sids,
 * access control lists and security descriptors that ntoskrnl.exe and ntdll.dll both export.
 * the dlls above ntdll keep their own copies of the types.
 */
#ifndef MIME_NTRTLSEC_H
#define MIME_NTRTLSEC_H

#include <mime/nt.h>

typedef PVOID PSID;
typedef PVOID PSECURITY_DESCRIPTOR;
typedef ULONG SECURITY_INFORMATION;
typedef USHORT SECURITY_DESCRIPTOR_CONTROL, *PSECURITY_DESCRIPTOR_CONTROL;

/* ---------------------------------------------------------------- sids */

typedef struct _SID_IDENTIFIER_AUTHORITY {
    UCHAR Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

typedef struct _SID {
    UCHAR Revision;
    UCHAR SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    ULONG SubAuthority[1];
} SID, *PISID;

typedef struct _SID_AND_ATTRIBUTES {
    PSID Sid;
    ULONG Attributes;
} SID_AND_ATTRIBUTES, *PSID_AND_ATTRIBUTES;

#define SID_REVISION 1
#define SID_MAX_SUB_AUTHORITIES 15
#define SECURITY_MAX_SID_SIZE (8 + 4 * SID_MAX_SUB_AUTHORITIES)

/* ---------------------------------------------------------------- access control lists */

typedef struct _ACL {
    UCHAR AclRevision;
    UCHAR Sbz1;
    USHORT AclSize;
    USHORT AceCount;
    USHORT Sbz2;
} ACL, *PACL;

typedef struct _ACE_HEADER {
    UCHAR AceType;
    UCHAR AceFlags;
    USHORT AceSize;
} ACE_HEADER, *PACE_HEADER;

typedef struct _ACCESS_ALLOWED_ACE {
    ACE_HEADER Header;
    ACCESS_MASK Mask;
    ULONG SidStart;
} ACCESS_ALLOWED_ACE, *PACCESS_ALLOWED_ACE;

#define MIN_ACL_REVISION 2
#define ACL_REVISION 2
#define ACL_REVISION_DS 4
#define MAX_ACL_REVISION 4

#define ACCESS_ALLOWED_ACE_TYPE 0x00
#define ACCESS_DENIED_ACE_TYPE 0x01
#define SYSTEM_AUDIT_ACE_TYPE 0x02
#define SYSTEM_ALARM_ACE_TYPE 0x03
#define ACCESS_ALLOWED_COMPOUND_ACE_TYPE 0x04
#define ACCESS_ALLOWED_OBJECT_ACE_TYPE 0x05
#define ACCESS_DENIED_OBJECT_ACE_TYPE 0x06
#define SYSTEM_AUDIT_OBJECT_ACE_TYPE 0x07
#define SYSTEM_ALARM_OBJECT_ACE_TYPE 0x08
#define ACCESS_ALLOWED_CALLBACK_ACE_TYPE 0x09
#define ACCESS_DENIED_CALLBACK_ACE_TYPE 0x0A
#define ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE 0x0B
#define ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE 0x0C
#define SYSTEM_AUDIT_CALLBACK_ACE_TYPE 0x0D
#define SYSTEM_ALARM_CALLBACK_ACE_TYPE 0x0E
#define SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE 0x0F
#define SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE 0x10
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#define SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE 0x12
#define SYSTEM_SCOPED_POLICY_ID_ACE_TYPE 0x13
#define SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE 0x14
#define SYSTEM_ACCESS_FILTER_ACE_TYPE 0x15

#define OBJECT_INHERIT_ACE 0x01
#define CONTAINER_INHERIT_ACE 0x02
#define NO_PROPAGATE_INHERIT_ACE 0x04
#define INHERIT_ONLY_ACE 0x08
#define INHERITED_ACE 0x10
#define CRITICAL_ACE_FLAG 0x20
#define SUCCESSFUL_ACCESS_ACE_FLAG 0x40
#define FAILED_ACCESS_ACE_FLAG 0x80
#define VALID_INHERIT_FLAGS 0x1F

#define ACE_OBJECT_TYPE_PRESENT 0x1
#define ACE_INHERITED_OBJECT_TYPE_PRESENT 0x2

#define SYSTEM_MANDATORY_LABEL_NO_WRITE_UP 0x1
#define SYSTEM_MANDATORY_LABEL_NO_READ_UP 0x2
#define SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP 0x4

#define COMPOUND_ACE_IMPERSONATION 1

typedef enum _ACL_INFORMATION_CLASS {
    AclRevisionInformation = 1,
    AclSizeInformation,
} ACL_INFORMATION_CLASS;

typedef struct _ACL_SIZE_INFORMATION {
    ULONG AceCount;
    ULONG AclBytesInUse;
    ULONG AclBytesFree;
} ACL_SIZE_INFORMATION, *PACL_SIZE_INFORMATION;

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead;
    ACCESS_MASK GenericWrite;
    ACCESS_MASK GenericExecute;
    ACCESS_MASK GenericAll;
} GENERIC_MAPPING, *PGENERIC_MAPPING;

/* ---------------------------------------------------------------- security descriptors */

/* absolute: the parts are pointers. */
typedef struct _SECURITY_DESCRIPTOR {
    UCHAR Revision;
    UCHAR Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID Owner;
    PSID Group;
    PACL Sacl;
    PACL Dacl;
} SECURITY_DESCRIPTOR, *PISECURITY_DESCRIPTOR;

/* self relative: the parts are offsets from the start. */
typedef struct _SECURITY_DESCRIPTOR_RELATIVE {
    UCHAR Revision;
    UCHAR Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    ULONG Owner;
    ULONG Group;
    ULONG Sacl;
    ULONG Dacl;
} SECURITY_DESCRIPTOR_RELATIVE, *PISECURITY_DESCRIPTOR_RELATIVE;

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
#define SE_RM_CONTROL_VALID 0x4000
#define SE_SELF_RELATIVE 0x8000

#define OWNER_SECURITY_INFORMATION 0x00000001
#define GROUP_SECURITY_INFORMATION 0x00000002
#define DACL_SECURITY_INFORMATION 0x00000004
#define SACL_SECURITY_INFORMATION 0x00000008

#define STATUS_BAD_INHERITANCE_ACL ((NTSTATUS)0xC000007DL)
#define STATUS_INVALID_OWNER ((NTSTATUS)0xC000005AL)
#define STATUS_INVALID_PRIMARY_GROUP ((NTSTATUS)0xC000005BL)
#define STATUS_UNKNOWN_REVISION ((NTSTATUS)0xC0000058L)
#define STATUS_REVISION_MISMATCH ((NTSTATUS)0xC0000059L)
#define STATUS_INVALID_ACL ((NTSTATUS)0xC0000077L)
#define STATUS_INVALID_SID ((NTSTATUS)0xC0000078L)
#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)
#define STATUS_ALLOTTED_SPACE_EXCEEDED ((NTSTATUS)0xC0000099L)
#define STATUS_BAD_DESCRIPTOR_FORMAT ((NTSTATUS)0xC00000E7L)
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EFL)
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0L)

#ifndef NTSYSAPI
#if defined(MIME_NTDLL) || defined(MIME_KERNEL)
#define NTSYSAPI
#else
#define NTSYSAPI DECLSPEC_IMPORT
#endif
#endif

/* sids */
NTSYSAPI BOOLEAN NTAPI RtlValidSid(PSID sid);
NTSYSAPI ULONG NTAPI RtlLengthSid(PSID sid);
NTSYSAPI ULONG NTAPI RtlLengthRequiredSid(ULONG count);
NTSYSAPI BOOLEAN NTAPI RtlEqualSid(PSID a, PSID b);
NTSYSAPI BOOLEAN NTAPI RtlEqualPrefixSid(PSID a, PSID b);
NTSYSAPI NTSTATUS NTAPI RtlCopySid(ULONG length, PSID dst, PSID src);
NTSYSAPI NTSTATUS NTAPI RtlInitializeSid(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, UCHAR count);
NTSYSAPI NTSTATUS RtlInitializeSidEx(PSID sid, PSID_IDENTIFIER_AUTHORITY authority, ULONG count, ...);
NTSYSAPI NTSTATUS NTAPI RtlAllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY authority, UCHAR count, ULONG s0,
                                                    ULONG s1, ULONG s2, ULONG s3, ULONG s4, ULONG s5, ULONG s6,
                                                    ULONG s7, PSID *sid);
NTSYSAPI NTSTATUS NTAPI RtlAllocateAndInitializeSidEx(PSID_IDENTIFIER_AUTHORITY authority, UCHAR count,
                                                      const ULONG *sub_authorities, PSID *sid);
NTSYSAPI PVOID NTAPI RtlFreeSid(PSID sid);
NTSYSAPI PSID_IDENTIFIER_AUTHORITY NTAPI RtlIdentifierAuthoritySid(PSID sid);
NTSYSAPI PULONG NTAPI RtlSubAuthoritySid(PSID sid, ULONG index);
NTSYSAPI PUCHAR NTAPI RtlSubAuthorityCountSid(PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlCopySidAndAttributesArray(ULONG count, PSID_AND_ATTRIBUTES source, ULONG sid_bytes,
                                                     PSID_AND_ATTRIBUTES dest, PSID sid_area, PSID *sid_end,
                                                     PULONG sid_bytes_left);
NTSYSAPI NTSTATUS NTAPI RtlSidDominates(PSID a, PSID b, PBOOLEAN dominates);
NTSYSAPI NTSTATUS NTAPI RtlSidIsHigherLevel(PSID a, PSID b, PBOOLEAN higher);
NTSYSAPI NTSTATUS NTAPI RtlSidEqualLevel(PSID a, PSID b, PBOOLEAN equal);
NTSYSAPI BOOLEAN NTAPI RtlIsValidProcessTrustLabelSid(PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlSidDominatesForTrust(PSID a, PSID b, PBOOLEAN dominates);

/* access control lists */
NTSYSAPI NTSTATUS NTAPI RtlCreateAcl(PACL acl, ULONG length, ULONG revision);
NTSYSAPI BOOLEAN NTAPI RtlValidAcl(PACL acl);
NTSYSAPI NTSTATUS NTAPI RtlGetAce(PACL acl, ULONG index, PVOID *ace);
NTSYSAPI NTSTATUS NTAPI RtlAddAce(PACL acl, ULONG revision, ULONG index, PVOID list, ULONG length);
NTSYSAPI NTSTATUS NTAPI RtlDeleteAce(PACL acl, ULONG index);
NTSYSAPI BOOLEAN NTAPI RtlFirstFreeAce(PACL acl, PVOID *ace);
NTSYSAPI NTSTATUS NTAPI RtlQueryInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass);
NTSYSAPI NTSTATUS NTAPI RtlSetInformationAcl(PACL acl, PVOID info, ULONG length, ACL_INFORMATION_CLASS klass);
NTSYSAPI NTSTATUS NTAPI RtlGetAcesBufferSize(PACL acl, PULONG size);
NTSYSAPI PVOID NTAPI RtlFindAceByType(PACL acl, UCHAR type, PULONG index);
NTSYSAPI BOOLEAN NTAPI RtlOwnerAcesPresent(PACL acl);
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
NTSYSAPI NTSTATUS NTAPI RtlAddCompoundAce(PACL acl, ULONG revision, UCHAR type, ACCESS_MASK mask, PSID server,
                                          PSID client);
NTSYSAPI NTSTATUS NTAPI RtlAddScopedPolicyIDAce(PACL acl, ULONG revision, ULONG flags, ACCESS_MASK mask, PSID sid);
NTSYSAPI NTSTATUS NTAPI RtlAddProcessTrustLabelAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                                   ACCESS_MASK mask);
NTSYSAPI NTSTATUS NTAPI RtlAddAccessFilterAce(PACL acl, ULONG revision, ULONG flags, PSID sid, UCHAR type,
                                              ACCESS_MASK mask, PVOID condition, USHORT condition_length);
NTSYSAPI VOID NTAPI RtlMapGenericMask(PACCESS_MASK mask, const GENERIC_MAPPING *mapping);
NTSYSAPI BOOLEAN NTAPI RtlAreAllAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);
NTSYSAPI BOOLEAN NTAPI RtlAreAnyAccessesGranted(ACCESS_MASK granted, ACCESS_MASK desired);

/* security descriptors */
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
NTSYSAPI NTSTATUS NTAPI RtlSetControlSecurityDescriptor(PSECURITY_DESCRIPTOR sd,
                                                        SECURITY_DESCRIPTOR_CONTROL mask,
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

/* private object security. */
#define SEF_DACL_AUTO_INHERIT 0x01
#define SEF_SACL_AUTO_INHERIT 0x02
#define SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT 0x04
#define SEF_AVOID_PRIVILEGE_CHECK 0x08
#define SEF_AVOID_OWNER_CHECK 0x10
#define SEF_DEFAULT_OWNER_FROM_PARENT 0x20
#define SEF_DEFAULT_GROUP_FROM_PARENT 0x40
#define LABEL_SECURITY_INFORMATION 0x00000010
#define ATTRIBUTE_SECURITY_INFORMATION 0x00000020
#define SCOPE_SECURITY_INFORMATION 0x00000040
#define PROCESS_TRUST_LABEL_SECURITY_INFORMATION 0x00000080
#define ACCESS_FILTER_SECURITY_INFORMATION 0x00000100

NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObject(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                             PSECURITY_DESCRIPTOR *object, BOOLEAN container, HANDLE token,
                                             const GENERIC_MAPPING *mapping);
NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObjectEx(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                               PSECURITY_DESCRIPTOR *object, const GUID *type, BOOLEAN container,
                                               ULONG flags, HANDLE token, const GENERIC_MAPPING *mapping);
NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObjectWithMultipleInheritance(
    PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object, const GUID **types,
    ULONG type_count, BOOLEAN container, ULONG flags, HANDLE token, const GENERIC_MAPPING *mapping);
NTSYSAPI NTSTATUS NTAPI RtlDeleteSecurityObject(PSECURITY_DESCRIPTOR *object);
NTSYSAPI NTSTATUS NTAPI RtlQuerySecurityObject(PSECURITY_DESCRIPTOR object, SECURITY_INFORMATION information,
                                               PSECURITY_DESCRIPTOR result, ULONG length, PULONG needed);
NTSYSAPI NTSTATUS NTAPI RtlSetSecurityObject(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                             PSECURITY_DESCRIPTOR *object, const GENERIC_MAPPING *mapping,
                                             HANDLE token);
NTSYSAPI NTSTATUS NTAPI RtlSetSecurityObjectEx(SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR modification,
                                               PSECURITY_DESCRIPTOR *object, ULONG flags,
                                               const GENERIC_MAPPING *mapping, HANDLE token);
NTSYSAPI NTSTATUS NTAPI RtlConvertToAutoInheritSecurityObject(PSECURITY_DESCRIPTOR parent,
                                                              PSECURITY_DESCRIPTOR current,
                                                              PSECURITY_DESCRIPTOR *result, const GUID *type,
                                                              BOOLEAN container, const GENERIC_MAPPING *mapping);

#endif
