/*
 * access tokens: the objects, the tokens of the system, of the session's
 * user and of the anonymous logon, copying and filtering, and the
 * impersonation of threads.
 *
 * every process of the session runs with the token of the logged on user,
 * as a member of the administrators group filtered by uac the way windows
 * starts programs; the system process has the system token. the filtered
 * administrator has the full token as its linked token. access checks are
 * not enforced, but the tokens answer every question programs ask of them
 * and the rules of copying, filtering and impersonating are windows'.
 */
#include "se.h"
#include <mime/wow32.h>

POBJECT_TYPE SeTokenObjectType;
FAST_MUTEX SeLock;

static volatile ULONG64 g_luid = 1001;
static PTOKEN g_system_token, g_session_token, g_elevated_token, g_anonymous_token;

#define TAG_GROUPS KI_TAG('S', 'e', 'G', 'p')
#define TAG_ACL KI_TAG('S', 'e', 'A', 'c')

ULONG64 SepAllocateLuid(void)
{
    return __atomic_add_fetch(&g_luid, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------------ sids */

ULONG SepSidLength(const void *sid)
{
    return 8 + 4 * (ULONG)((const UCHAR *)sid)[1];
}

BOOLEAN SepSidValid(const void *sid, SIZE_T available)
{
    const UCHAR *s = sid;
    return available >= 8 && s[0] == 1 && s[1] <= 15 && available >= SepSidLength(s);
}

BOOLEAN SepSidEqual(const void *a, const void *b)
{
    ULONG length = SepSidLength(a);
    return length == SepSidLength(b) && !memcmp(a, b, length);
}

void SepMakeSid(void *sid, UCHAR authority, ULONG count, const ULONG *sub)
{
    UCHAR *s = sid;
    s[0] = 1;
    s[1] = (UCHAR)count;
    memset(s + 2, 0, 5);
    s[7] = authority;
    memcpy(s + 8, sub, 4 * count);
}

static ULONG sid_rid(const void *sid)
{
    const UCHAR *s = sid;
    return s[1] ? ((const ULONG *)(s + 8))[s[1] - 1] : 0;
}

/* a label sid, S-1-16-rid. */
static BOOLEAN sid_is_label(const void *sid)
{
    const UCHAR *s = sid;
    return s[1] == 1 && s[7] == 16 && !s[2] && !s[3] && !s[4] && !s[5] && !s[6];
}

/* the groups windows takes away from a lua token and denies. */
BOOLEAN SepIsElevatedGroup(const SEP_GROUP *group)
{
    static const ULONG rids[] = {512, 516, 521, 498, 517, 518, 519, 520, 544, 547, 548, 549, 550, 551, 553, 554, 556,
                                 569, 114};
    const UCHAR *sid = group->Sid;
    if (group->Attributes & (SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY))
        return FALSE;
    /* the service sids, S-1-5-80 to S-1-5-111, are never elevated. */
    if (!sid[1])
        return FALSE;
    ULONG first = ((const ULONG *)(sid + 8))[0];
    if (first - 80 <= 0x1F)
        return FALSE;
    ULONG rid = sid_rid(sid);
    for (ULONG i = 0; i < RTL_NUMBER_OF(rids); i++)
        if (rid == rids[i])
            return TRUE;
    return FALSE;
}

ULONG SepIntegrityRid(const TOKEN *t)
{
    return t->IntegrityIndex < 0 ? 0 : sid_rid(t->Groups[t->IntegrityIndex].Sid);
}

/* ------------------------------------------------------------------ privileges */

static ULONG64 privilege_bit(ULONG luid)
{
    return 1ull << luid;
}

ULONG SepPrivilegeCount(const TOKEN *t)
{
    return (ULONG)__builtin_popcountll(t->Present);
}

/* the privileges that make a token elevated, as windows judges it. */
ULONG SepElevated(const TOKEN *t)
{
    if (t->Present & 0x1120160684ull)
        return 1;
    for (ULONG i = 0; i < t->GroupCount; i++)
        if (SepIsElevatedGroup(&t->Groups[i]))
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ building tokens */

static SEP_GROUP *groups_allocate(ULONG count)
{
    if (!count)
        return nullptr;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)count * sizeof(SEP_GROUP), TAG_GROUPS);
}

static void token_delete(PVOID object)
{
    PTOKEN t = object;
    if (t->Groups)
        ExFreePool(t->Groups);
    if (t->Restricted)
        ExFreePool(t->Restricted);
    if (t->Capabilities)
        ExFreePool(t->Capabilities);
    if (t->DefaultDacl)
        ExFreePool(t->DefaultDacl);
    if (t->Linked)
        ObDereferenceObject(t->Linked);
}

static PTOKEN token_new(void)
{
    PTOKEN t;
    if (!NT_SUCCESS(ObCreateObject(SeTokenObjectType, nullptr, KernelMode, sizeof(TOKEN), (PVOID *)&t)))
        return nullptr;
    memset(t, 0, sizeof(*t));
    t->TokenId = SepAllocateLuid();
    t->ModifiedId = SepAllocateLuid();
    t->ExpirationTime.QuadPart = 0x7FFFFFFFFFFFFFFFll;
    t->IntegrityIndex = -1;
    t->DynamicCharged = 500;
    return t;
}

typedef struct GROUP_SPEC {
    UCHAR Authority;
    UCHAR Count;
    ULONG Sub[8];
    ULONG Attributes;
} GROUP_SPEC;

static void group_set(SEP_GROUP *g, const GROUP_SPEC *s)
{
    memset(g, 0, sizeof(*g));
    SepMakeSid(g->Sid, s->Authority, s->Count, s->Sub);
    g->Attributes = s->Attributes;
}

/* the user is group 0; the label, when there is one, is the last group. */
static BOOLEAN token_groups(PTOKEN t, const GROUP_SPEC *user, const GROUP_SPEC *groups, ULONG count)
{
    t->Groups = groups_allocate(count + 1);
    if (!t->Groups)
        return FALSE;
    t->GroupCount = count + 1;
    group_set(&t->Groups[0], user);
    for (ULONG i = 0; i < count; i++) {
        group_set(&t->Groups[1 + i], &groups[i]);
        if (sid_is_label(t->Groups[1 + i].Sid))
            t->IntegrityIndex = (LONG)(1 + i);
    }
    return TRUE;
}

/* an acl of two aces granting everything, to the sids given. */
static void token_dacl(PTOKEN t, const void *first, const void *second)
{
    ULONG a = SepSidLength(first), b = SepSidLength(second);
    ULONG size = 8 + (8 + a) + (8 + b);
    PUCHAR acl = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, TAG_ACL);
    if (!acl)
        return;
    acl[0] = 2;
    *(PUSHORT)(acl + 2) = (USHORT)size;
    *(PUSHORT)(acl + 4) = 2;
    PUCHAR ace = acl + 8;
    const void *sids[2] = {first, second};
    for (int i = 0; i < 2; i++) {
        ULONG length = SepSidLength(sids[i]);
        ace[0] = 0; /* ACCESS_ALLOWED_ACE_TYPE */
        *(PUSHORT)(ace + 2) = (USHORT)(8 + length);
        *(PULONG)(ace + 4) = 0x10000000; /* GENERIC_ALL */
        memcpy(ace + 8, sids[i], length);
        ace += 8 + length;
    }
    t->DefaultDacl = acl;
    t->DefaultDaclSize = size;
}

static ULONG64 privilege_set(const ULONG *luids, ULONG count)
{
    ULONG64 bits = 0;
    for (ULONG i = 0; i < count; i++)
        bits |= privilege_bit(luids[i]);
    return bits;
}

/* the privileges of a filtered administrator: only the harmless ones remain. */
static const ULONG g_limited_privileges[] = {19, 23, 25, 33, 34};

/* everything an elevated administrator holds, and the three it has enabled. */
static const ULONG g_admin_privileges[] = {5, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 22, 23, 24, 25, 28, 29, 30,
                                          33, 34, 35, 36};
static const ULONG g_admin_enabled[] = {23, 29, 30};

static const ULONG g_system_privileges[] = {2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
                                           25, 28, 29, 30, 32, 33, 34, 35, 36};

#define LOGON_SESSION_ID 0x3E8D2
#define SYSTEM_LUID 0x3E7
#define ANONYMOUS_LUID 0x3E6

static PTOKEN make_system_token(void)
{
    static const GROUP_SPEC user = {5, 1, {18}, 0};
    static const GROUP_SPEC groups[] = {
        {5, 2, {32, 544}, SE_GROUP_NORMAL | SE_GROUP_OWNER},
        {1, 1, {0}, SE_GROUP_NORMAL},
        {5, 1, {11}, SE_GROUP_NORMAL},
        {16, 1, {0x4000}, SE_GROUP_LABEL},
    };
    PTOKEN t = token_new();
    if (!t)
        return nullptr;
    memcpy(t->Source, "*SYSTEM*", 8);
    t->AuthenticationId = SYSTEM_LUID;
    t->Type = TokenPrimary;
    t->InUse = TRUE;
    t->ElevationType = SEP_ELEVATION_DEFAULT;
    t->MandatoryPolicy = 1;
    t->Flags = TOKEN_VIRTUALIZE_ALLOWED | TOKEN_NOT_LOW;
    t->Present = privilege_set(g_system_privileges, RTL_NUMBER_OF(g_system_privileges));
    t->Enabled = t->EnabledByDefault = t->Present & ~privilege_bit(2);
    if (!token_groups(t, &user, groups, RTL_NUMBER_OF(groups))) {
        ObDereferenceObject(t);
        return nullptr;
    }
    t->OwnerIndex = 1; /* the administrators group owns what the system creates */
    memcpy(t->PrimaryGroup, t->Groups[0].Sid, SepSidLength(t->Groups[0].Sid));
    static const UCHAR system_sid[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
    token_dacl(t, system_sid, t->Groups[1].Sid);
    return t;
}

/* the logged on user: filtered when limited, whole when elevated. */
static PTOKEN make_user_token(BOOLEAN elevated)
{
    static const GROUP_SPEC common[] = {
        {1, 1, {0}, SE_GROUP_NORMAL},                                   /* Everyone */
        {5, 1, {114}, SE_GROUP_USE_FOR_DENY_ONLY},                      /* Local account and member of Administrators */
        {5, 2, {32, 544}, SE_GROUP_USE_FOR_DENY_ONLY},                  /* BUILTIN\Administrators */
        {5, 2, {32, 545}, SE_GROUP_NORMAL},                             /* BUILTIN\Users */
        {5, 1, {4}, SE_GROUP_NORMAL},                                   /* INTERACTIVE */
        {2, 1, {1}, SE_GROUP_NORMAL},                                   /* CONSOLE LOGON */
        {5, 1, {11}, SE_GROUP_NORMAL},                                  /* Authenticated Users */
        {5, 1, {15}, SE_GROUP_NORMAL},                                  /* This Organization */
        {5, 1, {113}, SE_GROUP_NORMAL},                                 /* Local account */
        {5, 3, {5, 0, LOGON_SESSION_ID}, SE_GROUP_NORMAL | SE_GROUP_LOGON_ID}, /* the logon session */
        {2, 1, {0}, SE_GROUP_NORMAL},                                   /* LOCAL */
        {5, 2, {64, 10}, SE_GROUP_NORMAL},                              /* NTLM Authentication */
        {16, 1, {0x2000}, SE_GROUP_LABEL},                              /* Medium Mandatory Level */
    };
    GROUP_SPEC groups[RTL_NUMBER_OF(common)];
    memcpy(groups, common, sizeof(common));
    if (elevated) {
        groups[1].Attributes = SE_GROUP_NORMAL;
        groups[2].Attributes = SE_GROUP_NORMAL | SE_GROUP_OWNER;
        groups[12].Sub[0] = 0x3000; /* High Mandatory Level */
    }
    GROUP_SPEC user = {5, 5, {21, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2], 1001}, 0};
    PTOKEN t = token_new();
    if (!t)
        return nullptr;
    memcpy(t->Source, "User32  ", 8);
    t->AuthenticationId = LOGON_SESSION_ID;
    t->Type = TokenPrimary;
    t->SessionId = 1;
    t->MandatoryPolicy = 1;
    t->ElevationType = elevated ? SEP_ELEVATION_FULL : SEP_ELEVATION_LIMITED;
    t->Flags = TOKEN_VIRTUALIZE_ALLOWED | TOKEN_NOT_LOW | (elevated ? 0 : TOKEN_IS_FILTERED);
    if (elevated) {
        t->Present = privilege_set(g_admin_privileges, RTL_NUMBER_OF(g_admin_privileges));
        t->Enabled = t->EnabledByDefault = privilege_set(g_admin_enabled, RTL_NUMBER_OF(g_admin_enabled));
    } else {
        t->Present = privilege_set(g_limited_privileges, RTL_NUMBER_OF(g_limited_privileges));
        t->Enabled = t->EnabledByDefault = privilege_bit(23);
    }
    if (!token_groups(t, &user, groups, RTL_NUMBER_OF(groups))) {
        ObDereferenceObject(t);
        return nullptr;
    }
    /* the owner is the user, or the administrators when elevated. */
    t->OwnerIndex = elevated ? 3 : 0;
    ULONG primary[5] = {21, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2], 513};
    SepMakeSid(t->PrimaryGroup, 5, 5, primary); /* None */
    UCHAR system_sid[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
    token_dacl(t, elevated ? t->Groups[3].Sid : t->Groups[0].Sid, system_sid);
    return t;
}

/* the anonymous logon, as windows makes it when Everyone does not include it: no groups but the label. */
static PTOKEN make_anonymous_token(void)
{
    static const GROUP_SPEC user = {5, 1, {7}, 0};
    static const GROUP_SPEC groups[] = {{16, 1, {0}, SE_GROUP_LABEL}};
    PTOKEN t = token_new();
    if (!t)
        return nullptr;
    memcpy(t->Source, "*SYSTEM*", 8);
    t->AuthenticationId = ANONYMOUS_LUID;
    t->Type = TokenImpersonation;
    t->ImpersonationLevel = SecurityImpersonation;
    t->ElevationType = SEP_ELEVATION_DEFAULT;
    t->MandatoryPolicy = 1;
    if (!token_groups(t, &user, groups, RTL_NUMBER_OF(groups))) {
        ObDereferenceObject(t);
        return nullptr;
    }
    static const GROUP_SPEC world = {1, 1, {0}, 0};
    SepMakeSid(t->PrimaryGroup, world.Authority, world.Count, world.Sub);
    return t;
}

/* ------------------------------------------------------------------ copies */

/* copies the contents of a token into one just made; the lock is held. */
static NTSTATUS token_copy(PTOKEN dst, const TOKEN *src)
{
    memcpy(dst->Source, src->Source, sizeof(dst->Source));
    dst->AuthenticationId = src->AuthenticationId;
    dst->ParentTokenId = src->ParentTokenId;
    dst->Origin = src->Origin;
    dst->ExpirationTime = src->ExpirationTime;
    dst->SessionId = src->SessionId;
    dst->ElevationType = src->ElevationType;
    dst->Present = src->Present;
    dst->Enabled = src->Enabled;
    dst->EnabledByDefault = src->EnabledByDefault;
    dst->OwnerIndex = src->OwnerIndex;
    dst->IntegrityIndex = src->IntegrityIndex;
    dst->MandatoryPolicy = src->MandatoryPolicy;
    dst->DynamicCharged = src->DynamicCharged;
    dst->LowboxNumber = src->LowboxNumber;
    memcpy(dst->PrimaryGroup, src->PrimaryGroup, sizeof(dst->PrimaryGroup));
    memcpy(dst->AppContainerSid, src->AppContainerSid, sizeof(dst->AppContainerSid));
    dst->Flags = src->Flags;
    if (src->GroupCount) {
        dst->Groups = groups_allocate(src->GroupCount);
        if (!dst->Groups)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(dst->Groups, src->Groups, (SIZE_T)src->GroupCount * sizeof(SEP_GROUP));
        dst->GroupCount = src->GroupCount;
    }
    if (src->RestrictedCount) {
        dst->Restricted = groups_allocate(src->RestrictedCount);
        if (!dst->Restricted)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(dst->Restricted, src->Restricted, (SIZE_T)src->RestrictedCount * sizeof(SEP_GROUP));
        dst->RestrictedCount = src->RestrictedCount;
    }
    if (src->CapabilityCount) {
        dst->Capabilities = groups_allocate(src->CapabilityCount);
        if (!dst->Capabilities)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(dst->Capabilities, src->Capabilities, (SIZE_T)src->CapabilityCount * sizeof(SEP_GROUP));
        dst->CapabilityCount = src->CapabilityCount;
    }
    if (src->DefaultDacl) {
        dst->DefaultDacl = ExAllocatePool2(POOL_FLAG_NON_PAGED, src->DefaultDaclSize, TAG_ACL);
        if (!dst->DefaultDacl)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(dst->DefaultDacl, src->DefaultDacl, src->DefaultDaclSize);
        dst->DefaultDaclSize = src->DefaultDaclSize;
    }
    if (src->Linked) {
        ObReferenceObject(src->Linked);
        dst->Linked = src->Linked;
    }
    return STATUS_SUCCESS;
}

/* removes the groups that are not enabled, and the privileges. the lock is held. */
static void token_effective_only(PTOKEN t)
{
    t->Present &= t->Enabled;
    t->EnabledByDefault &= t->Enabled;
    ULONG i = 1;
    while (i < t->GroupCount) {
        if (t->Groups[i].Attributes & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY)) {
            i++;
            continue;
        }
        ULONG last = t->GroupCount - 1;
        if (i == t->OwnerIndex)
            t->OwnerIndex = 0;
        if (t->IntegrityIndex == (LONG)i)
            t->IntegrityIndex = -1;
        if (t->IntegrityIndex == (LONG)last)
            t->IntegrityIndex = (LONG)i;
        if (t->OwnerIndex == last)
            t->OwnerIndex = i;
        t->Groups[i] = t->Groups[last];
        t->GroupCount--;
    }
}

PTOKEN SepDuplicate(const PTOKEN source, ULONG type, ULONG level, BOOLEAN effective_only, NTSTATUS *status)
{
    PTOKEN t = token_new();
    NTSTATUS result = t ? token_copy(t, source) : STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(result)) {
        t->Type = type;
        t->ImpersonationLevel = level;
        t->ModifiedId = source->ModifiedId;
        /* a copy is neither in use nor virtualizing, and it counts its session on its own. */
        t->Flags &= ~(TOKEN_SESSION_NOT_REFERENCED | TOKEN_VIRTUALIZE_ENABLED);
        if (effective_only)
            token_effective_only(t);
    } else if (t) {
        ObDereferenceObject(t);
        t = nullptr;
    }
    if (status)
        *status = result;
    return t;
}

/* ------------------------------------------------------------------ filtering */

static BOOLEAN group_in(const SEP_GROUP *groups, ULONG count, const void *sid)
{
    for (ULONG i = 0; i < count; i++)
        if (SepSidEqual(groups[i].Sid, sid))
            return TRUE;
    return FALSE;
}

/* the privileges kept by the lua filter, with and without an elevated group. */
#define LUA_KEEP_ELEVATED 0x602880000ull
#define LUA_KEEP_PLAIN 0xFFFFFFEEDFE9F97Bull

/*
 * the token of NtFilterToken: groups turned deny only, privileges deleted, sids to restricting with. the sids to
 * restricting a restricted token with must be among its own, else the request is refused.
 */
NTSTATUS SepFilter(const PTOKEN source, ULONG flags, ULONG disable_count, const SEP_GROUP *disable, ULONG delete_count,
                   const ULONG *delete_luids, ULONG restrict_count, const SEP_GROUP *restricting, PTOKEN *out)
{
    *out = nullptr;
    if ((flags & WRITE_RESTRICTED) && (source->Flags & (TOKEN_SANDBOX_INERT | TOKEN_IS_RESTRICTED | TOKEN_WRITE_RESTRICTED)))
        return STATUS_INVALID_PARAMETER;
    PTOKEN t = token_new();
    NTSTATUS status = t ? token_copy(t, source) : STATUS_INSUFFICIENT_RESOURCES;
    if (!NT_SUCCESS(status)) {
        if (t)
            ObDereferenceObject(t);
        return status;
    }
    t->Type = source->Type;
    t->ImpersonationLevel = source->ImpersonationLevel;
    t->ParentTokenId = source->TokenId;
    t->Flags &= ~(TOKEN_WRITE_RESTRICTED | TOKEN_SESSION_NOT_REFERENCED | TOKEN_VIRTUALIZE_ENABLED);

    /* the sids to restricting with; a restricted token keeps only those it already restricts with. */
    ULONG kept = 0;
    SEP_GROUP *list = nullptr;
    if (restrict_count) {
        list = groups_allocate(restrict_count);
        if (!list) {
            ObDereferenceObject(t);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        for (ULONG i = 0; i < restrict_count; i++) {
            if (source->RestrictedCount && !group_in(source->Restricted, source->RestrictedCount, restricting[i].Sid))
                continue;
            list[kept] = restricting[i];
            list[kept].Attributes = SE_GROUP_NORMAL;
            kept++;
        }
    }
    if (source->RestrictedCount && !kept) {
        if (list)
            ExFreePool(list);
        ObDereferenceObject(t);
        return STATUS_INVALID_PARAMETER;
    }
    if (t->Restricted) {
        ExFreePool(t->Restricted);
        t->Restricted = nullptr;
        t->RestrictedCount = 0;
    }
    if (kept) {
        t->Restricted = list;
        t->RestrictedCount = kept;
    } else if (list) {
        ExFreePool(list);
    }

    /* groups turned into deny only, the user included. */
    BOOLEAN elevated_found = FALSE;
    for (ULONG i = 0; i < t->GroupCount; i++) {
        SEP_GROUP *g = &t->Groups[i];
        BOOLEAN disabled = FALSE;
        if (!(g->Attributes & (SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY)) &&
            group_in(disable, disable_count, g->Sid)) {
            disabled = TRUE;
        } else if ((flags & LUA_TOKEN) && SepIsElevatedGroup(g)) {
            disabled = TRUE;
            elevated_found = TRUE;
        }
        if (!disabled)
            continue;
        g->Attributes = (g->Attributes & ~0xFu) | SE_GROUP_USE_FOR_DENY_ONLY;
        t->Flags |= TOKEN_IS_FILTERED;
        if (i == t->OwnerIndex)
            t->OwnerIndex = 0;
    }

    /* privileges. */
    ULONG before = SepPrivilegeCount(t);
    if (flags & DISABLE_MAX_PRIVILEGE) {
        t->Present &= privilege_bit(23);
        t->Enabled &= privilege_bit(23);
        t->EnabledByDefault &= privilege_bit(23);
    } else {
        if (flags & LUA_TOKEN) {
            ULONG64 mask = elevated_found ? LUA_KEEP_ELEVATED : LUA_KEEP_PLAIN;
            t->Present &= mask;
            t->Enabled &= mask;
            t->EnabledByDefault &= mask;
        }
        for (ULONG i = 0; i < delete_count; i++) {
            if (delete_luids[i] >= SEP_PRIVILEGE_FIRST && delete_luids[i] <= SEP_PRIVILEGE_LAST) {
                t->Present &= ~privilege_bit(delete_luids[i]);
                t->Enabled &= ~privilege_bit(delete_luids[i]);
            }
        }
    }
    if (!(t->Flags & TOKEN_IS_FILTERED) && SepPrivilegeCount(t) < before)
        t->Flags |= TOKEN_IS_FILTERED;

    if (t->RestrictedCount)
        t->Flags |= TOKEN_IS_RESTRICTED | TOKEN_IS_FILTERED;
    if (flags & WRITE_RESTRICTED)
        t->Flags |= TOKEN_WRITE_RESTRICTED | TOKEN_IS_RESTRICTED;
    /* a whole token cut down is not whole any more: it is the limited half of a pair with the one it came from. */
    if (t->ElevationType == SEP_ELEVATION_FULL && !SepElevated(t)) {
        t->ElevationType = SEP_ELEVATION_LIMITED;
        if (t->Linked)
            ObDereferenceObject(t->Linked);
        ObReferenceObject(source);
        t->Linked = source;
    }
    *out = t;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ handles */

/* the generic rights of a token and the maximum. */
ACCESS_MASK SepMapTokenAccess(ACCESS_MASK access)
{
    if (access & 0x02000000) /* MAXIMUM_ALLOWED */
        access = (access & ~0x02000000u) | TOKEN_ALL_ACCESS;
    if (access & 0x80000000)
        access |= 0x20000 | TOKEN_QUERY | TOKEN_QUERY_SOURCE;
    if (access & 0x40000000)
        access |= 0x20000 | TOKEN_ADJUST_PRIVILEGES | TOKEN_ADJUST_GROUPS | TOKEN_ADJUST_DEFAULT;
    if (access & 0x20000000)
        access |= 0x20000;
    if (access & 0x10000000)
        access |= TOKEN_ALL_ACCESS;
    return access & ~0xF0000000u;
}

/* adds an entry for a sid to the list of the token's descriptor, which it has by now. */
static void append_ace(PTOKEN token, ACCESS_MASK mask, const UCHAR *sid)
{
    SD_BLOB *sd, *made;
    if (!NT_SUCCESS(ObReferenceSecurity(token, &sd)) || !sd)
        return;
    if (NT_SUCCESS(SeSdAppendAllowAce(sd, sid, mask, &made))) {
        if (made != sd)
            ObStoreSecurity(token, made);
        SeSdDereference(made);
    }
    SeSdDereference(sd);
}

/* the entry the default list gets for a package, so what the app container makes is its own to use. */
static void append_default_ace(PTOKEN token, const UCHAR *sid)
{
    ULONG sid_size = SepSidLength(sid), ace_size = 8 + sid_size;
    ULONG size = token->DefaultDaclSize ? token->DefaultDaclSize : 8;
    PUCHAR acl = ExAllocatePool2(POOL_FLAG_NON_PAGED, (size + ace_size + 3) & ~3u, TAG_ACL);
    if (!acl)
        return;
    if (token->DefaultDaclSize) {
        memcpy(acl, token->DefaultDacl, token->DefaultDaclSize);
    } else {
        acl[0] = 2;
        *(PUSHORT)(acl + 2) = 8;
    }
    PUCHAR ace = acl + size;
    ace[0] = 0;
    *(PUSHORT)(ace + 2) = (USHORT)ace_size;
    *(PULONG)(ace + 4) = 0x10000000; /* GENERIC_ALL */
    memcpy(ace + 8, sid, sid_size);
    *(PUSHORT)(acl + 2) = (USHORT)((size + ace_size + 3) & ~3u);
    (*(PUSHORT)(acl + 4))++;
    if (token->DefaultDacl)
        ExFreePool(token->DefaultDacl);
    token->DefaultDacl = acl;
    token->DefaultDaclSize = (size + ace_size + 3) & ~3u;
}

/*
 * what the descriptor of a token gets once it exists, as the kernel of windows gives it: the administrators may query
 * every one, and an app container's token is open to the package, whose default list also names it.
 */
void SepFinalizeTokenSecurity(PTOKEN token)
{
    if (token->Flags & TOKEN_LOWBOX) {
        append_ace(token, TOKEN_ALL_ACCESS, token->AppContainerSid);
    }
    append_ace(token, TOKEN_QUERY, SeSidAdministrators);
}

void SepAddPackageToDefaultList(PTOKEN token)
{
    if (token->Flags & TOKEN_LOWBOX)
        append_default_ace(token, token->AppContainerSid);
}

/*
 * the descriptor of a token: the one its maker offered, else with its own subject the token's own default list and level,
 * as a token made for a process has, or the maker's default.
 */
NTSTATUS SepAssignTokenSecurity(PTOKEN token, const OBJECT_ATTRIBUTES *attributes, BOOLEAN own_subject)
{
    OBJECT_HEADER *h = OBJECT_TO_HEADER(token);
    if (!(h->Flags & OB_FLAG_SD_PENDING))
        return STATUS_SUCCESS;
    NTSTATUS status;
    if (!own_subject || (attributes && attributes->SecurityDescriptor)) {
        status = ObAssignSecurity(token, attributes, nullptr, KeGetPreviousMode());
    } else {
        SD_BLOB *sd;
        status = SeSdForToken(token, SeTokenObjectType->Init.GenericMapping, &sd);
        if (NT_SUCCESS(status))
            ObAttachSecurity(token, sd);
        else if (status == STATUS_NO_TOKEN)
            status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(status))
        SepFinalizeTokenSecurity(token);
    return status;
}

/* consumes the pointer reference. */
NTSTATUS SepInsertToken(PTOKEN token, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    if (KeGetPreviousMode() == UserMode)
        attributes &= ~OBJ_KERNEL_HANDLE;
    HANDLE h;
    NTSTATUS status = ObInsertObject(token, SepMapTokenAccess(access), attributes, &h);
    return NT_SUCCESS(status) ? ObStoreHandle(handle, h) : status;
}

/* a handle to a token that exists, which the caller is asked for: its descriptor decides, against the process's own token with as_self. */
NTSTATUS SepOpenToken(PTOKEN token, ACCESS_MASK access, ULONG attributes, BOOLEAN as_self, PHANDLE handle)
{
    ACCESS_MASK granted = access;
    NTSTATUS status = ObCheckOpenAccessEx(token, access, KeGetPreviousMode(), as_self ? SE_ACCESS_AS_SELF : 0, &granted);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(token);
        return status;
    }
    return SepInsertToken(token, granted, attributes, handle);
}

/* the token behind a handle that grants the rights; the pseudo handles stand for the current ones. */
NTSTATUS SepReferenceTokenByHandle(HANDLE handle, ACCESS_MASK required, PTOKEN *token, ACCESS_MASK *granted)
{
    LONG_PTR pseudo = (LONG_PTR)handle;
    if (pseudo >= -6 && pseudo <= -4) {
        if (required & ~(ACCESS_MASK)(TOKEN_QUERY | TOKEN_QUERY_SOURCE))
            return STATUS_ACCESS_DENIED;
        PETHREAD thread = PsCurrentThread();
        PTOKEN t = nullptr;
        ExAcquireFastMutex(&SeLock);
        if (pseudo == -4) {
            t = (PTOKEN)PsCurrentProcess()->Token;
        } else if (pseudo == -5) {
            t = thread->ImpersonationToken;
            if (!t) {
                ExReleaseFastMutex(&SeLock);
                return STATUS_NO_TOKEN;
            }
        } else {
            t = thread->ImpersonationToken ? thread->ImpersonationToken : (PTOKEN)PsCurrentProcess()->Token;
        }
        if (pseudo != -4 && thread->ImpersonationToken == t && thread->ImpersonationLevel == SecurityAnonymous) {
            ExReleaseFastMutex(&SeLock);
            return STATUS_CANT_OPEN_ANONYMOUS;
        }
        if (t)
            ObReferenceObject(t);
        ExReleaseFastMutex(&SeLock);
        if (!t)
            return STATUS_NO_TOKEN;
        *token = t;
        if (granted)
            *granted = TOKEN_QUERY | TOKEN_QUERY_SOURCE;
        return STATUS_SUCCESS;
    }
    ACCESS_MASK access;
    PTOKEN t;
    NTSTATUS status = ObReferenceHandle(handle, SeTokenObjectType, KeGetPreviousMode(), (PVOID *)&t, &access);
    if (!NT_SUCCESS(status))
        return status;
    if ((access & required) != required) {
        ObDereferenceObject(t);
        return STATUS_ACCESS_DENIED;
    }
    *token = t;
    if (granted)
        *granted = access;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ the tokens of processes and threads */

/*
 * a new process gets a copy of its parent's token, with a number of its own; the system process, which the
 * processes of the host's requests are children of, gives the user's.
 */
NTSTATUS SepAssignProcessToken(PEPROCESS child, PEPROCESS parent)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN source = parent && !parent->SystemProcess && parent->Token ? parent->Token : g_session_token;
    NTSTATUS status;
    PTOKEN t = SepDuplicate(source, TokenPrimary, 0, FALSE, &status);
    if (t) {
        t->InUse = TRUE;
        child->Token = t;
    }
    ExReleaseFastMutex(&SeLock);
    /* the token a process runs with is open to the process by its own default list and level. */
    if (t)
        SepAssignTokenSecurity(t, nullptr, TRUE);
    return status;
}

/*
 * the token of the service control manager: a copy of the system's own, in session 0 where the services live.
 * referenced, to be given to SepSetProcessToken.
 */
PTOKEN SepCreateServiceControllerToken(void)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = SepDuplicate(g_system_token, TokenPrimary, 0, FALSE, nullptr);
    if (t)
        t->SessionId = 0;
    ExReleaseFastMutex(&SeLock);
    return t;
}

/* the whole token of the user the machine runs for, for a program that asked for the rights of an administrator. */
PTOKEN SepCreateElevatedUserToken(void)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = SepDuplicate(g_elevated_token, TokenPrimary, 0, FALSE, nullptr);
    ExReleaseFastMutex(&SeLock);
    return t;
}

/* the session a process runs in, which is its token's. */
ULONG SepTokenSessionId(PEPROCESS process)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = process->Token;
    ULONG session = t ? t->SessionId : 1;
    ExReleaseFastMutex(&SeLock);
    return session;
}

/*
 * an explicit token for a new process. The process runs with a copy of it, as in windows, so the descriptor of the token
 * it runs with is its own and the token it was made from stays free for the next process. (ProcessAccessToken is
 * the way to give a process the token itself, see SepSetProcessAccessToken.)
 */
void SepSetProcessToken(PEPROCESS process, PTOKEN token)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN old = process->Token;
    PTOKEN copy = SepDuplicate(token, TokenPrimary, 0, FALSE, nullptr);
    if (copy) {
        copy->InUse = TRUE;
        process->Token = copy;
    }
    ExReleaseFastMutex(&SeLock);
    if (copy)
        SepAssignTokenSecurity(copy, nullptr, TRUE);
    if (old)
        ObDereferenceObject(old);
}

void SepFreeProcessToken(PEPROCESS process)
{
    PTOKEN t = process->Token;
    process->Token = nullptr;
    if (t)
        ObDereferenceObject(t);
}

/* the token a thread acts with, referenced. */
PTOKEN SepReferenceEffectiveToken(PETHREAD thread)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = thread->ImpersonationToken ? thread->ImpersonationToken : (PTOKEN)((PEPROCESS)thread->Tcb.Process)->Token;
    if (t)
        ObReferenceObject(t);
    ExReleaseFastMutex(&SeLock);
    return t;
}

PTOKEN SepReferencePrimaryToken(PEPROCESS process)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = process->Token;
    if (t)
        ObReferenceObject(t);
    ExReleaseFastMutex(&SeLock);
    return t;
}

/* whether the current thread holds a privilege enabled; kernel callers hold them all. */
BOOLEAN SepSinglePrivilegeCheck(ULONG luid, KPROCESSOR_MODE mode)
{
    if (mode == KernelMode)
        return TRUE;
    PTOKEN t = SepReferenceEffectiveToken(PsCurrentThread());
    if (!t)
        return FALSE;
    ExAcquireFastMutex(&SeLock);
    BOOLEAN held = (t->Present & t->Enabled & privilege_bit(luid)) != 0;
    ExReleaseFastMutex(&SeLock);
    ObDereferenceObject(t);
    return held;
}

/* the user running as the system. */
BOOLEAN SepIsSystemToken(const TOKEN *t)
{
    return t->GroupCount && t->Groups[0].Sid[1] == 1 && t->Groups[0].Sid[7] == 5 && sid_rid(t->Groups[0].Sid) == 18;
}

BOOLEAN SepCanImpersonate(PTOKEN pt, PTOKEN target, ULONG level)
{
    if (level < SecurityImpersonation || !pt)
        return TRUE;
    /* anyone may become the anonymous logon, but a process of an app container. */
    if (target->AuthenticationId == ANONYMOUS_LUID) {
        if (!(pt->Flags & TOKEN_LOWBOX))
            return TRUE;
        return (target->Flags & TOKEN_LOWBOX) && SepSidEqual(pt->AppContainerSid, target->AppContainerSid);
    }
    if (pt->Present & pt->Enabled & privilege_bit(SE_IMPERSONATE_PRIVILEGE))
        return TRUE;
    if (SepIntegrityRid(pt) < SepIntegrityRid(target))
        return FALSE;
    if (pt->AuthenticationId != target->Origin && !SepSidEqual(pt->Groups[0].Sid, target->Groups[0].Sid))
        return FALSE;
    if ((pt->Flags & TOKEN_LOWBOX) &&
        (!(target->Flags & TOKEN_LOWBOX) || !SepSidEqual(pt->AppContainerSid, target->AppContainerSid)))
        return FALSE;
    if ((pt->Flags & TOKEN_IS_RESTRICTED) && !((target->Flags & TOKEN_IS_RESTRICTED) &&
                                                ((pt->Flags & TOKEN_WRITE_RESTRICTED) ||
                                                 !(target->Flags & TOKEN_WRITE_RESTRICTED))))
        return FALSE;
    return pt->SessionId == target->SessionId || target->SessionId != 0;
}

/* tells the thread's own teb, and the 32-bit one, whether it impersonates. */
static void write_teb(PETHREAD thread, ULONG impersonating)
{
    PTEB teb = thread->Tcb.Teb;
    if (!teb)
        return;
    PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
    PKPROCESS saved = nullptr;
    BOOLEAN attached = FALSE;
    if (process != PsCurrentProcess()) {
        saved = KiCurrentThread()->ApcStateProcess;
        KiAttachProcess(&process->Pcb);
        attached = TRUE;
    }
    KiCopyToUser(&teb->IsImpersonating, &impersonating, sizeof(impersonating));
    if (thread->Tcb.Teb32)
        KiCopyToUser((PUCHAR)thread->Tcb.Teb32 + MIME_TEB32_IS_IMPERSONATING, &impersonating, sizeof(impersonating));
    if (attached)
        KiAttachProcess(saved);
}

/*
 * makes a thread act with a token, or with its process's again when there is none. a process that may not take
 * the token's identity at the level asked is given a copy that can only identify, as windows does.
 */
NTSTATUS SepImpersonateClient(PETHREAD thread, PTOKEN token, BOOLEAN copy_on_open, BOOLEAN effective_only, ULONG level)
{
    PTOKEN old = nullptr, use = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&SeLock);
    if (!token) {
        old = thread->ImpersonationToken;
        thread->ImpersonationToken = nullptr;
    } else {
        PTOKEN process_token = ((PEPROCESS)thread->Tcb.Process)->Token;
        if (SepCanImpersonate(process_token, token, level)) {
            use = token;
            ObReferenceObject(use);
        } else {
            ULONG identify = token->Type == TokenImpersonation && token->ImpersonationLevel < SecurityIdentification
                                 ? token->ImpersonationLevel
                                 : SecurityIdentification;
            use = SepDuplicate(token, TokenImpersonation, identify, FALSE, &status);
            level = identify;
        }
        if (use) {
            old = thread->ImpersonationToken;
            thread->ImpersonationToken = use;
            thread->ImpersonationLevel = (UCHAR)level;
            thread->EffectiveOnly = effective_only;
            thread->CopyOnOpen = copy_on_open;
        }
    }
    ExReleaseFastMutex(&SeLock);
    if (old)
        ObDereferenceObject(old);
    if (NT_SUCCESS(status) && (token ? use != nullptr : old != nullptr))
        write_teb(thread, token ? 1 : 0);
    return status;
}

/* a copy of an identity for a named pipe's client, which can only be impersonated. */
PTOKEN SepDuplicateForPipe(PTOKEN token, ULONG level, BOOLEAN effective_only)
{
    ExAcquireFastMutex(&SeLock);
    PTOKEN t = SepDuplicate(token, TokenImpersonation, level, effective_only, nullptr);
    ExReleaseFastMutex(&SeLock);
    return t;
}

void SepFreeThreadToken(PETHREAD thread)
{
    PTOKEN t = thread->ImpersonationToken;
    thread->ImpersonationToken = nullptr;
    if (t)
        ObDereferenceObject(t);
}

/* the token of the user the machine runs for, whose defaults files and keys show that nobody made otherwise. */
PTOKEN SepReferenceSessionToken(void)
{
    ObReferenceObject(g_session_token);
    return g_session_token;
}

PTOKEN SepAnonymousToken(void)
{
    return g_anonymous_token;
}

PTOKEN SepLinkedToken(PTOKEN token)
{
    return token->Linked;
}

/* ------------------------------------------------------------------ start up */

void SepInitialize(void)
{
    ExInitializeFastMutex(&SeLock);
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = TOKEN_ALL_ACCESS;
    init.DeleteProcedure = token_delete;
    ObSetGenericMapping(&init, 0x2001A, 0x201E0, 0x20005, 0xF01FF);
    SeTokenObjectType = ObCreateObjectType(L"Token", &init);

    g_system_token = make_system_token();
    g_session_token = make_user_token(FALSE);
    g_elevated_token = make_user_token(TRUE);
    g_anonymous_token = make_anonymous_token();
    if (!g_system_token || !g_session_token || !g_elevated_token || !g_anonymous_token)
        KeBugCheckEx(0x0000006B /* PROCESS1_INITIALIZATION_FAILED */, 0, 0, 0, 0);
    /* the filtered user and the whole one are each other's linked token. */
    ObReferenceObject(g_elevated_token);
    g_session_token->Linked = g_elevated_token;
    ObReferenceObject(g_session_token);
    g_elevated_token->Linked = g_session_token;
    g_elevated_token->ParentTokenId = g_session_token->ParentTokenId;

    /* the tokens of the machine are open to the users they are of. */
    SepAssignTokenSecurity(g_system_token, nullptr, TRUE);
    SepAssignTokenSecurity(g_session_token, nullptr, TRUE);
    SepAssignTokenSecurity(g_elevated_token, nullptr, TRUE);
    SepAssignTokenSecurity(g_anonymous_token, nullptr, TRUE);
    /* the system process, which was made before tokens could be. */
    g_system_token->InUse = TRUE;
    ObReferenceObject(g_system_token);
    PsInitialSystemProcess->Token = g_system_token;
}
