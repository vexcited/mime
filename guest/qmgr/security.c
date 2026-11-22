#include "qmgr.h"

typedef struct WTS_SESSION_INFOW_ {
    DWORD SessionId;
    LPWSTR pWinStationName;
    int State;
} WTS_SESSION_INFOW_;

typedef BOOL(WINAPI *WTS_ENUMERATE)(HANDLE server, DWORD reserved, DWORD version, WTS_SESSION_INFOW_ **sessions,
                                    DWORD *count);
typedef BOOL(WINAPI *WTS_QUERY_TOKEN)(ULONG session, PHANDLE token);
typedef void(WINAPI *WTS_FREE)(PVOID memory);

#define APPMODEL_ERROR_NO_PACKAGE_ 15700

static SRWLOCK g_wts_lock = SRWLOCK_INIT;
static HMODULE g_wts;
static WTS_ENUMERATE g_enumerate;
static WTS_QUERY_TOKEN g_query_token;
static WTS_FREE g_free;

enum { SID_SYSTEM, SID_LOCAL_SERVICE, SID_NETWORK_SERVICE, SID_ADMINS, SID_NETWORK, SID_RMU, SID_ANONYMOUS, SID_COUNT };

static BYTE g_sids[SID_COUNT][SECURITY_MAX_SID_SIZE];
static BOOL g_sids_ready;

static PSID well_known(int which)
{
    if (!g_sids_ready) {
        static const struct {
            UCHAR count;
            ULONG sub[2];
        } table[SID_COUNT] = {{1, {18}}, {1, {19}}, {1, {20}}, {2, {32, 544}}, {1, {2}}, {2, {32, 580}}, {1, {7}}};
        SID_IDENTIFIER_AUTHORITY nt = {{0, 0, 0, 0, 0, 5}};
        for (int i = 0; i < SID_COUNT; i++) {
            RtlInitializeSid(g_sids[i], &nt, table[i].count);
            for (UCHAR k = 0; k < table[i].count; k++)
                *RtlSubAuthoritySid(g_sids[i], k) = table[i].sub[k];
        }
        g_sids_ready = TRUE;
    }
    return g_sids[which];
}

BOOL SidIsService(PSID sid)
{
    return sid && (EqualSid(sid, well_known(SID_SYSTEM)) || EqualSid(sid, well_known(SID_LOCAL_SERVICE)) ||
                   EqualSid(sid, well_known(SID_NETWORK_SERVICE)));
}

static BOOL member(HANDLE token, int which)
{
    BOOL is = FALSE;
    return CheckTokenMembership(token, well_known(which), &is) && is;
}

/*
 * CNestedImpersonation: the com client when there is one, the service itself for calls of its own threads. the token
 * is kept at the level the client gave, which is enough for AccessCheck and for every question asked of it.
 */
HRESULT CallerOpen(CALLER *c)
{
    memset(c, 0, sizeof(*c));
    BOOL com = SUCCEEDED(CoImpersonateClient());
    if (!com && !ImpersonateSelf(SECURITY_IMPERSONATION_))
        return HRESULT_FROM_WIN32(GetLastError());
    HANDLE token = nullptr;
    BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY_ | TOKEN_DUPLICATE_ | TOKEN_IMPERSONATE_, TRUE, &token);
    DWORD error = GetLastError();
    if (com)
        CoRevertToSelf();
    else
        RevertToSelf();
    if (!opened)
        return error == ERROR_ACCESS_DENIED ? E_ACCESSDENIED : HRESULT_FROM_WIN32(error);
    c->token = token;
    BYTE buffer[sizeof(PSID) * 2 + SECURITY_MAX_SID_SIZE];
    DWORD size;
    if (!GetTokenInformation(token, TOKEN_USER_CLASS_, buffer, sizeof(buffer), &size)) {
        error = GetLastError();
        CallerClose(c);
        return HRESULT_FROM_WIN32(error);
    }
    PSID sid = *(PSID *)buffer;
    CopySid(sizeof(c->sid_buffer), c->sid_buffer, sid);
    c->sid = c->sid_buffer;
    GetTokenInformation(token, TOKEN_IMPERSONATION_LEVEL_CLASS_, &c->level, sizeof(c->level), &size);
    GetTokenInformation(token, TOKEN_ELEVATION_CLASS_, &c->elevated, sizeof(c->elevated), &size);
    GetTokenInformation(token, TOKEN_ELEVATION_TYPE_CLASS_, &c->elevation_type, sizeof(c->elevation_type), &size);
    GetTokenInformation(token, TOKEN_SESSION_ID_CLASS_, &c->session, sizeof(c->session), &size);
    if (GetTokenInformation(token, TOKEN_INTEGRITY_CLASS_, buffer, sizeof(buffer), &size)) {
        PSID label = *(PSID *)buffer;
        UCHAR count = *RtlSubAuthorityCountSid(label);
        c->il = count ? *RtlSubAuthoritySid(label, count - 1u) : 0;
    }
    c->admin = member(token, SID_ADMINS);
    c->service = SidIsService(c->sid);
    c->restricted = IsTokenRestricted(token);
    c->network = member(token, SID_NETWORK);
    c->remote_admin = member(token, SID_RMU);
    c->anonymous = EqualSid(c->sid, well_known(SID_ANONYMOUS));
    UINT32 length = ARRAYSIZE(c->package);
    if (GetPackageFullNameFromToken(token, &length, c->package))
        c->package[0] = 0;
    return S_OK;
}

void CallerClose(CALLER *c)
{
    if (c->token)
        CloseHandle(c->token);
    c->token = nullptr;
}

/* CJobManager::CheckClientAccess and DenyAccessIfRemoteUserWithInsufficientPermissions. */
HRESULT CallerCheckManager(CALLER *c)
{
    if (c->anonymous)
        return E_ACCESSDENIED;
    if (c->network && !c->admin && !c->remote_admin)
        return BG_E_REMOTE_NOT_SUPPORTED;
    if (c->restricted)
        return E_ACCESSDENIED;
    return S_OK;
}

/* ------------------------------------------------------------------ sessions */

static BOOL wts_ready(void)
{
    AcquireSRWLockExclusive(&g_wts_lock);
    if (!g_wts) {
        g_wts = LoadLibraryExW(L"wtsapi32.dll", nullptr, 0x800);
        if (g_wts) {
            g_enumerate = (WTS_ENUMERATE)GetProcAddress(g_wts, "WTSEnumerateSessionsW");
            g_query_token = (WTS_QUERY_TOKEN)GetProcAddress(g_wts, "WTSQueryUserToken");
            g_free = (WTS_FREE)GetProcAddress(g_wts, "WTSFreeMemory");
        }
    }
    BOOL ready = g_enumerate && g_query_token && g_free;
    ReleaseSRWLockExclusive(&g_wts_lock);
    return ready;
}

static BOOL token_is(HANDLE token, PSID sid)
{
    BYTE buffer[sizeof(PSID) * 2 + SECURITY_MAX_SID_SIZE];
    DWORD size;
    return GetTokenInformation(token, TOKEN_USER_CLASS_, buffer, sizeof(buffer), &size) && EqualSid(*(PSID *)buffer, sid);
}

/* the primary token of a logged on session of that user, as CUserSession keeps it on windows. */
HANDLE SessionTokenFor(PSID sid)
{
    if (!sid || !wts_ready())
        return nullptr;
    WTS_SESSION_INFOW_ *sessions = nullptr;
    DWORD count = 0;
    if (!g_enumerate(nullptr, 0, 1, &sessions, &count))
        return nullptr;
    HANDLE found = nullptr;
    for (DWORD i = 0; i < count && !found; i++) {
        HANDLE token;
        if (!g_query_token(sessions[i].SessionId, &token))
            continue;
        if (token_is(token, sid))
            found = token;
        else
            CloseHandle(token);
    }
    g_free(sessions);
    return found;
}

BOOL OwnerLoggedOn(PSID sid)
{
    if (SidIsService(sid))
        return TRUE;
    HANDLE token = SessionTokenFor(sid);
    if (!token)
        return FALSE;
    CloseHandle(token);
    return TRUE;
}

/*
 * the token files are made, renamed and deleted with. a caller at impersonation level gives its own; one that only lets
 * itself be identified gives the token of its logon session, as windows takes the user's logon token for this.
 */
HANDLE CallerOwnerToken(const CALLER *c)
{
    HANDLE source = nullptr, token = nullptr;
    if (c->level >= SECURITY_IMPERSONATION_)
        source = c->token;
    else
        source = SessionTokenFor(c->sid);
    if (source && !DuplicateTokenEx(source, TOKEN_ALL_ACCESS_, nullptr, SECURITY_IMPERSONATION_, TOKEN_IMPERSONATION_,
                                    &token))
        token = nullptr;
    if (source && source != c->token)
        CloseHandle(source);
    return token;
}

void SecurityShutdown(void)
{
    AcquireSRWLockExclusive(&g_wts_lock);
    if (g_wts)
        FreeLibrary(g_wts);
    g_wts = nullptr;
    g_enumerate = nullptr;
    g_query_token = nullptr;
    g_free = nullptr;
    ReleaseSRWLockExclusive(&g_wts_lock);
}

/* ------------------------------------------------------------------ the security of a job */

/*
 * CJobSecurityDescriptor::Initialize: owner and group are the owner, every grant is 0xF003F, anonymous is denied and the
 * label is the owner's integrity level with no write up.
 */
HRESULT JobSecurityCreate(PSID owner, DWORD il, PSECURITY_DESCRIPTOR *sd)
{
    *sd = nullptr;
    WCHAR *text = nullptr;
    if (!ConvertSidToStringSidW(owner, &text))
        return HRESULT_FROM_WIN32(GetLastError());
    WCHAR sddl[512];
    _snwprintf(sddl, ARRAYSIZE(sddl),
               L"O:%sG:%sD:(D;;0xf003f;;;AN)(A;;0xf003f;;;%s)(A;;0xf003f;;;BA)(A;;0xf003f;;;S-1-5-32-580)"
               L"(A;;0xf003f;;;AC)S:(ML;;NW;;;S-1-16-%u)",
               text, text, text, il);
    sddl[ARRAYSIZE(sddl) - 1] = 0;
    LocalFree(text);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, sd, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

BOOL JobIsOwner(BJOB *job, const CALLER *c)
{
    if (!EqualSid(c->sid, job->owner))
        return FALSE;
    if (!job->package || !*job->package)
        return !c->package[0];
    return c->package[0] && !_wcsicmp(job->package, c->package);
}

/*
 * CJob::CheckClientAccess: the security descriptor first, then the owner rules. an owner may not come back less
 * elevated than it made the job, and anybody else must be elevated, whatever the descriptor grants.
 */
HRESULT JobCheckAccess(BJOB *job, const CALLER *c, DWORD access, BOOL *owner)
{
    static const GENERIC_MAPPING mapping = {BITS_READ, BITS_WRITE, BITS_READ, BITS_ALL};
    if (owner)
        *owner = FALSE;
    if ((access & ~(DWORD)BITS_READ) && (job->state == BG_JOB_STATE_ACKNOWLEDGED || job->state == BG_JOB_STATE_CANCELLED))
        return BG_E_INVALID_STATE;
    BYTE privileges[256];
    DWORD length = sizeof(privileges), granted = 0;
    BOOL status = FALSE;
    if (!AccessCheck(job->sd, c->token, access, (void *)&mapping, privileges, &length, &granted, &status))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!status || (access != MAXIMUM_ALLOWED_ && granted != access))
        return E_ACCESSDENIED;
    if (JobIsOwner(job, c)) {
        if (owner)
            *owner = TRUE;
        if (c->elevated < job->owner_elevated)
            return E_ACCESSDENIED;
    } else if (!c->elevated)
        return E_ACCESSDENIED;
    if (c->elevation_type != ELEVATION_LIMITED_ && c->restricted)
        return E_ACCESSDENIED;
    JobAccessed(job);
    return S_OK;
}
