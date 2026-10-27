/*
 * tasklist.exe: lists the processes of the machine.
 *
 *   tasklist [/S system [/U user [/P [password]]]] [/M [module] | /SVC | /V | /APPS] [/FI filter]... [/FO format] [/NH]
 *
 * the default columns are the image name, the process id, the session and the memory in use. /V adds the status, the
 * user, the cpu time and the window title, /SVC the services a process hosts and /M the modules it has loaded.
 * the exit code is 0, or 1 when the command line is wrong or the processes cannot be listed.
 */
#include "proc.h"

#define RPC_S_SERVER_UNAVAILABLE_ 1722

static const WCHAR kUsage[] =
    L"\nTASKLIST [/S system [/U username [/P [password]]]]\n"
    L"         [/M [module] | /SVC | /V] [/FI filter] [/FO format] [/NH]\n"
    L"\n"
    L"Description:\n"
    L"    This tool displays a list of currently running processes on\n"
    L"    either a local or remote machine.\n"
    L"\n"
    L"Parameter List:\n"
    L"   /S     system           Specifies the remote system to connect to.\n\n"
    L"   /U     [domain\\]user    Specifies the user context under which\n"
    L"                           the command should execute.\n\n"
    L"   /P     [password]       Specifies the password for the given\n"
    L"                           user context. Prompts for input if omitted.\n\n"
    L"   /M     [module]         Lists all tasks currently using the given\n"
    L"                           exe/dll name. If the module name is not\n"
    L"                           specified all loaded modules are displayed.\n\n"
    L"   /SVC                    Displays services hosted in each process.\n\n"
    L"   /APPS                   Displays Store Apps and their associated processes.\n\n"
    L"   /V                      Displays verbose task information.\n\n"
    L"   /FI    filter           Displays a set of tasks that match a\n"
    L"                           given criteria specified by the filter.\n\n"
    L"   /FO    format           Specifies the output format.\n"
    L"                           Valid values: \"TABLE\", \"LIST\", \"CSV\".\n\n"
    L"   /NH                     Specifies that the \"Column Header\" should\n"
    L"                           not be displayed in the output.\n"
    L"                           Valid only for \"TABLE\" and \"CSV\" formats.\n\n"
    L"   /?                      Displays this help message.\n"
    L"\n"
    L"Filters:\n"
    L"    Filter Name     Valid Operators           Valid Value(s)\n"
    L"    -----------     ---------------           --------------------------\n"
    L"    STATUS          eq, ne                    RUNNING | SUSPENDED\n"
    L"                                              NOT RESPONDING | UNKNOWN\n"
    L"    IMAGENAME       eq, ne                    Image name\n"
    L"    PID             eq, ne, gt, lt, ge, le    PID value\n"
    L"    SESSION         eq, ne, gt, lt, ge, le    Session number\n"
    L"    SESSIONNAME     eq, ne                    Session name\n"
    L"    CPUTIME         eq, ne, gt, lt, ge, le    CPU time in the format\n"
    L"                                              of hh:mm:ss.\n"
    L"                                              hh - hours,\n"
    L"                                              mm - minutes, ss - seconds\n"
    L"    MEMUSAGE        eq, ne, gt, lt, ge, le    Memory usage in KB\n"
    L"    USERNAME        eq, ne                    User name in [domain\\]user\n"
    L"                                              format\n"
    L"    SERVICES        eq, ne                    Service name\n"
    L"    WINDOWTITLE     eq, ne                    Window title\n"
    L"    MODULES         eq, ne                    DLL name\n"
    L"\n"
    L"NOTE: \"WINDOWTITLE\" and \"STATUS\" filters are not supported when querying\n"
    L"      a remote machine.\n\n"
    L"Examples:\n"
    L"    TASKLIST\n"
    L"    TASKLIST /M\n"
    L"    TASKLIST /V /FO CSV\n"
    L"    TASKLIST /SVC /FO LIST\n"
    L"    TASKLIST /M wbem*\n"
    L"    TASKLIST /FI \"USERNAME ne NT AUTHORITY\\SYSTEM\" /FI \"STATUS eq running\"\n";

static int syntax(const WCHAR *text)
{
    tu_eprint(L"%s", text);
    return 1;
}

static BOOL is_option(const WCHAR *a, const WCHAR *name)
{
    return (a[0] == L'/' || a[0] == L'-') && tu_eq(a + 1, name);
}

static BOOL is_local_system(const WCHAR *name)
{
    WCHAR host[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD size = TU_ARRAY(host);
    if (tu_eq(name, L".") || tu_eq(name, L"localhost") || tu_eq(name, L"127.0.0.1") || tu_eq(name, L"::1"))
        return TRUE;
    while (*name == L'\\')
        name++;
    return GetComputerNameW(host, &size) && tu_eq(name, host);
}

/* a number with the thousands separator and the unit, as the memory column has it. */
static void memory_text(ULONGLONG kb, TU_STR *out)
{
    WCHAR digits[32];
    int n = 0;
    do {
        digits[n++] = (WCHAR)(L'0' + (DWORD)(kb % 10));
        kb /= 10;
    } while (kb);
    for (int i = n - 1; i >= 0; i--) {
        tu_str_putc(out, digits[i]);
        if (i && i % 3 == 0)
            tu_str_putc(out, L',');
    }
    tu_str_puts(out, L" K");
}

static void cpu_text(ULONGLONG seconds, TU_STR *out)
{
    tu_str_format(out, L"%lu:%02lu:%02lu", (DWORD)(seconds / 3600), (DWORD)(seconds / 60 % 60), (DWORD)(seconds % 60));
}

/* the table cuts a long list of names over several lines; the other formats keep one line. */
static void add_wrapped(TU_TABLE *t, int format, const WCHAR *const *head, int head_count, const WCHAR *list, int width)
{
    const WCHAR *cells[8];
    for (int i = 0; i < head_count; i++)
        cells[i] = head[i];
    if (format != TU_FMT_TABLE) {
        cells[head_count] = list;
        tu_table_row(t, cells);
        return;
    }
    TU_STR spaced = {};
    for (const WCHAR *c = list; *c; c++) {
        tu_str_putc(&spaced, *c);
        if (*c == L',')
            tu_str_putc(&spaced, L' ');
    }
    if ((int)spaced.length <= width) {
        cells[head_count] = tu_str_text(&spaced);
        tu_table_row(t, cells);
        tu_str_free(&spaced);
        return;
    }
    tu_str_free(&spaced);
    /* words end with a comma; a line holds as many as fit. */
    TU_STR line = {};
    BOOL first = TRUE;
    const WCHAR *p = list;
    while (*p) {
        const WCHAR *comma = wcschr(p, L',');
        SIZE_T n = comma ? (SIZE_T)(comma - p) + 1 : wcslen(p);
        if (line.length && (int)(line.length + 1 + n) > width) {
            cells[head_count] = tu_str_text(&line);
            if (first) {
                tu_table_row(t, cells);
                first = FALSE;
            } else {
                const WCHAR *blank[8] = {};
                blank[head_count] = cells[head_count];
                tu_table_row(t, blank);
            }
            line.length = 0;
        }
        if (line.length)
            tu_str_putc(&line, L' ');
        tu_str_append(&line, p, n);
        p += n;
    }
    if (line.length) {
        cells[head_count] = tu_str_text(&line);
        if (first) {
            tu_table_row(t, cells);
        } else {
            const WCHAR *blank[8] = {};
            blank[head_count] = cells[head_count];
            tu_table_row(t, blank);
        }
    }
    tu_str_free(&line);
}

int tool_main(void)
{
    BOOL verbose = FALSE, services = FALSE, modules = FALSE, apps = FALSE, no_header = FALSE, have_password = FALSE;
    int format = TU_FMT_TABLE;
    const WCHAR *system = nullptr, *user = nullptr, *module_pattern = nullptr;
    PROC_FILTERS filters = {};
    TU_STR error = {};
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        BOOL has_value = i + 1 < tu_argc;
        if (is_option(a, L"?")) {
            tu_puts(kUsage);
            return 0;
        } else if (is_option(a, L"V")) {
            verbose = TRUE;
        } else if (is_option(a, L"SVC")) {
            services = TRUE;
        } else if (is_option(a, L"APPS")) {
            apps = TRUE;
        } else if (is_option(a, L"NH")) {
            no_header = TRUE;
        } else if (is_option(a, L"M")) {
            modules = TRUE;
            if (has_value && tu_argv[i + 1][0] != L'/' && tu_argv[i + 1][0] != L'-')
                module_pattern = tu_argv[++i];
        } else if (is_option(a, L"S")) {
            if (!has_value)
                return syntax(L"Invalid syntax. Value expected for '/S'.\nType \"TASKLIST /?\" for usage.\n");
            system = tu_argv[++i];
            if (!*system)
                return syntax(L"Invalid syntax. System name cannot be empty.\n");
        } else if (is_option(a, L"U")) {
            if (!has_value)
                return syntax(L"Invalid syntax. Value expected for '/U'.\nType \"TASKLIST /?\" for usage.\n");
            user = tu_argv[++i];
            if (!*user)
                return syntax(L"Invalid syntax. User name cannot be empty.\n");
        } else if (is_option(a, L"P")) {
            have_password = TRUE;
            if (has_value && tu_argv[i + 1][0] != L'/' && tu_argv[i + 1][0] != L'-')
                i++;
        } else if (is_option(a, L"FO")) {
            if (!has_value)
                return syntax(L"Invalid syntax. Value expected for '/FO'.\nType \"TASKLIST /?\" for usage.\n");
            format = tu_format_by_name(tu_argv[++i]);
            if (format < 0) {
                tu_eprint(L"ERROR: Invalid syntax. '%s' value is not allowed for '/FO' option.\nType \"TASKLIST /?\" for usage.\n",
                          tu_argv[i]);
                return 1;
            }
        } else if (is_option(a, L"FI")) {
            if (!has_value)
                return syntax(L"Invalid syntax. Value expected for '/FI'.\nType \"TASKLIST /?\" for usage.\n");
            if (!proc_filter_add(&filters, tu_argv[++i], &error)) {
                tu_eprint(L"ERROR: %sType \"TASKLIST /?\" for usage.\n", tu_str_text(&error));
                return 1;
            }
        } else {
            tu_eprint(L"ERROR: Invalid argument/option - '%s'.\nType \"TASKLIST /?\" for usage.\n", a);
            return 1;
        }
    }
    if (user && !system)
        return syntax(L"Invalid syntax. /U can be specified only when /S is specified.\nType \"TASKLIST /?\" for usage.\n");
    if (have_password && !user)
        return syntax(L"Invalid syntax. /P can be specified only when /U is specified.\nType \"TASKLIST /?\" for usage.\n");
    if ((verbose + services + modules) > 1)
        return syntax(L"Invalid syntax. /V, /M and /SVC options cannot be used together.\nType \"TASKLIST /?\" for usage.\n");
    if (apps && (modules || services))
        return syntax(L"Invalid syntax. /APPS, /M and /SVC options cannot be used together.\nType \"TASKLIST /?\" for usage.\n");
    if (no_header && format == TU_FMT_LIST)
        return syntax(L"Invalid syntax. /NH option is allowed only for \"TABLE\" and \"CSV\" formats.\nType \"TASKLIST /?\" for usage.\n");
    if (system && !is_local_system(system)) {
        tu_error_text(RPC_S_SERVER_UNAVAILABLE_, &error);
        tu_eprint(L"ERROR: %s\n", tu_str_text(&error));
        return 1;
    }

    PROC_LIST list;
    if (!proc_list_load(&list)) {
        tu_eprint(L"ERROR: Unable to list the processes.\n");
        return 1;
    }
    if (verbose || proc_filter_needs(&filters, FILTER_USERNAME))
        proc_load_users(&list);
    if (verbose || proc_filter_needs(&filters, FILTER_WINDOWTITLE) || proc_filter_needs(&filters, FILTER_STATUS))
        proc_load_windows(&list);
    if (services || proc_filter_needs(&filters, FILTER_SERVICES))
        proc_load_services(&list);

    static const TU_COLUMN kDefault[] = {{L"Image Name", 25, FALSE}, {L"PID", 8, TRUE}, {L"Session Name", 16, FALSE},
                                         {L"Session#", 11, TRUE}, {L"Mem Usage", 12, TRUE}};
    static const TU_COLUMN kVerbose[] = {{L"Image Name", 25, FALSE}, {L"PID", 8, TRUE}, {L"Session Name", 16, FALSE},
                                         {L"Session#", 11, TRUE}, {L"Mem Usage", 12, TRUE}, {L"Status", 15, FALSE},
                                         {L"User Name", 50, FALSE}, {L"CPU Time", 12, TRUE}, {L"Window Title", 72, FALSE}};
    static const TU_COLUMN kServices[] = {{L"Image Name", 25, FALSE}, {L"PID", 8, TRUE}, {L"Services", 44, FALSE}};
    static const TU_COLUMN kModules[] = {{L"Image Name", 25, FALSE}, {L"PID", 8, TRUE}, {L"Modules", 44, FALSE}};
    TU_TABLE table;
    if (verbose)
        tu_table_init(&table, kVerbose, 9);
    else if (services)
        tu_table_init(&table, kServices, 3);
    else if (modules)
        tu_table_init(&table, kModules, 3);
    else
        tu_table_init(&table, kDefault, 5);

    for (DWORD i = 0; i < list.Count && !apps; i++) {
        PROC_ENTRY *p = &list.Items[i];
        TU_STR pid = {}, session = {}, memory = {}, cpu = {};
        WCHAR *module_text = nullptr;
        if (modules) {
            module_text = proc_modules(p->Pid);
            if (module_pattern && (!module_text || !proc_list_matches(module_text, module_pattern))) {
                tu_free(module_text);
                continue;
            }
        }
        if (!proc_filter_match(&filters, p)) {
            tu_free(module_text);
            continue;
        }
        tu_str_format(&pid, L"%lu", p->Pid);
        tu_str_format(&session, L"%lu", p->Session);
        memory_text(p->MemoryKb, &memory);
        const WCHAR *head[3] = {p->Name, tu_str_text(&pid)};
        if (services) {
            add_wrapped(&table, format, head, 2, p->Services ? p->Services : L"N/A", 44);
        } else if (modules) {
            add_wrapped(&table, format, head, 2, module_text ? module_text : L"N/A", 44);
        } else {
            cpu_text(p->CpuSeconds, &cpu);
            const WCHAR *cells[9] = {p->Name,
                                     tu_str_text(&pid),
                                     proc_session_name(p->Session),
                                     tu_str_text(&session),
                                     tu_str_text(&memory),
                                     proc_status_name(p),
                                     p->User ? p->User : L"N/A",
                                     tu_str_text(&cpu),
                                     p->Title ? p->Title : L"N/A"};
            tu_table_row(&table, cells);
        }
        tu_free(module_text);
        tu_str_free(&pid);
        tu_str_free(&session);
        tu_str_free(&memory);
        tu_str_free(&cpu);
    }
    if (!table.rows_count) {
        tu_print(L"INFO: No tasks are running which match the specified criteria.\n");
    } else {
        if (format == TU_FMT_TABLE)
            tu_puts(L"\n");
        tu_table_print(&table, format, no_header);
    }
    tu_table_free(&table);
    proc_list_free(&list);
    return 0;
}
