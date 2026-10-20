/*
 * the access check of a descriptor for the sids and privileges of a token. NtAccessCheck offers it to programs that
 * decide for themselves what a client may do, and SeCheckAccess is the check the kernel makes when a program in
 * user mode opens or creates an object, a file or a key: the calling thread's token against the descriptor of the
 * object, in the order the kernel of windows does, see docs/ntoskrnl.md.
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
#define SE_RELABEL_PRIVILEGE 32

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
static void evaluate(const ACL *dacl, const UCHAR *owner, const IDENTITY *id, const ULONG *mapping, BOOLEAN owns,
                     ACCESS_MASK *allowed, ACCESS_MASK *denied)
{
    (void)owner;
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

/*
 * what the mandatory label of an object lets a token below it do, from the rules of the kernel's own check. A token
 * that does not enforce the policy, or that dominates the level of the object, is held back from nothing. A token
 * below it keeps the read and execute rights and the standard ones that go with them, and loses what the policy of the
 * label names: no read up takes the read rights, no execute up the execute rights, no write up the write rights and
 * delete, write dac, write owner and the system security right. A right that is also a read or an execute right
 * stays while the policy leaves that one alone, so synchronize and read control survive a no write up label.
 */
#define MIC_STANDARD_WRITE 0x010D0000 /* delete, write dac, write owner, access system security */

static ACCESS_MASK label_mask(const TOKEN *token, ULONG level, ULONG policy, const ULONG *mapping)
{
    if (!(token->MandatoryPolicy & 1) || SepIntegrityRid(token) >= level)
        return ~(ACCESS_MASK)0;
    ACCESS_MASK read = mapping[0], write = mapping[1], execute = mapping[2];
    ACCESS_MASK mask = read | execute | READ_CONTROL_ | SYNCHRONIZE_;
    BOOLEAN read_ok = !(policy & 2), execute_ok = !(policy & 4);
    if (policy & 2) {
        ACCESS_MASK keep = execute_ok ? ((~read & execute) | SYNCHRONIZE_) : 0;
        mask &= keep | ~(read | READ_CONTROL_);
    }
    if (policy & 4) {
        ACCESS_MASK keep = read_ok ? (read | READ_CONTROL_) : 0;
        mask &= ~(~keep & ((execute & ~read) | SYNCHRONIZE_));
    }
    if (policy & 1) {
        ACCESS_MASK keep = (execute_ok ? ((execute & ~read) | SYNCHRONIZE_) : 0) | (read_ok ? (read | READ_CONTROL_) : 0);
        mask &= ~(~keep & (write | MIC_STANDARD_WRITE));
    }
    return mask;
}

/* the level and the policy of the label of a descriptor: the label ace in its list, else medium and no write up. */
static void sd_label(const UCHAR *sd, ULONG *level, ULONG *policy)
{
    *level = 0x2000;
    *policy = 1;
    PACL sacl = nullptr;
    BOOLEAN present, defaulted;
    RtlGetSaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &present, &sacl, &defaulted);
    if (!present || !sacl)
        return;
    const UCHAR *ace = (const UCHAR *)(sacl + 1);
    for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
        if (ace[0] != SYSTEM_MANDATORY_LABEL_ACE_TYPE || *(const USHORT *)(ace + 2) < 16)
            continue;
        if (ace[1] & INHERIT_ONLY_ACE)
            return;
        const UCHAR *sid = ace + 8;
        /* a label of another authority is none. */
        if (sid[1] && sid[7] == 16 && !sid[2] && !sid[3] && !sid[4] && !sid[5] && !sid[6])
            *level = *(const ULONG *)(sid + 8 + 4 * (sid[1] - 1));
        *policy = *(const ULONG *)(ace + 4);
        return;
    }
}

/* the integrity level of the label of a descriptor, medium for one without. */
ULONG SeSdIntegrityLevel(const SD_BLOB *sd)
{
    ULONG level, policy;
    sd_label(sd->Data, &level, &policy);
    return level;
}

typedef struct {
    ACCESS_MASK Granted;    /* with the rights the privileges add */
    ACCESS_MASK ByList;     /* without them */
    BOOLEAN HasSecurity, HasOwnership;
} VERDICT;

/* whether the token is the owner of a descriptor: its user or an enabled group, and for a restricted token a restricting sid too. */
static BOOLEAN token_owns(const TOKEN *token, const UCHAR *owner)
{
    if (!owner)
        return FALSE;
    BOOLEAN found = FALSE;
    for (ULONG i = 0; i < token->GroupCount && !found; i++) {
        const SEP_GROUP *g = &token->Groups[i];
        if (((i == 0 && !(g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY)) || (g->Attributes & SE_GROUP_ENABLED)) &&
            SepSidEqual(g->Sid, owner))
            found = TRUE;
    }
    if (!found || !token->RestrictedCount)
        return found;
    for (ULONG i = 0; i < token->RestrictedCount; i++)
        if (SepSidEqual(token->Restricted[i].Sid, owner))
            return (token->Restricted[i].Attributes & SE_GROUP_ENABLED) != 0;
    return FALSE;
}

/* what a token is granted of the descriptor at most, for the bits asked for or all of them. */
static void check_token(const PTOKEN token, const UCHAR *sd, const ULONG *mapping, VERDICT *v)
{
    PSID owner = nullptr;
    PACL dacl = nullptr;
    BOOLEAN present, defaulted;
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &owner, &defaulted);
    RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd, &present, &dacl, &defaulted);
    ACCESS_MASK everything = mapping[3] | 0x1F0000;
    ACCESS_MASK allowed = 0, denied = 0;
    IDENTITY normal = {token->Groups, token->GroupCount, TRUE};
    BOOLEAN owns = token_owns(token, owner);
    if (!present || !dacl)
        allowed = everything;
    else
        evaluate(dacl, owner, &normal, mapping, owns, &allowed, &denied);
    ACCESS_MASK granted = allowed & ~denied;

    /* a second pass against the sids that restrict the token: a write restricted one only checks the rights that write. */
    if (token->RestrictedCount) {
        IDENTITY restricting = {token->Restricted, token->RestrictedCount, FALSE};
        ACCESS_MASK second_allowed = 0, second_denied = 0;
        if (!present || !dacl)
            second_allowed = everything;
        else
            evaluate(dacl, owner, &restricting, mapping, owns, &second_allowed, &second_denied);
        ACCESS_MASK second = second_allowed & ~second_denied;
        if (token->Flags & TOKEN_WRITE_RESTRICTED)
            second |= ~((mapping[1] & ~(mapping[0] | mapping[2])) | MIC_STANDARD_WRITE);
        granted &= second;
    }

    /* an app container is checked against its own sid, its capabilities and the packages everyone in one has. */
    if (token->Flags & TOKEN_LOWBOX) {
        SEP_GROUP own[8];
        SEP_GROUP *use = own;
        ULONG count = 0;
        if (token->CapabilityCount + 2 > RTL_NUMBER_OF(own))
            use = ExAllocatePool2(POOL_FLAG_NON_PAGED, (token->CapabilityCount + 2) * sizeof(SEP_GROUP),
                                  KI_TAG('S', 'e', 'A', 'c'));
        if (!use) {
            granted = 0;
        } else {
            memcpy(use[count].Sid, token->AppContainerSid, SEP_SID_MAX);
            use[count++].Attributes = SE_GROUP_ENABLED;
            for (ULONG i = 0; i < token->CapabilityCount; i++)
                use[count++] = token->Capabilities[i];
            memcpy(use[count].Sid, SeSidAllAppPackages, sizeof(SeSidAllAppPackages));
            use[count++].Attributes = SE_GROUP_ENABLED;
            IDENTITY container = {use, count, FALSE};
            ACCESS_MASK second_allowed = 0, second_denied = 0;
            if (!present || !dacl)
                second_allowed = everything;
            else
                evaluate(dacl, owner, &container, mapping, FALSE, &second_allowed, &second_denied);
            granted &= second_allowed & ~second_denied;
            if (use != own)
                ExFreePool(use);
        }
    }

    /* the privileges for ownership and the system list. */
    v->HasOwnership = (token->Present & token->Enabled & ((1ull << SE_TAKE_OWNERSHIP_PRIVILEGE) | (1ull << SE_RELABEL_PRIVILEGE))) != 0;
    v->HasSecurity = (token->Present & token->Enabled & (1ull << SE_SECURITY_PRIVILEGE)) != 0;
    ACCESS_MASK by_privilege = 0;
    if (v->HasOwnership)
        by_privilege |= WRITE_OWNER_;
    if (v->HasSecurity)
        by_privilege |= ACCESS_SYSTEM_SECURITY;

    /* the label: a token below the level of the object, or of the default one, may not do what the policy forbids. */
    ULONG level, policy;
    sd_label(sd, &level, &policy);
    ACCESS_MASK mask = label_mask(token, level, policy, mapping);
    v->ByList = granted & mask;
    v->Granted = (granted | by_privilege) & mask;
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

/* ---------------------------------------------------------------- the kernel's check */

BOOLEAN SeAccessEnforced = TRUE;
BOOLEAN SeTraceAccess;

#define CACHE_ENTRIES 256

/*
 * what a token is granted of a descriptor is the same every time, so it is kept by the token's id and the id of its
 * last change, the descriptor's serial and the type's mapping. Under SeLock like the token it was made from.
 */
typedef struct {
    ULONG64 TokenId, ModifiedId, Serial;
    const ULONG *Mapping;
    VERDICT Verdict;
} CACHED_VERDICT;

static CACHED_VERDICT g_cache[CACHE_ENTRIES];

static ACCESS_MASK map_generic(ACCESS_MASK mask, const ULONG *m)
{
    if (mask & GENERIC_BITS) {
        if (mask & 0x80000000u)
            mask |= m[0];
        if (mask & 0x40000000u)
            mask |= m[1];
        if (mask & 0x20000000u)
            mask |= m[2];
        if (mask & 0x10000000u)
            mask |= m[3];
        mask &= ~GENERIC_BITS;
    }
    return mask;
}

/* the token a check is for, with SeLock held: the thread's own when it impersonates, else the process's. */
static PTOKEN subject_locked(ULONG flags, BOOLEAN *impersonating)
{
    PETHREAD thread = PsCurrentThread();
    PTOKEN primary = (PTOKEN)PsCurrentProcess()->Token;
    *impersonating = FALSE;
    if (!(flags & SE_ACCESS_AS_SELF) && thread->ImpersonationToken) {
        *impersonating = TRUE;
        return thread->ImpersonationToken;
    }
    return primary;
}

static BOOLEAN unrestricted_system(const TOKEN *t)
{
    return !(t->Flags & (TOKEN_IS_RESTRICTED | TOKEN_LOWBOX)) && SepIsSystemToken(t);
}

BOOLEAN SeAccessCheckRequired(void)
{
    if (!SeAccessEnforced)
        return FALSE;
    BOOLEAN impersonating;
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = subject_locked(0, &impersonating);
    BOOLEAN required = t && !unrestricted_system(t);
    ExReleaseFastMutex(&SeLock);
    return required;
}

BOOLEAN SeThreadHasPrivilege(ULONG luid)
{
    BOOLEAN impersonating;
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = subject_locked(0, &impersonating);
    BOOLEAN held = !t || unrestricted_system(t) || (t->Present & t->Enabled & (1ull << luid)) != 0;
    ExReleaseFastMutex(&SeLock);
    return held;
}

BOOLEAN SeThreadBypassesTraverse(void)
{
    return SeThreadHasPrivilege(SE_CHANGE_NOTIFY_PRIVILEGE);
}

ULONG SeThreadIntegrityRid(void)
{
    BOOLEAN impersonating;
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = subject_locked(0, &impersonating);
    ULONG rid = t ? SepIntegrityRid(t) : 0x4000;
    ExReleaseFastMutex(&SeLock);
    return rid;
}

/* the entries of a descriptor's list and the groups of the token, for a denial that is not understood. */
static void sid_text(const UCHAR *sid, char *out, ULONG size)
{
    ULONG n = 0;
    n += mime_format(out + n, size - n, "S-1-%u", (ULONG)sid[7]);
    for (ULONG i = 0; i < sid[1] && i < 6 && n + 12 < size; i++)
        n += mime_format(out + n, size - n, "-%u", ((const ULONG *)(sid + 8))[i]);
}

void SeTraceDescriptor(const SD_BLOB *sd)
{
    if (!SeTraceAccess || !sd)
        return;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    BOOLEAN present, defaulted;
    char text[96];
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)sd->Data, &owner, &defaulted);
    RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)sd->Data, &present, &dacl, &defaulted);
    if (owner)
        sid_text(owner, text, sizeof(text));
    else
        text[0] = 0;
    ULONG level, policy;
    sd_label(sd->Data, &level, &policy);
    KiLogPrintf("  descriptor serial %llu owner %s, label %x policy %x, list %s with %u entries", sd->Serial, text, level,
                policy, !present ? "absent" : dacl ? "present" : "null", dacl ? dacl->AceCount : 0);
    if (!dacl)
        return;
    const UCHAR *ace = (const UCHAR *)(dacl + 1);
    for (ULONG i = 0; i < dacl->AceCount && i < 24; i++, ace += *(const USHORT *)(ace + 2)) {
        sid_text(ace + 8, text, sizeof(text));
        KiLogPrintf("    entry type %u flags %02x mask %08x %s", (ULONG)ace[0], (ULONG)ace[1], *(const ULONG *)(ace + 4), text);
    }
}

void SeTraceDenial(PCSTR what, const UNICODE_STRING *name, ACCESS_MASK desired, NTSTATUS status)
{
    if (!SeTraceAccess)
        return;
    char text[96];
    ULONG n = 0;
    if (name && name->Buffer) {
        ULONG chars = name->Length / sizeof(WCHAR);
        for (ULONG i = chars > 90 ? chars - 90 : 0; i < chars && n < sizeof(text) - 1; i++)
            text[n++] = name->Buffer[i] < 0x80 && name->Buffer[i] >= 0x20 ? (char)name->Buffer[i] : '?';
    }
    text[n] = 0;
    BOOLEAN impersonating;
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = subject_locked(0, &impersonating);
    ULONG rid = t ? SepIntegrityRid(t) : 0, restricted = t ? t->RestrictedCount : 0, flags = t ? t->Flags : 0;
    ExReleaseFastMutex(&SeLock);
    KiLogPrintf("access denied %s '%s' access %08x status %08x, %s pid %u, integrity %x, restricting sids %u, token flags %x%s",
                what, text, desired, status, PsCurrentProcess()->ImageFileName,
                (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId, rid, restricted, flags,
                impersonating ? ", impersonating" : "");
}

NTSTATUS SeCheckAccess(const SD_BLOB *sd, SD_MAPPING mapping, ACCESS_MASK desired, ACCESS_MASK previous, ULONG flags,
                       ACCESS_MASK *granted)
{
    ACCESS_MASK want = map_generic(desired & ~(ACCESS_MASK)MAXIMUM_ALLOWED, mapping);
    BOOLEAN maximum = (desired & MAXIMUM_ALLOWED) != 0;
    if (!sd || !SeAccessEnforced)
        goto everything;

    ExAcquireFastMutex(&SeLock);
    BOOLEAN impersonating;
    PTOKEN t = subject_locked(flags, &impersonating);
    if (!t || unrestricted_system(t)) {
        ExReleaseFastMutex(&SeLock);
        goto everything;
    }
    if (impersonating && t->ImpersonationLevel < SecurityImpersonation) {
        ExReleaseFastMutex(&SeLock);
        return STATUS_BAD_IMPERSONATION_LEVEL;
    }
    VERDICT v;
    CACHED_VERDICT *c = &g_cache[(t->TokenId * 31 + sd->Serial * 17 + ((ULONG_PTR)mapping >> 4)) % CACHE_ENTRIES];
    if (c->TokenId == t->TokenId && c->ModifiedId == t->ModifiedId && c->Serial == sd->Serial && c->Mapping == mapping) {
        v = c->Verdict;
    } else {
        check_token(t, sd->Data, mapping, &v);
        c->TokenId = t->TokenId;
        c->ModifiedId = t->ModifiedId;
        c->Serial = sd->Serial;
        c->Mapping = mapping;
        c->Verdict = v;
    }
    ACCESS_MASK have = v.Granted;
    /* a file or key opened to back up or restore is open to the privileges meant for it, whatever the list says. */
    if (flags & SE_ACCESS_BACKUP_INTENT) {
        if (t->Present & t->Enabled & (1ull << SE_BACKUP_PRIVILEGE))
            have |= mapping[0] | mapping[2] | READ_CONTROL_ | SYNCHRONIZE_ | ACCESS_SYSTEM_SECURITY;
        if (t->Present & t->Enabled & (1ull << SE_RESTORE_PRIVILEGE))
            have |= mapping[1] | 0x10000 | WRITE_DAC_ | WRITE_OWNER_ | ACCESS_SYSTEM_SECURITY | (mapping[3] & 0x40);
    }
    BOOLEAN has_security = v.HasSecurity;
    ExReleaseFastMutex(&SeLock);
    if (!want && !maximum && !previous)
        return STATUS_ACCESS_DENIED;
    /* the right to the system list is the privilege's alone. */
    if ((want & ACCESS_SYSTEM_SECURITY) && !has_security && !(have & ACCESS_SYSTEM_SECURITY))
        return STATUS_PRIVILEGE_NOT_HELD;
    if (want & ~have)
        return STATUS_ACCESS_DENIED;
    if (maximum) {
        if (!have && !previous)
            return STATUS_ACCESS_DENIED;
        *granted = have | previous;
    } else {
        *granted = want | previous;
    }
    return STATUS_SUCCESS;

everything:
    *granted = want | previous | (maximum ? (mapping[3] ? mapping[3] : want) : 0);
    if (!want && !maximum && !previous)
        *granted = 0;
    return STATUS_SUCCESS;
}
