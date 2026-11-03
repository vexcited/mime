/* built-in software restriction levels; custom policies and lower-trust sandboxes are not implemented. */
#include "advapi32_private.h"

#define SAFER_DISALLOWED 0
#define SAFER_NORMALUSER 0x20000
#define SAFER_FULLYTRUSTED 0x40000
#define SAFER_NULL_IF_EQUAL 1
#define SAFER_COMPARE_ONLY 2
#define SAFER_MAKE_INERT 4
#define SAFER_WANT_FLAGS 8

typedef struct SAFER_LEVEL {
    struct SAFER_LEVEL *Next;
    HANDLE Handle;
    DWORD Level, Scope;
} SAFER_LEVEL;

static RTL_SRWLOCK g_safer_lock;
static SAFER_LEVEL *g_safer_levels;
static ULONG g_safer_sequence;

ADVAPI BOOL WINAPI SaferCreateLevel(DWORD scope, DWORD level, DWORD flags, PHANDLE result, PVOID reserved)
{
    if (!result)
        return AdvSetStatus(STATUS_ACCESS_VIOLATION);
    if (scope != 1 && scope != 2)
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    if (flags & 6)
        return AdvSetStatus(STATUS_NOT_IMPLEMENTED);
    if (level != SAFER_DISALLOWED && level != SAFER_NORMALUSER && level != SAFER_FULLYTRUSTED) {
        AdvSetError(level == 0x1000 || level == 0x10000 ? ERROR_NOT_SUPPORTED : ERROR_NOT_FOUND);
        return FALSE;
    }
    SAFER_LEVEL *entry = AdvAlloc(sizeof(*entry));
    if (!entry)
        return AdvSetStatus(STATUS_NO_MEMORY);
    RtlAcquireSRWLockExclusive(&g_safer_lock);
    if (g_safer_sequence == 0xffffff) {
        RtlReleaseSRWLockExclusive(&g_safer_lock);
        AdvFree(entry);
        return AdvSetStatus(STATUS_NO_MEMORY);
    }
    entry->Handle = (HANDLE)(ULONG_PTR)(0x74000000 | ++g_safer_sequence);
    entry->Level = level;
    entry->Scope = scope;
    entry->Next = g_safer_levels;
    g_safer_levels = entry;
    *result = entry->Handle;
    RtlReleaseSRWLockExclusive(&g_safer_lock);
    return TRUE;
}

/*
 * no policy is set, so every code is identified at the level a user is given by default: unrestricted. the last
 * argument is nothing or the name of the policy kind, such as the one of scripts.
 */
ADVAPI BOOL WINAPI SaferIdentifyLevel(DWORD count, PVOID records, PHANDLE result, PVOID reserved)
{
    if (!result || (count && !records))
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    /* the size of SAFER_CODE_PROPERTIES leads the record. */
    if (count && !*(DWORD *)records)
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    return SaferCreateLevel(1, SAFER_FULLYTRUSTED, 1, result, nullptr);
}

ADVAPI BOOL WINAPI SaferGetLevelInformation(HANDLE handle, DWORD information, PVOID buffer, DWORD size, PDWORD returned)
{
    DWORD value = 0;
    BOOL found = FALSE;
    RtlAcquireSRWLockShared(&g_safer_lock);
    for (SAFER_LEVEL *entry = g_safer_levels; entry; entry = entry->Next) {
        if (entry->Handle == handle) {
            value = information == 2 ? entry->Scope : entry->Level;
            found = TRUE;
            break;
        }
    }
    RtlReleaseSRWLockShared(&g_safer_lock);
    if (!found)
        return AdvSetStatus(STATUS_INVALID_HANDLE);
    /* the level and the scope are the answers there are, the names and descriptions are not kept. */
    if (information != 1 && information != 2) {
        AdvSetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (returned)
        *returned = sizeof(DWORD);
    if (!buffer || size < sizeof(DWORD)) {
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *(DWORD *)buffer = value;
    return TRUE;
}

ADVAPI BOOL WINAPI SaferCloseLevel(HANDLE handle)
{
    RtlAcquireSRWLockExclusive(&g_safer_lock);
    SAFER_LEVEL **link = &g_safer_levels;
    while (*link && (*link)->Handle != handle)
        link = &(*link)->Next;
    SAFER_LEVEL *entry = *link;
    if (entry)
        *link = entry->Next;
    RtlReleaseSRWLockExclusive(&g_safer_lock);
    AdvFree(entry);
    return entry ? TRUE : AdvSetStatus(STATUS_INVALID_HANDLE);
}

static NTSTATUS query_token(HANDLE token, ULONG information, PVOID *result)
{
    ULONG size = 0;
    *result = nullptr;
    NTSTATUS status = NtQueryInformationToken(token, information, nullptr, 0, &size);
    while (status == STATUS_BUFFER_TOO_SMALL) {
        PVOID buffer = AdvAlloc(size);
        if (!buffer)
            return STATUS_NO_MEMORY;
        status = NtQueryInformationToken(token, information, buffer, size, &size);
        if (NT_SUCCESS(status)) {
            *result = buffer;
            return status;
        }
        AdvFree(buffer);
    }
    return status;
}

static BOOL nt_authority(const SID *sid)
{
    static const SID_IDENTIFIER_AUTHORITY authority = {{0, 0, 0, 0, 0, 5}};
    return AdvSidValid(sid) && !memcmp(&sid->IdentifierAuthority, &authority, sizeof(authority));
}

static BOOL administrative_sid(const SID *sid)
{
    if (!nt_authority(sid))
        return FALSE;
    if (sid->SubAuthorityCount == 2 && sid->SubAuthority[0] == 32)
        return sid->SubAuthority[1] == 544 || sid->SubAuthority[1] == 547;
    if (sid->SubAuthorityCount == 5 && sid->SubAuthority[0] == 21) {
        ULONG rid = sid->SubAuthority[4];
        return rid == 512 || (rid >= 517 && rid <= 520);
    }
    return FALSE;
}

/* the native exemption descriptor grants system, service, restricted code and write restricted code. */
static BOOL exempt_sid(const SID *sid)
{
    if (!nt_authority(sid) || sid->SubAuthorityCount != 1)
        return FALSE;
    ULONG rid = sid->SubAuthority[0];
    return rid == 18 || rid == 6 || rid == 12 || rid == 33;
}

static BOOL exempt_token(const TOKEN_USER *user, const TOKEN_GROUPS *groups, const TOKEN_GROUPS *restricted)
{
    BOOL allowed = !(user->User.Attributes & SE_GROUP_USE_FOR_DENY_ONLY) && exempt_sid(user->User.Sid);
    for (ULONG i = 0; !allowed && i < groups->GroupCount; i++)
        allowed = (groups->Groups[i].Attributes & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY)) == SE_GROUP_ENABLED &&
                  exempt_sid(groups->Groups[i].Sid);
    if (!allowed || !restricted->GroupCount)
        return allowed;
    for (ULONG i = 0; i < restricted->GroupCount; i++)
        if (exempt_sid(restricted->Groups[i].Sid))
            return TRUE;
    return FALSE;
}

static NTSTATUS duplicate_primary(HANDLE token, PHANDLE result)
{
    struct {
        ULONG Length;
        SECURITY_IMPERSONATION_LEVEL Level;
        BOOLEAN Tracking, EffectiveOnly;
    } qos = {sizeof(qos), SecurityImpersonation, TRUE, FALSE};
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, nullptr, 0, nullptr, nullptr);
    attributes.SecurityQualityOfService = &qos;
    return NtDuplicateToken(token, TOKEN_ALL_ACCESS, &attributes, FALSE, TokenPrimary, result);
}

ADVAPI BOOL WINAPI SaferComputeTokenFromLevel(HANDLE handle, HANDLE input, PHANDLE output, DWORD flags, PVOID reserved)
{
    DWORD level = 0;
    BOOL found = FALSE;
    RtlAcquireSRWLockShared(&g_safer_lock);
    for (SAFER_LEVEL *entry = g_safer_levels; entry; entry = entry->Next) {
        if (entry->Handle == handle) {
            level = entry->Level;
            found = TRUE;
            break;
        }
    }
    RtlReleaseSRWLockShared(&g_safer_lock);
    if (!found)
        return AdvSetStatus(STATUS_INVALID_HANDLE);
    if ((flags & SAFER_COMPARE_ONLY) ? !reserved || !input : !output)
        return AdvSetStatus(STATUS_INVALID_PARAMETER);
    HANDLE token = input, filtered = nullptr, result = nullptr;
    NTSTATUS status = STATUS_SUCCESS;
    if (!token) {
        status = NtOpenThreadToken(NtCurrentThread(), READ_CONTROL | TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &token);
        if (status == STATUS_NO_TOKEN)
            status = NtOpenProcessToken(NtCurrentProcess(), READ_CONTROL | TOKEN_QUERY | TOKEN_DUPLICATE, &token);
        if (!NT_SUCCESS(status))
            return AdvSetStatus(status);
    }
    TOKEN_USER *user = nullptr;
    TOKEN_GROUPS *groups = nullptr, *restricted = nullptr, *disabled = nullptr;
    TOKEN_PRIVILEGES *privileges = nullptr;
    status = query_token(token, TokenUser, (PVOID *)&user);
    if (NT_SUCCESS(status))
        status = query_token(token, TokenGroups, (PVOID *)&groups);
    if (NT_SUCCESS(status))
        status = query_token(token, TokenRestrictedSids, (PVOID *)&restricted);
    ULONG inert = 0, returned;
    if (NT_SUCCESS(status))
        status = NtQueryInformationToken(token, TokenSandBoxInert, &inert, sizeof(inert), &returned);
    if (!NT_SUCCESS(status))
        goto done;
    BOOL exempt = inert || exempt_token(user, groups, restricted);
    if (level == SAFER_DISALLOWED && !exempt) {
        if (flags & SAFER_COMPARE_ONLY) {
            *(LONG *)reserved = -1;
            goto done;
        }
        status = (NTSTATUS)0xC0000361; /* STATUS_ACCESS_DISABLED_BY_POLICY_DEFAULT. */
        goto done;
    }
    BOOL changed = !exempt && (flags & SAFER_MAKE_INERT);
    BOOL compare_changed = FALSE;
    if (level == SAFER_NORMALUSER && !exempt) {
        SIZE_T capacity = (SIZE_T)groups->GroupCount + 1;
        if (capacity > (~(SIZE_T)0 - FIELD_OFFSET(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES)) {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        disabled = AdvAlloc(FIELD_OFFSET(TOKEN_GROUPS, Groups) + capacity * sizeof(SID_AND_ATTRIBUTES));
        if (!disabled) {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        disabled->GroupCount = 0;
        for (SIZE_T i = 0; i < capacity; i++) {
            SID_AND_ATTRIBUTES group = i ? groups->Groups[i - 1] : user->User;
            if (!administrative_sid(group.Sid) || (group.Attributes & SE_GROUP_USE_FOR_DENY_ONLY))
                continue;
            changed = TRUE;
            compare_changed |= (group.Attributes & SE_GROUP_ENABLED) != 0;
            disabled->Groups[disabled->GroupCount++] = group;
        }
        if (!(flags & SAFER_COMPARE_ONLY)) {
            status = query_token(token, TokenPrivileges, (PVOID *)&privileges);
            if (!NT_SUCCESS(status))
                goto done;
            for (ULONG i = 0; i < privileges->PrivilegeCount; i++)
                if (privileges->Privileges[i].Luid.HighPart || privileges->Privileges[i].Luid.LowPart != 23)
                    changed = TRUE;
        }
    }
    if (flags & SAFER_COMPARE_ONLY) {
        *(LONG *)reserved = compare_changed ? -1 : 1;
        goto done;
    }
    if ((flags & SAFER_NULL_IF_EQUAL) && !changed) {
        *output = nullptr;
        goto success;
    }
    if (!exempt && (level == SAFER_NORMALUSER || (flags & SAFER_MAKE_INERT))) {
        ULONG filter_flags = (level == SAFER_NORMALUSER ? 1 : 0) | ((flags & SAFER_MAKE_INERT) ? 2 : 0);
        for (ULONG i = 0; i < restricted->GroupCount; i++)
            restricted->Groups[i].Attributes = 0;
        status = NtFilterToken(token, filter_flags, disabled, nullptr, restricted, &filtered);
        if (!NT_SUCCESS(status))
            goto done;
    }
    status = duplicate_primary(filtered ? filtered : token, &result);
    if (!NT_SUCCESS(status))
        goto done;
    if (filtered && level == SAFER_NORMALUSER) {
        status = NtSetInformationToken(result, TokenOwner, &user->User.Sid, sizeof(PSID));
        if (!NT_SUCCESS(status)) {
            NtClose(result);
            goto done;
        }
    }
    *output = result;
success:
    if ((flags & SAFER_WANT_FLAGS) && reserved)
        *(DWORD *)reserved = flags & SAFER_MAKE_INERT ? 0x20000 : 0;
done:
    AdvFree(privileges);
    AdvFree(disabled);
    AdvFree(restricted);
    AdvFree(groups);
    AdvFree(user);
    if (filtered)
        NtClose(filtered);
    if (!input)
        NtClose(token);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}
