/*
 * the management of users, groups and shares. The machine is a workstation whose only account is the one that is
 * logged on, and it has neither an account database that can be changed nor a server service, so what reads answers
 * for that account and what writes is refused, and a domain controller is a domain that does not exist.
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

/* whether the name is the one of the logged on user, the only account of the machine. */
static BOOL logged_on_user(LMCSTR user)
{
    typedef BOOL (WINAPI *GET_USER_NAME)(LPWSTR, LPDWORD);
    WCHAR name[257];
    DWORD size = RTL_NUMBER_OF(name);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    GET_USER_NAME get = advapi ? (GET_USER_NAME)GetProcAddress(advapi, "GetUserNameW") : nullptr;
    BOOL known = user && get && get(name, &size) && !_wcsicmp(user, name);
    if (advapi)
        FreeLibrary(advapi);
    return known;
}

typedef struct NAME_ENTRY {
    PCWSTR Name;
    PCWSTR Comment;
    DWORD Attributes;
} NAME_ENTRY;

/* a buffer of fixed parts followed by their strings, as the functions of this family hand it out. */
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

/* the groups of the logged on user: an administrator of the machine and a user. */
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
    static const NAME_ENTRY groups[] = {{L"Administrators", L"Administrators have complete and unrestricted access to the computer/domain", 0},
                                        {L"Users", L"Users are prevented from making accidental or intentional system-wide changes and can run most applications", 0}};
    return name_list(groups, RTL_NUMBER_OF(groups), level, TRUE, buffer, entries, total);
}

/* the account has no global groups; the one every local account has is None. */
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
    /* the group is mandatory, enabled by default and enabled: SE_GROUP_MANDATORY | ENABLED_BY_DEFAULT | ENABLED. */
    static const NAME_ENTRY groups[] = {{L"None", nullptr, 7}};
    return name_list(groups, RTL_NUMBER_OF(groups), level, FALSE, buffer, entries, total);
}

/* nothing of the account database can be written. */
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
