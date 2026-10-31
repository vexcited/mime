/*
 * users, groups and shares. the machine is a workstation whose only account is the logged on user, with no writable
 * account database and no server service: reads answer for that account, writes are refused, and a domain controller
 * lookup finds no such domain.
 */
#include <mime/nt.h>
#include <mime/win32/winbase.h>

#define NETAPI DECLSPEC_EXPORT

typedef DWORD NET_API_STATUS;
typedef LPCWSTR LMCSTR;

#define NERR_Success 0
#define NERR_ServerNotStarted 2114
#define NERR_UserNotFound 2221
#define NERR_GroupNotFound 2220
#define MAX_PREFERRED_LENGTH ((DWORD)-1)
#define ERROR_NO_SUCH_DOMAIN_CODE 1355

NETAPI NET_API_STATUS WINAPI NetApiBufferAllocate(DWORD size, LPVOID *buffer);
NETAPI NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buffer);

static BOOL local_server(LMCSTR server)
{
    if (!server || !*server)
        return TRUE;
    while (*server == L'\\')
        server++;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = RTL_NUMBER_OF(computer);
    return !_wcsicmp(server, L".") || !_wcsicmp(server, L"localhost") ||
           (GetComputerNameW(computer, &n) && !_wcsicmp(server, computer));
}

static BOOL user_name(WCHAR *name, DWORD size)
{
    typedef BOOL (WINAPI *GET_USER_NAME)(LPWSTR, LPDWORD);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    GET_USER_NAME get = advapi ? (GET_USER_NAME)GetProcAddress(advapi, "GetUserNameW") : nullptr;
    BOOL ok = get && get(name, &size);
    if (advapi)
        FreeLibrary(advapi);
    return ok;
}

static BOOL logged_on_user(LMCSTR user)
{
    WCHAR name[257];
    return user && user_name(name, RTL_NUMBER_OF(name)) && !_wcsicmp(user, name);
}

typedef struct NAME_ENTRY {
    PCWSTR Name;
    PCWSTR Comment;
    DWORD Attributes;
} NAME_ENTRY;

/* the buffer holds the fixed parts, then their strings. */
static NET_API_STATUS name_list(const NAME_ENTRY *entries, DWORD count, DWORD level, BOOL with_comment_or_attribute,
                                LPBYTE *buffer, DWORD *returned, DWORD *total)
{
    SIZE_T entry_size = level ? 2 * sizeof(PVOID) : sizeof(PVOID);
    SIZE_T bytes = entry_size * count;
    for (DWORD i = 0; i < count; i++) {
        bytes += (wcslen(entries[i].Name) + 1) * sizeof(WCHAR);
        if (level && with_comment_or_attribute && entries[i].Comment)
            bytes += (wcslen(entries[i].Comment) + 1) * sizeof(WCHAR);
    }
    BYTE *memory;
    NET_API_STATUS status = NetApiBufferAllocate((DWORD)bytes, (LPVOID *)&memory);
    if (status)
        return status;
    BYTE *strings = memory + entry_size * count;
    for (DWORD i = 0; i < count; i++) {
        PVOID *fields = (PVOID *)(memory + entry_size * i);
        SIZE_T length = (wcslen(entries[i].Name) + 1) * sizeof(WCHAR);
        memcpy(strings, entries[i].Name, length);
        fields[0] = strings;
        strings += length;
        if (level) {
            if (with_comment_or_attribute && entries[i].Comment) {
                length = (wcslen(entries[i].Comment) + 1) * sizeof(WCHAR);
                memcpy(strings, entries[i].Comment, length);
                fields[1] = strings;
                strings += length;
            } else if (!with_comment_or_attribute)
                *(DWORD *)&fields[1] = entries[i].Attributes;
        }
    }
    *buffer = memory;
    *returned = *total = count;
    return NERR_Success;
}

/* the aliases of the account database, which are the two the account belongs to. */
static const NAME_ENTRY local_groups[] = {
    {L"Administrators", L"Administrators have complete and unrestricted access to the computer/domain", 0},
    {L"Users", L"Users are prevented from making accidental or intentional system-wide changes and can run most applications", 0}};

NETAPI NET_API_STATUS WINAPI NetUserGetLocalGroups(LMCSTR server, LMCSTR user, DWORD level, DWORD flags,
                                                    LPBYTE *buffer, DWORD preferred, LPDWORD entries, LPDWORD total)
{
    if (!buffer || !entries || !total || !user)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = *total = 0;
    if (level > 1)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    if (!logged_on_user(user))
        return NERR_UserNotFound;
    return name_list(local_groups, RTL_NUMBER_OF(local_groups), level, TRUE, buffer, entries, total);
}

/* the account has no global groups but None, which every local account has. */
NETAPI NET_API_STATUS WINAPI NetUserGetGroups(LMCSTR server, LMCSTR user, DWORD level, LPBYTE *buffer, DWORD preferred,
                                              LPDWORD entries, LPDWORD total)
{
    if (!buffer || !entries || !total || !user)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = *total = 0;
    if (level > 1)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    if (!logged_on_user(user))
        return NERR_UserNotFound;
    /* 7 is SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED. */
    static const NAME_ENTRY groups[] = {{L"None", nullptr, 7}};
    return name_list(groups, RTL_NUMBER_OF(groups), level, FALSE, buffer, entries, total);
}

/* the account database is read only. */
static NET_API_STATUS refuse(LMCSTR server)
{
    return local_server(server) ? ERROR_NOT_SUPPORTED : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetUserAdd(LMCSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (level < 1 || level > 4)
        return ERROR_INVALID_LEVEL;
    return refuse(server);
}

NETAPI NET_API_STATUS WINAPI NetUserDel(LMCSTR server, LMCSTR user)
{
    return user ? refuse(server) : ERROR_INVALID_PARAMETER;
}

NETAPI NET_API_STATUS WINAPI NetUserSetInfo(LMCSTR server, LMCSTR user, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (!user || !buffer)
        return ERROR_INVALID_PARAMETER;
    return refuse(server);
}

NETAPI NET_API_STATUS WINAPI NetGroupAddUser(LMCSTR server, LMCSTR group, LMCSTR user)
{
    return group && user ? refuse(server) : ERROR_INVALID_PARAMETER;
}

NETAPI NET_API_STATUS WINAPI NetGroupDelUser(LMCSTR server, LMCSTR group, LMCSTR user)
{
    return group && user ? refuse(server) : ERROR_INVALID_PARAMETER;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupAddMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    return group && buffer ? refuse(server) : ERROR_INVALID_PARAMETER;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDelMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    return group && buffer ? refuse(server) : ERROR_INVALID_PARAMETER;
}

/* the server service is not running, so there are no shares to read, change or end. */
NETAPI NET_API_STATUS WINAPI NetShareDel(LMCSTR server, LMCSTR share, DWORD reserved)
{
    if (!share)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetShareSetInfo(LMCSTR server, LMCSTR share, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (!share || !buffer)
        return ERROR_INVALID_PARAMETER;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

NETAPI NET_API_STATUS WINAPI NetShareGetInfo(LMCSTR server, LMCSTR share, DWORD level, LPBYTE *buffer)
{
    if (!share || !buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    return local_server(server) ? NERR_ServerNotStarted : ERROR_BAD_NETPATH;
}

/* a workgroup machine has no domain, and so no controller of one. */
NETAPI DWORD WINAPI DsGetDcNameW(LMCSTR computer, LMCSTR domain, GUID *guid, LMCSTR site, ULONG flags, PVOID *info)
{
    if (!info)
        return ERROR_INVALID_PARAMETER;
    *info = nullptr;
    if (computer && *computer && !local_server(computer))
        return ERROR_BAD_NETPATH;
    return ERROR_NO_SUCH_DOMAIN_CODE;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupEnum(LMCSTR server, DWORD level, LPBYTE *buffer, DWORD preferred,
                                               LPDWORD entries, LPDWORD total, PDWORD_PTR resume)
{
    if (!buffer || !entries || !total)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = *total = 0;
    if (level > 1)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return name_list(local_groups, RTL_NUMBER_OF(local_groups), level, TRUE, buffer, entries, total);
}

/* local groups by name, the account policy and the other calls of the account database that Mime refuses. */
static BOOL known_group(LMCSTR name, DWORD *index)
{
    for (DWORD i = 0; name && i < RTL_NUMBER_OF(local_groups); i++) {
        if (!_wcsicmp(name, local_groups[i].Name)) {
            if (index)
                *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

#define NERR_GroupExists 2223
#define NERR_SpeGroupOp 2234
#define ERROR_NO_SUCH_ALIAS_CODE 1376

NETAPI NET_API_STATUS WINAPI NetLocalGroupAdd(LMCSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (level != 0 && level != 1)
        return ERROR_INVALID_LEVEL;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return known_group(*(LMCSTR *)buffer, nullptr) ? NERR_GroupExists : refuse(server);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDel(LMCSTR server, LMCSTR group)
{
    if (!group)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return known_group(group, nullptr) ? NERR_SpeGroupOp : NERR_GroupNotFound;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupGetInfo(LMCSTR server, LMCSTR group, DWORD level, LPBYTE *buffer)
{
    if (!buffer || !group)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    if (level != 0 && level != 1 && level != 1002)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    DWORD index;
    if (!known_group(group, &index))
        return NERR_GroupNotFound;
    LPBYTE list;
    DWORD entries, total;
    NET_API_STATUS status = name_list(&local_groups[index], 1, level == 1002 ? 1 : level, TRUE, &list, &entries, &total);
    if (status)
        return status;
    if (level == 1002) {
        /* level 1002 is the comment alone. */
        *(PVOID *)list = ((PVOID *)list)[1];
    }
    *buffer = list;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupSetInfo(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (!group || !buffer)
        return ERROR_INVALID_PARAMETER;
    if (level != 0 && level != 1 && level != 1002)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return known_group(group, nullptr) ? refuse(server) : NERR_GroupNotFound;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupAddMember(LMCSTR server, LMCSTR group, PVOID sid)
{
    if (!group || !sid)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return known_group(group, nullptr) ? refuse(server) : ERROR_NO_SUCH_ALIAS_CODE;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDelMember(LMCSTR server, LMCSTR group, PVOID sid)
{
    return NetLocalGroupAddMember(server, group, sid);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupSetMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    if (!group || (!buffer && count))
        return ERROR_INVALID_PARAMETER;
    if (level > 3)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    return known_group(group, nullptr) ? refuse(server) : ERROR_NO_SUCH_ALIAS_CODE;
}

/* the policy of the accounts, which is the default of a client. */
typedef struct USER_MODALS_0 {
    DWORD MinPasswordLength, MaxPasswordAge, MinPasswordAge, ForceLogoff, PasswordHistoryLength;
} USER_MODALS_0;

typedef struct USER_MODALS_3 {
    DWORD LockoutDuration, LockoutObservationWindow, LockoutThreshold;
} USER_MODALS_3;

NETAPI NET_API_STATUS WINAPI NetUserModalsGet(LMCSTR server, DWORD level, LPBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    switch (level) {
    case 0: {
        USER_MODALS_0 *info;
        NET_API_STATUS status = NetApiBufferAllocate(sizeof(*info), (LPVOID *)&info);
        if (status)
            return status;
        /* no minimum length, passwords last 42 days, there is no history and no forced logoff. */
        *info = (USER_MODALS_0){0, 42 * 24 * 3600, 0, 0xFFFFFFFF, 0};
        *buffer = (LPBYTE)info;
        return NERR_Success;
    }
    case 3: {
        USER_MODALS_3 *info;
        NET_API_STATUS status = NetApiBufferAllocate(sizeof(*info), (LPVOID *)&info);
        if (status)
            return status;
        *info = (USER_MODALS_3){30 * 60, 30 * 60, 0};
        *buffer = (LPBYTE)info;
        return NERR_Success;
    }
    case 1:
    case 2:
    case 1001:
    case 1002:
    case 1003:
    case 1004:
    case 1005:
    case 1006:
    case 1007:
    case 1008:
    case 1009:
    case 1010:
    case 1011:
        return ERROR_NOT_SUPPORTED;
    default:
        return ERROR_INVALID_LEVEL;
    }
}

NETAPI NET_API_STATUS WINAPI NetUserModalsSet(LMCSTR server, DWORD level, LPBYTE buffer, LPDWORD parameter)
{
    if (parameter)
        *parameter = 0;
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return refuse(server);
}

NETAPI NET_API_STATUS WINAPI NetUserSetGroups(LMCSTR server, LMCSTR user, DWORD level, LPBYTE buffer, DWORD count)
{
    if (!user || (!buffer && count))
        return ERROR_INVALID_PARAMETER;
    if (level != 0 && level != 1)
        return ERROR_INVALID_LEVEL;
    return refuse(server);
}

/* the only global group is None, and the account is in it. */
NETAPI NET_API_STATUS WINAPI NetGroupGetUsers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE *buffer, DWORD preferred,
                                              LPDWORD entries, LPDWORD total, PDWORD_PTR resume)
{
    if (!buffer || !entries || !total || !group)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = *total = 0;
    if (level > 1)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    WCHAR name[257];
    if (_wcsicmp(group, L"None"))
        return NERR_GroupNotFound;
    if (!user_name(name, RTL_NUMBER_OF(name)))
        return NERR_UserNotFound;
    /* 7 is SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED. */
    NAME_ENTRY member = {name, nullptr, 7};
    return name_list(&member, 1, level, FALSE, buffer, entries, total);
}

#define ERROR_NO_SUCH_ALIAS_ 1376
#define SidTypeUser_ 1
#define SidTypeWellKnownGroup_ 5

typedef struct MEMBER {
    const BYTE *Sid;
    DWORD Usage;
    WCHAR Name[300];
} MEMBER;

NETAPI NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buffer);

/* administrators are the account, and users are whoever is signed in, as they are on a workstation. */
NETAPI NET_API_STATUS WINAPI NetLocalGroupGetMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE *buffer,
                                                     DWORD preferred, LPDWORD entries, LPDWORD total, PDWORD_PTR resume)
{
    if (!buffer || !entries || !total || !group)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = *total = 0;
    if (level > 3)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    static const BYTE interactive[] = {1, 1, 0, 0, 0, 0, 0, 5, 4, 0, 0, 0};
    static const BYTE authenticated[] = {1, 1, 0, 0, 0, 0, 0, 5, 11, 0, 0, 0};
    MEMBER members[2] = {};
    DWORD count = 0;
    LPBYTE info = nullptr;
    if (!_wcsicmp(group, L"Administrators")) {
        WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1], user[257];
        DWORD n = RTL_NUMBER_OF(computer);
        if (!GetComputerNameW(computer, &n) || !user_name(user, RTL_NUMBER_OF(user)))
            return NERR_UserNotFound;
        NET_API_STATUS status = NetUserGetInfo(nullptr, user, 23, &info);
        if (status)
            return status;
        /* the sid of level 23 is its last field. */
        members[0].Sid = *(const BYTE **)(info + 3 * sizeof(PVOID) + sizeof(PVOID));
        members[0].Usage = SidTypeUser_;
        _snwprintf(members[0].Name, RTL_NUMBER_OF(members[0].Name), L"%ls\\%ls", computer, user);
        count = 1;
    } else if (!_wcsicmp(group, L"Users")) {
        members[0] = (MEMBER){interactive, SidTypeWellKnownGroup_, L"NT AUTHORITY\\INTERACTIVE"};
        members[1] = (MEMBER){authenticated, SidTypeWellKnownGroup_, L"NT AUTHORITY\\Authenticated Users"};
        count = 2;
    } else {
        return ERROR_NO_SUCH_ALIAS_;
    }
    SIZE_T fixed = level == 0 ? sizeof(PVOID) : level == 3 ? sizeof(PVOID) : 3 * sizeof(PVOID);
    SIZE_T bytes = fixed * count;
    for (DWORD i = 0; i < count; i++)
        bytes += 8 + 4 * members[i].Sid[1] + 8 + (wcslen(members[i].Name) + 1) * sizeof(WCHAR);
    BYTE *memory;
    NET_API_STATUS status = NetApiBufferAllocate((DWORD)bytes, (LPVOID *)&memory);
    if (status) {
        NetApiBufferFree(info);
        return status;
    }
    memset(memory, 0, bytes);
    BYTE *tail = memory + fixed * count;
    for (DWORD i = 0; i < count; i++) {
        PVOID *fields = (PVOID *)(memory + fixed * i);
        SIZE_T sid_bytes = 8 + 4 * members[i].Sid[1], name_bytes = (wcslen(members[i].Name) + 1) * sizeof(WCHAR);
        BYTE *sid = (BYTE *)(((ULONG_PTR)tail + 7) & ~(ULONG_PTR)7);
        memcpy(sid, members[i].Sid, sid_bytes);
        WCHAR *name = (WCHAR *)(sid + sid_bytes);
        memcpy(name, members[i].Name, name_bytes);
        tail = (BYTE *)name + name_bytes;
        if (level == 3) {
            fields[0] = name;
        } else {
            fields[0] = sid;
            if (level) {
                *(DWORD *)&fields[1] = members[i].Usage;
                fields[2] = name;
            }
        }
    }
    NetApiBufferFree(info);
    *buffer = memory;
    *entries = *total = count;
    return NERR_Success;
}

/* the account database is read only, whichever the account. */
NETAPI NET_API_STATUS WINAPI NetUserChangePassword(LMCSTR domain, LMCSTR user, LMCSTR old_password, LMCSTR new_password)
{
    if (!old_password || !new_password)
        return ERROR_INVALID_PARAMETER;
    return refuse(domain);
}

#define NERR_DCNotFound 2453

/* a machine outside a domain has no controller of one to find, which is what asking for the local one answers too. */
NETAPI NET_API_STATUS WINAPI NetGetDCName(LMCSTR server, LMCSTR domain, LPBYTE *buffer)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    if (server && *server && !local_server(server))
        return ERROR_BAD_NETPATH;
    return NERR_DCNotFound;
}

NETAPI DWORD WINAPI DsGetDcNameA(LPCSTR computer, LPCSTR domain, GUID *guid, LPCSTR site, ULONG flags, PVOID *info)
{
    if (!info)
        return ERROR_INVALID_PARAMETER;
    *info = nullptr;
    WCHAR wide[MAX_COMPUTERNAME_LENGTH + 3] = {};
    if (computer && *computer &&
        !MultiByteToWideChar(CP_ACP, 0, computer, -1, wide, RTL_NUMBER_OF(wide)))
        return ERROR_BAD_NETPATH;
    return DsGetDcNameW(computer && *computer ? wide : nullptr, nullptr, guid, nullptr, flags, info);
}

#define RPC_S_SERVER_UNAVAILABLE_ 1722

/* there is no distributed file system service to ask about its namespaces. */
NETAPI NET_API_STATUS WINAPI NetDfsEnum(LPWSTR name, DWORD level, DWORD preferred, LPBYTE *buffer, LPDWORD entries,
                                        LPDWORD resume)
{
    if (!name || !buffer || !entries)
        return ERROR_INVALID_PARAMETER;
    *buffer = nullptr;
    *entries = 0;
    if (level < 1 || (level > 4 && level != 200 && level != 300))
        return ERROR_INVALID_LEVEL;
    return local_server(name) ? RPC_S_SERVER_UNAVAILABLE_ : ERROR_BAD_NETPATH;
}
