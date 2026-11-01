/*
 * the audit policy of the machine. windows keeps it in lsass, which asks lsaadt.dll, and sechost.dll reaches it through
 * a local procedure call; the category and subcategory tables are fixed data of those dlls. here the functions run in
 * the caller, over a policy kept in the registry key of the lsa: the system policy and the per user policies of
 * accounts, the security descriptor that guards the audit functions and the global sacls of files and keys.
 *
 * a caller needs the security privilege, or the access the descriptor of the audit functions gives: by default
 * administrators and the system may query the policies, nobody may change them without the privilege. the tables, the
 * checks and the default descriptor were read from lsaadt.dll, build 26300.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/ntapi.h>

ADVAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL self, PHANDLE token);
ADVAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
ADVAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa, SECURITY_IMPERSONATION_LEVEL level,
                                    TOKEN_TYPE type, PHANDLE result);
ADVAPI BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, PGENERIC_MAPPING mapping,
                               PPRIVILEGE_SET privileges, LPDWORD privileges_length, LPDWORD granted, LPBOOL status);
ADVAPI BOOL WINAPI IsValidAcl(PACL acl);
ADVAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd);
ADVAPI BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *dacl, LPBOOL defaulted);
ADVAPI BOOL WINAPI GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *sacl, LPBOOL defaulted);
ADVAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                        PSECURITY_DESCRIPTOR *result, PULONG size);

#define SE_SECURITY_PRIVILEGE 8
#define SE_AUDIT_PRIVILEGE 21

#define AUDIT_SET_SYSTEM_POLICY 0x1
#define AUDIT_QUERY_SYSTEM_POLICY 0x2
#define AUDIT_SET_USER_POLICY 0x4
#define AUDIT_QUERY_USER_POLICY 0x8
#define AUDIT_ENUMERATE_USERS 0x10

#define CATEGORY_COUNT 9
#define SUBCATEGORY_COUNT 60

#define STATUS_PRIVILEGE_NOT_HELD_ ((int)0xC0000061)
#define STATUS_INVALID_PARAMETER_ ((int)0xC000000D)
#define STATUS_OBJECT_NAME_NOT_FOUND_ ((int)0xC0000034)
#define STATUS_INVALID_SID_ ((int)0xC0000078)
#define STATUS_NONE_MAPPED_ ((int)0xC0000073)
#define STATUS_PATH_SYNTAX_BAD_ ((int)0xC000003B)
#define STATUS_NOT_FOUND_AUDIT ((int)0xC0000225)
#define STATUS_ACCESS_DENIED_ ((int)0xC0000022)

typedef struct AUDIT_POLICY_INFORMATION_ {
    GUID AuditSubCategoryGuid;
    ULONG AuditingInformation;
    GUID AuditCategoryGuid;
} AUDIT_POLICY_INFORMATION_;

typedef struct AUDIT_SID_ARRAY_ {
    ULONG UsersCount;
    PSID *UserSidArray;
} AUDIT_SID_ARRAY_;

/* {69979848-797a-11d9-bed3-505054503030} is the first category, {0cce9210-69ae-11d9-bed3-505054503030} the first subcategory. */
static const GUID g_category_guid = {0x69979848, 0x797A, 0x11D9, {0xBE, 0xD3, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30}};
static const GUID g_subcategory_guid = {0x0CCE9210, 0x69AE, 0x11D9, {0xBE, 0xD3, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30}};

static const PCWSTR g_category_names[CATEGORY_COUNT] = {L"System",          L"Logon/Logoff",      L"Object Access",
                                                        L"Privilege Use",   L"Detailed Tracking", L"Policy Change",
                                                        L"Account Management", L"DS Access",      L"Account Logon"};

/* the subcategories in the order of their guids: the category, the number in the category and the default policy. */
typedef struct SUBCATEGORY {
    BYTE Category;
    BYTE Index;
    BYTE Default;
    PCWSTR Name;
} SUBCATEGORY;

static const SUBCATEGORY g_subcategories[SUBCATEGORY_COUNT] = {
    {0, 0, 1, L"Security State Change"},
    {0, 1, 0, L"Security System Extension"},
    {0, 2, 3, L"System Integrity"},
    {0, 3, 0, L"IPsec Driver"},
    {0, 4, 3, L"Other System Events"},
    {1, 0, 3, L"Logon"},
    {1, 1, 1, L"Logoff"},
    {1, 2, 1, L"Account Lockout"},
    {1, 3, 0, L"IPsec Main Mode"},
    {1, 5, 0, L"IPsec Quick Mode"},
    {1, 6, 0, L"IPsec Extended Mode"},
    {1, 4, 1, L"Special Logon"},
    {1, 7, 0, L"Other Logon/Logoff Events"},
    {2, 0, 0, L"File System"},
    {2, 1, 0, L"Registry"},
    {2, 2, 0, L"Kernel Object"},
    {2, 3, 0, L"SAM"},
    {2, 5, 0, L"Certification Services"},
    {2, 6, 0, L"Application Generated"},
    {2, 7, 0, L"Handle Manipulation"},
    {2, 8, 0, L"File Share"},
    {2, 9, 0, L"Filtering Platform Packet Drop"},
    {2, 10, 0, L"Filtering Platform Connection"},
    {2, 4, 0, L"Other Object Access Events"},
    {3, 0, 0, L"Sensitive Privilege Use"},
    {3, 1, 0, L"Non Sensitive Privilege Use"},
    {3, 2, 0, L"Other Privilege Use Events"},
    {4, 0, 0, L"Process Creation"},
    {4, 1, 0, L"Process Termination"},
    {4, 2, 0, L"DPAPI Activity"},
    {4, 3, 0, L"RPC Events"},
    {5, 0, 1, L"Audit Policy Change"},
    {5, 1, 1, L"Authentication Policy Change"},
    {5, 2, 0, L"Authorization Policy Change"},
    {5, 3, 0, L"MPSSVC Rule-Level Policy Change"},
    {5, 4, 0, L"Filtering Platform Policy Change"},
    {5, 5, 0, L"Other Policy Change Events"},
    {6, 0, 1, L"User Account Management"},
    {6, 1, 0, L"Computer Account Management"},
    {6, 2, 1, L"Security Group Management"},
    {6, 3, 0, L"Distribution Group Management"},
    {6, 4, 0, L"Application Group Management"},
    {6, 5, 0, L"Other Account Management Events"},
    {7, 0, 0, L"Directory Service Access"},
    {7, 1, 0, L"Directory Service Changes"},
    {7, 2, 0, L"Directory Service Replication"},
    {7, 3, 0, L"Detailed Directory Service Replication"},
    {8, 0, 0, L"Credential Validation"},
    {8, 1, 0, L"Kerberos Service Ticket Operations"},
    {8, 2, 0, L"Other Account Logon Events"},
    {8, 3, 0, L"Kerberos Authentication Service"},
    {1, 8, 3, L"Network Policy Server"},
    {2, 11, 0, L"Detailed File Share"},
    {2, 12, 0, L"Removable Storage"},
    {2, 13, 0, L"Central Policy Staging"},
    {1, 9, 0, L"User / Device Claims"},
    {4, 4, 0, L"Plug and Play Events"},
    {1, 10, 0, L"Group Membership"},
    {4, 5, 0, L"Token Right Adjusted Events"},
    {1, 11, 0, L"Access Rights"},
};

/* ------------------------------------------------------------------ guids and ids */

static BOOL guid_tail_is(const GUID *g, const GUID *base)
{
    return g->Data2 == base->Data2 && g->Data3 == base->Data3 && !memcmp(g->Data4, base->Data4, 8);
}

static BOOL category_index(const GUID *g, DWORD *index)
{
    if (!g || !guid_tail_is(g, &g_category_guid) || g->Data1 - g_category_guid.Data1 >= CATEGORY_COUNT)
        return FALSE;
    *index = g->Data1 - g_category_guid.Data1;
    return TRUE;
}

static BOOL subcategory_index(const GUID *g, DWORD *index)
{
    if (!g || !guid_tail_is(g, &g_subcategory_guid) || g->Data1 - g_subcategory_guid.Data1 >= SUBCATEGORY_COUNT)
        return FALSE;
    *index = g->Data1 - g_subcategory_guid.Data1;
    return TRUE;
}

static void category_guid(DWORD index, GUID *g)
{
    *g = g_category_guid;
    g->Data1 += index;
}

static void subcategory_guid(DWORD index, GUID *g)
{
    *g = g_subcategory_guid;
    g->Data1 += index;
}

/* ------------------------------------------------------------------ the caller's rights */

/* true when the privilege is enabled in the token of the calling thread, or of the process. */
static BOOL privilege_enabled(DWORD luid)
{
    HANDLE token = nullptr;
    BOOL own = FALSE;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        own = TRUE;
    TOKEN_PRIVILEGES *privileges = AdvQueryToken(own ? token : nullptr, TokenPrivileges);
    if (own)
        CloseHandle(token);
    BOOL held = FALSE;
    for (DWORD i = 0; privileges && i < privileges->PrivilegeCount; i++)
        if (privileges->Privileges[i].Luid.LowPart == luid && !privileges->Privileges[i].Luid.HighPart)
            held = (privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0;
    AdvFree(privileges);
    return held;
}

#define AUDIT_KEY L"SYSTEM\\CurrentControlSet\\Control\\Lsa\\Audit"

static BOOL read_value(PCWSTR subkey, PCWSTR name, BYTE **data, DWORD *size)
{
    HKEY key;
    *data = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_QUERY_VALUE, &key))
        return FALSE;
    DWORD type;
    *size = 0;
    LONG error = RegQueryValueExW(key, name, nullptr, &type, nullptr, size);
    if (!error && *size) {
        *data = AdvAlloc(*size);
        if (*data)
            error = RegQueryValueExW(key, name, nullptr, &type, *data, size);
        else
            error = ERROR_NOT_ENOUGH_MEMORY;
    } else if (!error) {
        error = ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    if (error) {
        AdvFree(*data);
        *data = nullptr;
    }
    return error == 0;
}

static DWORD write_value(PCWSTR subkey, PCWSTR name, const void *data, DWORD size)
{
    HKEY key;
    DWORD disposition;
    LONG error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (error)
        return error;
    error = data ? RegSetValueExW(key, name, 0, REG_BINARY, data, size) : RegDeleteValueW(key, name);
    RegCloseKey(key);
    return error == ERROR_FILE_NOT_FOUND && !data ? 0 : error;
}

/* true when the descriptor that guards the audit functions, the default or the one a caller set, grants the access. */
static BOOL api_access(DWORD mask)
{
    static const WCHAR default_text[] = L"O:SYG:SYD:(A;;0x2005A;;;SY)(A;;0x2005A;;;BA)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    BYTE *dacl;
    DWORD dacl_size;
    BYTE owner[SECURITY_MAX_SID_SIZE];
    SECURITY_DESCRIPTOR absolute;
    BOOL custom = read_value(AUDIT_KEY, L"AuditApiDacl", &dacl, &dacl_size) && IsValidAcl((PACL)dacl);
    if (custom) {
        AdvMakeSid(owner, 5, 1, 18);
        InitializeSecurityDescriptor(&absolute, 1);
        SetSecurityDescriptorOwner(&absolute, owner, FALSE);
        SetSecurityDescriptorGroup(&absolute, owner, FALSE);
        SetSecurityDescriptorDacl(&absolute, TRUE, (PACL)dacl, FALSE);
        descriptor = &absolute;
    } else if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(default_text, 1, &descriptor, nullptr)) {
        AdvFree(dacl);
        return FALSE;
    }
    HANDLE thread = nullptr, impersonation = nullptr;
    BOOL granted_ok = FALSE;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_QUERY, TRUE, &thread))
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &thread);
    if (thread && DuplicateTokenEx(thread, TOKEN_QUERY, nullptr, 2, TokenImpersonation, &impersonation)) {
        GENERIC_MAPPING mapping = {0x20000 | 0x2 | 0x8 | 0x40, 0x20000 | 0x1 | 0x4, 0x20000, 0x7F | 0xF0000};
        PRIVILEGE_SET privileges;
        DWORD privileges_length = sizeof(privileges), granted;
        BOOL status;
        if (AccessCheck(descriptor, impersonation, mask, &mapping, &privileges, &privileges_length, &granted, &status))
            granted_ok = status;
    }
    if (impersonation)
        CloseHandle(impersonation);
    if (thread)
        CloseHandle(thread);
    if (!custom)
        LocalFree(descriptor);
    AdvFree(dacl);
    return granted_ok;
}

/*
 * the rules of lsaadt.dll for a caller: the security privilege lets anything through; without it a global sacl needs
 * it all the same, and the rest needs the access in the audit descriptor or, where allowed, the audit privilege.
 */
static int check_caller(BOOL global, DWORD mask, BOOL audit_privilege)
{
    if (!global && !mask && !audit_privilege)
        return 0;
    if (privilege_enabled(SE_SECURITY_PRIVILEGE))
        return 0;
    if (global)
        return STATUS_PRIVILEGE_NOT_HELD_;
    if (mask && api_access(mask))
        return 0;
    if (audit_privilege && privilege_enabled(SE_AUDIT_PRIVILEGE))
        return 0;
    return STATUS_PRIVILEGE_NOT_HELD_;
}

static BOOL fail(int status)
{
    SetLastError(RtlNtStatusToDosError(status));
    return FALSE;
}

/* ------------------------------------------------------------------ categories and names */

ADVAPI BOOLEAN WINAPI AuditEnumerateCategories(GUID **categories, PULONG count)
{
    if (!categories || !count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GUID *list = LocalAlloc(LMEM_ZEROINIT, CATEGORY_COUNT * sizeof(GUID));
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (DWORD i = 0; i < CATEGORY_COUNT; i++)
        category_guid(i, &list[i]);
    *categories = list;
    *count = CATEGORY_COUNT;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditEnumerateSubCategories(const GUID *category, BOOLEAN all, GUID **subcategories, PULONG count)
{
    DWORD wanted = 0;
    if (!subcategories || !count || (!all && !category_index(category, &wanted))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD found = 0;
    for (DWORD i = 0; i < SUBCATEGORY_COUNT; i++)
        if (all || g_subcategories[i].Category == wanted)
            found++;
    GUID *list = LocalAlloc(LMEM_ZEROINIT, found * sizeof(GUID) + 1);
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    found = 0;
    for (DWORD i = 0; i < SUBCATEGORY_COUNT; i++)
        if (all || g_subcategories[i].Category == wanted)
            subcategory_guid(i, &list[found++]);
    *subcategories = list;
    *count = found;
    return TRUE;
}

static BOOLEAN name_copy(PCWSTR name, PWSTR *out)
{
    SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
    PWSTR copy = LocalAlloc(LMEM_ZEROINIT, bytes);
    if (!copy) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, name, bytes);
    *out = copy;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditLookupCategoryNameW(const GUID *category, PWSTR *name)
{
    DWORD index;
    if (!name || !category_index(category, &index))
        return fail(STATUS_INVALID_PARAMETER_);
    return name_copy(g_category_names[index], name);
}

ADVAPI BOOLEAN WINAPI AuditLookupSubCategoryNameW(const GUID *subcategory, PWSTR *name)
{
    DWORD index;
    if (!name || !subcategory_index(subcategory, &index))
        return fail(STATUS_INVALID_PARAMETER_);
    return name_copy(g_subcategories[index].Name, name);
}

static BOOLEAN name_ansi(BOOLEAN(WINAPI *lookup)(const GUID *, PWSTR *), const GUID *guid, PSTR *out)
{
    PWSTR wide = nullptr;
    if (!lookup(guid, &wide))
        return FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    PSTR ansi = LocalAlloc(LMEM_ZEROINIT, bytes);
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, bytes, nullptr, nullptr);
    LocalFree(wide);
    if (!ansi) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *out = ansi;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditLookupCategoryNameA(const GUID *category, PSTR *name)
{
    return name_ansi(AuditLookupCategoryNameW, category, name);
}

ADVAPI BOOLEAN WINAPI AuditLookupSubCategoryNameA(const GUID *subcategory, PSTR *name)
{
    return name_ansi(AuditLookupSubCategoryNameW, subcategory, name);
}

ADVAPI BOOLEAN WINAPI AuditLookupCategoryGuidFromCategoryId(DWORD id, GUID *guid)
{
    if (!guid || id >= CATEGORY_COUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    category_guid(id, guid);
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditLookupCategoryIdFromCategoryGuid(const GUID *guid, DWORD *id)
{
    DWORD index;
    if (!id || !category_index(guid, &index)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *id = index;
    return TRUE;
}

/* ------------------------------------------------------------------ the policies */

static void default_policy(WORD words[SUBCATEGORY_COUNT])
{
    for (DWORD i = 0; i < SUBCATEGORY_COUNT; i++)
        words[i] = g_subcategories[i].Default;
}

/* the system policy: the defaults of a client, over which what was set is kept. */
static void load_system_policy(WORD words[SUBCATEGORY_COUNT])
{
    default_policy(words);
    BYTE *data;
    DWORD size;
    if (read_value(AUDIT_KEY, L"SystemPolicy", &data, &size)) {
        if (size == SUBCATEGORY_COUNT * sizeof(WORD))
            memcpy(words, data, size);
        AdvFree(data);
    }
}

static PWSTR sid_text(PSID sid)
{
    PWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text))
        return nullptr;
    return text;
}

#define USER_KEY AUDIT_KEY L"\\PerUser"
#define USER_BYTES (SUBCATEGORY_COUNT / 2)

static BOOL load_user_policy(PSID sid, BYTE nibbles[USER_BYTES])
{
    PWSTR text = sid_text(sid);
    if (!text)
        return FALSE;
    BYTE *data;
    DWORD size;
    BOOL found = read_value(USER_KEY, text, &data, &size);
    if (found && size == USER_BYTES)
        memcpy(nibbles, data, size);
    else
        found = FALSE;
    AdvFree(data);
    LocalFree(text);
    return found;
}

static DWORD user_nibble(const BYTE nibbles[USER_BYTES], DWORD index)
{
    return (nibbles[index >> 1] >> (4 * (index & 1))) & 0xF;
}

/*
 * the sid of a per user policy must name a user or a computer, or one of a few well known accounts. returns a status,
 * STATUS_NONE_MAPPED when the account does not exist, which only the deletion of a stored policy may go on with.
 */
static int validate_policy_sid(PSID sid, BOOL *is_system)
{
    *is_system = FALSE;
    if (!sid)
        return 0;
    if (!AdvSidValid(sid))
        return STATUS_INVALID_SID_;
    WCHAR name[260], domain[260];
    SID_NAME_USE use;
    BYTE system[SECURITY_MAX_SID_SIZE];
    AdvMakeSid(system, 5, 1, 18);
    if (AdvSidEqual(sid, (const SID *)system))
        return STATUS_PATH_SYNTAX_BAD_;
    if (!AdvLookupSid(sid, name, RTL_NUMBER_OF(name), domain, RTL_NUMBER_OF(domain), &use))
        return STATUS_NONE_MAPPED_;
    if (use == SidTypeUser || use == SidTypeComputer)
        return 0;
    return STATUS_PATH_SYNTAX_BAD_;
}

static AUDIT_POLICY_INFORMATION_ *new_result(const GUID *guids, ULONG count)
{
    AUDIT_POLICY_INFORMATION_ *result = LocalAlloc(LMEM_ZEROINIT, count * sizeof(*result));
    for (ULONG i = 0; result && i < count; i++) {
        DWORD index;
        result[i].AuditSubCategoryGuid = guids[i];
        subcategory_index(&guids[i], &index);
        category_guid(g_subcategories[index].Category, &result[i].AuditCategoryGuid);
    }
    return result;
}

static int guids_valid(const GUID *guids, ULONG count)
{
    if (!guids || !count)
        return STATUS_INVALID_PARAMETER_;
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        if (!subcategory_index(&guids[i], &index))
            return STATUS_INVALID_PARAMETER_;
    }
    return 0;
}

ADVAPI BOOLEAN WINAPI AuditQuerySystemPolicy(const GUID *guids, ULONG count, AUDIT_POLICY_INFORMATION_ **policy)
{
    if (!policy) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *policy = nullptr;
    int status = check_caller(FALSE, AUDIT_QUERY_SYSTEM_POLICY, TRUE);
    if (status >= 0)
        status = guids_valid(guids, count);
    if (status < 0)
        return fail(status);
    WORD words[SUBCATEGORY_COUNT];
    load_system_policy(words);
    AUDIT_POLICY_INFORMATION_ *result = new_result(guids, count);
    if (!result) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        subcategory_index(&guids[i], &index);
        result[i].AuditingInformation = words[index];
    }
    *policy = result;
    return TRUE;
}

/* the policies given by the caller are merged into the system policy: a subcategory that is not named keeps its policy. */
ADVAPI BOOLEAN WINAPI AuditSetSystemPolicy(const AUDIT_POLICY_INFORMATION_ *policy, ULONG count)
{
    int status = check_caller(FALSE, AUDIT_SET_SYSTEM_POLICY, FALSE);
    if (status >= 0 && (!policy || !count))
        status = STATUS_INVALID_PARAMETER_;
    for (ULONG i = 0; status >= 0 && i < count; i++) {
        DWORD index;
        if (!subcategory_index(&policy[i].AuditSubCategoryGuid, &index) || (policy[i].AuditingInformation & ~7u))
            status = STATUS_INVALID_PARAMETER_;
    }
    if (status < 0)
        return fail(status);
    WORD words[SUBCATEGORY_COUNT] = {};
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        subcategory_index(&policy[i].AuditSubCategoryGuid, &index);
        words[index] |= policy[i].AuditingInformation & 7;
    }
    WORD merged[SUBCATEGORY_COUNT];
    load_system_policy(merged);
    for (DWORD i = 0; i < SUBCATEGORY_COUNT; i++)
        if (words[i])
            merged[i] = words[i];
    DWORD error = write_value(AUDIT_KEY, L"SystemPolicy", merged, sizeof(merged));
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditQueryPerUserPolicy(PSID sid, const GUID *guids, ULONG count, AUDIT_POLICY_INFORMATION_ **policy)
{
    if (!sid || !policy) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *policy = nullptr;
    BOOL system;
    int status = validate_policy_sid(sid, &system);
    if (status == STATUS_NONE_MAPPED_)
        status = 0;
    if (status >= 0)
        status = check_caller(FALSE, AUDIT_QUERY_USER_POLICY, TRUE);
    if (status >= 0)
        status = guids_valid(guids, count);
    BYTE nibbles[USER_BYTES];
    if (status >= 0 && !load_user_policy(sid, nibbles))
        status = STATUS_OBJECT_NAME_NOT_FOUND_;
    if (status < 0)
        return fail(status);
    AUDIT_POLICY_INFORMATION_ *result = new_result(guids, count);
    if (!result) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        subcategory_index(&guids[i], &index);
        result[i].AuditingInformation = user_nibble(nibbles, index);
    }
    *policy = result;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditSetPerUserPolicy(PSID sid, const AUDIT_POLICY_INFORMATION_ *policy, ULONG count)
{
    if (!sid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE nibbles[USER_BYTES] = {};
    BOOL stored = load_user_policy(sid, nibbles);
    BOOL system;
    int status = validate_policy_sid(sid, &system);
    if (status == STATUS_NONE_MAPPED_ && stored)
        status = 0;
    if (status >= 0)
        status = check_caller(FALSE, AUDIT_SET_USER_POLICY, FALSE);
    if (status >= 0 && (!policy || !count))
        status = STATUS_INVALID_PARAMETER_;
    for (ULONG i = 0; status >= 0 && i < count; i++) {
        DWORD index;
        if (!subcategory_index(&policy[i].AuditSubCategoryGuid, &index) || (policy[i].AuditingInformation & ~31u))
            status = STATUS_INVALID_PARAMETER_;
    }
    if (status < 0)
        return fail(status);
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        subcategory_index(&policy[i].AuditSubCategoryGuid, &index);
        BYTE shift = 4 * (index & 1);
        BYTE value = (policy[i].AuditingInformation & 0x10) ? 0 : (BYTE)((nibbles[index >> 1] >> shift) & 0xF);
        value |= policy[i].AuditingInformation & 0xF;
        nibbles[index >> 1] = (nibbles[index >> 1] & ~(0xF << shift)) | (value << shift);
    }
    BOOL any = FALSE;
    for (DWORD i = 0; i < USER_BYTES; i++)
        any |= nibbles[i] != 0;
    PWSTR text = sid_text(sid);
    if (!text) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    DWORD error = write_value(USER_KEY, text, any ? nibbles : nullptr, USER_BYTES);
    LocalFree(text);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditEnumeratePerUserPolicy(AUDIT_SID_ARRAY_ **users)
{
    int status = check_caller(FALSE, AUDIT_ENUMERATE_USERS, FALSE);
    if (status >= 0 && !users)
        status = STATUS_INVALID_PARAMETER_;
    if (status < 0)
        return fail(status);
    *users = nullptr;
    PSID *sids = nullptr;
    DWORD count = 0, capacity = 0, bytes = 0;
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_QUERY_VALUE, &key)) {
        WCHAR name[96];
        for (DWORD i = 0;; i++) {
            DWORD chars = RTL_NUMBER_OF(name);
            if (RegEnumValueW(key, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
                break;
            PSID sid = nullptr;
            if (!ConvertStringSidToSidW(name, &sid))
                continue;
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 8;
                PSID *grown = AdvAlloc(capacity * sizeof(PSID));
                if (grown) {
                    if (sids)
                        memcpy(grown, sids, count * sizeof(PSID));
                    AdvFree(sids);
                    sids = grown;
                }
            }
            if (!sids) {
                LocalFree(sid);
                break;
            }
            sids[count++] = sid;
            bytes += (AdvSidLength(sid) + 7) & ~7u;
        }
        RegCloseKey(key);
    }
    SIZE_T total = (sizeof(AUDIT_SID_ARRAY_) + 7) & ~7ull;
    total += count * sizeof(PSID) + bytes;
    BYTE *memory = LocalAlloc(LMEM_ZEROINIT, total);
    if (memory) {
        AUDIT_SID_ARRAY_ *array = (AUDIT_SID_ARRAY_ *)memory;
        BYTE *at = memory + ((sizeof(AUDIT_SID_ARRAY_) + 7) & ~7ull);
        array->UsersCount = count;
        array->UserSidArray = count ? (PSID *)at : nullptr;
        at += count * sizeof(PSID);
        for (DWORD i = 0; i < count; i++) {
            DWORD length = AdvSidLength(sids[i]);
            memcpy(at, sids[i], length);
            array->UserSidArray[i] = at;
            at += (length + 7) & ~7u;
        }
        *users = array;
    }
    for (DWORD i = 0; i < count; i++)
        LocalFree(sids[i]);
    AdvFree(sids);
    if (!memory) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

/* the system policy with a user's include and exclude marks applied: success and failure each switched on or off. */
ADVAPI BOOLEAN WINAPI AuditComputeEffectivePolicyBySid(PSID sid, const GUID *guids, ULONG count, AUDIT_POLICY_INFORMATION_ **policy)
{
    if (!sid || !policy) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *policy = nullptr;
    BOOL system;
    int status = check_caller(FALSE, AUDIT_QUERY_SYSTEM_POLICY, TRUE);
    if (status >= 0)
        status = guids_valid(guids, count);
    if (status >= 0) {
        status = validate_policy_sid(sid, &system);
        if (status == STATUS_NONE_MAPPED_)
            status = 0;
    }
    if (status >= 0)
        status = check_caller(FALSE, AUDIT_QUERY_USER_POLICY, TRUE);
    if (status < 0)
        return fail(status);
    WORD words[SUBCATEGORY_COUNT];
    load_system_policy(words);
    BYTE nibbles[USER_BYTES];
    BOOL has_user = load_user_policy(sid, nibbles);
    AUDIT_POLICY_INFORMATION_ *result = new_result(guids, count);
    if (!result) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (ULONG i = 0; i < count; i++) {
        DWORD index;
        subcategory_index(&guids[i], &index);
        ULONG value = words[index];
        DWORD n = has_user ? user_nibble(nibbles, index) : 0;
        if (n & 1)
            value |= 1;
        else if (n & 2)
            value &= ~1u;
        if (n & 4)
            value |= 2;
        else if (n & 8)
            value &= ~2u;
        result[i].AuditingInformation = value;
    }
    *policy = result;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditComputeEffectivePolicyByToken(HANDLE token, const GUID *guids, ULONG count,
                                                          AUDIT_POLICY_INFORMATION_ **policy)
{
    TOKEN_USER *user = AdvQueryTokenUser(token);
    if (!user) {
        SetLastError(GetLastError() ? GetLastError() : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOLEAN ok = AuditComputeEffectivePolicyBySid(user->User.Sid, guids, count, policy);
    DWORD error = GetLastError();
    AdvFree(user);
    if (!ok)
        SetLastError(error);
    return ok;
}

/* ------------------------------------------------------------------ the descriptor and the global sacls */

ADVAPI BOOLEAN WINAPI AuditQuerySecurity(DWORD information, PSECURITY_DESCRIPTOR *descriptor)
{
    int status = check_caller(TRUE, 0, FALSE);
    if (status >= 0 && (!descriptor || !(information & 0xC)))
        status = STATUS_INVALID_PARAMETER_;
    if (status < 0) {
        if (descriptor)
            *descriptor = nullptr;
        return fail(status);
    }
    *descriptor = nullptr;
    BYTE *dacl = nullptr, *sacl = nullptr;
    DWORD dacl_size = 0, sacl_size = 0;
    read_value(AUDIT_KEY, L"AuditApiDacl", &dacl, &dacl_size);
    read_value(AUDIT_KEY, L"AuditApiSacl", &sacl, &sacl_size);
    PSECURITY_DESCRIPTOR built = nullptr;
    BOOL ok;
    if (dacl || sacl || (information & 4)) {
        SECURITY_DESCRIPTOR absolute;
        InitializeSecurityDescriptor(&absolute, 1);
        BYTE default_dacl[128];
        PACL use_dacl = (PACL)dacl;
        if ((information & 4) && !dacl) {
            BYTE system[SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
            AdvMakeSid(system, 5, 1, 18);
            AdvMakeSid(admins, 5, 2, 32, 544);
            use_dacl = (PACL)default_dacl;
            RtlCreateAcl(use_dacl, sizeof(default_dacl), 2);
            RtlAddAccessAllowedAce(use_dacl, 2, 0x2005A, system);
            RtlAddAccessAllowedAce(use_dacl, 2, 0x2005A, admins);
        }
        if (information & 4)
            SetSecurityDescriptorDacl(&absolute, TRUE, use_dacl, FALSE);
        if ((information & 8) && sacl)
            SetSecurityDescriptorSacl(&absolute, TRUE, (PACL)sacl, FALSE);
        DWORD length = 0;
        MakeSelfRelativeSD(&absolute, nullptr, &length);
        built = LocalAlloc(LMEM_ZEROINIT, length);
        ok = built && MakeSelfRelativeSD(&absolute, built, &length);
    } else {
        SECURITY_DESCRIPTOR absolute;
        InitializeSecurityDescriptor(&absolute, 1);
        DWORD length = 0;
        MakeSelfRelativeSD(&absolute, nullptr, &length);
        built = LocalAlloc(LMEM_ZEROINIT, length);
        ok = built && MakeSelfRelativeSD(&absolute, built, &length);
    }
    AdvFree(dacl);
    AdvFree(sacl);
    if (!ok) {
        if (built)
            LocalFree(built);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *descriptor = built;
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditSetSecurity(DWORD information, PSECURITY_DESCRIPTOR descriptor)
{
    int status = check_caller(TRUE, 0, FALSE);
    if (status >= 0 && (!descriptor || !(information & 0xC) || !IsValidSecurityDescriptor(descriptor)))
        status = STATUS_INVALID_PARAMETER_;
    BOOL present = FALSE, defaulted;
    PACL dacl = nullptr, sacl = nullptr;
    if (status >= 0 && (information & 4)) {
        if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present || !dacl)
            status = STATUS_INVALID_PARAMETER_;
    }
    if (status >= 0 && (information & 8)) {
        BOOL sacl_present = FALSE;
        GetSecurityDescriptorSacl(descriptor, &sacl_present, &sacl, &defaulted);
        if (!sacl_present)
            sacl = nullptr;
    }
    if (status < 0)
        return fail(status);
    DWORD error = 0;
    if (information & 4)
        error = write_value(AUDIT_KEY, L"AuditApiDacl", dacl, dacl->AclSize);
    if (!error && (information & 8))
        error = write_value(AUDIT_KEY, L"AuditApiSacl", sacl, sacl ? sacl->AclSize : 0);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

/* a global sacl is kept for the object types the system names: files and registry keys. */
static PCWSTR sacl_value(PCWSTR type)
{
    if (!_wcsicmp(type, L"File"))
        return L"GlobalSaclFile";
    if (!_wcsicmp(type, L"Key"))
        return L"GlobalSaclKey";
    return nullptr;
}

ADVAPI BOOLEAN WINAPI AuditQueryGlobalSaclW(PCWSTR type, PACL *acl)
{
    if (!type || !acl) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *acl = nullptr;
    int status = check_caller(TRUE, 0, FALSE);
    PCWSTR value = *type ? sacl_value(type) : nullptr;
    if (status >= 0 && !value)
        status = STATUS_INVALID_PARAMETER_;
    BYTE *data = nullptr;
    DWORD size = 0;
    if (status >= 0 && !read_value(AUDIT_KEY, value, &data, &size))
        status = STATUS_OBJECT_NAME_NOT_FOUND_;
    if (status < 0)
        return fail(status);
    *acl = LocalAlloc(LMEM_ZEROINIT, size);
    if (*acl)
        memcpy(*acl, data, size);
    AdvFree(data);
    if (!*acl) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditSetGlobalSaclW(PCWSTR type, PACL acl)
{
    if (!type) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (acl && !IsValidAcl(acl)) {
        SetLastError(ERROR_INVALID_ACL);
        return FALSE;
    }
    int status = check_caller(TRUE, 0, FALSE);
    PCWSTR value = *type ? sacl_value(type) : nullptr;
    if (status >= 0 && !value)
        status = STATUS_INVALID_PARAMETER_;
    if (status < 0)
        return fail(status);
    DWORD error = write_value(AUDIT_KEY, value, acl, acl ? acl->AclSize : 0);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}

ADVAPI BOOLEAN WINAPI AuditQueryGlobalSaclA(PCSTR type, PACL *acl)
{
    PWSTR wide = type ? AdvAnsiToWide(type) : nullptr;
    if (!wide) {
        SetLastError(type ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOLEAN ok = AuditQueryGlobalSaclW(wide, acl);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}

ADVAPI BOOLEAN WINAPI AuditSetGlobalSaclA(PCSTR type, PACL acl)
{
    PWSTR wide = type ? AdvAnsiToWide(type) : nullptr;
    if (!wide) {
        SetLastError(type ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOLEAN ok = AuditSetGlobalSaclW(wide, acl);
    DWORD error = GetLastError();
    AdvFree(wide);
    SetLastError(error);
    return ok;
}
