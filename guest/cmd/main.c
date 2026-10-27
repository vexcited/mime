/* cmd.exe: the command line of the interpreter, the interactive loop and the exit. */
#include "cmd.h"

static DWORD launch_input_mode;

static BOOL WINAPI ctrl_handler(DWORD type)
{
    if (type == CTRL_C_EVENT) {
        InterlockedExchange(&g.ctrlc, 1);
        return TRUE;
    }
    return FALSE;
}

static BOOL registry_flag(const WCHAR *name, BOOL fallback)
{
    static const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    BOOL result = fallback;
    /* the machine setting first, the user's replaces it. */
    for (int i = 1; i >= 0; i--) {
        HKEY key;
        if (RegOpenKeyExW(roots[i], L"Software\\Microsoft\\Command Processor", 0, KEY_READ, &key))
            continue;
        DWORD type, value = 0, size = sizeof(value);
        if (!RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
            result = value != 0;
        RegCloseKey(key);
    }
    return result;
}

static WCHAR *registry_autorun(HKEY root)
{
    HKEY key;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Command Processor", 0, KEY_READ, &key))
        return nullptr;
    DWORD type, size = 0;
    WCHAR *text = nullptr;
    if (!RegQueryValueExW(key, L"AutoRun", nullptr, &type, nullptr, &size) && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        WCHAR *raw = xalloc(size + 4);
        if (!RegQueryValueExW(key, L"AutoRun", nullptr, &type, (BYTE *)raw, &size) && raw[0]) {
            if (type == REG_EXPAND_SZ) {
                DWORD need = ExpandEnvironmentStringsW(raw, nullptr, 0);
                text = xalloc(((SIZE_T)need + 1) * sizeof(WCHAR));
                ExpandEnvironmentStringsW(raw, text, need);
            } else {
                text = xdup(raw);
            }
        }
        xfree(raw);
    }
    RegCloseKey(key);
    return text;
}

static void print_usage(void)
{
    out_s(L"Starts a new instance of the command interpreter.\r\n\r\n"
          L"CMD [/A | /U] [/Q] [/D] [/E:ON | /E:OFF] [/F:ON | /F:OFF] [/V:ON | /V:OFF]\r\n"
          L"    [[/S] [/C | /K] string]\r\n\r\n"
          L"/C      Carries out the command specified by string and then terminates\r\n"
          L"/K      Carries out the command specified by string but remains\r\n"
          L"/S      Modifies the treatment of string after /C or /K\r\n"
          L"/Q      Turns echo off\r\n"
          L"/D      Disable execution of AutoRun commands from registry\r\n"
          L"/A      Causes the output of internal commands to a pipe or file to be ANSI\r\n"
          L"/U      Causes the output of internal commands to a pipe or file to be Unicode\r\n"
          L"/E:ON   Enable command extensions\r\n"
          L"/E:OFF  Disable command extensions\r\n"
          L"/V:ON   Enable delayed environment variable expansion using ! as the delimiter\r\n"
          L"/V:OFF  Disable delayed environment variable expansion\r\n\r\n"
          L"If /C or /K is specified, then the remainder of the command line after the\r\n"
          L"switch is processed as a command line, where the following logic is used to\r\n"
          L"process quote (\") characters:\r\n\r\n"
          L"    1.  If all of the following conditions are met, then quote characters\r\n"
          L"        on the command line are preserved:\r\n\r\n"
          L"        - no /S switch\r\n"
          L"        - exactly two quote characters\r\n"
          L"        - no special characters between the two quote characters,\r\n"
          L"          where special is one of: &<>()@^|\r\n"
          L"        - there are one or more whitespace characters between the\r\n"
          L"          two quote characters\r\n"
          L"        - the string between the two quote characters is the name\r\n"
          L"          of an executable file.\r\n\r\n"
          L"    2.  Otherwise, old behavior is to see if the first character is\r\n"
          L"        a quote character and if so, strip the leading character and\r\n"
          L"        remove the last quote character on the command line, preserving\r\n"
          L"        any text after the last quote character.\r\n");
}

/* the text after /c or /k with the rules for its quotes applied. */
static WCHAR *command_text(const WCHAR *raw, BOOL strip_always)
{
    STR t;
    str_init(&t);
    str_adds(&t, skip_space(raw));
    if (!t.len || t.p[0] != L'"')
        return str_take(&t);
    BOOL keep = FALSE;
    if (!strip_always) {
        WCHAR *second = wcschr(t.p + 1, L'"');
        if (second && !wcschr(second + 1, L'"')) {
            WCHAR saved = second[1];
            second[1] = 0;
            BOOL special = wcspbrk(t.p, L"&<>@^|") != nullptr;
            BOOL space = FALSE;
            for (const WCHAR *c = t.p; *c; c++)
                if (is_space(*c))
                    space = TRUE;
            if (!special && space) {
                WCHAR *name = xdupn(t.p + 1, (SIZE_T)(second - t.p - 1));
                STR path;
                str_init(&path);
                int kind;
                if (!_wcsicmp(name, L"cmd") || find_program(name, &path, &kind))
                    keep = TRUE;
                str_free(&path);
                xfree(name);
            }
            second[1] = saved;
        }
    }
    if (!keep) {
        memmove(t.p, t.p + 1, t.len * sizeof(WCHAR));
        t.len--;
        WCHAR *last = wcsrchr(t.p, L'"');
        if (last) {
            memmove(last, last + 1, (wcslen(last + 1) + 1) * sizeof(WCHAR));
            t.len--;
        }
    }
    return str_take(&t);
}

typedef struct OPTIONS {
    WCHAR *command;
    BOOL keep_running; /* /k */
    BOOL no_autorun;
    BOOL help;
} OPTIONS;

static void parse_switches(const WCHAR *line, OPTIONS *o)
{
    /* the program name is the first word. */
    const WCHAR *p = line;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"')
            p++;
        if (*p)
            p++;
    } else {
        while (*p && !is_space(*p))
            p++;
    }
    BOOL strip_always = FALSE;
    while (*p) {
        const WCHAR *slash = wcschr(p, L'/');
        if (!slash)
            return;
        WCHAR c = towlower(slash[1]);
        p = slash + 2;
        if (!c)
            return;
        switch (c) {
        case L'?':
            o->help = TRUE;
            return;
        case L'q':
            g.quiet = TRUE;
            g.echo = FALSE;
            break;
        case L'c':
        case L'r':
            g.single = TRUE;
            o->command = command_text(p, strip_always);
            return;
        case L'k':
            o->keep_running = TRUE;
            o->command = command_text(p, strip_always);
            return;
        case L'u':
            g.unicode_out = TRUE;
            break;
        case L'a':
            g.unicode_out = FALSE;
            break;
        case L'x':
            g.ext = TRUE;
            break;
        case L'y':
            g.ext = FALSE;
            break;
        case L'e':
            if (*p == L':' && !_wcsnicmp(p + 1, L"off", 3)) {
                g.ext = FALSE;
                p += 4;
            } else {
                g.ext = TRUE;
                if (*p == L':' && !_wcsnicmp(p + 1, L"on", 2))
                    p += 3;
            }
            break;
        case L'v':
            if (*p == L':' && !_wcsnicmp(p + 1, L"off", 3)) {
                g.delayed = FALSE;
                p += 4;
            } else {
                g.delayed = TRUE;
                if (*p == L':' && !_wcsnicmp(p + 1, L"on", 2))
                    p += 3;
            }
            break;
        case L'd':
            o->no_autorun = TRUE;
            break;
        case L'f':
            if (*p == L':' && !_wcsnicmp(p + 1, L"off", 3))
                p += 4;
            else if (*p == L':' && !_wcsnicmp(p + 1, L"on", 2))
                p += 3;
            break;
        case L's':
            strip_always = TRUE;
            break;
        case L't':
            if (g.ext && *p == L':' && p[1] && p[2]) {
                int v[2];
                BOOL ok = TRUE;
                for (int i = 0; i < 2; i++) {
                    WCHAR d = p[1 + i];
                    if (is_digit(d))
                        v[i] = d - L'0';
                    else if ((d | 0x20) >= L'a' && (d | 0x20) <= L'f')
                        v[i] = (d | 0x20) - L'a' + 10;
                    else
                        ok = FALSE;
                }
                if (ok) {
                    g.default_color = (WORD)(v[0] << 4 | v[1]);
                    p += 3;
                }
            }
            break;
        default:
            break;
        }
    }
}

static void print_banner(void)
{
    STR v;
    str_init(&v);
    typedef LONG(NTAPI * RTL_GET_VERSION)(OSVERSIONINFOW *);
    OSVERSIONINFOW vi = {sizeof(vi)};
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RTL_GET_VERSION get = ntdll ? (RTL_GET_VERSION)(void *)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    if (get)
        get(&vi);
    DWORD revision = 0;
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key)) {
        DWORD type, size = sizeof(revision);
        if (RegQueryValueExW(key, L"UBR", nullptr, &type, (BYTE *)&revision, &size) || type != REG_DWORD)
            revision = 0;
        RegCloseKey(key);
    }
    out_fmt(L"Microsoft Windows [Version %lu.%lu.%lu.%lu]\r\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber,
            revision);
}

/* a line from the user: the console line editor, or the standard input of a pipe or file. */
static int interactive_loop(void)
{
    HANDLE in = g_io.h[0];
    for (;;) {
        g.goto_pending = FALSE;
        g.abort_all = FALSE;
        InterlockedExchange(&g.ctrlc, 0);
        if (g_io.con[0])
            SetConsoleMode(in, launch_input_mode);
        print_prompt();
        STR line;
        str_init(&line);
        BOOL got = read_stdin_line(&line, TRUE);
        if (!got) {
            if (g.ctrlc) {
                InterlockedExchange(&g.ctrlc, 0);
                out_s(L"^C\r\n");
                g.nul_node = TRUE;
                str_free(&line);
                continue;
            }
            str_free(&line);
            return 0;
        }
        const WCHAR *text = skip_space(str_get(&line));
        if (!*text) {
            g.nul_node = TRUE;
            str_free(&line);
            continue;
        }
        g.nul_node = FALSE;
        if (line.len > LINE_LIMIT) {
            err_s(L"The input line is too long.\r\n");
            str_free(&line);
            continue;
        }
        int r = exec_text(text, TRUE);
        (void)r;
        InterlockedExchange(&g.ctrlc, 0);
        str_free(&line);
    }
}

void entry(void)
{
    g.ext = registry_flag(L"EnableExtensions", TRUE);
    g.delayed = registry_flag(L"DelayedExpansion", FALSE);
    g.echo = TRUE;
    g.nul_node = TRUE;
    g.cmdline = xdup(GetCommandLineW());
    WCHAR self[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, self, NUM(self));
    g.self = xdupn(self, n);
    io_init_std();
    g.in_cp = GetConsoleCP();
    if (!g.in_cp)
        g.in_cp = GetOEMCP();
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    if (g_io.con[0])
        GetConsoleMode(g_io.h[0], &launch_input_mode);
    env_setup();
    OPTIONS o = {};
    parse_switches(g.cmdline, &o);
    if (o.help) {
        print_usage();
        ExitProcess(0);
    }
    if (!o.no_autorun) {
        static const HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
        for (int i = 0; i < 2; i++) {
            WCHAR *autorun = registry_autorun(roots[i]);
            if (autorun) {
                exec_text(autorun, TRUE);
                xfree(autorun);
            }
        }
    }
    int result = 0;
    if (o.command) {
        g.abort_all = FALSE;
        result = exec_text(o.command, TRUE);
        if (result < 0)
            result = 1;
        if (g.abort_all)
            result = 255;
        if (g.single && !o.keep_running) {
            while (g.pushd_count > 0)
                xfree(g.pushd[--g.pushd_count]);
            ExitProcess((UINT)result);
        }
    }
    if (!o.command) {
        print_banner();
        out_s(L"\r\n");
    }
    if (g_io.con[0] || !o.command || o.keep_running)
        g.interactive = TRUE;
    result = interactive_loop();
    ExitProcess(g.errorlevel);
}
