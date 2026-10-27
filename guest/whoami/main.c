/*
 * whoami.exe: tells who is running it, from the token of the process.
 *
 *   whoami [/UPN | /FQDN | /LOGONID]
 *   whoami { [/USER] [/GROUPS] [/CLAIMS] [/PRIV] } [/FO format] [/NH]
 *   whoami /ALL [/FO format] [/NH]
 *
 * the plain form prints domain\name in lower case. the exit code is 0, or 1 for a wrong command line or a failure.
 */
#include "tool.h"

#define TOKEN_QUERY_ 0x0008
#define TokenUser_ 1
#define TokenGroups_ 2
#define TokenPrivileges_ 3

#define SE_GROUP_MANDATORY_ 0x1
#define SE_GROUP_ENABLED_BY_DEFAULT_ 0x2
#define SE_GROUP_ENABLED_ 0x4
#define SE_GROUP_OWNER_ 0x8
#define SE_GROUP_USE_FOR_DENY_ONLY_ 0x10
#define SE_GROUP_LOGON_ID_ 0xC0000000

#define SE_PRIVILEGE_ENABLED_ 0x2

typedef struct SID_ATTR {
    PSID Sid;
    DWORD Attributes;
} SID_ATTR;

typedef struct GROUPS_ {
    DWORD Count;
    SID_ATTR Groups[1];
} GROUPS_;

typedef struct LUID_ATTR {
    DWORD Low;
    LONG High;
    DWORD Attributes;
} LUID_ATTR;

typedef struct PRIVILEGES_ {
    DWORD Count;
    LUID_ATTR Privileges[1];
} PRIVILEGES_;

WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, int klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI LookupAccountSidW(LPCWSTR system, PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                         LPDWORD domain_size, int *use);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI LookupPrivilegeNameW(LPCWSTR system, void *luid, LPWSTR name, LPDWORD size);
WINBASEAPI BOOL WINAPI LookupPrivilegeDisplayNameW(LPCWSTR system, LPCWSTR name, LPWSTR display, LPDWORD size,
                                                   LPDWORD language);

static const WCHAR kUsage[] =
    L"\nWhoAmI has three ways of working: \n\nSyntax 1:\n"
    L"    WHOAMI [/UPN | /FQDN | /LOGONID]\n\n"
    L"Syntax 2:\n"
    L"    WHOAMI { [/USER] [/GROUPS] [/CLAIMS] [/PRIV] } [/FO format] [/NH]\n\n"
    L"Syntax 3:\n"
    L"    WHOAMI /ALL [/FO format] [/NH]\n\n"
    L"Description:\n"
    L"    This utility can be used to get user name and group information\n"
    L"    along with the respective security identifiers (SID), claims,\n"
    L"    privileges, logon identifier (logon ID) for the current user\n"
    L"    on the local system. I.e. who is the current logged on user?\n"
    L"    If no switch is specified, tool displays the user name in NTLM\n"
    L"    format (domain\\username).\n\n"
    L"Parameter List:\n"
    L"    /UPN                    Displays the user name in User Principal \n"
    L"                            Name (UPN) format.\n\n"
    L"    /FQDN                   Displays the user name in Fully Qualified \n"
    L"                            Distinguished Name (FQDN) format.\n\n"
    L"    /USER                   Displays information on the current user\n"
    L"                            along with the security identifier (SID).\n\n"
    L"    /GROUPS                 Displays group membership for current user,\n"
    L"                            type of account, security identifiers (SID)\n"
    L"                            and attributes.\n\n"
    L"    /CLAIMS                 Displays claims for current user,\n"
    L"                            including claim name, flags, type and values.\n\n"
    L"    /PRIV                   Displays security privileges of the current\n"
    L"                            user.\n\n"
    L"    /LOGONID                Displays the logon ID of the current user.\n\n"
    L"    /ALL                    Displays the current user name, groups \n"
    L"                            belonged to along with the security \n"
    L"                            identifiers (SID), claims and privileges for \n"
    L"                            the current user access token.\n\n"
    L"    /FO       format        Specifies the output format to be displayed.\n"
    L"                            Valid values are TABLE, LIST, CSV.\n"
    L"                            Column headings are not displayed with CSV\n"
    L"                            format. Default format is TABLE.\n\n"
    L"    /NH                     Specifies that the column header should not\n"
    L"                            be displayed in the output. This is\n"
    L"                            valid only for TABLE and CSV formats.\n\n"
    L"    /?                      Displays this help message.\n\n"
    L"Examples:\n"
    L"    WHOAMI\n"
    L"    WHOAMI /UPN\n"
    L"    WHOAMI /FQDN \n"
    L"    WHOAMI /LOGONID\n"
    L"    WHOAMI /USER\n"
    L"    WHOAMI /USER /FO LIST\n"
    L"    WHOAMI /USER /FO CSV\n"
    L"    WHOAMI /GROUPS\n"
    L"    WHOAMI /GROUPS /FO CSV /NH\n"
    L"    WHOAMI /CLAIMS\n"
    L"    WHOAMI /CLAIMS /FO LIST\n"
    L"    WHOAMI /PRIV\n"
    L"    WHOAMI /PRIV /FO TABLE\n"
    L"    WHOAMI /USER /GROUPS\n"
    L"    WHOAMI /USER /GROUPS /CLAIMS /PRIV\n"
    L"    WHOAMI /ALL\n"
    L"    WHOAMI /ALL /FO LIST\n"
    L"    WHOAMI /ALL /FO CSV /NH\n"
    L"    WHOAMI /?\n";

static HANDLE g_token;

static void *token_info(int klass)
{
    DWORD size = 0;
    GetTokenInformation(g_token, klass, nullptr, 0, &size);
    if (!size)
        return nullptr;
    BYTE *info = tu_alloc(size);
    if (!GetTokenInformation(g_token, klass, info, size, &size)) {
        tu_free(info);
        return nullptr;
    }
    return info;
}

/* domain\name of an account, or the text form of the sid when the system does not know it. */
static WCHAR *account_name(PSID sid, int *use, BOOL lower)
{
    WCHAR name[256], domain[256];
    DWORD name_size = TU_ARRAY(name), domain_size = TU_ARRAY(domain);
    TU_STR text = {};
    int kind = 8;
    if (LookupAccountSidW(nullptr, sid, name, &name_size, domain, &domain_size, &kind) && name[0]) {
        if (domain[0])
            tu_str_format(&text, L"%s\\%s", domain, name);
        else
            tu_str_puts(&text, name);
    } else {
        WCHAR *string = nullptr;
        if (ConvertSidToStringSidW(sid, &string)) {
            tu_str_puts(&text, string);
            LocalFree(string);
        }
        kind = 8;
    }
    if (use)
        *use = kind;
    if (lower)
        for (SIZE_T i = 0; i < text.length; i++)
            text.data[i] = tu_lower(text.data[i]);
    WCHAR *result = tu_dup(tu_str_text(&text));
    tu_str_free(&text);
    return result;
}

static WCHAR *sid_text(PSID sid)
{
    WCHAR *string = nullptr, *copy;
    if (!ConvertSidToStringSidW(sid, &string))
        return tu_dup(L"");
    copy = tu_dup(string);
    LocalFree(string);
    return copy;
}

static int fail_syntax(void)
{
    tu_eprint(L"ERROR: Invalid syntax. \nType \"WHOAMI /?\" for usage.\n");
    return 1;
}

static void title(const WCHAR *text, int format)
{
    if (format == TU_FMT_CSV)
        return;
    tu_print(L"\n%s\n", text);
    for (const WCHAR *p = text; *p; p++)
        tu_puts(L"-");
    tu_puts(L"\n");
    if (format == TU_FMT_TABLE)
        tu_puts(L"\n");
}

static int show_user(int format, BOOL no_header)
{
    SID_ATTR *user = token_info(TokenUser_);
    if (!user) {
        tu_eprint(L"ERROR: Unexpected error occurred.\n");
        return 1;
    }
    static const TU_COLUMN columns[] = {{L"User Name", 0, FALSE}, {L"SID", 0, FALSE}};
    TU_TABLE table;
    tu_table_init(&table, columns, 2);
    WCHAR *name = account_name(user->Sid, nullptr, TRUE), *sid = sid_text(user->Sid);
    const WCHAR *row[2] = {name, sid};
    tu_table_row(&table, row);
    title(L"USER INFORMATION", format);
    /* the title carries its own rule, a table is printed after it. */
    tu_table_print(&table, format, no_header);
    tu_table_free(&table);
    tu_free(name);
    tu_free(sid);
    tu_free(user);
    return 0;
}

static const WCHAR *type_text(int use, DWORD attributes)
{
    if ((attributes & SE_GROUP_LOGON_ID_) == SE_GROUP_LOGON_ID_)
        return L"Logon ID";
    switch (use) {
    case 1: return L"User";
    case 2: return L"Group";
    case 3: return L"Domain";
    case 4: return L"Alias";
    case 5: return L"Well-known group";
    case 6: return L"Deleted account";
    case 7: return L"Invalid SID";
    case 9: return L"Computer";
    case 10: return L"Label";
    case 11: return L"Logon ID";
    default: return L"Unknown SID type";
    }
}

static void attribute_text(DWORD a, TU_STR *out)
{
    static const struct {
        DWORD Bit;
        const WCHAR *Text;
    } names[] = {{SE_GROUP_MANDATORY_, L"Mandatory group"},
                 {SE_GROUP_ENABLED_BY_DEFAULT_, L"Enabled by default"},
                 {SE_GROUP_ENABLED_, L"Enabled group"},
                 {SE_GROUP_OWNER_, L"Group owner"},
                 {SE_GROUP_USE_FOR_DENY_ONLY_, L"Group used for deny only"}};
    for (int i = 0; i < (int)TU_ARRAY(names); i++) {
        if (!(a & names[i].Bit))
            continue;
        if (out->length)
            tu_str_puts(out, L", ");
        tu_str_puts(out, names[i].Text);
    }
}

static int show_groups(int format, BOOL no_header)
{
    GROUPS_ *groups = token_info(TokenGroups_);
    if (!groups) {
        tu_eprint(L"ERROR: Unable to get group membership information.\n");
        return 1;
    }
    static const TU_COLUMN columns[] = {{L"Group Name", 0, FALSE}, {L"Type", 0, FALSE}, {L"SID", 0, FALSE},
                                        {L"Attributes", 0, FALSE}};
    TU_TABLE table;
    tu_table_init(&table, columns, 4);
    for (DWORD i = 0; i < groups->Count; i++) {
        int use;
        WCHAR *name = account_name(groups->Groups[i].Sid, &use, FALSE), *sid = sid_text(groups->Groups[i].Sid);
        TU_STR attributes = {};
        attribute_text(groups->Groups[i].Attributes, &attributes);
        const WCHAR *row[4] = {name, type_text(use, groups->Groups[i].Attributes), sid, tu_str_text(&attributes)};
        tu_table_row(&table, row);
        tu_free(name);
        tu_free(sid);
        tu_str_free(&attributes);
    }
    title(L"GROUP INFORMATION", format);
    tu_table_print(&table, format, no_header);
    tu_table_free(&table);
    tu_free(groups);
    return 0;
}

static int show_privileges(int format, BOOL no_header)
{
    PRIVILEGES_ *privileges = token_info(TokenPrivileges_);
    if (!privileges) {
        tu_eprint(L"ERROR: Unexpected error occurred.\n");
        return 1;
    }
    static const TU_COLUMN columns[] = {{L"Privilege Name", 0, FALSE}, {L"Description", 0, FALSE}, {L"State", 0, FALSE}};
    TU_TABLE table;
    tu_table_init(&table, columns, 3);
    for (DWORD i = 0; i < privileges->Count; i++) {
        WCHAR name[128], display[256];
        DWORD name_size = TU_ARRAY(name), display_size = TU_ARRAY(display), language;
        if (!LookupPrivilegeNameW(nullptr, &privileges->Privileges[i], name, &name_size))
            continue;
        if (!LookupPrivilegeDisplayNameW(nullptr, name, display, &display_size, &language))
            display[0] = 0;
        const WCHAR *row[3] = {name, display, (privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED_) ? L"Enabled" : L"Disabled"};
        tu_table_row(&table, row);
    }
    title(L"PRIVILEGES INFORMATION", format);
    tu_table_print(&table, format, no_header);
    tu_table_free(&table);
    tu_free(privileges);
    return 0;
}

static int show_claims(int format)
{
    if (format == TU_FMT_CSV) {
        return 0;
    }
    title(L"USER CLAIMS INFORMATION", TU_FMT_LIST);
    tu_puts(L"\nUser claims unknown.\n\nKerberos support for Dynamic Access Control on this device has been disabled.\n");
    return 0;
}

static int show_logon_id(void)
{
    GROUPS_ *groups = token_info(TokenGroups_);
    if (groups) {
        for (DWORD i = 0; i < groups->Count; i++) {
            if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID_) == SE_GROUP_LOGON_ID_) {
                WCHAR *sid = sid_text(groups->Groups[i].Sid);
                tu_print(L"%s\n", sid);
                tu_free(sid);
                tu_free(groups);
                return 0;
            }
        }
        tu_free(groups);
    }
    tu_eprint(L"ERROR: Unexpected error occurred.\n");
    return 1;
}

static BOOL is_switch(const WCHAR *a, const WCHAR *name)
{
    return (a[0] == L'/' || a[0] == L'-') && tu_eq(a + 1, name);
}

int tool_main(void)
{
    BOOL user = FALSE, groups = FALSE, claims = FALSE, priv = FALSE, all = FALSE, upn = FALSE, fqdn = FALSE,
         logon = FALSE, no_header = FALSE, have_format = FALSE;
    int format = TU_FMT_TABLE;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if (is_switch(a, L"?")) {
            if (tu_argc != 2)
                return fail_syntax();
            tu_puts(kUsage);
            return 0;
        } else if (is_switch(a, L"USER")) {
            user = TRUE;
        } else if (is_switch(a, L"GROUPS")) {
            groups = TRUE;
        } else if (is_switch(a, L"CLAIMS")) {
            claims = TRUE;
        } else if (is_switch(a, L"PRIV")) {
            priv = TRUE;
        } else if (is_switch(a, L"ALL")) {
            all = TRUE;
        } else if (is_switch(a, L"UPN")) {
            upn = TRUE;
        } else if (is_switch(a, L"FQDN")) {
            fqdn = TRUE;
        } else if (is_switch(a, L"LOGONID")) {
            logon = TRUE;
        } else if (is_switch(a, L"NH")) {
            no_header = TRUE;
        } else if (is_switch(a, L"FO")) {
            if (i + 1 >= tu_argc)
                return fail_syntax();
            format = tu_format_by_name(tu_argv[++i]);
            if (format < 0) {
                tu_eprint(L"ERROR: Invalid format (/FO) specified.\nType \"WHOAMI /?\" for usage.\n");
                return 1;
            }
            have_format = TRUE;
        } else {
            return fail_syntax();
        }
    }
    int simple = upn + fqdn + logon;
    BOOL detailed = user || groups || claims || priv || all;
    if (simple > 1 || (simple && (detailed || no_header || have_format)) || (all && (user || groups || claims || priv)))
        return fail_syntax();
    if (no_header && format == TU_FMT_LIST) {
        tu_eprint(L"ERROR: /NH switch cannot be used with the LIST format.\nType \"WHOAMI /?\" for usage.\n");
        return 1;
    }
    if ((no_header || have_format) && !detailed)
        return fail_syntax();

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_, &g_token)) {
        tu_eprint(L"ERROR: Unexpected error occurred - error code was %lu.\n", GetLastError());
        return 1;
    }
    if (upn) {
        tu_eprint(L"ERROR: Unable to get User Principal Name (UPN) as the current logged-on user\n       is not a domain user.\n");
        return 1;
    }
    if (fqdn) {
        tu_eprint(L"ERROR: Unable to get Fully Qualified Distinguished Name (FQDN) as the current\n       logged-on user is not a domain user.\n");
        return 1;
    }
    if (logon)
        return show_logon_id();
    if (!detailed) {
        SID_ATTR *info = token_info(TokenUser_);
        if (!info) {
            tu_eprint(L"ERROR: Unexpected error occurred.\n");
            return 1;
        }
        WCHAR *name = account_name(info->Sid, nullptr, TRUE);
        tu_print(L"%s\n", name);
        tu_free(name);
        tu_free(info);
        return 0;
    }
    int result = 0;
    if (all)
        user = groups = claims = priv = TRUE;
    if (user)
        result |= show_user(format, no_header);
    if (groups)
        result |= show_groups(format, no_header);
    if (claims)
        result |= show_claims(format);
    if (priv)
        result |= show_privileges(format, no_header);
    if (format != TU_FMT_CSV)
        tu_puts(L"\n");
    return result ? 1 : 0;
}
