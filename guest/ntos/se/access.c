/*
 * the access check of a descriptor for the sids and privileges of a token, which NtAccessCheck offers to programs
 * that decide for themselves what a client may do. The kernel itself does not use it: handles are given what they
 * ask for, see sd.c.
 *
 * What a token is granted is the access its user and enabled groups are given by the entries of the list in order,
 * without what an entry for them denies, with the rights the owner has, and for the privileges the token holds
 * enabled the right to take ownership and to reach the system list. A restricted token and an app container token
 * are checked a second time against the sids that restrict them, and get what both checks grant. The mandatory
 * label takes away what a lower integrity level may not do.
 */
#include "se.h"
#include <mime/ntrtlsec.h>

#define STATUS_INVALID_SECURITY_DESCR ((NTSTATUS)0xC0000079L)
#define STATUS_NO_IMPERSONATION_TOKEN ((NTSTATUS)0xC000005CL)
#define STATUS_GENERIC_NOT_MAPPED ((NTSTATUS)0xC00000E6L)

#define READ_CONTROL_ 0x00020000
#define WRITE_DAC_ 0x00040000
#define WRITE_OWNER_ 0x00080000
#define SYNCHRONIZE_ 0x00100000
#define GENERIC_BITS 0xF0000000u
#define SE_TAKE_OWNERSHIP_PRIVILEGE 9

#define PRIVILEGE_SET_HEADER 8
#define LUID_AND_ATTRIBUTES_SIZE 12

/* the sids a check matches the entries against, with the attributes the token gave them. */
typedef struct {
    const SEP_GROUP *Groups;
    ULONG Count;
    BOOLEAN UserFirst; /* the first sid is the user, which is not a group that can be disabled */
} IDENTITY;

static const UCHAR g_owner_rights[12] = {1, 1, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0};

static BOOLEAN identity_has(const IDENTITY *id, const UCHAR *sid, BOOLEAN deny)
{
    for (ULONG i = 0; i < id->Count; i++) {
        ULONG a = id->Groups[i].Attributes;
        if (i == 0 && id->UserFirst)
            a |= SE_GROUP_ENABLED;
        BOOLEAN usable = deny ? (a & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY)) != 0
                              : (a & SE_GROUP_ENABLED) && !(a & SE_GROUP_USE_FOR_DENY_ONLY);
        if (usable && SepSidEqual(id->Groups[i].Sid, sid))
            return TRUE;
    }
    return FALSE;
}

/* the sid of an entry whose mask is where every access entry has it, or NULL for an entry of no sid. */
static const UCHAR *ace_sid(const UCHAR *ace)
{
    switch (ace[0]) {
    case ACCESS_ALLOWED_ACE_TYPE:
    case ACCESS_DENIED_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_ACE_TYPE:
        return ace + 8;
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE: {
        ULONG flags = *(const ULONG *)(ace + 8);
        return ace + 12 + ((flags & 1) ? 16 : 0) + ((flags & 2) ? 16 : 0);
    }
    default:
        return nullptr;
    }
}

static BOOLEAN is_allow(UCHAR type)
{
    return type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
           type == ACCESS_ALLOWED_OBJECT_ACE_TYPE || type == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE;
}

/* the entries of the list that are not only for what is inherited, in order, each as its sid and mapped mask. */
static void evaluate(const ACL *dacl, const UCHAR *owner, const IDENTITY *id, const ULONG *mapping, BOOLEAN with_owner_rights,
                     ACCESS_MASK *allowed, ACCESS_MASK *denied)
{
    BOOLEAN owns = with_owner_rights && identity_has(id, owner, FALSE);
    const UCHAR *ace = (const UCHAR *)(dacl + 1);
    BOOLEAN rights_entry = FALSE;
    for (ULONG i = 0; i < dacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        const UCHAR *sid = ace_sid(ace);
        if (sid && !(ace[1] & INHERIT_ONLY_ACE) && SepSidEqual(sid, g_owner_rights))
            rights_entry = TRUE;
    }
    /* the owner may read and change the list, unless the list says what the owner may do. */
    if (owns && !rights_entry)
        *allowed |= READ_CONTROL_ | WRITE_DAC_;
    ace = (const UCHAR *)(dacl + 1);
    for (ULONG i = 0; i < dacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        const UCHAR *sid = ace_sid(ace);
        if (!sid || (ace[1] & INHERIT_ONLY_ACE) || *(const USHORT *)(ace + 2) < 12)
            continue;
        BOOLEAN allow = is_allow(ace[0]);
        /* an entry of the owner rights sid is for whoever owns the object. */
        BOOLEAN match = SepSidEqual(sid, g_owner_rights) ? owns : identity_has(id, sid, !allow);
        if (!match)
            continue;
        ACCESS_MASK mask = *(const ULONG *)(ace + 4);
        if (mask & GENERIC_BITS) {
            if (mask & 0x80000000u)
                mask |= mapping[0];
            if (mask & 0x40000000u)
                mask |= mapping[1];
            if (mask & 0x20000000u)
                mask |= mapping[2];
            if (mask & 0x10000000u)
                mask |= mapping[3];
            mask &= ~GENERIC_BITS;
        }
        if (allow)
            *allowed |= mask & ~*denied;
        else
            *denied |= mask & ~*allowed;
    }
}

/* the rights of a mapping that the policy bits of a label withhold from a lower level. */
static ACCESS_MASK label_withheld(ULONG policy, const ULONG *mapping)
{
    ACCESS_MASK withheld = 0;
    if (policy & 1)
        withheld |= mapping[1];
    if (policy & 2)
        withheld |= mapping[0];
    if (policy & 4)
        withheld |= mapping[2];
    return withheld & ~(ACCESS_MASK)(READ_CONTROL_ | SYNCHRONIZE_);
}

typedef struct {
    ACCESS_MASK Granted;    /* with the rights the privileges add */
    ACCESS_MASK ByList;     /* without them */
    BOOLEAN HasSecurity, HasOwnership;
} VERDICT;

/* what a token is granted of the descriptor at most, for the bits asked for or all of them. */
static void check_token(const PTOKEN token, const UCHAR *sd, const ULONG *mapping, VERDICT *v)
{
    PSID owner = nullptr;
    PACL dacl = nullptr, sacl = nullptr;
    BOOLEAN present, defaulted;
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &owner, &defaulted);
    RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &present, &dacl, &defaulted);
    ACCESS_MASK everything = mapping[3] | 0x1F0000;
    ACCESS_MASK allowed = 0, denied = 0;
    IDENTITY normal = {token->Groups, token->GroupCount, TRUE};
    if (!present || !dacl) {
        allowed = everything;
    } else {
        evaluate(dacl, owner, &normal, mapping, TRUE, &allowed, &denied);
    }
    ACCESS_MASK granted = allowed & ~denied;

    /* a second pass against the sids that restrict the token: a restricted token that is only for writing checks writes. */
    if (token->RestrictedCount) {
        IDENTITY restricting = {token->Restricted, token->RestrictedCount, FALSE};
        ACCESS_MASK second_allowed = 0, second_denied = 0;
        if (!present || !dacl)
            second_allowed = everything;
        else
            evaluate(dacl, owner, &restricting, mapping, TRUE, &second_allowed, &second_denied);
        ACCESS_MASK second = second_allowed & ~second_denied;
        if (token->Flags & TOKEN_WRITE_RESTRICTED) {
            ACCESS_MASK writes = mapping[1] & ~(ACCESS_MASK)(READ_CONTROL_ | SYNCHRONIZE_);
            second |= ~writes;
        }
        granted &= second;
    }

    /* an app container is checked against its own sid, its capabilities and the packages everyone in one has. */
    if (token->Flags & TOKEN_LOWBOX) {
        SEP_GROUP *own = ExAllocatePool2(POOL_FLAG_NON_PAGED, (token->CapabilityCount + 3) * sizeof(SEP_GROUP),
                                         KI_TAG('S', 'e', 'A', 'c'));
        if (!own) {
            granted = 0;
        } else {
            ULONG count = 0;
            memcpy(own[count].Sid, token->AppContainerSid, SEP_SID_MAX);
            own[count++].Attributes = SE_GROUP_ENABLED;
            for (ULONG i = 0; i < token->CapabilityCount; i++)
                own[count++] = token->Capabilities[i];
            memcpy(own[count].Sid, SeSidAllAppPackages, sizeof(SeSidAllAppPackages));
            own[count++].Attributes = SE_GROUP_ENABLED;
            IDENTITY container = {own, count, FALSE};
            ACCESS_MASK second_allowed = 0, second_denied = 0;
            if (!present || !dacl)
                second_allowed = everything;
            else
                evaluate(dacl, owner, &container, mapping, FALSE, &second_allowed, &second_denied);
            granted &= second_allowed & ~second_denied;
            ExFreePool(own);
        }
    }

    /* the label: a token below the level of the object, or of the default one, may not do what the policy forbids. */
    ULONG level = 0x2000, policy = 1;
    BOOLEAN has_label = FALSE;
    BOOLEAN sacl_present;
    RtlGetSaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &sacl_present, &sacl, &defaulted);
    if (sacl_present && sacl) {
        const UCHAR *ace = (const UCHAR *)(sacl + 1);
        for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
            if (ace[0] == SYSTEM_MANDATORY_LABEL_ACE_TYPE && *(const USHORT *)(ace + 2) >= 16) {
                const UCHAR *sid = ace + 8;
                if (sid[1])
                    level = *(const ULONG *)(sid + 8 + 4 * (sid[1] - 1));
                policy = *(const ULONG *)(ace + 4);
                has_label = TRUE;
                break;
            }
        }
    }
    (void)has_label;
    if (SepIntegrityRid(token) < level)
        granted &= ~label_withheld(policy, mapping);

    /* the privileges for ownership and the system list. */
    v->ByList = granted;
    v->Granted = granted;
    v->HasOwnership = (token->Present & token->Enabled & (1ull << SE_TAKE_OWNERSHIP_PRIVILEGE)) != 0;
    v->HasSecurity = (token->Present & token->Enabled & (1ull << SE_SECURITY_PRIVILEGE)) != 0;
    if (v->HasOwnership)
        v->Granted |= WRITE_OWNER_;
    if (v->HasSecurity)
        v->Granted |= ACCESS_SYSTEM_SECURITY;
}

NTSTATUS NTAPI NtAccessCheck(PVOID descriptor, HANDLE token_handle, ACCESS_MASK desired, PVOID mapping_pointer,
                             PVOID privilege_set, PULONG privilege_length, PACCESS_MASK granted_pointer,
                             PNTSTATUS access_status_pointer)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    ULONG mapping[4], length = 0;
    NTSTATUS status;
    if (mode == UserMode) {
        status = KiCopyFromUser(mapping, mapping_pointer, sizeof(mapping));
        if (NT_SUCCESS(status))
            status = KiCopyFromUser(&length, privilege_length, sizeof(length));
    } else {
        memcpy(mapping, mapping_pointer, sizeof(mapping));
        length = *privilege_length;
        status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(status))
        return status;
    if (length < PRIVILEGE_SET_HEADER + LUID_AND_ATTRIBUTES_SIZE) {
        ULONG need = PRIVILEGE_SET_HEADER + LUID_AND_ATTRIBUTES_SIZE;
        if (mode == UserMode)
            KiCopyToUser(privilege_length, &need, sizeof(need));
        else
            *privilege_length = need;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (desired & GENERIC_BITS)
        return STATUS_GENERIC_NOT_MAPPED;

    SD_BLOB *sd = nullptr;
    status = SeSdCapture(descriptor, mode, &sd);
    if (!NT_SUCCESS(status))
        return status;
    PSID owner = nullptr, group = nullptr;
    BOOLEAN defaulted;
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)sd->Data, &owner, &defaulted);
    RtlGetGroupSecurityDescriptor((PSECURITY_DESCRIPTOR)sd->Data, &group, &defaulted);
    if (!owner || !group) {
        SeSdDereference(sd);
        return STATUS_INVALID_SECURITY_DESCR;
    }

    PTOKEN token = nullptr;
    ACCESS_MASK token_access;
    status = SepReferenceTokenByHandle(token_handle, TOKEN_QUERY, &token, &token_access);
    if (!NT_SUCCESS(status)) {
        SeSdDereference(sd);
        return status;
    }
    VERDICT v = {};
    ExAcquireFastMutex(&SeLock);
    if (token->Type != TokenImpersonation)
        status = STATUS_NO_IMPERSONATION_TOKEN;
    else if (token->ImpersonationLevel < SecurityIdentification)
        status = STATUS_BAD_IMPERSONATION_LEVEL;
    else
        check_token(token, sd->Data, mapping, &v);
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(token);
    SeSdDereference(sd);
    if (!NT_SUCCESS(status))
        return status;

    ACCESS_MASK wanted = desired & ~(ACCESS_MASK)MAXIMUM_ALLOWED;
    ACCESS_MASK maximum = v.Granted;
    ACCESS_MASK granted = 0;
    NTSTATUS access_status = STATUS_SUCCESS;
    /* the right to the system list is the privilege's alone, and a program that asked for it without one is told so. */
    if ((wanted & ACCESS_SYSTEM_SECURITY) && !v.HasSecurity) {
        access_status = STATUS_PRIVILEGE_NOT_HELD;
    } else if (desired & MAXIMUM_ALLOWED) {
        granted = maximum;
        if (wanted & ~maximum)
            access_status = STATUS_ACCESS_DENIED;
        else if (!granted)
            access_status = STATUS_ACCESS_DENIED;
    } else if (!wanted || (wanted & ~maximum)) {
        access_status = STATUS_ACCESS_DENIED;
    } else {
        granted = wanted;
    }
    if (access_status == STATUS_ACCESS_DENIED || access_status == STATUS_PRIVILEGE_NOT_HELD)
        granted = 0;

    /* the privileges that decided it, in the set the caller gave. */
    ULONG used = 0;
    UCHAR set[PRIVILEGE_SET_HEADER + 2 * LUID_AND_ATTRIBUTES_SIZE] = {};
    ACCESS_MASK counted = (desired & MAXIMUM_ALLOWED) ? granted : wanted;
    if (NT_SUCCESS(access_status)) {
        if (v.HasOwnership && (counted & WRITE_OWNER_) && !(v.ByList & WRITE_OWNER_)) {
            *(ULONG64 *)(set + PRIVILEGE_SET_HEADER + used * LUID_AND_ATTRIBUTES_SIZE) = SE_TAKE_OWNERSHIP_PRIVILEGE;
            *(ULONG *)(set + PRIVILEGE_SET_HEADER + used * LUID_AND_ATTRIBUTES_SIZE + 8) = 0x80000002; /* used for access, enabled */
            used++;
        }
        if (v.HasSecurity && (counted & ACCESS_SYSTEM_SECURITY)) {
            *(ULONG64 *)(set + PRIVILEGE_SET_HEADER + used * LUID_AND_ATTRIBUTES_SIZE) = SE_SECURITY_PRIVILEGE;
            *(ULONG *)(set + PRIVILEGE_SET_HEADER + used * LUID_AND_ATTRIBUTES_SIZE + 8) = 0x80000002;
            used++;
        }
    }
    *(ULONG *)set = used;
    ULONG need = PRIVILEGE_SET_HEADER + (used ? used : 1) * LUID_AND_ATTRIBUTES_SIZE;
    if (length < need) {
        if (mode == UserMode)
            KiCopyToUser(privilege_length, &need, sizeof(need));
        else
            *privilege_length = need;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (mode == UserMode) {
        status = KiCopyToUser(privilege_set, set, PRIVILEGE_SET_HEADER + (used ? used : 1) * LUID_AND_ATTRIBUTES_SIZE);
        if (NT_SUCCESS(status))
            status = KiCopyToUser(granted_pointer, &granted, sizeof(granted));
        if (NT_SUCCESS(status))
            status = KiCopyToUser(access_status_pointer, &access_status, sizeof(access_status));
        if (NT_SUCCESS(status))
            status = KiCopyToUser(privilege_length, &need, sizeof(need));
    } else {
        memcpy(privilege_set, set, PRIVILEGE_SET_HEADER + (used ? used : 1) * LUID_AND_ATTRIBUTES_SIZE);
        *granted_pointer = granted;
        *access_status_pointer = access_status;
        *privilege_length = need;
    }
    return status;
}
