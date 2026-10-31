/*
 * local groups that programs make. the machine has two groups of its own, Administrators and Users, whose members are
 * fixed (accounts.c); the others are kept in the registry, each with its relative id, its comment and the sids of its
 * members, and the lookups of names and sids in advapi32 read the same key. a member of a group does not change the
 * token of a process: the groups of a logon are those it was made with.
 */
#include <mime/nt.h>
#include <mime/win32/winbase.h>

#define NETAPI DECLSPEC_EXPORT

typedef DWORD NET_API_STATUS;
typedef LPCWSTR LMCSTR;

#define NERR_Success 0
#define NERR_GroupNotFound 2220
#define NERR_SpeGroupOp 2234
#define ERROR_NO_SUCH_ALIAS_CODE 1376
#define ERROR_MEMBER_NOT_IN_ALIAS_CODE 1377
#define ERROR_MEMBER_IN_ALIAS_CODE 1378
#define ERROR_ALIAS_EXISTS_CODE 1379
#define ERROR_NONE_MAPPED_CODE 1332
#define ALIAS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Lsa\\MimeAliases"
#define FIRST_ALIAS_RID 1100

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

static const struct {
    PCWSTR Name, Comment;
} g_builtin[] = {
    {L"Administrators", L"Administrators have complete and unrestricted access to the computer/domain"},
    {L"Users", L"Users are prevented from making accidental or intentional system-wide changes and can run most applications"},
};

BOOL AliasIsBuiltin(LMCSTR name)
{
    for (DWORD i = 0; name && i < RTL_NUMBER_OF(g_builtin); i++)
        if (!_wcsicmp(name, g_builtin[i].Name))
            return TRUE;
    return FALSE;
}

/* the other groups of every machine. they cannot be made or deleted, but programs add members to them. */
static const struct {
    PCWSTR Name, Comment;
} g_stock[] = {
    {L"Guests", L"Guests have the same access as members of the Users group by default, except for the Guest account which is further restricted"},
    {L"Power Users", L"Power Users are included for backwards compatibility and possess limited administrative powers"},
    {L"Backup Operators", L"Backup Operators can override security restrictions for the sole purpose of backing up or restoring files"},
    {L"Replicator", L"Supports file replication in a domain"},
    {L"Remote Desktop Users", L"Members in this group are granted the right to logon remotely"},
    {L"Network Configuration Operators", L"Members in this group can have some administrative privileges to manage configuration of networking features"},
    {L"Performance Monitor Users", L"Members of this group can access performance counter data locally and remotely"},
    {L"Performance Log Users", L"Members of this group may schedule logging of performance counters, enable trace providers, and collect event traces both locally and via remote access to this computer"},
    {L"Distributed COM Users", L"Members are allowed to launch, activate and use Distributed COM objects on this machine."},
    {L"IIS_IUSRS", L"Built-in group used by Internet Information Services."},
    {L"Cryptographic Operators", L"Members are authorized to perform cryptographic operations."},
    {L"Event Log Readers", L"Members of this group can read event logs from local machine"},
    {L"Access Control Assistance Operators", L"Members of this group can remotely query authorization attributes and permissions for resources on this computer"},
    {L"Remote Management Users", L"Members of this group can access WMI resources over management protocols (such as WS-Management via the Windows Remote Management service). This applies only to WMI namespaces that grant access to the user."},
    {L"Device Owners", L"Members of this group can change system-wide settings."},
};

static int stock_index(LMCSTR name)
{
    for (int i = 0; name && i < (int)RTL_NUMBER_OF(g_stock); i++)
        if (!_wcsicmp(name, g_stock[i].Name))
            return i;
    return -1;
}

DWORD AliasStockCount(void)
{
    return RTL_NUMBER_OF(g_stock);
}

PCWSTR AliasStockName(DWORD index)
{
    return g_stock[index].Name;
}

PCWSTR AliasStockComment(DWORD index)
{
    return g_stock[index].Comment;
}

/* ------------------------------------------------------------------ advapi32 by name */

typedef BOOL(WINAPI *LOOKUP_NAME)(LPCWSTR, LPCWSTR, PVOID, LPDWORD, LPWSTR, LPDWORD, PDWORD);
typedef BOOL(WINAPI *LOOKUP_SID)(LPCWSTR, PVOID, LPWSTR, LPDWORD, LPWSTR, LPDWORD, PDWORD);
typedef BOOL(WINAPI *SID_TO_STRING)(PVOID, LPWSTR *);
typedef BOOL(WINAPI *STRING_TO_SID)(LPCWSTR, PVOID *);

static struct {
    LOOKUP_NAME lookup_name;
    LOOKUP_SID lookup_sid;
    SID_TO_STRING to_string;
    STRING_TO_SID from_string;
} g_adv;

static BOOL adv_ready(void)
{
    if (!g_adv.from_string) {
        HMODULE adv = LoadLibraryW(L"advapi32.dll");
        if (!adv)
            return FALSE;
        g_adv.lookup_name = (LOOKUP_NAME)GetProcAddress(adv, "LookupAccountNameW");
        g_adv.lookup_sid = (LOOKUP_SID)GetProcAddress(adv, "LookupAccountSidW");
        g_adv.to_string = (SID_TO_STRING)GetProcAddress(adv, "ConvertSidToStringSidW");
        g_adv.from_string = (STRING_TO_SID)GetProcAddress(adv, "ConvertStringSidToSidW");
    }
    return g_adv.lookup_name && g_adv.lookup_sid && g_adv.to_string && g_adv.from_string;
}

/* ------------------------------------------------------------------ the key of a group */

static BOOL valid_name(LMCSTR name)
{
    SIZE_T n = name ? wcslen(name) : 0;
    if (!n || n > 256)
        return FALSE;
    BOOL plain = FALSE;
    for (const WCHAR *c = name; *c; c++) {
        if (*c < 32 || wcschr(L"\"/\\[]:|<>+=;,?*@", *c))
            return FALSE;
        plain |= *c != L'.' && *c != L' ';
    }
    return plain;
}

static LONG open_alias(LMCSTR name, BOOL create, REGSAM access, HKEY *key)
{
    WCHAR path[400];
    _snwprintf(path, RTL_NUMBER_OF(path), ALIAS_KEY L"\\%ls", name);
    if (create)
        return RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, key, nullptr);
    return RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, key);
}

static BOOL alias_exists(LMCSTR name)
{
    HKEY key;
    if (!valid_name(name) || open_alias(name, FALSE, KEY_QUERY_VALUE, &key))
        return FALSE;
    RegCloseKey(key);
    return TRUE;
}

static DWORD allocate_rid(void)
{
    HKEY root;
    DWORD rid = FIRST_ALIAS_RID, size = sizeof(rid), type;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, ALIAS_KEY, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr))
        return 0;
    if (RegQueryValueExW(root, L"NextRid", nullptr, &type, (BYTE *)&rid, &size) || type != REG_DWORD)
        rid = FIRST_ALIAS_RID;
    DWORD next = rid + 1;
    RegSetValueExW(root, L"NextRid", 0, REG_DWORD, (const BYTE *)&next, sizeof(next));
    RegCloseKey(root);
    return rid;
}

static PWSTR read_text(HKEY key, PCWSTR name)
{
    DWORD size = 0, type;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) || type != REG_SZ)
        return nullptr;
    PWSTR text = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size + 2 * sizeof(WCHAR));
    if (text && RegQueryValueExW(key, name, nullptr, &type, (BYTE *)text, &size)) {
        LocalFree(text);
        text = nullptr;
    }
    return text;
}

/* the members as a multi string of sids in text. */
static PWSTR read_members(HKEY key)
{
    DWORD size = 0, type;
    if (RegQueryValueExW(key, L"Members", nullptr, &type, nullptr, &size) || type != REG_MULTI_SZ)
        size = 0;
    PWSTR list = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size + 4 * sizeof(WCHAR));
    if (list && size)
        RegQueryValueExW(key, L"Members", nullptr, &type, (BYTE *)list, &size);
    return list;
}

static BOOL has_member(PCWSTR list, PCWSTR sid)
{
    for (; *list; list += wcslen(list) + 1)
        if (!_wcsicmp(list, sid))
            return TRUE;
    return FALSE;
}

static SIZE_T list_chars(PCWSTR list)
{
    SIZE_T n = 0;
    for (; list[n]; n += wcslen(list + n) + 1)
        ;
    return n;
}

static void write_members(HKEY key, PCWSTR list)
{
    SIZE_T chars = list_chars(list) + 1;
    if (chars == 1)
        chars = 2;
    RegSetValueExW(key, L"Members", 0, REG_MULTI_SZ, (const BYTE *)list, (DWORD)(chars * sizeof(WCHAR)));
}

/* ------------------------------------------------------------------ the calls */

typedef struct LOCALGROUP_INFO_1 {
    LPWSTR Name, Comment;
} LOCALGROUP_INFO_1;

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
    LOCALGROUP_INFO_1 *info = (LOCALGROUP_INFO_1 *)buffer;
    if (!valid_name(info->Name))
        return ERROR_INVALID_NAME;
    if (AliasIsBuiltin(info->Name) || stock_index(info->Name) >= 0 || alias_exists(info->Name))
        return ERROR_ALIAS_EXISTS_CODE;
    DWORD rid = allocate_rid();
    HKEY key;
    if (!rid || open_alias(info->Name, TRUE, KEY_ALL_ACCESS, &key))
        return ERROR_ACCESS_DENIED;
    RegSetValueExW(key, L"Rid", 0, REG_DWORD, (const BYTE *)&rid, sizeof(rid));
    PCWSTR comment = level == 1 && info->Comment ? info->Comment : L"";
    RegSetValueExW(key, L"Comment", 0, REG_SZ, (const BYTE *)comment, (DWORD)((wcslen(comment) + 1) * sizeof(WCHAR)));
    write_members(key, L"\0");
    RegCloseKey(key);
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDel(LMCSTR server, LMCSTR group)
{
    if (!group)
        return ERROR_INVALID_PARAMETER;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    if (AliasIsBuiltin(group) || stock_index(group) >= 0)
        return NERR_SpeGroupOp;
    if (!alias_exists(group))
        return NERR_GroupNotFound;
    WCHAR path[400];
    _snwprintf(path, RTL_NUMBER_OF(path), ALIAS_KEY L"\\%ls", group);
    return RegDeleteKeyW(HKEY_LOCAL_MACHINE, path) ? ERROR_ACCESS_DENIED : NERR_Success;
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
    PWSTR comment = nullptr;
    PCWSTR name = nullptr;
    DWORD index;
    HKEY key;
    BOOL stock = FALSE;
    for (index = 0; index < RTL_NUMBER_OF(g_builtin) && _wcsicmp(group, g_builtin[index].Name); index++)
        ;
    if (index < RTL_NUMBER_OF(g_builtin)) {
        name = g_builtin[index].Name;
        comment = (PWSTR)g_builtin[index].Comment;
    } else if (stock_index(group) >= 0) {
        name = g_stock[stock_index(group)].Name;
        comment = (PWSTR)g_stock[stock_index(group)].Comment;
        stock = TRUE;
    } else if (valid_name(group) && !open_alias(group, FALSE, KEY_QUERY_VALUE, &key)) {
        comment = read_text(key, L"Comment");
        RegCloseKey(key);
        name = group;
    } else {
        return NERR_GroupNotFound;
    }
    SIZE_T name_bytes = (wcslen(name) + 1) * sizeof(WCHAR), comment_bytes = ((comment ? wcslen(comment) : 0) + 1) * sizeof(WCHAR);
    SIZE_T fixed = level == 0 || level == 1002 ? sizeof(PVOID) : 2 * sizeof(PVOID);
    BYTE *memory;
    NET_API_STATUS status = NetApiBufferAllocate((DWORD)(fixed + name_bytes + comment_bytes), (LPVOID *)&memory);
    if (status) {
        if (comment && !stock && comment != (PWSTR)g_builtin[0].Comment && comment != (PWSTR)g_builtin[1].Comment)
            LocalFree(comment);
        return status;
    }
    WCHAR *name_copy = (WCHAR *)(memory + fixed), *comment_copy = (WCHAR *)(memory + fixed + name_bytes);
    memcpy(name_copy, name, name_bytes);
    if (comment)
        memcpy(comment_copy, comment, comment_bytes);
    else
        *comment_copy = 0;
    PVOID *fields = (PVOID *)memory;
    if (level == 1002)
        fields[0] = comment_copy;
    else {
        fields[0] = name_copy;
        if (level == 1)
            fields[1] = comment_copy;
    }
    if (comment && !stock && comment != (PWSTR)g_builtin[0].Comment && comment != (PWSTR)g_builtin[1].Comment)
        LocalFree(comment);
    *buffer = memory;
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
    if (AliasIsBuiltin(group) || stock_index(group) >= 0)
        return ERROR_ACCESS_DENIED;
    HKEY key;
    if (!valid_name(group) || open_alias(group, FALSE, KEY_ALL_ACCESS, &key))
        return NERR_GroupNotFound;
    NET_API_STATUS status = NERR_Success;
    if (level == 1 || level == 1002) {
        PCWSTR comment = level == 1 ? ((LOCALGROUP_INFO_1 *)buffer)->Comment : *(LPWSTR *)buffer;
        if (!comment)
            comment = L"";
        RegSetValueExW(key, L"Comment", 0, REG_SZ, (const BYTE *)comment, (DWORD)((wcslen(comment) + 1) * sizeof(WCHAR)));
    } else if (_wcsicmp(*(LPWSTR *)buffer, group)) {
        /* a group is renamed by its key, which the lookups find by the name. */
        status = ERROR_ACCESS_DENIED;
    }
    RegCloseKey(key);
    return status;
}

/* the sid of a member the caller gave, by its sid or by its name. */
static NET_API_STATUS member_sid(DWORD level, const BYTE *entry, WCHAR **text)
{
    if (!adv_ready())
        return ERROR_PROC_NOT_FOUND;
    BYTE storage[68];
    const BYTE *sid = nullptr;
    if (level == 3) {
        LPWSTR name = *(LPWSTR *)entry;
        DWORD sid_size = sizeof(storage), domain_size = 256, use;
        WCHAR domain[256];
        if (!name || !g_adv.lookup_name(nullptr, name, storage, &sid_size, domain, &domain_size, &use))
            return ERROR_NONE_MAPPED_CODE;
        sid = storage;
    } else {
        sid = *(const BYTE **)entry;
        if (!sid)
            return ERROR_INVALID_PARAMETER;
    }
    return g_adv.to_string((PVOID)sid, text) ? NERR_Success : ERROR_INVALID_PARAMETER;
}

typedef enum MEMBER_CHANGE { MEMBERS_ADD, MEMBERS_DELETE, MEMBERS_SET } MEMBER_CHANGE;

static NET_API_STATUS change_members(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count, MEMBER_CHANGE change)
{
    if (!group || (!buffer && count))
        return ERROR_INVALID_PARAMETER;
    if (level > 3)
        return ERROR_INVALID_LEVEL;
    if (!local_server(server))
        return ERROR_BAD_NETPATH;
    if (AliasIsBuiltin(group))
        return ERROR_ACCESS_DENIED;
    HKEY key;
    if (!valid_name(group) || open_alias(group, stock_index(group) >= 0, KEY_ALL_ACCESS, &key))
        return ERROR_NO_SUCH_ALIAS_CODE;
    SIZE_T stride = level == 0 || level == 3 ? sizeof(PVOID) : level == 1 ? 3 * sizeof(PVOID) : 3 * sizeof(PVOID);
    PWSTR list = read_members(key);
    NET_API_STATUS status = list ? NERR_Success : ERROR_NOT_ENOUGH_MEMORY;
    /* the new list is built apart, so a change that fails halfway leaves the group as it was. */
    SIZE_T capacity = (list ? list_chars(list) : 0) + 4 + (SIZE_T)count * 80;
    PWSTR next = status ? nullptr : LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, capacity * sizeof(WCHAR));
    if (!status && !next)
        status = ERROR_NOT_ENOUGH_MEMORY;
    if (!status) {
        if (change != MEMBERS_SET)
            memcpy(next, list, (list_chars(list) + 1) * sizeof(WCHAR));
        for (DWORD i = 0; i < count && !status; i++) {
            WCHAR *text = nullptr;
            status = member_sid(level, buffer + i * stride, &text);
            if (status)
                break;
            BOOL present = has_member(next, text);
            if (change == MEMBERS_DELETE) {
                if (!present) {
                    status = ERROR_MEMBER_NOT_IN_ALIAS_CODE;
                } else {
                    PWSTR out = next;
                    for (PWSTR at = next; *at;) {
                        SIZE_T n = wcslen(at) + 1;
                        PWSTR following = at + n;
                        if (_wcsicmp(at, text)) {
                            memmove(out, at, n * sizeof(WCHAR));
                            out += n;
                        }
                        at = following;
                    }
                    *out = 0;
                    out[1] = 0;
                }
            } else if (present) {
                if (change == MEMBERS_ADD)
                    status = ERROR_MEMBER_IN_ALIAS_CODE;
            } else {
                SIZE_T used = list_chars(next);
                memcpy(next + used, text, (wcslen(text) + 1) * sizeof(WCHAR));
                next[used + wcslen(text) + 1] = 0;
            }
            LocalFree(text);
        }
        if (!status)
            write_members(key, next);
    }
    LocalFree(list);
    LocalFree(next);
    RegCloseKey(key);
    return status;
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupAddMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return change_members(server, group, level, buffer, count, MEMBERS_ADD);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDelMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    if (!buffer)
        return ERROR_INVALID_PARAMETER;
    return change_members(server, group, level, buffer, count, MEMBERS_DELETE);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupSetMembers(LMCSTR server, LMCSTR group, DWORD level, LPBYTE buffer, DWORD count)
{
    return change_members(server, group, level, buffer, count, MEMBERS_SET);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupAddMember(LMCSTR server, LMCSTR group, PVOID sid)
{
    if (!sid)
        return ERROR_INVALID_PARAMETER;
    return change_members(server, group, 0, (LPBYTE)&sid, 1, MEMBERS_ADD);
}

NETAPI NET_API_STATUS WINAPI NetLocalGroupDelMember(LMCSTR server, LMCSTR group, PVOID sid)
{
    if (!sid)
        return ERROR_INVALID_PARAMETER;
    return change_members(server, group, 0, (LPBYTE)&sid, 1, MEMBERS_DELETE);
}

/* the groups made here, by name; the count and the names are the caller's to free. */
DWORD AliasNames(PWSTR **names)
{
    *names = nullptr;
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ALIAS_KEY, 0, KEY_READ, &root))
        return 0;
    DWORD count = 0, longest;
    if (RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, &count, &longest, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) ||
        !count) {
        RegCloseKey(root);
        return 0;
    }
    PWSTR *list = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, count * sizeof(PWSTR));
    DWORD found = 0;
    for (DWORD i = 0; list && i < count; i++) {
        WCHAR name[260];
        DWORD n = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(root, i, name, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        if (stock_index(name) >= 0)
            continue;
        list[found] = LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(WCHAR));
        if (list[found])
            wcscpy(list[found++], name);
    }
    RegCloseKey(root);
    *names = list;
    return found;
}

void AliasFreeNames(PWSTR *names, DWORD count)
{
    for (DWORD i = 0; names && i < count; i++)
        LocalFree(names[i]);
    LocalFree(names);
}

PWSTR AliasComment(PCWSTR name)
{
    HKEY key;
    if (open_alias(name, FALSE, KEY_QUERY_VALUE, &key))
        return nullptr;
    PWSTR comment = read_text(key, L"Comment");
    RegCloseKey(key);
    return comment;
}

/* the sids of the members of a made group as binary sids in one block, or null when the group is not there. */
DWORD AliasMemberSids(PCWSTR name, BYTE ***sids)
{
    *sids = nullptr;
    HKEY key;
    BOOL stock = stock_index(name) >= 0;
    if (!valid_name(name) || !adv_ready())
        return (DWORD)-1;
    PWSTR list;
    if (open_alias(name, FALSE, KEY_QUERY_VALUE, &key)) {
        /* a stock group nobody was added to has no key yet. */
        if (!stock)
            return (DWORD)-1;
        list = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, 4 * sizeof(WCHAR));
    } else {
        list = read_members(key);
        RegCloseKey(key);
    }
    if (!list)
        return (DWORD)-1;
    DWORD count = 0;
    for (PCWSTR at = list; *at; at += wcslen(at) + 1)
        count++;
    BYTE **result = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (count ? count : 1) * sizeof(BYTE *));
    DWORD found = 0;
    for (PCWSTR at = list; result && *at; at += wcslen(at) + 1) {
        PVOID sid;
        if (g_adv.from_string(at, &sid))
            result[found++] = sid;
    }
    LocalFree(list);
    *sids = result;
    return found;
}

void AliasFreeSids(BYTE **sids, DWORD count)
{
    for (DWORD i = 0; sids && i < count; i++)
        LocalFree(sids[i]);
    LocalFree(sids);
}

/* the name of a member as domain and account, or its sid in text when no account has it. */
DWORD AliasMemberName(const BYTE *sid, PWSTR name, DWORD chars, DWORD *use)
{
    if (!adv_ready())
        return 0;
    WCHAR account[260], domain[260];
    DWORD name_size = RTL_NUMBER_OF(account), domain_size = RTL_NUMBER_OF(domain);
    if (g_adv.lookup_sid(nullptr, (PVOID)sid, account, &name_size, domain, &domain_size, use))
        return (DWORD)_snwprintf(name, chars, domain[0] ? L"%ls\\%ls" : L"%.0ls%ls", domain, account);
    WCHAR *text;
    *use = 8;
    if (!g_adv.to_string((PVOID)sid, &text))
        return 0;
    DWORD n = (DWORD)_snwprintf(name, chars, L"%ls", text);
    LocalFree(text);
    return n;
}
