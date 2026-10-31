/*
 * netsh.exe: the firewall rules of `netsh advfirewall firewall`, which installers use to open a port for a service.
 *
 *   netsh advfirewall firewall add rule name=<string> dir=in|out action=allow|block|bypass [protocol=...]
 *         [localport=...] [remoteport=...] [localip=...] [remoteip=...] [program=...] [service=...] [profile=...]
 *         [enable=yes|no] [description=...] [edge=yes|no]
 *   netsh advfirewall firewall delete rule name=<string>|all [dir=...] [protocol=...] [localport=...] ...
 *   netsh advfirewall firewall show rule name=<string>|all [dir=...] [verbose]
 *
 * the rules are kept where the firewall of windows keeps them, as values of the key FirewallRules, in its text format.
 * nothing here filters traffic, there is no firewall to enforce them. every other command ends the way netsh ends
 * when it does not know the command. the exit code is 0, or 1 when a command fails.
 */
#include "tool.h"
#include <stddef.h>

#define RULES_KEY L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules"
#define VERSION_TEXT L"v2.31"

typedef struct RULE {
    WCHAR *Name;
    WCHAR *Description;
    WCHAR *Action;
    WCHAR *Dir;
    WCHAR *Protocol;
    WCHAR *LocalPort;
    WCHAR *RemotePort;
    WCHAR *LocalIp;
    WCHAR *RemoteIp;
    WCHAR *Program;
    WCHAR *Service;
    WCHAR *Profile;
    WCHAR *Interface;
    BOOL Active;
    BOOL Edge;
} RULE;

static void rule_free(RULE *r)
{
    WCHAR **fields[] = {&r->Name,       &r->Description, &r->Action,  &r->Dir,     &r->Protocol,
                        &r->LocalPort,  &r->RemotePort,  &r->LocalIp, &r->RemoteIp, &r->Program,
                        &r->Service,    &r->Profile,     &r->Interface};
    for (SIZE_T i = 0; i < TU_ARRAY(fields); i++)
        tu_free(*fields[i]);
    memset(r, 0, sizeof(*r));
}

static void set_text(WCHAR **field, const WCHAR *value)
{
    tu_free(*field);
    *field = tu_dup(value);
}

/* ------------------------------------------------------------------ the stored text */

/* reads a stored rule, false when the text is not one. */
static BOOL rule_parse(const WCHAR *text, RULE *r)
{
    memset(r, 0, sizeof(*r));
    r->Active = TRUE;
    const WCHAR *p = text;
    const WCHAR *bar = wcschr(p, L'|');
    if (!bar || bar - p < 2 || p[0] != L'v')
        return FALSE;
    p = bar + 1;
    while (*p) {
        bar = wcschr(p, L'|');
        SIZE_T length = bar ? (SIZE_T)(bar - p) : wcslen(p);
        const WCHAR *equals = p;
        while (equals < p + length && *equals != L'=')
            equals++;
        if (equals < p + length) {
            WCHAR *key = tu_dup_n(p, (SIZE_T)(equals - p));
            WCHAR *value = tu_dup_n(equals + 1, (SIZE_T)(p + length - equals - 1));
            if (tu_eq(key, L"Name"))
                set_text(&r->Name, value);
            else if (tu_eq(key, L"Desc"))
                set_text(&r->Description, value);
            else if (tu_eq(key, L"Action"))
                set_text(&r->Action, value);
            else if (tu_eq(key, L"Dir"))
                set_text(&r->Dir, value);
            else if (tu_eq(key, L"Protocol"))
                set_text(&r->Protocol, value);
            else if (tu_eq(key, L"LPort"))
                set_text(&r->LocalPort, value);
            else if (tu_eq(key, L"RPort"))
                set_text(&r->RemotePort, value);
            else if (tu_eq(key, L"LA4") || tu_eq(key, L"LA6"))
                set_text(&r->LocalIp, value);
            else if (tu_eq(key, L"RA4") || tu_eq(key, L"RA6"))
                set_text(&r->RemoteIp, value);
            else if (tu_eq(key, L"App"))
                set_text(&r->Program, value);
            else if (tu_eq(key, L"Svc"))
                set_text(&r->Service, value);
            else if (tu_eq(key, L"Profile"))
                set_text(&r->Profile, value);
            else if (tu_eq(key, L"IFType"))
                set_text(&r->Interface, value);
            else if (tu_eq(key, L"Active"))
                r->Active = tu_eq(value, L"TRUE");
            else if (tu_eq(key, L"Edge"))
                r->Edge = tu_eq(value, L"TRUE");
            tu_free(key);
            tu_free(value);
        }
        if (!bar)
            break;
        p = bar + 1;
    }
    return r->Name != nullptr;
}

static void put_field(TU_STR *s, const WCHAR *key, const WCHAR *value)
{
    if (value && *value)
        tu_str_format(s, L"%s=%s|", key, value);
}

static void rule_format(const RULE *r, TU_STR *s)
{
    tu_str_puts(s, VERSION_TEXT L"|");
    put_field(s, L"Action", r->Action);
    tu_str_puts(s, r->Active ? L"Active=TRUE|" : L"Active=FALSE|");
    put_field(s, L"Dir", r->Dir);
    put_field(s, L"Protocol", r->Protocol);
    put_field(s, L"Profile", r->Profile);
    put_field(s, L"IFType", r->Interface);
    put_field(s, L"LPort", r->LocalPort);
    put_field(s, L"RPort", r->RemotePort);
    put_field(s, L"LA4", r->LocalIp);
    put_field(s, L"RA4", r->RemoteIp);
    put_field(s, L"App", r->Program);
    put_field(s, L"Svc", r->Service);
    if (r->Edge)
        tu_str_puts(s, L"Edge=TRUE|");
    put_field(s, L"Name", r->Name);
    put_field(s, L"Desc", r->Description);
}

/* ------------------------------------------------------------------ the store */

typedef struct STORED {
    WCHAR *Value;
    RULE Rule;
} STORED;

static HKEY open_rules(BOOL create, DWORD access, DWORD *error)
{
    HKEY key = nullptr;
    if (create)
        *error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, RULES_KEY, 0, nullptr, 0, access | KEY_WOW64_64KEY, nullptr, &key,
                                 nullptr);
    else
        *error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, RULES_KEY, 0, access | KEY_WOW64_64KEY, &key);
    return *error ? nullptr : key;
}

/* the rules in the store; the caller frees the array and each of its entries. */
static STORED *load_rules(HKEY key, SIZE_T *count)
{
    STORED *rules = nullptr;
    *count = 0;
    for (DWORD i = 0;; i++) {
        WCHAR name[260];
        DWORD name_length = TU_ARRAY(name);
        DWORD type, size = 0;
        LONG status = RegEnumValueW(key, i, name, &name_length, nullptr, &type, nullptr, &size);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
            continue;
        if (type != REG_SZ)
            continue;
        WCHAR *data = tu_alloc(size + sizeof(WCHAR));
        DWORD got = size;
        if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)data, &got) != ERROR_SUCCESS) {
            tu_free(data);
            continue;
        }
        data[got / sizeof(WCHAR)] = 0;
        STORED entry = {};
        if (!rule_parse(data, &entry.Rule)) {
            rule_free(&entry.Rule);
            tu_free(data);
            continue;
        }
        tu_free(data);
        entry.Value = tu_dup(name);
        rules = tu_realloc(rules, (*count + 1) * sizeof(STORED));
        rules[(*count)++] = entry;
    }
    return rules;
}

static void free_rules(STORED *rules, SIZE_T count)
{
    for (SIZE_T i = 0; i < count; i++) {
        tu_free(rules[i].Value);
        rule_free(&rules[i].Rule);
    }
    tu_free(rules);
}

/* ------------------------------------------------------------------ the command line */

typedef struct ARGS {
    const WCHAR *Name, *Description, *Dir, *Action, *Protocol, *LocalPort, *RemotePort, *LocalIp, *RemoteIp, *Program,
        *Service, *Profile, *Interface, *Enable, *Edge;
    BOOL Verbose;
} ARGS;

static BOOL same_name(const WCHAR *a, const WCHAR *b)
{
    return a && b && CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

/* the words of a command: a word that ends with = takes the next one as its value. */
static BOOL parse_args(int first, ARGS *args, const WCHAR **bad)
{
    static const struct {
        const WCHAR *Key;
        SIZE_T Offset;
    } keys[] = {
        {L"name", offsetof(ARGS, Name)},           {L"description", offsetof(ARGS, Description)},
        {L"dir", offsetof(ARGS, Dir)},             {L"action", offsetof(ARGS, Action)},
        {L"protocol", offsetof(ARGS, Protocol)},   {L"localport", offsetof(ARGS, LocalPort)},
        {L"remoteport", offsetof(ARGS, RemotePort)}, {L"localip", offsetof(ARGS, LocalIp)},
        {L"remoteip", offsetof(ARGS, RemoteIp)},   {L"program", offsetof(ARGS, Program)},
        {L"service", offsetof(ARGS, Service)},     {L"profile", offsetof(ARGS, Profile)},
        {L"interfacetype", offsetof(ARGS, Interface)}, {L"enable", offsetof(ARGS, Enable)},
        {L"edge", offsetof(ARGS, Edge)},
    };
    memset(args, 0, sizeof(*args));
    for (int i = first; i < tu_argc; i++) {
        const WCHAR *word = tu_argv[i];
        if (tu_eq(word, L"verbose")) {
            args->Verbose = TRUE;
            continue;
        }
        const WCHAR *equals = wcschr(word, L'=');
        if (!equals) {
            *bad = word;
            return FALSE;
        }
        WCHAR *key = tu_dup_n(word, (SIZE_T)(equals - word));
        const WCHAR *value = equals + 1;
        if (!*value && i + 1 < tu_argc)
            value = tu_argv[++i];
        BOOL known = FALSE;
        for (SIZE_T k = 0; k < TU_ARRAY(keys); k++)
            if (tu_eq(key, keys[k].Key)) {
                *(const WCHAR **)((BYTE *)args + keys[k].Offset) = value;
                known = TRUE;
                break;
            }
        tu_free(key);
        if (!known) {
            *bad = word;
            return FALSE;
        }
    }
    return TRUE;
}

static const WCHAR kAddUsage[] =
    L"\nUsage: add rule name=<string>\n"
    L"      dir=in|out\n"
    L"      action=allow|block|bypass (default=allow)\n"
    L"      [program=<program path>]\n"
    L"      [service=<service short name>|any]\n"
    L"      [description=<string>]\n"
    L"      [enable=yes|no (default=yes)]\n"
    L"      [profile=public|private|domain|any[,...]]\n"
    L"      [localip=any|<IPv4 address>|<IPv6 address>|<subnet>|<range>|<list>]\n"
    L"      [remoteip=any|localsubnet|dns|dhcp|wins|defaultgateway|\n"
    L"         <IPv4 address>|<IPv6 address>|<subnet>|<range>|<list>]\n"
    L"      [localport=0-65535|<port range>[,...]|RPC|RPC-EPMap|IPHTTPS|any (default=any)]\n"
    L"      [remoteport=0-65535|<port range>[,...]|any (default=any)]\n"
    L"      [protocol=0-255|icmpv4|icmpv6|icmpv4:type,code|icmpv6:type,code|\n"
    L"         tcp|udp|any (default=any)]\n"
    L"      [interfacetype=wireless|lan|ras|any]\n"
    L"      [edge=yes|deferapp|deferuser|no (default=no)]\n\n";

static BOOL yes_no(const WCHAR *text, BOOL *value)
{
    if (tu_eq(text, L"yes") || tu_eq(text, L"true")) {
        *value = TRUE;
        return TRUE;
    }
    if (tu_eq(text, L"no") || tu_eq(text, L"false")) {
        *value = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* the protocol as the store keeps it: a number. */
static WCHAR *protocol_number(const WCHAR *text)
{
    if (tu_eq(text, L"tcp"))
        return tu_dup(L"6");
    if (tu_eq(text, L"udp"))
        return tu_dup(L"17");
    if (tu_eq(text, L"icmpv4"))
        return tu_dup(L"1");
    if (tu_eq(text, L"icmpv6"))
        return tu_dup(L"58");
    if (tu_eq(text, L"any"))
        return tu_dup(L"256");
    DWORD number;
    if (tu_parse_u32(text, &number) && number < 256)
        return tu_dup(text);
    return nullptr;
}

static const WCHAR *protocol_text(const WCHAR *stored, WCHAR *buffer)
{
    if (!stored || tu_eq(stored, L"256"))
        return L"Any";
    if (tu_eq(stored, L"6"))
        return L"TCP";
    if (tu_eq(stored, L"17"))
        return L"UDP";
    if (tu_eq(stored, L"1"))
        return L"ICMPv4";
    if (tu_eq(stored, L"58"))
        return L"ICMPv6";
    lstrcpyW(buffer, stored);
    return buffer;
}

static BOOL ports_valid(const WCHAR *text)
{
    if (tu_eq(text, L"any") || tu_eq(text, L"rpc") || tu_eq(text, L"rpc-epmap") || tu_eq(text, L"iphttps"))
        return TRUE;
    BOOL digit = FALSE;
    for (const WCHAR *p = text; *p; p++) {
        if (tu_is_digit(*p))
            digit = TRUE;
        else if (*p != L',' && *p != L'-')
            return FALSE;
    }
    return digit;
}

static int fail(const WCHAR *text)
{
    tu_print(L"\n%s\n", text);
    return 1;
}

static int elevation(void)
{
    tu_print(L"\nThe requested operation requires elevation (Run as administrator).\n");
    return 1;
}

static int add_rule(int first)
{
    ARGS args;
    const WCHAR *bad = nullptr;
    if (!parse_args(first, &args, &bad)) {
        tu_print(L"\nThe parameter %s is not valid.\n", bad);
        tu_puts(kAddUsage);
        return 1;
    }
    if (!args.Name || !args.Dir) {
        tu_puts(kAddUsage);
        return 1;
    }
    if (tu_eq(args.Name, L"all") || !*args.Name)
        return fail(L"The rule name cannot be \"all\".");
    RULE r = {};
    r.Active = TRUE;
    if (!tu_eq(args.Dir, L"in") && !tu_eq(args.Dir, L"out"))
        return fail(L"The value of the dir parameter must be in or out.");
    set_text(&r.Dir, tu_eq(args.Dir, L"in") ? L"In" : L"Out");
    const WCHAR *action = args.Action ? args.Action : L"allow";
    if (tu_eq(action, L"allow"))
        set_text(&r.Action, L"Allow");
    else if (tu_eq(action, L"block"))
        set_text(&r.Action, L"Block");
    else if (tu_eq(action, L"bypass"))
        set_text(&r.Action, L"Allow-Bypass");
    else
        return fail(L"The value of the action parameter must be allow, block or bypass.");
    if (args.Enable && !yes_no(args.Enable, &r.Active))
        return fail(L"The value of the enable parameter must be yes or no.");
    if (args.Edge && !yes_no(args.Edge, &r.Edge) && !tu_eq(args.Edge, L"deferapp") && !tu_eq(args.Edge, L"deferuser"))
        return fail(L"The value of the edge parameter must be yes, deferapp, deferuser or no.");
    if (args.Protocol) {
        WCHAR *number = protocol_number(args.Protocol);
        if (!number)
            return fail(L"The protocol is not valid.");
        if (!tu_eq(number, L"256"))
            set_text(&r.Protocol, number);
        tu_free(number);
    }
    if ((args.LocalPort && !ports_valid(args.LocalPort)) || (args.RemotePort && !ports_valid(args.RemotePort)))
        return fail(L"An invalid port range was specified, or 0 was used as a port number.");
    if (args.LocalPort && !tu_eq(args.LocalPort, L"any")) {
        if (r.Protocol && !tu_eq(r.Protocol, L"6") && !tu_eq(r.Protocol, L"17"))
            return fail(L"Ports can only be specified when the protocol is TCP or UDP.");
        set_text(&r.LocalPort, args.LocalPort);
    }
    if (args.RemotePort && !tu_eq(args.RemotePort, L"any"))
        set_text(&r.RemotePort, args.RemotePort);
    if (args.LocalIp && !tu_eq(args.LocalIp, L"any"))
        set_text(&r.LocalIp, args.LocalIp);
    if (args.RemoteIp && !tu_eq(args.RemoteIp, L"any"))
        set_text(&r.RemoteIp, args.RemoteIp);
    if (args.Program)
        set_text(&r.Program, args.Program);
    if (args.Service && !tu_eq(args.Service, L"any"))
        set_text(&r.Service, args.Service);
    if (args.Interface && !tu_eq(args.Interface, L"any"))
        set_text(&r.Interface, args.Interface);
    if (args.Description)
        set_text(&r.Description, args.Description);
    set_text(&r.Name, args.Name);

    DWORD error;
    HKEY key = open_rules(TRUE, KEY_SET_VALUE, &error);
    if (!key) {
        rule_free(&r);
        return error == ERROR_ACCESS_DENIED ? elevation() : fail(L"The rule could not be added.");
    }
    /* the name of a value is made up for the rule, a guid in braces. */
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    ULONG64 seed = (ULONG64)counter.QuadPart ^ ((ULONG64)GetCurrentProcessId() << 32) ^ GetTickCount64();
    ULONG64 a = seed * 0x9E3779B97F4A7C15ull, b = (seed ^ 0xD1B54A32D192ED03ull) * 0xBF58476D1CE4E5B9ull;
    TU_STR value_name = {};
    tu_str_format(&value_name, L"{%08X-%04X-4%03X-A%03X-%012llX}", (DWORD)(a >> 32), (DWORD)(a >> 16) & 0xFFFF,
                  (DWORD)b & 0xFFF, (DWORD)(b >> 12) & 0xFFF, (b >> 24) & 0xFFFFFFFFFFFFull);
    TU_STR text = {};
    rule_format(&r, &text);
    error = RegSetValueExW(key, tu_str_text(&value_name), 0, REG_SZ, (const BYTE *)tu_str_text(&text),
                           (DWORD)((text.length + 1) * sizeof(WCHAR)));
    tu_str_free(&text);
    tu_str_free(&value_name);
    RegCloseKey(key);
    rule_free(&r);
    if (error)
        return error == ERROR_ACCESS_DENIED ? elevation() : fail(L"The rule could not be added.");
    tu_print(L"Ok.\n\n");
    return 0;
}

/* a stored rule is selected by the name, and by any of the other words that were given. */
static BOOL selected(const RULE *r, const ARGS *args)
{
    if (args->Name && !tu_eq(args->Name, L"all") && !same_name(args->Name, r->Name))
        return FALSE;
    if (args->Dir && !same_name(args->Dir, r->Dir))
        return FALSE;
    if (args->Protocol) {
        WCHAR *number = protocol_number(args->Protocol);
        BOOL match = number && ((r->Protocol && tu_eq(number, r->Protocol)) || (!r->Protocol && tu_eq(number, L"256")));
        tu_free(number);
        if (!match)
            return FALSE;
    }
    if (args->LocalPort && !(r->LocalPort ? same_name(args->LocalPort, r->LocalPort) : tu_eq(args->LocalPort, L"any")))
        return FALSE;
    if (args->RemotePort && !(r->RemotePort ? same_name(args->RemotePort, r->RemotePort) : tu_eq(args->RemotePort, L"any")))
        return FALSE;
    if (args->Program && !same_name(args->Program, r->Program))
        return FALSE;
    if (args->Service && !same_name(args->Service, r->Service))
        return FALSE;
    return TRUE;
}

static int delete_rule(int first)
{
    ARGS args;
    const WCHAR *bad = nullptr;
    if (!parse_args(first, &args, &bad) || !args.Name) {
        tu_print(L"\nUsage: delete rule name=<string>\n      [dir=in|out]\n      [profile=public|private|domain|any[,...]]\n"
                 L"      [program=<program path>]\n      [service=<service short name>|any]\n"
                 L"      [localip=any|<IPv4 address>|<IPv6 address>|<subnet>|<range>|<list>]\n"
                 L"      [remoteip=any|localsubnet|dns|dhcp|wins|defaultgateway|\n"
                 L"         <IPv4 address>|<IPv6 address>|<subnet>|<range>|<list>]\n"
                 L"      [localport=0-65535|<port range>[,...]|RPC|RPC-EPMap|IPHTTPS|any]\n"
                 L"      [remoteport=0-65535|<port range>[,...]|any]\n"
                 L"      [protocol=0-255|icmpv4|icmpv6|icmpv4:type,code|icmpv6:type,code|\n         tcp|udp|any]\n\n");
        return 1;
    }
    DWORD error;
    HKEY key = open_rules(FALSE, KEY_QUERY_VALUE | KEY_SET_VALUE, &error);
    if (!key && error == ERROR_ACCESS_DENIED)
        return elevation();
    SIZE_T count = 0;
    STORED *rules = key ? load_rules(key, &count) : nullptr;
    DWORD deleted = 0;
    for (SIZE_T i = 0; i < count; i++)
        if (selected(&rules[i].Rule, &args) && RegDeleteValueW(key, rules[i].Value) == ERROR_SUCCESS)
            deleted++;
    free_rules(rules, count);
    if (key)
        RegCloseKey(key);
    if (!deleted)
        return fail(L"No rules match the specified criteria.");
    tu_print(L"\nDeleted %lu rule(s).\nOk.\n\n", deleted);
    return 0;
}

static const WCHAR *or_any(const WCHAR *text)
{
    return text && *text ? text : L"Any";
}

static void show_one(const RULE *r, BOOL verbose)
{
    WCHAR number[16];
    tu_print(L"\nRule Name:                            %s\n", r->Name);
    tu_print(L"----------------------------------------------------------------------\n");
    if (verbose && r->Description)
        tu_print(L"Description:                          %s\n", r->Description);
    tu_print(L"Enabled:                              %s\n", r->Active ? L"Yes" : L"No");
    tu_print(L"Direction:                            %s\n", or_any(r->Dir));
    tu_print(L"Profiles:                             %s\n", r->Profile ? r->Profile : L"Domain,Private,Public");
    tu_print(L"Grouping:                             \n");
    tu_print(L"LocalIP:                              %s\n", or_any(r->LocalIp));
    tu_print(L"RemoteIP:                             %s\n", or_any(r->RemoteIp));
    tu_print(L"Protocol:                             %s\n", protocol_text(r->Protocol, number));
    tu_print(L"LocalPort:                            %s\n", or_any(r->LocalPort));
    tu_print(L"RemotePort:                           %s\n", or_any(r->RemotePort));
    tu_print(L"Edge traversal:                       %s\n", r->Edge ? L"Yes" : L"No");
    tu_print(L"Action:                               %s\n", r->Action && tu_eq(r->Action, L"Allow-Bypass") ? L"Bypass" : or_any(r->Action));
}

static int show_rule(int first)
{
    ARGS args;
    const WCHAR *bad = nullptr;
    if (!parse_args(first, &args, &bad) || !args.Name) {
        tu_print(L"\nUsage: show rule name=<string>\n      [dir=in|out]\n      [profile=public|private|domain|any[,...]]\n"
                 L"      [type=static|dynamic]\n      [verbose]\n\n");
        return 1;
    }
    DWORD error;
    HKEY key = open_rules(FALSE, KEY_QUERY_VALUE, &error);
    SIZE_T count = 0;
    STORED *rules = key ? load_rules(key, &count) : nullptr;
    BOOL found = FALSE;
    for (SIZE_T i = 0; i < count; i++)
        if (selected(&rules[i].Rule, &args)) {
            show_one(&rules[i].Rule, args.Verbose);
            found = TRUE;
        }
    free_rules(rules, count);
    if (key)
        RegCloseKey(key);
    if (!found)
        return fail(L"No rules match the specified criteria.");
    tu_print(L"\nOk.\n\n");
    return 0;
}

/* a word of the command line is the command when it is at least the first letters of it. */
static BOOL is_word(const WCHAR *given, const WCHAR *command)
{
    SIZE_T length = wcslen(given);
    return length && length <= wcslen(command) && tu_eq_n(given, command, length);
}

static int unknown(int count)
{
    TU_STR text = {};
    for (int i = 1; i < count && i < tu_argc; i++)
        tu_str_format(&text, i > 1 ? L" %s" : L"%s", tu_argv[i]);
    tu_print(L"The following command was not found: %s.\n", tu_str_text(&text));
    tu_str_free(&text);
    return 1;
}

int tool_main(void)
{
    if (tu_argc < 2)
        return 0;
    if (tu_argc >= 5 && is_word(tu_argv[1], L"advfirewall") && is_word(tu_argv[2], L"firewall") &&
        is_word(tu_argv[4], L"rule")) {
        if (is_word(tu_argv[3], L"add"))
            return add_rule(5);
        if (is_word(tu_argv[3], L"delete"))
            return delete_rule(5);
        if (is_word(tu_argv[3], L"show"))
            return show_rule(5);
    }
    return unknown(tu_argc);
}
