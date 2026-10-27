/* start: a program or document in its own window, or a new interpreter. */
#include "cmd.h"

BOOL create_child_ex(const WCHAR *program, const WCHAR *command_line, DWORD flags, HANDLE in, HANDLE out, HANDLE err,
                     BOOL use_std, const WCHAR *title, WORD show, BOOL use_show, const WCHAR *directory,
                     PROCESS_INFORMATION *pi);
BOOL shell_execute(const WCHAR *file, const WCHAR *parameters, const WCHAR *directory, int show, HANDLE *process);
void record_exit_code(DWORD code);
void report_launch_error(DWORD error, const WCHAR *name);
int image_subsystem(const WCHAR *path);

typedef struct START_OPTIONS {
    WCHAR *title;
    WCHAR *directory;
    BOOL wait, background, minimized, maximized;
    DWORD priority;
} START_OPTIONS;

/* a word at p, up to a blank, a quoted part is taken whole. */
static const WCHAR *token_end(const WCHAR *p)
{
    BOOL quoted = FALSE;
    while (*p && (quoted || !is_space(*p))) {
        if (*p == L'"')
            quoted = !quoted;
        p++;
    }
    return p;
}

static BOOL starts_switch(const WCHAR *p, const WCHAR *name, const WCHAR **after)
{
    if (*p != L'/')
        return FALSE;
    SIZE_T n = wcslen(name);
    if (_wcsnicmp(p + 1, name, n))
        return FALSE;
    const WCHAR *e = p + 1 + n;
    if (*e && !is_space(*e) && *e != L'/' && *e != L'"')
        return FALSE;
    *after = e;
    return TRUE;
}

int internal_start(const WCHAR *args, NODE *node)
{
    START_OPTIONS o = {};
    const WCHAR *p = skip_space(args);
    int result = 0;
    for (;;) {
        p = skip_space(p);
        if (!*p)
            break;
        const WCHAR *after;
        if (*p == L'"' && !o.title) {
            const WCHAR *e = p + 1;
            while (*e && *e != L'"')
                e++;
            o.title = xdupn(p + 1, (SIZE_T)(e - p - 1));
            p = *e ? e + 1 : e;
            continue;
        }
        if (*p != L'/')
            break;
        if (starts_switch(p, L"d", &after)) {
            const WCHAR *q = skip_space(after);
            const WCHAR *e = token_end(q);
            xfree(o.directory);
            o.directory = xdupn(q, (SIZE_T)(e - q));
            strip_quotes_inplace(o.directory);
            p = e;
        } else if (starts_switch(p, L"wait", &after)) {
            o.wait = TRUE;
            p = after;
        } else if (starts_switch(p, L"b", &after)) {
            o.background = TRUE;
            p = after;
        } else if (starts_switch(p, L"min", &after)) {
            o.minimized = TRUE;
            p = after;
        } else if (starts_switch(p, L"max", &after)) {
            o.maximized = TRUE;
            p = after;
        } else if (starts_switch(p, L"i", &after) || starts_switch(p, L"separate", &after) ||
                   starts_switch(p, L"shared", &after)) {
            p = after;
        } else if (starts_switch(p, L"low", &after)) {
            o.priority = IDLE_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"normal", &after)) {
            o.priority = NORMAL_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"high", &after)) {
            o.priority = HIGH_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"realtime", &after)) {
            o.priority = REALTIME_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"abovenormal", &after)) {
            o.priority = ABOVE_NORMAL_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"belownormal", &after)) {
            o.priority = BELOW_NORMAL_PRIORITY_CLASS;
            p = after;
        } else if (starts_switch(p, L"node", &after) || starts_switch(p, L"affinity", &after)) {
            const WCHAR *q = skip_space(after);
            p = token_end(q);
        } else if (p[1] == L'?') {
            out_s(L"Starts a separate window to run a specified program or command.\r\n\r\n"
                  L"START [\"title\"] [/D path] [/I] [/MIN] [/MAX] [/SEPARATE | /SHARED]\r\n"
                  L"      [/LOW | /NORMAL | /HIGH | /REALTIME | /ABOVENORMAL | /BELOWNORMAL]\r\n"
                  L"      [/NODE <NUMA node>] [/AFFINITY <hex affinity mask>] [/WAIT] [/B]\r\n"
                  L"      [command/program] [parameters]\r\n");
            xfree(o.title);
            xfree(o.directory);
            return 0;
        } else {
            const WCHAR *e = token_end(p);
            err_fmt(L"The system cannot accept the START command parameter %.*ls.\r\n", (int)(e - p), p);
            set_errorlevel(1);
            xfree(o.title);
            xfree(o.directory);
            return 1;
        }
    }
    DWORD flags = o.priority;
    WORD show = o.minimized ? SW_SHOWMINNOACTIVE : o.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    BOOL use_show = o.minimized || o.maximized;
    const WCHAR *program_text = p;
    PROCESS_INFORMATION pi = {};
    HANDLE process = nullptr;
    BOOL ok = FALSE;
    DWORD error = 0;
    WCHAR *command_line = nullptr;
    if (!*program_text) {
        /* nothing to run: a new interpreter. */
        STR line;
        str_init(&line);
        str_addc(&line, L'"');
        str_adds(&line, g.self);
        str_addc(&line, L'"');
        command_line = str_take(&line);
        flags |= o.background ? CREATE_NEW_PROCESS_GROUP : CREATE_NEW_CONSOLE;
        ok = create_child_ex(g.self, command_line, flags, g_io.h[0], g_io.h[1], g_io.h[2], o.background,
                             o.title, show, use_show, o.directory, &pi);
        error = GetLastError();
        if (ok)
            process = pi.hProcess;
    } else {
        /* the program word and the rest. */
        const WCHAR *e = token_end(program_text);
        WCHAR *word = xdupn(program_text, (SIZE_T)(e - program_text));
        strip_quotes_inplace(word);
        const WCHAR *rest = e;
        STR path;
        str_init(&path);
        int kind;
        const CMDDEF *internal = find_internal(word);
        BOOL found = !internal && find_program(word, &path, &kind);
        if (internal || (found && kind == KIND_BATCH)) {
            /* internal commands and batch files run in an interpreter of their own. */
            STR line;
            str_init(&line);
            str_addc(&line, L'"');
            str_adds(&line, g.self);
            str_adds(&line, L"\" /S /D ");
            str_adds(&line, g.delayed ? L"/V:ON " : L"/V:OFF ");
            str_adds(&line, L"/c\"");
            str_adds(&line, program_text);
            str_adds(&line, L"\"");
            command_line = str_take(&line);
            flags |= o.background ? CREATE_NEW_PROCESS_GROUP : CREATE_NEW_CONSOLE;
            ok = create_child_ex(g.self, command_line, flags, g_io.h[0], g_io.h[1], g_io.h[2], o.background, o.title,
                                 show, use_show, o.directory, &pi);
            error = GetLastError();
            if (ok)
                process = pi.hProcess;
        } else if (found && kind == KIND_EXE) {
            STR line;
            str_init(&line);
            str_adds(&line, program_text);
            command_line = str_take(&line);
            int subsystem = image_subsystem(path.p);
            if (subsystem != 2)
                flags |= o.background ? CREATE_NEW_PROCESS_GROUP : CREATE_NEW_CONSOLE;
            ok = create_child_ex(path.p, command_line, flags, g_io.h[0], g_io.h[1], g_io.h[2],
                                 o.background && subsystem != 2, o.title, show, use_show, o.directory, &pi);
            error = GetLastError();
            if (ok)
                process = pi.hProcess;
            else if (error == ERROR_BAD_EXE_FORMAT || error == ERROR_ELEVATION_REQUIRED) {
                ok = shell_execute(path.p, skip_space(rest), o.directory, show, &process);
                error = ok ? 0 : GetLastError();
            }
        } else if (wcspbrk(word, L".:\\/") || GetFileAttributesW(word) != INVALID_FILE_ATTRIBUTES) {
            /* a document, a folder, an address or a name the shell knows. */
            ok = shell_execute(word, skip_space(rest), o.directory, show, &process);
            error = ok ? 0 : GetLastError();
        } else {
            error = ERROR_FILE_NOT_FOUND;
        }
        if (!ok && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
            err_fmt(L"The system cannot find the file %ls.\r\n", word);
            set_errorlevel(9009);
            result = 1;
        } else if (!ok) {
            report_launch_error(error, word);
            result = 1;
        }
        str_free(&path);
        xfree(word);
    }
    if (!*program_text && !ok) {
        report_launch_error(error, g.self);
        result = 1;
    }
    if (ok) {
        if (pi.hThread)
            CloseHandle(pi.hThread);
        if (o.wait && process) {
            WaitForSingleObject(process, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            set_errorlevel(code);
            record_exit_code(code);
            result = (int)code;
        }
        if (process)
            CloseHandle(process);
    }
    xfree(command_line);
    xfree(o.title);
    xfree(o.directory);
    return result;
}
