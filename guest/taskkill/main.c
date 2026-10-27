/*
 * taskkill.exe: ends processes chosen by id, by image name or by a filter.
 *
 *   taskkill [/S system [/U user [/P [password]]]] { [/FI filter]... [/PID id]... | [/IM name]... } [/T] [/F]
 *
 * without /F a process is asked to close: its top level windows get WM_CLOSE, and a process without any can only be
 * ended forcefully. exit code 0 when every process ended, 128 when nothing matched, 1 for the rest.
 */
#include "proc.h"
#include <mime/win32/winuser.h>

#define PROCESS_TERMINATE_ 0x0001
#define PROCESS_QUERY_LIMITED_INFORMATION_ 0x1000
#define ProcessBreakOnTermination_ 29
#define RPC_S_SERVER_UNAVAILABLE_ 1722

typedef struct OPTIONS {
    BOOL Force;
    BOOL Tree;
    BOOL Help;
    WCHAR **Pids;
    int PidCount;
    WCHAR **Names;
    int NameCount;
    PROC_FILTERS Filters;
    const WCHAR *System;
    const WCHAR *User;
    BOOL HavePassword;
} OPTIONS;

static const WCHAR kUsage[] =
    L"\nTASKKILL [/S system [/U username [/P [password]]]]\n"
    L"         { [/FI filter] [/PID processid | /IM imagename] } [/T] [/F]\n"
    L"\n"
    L"Description:\n"
    L"    This tool is used to terminate tasks by process id (PID) or image name.\n"
    L"\n"
    L"Parameter List:\n"
    L"    /S    system           Specifies the remote system to connect to.\n\n"
    L"    /U    [domain\\]user    Specifies the user context under which the\n"
    L"                           command should execute.\n\n"
    L"    /P    [password]       Specifies the password for the given user\n"
    L"                           context. Prompts for input if omitted.\n\n"
    L"    /FI   filter           Applies a filter to select a set of tasks.\n"
    L"                           Allows \"*\" to be used. ex. imagename eq acme*\n\n"
    L"    /PID  processid        Specifies the PID of the process to be terminated.\n"
    L"                           Use TaskList to get the PID.\n\n"
    L"    /IM   imagename        Specifies the image name of the process\n"
    L"                           to be terminated. Wildcard '*' can be used\n"
    L"                           to specify all tasks or image names.\n\n"
    L"    /T                     Terminates the specified process and any\n"
    L"                           child processes which were started by it.\n\n"
    L"    /F                     Specifies to forcefully terminate the process(es).\n\n"
    L"    /?                     Displays this help message.\n"
    L"\n"
    L"Filters:\n"
    L"    Filter Name   Valid Operators           Valid Value(s)\n"
    L"    -----------   ---------------           -------------------------\n"
    L"    STATUS        eq, ne                    RUNNING |\n"
    L"                                            NOT RESPONDING | UNKNOWN\n"
    L"    IMAGENAME     eq, ne                    Image name\n"
    L"    PID           eq, ne, gt, lt, ge, le    PID value\n"
    L"    SESSION       eq, ne, gt, lt, ge, le    Session number.\n"
    L"    CPUTIME       eq, ne, gt, lt, ge, le    CPU time in the format\n"
    L"                                            of hh:mm:ss.\n"
    L"                                            hh - hours,\n"
    L"                                            mm - minutes, ss - seconds\n"
    L"    MEMUSAGE      eq, ne, gt, lt, ge, le    Memory usage in KB\n"
    L"    USERNAME      eq, ne                    User name in [domain\\]user\n"
    L"                                            format\n"
    L"    SERVICES      eq, ne                    Service name\n"
    L"    WINDOWTITLE   eq, ne                    Window title\n"
    L"\n"
    L"    NOTE\n"
    L"    ----\n"
    L"    1) Wildcard '*' for /IM switch is accepted only when a filter is applied.\n"
    L"    2) Termination of remote processes will always be done forcefully (/F).\n\n"
    L"Examples:\n"
    L"    TASKKILL /IM notepad.exe\n"
    L"    TASKKILL /PID 1230 /PID 1241 /PID 1253 /T\n"
    L"    TASKKILL /F /IM cmd.exe /T \n"
    L"    TASKKILL /F /FI \"PID ge 1000\" /FI \"WINDOWTITLE ne untitle*\"\n";

static int fail_syntax(const WCHAR *text)
{
    tu_eprint(L"ERROR: %s", text);
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
    const WCHAR *bare = name;
    while (*bare == L'\\')
        bare++;
    return GetComputerNameW(host, &size) && tu_eq(bare, host);
}

/* ------------------------------------------------------------------ closing and ending */

static BOOL CALLBACK close_callback(HWND window, LPARAM parameter)
{
    DWORD *state = (DWORD *)parameter;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == state[0] && PostMessageW(window, WM_CLOSE, 0, 0))
        state[1]++;
    return TRUE;
}

static BOOL has_windows(DWORD pid)
{
    DWORD state[2] = {pid, 0};
    EnumWindows(close_callback, (LPARAM)state);
    return state[1] != 0;
}

typedef struct VERDICT {
    DWORD Error;
    const WCHAR *Reason;
} VERDICT;

static BOOL is_critical(DWORD pid)
{
    if (pid == 0 || pid == 4)
        return TRUE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION_, FALSE, pid);
    if (!process)
        return FALSE;
    ULONG critical = 0;
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBreakOnTermination_, &critical, sizeof(critical), nullptr);
    CloseHandle(process);
    return status >= 0 && critical;
}

/* ends one process; returns 0, or the reason in text. */
static DWORD end_process(DWORD pid, BOOL force, TU_STR *reason, BOOL *signalled)
{
    *signalled = FALSE;
    if (pid == GetCurrentProcessId()) {
        tu_str_puts(reason, L"The process cannot terminate itself.\n");
        return 1;
    }
    if (is_critical(pid)) {
        tu_str_puts(reason, L"This is critical system process. Taskkill cannot end this process.\n");
        return 1;
    }
    if (!force) {
        if (!has_windows(pid)) {
            tu_str_puts(reason, L"This process can only be terminated forcefully (with /F option).\n");
            return 1;
        }
        *signalled = TRUE;
        return 0;
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE_, FALSE, pid);
    DWORD error = 0;
    if (!process || !TerminateProcess(process, 1))
        error = GetLastError();
    if (process)
        CloseHandle(process);
    if (error) {
        tu_error_text(error, reason);
        tu_str_putc(reason, L'\n');
        return error;
    }
    return 0;
}

typedef struct RUN {
    PROC_LIST *List;
    BOOL Force;
    BOOL Tree;
    int Failures;
    int Done;
} RUN;

/* the result of one process, in the words of the tool. parent is zero when it is not a child. */
static void report(RUN *run, PROC_ENTRY *p, BOOL by_name, DWORD parent)
{
    TU_STR reason = {};
    BOOL signalled;
    DWORD error = end_process(p->Pid, run->Force, &reason, &signalled);
    if (!error) {
        run->Done++;
        if (parent)
            tu_print(signalled ? L"SUCCESS: Sent termination signal to process with PID %lu, child of PID %lu.\n"
                               : L"SUCCESS: The process with PID %lu (child process of PID %lu) has been terminated.\n",
                     p->Pid, parent);
        else if (by_name)
            tu_print(signalled ? L"SUCCESS: Sent termination signal to the process \"%s\" with PID %lu.\n"
                               : L"SUCCESS: The process \"%s\" with PID %lu has been terminated.\n",
                     p->Name, p->Pid);
        else
            tu_print(signalled ? L"SUCCESS: Sent termination signal to the process with PID %lu.\n"
                               : L"SUCCESS: The process with PID %lu has been terminated.\n",
                     p->Pid);
    } else {
        run->Failures++;
        if (parent)
            tu_eprint(L"ERROR: The process with PID %lu (child process of PID %lu) could not be terminated.\nReason: %s",
                      p->Pid, parent, tu_str_text(&reason));
        else if (by_name)
            tu_eprint(L"ERROR: The process \"%s\" with PID %lu could not be terminated.\nReason: %s", p->Name, p->Pid,
                      tu_str_text(&reason));
        else
            tu_eprint(L"ERROR: The process with PID %lu could not be terminated.\nReason: %s", p->Pid,
                      tu_str_text(&reason));
    }
    tu_str_free(&reason);
}

/* the children first, then the process. depth guards against a cycle made of reused ids. */
static void end_tree(RUN *run, PROC_ENTRY *p, BOOL by_name, DWORD parent, int depth)
{
    if (run->Tree && depth < 64) {
        for (DWORD i = 0; i < run->List->Count; i++) {
            PROC_ENTRY *c = &run->List->Items[i];
            if (c->Parent == p->Pid && c->Pid != p->Pid && c->Pid != 0)
                end_tree(run, c, by_name, p->Pid, depth + 1);
        }
    }
    report(run, p, by_name, parent);
}

/* ------------------------------------------------------------------ the command line */

static int parse(OPTIONS *o)
{
    TU_STR error = {};
    o->Pids = tu_alloc(tu_argc * sizeof(WCHAR *));
    o->Names = tu_alloc(tu_argc * sizeof(WCHAR *));
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        BOOL has_value = i + 1 < tu_argc;
        if (is_option(a, L"?")) {
            o->Help = TRUE;
        } else if (is_option(a, L"F")) {
            o->Force = TRUE;
        } else if (is_option(a, L"T")) {
            o->Tree = TRUE;
        } else if (is_option(a, L"S")) {
            if (!has_value)
                return fail_syntax(L"Invalid syntax. Value expected for '/S'.\nType \"TASKKILL /?\" for usage.\n");
            o->System = tu_argv[++i];
            if (!*o->System)
                return fail_syntax(L"Invalid syntax. System name cannot be empty.\n");
        } else if (is_option(a, L"U")) {
            if (!has_value)
                return fail_syntax(L"Invalid syntax. Value expected for '/U'.\nType \"TASKKILL /?\" for usage.\n");
            o->User = tu_argv[++i];
            if (!*o->User)
                return fail_syntax(L"Invalid syntax. User name cannot be empty.\n");
        } else if (is_option(a, L"P")) {
            o->HavePassword = TRUE;
            if (has_value && tu_argv[i + 1][0] != L'/' && tu_argv[i + 1][0] != L'-')
                i++;
        } else if (is_option(a, L"FI")) {
            if (!has_value)
                return fail_syntax(L"Invalid syntax. Value expected for '/FI'.\nType \"TASKKILL /?\" for usage.\n");
            if (!proc_filter_add(&o->Filters, tu_argv[++i], &error)) {
                tu_eprint(L"ERROR: %sType \"TASKKILL /?\" for usage.\n", tu_str_text(&error));
                return 1;
            }
        } else if (is_option(a, L"PID")) {
            if (!has_value)
                return fail_syntax(L"Invalid syntax. Value expected for '/PID'.\nType \"TASKKILL /?\" for usage.\n");
            o->Pids[o->PidCount++] = tu_argv[++i];
        } else if (is_option(a, L"IM")) {
            if (!has_value)
                return fail_syntax(L"Invalid syntax. Value expected for '/IM'.\nType \"TASKKILL /?\" for usage.\n");
            o->Names[o->NameCount++] = tu_argv[++i];
        } else {
            tu_eprint(L"ERROR: Invalid argument/option - '%s'.\nType \"TASKKILL /?\" for usage.\n", a);
            return 1;
        }
    }
    tu_str_free(&error);
    return 0;
}

int tool_main(void)
{
    OPTIONS o = {};
    if (tu_argc < 2)
        return fail_syntax(L"Invalid syntax. Neither /FI nor /PID nor /IM were specified.\nType \"TASKKILL /?\" for usage.\n");
    int status = parse(&o);
    if (status)
        return status;
    if (o.Help) {
        if (tu_argc > 2)
            return fail_syntax(L"Invalid syntax.\nType \"TASKKILL /?\" for usage.\n");
        tu_puts(kUsage);
        return 0;
    }
    if (o.User && !o.System)
        return fail_syntax(L"Invalid syntax. /U can be specified only when /S is specified.\nType \"TASKKILL /?\" for usage.\n");
    if (o.HavePassword && !o.User)
        return fail_syntax(L"Invalid syntax. /P can be specified only when /U is specified.\nType \"TASKKILL /?\" for usage.\n");
    if (o.PidCount && o.NameCount)
        return fail_syntax(L"Invalid syntax. /PID and /IM cannot be used at the same time.\nType \"TASKKILL /?\" for usage.\n");
    if (!o.PidCount && !o.NameCount && !o.Filters.Count)
        return fail_syntax(L"Invalid syntax. Neither /FI nor /PID nor /IM were specified.\nType \"TASKKILL /?\" for usage.\n");
    for (int i = 0; i < o.NameCount; i++)
        if (tu_eq(o.Names[i], L"*") && !o.Filters.Count)
            return fail_syntax(L"Invalid syntax. '*' is accepted only when a filter is applied.\nType \"TASKKILL /?\" for usage.\n");
    DWORD *pids = tu_alloc((o.PidCount + 1) * sizeof(DWORD));
    for (int i = 0; i < o.PidCount; i++)
        if (!tu_parse_u32(o.Pids[i], &pids[i]))
            return fail_syntax(L"Specify a valid numeric value for '/PID' option.\n");
    if (o.System && !is_local_system(o.System)) {
        TU_STR text = {};
        tu_error_text(RPC_S_SERVER_UNAVAILABLE_, &text);
        tu_eprint(L"ERROR: %s\n", tu_str_text(&text));
        return 1;
    }

    PROC_LIST list;
    if (!proc_list_load(&list)) {
        tu_eprint(L"ERROR: Unable to list the processes.\n");
        return 1;
    }
    if (proc_filter_needs(&o.Filters, FILTER_USERNAME))
        proc_load_users(&list);
    if (proc_filter_needs(&o.Filters, FILTER_WINDOWTITLE) || proc_filter_needs(&o.Filters, FILTER_STATUS))
        proc_load_windows(&list);
    if (proc_filter_needs(&o.Filters, FILTER_SERVICES))
        proc_load_services(&list);

    RUN run = {&list, o.Force, o.Tree};
    int missing = 0;
    BOOL any_target = FALSE;
    if (o.PidCount) {
        for (int i = 0; i < o.PidCount; i++) {
            PROC_ENTRY *p = proc_find(&list, pids[i]);
            if (!p || !proc_filter_match(&o.Filters, p)) {
                tu_eprint(L"ERROR: The process \"%lu\" not found.\n", pids[i]);
                missing++;
                continue;
            }
            any_target = TRUE;
            end_tree(&run, p, FALSE, 0, 0);
        }
    } else {
        /* the names are patterns; with only a filter every process is a candidate. */
        int patterns = o.NameCount ? o.NameCount : 1;
        for (int n = 0; n < patterns; n++) {
            const WCHAR *pattern = o.NameCount ? o.Names[n] : L"*";
            BOOL found = FALSE;
            for (DWORD i = 0; i < list.Count; i++) {
                PROC_ENTRY *p = &list.Items[i];
                if (!proc_name_match(pattern, p->Name) || !proc_filter_match(&o.Filters, p))
                    continue;
                if (p->Pid == 0 && !tu_eq(pattern, p->Name))
                    continue;
                found = TRUE;
                any_target = TRUE;
                end_tree(&run, p, TRUE, 0, 0);
            }
            if (!found) {
                if (o.NameCount)
                    tu_eprint(L"ERROR: The process \"%s\" not found.\n", pattern);
                else
                    tu_print(L"\nINFO: No tasks running with the specified criteria.\n");
                missing++;
            }
        }
    }
    (void)any_target;
    proc_list_free(&list);
    if (run.Failures)
        return 1;
    return missing ? 128 : 0;
}
