/*
 * the security reference monitor's internal interface: access token objects.
 *
 * a token keeps its sids in fixed slots, so copying, filtering and removing
 * groups never move a pointer. every change of a token happens under SeLock.
 */
#ifndef MIME_SE_H
#define MIME_SE_H

#include "../ntos.h"


#define SEP_SID_MAX 68 /* 8 bytes and 15 sub authorities */
#define SEP_MAX_GROUPS 0x1000
#define SEP_PRIVILEGE_FIRST 2
#define SEP_PRIVILEGE_LAST 36

#define TokenPrimary 1
#define TokenImpersonation 2

#define SecurityAnonymous 0
#define SecurityIdentification 1
#define SecurityImpersonation 2
#define SecurityDelegation 3

/* the access rights of tokens. */
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
#define SE_GROUP_INTEGRITY_ENABLED 0x00000040
#define SE_GROUP_LOGON_ID 0xC0000000
#define SE_GROUP_NORMAL (SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED)
#define SE_GROUP_LABEL (SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED)
#define SE_GROUP_INVALID_BITS 0x1FFFFF80

#define SE_PRIVILEGE_ENABLED_BY_DEFAULT 0x1
#define SE_PRIVILEGE_ENABLED 0x2
#define SE_PRIVILEGE_REMOVED 0x4
#define SE_PRIVILEGE_VALID (SE_PRIVILEGE_ENABLED_BY_DEFAULT | SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_REMOVED)

/* the privileges by their luid. */
#define SE_TCB_PRIVILEGE 7
#define SE_SECURITY_PRIVILEGE 8
#define SE_ASSIGNPRIMARYTOKEN_PRIVILEGE 3
#define SE_CREATE_TOKEN_PRIVILEGE 2
#define SE_CHANGE_NOTIFY_PRIVILEGE 23
#define SE_IMPERSONATE_PRIVILEGE 29

/* the flags of a token, the values windows keeps in TokenFlags. */
#define TOKEN_WRITE_RESTRICTED 0x8
#define TOKEN_IS_RESTRICTED 0x10
#define TOKEN_SESSION_NOT_REFERENCED 0x20
#define TOKEN_SANDBOX_INERT 0x40
#define TOKEN_VIRTUALIZE_ALLOWED 0x200
#define TOKEN_VIRTUALIZE_ENABLED 0x400
#define TOKEN_IS_FILTERED 0x800
#define TOKEN_UIACCESS 0x1000
#define TOKEN_NOT_LOW 0x2000
#define TOKEN_LOWBOX 0x4000

/* the arguments of NtFilterToken. */
#define DISABLE_MAX_PRIVILEGE 0x1
#define SANDBOX_INERT 0x2
#define LUA_TOKEN 0x4
#define WRITE_RESTRICTED 0x8

/* what a token is for, as the elevation queries tell it. */
#define SEP_ELEVATION_DEFAULT 1
#define SEP_ELEVATION_FULL 2
#define SEP_ELEVATION_LIMITED 3

#define THREAD_SET_THREAD_TOKEN 0x0080
#define THREAD_IMPERSONATE 0x0100
#define THREAD_DIRECT_IMPERSONATION 0x0200
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800

typedef struct SEP_GROUP {
    UCHAR Sid[SEP_SID_MAX];
    ULONG Attributes;
} SEP_GROUP;

typedef struct _TOKEN {
    UCHAR Source[16]; /* TOKEN_SOURCE */
    ULONG64 TokenId, AuthenticationId, ParentTokenId, ModifiedId, Origin;
    LARGE_INTEGER ExpirationTime;
    ULONG Type;
    ULONG ImpersonationLevel;
    ULONG Flags;
    BOOLEAN InUse; /* the primary token of a process */
    ULONG SessionId;
    ULONG ElevationType;
    ULONG64 Present, Enabled, EnabledByDefault; /* privileges, one bit per luid */
    ULONG GroupCount; /* the user is group 0 */
    SEP_GROUP *Groups;
    ULONG RestrictedCount;
    SEP_GROUP *Restricted;
    ULONG OwnerIndex;
    LONG IntegrityIndex; /* -1 for untrusted */
    ULONG MandatoryPolicy;
    UCHAR PrimaryGroup[SEP_SID_MAX];
    ULONG DefaultDaclSize;
    PUCHAR DefaultDacl; /* a copy, NULL for none */
    ULONG DynamicCharged;
    struct _TOKEN *Linked; /* the other half of a filtered administrator's pair, referenced */
    ULONG LowboxNumber;
    ULONG CapabilityCount;
    SEP_GROUP *Capabilities;
    UCHAR AppContainerSid[SEP_SID_MAX]; /* only with TOKEN_LOWBOX */
} TOKEN, *PTOKEN;

extern POBJECT_TYPE SeTokenObjectType;
extern FAST_MUTEX SeLock;

/* sids. */
ULONG SepSidLength(const void *sid);
BOOLEAN SepSidValid(const void *sid, SIZE_T available);
BOOLEAN SepSidEqual(const void *a, const void *b);
void SepMakeSid(void *sid, UCHAR authority, ULONG count, const ULONG *sub);
BOOLEAN SepIsElevatedGroup(const SEP_GROUP *group);

/* token.c */
ULONG64 SepAllocateLuid(void);
ULONG SepPrivilegeCount(const TOKEN *t);
ULONG SepElevated(const TOKEN *t);
ULONG SepIntegrityRid(const TOKEN *t);
BOOLEAN SepIsSystemToken(const TOKEN *t);
PTOKEN SepDuplicate(const PTOKEN source, ULONG type, ULONG level, BOOLEAN effective_only, NTSTATUS *status);
NTSTATUS SepFilter(const PTOKEN source, ULONG flags, ULONG disable_count, const SEP_GROUP *disable, ULONG delete_count,
                   const ULONG *delete_luids, ULONG restrict_count, const SEP_GROUP *restricting, PTOKEN *out);
NTSTATUS SepInsertToken(PTOKEN token, ACCESS_MASK access, ULONG attributes, PHANDLE handle);
ACCESS_MASK SepMapTokenAccess(ACCESS_MASK access);
BOOLEAN SepCanImpersonate(PTOKEN process_token, PTOKEN token, ULONG level);
NTSTATUS SepReferenceTokenByHandle(HANDLE handle, ACCESS_MASK required, PTOKEN *token, ACCESS_MASK *granted);
PTOKEN SepAnonymousToken(void);
PTOKEN SepReferenceSessionToken(void);
PTOKEN SepReferencePrimaryToken(PEPROCESS process);
PTOKEN SepReferenceEffectiveToken(PETHREAD thread);
BOOLEAN SepSinglePrivilegeCheck(ULONG luid, KPROCESSOR_MODE mode);
NTSTATUS SepImpersonateClient(PETHREAD thread, PTOKEN token, BOOLEAN copy_on_open, BOOLEAN effective_only, ULONG level);

/* tokeninfo.c */
NTSTATUS SepSetThreadToken(HANDLE thread_handle, PVOID info, ULONG length);
NTSTATUS SepSetProcessAccessToken(HANDLE process_handle, PVOID info, ULONG length);
NTSTATUS SepReferenceTokenForNewProcess(HANDLE handle, PTOKEN *out);

#endif
