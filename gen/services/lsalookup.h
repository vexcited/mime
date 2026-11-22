#ifndef NDRGEN_LSALOOKUP_H
#define NDRGEN_LSALOOKUP_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct LSAPR_STRING LSAPR_STRING;
typedef struct RPC_SID_IDENTIFIER_AUTHORITY RPC_SID_IDENTIFIER_AUTHORITY;
typedef struct RPC_SID RPC_SID;
typedef struct LSAPR_ACL LSAPR_ACL;
typedef struct LSAPR_SECURITY_DESCRIPTOR LSAPR_SECURITY_DESCRIPTOR;
typedef struct LSAPR_SECURITY_QUALITY_OF_SERVICE LSAPR_SECURITY_QUALITY_OF_SERVICE;
typedef struct LSAPR_OBJECT_ATTRIBUTES LSAPR_OBJECT_ATTRIBUTES;
typedef struct LSAPR_SID_INFORMATION LSAPR_SID_INFORMATION;
typedef struct LSAPR_SID_ENUM_BUFFER LSAPR_SID_ENUM_BUFFER;
typedef struct RPC_UNICODE_STRING RPC_UNICODE_STRING;
typedef struct LSAPR_TRUST_INFORMATION LSAPR_TRUST_INFORMATION;
typedef struct LSAPR_REFERENCED_DOMAIN_LIST LSAPR_REFERENCED_DOMAIN_LIST;
typedef struct LSAPR_TRANSLATED_NAME_EX LSAPR_TRANSLATED_NAME_EX;
typedef struct LSAPR_TRANSLATED_NAMES_EX LSAPR_TRANSLATED_NAMES_EX;
typedef struct LSAPR_TRANSLATED_SID_EX2 LSAPR_TRANSLATED_SID_EX2;
typedef struct LSAPR_TRANSLATED_SIDS_EX2 LSAPR_TRANSLATED_SIDS_EX2;
typedef struct LSAPR_POLICY_ACCOUNT_DOM_INFO LSAPR_POLICY_ACCOUNT_DOM_INFO;
typedef struct LSAPR_POLICY_DNS_DOMAIN_INFO LSAPR_POLICY_DNS_DOMAIN_INFO;
typedef union LSAPR_LOOKUP_DOMAIN_INFO LSAPR_LOOKUP_DOMAIN_INFO;

typedef unsigned short * LSAPR_SERVER_NAME;
struct LSAPR_STRING {
    unsigned short Length;
    unsigned short MaximumLength;
    char *Buffer;
};
struct RPC_SID_IDENTIFIER_AUTHORITY {
    byte Value[6];
};
struct RPC_SID {
    unsigned char Revision;
    unsigned char SubAuthorityCount;
    RPC_SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    unsigned long SubAuthority[1];
};
struct LSAPR_ACL {
    unsigned char AclRevision;
    unsigned char Sbz1;
    unsigned short AclSize;
    unsigned char Dummy1[1];
};
struct LSAPR_SECURITY_DESCRIPTOR {
    unsigned char Revision;
    unsigned char Sbz1;
    unsigned short Control;
    RPC_SID *Owner;
    RPC_SID *Group;
    LSAPR_ACL *Sacl;
    LSAPR_ACL *Dacl;
};
typedef enum LSAPR_IMPERSONATION_LEVEL { LsaprSecurityAnonymous = 0, LsaprSecurityIdentification = 1, LsaprSecurityImpersonation = 2, LsaprSecurityDelegation = 3 } LSAPR_IMPERSONATION_LEVEL;
struct LSAPR_SECURITY_QUALITY_OF_SERVICE {
    unsigned long Length;
    LSAPR_IMPERSONATION_LEVEL ImpersonationLevel;
    unsigned char ContextTrackingMode;
    unsigned char EffectiveOnly;
};
struct LSAPR_OBJECT_ATTRIBUTES {
    unsigned long Length;
    unsigned char *RootDirectory;
    LSAPR_STRING *ObjectName;
    unsigned long Attributes;
    LSAPR_SECURITY_DESCRIPTOR *SecurityDescriptor;
    LSAPR_SECURITY_QUALITY_OF_SERVICE *SecurityQualityOfService;
};
typedef void *LSAPR_HANDLE;
struct LSAPR_SID_INFORMATION {
    RPC_SID *Sid;
};
struct LSAPR_SID_ENUM_BUFFER {
    unsigned long Entries;
    LSAPR_SID_INFORMATION *SidInfo;
};
struct RPC_UNICODE_STRING {
    unsigned short Length;
    unsigned short MaximumLength;
    unsigned short *Buffer;
};
struct LSAPR_TRUST_INFORMATION {
    RPC_UNICODE_STRING Name;
    RPC_SID *Sid;
};
struct LSAPR_REFERENCED_DOMAIN_LIST {
    unsigned long Entries;
    LSAPR_TRUST_INFORMATION *Domains;
    unsigned long MaxEntries;
};
typedef enum LSAPR_SID_NAME_USE { LsaprSidTypeUser = 1, LsaprSidTypeGroup = 2, LsaprSidTypeDomain = 3, LsaprSidTypeAlias = 4, LsaprSidTypeWellKnownGroup = 5, LsaprSidTypeDeletedAccount = 6, LsaprSidTypeInvalid = 7, LsaprSidTypeUnknown = 8, LsaprSidTypeComputer = 9, LsaprSidTypeLabel = 10 } LSAPR_SID_NAME_USE;
struct LSAPR_TRANSLATED_NAME_EX {
    LSAPR_SID_NAME_USE Use;
    RPC_UNICODE_STRING Name;
    long DomainIndex;
    unsigned long Flags;
};
struct LSAPR_TRANSLATED_NAMES_EX {
    unsigned long Entries;
    LSAPR_TRANSLATED_NAME_EX *Names;
};
struct LSAPR_TRANSLATED_SID_EX2 {
    LSAPR_SID_NAME_USE Use;
    RPC_SID *Sid;
    long DomainIndex;
    unsigned long Flags;
};
struct LSAPR_TRANSLATED_SIDS_EX2 {
    unsigned long Entries;
    LSAPR_TRANSLATED_SID_EX2 *Sids;
};
typedef enum LSA_LOOKUP_DOMAIN_INFO_CLASS { AccountDomainInformation = 5, DnsDomainInformation = 12 } LSA_LOOKUP_DOMAIN_INFO_CLASS;
struct LSAPR_POLICY_ACCOUNT_DOM_INFO {
    RPC_UNICODE_STRING DomainName;
    RPC_SID *DomainSid;
};
struct LSAPR_POLICY_DNS_DOMAIN_INFO {
    RPC_UNICODE_STRING Name;
    RPC_UNICODE_STRING DnsDomainName;
    RPC_UNICODE_STRING DnsForestName;
    GUID DomainGuid;
    RPC_SID *Sid;
};
union LSAPR_LOOKUP_DOMAIN_INFO {
    LSAPR_POLICY_ACCOUNT_DOM_INFO AccountDomainInfo;
    LSAPR_POLICY_DNS_DOMAIN_INFO DnsDomainInfo;
};

#ifdef _WIN64
_Static_assert(sizeof(LSAPR_SERVER_NAME) == 8, "size of LSAPR_SERVER_NAME");
_Static_assert(sizeof(LSAPR_STRING) == 16, "size of LSAPR_STRING");
_Static_assert(__builtin_offsetof(LSAPR_STRING, Length) == 0, "LSAPR_STRING.Length");
_Static_assert(__builtin_offsetof(LSAPR_STRING, MaximumLength) == 2, "LSAPR_STRING.MaximumLength");
_Static_assert(__builtin_offsetof(LSAPR_STRING, Buffer) == 8, "LSAPR_STRING.Buffer");
_Static_assert(sizeof(RPC_SID_IDENTIFIER_AUTHORITY) == 6, "size of RPC_SID_IDENTIFIER_AUTHORITY");
_Static_assert(__builtin_offsetof(RPC_SID_IDENTIFIER_AUTHORITY, Value) == 0, "RPC_SID_IDENTIFIER_AUTHORITY.Value");
_Static_assert(sizeof(RPC_SID) == 12, "size of RPC_SID");
_Static_assert(__builtin_offsetof(RPC_SID, Revision) == 0, "RPC_SID.Revision");
_Static_assert(__builtin_offsetof(RPC_SID, SubAuthorityCount) == 1, "RPC_SID.SubAuthorityCount");
_Static_assert(__builtin_offsetof(RPC_SID, IdentifierAuthority) == 2, "RPC_SID.IdentifierAuthority");
_Static_assert(__builtin_offsetof(RPC_SID, SubAuthority) == 8, "RPC_SID.SubAuthority");
_Static_assert(sizeof(LSAPR_ACL) == 6, "size of LSAPR_ACL");
_Static_assert(__builtin_offsetof(LSAPR_ACL, AclRevision) == 0, "LSAPR_ACL.AclRevision");
_Static_assert(__builtin_offsetof(LSAPR_ACL, Sbz1) == 1, "LSAPR_ACL.Sbz1");
_Static_assert(__builtin_offsetof(LSAPR_ACL, AclSize) == 2, "LSAPR_ACL.AclSize");
_Static_assert(__builtin_offsetof(LSAPR_ACL, Dummy1) == 4, "LSAPR_ACL.Dummy1");
_Static_assert(sizeof(LSAPR_SECURITY_DESCRIPTOR) == 40, "size of LSAPR_SECURITY_DESCRIPTOR");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Revision) == 0, "LSAPR_SECURITY_DESCRIPTOR.Revision");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Sbz1) == 1, "LSAPR_SECURITY_DESCRIPTOR.Sbz1");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Control) == 2, "LSAPR_SECURITY_DESCRIPTOR.Control");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Owner) == 8, "LSAPR_SECURITY_DESCRIPTOR.Owner");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Group) == 16, "LSAPR_SECURITY_DESCRIPTOR.Group");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Sacl) == 24, "LSAPR_SECURITY_DESCRIPTOR.Sacl");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Dacl) == 32, "LSAPR_SECURITY_DESCRIPTOR.Dacl");
_Static_assert(sizeof(LSAPR_IMPERSONATION_LEVEL) == 4, "size of LSAPR_IMPERSONATION_LEVEL");
_Static_assert(sizeof(LSAPR_SECURITY_QUALITY_OF_SERVICE) == 12, "size of LSAPR_SECURITY_QUALITY_OF_SERVICE");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, Length) == 0, "LSAPR_SECURITY_QUALITY_OF_SERVICE.Length");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, ImpersonationLevel) == 4, "LSAPR_SECURITY_QUALITY_OF_SERVICE.ImpersonationLevel");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, ContextTrackingMode) == 8, "LSAPR_SECURITY_QUALITY_OF_SERVICE.ContextTrackingMode");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, EffectiveOnly) == 9, "LSAPR_SECURITY_QUALITY_OF_SERVICE.EffectiveOnly");
_Static_assert(sizeof(LSAPR_OBJECT_ATTRIBUTES) == 48, "size of LSAPR_OBJECT_ATTRIBUTES");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, Length) == 0, "LSAPR_OBJECT_ATTRIBUTES.Length");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, RootDirectory) == 8, "LSAPR_OBJECT_ATTRIBUTES.RootDirectory");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, ObjectName) == 16, "LSAPR_OBJECT_ATTRIBUTES.ObjectName");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, Attributes) == 24, "LSAPR_OBJECT_ATTRIBUTES.Attributes");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, SecurityDescriptor) == 32, "LSAPR_OBJECT_ATTRIBUTES.SecurityDescriptor");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, SecurityQualityOfService) == 40, "LSAPR_OBJECT_ATTRIBUTES.SecurityQualityOfService");
_Static_assert(sizeof(LSAPR_HANDLE) == 8, "size of LSAPR_HANDLE");
_Static_assert(sizeof(LSAPR_SID_INFORMATION) == 8, "size of LSAPR_SID_INFORMATION");
_Static_assert(__builtin_offsetof(LSAPR_SID_INFORMATION, Sid) == 0, "LSAPR_SID_INFORMATION.Sid");
_Static_assert(sizeof(LSAPR_SID_ENUM_BUFFER) == 16, "size of LSAPR_SID_ENUM_BUFFER");
_Static_assert(__builtin_offsetof(LSAPR_SID_ENUM_BUFFER, Entries) == 0, "LSAPR_SID_ENUM_BUFFER.Entries");
_Static_assert(__builtin_offsetof(LSAPR_SID_ENUM_BUFFER, SidInfo) == 8, "LSAPR_SID_ENUM_BUFFER.SidInfo");
_Static_assert(sizeof(RPC_UNICODE_STRING) == 16, "size of RPC_UNICODE_STRING");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, Length) == 0, "RPC_UNICODE_STRING.Length");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, MaximumLength) == 2, "RPC_UNICODE_STRING.MaximumLength");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, Buffer) == 8, "RPC_UNICODE_STRING.Buffer");
_Static_assert(sizeof(LSAPR_TRUST_INFORMATION) == 24, "size of LSAPR_TRUST_INFORMATION");
_Static_assert(__builtin_offsetof(LSAPR_TRUST_INFORMATION, Name) == 0, "LSAPR_TRUST_INFORMATION.Name");
_Static_assert(__builtin_offsetof(LSAPR_TRUST_INFORMATION, Sid) == 16, "LSAPR_TRUST_INFORMATION.Sid");
_Static_assert(sizeof(LSAPR_REFERENCED_DOMAIN_LIST) == 24, "size of LSAPR_REFERENCED_DOMAIN_LIST");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, Entries) == 0, "LSAPR_REFERENCED_DOMAIN_LIST.Entries");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, Domains) == 8, "LSAPR_REFERENCED_DOMAIN_LIST.Domains");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, MaxEntries) == 16, "LSAPR_REFERENCED_DOMAIN_LIST.MaxEntries");
_Static_assert(sizeof(LSAPR_SID_NAME_USE) == 4, "size of LSAPR_SID_NAME_USE");
_Static_assert(sizeof(LSAPR_TRANSLATED_NAME_EX) == 32, "size of LSAPR_TRANSLATED_NAME_EX");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Use) == 0, "LSAPR_TRANSLATED_NAME_EX.Use");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Name) == 8, "LSAPR_TRANSLATED_NAME_EX.Name");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, DomainIndex) == 24, "LSAPR_TRANSLATED_NAME_EX.DomainIndex");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Flags) == 28, "LSAPR_TRANSLATED_NAME_EX.Flags");
_Static_assert(sizeof(LSAPR_TRANSLATED_NAMES_EX) == 16, "size of LSAPR_TRANSLATED_NAMES_EX");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAMES_EX, Entries) == 0, "LSAPR_TRANSLATED_NAMES_EX.Entries");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAMES_EX, Names) == 8, "LSAPR_TRANSLATED_NAMES_EX.Names");
_Static_assert(sizeof(LSAPR_TRANSLATED_SID_EX2) == 24, "size of LSAPR_TRANSLATED_SID_EX2");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Use) == 0, "LSAPR_TRANSLATED_SID_EX2.Use");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Sid) == 8, "LSAPR_TRANSLATED_SID_EX2.Sid");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, DomainIndex) == 16, "LSAPR_TRANSLATED_SID_EX2.DomainIndex");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Flags) == 20, "LSAPR_TRANSLATED_SID_EX2.Flags");
_Static_assert(sizeof(LSAPR_TRANSLATED_SIDS_EX2) == 16, "size of LSAPR_TRANSLATED_SIDS_EX2");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SIDS_EX2, Entries) == 0, "LSAPR_TRANSLATED_SIDS_EX2.Entries");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SIDS_EX2, Sids) == 8, "LSAPR_TRANSLATED_SIDS_EX2.Sids");
_Static_assert(sizeof(LSA_LOOKUP_DOMAIN_INFO_CLASS) == 4, "size of LSA_LOOKUP_DOMAIN_INFO_CLASS");
_Static_assert(sizeof(LSAPR_POLICY_ACCOUNT_DOM_INFO) == 24, "size of LSAPR_POLICY_ACCOUNT_DOM_INFO");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_ACCOUNT_DOM_INFO, DomainName) == 0, "LSAPR_POLICY_ACCOUNT_DOM_INFO.DomainName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_ACCOUNT_DOM_INFO, DomainSid) == 16, "LSAPR_POLICY_ACCOUNT_DOM_INFO.DomainSid");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(LSAPR_POLICY_DNS_DOMAIN_INFO) == 72, "size of LSAPR_POLICY_DNS_DOMAIN_INFO");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, Name) == 0, "LSAPR_POLICY_DNS_DOMAIN_INFO.Name");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DnsDomainName) == 16, "LSAPR_POLICY_DNS_DOMAIN_INFO.DnsDomainName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DnsForestName) == 32, "LSAPR_POLICY_DNS_DOMAIN_INFO.DnsForestName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DomainGuid) == 48, "LSAPR_POLICY_DNS_DOMAIN_INFO.DomainGuid");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, Sid) == 64, "LSAPR_POLICY_DNS_DOMAIN_INFO.Sid");
_Static_assert(sizeof(LSAPR_LOOKUP_DOMAIN_INFO) == 72, "size of LSAPR_LOOKUP_DOMAIN_INFO");
#else
_Static_assert(sizeof(LSAPR_SERVER_NAME) == 8, "size of LSAPR_SERVER_NAME");
_Static_assert(sizeof(LSAPR_STRING) == 8, "size of LSAPR_STRING");
_Static_assert(__builtin_offsetof(LSAPR_STRING, Length) == 0, "LSAPR_STRING.Length");
_Static_assert(__builtin_offsetof(LSAPR_STRING, MaximumLength) == 2, "LSAPR_STRING.MaximumLength");
_Static_assert(__builtin_offsetof(LSAPR_STRING, Buffer) == 4, "LSAPR_STRING.Buffer");
_Static_assert(sizeof(RPC_SID_IDENTIFIER_AUTHORITY) == 6, "size of RPC_SID_IDENTIFIER_AUTHORITY");
_Static_assert(__builtin_offsetof(RPC_SID_IDENTIFIER_AUTHORITY, Value) == 0, "RPC_SID_IDENTIFIER_AUTHORITY.Value");
_Static_assert(sizeof(RPC_SID) == 12, "size of RPC_SID");
_Static_assert(__builtin_offsetof(RPC_SID, Revision) == 0, "RPC_SID.Revision");
_Static_assert(__builtin_offsetof(RPC_SID, SubAuthorityCount) == 1, "RPC_SID.SubAuthorityCount");
_Static_assert(__builtin_offsetof(RPC_SID, IdentifierAuthority) == 2, "RPC_SID.IdentifierAuthority");
_Static_assert(__builtin_offsetof(RPC_SID, SubAuthority) == 8, "RPC_SID.SubAuthority");
_Static_assert(sizeof(LSAPR_ACL) == 6, "size of LSAPR_ACL");
_Static_assert(__builtin_offsetof(LSAPR_ACL, AclRevision) == 0, "LSAPR_ACL.AclRevision");
_Static_assert(__builtin_offsetof(LSAPR_ACL, Sbz1) == 1, "LSAPR_ACL.Sbz1");
_Static_assert(__builtin_offsetof(LSAPR_ACL, AclSize) == 2, "LSAPR_ACL.AclSize");
_Static_assert(__builtin_offsetof(LSAPR_ACL, Dummy1) == 4, "LSAPR_ACL.Dummy1");
_Static_assert(sizeof(LSAPR_SECURITY_DESCRIPTOR) == 20, "size of LSAPR_SECURITY_DESCRIPTOR");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Revision) == 0, "LSAPR_SECURITY_DESCRIPTOR.Revision");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Sbz1) == 1, "LSAPR_SECURITY_DESCRIPTOR.Sbz1");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Control) == 2, "LSAPR_SECURITY_DESCRIPTOR.Control");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Owner) == 4, "LSAPR_SECURITY_DESCRIPTOR.Owner");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Group) == 8, "LSAPR_SECURITY_DESCRIPTOR.Group");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Sacl) == 12, "LSAPR_SECURITY_DESCRIPTOR.Sacl");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_DESCRIPTOR, Dacl) == 16, "LSAPR_SECURITY_DESCRIPTOR.Dacl");
_Static_assert(sizeof(LSAPR_IMPERSONATION_LEVEL) == 4, "size of LSAPR_IMPERSONATION_LEVEL");
_Static_assert(sizeof(LSAPR_SECURITY_QUALITY_OF_SERVICE) == 12, "size of LSAPR_SECURITY_QUALITY_OF_SERVICE");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, Length) == 0, "LSAPR_SECURITY_QUALITY_OF_SERVICE.Length");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, ImpersonationLevel) == 4, "LSAPR_SECURITY_QUALITY_OF_SERVICE.ImpersonationLevel");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, ContextTrackingMode) == 8, "LSAPR_SECURITY_QUALITY_OF_SERVICE.ContextTrackingMode");
_Static_assert(__builtin_offsetof(LSAPR_SECURITY_QUALITY_OF_SERVICE, EffectiveOnly) == 9, "LSAPR_SECURITY_QUALITY_OF_SERVICE.EffectiveOnly");
_Static_assert(sizeof(LSAPR_OBJECT_ATTRIBUTES) == 24, "size of LSAPR_OBJECT_ATTRIBUTES");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, Length) == 0, "LSAPR_OBJECT_ATTRIBUTES.Length");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, RootDirectory) == 4, "LSAPR_OBJECT_ATTRIBUTES.RootDirectory");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, ObjectName) == 8, "LSAPR_OBJECT_ATTRIBUTES.ObjectName");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, Attributes) == 12, "LSAPR_OBJECT_ATTRIBUTES.Attributes");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, SecurityDescriptor) == 16, "LSAPR_OBJECT_ATTRIBUTES.SecurityDescriptor");
_Static_assert(__builtin_offsetof(LSAPR_OBJECT_ATTRIBUTES, SecurityQualityOfService) == 20, "LSAPR_OBJECT_ATTRIBUTES.SecurityQualityOfService");
_Static_assert(sizeof(LSAPR_HANDLE) == 4, "size of LSAPR_HANDLE");
_Static_assert(sizeof(LSAPR_SID_INFORMATION) == 4, "size of LSAPR_SID_INFORMATION");
_Static_assert(__builtin_offsetof(LSAPR_SID_INFORMATION, Sid) == 0, "LSAPR_SID_INFORMATION.Sid");
_Static_assert(sizeof(LSAPR_SID_ENUM_BUFFER) == 8, "size of LSAPR_SID_ENUM_BUFFER");
_Static_assert(__builtin_offsetof(LSAPR_SID_ENUM_BUFFER, Entries) == 0, "LSAPR_SID_ENUM_BUFFER.Entries");
_Static_assert(__builtin_offsetof(LSAPR_SID_ENUM_BUFFER, SidInfo) == 4, "LSAPR_SID_ENUM_BUFFER.SidInfo");
_Static_assert(sizeof(RPC_UNICODE_STRING) == 8, "size of RPC_UNICODE_STRING");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, Length) == 0, "RPC_UNICODE_STRING.Length");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, MaximumLength) == 2, "RPC_UNICODE_STRING.MaximumLength");
_Static_assert(__builtin_offsetof(RPC_UNICODE_STRING, Buffer) == 4, "RPC_UNICODE_STRING.Buffer");
_Static_assert(sizeof(LSAPR_TRUST_INFORMATION) == 12, "size of LSAPR_TRUST_INFORMATION");
_Static_assert(__builtin_offsetof(LSAPR_TRUST_INFORMATION, Name) == 0, "LSAPR_TRUST_INFORMATION.Name");
_Static_assert(__builtin_offsetof(LSAPR_TRUST_INFORMATION, Sid) == 8, "LSAPR_TRUST_INFORMATION.Sid");
_Static_assert(sizeof(LSAPR_REFERENCED_DOMAIN_LIST) == 12, "size of LSAPR_REFERENCED_DOMAIN_LIST");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, Entries) == 0, "LSAPR_REFERENCED_DOMAIN_LIST.Entries");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, Domains) == 4, "LSAPR_REFERENCED_DOMAIN_LIST.Domains");
_Static_assert(__builtin_offsetof(LSAPR_REFERENCED_DOMAIN_LIST, MaxEntries) == 8, "LSAPR_REFERENCED_DOMAIN_LIST.MaxEntries");
_Static_assert(sizeof(LSAPR_SID_NAME_USE) == 4, "size of LSAPR_SID_NAME_USE");
_Static_assert(sizeof(LSAPR_TRANSLATED_NAME_EX) == 20, "size of LSAPR_TRANSLATED_NAME_EX");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Use) == 0, "LSAPR_TRANSLATED_NAME_EX.Use");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Name) == 4, "LSAPR_TRANSLATED_NAME_EX.Name");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, DomainIndex) == 12, "LSAPR_TRANSLATED_NAME_EX.DomainIndex");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAME_EX, Flags) == 16, "LSAPR_TRANSLATED_NAME_EX.Flags");
_Static_assert(sizeof(LSAPR_TRANSLATED_NAMES_EX) == 8, "size of LSAPR_TRANSLATED_NAMES_EX");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAMES_EX, Entries) == 0, "LSAPR_TRANSLATED_NAMES_EX.Entries");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_NAMES_EX, Names) == 4, "LSAPR_TRANSLATED_NAMES_EX.Names");
_Static_assert(sizeof(LSAPR_TRANSLATED_SID_EX2) == 16, "size of LSAPR_TRANSLATED_SID_EX2");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Use) == 0, "LSAPR_TRANSLATED_SID_EX2.Use");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Sid) == 4, "LSAPR_TRANSLATED_SID_EX2.Sid");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, DomainIndex) == 8, "LSAPR_TRANSLATED_SID_EX2.DomainIndex");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SID_EX2, Flags) == 12, "LSAPR_TRANSLATED_SID_EX2.Flags");
_Static_assert(sizeof(LSAPR_TRANSLATED_SIDS_EX2) == 8, "size of LSAPR_TRANSLATED_SIDS_EX2");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SIDS_EX2, Entries) == 0, "LSAPR_TRANSLATED_SIDS_EX2.Entries");
_Static_assert(__builtin_offsetof(LSAPR_TRANSLATED_SIDS_EX2, Sids) == 4, "LSAPR_TRANSLATED_SIDS_EX2.Sids");
_Static_assert(sizeof(LSA_LOOKUP_DOMAIN_INFO_CLASS) == 4, "size of LSA_LOOKUP_DOMAIN_INFO_CLASS");
_Static_assert(sizeof(LSAPR_POLICY_ACCOUNT_DOM_INFO) == 12, "size of LSAPR_POLICY_ACCOUNT_DOM_INFO");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_ACCOUNT_DOM_INFO, DomainName) == 0, "LSAPR_POLICY_ACCOUNT_DOM_INFO.DomainName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_ACCOUNT_DOM_INFO, DomainSid) == 8, "LSAPR_POLICY_ACCOUNT_DOM_INFO.DomainSid");
_Static_assert(sizeof(GUID) == 16, "size of GUID");
_Static_assert(__builtin_offsetof(GUID, Data1) == 0, "GUID.Data1");
_Static_assert(__builtin_offsetof(GUID, Data2) == 4, "GUID.Data2");
_Static_assert(__builtin_offsetof(GUID, Data3) == 6, "GUID.Data3");
_Static_assert(__builtin_offsetof(GUID, Data4) == 8, "GUID.Data4");
_Static_assert(sizeof(LSAPR_POLICY_DNS_DOMAIN_INFO) == 44, "size of LSAPR_POLICY_DNS_DOMAIN_INFO");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, Name) == 0, "LSAPR_POLICY_DNS_DOMAIN_INFO.Name");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DnsDomainName) == 8, "LSAPR_POLICY_DNS_DOMAIN_INFO.DnsDomainName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DnsForestName) == 16, "LSAPR_POLICY_DNS_DOMAIN_INFO.DnsForestName");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, DomainGuid) == 24, "LSAPR_POLICY_DNS_DOMAIN_INFO.DomainGuid");
_Static_assert(__builtin_offsetof(LSAPR_POLICY_DNS_DOMAIN_INFO, Sid) == 40, "LSAPR_POLICY_DNS_DOMAIN_INFO.Sid");
_Static_assert(sizeof(LSAPR_LOOKUP_DOMAIN_INFO) == 44, "size of LSAPR_LOOKUP_DOMAIN_INFO");
#endif

/* ------------------------------------------------------------------ interface lsalookup */

#ifndef LSALOOKUP_CLIENT_ONLY
extern long WINAPI lsalookup_LsarOpenPolicy_s(LSAPR_SERVER_NAME SystemName, LSAPR_OBJECT_ATTRIBUTES *ObjectAttributes, unsigned long DesiredAccess, LSAPR_HANDLE *PolicyHandle);
extern long WINAPI lsalookup_LsarClose_s(LSAPR_HANDLE *ObjectHandle);
extern long WINAPI lsalookup_LsarLookupSids_s(LSAPR_HANDLE PolicyHandle, LSAPR_SID_ENUM_BUFFER *SidEnumBuffer, LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains, LSAPR_TRANSLATED_NAMES_EX *TranslatedNames, unsigned long *MappedCount, unsigned long LookupOptions, unsigned long ClientRevision);
extern long WINAPI lsalookup_LsarLookupNames_s(LSAPR_HANDLE PolicyHandle, unsigned long Count, RPC_UNICODE_STRING *Names, LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains, LSAPR_TRANSLATED_SIDS_EX2 *TranslatedSids, unsigned long *MappedCount, unsigned long LookupOptions, unsigned long ClientRevision);
extern long WINAPI lsalookup_LsarLookupUserAccountType_s(handle_t h);
extern long WINAPI lsalookup_LsarGetDomainInfo_s(LSAPR_HANDLE PolicyHandle, LSA_LOOKUP_DOMAIN_INFO_CLASS DomainInfoClass, LSAPR_LOOKUP_DOMAIN_INFO **DomainInfo);
extern long WINAPI lsalookup_LsarManageSidNameMapping_s(handle_t h);
#endif
void WINAPI LSAPR_HANDLE_rundown(void *);
handle_t WINAPI LSAPR_SERVER_NAME_bind(LSAPR_SERVER_NAME);
void WINAPI LSAPR_SERVER_NAME_unbind(LSAPR_SERVER_NAME, handle_t);
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char lsalookup_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x1a, 0x03, 0x30, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x08, 0x40, 0x36, 0x36, /* 6: struct LSAPR_OBJECT_ATTRIBUTES (BOGUS) */
    0x08, 0x40, 0x36, 0x36, 0x5b, 0x12, 0x08, 0x03, 0x5c, 0x12, 0x00, 0x0a,
    0x00, 0x12, 0x00, 0x29, 0x00, 0x12, 0x00, 0x72, 0x00,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x06, 0x06, 0x40, 0x36, /* 39: struct LSAPR_STRING (BOGUS) */
    0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1c, 0x00, 0x01, 0x00, 0x16, 0x00, 0x02, 0x00, 0x01, 0x00, 0x16, 0x00, /* 56: array */
    0x00, 0x00, 0x01, 0x00, 0x02, 0x5b,
    0x1a, 0x03, 0x28, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x03, 0x03, 0x06, 0x40, /* 74: struct LSAPR_SECURITY_DESCRIPTOR (BOGUS) */
    0x36, 0x36, 0x36, 0x36, 0x5b, 0x12, 0x00, 0x0e, 0x00, 0x12, 0x00, 0x0a,
    0x00, 0x12, 0x00, 0x1c, 0x00, 0x12, 0x00, 0x18, 0x00,
    0x17, 0x03, 0x08, 0x00, 0x06, 0x00, 0x03, 0x03, 0x42, 0x5b, /* 107: struct RPC_SID (CSTRUCT) */
    0x1b, 0x03, 0x04, 0x00, 0x03, 0x00, 0xf9, 0xff, 0x01, 0x00, 0x08, 0x5b, /* 117: array */
    0x17, 0x01, 0x04, 0x00, 0x06, 0x00, 0x03, 0x03, 0x06, 0x5b, /* 129: struct LSAPR_ACL (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x00, 0x03, 0x5b, /* 139: array */
    0x1a, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0d, 0x03, 0x03, /* 151: struct LSAPR_SECURITY_QUALITY_OF_SERVICE (BOGUS) */
    0x3e, 0x5b,
    0x30, 0xa0, 0x00, 0x00, /* 165: context handle LSAPR_HANDLE */
    0x30, 0xe0, 0x00, 0x00, /* 169: context handle LSAPR_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 173: context handle LSAPR_HANDLE */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x09, 0x00, 0x4c, 0x00, 0x09, 0x00, /* 177: struct LSAPR_SID_ENUM_BUFFER (BOGUS) */
    0x40, 0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, /* 196: range */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 206: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x36, 0x5b, 0x12, 0x00, /* 227: struct LSAPR_SID_INFORMATION (BOGUS) */
    0x7c, 0xff,
    0x11, 0x14, 0x02, 0x00, /* 241: pointer */
    0x12, 0x00, 0x02, 0x00, /* 245: pointer */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x40, 0x36, 0x08, /* 249: struct LSAPR_REFERENCED_DOMAIN_LIST (BOGUS) */
    0x40, 0x5b, 0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 267: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0x08, 0x00, /* 288: struct LSAPR_TRUST_INFORMATION (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x3b, 0xff,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x06, 0x06, 0x40, 0x36, /* 306: struct RPC_UNICODE_STRING (BOGUS) */
    0x5b, 0x12, 0x00, 0x02, 0x00,
    0x1c, 0x01, 0x02, 0x00, 0x16, 0x55, 0x02, 0x00, 0x01, 0x00, 0x16, 0x55, /* 323: array */
    0x00, 0x00, 0x01, 0x00, 0x05, 0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x09, 0x00, 0x4c, 0x00, 0x09, 0x00, /* 341: struct LSAPR_TRANSLATED_NAMES_EX (BOGUS) */
    0x40, 0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, /* 360: range */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 370: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x4c, 0x04, 0xa0, /* 391: struct LSAPR_TRANSLATED_NAME_EX (BOGUS) */
    0xff, 0x08, 0x08, 0x5b,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, /* 407: range */
    0x11, 0x00, 0x02, 0x00, /* 417: pointer */
    0x21, 0x03, 0x00, 0x00, 0x28, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, /* 421: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x7b, 0xff, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 442: pointer */
    0x12, 0x00, 0x39, 0xff, /* 446: pointer */
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x09, 0x00, 0x4c, 0x00, 0x09, 0x00, /* 450: struct LSAPR_TRANSLATED_SIDS_EX2 (BOGUS) */
    0x40, 0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, /* 469: range */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 479: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0d, 0x40, 0x36, 0x08, /* 500: struct LSAPR_TRANSLATED_SID_EX2 (BOGUS) */
    0x08, 0x5b, 0x12, 0x00, 0x67, 0xfe,
    0x11, 0x14, 0x02, 0x00, /* 518: pointer */
    0x12, 0x00, 0x02, 0x00, /* 522: pointer */
    0x2b, 0x06, 0x2d, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x48, 0x00, /* 526: union LSAPR_LOOKUP_DOMAIN_INFO */
    0x02, 0x30, 0x05, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x16, 0x00, 0xff, 0xff,
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0xfe, 0xfe, /* 554: struct LSAPR_POLICY_ACCOUNT_DOM_INFO (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x31, 0xfe,
    0x1a, 0x03, 0x48, 0x00, 0x00, 0x00, 0x14, 0x00, 0x4c, 0x00, 0xec, 0xfe, /* 572: struct LSAPR_POLICY_DNS_DOMAIN_INFO (BOGUS) */
    0x4c, 0x00, 0xe8, 0xfe, 0x4c, 0x00, 0xe4, 0xfe, 0x4c, 0x00, 0x08, 0x00,
    0x36, 0x5b, 0x12, 0x00, 0x13, 0xfe,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 602: struct GUID (STRUCT) */
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x16, 0x03, 0x18, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 6: struct LSAPR_OBJECT_ATTRIBUTES (PSTRUCT) */
    0x12, 0x08, 0x03, 0x5c, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x00,
    0x1e, 0x00, 0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x12, 0x00, 0x3b, 0x00,
    0x46, 0x5c, 0x14, 0x00, 0x14, 0x00, 0x12, 0x00, 0x94, 0x00, 0x5b, 0x08,
    0x36, 0x36, 0x08, 0x36, 0x36, 0x5b,
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 60: struct LSAPR_STRING (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x06, 0x06, 0x36, 0x5b,
    0x1c, 0x00, 0x01, 0x00, 0x16, 0x00, 0x02, 0x00, 0x01, 0x00, 0x16, 0x00, /* 81: array */
    0x00, 0x00, 0x01, 0x00, 0x02, 0x5b,
    0x16, 0x03, 0x14, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 99: struct LSAPR_SECURITY_DESCRIPTOR (PSTRUCT) */
    0x12, 0x00, 0x29, 0x00, 0x46, 0x5c, 0x08, 0x00, 0x08, 0x00, 0x12, 0x00,
    0x1f, 0x00, 0x46, 0x5c, 0x0c, 0x00, 0x0c, 0x00, 0x12, 0x00, 0x2b, 0x00,
    0x46, 0x5c, 0x10, 0x00, 0x10, 0x00, 0x12, 0x00, 0x21, 0x00, 0x5b, 0x03,
    0x03, 0x06, 0x36, 0x36, 0x36, 0x36, 0x5b,
    0x17, 0x03, 0x08, 0x00, 0x06, 0x00, 0x03, 0x03, 0x42, 0x5b, /* 154: struct RPC_SID (CSTRUCT) */
    0x1b, 0x03, 0x04, 0x00, 0x03, 0x00, 0xf9, 0xff, 0x01, 0x00, 0x08, 0x5b, /* 164: array */
    0x17, 0x01, 0x04, 0x00, 0x06, 0x00, 0x03, 0x03, 0x06, 0x5b, /* 176: struct LSAPR_ACL (CSTRUCT) */
    0x1b, 0x00, 0x01, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, 0x00, 0x03, 0x5b, /* 186: array */
    0x1a, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0d, 0x03, 0x03, /* 198: struct LSAPR_SECURITY_QUALITY_OF_SERVICE (BOGUS) */
    0x3e, 0x5b,
    0x30, 0xa0, 0x00, 0x00, /* 212: context handle LSAPR_HANDLE */
    0x30, 0xe0, 0x00, 0x00, /* 216: context handle LSAPR_HANDLE */
    0x30, 0x40, 0x00, 0x00, /* 220: context handle LSAPR_HANDLE */
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0x08, 0x00, /* 224: struct LSAPR_SID_ENUM_BUFFER (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, /* 242: range */
    0x1b, 0x03, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x4b, 0x5c, /* 252: array */
    0x48, 0x49, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x84, 0xff, 0x5b, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x16, 0x03, 0x04, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 286: struct LSAPR_SID_INFORMATION (PSTRUCT) */
    0x12, 0x00, 0x6e, 0xff, 0x5b, 0x36, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 305: pointer */
    0x12, 0x00, 0x02, 0x00, /* 309: pointer */
    0x1a, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x36, 0x08, 0x5b, /* 313: struct LSAPR_REFERENCED_DOMAIN_LIST (BOGUS) */
    0x12, 0x00, 0x02, 0x00,
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 329: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0x08, 0x00, /* 350: struct LSAPR_TRUST_INFORMATION (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x2c, 0xff,
    0x16, 0x03, 0x08, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x04, 0x00, 0x04, 0x00, /* 368: struct RPC_UNICODE_STRING (PSTRUCT) */
    0x12, 0x00, 0x07, 0x00, 0x5b, 0x06, 0x06, 0x36, 0x5b,
    0x1c, 0x01, 0x02, 0x00, 0x16, 0x55, 0x02, 0x00, 0x01, 0x00, 0x16, 0x55, /* 389: array */
    0x00, 0x00, 0x01, 0x00, 0x05, 0x5b,
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0x08, 0x00, /* 407: struct LSAPR_TRANSLATED_NAMES_EX (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, /* 425: range */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 435: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x4c, 0x00, 0x9d, /* 456: struct LSAPR_TRANSLATED_NAME_EX (BOGUS) */
    0xff, 0x08, 0x08, 0x5b,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, /* 472: range */
    0x11, 0x00, 0x02, 0x00, /* 482: pointer */
    0x1b, 0x03, 0x08, 0x00, 0x28, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4b, 0x5c, /* 486: array */
    0x48, 0x49, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x12, 0x00, 0x85, 0xff, 0x5b, 0x4c, 0x00, 0x6b, 0xff, 0x5b,
    0x11, 0x14, 0x02, 0x00, /* 520: pointer */
    0x12, 0x00, 0x2b, 0xff, /* 524: pointer */
    0x1a, 0x03, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0x08, 0x00, /* 528: struct LSAPR_TRANSLATED_SIDS_EX2 (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x0c, 0x00,
    0xb7, 0x08, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, /* 546: range */
    0x21, 0x03, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, /* 556: array */
    0xff, 0xff, 0x00, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x5b,
    0x1a, 0x03, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x0d, 0x36, 0x08, 0x08, /* 577: struct LSAPR_TRANSLATED_SID_EX2 (BOGUS) */
    0x5b, 0x12, 0x00, 0x4a, 0xfe,
    0x11, 0x14, 0x02, 0x00, /* 594: pointer */
    0x12, 0x00, 0x02, 0x00, /* 598: pointer */
    0x2b, 0x06, 0x2d, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x2c, 0x00, /* 602: union LSAPR_LOOKUP_DOMAIN_INFO */
    0x02, 0x30, 0x05, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x16, 0x00, 0xff, 0xff,
    0x1a, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x08, 0x00, 0x4c, 0x00, 0xf0, 0xfe, /* 630: struct LSAPR_POLICY_ACCOUNT_DOM_INFO (BOGUS) */
    0x36, 0x5b, 0x12, 0x00, 0x14, 0xfe,
    0x1a, 0x03, 0x2c, 0x00, 0x00, 0x00, 0x14, 0x00, 0x4c, 0x00, 0xde, 0xfe, /* 648: struct LSAPR_POLICY_DNS_DOMAIN_INFO (BOGUS) */
    0x4c, 0x00, 0xda, 0xfe, 0x4c, 0x00, 0xd6, 0xfe, 0x4c, 0x00, 0x08, 0x00,
    0x36, 0x5b, 0x12, 0x00, 0xf6, 0xfd,
    0x15, 0x03, 0x10, 0x00, 0x08, 0x06, 0x06, 0x43, 0x3d, 0x5b, /* 678: struct GUID (STRUCT) */
#endif
};

static const unsigned char lsalookup_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x28, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 2: lsalookup.LsarOpenPolicy */
    0x07, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x01,
    0x08, 0x00, 0x06, 0x00, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c, 0x10, 0x01,
    0x18, 0x00, 0xa5, 0x00, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x10, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 60: lsalookup.LsarClose */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0xa9, 0x00, 0x70, 0x00,
    0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x40, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 100: lsalookup.LsarLookupSids */
    0x2f, 0x00, 0x0f, 0x00, 0x47, 0x08, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xad, 0x00, 0x0b, 0x01,
    0x08, 0x00, 0xb1, 0x00, 0x13, 0x20, 0x10, 0x00, 0xf1, 0x00, 0x1b, 0x01,
    0x18, 0x00, 0x55, 0x01, 0x58, 0x01, 0x20, 0x00, 0x08, 0x5c, 0x48, 0x00,
    0x28, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x30, 0x00, 0x08, 0x5c, 0x70, 0x00,
    0x38, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x48, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 176: lsalookup.LsarLookupNames */
    0x37, 0x00, 0x0f, 0x00, 0x47, 0x09, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xad, 0x00, 0x88, 0x00,
    0x08, 0x00, 0x97, 0x01, 0x0b, 0x00, 0x10, 0x00, 0xa1, 0x01, 0x13, 0x20,
    0x18, 0x00, 0xba, 0x01, 0x1b, 0x01, 0x20, 0x00, 0xc2, 0x01, 0x58, 0x01,
    0x28, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x30, 0x00, 0x08, 0x5c, 0x48, 0x00,
    0x38, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x40, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 258: lsalookup.LsarLookupUserAccountType */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x20, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 290: lsalookup.LsarGetDomainInfo */
    0x1b, 0x00, 0x07, 0x00, 0x45, 0x04, 0x0a, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xad, 0x00, 0x48, 0x00,
    0x08, 0x00, 0x0d, 0x5c, 0x13, 0x20, 0x10, 0x00, 0x06, 0x02, 0x70, 0x00,
    0x18, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 342: lsalookup.LsarManageSidNameMapping */
    0x07, 0x00, 0x44, 0x01, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x08, 0x00, 0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x18, 0x00, 0x31, 0x08, 0x00, 0x00, 0x00, 0x5c, /* 2: lsalookup.LsarOpenPolicy */
    0x07, 0x00, 0x1f, 0x00, 0x46, 0x05, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x01, 0x08, 0x00,
    0x06, 0x00, 0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x10, 0x01, 0x10, 0x00,
    0xd4, 0x00, 0x70, 0x00, 0x14, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x01, 0x00, 0x08, 0x00, 0x30, 0xe0, 0x00, 0x00, 0x00, 0x00, /* 58: lsalookup.LsarClose */
    0x17, 0x00, 0x1f, 0x00, 0x44, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0xd8, 0x00, 0x70, 0x00, 0x04, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x02, 0x00, 0x20, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 96: lsalookup.LsarLookupSids */
    0x2f, 0x00, 0x0f, 0x00, 0x47, 0x08, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xdc, 0x00, 0x0b, 0x01, 0x04, 0x00,
    0xe0, 0x00, 0x13, 0x20, 0x08, 0x00, 0x31, 0x01, 0x1b, 0x01, 0x0c, 0x00,
    0x97, 0x01, 0x58, 0x01, 0x10, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x14, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x1c, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x03, 0x00, 0x24, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 170: lsalookup.LsarLookupNames */
    0x37, 0x00, 0x0f, 0x00, 0x47, 0x09, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xdc, 0x00, 0x88, 0x00, 0x04, 0x00,
    0xd8, 0x01, 0x0b, 0x00, 0x08, 0x00, 0xe2, 0x01, 0x13, 0x20, 0x0c, 0x00,
    0x08, 0x02, 0x1b, 0x01, 0x10, 0x00, 0x10, 0x02, 0x58, 0x01, 0x14, 0x00,
    0x08, 0x5c, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x1c, 0x00,
    0x08, 0x5c, 0x70, 0x00, 0x20, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x04, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 250: lsalookup.LsarLookupUserAccountType */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
    0x00, 0x40, 0x05, 0x00, 0x10, 0x00, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, /* 280: lsalookup.LsarGetDomainInfo */
    0x1b, 0x00, 0x07, 0x00, 0x45, 0x04, 0x08, 0x01, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xdc, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x0d, 0x5c, 0x13, 0x20, 0x08, 0x00, 0x52, 0x02, 0x70, 0x00, 0x0c, 0x00,
    0x08, 0x5c,
    0x00, 0x40, 0x06, 0x00, 0x08, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, /* 330: lsalookup.LsarManageSidNameMapping */
    0x07, 0x00, 0x44, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x04, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short lsalookup_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2, 60, 100, 176, 258, 290, 342
#else
    2, 58, 96, 170, 250, 280, 330
#endif
};

static void WINAPI lsalookup_ExprEval_0(PMIDL_STUB_MESSAGE pStubMsg)
{
#ifdef _WIN64
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((*(unsigned short *)(pStubMsg->StackTop + (-2))) - 4));
#else
    pStubMsg->Offset = 0;
    pStubMsg->MaxCount = (ULONG_PTR)(((*(unsigned short *)(pStubMsg->StackTop + (-2))) - 4));
#endif
}

static const EXPR_EVAL lsalookup_ExprEvalRoutines[] = {
    lsalookup_ExprEval_0
};

static const NDR_RUNDOWN lsalookup_RundownRoutines[] = {
    (NDR_RUNDOWN)(void *)LSAPR_HANDLE_rundown
};

static const GENERIC_BINDING_ROUTINE_PAIR lsalookup_BindingRoutines[] = {
    { (GENERIC_BINDING_ROUTINE)(void *)LSAPR_SERVER_NAME_bind, (GENERIC_UNBIND_ROUTINE)(void *)LSAPR_SERVER_NAME_unbind }
};

static RPC_CLIENT_INTERFACE lsalookup_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0xfb8a0729, 0x2d04, 0x4658, {0xbe, 0x93, 0x27, 0xb4, 0xad, 0x55, 0x3f, 0xac}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE lsalookup_c_ifspec = (RPC_IF_HANDLE)&lsalookup_c_ifspec_data;

static MIDL_STUB_DESC lsalookup_StubDesc = {
    .RpcInterfaceInformation = &lsalookup_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .apfnNdrRundownRoutines = lsalookup_RundownRoutines,
    .aGenericBindingRoutinePairs = lsalookup_BindingRoutines,
    .apfnExprEval = lsalookup_ExprEvalRoutines,
    .pFormatTypes = lsalookup_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef LSALOOKUP_CLIENT_ONLY
static const SERVER_ROUTINE lsalookup_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)lsalookup_LsarOpenPolicy_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarClose_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarLookupSids_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarLookupNames_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarLookupUserAccountType_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarGetDomainInfo_s,
    (SERVER_ROUTINE)(void *)lsalookup_LsarManageSidNameMapping_s
};

int (*lsalookup_hook)(PRPC_MESSAGE);

static void __RPC_STUB lsalookup_dispatch_0(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_1(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_2(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_3(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_4(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_5(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static void __RPC_STUB lsalookup_dispatch_6(PRPC_MESSAGE m)
{
    if (lsalookup_hook && lsalookup_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION lsalookup_DispatchFunctions[] = {
lsalookup_dispatch_0, lsalookup_dispatch_1, lsalookup_dispatch_2, lsalookup_dispatch_3, lsalookup_dispatch_4, lsalookup_dispatch_5, lsalookup_dispatch_6
};
static RPC_DISPATCH_TABLE lsalookup_DispatchTable = { 7, lsalookup_DispatchFunctions, 0 };
static MIDL_SERVER_INFO lsalookup_ServerInfo = {
    .pStubDesc = &lsalookup_StubDesc,
    .DispatchTable = lsalookup_ServerRoutineTable,
    .ProcString = lsalookup_ProcFormat,
    .FmtStringOffset = lsalookup_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE lsalookup_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0xfb8a0729, 0x2d04, 0x4658, {0xbe, 0x93, 0x27, 0xb4, 0xad, 0x55, 0x3f, 0xac}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &lsalookup_DispatchTable, 0, 0, 0, &lsalookup_ServerInfo, 0 };
RPC_IF_HANDLE lsalookup_s_ifspec = (RPC_IF_HANDLE)&lsalookup_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI lsalookup_LsarOpenPolicy(LSAPR_SERVER_NAME SystemName, LSAPR_OBJECT_ATTRIBUTES *ObjectAttributes, unsigned long DesiredAccess, LSAPR_HANDLE *PolicyHandle)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[0], SystemName, ObjectAttributes, DesiredAccess, PolicyHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        LSAPR_SERVER_NAME SystemName;
        LSAPR_OBJECT_ATTRIBUTES *ObjectAttributes;
        unsigned long DesiredAccess;
        LSAPR_HANDLE *PolicyHandle;
    } frame = { SystemName, ObjectAttributes, DesiredAccess, PolicyHandle };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarClose(LSAPR_HANDLE *ObjectHandle)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[1], ObjectHandle).Simple;
#else
#pragma pack(push, 4)
    struct {
        LSAPR_HANDLE *ObjectHandle;
    } frame = { ObjectHandle };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[1], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarLookupSids(LSAPR_HANDLE PolicyHandle, LSAPR_SID_ENUM_BUFFER *SidEnumBuffer, LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains, LSAPR_TRANSLATED_NAMES_EX *TranslatedNames, unsigned long *MappedCount, unsigned long LookupOptions, unsigned long ClientRevision)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[2], PolicyHandle, SidEnumBuffer, ReferencedDomains, TranslatedNames, MappedCount, LookupOptions, ClientRevision).Simple;
#else
#pragma pack(push, 4)
    struct {
        LSAPR_HANDLE PolicyHandle;
        LSAPR_SID_ENUM_BUFFER *SidEnumBuffer;
        LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains;
        LSAPR_TRANSLATED_NAMES_EX *TranslatedNames;
        unsigned long *MappedCount;
        unsigned long LookupOptions;
        unsigned long ClientRevision;
    } frame = { PolicyHandle, SidEnumBuffer, ReferencedDomains, TranslatedNames, MappedCount, LookupOptions, ClientRevision };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[2], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarLookupNames(LSAPR_HANDLE PolicyHandle, unsigned long Count, RPC_UNICODE_STRING *Names, LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains, LSAPR_TRANSLATED_SIDS_EX2 *TranslatedSids, unsigned long *MappedCount, unsigned long LookupOptions, unsigned long ClientRevision)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[3], PolicyHandle, Count, Names, ReferencedDomains, TranslatedSids, MappedCount, LookupOptions, ClientRevision).Simple;
#else
#pragma pack(push, 4)
    struct {
        LSAPR_HANDLE PolicyHandle;
        unsigned long Count;
        RPC_UNICODE_STRING *Names;
        LSAPR_REFERENCED_DOMAIN_LIST **ReferencedDomains;
        LSAPR_TRANSLATED_SIDS_EX2 *TranslatedSids;
        unsigned long *MappedCount;
        unsigned long LookupOptions;
        unsigned long ClientRevision;
    } frame = { PolicyHandle, Count, Names, ReferencedDomains, TranslatedSids, MappedCount, LookupOptions, ClientRevision };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[3], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarLookupUserAccountType(handle_t h)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[4], h).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
    } frame = { h };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[4], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarGetDomainInfo(LSAPR_HANDLE PolicyHandle, LSA_LOOKUP_DOMAIN_INFO_CLASS DomainInfoClass, LSAPR_LOOKUP_DOMAIN_INFO **DomainInfo)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[5], PolicyHandle, DomainInfoClass, DomainInfo).Simple;
#else
#pragma pack(push, 4)
    struct {
        LSAPR_HANDLE PolicyHandle;
        LSA_LOOKUP_DOMAIN_INFO_CLASS DomainInfoClass;
        LSAPR_LOOKUP_DOMAIN_INFO **DomainInfo;
    } frame = { PolicyHandle, DomainInfoClass, DomainInfo };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[5], (unsigned char *)&frame).Simple;
#endif
}

static NDRGEN_UNUSED long WINAPI lsalookup_LsarManageSidNameMapping(handle_t h)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[6], h).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
    } frame = { h };
#pragma pack(pop)
    return (long)NdrClientCall2(&lsalookup_StubDesc, lsalookup_ProcFormat + lsalookup_FormatStringOffsetTable[6], (unsigned char *)&frame).Simple;
#endif
}

#endif
