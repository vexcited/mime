/* running statements: redirections, pipes, internal commands, batch files and programs. */
#include "cmd.h"

typedef struct REDIR_STATE {
    IO saved;
    HANDLE opened[16];
    int nopened;
    BOOL active;
} REDIR_STATE;

void set_errorlevel(DWORD code)
{
    g.errorlevel = code;
}

/* ------------------------------------------------------------------ expansions of a statement */

/* for variables, then delayed expansion. */
static BOOL expand_statement_text(const WCHAR *text, STR *out)
{
    STR stage;
    str_init(&stage);
    expand_forvars(text, &stage);
    if (g.delayed && wcschr(str_get(&stage), L'!')) {
        BOOL ok = expand_delayed(stage.p, out);
        str_free(&stage);
        return ok;
    }
    str_adds(out, str_get(&stage));
    str_free(&stage);
    return TRUE;
}

/* ------------------------------------------------------------------ redirection */

static HANDLE open_redirect_file(const WCHAR *name, REDIR_KIND kind, DWORD *error)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE h;
    if (kind == R_IN) {
        h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    } else if (kind == R_APPEND) {
        h = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER zero = {};
            SetFilePointerEx(h, zero, nullptr, FILE_END);
        }
    } else {
        h = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (h == INVALID_HANDLE_VALUE)
        *error = GetLastError();
    return h;
}

static void redirs_end(REDIR_STATE *st)
{
    if (!st->active)
        return;
    g_io = st->saved;
    for (int i = 0; i < st->nopened; i++)
        CloseHandle(st->opened[i]);
    st->nopened = 0;
    st->active = FALSE;
}

/* applies the redirections in order. FALSE when one fails, with the message printed and nothing left applied. */
static BOOL redirs_begin(REDIR *list, REDIR_STATE *st)
{
    st->nopened = 0;
    st->active = FALSE;
    if (!list)
        return TRUE;
    st->saved = g_io;
    st->active = TRUE;
    for (REDIR *r = list; r; r = r->next) {
        if (r->kind == R_DUP) {
            if (r->fd <= 2 && r->dup_fd <= 2)
                io_set(&g_io, r->fd, g_io.h[r->dup_fd]);
            continue;
        }
        STR target;
        str_init(&target);
        if (!expand_statement_text(r->target, &target)) {
            str_free(&target);
            redirs_end(st);
            return FALSE;
        }
        strip_quotes_inplace(target.p ? target.p : (WCHAR *)L"");
        DWORD error = 0;
        HANDLE h = open_redirect_file(str_get(&target), r->kind, &error);
        str_free(&target);
        if (h == INVALID_HANDLE_VALUE) {
            print_win_error(error);
            set_errorlevel(1);
            redirs_end(st);
            return FALSE;
        }
        if (st->nopened < (int)NUM(st->opened))
            st->opened[st->nopened++] = h;
        if (r->fd <= 2)
            io_set(&g_io, r->fd, h);
        else if (st->nopened > 0) {
            CloseHandle(h);
            st->nopened--;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ programs */

static BOOL ext_in_pathext(const WCHAR *ext, const WCHAR *pathext)
{
    SIZE_T n = wcslen(ext);
    const WCHAR *p = pathext;
    while (*p) {
        const WCHAR *e = wcschr(p, L';');
        SIZE_T len = e ? (SIZE_T)(e - p) : wcslen(p);
        if (len == n && !_wcsnicmp(p, ext, n))
            return TRUE;
        if (!e)
            break;
        p = e + 1;
    }
    return FALSE;
}

static int kind_of_file(const WCHAR *path, const WCHAR *pathext)
{
    const WCHAR *file = wcsrchr(path, L'\\');
    file = file ? file + 1 : path;
    const WCHAR *dot = wcsrchr(file, L'.');
    if (!dot)
        return KIND_OTHER;
    if (!_wcsicmp(dot, L".bat") || !_wcsicmp(dot, L".cmd"))
        return KIND_BATCH;
    if (ext_in_pathext(dot, pathext))
        return KIND_EXE;
    return KIND_OTHER;
}

static BOOL is_file(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/*
 * the file a command name runs: the name as given when it has a path, else the current directory and then each
 * folder of PATH, in each folder every extension of PATHEXT in order.
 */
BOOL find_program(const WCHAR *name, STR *path, int *kind)
{
    *kind = KIND_NONE;
    if (!name[0] || has_wildcards(name))
        return FALSE;
    STR pathext;
    str_init(&pathext);
    if (!g.ext || !env_get(L"PATHEXT", &pathext))
        str_adds(&pathext, L".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WS;.MSC");
    const WCHAR *exts = str_get(&pathext);
    BOOL has_path = wcspbrk(name, L"\\/:") != nullptr;
    const WCHAR *file = name;
    for (const WCHAR *p = name; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            file = p + 1;
    /* a bare directory name such as \ , .. or c:\dir\ changes the directory. */
    if (!*file || !wcscmp(file, L".") || !wcscmp(file, L"..")) {
        DWORD a = GetFileAttributesW(name);
        str_free(&pathext);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
            str_adds(path, name);
            *kind = KIND_DIR;
            return TRUE;
        }
        return FALSE;
    }
    BOOL has_ext = wcschr(file, L'.') != nullptr;
    STR dirs;
    str_init(&dirs);
    if (has_path) {
        str_addc(&dirs, L';');
    } else {
        STR no_cwd;
        str_init(&no_cwd);
        if (!env_get(L"NoDefaultCurrentDirectoryInExePath", &no_cwd))
            str_addc(&dirs, L'.');
        str_free(&no_cwd);
        STR pv;
        str_init(&pv);
        if (env_get(L"PATH", &pv)) {
            str_addc(&dirs, L';');
            str_adds(&dirs, str_get(&pv));
        }
        str_free(&pv);
    }
    BOOL found = FALSE;
    const WCHAR *d = dirs.p ? dirs.p : L"";
    for (;;) {
        const WCHAR *e = wcschr(d, L';');
        SIZE_T len = e ? (SIZE_T)(e - d) : wcslen(d);
        STR dir;
        str_init(&dir);
        str_addn(&dir, d, len);
        /* quotes around a folder of PATH are not part of its name. */
        strip_quotes_inplace(dir.p ? dir.p : (WCHAR *)L"");
        dir.len = dir.p ? wcslen(dir.p) : 0;
        STR base;
        str_init(&base);
        if (!has_path) {
            if (len || d == dirs.p) {
                str_adds(&base, str_get(&dir));
                if (base.len && base.p[base.len - 1] != L'\\')
                    str_addc(&base, L'\\');
            }
        }
        if (has_path || base.len || d == dirs.p) {
            STR candidate;
            str_init(&candidate);
            str_adds(&candidate, str_get(&base));
            str_adds(&candidate, name);
            if (has_ext && is_file(str_get(&candidate))) {
                str_adds(path, candidate.p);
                *kind = kind_of_file(candidate.p, exts);
                found = TRUE;
            }
            if (!found) {
                /* name plus each extension, in the order of PATHEXT. */
                const WCHAR *x = exts;
                while (*x && !found) {
                    const WCHAR *xe = wcschr(x, L';');
                    SIZE_T xl = xe ? (SIZE_T)(xe - x) : wcslen(x);
                    if (xl) {
                        STR full;
                        str_init(&full);
                        str_adds(&full, str_get(&candidate));
                        str_addn(&full, x, xl);
                        if (is_file(str_get(&full))) {
                            str_adds(path, full.p);
                            *kind = kind_of_file(full.p, exts);
                            found = TRUE;
                        }
                        str_free(&full);
                    }
                    if (!xe)
                        break;
                    x = xe + 1;
                }
            }
            str_free(&candidate);
        }
        str_free(&base);
        str_free(&dir);
        if (found || !e)
            break;
        d = e + 1;
    }
    str_free(&dirs);
    str_free(&pathext);
    return found;
}

/* the subsystem of a program file: 2 is windows, 3 console, 0 when it is not an image. */
int image_subsystem(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    unsigned char head[64];
    DWORD got;
    int result = 0;
    if (ReadFile(f, head, sizeof(head), &got, nullptr) && got == sizeof(head) && head[0] == 'M' && head[1] == 'Z') {
        LONG lfanew = *(LONG *)(head + 0x3C);
        LARGE_INTEGER at;
        at.QuadPart = lfanew;
        unsigned char nt[0x60];
        if (lfanew > 0 && SetFilePointerEx(f, at, nullptr, FILE_BEGIN) && ReadFile(f, nt, sizeof(nt), &got, nullptr) &&
            got == sizeof(nt) && nt[0] == 'P' && nt[1] == 'E')
            result = *(USHORT *)(nt + 24 + 68);
    }
    CloseHandle(f);
    return result;
}

static HANDLE inheritable(HANDLE h, BOOL *made)
{
    *made = FALSE;
    if (!h || h == INVALID_HANDLE_VALUE)
        return h;
    DWORD flags;
    if (GetHandleInformation(h, &flags) && (flags & HANDLE_FLAG_INHERIT))
        return h;
    HANDLE dup;
    if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        *made = TRUE;
        return dup;
    }
    return h;
}

typedef struct SPAWN {
    HANDLE in, out, err;
    PROCESS_INFORMATION pi;
} SPAWN;

BOOL create_child_ex(const WCHAR *program, const WCHAR *command_line, DWORD flags, HANDLE in, HANDLE out, HANDLE err,
                     BOOL use_std, const WCHAR *title, WORD show, BOOL use_show, const WCHAR *directory,
                     PROCESS_INFORMATION *pi)
{
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    BOOL made[3] = {};
    if (use_std) {
        si.hStdInput = inheritable(in, &made[0]);
        si.hStdOutput = inheritable(out, &made[1]);
        si.hStdError = inheritable(err, &made[2]);
        si.dwFlags = STARTF_USESTDHANDLES;
    }
    if (title)
        si.lpTitle = (WCHAR *)title;
    if (use_show) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = show;
    }
    SIZE_T n = wcslen(command_line);
    WCHAR *line = xalloc((n + 1) * sizeof(WCHAR));
    memcpy(line, command_line, n * sizeof(WCHAR));
    BOOL ok = CreateProcessW(program, line, nullptr, nullptr, TRUE, flags, nullptr, directory, &si, pi);
    DWORD error = ok ? 0 : GetLastError();
    xfree(line);
    if (made[0])
        CloseHandle(si.hStdInput);
    if (made[1])
        CloseHandle(si.hStdOutput);
    if (made[2])
        CloseHandle(si.hStdError);
    if (!ok)
        SetLastError(error);
    return ok;
}

static BOOL create_child(const WCHAR *program, const WCHAR *command_line, DWORD flags, HANDLE in, HANDLE out,
                         HANDLE err, const WCHAR *title, WORD show, BOOL use_show, const WCHAR *directory,
                         PROCESS_INFORMATION *pi)
{
    return create_child_ex(program, command_line, flags, in, out, err, TRUE, title, show, use_show, directory, pi);
}

typedef BOOL(WINAPI *SHELL_EXECUTE)(void *info);

/* starts a document or a program that CreateProcess refused, through the shell. */
BOOL shell_execute(const WCHAR *file, const WCHAR *parameters, const WCHAR *directory, int show,
                          HANDLE *process)
{
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    SHELL_EXECUTE exec = shell ? (SHELL_EXECUTE)(void *)GetProcAddress(shell, "ShellExecuteExW") : nullptr;
    if (!exec) {
        SetLastError(ERROR_NO_ASSOCIATION);
        return FALSE;
    }
    struct {
        DWORD cbSize;
        ULONG fMask;
        HWND hwnd;
        const WCHAR *lpVerb, *lpFile, *lpParameters, *lpDirectory;
        int nShow;
        HINSTANCE hInstApp;
        void *lpIDList;
        const WCHAR *lpClass;
        HKEY hkeyClass;
        DWORD dwHotKey;
        HANDLE hIcon;
        HANDLE hProcess;
    } info = {};
    info.cbSize = sizeof(info);
    info.fMask = 0x40 /* SEE_MASK_NOCLOSEPROCESS */ | 0x400 /* SEE_MASK_FLAG_NO_UI */;
    info.lpFile = file;
    info.lpParameters = parameters && parameters[0] ? parameters : nullptr;
    info.lpDirectory = directory;
    info.nShow = show;
    if (!exec(&info))
        return FALSE;
    *process = info.hProcess;
    return TRUE;
}

void record_exit_code(DWORD code)
{
    WCHAR hex[16];
    _snwprintf(hex, NUM(hex), L"%08X", code);
    env_set(L"=ExitCode", hex);
    WCHAR ascii[4] = {};
    if (code >= 32 && code <= 126)
        ascii[0] = (WCHAR)code;
    env_set(L"=ExitCodeAscii", ascii[0] ? ascii : nullptr);
}

void report_launch_error(DWORD error, const WCHAR *name)
{
    set_errorlevel(error);
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        err_fmt(L"The system cannot find the file %ls.\r\n", name);
        break;
    case ERROR_BAD_EXE_FORMAT:
        err_fmt(L"%ls is not a valid Win32 application.\r\n", name);
        break;
    case ERROR_ELEVATION_REQUIRED:
        err_s(L"The requested operation requires elevation.\r\n");
        break;
    default:
        print_win_error(error);
        break;
    }
}

/*
 * runs a program. a console program is waited for. a program of the windows subsystem is not waited for at the
 * prompt, only in a batch file, with /c, or when asked to.
 */
int spawn_external(const WCHAR *program, const WCHAR *command_line, DWORD flags, BOOL wait, const WCHAR *title,
                   WORD show, const WCHAR *directory, DWORD *exit_code, BOOL use_show)
{
    PROCESS_INFORMATION pi = {};
    BOOL ok = create_child(program, command_line, flags, g_io.h[0], g_io.h[1], g_io.h[2], title, show, use_show,
                           directory, &pi);
    HANDLE process = nullptr;
    if (ok) {
        CloseHandle(pi.hThread);
        process = pi.hProcess;
    } else {
        DWORD error = GetLastError();
        if ((g.ext && error == ERROR_BAD_EXE_FORMAT) || error == ERROR_ELEVATION_REQUIRED) {
            /* the program is not for CreateProcess, the shell knows what to do with it. */
            const WCHAR *args = command_line;
            if (*args == L'"') {
                args++;
                while (*args && *args != L'"')
                    args++;
                if (*args)
                    args++;
            } else {
                while (*args && !is_space(*args))
                    args++;
            }
            if (shell_execute(program, skip_space(args), directory, use_show ? show : SW_SHOWNORMAL, &process)) {
                ok = TRUE;
            }
        }
        if (!ok) {
            report_launch_error(GetLastError(), program);
            if (exit_code)
                *exit_code = 1;
            return 1;
        }
    }
    int subsystem = image_subsystem(program);
    BOOL detach = !wait && g.ext && !g.batch && !g.single && subsystem == 2;
    if (!process || detach) {
        if (process)
            CloseHandle(process);
        if (exit_code)
            *exit_code = 0;
        return 0;
    }
    WaitForSingleObject(process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    set_errorlevel(code);
    record_exit_code(code);
    if (exit_code)
        *exit_code = code;
    return (int)code;
}

/* ------------------------------------------------------------------ internal commands table */

static const CMDDEF internal_table[] = {
    {L"dir", internal_dir},       {L"erase", internal_del},   {L"del", internal_del},     {L"type", internal_type},
    {L"copy", internal_copy},     {L"cd", internal_cd},       {L"chdir", internal_cd},    {L"rename", internal_ren},
    {L"ren", internal_ren},       {L"echo", internal_echo},   {L"set", internal_set},     {L"pause", internal_pause},
    {L"date", internal_date},     {L"time", internal_time},   {L"prompt", internal_prompt},
    {L"md", internal_md},         {L"mkdir", internal_md},    {L"rd", internal_rd},       {L"rmdir", internal_rd},
    {L"path", internal_path},     {L"goto", internal_goto},   {L"shift", internal_shift}, {L"cls", internal_cls},
    {L"call", internal_call},     {L"verify", internal_verify}, {L"ver", internal_ver},   {L"vol", internal_vol},
    {L"exit", internal_exit},     {L"setlocal", internal_setlocal}, {L"endlocal", internal_endlocal},
    {L"title", internal_title},   {L"start", internal_start}, {L"dpath", internal_path},  {L"keys", internal_break},
    {L"move", internal_move},     {L"pushd", internal_pushd}, {L"popd", internal_popd},   {L"assoc", internal_assoc},
    {L"ftype", internal_ftype},   {L"break", internal_break}, {L"color", internal_color}, {L"mklink", internal_mklink},
};

const CMDDEF *find_internal(const WCHAR *name)
{
    for (SIZE_T i = 0; i < NUM(internal_table); i++)
        if (!_wcsicmp(name, internal_table[i].name))
            return &internal_table[i];
    return nullptr;
}

static BOOL command_name_stop(WCHAR c)
{
    return c == L'=' || c == L',' || c == L';' || c == L'+' || c == L'/' || c == L'[' || c == L']' || c == L' ' ||
           c == L'\t' || c == L'(';
}

/* the end of the name of the command an internal one is looked for under, and the name. */
static SIZE_T split_command_name(const WCHAR *text, STR *name)
{
    BOOL quoted = FALSE;
    SIZE_T i = 0;
    SIZE_T first_special = (SIZE_T)-1;
    for (;; i++) {
        WCHAR c = text[i];
        if (c == L'"')
            quoted = !quoted;
        else if (!c || (!quoted && command_name_stop(c)))
            break;
        if (first_special == (SIZE_T)-1 && (c == L':' || c == L'.' || c == L'\\')) {
            if (!g.ext)
                break;
            first_special = i;
        }
        str_addc(name, c);
    }
    str_get(name);
    if (first_special != (SIZE_T)-1) {
        /* a path to an existing file is no internal command, otherwise the name stops at the first separator. */
        if (!is_file(name->p)) {
            name->p[first_special] = 0;
            name->len = first_special;
            i = first_special;
        }
    }
    return i;
}

/* help for /? of an internal command, short. */
static BOOL show_help(const WCHAR *name, const WCHAR *args)
{
    const WCHAR *a = skip_space(args);
    if (a[0] != L'/' || a[1] != L'?')
        return FALSE;
    static const struct {
        const WCHAR *name;
        const WCHAR *text;
    } help[] = {
        {L"echo", L"Displays messages, or turns command-echoing on or off.\r\n\r\n  ECHO [ON | OFF]\r\n  ECHO [message]\r\n"},
        {L"cd", L"Displays the name of or changes the current directory.\r\n\r\nCD [/D] [drive:][path]\r\nCD [..]\r\n"},
        {L"chdir", L"Displays the name of or changes the current directory.\r\n\r\nCHDIR [/D] [drive:][path]\r\n"},
        {L"set", L"Displays, sets, or removes environment variables.\r\n\r\nSET [variable=[string]]\r\nSET /A expression\r\nSET /P variable=[promptString]\r\n"},
        {L"if", L"Performs conditional processing in batch programs.\r\n"},
        {L"for", L"Runs a specified command for each file in a set of files.\r\n"},
        {L"dir", L"Displays a list of files and subdirectories in a directory.\r\n\r\nDIR [drive:][path][filename] [/A[[:]attributes]] [/B] [/L] [/O[[:]sortorder]] [/S] [/W]\r\n"},
        {L"copy", L"Copies one or more files to another location.\r\n\r\nCOPY [/Y | /-Y] [/B | /A] source [+ source [+ ...]] [destination]\r\n"},
        {L"del", L"Deletes one or more files.\r\n\r\nDEL [/P] [/F] [/S] [/Q] [/A[[:]attributes]] names\r\n"},
        {L"type", L"Displays the contents of a text file or files.\r\n\r\nTYPE [drive:][path]filename\r\n"},
        {L"start", L"Starts a separate window to run a specified program or command.\r\n\r\nSTART [\"title\"] [/D path] [/MIN] [/MAX] [/WAIT] [/B] [command/program] [parameters]\r\n"},
    };
    for (SIZE_T i = 0; i < NUM(help); i++)
        if (!_wcsicmp(name, help[i].name)) {
            out_s(help[i].text);
            return TRUE;
        }
    out_fmt(L"%ls: see the documentation of the command.\r\n", name);
    return TRUE;
}

/* ------------------------------------------------------------------ one command */

static void change_drive(WCHAR letter)
{
    WCHAR root[4] = {upcase(letter), L':', L'\\', 0};
    UINT type = GetDriveTypeW(root);
    if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) {
        print_win_error(ERROR_INVALID_DRIVE);
        set_errorlevel(1);
        return;
    }
    STR dir;
    str_init(&dir);
    if (!drive_dir_get(letter, &dir))
        str_adds(&dir, root);
    if (!SetCurrentDirectoryW(str_get(&dir)))
        SetCurrentDirectoryW(root);
    str_free(&dir);
}

static int run_external(NODE *n, const WCHAR *text, BOOL as_call)
{
    STR name;
    str_init(&name);
    const WCHAR *p = text;
    BOOL quoted = FALSE;
    while (*p && (quoted || !(is_space(*p) || *p == L',' || *p == L';' || *p == L'='))) {
        if (*p == L'"')
            quoted = !quoted;
        else
            str_addc(&name, *p);
        p++;
    }
    str_get(&name);
    const WCHAR *args = p;
    int result = 1;
    /* the program named cmd is this one. */
    if (g.ext && !_wcsicmp(name.p, L"cmd") ) {
        DWORD code;
        result = spawn_external(g.self, text, 0, TRUE, nullptr, 0, nullptr, &code, FALSE);
        str_free(&name);
        return result;
    }
    STR path;
    str_init(&path);
    int kind;
    if (!find_program(name.p, &path, &kind)) {
        if (name.len == 2 && name.p[1] == L':' && !quoted) {
            change_drive(name.p[0]);
            result = 0;
        } else if (name.len == 0) {
            result = 0;
        } else {
            DWORD err = GetLastError();
            (void)err;
            msg_not_recognized(name.p);
            set_errorlevel(9009);
            result = 1;
        }
    } else if (kind == KIND_DIR) {
        change_directory(path.p, TRUE);
        result = 0;
    } else if (kind == KIND_BATCH) {
        result = run_batch_file(path.p, name.p, args, as_call);
    } else {
        DWORD code = 0;
        result = spawn_external(path.p, text, 0, TRUE, nullptr, 0, nullptr, &code, FALSE);
    }
    str_free(&path);
    str_free(&name);
    return result;
}

int run_cmd_node(NODE *n)
{
    BOOL as_call = g.call_flag;
    g.call_flag = FALSE;
    STR text;
    str_init(&text);
    if (!expand_statement_text(n->text ? n->text : L"", &text)) {
        str_free(&text);
        return 1;
    }
    REDIR_STATE rs;
    if (!redirs_begin(n->redirs, &rs)) {
        str_free(&text);
        return 1;
    }
    int result = 0;
    const WCHAR *t = str_get(&text);
    t = skip_space(t);
    if (!*t) {
        redirs_end(&rs);
        str_free(&text);
        return 0;
    }
    /* a drive letter alone switches drives. */
    if (t[0] && t[1] == L':' && (!t[2] || is_space(t[2])) && (!t[2] || !*skip_space(t + 2))) {
        change_drive(t[0]);
        redirs_end(&rs);
        str_free(&text);
        return 0;
    }
    STR name;
    str_init(&name);
    SIZE_T name_len = split_command_name(t, &name);
    const CMDDEF *def = name.len ? find_internal(name.p) : nullptr;
    if (def) {
        const WCHAR *args = t + name_len;
        if (!show_help(name.p, args))
            result = def->fn(args, n);
        else
            result = 0;
    } else {
        result = run_external(n, t, as_call);
    }
    str_free(&name);
    redirs_end(&rs);
    str_free(&text);
    return result;
}

/* ------------------------------------------------------------------ pipes */

/* the text of a statement as one line for another interpreter: line breaks become &. */
WCHAR *unparse_for_child(const WCHAR *src)
{
    STR out;
    str_init(&out);
    BOOL quoted = FALSE;
    for (const WCHAR *p = src; *p; p++) {
        if (*p == L'"')
            quoted = !quoted;
        if (*p == L'\n' && !quoted) {
            str_addc(&out, L'&');
            continue;
        }
        if (*p == L'\n')
            continue;
        str_addc(&out, *p);
    }
    return str_take(&out);
}

static void build_child_line(STR *line, const WCHAR *text)
{
    str_addc(line, L'"');
    str_adds(line, g.self);
    str_adds(line, L"\" /S /D ");
    str_adds(line, g.delayed ? L"/V:ON " : L"/V:OFF ");
    str_adds(line, g.ext ? L"/E:ON " : L"/E:OFF ");
    str_adds(line, L"/c\" ");
    str_adds(line, text);
    str_adds(line, L" \"");
}

typedef struct STAGE {
    NODE *node;
    HANDLE process;
} STAGE;

static void flatten_pipe(NODE *n, NODE **stages, int *count, int max)
{
    while (n->kind == N_PIPE && *count < max - 1) {
        stages[(*count)++] = n->a;
        n = n->b;
    }
    stages[(*count)++] = n;
}

static int exec_pipe(NODE *n)
{
    NODE *stages[32];
    int count = 0;
    flatten_pipe(n, stages, &count, NUM(stages));
    HANDLE processes[32] = {};
    HANDLE prev_read = nullptr;
    int failed = 0;
    for (int i = 0; i < count; i++) {
        HANDLE pipe_read = nullptr, pipe_write = nullptr;
        BOOL last = i == count - 1;
        if (!last) {
            SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
            if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0)) {
                print_win_error(GetLastError());
                failed = 1;
                break;
            }
        }
        NODE *s = stages[i];
        STR src;
        str_init(&src);
        STR expanded;
        str_init(&expanded);
        expand_forvars(s->src ? s->src : L"", &expanded);
        WCHAR *flat = unparse_for_child(str_get(&expanded));
        str_free(&expanded);
        STR line;
        str_init(&line);
        build_child_line(&line, flat);
        xfree(flat);
        str_free(&src);
        HANDLE in = i == 0 ? g_io.h[0] : prev_read;
        HANDLE out = last ? g_io.h[1] : pipe_write;
        PROCESS_INFORMATION pi = {};
        BOOL ok = create_child(g.self, str_get(&line), 0, in, out, g_io.h[2], nullptr, 0, FALSE, nullptr, &pi);
        str_free(&line);
        if (prev_read)
            CloseHandle(prev_read);
        if (pipe_write)
            CloseHandle(pipe_write);
        prev_read = pipe_read;
        if (!ok) {
            print_win_error(GetLastError());
            failed = 1;
            break;
        }
        CloseHandle(pi.hThread);
        processes[i] = pi.hProcess;
    }
    if (prev_read)
        CloseHandle(prev_read);
    DWORD code = 0;
    for (int i = 0; i < count; i++) {
        if (!processes[i])
            continue;
        WaitForSingleObject(processes[i], INFINITE);
        if (i == count - 1)
            GetExitCodeProcess(processes[i], &code);
        CloseHandle(processes[i]);
    }
    if (failed) {
        set_errorlevel(1);
        return 1;
    }
    set_errorlevel(code);
    return (int)code;
}

/* ------------------------------------------------------------------ statements */

int exec_node(NODE *n, BOOL top)
{
    if (!n || g.goto_pending || g.abort_all)
        return 0;
    int r;
    switch (n->kind) {
    case N_EMPTY:
        return 0;
    case N_QUIET:
        return exec_node(n->a, top);
    case N_SEQ:
        r = exec_node(n->a, FALSE);
        if (g.goto_pending || g.abort_all)
            return 0;
        return exec_node(n->b, FALSE);
    case N_AND:
        r = exec_node(n->a, FALSE);
        if (r == 0 && !g.goto_pending)
            r = exec_node(n->b, FALSE);
        return r;
    case N_OR:
        r = exec_node(n->a, FALSE);
        if (r != 0 && !g.goto_pending) {
            set_errorlevel((DWORD)r);
            r = exec_node(n->b, FALSE);
        }
        return r;
    case N_BLOCK: {
        REDIR_STATE rs;
        if (!redirs_begin(n->redirs, &rs))
            return 1;
        r = exec_node(n->a, FALSE);
        redirs_end(&rs);
        return r;
    }
    case N_IF:
        return exec_if(n);
    case N_FOR:
        return exec_for(n);
    case N_PIPE:
        return exec_pipe(n);
    case N_CMD:
        r = run_cmd_node(n);
        return r;
    }
    return 0;
}

/* a line typed at the prompt, given to /c or called. */
int exec_text(const WCHAR *line, BOOL top)
{
    STR expanded;
    str_init(&expanded);
    if (!expand_percent(line, wcslen(line), &expanded)) {
        str_free(&expanded);
        return 1;
    }
    int error = 0;
    NODE *node = parse_line(str_get(&expanded), nullptr, FALSE, &error);
    str_free(&expanded);
    if (error)
        return -1;
    if (!node)
        return 0;
    int r = exec_node(node, top);
    node_free(node);
    return r;
}

/* ------------------------------------------------------------------ output of a command */

int run_command_capture(const WCHAR *command, STR *output)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return 1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STR line;
    str_init(&line);
    str_addc(&line, L'"');
    str_adds(&line, g.self);
    str_adds(&line, L"\" /S /D /c\" ");
    str_adds(&line, command);
    str_adds(&line, L" \"");
    PROCESS_INFORMATION pi = {};
    BOOL ok = create_child(g.self, str_get(&line), 0, g_io.h[0], wr, g_io.h[2], nullptr, 0, FALSE, nullptr, &pi);
    str_free(&line);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return 1;
    }
    CloseHandle(pi.hThread);
    char buffer[4096];
    DWORD got;
    STR raw;
    str_init(&raw);
    unsigned char *bytes = nullptr;
    SIZE_T total = 0, cap = 0;
    while (ReadFile(rd, buffer, sizeof(buffer), &got, nullptr) && got) {
        if (total + got > cap) {
            cap = (total + got) * 2;
            bytes = xrealloc(bytes, cap);
        }
        memcpy(bytes + total, buffer, got);
        total += got;
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (total) {
        DWORD cp = g.out_cp;
        int chars = MultiByteToWideChar(cp, 0, (char *)bytes, (int)total, nullptr, 0);
        WCHAR *w = xalloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
        MultiByteToWideChar(cp, 0, (char *)bytes, (int)total, w, chars);
        str_addn(output, w, (SIZE_T)chars);
        xfree(w);
    }
    xfree(bytes);
    str_free(&raw);
    str_get(output);
    return (int)code;
}
