/*
 * well known sids. sids are grouped by a shared prefix of an authority and up to two sub authorities, then a
 * relative id. the account domain group uses the caller's domain sid as its prefix.
 */
#include "advapi32_private.h"

typedef enum _WELL_KNOWN_SID_TYPE {
    WinNullSid = 0,
    WinNtAuthoritySid = 7,
    WinLogonIdsSid = 21,
    WinThisOrganizationCertificateSid = 82,
    WinUserModeDriversSid = 98,
} WELL_KNOWN_SID_TYPE;

typedef struct WK_ENTRY {
    DWORD Rid;
    WORD Type;
} WK_ENTRY;

typedef struct WK_GROUP {
    BYTE Authority;
    BYTE Prefix; /* sub authorities before the relative id: 0, 1 or the domain. */
    DWORD First; /* fixed first sub authority when Prefix is 1. */
    BOOL Domain; /* the prefix is the caller's domain sid. */
    const WK_ENTRY *Entries;
    DWORD Count;
} WK_GROUP;

#define E(rid, type) {rid, type}

static const WK_ENTRY g_null[] = {E(0, 0)};
static const WK_ENTRY g_world[] = {E(0, 1)};
static const WK_ENTRY g_local[] = {E(0, 2), E(1, 81)};
static const WK_ENTRY g_creator[] = {E(0, 3), E(1, 4), E(2, 5), E(3, 6), E(4, 71)};
static const WK_ENTRY g_nt[] = {
    E(1, 8),    E(2, 9),    E(3, 10),   E(4, 11),   E(6, 12),   E(7, 13),   E(8, 14),    E(9, 15),
    E(22, 74),  E(10, 16),  E(11, 17),  E(12, 18),  E(13, 19),  E(14, 20),  E(15, 54),   E(1000, 55),
    E(33, 70),  E(17, 63),  E(18, 22),  E(19, 23),  E(20, 24),  E(32, 25),  E(113, 105), E(114, 106),
};
static const WK_ENTRY g_builtin[] = {
    E(544, 26), E(545, 27), E(546, 28), E(547, 29), E(548, 30), E(549, 31), E(550, 32), E(551, 33), E(552, 34),
    E(554, 35), E(555, 36), E(556, 37), E(557, 56), E(558, 57), E(559, 58), E(560, 59), E(561, 60), E(562, 61),
    E(568, 62), E(569, 64), E(573, 76), E(574, 78), E(575, 95), E(576, 96), E(577, 97), E(578, 99), E(579, 101),
    E(580, 102), E(581, 111), E(582, 112), E(583, 119), E(584, 120), E(585, 121),
};
static const WK_ENTRY g_account[] = {
    E(500, 38), E(501, 39), E(502, 40), E(512, 41), E(513, 42), E(514, 43), E(515, 44), E(516, 45),
    E(521, 75), E(517, 46), E(518, 47), E(519, 48), E(520, 49), E(553, 50), E(571, 72), E(572, 73),
    E(498, 77), E(522, 100), E(525, 107), E(503, 110), E(526, 113), E(527, 114),
};
static const WK_ENTRY g_package[] = {E(10, 51), E(21, 52), E(14, 53)};
static const WK_ENTRY g_label[] = {E(0, 65), E(4096, 66), E(8192, 67), E(8448, 79), E(12288, 68), E(16384, 69)};
static const WK_ENTRY g_cert[] = {E(1, 82)};
static const WK_ENTRY g_any_package[] = {E(1, 84)};
static const WK_ENTRY g_capability[] = {E(1, 85), E(2, 86), E(3, 87), E(4, 88), E(5, 89), E(6, 90),
                                        E(7, 91), E(8, 93), E(9, 92), E(10, 94), E(11, 108), E(12, 109)};
static const WK_ENTRY g_authentication[] = {E(1, 103), E(2, 104), E(3, 118), E(4, 115), E(5, 116), E(6, 117)};

#define G(auth, prefix, first, domain, table) {auth, prefix, first, domain, table, RTL_NUMBER_OF(table)}

static const WK_GROUP g_groups[] = {
    G(0, 0, 0, FALSE, g_null),         G(1, 0, 0, FALSE, g_world),      G(2, 0, 0, FALSE, g_local),
    G(3, 0, 0, FALSE, g_creator),      G(5, 0, 0, FALSE, g_nt),         G(5, 1, 32, FALSE, g_builtin),
    G(5, 0, 0, TRUE, g_account),       G(5, 1, 64, FALSE, g_package),   G(16, 0, 0, FALSE, g_label),
    G(5, 1, 65, FALSE, g_cert),        G(15, 1, 2, FALSE, g_any_package), G(15, 1, 3, FALSE, g_capability),
    G(18, 0, 0, FALSE, g_authentication),
};

static void init_authority(SID *sid, BYTE authority, BYTE count)
{
    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = count;
    memset(sid->IdentifierAuthority.Value, 0, 6);
    sid->IdentifierAuthority.Value[5] = authority;
}

ADVAPI BOOL WINAPI CreateWellKnownSid(DWORD type, PSID domain, PSID sid, DWORD *size)
{
    if ((domain && !RtlValidSid(domain)) || !size || type == WinLogonIdsSid) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const WK_GROUP *group = nullptr;
    DWORD rid = 0, count = 0;
    if (type == WinUserModeDriversSid) {
        count = 6;
    } else if (type == WinNtAuthoritySid) {
        count = 0;
    } else {
        for (DWORD g = 0; g < RTL_NUMBER_OF(g_groups) && !group; g++) {
            for (DWORD i = 0; i < g_groups[g].Count; i++) {
                if (g_groups[g].Entries[i].Type == type) {
                    group = &g_groups[g];
                    rid = group->Entries[i].Rid;
                    break;
                }
            }
        }
        if (!group) {
            AdvSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (group->Domain) {
            if (!domain || ((SID *)domain)->SubAuthorityCount == SID_MAX_SUB_AUTHORITIES) {
                AdvSetError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            count = ((SID *)domain)->SubAuthorityCount + 1;
        } else {
            count = group->Prefix + 1;
        }
    }
    DWORD needed = RtlLengthRequiredSid(count);
    DWORD have = *size;
    *size = needed;
    if (have < needed) {
        AdvSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!sid) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SID *out = sid;
    if (group && group->Domain) {
        RtlCopySid(needed, sid, domain);
        out->SubAuthorityCount++;
    } else {
        init_authority(out, type == WinNtAuthoritySid || type == WinUserModeDriversSid ? 5 : group->Authority, (BYTE)count);
        if (type == WinUserModeDriversSid) {
            out->SubAuthority[0] = 84;
            memset(&out->SubAuthority[1], 0, 4 * (count - 1));
        } else if (group && group->Prefix) {
            out->SubAuthority[0] = group->First;
        }
    }
    if (count)
        out->SubAuthority[count - 1] = rid;
    return TRUE;
}

static BOOL authority_equals(const SID *sid, BYTE authority)
{
    const BYTE *v = sid->IdentifierAuthority.Value;
    return !v[0] && !v[1] && !v[2] && !v[3] && !v[4] && v[5] == authority;
}

static DWORD type_of_rid(const WK_ENTRY *entries, DWORD count, DWORD rid)
{
    for (DWORD i = 0; i < count; i++)
        if (entries[i].Rid == rid)
            return entries[i].Type;
    return (DWORD)-1;
}

ADVAPI BOOL WINAPI IsWellKnownSid(PSID sid, DWORD type)
{
    const SID *s = sid;
    if (!RtlValidSid(sid))
        return FALSE;
    AdvSetError(ERROR_SUCCESS);
    DWORD count = s->SubAuthorityCount;
    DWORD found = (DWORD)-1;
    if (count == 0) {
        found = authority_equals(s, 5) ? WinNtAuthoritySid : (DWORD)-1;
    } else if (count == 1) {
        /* only the first group of the authority is checked. */
        for (DWORD g = 0; g < RTL_NUMBER_OF(g_groups); g++) {
            if (authority_equals(s, g_groups[g].Authority)) {
                found = type_of_rid(g_groups[g].Entries, g_groups[g].Count, s->SubAuthority[0]);
                break;
            }
        }
    } else if (authority_equals(s, 5)) {
        DWORD first = s->SubAuthority[0], last = s->SubAuthority[count - 1];
        if (first == 32 && count == 2)
            found = type_of_rid(g_builtin, RTL_NUMBER_OF(g_builtin), last);
        else if (first == 21 && count == 5)
            found = type_of_rid(g_account, RTL_NUMBER_OF(g_account), last);
        else if (first == 5 && count == 3)
            found = WinLogonIdsSid;
        else if (first == 64 && count == 2)
            found = type_of_rid(g_package, RTL_NUMBER_OF(g_package), last);
        else if (first == 65 && count == 2)
            found = type_of_rid(g_cert, RTL_NUMBER_OF(g_cert), last);
        else if (first == 84 && count == 6 && !s->SubAuthority[1] && !s->SubAuthority[2] && !s->SubAuthority[3] &&
                 !s->SubAuthority[4] && !s->SubAuthority[5])
            found = WinUserModeDriversSid;
    } else if (authority_equals(s, 15) && count == 2) {
        if (s->SubAuthority[0] == 2)
            found = type_of_rid(g_any_package, RTL_NUMBER_OF(g_any_package), s->SubAuthority[1]);
        else if (s->SubAuthority[0] == 3)
            found = type_of_rid(g_capability, RTL_NUMBER_OF(g_capability), s->SubAuthority[1]);
    }
    return found != (DWORD)-1 && found == type;
}

ADVAPI BOOL WINAPI GetWindowsAccountDomainSid(PSID sid, PSID domain, DWORD *size)
{
    const SID *s = sid;
    if (!RtlValidSid(sid)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    if (size) {
        if (!authority_equals(s, 5) || s->SubAuthorityCount <= 3 || s->SubAuthority[0] != 21) {
            AdvSetError(ERROR_NON_ACCOUNT_SID);
            return FALSE;
        }
        DWORD needed = RtlLengthRequiredSid(4), have = *size;
        *size = needed;
        if (have < needed) {
            AdvSetError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        if (domain) {
            RtlInitializeSid(domain, RtlIdentifierAuthoritySid(sid), 4);
            memcpy(((SID *)domain)->SubAuthority, s->SubAuthority, 16);
            return TRUE;
        }
    }
    AdvSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

ADVAPI BOOL WINAPI EqualDomainSid(PSID a, PSID b, BOOL *equal)
{
    if (!RtlValidSid(a) || !RtlValidSid(b)) {
        AdvSetError(ERROR_INVALID_SID);
        return FALSE;
    }
    if (!equal) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (((SID *)a)->Revision != ((SID *)b)->Revision) {
        AdvSetError(ERROR_REVISION_MISMATCH);
        return FALSE;
    }
    /* the builtin domain stands in for the aliases that live in it. */
    BYTE builtin[SECURITY_MAX_SID_SIZE], first[SECURITY_MAX_SID_SIZE], second[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(builtin);
    if (!CreateWellKnownSid(25 /* WinBuiltinDomainSid */, nullptr, builtin, &size))
        return FALSE;
    const SID *domains[2];
    PSID sids[2] = {a, b};
    BYTE *storage[2] = {first, second};
    for (int i = 0; i < 2; i++) {
        size = sizeof(first);
        if (GetWindowsAccountDomainSid(sids[i], storage[i], &size)) {
            domains[i] = (const SID *)storage[i];
        } else if (authority_equals(sids[i], 5) && ((SID *)sids[i])->SubAuthorityCount &&
                   ((SID *)sids[i])->SubAuthority[0] == 32) {
            domains[i] = (const SID *)builtin;
        } else {
            AdvSetError(ERROR_NON_DOMAIN_SID);
            return FALSE;
        }
    }
    *equal = RtlEqualSid((PSID)domains[0], (PSID)domains[1]);
    return TRUE;
}
