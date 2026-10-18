/*
 * the system services of access tokens: opening, querying and setting,
 * copying and filtering, adjusting privileges and groups, the identity of
 * threads and the token of a new process.
 */
#include "se.h"

#define TAG_OUT KI_TAG('S', 'e', 'Q', 'r')
#define STATUS_CANT_DISABLE_MANDATORY ((NTSTATUS)0xC000005DL)

enum {
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
    TokenUserClaimAttributes,
    TokenDeviceClaimAttributes,
    TokenRestrictedUserClaimAttributes,
    TokenRestrictedDeviceClaimAttributes,
    TokenDeviceGroups,
    TokenRestrictedDeviceGroups,
    TokenSecurityAttributes,
    TokenIsRestricted,
    TokenProcessTrustLevel,
    TokenPrivateNameSpace,
    TokenSingletonAttributes,
    TokenBnoIsolation,
    TokenChildProcessFlags,
    TokenIsLessPrivilegedAppContainer,
    TokenIsSandboxed,
    TokenIsAppSilo,
};

/* ------------------------------------------------------------------ the caller's memory */

static NTSTATUS read_caller(void *dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        return KiCopyFromUser(dst, src, length);
    memcpy(dst, src, length);
    return STATUS_SUCCESS;
}

static NTSTATUS write_caller(void *dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        return KiCopyToUser(dst, src, length);
    memcpy(dst, src, length);
    return STATUS_SUCCESS;
}

static NTSTATUS write_length(PULONG returned, ULONG value)
{
    return write_caller(returned, &value, sizeof(value));
}

/* a sid of the caller, validated; sid must hold SEP_SID_MAX bytes. */
static NTSTATUS capture_sid(const void *user, UCHAR *sid)
{
    UCHAR header[2];
    NTSTATUS status = read_caller(header, user, 2);
    if (!NT_SUCCESS(status))
        return status;
    if (header[1] > 15)
        return STATUS_INVALID_SID;
    status = read_caller(sid, user, 8 + 4 * (SIZE_T)header[1]);
    if (!NT_SUCCESS(status))
        return status;
    return SepSidValid(sid, 8 + 4 * (SIZE_T)header[1]) ? STATUS_SUCCESS : STATUS_INVALID_SID;
}

/* a TOKEN_GROUPS of the caller as groups; free the array with ExFreePool. */
static NTSTATUS capture_groups(const void *user, SEP_GROUP **out, ULONG *count)
{
    *out = nullptr;
    *count = 0;
    if (!user)
        return STATUS_SUCCESS;
    ULONG n;
    NTSTATUS status = read_caller(&n, user, sizeof(n));
    if (!NT_SUCCESS(status) || !n)
        return status;
    if (n > SEP_MAX_GROUPS)
        return STATUS_INVALID_PARAMETER;
    SEP_GROUP *groups = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)n * sizeof(SEP_GROUP), KI_TAG('S', 'e', 'G', 'p'));
    if (!groups)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (ULONG i = 0; i < n && NT_SUCCESS(status); i++) {
        struct {
            const void *Sid;
            ULONG Attributes;
        } entry;
        status = read_caller(&entry, (const UCHAR *)user + 8 + 16 * (SIZE_T)i, sizeof(entry));
        if (!NT_SUCCESS(status))
            break;
        status = capture_sid(entry.Sid, groups[i].Sid);
        if (NT_SUCCESS(status) && (entry.Attributes & SE_GROUP_INVALID_BITS))
            status = STATUS_INVALID_PARAMETER;
        groups[i].Attributes = entry.Attributes;
    }
    if (!NT_SUCCESS(status)) {
        ExFreePool(groups);
        return status;
    }
    *out = groups;
    *count = n;
    return STATUS_SUCCESS;
}

/* a TOKEN_PRIVILEGES of the caller: the luids, which only count by their low part, and the attributes. */
static NTSTATUS capture_privileges(const void *user, ULONG **luids, ULONG **attributes, ULONG *count)
{
    *luids = *attributes = nullptr;
    *count = 0;
    if (!user)
        return STATUS_SUCCESS;
    ULONG n;
    NTSTATUS status = read_caller(&n, user, sizeof(n));
    if (!NT_SUCCESS(status) || !n)
        return status;
    if (n > SEP_MAX_GROUPS)
        return STATUS_INVALID_PARAMETER;
    ULONG *l = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)n * 8, KI_TAG('S', 'e', 'P', 'v'));
    if (!l)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (ULONG i = 0; i < n && NT_SUCCESS(status); i++) {
        ULONG entry[3];
        status = read_caller(entry, (const UCHAR *)user + 4 + 12 * (SIZE_T)i, sizeof(entry));
        l[i] = entry[1] ? ~0u : entry[0];
        l[n + i] = entry[2];
    }
    if (!NT_SUCCESS(status)) {
        ExFreePool(l);
        return status;
    }
    *luids = l;
    *attributes = l + n;
    *count = n;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ opening */

NTSTATUS NTAPI NtOpenProcessTokenEx(HANDLE process_handle, ACCESS_MASK access, ULONG attributes, PHANDLE token_handle)
{
    PEPROCESS process;
    NTSTATUS status = PsReferenceProcessHandle(process_handle, PROCESS_QUERY_LIMITED_INFORMATION, &process);
    if (!NT_SUCCESS(status))
        return status;
    PTOKEN t = SepReferencePrimaryToken(process);
    ObDereferenceObject(process);
    if (!t)
        return STATUS_NO_TOKEN;
    return SepInsertToken(t, access, attributes, token_handle);
}

NTSTATUS NTAPI NtOpenProcessToken(HANDLE process, ACCESS_MASK access, PHANDLE token)
{
    return NtOpenProcessTokenEx(process, access, 0, token);
}

NTSTATUS NTAPI NtOpenThreadTokenEx(HANDLE thread_handle, ACCESS_MASK access, BOOLEAN open_as_self, ULONG attributes,
                                   PHANDLE token_handle)
{
    PETHREAD thread;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(thread_handle, PsThreadTypeObject, KeGetPreviousMode(), (PVOID *)&thread, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (!(granted & THREAD_QUERY_LIMITED_INFORMATION)) {
        ObDereferenceObject(thread);
        return STATUS_ACCESS_DENIED;
    }
    PTOKEN dup = nullptr, t;
    ExAcquireFastMutex(&SeLock);
    t = thread->ImpersonationToken;
    if (!t) {
        status = STATUS_NO_TOKEN;
    } else if (thread->ImpersonationLevel == SecurityAnonymous) {
        status = STATUS_CANT_OPEN_ANONYMOUS;
    } else if (thread->CopyOnOpen) {
        /* the first opening makes the thread's token a copy, so the handle's changes are the thread's. */
        dup = SepDuplicate(t, TokenImpersonation, thread->ImpersonationLevel, thread->EffectiveOnly, &status);
        if (dup) {
            ObReferenceObject(dup);
            thread->ImpersonationToken = dup;
            thread->CopyOnOpen = FALSE;
            ObDereferenceObject(t);
            t = dup;
        }
    }
    if (NT_SUCCESS(status))
        ObReferenceObject(t);
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(thread);
    if (!NT_SUCCESS(status))
        return status;
    if (dup)
        ObDereferenceObject(dup);
    return SepInsertToken(t, access, attributes, token_handle);
}

NTSTATUS NTAPI NtOpenThreadToken(HANDLE thread, ACCESS_MASK access, BOOLEAN open_as_self, PHANDLE token)
{
    return NtOpenThreadTokenEx(thread, access, open_as_self, 0, token);
}

/* ------------------------------------------------------------------ copies and filters */

NTSTATUS NTAPI NtAllocateLocallyUniqueId(PLUID luid)
{
    ULONG64 value = SepAllocateLuid();
    return write_caller(luid, &value, sizeof(value));
}

NTSTATUS NTAPI NtDuplicateToken(HANDLE token_handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                BOOLEAN effective_only, ULONG type, PHANDLE new_token)
{
    if (KeGetPreviousMode() == UserMode && type != TokenPrimary && type != TokenImpersonation)
        return STATUS_INVALID_PARAMETER;
    OBJECT_ATTRIBUTES oa = {};
    BOOLEAN qos_present = FALSE;
    struct {
        ULONG Length, Level;
        UCHAR Tracking, Effective;
    } qos = {};
    if (attributes) {
        NTSTATUS status = read_caller(&oa, attributes, sizeof(oa));
        if (!NT_SUCCESS(status))
            return status;
        if (oa.SecurityQualityOfService) {
            status = read_caller(&qos, oa.SecurityQualityOfService, sizeof(qos));
            if (!NT_SUCCESS(status))
                return status;
            if (qos.Length != 12 || qos.Tracking >= 2)
                return STATUS_INVALID_PARAMETER;
            if (qos.Level > SecurityDelegation)
                return STATUS_BAD_IMPERSONATION_LEVEL;
            qos_present = TRUE;
        }
    }
    PTOKEN source;
    ACCESS_MASK granted;
    NTSTATUS status = SepReferenceTokenByHandle(token_handle, TOKEN_DUPLICATE, &source, &granted);
    if (!NT_SUCCESS(status))
        return status;
    PTOKEN dup = nullptr;
    ExAcquireFastMutex(&SeLock);
    ULONG level = qos_present ? qos.Level : source->ImpersonationLevel;
    if (source->Type == TokenImpersonation &&
        ((type == TokenImpersonation && level > source->ImpersonationLevel) ||
         (type == TokenPrimary && source->ImpersonationLevel < SecurityImpersonation)))
        status = STATUS_BAD_IMPERSONATION_LEVEL;
    else
        dup = SepDuplicate(source, type, level, effective_only, &status);
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(source);
    if (!dup)
        return status;
    return SepInsertToken(dup, access ? access : granted, oa.Attributes & OBJ_INHERIT, new_token);
}

/* ------------------------------------------------------------------ app containers */

#define SEP_LOWBOX_MAX 256

static UCHAR g_lowbox_sids[SEP_LOWBOX_MAX][SEP_SID_MAX];
static ULONG g_lowbox_count;

/* the number of an app container, the same for every token with its package sid; zero when the table is full. */
static ULONG lowbox_number(const UCHAR *package)
{
    for (ULONG i = 0; i < g_lowbox_count; i++)
        if (SepSidEqual(g_lowbox_sids[i], package))
            return i + 1;
    if (g_lowbox_count == SEP_LOWBOX_MAX)
        return 0;
    memcpy(g_lowbox_sids[g_lowbox_count], package, SepSidLength(package));
    return ++g_lowbox_count;
}

/* S-1-15-2 and eight sub authorities, or twelve for a child of a package; S-1-15-3 for a capability. */
static BOOLEAN sid_is_package(const UCHAR *sid)
{
    return sid[7] == 15 && !sid[2] && !sid[3] && !sid[4] && !sid[5] && !sid[6] && (sid[1] == 8 || sid[1] == 12) &&
           ((const ULONG *)(sid + 8))[0] == 2;
}

static BOOLEAN sid_is_capability(const UCHAR *sid)
{
    return sid[7] == 15 && !sid[2] && !sid[3] && !sid[4] && !sid[5] && !sid[6] && sid[1] >= 2 &&
           ((const ULONG *)(sid + 8))[0] == 3;
}

/*
 * a copy of a token that acts inside an app container: for the package, with the capabilities. the handles are
 * of the directories windows keeps the container's names in, which mime does not have; they are read and left
 * alone.
 */
NTSTATUS NTAPI NtCreateLowBoxToken(PHANDLE new_token, HANDLE token_handle, ACCESS_MASK access,
                                   POBJECT_ATTRIBUTES attributes, PVOID package_sid, ULONG capability_count,
                                   PVOID capabilities, ULONG handle_count, PHANDLE handles)
{
    if (!package_sid)
        return STATUS_INVALID_PARAMETER;
    if ((handle_count != 0) != (handles != nullptr) || (capability_count != 0) != (capabilities != nullptr))
        return STATUS_INVALID_PARAMETER_MIX;
    if (capability_count > SEP_MAX_GROUPS || handle_count > 0x10)
        return STATUS_INVALID_PARAMETER;
    OBJECT_ATTRIBUTES oa = {};
    NTSTATUS status = attributes ? read_caller(&oa, attributes, sizeof(oa)) : STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    HANDLE copies[0x10];
    if (handle_count) {
        status = read_caller(copies, handles, handle_count * sizeof(HANDLE));
        if (!NT_SUCCESS(status))
            return status;
    }
    PTOKEN source;
    ACCESS_MASK granted;
    status = SepReferenceTokenByHandle(token_handle, TOKEN_DUPLICATE, &source, &granted);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR package[SEP_SID_MAX];
    SEP_GROUP *caps = nullptr;
    status = capture_sid(package_sid, package);
    if (NT_SUCCESS(status) && !sid_is_package(package))
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && source->Type != TokenPrimary && source->ImpersonationLevel < SecurityImpersonation)
        status = STATUS_BAD_IMPERSONATION_LEVEL;
    if (NT_SUCCESS(status) && capability_count) {
        caps = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)capability_count * sizeof(SEP_GROUP), KI_TAG('S', 'e', 'C', 'p'));
        if (!caps)
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    for (ULONG i = 0; NT_SUCCESS(status) && i < capability_count; i++) {
        struct {
            const void *Sid;
            ULONG Attributes;
        } entry;
        status = read_caller(&entry, (const UCHAR *)capabilities + 16 * (SIZE_T)i, sizeof(entry));
        if (NT_SUCCESS(status))
            status = capture_sid(entry.Sid, caps[i].Sid);
        if (NT_SUCCESS(status) && (!sid_is_capability(caps[i].Sid) || (entry.Attributes & SE_GROUP_INVALID_BITS)))
            status = STATUS_INVALID_PARAMETER;
        /* none twice. */
        for (ULONG j = 0; NT_SUCCESS(status) && j < i; j++)
            if (SepSidEqual(caps[i].Sid, caps[j].Sid))
                status = STATUS_INVALID_PARAMETER;
        caps[i].Attributes = entry.Attributes;
    }
    PTOKEN t = nullptr;
    if (NT_SUCCESS(status)) {
        ExAcquireFastMutex(&SeLock);
        t = SepDuplicate(source, source->Type, source->ImpersonationLevel, FALSE, &status);
        if (t) {
            ULONG number = lowbox_number(package);
            if (!number) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                t->Flags |= TOKEN_LOWBOX;
                t->LowboxNumber = number;
                memcpy(t->AppContainerSid, package, SepSidLength(package));
                if (t->Capabilities)
                    ExFreePool(t->Capabilities);
                t->Capabilities = caps;
                t->CapabilityCount = capability_count;
                caps = nullptr;
                t->ParentTokenId = source->TokenId;
            }
        }
        ExReleaseFastMutex(&SeLock);
    }
    if (caps)
        ExFreePool(caps);
    ObDereferenceObject(source);
    if (t && !NT_SUCCESS(status)) {
        ObDereferenceObject(t);
        t = nullptr;
    }
    if (!t)
        return status;
    return SepInsertToken(t, access ? access : granted, oa.Attributes & OBJ_INHERIT, new_token);
}

NTSTATUS NTAPI NtFilterToken(HANDLE token_handle, ULONG flags, PVOID sids_to_disable, PVOID privileges_to_delete,
                             PVOID restricted_sids, PHANDLE new_token)
{
    SEP_GROUP *disable = nullptr, *restricting = nullptr;
    ULONG disable_count = 0, restrict_count = 0, delete_count = 0, *luids = nullptr, *attributes;
    NTSTATUS status = capture_groups(sids_to_disable, &disable, &disable_count);
    if (NT_SUCCESS(status))
        status = capture_privileges(privileges_to_delete, &luids, &attributes, &delete_count);
    if (NT_SUCCESS(status))
        status = capture_groups(restricted_sids, &restricting, &restrict_count);
    /* a sid to restricting with has no attributes of its own. */
    for (ULONG i = 0; NT_SUCCESS(status) && i < restrict_count; i++)
        if (restricting[i].Attributes)
            status = STATUS_INVALID_PARAMETER;
    PTOKEN source = nullptr, filtered = nullptr;
    if (NT_SUCCESS(status))
        status = SepReferenceTokenByHandle(token_handle, TOKEN_DUPLICATE, &source, nullptr);
    if (NT_SUCCESS(status)) {
        for (ULONG i = 0; i < restrict_count; i++) {
            /* packages and capabilities are not sids to restricting with. */
            ULONG first = restricting[i].Sid[1] ? ((ULONG *)(restricting[i].Sid + 8))[0] : 0;
            if (restricting[i].Sid[7] == 15 && restricting[i].Sid[1] && (first == 2 || first == 3))
                status = STATUS_INVALID_PARAMETER;
        }
    }
    if (NT_SUCCESS(status)) {
        ExAcquireFastMutex(&SeLock);
        status = SepFilter(source, flags, disable_count, disable, delete_count, luids, restrict_count, restricting,
                           &filtered);
        PTOKEN process_token = PsCurrentProcess()->Token;
        if (NT_SUCCESS(status) && (flags & SANDBOX_INERT) && process_token && SepIsSystemToken(process_token))
            filtered->Flags |= TOKEN_SANDBOX_INERT;
        ExReleaseFastMutex(&SeLock);
    }
    if (source)
        ObDereferenceObject(source);
    if (disable)
        ExFreePool(disable);
    if (restricting)
        ExFreePool(restricting);
    if (luids)
        ExFreePool(luids);
    if (!NT_SUCCESS(status))
        return status;
    return SepInsertToken(filtered, TOKEN_ALL_ACCESS, 0, new_token);
}

/* ------------------------------------------------------------------ querying */

typedef struct OUTPUT {
    PUCHAR Data; /* NULL while only measuring */
    ULONG_PTR Base;
    ULONG Size;
} OUTPUT;

static ULONG out_alloc(OUTPUT *o, ULONG bytes, ULONG alignment)
{
    ULONG at = (o->Size + alignment - 1) & ~(alignment - 1);
    o->Size = at + bytes;
    return at;
}

static void out_write(OUTPUT *o, ULONG at, const void *source, ULONG bytes)
{
    if (o->Data)
        memcpy(o->Data + at, source, bytes);
}

static void out_ulong(OUTPUT *o, ULONG at, ULONG value)
{
    out_write(o, at, &value, sizeof(value));
}

static void out_pointer(OUTPUT *o, ULONG at, ULONG target)
{
    ULONG_PTR p = o->Base + target;
    out_write(o, at, &p, sizeof(p));
}

/* a sid_and_attributes of the given sid, the sid placed after it where the caller said. */
static void out_sid_entry(OUTPUT *o, ULONG entry, const void *sid, ULONG attributes)
{
    ULONG at = out_alloc(o, SepSidLength(sid), 4);
    out_write(o, at, sid, SepSidLength(sid));
    out_pointer(o, entry, at);
    out_ulong(o, entry + 8, attributes);
}

static void out_groups(OUTPUT *o, const SEP_GROUP *groups, ULONG count, ULONG start)
{
    ULONG head = out_alloc(o, 8 + 16 * (count - start), 8);
    out_ulong(o, head, count - start);
    for (ULONG i = start; i < count; i++)
        out_sid_entry(o, head + 8 + 16 * (i - start), groups[i].Sid, groups[i].Attributes);
}

/* a pointer to a sid, or null, then the sid. */
static void out_sid_pointer(OUTPUT *o, const void *sid)
{
    ULONG head = out_alloc(o, 8, 8);
    if (sid) {
        ULONG at = out_alloc(o, SepSidLength(sid), 4);
        out_write(o, at, sid, SepSidLength(sid));
        out_pointer(o, head, at);
    }
}

/* the privileges as an array of luids and attributes, in luid order; their number. */
static ULONG out_privilege_entries(OUTPUT *o, ULONG at, const TOKEN *t)
{
    ULONG n = 0;
    for (ULONG luid = SEP_PRIVILEGE_FIRST; luid <= SEP_PRIVILEGE_LAST; luid++) {
        if (!(t->Present & (1ull << luid)))
            continue;
        ULONG entry[3] = {luid, 0, ((t->Enabled >> luid) & 1 ? SE_PRIVILEGE_ENABLED : 0) |
                                       ((t->EnabledByDefault >> luid) & 1 ? SE_PRIVILEGE_ENABLED_BY_DEFAULT : 0)};
        out_write(o, at + 12 * n, entry, sizeof(entry));
        n++;
    }
    return n;
}

static void out_bytes_ulong(OUTPUT *o, ULONG value)
{
    out_ulong(o, out_alloc(o, 4, 4), value);
}

/*
 * what NtQueryInformationToken answers, in the layouts of the 64-bit TOKEN_ structures. the sizes are those of
 * windows' answers, byte for byte. the lock is held.
 */
static NTSTATUS query_fill(const TOKEN *t, ULONG klass, ULONG length, OUTPUT *o)
{
    ULONG at, n;
    switch (klass) {
    case TokenUser:
        at = out_alloc(o, 16, 8);
        out_sid_entry(o, at, t->Groups[0].Sid, t->Groups[0].Attributes);
        return STATUS_SUCCESS;
    case TokenGroups:
        out_groups(o, t->Groups, t->GroupCount, 1);
        return STATUS_SUCCESS;
    case TokenPrivileges:
        n = SepPrivilegeCount(t);
        at = out_alloc(o, n > 1 ? 4 + 12 * n : 16, 4);
        out_ulong(o, at, out_privilege_entries(o, at + 4, t));
        return STATUS_SUCCESS;
    case TokenOwner:
        out_sid_pointer(o, t->Groups[t->OwnerIndex].Sid);
        return STATUS_SUCCESS;
    case TokenPrimaryGroup:
        out_sid_pointer(o, t->PrimaryGroup);
        return STATUS_SUCCESS;
    case TokenDefaultDacl:
        at = out_alloc(o, 8, 8);
        if (t->DefaultDacl) {
            ULONG acl = out_alloc(o, t->DefaultDaclSize, 4);
            out_write(o, acl, t->DefaultDacl, t->DefaultDaclSize);
            out_pointer(o, at, acl);
        }
        return STATUS_SUCCESS;
    case TokenSource:
        out_write(o, out_alloc(o, 16, 8), t->Source, 16);
        return STATUS_SUCCESS;
    case TokenType:
        out_bytes_ulong(o, t->Type);
        return STATUS_SUCCESS;
    case TokenImpersonationLevel:
        if (t->Type != TokenImpersonation)
            return STATUS_INVALID_INFO_CLASS;
        out_bytes_ulong(o, t->ImpersonationLevel);
        return STATUS_SUCCESS;
    case TokenStatistics: {
        at = out_alloc(o, 56, 8);
        ULONG used = SepSidLength(t->PrimaryGroup) + (t->DefaultDacl ? t->DefaultDaclSize : 0);
        out_write(o, at, &t->TokenId, 8);
        out_write(o, at + 8, &t->AuthenticationId, 8);
        out_write(o, at + 16, &t->ExpirationTime, 8);
        out_ulong(o, at + 24, t->Type);
        out_ulong(o, at + 28, t->ImpersonationLevel);
        out_ulong(o, at + 32, t->DynamicCharged);
        out_ulong(o, at + 36, t->DynamicCharged > used ? t->DynamicCharged - used : 0);
        out_ulong(o, at + 40, t->GroupCount - 1);
        out_ulong(o, at + 44, SepPrivilegeCount(t));
        out_write(o, at + 48, &t->ModifiedId, 8);
        return STATUS_SUCCESS;
    }
    case TokenRestrictedSids:
        out_groups(o, t->Restricted, t->RestrictedCount, 0);
        return STATUS_SUCCESS;
    case TokenSessionId:
        out_bytes_ulong(o, t->SessionId);
        return STATUS_SUCCESS;
    case TokenGroupsAndPrivileges: {
        n = SepPrivilegeCount(t);
        at = out_alloc(o, 56, 8);
        /* the sids rounded up to eight bytes, each array after the other. */
        ULONG sid_bytes = 16 * t->GroupCount, restricted_bytes = 16 * t->RestrictedCount;
        for (ULONG i = 0; i < t->GroupCount; i++)
            sid_bytes += (SepSidLength(t->Groups[i].Sid) + 7) & ~7u;
        for (ULONG i = 0; i < t->RestrictedCount; i++)
            restricted_bytes += (SepSidLength(t->Restricted[i].Sid) + 7) & ~7u;
        ULONG sids = out_alloc(o, sid_bytes, 8);
        ULONG restricted = t->RestrictedCount ? out_alloc(o, restricted_bytes, 8) : 0;
        ULONG privileges = out_alloc(o, 12 * n, 4);
        out_ulong(o, at, t->GroupCount);
        out_ulong(o, at + 4, sid_bytes);
        out_pointer(o, at + 8, sids);
        out_ulong(o, at + 16, t->RestrictedCount);
        out_ulong(o, at + 20, restricted_bytes);
        if (t->RestrictedCount)
            out_pointer(o, at + 24, restricted);
        out_ulong(o, at + 32, n);
        out_ulong(o, at + 36, 12 * n);
        out_pointer(o, at + 40, privileges);
        out_write(o, at + 48, &t->AuthenticationId, 8);
        ULONG cursor = sids + 16 * t->GroupCount;
        for (ULONG i = 0; i < t->GroupCount; i++) {
            ULONG bytes = SepSidLength(t->Groups[i].Sid);
            out_pointer(o, sids + 16 * i, cursor);
            out_ulong(o, sids + 16 * i + 8, t->Groups[i].Attributes);
            out_write(o, cursor, t->Groups[i].Sid, bytes);
            cursor += (bytes + 7) & ~7u;
        }
        cursor = restricted + 16 * t->RestrictedCount;
        for (ULONG i = 0; i < t->RestrictedCount; i++) {
            ULONG bytes = SepSidLength(t->Restricted[i].Sid);
            out_pointer(o, restricted + 16 * i, cursor);
            out_ulong(o, restricted + 16 * i + 8, t->Restricted[i].Attributes);
            out_write(o, cursor, t->Restricted[i].Sid, bytes);
            cursor += (bytes + 7) & ~7u;
        }
        out_privilege_entries(o, privileges, t);
        return STATUS_SUCCESS;
    }
    case TokenSandBoxInert:
        out_bytes_ulong(o, (t->Flags & TOKEN_SANDBOX_INERT) != 0);
        return STATUS_SUCCESS;
    case TokenAuditPolicy:
        out_alloc(o, 31, 1);
        return STATUS_SUCCESS;
    case TokenOrigin:
        out_write(o, out_alloc(o, 8, 8), &t->Origin, 8);
        return STATUS_SUCCESS;
    case TokenElevationType:
        out_bytes_ulong(o, t->ElevationType);
        return STATUS_SUCCESS;
    case TokenElevation:
        out_bytes_ulong(o, SepElevated(t));
        return STATUS_SUCCESS;
    case TokenHasRestrictions:
        at = out_alloc(o, 1, 1);
        if (o->Data)
            o->Data[at] = (t->Flags & (TOKEN_IS_RESTRICTED | TOKEN_IS_FILTERED)) != 0;
        return STATUS_SUCCESS;
    case TokenVirtualizationAllowed:
        out_bytes_ulong(o, (t->Flags & TOKEN_VIRTUALIZE_ALLOWED) != 0);
        return STATUS_SUCCESS;
    case TokenVirtualizationEnabled:
        out_bytes_ulong(o, (t->Flags & TOKEN_VIRTUALIZE_ENABLED) != 0);
        return STATUS_SUCCESS;
    case TokenIntegrityLevel: {
        static const UCHAR untrusted[12] = {1, 1, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0};
        at = out_alloc(o, 16, 8);
        if (t->IntegrityIndex < 0)
            out_sid_entry(o, at, untrusted, SE_GROUP_LABEL);
        else
            out_sid_entry(o, at, t->Groups[t->IntegrityIndex].Sid, t->Groups[t->IntegrityIndex].Attributes);
        return STATUS_SUCCESS;
    }
    case TokenUIAccess:
        out_bytes_ulong(o, (t->Flags & TOKEN_UIACCESS) != 0);
        return STATUS_SUCCESS;
    case TokenMandatoryPolicy:
        out_bytes_ulong(o, t->MandatoryPolicy);
        return STATUS_SUCCESS;
    case TokenLogonSid: {
        for (n = 0; n < t->GroupCount; n++)
            if ((t->Groups[n].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
                break;
        if (n == t->GroupCount)
            return STATUS_NOT_FOUND;
        at = out_alloc(o, 8 + 16, 8);
        out_ulong(o, at, 1);
        out_sid_entry(o, at + 8, t->Groups[n].Sid, t->Groups[n].Attributes);
        return STATUS_SUCCESS;
    }
    case TokenIsAppContainer:
        out_bytes_ulong(o, (t->Flags & TOKEN_LOWBOX) != 0);
        return STATUS_SUCCESS;
    case TokenCapabilities:
        out_groups(o, t->Capabilities, t->CapabilityCount, 0);
        return STATUS_SUCCESS;
    case TokenAppContainerSid:
        out_sid_pointer(o, (t->Flags & TOKEN_LOWBOX) ? t->AppContainerSid : nullptr);
        return STATUS_SUCCESS;
    case TokenAppContainerNumber:
        out_bytes_ulong(o, t->LowboxNumber);
        return STATUS_SUCCESS;
    case TokenUserClaimAttributes:
    case TokenDeviceClaimAttributes:
    case TokenSecurityAttributes:
        /* no claims: a version, an attribute count of zero and no list. */
        at = out_alloc(o, 16, 8);
        out_ulong(o, at, 1);
        return STATUS_SUCCESS;
    case TokenDeviceGroups:
        out_alloc(o, 24, 8);
        return STATUS_SUCCESS;
    case TokenIsRestricted:
        out_bytes_ulong(o, (t->Flags & (TOKEN_IS_RESTRICTED | TOKEN_WRITE_RESTRICTED)) != 0);
        return STATUS_SUCCESS;
    case TokenProcessTrustLevel:
        out_alloc(o, 8, 8);
        return STATUS_SUCCESS;
    case TokenBnoIsolation:
        out_alloc(o, 16, 8);
        return STATUS_SUCCESS;
    case TokenIsSandboxed:
        out_bytes_ulong(o, (t->Flags & (TOKEN_LOWBOX | TOKEN_IS_RESTRICTED)) != 0);
        return STATUS_SUCCESS;
    case TokenIsAppSilo:
        out_bytes_ulong(o, 0);
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

/* the required access of a class: the source needs a right of its own. */
static ACCESS_MASK query_access(ULONG klass)
{
    return klass == TokenSource ? TOKEN_QUERY_SOURCE : TOKEN_QUERY;
}

/* the handle of a linked token: a copy that can only identify, or a primary one for the trusted. */
static NTSTATUS query_linked(PTOKEN token, ULONG length, PVOID info, PULONG returned)
{
    NTSTATUS status = write_length(returned, 8);
    if (!NT_SUCCESS(status))
        return status;
    if (length != 8)
        return STATUS_INFO_LENGTH_MISMATCH;
    BOOLEAN trusted = SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, KeGetPreviousMode());
    PTOKEN dup = nullptr;
    ExAcquireFastMutex(&SeLock);
    if (!token->Linked)
        status = STATUS_NO_SUCH_LOGON_SESSION;
    else
        dup = SepDuplicate(token->Linked, trusted ? TokenPrimary : TokenImpersonation, SecurityIdentification, FALSE,
                           &status);
    ExReleaseFastMutex(&SeLock);
    if (!dup)
        return status;
    HANDLE h;
    status = ObInsertObject(dup, TOKEN_ALL_ACCESS, 0, &h);
    return NT_SUCCESS(status) ? ObStoreHandle(info, h) : status;
}

NTSTATUS NTAPI NtQueryInformationToken(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (!returned)
        return STATUS_ACCESS_VIOLATION;
    PTOKEN token;
    NTSTATUS status = SepReferenceTokenByHandle(handle, query_access(klass), &token, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (klass == TokenAuditPolicy && !SepSinglePrivilegeCheck(SE_SECURITY_PRIVILEGE, KeGetPreviousMode())) {
        ObDereferenceObject(token);
        return STATUS_PRIVILEGE_NOT_HELD;
    }
    if (klass == TokenElevation && length != 4) {
        ObDereferenceObject(token);
        write_length(returned, 4);
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (klass == TokenLinkedToken) {
        status = query_linked(token, length, info, returned);
        ObDereferenceObject(token);
        return status;
    }
    PUCHAR buffer = nullptr;
    ULONG capacity = 0;
    for (;;) {
        /* measured and filled under one hold of the lock, so the buffer always fits. */
        OUTPUT measure = {nullptr, (ULONG_PTR)info, 0};
        ExAcquireFastMutex(&SeLock);
        status = query_fill(token, klass, length, &measure);
        if (NT_SUCCESS(status) && measure.Size <= length && measure.Size <= capacity) {
            OUTPUT fill = {buffer, (ULONG_PTR)info, 0};
            query_fill(token, klass, length, &fill);
        }
        ExReleaseFastMutex(&SeLock);
        if (!NT_SUCCESS(status))
            break;
        status = write_length(returned, measure.Size);
        if (!NT_SUCCESS(status))
            break;
        if (length < measure.Size) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        if (measure.Size <= capacity) {
            status = write_caller(info, buffer, measure.Size);
            break;
        }
        if (buffer)
            ExFreePool(buffer);
        capacity = measure.Size;
        buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, TAG_OUT);
        if (!buffer) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
    }
    if (buffer)
        ExFreePool(buffer);
    ObDereferenceObject(token);
    return status;
}

/* ------------------------------------------------------------------ setting */

static ULONG sid_rid_of(const UCHAR *sid)
{
    return sid[1] ? ((const ULONG *)(sid + 8))[sid[1] - 1] : 0;
}

/* a label sid, S-1-16-rid. */
static BOOLEAN is_label(const UCHAR *sid)
{
    return sid[1] == 1 && sid[7] == 16 && !sid[2] && !sid[3] && !sid[4] && !sid[5] && !sid[6];
}

NTSTATUS NTAPI NtSetInformationToken(HANDLE handle, ULONG klass, PVOID info, ULONG length)
{
    /* the classes that can be set. */
    static const ULONG64 settable = 0x41024802F8B5070ull;
    if (klass > 63 || !((settable >> klass) & 1))
        return STATUS_INVALID_INFO_CLASS;
    ACCESS_MASK required = klass == TokenSessionId ? TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID
                           : klass == TokenLinkedToken ? TOKEN_ADJUST_DEFAULT | TOKEN_QUERY
                                                       : TOKEN_ADJUST_DEFAULT;
    PTOKEN t;
    NTSTATUS status = SepReferenceTokenByHandle(handle, required, &t, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    switch (klass) {
    case TokenOwner: {
        UCHAR sid[SEP_SID_MAX];
        const void *user;
        if (length < sizeof(PVOID)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&user, info, sizeof(user));
        if (NT_SUCCESS(status))
            status = capture_sid(user, sid);
        if (!NT_SUCCESS(status))
            break;
        ExAcquireFastMutex(&SeLock);
        ULONG i;
        for (i = 0; i < t->GroupCount; i++)
            if (SepSidEqual(sid, t->Groups[i].Sid))
                break;
        /* the user, or a group marked as able to own. */
        if (i < t->GroupCount && (i == 0 || (t->Groups[i].Attributes & SE_GROUP_OWNER))) {
            t->OwnerIndex = i;
            t->ModifiedId = SepAllocateLuid();
        } else {
            status = STATUS_INVALID_OWNER;
        }
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenPrimaryGroup: {
        UCHAR sid[SEP_SID_MAX];
        const void *user;
        if (length < sizeof(PVOID)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&user, info, sizeof(user));
        if (NT_SUCCESS(status))
            status = capture_sid(user, sid);
        if (!NT_SUCCESS(status))
            break;
        ExAcquireFastMutex(&SeLock);
        ULONG i;
        for (i = 0; i < t->GroupCount; i++)
            if (SepSidEqual(sid, t->Groups[i].Sid))
                break;
        if (i == t->GroupCount) {
            status = STATUS_INVALID_PRIMARY_GROUP;
        } else if (SepSidLength(sid) + (t->DefaultDacl ? t->DefaultDaclSize : 0) > t->DynamicCharged) {
            status = STATUS_ALLOTTED_SPACE_EXCEEDED;
        } else {
            memset(t->PrimaryGroup, 0, sizeof(t->PrimaryGroup));
            memcpy(t->PrimaryGroup, sid, SepSidLength(sid));
            t->ModifiedId = SepAllocateLuid();
        }
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenDefaultDacl: {
        const void *user;
        PUCHAR acl = nullptr;
        ULONG size = 0;
        if (length < sizeof(PVOID)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&user, info, sizeof(user));
        if (NT_SUCCESS(status) && user) {
            /* the size is in the acl's own header, with room for what the aces claim. */
            USHORT header[4];
            status = read_caller(header, user, sizeof(header));
            size = header[1];
            if (NT_SUCCESS(status) && (size < 8 || (((PUCHAR)header)[0] < 2 || ((PUCHAR)header)[0] > 4)))
                status = STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(status)) {
                acl = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('S', 'e', 'A', 'c'));
                status = acl ? read_caller(acl, user, size) : STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        if (!NT_SUCCESS(status)) {
            if (acl)
                ExFreePool(acl);
            break;
        }
        ExAcquireFastMutex(&SeLock);
        if (size + SepSidLength(t->PrimaryGroup) > t->DynamicCharged) {
            status = STATUS_ALLOTTED_SPACE_EXCEEDED;
        } else {
            PUCHAR old = t->DefaultDacl;
            t->DefaultDacl = acl;
            t->DefaultDaclSize = size;
            acl = old;
            t->ModifiedId = SepAllocateLuid();
        }
        ExReleaseFastMutex(&SeLock);
        if (acl)
            ExFreePool(acl);
        break;
    }
    case TokenSessionId: {
        ULONG session;
        if (length != 4) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&session, info, sizeof(session));
        if (!NT_SUCCESS(status))
            break;
        if (!SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode)) {
            status = STATUS_PRIVILEGE_NOT_HELD;
            break;
        }
        ExAcquireFastMutex(&SeLock);
        if (t->InUse)
            status = STATUS_TOKEN_ALREADY_IN_USE;
        else
            t->SessionId = session;
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenVirtualizationAllowed:
    case TokenUIAccess:
    case TokenVirtualizationEnabled: {
        ULONG value;
        if (length != 4) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&value, info, sizeof(value));
        if (!NT_SUCCESS(status))
            break;
        /* allowing virtualization takes the right to create tokens, ui access the tcb privilege to turn on. */
        if ((klass == TokenVirtualizationAllowed && !SepSinglePrivilegeCheck(SE_CREATE_TOKEN_PRIVILEGE, mode)) ||
            (klass == TokenUIAccess && value && !SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode))) {
            status = STATUS_PRIVILEGE_NOT_HELD;
            break;
        }
        ExAcquireFastMutex(&SeLock);
        if (klass == TokenVirtualizationAllowed) {
            t->Flags = value ? t->Flags | TOKEN_VIRTUALIZE_ALLOWED : t->Flags & ~TOKEN_VIRTUALIZE_ALLOWED;
        } else if (klass == TokenUIAccess) {
            t->Flags = value ? t->Flags | TOKEN_UIACCESS : t->Flags & ~TOKEN_UIACCESS;
        } else if (!value) {
            t->Flags &= ~TOKEN_VIRTUALIZE_ENABLED;
        } else if (t->Flags & TOKEN_VIRTUALIZE_ALLOWED) {
            t->Flags |= TOKEN_VIRTUALIZE_ENABLED;
        }
        t->ModifiedId = SepAllocateLuid();
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenIntegrityLevel: {
        struct {
            const void *Sid;
            ULONG Attributes;
        } label;
        UCHAR sid[SEP_SID_MAX];
        if (length < 16) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&label, info, sizeof(label));
        if (NT_SUCCESS(status))
            status = capture_sid(label.Sid, sid);
        if (!NT_SUCCESS(status))
            break;
        if (!is_label(sid) || sid_rid_of(sid) > 0x4000) {
            status = STATUS_INVALID_LABEL;
            break;
        }
        ULONG rid = sid_rid_of(sid);
        /* asked before the lock, which the check takes too. */
        BOOLEAN trusted = SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode);
        ExAcquireFastMutex(&SeLock);
        if (t->IntegrityIndex < 0) {
            status = STATUS_INVALID_LABEL;
        } else {
            ULONG current = sid_rid_of(t->Groups[t->IntegrityIndex].Sid);
            if (rid > current && !trusted) {
                status = STATUS_PRIVILEGE_NOT_HELD;
            } else if (rid > current && t->InUse) {
                status = STATUS_TOKEN_ALREADY_IN_USE;
            } else {
                if (rid < current)
                    t->Flags &= ~TOKEN_UIACCESS;
                memcpy(t->Groups[t->IntegrityIndex].Sid, sid, SepSidLength(sid));
                /* below high the lua rules take privileges away; below medium the token is low. */
                if (rid < 0x3000) {
                    t->Enabled &= 0xFFFFFFEEDFE9F97Bull;
                    t->EnabledByDefault &= 0xFFFFFFEEDFE9F97Bull;
                }
                if (rid >= 0x2000) {
                    t->Flags |= TOKEN_NOT_LOW;
                } else {
                    t->Enabled &= 0x202800000ull;
                    t->EnabledByDefault &= 0x202800000ull;
                    t->Flags &= ~TOKEN_NOT_LOW;
                }
                t->ModifiedId = SepAllocateLuid();
            }
        }
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenMandatoryPolicy: {
        ULONG policy;
        if (length != 4) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&policy, info, sizeof(policy));
        if (!NT_SUCCESS(status))
            break;
        if (!SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode)) {
            status = STATUS_PRIVILEGE_NOT_HELD;
            break;
        }
        ExAcquireFastMutex(&SeLock);
        if (t->InUse)
            status = STATUS_TOKEN_ALREADY_IN_USE;
        else if (policy & ~3u)
            status = STATUS_INVALID_PARAMETER;
        else {
            t->MandatoryPolicy = policy;
            t->ModifiedId = SepAllocateLuid();
        }
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenOrigin: {
        ULONG64 origin;
        if (length != 8) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&origin, info, sizeof(origin));
        if (!NT_SUCCESS(status))
            break;
        if (!SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode)) {
            status = STATUS_PRIVILEGE_NOT_HELD;
            break;
        }
        ExAcquireFastMutex(&SeLock);
        if (!t->Origin)
            t->Origin = origin;
        t->ModifiedId = SepAllocateLuid();
        ExReleaseFastMutex(&SeLock);
        break;
    }
    case TokenSessionReference: {
        ULONG value;
        if (length != 4) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = read_caller(&value, info, sizeof(value));
        if (!NT_SUCCESS(status))
            break;
        if (!SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode))
            status = STATUS_PRIVILEGE_NOT_HELD;
        else if (value)
            status = STATUS_INVALID_PARAMETER;
        else
            t->Flags |= TOKEN_SESSION_NOT_REFERENCED;
        break;
    }
    case TokenAuditPolicy:
        status = SepSinglePrivilegeCheck(SE_TCB_PRIVILEGE, mode) ? STATUS_SUCCESS : STATUS_PRIVILEGE_NOT_HELD;
        break;
    case TokenLinkedToken:
        status = length == 8 ? STATUS_PRIVILEGE_NOT_HELD : STATUS_INFO_LENGTH_MISMATCH;
        break;
    default:
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    ObDereferenceObject(t);
    return status;
}

/* ------------------------------------------------------------------ privileges and groups */

NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE handle, BOOLEAN disable_all, PVOID state, ULONG length, PVOID previous,
                                       PULONG returned)
{
    ULONG *luids = nullptr, *attributes = nullptr, count = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!disable_all) {
        if (!state)
            return STATUS_INVALID_PARAMETER;
        status = capture_privileges(state, &luids, &attributes, &count);
        if (!NT_SUCCESS(status))
            return status;
    }
    PTOKEN t;
    NTSTATUS referenced = SepReferenceTokenByHandle(handle, TOKEN_ADJUST_PRIVILEGES | (previous ? TOKEN_QUERY : 0), &t, nullptr);
    if (!NT_SUCCESS(referenced)) {
        if (luids)
            ExFreePool(luids);
        return referenced;
    }
    /* the privileges whose state changes, with the state they had. */
    ULONG changed_luid[SEP_PRIVILEGE_LAST + 1], changed_state[SEP_PRIVILEGE_LAST + 1], changes = 0;
    BOOLEAN missing = FALSE;
    ExAcquireFastMutex(&SeLock);
    ULONG64 enabled = t->Enabled, present = t->Present, default_enabled = t->EnabledByDefault;
    if (disable_all) {
        for (ULONG luid = SEP_PRIVILEGE_FIRST; luid <= SEP_PRIVILEGE_LAST; luid++) {
            if ((enabled >> luid) & 1) {
                changed_luid[changes] = luid;
                changed_state[changes++] = SE_PRIVILEGE_ENABLED | ((default_enabled >> luid) & 1);
            }
        }
        enabled = 0;
    } else {
        for (ULONG i = 0; i < count; i++) {
            ULONG luid = luids[i];
            if (luid < SEP_PRIVILEGE_FIRST || luid > SEP_PRIVILEGE_LAST || !((present >> luid) & 1)) {
                missing = TRUE;
                continue;
            }
            ULONG64 bit = 1ull << luid;
            ULONG old = ((enabled & bit) ? SE_PRIVILEGE_ENABLED : 0) | ((default_enabled & bit) ? SE_PRIVILEGE_ENABLED_BY_DEFAULT : 0);
            BOOLEAN removing = (attributes[i] & SE_PRIVILEGE_REMOVED) != 0;
            BOOLEAN enabling = !removing && (attributes[i] & SE_PRIVILEGE_ENABLED);
            if (!removing && !!(enabled & bit) == enabling)
                continue;
            BOOLEAN listed = FALSE;
            for (ULONG j = 0; j < changes; j++)
                listed |= changed_luid[j] == luid;
            if (!listed) {
                changed_luid[changes] = luid;
                changed_state[changes++] = old;
            }
            if (removing) {
                present &= ~bit;
                enabled &= ~bit;
                default_enabled &= ~bit;
            } else if (enabling) {
                enabled |= bit;
            } else {
                enabled &= ~bit;
            }
        }
    }
    ULONG needed = 4 + 12 * changes;
    if (previous) {
        if (length < needed)
            status = STATUS_BUFFER_TOO_SMALL;
    }
    if (NT_SUCCESS(status)) {
        t->Enabled = enabled;
        t->Present = present;
        t->EnabledByDefault = default_enabled;
        if (changes)
            t->ModifiedId = SepAllocateLuid();
    }
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(t);
    if (luids)
        ExFreePool(luids);
    if (NT_SUCCESS(status) && previous) {
        PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, needed, TAG_OUT);
        if (!out)
            return STATUS_INSUFFICIENT_RESOURCES;
        *(PULONG)out = changes;
        for (ULONG i = 0; i < changes; i++) {
            ULONG entry[3] = {changed_luid[i], 0, changed_state[i]};
            memcpy(out + 4 + 12 * i, entry, 12);
        }
        status = write_caller(previous, out, needed);
        ExFreePool(out);
    }
    if (returned && (previous || status == STATUS_BUFFER_TOO_SMALL)) {
        NTSTATUS written = write_length(returned, needed);
        if (!NT_SUCCESS(written))
            return written;
    } else if (returned) {
        NTSTATUS written = write_length(returned, 0);
        if (!NT_SUCCESS(written))
            return written;
    }
    if (NT_SUCCESS(status) && missing)
        return STATUS_NOT_ALL_ASSIGNED;
    return status;
}

NTSTATUS NTAPI NtAdjustGroupsToken(HANDLE handle, BOOLEAN reset_to_default, PVOID state, ULONG length, PVOID previous,
                                   PULONG returned)
{
    SEP_GROUP *wanted = nullptr;
    ULONG count = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (!reset_to_default) {
        if (!state)
            return STATUS_INVALID_PARAMETER;
        status = capture_groups(state, &wanted, &count);
        if (!NT_SUCCESS(status))
            return status;
    }
    PTOKEN t;
    NTSTATUS referenced = SepReferenceTokenByHandle(handle, TOKEN_ADJUST_GROUPS | (previous ? TOKEN_QUERY : 0), &t, nullptr);
    if (!NT_SUCCESS(referenced)) {
        if (wanted)
            ExFreePool(wanted);
        return referenced;
    }
    SEP_GROUP *old = nullptr;
    ULONG changes = 0;
    BOOLEAN missing = FALSE;
    PUCHAR found = ExAllocatePool2(POOL_FLAG_NON_PAGED, count ? count : 1, KI_TAG('S', 'e', 'G', 'f'));
    if (!found) {
        ObDereferenceObject(t);
        if (wanted)
            ExFreePool(wanted);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ExAcquireFastMutex(&SeLock);
    /* the groups as they will be, kept apart until the whole request is known to work. */
    SEP_GROUP *next = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)t->GroupCount * sizeof(SEP_GROUP) * 2, KI_TAG('S', 'e', 'G', 'a'));
    if (!next) {
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(t);
        ExFreePool(found);
        if (wanted)
            ExFreePool(wanted);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    old = next + t->GroupCount;
    memcpy(next, t->Groups, (SIZE_T)t->GroupCount * sizeof(SEP_GROUP));
    for (ULONG i = 1; i < t->GroupCount && NT_SUCCESS(status); i++) {
        SEP_GROUP *g = &next[i];
        if (g->Attributes & (SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY))
            continue;
        ULONG attributes = g->Attributes;
        if (reset_to_default) {
            attributes = (attributes & SE_GROUP_ENABLED_BY_DEFAULT) ? attributes | SE_GROUP_ENABLED : attributes & ~SE_GROUP_ENABLED;
        } else {
            for (ULONG j = 0; j < count; j++) {
                if (!SepSidEqual(g->Sid, wanted[j].Sid))
                    continue;
                if (wanted[j].Attributes & SE_GROUP_ENABLED)
                    attributes |= SE_GROUP_ENABLED;
                else if (g->Attributes & SE_GROUP_MANDATORY)
                    status = STATUS_CANT_DISABLE_MANDATORY;
                else
                    attributes &= ~SE_GROUP_ENABLED;
                found[j] = TRUE;
            }
        }
        if (attributes != g->Attributes) {
            old[changes] = *g;
            changes++;
            g->Attributes = attributes;
        }
    }
    for (ULONG j = 0; j < count; j++)
        if (!found[j])
            missing = TRUE;
    ULONG needed = 8 + 16 * changes;
    for (ULONG i = 0; i < changes; i++)
        needed += SepSidLength(old[i].Sid);
    if (NT_SUCCESS(status) && previous && length < needed)
        status = STATUS_BUFFER_TOO_SMALL;
    if (NT_SUCCESS(status)) {
        memcpy(t->Groups, next, (SIZE_T)t->GroupCount * sizeof(SEP_GROUP));
        if (changes)
            t->ModifiedId = SepAllocateLuid();
    }
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(t);
    ExFreePool(found);
    if (wanted)
        ExFreePool(wanted);
    if (NT_SUCCESS(status) && previous) {
        OUTPUT o = {nullptr, (ULONG_PTR)previous, 0};
        PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, needed, TAG_OUT);
        if (out) {
            o.Data = out;
            out_groups(&o, old, changes, 0);
            status = write_caller(previous, out, needed);
            ExFreePool(out);
        } else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    ExFreePool(next);
    if (returned && (previous || status == STATUS_BUFFER_TOO_SMALL))
        write_length(returned, needed);
    if (NT_SUCCESS(status) && missing)
        return STATUS_NOT_ALL_ASSIGNED;
    return status;
}

/* ------------------------------------------------------------------ impersonation */

/* ThreadImpersonationToken: a token handle, or none to go back to the process's identity. */
NTSTATUS SepSetThreadToken(HANDLE thread_handle, PVOID info, ULONG length)
{
    if (length != sizeof(HANDLE))
        return STATUS_INFO_LENGTH_MISMATCH;
    HANDLE token_handle;
    NTSTATUS status = read_caller(&token_handle, info, sizeof(token_handle));
    if (!NT_SUCCESS(status))
        return status;
    PETHREAD thread;
    ACCESS_MASK granted;
    status = ObReferenceHandle(thread_handle, PsThreadTypeObject, KeGetPreviousMode(), (PVOID *)&thread, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (!(granted & THREAD_SET_THREAD_TOKEN)) {
        ObDereferenceObject(thread);
        return STATUS_ACCESS_DENIED;
    }
    if (!token_handle) {
        status = SepImpersonateClient(thread, nullptr, FALSE, FALSE, SecurityImpersonation);
    } else {
        PTOKEN token;
        status = SepReferenceTokenByHandle(token_handle, TOKEN_IMPERSONATE, &token, nullptr);
        if (NT_SUCCESS(status)) {
            ULONG level = token->ImpersonationLevel;
            if (token->Type != TokenImpersonation)
                status = STATUS_BAD_TOKEN_TYPE;
            else
                status = SepImpersonateClient(thread, token, FALSE, FALSE, level);
            ObDereferenceObject(token);
        }
    }
    ObDereferenceObject(thread);
    return status;
}

NTSTATUS NTAPI NtImpersonateAnonymousToken(HANDLE thread_handle)
{
    PETHREAD thread;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(thread_handle, PsThreadTypeObject, KeGetPreviousMode(), (PVOID *)&thread, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (!(granted & THREAD_IMPERSONATE)) {
        ObDereferenceObject(thread);
        return STATUS_ACCESS_DENIED;
    }
    /* a restricted process, but a write restricted one, may not become anonymous. */
    PTOKEN process_token = SepReferencePrimaryToken(PsCurrentProcess());
    BOOLEAN restricted = FALSE;
    if (process_token) {
        ExAcquireFastMutex(&SeLock);
        restricted = (process_token->Flags & TOKEN_IS_RESTRICTED) && !(process_token->Flags & TOKEN_WRITE_RESTRICTED);
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(process_token);
    }
    if (restricted)
        status = STATUS_ACCESS_DENIED;
    else
        status = SepImpersonateClient(thread, SepAnonymousToken(), TRUE, FALSE, SecurityImpersonation);
    ObDereferenceObject(thread);
    return status;
}

/* ------------------------------------------------------------------ the token of a process */

/* whether a process may hand the token to a new process without a privilege: it descends from its own. */
static BOOLEAN token_assignable(PTOKEN token)
{
    PTOKEN pt = SepReferencePrimaryToken(PsCurrentProcess());
    BOOLEAN free = FALSE;
    if (pt) {
        ExAcquireFastMutex(&SeLock);
        /* a child or a sibling of the caller's token, which is not more trusted than it. */
        if (SepIntegrityRid(pt) >= SepIntegrityRid(token) &&
            (token->ParentTokenId == pt->TokenId ||
             (token->ParentTokenId == pt->ParentTokenId && token->AuthenticationId == pt->AuthenticationId)))
            free = TRUE;
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(pt);
    }
    return free || SepSinglePrivilegeCheck(SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, KeGetPreviousMode());
}

/*
 * the token of a new process given as a handle: it needs the right to be assigned, and either to descend from
 * the creator's token or the privilege to assign tokens. a token that only impersonates can only identify when
 * its level is low, and is made primary otherwise.
 */
NTSTATUS SepReferenceTokenForNewProcess(HANDLE handle, PTOKEN *out)
{
    PTOKEN t;
    NTSTATUS status = SepReferenceTokenByHandle(handle, TOKEN_ASSIGN_PRIMARY, &t, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (t->Type == TokenImpersonation && t->ImpersonationLevel < SecurityImpersonation) {
        ObDereferenceObject(t);
        return STATUS_BAD_IMPERSONATION_LEVEL;
    }
    if (!token_assignable(t)) {
        ObDereferenceObject(t);
        return STATUS_PRIVILEGE_NOT_HELD;
    }
    if (t->Type != TokenPrimary) {
        PTOKEN primary;
        ExAcquireFastMutex(&SeLock);
        primary = SepDuplicate(t, TokenPrimary, 0, FALSE, &status);
        ExReleaseFastMutex(&SeLock);
        ObDereferenceObject(t);
        t = primary;
        if (!t)
            return status;
    }
    *out = t;
    return STATUS_SUCCESS;
}

/* NtSetInformationProcess(ProcessAccessToken): a process that has not started takes another primary token. */
NTSTATUS SepSetProcessAccessToken(HANDLE process_handle, PVOID info, ULONG length)
{
    HANDLE pair[2];
    if (length != sizeof(pair))
        return STATUS_INFO_LENGTH_MISMATCH;
    NTSTATUS status = read_caller(pair, info, sizeof(pair));
    if (!NT_SUCCESS(status))
        return status;
    PEPROCESS process;
    ACCESS_MASK granted;
    status = ObReferenceHandle(process_handle, PsProcessTypeObject, KeGetPreviousMode(), (PVOID *)&process, &granted);
    if (!NT_SUCCESS(status))
        return status;
    PTOKEN token;
    if (!(granted & PROCESS_SET_INFORMATION)) {
        ObDereferenceObject(process);
        return STATUS_ACCESS_DENIED;
    }
    status = SepReferenceTokenByHandle(pair[0], TOKEN_ASSIGN_PRIMARY, &token, nullptr);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(process);
        return status;
    }
    if (token->Type == TokenImpersonation && token->ImpersonationLevel < SecurityImpersonation)
        status = STATUS_BAD_IMPERSONATION_LEVEL;
    else if (!token_assignable(token))
        status = STATUS_PRIVILEGE_NOT_HELD;
    else if (process->TokenFrozen)
        status = STATUS_NOT_SUPPORTED;
    if (NT_SUCCESS(status)) {
        PTOKEN old = nullptr;
        ExAcquireFastMutex(&SeLock);
        if (token->Type != TokenPrimary) {
            status = STATUS_BAD_TOKEN_TYPE;
        } else if (token->InUse) {
            status = STATUS_TOKEN_ALREADY_IN_USE;
        } else {
            token->InUse = TRUE;
            token->SessionId = 1;
            old = process->Token;
            ObReferenceObject(token);
            process->Token = token;
            if (old)
                old->InUse = FALSE;
        }
        ExReleaseFastMutex(&SeLock);
        if (old)
            ObDereferenceObject(old);
    }
    ObDereferenceObject(token);
    ObDereferenceObject(process);
    return status;
}
